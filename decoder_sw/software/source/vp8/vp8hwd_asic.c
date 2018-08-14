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
#include "vp8decmc_internals.h"
#include "vp8hwd_container.h"
#include "vp8hwd_asic.h"
#include "vp8hwd_buffer_queue.h"
#include "vp8hwd_debug.h"
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
#endif

#define SCAN(i)         HWIF_SCAN_MAP_ ## i

static const u32 ScanTblRegId[16] = { 0 /* SCAN(0) */ ,
                                      SCAN(1), SCAN(2), SCAN(3), SCAN(4), SCAN(5), SCAN(6), SCAN(7), SCAN(8),
                                      SCAN(9), SCAN(10), SCAN(11), SCAN(12), SCAN(13), SCAN(14), SCAN(15)
                                    };

#define BASE(i)         HWIF_DCT_STRM ## i ## _BASE
static const u32 DctBaseId[] = { HWIF_VP6HWPART2_BASE,
                                 BASE(1), BASE(2), BASE(3), BASE(4), BASE(5), BASE(6), BASE(7)
                               };

#define BASE_MSB(i)         HWIF_DCT_STRM ## i ## _BASE_MSB
static const u32 DctBaseId_msb[] = { HWIF_VP6HWPART2_BASE_MSB,
                                     BASE_MSB(1), BASE_MSB(2), BASE_MSB(3), BASE_MSB(4), BASE_MSB(5), BASE_MSB(6), BASE_MSB(7)
                                   };

