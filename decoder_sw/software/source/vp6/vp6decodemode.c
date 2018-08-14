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
#include "vp6dec.h"
#include "vp6decodemode.h"

/****************************************************************************
 *
 *  ROUTINE       : VP6HWdecodeModeDiff
 *
 *  INPUTS        : PB_INSTANCE *pbi  : Pointer to decoder instance.
 *
 *  OUTPUTS       : None.
 *
 *  RETURNS       : a probability difference value decoded from the bitstream.
 *
 *  FUNCTION      : this function returns a probability difference value in
 *                  the range -256 to +256 (in steps of 4) transmitted in the
 *                  bitstream using a fixed tree with hardcoded probabilities.
 *
 *  SPECIAL NOTES : The hard coded probabilities for the difference tree
 *                  were calcualated by taking the average number of times a
 *                  branch was taken on some sample material ie
 *                  (bond,bike,beautifulmind)
 *
 ****************************************************************************/
int VP6HWdecodeModeDiff ( PB_INSTANCE *pbi ) {
  int sign;

  if ( VP6HWDecodeBool(&pbi->br, 205) == 0 )
    return 0;

  sign = 1 + -2 * VP6HWDecodeBool128(&pbi->br);

  if( !VP6HWDecodeBool(&pbi->br,171) ) {
    return sign<<(3-VP6HWDecodeBool(  &pbi->br,83));
  } else {
    if( !VP6HWDecodeBool( &pbi->br,199) ) {
      if(VP6HWDecodeBool( &pbi->br,140))
        return sign * 12;

      if(VP6HWDecodeBool( &pbi->br,125))
        return sign * 16;

      if(VP6HWDecodeBool( &pbi->br,104))
        return sign * 20;

      return sign * 24;
    } else {
      int diff = VP6HWbitread(&pbi->br,7);
      return sign * diff * 4;
    }
  }
}
/****************************************************************************
 *
 *  ROUTINE       : VP6HWBuildModeTree
 *
 *  INPUTS        : PB_INSTANCE *pbi  : Pointer to decoder instance.
 *
 *  OUTPUTS       : None.
 *
 *  RETURNS       : void
 *
 *  FUNCTION      : Fills in probabilities at each branch of the huffman tree
 *                  based upon prob_xmitted, the frequencies transmitted in the bitstream.
 *
 ****************************************************************************/
void VP6HWBuildModeTree ( PB_INSTANCE *pbi ) {
  int i,j,k;

  /*  create a huffman tree and code array for each of our modes  */
  /*  Note: each of the trees is minus the node give by probmodesame */
  for ( i=0; i<10; i++ ) {
    unsigned int Counts[MAX_MODES];
    unsigned int total;

    /*  set up the probabilities for each tree */
    for(k=0; k<MODETYPES; k++) {
      total=0;
      for ( j=0; j<10; j++ ) {
        if ( i == j ) {
          Counts[j]=0;
        } else {
          Counts[j]=100*pbi->prob_xmitted[k][0][j];
        }

        total+=Counts[j];
      }

      pbi->prob_mode_same[k][i] = 255-
                                  255 * pbi->prob_xmitted[k][1][i]
                                  /
                                  ( 1 +
                                    pbi->prob_xmitted[k][1][i] +
                                    pbi->prob_xmitted[k][0][i]
                                  );

      /*  each branch is basically calculated via  */
      /*  summing all posibilities at that branch. */
      pbi->prob_mode[k][i][0]= 1 + 255 *
                               (
                                 Counts[CODE_INTER_NO_MV]+
                                 Counts[CODE_INTER_PLUS_MV]+
                                 Counts[CODE_INTER_NEAREST_MV]+
                                 Counts[CODE_INTER_NEAR_MV]
                               ) /
                               (   1 +
                                   total
                               );

      pbi->prob_mode[k][i][1]= 1 + 255 *
                               (
                                 Counts[CODE_INTER_NO_MV]+
                                 Counts[CODE_INTER_PLUS_MV]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_INTER_NO_MV]+
                                 Counts[CODE_INTER_PLUS_MV]+
                                 Counts[CODE_INTER_NEAREST_MV]+
                                 Counts[CODE_INTER_NEAR_MV]
                               );

      pbi->prob_mode[k][i][2]= 1 + 255 *
                               (
                                 Counts[CODE_INTRA]+
                                 Counts[CODE_INTER_FOURMV]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_INTRA]+
                                 Counts[CODE_INTER_FOURMV]+
                                 Counts[CODE_USING_GOLDEN]+
                                 Counts[CODE_GOLDEN_MV]+
                                 Counts[CODE_GOLD_NEAREST_MV]+
                                 Counts[CODE_GOLD_NEAR_MV]
                               );

      pbi->prob_mode[k][i][3]= 1 + 255 *
                               (
                                 Counts[CODE_INTER_NO_MV]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_INTER_NO_MV]+
                                 Counts[CODE_INTER_PLUS_MV]
                               );

      pbi->prob_mode[k][i][4]= 1 + 255 *
                               (
                                 Counts[CODE_INTER_NEAREST_MV]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_INTER_NEAREST_MV]+
                                 Counts[CODE_INTER_NEAR_MV]
                               ) ;

      pbi->prob_mode[k][i][5]= 1 + 255 *
                               (
                                 Counts[CODE_INTRA]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_INTRA]+
                                 Counts[CODE_INTER_FOURMV]
                               );

      pbi->prob_mode[k][i][6]= 1 + 255 *
                               (
                                 Counts[CODE_USING_GOLDEN]+
                                 Counts[CODE_GOLDEN_MV]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_USING_GOLDEN]+
                                 Counts[CODE_GOLDEN_MV]+
                                 Counts[CODE_GOLD_NEAREST_MV]+
                                 Counts[CODE_GOLD_NEAR_MV]
                               );

      pbi->prob_mode[k][i][7]= 1 + 255 *
                               (
                                 Counts[CODE_USING_GOLDEN]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_USING_GOLDEN]+
                                 Counts[CODE_GOLDEN_MV]
                               );

      pbi->prob_mode[k][i][8]= 1 + 255 *
                               (
                                 Counts[CODE_GOLD_NEAREST_MV]
                               ) /
                               (
                                 1 +
                                 Counts[CODE_GOLD_NEAREST_MV]+
                                 Counts[CODE_GOLD_NEAR_MV]
                               );
    }
  }
}

