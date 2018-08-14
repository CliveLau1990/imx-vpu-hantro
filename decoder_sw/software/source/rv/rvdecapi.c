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

#include "version.h"
#include "basetype.h"
#include "rv_container.h"
#include "rv_utils.h"
#include "rv_strm.h"
#include "rvdecapi.h"
#include "rvdecapi_internal.h"
#include "dwl.h"
#include "regdrv_g1.h"
#include "rv_headers.h"
#include "deccfg.h"
#include "refbuffer.h"
#include "rv_rpr.h"

#include "rv_debug.h"

#include "tiledref.h"
#include "commonconfig.h"
#include "errorhandling.h"

#ifdef RVDEC_TRACE
#define RV_API_TRC(str)    RvDecTrace((str))
#else
#define RV_API_TRC(str)

#endif

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

#define RV_BUFFER_UNDEFINED    16

#define RV_DEC_X170_MODE 8 /* TODO: What's the right mode for Hukka */

#define ID8170_DEC_TIMEOUT        0xFFU
#define ID8170_DEC_SYSTEM_ERROR   0xFEU
#define ID8170_DEC_HW_RESERVED    0xFDU

#define RVDEC_UPDATE_POUTPUT

#define RVDEC_NON_PIPELINE_AND_B_PICTURE \
        ((!dec_cont->pp_config_query.pipeline_accepted) \
             && dec_cont->FrameDesc.pic_coding_type == RV_B_PIC)

static u32 RvCheckFormatSupport(void);
static u32 rvHandleVlcModeError(DecContainer * dec_cont, u32 pic_num);
static void rvHandleFrameEnd(DecContainer * dec_cont);
static u32 RunDecoderAsic(DecContainer * dec_container, addr_t strm_bus_address);
static u32 RvSetRegs(DecContainer * dec_container, addr_t strm_bus_address);
static void RvPpControl(DecContainer * dec_container, u32 pipeline_off);
static void RvFillPicStruct(RvDecPicture * picture,
                            DecContainer * dec_cont, u32 pic_index);
static void RvPpMultiBufferInit(DecContainer * dec_cont);
static void RvPpMultiBufferSetup(DecContainer * dec_cont, u32 pipeline_off);
static void RvDecRunFullmode(DecContainer * dec_cont);
#ifdef USE_EXTERNAL_BUFFER
static void RvSetExternalBufferInfo(DecContainer * dec_cont);
#endif

#ifdef USE_OUTPUT_RELEASE
RvDecRet RvDecNextPicture_INTERNAL(RvDecInst dec_inst,
                                   RvDecPicture * picture, u32 end_of_stream);
#endif

#ifdef USE_OUTPUT_RELEASE
static void RvEnterAbortState(DecContainer *dec_cont);
static void RvExistAbortState(DecContainer *dec_cont);
static void RvEmptyBufferQueue(DecContainer *dec_cont);
#endif
/*------------------------------------------------------------------------------
       Version Information - DO NOT CHANGE!
------------------------------------------------------------------------------*/

#define RVDEC_MAJOR_VERSION 0
#define RVDEC_MINOR_VERSION 0

/*------------------------------------------------------------------------------

    Function: RvDecGetAPIVersion

        Functional description:
            Return version information of API

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

RvDecApiVersion RvDecGetAPIVersion() {
  RvDecApiVersion ver;

  ver.major = RVDEC_MAJOR_VERSION;
  ver.minor = RVDEC_MINOR_VERSION;

  return (ver);
}

/*------------------------------------------------------------------------------

    Function: RvDecGetBuild

        Functional description:
            Return build information of SW and HW

        Returns:
            RvDecBuild

------------------------------------------------------------------------------*/

RvDecBuild RvDecGetBuild(void) {
  RvDecBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_RV_DEC);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_RV_DEC);

  RV_API_TRC("RvDecGetBuild# OK\n");

  return build_info;
}

/*------------------------------------------------------------------------------

    Function: RvDecInit()

        Functional description:
            Initialize decoder software. Function reserves memory for the
            decoder instance.

        Inputs:
            error_handling
                            Flag to determine which error concealment method to use.

        Outputs:
            dec_inst        pointer to initialized instance is stored here

        Returns:
            RVDEC_OK       successfully initialized the instance
            RVDEC_MEM_FAIL memory allocation failed

------------------------------------------------------------------------------*/
RvDecRet RvDecInit(RvDecInst * dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                   const void *dwl,
#endif
                   enum DecErrorHandling error_handling,
                   u32 frame_code_length, u32 *frame_sizes, u32 rv_version,
                   u32 max_frame_width, u32 max_frame_height,
                   u32 num_frame_buffers, enum DecDpbFlags dpb_flags,
                   u32 use_adaptive_buffers, u32 n_guard_size,
                   struct DecDownscaleCfg *dscale_cfg) {
  /*@null@ */ DecContainer *dec_cont;
#ifndef USE_EXTERNAL_BUFFER
  /*@null@ */ const void *dwl;
#endif
  u32 i = 0;
  u32 ret;
  u32 reference_frame_format;

#ifndef USE_EXTERNAL_BUFFER
  struct DWLInitParam dwl_init;
#endif
  DWLHwConfig config;

  RV_API_TRC("RvDecInit#");
  RVDEC_API_DEBUG(("RvAPI_DecoderInit#"));

  /* check that right shift on negative numbers is performed signed */
  /*lint -save -e* following check causes multiple lint messages */
  if(((-1) >> 1) != (-1)) {
    RVDEC_API_DEBUG(("RVDecInit# ERROR: Right shift is not signed"));
    return (RVDEC_INITFAIL);
  }
  /*lint -restore */

  if(dec_inst == NULL) {
    RVDEC_API_DEBUG(("RVDecInit# ERROR: dec_inst == NULL"));
    return (RVDEC_PARAM_ERROR);
  }

  *dec_inst = NULL;

  /* check that RV decoding supported in HW */
  if(RvCheckFormatSupport()) {
    RVDEC_API_DEBUG(("RVDecInit# ERROR: RV not supported in HW\n"));
    return RVDEC_FORMAT_NOT_SUPPORTED;
  }

#ifndef USE_EXTERNAL_BUFFER
  dwl_init.client_type = DWL_CLIENT_TYPE_RV_DEC;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    RVDEC_API_DEBUG(("RVDecInit# ERROR: DWL Init failed"));
    return (RVDEC_DWL_ERROR);
  }
#endif

  RVDEC_API_DEBUG(("size of DecContainer %d \n", sizeof(DecContainer)));
  dec_cont = (DecContainer *) DWLmalloc(sizeof(DecContainer));

  if(dec_cont == NULL) {
#ifndef USE_EXTERNAL_BUFFER
    (void) DWLRelease(dwl);
#endif
    return (RVDEC_MEMFAIL);
  }

  /* set everything initially zero */
  (void) DWLmemset(dec_cont, 0, sizeof(DecContainer));

  ret = DWLMallocLinear(dwl,
                        RV_DEC_X170_MAX_NUM_SLICES*sizeof(u32),
                        &dec_cont->StrmStorage.slices);

  if( ret == HANTRO_NOK ) {
    DWLfree(dec_cont);
#ifndef USE_EXTERNAL_BUFFER
    (void) DWLRelease(dwl);
#endif
    return (RVDEC_MEMFAIL);
  }

  dec_cont->dwl = dwl;

  pthread_mutex_init(&dec_cont->protect_mutex, NULL);

  dec_cont->ApiStorage.DecStat = INITIALIZED;

  *dec_inst = (DecContainer *) dec_cont;

  for(i = 0; i < TOTAL_X170_REGISTERS; i++)
    dec_cont->rv_regs[i] = 0;

  SetCommonConfigRegs(dec_cont->rv_regs,DWL_CLIENT_TYPE_RV_DEC );

  (void) DWLmemset(&config, 0, sizeof(DWLHwConfig));

  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_RV_DEC);

  if(!config.addr64_support && sizeof(void *) == 8) {
    RVDEC_API_DEBUG("RVDecInit# ERROR: HW not support 64bit address!\n");
    return (RVDEC_PARAM_ERROR);
  }

  i = DWLReadAsicID(DWL_CLIENT_TYPE_RV_DEC) >> 16;
  if(i == 0x8170U)
    error_handling = 0;
  dec_cont->ref_buf_support = config.ref_buf_support;
  reference_frame_format = dpb_flags & DEC_REF_FRM_FMT_MASK;
  if(reference_frame_format == DEC_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!config.tiled_mode_support) {
      return RVDEC_FORMAT_NOT_SUPPORTED;
    }
    dec_cont->tiled_mode_support = config.tiled_mode_support;
  } else
    dec_cont->tiled_mode_support = 0;

  /* Down scaler ratio */
  if ((dscale_cfg->down_scale_x == 0) || (dscale_cfg->down_scale_y == 0)) {
    dec_cont->pp_enabled = 0;
    dec_cont->down_scale_x = 0;   /* Meaningless when pp not enabled. */
    dec_cont->down_scale_y = 0;
  } else if ((dscale_cfg->down_scale_x != 1 &&
              dscale_cfg->down_scale_x != 2 &&
              dscale_cfg->down_scale_x != 4 &&
              dscale_cfg->down_scale_x != 8 ) ||
             (dscale_cfg->down_scale_y != 1 &&
              dscale_cfg->down_scale_y != 2 &&
              dscale_cfg->down_scale_y != 4 &&
              dscale_cfg->down_scale_y != 8 )) {
    return (RVDEC_PARAM_ERROR);
  } else {
    u32 scale_table[9] = {0, 0, 1, 0, 2, 0, 0, 0, 3};

    dec_cont->pp_enabled = 1;
    dec_cont->down_scale_x = dscale_cfg->down_scale_x;
    dec_cont->down_scale_y = dscale_cfg->down_scale_y;
    dec_cont->dscale_shift_x = scale_table[dscale_cfg->down_scale_x];
    dec_cont->dscale_shift_y = scale_table[dscale_cfg->down_scale_y];
  }

  dec_cont->pp_buffer_queue = InputQueueInit(0);
  if (dec_cont->pp_buffer_queue == NULL) {
    return (RVDEC_MEMFAIL);
  }
  dec_cont->StrmStorage.release_buffer = 0;
  dec_cont->StrmStorage.intra_freeze = error_handling == DEC_EC_VIDEO_FREEZE;
  if (error_handling == DEC_EC_PARTIAL_FREEZE)
    dec_cont->StrmStorage.partial_freeze = 1;
  else if (error_handling == DEC_EC_PARTIAL_IGNORE)
    dec_cont->StrmStorage.partial_freeze = 2;
  dec_cont->StrmStorage.picture_broken = HANTRO_FALSE;
  if(num_frame_buffers > 16)
    num_frame_buffers = 16;
  dec_cont->StrmStorage.max_num_buffers = num_frame_buffers;

  dec_cont->StrmStorage.is_rv8 = rv_version == 0;
  if (rv_version == 0 && frame_sizes != NULL) {
    dec_cont->StrmStorage.frame_code_length = frame_code_length;
    DWLmemcpy(dec_cont->StrmStorage.frame_sizes, frame_sizes,
              18*sizeof(u32));
    SetDecRegister(dec_cont->rv_regs, HWIF_FRAMENUM_LEN,
                   frame_code_length);
  }

  /* prediction filter taps */
  if (rv_version == 0) {
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_0_0, -1);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_0_1, 12);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_0_2,  6);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_1_0,  6);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_1_1,  9);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_1_2,  1);
  } else {
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_0_0,  1);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_0_1, -5);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_0_2, 20);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_1_0,  1);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_1_1, -5);
    SetDecRegister(dec_cont->rv_regs, HWIF_PRED_BC_TAP_1_2, 52);
  }

  SetDecRegister(dec_cont->rv_regs, HWIF_DEC_MODE, RV_DEC_X170_MODE);
  SetDecRegister(dec_cont->rv_regs, HWIF_RV_PROFILE, rv_version != 0);

  dec_cont->StrmStorage.max_frame_width = max_frame_width;
  dec_cont->StrmStorage.max_frame_height = max_frame_height;
  dec_cont->StrmStorage.max_mbs_per_frame =
    ((dec_cont->StrmStorage.max_frame_width +15)>>4)*
    ((dec_cont->StrmStorage.max_frame_height+15)>>4);

  InitWorkarounds(RV_DEC_X170_MODE, &dec_cont->workarounds);

#ifdef USE_OUTPUT_RELEASE
  /* take top/botom fields into consideration */
  if (FifoInit(32, &dec_cont->fifo_display) != FIFO_OK)
    return RVDEC_MEMFAIL;
#endif
#ifdef USE_EXTERNAL_BUFFER
  dec_cont->no_reallocation = 1;
#endif

  dec_cont->use_adaptive_buffers = use_adaptive_buffers;
  dec_cont->n_guard_size = n_guard_size;

  /* If tile mode is enabled, should take DTRC minimum size(96x48) into consideration */
  if (dec_cont->tiled_mode_support) {
    dec_cont->min_dec_pic_width = RV_MIN_WIDTH_EN_DTRC;
    dec_cont->min_dec_pic_height = RV_MIN_HEIGHT_EN_DTRC;
  }
  else {
    dec_cont->min_dec_pic_width = RV_MIN_WIDTH;
    dec_cont->min_dec_pic_height = RV_MIN_HEIGHT;
  }

  RVDEC_API_DEBUG(("Container 0x%x\n", (u32) dec_cont));
  RV_API_TRC("RvDecInit: OK\n");

  return (RVDEC_OK);
}

