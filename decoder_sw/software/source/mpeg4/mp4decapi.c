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
#include "mp4deccfg.h"
#include "mp4dechwd_container.h"
#include "mp4dechwd_strmdec.h"
#include "mp4dechwd_generic.h"
#include "mp4dechwd_utils.h"
#include "mp4decdrv.h"
#include "mp4decapi.h"
#include "mp4decapi_internal.h"
#include "dwl.h"
#include "mp4dechwd_headers.h"
#include "deccfg.h"
#include "decapicommon.h"
#include "refbuffer.h"
#include "workaround.h"
#include "bqueue.h"
#include "tiledref.h"
#include "errorhandling.h"
#include "commonconfig.h"

#include "mp4debug.h"
#ifdef MP4_ASIC_TRACE
#include "mpeg4asicdbgtrace.h"
#endif

#ifdef MP4DEC_TRACE
#define MP4_API_TRC(str)    MP4DecTrace((str))
#else
#define MP4_API_TRC(str)
#endif

#ifndef HANTRO_OK
#define HANTRO_OK 0
#endif

#ifndef HANTRO_NOK
#define HANTRO_NOK 1
#endif

#define X170_DEC_TIMEOUT        0xFFU
#define X170_DEC_SYSTEM_ERROR   0xFEU
#define X170_DEC_HW_RESERVED    0xFDU

#define BUFFER_UNDEFINED        32

#define MP4DEC_UPDATE_POUTPUT \
    dec_cont->MbSetDesc.out_data.data_left = \
    DEC_STRM.p_strm_buff_start - dec_cont->MbSetDesc.out_data.strm_curr_pos; \
    (void) DWLmemcpy(output, &dec_cont->MbSetDesc.out_data, \
                             sizeof(MP4DecOutput))

#define NON_B_BUT_B_ALLOWED \
   !dec_container->Hdrs.low_delay && dec_container->VopDesc.vop_coding_type != BVOP
#define MP4_IS_FIELD_OUTPUT \
    dec_cont->Hdrs.interlaced && !dec_cont->pp_config_query.deinterlace
#define MP4_NON_PIPELINE_AND_B_PICTURE \
    ((!dec_cont->pp_config_query.pipeline_accepted || dec_cont->Hdrs.interlaced) \
    && dec_cont->VopDesc.vop_coding_type == BVOP)
#define MP4_SET_BOT_ADDR(idx) \
        if(dec_cont->dpb_mode == DEC_DPB_FRAME ) \
        { \
            dec_cont->pp_control.bottom_bus_luma = \
                MP4DecResolveBus(dec_cont, idx) + \
                (dec_cont->VopDesc.vop_width << 4); \
            dec_cont->pp_control.bottom_bus_chroma = \
                dec_cont->pp_control.bottom_bus_luma + \
                ((dec_cont->VopDesc.vop_width * \
                  dec_cont->VopDesc.vop_height) << 8); \
        } \
        else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) \
        { \
            u32 frame_size = (dec_cont->VopDesc.vop_width * \
                                    dec_cont->VopDesc. \
                                    vop_height) << 8; \
            dec_cont->pp_control.bottom_bus_luma = \
                MP4DecResolveBus(dec_cont, idx) + \
                frame_size/2; \
            dec_cont->pp_control.bottom_bus_chroma = \
                MP4DecResolveBus(dec_cont, idx) + \
                frame_size + frame_size/4; \
        }
#define MP4_SET_FIELD_DIMENSIONS \
        dec_cont->pp_control.inwidth = \
            dec_cont->pp_control.cropped_w = \
            dec_cont->VopDesc.vop_width << 4; \
        dec_cont->pp_control.inheight = \
            (((dec_cont->VopDesc.vop_height + 1) & ~1) / 2) << 4; \
        dec_cont->pp_control.cropped_h = \
            (dec_cont->VopDesc.vop_height << 4) / 2


void MP4RefreshRegs(DecContainer * dec_cont);
void MP4FlushRegs(DecContainer * dec_cont);
static u32 HandleVlcModeError(DecContainer * dec_cont, u32 pic_num);
static void HandleVopEnd(DecContainer * dec_cont);
static u32 RunDecoderAsic(DecContainer * dec_container, addr_t strm_bus_address);
static void MP4FillPicStruct(MP4DecPicture * picture,
                             DecContainer * dec_cont, u32 pic_index);
static u32 MP4SetRegs(DecContainer * dec_container, addr_t strm_bus_address);
static void MP4DecSetupDeinterlace(DecContainer * dec_cont);
static void MP4DecPrepareFieldProcessing(DecContainer * dec_cont, u32);
static void MP4DecParallelPP(DecContainer * dec_container, u32);
static void PPControl(DecContainer * dec_cont, u32 pipeline_off);
static void MP4SetIndexes(DecContainer * dec_cont);
static u32 MP4CheckFormatSupport(MP4DecStrmFmt strm_fmt);
/*static u32 MP4DecFilterDisable(DecContainer * dec_cont);*/
static void MP4DecFieldAndValidBuffer(MP4DecPicture * picture,
                                      DecContainer * dec_cont, u32* pic_index);
static void PPMultiBufferSetup(DecContainer * dec_cont, u32 pipeline_off);
static void MP4DecRunFullmode(DecContainer * dec_cont);
#ifdef USE_EXTERNAL_BUFFER
static void MP4SetExternalBufferInfo(MP4DecInst dec_inst);
#endif

#ifdef USE_OUTPUT_RELEASE
MP4DecRet MP4DecNextPicture_INTERNAL(MP4DecInst dec_inst, MP4DecPicture * picture,
                                     u32 end_of_stream);
#endif

#ifdef USE_OUTPUT_RELEASE
static void MP4EnterAbortState(DecContainer *dec_cont);
static void MP4ExistAbortState(DecContainer *dec_cont);
static void MP4EmptyBufferQueue(DecContainer *dec_cont);
#endif

/*------------------------------------------------------------------------------
       Version Information - DO NOT CHANGE!
------------------------------------------------------------------------------*/

#define MP4DEC_MAJOR_VERSION 1
#define MP4DEC_MINOR_VERSION 2

#define DEC_DPB_NOT_INITIALIZED      -1

/*------------------------------------------------------------------------------

    Function: MP4DecGetAPIVersion

        Functional description:
            Return version information of API

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

MP4DecApiVersion MP4DecGetAPIVersion() {
  MP4DecApiVersion ver;

  ver.major = MP4DEC_MAJOR_VERSION;
  ver.minor = MP4DEC_MINOR_VERSION;

  return (ver);
}

/*------------------------------------------------------------------------------

    Function: MP4DecGetBuild

        Functional description:
            Return build information of SW and HW

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

MP4DecBuild MP4DecGetBuild(void) {
  MP4DecBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_MPEG4_DEC);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_MPEG4_DEC);

  MP4_API_TRC("MP4DecGetBuild# OK\n");

  return build_info;
}

/*------------------------------------------------------------------------------

    Function: MP4DecDecode

        Functional description:
            Decode stream data. Calls StrmDec_Decode to do the actual decoding.

        Input:
            dec_inst     decoder instance
            input      pointer to input struct

        Outputs:
            output     pointer to output struct

        Returns:
            MP4DEC_NOT_INITIALIZED      decoder instance not initialized yet
            MP4DEC_PARAM_ERROR          invalid parameters

            MP4DEC_STRM_PROCESSED       stream buffer decoded
            MP4DEC_HDRS_RDY             headers decoded
            MP4DEC_DP_HDRS_RDY          headers decoded, data partitioned stream
            MP4DEC_PIC_DECODED          decoding of a picture finished
            MP4DEC_STRM_ERROR               serious error in decoding, no
                                            valid parameter sets available
                                            to decode picture data
            MP4DEC_VOS_END              video Object Sequence end marker
                                                dedected
            MP4DEC_HW_BUS_ERROR    decoder HW detected a bus error
            MP4DEC_SYSTEM_ERROR    wait for hardware has failed
            MP4DEC_MEMFAIL         decoder failed to allocate memory
            MP4DEC_DWL_ERROR   System wrapper failed to initialize
            MP4DEC_HW_TIMEOUT  HW timeout
            MP4DEC_HW_RESERVED HW could not be reserved

------------------------------------------------------------------------------*/

MP4DecRet MP4DecDecode(MP4DecInst dec_inst,
                       const MP4DecInput * input, MP4DecOutput * output) {
#define API_STOR ((DecContainer *)dec_inst)->ApiStorage
#define DEC_STRM ((DecContainer *)dec_inst)->StrmDesc
#define DEC_VOPD ((DecContainer *)dec_inst)->VopDesc

  DecContainer *dec_cont;
  MP4DecRet internal_ret;
  u32 i;
  u32 strm_dec_result;
  u32 asic_status;
  i32 ret = MP4DEC_OK;
  u32 error_concealment = HANTRO_FALSE;

  MP4_API_TRC("MP4DecDecode#\n");

  if(input == NULL || output == NULL || dec_inst == NULL) {
    MP4_API_TRC("MP44DecDecode# PARAM_ERROR\n");
    return MP4DEC_PARAM_ERROR;
  }

  dec_cont = ((DecContainer *) dec_inst);

  if(dec_cont->StrmStorage.unsupported_features_present) {
    return (MP4DEC_FORMAT_NOT_SUPPORTED);
  }

  /*
   *  Check if decoder is in an incorrect mode
   */
  if(API_STOR.DecStat == UNINIT) {

    MP4_API_TRC("MP4DecDecode: NOT_INITIALIZED\n");
    return MP4DEC_NOT_INITIALIZED;
  }

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort) {
    return (MP4DEC_ABORTED);
  }
#endif

  if(input->data_len == 0 ||
      input->data_len > DEC_X170_MAX_STREAM ||
      input->stream == NULL || input->stream_bus_address == 0) {
    MP4_API_TRC("MP44DecDecode# PARAM_ERROR\n");
    return MP4DEC_PARAM_ERROR;
  }
#ifdef USE_EXTERNAL_BUFFER
  /*
  *  Update stream structure
  */
  DEC_STRM.p_strm_buff_start = input->stream;
  DEC_STRM.strm_curr_pos = input->stream;
  DEC_STRM.bit_pos_in_word = 0;
  DEC_STRM.strm_buff_size = input->data_len;
  DEC_STRM.strm_buff_read_bits = 0;
#endif

  if(dec_cont->StrmStorage.num_buffers == 0 &&
      dec_cont->StrmStorage.custom_strm_headers) {
    internal_ret = MP4AllocateBuffers( dec_cont );
    if(internal_ret != MP4DEC_OK) {
      MP4_API_TRC("MP44DecDecode# MEMFAIL\n");
      return (internal_ret);
    }

#ifdef USE_EXTERNAL_BUFFER
    MP4SetExternalBufferInfo(dec_cont);
    dec_cont->buffer_index = 0;
    output->strm_curr_pos = dec_cont->StrmDesc.strm_curr_pos;
    output->strm_curr_bus_address = input->stream_bus_address +
                                    (dec_cont->StrmDesc.strm_curr_pos - dec_cont->StrmDesc.p_strm_buff_start);
    output->data_left = dec_cont->StrmDesc.strm_buff_size -
                        (output->strm_curr_pos - DEC_STRM.p_strm_buff_start);
#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->abort)
      return(MP4DEC_ABORTED);
    else
#endif
      return MP4DEC_WAITING_FOR_BUFFER;
#endif
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
        return MP4DEC_WAITING_FOR_BUFFER;
      }
#endif
      MP4FreeBuffers(dec_cont);
      internal_ret = MP4AllocateBuffers(dec_cont);
      if(internal_ret != MP4DEC_OK) {
        MP4_API_TRC("MP44DecDecode# MEMFAIL\n");
        return (internal_ret);
      }
      /* Headers ready now, mems allocated, decoding can start */
#ifndef USE_EXTERNAL_BUFFER
      API_STOR.DecStat = STREAMDECODING;
#endif
    }
  }

  if(input->enable_deblock) {
    API_STOR.disable_filter = 0;
  } else {
    API_STOR.disable_filter = 1;
  }

  /* Interlaced (and as such field DPB) not supported here */
  if(dec_cont->StrmStorage.custom_strm_headers)
    dec_cont->dpb_mode = DEC_DPB_FRAME;

#ifndef USE_EXTERNAL_BUFFER
  /*
  *  Update stream structure
  */
  DEC_STRM.p_strm_buff_start = input->stream;
  DEC_STRM.strm_curr_pos = input->stream;
  DEC_STRM.bit_pos_in_word = 0;
  DEC_STRM.strm_buff_size = input->data_len;
  DEC_STRM.strm_buff_read_bits = 0;
#endif

#ifdef _DEC_PP_USAGE
  dec_cont->StrmStorage.latest_id = input->pic_id;
