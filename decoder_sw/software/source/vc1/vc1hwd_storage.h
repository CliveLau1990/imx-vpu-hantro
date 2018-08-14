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

#ifndef VC1HWD_STORAGE_H
#define VC1HWD_STORAGE_H

/*------------------------------------------------------------------------------
    Include headers
------------------------------------------------------------------------------*/

#include "vc1hwd_picture_layer.h"
#include "vc1hwd_picture.h"
#include "vc1decapi.h"
#include "bqueue.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

#define     MB_SKIPPED          4
#define     MB_4MV              2
#define     MB_AC_PRED          4
#define     MB_DIRECT           2
#define     MB_FIELD_TX         1
#define     MB_OVERLAP_SMOOTH   2
#define     MB_FORWARD_MB       4

#define     MAX_OUTPUT_PICS    (16)

typedef enum {
  /* Headers */
  HDR_SEQ = 1,
  HDR_ENTRY = 2,
  /* Combinations */
  HDR_BOTH = HDR_SEQ | HDR_ENTRY
} hdr_e;

/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/

typedef struct vc1DecPpBuffer_s {
  u32 dec;
  u32 pp;
  u32 processed;
  u32 frame_pic;
  u32 b_frame;
} vc1DecPpBuffer_t;

typedef struct vc1DecPp_s {
  vc1DecPpBuffer_t dec_out;
  vc1DecPpBuffer_t pp_proc;
  vc1DecPpBuffer_t anchor;
  vc1DecPpBuffer_t pp_out;

  u32         anchor_pp_idx;

  u32 field_output;
  struct BufferQueue *bq_pp;
} vc1DecPp_t;

typedef struct swStrmStorage {
  u16x max_coded_width;         /* Maximum horizontal size in pixels */
  u16x max_coded_height;        /* Maximum vertical size in pixels */
  u16x cur_coded_width;
  u16x cur_coded_height;

  u16x pic_width_in_mbs;
  u16x pic_height_in_mbs;
  u16x num_of_mbs;
  u16x prev_num_mbs;

  u16x loop_filter;            /* Is loop filter used */
  u16x multi_res;              /* Can resolution change */
  u16x fast_uv_mc;              /* Fast motion compensation */
  u16x extended_mv;            /* Extended motion vectors used */
  u16x max_bframes;            /* Maximum number of B frames [0,7] */
  u16x dquant;                /* Can quantization step vary */
  u16x range_red;              /* Is range reduction used */

  u16x vs_transform;           /* Variable sized transform */
  u16x overlap;               /* Overlap smooting enabled */
  u16x sync_marker;            /* Sync markers present */
  u16x frame_interp_flag;       /* Is frame interpolation hints present */
  u16x quantizer;             /* Quantizer used for the sequence */

  picture_t* p_pic_buf;         /* Picture descriptors for required
                                 * reference frames and work buffers */
  struct DWLLinearMem pp_buffer[16];
  u32 release_buffer;
  u32 ext_buffer_added;

  u16x outp_buf[MAX_OUTPUT_PICS];
  u16x out_pic_id[2][MAX_OUTPUT_PICS];
  u16x out_err_mbs[MAX_OUTPUT_PICS];
#ifdef USE_OUTPUT_RELEASE
  VC1DecPicture picture_info[MAX_OUTPUT_PICS*2];  /* take top/bottom fields into consideration */
#endif
  u32 field_to_return;          /* 0 = First, 1 second */
  u16x outp_idx;
  u16x prev_outp_idx;
  u16x outp_count;
  u32 min_count;               /* for vc1hwdNextPicture */
  u32 field_count;

  u32 max_num_buffers;
  u32 num_pp_buffers;
  u16x work_buf_amount;         /* Amount of work buffers */
  u16x work_out;               /* Index for output */
  u16x work_out_prev;           /* Index for previous output */
  u16x work0;                 /* Index for last anchor frame */
  u16x work1;                 /* Index for previous to last anchor frame */
  u16x prev_bidx;

  pictureLayer_t pic_layer;

  u16x rnd;

  u8 *p_mb_flags;

  /* Sequence layer */
  u32 profile;
  u32 level;
  u32 color_diff_format;        /* color-difference/luma format (1 = 4:2:0) */
  u32 frmrtq_post_proc;
  u32 bitrtq_post_proc;
  u32 post_proc_flag;
  u32 pull_down;
  u32 interlace;
  u32 tfcntr_flag;
  u32 finterp_flag;
  u32 psf;
  u32 disp_horiz_size;
  u32 disp_vert_size;
  u32 aspect_horiz_size;
  u32 aspect_vert_size;

  u32 frame_rate_flag;
  u32 frame_rate_ind;
  u32 frame_rate_nr;
  u32 frame_rate_dr;

  u32 color_format_flag;
  u32 color_prim;
  u32 transfer_char;
  u32 matrix_coef;

  u32 hrd_param_flag;
  u32 hrd_num_leaky_buckets;
  u32 bit_rate_exponent;
  u32 buffer_size_exponent;
  u32* hrd_rate;
  u32* hrd_buffer;

  /* entry-point */
  u32 broken_link;
  u32 closed_entry;
  u32 pan_scan_flag;
  u32 ref_dist_flag;
  u32* hrd_fullness;
  u32 extended_dmv;

  u32 range_map_yflag;
  u32 range_map_y;
  u32 range_map_uv_flag;
  u32 range_map_uv;

  u32 anchor_inter[2]; /* [0] top field / frame, [1] bottom field */

  u32 skip_b;
  u32 resolution_changed;
  strmData_t tmp_strm_data;
  u32 prev_dec_result;
  u32 slice;
  u32 first_frame;
  u32 ff_start;        /* picture layer of the first field processed */
  u32 missing_field;

  hdr_e hdrs_decoded;  /* Contains info of decoded headers */
  u32 picture_broken;
  u32 intra_freeze;
  u32 partial_freeze;
  u32 previous_b;
  u32 previous_mode_full;

  u32 keep_hw_reserved;

  struct BufferQueue bq;
  struct BufferQueue bq_pp;

  u32 parallel_mode2;
  u32 pm2lock_buf;
  u32 pm2_all_processed_flag;
  u32 pm2_start_point;
  vc1DecPp_t dec_pp;
  void *dec_cont;

  u32 min_coded_width;
  u32 min_coded_height;
} swStrmStorage_t;

/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

#endif /* #ifndef VC1HWD_STORAGE_H */

