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

#include "dwl.h"
#include "hevc_vui.h"
#include "basetype.h"
#include "hevc_exp_golomb.h"
#include "hevc_util.h"
#include "sw_stream.h"

u32 HevcDecodeSubHrdParameters(struct StrmData *stream, u32 cpbcnt,
                               u32 sub_pic_hrd_params_present_flag,
                               struct SubHrdParameters_t *sub_hdr_parameters) {
  u32 tmp, i, value;

  ASSERT(stream);
  ASSERT(sub_hdr_parameters);

  (void)DWLmemset(sub_hdr_parameters, 0, sizeof(struct SubHrdParameters_t));
  for (i=0; i <=cpbcnt; i++) {
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    sub_hdr_parameters->bit_rate_value[i] = value + 1;
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    if (sub_pic_hrd_params_present_flag) {
      tmp = HevcDecodeExpGolombUnsigned(stream, &value);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      tmp = HevcDecodeExpGolombUnsigned(stream, &value);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      sub_hdr_parameters->bit_rate_du_value[i] = value + 1;
    }
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    sub_hdr_parameters->cbr_flag[i] = tmp;
  }
  return (tmp != END_OF_STREAM ? HANTRO_OK : END_OF_STREAM);
}

u32 HevcDecodeHrdParameters(struct StrmData *stream, u32 max_sub_layers,
                            struct HrdParameters_t *hdr_parameters) {
  u32 tmp, i, value;

  ASSERT(stream);
  ASSERT(hdr_parameters);

  (void)DWLmemset(hdr_parameters, 0, sizeof(struct HrdParameters_t));
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  hdr_parameters->nal_hrd_parameters_present_flag = tmp;
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  hdr_parameters->vcl_hrd_parameters_present_flag = tmp;
  if (hdr_parameters->nal_hrd_parameters_present_flag ||
      hdr_parameters->vcl_hrd_parameters_present_flag) {
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    hdr_parameters->sub_pic_hrd_params_present_flag = tmp;
    if (hdr_parameters->sub_pic_hrd_params_present_flag) {
      tmp = SwGetBits(stream, 8);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      hdr_parameters->tick_divisor = tmp + 2;
      tmp = SwGetBits(stream, 5);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      hdr_parameters->du_cpb_removal_delay_increment_length = tmp + 1;
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      tmp = SwGetBits(stream, 5);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      hdr_parameters->dpb_output_delay_du_length = tmp + 1;
    }
    tmp = SwGetBits(stream, 4);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    hdr_parameters->bit_rate_scale = tmp;
    tmp = SwGetBits(stream, 4);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    if (hdr_parameters->sub_pic_hrd_params_present_flag) {
      tmp = SwGetBits(stream, 4);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    }
    tmp = SwGetBits(stream, 5);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    hdr_parameters->initial_cpb_removal_delay_length = tmp + 1;
    tmp = SwGetBits(stream, 5);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    hdr_parameters->au_cpb_removal_delay_length = tmp + 1;
    tmp = SwGetBits(stream, 5);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    hdr_parameters->dpb_output_delay_length = tmp + 1;
  }
  for (i=0; i<max_sub_layers; i++) {
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    hdr_parameters->fixed_pic_rate_general_flag[i] = tmp;
    if (!hdr_parameters->fixed_pic_rate_general_flag[i]) {
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      hdr_parameters->fixed_pic_rate_within_cvs_flag[i] = tmp;
    } else
      hdr_parameters->fixed_pic_rate_within_cvs_flag[i] = 1;
    if (hdr_parameters->fixed_pic_rate_within_cvs_flag[i]) {
      tmp = HevcDecodeExpGolombUnsigned(stream, &value);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    } else {
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      hdr_parameters->low_delay_hrd_flag[i] = tmp;
    }
    if (!hdr_parameters->low_delay_hrd_flag[i]) {
      tmp = HevcDecodeExpGolombUnsigned(stream, &value);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      if (value < 0 || value > 32)
        return (HANTRO_NOK);
      else
        hdr_parameters->cpb_cnt[i] = value;
    }
    if (hdr_parameters->nal_hrd_parameters_present_flag ||
        hdr_parameters->vcl_hrd_parameters_present_flag)
      tmp = HevcDecodeSubHrdParameters(stream, hdr_parameters->cpb_cnt[i],
                                       hdr_parameters->sub_pic_hrd_params_present_flag,
                                       &hdr_parameters->sub_hrd_parameters[i]);
  }
  return (tmp != END_OF_STREAM ? HANTRO_OK : END_OF_STREAM);
}