#endif

  u32 status_tmp = dec_cont->StrmStorage.status;
  u32 start_code_loss_tmp = dec_cont->StrmStorage.start_code_loss;
  u32 vp_mb_number_tmp = dec_cont->StrmStorage.vp_mb_number;
  u32 vp_num_mbs_tmp = dec_cont->StrmStorage.vp_num_mbs;
  u32 num_err_mbs_tmp = dec_cont->StrmStorage.num_err_mbs;
  u32 vop_rounding_type_tmp = dec_cont->VopDesc.vop_rounding_type;
  u32 valid_vop_header_tmp = dec_cont->StrmStorage.valid_vop_header;

  do {
    MP4DEC_API_DEBUG(("Start Decode\n"));
    /* run SW if HW is not in the middle of processing a picture (in VLC
     * mode, indicated as HW_VOP_STARTED decoder status) */
#ifdef USE_EXTERNAL_BUFFER
    if(API_STOR.DecStat == HEADERSDECODED) {
      API_STOR.DecStat = STREAMDECODING;
      if(!dec_cont->no_reallocation) {
        dec_cont->buffer_index = 0;
        dec_cont->no_reallocation = 1;
        ret =  MP4DEC_WAITING_FOR_BUFFER;
      }
    } else if(API_STOR.DecStat != HW_VOP_STARTED)
#else
    if(API_STOR.DecStat != HW_VOP_STARTED)
#endif
    {
      strm_dec_result = StrmDec_Decode(dec_cont);
      switch (strm_dec_result) {
      case DEC_ERROR:
        /*
         *          Nothing finished, decoder not ready, cannot continue decoding
         *          before some headers received
         */
        dec_cont->StrmStorage.strm_dec_ready = HANTRO_FALSE;
        dec_cont->StrmStorage.status = STATE_SYNC_LOST;
        API_STOR.DecStat = INITIALIZED;
        if(dec_cont->StrmStorage.unsupported_features_present) {
          dec_cont->StrmStorage.unsupported_features_present = 0;
          ret = MP4DEC_STRM_NOT_SUPPORTED;
        } else {
          ret = MP4DEC_STRM_ERROR;
        }
        break;

      case DEC_ERROR_BUF_NOT_EMPTY:
        /*
         *          same as DEC_ERROR but stream buffer not empty
         */
        MP4DEC_UPDATE_POUTPUT;
        if(dec_cont->StrmStorage.unsupported_features_present) {
          dec_cont->StrmStorage.unsupported_features_present = 0;
          ret = MP4DEC_STRM_NOT_SUPPORTED;
        } else {
          ret = MP4DEC_STRM_ERROR;
        }
        break;

      case DEC_END_OF_STREAM:
      /*
       *          nothing finished, no data left in stream
       */
      /* fallthrough */
      case DEC_RDY:
        /*
         *          everything ok but no VOP finished
         */
        ret = MP4DEC_STRM_PROCESSED;
        break;

      case DEC_VOP_RDY_BUF_NOT_EMPTY:
      case DEC_VOP_RDY:
        /*
         * Vop finished and stream buffer could be empty or not. This
         * is RLC mode processing.
         */
        if(!DEC_VOPD.vop_coded && !dec_cont->packed_mode) {
          /* everything to not coded */
          MP4NotCodedVop(dec_cont);
        }
#ifdef MP4_ASIC_TRACE
        {
          u32 halves = 0;
          u32 num_addr = 0;

          WriteAsicCtrl(dec_cont);
          WriteAsicRlc(dec_cont, &halves, &num_addr);
          writePictureCtrl(dec_cont, &halves, &num_addr);

        }
#endif
        dec_cont->MbSetDesc.odd_rlc_vp = dec_cont->MbSetDesc.odd_rlc = 0;
        dec_cont->StrmStorage.vp_mb_number = 0;
        dec_cont->StrmStorage.vp_num_mbs = 0;
        dec_cont->StrmStorage.valid_vop_header = HANTRO_FALSE;

        /* reset macro block error status */
        for(i = 0; i < dec_cont->VopDesc.total_mb_in_vop; i++) {
          dec_cont->MBDesc[i].error_status = 0;
        }

        {
        u32 work_out_prev_tmp = dec_cont->StrmStorage.work_out_prev;
        dec_cont->StrmStorage.work_out_prev =
          dec_cont->StrmStorage.work_out;
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp_instance == NULL) {
          dec_cont->StrmStorage.work_out = BqueueNext2(
                                             &dec_cont->StrmStorage.bq,
                                             dec_cont->StrmStorage.work0,
                                             dec_cont->StrmStorage.work1,
                                             BQUEUE_UNUSED,
                                             0 );
          if(dec_cont->StrmStorage.work_out == (u32)0xFFFFFFFFU) {
            if (dec_cont->abort)
              return MP4DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
            else {
              output->strm_curr_pos = input->stream;
              output->strm_curr_bus_address = input->stream_bus_address;
              output->data_left = input->data_len;
              API_STOR.DecStat = STREAMDECODING;
              dec_cont->StrmStorage.work_out = INVALID_ANCHOR_PICTURE;
              dec_cont->StrmStorage.work_out_prev = work_out_prev_tmp;
              dec_cont->StrmStorage.status = status_tmp;
              dec_cont->StrmStorage.start_code_loss = start_code_loss_tmp;
              dec_cont->StrmStorage.vp_mb_number = vp_mb_number_tmp;
              dec_cont->StrmStorage.vp_num_mbs = vp_num_mbs_tmp;
              dec_cont->StrmStorage.num_err_mbs = num_err_mbs_tmp;
              dec_cont->VopDesc.vop_rounding_type = vop_rounding_type_tmp;
              dec_cont->StrmStorage.valid_vop_header = valid_vop_header_tmp;

              if(dec_cont->rlc_mode) {
              /* Reset pointers of the SW/HW-shared buffers */
                dec_cont->MbSetDesc.p_rlc_data_curr_addr =
                  dec_cont->MbSetDesc.p_rlc_data_addr;
                dec_cont->MbSetDesc.p_rlc_data_vp_addr =
                  dec_cont->MbSetDesc.p_rlc_data_addr;
                dec_cont->MbSetDesc.odd_rlc_vp = dec_cont->MbSetDesc.odd_rlc;
              }
              dec_cont->same_vop_header = 1;
              return MP4DEC_NO_DECODING_BUFFER;
            }
#endif
          }
          else if (dec_cont->same_vop_header) {
            dec_cont->same_vop_header = 0;
          }
          dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].first_show = 1;
        } else {
          dec_cont->StrmStorage.work_out = BqueueNext(
                                             &dec_cont->StrmStorage.bq,
                                             dec_cont->StrmStorage.work0,
                                             dec_cont->StrmStorage.work1,
                                             BQUEUE_UNUSED,
                                             0 );
        }
#else
        dec_cont->StrmStorage.work_out = BqueueNext(
                                           &dec_cont->StrmStorage.bq,
                                           dec_cont->StrmStorage.work0,
                                           dec_cont->StrmStorage.work1,
                                           BQUEUE_UNUSED,
                                           0 );
#endif
        if (dec_cont->pp_enabled) {
          dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data = InputQueueGetBuffer(dec_cont->pp_buffer_queue, 1);
        }

        /* reset macro block error status */
        for(i = 0; i < dec_cont->VopDesc.total_mb_in_vop; i++) {
          dec_cont->MBDesc[i].error_status = 0;
        }

        if (DEC_VOPD.vop_coded || !dec_cont->packed_mode) {
          asic_status = RunDecoderAsic(dec_cont, 0);
          dec_cont->VopDesc.vop_number++;
          dec_cont->VopDesc.vop_number_in_seq++;

          MP4DecBufferPicture(dec_cont, input->pic_id,
                              dec_cont->VopDesc.vop_coding_type,
                              dec_cont->StrmStorage.num_err_mbs);

          if(dec_cont->VopDesc.vop_coding_type != BVOP) {
            if(dec_cont->Hdrs.low_delay == 0) {
              dec_cont->StrmStorage.work1 =
                dec_cont->StrmStorage.work0;
            }
            dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work_out;
          }

          if(asic_status == X170_DEC_TIMEOUT) {
            error_concealment = HANTRO_TRUE;
            ret = MP4DEC_HW_TIMEOUT;
          } else if(asic_status == X170_DEC_SYSTEM_ERROR) {
            error_concealment = HANTRO_TRUE;
            ret = MP4DEC_SYSTEM_ERROR;
          } else if(asic_status == X170_DEC_HW_RESERVED) {
            error_concealment = HANTRO_TRUE;
            ret = MP4DEC_HW_RESERVED;
          } else if(asic_status & MP4_DEC_X170_IRQ_BUS_ERROR) {
            error_concealment = HANTRO_TRUE;
            ret = MP4DEC_HW_BUS_ERROR;
          } else if((asic_status & MP4_DEC_X170_IRQ_TIMEOUT) ||
                    (asic_status & DEC_8190_IRQ_ABORT)) {
            error_concealment = HANTRO_TRUE;
            ret = MP4DEC_HW_TIMEOUT;
          } else {
            ret = MP4DEC_PIC_DECODED;
            if( dec_cont->VopDesc.vop_coding_type == IVOP )
              dec_cont->StrmStorage.picture_broken = HANTRO_FALSE;
          }

          if(strm_dec_result != DEC_OUT_OF_BUFFER &&
              !dec_cont->Hdrs.data_partitioned &&
              dec_cont->StrmStorage.status == STATE_OK) {
            dec_cont->rlc_mode = 0;
          }

        } else { /* packed mode N-VOP */
          ret = IS_END_OF_STREAM(dec_cont) ?
                MP4DEC_STRM_PROCESSED : 0;
        }

        /* copy output parameters for this PIC (excluding stream pos) */
        dec_cont->MbSetDesc.out_data.strm_curr_pos =
          output->strm_curr_pos;
        MP4DEC_UPDATE_POUTPUT;
        }
        break;

      case DEC_VOP_SUPRISE_B:

        if(MP4DecBFrameSupport(dec_cont)) {
          internal_ret = MP4DecAllocExtraBPic(dec_cont);
          if(internal_ret != MP4DEC_OK) {
            MP4_API_TRC
            ("MP44DecDecode# MEMFAIL MP4DecAllocExtraBPic\n");
            return (internal_ret);
          }
          HandleVopEnd(dec_cont);

          dec_cont->StrmStorage.valid_vop_header = HANTRO_FALSE;

          MP4DecBufferPicture(dec_cont, input->pic_id,
                              dec_cont->VopDesc.vop_coding_type, 0);

          dec_cont->Hdrs.low_delay = 0;
          dec_cont->StrmStorage.work1 = dec_cont->StrmStorage.work0;
          dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work_out;
          dec_cont->VopDesc.vop_number++;
          dec_cont->VopDesc.vop_number_in_seq++;

          ret = MP4DEC_PIC_DECODED;
          API_STOR.DecStat = INITIALIZED;
          MP4DEC_UPDATE_POUTPUT;
        } else {
          /* B frames *not* supported */
          error_concealment = HANTRO_TRUE;
          ret = HandleVlcModeError(dec_cont, input->pic_id);
        }
        break;

      case DEC_VOP_HDR_RDY:

#if 0
        /* if h263 stream and the first frame is not intra */
        if(dec_cont->StrmStorage.short_video &&
            !dec_cont->VopDesc.vop_number &&
            dec_cont->VopDesc.vop_coding_type == PVOP) {
          dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work_out;
        }
#endif
        /* if type inter predicted and no reference -> error */
        if(( ( dec_cont->VopDesc.vop_coding_type == PVOP ||
               (dec_cont->VopDesc.vop_coding_type == IVOP &&
                !dec_cont->VopDesc.vop_coded) ) &&
             dec_cont->StrmStorage.work0 == INVALID_ANCHOR_PICTURE) ||
            (dec_cont->VopDesc.vop_coding_type == BVOP &&
             (dec_cont->StrmStorage.work0 == INVALID_ANCHOR_PICTURE ||
              (dec_cont->StrmStorage.work1 == INVALID_ANCHOR_PICTURE &&
               dec_cont->Hdrs.closed_gov == 0) ||
              dec_cont->StrmStorage.skip_b ||
              input->skip_non_reference )) ||
            (dec_cont->VopDesc.vop_coding_type == PVOP &&
             dec_cont->StrmStorage.picture_broken &&
             dec_cont->StrmStorage.intra_freeze) ) {
          if(dec_cont->StrmStorage.skip_b ||
              input->skip_non_reference) {
            MP4_API_TRC("MP4DecDecode# MP4DEC_NONREF_PIC_SKIPPED\n");
          }
          error_concealment = HANTRO_TRUE;
          ret = HandleVlcModeError(dec_cont, input->pic_id);
        } else {
          API_STOR.DecStat = HW_VOP_STARTED;
        }
        break;

      case DEC_VOP_HDR_RDY_ERROR:
        error_concealment = HANTRO_TRUE;
        ret = HandleVlcModeError(dec_cont, input->pic_id);
        /* copy output parameters for this PIC */
        MP4DEC_UPDATE_POUTPUT;
        break;

      case DEC_HDRS_RDY_BUF_NOT_EMPTY:
      case DEC_HDRS_RDY:

        internal_ret = MP4DecCheckSupport(dec_cont);
        if(internal_ret != MP4DEC_OK) {
          dec_cont->StrmStorage.strm_dec_ready = HANTRO_FALSE;
          dec_cont->StrmStorage.status = STATE_SYNC_LOST;
          dec_cont->Hdrs.data_partitioned = HANTRO_FALSE;
          API_STOR.DecStat = INITIALIZED;
          return internal_ret;
        }
        /*
         *          Either Vol header decoded or short video source format
         *          determined. Stream buffer could be empty or not
         */

        if(dec_cont->ApiStorage.first_headers) {
          /*dec_cont->ApiStorage.first_headers = 0;*/

          SetDecRegister(dec_cont->mp4_regs, HWIF_PIC_MB_WIDTH,
                         dec_cont->VopDesc.vop_width);
          SetDecRegister(dec_cont->mp4_regs, HWIF_PIC_MB_HEIGHT_P,
                         dec_cont->VopDesc.vop_height);
          SetDecRegister(dec_cont->mp4_regs, HWIF_PIC_MB_H_EXT,
                         dec_cont->VopDesc.vop_height >> 8);
          SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_MODE,
                         dec_cont->StrmStorage.short_video +
                         MP4_DEC_X170_MODE_MPEG4);

          SetConformanceFlags( dec_cont );

          if( dec_cont->StrmStorage.custom_overfill) {
            SetDecRegister(dec_cont->mp4_regs, HWIF_MB_WIDTH_OFF,
                           (dec_cont->StrmStorage.video_object_layer_width & 0xF));
            SetDecRegister(dec_cont->mp4_regs, HWIF_MB_HEIGHT_OFF,
                           (dec_cont->StrmStorage.video_object_layer_height & 0xF));
          }

          if( dec_cont->ref_buf_support ) {
            RefbuInit( &dec_cont->ref_buffer_ctrl, MP4_DEC_X170_MODE_MPEG4,
                       dec_cont->VopDesc.vop_width,
                       dec_cont->VopDesc.vop_height,
                       dec_cont->ref_buf_support );
          }
        }

        /* Initialize DPB mode */
        if( dec_cont->Hdrs.interlaced &&
            dec_cont->allow_dpb_field_ordering )
          dec_cont->dpb_mode = DEC_DPB_INTERLACED_FIELD;
        else
          dec_cont->dpb_mode = DEC_DPB_FRAME;

        /* Initialize tiled mode */
        if( dec_cont->tiled_mode_support) {
          /* Check mode validity */
          if(DecCheckTiledMode( dec_cont->tiled_mode_support,
                                dec_cont->dpb_mode,
                                dec_cont->Hdrs.interlaced ) !=
              HANTRO_OK ) {
            MP4_API_TRC("AvsDecDecode# ERROR: DPB mode does not "\
                        "support tiled reference pictures");
            return MP4DEC_PARAM_ERROR;
          }
        }

        API_STOR.DecStat = HEADERSDECODED;
        dec_cont->VopDesc.vop_number_in_seq = 0;
        dec_cont->StrmStorage.work_out_prev =
          dec_cont->StrmStorage.work_out = 0;
        dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work1 =
                                        INVALID_ANCHOR_PICTURE;

        if(dec_cont->StrmStorage.short_video)
          dec_cont->Hdrs.low_delay = 1;

        if(!dec_cont->Hdrs.data_partitioned) {
          dec_cont->rlc_mode = 0;
#ifdef USE_EXTERNAL_BUFFER
          MP4SetExternalBufferInfo(dec_cont);
          if (dec_cont->no_reallocation &&
              (!dec_cont->use_adaptive_buffers ||
               (dec_cont->use_adaptive_buffers &&
                dec_cont->tot_buffers + dec_cont->n_guard_size <= dec_cont->tot_buffers_added)))
            ret = MP4DEC_STRM_PROCESSED;
          else
#endif
          {
#ifdef USE_OUTPUT_RELEASE
            FifoPush(dec_cont->fifo_display, -2, FIFO_EXCEPTION_DISABLE);
#endif
            ret = MP4DEC_HDRS_RDY;
          }
          MP4DEC_UPDATE_POUTPUT;
        } else {
          dec_cont->rlc_mode = 1;
#ifdef USE_EXTERNAL_BUFFER
          MP4SetExternalBufferInfo(dec_cont);
          if (dec_cont->no_reallocation &&
              (!dec_cont->use_adaptive_buffers ||
               (dec_cont->use_adaptive_buffers &&
                dec_cont->tot_buffers + dec_cont->n_guard_size <= dec_cont->tot_buffers_added)))
            ret = MP4DEC_STRM_PROCESSED;
          else
#endif
          {
#ifdef USE_OUTPUT_RELEASE
            FifoPush(dec_cont->fifo_display, -2, FIFO_EXCEPTION_DISABLE);
#endif
            ret = MP4DEC_DP_HDRS_RDY;
          }
          MP4DEC_UPDATE_POUTPUT;
        }
        MP4DEC_API_DEBUG(("HDRS_RDY\n"));

        break;
      case DEC_VOS_END:
        /*
         *          Vos end code encountered, stopping.
         */
        ret = MP4DEC_VOS_END;
        break;

      default:
        dec_cont->StrmStorage.strm_dec_ready = HANTRO_FALSE;
        dec_cont->StrmStorage.status = STATE_SYNC_LOST;
        API_STOR.DecStat = INITIALIZED;
        MP4DEC_API_DEBUG(("entry:default\n"));
        ret = MP4DEC_STRM_ERROR;
        break;
      }
    }
    /* VLC mode */
    if(API_STOR.DecStat == HW_VOP_STARTED) {
      if(DEC_VOPD.vop_coded) {
        if(!dec_cont->asic_running) {
          MP4SetIndexes(dec_cont);
#ifdef USE_OUTPUT_RELEASE
          if(dec_cont->StrmStorage.work_out == (u32)0xFFFFFFFFU) {
            if (dec_cont->abort)
              return MP4DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
            else {
              output->strm_curr_pos = input->stream;
              output->strm_curr_bus_address = input->stream_bus_address;
              output->data_left = input->data_len;
              API_STOR.DecStat = STREAMDECODING;
              dec_cont->StrmStorage.work_out = INVALID_ANCHOR_PICTURE;
              dec_cont->StrmDesc.strm_curr_pos = input->stream;
              dec_cont->StrmStorage.status = status_tmp;
              dec_cont->StrmStorage.start_code_loss = start_code_loss_tmp;
              dec_cont->StrmStorage.vp_mb_number = vp_mb_number_tmp;
              dec_cont->StrmStorage.vp_num_mbs = vp_num_mbs_tmp;
              dec_cont->StrmStorage.num_err_mbs = num_err_mbs_tmp;
              dec_cont->VopDesc.vop_rounding_type = vop_rounding_type_tmp;
              dec_cont->StrmStorage.valid_vop_header = valid_vop_header_tmp;
              ret = MP4DEC_NO_DECODING_BUFFER;
              dec_cont->same_vop_header = 1;
              break;
            }
#endif
          }
          else if (dec_cont->same_vop_header) {
            dec_cont->same_vop_header = 0;
          }
#endif
          if( dec_cont->workarounds.mpeg.stuffing ) {
            PrepareStuffingWorkaround( (u8*)MP4DecResolveVirtual
                                       (dec_cont, dec_cont->StrmStorage.work_out),
                                       dec_cont->VopDesc.vop_width,
                                       dec_cont->VopDesc.vop_height );
          }

          if (dec_cont->StrmStorage.partial_freeze) {
            PreparePartialFreeze( (u8*)MP4DecResolveVirtual
                                  (dec_cont, dec_cont->StrmStorage.work_out),
                                  dec_cont->VopDesc.vop_width,
                                  dec_cont->VopDesc.vop_height);
          }
        }

        asic_status = RunDecoderAsic(dec_cont, input->stream_bus_address);

        /* Translate buffer empty IRQ to error in divx3 */
        if((asic_status & MP4_DEC_X170_IRQ_BUFFER_EMPTY) &&
            dec_cont->StrmStorage.custom_strm_headers ) {
          asic_status = asic_status & ~MP4_DEC_X170_IRQ_BUFFER_EMPTY;
          asic_status = asic_status | MP4_DEC_X170_IRQ_STREAM_ERROR;

          /* reset HW */
          SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_IRQ_STAT, 0);
          SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_IRQ, 0);
          SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_E, 0);

          DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                       dec_cont->mp4_regs[1]);

          /* End PP co-operation */
          if(dec_cont->pp_control.pp_status == DECPP_RUNNING) {
            if(dec_cont->pp_instance != NULL)
              dec_cont->PPEndCallback(dec_cont->pp_instance);
            dec_cont->pp_control.pp_status = DECPP_PIC_READY;
          }

          dec_cont->asic_running = 0;

          DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);
        }

        if(asic_status == X170_DEC_TIMEOUT) {
          ret = MP4DEC_HW_TIMEOUT;
        } else if(asic_status == X170_DEC_SYSTEM_ERROR) {
          ret = MP4DEC_SYSTEM_ERROR;
        } else if(asic_status == X170_DEC_HW_RESERVED) {
          ret = MP4DEC_HW_RESERVED;
        } else if(asic_status & MP4_DEC_X170_IRQ_BUS_ERROR) {
          ret = MP4DEC_HW_BUS_ERROR;
        } else if(asic_status & MP4_DEC_X170_IRQ_STREAM_ERROR ||
                  asic_status & MP4_DEC_X170_IRQ_TIMEOUT) {
          if (!dec_cont->StrmStorage.partial_freeze ||
              !ProcessPartialFreeze(
                (u8*)MP4DecResolveVirtual(dec_cont, dec_cont->StrmStorage.work_out),
                dec_cont->StrmStorage.work0 != INVALID_ANCHOR_PICTURE ?
                (u8*)MP4DecResolveVirtual(dec_cont, dec_cont->StrmStorage.work0) : NULL,
                dec_cont->VopDesc.vop_width,
                dec_cont->VopDesc.vop_height,
                dec_cont->StrmStorage.partial_freeze == 1)) {
            /* picture freeze concealment disabled -> need to allocate
             * memory for rlc mode buffers -> return MEMORY_REALLOCATION and
             * let the application decide. Continue from start of the
             * current stream buffer with SW */
            if(asic_status & MP4_DEC_X170_IRQ_STREAM_ERROR) {
              MP4DEC_API_DEBUG(("IRQ:STREAM ERROR IN HW\n"));

              if( dec_cont->workarounds.mpeg.stuffing ) {
                u8 *p_ref_pic = NULL;

                if(dec_cont->VopDesc.vop_number_in_seq > 0)
                  p_ref_pic = (u8*)MP4DecResolveVirtual(dec_cont, dec_cont->StrmStorage.work0);
                else
                  p_ref_pic = (u8*)MP4DecResolveVirtual(dec_cont, dec_cont->StrmStorage.work_out);

                /* We process stuffing workaround. If everything is OK
                 * then mask interrupt as DEC_RDY and not STREAM_ERROR */
                if(ProcessStuffingWorkaround( (u8*)MP4DecResolveVirtual
                                              (dec_cont, dec_cont->StrmStorage.work_out),
                                              p_ref_pic, dec_cont->VopDesc.vop_width,
                                              dec_cont->VopDesc.vop_height ) == HANTRO_TRUE) {
                  asic_status &= ~MP4_DEC_X170_IRQ_STREAM_ERROR;
                  asic_status |= MP4_DEC_X170_IRQ_DEC_RDY;
                  error_concealment = HANTRO_FALSE;
                }
              }
            } else {
              MP4DEC_API_DEBUG(("IRQ: HW TIMEOUT\n"));
            }

            if (dec_cont->pp_enabled)
              InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data->virtual_address);
            ret = HandleVlcModeError(dec_cont, input->pic_id);
            error_concealment = HANTRO_TRUE;
            MP4DEC_UPDATE_POUTPUT;
          } else {
            asic_status &= ~MP4_DEC_X170_IRQ_STREAM_ERROR;
            asic_status &= ~MP4_DEC_X170_IRQ_TIMEOUT;
            asic_status |= MP4_DEC_X170_IRQ_DEC_RDY;
            error_concealment = HANTRO_FALSE;
          }
        } else if(asic_status & MP4_DEC_X170_IRQ_BUFFER_EMPTY) {
          ret = MP4DEC_BUF_EMPTY;
        } else if(asic_status & MP4_DEC_X170_IRQ_DEC_RDY) {
          /* Nothing here */
        } else {
          ASSERT(0);
        }

        /* HW finished decoding a picture */
        if(asic_status & MP4_DEC_X170_IRQ_DEC_RDY) {
          dec_cont->VopDesc.vop_number++;
          dec_cont->VopDesc.vop_number_in_seq++;

          HandleVopEnd(dec_cont);

          dec_cont->StrmStorage.valid_vop_header = HANTRO_FALSE;
          if( dec_cont->StrmStorage.skip_b )
            dec_cont->StrmStorage.skip_b--;

          MP4DecBufferPicture(dec_cont, input->pic_id,
                              dec_cont->VopDesc.vop_coding_type, 0);

          ret = MP4DEC_PIC_DECODED;
          if( dec_cont->VopDesc.vop_coding_type == IVOP )
            dec_cont->StrmStorage.picture_broken = HANTRO_FALSE;

        }

        if(ret != MP4DEC_STRM_PROCESSED && ret != MP4DEC_BUF_EMPTY) {
          API_STOR.DecStat = STREAMDECODING;
        }

        if(ret == MP4DEC_PIC_RDY || ret == MP4DEC_STRM_PROCESSED || ret == MP4DEC_BUF_EMPTY) {
          /* copy output parameters for this PIC (excluding stream pos) */
          dec_cont->MbSetDesc.out_data.strm_curr_pos =
            output->strm_curr_pos;
          MP4DEC_UPDATE_POUTPUT;
        }

        if(dec_cont->VopDesc.vop_coding_type != BVOP &&
            ret != MP4DEC_STRM_PROCESSED &&
            ret != MP4DEC_BUF_EMPTY &&
            !(dec_cont->StrmStorage.sorenson_spark &&
              dec_cont->StrmStorage.disposable)) {
          if(dec_cont->Hdrs.low_delay == 0) {
            dec_cont->StrmStorage.work1 =
              dec_cont->StrmStorage.work0;
          }
          dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work_out;

        }
      } else if (!dec_cont->packed_mode) { /* not-coded VOP */
        MP4DEC_API_DEBUG(("\n\nNot CODED VOP\n"));

        dec_cont->StrmStorage.valid_vop_header = HANTRO_FALSE;
        /* stuffing not read for not coded VOPs -> skip */
        dec_cont->StrmDesc.strm_curr_pos++;

        /* rotate picture indexes for current output */
        MP4SetIndexes(dec_cont);
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->StrmStorage.work_out == (u32)0xFFFFFFFFU) {
          if (dec_cont->abort)
              return MP4DEC_ABORTED;
#ifdef GET_FREE_BUFFER_NON_BLOCK
          else {
            output->strm_curr_pos = input->stream;
            output->strm_curr_bus_address = input->stream_bus_address;
            output->data_left = input->data_len;
            API_STOR.DecStat = STREAMDECODING;
            dec_cont->StrmStorage.work_out = INVALID_ANCHOR_PICTURE;
            dec_cont->StrmStorage.status = status_tmp;
            dec_cont->StrmStorage.start_code_loss = start_code_loss_tmp;
            dec_cont->StrmStorage.vp_mb_number = vp_mb_number_tmp;
            dec_cont->StrmStorage.vp_num_mbs = vp_num_mbs_tmp;
            dec_cont->StrmStorage.num_err_mbs = num_err_mbs_tmp;
            dec_cont->VopDesc.vop_rounding_type = vop_rounding_type_tmp;
            dec_cont->StrmStorage.valid_vop_header = valid_vop_header_tmp;
            dec_cont->same_vop_header = 1;
            return MP4DEC_NO_DECODING_BUFFER;
          }
#endif
        }
        else if (dec_cont->same_vop_header) {
          dec_cont->same_vop_header = 0;
        }
#endif
        /* copy data pointers from prev output as not coded pic
         * out */
        /*
        MP4DecChangeDataIndex(dec_cont,
                              dec_cont->StrmStorage.work_out,
                              dec_cont->StrmStorage.work0);
                              */

        if(dec_cont->pp_instance != NULL) {
          i32 id = dec_cont->StrmStorage.work_out;

          dec_cont->StrmStorage.p_pic_buf[id].send_to_pp = 1;

          PPControl(dec_cont, 1);

          /* End PP co-operation */
          if(dec_cont->pp_control.pp_status == DECPP_RUNNING) {
            MP4DEC_API_DEBUG(("Wait for PP\n"));
            dec_cont->PPEndCallback(dec_cont->pp_instance);

            dec_cont->pp_control.pp_status = DECPP_PIC_READY;
            /* Same data is used for NVOP, send the VOP to PP again */
            dec_cont->StrmStorage.p_pic_buf[id].send_to_pp = 1;
          }
        }

        HandleVopEnd(dec_cont);

        if( dec_cont->StrmStorage.skip_b )
          dec_cont->StrmStorage.skip_b--;

        MP4DecBufferPicture(dec_cont, input->pic_id,
                            dec_cont->VopDesc.vop_coding_type, 0);

        dec_cont->VopDesc.vop_number++;
        dec_cont->VopDesc.vop_number_in_seq++;

        if(dec_cont->VopDesc.vop_coding_type != BVOP) {
          /*     MP4DecChangeDataIndex( dec_cont,
           * dec_cont->StrmStorage.work0,
           * dec_cont->StrmStorage.work1); */
          if(dec_cont->Hdrs.low_delay == 0) {
            dec_cont->StrmStorage.work1 =
              dec_cont->StrmStorage.work0;
          }
          dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work_out;
        }

        dec_cont->StrmStorage.previous_not_coded = 1;

        ret = MP4DEC_PIC_DECODED;

        API_STOR.DecStat = STREAMDECODING;

        /* copy output parameters for this PIC */
        MP4DEC_UPDATE_POUTPUT;
      } else { /* not coded-VOP, custom codec */
        /* Report to application that a picture was decoded and skipped
         * without output. */
        ret = MP4DEC_NONREF_PIC_SKIPPED;
        API_STOR.DecStat = STREAMDECODING;
        /* TODO: what needs to be done to send previous ref to PP */
      }
    }
  } while(ret == 0);

  StrmDec_ProcessPacketFooter( dec_cont );

  if( error_concealment && dec_cont->VopDesc.vop_coding_type != BVOP ) {
    dec_cont->StrmStorage.picture_broken = HANTRO_TRUE;
  }

  output->strm_curr_pos = dec_cont->StrmDesc.strm_curr_pos;
  output->strm_curr_bus_address = input->stream_bus_address +
                                  (dec_cont->StrmDesc.strm_curr_pos - dec_cont->StrmDesc.p_strm_buff_start);
  output->data_left = dec_cont->StrmDesc.strm_buff_size -
                      (output->strm_curr_pos - DEC_STRM.p_strm_buff_start);

  if(dec_cont->Hdrs.data_partitioned)
    dec_cont->rlc_mode = 1;

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp_instance == NULL) {
    u32 tmpret;
    MP4DecPicture output;
    /*if(ret == MP4DEC_PIC_DECODED)*/ {
      do {
        tmpret = MP4DecNextPicture_INTERNAL(dec_cont, &output, 0);
        if(tmpret == MP4DEC_ABORTED)
          return (MP4DEC_ABORTED);
      } while( tmpret == MP4DEC_PIC_RDY);
    }
  }
