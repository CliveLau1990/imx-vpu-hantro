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

#include "hevc_byte_stream.h"
#include "hevc_util.h"

#define BYTE_STREAM_ERROR 0xFFFFFFFF

/*------------------------------------------------------------------------------

    Extracts one NAL unit from the byte stream buffer.

    Stream buffer is assumed to contain either exactly one NAL unit
    and nothing else, or one or more NAL units embedded in byte
    stream format described in the Annex B of the standard. Function
    detects which one is used based on the first bytes in the buffer.

------------------------------------------------------------------------------*/
u32 HevcExtractNalUnit(const u8 *byte_stream, u32 strm_len,
                       const u8 *strm_buf, u32 buf_len,
                       struct StrmData *stream, u32 *read_bytes,
                       u32 *start_code_detected) {

  /* Variables */

  /* Code */

  ASSERT(byte_stream);
  ASSERT(strm_len);
  ASSERT(strm_len < BYTE_STREAM_ERROR);
  ASSERT(stream);

  /* from strm to buf end */
  stream->strm_buff_start = strm_buf;
  stream->strm_curr_pos = byte_stream;
  stream->bit_pos_in_word = 0;
  stream->strm_buff_read_bits = 0;
  stream->strm_buff_size = buf_len;
  stream->strm_data_size = strm_len;

  stream->remove_emul3_byte = 1;

  /* byte stream format if starts with 0x000001 or 0x000000. Force using
   * byte stream format if start codes found earlier. */
  if (*start_code_detected || SwShowBits(stream, 3) <= 0x01) {
    *start_code_detected = 1;
    DEBUG_PRINT(("BYTE STREAM detected\n"));

    /* search for NAL unit start point, i.e. point after first start code
     * prefix in the stream */
    while (SwShowBits(stream, 24) != 0x01) {
      if (SwFlushBits(stream, 8) == END_OF_STREAM) {
        *read_bytes = strm_len;
        stream->remove_emul3_byte = 0;
        return HANTRO_NOK;
      }
    }
    if (SwFlushBits(stream, 24) == END_OF_STREAM) {
      *read_bytes = strm_len;
      stream->remove_emul3_byte = 0;
      return HANTRO_NOK;
    }
  }

  /* return number of bytes "consumed" */
  stream->remove_emul3_byte = 0;
  *read_bytes = stream->strm_buff_read_bits / 8;
  return (HANTRO_OK);
}

/* Searches next start code in the stream buffer. */
u32 HevcNextStartCode(struct StrmData *stream) {

  u32 tmp;

  if (stream->bit_pos_in_word) SwGetBits(stream, 8 - stream->bit_pos_in_word);

  stream->remove_emul3_byte = 1;

  while (1) {
    tmp = SwShowBits(stream, 32);
    if (tmp <= 0x01 || (tmp >> 8) == 0x01) {
      stream->remove_emul3_byte = 0;
      return HANTRO_OK;
    }

    if (SwFlushBits(stream, 8) == END_OF_STREAM) {
      stream->remove_emul3_byte = 0;
      return END_OF_STREAM;
    }
  }

  return HANTRO_OK;
}
