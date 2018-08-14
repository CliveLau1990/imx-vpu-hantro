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

#include "jpegdecutils.h"
#include "jpegdecmarkers.h"

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
    5. Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function name: JpegDecGetByte

        Functional description:
          Reads one byte (8 bits) from stream and returns the bits

          Note! This function does not skip the 0x00 byte if the previous
          byte value was 0xFF!!!

        Inputs:
          StreamStorage *stream   Pointer to structure

        Outputs:
          Returns 8 bit value if ok
          else returns STRM_ERROR (0xFFFFFFFF)

------------------------------------------------------------------------------*/
u32 JpegDecGetByte(StreamStorage * stream) {
  u32 tmp;

  if((stream->read_bits + 8) > (8 * stream->stream_length))
    return (STRM_ERROR);

  tmp = *(stream->p_curr_pos)++;
  tmp = (tmp << 8) | *(stream->p_curr_pos);
  tmp = (tmp >> (8 - stream->bit_pos_in_byte)) & 0xFF;
  stream->read_bits += 8;

  if(stream->p_curr_pos > stream->p_start_of_stream + stream->stream_length)
    return (STRM_ERROR);

  return (tmp);
}

/*------------------------------------------------------------------------------

    Function name: JpegDecGet2Bytes

        Functional description:
          Reads two bytes (16 bits) from stream and returns the bits

          Note! This function does not skip the 0x00 byte if the previous
          byte value was 0xFF!!!

        Inputs:
          StreamStorage *stream   Pointer to structure

        Outputs:
          Returns 16 bit value

------------------------------------------------------------------------------*/
u32 JpegDecGet2Bytes(StreamStorage * stream) {
  u32 tmp;

  if((stream->read_bits + 16) > (8 * stream->stream_length))
    return (STRM_ERROR);

  tmp = *(stream->p_curr_pos)++;
  tmp = (tmp << 8) | *(stream->p_curr_pos)++;
  tmp = (tmp << 8) | *(stream->p_curr_pos);
  tmp = (tmp >> (8 - stream->bit_pos_in_byte)) & 0xFFFF;
  stream->read_bits += 16;

  if(stream->p_curr_pos > stream->p_start_of_stream + stream->stream_length)
    return (STRM_ERROR);

  return (tmp);
}

/*------------------------------------------------------------------------------

    Function name: JpegDecShowBits

        Functional description:
          Reads 32 bits from stream and returns the bits, does not update
          stream pointers. If there are not enough bits in data buffer it
          reads the rest of the data buffer bits and fills the lsb of return
          value with zero bits.

          Note! This function will skip the byte valued 0x00 if the previous
          byte value was 0xFF!!!

        Inputs:
          StreamStorage *stream   Pointer to structure

        Outputs:
          Returns  32 bit value

------------------------------------------------------------------------------*/
u32 JpegDecShowBits(StreamStorage * stream) {
  i32 bits;
  u32 read_bits;
  u32 out = 0;
  u8 *p_data = stream->p_curr_pos;

  /* bits left in buffer */
  bits = (i32) (8 * stream->stream_length - stream->read_bits);
  if(!bits)
    return (0);

  read_bits = 0;
  do {
    if(p_data > stream->p_start_of_stream) {
      /* FF 00 bytes in stream -> jump over 00 byte */
      if((p_data[-1] == 0xFF) && (p_data[0] == 0x00)) {
        p_data++;
        bits -= 8;
      }
    }
    if(read_bits == 32 && stream->bit_pos_in_byte) {
      out <<= stream->bit_pos_in_byte;
      out |= *p_data >> (8 - stream->bit_pos_in_byte);
      read_bits = 0;
      break;
    }
    out = (out << 8) | *p_data++;
    read_bits += 8;
    bits -= 8;
  } while(read_bits < (32 + stream->bit_pos_in_byte) && bits > 0);

  if(bits <= 0 &&
      ((read_bits + stream->read_bits) >= (stream->stream_length * 8))) {
    /* not enough bits in stream, fill with zeros */
    out = (out << (32 - (read_bits - stream->bit_pos_in_byte)));
  }

  return (out);
}

/*------------------------------------------------------------------------------

    Function name: JpegDecFlushBits

        Functional description:
          Updates stream pointers, flushes bits from stream

          Note! This function will skip the byte valued 0x00 if the previous
          byte value was 0xFF!!!

        Inputs:
          StreamStorage *stream   Pointer to structure
          u32 bits                 Number of bits to be flushed

        Outputs:
          OK
          STRM_ERROR

------------------------------------------------------------------------------*/
u32 JpegDecFlushBits(StreamStorage * stream, u32 bits) {
  u32 tmp;
  u32 extra_bits = 0;

  if((stream->read_bits + bits) > (8 * stream->stream_length)) {
    /* there are not so many bits left in buffer */
    /* stream pointers to the end of the stream  */
    /* and return value STRM_ERROR               */
    stream->read_bits = 8 * stream->stream_length;
    stream->bit_pos_in_byte = 0;
    stream->p_curr_pos = stream->p_start_of_stream + stream->stream_length;
    return (STRM_ERROR);
  } else {
    tmp = 0;
    while(tmp < bits) {
      if(bits - tmp < 8) {
        if((8 - stream->bit_pos_in_byte) > (bits - tmp)) {
          /* inside one byte */
          stream->bit_pos_in_byte += bits - tmp;
          tmp = bits;
        } else {
          if(stream->p_curr_pos[0] == 0xFF &&
              stream->p_curr_pos[1] == 0x00) {
            extra_bits += 8;
            stream->p_curr_pos += 2;
          } else {
            stream->p_curr_pos++;
          }
          tmp += 8 - stream->bit_pos_in_byte;
          stream->bit_pos_in_byte = 0;
          stream->bit_pos_in_byte = bits - tmp;
          tmp = bits;
        }
      } else {
        tmp += 8;
        if(stream->appn_flag) {
          stream->p_curr_pos++;
        } else {
          if(stream->p_curr_pos[0] == 0xFF &&
              stream->p_curr_pos[1] == 0x00) {
            extra_bits += 8;
            stream->p_curr_pos += 2;
          } else {
            stream->p_curr_pos++;
          }
        }
      }
    }
    /* update stream pointers */
    stream->read_bits += bits + extra_bits;

    if(stream->p_curr_pos > stream->p_start_of_stream + stream->stream_length)
      return (STRM_ERROR);

    return (OK);
  }
}
