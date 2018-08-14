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

#ifndef HEVC_DPB_H_
#define HEVC_DPB_H_

#include "basetype.h"
#include "hevc_slice_header.h"
#include "hevc_image.h"
#include "hevc_fb_mngr.h"
#include "hevc_sei.h"
#include "dwl.h"

/* enumeration to represent status of buffered image */
enum DpbPictureStatus {
  UNUSED = 0,
  NON_EXISTING,
  SHORT_TERM,
  LONG_TERM,
  EMPTY
};

/* structure to represent a buffered picture */
struct DpbPicture {
  u32 mem_idx;
  struct DWLLinearMem *data;
  struct DWLLinearMem *pp_data;
  i32 pic_num;
  i32 pic_order_cnt;
  i32 pic_order_cnt_lsb;
  enum DpbPictureStatus status;
  u32 to_be_displayed;
  u32 pic_id;
  u32 decode_id;
  u32 num_err_mbs;
  u32 is_idr;
  u32 is_tsa_ref;
  u32 tiled_mode;
  u32 cycles_per_mb;
  u32 ref_poc[MAX_DPB_SIZE + 1];
  u32 pic_struct;
  double dpb_output_time;
};

/* structure to represent display image output from the buffer */
struct DpbOutPicture {
  u32 mem_idx;
  struct DWLLinearMem *data;
#ifdef USE_EXTERNAL_BUFFER
  struct DWLLinearMem *pp_data;
#endif
  u32 pic_id;
  u32 decode_id;
  u32 num_err_mbs;
  u32 is_idr;
  u32 is_tsa_ref;
  u32 tiled_mode;
  u32 pic_width;
  u32 pic_height;
  u32 cycles_per_mb;
  struct HevcCropParams crop_params;
  u32 pic_struct;
  u32 bit_depth_luma;
  u32 bit_depth_chroma;
};

/* structure to represent DPB */
struct DpbStorage {
  struct DpbPicture buffer[MAX_DPB_SIZE + 1];
  u32 list[MAX_DPB_SIZE + 1];
  struct DpbPicture *current_out;
  u32 current_out_pos;
  double cpb_removal_time;
  u32 bumping_flag;
  struct DpbOutPicture *out_buf;
  u32 num_out;
  u32 out_index_w;
  u32 out_index_r;
  u32 max_ref_frames;
  u32 dpb_size;
  u32 real_size;
  u32 dpb_reset;

  u32 max_long_term_frame_idx;
  u32 num_ref_frames;
  u32 fullness;
  u32 num_out_pics_buffered;
  u32 prev_ref_frame_num;
  u32 last_contains_mmco5;
  u32 no_reordering;
  u32 flushed;
  u32 pic_size;
  u32 dir_mv_offset;
  struct DWLLinearMem poc;
  u32 delayed_out;
  u32 delayed_id;
  u32 interlaced;
  u32 ch2_offset;
  u32 cbs_tbl_offsety;
  u32 cbs_tbl_offsetc;
  u32 cbs_tbl_size;

  u32 tot_buffers;
  struct DWLLinearMem pic_buffers[MAX_FRAME_BUFFER_NUMBER];
  u32 pic_buff_id[MAX_FRAME_BUFFER_NUMBER];

  /* flag to prevent output when display smoothing is used and second field
   * of a picture was just decoded */
  u32 no_output;

  u32 prev_out_idx;

  i32 ref_poc_list[16]; /* TODO: check dimenstions */
  i32 poc_st_curr[16];
  i32 poc_st_foll[16];
  i32 poc_lt_curr[16];
  i32 poc_lt_foll[16];
  u32 ref_pic_set_st[16];
  u32 ref_pic_set_lt[16];
  u32 num_poc_st_curr;
  u32 num_poc_st_curr_before;
  u32 num_poc_st_foll;
  u32 num_poc_lt_curr;
  u32 num_poc_lt_foll;

  struct FrameBufferList *fb_list;
  u32 ref_id[16];
  u32 pic_width;
  u32 pic_height;
  u32 bit_depth_luma;
  u32 bit_depth_chroma;
  struct HevcCropParams crop_params;

#ifdef USE_EXTERNAL_BUFFER
  //u32 buffer_index;   /* Next buffer to be released or allocated. */
  struct Storage *storage;  /* TODO(min): temp use for raster buffer allocation. */
#endif
};

struct DpbInitParams {
  u32 pic_size;
  u32 buff_size;
  u32 dpb_size;
  u32 tbl_sizey;
  u32 tbl_sizec;
  u32 n_extra_frm_buffers;
  u32 no_reordering;
};

#ifndef USE_EXTERNAL_BUFFER
u32 HevcResetDpb(const void *dwl, struct DpbStorage *dpb,
                 struct DpbInitParams *dpb_params);
#else
u32 HevcResetDpb(const void *dec_inst, struct DpbStorage *dpb,
                 struct DpbInitParams *dpb_params);
#endif

u32 HevcSetRefPics(struct DpbStorage *dpb, struct SliceHeader *slice_header,
                   i32 pic_order_cnt, struct SeqParamSet *sps, u32 is_idr,
                   u32 is_cra, u32 hrd_present);

void *HevcAllocateDpbImage(struct DpbStorage *dpb, i32 pic_order_cnt,
                           i32 pic_order_cnt_lsb, u32 is_idr,
                           u32 current_pic_id, u32 is_tsa_ref);

u8 *HevcGetRefPicData(const struct DpbStorage *dpb, u32 index);

/*@null@*/ struct DpbOutPicture *HevcDpbOutputPicture(struct DpbStorage *dpb);

void HevcFlushDpb(struct DpbStorage *dpb);
#ifndef USE_EXTERNAL_BUFFER
void HevcFreeDpb(const void *dwl, struct DpbStorage *dpb);
#else
i32 HevcFreeDpb(const void *inst, struct DpbStorage *dpb);
i32 HevcFreeDpbExt(const void *dec_inst, struct DpbStorage *dpb);
void HevcDpbMarkOlderUnused(struct DpbStorage *dpb, i32 pic_order_cnt, u32 hrd_present);
void HevcEmptyDpb(const void *dec_inst, struct DpbStorage *dpb);
#endif
void HevcDpbUpdateOutputList(struct DpbStorage *dpb);
void HevcDpbCheckMaxLatency(struct DpbStorage *dpb, u32 max_latency);
u32 HevcDpbHrdBumping(struct DpbStorage *dpb);

#endif /* #ifdef HEVC_DPB_H_ */
