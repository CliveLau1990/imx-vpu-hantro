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

#ifndef VP9_COMMON_VP9_ENTROPYMODE_H_
#define VP9_COMMON_VP9_ENTROPYMODE_H_

#include "commonvp9.h"
#include "vp9_treecoder.h"
#include "vp9hwd_decoder.h"

#define DEFAULT_COMP_INTRA_PROB 32

#define VP9_DEF_INTERINTRA_PROB 248
#define VP9_UPD_INTERINTRA_PROB 192
#define SEPARATE_INTERINTRA_UV 0

extern const vp9_prob vp9_kf_default_bmode_probs
[VP9_INTRA_MODES][VP9_INTRA_MODES][VP9_INTRA_MODES - 1];

extern const vp9_tree_index vp9_intra_mode_tree[];
extern const vp9_tree_index vp9_sb_mv_ref_tree[];

extern struct vp9_token vp9_intra_mode_encodings[VP9_INTRA_MODES];

/* Inter mode values do not start at zero */

extern struct vp9_token vp9_sb_mv_ref_encoding_array[VP9_INTER_MODES];

/* probability models for partition information */
extern const vp9_tree_index vp9_partition_tree[];
extern struct vp9_token vp9_partition_encodings[PARTITION_TYPES];
extern const vp9_prob vp9_partition_probs
[NUM_FRAME_TYPES][NUM_PARTITION_CONTEXTS][PARTITION_TYPES];

void Vp9EntropyModeInit(void);

struct VP9Common;

void Vp9InitMbmodeProbs(struct Vp9Decoder *x);

extern void Vp9InitModeContexts(struct Vp9Decoder *pc);

extern const enum InterpolationFilterType
vp9_switchable_interp[VP9_SWITCHABLE_FILTERS];

extern const int vp9_switchable_interp_map[SWITCHABLE + 1];

extern const vp9_tree_index
vp9_switchable_interp_tree[2 * (VP9_SWITCHABLE_FILTERS - 1)];

extern struct vp9_token vp9_switchable_interp_encodings[VP9_SWITCHABLE_FILTERS];

extern const vp9_prob vp9_switchable_interp_prob[VP9_SWITCHABLE_FILTERS +
    1][VP9_SWITCHABLE_FILTERS - 1];

extern const vp9_prob
vp9_default_tx_probs_32x32p[TX_SIZE_CONTEXTS][TX_SIZE_MAX_SB - 1];
extern const vp9_prob
vp9_default_tx_probs_16x16p[TX_SIZE_CONTEXTS][TX_SIZE_MAX_SB - 2];
extern const vp9_prob
vp9_default_tx_probs_8x8p[TX_SIZE_CONTEXTS][TX_SIZE_MAX_SB - 3];

#endif
