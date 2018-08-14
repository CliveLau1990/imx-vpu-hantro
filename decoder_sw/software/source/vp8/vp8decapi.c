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
#include "decapicommon.h"
#include "vp8decapi.h"
#include "vp8decmc_internals.h"

#include "version.h"

#include "dwl.h"
#include "vp8hwd_buffer_queue.h"
#include "vp8hwd_container.h"

#include "vp8hwd_debug.h"
#include "tiledref.h"

#include "vp8hwd_asic.h"

#include "regdrv_g1.h"
#include "refbuffer.h"

#include "vp8hwd_asic.h"
#include "vp8hwd_headers.h"

#include "errorhandling.h"

#define VP8DEC_MAJOR_VERSION 1
#define VP8DEC_MINOR_VERSION 0

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#include <stdio.h>
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

#ifdef VP8DEC_TRACE
#define DEC_API_TRC(str)    VP8DecTrace(str)
#else
#define DEC_API_TRC(str)    do{}while(0)
#endif

#define MB_MULTIPLE(x)  (((x)+15)&~15)

#define VP8_MIN_WIDTH  48
#define VP8_MIN_HEIGHT 48
#define VP8_MIN_WIDTH_EN_DTRC  96
#define VP8_MIN_HEIGHT_EN_DTRC 48
#define MAX_PIC_SIZE   4096*4096

#define EOS_MARKER (-1)
#define FLUSH_MARKER (-2)

extern void vp8hwdPreparePpRun(VP8DecContainer_t *dec_cont);
static u32 vp8hwdCheckSupport( VP8DecContainer_t *dec_cont );
static void vp8hwdFreeze(VP8DecContainer_t *dec_cont);
static u32 CheckBitstreamWorkaround(vp8_decoder_t* dec);
#if 0
static void DoBitstreamWorkaround(vp8_decoder_t* dec, DecAsicBuffers_t *p_asic_buff, vpBoolCoder_t*bc);
#endif
void vp8hwdErrorConceal(VP8DecContainer_t *dec_cont, addr_t bus_address,
                        u32 conceal_everything);
static struct DWLLinearMem* GetPrevRef(VP8DecContainer_t *dec_cont);
void ConcealRefAvailability(u32 * output, u32 height, u32 width);

static i32 FindIndex(VP8DecContainer_t* dec_cont, const u32* address);
#ifdef USE_OUTPUT_RELEASE
static VP8DecRet VP8DecNextPicture_INTERNAL(VP8DecInst dec_inst,
    VP8DecPicture * output, u32 end_of_stream);
static VP8DecRet VP8PushOutput(VP8DecContainer_t* dec_cont);
#endif

#ifdef USE_EXTERNAL_BUFFER
static void VP8SetExternalBufferInfo(VP8DecInst dec_inst);
#endif

#if defined(USE_EXTERNAL_BUFFER) && defined(USE_OUTPUT_RELEASE)
static void VP8EnterAbortState(VP8DecContainer_t* dec_cont);
static void VP8ExistAbortState(VP8DecContainer_t* dec_cont);
static void VP8EmptyBufferQueue(VP8DecContainer_t* dec_cont);
#endif

extern void VP8HwdBufferQueueSetAbort(BufferQueue queue);
extern void VP8HwdBufferQueueClearAbort(BufferQueue queue);
extern void VP8HwdBufferQueueEmptyRef(BufferQueue queue, i32 buffer);


/*------------------------------------------------------------------------------
    Function name : VP8DecGetAPIVersion
    Description   : Return the API version information

    Return type   : VP8DecApiVersion
    Argument      : void
------------------------------------------------------------------------------*/
VP8DecApiVersion VP8DecGetAPIVersion(void) {
  VP8DecApiVersion ver;

  ver.major = VP8DEC_MAJOR_VERSION;
  ver.minor = VP8DEC_MINOR_VERSION;

  DEC_API_TRC("VP8DecGetAPIVersion# OK\n");

  return ver;
}

/*------------------------------------------------------------------------------
    Function name : VP8DecGetBuild
    Description   : Return the SW and HW build information

    Return type   : VP8DecBuild
    Argument      : void
------------------------------------------------------------------------------*/
VP8DecBuild VP8DecGetBuild(void) {
  VP8DecBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_VP8_DEC);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_VP8_DEC);

  DEC_API_TRC("VP8DecGetBuild# OK\n");

  return build_info;
}

/*------------------------------------------------------------------------------
    Function name   : vp8decinit
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst * dec_inst
                      enum DecErrorHandling error_handling
------------------------------------------------------------------------------*/
VP8DecRet VP8DecInit(VP8DecInst * dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                     const void *dwl,
#endif
                     VP8DecFormat dec_format,
                     enum DecErrorHandling error_handling,
                     u32 num_frame_buffers,
                     enum DecDpbFlags dpb_flags,
                     u32 use_adaptive_buffers,
                     u32 n_guard_size ) {
  VP8DecContainer_t *dec_cont;
#ifndef USE_EXTERNAL_BUFFER
  const void *dwl;
#endif
  u32 i;
  u32 reference_frame_format;
#ifndef USE_EXTERNAL_BUFFER
  struct DWLInitParam dwl_init;
#endif
  DWLHwConfig config;

  DEC_API_TRC("VP8DecInit#\n");

  /* check that right shift on negative numbers is performed signed */
  /*lint -save -e* following check causes multiple lint messages */
#if (((-1) >> 1) != (-1))
#error Right bit-shifting (>>) does not preserve the sign
#endif
  /*lint -restore */

  if(dec_inst == NULL) {
    DEC_API_TRC("VP8DecInit# ERROR: dec_inst == NULL");
    return (VP8DEC_PARAM_ERROR);
  }

  *dec_inst = NULL;   /* return NULL instance for any error */

  /* check that decoding supported in HW */
  {

    DWLHwConfig hw_cfg;

    DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_VP8_DEC);
    if(dec_format == VP8DEC_VP7 && !hw_cfg.vp7_support) {
      DEC_API_TRC("VP8DecInit# ERROR: VP7 not supported in HW\n");
      return VP8DEC_FORMAT_NOT_SUPPORTED;
    }

    if((dec_format == VP8DEC_VP8 || dec_format == VP8DEC_WEBP) &&
        !hw_cfg.vp8_support) {
      DEC_API_TRC("VP8DecInit# ERROR: VP8 not supported in HW\n");
      return VP8DEC_FORMAT_NOT_SUPPORTED;
    }

  }

#ifndef USE_EXTERNAL_BUFFER
  /* init DWL for the specified client */
  dwl_init.client_type = DWL_CLIENT_TYPE_VP8_DEC;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    DEC_API_TRC("VP8DecInit# ERROR: DWL Init failed\n");
    return (VP8DEC_DWL_ERROR);
  }
#endif
  /* allocate instance */
  dec_cont = (VP8DecContainer_t *) DWLmalloc(sizeof(VP8DecContainer_t));

  if(dec_cont == NULL) {
    DEC_API_TRC("VP8DecInit# ERROR: Memory allocation failed\n");
#ifndef USE_EXTERNAL_BUFFER
    (void)DWLRelease(dwl);
#endif
    return VP8DEC_MEMFAIL;
  }

  (void) DWLmemset(dec_cont, 0, sizeof(VP8DecContainer_t));
  dec_cont->dwl = dwl;

  pthread_mutex_init(&dec_cont->protect_mutex, NULL);
  /* initial setup of instance */

  dec_cont->dec_stat = VP8DEC_INITIALIZED;
  dec_cont->checksum = dec_cont;  /* save instance as a checksum */

  if( num_frame_buffers > VP8DEC_MAX_PIC_BUFFERS)
    num_frame_buffers = VP8DEC_MAX_PIC_BUFFERS;
  switch(dec_format) {
  case VP8DEC_VP7:
    dec_cont->dec_mode = dec_cont->decoder.dec_mode = VP8HWD_VP7;
    if(num_frame_buffers < 3)
      num_frame_buffers = 3;
    break;
  case VP8DEC_VP8:
    dec_cont->dec_mode = dec_cont->decoder.dec_mode = VP8HWD_VP8;
    if(num_frame_buffers < 4)
      num_frame_buffers = 4;
    break;
  case VP8DEC_WEBP:
    dec_cont->dec_mode = dec_cont->decoder.dec_mode = VP8HWD_VP8;
    dec_cont->intra_only = HANTRO_TRUE;
    num_frame_buffers = 1;
    break;
  }
  dec_cont->num_buffers = num_frame_buffers;
  dec_cont->num_buffers_reserved = num_frame_buffers;
  VP8HwdAsicInit(dec_cont);   /* Init ASIC */

  if(!DWLReadAsicCoreCount()) {
    return (VP8DEC_DWL_ERROR);
  }
  dec_cont->num_cores = 1;

  (void)DWLmemset(&config, 0, sizeof(DWLHwConfig));

  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_VP8_DEC);

  if(!config.addr64_support && sizeof(void *) == 8) {
    DEC_API_TRC("VP8DecInit# ERROR: HW not support 64bit address!\n");
    return (VP8DEC_PARAM_ERROR);
  }

  i = DWLReadAsicID(DWL_CLIENT_TYPE_VP8_DEC) >> 16;
  if(i == 0x8170U)
    error_handling = 0;
  dec_cont->ref_buf_support = config.ref_buf_support;
  reference_frame_format = dpb_flags & DEC_REF_FRM_FMT_MASK;
  if(reference_frame_format == DEC_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!config.tiled_mode_support) {
      DEC_API_TRC("VP8DecInit# ERROR: Tiled reference picture format not supported in HW\n");
      DWLfree(dec_cont);
#ifndef USE_EXTERNAL_BUFFER
      (void)DWLRelease(dwl);
#endif
      return VP8DEC_FORMAT_NOT_SUPPORTED;
    }
    dec_cont->tiled_mode_support = config.tiled_mode_support;
  } else
    dec_cont->tiled_mode_support = 0;
  dec_cont->intra_freeze = error_handling == DEC_EC_VIDEO_FREEZE;
  if (error_handling == DEC_EC_PARTIAL_FREEZE)
    dec_cont->partial_freeze = 1;
  else if (error_handling == DEC_EC_PARTIAL_IGNORE)
    dec_cont->partial_freeze = 2;
  dec_cont->picture_broken = 0;
  dec_cont->decoder.refbu_pred_hits = 0;

  if (!error_handling && dec_format == VP8DEC_VP8)
    dec_cont->hw_ec_support = config.ec_support;

  if ((dec_format == VP8DEC_VP8) || (dec_format == VP8DEC_WEBP))
    dec_cont->stride_support = config.stride_support;
#ifdef USE_OUTPUT_RELEASE
  if (FifoInit(VP8DEC_MAX_PIC_BUFFERS, &dec_cont->fifo_out) != FIFO_OK)
    return VP8DEC_MEMFAIL;
#endif
#ifdef USE_EXTERNAL_BUFFER
  dec_cont->no_reallocation = 1;
#endif

  dec_cont->use_adaptive_buffers = use_adaptive_buffers;
  dec_cont->n_guard_size = n_guard_size;

  /* If tile mode is enabled, should take DTRC minimum size(96x48) into consideration */
  if (dec_cont->tiled_mode_support) {
    dec_cont->min_dec_pic_width = VP8_MIN_WIDTH_EN_DTRC;
    dec_cont->min_dec_pic_height = VP8_MIN_HEIGHT_EN_DTRC;
  }
  else {
    dec_cont->min_dec_pic_width = VP8_MIN_WIDTH;
    dec_cont->min_dec_pic_height = VP8_MIN_HEIGHT;
  }

  /* return new instance to application */
  *dec_inst = (VP8DecInst) dec_cont;

  DEC_API_TRC("VP8DecInit# OK\n");
  return (VP8DEC_OK);

}

