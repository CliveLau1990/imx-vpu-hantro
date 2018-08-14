/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2006 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
--
--  Description : Rate control
--
------------------------------------------------------------------------------*/
#include "vp8ratecontrol.h"
#include "vp8quanttable.h"

/*------------------------------------------------------------------------------
  Module defines
------------------------------------------------------------------------------*/

#ifdef TRACE_RC
#include <stdio.h>
FILE *fpRcTrc = NULL;
/* Select debug output: fpRcTrc (==file rc.trc) or stdout */
#define DBGOUTPUT fpRcTrc
/* Select debug level: 0 = minimum, 2 = maximum */
#define DBG_LEVEL 2
#define DBG(l, str) if (l <= DBG_LEVEL) fprintf str
#else
#define DBG(l, str)
#endif

#define INITIAL_BUFFER_FULLNESS   60    /* Decoder Buffer in procents */
#define MIN_PIC_SIZE              50    /* Procents from picPerPic */

#define DIV(a, b)               (((a) + (SIGN(a) * (b)) / 2) / (b))
#define DSCY                    32  /* scaler for linear regression */
#define DSCBITPERMB             128 /* bitPerMb scaler  */
#define I32_MAX                 2147483647 /* 2 ^ 31 - 1 */
#define QP_DELTA                4
#define QP_DELTA_LIMIT          30
#define RC_ERROR_RESET          0x7fffffff

/*------------------------------------------------------------------------------
  Local structures
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
  Local function prototypes
------------------------------------------------------------------------------*/

static i32 InitialQp(i32 bits, i32 pels);
static void PicSkip(vp8RateControl_s * rc, u32 layerId);
static void PicQuantLimit(vp8RateControl_s * rc);
static i32 VirtualBuffer(vp8VirtualBuffer_s *vb, i32 timeInc);
static void PicQuant(vp8RateControl_s * rc, u32 layerId);
static i32 avg_rc_error(linReg_s *p);
static void update_rc_error(linReg_s *p, i32 bits, i32 windowLen);
static i32 gop_avg_qp(vp8RateControl_s *rc);
static i32 new_pic_quant(linReg_s *p, i32 bits, i32 prev_qp, true_e useQpDeltaLimit);
static i32 get_avg_bits(linReg_s *p, i32 n);
static void update_tables(linReg_s *p, i32 qp, i32 bits);
static void update_model(linReg_s *p);
static i32 lin_sy(i32 *qp, i32 *r, i32 n);
static i32 lin_sx(i32 *qp, i32 n);
static i32 lin_sxy(i32 *qp, i32 *r, i32 n);
static i32 lin_nsxx(i32 *qp, i32 n);

