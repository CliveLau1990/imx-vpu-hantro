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
#include "avsdecapi.h"
#include "basetype.h"
#include "avs_cfg.h"
#include "avs_container.h"
#include "avs_utils.h"
#include "avs_strm.h"
#include "avsdecapi_internal.h"
#include "dwl.h"
#include "regdrv_g1.h"
#include "avs_headers.h"
#include "deccfg.h"
#include "refbuffer.h"
#include "tiledref.h"
#include "errorhandling.h"
#include "commonconfig.h"

#ifdef AVSDEC_TRACE
#define AVS_API_TRC(str)    AvsDecTrace((str))
#else
#define AVS_API_TRC(str)
#endif

#define AVS_BUFFER_UNDEFINED    16

#define ID8170_DEC_TIMEOUT        0xFFU
#define ID8170_DEC_SYSTEM_ERROR   0xFEU
#define ID8170_DEC_HW_RESERVED    0xFDU

#define AVSDEC_IS_FIELD_OUTPUT \
    !dec_cont->Hdrs.progressive_sequence && !dec_cont->pp_config_query.deinterlace

#define AVSDEC_NON_PIPELINE_AND_B_PICTURE \
    ((!dec_cont->pp_config_query.pipeline_accepted || !dec_cont->Hdrs.progressive_sequence) \
    && dec_cont->Hdrs.pic_coding_type == BFRAME)
void AvsRefreshRegs(DecContainer * dec_cont);
void AvsFlushRegs(DecContainer * dec_cont);
static u32 AvsHandleVlcModeError(DecContainer * dec_cont, u32 pic_num);
static void AvsHandleFrameEnd(DecContainer * dec_cont);
static u32 RunDecoderAsic(DecContainer * dec_container, addr_t strm_bus_address);
static void AvsFillPicStruct(AvsDecPicture * picture,
                             DecContainer * dec_cont, u32 pic_index);
static u32 AvsSetRegs(DecContainer * dec_container, addr_t strm_bus_address);
static void AvsDecSetupDeinterlace(DecContainer * dec_cont);
static void AvsDecPrepareFieldProcessing(DecContainer * dec_cont);
static u32 AvsCheckFormatSupport(void);
static void AvsPpControl(DecContainer * dec_container, u32 pipeline_off);
static void AvsPpMultiBufferInit(DecContainer * dec_cont);
static void AvsPpMultiBufferSetup(DecContainer * dec_cont, u32 pipeline_off);
static void AvsDecRunFullmode(DecContainer * dec_cont);

#ifdef USE_OUTPUT_RELEASE
static AvsDecRet AvsDecNextPicture_INTERNAL(AvsDecInst dec_inst,
    AvsDecPicture * picture, u32 end_of_stream);
#endif

#ifdef USE_EXTERNAL_BUFFER
static void AvsSetExternalBufferInfo(AvsDecInst dec_inst);
#endif

#ifdef USE_OUTPUT_RELEASE
static void AvsEnterAbortState(DecContainer *dec_cont);
static void AvsExistAbortState(DecContainer *dec_cont);
static void AvsEmptyBufferQueue(DecContainer *dec_cont);
#endif

#define DEC_X170_MODE_AVS   11
#define DEC_DPB_NOT_INITIALIZED      -1

/*------------------------------------------------------------------------------
       Version Information - DO NOT CHANGE!
------------------------------------------------------------------------------*/

#define AVSDEC_MAJOR_VERSION 1
#define AVSDEC_MINOR_VERSION 1

/*------------------------------------------------------------------------------

    Function: AvsDecGetAPIVersion

        Functional description:
            Return version information of API

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

AvsDecApiVersion AvsDecGetAPIVersion() {
  AvsDecApiVersion ver;

  ver.major = AVSDEC_MAJOR_VERSION;
  ver.minor = AVSDEC_MINOR_VERSION;

  return (ver);
}

/*------------------------------------------------------------------------------

    Function: AvsDecGetBuild

        Functional description:
            Return build information of SW and HW

        Returns:
            AvsDecBuild

------------------------------------------------------------------------------*/

AvsDecBuild AvsDecGetBuild(void) {
  AvsDecBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_AVS_DEC);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_AVS_DEC);

  AVS_API_TRC("AvsDecGetBuild# OK\n");

  return build_info;
}

/*------------------------------------------------------------------------------

    Function: AvsDecInit()

        Functional description:
            Initialize decoder software. Function reserves memory for the
            decoder instance.

        Inputs:
            enum DecErrorHandling error_handling
                            Flag to determine which error concealment method to use.

        Outputs:
            dec_inst         pointer to initialized instance is stored here

        Returns:
            AVSDEC_OK       successfully initialized the instance
            AVSDEC_MEM_FAIL memory allocation failed

------------------------------------------------------------------------------*/
AvsDecRet AvsDecInit(AvsDecInst * dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                     const void *dwl,
#endif
                     enum DecErrorHandling error_handling,
                     u32 num_frame_buffers,
                     enum DecDpbFlags dpb_flags,
                     u32 use_adaptive_buffers,
                     u32 n_guard_size,
                     struct DecDownscaleCfg *dscale_cfg) {
  /*@null@ */ DecContainer *dec_cont;
#ifndef USE_EXTERNAL_BUFFER
  /*@null@ */ const void *dwl;
#endif
  u32 i = 0;
  //u32 version = 0;

#ifndef USE_EXTERNAL_BUFFER
  struct DWLInitParam dwl_init;
#endif
  DWLHwConfig config;
  u32 reference_frame_format;

  AVS_API_TRC("AvsDecInit#");
  AVSDEC_DEBUG(("AvsAPI_DecoderInit#"));

  /* check that right shift on negative numbers is performed signed */
  /*lint -save -e* following check causes multiple lint messages */
  if(((-1) >> 1) != (-1)) {
    AVSDEC_DEBUG(("AVSDecInit# ERROR: Right shift is not signed"));
    return (AVSDEC_INITFAIL);
  }
  /*lint -restore */

  if(dec_inst == NULL) {
    AVSDEC_DEBUG(("AVSDecInit# ERROR: dec_inst == NULL"));
    return (AVSDEC_PARAM_ERROR);
  }

  *dec_inst = NULL;

  /* check that AVS decoding supported in HW */
  if(AvsCheckFormatSupport()) {
    AVSDEC_DEBUG(("AVSDecInit# ERROR: AVS not supported in HW\n"));
    return AVSDEC_FORMAT_NOT_SUPPORTED;
  }

#ifndef USE_EXTERNAL_BUFFER
  dwl_init.client_type = DWL_CLIENT_TYPE_AVS_DEC;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    AVSDEC_DEBUG(("AVSDecInit# ERROR: DWL Init failed"));
    return (AVSDEC_DWL_ERROR);
  }
#endif

  AVSDEC_DEBUG(("size of DecContainer %d \n", sizeof(DecContainer)));
  dec_cont = (DecContainer *) DWLmalloc(sizeof(DecContainer));

  if(dec_cont == NULL) {
#ifndef USE_EXTERNAL_BUFFER
    (void) DWLRelease(dwl);
#endif
    return (AVSDEC_MEMFAIL);
  }

  /* set everything initially zero */
  (void) DWLmemset(dec_cont, 0, sizeof(DecContainer));

  dec_cont->dwl = dwl;

  pthread_mutex_init(&dec_cont->protect_mutex, NULL);

  if(num_frame_buffers > 16)   num_frame_buffers = 16;

  dec_cont->StrmStorage.max_num_buffers = num_frame_buffers;

  AvsAPI_InitDataStructures(dec_cont);

  dec_cont->ApiStorage.DecStat = INITIALIZED;
  dec_cont->ApiStorage.first_field = 1;
  dec_cont->StrmStorage.unsupported_features_present = 0;

  *dec_inst = (DecContainer *) dec_cont;

  for(i = 0; i < TOTAL_X170_REGISTERS; i++)
    dec_cont->avs_regs[i] = 0;

  SetCommonConfigRegs(dec_cont->avs_regs,DWL_CLIENT_TYPE_AVS_DEC);

  /* Set prediction filter taps */
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_0_0,-1);
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_0_1, 5);
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_0_2, 5);
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_0_3,-1);
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_1_0, 1);
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_1_1, 7);
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_1_2, 7);
  SetDecRegister(dec_cont->avs_regs, HWIF_PRED_BC_TAP_1_3, 1);

  (void)DWLmemset(&config, 0, sizeof(DWLHwConfig));

  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_AVS_DEC);

  if(!config.addr64_support && sizeof(void *) == 8) {
    AVSDEC_DEBUG("AVSDecInit# ERROR: HW not support 64bit address!\n");
    return (AVSDEC_PARAM_ERROR);
  }

  i = DWLReadAsicID(DWL_CLIENT_TYPE_AVS_DEC);

  if((i >> 16) == 0x8170U)
    error_handling = 0;

  //version = (i >> 4) & 0xFFF;

  AVSDEC_DEBUG(("AVS Plus supported: %s\n", config.avs_plus_support? "YES" : "NO"));
  dec_cont->avs_plus_support = config.avs_plus_support;

  dec_cont->ref_buf_support = config.ref_buf_support;
  reference_frame_format = dpb_flags & DEC_REF_FRM_FMT_MASK;

  if(reference_frame_format == DEC_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!config.tiled_mode_support) {
      return AVSDEC_FORMAT_NOT_SUPPORTED;
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
    return (AVSDEC_PARAM_ERROR);
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
    return (AVSDEC_MEMFAIL);
  }
  dec_cont->StrmStorage.release_buffer = 0;

  /* Custom DPB modes require tiled support >= 2 */
  dec_cont->allow_dpb_field_ordering = 0;
  dec_cont->dpb_mode = DEC_DPB_NOT_INITIALIZED;

  if( dpb_flags & DEC_DPB_ALLOW_FIELD_ORDERING )
    dec_cont->allow_dpb_field_ordering = config.field_dpb_support;

  dec_cont->StrmStorage.intra_freeze = error_handling == DEC_EC_VIDEO_FREEZE;

  if (error_handling == DEC_EC_PARTIAL_FREEZE)
    dec_cont->StrmStorage.partial_freeze = 1;
  else if (error_handling == DEC_EC_PARTIAL_IGNORE)
    dec_cont->StrmStorage.partial_freeze = 2;

  dec_cont->StrmStorage.picture_broken = 0;

#ifdef USE_OUTPUT_RELEASE
  /* take top/botom fields into consideration */
  if (FifoInit(32, &dec_cont->fifo_display) != FIFO_OK)
    return AVSDEC_MEMFAIL;
#endif

  dec_cont->use_adaptive_buffers = use_adaptive_buffers;
  dec_cont->n_guard_size = n_guard_size;

  /* If tile mode is enabled, should take DTRC minimum size(96x48) into consideration */
  if (dec_cont->tiled_mode_support) {
    dec_cont->min_dec_pic_width = AVS_MIN_WIDTH_EN_DTRC;
    dec_cont->min_dec_pic_height = AVS_MIN_HEIGHT_EN_DTRC;
  } else {
    dec_cont->min_dec_pic_width = AVS_MIN_WIDTH;
    dec_cont->min_dec_pic_height = AVS_MIN_HEIGHT;
  }

  AVSDEC_DEBUG(("Container 0x%x\n", (u32) dec_cont));
  AVS_API_TRC("AvsDecInit: OK\n");

  return (AVSDEC_OK);
}

/*------------------------------------------------------------------------------

    Function: AvsDecGetInfo()

        Functional description:
            This function provides read access to decoder information. This
            function should not be called before AvsDecDecode function has
            indicated that headers are ready.

        Inputs:
            dec_inst     decoder instance

        Outputs:
            dec_info    pointer to info struct where data is written

        Returns:
            AVSDEC_OK            success
            AVSDEC_PARAM_ERROR     invalid parameters

------------------------------------------------------------------------------*/
AvsDecRet AvsDecGetInfo(AvsDecInst dec_inst, AvsDecInfo * dec_info) {

#define API_STOR ((DecContainer *)dec_inst)->ApiStorage
#define DEC_STST ((DecContainer *)dec_inst)->StrmStorage
#define DEC_HDRS ((DecContainer *)dec_inst)->Hdrs
#define DEC_REGS ((DecContainer *)dec_inst)->avs_regs

  AVS_API_TRC("AvsDecGetInfo#");

  if(dec_inst == NULL || dec_info == NULL) {
    return AVSDEC_PARAM_ERROR;
  }

  dec_info->multi_buff_pp_size = 2;

  if(API_STOR.DecStat == UNINIT || API_STOR.DecStat == INITIALIZED) {
    return AVSDEC_HDRS_NOT_RDY;
  }

  if (!((DecContainer *)dec_inst)->pp_enabled) {
    dec_info->frame_width = DEC_STST.frame_width << 4;
    dec_info->frame_height = DEC_STST.frame_height << 4;

    dec_info->coded_width = DEC_HDRS.horizontal_size;
    dec_info->coded_height = DEC_HDRS.vertical_size;
  } else {
    dec_info->frame_width = (DEC_STST.frame_width << 4) >> ((DecContainer *)dec_inst)->dscale_shift_x;
    dec_info->frame_height = (DEC_STST.frame_height << 4) >> ((DecContainer *)dec_inst)->dscale_shift_y;

    dec_info->coded_width = DEC_HDRS.horizontal_size >> ((DecContainer *)dec_inst)->dscale_shift_x;
    dec_info->coded_height = DEC_HDRS.vertical_size >> ((DecContainer *)dec_inst)->dscale_shift_y;
  }

  dec_info->profile_id = DEC_HDRS.profile_id;
  dec_info->level_id = DEC_HDRS.level_id;
  dec_info->video_range = DEC_HDRS.sample_range;
  dec_info->video_format = DEC_HDRS.video_format;
  dec_info->interlaced_sequence = !DEC_HDRS.progressive_sequence;
  dec_info->dpb_mode = ((DecContainer *)dec_inst)->dpb_mode;
#ifdef USE_EXTERNAL_BUFFER
  dec_info->pic_buff_size = ((DecContainer *)dec_inst)->buf_num;
#endif

  AvsDecAspectRatio((DecContainer *) dec_inst, dec_info);

  if(((DecContainer *)dec_inst)->tiled_mode_support) {
    if(!DEC_HDRS.progressive_sequence &&
        (dec_info->dpb_mode != DEC_DPB_INTERLACED_FIELD)) {
      dec_info->output_format = AVSDEC_SEMIPLANAR_YUV420;
    } else {
      dec_info->output_format = AVSDEC_TILED_YUV420;
    }
  } else {
    dec_info->output_format = AVSDEC_SEMIPLANAR_YUV420;
  }

  AVS_API_TRC("AvsDecGetInfo: OK");
  return (AVSDEC_OK);

#undef API_STOR
#undef DEC_STST
#undef DEC_HDRS
#undef DEC_REGS

}

/*------------------------------------------------------------------------------

    Function: AvsDecDecode

        Functional description:
            Decode stream data. Calls StrmDec_Decode to do the actual decoding.

        Input:
            dec_inst     decoder instance
            input      pointer to input struct

        Outputs:
            output     pointer to output struct

        Returns:
            AVSDEC_NOT_INITIALIZED   decoder instance not initialized yet
            AVSDEC_PARAM_ERROR       invalid parameters

            AVSDEC_STRM_PROCESSED    stream buffer decoded
            AVSDEC_HDRS_RDY          headers decoded
            AVSDEC_PIC_DECODED       decoding of a picture finished
            AVSDEC_STRM_ERROR        serious error in decoding, no
                                       valid parameter sets available
                                       to decode picture data

------------------------------------------------------------------------------*/