/****************************************************************************
 *
 *  ROUTINE       :     VP6HWDecodeModeProbs
 *
 *  INPUTS        :     PB_INSTANCE *pbi  : Pointer to decoder instance.
 *
 *  OUTPUTS       :     None.
 *
 *  RETURNS       :     void
 *
 *  FUNCTION      :     This function parses the probabilities transmitted in
 *                      the bitstream. The bitstream may either use the
 *                      last frames' baselines, or transmit a pointer to a
 *                      vector of new probabilities. It may then additionally
 *                      contain updates to each of these probabilities.
 *
 *  SPECIAL NOTES :     None.
 *
 ****************************************************************************/
void VP6HWDecodeModeProbs ( PB_INSTANCE *pbi ) {
  int i,j;

  /*  For each mode type (all modes available, no nearest, no near mode) */
  for ( j=0; j<MODETYPES; j++ ) {
    /*  determine whether we are sending a vector for this mode byte */
    if ( VP6HWDecodeBool( &pbi->br, PROBVECTORXMIT ) ) {
      /*  figure out which vector we have encoded */
      int which_vector = VP6HWbitread(&pbi->br, 4);

      /*  adjust the vector */
      for ( i=0; i<MAX_MODES; i++ ) {
        pbi->prob_xmitted[j][1][i] = VP6HWModeVq[j][which_vector][i*2];
        pbi->prob_xmitted[j][0][i] = VP6HWModeVq[j][which_vector][i*2+1];
      }

      pbi->prob_mode_update = 1;
    }

    /*  decode updates to bring it closer to ideal  */
    if ( VP6HWDecodeBool( &pbi->br, PROBIDEALXMIT) ) {
      for ( i=0; i<10; i++ ) {
        int diff;

        /*  determine difference  */
        diff = VP6HWdecodeModeDiff(pbi);
        diff += pbi->prob_xmitted[j][1][i];

        pbi->prob_xmitted[j][1][i] = ( diff<0 ? 0 : (diff>255?255:diff) );

        /*  determine difference  */
        diff = VP6HWdecodeModeDiff(pbi);
        diff += pbi->prob_xmitted[j][0][i];

        pbi->prob_xmitted[j][0][i] = ( diff<0 ? 0 : (diff>255?255:diff) );

      }

      pbi->prob_mode_update = 1;
    }
  }

  VP6HWBuildModeTree(pbi);
}
