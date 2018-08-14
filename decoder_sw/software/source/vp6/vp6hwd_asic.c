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
#include "dwl.h"
#include "regdrv_g1.h"
#include "vp6hwd_container.h"
#include "vp6hwd_asic.h"
#include "vp6hwd_debug.h"
#include "tiledref.h"
#include "commonconfig.h"

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#include <stdio.h>
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

#ifdef ASIC_TRACE_SUPPORT
#include "vp6_sw_trace.h"
#endif

#define SCAN(i)         HWIF_SCAN_MAP_ ## i

static const u32 ScanTblRegId[64] = { 0 /* SCAN(0) */ ,
                                      SCAN(1), SCAN(2), SCAN(3), SCAN(4), SCAN(5), SCAN(6), SCAN(7), SCAN(8),
                                      SCAN(9), SCAN(10), SCAN(11), SCAN(12), SCAN(13), SCAN(14),
                                      SCAN(15), SCAN(16), SCAN(17), SCAN(18), SCAN(19), SCAN(20),
                                      SCAN(21), SCAN(22), SCAN(23), SCAN(24), SCAN(25), SCAN(26),
                                      SCAN(27), SCAN(28), SCAN(29), SCAN(30), SCAN(31), SCAN(32),
                                      SCAN(33), SCAN(34), SCAN(35), SCAN(36), SCAN(37), SCAN(38),
                                      SCAN(39), SCAN(40), SCAN(41), SCAN(42), SCAN(43), SCAN(44),
                                      SCAN(45), SCAN(46), SCAN(47), SCAN(48), SCAN(49), SCAN(50),
                                      SCAN(51), SCAN(52), SCAN(53), SCAN(54), SCAN(55), SCAN(56),
                                      SCAN(57), SCAN(58), SCAN(59), SCAN(60), SCAN(61), SCAN(62), SCAN(63)
                                    };

#define TAP(i, j)       HWIF_PRED_BC_TAP_ ## i ## _ ## j

static const u32 TapRegId[8][4] = {
  {TAP(0, 0), TAP(0, 1), TAP(0, 2), TAP(0, 3)},
  {TAP(1, 0), TAP(1, 1), TAP(1, 2), TAP(1, 3)},
  {TAP(2, 0), TAP(2, 1), TAP(2, 2), TAP(2, 3)},
  {TAP(3, 0), TAP(3, 1), TAP(3, 2), TAP(3, 3)},
  {TAP(4, 0), TAP(4, 1), TAP(4, 2), TAP(4, 3)},
  {TAP(5, 0), TAP(5, 1), TAP(5, 2), TAP(5, 3)},
  {TAP(6, 0), TAP(6, 1), TAP(6, 2), TAP(6, 3)},
  {TAP(7, 0), TAP(7, 1), TAP(7, 2), TAP(7, 3)}
};

#define probSameAsLastOffset                (0)
#define probModeOffset                      (4*8)
#define probMvIsShortOffset                 (38*8)
#define probMvSignOffset                    (probMvIsShortOffset + 2)
#define probMvSizeOffset                    (39*8)
#define probMvShortOffset                   (41*8)

#define probDCFirstOffset                   (43*8)
#define probACFirstOffset                   (46*8)
#define probACZeroRunFirstOffset            (64*8)
#define probDCRestOffset                    (65*8)
#define probACRestOffset                    (71*8)
#define probACZeroRunRestOffset             (107*8)

#define huffmanTblDCOffset                  (43*8)
#define huffmanTblACZeroRunOffset           (huffmanTblDCOffset + 48)
#define huffmanTblACOffset                  (huffmanTblACZeroRunOffset + 48)

