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

#include "h264hwd_seq_param_set.h"
#include "h264hwd_util.h"
#include "h264hwd_vlc.h"
#include "h264hwd_vui.h"
#include "h264hwd_cfg.h"
#include "dwl.h"
#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* enumeration to indicate invalid return value from the GetDpbSize function */
enum {INVALID_DPB_SIZE = 0x7FFFFFFF};

const u32 default4x4_intra[16] =
{6,13,13,20,20,20,28,28,28,28,32,32,32,37,37,42};
const u32 default4x4_inter[16] =
{10,14,14,20,20,20,24,24,24,24,27,27,27,30,30,34};
const u32 default8x8_intra[64] = {
  6,10,10,13,11,13,16,16,16,16,18,18,18,18,18,23,
  23,23,23,23,23,25,25,25,25,25,25,25,27,27,27,27,
  27,27,27,27,29,29,29,29,29,29,29,31,31,31,31,31,
  31,33,33,33,33,33,36,36,36,36,38,38,38,40,40,42
};
const u32 default8x8_inter[64] = {
  9,13,13,15,13,15,17,17,17,17,19,19,19,19,19,21,
  21,21,21,21,21,22,22,22,22,22,22,22,24,24,24,24,
  24,24,24,24,25,25,25,25,25,25,25,27,27,27,27,27,
  27,28,28,28,28,28,30,30,30,30,32,32,32,33,33,35
};

static const u32 zig_zag4x4[16] = {
  0,  1,  4,  8, 5,  2,  3,  6, 9, 12, 13, 10, 7, 11, 14, 15
};

static const u32 zig_zag8x8[64] = {
  0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};


/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 GetDpbSize(u32 pic_size_in_mbs, u32 level_idc);
static u32 DecodeMvcExtension(strmData_t *p_strm_data,
                              seqParamSet_t *p_seq_param_set);

/*------------------------------------------------------------------------------

    Function name: h264bsdDecodeSeqParamSet

        Functional description:
            Decode sequence parameter set information from the stream.

            Function allocates memory for offset_for_ref_frame array if
            picture order count type is 1 and num_ref_frames_in_pic_order_cnt_cycle
            is greater than zero.

        Inputs:
            p_strm_data       pointer to stream data structure

        Outputs:
            p_seq_param_set    decoded information is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      failure, invalid information or end of stream
            MEMORY_ALLOCATION_ERROR for memory allocation failure

------------------------------------------------------------------------------*/

void ScalingList(u8 scaling_list[][64], strmData_t *p_strm_data, u32 idx) {

  u32 last_scale = 8, next_scale = 8;
  u32 i, size;
  u32 use_default = 0;
  i32 delta;
  const u32 *def_list[8] = {
    default4x4_intra, default4x4_intra, default4x4_intra,
    default4x4_inter, default4x4_inter, default4x4_inter,
    default8x8_intra, default8x8_inter
  };

  const u32 *zig_zag;

  size = idx < 6 ? 16 : 64;

  zig_zag = idx < 6 ? zig_zag4x4 : zig_zag8x8;

  for (i = 0; i < size; i++) {
    if (next_scale) {
      u32 tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &delta);
      (void)tmp; /* TODO: should we check for error here? */

      next_scale = (last_scale + delta + 256)&0xFF;
      if (!i && !next_scale) {
        use_default = 1;
        break;
      }
    }
    scaling_list[idx][zig_zag[i]] = next_scale ? next_scale : last_scale;
    last_scale = scaling_list[idx][zig_zag[i]];
  }

  if (use_default)
    for (i = 0; i < size; i++)
      scaling_list[idx][zig_zag[i]] = def_list[idx][i];

}

void FallbackScaling(u8 scaling_list[][64], u32 idx) {

  u32 i;

  ASSERT(idx < 8);

  switch (idx) {
  case 0:
    for (i = 0; i < 16; i++)
      scaling_list[idx][zig_zag4x4[i]] = default4x4_intra[i];
    break;
  case 3:
    for (i = 0; i < 16; i++)
      scaling_list[idx][zig_zag4x4[i]] = default4x4_inter[i];
    break;
  case 6:
    for (i = 0; i < 64; i++)
      scaling_list[idx][zig_zag8x8[i]] = default8x8_intra[i];
    break;
  case 7:
    for (i = 0; i < 64; i++)
      scaling_list[idx][zig_zag8x8[i]] = default8x8_inter[i];
    break;
  default:
    for (i = 0; i < 16; i++)
      scaling_list[idx][i] = scaling_list[idx-1][i];
    break;
  }
}


