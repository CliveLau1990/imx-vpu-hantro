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

#include "dwl.h"
#include "testparams.h"
#include "vp8bufferalloc.h"
#include "vp8decapi.h"

typedef struct useralloc_s {
  struct DWLLinearMem user_alloc_luma[16];
  struct DWLLinearMem user_alloc_chroma[16];
  VP8DecPictureBufferProperties pbp;
  test_params* params;
} useralloc_t;

useralloc_inst useralloc_open(test_params* params) {
  useralloc_t* inst = calloc(1, sizeof(useralloc_t));
  if (inst==NULL)
    return NULL;

  if(params==NULL)
    return NULL;

  inst->params = params;

  return inst;
}

void useralloc_close(useralloc_inst inst) {
  useralloc_t* useralloc = (useralloc_t*)inst;
  if(useralloc)
    free(useralloc);
}



void useralloc_free(useralloc_inst inst,
                    VP8DecInst dec_inst,
                    void *dwl) {
  useralloc_t* ualloc = (useralloc_t*)inst;
  i32 i;

  for( i = 0 ; i < 16 ; ++i ) {
    if (ualloc->user_alloc_luma[i].virtual_address) {
      DWLFreeRefFrm(dwl,
                    &ualloc->user_alloc_luma[i]);
    }
    if (ualloc->user_alloc_chroma[i].virtual_address) {
      DWLFreeRefFrm(dwl,
                    &ualloc->user_alloc_chroma[i]);
    }
  }
}


