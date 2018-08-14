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
#include "vp6decapi.h"
#include "version.h"
#include "dwl.h"
#include "vp6hwd_container.h"
#include "vp6hwd_debug.h"
#include "vp6hwd_asic.h"
#include "refbuffer.h"
#include "bqueue.h"
#include "errorhandling.h"
#include "tiledref.h"

#define VP6DEC_MAJOR_VERSION 1
#define VP6DEC_MINOR_VERSION 0

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#include <stdio.h>
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

#ifdef VP6DEC_TRACE
#define DEC_API_TRC(str)    VP6DecTrace(str)
#else
#define DEC_API_TRC(str)    do{}while(0)
#endif
#ifndef HANTRO_OK
#define HANTRO_OK 0
#endif

#ifndef HANTRO_NOK
#define HANTRO_NOK 1
#endif

#define VP6_MIN_WIDTH  48
#define VP6_MIN_HEIGHT 48

#define VP6_MIN_WIDTH_EN_DTRC  96
#define VP6_MIN_HEIGHT_EN_DTRC 48
#define MAX_PIC_SIZE   4096*4096

extern void vp6PreparePpRun(VP6DecContainer_t *dec_cont);
static u32 vp6hwdCheckSupport(VP6DecContainer_t *dec_cont);

#ifdef USE_EXTERNAL_BUFFER
static void VP6SetExternalBufferInfo(VP6DecInst dec_inst);
#endif
static i32 FindIndex(VP6DecContainer_t *dec_cont, const u32* address);

#ifdef USE_OUTPUT_RELEASE
VP6DecRet VP6DecNextPicture_INTERNAL(VP6DecInst dec_inst,
                                     VP6DecPicture * output, u32 end_of_stream);

static VP6DecRet VP6PushOutput(VP6DecContainer_t* dec_cont);

static void VP6EnterAbortState(VP6DecContainer_t *dec_cont);
static void VP6ExistAbortState(VP6DecContainer_t *dec_cont);
static void VP6EmptyBufferQueue(VP6DecContainer_t *dec_cont);
#endif

/*------------------------------------------------------------------------------
    Function name : VP6DecGetAPIVersion
    Description   : Return the API version information

    Return type   : VP6DecApiVersion
    Argument      : void
------------------------------------------------------------------------------*/
VP6DecApiVersion VP6DecGetAPIVersion(void) {
  VP6DecApiVersion ver;

  ver.major = VP6DEC_MAJOR_VERSION;
  ver.minor = VP6DEC_MINOR_VERSION;

  DEC_API_TRC("VP6DecGetAPIVersion# OK\n");

  return ver;
}

/*------------------------------------------------------------------------------
    Function name : VP6DecGetBuild
    Description   : Return the SW and HW build information

    Return type   : VP6DecBuild
    Argument      : void
------------------------------------------------------------------------------*/
VP6DecBuild VP6DecGetBuild(void) {
  VP6DecBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_VP6_DEC);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_VP6_DEC);

  DEC_API_TRC("VP6DecGetBuild# OK\n");

  return build_info;
}

/*------------------------------------------------------------------------------
    Function name   : vp6decinit
    Description     :
    Return type     : VP6DecRet
    Argument        : VP6DecInst * dec_inst
                     enum DecErrorHandling error_handling
------------------------------------------------------------------------------*/
VP6DecRet VP6DecInit(VP6DecInst * dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                     const void *dwl,
#endif
                     enum DecErrorHandling error_handling,
                     u32 num_frame_buffers,
                     enum DecDpbFlags dpb_flags,
                     u32 use_adaptive_buffers,
                     u32 n_guard_size) {
  VP6DecRet ret;
  VP6DecContainer_t *dec_cont;
#ifndef USE_EXTERNAL_BUFFER
  const void *dwl;
#endif
  u32 i;
  u32 reference_frame_format;

#ifndef USE_EXTERNAL_BUFFER
  struct DWLInitParam dwl_init;
#endif
  DWLHwConfig config;

  DEC_API_TRC("VP6DecInit#\n");

  /* check that right shift on negative numbers is performed signed */
  /*lint -save -e* following check causes multiple lint messages */
#if (((-1) >> 1) != (-1))
#error Right bit-shifting (>>) does not preserve the sign
#endif
  /*lint -restore */

  if(dec_inst == NULL) {
    DEC_API_TRC("VP6DecInit# ERROR: dec_inst == NULL");
    return (VP6DEC_PARAM_ERROR);
  }

  *dec_inst = NULL;   /* return NULL instance for any error */

  /* check that VP6 decoding supported in HW */
  {

    DWLHwConfig hw_cfg;

    DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_VP6_DEC);
    if(!hw_cfg.vp6_support) {
      DEC_API_TRC("VP6DecInit# ERROR: VP6 not supported in HW\n");
      return VP6DEC_FORMAT_NOT_SUPPORTED;
    }
  }

#ifndef USE_EXTERNAL_BUFFER
  /* init DWL for the specified client */
  dwl_init.client_type = DWL_CLIENT_TYPE_VP6_DEC;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    DEC_API_TRC("VP6DecInit# ERROR: DWL Init failed\n");
    return (VP6DEC_DWL_ERROR);
  }
