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

#ifndef __VP9_BOOL_H__
#define __VP9_BOOL_H__

#include "basetype.h"

#if 0
#include <stdio.h>
#define STREAM_TRACE(x, y) printf("%-30s-%9d\n", x, y);
#define VP9DEC_DEBUG(x) printf x
#else
#define STREAM_TRACE(x, y)
#define VP9DEC_DEBUG(x)
#endif

#define CHECK_END_OF_STREAM(s) \
  if ((s) == END_OF_STREAM) return (s)

struct VpBoolCoder {
  u32 lowvalue;
  u32 range;
  u32 value;
  i32 count;
  u32 read_len;
  const u8 *buffer;
  const u8 *buffer_start;
  u32 BitCounter;
  u32 buffer_len;
  u32 stream_len;
  u32 strm_error;
};

extern void Vp9BoolStart(struct VpBoolCoder *bc, const u8* stream,
                         u32 strm_len, const u8* strm_buf, u32 buf_len);
extern u32 Vp9DecodeBool(struct VpBoolCoder *bc, i32 probability);
extern u32 Vp9DecodeBool128(struct VpBoolCoder *bc);
extern void Vp9BoolStop(struct VpBoolCoder *bc);

u32 Vp9DecodeSubExp(struct VpBoolCoder *bc, u32 k, u32 num_syms);
u32 Vp9ReadBits(struct VpBoolCoder *br, i32 bits);

#endif /* __VP9_BOOL_H__ */
