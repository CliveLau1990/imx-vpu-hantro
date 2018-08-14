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

#ifndef MPEG2DECSTRMSTORAGE_H_DEFINED
#define MPEG2DECSTRMSTORAGE_H_DEFINED

#include "basetype.h"
#include "mpeg2hwd_cfg.h"
#include "bqueue.h"

#define MPEG2_MAX_BUFFERS 32

typedef struct {
  struct DWLLinearMem data;
  struct DWLLinearMem *pp_data;
  u32 pic_type;
  u32 pic_id;
  u32 pic_code_type[2];
  u32 tf;
  u32 ff[2];
  u32 rff;
  u32 rfc;
  u32 sf;       /* single field in buffer */
  u32 ps;       /* picture structure */
  u32 is_inter;
  u32 nbr_err_mbs;
  Mpeg2DecRet ret_val;
  u32 send_to_pp;
  Mpeg2DecTime time_code;
  u32 tiled_mode;
#ifdef USE_OUTPUT_RELEASE
  u32 first_show;
#endif
  DecFrameDesc FrameDesc; /* Frame description */
  DecHdrs Hdrs;
  enum DecDpbMode dpb_mode;
} picture_t;

typedef struct {
  u32 status;
  u32 strm_dec_ready;

  u32 valid_pic_header;
  u32 valid_pic_ext_header;
  u32 valid_sequence;
  u32 vp_qp;
  u32 error_in_hdr;

  u32 max_num_buffers;
  u32 num_buffers;
  u32 num_pp_buffers;
  picture_t p_pic_buf[MPEG2_MAX_BUFFERS+1];  /* One more element's space is alloced to avoid the overflow */
  struct DWLLinearMem pp_buffer[MPEG2_MAX_BUFFERS];
  u32 release_buffer;
  u32 ext_buffer_added;
  u32 out_buf[MPEG2_MAX_BUFFERS];
#ifdef USE_OUTPUT_RELEASE
  Mpeg2DecPicture picture_info[MPEG2_MAX_BUFFERS*2];  /* take top/bottom fields into consideration */
#endif
  u32 out_index;
  u32 out_count;
  u32 work_out;
  u32 work0;
  u32 work1;
  u32 work_out_prev;
  u32 work_out_field;  /* Single field output index */
  u32 latest_id;   /* current pic id, used for debug */
  u32 skip_b;
  u32 prev_pic_coding_type;
  u32 prev_pic_structure;
  u32 field_to_return;  /* 0 = First, 1 second */

  u32 picture_broken;
  u32 intra_freeze;
  u32 new_headers_change_resolution;
  u32 previous_b;
  u32 previous_mode_full;

  u32 prev_bidx;

  struct BufferQueue bq;
  struct BufferQueue bq_pp;

  u32 parallel_mode2;
  u32 pm2lock_buf; /* "Locked" PP buffer, i.e. future output frame
                     * that shouldn't be overwritten. */
  u32 pm2_all_processed_flag;
  u32 pm2_start_point;
  u32 last_bskipped;

  u32 partial_freeze;
} DecStrmStorage;

#endif /* #ifndef MPEG2DECSTRMSTORAGE_H_DEFINED */
