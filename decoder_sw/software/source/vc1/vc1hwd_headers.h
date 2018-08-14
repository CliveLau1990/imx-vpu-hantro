/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#ifndef VC1HWD_HEADERS_H
#define VC1HWD_HEADERS_H

/*------------------------------------------------------------------------------
    Include headers
------------------------------------------------------------------------------*/

#include "vc1hwd_util.h"
#include "vc1hwd_stream.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/* enumerated sample aspect ratios, ASPECT_RATIO_M_N means M:N */
enum {
  ASPECT_RATIO_UNSPECIFIED = 0,
  ASPECT_RATIO_1_1,
  ASPECT_RATIO_12_11,
  ASPECT_RATIO_10_11,
  ASPECT_RATIO_16_11,
  ASPECT_RATIO_40_33,
  ASPECT_RATIO_24_11,
  ASPECT_RATIO_20_11,
  ASPECT_RATIO_32_11,
  ASPECT_RATIO_80_33,
  ASPECT_RATIO_18_11,
  ASPECT_RATIO_15_11,
  ASPECT_RATIO_64_33,
  ASPECT_RATIO_160_99,
  ASPECT_RATIO_EXTENDED = 15
};

typedef enum {
  SC_END_OF_SEQ       = 0x0000010A,
  SC_SLICE            = 0x0000010B,
  SC_FIELD            = 0x0000010C,
  SC_FRAME            = 0x0000010D,
  SC_ENTRY_POINT      = 0x0000010E,
  SC_SEQ              = 0x0000010F,
  SC_SLICE_UD         = 0x0000011B,
  SC_FIELD_UD         = 0x0000011C,
  SC_FRAME_UD         = 0x0000011D,
  SC_ENTRY_POINT_UD   = 0x0000011E,
  SC_SEQ_UD           = 0x0000011F,
  SC_NOT_FOUND        = 0xFFFE
} startCode_e;

typedef enum {
  VC1_SIMPLE,
  VC1_MAIN,
  VC1_ADVANCED
} vc1Profile_e;

/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

struct swStrmStorage;
u32 vc1hwdDecodeSequenceLayer( struct swStrmStorage *storage,
                               strmData_t *p_strm_data );

u32 vc1hwdDecodeEntryPointLayer( struct swStrmStorage *storage,
                                 strmData_t *p_strm_data );

u32 vc1hwdGetStartCode( strmData_t *p_strm_data );

u32 vc1hwdGetUserData( struct swStrmStorage *storage,
                       strmData_t *p_strm_data );

#endif /* #ifndef VC1HWD_HEADERS_H */

