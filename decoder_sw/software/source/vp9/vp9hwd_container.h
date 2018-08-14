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

#ifndef VP9HWD_CONTAINER_H
#define VP9HWD_CONTAINER_H

#include "basetype.h"
#include "commonvp9.h"
#include "deccfg.h"
#include "decppif.h"
#include "dwl.h"
#include "fifo.h"
#include "sw_debug.h"
#include "vp9decapi.h"
#include "vp9hwd_bool.h"
#include "vp9hwd_buffer_queue.h"
#include "vp9hwd_decoder.h"
#include "input_queue.h"

#define VP9DEC_UNINITIALIZED 0U
#define VP9DEC_INITIALIZED 1U
#define VP9DEC_NEW_HEADERS 3U
#define VP9DEC_DECODING 4U
#define VP9DEC_END_OF_STREAM 5U
#ifdef USE_EXTERNAL_BUFFER
#define VP9DEC_WAITING_FOR_BUFFER 6U
#endif

#define VP9DEC_MAX_PIC_BUFFERS 32
#define VP9DEC_DYNAMIC_PIC_LIMIT 10

#define VP9_UNDEFINED_BUFFER (i32)(-1)

#define EMPTY_MARKER -5

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

typedef struct {
  unsigned char signature[4];  //='DKIF';
  unsigned short version;      //= 0;
  unsigned short headersize;   //= 32;
  unsigned int FourCC;
  unsigned short width;
  unsigned short height;
  unsigned int rate;
  unsigned int scale;
  unsigned int length;
  unsigned char unused[4];
} IVF_HEADER;
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

struct PicCallbackArg {
  u32 display_number;
  i32 index; /* Buffer index of the output buffer. */
  i32 index_a;
  i32 index_g;
  i32 index_p;
  u32 show_frame;
  FifoInst fifo_out;        /* Output FIFO instance. */
  struct Vp9DecPicture pic; /* Information needed for outputting the frame. */
};

/* asic interface */
struct DecAsicBuffers {
  u32 width, height;

#ifndef USE_EXTERNAL_BUFFER
  struct DWLLinearMem prob_tbl;
  struct DWLLinearMem ctx_counters;
  struct DWLLinearMem segment_map[2];
  struct DWLLinearMem tile_info;
  struct DWLLinearMem filter_mem;
  struct DWLLinearMem bsd_control_mem;
#else
  struct DWLLinearMem tile_edge;
  struct DWLLinearMem misc_linear;
  struct DWLLinearMem segment_map;
  u32 prob_tbl_offset;
  u32 ctx_counters_offset;
  u32 segment_map_offset[2];
  u32 tile_info_offset;
  u32 filter_mem_offset;
  u32 bsd_control_mem_offset;
  u32 segment_map_size_new;
#endif
  u32 display_index[VP9DEC_MAX_PIC_BUFFERS];

  /* Concurrent access to following picture arrays is controlled indirectly
   * through buffer queue. */
#ifndef USE_EXTERNAL_BUFFER
  struct DWLLinearMem pictures[VP9DEC_MAX_PIC_BUFFERS];
  struct DWLLinearMem pictures_c[VP9DEC_MAX_PIC_BUFFERS];
  struct DWLLinearMem dir_mvs[VP9DEC_MAX_PIC_BUFFERS]; /* colocated MVs */
  struct DWLLinearMem pp_luma[VP9DEC_MAX_PIC_BUFFERS];
  struct DWLLinearMem pp_chroma[VP9DEC_MAX_PIC_BUFFERS];
  struct DWLLinearMem cbs_luma_table[VP9DEC_MAX_PIC_BUFFERS];
  struct DWLLinearMem cbs_chroma_table[VP9DEC_MAX_PIC_BUFFERS];

#else

  struct DWLLinearMem pictures[VP9DEC_MAX_PIC_BUFFERS];
//  struct DWLLinearMem dir_mvs[VP9DEC_MAX_PIC_BUFFERS]; /* colocated MVs */
  struct DWLLinearMem pp_pictures[VP9DEC_MAX_PIC_BUFFERS];
//  struct DWLLinearMem cbs_table[VP9DEC_MAX_PIC_BUFFERS];
  u32 pictures_c_offset[VP9DEC_MAX_PIC_BUFFERS];
  u32 dir_mvs_offset[VP9DEC_MAX_PIC_BUFFERS];
  u32 pp_c_offset[VP9DEC_MAX_PIC_BUFFERS];
  u32 cbs_y_tbl_offset[VP9DEC_MAX_PIC_BUFFERS];
  u32 cbs_c_tbl_offset[VP9DEC_MAX_PIC_BUFFERS];
  u32 picture_size;
  u32 pp_size;
  i32 pp_buffer_map[VP9DEC_MAX_PIC_BUFFERS];  /* reference buffer -> rs/ds buffer */