static void VP6HwdAsicRefreshRegs(VP6DecContainer_t * dec_cont);
static void VP6HwdAsicFlushRegs(VP6DecContainer_t * dec_cont);

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicInit
    Description     :
    Return type     : void
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP6HwdAsicInit(VP6DecContainer_t * dec_cont) {

  DWLmemset(dec_cont->vp6_regs, 0, sizeof(dec_cont->vp6_regs));

  SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_MODE, DEC_8190_MODE_VP6);

  SetCommonConfigRegs(dec_cont->vp6_regs,DWL_CLIENT_TYPE_VP6_DEC);

}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicAllocateMem
    Description     :
    Return type     : i32
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
i32 VP6HwdAsicAllocateMem(VP6DecContainer_t * dec_cont) {

  const void *dwl = dec_cont->dwl;
  i32 dwl_ret;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  dwl_ret = DWLMallocLinear(dwl, (1 + 266) * 8, &p_asic_buff->prob_tbl);

  if(dwl_ret != DWL_OK) {
    goto err;
  }

  SET_ADDR_REG(dec_cont->vp6_regs, HWIF_VP6HWPROBTBL_BASE,
               p_asic_buff->prob_tbl.bus_address);

  return 0;

err:
  VP6HwdAsicReleaseMem(dec_cont);
  return -1;
}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicReleaseMem
    Description     :
    Return type     : void
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP6HwdAsicReleaseMem(VP6DecContainer_t * dec_cont) {
  const void *dwl = dec_cont->dwl;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  if(p_asic_buff->prob_tbl.virtual_address != NULL) {
    DWLFreeLinear(dwl, &p_asic_buff->prob_tbl);
    DWLmemset(&p_asic_buff->prob_tbl, 0, sizeof(p_asic_buff->prob_tbl));
  }
}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicAllocatePictures
    Description     :
    Return type     : i32
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
i32 VP6HwdAsicAllocatePictures(VP6DecContainer_t * dec_cont) {
  i32 dwl_ret;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

#ifndef USE_EXTERNAL_BUFFER
  const void *dwl = dec_cont->dwl;
  i32 i;
  const u32 pict_buff_size = p_asic_buff->width * p_asic_buff->height * 3 / 2;
#endif

#ifdef USE_EXTERNAL_BUFFER
  if(!dec_cont->no_reallocation)
#endif
  {
    DWLmemset(p_asic_buff->pictures, 0, sizeof(p_asic_buff->pictures));

#ifdef USE_OUTPUT_RELEASE
    dwl_ret = BqueueInit2(&dec_cont->bq,
                          dec_cont->num_buffers );
#else
    dwl_ret = BqueueInit(&dec_cont->bq,
                         dec_cont->num_buffers );
#endif
    if( dwl_ret != 0 ) {
      goto err;
    }
  }

#ifndef USE_EXTERNAL_BUFFER
  for (i = 0; i < dec_cont->num_buffers; i++) {
    dwl_ret = DWLMallocRefFrm(dwl, pict_buff_size, &p_asic_buff->pictures[i]);
    if(dwl_ret != DWL_OK) {
      goto err;
    }
  }
#endif

  SetDecRegister(dec_cont->vp6_regs, HWIF_PIC_MB_WIDTH, p_asic_buff->width / 16);

  SetDecRegister(dec_cont->vp6_regs, HWIF_PIC_MB_HEIGHT_P,
                 p_asic_buff->height / 16);
  SetDecRegister(dec_cont->vp6_regs, HWIF_PIC_MB_H_EXT, (p_asic_buff->height / 16)>>8);

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp.pp_instance == NULL) {
    p_asic_buff->out_buffer_i = BqueueNext2( &dec_cont->bq,
                                BQUEUE_UNUSED, BQUEUE_UNUSED,
                                BQUEUE_UNUSED, 0 );
    if (p_asic_buff->out_buffer_i == (u32)0xFFFFFFFFU)
      return -2;
    p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
  } else {
    p_asic_buff->out_buffer_i = BqueueNext( &dec_cont->bq,
                                            BQUEUE_UNUSED, BQUEUE_UNUSED,
                                            BQUEUE_UNUSED, 0 );
  }
#else
  p_asic_buff->out_buffer_i = BqueueNext( &dec_cont->bq,
                                          BQUEUE_UNUSED, BQUEUE_UNUSED,
                                          BQUEUE_UNUSED, 0 );
#endif

  p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
  /* These need to point at something so use the output buffer */
  p_asic_buff->refBuffer        = p_asic_buff->out_buffer;
  p_asic_buff->golden_buffer     = p_asic_buff->out_buffer;
  p_asic_buff->ref_buffer_i       = BQUEUE_UNUSED;
  p_asic_buff->golden_buffer_i    = BQUEUE_UNUSED;

  return 0;

