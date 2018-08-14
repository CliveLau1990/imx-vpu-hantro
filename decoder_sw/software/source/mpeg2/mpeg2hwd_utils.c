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

#include "mpeg2hwd_utils.h"
#include "mpeg2hwd_debug.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/
enum {
  SV_MARKER_MASK = 0x1FFFF,
  SV_END_MASK = 0x3FFFFF,
  SECOND_BYTE_ZERO_MASK = 0x00FF0000
};

/* to check first 23 bits */
#define START_CODE_MASK      0xFFFFFE00
/* to check first 16 bits */
#define RESYNC_MASK          0xFFFF0000

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.6  Function name: mpeg2_strm_dec_next_start_code

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_next_start_code(DecContainer * dec_container) {
  u32 tmp;
  u8 *p_strm;
  i32 bits;

  ASSERT(dec_container);

  if((dec_container->StrmDesc.bit_pos_in_word & 0x7)) {
    tmp = mpeg2_strm_dec_flush_bits(dec_container,
                                    8 - (dec_container->StrmDesc.bit_pos_in_word & 0x7));
    if(tmp != HANTRO_OK)
      return END_OF_STREAM;
  }

  p_strm = dec_container->StrmDesc.strm_curr_pos + 4;

  /* number of bits left in the buffer */
  bits = (i32) dec_container->StrmDesc.strm_buff_size * 8 -
         (i32) dec_container->StrmDesc.strm_buff_read_bits;

  if (bits == 0)
    return END_OF_STREAM;

  tmp = mpeg2_strm_dec_show_bits32(dec_container);

  do {
    if((tmp >> 8) == 0x1) {
      if (bits < 32) { /* end of stream */
        break;
      } else {
        dec_container->StrmDesc.strm_buff_read_bits =
          dec_container->StrmDesc.strm_buff_size * 8 - bits + 32;
        dec_container->StrmDesc.strm_curr_pos =
          dec_container->StrmDesc.p_strm_buff_start +
          dec_container->StrmDesc.strm_buff_read_bits/8;

        return (tmp & 0xFF);
      }
    }
    if (bits >= 40)
      tmp = (tmp << 8) | *p_strm++;
    bits -= 8;
  } while(bits >= 32);

  /* end of stream */
  dec_container->StrmDesc.strm_curr_pos =
    dec_container->StrmDesc.p_strm_buff_start +
    dec_container->StrmDesc.strm_buff_size;
  dec_container->StrmDesc.strm_buff_read_bits =
    dec_container->StrmDesc.strm_buff_size*8;

  return (END_OF_STREAM);

}

/*------------------------------------------------------------------------------

   5.8  Function name: mpeg2_strm_dec_num_bits

        Purpose: computes number of bits needed to represent value given as
        argument

        Input:
            u32 value [0,2^32)

        Output:
            Number of bits needed to represent input value

------------------------------------------------------------------------------*/

u32 mpeg2_strm_dec_num_bits(u32 value) {

  u32 num_bits = 0;

  while(value) {
    value >>= 1;
    num_bits++;
  }

  if(!num_bits) {
    num_bits = 1;
  }

  return (num_bits);

}

