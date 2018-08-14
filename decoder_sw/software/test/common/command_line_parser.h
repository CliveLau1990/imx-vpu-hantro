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

#ifndef __COMMAND_LINE_PARSER_H__
#define __COMMAND_LINE_PARSER_H__

#include "basetype.h"
#include "decapicommon.h"
#include "error_simulator.h"
#include "dectypes.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  VP8DEC_DECODER_ALLOC = 0,
  VP8DEC_EXTERNAL_ALLOC = 1,
  VP8DEC_EXTERNAL_ALLOC_ALT = 2
};

enum FileFormat {
  FILEFORMAT_AUTO_DETECT = 0,
  FILEFORMAT_BYTESTREAM,
  FILEFORMAT_IVF,
  FILEFORMAT_WEBM
};

enum StreamReadMode {
  STREAMREADMODE_FRAME = 0,
  STREAMREADMODE_NALUNIT = 1,
  STREAMREADMODE_FULLSTREAM = 2,
  STREAMREADMODE_PACKETIZE = 3
};

enum SinkType {
  SINK_FILE_SEQUENCE = 0,
  SINK_FILE_PICTURE,
  SINK_MD5_SEQUENCE,
  SINK_MD5_PICTURE,
  SINK_SDL,
  SINK_NULL
};

struct TestParams {
  char* in_file_name;
  char* out_file_name;
  u32 num_of_decoded_pics;
  enum DecPictureFormat format;
  enum DecPictureFormat hw_format;
  enum SinkType sink_type;
  u8 display_cropped;
  u8 hw_traces;
  u8 trace_target;
  u8 extra_output_thread;
  u8 disable_display_order;
  enum FileFormat file_format;
  enum StreamReadMode read_mode;
  struct ErrorSimulationParams error_sim;
  enum DecErrorConcealment concealment_mode;
  struct DecDownscaleCfg dscale_cfg;
  u8 compress_bypass;   /* compressor bypass flag */
  u8 is_ringbuffer;     /* ringbuffer mode by default */
  u8 fetch_one_pic;     /* prefetch one pic_id together */
  u32 force_output_8_bits;  /* Output 8 bits per pixel. */
  u32 p010_output;          /* Output in MS P010 format. */
  u32 bigendian_output;            /* Output big endian format. */
};

void PrintUsage(char* executable);
void SetupDefaultParams(struct TestParams* params);
int ParseParams(int argc, char* argv[], struct TestParams* params);
int ResolveOverlap(struct TestParams* params);

#ifdef __cplusplus
}
#endif

#endif /* __COMMAND_LINE_PARSER_H__ */