AvsDecRet AvsDecDecode(AvsDecInst dec_inst,
                       AvsDecInput * input, AvsDecOutput * output) {
#define API_STOR ((DecContainer *)dec_inst)->ApiStorage
#define DEC_STRM ((DecContainer *)dec_inst)->StrmDesc

  DecContainer *dec_cont;
  AvsDecRet internal_ret;
  u32 strm_dec_result;
  u32 asic_status;
  i32 ret = 0;
  u32 field_rdy = 0;
  u32 error_concealment = 0;

  AVS_API_TRC("\nAvs_dec_decode#");

  if(input == NULL || output == NULL || dec_inst == NULL) {
    AVS_API_TRC("AvsDecDecode# PARAM_ERROR\n");
    return AVSDEC_PARAM_ERROR;
  }

  dec_cont = ((DecContainer *) dec_inst);

  if(dec_cont->StrmStorage.unsupported_features_present) {
    return (AVSDEC_FORMAT_NOT_SUPPORTED);
  }

  /*
   *  Check if decoder is in an incorrect mode
   */
  if(API_STOR.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecDecode: NOT_INITIALIZED\n");
    return AVSDEC_NOT_INITIALIZED;
  }
#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort) {
    return (AVSDEC_ABORTED);
  }
#endif

  if(input->data_len == 0 ||
      input->data_len > DEC_X170_MAX_STREAM ||
      input->stream == NULL || input->stream_bus_address == 0) {
    AVS_API_TRC("AvsDecDecode# PARAM_ERROR\n");
    return AVSDEC_PARAM_ERROR;
  }

  /* If we have set up for delayed resolution change, do it here */
  if(dec_cont->StrmStorage.new_headers_change_resolution) {
    dec_cont->StrmStorage.new_headers_change_resolution = 0;
    dec_cont->Hdrs.horizontal_size = dec_cont->tmp_hdrs.horizontal_size;
    dec_cont->Hdrs.vertical_size = dec_cont->tmp_hdrs.vertical_size;
    /* Set rest of parameters just in case */
    dec_cont->Hdrs.aspect_ratio = dec_cont->tmp_hdrs.aspect_ratio;
    dec_cont->Hdrs.frame_rate_code = dec_cont->tmp_hdrs.frame_rate_code;
    dec_cont->Hdrs.bit_rate_value = dec_cont->tmp_hdrs.bit_rate_value;

#ifdef USE_EXTERNAL_BUFFER
    /* Recalculate flag "no_reallocation". */
    if (((dec_cont->Hdrs.horizontal_size + 15) >> 4) *
        ((dec_cont->Hdrs.vertical_size + 15) >> 4) * 384 >
        (dec_cont->use_adaptive_buffers ? dec_cont->n_ext_buf_size :
         (dec_cont->StrmStorage.frame_width * dec_cont->StrmStorage.frame_height * 384)))
      dec_cont->no_reallocation = 0;
#endif


    dec_cont->StrmStorage.frame_width =
      (dec_cont->Hdrs.horizontal_size + 15) >> 4;
    if(dec_cont->Hdrs.progressive_sequence)
      dec_cont->StrmStorage.frame_height =
        (dec_cont->Hdrs.vertical_size + 15) >> 4;
    else
      dec_cont->StrmStorage.frame_height =
        2 * ((dec_cont->Hdrs.vertical_size + 31) >> 5);
    dec_cont->StrmStorage.total_mbs_in_frame =
      (dec_cont->StrmStorage.frame_width *
       dec_cont->StrmStorage.frame_height);
  }

  if(API_STOR.DecStat == HEADERSDECODED) {
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
        return AVSDEC_WAITING_FOR_BUFFER;
      }
#endif
      AvsFreeBuffers(dec_cont);
      /* If buffers not allocated */
#ifndef USE_EXTERNAL_BUFFER
      if(!dec_cont->StrmStorage.p_pic_buf[0].data.bus_address)
#else
      if(!dec_cont->StrmStorage.direct_mvs.virtual_address)
#endif
      {
        AVSDEC_DEBUG(("Allocate buffers\n"));
        internal_ret = AvsAllocateBuffers(dec_cont);
        if(internal_ret != AVSDEC_OK) {
          AVSDEC_DEBUG(("ALLOC BUFFER FAIL\n"));
          AVS_API_TRC("AvsDecDecode# MEMFAIL\n");
          return (internal_ret);
        }
      }

      /* Headers ready now, mems allocated, decoding can start */
#ifndef USE_EXTERNAL_BUFFER
      API_STOR.DecStat = STREAMDECODING;
#endif
    }
  }

  /*
   *  Update stream structure
   */
  DEC_STRM.p_strm_buff_start = input->stream;
  DEC_STRM.strm_curr_pos = input->stream;
  DEC_STRM.bit_pos_in_word = 0;
  DEC_STRM.strm_buff_size = input->data_len;
  DEC_STRM.strm_buff_read_bits = 0;

#ifdef _DEC_PP_USAGE
  dec_cont->StrmStorage.latest_id = input->pic_id;
#endif
  do {
    AVSDEC_DEBUG(("Start Decode\n"));
    /* run SW if HW is not in the middle of processing a picture
     * (indicated as HW_PIC_STARTED decoder status) */
#ifdef USE_EXTERNAL_BUFFER
    if(API_STOR.DecStat == HEADERSDECODED) {
      API_STOR.DecStat = STREAMDECODING;
      if(!dec_cont->no_reallocation) {
        dec_cont->buffer_index = 0;
        dec_cont->no_reallocation = 1;
        ret =  AVSDEC_WAITING_FOR_BUFFER;
      }
    } else if(API_STOR.DecStat != HW_PIC_STARTED)
#else
    if(API_STOR.DecStat != HW_PIC_STARTED)
#endif
    {
      strm_dec_result = AvsStrmDec_Decode(dec_cont);
      /* TODO: Could it be odd field? If, so release PP and Hw. */
      switch (strm_dec_result) {
      case DEC_PIC_HDR_RDY:
        /* if type inter predicted and no reference -> error */
        if((dec_cont->Hdrs.pic_coding_type == PFRAME &&
            dec_cont->StrmStorage.work0 == INVALID_ANCHOR_PICTURE) ||
            (dec_cont->Hdrs.pic_coding_type == BFRAME &&
             (dec_cont->StrmStorage.work0 == INVALID_ANCHOR_PICTURE ||
              dec_cont->StrmStorage.skip_b ||
              input->skip_non_reference)) ||
            (dec_cont->Hdrs.pic_coding_type == PFRAME &&
             dec_cont->StrmStorage.picture_broken &&
             dec_cont->StrmStorage.intra_freeze) ) {
          if(dec_cont->StrmStorage.skip_b ||
              input->skip_non_reference) {
            AVS_API_TRC("AvsDecDecode# AVSDEC_NONREF_PIC_SKIPPED\n");
          }
          if (!dec_cont->ApiStorage.first_field && dec_cont->pp_enabled)
            InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data->virtual_address);
          ret = AvsHandleVlcModeError(dec_cont, input->pic_id);
          error_concealment = 1;
        } else
          API_STOR.DecStat = HW_PIC_STARTED;
        break;

      case DEC_PIC_SUPRISE_B:
        /* Handle suprise B */
        dec_cont->Hdrs.low_delay = 0;

        AvsDecBufferPicture(dec_cont,
                            input->pic_id, 1, 0,
                            AVSDEC_PIC_DECODED, 0);

        ret = AvsHandleVlcModeError(dec_cont, input->pic_id);
        error_concealment = 1;
        break;

      case DEC_PIC_HDR_RDY_ERROR:
        if(dec_cont->StrmStorage.unsupported_features_present) {
          dec_cont->StrmStorage.unsupported_features_present = 0;
          return AVSDEC_STREAM_NOT_SUPPORTED;
        }
        if (!dec_cont->ApiStorage.first_field && dec_cont->pp_enabled)
          InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data->virtual_address);
        ret = AvsHandleVlcModeError(dec_cont, input->pic_id);
        error_concealment = 1;
        break;

      case DEC_HDRS_RDY:

        internal_ret = AvsDecCheckSupport(dec_cont);
        if(internal_ret != AVSDEC_OK) {
          dec_cont->StrmStorage.strm_dec_ready = FALSE;
          dec_cont->StrmStorage.valid_sequence = 0;
          API_STOR.DecStat = INITIALIZED;
          return internal_ret;
        }

        if(dec_cont->ApiStorage.first_headers) {
          dec_cont->ApiStorage.first_headers = 0;

          SetDecRegister(dec_cont->avs_regs, HWIF_PIC_MB_WIDTH,
                         dec_cont->StrmStorage.frame_width);

          SetDecRegister(dec_cont->avs_regs, HWIF_DEC_MODE,
                         DEC_X170_MODE_AVS);

          if (dec_cont->ref_buf_support) {
            RefbuInit(&dec_cont->ref_buffer_ctrl,
                      DEC_X170_MODE_AVS,
                      dec_cont->StrmStorage.frame_width,
                      dec_cont->StrmStorage.frame_height,
                      dec_cont->ref_buf_support);
          }
        }

        /* Initialize DPB mode */
        if( !dec_cont->Hdrs.progressive_sequence &&
            dec_cont->allow_dpb_field_ordering )
          dec_cont->dpb_mode = DEC_DPB_INTERLACED_FIELD;
        else
          dec_cont->dpb_mode = DEC_DPB_FRAME;

        /* Initialize tiled mode */
        if( dec_cont->tiled_mode_support) {
          /* Check mode validity */
          if(DecCheckTiledMode( dec_cont->tiled_mode_support,
                                dec_cont->dpb_mode,
                                !dec_cont->Hdrs.progressive_sequence ) !=
              HANTRO_OK ) {
            AVS_API_TRC("AvsDecDecode# ERROR: DPB mode does not "\
                        "support tiled reference pictures");
            return AVSDEC_PARAM_ERROR;
          }
        }

        API_STOR.DecStat = HEADERSDECODED;
#ifdef USE_EXTERNAL_BUFFER
        AvsSetExternalBufferInfo(dec_cont);

        if (dec_cont->no_reallocation &&
            (!dec_cont->use_adaptive_buffers ||
             (dec_cont->use_adaptive_buffers &&
              dec_cont->tot_buffers + dec_cont->n_guard_size <= dec_cont->tot_buffers_added)))
          ret = AVSDEC_STRM_PROCESSED;
        else
#endif
        {
#ifdef USE_OUTPUT_RELEASE
          FifoPush(dec_cont->fifo_display, -2, FIFO_EXCEPTION_DISABLE);
#endif
          AVSDEC_DEBUG(("HDRS_RDY\n"));
          ret = AVSDEC_HDRS_RDY;
        }
        break;

      default:
        ASSERT(strm_dec_result == DEC_END_OF_STREAM);
        if (dec_cont->StrmStorage.new_headers_change_resolution)
          ret = AVSDEC_PIC_DECODED;
        else
          ret = AVSDEC_STRM_PROCESSED;
        break;
      }
    }

    /* picture header properly decoded etc -> start HW */
    if(API_STOR.DecStat == HW_PIC_STARTED) {
      if(dec_cont->ApiStorage.first_field &&
          !dec_cont->asic_running) {
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp_instance == NULL) {
          dec_cont->StrmStorage.work_out = BqueueNext2(
                                             &dec_cont->StrmStorage.bq,
                                             dec_cont->StrmStorage.work0,
                                             dec_cont->StrmStorage.work1,
                                             BQUEUE_UNUSED,
                                             dec_cont->Hdrs.pic_coding_type == BFRAME );
          if(dec_cont->StrmStorage.work_out == (u32)0xFFFFFFFFU) {
            if (dec_cont->abort)
              return AVSDEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
            else {
              ret = AVSDEC_NO_DECODING_BUFFER;
              break;
            }
#endif
          }
          dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].first_show = 1;
        } else {
          dec_cont->StrmStorage.work_out = BqueueNext(
                                             &dec_cont->StrmStorage.bq,
                                             dec_cont->StrmStorage.work0,
                                             dec_cont->StrmStorage.work1,
                                             BQUEUE_UNUSED,
                                             dec_cont->Hdrs.pic_coding_type == BFRAME );
        }
#else
        dec_cont->StrmStorage.work_out = BqueueNext(
                                           &dec_cont->StrmStorage.bq,
                                           dec_cont->StrmStorage.work0,
                                           dec_cont->StrmStorage.work1,
                                           BQUEUE_UNUSED,
                                           dec_cont->Hdrs.pic_coding_type == BFRAME );
#endif
        if (dec_cont->pp_enabled) {
          dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data = InputQueueGetBuffer(dec_cont->pp_buffer_queue, 1);
        }
      }

      if (!dec_cont->asic_running && dec_cont->StrmStorage.partial_freeze) {
        PreparePartialFreeze(
          (u8*)dec_cont->StrmStorage.p_pic_buf[
            (i32)dec_cont->StrmStorage.work_out].data.virtual_address,
          dec_cont->StrmStorage.frame_width,
          dec_cont->StrmStorage.frame_height);
      }

      asic_status = RunDecoderAsic(dec_cont, input->stream_bus_address);

      if(asic_status == ID8170_DEC_TIMEOUT) {
        ret = AVSDEC_HW_TIMEOUT;
      } else if(asic_status == ID8170_DEC_SYSTEM_ERROR) {
        ret = AVSDEC_SYSTEM_ERROR;
      } else if(asic_status == ID8170_DEC_HW_RESERVED) {
        ret = AVSDEC_HW_RESERVED;
      } else if(asic_status & DEC_8190_IRQ_ABORT) {
        AVSDEC_DEBUG(("IRQ ABORT IN HW\n"));
        ret = AvsHandleVlcModeError(dec_cont, input->pic_id);
        error_concealment = 1;
      } else if( (asic_status & AVS_DEC_X170_IRQ_STREAM_ERROR) ||
                 (asic_status & AVS_DEC_X170_IRQ_TIMEOUT) ) {
        if (!dec_cont->StrmStorage.partial_freeze ||
            !ProcessPartialFreeze(
              (u8*)dec_cont->StrmStorage.p_pic_buf[
                (i32)dec_cont->StrmStorage.work_out].data.virtual_address,
              dec_cont->StrmStorage.work0 != INVALID_ANCHOR_PICTURE ?
              (u8*)dec_cont->StrmStorage.p_pic_buf[
                (i32)dec_cont->StrmStorage.work0].data.virtual_address :
              NULL,
              dec_cont->StrmStorage.frame_width,
              dec_cont->StrmStorage.frame_height,
              dec_cont->StrmStorage.partial_freeze == 1)) {
          if (asic_status & AVS_DEC_X170_IRQ_STREAM_ERROR) {
            AVSDEC_DEBUG(("STREAM ERROR IN HW\n"));
          } else {
            AVSDEC_DEBUG(("IRQ TIMEOUT IN HW\n"));
          }

          if (dec_cont->pp_enabled) {
            InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data->virtual_address);
          }
          ret = AvsHandleVlcModeError(dec_cont, input->pic_id);
          error_concealment = 1;
          if (asic_status & AVS_DEC_X170_IRQ_STREAM_ERROR) {
            dec_cont->StrmStorage.prev_pic_coding_type =
              dec_cont->Hdrs.pic_coding_type;
          }
        } else {
          asic_status &= ~AVS_DEC_X170_IRQ_STREAM_ERROR;
          asic_status &= ~AVS_DEC_X170_IRQ_TIMEOUT;
          asic_status |= AVS_DEC_X170_IRQ_DEC_RDY;
          error_concealment = HANTRO_FALSE;
        }
      } else if(asic_status & AVS_DEC_X170_IRQ_BUFFER_EMPTY) {
        AvsDecPreparePicReturn(dec_cont);
        ret = AVSDEC_BUF_EMPTY;

      }
      /* HW finished decoding a picture */
      else if(asic_status & AVS_DEC_X170_IRQ_DEC_RDY) {
      } else {
        ASSERT(0);
      }

      /* HW finished decoding a picture */
      if(asic_status & AVS_DEC_X170_IRQ_DEC_RDY) {
        if(dec_cont->Hdrs.picture_structure == FRAMEPICTURE ||
            !dec_cont->ApiStorage.first_field) {
          field_rdy = 0;
          dec_cont->StrmStorage.frame_number++;

          AvsHandleFrameEnd(dec_cont);

          AvsDecBufferPicture(dec_cont,
                              input->pic_id,
                              dec_cont->Hdrs.
                              pic_coding_type == BFRAME,
                              dec_cont->Hdrs.
                              pic_coding_type == PFRAME,
                              AVSDEC_PIC_DECODED, 0);

          ret = AVSDEC_PIC_DECODED;

          dec_cont->ApiStorage.first_field = 1;
          if(dec_cont->Hdrs.pic_coding_type != BFRAME) {
            dec_cont->StrmStorage.work1 =
              dec_cont->StrmStorage.work0;
            dec_cont->StrmStorage.work0 =
              dec_cont->StrmStorage.work_out;
            if( dec_cont->StrmStorage.skip_b )
              dec_cont->StrmStorage.skip_b--;
          }
          dec_cont->StrmStorage.prev_pic_coding_type =
            dec_cont->Hdrs.pic_coding_type;
          if( dec_cont->Hdrs.pic_coding_type != BFRAME )
            dec_cont->StrmStorage.prev_pic_structure =
              dec_cont->Hdrs.picture_structure;

          if( dec_cont->Hdrs.pic_coding_type == IFRAME )
            dec_cont->StrmStorage.picture_broken = 0;
        } else {
          field_rdy = 1;
          AvsHandleFrameEnd(dec_cont);
          dec_cont->ApiStorage.first_field = 0;
          if((u32) (dec_cont->StrmDesc.strm_curr_pos -
                    dec_cont->StrmDesc.p_strm_buff_start) >=
              input->data_len) {
            ret = AVSDEC_BUF_EMPTY;
          }
        }
        dec_cont->StrmStorage.valid_pic_header = HANTRO_FALSE;

        /* handle first field indication */
        if(!dec_cont->Hdrs.progressive_sequence) {
          if(dec_cont->Hdrs.picture_structure != FRAMEPICTURE)
            dec_cont->StrmStorage.field_index++;
          else
            dec_cont->StrmStorage.field_index = 1;
        }

        AvsDecPreparePicReturn(dec_cont);
      }

      if(ret != AVSDEC_STRM_PROCESSED && ret != AVSDEC_BUF_EMPTY && !field_rdy) {
        API_STOR.DecStat = STREAMDECODING;
      }
    }
  } while(ret == 0);

  if( error_concealment && dec_cont->Hdrs.pic_coding_type != BFRAME ) {
    dec_cont->StrmStorage.picture_broken = 1;
  }

  AVS_API_TRC("AvsDecDecode: Exit\n");
  output->strm_curr_pos = dec_cont->StrmDesc.strm_curr_pos;
  output->strm_curr_bus_address = input->stream_bus_address +
                                  (dec_cont->StrmDesc.strm_curr_pos - dec_cont->StrmDesc.p_strm_buff_start);
  output->data_left = dec_cont->StrmDesc.strm_buff_size -
                      (output->strm_curr_pos - DEC_STRM.p_strm_buff_start);

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp_instance == NULL) {
    u32 tmpret;
    AvsDecPicture output;
    if(ret == AVSDEC_PIC_DECODED) {
      do {
        tmpret = AvsDecNextPicture_INTERNAL(dec_cont, &output, 0);
        if(tmpret == AVSDEC_ABORTED)
          return (AVSDEC_ABORTED);
      } while( tmpret == AVSDEC_PIC_RDY);
    }
  }

  if(dec_cont->abort)
    return(AVSDEC_ABORTED);
  else
