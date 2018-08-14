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

#ifndef __DEMUXER_TYPES_H__
#define __DEMUXER_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"

/* Enumeration for the bitstream format outputted from the reader */
#define BITSTREAM_VP7 0x01
#define BITSTREAM_VP8 0x02
#define BITSTREAM_VP9 0x03
#define BITSTREAM_WEBP 0x04
#define BITSTREAM_H264 0x05
#define BITSTREAM_HEVC 0x06

/* Generic demuxer interface. */
typedef const void* DemuxerOpenFunc(const char* fname, u32 mode);
typedef int DemuxerIdentifyFormatFunc(const void* inst);
typedef void DemuxerHeadersDecoded(const void* inst);
typedef int DemuxerReadPacketFunc(const void* inst, u8* buffer, u8* stream[2], i32* size, u8 rb);
typedef void DemuxerCloseFunc(const void* inst);
typedef struct Demuxer_ {
  const void* inst;
  DemuxerOpenFunc* open;
  DemuxerIdentifyFormatFunc* GetVideoFormat;
  DemuxerHeadersDecoded* HeadersDecoded;
  DemuxerReadPacketFunc* ReadPacket;
  DemuxerCloseFunc* close;
} Demuxer;

#ifdef __cplusplus
}
#endif

#endif /* __DEMUXER_TYPES_H__ */
