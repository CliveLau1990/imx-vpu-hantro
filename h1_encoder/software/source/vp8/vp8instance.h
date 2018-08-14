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
--  Abstract  :   Encoder instance
--
------------------------------------------------------------------------------*/

#ifndef __VP8_INSTANCE_H__
#define __VP8_INSTANCE_H__

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "enccommon.h"
#include "encpreprocess.h"
#include "encasiccontroller.h"

#include "vp8seqparameterset.h"
#include "vp8picparameterset.h"
#include "vp8picturebuffer.h"
#include "vp8putbits.h"
#include "vp8ratecontrol.h"
#include "vp8quanttable.h"

#ifdef VIDEOSTAB_ENABLED
#include "vidstabcommon.h"
#endif

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum VP8EncStatus
{
    VP8ENCSTAT_INIT = 0xA1,
    VP8ENCSTAT_KEYFRAME,
    VP8ENCSTAT_START_FRAME,
    VP8ENCSTAT_ERROR
};

enum VP8EncQualityMetric {
    VP8ENC_PSNR = 0,
    VP8ENC_SSIM = 1
};

typedef struct {
    i32 quant[2];
    i32 zbin[2];
    i32 round[2];
    i32 dequant[2];
} qp;

typedef struct {
    /* Approximate bit cost of mode. IOW bits used when selected mode is
     * boolean encoded using appropriate tree and probabilities. Note that
     * this value is scale up with SCALE (=256) */
    i32 intra16ModeBitCost[4 + 1];
    i32 intra4ModeBitCost[14 + 1];
} mbs;

typedef struct {
    /* Enable signals */
    i32 goldenUpdateEnable;
    i32 goldenBoostEnable;
    
    /* MV accumulators for period */
    i32 *mvSumX, *mvSumY; 
    
    /* MB type counters for one period*/
    i32 goldenCnt;  /* Count of golden mbs */
    i32 goldenDiv;  /* Max nbr of golden */
    i32 intraCnt;   /* P-frame Intra coded mbs */
    i32 skipCnt;    /* P-frame skipped mbs */
    i32 skipDiv;    /* Nbr of P-frame macroblocks */
} statPeriod;

typedef struct
{
    u32 encStatus;
    u32 mbPerFrame;
    u32 mbPerRow;
    u32 mbPerCol;
    u32 frameCnt;
    u32 testId;
    u32 numRefBuffsLum;
    u32 numRefBuffsChr;
    u32 prevFrameLost;
    
    i32 qualityMetric;

    i32 maxNumPasses;
    u32 passNbr;
    u32 layerId;
        
    statPeriod statPeriod;
    preProcess_s preProcess;
    vp8RateControl_s rateControl;
    picBuffer picBuffer;         /* Reference picture container */
    sps sps;                     /* Sequence parameter set */
    ppss ppss;                   /* Picture parameter set */
    vp8buffer buffer[10];         /* Stream buffer per partition */
    qp qpY1[QINDEX_RANGE];  /* Quant table for 1'st order luminance */
    qp qpY2[QINDEX_RANGE];  /* Quant table for 2'nd order luminance */
    qp qpCh[QINDEX_RANGE];  /* Quant table for chrominance */
    mbs mbs;
    asicData_s asic;
    u32 *pOutBuf;                   /* User given stream output buffer */
    const void *inst;               /* Pointer to this instance for checking */
#ifdef VIDEOSTAB_ENABLED
    HWStabData vsHwData;
    SwStbData vsSwData;
#endif
    entropy entropy[1];
    u16 probCountStore[ASIC_VP8_PROB_COUNT_SIZE/2];
} vp8Instance_s;

#endif