err:
  VP6HwdAsicReleasePictures(dec_cont);
  return -1;
}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicReleasePictures
    Description     :
    Return type     : void
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP6HwdAsicReleasePictures(VP6DecContainer_t * dec_cont) {
#ifndef USE_EXTERNAL_BUFFER
  const void *dwl = dec_cont->dwl;
  u32 i;
#endif
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
#ifdef USE_OUTPUT_RELEASE
  BqueueRelease2( &dec_cont->bq );
#else
  BqueueRelease( &dec_cont->bq );
#endif
  dec_cont->num_buffers = dec_cont->num_buffers_reserved;

#ifndef USE_EXTERNAL_BUFFER
  for( i = 0 ; i < dec_cont->num_buffers ; ++i ) {
    if(p_asic_buff->pictures[i].virtual_address != NULL) {
      DWLFreeRefFrm(dwl, &p_asic_buff->pictures[i]);
    }
  }
#endif
  DWLmemset(p_asic_buff->pictures, 0, sizeof(p_asic_buff->pictures));
}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicInitPicture
    Description     :
    Return type     : void
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP6HwdAsicInitPicture(VP6DecContainer_t * dec_cont) {

  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  SET_ADDR_REG(dec_cont->vp6_regs, HWIF_DEC_OUT_BASE,
               p_asic_buff->out_buffer->bus_address);

  /* golden reference */
  SET_ADDR_REG(dec_cont->vp6_regs, HWIF_VP6HWGOLDEN_BASE,
               p_asic_buff->golden_buffer->bus_address);

  /* previous picture */
  SET_ADDR_REG(dec_cont->vp6_regs, HWIF_REFER0_BASE,
               p_asic_buff->refBuffer->bus_address);

  /* frame type */
  if(dec_cont->pb.FrameType == BASE_FRAME) {
    SetDecRegister(dec_cont->vp6_regs, HWIF_PIC_INTER_E, 0);
  } else {
    SetDecRegister(dec_cont->vp6_regs, HWIF_PIC_INTER_E, 1);
  }

  SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_OUT_DIS, 0);

  /* loop filter */
  if(dec_cont->pb.UseLoopFilter) {
    SetDecRegister(dec_cont->vp6_regs, HWIF_FILTERING_DIS, 0);

    /* FLimit for loop filter */
    SetDecRegister(dec_cont->vp6_regs, HWIF_LOOP_FILT_LIMIT,
                   VP6HWDeblockLimitValues[dec_cont->pb.DctQMask]);
  } else {
    SetDecRegister(dec_cont->vp6_regs, HWIF_LOOP_FILT_LIMIT, 0);
    SetDecRegister(dec_cont->vp6_regs, HWIF_FILTERING_DIS, 1);
  }

  /* multistream and huffman */
  if(dec_cont->pb.MultiStream) {
    SetDecRegister(dec_cont->vp6_regs, HWIF_MULTISTREAM_E, 1);

    if(dec_cont->pb.UseHuffman) {
      SetDecRegister(dec_cont->vp6_regs, HWIF_HUFFMAN_E, 1);
    } else {
      SetDecRegister(dec_cont->vp6_regs, HWIF_HUFFMAN_E, 0);
    }

    /* second partiton's base address */
    /*SetDecRegister(dec_cont->vp6_regs, HWIF_VP6HWPART2_BASE,
     * p_asic_buff->partition2_base); */
  } else {
    SetDecRegister(dec_cont->vp6_regs, HWIF_MULTISTREAM_E, 0);

    SetDecRegister(dec_cont->vp6_regs, HWIF_HUFFMAN_E, 0);

    /*SetDecRegister(dec_cont->vp6_regs, HWIF_VP6HWPART2_BASE, (u32) (~0)); */
  }

  /* first bool decoder status */
  SetDecRegister(dec_cont->vp6_regs, HWIF_BOOLEAN_VALUE,
                 (dec_cont->pb.br.value >> 24) & (0xFFU));

  SetDecRegister(dec_cont->vp6_regs, HWIF_BOOLEAN_RANGE,
                 dec_cont->pb.br.range & (0xFFU));

  /* interpolation filter type */
  if(dec_cont->pb.PredictionFilterMode == BILINEAR_ONLY_PM) {
    /* bilinear only */
    SetDecRegister(dec_cont->vp6_regs, HWIF_BILIN_MC_E, 1);

    SetDecRegister(dec_cont->vp6_regs, HWIF_VARIANCE_TEST_E, 0);

    SetDecRegister(dec_cont->vp6_regs, HWIF_VAR_THRESHOLD, 0);

    SetDecRegister(dec_cont->vp6_regs, HWIF_MV_THRESHOLD, 0);
  } else if (dec_cont->pb.PredictionFilterMode == BICUBIC_ONLY_PM) {
    /* bicubic only */
    SetDecRegister(dec_cont->vp6_regs, HWIF_BILIN_MC_E, 0);

    SetDecRegister(dec_cont->vp6_regs, HWIF_VARIANCE_TEST_E, 0);

    SetDecRegister(dec_cont->vp6_regs, HWIF_VAR_THRESHOLD, 0);

    SetDecRegister(dec_cont->vp6_regs, HWIF_MV_THRESHOLD, 0);
  } else {
    /* auto selection bilinear/bicubic */
    SetDecRegister(dec_cont->vp6_regs, HWIF_BILIN_MC_E, 0);

    SetDecRegister(dec_cont->vp6_regs, HWIF_VARIANCE_TEST_E, 1);

    SetDecRegister(dec_cont->vp6_regs, HWIF_VAR_THRESHOLD,
                   dec_cont->pb.PredictionFilterVarThresh & (0x3FFU));

    SetDecRegister(dec_cont->vp6_regs, HWIF_MV_THRESHOLD,
                   dec_cont->pb.PredictionFilterMvSizeThresh & (0x07U));
  }

  /* QP */
  SetDecRegister(dec_cont->vp6_regs, HWIF_INIT_QP, dec_cont->pb.DctQMask);

  /* scan order */
  {
    i32 i;

    for(i = 1; i < BLOCK_SIZE; i++) {
      SetDecRegister(dec_cont->vp6_regs, ScanTblRegId[i],
                     dec_cont->pb.MergedScanOrder[i]);
    }
  }

  /* bicubic prediction filter tap */
  if(dec_cont->pb.PredictionFilterMode == AUTO_SELECT_PM ||
      dec_cont->pb.PredictionFilterMode == BICUBIC_ONLY_PM ) {
    i32 i, j;
    const i32 *bcfs =
      &VP6HW_BicubicFilterSet[dec_cont->pb.PredictionFilterAlpha][0][0];

    for(i = 0; i < 8; i++) {
      for(j = 0; j < 4; j++) {
        SetDecRegister(dec_cont->vp6_regs, TapRegId[i][j],
                       (bcfs[i * 4 + j] & 0xFF));
      }
    }

  }
  /* FLimit for loop filter */
  SetDecRegister(dec_cont->vp6_regs, HWIF_LOOP_FILT_LIMIT,
                 VP6HWDeblockLimitValues[dec_cont->pb.DctQMask]);

  /* Setup reference picture buffer */
  if( dec_cont->ref_buf_support ) {
    RefbuSetup( &dec_cont->ref_buffer_ctrl, dec_cont->vp6_regs,
                REFBU_FRAME,
                (dec_cont->pb.FrameType == BASE_FRAME),
                HANTRO_FALSE,
                0, 0,
                REFBU_FORCE_ADAPTIVE_SINGLE );
  }

  if( dec_cont->tiled_mode_support) {
    dec_cont->tiled_reference_enable =
      DecSetupTiledReference( dec_cont->vp6_regs,
                              dec_cont->tiled_mode_support,
                              DEC_DPB_FRAME,
                              0 /* interlaced content not present */ );
  } else {
    dec_cont->tiled_reference_enable = 0;
  }
}