/*------------------------------------------------------------------------------

   5.10  Function name: mpeg2_strm_dec_check_stuffing

        Purpose: checks mpeg-2 stuffing from input stream

        Input:
            Pointer to decContainer_t structure
                -uses and updates StrmDesc

        Output:
            HANTRO_OK if there is stuffing at current stream position
            HANTRO_NOK otherwise

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_check_stuffing(DecContainer * dec_container) {
  ASSERT(dec_container);
  ASSERT(dec_container->StrmDesc.bit_pos_in_word < 8);

#ifdef HANTRO_PEDANTIC_MODE
  u32 length = (8 - dec_container->StrmDesc.bit_pos_in_word) & 0x3;

  if((mpeg2_strm_dec_show_bits32(dec_container) >> (32 - length)) == 0)
    return (HANTRO_OK);
  else
    return (HANTRO_NOK);
#else
  UNUSED(dec_container);
  return (HANTRO_OK);
#endif

}

/*------------------------------------------------------------------------------

    Function: mpeg2_strm_dec_get_bits

        Functional description:
            Read and remove bits from the stream buffer.

        Input:
            pStrmData   pointer to stream data structure
            num_bits     number of bits to read

        Output:
            none

        Returns:
            bits read from stream
            END_OF_STREAM if not enough bits left

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_get_bits(DecContainer * dec_container, u32 num_bits) {

  u32 out;

  ASSERT(dec_container);
  ASSERT(num_bits < 32);

  out = mpeg2_strm_dec_show_bits32(dec_container) >> (32 - num_bits);

  if(mpeg2_strm_dec_flush_bits(dec_container, num_bits) == HANTRO_OK) {
    return (out);
  } else {
    return (END_OF_STREAM);
  }
}

/*------------------------------------------------------------------------------

    Function: mmpeg2StrmDec_ShowBits32

        Functional description:
            Read 32 bits from the stream buffer. Buffer is left as it is, i.e.
            no bits are removed. First bit read from the stream is the MSB of
            the return value. If there is not enough bits in the buffer ->
            bits beyong the end of the stream are set to '0' in the return
            value.

        Input:
            pStrmData   pointer to stream data structure

        Output:
            none

        Returns:
            bits read from stream

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_show_bits32(DecContainer * dec_container) {

  i32 bits, shift;
  u32 out;
  u8 *p_strm;

  ASSERT(dec_container);
  ASSERT(dec_container->StrmDesc.strm_curr_pos);
  ASSERT(dec_container->StrmDesc.bit_pos_in_word < 8);
  ASSERT(dec_container->StrmDesc.bit_pos_in_word ==
         (dec_container->StrmDesc.strm_buff_read_bits & 0x7));

  p_strm = dec_container->StrmDesc.strm_curr_pos;

  /* number of bits left in the buffer */
  bits =
    (i32) dec_container->StrmDesc.strm_buff_size * 8 -
    (i32) dec_container->StrmDesc.strm_buff_read_bits;

  /* at least 32-bits in the buffer */
  if(bits >= 32) {
    u32 bit_pos_in_word = dec_container->StrmDesc.bit_pos_in_word;

    out = ((u32) p_strm[3]) | ((u32) p_strm[2] << 8) |
          ((u32) p_strm[1] << 16) | ((u32) p_strm[0] << 24);

    if(bit_pos_in_word) {
      out <<= bit_pos_in_word;
      out |= (u32) p_strm[4] >> (8 - bit_pos_in_word);
    }
    return (out);
  }
  /* at least one bit in the buffer */
  else if(bits > 0) {
    shift = (i32) (24 + dec_container->StrmDesc.bit_pos_in_word);
    out = (u32) (*p_strm++) << shift;
    bits -= (i32) (8 - dec_container->StrmDesc.bit_pos_in_word);
    while(bits > 0) {
      shift -= 8;
      out |= (u32) (*p_strm++) << shift;
      bits -= 8;
    }
    return (out);
  } else
    return (0);

}

