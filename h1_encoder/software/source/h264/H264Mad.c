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

#include "H264Mad.h"

/*------------------------------------------------------------------------------

    Init MAD structure

------------------------------------------------------------------------------*/
void H264MadInit(madTable_s *mad, u32 mbPerFrame)
{
    i32 i;

    mad->mbPerFrame = mbPerFrame;

    /* Initial threshold for the first frame. */
    mad->threshold[0] = 256*16;
    mad->threshold[1] = 256*8;
    mad->threshold[2] = 256*4;

    for (i = 0; i < MAD_TABLE_LEN; i++)
    {
        mad->th[i] = 0;
        mad->count[i] = 0;
    }
    mad->pos = 0;
    mad->len = 0;
}

/*------------------------------------------------------------------------------
  update_tables()
------------------------------------------------------------------------------*/
static void update_tables(madTable_s *p, i32 th, i32 count)
{
    const i32 clen = 3;
    i32 tmp = p->pos;

    p->th[tmp]    = th;
    p->count[tmp] = count;

    if (++p->pos >= clen) {
        p->pos = 0;
    }
    if (p->len < clen) {
        p->len++;
    }
}
/*------------------------------------------------------------------------------
  lin_sx()  calculate value of Sx for n points.
------------------------------------------------------------------------------*/
static i32 lin_sx(i32 *x, i32 n)
{
    i32 sum = 0;

    while (n--) {
        sum += x[n];
        if (sum < 0) {
            return I32_MAX;
        }
    }
    return sum;
}

/*------------------------------------------------------------------------------
  lin_sxy()  calculate value of Sxy for n points.
------------------------------------------------------------------------------*/
static i32 lin_sxy(i32 *qp, i32 *r, i32 n)
{
    i32 sum = 0;

    while (n--) {
        sum += qp[n] * r[n];
        if (sum < 0) {
            return I32_MAX;
        }
    }
    return sum;
}

/*------------------------------------------------------------------------------
  lin_nsxx()  calculate value of n * Sxx for n points.
------------------------------------------------------------------------------*/
static i32 lin_nsxx(i32 *qp, i32 n)
{
    i32 sum = 0, d = n;

    while (n--) {
        sum += d * qp[n] * qp[n];
        if (sum < 0) {
            return I32_MAX;
        }
    }
    return sum;
}

/*------------------------------------------------------------------------------
    update_model()  Update model parameter by Linear Regression.
------------------------------------------------------------------------------*/
static void update_model(madTable_s *p)
{
    i32 *count = p->count, *th = p->th, n = p->len;
    i32 sx = lin_sx(p->count, n);
    i32 sy = lin_sx(p->th, n);
    i32 a1 = 0, a2 = 0;

    /*i32 i;
    for (i = 0; i < n; i++) {
        printf("model: cnt %i  th %i\n", count[i], th[i]);
    }*/

    ASSERT(sx >= 0);
    ASSERT(sy >= 0);

    if (n > 1)
    {
        a1 = lin_sxy(count, th, n);
        a1 = a1 < (I32_MAX / n) ? a1 * n : I32_MAX;

        if (sy) {
            a1 -= sx < (I32_MAX / sy) ? sx * sy : I32_MAX;
        }

        a2 = (lin_nsxx(count, n) - (sx * sx));
        if (a2) {
            a1 = DIV(a1 * DSCY, a2);
        } else {
            a1 = 0;
        }

        /* Value of a1 shouldn't be excessive */
        a1 = MAX(a1, 0);
        a1 = MIN(a1, 1024*DSCY);

        if (n)
            a2 = DIV(sy, n) - DIV(a1 * sx, n*DSCY);
        else
            a2 = 0;
    }

    p->a1 = a1;
    p->a2 = a2;

    /*printf("model: a2:%9d  a1:%8d\n", p->a2, p->a1);*/
}

/*------------------------------------------------------------------------------

    Update MAD threshold based on previous frame count of macroblocks with MAD
    under threshold. Trying to adjust threshold so that madCount == targetCount.

------------------------------------------------------------------------------*/
void H264MadThreshold(madTable_s *mad, u32 *madCount)
{
    /* Target to improve quality for 40% of macroblocks */
    i32 targetCount = 40*mad->mbPerFrame/100;
    i32 threshold = mad->threshold[0];
    i32 prevthreshold = mad->threshold[0];
    i32 lowLimit, highLimit;

    update_tables(mad, prevthreshold, madCount[0]);
    update_model(mad);

    /* Calculate new threshold for next frame using either linear regression
     * model or adjustment based on current setting */
    if (mad->a1)
        threshold = mad->a1 * targetCount / DSCY + mad->a2;
    else if (madCount[0] < (u32)targetCount)
        threshold = MAX(prevthreshold * 5/4, prevthreshold + 256);
    else
        threshold = MIN(prevthreshold * 3/4, prevthreshold - 256);

    /* For small count, ensure that we increase the threshold minimum 1 step */
    if (madCount[0] < (u32)targetCount/2)
        threshold = MAX(threshold, prevthreshold + 256);

    /* If previous frame had zero count, ensure that we increase threshold */
    if (!madCount[0])
        threshold = MAX(threshold, prevthreshold + 256*4);

    /* Limit how much the threshold can change between two frames */
    lowLimit = prevthreshold/2;
    highLimit = MAX(prevthreshold*2, 256*4);
    threshold = MIN(highLimit, MAX(lowLimit, threshold));

    /* threshold_div256 has 6-bits range [0,63] */
    threshold = ((threshold+128)/256)*256;
    mad->threshold[0] = MAX(0, MIN(63*256, threshold));

    /* Middle threshold: 2/3 of high threshold, smaller but not negative. */
    threshold = ((mad->threshold[0] * 2/3 + 128)/256)*256;
    mad->threshold[1] = MAX(0, MIN(mad->threshold[0] - 256, threshold));

    /* Low threshold: 1/3 of high threshold, smaller but not negative. */
    threshold = ((mad->threshold[0] * 1/3 + 128)/256)*256;
    mad->threshold[2] = MAX(0, MIN(mad->threshold[1] - 256, threshold));

}