#endif

  /* allocate instance */
  dec_cont = (VP6DecContainer_t *) DWLmalloc(sizeof(VP6DecContainer_t));

  if(dec_cont == NULL) {
    DEC_API_TRC("VP6DecInit# ERROR: Memory allocation failed\n");
    ret = VP6DEC_MEMFAIL;
    goto err;
  }

  (void) DWLmemset(dec_cont, 0, sizeof(VP6DecContainer_t));
  dec_cont->dwl = dwl;

  pthread_mutex_init(&dec_cont->protect_mutex, NULL);

  /* initial setup of instance */

  dec_cont->dec_stat = VP6DEC_INITIALIZED;
  dec_cont->checksum = dec_cont;  /* save instance as a checksum */
  if(num_frame_buffers > 16)    num_frame_buffers = 16;
  if(num_frame_buffers < 3)     num_frame_buffers = 3;
  dec_cont->num_buffers = num_frame_buffers;
  dec_cont->num_buffers_reserved = num_frame_buffers;

  VP6HwdAsicInit(dec_cont);   /* Init ASIC */

  if(VP6HwdAsicAllocateMem(dec_cont) != 0) {
    DEC_API_TRC("VP6DecInit# ERROR: ASIC Memory allocation failed\n");
    ret = VP6DEC_MEMFAIL;
    goto err;
  }

  (void)DWLmemset(&config, 0, sizeof(DWLHwConfig));

  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_VP6_DEC);

  if(!config.addr64_support && sizeof(void *) == 8) {
    DEC_API_TRC("VP6DecInit# ERROR: HW not support 64bit address!\n");
    return (VP6DEC_PARAM_ERROR);
  }

  i = DWLReadAsicID(DWL_CLIENT_TYPE_VP6_DEC) >> 16;
  if(i == 0x8170U)
    error_handling = 0;
  dec_cont->ref_buf_support = config.ref_buf_support;
  reference_frame_format = dpb_flags & DEC_REF_FRM_FMT_MASK;
  if(reference_frame_format == DEC_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!config.tiled_mode_support) {
      return VP6DEC_FORMAT_NOT_SUPPORTED;
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

#ifdef USE_OUTPUT_RELEASE
  if (FifoInit(16, &dec_cont->fifo_display) != FIFO_OK)
    return VP6DEC_MEMFAIL;
#endif
#ifdef USE_EXTERNAL_BUFFER
  dec_cont->no_reallocation = 1;
#endif

  dec_cont->use_adaptive_buffers = use_adaptive_buffers;
  dec_cont->n_guard_size = n_guard_size;

  /* If tile mode is enabled, should take DTRC minimum size(96x48) into consideration */
  if (dec_cont->tiled_mode_support) {
    dec_cont->min_dec_pic_width = VP6_MIN_WIDTH_EN_DTRC;
    dec_cont->min_dec_pic_height = VP6_MIN_HEIGHT_EN_DTRC;
  }
  else {
    dec_cont->min_dec_pic_width = VP6_MIN_WIDTH;
    dec_cont->min_dec_pic_height = VP6_MIN_HEIGHT;
  }

  dec_cont->dec_stat = VP6DEC_INITIALIZED;

  /* return new instance to application */
  *dec_inst = (VP6DecInst) dec_cont;

  DEC_API_TRC("VP6DecInit# OK\n");
  return (VP6DEC_OK);

err:
  if(dec_cont != NULL)
    DWLfree(dec_cont);

#ifndef USE_EXTERNAL_BUFFER
  if(dwl != NULL) {
    i32 dwlret = DWLRelease(dwl);

    ASSERT(dwlret == DWL_OK);
    (void) dwlret;
  }
#endif

  *dec_inst = NULL;
  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : VP6DecRelease
    Description     :
    Return type     : void
    Argument        : VP6DecInst dec_inst
------------------------------------------------------------------------------*/
void VP6DecRelease(VP6DecInst dec_inst) {

  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;
  const void *dwl;

  DEC_API_TRC("VP6DecRelease#\n");

  if(dec_cont == NULL) {
    DEC_API_TRC("VP6DecRelease# ERROR: dec_inst == NULL\n");
    return;
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecRelease# ERROR: Decoder not initialized\n");
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

#ifdef USE_OUTPUT_RELEASE
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
#endif

  VP6HwdAsicReleaseMem(dec_cont);
  VP6HwdAsicReleasePictures(dec_cont);
  VP6HWDeleteHuffman(&dec_cont->pb);

  dec_cont->checksum = NULL;
  DWLfree(dec_cont);

#ifndef USE_EXTERNAL_BUFFER
  {
    i32 dwlret = DWLRelease(dwl);

    ASSERT(dwlret == DWL_OK);
    (void) dwlret;
  }
#endif

  DEC_API_TRC("VP6DecRelease# OK\n");

  return;
}

/*------------------------------------------------------------------------------
    Function name   : VP6DecGetInfo
    Description     :
    Return type     : VP6DecRet
    Argument        : VP6DecInst dec_inst
    Argument        : VP6DecInfo * dec_info
------------------------------------------------------------------------------*/
VP6DecRet VP6DecGetInfo(VP6DecInst dec_inst, VP6DecInfo * dec_info) {
  const VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;

  DEC_API_TRC("VP6DecGetInfo#");

  if(dec_inst == NULL || dec_info == NULL) {
    DEC_API_TRC("VP6DecGetInfo# ERROR: dec_inst or dec_info is NULL\n");
    return VP6DEC_PARAM_ERROR;
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecGetInfo# ERROR: Decoder not initialized\n");
    return VP6DEC_NOT_INITIALIZED;
  }

  dec_info->vp6_version = dec_cont->pb.Vp3VersionNo;
  dec_info->vp6_profile = dec_cont->pb.VpProfile;
#ifdef USE_EXTERNAL_BUFFER
  dec_info->pic_buff_size = dec_cont->buf_num;
#endif

  if(dec_cont->tiled_mode_support) {
    dec_info->output_format = VP6DEC_TILED_YUV420;
  } else {
    dec_info->output_format = VP6DEC_SEMIPLANAR_YUV420;
  }

  /* Fragments have 8 pixels */
  dec_info->frame_width = dec_cont->pb.HFragments * 8;
  dec_info->frame_height = dec_cont->pb.VFragments * 8;
  dec_info->scaled_width = dec_cont->pb.OutputWidth * 8;
  dec_info->scaled_height = dec_cont->pb.OutputHeight * 8;
  dec_info->dpb_mode = DEC_DPB_FRAME;

  dec_info->scaling_mode = dec_cont->pb.ScalingMode;

  return VP6DEC_OK;
}

/*------------------------------------------------------------------------------
    Function name   : VP6DecDecode
    Description     :
    Return type     : VP6DecRet
    Argument        : VP6DecInst dec_inst
    Argument        : const VP6DecInput * input
    Argument        : VP6DecFrame * output
------------------------------------------------------------------------------*/
VP6DecRet VP6DecDecode(VP6DecInst dec_inst,
                       const VP6DecInput * input, VP6DecOutput * output) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff = NULL;
  i32 ret;
  u32 asic_status;
  u32 error_concealment = 0;

  DEC_API_TRC("VP6DecDecode#\n");

  /* Check that function input parameters are valid */
  if(input == NULL || output == NULL || dec_inst == NULL) {
    DEC_API_TRC("VP6DecDecode# ERROR: NULL arg(s)\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecDecode# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }

  DWLmemset(output, 0, sizeof(VP6DecOutput));

  if(input->data_len == 0 ||
      input->data_len > DEC_X170_MAX_STREAM ||
      X170_CHECK_VIRTUAL_ADDRESS(input->stream) ||
      X170_CHECK_BUS_ADDRESS(input->stream_bus_address)) {
    DEC_API_TRC("VP6DecDecode# ERROR: Invalid arg value\n");
    return VP6DEC_PARAM_ERROR;
  }

#ifdef VP6DEC_EVALUATION
  if(dec_cont->pic_number > VP6DEC_EVALUATION) {
    DEC_API_TRC("VP6DecDecode# VP6DEC_EVALUATION_LIMIT_EXCEEDED\n");
    return VP6DEC_EVALUATION_LIMIT_EXCEEDED;
  }
#endif

  /* aliases */
  p_asic_buff = dec_cont->asic_buff;

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort) {
    return (VP6DEC_ABORTED);
  }

  if (dec_cont->get_buffer_after_abort) {
    if(dec_cont->pp.pp_instance == NULL) {
      p_asic_buff->out_buffer_i = BqueueNext2( &dec_cont->bq,
                                          BQUEUE_UNUSED, BQUEUE_UNUSED,
                                          BQUEUE_UNUSED, 0 );
      if (p_asic_buff->out_buffer_i == (u32)0xFFFFFFFFU) {
        if (dec_cont->abort)
          return VP6DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
        else {
          output->data_left = input->data_len;
          return VP6DEC_NO_DECODING_BUFFER;
        }
#endif
      }
      p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
    }
    else {
      p_asic_buff->out_buffer_i = BqueueNext( &dec_cont->bq,
                                          BQUEUE_UNUSED, BQUEUE_UNUSED,
                                          BQUEUE_UNUSED, 0 );
    }

    p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
    /* These need to point at something so use the output buffer */
    p_asic_buff->refBuffer        = p_asic_buff->out_buffer;
    p_asic_buff->golden_buffer     = p_asic_buff->out_buffer;
    p_asic_buff->ref_buffer_i      = BQUEUE_UNUSED;
    p_asic_buff->golden_buffer_i   = BQUEUE_UNUSED;
    dec_cont->get_buffer_after_abort = 0;
  }

#endif

  if (dec_cont->no_decoding_buffer) {
    dec_cont->no_decoding_buffer = 0;
    goto request_decoding_buffer;
  }

  if(dec_cont->dec_stat == VP6DEC_NEW_HEADERS) {
    /* we stopped the decoding after noticing new picture size */
    /* continue from where we left */
#ifndef USE_EXTERNAL_BUFFER
    dec_cont->dec_stat = VP6DEC_INITIALIZED;
    goto continue_pic_decode;
#else
    //if(!dec_cont->no_reallocation)
    //    VP6HwdAsicReleasePictures(dec_cont);
    if((ret = VP6HwdAsicAllocatePictures(dec_cont)) != 0) {
      DEC_API_TRC
      ("VP6DecDecode# ERROR: Picture memory allocation failed\n");
#ifdef USE_OUTPUT_RELEASE
      if (ret == -2)
        return VP6DEC_ABORTED;
      else
#endif
        return VP6DEC_MEMFAIL;
    }

    if(dec_cont->no_reallocation)
      dec_cont->dec_stat = VP6DEC_INITIALIZED;
    else {
      dec_cont->no_reallocation = 1;
      dec_cont->dec_stat = VP6DEC_WAITING_BUFFER;
      dec_cont->buffer_index = 0;
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->abort)
        return VP6DEC_ABORTED;
      else
#endif
        return VP6DEC_WAITING_FOR_BUFFER;
    }
#endif
  }
#ifdef USE_EXTERNAL_BUFFER
  else if (dec_cont->dec_stat == VP6DEC_WAITING_BUFFER) {
    dec_cont->dec_stat = VP6DEC_INITIALIZED;
    goto continue_pic_decode;
  }
#endif

  Vp6StrmInit(&dec_cont->pb.strm, input->stream, input->data_len);

  /* update strm base addresses to ASIC */
  p_asic_buff->partition1_base = input->stream_bus_address;
  p_asic_buff->partition2_base = input->stream_bus_address;

  /* decode frame headers */
  ret = VP6HWLoadFrameHeader(&dec_cont->pb);

  if(ret) {
    DEC_API_TRC("VP6DecDecode# ERROR: Frame header decoding failed\n");
    return VP6DEC_STRM_ERROR;
  }

  if (dec_cont->pb.br.strm_error) {
    if (!dec_cont->pic_number) {
      return VP6DEC_STRM_ERROR;
    } else
      goto freeze;
  }


  /* check for picture size change */
  if((dec_cont->width != (dec_cont->pb.HFragments * 8)) ||
      (dec_cont->height != (dec_cont->pb.VFragments * 8))) {
#ifdef USE_EXTERNAL_BUFFER
    if ((!dec_cont->use_adaptive_buffers &&
         ((dec_cont->pb.HFragments * 8) * (dec_cont->pb.VFragments * 8) >
          dec_cont->width * dec_cont->height)) ||
        (dec_cont->use_adaptive_buffers &&
         (((dec_cont->pb.HFragments * 8) * (dec_cont->pb.VFragments * 8) >
           dec_cont->n_ext_buf_size) ||
          (dec_cont->num_buffers_reserved + dec_cont->n_guard_size > dec_cont->tot_buffers))))
      dec_cont->no_reallocation = 0;

    if (dec_cont->width == 0 && dec_cont->height == 0)
      dec_cont->no_reallocation = 0;
#endif

    /* reallocate picture buffers */
    p_asic_buff->width = dec_cont->pb.HFragments * 8;
    p_asic_buff->height = dec_cont->pb.VFragments * 8;

#ifdef USE_EXTERNAL_BUFFER
    if(!dec_cont->no_reallocation) {
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp.pp_instance == NULL) {
#ifndef USE_EXT_BUF_SAFE_RELEASE
        BqueueMarkNotInUse(&dec_cont->bq);
#else
        BqueueWaitNotInUse(&dec_cont->bq);
#endif
      }
#endif

      VP6HwdAsicReleasePictures(dec_cont);
    }
#else
    if((ret = VP6HwdAsicAllocatePictures(dec_cont)) != 0) {
      DEC_API_TRC
      ("VP6DecDecode# ERROR: Picture memory allocation failed\n");
#ifdef USE_OUTPUT_RELEASE
      if (ret == -2)
        return VP6DEC_ABORTED;
      else
#endif
        return VP6DEC_MEMFAIL;
    }
#endif

    if (vp6hwdCheckSupport(dec_cont) != HANTRO_OK) {
      dec_cont->dec_stat = VP6DEC_INITIALIZED;
      return VP6DEC_STREAM_NOT_SUPPORTED;
    }

    dec_cont->width = dec_cont->pb.HFragments * 8;
    dec_cont->height = dec_cont->pb.VFragments * 8;

    dec_cont->dec_stat = VP6DEC_NEW_HEADERS;

    if( dec_cont->ref_buf_support ) {
      RefbuInit( &dec_cont->ref_buffer_ctrl, 7 /* dec_mode_vp6 */,
                 dec_cont->pb.HFragments / 2,
                 dec_cont->pb.VFragments / 2,
                 dec_cont->ref_buf_support);
    }

    DEC_API_TRC("VP6DecDecode# VP6DEC_HDRS_RDY\n");
#ifdef USE_EXTERNAL_BUFFER
    VP6SetExternalBufferInfo(dec_cont);
    if(dec_cont->no_reallocation) {
      output->data_left = input->data_len;
      return VP6DEC_STRM_PROCESSED;
    } else
#endif
    {

#ifdef USE_OUTPUT_RELEASE
      FifoPush(dec_cont->fifo_display, -2, FIFO_EXCEPTION_DISABLE);
      if(dec_cont->abort)
        return VP6DEC_ABORTED;
      else
#endif
        return VP6DEC_HDRS_RDY;
    }
  }

continue_pic_decode:

  /* If output picture is broken and we are not decoding a base frame,
   * don't even start HW, just output same picture again. */
  if( dec_cont->pb.FrameType != BASE_FRAME &&
      dec_cont->picture_broken &&
      dec_cont->intra_freeze) {

freeze:

    /* Skip */
    dec_cont->pic_number++;
    dec_cont->ref_to_out = 1;
    dec_cont->out_count++;

    DEC_API_TRC("VP6DecDecode# VP6DEC_PIC_DECODED\n");

    if(dec_cont->pp.pp_instance != NULL) {
      vp6PreparePpRun(dec_cont);
      dec_cont->pp.dec_pp_if.use_pipeline = 0;
    }

#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->pp.pp_instance == NULL) {
      if (VP6PushOutput(dec_cont) == VP6DEC_ABORTED)
        return VP6DEC_ABORTED;
    }
#endif
    return VP6DEC_PIC_DECODED;
  } else {
    dec_cont->ref_to_out = 0;
  }

  /* decode probability updates */
  ret = VP6HWDecodeProbUpdates(&dec_cont->pb);
  if(ret) {
    DEC_API_TRC
    ("VP6DecDecode# ERROR: Priobability updates decoding failed\n");
    return VP6DEC_STRM_ERROR;
  }
  if (dec_cont->pb.br.strm_error) {
    if (!dec_cont->pic_number) {
      return VP6DEC_STRM_ERROR;
    } else
      goto freeze;
  }

  /* prepare asic */
  VP6HwdAsicProbUpdate(dec_cont);

  VP6HwdAsicInitPicture(dec_cont);

  VP6HwdAsicStrmPosUpdate(dec_cont);

  /* PP setup stuff */
  vp6PreparePpRun(dec_cont);

  if (!dec_cont->asic_running && dec_cont->partial_freeze) {
    PreparePartialFreeze((u8*)p_asic_buff->out_buffer->virtual_address,
                         (dec_cont->width >> 4), (dec_cont->height >> 4));
  }

  /* run the hardware */
  asic_status = VP6HwdAsicRun(dec_cont);

  /* Handle system error situations */
  if(asic_status == VP6HWDEC_SYSTEM_TIMEOUT) {
    /* This timeout is DWL(software/os) generated */
    DEC_API_TRC("VP6DecDecode# VP6DEC_HW_TIMEOUT, SW generated\n");
    return VP6DEC_HW_TIMEOUT;
  } else if(asic_status == VP6HWDEC_SYSTEM_ERROR) {
    DEC_API_TRC("VP6DecDecode# VP6HWDEC_SYSTEM_ERROR\n");
    return VP6DEC_SYSTEM_ERROR;
  } else if(asic_status == VP6HWDEC_HW_RESERVED) {
    DEC_API_TRC("VP6DecDecode# VP6HWDEC_HW_RESERVED\n");
    return VP6DEC_HW_RESERVED;
  }

  /* Handle possible common HW error situations */
  if(asic_status & DEC_8190_IRQ_BUS) {
    DEC_API_TRC("VP6DecDecode# VP6DEC_HW_BUS_ERROR\n");
    return VP6DEC_HW_BUS_ERROR;
  }

  /* for all the rest we will output a picture (concealed or not) */
  if((asic_status & DEC_8190_IRQ_TIMEOUT) ||
      (asic_status & DEC_8190_IRQ_ERROR) ||
      (asic_status & DEC_8190_IRQ_ABORT)) {
    if (!dec_cont->partial_freeze ||
        !ProcessPartialFreeze((u8*)p_asic_buff->out_buffer->virtual_address,
                              (u8*)p_asic_buff->refBuffer->virtual_address,
                              (dec_cont->width >> 4),
                              (dec_cont->height >> 4),
                              dec_cont->partial_freeze == 1)) {
      /* This timeout is HW generated */
      if(asic_status & DEC_8190_IRQ_TIMEOUT) {
#ifdef VP6HWTIMEOUT_ASSERT
        ASSERT(0);
#endif
        DEBUG_PRINT(("IRQ: HW TIMEOUT\n"));
      } else if(asic_status & DEC_8190_IRQ_ABORT) {
        DEBUG_PRINT(("IRQ: HW ABORT\n"));
      } else {
        DEBUG_PRINT(("IRQ: STREAM ERROR\n"));
      }

      /* PP has to run again for the concealed picture */
      if(dec_cont->pp.pp_instance != NULL && dec_cont->pp.dec_pp_if.use_pipeline) {
        TRACE_PP_CTRL
        ("VP6DecDecode: Concealed picture, PP should run again\n");
        dec_cont->pp.dec_pp_if.pp_status = DECPP_RUNNING;
      }
      dec_cont->out_count++;

      error_concealment = 1;
    } else {
      asic_status &= ~DEC_8190_IRQ_ERROR;
      asic_status &= ~DEC_8190_IRQ_TIMEOUT;
      asic_status |= DEC_8190_IRQ_RDY;
      error_concealment = 0;
    }
  } else if(asic_status & DEC_8190_IRQ_RDY) {
  } else {
    ASSERT(0);
  }

  if(asic_status & DEC_8190_IRQ_RDY) {
    DEBUG_PRINT(("IRQ: PICTURE RDY\n"));

    if(dec_cont->pb.FrameType == BASE_FRAME) {
      p_asic_buff->refBuffer = p_asic_buff->out_buffer;
      p_asic_buff->golden_buffer = p_asic_buff->out_buffer;

      p_asic_buff->ref_buffer_i   = p_asic_buff->out_buffer_i;
      p_asic_buff->golden_buffer_i = p_asic_buff->out_buffer_i;

      dec_cont->picture_broken = 0;

    } else if(dec_cont->pb.RefreshGoldenFrame) {
      p_asic_buff->refBuffer = p_asic_buff->out_buffer;
      p_asic_buff->ref_buffer_i   = p_asic_buff->out_buffer_i;
      p_asic_buff->golden_buffer = p_asic_buff->out_buffer;
      p_asic_buff->golden_buffer_i = p_asic_buff->out_buffer_i;
    } else {
      p_asic_buff->refBuffer = p_asic_buff->out_buffer;
      p_asic_buff->ref_buffer_i   = p_asic_buff->out_buffer_i;
    }
    dec_cont->out_count++;
  }

  /* find first free buffer and use it as next output */
  {
    p_asic_buff->prev_out_buffer = p_asic_buff->out_buffer;
    p_asic_buff->prev_out_buffer_i = p_asic_buff->out_buffer_i;
    p_asic_buff->out_buffer = NULL;

request_decoding_buffer:

#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->pp.pp_instance == NULL) {
      p_asic_buff->out_buffer_i = BqueueNext2( &dec_cont->bq,
                                  p_asic_buff->ref_buffer_i,
                                  p_asic_buff->golden_buffer_i,
                                  BQUEUE_UNUSED, 0);
      if(p_asic_buff->out_buffer_i == (u32)0xFFFFFFFFU) {
        if (dec_cont->abort)
          return VP6DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
        else {
          output->data_left = input->data_len;
          dec_cont->no_decoding_buffer = 1;
          return VP6DEC_NO_DECODING_BUFFER;
        }
#endif
      }
      p_asic_buff->first_show[p_asic_buff->out_buffer_i] = 1;
    } else {
      p_asic_buff->out_buffer_i = BqueueNext( &dec_cont->bq,
                                              p_asic_buff->ref_buffer_i,
                                              p_asic_buff->golden_buffer_i,
                                              BQUEUE_UNUSED, 0);
    }
