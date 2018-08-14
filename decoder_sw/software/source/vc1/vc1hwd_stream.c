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

#include "vc1hwd_util.h"
#include "vc1hwd_stream.h"

/*------------------------------------------------------------------------------
    External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

static u32 SwShowBits32(strmData_t *p_strm_data);
static u32 SwFlushBits(strmData_t *p_strm_data, u32 num_bits);
static u32 SwFlushBitsAdv(strmData_t *p_strm_data, u32 num_bits);

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function: vc1hwdGetBits

        Functional description:
            Read and remove bits from the stream buffer.

        Input:
            p_strm_data   pointer to stream data structure
            num_bits     number of bits to read

        Output:
            none

        Returns:
            bits read from stream
            END_OF_STREAM if not enough bits left

------------------------------------------------------------------------------*/

u32 vc1hwdGetBits(strmData_t *p_strm_data, u32 num_bits) {

  u32 out;

  ASSERT(p_strm_data);

  if (p_strm_data->remove_emul_prev_bytes)
    out = vc1hwdShowBits( p_strm_data, num_bits );
  else
    out = (SwShowBits32(p_strm_data) >> (32 - num_bits));

  if (vc1hwdFlushBits(p_strm_data, num_bits) == HANTRO_OK) {
    return(out);
  } else {
    return(END_OF_STREAM);
  }
}

/*------------------------------------------------------------------------------

    Function: vc1hwdShowBits

        Functional description:
            Read bits from the stream buffer. Buffer is left as it is, i.e.
            no bits are removed. First bit read from the stream is the MSB of
            the return value. If there is not enough bits in the buffer ->
            bits beyong the end of the stream are set to '0' in the return
            value.

        Input:
            p_strm_data   pointer to stream data structure

        Output:
            none

        Returns:
            bits read from stream

------------------------------------------------------------------------------*/
u32 vc1hwdShowBits(strmData_t *p_strm_data, u32 num_bits ) {

  i32 bits;
  u32 out, out_bits;
  u32 tmp_read_bits;
  u8 *p_strm;

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->strm_curr_pos);
  ASSERT(p_strm_data->bit_pos_in_word < 8);
  ASSERT(p_strm_data->bit_pos_in_word ==
         (p_strm_data->strm_buff_read_bits & 0x7));
  ASSERT(num_bits <= 32);

  p_strm = p_strm_data->strm_curr_pos;

  /* bits left in the buffer */
  bits = (i32)p_strm_data->strm_buff_size*8 - (i32)p_strm_data->strm_buff_read_bits;

  if (!num_bits || !bits) {
    return(0);
  }

  out = out_bits = 0;
  tmp_read_bits = p_strm_data->strm_buff_read_bits;

  if (p_strm_data->bit_pos_in_word) {
    out = p_strm[0]<<(24+p_strm_data->bit_pos_in_word);
    p_strm++;
    out_bits = 8-p_strm_data->bit_pos_in_word;
    bits -= out_bits;
    tmp_read_bits += out_bits;
  }

  while (bits && out_bits < num_bits) {
    /* check emulation prevention byte */
    if (p_strm_data->remove_emul_prev_bytes && tmp_read_bits >= 16 &&
        p_strm[-2] == 0x0 && p_strm[-1] == 0x0 && p_strm[0] == 0x3) {
      p_strm++;
      tmp_read_bits += 8;
      bits -= 8;
      /* emulation prevention byte shall not be the last byte of the
       * stream */
      if (bits <= 0)
        break;
    }

    if (out_bits <= 24)
      out |= (u32)(*p_strm++) << (24 - out_bits);
    else
      out |= (u32)(*p_strm++) >> (out_bits-24);
    out_bits += 8;

    tmp_read_bits += 8;
    bits -= 8;
  }

  return(out>>(32-num_bits));

}

