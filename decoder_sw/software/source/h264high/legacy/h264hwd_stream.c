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

#include "h264hwd_util.h"
#include "h264hwd_stream.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function: h264bsdGetBits

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

u32 h264bsdGetBits(strmData_t * p_strm_data, u32 num_bits) {

  u32 out;

  ASSERT(p_strm_data);
  ASSERT(num_bits < 32);

  out = h264bsdShowBits(p_strm_data, 32) >> (32 - num_bits);

  if(h264bsdFlushBits(p_strm_data, num_bits) == HANTRO_OK) {
    return (out);
  } else {
    return (END_OF_STREAM);
  }

}

/*------------------------------------------------------------------------------

   5.2  Function: HwShowBits

        Functional description:
          Read bits from input stream. Bits are located right
          aligned in the 32-bit output word. In case stream ends,
          function fills the word with zeros. For example, num_bits = 18 and
          there are 7 bits left in the stream buffer -> return
              00000000000000xxxxxxx00000000000,
          where 'x's represent actual bits read from buffer.

        Input:

        Output:

------------------------------------------------------------------------------*/

u32 h264bsdShowBits(strmData_t * p_strm_data, u32 num_bits) {

  i32 bits;
  u32 out, out_bits;
  u32 tmp_read_bits;
  const u8 *p_strm;

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->strm_curr_pos);
  ASSERT(p_strm_data->bit_pos_in_word < 8);
  ASSERT(p_strm_data->bit_pos_in_word == (p_strm_data->strm_buff_read_bits & 0x7));
  ASSERT(num_bits <= 32);

  p_strm = p_strm_data->strm_curr_pos;

  /* bits left in the buffer */
  bits =
    (i32) p_strm_data->strm_buff_size * 8 - (i32) p_strm_data->strm_buff_read_bits;

  if(/*!num_bits || */!bits) { /* for the moment is always called  with num_bits = 32 */
    return (0);
  }

  if(!p_strm_data->remove_emul3_byte) {

    out = out_bits = 0;
    tmp_read_bits = p_strm_data->strm_buff_read_bits;

    if(p_strm_data->bit_pos_in_word) {
      out = p_strm[0] << (24 + p_strm_data->bit_pos_in_word);
      p_strm++;
      out_bits = 8 - p_strm_data->bit_pos_in_word;
      bits -= out_bits;
      tmp_read_bits += out_bits;
    }

    while(bits && (out_bits < num_bits)) {
      /* check emulation prevention byte */
      if(tmp_read_bits >= 16 &&
          p_strm[-2] == 0x0 && p_strm[-1] == 0x0 && p_strm[0] == 0x3) {
        p_strm++;
        tmp_read_bits += 8;
        bits -= 8;
        /* emulation prevention byte shall not be the last byte of the
         * stream */
        if(bits <= 0)
          break;
      }

      tmp_read_bits += 8;

      if(out_bits <= 24)
        out |= (u32) (*p_strm++) << (24 - out_bits);
      else
        out |= (u32) (*p_strm++) >> (out_bits - 24);

      out_bits += 8;
      bits -= 8;
    }

    return (out >> (32 - num_bits));

  } else {
    u32 shift;

    /* at least 32-bits in the buffer */
    if(bits >= 32) {
      u32 bit_pos_in_word = p_strm_data->bit_pos_in_word;

      out = ((u32) p_strm[3]) | ((u32) p_strm[2] << 8) |
            ((u32) p_strm[1] << 16) | ((u32) p_strm[0] << 24);

      if(bit_pos_in_word) {
        out <<= bit_pos_in_word;
        out |= (u32) p_strm[4] >> (8 - bit_pos_in_word);
      }

      return (out >> (32 - num_bits));
    }
    /* at least one bit in the buffer */
    else if(bits > 0) {
      shift = (i32) (24 + p_strm_data->bit_pos_in_word);
      out = (u32) (*p_strm++) << shift;
      bits -= (i32) (8 - p_strm_data->bit_pos_in_word);
      while(bits > 0) {
        shift -= 8;
        out |= (u32) (*p_strm++) << shift;
        bits -= 8;
      }
      return (out >> (32 - num_bits));
    } else
      return (0);
  }
}

