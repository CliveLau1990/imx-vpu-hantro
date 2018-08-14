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

#include "rv_headers.h"
#include "rv_utils.h"
#include "rv_strm.h"
#include "rv_cfg.h"
#include "rv_debug.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

static const u32 pic_width[8] = {160, 176, 240, 320, 352, 640, 704, 0};
static const u32 pic_height[8] = {120, 132, 144, 240, 288, 480, 0, 0};
static const u32 pic_height2[5] = {180, 360, 576, 0};
static const u32 max_pic_size[] = {48, 99, 396, 1584, 6336, 9216};

#define IS_INTRA_PIC(pic_type) (pic_type < RV_P_PIC)

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.1  Function name:
                GetPictureSize

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 GetPictureSize(DecContainer * dec_container, DecHdrs *p_hdrs) {

  u32 tmp;
  u32 bits;
  u32 w, h;

  tmp = rv_GetBits(dec_container, 3);
  if (tmp == END_OF_STREAM)
    return(tmp);
  w = pic_width[tmp];
  if (!w) {
    do {
      tmp = rv_GetBits(dec_container, 8);
      w += tmp << 2;
    } while (tmp == 255);
  }

  bits = dec_container->StrmDesc.strm_buff_read_bits;

  tmp = rv_GetBits(dec_container, 3);
  if (tmp == END_OF_STREAM)
    return(tmp);
  h = pic_height[tmp];
  if (!h) {
    tmp = (tmp<<1) | rv_GetBits(dec_container, 1);
    tmp &= 0x3;
    h = pic_height2[tmp];
    if (!h) {
      do {
        tmp = rv_GetBits(dec_container, 8);
        h += tmp << 2;
      } while (tmp == 255);
      if (tmp == END_OF_STREAM)
        return(tmp);
    }
  }

  p_hdrs->horizontal_size = w;
  p_hdrs->vertical_size = h;

  dec_container->StrmStorage.frame_size_bits =
    dec_container->StrmDesc.strm_buff_read_bits - bits;

  return HANTRO_OK;

}

static u32 MbNumLen(u32 num_mbs) {

  u32 tmp;

  if( num_mbs <= 48 )          tmp = 6;
  else if( num_mbs <= 99   )   tmp = 7;
  else if( num_mbs <= 396  )   tmp = 9;
  else if( num_mbs <= 1584 )   tmp = 11;
  else if( num_mbs <= 6336 )   tmp = 13;
  else                        tmp = 14;
  return tmp;

}

