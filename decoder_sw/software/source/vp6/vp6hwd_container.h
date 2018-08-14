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

#ifndef VP6HWD_CONTAINER_H
#define VP6HWD_CONTAINER_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "deccfg.h"
#include "decppif.h"
#include "dwl.h"
#include "vp6dec.h"
#include "refbuffer.h"
#include "bqueue.h"

#ifdef USE_OUTPUT_RELEASE
#include "fifo.h"
#include "vp6decapi.h"
#endif
/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

#define VP6DEC_UNINITIALIZED   0U
#define VP6DEC_INITIALIZED     1U
#define VP6DEC_BUFFER_EMPTY    2U
#define VP6DEC_NEW_HEADERS     3U
#ifdef USE_EXTERNAL_BUFFER
#define VP6DEC_WAITING_BUFFER  4U
#endif

/* asic interface */
typedef struct DecAsicBuffers {
  struct DWLLinearMem prob_tbl;
  struct DWLLinearMem *out_buffer;
  struct DWLLinearMem *prev_out_buffer;
  struct DWLLinearMem *refBuffer;
  struct DWLLinearMem *golden_buffer;
  struct DWLLinearMem pictures[16];
  u32 decode_id[16];

#ifdef USE_OUTPUT_RELEASE
  VP6DecPicture picture_info[16];
  u32 first_show[16];
#endif
  u32 frame_width[16];
  u32 frame_height[16];
  /* Indexes for picture buffers in pictures[] array */
  u32 out_buffer_i;
  u32 prev_out_buffer_i;
  u32 ref_buffer_i;
  u32 golden_buffer_i;

  u32 width;
  u32 height;
  u32 whole_pic_concealed;
  u32 disable_out_writing;
  addr_t partition1_base;
  u32 partition1_bit_offset;
  addr_t partition2_base;
} DecAsicBuffers_t;

typedef struct VP6DecContainer {
  const void *checksum;
  u32 dec_stat;
  u32 pic_number;
  u32 asic_running;
  u32 width;
  u32 height;
  u32 vp6_regs[TOTAL_X170_REGISTERS];
  DecAsicBuffers_t asic_buff[1];
  const void *dwl;         /* DWL instance */
  i32 core_id;
  u32 ref_buf_support;
  u32 tiled_mode_support;
  u32 tiled_reference_enable;
  struct refBuffer ref_buffer_ctrl;

  PB_INSTANCE pb;          /* SW decoder instance */

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
  u32 tot_buffers_added;
  u32 n_ext_buf_size;        // size of external buffers added
  u32 no_reallocation;
#endif
  u32 use_adaptive_buffers;
  u32 n_guard_size;
#ifdef USE_OUTPUT_RELEASE
  u32 fullness;
  FifoInst fifo_display;
  u32 abort;
#endif

  u32 picture_broken;
  u32 intra_freeze;
  u32 partial_freeze;
  u32 ref_to_out;
  u32 out_count;
  pthread_mutex_t protect_mutex;

  u32 num_buffers;
  u32 num_buffers_reserved;
  struct BufferQueue bq;

  u32 get_buffer_after_abort;
  u32 no_decoding_buffer;
  u32 min_dec_pic_width;
  u32 min_dec_pic_height;
} VP6DecContainer_t;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

#endif /* #ifdef VP6HWD_CONTAINER_H */