/*------------------------------------------------------------------------------

    Function: mpeg2_strm_dec_flush_bits

        Functional description:
            Remove bits from the stream buffer

        Input:
            pStrmData       pointer to stream data structure
            num_bits         number of bits to remove

        Output:
            none

        Returns:
            HANTRO_OK       success
            END_OF_STREAM   not enough bits left

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_flush_bits(DecContainer * dec_container, u32 num_bits) {

  ASSERT(dec_container);
  ASSERT(dec_container->StrmDesc.p_strm_buff_start);
  ASSERT(dec_container->StrmDesc.strm_curr_pos);
  ASSERT(dec_container->StrmDesc.bit_pos_in_word < 8);
  ASSERT(dec_container->StrmDesc.bit_pos_in_word ==
         (dec_container->StrmDesc.strm_buff_read_bits & 0x7));

  dec_container->StrmDesc.strm_buff_read_bits += num_bits;
  dec_container->StrmDesc.bit_pos_in_word =
    dec_container->StrmDesc.strm_buff_read_bits & 0x7;
  if((dec_container->StrmDesc.strm_buff_read_bits) <=
      (8 * dec_container->StrmDesc.strm_buff_size)) {
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmDesc.p_strm_buff_start +
      (dec_container->StrmDesc.strm_buff_read_bits >> 3);
    return (HANTRO_OK);
  } else {
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmDesc.p_strm_buff_start +
      dec_container->StrmDesc.strm_buff_size;
    return (END_OF_STREAM);
  }
}

/*------------------------------------------------------------------------------

   5.2  Function name: mpeg2_strm_dec_show_bits

        Purpose: read bits from mpeg-2 input stream. Bits are located right
        aligned in the 32-bit output word. In case stream ends,
        function fills the word with zeros. For example, num_bits = 18 and
        there are 7 bits left in the stream buffer -> return
            00000000000000xxxxxxx00000000000,
        where 'x's represent actual bits read from buffer.

        Input:
            Pointer to DecContainer structure
                -uses but does not update StrmDesc
            Number of bits to read [0,32]

        Output:
            u32 containing bits read from stream

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_show_bits(DecContainer * dec_container, u32 num_bits) {

  u32 i;
  i32 bits, shift;
  u32 out;
  u8 *pstrm = dec_container->StrmDesc.strm_curr_pos;

  ASSERT(num_bits <= 32);

  /* bits left in the buffer */
  bits = (i32) dec_container->StrmDesc.strm_buff_size * 8 -
         (i32) dec_container->StrmDesc.strm_buff_read_bits;

  if(!num_bits || !bits) {
    return (0);
  }

  /* at least 32-bits in the buffer -> get 32 bits and drop extra bits out */
  if(bits >= 32) {
    out = ((u32) pstrm[0] << 24) | ((u32) pstrm[1] << 16) |
          ((u32) pstrm[2] << 8) | ((u32) pstrm[3]);
    if(dec_container->StrmDesc.bit_pos_in_word) {
      out <<= dec_container->StrmDesc.bit_pos_in_word;
      out |= (u32) pstrm[4] >> (8 - dec_container->StrmDesc.bit_pos_in_word);
    }
  } else {
    shift = 24 + dec_container->StrmDesc.bit_pos_in_word;
    out = (u32) pstrm[0] << shift;
    bits -= 8 - dec_container->StrmDesc.bit_pos_in_word;
    i = 1;
    while(bits > 0) {
      shift -= 8;
      out |= (u32) pstrm[i] << shift;
      bits -= 8;
      i++;
    }
  }

  return (out >> (32 - num_bits));

}

/*------------------------------------------------------------------------------

   5.3  Function name: mpeg2_strm_dec_show_bits_aligned

        Purpose: read bits from mpeg-2 input stream at byte aligned position
        given as parameter (offset from current stream position).

        Input:
            Pointer to DecContainer structure
                -uses but does not update StrmDesc
            Number of bits to read [1,32]
            Byte offset from current stream position

        Output:
            u32 containing bits read from stream

------------------------------------------------------------------------------*/

u32 mpeg2_strm_dec_show_bits_aligned(DecContainer * dec_container, u32 num_bits,
                                     u32 byte_offset) {

  u32 i;
  u32 out;
  u32 bytes, shift;
  u8 *pstrm = dec_container->StrmDesc.strm_curr_pos + byte_offset;

  ASSERT(num_bits);
  ASSERT(num_bits <= 32);
  out = 0;

  /* at least four bytes available starting byte_offset bytes ahead */
  if((dec_container->StrmDesc.strm_buff_size >= (4 + byte_offset)) &&
      ((dec_container->StrmDesc.strm_buff_read_bits >> 3) <=
       (dec_container->StrmDesc.strm_buff_size - byte_offset - 4))) {
    out = ((u32) pstrm[0] << 24) | ((u32) pstrm[1] << 16) |
          ((u32) pstrm[2] << 8) | ((u32) pstrm[3]);
    out >>= (32 - num_bits);
  } else {
    bytes = dec_container->StrmDesc.strm_buff_size -
            (dec_container->StrmDesc.strm_buff_read_bits >> 3);
    if(bytes > byte_offset) {
      bytes -= byte_offset;
    } else {
      bytes = 0;
    }

    shift = 24;
    i = 0;
    while(bytes) {
      out |= (u32) pstrm[i] << shift;
      i++;
      shift -= 8;
      bytes--;
    }

    out >>= (32 - num_bits);
  }

  return (out);

}
