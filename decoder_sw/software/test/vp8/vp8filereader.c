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

#include "vp8filereader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ivf.h"
#ifdef WEBM_ENABLED
#include <stdarg.h>
#include "nestegg/include/nestegg/nestegg.h"
#endif  /* WEBM_ENABLED */

/* Module defines */
#define VP8_FOURCC (0x00385056)
/* TODO(vmr): HANTRO_* values should be defined centrally. */
#define HANTRO_OK     (0)
#define HANTRO_NOK    (1)
#define HANTRO_FALSE  (0)
#define HANTRO_TRUE   (1)

/* Module datatypes */
typedef enum {
  FF_VP7,
  FF_VP8,
  FF_WEBP,
  FF_WEBM,
  FF_NONE
} RdrFileFormat;

#ifdef WEBM_ENABLED
typedef struct input_ctx_ {
  nestegg        *nestegg_ctx;
  nestegg_packet *pkt;
  unsigned int    chunk;
  unsigned int    chunks;
  unsigned int    video_track;
} input_ctx_t;
#endif  /* WEBM_ENABLED */

typedef struct ffReader_s {
  RdrFileFormat format_;
  BitstreamFormat bitstream_format_;
  u32 ivf_headers_read_;
  FILE* file_;
#ifdef WEBM_ENABLED
  input_ctx_t input_ctx_;
#endif  /* WEBM_ENABLED */
} ffReader_t;

/* Module local functions. */
static u32 ReadIvfFileHeader(FILE* fin);
static u32 ReadIvfFrameHeader(FILE* fin, u32 *frame_size);
static BitstreamFormat FfCheckFormat(reader_inst inst);
#ifdef WEBM_ENABLED
static int file_is_webm(input_ctx_t *input, FILE* infile, unsigned int* fourcc,
                        unsigned int* width, unsigned int* height,
                        unsigned int *fps_den, unsigned int *fps_num);
#endif  /* WEBM_ENABLED */

reader_inst rdr_open(char* filename) {
  ffReader_t* inst = calloc(1, sizeof(ffReader_t));
  if (inst==NULL)
    return NULL;
  inst->format_ = FF_NONE;
  inst->file_ = fopen(filename, "rb");
  if (inst->file_ == NULL) {
    free(inst);
    return NULL;
  }
  inst->bitstream_format_ = FfCheckFormat(inst);
  return inst;
}

void rdr_close(reader_inst inst) {
  ffReader_t* reader = (ffReader_t*)inst;
  fclose(reader->file_);
  free(reader);
}

BitstreamFormat rdr_identify_format(reader_inst inst) {
  ffReader_t* reader = (ffReader_t*)inst;
  return reader->bitstream_format_;
}

