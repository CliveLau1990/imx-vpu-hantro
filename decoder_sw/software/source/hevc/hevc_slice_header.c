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

#include "hevc_slice_header.h"
#include "hevc_exp_golomb.h"
#include "hevc_nal_unit.h"
#include "hevc_dpb.h"
#include "hevc_util.h"
#include "dwl.h"
#include "sw_util.h"

/* Decode slice header data from the stream. */
u32 HevcDecodeSliceHeader(struct StrmData* stream,
                          struct SliceHeader* slice_header,
                          struct SeqParamSet* seq_param_set,
                          struct PicParamSet* pic_param_set,
                          struct NalUnit* nal_unit) {

  u32 tmp, i, value;
  u32 read_bits;
  u32 first_slice_in_pic;
  u32 tmp_count;

  ASSERT(stream);
  ASSERT(slice_header);
  ASSERT(seq_param_set);
  ASSERT(pic_param_set);
  ASSERT(IS_SLICE_NAL_UNIT(nal_unit));

  (void)DWLmemset(slice_header, 0, sizeof(struct SliceHeader));

  /* first slice in pic */
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM) return(HANTRO_NOK);
  first_slice_in_pic = tmp;

  if (IS_RAP_NAL_UNIT(nal_unit)) {
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return(HANTRO_NOK);
    slice_header->no_output_of_prior_pics_flag = tmp;
  }

  tmp = HevcDecodeExpGolombUnsigned(stream, &value);
  if (tmp != HANTRO_OK) return (tmp);
  slice_header->pic_parameter_set_id = value;

  /* SW only decodes the first slice in the pic -> return error if not
   * first slice.
   * TODO: error handling, if first slice is missing, should we read
   * something (poc?) to properly handle missing stuff */
  if (!first_slice_in_pic) return HANTRO_NOK;

  /* TODO: related to error handling */
  /*
  if (pic_param_set->dependent_slice_segments_enabled)
  {
      tmp = SwGetBits(stream, 1);
      slice_header->dependent_slice_flag = tmp;
  }
  pic_size_in_ctbs =
      NEXT_MULTIPLE(seq_param_set->pic_width,  max_cb_size) *
      NEXT_MULTIPLE(seq_param_set->pic_height, max_cb_size);

  tmp = SwNumBits(pic_size_in_ctbs);
  slice_header->slice_address = SwGetBits(stream, tmp);
  */

  /* if (!slice_header->dependent_slice_flag) */

  tmp = SwGetBits(stream, pic_param_set->num_extra_slice_header_bits);
  if (tmp == END_OF_STREAM) return(HANTRO_NOK);

  tmp = HevcDecodeExpGolombUnsigned(stream, &value);
  if (tmp != HANTRO_OK) return (tmp);
  slice_header->slice_type = value;

  /* slice type has to be either I, P or B slice. Only I slice is
   * allowed when current NAL unit is an RAP NAL */
  /* Some bitstreams have max_dec_pic_buffering = 0 while slice coded to P slice.*/
  if (!IS_I_SLICE(slice_header->slice_type) &&
      (IS_RAP_NAL_UNIT(nal_unit) /*||
       seq_param_set->max_dec_pic_buffering[seq_param_set->max_sub_layers -
           1] == 0*/)) {
    ERROR_PRINT("slice_type");
    return (HANTRO_NOK);
  }

  /* to determine number of bits skipped by hw slice header decoding */
  read_bits = stream->strm_buff_read_bits;
  tmp_count = stream->emul_byte_count;
  stream->emul_byte_count = 0;
  ;

  if (pic_param_set->output_flag_present) {
    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return(HANTRO_NOK);
    slice_header->pic_output_flag = tmp;
  }

  /* Not supported yet
  if (seq_param_set->separate_colour_plane)
      slice_header->colour_plane_id = SwGetBits(stream, 2);
  */

  if (!IS_IDR_NAL_UNIT(nal_unit)) {
    tmp =
      SwGetBits(stream, SwNumBits(seq_param_set->max_pic_order_cnt_lsb - 1));
    if (tmp == END_OF_STREAM) return(HANTRO_NOK);
    slice_header->pic_order_cnt_lsb = tmp;

    tmp = SwGetBits(stream, 1);
    if (tmp == END_OF_STREAM) return(HANTRO_NOK);
    slice_header->short_term_ref_pic_set_sps_flag = tmp;
    if (!slice_header->short_term_ref_pic_set_sps_flag) {
      tmp = HevcDecodeShortTermRefPicSet(
              stream,
              /* decoded into last position in st_ref_pic_set array of sps */
              seq_param_set->st_ref_pic_set +
              seq_param_set->num_short_term_ref_pic_sets,
              1, seq_param_set->num_short_term_ref_pic_sets == 0);
      slice_header->short_term_ref_pic_set_idx =
        seq_param_set->num_short_term_ref_pic_sets;
      slice_header->st_ref_pic_set =
        seq_param_set
        ->st_ref_pic_set[slice_header->short_term_ref_pic_set_idx];
      DWLmemset(seq_param_set->st_ref_pic_set +
                slice_header->short_term_ref_pic_set_idx,
                0, sizeof(struct StRefPicSet));

    } else {
      if (seq_param_set->num_short_term_ref_pic_sets > 1) {
        i = SwNumBits(seq_param_set->num_short_term_ref_pic_sets - 1);
        tmp = SwGetBits(stream, i);
        if (tmp == END_OF_STREAM) return(HANTRO_NOK);
        slice_header->short_term_ref_pic_set_idx = tmp;
      }
      slice_header->st_ref_pic_set =
        seq_param_set
        ->st_ref_pic_set[slice_header->short_term_ref_pic_set_idx];
    }

    if (seq_param_set->long_term_ref_pic_present) {
      u32 tot_long_term;
      u32 len;

      if (seq_param_set->num_long_term_ref_pics) {
        tmp = HevcDecodeExpGolombUnsigned(stream, &value);
        if (tmp != HANTRO_OK) return (tmp);
        slice_header->num_long_term_sps = value;
      }
      tmp = HevcDecodeExpGolombUnsigned(stream, &value);
      if (tmp != HANTRO_OK) return (tmp);
      slice_header->num_long_term_pics = value;

      tot_long_term =
        slice_header->num_long_term_sps + slice_header->num_long_term_pics;

      if (seq_param_set->num_long_term_ref_pics > 1)
        len = SwNumBits(seq_param_set->num_long_term_ref_pics - 1);
      else
        len = 0;
      for (i = 0; i < tot_long_term; i++) {
        if (i < slice_header->num_long_term_sps) {
          tmp = SwGetBits(stream, len);
          if (tmp == END_OF_STREAM) return(HANTRO_NOK);
          slice_header->lt_idx_sps[i] = tmp;
          slice_header->used_by_curr_pic_lt[i] =
            seq_param_set->used_by_curr_pic_lt[tmp];
        } else {
          slice_header->poc_lsb_lt[i] = SwGetBits(
                                          stream, SwNumBits(seq_param_set->max_pic_order_cnt_lsb - 1));
          if (slice_header->poc_lsb_lt[i] == END_OF_STREAM) return(HANTRO_NOK);
          slice_header->used_by_curr_pic_lt[i] = SwGetBits(stream, 1);
          if (slice_header->used_by_curr_pic_lt[i] == END_OF_STREAM) return(HANTRO_NOK);
        }

        slice_header->delta_poc_msb_present_flag[i] = SwGetBits(stream, 1);
        if (slice_header->delta_poc_msb_present_flag[i] == END_OF_STREAM) return(HANTRO_NOK);

        if (slice_header->delta_poc_msb_present_flag[i]) {
          tmp = HevcDecodeExpGolombUnsigned(stream, &value);
          if (tmp != HANTRO_OK) return (tmp);
          slice_header->delta_poc_msb_cycle_lt[i] = value;
        }
        /* delta_poc_msb_cycle is delta to previous value, long-term
         * pics from SPS and long-term pics specified here handled
         * separately */
        if (i && i != slice_header->num_long_term_sps)
          slice_header->delta_poc_msb_cycle_lt[i] +=
            slice_header->delta_poc_msb_cycle_lt[i - 1];
      }
    }

    slice_header->hw_skip_bits = stream->strm_buff_read_bits - read_bits;
    slice_header->hw_skip_bits -= stream->emul_byte_count * 8;
    stream->emul_byte_count += tmp_count;

    if (seq_param_set->temporal_mvp_enable) {
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM) return(HANTRO_NOK);
    }

    if (seq_param_set->sample_adaptive_offset_enable) {
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM) return(HANTRO_NOK);
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM) return(HANTRO_NOK);
    }
    if (IS_P_SLICE(slice_header->slice_type) ||
        IS_B_SLICE(slice_header->slice_type)) {
      tmp = SwGetBits(stream, 1);
      if (tmp == END_OF_STREAM) return(HANTRO_NOK);
      if (tmp) {
        /* num_ref_idx_l0_active_minus1 */
        tmp = HevcDecodeExpGolombUnsigned(stream, &value);
        if (tmp != HANTRO_OK) return (tmp);
        slice_header->num_ref_idx_l0_active = value + 1;
        if (IS_B_SLICE(slice_header->slice_type)) {
          tmp = HevcDecodeExpGolombUnsigned(stream, &value);
          if (tmp != HANTRO_OK) return (tmp);
          slice_header->num_ref_idx_l1_active = value + 1;
        }
      }
      /* set num_ref_idx_l0_active and num_ref_idx_l1_active from pic param set */
      else {
        slice_header->num_ref_idx_l0_active = pic_param_set->num_ref_idx_l0_active;
        slice_header->num_ref_idx_l1_active = pic_param_set->num_ref_idx_l1_active;
      }
      if (!IS_B_SLICE(slice_header->slice_type))
        slice_header->num_ref_idx_l1_active = 0;
    }
  } else {
    slice_header->hw_skip_bits = stream->strm_buff_read_bits - read_bits;
    slice_header->hw_skip_bits -= stream->emul_byte_count * 8;
    stream->emul_byte_count += tmp_count;
  }
  return (HANTRO_OK);
}

