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
#include "vp6dec.h"


typedef struct _sortnode {
  i32 next;
  i32 freq;
  tokenorptr value;
} sortnode;

/****************************************************************************
*
*  ROUTINE       :     BoolTreeToHuffCodes
*
*  INPUTS        :     u8  *BoolTreeProbs    -- Dct coeff tree node probs
*
*  OUTPUTS       :     u32 *HuffProbs        -- Dct coeff prob distribution
*
*  RETURNS       :     None.
*
*  FUNCTION      :     Convert zero run-length tree node probs to set
*                      of run-length probs (run lengths 1--8, and >8
*                      are the tokens).
*
*  SPECIAL NOTES :     None.
*
*
*  ERRORS        :     None.
*
****************************************************************************/
void VP6HW_BoolTreeToHuffCodes(const u8 * BoolTreeProbs, u32 * HuffProbs) {
  u32 Prob;
  u32 Prob1;

  HuffProbs[DCT_EOB_TOKEN] =
    ((u32) BoolTreeProbs[0] * (u32) BoolTreeProbs[1]) >> 8;
  HuffProbs[ZERO_TOKEN] =
    ((u32) BoolTreeProbs[0] * (255 - (u32) BoolTreeProbs[1])) >> 8;

  Prob = (255 - (u32) BoolTreeProbs[0]);
  HuffProbs[ONE_TOKEN] = (Prob * (u32) BoolTreeProbs[2]) >> 8;

  Prob = (Prob * (255 - (u32) BoolTreeProbs[2])) >> 8;
  Prob1 = (Prob * (u32) BoolTreeProbs[3]) >> 8;
  HuffProbs[TWO_TOKEN] = (Prob1 * (u32) BoolTreeProbs[4]) >> 8;
  Prob1 = (Prob1 * (255 - (u32) BoolTreeProbs[4])) >> 8;
  HuffProbs[THREE_TOKEN] = (Prob1 * (u32) BoolTreeProbs[5]) >> 8;
  HuffProbs[FOUR_TOKEN] = (Prob1 * (255 - (u32) BoolTreeProbs[5])) >> 8;

  Prob = (Prob * (255 - (u32) BoolTreeProbs[3])) >> 8;
  Prob1 = (Prob * (u32) BoolTreeProbs[6]) >> 8;
  HuffProbs[DCT_VAL_CATEGORY1] = (Prob1 * (u32) BoolTreeProbs[7]) >> 8;
  HuffProbs[DCT_VAL_CATEGORY2] =
    (Prob1 * (255 - (u32) BoolTreeProbs[7])) >> 8;

  Prob = (Prob * (255 - (u32) BoolTreeProbs[6])) >> 8;
  Prob1 = (Prob * (u32) BoolTreeProbs[8]) >> 8;
  HuffProbs[DCT_VAL_CATEGORY3] = (Prob1 * (u32) BoolTreeProbs[9]) >> 8;
  HuffProbs[DCT_VAL_CATEGORY4] =
    (Prob1 * (255 - (u32) BoolTreeProbs[9])) >> 8;

  Prob = (Prob * (255 - (u32) BoolTreeProbs[8])) >> 8;
  HuffProbs[DCT_VAL_CATEGORY5] = (Prob * (u32) BoolTreeProbs[10]) >> 8;
  HuffProbs[DCT_VAL_CATEGORY6] =
    (Prob * (255 - (u32) BoolTreeProbs[10])) >> 8;
}