/*------------------------------------------------------------------------------

    Function: RvDecGetInfo()

        Functional description:
            This function provides read access to decoder information. This
            function should not be called before RvDecDecode function has
            indicated that headers are ready.

        Inputs:
            dec_inst     decoder instance

        Outputs:
            dec_info    pointer to info struct where data is written

        Returns:
            RVDEC_OK            success
            RVDEC_PARAM_ERROR     invalid parameters

------------------------------------------------------------------------------*/
RvDecRet RvDecGetInfo(RvDecInst dec_inst, RvDecInfo * dec_info) {

  DecFrameDesc *p_frame_d;
  DecApiStorage *p_api_stor;
  DecHdrs *p_hdrs;

  RV_API_TRC("RvDecGetInfo#");

  if(dec_inst == NULL || dec_info == NULL) {
    return RVDEC_PARAM_ERROR;
  }

  p_api_stor = &((DecContainer*)dec_inst)->ApiStorage;
  p_frame_d = &((DecContainer*)dec_inst)->FrameDesc;
  p_hdrs = &((DecContainer*)dec_inst)->Hdrs;

  dec_info->multi_buff_pp_size = 2;
#ifdef USE_EXTERNAL_BUFFER
  dec_info->pic_buff_size = ((DecContainer *)dec_inst)->buf_num;
#endif

  if(p_api_stor->DecStat == UNINIT || p_api_stor->DecStat == INITIALIZED) {
    return RVDEC_HDRS_NOT_RDY;
  }

  if (!((DecContainer *)dec_inst)->pp_enabled) {
    dec_info->frame_width = p_frame_d->frame_width;
    dec_info->frame_height = p_frame_d->frame_height;

    dec_info->coded_width = p_hdrs->horizontal_size;
    dec_info->coded_height = p_hdrs->vertical_size;
  } else {
    dec_info->frame_width = p_frame_d->frame_width >> ((DecContainer *)dec_inst)->dscale_shift_x;
    dec_info->frame_height = p_frame_d->frame_height >> ((DecContainer *)dec_inst)->dscale_shift_y;

    dec_info->coded_width = p_hdrs->horizontal_size >> ((DecContainer *)dec_inst)->dscale_shift_x;
    dec_info->coded_height = p_hdrs->vertical_size >> ((DecContainer *)dec_inst)->dscale_shift_y;
  }

  dec_info->dpb_mode = DEC_DPB_FRAME;

  if(((DecContainer *)dec_inst)->tiled_mode_support)
    dec_info->output_format = RVDEC_TILED_YUV420;
  else
    dec_info->output_format = RVDEC_SEMIPLANAR_YUV420;

  RV_API_TRC("RvDecGetInfo: OK");
  return (RVDEC_OK);

}

/*------------------------------------------------------------------------------

    Function: RvDecDecode

        Functional description:
            Decode stream data. Calls StrmDec_Decode to do the actual decoding.

        Input:
            dec_inst     decoder instance
            input      pointer to input struct

        Outputs:
            output     pointer to output struct

        Returns:
            RVDEC_NOT_INITIALIZED   decoder instance not initialized yet
            RVDEC_PARAM_ERROR       invalid parameters

            RVDEC_STRM_PROCESSED    stream buffer decoded
            RVDEC_HDRS_RDY          headers decoded
            RVDEC_PIC_DECODED       decoding of a picture finished
            RVDEC_STRM_ERROR        serious error in decoding, no
                                       valid parameter sets available
                                       to decode picture data

------------------------------------------------------------------------------*/

RvDecRet RvDecDecode(RvDecInst dec_inst,
                     RvDecInput * input, RvDecOutput * output) {

  DecContainer *dec_cont;
  RvDecRet internal_ret;
  DecApiStorage *p_api_stor;
  DecStrmDesc *p_strm_desc;
  u32 strm_dec_result;
  u32 asic_status;
  i32 ret = 0;
  u32 error_concealment = 0;
  u32 i;
  u32 *slice_info;
  u32 contains_invalid_slice = HANTRO_FALSE;

  RV_API_TRC("\nRv_dec_decode#");

  if(input == NULL || output == NULL || dec_inst == NULL) {
    RV_API_TRC("RvDecDecode# PARAM_ERROR\n");
    return RVDEC_PARAM_ERROR;
  }

  dec_cont = ((DecContainer *) dec_inst);
  p_api_stor = &dec_cont->ApiStorage;
  p_strm_desc = &dec_cont->StrmDesc;

  /*
   *  Check if decoder is in an incorrect mode
   */
  if(p_api_stor->DecStat == UNINIT) {

    RV_API_TRC("RvDecDecode: NOT_INITIALIZED\n");
    return RVDEC_NOT_INITIALIZED;
  }

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort) {
    return (RVDEC_ABORTED);
  }
#endif

  if(input->data_len == 0 ||
      input->data_len > DEC_X170_MAX_STREAM ||
      input->stream == NULL || input->stream_bus_address == 0) {
    RV_API_TRC("RvDecDecode# PARAM_ERROR\n");
    return RVDEC_PARAM_ERROR;
  }

  /* If we have set up for delayed resolution change, do it here */
  if(dec_cont->StrmStorage.rpr_detected) {
    u32 new_width, new_height;
    u32 num_pics_resampled;
    u32 resample_pics[2];
    struct DWLLinearMem tmp_data;

    dec_cont->StrmStorage.rpr_detected = 0;

    num_pics_resampled = 0;
    if( dec_cont->StrmStorage.rpr_next_pic_type == RV_P_PIC) {
      resample_pics[0] = dec_cont->StrmStorage.work0;
      num_pics_resampled = 1;
    } else if ( dec_cont->StrmStorage.rpr_next_pic_type == RV_B_PIC ) {
      /* B picture resampling not supported (would affect picture output
       * order and co-located MB data). So let's skip B frames until
       * next reference picture. */
      dec_cont->StrmStorage.skip_b = 1;
    }

    /* Allocate extra picture buffer for resampling */
    if( num_pics_resampled ) {
      internal_ret = rvAllocateRprBuffer( dec_cont );
      if( internal_ret != RVDEC_OK ) {
        RVDEC_DEBUG(("ALLOC RPR BUFFER FAIL\n"));
        RV_API_TRC("RvDecDecode# MEMFAIL\n");
        return (internal_ret);
      }
    }

    new_width = dec_cont->tmp_hdrs.horizontal_size;
    new_height = dec_cont->tmp_hdrs.vertical_size;

    /* Resample ref picture(s). Should be safe to do at this point; all
     * original size pictures are output before this point. */
    for( i = 0 ; i < num_pics_resampled ; ++i ) {
      u32 j = resample_pics[i];
      picture_t * p_ref_pic;

      p_ref_pic = &dec_cont->StrmStorage.p_pic_buf[j];

      if( p_ref_pic->coded_width == new_width &&
          p_ref_pic->coded_height == new_height )
        continue;

      rvRpr( p_ref_pic,
             &dec_cont->StrmStorage.p_rpr_buf,
             &dec_cont->StrmStorage.rpr_work_buffer,
             0 /*round*/,
             new_width,
             new_height,
             dec_cont->tiled_reference_enable);

      p_ref_pic->coded_width = new_width;
      p_ref_pic->frame_width = ( 15 + new_width ) & ~15;
      p_ref_pic->coded_height = new_height;
      p_ref_pic->frame_height = ( 15 + new_height ) & ~15;

      tmp_data = dec_cont->StrmStorage.p_rpr_buf.data;
      dec_cont->StrmStorage.p_rpr_buf.data = p_ref_pic->data;
      p_ref_pic->data = tmp_data;
    }

    dec_cont->Hdrs.horizontal_size = new_width;
    dec_cont->Hdrs.vertical_size = new_height;

    SetDecRegister(dec_cont->rv_regs, HWIF_PIC_MB_WIDTH,
                   dec_cont->FrameDesc.frame_width);
    SetDecRegister(dec_cont->rv_regs, HWIF_PIC_MB_HEIGHT_P,
                   dec_cont->FrameDesc.frame_height);
    SetDecRegister(dec_cont->rv_regs, HWIF_PIC_MB_H_EXT,
                   dec_cont->FrameDesc.frame_height >> 8);

    if(dec_cont->ref_buf_support) {
      RefbuInit(&dec_cont->ref_buffer_ctrl,
                RV_DEC_X170_MODE,
                dec_cont->FrameDesc.frame_width,
                dec_cont->FrameDesc.frame_height,
                dec_cont->ref_buf_support);
    }

    dec_cont->StrmStorage.strm_dec_ready = HANTRO_TRUE;
  }

  if(p_api_stor->DecStat == HEADERSDECODED) {
#ifdef USE_EXTERNAL_BUFFER
    if(!dec_cont->no_reallocation)
#endif
    {
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp_instance == NULL) {
#ifndef USE_EXT_BUF_SAFE_RELEASE
        BqueueMarkNotInUse(&dec_cont->StrmStorage.bq);
#else
        BqueueWaitNotInUse(&dec_cont->StrmStorage.bq);
        if (dec_cont->pp_enabled)
          InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);
#endif
      }
#endif

#ifdef USE_EXTERNAL_BUFFER
      if (dec_cont->pp_enabled && dec_cont->StrmStorage.ext_buffer_added) {
        dec_cont->StrmStorage.release_buffer = 1;
        return RVDEC_WAITING_FOR_BUFFER;
      }
#endif
      rvFreeBuffers(dec_cont);

      if( dec_cont->StrmStorage.max_frame_width == 0 ) {
        dec_cont->StrmStorage.max_frame_width =
          dec_cont->Hdrs.horizontal_size;
        dec_cont->StrmStorage.max_frame_height =
          dec_cont->Hdrs.vertical_size;
        dec_cont->StrmStorage.max_mbs_per_frame =
          ((dec_cont->StrmStorage.max_frame_width +15)>>4)*
          ((dec_cont->StrmStorage.max_frame_height+15)>>4);
      }

#ifndef USE_EXTERNAL_BUFFER
      if(!dec_cont->StrmStorage.p_pic_buf[0].data.bus_address)
#else
      if(!dec_cont->StrmStorage.direct_mvs.virtual_address)
#endif
      {
        RVDEC_DEBUG(("Allocate buffers\n"));
        internal_ret = rvAllocateBuffers(dec_cont);
        if(internal_ret != RVDEC_OK) {
          RVDEC_DEBUG(("ALLOC BUFFER FAIL\n"));
          RV_API_TRC("RvDecDecode# MEMFAIL\n");
          return (internal_ret);
        }
      }

      /* Headers ready now, mems allocated, decoding can start */
#ifndef USE_EXTERNAL_BUFFER
      p_api_stor->DecStat = STREAMDECODING;
#endif
    }
  }

  /*
   *  Update stream structure
   */
  p_strm_desc->p_strm_buff_start = input->stream;
  p_strm_desc->strm_curr_pos = input->stream;
  p_strm_desc->bit_pos_in_word = 0;
  p_strm_desc->strm_buff_size = input->data_len;
  p_strm_desc->strm_buff_read_bits = 0;

  dec_cont->StrmStorage.num_slices = input->slice_info_num+1;
  /* Limit maximum n:o of slices
   * (TODO, should we report an error?) */
  if(dec_cont->StrmStorage.num_slices > RV_DEC_X170_MAX_NUM_SLICES)
    dec_cont->StrmStorage.num_slices = RV_DEC_X170_MAX_NUM_SLICES;
  slice_info = dec_cont->StrmStorage.slices.virtual_address;

#ifdef RV_RAW_STREAM_SUPPORT
  dec_cont->StrmStorage.raw_mode = input->slice_info_num == 0;
#endif

  /* convert slice offsets into slice sizes, TODO: check if memory given by application is writable */
  if (p_api_stor->DecStat == STREAMDECODING
#ifdef RV_RAW_STREAM_SUPPORT
      && !dec_cont->StrmStorage.raw_mode
#endif
     )
    /* Copy offsets to HW external memory */
    for( i = 0 ; i < input->slice_info_num ; ++i ) {
      i32 tmp;
      if( i == input->slice_info_num-1 )
        tmp = input->data_len;
      else
        tmp = input->slice_info[i+1].offset;
      slice_info[i] = tmp - input->slice_info[i].offset;
      if(!input->slice_info[i].is_valid) {
        contains_invalid_slice = HANTRO_TRUE;
      }
    }

#ifdef _DEC_PP_USAGE
  dec_cont->StrmStorage.latest_id = input->pic_id;
