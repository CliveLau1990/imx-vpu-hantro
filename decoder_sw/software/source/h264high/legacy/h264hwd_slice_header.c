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

#include "h264hwd_slice_header.h"
#include "h264hwd_util.h"
#include "h264hwd_vlc.h"
#include "h264hwd_nal_unit.h"
#include "h264hwd_dpb.h"
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

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 RefPicListReordering(strmData_t *, refPicListReordering_t *,
                                u32, u32, u32);

static u32 PredWeightTable(strmData_t *, sliceHeader_t *, u32 mono_chrome);

static u32 NumSliceGroupChangeCycleBits(u32 pic_size_in_mbs,
                                        u32 slice_group_change_rate);

static u32 DecRefPicMarking(strmData_t * p_strm_data,
                            decRefPicMarking_t * p_dec_ref_pic_marking,
                            u32 idr_pic_flag, u32 num_ref_frames);

/*------------------------------------------------------------------------------

    Function name: h264bsdDecodeSliceHeader

        Functional description:
            Decode slice header data from the stream.

        Inputs:
            p_strm_data       pointer to stream data structure
            p_seq_param_set    pointer to active sequence parameter set
            p_pic_param_set    pointer to active picture parameter set
            p_nal_unit        pointer to current NAL unit structure

        Outputs:
            p_slice_header    decoded data is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data or end of stream

------------------------------------------------------------------------------*/