#define OFFSET(i)         HWIF_DCT ## i ## _START_BIT
static const u32 DctStartBit[] = { HWIF_STRM_START_BIT,
                                   OFFSET(1), OFFSET(2), OFFSET(3), OFFSET(4),
                                   OFFSET(5), OFFSET(6), OFFSET(7)
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

static const u32 mc_filter[8][6] = {
  { 0,  0,  128,    0,   0,  0 },
  { 0, -6,  123,   12,  -1,  0 },
  { 2, -11, 108,   36,  -8,  1 },
  { 0, -9,   93,   50,  -6,  0 },
  { 3, -16,  77,   77, -16,  3 },
  { 0, -6,   50,   93,  -9,  0 },
  { 1, -8,   36,  108, -11,  2 },
  { 0, -1,   12,  123,  -6,  0 }
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

#define PROB_TABLE_SIZE  1208

#define DEC_MODE_VP7  9
#define DEC_MODE_VP8 10

static void VP8HwdAsicRefreshRegs(VP8DecContainer_t * dec_cont);
static void VP8HwdAsicFlushRegs(VP8DecContainer_t * dec_cont);
static void vp8hwdUpdateOutBase(VP8DecContainer_t *dec_cont);
static struct DWLLinearMem* GetPrevRef(VP8DecContainer_t *dec_cont);
static struct DWLLinearMem* GetGoldenRef(VP8DecContainer_t *dec_cont);
static struct DWLLinearMem* GetAltRef(VP8DecContainer_t *dec_cont);

enum {

  VP8HWD_BUFFER_BASIC = 0,
  VP8HWD_BUFFER_STRIDE = 1,
  VP8HWD_BUFFER_CUSTOM = 2

};

/* VP7 QP LUTs */
static const u16 YDcQLookup[128] = {
  4,    4,    5,    6,    6,    7,    8,    8,
  9,   10,   11,   12,   13,   14,   15,   16,
  17,   18,   19,   20,   21,   22,   23,   23,
  24,   25,   26,   27,   28,   29,   30,   31,
  32,   33,   33,   34,   35,   36,   36,   37,
  38,   39,   39,   40,   41,   41,   42,   43,
  43,   44,   45,   45,   46,   47,   48,   48,
  49,   50,   51,   52,   53,   53,   54,   56,
  57,   58,   59,   60,   62,   63,   65,   66,
  68,   70,   72,   74,   76,   79,   81,   84,
  87,   90,   93,   96,  100,  104,  108,  112,
  116,  121,  126,  131,  136,  142,  148,  154,
  160,  167,  174,  182,  189,  198,  206,  215,
  224,  234,  244,  254,  265,  277,  288,  301,
  313,  327,  340,  355,  370,  385,  401,  417,
  434,  452,  470,  489,  509,  529,  550,  572
};

static const u16 YAcQLookup[128] = {
  4,    4,    5,    5,    6,    6,    7,    8,
  9,   10,   11,   12,   13,   15,   16,   17,
  19,   20,   22,   23,   25,   26,   28,   29,
  31,   32,   34,   35,   37,   38,   40,   41,
  42,   44,   45,   46,   48,   49,   50,   51,
  53,   54,   55,   56,   57,   58,   59,   61,
  62,   63,   64,   65,   67,   68,   69,   70,
  72,   73,   75,   76,   78,   80,   82,   84,
  86,   88,   91,   93,   96,   99,  102,  105,
  109,  112,  116,  121,  125,  130,  135,  140,
  146,  152,  158,  165,  172,  180,  188,  196,
  205,  214,  224,  234,  245,  256,  268,  281,
  294,  308,  322,  337,  353,  369,  386,  404,
  423,  443,  463,  484,  506,  529,  553,  578,
  604,  631,  659,  688,  718,  749,  781,  814,
  849,  885,  922,  960, 1000, 1041, 1083, 1127

};

static const u16 Y2DcQLookup[128] = {
  7,    9,   11,   13,   15,   17,   19,   21,
  23,   26,   28,   30,   33,   35,   37,   39,
  42,   44,   46,   48,   51,   53,   55,   57,
  59,   61,   63,   65,   67,   69,   70,   72,
  74,   75,   77,   78,   80,   81,   83,   84,
  85,   87,   88,   89,   90,   92,   93,   94,
  95,   96,   97,   99,  100,  101,  102,  104,
  105,  106,  108,  109,  111,  113,  114,  116,
  118,  120,  123,  125,  128,  131,  134,  137,
  140,  144,  148,  152,  156,  161,  166,  171,
  176,  182,  188,  195,  202,  209,  217,  225,
  234,  243,  253,  263,  274,  285,  297,  309,
  322,  336,  350,  365,  381,  397,  414,  432,
  450,  470,  490,  511,  533,  556,  579,  604,
  630,  656,  684,  713,  742,  773,  805,  838,
  873,  908,  945,  983, 1022, 1063, 1105, 1148
};

static const u16 Y2AcQLookup[128] = {
  7,    9,   11,   13,   16,   18,   21,   24,
  26,   29,   32,   35,   38,   41,   43,   46,
  49,   52,   55,   58,   61,   64,   66,   69,
  72,   74,   77,   79,   82,   84,   86,   88,
  91,   93,   95,   97,   98,  100,  102,  104,
  105,  107,  109,  110,  112,  113,  115,  116,
  117,  119,  120,  122,  123,  125,  127,  128,
  130,  132,  134,  136,  138,  141,  143,  146,
  149,  152,  155,  158,  162,  166,  171,  175,
  180,  185,  191,  197,  204,  210,  218,  226,
  234,  243,  252,  262,  273,  284,  295,  308,
  321,  335,  350,  365,  381,  398,  416,  435,
  455,  476,  497,  520,  544,  569,  595,  622,
  650,  680,  711,  743,  776,  811,  848,  885,
  925,  965, 1008, 1052, 1097, 1144, 1193, 1244,
  1297, 1351, 1407, 1466, 1526, 1588, 1652, 1719
};

static const u16 UvDcQLookup[128] = {
  4,    4,    5,    6,    6,    7,    8,    8,
  9,   10,   11,   12,   13,   14,   15,   16,
  17,   18,   19,   20,   21,   22,   23,   23,
  24,   25,   26,   27,   28,   29,   30,   31,
  32,   33,   33,   34,   35,   36,   36,   37,
  38,   39,   39,   40,   41,   41,   42,   43,
  43,   44,   45,   45,   46,   47,   48,   48,
  49,   50,   51,   52,   53,   53,   54,   56,
  57,   58,   59,   60,   62,   63,   65,   66,
  68,   70,   72,   74,   76,   79,   81,   84,
  87,   90,   93,   96,  100,  104,  108,  112,
  116,  121,  126,  131,  132,  132,  132,  132,
  132,  132,  132,  132,  132,  132,  132,  132,
  132,  132,  132,  132,  132,  132,  132,  132,
  132,  132,  132,  132,  132,  132,  132,  132,
  132,  132,  132,  132,  132,  132,  132,  132
};


static const u16 UvAcQLookup[128] = {
  4,    4,    5,    5,    6,    6,    7,    8,
  9,   10,   11,   12,   13,   15,   16,   17,
  19,   20,   22,   23,   25,   26,   28,   29,
  31,   32,   34,   35,   37,   38,   40,   41,
  42,   44,   45,   46,   48,   49,   50,   51,
  53,   54,   55,   56,   57,   58,   59,   61,
  62,   63,   64,   65,   67,   68,   69,   70,
  72,   73,   75,   76,   78,   80,   82,   84,
  86,   88,   91,   93,   96,   99,  102,  105,
  109,  112,  116,  121,  125,  130,  135,  140,
  146,  152,  158,  165,  172,  180,  188,  196,
  205,  214,  224,  234,  245,  256,  268,  281,
  294,  308,  322,  337,  353,  369,  386,  404,
  423,  443,  463,  484,  506,  529,  553,  578,
  604,  631,  659,  688,  718,  749,  781,  814,
  849,  885,  922,  960, 1000, 1041, 1083, 1127
};

#define CLIP3(l, h, v) ((v) < (l) ? (l) : ((v) > (h) ? (h) : (v)))
#define MB_MULTIPLE(x)  (((x)+15)&~15)

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicInit
    Description     :
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicInit(VP8DecContainer_t * dec_cont) {

  DWLmemset(dec_cont->vp8_regs, 0, sizeof(dec_cont->vp8_regs));

  SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_MODE,
                 dec_cont->dec_mode == VP8HWD_VP7 ?  DEC_MODE_VP7 : DEC_MODE_VP8);

  SetCommonConfigRegs(dec_cont->vp8_regs,DWL_CLIENT_TYPE_VP8_DEC);

}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicAllocateMem
    Description     :
    Return type     : i32
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
i32 VP8HwdAsicAllocateMem(VP8DecContainer_t * dec_cont) {

  const void *dwl = dec_cont->dwl;
  i32 dwl_ret = DWL_OK;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 i = 0;
  u32 num_mbs = 0;
  u32 memory_size = 0;
  /*  A segment map memory is allocated at the end of the probabilities for VP8 */

  if(dec_cont->dec_mode == VP8HWD_VP8) {
    num_mbs = (p_asic_buff->width>>4)*(p_asic_buff->height>>4);
    memory_size = (num_mbs+3)>>2; /* We fit 4 MBs on data into every full byte */
    memory_size = 64*((memory_size + 63) >> 6); /* Round up to next multiple of 64 bytes */
    p_asic_buff->segment_map_size = memory_size;
  }

  memory_size += PROB_TABLE_SIZE;

  for(i=0; i < dec_cont->num_cores; i++) {
    dwl_ret = DWLMallocLinear(dwl, memory_size, &p_asic_buff->prob_tbl[i]);
    if(dwl_ret != DWL_OK) {
      break;
    }
    if(dec_cont->dec_mode == VP8HWD_VP8) {
      p_asic_buff->segment_map[i].virtual_address =
        p_asic_buff->prob_tbl[i].virtual_address + PROB_TABLE_SIZE/4;
      DWLmemset(p_asic_buff->segment_map[i].virtual_address,
                0, p_asic_buff->segment_map_size);
      p_asic_buff->segment_map[i].bus_address =
        p_asic_buff->prob_tbl[i].bus_address + PROB_TABLE_SIZE;
    }
  }

  if(dwl_ret != DWL_OK) {
    VP8HwdAsicReleaseMem(dec_cont);
    return -1;
  }

  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicReleaseMem
    Description     :
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicReleaseMem(VP8DecContainer_t * dec_cont) {
  const void *dwl = dec_cont->dwl;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 i;

  for(i=0; i < dec_cont->num_cores; i++) {
    if(p_asic_buff->prob_tbl[i].virtual_address != NULL) {
      DWLFreeLinear(dwl, &p_asic_buff->prob_tbl[i]);
      DWLmemset(&p_asic_buff->prob_tbl[i], 0, sizeof(p_asic_buff->prob_tbl[i]));
    }
  }
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicAllocatePictures
    Description     :
    Return type     : i32
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
i32 VP8HwdAsicAllocatePictures(VP8DecContainer_t * dec_cont) {
  const void *dwl = dec_cont->dwl;
  i32 dwl_ret;
  u32 i, count;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  u32 pict_buff_size;
  u32 luma_stride;
  u32 chroma_stride;
  u32 luma_size;
  u32 chroma_size;
  u32 height;

  u32 num_mbs;
  u32 memory_size;

  /* allocate segment map */

  num_mbs = (p_asic_buff->width>>4)*(p_asic_buff->height>>4);

#if 0
  memory_size = (num_mbs+3)>>2; /* We fit 4 MBs on data into every full byte */
  memory_size = 64*((memory_size + 63) >> 6); /* Round up to next multiple of 64 bytes */

  if( dec_cont->decoder.dec_mode != VP8HWD_VP7 ) {

    dwl_ret = DWLMallocLinear(dwl, memory_size, &p_asic_buff->segment_map);

    if(dwl_ret != DWL_OK) {
      VP8HwdAsicReleasePictures(dec_cont);
      return -1;
    }

    p_asic_buff->segment_map_size = memory_size;
    SetDecRegister(dec_cont->vp8_regs, HWIF_SEGMENT_BASE,
                   p_asic_buff->segment_map.bus_address);
    DWLmemset(p_asic_buff->segment_map.virtual_address,
              0, p_asic_buff->segment_map_size);
  }
#endif
#ifdef USE_EXTERNAL_BUFFER
  if(!dec_cont->no_reallocation)
#endif
  {
    DWLmemset(p_asic_buff->pictures, 0, sizeof(p_asic_buff->pictures));
  }

  count = dec_cont->num_buffers;

  luma_stride = p_asic_buff->luma_stride ? p_asic_buff->luma_stride : p_asic_buff->width;
  chroma_stride = p_asic_buff->chroma_stride ? p_asic_buff->chroma_stride : p_asic_buff->width;

  if( luma_stride < p_asic_buff->width ) {
    VP8HwdAsicReleasePictures(dec_cont);
    return -1;
  }

  if( chroma_stride < p_asic_buff->width ) {
    VP8HwdAsicReleasePictures(dec_cont);
    return -1;
  }

  if (!dec_cont->slice_height)
    height = p_asic_buff->height;
  else
    height = (dec_cont->slice_height + 1) * 16;

  luma_size = luma_stride * height;
  chroma_size = chroma_stride * (height / 2);
  p_asic_buff->chroma_buf_offset = luma_size;

  pict_buff_size = luma_size + chroma_size;

  /*
      if (!dec_cont->slice_height)
          pict_buff_size = luma_size + chroma_size; p_asic_buff->width * p_asic_buff->height * 3 / 2;
      else
          pict_buff_size = p_asic_buff->width *
              (dec_cont->slice_height + 1) * 16 * 3 / 2;
              */

  p_asic_buff->sync_mc_offset = pict_buff_size;
  pict_buff_size += 16; /* space for multicore status fields */

#ifdef USE_EXTERNAL_BUFFER
  if(!dec_cont->no_reallocation)
#endif
  {
    dec_cont->bq = VP8HwdBufferQueueInitialize(dec_cont->num_buffers);
    if( dec_cont->bq == NULL ) {
      VP8HwdAsicReleasePictures(dec_cont);
      return -1;
    }
  }

  if (dec_cont->pp.pp_instance != NULL) {
    dec_cont->pp.pp_info.tiled_mode =
      dec_cont->tiled_reference_enable;
    dec_cont->pp.PPConfigQuery(dec_cont->pp.pp_instance,
                               &dec_cont->pp.pp_info);
  }
  if (!dec_cont->user_mem &&
      !(dec_cont->intra_only && dec_cont->pp.pp_info.pipeline_accepted)) {
    if(p_asic_buff->custom_buffers) {
      for (i = 0; i < count; i++) {
        p_asic_buff->pictures[i].virtual_address = p_asic_buff->user_mem.p_pic_buffer_y[i];
        p_asic_buff->pictures[i].bus_address = p_asic_buff->user_mem.pic_buffer_bus_addr_y[i];
        p_asic_buff->pictures_c[i].virtual_address = p_asic_buff->user_mem.p_pic_buffer_c[i];
        p_asic_buff->pictures_c[i].bus_address = p_asic_buff->user_mem.pic_buffer_bus_addr_c[i];
      }
    } else {
#ifndef USE_EXTERNAL_BUFFER
      for (i = 0; i < count; i++) {
        dwl_ret = DWLMallocRefFrm(dwl, pict_buff_size, &p_asic_buff->pictures[i]);
        if(dwl_ret != DWL_OK) {
          VP8HwdAsicReleasePictures(dec_cont);
          return -1;
        }
        /* setup pointers to chroma addresses */
        p_asic_buff->pictures_c[i].virtual_address =
          p_asic_buff->pictures[i].virtual_address +
          (p_asic_buff->chroma_buf_offset/4);
        p_asic_buff->pictures_c[i].bus_address =
          p_asic_buff->pictures[i].bus_address +
          p_asic_buff->chroma_buf_offset;

        {
          void *base = (char*)p_asic_buff->pictures[i].virtual_address
                       + p_asic_buff->sync_mc_offset;
          (void) DWLmemset(base, ~0, 16);
        }
      }
#endif
    }
  }

  /* webp/snapshot -> allocate memory for HW above row storage */
  SetDecRegister(dec_cont->vp8_regs, HWIF_WEBP_E, dec_cont->intra_only );
  if (dec_cont->intra_only && 0) { /* Not used for HW */
    u32 tmp_w = 0;
    ASSERT(count == 1);

    /* width of the memory in macroblocks, has to be multiple of amount
     * of mbs stored/load per access */
    /* TODO: */
    while (tmp_w < MAX_SNAPSHOT_WIDTH) tmp_w += 256;

    /* TODO: check amount of memory per macroblock for each HW block */
    memory_size = tmp_w /* num mbs */ *
                  (  2 + 2 /* streamd (cbf + intra pred modes) */ +
                     16 + 2*8 /* intra pred (luma + cb + cr) */ +
                     16*8 + 2*8*4 /* filterd */ );
    dwl_ret = DWLMallocLinear(dwl, memory_size, &p_asic_buff->pictures[1]);
    if(dwl_ret != DWL_OK) {
      VP8HwdAsicReleasePictures(dec_cont);
      return -1;
    }
    DWLmemset(p_asic_buff->pictures[1].virtual_address, 0, memory_size);
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER6_BASE,
                 p_asic_buff->pictures[1].bus_address);
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER2_BASE,
                 p_asic_buff->pictures[1].bus_address + 4*tmp_w);
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER3_BASE,
                 p_asic_buff->pictures[1].bus_address + 36*tmp_w);
  }

  if (dec_cont->hw_ec_support) {
    /* allocate memory for motion vectors used for error concealment */
    memory_size = num_mbs * 16 * sizeof(u32);
    dwl_ret = DWLMallocLinear(dwl, memory_size, &p_asic_buff->mvs[0]);
    if(dwl_ret != DWL_OK) {
      VP8HwdAsicReleasePictures(dec_cont);
      return -1;
    }
    dwl_ret = DWLMallocLinear(dwl, memory_size, &p_asic_buff->mvs[1]);
    if(dwl_ret != DWL_OK) {
      VP8HwdAsicReleasePictures(dec_cont);
      return -1;
    }
  }
  p_asic_buff->mvs_curr = p_asic_buff->mvs_ref = 0;

  SetDecRegister(dec_cont->vp8_regs, HWIF_PIC_MB_WIDTH, (p_asic_buff->width / 16)&0x1FF);
  SetDecRegister(dec_cont->vp8_regs, HWIF_PIC_MB_HEIGHT_P, (p_asic_buff->height / 16)&0xFF);
  SetDecRegister(dec_cont->vp8_regs, HWIF_PIC_MB_W_EXT, (p_asic_buff->width / 16)>>9);
  SetDecRegister(dec_cont->vp8_regs, HWIF_PIC_MB_H_EXT, (p_asic_buff->height / 16)>>8);

  SetDecRegister(dec_cont->vp8_regs, HWIF_Y_STRIDE_POW2,
                 p_asic_buff->strides_used ? p_asic_buff->luma_stride_pow2 : 0);
  SetDecRegister(dec_cont->vp8_regs, HWIF_C_STRIDE_POW2,
                 p_asic_buff->strides_used ? p_asic_buff->chroma_stride_pow2 : 0);

  /* Id for first frame. */
  p_asic_buff->out_buffer_i = VP8HwdBufferQueueGetBuffer(dec_cont->bq);
#ifdef USE_OUTPUT_RELEASE
  if(p_asic_buff->out_buffer_i == 0xFFFFFFFF) {
    return -2;
  }
  p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
#endif
  p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
  /* These need to point at something so use the output buffer */
  VP8HwdBufferQueueUpdateRef(dec_cont->bq,
                             BQUEUE_FLAG_PREV | BQUEUE_FLAG_GOLDEN | BQUEUE_FLAG_ALT,
                             p_asic_buff->out_buffer_i);
  if(dec_cont->intra_only != HANTRO_TRUE) {
    VP8HwdBufferQueueAddRef(dec_cont->bq, VP8HwdBufferQueueGetPrevRef(dec_cont->bq));
    VP8HwdBufferQueueAddRef(dec_cont->bq, VP8HwdBufferQueueGetAltRef(dec_cont->bq));
    VP8HwdBufferQueueAddRef(dec_cont->bq, VP8HwdBufferQueueGetGoldenRef(dec_cont->bq));
  }
  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicReleasePictures
    Description     :
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicReleasePictures(VP8DecContainer_t * dec_cont) {
#ifndef USE_EXTERNAL_BUFFER
  u32 i, count;
#endif
  const void *dwl = dec_cont->dwl;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 index;

  if(dec_cont->stream_consumed_callback == NULL &&
      dec_cont->bq && dec_cont->intra_only != HANTRO_TRUE) {
    /* Legacy single Core: remove references made for the next decode. */
    if (( index = VP8HwdBufferQueueGetPrevRef(dec_cont->bq)) != REFERENCE_NOT_SET)
      VP8HwdBufferQueueRemoveRef(dec_cont->bq, index);
    if (( index = VP8HwdBufferQueueGetAltRef(dec_cont->bq)) != REFERENCE_NOT_SET)
      VP8HwdBufferQueueRemoveRef(dec_cont->bq, index);
    if (( index = VP8HwdBufferQueueGetGoldenRef(dec_cont->bq)) != REFERENCE_NOT_SET)
      VP8HwdBufferQueueRemoveRef(dec_cont->bq, index);
    if (dec_cont->asic_buff->out_buffer_i != 0xFFFFFFFF)
      VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                 dec_cont->asic_buff->out_buffer_i);
  }

  if(dec_cont->bq) {
    if(dec_cont->intra_only == HANTRO_TRUE) {
      if (dec_cont->asic_buff->out_buffer_i != 0xFFFFFFFF)
        VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                   dec_cont->asic_buff->out_buffer_i);
    }
    VP8HwdBufferQueueRelease(dec_cont->bq);
    dec_cont->bq = NULL;
    dec_cont->num_buffers = dec_cont->num_buffers_reserved;
  }

  if(!p_asic_buff->custom_buffers) {
#ifndef USE_EXTERNAL_BUFFER
    count = dec_cont->num_buffers;
    for (i = 0; i < count; i++) {
      if(p_asic_buff->pictures[i].virtual_address != NULL) {
        DWLFreeRefFrm(dwl, &p_asic_buff->pictures[i]);
      }
    }
#endif
  }