#endif

  if(contains_invalid_slice) {
    /* If stream contains even one invalid slice, we freeze the whole
     * picture. At this point we could try to do some advanced
     * error concealment stuff */
    RVDEC_API_DEBUG(("STREAM ERROR; LEAST ONE SLICE BROKEN\n"));
    RVFLUSH;
    dec_cont->FrameDesc.pic_coding_type = RV_P_PIC;
    ret = rvHandleVlcModeError(dec_cont, input->pic_id);
    error_concealment = HANTRO_TRUE;
    RVDEC_UPDATE_POUTPUT;
    ret = RVDEC_PIC_DECODED;
  } else { /* All slices OK */
    /* TODO: do we need loop? (maybe if many slices?) */
    do {
      RVDEC_API_DEBUG(("Start Decode\n"));
      /* run SW if HW is not in the middle of processing a picture
       * (indicated as HW_PIC_STARTED decoder status) */
#ifdef USE_EXTERNAL_BUFFER
      if(p_api_stor->DecStat == HEADERSDECODED) {
        p_api_stor->DecStat = STREAMDECODING;
        if (!dec_cont->no_reallocation) {
          dec_cont->buffer_index = 0;
          dec_cont->no_reallocation = 1;
          ret = RVDEC_WAITING_FOR_BUFFER;
        }
      } else if(p_api_stor->DecStat != HW_PIC_STARTED)
#else
      if (p_api_stor->DecStat != HW_PIC_STARTED)
#endif
      {
        strm_dec_result = rv_StrmDecode(dec_cont);
        switch (strm_dec_result) {
        case DEC_PIC_HDR_RDY:
          /* if type inter predicted and no reference -> error */
          if((dec_cont->FrameDesc.pic_coding_type == RV_P_PIC &&
              dec_cont->StrmStorage.work0 == INVALID_ANCHOR_PICTURE) ||
              (dec_cont->FrameDesc.pic_coding_type == RV_B_PIC &&
               (dec_cont->StrmStorage.work1 == INVALID_ANCHOR_PICTURE ||
                dec_cont->StrmStorage.work0 == INVALID_ANCHOR_PICTURE ||
                dec_cont->StrmStorage.skip_b ||
                input->skip_non_reference)) ||
              (dec_cont->FrameDesc.pic_coding_type == RV_P_PIC &&
               dec_cont->StrmStorage.picture_broken &&
               dec_cont->StrmStorage.intra_freeze)) {
            if(dec_cont->StrmStorage.skip_b ||
                input->skip_non_reference ) {
              RV_API_TRC("RvDecDecode# RVDEC_NONREF_PIC_SKIPPED\n");
            }
            ret = rvHandleVlcModeError(dec_cont, input->pic_id);
            error_concealment = HANTRO_TRUE;
          } else {
            p_api_stor->DecStat = HW_PIC_STARTED;
          }
          break;

        case DEC_PIC_HDR_RDY_ERROR:
          ret = rvHandleVlcModeError(dec_cont, input->pic_id);
          error_concealment = HANTRO_TRUE;
          /* copy output parameters for this PIC */
          RVDEC_UPDATE_POUTPUT;
          break;

        case DEC_PIC_HDR_RDY_RPR:
          dec_cont->StrmStorage.strm_dec_ready = FALSE;
          p_api_stor->DecStat = STREAMDECODING;

          if(dec_cont->ref_buf_support) {
            RefbuInit(&dec_cont->ref_buffer_ctrl,
                      RV_DEC_X170_MODE,
                      dec_cont->FrameDesc.frame_width,
                      dec_cont->FrameDesc.frame_height,
                      dec_cont->ref_buf_support);
          }
          ret = RVDEC_STRM_PROCESSED;
          break;

        case DEC_HDRS_RDY:
          internal_ret = rvDecCheckSupport(dec_cont);
          if(internal_ret != RVDEC_OK) {
            dec_cont->StrmStorage.strm_dec_ready = FALSE;
            p_api_stor->DecStat = INITIALIZED;
            return internal_ret;
          }

          dec_cont->ApiStorage.first_headers = 0;

          SetDecRegister(dec_cont->rv_regs, HWIF_PIC_MB_WIDTH,
                         dec_cont->FrameDesc.frame_width);
          SetDecRegister(dec_cont->rv_regs, HWIF_PIC_MB_HEIGHT_P,
                         dec_cont->FrameDesc.frame_height);
          SetDecRegister(dec_cont->rv_regs, HWIF_PIC_MB_H_EXT,
                         dec_cont->FrameDesc.frame_height >> 8);

          if(dec_cont->ref_buf_support) {
            RefbuInit(&dec_cont->ref_buffer_ctrl,
                      RV_DEC_X170_MODE,
                      dec_cont->FrameDesc.frame_width,
                      dec_cont->FrameDesc.frame_height,
                      dec_cont->ref_buf_support);
          }

          p_api_stor->DecStat = HEADERSDECODED;
#ifdef USE_EXTERNAL_BUFFER
          RvSetExternalBufferInfo(dec_cont);
          if (dec_cont->no_reallocation &&
              (!dec_cont->use_adaptive_buffers ||
               (dec_cont->use_adaptive_buffers &&
                dec_cont->tot_buffers + dec_cont->n_guard_size <= dec_cont->tot_buffers_added)))
            ret = RVDEC_STRM_PROCESSED;
          else
#endif
          {
#ifdef USE_OUTPUT_RELEASE
            FifoPush(dec_cont->fifo_display, -2, FIFO_EXCEPTION_DISABLE);
#endif
            RVDEC_API_DEBUG(("HDRS_RDY\n"));
            ret = RVDEC_HDRS_RDY;
          }

          output->data_left = input->data_len;
          if(dec_cont->StrmStorage.rpr_detected)
          ret = RVDEC_STRM_PROCESSED;
          return ret;

        default:
          output->data_left = 0;
          //ASSERT(strm_dec_result == DEC_END_OF_STREAM);
          if(dec_cont->StrmStorage.rpr_detected) {
            ret = RVDEC_PIC_DECODED;
          } else {
            ret = RVDEC_STRM_PROCESSED;
          }
          return ret;
        }
      }

      /* picture header properly decoded etc -> start HW */
      if(p_api_stor->DecStat == HW_PIC_STARTED) {
        if (!dec_cont->asic_running) {
#ifdef USE_OUTPUT_RELEASE
          if(dec_cont->pp_instance == NULL) {
            dec_cont->StrmStorage.work_out =
              BqueueNext2( &dec_cont->StrmStorage.bq,
                           dec_cont->StrmStorage.work0,
                           dec_cont->StrmStorage.work1,
                           BQUEUE_UNUSED,
                           dec_cont->FrameDesc.pic_coding_type == RV_B_PIC );
            if(dec_cont->StrmStorage.work_out == (u32)0xFFFFFFFFU) {
              if (dec_cont->abort)
                return RVDEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
              else {
                output->strm_curr_pos = input->stream;
                output->strm_curr_bus_address = input->stream_bus_address;
                output->data_left = input->data_len;
                p_api_stor->DecStat = STREAMDECODING;
                dec_cont->same_slice_header = 1;
                return RVDEC_NO_DECODING_BUFFER;
              }
#endif
            }
            else if (dec_cont->same_slice_header)
              dec_cont->same_slice_header = 0;

            dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].first_show = 1;
          } else {
            dec_cont->StrmStorage.work_out =
              BqueueNext( &dec_cont->StrmStorage.bq,
                          dec_cont->StrmStorage.work0,
                          dec_cont->StrmStorage.work1,
                          BQUEUE_UNUSED,
                          dec_cont->FrameDesc.pic_coding_type == RV_B_PIC );
          }
#else
          dec_cont->StrmStorage.work_out =
            BqueueNext( &dec_cont->StrmStorage.bq,
                        dec_cont->StrmStorage.work0,
                        dec_cont->StrmStorage.work1,
                        BQUEUE_UNUSED,
                        dec_cont->FrameDesc.pic_coding_type == RV_B_PIC );

#endif
          if (dec_cont->pp_enabled) {
            dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data = InputQueueGetBuffer(dec_cont->pp_buffer_queue, 1);
          }
          if(dec_cont->FrameDesc.pic_coding_type == RV_B_PIC) {
            dec_cont->StrmStorage.prev_bidx = dec_cont->StrmStorage.work_out;
          }

          if (dec_cont->StrmStorage.partial_freeze) {
            PreparePartialFreeze((u8*)dec_cont->StrmStorage.p_pic_buf[
                                   dec_cont->StrmStorage.work_out].data.virtual_address,
                                 dec_cont->FrameDesc.frame_width,
                                 dec_cont->FrameDesc.frame_height);
          }
        }
        asic_status = RunDecoderAsic(dec_cont, input->stream_bus_address);

        if(asic_status == ID8170_DEC_TIMEOUT) {
          ret = RVDEC_HW_TIMEOUT;
        } else if(asic_status == ID8170_DEC_SYSTEM_ERROR) {
          ret = RVDEC_SYSTEM_ERROR;
        } else if(asic_status == ID8170_DEC_HW_RESERVED) {
          ret = RVDEC_HW_RESERVED;
        } else if(asic_status & RV_DEC_X170_IRQ_BUS_ERROR) {
          ret = RVDEC_HW_BUS_ERROR;
        } else if( (asic_status & RV_DEC_X170_IRQ_STREAM_ERROR) ||
                   (asic_status & RV_DEC_X170_IRQ_TIMEOUT) ||
                   (asic_status & RV_DEC_X170_IRQ_ABORT)) {
          if (!dec_cont->StrmStorage.partial_freeze ||
              !ProcessPartialFreeze(
                (u8*)dec_cont->StrmStorage.p_pic_buf[
                  dec_cont->StrmStorage.work_out].data.virtual_address,
                dec_cont->StrmStorage.work0 != INVALID_ANCHOR_PICTURE ?
                (u8*)dec_cont->StrmStorage.p_pic_buf[
                  dec_cont->StrmStorage.work0].data.virtual_address :
                NULL,
                dec_cont->FrameDesc.frame_width,
                dec_cont->FrameDesc.frame_height,
                dec_cont->StrmStorage.partial_freeze == 1)) {
            if(asic_status & RV_DEC_X170_IRQ_TIMEOUT ||
                asic_status & RV_DEC_X170_IRQ_ABORT) {
              RVDEC_API_DEBUG(("IRQ TIMEOUT IN HW\n"));
            } else {
              RVDEC_API_DEBUG(("STREAM ERROR IN HW\n"));
              RVFLUSH;
            }
            if (dec_cont->pp_enabled) {
              InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data->virtual_address);
            }
            ret = rvHandleVlcModeError(dec_cont, input->pic_id);
            error_concealment = HANTRO_TRUE;
            RVDEC_UPDATE_POUTPUT;
          } else {
            asic_status &= ~RV_DEC_X170_IRQ_STREAM_ERROR;
            asic_status &= ~RV_DEC_X170_IRQ_TIMEOUT;
            asic_status &= ~RV_DEC_X170_IRQ_ABORT;
            asic_status |= RV_DEC_X170_IRQ_DEC_RDY;
            error_concealment = HANTRO_FALSE;
          }
        } else if(asic_status & RV_DEC_X170_IRQ_BUFFER_EMPTY) {
          rvDecPreparePicReturn(dec_cont);
          ret = RVDEC_BUF_EMPTY;
        } else if(asic_status & RV_DEC_X170_IRQ_DEC_RDY) {
        } else {
          ASSERT(0);
        }

        /* HW finished decoding a picture */
        if(asic_status & RV_DEC_X170_IRQ_DEC_RDY) {
          dec_cont->FrameDesc.frame_number++;

          rvHandleFrameEnd(dec_cont);

          rvDecBufferPicture(dec_cont,
                             input->pic_id,
                             dec_cont->FrameDesc.
                             pic_coding_type == RV_B_PIC,
                             dec_cont->FrameDesc.
                             pic_coding_type == RV_P_PIC,
                             RVDEC_PIC_DECODED, 0);

          ret = RVDEC_PIC_DECODED;

          if(dec_cont->FrameDesc.pic_coding_type != RV_B_PIC) {
            dec_cont->StrmStorage.work1 =
              dec_cont->StrmStorage.work0;
            dec_cont->StrmStorage.work0 =
              dec_cont->StrmStorage.work_out;
            if(dec_cont->StrmStorage.skip_b)
              dec_cont->StrmStorage.skip_b--;
          }

          if(dec_cont->FrameDesc.pic_coding_type == RV_I_PIC)
            dec_cont->StrmStorage.picture_broken = HANTRO_FALSE;

          rvDecPreparePicReturn(dec_cont);
        }

        if(ret != RVDEC_STRM_PROCESSED && ret != RVDEC_BUF_EMPTY) {
          p_api_stor->DecStat = STREAMDECODING;
        }

        if(ret == RVDEC_PIC_DECODED || ret == RVDEC_STRM_PROCESSED || ret == RVDEC_BUF_EMPTY) {
          /* copy output parameters for this PIC (excluding stream pos) */
          dec_cont->MbSetDesc.out_data.strm_curr_pos =
            output->strm_curr_pos;
          RVDEC_UPDATE_POUTPUT;
        }
      }
    } while(ret == 0);
  }

  if(error_concealment && dec_cont->FrameDesc.pic_coding_type != RV_B_PIC) {
    dec_cont->StrmStorage.picture_broken = HANTRO_TRUE;
  }

  RV_API_TRC("RvDecDecode: Exit\n");
  if(!dec_cont->StrmStorage.rpr_detected) {
    u32 consumed = (u32)(dec_cont->StrmDesc.strm_curr_pos - dec_cont->StrmDesc.p_strm_buff_start);
    /* Workaround for consumed data bigger than input data length */
    if (consumed > input->data_len)
      consumed = input->data_len;
    output->strm_curr_pos = input->stream + consumed;
    output->strm_curr_bus_address = input->stream_bus_address + consumed;
    output->data_left = dec_cont->StrmDesc.strm_buff_size - consumed;
  } else {
    output->data_left = input->data_len;
    ret = RVDEC_STRM_PROCESSED;
  }

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp_instance == NULL) {
    if(ret == RVDEC_PIC_DECODED ||
        ret == RVDEC_OK ||
        ret == RVDEC_STRM_PROCESSED ||
        ret == RVDEC_BUF_EMPTY ||
        ret == RVDEC_NONREF_PIC_SKIPPED) {
      dec_cont->output_stat = RvDecNextPicture_INTERNAL(dec_cont, &dec_cont->out_pic, 0);
      if(dec_cont->output_stat == RVDEC_ABORTED)
        return (RVDEC_ABORTED);

    }
  }
#endif
#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort)
    return(RVDEC_ABORTED);
  else
#endif
    return ((RvDecRet) ret);


}

/*------------------------------------------------------------------------------

    Function: RvDecRelease()

        Functional description:
            Release the decoder instance.

        Inputs:
            dec_inst     Decoder instance

        Outputs:
            none

        Returns:
            none

------------------------------------------------------------------------------*/

void RvDecRelease(RvDecInst dec_inst) {
  DecContainer *dec_cont = NULL;
#ifndef USE_EXTERNAL_BUFFER
  const void *dwl;
#endif

  RVDEC_DEBUG(("1\n"));
  RV_API_TRC("RvDecRelease#");
  if(dec_inst == NULL) {
    RV_API_TRC("RvDecRelease# ERROR: dec_inst == NULL");
    return;
  }

  dec_cont = ((DecContainer *) dec_inst);
#ifndef USE_EXTERNAL_BUFFER
  dwl = dec_cont->dwl;
#endif

  pthread_mutex_destroy(&dec_cont->protect_mutex);

  if (dec_cont->asic_running)
    (void) DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

#ifdef USE_OUTPUT_RELEASE
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
#endif

  rvFreeBuffers(dec_cont);

  if (dec_cont->StrmStorage.slices.virtual_address != NULL)
    DWLFreeLinear(dec_cont->dwl, &dec_cont->StrmStorage.slices);

  DWLfree(dec_cont);
#ifndef USE_EXTERNAL_BUFFER
  (void) DWLRelease(dwl);
#endif

  RV_API_TRC("RvDecRelease: OK");
}

/*------------------------------------------------------------------------------

    Function: rvRegisterPP()

        Functional description:
            Register the pp for RV pipeline

        Inputs:
            dec_inst     Decoder instance
            const void  *pp_inst - post-processor instance
            (*PPRun)(const void *) - decoder calls this to start PP
            void (*PPEndCallback)(const void *) - decoder calls this
                        to notify PP that a picture was done.

        Outputs:
            none

        Returns:
            i32 - return 0 for success or a negative error code

------------------------------------------------------------------------------*/

