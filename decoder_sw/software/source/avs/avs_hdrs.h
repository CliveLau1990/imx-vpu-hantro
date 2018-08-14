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

#ifndef AVSDECHDRS_H
#define AVSDECHDRS_H

#include "basetype.h"

typedef struct {
  u32 drop_flag;
  u32 hours;
  u32 minutes;
  u32 seconds;
  u32 picture;
} DecTimeCode;

typedef struct {
  /* sequence header */
  u32 profile_id;
  u32 level_id;
  u32 progressive_sequence;
  u32 horizontal_size;
  u32 vertical_size;
  u32 chroma_format;
  u32 aspect_ratio;
  u32 frame_rate_code;
  u32 bit_rate_value;
  u32 low_delay;
  u32 bbv_buffer_size;

  /* sequence display extension header */
  u32 video_format;
  u32 sample_range;
  u32 color_description;
  u32 color_primaries;
  u32 transfer_characteristics;
  u32 matrix_coefficients;
  u32 display_horizontal_size;
  u32 display_vertical_size;

  /* picture header */
  u32 pic_coding_type;
  u32 bbv_delay;
  DecTimeCode time_code;
  u32 picture_distance;
  u32 progressive_frame;
  u32 picture_structure;
  u32 advanced_pred_mode_disable;
  u32 top_field_first;
  u32 repeat_first_field;
  u32 fixed_picture_qp;
  u32 picture_qp;
  u32 picture_reference_flag;
  u32 skip_mode_flag;
  u32 loop_filter_disable;
  i32 alpha_offset;
  i32 beta_offset;

  /*AVS Plus stuff */
  /* weighting quant */
  u32 weighting_quant_flag;
  u32 chroma_quant_param_disable;
  i32 chroma_quant_param_delta_cb;
  i32 chroma_quant_param_delta_cr;
  u32 weighting_quant_param_index;
  u32 weighting_quant_model;
  i32 weighting_quant_param_delta1[6];
  i32 weighting_quant_param_delta2[6];
  u32 weighting_quant_param[6]; // wqP[m][6]

  /* advance entropy coding */
  u32 aec_enable;

  /* picture enhance */
  u32 no_forward_reference_flag;
  u32 pb_field_enhanced_flag;
} DecHdrs;

#endif /* #ifndef AVSDECHDRS_H */