#endif
  MP4_API_TRC("MP4DecDecode: Exit\n");
#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort)
    return(MP4DEC_ABORTED);
  else
#endif
    return ((MP4DecRet) ret);

#undef API_STOR
#undef DEC_STRM
#undef DEC_VOPD

}

/*------------------------------------------------------------------------------

    Function: MP4DecSetInfo()

        Functional description:
            Provide external information to decoder. Used for some custom
            streams which do not contain all necessary information in the
            elementary bitstream.

        Inputs:
            dec_inst        pointer to initialized instance
            width           frame width in pixels
            height          frame height in pixels

        Outputs:

        Returns:
            MP4DEC_OK
                successfully updated info

------------------------------------------------------------------------------*/
MP4DecRet MP4DecSetInfo(MP4DecInst * dec_inst, const u32 width,
                        const u32 height ) {

  DecContainer *dec_cont;

  MP4_API_TRC("MP4DecSetInfo#\n");

  if(dec_inst == NULL) {
    return MP4DEC_PARAM_ERROR;
  }

  dec_cont = ((DecContainer *) dec_inst);
  SetCustomInfo( dec_cont, width, height );

  MP4_API_TRC("MP4DecSetInfo: OK\n");

  return MP4DEC_OK;
}

/*------------------------------------------------------------------------------

    Function: MP4DecInit()

        Functional description:
            Initialize decoder software. Function reserves memory for the
            decoder instance.

        Inputs:
            strm_fmt         specifies input stream format
                                (MPEG4, Sorenson Spark)
            error_handling
                            Flag to determine which error concealment method to use.

        Outputs:
            dec_inst         pointer to initialized instance is stored here

        Returns:
            MP4DEC_OK
                successfully initialized the instance
            MP4DEC_PARAM_ERROR
                invalid parameters
            MP4DEC_MEMFAIL
                memory allocation failed
            MP4DEC_UNSUPPORTED_FORMAT
                hw doesn't support the initialized format
            MP4DEC_DWL_ERROR
                error initializing the system interface

------------------------------------------------------------------------------*/
MP4DecRet MP4DecInit(MP4DecInst * dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                     const void *dwl,
#endif
                     MP4DecStrmFmt strm_fmt,
                     enum DecErrorHandling error_handling,
                     u32 num_frame_buffers,
                     enum DecDpbFlags dpb_flags,
                     u32 use_adaptive_buffers,
                     u32 n_guard_size,
                     struct DecDownscaleCfg *dscale_cfg ) {
  /*@null@ */ DecContainer *dec_cont;
#ifndef USE_EXTERNAL_BUFFER
  /*@null@ */ const void *dwl;
#endif
  u32 i;
#ifndef USE_EXTERNAL_BUFFER
  struct DWLInitParam dwl_init;
#endif
  DWLHwConfig config;
  u32 reference_frame_format;

  MP4_API_TRC("MP4DecInit#\n");

  /* check that right shift on negative numbers is performed signed */
  /*lint -save -e* following check causes multiple lint messages */
#if (((-1) >> 1) != (-1))
#error Right bit-shifting (>>) does not preserve the sign
#endif
  /*lint -restore */

  if(dec_inst == NULL) {
    MP4_API_TRC("MPEG4DecInit# ERROR: dec_inst == NULL\n");
    return (MP4DEC_PARAM_ERROR);
  }

  *dec_inst = NULL; /* return NULL for any error */

  if(MP4CheckFormatSupport(strm_fmt)) {
    MP4_API_TRC("MPEG4DecInit# ERROR: Format not supported!\n");
    return (MP4DEC_FORMAT_NOT_SUPPORTED);
  }
#ifndef USE_EXTERNAL_BUFFER
  dwl_init.client_type = DWL_CLIENT_TYPE_MPEG4_DEC;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    MP4_API_TRC("MPEG4DecInit# ERROR: DWL Init failed\n");
    return (MP4DEC_DWL_ERROR);
  }
#endif

  dec_cont = (DecContainer *) DWLmalloc(sizeof(DecContainer));

  if(dec_cont == NULL) {
#ifndef USE_EXTERNAL_BUFFER
    (void) DWLRelease(dwl);
#endif
    MP4_API_TRC("MPEG4DecInit# ERROR: Memory allocation failed\n");
    return (MP4DEC_MEMFAIL);
  }

  /* set everything initially zero */
  (void) DWLmemset(dec_cont, 0, sizeof(DecContainer));

  dec_cont->dwl = dwl;

  pthread_mutex_init(&dec_cont->protect_mutex, NULL);

  MP4API_InitDataStructures(dec_cont);

  dec_cont->ApiStorage.DecStat = INITIALIZED;

  dec_cont->StrmStorage.unsupported_features_present = 0; /* will be se
                                                           * later on */
  SetStrmFmt( dec_cont, strm_fmt );

  dec_cont->StrmStorage.last_packet_byte = 0xFF;
  if( num_frame_buffers > MP4_MAX_BUFFERS )
    num_frame_buffers = MP4_MAX_BUFFERS;
  dec_cont->StrmStorage.max_num_buffers = num_frame_buffers;

  SetCommonConfigRegs(dec_cont->mp4_regs,DWL_CLIENT_TYPE_MPEG4_DEC);

  /* Set prediction filter taps */
  SetDecRegister(dec_cont->mp4_regs, HWIF_PRED_BC_TAP_0_0, -1);
  SetDecRegister(dec_cont->mp4_regs, HWIF_PRED_BC_TAP_0_1,  3);
  SetDecRegister(dec_cont->mp4_regs, HWIF_PRED_BC_TAP_0_2, -6);
  SetDecRegister(dec_cont->mp4_regs, HWIF_PRED_BC_TAP_0_3, 20);

  (void)DWLmemset(&config, 0, sizeof(DWLHwConfig));

  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_MPEG4_DEC);

  if(!config.addr64_support && sizeof(void *) == 8) {
    MP4_API_TRC("MPEG4DecInit# ERROR: HW not support 64bit address!\n");
    return (MP4DEC_PARAM_ERROR);
  }

  i = DWLReadAsicID(DWL_CLIENT_TYPE_MPEG4_DEC) >> 16;
  if(i == 0x8170U)
    error_handling = 0;

  dec_cont->ref_buf_support = config.ref_buf_support;
  reference_frame_format = dpb_flags & DEC_REF_FRM_FMT_MASK;
  if(reference_frame_format == DEC_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!config.tiled_mode_support) {
      return MP4DEC_FORMAT_NOT_SUPPORTED;
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
    return (MP4DEC_PARAM_ERROR);
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
    return (MP4DEC_MEMFAIL);
  }
  dec_cont->StrmStorage.release_buffer = 0;


  /* Custom DPB modes require tiled support >= 2 */
  dec_cont->allow_dpb_field_ordering = 0;
  dec_cont->dpb_mode = DEC_DPB_NOT_INITIALIZED;
  if( dpb_flags & DEC_DPB_ALLOW_FIELD_ORDERING ) {
    dec_cont->allow_dpb_field_ordering = config.field_dpb_support;
  }

  dec_cont->StrmStorage.intra_freeze = error_handling == DEC_EC_VIDEO_FREEZE;
  if (error_handling == DEC_EC_PARTIAL_FREEZE)
    dec_cont->StrmStorage.partial_freeze = 1;
  else if (error_handling == DEC_EC_PARTIAL_IGNORE)
    dec_cont->StrmStorage.partial_freeze = 2;
  dec_cont->StrmStorage.picture_broken = HANTRO_FALSE;

  InitWorkarounds(MP4_DEC_X170_MODE_MPEG4, &dec_cont->workarounds);

#ifdef USE_OUTPUT_RELEASE
  /* take top/botom fields into consideration */
  if (FifoInit(MP4_MAX_BUFFERS*2, &dec_cont->fifo_display) != FIFO_OK)
    return MP4DEC_MEMFAIL;
#endif
#ifdef USE_EXTERNAL_BUFFER
  dec_cont->no_reallocation = 1;
#endif

  dec_cont->use_adaptive_buffers = use_adaptive_buffers;
  dec_cont->n_guard_size = n_guard_size;

  /* If tile mode is enabled, should take DTRC minimum size(96x48) into consideration */
  if (dec_cont->tiled_mode_support) {
    dec_cont->min_dec_pic_width = MP4_MIN_WIDTH_EN_DTRC;
    dec_cont->min_dec_pic_height = MP4_MIN_HEIGHT_EN_DTRC;
  }
  else {
    dec_cont->min_dec_pic_width = MP4_MIN_WIDTH;
    dec_cont->min_dec_pic_height = MP4_MIN_HEIGHT;
  }

  /* return the newly created instance */
  *dec_inst = (DecContainer *) dec_cont;

  MP4_API_TRC("MP4DecInit: OK\n");

  return (MP4DEC_OK);
}

/*------------------------------------------------------------------------------

    Function: MP4DecGetInfo()

        Functional description:
            This function provides read access to decoder information. This
            function should not be called before MP4DecDecode function has
            indicated that headers are ready.

        Inputs:
            dec_inst     decoder instance

        Outputs:
            dec_info    pointer to info struct where data is written

        Returns:
            MP4DEC_OK            success
            MP4DEC_PARAM_ERROR   invalid parameters
            MP4DEC_HDRS_NOT_RDY  information not available yet

------------------------------------------------------------------------------*/
MP4DecRet MP4DecGetInfo(MP4DecInst dec_inst, MP4DecInfo * dec_info) {

#define API_STOR ((DecContainer *)dec_inst)->ApiStorage
#define DEC_VOPD ((DecContainer *)dec_inst)->VopDesc
#define DEC_STRM ((DecContainer *)dec_inst)->StrmDesc
#define DEC_STST ((DecContainer *)dec_inst)->StrmStorage
#define DEC_HDRS ((DecContainer *)dec_inst)->Hdrs
#define DEC_REGS ((DecContainer *)dec_inst)->mp4_regs

  MP4_API_TRC("MP4DecGetInfo#\n");

  if(dec_inst == NULL || dec_info == NULL) {
    return MP4DEC_PARAM_ERROR;
  }

  dec_info->multi_buff_pp_size = 2;

  if(API_STOR.DecStat == UNINIT || API_STOR.DecStat == INITIALIZED) {
    return MP4DEC_HDRS_NOT_RDY;
  }

  if (!((DecContainer *)dec_inst)->pp_enabled) {
    dec_info->frame_width = DEC_VOPD.vop_width << 4;
    dec_info->frame_height = DEC_VOPD.vop_height << 4;
  } else {
    dec_info->frame_width = (DEC_VOPD.vop_width << 4) >> ((DecContainer *)dec_inst)->dscale_shift_x;
    dec_info->frame_height = (DEC_VOPD.vop_height << 4) >> ((DecContainer *)dec_inst)->dscale_shift_y;
  }
  if(DEC_STST.short_video)
    dec_info->stream_format = DEC_STST.mpeg4_video ? 1 : 2;
  else
    dec_info->stream_format = 0;

  dec_info->profile_and_level_indication = DEC_HDRS.profile_and_level_indication;
  dec_info->video_range = DEC_HDRS.video_range;
  dec_info->video_format = DEC_HDRS.video_format;

  if (!((DecContainer *)dec_inst)->pp_enabled) {
    dec_info->coded_width = DEC_HDRS.video_object_layer_width;
    dec_info->coded_height = DEC_HDRS.video_object_layer_height;
  } else {
    dec_info->coded_width = DEC_HDRS.video_object_layer_width >> ((DecContainer *)dec_inst)->dscale_shift_x;
    dec_info->coded_height = DEC_HDRS.video_object_layer_height >> ((DecContainer *)dec_inst)->dscale_shift_y;
  }

  /* length of user data fields */
  dec_info->user_data_voslen = DEC_STRM.user_data_voslen;
  dec_info->user_data_visolen = DEC_STRM.user_data_volen;
  dec_info->user_data_vollen = DEC_STRM.user_data_vollen;
  dec_info->user_data_govlen = DEC_STRM.user_data_govlen;
  dec_info->dpb_mode = ((DecContainer *)dec_inst)->dpb_mode;

  MP4DecPixelAspectRatio((DecContainer *) dec_inst, dec_info);

  dec_info->interlaced_sequence = DEC_HDRS.interlaced;
#ifdef USE_EXTERNAL_BUFFER
  dec_info->pic_buff_size = 3;
#endif

  dec_info->multi_buff_pp_size = 2; /*DEC_HDRS.interlaced ? 1 : 2;*/

  if(((DecContainer *)dec_inst)->tiled_mode_support) {
    if(DEC_HDRS.interlaced &&
        (dec_info->dpb_mode != DEC_DPB_INTERLACED_FIELD)) {
      dec_info->output_format = MP4DEC_SEMIPLANAR_YUV420;
    } else {
      dec_info->output_format = MP4DEC_TILED_YUV420;
    }
  } else {
    dec_info->output_format = MP4DEC_SEMIPLANAR_YUV420;
  }

  dec_info->gmc_support =  (DEC_VOPD.vop_coding_type != 3);

  MP4_API_TRC("MP4DecGetInfo: OK\n");
  return (MP4DEC_OK);

#undef API_STOR
#undef DEC_STRM
#undef DEC_VOPD
#undef DEC_STST
#undef DEC_HDRS
#undef DEC_REGS
}

/*------------------------------------------------------------------------------

    Function: MP4DecRelease()

        Functional description:
            Release the decoder instance.

        Inputs:
            dec_inst     Decoder instance

        Outputs:
            none

        Returns:
            none

------------------------------------------------------------------------------*/

void MP4DecRelease(MP4DecInst dec_inst) {
#define API_STOR ((DecContainer *)dec_inst)->ApiStorage
  DecContainer *dec_cont = NULL;
  const void *dwl;
#ifndef USE_EXTERNAL_BUFFER
  u32 i;
#endif

  MP4DEC_DEBUG(("1\n"));
  MP4_API_TRC("MP4DecRelease#\n");
  if(dec_inst == NULL) {
    MP4_API_TRC("MP4DecRelease# ERROR: dec_inst == NULL\n");
    return;
  }

  dec_cont = ((DecContainer *) dec_inst);
  dwl = dec_cont->dwl;
  pthread_mutex_destroy(&dec_cont->protect_mutex);

  if (dec_cont->asic_running)
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);
#ifndef USE_OUTPUT_RELEASE
  BqueueRelease(&dec_cont->StrmStorage.bq);
#else
  BqueueRelease2(&dec_cont->StrmStorage.bq);
#endif
  BqueueRelease(&dec_cont->StrmStorage.bq_pp);

#ifdef USE_OUTPUT_RELEASE
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
#endif

  if(dec_cont->MbSetDesc.ctrl_data_mem.virtual_address != NULL) {
    DWLFreeLinear(dec_cont->dwl, &dec_cont->MbSetDesc.ctrl_data_mem);
    dec_cont->MbSetDesc.ctrl_data_mem.virtual_address = NULL;
  }
  if(dec_cont->MbSetDesc.mv_data_mem.virtual_address != NULL) {
    DWLFreeLinear(dec_cont->dwl, &dec_cont->MbSetDesc.mv_data_mem);
    dec_cont->MbSetDesc.mv_data_mem.virtual_address = NULL;
  }
  if(dec_cont->MbSetDesc.rlc_data_mem.virtual_address != NULL) {
    DWLFreeLinear(dec_cont->dwl, &dec_cont->MbSetDesc.rlc_data_mem);
    dec_cont->MbSetDesc.rlc_data_mem.virtual_address = NULL;
  }
  if(dec_cont->MbSetDesc.DcCoeffMem.virtual_address != NULL) {
    DWLFreeLinear(dec_cont->dwl, &dec_cont->MbSetDesc.DcCoeffMem);
    dec_cont->MbSetDesc.DcCoeffMem.virtual_address = NULL;
  }
  if(dec_cont->StrmStorage.direct_mvs.virtual_address != NULL)
    DWLFreeLinear(dec_cont->dwl, &dec_cont->StrmStorage.direct_mvs);

  if(dec_cont->StrmStorage.quant_mat_linear.virtual_address != NULL)
    DWLFreeLinear(dec_cont->dwl, &dec_cont->StrmStorage.quant_mat_linear);

#ifndef USE_EXTERNAL_BUFFER
  for(i = 0; i < dec_cont->StrmStorage.num_buffers ; i++)
    if(dec_cont->StrmStorage.data[i].virtual_address != NULL)
      DWLFreeRefFrm(dec_cont->dwl, &dec_cont->StrmStorage.data[i]);
#endif

  DWLfree(dec_cont);
#ifndef USE_EXTERNAL_BUFFER
  (void) DWLRelease(dwl);
#endif

  MP4_API_TRC("MP4DecRelease: OK\n");
#undef API_STOR
  (void)dwl;
}

/*------------------------------------------------------------------------------

    Function: mpeg4RegisterPP()

        Functional description:
            Register the pp for mpeg-4 pipeline

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

i32 mpeg4RegisterPP(const void *dec_inst, const void *pp_inst,
                    void (*PPRun) (const void *, DecPpInterface *),
                    void (*PPEndCallback) (const void *),
                    void (*PPConfigQuery) (const void *, DecPpQuery *),
                    void (*PPDisplayIndex)(const void *, u32),
                    void (*PPBufferData) (const void *, u32, addr_t, addr_t, addr_t, addr_t)) {
  DecContainer *dec_cont;

  dec_cont = (DecContainer *) dec_inst;

  if(dec_inst == NULL || dec_cont->pp_instance != NULL ||
      pp_inst == NULL || PPRun == NULL || PPEndCallback == NULL ||
      PPConfigQuery == NULL || PPDisplayIndex == NULL ||
      PPBufferData == NULL)
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

    Function: mpeg4RegisterPP()

        Functional description:
            Unregister the pp from mpeg-4 pipeline

        Inputs:
            dec_inst     Decoder instance
            const void  *pp_inst - post-processor instance

        Outputs:
            none

        Returns:
            i32 - return 0 for success or a negative error code

------------------------------------------------------------------------------*/

