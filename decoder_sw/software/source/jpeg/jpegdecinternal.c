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

#include "basetype.h"
#include "jpegdeccontainer.h"
#include "jpegdecapi.h"
#include "jpegdecmarkers.h"
#include "jpegdecutils.h"
#include "jpegdechdrs.h"
#include "jpegdecscan.h"
#include "jpegregdrv.h"
#include "jpegdecinternal.h"
#include "dwl.h"
#include "deccfg.h"

#ifdef JPEGDEC_ASIC_TRACE
#include "jpegasicdbgtrace.h"
#endif /* #ifdef JPEGDEC_TRACE */

#ifdef JPEGDEC_PP_TRACE
#include "ppinternal.h"
#endif /* #ifdef JPEGDEC_PP_TRACE */

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

static const u8 zz_order[64] = {
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

#define JPEGDEC_SLICE_START_VALUE 0
#define JPEGDEC_VLC_LEN_START_REG 16
#define JPEGDEC_VLC_LEN_END_REG 29

#ifdef PJPEG_COMPONENT_TRACE
extern u32 pjpeg_component_id;
extern u32 *pjpeg_coeff_base;
extern u32 pjpeg_coeff_size;

#define TRACE_COMPONENT_ID(id) pjpeg_component_id = id
#else
#define TRACE_COMPONENT_ID(id)
#endif

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/
static void JpegDecWriteTables(JpegDecContainer * jpeg_dec_cont);
static void JpegDecWriteTablesNonInterleaved(JpegDecContainer * jpeg_dec_cont);
static void JpegDecWriteTablesProgressive(JpegDecContainer * jpeg_dec_cont);
static void JpegDecChromaTableSelectors(JpegDecContainer * jpeg_dec_cont);
static void JpegDecSetHwStrmParams(JpegDecContainer * jpeg_dec_cont);
static void JpegDecWriteLenBits(JpegDecContainer * jpeg_dec_cont);
static void JpegDecWriteLenBitsNonInterleaved(JpegDecContainer * jpeg_dec_cont);
static void JpegDecWriteLenBitsProgressive(JpegDecContainer * jpeg_dec_cont);

/*------------------------------------------------------------------------------
    5. Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

        Function name: JpegDecClearStructs

        Functional description:
          handles the initialisation of jpeg decoder data structure

        Inputs:

        Outputs:
          Returns OK when successful, NOK in case unknown message type is
          asked

------------------------------------------------------------------------------*/
void JpegDecClearStructs(JpegDecContainer * jpeg_dec_cont, u32 mode) {
  u32 i;

  ASSERT(jpeg_dec_cont);

  /* stream pointers */
  jpeg_dec_cont->stream.stream_bus = 0;
  jpeg_dec_cont->stream.p_start_of_stream = NULL;
  jpeg_dec_cont->stream.p_curr_pos = NULL;
  jpeg_dec_cont->stream.bit_pos_in_byte = 0;
  jpeg_dec_cont->stream.stream_length = 0;
  jpeg_dec_cont->stream.read_bits = 0;
  jpeg_dec_cont->stream.appn_flag = 0;
  jpeg_dec_cont->stream.return_sos_marker = 0;

  /* output image pointers and variables */
  jpeg_dec_cont->image.p_start_of_image = NULL;
  jpeg_dec_cont->image.p_lum = NULL;
  jpeg_dec_cont->image.p_cr = NULL;
  jpeg_dec_cont->image.p_cb = NULL;
  jpeg_dec_cont->image.header_ready = 0;
  jpeg_dec_cont->image.image_ready = 0;
  jpeg_dec_cont->image.ready = 0;
  jpeg_dec_cont->image.size = 0;
  jpeg_dec_cont->image.size_luma = 0;
  jpeg_dec_cont->image.size_chroma = 0;
  for(i = 0; i < MAX_NUMBER_OF_COMPONENTS; i++) {
    jpeg_dec_cont->image.columns[i] = 0;
    jpeg_dec_cont->image.pixels_per_row[i] = 0;
  }

  /* frame info */
  jpeg_dec_cont->frame.Lf = 0;
  jpeg_dec_cont->frame.P = 0;
  jpeg_dec_cont->frame.Y = 0;
  jpeg_dec_cont->frame.X = 0;

  if(!mode) {
    jpeg_dec_cont->frame.hw_y = 0;
    jpeg_dec_cont->frame.hw_x = 0;
    jpeg_dec_cont->frame.hw_ytn = 0;
    jpeg_dec_cont->frame.hw_xtn = 0;
    jpeg_dec_cont->frame.full_x = 0;
    jpeg_dec_cont->frame.full_y = 0;
    jpeg_dec_cont->stream.thumbnail = 0;
  } else {
    if(jpeg_dec_cont->stream.thumbnail) {
      jpeg_dec_cont->frame.hw_y = jpeg_dec_cont->frame.full_y;
      jpeg_dec_cont->frame.hw_x = jpeg_dec_cont->frame.full_x;
      jpeg_dec_cont->stream.thumbnail = 0;
    }
  }

  jpeg_dec_cont->frame.Nf = 0; /* Number of components in frame */
  jpeg_dec_cont->frame.coding_type = 0;
  jpeg_dec_cont->frame.num_mcu_in_frame = 0;
  jpeg_dec_cont->frame.num_mcu_in_row = 0;
  jpeg_dec_cont->frame.mcu_number = 0;
  jpeg_dec_cont->frame.Ri = 0;
  jpeg_dec_cont->frame.row = 0;
  jpeg_dec_cont->frame.col = 0;
  jpeg_dec_cont->frame.dri_period = 0;
  jpeg_dec_cont->frame.block = 0;
  jpeg_dec_cont->frame.c_index = 0;
  jpeg_dec_cont->frame.buffer_bus = 0;

  if(!mode) {
    jpeg_dec_cont->frame.p_buffer = NULL;
    jpeg_dec_cont->frame.p_buffer_cb = NULL;
    jpeg_dec_cont->frame.p_buffer_cr = NULL;
    jpeg_dec_cont->frame.p_table_base.virtual_address = NULL;
    jpeg_dec_cont->frame.p_table_base.bus_address = 0;

    /* asic buffer */
    jpeg_dec_cont->asic_buff.out_luma_buffer.virtual_address = NULL;
    jpeg_dec_cont->asic_buff.out_chroma_buffer.virtual_address = NULL;
    jpeg_dec_cont->asic_buff.out_chroma_buffer2.virtual_address = NULL;
    jpeg_dec_cont->asic_buff.out_luma_buffer.bus_address = 0;
    jpeg_dec_cont->asic_buff.out_chroma_buffer.bus_address = 0;
    jpeg_dec_cont->asic_buff.out_chroma_buffer2.bus_address = 0;
    jpeg_dec_cont->asic_buff.out_luma_buffer.size = 0;
    jpeg_dec_cont->asic_buff.out_chroma_buffer.size = 0;
    jpeg_dec_cont->asic_buff.out_chroma_buffer2.size = 0;

    /* pp instance */
    jpeg_dec_cont->pp_status = 0;
    jpeg_dec_cont->pp_instance = NULL;
    jpeg_dec_cont->PPRun = NULL;
    jpeg_dec_cont->PPEndCallback = NULL;
    jpeg_dec_cont->pp_control.use_pipeline = 0;

    /* resolution */
    jpeg_dec_cont->min_supported_width = 0;
    jpeg_dec_cont->min_supported_height = 0;
    jpeg_dec_cont->max_supported_width = 0;
    jpeg_dec_cont->max_supported_height = 0;
    jpeg_dec_cont->max_supported_pixel_amount = 0;
    jpeg_dec_cont->max_supported_slice_size = 0;

    /* out bus tmp */
    jpeg_dec_cont->info.out_luma.virtual_address = NULL;
    jpeg_dec_cont->info.out_chroma.virtual_address = NULL;
    jpeg_dec_cont->info.out_chroma2.virtual_address = NULL;

    /* user allocated addresses */
    jpeg_dec_cont->info.given_out_luma.virtual_address = NULL;
    jpeg_dec_cont->info.given_out_chroma.virtual_address = NULL;
    jpeg_dec_cont->info.given_out_chroma2.virtual_address = NULL;
  }

  /* asic running flag */
  jpeg_dec_cont->asic_running = 0;

  /* image handling info */
  jpeg_dec_cont->info.slice_height = 0;
  jpeg_dec_cont->info.amount_of_qtables = 0;
  jpeg_dec_cont->info.y_cb_cr_mode = 0;
  jpeg_dec_cont->info.y_cb_cr422 = 0;
  jpeg_dec_cont->info.column = 0;
  jpeg_dec_cont->info.X = 0;
  jpeg_dec_cont->info.Y = 0;
  jpeg_dec_cont->info.mem_size = 0;
  jpeg_dec_cont->info.SliceCount = 0;
  jpeg_dec_cont->info.SliceMBCutValue = 0;
  jpeg_dec_cont->info.pipeline = 0;
  if(!mode)
    jpeg_dec_cont->info.user_alloc_mem = 0;
  jpeg_dec_cont->info.slice_start_count = 0;
  jpeg_dec_cont->info.amount_of_slices = 0;
  jpeg_dec_cont->info.no_slice_irq_for_user = 0;
  jpeg_dec_cont->info.SliceReadyForPause = 0;
  jpeg_dec_cont->info.slice_limit_reached = 0;
  jpeg_dec_cont->info.slice_mb_set_value = 0;
  jpeg_dec_cont->info.timeout = (u32) DEC_X170_TIMEOUT_LENGTH;
  jpeg_dec_cont->info.rlc_mode = 0; /* JPEG always in VLC mode == 0 */
  jpeg_dec_cont->info.luma_pos = 0;
  jpeg_dec_cont->info.chroma_pos = 0;
  jpeg_dec_cont->info.fill_right = 0;
  jpeg_dec_cont->info.fill_bottom = 0;
  jpeg_dec_cont->info.stream_end = 0;
  jpeg_dec_cont->info.stream_end_flag = 0;
  jpeg_dec_cont->info.input_buffer_empty = 0;
  jpeg_dec_cont->info.input_streaming = 0;
  jpeg_dec_cont->info.input_buffer_len = 0;
  jpeg_dec_cont->info.decoded_stream_len = 0;
  jpeg_dec_cont->info.init = 0;
  jpeg_dec_cont->info.init_thumb = 0;
  jpeg_dec_cont->info.init_buffer_size = 0;

  /* progressive */
  jpeg_dec_cont->info.non_interleaved = 0;
  jpeg_dec_cont->info.component_id = 0;
  jpeg_dec_cont->info.operation_type = 0;
  jpeg_dec_cont->info.operation_type_thumb = 0;
  jpeg_dec_cont->info.progressive_scan_ready = 0;
  jpeg_dec_cont->info.non_interleaved_scan_ready = 0;

  if(!mode) {
    jpeg_dec_cont->info.p_coeff_base.virtual_address = NULL;
    jpeg_dec_cont->info.p_coeff_base.bus_address = 0;
    jpeg_dec_cont->info.y_cb_cr_mode_orig = 0;
    jpeg_dec_cont->info.get_info_ycb_cr_mode = 0;
    jpeg_dec_cont->info.get_info_ycb_cr_mode_tn = 0;
  }

  jpeg_dec_cont->info.allocated = 0;
  jpeg_dec_cont->info.progressive_finish = 0;
  jpeg_dec_cont->info.pf_comp_id = 0;
  for(i = 0; i < MAX_NUMBER_OF_COMPONENTS; i++)
    jpeg_dec_cont->info.pf_needed[i] = 0;
  jpeg_dec_cont->info.tmp_strm.virtual_address = NULL;

  for(i = 0; i < MAX_NUMBER_OF_COMPONENTS; i++) {
    jpeg_dec_cont->info.components[i] = 0;
    jpeg_dec_cont->info.pred[i] = 0;
    jpeg_dec_cont->info.dc_res[i] = 0;
    jpeg_dec_cont->frame.num_blocks[i] = 0;
    jpeg_dec_cont->frame.blocks_per_row[i] = 0;
    jpeg_dec_cont->frame.use_ac_offset[i] = 0;
    jpeg_dec_cont->frame.component[i].C = 0;
    jpeg_dec_cont->frame.component[i].H = 0;
    jpeg_dec_cont->frame.component[i].V = 0;
    jpeg_dec_cont->frame.component[i].Tq = 0;
  }

  /* scan info */
  jpeg_dec_cont->scan.Ls = 0;
  jpeg_dec_cont->scan.Ns = 0;
  jpeg_dec_cont->scan.Ss = 0;
  jpeg_dec_cont->scan.Se = 0;
  jpeg_dec_cont->scan.Ah = 0;
  jpeg_dec_cont->scan.Al = 0;
  jpeg_dec_cont->scan.index = 0;
  jpeg_dec_cont->scan.num_idct_rows = 0;

  for(i = 0; i < MAX_NUMBER_OF_COMPONENTS; i++) {
    jpeg_dec_cont->scan.Cs[i] = 0;
    jpeg_dec_cont->scan.Td[i] = 0;
    jpeg_dec_cont->scan.Ta[i] = 0;
    jpeg_dec_cont->scan.pred[i] = 0;
  }

  /* huffman table lengths */
  jpeg_dec_cont->vlc.default_tables = 0;
  jpeg_dec_cont->vlc.ac_table0.table_length = 0;
  jpeg_dec_cont->vlc.ac_table1.table_length = 0;
  jpeg_dec_cont->vlc.ac_table2.table_length = 0;
  jpeg_dec_cont->vlc.ac_table3.table_length = 0;

  jpeg_dec_cont->vlc.dc_table0.table_length = 0;
  jpeg_dec_cont->vlc.dc_table1.table_length = 0;
  jpeg_dec_cont->vlc.dc_table2.table_length = 0;
  jpeg_dec_cont->vlc.dc_table3.table_length = 0;

  /* Restart interval */
  jpeg_dec_cont->frame.Ri = 0;

  if(jpeg_dec_cont->vlc.ac_table0.vals) {
    DWLfree(jpeg_dec_cont->vlc.ac_table0.vals);
  }
  if(jpeg_dec_cont->vlc.ac_table1.vals) {
    DWLfree(jpeg_dec_cont->vlc.ac_table1.vals);
  }
  if(jpeg_dec_cont->vlc.ac_table2.vals) {
    DWLfree(jpeg_dec_cont->vlc.ac_table2.vals);
  }
  if(jpeg_dec_cont->vlc.ac_table3.vals) {
    DWLfree(jpeg_dec_cont->vlc.ac_table3.vals);
  }
  if(jpeg_dec_cont->vlc.dc_table0.vals) {
    DWLfree(jpeg_dec_cont->vlc.dc_table0.vals);
  }
  if(jpeg_dec_cont->vlc.dc_table1.vals) {
    DWLfree(jpeg_dec_cont->vlc.dc_table1.vals);
  }
  if(jpeg_dec_cont->vlc.dc_table2.vals) {
    DWLfree(jpeg_dec_cont->vlc.dc_table2.vals);
  }
  if(jpeg_dec_cont->vlc.dc_table3.vals) {
    DWLfree(jpeg_dec_cont->vlc.dc_table3.vals);
  }
  if(jpeg_dec_cont->frame.p_buffer) {
    DWLfree(jpeg_dec_cont->frame.p_buffer);
  }

  /* pointer initialisation */
  jpeg_dec_cont->vlc.ac_table0.vals = NULL;
  jpeg_dec_cont->vlc.ac_table1.vals = NULL;
  jpeg_dec_cont->vlc.ac_table2.vals = NULL;
  jpeg_dec_cont->vlc.ac_table3.vals = NULL;

  jpeg_dec_cont->vlc.dc_table0.vals = NULL;
  jpeg_dec_cont->vlc.dc_table1.vals = NULL;
  jpeg_dec_cont->vlc.dc_table2.vals = NULL;
  jpeg_dec_cont->vlc.dc_table3.vals = NULL;

  jpeg_dec_cont->frame.p_buffer = NULL;

  return;
}
/*------------------------------------------------------------------------------

        Function name: JpegDecInitHW

        Functional description:
          Set up HW regs for decode

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          Returns OK when successful, NOK in case unknown message type is
          asked

------------------------------------------------------------------------------*/
JpegDecRet JpegDecInitHW(JpegDecContainer * jpeg_dec_cont) {
  u32 i;
  addr_t coeff_buffer = 0;

#define PTR_JPGC   jpeg_dec_cont

  ASSERT(jpeg_dec_cont);

  TRACE_COMPONENT_ID(PTR_JPGC->info.component_id);

  /* Check if first InitHw call */
  if(PTR_JPGC->info.slice_start_count == 0) {
    /* Check if HW resource is available */
    if(DWLReserveHw(jpeg_dec_cont->dwl, &jpeg_dec_cont->core_id) == DWL_ERROR) {
      JPEGDEC_TRACE_INTERNAL(("JpegDecInitHW: ERROR hw resource unavailable"));
      return JPEGDEC_HW_RESERVED;
    }
  }

  /*************** Set swreg4 data ************/
  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width extension\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_W_EXT,
                 ((((PTR_JPGC->info.X) >> (4)) & 0xE00) >> 9));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_WIDTH,
                 ((PTR_JPGC->info.X) >> (4)) & 0x1FF);

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height extension\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_H_EXT,
                 ((((PTR_JPGC->info.Y) >> (4)) & 0x700) >> 8));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_HEIGHT_P,
                 ((PTR_JPGC->info.Y) >> (4)) & 0x0FF);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set decoding mode: JPEG\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_MODE, JPEG_X170_MODE_JPEG);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set output write enabled\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_DIS, 0);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set filtering disabled\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_FILTERING_DIS, 1);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set amount of QP Table\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_QTABLES,
                 PTR_JPGC->info.amount_of_qtables);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set input format\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_MODE,
                 PTR_JPGC->info.y_cb_cr_mode);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: RLC mode enable, JPEG == disable\n"));
  /* In case of JPEG: Always VLC mode used (0) */
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_RLC_MODE_E, PTR_JPGC->info.rlc_mode);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Width is not multiple of 16\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_FILRIGHT_E,
                 PTR_JPGC->info.fill_right);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_FILDOWN_E,
                 PTR_JPGC->info.fill_bottom);

  /*************** Set swreg15 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set slice/full mode: 0 full; other = slice\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_SLICE_H,
                 PTR_JPGC->info.slice_height);

  /*************** Set swreg52 data ************/
  if(PTR_JPGC->info.operation_type != JPEGDEC_PROGRESSIVE) {
    /* Set JPEG operation mode */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_E, 0);
  } else {
    /* Set JPEG operation mode */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_E, 1);
  }

  /* Set spectral selection start coefficient */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_SS, PTR_JPGC->scan.Ss);

  /* Set spectral selection end coefficient */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_SE, PTR_JPGC->scan.Se);

  /* Set the point transform used in the preceding scan */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_AH, PTR_JPGC->scan.Ah);

  /* Set the point transform value */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_AL, PTR_JPGC->scan.Al);

  /* Set needed progressive parameters */
  if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE) {
    /* write coeff table base */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set coefficient buffer base address\n"));
    coeff_buffer = PTR_JPGC->info.p_coeff_base.bus_address;
    /* non-interleaved */
    if(PTR_JPGC->info.non_interleaved) {
      for(i = 0; i < PTR_JPGC->info.component_id; i++) {
        coeff_buffer += (JPEGDEC_COEFF_SIZE *
                         PTR_JPGC->frame.num_blocks[i]);
      }
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_COEFF_BUF,
                   coeff_buffer);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_DIS, 0);
    }
    /* interleaved components */
    else {
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_COEFF_BUF,
                   coeff_buffer);
      coeff_buffer += (JPEGDEC_COEFF_SIZE) * PTR_JPGC->frame.num_blocks[0];
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_DCCB_BASE,
                   coeff_buffer);
      coeff_buffer += (JPEGDEC_COEFF_SIZE) * PTR_JPGC->frame.num_blocks[1];
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_DCCR_BASE,
                   coeff_buffer);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_DIS, 1);
    }
  }

  /*************** Set swreg5/swreg6/swreg12/swreg16-swreg27 data ************/

  if(PTR_JPGC->info.operation_type == JPEGDEC_BASELINE) {
    /* write "length amounts" */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write VLC length amounts to register\n"));
    JpegDecWriteLenBits(PTR_JPGC);

    /* Create AC/DC/QP tables for HW */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write AC,DC,QP tables to base\n"));
    JpegDecWriteTables(PTR_JPGC);

  } else if(PTR_JPGC->info.operation_type == JPEGDEC_NONINTERLEAVED) {
    /* write "length amounts" */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write VLC length amounts to register\n"));
    JpegDecWriteLenBitsNonInterleaved(PTR_JPGC);

    /* Create AC/DC/QP tables for HW */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write AC,DC,QP tables to base\n"));
    JpegDecWriteTablesNonInterleaved(PTR_JPGC);
  } else {
    /* write "length amounts" */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write VLC length amounts to register\n"));
    JpegDecWriteLenBitsProgressive(PTR_JPGC);

    /* Create AC/DC/QP tables for HW */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write AC,DC,QP tables to base\n"));
    JpegDecWriteTablesProgressive(PTR_JPGC);
  }

  /* Select which tables the chromas use */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Select chroma AC,DC tables\n"));
  JpegDecChromaTableSelectors(PTR_JPGC);

  /* write table base */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set AC,DC,QP table base address\n"));
  SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_QTABLE_BASE,
               PTR_JPGC->frame.p_table_base.bus_address);

  /* set up stream position for HW decode */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set stream position for HW\n"));
  JpegDecSetHwStrmParams(PTR_JPGC);

  /* set restart interval */
  if(PTR_JPGC->frame.Ri) {
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_SYNC_MARKER_E, 1);
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_REST_FREQ,
                   PTR_JPGC->frame.Ri);
  } else
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_SYNC_MARKER_E, 0);

  /* Handle PP and output base addresses */

  /* PP depending register writes */
  if(PTR_JPGC->pp_instance != NULL && PTR_JPGC->pp_control.use_pipeline) {
    /*************** Set swreg4 data ************/

    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set output write disabled\n"));
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_DIS, 1);

    /* set output to zero, because of pp */
    /*************** Set swreg13 data ************/
    /* Luminance output */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set LUMA OUTPUT data base address\n"));
    SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE, (addr_t)0);

    /*************** Set swreg14 data ************/
    /* Chrominance output */
    if(PTR_JPGC->image.size_chroma) {
      /* write output base */
      JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set CHROMA OUTPUT data base address\n"));
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_JPG_CH_OUT_BASE, (addr_t)0);
    }

    if(PTR_JPGC->info.slice_start_count == JPEGDEC_SLICE_START_VALUE) {
      /* Enable pp */
      JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Enable pp\n"));
      PTR_JPGC->PPRun(PTR_JPGC->pp_instance, &PTR_JPGC->pp_control);

      PTR_JPGC->pp_control.pp_status = DECPP_RUNNING;
    }

    PTR_JPGC->info.pipeline = 1;
  } else {
    /*************** Set swreg13 data ************/

    /* Luminance output */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set LUMA OUTPUT data base address\n"));

    if(PTR_JPGC->info.operation_type == JPEGDEC_BASELINE) {
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE,
                   PTR_JPGC->asic_buff.out_luma_buffer.bus_address);

      /*************** Set swreg14 data ************/

      /* Chrominance output */
      if(PTR_JPGC->image.size_chroma) {
        /* write output base */
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set CHROMA OUTPUT data base address\n"));
        SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_JPG_CH_OUT_BASE,
                     PTR_JPGC->asic_buff.out_chroma_buffer.bus_address);
      }
    } else {
      if(PTR_JPGC->info.component_id == 0) {
        SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE,
                     PTR_JPGC->asic_buff.out_luma_buffer.bus_address);
      } else if(PTR_JPGC->info.component_id == 1) {
        SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE,
                     PTR_JPGC->asic_buff.out_chroma_buffer.bus_address);
      } else {
        SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE,
                     (PTR_JPGC->asic_buff.out_chroma_buffer2.
                      bus_address));
      }
    }

    PTR_JPGC->info.pipeline = 0;
  }

  PTR_JPGC->info.slice_start_count = 1;

