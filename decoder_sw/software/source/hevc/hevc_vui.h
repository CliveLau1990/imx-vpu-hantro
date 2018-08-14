/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
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

#ifndef HEVC_VUI_H_
#define HEVC_VUI_H_

#include "basetype.h"
#include "sw_stream.h"
#include "hevc_cfg.h"

#define MAX_CPB_CNT 32

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
  ASPECT_RATIO_EXTENDED_SAR = 255
};

struct SubHrdParameters_t {
  u32 bit_rate_value[MAX_CPB_CNT];
  u32 bit_rate_du_value[MAX_CPB_CNT];
  u32 cbr_flag[MAX_CPB_CNT];
};
struct HrdParameters_t {
  u32 nal_hrd_parameters_present_flag;
  u32 vcl_hrd_parameters_present_flag;
  u32 sub_pic_hrd_params_present_flag;
  u32 tick_divisor;
  u32 du_cpb_removal_delay_increment_length;
  u32 dpb_output_delay_du_length;
  u32 bit_rate_scale;
  u32 initial_cpb_removal_delay_length;
  u32 au_cpb_removal_delay_length;
  u32 dpb_output_delay_length;
  u32 fixed_pic_rate_general_flag[MAX_SUB_LAYERS];
  u32 fixed_pic_rate_within_cvs_flag[MAX_SUB_LAYERS];
  u32 low_delay_hrd_flag[MAX_SUB_LAYERS];
  u32 cpb_cnt[MAX_SUB_LAYERS];
  struct SubHrdParameters_t sub_hrd_parameters[MAX_SUB_LAYERS];
};
/* storage for VUI parameters */
struct VuiParameters {
  u32 aspect_ratio_present_flag;
  u32 aspect_ratio_idc;
  u32 sar_width;
  u32 sar_height;
  u32 video_signal_type_present_flag;
  u32 video_format;
  u32 video_full_range_flag;
  u32 colour_description_present_flag;
  u32 colour_primaries;
  u32 transfer_characteristics;
  u32 matrix_coefficients;
  u32 chroma_loc_info_present_flag;
  u32 chroma_sample_loc_type_top_field;
  u32 chroma_sample_loc_type_bottom_field;
  u32 frame_field_info_present_flag;
  u32 default_display_window_flag;
  u32 vui_timing_info_present_flag;
  u32 vui_num_units_in_tick;
  u32 vui_time_scale;
  u32 vui_poc_proportional_to_timing_flag;
  u32 vui_hrd_parameters_present_flag;
  struct  HrdParameters_t hrd_parameters;
};

u32 HevcDecodeVuiParameters(struct StrmData *stream, u32 max_sub_layers,
                            struct VuiParameters *vui_parameters);

#endif /* #ifdef HEVC_VUI_H_ */
