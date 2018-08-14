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
#include "vp6decodemv.h"
#include "vp6dec.h"

/****************************************************************************
 *
 *  ROUTINE       :     VP6HWConfigureMvEntropyDecoder
 *
 *  INPUTS        :     PB_INSTANCE *pbi : Pointer to decoder instance.
 *                      u8 FrameType  : Type of the frame.
 *
 *  OUTPUTS       :     None.
 *
 *  RETURNS       :     void
 *
 *  FUNCTION      :     Builds the MV entropy decoding tree.
 *
 *  SPECIAL NOTES :     None.
 *
***************************************************************************/
void VP6HWConfigureMvEntropyDecoder(PB_INSTANCE * pbi, u8 FrameType) {
  i32 i;

  (void) FrameType;

  /* This funciton is not called at all for a BASE_FRAME
   * Read any changes to mv probabilities. */
  for(i = 0; i < 2; i++) {
    /* Short vector probability */
    if(VP6HWDecodeBool(&pbi->br, VP6HWMvUpdateProbs[i][0])) {
      pbi->IsMvShortProb[i] =
        VP6HWbitread(&pbi->br, PROB_UPDATE_BASELINE_COST) << 1;
      if(pbi->IsMvShortProb[i] == 0)
        pbi->IsMvShortProb[i] = 1;

      pbi->prob_mv_update = 1;
    }

    /* Sign probability */
    if(VP6HWDecodeBool(&pbi->br, VP6HWMvUpdateProbs[i][1])) {
      pbi->MvSignProbs[i] =
        VP6HWbitread(&pbi->br, PROB_UPDATE_BASELINE_COST) << 1;
      if(pbi->MvSignProbs[i] == 0)
        pbi->MvSignProbs[i] = 1;

      pbi->prob_mv_update = 1;
    }
  }

  /* Short vector tree node probabilities */
  for(i = 0; i < 2; i++) {
    u32 j;
    u32 MvUpdateProbsOffset = 2;    /* Offset into MvUpdateProbs[i][] */

    for(j = 0; j < 7; j++) {
      if(VP6HWDecodeBool
          (&pbi->br, VP6HWMvUpdateProbs[i][MvUpdateProbsOffset])) {
        pbi->MvShortProbs[i][j] =
          VP6HWbitread(&pbi->br, PROB_UPDATE_BASELINE_COST) << 1;
        if(pbi->MvShortProbs[i][j] == 0)
          pbi->MvShortProbs[i][j] = 1;

        pbi->prob_mv_update = 1;
      }
      MvUpdateProbsOffset++;
    }
  }

  /* Long vector tree node probabilities */
  for(i = 0; i < 2; i++) {
    u32 j;
    u32 MvUpdateProbsOffset = 2 + 7;

    for(j = 0; j < LONG_MV_BITS; j++) {
      if(VP6HWDecodeBool
          (&pbi->br, VP6HWMvUpdateProbs[i][MvUpdateProbsOffset])) {
        pbi->MvSizeProbs[i][j] =
          VP6HWbitread(&pbi->br, PROB_UPDATE_BASELINE_COST) << 1;
        if(pbi->MvSizeProbs[i][j] == 0)
          pbi->MvSizeProbs[i][j] = 1;

        pbi->prob_mv_update = 1;
      }
      MvUpdateProbsOffset++;
    }
  }
}
