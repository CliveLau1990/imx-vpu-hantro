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

#ifndef RV_STRMSTORAGE_H
#define RV_STRMSTORAGE_H

#include "basetype.h"
#include "rv_cfg.h"
#include "bqueue.h"

typedef struct {
  struct DWLLinearMem data;
  struct DWLLinearMem *pp_data;
  u32 pic_type;
  u32 pic_id;
  u32 decode_id;
  u32 pic_code_type;
  u32 tf;
  u32 ff[2];
  u32 rff;
  u32 rfc;
  u32 is_inter;
  u32 nbr_err_mbs;
  RvDecRet ret_val;
  u32 send_to_pp;
  u32 frame_width, frame_height;
  u32 coded_width, coded_height;
  u32 tiled_mode;
#ifdef USE_OUTPUT_RELEASE
  u32 first_show;
#endif
} picture_t;

typedef struct {
  u32 status;
  u32 strm_dec_ready;

  picture_t p_pic_buf[16+1];   /* One more element's space is alloced to avoid the overflow */
  picture_t p_rpr_buf;
  u32 out_buf[16];
  struct DWLLinearMem pp_buffer[16];
  u32 release_buffer;
  u32 ext_buffer_added;
#ifdef USE_OUTPUT_RELEASE
  RvDecPicture picture_info[32];          /* take top/bottom fields into consideration */
#endif
  u32 out_index;
  u32 out_count;
  u32 work_out;
  u32 work0;
  u32 work1;
  u32 latest_id;   /* current pic id, used for debug */
  u32 skip_b;
  u32 prev_pic_coding_type;

  u32 picture_broken;
  u32 intra_freeze;
  u32 partial_freeze;
  u32 rpr_detected;
  u32 rpr_next_pic_type;
  u32 previous_b;
  u32 previous_mode_full;

  u32 is_rv8;
  u32 fwd_scale;
  u32 bwd_scale;
  u32 tr;
  u32 prev_tr;
  u32 trb;
  struct DWLLinearMem vlc_tables;
  struct DWLLinearMem direct_mvs;
  struct DWLLinearMem rpr_work_buffer;
  struct DWLLinearMem slices;

  u32 frame_code_length;
  u32 frame_sizes[2*9];
  u32 max_frame_width, max_frame_height;
  u32 max_mbs_per_frame;

  u32 num_slices;
  u32 raw_mode;

  /* to store number of bits needed to indicate rv9 frame size */
  u32 frame_size_bits;

  /* used to compute timestamps for output pictures */
  u32 pic_id;
  u32 prev_pic_id;

  u32 prev_bidx;
  struct BufferQueue bq;
  struct BufferQueue bq_pp;
  u32 max_num_buffers;
  u32 num_buffers;
  u32 num_pp_buffers;

} DecStrmStorage;

#endif /* #ifndef RV_STRMSTORAGE_H */