u32 h264bsdDecodeSeqParamSet(strmData_t *p_strm_data, seqParamSet_t *p_seq_param_set,
                             u32 mvc_flag) {

  /* Variables */

  u32 tmp, i, value;
  u32 invalid_dpb_size;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_seq_param_set);

  (void)DWLmemset(p_seq_param_set, 0, sizeof(seqParamSet_t));

  /* profile_idc */
  tmp = h264bsdGetBits(p_strm_data, 8);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  if (tmp != 66) {
    DEBUG_PRINT(("NOT BASELINE PROFILE %d\n", tmp));
  }
#ifdef ASIC_TRACE_SUPPORT
  if (tmp==66)
    trace_h264_dec_tools.profile_type.baseline = 1;
  if (tmp==77)
    trace_h264_dec_tools.profile_type.main = 1;
  if (tmp == 100)
    trace_h264_dec_tools.profile_type.high = 1;
#endif
  p_seq_param_set->profile_idc = tmp;

  /* constrained_set0_flag */
  tmp = h264bsdGetBits(p_strm_data, 1);
  p_seq_param_set->constrained_set0_flag = tmp;
  /* constrained_set1_flag */
  tmp = h264bsdGetBits(p_strm_data, 1);
  p_seq_param_set->constrained_set1_flag = tmp;
  /* constrained_set2_flag */
  tmp = h264bsdGetBits(p_strm_data, 1);
  p_seq_param_set->constrained_set2_flag = tmp;
  /* constrained_set3_flag */
  tmp = h264bsdGetBits(p_strm_data, 1);
  p_seq_param_set->constrained_set3_flag = tmp;
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);

  /* reserved_zero_4bits, values of these bits shall be ignored */
  tmp = h264bsdGetBits(p_strm_data, 4);
  if (tmp == END_OF_STREAM) {
    ERROR_PRINT("reserved_zero_4bits");
    return(HANTRO_NOK);
  }

  tmp = h264bsdGetBits(p_strm_data, 8);
  if (tmp == END_OF_STREAM) {
    ERROR_PRINT("level_idc");
    return(HANTRO_NOK);
  }
  p_seq_param_set->level_idc = tmp;

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                       &p_seq_param_set->seq_parameter_set_id);
  if (tmp != HANTRO_OK)
    return(tmp);
  if (p_seq_param_set->seq_parameter_set_id >= MAX_NUM_SEQ_PARAM_SETS) {
    ERROR_PRINT("seq_param_set_id");
    return(HANTRO_NOK);
  }

  if( p_seq_param_set->profile_idc >= 100 ) {
    /* chroma_format_idc */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    if( tmp > 1 )
      return(HANTRO_NOK);
    p_seq_param_set->chroma_format_idc = value;
    if (p_seq_param_set->chroma_format_idc == 0)
      p_seq_param_set->mono_chrome = 1;
    /* residual_colour_transform_flag (skipped) */

    /* bit_depth_luma_minus8 */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &value);
    if (tmp != HANTRO_OK)
      return(tmp);

    /* bit_depth_chroma_minus8 */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &value);
    if (tmp != HANTRO_OK)
      return(tmp);

    /* qpprime_y_zero_transform_bypass_flag */
    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(HANTRO_NOK);

    /* seq_scaling_matrix_present_flag */
    tmp = h264bsdGetBits(p_strm_data, 1);
#ifdef ASIC_TRACE_SUPPORT
    if (tmp)
      trace_h264_dec_tools.scaling_matrix_present_type.seq = 1;
