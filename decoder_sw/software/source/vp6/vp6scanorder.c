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
#include "vp6scanorder.h"
#include "vp6dec.h"

/****************************************************************************
*
*  ROUTINE       :     BuildScanOrder
*
*  INPUTS        :     PB_INSTANCE *pbi : Pointer to instance of a decoder.
*                      u8 *ScanBands : Pointer to array containing band for
*                                         each DCT coeff position.
*
*  OUTPUTS       :     None
*
*  RETURNS       :     void
*
*  FUNCTION      :     Builds a custom dct scan order from a set of band data.
*
*  SPECIAL NOTES :     None.
*
****************************************************************************/
void VP6HWBuildScanOrder(PB_INSTANCE * pbi, u8 * ScanBands) {
  u32 i, j;
  u32 ScanOrderIndex = 1;
  u32 MaxOffset;

  /*  DC is fixed */
  pbi->ModifiedScanOrder[0] = 0;

  /*  Create a scan order where within each band the coefs are in ascending order */
  /*  (in terms of their original zig-zag positions). */
  for(i = 0; i < SCAN_ORDER_BANDS; i++) {
    for(j = 1; j < BLOCK_SIZE; j++) {
      if(ScanBands[j] == i) {
        pbi->ModifiedScanOrder[ScanOrderIndex] = j;
        ScanOrderIndex++;
      }
    }
  }

  /*  For each of the positions in the modified scan order work out the  */
  /*  worst case EOB offset in zig zag order. This is used in selecting */
  /*  the appropriate idct variant */
  for(i = 0; i < BLOCK_SIZE; i++) {
    MaxOffset = 0;

    for(j = 0; j <= i; j++) {
      if(pbi->ModifiedScanOrder[j] > MaxOffset)
        MaxOffset = pbi->ModifiedScanOrder[j];
    }

    pbi->EobOffsetTable[i] = MaxOffset;

    if(pbi->Vp3VersionNo > 6)
      pbi->EobOffsetTable[i] = MaxOffset + 1;
  }

  {
    i32 i;

    for(i = 0; i < 64; i++) {
      pbi->MergedScanOrder[i] =
        VP6HWtransIndexC[pbi->ModifiedScanOrder[i]];
    }

#if 0   /* TODO: is this needed? */
    /*  Create Huffman codes for tokens based on tree probabilities */
    if(pbi->UseHuffman) {
      for(i = 64; i < 64 + 65; i++) {
        pbi->MergedScanOrder[i] = VP6HWCoeffToHuffBand[i - 64];
      }
    } else {
      for(i = 64; i < 64 + 65; i++)
        pbi->MergedScanOrder[i] = VP6HWCoeffToBand[i - 64];
    }
#endif

  }

}
