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

#include "dwl.h"
#include "hevc_vui.h"
#include "basetype.h"
#include "hevc_exp_golomb.h"
#include "hevc_nal_unit.h"
#include "hevc_util.h"
#include "sw_stream.h"
#include "sw_util.h"
#include "hevc_sei.h"
#include <string.h>
#include<math.h>

u32 buffering_period(int layerid, struct StrmData *stream,
                     struct BufPeriodParameters *buf_parameter,
                     struct SeqParamSet **sps) {
  u32 tmp, i, value;
  struct VuiParameters *vui_para;
  u32 initial_cpb_len;
  u32 cpb_delay_len, dpb_delay_len;

  tmp = HevcDecodeExpGolombUnsigned(stream, &value);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  buf_parameter->bp_seq_parameter_set_id = value;
  if (sps[value] == NULL)
    return HANTRO_NOK;
  if (!sps[value]->vui_parameters_present_flag)
    return HANTRO_NOK;
  vui_para = &(sps[value]->vui_parameters);
  initial_cpb_len = vui_para->hrd_parameters.initial_cpb_removal_delay_length;
  cpb_delay_len = vui_para->hrd_parameters.au_cpb_removal_delay_length;
  dpb_delay_len = vui_para->hrd_parameters.dpb_output_delay_length;
  if (!vui_para->hrd_parameters.sub_pic_hrd_params_present_flag) {
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    buf_parameter->irap_cpb_params_present_flag = tmp;
  }
  if (buf_parameter->irap_cpb_params_present_flag) {
    tmp = SwShowBits(stream, cpb_delay_len);
    if (SwFlushBits(stream, cpb_delay_len) == END_OF_STREAM)
      return(END_OF_STREAM);
    buf_parameter->cpb_delay_offset = tmp;
    tmp = SwShowBits(stream, dpb_delay_len);
    if (SwFlushBits(stream, dpb_delay_len) == END_OF_STREAM)
      return(END_OF_STREAM);
    buf_parameter->dpb_delay_offset = tmp;
  }
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  buf_parameter->concatenation_flag = tmp;
  tmp = SwShowBits(stream, cpb_delay_len);
  if (SwFlushBits(stream, cpb_delay_len) == END_OF_STREAM)
    return(END_OF_STREAM);
  buf_parameter->au_cpb_removal_delay_delta = tmp + 1;
  if (vui_para->hrd_parameters.nal_hrd_parameters_present_flag) {
    for (i=0; i<=vui_para->hrd_parameters.cpb_cnt[layerid]; i++) {
      tmp = SwShowBits(stream, initial_cpb_len);
      if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
        return(END_OF_STREAM);
      buf_parameter->nal_initial_cpb_removal_delay[i] = tmp;
      tmp = SwShowBits(stream, initial_cpb_len);
      if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
        return(END_OF_STREAM);
      buf_parameter->nal_initial_cpb_removal_offset[i] = tmp;
      if (vui_para->hrd_parameters.sub_pic_hrd_params_present_flag ||
          buf_parameter->irap_cpb_params_present_flag) {
        tmp = SwShowBits(stream, initial_cpb_len);
        if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
          return(END_OF_STREAM);
        buf_parameter->nal_initial_alt_cpb_removal_delay[i] = tmp;
        tmp = SwShowBits(stream, initial_cpb_len);
        if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
          return(END_OF_STREAM);
        buf_parameter->nal_initial_alt_cpb_removal_offset[i] = tmp;
      }
    }
  }

  if (vui_para->hrd_parameters.vcl_hrd_parameters_present_flag) {
    for (i=0; i<=vui_para->hrd_parameters.cpb_cnt[layerid]; i++) {
      tmp = SwShowBits(stream, initial_cpb_len);
      if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
        return(END_OF_STREAM);
      buf_parameter->vcl_initial_cpb_removal_delay[i] = tmp;
      tmp = SwShowBits(stream, initial_cpb_len);
      if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
        return(END_OF_STREAM);
      buf_parameter->vcl_initial_cpb_removal_offset[i] = tmp;
      if (vui_para->hrd_parameters.sub_pic_hrd_params_present_flag ||
          buf_parameter->irap_cpb_params_present_flag) {
        tmp = SwShowBits(stream, initial_cpb_len);
        if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
          return(END_OF_STREAM);
        buf_parameter->vcl_initial_alt_cpb_removal_delay[i] = tmp;
        tmp = SwShowBits(stream, initial_cpb_len);
        if (SwFlushBits(stream, initial_cpb_len) == END_OF_STREAM)
          return(END_OF_STREAM);
        buf_parameter->vcl_initial_alt_cpb_removal_offset[i] = tmp;
      }
    }
  }
  return HANTRO_OK;
}

