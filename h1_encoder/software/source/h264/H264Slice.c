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
--  Abstract  :
--
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "H264Slice.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

	H264SliceInit

------------------------------------------------------------------------------*/
void H264SliceInit(slice_s * slice)
{
    slice->byteStream = ENCHW_YES;
    slice->nalUnitType = IDR;
    slice->sliceType = ISLICE;
    slice->picParameterSetId = 0;
    slice->frameNum = 0;
    slice->frameNumBits = 16;
    slice->idrPicId = 0;
    slice->nalRefIdc = 1;
    slice->disableDeblocking = 0;
    slice->filterOffsetA = -4;
    slice->filterOffsetB = 6;
    slice->sliceSize = 0;
    slice->cabacInitIdc = 0;
    slice->quarterPixelMv = 1;

    return;
}