/*------------------------------------------------------------------------------

    Function: h264bsdFlushBits

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

u32 h264bsdFlushBits(strmData_t * p_strm_data, u32 num_bits) {

  u32 bytes_left;
  const u8 *p_strm;

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->p_strm_buff_start);
  ASSERT(p_strm_data->strm_curr_pos);
  ASSERT(p_strm_data->bit_pos_in_word < 8);
  ASSERT(p_strm_data->bit_pos_in_word == (p_strm_data->strm_buff_read_bits & 0x7));
  if(!p_strm_data->remove_emul3_byte) {

    if((p_strm_data->strm_buff_read_bits + num_bits) >
        (8 * p_strm_data->strm_buff_size)) {
      p_strm_data->strm_buff_read_bits = 8 * p_strm_data->strm_buff_size;
      p_strm_data->bit_pos_in_word = 0;
      p_strm_data->strm_curr_pos =
        p_strm_data->p_strm_buff_start + p_strm_data->strm_buff_size;
      return (END_OF_STREAM);
    } else {
      bytes_left =
        (8 * p_strm_data->strm_buff_size - p_strm_data->strm_buff_read_bits) / 8;
      p_strm = p_strm_data->strm_curr_pos;
      if(p_strm_data->bit_pos_in_word) {
        if(num_bits < 8 - p_strm_data->bit_pos_in_word) {
          p_strm_data->strm_buff_read_bits += num_bits;
          p_strm_data->bit_pos_in_word += num_bits;
          return (HANTRO_OK);
        }
        num_bits -= 8 - p_strm_data->bit_pos_in_word;
        p_strm_data->strm_buff_read_bits += 8 - p_strm_data->bit_pos_in_word;
        p_strm_data->bit_pos_in_word = 0;
        p_strm++;

        if(p_strm_data->strm_buff_read_bits >= 16 && bytes_left &&
            p_strm[-2] == 0x0 && p_strm[-1] == 0x0 && p_strm[0] == 0x3) {
          p_strm++;
          p_strm_data->strm_buff_read_bits += 8;
          bytes_left--;
          p_strm_data->emul_byte_count++;
        }

      }

      while(num_bits >= 8 && bytes_left) {
        if(bytes_left > 2 && p_strm[0] == 0 && p_strm[1] == 0 &&
            p_strm[2] <= 1) {
          /* trying to flush part of start code prefix -> error */
          p_strm_data->strm_curr_pos = p_strm;
          return (HANTRO_NOK);
        }

        p_strm++;
        p_strm_data->strm_buff_read_bits += 8;
        bytes_left--;

        /* check emulation prevention byte */
        if(p_strm_data->strm_buff_read_bits >= 16 && bytes_left &&
            p_strm[-2] == 0x0 && p_strm[-1] == 0x0 && p_strm[0] == 0x3) {
          p_strm++;
          p_strm_data->strm_buff_read_bits += 8;
          bytes_left--;
          p_strm_data->emul_byte_count++;
        }
        num_bits -= 8;
      }

      if(num_bits && bytes_left) {
        if(bytes_left > 2 && p_strm[0] == 0 && p_strm[1] == 0 &&
            p_strm[2] <= 1) {
          /* trying to flush part of start code prefix -> error */
          p_strm_data->strm_curr_pos = p_strm;
          return (HANTRO_NOK);
        }

        p_strm_data->strm_buff_read_bits += num_bits;
        p_strm_data->bit_pos_in_word = num_bits;
        num_bits = 0;
      }

      p_strm_data->strm_curr_pos = p_strm;

      if(num_bits)
        return (END_OF_STREAM);
      else
        return (HANTRO_OK);
    }
  } else {
    p_strm_data->strm_buff_read_bits += num_bits;
    p_strm_data->bit_pos_in_word = p_strm_data->strm_buff_read_bits & 0x7;
    if((p_strm_data->strm_buff_read_bits) <= (8 * p_strm_data->strm_buff_size)) {
      p_strm_data->strm_curr_pos = p_strm_data->p_strm_buff_start +
                                   (p_strm_data->strm_buff_read_bits >> 3);
      return (HANTRO_OK);
    } else
      return (END_OF_STREAM);
  }
}

/*------------------------------------------------------------------------------

    Function: h264bsdIsByteAligned

        Functional description:
            Check if current stream position is byte aligned.

        Inputs:
            p_strm_data   pointer to stream data structure

        Outputs:
            none

        Returns:
            HANTRO_TRUE        stream is byte aligned
            HANTRO_FALSE       stream is not byte aligned

------------------------------------------------------------------------------*/

u32 h264bsdIsByteAligned(strmData_t * p_strm_data) {

  /* Variables */

  /* Code */

  if(!p_strm_data->bit_pos_in_word)
    return (HANTRO_TRUE);
  else
    return (HANTRO_FALSE);

}