#ifndef USE_EXTERNAL_BUFFER
  if (dec_cont->intra_only) {
    if(p_asic_buff->pictures[1].virtual_address != NULL) {
      DWLFreeLinear(dwl, &p_asic_buff->pictures[1]);
    }
  }
#endif
  DWLmemset(p_asic_buff->pictures, 0, sizeof(p_asic_buff->pictures));

  if(p_asic_buff->mvs[0].virtual_address != NULL)
    DWLFreeLinear(dwl, &p_asic_buff->mvs[0]);

  if(p_asic_buff->mvs[1].virtual_address != NULL)
    DWLFreeLinear(dwl, &p_asic_buff->mvs[1]);

}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicInitPicture
    Description     :
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicInitPicture(VP8DecContainer_t * dec_cont) {

  vp8_decoder_t *dec = &dec_cont->decoder;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  DWLHwConfig hw_config;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_VP8_DEC);

#ifdef SET_EMPTY_PICTURE_DATA   /* USE THIS ONLY FOR DEBUGGING PURPOSES */
  {
    i32 bgd = SET_EMPTY_PICTURE_DATA;
    i32 height = dec_cont->slice_height ? 16*dec_cont->slice_height :
                 MB_MULTIPLE(dec->height);
    i32 w_y = p_asic_buff->luma_stride ?
              p_asic_buff->luma_stride : MB_MULTIPLE(dec->width);
    i32 w_c = p_asic_buff->chroma_stride ?
              p_asic_buff->chroma_stride : MB_MULTIPLE(dec->width);
    i32 s_y = w_y * height;
    i32 s_c = w_c * height / 2;
    if(dec_cont->user_mem) {
      DWLmemset(p_asic_buff->user_mem.p_pic_buffer_y[0],
                bgd, s_y );
      DWLmemset(p_asic_buff->user_mem.p_pic_buffer_c[0],
                bgd, s_c );
    } else if (p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
      DWLmemset(p_asic_buff->out_buffer->virtual_address,
                bgd, s_y);
      DWLmemset(p_asic_buff->pictures_c[p_asic_buff->out_buffer_i].virtual_address,
                bgd, s_c );
    } else {
      DWLmemset(p_asic_buff->out_buffer->virtual_address,
                bgd, s_y + s_c);
    }
  }