i32 rvRegisterPP(const void *dec_inst, const void *pp_inst,
                 void (*PPRun) (const void *, DecPpInterface *),
                 void (*PPEndCallback) (const void *),
                 void (*PPConfigQuery) (const void *, DecPpQuery *),
                 void (*PPDisplayIndex) (const void *, u32),
                 void (*PPBufferData) (const void *, u32, addr_t, addr_t, addr_t, addr_t)) {
  DecContainer *dec_cont;

  dec_cont = (DecContainer *) dec_inst;

  if(dec_inst == NULL || dec_cont->pp_instance != NULL ||
      pp_inst == NULL || PPRun == NULL || PPEndCallback == NULL ||
      PPConfigQuery == NULL || PPDisplayIndex == NULL || PPBufferData == NULL)
    return -1;

  if(dec_cont->asic_running)
    return -2;

  dec_cont->pp_instance = pp_inst;
  dec_cont->PPEndCallback = PPEndCallback;
  dec_cont->PPRun = PPRun;
  dec_cont->PPConfigQuery = PPConfigQuery;
  dec_cont->PPDisplayIndex = PPDisplayIndex;
  dec_cont->PPBufferData = PPBufferData;

  return 0;
}

/*------------------------------------------------------------------------------

    Function: rvUnregisterPP()

        Functional description:
            Unregister the pp from RV pipeline

        Inputs:
            dec_inst     Decoder instance
            const void  *pp_inst - post-processor instance

        Outputs:
            none

        Returns:
            i32 - return 0 for success or a negative error code

------------------------------------------------------------------------------*/

i32 rvUnregisterPP(const void *dec_inst, const void *pp_inst) {
  DecContainer *dec_cont;

  dec_cont = (DecContainer *) dec_inst;

  if(dec_inst == NULL || pp_inst != dec_cont->pp_instance)
    return -1;

  if(dec_cont->asic_running)
    return -2;

  dec_cont->pp_instance = NULL;
  dec_cont->PPEndCallback = NULL;
  dec_cont->PPRun = NULL;
  dec_cont->PPConfigQuery = NULL;
  dec_cont->PPDisplayIndex = NULL;
  dec_cont->PPBufferData = NULL;

  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : rvRefreshRegs
    Description     :
    Return type     : void
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void rvRefreshRegs(DecContainer * dec_cont) {
  i32 i;
  u32 *pp_regs = dec_cont->rv_regs;
  u32 offset;

  for(i = 0; i < DEC_X170_REGISTERS; i++) {
    pp_regs[i] = DWLReadReg(dec_cont->dwl, dec_cont->core_id, 4 * i);
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs =  dec_cont->rv_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *pp_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#endif
  offset = PP_START_UNIFIED_REGS * 0x04;
  pp_regs =  dec_cont->rv_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    *pp_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
}

/*------------------------------------------------------------------------------
    Function name   : rvFlushRegs
    Description     :
    Return type     : void
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void rvFlushRegs(DecContainer * dec_cont) {
  i32 i;
  u32 *pp_regs = dec_cont->rv_regs;
  u32 offset;

  for(i = 2; i < DEC_X170_REGISTERS; i++) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * i, pp_regs[i]);
    pp_regs[i] = 0;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs =  dec_cont->rv_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *pp_regs);
    pp_regs++;
    offset += 4;
  }
#endif
  offset = PP_START_UNIFIED_REGS * 0x04;
  pp_regs =  dec_cont->rv_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *pp_regs);
    pp_regs++;
    offset += 4;
  }
}

/*------------------------------------------------------------------------------
    Function name   : rvHandleVlcModeError
    Description     :
    Return type     : u32
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
u32 rvHandleVlcModeError(DecContainer * dec_cont, u32 pic_num) {
  u32 ret = RVDEC_STRM_PROCESSED;
  ASSERT(dec_cont->StrmStorage.strm_dec_ready);

  /*
  tmp = rvStrmDec_NextStartCode(dec_cont);
  if(tmp != END_OF_STREAM)
  {
      dec_cont->StrmDesc.strm_curr_pos -= 4;
      dec_cont->StrmDesc.strm_buff_read_bits -= 32;
  }
  */

  /* error in first picture -> set reference to grey */
  if(!dec_cont->FrameDesc.frame_number) {
    (void) DWLmemset(dec_cont->StrmStorage.
                     p_pic_buf[dec_cont->StrmStorage.work_out].data.
                     virtual_address, 128,
                     384 * dec_cont->FrameDesc.total_mb_in_frame);

    rvDecPreparePicReturn(dec_cont);

    /* no pictures finished -> return STRM_PROCESSED */
    ret = RVDEC_STRM_PROCESSED;
    dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work_out;
    dec_cont->StrmStorage.skip_b = 2;
  } else {
    if(dec_cont->FrameDesc.pic_coding_type != RV_B_PIC) {
      dec_cont->FrameDesc.frame_number++;

      /* reset send_to_pp to prevent post-processing partly decoded
       * pictures */
      if(dec_cont->StrmStorage.work_out != dec_cont->StrmStorage.work0)
        dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].
        send_to_pp = 0;

      BqueueDiscard(&dec_cont->StrmStorage.bq,
                    dec_cont->StrmStorage.work_out );
      dec_cont->StrmStorage.work_out = dec_cont->StrmStorage.work0;

      rvDecBufferPicture(dec_cont,
                         pic_num,
                         dec_cont->FrameDesc.pic_coding_type == RV_B_PIC,
                         1, (RvDecRet) FREEZED_PIC_RDY,
                         dec_cont->FrameDesc.total_mb_in_frame);

      ret = RVDEC_PIC_DECODED;

      dec_cont->StrmStorage.work1 = dec_cont->StrmStorage.work0;
      dec_cont->StrmStorage.skip_b = 2;
    } else {
      if(dec_cont->StrmStorage.intra_freeze) {
        dec_cont->FrameDesc.frame_number++;
        rvDecBufferPicture(dec_cont,
                           pic_num,
                           dec_cont->FrameDesc.pic_coding_type ==
                           RV_B_PIC, 1, (RvDecRet) FREEZED_PIC_RDY,
                           dec_cont->FrameDesc.total_mb_in_frame);

        ret = RVDEC_PIC_DECODED;

      } else {
        ret = RVDEC_NONREF_PIC_SKIPPED;
      }

      dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.prev_bidx].send_to_pp = 0;
    }
  }

  dec_cont->ApiStorage.DecStat = STREAMDECODING;

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : rvHandleFrameEnd
    Description     :
    Return type     : u32
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void rvHandleFrameEnd(DecContainer * dec_cont) {

  dec_cont->StrmDesc.strm_buff_read_bits =
    8 * (dec_cont->StrmDesc.strm_curr_pos -
         dec_cont->StrmDesc.p_strm_buff_start);
  dec_cont->StrmDesc.bit_pos_in_word = 0;

}

/*------------------------------------------------------------------------------

         Function name: RunDecoderAsic

         Purpose:       Set Asic run lenght and run Asic

         Input:         DecContainer *dec_cont

         Output:        void

------------------------------------------------------------------------------*/
u32 RunDecoderAsic(DecContainer * dec_container, addr_t strm_bus_address) {
  i32 ret;
  addr_t tmp = 0;
  u32 asic_status = 0;

  ASSERT(dec_container->StrmStorage.
         p_pic_buf[dec_container->StrmStorage.work_out].data.bus_address != 0);
  ASSERT(strm_bus_address != 0);

  /* set pp luma bus */
  dec_container->pp_control.input_bus_luma = 0;

  if(!dec_container->asic_running) {
    tmp = RvSetRegs(dec_container, strm_bus_address);
    if(tmp == HANTRO_NOK)
      return 0;

    (void) DWLReserveHw(dec_container->dwl, &dec_container->core_id);

    /* Start PP */
    if(dec_container->pp_instance != NULL) {
      RvPpControl(dec_container, 0);
    } else {
      SetDecRegister(dec_container->rv_regs, HWIF_DEC_OUT_DIS, 0);
      SetDecRegister(dec_container->rv_regs, HWIF_FILTERING_DIS, 1);
    }

    dec_container->asic_running = 1;

    DWLWriteReg(dec_container->dwl, dec_container->core_id, 0x4, 0);

    rvFlushRegs(dec_container);

    /* Enable HW */
    SetDecRegister(dec_container->rv_regs, HWIF_DEC_E, 1);
    DWLEnableHw(dec_container->dwl, dec_container->core_id, 4 * 1,
                dec_container->rv_regs[1]);
  } else { /* in the middle of decoding, continue decoding */
    /* tmp is strm_bus_address + number of bytes decoded by SW */
    /* TODO: alotetaanko aina bufferin alusta? */
    tmp = dec_container->StrmDesc.strm_curr_pos -
          dec_container->StrmDesc.p_strm_buff_start;
    tmp = strm_bus_address + tmp;

    /* pointer to start of the stream, mask to get the pointer to
     * previous 64-bit aligned position */
    if(!(tmp & ~0x7)) {
      return 0;
    }

    SET_ADDR_REG(dec_container->rv_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);
    /* amount of stream (as seen by the HW), obtained as amount of stream
     * given by the application subtracted by number of bytes decoded by
     * SW (if strm_bus_address is not 64-bit aligned -> adds number of bytes
     * from previous 64-bit aligned boundary) */
    SetDecRegister(dec_container->rv_regs, HWIF_STREAM_LEN,
                   dec_container->StrmDesc.strm_buff_size -
                   ((tmp & ~0x7) - strm_bus_address));
    SetDecRegister(dec_container->rv_regs, HWIF_STRM_START_BIT,
                   dec_container->StrmDesc.bit_pos_in_word + 8 * (tmp & 0x7));

    /* This depends on actual register allocation */
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 5,
                dec_container->rv_regs[5]);
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 6,
                dec_container->rv_regs[6]);
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 12,
                dec_container->rv_regs[12]);
#ifdef USE_64BIT_ENV
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 122,
                dec_container->rv_regs[122]);
#endif

    DWLEnableHw(dec_container->dwl, dec_container->core_id, 4 * 1,
                dec_container->rv_regs[1]);
  }

  /* Wait for HW ready */
  RVDEC_API_DEBUG(("Wait for Decoder\n"));
  ret = DWLWaitHwReady(dec_container->dwl, dec_container->core_id,
                       (u32) DEC_X170_TIMEOUT_LENGTH);

  rvRefreshRegs(dec_container);

  if(ret == DWL_HW_WAIT_OK) {
    asic_status =
      GetDecRegister(dec_container->rv_regs, HWIF_DEC_IRQ_STAT);
  } else if(ret == DWL_HW_WAIT_TIMEOUT) {
    asic_status = ID8170_DEC_TIMEOUT;
  } else {
    asic_status = ID8170_DEC_SYSTEM_ERROR;
  }

  if(!(asic_status & RV_DEC_X170_IRQ_BUFFER_EMPTY) ||
      (asic_status & RV_DEC_X170_IRQ_STREAM_ERROR) ||
      (asic_status & RV_DEC_X170_IRQ_BUS_ERROR) ||
      (asic_status == ID8170_DEC_TIMEOUT) ||
      (asic_status == ID8170_DEC_SYSTEM_ERROR)) {
    /* reset HW */
    SetDecRegister(dec_container->rv_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_container->rv_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_container->rv_regs, HWIF_DEC_E, 0);

    DWLDisableHw(dec_container->dwl, dec_container->core_id, 4 * 1,
                 dec_container->rv_regs[1]);

    /* End PP co-operation */
    if(dec_container->pp_control.pp_status == DECPP_RUNNING) {
      RVDEC_API_DEBUG(("RunDecoderAsic: PP Wait for end\n"));
      if(dec_container->pp_instance != NULL)
        dec_container->PPEndCallback(dec_container->pp_instance);
      RVDEC_API_DEBUG(("RunDecoderAsic: PP Finished\n"));

      if((asic_status & RV_DEC_X170_IRQ_STREAM_ERROR) &&
          dec_container->pp_control.use_pipeline)
        dec_container->pp_control.pp_status = DECPP_IDLE;
      else
        dec_container->pp_control.pp_status = DECPP_PIC_READY;
    }

    dec_container->asic_running = 0;
    (void) DWLReleaseHw(dec_container->dwl, dec_container->core_id);
  }

  /* if HW interrupt indicated either BUFFER_EMPTY or
   * DEC_RDY -> read stream end pointer and update StrmDesc structure */
  if((asic_status &
      (RV_DEC_X170_IRQ_BUFFER_EMPTY | RV_DEC_X170_IRQ_DEC_RDY))) {
    tmp = GET_ADDR_REG(dec_container->rv_regs, HWIF_RLC_VLC_BASE);

    if((tmp - strm_bus_address) <= DEC_X170_MAX_STREAM) {
      dec_container->StrmDesc.strm_curr_pos =
        dec_container->StrmDesc.p_strm_buff_start + (tmp - strm_bus_address);
    } else {
      dec_container->StrmDesc.strm_curr_pos =
        dec_container->StrmDesc.p_strm_buff_start +
        dec_container->StrmDesc.strm_buff_size;
    }

    dec_container->StrmDesc.strm_buff_read_bits =
      8 * (dec_container->StrmDesc.strm_curr_pos -
           dec_container->StrmDesc.p_strm_buff_start);
    dec_container->StrmDesc.bit_pos_in_word = 0;
  }

  if( dec_container->FrameDesc.pic_coding_type != RV_B_PIC &&
      dec_container->ref_buf_support &&
      (asic_status & RV_DEC_X170_IRQ_DEC_RDY) &&
      dec_container->asic_running == 0) {
    RefbuMvStatistics( &dec_container->ref_buffer_ctrl,
                       dec_container->rv_regs,
                       dec_container->StrmStorage.direct_mvs.virtual_address,
                       dec_container->FrameDesc.pic_coding_type == RV_P_PIC,
                       dec_container->FrameDesc.pic_coding_type == RV_I_PIC );
  }

  SetDecRegister(dec_container->rv_regs, HWIF_DEC_IRQ_STAT, 0);

  return asic_status;

}

