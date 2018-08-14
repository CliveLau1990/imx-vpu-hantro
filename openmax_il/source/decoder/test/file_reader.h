/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
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

#ifndef FILE_READER_H
#define FILE_READER_H

typedef struct RCVSTATE
{
   int rcV1;
   int advanced;
   int filesize;
} RCVSTATE;

int read_any_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_mpeg2_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_mpeg4_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_sorenson_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_h263_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_h264_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_vp6_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_DIVX3_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_vp8_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_webp_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_avs_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
int read_mjpeg_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
#ifdef ENABLE_CODEC_RV
int read_rv_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);
#endif
int read_rcv_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof);

#endif