#endif
    return ((AvsDecRet) ret);

#undef API_STOR
#undef DEC_STRM

}

/*------------------------------------------------------------------------------

    Function: AvsDecRelease()

        Functional description:
            Release the decoder instance.

        Inputs:
            dec_inst     Decoder instance

        Outputs:
            none

        Returns:
            none

------------------------------------------------------------------------------*/

void AvsDecRelease(AvsDecInst dec_inst) {
  DecContainer *dec_cont = NULL;
#ifndef USE_EXTERNAL_BUFFER
  const void *dwl;
#endif

  AVS_API_TRC("AvsDecRelease#");
  if(dec_inst == NULL) {
    AVS_API_TRC("AvsDecRelease# ERROR: dec_inst == NULL");
    return;
  }

  dec_cont = ((DecContainer *) dec_inst);
#ifndef USE_EXTERNAL_BUFFER
  dwl = dec_cont->dwl;
#endif

  pthread_mutex_destroy(&dec_cont->protect_mutex);

  if(dec_cont->asic_running) {
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1, 0);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);
  }

#ifdef USE_OUTPUT_RELEASE
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
#endif

  AvsFreeBuffers(dec_cont);

  DWLfree(dec_cont);

#ifndef USE_EXTERNAL_BUFFER
  (void) DWLRelease(dwl);
#endif

  AVS_API_TRC("AvsDecRelease: OK");
}

/*------------------------------------------------------------------------------

    Function: avsRegisterPP()

        Functional description:
            Register the pp for avs pipeline

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

i32 avsRegisterPP(const void *dec_inst, const void *pp_inst,
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

    Function: avsUnregisterPP()

        Functional description:
            Unregister the pp from avs pipeline

        Inputs:
            dec_inst     Decoder instance
            const void  *pp_inst - post-processor instance

        Outputs:
            none

        Returns:
            i32 - return 0 for success or a negative error code

------------------------------------------------------------------------------*/

i32 avsUnregisterPP(const void *dec_inst, const void *pp_inst) {
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
    Function name   : AvsRefreshRegs
    Description     :
    Return type     : void
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void AvsRefreshRegs(DecContainer * dec_cont) {
  i32 i;
  u32 *pp_regs = dec_cont->avs_regs;
  u32 offset;

  for(i = 0; i < DEC_X170_REGISTERS; i++) {
    pp_regs[i] = DWLReadReg(dec_cont->dwl, dec_cont->core_id, 4 * i);
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs = dec_cont->avs_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *pp_regs = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    pp_regs++;
    offset += 4;
  }
#endif
  offset = PP_START_UNIFIED_REGS * 0x04;
  pp_regs =  dec_cont->avs_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    *pp_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
}

/*------------------------------------------------------------------------------
    Function name   : AvsFlushRegs
    Description     :
    Return type     : void
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void AvsFlushRegs(DecContainer * dec_cont) {
  i32 i;
  u32 *pp_regs = dec_cont->avs_regs;
  u32 offset;

  for(i = 2; i < DEC_X170_REGISTERS; i++) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * i, pp_regs[i]);
    pp_regs[i] = 0;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs = dec_cont->avs_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *pp_regs);
    *pp_regs = 0;
    pp_regs++;
    offset += 4;
  }
#endif
  offset = PP_START_UNIFIED_REGS * 0x04;
  pp_regs =  dec_cont->avs_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *pp_regs);
    pp_regs++;
    offset += 4;
  }
}

/*------------------------------------------------------------------------------
    Function name   : AvsHandleVlcModeError
    Description     :
    Return type     : u32
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
u32 AvsHandleVlcModeError(DecContainer * dec_cont, u32 pic_num) {
  u32 ret = 0, tmp;

  ASSERT(dec_cont->StrmStorage.strm_dec_ready);

  tmp = AvsStrmDec_NextStartCode(dec_cont);
  if(tmp != END_OF_STREAM) {
    dec_cont->StrmDesc.strm_curr_pos -= 4;
    dec_cont->StrmDesc.strm_buff_read_bits -= 32;
  }

  /* error in first picture -> set reference to grey */
  if(!dec_cont->StrmStorage.frame_number) {
    (void) DWLmemset(dec_cont->StrmStorage.
                     p_pic_buf[(i32)dec_cont->StrmStorage.work_out].data.
                     virtual_address, 128,
                     384 * dec_cont->StrmStorage.total_mbs_in_frame);

    AvsDecPreparePicReturn(dec_cont);

    /* no pictures finished -> return STRM_PROCESSED */
    if(tmp == END_OF_STREAM)
      ret = AVSDEC_STRM_PROCESSED;
    else
      ret = 0;
    dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work_out;
    dec_cont->StrmStorage.skip_b = 2;
  } else {
    if (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_FULLMODE)
      dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_SEMIMODE;

    if(dec_cont->Hdrs.pic_coding_type != BFRAME) {
      dec_cont->StrmStorage.frame_number++;

      /* reset send_to_pp to prevent post-processing partly decoded
       * pictures */
      if(dec_cont->StrmStorage.work_out != dec_cont->StrmStorage.work0)
        dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work_out].
        send_to_pp = 0;

      BqueueDiscard( &dec_cont->StrmStorage.bq,
                     dec_cont->StrmStorage.work_out );
      dec_cont->StrmStorage.work_out = dec_cont->StrmStorage.work0;

      AvsDecBufferPicture(dec_cont,
                          pic_num,
                          dec_cont->Hdrs.pic_coding_type == BFRAME,
                          1, (AvsDecRet) FREEZED_PIC_RDY,
                          dec_cont->StrmStorage.total_mbs_in_frame);

      ret = AVSDEC_PIC_DECODED;

      dec_cont->StrmStorage.work1 = dec_cont->StrmStorage.work0;
      dec_cont->StrmStorage.skip_b = 2;
    } else {
      if(dec_cont->StrmStorage.intra_freeze) {
        dec_cont->StrmStorage.frame_number++;

        AvsDecBufferPicture(dec_cont,
                            pic_num,
                            dec_cont->Hdrs.pic_coding_type == BFRAME,
                            1, (AvsDecRet) FREEZED_PIC_RDY,
                            dec_cont->StrmStorage.total_mbs_in_frame);

        ret = AVSDEC_PIC_DECODED;

      }

      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work_out].
      send_to_pp = 0;
    }
  }

  dec_cont->ApiStorage.first_field = 1;

  dec_cont->ApiStorage.DecStat = STREAMDECODING;
  dec_cont->StrmStorage.valid_pic_header = HANTRO_FALSE;
  dec_cont->Hdrs.picture_structure = FRAMEPICTURE;

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : AvsHandleFrameEnd
    Description     :
    Return type     : u32
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void AvsHandleFrameEnd(DecContainer * dec_cont) {

  u32 tmp;

  dec_cont->StrmDesc.strm_buff_read_bits =
    8 * (dec_cont->StrmDesc.strm_curr_pos -
         dec_cont->StrmDesc.p_strm_buff_start);
  dec_cont->StrmDesc.bit_pos_in_word = 0;

  do {
    tmp = AvsStrmDec_ShowBits(dec_cont, 32);
    if((tmp >> 8) == 0x1)
      break;
  } while(AvsStrmDec_FlushBits(dec_cont, 8) == HANTRO_OK);

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
         p_pic_buf[(i32)dec_container->StrmStorage.work_out].data.bus_address != 0);
  ASSERT(strm_bus_address != 0);

  /* set pp luma bus */
  dec_container->pp_control.input_bus_luma = 0;

  /* Save frame/Hdr info for current picture. */
  dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].frame_width
    = dec_container->StrmStorage.frame_width;
  dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].frame_height
    = dec_container->StrmStorage.frame_height;
  dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].Hdrs
    = dec_container->Hdrs;

  if(!dec_container->asic_running) {
    tmp = AvsSetRegs(dec_container, strm_bus_address);
    if(tmp == HANTRO_NOK)
      return 0;

    if (!dec_container->keep_hw_reserved)
      (void) DWLReserveHw(dec_container->dwl, &dec_container->core_id);

    SetDecRegister(dec_container->avs_regs, HWIF_DEC_OUT_DIS, 0);

    /* Start PP */
    if(dec_container->pp_instance != NULL) {
      AvsPpControl(dec_container, 0);
    }

    dec_container->asic_running = 1;

    DWLWriteReg(dec_container->dwl, dec_container->core_id, 0x4, 0);

    AvsFlushRegs(dec_container);

    /* Enable HW */
    SetDecRegister(dec_container->avs_regs, HWIF_DEC_E, 1);
    DWLEnableHw(dec_container->dwl, dec_container->core_id,
                4 * 1, dec_container->avs_regs[1]);
  } else { /* in the middle of decoding, continue decoding */
    /* tmp is strm_bus_address + number of bytes decoded by SW */
    tmp = dec_container->StrmDesc.strm_curr_pos -
          dec_container->StrmDesc.p_strm_buff_start;
    tmp = strm_bus_address + tmp;

    /* pointer to start of the stream, mask to get the pointer to
     * previous 64-bit aligned position */
    if(!(tmp & ~0x7)) {
      return 0;
    }

    SET_ADDR_REG(dec_container->avs_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);
    /* amount of stream (as seen by the HW), obtained as amount of stream
     * given by the application subtracted by number of bytes decoded by
     * SW (if strm_bus_address is not 64-bit aligned -> adds number of bytes
     * from previous 64-bit aligned boundary) */
    SetDecRegister(dec_container->avs_regs, HWIF_STREAM_LEN,
                   dec_container->StrmDesc.strm_buff_size -
                   ((tmp & ~0x7) - strm_bus_address));
    SetDecRegister(dec_container->avs_regs, HWIF_STRM_START_BIT,
                   dec_container->StrmDesc.bit_pos_in_word + 8 * (tmp & 0x7));

    /* This depends on actual register allocation */
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 5,
                dec_container->avs_regs[5]);
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 6,
                dec_container->avs_regs[6]);
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 12,
                dec_container->avs_regs[12]);
#ifdef USE_64BIT_ENV
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 122,
                dec_container->avs_regs[122]);