/*------------------------------------------------------------------------------

    Function name: RvDecNextPicture

    Functional description:
        Retrieve next decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct
        end_of_stream Indicates whether end of stream has been reached

    Output:
        picture Decoder output picture.

    Return values:
        RVDEC_OK         No picture available.
        RVDEC_PIC_RDY    Picture ready.

------------------------------------------------------------------------------*/
RvDecRet RvDecNextPicture(RvDecInst dec_inst,
                          RvDecPicture * picture, u32 end_of_stream) {
  /* Variables */
  RvDecRet return_value = RVDEC_PIC_RDY;
  DecContainer *dec_cont;
  picture_t *p_pic;
  u32 pic_index = RV_BUFFER_UNDEFINED;
  u32 min_count;
  u32 tmp = 0;
  static u32 pic_count = 0;
  i32 ret;

  /* Code */
  RV_API_TRC("\nRv_dec_next_picture#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    RV_API_TRC("RvDecNextPicture# ERROR: picture is NULL");
    return (RVDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    RV_API_TRC("RvDecNextPicture# ERROR: Decoder not initialized");
    return (RVDEC_NOT_INITIALIZED);
  }

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp_instance == NULL) {
    addr_t i;
    if ((ret = FifoPop(dec_cont->fifo_display, (FifoObject *)&i,
#ifdef GET_OUTPUT_BUFFER_NON_BLOCK
            FIFO_EXCEPTION_ENABLE
#else
            FIFO_EXCEPTION_DISABLE
#endif
            )) != FIFO_ABORT) {

#ifdef GET_OUTPUT_BUFFER_NON_BLOCK
      if (ret == FIFO_EMPTY) return RVDEC_OK;
#endif

      if ((i32)i == -1) {
        RV_API_TRC("RvDecNextPicture# RVDEC_END_OF_STREAM\n");
        return RVDEC_END_OF_STREAM;
      }
      if ((i32)i == -2) {
        RV_API_TRC("RvDecNextPicture# RVDEC_FLUSHED\n");
        return RVDEC_FLUSHED;
      }

      *picture = dec_cont->StrmStorage.picture_info[i];

      RV_API_TRC("RvDecNextPicture# RVDEC_PIC_RDY\n");
      return (RVDEC_PIC_RDY);
    } else
      return RVDEC_ABORTED;
  }
#endif

  min_count = 0;
  if(!end_of_stream && !dec_cont->StrmStorage.rpr_detected)
    min_count = 1;

  /* this is to prevent post-processing of non-finished pictures in the
   * end of the stream */
  if(end_of_stream && dec_cont->FrameDesc.pic_coding_type == RV_B_PIC) {
    dec_cont->FrameDesc.pic_coding_type = RV_P_PIC;
    dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.prev_bidx].send_to_pp = 0;
  }

  /* Nothing to send out */
  if(dec_cont->StrmStorage.out_count <= min_count) {
    (void) DWLmemset(picture, 0, sizeof(RvDecPicture));
    picture->output_picture = NULL;
    return_value = RVDEC_OK;
  } else {
    pic_index = dec_cont->StrmStorage.out_index;
    pic_index = dec_cont->StrmStorage.out_buf[pic_index];

    RvFillPicStruct(picture, dec_cont, pic_index);

    pic_count++;

    dec_cont->StrmStorage.out_count--;
    dec_cont->StrmStorage.out_index++;
    dec_cont->StrmStorage.out_index &= 0xF;
  }

  if(dec_cont->pp_instance &&
      (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_FULLMODE) &&
      end_of_stream && (return_value == RVDEC_PIC_RDY)) {
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_UNINIT;
    if( dec_cont->StrmStorage.previous_b) {
      dec_cont->pp_control.display_index =
        dec_cont->pp_control.prev_anchor_display_index;
      dec_cont->pp_control.buffer_index =
        dec_cont->pp_control.display_index;
    } else {
      dec_cont->pp_control.display_index = dec_cont->pp_control.buffer_index;
    }
    dec_cont->PPDisplayIndex(dec_cont->pp_instance,
                             dec_cont->pp_control.buffer_index);
  }

  /* pp display process is separate of decoding process */
  if(dec_cont->pp_instance && return_value == RVDEC_PIC_RDY &&
      (dec_cont->pp_control.multi_buf_stat != MULTIBUFFER_FULLMODE)) {
    /* pp and decoder running in parallel, decoder finished first field ->
     * decode second field and wait PP after that */
    if(dec_cont->pp_instance != NULL &&
        dec_cont->pp_control.pp_status == DECPP_PIC_NOT_FINISHED) {
      return (RVDEC_OK);
    }

    if(dec_cont->pp_control.pp_status == DECPP_PIC_READY) {
      RvFillPicStruct(picture, dec_cont, pic_index);
      return_value = RVDEC_PIC_RDY;
      dec_cont->pp_control.pp_status = DECPP_IDLE;
    } else {
      p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;
      return_value = RVDEC_OK;

      if(RVDEC_NON_PIPELINE_AND_B_PICTURE) {
        pic_index = dec_cont->StrmStorage.prev_bidx;   /* send index 2 (B Picture output) to PP) */
        dec_cont->FrameDesc.pic_coding_type = RV_I_PIC;

      } else if(end_of_stream) {
        pic_index = dec_cont->StrmStorage.work0;
        p_pic[pic_index].send_to_pp = 2;
        /*
         * pic_index = 0;
         * while((pic_index < 3) && !p_pic[pic_index].send_to_pp)
         * pic_index++;
         * if (pic_index == 3)
         * return RVDEC_OK;
         */

      }
      /* handle I/P pictures where RunDecoderAsic was not invoced (error
       * in picture headers etc) */
      else if(!dec_cont->pp_config_query.pipeline_accepted) {
        if(dec_cont->StrmStorage.out_count &&
            dec_cont->StrmStorage.out_buf[0] ==
            dec_cont->StrmStorage.out_buf[1]) {
          pic_index = dec_cont->StrmStorage.out_buf[0];
          tmp = 1;
        }
      }

      if(pic_index != RV_BUFFER_UNDEFINED) {
        if(p_pic[pic_index].send_to_pp && p_pic[pic_index].send_to_pp < 3) {
          RVDEC_API_DEBUG(("NextPicture: send to pp %d\n",
                           pic_index));
          dec_cont->pp_control.tiled_input_mode =
            dec_cont->tiled_reference_enable;
          dec_cont->pp_control.progressive_sequence = 1;
          dec_cont->pp_control.input_bus_luma =
            dec_cont->StrmStorage.p_pic_buf[pic_index].data.
            bus_address;
          dec_cont->pp_control.input_bus_chroma =
            dec_cont->pp_control.input_bus_luma +
            ((dec_cont->FrameDesc.frame_width *
              dec_cont->FrameDesc.frame_height) << 8);
          dec_cont->pp_control.inwidth =
            dec_cont->pp_control.cropped_w =
              dec_cont->FrameDesc.frame_width << 4;
          dec_cont->pp_control.inheight =
            dec_cont->pp_control.cropped_h =
              dec_cont->FrameDesc.frame_height << 4;

          dec_cont->pp_control.use_pipeline = 0;
          {
            u32 value = GetDecRegister(dec_cont->rv_regs,
                                       HWIF_DEC_OUT_ENDIAN);

            dec_cont->pp_control.little_endian =
              (value == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
          }
          dec_cont->pp_control.word_swap =
            GetDecRegister(dec_cont->rv_regs,
                           HWIF_DEC_OUTSWAP32_E) ? 1 : 0;

          /* Run pp */
          dec_cont->PPRun(dec_cont->pp_instance, &dec_cont->pp_control);
          dec_cont->pp_control.pp_status = DECPP_RUNNING;

          if(dec_cont->pp_control.pic_struct ==
              DECPP_PIC_FRAME_OR_TOP_FIELD ||
              dec_cont->pp_control.pic_struct ==
              DECPP_PIC_TOP_AND_BOT_FIELD_FRAME) {
            /* tmp set if freezed pic and has will be used as
             * output another time */
            if(!tmp)
              dec_cont->StrmStorage.p_pic_buf[pic_index].send_to_pp =
                0;
          } else {
            dec_cont->StrmStorage.p_pic_buf[pic_index].send_to_pp--;
          }

          /* Wait for result */
          dec_cont->PPEndCallback(dec_cont->pp_instance);

          RvFillPicStruct(picture, dec_cont, pic_index);
          return_value = RVDEC_PIC_RDY;
          dec_cont->pp_control.pp_status = DECPP_IDLE;
          dec_cont->pp_control.pic_struct =
            DECPP_PIC_FRAME_OR_TOP_FIELD;
        }
      }
    }
  }

  return return_value;
}

#ifdef USE_OUTPUT_RELEASE
/*------------------------------------------------------------------------------

    Function name: RvDecNextPicture_INTERNAL

    Functional description:
        Push next picture in display order into output fifo if any available.

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct
        end_of_stream Indicates whether end of stream has been reached

    Output:
        picture Decoder output picture.

    Return values:
        RVDEC_OK                No picture available.
        RVDEC_PIC_RDY           Picture ready.
        rvDEC_PARAM_ERROR       invalid parameters.
        RVDEC_NOT_INITIALIZED   decoder instance not initialized yet.

------------------------------------------------------------------------------*/
RvDecRet RvDecNextPicture_INTERNAL(RvDecInst dec_inst,
                                   RvDecPicture * picture, u32 end_of_stream) {
  /* Variables */
  RvDecRet return_value = RVDEC_PIC_RDY;
  DecContainer *dec_cont;
  u32 pic_index = RV_BUFFER_UNDEFINED;
  u32 min_count;
  static u32 pic_count = 0;

  /* Code */
  RV_API_TRC("\nRv_dec_next_picture_INTERNAL#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    RV_API_TRC("RvDecNextPicture_INTERNAL# ERROR: picture is NULL");
    return (RVDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    RV_API_TRC("RvDecNextPicture_INTERNAL# ERROR: Decoder not initialized");
    return (RVDEC_NOT_INITIALIZED);
  }

  min_count = 0;
  if(!end_of_stream && !dec_cont->StrmStorage.rpr_detected)
    min_count = 1;

  /* this is to prevent post-processing of non-finished pictures in the
   * end of the stream */
  if(end_of_stream && dec_cont->FrameDesc.pic_coding_type == RV_B_PIC) {
    dec_cont->FrameDesc.pic_coding_type = RV_P_PIC;
    dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.prev_bidx].send_to_pp = 0;
  }

  /* Nothing to send out */
  if(dec_cont->StrmStorage.out_count <= min_count) {
    (void) DWLmemset(picture, 0, sizeof(RvDecPicture));
    picture->output_picture = NULL;
    return_value = RVDEC_OK;
  } else {
    pic_index = dec_cont->StrmStorage.out_index;
    pic_index = dec_cont->StrmStorage.out_buf[pic_index];

    RvFillPicStruct(picture, dec_cont, pic_index);

    pic_count++;

    dec_cont->StrmStorage.out_count--;
    dec_cont->StrmStorage.out_index++;
    dec_cont->StrmStorage.out_index &= 0xF;

#ifdef USE_PICTURE_DISCARD
    if (dec_cont->StrmStorage.p_pic_buf[pic_index].first_show)
#endif
    {
#ifndef USE_PICTURE_DISCARD
      /* wait this buffer as unused */
      if (BqueueWaitBufNotInUse(&dec_cont->StrmStorage.bq, pic_index) != HANTRO_OK)
        return RVDEC_ABORTED;

      if(dec_cont->pp_enabled) {
        InputQueueWaitBufNotUsed(dec_cont->pp_buffer_queue,dec_cont->StrmStorage.p_pic_buf[pic_index].pp_data->virtual_address);
      }
#endif

      dec_cont->StrmStorage.p_pic_buf[pic_index].first_show = 0;

      /* set this buffer as used */
      BqueueSetBufferAsUsed(&dec_cont->StrmStorage.bq, pic_index);

      if(dec_cont->pp_enabled)
        InputQueueSetBufAsUsed(dec_cont->pp_buffer_queue,dec_cont->StrmStorage.p_pic_buf[pic_index].pp_data->virtual_address);

      dec_cont->StrmStorage.picture_info[dec_cont->fifo_index] = *picture;
      FifoPush(dec_cont->fifo_display, dec_cont->fifo_index, FIFO_EXCEPTION_DISABLE);
      dec_cont->fifo_index++;
      if(dec_cont->fifo_index == 32)
        dec_cont->fifo_index = 0;
      if (dec_cont->pp_enabled) {
        BqueuePictureRelease(&dec_cont->StrmStorage.bq, pic_index);
      }
    }
  }

  return return_value;
}

/*------------------------------------------------------------------------------

    Function name: RvDecPictureConsumed

    Functional description:
        release specific decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to  picture struct


    Return values:
        RVDEC_PARAM_ERROR         Decoder instance or picture is null
        RVDEC_NOT_INITIALIZED     Decoder instance isn't initialized
        RVDEC_OK                          picture release success
------------------------------------------------------------------------------*/
RvDecRet RvDecPictureConsumed(RvDecInst dec_inst, RvDecPicture * picture) {
  /* Variables */
  DecContainer *dec_cont;
  u32 i;

  /* Code */
  RV_API_TRC("\nRv_dec_picture_consumed#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    RV_API_TRC("RvDecPictureConsumed# ERROR: picture is NULL");
    return (RVDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    RV_API_TRC("RvDecPictureConsumed# ERROR: Decoder not initialized");
    return (RVDEC_NOT_INITIALIZED);
  }

  if (!dec_cont->pp_enabled) {
    for(i = 0; i < dec_cont->StrmStorage.num_buffers; i++) {
      if(picture->output_picture_bus_address
          == dec_cont->StrmStorage.p_pic_buf[i].data.bus_address) {
        if(dec_cont->pp_instance == NULL) {
          BqueuePictureRelease(&dec_cont->StrmStorage.bq, i);
        }
        return (RVDEC_OK);
      }
    }
  } else {
    InputQueueReturnBuffer(dec_cont->pp_buffer_queue,(u32 *)picture->output_picture);
    return (RVDEC_OK);
  }
  return (RVDEC_PARAM_ERROR);
}


RvDecRet RvDecEndOfStream(RvDecInst dec_inst, u32 strm_end_flag) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;

  RV_API_TRC("RvDecEndOfStream#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    RV_API_TRC("RvDecEndOfStream# ERROR: Decoder not initialized");
    return (RVDEC_NOT_INITIALIZED);
  }
  pthread_mutex_lock(&dec_cont->protect_mutex);
  if(dec_cont->dec_stat == RVDEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (RVDEC_OK);
  }

  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->rv_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->rv_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->rv_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->rv_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  dec_cont->output_stat = RvDecNextPicture_INTERNAL(dec_cont, &dec_cont->out_pic, 1);
  if(dec_cont->output_stat == RVDEC_ABORTED) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (RVDEC_ABORTED);
  }

  if(strm_end_flag) {
    dec_cont->dec_stat = RVDEC_END_OF_STREAM;
    FifoPush(dec_cont->fifo_display, -1, FIFO_EXCEPTION_DISABLE);
  }

  /* Wait all buffers as unused */
  //if(dec_cont->pp_instance == NULL && !strm_end_flag)
  //  BqueueWaitNotInUse(&dec_cont->StrmStorage.bq);

  dec_cont->StrmStorage.work0 =
    dec_cont->StrmStorage.work1 = INVALID_ANCHOR_PICTURE;

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  RV_API_TRC("RvDecEndOfStream# RVDEC_OK\n");
  return RVDEC_OK;
}

