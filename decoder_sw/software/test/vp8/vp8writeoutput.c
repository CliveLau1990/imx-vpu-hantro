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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vp8writeoutput.h"
#include "testparams.h"
#include "md5.h"

typedef struct output_s {
  u8 *frame_pic_;
  test_params *params;
  FILE *file_;
} output_t;

static void FramePicture( u8 *p_in, u8* p_ch, i32 in_width, i32 in_height,
                          i32 in_frame_width, i32 in_frame_height,
                          u8 *p_out, i32 out_width, i32 out_height,
                          u32 luma_stride, u32 chroma_stride );

output_inst output_open(char* filename, test_params *params) {

  output_t* inst = calloc(1, sizeof(output_t));
  if (inst==NULL)
    return NULL;

  if(params==NULL)
    return NULL;

  inst->params = params;

  if(!params->disable_write_) {
    inst->file_ = fopen(filename, "wb");
    if (inst->file_ == NULL) {
      free(inst);
      return NULL;
    }
  } else {
    inst->file_ = NULL;
  }
  return inst;

}

void output_close(output_inst inst) {
  output_t* output = (output_t*)inst;
  if(output->file_ != NULL) {
    fclose(output->file_);
  }
  if(output->frame_pic_ != NULL) {
    free(output->frame_pic_);
  }

  free(output);
}

i32 output_write_pic(output_inst inst, unsigned char *buffer,
                     unsigned char *buffer_ch, i32 frame_width,
                     i32 frame_height, i32 cropped_width, i32 cropped_height, i32 planar,
                     i32 tiled_mode, u32 luma_stride, u32 chroma_stride, u32 pic_num) {
  output_t* output = (output_t *)inst;

  int luma_size = luma_stride * frame_height;
  int frame_size = luma_size + (chroma_stride*frame_height/2);
  static int pic_number = 0;
  int height_crop = 0;
  int include_strides  = 0;
  static struct MD5Context ctx;
  unsigned char digest[16];
  int i = 0;
  unsigned char *cb,*cr;
  int ret;
  unsigned char *output_l = buffer;
  unsigned char *output_ch = buffer_ch;
  int write_planar = planar;
  unsigned int stride_luma_local = luma_stride;
  unsigned int stride_chroma_local = chroma_stride;
  unsigned char *local_buffer = buffer;


  if(output->file_ == NULL)
    return 0;

  /* TODO(mheikkinen) TILED format */
  /* TODO(mheikkinen) DEC_X170_BIG_ENDIAN */
  if (output->params->num_of_decoded_pics_ <= pic_num &&
      output->params->num_of_decoded_pics_) {
    return 1;
  }


  if (output->params->frame_picture_) {
    if (output->frame_pic_ == NULL) {
      output->frame_pic_ =
        (u8*)malloc( frame_height * frame_width *3/2 * sizeof(u8));
    }
    FramePicture((u8*)buffer,
                 (u8*)buffer_ch,
                 cropped_width,
                 cropped_height,
                 frame_width,
                 frame_height,
                 output->frame_pic_, frame_width, frame_height,
                 output->params->luma_stride_,
                 output->params->chroma_stride_);
    output_l = output->frame_pic_;
    output_ch = NULL;
    write_planar = 1;
    stride_luma_local = stride_chroma_local = frame_width;
    luma_size = frame_width * frame_height;
    frame_size = luma_size * 3/2;
    local_buffer = output->frame_pic_;
  }

  if (output->params->md5_) {
    /* chroma should be right after luma */
    MD5Init(&ctx);
    MD5Update(&ctx, buffer, frame_size);
    MD5Final(digest, &ctx);

    for(i = 0; i < sizeof digest; i++) {
      fprintf(output->file_, "%02X", digest[i]);
    }
    fprintf(output->file_, "\n");

    return 0;
  } else {
    if (output_ch == NULL) {
      output_ch = output_l + luma_size;
    }

    if (!height_crop || (cropped_height == frame_height && cropped_width == frame_width)) {
      u32 i, j;
      u8 *buffer_tmp;
      buffer_tmp = local_buffer;

      for( i = 0 ; i < frame_height ; ++i ) {
        fwrite( buffer_tmp, include_strides ? stride_luma_local : frame_width, 1, output->file_);
        buffer_tmp += stride_luma_local;
      }

      if (!write_planar) {

        buffer_tmp = output_ch;
        for( i = 0 ; i < frame_height / 2 ; ++i ) {
          fwrite( buffer_tmp, include_strides ? stride_chroma_local : frame_width, 1, output->file_);
          buffer_tmp += stride_chroma_local;
        }
      } else {
        buffer_tmp = output_ch;
        for(i = 0; i < frame_height / 2; i++) {
          for( j = 0 ; j < (include_strides ? stride_chroma_local / 2 : frame_width / 2); ++j) {
            fwrite(buffer_tmp + j * 2, 1, 1, output->file_);
          }
          buffer_tmp += stride_chroma_local;
        }
        buffer_tmp = output_ch + 1;
        for(i = 0; i < frame_height / 2; i++) {
          for( j = 0 ; j < (include_strides ? stride_chroma_local / 2: frame_width / 2); ++j) {
            fwrite(buffer_tmp + j * 2, 1, 1, output->file_);
          }
          buffer_tmp += stride_chroma_local;
        }
      }
    } else {
      u32 row;
      for( row = 0 ; row < cropped_height ; row++) {
        fwrite(local_buffer + row*stride_luma_local, cropped_width, 1, output->file_);
      }
      if (!write_planar) {
        if(cropped_height &1)
          cropped_height++;
        if(cropped_width & 1)
          cropped_width++;
        for( row = 0 ; row < cropped_height/2 ; row++)
          fwrite(output_ch + row*stride_chroma_local, (cropped_width*2)/2, 1, output->file_);
      } else {
        u32 i, tmp;
        tmp = frame_width*cropped_height/4;

        if(cropped_height &1)
          cropped_height++;
        if(cropped_width & 1)
          cropped_width++;

        for( row = 0 ; row < cropped_height/2 ; ++row ) {
          for(i = 0; i < cropped_width/2; i++)
            fwrite(output_ch + row*stride_chroma_local + i * 2, 1, 1, output->file_);
        }
        for( row = 0 ; row < cropped_height/2 ; ++row ) {
          for(i = 0; i < cropped_width/2; i++)
            fwrite(output_ch + 1 + row*stride_chroma_local + i * 2, 1, 1, output->file_);
        }
      }
    }
  }

  return 0;

}