/*------------------------------------------------------------------------------
  VP8InitRc() Initialize rate control.
------------------------------------------------------------------------------*/
void VP8InitRc(vp8RateControl_s * rc, u32 newStream)
{
    vp8VirtualBuffer_s *vb = rc->virtualBuffer;
    i32 maxBps, totalBps = 0;
    u32 i;

#if defined(TRACE_RC) && (DBGOUTPUT == fpRcTrc)
    if (!fpRcTrc) fpRcTrc = fopen("rc.trc", "w");
#endif

    if (rc->qpMax >= QINDEX_RANGE)
        rc->qpMax = QINDEX_RANGE-1;

    if (rc->qpMin < 0)
        rc->qpMin = 0;

    rc->layerAmount=0;
    for (i = 0; i < 4; i++) {
        totalBps += vb[i].bitRate;
        if (vb[i].bitRate) rc->layerAmount++;
    }

    /* Limit bitrate settings that are way over head.
     * Maximum limit is half of the uncompressed YUV bitrate (12bpp). */
    maxBps = rc->mbPerPic*16*16*6;   /* Max bits per frame */
    maxBps = VP8Calculate(maxBps, rc->outRateNum, rc->outRateDenom);
    if (maxBps < 0)
        maxBps = I32_MAX;
    if (totalBps > maxBps) {
        /* Trying to set too high bitrate, reduce each layer with same factor */
        for (i = 0; i < rc->layerAmount; i++) {
            vb[i].bitRate = VP8Calculate(vb[i].bitRate, maxBps, totalBps);
        }
    }

    for (i = 0; i < rc->layerAmount; i++) {
        vb[i].bitPerPic = VP8Calculate(vb[i].bitRate, vb[i].outRateDenom,
            rc->outRateNum);
    }

    /* QP -1: Initial QP estimation done by RC. Using the base layer bitrate
     * to make intra frames smaller for RTC. Could use totalBps as well. */
    if (rc->qpHdr == -1)
        rc->qpHdr = InitialQp(vb[0].bitPerPic, rc->mbPerPic*16*16);

    PicQuantLimit(rc);

    DBG(0, (DBGOUTPUT, "\nInitRc:\n  picRc\t\t%i\n  picSkip\t%i\n",
                     rc->picRc, rc->picSkip));
    DBG(0, (DBGOUTPUT, "  qpHdr\t\t%i\n  qpMin,Max\t%i,%i\n",
                     rc->qpHdr, rc->qpMin, rc->qpMax));

    for (i = 0; i < rc->layerAmount; i++) {
        DBG(0, (DBGOUTPUT, "L%d BitRate\t%8i  BitPerPic\t%8i\n",
            i, vb[i].bitRate, vb[i].bitPerPic));
    }

    rc->qpHdrPrev       = rc->qpHdr;
    rc->fixedQp         = rc->qpHdr;

    /* If changing QP between frames don't reset GOP RC.
     * Changing bitrate resets RC the same way as new stream. */
    if (!newStream)
        return;

    rc->frameCoded      = ENCHW_YES;
    rc->currFrameIntra  = 1;
    rc->prevFrameIntra  = 0;
    rc->gopQpSum        = 0;
    rc->gopQpDiv        = 0;
    rc->targetPicSize   = 0;
    rc->frameBitCnt     = 0;
    rc->intraIntervalCtr = rc->intraInterval = rc->windowRem = rc->windowLen;

    EWLmemset(&rc->linReg, 0, sizeof(linReg_s));
    rc->linReg.qs[0]    = AcQLookup[QINDEX_RANGE-1];
    rc->linReg.qp_prev  = rc->qpHdr;

    for (i = 0; i < 4; i++) {
        vb[i].picTimeInc      = 0;
        vb[i].realBitCnt      = 0;
        vb[i].virtualBitCnt   = 0;
        vb[i].qpPrev          = rc->qpHdr;
        vb[i].timeScale       = rc->outRateNum;
        update_rc_error(&rc->rError[i], RC_ERROR_RESET, 0);
    }

}

/*------------------------------------------------------------------------------
  InitialQp()   Returns sequence initial quantization parameter based on the
                configured resolution and bitrate.
------------------------------------------------------------------------------*/
static i32 InitialQp(i32 bits, i32 pels)
{
    /* Experimental table for choosing a QP based on target bits/pixel.
     * The table is calculated by encoding a set of 4CIF resolution video
     * clips with fixed QP and using the average bitsPerFrame in the equation:
     * (bitPerFrame/(290 - 120/8*11)*8000/1584) */
    const i32 qp_tbl[2][11] = {
        {604, 785, 1015, 1300, 1653, 2217, 2997, 3988, 5899, 9023, 0x7FFFFFFF},
        {110, 100, 90, 80, 70, 60, 50, 40, 30, 20, 10}};
    const i32 upscale = 8000;
    i32 i = -1;
    i32 b = 0;

    /* prevents overflow, QP would anyway be 10 with this high bitrate
       for all resolutions under and including 1920x1088 */
    if (bits > 1000000)
        return 10;

    /* Pixels => macroblocks */
    pels >>= 8;

    if (!bits || !pels)
        return 127;

    /* Make resolution linear by counting number of bits */
    while ((pels >> b) > 0)
        b++;

    if (b > 19)
        return 127;

    /* Adjust the bits value for the current resolution */
    b = 290 - b*120/8;
    bits /= b;
    bits = VP8Calculate(bits, upscale, pels);

    DBG(0, (DBGOUTPUT, "BitsPerMb\t\t%8d  bits\t%8d\n",
                bits*b/upscale, bits));

    /* Use maximum QP if bitrate way too low. */
    if (bits < qp_tbl[0][0]/3)
        return 127;

    while (qp_tbl[0][++i] < bits);

    /* Adjust the first frame QP a bit lower than average. */
    return 9*qp_tbl[1][i]/10;
}