i32 useralloc_alloc(useralloc_inst inst,
                    VP8DecInst dec_inst,
                    void *dwl) {
  useralloc_t* ualloc = (useralloc_t*)inst;

  u32 size_luma;
  u32 size_chroma;
  u32 width_y, width_c, i;
  VP8DecInfo info;
  VP8DecRet ret;
  VP8DecPictureBufferProperties* pbp = &ualloc->pbp;
  test_params* params = ualloc->params;

  ret = VP8DecGetInfo(dec_inst, &info);
  if (ret != VP8DEC_OK) {
    return ret;
  }

  pbp->luma_stride = params->luma_stride_;
  pbp->chroma_stride = params->chroma_stride_;
  pbp->num_buffers = params->num_frame_buffers_;

  width_y = pbp->luma_stride ? pbp->luma_stride : info.frame_width;
  width_c = pbp->chroma_stride ? pbp->chroma_stride : info.frame_width;

  useralloc_free(ualloc, dec_inst, dwl);

  size_luma = info.frame_height * width_y;
  size_chroma = info.frame_height * width_c / 2;

  u32 *p_pic_buffer_y[16];
  u32 *p_pic_buffer_c[16];
  addr_t pic_buffer_bus_address_y[16];
  addr_t pic_buffer_bus_address_c[16];
  if( pbp->num_buffers < 5 )
    pbp->num_buffers = 5;

  /* TODO(mheikkinen) Alloc space for ref status. */

  /* Custom use case: interleaved buffers (strides must
   * meet strict requirements here). If met, only one or
   * two buffers will be allocated, into which all ref
   * pictures' data will be interleaved into. */
  if(params->interleaved_buffers_) {
    u32 size_buffer;
    /* Mode 1: luma / chroma strides same; both can be interleaved */
    if( ((pbp->luma_stride == pbp->chroma_stride) ||
         ((2*pbp->luma_stride) == pbp->chroma_stride)) &&
        pbp->luma_stride >= info.frame_width*2*pbp->num_buffers) {
      size_buffer = pbp->luma_stride * (info.frame_height+1);
      if (DWLMallocRefFrm(dwl,
                          size_buffer, &ualloc->user_alloc_luma[0]) != DWL_OK) {
        fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
        return -1;
      }

      for( i = 0 ; i < pbp->num_buffers ; ++i ) {
        p_pic_buffer_y[i] = ualloc->user_alloc_luma[0].virtual_address +
                            (info.frame_width*2*i)/4;
        pic_buffer_bus_address_y[i] = ualloc->user_alloc_luma[0].bus_address +
                                      info.frame_width*2*i;
        p_pic_buffer_c[i] = ualloc->user_alloc_luma[0].virtual_address +
                            (info.frame_width*(2*i+1))/4;
        pic_buffer_bus_address_c[i] = ualloc->user_alloc_luma[0].bus_address +
                                      info.frame_width*(2*i+1);
      }

    } else { /* Mode 2: separate buffers for luma and chroma */
      if( (pbp->luma_stride < info.frame_width*pbp->num_buffers) ||
          (pbp->chroma_stride < info.frame_width*pbp->num_buffers)) {
        fprintf(stderr, "CHROMA STRIDE LENGTH TOO SMALL FOR INTERLEAVED FRAME BUFFERS\n");
        return -1;
      }

      size_buffer = pbp->luma_stride * (info.frame_height+1);
      if (DWLMallocRefFrm(dwl,
                          size_buffer,
                          &ualloc->user_alloc_luma[0]) != DWL_OK) {
        fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
        return -1;
      }
      size_buffer = pbp->chroma_stride * (info.frame_height+1);
      if (DWLMallocRefFrm(dwl,
                          size_buffer,
                          &ualloc->user_alloc_chroma[0]) != DWL_OK) {
        fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
        return -1;
      }
      for( i = 0 ; i < pbp->num_buffers ; ++i ) {
        p_pic_buffer_y[i] = ualloc->user_alloc_luma[0].virtual_address +
                            (info.frame_width*i)/4;
        pic_buffer_bus_address_y[i] = ualloc->user_alloc_luma[0].bus_address +
                                      info.frame_width*i;
        p_pic_buffer_c[i] = ualloc->user_alloc_chroma[0].virtual_address +
                            (info.frame_width*i)/4;
        pic_buffer_bus_address_c[i] = ualloc->user_alloc_chroma[0].bus_address +
                                      info.frame_width*i;
      }
    }
  } else { /* dedicated buffers */
    if(params->user_allocated_buffers_ == VP8DEC_EXTERNAL_ALLOC_ALT) {
      /* alloc all lumas first and only then chromas */
      for( i = 0 ; i < pbp->num_buffers ; ++i ) {
        if (DWLMallocRefFrm(dwl,
                            size_luma,
                            &ualloc->user_alloc_luma[i]) != DWL_OK) {
          fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
          return -1;
        }
        p_pic_buffer_y[i] = ualloc->user_alloc_luma[i].virtual_address;
        pic_buffer_bus_address_y[i] = ualloc->user_alloc_luma[i].bus_address;
      }
      for( i = 0 ; i < pbp->num_buffers ; ++i ) {
        if (DWLMallocRefFrm(dwl,
                            size_chroma+16,
                            &ualloc->user_alloc_chroma[i]) != DWL_OK) {
          fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
          return -1;
        }
        p_pic_buffer_c[i] = ualloc->user_alloc_chroma[i].virtual_address;
        pic_buffer_bus_address_c[i] = ualloc->user_alloc_chroma[i].bus_address;
      }
    } else {

      for( i = 0 ; i < pbp->num_buffers ; ++i ) {
        if (DWLMallocRefFrm(dwl,
                            size_luma, &ualloc->user_alloc_luma[i]) != DWL_OK) {
          fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
          return -1;
        }
        if (DWLMallocRefFrm(dwl,
                            size_chroma+16, &ualloc->user_alloc_chroma[i]) != DWL_OK) {
          fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
          return -1;
        }
        p_pic_buffer_y[i] = ualloc->user_alloc_luma[i].virtual_address;
        pic_buffer_bus_address_y[i] = ualloc->user_alloc_luma[i].bus_address;
        p_pic_buffer_c[i] = ualloc->user_alloc_chroma[i].virtual_address;
        pic_buffer_bus_address_c[i] = ualloc->user_alloc_chroma[i].bus_address;
      }
    }
  }
  pbp->p_pic_buffer_y = p_pic_buffer_y;
  pbp->pic_buffer_bus_address_y = pic_buffer_bus_address_y;
  pbp->p_pic_buffer_c = p_pic_buffer_c;
  pbp->pic_buffer_bus_address_c = pic_buffer_bus_address_c;

  if( VP8DecSetPictureBuffers( dec_inst, pbp ) != VP8DEC_OK ) {
    fprintf(stderr, "ERROR IN SETUP OF CUSTOM FRAME BUFFERS\n");
    return -1;
  }

  return 0;
}