#endif
    if (tmp == END_OF_STREAM)
      return(HANTRO_NOK);
    p_seq_param_set->scaling_matrix_present_flag = tmp;
    if (tmp) {
      for (i = 0; i < 8; i++) {
        tmp = h264bsdGetBits(p_strm_data, 1);
        p_seq_param_set->scaling_list_present[i] = tmp;
        if (tmp) {
          ScalingList(p_seq_param_set->scaling_list, p_strm_data, i);
        } else
          FallbackScaling(p_seq_param_set->scaling_list, i);
      }
    }

  } else {
    p_seq_param_set->chroma_format_idc = 1; /* 4:2:0 */
    p_seq_param_set->scaling_matrix_present_flag = 0;
  }

  /* log2_max_frame_num_minus4 */
  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK) {
    ERROR_PRINT("log2_max_frame_num_minus4");
    return(tmp);
  }
  if (value > 12) {
    ERROR_PRINT("log2_max_frame_num_minus4");
    return(HANTRO_NOK);
  }
  /* max_frame_num = 2^(log2_max_frame_num_minus4 + 4) */
  p_seq_param_set->max_frame_num = 1 << (value+4);

  /* valid POC types are 0, 1 and 2 */
  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK) {
    ERROR_PRINT("pic_order_cnt_type");
    return(tmp);
  }
  if (value > 2) {
    ERROR_PRINT("pic_order_cnt_type");
    return(HANTRO_NOK);
  }
  p_seq_param_set->pic_order_cnt_type = value;

  if (p_seq_param_set->pic_order_cnt_type == 0) {
    /* log2_max_pic_order_cnt_lsb_minus4 */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (value > 12) {
      ERROR_PRINT("log2_max_pic_order_cnt_lsb_minus4");
      return(HANTRO_NOK);
    }
    /* max_pic_order_cnt_lsb = 2^(log2_max_pic_order_cnt_lsb_minus4 + 4) */
    p_seq_param_set->max_pic_order_cnt_lsb = 1 << (value+4);
  } else if (p_seq_param_set->pic_order_cnt_type == 1) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(HANTRO_NOK);
    p_seq_param_set->delta_pic_order_always_zero_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

    tmp = h264bsdDecodeExpGolombSigned(p_strm_data,
                                       &p_seq_param_set->offset_for_non_ref_pic);
    if (tmp != HANTRO_OK)
      return(tmp);

    tmp = h264bsdDecodeExpGolombSigned(p_strm_data,
                                       &p_seq_param_set->offset_for_top_to_bottom_field);
    if (tmp != HANTRO_OK)
      return(tmp);

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_seq_param_set->num_ref_frames_in_pic_order_cnt_cycle);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (p_seq_param_set->num_ref_frames_in_pic_order_cnt_cycle > 255) {
      ERROR_PRINT("num_ref_frames_in_pic_order_cnt_cycle");
      return(HANTRO_NOK);
    }

    if (p_seq_param_set->num_ref_frames_in_pic_order_cnt_cycle) {
      /* NOTE: This has to be freed somewhere! */
      ALLOCATE(p_seq_param_set->offset_for_ref_frame,
               p_seq_param_set->num_ref_frames_in_pic_order_cnt_cycle, i32);
      if (p_seq_param_set->offset_for_ref_frame == NULL)
        return(MEMORY_ALLOCATION_ERROR);

      for (i = 0; i < p_seq_param_set->num_ref_frames_in_pic_order_cnt_cycle; i++) {
        tmp =  h264bsdDecodeExpGolombSigned(p_strm_data,
                                            p_seq_param_set->offset_for_ref_frame + i);
        if (tmp != HANTRO_OK)
          return(tmp);
      }
    } else {
      p_seq_param_set->offset_for_ref_frame = NULL;
    }
  }

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                       &p_seq_param_set->num_ref_frames);
  if (tmp != HANTRO_OK)
    return(tmp);
  if (p_seq_param_set->num_ref_frames > MAX_NUM_REF_PICS ||
      /* max num ref frames in mvc stereo high profile is actually 8, but
       * here we just check that it is less than 15 (base 15 used for
       * inter view reference picture) */
      (mvc_flag && (p_seq_param_set->num_ref_frames > 15))) {
    ERROR_PRINT("num_ref_frames");
    return(HANTRO_NOK);
  }

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_seq_param_set->gaps_in_frame_num_value_allowed_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK)
    return(tmp);
  p_seq_param_set->pic_width_in_mbs = value + 1;

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK)
    return(tmp);
  p_seq_param_set->pic_height_in_mbs = value + 1;

  /* frame_mbs_only_flag, shall be 1 for baseline profile */
  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_seq_param_set->frame_mbs_only_flag = tmp;

  if (!p_seq_param_set->frame_mbs_only_flag) {
    p_seq_param_set->mb_adaptive_frame_field_flag =
      h264bsdGetBits(p_strm_data, 1);
    p_seq_param_set->pic_height_in_mbs *= 2;
  }

  /* direct_8x8_inference_flag */
  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_seq_param_set->direct8x8_inference_flag = tmp;

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_seq_param_set->frame_cropping_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

#ifdef ASIC_TRACE_SUPPORT
  if (tmp)
    trace_h264_dec_tools.image_cropping = 1;