u32 h264bsdDecodeSliceHeader(strmData_t * p_strm_data,
                             sliceHeader_t * p_slice_header,
                             seqParamSet_t * p_seq_param_set,
                             picParamSet_t * p_pic_param_set, nalUnit_t * p_nal_unit) {

  /* Variables */

  u32 tmp, i, value;
  i32 itmp;
  u32 pic_size_in_mbs;
  u32 strm_pos;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_slice_header);
  ASSERT(p_seq_param_set);
  ASSERT(p_pic_param_set);
  ASSERT(p_nal_unit->nal_unit_type == NAL_CODED_SLICE ||
         p_nal_unit->nal_unit_type == NAL_CODED_SLICE_IDR ||
         p_nal_unit->nal_unit_type == NAL_CODED_SLICE_EXT);

  (void) DWLmemset(p_slice_header, 0, sizeof(sliceHeader_t));

  pic_size_in_mbs = p_seq_param_set->pic_width_in_mbs * p_seq_param_set->pic_height_in_mbs;
  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);
  p_slice_header->first_mb_in_slice = value;
  if(value >= pic_size_in_mbs) {
    ERROR_PRINT("first_mb_in_slice");
    return (HANTRO_NOK);
  }

  DEBUG_PRINT(("\tfirst_mb_in_slice %4d\n", value));

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);
  p_slice_header->slice_type = value;
  /* slice type has to be either I, P or B slice. Only I slice is
   * allowed when current NAL unit is an IDR NAL unit or num_ref_frames
   * is 0 */
  if( ! ( IS_I_SLICE(p_slice_header->slice_type) ||
          ((IS_P_SLICE(p_slice_header->slice_type) ||
            IS_B_SLICE(p_slice_header->slice_type)) &&
           p_nal_unit->nal_unit_type != NAL_CODED_SLICE_IDR &&
           p_seq_param_set->num_ref_frames) ) ) {
    ERROR_PRINT("slice_type");
    return (HANTRO_NOK);
  }

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);
  p_slice_header->pic_parameter_set_id = value;
  if(p_slice_header->pic_parameter_set_id != p_pic_param_set->pic_parameter_set_id) {
    ERROR_PRINT("pic_parameter_set_id");
    return (HANTRO_NOK);
  }

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(p_seq_param_set->max_frame_num >> i)
    i++;
  i--;

  tmp = h264bsdGetBits(p_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);
  /* removed for XXXXXXXX compliance */
  /*
  if(IS_IDR_NAL_UNIT(p_nal_unit) && tmp != 0)
  {
      ERROR_PRINT("frame_num");
      return (HANTRO_NOK);
  }
  */
  p_slice_header->frame_num = tmp;

  if (!p_seq_param_set->frame_mbs_only_flag) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    p_slice_header->field_pic_flag = tmp;
    if (p_slice_header->field_pic_flag) {
      tmp = h264bsdGetBits(p_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
      p_slice_header->bottom_field_flag = tmp;
    }

  }

  if(IS_IDR_NAL_UNIT(p_nal_unit)) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
    p_slice_header->idr_pic_id = value;
    if(value > 65535) {
      ERROR_PRINT("idr_pic_id");
      return (HANTRO_NOK);
    }
  }

  strm_pos = p_strm_data->strm_buff_read_bits;
  p_strm_data->emul_byte_count = 0;
  if(p_seq_param_set->pic_order_cnt_type == 0) {
    /* log2(max_pic_order_cnt_lsb) -> num bits to represent pic_order_cnt_lsb */
    i = 0;
    while(p_seq_param_set->max_pic_order_cnt_lsb >> i)
      i++;
    i--;

    tmp = h264bsdGetBits(p_strm_data, i);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    p_slice_header->pic_order_cnt_lsb = tmp;

    if(p_pic_param_set->pic_order_present_flag && !p_slice_header->field_pic_flag) {
      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      if(tmp != HANTRO_OK)
        return (tmp);
      p_slice_header->delta_pic_order_cnt_bottom = itmp;
    }

    /* check that picOrderCnt for IDR picture will be zero. See
     * DecodePicOrderCnt function to understand the logic here */
    /* removed for XXXXXXXX compliance */
    /*
    if(IS_IDR_NAL_UNIT(p_nal_unit) &&
       ((p_slice_header->pic_order_cnt_lsb >
         p_seq_param_set->max_pic_order_cnt_lsb / 2) ||
        MIN((i32) p_slice_header->pic_order_cnt_lsb,
            (i32) p_slice_header->pic_order_cnt_lsb +
            p_slice_header->delta_pic_order_cnt_bottom) != 0))
    {
        return (HANTRO_NOK);
    }
    */
  }

  if((p_seq_param_set->pic_order_cnt_type == 1) &&
      !p_seq_param_set->delta_pic_order_always_zero_flag) {
    tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
    if(tmp != HANTRO_OK)
      return (tmp);
    p_slice_header->delta_pic_order_cnt[0] = itmp;

    if(p_pic_param_set->pic_order_present_flag && !p_slice_header->field_pic_flag) {
      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      if(tmp != HANTRO_OK)
        return (tmp);
      p_slice_header->delta_pic_order_cnt[1] = itmp;
    }

    /* check that picOrderCnt for IDR picture will be zero. See
     * DecodePicOrderCnt function to understand the logic here */
    if(IS_IDR_NAL_UNIT(p_nal_unit) &&
        (p_slice_header->field_pic_flag ?
         p_slice_header->bottom_field_flag ?
         p_slice_header->delta_pic_order_cnt[0] +
         p_seq_param_set->offset_for_top_to_bottom_field +
         p_slice_header->delta_pic_order_cnt[1] != 0 :
         p_slice_header->delta_pic_order_cnt[0] != 0 :
         MIN(p_slice_header->delta_pic_order_cnt[0],
             p_slice_header->delta_pic_order_cnt[0] +
             p_seq_param_set->offset_for_top_to_bottom_field +
             p_slice_header->delta_pic_order_cnt[1]) != 0)) {
      return (HANTRO_NOK);
    }
  }

  p_slice_header->poc_length = p_strm_data->strm_buff_read_bits - strm_pos;
  p_slice_header->poc_length_hw = p_strm_data->strm_buff_read_bits - strm_pos -
                                  8*p_strm_data->emul_byte_count;
  if(p_pic_param_set->redundant_pic_cnt_present_flag) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
    p_slice_header->redundant_pic_cnt = value;
    if(value > 127) {
      ERROR_PRINT("redundant_pic_cnt");
      return (HANTRO_NOK);
    }

    /* don't decode redundant slices */
    if(value != 0) {
      DEBUG_PRINT(("h264bsdDecodeSliceHeader: REDUNDANT PICTURE SLICE\n"));
      /* return(HANTRO_NOK); */
    }
  }

  if (IS_B_SLICE(p_slice_header->slice_type)) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    p_slice_header->direct_spatial_mv_pred_flag = tmp;
  }
  if (IS_P_SLICE(p_slice_header->slice_type) ||
      IS_B_SLICE(p_slice_header->slice_type)) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    p_slice_header->num_ref_idx_active_override_flag = tmp;

    if(p_slice_header->num_ref_idx_active_override_flag) {
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if(tmp != HANTRO_OK)
        return (tmp);
      if (value > 31 ||
          (p_slice_header->field_pic_flag == 0 && value > 15)) {
        ERROR_PRINT("num_ref_idx_l0_active_minus1");
        return (HANTRO_NOK);
      }
      p_slice_header->num_ref_idx_l0_active = value + 1;

      if (IS_B_SLICE(p_slice_header->slice_type)) {
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
        if(tmp != HANTRO_OK)
          return (tmp);
        if (value > 31 ||
            (p_slice_header->field_pic_flag == 0 && value > 15)) {
          ERROR_PRINT("num_ref_idx_l1_active_minus1");
          return (HANTRO_NOK);
        }
        p_slice_header->num_ref_idx_l1_active = value + 1;
      }
    }
    /* set num_ref_idx_l0_active from pic param set */
    else {
      /* if value (minus1) in picture parameter set exceeds 15 and
       * current picture is not field picture, it should have been
       * overridden here */
      if( (p_pic_param_set->num_ref_idx_l0_active > 16 &&
           !p_slice_header->field_pic_flag) ||
          (IS_B_SLICE(p_slice_header->slice_type) &&
           p_pic_param_set->num_ref_idx_l1_active > 16 &&
           !p_slice_header->field_pic_flag) ) {
        ERROR_PRINT("num_ref_idx_active_override_flag");
        return (HANTRO_NOK);
      }
      p_slice_header->num_ref_idx_l0_active = p_pic_param_set->num_ref_idx_l0_active;
      p_slice_header->num_ref_idx_l1_active = p_pic_param_set->num_ref_idx_l1_active;
    }
  }

  if(!IS_I_SLICE(p_slice_header->slice_type)) {
    tmp = RefPicListReordering(p_strm_data,
                               &p_slice_header->ref_pic_list_reordering,
                               p_slice_header->num_ref_idx_l0_active,
                               p_slice_header->field_pic_flag ?
                               2*p_seq_param_set->max_frame_num :
                               p_seq_param_set->max_frame_num,
                               p_nal_unit->nal_unit_type ==
                               NAL_CODED_SLICE_EXT );
    if(tmp != HANTRO_OK)
      return (tmp);
  }
  if (IS_B_SLICE(p_slice_header->slice_type)) {
    tmp = RefPicListReordering(p_strm_data,
                               &p_slice_header->ref_pic_list_reordering_l1,
                               p_slice_header->num_ref_idx_l1_active,
                               p_slice_header->field_pic_flag ?
                               2*p_seq_param_set->max_frame_num :
                               p_seq_param_set->max_frame_num,
                               p_nal_unit->nal_unit_type ==
                               NAL_CODED_SLICE_EXT );
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  if ( (p_pic_param_set->weighted_pred_flag &&
        IS_P_SLICE(p_slice_header->slice_type)) ||
       (p_pic_param_set->weighted_bi_pred_idc == 1 &&
        IS_B_SLICE(p_slice_header->slice_type)) ) {
    tmp = PredWeightTable(p_strm_data, p_slice_header, p_seq_param_set->mono_chrome);
    if (tmp != HANTRO_OK)
      return (tmp);
  }

  if(p_nal_unit->nal_ref_idc != 0) {
    tmp = DecRefPicMarking(p_strm_data, &p_slice_header->dec_ref_pic_marking,
                           IS_IDR_NAL_UNIT(p_nal_unit),
                           p_seq_param_set->num_ref_frames);
    if(tmp != HANTRO_OK)
      return (tmp);
  } else
    p_slice_header->dec_ref_pic_marking.strm_len = 0;

  if(p_pic_param_set->entropy_coding_mode_flag &&
      !IS_I_SLICE(p_slice_header->slice_type)) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if(value > 2 || tmp != HANTRO_OK) {
      ERROR_PRINT("cabac_init_idc");
      return (HANTRO_NOK);
    }
    p_slice_header->cabac_init_idc = value;
  }

  /* decode slice_qp_delta and check that initial QP for the slice will be on
   * the range [0, 51] */
  tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
  if(tmp != HANTRO_OK)
    return (tmp);
  p_slice_header->slice_qp_delta = itmp;
  itmp += (i32) p_pic_param_set->pic_init_qp;
  if((itmp < 0) || (itmp > 51)) {
    ERROR_PRINT("slice_qp_delta");
    return (HANTRO_NOK);
  }

  if(p_pic_param_set->deblocking_filter_control_present_flag) {
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
    p_slice_header->disable_deblocking_filter_idc = value;
    if(p_slice_header->disable_deblocking_filter_idc > 2) {
      ERROR_PRINT("disable_deblocking_filter_idc");
      return (HANTRO_NOK);
    }

    if(p_slice_header->disable_deblocking_filter_idc != 1) {
      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      if(tmp != HANTRO_OK)
        return (tmp);
      if((itmp < -6) || (itmp > 6)) {
        ERROR_PRINT("slice_alpha_c0_offset_div2");
        return (HANTRO_NOK);
      }
      p_slice_header->slice_alpha_c0_offset = itmp /* * 2 */ ;

      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      if(tmp != HANTRO_OK)
        return (tmp);
      if((itmp < -6) || (itmp > 6)) {
        ERROR_PRINT("slice_beta_offset_div2");
        return (HANTRO_NOK);
      }
      p_slice_header->slice_beta_offset = itmp /* * 2 */ ;
    }
  }

  if((p_pic_param_set->num_slice_groups > 1) &&
      (p_pic_param_set->slice_group_map_type >= 3) &&
      (p_pic_param_set->slice_group_map_type <= 5)) {
    /* set tmp to number of bits used to represent slice_group_change_cycle
     * in the stream */
    tmp = NumSliceGroupChangeCycleBits(pic_size_in_mbs,
                                       p_pic_param_set->slice_group_change_rate);
    value = h264bsdGetBits(p_strm_data, tmp);
    if(value == END_OF_STREAM)
      return (HANTRO_NOK);
    p_slice_header->slice_group_change_cycle = value;

    /* corresponds to tmp = Ceil(pic_size_in_mbs / slice_group_change_rate) */
    tmp = (pic_size_in_mbs + p_pic_param_set->slice_group_change_rate - 1) /
          p_pic_param_set->slice_group_change_rate;
    if(p_slice_header->slice_group_change_cycle > tmp) {
      ERROR_PRINT("slice_group_change_cycle");
      return (HANTRO_NOK);
    }
  }

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: NumSliceGroupChangeCycleBits

        Functional description:
            Determine number of bits needed to represent
            slice_group_change_cycle in the stream. The standard states that
            slice_group_change_cycle is represented by
                Ceil( Log2( (pic_size_in_mbs / slice_group_change_rate) + 1) )

            bits. Division "/" in the equation is non-truncating division.

        Inputs:
            pic_size_in_mbs            picture size in macroblocks
            slice_group_change_rate

        Outputs:
            none

        Returns:
            number of bits needed

