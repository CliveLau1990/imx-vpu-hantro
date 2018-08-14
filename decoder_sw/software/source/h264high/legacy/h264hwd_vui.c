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

#include "dwl.h"
#include "h264hwd_vui.h"
#include "basetype.h"
#include "h264hwd_vlc.h"
#include "h264hwd_stream.h"
#include "h264hwd_util.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

#define MAX_DPB_SIZE 16
#define MAX_BR       240000 /* for level 5.1 */
#define MAX_CPB      240000 /* for level 5.1 */

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function: h264bsdDecodeVuiParameters

        Functional description:
            Decode VUI parameters from the stream. See standard for details.

        Inputs:
            p_strm_data       pointer to stream data structure

        Outputs:
            p_vui_parameters  decoded information is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data or end of stream

------------------------------------------------------------------------------*/

u32 h264bsdDecodeVuiParameters(strmData_t *p_strm_data,
                               vuiParameters_t *p_vui_parameters) {

  /* Variables */

  u32 tmp;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_vui_parameters);

  (void) DWLmemset(p_vui_parameters, 0, sizeof(vuiParameters_t));

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->aspect_ratio_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->aspect_ratio_present_flag) {
    tmp = h264bsdGetBits(p_strm_data, 8);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->aspect_ratio_idc = tmp;

    if (p_vui_parameters->aspect_ratio_idc == ASPECT_RATIO_EXTENDED_SAR) {
      tmp = h264bsdGetBits(p_strm_data, 16);
      if (tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_vui_parameters->sar_width = tmp;

      tmp = h264bsdGetBits(p_strm_data, 16);
      if (tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_vui_parameters->sar_height = tmp;
    }
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->overscan_info_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->overscan_info_present_flag) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->overscan_appropriate_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->video_signal_type_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->video_signal_type_present_flag) {
    tmp = h264bsdGetBits(p_strm_data, 3);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->video_format = tmp;

    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->video_full_range_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->colour_description_present_flag =
      (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

    if (p_vui_parameters->colour_description_present_flag) {
      tmp = h264bsdGetBits(p_strm_data, 8);
      if (tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_vui_parameters->colour_primaries = tmp;

      tmp = h264bsdGetBits(p_strm_data, 8);
      if (tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_vui_parameters->transfer_characteristics = tmp;

      tmp = h264bsdGetBits(p_strm_data, 8);
      if (tmp == END_OF_STREAM)
        return(END_OF_STREAM);
      p_vui_parameters->matrix_coefficients = tmp;
    } else {
      p_vui_parameters->colour_primaries         = 2;
      p_vui_parameters->transfer_characteristics = 2;
      p_vui_parameters->matrix_coefficients      = 2;
    }
  } else {
    p_vui_parameters->video_format             = 5;
    p_vui_parameters->colour_primaries         = 2;
    p_vui_parameters->transfer_characteristics = 2;
    p_vui_parameters->matrix_coefficients      = 2;
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->chroma_loc_info_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->chroma_loc_info_present_flag) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->chroma_sample_loc_type_top_field);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (p_vui_parameters->chroma_sample_loc_type_top_field > 5)
      return(END_OF_STREAM);

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->chroma_sample_loc_type_bottom_field);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (p_vui_parameters->chroma_sample_loc_type_bottom_field > 5)
      return(END_OF_STREAM);
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->timing_info_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->timing_info_present_flag) {
    tmp = h264bsdShowBits(p_strm_data,32);
    if (h264bsdFlushBits(p_strm_data, 32) == END_OF_STREAM)
      return(END_OF_STREAM);
#ifdef HANTRO_PEDANTIC_MODE
    if (tmp == 0)
      return(HANTRO_NOK);
#endif /* HANTRO_PEDANTIC_MODE */
    p_vui_parameters->num_units_in_tick = tmp;

    tmp = h264bsdShowBits(p_strm_data,32);
    if (h264bsdFlushBits(p_strm_data, 32) == END_OF_STREAM)
      return(END_OF_STREAM);
#ifdef HANTRO_PEDANTIC_MODE
    if (tmp == 0)
      return(HANTRO_NOK);
#endif /* HANTRO_PEDANTIC_MODE */
    p_vui_parameters->time_scale = tmp;

    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->fixed_frame_rate_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->nal_hrd_parameters_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->nal_hrd_parameters_present_flag) {
    tmp = h264bsdDecodeHrdParameters(p_strm_data,
                                     &p_vui_parameters->nal_hrd_parameters);
    if (tmp != HANTRO_OK) {
      /* ignore error */
      return(HANTRO_OK);
    }
  } else {
    p_vui_parameters->nal_hrd_parameters.cpb_cnt          = 1;
    /* MaxBR and MaxCPB should be the values correspondig to the levelIdc
     * in the SPS containing these VUI parameters. However, these values
     * are not used anywhere and maximum for any level will be used here */
    p_vui_parameters->nal_hrd_parameters.bit_rate_value[0] = 1200 * MAX_BR + 1;
    p_vui_parameters->nal_hrd_parameters.cpb_size_value[0] = 1200 * MAX_CPB + 1;
    p_vui_parameters->nal_hrd_parameters.initial_cpb_removal_delay_length = 24;
    p_vui_parameters->nal_hrd_parameters.cpb_removal_delay_length        = 24;
    p_vui_parameters->nal_hrd_parameters.dpb_output_delay_length         = 24;
    p_vui_parameters->nal_hrd_parameters.time_offset_length             = 24;
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->vcl_hrd_parameters_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->vcl_hrd_parameters_present_flag) {
    p_vui_parameters->update_hrdparameter_flag = 1;
    tmp = h264bsdDecodeHrdParameters(p_strm_data,
                                     &p_vui_parameters->vcl_hrd_parameters);
    if (tmp != HANTRO_OK) {
      /* ignore error */
      return(HANTRO_OK);
    }
  } else {
    p_vui_parameters->vcl_hrd_parameters.cpb_cnt          = 1;
    /* MaxBR and MaxCPB should be the values correspondig to the levelIdc
     * in the SPS containing these VUI parameters. However, these values
     * are not used anywhere and maximum for any level will be used here */
    p_vui_parameters->vcl_hrd_parameters.bit_rate_value[0] = 1000 * MAX_BR + 1;
    p_vui_parameters->vcl_hrd_parameters.cpb_size_value[0] = 1000 * MAX_CPB + 1;
    p_vui_parameters->vcl_hrd_parameters.initial_cpb_removal_delay_length = 24;
    p_vui_parameters->vcl_hrd_parameters.cpb_removal_delay_length        = 24;
    p_vui_parameters->vcl_hrd_parameters.dpb_output_delay_length         = 24;
    p_vui_parameters->vcl_hrd_parameters.time_offset_length             = 24;
  }

  if (p_vui_parameters->nal_hrd_parameters_present_flag ||
      p_vui_parameters->vcl_hrd_parameters_present_flag) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->low_delay_hrd_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->pic_struct_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_vui_parameters->bitstream_restriction_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (p_vui_parameters->bitstream_restriction_flag) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_vui_parameters->motion_vectors_over_pic_boundaries_flag =
      (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->max_bytes_per_pic_denom);
    if (tmp != HANTRO_OK)
      return(tmp);
#ifdef HANTRO_PEDANTIC_MODE
    if (p_vui_parameters->max_bytes_per_pic_denom > 16)
      return(HANTRO_NOK);
#endif /* HANTRO_PEDANTIC_MODE */

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->max_bits_per_mb_denom);
    if (tmp != HANTRO_OK)
      return(tmp);
