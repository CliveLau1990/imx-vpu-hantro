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

#ifndef JPEGDECCONT_H
#define JPEGDECCONT_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "basetype.h"
#include "jpegdecapi.h"
#include "dwl.h"
#include "deccfg.h"
#include "decppif.h"

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/
#ifdef _ASSERT_USED
#include <assert.h>
#endif

/* macro for assertion, used only if compiler flag _ASSERT_USED is defined */
#ifdef _ASSERT_USED
#define ASSERT(expr) assert(expr)
#else
#define ASSERT(expr)
#endif

#define MIN_NUMBER_OF_COMPONENTS 1
#define MAX_NUMBER_OF_COMPONENTS 3

#define JPEGDEC_X170_MIN_BUFFER 5120
#define JPEGDEC_X170_MAX_BUFFER 16776960
#define JPEGDEC_MAX_SLICE_SIZE 4096
#define JPEGDEC_TABLE_SIZE 544
#define JPEGDEC_MIN_WIDTH 48
#define JPEGDEC_MIN_HEIGHT 48
#define JPEGDEC_MAX_WIDTH 4672
#define JPEGDEC_MAX_HEIGHT 4672
#define JPEGDEC_MAX_PIXEL_AMOUNT 16370688
#define JPEGDEC_MAX_WIDTH_8190 8176
#define JPEGDEC_MAX_HEIGHT_8190 8176
#define JPEGDEC_MAX_PIXEL_AMOUNT_8190 66846976
#define JPEGDEC_MAX_SLICE_SIZE_8190 8100
#define JPEGDEC_MAX_WIDTH_WEBP 16384
#define JPEGDEC_MAX_HEIGHT_WEBP 16384
#define JPEGDEC_MAX_PIXEL_AMOUNT_WEBP 268435456
#define JPEGDEC_MAX_SLICE_SIZE_WEBP (1<<30)
#define JPEGDEC_MAX_WIDTH_TN 256
#define JPEGDEC_MAX_HEIGHT_TN 256
#define JPEGDEC_YUV400 0
#define JPEGDEC_YUV420 2
#define JPEGDEC_YUV422 3
#define JPEGDEC_YUV444 4
#define JPEGDEC_YUV440 5
#define JPEGDEC_YUV411 6
#define JPEGDEC_BASELINE_TABLE_SIZE 544
#define JPEGDEC_PROGRESSIVE_TABLE_SIZE 576
#define JPEGDEC_QP_BASE 32
#define JPEGDEC_AC1_BASE 48
#define JPEGDEC_AC2_BASE 88
#define JPEGDEC_DC1_BASE 129
#define JPEGDEC_DC2_BASE 132
#define JPEGDEC_DC3_BASE 135

/* progressive */
#define JPEGDEC_COEFF_SIZE 96

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

typedef struct {
  u32 C;  /* Component id */
  u32 H;  /* Horizontal sampling factor */
  u32 V;  /* Vertical sampling factor */
  u32 Tq; /* Quantization table destination selector */
} Components;

typedef struct {
  u8 *p_start_of_stream;
  u8 *p_curr_pos;
  addr_t stream_bus;
  u32 bit_pos_in_byte;
  u32 stream_length;
  u32 read_bits;
  u32 appn_flag;
  u32 thumbnail;
  u32 return_sos_marker;
} StreamStorage;

typedef struct {
  u8 *p_start_of_image;
  u8 *p_lum;
  u8 *p_cr;
  u8 *p_cb;
  u32 image_ready;
  u32 header_ready;
  u32 size;
  u32 size_luma;
  u32 size_chroma;
  u32 ready;
  u32 columns[MAX_NUMBER_OF_COMPONENTS];
  u32 pixels_per_row[MAX_NUMBER_OF_COMPONENTS];
} ImageData;

typedef struct {
  u32 Lf;
  u32 P;
  u32 Y;
  u32 hw_y;
  u32 X;
  u32 hw_x;
  u32 hw_ytn;
  u32 hw_xtn;
  u32 full_x;
  u32 full_y;
  u32 Nf; /* Number of components in frame */
  u32 coding_type;
  u32 num_mcu_in_frame;
  u32 num_mcu_in_row;
  u32 mcu_number;
  u32 next_rst_number;
  addr_t Ri;
  u32 dri_period;
  u32 block;
  u32 row;
  u32 col;
  u32 c_index;
  u32 *p_buffer;
  addr_t buffer_bus;
  i32 *p_buffer_cb;
  i32 *p_buffer_cr;
  struct DWLLinearMem p_table_base;
  u32 num_blocks[MAX_NUMBER_OF_COMPONENTS];
  u32 blocks_per_row[MAX_NUMBER_OF_COMPONENTS];
  u32 use_ac_offset[MAX_NUMBER_OF_COMPONENTS];
  Components component[MAX_NUMBER_OF_COMPONENTS];
} FrameInfo;