i32 mpeg4UnregisterPP(const void *dec_inst, const void *pp_inst) {
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

        Function name: MP4DecGetUserData()

        Purpose:    This function is used to get user data information.

        Input:      MP4DecInst        dec_inst   (decoder instance)
                    MP4DecUserConf     .userDataConfig (config structure ptr)

        Output:     MP4DEC_PARAM_ERROR  error in parameters
                    MP4DEC_OK           success

------------------------------------------------------------------------------*/

MP4DecRet MP4DecGetUserData(MP4DecInst dec_inst,
                            const MP4DecInput * input,
                            MP4DecUserConf * p_user_data_config) {
#define API_STOR ((DecContainer *)dec_inst)->ApiStorage
#define DEC_STRM ((DecContainer *)dec_inst)->StrmDesc
#define DEC_HDRS ((DecContainer *)dec_inst)->Hdrs
#define DEC_VOPD ((DecContainer *)dec_inst)->VopDesc

  DecContainer *dec_cont;
  u32 mode = 0;

  MP4_API_TRC("MP4DecGetUserData#\n");
  if((dec_inst == NULL) || (p_user_data_config == NULL) || (input == NULL)) {
    MP4DEC_API_DEBUG(("MP4DecGetUserData# ERROR: input pointer is NULL\n"));
    return (MP4DEC_PARAM_ERROR);
  }
  dec_cont = (DecContainer *) dec_inst;

  if((input->stream == NULL) || (input->data_len == 0)) {
    MP4DEC_API_DEBUG(("MP4DecGetUserData# ERROR: stream pointer is NULL\n"));
    return (MP4DEC_PARAM_ERROR);
  }

  /* Assign pointers into structures */

  DEC_STRM.p_strm_buff_start = input->stream;
  DEC_STRM.strm_curr_pos = input->stream;
  DEC_STRM.bit_pos_in_word = 0;
  DEC_STRM.strm_buff_size = input->data_len;
  DEC_STRM.strm_buff_read_bits = 0;

  switch (p_user_data_config->user_data_type) {
  case MP4DEC_USER_DATA_VOS:
    mode = SC_VOS_START;
    if(p_user_data_config->p_user_data_vos) {
      DEC_STRM.p_user_data_vos = p_user_data_config->p_user_data_vos;
    } else {
      MP4DEC_API_DEBUG(("MP4DecGetUserData# ERR:p_user_data_vos = NULL"));
      return (MP4DEC_PARAM_ERROR);
    }
    DEC_STRM.user_data_vosmax_len = p_user_data_config->user_data_vosmax_len;
    break;
  case MP4DEC_USER_DATA_VISO:
    mode = SC_VISO_START;
    if(p_user_data_config->p_user_data_viso) {
      DEC_STRM.p_user_data_vo = p_user_data_config->p_user_data_viso;
    } else {
      MP4DEC_API_DEBUG(("MP4DecGetUserData# ERR:p_user_data_viso = NULL"));
      return (MP4DEC_PARAM_ERROR);
    }
    DEC_STRM.user_data_vomax_len = p_user_data_config->user_data_visomax_len;
    break;
  case MP4DEC_USER_DATA_VOL:
    mode = SC_VOL_START;
    if(p_user_data_config->p_user_data_vol) {
      DEC_STRM.p_user_data_vol = p_user_data_config->p_user_data_vol;
    } else {
      MP4DEC_API_DEBUG(("MP4DecGetUserData# ERR:p_user_data_vol = NULL"));
      return (MP4DEC_PARAM_ERROR);
    }
    DEC_STRM.user_data_volmax_len = p_user_data_config->user_data_volmax_len;
    break;
  case MP4DEC_USER_DATA_GOV:
    mode = SC_GVOP_START;
    if(p_user_data_config->p_user_data_gov) {
      DEC_STRM.p_user_data_gov = p_user_data_config->p_user_data_gov;
    } else {
      MP4DEC_API_DEBUG(("MP4DecGetUserData# ERR:p_user_data_gov = NULL"));
      return (MP4DEC_PARAM_ERROR);
    }
    DEC_STRM.user_data_govmax_len = p_user_data_config->user_data_govmax_len;
    break;
  default:
    MP4DEC_API_DEBUG(("MP4DecGetUserData# ERR:incorrect user data type"));
    return (MP4DEC_PARAM_ERROR);
  }

  /* search VOS, VISO, VOL or GOV start code */
  while(!IS_END_OF_STREAM(dec_cont)) {
    if(StrmDec_ShowBits(dec_cont, 32) == mode) {
      MP4DEC_DEBUG(("SEARCH MODE START CODE\n"));
      break;
    }
    (void) StrmDec_FlushBits(dec_cont, 8);
  }

  /* search user data start code */
  while(!IS_END_OF_STREAM(dec_cont)) {
    if(StrmDec_ShowBits(dec_cont, 32) == SC_UD_START) {
      MP4DEC_DEBUG(("FOUND THE START CODE\n"));
      break;
    }
    (void) StrmDec_FlushBits(dec_cont, 8);
  }

  /* read and save user data */
  if(StrmDec_SaveUserData(dec_cont, mode) == HANTRO_NOK) {
    MP4DEC_API_DEBUG(("MP4DecGetUserData# ERR: reading user data failed"));
    return (MP4DEC_PARAM_ERROR);
  }

  /* restore StrmDesc structure */

  /* set zeros after reading user data field */
  switch (p_user_data_config->user_data_type) {
  case MP4DEC_USER_DATA_VOS:
    DEC_STRM.user_data_voslen = 0;
    DEC_STRM.p_user_data_vos = 0;
    DEC_STRM.user_data_vosmax_len = 0;
    break;
  case MP4DEC_USER_DATA_VISO:
    DEC_STRM.user_data_volen = 0;
    DEC_STRM.p_user_data_vo = 0;
    DEC_STRM.user_data_vomax_len = 0;
    break;
  case MP4DEC_USER_DATA_VOL:
    DEC_STRM.user_data_vollen = 0;
    DEC_STRM.p_user_data_vol = 0;
    DEC_STRM.user_data_volmax_len = 0;
    break;
  case MP4DEC_USER_DATA_GOV:
    DEC_STRM.user_data_govlen = 0;
    DEC_STRM.p_user_data_gov = 0;
    DEC_STRM.user_data_govmax_len = 0;
    break;
  default:
    break;
  }
  MP4_API_TRC("MP4DecGetUserData# OK\n");
  return (MP4DEC_OK);

#undef API_STOR
#undef DEC_STRM
#undef DEC_HDRS
#undef DEC_VOPD

}

/*------------------------------------------------------------------------------
    Function name   : MP4RefreshRegs
    Description     : update shadow registers from real register
    Return type     : void
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void MP4RefreshRegs(DecContainer * dec_cont) {
  i32 i;
  u32 *pp_regs = dec_cont->mp4_regs;
  u32 offset;

  for(i = 0; i < DEC_X170_REGISTERS; i++) {
    pp_regs[i] = DWLReadReg(dec_cont->dwl, dec_cont->core_id, 4 * i);
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs =  dec_cont->mp4_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *pp_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#endif
  offset = PP_START_UNIFIED_REGS * 0x04;
  pp_regs =  dec_cont->mp4_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    *pp_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
}

/*------------------------------------------------------------------------------
    Function name   : MP4FlushRegs
    Description     : Flush shadow register to real register
    Return type     : void
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void MP4FlushRegs(DecContainer * dec_cont) {
  i32 i;
  u32 *pp_regs = dec_cont->mp4_regs;
  u32 offset;

  for(i = 2; i < DEC_X170_REGISTERS; i++) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * i, pp_regs[i]);
    pp_regs[i] = 0;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  pp_regs =  dec_cont->mp4_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *pp_regs);
    pp_regs++;
    offset += 4;
  }
#endif
  offset = PP_START_UNIFIED_REGS * 0x04;
  pp_regs =  dec_cont->mp4_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *pp_regs);
    pp_regs++;
    offset += 4;
  }
}

/*------------------------------------------------------------------------------
    Function name   : HandleVlcModeError
    Description     : error handling for VLC mode
    Return type     : u32
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
u32 HandleVlcModeError(DecContainer * dec_cont, u32 pic_num) {

  u32 ret = MP4DEC_STRM_PROCESSED, tmp;

  tmp = StrmDec_FindSync(dec_cont);
  /* error in first picture -> discard directly */
  if(!dec_cont->VopDesc.vop_number_in_seq) {

    if(dec_cont->pp_instance != NULL)
      dec_cont->StrmStorage.p_pic_buf
      [dec_cont->StrmStorage.work_out].send_to_pp = 0;

    dec_cont->StrmStorage.skip_b = 2;
    /* no pictures finished -> return STRM_PROCESSED */
    if(tmp == END_OF_STREAM) {
      ret = MP4DEC_STRM_PROCESSED;
    } else {
      if(dec_cont->StrmDesc.strm_buff_read_bits > 39) {
        dec_cont->StrmDesc.strm_buff_read_bits -= 32;
        /* drop 3 lsbs (i.e. make read bits next lowest multiple of byte) */
        dec_cont->StrmDesc.strm_buff_read_bits &= 0xFFFFFFF8;
        dec_cont->StrmDesc.bit_pos_in_word = 0;
        dec_cont->StrmDesc.strm_curr_pos -= 4;
      } else {
        dec_cont->StrmDesc.strm_buff_read_bits = 0;
        dec_cont->StrmDesc.bit_pos_in_word = 0;
        dec_cont->StrmDesc.strm_curr_pos =
          dec_cont->StrmDesc.p_strm_buff_start;
      }
      dec_cont->StrmStorage.status = STATE_OK;

      ret = MP4DEC_OK;
    }

  } else {
    if(dec_cont->VopDesc.vop_coding_type != BVOP) {
      ret = MP4DEC_PIC_DECODED;

      if(dec_cont->pp_instance != NULL)
        dec_cont->StrmStorage.p_pic_buf
        [dec_cont->StrmStorage.work_out].send_to_pp = 0;

      BqueueDiscard( &dec_cont->StrmStorage.bq, dec_cont->StrmStorage.work_out );
      dec_cont->StrmStorage.work_out_prev = dec_cont->StrmStorage.work_out;
      dec_cont->StrmStorage.work_out = dec_cont->StrmStorage.work0;

      /* start PP! */
      if (dec_cont->pp_instance != NULL &&
          !dec_cont->StrmStorage.parallel_mode2 &&
          /* parallel mode and PP finished -> do not run again */
          (dec_cont->pp_control.use_pipeline ||
           dec_cont->pp_control.pp_status != DECPP_PIC_READY)) {
        PPControl(dec_cont, 1);

        /* End PP co-operation */
        if(dec_cont->pp_control.pp_status == DECPP_RUNNING) {
          MP4DEC_API_DEBUG(("Wait for PP\n"));
          dec_cont->PPEndCallback(dec_cont->pp_instance);

          dec_cont->pp_control.pp_status = DECPP_PIC_READY;
        }
      }

      if(dec_cont->pp_instance != NULL &&
          !dec_cont->StrmStorage.parallel_mode2)
        dec_cont->StrmStorage.p_pic_buf
        [dec_cont->StrmStorage.work_out].send_to_pp = 1;

      dec_cont->StrmStorage.skip_b = 2;

      dec_cont->VopDesc.vop_number++;
      dec_cont->VopDesc.vop_number_in_seq++;
      MP4DecBufferPicture(dec_cont, pic_num,
                          dec_cont->VopDesc.vop_coding_type,
                          dec_cont->VopDesc.total_mb_in_vop);

      if( dec_cont->StrmStorage.work1 != INVALID_ANCHOR_PICTURE )
        dec_cont->StrmStorage.work1 = dec_cont->StrmStorage.work0;
    } else {
      if(dec_cont->StrmStorage.intra_freeze) {
        dec_cont->VopDesc.vop_number++;
        dec_cont->VopDesc.vop_number_in_seq++;
        MP4DecBufferPicture(dec_cont, pic_num,
                            dec_cont->VopDesc.vop_coding_type,
                            dec_cont->VopDesc.total_mb_in_vop);

        ret = MP4DEC_PIC_DECODED;
      } else {
        ret = MP4DEC_NONREF_PIC_SKIPPED;
      }

      /* prev_bidx is not valid if this is B skipped due to previous
       * errors */
      if (!dec_cont->StrmStorage.skip_b)
        dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.prev_bidx].
        send_to_pp = 0;
    }

  }

  dec_cont->ApiStorage.DecStat = STREAMDECODING;
  dec_cont->StrmStorage.valid_vop_header = HANTRO_FALSE;

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : HandleVopEnd
    Description     : VOP end special cases
    Return type     : void
    Argument        : DecContainer *dec_cont
------------------------------------------------------------------------------*/
void HandleVopEnd(DecContainer * dec_cont) {

  u32 tmp;

  ProcessHwOutput( dec_cont );

  dec_cont->StrmDesc.strm_buff_read_bits =
    8 * (dec_cont->StrmDesc.strm_curr_pos -
         dec_cont->StrmDesc.p_strm_buff_start);
  dec_cont->StrmDesc.bit_pos_in_word = 0;

  /* If last MBs of BVOP were skipped using colocated MB status,
   * eat away possible resync markers. */
  if(dec_cont->VopDesc.vop_coding_type == BVOP) {
    StrmDec_ProcessBvopExtraResync( dec_cont );
  }

  /* there might be extra stuffing byte if next start code is
   * video object sequence start or end code. If this is the
   * case the stuffing is normal mpeg4 stuffing. */
  tmp = StrmDec_ShowBitsAligned(dec_cont, 32, 1);
  if(((tmp == SC_VOS_START) || (tmp == SC_VOS_END) ||
      ((u32)(dec_cont->StrmDesc.strm_curr_pos -
             dec_cont->StrmDesc.p_strm_buff_start) ==
       (dec_cont->StrmDesc.strm_buff_size - 1))) &&
      (dec_cont->StrmDesc.strm_curr_pos[0] == 0x7F)) {
    (void) StrmDec_FlushBits(dec_cont, 8);
  }

  /* handle extra zeros after VOP */
  if(!dec_cont->StrmStorage.short_video) {
    tmp = StrmDec_ShowBits(dec_cont, 32);
    if(!(tmp >> 8)) {
      do {
        if(StrmDec_FlushBits(dec_cont, 8) == END_OF_STREAM)
          break;
        tmp = StrmDec_ShowBits(dec_cont, 32);
      } while(!(tmp >> 8));
    }
  } else {
    tmp = StrmDec_ShowBits(dec_cont, 24);
    if(!tmp) {
      do {
        if(StrmDec_FlushBits(dec_cont, 8) == END_OF_STREAM)
          break;
        tmp = StrmDec_ShowBits(dec_cont, 24);
      } while(!tmp);
    }
  }
}

/*------------------------------------------------------------------------------

         Function name: RunDecoderAsic
         Purpose:       Set Asic run lenght and run Asic
         Input:         DecContainer *dec_cont
         Output:        u32 asic status

------------------------------------------------------------------------------*/
u32 RunDecoderAsic(DecContainer * dec_container, addr_t strm_bus_address) {

  i32 ret;
  addr_t tmp = 0;
  u32 asic_status = 0;

  ASSERT(MP4DecResolveVirtual(dec_container,
                              dec_container->StrmStorage.work_out) != 0);
  ASSERT(dec_container->rlc_mode || strm_bus_address != 0);
  dec_container->pp_control.input_bus_luma = 0;

  /* Save frameDesc/Hdr info for current picture. */
  dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].VopDesc
    = dec_container->VopDesc;
  dec_container->StrmStorage.p_pic_buf[dec_container->StrmStorage.work_out].Hdrs
    = dec_container->Hdrs;

  if(!dec_container->asic_running) {

    /* quantization matrices may have changed -> reload */
    if (dec_container->StrmStorage.reload_quant_matrices) {
      MP4SetQuantMatrix(dec_container);
      dec_container->StrmStorage.reload_quant_matrices = HANTRO_FALSE;
    }

    tmp = MP4SetRegs(dec_container, strm_bus_address);
    if(tmp == HANTRO_NOK)
      return 0;

    (void) DWLReserveHw(dec_container->dwl, &dec_container->core_id);

    /* Start PP */
    if(dec_container->pp_instance != NULL) {
      PPControl(dec_container, 0);
    } else {
      SetDecRegister(dec_container->mp4_regs, HWIF_DEC_OUT_DIS, 0);
      SetDecRegister(dec_container->mp4_regs, HWIF_FILTERING_DIS, 1);
    }

#ifdef MP4_ASIC_TRACE
    writePictureCtrlHex(dec_container, dec_container->rlc_mode);
#endif

    dec_container->asic_running = 1;

    DWLWriteReg(dec_container->dwl, dec_container->core_id, 0x4, 0);

    MP4FlushRegs(dec_container);

    /* Enable HW */
    DWLEnableHw(dec_container->dwl, dec_container->core_id, 4 * 1, 1);
  } else { /* in the middle of VOP, continue decoding */
    /* tmp is strm_bus_address + number of bytes decoded by SW */
    tmp = dec_container->StrmDesc.strm_curr_pos -
          dec_container->StrmDesc.p_strm_buff_start;
    tmp = strm_bus_address + tmp;

    /* pointer to start of the stream, mask to get the pointer to
     * previous 64-bit aligned position */
    if(!(tmp & ~0x7)) {
      return 0;
    }

    SET_ADDR_REG(dec_container->mp4_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);
    /* amount of stream (as seen by the HW), obtained as amount of stream
     * given by the application subtracted by number of bytes decoded by
     * SW (if strm_bus_address is not 64-bit aligned -> adds number of bytes
     * from previous 64-bit aligned boundary) */
    SetDecRegister(dec_container->mp4_regs, HWIF_STREAM_LEN,
                   dec_container->StrmDesc.strm_buff_size -
                   ((tmp & ~0x7) - strm_bus_address));
    SetDecRegister(dec_container->mp4_regs, HWIF_STRM_START_BIT,
                   dec_container->StrmDesc.bit_pos_in_word + 8 * (tmp & 0x7));

    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 5,
                dec_container->mp4_regs[5]);
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 6,
                dec_container->mp4_regs[6]);
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 12,
                dec_container->mp4_regs[12]);
#ifdef USE_64BIT_ENV
    DWLWriteReg(dec_container->dwl, dec_container->core_id, 4 * 122,
                dec_container->mp4_regs[122]);
