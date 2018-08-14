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
#include "bytestream_parser.h"

#include <stdio.h>
#include <stdlib.h>

static int FindNextNALStart(FILE *f, off_t *start) {
  int zero_count = 0;
  char byte;
  int ret;

  off_t nal_start = 0;

  while (1) {
    ret = fread(&byte, 1, 1, f);

    if (feof(f)) {
      break;
    }

    if (byte == 0) {
      zero_count++;
    } else if (byte == 1 && zero_count >= 2) {
      /* we found the start code!
       * we can have one extra leading zero byte and the rest are
       * considered trailing zeros for the previous unit
       */
      nal_start = ftello(f);
      nal_start -= 1; /* 1 byte */
      nal_start -= (zero_count > 3 ? 3 : zero_count); /* max 3 zero bytes */

      break;
    } else {
      /* reset zero count and try again*/
      zero_count = 0;
    }
  }

  *start = nal_start;

  if (nal_start == 0 && feof(f))
    return -1;
  else
    return 0;
}

u32 NextNALFromFile(FILE *finput, u8 *stream_buff, u32 buff_size) {
  off_t next_nal_start, nal_start, nal_size;

  int eof,ret;

  /* start of current NAL */
  eof = FindNextNALStart(finput, &nal_start);

  if (eof) {
    return 0;
  }

  /* start of next NAL */
  eof = FindNextNALStart(finput, &next_nal_start);

  if (eof) {
    /* last NAL of the stream */
    fseeko(finput, 0, SEEK_END);
    next_nal_start = ftello(finput);
  }

  fseeko(finput, nal_start, SEEK_SET);

  nal_size = next_nal_start - nal_start;

  if (nal_size > buff_size) {
    fprintf(stderr, "NAL does not fit provided buffer\n");
    return 0;
  }

  ret = fread(stream_buff, 1, nal_size, finput);

  return (u32) nal_size;
}