#ifdef JPEGDEC_ASIC_TRACE
  {
    FILE *fd;

    fd = fopen("picture_ctrl_dec.trc", "at");
    DumpJPEGCtrlReg(jpeg_dec_cont->jpeg_regs, fd);
    fclose(fd);

    fd = fopen("picture_ctrl_dec.hex", "at");
    HexDumpJPEGCtrlReg(jpeg_dec_cont->jpeg_regs, fd);
    fclose(fd);

    fd = fopen("jpeg_tables.hex", "at");
    HexDumpJPEGTables(jpeg_dec_cont->jpeg_regs, jpeg_dec_cont, fd);
    fclose(fd);

    fd = fopen("registers.hex", "at");
    HexDumpRegs(jpeg_dec_cont->jpeg_regs, fd);
    fclose(fd);
  }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

#ifdef JPEGDEC_PP_TRACE
  ppRegDump(((PPContainer_t *) PTR_JPGC->pp_instance)->pp_regs);
#endif /* #ifdef JPEGDEC_PP_TRACE */

  PTR_JPGC->asic_running = 1;

  /* Flush regs to hw register */
  JpegFlushRegs(PTR_JPGC);

  /* Enable jpeg mode and set slice mode */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Enable jpeg\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_E, 1);
  DWLEnableHw(PTR_JPGC->dwl, PTR_JPGC->core_id, 4 * 1, PTR_JPGC->jpeg_regs[1]);

#undef PTR_JPGC

  return JPEGDEC_OK;
}

/*------------------------------------------------------------------------------

        Function name: JpegDecInitHWContinue

        Functional description:
          Set up HW regs for decode

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          Returns OK when successful, NOK in case unknown message type is
          asked

------------------------------------------------------------------------------*/
void JpegDecInitHWContinue(JpegDecContainer * jpeg_dec_cont) {
#define PTR_JPGC   jpeg_dec_cont

  ASSERT(jpeg_dec_cont);

  /* update slice counter */
  PTR_JPGC->info.amount_of_slices++;

  if(PTR_JPGC->pp_instance == NULL &&
      PTR_JPGC->info.user_alloc_mem == 1 && PTR_JPGC->info.slice_start_count > 0) {
    /* if user allocated memory ==> new addresses */
    PTR_JPGC->asic_buff.out_luma_buffer.virtual_address =
      PTR_JPGC->info.given_out_luma.virtual_address;
    PTR_JPGC->asic_buff.out_luma_buffer.bus_address =
      PTR_JPGC->info.given_out_luma.bus_address;
    PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address =
      PTR_JPGC->info.given_out_chroma.virtual_address;
    PTR_JPGC->asic_buff.out_chroma_buffer.bus_address =
      PTR_JPGC->info.given_out_chroma.bus_address;
  }

  /* Update only register/values that might have been changed */

  /*************** Set swreg1 data ************/
  /* clear status bit */
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_SLICE_INT, 0);

  /*************** Set swreg5 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: Set stream last buffer bit\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_STREAM_ALL,
                 PTR_JPGC->info.stream_end);

  /*************** Set swreg13 data ************/
  /* PP depending register writes */
  if(PTR_JPGC->pp_instance == NULL) {
    /* Luminance output */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: Set LUMA OUTPUT data base address\n"));
    SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE,
                 PTR_JPGC->asic_buff.out_luma_buffer.bus_address);

    /*************** Set swreg14 data ************/

    /* Chrominance output */
    if(PTR_JPGC->image.size_chroma) {
      /* write output base */
      JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: Set CHROMA OUTPUT data base address\n"));
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_JPG_CH_OUT_BASE,
                   PTR_JPGC->asic_buff.out_chroma_buffer.bus_address);
    }

    PTR_JPGC->info.pipeline = 0;
  }

  /*************** Set swreg13 data ************/
  /* PP depending register writes */
  if(PTR_JPGC->pp_instance != NULL && PTR_JPGC->pp_control.use_pipeline == 0) {
    if(PTR_JPGC->info.y_cb_cr_mode == JPEGDEC_YUV420) {
      PTR_JPGC->info.luma_pos = (PTR_JPGC->info.X *
                                 (PTR_JPGC->info.slice_mb_set_value * 16));
      PTR_JPGC->info.chroma_pos = ((PTR_JPGC->info.X) *
                                   (PTR_JPGC->info.slice_mb_set_value * 8));
    } else if(PTR_JPGC->info.y_cb_cr_mode == JPEGDEC_YUV422) {
      PTR_JPGC->info.luma_pos = (PTR_JPGC->info.X *
                                 (PTR_JPGC->info.slice_mb_set_value * 16));
      PTR_JPGC->info.chroma_pos = ((PTR_JPGC->info.X) *
                                   (PTR_JPGC->info.slice_mb_set_value * 16));
    } else if(PTR_JPGC->info.y_cb_cr_mode == JPEGDEC_YUV440) {
      PTR_JPGC->info.luma_pos = (PTR_JPGC->info.X *
                                 (PTR_JPGC->info.slice_mb_set_value * 16));
      PTR_JPGC->info.chroma_pos = ((PTR_JPGC->info.X) *
                                   (PTR_JPGC->info.slice_mb_set_value * 16));
    } else {
      PTR_JPGC->info.luma_pos = (PTR_JPGC->info.X *
                                 (PTR_JPGC->info.slice_mb_set_value * 16));
      PTR_JPGC->info.chroma_pos = 0;
    }

    /* update luma/chroma position */
    PTR_JPGC->info.luma_pos = (PTR_JPGC->info.luma_pos *
                               PTR_JPGC->info.amount_of_slices);
    if(PTR_JPGC->info.chroma_pos) {
      PTR_JPGC->info.chroma_pos = (PTR_JPGC->info.chroma_pos *
                                   PTR_JPGC->info.amount_of_slices);
    }

    /* Luminance output */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: Set LUMA OUTPUT data base address\n"));
    SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE,
                 PTR_JPGC->asic_buff.out_luma_buffer.bus_address +
                 PTR_JPGC->info.luma_pos);

    /*************** Set swreg14 data ************/

    /* Chrominance output */
    if(PTR_JPGC->image.size_chroma) {
      /* write output base */
      JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: Set CHROMA OUTPUT data base address\n"));
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_JPG_CH_OUT_BASE,
                   PTR_JPGC->asic_buff.out_chroma_buffer.bus_address +
                   PTR_JPGC->info.chroma_pos);
    }

    PTR_JPGC->info.pipeline = 0;
  }

  /*************** Set swreg15 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: Set slice/full mode: 0 full; other = slice\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_SLICE_H,
                 PTR_JPGC->info.slice_height);

  /* Flush regs to hw register */
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x34,
              PTR_JPGC->jpeg_regs[13]);
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x38,
              PTR_JPGC->jpeg_regs[14]);
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x3C,
              PTR_JPGC->jpeg_regs[15]);