u32 pic_timing(struct StrmData *stream,
               struct BufPeriodParameters *buf_parameter,
               struct PicTimingParameters *pic_parameter,
               struct SeqParamSet **sps) {
  u32 tmp;
  struct VuiParameters *vui_parameters;
  u32 cpb_delay_len, dpb_delay_len, dpb_delay_du_len;
  u32 pps_id = buf_parameter->bp_seq_parameter_set_id;
  if (sps[pps_id] == NULL)
    return HANTRO_NOK;
  if (!sps[pps_id]->vui_parameters_present_flag)
    return HANTRO_NOK;
  vui_parameters = &(sps[pps_id]->vui_parameters);
  cpb_delay_len = vui_parameters->hrd_parameters.au_cpb_removal_delay_length;
  dpb_delay_len = vui_parameters->hrd_parameters.dpb_output_delay_length;
  dpb_delay_du_len = vui_parameters->hrd_parameters.dpb_output_delay_du_length;
  if (vui_parameters->frame_field_info_present_flag) {
    tmp = SwGetBits(stream, 4);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    pic_parameter->pic_struct = tmp;
    tmp = SwGetBits(stream, 2);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  }
  if (vui_parameters->hrd_parameters.nal_hrd_parameters_present_flag ||
      vui_parameters->hrd_parameters.vcl_hrd_parameters_present_flag) {
    tmp = SwShowBits(stream, cpb_delay_len);
    if (SwFlushBits(stream, cpb_delay_len) == END_OF_STREAM)
      return(END_OF_STREAM);
    pic_parameter->au_cpb_removal_delay = tmp + 1;
    tmp = SwShowBits(stream, dpb_delay_len);
    if (SwFlushBits(stream, dpb_delay_len) == END_OF_STREAM)
      return(END_OF_STREAM);
    pic_parameter->pic_dpb_output_delay = tmp;
    if (vui_parameters->hrd_parameters.sub_pic_hrd_params_present_flag) {
      tmp = SwShowBits(stream, dpb_delay_du_len);
      if (SwFlushBits(stream, dpb_delay_du_len) == END_OF_STREAM)
        return(END_OF_STREAM);
      pic_parameter->pic_dpb_output_du_delay = tmp;
    }
  }
  return HANTRO_OK;
}

u32 mastering_display_colour_volume(struct StrmData *stream,
                                    struct MasterDisParameters *dis_parameter) {
  u32 c;
  u32 tmp;

  for (c = 0; c < 3; c++) {
    tmp = SwGetBits(stream, 16);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    dis_parameter->display_primaries_x[c] = tmp;

    tmp = SwGetBits(stream, 16);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    dis_parameter->display_primaries_y[c] = tmp;
  }

  tmp = SwGetBits(stream, 16);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  dis_parameter->white_point_x = tmp;

  tmp = SwGetBits(stream, 16);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  dis_parameter->white_point_y = tmp;

  tmp = SwShowBits(stream, 32);
  if (SwFlushBits(stream, 32) == END_OF_STREAM)
    return(END_OF_STREAM);
  dis_parameter->max_display_mastering_luminance = tmp;

  tmp = SwShowBits(stream, 32);
  if (SwFlushBits(stream, 32) == END_OF_STREAM)
    return(END_OF_STREAM);
  dis_parameter->min_display_mastering_luminance = tmp;

  return HANTRO_OK;
}

u32 content_light_level_info(struct StrmData *stream,
                                    struct LightLevelParameters *light_parameter) {
  u32 tmp;

  tmp = SwGetBits(stream, 16);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  light_parameter->max_content_light_level = tmp;

  tmp = SwGetBits(stream, 16);
  if (tmp == END_OF_STREAM) return (END_OF_STREAM);
  light_parameter->max_pic_average_light_level = tmp;

  return HANTRO_OK;
}