#else
    p_asic_buff->out_buffer_i = BqueueNext( &dec_cont->bq,
                                            p_asic_buff->ref_buffer_i,
                                            p_asic_buff->golden_buffer_i,
                                            BQUEUE_UNUSED, 0);
#endif
    p_asic_buff->decode_id[p_asic_buff->prev_out_buffer_i] = input->pic_id;
    p_asic_buff->out_buffer = &p_asic_buff->pictures[p_asic_buff->out_buffer_i];
    ASSERT(p_asic_buff->out_buffer != NULL);
  }

  if( error_concealment ) {
    dec_cont->ref_to_out = 1;
    dec_cont->picture_broken = 1;
    BqueueDiscard(&dec_cont->bq, p_asic_buff->out_buffer_i);
    if (!dec_cont->pic_number) {
      (void) DWLmemset( p_asic_buff->refBuffer->virtual_address, 128,
                        p_asic_buff->width * p_asic_buff->height * 3 / 2);
    }
  }

  dec_cont->pic_number++;

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp.pp_instance == NULL) {
    if (VP6PushOutput(dec_cont) == VP6DEC_ABORTED)
      return VP6DEC_ABORTED;
  }
#endif

  DEC_API_TRC("VP6DecDecode# VP6DEC_PIC_DECODED\n");
  return VP6DEC_PIC_DECODED;
}