/*------------------------------------------------------------------------------
    Function name   : VP8DecRelease
    Description     :
    Return type     : void
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
void VP8DecRelease(VP8DecInst dec_inst) {

  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;
  const void *dwl;

  DEC_API_TRC("VP8DecRelease#\n");

  if(dec_cont == NULL) {
    DEC_API_TRC("VP8DecRelease# ERROR: dec_inst == NULL\n");
    return;
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecRelease# ERROR: Decoder not initialized\n");
    return;
  }

  /* PP instance must be already disconnected at this point */
  ASSERT(dec_cont->pp.pp_instance == NULL);

  dwl = dec_cont->dwl;

  pthread_mutex_destroy(&dec_cont->protect_mutex);

  if(dec_cont->asic_running) {
    DWLDisableHw(dwl, dec_cont->core_id, 1 * 4, 0);    /* stop HW */
    DWLReleaseHw(dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  VP8HwdAsicReleaseMem(dec_cont);
  VP8HwdAsicReleasePictures(dec_cont);
  if (dec_cont->hw_ec_support)
    vp8hwdReleaseEc(&dec_cont->ec);

  if (dec_cont->fifo_out)
    FifoRelease(dec_cont->fifo_out);

  dec_cont->checksum = NULL;
  DWLfree(dec_cont);

  {
#ifndef USE_EXTERNAL_BUFFER
    i32 dwlret = DWLRelease(dwl);

    ASSERT(dwlret == DWL_OK);
    (void) dwlret;
#endif
  }

  DEC_API_TRC("VP8DecRelease# OK\n");

  return;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecGetInfo
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
    Argument        : VP8DecInfo * dec_info
------------------------------------------------------------------------------*/
VP8DecRet VP8DecGetInfo(VP8DecInst dec_inst, VP8DecInfo * dec_info) {
  const VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;

  DEC_API_TRC("VP8DecGetInfo#");

  if(dec_inst == NULL || dec_info == NULL) {
    DEC_API_TRC("VP8DecGetInfo# ERROR: dec_inst or dec_info is NULL\n");
    return VP8DEC_PARAM_ERROR;
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecGetInfo# ERROR: Decoder not initialized\n");
    return VP8DEC_NOT_INITIALIZED;
  }

  if (dec_cont->dec_stat == VP8DEC_INITIALIZED) {
    return VP8DEC_HDRS_NOT_RDY;
  }

  dec_info->vp_version = dec_cont->decoder.vp_version;
  dec_info->vp_profile = dec_cont->decoder.vp_profile;
#ifdef USE_EXTERNAL_BUFFER
  dec_info->pic_buff_size = dec_cont->buf_num;
#endif

  if(dec_cont->tiled_mode_support) {
    dec_info->output_format = VP8DEC_TILED_YUV420;
  } else {
    dec_info->output_format = VP8DEC_SEMIPLANAR_YUV420;
  }

  /* Fragments have 8 pixels */
  dec_info->coded_width = dec_cont->decoder.width;
  dec_info->coded_height = dec_cont->decoder.height;
  dec_info->frame_width = (dec_cont->decoder.width + 15) & ~15;
  dec_info->frame_height = (dec_cont->decoder.height + 15) & ~15;
  dec_info->scaled_width = dec_cont->decoder.scaled_width;
  dec_info->scaled_height = dec_cont->decoder.scaled_height;
  dec_info->dpb_mode = DEC_DPB_FRAME;

  return VP8DEC_OK;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecDecode
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
    Argument        : const VP8DecInput * input
    Argument        : VP8DecFrame * output
------------------------------------------------------------------------------*/
VP8DecRet VP8DecDecode(VP8DecInst dec_inst,
                       const VP8DecInput * input, VP8DecOutput * output) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff;
  i32 ret;
  u32 asic_status;
  u32 error_concealment = 0;

  DEC_API_TRC("VP8DecDecode#\n");

  /* Check that function input parameters are valid */
  if(input == NULL || output == NULL || dec_inst == NULL) {
    DEC_API_TRC("VP8DecDecode# ERROR: NULL arg(s)\n");
    return (VP8DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecDecode# ERROR: Decoder not initialized\n");
    return (VP8DEC_NOT_INITIALIZED);
  }

  DWLmemset(output, 0, sizeof(VP8DecOutput));

#ifdef USE_OUTPUT_RELEASE
  if (dec_cont->abort) {
    return (VP8DEC_ABORTED);
  }
#endif

  if(((input->data_len > DEC_X170_MAX_STREAM) && !dec_cont->intra_only) ||
      X170_CHECK_VIRTUAL_ADDRESS(input->stream) ||
      X170_CHECK_BUS_ADDRESS(input->stream_bus_address)) {
    DEC_API_TRC("VP8DecDecode# ERROR: Invalid arg value\n");
    return VP8DEC_PARAM_ERROR;
  }

  if ((input->p_pic_buffer_y != NULL && input->pic_buffer_bus_address_y == 0) ||
      (input->p_pic_buffer_y == NULL && input->pic_buffer_bus_address_y != 0) ||
      (input->p_pic_buffer_c != NULL && input->pic_buffer_bus_address_c == 0) ||
      (input->p_pic_buffer_c == NULL && input->pic_buffer_bus_address_c != 0) ||
      (input->p_pic_buffer_y == NULL && input->p_pic_buffer_c != 0) ||
      (input->p_pic_buffer_y != NULL && input->p_pic_buffer_c == 0)) {
    DEC_API_TRC("VP8DecDecode# ERROR: Invalid arg value\n");
    return VP8DEC_PARAM_ERROR;
  }

#ifdef VP8DEC_EVALUATION
  if(dec_cont->pic_number > VP8DEC_EVALUATION) {
    DEC_API_TRC("VP8DecDecode# VP8DEC_EVALUATION_LIMIT_EXCEEDED\n");
    return VP8DEC_EVALUATION_LIMIT_EXCEEDED;
  }
#endif

  if(!input->data_len && dec_cont->stream_consumed_callback ) {
    dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
    return VP8DEC_OK;
  }
  /* aliases */
  p_asic_buff = dec_cont->asic_buff;

  if (dec_cont->no_decoding_buffer || dec_cont->get_buffer_after_abort) {
    p_asic_buff->out_buffer_i = VP8HwdBufferQueueGetBuffer(dec_cont->bq);
#ifdef USE_OUTPUT_RELEASE
    if(p_asic_buff->out_buffer_i == 0xFFFFFFFF) {
      if (dec_cont->abort)
        return VP8DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
      else {
        output->data_left = input->data_len;
        dec_cont->no_decoding_buffer = 1;
        return VP8DEC_NO_DECODING_BUFFER;
      }
#endif
    }
    p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
#endif
    p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
    p_asic_buff->decode_id[p_asic_buff->prev_out_buffer_i] = input->pic_id;
    dec_cont->no_decoding_buffer = 0;

    if (dec_cont->get_buffer_after_abort) {
      dec_cont->get_buffer_after_abort = 0;
      VP8HwdBufferQueueUpdateRef(dec_cont->bq,
          BQUEUE_FLAG_PREV | BQUEUE_FLAG_GOLDEN | BQUEUE_FLAG_ALT,
          p_asic_buff->out_buffer_i);

      if(dec_cont->intra_only != HANTRO_TRUE) {
        VP8HwdBufferQueueAddRef(dec_cont->bq, VP8HwdBufferQueueGetPrevRef(dec_cont->bq));
        VP8HwdBufferQueueAddRef(dec_cont->bq, VP8HwdBufferQueueGetAltRef(dec_cont->bq));
        VP8HwdBufferQueueAddRef(dec_cont->bq, VP8HwdBufferQueueGetGoldenRef(dec_cont->bq));
      }
    }

    if(dec_cont->pp.pp_instance == NULL) {
      if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
        return VP8DEC_ABORTED;
    }
  }

  if((!dec_cont->intra_only) && input->slice_height ) {
    DEC_API_TRC("VP8DecDecode# ERROR: Invalid arg value\n");
    return VP8DEC_PARAM_ERROR;
  }
#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->intra_only) {
    while(dec_cont->asic_buff->not_displayed[p_asic_buff->out_buffer_i])
      sched_yield();
  }
#endif

  /* application indicates that slice mode decoding should be used ->
   * disabled unless WebP and PP not used */
  if (dec_cont->dec_stat != VP8DEC_MIDDLE_OF_PIC &&
      dec_cont->intra_only && input->slice_height &&
      dec_cont->pp.pp_instance == NULL) {
    DWLHwConfig hw_config;
    u32 tmp;
    /* Slice mode can only be enabled if image width is larger
     * than supported video decoder maximum width. */
    DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_VP8_DEC);
    if( input->data_len >= 5 ) {
      /* Peek frame width. We make shortcuts and assumptions:
       *  -always keyframe
       *  -always VP8 (i.e. not VP7)
       *  -keyframe start code and frame tag skipped
       *  -if keyframe start code invalid, handle it later */
      tmp = (input->stream[7] << 8)|
            (input->stream[6]); /* Read 16-bit chunk */
      tmp = tmp & 0x3fff;
      if( tmp > hw_config.max_dec_pic_width ) {
        if(input->slice_height > 255) {
          DEC_API_TRC("VP8DecDecode# ERROR: Slice height > max\n");
          return VP8DEC_PARAM_ERROR;
        }

        dec_cont->slice_height = input->slice_height;
      } else {
        dec_cont->slice_height = 0;
      }
    } else {
      /* Too little data in buffer, let later error management
       * handle it. Disallow slice mode. */
    }
  }

  if (dec_cont->intra_only && input->p_pic_buffer_y) {
    dec_cont->user_mem = 1;
    dec_cont->asic_buff->user_mem.p_pic_buffer_y[0] = input->p_pic_buffer_y;
    dec_cont->asic_buff->user_mem.pic_buffer_bus_addr_y[0] =
      input->pic_buffer_bus_address_y;
    dec_cont->asic_buff->user_mem.p_pic_buffer_c[0] = input->p_pic_buffer_c;
    dec_cont->asic_buff->user_mem.pic_buffer_bus_addr_c[0] =
      input->pic_buffer_bus_address_c;
  }

  if (dec_cont->dec_stat == VP8DEC_NEW_HEADERS) {
    /* if picture size > 16mpix, slice mode mandatory */
    if((dec_cont->asic_buff->width*
        dec_cont->asic_buff->height > WEBP_MAX_PIXEL_AMOUNT_NONSLICE) &&
        dec_cont->intra_only &&
        (dec_cont->slice_height == 0) &&
        (dec_cont->pp.pp_instance == NULL) ) {
      if(dec_cont->stream_consumed_callback != NULL) {
        dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
      }
      return VP8DEC_STREAM_NOT_SUPPORTED;
    }

    if((ret = VP8HwdAsicAllocatePictures(dec_cont)) != 0 ||
        (dec_cont->hw_ec_support &&
         vp8hwdInitEc(&dec_cont->ec, dec_cont->width, dec_cont->height, 16))) {
      if(dec_cont->stream_consumed_callback != NULL) {
        dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
      }
      DEC_API_TRC
      ("VP8DecDecode# ERROR: Picture memory allocation failed\n");
#ifdef USE_OUTPUT_RELEASE
      if (ret == -2)
        return VP8DEC_ABORTED;
      else
#endif
        return VP8DEC_MEMFAIL;
    }

    if(VP8HwdAsicAllocateMem(dec_cont) != 0) {
      if(dec_cont->stream_consumed_callback != NULL) {
        dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
      }
      DEC_API_TRC("VP8DecInit# ERROR: ASIC Memory allocation failed\n");
      return VP8DEC_MEMFAIL;
    }
#ifndef USE_EXTERNAL_BUFFER
    dec_cont->dec_stat = VP8DEC_DECODING;
#else
    if(dec_cont->user_mem ||
        (dec_cont->intra_only && dec_cont->pp.pp_info.pipeline_accepted) ||
        ((!dec_cont->user_mem &&
          !(dec_cont->intra_only && dec_cont->pp.pp_info.pipeline_accepted)) &&
         p_asic_buff->custom_buffers))
      dec_cont->dec_stat = VP8DEC_DECODING;
    else {
      if(dec_cont->no_reallocation)
        dec_cont->dec_stat = VP8DEC_DECODING;
      else {
        dec_cont->no_reallocation = 1;
        dec_cont->dec_stat = VP8DEC_WAITING_BUFFER;
        dec_cont->buffer_index = 0;
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->abort)
          return(VP8DEC_ABORTED);
        else
#endif
          return VP8DEC_WAITING_FOR_BUFFER;
      }
    }
#endif
  }
#ifdef USE_EXTERNAL_BUFFER
  else if (dec_cont->dec_stat == VP8DEC_WAITING_BUFFER) {
    dec_cont->dec_stat = VP8DEC_DECODING;
  }
#endif
  else if (dec_cont->dec_stat != VP8DEC_MIDDLE_OF_PIC && input->data_len) {
    dec_cont->prev_is_key = dec_cont->decoder.key_frame;
    dec_cont->decoder.probs_decoded = 0;

    /* decode frame tag */
    vp8hwdDecodeFrameTag( input->stream, &dec_cont->decoder );

    /* When on key-frame, reset probabilities and such */
    if( dec_cont->decoder.key_frame ) {
      vp8hwdResetDecoder( &dec_cont->decoder);
    }
    /* intra only and non key-frame */
    else if (dec_cont->intra_only) {
      if(dec_cont->stream_consumed_callback != NULL) {
        dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
      }
      return VP8DEC_STRM_ERROR;
    }

    if (dec_cont->decoder.key_frame || dec_cont->decoder.vp_version > 0) {
      p_asic_buff->dc_pred[0] = p_asic_buff->dc_pred[1] =
                                  p_asic_buff->dc_match[0] = p_asic_buff->dc_match[1] = 0;
    }

    /* Decode frame header (now starts bool coder as well) */
    ret = vp8hwdDecodeFrameHeader(
            input->stream + dec_cont->decoder.frame_tag_size,
            input->data_len - dec_cont->decoder.frame_tag_size,
            &dec_cont->bc, &dec_cont->decoder );
    if( ret != HANTRO_OK ) {
      if(dec_cont->stream_consumed_callback != NULL) {
        dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
      }
      DEC_API_TRC("VP8DecDecode# ERROR: Frame header decoding failed\n");
      if (!dec_cont->pic_number || dec_cont->dec_stat != VP8DEC_DECODING) {
        return VP8DEC_STRM_ERROR;
      } else {
        vp8hwdFreeze(dec_cont);
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp.pp_instance == NULL) {
          if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
            return VP8DEC_ABORTED;
        }
#endif
        DEC_API_TRC("VP8DecDecode# VP8DEC_PIC_DECODED\n");
        return VP8DEC_PIC_DECODED;
      }
    }
    /* flag the stream as non "error-resilient" */
    else if (dec_cont->decoder.refresh_entropy_probs)
      dec_cont->prob_refresh_detected = 1;

    if(CheckBitstreamWorkaround(&dec_cont->decoder)) {
      /* do bitstream workaround */
      /*DoBitstreamWorkaround(&dec_cont->decoder, p_asic_buff, &dec_cont->bc);*/
    }

    ret = vp8hwdSetPartitionOffsets(input->stream, input->data_len,
                                    &dec_cont->decoder);
    /* ignore errors in partition offsets if HW error concealment used
     * (assuming parts of stream missing -> partition start offsets may
     * be larger than amount of stream in the buffer) */
    if (ret != HANTRO_OK && !dec_cont->hw_ec_support) {
      if(dec_cont->stream_consumed_callback != NULL) {
        dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
      }
      if (!dec_cont->pic_number || dec_cont->dec_stat != VP8DEC_DECODING) {
        return VP8DEC_STRM_ERROR;
      } else {
        vp8hwdFreeze(dec_cont);
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp.pp_instance == NULL) {
          if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
            return VP8DEC_ABORTED;
        }
#endif
        DEC_API_TRC("VP8DecDecode# VP8DEC_PIC_DECODED\n");
        return VP8DEC_PIC_DECODED;
      }
    }

    /* check for picture size change */
    if((dec_cont->width != (dec_cont->decoder.width)) ||
        (dec_cont->height != (dec_cont->decoder.height))) {
#ifdef USE_EXTERNAL_BUFFER
      if ((!dec_cont->use_adaptive_buffers &&
           (dec_cont->decoder.width * dec_cont->decoder.height >
            dec_cont->width * dec_cont->height)) ||
          (dec_cont->use_adaptive_buffers &&
           ((dec_cont->decoder.width * dec_cont->decoder.height >
             dec_cont->n_ext_buf_size) ||
            (dec_cont->num_buffers_reserved + dec_cont->n_guard_size > dec_cont->tot_buffers))))
        dec_cont->no_reallocation = 0;

      if (dec_cont->width == 0 && dec_cont->height == 0)
        dec_cont->no_reallocation = 0;
#endif

      if (dec_cont->stream_consumed_callback != NULL && dec_cont->bq) {
        VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                   dec_cont->asic_buff->out_buffer_i);
        dec_cont->asic_buff->out_buffer_i = VP8_UNDEFINED_BUFFER;
        VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                   VP8HwdBufferQueueGetPrevRef(dec_cont->bq));
        VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                   VP8HwdBufferQueueGetGoldenRef(dec_cont->bq));
        VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                   VP8HwdBufferQueueGetAltRef(dec_cont->bq));
        /* Wait for output processing to finish before releasing. */
        VP8HwdBufferQueueWaitPending(dec_cont->bq);
      }

      /* reallocate picture buffers */
      p_asic_buff->width = ( dec_cont->decoder.width + 15 ) & ~15;
      p_asic_buff->height = ( dec_cont->decoder.height + 15 ) & ~15;

