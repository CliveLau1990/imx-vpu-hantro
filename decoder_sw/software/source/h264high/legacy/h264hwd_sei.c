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
#include "basetype.h"
#include "h264hwd_vlc.h"
#include "h264hwd_stream.h"
#include "h264hwd_util.h"
#include "h264hwd_sei.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function: h264bsdDecodeSEIParameters

        Functional description:
            Decode SEI parameters from the stream. See standard for details.

        Inputs:
            p_strm_data       pointer to stream data structure

        Outputs:
            p_sei_parameters  decoded information is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data or end of stream

------------------------------------------------------------------------------*/

u32 h264bsdDecodeSeiParameters(seqParamSet_t **sps, strmData_t *p_strm_data,
                               seiParameters_t *p_sei_parameters) {

  /* Variables */

  u32 tmp;
  u32 pay_load_type = 0;
  u32 pay_load_size = 0;
  u32 last_pay_load_type_byte;
  u32 last_pay_load_size_byte;
  strmData_t tmp_strm_data;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_sei_parameters);

  do {
    pay_load_type = 0;

    while(h264bsdShowBits(p_strm_data, 8) == 0xFF) {
      pay_load_type += 255;
      if(h264bsdFlushBits(p_strm_data, 8) == END_OF_STREAM)
        return(END_OF_STREAM);
    }

    tmp = h264bsdGetBits(p_strm_data, 8);
    if(tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    last_pay_load_type_byte = tmp;
    pay_load_type += last_pay_load_type_byte;

    pay_load_size = 0;

    while(h264bsdShowBits(p_strm_data, 8) == 0xFF) {
      pay_load_size += 255;
      if(h264bsdFlushBits(p_strm_data, 8) == END_OF_STREAM)
        return(END_OF_STREAM);
    }

    tmp = h264bsdGetBits(p_strm_data, 8);
    if(tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    last_pay_load_size_byte = tmp;
    pay_load_size += last_pay_load_size_byte;

    tmp_strm_data = *p_strm_data;
    switch (pay_load_type) {
    case SEI_BUFFERING_PERIOD:
      h264bsdDecodeBufferingPeriodInfo(sps, &tmp_strm_data,
                                       &p_sei_parameters->buffering_period_info);
      if(tmp == HANTRO_NOK) {
        p_sei_parameters->buffering_period_info.exist_flag = 0;
        return(HANTRO_NOK);
      } else
        p_sei_parameters->buffering_period_info.exist_flag = 1;
      break;

    case SEI_PIC_TIMING:
      h264bsdDecodePicTimingInfo(sps,
                                 &tmp_strm_data, &p_sei_parameters->pic_timing_info,
                                 &p_sei_parameters->buffering_period_info);
      if(tmp == HANTRO_NOK) {
        p_sei_parameters->pic_timing_info.exist_flag = 0;
        return(HANTRO_NOK);
      } else
        p_sei_parameters->pic_timing_info.exist_flag = 1;
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

    case SEI_DEC_REF_PIC_MARKING_REPETITION:
      break;

    case SEI_SPARE_PIC:
      break;

    case SEI_SCENE_INFO:
      break;

    case SEI_SUB_SEQ_INFO:
      break;

    case SEI_SUB_SEQ_LAYER_CHARACTERISTICS:
      break;

    case SEI_SUB_SEQ_CHARACTERISTICS:
      break;

    case SEI_FULL_FRAME_FREEZE:
      break;

    case SEI_FULL_FRAME_FREEZE_RELEASE:
      break;

    case SEI_FULL_FRAME_SNAPSHOT:
      break;

    case SEI_PROGRESSIVE_REFINEMENT_SEGMENT_START:
      break;

    case SEI_PROGRESSIVE_REFINEMENT_SEGMENT_END:
      break;

    case SEI_MOTION_CONSTRAINED_SLICE_GROUP_SET:
      break;

    case SEI_FILM_GRAIN_CHARACTERISTICS:
      break;

    case SEI_DEBLOCKING_FILTER_DISPLAY_PREFERENCE:
      break;

    case SEI_STEREO_VIDEO_INFO:
      break;

    case SEI_TONE_MAPPING:
      break;

    case SEI_POST_FILTER_HINTS:
      break;

    case SEI_FRAME_PACKING_ARRANGEMENT:
      break;

    case SEI_GREEN_METADATA:
      break;

    default:
      break;
    }

    if(h264bsdFlushBits(p_strm_data, pay_load_size * 8) == END_OF_STREAM)
      return(END_OF_STREAM);


  } while(h264bsdMoreRbspData(p_strm_data));

  return(HANTRO_OK);
}


u32 h264bsdDecodeBufferingPeriodInfo(seqParamSet_t **sps,
                                     strmData_t *p_strm_data,
                                     bufferingPeriodInfo_t *p_buffering_period_info) {

  /* Variables */

  u32 tmp, i;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_buffering_period_info);
  ASSERT(sps);

  seqParamSet_t *p_seq_param_set;
  (void) DWLmemset(p_buffering_period_info, 0, sizeof(bufferingPeriodInfo_t));

  p_buffering_period_info->seq_parameter_set_id =
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_buffering_period_info->seq_parameter_set_id);
  if (tmp != HANTRO_OK)
    return(tmp);

  p_seq_param_set = sps[p_buffering_period_info->seq_parameter_set_id];
  if (p_seq_param_set == NULL || p_seq_param_set->vui_parameters == NULL)
    return (HANTRO_NOK);
  if (p_seq_param_set->vui_parameters->error_hrdparameter_flag)
    return (HANTRO_NOK);

  if(p_seq_param_set->vui_parameters_present_flag) {
    if(p_seq_param_set->vui_parameters->nal_hrd_parameters_present_flag) {

      for(i = 0; i < p_seq_param_set->vui_parameters->nal_hrd_parameters.cpb_cnt; i++) {
        tmp = h264bsdShowBits(p_strm_data,
                              p_seq_param_set->vui_parameters->nal_hrd_parameters.initial_cpb_removal_delay_length);
        if (h264bsdFlushBits(p_strm_data,
                             p_seq_param_set->vui_parameters->nal_hrd_parameters.initial_cpb_removal_delay_length) == END_OF_STREAM)
          return(END_OF_STREAM);
        p_buffering_period_info->initial_cpb_removal_delay[i] = tmp;

        tmp = h264bsdShowBits(p_strm_data,
                              p_seq_param_set->vui_parameters->nal_hrd_parameters.initial_cpb_removal_delay_length);
        if (h264bsdFlushBits(p_strm_data,
                             p_seq_param_set->vui_parameters->nal_hrd_parameters.initial_cpb_removal_delay_length) == END_OF_STREAM)
          return(END_OF_STREAM);
        p_buffering_period_info->initial_cpb_removal_delay_offset[i] = tmp;
      }
    }

    if(p_seq_param_set->vui_parameters->vcl_hrd_parameters_present_flag) {

      for(i = 0; i < p_seq_param_set->vui_parameters->vcl_hrd_parameters.cpb_cnt; i++) {
        tmp = h264bsdShowBits(p_strm_data,
                              p_seq_param_set->vui_parameters->vcl_hrd_parameters.initial_cpb_removal_delay_length);
        if (h264bsdFlushBits(p_strm_data,
                             p_seq_param_set->vui_parameters->vcl_hrd_parameters.initial_cpb_removal_delay_length) == END_OF_STREAM)
          return(END_OF_STREAM);
        p_buffering_period_info->initial_cpb_removal_delay[i] = tmp;

        tmp = h264bsdShowBits(p_strm_data,
                              p_seq_param_set->vui_parameters->vcl_hrd_parameters.initial_cpb_removal_delay_length);
        if (h264bsdFlushBits(p_strm_data,
                             p_seq_param_set->vui_parameters->vcl_hrd_parameters.initial_cpb_removal_delay_length) == END_OF_STREAM)
          return(END_OF_STREAM);
        p_buffering_period_info->initial_cpb_removal_delay_offset[i] = tmp;
      }
    }
  }

  return(HANTRO_OK);
}