/*------------------------------------------------------------------------------
    Function name   : VP6DecNextPicture
    Description     :
    Return type     : VP6DecRet
    Argument        : VP6DecInst dec_inst
------------------------------------------------------------------------------*/
VP6DecRet VP6DecNextPicture(VP6DecInst dec_inst,
                            VP6DecPicture * output, u32 end_of_stream) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 pic_for_output = 0;
  u32 buffer_id;
  i32 ret;

  DEC_API_TRC("VP6DecNextPicture#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP6DecNextPicture# ERROR: dec_inst or output is NULL\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecNextPicture# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp.pp_instance == NULL) {
    addr_t i;
    if ((ret = FifoPop(dec_cont->fifo_display, (FifoObject *)&i,
#ifdef GET_OUTPUT_BUFFER_NON_BLOCK
            FIFO_EXCEPTION_ENABLE
#else
            FIFO_EXCEPTION_DISABLE
#endif
            )) != FIFO_ABORT) {

#ifdef GET_OUTPUT_BUFFER_NON_BLOCK
      if (ret == FIFO_EMPTY) return VP6DEC_OK;
#endif

      if ((i32)i == -1) {
        DEC_API_TRC("VP6DecNextPicture# VP6DEC_END_OF_STREAM\n");
        return VP6DEC_END_OF_STREAM;
      }
      if ((i32)i == -2) {
        DEC_API_TRC("VP6DecNextPicture# VP6DEC_FLUSHED\n");
        return VP6DEC_FLUSHED;
      }

      *output = dec_cont->asic_buff->picture_info[i];

      DEC_API_TRC("VP6DecNextPicture# VP6DEC_PIC_RDY\n");
      return (VP6DEC_PIC_RDY);
    } else
      return VP6DEC_ABORTED;
  }