#endif

    DWLEnableHw(dec_container->dwl, dec_container->core_id,
                4 * 1, dec_container->avs_regs[1]);
  }

  /* Wait for HW ready */
  ret = DWLWaitHwReady(dec_container->dwl, dec_container->core_id,
                       (u32) DEC_X170_TIMEOUT_LENGTH);

  AvsRefreshRegs(dec_container);

  if(ret == DWL_HW_WAIT_OK) {
    asic_status =
      GetDecRegister(dec_container->avs_regs, HWIF_DEC_IRQ_STAT);
  } else if(ret == DWL_HW_WAIT_TIMEOUT) {
    asic_status = ID8170_DEC_TIMEOUT;
  } else {
    asic_status = ID8170_DEC_SYSTEM_ERROR;
  }

  if(!(asic_status & AVS_DEC_X170_IRQ_BUFFER_EMPTY) ||
      (asic_status & AVS_DEC_X170_IRQ_STREAM_ERROR) ||
      (asic_status & AVS_DEC_X170_IRQ_BUS_ERROR) ||
      (asic_status == ID8170_DEC_TIMEOUT) ||
      (asic_status == ID8170_DEC_SYSTEM_ERROR)) {
    /* reset HW */
    SetDecRegister(dec_container->avs_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_DEC_E, 0);

    DWLDisableHw(dec_container->dwl, dec_container->core_id, 4 * 1,
                 dec_container->avs_regs[1]);

    dec_container->keep_hw_reserved = 0;

    /* End PP co-operation */
    if(dec_container->pp_instance != NULL) {
      if(dec_container->Hdrs.picture_structure != FRAMEPICTURE) {
        if(dec_container->pp_control.pp_status == DECPP_RUNNING ||
            dec_container->pp_control.pp_status == DECPP_PIC_NOT_FINISHED) {
          if(dec_container->ApiStorage.first_field &&
              (asic_status & AVS_DEC_X170_IRQ_DEC_RDY)) {
            dec_container->pp_control.pp_status =
              DECPP_PIC_NOT_FINISHED;
            dec_container->keep_hw_reserved = 1;
          } else {
            dec_container->pp_control.pp_status = DECPP_PIC_READY;
            dec_container->PPEndCallback(dec_container->pp_instance);
          }
        }
      } else {
        /* End PP co-operation */
        if(dec_container->pp_control.pp_status == DECPP_RUNNING) {
          dec_container->PPEndCallback(dec_container->pp_instance);

          if((asic_status & AVS_DEC_X170_IRQ_STREAM_ERROR) &&
              dec_container->pp_control.use_pipeline)
            dec_container->pp_control.pp_status = DECPP_IDLE;
          else
            dec_container->pp_control.pp_status = DECPP_PIC_READY;
        }
      }
    }

    dec_container->asic_running = 0;
    if (!dec_container->keep_hw_reserved)
      (void) DWLReleaseHw(dec_container->dwl, dec_container->core_id);
  }

  /* if HW interrupt indicated either BUFFER_EMPTY or
   * DEC_RDY -> read stream end pointer and update StrmDesc structure */
  if((asic_status &
      (AVS_DEC_X170_IRQ_BUFFER_EMPTY | AVS_DEC_X170_IRQ_DEC_RDY))) {
    tmp = GET_ADDR_REG(dec_container->avs_regs, HWIF_RLC_VLC_BASE);

    if(((tmp - strm_bus_address) <= DEC_X170_MAX_STREAM) &&
        ((tmp - strm_bus_address) <= dec_container->StrmDesc.strm_buff_size)) {
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

  if( dec_container->Hdrs.pic_coding_type != BFRAME &&
      dec_container->ref_buf_support &&
      (asic_status & AVS_DEC_X170_IRQ_DEC_RDY) &&
      dec_container->asic_running == 0) {
    RefbuMvStatistics( &dec_container->ref_buffer_ctrl,
                       dec_container->avs_regs,
                       NULL,
                       HANTRO_FALSE,
                       dec_container->Hdrs.pic_coding_type == IFRAME );
  }

  SetDecRegister(dec_container->avs_regs, HWIF_DEC_IRQ_STAT, 0);

  return asic_status;

}

/*------------------------------------------------------------------------------

    Function name: AvsDecNextPicture

    Functional description:
        Retrieve next decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct
        end_of_stream Indicates whether end of stream has been reached

    Output:
        picture Decoder output picture.

    Return values:
        AVSDEC_OK         No picture available.
        AVSDEC_PIC_RDY    Picture ready.

------------------------------------------------------------------------------*/
AvsDecRet AvsDecNextPicture(AvsDecInst dec_inst,
                            AvsDecPicture * picture, u32 end_of_stream) {
  /* Variables */
  AvsDecRet return_value = AVSDEC_PIC_RDY;
  DecContainer *dec_cont;
  picture_t *p_pic;
  u32 pic_index = AVS_BUFFER_UNDEFINED;
  u32 min_count;
  u32 tmp = 0;
  i32 ret;

  /* Code */
  AVS_API_TRC("\nAvs_dec_next_picture#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    AVS_API_TRC("AvsDecNextPicture# ERROR: picture is NULL");
    return (AVSDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecNextPicture# ERROR: Decoder not initialized");
    return (AVSDEC_NOT_INITIALIZED);
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
      if (ret == FIFO_EMPTY) return AVSDEC_OK;
#endif

      if ((i32)i == -1) {
        AVS_API_TRC("AvsDecNextPicture# AVSDEC_END_OF_STREAM\n");
        return AVSDEC_END_OF_STREAM;
      }
      if ((i32)i == -2) {
        AVS_API_TRC("AvsDecNextPicture# AVSDEC_FLUSHED\n");
        return AVSDEC_FLUSHED;
      }

      *picture = dec_cont->StrmStorage.picture_info[i];

      AVS_API_TRC("AvsDecNextPicture# AVSDEC_PIC_RDY\n");
      return (AVSDEC_PIC_RDY);
    } else
      return AVSDEC_ABORTED;
  }
#endif
  min_count = 0;
  if(dec_cont->StrmStorage.sequence_low_delay == 0 && !end_of_stream &&
      !dec_cont->StrmStorage.new_headers_change_resolution)
    min_count = 1;

  /* this is to prevent post-processing of non-finished pictures in the
   * end of the stream */
  if(end_of_stream && dec_cont->Hdrs.pic_coding_type == BFRAME) {
    dec_cont->Hdrs.pic_coding_type = PFRAME;
    dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work_out].send_to_pp =
      0;
  }

  /* Nothing to send out */
  if(dec_cont->StrmStorage.out_count <= min_count) {
    (void) DWLmemset(picture, 0, sizeof(AvsDecPicture));
    picture->output_picture = NULL;
    picture->interlaced = !dec_cont->Hdrs.progressive_sequence;
    return_value = AVSDEC_OK;
  } else {
    pic_index = dec_cont->StrmStorage.out_index;
    pic_index = dec_cont->StrmStorage.out_buf[pic_index];

    AvsFillPicStruct(picture, dec_cont, pic_index);

    /* field output */
    if(AVSDEC_IS_FIELD_OUTPUT) {
      picture->interlaced = 1;
      picture->field_picture = 1;

      if(!dec_cont->ApiStorage.output_other_field) {
        picture->top_field =
          dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 1 : 0;
        dec_cont->ApiStorage.output_other_field = 1;
      } else {
        picture->top_field =
          dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 0 : 1;
        dec_cont->ApiStorage.output_other_field = 0;
        dec_cont->StrmStorage.out_count--;
        dec_cont->StrmStorage.out_index++;
        dec_cont->StrmStorage.out_index &= 15;
      }
    } else {
      /* progressive or deinterlaced frame output */
      picture->interlaced = !dec_cont->Hdrs.progressive_sequence;
      picture->top_field = 0;
      picture->field_picture = 0;
      dec_cont->StrmStorage.out_count--;
      dec_cont->StrmStorage.out_index++;
      dec_cont->StrmStorage.out_index &= 15;
    }
  }

  if(dec_cont->pp_instance &&
      (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_FULLMODE) &&
      end_of_stream && (return_value == AVSDEC_PIC_RDY)) {
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
  if(dec_cont->pp_instance &&
      (dec_cont->pp_control.multi_buf_stat != MULTIBUFFER_FULLMODE)) {
    /* pp and decoder running in parallel, decoder finished first field ->
     * decode second field and wait PP after that */
    if(dec_cont->pp_instance != NULL &&
        dec_cont->pp_control.pp_status == DECPP_PIC_NOT_FINISHED) {
      return (AVSDEC_OK);
    }

    if(dec_cont->pp_control.pp_status == DECPP_PIC_READY) {
      pic_index = dec_cont->ApiStorage.pp_pic_index;
      if(AVSDEC_IS_FIELD_OUTPUT) {
        if (dec_cont->ApiStorage.buffer_for_pp != NO_BUFFER)
          pic_index = dec_cont->ApiStorage.buffer_for_pp - 1;
        picture->interlaced = 1;
        picture->field_picture = 1;
        picture->top_field =
          dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 1 : 0;
      }
      AvsFillPicStruct(picture, dec_cont, pic_index);
      return_value = AVSDEC_PIC_RDY;
      dec_cont->pp_control.pp_status = DECPP_IDLE;
    } else {
      p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;
      return_value = AVSDEC_OK;
      pic_index = AVS_BUFFER_UNDEFINED;

      if(AVSDEC_NON_PIPELINE_AND_B_PICTURE) {
        /* send current B Picture output to PP */
        pic_index = dec_cont->StrmStorage.work_out;
        dec_cont->Hdrs.pic_coding_type = IFRAME;

        /* Set here field decoding for first field of a B picture */
        if(AVSDEC_IS_FIELD_OUTPUT) {
          dec_cont->ApiStorage.buffer_for_pp = pic_index+1;
          picture->interlaced = 1;
          picture->field_picture = 1;
          picture->top_field =
            dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 1 : 0;
          dec_cont->pp_control.pic_struct =
            dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ?
            DECPP_PIC_TOP_FIELD_FRAME : DECPP_PIC_BOT_FIELD_FRAME;
        }
      } else if(AVSDEC_IS_FIELD_OUTPUT &&
                (dec_cont->ApiStorage.buffer_for_pp != NO_BUFFER)) {
        picture->interlaced = 1;
        picture->field_picture = 1;
        pic_index = (dec_cont->ApiStorage.buffer_for_pp-1);
        dec_cont->pp_control.pic_struct =
          dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ?
          DECPP_PIC_BOT_FIELD_FRAME : DECPP_PIC_TOP_FIELD_FRAME;
        picture->top_field =
          dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 0 : 1;
        dec_cont->ApiStorage.buffer_for_pp = NO_BUFFER;
        p_pic[pic_index].send_to_pp = 1;
      } else if(end_of_stream) {
        pic_index = 0;
        while((pic_index < dec_cont->StrmStorage.num_buffers) && !p_pic[pic_index].send_to_pp)
          pic_index++;

        if (pic_index == dec_cont->StrmStorage.num_buffers)
          return AVSDEC_OK;

        if(AVSDEC_IS_FIELD_OUTPUT) {
          /* if field output, other field must be processed also */
          dec_cont->ApiStorage.buffer_for_pp = pic_index+1;

          /* set field processing */
          dec_cont->pp_control.pic_struct =
            dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ?
            DECPP_PIC_TOP_FIELD_FRAME : DECPP_PIC_BOT_FIELD_FRAME;

          picture->top_field =
            dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 1 : 0;
          picture->field_picture = 1;
        } else if(dec_cont->pp_config_query.deinterlace) {
          AvsDecSetupDeinterlace(dec_cont);
        }
      }

      if(pic_index != AVS_BUFFER_UNDEFINED) {
        if(p_pic[pic_index].send_to_pp && p_pic[pic_index].send_to_pp < 3) {
          /* forward tiled mode */
          dec_cont->pp_control.tiled_input_mode =
            p_pic[pic_index].tiled_mode;
          dec_cont->pp_control.progressive_sequence =
            dec_cont->Hdrs.progressive_sequence;

          /* Set up pp */
          if(dec_cont->pp_control.pic_struct ==
              DECPP_PIC_BOT_FIELD_FRAME) {
            dec_cont->pp_control.input_bus_luma = 0;
            dec_cont->pp_control.input_bus_chroma = 0;

            if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
              dec_cont->pp_control.bottom_bus_luma =
                dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].data.bus_address +
                (dec_cont->StrmStorage.frame_width << 4);

              dec_cont->pp_control.bottom_bus_chroma =
                dec_cont->pp_control.bottom_bus_luma +
                ((dec_cont->StrmStorage.frame_width *
                  dec_cont->StrmStorage.frame_height) << 8);
            } else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
              u32 frame_size = (dec_cont->StrmStorage.frame_width *
                                dec_cont->StrmStorage.
                                frame_height) << 8;
              dec_cont->pp_control.bottom_bus_luma =
                dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].
                data.bus_address + frame_size/2;

              dec_cont->pp_control.bottom_bus_chroma =
                dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].
                data.bus_address + frame_size + frame_size/4;
            }

            dec_cont->pp_control.inwidth =
              dec_cont->pp_control.cropped_w =
                dec_cont->StrmStorage.frame_width << 4;
            dec_cont->pp_control.inheight =
              (((dec_cont->StrmStorage.frame_height +
                 1) & ~1) / 2) << 4;
            dec_cont->pp_control.cropped_h =
              (dec_cont->StrmStorage.frame_height << 4) / 2;
          } else {
            dec_cont->pp_control.input_bus_luma =
              dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].data.
              bus_address;
            dec_cont->pp_control.input_bus_chroma =
              dec_cont->pp_control.input_bus_luma +
              ((dec_cont->StrmStorage.frame_width *
                dec_cont->StrmStorage.frame_height) << 8);
            if(dec_cont->pp_control.pic_struct ==
                DECPP_PIC_TOP_FIELD_FRAME) {
              dec_cont->pp_control.bottom_bus_luma = 0;
              dec_cont->pp_control.bottom_bus_chroma = 0;
              dec_cont->pp_control.inwidth =
                dec_cont->pp_control.cropped_w =
                  dec_cont->StrmStorage.frame_width << 4;
              dec_cont->pp_control.inheight =
                (((dec_cont->StrmStorage.frame_height +
                   1) & ~1) / 2) << 4;
              dec_cont->pp_control.cropped_h =
                (dec_cont->StrmStorage.frame_height << 4) / 2;
            } else {
              dec_cont->pp_control.inwidth =
                dec_cont->pp_control.cropped_w =
                  dec_cont->StrmStorage.frame_width << 4;
              dec_cont->pp_control.inheight =
                dec_cont->pp_control.cropped_h =
                  dec_cont->StrmStorage.frame_height << 4;
              if(dec_cont->pp_config_query.deinterlace) {
                AvsDecSetupDeinterlace(dec_cont);
              }

            }
          }

          if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD &&
              !dec_cont->pp_config_query.deinterlace ) {
            dec_cont->pp_control.pic_struct = (picture->top_field) ?
                                              DECPP_PIC_FRAME_OR_TOP_FIELD : DECPP_PIC_BOT_FIELD;
          }

          dec_cont->pp_control.use_pipeline = 0;
          {
            u32 value = GetDecRegister(dec_cont->avs_regs,
                                       HWIF_DEC_OUT_ENDIAN);

            dec_cont->pp_control.little_endian =
              (value == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
          }
          dec_cont->pp_control.word_swap =
            GetDecRegister(dec_cont->avs_regs,
                           HWIF_DEC_OUTSWAP32_E) ? 1 : 0;

          /* Run pp */
          dec_cont->PPRun(dec_cont->pp_instance, &dec_cont->pp_control);
          dec_cont->pp_control.pp_status = DECPP_RUNNING;

          if(dec_cont->pp_control.pic_struct ==
              DECPP_PIC_FRAME_OR_TOP_FIELD ||
              dec_cont->pp_control.pic_struct ==
              DECPP_PIC_TOP_AND_BOT_FIELD_FRAME ||
              dec_cont->pp_control.pic_struct ==
              DECPP_PIC_TOP_AND_BOT_FIELD) {
            /* tmp set if freezed pic and has will be used as
             * output another time */
            if(!tmp)
              dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].send_to_pp =
                0;
          } else {
            dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].send_to_pp--;
          }

          /* Wait for result */
          dec_cont->PPEndCallback(dec_cont->pp_instance);

          AvsFillPicStruct(picture, dec_cont, pic_index);
          return_value = AVSDEC_PIC_RDY;
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

    Function name: AvsDecNextPicture_INTERNAL

    Functional description:
        Push next picture in display order into output fifo if any available.

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct
        end_of_stream Indicates whether end of stream has been reached

    Output:
        picture Decoder output picture.

    Return values:
        AVSDEC_OK               No picture available.
        AVSDEC_PIC_RDY          Picture ready.
        AVSDEC_PARAM_ERROR      invalid parameters
        AVSDEC_NOT_INITIALIZED  decoder instance not initialized yet

