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

#include "hevc_seq_param_set.h"
#include "hevc_exp_golomb.h"
#include "hevc_video_param_set.h"
#include "hevc_cfg.h"
#include "hevc_byte_stream.h"
#include "hevc_util.h"
#include "dwl.h"
#include <stdlib.h>

u32 HevcDecodeVideoParamSet(struct StrmData *stream,
                            struct VideoParamSet *video_param_set) {

  u32 tmp, i, j, value;

  ASSERT(stream);
  ASSERT(video_param_set);

  (void)DWLmemset(video_param_set, 0, sizeof(struct VideoParamSet));

  /* video_parameter_set_id */
  tmp = SwGetBits(stream, 4);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  video_param_set->vps_video_parameter_set_id = tmp;
  if (video_param_set->vps_video_parameter_set_id >= MAX_NUM_VIDEO_PARAM_SETS) {
    ERROR_PRINT("video_parameter_set_id");
    return (HANTRO_NOK);
  }

  tmp = SwFlushBits(stream, 2);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);

  tmp = SwGetBits(stream, 6);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  video_param_set->vps_max_layers = tmp + 1;

  tmp = SwGetBits(stream, 3);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  if (tmp < 0 || tmp > 6) {
    ERROR_PRINT("video_parameter_set_id");
    return (HANTRO_NOK);
  }
  video_param_set->vps_max_sub_layers = tmp + 1;

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  video_param_set->vps_temporal_id_nesting_flag = tmp;

  tmp = SwFlushBits(stream, 16);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);

  tmp = ProfileAndLevel(stream, &video_param_set->profile, 1,
                        video_param_set->vps_max_sub_layers);

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  video_param_set->vps_sub_layer_ordering_info_present_flag = tmp;

  i = video_param_set->vps_sub_layer_ordering_info_present_flag ? 0
      : video_param_set->vps_max_sub_layers - 1;
  for (; i < video_param_set->vps_max_sub_layers; i++) {
    /* max_dec_pic_buffering */
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return (tmp);
    video_param_set->vps_max_dec_pic_buffering[i] = value;
    /* num_reorder_pics */
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return (tmp);
    video_param_set->vps_max_num_reorder_pics[i] = value;
    /* max_latency_increase */
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return (tmp);
    video_param_set->vps_max_latency_increase[i] = value;
  }

  tmp = SwGetBits(stream, 6);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  video_param_set->vps_max_layer_id = tmp;

  tmp = HevcDecodeExpGolombUnsigned(stream, &value);
  if (tmp != HANTRO_OK) return (tmp);
  if (value < 0 || value > 1023) {
    ERROR_PRINT("video_parameter_set_id");
    return (HANTRO_NOK);
  }
  video_param_set->vps_num_layer_sets = value + 1;

  for (i = 1; i < video_param_set->vps_num_layer_sets; i++) {
    for (j = 0; j <= video_param_set->vps_max_layer_id; j++) {
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
    }
  }

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  video_param_set->vps_timing_info_present_flag = tmp;

  if (video_param_set->vps_timing_info_present_flag) {
    tmp = SwShowBits(stream, 32);
    if (SwFlushBits(stream, 32) == END_OF_STREAM)
      return(END_OF_STREAM);
    video_param_set->vps_num_units_in_tick = tmp;
    tmp = SwShowBits(stream, 32);
    if (SwFlushBits(stream, 32) == END_OF_STREAM)
      return(END_OF_STREAM);
    video_param_set->vps_time_scale = tmp;
  }
  return (HANTRO_OK);
}

/* Returns true if two sequence parameter sets are equal. */
u32 HevcCompareVideoParamSets(struct VideoParamSet *sps1,
                              struct VideoParamSet *sps2) {

  u32 i;
  u8 *p1, *p2;

  /* TODO: should we add memcmp to dwl? */

  p1 = (u8 *)sps1;
  p2 = (u8 *)sps2;
  for (i = 0; i < sizeof(struct VideoParamSet); i++) {
    if (*p1++ != *p2++) return 0;
  }

  return 1;
}