/*------------------------------------------------------------------------------
  VirtualBuffer()  Return difference of target and real buffer fullness.
  Virtual buffer and real bit count grow until one second.  After one second
  output bit rate per second is removed from virtualBitCnt and realBitCnt. Bit
  drifting has been taken care.

  If the leaky bucket in VBR mode becomes empty (e.g. underflow), those R * T_e
  bits are lost and must be decremented from virtualBitCnt. (NOTE: Drift
  calculation will mess virtualBitCnt up, so the loss is added to realBitCnt)
------------------------------------------------------------------------------*/
static i32 VirtualBuffer(vp8VirtualBuffer_s *vb, i32 timeInc)
{
    i32 target;

    vb->picTimeInc    += timeInc;
    /* picTimeInc must be in range of [0, timeScale) */
    while (vb->picTimeInc >= vb->timeScale) {
        vb->picTimeInc    -= vb->timeScale;
        vb->realBitCnt    -= vb->bitRate;
        vb->seconds++;
        vb->averageBitRate = vb->bitRate + vb->realBitCnt/vb->seconds;
    }
    /* virtualBitCnt grows until one second, this fixes bit drifting
     * when bitPerPic is rounded to integer. */
    vb->virtualBitCnt = VP8Calculate(vb->bitRate, vb->picTimeInc, vb->timeScale);

    /* Saturate realBitCnt, this is to prevent overflows caused by much greater
       bitrate setting than is really possible to reach */
    if (vb->realBitCnt > 0x1FFFFFFF)
        vb->realBitCnt = 0x1FFFFFFF;
    if (vb->realBitCnt < -0x1FFFFFFF)
        vb->realBitCnt = -0x1FFFFFFF;

    target = vb->virtualBitCnt - vb->realBitCnt;

    /* Saturate target, prevents rc going totally out of control.
       This situation should never happen. */
    if (target > 0x1FFFFFFF)
        target = 0x1FFFFFFF;
    if (target < -0x1FFFFFFF)
        target = -0x1FFFFFFF;

    DBG(1, (DBGOUTPUT, "virtualBitCnt:\t%8i  realBitCnt:\t%8i",
                vb->virtualBitCnt, vb->realBitCnt));
    DBG(1, (DBGOUTPUT, "  diff bits:\t%8i  avg bitrate:\t%8i\n", target,
                vb->averageBitRate));

    return target;
}

/*------------------------------------------------------------------------------
  VP8BeforePicRc()  Update virtual buffer and calculate picInitQp for current
  picture.
------------------------------------------------------------------------------*/
void VP8BeforePicRc(vp8RateControl_s * rc, u32 timeInc, u32 frameTypeIntra,
                    u32 goldenRefresh, i32 boostPct, u32 layerId)
{
    u32 i;
    i32 rcWindow, biterr = 0, intraBits = 0, tmp = 0;
    i32 bitPerPic = rc->virtualBuffer[layerId].bitPerPic;

    ASSERT(layerId <= 3);

    rc->frameCoded = ENCHW_YES;
    rc->currFrameIntra = frameTypeIntra;
    rc->currFrameGolden = goldenRefresh;

    /* Store the start QP, before any adjustment */
    rc->qpHdrPrev2 = rc->qpHdr;

    DBG(1, (DBGOUTPUT, "\nBEFORE PIC RC: pic=%d\n", rc->frameCnt));
    DBG(1, (DBGOUTPUT, "Frame type:\t%8i  timeInc:\t%8i  layerId:\t%8i\n",
                frameTypeIntra, timeInc, layerId));

    if (!layerId) {
        if (rc->currFrameIntra || rc->windowRem == 1) {
            rc->windowRem = rc->windowLen;
        } else {
            rc->windowRem--;
        }
    }

    /* Update the virtual buffer for each layer. Use the virtual buffer for
     * this frame's layer to calculate the difference of target bitrate
     * and actual bitrate. */
    for (i = 0; i < rc->layerAmount; i++) {
        DBG(1, (DBGOUTPUT, "virtualBuffer[%d]\n", i));
        tmp = VirtualBuffer(&rc->virtualBuffer[i], (i32) timeInc);
        if (i == layerId)
            biterr = tmp;
    }

    /* Calculate target size for this picture. Adjust the target bitPerPic
     * with the cumulated error between target and actual bitrates (biterr).
     * For base layer also take into account the bits used by intra frame
     * starting the GOP. */
    if (!layerId && !rc->currFrameIntra && rc->intraInterval > 1) {
        /* GOP bits that are used by intra frame. Amount of bits
         * "stolen" by intra from each inter frame in the GOP. */
        intraBits = bitPerPic*rc->intraInterval*get_avg_bits(&rc->gop, 10)/100;
        intraBits -= bitPerPic;
        intraBits /= (rc->intraInterval-1);
        intraBits = MAX(0, intraBits);
    }

    /* Compensate the virtual bit buffer for intra frames "stealing" bits
     * from inter frames because after intra frame the bit buffer seems
     * to overflow but the following inters will level the buffer. */
    biterr += intraBits*(rc->intraInterval-rc->intraIntervalCtr);

    /* The window controls how fast the average bitrate should match the
     * target. We can be fairly easy with this one, let's make it
     * a moving window to smoothen the changes. */
    rcWindow = MAX(1, rc->windowLen);

    rc->targetPicSize = bitPerPic - intraBits + DIV(biterr, rcWindow);
    /* Limit the target to a realistic minimum that can be reached.
     * Setting target lower than this will confuse RC because it can never
     * be reached. */
    rc->targetPicSize = MAX(rc->mbPerPic*2/3, rc->targetPicSize);

    if(rc->picSkip)
        PicSkip(rc, layerId);

    /* determine initial quantization parameter for current picture */
    PicQuant(rc, layerId);
    /* quantization parameter user defined limitations */
    PicQuantLimit(rc);

    rc->qpHdrPrev = rc->qpHdr;
    rc->virtualBuffer[layerId].qpPrev = rc->qpHdr;

    if(rc->currFrameIntra) {
        if(rc->fixedIntraQp)
            rc->qpHdr = rc->fixedIntraQp;
        else if (!rc->prevFrameIntra)
        {            
            rc->qpHdr += rc->intraQpDelta;
        }

        /* quantization parameter user defined limitations still apply */
        PicQuantLimit(rc);
        /* New intra interval, intra frames should be layerId=0 */
        if (rc->intraIntervalCtr > 1)
            rc->intraInterval = rc->intraIntervalCtr;
        rc->intraIntervalCtr = 1;
    } else {
        /* trace the QP over GOP, excluding Intra QP */
        rc->gopQpSum += rc->qpHdr;
        rc->gopQpDiv++;

        /* Calculate the interval of intra frames for base layer. */
        if (!layerId) {
            rc->intraIntervalCtr++;
            /* Check that interval is repeating */
            if (rc->intraIntervalCtr > rc->intraInterval)
                rc->intraInterval = rc->intraIntervalCtr;
        }
    }

    /* Apply golden boost */    
    if(rc->currFrameGolden && rc->adaptiveGoldenBoost)
    {
        /* Boost by percentage */
        rc->qpHdr = (1000-boostPct)*rc->qpHdr / 1000;
    }
               
    DBG(0, (DBGOUTPUT, "Frame coded\t%8d  ", rc->frameCoded));
    DBG(0, (DBGOUTPUT, "Frame qpHdr\t%8d  ", rc->qpHdr));
    DBG(0, (DBGOUTPUT, "GopRem:\t%8d  ", rc->windowRem));
    DBG(0, (DBGOUTPUT, "Target bits:\t%8d  ", rc->targetPicSize));
    DBG(1, (DBGOUTPUT, "\nintraBits:\t%8d  ", intraBits));
    DBG(1, (DBGOUTPUT, "bufferComp:\t%8d  ", DIV(biterr, rcWindow)));
    DBG(1, (DBGOUTPUT, "Rd:\t\t%8d\n", avg_rc_error(&rc->rError[layerId])));

}