------------------------------------------------------------------------------*/
AvsDecRet AvsDecNextPicture_INTERNAL(AvsDecInst dec_inst,
                                     AvsDecPicture * picture, u32 end_of_stream) {
  /* Variables */
  AvsDecRet return_value = AVSDEC_PIC_RDY;
  DecContainer *dec_cont;
  u32 pic_index = AVS_BUFFER_UNDEFINED;
  u32 min_count;

  /* Code */
  AVS_API_TRC("\nAvs_dec_next_picture_INTERNAL#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    AVS_API_TRC("AvsDecNextPicture_INTERNAL# ERROR: picture is NULL");
    return (AVSDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecNextPicture_INTERNAL# ERROR: Decoder not initialized");
    return (AVSDEC_NOT_INITIALIZED);
  }

  min_count = 0;
  if(dec_cont->StrmStorage.sequence_low_delay == 0 && !end_of_stream &&
      !dec_cont->StrmStorage.new_headers_change_resolution)
    min_count = 1;

  /* this is to prevent post-processing of non-finished pictures in the
   * end of the stream */
  if(end_of_stream && dec_cont->Hdrs.pic_coding_type == BFRAME) {
    dec_cont->Hdrs.pic_coding_type = PFRAME;
    dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work_out].send_to_pp =
      0;
  }

  /* Nothing to send out */
  if(dec_cont->StrmStorage.out_count <= min_count) {
    (void) DWLmemset(picture, 0, sizeof(AvsDecPicture));
    picture->output_picture = NULL;
    picture->interlaced = !dec_cont->Hdrs.progressive_sequence;
    return_value = AVSDEC_OK;
  } else {
    pic_index = dec_cont->StrmStorage.out_index;
    pic_index = dec_cont->StrmStorage.out_buf[pic_index];

    AvsFillPicStruct(picture, dec_cont, pic_index);

    /* field output */
    //if(AVSDEC_IS_FIELD_OUTPUT)
    if(!dec_cont->StrmStorage.p_pic_buf[pic_index].Hdrs.progressive_sequence &&
        !dec_cont->pp_config_query.deinterlace) {
      picture->interlaced = 1;
      picture->field_picture = 1;

      if(!dec_cont->ApiStorage.output_other_field) {
        picture->top_field =
          dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 1 : 0;
        dec_cont->ApiStorage.output_other_field = 1;
      } else {
        picture->top_field =
          dec_cont->StrmStorage.p_pic_buf[(i32)pic_index].tf ? 0 : 1;
        dec_cont->ApiStorage.output_other_field = 0;
        dec_cont->StrmStorage.out_count--;
        dec_cont->StrmStorage.out_index++;
        dec_cont->StrmStorage.out_index &= 15;
      }
    } else {
      /* progressive or deinterlaced frame output */
      picture->interlaced = !dec_cont->StrmStorage.p_pic_buf[pic_index].Hdrs.progressive_sequence;
      picture->top_field = 0;
      picture->field_picture = 0;
      dec_cont->StrmStorage.out_count--;
      dec_cont->StrmStorage.out_index++;
      dec_cont->StrmStorage.out_index &= 15;
    }

#ifdef USE_PICTURE_DISCARD
    if (dec_cont->StrmStorage.p_pic_buf[pic_index].first_show)
#endif
    {
#ifndef USE_PICTURE_DISCARD
      /* wait this buffer as unused */
      if(BqueueWaitBufNotInUse(&dec_cont->StrmStorage.bq, pic_index) != HANTRO_OK)
        return AVSDEC_ABORTED;
      if(dec_cont->pp_enabled) {
        InputQueueWaitBufNotUsed(dec_cont->pp_buffer_queue,dec_cont->StrmStorage.p_pic_buf[pic_index].pp_data->virtual_address);
      }
#endif

      //dec_cont->StrmStorage.p_pic_buf[pic_index].notDisplayed = 1;

      /* set this buffer as used */
      if((!dec_cont->ApiStorage.output_other_field &&
          picture->interlaced) || !picture->interlaced) {
        BqueueSetBufferAsUsed(&dec_cont->StrmStorage.bq, pic_index);
        dec_cont->StrmStorage.p_pic_buf[pic_index].first_show = 0;
        if(dec_cont->pp_enabled)
          InputQueueSetBufAsUsed(dec_cont->pp_buffer_queue,dec_cont->StrmStorage.p_pic_buf[pic_index].pp_data->virtual_address);
      }

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

    Function name: AvsDecPictureConsumed

    Functional description:
        release specific decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to  picture struct


    Return values:
        AVSDEC_PARAM_ERROR         Decoder instance or picture is null
        AVSDEC_NOT_INITIALIZED     Decoder instance isn't initialized
        AVSDEC_OK                  picture release success
------------------------------------------------------------------------------*/
AvsDecRet AvsDecPictureConsumed(AvsDecInst dec_inst, AvsDecPicture * picture) {
  /* Variables */
  DecContainer *dec_cont;
  u32 i;

  /* Code */
  AVS_API_TRC("\nAvs_dec_picture_consumed#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    AVS_API_TRC("AvsDecPictureConsumed# ERROR: picture is NULL");
    return (AVSDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecPictureConsumed# ERROR: Decoder not initialized");
    return (AVSDEC_NOT_INITIALIZED);
  }

  if (!dec_cont->pp_enabled) {
    for(i = 0; i < dec_cont->StrmStorage.num_buffers; i++) {
      if(picture->output_picture_bus_address == dec_cont->StrmStorage.p_pic_buf[i].data.bus_address
          && (addr_t)picture->output_picture
          == (addr_t)dec_cont->StrmStorage.p_pic_buf[i].data.virtual_address) {
        if(dec_cont->pp_instance == NULL) {
          BqueuePictureRelease(&dec_cont->StrmStorage.bq, i);
        }
        return (AVSDEC_OK);
      }
    }
  } else {
    InputQueueReturnBuffer(dec_cont->pp_buffer_queue,(u32 *)picture->output_picture);
    return (AVSDEC_OK);
  }
  return (AVSDEC_PARAM_ERROR);
}

AvsDecRet AvsDecEndOfStream(AvsDecInst dec_inst, u32 strm_end_flag) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;
  AvsDecPicture output;
  AvsDecRet ret;

  AVS_API_TRC("AvsDecEndOfStream#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecPictureConsumed# ERROR: Decoder not initialized");
    return (AVSDEC_NOT_INITIALIZED);
  }
  pthread_mutex_lock(&dec_cont->protect_mutex);
  if(dec_cont->dec_stat == AVSDEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (AVSDEC_OK);
  }

  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->avs_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->avs_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->avs_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->avs_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  } else if (dec_cont->keep_hw_reserved) {
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->keep_hw_reserved = 0;
  }

  while((ret = AvsDecNextPicture_INTERNAL(dec_inst, &output, 1)) == AVSDEC_PIC_RDY);
  if(ret == AVSDEC_ABORTED) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (AVSDEC_ABORTED);
  }

  if(strm_end_flag) {
    dec_cont->dec_stat = AVSDEC_END_OF_STREAM;
    FifoPush(dec_cont->fifo_display, -1, FIFO_EXCEPTION_DISABLE);
  }

  /* Wait all buffers as unused */
  //if(dec_cont->pp_instance == NULL && !strm_end_flag)
  //  BqueueWaitNotInUse(&dec_cont->StrmStorage.bq);

  dec_cont->StrmStorage.work0 =
    dec_cont->StrmStorage.work1 = INVALID_ANCHOR_PICTURE;

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  AVS_API_TRC("AvsDecEndOfStream# AVSDEC_OK\n");
  return (AVSDEC_OK);
}

#endif

/*----------------------=-------------------------------------------------------

    Function name: AvsFillPicStruct

    Functional description:
        Fill data to output pic description

    Input:
        dec_cont    Decoder container
        picture    Pointer to return value struct

    Return values:
        void

------------------------------------------------------------------------------*/
static void AvsFillPicStruct(AvsDecPicture * picture,
                             DecContainer * dec_cont, u32 pic_index) {
  picture_t *p_pic;

  p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;

  if (!dec_cont->pp_enabled) {
    picture->frame_width = p_pic[pic_index].frame_width << 4;
    picture->frame_height = p_pic[pic_index].frame_height << 4;
    picture->coded_width = p_pic[pic_index].Hdrs.horizontal_size;
    picture->coded_height = p_pic[pic_index].Hdrs.vertical_size;
  } else {
    picture->frame_width = (p_pic[pic_index].frame_width << 4) >> dec_cont->dscale_shift_x;
    picture->frame_height = (p_pic[pic_index].frame_height << 4) >> dec_cont->dscale_shift_y;
    picture->coded_width = p_pic[pic_index].Hdrs.horizontal_size >> dec_cont->dscale_shift_x;
    picture->coded_height = p_pic[pic_index].Hdrs.vertical_size >> dec_cont->dscale_shift_y;
  }
  picture->interlaced = !p_pic[pic_index].Hdrs.progressive_sequence;

  if (!dec_cont->pp_enabled) {
    picture->output_picture = (u8 *) p_pic[pic_index].data.virtual_address;
    picture->output_picture_bus_address = p_pic[pic_index].data.bus_address;
  } else {
    picture->output_picture = (u8 *) p_pic[pic_index].pp_data->virtual_address;
    picture->output_picture_bus_address = p_pic[pic_index].pp_data->bus_address;
  }
  picture->key_picture = p_pic[pic_index].pic_type;
  picture->pic_id = p_pic[pic_index].pic_id;
  picture->decode_id = p_pic[pic_index].pic_id;
  picture->pic_coding_type = p_pic[pic_index].pic_code_type;

  /* handle first field indication */
  if(!p_pic[pic_index].Hdrs.progressive_sequence) {
    if(dec_cont->StrmStorage.field_out_index)
      dec_cont->StrmStorage.field_out_index = 0;
    else
      dec_cont->StrmStorage.field_out_index = 1;
  }

  picture->first_field = p_pic[pic_index].ff[dec_cont->StrmStorage.field_out_index];
  picture->repeat_first_field = p_pic[pic_index].rff;
  picture->repeat_frame_count = p_pic[pic_index].rfc;
  picture->number_of_err_mbs = p_pic[pic_index].nbr_err_mbs;
  picture->output_format = p_pic[pic_index].tiled_mode ?
                           DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

  (void) DWLmemcpy(&picture->time_code,
                   &p_pic[pic_index].time_code, sizeof(AvsDecTime));
}

