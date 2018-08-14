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

#include "sw_util.h"
#include "sw_debug.h"

u32 SwCountLeadingZeros(u32 value, u32 length) {

  u32 zeros = 0;
  u32 mask = 1 << (length - 1);

  ASSERT(length <= 32);

  while (mask && !(value & mask)) {
    zeros++;
    mask >>= 1;
  }
  return (zeros);
}

u32 SwNumBits(u32 value) {
  return 32 - SwCountLeadingZeros(value, 32);
}

u8* SwTurnAround(const u8 * strm, const u8* buf, u8* tmp_buf, u32 buf_size, u32 num_bits) {
  u32 bytes = (num_bits+7)/8;
  u32 is_turn_around = 0;
  if((strm + bytes) > (buf + buf_size))
    is_turn_around = 1;

  if((addr_t)strm - (addr_t)buf < 2)  {
    ASSERT(is_turn_around == 0);
    is_turn_around = 2;
  }

  if(is_turn_around == 0) {
    return NULL;
  } else if(is_turn_around == 1) {
    i32 i;
    u32 bytes_left = (u32)((addr_t)(buf + buf_size) - (addr_t)strm);

    /* turn around */
    for(i = -3; i < (i32)bytes_left; i++) {
      tmp_buf[3 + i] = DWLPrivateAreaReadByte(strm + i);
    }

    /*turn around point*/
    for(i = 0; i < (bytes - bytes_left); i++) {
      tmp_buf[3 + bytes_left + i] = DWLPrivateAreaReadByte(buf + i);
    }

    return tmp_buf+3;
  } else {
    i32 i;
    u32 left_byte = (u32)((addr_t) strm - (addr_t) buf);
    /* turn around */
    for(i = 0; i < 2; i++) {
      tmp_buf[i] = DWLPrivateAreaReadByte(buf + buf_size - 2 + i);
    }

    /*turn around point*/
    for(i = 0; i < bytes + left_byte; i++) {
      tmp_buf[i + 2] = DWLPrivateAreaReadByte(buf + i);
    }

    return (tmp_buf + 2 + left_byte);
  }
}
