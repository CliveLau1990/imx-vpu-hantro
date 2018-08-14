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

#ifndef HEVC_CONTAINER_H_
#define HEVC_CONTAINER_H_

#include "basetype.h"
#include "hevc_storage.h"
#include "hevc_util.h"
#include "hevc_fb_mngr.h"
#include "deccfg.h"
#include "decppif.h"
#include "dwl.h"
#include "input_queue.h"

#ifdef USE_EXTERNAL_BUFFER
#include "raster_buffer_mgr.h"
#endif

#ifdef USE_RANDOM_TEST
#include <stdio.h>
struct ErrorParams {
  u32 seed;
  u8 truncate_stream;
  char truncate_stream_odds[24];
  u8 swap_bits_in_stream;
  char swap_bit_odds[24];
  u8 lose_packets;
  char packet_loss_odds[24];
  u32 random_error_enabled;
};
#endif

#ifdef USE_EXTERNAL_BUFFER
enum BufferType {
  REFERENCE_BUFFER = 0, /* reference + compression table + DMV*/
  RASTERSCAN_OUT_BUFFER,
  DOWNSCALE_OUT_BUFFER,
  TILE_EDGE_BUFFER,  /* filter mem + bsd control mem */
  SEGMENT_MAP_BUFFER, /* segment map */
  MISC_LINEAR_BUFFER, /* tile info + prob table + entropy context counter */
  BUFFER_TYPE_NUM
};

#define IS_EXTERNAL_BUFFER(config, type) (((config) >> (type)) & 1)
#endif

/* asic interface */
struct HevcDecAsic {
  struct DWLLinearMem *out_buffer;
#ifndef USE_EXTERNAL_BUFFER
  struct DWLLinearMem scaling_lists;
  struct DWLLinearMem tile_info;
  struct DWLLinearMem filter_mem;
  struct DWLLinearMem sao_mem;
  struct DWLLinearMem bsd_control_mem;
#else
  struct DWLLinearMem misc_linear;
  struct DWLLinearMem tile_edge;
  struct DWLLinearMem *out_pp_buffer;
  u32 scaling_lists_offset;
  u32 tile_info_offset;
#ifdef USE_FAKE_RFC_TABLE
  u32 fake_table_offset;
  u32 fake_tbly_size;
  u32 fake_tblc_size;
#endif
  u32 filter_mem_offset;
  u32 sao_mem_offset;
  u32 bsd_control_mem_offset;
#endif
  i32 chroma_qp_index_offset;
  i32 chroma_qp_index_offset2;
  u32 disable_out_writing;
  u32 asic_id;
};

struct HevcDecContainer {
  const void *checksum;

  enum {
    HEVCDEC_UNINITIALIZED,
    HEVCDEC_INITIALIZED,
    HEVCDEC_BUFFER_EMPTY,
#ifdef USE_EXTERNAL_BUFFER
    HEVCDEC_WAITING_FOR_BUFFER,
    HEVCDEC_ABORTED,
#endif
    HEVCDEC_NEW_HEADERS,
    HEVCDEC_EOS
  } dec_state;

  i32 core_id;
  u32 max_dec_pic_width;
  u32 max_dec_pic_height;
  u32 dpb_mode;
  u32 output_format;
  u32 asic_running;
  u32 start_code_detected;

  u32 pic_number;
  const u8 *hw_stream_start;
  const u8 *hw_buffer;
  addr_t hw_stream_start_bus;
  addr_t hw_buffer_start_bus;
  u32 hw_buffer_length;
  u32 hw_bit_pos;
  u32 hw_length;
  u32 stream_pos_updated;
  u32 packet_decoded;

  u32 down_scale_enabled;
  u32 down_scale_x;
  u32 down_scale_y;

  u32 hevc_main10_support;
  u32 use_8bits_output;
  u32 use_ringbuffer;
  u32 use_fetch_one_pic;
  u32 use_video_compressor;
  u32 use_p010_output;
  enum DecPicturePixelFormat pixel_format;

  const void *dwl; /* DWL instance */
  struct FrameBufferList fb_list;

  struct Storage storage;
  struct HevcDecAsic asic_buff[1];
  u32 hevc_regs[DEC_X170_REGISTERS];

#ifdef USE_EXTERNAL_BUFFER
  u32 ext_buffer_config;  /* Bit map config for external buffers. */
  u32 use_adaptive_buffers;
  u32 guard_size;
  u32 ext_buffer_size;    /* size of external buffers allocated already */
  u32 reset_ext_buffer;   /* Is it necessary to reset external buffers? */
  u32 reset_dpb_done;

  u32 buffer_num_requested; /* total buffers requested */
  u32 buffer_num_added;     /* num of buffers already added to decoder */
  u32 min_buffer_num;       /* minimum num of buffers needed */

  u32 next_buf_size;  /* size of the requested external buffer */
  u32 buf_num;        /* number of buffers (with size of next_buf_size) requested to be allocated externally */
  struct DWLLinearMem _buf_to_free; /* For internal temp use, holding the info of linear mem to be released. */
  struct DWLLinearMem *buf_to_free;
  enum BufferType buf_type;
  u32 buffer_index;

  u32 resource_ready;
  struct DWLLinearMem tiled_buffers[MAX_FRAME_BUFFER_NUMBER];
  struct RasterBufferParams params;
  u32 rbm_release;    // rbm release not done, used to free old raster buffer before reallocating.
#ifdef ASIC_TRACE_SUPPORT
  u32 is_frame_buffer;  /* Whether it's a frame buffer (reference/raster scan/down scale, which will be allocated by DWLMallocRefFrm */
#endif
  u32 abort;
#endif
  pthread_mutex_t protect_mutex;

#ifdef USE_RANDOM_TEST
  struct ErrorParams error_params;
  u32 stream_not_consumed;
  u32 prev_input_len;
  FILE *ferror_stream;
#endif

  u32 legacy_regs;  /* Legacy registers. */
  u32 secure_mode;
};

#endif /* #ifdef HEVC_CONTAINER_H_ */