/*------------------------------------------------------------------------------

    Function name: AvsSetRegs

    Functional description:
        Set registers

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static u32 AvsSetRegs(DecContainer * dec_container, addr_t strm_bus_address) {
  addr_t tmp = 0;
  u32 tmp_fwd, tmp_curr;

#ifdef _DEC_PP_USAGE
  AvsDecPpUsagePrint(dec_container, DECPP_UNSPECIFIED,
                     dec_container->StrmStorage.work_out, 1,
                     dec_container->StrmStorage.latest_id);
#endif

  /*
  if(!dec_container->Hdrs.progressive_sequence)
      SetDecRegister(dec_container->avs_regs, HWIF_DEC_OUT_TILED_E, 0);
      */

  if(dec_container->Hdrs.picture_structure == FRAMEPICTURE ||
      dec_container->ApiStorage.first_field) {
    dec_container->StrmStorage.p_pic_buf[(i32)dec_container->StrmStorage.work_out].
    send_to_pp = 1;
  } else {
    dec_container->StrmStorage.p_pic_buf[(i32)dec_container->StrmStorage.work_out].
    send_to_pp = 2;
  }

  AVSDEC_DEBUG(("Decoding to index %d \n",
                dec_container->StrmStorage.work_out));

  if(dec_container->Hdrs.picture_structure == FRAMEPICTURE) {
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_INTERLACE_E, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_FIELDMODE_E, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_TOPFIELD_E, 0);
  } else {
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_INTERLACE_E, 1);
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_FIELDMODE_E, 1);
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_TOPFIELD_E,
                   dec_container->ApiStorage.first_field);
  }

  SetDecRegister(dec_container->avs_regs, HWIF_PIC_MB_HEIGHT_P,
                 dec_container->StrmStorage.frame_height);
  SetDecRegister(dec_container->avs_regs, HWIF_AVS_H264_H_EXT,
                 dec_container->StrmStorage.frame_height >> 8);

  if(dec_container->Hdrs.pic_coding_type == BFRAME)
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_B_E, 1);
  else
    SetDecRegister(dec_container->avs_regs, HWIF_PIC_B_E, 0);

  SetDecRegister(dec_container->avs_regs, HWIF_PIC_INTER_E,
                 dec_container->Hdrs.pic_coding_type != IFRAME);

  /* tmp is strm_bus_address + number of bytes decoded by SW */
  tmp = dec_container->StrmDesc.strm_curr_pos -
        dec_container->StrmDesc.p_strm_buff_start;
  tmp = strm_bus_address + tmp;

  /* bus address must not be zero */
  if(!(tmp & ~0x7)) {
    return 0;
  }

  /* pointer to start of the stream, mask to get the pointer to
   * previous 64-bit aligned position */
  SET_ADDR_REG(dec_container->avs_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);

  /* amount of stream (as seen by the HW), obtained as amount of
   * stream given by the application subtracted by number of bytes
   * decoded by SW (if strm_bus_address is not 64-bit aligned -> adds
   * number of bytes from previous 64-bit aligned boundary) */
  SetDecRegister(dec_container->avs_regs, HWIF_STREAM_LEN,
                 dec_container->StrmDesc.strm_buff_size -
                 ((tmp & ~0x7) - strm_bus_address));

  SetDecRegister(dec_container->avs_regs, HWIF_STRM_START_BIT,
                 dec_container->StrmDesc.bit_pos_in_word + 8 * (tmp & 0x7));

  SetDecRegister(dec_container->avs_regs, HWIF_PIC_FIXED_QUANT,
                 dec_container->Hdrs.fixed_picture_qp);
  SetDecRegister(dec_container->avs_regs, HWIF_INIT_QP,
                 dec_container->Hdrs.picture_qp);

  /* AVS Plus stuff */
  SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_E,
                 dec_container->Hdrs.weighting_quant_flag);
  SetDecRegister(dec_container->avs_regs, HWIF_AVS_AEC_E,
                 dec_container->Hdrs.aec_enable);
  SetDecRegister(dec_container->avs_regs, HWIF_NO_FWD_REF_E,
                 dec_container->Hdrs.no_forward_reference_flag);
  SetDecRegister(dec_container->avs_regs, HWIF_PB_FIELD_ENHANCED_E,
                 dec_container->Hdrs.pb_field_enhanced_flag);

  if (dec_container->Hdrs.profile_id == 0x48) {
    SetDecRegister(dec_container->avs_regs, HWIF_DEC_AVSP_ENA, 1);
  } else {
    SetDecRegister(dec_container->avs_regs, HWIF_DEC_AVSP_ENA, 0);
  }

  if(dec_container->Hdrs.weighting_quant_flag == 1 &&
      dec_container->Hdrs.chroma_quant_param_disable == 0x0) {
    SetDecRegister(dec_container->avs_regs, HWIF_QP_DELTA_CB,
                   dec_container->Hdrs.chroma_quant_param_delta_cb);
    SetDecRegister(dec_container->avs_regs, HWIF_QP_DELTA_CR,
                   dec_container->Hdrs.chroma_quant_param_delta_cr);
  } else {
    SetDecRegister(dec_container->avs_regs, HWIF_QP_DELTA_CB, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_QP_DELTA_CR, 0);
  }

  if(dec_container->Hdrs.weighting_quant_flag == 1) {
    SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_MODEL,
                   dec_container->Hdrs.weighting_quant_model);

    SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_0,
                   dec_container->Hdrs.weighting_quant_param[0]);
    SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_1,
                   dec_container->Hdrs.weighting_quant_param[1]);
    SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_2,
                   dec_container->Hdrs.weighting_quant_param[2]);
    SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_3,
                   dec_container->Hdrs.weighting_quant_param[3]);
    SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_4,
                   dec_container->Hdrs.weighting_quant_param[4]);
    SetDecRegister(dec_container->avs_regs, HWIF_WEIGHT_QP_5,
                   dec_container->Hdrs.weighting_quant_param[5]);
  }
  /* AVS Plus end */

  if (dec_container->Hdrs.picture_structure == FRAMEPICTURE ||
      dec_container->ApiStorage.first_field) {
    SET_ADDR_REG(dec_container->avs_regs, HWIF_DEC_OUT_BASE,
                 dec_container->StrmStorage.p_pic_buf[(i32)dec_container->
                     StrmStorage.work_out].
                 data.bus_address);
  } else {

    /* start of bottom field line */
    if(dec_container->dpb_mode == DEC_DPB_FRAME ) {
      SET_ADDR_REG(dec_container->avs_regs, HWIF_DEC_OUT_BASE,
                   (dec_container->StrmStorage.p_pic_buf[(i32)dec_container->
                       StrmStorage.work_out].
                    data.bus_address +
                    ((dec_container->StrmStorage.frame_width << 4))));
    } else if( dec_container->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
      SET_ADDR_REG(dec_container->avs_regs, HWIF_DEC_OUT_BASE,
                   dec_container->StrmStorage.p_pic_buf[(i32)dec_container->
                       StrmStorage.work_out].
                   data.bus_address);
    }
  }
  SetDecRegister( dec_container->avs_regs, HWIF_PP_PIPELINE_E_U, dec_container->pp_enabled );
  if (dec_container->pp_enabled) {
    u32 dsw, dsh;
#define NEXT_MULTIPLE(value, n) (((value) + (n) - 1) & ~((n) - 1))

    dsw = NEXT_MULTIPLE((dec_container->StrmStorage.frame_width * 16 >> dec_container->dscale_shift_x) * 8, 16 * 8) / 8;
    dsh = (dec_container->StrmStorage.frame_height * 16 >> dec_container->dscale_shift_y);
    if (dec_container->dscale_shift_x == 0) {
      SetDecRegister(dec_container->avs_regs, HWIF_HOR_SCALE_MODE_U, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_WSCALE_INVRA_U, 0);
    } else {
      /* down scale */
      SetDecRegister(dec_container->avs_regs, HWIF_HOR_SCALE_MODE_U, 2);
      SetDecRegister(dec_container->avs_regs, HWIF_WSCALE_INVRA_U,
                     1<<(16-dec_container->dscale_shift_x));
    }

    if (dec_container->dscale_shift_y == 0) {
      SetDecRegister(dec_container->avs_regs, HWIF_VER_SCALE_MODE_U, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_HSCALE_INVRA_U, 0);
    } else {
      /* down scale */
      SetDecRegister(dec_container->avs_regs, HWIF_VER_SCALE_MODE_U, 2);
      SetDecRegister(dec_container->avs_regs, HWIF_HSCALE_INVRA_U,
                     1<<(16-dec_container->dscale_shift_y));
    }
    SET_ADDR64_REG(dec_container->avs_regs, HWIF_PP_OUT_LU_BASE_U,
                   dec_container->StrmStorage.p_pic_buf[dec_container->
                       StrmStorage.work_out].
                   pp_data->bus_address);
    SET_ADDR64_REG(dec_container->avs_regs, HWIF_PP_OUT_CH_BASE_U,
                   dec_container->StrmStorage.p_pic_buf[dec_container->
                       StrmStorage.work_out].
                   pp_data->bus_address + dsw * dsh);

    SetPpRegister(dec_container->avs_regs, HWIF_PP_IN_FORMAT_U, 1);
  }
  SetDecRegister( dec_container->avs_regs, HWIF_DPB_ILACE_MODE,
                  dec_container->dpb_mode );

  if(dec_container->Hdrs.picture_structure == FRAMEPICTURE) {
    if(dec_container->Hdrs.pic_coding_type == BFRAME) {
      /* past anchor set to future anchor if past is invalid (second
       * picture in sequence is B) */
      tmp_fwd =
        dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE ?
        dec_container->StrmStorage.work1 :
        dec_container->StrmStorage.work0;

      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER0_BASE,
                   dec_container->StrmStorage.p_pic_buf[(i32)tmp_fwd].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER1_BASE,
                   dec_container->StrmStorage.p_pic_buf[(i32)tmp_fwd].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER2_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER3_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                   bus_address);

      /* block distances */

      /* current to future anchor */
      tmp = (2*dec_container->StrmStorage.
             p_pic_buf[(i32)dec_container->StrmStorage.work0].picture_distance -
             2*dec_container->Hdrs.picture_distance + 512) & 0x1FF;
      /* prevent division by zero */
      if (!tmp) tmp = 2;
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_2, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_3, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_2,
                     512/tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_3,
                     512/tmp);

      /* current to past anchor */
      if (dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE) {
        tmp = (2*dec_container->Hdrs.picture_distance -
               2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work1].picture_distance -
               512) & 0x1FF;
        if (!tmp) tmp = 2;
      }
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_0, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_1, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_0,
                     512/tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_1,
                     512/tmp);

      /* future anchor to past anchor */
      if (dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE) {
        tmp = (2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work0].picture_distance -
               2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work1].picture_distance -
               + 512) & 0x1FF;
        if (!tmp) tmp = 2;
      }
      tmp = 16384/tmp;
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_0, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_1, tmp);

      /* future anchor to previous past anchor */
      tmp = dec_container->StrmStorage.future2prev_past_dist;
      tmp = 16384/tmp;
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_2, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_3, tmp);
    } else {
      tmp_fwd =
        dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE ?
        dec_container->StrmStorage.work1 :
        dec_container->StrmStorage.work0;

      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER0_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER1_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER2_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)tmp_fwd].data.bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER3_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)tmp_fwd].data.bus_address);

      /* current to past anchor */
      tmp = (2*dec_container->Hdrs.picture_distance -
             2*dec_container->StrmStorage.
             p_pic_buf[(i32)dec_container->StrmStorage.work0].picture_distance -
             512) & 0x1FF;
      if (!tmp) tmp = 2;

      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_0, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_1, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_0,
                     512/tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_1,
                     512/tmp);
      /* current to previous past anchor */
      if (dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE) {
        tmp = (2*dec_container->Hdrs.picture_distance -
               2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work1].picture_distance -
               512) & 0x1FF;
        if (!tmp) tmp = 2;
      }

      /* this will become "future to previous past" for next B */
      dec_container->StrmStorage.future2prev_past_dist = tmp;

      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_2, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_3, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_2,
                     512/tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_3,
                     512/tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_0, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_1, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_2, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_3, 0);
    }
    /* AVS Plus stuff */
    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_0, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_1, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_2, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_3, 0);

    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_0, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_1, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_2, 0);
    SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_3, 0);
    /* AVS Plus end */
  } else { /* field interlaced */
    if(dec_container->Hdrs.pic_coding_type == BFRAME) {
      /* past anchor set to future anchor if past is invalid (second
       * picture in sequence is B) */
      tmp_fwd =
        dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE ?
        dec_container->StrmStorage.work1 :
        dec_container->StrmStorage.work0;

      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER0_BASE,
                   dec_container->StrmStorage.p_pic_buf[(i32)tmp_fwd].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER1_BASE,
                   dec_container->StrmStorage.p_pic_buf[(i32)tmp_fwd].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER2_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                   bus_address);
      SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER3_BASE,
                   dec_container->StrmStorage.
                   p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                   bus_address);

      /* block distances */
      tmp = (2*dec_container->StrmStorage.
             p_pic_buf[(i32)dec_container->StrmStorage.work0].picture_distance -
             2*dec_container->Hdrs.picture_distance + 512) & 0x1FF;
      /* prevent division by zero */
      if (!tmp) tmp = 2;
      if (dec_container->ApiStorage.first_field) {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_2,
                       tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_3,
                       tmp+1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_2,
                       512/tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_3,
                       512/(tmp+1));
      } else {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_2,
                       tmp-1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_3,
                       tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_2,
                       512/(tmp-1));
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_3,
                       512/tmp);
      }

      if (dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE) {
        tmp = (2*dec_container->Hdrs.picture_distance -
               2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work1].picture_distance -
               512) & 0x1FF;
        if (!tmp) tmp = 2;
      }
      if (dec_container->ApiStorage.first_field) {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_0,
                       tmp-1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_1,
                       tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_0,
                       512/(tmp-1));
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_1,
                       512/tmp);
      } else {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_0,
                       tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_1,
                       tmp+1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_0,
                       512/tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_1,
                       512/(tmp+1));
      }

      if (dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE) {
        tmp = (2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work0].picture_distance -
               2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work1].picture_distance -
               + 512) & 0x1FF;
        if (!tmp) tmp = 2;
      }
      /* AVS Plus stuff */
      if (dec_container->Hdrs.pb_field_enhanced_flag &&
          !dec_container->ApiStorage.first_field) {
        /* in this case, BlockDistanceRef is different with before, the mvRef points to top field */
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_0,
                       16384/(tmp-1));
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_1,
                       16384/tmp);

        /* future anchor to previous past anchor */
        tmp = dec_container->StrmStorage.future2prev_past_dist;

        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_2,
                       16384/(tmp-1));
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_3,
                       16384/tmp);
      } else {
        if (dec_container->ApiStorage.first_field) {
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_0,
                         16384/(tmp-1));
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_1,
                         16384/tmp);
        } else {
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_0,
                         16384/1);
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_1,
                         16384/tmp);
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_2,
                         16384/(tmp+1));
        }

        /* future anchor to previous past anchor */
        tmp = dec_container->StrmStorage.future2prev_past_dist;

        if( dec_container->ApiStorage.first_field) {
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_2,
                         16384/(tmp-1));
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_3,
                         16384/tmp);
        } else
          SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_3,
                         16384/tmp);
      }

      if(dec_container->ApiStorage.first_field) {
        /* 1 means delta=2, 3 means delta=-2, 0 means delta=0 */
        /* delta1 */
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_0, 2);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_1, 0);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_2, 2);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_3, 0);

        /* deltaFw */
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_0, 2);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_1, 0);
        /* deltaBw */
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_2, 0);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_3, 0);
      } else {
        /* delta1 */
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_0, 2);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_1, 0);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_2, 2);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_3, 0);

        /* deltaFw */
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_0, 0);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_1, 0);
        /* deltaBw */
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_2,
                       (u32)-2);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_3,
                       (u32)-2);
      }
      /* AVS Plus end */
    } else {
      tmp_fwd =
        dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE ?
        dec_container->StrmStorage.work1 :
        dec_container->StrmStorage.work0;

      tmp_curr = dec_container->StrmStorage.work_out;
      /* past anchor not available -> use current (this results in using
       * the same top or bottom field as reference and output picture
       * base, utput is probably corrupted) */
      if(tmp_fwd == INVALID_ANCHOR_PICTURE)
        tmp_fwd = tmp_curr;

      if(dec_container->ApiStorage.first_field) {
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER0_BASE,
                     dec_container->StrmStorage.
                     p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                     bus_address);
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER1_BASE,
                     dec_container->StrmStorage.
                     p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                     bus_address);
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER2_BASE,
                     dec_container->StrmStorage.
                     p_pic_buf[(i32)tmp_fwd].data.bus_address);
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER3_BASE,
                     dec_container->StrmStorage.
                     p_pic_buf[(i32)tmp_fwd].data.bus_address);

      } else {
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER0_BASE,
                     dec_container->StrmStorage.p_pic_buf[(i32)tmp_curr].data.
                     bus_address);
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER1_BASE,
                     dec_container->StrmStorage.
                     p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                     bus_address);
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER2_BASE,
                     dec_container->StrmStorage.
                     p_pic_buf[(i32)dec_container->StrmStorage.work0].data.
                     bus_address);
        SET_ADDR_REG(dec_container->avs_regs, HWIF_REFER3_BASE,
                     dec_container->StrmStorage.p_pic_buf[(i32)tmp_fwd].data.
                     bus_address);
      }

      tmp = (2*dec_container->Hdrs.picture_distance -
             2*dec_container->StrmStorage.
             p_pic_buf[(i32)dec_container->StrmStorage.work0].picture_distance -
             512) & 0x1FF;
      if (!tmp) tmp = 2;

      if(!dec_container->ApiStorage.first_field) {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_0, 1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_2,
                       tmp+1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_0,
                       512/1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_2,
                       512/(tmp+1));
      } else {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_0,
                       tmp-1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_0,
                       512/(tmp-1));
      }
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_1, tmp);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_1,
                     512/tmp);

      if (dec_container->StrmStorage.work1 != INVALID_ANCHOR_PICTURE) {
        tmp = (2*dec_container->Hdrs.picture_distance -
               2*dec_container->StrmStorage.
               p_pic_buf[(i32)dec_container->StrmStorage.work1].picture_distance -
               512) & 0x1FF;
        if (!tmp) tmp = 2;
      }

      /* this will become "future to previous past" for next B */
      dec_container->StrmStorage.future2prev_past_dist = tmp;

      if(dec_container->ApiStorage.first_field) {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_2,
                       tmp-1);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_3,
                       tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_2,
                       512/(tmp-1));
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_3,
                       512/tmp);
      } else {
        SetDecRegister(dec_container->avs_regs, HWIF_REF_DIST_CUR_3,
                       tmp);
        SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_CUR_3,
                       512/tmp);
      }

      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_0, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_1, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_2, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_INVD_COL_3, 0);

      /* AVS Plus stuff */
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_0, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_1, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_2, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_COL_3, 0);

      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_0, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_1, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_2, 0);
      SetDecRegister(dec_container->avs_regs, HWIF_REF_DELTA_CUR_3, 0);
      /* AVS Plus end */
    }

  }

  SetDecRegister(dec_container->avs_regs, HWIF_STARTMB_X, 0);
  SetDecRegister(dec_container->avs_regs, HWIF_STARTMB_Y, 0);

  SetDecRegister(dec_container->avs_regs, HWIF_FILTERING_DIS,
                 dec_container->Hdrs.loop_filter_disable);
  SetDecRegister(dec_container->avs_regs, HWIF_ALPHA_OFFSET,
                 dec_container->Hdrs.alpha_offset);
  SetDecRegister(dec_container->avs_regs, HWIF_BETA_OFFSET,
                 dec_container->Hdrs.beta_offset);
  SetDecRegister(dec_container->avs_regs, HWIF_SKIP_MODE,
                 dec_container->Hdrs.skip_mode_flag);

  SetDecRegister(dec_container->avs_regs, HWIF_PIC_REFER_FLAG,
                 dec_container->Hdrs.picture_reference_flag);

  if (dec_container->Hdrs.pic_coding_type == PFRAME ||
      (dec_container->Hdrs.pic_coding_type == IFRAME /*&&
         !dec_container->ApiStorage.first_field*/)) { /* AVS Plus change */
    SetDecRegister(dec_container->avs_regs, HWIF_WRITE_MVS_E, 1);
  } else
    SetDecRegister(dec_container->avs_regs, HWIF_WRITE_MVS_E, 0);

  if (dec_container->ApiStorage.first_field ||
      ( dec_container->Hdrs.pic_coding_type == BFRAME &&
        dec_container->StrmStorage.prev_pic_structure ))
    SET_ADDR_REG(dec_container->avs_regs, HWIF_DIR_MV_BASE,
                 dec_container->StrmStorage.direct_mvs.bus_address);
  else
    SET_ADDR_REG(dec_container->avs_regs, HWIF_DIR_MV_BASE,
                 dec_container->StrmStorage.direct_mvs.bus_address+
                 ( ( ( dec_container->StrmStorage.frame_width *
                       dec_container->StrmStorage.frame_height/2 + 1) & ~0x1) *
                   4 * sizeof(u32)));
  /* AVS Plus stuff */
  SET_ADDR_REG(dec_container->avs_regs, HWIF_DIR_MV_BASE2,
               dec_container->StrmStorage.direct_mvs.bus_address);
  /* AVS Plus end */
  SetDecRegister(dec_container->avs_regs, HWIF_PREV_ANC_TYPE,
                 !dec_container->StrmStorage.p_pic_buf[(i32)dec_container->StrmStorage.work0].
                 pic_type ||
                 (!dec_container->ApiStorage.first_field &&
                  dec_container->StrmStorage.prev_pic_structure == 0));

  /* b-picture needs to know if future reference is field or frame coded */
  SetDecRegister(dec_container->avs_regs, HWIF_REFER2_FIELD_E,
                 dec_container->StrmStorage.prev_pic_structure == 0);
  SetDecRegister(dec_container->avs_regs, HWIF_REFER3_FIELD_E,
                 dec_container->StrmStorage.prev_pic_structure == 0);

  /* Setup reference picture buffer */
  if( dec_container->ref_buf_support )
    RefbuSetup(&dec_container->ref_buffer_ctrl, dec_container->avs_regs,
               dec_container->Hdrs.picture_structure == FIELDPICTURE ?
               REFBU_FIELD : REFBU_FRAME,
               dec_container->Hdrs.pic_coding_type == IFRAME,
               dec_container->Hdrs.pic_coding_type == BFRAME, 0, 2,
               0 );

  if( dec_container->tiled_mode_support) {
    dec_container->tiled_reference_enable =
      DecSetupTiledReference( dec_container->avs_regs,
                              dec_container->tiled_mode_support,
                              dec_container->dpb_mode,
                              !dec_container->Hdrs.progressive_sequence );
  } else {
    dec_container->tiled_reference_enable = 0;
  }

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    Function name: AvsDecSetupDeinterlace

    Functional description:
        Setup PP interface for deinterlacing

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void AvsDecSetupDeinterlace(DecContainer * dec_cont) {
  if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
    dec_cont->pp_control.pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
    dec_cont->pp_control.bottom_bus_luma = dec_cont->pp_control.input_bus_luma +
                                           (dec_cont->StrmStorage.frame_width << 4);
    dec_cont->pp_control.bottom_bus_chroma = dec_cont->pp_control.input_bus_chroma +
        (dec_cont->StrmStorage.frame_width << 4);
  } else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
    dec_cont->pp_control.pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD;
    dec_cont->pp_control.bottom_bus_luma = dec_cont->pp_control.input_bus_luma +
                                           (dec_cont->StrmStorage.frame_width*
                                            dec_cont->StrmStorage.frame_height << 7);
    dec_cont->pp_control.bottom_bus_chroma = dec_cont->pp_control.input_bus_chroma +
        (dec_cont->StrmStorage.frame_width*
         dec_cont->StrmStorage.frame_height << 6);
  } else {
    ASSERT(0);
  }
}