------------------------------------------------------------------------------*/

u32 NumSliceGroupChangeCycleBits(u32 pic_size_in_mbs, u32 slice_group_change_rate) {

  /* Variables */

  u32 tmp, num_bits, mask;

  /* Code */

  ASSERT(pic_size_in_mbs);
  ASSERT(slice_group_change_rate);
  ASSERT(slice_group_change_rate <= pic_size_in_mbs);

  /* compute (pic_size_in_mbs / slice_group_change_rate + 1), rounded up */
  if(pic_size_in_mbs % slice_group_change_rate)
    tmp = 2 + pic_size_in_mbs / slice_group_change_rate;
  else
    tmp = 1 + pic_size_in_mbs / slice_group_change_rate;

  num_bits = 0;
  mask = ~0U;

  /* set num_bits to position of right-most non-zero bit */
  while(tmp & (mask << ++num_bits))
    ;
  num_bits--;

  /* add one more bit if value greater than 2^num_bits */
  if(tmp & ((1 << num_bits) - 1))
    num_bits++;

  return (num_bits);

}

/*------------------------------------------------------------------------------

    Function: RefPicListReordering

        Functional description:
            Decode reference picture list reordering syntax elements from
            the stream. Max number of reordering commands is num_ref_idx_active.

        Inputs:
            p_strm_data       pointer to stream data structure
            num_ref_idx_active number of active reference indices to be used for
                            current slice
            max_pic_num       max_frame_num from the active SPS

        Outputs:
            p_ref_pic_list_reordering   decoded data is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 RefPicListReordering(strmData_t * p_strm_data,
                         refPicListReordering_t * p_ref_pic_list_reordering,
                         u32 num_ref_idx_active, u32 max_pic_num, u32 mvc) {

  /* Variables */

  u32 tmp, value, i;
  u32 command;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_ref_pic_list_reordering);
  ASSERT(num_ref_idx_active);
  ASSERT(max_pic_num);

  tmp = h264bsdGetBits(p_strm_data, 1);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  p_ref_pic_list_reordering->ref_pic_list_reordering_flag_l0 = tmp;

  if(p_ref_pic_list_reordering->ref_pic_list_reordering_flag_l0) {
    i = 0;

    do {
      if(i > num_ref_idx_active) {
        ERROR_PRINT("Too many reordering commands");
        return (HANTRO_NOK);
      }

      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &command);
      if(tmp != HANTRO_OK)
        return (tmp);
      if(command > (mvc ? 5 : 3)) {
        ERROR_PRINT("reordering_of_pic_nums_idc");
        return (HANTRO_NOK);
      }

      p_ref_pic_list_reordering->command[i].reordering_of_pic_nums_idc = command;

      if((command == 0) || (command == 1)) {
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
        if(tmp != HANTRO_OK)
          return (tmp);
        if(value >= max_pic_num) {
          ERROR_PRINT("abs_diff_pic_num_minus1");
          return (HANTRO_NOK);
        }
        p_ref_pic_list_reordering->command[i].abs_diff_pic_num = value + 1;
      } else if(command == 2) {
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
        if(tmp != HANTRO_OK)
          return (tmp);
        p_ref_pic_list_reordering->command[i].long_term_pic_num = value;
      } else if(command == 4 || command == 5) {
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
        if(tmp != HANTRO_OK)
          return (tmp);
        p_ref_pic_list_reordering->command[i].abs_diff_view_idx = value + 1;
      }
      i++;
    } while(command != 3);

    /* there shall be at least one reordering command if
     * ref_pic_list_reordering_flag_l0 was set */
    if(i == 1) {
      ERROR_PRINT("ref_pic_list_reordering");
      return (HANTRO_NOK);
    }
  }

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: DecRefPicMarking

        Functional description:
            Decode decoded reference picture marking syntax elements from
            the stream.

        Inputs:
            p_strm_data       pointer to stream data structure
            nal_unit_type     type of the current NAL unit
            num_ref_frames    max number of reference frames from the active SPS

        Outputs:
            p_dec_ref_pic_marking   decoded data is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 DecRefPicMarking(strmData_t * p_strm_data,
                     decRefPicMarking_t * p_dec_ref_pic_marking,
                     u32 idr_pic_flag, u32 num_ref_frames) {

  /* Variables */

  u32 tmp, value;
  u32 i;
  u32 operation;
  u32 strm_pos;

  /* variables for error checking purposes, store number of memory
   * management operations of certain type */
  u32 num4 = 0, num5 = 0, num6 = 0, num1to3 = 0;

  /* Code */

  strm_pos = p_strm_data->strm_buff_read_bits;
  p_strm_data->emul_byte_count = 0;

  if(idr_pic_flag) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    p_dec_ref_pic_marking->no_output_of_prior_pics_flag = tmp;

    tmp = h264bsdGetBits(p_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    p_dec_ref_pic_marking->long_term_reference_flag = tmp;
    if(!num_ref_frames && p_dec_ref_pic_marking->long_term_reference_flag) {
      ERROR_PRINT("long_term_reference_flag");
      return (HANTRO_NOK);
    }
  } else {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    p_dec_ref_pic_marking->adaptive_ref_pic_marking_mode_flag = tmp;
    if(p_dec_ref_pic_marking->adaptive_ref_pic_marking_mode_flag) {
      i = 0;
      do {
        /* see explanation of the MAX_NUM_MMC_OPERATIONS in
         * slice_header.h */
        if(i > (2 * num_ref_frames + 2)) {
          ERROR_PRINT("Too many management operations");
          return (HANTRO_NOK);
        }

        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &operation);
        if(tmp != HANTRO_OK)
          return (tmp);
        if(operation > 6) {
          ERROR_PRINT("memory_management_control_operation");
          return (HANTRO_NOK);
        }

        p_dec_ref_pic_marking->operation[i].
        memory_management_control_operation = operation;
        if((operation == 1) || (operation == 3)) {
          tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
          if(tmp != HANTRO_OK)
            return (tmp);
          p_dec_ref_pic_marking->operation[i].difference_of_pic_nums =
            value + 1;
        }
        if(operation == 2) {
          tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
          if(tmp != HANTRO_OK)
            return (tmp);
          p_dec_ref_pic_marking->operation[i].long_term_pic_num = value;
        }
        if((operation == 3) || (operation == 6)) {
          tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
          if(tmp != HANTRO_OK)
            return (tmp);
          p_dec_ref_pic_marking->operation[i].long_term_frame_idx = value;
        }
        if(operation == 4) {
          tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
          if(tmp != HANTRO_OK)
            return (tmp);
          /* value shall be in range [0, num_ref_frames] */
          if(value > num_ref_frames) {
            ERROR_PRINT("max_long_term_frame_idx_plus1");
            return (HANTRO_NOK);
          }
          if(value == 0) {
            p_dec_ref_pic_marking->operation[i].
            max_long_term_frame_idx = NO_LONG_TERM_FRAME_INDICES;
          } else {
            p_dec_ref_pic_marking->operation[i].
            max_long_term_frame_idx = value - 1;
          }
          num4++;
        }
        if(operation == 5) {
          num5++;
        }
        if(operation && operation <= 3)
          num1to3++;
        if(operation == 6)
          num6++;

        i++;
      } while(operation != 0);

      /* error checking */
      if(num4 > 1 || num5 > 1 || num6 > 1 || (num1to3 && num5))
        return (HANTRO_NOK);

    }
  }

  p_dec_ref_pic_marking->strm_len = p_strm_data->strm_buff_read_bits - strm_pos -
                                    8*p_strm_data->emul_byte_count;

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function name: h264bsdCheckPpsId

        Functional description:
            Peek value of pic_parameter_set_id from the slice header. Function
            does not modify current stream positions but copies the stream
            data structure to tmp structure which is used while accessing
            stream data.

        Inputs:
            p_strm_data       pointer to stream data structure

        Outputs:
            pic_param_set_id   value is stored here
            slice_type       value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckPpsId(strmData_t * p_strm_data, u32 * pic_param_set_id, u32 * slice_type) {

  /* Variables */

  u32 tmp, value;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  *slice_type = value;

  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);
  if(value >= MAX_NUM_PIC_PARAM_SETS)
    return (HANTRO_NOK);

  *pic_param_set_id = value;

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdCheckFrameNum

        Functional description:
            Peek value of frame_num from the slice header. Function does not
            modify current stream positions but copies the stream data
            structure to tmp structure which is used while accessing stream
            data.

        Inputs:
            p_strm_data       pointer to stream data structure
            max_frame_num

        Outputs:
            frame_num        value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckFrameNum(strmData_t * p_strm_data,
                         u32 max_frame_num, u32 * frame_num) {

  /* Variables */

  u32 tmp, value, i;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(max_frame_num);
  ASSERT(frame_num);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(max_frame_num >> i)
    i++;
  i--;

  /* frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);
  *frame_num = tmp;

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdCheckIdrPicId

        Functional description:
            Peek value of idr_pic_id from the slice header. Function does not
            modify current stream positions but copies the stream data
            structure to tmp structure which is used while accessing stream
            data.

        Inputs:
            p_strm_data       pointer to stream data structure
            max_frame_num     max frame number from active SPS
            nal_unit_type     type of the current NAL unit

        Outputs:
            idr_pic_id        value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckIdrPicId(strmData_t * p_strm_data,
                         u32 max_frame_num,
                         nalUnitType_e nal_unit_type, u32 field_pic_flag,
                         u32 * idr_pic_id) {

  /* Variables */

  u32 tmp, value, i;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(max_frame_num);
  ASSERT(idr_pic_id);

  /* nal_unit_type must be equal to 5 because otherwise idr_pic_id is not
   * present */
  if(nal_unit_type != NAL_CODED_SLICE_IDR)
    return (HANTRO_NOK);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (field_pic_flag) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    if (tmp) {
      tmp = h264bsdGetBits(tmp_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
    }
  }

  /* idr_pic_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, idr_pic_id);
  if(tmp != HANTRO_OK)
    return (tmp);

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdCheckPicOrderCntLsb

        Functional description:
            Peek value of pic_order_cnt_lsb from the slice header. Function
            does not modify current stream positions but copies the stream
            data structure to tmp structure which is used while accessing
            stream data.

        Inputs:
            p_strm_data       pointer to stream data structure
            p_seq_param_set    pointer to active SPS
            nal_unit_type     type of the current NAL unit

        Outputs:
            pic_order_cnt_lsb  value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckPicOrderCntLsb(strmData_t * p_strm_data,
                               seqParamSet_t * p_seq_param_set,
                               nalUnitType_e nal_unit_type,
                               u32 * pic_order_cnt_lsb) {

  /* Variables */

  u32 tmp, value, i;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_seq_param_set);
  ASSERT(pic_order_cnt_lsb);

  /* pic_order_cnt_type must be equal to 0 */
  ASSERT(p_seq_param_set->pic_order_cnt_type == 0);
  ASSERT(p_seq_param_set->max_frame_num);
  ASSERT(p_seq_param_set->max_pic_order_cnt_lsb);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(p_seq_param_set->max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (!p_seq_param_set->frame_mbs_only_flag) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    if (tmp) {
      tmp = h264bsdGetBits(tmp_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
    }
  }
  /* skip idr_pic_id when necessary */
  if(nal_unit_type == NAL_CODED_SLICE_IDR) {
    tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  /* log2(max_pic_order_cnt_lsb) -> num bits to represent pic_order_cnt_lsb */
  i = 0;
  while(p_seq_param_set->max_pic_order_cnt_lsb >> i)
    i++;
  i--;

  /* pic_order_cnt_lsb */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);
  *pic_order_cnt_lsb = tmp;

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdCheckDeltaPicOrderCntBottom

        Functional description:
            Peek value of delta_pic_order_cnt_bottom from the slice header.
            Function does not modify current stream positions but copies the
            stream data structure to tmp structure which is used while
            accessing stream data.

        Inputs:
            p_strm_data       pointer to stream data structure
            p_seq_param_set    pointer to active SPS
            nal_unit_type     type of the current NAL unit

        Outputs:
            delta_pic_order_cnt_bottom  value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckDeltaPicOrderCntBottom(strmData_t * p_strm_data,
                                       seqParamSet_t * p_seq_param_set,
                                       nalUnitType_e nal_unit_type,
                                       i32 * delta_pic_order_cnt_bottom) {

  /* Variables */

  u32 tmp, value, i;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_seq_param_set);
  ASSERT(delta_pic_order_cnt_bottom);

  /* pic_order_cnt_type must be equal to 0 and pic_order_present_flag must be HANTRO_TRUE
   * */
  ASSERT(p_seq_param_set->pic_order_cnt_type == 0);
  ASSERT(p_seq_param_set->max_frame_num);
  ASSERT(p_seq_param_set->max_pic_order_cnt_lsb);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(p_seq_param_set->max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (!p_seq_param_set->frame_mbs_only_flag) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    if (tmp) {
      tmp = h264bsdGetBits(tmp_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
    }
  }
  /* skip idr_pic_id when necessary */
  if(nal_unit_type == NAL_CODED_SLICE_IDR) {
    tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  /* log2(max_pic_order_cnt_lsb) -> num bits to represent pic_order_cnt_lsb */
  i = 0;
  while(p_seq_param_set->max_pic_order_cnt_lsb >> i)
    i++;
  i--;

  /* skip pic_order_cnt_lsb */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  /* delta_pic_order_cnt_bottom */
  tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, delta_pic_order_cnt_bottom);
  if(tmp != HANTRO_OK)
    return (tmp);

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdCheckDeltaPicOrderCnt

        Functional description:
            Peek values delta_pic_order_cnt[0] and delta_pic_order_cnt[1]
            from the slice header. Function does not modify current stream
            positions but copies the stream data structure to tmp structure
            which is used while accessing stream data.

        Inputs:
            p_strm_data               pointer to stream data structure
            p_seq_param_set            pointer to active SPS
            nal_unit_type             type of the current NAL unit
            pic_order_present_flag     flag indicating if delta_pic_order_cnt[1]
                                    is present in the stream

        Outputs:
            delta_pic_order_cnt        values are stored here

        Returns:
            HANTRO_OK               success
            HANTRO_NOK              invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckDeltaPicOrderCnt(strmData_t * p_strm_data,
                                 seqParamSet_t * p_seq_param_set,
                                 nalUnitType_e nal_unit_type,
                                 u32 pic_order_present_flag,
                                 i32 * delta_pic_order_cnt) {

  /* Variables */

  u32 tmp, value, i;
  u32 field_pic_flag = 0;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_seq_param_set);
  ASSERT(delta_pic_order_cnt);

  /* pic_order_cnt_type must be equal to 1 and delta_pic_order_always_zero_flag must
   * be HANTRO_FALSE */
  ASSERT(p_seq_param_set->pic_order_cnt_type == 1);
  ASSERT(!p_seq_param_set->delta_pic_order_always_zero_flag);
  ASSERT(p_seq_param_set->max_frame_num);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(p_seq_param_set->max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (!p_seq_param_set->frame_mbs_only_flag) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    field_pic_flag = tmp;
    if (tmp) {
      tmp = h264bsdGetBits(tmp_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
    }
  }
  /* skip idr_pic_id when necessary */
  if(nal_unit_type == NAL_CODED_SLICE_IDR) {
    tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  /* delta_pic_order_cnt[0] */
  tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &delta_pic_order_cnt[0]);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* delta_pic_order_cnt[1] if present */
  if(pic_order_present_flag && !field_pic_flag) {
    tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &delta_pic_order_cnt[1]);
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------
    Function name   : h264bsdCheckPriorPicsFlag
    Description     : Must be called only for IDR slices
    Return type     : u32
    Argument        : u32 * no_output_of_prior_pics_flag
    Argument        : const strmData_t *p_strm_data
    Argument        : const seqParamSet_t *p_seq_param_set
    Argument        : const picParamSet_t *p_pic_param_set
------------------------------------------------------------------------------*/
u32 h264bsdCheckPriorPicsFlag(u32 *no_output_of_prior_pics_flag,
                              const strmData_t *p_strm_data,
                              const seqParamSet_t *p_seq_param_set,
                              const picParamSet_t *p_pic_param_set) {
  /* Variables */

  u32 tmp, value, i;
  i32 ivalue;
  u32 field_pic_flag = 0;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_seq_param_set);
  ASSERT(p_pic_param_set);
  ASSERT(no_output_of_prior_pics_flag);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(p_seq_param_set->max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (!p_seq_param_set->frame_mbs_only_flag) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    field_pic_flag = tmp;
    if (tmp) {
      tmp = h264bsdGetBits(tmp_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
    }
  }
  /* skip idr_pic_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  if(p_seq_param_set->pic_order_cnt_type == 0) {
    /* log2(max_pic_order_cnt_lsb) -> num bits to represent pic_order_cnt_lsb */
    i = 0;
    while(p_seq_param_set->max_pic_order_cnt_lsb >> i)
      i++;
    i--;

    /* skip pic_order_cnt_lsb */
    tmp = h264bsdGetBits(tmp_strm_data, i);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);

    if(p_pic_param_set->pic_order_present_flag && !field_pic_flag) {
      /* skip delta_pic_order_cnt_bottom */
      tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &ivalue);
      if(tmp != HANTRO_OK)
        return (tmp);
    }
  }

  if(p_seq_param_set->pic_order_cnt_type == 1 &&
      !p_seq_param_set->delta_pic_order_always_zero_flag) {
    /* skip delta_pic_order_cnt[0] */
    tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &ivalue);
    if(tmp != HANTRO_OK)
      return (tmp);

    /* skip delta_pic_order_cnt[1] if present */
    if(p_pic_param_set->pic_order_present_flag && !field_pic_flag) {
      tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &ivalue);
      if(tmp != HANTRO_OK)
        return (tmp);
    }
  }

  /* skip redundant_pic_cnt */
  if(p_pic_param_set->redundant_pic_cnt_present_flag) {
    tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  *no_output_of_prior_pics_flag = h264bsdGetBits(tmp_strm_data, 1);
  if(*no_output_of_prior_pics_flag == END_OF_STREAM)
    return (HANTRO_NOK);

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: PredWeightTable

        Functional description:

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/

u32 PredWeightTable(strmData_t *p_strm_data, sliceHeader_t *p_slice_header,
                    u32 mono_chrome) {

  /* Variables */

  u32 tmp, value, i;
  i32 itmp;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_slice_header);

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (!mono_chrome)
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  for (i = 0; i < p_slice_header->num_ref_idx_l0_active; i++) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == 1) {
      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
    }
    if (!mono_chrome) {
      tmp = h264bsdGetBits(p_strm_data, 1);
      if (tmp == 1) {
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      }
    }
  }

  if (IS_B_SLICE(p_slice_header->slice_type)) {
    for (i = 0; i < p_slice_header->num_ref_idx_l1_active; i++) {
      tmp = h264bsdGetBits(p_strm_data, 1);
      if (tmp == 1) {
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      }
      if (!mono_chrome) {
        tmp = h264bsdGetBits(p_strm_data, 1);
        if (tmp == 1) {
          tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
          tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
          tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
          tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
        }
      }
    }
  }

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function name: h264bsdIsOppositeFieldPic

        Functional description:

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/