#endif

  if (!dec_cont->pp.pp_instance)
    pic_for_output = dec_cont->out_count != 0;
  else if (dec_cont->pp.dec_pp_if.pp_status != DECPP_IDLE)
    pic_for_output = 1;
  else if (end_of_stream && dec_cont->out_count) {
    pic_for_output = 1;
    dec_cont->pp.dec_pp_if.pp_status = DECPP_RUNNING;
  }

  /* TODO: What if intra_freeze and not pipelined pp? */
  if (dec_cont->pp.pp_instance != NULL &&
      dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
    DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;
    TRACE_PP_CTRL("VP6DecNextPicture: PP has to run\n");

    dec_pp_if->use_pipeline = 0;

    dec_pp_if->inwidth = dec_cont->width;
    dec_pp_if->inheight = dec_cont->height;
    dec_pp_if->cropped_w = dec_cont->width;
    dec_pp_if->cropped_h = dec_cont->height;
    dec_pp_if->tiled_input_mode = dec_cont->tiled_reference_enable;
    dec_pp_if->progressive_sequence = 1;

    dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
    dec_pp_if->input_bus_luma = p_asic_buff->refBuffer->bus_address;
    output->output_frame_bus_address = p_asic_buff->refBuffer->bus_address;
    dec_pp_if->input_bus_chroma = dec_pp_if->input_bus_luma +
                                  dec_pp_if->inwidth * dec_pp_if->inheight;

    dec_pp_if->little_endian =
      GetDecRegister(dec_cont->vp6_regs, HWIF_DEC_OUT_ENDIAN);
    dec_pp_if->word_swap =
      GetDecRegister(dec_cont->vp6_regs, HWIF_DEC_OUTSWAP32_E);

    dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);

    TRACE_PP_CTRL("VP6DecNextPicture: PP wait to be done\n");

    dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);
    dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

    TRACE_PP_CTRL("VP6DecNextPicture: PP Finished\n");
  }

  if (pic_for_output) {
    const struct DWLLinearMem *out_pic = NULL;

    /* HANSKAA output, out_pic jos !PP tai pipeline ja !ref_to_out, muutoin
     * refPic */

    if ( (dec_cont->pp.pp_instance == NULL ||
          dec_cont->pp.dec_pp_if.use_pipeline) && !dec_cont->ref_to_out)
      out_pic = p_asic_buff->prev_out_buffer;
    else
      out_pic = p_asic_buff->refBuffer;

    dec_cont->out_count--;

    output->p_output_frame = out_pic->virtual_address;
    output->output_frame_bus_address = out_pic->bus_address;
    output->pic_id = 0;
    buffer_id = FindIndex(dec_cont, output->p_output_frame);
    output->decode_id = dec_cont->asic_buff->decode_id[buffer_id];
    if(dec_cont->pb.FrameType == BASE_FRAME)
      output->pic_coding_type = DEC_PIC_TYPE_I;
    else
      output->pic_coding_type = DEC_PIC_TYPE_P;
    output->is_intra_frame = 0;
    output->is_golden_frame = 0;
    output->nbr_of_err_mbs = 0;
    output->output_format = dec_cont->tiled_reference_enable ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

    output->frame_width = dec_cont->width;
    output->frame_height = dec_cont->height;

    DEC_API_TRC("VP6DecNextPicture# VP6DEC_PIC_RDY\n");

    dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;

    return (VP6DEC_PIC_RDY);
  }

  DEC_API_TRC("VP6DecNextPicture# VP6DEC_OK\n");
  return (VP6DEC_OK);

}

