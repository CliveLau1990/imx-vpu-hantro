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

#ifndef DECHDRS_H
#define DECHDRS_H

#include "basetype.h"

typedef struct DecHdrs_t {
  u32 lock;   /* header information lock */
  u32 last_header_type;
  u32 profile_and_level_indication;  /* Visual Object Sequence */
  u32 is_visual_object_identifier;   /* Visual Object */
  u32 visual_object_verid;
  u32 visual_object_priority;
  u32 visual_object_type;
  u32 video_signal_type;
  u32 video_format;
  u32 video_range;
  u32 colour_description;
  u32 colour_primaries;
  u32 transfer_characteristics;
  u32 matrix_coefficients; /* end of Visual Object */
  u32 random_accessible_vol;    /* start of VOL */
  u32 video_object_type_indication;
  u32 is_object_layer_identifier;
  u32 video_object_layer_verid;
  u32 video_object_layer_priority;
  u32 aspect_ratio_info;
  u32 par_width;
  u32 par_height;
  u32 vol_control_parameters;
  u32 chroma_format;
  u32 low_delay;
  u32 vbv_parameters;
  u32 first_half_bit_rate;
  u32 latter_half_bit_rate;
  u32 first_half_vbv_buffer_size;
  u32 latter_half_vbv_buffer_size;
  u32 first_half_vbv_occupancy;
  u32 latter_half_vbv_occupancy;
  u32 video_object_layer_shape;
  u32 vop_time_increment_resolution;
  u32 fixed_vop_rate;
  u32 fixed_vop_time_increment;
  u32 video_object_layer_width;
  u32 video_object_layer_height;
  u32 interlaced;
  u32 obmc_disable;
  u32 sprite_enable;
  u32 not8_bit;
  u32 quant_type;
  u32 complexity_estimation_disable;
  u32 resync_marker_disable;
  u32 data_partitioned;
  u32 reversible_vlc;
  u32 scalability;
  u32 estimation_method;
  u32 shape_complexity_estimation_disable;
  u32 opaque;
  u32 transparent;
  u32 intra_cae;
  u32 inter_cae;
  u32 no_update;
  u32 upsampling;
  u32 texture_complexity_estimation_set1_disable;
  u32 intra_blocks;
  u32 inter_blocks;
  u32 inter4v_blocks;
  u32 not_coded_blocks;
  u32 texture_complexity_estimation_set2_disable;
  u32 dct_coefs;
  u32 dct_lines;
  u32 vlc_symbols;
  u32 vlc_bits;
  u32 motion_compensation_complexity_disable;
  u32 apm;
  u32 npm;
  u32 interpolate_mc_q;
  u32 forw_back_mc_q;
  u32 halfpel2;
  u32 halfpel4;
  u32 version2_complexity_estimation_disable;
  u32 sadct;
  u32 quarterpel;
  u32 closed_gov;
  u32 broken_link;

  u32 num_rows_in_slice;
  u32 rlc_table_y, rlc_table_c;
  u32 dc_table;
  u32 mv_table;
  u32 skip_mb_code;
  u32 flip_flop_rounding;
} DecHdrs;

#endif
