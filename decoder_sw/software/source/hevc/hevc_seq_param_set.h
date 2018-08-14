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

#ifndef HEVC_SEQ_PARAM_SET_H_
#define HEVC_SEQ_PARAM_SET_H_

#include "basetype.h"
#include "hevc_cfg.h"
#include "hevc_vui.h"
#include "sw_stream.h"

struct StRefPicElem {
  i32 delta_poc;
  u32 used_by_curr_pic;
};

struct StRefPicSet {
  u32 num_negative_pics;
  u32 num_positive_pics;
  struct StRefPicElem elem[MAX_DPB_SIZE];
};

struct Profile {
  u32 general_profile_space;
  u32 general_tier_flag;
  u32 general_profile_idc;
  u32 general_profile_compatibility_flags;
  u32 general_level_idc;
  u32 sub_layer_profile_space[MAX_SUB_LAYERS];
  u32 sub_layer_tier_flag[MAX_SUB_LAYERS];
  u32 sub_layer_profile_idc[MAX_SUB_LAYERS];
  u32 sub_layer_profile_compatibility_flags[MAX_SUB_LAYERS];
  u32 sub_layer_level_idc[MAX_SUB_LAYERS];
  u32 progressive_source_flag;
  u32 interlaced_source_flag;
  u32 non_packed_constraint_flag;
  u32 frame_only_contraint_flag;
  u32 sub_layer_progressive_source_flag[MAX_SUB_LAYERS];
  u32 sub_layer_interlaced_source_flag[MAX_SUB_LAYERS];
  u32 sub_layer_non_packed_constraint_flag[MAX_SUB_LAYERS];
  u32 sub_layer_frame_only_contraint_flag[MAX_SUB_LAYERS];
};

/* structure to store sequence parameter set information decoded from the
 * stream */
struct SeqParamSet {
  u32 vps_id;
  u32 max_sub_layers;
  struct Profile profile;
  u32 seq_parameter_set_id;
  u32 chroma_format_idc;
  u32 separate_colour_plane;
  u32 pic_width;
  u32 pic_height;
  u32 pic_cropping_flag;
  u32 pic_crop_left_offset;
  u32 pic_crop_right_offset;
  u32 pic_crop_top_offset;
  u32 pic_crop_bottom_offset;
  u32 bit_depth_luma;
  u32 bit_depth_chroma;
  u32 pcm_enabled;
  u32 pcm_bit_depth_luma;
  u32 pcm_bit_depth_chroma;
  u32 max_pic_order_cnt_lsb;
  u32 sub_layer_ordering_info_present;
  u32 max_dec_pic_buffering[MAX_SUB_LAYERS];
  u32 max_num_reorder_pics[MAX_SUB_LAYERS];
  u32 max_latency_increase[MAX_SUB_LAYERS];
  u32 restricted_ref_pic_lists;
  u32 lists_modification_present_flag;
  u32 log_min_coding_block_size;
  u32 log_max_coding_block_size;
  u32 log_min_transform_block_size;
  u32 log_max_transform_block_size;
  u32 log_min_pcm_block_size;
  u32 log_max_pcm_block_size;
  u32 max_transform_hierarchy_depth_intra;
  u32 max_transform_hierarchy_depth_inter;
  u32 scaling_list_enable;
  u32 scaling_list_present_flag;
  u8 scaling_list[4][6][64];
  u32 asymmetric_motion_partitions_enable;
  u32 sample_adaptive_offset_enable;
  u32 pcm_loop_filter_disable;
  u32 temporal_id_nesting;
  u32 num_short_term_ref_pic_sets;
  struct StRefPicSet st_ref_pic_set[MAX_NUM_ST_REF_PIC_SETS + 1];
  u32 long_term_ref_pic_present;
  u32 num_long_term_ref_pics;
  u32 lt_ref_pic_poc_lsb[MAX_NUM_LT_REF_PICS_SPS];
  u32 used_by_curr_pic_lt[MAX_NUM_LT_REF_PICS_SPS];
  u32 temporal_mvp_enable;
  u32 strong_intra_smoothing_enable;
  u32 vui_parameters_present_flag;
  struct VuiParameters vui_parameters;
  u32 max_dpb_size;
};

u32 ProfileAndLevel(struct StrmData *stream, struct Profile *p,
                    u32 profile_present, u32 max_num_sub_layers);
u32 HevcDecodeSeqParamSet(struct StrmData *stream,
                          struct SeqParamSet *seq_param_set,
                          u32* vui_update_flag);
u32 HevcDecodeShortTermRefPicSet(struct StrmData *stream,
                                 struct StRefPicSet *st_rps, u32 slice_header,
                                 u32 first_set);
u32 HevcCompareSeqParamSets(struct SeqParamSet *sps1, struct SeqParamSet *sps2);

#endif /* #ifdef HEVC_SEQ_PARAM_SET_H_ */
