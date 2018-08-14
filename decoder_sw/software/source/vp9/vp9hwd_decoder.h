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

#ifndef __VP9_DECODER_H__
#define __VP9_DECODER_H__

#include "basetype.h"
#include "commonvp9.h"
#include "sw_util.h"

#define VP9_ACTIVE_REFS 3
#define VP9_REF_LIST_SIZE 8

#define DEC_8190_ALIGN_MASK 0x07U
#define DEC_8190_MODE_VP9 0x09U

#define VP9HWDEC_HW_RESERVED 0x0100
#define VP9HWDEC_SYSTEM_ERROR 0x0200
#define VP9HWDEC_SYSTEM_TIMEOUT 0x0300

#define MAX_NBR_OF_DCT_PARTITIONS (8)

enum Vp9ColorSpace {
  VP9_YCbCr_BT601,
  VP9_CUSTOM
};

struct Vp9Decoder {
  u32 dec_mode;

  /* Current frame dimensions */
  u32 width;
  u32 height;
  u32 scaled_width;
  u32 scaled_height;
  u32 last_width;
  u32 last_height;

  u32 vp_version;
  u32 vp_profile;

  u32 bit_depth;
  u32 key_frame;
  u32 prev_is_key_frame;
  u32 scaling_active;
  u32 resolution_change;

  /* DCT coefficient partitions */
  u32 offset_to_dct_parts;
  u32 dct_partition_offsets[MAX_NBR_OF_DCT_PARTITIONS];

  enum Vp9ColorSpace color_space;
  u32 clamping;
  u32 error_resilient;
  u32 show_frame;
  u32 prev_show_frame;
  u32 show_existing_frame;
  u32 show_existing_frame_index;
  u32 intra_only;
  u32 subsampling_x;
  u32 subsampling_y;

  u32 frame_context_idx;
  u32 active_ref_idx[ALLOWED_REFS_PER_FRAME];
  u32 refresh_frame_flags;
  u32 refresh_entropy_probs;
  u32 frame_parallel_decoding;
  u32 reset_frame_context;

  u32 ref_frame_sign_bias[MAX_REF_FRAMES];
  i32 loop_filter_level;
  u32 loop_filter_sharpness;

  /* Quantization parameters */
  i32 qp_yac, qp_ydc, qp_y2_ac, qp_y2_dc, qp_ch_ac, qp_ch_dc;

  /* From here down, frame-to-frame persisting stuff */

  u32 lossless;
  u32 transform_mode;
  u32 allow_high_precision_mv;
  u32 allow_comp_inter_inter;
  u32 mcomp_filter_type;
  u32 pred_filter_mode;
  u32 comp_pred_mode;
  u32 comp_fixed_ref;
  u32 comp_var_ref[2];
  u32 log2_tile_columns;
  u32 log2_tile_rows;

  struct Vp9EntropyProbs entropy;
  struct Vp9EntropyProbs entropy_last[NUM_FRAME_CONTEXTS];
  struct Vp9AdaptiveEntropyProbs prev_ctx;
  struct Vp9EntropyCounts ctx_ctr;

  /* Segment and macroblock specific values */
  u32 segment_enabled;
  u32 segment_map_update;
  u32 segment_map_temporal_update;
  u32 segment_feature_mode; /* ABS data or delta data */
  u32 segment_feature_enable[MAX_MB_SEGMENTS][SEG_LVL_MAX];
  i32 segment_feature_data[MAX_MB_SEGMENTS][SEG_LVL_MAX];
  u32 mode_ref_lf_enabled;
  i32 mb_ref_lf_delta[MAX_REF_LF_DELTAS];
  i32 mb_mode_lf_delta[MAX_MODE_LF_DELTAS];
  i32 ref_frame_map[NUM_REF_FRAMES];
  u32 existing_ref_map;
  u32 reset_frame_flags;

  u32 frame_tag_size;

  /* Value to remember last frames prediction for hits into most
   * probable reference frame */
  u32 refbu_pred_hits;

  u32 probs_decoded;
};

struct DecAsicBuffers;

void Vp9ResetDecoder(struct Vp9Decoder *dec, struct DecAsicBuffers *asic_buff);

#endif /* __VP9_BOOL_H__ */