/*------------------------------------------------------------------------------
  VP8AfterPicRc()  Update RC statistics after encoding frame.
------------------------------------------------------------------------------*/
void VP8AfterPicRc(vp8RateControl_s * rc, u32 byteCnt, u32 layerId)
{
    vp8VirtualBuffer_s *vb = &rc->virtualBuffer[layerId];
    i32 tmp = 0, bitCnt = (i32)byteCnt * 8;

    ASSERT(layerId <= 3);

    rc->frameCnt++;
    rc->frameBitCnt     = bitCnt;
    vb->realBitCnt      += bitCnt;
    if (!layerId)
        rc->gopBitCnt   += bitCnt;  /* GOP is used only for base layer */

    /* Apply golden boost */    
    if(rc->currFrameGolden)
        rc->qpHdrGolden = rc->qpHdr;
    
    if (rc->targetPicSize)
        tmp = ((bitCnt - rc->targetPicSize) * 100) / rc->targetPicSize;

    /* Stats */
    rc->sumQp += rc->qpHdr;
    rc->sumBitrateError += ABS(vb->realBitCnt-vb->virtualBitCnt);
    rc->sumFrameError += ABS(bitCnt-rc->targetPicSize);

    DBG(1, (DBGOUTPUT, "\nAFTER PIC RC:\n"));
    DBG(0, (DBGOUTPUT, "BitCnt\t\t%8d\n", bitCnt));
    DBG(1, (DBGOUTPUT, "BitErr/avg\t%7d%%  ",
                ((bitCnt - vb->bitPerPic) * 100) / (vb->bitPerPic+1)));
    DBG(1, (DBGOUTPUT, "BitErr/target\t%7d%%\n", tmp));
    DBG(1, (DBGOUTPUT, "Average: QP\t%8d  ", rc->sumQp/rc->frameCnt));
    DBG(1, (DBGOUTPUT, "Bitrate err\t%8d  ", rc->sumBitrateError/rc->frameCnt));
    DBG(1, (DBGOUTPUT, "Framesize err\t%8d\n", rc->sumFrameError/rc->frameCnt));

    /* Update number of bits used for residual, inter or intra */
    if (!rc->currFrameIntra) {
        tmp = VP8Calculate(bitCnt, DSCBITPERMB, rc->mbPerPic);
        update_tables(&rc->linReg, rc->qpHdrPrev, tmp);
        update_model(&rc->linReg);

        if (rc->windowRem == rc->windowLen-1) {
            /* First INTER frame of GOP */
            update_rc_error(&rc->rError[layerId], RC_ERROR_RESET, rc->windowLen);
        } else {
            /* Store the error between target and actual frame size
             * Saturate the error to avoid inter frames with
             * mostly intra MBs to affect too much. */
            update_rc_error(&rc->rError[layerId],
                    MIN(bitCnt - rc->targetPicSize, 2*rc->targetPicSize),
                    rc->windowLen);
        }
    } else {    /* Intra frame */
        tmp = VP8Calculate(bitCnt, DSCBITPERMB, rc->mbPerPic);
        update_tables(&rc->intra, rc->qpHdrPrev, tmp);
        update_model(&rc->intra);

        /* Store intra error when encoding only intra-frames. */
        if (!rc->prevFrameIntra) {
            update_rc_error(&rc->intraError, RC_ERROR_RESET, rc->windowLen);
        } else {
            /* Store the error between target and actual frame size. */
            update_rc_error(&rc->intraError,
                    MIN(bitCnt - rc->targetPicSize, 2*rc->targetPicSize),
                    rc->windowLen);
        }
    }

    DBG(1, (DBGOUTPUT, "plot\t%7i %8i  %7i\t%8i  %7i\t%8i  %7i\t%8i\n",
            rc->frameCnt, rc->qpHdr, rc->targetPicSize, bitCnt,
            vb->bitPerPic, rc->gopAvgBitCnt, vb->realBitCnt-vb->virtualBitCnt,
            vb->bitRate));

    rc->prevFrameIntra  = rc->currFrameIntra;
}

