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

#ifndef VP8HWD_CONTAINER_H
#define VP8HWD_CONTAINER_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "deccfg.h"
#include "decppif.h"
#include "dwl.h"
#include "decapicommon.h"
#include "refbuffer.h"

#include "vp8decapi.h"
#include "vp8hwd_decoder.h"
#include "vp8hwd_bool.h"
#include "vp8hwd_error.h"
#include "vp8hwd_buffer_queue.h"


#include "fifo.h"

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

#define VP8DEC_UNINITIALIZED   0U
#define VP8DEC_INITIALIZED     1U
#define VP8DEC_NEW_HEADERS     3U
#define VP8DEC_DECODING        4U
#define VP8DEC_MIDDLE_OF_PIC   5U
#ifdef USE_EXTERNAL_BUFFER
#define VP8DEC_WAITING_BUFFER  6U
#endif

#define VP8DEC_MAX_PIXEL_AMOUNT 16370688
#define VP8DEC_MAX_SLICE_SIZE 4096
#define VP8DEC_MAX_CORES MAX_ASIC_CORES

#define VP8_UNDEFINED_BUFFER  VP8DEC_MAX_PIC_BUFFERS

typedef struct HwRdyCallbackArg_t {
  void *dec_cont;
  u32 core_id;
  u32 show_frame;
  u32 display_number;
  BufferQueue* bq;
  i32 index;            /* Buffer index of the output buffer. */
  i32 index_p;
  i32 index_a;
  i32 index_g;
  u8* p_ref_status;       /* Pointer to the reference status field. */
  const u8 *stream;    /* Input buffer virtual address. */
  void* p_user_data;      /* User data associated with input buffer. */
  VP8DecMCStreamConsumed *stream_consumed_callback;
  FifoInst fifo_out;    /* Output FIFO instance. */
  VP8DecPicture pic;    /* Information needed for outputting the frame. */
} HwRdyCallbackArg;

typedef struct {
  u32 *p_pic_buffer_y[VP8DEC_MAX_PIC_BUFFERS];
  addr_t pic_buffer_bus_addr_y[VP8DEC_MAX_PIC_BUFFERS];
  u32 *p_pic_buffer_c[VP8DEC_MAX_PIC_BUFFERS];
  addr_t pic_buffer_bus_addr_c[VP8DEC_MAX_PIC_BUFFERS];
} userMem_t;

/* asic interface */
typedef struct DecAsicBuffers {
  u32 width, height;
  u32 strides_used, custom_buffers;
  u32 luma_stride, chroma_stride;
  u32 luma_stride_pow2, chroma_stride_pow2;
  u32 chroma_buf_offset;
  u32 sync_mc_offset;

  struct DWLLinearMem prob_tbl[VP8DEC_MAX_CORES];
  struct DWLLinearMem segment_map[VP8DEC_MAX_CORES];
  struct DWLLinearMem *out_buffer;
  struct DWLLinearMem *prev_out_buffer;
  u32 display_index[VP8DEC_MAX_PIC_BUFFERS];
  u32 decode_id[VP8DEC_MAX_PIC_BUFFERS];

  /* Concurrent access to following picture arrays is controlled indirectly
   * through buffer queue. */
  struct DWLLinearMem pictures[VP8DEC_MAX_PIC_BUFFERS];
  struct DWLLinearMem pictures_c[VP8DEC_MAX_PIC_BUFFERS];  /* only for usermem */
  VP8DecPicture picture_info[VP8DEC_MAX_PIC_BUFFERS];
  u32 frame_width[VP8DEC_MAX_PIC_BUFFERS];
  u32 frame_height[VP8DEC_MAX_PIC_BUFFERS];
  u32 coded_width[VP8DEC_MAX_PIC_BUFFERS];
  u32 coded_height[VP8DEC_MAX_PIC_BUFFERS];
#ifdef USE_OUTPUT_RELEASE
  u32 not_displayed[VP8DEC_MAX_PIC_BUFFERS];
  u32 first_show[VP8DEC_MAX_PIC_BUFFERS];     /* Flag to indicate the repeating output frame */
#endif
  struct DWLLinearMem mvs[2];
  u32 mvs_curr;
  u32 mvs_ref;

  /* Indexes for picture buffers in pictures[] array */
  u32 out_buffer_i;
  u32 prev_out_buffer_i;

  u32 whole_pic_concealed;
  u32 disable_out_writing;
  u32 segment_map_size;
  u32 partition1_base;
  u32 partition1_bit_offset;
  u32 partition2_base;
  i32 dc_pred[2];
  i32 dc_match[2];

  userMem_t user_mem;
} DecAsicBuffers_t;