#ifdef USE_64BIT_ENV
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x1EC,
              PTR_JPGC->jpeg_regs[123]);
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x1F0,
              PTR_JPGC->jpeg_regs[124]);
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x1F4,
              PTR_JPGC->jpeg_regs[125]);
#endif
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x14,
              PTR_JPGC->jpeg_regs[5]);
  DWLEnableHw(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x4,
              PTR_JPGC->jpeg_regs[1]);

#ifdef JPEGDEC_ASIC_TRACE
  {
    JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: REGS BEFORE IRQ CLEAN\n"));
    PrintJPEGReg(jpeg_dec_cont->jpeg_regs);
  }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

}

/*------------------------------------------------------------------------------

        Function name: JpegDecInitHWInputBuffLoad

        Functional description:
          Set up HW regs for decode after input buffer load

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          Returns OK when successful, NOK in case unknown message type is
          asked

------------------------------------------------------------------------------*/
void JpegDecInitHWInputBuffLoad(JpegDecContainer * jpeg_dec_cont) {
#define PTR_JPGC   jpeg_dec_cont

  ASSERT(jpeg_dec_cont);

  /* Update only register/values that might have been changed */
  /*************** Set swreg4 data ************/
  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width extension\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_W_EXT,
                 ((((PTR_JPGC->info.X) >> (4)) & 0xE00) >> 9));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_WIDTH,
                 ((PTR_JPGC->info.X) >> (4)) & 0x1FF);

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height extension\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_H_EXT,
                 ((((PTR_JPGC->info.Y) >> (4)) & 0x700) >> 8));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_HEIGHT_P,
                 ((PTR_JPGC->info.Y) >> (4)) & 0x0FF);

  /*************** Set swreg5 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: Set stream start bit\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_STRM_START_BIT,
                 PTR_JPGC->stream.bit_pos_in_byte);

  /*************** Set swreg6 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: Set stream length\n"));

  /* check if all stream will processed in this buffer */
  if((PTR_JPGC->info.decoded_stream_len) >= PTR_JPGC->stream.stream_length) {
    PTR_JPGC->info.stream_end = 1;
  }

  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_STREAM_LEN,
                 PTR_JPGC->info.input_buffer_len);

  /*************** Set swreg4 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: Set stream last buffer bit\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_STREAM_ALL,
                 PTR_JPGC->info.stream_end);

  /*************** Set swreg12 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: Set stream start address\n"));
  SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_RLC_VLC_BASE,
               PTR_JPGC->stream.stream_bus);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: Stream bus start 0x%08x\n",
                          PTR_JPGC->stream.stream_bus));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: Bit position 0x%08x\n",
                          PTR_JPGC->stream.bit_pos_in_byte));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: Stream length 0x%08x\n",
                          PTR_JPGC->stream.stream_length));

  /* Flush regs to hw register */
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x30,
              PTR_JPGC->jpeg_regs[12]);
#ifdef USE_64BIT_ENV
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x1E8,
              PTR_JPGC->jpeg_regs[122]);
#endif
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x10,
              PTR_JPGC->jpeg_regs[4]);
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x14,
              PTR_JPGC->jpeg_regs[5]);
  DWLWriteReg(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x18,
              PTR_JPGC->jpeg_regs[6]);
  DWLEnableHw(jpeg_dec_cont->dwl, PTR_JPGC->core_id, 0x04,
              PTR_JPGC->jpeg_regs[1]);

#ifdef JPEGDEC_ASIC_TRACE
  {
    JPEGDEC_TRACE_INTERNAL(("INTERNAL BUFFER LOAD: REGS BEFORE IRQ CLEAN\n"));
    PrintJPEGReg(jpeg_dec_cont->jpeg_regs);
  }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

}

/*------------------------------------------------------------------------------

        Function name: JpegDecInitHWProgressiveContinue

        Functional description:
          Set up HW regs for decode after progressive scan decoded

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          Returns OK when successful, NOK in case unknown message type is
          asked

------------------------------------------------------------------------------*/
void JpegDecInitHWProgressiveContinue(JpegDecContainer * jpeg_dec_cont) {
#define PTR_JPGC   jpeg_dec_cont

  u32 i;
  addr_t coeff_buffer = 0;
  addr_t output_buffer = 0;

  ASSERT(jpeg_dec_cont);

  if(PTR_JPGC->pp_instance == NULL && PTR_JPGC->info.user_alloc_mem == 1) {
    /* if user allocated memory ==> new addresses */
    PTR_JPGC->asic_buff.out_luma_buffer.virtual_address =
      PTR_JPGC->info.given_out_luma.virtual_address;
    PTR_JPGC->asic_buff.out_luma_buffer.bus_address =
      PTR_JPGC->info.given_out_luma.bus_address;
    PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address =
      PTR_JPGC->info.given_out_chroma.virtual_address;
    PTR_JPGC->asic_buff.out_chroma_buffer.bus_address =
      PTR_JPGC->info.given_out_chroma.bus_address;
    PTR_JPGC->asic_buff.out_chroma_buffer2.virtual_address =
      PTR_JPGC->info.given_out_chroma2.virtual_address;
    PTR_JPGC->asic_buff.out_chroma_buffer2.bus_address =
      PTR_JPGC->info.given_out_chroma2.bus_address;
  }

  TRACE_COMPONENT_ID(PTR_JPGC->info.component_id);
  /* Update only register/values that might have been changed */

  /*************** Set swreg13 data ************/
  /* Luminance output */
  if(PTR_JPGC->info.component_id == 0)
    output_buffer = PTR_JPGC->asic_buff.out_luma_buffer.bus_address;
  else if(PTR_JPGC->info.component_id == 1)
    output_buffer = (PTR_JPGC->asic_buff.out_chroma_buffer.bus_address);
  else
    output_buffer = (PTR_JPGC->asic_buff.out_chroma_buffer2.bus_address);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set LUMA OUTPUT data base address\n"));
  SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_BASE, output_buffer);

  PTR_JPGC->info.pipeline = 0;

  /* set up stream position for HW decode */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set stream position for HW\n"));
  JpegDecSetHwStrmParams(PTR_JPGC);

  /*************** Set swreg5 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set input format\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_MODE,
                 PTR_JPGC->info.y_cb_cr_mode);

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width extension\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_W_EXT,
                 ((((PTR_JPGC->info.X) >> (4)) & 0xE00) >> 9));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_WIDTH,
                 ((PTR_JPGC->info.X) >> (4)) & 0x1FF);

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height extension\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_H_EXT,
                 ((((PTR_JPGC->info.Y) >> (4)) & 0x700) >> 8));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PIC_MB_HEIGHT_P,
                 ((PTR_JPGC->info.Y) >> (4)) & 0x0FF);

  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_WDIV8, PTR_JPGC->info.fill_x);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_FILRIGHT_E,
                 PTR_JPGC->info.fill_x || PTR_JPGC->info.fill_right);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_HDIV8, PTR_JPGC->info.fill_y);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_FILDOWN_E,
                 PTR_JPGC->info.fill_y || PTR_JPGC->info.fill_bottom);

  /*************** Set swreg52 data ************/
  /* Set JPEG operation mode */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  if(PTR_JPGC->info.operation_type != JPEGDEC_PROGRESSIVE) {
    /* Set JPEG operation mode */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_E, 0);
  } else {
    /* Set JPEG operation mode */
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_E, 1);
  }

  /* Set spectral selection start coefficient */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_SS, PTR_JPGC->scan.Ss);

  /* Set spectral selection end coefficient */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_SE, PTR_JPGC->scan.Se);

  /* Set the point transform used in the preceding scan */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_AH, PTR_JPGC->scan.Ah);

  /* Set the point transform value */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_PJPEG_AL, PTR_JPGC->scan.Al);

  /* Set needed progressive parameters */
  if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE) {
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set coefficient buffer base address\n"));
    coeff_buffer = PTR_JPGC->info.p_coeff_base.bus_address;
    /* non-interleaved */
    if(PTR_JPGC->info.non_interleaved) {
      for(i = 0; i < PTR_JPGC->info.component_id; i++) {
        coeff_buffer += (JPEGDEC_COEFF_SIZE *
                         PTR_JPGC->frame.num_blocks[i]);
      }
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_COEFF_BUF,
                   coeff_buffer);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_DIS, 0);
    }
    /* interleaved components */
    else {
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_COEFF_BUF,
                   coeff_buffer);
      coeff_buffer += (JPEGDEC_COEFF_SIZE) * PTR_JPGC->frame.num_blocks[0];
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_DCCB_BASE,
                   coeff_buffer);
      coeff_buffer += (JPEGDEC_COEFF_SIZE) * PTR_JPGC->frame.num_blocks[1];
      SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_PJPEG_DCCR_BASE,
                   coeff_buffer);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_DIS, 1);
    }
  }

  /* write "length amounts" */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write VLC length amounts to register\n"));
  JpegDecWriteLenBitsProgressive(PTR_JPGC);

  /* Create AC/DC/QP tables for HW */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write AC,DC,QP tables to base\n"));
  JpegDecWriteTablesProgressive(PTR_JPGC);

  /* Select which tables the chromas use */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Select chroma AC,DC tables\n"));
  JpegDecChromaTableSelectors(PTR_JPGC);

  /* write table base */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set AC,DC,QP table base address\n"));
  SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_QTABLE_BASE,
               PTR_JPGC->frame.p_table_base.bus_address);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_QTABLES,
                 PTR_JPGC->info.amount_of_qtables);

  if(PTR_JPGC->info.slice_mb_set_value) {
    /*************** Set swreg15 data ************/
    JPEGDEC_TRACE_INTERNAL(("INTERNAL CONTINUE: Set slice/full mode: 0 full; other = slice\n"));
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_SLICE_H,
                   PTR_JPGC->info.slice_height);
  }

  /* set restart interval */
  if(PTR_JPGC->frame.Ri) {
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_SYNC_MARKER_E, 1);
    SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_REFER13_BASE,
                 PTR_JPGC->frame.Ri);
  } else
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_SYNC_MARKER_E, 0);

  PTR_JPGC->asic_running = 1;

  /* Flush regs to hw register */
  JpegFlushRegs(PTR_JPGC);

  /* Enable jpeg mode and set slice mode */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Enable jpeg\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_E, 1);
  DWLEnableHw(PTR_JPGC->dwl, PTR_JPGC->core_id, 4 * 1, PTR_JPGC->jpeg_regs[1]);

#ifdef JPEGDEC_ASIC_TRACE
  {
    JPEGDEC_TRACE_INTERNAL(("PROGRESSIVE CONTINUE: REGS BEFORE IRQ CLEAN\n"));
    PrintJPEGReg(jpeg_dec_cont->jpeg_regs);
  }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

}