#endif

  SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_OUT_DIS, 0);

  if (dec_cont->intra_only && dec_cont->pp.pp_info.pipeline_accepted) {
    SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_OUT_DIS, 1);
  } else if (!dec_cont->user_mem && !dec_cont->slice_height) {
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_DEC_OUT_BASE,
                 p_asic_buff->out_buffer->bus_address);

    if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
      SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER6_BASE,
                   p_asic_buff->pictures_c[p_asic_buff->out_buffer_i].bus_address);
    }

    if (!dec->key_frame)
      /* previous picture */
      SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER0_BASE,
                   GetPrevRef(dec_cont)->bus_address);
    else { /* chroma output base address */
      SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER0_BASE,
                   p_asic_buff->pictures_c[p_asic_buff->out_buffer_i].bus_address);
    }

    /* Note: REFER1 reg conflicts with slice size, so absolutely not
     * applicable with webp */
    if((p_asic_buff->strides_used || p_asic_buff->custom_buffers) && !dec_cont->intra_only ) {
      i32 idx = VP8HwdBufferQueueGetPrevRef(dec_cont->bq);
      SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER1_BASE,
                   p_asic_buff->pictures_c[idx].bus_address);
    }

  } else {
    u32 slice_height;

    if (dec_cont->slice_height*16 > p_asic_buff->height)
      slice_height = p_asic_buff->height/16;
    else
      slice_height = dec_cont->slice_height;

    SetDecRegister(dec_cont->vp8_regs, HWIF_JPEG_SLICE_H, slice_height);

    vp8hwdUpdateOutBase(dec_cont);
  }

  if(p_asic_buff->strides_used) {
    SetDecRegister(dec_cont->vp8_regs, HWIF_VP8_STRIDE_E, 1 );
  }
  if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
    SetDecRegister(dec_cont->vp8_regs, HWIF_VP8_CH_BASE_E, 1 );
  }

  /* golden reference */
  SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER4_BASE,
               GetGoldenRef(dec_cont)->bus_address);
  if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER2_BASE,
                 p_asic_buff->pictures_c[
                   VP8HwdBufferQueueGetGoldenRef(dec_cont->bq)].bus_address);
  }
  SetDecRegister(dec_cont->vp8_regs, HWIF_GREF_SIGN_BIAS,
                 dec->ref_frame_sign_bias[0]);

  /* alternate reference */
  SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER5_BASE,
               GetAltRef(dec_cont)->bus_address);
  if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER3_BASE,
                 p_asic_buff->pictures_c[
                   VP8HwdBufferQueueGetAltRef(dec_cont->bq)].bus_address);
  }
  SetDecRegister(dec_cont->vp8_regs, HWIF_AREF_SIGN_BIAS,
                 dec->ref_frame_sign_bias[1]);

  SetDecRegister(dec_cont->vp8_regs, HWIF_PIC_INTER_E, !dec->key_frame);


  /* mb skip mode [Codec X] */
  SetDecRegister(dec_cont->vp8_regs, HWIF_SKIP_MODE, !dec->coeff_skip_mode );

  /* loop filter */
  SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_TYPE,
                 dec->loop_filter_type);
  SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_SHARPNESS,
                 dec->loop_filter_sharpness);

  if (!dec->segmentation_enabled)
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_0,
                   dec->loop_filter_level);
  else if (dec->segment_feature_mode) { /* absolute mode */
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_0,
                   dec->segment_loopfilter[0]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_1,
                   dec->segment_loopfilter[1]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_2,
                   dec->segment_loopfilter[2]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_3,
                   dec->segment_loopfilter[3]);
  } else { /* delta mode */
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_0,
                   CLIP3((i32)0, (i32)63,
                         (i32)dec->loop_filter_level + dec->segment_loopfilter[0]));
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_1,
                   CLIP3((i32)0, (i32)63,
                         (i32)dec->loop_filter_level + dec->segment_loopfilter[1]));
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_2,
                   CLIP3((i32)0, (i32)63,
                         (i32)dec->loop_filter_level + dec->segment_loopfilter[2]));
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_LEVEL_3,
                   CLIP3((i32)0, (i32)63,
                         (i32)dec->loop_filter_level + dec->segment_loopfilter[3]));
  }

  SetDecRegister(dec_cont->vp8_regs, HWIF_SEGMENT_E, dec->segmentation_enabled );
  SetDecRegister(dec_cont->vp8_regs, HWIF_SEGMENT_UPD_E, dec->segmentation_map_update );

  /* TODO: seems that ref dec does not disable filtering based on version,
   * check */
  /*SetDecRegister(dec_cont->vp8_regs, HWIF_FILTERING_DIS, dec->vp_version >= 2);*/
  SetDecRegister(dec_cont->vp8_regs, HWIF_FILTERING_DIS,
                 dec->loop_filter_level == 0);

  /* Disable filtering if picture width larger than supported video
   * decoder width or height larger than 8K */
  if(dec_cont->width > hw_config.max_dec_pic_width ||
      dec_cont->height >= 8176 )
    SetDecRegister(dec_cont->vp8_regs, HWIF_FILTERING_DIS, 1 );

  /* full pell chroma mvs for VP8 version 3 */
  SetDecRegister(dec_cont->vp8_regs, HWIF_CH_MV_RES,
                 dec->dec_mode == VP8HWD_VP7 || dec->vp_version != 3);

  SetDecRegister(dec_cont->vp8_regs, HWIF_BILIN_MC_E,
                 dec->dec_mode == VP8HWD_VP8 && (dec->vp_version & 0x3));

  /* first bool decoder status */
  SetDecRegister(dec_cont->vp8_regs, HWIF_BOOLEAN_VALUE,
                 (dec_cont->bc.value >> 24) & (0xFFU));

  SetDecRegister(dec_cont->vp8_regs, HWIF_BOOLEAN_RANGE,
                 dec_cont->bc.range & (0xFFU));

  /* QP */
  if (dec_cont->dec_mode == VP8HWD_VP7) {
    /* LUT */
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_0, YDcQLookup[dec->qp_ydc]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_1, YAcQLookup[dec->qp_yac]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_2, Y2DcQLookup[dec->qp_y2_dc]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_3, Y2AcQLookup[dec->qp_y2_ac]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_4, UvDcQLookup[dec->qp_ch_dc]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_5, UvAcQLookup[dec->qp_ch_ac]);
  } else {
    if (!dec->segmentation_enabled)
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_0, dec->qp_yac);
    else if (dec->segment_feature_mode) { /* absolute mode */
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_0, dec->segment_qp[0]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_1, dec->segment_qp[1]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_2, dec->segment_qp[2]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_3, dec->segment_qp[3]);
    } else { /* delta mode */
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_0,
                     CLIP3(0, 127, dec->qp_yac + dec->segment_qp[0]));
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_1,
                     CLIP3(0, 127, dec->qp_yac + dec->segment_qp[1]));
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_2,
                     CLIP3(0, 127, dec->qp_yac + dec->segment_qp[2]));
      SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_3,
                     CLIP3(0, 127, dec->qp_yac + dec->segment_qp[3]));
    }
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_DELTA_0, dec->qp_ydc);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_DELTA_1, dec->qp_y2_dc);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_DELTA_2, dec->qp_y2_ac);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_DELTA_3, dec->qp_ch_dc);
    SetDecRegister(dec_cont->vp8_regs, HWIF_QUANT_DELTA_4, dec->qp_ch_ac);

    if (dec->mode_ref_lf_enabled) {
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_0,  dec->mb_ref_lf_delta[0]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_1,  dec->mb_ref_lf_delta[1]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_2,  dec->mb_ref_lf_delta[2]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_3,  dec->mb_ref_lf_delta[3]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_0,  dec->mb_mode_lf_delta[0]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_1,  dec->mb_mode_lf_delta[1]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_2,  dec->mb_mode_lf_delta[2]);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_3,  dec->mb_mode_lf_delta[3]);
    } else {
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_0,  0);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_1,  0);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_2,  0);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_REF_ADJ_3,  0);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_0,  0);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_1,  0);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_2,  0);
      SetDecRegister(dec_cont->vp8_regs, HWIF_FILT_MB_ADJ_3,  0);
    }
  }

  /* scan order */
  if (dec_cont->dec_mode == VP8HWD_VP7) {
    i32 i;

    for(i = 1; i < 16; i++) {
      SetDecRegister(dec_cont->vp8_regs, ScanTblRegId[i],
                     dec_cont->decoder.vp7_scan_order[i]);
    }
  }

  /* prediction filter taps */
  /* normal 6-tap filters */
  if ((dec->vp_version & 0x3) == 0 || dec->dec_mode == VP8HWD_VP7) {
    i32 i, j;

    for(i = 0; i < 8; i++) {
      for(j = 0; j < 4; j++) {
        SetDecRegister(dec_cont->vp8_regs, TapRegId[i][j],
                       mc_filter[i][j+1]);
      }
      if (i == 2) {
        SetDecRegister(dec_cont->vp8_regs, HWIF_PRED_TAP_2_M1,
                       mc_filter[i][0]);
        SetDecRegister(dec_cont->vp8_regs, HWIF_PRED_TAP_2_4,
                       mc_filter[i][5]);
      } else if (i == 4) {
        SetDecRegister(dec_cont->vp8_regs, HWIF_PRED_TAP_4_M1,
                       mc_filter[i][0]);
        SetDecRegister(dec_cont->vp8_regs, HWIF_PRED_TAP_4_4,
                       mc_filter[i][5]);
      } else if (i == 6) {
        SetDecRegister(dec_cont->vp8_regs, HWIF_PRED_TAP_6_M1,
                       mc_filter[i][0]);
        SetDecRegister(dec_cont->vp8_regs, HWIF_PRED_TAP_6_4,
                       mc_filter[i][5]);
      }
    }
  }
  /* TODO: taps for bilinear case? */

  if (dec->dec_mode == VP8HWD_VP7) {
    SetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_COMP0,
                   p_asic_buff->dc_pred[0]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_COMP1,
                   p_asic_buff->dc_pred[1]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_MATCH0,
                   p_asic_buff->dc_match[0]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_MATCH1,
                   p_asic_buff->dc_match[1]);
    SetDecRegister(dec_cont->vp8_regs, HWIF_VP7_VERSION,
                   dec->vp_version != 0);
  }

  /* Setup reference picture buffer */
  if( dec_cont->ref_buf_support && !dec_cont->intra_only ) {
    u32 cnt_last = 0,
        cnt_golden = 0,
        cnt_alt = 0;
    u32 cnt_best;
    u32 mul = 0;
    u32 all_mbs = 0;
    u32 buf_pic_id = 0;
    u32 force_disable = 0;
    u32 tmp;
    u32 cov;
    u32 intra_mbs;
    u32 flags = (dec_cont->num_cores > 1) ? REFBU_DONT_USE_STATS : 0;
    u32 threshold;

    if(!dec->key_frame) {
      /* Calculate most probable reference frame */

#define CALCULATE_SHARE(range, prob) \
    (((range)*(prob-1))/254)

      all_mbs = mul = dec->width * dec->height >> 8;        /* All mbs in frame */

      /* Deduct intra MBs */
      intra_mbs = CALCULATE_SHARE(mul, dec->prob_intra);
      mul = all_mbs - intra_mbs;

      cnt_last = CALCULATE_SHARE(mul, dec->prob_ref_last);
      if( dec_cont->dec_mode == VP8HWD_VP8 ) {
        mul -= cnt_last;     /* What's left is mbs either Golden or Alt */
        cnt_golden = CALCULATE_SHARE(mul, dec->prob_ref_golden);
        cnt_alt = mul - cnt_golden;
      } else {
        cnt_golden = mul - cnt_last; /* VP7 has no Alt ref frame */
        cnt_alt = 0;
      }

#undef CALCULATE_SHARE

      /* Select best reference frame */

      if(cnt_last > cnt_golden) {
        tmp = (cnt_last > cnt_alt);
        buf_pic_id = tmp ? 0 : 5;
        cnt_best  = tmp ? cnt_last : cnt_alt;
      } else {
        tmp = (cnt_golden > cnt_alt);
        buf_pic_id = tmp ? 4 : 5;
        cnt_best  = tmp ? cnt_golden : cnt_alt;
      }

      /* Check against refbuf-calculated threshold value; if  it
       * seems we'll miss our target, then don't bother enabling
       * the feature at all... */
      threshold = RefbuGetHitThreshold(&dec_cont->ref_buffer_ctrl);
      threshold *= (dec->height/16);
      threshold /= 4;
      if(cnt_best < threshold)
        force_disable = 1;

      /* Next frame has enough reference picture hits, now also take
       * actual hits and intra blocks into calculations... */
      if(!force_disable) {
        cov = RefbuVpxGetPrevFrameStats(&dec_cont->ref_buffer_ctrl);

        /* If we get prediction for coverage, we can disable checkpoint
         * and do calculations here */
        if( cov > 0 ) {
          /* Calculate percentage of hits from last frame, multiply
           * predicted reference frame referrals by it and compare.
           * Note: if refbuffer was off for previous frame, we might
           *    not get an accurate estimation... */

          tmp = dec->refbu_pred_hits;
          if(tmp)     cov = (256*cov)/tmp;
          else        cov = 0;
          cov = (cov*cnt_best)>>8;

          if( cov < threshold)
            force_disable = 1;
          else
            flags |= REFBU_DISABLE_CHECKPOINT;
        }
      }
      dec->refbu_pred_hits = cnt_best;
    } else {
      dec->refbu_pred_hits = 0;
    }

    RefbuSetup( &dec_cont->ref_buffer_ctrl, dec_cont->vp8_regs,
                REFBU_FRAME,
                dec->key_frame || force_disable,
                HANTRO_FALSE,
                buf_pic_id, 0,
                flags );
  }

  if( dec_cont->tiled_mode_support && !dec_cont->intra_only) {
    dec_cont->tiled_reference_enable =
      DecSetupTiledReference( dec_cont->vp8_regs,
                              dec_cont->tiled_mode_support,
                              DEC_DPB_FRAME,
                              0 );
  } else {
    dec_cont->tiled_reference_enable = 0;
  }

  SetDecRegister(dec_cont->vp8_regs, HWIF_STARTMB_X, 0);
  SetDecRegister(dec_cont->vp8_regs, HWIF_STARTMB_Y, 0);
  SetDecRegister(dec_cont->vp8_regs, HWIF_ERROR_CONC_MODE, 0);

  if (!dec_cont->conceal) {
    if (dec->key_frame || !dec_cont->hw_ec_support)
      SetDecRegister(dec_cont->vp8_regs, HWIF_WRITE_MVS_E, 0);
    else {
      SetDecRegister(dec_cont->vp8_regs, HWIF_WRITE_MVS_E, 1);
      SET_ADDR_REG(dec_cont->vp8_regs, HWIF_DIR_MV_BASE,
                   p_asic_buff->mvs[p_asic_buff->mvs_curr].bus_address);
    }
  }
  /* start HW in error concealment mode */
  else {
    SetDecRegister(dec_cont->vp8_regs, HWIF_WRITE_MVS_E, 0);
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_DIR_MV_BASE,
                 p_asic_buff->mvs[p_asic_buff->mvs_curr].bus_address);
    SetDecRegister(dec_cont->vp8_regs, HWIF_STARTMB_X,
                   dec_cont->conceal_start_mb_x);
    SetDecRegister(dec_cont->vp8_regs, HWIF_STARTMB_Y,
                   dec_cont->conceal_start_mb_y);
    SetDecRegister(dec_cont->vp8_regs, HWIF_ERROR_CONC_MODE, 1);
  }

}