#endif

/*----------------------=-------------------------------------------------------

    Function name: RvFillPicStruct

    Functional description:
        Fill data to output pic description

    Input:
        dec_cont    Decoder container
        picture    Pointer to return value struct

    Return values:
        void

------------------------------------------------------------------------------*/
void RvFillPicStruct(RvDecPicture * picture,
                     DecContainer * dec_cont, u32 pic_index) {
  picture_t *p_pic;

  p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;
  if (!dec_cont->pp_enabled) {
    picture->frame_width = p_pic[pic_index].frame_width;
    picture->frame_height = p_pic[pic_index].frame_height;
    picture->coded_width = p_pic[pic_index].coded_width;
    picture->coded_height = p_pic[pic_index].coded_height;

    picture->output_picture = (u8 *) p_pic[pic_index].data.virtual_address;
    picture->output_picture_bus_address = p_pic[pic_index].data.bus_address;
  } else {
    picture->frame_width = p_pic[pic_index].frame_width >> dec_cont->dscale_shift_x;
    picture->frame_height = p_pic[pic_index].frame_height >> dec_cont->dscale_shift_y;
    picture->coded_width = p_pic[pic_index].coded_width >> dec_cont->dscale_shift_x;
    picture->coded_height = p_pic[pic_index].coded_height >> dec_cont->dscale_shift_y;

    picture->output_picture = (u8 *) p_pic[pic_index].pp_data->virtual_address;
    picture->output_picture_bus_address = p_pic[pic_index].pp_data->bus_address;
  }
  picture->key_picture = p_pic[pic_index].pic_type;
  picture->pic_id = p_pic[pic_index].pic_id;
  picture->decode_id = p_pic[pic_index].decode_id;
  picture->pic_coding_type = p_pic[pic_index].pic_code_type;
  picture->output_format = p_pic[pic_index].tiled_mode ?
                           DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

  picture->number_of_err_mbs = p_pic[pic_index].nbr_err_mbs;

}

/*------------------------------------------------------------------------------

    Function name: RvSetRegs

    Functional description:
        Set registers

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
u32 RvSetRegs(DecContainer * dec_container, addr_t strm_bus_address) {
  addr_t tmp = 0;
  u32 tmp_fwd;

#ifdef _DEC_PP_USAGE
  RvDecPpUsagePrint(dec_container, DECPP_UNSPECIFIED,
                    dec_container->StrmStorage.work_out, 1,
                    dec_container->StrmStorage.latest_id);
#endif

  dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].
  send_to_pp = 1;


  RVDEC_API_DEBUG(("Decoding to index %d \n",
                   dec_container->StrmStorage.work_out));

  /* swReg3 */
  SetDecRegister(dec_container->rv_regs, HWIF_PIC_INTERLACE_E, 0);
  SetDecRegister(dec_container->rv_regs, HWIF_PIC_FIELDMODE_E, 0);

  /*
  SetDecRegister(dec_container->rv_regs, HWIF_PIC_MB_HEIGHT_P,
                 dec_container->FrameDesc.frame_height);
  */

  if(dec_container->FrameDesc.pic_coding_type == RV_B_PIC)
    SetDecRegister(dec_container->rv_regs, HWIF_PIC_B_E, 1);
  else
    SetDecRegister(dec_container->rv_regs, HWIF_PIC_B_E, 0);

  SetDecRegister(dec_container->rv_regs, HWIF_PIC_INTER_E,
                 dec_container->FrameDesc.pic_coding_type == RV_P_PIC ||
                 dec_container->FrameDesc.pic_coding_type == RV_B_PIC ? 1 : 0);

  SetDecRegister(dec_container->rv_regs, HWIF_WRITE_MVS_E,
                 dec_container->FrameDesc.pic_coding_type == RV_P_PIC);


  SetDecRegister(dec_container->rv_regs, HWIF_INIT_QP,
                 dec_container->FrameDesc.qp);

  SetDecRegister(dec_container->rv_regs, HWIF_RV_FWD_SCALE,
                 dec_container->StrmStorage.fwd_scale);
  SetDecRegister(dec_container->rv_regs, HWIF_RV_BWD_SCALE,
                 dec_container->StrmStorage.bwd_scale);

  /* swReg5 */

  /* tmp is strm_bus_address + number of bytes decoded by SW */
#ifdef RV_RAW_STREAM_SUPPORT
  if (dec_container->StrmStorage.raw_mode)
    tmp = dec_container->StrmDesc.strm_curr_pos -
          dec_container->StrmDesc.p_strm_buff_start;
  else
#endif
    tmp = 0;

  tmp = strm_bus_address + tmp;

  /* bus address must not be zero */
  if(!(tmp & ~0x7)) {
    return 0;
  }

  /* pointer to start of the stream, mask to get the pointer to
   * previous 64-bit aligned position */
  SET_ADDR_REG(dec_container->rv_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);

  /* amount of stream (as seen by the HW), obtained as amount of
   * stream given by the application subtracted by number of bytes
   * decoded by SW (if strm_bus_address is not 64-bit aligned -> adds
   * number of bytes from previous 64-bit aligned boundary) */
  SetDecRegister(dec_container->rv_regs, HWIF_STREAM_LEN,
                 dec_container->StrmDesc.strm_buff_size -
                 ((tmp & ~0x7) - strm_bus_address));


#ifdef RV_RAW_STREAM_SUPPORT
  if (dec_container->StrmStorage.raw_mode)
    SetDecRegister(dec_container->rv_regs, HWIF_STRM_START_BIT,
                   dec_container->StrmDesc.bit_pos_in_word + 8 * (tmp & 0x7));
  else
#endif
    SetDecRegister(dec_container->rv_regs, HWIF_STRM_START_BIT, 0);

  /* swReg13 */
  SET_ADDR_REG(dec_container->rv_regs, HWIF_DEC_OUT_BASE,
               dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].
               data.bus_address);

  SetDecRegister( dec_container->rv_regs, HWIF_PP_PIPELINE_E_U, dec_container->pp_enabled );
  if (dec_container->pp_enabled) {
    u32 dsw, dsh;
#define NEXT_MULTIPLE(value, n) (((value) + (n) - 1) & ~((n) - 1))

    dsw = NEXT_MULTIPLE((dec_container->FrameDesc.frame_width * 16 >> dec_container->dscale_shift_x) * 8, 16 * 8) / 8;
    dsh = (dec_container->FrameDesc.frame_height * 16 >> dec_container->dscale_shift_y);
    if (dec_container->dscale_shift_x == 0) {
      SetDecRegister(dec_container->rv_regs, HWIF_HOR_SCALE_MODE_U, 0);
      SetDecRegister(dec_container->rv_regs, HWIF_WSCALE_INVRA_U, 0);
    } else {
      /* down scale */
      SetDecRegister(dec_container->rv_regs, HWIF_HOR_SCALE_MODE_U, 2);
      SetDecRegister(dec_container->rv_regs, HWIF_WSCALE_INVRA_U,
                     1<<(16-dec_container->dscale_shift_x));
    }

    if (dec_container->dscale_shift_y == 0) {
      SetDecRegister(dec_container->rv_regs, HWIF_VER_SCALE_MODE_U, 0);
      SetDecRegister(dec_container->rv_regs, HWIF_HSCALE_INVRA_U, 0);
    } else {
      /* down scale */
      SetDecRegister(dec_container->rv_regs, HWIF_VER_SCALE_MODE_U, 2);
      SetDecRegister(dec_container->rv_regs, HWIF_HSCALE_INVRA_U,
                     1<<(16-dec_container->dscale_shift_y));
    }
    SET_ADDR64_REG(dec_container->rv_regs, HWIF_PP_OUT_LU_BASE_U,
                   dec_container->StrmStorage.p_pic_buf[dec_container->
                       StrmStorage.work_out].
                   pp_data->bus_address);
    SET_ADDR64_REG(dec_container->rv_regs, HWIF_PP_OUT_CH_BASE_U,
                   dec_container->StrmStorage.p_pic_buf[dec_container->
                       StrmStorage.work_out].
                   pp_data->bus_address + dsw * dsh);

    SetPpRegister(dec_container->rv_regs, HWIF_PP_IN_FORMAT_U, 1);
  }

  if(dec_container->FrameDesc.pic_coding_type == RV_B_PIC) { /* ? */
    /* past anchor set to future anchor if past is invalid (second
     * picture in sequence is B) */
    tmp_fwd =
      dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE ?
      dec_container->StrmStorage.work1 :
      dec_container->StrmStorage.work0;

    SET_ADDR_REG(dec_container->rv_regs, HWIF_REFER0_BASE,
                 dec_container->StrmStorage.p_pic_buf[tmp_fwd].data.
                 bus_address);
    SET_ADDR_REG(dec_container->rv_regs, HWIF_REFER1_BASE,
                 dec_container->StrmStorage.p_pic_buf[tmp_fwd].data.
                 bus_address);
    SET_ADDR_REG(dec_container->rv_regs, HWIF_REFER2_BASE,
                 dec_container->StrmStorage.
                 p_pic_buf[dec_container->StrmStorage.work0].data.
                 bus_address);
    SET_ADDR_REG(dec_container->rv_regs, HWIF_REFER3_BASE,
                 dec_container->StrmStorage.
                 p_pic_buf[dec_container->StrmStorage.work0].data.
                 bus_address);
  } else {
    SET_ADDR_REG(dec_container->rv_regs, HWIF_REFER0_BASE,
                 dec_container->StrmStorage.
                 p_pic_buf[dec_container->StrmStorage.work0].data.
                 bus_address);
    SET_ADDR_REG(dec_container->rv_regs, HWIF_REFER1_BASE,
                 dec_container->StrmStorage.
                 p_pic_buf[dec_container->StrmStorage.work0].data.
                 bus_address);
  }

  SetDecRegister(dec_container->rv_regs, HWIF_STARTMB_X, 0);
  SetDecRegister(dec_container->rv_regs, HWIF_STARTMB_Y, 0);

  SetDecRegister(dec_container->rv_regs, HWIF_DEC_OUT_DIS, 0);
  SetDecRegister(dec_container->rv_regs, HWIF_FILTERING_DIS, 1);

  SET_ADDR_REG(dec_container->rv_regs, HWIF_DIR_MV_BASE,
               dec_container->StrmStorage.direct_mvs.bus_address);
  SetDecRegister(dec_container->rv_regs, HWIF_PREV_ANC_TYPE,
                 dec_container->StrmStorage.p_pic_buf[
                   dec_container->StrmStorage.work0].is_inter);

#ifdef RV_RAW_STREAM_SUPPORT
  if (dec_container->StrmStorage.raw_mode)
    SetDecRegister(dec_container->rv_regs, HWIF_PIC_SLICE_AM, 0);
  else
#endif
    SetDecRegister(dec_container->rv_regs, HWIF_PIC_SLICE_AM,
                   dec_container->StrmStorage.num_slices-1);
  SET_ADDR_REG(dec_container->rv_regs, HWIF_QTABLE_BASE,
               dec_container->StrmStorage.slices.bus_address);

  if (!dec_container->StrmStorage.is_rv8)
    SetDecRegister(dec_container->rv_regs, HWIF_FRAMENUM_LEN,
                   dec_container->StrmStorage.frame_size_bits);

  /* Setup reference picture buffer */
  if(dec_container->ref_buf_support) {
    RefbuSetup(&dec_container->ref_buffer_ctrl, dec_container->rv_regs,
               REFBU_FRAME,
               dec_container->FrameDesc.pic_coding_type == RV_I_PIC,
               dec_container->FrameDesc.pic_coding_type == RV_B_PIC, 0, 2,
               0 );
  }

  if( dec_container->tiled_mode_support) {
    dec_container->tiled_reference_enable =
      DecSetupTiledReference( dec_container->rv_regs,
                              dec_container->tiled_mode_support,
                              DEC_DPB_FRAME,
                              0 /* interlaced content not present */ );
  } else {
    dec_container->tiled_reference_enable = 0;
  }


  if (dec_container->StrmStorage.raw_mode) {
    SetDecRegister(dec_container->rv_regs, HWIF_RV_OSV_QUANT,
                   dec_container->FrameDesc.vlc_set );
  }
  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    Function name: RvCheckFormatSupport

    Functional description:
        Check if RV supported

    Input:
        container

    Return values:
        return zero for OK

------------------------------------------------------------------------------*/
u32 RvCheckFormatSupport(void) {
  u32 id = 0;
  u32 product = 0;
  DWLHwConfig hw_config;

  id = DWLReadAsicID(DWL_CLIENT_TYPE_RV_DEC);

  product = id >> 16;

  if(product < 0x8170 &&
      product != 0x6731 )
    return ~0;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_RV_DEC);

  return (hw_config.rv_support == RV_NOT_SUPPORTED);
}

/*------------------------------------------------------------------------------

    Function name: RvPpControl

    Functional description:
        set up and start pp

    Input:
        container
        pipeline_off     override pipeline setting

    Return values:
        void

------------------------------------------------------------------------------*/