/*------------------------------------------------------------------------------
  PicSkip()  Decrease framerate if not enough bits available.
------------------------------------------------------------------------------*/
void PicSkip(vp8RateControl_s * rc, u32 layerId)
{
    vp8VirtualBuffer_s *vb = &rc->virtualBuffer[layerId];
    i32 bitAvailable = vb->virtualBitCnt - vb->realBitCnt;
    i32 skipIncLimit = -vb->bitPerPic / 3;
    i32 skipDecLimit = vb->bitPerPic / 3;

    /* When frameRc is enabled, skipFrameTarget is not allowed to be > 1
     * This makes sure that not too many frames is skipped and lets
     * the frameRc adjust QP instead of skipping many frames */
    if (((rc->picRc == ENCHW_NO) || (vb->skipFrameTarget == 0)) &&
       (bitAvailable < skipIncLimit))
        vb->skipFrameTarget++;

    if ((bitAvailable > skipDecLimit) && vb->skipFrameTarget > 0)
        vb->skipFrameTarget--;

    if (vb->skippedFrames < vb->skipFrameTarget) {
        vb->skippedFrames++;
        rc->frameCoded = ENCHW_NO;
    } else {
        vb->skippedFrames = 0;
    }
}

/*------------------------------------------------------------------------------
  PicQuant()  Calculate quantization parameter for next frame. In the beginning
                of GOP use previous GOP average QP and otherwise find new QP
                using the target size and previous frames QPs and bit counts.
------------------------------------------------------------------------------*/
void PicQuant(vp8RateControl_s * rc, u32 layerId)
{
    i32 normBits, targetBits;
    i32 qpPrev = rc->virtualBuffer[layerId].qpPrev;
    true_e useQpDeltaLimit = ENCHW_YES;

    if(rc->picRc != ENCHW_YES) {
        rc->qpHdr = rc->fixedQp;
        DBG(2, (DBGOUTPUT,
                "R/cx:\t\t    xxxx  QP:\t\t   xx xx  D:\t\t    xxxx  newQP: xx\n"));
        return;
    }

    /* Determine initial quantization parameter for current picture */
    if (rc->currFrameIntra) {
        /* Default intra QP == average of prev frame and prev GOP average */
        rc->qpHdr = (qpPrev + gop_avg_qp(rc) + 1)/2;
        /* If all frames are intra we calculate new QP
         * for intra the same way as for inter */
        if (rc->prevFrameIntra) {
            targetBits = rc->targetPicSize - avg_rc_error(&rc->intraError);
            targetBits = CLIP3(targetBits, 0, 2*rc->targetPicSize);
            normBits = VP8Calculate(targetBits, DSCBITPERMB, rc->mbPerPic);
            rc->qpHdr = new_pic_quant(&rc->intra, normBits, qpPrev, useQpDeltaLimit);
        } else {
            DBG(2, (DBGOUTPUT,
                "R/cx:\t\t    xxxx  QP:\t\t   xx xx  D:\t\t    xxxx  newQP: xx\n"));
        }
    } else {
        /* Calculate new QP by matching to previous frames R-Q curve */
        targetBits = rc->targetPicSize - avg_rc_error(&rc->rError[layerId]);
        targetBits = CLIP3(targetBits, 0, 2*rc->targetPicSize);
        normBits = VP8Calculate(targetBits, DSCBITPERMB, rc->mbPerPic);
        rc->qpHdr = new_pic_quant(&rc->linReg, normBits, qpPrev, useQpDeltaLimit);
    }
}

