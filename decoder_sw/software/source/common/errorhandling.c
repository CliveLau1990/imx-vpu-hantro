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

#include "errorhandling.h"
#include "dwl.h"
#include "deccfg.h"

#define MAGIC_WORD_LENGTH (8)

static const u8 magic_word[MAGIC_WORD_LENGTH] = "Rosebud\0";

#define NUM_OFFSETS 6

static const u32 row_offsets[] = {1, 2, 4, 8, 12, 16};

static u32 GetMbOffset(u32 mb_num, u32 vop_width, u32 vop_height);

u32 GetMbOffset(u32 mb_num, u32 vop_width, u32 vop_height) {
  u32 mb_row, mb_col;
  u32 offset;
  UNUSED(vop_height);

  mb_row = mb_num / vop_width;
  mb_col = mb_num % vop_width;
  offset = mb_row * 16 * 16 * vop_width + mb_col * 16;

  return offset;
}

/* Copy num_rows bottom mb rows from ref_pic to dec_out. */
void CopyRows(u32 num_rows, u8 *dec_out, const u8 *ref_pic, u32 vop_width,
              u32 vop_height) {

  u32 pix_width;
  u32 offset;
  u32 luma_size;
  u8 *src;
  u8 *dst;

  pix_width = 16 * vop_width;

  offset = (vop_height - num_rows) * 16 * pix_width;
  luma_size = 256 * vop_width * vop_height;

  dst = dec_out;
  src = (u8 *)ref_pic;

  dst += offset;
  src += offset;

  if (ref_pic)
    DWLmemcpy(dst, src, num_rows * 16 * pix_width);
  else
    DWLmemset(dst, 0, num_rows * 16 * pix_width);

  /* Chroma data */
  offset = (vop_height - num_rows) * 8 * pix_width;

  dst = dec_out;
  src = (u8 *)ref_pic;

  dst += luma_size;
  src += luma_size;
  dst += offset;
  src += offset;

  if (ref_pic)
    DWLmemcpy(dst, src, num_rows * 8 * pix_width);
  else
    DWLmemset(dst, 128, num_rows * 8 * pix_width);
}

void PreparePartialFreeze(u8 *dec_out, u32 vop_width, u32 vop_height) {

  u32 i, j;
  u8 *base;

  for (i = 0; i < NUM_OFFSETS && row_offsets[i] < vop_height / 4 &&
       row_offsets[i] <= DEC_X170_MAX_EC_COPY_ROWS;
       i++) {
    base = dec_out + GetMbOffset(vop_width * (vop_height - row_offsets[i]),
                                 vop_width, vop_height);

    for (j = 0; j < MAGIC_WORD_LENGTH; ++j) base[j] = magic_word[j];
  }
}

u32 ProcessPartialFreeze(u8 *dec_out, const u8 *ref_pic, u32 vop_width,
                         u32 vop_height, u32 copy) {

  u32 i, j;
  u8 *base;
  //u32 num_mbs;
  u32 match = HANTRO_TRUE;

  //num_mbs = vop_width * vop_height;

  for (i = 0; i < NUM_OFFSETS && row_offsets[i] < vop_height / 4 &&
       row_offsets[i] <= DEC_X170_MAX_EC_COPY_ROWS;
       i++) {
    base = dec_out + GetMbOffset(vop_width * (vop_height - row_offsets[i]),
                                 vop_width, vop_height);

    for (j = 0; j < MAGIC_WORD_LENGTH && match; ++j)
      if (base[j] != magic_word[j]) match = HANTRO_FALSE;

    if (!match) {
      if (copy)
        CopyRows(row_offsets[i], dec_out, ref_pic, vop_width, vop_height);
      return HANTRO_TRUE;
    }
  }

  return HANTRO_FALSE;
}

u32  GetPartialFreezePos( u8 * dec_out, u32 vop_width, u32 vop_height) {

  u32 i, j;
  u8 * base;
  u32 pos = vop_width * vop_height;
  u8 decoding_pos_found = 0;

  for (i = 0; i < NUM_OFFSETS && row_offsets[i] < vop_height/4 &&
       row_offsets[i] <= DEC_X170_MAX_EC_COPY_ROWS; i++) {
    base = dec_out + GetMbOffset(vop_width * (vop_height - row_offsets[i]),
                                     vop_width, vop_height );

    for( j = 0 ; j < MAGIC_WORD_LENGTH; ++j )
      if( base[j] != magic_word[j] ) {
        decoding_pos_found = 1;
        break;
      }

    if (decoding_pos_found) {
      if( i != 0)
        pos = row_offsets[i-1] * vop_width;
      else
        pos = 0;

      break;
    }
  }

  return pos;
}