#endif

    DWLEnableHw(dec_container->dwl, dec_container->core_id, 4 * 1,
                dec_container->mp4_regs[1]);
  }

  MP4DEC_API_DEBUG(("Wait for Decoder\n"));
  ret = DWLWaitHwReady(dec_container->dwl, dec_container->core_id,
                       (u32) DEC_X170_TIMEOUT_LENGTH);

  MP4RefreshRegs(dec_container);

  if(ret == DWL_HW_WAIT_OK) {
    asic_status = GetDecRegister(dec_container->mp4_regs, HWIF_DEC_IRQ_STAT);
  } else {
    /* reset HW */
    SetDecRegister(dec_container->mp4_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_container->mp4_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_container->mp4_regs, HWIF_DEC_E, 0);

    DWLDisableHw(dec_container->dwl, dec_container->core_id, 4 * 1, 0);

    /* Wait for PP to end also */
    if(dec_container->pp_instance != NULL &&
        dec_container->pp_control.pp_status == DECPP_RUNNING) {
      dec_container->pp_control.pp_status = DECPP_PIC_READY;

      MP4DEC_API_DEBUG(("RunDecoderAsic: PP Wait for end\n"));

      dec_container->PPEndCallback(dec_container->pp_instance);

      MP4DEC_API_DEBUG(("RunDecoderAsic: PP Finished\n"));
    }

    dec_container->asic_running = 0;

    DWLReleaseHw(dec_container->dwl, dec_container->core_id);

    return (ret == DWL_HW_WAIT_ERROR) ?
           X170_DEC_SYSTEM_ERROR : X170_DEC_TIMEOUT;
  }

  if(!(asic_status & MP4_DEC_X170_IRQ_BUFFER_EMPTY)) {
    DWLDisableHw(dec_container->dwl, dec_container->core_id, 0x4, 0);

    /* End PP co-operation */
    if(dec_container->pp_control.pp_status == DECPP_RUNNING) {
      MP4DEC_API_DEBUG(("RunDecoderAsic: PP Wait for end\n"));
      if(dec_container->pp_instance != NULL)
        dec_container->PPEndCallback(dec_container->pp_instance);
      MP4DEC_API_DEBUG(("RunDecoderAsic: PP Finished\n"));

      dec_container->pp_control.pp_status = DECPP_PIC_READY;
    }

    dec_container->asic_running = 0;

    DWLReleaseHw(dec_container->dwl, dec_container->core_id);
  }


  /* if in VLC mode and HW interrupt indicated either BUFFER_EMPTY or
   * DEC_RDY -> read stream end pointer and update StrmDesc structure */
  if((!dec_container->rlc_mode ||
      dec_container->VopDesc.vop_coding_type == BVOP) &&
      (asic_status & (MP4_DEC_X170_IRQ_BUFFER_EMPTY|MP4_DEC_X170_IRQ_DEC_RDY))) {
    tmp = GET_ADDR_REG(dec_container->mp4_regs, HWIF_RLC_VLC_BASE);

    /* update buffer size only for reasonable size of used data */
    if((tmp - strm_bus_address) <= dec_container->StrmDesc.strm_buff_size) {
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

  SetDecRegister(dec_container->mp4_regs, HWIF_DEC_IRQ_STAT, 0);

  if(dec_container->rlc_mode) {
    /* Reset pointers of the SW/HW-shared buffers */
    dec_container->MbSetDesc.p_rlc_data_curr_addr =
      dec_container->MbSetDesc.p_rlc_data_addr;
    dec_container->MbSetDesc.p_rlc_data_vp_addr =
      dec_container->MbSetDesc.p_rlc_data_addr;
    dec_container->MbSetDesc.odd_rlc_vp = dec_container->MbSetDesc.odd_rlc;
  }

  if( dec_container->VopDesc.vop_coding_type != BVOP &&
      dec_container->ref_buf_support &&
      (asic_status & MP4_DEC_X170_IRQ_DEC_RDY) &&
      dec_container->asic_running == 0) {
    RefbuMvStatistics( &dec_container->ref_buffer_ctrl,
                       dec_container->mp4_regs,
                       dec_container->StrmStorage.direct_mvs.virtual_address,
                       !dec_container->Hdrs.low_delay,
                       dec_container->VopDesc.vop_coding_type == IVOP );
  }

  return asic_status;

}

/*------------------------------------------------------------------------------

    Function name: MP4DecNextPicture

    Functional description:
        Retrieve next decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct
        end_of_stream Indicates whether end of stream has been reached

    Output:
        picture Decoder output picture.

    Return values:
        MP4DEC_OK                   No picture available.
        MP4DEC_PIC_RDY              Picture ready.
        MP4DEC_NOT_INITIALIZED      Decoder instance not initialized yet
------------------------------------------------------------------------------*/
MP4DecRet MP4DecNextPicture(MP4DecInst dec_inst, MP4DecPicture * picture,
                            u32 end_of_stream) {

  /* Variables */

  MP4DecRet return_value = MP4DEC_PIC_RDY;
  DecContainer *dec_cont;
  picture_t *p_pic;
  u32 pic_index = BUFFER_UNDEFINED;
  u32 min_count;
  u32 parallel_mode2_flag = 0;
  i32 ret;

  /* Code */

  MP4_API_TRC("MP4DecNextPicture#\n");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    MP4_API_TRC("MP4DecNextPicture# ERROR: picture is NULL\n");
    return (MP4DEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    MP4_API_TRC("MP4DecNextPicture# ERROR: Decoder not initialized\n");
    return (MP4DEC_NOT_INITIALIZED);
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
      if (ret == FIFO_EMPTY) return MP4DEC_OK;
#endif

      if ((i32)i == -1) {
        MP4_API_TRC("MP4DecNextPicture# MP4DEC_END_OF_STREAM\n");
        return MP4DEC_END_OF_STREAM;
      }
      if ((i32)i == -2) {
        MP4_API_TRC("MP4DecNextPicture# MP4DEC_FLUSHED\n");
        return MP4DEC_FLUSHED;
      }

      *picture = dec_cont->StrmStorage.picture_info[i];

      MP4_API_TRC("MP4DecNextPicture# MP4DEC_PIC_RDY\n");
      return (MP4DEC_PIC_RDY);
    } else
      return MP4DEC_ABORTED;
  }
#endif

  if(dec_cont->ApiStorage.DecStat == HEADERSDECODED)
    end_of_stream = 1;

  min_count = 0;
  if(dec_cont->Hdrs.low_delay == 0 && !end_of_stream)
    min_count = 1;

  if(dec_cont->StrmStorage.parallel_mode2 && !end_of_stream)
    min_count = 2;

  if(dec_cont->pp_instance &&
      dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_DISABLED) {
    dec_cont->pp_config_query.tiled_mode =
      dec_cont->tiled_reference_enable;
    dec_cont->PPConfigQuery(dec_cont->pp_instance,
                            &dec_cont->pp_config_query);

    if(dec_cont->pp_config_query.pipeline_accepted &&
        dec_cont->Hdrs.low_delay && !MP4_IS_FIELD_OUTPUT &&
        !dec_cont->pp_control.use_pipeline &&
        !dec_cont->StrmStorage.previous_not_coded) {
      dec_cont->StrmStorage.out_count = 1;
    }
  }

  /* Nothing to send out */
  if(dec_cont->StrmStorage.out_count <= min_count) {
    (void) DWLmemset(picture, 0, sizeof(MP4DecPicture));
    picture->output_picture = NULL;
    return_value = MP4DEC_OK;
  } else {
    pic_index = dec_cont->StrmStorage.out_index;
    pic_index = dec_cont->StrmStorage.out_buf[pic_index];

    MP4FillPicStruct(picture, dec_cont, pic_index);

    /* Fill field related */
    if(MP4_IS_FIELD_OUTPUT) {
      picture->field_picture = 1;

      if(!dec_cont->ApiStorage.output_other_field) {
        picture->top_field = dec_cont->VopDesc.top_field_first ? 1 : 0;
        dec_cont->ApiStorage.output_other_field = 1;
      } else {
        picture->top_field = dec_cont->VopDesc.top_field_first ? 0 : 1;
        dec_cont->ApiStorage.output_other_field = 0;
        dec_cont->StrmStorage.out_count--;
        dec_cont->StrmStorage.out_index++;
        dec_cont->StrmStorage.out_index &= (MP4_MAX_BUFFERS-1);
      }
    } else {
      /* progressive or deinterlaced frame output */
      picture->top_field = 0;
      picture->field_picture = 0;
      dec_cont->StrmStorage.out_count--;
      dec_cont->StrmStorage.out_index++;
      dec_cont->StrmStorage.out_index &= (MP4_MAX_BUFFERS-1);
    }
  }

  if(dec_cont->pp_instance &&
      (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_FULLMODE) &&
      end_of_stream && (return_value == MP4DEC_PIC_RDY)) {
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_UNINIT;
    if( dec_cont->StrmStorage.previous_b) {
      dec_cont->pp_control.display_index =
        dec_cont->pp_control.prev_anchor_display_index;
      dec_cont->pp_control.buffer_index =
        dec_cont->pp_control.display_index;
    } else {
      dec_cont->pp_control.display_index = dec_cont->pp_control.buffer_index;
    }
    dec_cont->PPDisplayIndex(dec_cont->pp_instance, dec_cont->pp_control.buffer_index);
  }
  /* pp display process is separate of decoding process  */
  if(dec_cont->pp_instance &&
      (dec_cont->pp_control.multi_buf_stat != MULTIBUFFER_FULLMODE) &&
      (!dec_cont->StrmStorage.parallel_mode2 || return_value == MP4DEC_PIC_RDY)) {
    /* In parallel mode 2, this is the last frame to display */
    if(dec_cont->StrmStorage.pm2_all_processed_flag &&
        !(MP4_IS_FIELD_OUTPUT &&
          (dec_cont->ApiStorage.buffer_for_pp != NO_BUFFER))) {
      pic_index = dec_cont->StrmStorage.work0;

      dec_cont->pp_control.display_index =
        dec_cont->pp_control.buffer_index =
          dec_cont->StrmStorage.pm2lock_buf;
      dec_cont->PPDisplayIndex(dec_cont->pp_instance,
                               dec_cont->pp_control.display_index);
      if(MP4_IS_FIELD_OUTPUT) {
        picture->interlaced = 1;
        picture->field_picture = 1;
        picture->top_field =
          dec_cont->VopDesc.top_field_first ? 1 : 0;

        MP4DecPrepareFieldProcessing(dec_cont, pic_index);
      }

      MP4FillPicStruct(picture, dec_cont, pic_index);
      return_value = MP4DEC_PIC_RDY;
      dec_cont->pp_control.pp_status = DECPP_IDLE;
    } else if(dec_cont->pp_control.pp_status == DECPP_PIC_READY &&
              (!(dec_cont->StrmStorage.parallel_mode2 && end_of_stream))) {
      if(MP4_IS_FIELD_OUTPUT) {
        picture->interlaced = 1;
        picture->field_picture = 1;
        picture->top_field = dec_cont->VopDesc.top_field_first ? 1 : 0;
      }
      MP4FillPicStruct(picture, dec_cont, pic_index);
      return_value = MP4DEC_PIC_RDY;
      dec_cont->pp_control.pp_status = DECPP_IDLE;
    } else {
      p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;
      return_value = MP4DEC_OK;

      if(((dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_DISABLED &&
           !dec_cont->Hdrs.low_delay) ) ||
          ((dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_SEMIMODE) &&
           MP4_IS_FIELD_OUTPUT) ||
          (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_DISABLED &&
           MP4_IS_FIELD_OUTPUT )) {
        pic_index = BUFFER_UNDEFINED;
      }

      if(end_of_stream) {
        pic_index = 0;
        while((pic_index < dec_cont->StrmStorage.num_buffers ) && !p_pic[pic_index].send_to_pp)
          pic_index++;

        /* In parallel mode 2, at end of stream there are 2 PP output
         * pictures left to show. If previous decoded picture was B
         * picture, we need to run that through PP now */
        if( dec_cont->StrmStorage.parallel_mode2 &&
            !dec_cont->StrmStorage.pm2_all_processed_flag) {
          /* Set flag to signal PP has run through every frame
           * it has to. Also set previous PP picture as "locked",
           * so we now to show it last. */
          dec_cont->StrmStorage.pm2_all_processed_flag = 1;
          dec_cont->pp_control.buffer_index =
            BqueueNext( &dec_cont->StrmStorage.bq_pp,
                        dec_cont->StrmStorage.pm2lock_buf,
                        BQUEUE_UNUSED,
                        BQUEUE_UNUSED,
                        0 );

          /* Connect PP output buffer to decoder output buffer */
          {
            addr_t luma = 0;
            addr_t chroma = 0;
            addr_t bot_luma = 0, bot_chroma = 0;

            luma = MP4DecResolveBus(dec_cont, dec_cont->StrmStorage.work_out);
            chroma = luma + ((dec_cont->VopDesc.vop_width *
                              dec_cont->VopDesc.vop_height) << 8);

            if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
              bot_luma = luma + (dec_cont->VopDesc.vop_width * 16);
              bot_chroma = chroma + (dec_cont->VopDesc.vop_width * 16);
            } else if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
              bot_luma = luma + ((dec_cont->VopDesc.vop_width * 16) *
                                 (dec_cont->VopDesc.vop_height * 8));
              bot_chroma = chroma + ((dec_cont->VopDesc.vop_width * 16) *
                                     (dec_cont->VopDesc.vop_height * 4));
            }

            dec_cont->PPBufferData(dec_cont->pp_instance,
                                   dec_cont->pp_control.buffer_index, luma, chroma,
                                   bot_luma, bot_chroma);
          }

          /* If previous picture was B, we send that to PP now */
          if(dec_cont->StrmStorage.previous_b) {
            pic_index = dec_cont->StrmStorage.prev_bidx;
            p_pic[pic_index].send_to_pp = 1;
            dec_cont->StrmStorage.previous_b = 0;
            parallel_mode2_flag = 1;
            dec_cont->pp_control.display_index =
              dec_cont->pp_control.buffer_index;
          }
          /* ...otherwise we send the previous anchor. */
          else {
            pic_index = dec_cont->StrmStorage.work0;
            p_pic[pic_index].send_to_pp = 1;
            parallel_mode2_flag = 1;
            dec_cont->pp_control.display_index =
              dec_cont->StrmStorage.pm2lock_buf;
            dec_cont->StrmStorage.pm2lock_buf =
              dec_cont->pp_control.buffer_index;
            /* Fix for case if stream has only one frame; then
             * we should have a proper buffer index instead of
             * undefined. */
            if(dec_cont->pp_control.display_index == BQUEUE_UNUSED)
              dec_cont->pp_control.display_index =
                dec_cont->pp_control.buffer_index;
            dec_cont->PPDisplayIndex(dec_cont->pp_instance,
                                     dec_cont->pp_control.display_index);
          }
          if(MP4_IS_FIELD_OUTPUT)
            MP4DecPrepareFieldProcessing(dec_cont, pic_index);
        } else if(MP4_IS_FIELD_OUTPUT) {
          if(MP4_IS_FIELD_OUTPUT &&
              (dec_cont->ApiStorage.buffer_for_pp != NO_BUFFER)) {
            MP4DecFieldAndValidBuffer(picture,
                                      dec_cont, &pic_index);

            /* Restore correct buffer index */
            if(dec_cont->StrmStorage.parallel_mode2 &&
                dec_cont->pp_control.buffer_index != dec_cont->pp_control.display_index) {
              picture_t *p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;

              dec_cont->pp_control.buffer_index =
                dec_cont->pp_control.display_index;
              pic_index = dec_cont->StrmStorage.work1;

              picture->interlaced = 1;
              picture->field_picture = 1;
              dec_cont->pp_control.pic_struct =
                dec_cont->VopDesc.
                top_field_first ? DECPP_PIC_BOT_FIELD_FRAME :
                DECPP_PIC_TOP_FIELD_FRAME;
              picture->top_field = dec_cont->VopDesc.top_field_first ? 0 : 1;

              dec_cont->ApiStorage.buffer_for_pp = NO_BUFFER;
              p_pic[pic_index].send_to_pp = 1;
            }

          } else {
            if(pic_index < dec_cont->StrmStorage.num_buffers) {
              picture->interlaced = 1;
              picture->field_picture = 1;
              /* if field output, other field must be processed also */
              if(!dec_cont->StrmStorage.parallel_mode2) {
                if(pic_index < dec_cont->StrmStorage.num_buffers ) {
                  dec_cont->ApiStorage.buffer_for_pp = pic_index+1;
                } else {
                  ASSERT(0);
                }
              }

              MP4DEC_API_DEBUG(("first field of last frame, send %d still to pp", dec_cont->ApiStorage.buffer_for_pp));
              /* set field processing */
              dec_cont->pp_control.pic_struct =
                dec_cont->VopDesc.top_field_first ?
                DECPP_PIC_TOP_FIELD_FRAME : DECPP_PIC_BOT_FIELD_FRAME;
              /* first field */

              if(dec_cont->pp_control.pic_struct ==
                  DECPP_PIC_BOT_FIELD_FRAME) {
                if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
                  dec_cont->pp_control.bottom_bus_luma =
                    MP4DecResolveBus(dec_cont, pic_index) +
                    (dec_cont->VopDesc.vop_width << 4);

                  dec_cont->pp_control.bottom_bus_chroma =
                    dec_cont->pp_control.bottom_bus_luma +
                    ((dec_cont->VopDesc.vop_width *
                      dec_cont->VopDesc.vop_height) << 8);
                } else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
                  u32 frame_size = (dec_cont->VopDesc.vop_width *
                                    dec_cont->VopDesc.
                                    vop_height) << 8;
                  dec_cont->pp_control.bottom_bus_luma =
                    MP4DecResolveBus(dec_cont, pic_index) +
                    frame_size/2;

                  dec_cont->pp_control.bottom_bus_chroma =
                    MP4DecResolveBus(dec_cont, pic_index) +
                    frame_size + frame_size/4;
                }
              }
            }
          }
        } else if(dec_cont->pp_config_query.deinterlace) {
          MP4DecSetupDeinterlace(dec_cont);
        }

        if(pic_index >= dec_cont->StrmStorage.num_buffers) {
          /* Clear the internal state just after last picture/field */
          dec_cont->VopDesc.vop_number_in_seq = 0;
          dec_cont->StrmStorage.work_out_prev = dec_cont->StrmStorage.work_out = 0;
          dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work1 =
                                          INVALID_ANCHOR_PICTURE;
        }
      } else if(MP4_NON_PIPELINE_AND_B_PICTURE &&
                !dec_cont->StrmStorage.parallel_mode2) {
        pic_index = dec_cont->StrmStorage.prev_bidx;   /* send index 2 (B Picture output) to PP) */
        dec_cont->VopDesc.vop_coding_type = IVOP;

        /* Field decoding for first field of a B picture */
        if(MP4_IS_FIELD_OUTPUT) {
          if(!dec_cont->StrmStorage.parallel_mode2) {
            if(pic_index < dec_cont->StrmStorage.num_buffers ) {
              dec_cont->ApiStorage.buffer_for_pp = pic_index+1;
            } else {
              ASSERT(0);
            }
          }
          picture->interlaced = 1;
          picture->field_picture = 1;
          MP4DEC_API_DEBUG((" Processing first field in NextPicture %d\n", pic_index));
          picture->top_field =
            dec_cont->VopDesc.top_field_first ? 1 : 0;
          dec_cont->pp_control.pic_struct =
            dec_cont->VopDesc.top_field_first ?
            DECPP_PIC_TOP_FIELD_FRAME : DECPP_PIC_BOT_FIELD_FRAME;

          /* first field */
          if(dec_cont->pp_control.pic_struct ==
              DECPP_PIC_BOT_FIELD_FRAME) {
            MP4_SET_BOT_ADDR(pic_index);
          }
        }
      } else if(MP4_IS_FIELD_OUTPUT &&
                (dec_cont->ApiStorage.buffer_for_pp != NO_BUFFER)) {
        MP4DecFieldAndValidBuffer(picture, dec_cont, &pic_index);

        /* Restore correct buffer index */
        if(dec_cont->StrmStorage.parallel_mode2 &&
            dec_cont->pp_control.buffer_index != dec_cont->pp_control.display_index) {
          picture_t *p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;

          dec_cont->pp_control.buffer_index =
            dec_cont->pp_control.display_index;
          pic_index = dec_cont->StrmStorage.work1;

          picture->interlaced = 1;
          picture->field_picture = 1;
          dec_cont->pp_control.pic_struct =
            dec_cont->VopDesc.
            top_field_first ? DECPP_PIC_BOT_FIELD_FRAME :
            DECPP_PIC_TOP_FIELD_FRAME;
          picture->top_field = dec_cont->VopDesc.top_field_first ? 0 : 1;

          dec_cont->ApiStorage.buffer_for_pp = NO_BUFFER;
          p_pic[pic_index].send_to_pp = 1;
        }
      }

      if(pic_index != BUFFER_UNDEFINED) {
        if(p_pic[pic_index].send_to_pp) {
          MP4DEC_API_DEBUG(("NextPicture: send to pp %d\n",
                            pic_index));

          /* forward tiled mode */
          dec_cont->pp_control.tiled_input_mode =
            p_pic[pic_index].tiled_mode;
          dec_cont->pp_control.progressive_sequence =
            !dec_cont->Hdrs.interlaced;

          /* Set up pp */
          if((dec_cont->pp_control.pic_struct ==
              DECPP_PIC_BOT_FIELD_FRAME) ||
              (dec_cont->pp_control.pic_struct ==
               DECPP_PIC_BOT_FIELD)) {
            dec_cont->pp_control.input_bus_luma = 0;
            dec_cont->pp_control.input_bus_chroma = 0;

            MP4_SET_BOT_ADDR(pic_index);

            MP4_SET_FIELD_DIMENSIONS;
          } else {
            dec_cont->pp_control.input_bus_luma =
              MP4DecResolveBus(dec_cont, pic_index);
            dec_cont->pp_control.input_bus_chroma =
              dec_cont->pp_control.input_bus_luma +
              ((dec_cont->VopDesc.vop_width *
                dec_cont->VopDesc.vop_height) << 8);
            if((dec_cont->pp_control.pic_struct ==
                DECPP_PIC_TOP_FIELD_FRAME) ||
                (dec_cont->pp_control.pic_struct ==
                 DECPP_PIC_FRAME_OR_TOP_FIELD &&
                 !dec_cont->pp_config_query.deinterlace &&
                 dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD)) {
              dec_cont->pp_control.bottom_bus_luma = 0;
              dec_cont->pp_control.bottom_bus_chroma = 0;

              MP4_SET_FIELD_DIMENSIONS;
            } else {
              dec_cont->pp_control.inwidth =
                dec_cont->pp_control.cropped_w =
                  dec_cont->VopDesc.vop_width << 4;
              dec_cont->pp_control.inheight =
                dec_cont->pp_control.cropped_h =
                  dec_cont->VopDesc.vop_height << 4;
              if(dec_cont->pp_config_query.deinterlace) {
                MP4DecSetupDeinterlace(dec_cont);
              }

            }
          }

          if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD &&
              !dec_cont->pp_config_query.deinterlace) {
            dec_cont->pp_control.pic_struct = (dec_cont->pp_control.input_bus_luma) ?
                                              DECPP_PIC_FRAME_OR_TOP_FIELD : DECPP_PIC_BOT_FIELD;
          }

          dec_cont->pp_control.use_pipeline = 0;
          {
            u32 tmp =
              GetDecRegister(dec_cont->mp4_regs,
                             HWIF_DEC_OUT_ENDIAN);
            dec_cont->pp_control.little_endian =
              (tmp == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
          }
          dec_cont->pp_control.word_swap =
            GetDecRegister(dec_cont->mp4_regs,
                           HWIF_DEC_OUTSWAP32_E) ? 1 : 0;

          /* Run pp */
          SetDecRegister(dec_cont->mp4_regs, HWIF_FILTERING_DIS, 1);
          dec_cont->PPRun(dec_cont->pp_instance, &dec_cont->pp_control);
          dec_cont->pp_control.pp_status = DECPP_RUNNING;
          ASSERT(dec_cont->StrmStorage.p_pic_buf[pic_index].send_to_pp ==
                 1);
          dec_cont->StrmStorage.p_pic_buf[pic_index].send_to_pp = 0;
          /* Wait for result */
          dec_cont->PPEndCallback(dec_cont->pp_instance);

          MP4FillPicStruct(picture, dec_cont, pic_index);
          return_value = MP4DEC_PIC_RDY;
          if(!parallel_mode2_flag)
            dec_cont->pp_control.pp_status = DECPP_IDLE;
          dec_cont->pp_control.pic_struct =
            DECPP_PIC_FRAME_OR_TOP_FIELD;
        }
      }
    }
  }

  if(return_value == MP4DEC_PIC_RDY) {
    MP4_API_TRC("MP4DecNextPicture# MP4DEC_PIC_RDY\n");
  } else {
    MP4_API_TRC("MP4DecNextPicture# MP4DEC_OK\n");
  }

  return return_value;

}


#ifdef USE_OUTPUT_RELEASE