void RvPpControl(DecContainer * dec_container, u32 pipeline_off) {
  u32 index_for_pp = RV_BUFFER_UNDEFINED;
  u32 next_buffer_index;

  DecPpInterface *pc = &dec_container->pp_control;

  /* PP not connected or still running (not waited when first field of frame
   * finished */
  if(pc->multi_buf_stat == MULTIBUFFER_DISABLED &&
      pc->pp_status == DECPP_PIC_NOT_FINISHED)
    return;

  dec_container->pp_config_query.tiled_mode =
    dec_container->tiled_reference_enable;
  dec_container->PPConfigQuery(dec_container->pp_instance,
                               &dec_container->pp_config_query);

  RvPpMultiBufferSetup(dec_container, (pipeline_off ||
                                       !dec_container->pp_config_query.
                                       pipeline_accepted));

  dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].
  send_to_pp = 1;

  /* Select new PP buffer index to use. If multibuffer is disabled, use
   * previous buffer, otherwise select new buffer from queue. */
  if(pc->multi_buf_stat != MULTIBUFFER_DISABLED) {
    next_buffer_index = BqueueNext( &dec_container->StrmStorage.bq_pp,
                                    BQUEUE_UNUSED,
                                    BQUEUE_UNUSED,
                                    BQUEUE_UNUSED,
                                    dec_container->FrameDesc.pic_coding_type == RV_B_PIC);
    pc->buffer_index = next_buffer_index;
  } else {
    next_buffer_index = pc->buffer_index;
  }

  if(/*p_hdrs->lowDelay ||*/
    dec_container->FrameDesc.pic_coding_type == RV_B_PIC) {
    pc->display_index = pc->buffer_index;
  } else {
    pc->display_index = pc->prev_anchor_display_index;
  }

  /* Connect PP output buffer to decoder output buffer */
  {
    u32 luma = 0;
    u32 chroma = 0;
    u32 bot_luma, bot_chroma;

    luma = dec_container->StrmStorage.
           p_pic_buf[dec_container->StrmStorage.work_out].data.bus_address;
    chroma = luma + ((dec_container->FrameDesc.frame_width *
                      dec_container->FrameDesc.frame_height) << 8);

    bot_luma = luma + (dec_container->FrameDesc.frame_width * 16);
    bot_chroma = chroma + (dec_container->FrameDesc.frame_width * 16);

    dec_container->PPBufferData(dec_container->pp_instance,
                                pc->buffer_index, luma, chroma, bot_luma, bot_chroma );
  }

  if(pc->multi_buf_stat == MULTIBUFFER_FULLMODE) {
    RVDEC_API_DEBUG(("Full pipeline# \n"));
    pc->use_pipeline = dec_container->pp_config_query.pipeline_accepted;
    RvDecRunFullmode(dec_container);
    dec_container->StrmStorage.previous_mode_full = 1;
  } else if(dec_container->StrmStorage.previous_mode_full == 1) {
    if(dec_container->FrameDesc.pic_coding_type == RV_B_PIC) {
      dec_container->StrmStorage.previous_b = 1;
    } else {
      dec_container->StrmStorage.previous_b = 0;
    }

    if(dec_container->FrameDesc.pic_coding_type == RV_B_PIC) {
      RVDEC_API_DEBUG(("PIPELINE OFF, DON*T SEND B TO PP\n"));
      index_for_pp = RV_BUFFER_UNDEFINED;
      pc->input_bus_luma = 0;
    }
    pc->pp_status = DECPP_IDLE;

    dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work0].send_to_pp = 1;

    dec_container->StrmStorage.previous_mode_full = 0;
  } else {
    pc->buffer_index = pc->display_index;

    if(dec_container->FrameDesc.pic_coding_type == RV_B_PIC) {
      dec_container->StrmStorage.previous_b = 1;
    } else {
      dec_container->StrmStorage.previous_b = 0;
    }

    if((dec_container->FrameDesc.pic_coding_type != RV_B_PIC) ||
        pipeline_off) {
      pc->use_pipeline = 0;
    } else {
      RVDEC_API_DEBUG(("RUN PP  # \n"));
      RVFLUSH;
      pc->use_pipeline = dec_container->pp_config_query.pipeline_accepted;
    }

    if(!pc->use_pipeline) {
      /* pipeline not accepted, don't run for first picture */
      if(dec_container->FrameDesc.frame_number &&
          (dec_container->ApiStorage.buffer_for_pp == NO_BUFFER)) {
        pc->tiled_input_mode = dec_container->tiled_reference_enable;
        pc->progressive_sequence = 1;

        /*if:
         * B pictures allowed and non B picture OR
         * B pictures not allowed */
        if(dec_container->FrameDesc.pic_coding_type != RV_B_PIC) {
#ifdef _DEC_PP_USAGE
          RvDecPpUsagePrint(dec_container, DECPP_PARALLEL,
                            dec_container->StrmStorage.work0,
                            0,
                            dec_container->StrmStorage.
                            p_pic_buf[dec_container->StrmStorage.
                                      work0].pic_id);
#endif
          pc->input_bus_luma =
            dec_container->StrmStorage.p_pic_buf[dec_container->
                                                 StrmStorage.work0].
            data.bus_address;

          pc->input_bus_chroma =
            pc->input_bus_luma +
            ((dec_container->FrameDesc.frame_width *
              dec_container->FrameDesc.frame_height) << 8);

          pc->inwidth = pc->cropped_w =
                          dec_container->FrameDesc.frame_width << 4;
          pc->inheight = pc->cropped_h =
                           dec_container->FrameDesc.frame_height << 4;
          {
            u32 value = GetDecRegister(dec_container->rv_regs,
                                       HWIF_DEC_OUT_ENDIAN);

            pc->little_endian =
              (value == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
          }
          pc->word_swap =
            GetDecRegister(dec_container->rv_regs,
                           HWIF_DEC_OUTSWAP32_E) ? 1 : 0;

          RVDEC_API_DEBUG(("sending NON B to pp\n"));
          index_for_pp = dec_container->StrmStorage.work0;

          dec_container->StrmStorage.p_pic_buf[index_for_pp].send_to_pp = 2;
        } else {
          RVDEC_API_DEBUG(("PIPELINE OFF, DON*T SEND B TO PP\n"));
          index_for_pp = dec_container->StrmStorage.work_out;
          index_for_pp = RV_BUFFER_UNDEFINED;
          pc->input_bus_luma = 0;
        }
      } else {
        pc->input_bus_luma = 0;
      }
    } else { /* pipeline */
#ifdef _DEC_PP_USAGE
      RvDecPpUsagePrint(dec_container, DECPP_PIPELINED,
                        dec_container->StrmStorage.work_out, 0,
                        dec_container->StrmStorage.
                        p_pic_buf[dec_container->StrmStorage.
                                  work_out].pic_id);
#endif
      pc->input_bus_luma = pc->input_bus_chroma = 0;
      index_for_pp = dec_container->StrmStorage.work_out;
      pc->tiled_input_mode = dec_container->tiled_reference_enable;
      pc->progressive_sequence = 1;

      pc->inwidth = pc->cropped_w =
                      dec_container->FrameDesc.frame_width << 4;
      pc->inheight = pc->cropped_h =
                       dec_container->FrameDesc.frame_height << 4;
    }

    /* start PP */
    if(((pc->input_bus_luma && !pc->use_pipeline) ||
        (!pc->input_bus_luma && pc->use_pipeline))
        && dec_container->StrmStorage.p_pic_buf[index_for_pp].send_to_pp) {
      RVDEC_API_DEBUG(("pprun, pipeline %s\n",
                       pc->use_pipeline ? "on" : "off"));

      /* filter needs pipeline to work! */
      RVDEC_API_DEBUG(("Filter %s# \n",
                       dec_container->ApiStorage.
                       disable_filter ? "off" : "on"));

      /* always disabled in MPEG-2 */
      dec_container->ApiStorage.disable_filter = 1;

      SetDecRegister(dec_container->rv_regs, HWIF_FILTERING_DIS,
                     dec_container->ApiStorage.disable_filter);

      if(pc->use_pipeline) { /*CHECK !! */
        if(dec_container->FrameDesc.pic_coding_type == RV_B_PIC) {

          SetDecRegister(dec_container->rv_regs, HWIF_DEC_OUT_DIS,
                         1);

          /*frame or top or bottom */
          ASSERT((pc->pic_struct == DECPP_PIC_FRAME_OR_TOP_FIELD) ||
                 (pc->pic_struct == DECPP_PIC_BOT_FIELD));
        }
      }

      /*ASSERT(index_for_pp != dec_container->StrmStorage.work_out); */
      ASSERT(index_for_pp != RV_BUFFER_UNDEFINED);

      RVDEC_API_DEBUG(("send %d %d %d %d, index_for_pp %d\n",
                       dec_container->StrmStorage.p_pic_buf[0].send_to_pp,
                       dec_container->StrmStorage.p_pic_buf[1].send_to_pp,
                       dec_container->StrmStorage.p_pic_buf[2].send_to_pp,
                       dec_container->StrmStorage.p_pic_buf[3].send_to_pp,
                       index_for_pp));

      dec_container->StrmStorage.p_pic_buf[index_for_pp].send_to_pp = 0;

      dec_container->PPRun(dec_container->pp_instance, pc);

      pc->pp_status = DECPP_RUNNING;
    }
    dec_container->StrmStorage.previous_mode_full = 0;
  }

  if( dec_container->FrameDesc.pic_coding_type != RV_B_PIC ) {
    pc->prev_anchor_display_index = next_buffer_index;
  }

}

/*------------------------------------------------------------------------------

    Function name: RvPpMultiBufferInit

    Functional description:
        Modify state of pp output buffering.

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
void RvPpMultiBufferInit(DecContainer * dec_cont) {
  DecPpQuery *pq = &dec_cont->pp_config_query;
  DecPpInterface *pc = &dec_cont->pp_control;

  if(pq->multi_buffer &&
      !dec_cont->workarounds.rv.multibuffer ) {
    if(!pq->pipeline_accepted) {
      RVDEC_API_DEBUG(("MULTIBUFFER_SEMIMODE\n"));
      pc->multi_buf_stat = MULTIBUFFER_SEMIMODE;
    } else {
      RVDEC_API_DEBUG(("MULTIBUFFER_FULLMODE\n"));
      pc->multi_buf_stat = MULTIBUFFER_FULLMODE;
    }

    pc->buffer_index = 1;
  } else {
    pc->multi_buf_stat = MULTIBUFFER_DISABLED;
  }

}

/*------------------------------------------------------------------------------

    Function name: RvPpMultiBufferSetup

    Functional description:
        Modify state of pp output buffering.

    Input:
        container
        pipeline_off     override pipeline setting

    Return values:
        void

------------------------------------------------------------------------------*/
void RvPpMultiBufferSetup(DecContainer * dec_cont, u32 pipeline_off) {

  if(dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_DISABLED) {
    RVDEC_API_DEBUG(("MULTIBUFFER_DISABLED\n"));
    return;
  }

  if(pipeline_off && dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_FULLMODE) {
    RVDEC_API_DEBUG(("MULTIBUFFER_SEMIMODE\n"));
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_SEMIMODE;
  }

  if(!pipeline_off &&
      (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_SEMIMODE)) {
    RVDEC_API_DEBUG(("MULTIBUFFER_FULLMODE\n"));
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_FULLMODE;
  }

  if(dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_UNINIT)
    RvPpMultiBufferInit(dec_cont);

}

/*------------------------------------------------------------------------------

    Function name: RvDecRunFullmode

    Functional description:

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
void RvDecRunFullmode(DecContainer * dec_cont) {
  u32 index_for_pp = RV_BUFFER_UNDEFINED;
  DecPpInterface *pc = &dec_cont->pp_control;

#ifdef _DEC_PP_USAGE
  RvDecPpUsagePrint(dec_cont, DECPP_PIPELINED,
                    dec_cont->StrmStorage.work_out, 0,
                    dec_cont->StrmStorage.p_pic_buf[dec_cont->
                        StrmStorage.
                        work_out].pic_id);
#endif

  if(!dec_cont->StrmStorage.previous_mode_full &&
      dec_cont->FrameDesc.frame_number) {
    if(dec_cont->FrameDesc.pic_coding_type == RV_B_PIC) {
      dec_cont->StrmStorage.p_pic_buf[dec_cont->
                                      StrmStorage.work0].send_to_pp = 0;
      dec_cont->StrmStorage.p_pic_buf[dec_cont->
                                      StrmStorage.work1].send_to_pp = 0;
    } else {
      dec_cont->StrmStorage.p_pic_buf[dec_cont->
                                      StrmStorage.work0].send_to_pp = 0;
    }
  }

  if(dec_cont->FrameDesc.pic_coding_type == RV_B_PIC) {
    dec_cont->StrmStorage.previous_b = 1;
  } else {
    dec_cont->StrmStorage.previous_b = 0;
  }

  index_for_pp = dec_cont->StrmStorage.work_out;
  pc->input_bus_luma = dec_cont->StrmStorage.p_pic_buf[dec_cont->
                       StrmStorage.work_out].
                       data.bus_address;

  pc->input_bus_chroma = pc->input_bus_luma +
                         ((dec_cont->FrameDesc.frame_width *
                           dec_cont->FrameDesc.frame_height) << 8);

  pc->tiled_input_mode = dec_cont->tiled_reference_enable;
  pc->progressive_sequence = 1;
  pc->inwidth = pc->cropped_w = dec_cont->FrameDesc.frame_width << 4;
  pc->inheight = pc->cropped_h = dec_cont->FrameDesc.frame_height << 4;

  {
    if(dec_cont->FrameDesc.pic_coding_type == RV_B_PIC)
      SetDecRegister(dec_cont->rv_regs, HWIF_DEC_OUT_DIS, 1);

    /* always disabled in MPEG-2 */
    dec_cont->ApiStorage.disable_filter = 1;

    SetDecRegister(dec_cont->rv_regs, HWIF_FILTERING_DIS,
                   dec_cont->ApiStorage.disable_filter);
  }

  ASSERT(index_for_pp != RV_BUFFER_UNDEFINED);

  RVDEC_API_DEBUG(("send %d %d %d %d, index_for_pp %d\n",
                   dec_cont->StrmStorage.p_pic_buf[0].send_to_pp,
                   dec_cont->StrmStorage.p_pic_buf[1].send_to_pp,
                   dec_cont->StrmStorage.p_pic_buf[2].send_to_pp,
                   dec_cont->StrmStorage.p_pic_buf[3].send_to_pp,
                   index_for_pp));

  ASSERT(dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp == 1);
  dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp = 0;

  dec_cont->PPRun(dec_cont->pp_instance, pc);

  pc->pp_status = DECPP_RUNNING;
}