/*------------------------------------------------------------------------------

    Function: SwShowBits32

        Functional description:
            Read 32 bits from the stream buffer when emulation prevention bytes
            are not present in the bitstream. Buffer is left as it is, i.e.
            no bits are removed. First bit read from the stream is the MSB of
            the return value. If there is not enough bits in the buffer ->
            bits beyong the end of the stream are set to '0' in the return
            value.

        Input:
            p_strm_data   pointer to stream data structure

        Output:
            none

        Returns:
            bits read from stream

------------------------------------------------------------------------------*/
u32 SwShowBits32(strmData_t *p_strm_data) {
  i32 bits, shift;
  u32 out;
  u8 *p_strm;

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->strm_curr_pos);
  ASSERT(p_strm_data->bit_pos_in_word < 8);
  ASSERT(p_strm_data->bit_pos_in_word ==
         (p_strm_data->strm_buff_read_bits & 0x7));

  p_strm = p_strm_data->strm_curr_pos;

  /* number of bits left in the buffer */
  bits = (i32)p_strm_data->strm_buff_size*8 - (i32)p_strm_data->strm_buff_read_bits;

  /* at least 32-bits in the buffer */
  if (bits >= 32) {
    u32 bit_pos_in_word = p_strm_data->bit_pos_in_word;

    out = ((u32)p_strm[3]) | ((u32)p_strm[2] <<  8) |
          ((u32)p_strm[1] << 16) | ((u32)p_strm[0] << 24);

    if (bit_pos_in_word) {
      out <<= bit_pos_in_word;
      out |= (u32)p_strm[4]>>(8-bit_pos_in_word);
    }
    return (out);
  }
  /* at least one bit in the buffer */
  else if (bits > 0) {
    shift = (i32)(24 + p_strm_data->bit_pos_in_word);
    out = (u32)(*p_strm++) << shift;
    bits -= (i32)(8 - p_strm_data->bit_pos_in_word);
    while (bits > 0) {
      shift -= 8;
      out |= (u32)(*p_strm++) << shift;
      bits -= 8;
    }
    return (out);
  } else
    return (0);
}


/*------------------------------------------------------------------------------

    Function: vc1hwdFlushBits

        Functional description:
            Remove bits from the stream buffer

        Input:
            p_strm_data       pointer to stream data structure
            num_bits         number of bits to remove

        Output:
            none

        Returns:
            HANTRO_OK       success
            END_OF_STREAM   not enough bits left

------------------------------------------------------------------------------*/
u32 vc1hwdFlushBits(strmData_t *p_strm_data, u32 num_bits) {
  u32 rv;

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->p_strm_buff_start);
  ASSERT(p_strm_data->strm_curr_pos);
  ASSERT(p_strm_data->bit_pos_in_word < 8);
  ASSERT(p_strm_data->bit_pos_in_word == (p_strm_data->strm_buff_read_bits & 0x7));

  if (p_strm_data->remove_emul_prev_bytes)
    rv = SwFlushBitsAdv( p_strm_data, num_bits );
  else
    rv = SwFlushBits(p_strm_data, num_bits);

  return rv;
}


/*------------------------------------------------------------------------------

    Function: SwFlushBits

        Functional description:
            Remove bits from the stream buffer for streams that are not
            containing emulation prevention bytes

        Input:
            p_strm_data       pointer to stream data structure
            num_bits         number of bits to remove

        Output:
            none

        Returns:
            HANTRO_OK       success
            END_OF_STREAM   not enough bits left

------------------------------------------------------------------------------*/
u32 SwFlushBits(strmData_t *p_strm_data, u32 num_bits) {
  ASSERT(p_strm_data);
  ASSERT(p_strm_data->p_strm_buff_start);
  ASSERT(p_strm_data->strm_curr_pos);
  ASSERT(p_strm_data->bit_pos_in_word < 8);
  ASSERT(p_strm_data->bit_pos_in_word == (p_strm_data->strm_buff_read_bits & 0x7));

  p_strm_data->strm_buff_read_bits += num_bits;
  p_strm_data->bit_pos_in_word = p_strm_data->strm_buff_read_bits & 0x7;
  if ( (p_strm_data->strm_buff_read_bits ) <= (8*p_strm_data->strm_buff_size) ) {
    p_strm_data->strm_curr_pos = p_strm_data->p_strm_buff_start +
                                 (p_strm_data->strm_buff_read_bits >> 3);
    return(HANTRO_OK);
  } else {
    p_strm_data->strm_exhausted = HANTRO_TRUE;
    return(END_OF_STREAM);
  }
}

