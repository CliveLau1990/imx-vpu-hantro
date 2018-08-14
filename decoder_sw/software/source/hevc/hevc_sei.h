/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
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

#ifndef HEVC_SEI_H_
#define HEVC_SEI_H_

#include "basetype.h"
#include "sw_stream.h"
#include "hevc_cfg.h"
#include "hevc_seq_param_set.h"

enum SeiType {
  SEI_BUFFERING_PERIOD = 0,
  SEI_PIC_TIMING,
  SEI_PAN_SCAN_RECT,
  SEI_FILLER_PAYLOAD,
  SEI_USER_DATA_REGISTERED_ITU_T_T35,
  SEI_USER_DATA_UNREGISTERED,
  SEI_RECOVERY_POINT,
  SEI_SCENE_INFO = 9,
  SEI_PICTURE_SNAPSHOT = 15,
  SEI_PROGRESSIVE_REFINEMENT_SEGMENT_START,
  SEI_PROGRESSIVE_REFINEMENT_SEGMENT_END,
  SEI_FILM_GRAIN_CHARACTERISTICS = 19,
  SEI_POST_FILTER_HINTS = 22,
  SEI_TONE_MAPPING_INFO,
  SEI_FRAME_PACKING_ARRANGEMENT = 45,
  SEI_DISPLAY_ORIENTATION = 47,
  SEI_STRUCTURE_OF_PICTURES_INFO = 128,
  SEI_ACTIVE_PARAMETER_SETS,
  SEI_DECODING_UNIT_INFO,
  SEI_TEMPORAL_SUB_LAYER_ZERO_INDEX,
  SEI_SCALABLE_NESTING = 133,
  SEI_REGION_REFRESH_INFO,
  SEI_MASTERING_DISPLAY_COLOR_VOLUME = 137,
  SEI_CONTENT_LIGHT_LEVEL_INFO  = 144
};

struct BufPeriodParameters {
  u32 bp_seq_parameter_set_id;
  u32 irap_cpb_params_present_flag;
  u32 cpb_delay_offset;
  u32 dpb_delay_offset;
  u32 concatenation_flag;
  u32 au_cpb_removal_delay_delta;
  u32 nal_initial_cpb_removal_delay[MAX_CPB_CNT];
  u32 nal_initial_cpb_removal_offset[MAX_CPB_CNT];
  u32 nal_initial_alt_cpb_removal_delay[MAX_CPB_CNT];
  u32 nal_initial_alt_cpb_removal_offset[MAX_CPB_CNT];
  u32 vcl_initial_cpb_removal_delay[MAX_CPB_CNT];
  u32 vcl_initial_cpb_removal_offset[MAX_CPB_CNT];
  u32 vcl_initial_alt_cpb_removal_delay[MAX_CPB_CNT];
  u32 vcl_initial_alt_cpb_removal_offset[MAX_CPB_CNT];
};
struct PicTimingParameters {
  u32 pic_struct;
  u32 au_cpb_removal_delay;
  u32 pic_dpb_output_delay;
  u32 pic_dpb_output_du_delay;
};

struct MasterDisParameters {
  u32 display_primaries_x[3];
  u32 display_primaries_y[3];
  u32 white_point_x;
  u32 white_point_y;
  u32 max_display_mastering_luminance;
  u32 min_display_mastering_luminance;
};

struct LightLevelParameters {
  u32 max_content_light_level;
  u32 max_pic_average_light_level;
};

struct DpbOutDelay {
  u32 first_unit_flag;
  u32 hrd_init_flag;
  u32 alt_flag;
  u32 pre_nondiscard_flag;
  double clock_tick;
  double pre_au_finall_arrival_time;
  double pre_au_nominal_remove_time;
  double prefirstpic_au_nominal_time;
  double prenondiscardable_au_nominal_time;
  double firstpic_au_nominal_time;
  u32 precpb_removal_delay;
  u32 precpb_removal_delay_msb;
  double cpb_removal_time;
  double dpb_output_time;
};
/* storage for SEI parameters */
struct SEIParameters {
  u32 bufperiod_present_flag;
  u32 pictiming_present_flag;
  u32 mastering_display_present_flag;
  u32 lightlevel_present_flag;
  u32 stream_len;
  u32 bumping_flag;
  u32 vui_parameters_update_flag;
  struct BufPeriodParameters buf_parameter;
  struct PicTimingParameters pic_parameter;
  struct MasterDisParameters dis_parameter;
  struct LightLevelParameters light_parameter;
  struct DpbOutDelay time_parameter;
};

u32 HevcDecodeSEIParameters(struct StrmData *stream, int layerid,
                            struct SEIParameters *sei,
                            struct SeqParamSet **sps);
u32 HevcDecodeRHDParameters(int stream_len,
                            struct SEIParameters *sei,
                            struct NalUnit *nal_unit,
                            struct SeqParamSet *sps);
#endif /* #ifdef HEVC_SEI_H */