#ifdef USE_EXTERNAL_BUFFER
      if(dec_cont->no_reallocation == 0) {
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp.pp_instance == NULL) {
#ifndef USE_EXT_BUF_SAFE_RELEASE
          VP8HwdBufferQueueMarkNotInUse(dec_cont->bq);
#else
          VP8HwdBufferQueueWaitNotInUse(dec_cont->bq);
#endif
        }
#endif
        VP8HwdAsicReleasePictures(dec_cont);
      } else {
        if(dec_cont->stream_consumed_callback == NULL &&
            dec_cont->bq && dec_cont->intra_only != HANTRO_TRUE) {
          i32 index;
          /* Legacy single Core: remove references made for the next decode. */
          if (( index = VP8HwdBufferQueueGetPrevRef(dec_cont->bq)) != (i32)REFERENCE_NOT_SET)
            VP8HwdBufferQueueRemoveRef(dec_cont->bq, index);
          if (( index = VP8HwdBufferQueueGetAltRef(dec_cont->bq)) != (i32)REFERENCE_NOT_SET)
            VP8HwdBufferQueueRemoveRef(dec_cont->bq, index);
          if (( index = VP8HwdBufferQueueGetGoldenRef(dec_cont->bq)) != (i32)REFERENCE_NOT_SET)
            VP8HwdBufferQueueRemoveRef(dec_cont->bq, index);
          VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                     dec_cont->asic_buff->out_buffer_i);
        }

        if(p_asic_buff->mvs[0].virtual_address != NULL)
          DWLFreeLinear(dec_cont->dwl, &p_asic_buff->mvs[0]);

        if(p_asic_buff->mvs[1].virtual_address != NULL)
          DWLFreeLinear(dec_cont->dwl, &p_asic_buff->mvs[1]);
      }
#else
      VP8HwdAsicReleasePictures(dec_cont);
#endif
      VP8HwdAsicReleaseMem(dec_cont);

      if (dec_cont->hw_ec_support)
        vp8hwdReleaseEc(&dec_cont->ec);

      if (vp8hwdCheckSupport(dec_cont) != HANTRO_OK) {
        if(dec_cont->stream_consumed_callback != NULL) {
          dec_cont->stream_consumed_callback((u8*)input->stream, input->p_user_data);
        }
        dec_cont->dec_stat = VP8DEC_INITIALIZED;
        return VP8DEC_STREAM_NOT_SUPPORTED;
      }

      dec_cont->width = dec_cont->decoder.width;
      dec_cont->height = dec_cont->decoder.height;

      dec_cont->dec_stat = VP8DEC_NEW_HEADERS;

      if( dec_cont->ref_buf_support && !dec_cont->intra_only ) {
        RefbuInit( &dec_cont->ref_buffer_ctrl, 10,
                   MB_MULTIPLE(dec_cont->decoder.width)>>4,
                   MB_MULTIPLE(dec_cont->decoder.height)>>4,
                   dec_cont->ref_buf_support);
      }

      DEC_API_TRC("VP8DecDecode# VP8DEC_HDRS_RDY\n");
#ifdef USE_EXTERNAL_BUFFER
      VP8SetExternalBufferInfo(dec_cont);
      if(dec_cont->no_reallocation) {
        output->data_left = input->data_len;
        return VP8DEC_STRM_PROCESSED;
      } else
#endif
      {
#ifdef USE_OUTPUT_RELEASE
        FifoPush(dec_cont->fifo_out, FLUSH_MARKER, FIFO_EXCEPTION_DISABLE);
        if(dec_cont->abort)
          return(VP8DEC_ABORTED);
        else
#endif
          return VP8DEC_HDRS_RDY;
      }
    }

    /* If we are here and dimensions are still 0, it means that we have
     * yet to decode a valid keyframe, in which case we must give up. */
    if( dec_cont->width == 0 || dec_cont->height == 0 ) {
      return VP8DEC_STRM_PROCESSED;
    }

    /* If output picture is broken and we are not decoding a base frame,
     * don't even start HW, just output same picture again. */
    if( !dec_cont->decoder.key_frame &&
        dec_cont->picture_broken &&
        (dec_cont->intra_freeze || dec_cont->force_intra_freeze) ) {
      vp8hwdFreeze(dec_cont);
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp.pp_instance == NULL) {
        if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
          return VP8DEC_ABORTED;
      }
#endif
      DEC_API_TRC("VP8DecDecode# VP8DEC_PIC_DECODED\n");
      return VP8DEC_PIC_DECODED;
    }

  }
  /* missing picture, conceal */
  else if (!input->data_len) {
    if (!dec_cont->hw_ec_support || dec_cont->force_intra_freeze ||
        dec_cont->prev_is_key) {
      dec_cont->decoder.probs_decoded = 0;
      vp8hwdFreeze(dec_cont);
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp.pp_instance == NULL) {
        if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
          return VP8DEC_ABORTED;
      }
#endif
      DEC_API_TRC("VP8DecDecode# VP8DEC_PIC_DECODED\n");
      return VP8DEC_PIC_DECODED;
    } else {
      dec_cont->conceal_start_mb_x = dec_cont->conceal_start_mb_y = 0;
      vp8hwdErrorConceal(dec_cont, input->stream_bus_address,
                         /*conceal_everything*/ 1);
      /* Assume that broken picture updated the last reference only and
       * also addref it since it will be outputted one more time. */
      VP8HwdBufferQueueAddRef(dec_cont->bq, p_asic_buff->out_buffer_i);
      VP8HwdBufferQueueUpdateRef(dec_cont->bq, BQUEUE_FLAG_PREV,
                                 p_asic_buff->out_buffer_i);
      p_asic_buff->prev_out_buffer = p_asic_buff->out_buffer;
      p_asic_buff->prev_out_buffer_i = p_asic_buff->out_buffer_i;
      p_asic_buff->out_buffer = NULL;

      dec_cont->pic_number++;
      dec_cont->out_count++;
      if (dec_cont->prob_refresh_detected) {
        dec_cont->picture_broken = 1;
        dec_cont->force_intra_freeze = 1;
      }

#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp.pp_instance == NULL) {
        if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
          return VP8DEC_ABORTED;
      }
#endif

      p_asic_buff->out_buffer_i = VP8HwdBufferQueueGetBuffer(dec_cont->bq);
#ifdef USE_OUTPUT_RELEASE
      if(p_asic_buff->out_buffer_i == 0xFFFFFFFF) {
        if (dec_cont->abort)
          return VP8DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
        else {
          output->data_left = 0;
          dec_cont->no_decoding_buffer = 1;
          return VP8DEC_PIC_DECODED;
          //return VP8DEC_NO_DECODING_BUFFER;
        }
#endif
      }
      p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
#endif
      p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
      p_asic_buff->decode_id[p_asic_buff->prev_out_buffer_i] = input->pic_id;
#if 0
      dec_cont->pic_number++;
      dec_cont->out_count++;
      ASSERT(p_asic_buff->out_buffer != NULL);
      if (dec_cont->prob_refresh_detected) {
        dec_cont->picture_broken = 1;
        dec_cont->force_intra_freeze = 1;
      }

#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp.pp_instance == NULL) {
        if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
          return VP8DEC_ABORTED;
      }