#endif

  if (p_seq_param_set->frame_cropping_flag) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_seq_param_set->frame_crop_left_offset);
    if (tmp != HANTRO_OK)
      return(tmp);
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_seq_param_set->frame_crop_right_offset);
    if (tmp != HANTRO_OK)
      return(tmp);
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_seq_param_set->frame_crop_top_offset);
    if (tmp != HANTRO_OK)
      return(tmp);
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_seq_param_set->frame_crop_bottom_offset);
    if (tmp != HANTRO_OK)
      return(tmp);

    /* check that frame cropping params are valid, parameters shall
     * specify non-negative area within the original picture */
    if ( ( (i32)p_seq_param_set->frame_crop_left_offset >
           ( 8 * (i32)p_seq_param_set->pic_width_in_mbs -
             ((i32)p_seq_param_set->frame_crop_right_offset + 1) ) ) ||
         ( (i32)p_seq_param_set->frame_crop_top_offset >
           ( 8 * (i32)p_seq_param_set->pic_height_in_mbs -
             ((i32)p_seq_param_set->frame_crop_bottom_offset + 1) ) ) ) {
      ERROR_PRINT("frame_cropping");
      return(HANTRO_NOK);
    }
  }

  /* check that image dimensions and level_idc match */
  tmp = p_seq_param_set->pic_width_in_mbs * p_seq_param_set->pic_height_in_mbs;
  value = GetDpbSize(tmp, p_seq_param_set->level_idc);
  if (value == INVALID_DPB_SIZE || p_seq_param_set->num_ref_frames > value) {
    DEBUG_PRINT(("WARNING! Invalid DPB size based on SPS Level!\n"));
    DEBUG_PRINT(("WARNING! Using num_ref_frames =%d for DPB size!\n",
                 p_seq_param_set->num_ref_frames));
    /* set max_dpb_size to 1 if num_ref_frames is zero */
    value = p_seq_param_set->num_ref_frames ? p_seq_param_set->num_ref_frames : 1;
    invalid_dpb_size = 1;
  }
  p_seq_param_set->max_dpb_size = value;

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_seq_param_set->vui_parameters_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  /* VUI */
  if (p_seq_param_set->vui_parameters_present_flag) {
    ALLOCATE(p_seq_param_set->vui_parameters, 1, vuiParameters_t);
    if (p_seq_param_set->vui_parameters == NULL)
      return(MEMORY_ALLOCATION_ERROR);
    tmp = h264bsdDecodeVuiParameters(p_strm_data,
                                     p_seq_param_set->vui_parameters);
    if (tmp == HANTRO_OK) {
      p_seq_param_set->vui_parameters->update_hrdparameter_flag = 1;
      p_seq_param_set->vui_parameters->error_hrdparameter_flag = 0;
    } else {
      p_seq_param_set->vui_parameters->update_hrdparameter_flag = 0;
      p_seq_param_set->vui_parameters->error_hrdparameter_flag = 1;
    }
    if (tmp == END_OF_STREAM) {
      p_seq_param_set->vui_parameters->bitstream_restriction_flag |= 1;
      p_seq_param_set->vui_parameters->max_dec_frame_buffering =
        p_seq_param_set->max_dpb_size;
    } else if (tmp != HANTRO_OK)
      return(tmp);
    /* check num_reorder_frames and max_dec_frame_buffering */
    if (p_seq_param_set->vui_parameters->bitstream_restriction_flag) {
      if (p_seq_param_set->vui_parameters->num_reorder_frames >
          p_seq_param_set->vui_parameters->max_dec_frame_buffering ||
          p_seq_param_set->vui_parameters->max_dec_frame_buffering <
          p_seq_param_set->num_ref_frames ||
          p_seq_param_set->vui_parameters->max_dec_frame_buffering >
          p_seq_param_set->max_dpb_size) {
        /* Set pSeqParamSet->maxDpbSize to a valid value */
        if (p_seq_param_set->vui_parameters->max_dec_frame_buffering >
            p_seq_param_set->max_dpb_size && invalid_dpb_size)
            p_seq_param_set->max_dpb_size =
          p_seq_param_set->vui_parameters->max_dec_frame_buffering;
        else {
          ERROR_PRINT("Not valid vui_parameters->bitstream_restriction");
          return(HANTRO_NOK);
        }
      }

      /* standard says that "the sequence shall not require a DPB with
       * size of more than max(1, max_dec_frame_buffering) */
      p_seq_param_set->max_dpb_size =
        MAX(1, p_seq_param_set->vui_parameters->max_dec_frame_buffering);
    }
  }

  if (mvc_flag) {
    if (p_seq_param_set->profile_idc == 118 || p_seq_param_set->profile_idc == 128) {
      /* bit_equal_to_one */
      tmp = h264bsdGetBits(p_strm_data, 1);
      tmp = DecodeMvcExtension(p_strm_data, p_seq_param_set);
      if (tmp != HANTRO_OK)
        return tmp;
    }

    /* additional_extension2_flag, shall be zero */
    tmp = h264bsdGetBits(p_strm_data, 1);
    /* TODO: skip rest of the stuff if equal to 1 */
  }

  tmp = h264bsdRbspTrailingBits(p_strm_data);

  /* ignore possible errors in trailing bits of parameters sets */
  return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: GetDpbSize

        Functional description:
            Get size of the DPB in frames. Size is determined based on the
            picture size and MaxDPB for the specified level. These determine
            how many pictures may fit into to the buffer. However, the size
            is also limited to a maximum of 16 frames and therefore function
            returns the minimum of the determined size and 16.

        Inputs:
            pic_size_in_mbs    number of macroblocks in the picture
            level_idc        indicates the level

        Outputs:
            none

        Returns:
            size of the DPB in frames
            INVALID_DPB_SIZE when invalid level_idc specified or pic_size_in_mbs
            is higher than supported by the level in question