/*------------------------------------------------------------------------------
    Function name : VP6HwdAsicStrmPosUpdate
    Description   : Set stream base and length related registers

    Return type   :
    Argument      : container
------------------------------------------------------------------------------*/
void VP6HwdAsicStrmPosUpdate(VP6DecContainer_t * dec_cont) {
  addr_t tmp;
  u32 hw_bit_pos;

  DEBUG_PRINT(("VP6HwdAsicStrmPosUpdate:\n"));

  dec_cont->asic_buff->partition1_bit_offset =
    (dec_cont->pb.strm.bits_consumed) +
    ((dec_cont->pb.br.pos - 3) * 8) + (8 - dec_cont->pb.br.count);

  dec_cont->asic_buff->partition1_base +=
    dec_cont->asic_buff->partition1_bit_offset / 8;
  dec_cont->asic_buff->partition1_bit_offset =
    dec_cont->asic_buff->partition1_bit_offset & 7;

  /* bit offset if base is unaligned */
  tmp = (dec_cont->asic_buff->partition1_base & DEC_8190_ALIGN_MASK) * 8;

  hw_bit_pos = tmp + dec_cont->asic_buff->partition1_bit_offset;

  DEBUG_PRINT(("\tStart bit pos %8d\n", hw_bit_pos));

  tmp = dec_cont->asic_buff->partition1_base;   /* unaligned base */
  tmp &= (~DEC_8190_ALIGN_MASK);  /* align the base */

  SET_ADDR_REG(dec_cont->vp6_regs, HWIF_VP6HWPART1_BASE, (addr_t)(~0));
  SET_ADDR_REG(dec_cont->vp6_regs, HWIF_VP6HWPART2_BASE, (addr_t)(~0));
  SetDecRegister(dec_cont->vp6_regs, HWIF_STRM1_START_BIT, 0);
  SetDecRegister(dec_cont->vp6_regs, HWIF_STRM_START_BIT, 0);

  /* Set stream pointer */
  if(dec_cont->pb.MultiStream) {
    /* Multistream; set both partition pointers. */
    DEBUG_PRINT(("\tStream base 1 %08x\n", tmp));
    SET_ADDR_REG(dec_cont->vp6_regs, HWIF_VP6HWPART1_BASE, tmp);
    SetDecRegister(dec_cont->vp6_regs, HWIF_STRM1_START_BIT, hw_bit_pos);

    DEBUG_PRINT(("\tStream base 2 %08x\n", tmp));
    tmp = dec_cont->asic_buff->partition2_base + dec_cont->pb.Buff2Offset;
    SET_ADDR_REG(dec_cont->vp6_regs, HWIF_VP6HWPART2_BASE,
                 tmp & (~DEC_8190_ALIGN_MASK));

    /* bit offset if base is unaligned */
    tmp = (tmp & DEC_8190_ALIGN_MASK) * 8;

    SetDecRegister(dec_cont->vp6_regs, HWIF_STRM_START_BIT, tmp);

    /* calculate strm 1 length */
    tmp = dec_cont->asic_buff->partition2_base -
          dec_cont->asic_buff->partition1_base + dec_cont->pb.Buff2Offset +
          hw_bit_pos/8;
    SetDecRegister(dec_cont->vp6_regs, HWIF_STREAM1_LEN, tmp );
  } else {
    /* Single stream; */
    SetDecRegister(dec_cont->vp6_regs, HWIF_STREAM1_LEN, 0);
    DEBUG_PRINT(("\tStream base 1 %08x\n", tmp));
    SET_ADDR_REG(dec_cont->vp6_regs, HWIF_VP6HWPART2_BASE, tmp);
    SetDecRegister(dec_cont->vp6_regs, HWIF_STRM_START_BIT, hw_bit_pos);
  }

  tmp = dec_cont->pb.strm.amount_left; /* unaligned stream */
  tmp += hw_bit_pos / 8;    /* add the alignmet bytes */

  DEBUG_PRINT(("\tStream length 1 %8d\n", tmp));
  SetDecRegister(dec_cont->vp6_regs, HWIF_STREAM_LEN, tmp);

}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicRefreshRegs
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP6HwdAsicRefreshRegs(VP6DecContainer_t * dec_cont) {
  i32 i;
  u32 offset = 0x0;

  u32 *dec_regs = dec_cont->vp6_regs;

  for(i = DEC_X170_REGISTERS; i > 0; --i) {
    *dec_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  dec_regs =  dec_cont->vp6_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *dec_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicFlushRegs
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP6HwdAsicFlushRegs(VP6DecContainer_t * dec_cont) {
  i32 i;
  u32 offset = 0x04;
  const u32 *dec_regs = dec_cont->vp6_regs + 1;

#ifdef TRACE_START_MARKER
  /* write ID register to trigger logic analyzer */
  DWLWriteReg(dec_cont->dwl, 0x00, ~0);
#endif

  for(i = DEC_X170_REGISTERS; i > 1; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  dec_regs =  dec_cont->vp6_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicRun
    Description     :
    Return type     : u32
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
u32 VP6HwdAsicRun(VP6DecContainer_t * dec_cont) {
  u32 asic_status = 0;
  i32 ret;

  ret = DWLReserveHw(dec_cont->dwl, &dec_cont->core_id);
  if(ret != DWL_OK) {
    return VP6HWDEC_HW_RESERVED;
  }

  dec_cont->asic_buff->frame_width[dec_cont->asic_buff->out_buffer_i] = dec_cont->width;
  dec_cont->asic_buff->frame_height[dec_cont->asic_buff->out_buffer_i] = dec_cont->height;

  dec_cont->asic_running = 1;

#ifdef ASIC_TRACE_SUPPORT
  {
    const PB_INSTANCE *pbi = &dec_cont->pb;
    static u8 bicubic_filter_alpha_prev = 255;

    if(bicubic_filter_alpha_prev == 255)
      bicubic_filter_alpha_prev = pbi->PredictionFilterAlpha;

    if(pbi->RefreshGoldenFrame)
      trace_DecodingToolUsed(TOOL_GOLDEN_FRAME_UPDATES);
    if(pbi->PredictionFilterAlpha != bicubic_filter_alpha_prev)
      trace_DecodingToolUsed(TOOL_MULTIPLE_FILTER_ALPHA);
    if(pbi->prob_mode_update)
      trace_DecodingToolUsed(TOOL_MODE_PROBABILITY_UPDATES);
    if(pbi->prob_mv_update)
      trace_DecodingToolUsed(TOOL_MV_TREE_UPDATES);
    if(pbi->scan_update)
      trace_DecodingToolUsed(TOOL_CUSTOM_SCAN_ORDER);
    if(pbi->prob_dc_update)
      trace_DecodingToolUsed(TOOL_COEFF_PROBABILITY_UPDATES_DC);
    if(pbi->prob_ac_update)
      trace_DecodingToolUsed(TOOL_COEFF_PROBABILITY_UPDATES_AC);
    if(pbi->prob_zrl_update)
      trace_DecodingToolUsed(TOOL_COEFF_PROBABILITY_UPDATES_ZRL);

    if(pbi->HFragments != pbi->OutputWidth ||
        pbi->VFragments != pbi->OutputHeight)
      trace_DecodingToolUsed(TOOL_OUTPUT_SCALING);
  }
#endif

  if(dec_cont->pp.pp_instance != NULL &&
      dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
    DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;
    DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

    TRACE_PP_CTRL("VP6HwdAsicRun: PP Run\n");

    dec_pp_if->inwidth  = dec_cont->pb.HFragments * 8;
    dec_pp_if->inheight = dec_cont->pb.VFragments * 8;
    dec_pp_if->cropped_w = dec_pp_if->inwidth;
    dec_pp_if->cropped_h = dec_pp_if->inheight;
    dec_pp_if->tiled_input_mode = dec_cont->tiled_reference_enable;
    dec_pp_if->progressive_sequence = 1;

    dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
    dec_pp_if->little_endian =
      GetDecRegister(dec_cont->vp6_regs, HWIF_DEC_OUT_ENDIAN);
    dec_pp_if->word_swap =
      GetDecRegister(dec_cont->vp6_regs, HWIF_DEC_OUTSWAP32_E);

    if(dec_pp_if->use_pipeline) {
      dec_pp_if->input_bus_luma = dec_pp_if->input_bus_chroma = 0;
    } else { /* parallel processing */
      dec_pp_if->input_bus_luma = p_asic_buff->refBuffer->bus_address;
      dec_pp_if->input_bus_chroma =
        dec_pp_if->input_bus_luma + dec_pp_if->inwidth * dec_pp_if->inheight;
    }

    dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);
  }

  VP6HwdAsicFlushRegs(dec_cont);

  SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_E, 1);
  DWLEnableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1, dec_cont->vp6_regs[1]);

  ret = DWLWaitHwReady(dec_cont->dwl, dec_cont->core_id,
                       (u32) DEC_X170_TIMEOUT_LENGTH);

  if(ret != DWL_HW_WAIT_OK) {
    ERROR_PRINT("DWLWaitHwReady");
    DEBUG_PRINT(("DWLWaitHwReady returned: %d\n", ret));

    /* reset HW */
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_E, 0);

    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->vp6_regs[1]);

    /* Wait for PP to end also */
    if(dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
      dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

      TRACE_PP_CTRL("VP6HwdAsicRun: PP Wait for end\n");

      dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

      TRACE_PP_CTRL("VP6HwdAsicRun: PP Finished\n");
    }

    dec_cont->asic_running = 0;
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

    return (ret == DWL_HW_WAIT_ERROR) ?
           VP6HWDEC_SYSTEM_ERROR : VP6HWDEC_SYSTEM_TIMEOUT;
  }

  VP6HwdAsicRefreshRegs(dec_cont);

  /* React to the HW return value */

  asic_status = GetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ_STAT);

  SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ_STAT, 0);
  SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ, 0); /* just in case */
  SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_E, 0);

  /* HW done, release it! */
  DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1, dec_cont->vp6_regs[1]);
  dec_cont->asic_running = 0;

  /* Wait for PP to end also, this is pipeline case */
  if(dec_cont->pp.pp_instance != NULL &&
      dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
    dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

    TRACE_PP_CTRL("VP6HwdAsicRun: PP Wait for end\n");

    dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

    TRACE_PP_CTRL("VP6HwdAsicRun: PP Finished\n");
  }

  DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

  if( dec_cont->ref_buf_support &&
      (asic_status & DEC_8190_IRQ_RDY) &&
      dec_cont->asic_running == 0 ) {
    RefbuMvStatistics( &dec_cont->ref_buffer_ctrl,
                       dec_cont->vp6_regs,
                       NULL, HANTRO_FALSE,
                       dec_cont->pb.FrameType == BASE_FRAME );
  }

  return asic_status;
}