u32 h264bsdIsOppositeFieldPic(sliceHeader_t * p_slice_curr,
                              sliceHeader_t * p_slice_prev,
                              u32 *second_field, u32 prev_ref_frame_num,
                              u32 new_picture) {

  /* Variables */

  /* Code */

  ASSERT(p_slice_curr);
  ASSERT(p_slice_prev);

  if ((p_slice_curr->frame_num == p_slice_prev->frame_num ||
       p_slice_curr->frame_num == prev_ref_frame_num) &&
      p_slice_curr->field_pic_flag && p_slice_prev->field_pic_flag &&
      p_slice_curr->bottom_field_flag != p_slice_prev->bottom_field_flag &&
      *second_field && !new_picture) {
    *second_field = 0;
    return HANTRO_TRUE;
  } else {
    *second_field = p_slice_curr->field_pic_flag ? 1 : 0;
    return HANTRO_FALSE;
  }

}

/*------------------------------------------------------------------------------

    Function: h264bsdCheckFieldPicFlag

        Functional description:

        Inputs:
            p_strm_data       pointer to stream data structure
            max_frame_num     max frame number from active SPS
            nal_unit_type     type of the current NAL unit

        Outputs:
            idr_pic_id        value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckFieldPicFlag(strmData_t *p_strm_data,
                             u32 max_frame_num,
                             u32 field_pic_flag_present,
                             u32 *field_pic_flag) {

  /* Variables */

  u32 tmp, value, i;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(max_frame_num);
  ASSERT(field_pic_flag);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (field_pic_flag_present) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    *field_pic_flag = tmp;
  }

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdBottomFieldFlag

        Functional description:

        Inputs:
            p_strm_data       pointer to stream data structure
            max_frame_num     max frame number from active SPS
            nal_unit_type     type of the current NAL unit

        Outputs:
            idr_pic_id        value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckBottomFieldFlag(strmData_t *p_strm_data,
                                u32 max_frame_num,
                                u32 field_pic_flag,
                                u32 *bottom_field_flag) {

  /* Variables */

  u32 tmp, value, i;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(max_frame_num);
  ASSERT(bottom_field_flag);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (field_pic_flag) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    if (tmp) {
      tmp = h264bsdGetBits(tmp_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
      *bottom_field_flag = tmp;
    }
  }

  return (HANTRO_OK);

}


