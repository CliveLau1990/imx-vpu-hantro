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

#include "h264hwd_byte_stream.h"
#include "h264hwd_util.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

#define BYTE_STREAM_ERROR  0xFFFFFFFF

#ifdef USE_WORDACCESS
/* word width */
#define WORDWIDTH_64BIT
//#define WORDWIDTH_32BIT

#if defined (WORDWIDTH_64BIT)
typedef u64 uword;
#define WORDWIDTH                 64
#define WORDADDR_REMAINDER_MASK   0x07

#elif defined(WORDWIDTH_32BIT)
typedef u32 uword;
#define WORDWIDTH                 32
#define WORDADDR_REMAINDER_MASK   0x03

#else
#error "Please specify word length!"
#endif
#endif


/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

#ifdef USE_WORDACCESS
static u32 IsLittleEndian()
{
  uword i = 1;
  if (*((u8 *)&i))
    return 1;
  else
    return 0;
}
#endif


/*------------------------------------------------------------------------------

    Function name: ExtractNalUnit

        Functional description:
            Extracts one NAL unit from the byte stream buffer. Removes
            emulation prevention bytes if present. The original stream buffer
            is used directly and is therefore modified if emulation prevention
            bytes are present in the stream.

            Stream buffer is assumed to contain either exactly one NAL unit
            and nothing else, or one or more NAL units embedded in byte
            stream format described in the Annex B of the standard. Function
            detects which one is used based on the first bytes in the buffer.

        Inputs:
            p_byte_stream     pointer to byte stream buffer
            len             length of the stream buffer (in bytes)

        Outputs:
            p_strm_data       stream information is stored here
            read_bytes       number of bytes "consumed" from the stream buffer

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      error in byte stream

------------------------------------------------------------------------------*/