#ifdef USE_OUTPUT_RELEASE
/*------------------------------------------------------------------------------
    Function name   : VP6DecNextPicture_INTERNAL
    Description     :
    Return type     : VP6DecRet
    Argument        : VP6DecInst dec_inst
------------------------------------------------------------------------------*/
VP6DecRet VP6DecNextPicture_INTERNAL(VP6DecInst dec_inst,
                                     VP6DecPicture * output, u32 end_of_stream) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 pic_for_output = 0;
  u32 buffer_id;

  DEC_API_TRC("VP6DecNextPicture_INTERNAL#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP6DecNextPicture_INTERNAL# ERROR: dec_inst or output is NULL\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecNextPicture_INTERNAL# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }

  pic_for_output = dec_cont->out_count != 0;

  if (pic_for_output) {
    const struct DWLLinearMem *out_pic = NULL;

    if ( !dec_cont->ref_to_out)
      out_pic = p_asic_buff->prev_out_buffer;
    else
      out_pic = p_asic_buff->refBuffer;

    dec_cont->out_count--;

    output->p_output_frame = out_pic->virtual_address;
    output->output_frame_bus_address = out_pic->bus_address;
    output->pic_id = 0;

    if(dec_cont->pb.FrameType == BASE_FRAME)
      output->pic_coding_type = DEC_PIC_TYPE_I;
    else
      output->pic_coding_type = DEC_PIC_TYPE_P;

    output->is_intra_frame = 0;
    output->is_golden_frame = 0;
    output->nbr_of_err_mbs = 0;
    output->output_format = dec_cont->tiled_reference_enable ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

#if 0
    output->frame_width = dec_cont->width;
    output->frame_height = dec_cont->height;
#endif
    buffer_id = FindIndex(dec_cont, output->p_output_frame);

    output->frame_width = dec_cont->asic_buff->frame_width[buffer_id];
    output->frame_height = dec_cont->asic_buff->frame_height[buffer_id];

    output->decode_id = dec_cont->asic_buff->decode_id[buffer_id];

#ifdef USE_PICTURE_DISCARD
    if (dec_cont->asic_buff->first_show[buffer_id])
#endif
    {

#ifndef USE_PICTURE_DISCARD
      /* wait this buffer as unused */
      if (BqueueWaitBufNotInUse(&dec_cont->bq, buffer_id) != HANTRO_OK)
        return VP6DEC_ABORTED;
#endif

      dec_cont->asic_buff->first_show[buffer_id] = 0;

      /* set this buffer as used */
      BqueueSetBufferAsUsed(&dec_cont->bq, buffer_id);

      dec_cont->asic_buff->picture_info[buffer_id] = *output;
      FifoPush(dec_cont->fifo_display, buffer_id, FIFO_EXCEPTION_DISABLE);
    }

    DEC_API_TRC("VP6DecNextPicture_INTERNAL# VP6DEC_PIC_RDY\n");

    return (VP6DEC_PIC_RDY);
  }

  DEC_API_TRC("VP6DecNextPicture_INTERNAL# VP6DEC_OK\n");
  return (VP6DEC_OK);

}