/****************************************************************************
*
*  ROUTINE       :     ZerosBoolTreeToHuffCodes
*
*  INPUTS        :     u8  *BoolTreeProbs : Zrl tree node probabilities
*
*  OUTPUTS       :     u32 *HuffProbs     : Zrl run-length distribution
*
*  RETURNS       :     void
*
*  FUNCTION      :     Convert zero run-length tree node probs to set
*                      of run-length probs (run lengths 1--8, and >8
*                      are the tokens).
*
*  SPECIAL NOTES :     None.
*
****************************************************************************/
void VP6HW_ZerosBoolTreeToHuffCodes(const u8 * BoolTreeProbs, u32 * HuffProbs) {
  u32 Prob;

  Prob = ((u32) BoolTreeProbs[0] * (u32) BoolTreeProbs[1]) >> 8;
  HuffProbs[0] = (Prob * (u32) BoolTreeProbs[2]) >> 8;
  HuffProbs[1] = (Prob * (u32) (255 - BoolTreeProbs[2])) >> 8;

  Prob = ((u32) BoolTreeProbs[0] * (u32) (255 - BoolTreeProbs[1])) >> 8;
  HuffProbs[2] = (Prob * (u32) BoolTreeProbs[3]) >> 8;
  HuffProbs[3] = (Prob * (u32) (255 - BoolTreeProbs[3])) >> 8;

  Prob = ((u32) (255 - BoolTreeProbs[0]) * (u32) BoolTreeProbs[4]) >> 8;
  Prob = (Prob * (u32) BoolTreeProbs[5]) >> 8;
  HuffProbs[4] = (Prob * (u32) BoolTreeProbs[6]) >> 8;
  HuffProbs[5] = (Prob * (u32) (255 - BoolTreeProbs[6])) >> 8;

  Prob = ((u32) (255 - BoolTreeProbs[0]) * (u32) BoolTreeProbs[4]) >> 8;
  Prob = (Prob * (u32) (255 - BoolTreeProbs[5])) >> 8;
  HuffProbs[6] = (Prob * (u32) BoolTreeProbs[7]) >> 8;
  HuffProbs[7] = (Prob * (u32) (255 - BoolTreeProbs[7])) >> 8;

  Prob =
    ((u32) (255 - BoolTreeProbs[0]) * (u32) (255 - BoolTreeProbs[4])) >> 8;
  HuffProbs[8] = Prob;
}

/****************************************************************************
 *
 *  ROUTINE       :     InsertSorted
 *
 *  INPUTS        :     sortnode *sn   : Array of sort nodes.
 *                      i32 node       : Index of node to be inserted.
 *                      i32 *startnode : Pointer to head of linked-list.
 *
 *  OUTPUTS       :     i32 *startnode : Pointer to head of linked-list.
 *
 *  RETURNS       :     void
 *
 *  FUNCTION      :     Inserts a node into a sorted linklist.
 *
 *  SPECIAL NOTES :     None.
 *
 ****************************************************************************/
static void InsertSorted(sortnode * sn, i32 node, i32 *startnode) {
  i32 which = *startnode;
  i32 prior = *startnode;

  /*  find the position at which to insert the node */
  while(which != -1 && sn[node].freq > sn[which].freq) {
    prior = which;
    which = sn[which].next;
  }

  if(which == *startnode) {
    *startnode = node;
    sn[node].next = which;
  } else {
    sn[prior].next = node;
    sn[node].next = which;
  }
}

/****************************************************************************
 *
 *  ROUTINE       :     VP6_BuildHuffTree
 *
 *  INPUTS        :     i32 values           : Number of values in the tree.
 *                      u32 *counts : Histogram of token frequencies.
 *
 *  OUTPUTS       :     HUFF_NODE *hn        : Array of nodes (containing token frequency)
 *                                             from which to create tree.
 *                      u32 *counts : Histogram of token frequencies (0 freq clipped to 1).
 *
 *  RETURNS       :     void
 *
 *  FUNCTION      :     Creates a Huffman tree data structure from list
 *                      of token frequencies.
 *
 *  SPECIAL NOTES :     Maximum of 256 nodes can be handled.
 *
 ****************************************************************************/