#endif
#endif
      return VP8DEC_PIC_DECODED;
    }
  }

  if (dec_cont->dec_stat != VP8DEC_MIDDLE_OF_PIC) {
    dec_cont->ref_to_out = 0;

    if(dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.dec_pp_if.use_pipeline) {
      /* reserved both DEC and PP hardware for pipeline */
      ret = DWLReserveHwPipe(dec_cont->dwl, &dec_cont->core_id);
    } else {
      ret = DWLReserveHw(dec_cont->dwl, &dec_cont->core_id);
    }

    if(ret != DWL_OK) {
      ERROR_PRINT("DWLReserveHw Failed");
      return VP8HWDEC_HW_RESERVED;
    }

    /* prepare asic */

    VP8HwdAsicProbUpdate(dec_cont);

    VP8HwdSegmentMapUpdate(dec_cont);

    VP8HwdAsicInitPicture(dec_cont);

    VP8HwdAsicStrmPosUpdate(dec_cont, input->stream_bus_address);

    /* Store the needed data for callback setup. */
    /* TODO(vmr): Consider parametrizing this. */
    dec_cont->stream = input->stream;
    dec_cont->p_user_data = input->p_user_data;

    /* PP setup stuff */
    vp8hwdPreparePpRun(dec_cont);
  } else
    VP8HwdAsicContPicture(dec_cont);

  if (dec_cont->partial_freeze) {
    PreparePartialFreeze((u8*)p_asic_buff->out_buffer->virtual_address,
                         (dec_cont->width >> 4),(dec_cont->height >> 4));
  }

  /* run the hardware */
  asic_status = VP8HwdAsicRun(dec_cont);

  /* Rollback entropy probabilities if refresh is not set */
  if(dec_cont->decoder.refresh_entropy_probs == HANTRO_FALSE) {
    DWLmemcpy( &dec_cont->decoder.entropy, &dec_cont->decoder.entropy_last,
               sizeof(vp8EntropyProbs_t));
    DWLmemcpy( dec_cont->decoder.vp7_scan_order, dec_cont->decoder.vp7_prev_scan_order,
               sizeof(dec_cont->decoder.vp7_scan_order));
  }
  /* If in asynchronous mode, just return OK  */
  if (asic_status == VP8HWDEC_ASYNC_MODE) {
    /* find first free buffer and use it as next output */
    if (!error_concealment || dec_cont->intra_only) {
      p_asic_buff->prev_out_buffer = p_asic_buff->out_buffer;
      p_asic_buff->prev_out_buffer_i = p_asic_buff->out_buffer_i;
      p_asic_buff->out_buffer = NULL;

      /* If we are never going to output the current buffer, we can release
      * the ref for output on the buffer. */
      if (!dec_cont->decoder.show_frame)
        VP8HwdBufferQueueRemoveRef(dec_cont->bq, p_asic_buff->out_buffer_i);
      /* If WebP, we will be recycling only one buffer and no update is
       * necessary. */
      if (!dec_cont->intra_only) {
        p_asic_buff->out_buffer_i = VP8HwdBufferQueueGetBuffer(dec_cont->bq);
#ifdef USE_OUTPUT_RELEASE
        if(p_asic_buff->out_buffer_i == 0xFFFFFFFF) {
          if ( dec_cont->abort)
            return VP8DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
          else {
            output->data_left = 0;
            dec_cont->no_decoding_buffer = 1;
            return VP8DEC_OK;
          }
#endif
        }
        p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
#endif
      }
      p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
      p_asic_buff->decode_id[p_asic_buff->prev_out_buffer_i] = input->pic_id;
      ASSERT(p_asic_buff->out_buffer != NULL);
    }
    return VP8DEC_OK;
  }

  /* Handle system error situations */
  if(asic_status == VP8HWDEC_SYSTEM_TIMEOUT) {
    /* This timeout is DWL(software/os) generated */
    DEC_API_TRC("VP8DecDecode# VP8DEC_HW_TIMEOUT, SW generated\n");
    return VP8DEC_HW_TIMEOUT;
  } else if(asic_status == VP8HWDEC_SYSTEM_ERROR) {
    DEC_API_TRC("VP8DecDecode# VP8HWDEC_SYSTEM_ERROR\n");
    return VP8DEC_SYSTEM_ERROR;
  } else if(asic_status == VP8HWDEC_HW_RESERVED) {
    DEC_API_TRC("VP8DecDecode# VP8HWDEC_HW_RESERVED\n");
    return VP8DEC_HW_RESERVED;
  }

  /* Handle possible common HW error situations */
  if(asic_status & DEC_8190_IRQ_BUS) {
    DEC_API_TRC("VP8DecDecode# VP8DEC_HW_BUS_ERROR\n");
    return VP8DEC_HW_BUS_ERROR;
  }

  /* for all the rest we will output a picture (concealed or not) */
  if((asic_status & DEC_8190_IRQ_TIMEOUT) ||
      (asic_status & DEC_8190_IRQ_ERROR) ||
      (asic_status & DEC_8190_IRQ_ASO) || /* to signal lost residual */
      (asic_status & DEC_8190_IRQ_ABORT)) {
    u32 conceal_everything = 0;

    if (!dec_cont->partial_freeze ||
        !ProcessPartialFreeze((u8*)p_asic_buff->out_buffer->virtual_address,
                              (u8*)GetPrevRef(dec_cont)->virtual_address,
                              (dec_cont->width >> 4),
                              (dec_cont->height >> 4),
                              dec_cont->partial_freeze == 1)) {
      /* This timeout is HW generated */
      if(asic_status & DEC_8190_IRQ_TIMEOUT) {
#ifdef VP8HWTIMEOUT_ASSERT
        ASSERT(0);
#endif
        DEBUG_PRINT(("IRQ: HW TIMEOUT\n"));
      } else {
        DEBUG_PRINT(("IRQ: STREAM ERROR\n"));
      }

      /* keyframe -> all mbs concealed */
      if (dec_cont->decoder.key_frame ||
          (asic_status & DEC_8190_IRQ_TIMEOUT) ||
          (asic_status & DEC_8190_IRQ_ABORT)) {
        dec_cont->conceal_start_mb_x = 0;
        dec_cont->conceal_start_mb_y = 0;
        conceal_everything = 1;
      } else {
        /* concealment start point read from sw/hw registers */
        dec_cont->conceal_start_mb_x =
          GetDecRegister(dec_cont->vp8_regs, HWIF_STARTMB_X);
        dec_cont->conceal_start_mb_y =
          GetDecRegister(dec_cont->vp8_regs, HWIF_STARTMB_Y);
        /* error in control partition -> conceal all mbs from start point
         * onwards, otherwise only intra mbs get concealed */
        conceal_everything = !(asic_status & DEC_8190_IRQ_ASO);
      }

      if (dec_cont->slice_height && dec_cont->intra_only) {
        dec_cont->slice_concealment = 1;
      }

      /* PP has to run again for the concealed picture */
      if(dec_cont->pp.pp_instance != NULL && dec_cont->pp.dec_pp_if.use_pipeline) {
        /* concealed current, i.e. last  ref to PP */
        TRACE_PP_CTRL
        ("VP8DecDecode: Concealed picture, PP should run again\n");
        dec_cont->pp.dec_pp_if.pp_status = DECPP_RUNNING;
      }

      /* HW error concealment not used if
       * 1) previous frame was key frame (no ref mvs available) AND
       * 2) whole control partition corrupted (no current mvs available) */
      if (dec_cont->hw_ec_support &&
          (!dec_cont->prev_is_key || !conceal_everything ||
           dec_cont->conceal_start_mb_y || dec_cont->conceal_start_mb_x))
        vp8hwdErrorConceal(dec_cont, input->stream_bus_address,
                           conceal_everything);
      else /* normal picture freeze */
        error_concealment = 1;
    } else {
      asic_status &= ~DEC_8190_IRQ_ERROR;
      asic_status &= ~DEC_8190_IRQ_TIMEOUT;
      asic_status &= ~DEC_8190_IRQ_ASO;
      asic_status |= DEC_8190_IRQ_RDY;
      error_concealment = 0;
    }
  } else if(asic_status & DEC_8190_IRQ_RDY) {
  } else if (asic_status & DEC_8190_IRQ_SLICE) {
  } else {
    ASSERT(0);
  }

  if(asic_status & DEC_8190_IRQ_RDY) {
    DEBUG_PRINT(("IRQ: PICTURE RDY\n"));

    if (dec_cont->decoder.key_frame) {
      dec_cont->picture_broken = 0;
      dec_cont->force_intra_freeze = 0;
    }

    if (dec_cont->slice_height) {
      dec_cont->output_rows = p_asic_buff->height -  dec_cont->tot_decoded_rows*16;
      /* Below code not needed; slice mode always disables loop-filter -->
       * output 16 rows multiple */
      /*
      if (dec_cont->tot_decoded_rows)
          dec_cont->output_rows += 8;
          */
#ifdef USE_OUTPUT_RELEASE
      dec_cont->last_slice = 1;
      if(dec_cont->pp.pp_instance == NULL) {
        if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
          return VP8DEC_ABORTED;
      }
#endif
      return VP8DEC_PIC_DECODED;
    }

  } else if (asic_status & DEC_8190_IRQ_SLICE) {
    dec_cont->dec_stat = VP8DEC_MIDDLE_OF_PIC;

    dec_cont->output_rows = dec_cont->slice_height * 16;
    /* Below code not needed; slice mode always disables loop-filter -->
     * output 16 rows multiple */
    /*if (!dec_cont->tot_decoded_rows)
        dec_cont->output_rows -= 8;*/

    dec_cont->tot_decoded_rows += dec_cont->slice_height;

#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->pp.pp_instance == NULL) {
      if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
        return VP8DEC_ABORTED;
    }
#endif
    return VP8DEC_SLICE_RDY;
  }

  if(dec_cont->intra_only != HANTRO_TRUE)
    VP8HwdUpdateRefs(dec_cont, error_concealment);

  if (dec_cont->decoder.show_frame)
    dec_cont->out_count++;

  /* find first free buffer and use it as next output */
  if (!error_concealment || dec_cont->intra_only) {

    p_asic_buff->prev_out_buffer = p_asic_buff->out_buffer;
    p_asic_buff->prev_out_buffer_i = p_asic_buff->out_buffer_i;
    p_asic_buff->out_buffer = NULL;

    /* If WebP, we will be recycling only one buffer and no update is
     * necessary. */
    if (!dec_cont->intra_only) {
#ifdef USE_OUTPUT_RELEASE
      if (dec_cont->pp.pp_instance == NULL && dec_cont->decoder.show_frame) {
        if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
          return VP8DEC_ABORTED;
      }
#endif
      VP8HwdBufferQueueRemoveRef(dec_cont->bq, p_asic_buff->out_buffer_i);
      p_asic_buff->out_buffer_i = VP8HwdBufferQueueGetBuffer(dec_cont->bq);
#ifdef USE_OUTPUT_RELEASE
      if(p_asic_buff->out_buffer_i == 0xFFFFFFFF) {
        if (dec_cont->abort)
          return VP8DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
        else {
          output->data_left = 0;
          dec_cont->no_decoding_buffer = 1;
          return VP8DEC_PIC_DECODED;
          //return VP8DEC_NO_DECODING_BUFFER;
        }
#endif
      }
      p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
#endif
    }

    p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
    p_asic_buff->decode_id[p_asic_buff->prev_out_buffer_i] = input->pic_id;
    ASSERT(p_asic_buff->out_buffer != NULL);
  } else {
    dec_cont->ref_to_out = 1;
    dec_cont->picture_broken = 1;
    if (!dec_cont->pic_number) {
      (void) DWLmemset( GetPrevRef(dec_cont)->virtual_address, 128,
                        p_asic_buff->width * p_asic_buff->height * 3 / 2);
    }
  }

  dec_cont->pic_number++;

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp.pp_instance == NULL) {
    if (VP8PushOutput(dec_cont) == VP8DEC_ABORTED)
      return VP8DEC_ABORTED;
  }
#endif

  DEC_API_TRC("VP8DecDecode# VP8DEC_PIC_DECODED\n");
  return VP8DEC_PIC_DECODED;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecNextPicture
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
    Argument        : VP8DecPicture * output
    Argument        : u32 end_of_stream
