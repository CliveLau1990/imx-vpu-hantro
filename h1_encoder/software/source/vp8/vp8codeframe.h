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
--  Description :  Encode picture
--
------------------------------------------------------------------------------*/

#ifndef __VP8_CODE_FRAME_H__
#define __VP8_CODE_FRAME_H__

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "vp8instance.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

typedef enum
{
    VP8ENCODE_OK = 0,
    VP8ENCODE_TIMEOUT = 1,
    VP8ENCODE_DATA_ERROR = 2,
    VP8ENCODE_HW_ERROR = 3,
    VP8ENCODE_SYSTEM_ERROR = 4,
    VP8ENCODE_HW_RESET = 5
} vp8EncodeFrame_e;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/
void VP8SetFrameParams(vp8Instance_s *inst);
vp8EncodeFrame_e VP8CodeFrame(vp8Instance_s * inst);
vp8EncodeFrame_e VP8CodeFrameMultiPass(vp8Instance_s * inst);
void VP8InitPenalties(vp8Instance_s * inst);
u32 ProcessStatistics(vp8Instance_s *inst, i32 *boost);

#endif
