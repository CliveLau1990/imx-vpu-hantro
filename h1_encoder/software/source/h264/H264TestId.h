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
--  Description :  Encoder setup according to a test vector
--
------------------------------------------------------------------------------*/

#ifndef __H264_TESTID_H__
#define __H264_TESTID_H__

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "basetype.h"
#include "H264Instance.h"
#include "H264Slice.h"
#include "H264RateControl.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    TestID defines a test configuration for the encoder. If the encoder control
    software is compiled with INTERNAL_TEST flag the test ID will force the 
    encoder operation according to the test vector. 

    TestID  Description
    0       No action, normal encoder operation
    1       Frame quantization test, adjust qp for every frame, qp = 0..51
    2       Slice test, adjust slice amount for each frame
    4       Stream buffer limit test, limit=500 (4kB) for first frame
    6       Quantization test, min and max QP values.
    7       Filter test, set disableDeblocking and filterOffsets A and B
    8       Segment test, set segment map and segment qps
    9       Reference frame test, all combinations of reference and refresh.
    10      Segment map test
    11      Temporal layer test, reference and refresh as with 3 layers
    12      User data test
    15      Intra16Favor test, set to maximum value
    16      Cropping test, set cropping values for every frame
    19      RGB input mask test, set all values
    20      MAD test, test all MAD QP change values
    21      InterFavor test, set to maximum value
    22      MV test, set cropping offsets so that max MVs are tested
    23      DMV penalty test, set to minimum/maximum values
    24      Max overfill MV
    26      ROI test
    27      Intra area test
    28      CIR test
    29      Intra slice map test
    31      Non-zero penalty test, don't use zero penalties
    34      Downscaling test

------------------------------------------------------------------------------*/
enum
{
    TID_NONZERO_PANEALTY=31,   /* 31   Non-zero penalty test, don't use zero penalties */
    TID_DOWNSCALING=34,        /* 34 Downscaling test */
    TID_INPUT_BUFFER=35,       /* 35 */
    TID_RFC_OVERFLOW=36,
    TID_TRANS_OVERFLOW=37,
    TID_MVOUT_TOGGLE,          /* 38 Toggle MV Output */
    TID_COUNT_MAX
};

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

void H264ConfigureTestBeforeFrame(h264Instance_s * inst);
void H264ConfigureTestPenalties(h264Instance_s * inst);
void H264InputLineBufDepthTest(h264Instance_s *inst);
void H264CroppingTest(h264Instance_s *inst);

#endif
