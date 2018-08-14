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

#ifndef __VP6DEC_H__
#define __VP6DEC_H__

#include "basetype.h"
#include "vp6strmbuffer.h"
#include "vp6booldec.h"
#include "vp6huffdec.h"

/*  Enumeration of how block is coded */
/*  VP6.2 version is >= 8 */
#define CURRENT_DECODE_VERSION  8U

#define SIMPLE_PROFILE      0U
#define PROFILE_1       1U
#define PROFILE_2       2U
#define ADVANCED_PROFILE    3U

#define BASE_FRAME              0U
#define NORMAL_FRAME            1
#define Q_TABLE_SIZE            64
#define BLOCK_HEIGHT_WIDTH      8
#define BLOCK_SIZE              (BLOCK_HEIGHT_WIDTH * BLOCK_HEIGHT_WIDTH)

/*  Loop filter options */
#define NO_LOOP_FILTER      0U
#define LOOP_FILTER_BASIC   2U
#define LOOP_FILTER_DERING    3U

#define BILINEAR_ONLY_PM      0U
#define BICUBIC_ONLY_PM       1U
#define AUTO_SELECT_PM        2U

#define LONG_MV_BITS            8
#define MAX_MODES               10
#define MV_NODES              17

#define DCProbOffset(A,B) \
    ((A) * (MAX_ENTROPY_TOKENS - 1) + (B))

#define ACProbOffset(A,B,C,D) \
    ((A) * PREC_CASES * VP6HWAC_BANDS * (MAX_ENTROPY_TOKENS - 1) \
    + (B) *    VP6HWAC_BANDS * (MAX_ENTROPY_TOKENS - 1) \
    + (C) * (MAX_ENTROPY_TOKENS - 1) \
    + (D))

#define DcNodeOffset(A,B,C) \
    ((A) * DC_TOKEN_CONTEXTS * CONTEXT_NODES \
    + (B) * CONTEXT_NODES + (C))

/*  Playback Instance Definition */
typedef struct PB_INSTANCE {
  Vp6StrmBuffer strm;
  BOOL_CODER br;
  BOOL_CODER br2;
  HUFF_INSTANCE *huff;

  /*  Decoder and Frame Type Information */
  u8 Vp3VersionNo;
  u8 VpProfile;

  u8 FrameType;

  u32 VFragments;
  u32 HFragments;

  u32 OutputWidth;
  u32 OutputHeight;

  u32 ScalingMode;

  u8 PredictionFilterMode;
  u8 PredictionFilterMvSizeThresh;
  u32 PredictionFilterVarThresh;
  u8 PredictionFilterAlpha;

  u32 RefreshGoldenFrame;

  /*  Does this frame use multiple data streams */
  /*  Multistream is implicit for SIMPLE_PROFILE */
  u32 MultiStream;

  /*  Second partition buffer details */
  u32 Buff2Offset;

  u32 UseHuffman;

  /*  Should we do loop filtering. */
  /*  In simple profile this is ignored and there is no loop filtering  */
  u8 UseLoopFilter;

  u32 DctQMask;

  u8 MvSignProbs[2];

  u8 IsMvShortProb[2];

  u8 MvShortProbs[2][7];

  u8 MvSizeProbs[2][LONG_MV_BITS];

  u8 prob_xmitted[4][2][MAX_MODES];

  u8 prob_mode_same[4][MAX_MODES];

  u8 prob_mode[4][MAX_MODES][MAX_MODES - 1];   /*  nearest+near,nearest only, nonearest+nonear, 10 preceding modes, 9 nodes */

  u8 DcProbs[2 * (MAX_ENTROPY_TOKENS - 1)];

  u8 AcProbs[2 * PREC_CASES * VP6HWAC_BANDS * (MAX_ENTROPY_TOKENS - 1)];
  u8 DcNodeContexts[2 * DC_TOKEN_CONTEXTS * CONTEXT_NODES];   /*  Plane, Contexts, Node */
  u8 ZeroRunProbs[ZRL_BANDS][ZERO_RUN_PROB_CASES];

  u8 ModifiedScanOrder[BLOCK_SIZE];
  u8 MergedScanOrder[BLOCK_SIZE + 65];
  u8 EobOffsetTable[BLOCK_SIZE];
  u8 ScanBands[BLOCK_SIZE];

  u8 prob_mode_update;
  u8 prob_mv_update;
  u8 scan_update;
  u8 prob_dc_update;
  u8 prob_ac_update;
  u8 prob_zrl_update;

} PB_INSTANCE;

extern const i32 VP6HW_BicubicFilterSet[17][8][4];
extern const u8 VP6HWDeblockLimitValues[Q_TABLE_SIZE];

i32 VP6HWLoadFrameHeader(PB_INSTANCE * pbi);
i32 VP6HWDecodeProbUpdates(PB_INSTANCE * pbi);

void VP6HWDeleteHuffman(PB_INSTANCE * pbi);

#endif /* __VP6DEC_H__ */
