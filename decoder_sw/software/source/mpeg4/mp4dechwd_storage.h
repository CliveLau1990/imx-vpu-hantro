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

#ifndef DECSTRMSTORAGE_H_DEFINED
#define DECSTRMSTORAGE_H_DEFINED

#include "basetype.h"
#include "mp4deccfg.h"
#include "bqueue.h"

#define CUSTOM_STRM_0   (3)
#define CUSTOM_STRM_1   (4)
#define CUSTOM_STRM_2   (5)

#define MP4_MAX_BUFFERS 32

typedef struct {
  u32 data_index;
  struct DWLLinearMem *pp_data;
  u32 pic_type;
  u32 pic_id;
  u32 is_inter;
  MP4DecRet ret_val;
  u32 nbr_err_mbs;
  u32 send_to_pp;
  u32 tiled_mode;
  MP4DecTime time_code;
#ifdef USE_OUTPUT_RELEASE
  u32 first_show;
#endif
  DecVopDesc VopDesc; /* Frame description */
  DecHdrs Hdrs;
} picture_t;

typedef struct {
  u8  quant_mat[64*2];
  u32 status; /* status of syncronization */
  u32 strm_dec_ready;

  u32 resync_marker_length;
  u32 vp_mb_number;
  u32 vp_num_mbs;
  u32 vp_first_coded_mb;
  u32 q_p;
  u32 prev_qp;
  u32 vp_qp;
  u32 skip_b;

  u32 short_video;
  u32 mpeg4_video; /* Sequence contains mpeg-4 headers: either mpeg-4 or
                       mpeg-4 short video */
  u32 gob_resync_flag;

  const u8 *p_last_sync;
  /* pointer to stream buffer right after motion vectors of an intra macro
   * block. Needed to avoid decoding motion vectors twice in case decoder
   * runs out of rlc buffer (decoding twice means using results of previous
   * decoding in motion vector differential decoding) */
  u8 *p_strm_after_mv;
  u32 bit_pos_after_mv;
  u32 read_bits_after_mv;

  u32 start_code_loss;
  u32 valid_vop_header;

  /* to be added to TicsFromPrev (to accommodate modulo time base changes
   * caused by GOV time codes) */
  u32 gov_time_increment;

  u32 num_err_mbs;

  /* 6 lsbs represent status for each block, msb high if mb not coded */
  u8 coded_bits[MP4API_DEC_MBS];

  struct DWLLinearMem quant_mat_linear;
  struct DWLLinearMem direct_mvs;
  struct DWLLinearMem pp_buffer[MP4_MAX_BUFFERS];
  u32 release_buffer;
  u32 ext_buffer_added;

  picture_t p_pic_buf[MP4_MAX_BUFFERS+1];  /* One more element's space is alloced to avoid the overflow */
  struct DWLLinearMem data[MP4_MAX_BUFFERS];
  u32 out_buf[MP4_MAX_BUFFERS];
#ifdef USE_OUTPUT_RELEASE
  MP4DecPicture picture_info[MP4_MAX_BUFFERS*2];  /* take top/bottom fields into consideration */
#endif
  u32 out_index;
  u32 out_count;
  u32 work_out;
  u32 work_out_prev;
  u32 work0;
  u32 work1;
  u32 latest_id; /* current pic id, used for debug */
  u32 previous_not_coded;
  u32 previous_b;
  u32 sorenson_spark;
  u32 sorenson_ver;
  u32 disposable; /* sorenson */
  u32 custom_strm_ver;
  u32 custom_strm_headers;
  u32 custom_idct;
  u32 custom_overfill;
  u32 unsupported_features_present;

  /* these are used to check if re-initialization is needed */
  u32 low_delay;
  u32 interlaced;
  u32 video_object_layer_width;
  u32 video_object_layer_height;

  u8  last_packet_byte; /* last byte of last decoded packet. used to check
                         * against clumsily stuffed short video end markers */
  u32 intra_freeze;
  u32 partial_freeze;
  u32 picture_broken;
  u32 previous_mode_full;

  u32 prev_bidx;
  u32 max_num_buffers;
  u32 num_buffers;
  u32 num_pp_buffers;
  struct BufferQueue bq;
  struct BufferQueue bq_pp; /* for multi-buffer PP */

  u32 parallel_mode2;
  u32 pm2lock_buf;
  u32 pm2_all_processed_flag;
  u32 pm2_start_point;

  u32 reload_quant_matrices;
} DecStrmStorage;

#endif