u32 h264bsdDecodePicTimingInfo(seqParamSet_t **sps, strmData_t *p_strm_data,
                               picTimingInfo_t *p_pic_timing_info,
                               bufferingPeriodInfo_t *p_buffering_period_info) {

  /* Variables */

  u32 tmp, i;
  u32 CpbDpbDelaysPresentFlag;
  u32 cpb_removal_len;
  u32 dpb_output_len;
  u32 pic_struct_present_flag;
  u32 NumClockTs = 0;
  u32 time_offset_length;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_pic_timing_info);
  ASSERT(sps);

  (void) DWLmemset(p_pic_timing_info, 0, sizeof(picTimingInfo_t));

  seqParamSet_t *p_seq_param_set = sps[p_buffering_period_info->seq_parameter_set_id];
  if (p_seq_param_set == NULL || p_seq_param_set->vui_parameters == NULL)
    return (HANTRO_NOK);
  if (p_seq_param_set->vui_parameters->error_hrdparameter_flag)
    return (HANTRO_NOK);

  CpbDpbDelaysPresentFlag = p_seq_param_set->vui_parameters_present_flag
                            && (( p_seq_param_set->vui_parameters->nal_hrd_parameters_present_flag != 0)
                                || (p_seq_param_set->vui_parameters->vcl_hrd_parameters_present_flag != 0));

  if(CpbDpbDelaysPresentFlag) {
    if(p_seq_param_set->vui_parameters_present_flag) {
      if(p_seq_param_set->vui_parameters->nal_hrd_parameters_present_flag) {
        cpb_removal_len =
          p_seq_param_set->vui_parameters->nal_hrd_parameters.cpb_removal_delay_length;
        dpb_output_len  =
          p_seq_param_set->vui_parameters->nal_hrd_parameters.dpb_output_delay_length;
      }

      if(p_seq_param_set->vui_parameters->vcl_hrd_parameters_present_flag) {
        cpb_removal_len =
          p_seq_param_set->vui_parameters->vcl_hrd_parameters.cpb_removal_delay_length;
        dpb_output_len  =
          p_seq_param_set->vui_parameters->vcl_hrd_parameters.dpb_output_delay_length;
      }
    }

    if(p_seq_param_set->vui_parameters->nal_hrd_parameters_present_flag
        || p_seq_param_set->vui_parameters->vcl_hrd_parameters_present_flag) {
      tmp = h264bsdGetBits(p_strm_data, cpb_removal_len);
      if(tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_pic_timing_info->cpb_removal_delay = tmp;

      tmp = h264bsdGetBits(p_strm_data, dpb_output_len);
      if(tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_pic_timing_info->dpb_output_delay = tmp;
    }
  }

  if(!p_seq_param_set->vui_parameters_present_flag)
    pic_struct_present_flag = 0;
  else
    pic_struct_present_flag = p_seq_param_set->vui_parameters->pic_struct_present_flag;

  if(pic_struct_present_flag) {
    tmp = h264bsdGetBits(p_strm_data, 4);
    if(tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_pic_timing_info->pic_struct = tmp;

    switch (p_pic_timing_info->pic_struct) {
    case 0:
    case 1:
    case 2:
      NumClockTs = 1;
      break;

    case 3:
    case 4:
    case 7:
      NumClockTs = 2;
      break;

    case 5:
    case 6:
    case 8:
      NumClockTs = 3;
      break;
    default:
      break;
    }

    for (i = 0; i < NumClockTs; i++) {
      tmp = h264bsdGetBits(p_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_pic_timing_info->clock_timestamp_flag[i] = tmp;

      if( p_pic_timing_info->clock_timestamp_flag[i]) {
        tmp = h264bsdGetBits(p_strm_data, 2);
        if(tmp == END_OF_STREAM)
          return(END_OF_STREAM);
        p_pic_timing_info->ct_type = tmp;

        tmp = h264bsdGetBits(p_strm_data, 1);
        if(tmp == END_OF_STREAM)
          return(END_OF_STREAM);
        p_pic_timing_info->nuit_field_based_flag = tmp;

        tmp = h264bsdGetBits(p_strm_data, 5);
        if(tmp == END_OF_STREAM)
          return(END_OF_STREAM);
        p_pic_timing_info->counting_type = tmp;

        tmp = h264bsdGetBits(p_strm_data, 1);
        if(tmp == END_OF_STREAM)
          return(END_OF_STREAM);
        p_pic_timing_info->full_timestamp_flag = tmp;

        tmp = h264bsdGetBits(p_strm_data, 1);
        if(tmp == END_OF_STREAM)
          return(END_OF_STREAM);
        p_pic_timing_info->discontinuity_flag = tmp;

        tmp = h264bsdGetBits(p_strm_data, 1);
        if(tmp == END_OF_STREAM)
          return(END_OF_STREAM);
        p_pic_timing_info->cnt_dropped_flag = tmp;

        tmp = h264bsdGetBits(p_strm_data, 8);
        if(tmp == END_OF_STREAM)
          return(END_OF_STREAM);
        p_pic_timing_info->n_frames = tmp;

        if(p_pic_timing_info->full_timestamp_flag) {
          tmp = h264bsdGetBits(p_strm_data, 6);
          if(tmp == END_OF_STREAM)
            return(END_OF_STREAM);
          p_pic_timing_info->seconds_value = tmp;

          tmp = h264bsdGetBits(p_strm_data, 6);
          if(tmp == END_OF_STREAM)
            return(END_OF_STREAM);
          p_pic_timing_info->minutes_value = tmp;

          tmp = h264bsdGetBits(p_strm_data, 5);
          if(tmp == END_OF_STREAM)
            return(END_OF_STREAM);
          p_pic_timing_info->hours_value = tmp;
        } else {
          tmp = h264bsdGetBits(p_strm_data, 1);
          if(tmp == END_OF_STREAM)
            return(END_OF_STREAM);
          p_pic_timing_info->seconds_flag = tmp;

          if(p_pic_timing_info->seconds_flag) {
            tmp = h264bsdGetBits(p_strm_data, 6);
            if(tmp == END_OF_STREAM)
              return(END_OF_STREAM);
            p_pic_timing_info->seconds_value = tmp;

            tmp = h264bsdGetBits(p_strm_data, 1);
            if(tmp == END_OF_STREAM)
              return(END_OF_STREAM);
            p_pic_timing_info->minutes_flag = tmp;

            if(p_pic_timing_info->minutes_flag) {
              tmp = h264bsdGetBits(p_strm_data, 6);
              if(tmp == END_OF_STREAM)
                return(END_OF_STREAM);
              p_pic_timing_info->minutes_value = tmp;

              tmp = h264bsdGetBits(p_strm_data, 1);
              if(tmp == END_OF_STREAM)
                return(END_OF_STREAM);
              p_pic_timing_info->hours_flag = tmp;

              if(p_pic_timing_info->hours_flag) {
                tmp = h264bsdGetBits(p_strm_data, 5);
                if(tmp == END_OF_STREAM)
                  return(END_OF_STREAM);
                p_pic_timing_info->minutes_value = tmp;
              }
            }
          }
        }

        if(p_seq_param_set->vui_parameters->vcl_hrd_parameters_present_flag) {
          time_offset_length =
            p_seq_param_set->vui_parameters->vcl_hrd_parameters.time_offset_length;
        } else if(p_seq_param_set->vui_parameters->nal_hrd_parameters_present_flag) {
          time_offset_length =
            p_seq_param_set->vui_parameters->nal_hrd_parameters.time_offset_length;
        } else
          time_offset_length = 24;

        if(time_offset_length) {
          tmp = h264bsdGetBits(p_strm_data, 5);
          if(tmp == END_OF_STREAM)
            return(END_OF_STREAM);
          p_pic_timing_info->time_offset = (i32)tmp;
        } else
          p_pic_timing_info->time_offset = 0;

      }
    }
  }
  return(HANTRO_OK);
}

double Ceil(double a) {
  u32 tmp;
  tmp = (u32) a;
  if ((double) (tmp) < a)
    return ((double) (tmp + 1));
  else
    return ((double) (tmp));
}

u32 h264bsdComputeTimes(seqParamSet_t *sps,
                        seiParameters_t *p_sei_parameters) {
  seqParamSet_t *p_seq_param_set = sps;
  computeTimeInfo_t *time_info = &p_sei_parameters->compute_time_info;
  u32 cbr_flag = 0;
  u32 bit_rate_value = 0;
  u32 bit_rate_scale = 0;
  u32 bit_rate;

  // compute tc
  if(p_seq_param_set->vui_parameters == NULL)
    return(HANTRO_NOK);

  if(!p_sei_parameters->pic_timing_info.exist_flag ||
      !p_sei_parameters->buffering_period_info.exist_flag) {
    p_sei_parameters->pic_timing_info.exist_flag = 0;
    return(HANTRO_NOK);
  }

  if (!p_sei_parameters->pic_timing_info.cpb_removal_delay &&
      !p_sei_parameters->pic_timing_info.dpb_output_delay) {
    p_sei_parameters->pic_timing_info.exist_flag = 0;
    return(HANTRO_NOK);
  }

  if(time_info->is_first_au) {
    if(p_seq_param_set->vui_parameters->update_hrdparameter_flag)
      time_info->hrd_init_flag = 1;
    else
      time_info->hrd_init_flag = 0;
    p_seq_param_set->vui_parameters->update_hrdparameter_flag = 0;
  }
  if(p_seq_param_set->vui_parameters->timing_info_present_flag)
    time_info->clock_tick =
      (double)p_seq_param_set->vui_parameters->num_units_in_tick /
      p_seq_param_set->vui_parameters->time_scale;
  else
    time_info->clock_tick = 0;

  if(p_seq_param_set->vui_parameters->nal_hrd_parameters_present_flag) {
    cbr_flag = p_seq_param_set->vui_parameters->nal_hrd_parameters.cbr_flag[0];
    bit_rate_scale = p_seq_param_set->vui_parameters->nal_hrd_parameters.bit_rate_scale;
    bit_rate_value = p_seq_param_set->vui_parameters->nal_hrd_parameters.bit_rate_value[0];
  }

  else {
    cbr_flag = p_seq_param_set->vui_parameters->vcl_hrd_parameters.cbr_flag[0];
    bit_rate_scale = p_seq_param_set->vui_parameters->vcl_hrd_parameters.bit_rate_scale;
    bit_rate_value = p_seq_param_set->vui_parameters->vcl_hrd_parameters.bit_rate_value[0];
  }

  //compute trn

  if(time_info->is_first_au) {
    if (time_info->hrd_init_flag) {
      time_info->nominal_removal_time =
        p_sei_parameters->buffering_period_info.initial_cpb_removal_delay[0] / 90000.0;
    } else {
      time_info->nominal_removal_time = time_info->prev_nominal_removal_time +
                                        time_info->clock_tick * p_sei_parameters->pic_timing_info.cpb_removal_delay;
    }
    time_info->prev_nominal_removal_time = time_info->nominal_removal_time;
    time_info->nominal_removal_time_first = time_info->nominal_removal_time;
  } else {
    time_info->nominal_removal_time = time_info->nominal_removal_time_first +
                                      time_info->clock_tick * p_sei_parameters->pic_timing_info.cpb_removal_delay;
  }

  if(time_info->is_first_au) {
    time_info->initial_arrival_time_earliest = time_info->nominal_removal_time -
        (p_sei_parameters->buffering_period_info.initial_cpb_removal_delay[0] / 90000.0);
  } else
    time_info->initial_arrival_time_earliest = time_info->nominal_removal_time -
        ((p_sei_parameters->buffering_period_info.initial_cpb_removal_delay[0] +
          p_sei_parameters->buffering_period_info.initial_cpb_removal_delay_offset[0])/ 90000.0);

  //compute tai
  if(time_info->is_first_au)
    time_info->initial_arrival_time = 0;
  else if(cbr_flag == 1)
    time_info->initial_arrival_time = time_info->final_arrival_time;
  else
    time_info->initial_arrival_time =
      (time_info->final_arrival_time >= time_info->initial_arrival_time_earliest) ?
      time_info->final_arrival_time : time_info->initial_arrival_time_earliest;

  //compute taf
  bit_rate = bit_rate_value * (2 << (6 + bit_rate_scale));
  time_info->final_arrival_time =
    time_info->initial_arrival_time + ((double)time_info->access_unit_size) / bit_rate;

  //compute tr
  if(!p_seq_param_set->vui_parameters->low_delay_hrd_flag || (time_info->nominal_removal_time >= time_info->final_arrival_time))
    time_info->cpb_removal_time = time_info->nominal_removal_time;
  else
    time_info->cpb_removal_time =
      time_info->nominal_removal_time + time_info->clock_tick *
      Ceil((time_info->final_arrival_time - time_info->nominal_removal_time) /
           time_info->clock_tick);

  //compute to_dpb
  time_info->dpb_output_time = time_info->cpb_removal_time +
                               time_info->clock_tick * p_sei_parameters->pic_timing_info.dpb_output_delay;
  time_info->is_first_au = 0;
  return(HANTRO_OK);
}