i32 rdr_read_frame(reader_inst inst, const u8 *buffer, u32 max_buffer_size,
                   u32 *frame_size, u32 pedantic_mode ) {
  u32 tmp;
  u32 ret;
  u32 pos;
  ffReader_t* ff = (ffReader_t*)inst;
  FILE* fin = ff->file_;

  pos = ftell(fin);
  /* Read VP8 IVF file header */
  if( ff->format_ == FF_VP8 &&
      ff->ivf_headers_read_ == HANTRO_FALSE ) {
    tmp = ReadIvfFileHeader( fin );
    if( tmp != HANTRO_OK )
      return tmp;
    ff->ivf_headers_read_ = HANTRO_TRUE;
  }

  /* Read VP8 IVF file header */
  if( ff->format_ == FF_VP8 &&
      ff->ivf_headers_read_ == HANTRO_FALSE ) {
    tmp = ReadIvfFileHeader( fin );
    if( tmp != HANTRO_OK )
      return tmp;
    ff->ivf_headers_read_ = HANTRO_TRUE;
  }

  /* Read frame header */
  if( ff->format_ == FF_VP8 ) {
    tmp = ReadIvfFrameHeader( fin, frame_size );
    if( tmp != HANTRO_OK )
      return tmp;
  } else if (ff->format_ == FF_VP7) {
    u8 size[4];
    tmp = fread( size, sizeof(u8), 4, fin );
    if( tmp != 4 )
      return HANTRO_NOK;

    *frame_size =
      size[0] +
      (size[1] << 8) +
      (size[2] << 16) +
      (size[3] << 24);
  } else if (ff->format_ == FF_WEBP) {
    char signature[] = "WEBP";
    char format_[] = "VP8 ";
    u8 tmp[4];
    fseek(fin, 8, SEEK_CUR);
    ret = fread(tmp, sizeof(u8), 4, fin);
    if (strncmp(signature, tmp, 4))
      return HANTRO_NOK;
    ret = fread(tmp, sizeof(u8), 4, fin);
    if (strncmp(format_, tmp, 4))
      return HANTRO_NOK;
    ret = fread(tmp, sizeof(u8), 4, fin);
    *frame_size =
      tmp[0] +
      (tmp[1] << 8) +
      (tmp[2] << 16) +
      (tmp[3] << 24);
  }
#ifdef WEBM_ENABLED
  else { /* FF_WEBM */
    size_t buf_sz;
    u8 *tmp_pkt;
    if(ff->input_ctx_.chunk >= ff->input_ctx_.chunks) {
      unsigned int track;

      do {
        /* End of this packet, get another. */
        if(ff->input_ctx_.pkt)
          nestegg_free_packet(ff->input_ctx_.pkt);

        if(nestegg_read_packet(ff->input_ctx_.nestegg_ctx,
                               &ff->input_ctx_.pkt) <= 0 ||
            nestegg_packet_track(ff->input_ctx_.pkt, &track))
          return 1;

      } while(track != ff->input_ctx_.video_track);

      if(nestegg_packet_count(ff->input_ctx_.pkt, &ff->input_ctx_.chunks))
        return 1;
      ff->input_ctx_.chunk = 0;
    }

    if(nestegg_packet_data(ff->input_ctx_.pkt, ff->input_ctx_.chunk, &tmp_pkt,
                           &buf_sz))
      return 1;
    ff->input_ctx_.chunk++;
    *frame_size = buf_sz;
    memcpy((void*)buffer, tmp_pkt, *frame_size);

    return 0;
  }
#endif  /* WEBM_ENABLED */

  if(feof(fin)) {
    fprintf(stderr, "EOF: Input\n");
    return HANTRO_NOK;
  }

  if(*frame_size > max_buffer_size) {
    fprintf(stderr, "Frame size %d > buffer size %d\n",
            *frame_size, max_buffer_size );
    fseek(fin, *frame_size, SEEK_CUR);
    if (ff->format_ != FF_WEBP)
      *frame_size = (u32)(-1);
    else
      fseek(fin, pos, SEEK_SET);
    return HANTRO_NOK;
  }

  {
    size_t result = fread( (u8*)buffer, sizeof(u8), *frame_size, fin );

    if(result != *frame_size && pedantic_mode) {
      /* fread failed. */
      return HANTRO_NOK;
    }

  }

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    ReadIvfFileHeader

        Read IVF file header

------------------------------------------------------------------------------*/
static u32 ReadIvfFileHeader( FILE* fin ) {
  IVF_HEADER ivf;
  u32 tmp;

  tmp = fread( &ivf, sizeof(char), sizeof(IVF_HEADER), fin );
  if( tmp == 0 )
    return (HANTRO_NOK);

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    ReadIvfFrameHeader

        Read IVF frame header

------------------------------------------------------------------------------*/
static u32 ReadIvfFrameHeader( FILE* fin, u32 *frame_size ) {
  union {
    IVF_FRAME_HEADER ivf;
    u8 p[12];
  } fh;
  u32 tmp;

  tmp = fread( &fh, sizeof(char), sizeof(IVF_FRAME_HEADER), fin );
  if( tmp == 0 )
    return (HANTRO_NOK);

  *frame_size =
    fh.p[0] +
    (fh.p[1] << 8) +
    (fh.p[2] << 16) +
    (fh.p[3] << 24);

  return HANTRO_OK;
}

static BitstreamFormat FfCheckFormat(reader_inst inst) {
  u32 tmp;
  char id[5] = "DKIF";
  char id2[5] = "RIFF";
  char string[5];
  BitstreamFormat format = BITSTREAM_VP8;
  ffReader_t* ff = (ffReader_t*)inst;

#ifdef WEBM_ENABLED
  if (file_is_webm(&ff->input_ctx_, ff->file_, &tmp, &tmp, &tmp, &tmp, &tmp)) {
    ff->format_ = FF_WEBM;
    format = BITSTREAM_VP8;
    nestegg_track_seek(ff->input_ctx_.nestegg_ctx, ff->input_ctx_.video_track,
                       0);
  } else if (fread(string, 1, 5, ff->file_) == 5)
#else
  if (fread(string, 1, 5, ff->file_) == 5)
#endif
  {
    if (!strncmp(id, string, 5)) {
      ff->format_ = FF_VP8;
      format = BITSTREAM_VP8;
    } else if (!strncmp(id2, string, 4)) {
      ff->format_ = FF_WEBP;
      format = BITSTREAM_WEBP;
    } else {
      ff->format_ = FF_VP7;
      format = BITSTREAM_VP7;
    }
    rewind(ff->file_);
  }
  return format;
}

#ifdef WEBM_ENABLED
static int
nestegg_read_cb(void *buffer, size_t length, void *userdata) {
  FILE *f = userdata;

  if(fread(buffer, 1, length, f) < length) {
    if (ferror(f))
      return -1;
    if (feof(f))
      return 0;
  }
  return 1;
}

static int
nestegg_seek_cb(int64_t offset, int whence, void * userdata) {
  switch(whence) {
  case NESTEGG_SEEK_SET:
    whence = SEEK_SET;
    break;
  case NESTEGG_SEEK_CUR:
    whence = SEEK_CUR;
    break;
  case NESTEGG_SEEK_END:
    whence = SEEK_END;
    break;
  };
  return fseek(userdata, offset, whence)? -1 : 0;
}

static int64_t
nestegg_tell_cb(void * userdata) {
  return ftell(userdata);
}

static void
nestegg_log_cb(nestegg * context, unsigned int severity, char const * format,
               ...) {
  va_list ap;

  va_start(ap, format);
  vfprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

static int file_is_webm(input_ctx_t *input, FILE* infile, unsigned int* fourcc,
                        unsigned int* width, unsigned int* height,
                        unsigned int *fps_den, unsigned int *fps_num) {
  unsigned int i, n;
  int          track_type = -1;
  int64_t      file_size = 0;

  nestegg_io io = {nestegg_read_cb, nestegg_seek_cb, nestegg_tell_cb,
                   infile
                  };
  nestegg_video_params params;

  /* Get the file size for nestegg. */
  fseek(infile, 0, SEEK_END);
  file_size = ftell(infile);
  fseek(infile, 0, SEEK_SET);

  if(nestegg_init(&input->nestegg_ctx, io, NULL, file_size))
    goto fail;

  if(nestegg_track_count(input->nestegg_ctx, &n))
    goto fail;

  for(i=0; i<n; i++) {
    track_type = nestegg_track_type(input->nestegg_ctx, i);

    if(track_type == NESTEGG_TRACK_VIDEO)
      break;
    else if(track_type < 0)
      goto fail;
  }

  if(nestegg_track_codec_id(input->nestegg_ctx, i) != NESTEGG_CODEC_VP8) {
    fprintf(stderr, "Not VP8 video, quitting.\n");
    exit(1);
  }

  input->video_track = i;

  if(nestegg_track_video_params(input->nestegg_ctx, i, &params))
    goto fail;

  *fps_den = 0;
  *fps_num = 0;
  *fourcc = VP8_FOURCC;
  *width = params.width;
  *height = params.height;
  return 1;
fail:
  input->nestegg_ctx = NULL;
  rewind(infile);
  return 0;
}
#endif  /* WEBM_ENABLED */