/*------------------------------------------------------------------------------

    Function name: MP4DecNextPicture_INTERNAL

    Functional description:
        Push next picture in display order into output fifo if any available.

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct
        end_of_stream Indicates whether end of stream has been reached

    Output:
        picture Decoder output picture.

    Return values:
        MP4DEC_OK                   No picture available.
        MP4DEC_PIC_RDY              Picture ready.
        MP4DEC_NOT_INITIALIZED      Decoder instance not initialized yet
------------------------------------------------------------------------------*/
MP4DecRet MP4DecNextPicture_INTERNAL(MP4DecInst dec_inst, MP4DecPicture * picture,
                                     u32 end_of_stream) {

  /* Variables */

  MP4DecRet return_value = MP4DEC_PIC_RDY;
  DecContainer *dec_cont;
  u32 pic_index = BUFFER_UNDEFINED;
  u32 min_count;

  /* Code */

  MP4_API_TRC("MP4DecNextPicture_INTERNAL#\n");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    MP4_API_TRC("MP4DecNextPicture_INTERNAL# ERROR: picture is NULL\n");
    return (MP4DEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    MP4_API_TRC("MP4DecNextPicture_INTERNAL# ERROR: Decoder not initialized\n");
    return (MP4DEC_NOT_INITIALIZED);
  }

  if(dec_cont->ApiStorage.DecStat == HEADERSDECODED)
    end_of_stream = 1;

  min_count = 0;
  if(dec_cont->Hdrs.low_delay == 0 && !end_of_stream)
    min_count = 1;

  if(dec_cont->StrmStorage.parallel_mode2 && !end_of_stream)
    min_count = 2;

  /* Nothing to send out */
  if(dec_cont->StrmStorage.out_count <= min_count) {
    (void) DWLmemset(picture, 0, sizeof(MP4DecPicture));
    picture->output_picture = NULL;
    return_value = MP4DEC_OK;
  } else {
    pic_index = dec_cont->StrmStorage.out_index;
    pic_index = dec_cont->StrmStorage.out_buf[pic_index];

    MP4FillPicStruct(picture, dec_cont, pic_index);

    /* Fill field related */
    //if(MP4_IS_FIELD_OUTPUT)
    if(dec_cont->StrmStorage.p_pic_buf[pic_index].Hdrs.interlaced && !dec_cont->pp_config_query.deinterlace) {
      picture->field_picture = 1;

      if(!dec_cont->ApiStorage.output_other_field) {
        picture->top_field = dec_cont->VopDesc.top_field_first ? 1 : 0;
        dec_cont->ApiStorage.output_other_field = 1;
      } else {
        picture->top_field = dec_cont->VopDesc.top_field_first ? 0 : 1;
        dec_cont->ApiStorage.output_other_field = 0;
        dec_cont->StrmStorage.out_count--;
        dec_cont->StrmStorage.out_index++;
        dec_cont->StrmStorage.out_index &= (MP4_MAX_BUFFERS-1);
      }
    } else {
      /* progressive or deinterlaced frame output */
      picture->top_field = 0;
      picture->field_picture = 0;
      dec_cont->StrmStorage.out_count--;
      dec_cont->StrmStorage.out_index++;
      dec_cont->StrmStorage.out_index &= (MP4_MAX_BUFFERS-1);
    }

#ifdef USE_PICTURE_DISCARD
    if (dec_cont->StrmStorage.p_pic_buf[pic_index].first_show)
#endif
    {
#ifndef USE_PICTURE_DISCARD
      /* wait this buffer as unused */
      if (BqueueWaitBufNotInUse(&dec_cont->StrmStorage.bq, pic_index) != HANTRO_OK)
        return MP4DEC_ABORTED;
      if(dec_cont->pp_enabled) {
        InputQueueWaitBufNotUsed(dec_cont->pp_buffer_queue,dec_cont->StrmStorage.p_pic_buf[pic_index].pp_data->virtual_address);
      }
#endif

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

  if(return_value == MP4DEC_PIC_RDY) {
    MP4_API_TRC("MP4DecNextPicture_INTERNAL# MP4DEC_PIC_RDY\n");
  } else {
    MP4_API_TRC("MP4DecNextPicture_INTERNAL# MP4DEC_OK\n");
  }

  return return_value;

}


/*------------------------------------------------------------------------------

    Function name: MP4DecPictureConsumed

    Functional description:
        Release specific decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to  picture struct


    Return values:
        MP4DEC_PARAM_ERROR         Decoder instance or picture is null
        MP4DEC_NOT_INITIALIZED     Decoder instance isn't initialized
        MP4DEC_OK                          picture release success
------------------------------------------------------------------------------*/
MP4DecRet MP4DecPictureConsumed(MP4DecInst dec_inst, MP4DecPicture * picture) {
  /* Variables */
  DecContainer *dec_cont;
  u32 i;

  /* Code */
  MP4_API_TRC("\nMp4_dec_picture_consumed#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    MP4_API_TRC("MP4DecPictureConsumed# ERROR: picture is NULL");
    return (MP4DEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    MP4_API_TRC("MP4DecPictureConsumed# ERROR: Decoder not initialized");
    return (MP4DEC_NOT_INITIALIZED);
  }

  if (!dec_cont->pp_enabled) {
    for(i = 0; i < dec_cont->StrmStorage.num_buffers; i++) {
      if(picture->output_picture_bus_address == dec_cont->StrmStorage.data[i].bus_address
          && (addr_t)picture->output_picture
          == (addr_t)dec_cont->StrmStorage.data[i].virtual_address) {
        BqueuePictureRelease(&dec_cont->StrmStorage.bq, i);
        return (MP4DEC_OK);
      }
    }
  } else {
    InputQueueReturnBuffer(dec_cont->pp_buffer_queue,(u32 *)picture->output_picture);
    return (MP4DEC_OK);
  }
  return (MP4DEC_PARAM_ERROR);
}


MP4DecRet MP4DecEndOfStream(MP4DecInst dec_inst, u32 strm_end_flag) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;
  MP4DecRet ret;
  MP4DecPicture output;

  MP4_API_TRC("MP4DecEndOfStream#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    MP4_API_TRC("MP4DecEndOfStream# ERROR: Decoder not initialized");
    return (MP4DEC_NOT_INITIALIZED);
  }
  pthread_mutex_lock(&dec_cont->protect_mutex);
  if(dec_cont->dec_stat == MP4DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (MP4DEC_OK);
  }

  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->mp4_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  while((ret = MP4DecNextPicture_INTERNAL(dec_inst, &output, 1)) == MP4DEC_PIC_RDY);
  if(ret == MP4DEC_ABORTED) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (MP4DEC_ABORTED);
  }

  if(strm_end_flag) {
    dec_cont->dec_stat = MP4DEC_END_OF_STREAM;
    FifoPush(dec_cont->fifo_display, -1, FIFO_EXCEPTION_DISABLE);
  }

  //if(dec_cont->pp_instance == NULL && !strm_end_flag)
  //  BqueueWaitNotInUse(&dec_cont->StrmStorage.bq);

  dec_cont->StrmStorage.work0 =
  dec_cont->StrmStorage.work1 = INVALID_ANCHOR_PICTURE;

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  MP4_API_TRC("MP4DecEndOfStream# MP4DEC_OK\n");
  return (MP4DEC_OK);
}

#endif


/*------------------------------------------------------------------------------

    Function name: MP4FillPicStruct

    Functional description:
        Fill data to output pic description

    Input:
        dec_cont    Decoder container
        picture    Pointer to return value struct

    Return values:
        void

------------------------------------------------------------------------------*/
static void MP4FillPicStruct(MP4DecPicture * picture,
                             DecContainer * dec_cont, u32 pic_index) {
  picture_t *p_pic;
#if 0
  picture->frame_width = dec_cont->VopDesc.vop_width << 4;
  picture->frame_height = dec_cont->VopDesc.vop_height << 4;
  picture->interlaced = dec_cont->Hdrs.interlaced;

  picture->coded_width = dec_cont->Hdrs.video_object_layer_width;
  picture->coded_height = dec_cont->Hdrs.video_object_layer_height;
#endif
  p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;

  if (!dec_cont->pp_enabled) {
    picture->frame_width = p_pic[pic_index].VopDesc.vop_width << 4;
    picture->frame_height = p_pic[pic_index].VopDesc.vop_height << 4;
    picture->coded_width = p_pic[pic_index].Hdrs.video_object_layer_width;
    picture->coded_height = p_pic[pic_index].Hdrs.video_object_layer_height;
  } else {
    picture->frame_width = (p_pic[pic_index].VopDesc.vop_width << 4) >> dec_cont->dscale_shift_x;
    picture->frame_height = (p_pic[pic_index].VopDesc.vop_height << 4) >> dec_cont->dscale_shift_y;
    picture->coded_width = p_pic[pic_index].Hdrs.video_object_layer_width >> dec_cont->dscale_shift_x;
    picture->coded_height = p_pic[pic_index].Hdrs.video_object_layer_height >> dec_cont->dscale_shift_y;
  }
  picture->interlaced = p_pic[pic_index].Hdrs.interlaced;

  if (!dec_cont->pp_enabled) {
    picture->output_picture = (u8 *) MP4DecResolveVirtual(dec_cont, pic_index);

    picture->output_picture_bus_address = MP4DecResolveBus(dec_cont, pic_index);
  } else {
    if ((i32)pic_index < 0) {
      picture->output_picture = NULL;
      picture->output_picture_bus_address = 0;
    } else {
      picture->output_picture = (u8 *)dec_cont->StrmStorage.p_pic_buf[pic_index].pp_data->virtual_address;
      picture->output_picture_bus_address = dec_cont->StrmStorage.p_pic_buf[pic_index].pp_data->bus_address;
    }
  }
  picture->key_picture = p_pic[pic_index].pic_type == IVOP;
  picture->pic_id = p_pic[pic_index].pic_id;
  picture->decode_id = p_pic[pic_index].pic_id;
  picture->pic_coding_type = p_pic[pic_index].pic_type;
  picture->nbr_of_err_mbs = p_pic[pic_index].nbr_err_mbs;
  picture->output_format = p_pic[pic_index].tiled_mode ?
                           DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

  (void) DWLmemcpy(&picture->time_code,
                   &p_pic[pic_index].time_code, sizeof(MP4DecTime));

}

/*------------------------------------------------------------------------------

    Function name: MP4SetRegs

    Functional description:
        Set registers

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static u32 MP4SetRegs(DecContainer * dec_container, addr_t strm_bus_address) {

  addr_t tmp = 0;
  i32 itmp;

#ifdef _DEC_PP_USAGE
  MP4DecPpUsagePrint(dec_container, DECPP_UNSPECIFIED,
                     dec_container->StrmStorage.work_out, 1,
                     dec_container->StrmStorage.latest_id);
#endif

  /*
      if(dec_container->Hdrs.interlaced)
          SetDecRegister(dec_container->mp4_regs, HWIF_DEC_OUT_TILED_E, 0);
          */

  SetDecRegister(dec_container->mp4_regs, HWIF_STARTMB_X, 0);
  SetDecRegister(dec_container->mp4_regs, HWIF_STARTMB_Y, 0);
  SetDecRegister(dec_container->mp4_regs, HWIF_BLACKWHITE_E, 0);

  SET_ADDR_REG(dec_container->mp4_regs, HWIF_DEC_OUT_BASE,
               MP4DecResolveBus(dec_container,
                                dec_container->StrmStorage.work_out));

  SetDecRegister( dec_container->mp4_regs, HWIF_PP_PIPELINE_E_U, dec_container->pp_enabled );
  if (dec_container->pp_enabled) {
    u32 dsw, dsh;
#define NEXT_MULTIPLE(value, n) (((value) + (n) - 1) & ~((n) - 1))

    dsw = NEXT_MULTIPLE((dec_container->VopDesc.vop_width * 16 >> dec_container->dscale_shift_x) * 8, 16 * 8) / 8;
    dsh = (dec_container->VopDesc.vop_height * 16 >> dec_container->dscale_shift_y);
    if (dec_container->dscale_shift_x == 0) {
      SetDecRegister(dec_container->mp4_regs, HWIF_HOR_SCALE_MODE_U, 0);
      SetDecRegister(dec_container->mp4_regs, HWIF_WSCALE_INVRA_U, 0);
    } else {
      /* down scale */
      SetDecRegister(dec_container->mp4_regs, HWIF_HOR_SCALE_MODE_U, 2);
      SetDecRegister(dec_container->mp4_regs, HWIF_WSCALE_INVRA_U,
                     1<<(16-dec_container->dscale_shift_x));
    }

    if (dec_container->dscale_shift_y == 0) {
      SetDecRegister(dec_container->mp4_regs, HWIF_VER_SCALE_MODE_U, 0);
      SetDecRegister(dec_container->mp4_regs, HWIF_HSCALE_INVRA_U, 0);
    } else {
      /* down scale */
      SetDecRegister(dec_container->mp4_regs, HWIF_VER_SCALE_MODE_U, 2);
      SetDecRegister(dec_container->mp4_regs, HWIF_HSCALE_INVRA_U,
                     1<<(16-dec_container->dscale_shift_y));
    }
    SET_ADDR64_REG(dec_container->mp4_regs, HWIF_PP_OUT_LU_BASE_U,
                   dec_container->StrmStorage.p_pic_buf[dec_container->
                       StrmStorage.work_out].
                   pp_data->bus_address);
    SET_ADDR64_REG(dec_container->mp4_regs, HWIF_PP_OUT_CH_BASE_U,
                   dec_container->StrmStorage.p_pic_buf[dec_container->
                       StrmStorage.work_out].
                   pp_data->bus_address + dsw * dsh);

    SetPpRegister(dec_container->mp4_regs, HWIF_PP_IN_FORMAT_U, 1);
  }

  SetDecRegister( dec_container->mp4_regs, HWIF_DPB_ILACE_MODE,
                  dec_container->dpb_mode );

  SetDecRegister(dec_container->mp4_regs, HWIF_DEC_OUT_DIS, 0);
  SetDecRegister(dec_container->mp4_regs, HWIF_FILTERING_DIS, 1);

  if(dec_container->VopDesc.vop_coding_type == BVOP) {
    MP4DEC_API_DEBUG(("decoding a B picture\n"));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER0_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work1));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER1_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work1));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER2_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work0));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER3_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work0));
  } else {
    MP4DEC_API_DEBUG(("decoding anchor picture\n"));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER0_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work0));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER1_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work0));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER2_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work0));
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_REFER3_BASE,
                 MP4DecResolveBus(dec_container,
                                  dec_container->StrmStorage.work0));
  }

  SetDecRegister(dec_container->mp4_regs, HWIF_FCODE_FWD_HOR,
                 dec_container->VopDesc.fcode_fwd);
  SetDecRegister(dec_container->mp4_regs, HWIF_FCODE_FWD_VER,
                 dec_container->VopDesc.fcode_fwd);
  SetDecRegister(dec_container->mp4_regs, HWIF_MPEG4_VC1_RC,
                 dec_container->VopDesc.vop_rounding_type);
  SetDecRegister(dec_container->mp4_regs, HWIF_INTRADC_VLC_THR,
                 dec_container->VopDesc.intra_dc_vlc_thr);
  SetDecRegister(dec_container->mp4_regs, HWIF_INIT_QP,
                 dec_container->VopDesc.q_p);
  SetDecRegister(dec_container->mp4_regs, HWIF_SYNC_MARKER_E, 1);
  SetDecRegister(dec_container->mp4_regs, HWIF_PIC_INTER_E,
                 dec_container->VopDesc.vop_coding_type != IVOP ? 1 : 0);

  if(dec_container->rlc_mode && dec_container->VopDesc.vop_coding_type != BVOP) {
    MP4DEC_API_DEBUG(("RLC mode\n"));
    SetDecRegister(dec_container->mp4_regs, HWIF_RLC_MODE_E, 1);
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_RLC_VLC_BASE,
                 dec_container->MbSetDesc.rlc_data_mem.bus_address);
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_MB_CTRL_BASE,
                 dec_container->MbSetDesc.ctrl_data_mem.bus_address);
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_MPEG4_DC_BASE,
                 dec_container->MbSetDesc.DcCoeffMem.bus_address);
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_DIFF_MV_BASE,
                 dec_container->MbSetDesc.mv_data_mem.bus_address);
    SetDecRegister(dec_container->mp4_regs, HWIF_STREAM_LEN, 0);
    SetDecRegister(dec_container->mp4_regs, HWIF_STRM_START_BIT, 0);
  } else {
    SetDecRegister(dec_container->mp4_regs, HWIF_RLC_MODE_E, 0);
    SetDecRegister(dec_container->mp4_regs, HWIF_VOP_TIME_INCR,
                   dec_container->Hdrs.vop_time_increment_resolution);

    /* tmp is strm_bus_address + number of bytes decoded by SW */
    tmp = dec_container->StrmDesc.strm_curr_pos -
          dec_container->StrmDesc.p_strm_buff_start;
    tmp = strm_bus_address + tmp;

    /* bus address must not be zero */
    if(!(tmp & ~0x7)) {
      return HANTRO_NOK;
    }

    /* pointer to start of the stream, mask to get the pointer to
     * previous 64-bit aligned position */
    SET_ADDR_REG(dec_container->mp4_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);

    /* amount of stream (as seen by the HW), obtained as amount of
     * stream given by the application subtracted by number of bytes
     * decoded by SW (if strm_bus_address is not 64-bit aligned -> adds
     * number of bytes from previous 64-bit aligned boundary) */
    SetDecRegister(dec_container->mp4_regs, HWIF_STREAM_LEN,
                   dec_container->StrmDesc.strm_buff_size -
                   ((tmp & ~0x7) - strm_bus_address));
    SetDecRegister(dec_container->mp4_regs, HWIF_STRM_START_BIT,
                   dec_container->StrmDesc.bit_pos_in_word + 8 * (tmp & 0x7));

  }

  /* MPEG-4 ASP */
  SetDecRegister(dec_container->mp4_regs, HWIF_FCODE_BWD_HOR,
                 dec_container->VopDesc.fcode_bwd);
  SetDecRegister(dec_container->mp4_regs, HWIF_FCODE_BWD_VER,
                 dec_container->VopDesc.fcode_bwd);
  SetDecRegister(dec_container->mp4_regs, HWIF_PIC_INTERLACE_E,
                 dec_container->Hdrs.interlaced);
  SetDecRegister(dec_container->mp4_regs, HWIF_PIC_B_E,
                 dec_container->VopDesc.vop_coding_type == BVOP ? 1 : 0);
  SetDecRegister(dec_container->mp4_regs, HWIF_WRITE_MVS_E,
                 (dec_container->VopDesc.vop_coding_type == PVOP ? 1 : 0) &&
                 !dec_container->Hdrs.low_delay);
  SET_ADDR_REG(dec_container->mp4_regs, HWIF_DIR_MV_BASE,
               dec_container->StrmStorage.direct_mvs.bus_address);
  SetDecRegister(dec_container->mp4_regs, HWIF_PREV_ANC_TYPE,
                 dec_container->StrmStorage.p_pic_buf[(i32)dec_container->
                     StrmStorage.work0].
                 pic_type == PVOP);
  SetDecRegister(dec_container->mp4_regs, HWIF_TYPE1_QUANT_E,
                 dec_container->Hdrs.quant_type == 1);
  SET_ADDR_REG(dec_container->mp4_regs, HWIF_QTABLE_BASE,
               dec_container->StrmStorage.quant_mat_linear.bus_address);
  SetDecRegister(dec_container->mp4_regs, HWIF_MV_ACCURACY_FWD,
                 dec_container->Hdrs.quarterpel ? 1 : 0);
  SetDecRegister(dec_container->mp4_regs, HWIF_ALT_SCAN_FLAG_E,
                 dec_container->VopDesc.alt_vertical_scan_flag);
  SetDecRegister(dec_container->mp4_regs, HWIF_TOPFIELDFIRST_E,
                 dec_container->VopDesc.top_field_first);
  if(dec_container->VopDesc.vop_coding_type == BVOP) {
    /*  use 32 bit variables? */
    if(dec_container->VopDesc.trd == 0)
      itmp = 0;
    else
      itmp = (((long long int) dec_container->VopDesc.trb << 27) +
              dec_container->VopDesc.trd - 1) /
             dec_container->VopDesc.trd;

    SetDecRegister(dec_container->mp4_regs, HWIF_TRB_PER_TRD_D0, itmp);

    /* plus 1 */
    itmp = (((long long int) (2 * dec_container->VopDesc.trb + 1) << 27) +
            2 * dec_container->VopDesc.trd) /
           (2 * dec_container->VopDesc.trd + 1);

    SetDecRegister(dec_container->mp4_regs, HWIF_TRB_PER_TRD_D1, itmp);

    /* minus 1 */
    if(dec_container->VopDesc.trd == 0)
      itmp = 0;
    else
      itmp =
        (((long long int) (2 * dec_container->VopDesc.trb - 1) << 27) +
         2 * dec_container->VopDesc.trd -
         2) / (2 * dec_container->VopDesc.trd - 1);

    SetDecRegister(dec_container->mp4_regs, HWIF_TRB_PER_TRD_DM1, itmp);

  }

  if(dec_container->StrmStorage.sorenson_spark)
    SetDecRegister(dec_container->mp4_regs, HWIF_SORENSON_E,
                   dec_container->StrmStorage.sorenson_ver);

  SetConformanceRegs( dec_container );

  /* Setup reference picture buffer */
  if( dec_container->ref_buf_support ) {
    RefbuSetup( &dec_container->ref_buffer_ctrl, dec_container->mp4_regs,
                REFBU_FRAME,
                dec_container->VopDesc.vop_coding_type == IVOP,
                dec_container->VopDesc.vop_coding_type == BVOP,
                0, 2, 0);
  }

  if( dec_container->tiled_mode_support) {
    dec_container->tiled_reference_enable =
      DecSetupTiledReference( dec_container->mp4_regs,
                              dec_container->tiled_mode_support,
                              dec_container->dpb_mode,
                              dec_container->Hdrs.interlaced );
  } else {
    dec_container->tiled_reference_enable = 0;
  }

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    Function name: MP4DecSetupDeinterlace

    Functional description:
        Setup PP interface for deinterlacing

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void MP4DecSetupDeinterlace(DecContainer * dec_cont) {
  if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
    dec_cont->pp_control.pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
    dec_cont->pp_control.bottom_bus_luma = dec_cont->pp_control.input_bus_luma +
                                           (dec_cont->VopDesc.vop_width << 4);
    dec_cont->pp_control.bottom_bus_chroma = dec_cont->pp_control.input_bus_chroma +
        (dec_cont->VopDesc.vop_width << 4);
  } else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
    dec_cont->pp_control.pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD;
    dec_cont->pp_control.bottom_bus_luma = dec_cont->pp_control.input_bus_luma +
                                           (dec_cont->VopDesc.vop_width*dec_cont->VopDesc.vop_height << 7);
    dec_cont->pp_control.bottom_bus_chroma = dec_cont->pp_control.input_bus_chroma +
        (dec_cont->VopDesc.vop_width*dec_cont->VopDesc.vop_height << 6);
  } else {
    ASSERT(0);
  }

}

