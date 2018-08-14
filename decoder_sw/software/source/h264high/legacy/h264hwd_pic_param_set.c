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

#include "h264hwd_pic_param_set.h"
#include "h264hwd_util.h"
#include "h264hwd_vlc.h"
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

/* lookup table for ceil(log2(num_slice_groups)), i.e. number of bits needed to
 * represent range [0, num_slice_groups)
 *
 * NOTE: if MAX_NUM_SLICE_GROUPS is higher than 8 this table has to be resized
 * accordingly */
static const u32 CeilLog2NumSliceGroups[8] = {1, 1, 2, 2, 3, 3, 3, 3};

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function name: h264bsdDecodePicParamSet

        Functional description:
            Decode picture parameter set information from the stream.

            Function allocates memory for
                - run lengths if slice group map type is 0
                - top-left and bottom-right arrays if map type is 2
                - for slice group ids if map type is 6

            Validity of some of the slice group mapping information depends
            on the image dimensions which are not known here. Therefore the
            validity has to be checked afterwards, currently in the parameter
            set activation phase.

        Inputs:
            p_strm_data       pointer to stream data structure

        Outputs:
            p_pic_param_set    decoded information is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      failure, invalid information or end of stream
            MEMORY_ALLOCATION_ERROR for memory allocation failure


------------------------------------------------------------------------------*/

void ScalingList(u8 scaling_list[][64], strmData_t *p_strm_data, u32 idx);
void FallbackScaling(u8 scaling_list[][64], u32 idx);