------------------------------------------------------------------------------*/
VP8DecRet VP8DecNextPicture(VP8DecInst dec_inst,
                            VP8DecPicture * output, u32 end_of_stream) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 pic_for_output = 0;
  i32 buff_id;
  i32 ret;

  DEC_API_TRC("VP8DecNextPicture#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP8DecNextPicture# ERROR: dec_inst or output is NULL\n");
    return (VP8DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecNextPicture# ERROR: Decoder not initialized\n");
    return (VP8DEC_NOT_INITIALIZED);
  }
#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp.pp_instance == NULL) {
    /*  NextOutput will block until there is an output. */
    addr_t i;
    if ((ret = FifoPop(dec_cont->fifo_out, &i,
#ifdef GET_OUTPUT_BUFFER_NON_BLOCK
          FIFO_EXCEPTION_ENABLE
#else
          FIFO_EXCEPTION_DISABLE
#endif
          )) != FIFO_ABORT) {

#ifdef GET_OUTPUT_BUFFER_NON_BLOCK
      if (ret == FIFO_EMPTY) return VP8DEC_OK;
#endif

      if ((i32)i == EOS_MARKER) {
        DEC_API_TRC("VP8DecNextPicture# VP8DEC_END_OF_STREAM\n");
        return VP8DEC_END_OF_STREAM;
      }
      if ((i32)i == FLUSH_MARKER) {
        DEC_API_TRC("VP8DecNextPicture# VP8DEC_FLUSHED\n");
        return VP8DEC_FLUSHED;
      }

      *output = dec_cont->asic_buff->picture_info[i];

      DEC_API_TRC("VP8DecNextPicture# VP8DEC_PIC_RDY\n");
      return VP8DEC_PIC_RDY;
    } else
      return VP8DEC_ABORTED;
  }
#endif

  if (!dec_cont->out_count && !dec_cont->output_rows)
    return VP8DEC_OK;

  if(dec_cont->slice_concealment)
    return (VP8DEC_OK);

  /* slice for output */
  if (dec_cont->output_rows) {
    output->num_slice_rows = dec_cont->output_rows;

    if (dec_cont->user_mem) {
      output->p_output_frame = p_asic_buff->user_mem.p_pic_buffer_y[0];
      output->p_output_frame_c = p_asic_buff->user_mem.p_pic_buffer_c[0];
      output->output_frame_bus_address =
        p_asic_buff->user_mem.pic_buffer_bus_addr_y[0];
      output->output_frame_bus_address_c =
        p_asic_buff->user_mem.pic_buffer_bus_addr_c[0];
    } else {
      u32 offset = 16 * (dec_cont->slice_height + 1) * p_asic_buff->width;
      output->p_output_frame = p_asic_buff->pictures[0].virtual_address;
      output->output_frame_bus_address =
        p_asic_buff->pictures[0].bus_address;

      if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
        output->p_output_frame_c = p_asic_buff->pictures_c[0].virtual_address;
        output->output_frame_bus_address_c =
          p_asic_buff->pictures_c[0].bus_address;
      } else {
        output->p_output_frame_c =
          (u32*)((addr_t)output->p_output_frame + offset);
        output->output_frame_bus_address_c =
          output->output_frame_bus_address + offset;
      }
    }

    output->pic_id = 0;
    output->decode_id = p_asic_buff->picture_info[0].decode_id;
    output->is_intra_frame = dec_cont->decoder.key_frame;
    output->is_golden_frame = 0;
    output->nbr_of_err_mbs = 0;
    output->frame_width = (dec_cont->width + 15) & ~15;
    output->frame_height = (dec_cont->height + 15) & ~15;
    output->coded_width = dec_cont->width;
    output->coded_height = dec_cont->height;
    output->luma_stride = p_asic_buff->luma_stride ?
                          p_asic_buff->luma_stride : p_asic_buff->width;
    output->chroma_stride = p_asic_buff->chroma_stride ?
                            p_asic_buff->chroma_stride : p_asic_buff->width;

    output->output_format = dec_cont->tiled_reference_enable ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

    dec_cont->output_rows = 0;

    return (VP8DEC_PIC_RDY);
  }

  output->num_slice_rows = 0;

  if (!dec_cont->pp.pp_instance || dec_cont->pp.dec_pp_if.pp_status != DECPP_IDLE)
    pic_for_output = 1;
  else if (!dec_cont->decoder.refresh_last || end_of_stream) {
    pic_for_output = 1;
    dec_cont->pp.dec_pp_if.pp_status = DECPP_RUNNING;
  } else if (dec_cont->pp.pp_instance && dec_cont->pending_pic_to_pp) {
    pic_for_output = 1;
    dec_cont->pp.dec_pp_if.pp_status = DECPP_RUNNING;
  }

  /* TODO: What if intra_freeze and not pipelined pp? */
  if (dec_cont->pp.pp_instance != NULL &&
      dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
    DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;
    TRACE_PP_CTRL("VP8DecNextPicture: PP has to run\n");

    dec_pp_if->use_pipeline = 0;

    dec_pp_if->inwidth = MB_MULTIPLE(dec_cont->width);
    dec_pp_if->inheight = MB_MULTIPLE(dec_cont->height);
    dec_pp_if->cropped_w = dec_pp_if->inwidth;
    dec_pp_if->cropped_h = dec_pp_if->inheight;

    dec_pp_if->luma_stride = p_asic_buff->luma_stride ?
                             p_asic_buff->luma_stride : p_asic_buff->width;
    dec_pp_if->chroma_stride = p_asic_buff->chroma_stride ?
                               p_asic_buff->chroma_stride : p_asic_buff->width;

    /* forward tiled mode */
    dec_pp_if->tiled_input_mode = dec_cont->tiled_reference_enable;
    dec_pp_if->progressive_sequence = 1;

    dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
    if (dec_cont->user_mem) {
      dec_pp_if->input_bus_luma =
        p_asic_buff->user_mem.pic_buffer_bus_addr_y[0];
      dec_pp_if->input_bus_chroma =
        p_asic_buff->user_mem.pic_buffer_bus_addr_c[0];
    } else {
      if (dec_cont->decoder.refresh_last || dec_cont->ref_to_out) {
        dec_pp_if->input_bus_luma = GetPrevRef(dec_cont)->bus_address;
        dec_pp_if->input_bus_chroma =
          p_asic_buff->pictures_c[
            VP8HwdBufferQueueGetPrevRef(dec_cont->bq)].bus_address;
      } else {
        dec_pp_if->input_bus_luma = p_asic_buff->prev_out_buffer->bus_address;
        dec_pp_if->input_bus_chroma =
          p_asic_buff->pictures_c[p_asic_buff->prev_out_buffer_i].bus_address;
      }

      if(!(p_asic_buff->strides_used || p_asic_buff->custom_buffers)) {
        dec_pp_if->input_bus_chroma = dec_pp_if->input_bus_luma +
                                      dec_pp_if->inwidth * dec_pp_if->inheight;
      }
    }

    output->output_frame_bus_address = dec_pp_if->input_bus_luma;

    dec_pp_if->little_endian =
      GetDecRegister(dec_cont->vp8_regs, HWIF_DEC_OUT_ENDIAN);
    dec_pp_if->word_swap =
      GetDecRegister(dec_cont->vp8_regs, HWIF_DEC_OUTSWAP32_E);

    dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);

    TRACE_PP_CTRL("VP8DecNextPicture: PP wait to be done\n");

    dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);
    dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

    TRACE_PP_CTRL("VP8DecNextPicture: PP Finished\n");
  }

  if (pic_for_output) {
    const struct DWLLinearMem *out_pic = NULL;
    const struct DWLLinearMem *out_pic_c = NULL;

    /* no pp -> current output (ref if concealed)
     * pipeline -> the same
     * pp stand-alone (non-reference frame) -> current output
     * pp stand-alone  */
    if ( (dec_cont->pp.pp_instance == NULL ||
          dec_cont->pp.dec_pp_if.use_pipeline ||
          (dec_cont->out_count == 1 &&
           !dec_cont->decoder.refresh_last) ) && !dec_cont->ref_to_out) {
      out_pic = p_asic_buff->prev_out_buffer;
      out_pic_c = &p_asic_buff->pictures_c[ p_asic_buff->prev_out_buffer_i ];
    } else {
      if (dec_cont->pp.pp_instance != NULL) {
        dec_cont->combined_mode_used = 1;
      }
      out_pic = GetPrevRef(dec_cont);
      out_pic_c = &p_asic_buff->pictures_c[
                    VP8HwdBufferQueueGetPrevRef(dec_cont->bq)];

    }

    dec_cont->out_count--;

    output->luma_stride = p_asic_buff->luma_stride ?
                          p_asic_buff->luma_stride : p_asic_buff->width;
    output->chroma_stride = p_asic_buff->chroma_stride ?
                            p_asic_buff->chroma_stride : p_asic_buff->width;

    if (dec_cont->user_mem) {
      output->p_output_frame = p_asic_buff->user_mem.p_pic_buffer_y[0];
      output->output_frame_bus_address =
        p_asic_buff->user_mem.pic_buffer_bus_addr_y[0];
      output->p_output_frame_c = p_asic_buff->user_mem.p_pic_buffer_c[0];
      output->output_frame_bus_address_c =
        p_asic_buff->user_mem.pic_buffer_bus_addr_c[0];

    } else {
      output->p_output_frame = out_pic->virtual_address;
      output->output_frame_bus_address = out_pic->bus_address;
      if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
        output->p_output_frame_c = out_pic_c->virtual_address;
        output->output_frame_bus_address_c = out_pic_c->bus_address;
      } else {
        u32 chroma_buf_offset = p_asic_buff->width * p_asic_buff->height;
        output->p_output_frame_c = output->p_output_frame +
                                   chroma_buf_offset / 4;
        output->output_frame_bus_address_c = output->output_frame_bus_address +
                                             chroma_buf_offset;
      }
    }
    output->pic_id = 0;
    buff_id = FindIndex(dec_cont, output->p_output_frame);
    output->decode_id = p_asic_buff->decode_id[buff_id];
    output->is_intra_frame = dec_cont->decoder.key_frame;
    output->is_golden_frame = 0;
    output->nbr_of_err_mbs = 0;

    output->frame_width = (dec_cont->width + 15) & ~15;
    output->frame_height = (dec_cont->height + 15) & ~15;
    output->coded_width = dec_cont->width;
    output->coded_height = dec_cont->height;
    output->output_format = dec_cont->tiled_reference_enable ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

    DEC_API_TRC("VP8DecNextPicture# VP8DEC_PIC_RDY\n");

    dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;

    return (VP8DEC_PIC_RDY);
  }

  DEC_API_TRC("VP8DecNextPicture# VP8DEC_OK\n");
  return (VP8DEC_OK);

}