/*------------------------------------------------------------------------------

   5.1  Function name:
                rv_DecodeSliceHeader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/

#ifdef RV_RAW_STREAM_SUPPORT

#define PACK_LENGTH_AND_INFO(length, info) (((length) << 4) | (info))

static const u8 rv_vlc_table[256] = {
  PACK_LENGTH_AND_INFO(8,  0),   /* 00000000 */
  PACK_LENGTH_AND_INFO(8,  1),   /* 00000001 */
  PACK_LENGTH_AND_INFO(7,  0),   /* 00000010 */
  PACK_LENGTH_AND_INFO(7,  0),   /* 00000011 */
  PACK_LENGTH_AND_INFO(8,  2),   /* 00000100 */
  PACK_LENGTH_AND_INFO(8,  3),   /* 00000101 */
  PACK_LENGTH_AND_INFO(7,  1),   /* 00000110 */
  PACK_LENGTH_AND_INFO(7,  1),   /* 00000111 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001000 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001001 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001010 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001011 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001100 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001101 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001110 */
  PACK_LENGTH_AND_INFO(5,  0),   /* 00001111 */
  PACK_LENGTH_AND_INFO(8,  4),   /* 00010000 */
  PACK_LENGTH_AND_INFO(8,  5),   /* 00010001 */
  PACK_LENGTH_AND_INFO(7,  2),   /* 00010010 */
  PACK_LENGTH_AND_INFO(7,  2),   /* 00010011 */
  PACK_LENGTH_AND_INFO(8,  6),   /* 00010100 */
  PACK_LENGTH_AND_INFO(8,  7),   /* 00010101 */
  PACK_LENGTH_AND_INFO(7,  3),   /* 00010110 */
  PACK_LENGTH_AND_INFO(7,  3),   /* 00010111 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011000 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011001 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011010 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011011 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011100 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011101 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011110 */
  PACK_LENGTH_AND_INFO(5,  1),   /* 00011111 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100000 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100001 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100010 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100011 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100100 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100101 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100110 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00100111 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101000 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101001 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101010 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101011 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101100 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101101 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101110 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00101111 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110000 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110001 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110010 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110011 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110100 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110101 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110110 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00110111 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111000 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111001 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111010 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111011 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111100 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111101 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111110 */
  PACK_LENGTH_AND_INFO(3,  0),   /* 00111111 */
  PACK_LENGTH_AND_INFO(8,  8),   /* 01000000 */
  PACK_LENGTH_AND_INFO(8,  9),   /* 01000001 */
  PACK_LENGTH_AND_INFO(7,  4),   /* 01000010 */
  PACK_LENGTH_AND_INFO(7,  4),   /* 01000011 */
  PACK_LENGTH_AND_INFO(8, 10),   /* 01000100 */
  PACK_LENGTH_AND_INFO(8, 11),   /* 01000101 */
  PACK_LENGTH_AND_INFO(7,  5),   /* 01000110 */
  PACK_LENGTH_AND_INFO(7,  5),   /* 01000111 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001000 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001001 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001010 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001011 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001100 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001101 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001110 */
  PACK_LENGTH_AND_INFO(5,  2),   /* 01001111 */
  PACK_LENGTH_AND_INFO(8, 12),   /* 01010000 */
  PACK_LENGTH_AND_INFO(8, 13),   /* 01010001 */
  PACK_LENGTH_AND_INFO(7,  6),   /* 01010010 */
  PACK_LENGTH_AND_INFO(7,  6),   /* 01010011 */
  PACK_LENGTH_AND_INFO(8, 14),   /* 01010100 */
  PACK_LENGTH_AND_INFO(8, 15),   /* 01010101 */
  PACK_LENGTH_AND_INFO(7,  7),   /* 01010110 */
  PACK_LENGTH_AND_INFO(7,  7),   /* 01010111 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011000 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011001 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011010 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011011 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011100 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011101 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011110 */
  PACK_LENGTH_AND_INFO(5,  3),   /* 01011111 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100000 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100001 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100010 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100011 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100100 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100101 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100110 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01100111 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101000 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101001 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101010 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101011 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101100 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101101 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101110 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01101111 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110000 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110001 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110010 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110011 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110100 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110101 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110110 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01110111 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111000 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111001 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111010 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111011 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111100 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111101 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111110 */
  PACK_LENGTH_AND_INFO(3,  1),   /* 01111111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10000111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10001111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10010111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10011111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10100111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10101111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10110111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 10111111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11000111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11001111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11010111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11011111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11100111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11101111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110110 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11110111 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11111000 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11111001 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11111010 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11111011 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11111100 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11111101 */
  PACK_LENGTH_AND_INFO(1,  0),   /* 11111110 */
  PACK_LENGTH_AND_INFO(1,  0)    /* 11111111 */
};

static u32 RvDecodeCode( DecContainer *p_d, u32 *code ) {

  /* Variables */

  u32 bits, len;
  u32 info;
  u32 symbol = 0;
  u32 chunk_len;

  /* Code */

  len = 0;

  do {
    bits = rv_ShowBits(p_d, 8);
    info = rv_vlc_table[bits];
    chunk_len = info>>4;
    symbol = (symbol << (chunk_len>>1)) | (info & 0xF);
    len += chunk_len;

    if (rv_FlushBits(p_d, chunk_len) == END_OF_STREAM)
      return END_OF_STREAM;
  } while (!(len&0x1));

  *code = symbol;

  return len;
}
#endif