/*------------------------------------------------------------------------------

    Function name: RvDecPeek

    Functional description:
        Retrieve last decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct

    Output:
        picture Decoder output picture.

    Return values:
        RVDEC_OK         No picture available.
        RVDEC_PIC_RDY    Picture ready.

------------------------------------------------------------------------------*/
RvDecRet RvDecPeek(RvDecInst dec_inst, RvDecPicture * picture) {
  /* Variables */
  RvDecRet return_value = RVDEC_PIC_RDY;
  DecContainer *dec_cont;
  u32 pic_index = RV_BUFFER_UNDEFINED;

  /* Code */
  RV_API_TRC("\nRv_dec_peek#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    RV_API_TRC("RvDecPeek# ERROR: picture is NULL");
    return (RVDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    RV_API_TRC("RvDecPeek# ERROR: Decoder not initialized");
    return (RVDEC_NOT_INITIALIZED);
  }

#ifdef USE_OUTPUT_RELEASE
  /* when output release thread enabled, RvDecNextPicture_INTERNAL() called in
     RvDecDecode(), and "dec_cont->StrmStorage.out_count--" may called in
     RvDecNextPicture_INTERNAL() before RvDecPeek() called, so dec_cont->fullness
     used to sample the real out_count in case of RvDecNextPicture_INTERNAL() called
     before than RvDecPeek() */
  u32 tmp = dec_cont->fullness;
#else
  u32 tmp = dec_cont->StrmStorage.out_count;
#endif

  /* Nothing to send out */
  if(!tmp) {
    (void) DWLmemset(picture, 0, sizeof(RvDecPicture));
    return_value = RVDEC_OK;
  } else {
    pic_index = dec_cont->StrmStorage.work_out;

    RvFillPicStruct(picture, dec_cont, pic_index);

  }

  return return_value;
}

#ifdef USE_EXTERNAL_BUFFER
void RvSetExternalBufferInfo(DecContainer * dec_cont) {
  u32 pic_size_in_mbs = 0;
  u32 ext_buffer_size;

  /* set pic_size according to valid resolution here */
#if 0
  if(dec_cont->StrmStorage.max_frame_width) {
    if(dec_cont->pp_instance)
      pic_size_in_mbs = ((dec_cont->StrmStorage.max_frame_width + 15)>>4) *
                        ((((dec_cont->StrmStorage.max_frame_height + 15)>>4) + 1) / 2) * 2;
    else
      pic_size_in_mbs = dec_cont->StrmStorage.max_mbs_per_frame;
  } else
#endif
  {
    if(dec_cont->pp_instance)
      pic_size_in_mbs = ((dec_cont->Hdrs.horizontal_size + 15)>>4) *
                        ((((dec_cont->Hdrs.vertical_size + 15)>>4) + 1) / 2) * 2;
    else
      pic_size_in_mbs = ((dec_cont->Hdrs.horizontal_size + 15)>>4) *
                        ((dec_cont->Hdrs.vertical_size + 15)>>4);
  }

  u32 pic_size = pic_size_in_mbs * 384;

  u32 ref_buff_size = pic_size;
  ext_buffer_size = ref_buff_size;

  u32 buffers = 3;

  if( dec_cont->pp_instance ) { /* Combined mode used */
    buffers = 3;
  } else { /* Dec only or separate PP */
    buffers = dec_cont->StrmStorage.max_num_buffers;
    if( buffers < 3 )
      buffers = 3;
  }

  dec_cont->tot_buffers_added = dec_cont->tot_buffers;

  if(pic_size > (dec_cont->use_adaptive_buffers ?
                 dec_cont->n_ext_buf_size :  dec_cont->next_buf_size))
    dec_cont->no_reallocation = 0;

  if (dec_cont->pp_enabled) {
    u32 pp_width, pp_height, pp_stride, pp_buff_size;
    if (dec_cont->StrmStorage.max_frame_width) {
      pp_width = (((dec_cont->StrmStorage.max_frame_width + 15)>>4) * 16) >> dec_cont->dscale_shift_x;
      pp_height = (((dec_cont->StrmStorage.max_frame_height + 15)>>4) * 16) >> dec_cont->dscale_shift_y;
    } else {
      pp_width = (((dec_cont->Hdrs.horizontal_size + 15)>>4) * 16) >> dec_cont->dscale_shift_x;
      pp_height = (((dec_cont->Hdrs.vertical_size + 15)>>4) * 16) >> dec_cont->dscale_shift_y;
    }
    pp_stride = ((pp_width + 15) >> 4) << 4;
    pp_buff_size = pp_stride * pp_height * 3 / 2;
    ext_buffer_size = pp_buff_size;
  }

  dec_cont->tot_buffers_added = dec_cont->tot_buffers;
  if (dec_cont->pp_enabled)
    dec_cont->tot_buffers = dec_cont->buf_num =  buffers;
  else
    dec_cont->tot_buffers = dec_cont->buf_num =  buffers + 1;
  dec_cont->next_buf_size = ext_buffer_size;
}

RvDecRet RvDecGetBufferInfo(RvDecInst dec_inst, RvDecBufferInfo *mem_info) {
  DecContainer  * dec_cont = (DecContainer *)dec_inst;

  struct DWLLinearMem empty = {0, 0, 0, 0};

  struct DWLLinearMem *buffer = NULL;

  if(dec_cont == NULL || mem_info == NULL) {
    return RVDEC_PARAM_ERROR;
  }

  if (dec_cont->StrmStorage.release_buffer) {
    /* Release old buffers from input queue. */
    //buffer = InputQueueGetBuffer(decCont->pp_buffer_queue, 0);
    buffer = NULL;
    if (dec_cont->ext_buffer_num) {
      buffer = &dec_cont->ext_buffers[dec_cont->ext_buffer_num - 1];
      dec_cont->ext_buffer_num--;
    }
    if (buffer == NULL) {
      /* All buffers have been released. */
      dec_cont->StrmStorage.release_buffer = 0;
      InputQueueRelease(dec_cont->pp_buffer_queue);
      dec_cont->pp_buffer_queue = InputQueueInit(0);
      if (dec_cont->pp_buffer_queue == NULL) {
        return (RVDEC_MEMFAIL);
      }
      dec_cont->StrmStorage.ext_buffer_added = 0;
      mem_info->buf_to_free = empty;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return RVDEC_OK;
    } else {
      mem_info->buf_to_free = *buffer;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return RVDEC_WAITING_FOR_BUFFER;
    }
  }

  if(dec_cont->buf_to_free == NULL && dec_cont->next_buf_size == 0) {
    /* External reference buffer: release done. */
    mem_info->buf_to_free = empty;
    mem_info->next_buf_size = dec_cont->next_buf_size;
    mem_info->buf_num = dec_cont->buf_num;
    return RVDEC_OK;
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

  return RVDEC_WAITING_FOR_BUFFER;
}

RvDecRet RvDecAddBuffer(RvDecInst dec_inst, struct DWLLinearMem *info) {
  DecContainer *dec_cont = (DecContainer *)dec_inst;
  RvDecRet dec_ret = RVDEC_OK;

  if(dec_inst == NULL || info == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(info->virtual_address) ||
      X170_CHECK_BUS_ADDRESS_AGLINED(info->bus_address) ||
      info->size < dec_cont->next_buf_size) {
    return RVDEC_PARAM_ERROR;
  }

  u32 i = dec_cont->buffer_index;

  dec_cont->n_ext_buf_size = info->size;
  dec_cont->ext_buffers[dec_cont->ext_buffer_num] = *info;
  dec_cont->ext_buffer_num++;

  if (dec_cont->pp_enabled == 0) {
    if(i < dec_cont->tot_buffers) {
      if(i < (dec_cont->tot_buffers - 1)) {
        dec_cont->StrmStorage.p_pic_buf[i].data = *info;

        dec_cont->buffer_index++;
        if(dec_cont->buffer_index < dec_cont->tot_buffers)
          dec_ret = RVDEC_WAITING_FOR_BUFFER;
      } else {
        dec_cont->StrmStorage.p_rpr_buf.data = *info;
        dec_cont->buffer_index++;
        dec_ret = RVDEC_OK;
      }
    } else {
      /* Adding extra buffers. */
      if(i > 16) {
        /* Too much buffers added. */
        return RVDEC_EXT_BUFFER_REJECTED;
      }

      dec_cont->StrmStorage.p_pic_buf[i - 1].data = *info;

      dec_cont->buffer_index++;
      dec_cont->tot_buffers++;
      dec_cont->StrmStorage.bq.queue_size++;
      dec_cont->StrmStorage.num_buffers++;
    }
  } else {
    /* Add down scale buffer. */
    InputQueueAddBuffer(dec_cont->pp_buffer_queue, info);
  }
  return dec_ret;
}

#endif

#ifdef USE_OUTPUT_RELEASE
void RvEnterAbortState(DecContainer *dec_cont) {
  BqueueSetAbort(&dec_cont->StrmStorage.bq);
#ifdef USE_OMXIL_BUFFER
  FifoSetAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 1;
}

void RvExistAbortState(DecContainer *dec_cont) {
  BqueueClearAbort(&dec_cont->StrmStorage.bq);
#ifdef USE_OMXIL_BUFFER
  FifoClearAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 0;
}

void RvEmptyBufferQueue(DecContainer *dec_cont) {
  BqueueEmpty(&dec_cont->StrmStorage.bq);
  dec_cont->StrmStorage.work_out = 0;
  dec_cont->StrmStorage.work0 =
    dec_cont->StrmStorage.work1 = INVALID_ANCHOR_PICTURE;
}

void RvStateReset(DecContainer *dec_cont) {
  u32 buffers = 3;

  if( !dec_cont->pp_instance ) { /* Combined mode used */
    buffers = dec_cont->StrmStorage.max_num_buffers;
    if( buffers < 3 )
      buffers = 3;
  }

  /* Clear internal parameters in DecContainer */
#ifdef USE_EXTERNAL_BUFFER
#ifdef USE_OMXIL_BUFFER
  dec_cont->tot_buffers = buffers + 1;
  dec_cont->buffer_index = 0;
#endif
  dec_cont->no_reallocation = 1;
#endif
  dec_cont->fullness = 0;
#ifdef USE_OMXIL_BUFFER
  dec_cont->fifo_index = 0;
  dec_cont->ext_buffer_num = 0;
#endif
  dec_cont->same_slice_header = 0;
  dec_cont->mb_error_conceal = 0;
  dec_cont->dec_stat = RVDEC_OK;
  dec_cont->output_stat = RVDEC_OK;

  /* Clear internal parameters in DecStrmStorage */
#ifdef CLEAR_HDRINFO_IN_SEEK
  dec_cont->StrmStorage.strm_dec_ready = 0;
#endif
  dec_cont->StrmStorage.out_index = 0;
  dec_cont->StrmStorage.out_count = 0;
  dec_cont->StrmStorage.skip_b = 0;
  dec_cont->StrmStorage.prev_pic_coding_type = 0;
  dec_cont->StrmStorage.picture_broken = 0;
  dec_cont->StrmStorage.rpr_detected = 0;
  dec_cont->StrmStorage.rpr_next_pic_type = 0;
  dec_cont->StrmStorage.previous_b = 0;
  dec_cont->StrmStorage.previous_mode_full = 0;
  dec_cont->StrmStorage.fwd_scale = 0;
  dec_cont->StrmStorage.bwd_scale = 0;
  dec_cont->StrmStorage.tr = 0;
  dec_cont->StrmStorage.prev_tr = 0;
  dec_cont->StrmStorage.trb = 0;
  dec_cont->StrmStorage.frame_size_bits = 0;
  dec_cont->StrmStorage.pic_id = 0;
  dec_cont->StrmStorage.prev_pic_id = 0;
  dec_cont->StrmStorage.prev_bidx = 0;
#ifdef USE_OMXIL_BUFFER
  dec_cont->StrmStorage.bq.queue_size = buffers;
  dec_cont->StrmStorage.num_buffers = buffers;
#endif

  /* Clear internal parameters in DecApiStorage */
  dec_cont->ApiStorage.DecStat = STREAMDECODING;

  /* Clear internal parameters in DecFrameDesc */
  dec_cont->FrameDesc.frame_number = 0;
  dec_cont->FrameDesc.pic_coding_type = 0;
  dec_cont->FrameDesc.vlc_set = 0;
  dec_cont->FrameDesc.qp = 0;

  (void) DWLmemset(&dec_cont->MbSetDesc, 0, sizeof(DecMbSetDesc));
  (void) DWLmemset(&dec_cont->StrmDesc, 0, sizeof(DecStrmDesc));
  (void) DWLmemset(&dec_cont->out_pic, 0, sizeof(RvDecPicture));
  (void) DWLmemset(dec_cont->StrmStorage.out_buf, 0, 16 * sizeof(u32));
#ifdef USE_OMXIL_BUFFER
  (void) DWLmemset(dec_cont->StrmStorage.p_pic_buf, 0, 16 * sizeof(picture_t));
  (void) DWLmemset(&dec_cont->StrmStorage.p_rpr_buf, 0, sizeof(picture_t));
  (void) DWLmemset(dec_cont->StrmStorage.picture_info, 0, 32 * sizeof(RvDecPicture));
#endif
#ifdef CLEAR_HDRINFO_IN_SEEK
  (void) DWLmemset(&dec_cont->Hdrs, 0, sizeof(DecHdrs));
  (void) DWLmemset(&dec_cont->tmp_hdrs, 0, sizeof(DecHdrs));
#endif

#ifdef USE_OMXIL_BUFFER
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
  FifoInit(32, &dec_cont->fifo_display);
#endif
}

RvDecRet RvDecAbort(RvDecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;

  RV_API_TRC("RvDecAbort#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    RV_API_TRC("RvDecAbort# ERROR: Decoder not initialized");
    return (RVDEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);
  /* Abort frame buffer waiting */
  RvEnterAbortState(dec_cont);
  pthread_mutex_unlock(&dec_cont->protect_mutex);

  RV_API_TRC("RvDecAbort# RVDEC_OK\n");
  return (RVDEC_OK);
}

RvDecRet RvDecAbortAfter(RvDecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;

  RV_API_TRC("RvDecAbortAfter#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    RV_API_TRC("RvDecAbortAfter# ERROR: Decoder not initialized");
    return (RVDEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);

#if 0
  /* If normal EOS is waited, return directly */
  if(dec_cont->dec_stat == RVDEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (RVDEC_OK);
  }
#endif

  /* Stop and release HW */
  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->rv_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->rv_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->rv_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->rv_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  /* Clear any remaining pictures from DPB */
  RvEmptyBufferQueue(dec_cont);

  RvStateReset(dec_cont);

  RvExistAbortState(dec_cont);

  pthread_mutex_unlock(&dec_cont->protect_mutex);

  RV_API_TRC("RvDecAbortAfter# RVDEC_OK\n");
  return (RVDEC_OK);
}
#endif