/*------------------------------------------------------------------------------
    Function name : VP8HwdAsicStrmPosUpdate
    Description   : Set stream base and length related registers

    Return type   :
    Argument      : container
------------------------------------------------------------------------------*/
void VP8HwdAsicStrmPosUpdate(VP8DecContainer_t * dec_cont, addr_t strm_bus_address) {
  u32 i, tmp;
  u32 hw_bit_pos;
  addr_t tmp_addr;
  u32 tmp2;
  u32 byte_offset;
  u32 extra_bytes_packed = 0;
  vp8_decoder_t *dec = &dec_cont->decoder;

  DEBUG_PRINT(("VP8HwdAsicStrmPosUpdate:\n"));

  /* TODO: miksi bitin tarkkuudella (count) kun kuitenki luetaan tavu
   * kerrallaan? Vai lukeeko HW eri lailla? */
  /* start of control partition */
  tmp = (dec_cont->bc.pos) * 8 + (8 - dec_cont->bc.count);

  if (dec->frame_tag_size == 4) tmp+=8;

  if(dec_cont->dec_mode == VP8HWD_VP8 &&
      dec_cont->decoder.key_frame)
    extra_bytes_packed += 7;

  tmp += extra_bytes_packed*8;

  tmp_addr = strm_bus_address + tmp/8;
  hw_bit_pos = (tmp_addr & DEC_8190_ALIGN_MASK) * 8;
  tmp_addr &= (~DEC_8190_ALIGN_MASK);  /* align the base */

  hw_bit_pos += tmp & 0x7;

  /* control partition */
  SET_ADDR_REG(dec_cont->vp8_regs, HWIF_VP6HWPART1_BASE, tmp_addr);
  SetDecRegister(dec_cont->vp8_regs, HWIF_STRM1_START_BIT, hw_bit_pos);

  /* total stream length */
  /*tmp = dec_cont->bc.stream_end_pos - (tmp_addr - strm_bus_address);*/

  /* calculate dct partition length here instead */
  tmp = dec_cont->bc.stream_end_pos + dec->frame_tag_size - dec->dct_partition_offsets[0];
  tmp += (dec->nbr_dct_partitions-1)*3;
  tmp2 = strm_bus_address + extra_bytes_packed + dec->dct_partition_offsets[0];
  tmp += (tmp2 & 0x7);

  SetDecRegister(dec_cont->vp8_regs, HWIF_STREAM_LEN, tmp);
  /* Default length register is 24 bits. If running webp, write extra
   * length MSBs to extension register. */
  if(dec_cont->intra_only)
    SetDecRegister(dec_cont->vp8_regs, HWIF_STREAM_LEN_EXT, tmp >> 24);

  /* control partition length */
  tmp = dec->offset_to_dct_parts;
  /* if total length smaller than indicated control length -> cut */
  if (tmp > dec_cont->bc.stream_end_pos)
    tmp = dec_cont->bc.stream_end_pos;
  tmp += dec->frame_tag_size + extra_bytes_packed;
  /* subtract what was read by SW */
  tmp -= (tmp_addr - strm_bus_address);
  /* give extra byte of data to negotiate "tight" buffers */
  if (!dec_cont->hw_ec_support)
    tmp ++;

  SetDecRegister(dec_cont->vp8_regs, HWIF_STREAM1_LEN, tmp);

  /* number of dct partitions */
  SetDecRegister(dec_cont->vp8_regs, HWIF_COEFFS_PART_AM,
                 dec->nbr_dct_partitions-1);

  /* base addresses and bit offsets of dct partitions */
  for (i = 0; i < dec->nbr_dct_partitions; i++) {
    tmp_addr = strm_bus_address + extra_bytes_packed + dec->dct_partition_offsets[i];
    byte_offset = tmp_addr & 0x7;
    SetDecRegister(dec_cont->vp8_regs, DctBaseId[i], tmp_addr & 0xFFFFFFF8);
#ifdef USE_64BIT_ENV
    SetDecRegister(dec_cont->vp8_regs, DctBaseId_msb[i], tmp_addr >> 32);
#endif
    SetDecRegister(dec_cont->vp8_regs, DctStartBit[i], byte_offset * 8);
  }


}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicRefreshRegs
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicRefreshRegs(VP8DecContainer_t * dec_cont) {
  i32 i;
  u32 offset = 0x0;

  u32 *dec_regs = dec_cont->vp8_regs;

  for(i = DEC_X170_REGISTERS; i > 0; --i) {
    *dec_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  dec_regs =  dec_cont->vp8_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *dec_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicFlushRegs
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicFlushRegs(VP8DecContainer_t * dec_cont) {
  i32 i;
  u32 offset = 0x04;
  u32 *dec_regs = dec_cont->vp8_regs + 1;

#ifdef TRACE_START_MARKER
  /* write ID register to trigger logic analyzer */
  DWLWriteReg(dec_cont->dwl, 0x00, ~0);
#endif

  dec_regs[0] &= ~dec_regs[0];

  for(i = DEC_X170_REGISTERS; i > 1; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  dec_regs =  dec_cont->vp8_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicRun
    Description     :
    Return type     : u32
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
u32 VP8HwdAsicRun(VP8DecContainer_t * dec_cont) {
  u32 asic_status = 0;
  i32 ret;

  dec_cont->asic_buff->frame_width[dec_cont->asic_buff->out_buffer_i] = (dec_cont->width + 15) & ~15;
  dec_cont->asic_buff->frame_height[dec_cont->asic_buff->out_buffer_i] = (dec_cont->height + 15) & ~15;
  dec_cont->asic_buff->coded_width[dec_cont->asic_buff->out_buffer_i] = dec_cont->width;
  dec_cont->asic_buff->coded_height[dec_cont->asic_buff->out_buffer_i] = dec_cont->height;

  if (!dec_cont->asic_running || dec_cont->stream_consumed_callback != NULL) {
    dec_cont->asic_running = 1;
    if(dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
      DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;
      DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

      TRACE_PP_CTRL("VP8HwdAsicRun: PP Run\n");

      dec_pp_if->inwidth  = MB_MULTIPLE(dec_cont->width);
      dec_pp_if->inheight = MB_MULTIPLE(dec_cont->height);
      dec_pp_if->cropped_w = (dec_pp_if->inwidth + 7) & ~7;
      dec_pp_if->cropped_h = (dec_pp_if->inheight + 7) & ~7;

      dec_pp_if->luma_stride = p_asic_buff->luma_stride ?
                               p_asic_buff->luma_stride : p_asic_buff->width;
      dec_pp_if->chroma_stride = p_asic_buff->chroma_stride ?
                                 p_asic_buff->chroma_stride : p_asic_buff->width;

      /* forward tiled mode */
      dec_pp_if->tiled_input_mode = dec_cont->tiled_reference_enable;
      dec_pp_if->progressive_sequence = 1;

      dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
      dec_pp_if->little_endian =
        GetDecRegister(dec_cont->vp8_regs, HWIF_DEC_OUT_ENDIAN);
      dec_pp_if->word_swap =
        GetDecRegister(dec_cont->vp8_regs, HWIF_DEC_OUTSWAP32_E);

      if(dec_pp_if->use_pipeline) {
        dec_pp_if->input_bus_luma = dec_pp_if->input_bus_chroma = 0;
      } else { /* parallel processing */
        dec_pp_if->input_bus_luma = GetPrevRef(dec_cont)->bus_address;
        dec_pp_if->input_bus_chroma =
          p_asic_buff->pictures_c[
            VP8HwdBufferQueueGetPrevRef(dec_cont->bq)].bus_address;

        if(!(p_asic_buff->strides_used || p_asic_buff->custom_buffers)) {
          dec_pp_if->input_bus_chroma = dec_pp_if->input_bus_luma +
                                        dec_pp_if->inwidth * dec_pp_if->inheight;
        }

      }

      dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);
    }
    VP8HwdAsicFlushRegs(dec_cont);

    SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_E, 1);
    /* If in multicore-mode, prepare the callback. */
    if (dec_cont->stream_consumed_callback) {
      VP8MCSetHwRdyCallback(dec_cont);

      /* Manage the reference frame bookkeeping. */
      VP8HwdUpdateRefs(dec_cont, 0);
      /* Make sure the reference statuses (128-bits) are zeroed. */
      /* Use status writing only for actual multicore operation,
         multithreaded decoding can be used with one Core also. */
      if(dec_cont->num_cores > 1) {
        DWLmemset(VP8HwdRefStatusAddress(dec_cont), 0, 4 * sizeof(u32));
      }

    }
    DWLEnableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                dec_cont->vp8_regs[1]);
  } else {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 13,
                dec_cont->vp8_regs[13]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 14,
                dec_cont->vp8_regs[14]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 15,
                dec_cont->vp8_regs[15]);
#ifdef USE_64BIT_ENV
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 123,
                dec_cont->vp8_regs[123]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 124,
                dec_cont->vp8_regs[124]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 125,
                dec_cont->vp8_regs[125]);
#endif
    DWLEnableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                dec_cont->vp8_regs[1]);
  }

  /* If the decoder is run by callback mechanism, no need to wait for HW. */
  if (dec_cont->stream_consumed_callback)
    return VP8HWDEC_ASYNC_MODE;
  else
    ret = DWLWaitHwReady(dec_cont->dwl, dec_cont->core_id,
                         (u32) DEC_X170_TIMEOUT_LENGTH);

  if(ret != DWL_HW_WAIT_OK) {
    ERROR_PRINT("DWLWaitHwReady");
    DEBUG_PRINT(("DWLWaitHwReady returned: %d\n", ret));

    /* reset HW */
    SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_E, 0);

    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->vp8_regs[1]);

    /* Wait for PP to end also */
    if(dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
      dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

      TRACE_PP_CTRL("VP8HwdAsicRun: PP Wait for end\n");

      dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

      TRACE_PP_CTRL("VP8HwdAsicRun: PP Finished\n");
    }

    dec_cont->asic_running = 0;
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

    return (ret == DWL_HW_WAIT_ERROR) ?
           VP8HWDEC_SYSTEM_ERROR : VP8HWDEC_SYSTEM_TIMEOUT;
  }

  VP8HwdAsicRefreshRegs(dec_cont);

  /* React to the HW return value */

  asic_status = GetDecRegister(dec_cont->vp8_regs, HWIF_DEC_IRQ_STAT);

  SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_IRQ_STAT, 0);
  SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_IRQ, 0); /* just in case */

  if (dec_cont->decoder.dec_mode == VP8HWD_VP7) {
    dec_cont->asic_buff->dc_pred[0] =
      GetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_COMP0);
    dec_cont->asic_buff->dc_pred[1] =
      GetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_COMP1);
    dec_cont->asic_buff->dc_match[0] =
      GetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_MATCH0);
    dec_cont->asic_buff->dc_match[1] =
      GetDecRegister(dec_cont->vp8_regs, HWIF_INIT_DC_MATCH1);
  }
  if (!(asic_status & DEC_8190_IRQ_SLICE)) { /* not slice interrupt */
    /* HW done, release it! */
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->vp8_regs[1]);
    dec_cont->asic_running = 0;

    /* Wait for PP to end also, this is pipeline case */
    if(dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
      dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

      TRACE_PP_CTRL("VP8HwdAsicRun: PP Wait for end\n");

      dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

      TRACE_PP_CTRL("VP8HwdAsicRun: PP Finished\n");
    }

    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

    if( dec_cont->ref_buf_support &&
        (asic_status & DEC_8190_IRQ_RDY) &&
        dec_cont->asic_running == 0 ) {
      RefbuMvStatistics( &dec_cont->ref_buffer_ctrl,
                         dec_cont->vp8_regs,
                         NULL, HANTRO_FALSE,
                         dec_cont->decoder.key_frame );
    }
  }

  return asic_status;
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdSegmentMapUpdate
    Description     : Set register base for HW, issue new buffer for write
                        reset dor keyframe.
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdSegmentMapUpdate(VP8DecContainer_t * dec_cont) {
  vp8_decoder_t *dec = &dec_cont->decoder;

  /* For map update, provide new segmentation map buffer. There are as many
   * buffers as cores. */
  if(dec->segmentation_map_update && dec_cont->stream_consumed_callback) {
    dec_cont->segm_id = (dec_cont->segm_id+1) % dec_cont->num_cores;
  }
  SET_ADDR_REG(dec_cont->vp8_regs, HWIF_SEGMENT_BASE,
               dec_cont->asic_buff->segment_map[dec_cont->segm_id].bus_address);

  if(dec_cont->decoder.key_frame) {
    vp8hwdResetSegMap(&dec_cont->decoder, dec_cont->asic_buff, dec_cont->segm_id);
  }
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicProbUpdate
    Description     :
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicProbUpdate(VP8DecContainer_t * dec_cont) {
  u32 *dst;
  u32 i, j, k;
  u32 tmp;
  u32 *asic_prob_base = dec_cont->asic_buff->prob_tbl[dec_cont->core_id].virtual_address;

  SET_ADDR_REG(dec_cont->vp8_regs, HWIF_VP6HWPROBTBL_BASE,
               dec_cont->asic_buff->prob_tbl[dec_cont->core_id].bus_address);

  /* first probs */
  dst = asic_prob_base;

  tmp = (dec_cont->decoder.prob_mb_skip_false << 24) |
        (dec_cont->decoder.prob_intra       << 16) |
        (dec_cont->decoder.prob_ref_last     <<  8) |
        (dec_cont->decoder.prob_ref_golden   <<  0);
  *dst++ = tmp;
  tmp = (dec_cont->decoder.prob_segment[0]  << 24) |
        (dec_cont->decoder.prob_segment[1]  << 16) |
        (dec_cont->decoder.prob_segment[2]  <<  8) |
        (0 /*unused*/                      <<  0);
  *dst++ = tmp;

  tmp = (dec_cont->decoder.entropy.prob_luma16x16_pred_mode[0] << 24) |
        (dec_cont->decoder.entropy.prob_luma16x16_pred_mode[1] << 16) |
        (dec_cont->decoder.entropy.prob_luma16x16_pred_mode[2] <<  8) |
        (dec_cont->decoder.entropy.prob_luma16x16_pred_mode[3] <<  0);
  *dst++ = tmp;
  tmp = (dec_cont->decoder.entropy.prob_chroma_pred_mode[0]    << 24) |
        (dec_cont->decoder.entropy.prob_chroma_pred_mode[1]    << 16) |
        (dec_cont->decoder.entropy.prob_chroma_pred_mode[2]    <<  8) |
        (0 /*unused*/                                       <<  0);
  *dst++ = tmp;

  /* mv probs */
  tmp = (dec_cont->decoder.entropy.prob_mv_context[0][0] << 24) | /* is short */
        (dec_cont->decoder.entropy.prob_mv_context[1][0] << 16) |
        (dec_cont->decoder.entropy.prob_mv_context[0][1] <<  8) | /* sign */
        (dec_cont->decoder.entropy.prob_mv_context[1][1] <<  0);
  *dst++ = tmp;
  tmp = (dec_cont->decoder.entropy.prob_mv_context[0][8+9] << 24) |
        (dec_cont->decoder.entropy.prob_mv_context[0][9+9] << 16) |
        (dec_cont->decoder.entropy.prob_mv_context[1][8+9] <<  8) |
        (dec_cont->decoder.entropy.prob_mv_context[1][9+9] <<  0);
  *dst++ = tmp;
  for( i = 0 ; i < 2 ; ++i ) {
    for( j = 0 ; j < 8 ; j+=4 ) {
      tmp = (dec_cont->decoder.entropy.prob_mv_context[i][j+9+0] << 24) |
            (dec_cont->decoder.entropy.prob_mv_context[i][j+9+1] << 16) |
            (dec_cont->decoder.entropy.prob_mv_context[i][j+9+2] <<  8) |
            (dec_cont->decoder.entropy.prob_mv_context[i][j+9+3] <<  0);
      *dst++ = tmp;
    }
  }
  for( i = 0 ; i < 2 ; ++i ) {
    tmp = (dec_cont->decoder.entropy.prob_mv_context[i][0+2] << 24) |
          (dec_cont->decoder.entropy.prob_mv_context[i][1+2] << 16) |
          (dec_cont->decoder.entropy.prob_mv_context[i][2+2] <<  8) |
          (dec_cont->decoder.entropy.prob_mv_context[i][3+2] <<  0);
    *dst++ = tmp;
    tmp = (dec_cont->decoder.entropy.prob_mv_context[i][4+2] << 24) |
          (dec_cont->decoder.entropy.prob_mv_context[i][5+2] << 16) |
          (dec_cont->decoder.entropy.prob_mv_context[i][6+2] <<  8) |
          (0 /* unused */                                  <<  0);
    *dst++ = tmp;
  }

  /* coeff probs (header part) */
  dst = asic_prob_base + 8*7/4;
  for( i = 0 ; i < 4 ; ++i ) {
    for( j = 0 ; j < 8 ; ++j ) {
      for( k = 0 ; k < 3 ; ++k ) {
        tmp = (dec_cont->decoder.entropy.prob_coeffs[i][j][k][0] << 24) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][1] << 16) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][2] <<  8) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][3] <<  0);
        *dst++ = tmp;
      }
    }
  }

  /* coeff probs (footer part) */
  dst = asic_prob_base + 8*55/4;
  for( i = 0 ; i < 4 ; ++i ) {
    for( j = 0 ; j < 8 ; ++j ) {
      for( k = 0 ; k < 3 ; ++k ) {
        tmp = (dec_cont->decoder.entropy.prob_coeffs[i][j][k][4] << 24) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][5] << 16) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][6] <<  8) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][7] <<  0);
        *dst++ = tmp;
        tmp = (dec_cont->decoder.entropy.prob_coeffs[i][j][k][8] << 24) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][9] << 16) |
              (dec_cont->decoder.entropy.prob_coeffs[i][j][k][10] << 8) |
              (0 /* unused */                                   <<  0);
        *dst++ = tmp;
      }
    }
  }
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdUpdateRefs
    Description     :
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdUpdateRefs(VP8DecContainer_t * dec_cont, u32 corrupted) {

  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  i32 prev_p = 0, prev_a = 0, prev_g = 0;

  if(dec_cont->stream_consumed_callback == NULL) {
    /* Store current ref indices but remove only after new refs have been
     * protected. */
    prev_p = VP8HwdBufferQueueGetPrevRef(dec_cont->bq);
    prev_a = VP8HwdBufferQueueGetAltRef(dec_cont->bq);
    prev_g = VP8HwdBufferQueueGetGoldenRef(dec_cont->bq);
  }
  if (dec_cont->decoder.copy_buffer_to_alternate == 1) {
    VP8HwdBufferQueueUpdateRef(dec_cont->bq, BQUEUE_FLAG_ALT,
                               VP8HwdBufferQueueGetPrevRef(dec_cont->bq));
  } else if (dec_cont->decoder.copy_buffer_to_alternate == 2) {
    VP8HwdBufferQueueUpdateRef(dec_cont->bq, BQUEUE_FLAG_ALT,
                               VP8HwdBufferQueueGetGoldenRef(dec_cont->bq));
  }

  if (dec_cont->decoder.copy_buffer_to_golden == 1) {
    VP8HwdBufferQueueUpdateRef(dec_cont->bq, BQUEUE_FLAG_GOLDEN,
                               VP8HwdBufferQueueGetPrevRef(dec_cont->bq));
  } else if (dec_cont->decoder.copy_buffer_to_golden == 2) {
    VP8HwdBufferQueueUpdateRef(dec_cont->bq, BQUEUE_FLAG_GOLDEN,
                               VP8HwdBufferQueueGetAltRef(dec_cont->bq));
  }


  if (!corrupted) {
    u32 update_flags = 0;
    if(dec_cont->decoder.refresh_golden) {
      update_flags |= BQUEUE_FLAG_GOLDEN;
    }

    if(dec_cont->decoder.refresh_alternate) {
      update_flags |= BQUEUE_FLAG_ALT;
    }

    if (dec_cont->decoder.refresh_last) {
      update_flags |= BQUEUE_FLAG_PREV;
    }
    VP8HwdBufferQueueUpdateRef(dec_cont->bq, update_flags,
                               p_asic_buff->out_buffer_i);

    p_asic_buff->mvs_ref = p_asic_buff->mvs_curr;
    p_asic_buff->mvs_curr ^= 0x1;
  }
  /* AddRef the pictures for this Core run. */
  VP8HwdBufferQueueAddRef(dec_cont->bq,
                          VP8HwdBufferQueueGetPrevRef(dec_cont->bq));
  VP8HwdBufferQueueAddRef(dec_cont->bq,
                          VP8HwdBufferQueueGetAltRef(dec_cont->bq));
  VP8HwdBufferQueueAddRef(dec_cont->bq,
                          VP8HwdBufferQueueGetGoldenRef(dec_cont->bq));

  if(dec_cont->stream_consumed_callback == NULL) {
    VP8HwdBufferQueueRemoveRef(dec_cont->bq, prev_p);
    VP8HwdBufferQueueRemoveRef(dec_cont->bq, prev_a);
    VP8HwdBufferQueueRemoveRef(dec_cont->bq, prev_g);
  }
}