u32 HevcDecodeSEIParameters(struct StrmData *stream, int layerid,
                            struct SEIParameters *sei_parameters,
                            struct SeqParamSet **sps) {
  u32 tmp;
  enum SeiType pay_load_type = 0;
  u32 pay_load_size = 0;
  struct StrmData tmpstream;

  ASSERT(stream);
  ASSERT(sei_parameters);
  do {
    pay_load_type = 0;
    while (SwShowBits(stream, 8) == 0xFF) {
      pay_load_type += 255;
      tmp = SwFlushBits(stream, 8);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    }
    tmp = SwGetBits(stream, 8);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    pay_load_type += tmp;

    pay_load_size = 0;
    while(SwShowBits(stream, 8) == 0xFF) {
      pay_load_size += 255;
      tmp = SwFlushBits(stream, 8);
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    }
    tmp = SwGetBits(stream, 8);
    if (tmp == END_OF_STREAM) return (END_OF_STREAM);
    pay_load_size += tmp;

    memcpy(&tmpstream, stream , sizeof(struct StrmData));

    switch (pay_load_type) {
    case SEI_BUFFERING_PERIOD:
      tmp = buffering_period(layerid, &tmpstream, &sei_parameters->buf_parameter, sps);
      if (tmp != HANTRO_OK)
        sei_parameters->bufperiod_present_flag = 0;
      else
        sei_parameters->bufperiod_present_flag = 1;
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      break;
    case SEI_PIC_TIMING:
      tmp = pic_timing(&tmpstream, &sei_parameters->buf_parameter, &sei_parameters->pic_parameter, sps);
      if (tmp != HANTRO_OK)
        sei_parameters->pictiming_present_flag = 0;
      else
        sei_parameters->pictiming_present_flag = 1;
      if (tmp == END_OF_STREAM) return (END_OF_STREAM);
      break;

    case SEI_PAN_SCAN_RECT:
      break;

    case SEI_FILLER_PAYLOAD:
      break;

    case SEI_USER_DATA_REGISTERED_ITU_T_T35:
      break;

    case SEI_USER_DATA_UNREGISTERED:
      break;

    case SEI_RECOVERY_POINT:
      break;

    case SEI_SCENE_INFO:
      break;

    case SEI_PICTURE_SNAPSHOT:
      break;

    case SEI_PROGRESSIVE_REFINEMENT_SEGMENT_START:
      break;

    case SEI_PROGRESSIVE_REFINEMENT_SEGMENT_END:
      break;

    case SEI_FILM_GRAIN_CHARACTERISTICS:
      break;

    case SEI_POST_FILTER_HINTS:
      break;

    case SEI_TONE_MAPPING_INFO:
      break;

    case SEI_FRAME_PACKING_ARRANGEMENT:
      break;

    case SEI_DISPLAY_ORIENTATION:
      break;

    case SEI_STRUCTURE_OF_PICTURES_INFO:
      break;

    case SEI_ACTIVE_PARAMETER_SETS:
      break;

    case SEI_DECODING_UNIT_INFO:
      break;

    case SEI_TEMPORAL_SUB_LAYER_ZERO_INDEX:
      break;

    case SEI_SCALABLE_NESTING:
      break;

    case SEI_REGION_REFRESH_INFO:
      break;

    case SEI_MASTERING_DISPLAY_COLOR_VOLUME:
      tmp = mastering_display_colour_volume(&tmpstream, &sei_parameters->dis_parameter);
      if (tmp != HANTRO_OK)
        sei_parameters->mastering_display_present_flag = 0;
      else
        sei_parameters->mastering_display_present_flag = 1;
      break;
    case SEI_CONTENT_LIGHT_LEVEL_INFO:
      tmp = content_light_level_info(&tmpstream, &sei_parameters->light_parameter);
      if (tmp != HANTRO_OK)
        sei_parameters->lightlevel_present_flag = 0;
      else
        sei_parameters->lightlevel_present_flag = 1;
      break;

    default:
      break;
    }

    if (SwFlushBits(stream, 8 * pay_load_size) == END_OF_STREAM)
      return(END_OF_STREAM);
  } while(HevcMoreRbspData(stream));
  return HANTRO_OK;
}