/*------------------------------------------------------------------------------

    Function: SwFlushBitsAdv

        Functional description:
            Remove bits from the stream buffer for streams with emulation
            prevention bytes.

        Input:
            p_strm_data       pointer to stream data structure
            num_bits         number of bits to remove

        Output:
            none

        Returns:
            HANTRO_OK       success
            END_OF_STREAM   not enough bits left

------------------------------------------------------------------------------*/
u32 SwFlushBitsAdv(strmData_t *p_strm_data, u32 num_bits) {

  u32 bytes_left;
  u8 *p_strm;

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->p_strm_buff_start);
  ASSERT(p_strm_data->strm_curr_pos);
  ASSERT(p_strm_data->bit_pos_in_word < 8);
  ASSERT(p_strm_data->bit_pos_in_word == (p_strm_data->strm_buff_read_bits & 0x7));

  if ( (p_strm_data->strm_buff_read_bits + num_bits) > (8*p_strm_data->strm_buff_size) ) {
    p_strm_data->strm_buff_read_bits = 8 * p_strm_data->strm_buff_size;
    p_strm_data->bit_pos_in_word = 0;
    p_strm_data->strm_curr_pos =
      p_strm_data->p_strm_buff_start + p_strm_data->strm_buff_size;
    p_strm_data->strm_exhausted = HANTRO_TRUE;
    return(END_OF_STREAM);
  } else {
    bytes_left = (8*p_strm_data->strm_buff_size-p_strm_data->strm_buff_read_bits)/8;
    p_strm = p_strm_data->strm_curr_pos;
    if (p_strm_data->bit_pos_in_word) {
      if (num_bits < 8-p_strm_data->bit_pos_in_word) {
        p_strm_data->strm_buff_read_bits += num_bits;
        p_strm_data->bit_pos_in_word += num_bits;
        return(HANTRO_OK);
      }
      num_bits -= 8-p_strm_data->bit_pos_in_word;
      p_strm_data->strm_buff_read_bits += 8-p_strm_data->bit_pos_in_word;
      p_strm_data->bit_pos_in_word = 0;
      p_strm++;
      if (p_strm_data->strm_buff_read_bits >= 16 && bytes_left &&
          p_strm[-2] == 0x0 && p_strm[-1] == 0x0 && p_strm[0] == 0x3) {
        p_strm++;
        p_strm_data->strm_buff_read_bits += 8;
        p_strm_data->slice_piclayer_emulation_bits += 8;
        bytes_left--;
      }
    }

    while (num_bits >= 8 && bytes_left) {
      p_strm++;
      p_strm_data->strm_buff_read_bits += 8;
      bytes_left--;
      /* check emulation prevention byte */
      if (p_strm_data->strm_buff_read_bits >= 16 && bytes_left &&
          p_strm[-2] == 0x0 && p_strm[-1] == 0x0 && p_strm[0] == 0x3) {
        p_strm++;
        p_strm_data->strm_buff_read_bits += 8;
        p_strm_data->slice_piclayer_emulation_bits += 8;
        bytes_left--;
      }

      num_bits -= 8;
    }

    if (num_bits && bytes_left) {
      p_strm_data->strm_buff_read_bits += num_bits;
      p_strm_data->bit_pos_in_word = num_bits;
      num_bits = 0;
    }
    p_strm_data->strm_curr_pos = p_strm;

    if (num_bits) {
      p_strm_data->strm_exhausted = HANTRO_TRUE;
      return(END_OF_STREAM);
    } else
      return(HANTRO_OK);
  }

}

/*------------------------------------------------------------------------------

    Function: vc1hwdIsExhausted

        Functional description:
            Check if attempted to read more bits from stream than there are
            available.

        Inputs:
            p_strm_data   pointer to stream data structure

        Outputs:
            None

        Returns:
            TRUE        stream is exhausted.
            FALSE       stream is not exhausted.


------------------------------------------------------------------------------*/

u32 vc1hwdIsExhausted(const strmData_t * const p_strm_data) {
  return p_strm_data->strm_exhausted;
}