u32 h264bsdDecodePicParamSet(strmData_t *p_strm_data, picParamSet_t *p_pic_param_set) {

  /* Variables */

  u32 tmp, i, value;
  i32 itmp;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_pic_param_set);


  (void) DWLmemset(p_pic_param_set, 0, sizeof(picParamSet_t));

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                       &p_pic_param_set->pic_parameter_set_id);
  if (tmp != HANTRO_OK)
    return(tmp);
  if (p_pic_param_set->pic_parameter_set_id >= MAX_NUM_PIC_PARAM_SETS) {
    ERROR_PRINT("pic_parameter_set_id");
    return(HANTRO_NOK);
  }

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                       &p_pic_param_set->seq_parameter_set_id);
  if (tmp != HANTRO_OK)
    return(tmp);
  if (p_pic_param_set->seq_parameter_set_id >= MAX_NUM_SEQ_PARAM_SETS) {
    ERROR_PRINT("seq_param_set_id");
    return(HANTRO_NOK);
  }

  /* entropy_coding_mode_flag, shall be 0 for baseline profile */
  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_pic_param_set->entropy_coding_mode_flag = tmp;

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_pic_param_set->pic_order_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  /* num_slice_groups_minus1 */
  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK)
    return(tmp);
  p_pic_param_set->num_slice_groups = value + 1;
  if (p_pic_param_set->num_slice_groups > MAX_NUM_SLICE_GROUPS) {
    ERROR_PRINT("num_slice_groups_minus1");
    return(HANTRO_NOK);
  }

  /* decode slice group mapping information if more than one slice groups */
  if (p_pic_param_set->num_slice_groups > 1) {
#ifdef ASIC_TRACE_SUPPORT
    trace_h264_dec_tools.slice_groups = 1;
#endif
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data,
                                         &p_pic_param_set->slice_group_map_type);
    if (tmp != HANTRO_OK)
      return(tmp);
    if (p_pic_param_set->slice_group_map_type > 6) {
      ERROR_PRINT("slice_group_map_type");
      return(HANTRO_NOK);
    }

    if (p_pic_param_set->slice_group_map_type == 0) {
      ALLOCATE(p_pic_param_set->run_length,
               p_pic_param_set->num_slice_groups, u32);
      if (p_pic_param_set->run_length == NULL)
        return(MEMORY_ALLOCATION_ERROR);
      for (i = 0; i < p_pic_param_set->num_slice_groups; i++) {
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
        if (tmp != HANTRO_OK)
          return(tmp);
        p_pic_param_set->run_length[i] = value+1;
        /* param values checked in CheckPps() */
      }
    } else if (p_pic_param_set->slice_group_map_type == 2) {
      ALLOCATE(p_pic_param_set->top_left,
               p_pic_param_set->num_slice_groups - 1, u32);
      ALLOCATE(p_pic_param_set->bottom_right,
               p_pic_param_set->num_slice_groups - 1, u32);
      if (p_pic_param_set->top_left == NULL ||
          p_pic_param_set->bottom_right == NULL)
        return(MEMORY_ALLOCATION_ERROR);
      for (i = 0; i < p_pic_param_set->num_slice_groups - 1; i++) {
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
        if (tmp != HANTRO_OK)
          return(tmp);
        p_pic_param_set->top_left[i] = value;
        tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
        if (tmp != HANTRO_OK)
          return(tmp);
        p_pic_param_set->bottom_right[i] = value;
        /* param values checked in CheckPps() */
      }
    } else if ( (p_pic_param_set->slice_group_map_type == 3) ||
                (p_pic_param_set->slice_group_map_type == 4) ||
                (p_pic_param_set->slice_group_map_type == 5) ) {
      tmp = h264bsdGetBits(p_strm_data, 1);
      if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
      p_pic_param_set->slice_group_change_direction_flag =
        (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      p_pic_param_set->slice_group_change_rate = value + 1;
      /* param value checked in CheckPps() */
    } else if (p_pic_param_set->slice_group_map_type == 6) {
      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if (tmp != HANTRO_OK)
        return(tmp);
      p_pic_param_set->pic_size_in_map_units = value + 1;

      /* check pic_size_in_map_units is valid or not.
         it should be smaller than the max supported picSizeInMb. */
      if (p_pic_param_set->pic_size_in_map_units > ((4096 >> 4) * (4096 >> 4))) {
        ERROR_PRINT("pic_size_in_map_units_minus1");
        return HANTRO_NOK;
      }

      ALLOCATE(p_pic_param_set->slice_group_id,
               p_pic_param_set->pic_size_in_map_units, u32);
      if (p_pic_param_set->slice_group_id == NULL)
        return(MEMORY_ALLOCATION_ERROR);

      /* determine number of bits needed to represent range
       * [0, num_slice_groups) */
      tmp = CeilLog2NumSliceGroups[p_pic_param_set->num_slice_groups-1];

      for (i = 0; i < p_pic_param_set->pic_size_in_map_units; i++) {
        p_pic_param_set->slice_group_id[i] = h264bsdGetBits(p_strm_data, tmp);
        if ( p_pic_param_set->slice_group_id[i] >=
             p_pic_param_set->num_slice_groups ) {
          ERROR_PRINT("slice_group_id");
          return(HANTRO_NOK);
        }
      }
    }
  }

  /* num_ref_idx_l0_active_minus1 */
  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK)
    return(tmp);
  if (value > 31) {
    ERROR_PRINT("num_ref_idx_l0_active_minus1");
    return(HANTRO_NOK);
  }
  p_pic_param_set->num_ref_idx_l0_active = value + 1;

  /* num_ref_idx_l1_active_minus1 */
  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
  if (tmp != HANTRO_OK)
    return(tmp);
  if (value > 31) {
    ERROR_PRINT("num_ref_idx_l1_active_minus1");
    return(HANTRO_NOK);
  }
  p_pic_param_set->num_ref_idx_l1_active = value + 1;

  /* weighted_pred_flag, this shall be 0 for baseline profile */
  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_pic_param_set->weighted_pred_flag = tmp;

  /* weighted_bipred_idc */
  tmp = h264bsdGetBits(p_strm_data, 2);
  if (tmp > 2) {
    ERROR_PRINT("weighted_bipred_idc");
    return(HANTRO_NOK);
  }
  p_pic_param_set->weighted_bi_pred_idc = tmp;

  /* pic_init_qp_minus26 */
  tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
  if (tmp != HANTRO_OK)
    return(tmp);
  if ((itmp < -26) || (itmp > 25)) {
    ERROR_PRINT("pic_init_qp_minus26");
    return(HANTRO_NOK);
  }
  p_pic_param_set->pic_init_qp = (u32)(itmp + 26);

  /* pic_init_qs_minus26 */
  tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
  if (tmp != HANTRO_OK)
    return(tmp);
  if ((itmp < -26) || (itmp > 25)) {
    ERROR_PRINT("pic_init_qs_minus26");
    return(HANTRO_NOK);
  }

  tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
  if (tmp != HANTRO_OK)
    return(tmp);
  if ((itmp < -12) || (itmp > 12)) {
    ERROR_PRINT("chroma_qp_index_offset");
    return(HANTRO_NOK);
  }
  p_pic_param_set->chroma_qp_index_offset = itmp;

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_pic_param_set->deblocking_filter_control_present_flag =
    (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_pic_param_set->constrained_intra_pred_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  tmp = h264bsdGetBits(p_strm_data, 1);
  if (tmp == END_OF_STREAM)
    return(HANTRO_NOK);
  p_pic_param_set->redundant_pic_cnt_present_flag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

  if (h264bsdMoreRbspData(p_strm_data)) {
    tmp = h264bsdGetBits(p_strm_data, 1);
    if (tmp == END_OF_STREAM)
      return(HANTRO_NOK);
    p_pic_param_set->transform8x8_flag = tmp;
    /* pic_scaling_matrix_present_flag */
    tmp = h264bsdGetBits(p_strm_data, 1);
#ifdef ASIC_TRACE_SUPPORT
    if (tmp)
      trace_h264_dec_tools.scaling_matrix_present_type.pic = 1;
#endif
    if (tmp == END_OF_STREAM)
      return(HANTRO_NOK);
    p_pic_param_set->scaling_matrix_present_flag = tmp;
    if (tmp) {
      for (i = 0; i < 6+2*p_pic_param_set->transform8x8_flag; i++) {
        tmp = h264bsdGetBits(p_strm_data, 1);
        p_pic_param_set->scaling_list_present[i] = tmp;
        if (tmp) {
          ScalingList(p_pic_param_set->scaling_list, p_strm_data, i);
        } else
          FallbackScaling(p_pic_param_set->scaling_list, i);
      }
    }
    tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
    if (tmp != HANTRO_OK)
      return(tmp);
    if ((itmp < -12) || (itmp > 12)) {
      ERROR_PRINT("second_chroma_qp_index_offset");
      return(HANTRO_NOK);
    }
    p_pic_param_set->chroma_qp_index_offset2 = itmp;
  } else {
    p_pic_param_set->scaling_matrix_present_flag = 0;
    p_pic_param_set->transform8x8_flag = 0;
    p_pic_param_set->chroma_qp_index_offset2 = p_pic_param_set->chroma_qp_index_offset;
  }

  tmp = h264bsdRbspTrailingBits(p_strm_data);

  /* ignore possible errors in trailing bits of parameters sets */
  return(HANTRO_OK);

}