  u32 realloc_out_buffer;   /* 1 indidates there is a pending out buffer hold.*/
  u32 realloc_seg_map_buffer; /* 1 indidates a segment map is being reallocated. */
  u32 realloc_tile_edge_mem;  /* 1 indicates we are allocating/reallocating a tile edge buffer. */
#endif
  struct Vp9DecPicture picture_info[VP9DEC_MAX_PIC_BUFFERS];
  i32 first_show[VP9DEC_MAX_PIC_BUFFERS];
  i32 reference_list[VP9_REF_LIST_SIZE]; /* Contains indexes to full list of
                                            picture */

  /* Indexes for picture buffers in pictures[] array */
  i32 out_buffer_i;
  i32 prev_out_buffer_i;

#ifdef USE_EXTERNAL_BUFFER
  /* Indexes for picture buffers in raster[]/dscale[] array */
  i32 out_pp_buffer_i;
#endif

  u32 whole_pic_concealed;
  u32 disable_out_writing;
  u32 segment_map_size;
  u32 partition1_base;
  u32 partition1_bit_offset;
  u32 partition2_base;
};

struct Vp9DecContainer {
  const void *checksum;
  u32 dec_mode;
  u32 dec_stat;
  u32 pic_number;
  u32 asic_running;
  u32 width;
  u32 height;
  u32 vp9_regs[DEC_X170_REGISTERS];
  struct DecAsicBuffers asic_buff[1];
  const void *dwl; /* struct DWL instance */
  i32 core_id;

  struct Vp9Decoder decoder;
  struct VpBoolCoder bc;

  u32 picture_broken;
  u32 intra_freeze;
  u32 out_count;
  u32 num_buffers;
  u32 num_buffers_reserved;
  u32 active_segment_map;
  u32 n_extra_frm_buffers;

  BufferQueue bq;
#ifdef USE_EXTERNAL_BUFFER
  u32 num_pp_buffers;
  BufferQueue pp_bq;  /* raster/dscale buffer queue for free raster output buffer. */
  u32 min_buffer_num; /* Minimum external buffer needed. */
#endif

  u32 intra_only;
  u32 conceal;
  u32 prev_is_key;
  u32 force_intra_freeze;
  u32 prob_refresh_detected;
  struct PicCallbackArg pic_callback_arg[MAX_ASIC_CORES];
  /* Output related variables. */
  FifoInst fifo_out;     /* Fifo for output indices. */
  FifoInst fifo_display; /* Store of output indices for display reordering. */
  u32 display_number;
  pthread_mutex_t sync_out; /* protects access to pictures in output fifo. */
  pthread_cond_t sync_out_cv;

  DWLHwConfig hw_cfg;

  enum DecPictureFormat output_format;
  u32 dynamic_buffer_limit; /* limit for dynamic frame buffer count */

  u32 down_scale_enabled;
  u32 down_scale_x_shift;
  u32 down_scale_y_shift;

  u32 vp9_10bit_support;
  u32 use_video_compressor;
  u32 use_ringbuffer;
  u32 use_fetch_one_pic;
  u32 use_8bits_output;
  u32 use_p010_output;
  enum DecPicturePixelFormat pixel_format;

#ifdef USE_EXTERNAL_BUFFER
  u32 ext_buffer_config;
  u32 next_buf_size;
  u32 buf_num;
  struct DWLLinearMem *buf_to_free;
  enum BufferType buf_type;
  u32 buffer_index;
  u32 add_buffer; /* flag to add the newly allocated buffer. */
  u32 buf_not_added; /* A reallocated buffer has been added to decoder or not */
#ifdef ASIC_TRACE_SUPPORT
  u32 is_frame_buffer;
#endif
  u32 buffer_num_added;   /* num of external buffers added */
  u32 abort;
#endif
  pthread_mutex_t protect_mutex;

  u32 input_data_len;
#ifdef USE_RANDOM_TEST
  struct ErrorParams error_params;
  u32 stream_not_consumed;
  u32 prev_input_len;
  FILE *ferror_stream;
#endif

  u32 legacy_regs;  /* Legacy releases? */
#ifdef USE_VP9_EC
  u32 entropy_broken;
#endif
  u32 no_decoding_buffer;

  u32 min_dec_pic_width;
  u32 min_dec_pic_height;
};

#endif /* #ifdef VP9HWD_CONTAINER_H */