/*------------------------------------------------------------------------------

    Function name: MP4DecPrepareFieldProcessing

    Functional description:
        Setup PP interface for deinterlacing

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void MP4DecPrepareFieldProcessing(DecContainer * dec_cont, u32 pic_index_override) {
  u32 pic_index;
  dec_cont->pp_control.pic_struct =
    dec_cont->VopDesc.top_field_first ?
    DECPP_PIC_TOP_FIELD_FRAME : DECPP_PIC_BOT_FIELD_FRAME;

  if(pic_index_override != BQUEUE_UNUSED) {
    pic_index = pic_index_override;
  } else if( dec_cont->StrmStorage.parallel_mode2) {
    pic_index = dec_cont->StrmStorage.work_out_prev;
  } else {
    pic_index = dec_cont->StrmStorage.work0;
  }

  if(dec_cont->StrmStorage.work0 < dec_cont->StrmStorage.num_buffers ) {
    dec_cont->ApiStorage.buffer_for_pp = pic_index+1;
  } else {
    ASSERT(0);
  }

  if(dec_cont->VopDesc.top_field_first) {
    dec_cont->pp_control.input_bus_luma =
      MP4DecResolveBus(dec_cont, pic_index);

    dec_cont->pp_control.input_bus_chroma =
      dec_cont->pp_control.input_bus_luma +
      ((dec_cont->VopDesc.vop_width * dec_cont->VopDesc.vop_height) << 8);
  } else {

    MP4_SET_BOT_ADDR(pic_index);

  }

  if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
    dec_cont->pp_control.pic_struct = (dec_cont->VopDesc.top_field_first) ?
                                      DECPP_PIC_FRAME_OR_TOP_FIELD : DECPP_PIC_BOT_FIELD;
  }

  MP4_SET_FIELD_DIMENSIONS;

  MP4DEC_API_DEBUG(("FIELD: send %s\n",
                    dec_cont->pp_control.pic_struct ==
                    DECPP_PIC_TOP_FIELD_FRAME ? "top" : "bottom"));
}

/*------------------------------------------------------------------------------

    Function name: MP4DecParallelPP

    Functional description:
        Setup PP for parallel use

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void MP4DecParallelPP(DecContainer * dec_container, u32 index_for_pp) {

#ifdef _DEC_PP_USAGE
  MP4DecPpUsagePrint(dec_container, DECPP_PARALLEL,
                     dec_container->StrmStorage.work0, 0,
                     dec_container->StrmStorage.p_pic_buf[(i32)dec_container->
                         StrmStorage.work0].
                     pic_id);
#endif

  dec_container->pp_control.use_pipeline = 0;

  dec_container->pp_control.input_bus_luma =
    MP4DecResolveBus(dec_container, index_for_pp);

  dec_container->pp_control.input_bus_chroma =
    dec_container->pp_control.input_bus_luma +
    ((dec_container->VopDesc.vop_width *
      dec_container->VopDesc.vop_height) << 8);

  /* forward tiled mode */
  dec_container->pp_control.tiled_input_mode =
    dec_container->StrmStorage.p_pic_buf[index_for_pp].tiled_mode;
  dec_container->pp_control.progressive_sequence =
    !dec_container->Hdrs.interlaced;

  dec_container->pp_control.inwidth =
    dec_container->pp_control.cropped_w =
      dec_container->VopDesc.vop_width << 4;
  dec_container->pp_control.inheight =
    dec_container->pp_control.cropped_h =
      dec_container->VopDesc.vop_height << 4;

  {
    u32 tmp = GetDecRegister(dec_container->mp4_regs, HWIF_DEC_OUT_ENDIAN);

    dec_container->pp_control.little_endian =
      (tmp == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
  }
  dec_container->pp_control.word_swap =
    GetDecRegister(dec_container->mp4_regs, HWIF_DEC_OUTSWAP32_E) ? 1 : 0;

  if(dec_container->pp_config_query.deinterlace) {
    MP4DecSetupDeinterlace(dec_container);
  }
  /* if field output is used, send only a field to PP */
  else if(dec_container->Hdrs.interlaced) {
    MP4DecPrepareFieldProcessing(dec_container, BQUEUE_UNUSED);
  }

}

/*------------------------------------------------------------------------------

    Function name: PPControl

    Functional description:
        set up and start pp

    Input:
        container
        pipeline_off     override pipeline setting

    Return values:
        void

------------------------------------------------------------------------------*/

static void PPControl(DecContainer * dec_cont, u32 pipeline_off) {
  u32 index_for_pp = BUFFER_UNDEFINED;
  u32 next_buffer_index;

  DecPpInterface * pc = &dec_cont->pp_control;
  DecHdrs        * p_hdrs = &dec_cont->Hdrs;

  dec_cont->pp_config_query.tiled_mode =
    dec_cont->tiled_reference_enable;
  dec_cont->PPConfigQuery(dec_cont->pp_instance,
                          &dec_cont->pp_config_query);

  /* If we have once enabled parallel mode 2, keep it on */
  if(dec_cont->StrmStorage.parallel_mode2)
    pipeline_off = 1;

  PPMultiBufferSetup(dec_cont, (pipeline_off ||
                                !dec_cont->pp_config_query.pipeline_accepted));

  /* Check whether to enable parallel mode 2 */
  if( (!dec_cont->pp_config_query.pipeline_accepted ||
       dec_cont->Hdrs.interlaced) &&
      pc->multi_buf_stat != MULTIBUFFER_DISABLED &&
      !dec_cont->Hdrs.low_delay &&
      !dec_cont->StrmStorage.parallel_mode2 ) {
    dec_cont->StrmStorage.parallel_mode2 = 1;
    dec_cont->StrmStorage.pm2_all_processed_flag = 0;
    dec_cont->StrmStorage.pm2lock_buf    = pc->prev_anchor_display_index;
    dec_cont->StrmStorage.pm2_start_point =
      dec_cont->VopDesc.vop_number;
  }

  dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].
  send_to_pp = 1;

  /* Select new PP buffer index to use. If multibuffer is disabled, use
   * previous buffer, otherwise select new buffer from queue. */
  if(pc->multi_buf_stat != MULTIBUFFER_DISABLED) {
    u32 buf0 = BQUEUE_UNUSED;
    /* In parallel mode 2, we must refrain from reusing future
     * anchor frame output buffer until it has been put out. */
    if(dec_cont->StrmStorage.parallel_mode2)
      buf0 = dec_cont->StrmStorage.pm2lock_buf;
    next_buffer_index = BqueueNext( &dec_cont->StrmStorage.bq_pp,
                                    buf0,
                                    BQUEUE_UNUSED,
                                    BQUEUE_UNUSED,
                                    dec_cont->VopDesc.vop_coding_type == BVOP );
    pc->buffer_index = next_buffer_index;
  } else {
    next_buffer_index = pc->buffer_index = 0;
  }

  if(dec_cont->StrmStorage.parallel_mode2) {
    if(dec_cont->StrmStorage.previous_b) {
      pc->display_index = pc->buffer_index;
    } else {
      pc->display_index = dec_cont->StrmStorage.pm2lock_buf;
      /* Fix for case if stream has only one frame; then for NextPicture()
       * we should have a proper buffer index instead of undefined. */
      if(pc->display_index == BQUEUE_UNUSED)
        pc->display_index = pc->buffer_index;
    }
  } else if(p_hdrs->low_delay ||
            dec_cont->VopDesc.vop_coding_type == BVOP) {
    pc->display_index = pc->buffer_index;
  } else {
    pc->display_index = pc->prev_anchor_display_index;
  }

  /* Connect PP output buffer to decoder output buffer */
  {
    addr_t luma = 0;
    addr_t chroma = 0;
    addr_t bot_luma = 0, bot_chroma = 0;
    u32 work_buffer;

    if(dec_cont->StrmStorage.parallel_mode2)
      work_buffer = dec_cont->StrmStorage.work_out_prev;
    else
      work_buffer = dec_cont->StrmStorage.work_out;

    luma = MP4DecResolveBus(dec_cont, work_buffer);
    chroma = luma + ((dec_cont->VopDesc.vop_width *
                      dec_cont->VopDesc.vop_height) << 8);

    if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
      bot_luma = luma + (dec_cont->VopDesc.vop_width * 16);
      bot_chroma = chroma + (dec_cont->VopDesc.vop_width * 16);
    } else if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
      bot_luma = luma + ((dec_cont->VopDesc.vop_width * 16) *
                         (dec_cont->VopDesc.vop_height * 8));
      bot_chroma = chroma + ((dec_cont->VopDesc.vop_width * 16) *
                             (dec_cont->VopDesc.vop_height * 4));
    }

    dec_cont->PPBufferData(dec_cont->pp_instance,
                           pc->buffer_index, luma, chroma, bot_luma, bot_chroma );
  }

  if(pc->multi_buf_stat == MULTIBUFFER_FULLMODE) {
    MP4DEC_API_DEBUG(("Full pipeline# \n"));
    pc->use_pipeline =
      dec_cont->pp_config_query.pipeline_accepted;
    MP4DecRunFullmode(dec_cont);
    dec_cont->StrmStorage.previous_mode_full = 1;
  } else if(dec_cont->StrmStorage.previous_mode_full == 1) {
    if(dec_cont->VopDesc.vop_coding_type == BVOP) {
      dec_cont->StrmStorage.previous_b = 1;
    } else {
      dec_cont->StrmStorage.previous_b = 0;
    }

    if(dec_cont->VopDesc.vop_coding_type == BVOP) {
      MP4DEC_API_DEBUG(("PIPELINE OFF, DON*T SEND B TO PP\n"));
      index_for_pp = BUFFER_UNDEFINED;
      pc->input_bus_luma = 0;
    }
    pc->pp_status = DECPP_IDLE;

    dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work0].send_to_pp = 1;

    dec_cont->StrmStorage.previous_mode_full = 0;
  } else {
    /* store previous B status */
    u32 previous_b = dec_cont->StrmStorage.previous_b;

    if(!dec_cont->StrmStorage.parallel_mode2) {
      pc->buffer_index = pc->display_index;
    }

    if(dec_cont->VopDesc.vop_coding_type == BVOP) {
      dec_cont->StrmStorage.previous_b = 1;
    } else {
      dec_cont->StrmStorage.previous_b = 0;
    }

    if((!p_hdrs->low_delay && (dec_cont->VopDesc.vop_coding_type != BVOP)) ||
        p_hdrs->interlaced ||
        (!dec_cont->VopDesc.vop_coded && !dec_cont->rlc_mode) ||
        pipeline_off) {
      pc->use_pipeline = 0;
    } else {
      MP4DEC_API_DEBUG(("RUN PP  # \n"));
      pc->use_pipeline =
        dec_cont->pp_config_query.pipeline_accepted;
    }

    if(!pc->use_pipeline) {
      /* pipeline not accepted, don't run for first picture */
      if(dec_cont->VopDesc.vop_number_in_seq) {

        /* In parallel mode 2 we always run previous decoder output
         * picture through PP */
        if( dec_cont->StrmStorage.parallel_mode2) {
          pc->input_bus_luma =
            MP4DecResolveBus(dec_cont, dec_cont->StrmStorage.work_out_prev);

          /* If we got an anchor frame, lock the PP output buffer */
          if(!previous_b) {
            dec_cont->StrmStorage.pm2lock_buf = pc->buffer_index;
          }

          /* forward tiled mode */
          dec_cont->pp_control.tiled_input_mode =
            dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work_out_prev].
            tiled_mode;
          dec_cont->pp_control.progressive_sequence =
            !dec_cont->Hdrs.interlaced;

          pc->input_bus_chroma =
            pc->input_bus_luma +
            ((dec_cont->VopDesc.vop_width *
              dec_cont->VopDesc.vop_height) << 8);

          pc->inwidth = pc->cropped_w =
                          dec_cont->VopDesc.vop_width << 4;
          pc->inheight = pc->cropped_h =
                           dec_cont->VopDesc.vop_height << 4;
          {
            u32 value = GetDecRegister(dec_cont->mp4_regs,
                                       HWIF_DEC_OUT_ENDIAN);

            pc->little_endian =
              (value == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
          }
          pc->word_swap =
            GetDecRegister(dec_cont->mp4_regs,
                           HWIF_DEC_OUTSWAP32_E) ? 1 : 0;

          MP4DEC_API_DEBUG(("sending NON B to pp\n"));
          index_for_pp = dec_cont->StrmStorage.work_out_prev;

          if(dec_cont->pp_config_query.deinterlace) {
            MP4DecSetupDeinterlace(dec_cont);
          }
          /* if field output is used, send only a field to PP */
          else if(p_hdrs->interlaced) {
            MP4DecPrepareFieldProcessing(dec_cont,
                                         BQUEUE_UNUSED);
          }
          dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp = 1;
        }

        /*if:
         * B pictures allowed and non B picture OR
         * B pictures not allowed. must be coded*/
        else if(((!p_hdrs->low_delay &&
                  dec_cont->VopDesc.vop_coding_type != BVOP) ||
                 p_hdrs->low_delay) &&
                dec_cont->VopDesc.vop_coded) {
          MP4DEC_API_DEBUG(("sending NON B to pp\n"));
          index_for_pp = dec_cont->StrmStorage.work0;
          /* set up parallel pp run */
          MP4DecParallelPP(dec_cont, index_for_pp);
        } else {
          /* If not coded BVOP, send previous NON B to PP */
          if(dec_cont->VopDesc.vop_coding_type == BVOP &&
              dec_cont->VopDesc.vop_coded) {
            MP4DEC_API_DEBUG(("PIPELINE OFF, DON*T SEND B TO PP\n"));
            index_for_pp = BUFFER_UNDEFINED;
            pc->input_bus_luma = 0;
          } else {
            MP4DEC_API_DEBUG(("sending NON B to pp\n"));
            index_for_pp = dec_cont->StrmStorage.work_out;
            /* set up parallel pp run */
            MP4DecParallelPP(dec_cont, index_for_pp);
          }
        }
      } else {
        pc->input_bus_luma = 0;
      }
    } else {
#ifdef _DEC_PP_USAGE
      MP4DecPpUsagePrint(dec_cont, DECPP_PIPELINED,
                         dec_cont->StrmStorage.work_out, 0,
                         dec_cont->StrmStorage.p_pic_buf[dec_cont->
                             StrmStorage.
                             work_out].pic_id);
#endif

      pc->input_bus_luma = pc->input_bus_chroma = 0;
      index_for_pp = dec_cont->StrmStorage.work_out;
      pc->inwidth = pc->cropped_w =
                      dec_cont->VopDesc.vop_width << 4;
      pc->inheight = pc->cropped_h =
                       dec_cont->VopDesc.vop_height << 4;
      pc->tiled_input_mode = dec_cont->tiled_reference_enable;
      pc->progressive_sequence =
        !dec_cont->Hdrs.interlaced;
    }

    /* start PP */
    if(((pc->input_bus_luma && !pc->use_pipeline) ||
        (!pc->input_bus_luma && pc->use_pipeline))
        && dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp) {
      {
        u32 fdis = 1;

        if(pc->use_pipeline) {
          if(dec_cont->VopDesc.vop_coding_type == BVOP)
            SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_OUT_DIS, 1);

          fdis = dec_cont->ApiStorage.disable_filter;
        }
        SetDecRegister(dec_cont->mp4_regs, HWIF_FILTERING_DIS, fdis);

      }

      if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD &&
          !dec_cont->pp_config_query.deinterlace) {
        dec_cont->pp_control.pic_struct = (dec_cont->VopDesc.
                                           top_field_first) ?
                                          DECPP_PIC_FRAME_OR_TOP_FIELD : DECPP_PIC_BOT_FIELD;
      }

      ASSERT(index_for_pp != BUFFER_UNDEFINED);
      MP4DEC_API_DEBUG(("sent to pp %d\n", index_for_pp));

      MP4DEC_API_DEBUG(("send %d %d %d %d, index_for_pp %d\n",
                        dec_cont->StrmStorage.p_pic_buf[0].send_to_pp,
                        dec_cont->StrmStorage.p_pic_buf[1].send_to_pp,
                        dec_cont->StrmStorage.p_pic_buf[2].send_to_pp,
                        dec_cont->StrmStorage.p_pic_buf[3].send_to_pp,
                        index_for_pp));

      ASSERT(dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp == 1);
      dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp = 0;

      dec_cont->PPRun(dec_cont->pp_instance,
                      pc);

      pc->pp_status = DECPP_RUNNING;
    }
    dec_cont->StrmStorage.previous_mode_full = 0;
  }

  if( dec_cont->VopDesc.vop_coding_type != BVOP ) {
    pc->prev_anchor_display_index = next_buffer_index;
  }

  if( pc->input_bus_luma == 0 &&
      dec_cont->StrmStorage.parallel_mode2 ) {
    BqueueDiscard( &dec_cont->StrmStorage.bq_pp,
                   pc->buffer_index );
  }

  /* Clear 2nd field indicator from structure if in parallel mode 2 and
   * outputting separate fields and output buffer not yet filled. This
   * prevents NextPicture from going bonkers if stream ends before
   * frame 2. */

  if(dec_cont->VopDesc.vop_number -
      dec_cont->StrmStorage.pm2_start_point < 2 &&
      dec_cont->StrmStorage.parallel_mode2 &&
      dec_cont->Hdrs.interlaced &&
      !dec_cont->pp_config_query.deinterlace ) {
    dec_cont->ApiStorage.buffer_for_pp = NO_BUFFER;
  }

}

/*------------------------------------------------------------------------------

    Function name: MP4Setindexes

    Functional description:
        set up index bank for this picture

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void MP4SetIndexes(DecContainer * dec_cont) {

  dec_cont->StrmStorage.work_out_prev =
    dec_cont->StrmStorage.work_out;

  if(dec_cont->VopDesc.vop_coded) {
#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->pp_instance == NULL) {
      dec_cont->StrmStorage.work_out = BqueueNext2(
                                         &dec_cont->StrmStorage.bq,
                                         dec_cont->StrmStorage.work0,
                                         dec_cont->Hdrs.low_delay ? BQUEUE_UNUSED : dec_cont->StrmStorage.work1,
                                         BQUEUE_UNUSED,
                                         dec_cont->VopDesc.vop_coding_type == BVOP );
      if(dec_cont->StrmStorage.work_out == (u32)0xFFFFFFFFU)
        return;
      dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].first_show = 1;
    } else {
      dec_cont->StrmStorage.work_out = BqueueNext(
                                         &dec_cont->StrmStorage.bq,
                                         dec_cont->StrmStorage.work0,
                                         dec_cont->Hdrs.low_delay ? BQUEUE_UNUSED : dec_cont->StrmStorage.work1,
                                         BQUEUE_UNUSED,
                                         dec_cont->VopDesc.vop_coding_type == BVOP );
    }
#else
    dec_cont->StrmStorage.work_out = BqueueNext(
                                       &dec_cont->StrmStorage.bq,
                                       dec_cont->StrmStorage.work0,
                                       dec_cont->Hdrs.low_delay ? BQUEUE_UNUSED : dec_cont->StrmStorage.work1,
                                       BQUEUE_UNUSED,
                                       dec_cont->VopDesc.vop_coding_type == BVOP );
#endif
    if (dec_cont->pp_enabled) {
      dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].pp_data = InputQueueGetBuffer(dec_cont->pp_buffer_queue, 1);
    }
  } else {
    if(dec_cont->VopDesc.vop_coding_type != BVOP) {
      dec_cont->StrmStorage.work_out =
        dec_cont->StrmStorage.work0;
    } else {
      dec_cont->StrmStorage.work_out =
        dec_cont->StrmStorage.work1;
    }
  }
  if(dec_cont->VopDesc.vop_coding_type == BVOP) {
    dec_cont->StrmStorage.prev_bidx = dec_cont->StrmStorage.work_out;
  }

  if(dec_cont->StrmStorage.previous_not_coded) {
    if(dec_cont->VopDesc.vop_coding_type != BVOP) {
      /*
      dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].
          data_index =
        dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work0].
          data_index;*/
      dec_cont->StrmStorage.previous_not_coded = 0;
    }
  }
}

/*------------------------------------------------------------------------------

    Function name: MP4CheckFormatSupport

    Functional description:
        Check if mpeg4 or sorenson are supported

    Input:
        container

    Return values:
        return zero for OK

------------------------------------------------------------------------------*/
static u32 MP4CheckFormatSupport(MP4DecStrmFmt strm_fmt) {
  u32 id = 0;
  u32 product = 0;
  DWLHwConfig hw_config;

  id = DWLReadAsicID(DWL_CLIENT_TYPE_MPEG4_DEC);

  product = id >> 16;

  if(product < 0x8170 &&
      product != 0x6731 )
    return ~0;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_MPEG4_DEC);

  if(strm_fmt == MP4DEC_SORENSON)
    return (hw_config.sorenson_spark_support == SORENSON_SPARK_NOT_SUPPORTED);
  else
    return (hw_config.mpeg4_support == MPEG4_NOT_SUPPORTED);
}

/*------------------------------------------------------------------------------

    Function name: MP4DecFilterDisable

    Functional description:
        check possibility to use filter

    Input:
        container

    Return values:
        returns nonzero for disable

------------------------------------------------------------------------------*/
#if 0
static u32 MP4DecFilterDisable(DecContainer * dec_cont) {
  u32 ret = 0;

  /* combined mode must be enabled */
  if(dec_cont->pp_instance == NULL)
    ret++;

  if(!dec_cont->Hdrs.low_delay)
    ret++;

  if(dec_cont->Hdrs.interlaced)
    ret++;

  return ret;
}
#endif

/*------------------------------------------------------------------------------

    Function name: MP4DecFieldAndValidBuffer

    Functional description:
        set picture info for field data for second field

    Input:
        container, picture desc, id

    Return values:
        void

------------------------------------------------------------------------------*/
static void MP4DecFieldAndValidBuffer(MP4DecPicture * picture,
                                      DecContainer * dec_cont, u32* pic_index ) {

  picture_t *p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf;
  picture->interlaced = 1;
  picture->field_picture = 1;
  dec_cont->pp_control.pic_struct =
    dec_cont->VopDesc.
    top_field_first ? DECPP_PIC_BOT_FIELD_FRAME :
    DECPP_PIC_TOP_FIELD_FRAME;
  picture->top_field = dec_cont->VopDesc.top_field_first ? 0 : 1;

  if(dec_cont->ApiStorage.buffer_for_pp >= 1)
    *pic_index = dec_cont->ApiStorage.buffer_for_pp - 1;
  else
    *pic_index = 0;

  dec_cont->ApiStorage.buffer_for_pp = NO_BUFFER;
  p_pic[*pic_index].send_to_pp = 1;
  MP4DEC_API_DEBUG(("Processing second field in NextPicture index %d\n", *pic_index));

}