/*------------------------------------------------------------------------------

    Function name: AvsDecPrepareFieldProcessing

    Functional description:
        Setup PP interface for deinterlacing

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void AvsDecPrepareFieldProcessing(DecContainer * dec_cont) {
  dec_cont->pp_control.pic_struct =
    dec_cont->Hdrs.top_field_first ?
    DECPP_PIC_TOP_FIELD_FRAME : DECPP_PIC_BOT_FIELD_FRAME;
  dec_cont->ApiStorage.buffer_for_pp = dec_cont->StrmStorage.work0 + 1;

  /* forward tiled mode */
  dec_cont->pp_control.tiled_input_mode = dec_cont->tiled_reference_enable;
  dec_cont->pp_control.progressive_sequence =
    dec_cont->Hdrs.progressive_sequence;

  if(dec_cont->Hdrs.top_field_first) {
    dec_cont->pp_control.input_bus_luma =
      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->
                                      StrmStorage.work0].data.bus_address;

    dec_cont->pp_control.input_bus_chroma =
      dec_cont->pp_control.input_bus_luma +
      ((dec_cont->StrmStorage.frame_width *
        dec_cont->StrmStorage.frame_height) << 8);
  } else {

    if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
      dec_cont->pp_control.bottom_bus_luma =
        dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->
                                        StrmStorage.work0].data.bus_address +
        (dec_cont->StrmStorage.frame_width << 4);

      dec_cont->pp_control.bottom_bus_chroma =
        dec_cont->pp_control.bottom_bus_luma +
        ((dec_cont->StrmStorage.frame_width *
          dec_cont->StrmStorage.frame_height) << 8);
    } else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
      u32 frame_size = (dec_cont->StrmStorage.frame_width *
                        dec_cont->StrmStorage.
                        frame_height) << 8;
      dec_cont->pp_control.bottom_bus_luma =
        dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->
                                        StrmStorage.work0].
        data.bus_address + frame_size/2;

      dec_cont->pp_control.bottom_bus_chroma =
        dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->
                                        StrmStorage.work0].
        data.bus_address + frame_size + frame_size/4;
    }
  }

  if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
    dec_cont->pp_control.pic_struct = (dec_cont->Hdrs.top_field_first) ?
                                      DECPP_PIC_FRAME_OR_TOP_FIELD : DECPP_PIC_BOT_FIELD;
  }

  dec_cont->pp_control.inwidth =
    dec_cont->pp_control.cropped_w = dec_cont->StrmStorage.frame_width << 4;
  dec_cont->pp_control.inheight =
    (((dec_cont->StrmStorage.frame_height + 1) & ~1) / 2) << 4;
  dec_cont->pp_control.cropped_h = (dec_cont->StrmStorage.frame_height << 4) / 2;

  AVSDEC_DEBUG(("FIELD: send %s\n",
                dec_cont->pp_control.pic_struct ==
                DECPP_PIC_TOP_FIELD_FRAME ? "top" : "bottom"));
}

/*------------------------------------------------------------------------------

    Function name: AvsCheckFormatSupport

    Functional description:
        Check if avs supported

    Input:
        container

    Return values:
        return zero for OK

------------------------------------------------------------------------------*/
u32 AvsCheckFormatSupport(void) {
  u32 id = 0;
  u32 product = 0;
  DWLHwConfig hw_config;

  id = DWLReadAsicID(DWL_CLIENT_TYPE_AVS_DEC);

  product = id >> 16;

  if(product < 0x8170 &&
      product != 0x6731 )
    return ~0;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_AVS_DEC);

  return (hw_config.avs_support == AVS_NOT_SUPPORTED);
}

/*------------------------------------------------------------------------------

    Function name: AvsPpControl

    Functional description:
        set up and start pp

    Input:
        container
        pipeline_off     override pipeline setting

    Return values:
        void

------------------------------------------------------------------------------*/

void AvsPpControl(DecContainer * dec_container, u32 pipeline_off) {
  u32 index_for_pp = AVS_BUFFER_UNDEFINED;

  DecPpInterface *pc = &dec_container->pp_control;
  DecHdrs *p_hdrs = &dec_container->Hdrs;
  u32 next_buffer_index;

  /* PP not connected or still running (not waited when first field of frame
   * finished */
  if (pc->pp_status == DECPP_PIC_NOT_FINISHED)
    return;

  dec_container->pp_config_query.tiled_mode =
    dec_container->tiled_reference_enable;
  dec_container->PPConfigQuery(dec_container->pp_instance,
                               &dec_container->pp_config_query);

  AvsPpMultiBufferSetup(dec_container, (pipeline_off ||
                                        !dec_container->pp_config_query.
                                        pipeline_accepted));

  dec_container->StrmStorage.p_pic_buf[(i32)dec_container->StrmStorage.work_out].
  send_to_pp = 1;

  /* Select new PP buffer index to use. If multibuffer is disabled, use
   * previous buffer, otherwise select new buffer from queue. */
  if(pc->multi_buf_stat != MULTIBUFFER_DISABLED) {
    next_buffer_index = BqueueNext( &dec_container->StrmStorage.bq_pp,
                                    BQUEUE_UNUSED,
                                    BQUEUE_UNUSED,
                                    BQUEUE_UNUSED,
                                    dec_container->Hdrs.pic_coding_type == BFRAME);
    pc->buffer_index = next_buffer_index;
  } else {
    next_buffer_index = pc->buffer_index;
  }

  if(p_hdrs->low_delay ||
      dec_container->Hdrs.pic_coding_type == BFRAME) {
    pc->display_index = pc->buffer_index;
  } else {
    pc->display_index = pc->prev_anchor_display_index;
  }

  /* Connect PP output buffer to decoder output buffer */
  {
    addr_t luma = 0;
    addr_t chroma = 0;
    addr_t bot_luma = 0, bot_chroma = 0;

    luma = dec_container->StrmStorage.
           p_pic_buf[(i32)dec_container->StrmStorage.work_out].data.bus_address;
    chroma = luma + ((dec_container->StrmStorage.frame_width *
                      dec_container->StrmStorage.frame_height) << 8);

    if(dec_container->dpb_mode == DEC_DPB_FRAME ) {
      bot_luma = luma + (dec_container->StrmStorage.frame_width * 16);
      bot_chroma = chroma + (dec_container->StrmStorage.frame_width * 16);
    } else if( dec_container->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
      bot_luma = luma + ((dec_container->StrmStorage.frame_width * 16) *
                         (dec_container->StrmStorage.frame_height * 8));
      bot_chroma = chroma + ((dec_container->StrmStorage.frame_width * 16) *
                             (dec_container->StrmStorage.frame_height * 4));
    }

    dec_container->PPBufferData(dec_container->pp_instance,
                                pc->buffer_index, luma, chroma, bot_luma, bot_chroma);
  }

  if(pc->multi_buf_stat == MULTIBUFFER_FULLMODE) {
    pc->use_pipeline = dec_container->pp_config_query.pipeline_accepted;
    AvsDecRunFullmode(dec_container);
    dec_container->StrmStorage.previous_mode_full = 1;
  } else if(dec_container->StrmStorage.previous_mode_full == 1) {
    if(dec_container->Hdrs.pic_coding_type == BFRAME) {
      dec_container->StrmStorage.previous_b = 1;
    } else {
      dec_container->StrmStorage.previous_b = 0;
    }

    if(dec_container->Hdrs.pic_coding_type == BFRAME) {
      index_for_pp = AVS_BUFFER_UNDEFINED;
      pc->input_bus_luma = 0;
    }
    pc->pp_status = DECPP_IDLE;

    dec_container->StrmStorage.p_pic_buf[(i32)dec_container->StrmStorage.work0].
    send_to_pp = 1;

    dec_container->StrmStorage.previous_mode_full = 0;
  } else {
    pc->buffer_index = pc->display_index;

    if(dec_container->Hdrs.pic_coding_type == BFRAME) {
      dec_container->StrmStorage.previous_b = 1;
    } else {
      dec_container->StrmStorage.previous_b = 0;
    }

    if((!dec_container->StrmStorage.sequence_low_delay &&
        (dec_container->Hdrs.pic_coding_type != BFRAME)) ||
        !p_hdrs->progressive_sequence || pipeline_off) {
      pc->use_pipeline = 0;
    } else {
      pc->use_pipeline = dec_container->pp_config_query.pipeline_accepted;
    }

    if(!pc->use_pipeline) {
      /* pipeline not accepted, don't run for first picture */
      if(dec_container->StrmStorage.frame_number &&
          (dec_container->ApiStorage.buffer_for_pp == NO_BUFFER) &&
          (p_hdrs->progressive_sequence ||
           dec_container->ApiStorage.first_field ||
           !dec_container->pp_config_query.deinterlace)) {
        /*if:
         * B pictures allowed and non B picture OR
         * B pictures not allowed */
        if(dec_container->StrmStorage.sequence_low_delay ||
            dec_container->Hdrs.pic_coding_type != BFRAME) {

          /* forward tiled mode */
          dec_container->pp_control.tiled_input_mode =
            dec_container->StrmStorage.p_pic_buf[(i32)dec_container->
                                                 StrmStorage.work0].
            tiled_mode;
          dec_container->pp_control.progressive_sequence =
            dec_container->Hdrs.progressive_sequence;

          pc->input_bus_luma =
            dec_container->StrmStorage.p_pic_buf[(i32)dec_container->
                                                 StrmStorage.work0].
            data.bus_address;

          pc->input_bus_chroma =
            pc->input_bus_luma +
            ((dec_container->StrmStorage.frame_width *
              dec_container->StrmStorage.frame_height) << 8);

          pc->inwidth = pc->cropped_w =
                          dec_container->StrmStorage.frame_width << 4;
          pc->inheight = pc->cropped_h =
                           dec_container->StrmStorage.frame_height << 4;
          {
            u32 value = GetDecRegister(dec_container->avs_regs,
                                       HWIF_DEC_OUT_ENDIAN);

            pc->little_endian =
              (value == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
          }
          pc->word_swap =
            GetDecRegister(dec_container->avs_regs,
                           HWIF_DEC_OUTSWAP32_E) ? 1 : 0;

          index_for_pp = dec_container->StrmStorage.work0;

          if(dec_container->pp_config_query.deinterlace) {
            AvsDecSetupDeinterlace(dec_container);
          }
          /* if field output is used, send only a field to PP */
          else if(!p_hdrs->progressive_sequence) {
            AvsDecPrepareFieldProcessing(dec_container);
          }
          dec_container->StrmStorage.p_pic_buf[(i32)index_for_pp].send_to_pp = 2;

          dec_container->ApiStorage.pp_pic_index = index_for_pp;
        } else {
          index_for_pp = dec_container->StrmStorage.work_out;
          index_for_pp = AVS_BUFFER_UNDEFINED;
          pc->input_bus_luma = 0;
        }
      } else {
        pc->input_bus_luma = 0;
      }
    } else { /* pipeline */
      pc->input_bus_luma = pc->input_bus_chroma = 0;
      index_for_pp = dec_container->StrmStorage.work_out;

      /* forward tiled mode */
      pc->tiled_input_mode = dec_container->tiled_reference_enable;
      pc->progressive_sequence =
        dec_container->Hdrs.progressive_sequence;

      pc->inwidth = pc->cropped_w =
                      dec_container->StrmStorage.frame_width << 4;
      pc->inheight = pc->cropped_h =
                       dec_container->StrmStorage.frame_height << 4;
    }

    /* start PP */
    if(((pc->input_bus_luma && !pc->use_pipeline) ||
        (!pc->input_bus_luma && pc->use_pipeline))
        && dec_container->StrmStorage.p_pic_buf[(i32)index_for_pp].send_to_pp) {

      if(pc->use_pipeline) { /*CHECK !! */
        if(dec_container->Hdrs.pic_coding_type == BFRAME)
          SetDecRegister(dec_container->avs_regs, HWIF_DEC_OUT_DIS, 1);
      }

      ASSERT(index_for_pp != AVS_BUFFER_UNDEFINED);

      if(pc->pic_struct == DECPP_PIC_FRAME_OR_TOP_FIELD ||
          pc->pic_struct == DECPP_PIC_TOP_AND_BOT_FIELD_FRAME ||
          pc->pic_struct == DECPP_PIC_TOP_AND_BOT_FIELD) {
        dec_container->StrmStorage.p_pic_buf[(i32)index_for_pp].send_to_pp = 0;
      } else {
        dec_container->StrmStorage.p_pic_buf[(i32)index_for_pp].send_to_pp--;
      }

      dec_container->PPRun(dec_container->pp_instance, pc);

      pc->pp_status = DECPP_RUNNING;
    }
    dec_container->StrmStorage.previous_mode_full = 0;
  }

  if( dec_container->Hdrs.pic_coding_type != BFRAME ) {
    pc->prev_anchor_display_index = next_buffer_index;
  }
}

/*------------------------------------------------------------------------------

    Function name: AvsPpMultiBufferSetup

    Functional description:
        Modify state of pp output buffering.

    Input:
        container
        pipeline_off     override pipeline setting

    Return values:
        void

------------------------------------------------------------------------------*/
void AvsPpMultiBufferSetup(DecContainer * dec_cont, u32 pipeline_off) {

  if(dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_DISABLED) {
    return;
  }

  if(pipeline_off && dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_FULLMODE) {
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_SEMIMODE;
  }

  if(!pipeline_off &&
      dec_cont->Hdrs.progressive_sequence &&
      (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_SEMIMODE)) {
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_FULLMODE;
  }

  if(dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_UNINIT)
    AvsPpMultiBufferInit(dec_cont);

}

/*------------------------------------------------------------------------------

    Function name: AvsPpMultiBufferInit

    Functional description:
        Modify state of pp output buffering.

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
void AvsPpMultiBufferInit(DecContainer * dec_cont) {
  DecPpQuery *pq = &dec_cont->pp_config_query;
  DecPpInterface *pc = &dec_cont->pp_control;

  if(pq->multi_buffer) {
    if(!pq->pipeline_accepted || !dec_cont->Hdrs.progressive_sequence) {

      pc->multi_buf_stat = MULTIBUFFER_SEMIMODE;
    } else {
      pc->multi_buf_stat = MULTIBUFFER_FULLMODE;
    }

    pc->buffer_index = 1;
  } else {
    pc->multi_buf_stat = MULTIBUFFER_DISABLED;
  }

}

/*------------------------------------------------------------------------------

    Function name: AvsDecRunFullmode

    Functional description:

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
void AvsDecRunFullmode(DecContainer * dec_cont) {
  u32 index_for_pp = AVS_BUFFER_UNDEFINED;
  DecPpInterface *pc = &dec_cont->pp_control;

#ifdef _DEC_PP_USAGE
  AvsDecPpUsagePrint(dec_cont, DECPP_PIPELINED,
                     dec_cont->StrmStorage.work_out, 0,
                     dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->
                         StrmStorage.
                         work_out].pic_id);
#endif

  if(!dec_cont->StrmStorage.previous_mode_full &&
      dec_cont->StrmStorage.frame_number) {
    if(dec_cont->Hdrs.pic_coding_type == BFRAME) {
      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work0].
      send_to_pp = 0;
      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work1].
      send_to_pp = 0;
    } else {
      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work0].
      send_to_pp = 0;
    }
  }

  if(dec_cont->Hdrs.pic_coding_type == BFRAME) {
    dec_cont->StrmStorage.previous_b = 1;
  } else {
    dec_cont->StrmStorage.previous_b = 0;
  }

  index_for_pp = dec_cont->StrmStorage.work_out;
  pc->input_bus_luma = dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->
                       StrmStorage.work_out].
                       data.bus_address;

  pc->input_bus_chroma = pc->input_bus_luma +
                         ((dec_cont->StrmStorage.frame_width *
                           dec_cont->StrmStorage.frame_height) << 8);

  pc->inwidth = pc->cropped_w = dec_cont->StrmStorage.frame_width << 4;
  pc->inheight = pc->cropped_h = dec_cont->StrmStorage.frame_height << 4;
  /* forward tiled mode */
  pc->tiled_input_mode = dec_cont->tiled_reference_enable;
  pc->progressive_sequence = dec_cont->Hdrs.progressive_sequence;

  {
    if(dec_cont->Hdrs.pic_coding_type == BFRAME)
      SetDecRegister(dec_cont->avs_regs, HWIF_DEC_OUT_DIS, 1);
  }

  ASSERT(index_for_pp != AVS_BUFFER_UNDEFINED);

  ASSERT(dec_cont->StrmStorage.p_pic_buf[(i32)index_for_pp].send_to_pp == 1);
  dec_cont->StrmStorage.p_pic_buf[(i32)index_for_pp].send_to_pp = 0;

  dec_cont->PPRun(dec_cont->pp_instance, pc);

  pc->pp_status = DECPP_RUNNING;
}

