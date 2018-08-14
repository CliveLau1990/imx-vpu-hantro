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

#include "h264hwd_neighbour.h"
#include "h264hwd_util.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* Following four tables indicate neighbours of each block of a macroblock.
 * First 16 values are for luma blocks, next 4 values for Cb and last 4
 * values for Cr. Elements of the table indicate to which macroblock the
 * neighbour block belongs and the index of the neighbour block in question.
 * Indexing of the blocks goes as follows
 *
 *          Y             Cb       Cr
 *      0  1  4  5      16 17    20 21
 *      2  3  6  7      18 19    22 23
 *      8  9 12 13
 *     10 11 14 15
 */

/* left neighbour for each block */
static const neighbour_t N_A_4x4B[24] = {
  {MB_A,5},    {MB_CURR,0}, {MB_A,7},    {MB_CURR,2},
  {MB_CURR,1}, {MB_CURR,4}, {MB_CURR,3}, {MB_CURR,6},
  {MB_A,13},   {MB_CURR,8}, {MB_A,15},   {MB_CURR,10},
  {MB_CURR,9}, {MB_CURR,12},{MB_CURR,11},{MB_CURR,14},
  {MB_A,17},   {MB_CURR,16},{MB_A,19},   {MB_CURR,18},
  {MB_A,21},   {MB_CURR,20},{MB_A,23},   {MB_CURR,22}
};

/* above neighbour for each block */
static const neighbour_t N_B_4x4B[24] = {
  {MB_B,10},   {MB_B,11},   {MB_CURR,0}, {MB_CURR,1},
  {MB_B,14},   {MB_B,15},   {MB_CURR,4}, {MB_CURR,5},
  {MB_CURR,2}, {MB_CURR,3}, {MB_CURR,8}, {MB_CURR,9},
  {MB_CURR,6}, {MB_CURR,7}, {MB_CURR,12},{MB_CURR,13},
  {MB_B,18},   {MB_B,19},   {MB_CURR,16},{MB_CURR,17},
  {MB_B,22},   {MB_B,23},   {MB_CURR,20},{MB_CURR,21}
};

#if 0 /* not in use currently */
/* above-right neighbour for each block */
static const neighbour_t N_C_4x4B[24] = {
  {MB_B,11},   {MB_B,14},   {MB_CURR,1}, {MB_NA,4},
  {MB_B,15},   {MB_C,10},   {MB_CURR,5}, {MB_NA,0},
  {MB_CURR,3}, {MB_CURR,6}, {MB_CURR,9}, {MB_NA,12},
  {MB_CURR,7}, {MB_NA,2},   {MB_CURR,13},{MB_NA,8},
  {MB_B,19},   {MB_C,18},   {MB_CURR,17},{MB_NA,16},
  {MB_B,23},   {MB_C,22},   {MB_CURR,21},{MB_NA,20}
};
#endif

/* above-left neighbour for each block */
static const neighbour_t N_D_4x4B[24] = {
  {MB_D,15},   {MB_B,10},   {MB_A,5},    {MB_CURR,0},
  {MB_B,11},   {MB_B,14},   {MB_CURR,1}, {MB_CURR,4},
  {MB_A,7},    {MB_CURR,2}, {MB_A,13},   {MB_CURR,8},
  {MB_CURR,3}, {MB_CURR,6}, {MB_CURR,9}, {MB_CURR,12},
  {MB_D,19},   {MB_B,18},   {MB_A,17},   {MB_CURR,16},
  {MB_D,23},   {MB_B,22},   {MB_A,21},   {MB_CURR,20}
};

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function: h264bsdInitMbNeighbours

        Functional description:
            Initialize macroblock neighbours. Function sets neighbour
            macroblock pointers in macroblock structures to point to
            macroblocks on the left, above, above-right and above-left.
            Pointers are set NULL if the neighbour does not fit into the
            picture.

        Inputs:
            pic_width        width of the picture in macroblocks
            pic_size_in_mbs    no need to clarify

        Outputs:
            p_mb_storage      neighbour pointers of each mbStorage structure
                            stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void h264bsdInitMbNeighbours(mbStorage_t *p_mb_storage, u32 pic_width,
                             u32 pic_size_in_mbs) {

  /* Variables */

  u32 i, row, col;

  /* Code */

  ASSERT(p_mb_storage);
  ASSERT(pic_width);
  ASSERT(pic_width <= pic_size_in_mbs);
  ASSERT(((pic_size_in_mbs / pic_width) * pic_width) == pic_size_in_mbs);

  row = col = 0;

  for (i = 0; i < pic_size_in_mbs; i++) {

    if (col)
      p_mb_storage[i].mb_a = p_mb_storage + i - 1;
    else
      p_mb_storage[i].mb_a = NULL;

    if (row)
      p_mb_storage[i].mb_b = p_mb_storage + i - pic_width;
    else
      p_mb_storage[i].mb_b = NULL;

    if (row && (col < pic_width - 1))
      p_mb_storage[i].mb_c = p_mb_storage + i - (pic_width - 1);
    else
      p_mb_storage[i].mb_c = NULL;

    if (row && col)
      p_mb_storage[i].mb_d = p_mb_storage + i - (pic_width + 1);
    else
      p_mb_storage[i].mb_d = NULL;

    col++;
    if (col == pic_width) {
      col = 0;
      row++;
    }
  }

}

