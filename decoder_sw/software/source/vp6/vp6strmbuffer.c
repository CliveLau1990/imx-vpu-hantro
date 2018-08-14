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

#include "basetype.h"
#include "vp6strmbuffer.h"

/*------------------------------------------------------------------------------
    Function name   : Vp6StrmInit
    Description     :
    Return type     : u32
    Argument        : Vp6StrmBuffer * sb
    Argument        : const u8 * data
    Argument        : u32 amount
------------------------------------------------------------------------------*/
u32 Vp6StrmInit(Vp6StrmBuffer * sb, const u8 * data, u32 amount) {
  sb->buffer = data;
  sb->pos = 4;
  sb->bits_in_buffer = 32;
  sb->val = (u32) data[0] << 24 | (u32) data[1] << 16 | (u32) data[2] << 8
            | (u32) data[3];
  sb->amount_left = amount - 4;
  sb->bits_consumed = 0;
  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : Vp6StrmGetBits
    Description     :
    Return type     : u32
    Argument        : Vp6StrmBuffer * sb
    Argument        : u32 bits
------------------------------------------------------------------------------*/
u32 Vp6StrmGetBits(Vp6StrmBuffer * sb, u32 bits) {
  u32 ret_val;
  const u8 *strm = &sb->buffer[sb->pos];

  sb->bits_consumed += bits;

  if(bits < sb->bits_in_buffer) {
    ret_val = sb->val >> (32 - bits);
  } else {
    ret_val = sb->val >> (32 - bits);
    bits -= sb->bits_in_buffer;
    sb->val =
      ((u32) strm[0]) << 24 | ((u32) strm[1]) << 16 | ((u32) strm[2]) << 8
      | ((u32) strm[3]);
    sb->pos += 4;
    sb->bits_in_buffer = 32;
    if(bits) {
      u32 tmp = sb->val >> (32 - bits);

      ret_val <<= bits;
      ret_val |= tmp;
    }
  }
  sb->val <<= bits;
  sb->bits_in_buffer -= bits;

  return ret_val;
}