void vp8hwdUpdateOutBase(VP8DecContainer_t *dec_cont) {

  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  ASSERT(dec_cont->intra_only);

  if (dec_cont->user_mem) {
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_DEC_OUT_BASE,
                 p_asic_buff->user_mem.pic_buffer_bus_addr_y[0]);
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER0_BASE,
                 p_asic_buff->user_mem.pic_buffer_bus_addr_c[0]);
  } else { /* if (startOfPic)*/
    SET_ADDR_REG(dec_cont->vp8_regs, HWIF_DEC_OUT_BASE,
                 p_asic_buff->out_buffer->bus_address);

    if(!(p_asic_buff->strides_used || p_asic_buff->custom_buffers)) {
      SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER0_BASE,
                   p_asic_buff->out_buffer->bus_address +
                   p_asic_buff->width *
                   (dec_cont->slice_height ? (dec_cont->slice_height + 1)*16 :
                    p_asic_buff->height));
    } else {
      SET_ADDR_REG(dec_cont->vp8_regs, HWIF_REFER0_BASE,
                   p_asic_buff->out_buffer->bus_address +
                   p_asic_buff->chroma_buf_offset);
    }
  }
}

/*------------------------------------------------------------------------------
    Function name   : VP8HwdAsicContPicture
    Description     :
    Return type     : void
    Argument        : VP8DecContainer_t * dec_cont
------------------------------------------------------------------------------*/
void VP8HwdAsicContPicture(VP8DecContainer_t * dec_cont) {

  u32 slice_height;

  /* update output picture buffer if not pipeline */
  if(dec_cont->pp.pp_instance == NULL ||
      !dec_cont->pp.dec_pp_if.use_pipeline)
    vp8hwdUpdateOutBase(dec_cont);

  /* slice height */
  if (dec_cont->tot_decoded_rows + dec_cont->slice_height >
      dec_cont->asic_buff->height/16)
    slice_height = dec_cont->asic_buff->height/16 - dec_cont->tot_decoded_rows;
  else
    slice_height = dec_cont->slice_height;

  SetDecRegister(dec_cont->vp8_regs, HWIF_JPEG_SLICE_H, slice_height);

}