/*------------------------------------------------------------------------------
  PicQuantLimit()
------------------------------------------------------------------------------*/
void PicQuantLimit(vp8RateControl_s * rc)
{
    rc->qpHdr = MIN(rc->qpMax, MAX(rc->qpMin, rc->qpHdr));
}

/*------------------------------------------------------------------------------
  Calculate()  I try to avoid overflow and calculate good enough result of a*b/c
------------------------------------------------------------------------------*/
i32 VP8Calculate(i32 a, i32 b, i32 c)
{
    u32 left = 32;
    u32 right = 0;
    u32 shift;
    i32 sign = 1;
    i32 tmp;

    if(a == 0 || b == 0)
    {
        return 0;
    }
    else if((a * b / b) == a && c != 0)
    {
        return (a * b / c);
    }
    if(a < 0)
    {
        sign = -1;
        a = -a;
    }
    if(b < 0)
    {
        sign *= -1;
        b = -b;
    }
    if(c < 0)
    {
        sign *= -1;
        c = -c;
    }

    if(c == 0 )
    {
        return 0x7FFFFFFF * sign;
    }

    if(b > a)
    {
        tmp = b;
        b = a;
        a = tmp;
    }

    for(--left; (((u32)a << left) >> left) != (u32)a; --left);
    left--; /* unsigned values have one more bit on left,
               we want signed accuracy. shifting signed values gives
               lint warnings */

    while(((u32)b >> right) > (u32)c)
    {
        right++;
    }

    if(right > left)
    {
        return 0x7FFFFFFF * sign;
    }
    else
    {
        shift = left - right;
        return (i32)((((u32)a << shift) / (u32)c * (u32)b) >> shift) * sign;
    }
}

/*------------------------------------------------------------------------------
  avg_rc_error()  PI(D)-control for rate prediction error.
------------------------------------------------------------------------------*/
static i32 avg_rc_error(linReg_s *p)
{
    if (ABS(p->bits[2]) < 0xFFFFFFF && ABS(p->bits[1]) < 0xFFFFFFF)
        return DIV(p->bits[2] * 4 + p->bits[1] * 2 + p->bits[0] * 0, 100);

    /* Avoid overflow */
    return VP8Calculate(p->bits[2], 4, 100) +
           VP8Calculate(p->bits[1], 2, 100);
}

/*------------------------------------------------------------------------------
  update_rc_error()  Update PI(D)-control values
------------------------------------------------------------------------------*/
static void update_rc_error(linReg_s *p, i32 bits, i32 windowLen)
{
    p->len = 3;

    if (bits == (i32)RC_ERROR_RESET) {
        /* RESET */
        p->bits[0] = 0;
        if (windowLen)  /* Store the average error of previous GOP. */
            p->bits[1] = p->bits[1]/windowLen;
        else
            p->bits[1] = 0;
        p->bits[2] = 0;
        DBG(2, (DBGOUTPUT, "P\t\t     ---  I\t\t     ---  D\t\t     ---\n"));
        return;
    }

    p->bits[0] = bits - p->bits[2]; /* Derivative */
    if ((bits > 0) && (bits + p->bits[1] > p->bits[1]))
        p->bits[1] = bits + p->bits[1]; /* Integral */
    if ((bits < 0) && (bits + p->bits[1] < p->bits[1]))
        p->bits[1] = bits + p->bits[1]; /* Integral */
    p->bits[2] = bits;              /* Proportional */

    DBG(2, (DBGOUTPUT, "P\t\t%8d  I\t\t%8d  D\t\t%8d\n",
                p->bits[2],  p->bits[1], p->bits[0]));
}