u32 rv_DecodeSliceHeader(DecContainer * dec_container) {
  u32 i = 0, tmp;
  DecHdrs *p_hdrs;

  ASSERT(dec_container);

  RVDEC_DEBUG(("Decode Slice Header Start\n"));

  p_hdrs = dec_container->StrmStorage.strm_dec_ready == FALSE ?
           &dec_container->Hdrs : &dec_container->tmp_hdrs;

#ifdef RV_RAW_STREAM_SUPPORT
  if (dec_container->StrmStorage.raw_mode) {
    if (dec_container->StrmStorage.is_rv8) {
      if (rv_ShowBits(dec_container, 24) != 0x000001)
        return HANTRO_NOK;
      rv_FlushBits(dec_container, 24);
    } else {
      if (rv_ShowBits(dec_container, 32) != 0x55555555)
        return HANTRO_NOK;
      rv_FlushBits(dec_container, 32);
    }

    tmp = RvDecodeCode(dec_container, &i);
    if (tmp < 31 || (i & 1))
      return HANTRO_NOK;

    if (!((i>>1)&1)) {
      p_hdrs->horizontal_size = 176;
      p_hdrs->vertical_size = 144;
    } else {
      p_hdrs->horizontal_size = 0;
      p_hdrs->vertical_size = 0;
    }
    dec_container->FrameDesc.qp = (i >> 2) & 0x1F;
    p_hdrs->temporal_reference = (i >> 7) & 0xFF;

    /* picture type */
    tmp = RvDecodeCode(dec_container, &i);
    if (tmp == 1)
      dec_container->FrameDesc.pic_coding_type = RV_P_PIC;
    else if (tmp == 3 && i == 1)
      dec_container->FrameDesc.pic_coding_type = RV_I_PIC;
    else if (tmp == 5 && i == 0)
      dec_container->FrameDesc.pic_coding_type = RV_B_PIC;
    else
      return HANTRO_NOK;
    if (p_hdrs->horizontal_size == 0) {
      /* pixel_aspect_ratio */
      rv_FlushBits(dec_container, 4);
      tmp = rv_GetBits(dec_container, 9);
      p_hdrs->horizontal_size = (tmp + 1)  * 4;
      if (!rv_GetBits(dec_container, 1))
        return HANTRO_NOK;
      tmp = rv_GetBits(dec_container, 9);
      p_hdrs->vertical_size = tmp * 4;
    }

    if (!dec_container->StrmStorage.is_rv8)
      dec_container->FrameDesc.vlc_set = rv_GetBits(dec_container, 2);
    else
      dec_container->FrameDesc.vlc_set = 0;

    dec_container->FrameDesc.frame_width =
      (p_hdrs->horizontal_size + 15) >> 4;
    dec_container->FrameDesc.frame_height =
      (p_hdrs->vertical_size + 15) >> 4;
    dec_container->FrameDesc.total_mb_in_frame =
      (dec_container->FrameDesc.frame_width *
       dec_container->FrameDesc.frame_height);

  } else
#endif
    if (dec_container->StrmStorage.is_rv8) {
      /* bitstream version */
      tmp = rv_GetBits(dec_container, 3);

      tmp = dec_container->FrameDesc.pic_coding_type =
              rv_GetBits(dec_container, 2);
      if (dec_container->FrameDesc.pic_coding_type == RV_FI_PIC)
        dec_container->FrameDesc.pic_coding_type = RV_I_PIC;

      /* ecc */
      tmp = rv_GetBits(dec_container, 1);

      /* qp */
      tmp = dec_container->FrameDesc.qp =
              rv_GetBits(dec_container, 5);

      /* deblocking filter flag */
      tmp = rv_GetBits(dec_container, 1);

      tmp = p_hdrs->temporal_reference = rv_GetBits(dec_container, 13);

      /* frame size code */
      tmp = rv_GetBits(dec_container,
                       dec_container->StrmStorage.frame_code_length);

      p_hdrs->horizontal_size = dec_container->StrmStorage.frame_sizes[2*(i32)tmp];
      p_hdrs->vertical_size = dec_container->StrmStorage.frame_sizes[2*(i32)tmp+1];

      dec_container->FrameDesc.frame_width =
        (p_hdrs->horizontal_size + 15) >> 4;
      dec_container->FrameDesc.frame_height =
        (p_hdrs->vertical_size + 15) >> 4;
      dec_container->FrameDesc.total_mb_in_frame =
        (dec_container->FrameDesc.frame_width *
         dec_container->FrameDesc.frame_height);

      /* first mb in slice */
      tmp = rv_GetBits(dec_container,
                       MbNumLen(dec_container->FrameDesc.total_mb_in_frame));
      if (tmp == END_OF_STREAM)
        return tmp;

      /* rounding type */
      tmp = rv_GetBits(dec_container, 1);

    } else {

      /* TODO: shall be 0, check? */
      tmp = rv_GetBits(dec_container, 1);

      tmp = dec_container->FrameDesc.pic_coding_type =
              rv_GetBits(dec_container, 2);
      if (dec_container->FrameDesc.pic_coding_type == RV_FI_PIC)
        dec_container->FrameDesc.pic_coding_type = RV_I_PIC;

      /* qp */
      tmp = dec_container->FrameDesc.qp =
              rv_GetBits(dec_container, 5);

      /* TODO: shall be 0, check? */
      tmp = rv_GetBits(dec_container, 2);

      tmp = dec_container->FrameDesc.vlc_set =
              rv_GetBits(dec_container, 2);

      /* deblocking filter flag */
      tmp = rv_GetBits(dec_container, 1);

      tmp = p_hdrs->temporal_reference = rv_GetBits(dec_container, 13);

      /* picture size */
      if (IS_INTRA_PIC(dec_container->FrameDesc.pic_coding_type) ||
          rv_GetBits(dec_container, 1) == 0) {
        GetPictureSize(dec_container, p_hdrs);
      }

      dec_container->FrameDesc.frame_width =
        (p_hdrs->horizontal_size + 15) >> 4;
      dec_container->FrameDesc.frame_height =
        (p_hdrs->vertical_size + 15) >> 4;
      dec_container->FrameDesc.total_mb_in_frame =
        (dec_container->FrameDesc.frame_width *
         dec_container->FrameDesc.frame_height);

      /* check that we got valid picture size */
      if (!dec_container->FrameDesc.frame_width &&
          !dec_container->FrameDesc.frame_height)
        return HANTRO_NOK;

      /* first mb in slice */
      tmp = rv_GetBits(dec_container,
                       MbNumLen(dec_container->FrameDesc.total_mb_in_frame));
      if (tmp == END_OF_STREAM)
        return tmp;

      /* TODO: check that this slice starts from mb 0 */
    }

  /* check that picture size does not exceed maximum */
  if ( !dec_container->StrmStorage.raw_mode &&
       (p_hdrs->horizontal_size > dec_container->StrmStorage.max_frame_width ||
        p_hdrs->vertical_size   > dec_container->StrmStorage.max_frame_height) )
    return HANTRO_NOK;

  if(dec_container->StrmStorage.strm_dec_ready) {
    if( p_hdrs->horizontal_size != dec_container->Hdrs.horizontal_size ||
        p_hdrs->vertical_size != dec_container->Hdrs.vertical_size ) {
      /*dec_container->ApiStorage.firstHeaders = 1;*/
      /*dec_container->StrmStorage.strm_dec_ready = HANTRO_FALSE;*/

      /* Resolution change delayed */
      dec_container->StrmStorage.rpr_detected = 1;
      /* Get picture type from here to perform resampling */
      dec_container->StrmStorage.rpr_next_pic_type =
        dec_container->FrameDesc.pic_coding_type;
    }

    if (!dec_container->same_slice_header) {
      if (dec_container->FrameDesc.pic_coding_type != RV_B_PIC) {
        dec_container->StrmStorage.prev_tr = dec_container->StrmStorage.tr;
        dec_container->StrmStorage.tr = p_hdrs->temporal_reference;
      } else {
        i32 trb, trd;

        trb = p_hdrs->temporal_reference -
              dec_container->StrmStorage.prev_tr;
        trd = dec_container->StrmStorage.tr -
              dec_container->StrmStorage.prev_tr;

        if (trb < 0) trb += (1<<13);
        if (trd < 0) trd += (1<<13);

        dec_container->StrmStorage.trb = trb;

        /* current time stamp not between the fwd and bwd references */
        if (trb > trd)
          trb = trd/2;

        if (trd) {
          dec_container->StrmStorage.bwd_scale = (trb << 14) / trd;
          dec_container->StrmStorage.fwd_scale = ((trd-trb) << 14) / trd;
        } else {
          dec_container->StrmStorage.fwd_scale = 0;
          dec_container->StrmStorage.bwd_scale = 0;
        }
      }
    }
  }

  RVDEC_DEBUG(("Decode Slice Header Done\n"));

  return (HANTRO_OK);
}