/*------------------------------------------------------------------------------

    Function: h264bsdCheckFirstMbInSlice

        Functional description:
            Peek value of first_mb_in_slice from the slice header. Function does not
            modify current stream positions but copies the stream data
            structure to tmp structure which is used while accessing stream
            data.

        Inputs:
            p_strm_data       pointer to stream data structure
            nal_unit_type     type of the current NAL unit

        Outputs:
            first_mb_in_slice  value is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdCheckFirstMbInSlice(strmData_t * p_strm_data,
                               nalUnitType_e nal_unit_type,
                               u32 * first_mb_in_slice) {

  /* Variables */

  u32 tmp, value;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(first_mb_in_slice);

  /* nal_unit_type must be equal to 5 because otherwise we shouldn't be
   * here */
  if(nal_unit_type != NAL_CODED_SLICE_IDR)
    return (HANTRO_NOK);

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  *first_mb_in_slice = value;

  return (HANTRO_OK);

}


/*------------------------------------------------------------------------------
    Function name   : h264bsdCheckRedundantPicCnt
    Description     : Must be called only for IDR slices
    Return type     : u32
    Argument        : const strmData_t * p_strm_data
    Argument        : const seqParamSet_t * p_seq_param_set
    Argument        : const picParamSet_t * p_pic_param_set
    Argument        : u32 * redundant_pic_cnt
------------------------------------------------------------------------------*/
u32 h264bsdCheckRedundantPicCnt(const strmData_t * p_strm_data,
                                const seqParamSet_t * p_seq_param_set,
                                const picParamSet_t * p_pic_param_set,
                                u32 * redundant_pic_cnt ) {
  /* Variables */

  u32 tmp, value, i;
  i32 ivalue;
  u32 field_pic_flag = 0;
  strmData_t tmp_strm_data[1];

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_seq_param_set);
  ASSERT(p_pic_param_set);
  ASSERT(redundant_pic_cnt);

  if(!p_pic_param_set->redundant_pic_cnt_present_flag) {
    *redundant_pic_cnt = 0;
    return HANTRO_OK;
  }

  /* don't touch original stream position params */
  *tmp_strm_data = *p_strm_data;

  /* skip first_mb_in_slice */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* slice_type */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* skip pic_parameter_set_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  /* log2(max_frame_num) -> num bits to represent frame_num */
  i = 0;
  while(p_seq_param_set->max_frame_num >> i)
    i++;
  i--;

  /* skip frame_num */
  tmp = h264bsdGetBits(tmp_strm_data, i);
  if(tmp == END_OF_STREAM)
    return (HANTRO_NOK);

  if (!p_seq_param_set->frame_mbs_only_flag) {
    tmp = h264bsdGetBits(tmp_strm_data, 1);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);
    field_pic_flag = tmp;
    if (tmp) {
      tmp = h264bsdGetBits(tmp_strm_data, 1);
      if(tmp == END_OF_STREAM)
        return (HANTRO_NOK);
    }
  }
  /* skip idr_pic_id */
  tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if(tmp != HANTRO_OK)
    return (tmp);

  if(p_seq_param_set->pic_order_cnt_type == 0) {
    /* log2(max_pic_order_cnt_lsb) -> num bits to represent pic_order_cnt_lsb */
    i = 0;
    while(p_seq_param_set->max_pic_order_cnt_lsb >> i)
      i++;
    i--;

    /* skip pic_order_cnt_lsb */
    tmp = h264bsdGetBits(tmp_strm_data, i);
    if(tmp == END_OF_STREAM)
      return (HANTRO_NOK);

    if(p_pic_param_set->pic_order_present_flag && !field_pic_flag) {
      /* skip delta_pic_order_cnt_bottom */
      tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &ivalue);
      if(tmp != HANTRO_OK)
        return (tmp);
    }
  }

  if(p_seq_param_set->pic_order_cnt_type == 1 &&
      !p_seq_param_set->delta_pic_order_always_zero_flag) {
    /* skip delta_pic_order_cnt[0] */
    tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &ivalue);
    if(tmp != HANTRO_OK)
      return (tmp);

    /* skip delta_pic_order_cnt[1] if present */
    if(p_pic_param_set->pic_order_present_flag && !field_pic_flag) {
      tmp = h264bsdDecodeExpGolombSigned(tmp_strm_data, &ivalue);
      if(tmp != HANTRO_OK)
        return (tmp);
    }
  }

  /* redundant_pic_cnt */
  if(p_pic_param_set->redundant_pic_cnt_present_flag) {
    tmp = h264bsdDecodeExpGolombUnsigned(tmp_strm_data, &value);
    if(tmp != HANTRO_OK)
      return (tmp);
    *redundant_pic_cnt = value;
  }

  return (HANTRO_OK);

}