/*------------------------------------------------------------------------------
  gop_avg_qp()  Average quantization parameter of P frames of the previous GOP.
------------------------------------------------------------------------------*/
i32 gop_avg_qp(vp8RateControl_s *rc)
{
    i32 tmp = rc->qpHdrPrev;
    i32 maxIntraBitRatio = 95;  /* Percentage of total GOP bits. */

    if (rc->gopQpSum && rc->gopQpDiv) {
        tmp = DIV(rc->gopQpSum, rc->gopQpDiv);
    }
    /* Average bit count per frame for previous GOP (intra + inter) */
    rc->gopAvgBitCnt = DIV(rc->gopBitCnt, (rc->gopQpDiv+1));

    /* Ratio of intra_frame_bits/all_gop_bits % for previous GOP */
    if (rc->gopBitCnt) {
        i32 gopIntraBitRatio = VP8Calculate(get_avg_bits(&rc->intra,1),
            rc->mbPerPic, DSCBITPERMB) * 100;
        gopIntraBitRatio = DIV(gopIntraBitRatio, rc->gopBitCnt);
        /* Limit GOP intra bit ratio to leave room for inters. */
        gopIntraBitRatio = MIN(maxIntraBitRatio, gopIntraBitRatio);
        update_tables(&rc->gop, tmp, gopIntraBitRatio);
    }
    rc->gopQpSum = 0;
    rc->gopQpDiv = 0;
    rc->gopBitCnt = 0;

    return tmp;
}

/*------------------------------------------------------------------------------
  new_pic_quant()  Calculate new quantization parameter from the 2nd degree R-Q
  equation. Further adjust Qp for "smoother" visual quality.
------------------------------------------------------------------------------*/
static i32 new_pic_quant(linReg_s *p, i32 bits, i32 prev_qp, true_e useQpDeltaLimit)
{
    i32 tmp = 0, qp_best = prev_qp, qp = prev_qp, diff;
    i32 diff_prev = 0, qp_prev = 0, diff_best = 0x7FFFFFFF;

    DBG(2, (DBGOUTPUT, "R/cx:\t\t%8d",bits));

    if (p->a1 == 0 && p->a2 == 0) {
        DBG(2, (DBGOUTPUT, "  QP:\t\t   xx xx  D:\t\t    ====  newQP: %2d\n", qp));
        return qp;
    }

    /* Find the qp that has the best match on fitted curve */
    do {
        tmp  = DIV(p->a1, AcQLookup[qp]);
        tmp += DIV(p->a2, AcQLookup[qp] * AcQLookup[qp]);
        diff = ABS(tmp - bits);

        if (diff < diff_best) {
            if (diff_best == 0x7FFFFFFF) {
                diff_prev = diff;
                qp_prev   = qp;
            } else {
                diff_prev = diff_best;
                qp_prev   = qp_best;
            }
            diff_best = diff;
            qp_best   = qp;
            if ((tmp - bits) <= 0) {
                if (qp < 1) {
                    break;
                }
                qp--;
            } else {
                if (qp >= QINDEX_RANGE-1) {
                    break;
                }
                qp++;
            }
        } else {
            break;
        }
    } while ((qp >= 0) && (qp < QINDEX_RANGE));
    qp = qp_best;

    tmp = diff_prev - diff_best;
    DBG(2, (DBGOUTPUT, "  QP:\t\t   %2d %2d  D:\t\t%8d", qp, qp_prev, tmp));
    DBG(2, (DBGOUTPUT, "  newQP: %2d\n", qp));
    tmp = qp_prev;

    /* Limit Qp change between frames for smoother visual quality */
    if (useQpDeltaLimit) {
        tmp = qp - prev_qp;
        if (tmp > QP_DELTA) {
            qp = prev_qp + QP_DELTA;
            /* When QP is totally wrong, allow faster QP increase */
            if (tmp > QP_DELTA_LIMIT)
                qp = prev_qp + QP_DELTA*2;
        } else if (tmp < -QP_DELTA) {
            qp = prev_qp - QP_DELTA;
        }
    }

    return qp;
}

/*------------------------------------------------------------------------------
  get_avg_bits()
------------------------------------------------------------------------------*/
static i32 get_avg_bits(linReg_s *p, i32 n)
{
    i32 i;
    i32 sum = 0;
    i32 pos = p->pos;

    if (!p->len) return 0;

    if (n == -1 || n > p->len)
        n = p->len;

    i = n;
    while (i--) {
        if (pos) pos--;
        else pos = p->len-1;
        sum += p->bits[pos];
        if (sum < 0) {
            return I32_MAX / (n-i);
        }
    }
    return DIV(sum, n);
}