/*------------------------------------------------------------------------------

        Function name: JpegDecSetHwStrmParams

        Functional description:
          set up hw stream start position

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecSetHwStrmParams(JpegDecContainer * jpeg_dec_cont) {

#define PTR_JPGC    jpeg_dec_cont
#define JPG_STR     jpeg_dec_cont->stream

  addr_t addr_tmp = 0;
  u32 amount_of_stream = 0;

  /* calculate and set stream start address to hw */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: read bits %d\n", JPG_STR.read_bits));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: read bytes %d\n", JPG_STR.read_bits / 8));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Stream bus start 0x%08x\n",
                          JPG_STR.stream_bus));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Stream virtual start 0x%08x\n",
                          JPG_STR.p_start_of_stream));

  /* calculate and set stream start address to hw */
  addr_tmp = ((addr_t) JPG_STR.stream_bus + ((addr_t) JPG_STR.p_start_of_stream & 0x3) +
              (addr_t) (JPG_STR.p_curr_pos - JPG_STR.p_start_of_stream)) & (~7);

  SET_ADDR_REG(PTR_JPGC->jpeg_regs, HWIF_RLC_VLC_BASE, addr_tmp);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Stream bus start 0x%08x\n",
                          JPG_STR.stream_bus));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Start Addr 0x%08x\n",
                          GetJpegDecStreamStartAddress(PTR_JPGC->jpeg_regs)));

  /* calculate and set stream start bit to hw */

  /* change current pos to bus address style */
  /* remove three lowest bits and add the difference to bitPosInWord */
  /* used as bit pos in word not as bit pos in byte actually... */
  switch ((addr_t) JPG_STR.p_curr_pos & (7)) {
  case 0:
    break;
  case 1:
    JPG_STR.bit_pos_in_byte += 8;
    break;
  case 2:
    JPG_STR.bit_pos_in_byte += 16;
    break;
  case 3:
    JPG_STR.bit_pos_in_byte += 24;
    break;
  case 4:
    JPG_STR.bit_pos_in_byte += 32;
    break;
  case 5:
    JPG_STR.bit_pos_in_byte += 40;
    break;
  case 6:
    JPG_STR.bit_pos_in_byte += 48;
    break;
  case 7:
    JPG_STR.bit_pos_in_byte += 56;
    break;
  default:
    ASSERT(0);
    break;
  }

  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_STRM_START_BIT,
                 JPG_STR.bit_pos_in_byte);

  /* set up stream length for HW.
   * length = size of original buffer - stream we already decoded in SW */
  JPG_STR.p_curr_pos = (u8 *) ((addr_t) JPG_STR.p_curr_pos & (~7));

  if(PTR_JPGC->info.input_streaming) {
    amount_of_stream = (PTR_JPGC->info.input_buffer_len -
                        (u32) (JPG_STR.p_curr_pos - JPG_STR.p_start_of_stream));

    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_STREAM_LEN, amount_of_stream);
  } else {
    amount_of_stream = (JPG_STR.stream_length -
                        (u32) (JPG_STR.p_curr_pos - JPG_STR.p_start_of_stream));

    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_STREAM_LEN, amount_of_stream);

    /* because no input streaming, frame should be ready during decoding this buffer */
    PTR_JPGC->info.stream_end = 1;
  }

  /*************** Set swreg4 data ************/
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set stream last buffer bit\n"));
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_JPEG_STREAM_ALL,
                 PTR_JPGC->info.stream_end);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: JPG_STR.stream_length %d\n",
                          JPG_STR.stream_length));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: JPG_STR.p_curr_pos 0x%08x\n",
                          (u32) JPG_STR.p_curr_pos));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: JPG_STR.p_start_of_stream 0x%08x\n",
                          (u32) JPG_STR.p_start_of_stream));
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: JPG_STR.bit_pos_in_byte 0x%08x\n",
                          JPG_STR.bit_pos_in_byte));

  return;

#undef JPG_STR
#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

        Function name: JpegDecAllocateResidual

        Functional description:
          Allocates residual buffer

        Inputs:
          JpegDecContainer *jpeg_dec_cont  Pointer to DecData structure

        Outputs:
          OK
          JPEGDEC_MEMFAIL

------------------------------------------------------------------------------*/
JpegDecRet JpegDecAllocateResidual(JpegDecContainer * jpeg_dec_cont) {
#define PTR_JPGC   jpeg_dec_cont

  i32 tmp = JPEGDEC_ERROR;
  u32 num_blocks = 0;
  u32 i;
  u32 table_size = 0;

  ASSERT(PTR_JPGC);

  if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE) {
    for(i = 0; i < PTR_JPGC->frame.Nf; i++) {
      num_blocks += PTR_JPGC->frame.num_blocks[i];
    }

    /* allocate coefficient buffer */
    tmp = DWLMallocLinear(PTR_JPGC->dwl, (sizeof(u8) * (JPEGDEC_COEFF_SIZE *
                                          num_blocks)),
                          &(PTR_JPGC->info.p_coeff_base));
    if(tmp == -1) {
      return (JPEGDEC_MEMFAIL);
    }
#ifdef PJPEG_COMPONENT_TRACE
    pjpeg_coeff_base = PTR_JPGC->info.p_coeff_base.virtual_address;
    pjpeg_coeff_size = num_blocks * JPEGDEC_COEFF_SIZE;
#endif

    JPEGDEC_TRACE_INTERNAL(("ALLOCATE: COEFF virtual %x bus %x\n",
                            (u32) PTR_JPGC->info.p_coeff_base.virtual_address,
                            PTR_JPGC->info.p_coeff_base.bus_address));
    if(PTR_JPGC->frame.Nf > 1) {
      tmp = DWLMallocLinear(PTR_JPGC->dwl, sizeof(u8) * 100,
                            &PTR_JPGC->info.tmp_strm);
      if(tmp == -1) {
        return (JPEGDEC_MEMFAIL);
      }
    }
  }

  /* QP/VLC memory size */
  if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE)
    table_size = JPEGDEC_PROGRESSIVE_TABLE_SIZE;
  else
    table_size = JPEGDEC_BASELINE_TABLE_SIZE;

  /* allocate VLC/QP table */
  if(PTR_JPGC->frame.p_table_base.virtual_address == NULL) {
    tmp =
      DWLMallocLinear(PTR_JPGC->dwl, (sizeof(u8) * table_size),
                      &(PTR_JPGC->frame.p_table_base));
    if(tmp == -1) {
      return (JPEGDEC_MEMFAIL);
    }
  }

  JPEGDEC_TRACE_INTERNAL(("ALLOCATE: VLC/QP virtual %x bus %x\n",
                          (u32) PTR_JPGC->frame.p_table_base.virtual_address,
                          PTR_JPGC->frame.p_table_base.bus_address));

  if(PTR_JPGC->pp_instance != NULL) {
    PTR_JPGC->pp_config_query.tiled_mode = 0;
    PTR_JPGC->PPConfigQuery(PTR_JPGC->pp_instance, &PTR_JPGC->pp_config_query);

    PTR_JPGC->pp_control.use_pipeline =
      PTR_JPGC->pp_config_query.pipeline_accepted;

    if(!PTR_JPGC->pp_control.use_pipeline) {
      PTR_JPGC->image.size_luma = (PTR_JPGC->info.X * PTR_JPGC->info.Y);
      if(PTR_JPGC->image.size_chroma) {
        if(PTR_JPGC->info.y_cb_cr_mode == JPEGDEC_YUV420)
          PTR_JPGC->image.size_chroma = (PTR_JPGC->image.size_luma / 2);
        else if(PTR_JPGC->info.y_cb_cr_mode == JPEGDEC_YUV422 ||
                PTR_JPGC->info.y_cb_cr_mode == JPEGDEC_YUV440)
          PTR_JPGC->image.size_chroma = PTR_JPGC->image.size_luma;
      }
    }
  }

  /* if pipelined PP -> decoder's output is not written external memory */
  if(PTR_JPGC->pp_instance == NULL ||
      (PTR_JPGC->pp_instance != NULL && !PTR_JPGC->pp_control.use_pipeline)) {
    if(PTR_JPGC->info.given_out_luma.virtual_address == NULL) {
      /* allocate luminance output */
      if(PTR_JPGC->asic_buff.out_luma_buffer.virtual_address == NULL) {
        tmp =
          DWLMallocRefFrm(PTR_JPGC->dwl, (PTR_JPGC->image.size_luma),
                          &(PTR_JPGC->asic_buff.out_luma_buffer));
        if(tmp == -1) {
          return (JPEGDEC_MEMFAIL);
        }
      }

      /* luma bus address to output */
      PTR_JPGC->info.out_luma = PTR_JPGC->asic_buff.out_luma_buffer;
    } else {
      PTR_JPGC->asic_buff.out_luma_buffer.virtual_address =
        PTR_JPGC->info.given_out_luma.virtual_address;
      PTR_JPGC->asic_buff.out_luma_buffer.bus_address =
        PTR_JPGC->info.given_out_luma.bus_address;

      /* luma bus address to output */
      PTR_JPGC->info.out_luma = PTR_JPGC->asic_buff.out_luma_buffer;

      /* flag to release */
      PTR_JPGC->info.user_alloc_mem = 1;
    }

    JPEGDEC_TRACE_INTERNAL(("ALLOCATE: Luma virtual %lx bus %lx\n",
                            PTR_JPGC->asic_buff.out_luma_buffer.
                            virtual_address,
                            PTR_JPGC->asic_buff.out_luma_buffer.bus_address));

    /* allocate chrominance output */
    if(PTR_JPGC->image.size_chroma) {
      if(PTR_JPGC->info.given_out_chroma.virtual_address == NULL) {
        if(PTR_JPGC->info.operation_type != JPEGDEC_BASELINE) {
          if(PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address == NULL) {
            tmp =
              DWLMallocRefFrm(PTR_JPGC->dwl,
                              (PTR_JPGC->image.size_chroma / 2),
                              &(PTR_JPGC->asic_buff.out_chroma_buffer));
            if(tmp == -1)
              return (JPEGDEC_MEMFAIL);
          }

          if(PTR_JPGC->asic_buff.out_chroma_buffer2.virtual_address == NULL) {
            tmp =
              DWLMallocRefFrm(PTR_JPGC->dwl,
                              (PTR_JPGC->image.size_chroma / 2),
                              &(PTR_JPGC->asic_buff.out_chroma_buffer2));
            if(tmp == -1)
              return (JPEGDEC_MEMFAIL);
          }
        } else {
          if(PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address == NULL) {
            tmp =
              DWLMallocRefFrm(PTR_JPGC->dwl,
                              (PTR_JPGC->image.size_chroma),
                              &(PTR_JPGC->asic_buff.out_chroma_buffer));
            if(tmp == -1)
              return (JPEGDEC_MEMFAIL);
          }

          PTR_JPGC->asic_buff.out_chroma_buffer2.virtual_address = NULL;
          PTR_JPGC->asic_buff.out_chroma_buffer2.bus_address = 0;
        }
      } else {
        PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address =
          PTR_JPGC->info.given_out_chroma.virtual_address;
        PTR_JPGC->asic_buff.out_chroma_buffer.bus_address =
          PTR_JPGC->info.given_out_chroma.bus_address;
        PTR_JPGC->asic_buff.out_chroma_buffer2.virtual_address =
          PTR_JPGC->info.given_out_chroma2.virtual_address;
        PTR_JPGC->asic_buff.out_chroma_buffer2.bus_address =
          PTR_JPGC->info.given_out_chroma2.bus_address;

      }

      /* chroma bus address to output */
      PTR_JPGC->info.out_chroma = PTR_JPGC->asic_buff.out_chroma_buffer;
      PTR_JPGC->info.out_chroma2 = PTR_JPGC->asic_buff.out_chroma_buffer2;

      JPEGDEC_TRACE_INTERNAL(("ALLOCATE: Chroma virtual %lx bus %lx\n",
                              PTR_JPGC->asic_buff.out_chroma_buffer.
                              virtual_address,
                              PTR_JPGC->asic_buff.out_chroma_buffer.
                              bus_address));
    }
  }

#ifdef JPEGDEC_RESET_OUTPUT
  {
    (void) DWLmemset(PTR_JPGC->asic_buff.out_luma_buffer.virtual_address,
                     128, PTR_JPGC->image.size_luma);
    if(PTR_JPGC->image.size_chroma) {
      if(PTR_JPGC->info.operation_type != JPEGDEC_BASELINE) {
        (void) DWLmemset(PTR_JPGC->asic_buff.out_chroma_buffer.
                         virtual_address, 128,
                         PTR_JPGC->image.size_chroma / 2);
        (void) DWLmemset(PTR_JPGC->asic_buff.out_chroma_buffer2.
                         virtual_address, 128,
                         PTR_JPGC->image.size_chroma / 2);
      } else
        (void) DWLmemset(PTR_JPGC->asic_buff.out_chroma_buffer.
                         virtual_address, 128,
                         PTR_JPGC->image.size_chroma);
    }
    (void) DWLmemset(PTR_JPGC->frame.p_table_base.virtual_address, 0,
                     (sizeof(u8) * table_size));
    if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE) {
      (void) DWLmemset(PTR_JPGC->info.p_coeff_base.virtual_address, 0,
                       (sizeof(u8) * JPEGDEC_COEFF_SIZE * num_blocks));
    }
  }
#endif /* #ifdef JPEGDEC_RESET_OUTPUT */

  return JPEGDEC_OK;

#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

        Function name: JpegDecSliceSizeCalculation

        Functional description:
          Calculates slice size

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
void JpegDecSliceSizeCalculation(JpegDecContainer * jpeg_dec_cont) {
#define PTR_JPGC   jpeg_dec_cont

  if(((PTR_JPGC->info.SliceCount +
       1) * (PTR_JPGC->info.slice_mb_set_value * 16)) > PTR_JPGC->info.Y) {
    PTR_JPGC->info.slice_height = ((PTR_JPGC->info.Y / 16) -
                                   (PTR_JPGC->info.SliceCount *
                                    PTR_JPGC->info.slice_height));
  } else {
    /* TODO! other sampling formats also than YUV420 */
    if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE &&
        PTR_JPGC->info.component_id != 0)
      PTR_JPGC->info.slice_height = PTR_JPGC->info.slice_mb_set_value / 2;
    else
      PTR_JPGC->info.slice_height = PTR_JPGC->info.slice_mb_set_value;
  }
}