/*------------------------------------------------------------------------------
    Function name   : VP6HwdAsicProbUpdate
    Description     :
    Return type     : void
    Argument        : VP6DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP6HwdAsicProbUpdate(VP6DecContainer_t * dec_cont) {
  u8 *dst;
  const u8 *src;
  u32 i, j, z, q;
  i32 r;
  PB_INSTANCE *pbi = &dec_cont->pb;

  u32 *asic_prob_base = dec_cont->asic_buff->prob_tbl.virtual_address;

  /* prob mode same as last */
  dst = ((u8 *) asic_prob_base) + probSameAsLastOffset;

  r = 3;
  for(i = 0; i < 3; i++) {
    for(j = 0; j < 10; j++) {
      dst[r] = pbi->prob_mode_same[i][j];
      r--;
      if( r < 0 ) {
        r = 3;
        dst += 4;
      }
    }
  }

  /* prob mode */
  dst = ((u8 *) asic_prob_base) + probModeOffset;

  r = 3;
  for(i = 0; i < 3; i++) {
    for(j = 0; j < 10; j++) {
      for(z = 0; z < 8; z++) {
        dst[r] = pbi->prob_mode[i][j][z];
        r--;
        if( r < 0 ) {
          r = 3;
          dst += 4;
        }
      }
    }
  }

  for(i = 0; i < 3; i++) {
    for(j = 0; j < 10; j++) {
      dst[r] = pbi->prob_mode[i][j][8];
      r--;
      if( r < 0 ) {
        r = 3;
        dst += 4;
      }
    }
  }

  /* prob is MV short */
  dst = ((u8 *) asic_prob_base) + probMvIsShortOffset;

  dst[3] = pbi->IsMvShortProb[0];
  dst[2] = pbi->IsMvShortProb[1];

  /* prob MV sign */
  /*dst = ((u8 *) asic_prob_base) + probMvSignOffset;*/

  dst[1] = pbi->MvSignProbs[0];
  dst[0] = pbi->MvSignProbs[1];

  /* prob MV size */
  dst = ((u8 *) asic_prob_base) + probMvSizeOffset;

  r = 3;
  for(i = 0; i < 2; i++) {
    for(j = 0; j < 8; j++) {
      dst[r] = pbi->MvSizeProbs[i][j];
      r--;
      if( r < 0 ) {
        r = 3;
        dst += 4;
      }
    }
  }

  /* prob MV short */
  dst = ((u8 *) asic_prob_base) + probMvShortOffset;

  for(j = 0; j < 4; j++)
    dst[3-j] = pbi->MvShortProbs[0][j];
  dst += 4;
  for(j = 4; j < 7; j++)
    dst[7-j] = pbi->MvShortProbs[0][j];
  dst += 4;  /* enhance to new 8 aligned address */
  for(j = 0; j < 4; j++)
    dst[3-j] = pbi->MvShortProbs[1][j];
  dst += 4;
  for(j = 4; j < 7; j++)
    dst[7-j] = pbi->MvShortProbs[1][j];

  /* DCT coeff probs */
  if(pbi->UseHuffman) {
    dst = ((u8 *) asic_prob_base) + huffmanTblDCOffset;
    r = 2;
    for( i = 0 ; i < 2 ; ++i ) {
      for( j = 0 ; j < 12 ; ++j ) {
        dst[ r ] = pbi->huff->DcHuffLUT[i][j] & 0xFF;
        dst[r+1] = pbi->huff->DcHuffLUT[i][j] >> 8;
        r -= 2;
        if( r ) {
          r = 2;
          dst += 4;
        }
      }
    }
    dst = ((u8 *) asic_prob_base) + huffmanTblACZeroRunOffset;
    r = 2;
    for( i = 0 ; i < 2 ; ++i ) {
      for( j = 0 ; j < 12 ; ++j ) {
        dst[ r ] = pbi->huff->ZeroHuffLUT[i][j] & 0xFF;
        dst[r+1] = pbi->huff->ZeroHuffLUT[i][j] >> 8;
        r -= 2;
        if( r ) {
          r = 2;
          dst += 4;
        }
      }
    }
    dst = ((u8 *) asic_prob_base) + huffmanTblACOffset;
    r = 2;
    for( i = 0 ; i < 2 ; ++i ) {
      for( j = 0 ; j < 3 ; ++j ) {
        for( z = 0 ; z < 4 ; ++z ) {
          for( q = 0 ; q < 12 ; ++q ) {
            dst[ r ] = pbi->huff->AcHuffLUT[i][j][z][q] & 0xFF;
            dst[r+1] = pbi->huff->AcHuffLUT[i][j][z][q] >> 8;
            r -= 2;
            if( r ) {
              r = 2;
              dst += 4;
            }
          }
        }
      }
    }
    return;
  }

  /* prob DC context (first 4 probs) */
  dst = ((u8 *) asic_prob_base) + probDCFirstOffset;
  src = pbi->DcNodeContexts;

  for(i = 0; i < 2; i++) {
    for(j = 0; j < 3; j++) {
      dst[3] = src[0];
      dst[2] = src[1];
      dst[1] = src[2];
      dst[0] = src[3];
      dst += 4;
      src += 5;
    }
  }

  /* prob AC (first 4 probs) */
  dst = ((u8 *) asic_prob_base) + probACFirstOffset;
  src = pbi->AcProbs;

  for(i = 0; i < 2; i++) {
    for(j = 0; j < 3; j++) {
      for(z = 0; z < 6; z++) {
        dst[3] = src[0];
        dst[2] = src[1];
        dst[1] = src[2];
        dst[0] = src[3];
        dst += 4;
        src += 4+7;
      }

    }
  }

  /* prob AC zero run (first 4 probs) */
  dst = ((u8 *) asic_prob_base) + probACZeroRunFirstOffset;
  for(i = 0; i < 2; i++) {
    dst[3] = pbi->ZeroRunProbs[i][0];
    dst[2] = pbi->ZeroRunProbs[i][1];
    dst[1] = pbi->ZeroRunProbs[i][2];
    dst[0] = pbi->ZeroRunProbs[i][3];
    dst += 4;
  }

  /* prob DC context (last 7 probs)  */
  dst = ((u8 *) asic_prob_base) + probDCRestOffset;
  src = pbi->DcNodeContexts;

  for(i = 0; i < 2; i++) {
    for(j = 0; j < 3; j++) {
      src += 4;   /* last prob */

      dst[3] = src[0];
      dst[2] = pbi->DcProbs[i * 11 + 5 + 0];
      dst[1] = pbi->DcProbs[i * 11 + 5 + 1];
      dst[0] = pbi->DcProbs[i * 11 + 5 + 2];
      dst[7] = pbi->DcProbs[i * 11 + 5 + 3];
      dst[6] = pbi->DcProbs[i * 11 + 5 + 4];
      dst[5] = pbi->DcProbs[i * 11 + 5 + 5];
      dst += 8;
      src++;
    }
  }

  /* prob AC (last 7 probs) */
  dst = ((u8 *) asic_prob_base) + probACRestOffset;
  src = pbi->AcProbs;

  for(i = 0; i < 2; i++) {
    for(j = 0; j < 3; j++) {
      for(z = 0; z < 6; z++) {
        src += 4;   /* last 7 probs */
        dst[3] = src[0];
        dst[2] = src[1];
        dst[1] = src[2];
        dst[0] = src[3];
        dst[7] = src[4];
        dst[6] = src[5];
        dst[5] = src[6];
        src += 7;
        dst += 8;
      }

    }
  }

  /* prob AC zero run (last 10 probs) */
  dst = ((u8 *) asic_prob_base) + probACZeroRunRestOffset;

  r = 3;
  for(j = 4; j < 14; j++) {
    dst[r] = pbi->ZeroRunProbs[0][j];
    r--;
    if( r < 0 ) {
      r = 3;
      dst += 4;
    }
  }

  dst += 8;   /* enhance to new 8 aligned address */
  r = 3;
  for(j = 4; j < 14; j++) {
    dst[r] = pbi->ZeroRunProbs[1][j];
    r--;
    if( r < 0 ) {
      r = 3;
      dst += 4;
    }
  }
}