/*------------------------------------------------------------------------------
    Function name   : VP6DecPictureConsumed
    Description     :
    Return type     : VP6DecRet
    Argument        : VP6DecInst dec_inst
------------------------------------------------------------------------------*/
VP6DecRet VP6DecPictureConsumed(VP6DecInst dec_inst, VP6DecPicture * output) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;
  u32 buffer_id;

  DEC_API_TRC("VP6DecPictureConsumed#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP6DecPictureConsumed# ERROR: dec_inst or output is NULL\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecPictureConsumed# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }

  buffer_id = FindIndex(dec_cont, output->p_output_frame);

  /* Remove the reference to the buffer. */
  BqueuePictureRelease(&dec_cont->bq, buffer_id);

  DEC_API_TRC("VP6DecPictureConsumed# VP6DEC_OK\n");
  return (VP6DEC_OK);
}

VP6DecRet VP6DecEndOfStream(VP6DecInst dec_inst, u32 strm_end_flag) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *)dec_inst;
  VP6DecRet ret;
  VP6DecPicture output;

  DEC_API_TRC("VP6DecEndOfStream#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL) {
    DEC_API_TRC("VP6DecEndOfStream# ERROR: dec_inst or output is NULL\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecEndOfStream# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);
  if(dec_cont->dec_stat == VP6DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (VP6DEC_OK);
  }

  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->vp6_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  ret = VP6DecNextPicture_INTERNAL(dec_inst, &output, 1);
  if(ret == VP6DEC_ABORTED) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (VP6DEC_ABORTED);
  }

  if(strm_end_flag) {
    dec_cont->dec_stat = VP6DEC_END_OF_STREAM;
    FifoPush(dec_cont->fifo_display, -1, FIFO_EXCEPTION_DISABLE);
  }

  /* Wait all buffers as unused */
  //if(!strm_end_flag)
  //  BqueueWaitNotInUse(&dec_cont->bq);

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  DEC_API_TRC("VP6DecEndOfStream# VP6DEC_OK\n");
  return (VP6DEC_OK);
}

static VP6DecRet VP6PushOutput(VP6DecContainer_t* dec_cont) {
  VP6DecRet ret;
  VP6DecPicture output;

  /* Sample dec_cont->out_count for Peek */
  dec_cont->fullness = dec_cont->out_count;
  ret = VP6DecNextPicture_INTERNAL(dec_cont, &output, 0);
  if(ret == VP6DEC_ABORTED)
    return (VP6DEC_ABORTED);
  else
    return ret;
}

#endif

static i32 FindIndex(VP6DecContainer_t* dec_cont, const u32* address) {
  i32 i;

  for (i = 0; i < (i32)dec_cont->num_buffers; i++) {
    if (dec_cont->asic_buff->pictures[i].virtual_address == address)
      break;
  }
  ASSERT((u32)i < dec_cont->num_buffers);

  return i;
}

/*------------------------------------------------------------------------------
    Function name   : VP6DecPeek
    Description     :
    Return type     : VP6DecRet
    Argument        : VP6DecInst dec_inst
------------------------------------------------------------------------------*/
VP6DecRet VP6DecPeek(VP6DecInst dec_inst, VP6DecPicture * output) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  const struct DWLLinearMem *out_pic = NULL;

  DEC_API_TRC("VP6DecPeek#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP6DecPeek# ERROR: dec_inst or output is NULL\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecPeek# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }

#ifdef USE_OUTPUT_RELEASE
  /* when output release thread enabled, VP6DecNextPicture_INTERNAL() called in
     VP6DecDecode(), and "dec_cont->out_count--" may called in VP6DecNextPicture()
     before VP6DecPeek() called, so dec_cont->fullness used to sample the real
     out_count in case of VP6DecNextPicture_INTERNAL() called before than VP6DecPeek() */
  u32 tmp = dec_cont->fullness;
#else
  u32 tmp = dec_cont->out_count;
#endif

  if (tmp == 0) {
    (void)DWLmemset(output, 0, sizeof(VP6DecPicture));
    return VP6DEC_OK;
  }

  out_pic = p_asic_buff->prev_out_buffer;

  output->p_output_frame = out_pic->virtual_address;
  output->output_frame_bus_address = out_pic->bus_address;
  output->pic_id = 0;
  output->decode_id = dec_cont->asic_buff->decode_id[p_asic_buff->prev_out_buffer_i];
  if(dec_cont->pb.FrameType == BASE_FRAME)
    output->pic_coding_type = DEC_PIC_TYPE_I;
  else
    output->pic_coding_type = DEC_PIC_TYPE_P;
  output->is_intra_frame = 0;
  output->is_golden_frame = 0;
  output->nbr_of_err_mbs = 0;

  output->frame_width = dec_cont->width;
  output->frame_height = dec_cont->height;

  DEC_API_TRC("VP6DecPeek# VP6DEC_PIC_RDY\n");

  return (VP6DEC_PIC_RDY);

}

#ifdef USE_EXTERNAL_BUFFER
void VP6SetExternalBufferInfo(VP6DecInst dec_inst) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *)dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 pic_buff_size;

  if(dec_cont->pp.pp_instance)
    pic_buff_size = p_asic_buff->width * ((p_asic_buff->height + 31) >> 5) * 32 * 3 / 2;
  else
    pic_buff_size = p_asic_buff->width * p_asic_buff->height * 3 / 2;

  u32 ref_buff_size = pic_buff_size;

  dec_cont->tot_buffers = dec_cont->buf_num = dec_cont->num_buffers;
  dec_cont->next_buf_size = ref_buff_size;
}

VP6DecRet VP6DecGetBufferInfo(VP6DecInst dec_inst, VP6DecBufferInfo *mem_info) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *)dec_inst;

  struct DWLLinearMem empty = {0, 0, 0};

  if(dec_cont == NULL || mem_info == NULL) {
    return VP6DEC_PARAM_ERROR;
  }

  if(dec_cont->buf_to_free == NULL && dec_cont->next_buf_size == 0) {
    /* External reference buffer: release done. */
    mem_info->buf_to_free = empty;
    mem_info->next_buf_size = dec_cont->next_buf_size;
    mem_info->buf_num = dec_cont->buf_num;
    return VP6DEC_OK;
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

  return VP6DEC_WAITING_FOR_BUFFER;
}