#ifdef HANTRO_PEDANTIC_MODE
    if (p_vui_parameters->max_bits_per_mb_denom > 16)
      return(HANTRO_NOK);
#endif /* HANTRO_PEDANTIC_MODE */

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->log2_max_mv_length_horizontal);
    if (tmp != HANTRO_OK)
      return(tmp);
#ifdef HANTRO_PEDANTIC_MODE
    if (p_vui_parameters->log2_max_mv_length_horizontal > 16)
      return(HANTRO_NOK);
#endif /* HANTRO_PEDANTIC_MODE */

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->log2_max_mv_length_vertical);
    if (tmp != HANTRO_OK)
      return(tmp);
#ifdef HANTRO_PEDANTIC_MODE
    if (p_vui_parameters->log2_max_mv_length_vertical > 16)
      return(HANTRO_NOK);
#endif /* HANTRO_PEDANTIC_MODE */

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->num_reorder_frames);
    if (tmp != HANTRO_OK)
      return(tmp);

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_vui_parameters->max_dec_frame_buffering);
    if (tmp != HANTRO_OK)
      return(tmp);
  } else {
    p_vui_parameters->motion_vectors_over_pic_boundaries_flag = HANTRO_TRUE;
    p_vui_parameters->max_bytes_per_pic_denom       = 2;
    p_vui_parameters->max_bits_per_mb_denom         = 1;
    p_vui_parameters->log2_max_mv_length_horizontal = 16;
    p_vui_parameters->log2_max_mv_length_vertical   = 16;
    p_vui_parameters->num_reorder_frames          = MAX_DPB_SIZE;
    p_vui_parameters->max_dec_frame_buffering      = MAX_DPB_SIZE;
  }

  return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdDecodeHrdParameters

        Functional description:
            Decode HRD parameters from the stream. See standard for details.

        Inputs:
            p_strm_data       pointer to stream data structure

        Outputs:
            p_hrd_parameters  decoded information is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdDecodeHrdParameters(
  strmData_t *p_strm_data,
  hrdParameters_t *p_hrd_parameters) {

  /* Variables */

  u32 tmp, i;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_hrd_parameters);


  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &p_hrd_parameters->cpb_cnt);
  if (tmp != HANTRO_OK)
    return(tmp);
  /* cpbCount = cpb_cnt_minus1 + 1 */
  p_hrd_parameters->cpb_cnt++;
  if (p_hrd_parameters->cpb_cnt > MAX_CPB_CNT)
    return(HANTRO_NOK);

  tmp = h264bsdGetBits(p_strm_data, 4);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_hrd_parameters->bit_rate_scale = tmp;

  tmp = h264bsdGetBits(p_strm_data, 4);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_hrd_parameters->cpb_size_scale = tmp;

  for (i = 0; i < p_hrd_parameters->cpb_cnt; i++) {
    /* bit_rate_value_minus1 in the range [0, 2^32 - 2] */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_hrd_parameters->bit_rate_value[i]);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (p_hrd_parameters->bit_rate_value[i] > 4294967294U)
      return(HANTRO_NOK);
    p_hrd_parameters->bit_rate_value[i]++;
    /* this may result in overflow, but this value is not used for
     * anything */
    /* cpb_size_value_minus1 in the range [0, 2^32 - 2] */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_hrd_parameters->cpb_size_value[i]);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (p_hrd_parameters->cpb_size_value[i] > 4294967294U)
      return(HANTRO_NOK);
    p_hrd_parameters->cpb_size_value[i]++;
    /* this may result in overflow, but this value is not used for
     * anything */
    p_hrd_parameters->cpb_size_value[i] *=
      1 << (4 + p_hrd_parameters->cpb_size_scale);

    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(END_OF_STREAM);
    p_hrd_parameters->cbr_flag[i] = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;
  }

  tmp = h264bsdGetBits(p_strm_data, 5);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_hrd_parameters->initial_cpb_removal_delay_length = tmp + 1;

  tmp = h264bsdGetBits(p_strm_data, 5);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_hrd_parameters->cpb_removal_delay_length = tmp + 1;

  tmp = h264bsdGetBits(p_strm_data, 5);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_hrd_parameters->dpb_output_delay_length = tmp + 1;

  tmp = h264bsdGetBits(p_strm_data, 5);
  if (tmp == END_OF_STREAM)
    return(END_OF_STREAM);
  p_hrd_parameters->time_offset_length = tmp;

  return(HANTRO_OK);

}