------------------------------------------------------------------------------*/

u32 GetDpbSize(u32 pic_size_in_mbs, u32 level_idc) {

  /* Variables */

  u32 tmp;
  u32 max_pic_size_in_mbs;

  /* Code */

  ASSERT(pic_size_in_mbs);

  /* use tmp as the size of the DPB in bytes, computes as 1024 * MaxDPB
   * (from table A-1 in Annex A) */
  switch (level_idc) {
  case 10:
    tmp = 152064;
    max_pic_size_in_mbs = 99;
    break;

  case 11:
    tmp = 345600;
    max_pic_size_in_mbs = 396;
    break;

  case 12:
    tmp = 912384;
    max_pic_size_in_mbs = 396;
    break;

  case 13:
    tmp = 912384;
    max_pic_size_in_mbs = 396;
    break;

  case 20:
    tmp = 912384;
    max_pic_size_in_mbs = 396;
    break;

  case 21:
    tmp = 1824768;
    max_pic_size_in_mbs = 792;
    break;

  case 22:
    tmp = 3110400;
    max_pic_size_in_mbs = 1620;
    break;

  case 30:
    tmp = 3110400;
    max_pic_size_in_mbs = 1620;
    break;

  case 31:
    tmp = 6912000;
    max_pic_size_in_mbs = 3600;
    break;

  case 32:
    tmp = 7864320;
    max_pic_size_in_mbs = 5120;
    break;

  case 40:
    tmp = 12582912;
    max_pic_size_in_mbs = 8192;
    break;

  case 41:
    tmp = 12582912;
    max_pic_size_in_mbs = 8192;
    break;

  case 42:
    tmp = 12582912;
    max_pic_size_in_mbs = 8192;
    break;

  case 50:
    /* standard says 42301440 here, but corrigendum "corrects" this to
     * 42393600 */
    tmp = 42393600;
    max_pic_size_in_mbs = 22080;
    break;

  case 51:
    tmp = 70778880;
    max_pic_size_in_mbs = 36864;
    break;

  default:
    return(INVALID_DPB_SIZE);
  }

  /* this is not "correct" return value! However, it results in error in
   * decoding and this was easiest place to check picture size */
  if (pic_size_in_mbs > max_pic_size_in_mbs)
    return(INVALID_DPB_SIZE);

  tmp /= (pic_size_in_mbs*384);

  return(MIN(tmp, 16));

}

/*------------------------------------------------------------------------------

    Function name: h264bsdCompareSeqParamSets

        Functional description:
            Compare two sequence parameter sets.

        Inputs:
            p_sps1   pointer to a sequence parameter set
            p_sps2   pointer to another sequence parameter set

        Outputs:
            0       sequence parameter sets are equal
            1       otherwise

------------------------------------------------------------------------------*/

