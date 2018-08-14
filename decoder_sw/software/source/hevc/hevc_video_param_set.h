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

#ifndef HEVC_VIDEO_PARAM_SET_H_
#define HEVC_VIDEO_PARAM_SET_H_

#include "basetype.h"
#include "hevc_cfg.h"
#include "hevc_seq_param_set.h"
#include "sw_stream.h"

/* structure to store video parameter set information decoded from the
 * stream */
struct VideoParamSet {
  u32 vps_video_parameter_set_id;
  u32 vps_max_layers;
  u32 vps_max_sub_layers;
  u32 vps_temporal_id_nesting_flag;
  struct Profile profile;
  u32 vps_sub_layer_ordering_info_present_flag;
  u32 vps_max_dec_pic_buffering[MAX_SUB_LAYERS];
  u32 vps_max_num_reorder_pics[MAX_SUB_LAYERS];
  u32 vps_max_latency_increase[MAX_SUB_LAYERS];
  u32 vps_max_layer_id;
  u32 vps_num_layer_sets;
  u32 vps_timing_info_present_flag;
  u32 vps_num_units_in_tick;
  u32 vps_time_scale;
};

u32 HevcDecodeVideoParamSet(struct StrmData *stream,
                            struct VideoParamSet *video_param_set);
u32 HevcCompareVideoParamSets(struct VideoParamSet *sps1,
                              struct VideoParamSet *sps2);
#endif /* #ifdef HEVC_VIDEO_PARAM_SET_H_ */