/*------------------------------------------------------------------------------

    Function: h264bsdGetNeighbourMb

        Functional description:
            Get pointer to neighbour macroblock.

        Inputs:
            p_mb         pointer to macroblock structure of the macroblock
                        whose neighbour is wanted
            neighbour   indicates which neighbour is wanted

        Outputs:
            none

        Returns:
            pointer to neighbour macroblock
            NULL if not available

------------------------------------------------------------------------------*/

mbStorage_t* h264bsdGetNeighbourMb(mbStorage_t *p_mb, neighbourMb_e neighbour) {

  /* Variables */


  /* Code */

  ASSERT((neighbour <= MB_CURR) || (neighbour == MB_NA));

  if (neighbour == MB_A)
    return(p_mb->mb_a);
  else if (neighbour == MB_B)
    return(p_mb->mb_b);
  else if (neighbour == MB_D)
    return(p_mb->mb_d);
  else if (neighbour == MB_CURR)
    return(p_mb);
  else if (neighbour == MB_C)
    return(p_mb->mb_c);
  else
    return(NULL);

}

/*------------------------------------------------------------------------------

    Function: h264bsdNeighbour4x4BlockA

        Functional description:
            Get left neighbour of the block. Function returns pointer to
            the table defined in the beginning of the file.

        Inputs:
            block_index  indicates the block whose neighbours are wanted

        Outputs:

        Returns:
            pointer to neighbour structure

------------------------------------------------------------------------------*/

const neighbour_t* h264bsdNeighbour4x4BlockA(u32 block_index) {

  /* Variables */

  /* Code */

  ASSERT(block_index < 24);

  return(N_A_4x4B+block_index);

}

/*------------------------------------------------------------------------------

    Function: h264bsdNeighbour4x4BlockB

        Functional description:
            Get above neighbour of the block. Function returns pointer to
            the table defined in the beginning of the file.

        Inputs:
            block_index  indicates the block whose neighbours are wanted

        Outputs:

        Returns:
            pointer to neighbour structure

------------------------------------------------------------------------------*/

const neighbour_t* h264bsdNeighbour4x4BlockB(u32 block_index) {

  /* Variables */

  /* Code */

  ASSERT(block_index < 24);

  return(N_B_4x4B+block_index);

}

/*------------------------------------------------------------------------------

    Function: h264bsdNeighbour4x4BlockC

        Functional description:
            Get above-right  neighbour of the block. Function returns pointer
            to the table defined in the beginning of the file.

        Inputs:
            block_index  indicates the block whose neighbours are wanted

        Outputs:

        Returns:
            pointer to neighbour structure

------------------------------------------------------------------------------*/
#if 0 /* not used */
const neighbour_t* h264bsdNeighbour4x4BlockC(u32 block_index) {

  /* Variables */

  /* Code */

  ASSERT(block_index < 24);

  return(N_C_4x4B+block_index);

}
#endif

/*------------------------------------------------------------------------------

    Function: h264bsdNeighbour4x4BlockD

        Functional description:
            Get above-left neighbour of the block. Function returns pointer to
            the table defined in the beginning of the file.

        Inputs:
            block_index  indicates the block whose neighbours are wanted

        Outputs:

        Returns:
            pointer to neighbour structure

------------------------------------------------------------------------------*/

const neighbour_t* h264bsdNeighbour4x4BlockD(u32 block_index) {

  /* Variables */

  /* Code */

  ASSERT(block_index < 24);

  return(N_D_4x4B+block_index);

}

/*------------------------------------------------------------------------------

    Function: h264bsdIsNeighbourAvailable

        Functional description:
            Check if neighbour macroblock is available. Neighbour macroblock
            is considered available if it is within the picture and belongs
            to the same slice as the current macroblock.

        Inputs:
            p_mb         pointer to the current macroblock
            p_neighbour  pointer to the neighbour macroblock

        Outputs:
            none

        Returns:
            HANTRO_TRUE    neighbour is available
            HANTRO_FALSE   neighbour is not available

------------------------------------------------------------------------------*/

u32 h264bsdIsNeighbourAvailable(mbStorage_t *p_mb, mbStorage_t *p_neighbour) {

  /* Variables */

  /* Code */

  if ( (p_neighbour == NULL) || (p_mb->slice_id != p_neighbour->slice_id) )
    return(HANTRO_FALSE);
  else
    return(HANTRO_TRUE);

}