/*------------------------------------------------------------------------------
  update_tables()  only statistics of PSLICE, please.
------------------------------------------------------------------------------*/
static void update_tables(linReg_s *p, i32 qp, i32 bits)
{
    const i32 clen = RC_TABLE_LENGTH;
    i32 tmp = p->pos;

    p->qp_prev   = qp;
    p->qs[tmp]   = AcQLookup[qp];
    p->bits[tmp] = bits;

    if (++p->pos >= clen) {
        p->pos = 0;
    }
    if (p->len < clen) {
        p->len++;
    }
}

/*------------------------------------------------------------------------------
            update_model()  Update model parameter by Linear Regression.
------------------------------------------------------------------------------*/
static void update_model(linReg_s *p)
{
    i32 *qs = p->qs, *r = p->bits, n = p->len;
    i32 i, a1, a2, sx = lin_sx(qs, n), sy = lin_sy(qs, r, n);

    for (i = 0; i < n; i++) {
        DBG(2, (DBGOUTPUT, "model: qs:\t%8i  r:\t\t%8i\n",qs[i], r[i]));
    }

    if (sy == 0) {
        a1 = 0;
    } else {
        a1 = lin_sxy(qs, r, n);
        if (a1 < (I32_MAX/n)) {
            a1 *= n;
            a1 -= (sx < I32_MAX/sy) ? sx * sy : I32_MAX;
        } else {
            a1 -= VP8Calculate(sx, sy, n);
            a1 *= n;
        }
    }

    a2 = (lin_nsxx(qs, n) - (sx * sx));
    DBG(2, (DBGOUTPUT, "model: sx:\t%8i  sy:\t\t%8i  a1_:\t\t%8i\n", sx, sy, a1));
    DBG(2, (DBGOUTPUT, "model: nSxx:\t%8i  a2_:\t\t%8i\n", lin_nsxx(qs, n), a2));

    if (a2 == 0) {
        if (p->a1 == 0) {
            /* If encountered in the beginning */
            a1 = 0;
        } else {
            a1 = (p->a1 * 2) / 3;
        }
    } else {
        a1 = VP8Calculate(a1, DSCY, a2);
    }

    /* Value of a1 shouldn't be excessive (small) */
    a1 = MAX(a1, -4096*DSCY);
    a1 = MIN(a1,  4096*DSCY-1);
    a1 = MAX(a1, -I32_MAX/AcQLookup[QINDEX_RANGE-1]/RC_TABLE_LENGTH);
    a1 = MIN(a1,  I32_MAX/AcQLookup[QINDEX_RANGE-1]/RC_TABLE_LENGTH);

    ASSERT(ABS(a1) * sx >= 0);
    ASSERT(sx * DSCY >= 0);
    a2 = DIV(sy * DSCY, n) - DIV(a1 * sx, n);

    DBG(2, (DBGOUTPUT, "model: a2:\t%8d  a1:\t\t%8d\n", a2, a1));

    if (p->len > 0) {
        p->a1 = a1;
        p->a2 = a2;
    }
}

/*------------------------------------------------------------------------------
  lin_sy()  calculate value of Sy for n points.
------------------------------------------------------------------------------*/
static i32 lin_sy(i32 *qp, i32 *r, i32 n)
{
    i32 sum = 0;

    while (n--) {
        sum += qp[n] * qp[n] * r[n];
        if (sum < 0) {
            return I32_MAX / DSCY;
        }
    }
    return DIV(sum, DSCY);
}

/*------------------------------------------------------------------------------
  lin_sx()  calculate value of Sx for n points.
------------------------------------------------------------------------------*/
static i32 lin_sx(i32 *qp, i32 n)
{
    i32 tmp = 0;

    while (n--) {
        ASSERT(qp[n]);
        tmp += qp[n];
    }
    return tmp;
}

/*------------------------------------------------------------------------------
  lin_sxy()  calculate value of Sxy for n points.
------------------------------------------------------------------------------*/
static i32 lin_sxy(i32 *qp, i32 *r, i32 n)
{
    i32 tmp, sum = 0;

    while (n--) {
        tmp = qp[n] * qp[n] * qp[n];
        if (tmp > r[n]) {
            sum += DIV(tmp, DSCY) * r[n];
        } else {
            sum += tmp * DIV(r[n], DSCY);
        }
        if (sum < 0) {
            return I32_MAX;
        }
    }
    return sum;
}

/*------------------------------------------------------------------------------
  lin_nsxx()  calculate value of n * Sxy for n points.
------------------------------------------------------------------------------*/
static i32 lin_nsxx(i32 *qp, i32 n)
{
    i32 tmp = 0, sum = 0, d = n ;

    while (n--) {
        tmp = qp[n];
        tmp *= tmp;
        sum += d * tmp;
    }
    return sum;
}