#ifdef USE_OUTPUT_RELEASE
/*------------------------------------------------------------------------------
    Function name   : VP8DecNextPicture_INTERNAL
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
    Argument        : VP8DecPicture * output
    Argument        : u32 end_of_stream
------------------------------------------------------------------------------*/
VP8DecRet VP8DecNextPicture_INTERNAL(VP8DecInst dec_inst,
                                     VP8DecPicture * output, u32 end_of_stream) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 pic_for_output = 0;
  u32 buff_id;

  DEC_API_TRC("VP8DecNextPicture_INTERNAL#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP8DecNextPicture_INTERNAL# ERROR: dec_inst or output is NULL\n");
    return (VP8DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecNextPicture_INTERNAL# ERROR: Decoder not initialized\n");
    return (VP8DEC_NOT_INITIALIZED);
  }

  if (!dec_cont->out_count && !dec_cont->output_rows)
    return VP8DEC_OK;

  if(dec_cont->slice_concealment)
    return (VP8DEC_OK);

  /* slice for output */
  if (dec_cont->output_rows) {
    output->num_slice_rows = dec_cont->output_rows;
    output->last_slice = dec_cont->last_slice;

    if (dec_cont->user_mem) {
      output->p_output_frame = p_asic_buff->user_mem.p_pic_buffer_y[0];
      output->p_output_frame_c = p_asic_buff->user_mem.p_pic_buffer_c[0];
      output->output_frame_bus_address =
        p_asic_buff->user_mem.pic_buffer_bus_addr_y[0];
      output->output_frame_bus_address_c =
        p_asic_buff->user_mem.pic_buffer_bus_addr_c[0];
    } else {
      u32 offset = 16 * (dec_cont->slice_height + 1) * p_asic_buff->width;
      output->p_output_frame = p_asic_buff->pictures[0].virtual_address;
      output->output_frame_bus_address =
        p_asic_buff->pictures[0].bus_address;

      if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
        output->p_output_frame_c = p_asic_buff->pictures_c[0].virtual_address;
        output->output_frame_bus_address_c =
          p_asic_buff->pictures_c[0].bus_address;
      } else {
        output->p_output_frame_c =
          (u32*)((addr_t)output->p_output_frame + offset);
        output->output_frame_bus_address_c =
          output->output_frame_bus_address + offset;
      }
    }

    output->pic_id = 0;
    output->decode_id = p_asic_buff->picture_info[0].decode_id;
    output->is_intra_frame = dec_cont->decoder.key_frame;
    output->is_golden_frame = 0;
    output->nbr_of_err_mbs = 0;
    output->frame_width = (dec_cont->width + 15) & ~15;
    output->frame_height = (dec_cont->height + 15) & ~15;
    output->coded_width = dec_cont->width;
    output->coded_height = dec_cont->height;
    output->luma_stride = p_asic_buff->luma_stride ?
                          p_asic_buff->luma_stride : p_asic_buff->width;
    output->chroma_stride = p_asic_buff->chroma_stride ?
                            p_asic_buff->chroma_stride : p_asic_buff->width;

    output->output_format = dec_cont->tiled_reference_enable ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

    dec_cont->output_rows = 0;

    while(dec_cont->asic_buff->not_displayed[0])
      sched_yield();

    VP8HwdBufferQueueSetBufferAsUsed(dec_cont->bq, 0);
    dec_cont->asic_buff->not_displayed[0] = 1;
    dec_cont->asic_buff->picture_info[0] = *output;
    FifoPush(dec_cont->fifo_out, 0, FIFO_EXCEPTION_DISABLE);
    return (VP8DEC_PIC_RDY);
  }

  output->num_slice_rows = 0;

  if (!dec_cont->pp.pp_instance || dec_cont->pp.dec_pp_if.pp_status != DECPP_IDLE)
    pic_for_output = 1;
  else if (!dec_cont->decoder.refresh_last || end_of_stream) {
    pic_for_output = 1;
    dec_cont->pp.dec_pp_if.pp_status = DECPP_RUNNING;
  }

  if (pic_for_output) {
    const struct DWLLinearMem *out_pic = NULL;
    const struct DWLLinearMem *out_pic_c = NULL;

    /* no pp -> current output (ref if concealed)
     * pipeline -> the same
     * pp stand-alone (non-reference frame) -> current output
     * pp stand-alone  */
    if ( (dec_cont->pp.pp_instance == NULL ||
          dec_cont->pp.dec_pp_if.use_pipeline ||
          (dec_cont->out_count == 1 &&
           !dec_cont->decoder.refresh_last) ) && !dec_cont->ref_to_out) {
      out_pic = p_asic_buff->prev_out_buffer;
      out_pic_c = &p_asic_buff->pictures_c[ p_asic_buff->prev_out_buffer_i ];
    } else {
      out_pic = GetPrevRef(dec_cont);
      out_pic_c = &p_asic_buff->pictures_c[
                    VP8HwdBufferQueueGetPrevRef(dec_cont->bq)];

    }

    dec_cont->out_count--;

    output->luma_stride = p_asic_buff->luma_stride ?
                          p_asic_buff->luma_stride : p_asic_buff->width;
    output->chroma_stride = p_asic_buff->chroma_stride ?
                            p_asic_buff->chroma_stride : p_asic_buff->width;

    if (dec_cont->user_mem) {
      output->p_output_frame = p_asic_buff->user_mem.p_pic_buffer_y[0];
      output->output_frame_bus_address =
        p_asic_buff->user_mem.pic_buffer_bus_addr_y[0];
      output->p_output_frame_c = p_asic_buff->user_mem.p_pic_buffer_c[0];
      output->output_frame_bus_address_c =
        p_asic_buff->user_mem.pic_buffer_bus_addr_c[0];

    } else {
      output->p_output_frame = out_pic->virtual_address;
      output->output_frame_bus_address = out_pic->bus_address;
      if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
        output->p_output_frame_c = out_pic_c->virtual_address;
        output->output_frame_bus_address_c = out_pic_c->bus_address;
      } else {
        u32 chroma_buf_offset = p_asic_buff->width * p_asic_buff->height;
        output->p_output_frame_c = output->p_output_frame +
                                   chroma_buf_offset / 4;
        output->output_frame_bus_address_c = output->output_frame_bus_address +
                                             chroma_buf_offset;
      }
    }
    output->pic_id = 0;
    output->is_intra_frame = dec_cont->decoder.key_frame;
    output->is_golden_frame = 0;
    output->nbr_of_err_mbs = 0;

#if 0
    output->frame_width = (dec_cont->width + 15) & ~15;
    output->frame_height = (dec_cont->height + 15) & ~15;
    output->coded_width = dec_cont->width;
    output->coded_height = dec_cont->height;
#endif

    buff_id = FindIndex(dec_cont, output->p_output_frame);
    output->frame_width = dec_cont->asic_buff->frame_width[buff_id];
    output->frame_height = dec_cont->asic_buff->frame_height[buff_id];
    output->coded_width = dec_cont->asic_buff->coded_width[buff_id];
    output->coded_height = dec_cont->asic_buff->coded_height[buff_id];

    output->output_format = dec_cont->tiled_reference_enable ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

    DEC_API_TRC("VP8DecNextPicture_INTERNAL# VP8DEC_PIC_RDY\n");

    output->decode_id = p_asic_buff->decode_id[buff_id];

#ifdef USE_PICTURE_DISCARD
    if (dec_cont->asic_buff->first_show[buff_id])
#endif
    {
#ifndef USE_PICTURE_DISCARD
      if (VP8HwdBufferQueueWaitBufNotInUse(dec_cont->bq, buff_id) == HANTRO_NOK)
        return VP8DEC_ABORTED;
#endif
      dec_cont->asic_buff->not_displayed[buff_id] = 1;
      VP8HwdBufferQueueSetBufferAsUsed(dec_cont->bq, buff_id);
      dec_cont->asic_buff->picture_info[buff_id] = *output;
      FifoPush(dec_cont->fifo_out, buff_id, FIFO_EXCEPTION_DISABLE);
      dec_cont->asic_buff->first_show[buff_id] = 0;
    }

    return (VP8DEC_PIC_RDY);
  }

  DEC_API_TRC("VP8DecNextPicture_INTERNAL# VP8DEC_OK\n");
  return (VP8DEC_OK);

}


/*------------------------------------------------------------------------------
    Function name   : VP8DecPictureConsumed
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
VP8DecRet VP8DecPictureConsumed(VP8DecInst dec_inst,
                                const VP8DecPicture * picture) {
  u32 buffer_id;
  if (dec_inst == NULL || picture == NULL) {
    return VP8DEC_PARAM_ERROR;
  }

  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;
  buffer_id = FindIndex(dec_cont, picture->p_output_frame);

  /* Remove the reference to the buffer. */
  if(dec_cont->asic_buff->not_displayed[buffer_id]) {
    dec_cont->asic_buff->not_displayed[buffer_id] = 0;
    if(picture->num_slice_rows == 0 || picture->last_slice)
      VP8HwdBufferQueueReleaseBuffer(dec_cont->bq, buffer_id);
  }
  DEC_API_TRC("VP8DecPictureConsumed# VP8DEC_OK\n");
  return VP8DEC_OK;
}


/*------------------------------------------------------------------------------
    Function name   : VP8DecEndOfStream
    Description     : Used for signalling stream end from the input thread
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
VP8DecRet VP8DecEndOfStream(VP8DecInst dec_inst, u32 strm_end_flag) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;
  VP8DecPicture output;
  VP8DecRet ret;

  if (dec_inst == NULL) {
    return VP8DEC_PARAM_ERROR;
  }

  /* Don't do end of stream twice. This is not thread-safe, so it must be
   * called from the single input thread that is also used to call
   * VP8DecDecode. */
  pthread_mutex_lock(&dec_cont->protect_mutex);
  if (dec_cont->dec_stat == VP8DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return VP8DEC_END_OF_STREAM;
  }

  while((ret = VP8DecNextPicture_INTERNAL(dec_inst, &output, 1)) == VP8DEC_PIC_RDY);
  if(ret == VP8DEC_ABORTED) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (VP8DEC_ABORTED);
  }

  if(strm_end_flag) {
    dec_cont->dec_stat = VP8DEC_END_OF_STREAM;
    FifoPush(dec_cont->fifo_out, EOS_MARKER, FIFO_EXCEPTION_DISABLE);
  }
  pthread_mutex_unlock(&dec_cont->protect_mutex);
  return VP8DEC_OK;
}

#endif

u32 vp8hwdCheckSupport( VP8DecContainer_t *dec_cont ) {

  DWLHwConfig hw_config;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_VP8_DEC);

  if ( (dec_cont->asic_buff->width > hw_config.max_dec_pic_width) ||
       (dec_cont->asic_buff->width < dec_cont->min_dec_pic_width) ||
       (dec_cont->asic_buff->height < dec_cont->min_dec_pic_height) ||
       (dec_cont->asic_buff->width*dec_cont->asic_buff->height > MAX_PIC_SIZE) ) {
    /* check if webp support */
    if (dec_cont->intra_only && hw_config.webp_support &&
        dec_cont->asic_buff->width <= MAX_SNAPSHOT_WIDTH*16 &&
        dec_cont->asic_buff->height <= MAX_SNAPSHOT_HEIGHT*16) {
      return HANTRO_OK;
    } else {
      return HANTRO_NOK;
    }
  }

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecPeek
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
VP8DecRet VP8DecPeek(VP8DecInst dec_inst, VP8DecPicture * output) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 buff_id;
  const struct DWLLinearMem *out_pic = NULL;
  const struct DWLLinearMem *out_pic_c = NULL;

  DEC_API_TRC("VP8DecPeek#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP8DecPeek# ERROR: dec_inst or output is NULL\n");
    return (VP8DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecPeek# ERROR: Decoder not initialized\n");
    return (VP8DEC_NOT_INITIALIZED);
  }

  /* Don't allow peek for webp */
  if(dec_cont->intra_only) {
    DWLmemset(output, 0, sizeof(VP8DecPicture));
    return VP8DEC_OK;
  }

#ifdef USE_OUTPUT_RELEASE
  /* when output release thread enabled, VP8DecNextPicture_INTERNAL() called in
     VP8DecDecode(), and so dec_cont->fullness used to sample the real out_count
     in case of VP8DecNextPicture_INTERNAL() called before than VP8DecPeek() */
  u32 tmp = dec_cont->fullness;
#else
  u32 tmp = dec_cont->out_count;
#endif

  if (!tmp) {
    DWLmemset(output, 0, sizeof(VP8DecPicture));
    return VP8DEC_OK;
  }

  out_pic = p_asic_buff->prev_out_buffer;
  out_pic_c = &p_asic_buff->pictures_c[ p_asic_buff->prev_out_buffer_i ];

  output->p_output_frame = out_pic->virtual_address;
  output->output_frame_bus_address = out_pic->bus_address;
  if(p_asic_buff->strides_used || p_asic_buff->custom_buffers) {
    output->p_output_frame_c = out_pic_c->virtual_address;
    output->output_frame_bus_address_c = out_pic_c->bus_address;
  } else {
    u32 chroma_buf_offset = p_asic_buff->width * p_asic_buff->height;
    output->p_output_frame_c = output->p_output_frame +
                               chroma_buf_offset / 4;
    output->output_frame_bus_address_c = output->output_frame_bus_address +
                                         chroma_buf_offset;
  }

  output->pic_id = 0;
  buff_id = FindIndex(dec_cont, output->p_output_frame);
  output->decode_id = p_asic_buff->decode_id[buff_id];
  output->is_intra_frame = dec_cont->decoder.key_frame;
  output->is_golden_frame = 0;
  output->nbr_of_err_mbs = 0;

  output->frame_width = (dec_cont->width + 15) & ~15;
  output->frame_height = (dec_cont->height + 15) & ~15;
  output->coded_width = dec_cont->width;
  output->coded_height = dec_cont->height;
  output->luma_stride = p_asic_buff->luma_stride ?
                        p_asic_buff->luma_stride : p_asic_buff->width;
  output->chroma_stride = p_asic_buff->chroma_stride ?
                          p_asic_buff->chroma_stride : p_asic_buff->width;

  return (VP8DEC_PIC_RDY);


}

