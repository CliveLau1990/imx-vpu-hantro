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

#include "software/source/hevc/hevc_nal_unit_type.h"
#include "software/test/common/bytestream_parser.h"
#include "software/test/common/command_line_parser.h"

#include <stdlib.h>
#include <stdio.h>

enum BSBoundaryType {
  BSPARSER_NO_BOUNDARY = 0,
  BSPARSER_BOUNDARY = 1,
  BSPARSER_BOUNDARY_NON_SLICE_NAL = 2
};

struct BSParser {
  FILE* file;
  off_t size;
  u32 mode;
};

static off_t FindNextStartCode(struct BSParser* inst, u32* zero_count) {
  off_t start = ftello(inst->file);
  *zero_count = 0;
  /* Scan for the beginning of the packet. */
  for (int i = 0; i < inst->size && i < inst->size - start; i++) {
    unsigned char byte;
    int ret_val = fgetc(inst->file);
    if (ret_val == EOF) return ftello(inst->file);
    byte = (unsigned char)ret_val;
    switch (byte) {
    case 0:
      *zero_count = *zero_count + 1;
      break;
    case 1:
      /* If there's more than three leading zeros, consider only three
       * of them to be part of this packet and the rest to be part of
       * the previous packet. */
      if (*zero_count > 3) *zero_count = 3;
      if (*zero_count >= 2) {
        return ftello(inst->file) - *zero_count - 1;
      }
      *zero_count = 0;
      break;
    default:
      *zero_count = 0;
      break;
    }
  }
  return ftello(inst->file);
}

BSParserInst ByteStreamParserOpen(const char* fname, u32 mode) {
  struct BSParser* inst = malloc(sizeof(struct BSParser));
  inst->mode = mode;
  inst->file = fopen(fname, "rb");
  if (inst->file == NULL) {
    free(inst);
    return NULL;
  }
  fseeko(inst->file, 0, SEEK_END);
  inst->size = ftello(inst->file);
  fseeko(inst->file, 0, SEEK_SET);
  return inst;
}

int ByteStreamParserIdentifyFormat(BSParserInst instance) {
  // struct BSParser* inst = (struct BSParser*)instance;
  /* TODO(vmr): Implement properly when H.264 is added, too. */
  return BITSTREAM_HEVC;
}

void ByteStreamParserHeadersDecoded(BSParserInst inst) {
  return;
}

u32 CheckAccessUnitBoundary(FILE* file, off_t nal_begin) {
  u32 is_boundary = BSPARSER_NO_BOUNDARY;
  u32 nal_type, val;

  off_t start = ftello(file);

  fseeko(file, nal_begin + 1, SEEK_SET);
  nal_type = (getc(file) & 0x7E) >> 1;

  if (nal_type > NAL_CODED_SLICE_CRA)
    is_boundary = BSPARSER_BOUNDARY_NON_SLICE_NAL;
  else {
    val = getc(file);  // nothing interesting here...
    val = getc(file);
    /* Check if first slice segment in picture */
    if (val & 0x80) is_boundary = BSPARSER_BOUNDARY;
  }

  fseeko(file, start, SEEK_SET);
  return is_boundary;
}

int ByteStreamParserReadFrame(BSParserInst instance, u8* buffer,
                              u8 *stream[2], i32* size, u8 is_ringbuffer) {
  struct BSParser* inst = (struct BSParser*)instance;
  off_t begin, end, strm_len, offset;
  u32 buf_len = *size;
  u32 strm_read_len;
  u8* strm = stream[1];
  u32 zero_count = 0;

  if (inst->mode == STREAMREADMODE_FULLSTREAM) {
    if (ftello(inst->file) == inst->size) return 0; /* End of stream */
    begin = 0;
    end = inst->size;
  } else if (inst->mode == STREAMREADMODE_FRAME) {
    u32 new_access_unit = 0, tmp = 0;
    off_t nal_begin;

    begin = FindNextStartCode(inst, &zero_count);

    /* Check for non-slice type in current NAL. non slice NALs are
     * decoded one-by-one */
    nal_begin = begin + zero_count;
    tmp = CheckAccessUnitBoundary(inst->file, nal_begin);

    end = nal_begin = FindNextStartCode(inst, &zero_count);

    /* if there is more stream and a slice type NAL */
    if (end != begin && tmp != BSPARSER_BOUNDARY_NON_SLICE_NAL) {
      do {
        end = nal_begin;
        nal_begin += zero_count;

        /* Check access unit boundary for next NAL */
        new_access_unit = CheckAccessUnitBoundary(inst->file, nal_begin);
        if (new_access_unit != BSPARSER_BOUNDARY) {
          nal_begin = FindNextStartCode(inst, &zero_count);
        }
      } while (new_access_unit != BSPARSER_BOUNDARY && end != nal_begin);
    }
  } else {
    begin = FindNextStartCode(inst, &zero_count);
    /* If we have NAL unit mode, strip the leading start code. */
    if (inst->mode == STREAMREADMODE_NALUNIT) begin += zero_count;
    end = FindNextStartCode(inst, &zero_count);
  }
  if (end == begin) {
    return 0; /* End of stream */
  }
  fseeko(inst->file, begin, SEEK_SET);
  if (*size < end - begin) {
    *size = end - begin;
    return -1; /* Insufficient buffer size */
  }

  strm_len = end - begin;
  if(is_ringbuffer) {
    offset = (off_t)(strm - buffer);
    stream[0] = stream[1];
    if(offset + strm_len < buf_len) {
      /* no turnaround */
      strm_read_len = fread(strm, 1, strm_len, inst->file);
      stream[1] = strm + strm_read_len;
    } else {
      /* turnaround */
      u32 tmp_len;
      strm_read_len = fread(strm, 1, buf_len - offset, inst->file);
      tmp_len = fread(buffer, 1, strm_len - (buf_len - offset), inst->file);
      strm_read_len += tmp_len;
      stream[1] = buffer + tmp_len;
    }
  } else {
    strm_read_len = fread(buffer, 1, strm_len, inst->file);
    stream[0] = buffer;
    stream[1] = buffer + strm_read_len;
  }
  return strm_read_len;
}

void ByteStreamParserClose(BSParserInst instance) {
  struct BSParser* inst = (struct BSParser*)instance;
  fclose(inst->file);
  free(inst);
}