typedef struct {
  u32 Ls;
  u32 Ns;
  u32 Cs[MAX_NUMBER_OF_COMPONENTS];   /* Scan component selector */
  u32 Td[MAX_NUMBER_OF_COMPONENTS];   /* Selects table for DC */
  u32 Ta[MAX_NUMBER_OF_COMPONENTS];   /* Selects table for AC */
  u32 Ss;
  u32 Se;
  u32 Ah;
  u32 Al;
  u32 index;
  i32 num_idct_rows;
  i32 pred[MAX_NUMBER_OF_COMPONENTS];
} ScanInfo;

typedef struct {
  u32 slice_height;
  u32 amount_of_qtables;
  u32 y_cb_cr_mode;
  u32 y_cb_cr422;
  u32 column;
  u32 X;
  u32 Y;
  u32 mem_size;
  u32 SliceCount;
  u32 SliceReadyForPause;
  u32 SliceMBCutValue;
  u32 pipeline;
  u32 user_alloc_mem;
  u32 slice_mb_set_value;
  u32 timeout;
  u32 rlc_mode;
  u32 luma_pos;
  u32 chroma_pos;
  u32 slice_start_count;
  u32 amount_of_slices;
  u32 no_slice_irq_for_user;
  u32 slice_limit_reached;
  u32 input_buffer_empty;
  u32 fill_right;
  u32 fill_bottom;
  u32 stream_end;
  u32 stream_end_flag;
  u32 input_buffer_len;
  u32 input_streaming;
  u32 decoded_stream_len;
  u32 init;
  u32 init_thumb;
  u32 init_buffer_size;
  i32 dc_res[MAX_NUMBER_OF_COMPONENTS];
  struct DWLLinearMem out_luma;
  struct DWLLinearMem out_chroma;
  struct DWLLinearMem out_chroma2;
  struct DWLLinearMem given_out_luma;
  struct DWLLinearMem given_out_chroma;
  struct DWLLinearMem given_out_chroma2;
  i32 pred[MAX_NUMBER_OF_COMPONENTS];
  /* progressive parameters */
  u32 non_interleaved;
  u32 component_id;
  u32 operation_type;
  u32 operation_type_thumb;
  u32 progressive_scan_ready;
  u32 non_interleaved_scan_ready;
  u32 allocated;
  u32 y_cb_cr_mode_orig;
  u32 get_info_ycb_cr_mode;
  u32 get_info_ycb_cr_mode_tn;
  u32 components[MAX_NUMBER_OF_COMPONENTS];
  struct DWLLinearMem p_coeff_base;

  u32 fill_x;
  u32 fill_y;

  u32 progressive_finish;
  u32 pf_comp_id;
  u32 pf_needed[MAX_NUMBER_OF_COMPONENTS];
  struct DWLLinearMem tmp_strm;

} DecInfo;

typedef struct {

  struct DWLLinearMem out_luma_buffer;
  struct DWLLinearMem out_chroma_buffer;
  struct DWLLinearMem out_chroma_buffer2;

} JpegAsicBuffers;

typedef struct {
  u32 bits[16];
  u32 *vals;
  u32 table_length;
  u32 start;
  u32 last;
} VlcTable;

typedef struct {
  u32 Lh;
  u32 default_tables;
  VlcTable ac_table0;
  VlcTable ac_table1;
  VlcTable ac_table2;
  VlcTable ac_table3;
  VlcTable dc_table0;
  VlcTable dc_table1;
  VlcTable dc_table2;
  VlcTable dc_table3;
  VlcTable *table;
} HuffmanTables;

typedef struct {
  u32 Lq; /* Quantization table definition length */
  u32 table0[64];
  u32 table1[64];
  u32 table2[64];
  u32 table3[64];
  u32 *table;
} QuantTables;

typedef struct {
  u32 jpeg_regs[TOTAL_X170_REGISTERS];
  u32 asic_running;
  StreamStorage stream;
  FrameInfo frame;
  ImageData image;
  ScanInfo scan;
  DecInfo info;
  HuffmanTables vlc;
  QuantTables quant;
  u32 tmp_data[64];
  u32 is8190;
  u32 fuse_burned;
  u32 min_supported_width;
  u32 min_supported_height;
  u32 max_supported_width;
  u32 max_supported_height;
  u32 max_supported_pixel_amount;
  u32 max_supported_slice_size;
  u32 extensions_supported;
  JpegAsicBuffers asic_buff;
  DecPpInterface pp_control;
  DecPpQuery pp_config_query;   /* Decoder asks pp info about setup, info stored here */
  u32 pp_status;
  const void *dwl;    /* DWL instance */
  i32 core_id;

  const void *pp_instance;
  void (*PPRun) (const void *, const DecPpInterface *);
  void (*PPEndCallback) (const void *);
  void (*PPConfigQuery) (const void *, DecPpQuery *);

} JpegDecContainer;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

#endif /* #endif JPEGDECDATA_H */