/* Peek value of pic_parameter_set_id from the slice header. Function does not
 * modify current stream positions but copies the stream data structure to tmp
 * structure which is used while accessing stream data. */
u32 HevcCheckPpsId(struct StrmData* stream, u32* pic_param_set_id,
                   u32 is_rap_nal_unit) {

  u32 tmp, value;
  struct StrmData tmp_strm_data[1];

  ASSERT(stream);

  /* don't touch original stream position params */
  *tmp_strm_data = *stream;

  /* first slice in pic */
  tmp = SwGetBits(tmp_strm_data, 1);
  if (tmp == END_OF_STREAM) return(HANTRO_NOK);

  if (is_rap_nal_unit) {
    tmp = SwGetBits(tmp_strm_data, 1);
    if (tmp == END_OF_STREAM) return(HANTRO_NOK);
  }

  tmp = HevcDecodeExpGolombUnsigned(tmp_strm_data, &value);
  if (tmp != HANTRO_OK) return (tmp);
  *pic_param_set_id = value;

  return (tmp);
}

u32 HevcCheckPriorPicsFlag(u32* no_output_of_prior_pics_flag,
                           const struct StrmData* stream) {

  u32 tmp;

  tmp = SwShowBits(stream, 2);

  *no_output_of_prior_pics_flag = tmp & 0x1;

  return (HANTRO_OK);
}
