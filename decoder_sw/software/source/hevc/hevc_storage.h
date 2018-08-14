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

#ifndef HEVC_STORAGE_H_
#define HEVC_STORAGE_H_

#include "basetype.h"
#include "hevc_cfg.h"
#include "hevc_seq_param_set.h"
#include "hevc_pic_param_set.h"
#include "hevc_video_param_set.h"
#include "hevc_nal_unit.h"
#include "hevc_slice_header.h"
#include "hevc_seq_param_set.h"
#include "hevc_dpb.h"
#include "hevc_pic_order_cnt.h"
#include "raster_buffer_mgr.h"
#include "hevc_sei.h"

/* structure to store parameters needed for access unit boundary checking */
struct AubCheck {
  struct NalUnit nu_prev[1];
  u32 prev_idr_pic_id;
  u32 prev_pic_order_cnt_lsb;
  u32 first_call_flag;
  u32 new_picture;
};

/* storage data structure, holds all data of a decoder instance */
struct Storage {
  /* active paramet set ids and pointers */
  u32 old_sps_id;
  u32 active_pps_id;
  u32 active_sps_id;
  u32 active_vps_id;
  struct PicParamSet *active_pps;
  struct SeqParamSet *active_sps;
  struct VideoParamSet *active_vps;
  struct SeqParamSet *sps[MAX_NUM_SEQ_PARAM_SETS];
  struct PicParamSet *pps[MAX_NUM_PIC_PARAM_SETS];
  struct VideoParamSet *vps[MAX_NUM_VIDEO_PARAM_SETS];

  struct SEIParameters sei_param;
  u32 pic_width_in_cbs;
  u32 pic_height_in_cbs;
  u32 pic_width_in_ctbs;
  u32 pic_height_in_ctbs;
  u32 pic_size_in_ctbs;
  u32 pic_size;
  double frame_rate;

  u32 pic_started;

  /* flag to indicate if current access unit contains any valid slices */
  u32 valid_slice_in_access_unit;

  /* pic_id given by application */
  u32 current_pic_id;

  /* flag to store no_output_reordering flag set by the application */
  u32 no_reordering;

  /* pointer to DPB */
  struct DpbStorage dpb[2];

  /* structure to store picture order count related information */
  struct PocStorage poc[2];

  /* access unit boundary checking related data */
  struct AubCheck aub[1];

  /* current processed image */
  struct Image curr_image[1];

  /* last valid NAL unit header is stored here */
  struct NalUnit prev_nal_unit[1];

  /* slice header, second structure used as a temporary storage while
   * decoding slice header, first one stores last successfully decoded
   * slice header */
  struct SliceHeader slice_header[2];

  /* fields to store old stream buffer pointers, needed when only part of
   * a stream buffer is processed by HevcDecode function */
  u32 prev_buf_not_finished;
  const u8 *prev_buf_pointer;
  u32 prev_bytes_consumed;
  struct StrmData strm[1];

  u32 checked_aub;        /* signal that AUB was checked already */
  u32 prev_idr_pic_ready; /* for FFWD workaround */

  u32 intra_freeze;
  u32 picture_broken;

  i32 poc_last_display;

  u32 dmv_mem_size;
  u32 n_extra_frm_buffers;
  const struct DpbOutPicture *pending_out_pic;

  u32 no_rasl_output;

  u32 raster_enabled;
  RasterBufferMgr raster_buffer_mgr;
  u32 down_scale_enabled;
  u32 down_scale_x_shift;
  u32 down_scale_y_shift;

  u32 use_p010_output;
  u32 use_8bits_output;
  u32 use_video_compressor;
#ifdef USE_FAST_EC
  u32 fast_freeze;
#endif
};

void HevcInitStorage(struct Storage *storage);
void HevcResetStorage(struct Storage *storage);
u32 HevcIsStartOfPicture(struct Storage *storage);
u32 HevcStoreSeqParamSet(struct Storage *storage,
                         struct SeqParamSet *seq_param_set);
u32 HevcStorePicParamSet(struct Storage *storage,
                         struct PicParamSet *pic_param_set);
u32 HevcStoreVideoParamSet(struct Storage *storage,
                           struct VideoParamSet *video_param_set);
u32 HevcActivateParamSets(struct Storage *storage, u32 pps_id, u32 is_idr);

u32 HevcCheckAccessUnitBoundary(struct StrmData *strm, struct NalUnit *nu_next,
                                struct Storage *storage,
                                u32 *access_unit_boundary_flag);

u32 HevcValidParamSets(struct Storage *storage);
void HevcClearStorage(struct Storage *storage);

u32 HevcStoreSEIInfoForCurrentPic(struct Storage *storage);

#ifndef USE_EXTERNAL_BUFFER
u32 HevcAllocateSwResources(const void *dwl, struct Storage *storage);
#else
u32 IsExternalBuffersRealloc(void *dec_inst, struct Storage *storage);
void HevcSetExternalBufferInfo(void *dec_inst, struct Storage *storage);
u32 HevcAllocateSwResources(const void *dwl, struct Storage *storage, void *dec_inst);
#endif
#endif /* #ifdef HEVC_STORAGE_H_ */