/*------------------------------------------------------------------------------

    Function name: AvsDecPeek

    Functional description:
        Retrieve last decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct

    Output:
        picture Decoder output picture.

    Return values:
        AVSDEC_OK         No picture available.
        AVSDEC_PIC_RDY    Picture ready.

------------------------------------------------------------------------------*/
AvsDecRet AvsDecPeek(AvsDecInst dec_inst, AvsDecPicture * picture) {
  /* Variables */

  DecContainer *dec_cont;
  u32 pic_index;
  picture_t *p_pic;

  /* Code */

  AVS_API_TRC("\nAvs_dec_peek#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    AVS_API_TRC("AvsDecPeek# ERROR: picture is NULL");
    return (AVSDEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecPeek# ERROR: Decoder not initialized");
    return (AVSDEC_NOT_INITIALIZED);
  }

#ifdef USE_OUTPUT_RELEASE
  /* when output release thread enabled, AvsDecNextPicture_INTERNAL() called in
     AvsDecDecode(), and "dec_cont->StrmStorage.out_count--" may called in
     AvsDecNextPicture() before AvsDecPeek() called, so dec_cont->fullness
     used to sample the real out_count in case of AvsDecNextPicture_INTERNAL() called
     before than AvsDecPeek() */
  u32 tmp = dec_cont->fullness;
#else
  u32 tmp = dec_cont->StrmStorage.out_count;
#endif

  if(!tmp ||
      dec_cont->StrmStorage.new_headers_change_resolution) {
    (void) DWLmemset(picture, 0, sizeof(AvsDecPicture));
    return AVSDEC_OK;
  }

  pic_index = dec_cont->StrmStorage.work_out;
  if (!dec_cont->pp_enabled) {
    picture->frame_width = dec_cont->StrmStorage.frame_width << 4;
    picture->frame_height = dec_cont->StrmStorage.frame_height << 4;
    picture->coded_width = dec_cont->Hdrs.horizontal_size;
    picture->coded_height = dec_cont->Hdrs.vertical_size;
  } else {
    picture->frame_width = (dec_cont->StrmStorage.frame_width << 4) >> dec_cont->dscale_shift_x;
    picture->frame_height = (dec_cont->StrmStorage.frame_height << 4) >> dec_cont->dscale_shift_y;
    picture->coded_width = dec_cont->Hdrs.horizontal_size >> dec_cont->dscale_shift_x;
    picture->coded_height = dec_cont->Hdrs.vertical_size >> dec_cont->dscale_shift_y;
  }

  picture->interlaced = !dec_cont->Hdrs.progressive_sequence;

  p_pic = dec_cont->StrmStorage.p_pic_buf + pic_index;
  if (!dec_cont->pp_enabled) {
    picture->output_picture = (u8 *) p_pic->data.virtual_address;
    picture->output_picture_bus_address = p_pic->data.bus_address;
  } else {
    picture->output_picture = (u8 *) p_pic->pp_data->virtual_address;
    picture->output_picture_bus_address = p_pic->pp_data->bus_address;
  }
  picture->key_picture = p_pic->pic_type;
  picture->pic_id = p_pic->pic_id;
  picture->decode_id = p_pic->pic_id;
  picture->pic_coding_type = p_pic->pic_code_type;

  picture->repeat_first_field = p_pic->rff;
  picture->repeat_frame_count = p_pic->rfc;
  picture->number_of_err_mbs = p_pic->nbr_err_mbs;
  picture->output_format = p_pic->tiled_mode ?
                           DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

  (void) DWLmemcpy(&picture->time_code,
                   &p_pic->time_code, sizeof(AvsDecTime));

  /* frame output */
  picture->field_picture = 0;
  picture->top_field = 0;
  picture->first_field = 0;

  return AVSDEC_PIC_RDY;
}

#ifdef USE_EXTERNAL_BUFFER
void AvsSetExternalBufferInfo(AvsDecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *)dec_inst;
  u32 pic_size_in_mbs = 0;
  u32 ext_buffer_size;

  if(dec_cont->pp_instance)
    pic_size_in_mbs = dec_cont->StrmStorage.frame_width *
                      ((dec_cont->StrmStorage.frame_height + 1) / 2) * 2;
  else
    pic_size_in_mbs = dec_cont->StrmStorage.total_mbs_in_frame;

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

  if (dec_cont->pp_enabled) {
    u32 pp_width, pp_height, pp_stride, pp_buff_size;
    pp_width = (dec_cont->StrmStorage.frame_width * 16) >> dec_cont->dscale_shift_x;
    pp_height = (dec_cont->StrmStorage.frame_height * 16) >> dec_cont->dscale_shift_y;
    pp_stride = ((pp_width + 15) >> 4) << 4;
    pp_buff_size = pp_stride * pp_height * 3 / 2;
    ext_buffer_size = pp_buff_size;
  }

  dec_cont->tot_buffers_added = dec_cont->tot_buffers;
  dec_cont->tot_buffers = dec_cont->buf_num =  buffers;
  dec_cont->next_buf_size = ext_buffer_size;
}

AvsDecRet AvsDecGetBufferInfo(AvsDecInst dec_inst, AvsDecBufferInfo *mem_info) {
  DecContainer  * dec_cont = (DecContainer *)dec_inst;

  struct DWLLinearMem empty = {0, 0, 0, 0};

  struct DWLLinearMem *buffer = NULL;

  if(dec_cont == NULL || mem_info == NULL) {
    return AVSDEC_PARAM_ERROR;
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
        return (AVSDEC_MEMFAIL);
      }
      dec_cont->StrmStorage.ext_buffer_added = 0;
      mem_info->buf_to_free = empty;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return AVSDEC_OK;
    } else {
      mem_info->buf_to_free = *buffer;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return AVSDEC_WAITING_FOR_BUFFER;
    }
  }

  if(dec_cont->buf_to_free == NULL && dec_cont->next_buf_size == 0) {
    /* External reference buffer: release done. */
    mem_info->buf_to_free = empty;
    mem_info->next_buf_size = dec_cont->next_buf_size;
    mem_info->buf_num = dec_cont->buf_num;
    return AVSDEC_OK;
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

  return AVSDEC_WAITING_FOR_BUFFER;
}

AvsDecRet AvsDecAddBuffer(AvsDecInst dec_inst, struct DWLLinearMem *info) {
  DecContainer *dec_cont = (DecContainer *)dec_inst;
  AvsDecRet dec_ret = AVSDEC_OK;

  if(dec_inst == NULL || info == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(info->virtual_address) ||
      X170_CHECK_BUS_ADDRESS_AGLINED(info->bus_address) ||
      info->size < dec_cont->next_buf_size) {
    return AVSDEC_PARAM_ERROR;
  }

  u32 i = dec_cont->buffer_index;

  dec_cont->n_ext_buf_size = info->size;
  dec_cont->ext_buffers[dec_cont->ext_buffer_num] = *info;
  dec_cont->ext_buffer_num++;

  if (dec_cont->pp_enabled == 0) {
    if(i < dec_cont->tot_buffers) {
      dec_cont->StrmStorage.p_pic_buf[i].data = *info;
      dec_cont->buffer_index++;

      if(dec_cont->buffer_index < dec_cont->tot_buffers)
        dec_ret = AVSDEC_WAITING_FOR_BUFFER;
    } else {
      /* Adding extra buffers. */
      if(i >= 16) {
        /* Too much buffers added. */
        return AVSDEC_EXT_BUFFER_REJECTED;
      }

      dec_cont->StrmStorage.p_pic_buf[i].data = *info;

      dec_cont->buffer_index++;
      dec_cont->tot_buffers ++;
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
void AvsEnterAbortState(DecContainer *dec_cont) {
  BqueueSetAbort(&dec_cont->StrmStorage.bq);
#ifdef USE_OMXIL_BUFFER
  FifoSetAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 1;
}

void AvsExistAbortState(DecContainer *dec_cont) {
  BqueueClearAbort(&dec_cont->StrmStorage.bq);
#ifdef USE_OMXIL_BUFFER
  FifoClearAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 0;
}

void AvsEmptyBufferQueue(DecContainer *dec_cont) {
  BqueueEmpty(&dec_cont->StrmStorage.bq);
  dec_cont->StrmStorage.work_out = 0;
  dec_cont->StrmStorage.work0 =
    dec_cont->StrmStorage.work1 = INVALID_ANCHOR_PICTURE;
}

void AvsStateReset(DecContainer *dec_cont) {
  u32 buffers = 3;

  if( !dec_cont->pp_instance ) { /* Combined mode used */
    buffers = dec_cont->StrmStorage.max_num_buffers;
    if( buffers < 3 )
      buffers = 3;
  }

  /* Clear internal parameters in DecContainer */
  dec_cont->keep_hw_reserved = 0;
#ifdef USE_EXTERNAL_BUFFER
#ifdef USE_OMXIL_BUFFER
  dec_cont->tot_buffers = buffers;
  dec_cont->buffer_index = 0;
#endif
  dec_cont->no_reallocation = 1;
#endif
  dec_cont->fullness = 0;
#ifdef USE_OMXIL_BUFFER
  dec_cont->fifo_index = 0;
  dec_cont->ext_buffer_num = 0;
#endif
  dec_cont->dec_stat = AVSDEC_OK;

  /* Clear internal parameters in DecStrmStorage */
  dec_cont->StrmStorage.valid_pic_header = 0;
#ifdef CLEAR_HDRINFO_IN_SEEK
  dec_cont->StrmStorage.strm_dec_ready = 0;
  dec_cont->StrmStorage.valid_sequence = 0;
#endif
  dec_cont->StrmStorage.out_index = 0;
  dec_cont->StrmStorage.out_count = 0;
  dec_cont->StrmStorage.skip_b = 0;
  dec_cont->StrmStorage.prev_pic_coding_type = 0;
  dec_cont->StrmStorage.prev_pic_structure = 0;
  dec_cont->StrmStorage.field_out_index = 1;
  dec_cont->StrmStorage.frame_number = 0;
  dec_cont->StrmStorage.picture_broken = 0;
  dec_cont->StrmStorage.unsupported_features_present = 0;
  dec_cont->StrmStorage.previous_b = 0;
  dec_cont->StrmStorage.previous_mode_full = 0;
  dec_cont->StrmStorage.sequence_low_delay = 0;
  dec_cont->StrmStorage.new_headers_change_resolution = 0;
#ifdef USE_OMXIL_BUFFER
  dec_cont->StrmStorage.bq.queue_size = buffers;
  dec_cont->StrmStorage.num_buffers = buffers;
#endif
  dec_cont->StrmStorage.future2prev_past_dist = 0;

  /* Clear internal parameters in DecApiStorage */
  dec_cont->ApiStorage.DecStat = INITIALIZED;
  dec_cont->ApiStorage.first_field = 1;
  dec_cont->ApiStorage.output_other_field = 0;

  (void) DWLmemset(&(dec_cont->StrmDesc), 0, sizeof(DecStrmDesc));
  (void) DWLmemset(&(dec_cont->out_data), 0, sizeof(AvsDecOutput));
  (void) DWLmemset(dec_cont->StrmStorage.out_buf, 0, 16 * sizeof(u32));
#ifdef USE_OMXIL_BUFFER
  (void) DWLmemset(dec_cont->StrmStorage.p_pic_buf, 0, 16 * sizeof(picture_t));
  (void) DWLmemset(dec_cont->StrmStorage.picture_info, 0, 32 * sizeof(AvsDecPicture));
#endif
#ifdef CLEAR_HDRINFO_IN_SEEK
  (void) DWLmemset(&(dec_cont->Hdrs), 0, sizeof(DecHdrs));
  (void) DWLmemset(&(dec_cont->tmp_hdrs), 0, sizeof(DecHdrs));
  AvsAPI_InitDataStructures(dec_cont);
#endif

#ifdef USE_OMXIL_BUFFER
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
  FifoInit(32, &dec_cont->fifo_display);
#endif
}

AvsDecRet AvsDecAbort(AvsDecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;

  AVS_API_TRC("AvsDecAbort#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecAbort# ERROR: Decoder not initialized");
    return (AVSDEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);
  /* Abort frame buffer waiting */
  AvsEnterAbortState(dec_cont);

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  return (AVSDEC_OK);
}

AvsDecRet AvsDecAbortAfter(AvsDecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;

  AVS_API_TRC("AvsDecAbortAfter#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    AVS_API_TRC("AvsDecAbortAfter# ERROR: Decoder not initialized");
    return (AVSDEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);

#if 0
  /* If normal EOS is waited, return directly */
  if(dec_cont->dec_stat == AVSDEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (AVSDEC_OK);
  }
#endif

  /* Stop and release HW */
  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->avs_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->avs_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->avs_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->avs_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  } else if(dec_cont->keep_hw_reserved) {
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->keep_hw_reserved = 0;
  }

  /* Clear any remaining pictures from DPB */
  AvsEmptyBufferQueue(dec_cont);

  AvsStateReset(dec_cont);

  AvsExistAbortState(dec_cont);

  pthread_mutex_unlock(&dec_cont->protect_mutex);

  AVS_API_TRC("AvsDecAbortAfter# AVSDEC_OK\n");
  return (AVSDEC_OK);
}
#endif