VP6DecRet VP6DecAddBuffer(VP6DecInst dec_inst, struct DWLLinearMem *info) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *)dec_inst;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  VP6DecRet dec_ret = VP6DEC_OK;

  if(dec_inst == NULL || info == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(info->virtual_address) ||
      X170_CHECK_BUS_ADDRESS_AGLINED(info->bus_address) ||
      info->size < dec_cont->next_buf_size) {
    return VP6DEC_PARAM_ERROR;
  }

  u32 i = dec_cont->buffer_index;

  dec_cont->n_ext_buf_size = info->size;

  if(i < dec_cont->tot_buffers) {
    p_asic_buff->pictures[i] = *info;
    dec_cont->buffer_index++;
    if(dec_cont->buffer_index < dec_cont->tot_buffers)
      dec_ret = VP6DEC_WAITING_FOR_BUFFER;
  } else {
    /* Adding extra buffers. */
    if(i >= 16) {
      /* Too much buffers added. */
      return VP6DEC_EXT_BUFFER_REJECTED;
    }

    p_asic_buff->pictures[i] = *info;

    dec_cont->buffer_index++;
    dec_cont->tot_buffers++;
    dec_cont->num_buffers++;
    dec_cont->bq.queue_size++;
  }
  return dec_ret;
}

#endif

#ifdef USE_OUTPUT_RELEASE
void VP6EnterAbortState(VP6DecContainer_t *dec_cont) {
  BqueueSetAbort(&dec_cont->bq);
#ifdef USE_OMXIL_BUFFER
  FifoSetAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 1;
}

void VP6ExistAbortState(VP6DecContainer_t *dec_cont) {
  BqueueClearAbort(&dec_cont->bq);
#ifdef USE_OMXIL_BUFFER
  FifoClearAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 0;
}

void VP6EmptyBufferQueue(VP6DecContainer_t *dec_cont) {
  BqueueEmpty(&dec_cont->bq);
}

void VP6StateReset(VP6DecContainer_t *dec_cont) {
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  u32 buffers = dec_cont->num_buffers_reserved;

/* Clear internal parameters in VP6DecContainer */
  dec_cont->dec_stat = VP6DEC_INITIALIZED;
  dec_cont->pic_number = 0;
#ifdef USE_EXTERNAL_BUFFER
#ifdef USE_OMXIL_BUFFER
  dec_cont->tot_buffers = buffers;
  dec_cont->buffer_index = 0;
#endif
  dec_cont->no_reallocation = 1;
#endif
#ifdef USE_OMXIL_BUFFER
  dec_cont->bq.queue_size = buffers;
  dec_cont->num_buffers = buffers;
#endif
  dec_cont->fullness = 0;
  dec_cont->ref_to_out = 0;
  dec_cont->out_count = 0;
  dec_cont->get_buffer_after_abort = 1;

  /* Clear internal parameters in DecAsicBuffers */
  (void) DWLmemset(p_asic_buff->decode_id, 0, 16 * sizeof(u32));
  (void) DWLmemset(p_asic_buff->first_show, 0, 16 * sizeof(u32));
#ifdef USE_OMXIL_BUFFER
  (void) DWLmemset(p_asic_buff->picture_info, 0, 16 * sizeof(VP6DecPicture));
#endif

  p_asic_buff->whole_pic_concealed = 0;
  p_asic_buff->prev_out_buffer = NULL;
  p_asic_buff->ref_buffer_i       = BQUEUE_UNUSED;
  p_asic_buff->golden_buffer_i    = BQUEUE_UNUSED;
  p_asic_buff->prev_out_buffer_i  = BQUEUE_UNUSED;
  p_asic_buff->out_buffer_i = 0;
  p_asic_buff->out_buffer = NULL;
  p_asic_buff->prev_out_buffer = NULL;

#ifdef USE_OMXIL_BUFFER
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
  FifoInit(16, &dec_cont->fifo_display);
#endif
  (void)buffers;
}

VP6DecRet VP6DecAbort(VP6DecInst dec_inst) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;

  DEC_API_TRC("VP6DecAbort#\n");
  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL) {
    DEC_API_TRC("VP6DecAbort# ERROR: dec_inst or output is NULL\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecAbort# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }
  pthread_mutex_lock(&dec_cont->protect_mutex);
  /* Abort frame buffer waiting */
  VP6EnterAbortState(dec_cont);

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  return (VP6DEC_OK);
}

VP6DecRet VP6DecAbortAfter(VP6DecInst dec_inst) {
  VP6DecContainer_t *dec_cont = (VP6DecContainer_t *) dec_inst;

  DEC_API_TRC("VP6DecAbortAfter#\n");
  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL) {
    DEC_API_TRC("VP6DecAbortAfter# ERROR: dec_inst or output is NULL\n");
    return (VP6DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP6DecAbortAfter# ERROR: Decoder not initialized\n");
    return (VP6DEC_NOT_INITIALIZED);
  }
  pthread_mutex_lock(&dec_cont->protect_mutex);

#if 0
  /* If normal EOS is waited, return directly */
  if(dec_cont->dec_stat == VP6DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (VP6DEC_OK);
  }
#endif

  /* Stop and release HW */
  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->vp6_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->vp6_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  /* Clear any remaining pictures from DPB */
  VP6EmptyBufferQueue(dec_cont);

  VP6StateReset(dec_cont);

  VP6ExistAbortState(dec_cont);
  pthread_mutex_unlock(&dec_cont->protect_mutex);

  DEC_API_TRC("VP6DecAbortAfter# VP6DEC_OK\n");
  return (VP6DEC_OK);
}
#endif

static u32 vp6hwdCheckSupport(VP6DecContainer_t *dec_cont)
{
  DWLHwConfig hw_config;
  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_VP6_DEC);

  if ( (dec_cont->asic_buff->width > hw_config.max_dec_pic_width) ||
     (dec_cont->asic_buff->width < dec_cont->min_dec_pic_width) ||
     (dec_cont->asic_buff->height < dec_cont->min_dec_pic_height) ||
     (dec_cont->asic_buff->width*dec_cont->asic_buff->height > MAX_PIC_SIZE) ) {
    return HANTRO_NOK;
  }

    return HANTRO_OK;
}