/*------------------------------------------------------------------------------

    Function name: PPMultiBufferInit

    Functional description:
        Modify state of pp output buffering.

    Input:
        container
        pipeline_off     override pipeline setting

    Return values:
        void

------------------------------------------------------------------------------*/
static void PPMultiBufferInit(DecContainer * dec_cont) {
  DecPpQuery * pq = &dec_cont->pp_config_query;
  DecPpInterface * pc = &dec_cont->pp_control;

  if(pq->multi_buffer) {
    if(!pq->pipeline_accepted || dec_cont->Hdrs.interlaced) {

      MP4DEC_API_DEBUG(("MULTIBUFFER_SEMIMODE\n"));
      pc->multi_buf_stat = MULTIBUFFER_SEMIMODE;
    } else {
      MP4DEC_API_DEBUG(("MULTIBUFFER_FULLMODE\n"));
      pc->multi_buf_stat = MULTIBUFFER_FULLMODE;
    }
  } else {
    pc->multi_buf_stat = MULTIBUFFER_DISABLED;
  }

}

/*------------------------------------------------------------------------------

    Function name: PPMultiBufferSetup

    Functional description:
        Modify state of pp output buffering.

    Input:
        container
        pipeline_off     override pipeline setting

    Return values:
        void

------------------------------------------------------------------------------*/
static void PPMultiBufferSetup(DecContainer * dec_cont, u32 pipeline_off) {

  if( dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_DISABLED) {
    MP4DEC_API_DEBUG(("MULTIBUFFER_DISABLED\n"));
    return;
  }

  if(pipeline_off &&
      (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_FULLMODE)) {
    MP4DEC_API_DEBUG(("MULTIBUFFER_SEMIMODE\n"));
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_SEMIMODE;
  }

  if(!pipeline_off && !dec_cont->Hdrs.interlaced &&
      (dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_SEMIMODE)) {
    MP4DEC_API_DEBUG(("MULTIBUFFER_FULLMODE\n"));
    dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_FULLMODE;
  }

  if(dec_cont->pp_control.multi_buf_stat == MULTIBUFFER_UNINIT)
    PPMultiBufferInit(dec_cont);

}

/*------------------------------------------------------------------------------

    Function name: MP4DecRunFullmode

    Functional description:


    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void MP4DecRunFullmode(DecContainer * dec_cont) {
  u32 index_for_pp = BUFFER_UNDEFINED;
  DecPpInterface * pc = &dec_cont->pp_control;

#ifdef _DEC_PP_USAGE
  MP4DecPpUsagePrint(dec_cont, DECPP_PIPELINED,
                     dec_cont->StrmStorage.work_out, 0,
                     dec_cont->StrmStorage.p_pic_buf[dec_cont->
                         StrmStorage.
                         work_out].pic_id);
#endif

  if(!dec_cont->StrmStorage.previous_mode_full && dec_cont->VopDesc.vop_number) {
    if(dec_cont->VopDesc.vop_coding_type == BVOP) {
      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work0].send_to_pp = 0;
      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work1].send_to_pp = 0;
    } else {
      dec_cont->StrmStorage.p_pic_buf[(i32)dec_cont->StrmStorage.work0].send_to_pp = 0;
    }
  }

  if(dec_cont->VopDesc.vop_coding_type == BVOP) {
    dec_cont->StrmStorage.previous_b = 1;
  } else {
    dec_cont->StrmStorage.previous_b = 0;
  }

  index_for_pp = dec_cont->StrmStorage.work_out;
  pc->tiled_input_mode = dec_cont->tiled_reference_enable;
  pc->progressive_sequence =
    !dec_cont->Hdrs.interlaced;

  pc->input_bus_luma = MP4DecResolveBus(dec_cont, dec_cont->StrmStorage.work_out);
  pc->input_bus_chroma =  pc->input_bus_luma +
                          ((dec_cont->VopDesc.vop_width *
                            dec_cont->VopDesc.vop_height) << 8);

  pc->inwidth = pc->cropped_w =
                  dec_cont->VopDesc.vop_width << 4;
  pc->inheight = pc->cropped_h =
                   dec_cont->VopDesc.vop_height << 4;

  {
    u32 fdis = 1;

    if(dec_cont->VopDesc.vop_coding_type == BVOP)
      SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_OUT_DIS, 1);

    fdis = dec_cont->ApiStorage.disable_filter;
    SetDecRegister(dec_cont->mp4_regs, HWIF_FILTERING_DIS, fdis);
  }

  ASSERT(index_for_pp != BUFFER_UNDEFINED);
  MP4DEC_API_DEBUG(("sent to pp %d\n", index_for_pp));

  MP4DEC_API_DEBUG(("send %d %d %d %d, index_for_pp %d\n",
                    dec_cont->StrmStorage.p_pic_buf[0].send_to_pp,
                    dec_cont->StrmStorage.p_pic_buf[1].send_to_pp,
                    dec_cont->StrmStorage.p_pic_buf[2].send_to_pp,
                    dec_cont->StrmStorage.p_pic_buf[3].send_to_pp,
                    index_for_pp));

  ASSERT(dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp == 1);
  dec_cont->StrmStorage.p_pic_buf[index_for_pp].send_to_pp = 0;
  dec_cont->PPRun(dec_cont->pp_instance,
                  pc);

  pc->pp_status = DECPP_RUNNING;

}

/*------------------------------------------------------------------------------

    Function name: MP4DecPeek

    Functional description:
        Retrieve last decoded picture

    Input:
        dec_inst     Reference to decoder instance.
        picture    Pointer to return value struct

    Output:
        picture Decoder output picture.

    Return values:
        MP4DEC_OK         No picture available.
        MP4DEC_PIC_RDY    Picture ready.

------------------------------------------------------------------------------*/
MP4DecRet MP4DecPeek(MP4DecInst dec_inst, MP4DecPicture * picture) {
  /* Variables */
  MP4DecRet return_value = MP4DEC_PIC_RDY;
  DecContainer *dec_cont;
  u32 pic_index = BUFFER_UNDEFINED;

  /* Code */
  MP4_API_TRC("\nMp4_dec_peek#");

  /* Check that function input parameters are valid */
  if(picture == NULL) {
    MP4_API_TRC("MP4DecPeek# ERROR: picture is NULL");
    return (MP4DEC_PARAM_ERROR);
  }

  dec_cont = (DecContainer *) dec_inst;

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    MP4_API_TRC("MP4DecPeek# ERROR: Decoder not initialized");
    return (MP4DEC_NOT_INITIALIZED);
  }

#ifdef USE_OUTPUT_RELEASE
  /* when output release thread enabled, MP4DecNextPicture_INTERNAL() called in
     MP4DecDecode(), and "dec_cont->StrmStorage.out_count--" may called in
     MP4DecNextPicture_INTERNAL() before MP4DecPeek() called, so dec_cont->fullness
     used to sample the real out_count in case of MP4DecNextPicture_INTERNAL() called
     before than MP4DecPeek() */
  u32 tmp = dec_cont->fullness;
#else
  u32 tmp = dec_cont->StrmStorage.out_count;
#endif

  /* no output pictures available */
  if(!tmp) {
    (void) DWLmemset(picture, 0, sizeof(MP4DecPicture));
    picture->output_picture = NULL;
    picture->interlaced = dec_cont->Hdrs.interlaced;
    /* print nothing to send out */
    return_value = MP4DEC_OK;
  } else {
    /* output current (last decoded) picture */
    pic_index = dec_cont->StrmStorage.work_out;

    MP4FillPicStruct(picture, dec_cont, pic_index);

    /* frame output */
    picture->field_picture = 0;
    picture->top_field = 0;
  }

  return return_value;
}

#ifdef USE_EXTERNAL_BUFFER
void MP4SetExternalBufferInfo(MP4DecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *)dec_inst;
  u32 pic_size_in_mbs = 0;
  u32 ext_buffer_size;

  if(dec_cont->pp_instance)
    pic_size_in_mbs = dec_cont->VopDesc.vop_width *
                      ((dec_cont->VopDesc.vop_height + 1) / 2) * 2;
  else
    pic_size_in_mbs = dec_cont->VopDesc.total_mb_in_vop;
  u32 pic_size = pic_size_in_mbs * 384;

  u32 ref_buff_size = pic_size;
  ext_buffer_size = ref_buff_size;

  u32 buffers = 3;

  dec_cont->StrmStorage.parallel_mode2 = 0;
  dec_cont->pp_control.prev_anchor_display_index = BQUEUE_UNUSED;

  if( dec_cont->pp_instance ) { /* Combined mode used */
    dec_cont->pp_config_query.tiled_mode =
      dec_cont->tiled_reference_enable;
    dec_cont->PPConfigQuery(dec_cont->pp_instance,
                            &dec_cont->pp_config_query);
    if(dec_cont->pp_config_query.multi_buffer)
      buffers = 4;
  } else { /* Dec only or separate PP */
    buffers = dec_cont->StrmStorage.max_num_buffers;
    if( buffers < 3 )
      buffers = 3;
  }

  dec_cont->tot_buffers_added = dec_cont->tot_buffers;

  if(dec_cont->VopDesc.total_mb_in_vop * 384 >
      (dec_cont->use_adaptive_buffers ?
       dec_cont->n_ext_buf_size : dec_cont->old_mb_in_vop * 384))
    dec_cont->no_reallocation = 0;

  if (dec_cont->pp_enabled) {
    u32 pp_width, pp_height, pp_stride, pp_buff_size;
    pp_width = (dec_cont->VopDesc.vop_width * 16) >> dec_cont->dscale_shift_x;
    pp_height = (dec_cont->VopDesc.vop_height * 16) >> dec_cont->dscale_shift_y;
    pp_stride = ((pp_width + 15) >> 4) << 4;
    pp_buff_size = pp_stride * pp_height * 3 / 2;
    ext_buffer_size = pp_buff_size;
  }

  dec_cont->old_mb_in_vop = dec_cont->VopDesc.total_mb_in_vop;
  dec_cont->tot_buffers = dec_cont->buf_num =  buffers;
  dec_cont->next_buf_size = ext_buffer_size;
}

MP4DecRet MP4DecGetBufferInfo(MP4DecInst dec_inst, MP4DecBufferInfo *mem_info) {
  DecContainer  * dec_cont = (DecContainer *)dec_inst;

  struct DWLLinearMem empty = {0, 0, 0, 0};

  struct DWLLinearMem *buffer = NULL;

  if(dec_cont == NULL || mem_info == NULL) {
    return MP4DEC_PARAM_ERROR;
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
        return (MP4DEC_MEMFAIL);
      }
      dec_cont->StrmStorage.ext_buffer_added = 0;
      mem_info->buf_to_free = empty;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return MP4DEC_OK;
    } else {
      mem_info->buf_to_free = *buffer;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return MP4DEC_WAITING_FOR_BUFFER;
    }
  }

  if(dec_cont->buf_to_free == NULL && dec_cont->next_buf_size == 0) {
    /* External reference buffer: release done. */
    mem_info->buf_to_free = empty;
    mem_info->next_buf_size = dec_cont->next_buf_size;
    mem_info->buf_num = dec_cont->buf_num;
    return MP4DEC_OK;
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

  return MP4DEC_OK;;
}

MP4DecRet MP4DecAddBuffer(MP4DecInst dec_inst, struct DWLLinearMem *info) {
  DecContainer *dec_cont = (DecContainer *)dec_inst;
  MP4DecRet dec_ret = MP4DEC_OK;

  if(dec_inst == NULL || info == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(info->virtual_address) ||
      X170_CHECK_BUS_ADDRESS_AGLINED(info->bus_address) ||
      info->size < dec_cont->next_buf_size) {
    return MP4DEC_PARAM_ERROR;
  }

  u32 i = dec_cont->buffer_index;

  dec_cont->n_ext_buf_size = info->size;
  dec_cont->ext_buffers[dec_cont->ext_buffer_num] = *info;
  dec_cont->ext_buffer_num++;

  if (dec_cont->pp_enabled == 0) {
    if(i < dec_cont->tot_buffers) {
      dec_cont->StrmStorage.data[i] = *info;
      dec_cont->StrmStorage.p_pic_buf[i].data_index = i;
      dec_cont->buffer_index++;
      if(dec_cont->buffer_index < dec_cont->tot_buffers)
        dec_ret = MP4DEC_WAITING_FOR_BUFFER;
    } else {
      /* Adding extra buffers. */
      if(i >= MP4_MAX_BUFFERS) {
        /* Too much buffers added. */
        return MP4DEC_EXT_BUFFER_REJECTED;
      }

      dec_cont->StrmStorage.data[i] = *info;
      dec_cont->StrmStorage.p_pic_buf[i].data_index = i;
      dec_cont->buffer_index++;
      dec_cont->tot_buffers++;
      dec_cont->StrmStorage.num_buffers++;
      dec_cont->StrmStorage.bq.queue_size++;
    }
  } else {
    /* Add down scale buffer. */
    InputQueueAddBuffer(dec_cont->pp_buffer_queue, info);
  }
  return dec_ret;
}

#endif

#ifdef USE_OUTPUT_RELEASE
void MP4EnterAbortState(DecContainer *dec_cont) {
  BqueueSetAbort(&dec_cont->StrmStorage.bq);
#ifdef USE_OMXIL_BUFFER
  FifoSetAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 1;
}

void MP4ExistAbortState(DecContainer *dec_cont) {
  BqueueClearAbort(&dec_cont->StrmStorage.bq);
#ifdef USE_OMXIL_BUFFER
  FifoClearAbort(dec_cont->fifo_display);
#endif
  dec_cont->abort = 0;
}

void MP4EmptyBufferQueue(DecContainer *dec_cont) {
  BqueueEmpty(&dec_cont->StrmStorage.bq);
  dec_cont->VopDesc.vop_number_in_seq = 0;
  dec_cont->StrmStorage.work_out_prev = 0;
  dec_cont->StrmStorage.work_out = 0;
  dec_cont->StrmStorage.work0 =
    dec_cont->StrmStorage.work1 = INVALID_ANCHOR_PICTURE;
}

void MP4StateReset(DecContainer *dec_cont) {
  u32 buffers = 3;

  if( dec_cont->pp_instance ) { /* Combined mode used */
    dec_cont->PPConfigQuery(dec_cont->pp_instance,
                            &dec_cont->pp_config_query);
    if(dec_cont->pp_config_query.multi_buffer)
      buffers = 4;
  } else { /* Dec only or separate PP */
    buffers = dec_cont->StrmStorage.max_num_buffers;
    if( buffers < 3 )
      buffers = 3;
  }

  /* Clear internal parameters in DecContainer */
#ifdef CLEAR_HDRINFO_IN_SEEK
  dec_cont->rlc_mode = 0;
  dec_cont->packed_mode = 0;
#endif

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
  dec_cont->dec_stat = MP4DEC_OK;
  dec_cont->same_vop_header = 0;

  /* Clear internal parameters in DecStrmStorage*/
  dec_cont->StrmStorage.status = STATE_OK;
  dec_cont->StrmStorage.vp_mb_number = 0;
  dec_cont->StrmStorage.vp_num_mbs = 0;
  dec_cont->StrmStorage.vp_first_coded_mb = 0;
  dec_cont->StrmStorage.skip_b = 0;
  dec_cont->StrmStorage.gob_resync_flag = HANTRO_FALSE;
  dec_cont->StrmStorage.p_last_sync = NULL;
  dec_cont->StrmStorage.start_code_loss = HANTRO_FALSE;
  dec_cont->StrmStorage.valid_vop_header = HANTRO_FALSE;
#ifdef CLEAR_HDRINFO_IN_SEEK
  dec_cont->StrmStorage.strm_dec_ready = 0;
  dec_cont->StrmStorage.short_video = HANTRO_FALSE;
#endif
  dec_cont->StrmStorage.num_err_mbs = 0;
#ifdef USE_OMXIL_BUFFER
  dec_cont->StrmStorage.bq.queue_size = buffers;
  dec_cont->StrmStorage.num_buffers = buffers;
#endif
  dec_cont->StrmStorage.out_index = 0;
  dec_cont->StrmStorage.out_count = 0;
  dec_cont->StrmStorage.vp_qp = 1;
  dec_cont->StrmStorage.previous_not_coded = 0;
  dec_cont->StrmStorage.previous_b = 0;
  dec_cont->StrmStorage.unsupported_features_present = 0;
  dec_cont->StrmStorage.picture_broken = 0;
  dec_cont->StrmStorage.previous_mode_full = 0;
  dec_cont->StrmStorage.prev_bidx = 0;
  dec_cont->StrmStorage.reload_quant_matrices = HANTRO_FALSE;
  dec_cont->StrmStorage.gov_time_increment = 0;

  /* Clear internal parameters in DecApiStorage */
  dec_cont->ApiStorage.DecStat = INITIALIZED;
  dec_cont->ApiStorage.output_other_field = 0;

  /* Clear internal parameters in DecVopDesc */
  dec_cont->VopDesc.vop_number_in_seq = 0;
  dec_cont->VopDesc.vop_number = 0;
  dec_cont->VopDesc.vop_rounding_type = 0;
  dec_cont->VopDesc.fcode_fwd = 1;
  dec_cont->VopDesc.intra_dc_vlc_thr = 0;
  dec_cont->VopDesc.vop_coded = 1;
#ifdef CLEAR_HDRINFO_IN_SEEK
  dec_cont->VopDesc.vop_time_increment = 0;
  dec_cont->VopDesc.modulo_time_base = 0;
  dec_cont->VopDesc.prev_vop_time_increment = 0;
  dec_cont->VopDesc.prev_modulo_time_base = 0;
  dec_cont->VopDesc.tics_from_prev = 0;
  dec_cont->VopDesc.time_code_hours = 0;
  dec_cont->VopDesc.time_code_minutes = 0;
  dec_cont->VopDesc.time_code_seconds = 0;
  dec_cont->VopDesc.gov_counter = 0;
#endif

  /* Clear internal parameters in DecMbSetDesc */
  dec_cont->MbSetDesc.odd_rlc_vp = 0;
  dec_cont->MbSetDesc.odd_rlc = 0;

  /* Clear internal parameters in DecSvDesc_t */
  dec_cont->SvDesc.gob_frame_id = 0;
  dec_cont->SvDesc.temporal_reference = 0;
  dec_cont->SvDesc.tics = 0;
  dec_cont->SvDesc.num_mbs_in_gob = 0;
  dec_cont->SvDesc.num_gobs_in_vop = 0;
#ifdef CLEAR_HDRINFO_IN_SEEK
  dec_cont->SvDesc.source_format = 0;
#endif
  dec_cont->SvDesc.cpcf = 0;

  (void) DWLmemset(&(dec_cont->StrmDesc), 0, sizeof(DecStrmDesc));
  (void) DWLmemset(&(dec_cont->MbSetDesc.out_data), 0, sizeof(MP4DecOutput));
  (void) DWLmemset(dec_cont->MBDesc, 0, MP4API_DEC_MBS * sizeof(DecMBDesc));
  (void) DWLmemset(dec_cont->StrmStorage.out_buf, 0, MP4_MAX_BUFFERS * sizeof(u32));
#ifdef USE_OMXIL_BUFFER
  (void) DWLmemset(dec_cont->StrmStorage.p_pic_buf, 0, MP4_MAX_BUFFERS * sizeof(picture_t));
  (void) DWLmemset(dec_cont->StrmStorage.picture_info, 0, MP4_MAX_BUFFERS * 2 * sizeof(MP4DecPicture));
#endif
#ifdef CLEAR_HDRINFO_IN_SEEK
  (void) DWLmemset(&(dec_cont->Hdrs), 0, sizeof(DecHdrs));
  (void) DWLmemset(&(dec_cont->tmp_hdrs), 0, sizeof(DecHdrs));
  MP4API_InitDataStructures(dec_cont);
  dec_cont->Hdrs.low_delay = HANTRO_TRUE;
  dec_cont->Hdrs.video_object_layer_width = dec_cont->StrmStorage.video_object_layer_width;
  dec_cont->Hdrs.video_object_layer_height = dec_cont->StrmStorage.video_object_layer_height;
  dec_cont->Hdrs.vop_time_increment_resolution = 30000;
  dec_cont->Hdrs.data_partitioned = HANTRO_FALSE;
  dec_cont->Hdrs.resync_marker_disable = HANTRO_TRUE;
  dec_cont->Hdrs.colour_primaries = 1;
  dec_cont->Hdrs.transfer_characteristics = 1;
  dec_cont->Hdrs.matrix_coefficients = 6;
#endif

#ifdef USE_OMXIL_BUFFER
  if (dec_cont->fifo_display)
    FifoRelease(dec_cont->fifo_display);
  FifoInit(MP4_MAX_BUFFERS*2, &dec_cont->fifo_display);
#endif
}

MP4DecRet MP4DecAbort(MP4DecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;

  MP4_API_TRC("MP4DecAbort#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    MP4_API_TRC("MP4DecAbort# ERROR: Decoder not initialized");
    return (MP4DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);
  /* Abort frame buffer waiting */
  MP4EnterAbortState(dec_cont);
  pthread_mutex_unlock(&dec_cont->protect_mutex);

  MP4_API_TRC("MP4DecAbort# MP4DEC_OK\n");
  return (MP4DEC_OK);
}

MP4DecRet MP4DecAbortAfter(MP4DecInst dec_inst) {
  DecContainer *dec_cont = (DecContainer *) dec_inst;

  MP4_API_TRC("MP4DecAbortAfter#\n");

  /* Check if decoder is in an incorrect mode */
  if(dec_inst == NULL || dec_cont->ApiStorage.DecStat == UNINIT) {
    MP4_API_TRC("MP4DecAbortAfter# ERROR: Decoder not initialized");
    return (MP4DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);

#if 0
  /* If normal EOS is waited, return directly */
  if(dec_cont->dec_stat == MP4DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (MP4DEC_OK);
  }
#endif

  /* Stop and release HW */
  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->mp4_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;
  }

  /* Clear any remaining pictures from DPB */
  MP4EmptyBufferQueue(dec_cont);
  MP4StateReset(dec_cont);
  MP4ExistAbortState(dec_cont);
  pthread_mutex_unlock(&dec_cont->protect_mutex);

  MP4_API_TRC("MP4DecAbortAfter# MP4DEC_OK\n");
  return (MP4DEC_OK);
}
#endif