u32 h264bsdCompareSeqParamSets(seqParamSet_t *p_sps1, seqParamSet_t *p_sps2) {

  /* Variables */

  u32 i;
  vuiParameters_t *p_vui1, *p_vui2;
  hrdParameters_t *p_hrd1, *p_hrd2;

  /* Code */

  ASSERT(p_sps1);
  ASSERT(p_sps2);

  /* first compare parameters whose existence does not depend on other
   * parameters and only compare the rest of the params if these are equal */
  if (p_sps1->profile_idc        == p_sps2->profile_idc &&
      p_sps1->level_idc          == p_sps2->level_idc &&
      p_sps1->max_frame_num       == p_sps2->max_frame_num &&
      p_sps1->pic_order_cnt_type   == p_sps2->pic_order_cnt_type &&
      p_sps1->num_ref_frames      == p_sps2->num_ref_frames &&
      p_sps1->gaps_in_frame_num_value_allowed_flag ==
      p_sps2->gaps_in_frame_num_value_allowed_flag &&
      p_sps1->pic_width_in_mbs     == p_sps2->pic_width_in_mbs &&
      p_sps1->pic_height_in_mbs    == p_sps2->pic_height_in_mbs &&
      p_sps1->frame_cropping_flag == p_sps2->frame_cropping_flag &&
      p_sps1->frame_mbs_only_flag  == p_sps2->frame_mbs_only_flag &&
      p_sps1->vui_parameters_present_flag == p_sps2->vui_parameters_present_flag &&
      p_sps1->scaling_matrix_present_flag == p_sps2->scaling_matrix_present_flag) {

    if (p_sps1->pic_order_cnt_type == 0) {
      if (p_sps1->max_pic_order_cnt_lsb != p_sps2->max_pic_order_cnt_lsb)
        return 1;
    } else if (p_sps1->pic_order_cnt_type == 1) {
      if (p_sps1->delta_pic_order_always_zero_flag !=
          p_sps2->delta_pic_order_always_zero_flag ||
          p_sps1->offset_for_non_ref_pic != p_sps2->offset_for_non_ref_pic ||
          p_sps1->offset_for_top_to_bottom_field !=
          p_sps2->offset_for_top_to_bottom_field ||
          p_sps1->num_ref_frames_in_pic_order_cnt_cycle !=
          p_sps2->num_ref_frames_in_pic_order_cnt_cycle) {
        return 1;
      } else {
        for (i = 0; i < p_sps1->num_ref_frames_in_pic_order_cnt_cycle; i++)
          if (p_sps1->offset_for_ref_frame[i] !=
              p_sps2->offset_for_ref_frame[i]) {
            return 1;
          }
      }
    }
    if (p_sps1->frame_cropping_flag) {
      if (p_sps1->frame_crop_left_offset   != p_sps2->frame_crop_left_offset ||
          p_sps1->frame_crop_right_offset  != p_sps2->frame_crop_right_offset ||
          p_sps1->frame_crop_top_offset    != p_sps2->frame_crop_top_offset ||
          p_sps1->frame_crop_bottom_offset != p_sps2->frame_crop_bottom_offset) {
        return 1;
      }
    }

    if (!p_sps1->frame_mbs_only_flag)
      if (p_sps1->mb_adaptive_frame_field_flag !=
          p_sps2->mb_adaptive_frame_field_flag)
        return 1;

    /* copy scaling matrices if used */
    if (p_sps1->scaling_matrix_present_flag) {
      u32 i, j;

      for (i = 0; i < 8; i++) {
        p_sps2->scaling_list_present[i] = p_sps1->scaling_list_present[i];
        for (j = 0; j < 64; j++)
          p_sps2->scaling_list[i][j] = p_sps1->scaling_list[i][j];
      }
    }

    if (p_sps1->vui_parameters_present_flag) {
      p_vui1 = p_sps1->vui_parameters;
      p_vui2 = p_sps2->vui_parameters;

      if (p_vui1->aspect_ratio_present_flag       != p_vui2->aspect_ratio_present_flag ||
          p_vui1->overscan_info_present_flag      != p_vui2->overscan_info_present_flag ||
          p_vui1->video_signal_type_present_flag  != p_vui2->video_signal_type_present_flag ||
          p_vui1->chroma_loc_info_present_flag    != p_vui2->chroma_loc_info_present_flag ||
          p_vui1->timing_info_present_flag        != p_vui2->timing_info_present_flag ||
          p_vui1->nal_hrd_parameters_present_flag != p_vui2->nal_hrd_parameters_present_flag ||
          p_vui1->vcl_hrd_parameters_present_flag != p_vui2->vcl_hrd_parameters_present_flag ||
          p_vui1->pic_struct_present_flag         != p_vui2->pic_struct_present_flag ||
          p_vui1->bitstream_restriction_flag      != p_vui2->bitstream_restriction_flag)
        return 1;

      else if (p_vui1->aspect_ratio_present_flag) {
        if (p_vui1->aspect_ratio_idc != p_vui2->aspect_ratio_idc)
          return 1;
        else if (p_vui1->aspect_ratio_idc == ASPECT_RATIO_EXTENDED_SAR) {
          if (p_vui1->sar_width  != p_vui2->sar_width ||
              p_vui1->sar_height != p_vui2->sar_height) {
            return 1;
          }
        }
      }

      else if (p_vui1->overscan_info_present_flag) {
        if (p_vui1->overscan_appropriate_flag != p_vui2->overscan_appropriate_flag)
          return 1;
      }

      else if (p_vui1->video_signal_type_present_flag) {
        if (p_vui1->video_format                    != p_vui2->video_format ||
            p_vui1->video_full_range_flag           != p_vui2->video_full_range_flag ||
            p_vui1->colour_description_present_flag != p_vui2->colour_description_present_flag)
          return 1;
        else if (p_vui1->colour_description_present_flag) {
          if (p_vui1->colour_primaries         != p_vui2->colour_primaries ||
              p_vui1->transfer_characteristics != p_vui2->transfer_characteristics ||
              p_vui1->matrix_coefficients      != p_vui2->matrix_coefficients) {
            return 1;
          }
        }
      }

      else if (p_vui1->chroma_loc_info_present_flag) {
        if (p_vui1->chroma_sample_loc_type_top_field    != p_vui2->chroma_sample_loc_type_top_field ||
            p_vui1->chroma_sample_loc_type_bottom_field != p_vui2->chroma_sample_loc_type_bottom_field)
          return 1;
      }

      else if (p_vui1->timing_info_present_flag) {
        if (p_vui1->num_units_in_tick     != p_vui2->num_units_in_tick ||
            p_vui1->time_scale            != p_vui2->time_scale ||
            p_vui1->fixed_frame_rate_flag != p_vui2->fixed_frame_rate_flag)
          return 1;
      }

      else if (p_vui1->nal_hrd_parameters_present_flag || p_vui1->vcl_hrd_parameters_present_flag) {
        if (p_vui1->nal_hrd_parameters_present_flag) {
          p_hrd1 = &(p_vui1->nal_hrd_parameters);
          p_hrd2 = &(p_vui2->nal_hrd_parameters);
        }
        else {
          p_hrd1 = &(p_vui1->vcl_hrd_parameters);
          p_hrd2 = &(p_vui2->vcl_hrd_parameters);
        }

        if (p_hrd1->cpb_cnt                          != p_hrd2->cpb_cnt ||
            p_hrd1->bit_rate_scale                   != p_hrd2->bit_rate_scale ||
            p_hrd1->cpb_size_scale                   != p_hrd2->cpb_size_scale ||
            p_hrd1->initial_cpb_removal_delay_length != p_hrd2->initial_cpb_removal_delay_length ||
            p_hrd1->cpb_removal_delay_length         != p_hrd2->cpb_removal_delay_length ||
            p_hrd1->dpb_output_delay_length          != p_hrd2->dpb_output_delay_length ||
            p_hrd1->time_offset_length               != p_hrd2->time_offset_length)
          return 1;
        else if (p_hrd1->cpb_cnt) {
          for (u32 i = 0; i < p_hrd1->cpb_cnt; i++) {
            if (p_hrd1->bit_rate_value[i] != p_hrd2->bit_rate_value[i] ||
                p_hrd1->cpb_size_value[i] != p_hrd2->cpb_size_value[i] ||
                p_hrd1->cbr_flag[i]       != p_hrd2->cbr_flag[i])
              return 1;
          }
        }

        if (p_vui1->low_delay_hrd_flag != p_vui2->low_delay_hrd_flag)
          return 1;
      }

      else if (p_vui1->bitstream_restriction_flag)
      {
        if (p_vui1->motion_vectors_over_pic_boundaries_flag != p_vui2->motion_vectors_over_pic_boundaries_flag ||
            p_vui1->max_bytes_per_pic_denom                 != p_vui2->max_bytes_per_pic_denom ||
            p_vui1->max_bits_per_mb_denom                   != p_vui2->max_bits_per_mb_denom ||
            p_vui1->log2_max_mv_length_horizontal           != p_vui2->log2_max_mv_length_horizontal ||
            p_vui1->log2_max_mv_length_vertical             != p_vui2->log2_max_mv_length_vertical ||
            p_vui1->num_reorder_frames                      != p_vui2->num_reorder_frames ||
            p_vui1->max_dec_frame_buffering                 != p_vui2->max_dec_frame_buffering)
          return 1;
      }
    }

    return 0;
  }

  return 1;
}

