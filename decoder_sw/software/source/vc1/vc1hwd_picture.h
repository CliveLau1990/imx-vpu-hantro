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

#ifndef VC1HWD_PICTURE_H
#define VC1HWD_PICTURE_H

/*------------------------------------------------------------------------------
    Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "dwl.h"
#include "vc1decapi.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

typedef enum {
  NONE = 0,
  PIPELINED = 1,
  PARALLEL = 2,
  STAND_ALONE = 3
} decPpStatus_e;

/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/

/* Part of picture buffer descriptor. This informaton is unique
 * for each field of the frame. */
typedef struct field {
  intCompField_e int_comp_f;
  i32 i_scale_a;            /* A used for TOP */
  i32 i_shift_a;
  i32 i_scale_b;            /* B used for BOTTOM */
  i32 i_shift_b;

  picType_e type;
  u32 pic_id;
  u32 decode_id;

  u32 pp_buffer_index;
  decPpStatus_e dec_pp_stat; /* pipeline / parallel ... */
  VC1DecRet return_value;
} field_t;


/* Picture buffer descriptor. Used for anchor frames
 * and work buffer and output buffer. */
typedef struct picture {
  struct DWLLinearMem data;

  struct DWLLinearMem *pp_data;

  fcm_e fcm;              /* frame coding mode */

  u16x coded_width;        /* Coded height in pixels */
  u16x coded_height;       /* Coded widht in pixels */
  u16x key_frame;          /* for field pictures both must be Intra */

  u16x range_red_frm;       /* Main profile range reduction information */
  u32 range_map_yflag;      /* Advanced profile range mapping parameters */
  u32 range_map_y;
  u32 range_map_uv_flag;
  u32 range_map_uv;

  u32 is_first_field;       /* Is current field first or second */
  u32 is_top_field_first;    /* Which field is first */
  u32 rff;                /* repeat first field */
  u32 rptfrm;             /* repeat frame count */
  u32 tiled_mode;
  u32 buffered;
  u32 pic_code_type[2];

  field_t field[2];       /* 0 = first; 1 = second */
#ifdef USE_OUTPUT_RELEASE
  u32 first_show;
#endif
} picture_t;

/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

#endif /* #ifndef VC1SWD_PICTURE_H */