double Ceil(double a) {
  u32 tmp;
  tmp = (u32) a;
  if ((double) (tmp) < a)
    return ((double) (tmp + 1));
  else
    return ((double) (tmp));
}
u32 HevcDecodeRHDParameters(int stream_len,
                            struct SEIParameters *sei_parameters,
                            struct NalUnit *nal_unit,
                            struct SeqParamSet *sps) {
  u32 cpb_delay_offset, dpb_delay_offset;
  u32 init_cpb_dalay, init_cpb_delay_offset;
  u32 bit_rate;
  double au_nominal_remove_time;
  double base_time;
  double init_arrival_earliest_time;
  double init_arrival_time;
  double au_finall_arrival_time;
  u32 tmp_cpb_removal_delay;
  u32 cpb_removal_delay_val, max_cpb_removal_delay_val;
  u32 cpb_removal_delay_msb;
  u32 layerid = nal_unit->temporal_id;
  struct DpbOutDelay *time_parameter;
  struct VuiParameters *vui_parameters;
  if (sps == NULL)
    return HANTRO_NOK;
  if (!sei_parameters->pic_parameter.au_cpb_removal_delay &&
      !sei_parameters->pic_parameter.pic_dpb_output_delay)
    return HANTRO_NOK;
  time_parameter = &(sei_parameters->time_parameter);
  vui_parameters = &(sps->vui_parameters);
  time_parameter->first_unit_flag = nal_unit->nal_unit_type == NAL_CODED_SLICE_BLA_W_LP ||
                                    nal_unit->nal_unit_type == NAL_CODED_SLICE_BLA_W_DLP ||
                                    nal_unit->nal_unit_type == NAL_CODED_SLICE_BLA_N_LP ||
                                    nal_unit->nal_unit_type == NAL_CODED_SLICE_IDR_W_LP ||
                                    nal_unit->nal_unit_type == NAL_CODED_SLICE_IDR_N_LP ||
                                    nal_unit->nal_unit_type == NAL_CODED_SLICE_CRA;
  if (time_parameter->first_unit_flag) {
    if (sei_parameters->vui_parameters_update_flag)
      time_parameter->hrd_init_flag = 1;
    else
      time_parameter->hrd_init_flag = 0;
    sei_parameters->vui_parameters_update_flag = 0;
  }
  if (time_parameter->first_unit_flag) {
    if ((nal_unit->nal_unit_type == NAL_CODED_SLICE_BLA_W_DLP ||
         nal_unit->nal_unit_type == NAL_CODED_SLICE_BLA_N_LP) &&
        (sei_parameters->buf_parameter.irap_cpb_params_present_flag))
      time_parameter->alt_flag = 1;
    else
      time_parameter->alt_flag = 0;
  }
  time_parameter->pre_nondiscard_flag = ((nal_unit->temporal_id == 0) &&
                                         (nal_unit->nal_unit_type != NAL_CODED_SLICE_RADL_N &&
                                          nal_unit->nal_unit_type != NAL_CODED_SLICE_RADL_R &&
                                          nal_unit->nal_unit_type != NAL_CODED_SLICE_RASL_N &&
                                          nal_unit->nal_unit_type != NAL_CODED_SLICE_RASL_R)) ||
                                        ((nal_unit->temporal_id != 0) &&
                                         (nal_unit->nal_unit_type == NAL_CODED_SLICE_TRAIL_N ||
                                          nal_unit->nal_unit_type == NAL_CODED_SLICE_TSA_N ||
                                          nal_unit->nal_unit_type == NAL_CODED_SLICE_STSA_N ||
                                          nal_unit->nal_unit_type == NAL_CODED_SLICE_RADL_N ||
                                          nal_unit->nal_unit_type == NAL_CODED_SLICE_RASL_N));

  time_parameter->clock_tick = ((double) vui_parameters->vui_num_units_in_tick) / vui_parameters->vui_time_scale;
  if (time_parameter->alt_flag) {
    cpb_delay_offset = sei_parameters->buf_parameter.cpb_delay_offset;
    dpb_delay_offset = sei_parameters->buf_parameter.dpb_delay_offset;
    if (vui_parameters->hrd_parameters.nal_hrd_parameters_present_flag) {
      init_cpb_dalay = sei_parameters->buf_parameter.nal_initial_alt_cpb_removal_delay[0];
      init_cpb_delay_offset = sei_parameters->buf_parameter.nal_initial_alt_cpb_removal_offset[0];
    } else {
      init_cpb_dalay = sei_parameters->buf_parameter.vcl_initial_alt_cpb_removal_delay[0];
      init_cpb_delay_offset = sei_parameters->buf_parameter.vcl_initial_alt_cpb_removal_offset[0];
    }
  } else {
    cpb_delay_offset = 0;
    dpb_delay_offset = 0;
    if (vui_parameters->hrd_parameters.nal_hrd_parameters_present_flag) {
      init_cpb_dalay = sei_parameters->buf_parameter.nal_initial_cpb_removal_delay[0];
      init_cpb_delay_offset = sei_parameters->buf_parameter.nal_initial_cpb_removal_offset[0];
    } else {
      init_cpb_dalay = sei_parameters->buf_parameter.vcl_initial_cpb_removal_delay[0];
      init_cpb_delay_offset = sei_parameters->buf_parameter.vcl_initial_cpb_removal_offset[0];
    }
  }
  if (vui_parameters->hrd_parameters.sub_pic_hrd_params_present_flag)
    bit_rate = vui_parameters->hrd_parameters.sub_hrd_parameters[layerid].bit_rate_du_value[0] <<
               (6 + vui_parameters->hrd_parameters.bit_rate_scale);
  else
    bit_rate = vui_parameters->hrd_parameters.sub_hrd_parameters[layerid].bit_rate_value[0] <<
               (6 + vui_parameters->hrd_parameters.bit_rate_scale);
  if (time_parameter->hrd_init_flag) {
    cpb_removal_delay_val = sei_parameters->pic_parameter.au_cpb_removal_delay;
    cpb_removal_delay_msb = 0;
  } else {
    max_cpb_removal_delay_val = 1 << vui_parameters->hrd_parameters.au_cpb_removal_delay_length;
    if (sei_parameters->pic_parameter.au_cpb_removal_delay <= time_parameter->precpb_removal_delay)
      cpb_removal_delay_msb = time_parameter->precpb_removal_delay_msb + max_cpb_removal_delay_val;
    else
      cpb_removal_delay_msb = time_parameter->precpb_removal_delay_msb;
    cpb_removal_delay_val = cpb_removal_delay_msb + sei_parameters->pic_parameter.au_cpb_removal_delay;
  }
  if (time_parameter->pre_nondiscard_flag) {
    time_parameter->precpb_removal_delay = sei_parameters->pic_parameter.au_cpb_removal_delay;
    time_parameter->precpb_removal_delay_msb = cpb_removal_delay_msb;
  }
  if (time_parameter->first_unit_flag) {
    if (time_parameter->hrd_init_flag) {
      au_nominal_remove_time = init_cpb_dalay / 90000.0;
    } else {
      if (sei_parameters->buf_parameter.concatenation_flag) {
        base_time = time_parameter->prefirstpic_au_nominal_time;
        tmp_cpb_removal_delay = cpb_removal_delay_val;
      } else {
        base_time = time_parameter->prenondiscardable_au_nominal_time;
        tmp_cpb_removal_delay = MAX(sei_parameters->buf_parameter.au_cpb_removal_delay_delta,
                                    Ceil(init_cpb_dalay / 90000.0 + (time_parameter->pre_au_finall_arrival_time -
                                         time_parameter->pre_au_nominal_remove_time) / time_parameter->clock_tick));
      }
      au_nominal_remove_time = base_time +
                               time_parameter->clock_tick * (tmp_cpb_removal_delay - cpb_delay_offset);
    }
  } else {
    au_nominal_remove_time = time_parameter->firstpic_au_nominal_time +
                             time_parameter->clock_tick * (cpb_removal_delay_val - cpb_delay_offset);
  }
  if (time_parameter->first_unit_flag) {
    time_parameter->firstpic_au_nominal_time = au_nominal_remove_time;
    time_parameter->prefirstpic_au_nominal_time = au_nominal_remove_time;
  }
  if (time_parameter->pre_nondiscard_flag)
    time_parameter->prenondiscardable_au_nominal_time = au_nominal_remove_time;
  time_parameter->pre_au_nominal_remove_time = au_nominal_remove_time;
  if (time_parameter->first_unit_flag)
    init_arrival_earliest_time = au_nominal_remove_time - (init_cpb_dalay / 90000.0);
  else
    init_arrival_earliest_time = au_nominal_remove_time - ((init_cpb_dalay + init_cpb_delay_offset) /90000.0);
  if (time_parameter->first_unit_flag)
    init_arrival_time = 0;
  else {
    if (vui_parameters->hrd_parameters.sub_hrd_parameters[layerid].cbr_flag[0])
      init_arrival_time = time_parameter->pre_au_finall_arrival_time;
    else
      init_arrival_time = MAX(time_parameter->pre_au_finall_arrival_time, init_arrival_earliest_time);
  }
  au_finall_arrival_time = init_arrival_time + ((double) stream_len) / bit_rate;
  time_parameter->pre_au_finall_arrival_time = au_finall_arrival_time;
  if ((!vui_parameters->hrd_parameters.low_delay_hrd_flag[layerid]) ||
      (au_nominal_remove_time >= au_finall_arrival_time))
    time_parameter->cpb_removal_time = au_nominal_remove_time;
  else
    time_parameter->cpb_removal_time = au_nominal_remove_time + time_parameter->clock_tick *
                                       Ceil((au_finall_arrival_time - au_nominal_remove_time) / time_parameter->clock_tick);
  if (!vui_parameters->hrd_parameters.sub_pic_hrd_params_present_flag) {
    time_parameter->dpb_output_time = time_parameter->cpb_removal_time +
                                      time_parameter->clock_tick * sei_parameters->pic_parameter.pic_dpb_output_delay;
    if (time_parameter->first_unit_flag)
      time_parameter->dpb_output_time -= time_parameter->clock_tick * dpb_delay_offset;
  }
  return (HANTRO_OK);
}