u32 DecodeMvcExtension(strmData_t *p_strm_data, seqParamSet_t *p_seq_param_set) {

  /* Variables */

  u32 tmp, i, j, k;
  u32 value, tmp_count, tmp_count1, tmp_count2;
  hrdParameters_t hrd_params;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_seq_param_set);

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK)
    return(tmp);
  p_seq_param_set->mvc.num_views = value + 1;

  if (p_seq_param_set->mvc.num_views > MAX_NUM_VIEWS)
    return(HANTRO_NOK);

  /* view_id */
  for (i = 0; i < p_seq_param_set->mvc.num_views; i++) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (i < MAX_NUM_VIEWS)
      p_seq_param_set->mvc.view_id[i] = value;
  }

  for (i = 1; i < p_seq_param_set->mvc.num_views; i++) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    /*p_seq_param_set->mvc.numAnchorRefsL0[i] = value;*/
    tmp_count = value;
    for (j = 0; j < tmp_count; j++) {
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      /*p_seq_param_set->mvc.anchorRefL0[i][j] = value;*/
    }

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    /*p_seq_param_set->mvc.numNonAnchorRefsL0[i] = value;*/
    tmp_count = value;
    for (j = 0; j < tmp_count; j++) {
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      /*p_seq_param_set->mvc.nonAnchorRefL0[i][j] = value;*/
    }
  }

  for (i = 1; i < p_seq_param_set->mvc.num_views; i++) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    /*p_seq_param_set->mvc.numAnchorRefsL1[i] = value;*/
    tmp_count = value;
    for (j = 0; j < tmp_count; j++) {
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      /*p_seq_param_set->mvc.anchorRefL1[i][j] = value;*/
    }

    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    /*p_seq_param_set->mvc.numNonAnchorRefsL1[i] = value;*/
    tmp_count = value;
    for (j = 0; j < tmp_count; j++) {
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      /*p_seq_param_set->mvc.nonAnchorRefL1[i][j] = value;*/
    }
  }

  /* num_level_values_signalled_minus1 */
  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK)
    return(tmp);
  tmp_count = value + 1;
  for (i = 0; i < tmp_count; i++) {
    /* level_idc */
    tmp = h264bsdGetBits(p_strm_data, 8);
    /* num_applicable_ops_minus1 */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    tmp_count1 = value + 1;
    for (j = 0; j < tmp_count1; j++) {
      /* applicable_op_temporal_id  */
      tmp = h264bsdGetBits(p_strm_data, 3);
      /* applicable_op_num_target_views_minus1 */
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      tmp_count2 = value + 1;
      for (k = 0; k < tmp_count2; k++) {
        /* applicable_op_target_view_id */
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      }
      /* applicable_op_num_views_minus1 */
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
    }
  }

  /* mvc_vui_parameters_present_flag */
  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == 1) {
    /* vui_mvc_num_ops_minus1 */
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if (tmp != HANTRO_OK)
      return(tmp);
    tmp_count = value + 1;
    for (i = 0; i < tmp_count; i++) {
      /* vui_mvc_temporal_id  */
      tmp = h264bsdGetBits(p_strm_data, 3);
      /* vui_mvc_num_target_output_views_minus1 */
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      tmp_count1 = value + 1;
      for (k = 0; k < tmp_count1; k++) {
        /* vui_mvc_view_id */
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      }
      /* vui_mvc_timing_info_present_flag  */
      tmp = h264bsdGetBits(p_strm_data, 1);
      if (tmp == 1) {
        /* vui_mvc_num_units_in_tick  */
        tmp = h264bsdShowBits(p_strm_data,32);
        if (h264bsdFlushBits(p_strm_data, 32) == END_OF_STREAM)
          return(END_OF_STREAM);
        /* vui_mvc_time_scale  */
        tmp = h264bsdShowBits(p_strm_data,32);
        if (h264bsdFlushBits(p_strm_data, 32) == END_OF_STREAM)
          return(END_OF_STREAM);
        /* vui_mvc_fixed_frame_rate_flag */
        tmp = h264bsdGetBits(p_strm_data, 1);
      }

      j = 0;
      /* vui_mvc_nal_hrd_parameters_present_flag */
      tmp = h264bsdGetBits(p_strm_data, 1);
      if (tmp == 1) {
        j = 1;
        tmp = h264bsdDecodeHrdParameters(p_strm_data, &hrd_params);
      }

      /* vui_mvc_vcl_hrd_parameters_present_flag */
      tmp = h264bsdGetBits(p_strm_data, 1);
      if (tmp == 1) {
        j = 1;
        tmp = h264bsdDecodeHrdParameters(p_strm_data, &hrd_params);
      }

      if (j) {
        /* vui_mvc_low_delay_hrd_flag */
        tmp = h264bsdGetBits(p_strm_data, 1);
      }
      /* vui_mvc_pic_struct_present_flag */
      tmp = h264bsdGetBits(p_strm_data, 1);
    }

  }

  return(HANTRO_OK);

}
