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

#include "hevc_util.h"

static u32 MoreRbspTrailingData(struct StrmData *stream);

u32 HevcRbspTrailingBits(struct StrmData *stream) {

  u32 stuffing;
  u32 stuffing_length;

  ASSERT(stream);
  ASSERT(stream->bit_pos_in_word < 8);

  stuffing_length = 8 - stream->bit_pos_in_word;

  stuffing = SwGetBits(stream, stuffing_length);
  if (stuffing == END_OF_STREAM) return (HANTRO_NOK);

  return (HANTRO_OK);
}

u32 MoreRbspTrailingData(struct StrmData *stream) {

  i32 bits;

  ASSERT(stream);
  //TODO: This assert is necessary?
  //ASSERT(stream->strm_buff_read_bits <= 8 * stream->strm_data_size);

  bits = (i32)stream->strm_data_size * 8 - (i32)stream->strm_buff_read_bits;
  if (bits >= 8)
    return (HANTRO_TRUE);
  else
    return (HANTRO_FALSE);
}

u32 HevcMoreRbspData(struct StrmData *stream) {

  u32 bits;

  ASSERT(stream);
  ASSERT(stream->strm_buff_read_bits <= 8 * stream->strm_data_size);

  bits = stream->strm_data_size * 8 - stream->strm_buff_read_bits;

  if (bits == 0) return (HANTRO_FALSE);

  if (bits > 8) {
    if (stream->remove_emul3_byte) return (HANTRO_TRUE);

    bits &= 0x7;
    if (!bits) bits = 8;
    if (SwShowBits(stream, bits) != (1U << (bits - 1)) ||
        (SwShowBits(stream, 23 + bits) << 9))
      return (HANTRO_TRUE);
    else
      return (HANTRO_FALSE);
  } else if (SwShowBits(stream, bits) != (1U << (bits - 1)))
    return (HANTRO_TRUE);
  else
    return (HANTRO_FALSE);
}

u32 HevcCheckCabacZeroWords(struct StrmData *strm_data) {

  u32 tmp;

  ASSERT(strm_data);

  if (MoreRbspTrailingData(strm_data)) {
    tmp = SwGetBits(strm_data, 16);
    if (tmp == END_OF_STREAM)
      return HANTRO_OK;
    else if (tmp == 0xFFFF) {
      /* Workaround: some testing streams use 0xFFFF instead of 0x0000
       * as cabac_zero_words, which is not completely compatable with spec. */
      return HANTRO_OK;
    }
    else if (tmp)
      return HANTRO_NOK;
  }

  return HANTRO_OK;
}