void vp8hwdMCFreeze(VP8DecContainer_t *dec_cont) {
  /*TODO (mheikkinen) Error handling/concealment is still under construction.*/
  /* TODO Output reference handling */
  VP8HwdBufferQueueRemoveRef(dec_cont->bq, dec_cont->asic_buff->out_buffer_i);
  dec_cont->stream_consumed_callback((u8*)dec_cont->stream, dec_cont->p_user_data);
}

/*------------------------------------------------------------------------------
    Function name   : vp8hwdFreeze
    Description     :
    Return type     :
    Argument        :
------------------------------------------------------------------------------*/
void vp8hwdFreeze(VP8DecContainer_t *dec_cont) {
  /* for multicore */
  if(dec_cont->stream_consumed_callback) {
    vp8hwdMCFreeze(dec_cont);
    return;
  }
  /* Skip */
  dec_cont->pic_number++;
  dec_cont->ref_to_out = 1;
  if (dec_cont->decoder.show_frame)
    dec_cont->out_count++;

  if(dec_cont->pp.pp_instance != NULL) {
    /* last ref to PP */
    dec_cont->pp.dec_pp_if.use_pipeline = 0;
    dec_cont->pp.dec_pp_if.pp_status = DECPP_RUNNING;
  }

  /* Rollback entropy probabilities if refresh is not set */
  if (dec_cont->decoder.probs_decoded &&
      dec_cont->decoder.refresh_entropy_probs == HANTRO_FALSE) {
    DWLmemcpy( &dec_cont->decoder.entropy, &dec_cont->decoder.entropy_last,
               sizeof(vp8EntropyProbs_t));
    DWLmemcpy( dec_cont->decoder.vp7_scan_order, dec_cont->decoder.vp7_prev_scan_order,
               sizeof(dec_cont->decoder.vp7_scan_order));
  }
  /* lost accumulated coeff prob updates -> force video freeze until next
   * keyframe received */
  else if (dec_cont->hw_ec_support && dec_cont->prob_refresh_detected) {
    dec_cont->force_intra_freeze = 1;
  }

  dec_cont->picture_broken = 1;

  /* reset mv memory to not to use too old mvs in extrapolation */
  if (dec_cont->asic_buff->mvs[dec_cont->asic_buff->mvs_ref].virtual_address)
    DWLmemset(
      dec_cont->asic_buff->mvs[dec_cont->asic_buff->mvs_ref].virtual_address,
      0, dec_cont->width * dec_cont->height / 256 * 16 * sizeof(u32));


}

/*------------------------------------------------------------------------------
    Function name   : CheckBitstreamWorkaround
    Description     : Check if we need a workaround for a rare bug.
    Return type     :
    Argument        :
------------------------------------------------------------------------------*/
u32 CheckBitstreamWorkaround(vp8_decoder_t* dec) {
  /* TODO: HW ID check, P pic stuff */
  if( dec->segmentation_map_update &&
      dec->coeff_skip_mode == 0 &&
      dec->key_frame) {
    return 1;
  }

  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : DoBitstreamWorkaround
    Description     : Perform workaround for bug.
    Return type     :
    Argument        :
------------------------------------------------------------------------------*/
#if 0
void DoBitstreamWorkaround(vp8_decoder_t* dec, DecAsicBuffers_t *p_asic_buff, vpBoolCoder_t*bc) {
  /* TODO in entirety */
}
#endif

/* TODO(mheikkinen) Work in progress */
void ConcealRefAvailability(u32 * output, u32 height, u32 width) {
  u8 * p_ref_status = (u8 *)(output +
                             (height * width * 3)/2);

  p_ref_status[1] = height & 0xFFU;
  p_ref_status[0] = (height >> 8) & 0xFFU;
}

/*------------------------------------------------------------------------------
    Function name   : vp8hwdErrorConceal
    Description     :
    Return type     :
    Argument        :
------------------------------------------------------------------------------*/
void vp8hwdErrorConceal(VP8DecContainer_t *dec_cont, addr_t bus_address,
                        u32 conceal_everything) {
  /* force keyframes processed like normal frames (mvs extrapolated for
   * all mbs) */
  if (dec_cont->decoder.key_frame) {
    dec_cont->decoder.key_frame = 0;
  }

  vp8hwdEc(&dec_cont->ec,
           dec_cont->asic_buff->mvs[dec_cont->asic_buff->mvs_ref].virtual_address,
           dec_cont->asic_buff->mvs[dec_cont->asic_buff->mvs_curr].virtual_address,
           dec_cont->conceal_start_mb_y * dec_cont->width/16 + dec_cont->conceal_start_mb_x,
           conceal_everything);

  dec_cont->conceal = 1;
  if (conceal_everything)
    dec_cont->conceal_start_mb_x = dec_cont->conceal_start_mb_y = 0;
  VP8HwdAsicInitPicture(dec_cont);
  VP8HwdAsicStrmPosUpdate(dec_cont, bus_address);

  ConcealRefAvailability(dec_cont->asic_buff->out_buffer->virtual_address,
                         MB_MULTIPLE(dec_cont->asic_buff->height),  MB_MULTIPLE(dec_cont->asic_buff->width));

  dec_cont->conceal = 0;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecSetPictureBuffers
    Description     :
    Return type     :
    Argument        :
------------------------------------------------------------------------------*/
VP8DecRet VP8DecSetPictureBuffers( VP8DecInst dec_inst,
                                   VP8DecPictureBufferProperties * p_pbp ) {

  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;
  u32 i;
  u32 ok;
#if DEC_X170_REFBU_WIDTH == 64
  const u32 max_stride = 1<<18;
#else
  const u32 max_stride = 1<<17;
#endif /* DEC_X170_REFBU_WIDTH */
  u32 luma_stride_pow2 = 0;
  u32 chroma_stride_pow2 = 0;

  if(!dec_inst || !p_pbp) {
    DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: NULL parameter");
    return VP8DEC_PARAM_ERROR;
  }

  /* Allow this only at stream start! */
  if ( ((dec_cont->dec_stat != VP8DEC_NEW_HEADERS) &&
        (dec_cont->dec_stat != VP8DEC_INITIALIZED)) ||
       (dec_cont->pic_number > 0)) {
    DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: Setup allowed at stream"\
                " start only!");
    return VP8DEC_PARAM_ERROR;
  }

  if( !dec_cont->stride_support ) {
    DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: Not supported");
    return VP8DEC_FORMAT_NOT_SUPPORTED;
  }

  /* Tiled mode and custom strides not supported yet */
  if( dec_cont->tiled_mode_support &&
      (p_pbp->luma_stride || p_pbp->chroma_stride)) {
    DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: tiled mode and picture "\
                "buffer properties conflict");
    return VP8DEC_PARAM_ERROR;
  }

  /* Strides must be 2^N for some N>=4 */
  if(p_pbp->luma_stride || p_pbp->chroma_stride) {
    ok = 0;
    for ( i = 10 ; i < 32 ; ++i ) {
      if(p_pbp->luma_stride == (u32)(1<<i)) {
        luma_stride_pow2 = i;
        ok = 1;
        break;
      }
    }
    if(!ok) {
      DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: luma stride must be a "\
                  "power of 2");
      return VP8DEC_PARAM_ERROR;
    }

    ok = 0;
    for ( i = 10 ; i < 32 ; ++i ) {
      if(p_pbp->chroma_stride == (u32)(1<<i)) {
        chroma_stride_pow2 = i;
        ok = 1;
        break;
      }
    }
    if(!ok) {
      DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: luma stride must be a "\
                  "power of 2");
      return VP8DEC_PARAM_ERROR;
    }
  }

  /* Max luma stride check */
  if(p_pbp->luma_stride > max_stride) {
    DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: luma stride exceeds "\
                "maximum");
    return VP8DEC_PARAM_ERROR;
  }

  /* Max chroma stride check */
  if(p_pbp->chroma_stride > max_stride) {
    DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: chroma stride exceeds "\
                "maximum");
    return VP8DEC_PARAM_ERROR;
  }

  dec_cont->asic_buff->luma_stride = p_pbp->luma_stride;
  dec_cont->asic_buff->chroma_stride = p_pbp->chroma_stride;
  dec_cont->asic_buff->luma_stride_pow2 = luma_stride_pow2;
  dec_cont->asic_buff->chroma_stride_pow2 = chroma_stride_pow2;
  dec_cont->asic_buff->strides_used = 0;
  dec_cont->asic_buff->custom_buffers = 0;
  if( dec_cont->asic_buff->luma_stride ||
      dec_cont->asic_buff->chroma_stride ) {
    dec_cont->asic_buff->strides_used = 1;
  }

  /* Check custom buffers */
  if( p_pbp->num_buffers ) {
    userMem_t * user_mem = &dec_cont->asic_buff->user_mem;
    u32 num_buffers = p_pbp->num_buffers;

    if( dec_cont->intra_only ) {
      DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: custom buffers not "\
                  "applicable for WebP");
      return VP8DEC_PARAM_ERROR;
    }

    /* Check buffer arrays */
    if( !p_pbp->p_pic_buffer_y ||
        !p_pbp->pic_buffer_bus_address_y ||
        !p_pbp->p_pic_buffer_c ||
        !p_pbp->pic_buffer_bus_address_c ) {
      DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: Invalid buffer "\
                  "array(s)");
      return VP8DEC_PARAM_ERROR;
    }

    /* As of now, minimum 5 buffers in multicore, 4 in single Core legacy. */
    /* TODO(mheikkinen) Single Core should work with 4 buffers
    if( num_buffers < 4 + (dec_cont->num_cores > 1) ? 1 : 0 )*/
    if( num_buffers < 5) {
      DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: Not enough buffers. " \
                  "Minimum requirement is 5 buffers.");
      return VP8DEC_PARAM_ERROR;
    }

    /* Limit upper boundary to 32 */
    if( num_buffers > VP8DEC_MAX_PIC_BUFFERS )
      num_buffers = VP8DEC_MAX_PIC_BUFFERS;

    /* Check all buffers */
    for( i = 0 ; i < num_buffers ; ++i ) {
      if ((p_pbp->p_pic_buffer_y[i] != NULL && p_pbp->pic_buffer_bus_address_y[i] == 0) ||
          (p_pbp->p_pic_buffer_y[i] == NULL && p_pbp->pic_buffer_bus_address_y[i] != 0) ||
          (p_pbp->p_pic_buffer_c[i] != NULL && p_pbp->pic_buffer_bus_address_c[i] == 0) ||
          (p_pbp->p_pic_buffer_c[i] == NULL && p_pbp->pic_buffer_bus_address_c[i] != 0) ||
          (p_pbp->p_pic_buffer_y[i] == NULL && p_pbp->p_pic_buffer_c[i] != 0) ||
          (p_pbp->p_pic_buffer_y[i] != NULL && p_pbp->p_pic_buffer_c[i] == 0)) {
        DEC_API_TRC("VP8DecSetPictureBuffers# ERROR: Invalid buffer "\
                    "supplied");
        return VP8DEC_PARAM_ERROR;
      }
    }

    /* Seems ok, update internal descriptors */
    for( i = 0 ; i < num_buffers ; ++i ) {
      user_mem->p_pic_buffer_y[i] = p_pbp->p_pic_buffer_y[i];
      user_mem->p_pic_buffer_c[i] = p_pbp->p_pic_buffer_c[i];
      user_mem->pic_buffer_bus_addr_y[i] = p_pbp->pic_buffer_bus_address_y[i];
      user_mem->pic_buffer_bus_addr_c[i] = p_pbp->pic_buffer_bus_address_c[i];
    }
    dec_cont->num_buffers = num_buffers;
    dec_cont->asic_buff->custom_buffers = 1;

  }
#ifdef USE_EXTERNAL_BUFFER
  VP8SetExternalBufferInfo(dec_cont);
#endif

  DEC_API_TRC("VP8DecSetPictureBuffers# OK\n");
  return (VP8DEC_OK);

}

static struct DWLLinearMem* GetPrevRef(VP8DecContainer_t *dec_cont) {
  return dec_cont->asic_buff->pictures +
         VP8HwdBufferQueueGetPrevRef(dec_cont->bq);
}

static i32 FindIndex(VP8DecContainer_t* dec_cont, const u32* address) {
  i32 i;

  if(dec_cont->user_mem) {
    for (i = 0; i < (i32)dec_cont->num_buffers; i++) {
      if(dec_cont->asic_buff->user_mem.p_pic_buffer_y[i] == address)
        break;
    }
    ASSERT((u32)i < dec_cont->num_buffers);
  } else {
    for (i = 0; i < (i32)dec_cont->num_buffers; i++) {
      if (dec_cont->asic_buff->pictures[i].virtual_address == address)
        break;
    }
    ASSERT((u32)i < dec_cont->num_buffers);
  }
  return i;
}

#ifdef USE_OUTPUT_RELEASE
static VP8DecRet VP8PushOutput(VP8DecContainer_t* dec_cont) {
  u32 ret=VP8DEC_OK;
  VP8DecPicture output;

  /* Sample dec_cont->out_count for Peek */
  dec_cont->fullness = dec_cont->out_count;
  if(dec_cont->num_cores == 1) {
    do {
      ret = VP8DecNextPicture_INTERNAL(dec_cont, &output, 0);
      if(ret == VP8DEC_ABORTED)
        return (VP8DEC_ABORTED);
    } while( ret == VP8DEC_PIC_RDY);
  }
  return ret;
}
#endif

#ifdef USE_EXTERNAL_BUFFER
void VP8SetExternalBufferInfo(VP8DecInst dec_inst) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 pict_buff_size;
  u32 luma_stride;
  u32 chroma_stride;
  u32 luma_size;
  u32 chroma_size;
  u32 height;
  u32 buffer;

  buffer = dec_cont->num_buffers;
  switch(dec_cont->dec_mode) {
  case VP8HWD_VP7:
    if(buffer < 3)
      buffer = 3;
    break;
  case VP8HWD_VP8:
    if(dec_cont->intra_only == HANTRO_TRUE)
      buffer = 1;
    else {
      if(buffer < 4)
        buffer = 4;
    }
    break;
  }

  luma_stride = p_asic_buff->luma_stride ? p_asic_buff->luma_stride : p_asic_buff->width;
  chroma_stride = p_asic_buff->chroma_stride ? p_asic_buff->chroma_stride : p_asic_buff->width;

  if (!dec_cont->slice_height)
    height = p_asic_buff->height;
  else
    height = (dec_cont->slice_height + 1) * 16;

  if(dec_cont->pp.pp_instance)
    height = ((height + 16) / 32) * 32;

  luma_size = luma_stride * height;
  chroma_size = chroma_stride * (height / 2);
  pict_buff_size = luma_size + chroma_size;
  pict_buff_size += 16; /* space for multicore status fields */

  u32 ref_buff_size = pict_buff_size;

  dec_cont->tot_buffers = dec_cont->buf_num = buffer;
  dec_cont->next_buf_size = ref_buff_size;
}

VP8DecRet VP8DecGetBufferInfo(VP8DecInst dec_inst, VP8DecBufferInfo *mem_info) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;

  struct DWLLinearMem empty = {0, 0, 0, 0};

  if(dec_cont == NULL || mem_info == NULL) {
    return VP8DEC_PARAM_ERROR;
  }

  if(dec_cont->buf_to_free == NULL && dec_cont->next_buf_size == 0) {
    /* External reference buffer: release done. */
    mem_info->buf_to_free = empty;
    mem_info->next_buf_size = dec_cont->next_buf_size;
    mem_info->buf_num = dec_cont->buf_num;
    return VP8DEC_OK;
  }

  if(dec_cont->buf_to_free) {
    mem_info->buf_to_free = *dec_cont->buf_to_free;
    dec_cont->buf_to_free->virtual_address = NULL;
    dec_cont->buf_to_free = NULL;
  } else
    mem_info->buf_to_free = empty;

  mem_info->next_buf_size = dec_cont->next_buf_size;
  mem_info->buf_num = dec_cont->buf_num;

  ASSERT((mem_info->buf_num && mem_info->next_buf_size) ||
         (mem_info->buf_to_free.virtual_address != NULL));

  return VP8DEC_WAITING_FOR_BUFFER;
}