/*------------------------------------------------------------------------------

        Function name: JpegDecWriteTables

        Functional description:
          Writes q/ac/dc tables to the HW format as specified in HW regs

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecWriteTables(JpegDecContainer * jpeg_dec_cont) {

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_VLC     jpeg_dec_cont->vlc
#define JPG_QTB     jpeg_dec_cont->quant
#define JPG_FRM     jpeg_dec_cont->frame

  u32 i, j = 0;
  u32 shifter = 32;
  u32 table_word = 0;
  u32 table_value = 0;
  u8 table_tmp[64] = { 0 };
  u32 *p_table_base = NULL;

  ASSERT(PTR_JPGC);
  ASSERT(PTR_JPGC->frame.p_table_base.virtual_address);
  ASSERT(PTR_JPGC->frame.p_table_base.bus_address);
  ASSERT(PTR_JPGC->frame.p_table_base.size);

  p_table_base = PTR_JPGC->frame.p_table_base.virtual_address;

  /* QP tables for all components */
  for(j = 0; j < PTR_JPGC->info.amount_of_qtables; j++) {
    if((JPG_FRM.component[j].Tq) == 0) {
      for(i = 0; i < 64; i++) {
        table_tmp[zz_order[i]] = (u8) JPG_QTB.table0[i];
      }

      /* update shifter */
      shifter = 32;

      for(i = 0; i < 64; i++) {
        shifter -= 8;

        if(shifter == 24)
          table_word = (table_tmp[i] << shifter);
        else
          table_word |= (table_tmp[i] << shifter);

        if(shifter == 0) {
          *(p_table_base) = table_word;
          p_table_base++;
          shifter = 32;
        }
      }
    } else {
      for(i = 0; i < 64; i++) {
        table_tmp[zz_order[i]] = (u8) JPG_QTB.table1[i];
      }

      /* update shifter */
      shifter = 32;

      for(i = 0; i < 64; i++) {
        shifter -= 8;

        if(shifter == 24)
          table_word = (table_tmp[i] << shifter);
        else
          table_word |= (table_tmp[i] << shifter);

        if(shifter == 0) {
          *(p_table_base) = table_word;
          p_table_base++;
          shifter = 32;
        }
      }
    }
  }

  /* update shifter */
  shifter = 32;

  if(PTR_JPGC->info.y_cb_cr_mode != JPEGDEC_YUV400) {
    /* this trick is done because hw always wants luma table as ac hw table 1 */
    if(JPG_SCN.Ta[0] == 0) {
      /* Write AC Table 1 (as specified in HW regs)
       * NOTE: Not the same as actable[1] (as specified in JPEG Spec) */
      JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write tables: AC1 (luma)\n"));
      if(JPG_VLC.ac_table0.vals) {
        for(i = 0; i < 162; i++) {
          if(i < JPG_VLC.ac_table0.table_length) {
            table_value = (u8) JPG_VLC.ac_table0.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 162; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }
      /* Write AC Table 2 */
      JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write tables: AC2 (not-luma)\n"));
      if(JPG_VLC.ac_table1.vals) {
        for(i = 0; i < 162; i++) {
          if(i < JPG_VLC.ac_table1.table_length) {
            table_value = (u8) JPG_VLC.ac_table1.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 162; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }
    } else {
      /* Write AC Table 1 (as specified in HW regs)
       * NOTE: Not the same as actable[1] (as specified in JPEG Spec) */

      if(JPG_VLC.ac_table1.vals) {
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write tables: AC1 (luma)\n"));
        for(i = 0; i < 162; i++) {
          if(i < JPG_VLC.ac_table1.table_length) {
            table_value = (u8) JPG_VLC.ac_table1.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 162; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }

      /* Write AC Table 2 */

      if(JPG_VLC.ac_table0.vals) {
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: write_tables: AC2 (not-luma)\n"));
        for(i = 0; i < 162; i++) {
          if(i < JPG_VLC.ac_table0.table_length) {
            table_value = (u8) JPG_VLC.ac_table0.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 162; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }
    }

    /* this trick is done because hw always wants luma table as dc hw table 1 */
    if(JPG_SCN.Td[0] == 0) {
      if(JPG_VLC.dc_table0.vals) {
        for(i = 0; i < 12; i++) {
          if(i < JPG_VLC.dc_table0.table_length) {
            table_value = (u8) JPG_VLC.dc_table0.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 12; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }

      if(JPG_VLC.dc_table1.vals) {
        for(i = 0; i < 12; i++) {
          if(i < JPG_VLC.dc_table1.table_length) {
            table_value = (u8) JPG_VLC.dc_table1.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 12; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }

    } else {
      if(JPG_VLC.dc_table1.vals) {
        for(i = 0; i < 12; i++) {
          if(i < JPG_VLC.dc_table1.table_length) {
            table_value = (u8) JPG_VLC.dc_table1.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 12; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }

      if(JPG_VLC.dc_table0.vals) {
        for(i = 0; i < 12; i++) {
          if(i < JPG_VLC.dc_table0.table_length) {
            table_value = (u8) JPG_VLC.dc_table0.vals[i];
          } else {
            table_value = 0;
          }

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        for(i = 0; i < 12; i++) {
          table_word = 0;

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }
    }
  } else { /* YUV400 */
    if(!PTR_JPGC->info.non_interleaved_scan_ready) {
      /* this trick is done because hw always wants luma table as ac hw table 1 */
      if(JPG_SCN.Ta[0] == 0) {
        /* Write AC Table 1 (as specified in HW regs)
         * NOTE: Not the same as actable[1] (as specified in JPEG Spec) */
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write tables: AC1 (luma)\n"));
        if(JPG_VLC.ac_table0.vals) {
          for(i = 0; i < 162; i++) {
            if(i < JPG_VLC.ac_table0.table_length) {
              table_value = (u8) JPG_VLC.ac_table0.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 162; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        /* Write AC Table 2 */
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write zero table (YUV400): \n"));
        for(i = 0; i < 162; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        /* Write AC Table 1 (as specified in HW regs)
         * NOTE: Not the same as actable[1] (as specified in JPEG Spec) */

        if(JPG_VLC.ac_table1.vals) {
          JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write tables: AC1 (luma)\n"));
          for(i = 0; i < 162; i++) {
            if(i < JPG_VLC.ac_table1.table_length) {
              table_value = (u8) JPG_VLC.ac_table1.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 162; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        /* Write AC Table 2 */
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: write_tables: padding zero (YUV400)\n"));
        for(i = 0; i < 162; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }

      /* this trick is done because hw always wants luma table as dc hw table 1 */
      if(JPG_SCN.Td[0] == 0) {
        if(JPG_VLC.dc_table0.vals) {
          for(i = 0; i < 12; i++) {
            if(i < JPG_VLC.dc_table0.table_length) {
              table_value = (u8) JPG_VLC.dc_table0.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 12; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        for(i = 0; i < 12; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        if(JPG_VLC.dc_table1.vals) {
          for(i = 0; i < 12; i++) {
            if(i < JPG_VLC.dc_table1.table_length) {
              table_value = (u8) JPG_VLC.dc_table1.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 12; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        for(i = 0; i < 12; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }
    } else {
      /* this trick is done because hw always wants luma table as ac hw table 1 */
      if(JPG_SCN.Ta[PTR_JPGC->info.component_id] == 0) {
        /* Write AC Table 1 (as specified in HW regs)
         * NOTE: Not the same as actable[1] (as specified in JPEG Spec) */
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write tables: AC1 (luma)\n"));
        if(JPG_VLC.ac_table0.vals) {
          for(i = 0; i < 162; i++) {
            if(i < JPG_VLC.ac_table0.table_length) {
              table_value = (u8) JPG_VLC.ac_table0.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 162; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        /* Write AC Table 2 */
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write zero table (YUV400): \n"));
        for(i = 0; i < 162; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        /* Write AC Table 1 (as specified in HW regs)
         * NOTE: Not the same as actable[1] (as specified in JPEG Spec) */

        if(JPG_VLC.ac_table1.vals) {
          JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write tables: AC1 (luma)\n"));
          for(i = 0; i < 162; i++) {
            if(i < JPG_VLC.ac_table1.table_length) {
              table_value = (u8) JPG_VLC.ac_table1.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 162; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        /* Write AC Table 2 */
        JPEGDEC_TRACE_INTERNAL(("INTERNAL: write_tables: padding zero (YUV400)\n"));
        for(i = 0; i < 162; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }

      /* this trick is done because hw always wants luma table as dc hw table 1 */
      if(JPG_SCN.Td[PTR_JPGC->info.component_id] == 0) {
        if(JPG_VLC.dc_table0.vals) {
          for(i = 0; i < 12; i++) {
            if(i < JPG_VLC.dc_table0.table_length) {
              table_value = (u8) JPG_VLC.dc_table0.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 12; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        for(i = 0; i < 12; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      } else {
        if(JPG_VLC.dc_table1.vals) {
          for(i = 0; i < 12; i++) {
            if(i < JPG_VLC.dc_table1.table_length) {
              table_value = (u8) JPG_VLC.dc_table1.vals[i];
            } else {
              table_value = 0;
            }

            if(shifter == 32)
              table_word = (table_value << (shifter - 8));
            else
              table_word |= (table_value << (shifter - 8));

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        } else {
          for(i = 0; i < 12; i++) {
            table_word = 0;

            shifter -= 8;

            if(shifter == 0) {
              *(p_table_base) = table_word;
              p_table_base++;
              shifter = 32;
            }
          }
        }

        for(i = 0; i < 12; i++) {
          table_value = 0;

          if(shifter == 32)
            table_word = (table_value << (shifter - 8));
          else
            table_word |= (table_value << (shifter - 8));

          shifter -= 8;

          if(shifter == 0) {
            *(p_table_base) = table_word;
            p_table_base++;
            shifter = 32;
          }
        }
      }
    }

  }

  for(i = 0; i < 4; i++) {
    table_value = 0;

    if(shifter == 32)
      table_word = (table_value << (shifter - 8));
    else
      table_word |= (table_value << (shifter - 8));

    shifter -= 8;

    if(shifter == 0) {
      *(p_table_base) = table_word;
      p_table_base++;
      shifter = 32;
    }
  }

#undef JPG_SCN
#undef JPG_VLC
#undef JPG_QTB
#undef JPG_FRM
#undef PTR_JPGC

}

/*------------------------------------------------------------------------------
        Function name: JpegDecWriteTablesNonInterleaved

        Functional description:
          Writes q/ac/dc tables to the HW format as specified in HW regs

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecWriteTablesNonInterleaved(JpegDecContainer * jpeg_dec_cont) {

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_VLC     jpeg_dec_cont->vlc
#define JPG_QTB     jpeg_dec_cont->quant
#define JPG_FRM     jpeg_dec_cont->frame

  u32 i, j = 0;
  u32 table_word = 0;
  u8 table_tmp[64] = { 0 };
  u8 *p_tmp;
  u32 *p_table_base = NULL;
  u32 first, count;
  u32 len, num_words;
  u32 *vals;
  u32 *p_table;
  u32 qp_table_base = 0;

  ASSERT(PTR_JPGC);
  ASSERT(PTR_JPGC->frame.p_table_base.virtual_address);
  ASSERT(PTR_JPGC->frame.p_table_base.bus_address);
  ASSERT(PTR_JPGC->frame.p_table_base.size);
  ASSERT(PTR_JPGC->info.non_interleaved);

  /* Reset the table memory */
  (void) DWLmemset(PTR_JPGC->frame.p_table_base.virtual_address, 0,
                   (sizeof(u8) * JPEGDEC_BASELINE_TABLE_SIZE));

  p_table_base = PTR_JPGC->frame.p_table_base.virtual_address;

  first = PTR_JPGC->info.component_id;
  count = 1;

  /* QP tables for all components */
  for(j = first; j < first + count; j++) {
    if((JPG_FRM.component[j].Tq) == 0)
      p_table = JPG_QTB.table0;
    else
      p_table = JPG_QTB.table1;

    for(i = 0; i < 64; i++) {
      table_tmp[zz_order[i]] = (u8) p_table[i];
    }

    p_tmp = table_tmp;
    for(i = 0; i < 16; i++) {
      table_word = (p_tmp[0] << 24) | (p_tmp[1] << 16) |
                   (p_tmp[2] << 8) | (p_tmp[3] << 0);;

      *p_table_base++ = table_word;
      p_tmp += 4;
    }
  }

  /* AC table */
  for(i = first; i < first + count; i++) {
    num_words = 162;
    switch (JPG_SCN.Ta[i]) {
    case 0:
      vals = JPG_VLC.ac_table0.vals;
      len = JPG_VLC.ac_table0.table_length;
      break;
    case 1:
      vals = JPG_VLC.ac_table1.vals;
      len = JPG_VLC.ac_table1.table_length;
      break;
    case 2:
      vals = JPG_VLC.ac_table2.vals;
      len = JPG_VLC.ac_table2.table_length;
      break;
    default:
      vals = JPG_VLC.ac_table3.vals;
      len = JPG_VLC.ac_table3.table_length;
      break;
    }

    /* set pointer */
    if(count == 3)
      qp_table_base = 0;
    else
      qp_table_base = JPEGDEC_QP_BASE;

    p_table_base =
      &PTR_JPGC->frame.p_table_base.virtual_address[JPEGDEC_AC1_BASE -
          qp_table_base];

    for(j = 0; j < num_words; j++) {
      table_word <<= 8;
      if(j < len)
        table_word |= vals[j];

      if((j & 0x3) == 0x3)
        *p_table_base++ = table_word;
    }

    /* fill to border */
    num_words = 164;
    len = 164;
    for(j = 162; j < num_words; j++) {
      table_word <<= 8;
      if(j < len)
        table_word |= 0;

      if((j & 0x3) == 0x3)
        *p_table_base++ = table_word;
    }
  }

  /* DC table */
  for(i = first; i < first + count; i++) {
    num_words = 12;
    switch (JPG_SCN.Td[i]) {
    case 0:
      vals = JPG_VLC.dc_table0.vals;
      len = JPG_VLC.dc_table0.table_length;
      break;
    case 1:
      vals = JPG_VLC.dc_table1.vals;
      len = JPG_VLC.dc_table1.table_length;
      break;
    case 2:
      vals = JPG_VLC.dc_table2.vals;
      len = JPG_VLC.dc_table2.table_length;
      break;
    default:
      vals = JPG_VLC.dc_table3.vals;
      len = JPG_VLC.dc_table3.table_length;
      break;
    }

    /* set pointer */
    if(count == 3)
      qp_table_base = 0;
    else
      qp_table_base = JPEGDEC_QP_BASE;

    p_table_base =
      &PTR_JPGC->frame.p_table_base.virtual_address[JPEGDEC_DC1_BASE -
          qp_table_base];

    for(j = 0; j < num_words; j++) {
      table_word <<= 8;
      if(j < len)
        table_word |= vals[j];

      if((j & 0x3) == 0x3)
        *p_table_base++ = table_word;
    }
  }

  *p_table_base = 0;

#undef JPG_SCN
#undef JPG_VLC
#undef JPG_QTB
#undef JPG_FRM
#undef PTR_JPGC

}

/*------------------------------------------------------------------------------

        Function name: JpegDecWriteTablesProgressive

        Functional description:
          Writes q/ac/dc tables to the HW format as specified in HW regs

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecWriteTablesProgressive(JpegDecContainer * jpeg_dec_cont) {

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_VLC     jpeg_dec_cont->vlc
#define JPG_QTB     jpeg_dec_cont->quant
#define JPG_FRM     jpeg_dec_cont->frame

  u32 i, j = 0;
  u32 table_word = 0;
  u8 table_tmp[64] = { 0 };
  u8 *p_tmp;
  u32 *p_table_base = NULL;
  u32 first, count;
  u32 len, num_words;
  u32 *vals;
  u32 *p_table;
  u32 dc_table = 0;
  u32 qp_table_base = 0;

  ASSERT(PTR_JPGC);
  ASSERT(PTR_JPGC->frame.p_table_base.virtual_address);
  ASSERT(PTR_JPGC->frame.p_table_base.bus_address);
  ASSERT(PTR_JPGC->frame.p_table_base.size);

  /* Reset the table memory */
  (void) DWLmemset(PTR_JPGC->frame.p_table_base.virtual_address, 0,
                   (sizeof(u8) * JPEGDEC_PROGRESSIVE_TABLE_SIZE));

  p_table_base = PTR_JPGC->frame.p_table_base.virtual_address;

  if(PTR_JPGC->info.non_interleaved) {
    first = PTR_JPGC->info.component_id;
    count = 1;
  } else {
    first = 0;
    count = 3;
  }

  /* QP tables for all components */
  for(j = first; j < first + count; j++) {
    if((JPG_FRM.component[j].Tq) == 0)
      p_table = JPG_QTB.table0;
    else
      p_table = JPG_QTB.table1;

    for(i = 0; i < 64; i++) {
      table_tmp[zz_order[i]] = (u8) p_table[i];
    }

    p_tmp = table_tmp;
    for(i = 0; i < 16; i++) {
      table_word = (p_tmp[0] << 24) | (p_tmp[1] << 16) |
                   (p_tmp[2] << 8) | (p_tmp[3] << 0);;

      *p_table_base++ = table_word;
      p_tmp += 4;
    }
  }

  /* if later stage DC ==> no need for table */
  if(PTR_JPGC->scan.Ah != 0 && PTR_JPGC->scan.Ss == 0)
    return;

  for(i = first; i < first + count; i++) {
    if(PTR_JPGC->scan.Ss == 0) { /* DC */
      dc_table = 1;
      num_words = 12;
      switch (JPG_SCN.Td[i]) {
      case 0:
        vals = JPG_VLC.dc_table0.vals;
        len = JPG_VLC.dc_table0.table_length;
        break;
      case 1:
        vals = JPG_VLC.dc_table1.vals;
        len = JPG_VLC.dc_table1.table_length;
        break;
      case 2:
        vals = JPG_VLC.dc_table2.vals;
        len = JPG_VLC.dc_table2.table_length;
        break;
      default:
        vals = JPG_VLC.dc_table3.vals;
        len = JPG_VLC.dc_table3.table_length;
        break;
      }
    } else {
      num_words = 162;
      switch (JPG_SCN.Ta[i]) {
      case 0:
        vals = JPG_VLC.ac_table0.vals;
        len = JPG_VLC.ac_table0.table_length;
        break;
      case 1:
        vals = JPG_VLC.ac_table1.vals;
        len = JPG_VLC.ac_table1.table_length;
        break;
      case 2:
        vals = JPG_VLC.ac_table2.vals;
        len = JPG_VLC.ac_table2.table_length;
        break;
      default:
        vals = JPG_VLC.ac_table3.vals;
        len = JPG_VLC.ac_table3.table_length;
        break;
      }
    }

    /* set pointer */
    if(count == 3)
      qp_table_base = 0;
    else
      qp_table_base = JPEGDEC_QP_BASE;

    if(dc_table) {
      /* interleaved || non-interleaved */
      if(count == 3) {
        if(i == 0)
          p_table_base =
            &PTR_JPGC->frame.p_table_base.
            virtual_address[JPEGDEC_DC1_BASE - qp_table_base];
        else if(i == 1)
          p_table_base =
            &PTR_JPGC->frame.p_table_base.
            virtual_address[JPEGDEC_DC2_BASE - qp_table_base];
        else
          p_table_base =
            &PTR_JPGC->frame.p_table_base.
            virtual_address[JPEGDEC_DC3_BASE - qp_table_base];
      } else {
        p_table_base =
          &PTR_JPGC->frame.p_table_base.
          virtual_address[JPEGDEC_DC1_BASE - qp_table_base];
      }
    } else {
      p_table_base =
        &PTR_JPGC->frame.p_table_base.virtual_address[JPEGDEC_AC1_BASE -
            qp_table_base];
    }

    for(j = 0; j < num_words; j++) {
      table_word <<= 8;
      if(j < len)
        table_word |= vals[j];

      if((j & 0x3) == 0x3)
        *p_table_base++ = table_word;
    }

    /* fill to border */
    if(i == 0 && dc_table == 0) {
      num_words = 164;
      len = 164;
      for(j = 162; j < num_words; j++) {
        table_word <<= 8;
        if(j < len)
          table_word |= 0;

        if((j & 0x3) == 0x3)
          *p_table_base++ = table_word;
      }
    }

    /* reset */
    dc_table = 0;
  }

  *p_table_base = 0;

#undef JPG_SCN
#undef JPG_VLC
#undef JPG_QTB
#undef JPG_FRM
#undef PTR_JPGC

}

/*------------------------------------------------------------------------------

        Function name: JpegDecChromaTableSelectors

        Functional description:
          select what tables chromas use

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecChromaTableSelectors(JpegDecContainer * jpeg_dec_cont) {

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_FRM     jpeg_dec_cont->frame

  /* this trick is done because hw always wants luma table as ac hw table 1 */
  if(JPG_SCN.Ta[0] == 0) {
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_AC_VLCTABLE, JPG_SCN.Ta[2]);
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_AC_VLCTABLE, JPG_SCN.Ta[1]);
  } else {
    if(JPG_SCN.Ta[0] == JPG_SCN.Ta[1])
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_AC_VLCTABLE, 0);
    else
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_AC_VLCTABLE, 1);

    if(JPG_SCN.Ta[0] == JPG_SCN.Ta[2])
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_AC_VLCTABLE, 0);
    else
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_AC_VLCTABLE, 1);
  }

  /* Third DC table selectors */
  if(PTR_JPGC->info.operation_type != JPEGDEC_PROGRESSIVE) {
    if(JPG_SCN.Td[0] == 0) {
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE,
                     JPG_SCN.Td[2]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE,
                     JPG_SCN.Td[1]);
    } else {
      if(JPG_SCN.Td[0] == JPG_SCN.Td[1])
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE, 0);
      else
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE, 1);

      if(JPG_SCN.Td[0] == JPG_SCN.Td[2])
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE, 0);
      else
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE, 1);
    }

    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE3, 0);
    SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE3, 0);
  } else {
    /* if non-interleaved ==> decoding mode YUV400, uses table zero (0) */
    if(PTR_JPGC->info.non_interleaved) {
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE3, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE3, 0);
    } else {
      /* if later stage DC ==> no need for table */
      if(PTR_JPGC->scan.Ah != 0 && PTR_JPGC->scan.Ss == 0) {
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE3, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE3, 0);
      } else {
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CR_DC_VLCTABLE3, 1);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE, 1);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_CB_DC_VLCTABLE3, 0);
      }
    }
  }

  return;

#undef JPG_SCN
#undef JPG_FRM
#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

        Function name: JpegDecWriteLenBits

        Functional description:
          tell hw how many vlc words of different lengths we have

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecWriteLenBits(JpegDecContainer * jpeg_dec_cont) {

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_VLC     jpeg_dec_cont->vlc
#define JPG_QTB     jpeg_dec_cont->quant
#define JPG_FRM     jpeg_dec_cont->frame

  VlcTable *p_table1 = NULL;
  VlcTable *p_table2 = NULL;

  /* first select the table we'll use */

  /* this trick is done because hw always wants luma table as ac hw table 1 */
  if(JPG_SCN.Ta[0] == 0) {

    p_table1 = &(JPG_VLC.ac_table0);
    p_table2 = &(JPG_VLC.ac_table1);

  } else {

    p_table1 = &(JPG_VLC.ac_table1);
    p_table2 = &(JPG_VLC.ac_table0);
  }

  ASSERT(p_table1);
  ASSERT(p_table2);

  /* write AC table 1 (luma) */

  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE1_CNT, p_table1->bits[0]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE2_CNT, p_table1->bits[1]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE3_CNT, p_table1->bits[2]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE4_CNT, p_table1->bits[3]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE5_CNT, p_table1->bits[4]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE6_CNT, p_table1->bits[5]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE7_CNT, p_table1->bits[6]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE8_CNT, p_table1->bits[7]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE9_CNT, p_table1->bits[8]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE10_CNT, p_table1->bits[9]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE11_CNT, p_table1->bits[10]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE12_CNT, p_table1->bits[11]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE13_CNT, p_table1->bits[12]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE14_CNT, p_table1->bits[13]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE15_CNT, p_table1->bits[14]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE16_CNT, p_table1->bits[15]);

  /* table AC2 (the not-luma table) */
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE1_CNT, p_table2->bits[0]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE2_CNT, p_table2->bits[1]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE3_CNT, p_table2->bits[2]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE4_CNT, p_table2->bits[3]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE5_CNT, p_table2->bits[4]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE6_CNT, p_table2->bits[5]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE7_CNT, p_table2->bits[6]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE8_CNT, p_table2->bits[7]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE9_CNT, p_table2->bits[8]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE10_CNT, p_table2->bits[9]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE11_CNT, p_table2->bits[10]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE12_CNT, p_table2->bits[11]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE13_CNT, p_table2->bits[12]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE14_CNT, p_table2->bits[13]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE15_CNT, p_table2->bits[14]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC2_CODE16_CNT, p_table2->bits[15]);

  if(JPG_SCN.Td[0] == 0) {

    p_table1 = &(JPG_VLC.dc_table0);
    p_table2 = &(JPG_VLC.dc_table1);

  } else {

    p_table1 = &(JPG_VLC.dc_table1);
    p_table2 = &(JPG_VLC.dc_table0);
  }

  ASSERT(p_table1);
  ASSERT(p_table2);

  /* write DC table 1 (luma) */
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE1_CNT, p_table1->bits[0]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE2_CNT, p_table1->bits[1]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE3_CNT, p_table1->bits[2]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE4_CNT, p_table1->bits[3]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE5_CNT, p_table1->bits[4]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE6_CNT, p_table1->bits[5]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE7_CNT, p_table1->bits[6]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE8_CNT, p_table1->bits[7]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE9_CNT, p_table1->bits[8]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE10_CNT, p_table1->bits[9]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE11_CNT, p_table1->bits[10]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE12_CNT, p_table1->bits[11]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE13_CNT, p_table1->bits[12]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE14_CNT, p_table1->bits[13]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE15_CNT, p_table1->bits[14]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE16_CNT, p_table1->bits[15]);

  /* table DC2 (the not-luma table) */

  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE1_CNT, p_table2->bits[0]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE2_CNT, p_table2->bits[1]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE3_CNT, p_table2->bits[2]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE4_CNT, p_table2->bits[3]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE5_CNT, p_table2->bits[4]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE6_CNT, p_table2->bits[5]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE7_CNT, p_table2->bits[6]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE8_CNT, p_table2->bits[7]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE9_CNT, p_table2->bits[8]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE10_CNT, p_table2->bits[9]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE11_CNT, p_table2->bits[10]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE12_CNT, p_table2->bits[11]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE13_CNT, p_table2->bits[12]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE14_CNT, p_table2->bits[13]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE15_CNT, p_table2->bits[14]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE16_CNT, p_table2->bits[15]);

  return;

#undef JPG_SCN
#undef JPG_VLC
#undef JPG_QTB
#undef JPG_FRM
#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

        Function name: JpegDecWriteLenBitsNonInterleaved

        Functional description:
          tell hw how many vlc words of different lengths we have

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecWriteLenBitsNonInterleaved(JpegDecContainer * jpeg_dec_cont) {

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_VLC     jpeg_dec_cont->vlc
#define JPG_QTB     jpeg_dec_cont->quant
#define JPG_FRM     jpeg_dec_cont->frame

  VlcTable *p_table1 = NULL;
  VlcTable *p_table2 = NULL;
#ifdef NDEBUG
  UNUSED(p_table2);
#endif

  /* first select the table we'll use */

  /* this trick is done because hw always wants luma table as ac hw table 1 */
  if(JPG_SCN.Ta[PTR_JPGC->info.component_id] == 0) {

    p_table1 = &(JPG_VLC.ac_table0);
    p_table2 = &(JPG_VLC.ac_table1);

  } else {

    p_table1 = &(JPG_VLC.ac_table1);
    p_table2 = &(JPG_VLC.ac_table0);
  }

  ASSERT(p_table1);
  ASSERT(p_table2);

  /* write AC table 1 (luma) */

  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE1_CNT, p_table1->bits[0]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE2_CNT, p_table1->bits[1]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE3_CNT, p_table1->bits[2]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE4_CNT, p_table1->bits[3]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE5_CNT, p_table1->bits[4]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE6_CNT, p_table1->bits[5]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE7_CNT, p_table1->bits[6]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE8_CNT, p_table1->bits[7]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE9_CNT, p_table1->bits[8]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE10_CNT, p_table1->bits[9]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE11_CNT, p_table1->bits[10]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE12_CNT, p_table1->bits[11]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE13_CNT, p_table1->bits[12]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE14_CNT, p_table1->bits[13]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE15_CNT, p_table1->bits[14]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE16_CNT, p_table1->bits[15]);

  if(JPG_SCN.Td[PTR_JPGC->info.component_id] == 0) {

    p_table1 = &(JPG_VLC.dc_table0);
    p_table2 = &(JPG_VLC.dc_table1);

  } else {

    p_table1 = &(JPG_VLC.dc_table1);
    p_table2 = &(JPG_VLC.dc_table0);
  }

  ASSERT(p_table1);
  ASSERT(p_table2);

  /* write DC table 1 (luma) */
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE1_CNT, p_table1->bits[0]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE2_CNT, p_table1->bits[1]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE3_CNT, p_table1->bits[2]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE4_CNT, p_table1->bits[3]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE5_CNT, p_table1->bits[4]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE6_CNT, p_table1->bits[5]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE7_CNT, p_table1->bits[6]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE8_CNT, p_table1->bits[7]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE9_CNT, p_table1->bits[8]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE10_CNT, p_table1->bits[9]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE11_CNT, p_table1->bits[10]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE12_CNT, p_table1->bits[11]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE13_CNT, p_table1->bits[12]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE14_CNT, p_table1->bits[13]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE15_CNT, p_table1->bits[14]);
  SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE16_CNT, p_table1->bits[15]);

  return;

#undef JPG_SCN
#undef JPG_VLC
#undef JPG_QTB
#undef JPG_FRM
#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

        Function name: JpegDecWriteLenBitsProgressive

        Functional description:
          tell hw how many vlc words of different lengths we have

        Inputs:
          JpegDecContainer *jpeg_dec_cont

        Outputs:
          void

------------------------------------------------------------------------------*/
static void JpegDecWriteLenBitsProgressive(JpegDecContainer * jpeg_dec_cont) {

  u32 i;

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_VLC     jpeg_dec_cont->vlc
#define JPG_QTB     jpeg_dec_cont->quant
#define JPG_FRM     jpeg_dec_cont->frame

  VlcTable *p_table1 = NULL;
  VlcTable *p_table2 = NULL;
  VlcTable *p_table3 = NULL;

  /* reset swregs that contains vlc length information: swregs [16-28] */
  for(i = JPEGDEC_VLC_LEN_START_REG; i < JPEGDEC_VLC_LEN_END_REG; i++)
    PTR_JPGC->jpeg_regs[i] = 0;

  /* check if interleaved scan ==> only one table needed */
  if(PTR_JPGC->info.non_interleaved) {
    /* check if AC or DC coefficient scan */
    if(PTR_JPGC->scan.Ss == 0) { /* DC */
      /* check component ID */
      if(PTR_JPGC->info.component_id == 0) {
        if(JPG_SCN.Td[0] == 0)
          p_table1 = &(JPG_VLC.dc_table0);
        else if(JPG_SCN.Td[0] == 1)
          p_table1 = &(JPG_VLC.dc_table1);
        else if(JPG_SCN.Td[0] == 2)
          p_table1 = &(JPG_VLC.dc_table2);
        else
          p_table1 = &(JPG_VLC.dc_table3);
      } else if(PTR_JPGC->info.component_id == 1) {
        if(JPG_SCN.Td[1] == 0)
          p_table1 = &(JPG_VLC.dc_table0);
        else if(JPG_SCN.Td[1] == 1)
          p_table1 = &(JPG_VLC.dc_table1);
        else if(JPG_SCN.Td[1] == 2)
          p_table1 = &(JPG_VLC.dc_table2);
        else
          p_table1 = &(JPG_VLC.dc_table3);
      } else {
        if(JPG_SCN.Td[2] == 0)
          p_table1 = &(JPG_VLC.dc_table0);
        else if(JPG_SCN.Td[2] == 1)
          p_table1 = &(JPG_VLC.dc_table1);
        else if(JPG_SCN.Td[2] == 2)
          p_table1 = &(JPG_VLC.dc_table2);
        else
          p_table1 = &(JPG_VLC.dc_table3);
      }

      ASSERT(p_table1);

      /* if later stage DC ==> no need for table */
      if(PTR_JPGC->scan.Ah == 0) {
        /* write DC table 1 */
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE1_CNT,
                       p_table1->bits[0]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE2_CNT,
                       p_table1->bits[1]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE3_CNT,
                       p_table1->bits[2]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE4_CNT,
                       p_table1->bits[3]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE5_CNT,
                       p_table1->bits[4]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE6_CNT,
                       p_table1->bits[5]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE7_CNT,
                       p_table1->bits[6]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE8_CNT,
                       p_table1->bits[7]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE9_CNT,
                       p_table1->bits[8]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE10_CNT,
                       p_table1->bits[9]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE11_CNT,
                       p_table1->bits[10]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE12_CNT,
                       p_table1->bits[11]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE13_CNT,
                       p_table1->bits[12]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE14_CNT,
                       p_table1->bits[13]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE15_CNT,
                       p_table1->bits[14]);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE16_CNT,
                       p_table1->bits[15]);
      } else {
        /* write zero table */
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE1_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE2_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE3_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE4_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE5_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE6_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE7_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE8_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE9_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE10_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE11_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE12_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE13_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE14_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE15_CNT, 0);
        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE16_CNT, 0);
      }

    } else { /* AC */
      /* check component ID */
      if(PTR_JPGC->info.component_id == 0) {
        if(JPG_SCN.Ta[0] == 0)
          p_table1 = &(JPG_VLC.ac_table0);
        else if(JPG_SCN.Ta[0] == 1)
          p_table1 = &(JPG_VLC.ac_table1);
        else if(JPG_SCN.Ta[0] == 2)
          p_table1 = &(JPG_VLC.ac_table2);
        else
          p_table1 = &(JPG_VLC.ac_table3);
      } else if(PTR_JPGC->info.component_id == 1) {
        if(JPG_SCN.Ta[1] == 0)
          p_table1 = &(JPG_VLC.ac_table0);
        else if(JPG_SCN.Ta[1] == 1)
          p_table1 = &(JPG_VLC.ac_table1);
        else if(JPG_SCN.Ta[1] == 2)
          p_table1 = &(JPG_VLC.ac_table2);
        else
          p_table1 = &(JPG_VLC.ac_table3);
      } else {
        if(JPG_SCN.Ta[2] == 0)
          p_table1 = &(JPG_VLC.ac_table0);
        else if(JPG_SCN.Ta[2] == 1)
          p_table1 = &(JPG_VLC.ac_table1);
        else if(JPG_SCN.Ta[2] == 2)
          p_table1 = &(JPG_VLC.ac_table2);
        else
          p_table1 = &(JPG_VLC.ac_table3);
      }

      ASSERT(p_table1);

      /* write AC table 1 */
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE1_CNT,
                     p_table1->bits[0]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE2_CNT,
                     p_table1->bits[1]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE3_CNT,
                     p_table1->bits[2]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE4_CNT,
                     p_table1->bits[3]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE5_CNT,
                     p_table1->bits[4]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE6_CNT,
                     p_table1->bits[5]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE7_CNT,
                     p_table1->bits[6]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE8_CNT,
                     p_table1->bits[7]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE9_CNT,
                     p_table1->bits[8]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE10_CNT,
                     p_table1->bits[9]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE11_CNT,
                     p_table1->bits[10]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE12_CNT,
                     p_table1->bits[11]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE13_CNT,
                     p_table1->bits[12]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE14_CNT,
                     p_table1->bits[13]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE15_CNT,
                     p_table1->bits[14]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_AC1_CODE16_CNT,
                     p_table1->bits[15]);
    }
  } else { /* interleaved */
    /* first select the table we'll use */
    /* this trick is done because hw always wants luma table as ac hw table 1 */

    if(JPG_SCN.Td[0] == 0)
      p_table1 = &(JPG_VLC.dc_table0);
    else if(JPG_SCN.Td[0] == 1)
      p_table1 = &(JPG_VLC.dc_table1);
    else if(JPG_SCN.Td[0] == 2)
      p_table1 = &(JPG_VLC.dc_table2);
    else
      p_table1 = &(JPG_VLC.dc_table3);

    if(JPG_SCN.Td[1] == 0)
      p_table2 = &(JPG_VLC.dc_table0);
    else if(JPG_SCN.Td[1] == 1)
      p_table2 = &(JPG_VLC.dc_table1);
    else if(JPG_SCN.Td[1] == 2)
      p_table2 = &(JPG_VLC.dc_table2);
    else
      p_table2 = &(JPG_VLC.dc_table3);

    if(JPG_SCN.Td[2] == 0)
      p_table3 = &(JPG_VLC.dc_table0);
    else if(JPG_SCN.Td[2] == 1)
      p_table3 = &(JPG_VLC.dc_table1);
    else if(JPG_SCN.Td[2] == 2)
      p_table3 = &(JPG_VLC.dc_table2);
    else
      p_table3 = &(JPG_VLC.dc_table3);

    ASSERT(p_table1);
    ASSERT(p_table2);
    ASSERT(p_table3);

    /* if later stage DC ==> no need for table */
    if(PTR_JPGC->scan.Ah == 0) {
      /* write DC table 1 (luma) */
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE1_CNT,
                     p_table1->bits[0]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE2_CNT,
                     p_table1->bits[1]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE3_CNT,
                     p_table1->bits[2]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE4_CNT,
                     p_table1->bits[3]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE5_CNT,
                     p_table1->bits[4]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE6_CNT,
                     p_table1->bits[5]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE7_CNT,
                     p_table1->bits[6]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE8_CNT,
                     p_table1->bits[7]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE9_CNT,
                     p_table1->bits[8]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE10_CNT,
                     p_table1->bits[9]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE11_CNT,
                     p_table1->bits[10]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE12_CNT,
                     p_table1->bits[11]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE13_CNT,
                     p_table1->bits[12]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE14_CNT,
                     p_table1->bits[13]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE15_CNT,
                     p_table1->bits[14]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE16_CNT,
                     p_table1->bits[15]);

      /* table DC2 (Cb) */
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE1_CNT,
                     p_table2->bits[0]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE2_CNT,
                     p_table2->bits[1]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE3_CNT,
                     p_table2->bits[2]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE4_CNT,
                     p_table2->bits[3]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE5_CNT,
                     p_table2->bits[4]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE6_CNT,
                     p_table2->bits[5]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE7_CNT,
                     p_table2->bits[6]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE8_CNT,
                     p_table2->bits[7]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE9_CNT,
                     p_table2->bits[8]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE10_CNT,
                     p_table2->bits[9]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE11_CNT,
                     p_table2->bits[10]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE12_CNT,
                     p_table2->bits[11]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE13_CNT,
                     p_table2->bits[12]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE14_CNT,
                     p_table2->bits[13]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE15_CNT,
                     p_table2->bits[14]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE16_CNT,
                     p_table2->bits[15]);

      /* table DC2 (Cr) */
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE1_CNT,
                     p_table3->bits[0]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE2_CNT,
                     p_table3->bits[1]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE3_CNT,
                     p_table3->bits[2]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE4_CNT,
                     p_table3->bits[3]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE5_CNT,
                     p_table3->bits[4]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE6_CNT,
                     p_table3->bits[5]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE7_CNT,
                     p_table3->bits[6]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE8_CNT,
                     p_table3->bits[7]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE9_CNT,
                     p_table3->bits[8]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE10_CNT,
                     p_table3->bits[9]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE11_CNT,
                     p_table3->bits[10]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE12_CNT,
                     p_table3->bits[11]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE13_CNT,
                     p_table3->bits[12]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE14_CNT,
                     p_table3->bits[13]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE15_CNT,
                     p_table3->bits[14]);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE16_CNT,
                     p_table3->bits[15]);
    } else {
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE1_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE2_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE3_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE4_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE5_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE6_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE7_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE8_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE9_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE10_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE11_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE12_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE13_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE14_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE15_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC1_CODE16_CNT, 0);

      /* table DC2 (Cb) */
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE1_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE2_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE3_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE4_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE5_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE6_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE7_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE8_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE9_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE10_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE11_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE12_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE13_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE14_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE15_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC2_CODE16_CNT, 0);

      /* table DC2 (Cr) */
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE1_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE2_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE3_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE4_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE5_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE6_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE7_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE8_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE9_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE10_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE11_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE12_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE13_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE14_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE15_CNT, 0);
      SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DC3_CODE16_CNT, 0);
    }
  }

  return;

#undef JPG_SCN
#undef JPG_VLC
#undef JPG_QTB
#undef JPG_FRM
#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

        Function name: JpegDecNextScanHdrs

        Functional description:
          Decodes next headers in case of non-interleaved stream

        Inputs:
          JpegDecContainer *pDecData      Pointer to JpegDecContainer structure

        Outputs:
          OK/NOK

------------------------------------------------------------------------------*/
JpegDecRet JpegDecNextScanHdrs(JpegDecContainer * jpeg_dec_cont) {

  u32 i;
  u32 current_byte = 0;
  u32 current_bytes = 0;
  JpegDecRet ret_code;

#define PTR_JPGC    jpeg_dec_cont
#define JPG_SCN     jpeg_dec_cont->scan
#define JPG_VLC     jpeg_dec_cont->vlc
#define JPG_QTB     jpeg_dec_cont->quant
#define JPG_FRM     jpeg_dec_cont->frame

  ret_code = JPEGDEC_OK;

  /* reset for new headers */
  PTR_JPGC->image.header_ready = 0;

  /* find markers and go ! */
  do {
    /* Look for marker prefix byte from stream */
    if(JpegDecGetByte(&(PTR_JPGC->stream)) == 0xFF) {
      current_byte = JpegDecGetByte(&(PTR_JPGC->stream));

      /* switch to certain header decoding */
      switch (current_byte) {
      case 0x00:
      case SOF0:
      case SOF2:
        break;
      /* Start of Scan */
      case SOS:
        /* reset image ready */
        PTR_JPGC->image.image_ready = 0;
        ret_code = JpegDecDecodeScan(PTR_JPGC);
        PTR_JPGC->image.header_ready = 1;
        if(ret_code != JPEGDEC_OK) {
          if(ret_code == JPEGDEC_STRM_ERROR) {
            JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: Stream error"));
            return (ret_code);
          } else {
            JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# JpegDecDecodeScan err\n"));
            return (ret_code);
          }
        }

        if(PTR_JPGC->stream.bit_pos_in_byte) {
          /* delete stuffing bits */
          current_byte = (8 - PTR_JPGC->stream.bit_pos_in_byte);
          if(JpegDecFlushBits
              (&(PTR_JPGC->stream),
               8 - PTR_JPGC->stream.bit_pos_in_byte) == STRM_ERROR) {
            JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: Stream error"));
            return (JPEGDEC_STRM_ERROR);
          }
        }
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# Stuffing bits deleted\n"));
        break;
      /* Start of Huffman tables */
      case DHT:
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# JpegDecDecodeHuffmanTables dec"));
        ret_code = JpegDecDecodeHuffmanTables(PTR_JPGC);
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# JpegDecDecodeHuffmanTables stops"));
        if(ret_code != JPEGDEC_OK) {
          if(ret_code == JPEGDEC_STRM_ERROR) {
            JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: Stream error"));
            return (ret_code);
          } else {
            JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: JpegDecDecodeHuffmanTables err"));
            return (ret_code);
          }
        }
        break;
      /* start of Quantisation Tables */
      case DQT:
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# JpegDecDecodeQuantTables dec"));
        ret_code = JpegDecDecodeQuantTables(PTR_JPGC);
        if(ret_code != JPEGDEC_OK) {
          if(ret_code == JPEGDEC_STRM_ERROR) {
            JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: Stream error"));
            return (ret_code);
          } else {
            JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: JpegDecDecodeQuantTables err"));
            return (ret_code);
          }
        }
        break;
      /* Start of Image */
      case SOI:
        /* no actions needed, continue */
        break;
      /* End of Image */
      case EOI:
        if(PTR_JPGC->image.image_ready) {
          JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# EOI: OK\n"));
          return (JPEGDEC_FRAME_READY);
        } else {
          JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: EOI: NOK\n"));
          return (JPEGDEC_ERROR);
        }
      /* Define Restart Interval */
      case DRI:
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# DRI"));
        current_bytes = JpegDecGet2Bytes(&(PTR_JPGC->stream));
        if(current_bytes == STRM_ERROR) {
          JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: Read bits "));
          return (JPEGDEC_STRM_ERROR);
        }
        PTR_JPGC->frame.Ri = JpegDecGet2Bytes(&(PTR_JPGC->stream));
        break;
      /* Restart with modulo 8 count m */
      case RST0:
      case RST1:
      case RST2:
      case RST3:
      case RST4:
      case RST5:
      case RST6:
      case RST7:
        /* initialisation of DC predictors to zero value !!! */
        for(i = 0; i < MAX_NUMBER_OF_COMPONENTS; i++) {
          PTR_JPGC->scan.pred[i] = 0;
        }
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# DC predictors init"));
        break;
      /* unsupported features */
      case DNL:
      case SOF1:
      case SOF3:
      case SOF5:
      case SOF6:
      case SOF7:
      case SOF9:
      case SOF10:
      case SOF11:
      case SOF13:
      case SOF14:
      case SOF15:
      case DAC:
      case DHP:
      case TEM:
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: Unsupported Features"));
        return (JPEGDEC_UNSUPPORTED);
      /* application data & comments */
      case APP0:
      case APP1:
      case APP2:
      case APP3:
      case APP4:
      case APP5:
      case APP6:
      case APP7:
      case APP8:
      case APP9:
      case APP10:
      case APP11:
      case APP12:
      case APP13:
      case APP14:
      case APP15:
      case COM:
        JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# COM"));
        current_bytes = JpegDecGet2Bytes(&(PTR_JPGC->stream));
        if(current_bytes == STRM_ERROR) {
          JPEGDEC_TRACE_INTERNAL(("JpegDecNextScanHdrs# ERROR: Read bits "));
          return (JPEGDEC_STRM_ERROR);
        }
        /* jump over not supported header */
        if(current_bytes != 0) {
          PTR_JPGC->stream.read_bits += ((current_bytes * 8) - 16);
          PTR_JPGC->stream.p_curr_pos +=
            (((current_bytes * 8) - 16) / 8);
        }
        break;
      default:
        break;
      }
    } else {
      if(current_byte == 0xFFFFFFFF) {
        break;
      }
    }

    if(PTR_JPGC->image.header_ready)
      break;
  } while((PTR_JPGC->stream.read_bits >> 3) <= PTR_JPGC->stream.stream_length);

  return (JPEGDEC_OK);

#undef JPG_SCN
#undef JPG_VLC
#undef JPG_QTB
#undef JPG_FRM
#undef PTR_JPGC
}

/*------------------------------------------------------------------------------
    Function name   : JpegRefreshRegs
    Description     :
    Return type     : void
    Argument        : PPContainer * ppC
------------------------------------------------------------------------------*/
void JpegRefreshRegs(JpegDecContainer * jpeg_dec_cont) {
  i32 i;
  u32 offset = 0x0;

  u32 *pp_regs = jpeg_dec_cont->jpeg_regs;

  for(i = DEC_X170_REGISTERS; i > 0; i--) {
    *pp_regs++ = DWLReadReg(jpeg_dec_cont->dwl, jpeg_dec_cont->core_id, offset);
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs = jpeg_dec_cont->jpeg_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *pp_regs++ = DWLReadReg(jpeg_dec_cont->dwl, jpeg_dec_cont->core_id, offset);
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : JpegFlushRegs
    Description     :
    Return type     : void
    Argument        : PPContainer * ppC
------------------------------------------------------------------------------*/
void JpegFlushRegs(JpegDecContainer * jpeg_dec_cont) {
  i32 i;
  u32 offset = 0x4;
  u32 *pp_regs = jpeg_dec_cont->jpeg_regs;

#ifdef JPEGDEC_ASIC_TRACE
  {
    JPEGDEC_TRACE_INTERNAL(("INTERNAL: REGS BEFORE HW ENABLE\n"));
    PrintJPEGReg(jpeg_dec_cont->jpeg_regs);
  }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

#ifdef JPEGDEC_INTEGRATOR
  DWLWriteReg(jpeg_dec_cont->dwl, 0, 0x00000000);
#endif /* #ifdef JPEGDEC_INTEGRATOR */

  /* skip id register */
  pp_regs++;

  for(i = DEC_X170_REGISTERS; i > 1; i--) {
    DWLWriteReg(jpeg_dec_cont->dwl,jpeg_dec_cont->core_id, offset, *pp_regs);
    *pp_regs = 0;
    pp_regs++;
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs = jpeg_dec_cont->jpeg_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    DWLWriteReg(jpeg_dec_cont->dwl,jpeg_dec_cont->core_id, offset, *pp_regs);
    *pp_regs = 0;
    pp_regs++;
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : JpegDecInitHWEmptyScan
    Description     :
    Return type     : void
    Argument        :
------------------------------------------------------------------------------*/
static u32 NumBits(u32 value) {

  u32 num_bits = 0;

  while(value) {
    value >>= 1;
    num_bits++;
  }

  if(!num_bits) {
    num_bits = 1;
  }

  return (num_bits);

}

void JpegDecInitHWEmptyScan(JpegDecContainer * jpeg_dec_cont, u32 component_id) {

  u32 i;
  i32 n;
  addr_t coeff_buffer = 0;
  addr_t output_buffer = 0;
  u32 num_blocks;
  u32 num_max;
  u8 *p_strm;
  u32 bits;
  u32 bit_pos;
  u32 *p_table_base = NULL;

  ASSERT(jpeg_dec_cont);

  jpeg_dec_cont->info.non_interleaved = 1;
  jpeg_dec_cont->info.component_id = component_id;

  if(jpeg_dec_cont->pp_instance == NULL && jpeg_dec_cont->info.user_alloc_mem == 1) {
    /* if user allocated memory ==> new addresses */
    jpeg_dec_cont->asic_buff.out_luma_buffer.virtual_address =
      jpeg_dec_cont->info.given_out_luma.virtual_address;
    jpeg_dec_cont->asic_buff.out_luma_buffer.bus_address =
      jpeg_dec_cont->info.given_out_luma.bus_address;
    jpeg_dec_cont->asic_buff.out_chroma_buffer.virtual_address =
      jpeg_dec_cont->info.given_out_chroma.virtual_address;
    jpeg_dec_cont->asic_buff.out_chroma_buffer.bus_address =
      jpeg_dec_cont->info.given_out_chroma.bus_address;
    jpeg_dec_cont->asic_buff.out_chroma_buffer2.virtual_address =
      jpeg_dec_cont->info.given_out_chroma2.virtual_address;
    jpeg_dec_cont->asic_buff.out_chroma_buffer2.bus_address =
      jpeg_dec_cont->info.given_out_chroma2.bus_address;
  }

  /*************** Set swreg13 data ************/
  /* Luminance output */
  if(component_id == 0)
    output_buffer = jpeg_dec_cont->asic_buff.out_luma_buffer.bus_address;
  else if(component_id == 1)
    output_buffer = (jpeg_dec_cont->asic_buff.out_chroma_buffer.bus_address);
  else
    output_buffer = (jpeg_dec_cont->asic_buff.out_chroma_buffer2.bus_address);

  SET_ADDR_REG(jpeg_dec_cont->jpeg_regs, HWIF_DEC_OUT_BASE, output_buffer);

  jpeg_dec_cont->info.y_cb_cr_mode = 0;
  jpeg_dec_cont->info.X = jpeg_dec_cont->frame.hw_x;
  jpeg_dec_cont->info.Y = jpeg_dec_cont->frame.hw_y;
  jpeg_dec_cont->info.fill_x = 0;
  jpeg_dec_cont->info.fill_y = 0;
  num_blocks = jpeg_dec_cont->frame.hw_x * jpeg_dec_cont->frame.hw_y / 64;
  coeff_buffer = jpeg_dec_cont->info.p_coeff_base.bus_address;
  if(component_id) {
    coeff_buffer += JPEGDEC_COEFF_SIZE * num_blocks;
    if(jpeg_dec_cont->info.y_cb_cr_mode_orig == JPEGDEC_YUV420) {
      jpeg_dec_cont->info.X /= 2;
      if(jpeg_dec_cont->info.X & 0xF) {
        jpeg_dec_cont->info.X += 8;
        jpeg_dec_cont->info.fill_x = 1;
      }
      jpeg_dec_cont->info.Y /= 2;
      if(jpeg_dec_cont->info.Y & 0xF) {
        jpeg_dec_cont->info.Y += 8;
        jpeg_dec_cont->info.fill_y = 1;
      }
      num_blocks /= 4;
    } else if(jpeg_dec_cont->info.y_cb_cr_mode_orig == JPEGDEC_YUV422) {
      jpeg_dec_cont->info.X /= 2;
      if(jpeg_dec_cont->info.X & 0xF) {
        jpeg_dec_cont->info.X += 8;
        jpeg_dec_cont->info.fill_x = 1;
      }
      num_blocks /= 2;
    } else if(jpeg_dec_cont->info.y_cb_cr_mode_orig == JPEGDEC_YUV440) {
      jpeg_dec_cont->info.Y /= 2;
      if(jpeg_dec_cont->info.Y & 0xF) {
        jpeg_dec_cont->info.Y += 8;
        jpeg_dec_cont->info.fill_y = 1;
      }
      num_blocks /= 2;
    }
    if(component_id > 1)
      coeff_buffer += JPEGDEC_COEFF_SIZE * num_blocks;
  }

  p_strm = (u8*)jpeg_dec_cont->info.tmp_strm.virtual_address;
  num_max = 0;
  while(num_blocks > 32767) {
    num_blocks -= 32767;
    num_max++;
  }

  n = NumBits(num_blocks);

  /* do we still have correct quantization tables ?? */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Write AC,DC,QP tables to base\n"));
  JpegDecWriteTablesProgressive(jpeg_dec_cont);

  /* two vlc codes, both with length 1 (can be done?), 0 for largest eob, 1
   * for last eob (EOBn) */
  /* write "length amounts" */
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_AC1_CODE1_CNT, 2);

  /* codeword values 0xE0 (for EOB run of 32767 blocks) and 0xn0 */
  p_table_base = jpeg_dec_cont->frame.p_table_base.virtual_address;
  p_table_base += 48;   /* start of vlc tables */
  *p_table_base = (0xE0 << 24) | ((n - 1) << 20);

  /* write num_max ext eobs of length 32767 followed by last ext eob */
  bit_pos = 0;
  for(i = 0; i < num_max; i++) {
    bits = 0x3FFF << 17;
    *p_strm = (bit_pos ? *p_strm : 0) | bits >> (24 + bit_pos);
    p_strm++;
    bits <<= 8 - bit_pos;
    *p_strm = bits >> 24;
    if(bit_pos >= 1) {
      p_strm++;
      bits <<= 8;
      *p_strm = bits >> 24;
    }
    bit_pos = (bit_pos + 15) & 0x7;
  }

  if(num_blocks) {
    /* codeword to be written:
     * '1' to indicate EOBn followed by number of blocks - 2^(n-1) */
    bits = num_blocks << (32 - n);
    *p_strm = (bit_pos ? *p_strm : 0) | bits >> (24 + bit_pos);
    p_strm++;
    bits <<= 8 - bit_pos;
    n -= 8 - bit_pos;
    while(n > 0) {
      *p_strm++ = bits >> 24;
      bits <<= 8;
      n -= 8;
    }
  }

  SET_ADDR_REG(jpeg_dec_cont->jpeg_regs, HWIF_RLC_VLC_BASE,
               jpeg_dec_cont->info.tmp_strm.bus_address);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_STRM_START_BIT, 0);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_STREAM_LEN, 100);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set input format\n"));
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_JPEG_MODE, JPEGDEC_YUV400);

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width extension\n"));
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PIC_MB_W_EXT,
                 ((((jpeg_dec_cont->info.X) >> (4)) & 0xE00) >> 9));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame width\n"));
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PIC_MB_WIDTH,
                 ((jpeg_dec_cont->info.X) >> (4)) & 0x1FF);

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height extension\n"));
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PIC_MB_H_EXT,
                 ((((jpeg_dec_cont->info.Y) >> (4)) & 0x700) >> 8));

  /* frame size, round up the number of mbs */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Frame height\n"));
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PIC_MB_HEIGHT_P,
                 ((jpeg_dec_cont->info.Y) >> (4)) & 0x0FF);

  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_WDIV8,
                 jpeg_dec_cont->info.fill_x);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_JPEG_FILRIGHT_E,
                 jpeg_dec_cont->info.fill_x);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_HDIV8,
                 jpeg_dec_cont->info.fill_y);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_FILDOWN_E,
                 jpeg_dec_cont->info.fill_y);

  /*************** Set swreg52 data ************/
  /* Set JPEG operation mode */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set JPEG operation mode\n"));
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_E, 1);

  /* indicate first ac scan for any spectral coeffs, nothing will be changed
   * as every block "skipped" by extended eobs */
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_SS, 1);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_SE, 1);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_AH, 0);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_AL, 0);

  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set coefficient buffer base address\n"));
  SET_ADDR_REG(jpeg_dec_cont->jpeg_regs, HWIF_PJPEG_COEFF_BUF, coeff_buffer);
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_DEC_OUT_DIS, 0);

  /* write table base */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set AC,DC,QP table base address\n"));
  SET_ADDR_REG(jpeg_dec_cont->jpeg_regs, HWIF_QTABLE_BASE,
               jpeg_dec_cont->frame.p_table_base.bus_address);

  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_SYNC_MARKER_E, 0);

  jpeg_dec_cont->asic_running = 1;

  /* Flush regs to hw register */
  JpegFlushRegs(jpeg_dec_cont);

  /* Enable jpeg mode and set slice mode */
  JPEGDEC_TRACE_INTERNAL(("INTERNAL: Enable jpeg\n"));
  SetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_DEC_E, 1);
  DWLEnableHw(jpeg_dec_cont->dwl, jpeg_dec_cont->core_id, 4 * 1,
              jpeg_dec_cont->jpeg_regs[1]);

#ifdef JPEGDEC_ASIC_TRACE
  {
    JPEGDEC_TRACE_INTERNAL(("PROGRESSIVE CONTINUE: REGS BEFORE IRQ CLEAN\n"));
    PrintJPEGReg(jpeg_dec_cont->jpeg_regs);
  }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

}