u32 h264bsdExtractNalUnit(const u8 * p_byte_stream, u32 len,
                          strmData_t * p_strm_data, u32 * read_bytes, u32 rlc_mode) {

  /* Variables */

  u32 byte_count, init_byte_count;
  u32 zero_count;
  u8 byte;
  u32 invalid_stream = HANTRO_FALSE;
  u32 has_emulation = HANTRO_FALSE;
  const u8 *read_ptr;

  /* Code */

  ASSERT(p_byte_stream);
  ASSERT(len);
  ASSERT(len < BYTE_STREAM_ERROR);
  ASSERT(p_strm_data);

  /* byte stream format if starts with 0x000001 or 0x000000 */
  if(len > 3 && p_byte_stream[0] == 0x00 && p_byte_stream[1] == 0x00 &&
      (p_byte_stream[2] & 0xFE) == 0x00) {
    DEBUG_PRINT(("BYTE STREAM detected\n"));
    /* search for NAL unit start point, i.e. point after first start code
     * prefix in the stream */
    zero_count = byte_count = 2;
    read_ptr = p_byte_stream + 2;
    /*lint -e(716) while(1) used consciously */
    while(1) {
      byte = *read_ptr++;
      byte_count++;

      if(byte_count == len) {
        /* no start code prefix found -> error */
        *read_bytes = len;

        ERROR_PRINT("NO START CODE PREFIX");
        return (HANTRO_NOK);
      }

      if(!byte)
        zero_count++;
      else if((byte == 0x01) && (zero_count >= 2))
        break;
      else
        zero_count = 0;
    }

    init_byte_count = byte_count;
#if 1
    if(!rlc_mode) {
      p_strm_data->p_strm_buff_start = p_byte_stream + init_byte_count;
      p_strm_data->strm_curr_pos = p_strm_data->p_strm_buff_start;
      p_strm_data->bit_pos_in_word = 0;
      p_strm_data->strm_buff_read_bits = 0;
      p_strm_data->strm_buff_size = len - init_byte_count;
      *read_bytes = len;
      return (HANTRO_OK);
    }
#endif
    /* determine size of the NAL unit. Search for next start code prefix
     * or end of stream and ignore possible trailing zero bytes */
    zero_count = 0;
    /*lint -e(716) while(1) used consciously */
    while(1) {
      byte = *read_ptr++;
      byte_count++;
      if(!byte)
        zero_count++;
      else {
        if((byte == 0x03) && (zero_count == 2)) {
          has_emulation = HANTRO_TRUE;
        } else if((byte == 0x01) && (zero_count >= 2)) {
          p_strm_data->strm_buff_size =
            byte_count - init_byte_count - zero_count - 1;
          zero_count -= MIN(zero_count, 3);
          break;
        }

        if(zero_count >= 3)
          invalid_stream = HANTRO_TRUE;

        zero_count = 0;
      }

      if(byte_count == len) {
        p_strm_data->strm_buff_size = byte_count - init_byte_count - zero_count;
        break;
      }

    }
  }
  /* separate NAL units as input -> just set stream params */
  else {
    DEBUG_PRINT(("SINGLE NAL unit detected\n"));

    init_byte_count = 0;
    zero_count = 0;
    p_strm_data->strm_buff_size = len;
    has_emulation = HANTRO_TRUE;
  }

  p_strm_data->p_strm_buff_start = p_byte_stream + init_byte_count;
  p_strm_data->strm_curr_pos = p_strm_data->p_strm_buff_start;
  p_strm_data->bit_pos_in_word = 0;
  p_strm_data->strm_buff_read_bits = 0;

  /* return number of bytes "consumed" */
  *read_bytes = p_strm_data->strm_buff_size + init_byte_count + zero_count;

  if(invalid_stream) {

    ERROR_PRINT("INVALID STREAM");
    return (HANTRO_NOK);
  }

  /* remove emulation prevention bytes before rbsp processing */
  if(has_emulation && p_strm_data->remove_emul3_byte) {
    i32 i = p_strm_data->strm_buff_size;
    u8 *write_ptr = (u8 *) p_strm_data->p_strm_buff_start;

    read_ptr = p_strm_data->p_strm_buff_start;

    zero_count = 0;
    while(i--) {
      if((zero_count == 2) && (*read_ptr == 0x03)) {
        /* emulation prevention byte shall be followed by one of the
         * following bytes: 0x00, 0x01, 0x02, 0x03. This implies that
         * emulation prevention 0x03 byte shall not be the last byte
         * of the stream. */
        if((i == 0) || (*(read_ptr + 1) > 0x03))
          return (HANTRO_NOK);

        DEBUG_PRINT(("EMULATION PREVENTION 3 BYTE REMOVED\n"));

        /* do not write emulation prevention byte */
        read_ptr++;
        zero_count = 0;
      } else {
        /* NAL unit shall not contain byte sequences 0x000000,
         * 0x000001 or 0x000002 */
        if((zero_count == 2) && (*read_ptr <= 0x02))
          return (HANTRO_NOK);

        if(*read_ptr == 0)
          zero_count++;
        else
          zero_count = 0;

        *write_ptr++ = *read_ptr++;
      }
    }

    /* (read_ptr - write_ptr) indicates number of "removed" emulation
     * prevention bytes -> subtract from stream buffer size */
    p_strm_data->strm_buff_size -= (u32) (read_ptr - write_ptr);
  }

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------
    Function name   : h264bsdFindNextStartCode
    Description     :
    Return type     : const u8 *
    Argument        : const u8 * p_byte_stream
    Argument        : u32 len
------------------------------------------------------------------------------*/
const u8 *h264bsdFindNextStartCode(const u8 * p_byte_stream, u32 len) {
#ifndef USE_WORDACCESS
  u32 byte_count = 0;
  u32 zero_count = 0;
  const u8 *start = NULL;

  /* determine size of the NAL unit. Search for next start code prefix
   * or end of stream  */

  while(byte_count++ < len) {
    u32 byte = *p_byte_stream++;

    if(byte == 0)
      zero_count++;
    else {
      if((byte == 0x01) && (zero_count >= 2)) {
        start = p_byte_stream - MIN(zero_count, 3) - 1;
        break;
      }

      zero_count = 0;
    }
  }

  return start;
#else
  const u8 * strm = p_byte_stream;
  const u8 * start;
  u32 found = 0;
  u8 data = 0xff;
  u32 byte_count = 0;
  u32 zero_count = 0; // run zero
  uword data_in_word = 0;
  i32 bit_pos = 0;
  u32 align = (WORDWIDTH / 8) - ((addr_t)p_byte_stream & WORDADDR_REMAINDER_MASK);
  u32 is_little_e = IsLittleEndian();

  if (align == WORDWIDTH / 8) align = 0;
  if (align > len) align = len;

  /* prologue */
  while(align > byte_count) {
    data = *strm++;
    byte_count++;

    if(data == 0) zero_count++;
    else if((data == 0x01) && (zero_count >= 2)) {
      strm = strm - MIN(zero_count, 3) - 1;
      found = 1;
      break;
    } else
      zero_count = 0;
  }

  /* word access search */
  if(found == 0) {
    while(len >= (byte_count + WORDWIDTH / 8)) {
      data_in_word = *((uword *)strm);

      if(data_in_word == 0x00) {
        /* boost zero stuffing skip performance */
        zero_count += (WORDWIDTH / 8);
        byte_count += (WORDWIDTH / 8);
        strm += (WORDWIDTH / 8);
      } else {
        bit_pos = 0;
        do {
          /* big endian or small endian */
          if(is_little_e)
            data = (u8)(data_in_word >> bit_pos);
          else
            data = (u8)(data_in_word >> ((WORDWIDTH - 8) - bit_pos));

          bit_pos += 8; // 8bit shift

          if(data == 0x0)
            zero_count++;
          else if(data == 0x01 && zero_count >= 2) {
            found = 1;
            strm = strm + bit_pos/8 - MIN(zero_count, 3) - 1;
            break;
          } else
            zero_count = 0;

        } while(bit_pos < WORDWIDTH);

        if(found) break;

        strm += (bit_pos >> 3); // bit to byte
        byte_count += (bit_pos >> 3);
      }
    }
  } // word access search end

  /* epilogue */
  if(found == 0) {
    while(len > byte_count) {
      data = *strm++;
      byte_count++;

      if(data == 0x0)
        zero_count++;
      else if(data == 0x01 && zero_count >= 2) {
        strm = strm - MIN(zero_count, 3) - 1;
        found = 1;
        break;
      } else
        zero_count = 0;
    }
  }

  /* update status & retrun code*/
  if(found && (len > byte_count))
    start = strm;
  else
    /* not found, discard all */
    start = NULL;

  return start;
#endif
}