typedef struct VP8DecContainer {
  const void *checksum;
  u32 dec_mode;
  u32 dec_stat;
  u32 pic_number;
  u32 display_number;
  u32 asic_running;
  u32 width;
  u32 height;
  u32 vp8_regs[TOTAL_X170_REGISTERS];
  DecAsicBuffers_t asic_buff[1];
  const void *dwl;         /* DWL instance */
  i32 core_id;
  i32 segm_id;
  u32 num_cores;
  u32 ref_buf_support;
  struct refBuffer ref_buffer_ctrl;
  vp8_decoder_t decoder;
  vpBoolCoder_t bc;
  u32 combined_mode_used;

  struct {
    const void *pp_instance;
    void (*PPDecStart) (const void *, const DecPpInterface *);
    void (*PPDecWaitEnd) (const void *);
    void (*PPConfigQuery) (const void *, DecPpQuery *);
    DecPpInterface dec_pp_if;
    DecPpQuery pp_info;
  } pp;
#ifdef USE_EXTERNAL_BUFFER
  u32 next_buf_size;  /* size of the requested external buffer */
  u32 buf_num;        /* number of buffers (with size of next_buf_size) requested to be allocated externally */
  struct DWLLinearMem *buf_to_free;
  u32 buffer_index;
  u32 tot_buffers;
  u32 n_ext_buf_size;
  u32 no_reallocation;
#endif
  u32 use_adaptive_buffers;
  u32 n_guard_size;
#ifdef USE_OUTPUT_RELEASE
  u32 last_slice;
  u32 fullness;
  u32 abort;
#endif
  pthread_mutex_t protect_mutex;

  u32 picture_broken;
  u32 intra_freeze;
  u32 partial_freeze;
  u32 out_count;
  u32 ref_to_out;
  u32 pending_pic_to_pp;

  BufferQueue bq;
  u32 num_buffers;
  u32 num_buffers_reserved;

  u32 intra_only;
  u32 slice_concealment;
  u32 user_mem;
  u32 slice_height;
  u32 tot_decoded_rows;
  u32 output_rows;

  u32 tiled_mode_support;
  u32 tiled_reference_enable;

  u32 hw_ec_support;
  u32 stride_support;
  u32 conceal;
  u32 conceal_start_mb_x;
  u32 conceal_start_mb_y;
  u32 prev_is_key;
  u32 force_intra_freeze;
  u32 prob_refresh_detected;
  vp8ec_t ec;
  HwRdyCallbackArg hw_rdy_callback_arg[MAX_ASIC_CORES];

  /* Output related variables. */
  FifoInst fifo_out;  /* Fifo for output indices. */
  FifoInst fifo_display;  /* Store of output indices for display reordering. */

  /* Stream buffer callback specifics. */
  VP8DecMCStreamConsumed *stream_consumed_callback;
  void* p_user_data;
  const u8* stream;
  u32 no_decoding_buffer;
  u32 get_buffer_after_abort;
  u32 min_dec_pic_width;
  u32 min_dec_pic_height;
} VP8DecContainer_t;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

#endif /* #ifdef VP8HWD_CONTAINER_H */