void VP6HW_BuildHuffTree(HUFF_NODE * hn, u32 *counts, i32 values) {
  i32 i;
  sortnode sn[256];
  i32 sncount = 0;
  i32 startnode = 0;

  /*  NOTE: */
  /*  Create huffman tree in reverse order so that the root will always be 0 */
  i32 huffptr = values - 1;

  /*  Set up sorted linked list of values/pointers into the huffman tree */
  for(i = 0; i < values; i++) {
    sn[i].value.selector = 1;
    sn[i].value.value = i;

    if(counts[i] == 0)
      counts[i] = 1;
    sn[i].freq = counts[i];
    sn[i].next = -1;

  }

  sncount = values;

  /*  Connect above list into a linked list */
  for(i = 1; i < values; i++)
    InsertSorted(sn, i, &startnode);

  /*  while there is more than one node in our linked list */
  while(sn[startnode].next != -1) {
    i32 first = startnode;
    i32 second = sn[startnode].next;
    i32 sumfreq = sn[first].freq + sn[second].freq;

    /*  set-up new merged huffman node */
    --huffptr;

    hn[huffptr].leftunion.left = sn[first].value;
    hn[huffptr].rightunion.right = sn[second].value;

    /*         hn[huffptr].freq = 256 * sn[first].freq / sumfreq; */

    /*  set up new merged sort node pointing to our huffnode */
    sn[sncount].value.selector = 0;
    sn[sncount].value.value = huffptr;

    sn[sncount].freq = sumfreq;
    sn[sncount].next = -1;

    /*  remove the two nodes we just merged from the linked list */
    startnode = sn[second].next;

    /*  insert the new sort node into the proper location */
    InsertSorted(sn, sncount, &startnode);

    /*  account for new nodes */
    sncount++;
  }

  return;
}

/****************************************************************************
 *
 *  ROUTINE       :     VP6_BuildHuffLookupTable
 *
 *  INPUTS        :     HUFF_NODE *HuffTreeRoot : Pointer to root of Huffman tree.
 *
 *  OUTPUTS       :     u16 *HuffTable       : Array (LUT) of Huffman codes.
 *
 *  RETURNS       :     void
 *
 *  FUNCTION      :     Traverse Huffman tree to create LUT of Huffman codes.
 *
 *  SPECIAL NOTES :     None.
 *
 ****************************************************************************/
void VP6HW_BuildHuffLookupTable(HUFF_NODE * HuffTreeRoot, u16 * HuffTable) {
  i32 i, j;
  i32 bits;
  tokenorptr torp;

  for(i = 0; i < (1 << HUFF_LUT_LEVELS); i++) {
    bits = i;
    j = 0;

    torp.value = 0;
    torp.selector = 0;

    do {
      j++;
      if((bits >> (HUFF_LUT_LEVELS - j)) & 1)
        torp = HuffTreeRoot[torp.value].rightunion.right;
      else
        torp = HuffTreeRoot[torp.value].leftunion.left;
    } while(!(torp.selector) && (j < HUFF_LUT_LEVELS));

    ((HUFF_TABLE_NODE *) HuffTable)[i].value = torp.value;
    ((HUFF_TABLE_NODE *) HuffTable)[i].flag = torp.selector;
    ((HUFF_TABLE_NODE *) HuffTable)[i].length = j;
  }
}

static void ProcessTreeNode(const HUFF_NODE * hn, tokenorptr torp, u32 code,
                            u32 len, u16 * HuffTable) {
  if( torp.selector ) {
    /* it is a leaf! */
    HuffTable[torp.value] = (code << 4) | len;
  } else {
    /* it is a node, process children */
    code <<= 1;
    ProcessTreeNode(hn, hn[torp.value].leftunion.left, code,
                    len+1, HuffTable);
    ProcessTreeNode(hn, hn[torp.value].rightunion.right, code+1,
                    len+1, HuffTable);
  }
}

void VP6HW_CreateHuffmanLUT(const HUFF_NODE * hn, u16 * HuffTable, i32 values) {
  tokenorptr torp;

  torp.value = 0;
  torp.selector = 0;

  /* start with root */
  ProcessTreeNode( hn, torp, 0, 0, HuffTable );
}
