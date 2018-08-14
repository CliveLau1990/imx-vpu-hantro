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

#if defined(CONFIG_DEBUG) && CONFIG_DEBUG
#include <assert.h>
#endif
#include <stdio.h>

#include "vp9_treecoder.h"

static unsigned int ConvertDistribution(unsigned int i, vp9_tree tree,
                                        vp9_prob probs[],
                                        unsigned int branch_ct[][2],
                                        const unsigned int num_events[],
                                        unsigned int tok0_offset) {
  unsigned int left, right;

  if (tree[i] <= 0) {
    left = num_events[-tree[i] - tok0_offset];
  } else {
    left = ConvertDistribution(tree[i], tree, probs, branch_ct, num_events,
                               tok0_offset);
  }
  if (tree[i + 1] <= 0) {
    right = num_events[-tree[i + 1] - tok0_offset];
  } else {
    right = ConvertDistribution(tree[i + 1], tree, probs, branch_ct, num_events,
                                tok0_offset);
  }
  probs[i >> 1] = GetBinaryProb(left, right);
  branch_ct[i >> 1][0] = left;
  branch_ct[i >> 1][1] = right;
  return left + right;
}

void Vp9TreeProbsFromDistribution(vp9_tree tree, vp9_prob probs[/* n-1 */],
                                  unsigned int branch_ct[/* n-1 */][2],
                                  const unsigned int num_events[/* n */],
                                  unsigned int tok0_offset) {
  ConvertDistribution(0, tree, probs, branch_ct, num_events, tok0_offset);
}