struct DWLLinearMem* GetOutput(VP8DecContainer_t *dec_cont) {
  return dec_cont->asic_buff->pictures + dec_cont->asic_buff->out_buffer_i;
}

static struct DWLLinearMem* GetPrevRef(VP8DecContainer_t *dec_cont) {
  return dec_cont->asic_buff->pictures +
         VP8HwdBufferQueueGetPrevRef(dec_cont->bq);
}

static struct DWLLinearMem* GetGoldenRef(VP8DecContainer_t *dec_cont) {
  return dec_cont->asic_buff->pictures +
         VP8HwdBufferQueueGetGoldenRef(dec_cont->bq);
}

static struct DWLLinearMem* GetAltRef(VP8DecContainer_t *dec_cont) {
  return dec_cont->asic_buff->pictures +
         VP8HwdBufferQueueGetAltRef(dec_cont->bq);
}

u32 VP8HwdBufferingMode(VP8DecContainer_t * dec_cont) {
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  u32 mode = VP8HWD_BUFFER_BASIC;

  if (p_asic_buff->custom_buffers) {
    mode = VP8HWD_BUFFER_CUSTOM;
  } else if (p_asic_buff->strides_used) {
    /* In the current implementation stride cannot be
     * enabled without separate buffers. */
    mode = VP8HWD_BUFFER_STRIDE;
    ASSERT(0 && "Error: Stride without custom buffers not implemented.");
  }
  return mode;
}

u32* VP8HwdRefStatusAddress(VP8DecContainer_t * dec_cont) {
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 chroma_width;
  u32 *p;

  switch (VP8HwdBufferingMode(dec_cont)) {
  case VP8HWD_BUFFER_BASIC:
    p = GetOutput(dec_cont)->virtual_address +
        dec_cont->asic_buff->width *
        dec_cont->asic_buff->height * 3 / 8;
    break;
  case VP8HWD_BUFFER_CUSTOM:
    chroma_width = (p_asic_buff->chroma_stride ?
                    p_asic_buff->chroma_stride : p_asic_buff->width);
    p = p_asic_buff->pictures_c[p_asic_buff->out_buffer_i].virtual_address +
        chroma_width * dec_cont->asic_buff->height / 8;
    break;
  case VP8HWD_BUFFER_STRIDE:
  default:
    ASSERT(0);
    p = NULL;
    break;
  }
  return p;
}
