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

#ifndef MPEG2DECHDRS_H
#define MPEG2DECHDRS_H

#include "basetype.h"

typedef struct DecTimeCode_t {
  u32 drop_flag;
  u32 hours;
  u32 minutes;
  u32 seconds;
  u32 picture;
} DecTimeCode;

typedef struct {
  /* sequence header */
  u32 horizontal_size;
  u32 vertical_size;
  u32 aspect_ratio_info;
  u32 par_width;
  u32 par_height;
  u32 frame_rate_code;
  u32 bit_rate_value;
  u32 vbv_buffer_size;
  u32 constr_parameters;
  u32 load_intra_matrix;
  u32 load_non_intra_matrix;
  u8 q_table_intra[64];
  u8 q_table_non_intra[64];
  /* sequence extension header */
  u32 profile_and_level_indication;
  u32 progressive_sequence;
  u32 chroma_format;
  u32 hor_size_extension;
  u32 ver_size_extension;
  u32 bit_rate_extension;
  u32 vbv_buffer_size_extension;
  u32 low_delay;
  u32 frame_rate_extension_n;
  u32 frame_rate_extension_d;
  /* sequence display extension header */
  u32 video_format;
  u32 color_description;
  u32 color_primaries;
  u32 transfer_characteristics;
  u32 matrix_coefficients;
  u32 display_horizontal_size;
  u32 display_vertical_size;
  /* GOP (Group of Pictures) header */
  DecTimeCode time;
  u32 closed_gop;
  u32 broken_link;
  /* picture header */
  u32 temporal_reference;
  u32 picture_coding_type;
  u32 vbv_delay;
  u32 extra_info_byte_count;
  /* picture coding extension header */
  u32 f_code[2][2];
  u32 intra_dc_precision;
  u32 picture_structure;
  u32 top_field_first;
  u32 frame_pred_frame_dct;
  u32 concealment_motion_vectors;
  u32 quant_type;
  u32 intra_vlc_format;
  u32 alternate_scan;
  u32 repeat_first_field;
  u32 chroma420_type;
  u32 progressive_frame;
  u32 composite_display_flag;
  u32 v_axis;
  u32 field_sequence;
  u32 sub_carrier;
  u32 burst_amplitude;
  u32 sub_carrier_phase;
  /* picture display extension header */
  u32 frame_centre_hor_offset[3];
  u32 frame_centre_ver_offset[3];

  /* extra */
  u32 mpeg2_stream;
  u32 frame_rate;
  u32 video_range;
  u32 interlaced;
  u32 repeat_frame_count;
  i32 first_field_in_frame;
  i32 field_index;
  i32 field_out_index;

  /* for motion vectors */
  u32 f_code_fwd_hor;
  u32 f_code_fwd_ver;
  u32 f_code_bwd_hor;
  u32 f_code_bwd_ver;

} DecHdrs;

#endif /* #ifndef MPEG2DECHDRS_H */
