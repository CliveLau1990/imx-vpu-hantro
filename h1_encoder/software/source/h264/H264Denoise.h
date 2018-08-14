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
--  Abstract : Encoder initialization and setup
--
------------------------------------------------------------------------------*/

#ifndef __H264_DENOISE_H__
#define __H264_DENOISE_H__

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "h264encapi.h"
#include "H264Instance.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/
#define DNF_STENGTH_BITS 11
#define DNF_LEVELIVT_BITS 12
#define DNF_LEVELMAX_BITS 16

#define BITMASK(n) ((1<<(n))-1)

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

H264EncRet H264EncDnfInit(h264Instance_s *inst);
H264EncRet H264EncDnfSetParameters(h264Instance_s *inst, const H264EncCodingCtrl *pCodeParams);
H264EncRet H264EncDnfGetParameters(h264Instance_s *inst, H264EncCodingCtrl *pCodeParams);
H264EncRet H264EncDnfPrepare(h264Instance_s *inst);
H264EncRet H264EncDnfUpdate(h264Instance_s *inst);

#endif

