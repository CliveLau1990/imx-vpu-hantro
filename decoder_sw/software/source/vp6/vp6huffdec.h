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

#ifndef __VP6HUFFDEC_H__
#define __VP6HUFFDEC_H__

/*  VP6 hufman table AC bands */
#define VP6HWAC_BANDS     6

/*  Tokens                               Value       Extra Bits (range + sign) */
#define ZERO_TOKEN              0   /* 0         Extra Bits 0+0 */
#define ONE_TOKEN               1   /* 1         Extra Bits 0+1 */
#define TWO_TOKEN               2   /* 2         Extra Bits 0+1 */
#define THREE_TOKEN             3   /* 3         Extra Bits 0+1 */
#define FOUR_TOKEN              4   /* 4         Extra Bits 0+1 */
#define DCT_VAL_CATEGORY1       5   /* 5-6       Extra Bits 1+1 */
#define DCT_VAL_CATEGORY2   6   /* 7-10      Extra Bits 2+1 */
#define DCT_VAL_CATEGORY3   7   /* 11-26     Extra Bits 4+1 */
#define DCT_VAL_CATEGORY4   8   /* 11-26     Extra Bits 5+1 */
#define DCT_VAL_CATEGORY5   9   /* 27-58     Extra Bits 5+1 */
#define DCT_VAL_CATEGORY6   10  /* 59+       Extra Bits 11+1 */
#define DCT_EOB_TOKEN           11  /* EOB       Extra Bits 0+0 */
#define MAX_ENTROPY_TOKENS      (DCT_EOB_TOKEN + 1)
#define ILLEGAL_TOKEN     255

#define DC_TOKEN_CONTEXTS   3   /*  00, 0!0, !0!0 */
#define CONTEXT_NODES     (MAX_ENTROPY_TOKENS-7)

#define PREC_CASES        3
#define ZERO_RUN_PROB_CASES     14

#define DC_PROBABILITY_UPDATE_THRESH  100

#define ZERO_CONTEXT_NODE   0
#define EOB_CONTEXT_NODE    1
#define ONE_CONTEXT_NODE    2
#define LOW_VAL_CONTEXT_NODE  3
#define TWO_CONTEXT_NODE    4
#define THREE_CONTEXT_NODE    5
#define HIGH_LOW_CONTEXT_NODE 6
#define CAT_ONE_CONTEXT_NODE  7
#define CAT_THREEFOUR_CONTEXT_NODE  8
#define CAT_THREE_CONTEXT_NODE  9
#define CAT_FIVE_CONTEXT_NODE 10

#define PROB_UPDATE_BASELINE_COST 7

#define MAX_PROB        254
#define DCT_MAX_VALUE     2048

#define ZRL_BANDS       2
#define ZRL_BAND2       6

#define SCAN_ORDER_BANDS    16
#define SCAN_BAND_UPDATE_BITS   4

#define HUFF_LUT_LEVELS         6

typedef struct _tokenorptr {
  u16 selector:1;         /*  1 bit selector 0->ptr, 1->token */
  u16 value:7;
} tokenorptr;

typedef struct _dhuffnode {

  union {

    i8 l;

    tokenorptr left;

  } leftunion;

  union {

    i8 r;

    tokenorptr right;

  } rightunion;
} HUFF_NODE;
typedef struct _HUFF_TABLE_NODE {

  u16 flag:1;             /*  bit 0: 1-Token, 0-Index
 */
  u16 value:5;             /*  value: the value of the Token or the Index to the huffman tree
 */
  u16 unused:6;            /*  not used for now
 */
  u16 length:4;            /*  Huffman code length of the token
 */
} HUFF_TABLE_NODE;

typedef struct HUFF_INSTANCE {

  /* Huffman code tables for DC, AC & Zero Run Length */
  /*u32 DcHuffCode[2][MAX_ENTROPY_TOKENS]; */

  /*u8 DcHuffLength[2][MAX_ENTROPY_TOKENS]; */

  u32 DcHuffProbs[2][MAX_ENTROPY_TOKENS];

  HUFF_NODE DcHuffTree[2][MAX_ENTROPY_TOKENS];

  /*u32 AcHuffCode[PREC_CASES][2][VP6HWAC_BANDS][MAX_ENTROPY_TOKENS]; */

  /*u8 AcHuffLength[PREC_CASES][2][VP6HWAC_BANDS][MAX_ENTROPY_TOKENS]; */

  u32 AcHuffProbs[PREC_CASES][2][VP6HWAC_BANDS][MAX_ENTROPY_TOKENS];

  HUFF_NODE AcHuffTree[PREC_CASES][2][VP6HWAC_BANDS][MAX_ENTROPY_TOKENS];

  /*u32 ZeroHuffCode[ZRL_BANDS][ZERO_RUN_PROB_CASES]; */

  /*u8 ZeroHuffLength[ZRL_BANDS][ZERO_RUN_PROB_CASES]; */

  u32 ZeroHuffProbs[ZRL_BANDS][ZERO_RUN_PROB_CASES];

  HUFF_NODE ZeroHuffTree[ZRL_BANDS][ZERO_RUN_PROB_CASES];

#if 0
  /* FAST look-up-table for huffman Trees */
  u16 DcHuffLUT[2][1 << HUFF_LUT_LEVELS];

  u16 AcHuffLUT[PREC_CASES][2][VP6HWAC_BANDS][1 << HUFF_LUT_LEVELS];

  u16 ZeroHuffLUT[ZRL_BANDS][1 << HUFF_LUT_LEVELS];

  /* Counters for runs of zeros at DC & EOB at first AC position in Huffman mode */
  i32 CurrentDcRunLen[2];

  i32 CurrentAc1RunLen[2];
#endif

  u16 DcHuffLUT[2][12];

  u16 AcHuffLUT[2][3][/*6*/4][12];

  u16 ZeroHuffLUT[2][12];

} HUFF_INSTANCE;

void VP6HW_BoolTreeToHuffCodes(const u8 * BoolTreeProbs, u32 * HuffProbs);
void VP6HW_ZerosBoolTreeToHuffCodes(const u8 * BoolTreeProbs, u32 * HuffProbs);

void VP6HW_BuildHuffTree(HUFF_NODE * hn, u32 * counts, i32 values);
void VP6HW_CreateHuffmanLUT(const HUFF_NODE * hn, u16 * HuffTable, i32 values);

#endif /* __VP6HUFFDEC_H__ */