VP8DecRet VP8DecAddBuffer(VP8DecInst dec_inst, struct DWLLinearMem *info) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  VP8DecRet dec_ret = VP8DEC_OK;

  if(dec_inst == NULL || info == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(info->virtual_address) ||
      X170_CHECK_BUS_ADDRESS_AGLINED(info->bus_address) ||
      info->size < dec_cont->next_buf_size) {
    return VP8DEC_PARAM_ERROR;
  }

  u32 i = dec_cont->buffer_index;

  dec_cont->n_ext_buf_size = info->size;

  if(i < dec_cont->tot_buffers) {
    p_asic_buff->pictures[i] = *info;
    p_asic_buff->pictures_c[i].virtual_address =
      p_asic_buff->pictures[i].virtual_address +
      (p_asic_buff->chroma_buf_offset/4);
    p_asic_buff->pictures_c[i].bus_address =
      p_asic_buff->pictures[i].bus_address +
      p_asic_buff->chroma_buf_offset;

    {
      void *base = (char*)p_asic_buff->pictures[i].virtual_address
                   + p_asic_buff->sync_mc_offset;
      (void) DWLmemset(base, ~0, 16);
    }

    dec_cont->buffer_index++;
    if(dec_cont->buffer_index < dec_cont->tot_buffers)
      dec_ret = VP8DEC_WAITING_FOR_BUFFER;
  } else {
    /* Adding extra buffers. */
    if(i >= VP8DEC_MAX_PIC_BUFFERS) {
      /* Too much buffers added. */
      return VP8DEC_EXT_BUFFER_REJECTED;
    }

    p_asic_buff->pictures[i] = *info;
    p_asic_buff->pictures_c[i].virtual_address =
      p_asic_buff->pictures[i].virtual_address +
      (p_asic_buff->chroma_buf_offset/4);
    p_asic_buff->pictures_c[i].bus_address =
      p_asic_buff->pictures[i].bus_address +
      p_asic_buff->chroma_buf_offset;

    {
      void *base = (char*)p_asic_buff->pictures[i].virtual_address
                   + p_asic_buff->sync_mc_offset;
      (void) DWLmemset(base, ~0, 16);
    }

    dec_cont->buffer_index++;
    dec_cont->tot_buffers++;
    dec_cont->num_buffers++;
    VP8HwdBufferQueueAddBuffer(dec_cont->bq, i);
  }
  return dec_ret;
}
#endif

#ifdef USE_OUTPUT_RELEASE
void VP8EnterAbortState(VP8DecContainer_t* dec_cont) {
  VP8HwdBufferQueueSetAbort(dec_cont->bq);
#ifdef USE_OMXIL_BUFFER
  FifoSetAbort(dec_cont->fifo_out);
#endif
  dec_cont->abort = 1;
}

void VP8ExistAbortState(VP8DecContainer_t* dec_cont) {
  VP8HwdBufferQueueClearAbort(dec_cont->bq);
#ifdef USE_OMXIL_BUFFER
  FifoClearAbort(dec_cont->fifo_out);
#endif
  dec_cont->abort = 0;
}


void VP8EmptyBufferQueue(VP8DecContainer_t* dec_cont) {
  u32 i;

  for (i = 0; i < dec_cont->num_buffers; i++) {
    VP8HwdBufferQueueEmptyRef(dec_cont->bq, i);
  }
}

void VP8StateReset(VP8DecContainer_t* dec_cont) {
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 buffers = dec_cont->num_buffers_reserved;

  /* Clear internal parameters in VP8DecContainer_t */
  dec_cont->dec_stat = VP8DEC_INITIALIZED;
  dec_cont->pic_number = 0;
  dec_cont->display_number = 0;
#ifdef USE_EXTERNAL_BUFFER
#ifdef USE_OMXIL_BUFFER
  dec_cont->tot_buffers = buffers;
  dec_cont->buffer_index = 0;
#endif
  dec_cont->no_reallocation = 1;
#endif
#ifdef USE_OMXIL_BUFFER
  dec_cont->num_buffers = buffers;
  if (dec_cont->bq)
    VP8HwdBufferQueueRelease(dec_cont->bq);
  dec_cont->bq = VP8HwdBufferQueueInitialize(dec_cont->num_buffers);
#endif
  dec_cont->last_slice = 0;
  dec_cont->fullness = 0;
  dec_cont->picture_broken = 0;
  dec_cont->out_count = 0;
  dec_cont->ref_to_out = 0;
  dec_cont->slice_concealment = 0;
  dec_cont->tot_decoded_rows = 0;
  dec_cont->output_rows = 0;
  dec_cont->conceal = 0;
  dec_cont->conceal_start_mb_x = 0;
  dec_cont->conceal_start_mb_y = 0;
  dec_cont->prev_is_key = 0;
  dec_cont->force_intra_freeze = 0;
  dec_cont->prob_refresh_detected = 0;
  dec_cont->get_buffer_after_abort = 1;
  (void) DWLmemset(&dec_cont->bc, 0, sizeof(vpBoolCoder_t));

  /* Clear internal parameters in DecAsicBuffers */
  p_asic_buff->prev_out_buffer = NULL;
  p_asic_buff->mvs_curr = p_asic_buff->mvs_ref = 0;
  p_asic_buff->whole_pic_concealed = 0;
  p_asic_buff->dc_pred[0] = p_asic_buff->dc_pred[1] =
  p_asic_buff->dc_match[0] = p_asic_buff->dc_match[1] = 0;
  (void) DWLmemset(p_asic_buff->decode_id, 0, VP8DEC_MAX_PIC_BUFFERS * sizeof(u32));
  (void) DWLmemset(p_asic_buff->first_show, 0, VP8DEC_MAX_PIC_BUFFERS * sizeof(u32));
#ifdef USE_OMXIL_BUFFER
  (void) DWLmemset(p_asic_buff->not_displayed, 0, VP8DEC_MAX_PIC_BUFFERS * sizeof(u32));
  (void) DWLmemset(p_asic_buff->display_index, 0, VP8DEC_MAX_PIC_BUFFERS * sizeof(u32));
  (void) DWLmemset(p_asic_buff->picture_info, 0, VP8DEC_MAX_PIC_BUFFERS * sizeof(VP8DecPicture));
#endif
  p_asic_buff->out_buffer_i = REFERENCE_NOT_SET;
  p_asic_buff->out_buffer = NULL;
#ifdef USE_OMXIL_BUFFER
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_out);
  FifoInit(VP8DEC_MAX_PIC_BUFFERS, &dec_cont->fifo_out);
#endif
  (void)buffers;
}

VP8DecRet VP8DecAbort(VP8DecInst dec_inst) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;

  DEC_API_TRC("VP8DecAbort#\n");

  if (dec_inst == NULL) {
    return (VP8DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);
  /* Abort frame buffer waiting */
  VP8EnterAbortState(dec_cont);

  pthread_mutex_unlock(&dec_cont->protect_mutex);

  DEC_API_TRC("VP8DecAbort# VP8DEC_OK\n");
  return (VP8DEC_OK);
}

VP8DecRet VP8DecAbortAfter(VP8DecInst dec_inst) {
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;

  DEC_API_TRC("VP8DecAbortAfter#\n");

  if (dec_inst == NULL) {
    return (VP8DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);

#if 0
  /* If normal EOS is waited, return directly */
  if(dec_cont->dec_stat == VP8DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (VP8DEC_OK);
  }
#endif

  /* Stop and release HW */
  if(dec_cont->asic_running) {
    /* stop HW */
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 1 * 4, 0);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  /* Clear any remaining pictures from DPB */
  VP8EmptyBufferQueue(dec_cont);

  VP8StateReset(dec_cont);

  VP8ExistAbortState(dec_cont);

  pthread_mutex_unlock(&dec_cont->protect_mutex);

  DEC_API_TRC("VP8DecAbortAfter# VP8DEC_OK\n");
  return (VP8DEC_OK);
}
#endif