/* Decode VUI parameters from the stream. See standard for details. */
u32 HevcDecodeVuiParameters(struct StrmData *stream, u32 max_sub_layers,
                            struct VuiParameters *vui_parameters) {

  u32 tmp;
  u32 value;

  ASSERT(stream);
  ASSERT(vui_parameters);

  (void)DWLmemset(vui_parameters, 0, sizeof(struct VuiParameters));

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  vui_parameters->aspect_ratio_present_flag = tmp;

  if (vui_parameters->aspect_ratio_present_flag) {
    tmp = SwGetBits(stream, 8);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    vui_parameters->aspect_ratio_idc = tmp;

    if (vui_parameters->aspect_ratio_idc == ASPECT_RATIO_EXTENDED_SAR) {
      tmp = SwGetBits(stream, 16);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      vui_parameters->sar_width = tmp;

      tmp = SwGetBits(stream, 16);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      vui_parameters->sar_height = tmp;
    }
  }

  /* overscan_info_present_flag */
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  if (tmp) {
    /* overscan_appropriate_flag */
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  }

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  vui_parameters->video_signal_type_present_flag = tmp;

  if (vui_parameters->video_signal_type_present_flag) {
    tmp = SwGetBits(stream, 3);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    vui_parameters->video_format = tmp;

    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    vui_parameters->video_full_range_flag = tmp;

    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    vui_parameters->colour_description_present_flag = tmp;

    if (vui_parameters->colour_description_present_flag) {
      tmp = SwGetBits(stream, 8);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      vui_parameters->colour_primaries = tmp;

      tmp = SwGetBits(stream, 8);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      vui_parameters->transfer_characteristics = tmp;

      tmp = SwGetBits(stream, 8);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      vui_parameters->matrix_coefficients = tmp;
    } else {
      vui_parameters->colour_primaries = 2;
      vui_parameters->transfer_characteristics = 2;
      vui_parameters->matrix_coefficients = 2;
    }
  } else {
    vui_parameters->video_format = 5;
    vui_parameters->colour_primaries = 2;
    vui_parameters->transfer_characteristics = 2;
    vui_parameters->matrix_coefficients = 2;
  }

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  vui_parameters->chroma_loc_info_present_flag = tmp;

  if (vui_parameters->chroma_loc_info_present_flag) {
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return(tmp);
    if (value > 5) return(END_OF_STREAM);
    vui_parameters->chroma_sample_loc_type_top_field = value;
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return(tmp);
    if (value > 5) return(END_OF_STREAM);
    vui_parameters->chroma_sample_loc_type_bottom_field = value;
  }

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  vui_parameters->frame_field_info_present_flag = tmp;
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  vui_parameters->default_display_window_flag = tmp;

  if (vui_parameters->default_display_window_flag) {
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return(tmp);
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return(tmp);
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return(tmp);
    tmp = HevcDecodeExpGolombUnsigned(stream, &value);
    if (tmp != HANTRO_OK) return(tmp);
  }

  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  vui_parameters->vui_timing_info_present_flag = tmp;
  if (vui_parameters->vui_timing_info_present_flag) {
    tmp = SwShowBits(stream, 32);
    if (SwFlushBits(stream, 32) == END_OF_STREAM)
      return(END_OF_STREAM);
    vui_parameters->vui_num_units_in_tick = tmp;
    tmp = SwShowBits(stream, 32);
    if (SwFlushBits(stream, 32) == END_OF_STREAM)
      return(END_OF_STREAM);
    vui_parameters->vui_time_scale = tmp;
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    vui_parameters->vui_poc_proportional_to_timing_flag = tmp;
    if (vui_parameters->vui_poc_proportional_to_timing_flag) {
      tmp = HevcDecodeExpGolombUnsigned(stream, &value);
      if (tmp != HANTRO_OK) return(tmp);
    }
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    vui_parameters->vui_hrd_parameters_present_flag = tmp;
    if (vui_parameters->vui_hrd_parameters_present_flag) {
      tmp = HevcDecodeHrdParameters(stream, max_sub_layers, &vui_parameters->hrd_parameters);
    } else {
      vui_parameters->hrd_parameters.initial_cpb_removal_delay_length = 24;
      vui_parameters->hrd_parameters.au_cpb_removal_delay_length = 24;
      vui_parameters->hrd_parameters.dpb_output_delay_length = 24;
    }
  }
  return (tmp);
}