static void FramePicture( u8 *p_in, u8* p_ch, i32 in_width, i32 in_height,
                          i32 in_frame_width, i32 in_frame_height,
                          u8 *p_out, i32 out_width, i32 out_height,
                          u32 luma_stride, u32 chroma_stride ) {

  /* Variables */

  i32 x, y;

  /* Code */
  memset( p_out, 0, out_width*out_height );
  memset( p_out+out_width*out_height, 128, out_width*out_height/2 );

  /* Luma */
  for ( y = 0 ; y < in_height ; ++y ) {
    for( x = 0 ; x < in_width; ++x )
      *p_out++ = *p_in++;
    p_in += ( luma_stride - in_width );
    p_out += ( out_width - in_width );
  }

  p_out += out_width * ( out_height - in_height );
  if(p_ch)
    p_in = p_ch;
  else
    p_in += luma_stride * ( in_frame_height - in_height );

  in_frame_height /= 2;
  in_frame_width /= 2;
  out_height /= 2;
  out_width /= 2;
  in_height /= 2;
  in_width /= 2;
  /* Chroma */
  for ( y = 0 ; y < in_height ; ++y ) {
    for( x = 0 ; x < 2*in_width; ++x )
      *p_out++ = *p_in++;
    p_in += 2 * ( (chroma_stride/2) - in_width );
    p_out += 2 * ( out_width - in_width );
  }
}
