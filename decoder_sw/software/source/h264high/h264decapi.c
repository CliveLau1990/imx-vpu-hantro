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
#include "version.h"
#include "h264hwd_container.h"
#include "h264decapi.h"
#include "h264hwd_decoder.h"
#include "h264hwd_util.h"
#include "h264hwd_exports.h"
#include "h264hwd_dpb.h"
#include "h264hwd_neighbour.h"
#include "h264hwd_asic.h"
#include "h264hwd_regdrv.h"
#include "h264hwd_byte_stream.h"
#include "deccfg.h"
#include "h264_pp_multibuffer.h"
#include "tiledref.h"
#include "workaround.h"
#include "errorhandling.h"
#include "commonconfig.h"

#include "dwl.h"
#include "h264decmc_internals.h"
/*------------------------------------------------------------------------------
       Version Information - DO NOT CHANGE!
------------------------------------------------------------------------------*/

#define H264DEC_MAJOR_VERSION 1
#define H264DEC_MINOR_VERSION 1

/*
 * H264DEC_TRACE         Trace H264 Decoder API function calls. H264DecTrace
 *                       must be implemented externally.
 * H264DEC_EVALUATION    Compile evaluation version, restricts number of frames
 *                       that can be decoded
 */

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

#ifdef H264DEC_TRACE
#define DEC_API_TRC(str)    H264DecTrace(str)
#else
#define DEC_API_TRC(str)    do{}while(0)
#endif

#ifdef USE_RANDOM_TEST
#include "string.h"
#include "stream_corrupt.h"
#endif

static void h264UpdateAfterPictureDecode(decContainer_t * dec_cont);
static u32 h264SpsSupported(const decContainer_t * dec_cont);
static u32 h264PpsSupported(const decContainer_t * dec_cont);
static u32 h264StreamIsBaseline(const decContainer_t * dec_cont);

static u32 h264AllocateResources(decContainer_t * dec_cont);
static void bsdDecodeReturn(u32 retval);
extern void h264InitPicFreezeOutput(decContainer_t * dec_cont, u32 from_old_dpb);

static void h264GetSarInfo(const storage_t * storage,
                           u32 * sar_width, u32 * sar_height);
extern void h264PreparePpRun(decContainer_t * dec_cont);

static void h264CheckReleasePpAndHw(decContainer_t *dec_cont);

#ifdef USE_OUTPUT_RELEASE
static H264DecRet H264DecNextPicture_INTERNAL(H264DecInst dec_inst,
    H264DecPicture * output,
    u32 end_of_stream);
#endif

#ifdef USE_EXTERNAL_BUFFER
static void h264SetExternalBufferInfo(H264DecInst dec_inst, storage_t *storage) ;
static void h264SetMVCExternalBufferInfo(H264DecInst dec_inst, storage_t *storage);
#endif
#ifdef USE_OUTPUT_RELEASE
static void h264EnterAbortState(decContainer_t *dec_cont);
static void h264ExistAbortState(decContainer_t *dec_cont);
static void h264StateReset(decContainer_t *dec_cont);
#endif

#define DEC_DPB_NOT_INITIALIZED      -1

/*------------------------------------------------------------------------------

    Function: H264DecInit()

        Functional description:
            Initialize decoder software. Function reserves memory for the
            decoder instance and calls h264bsdInit to initialize the
            instance data.

        Inputs:
            no_output_reordering  flag to indicate decoder that it doesn't have
                                to try to provide output pictures in display
                                order, saves memory
            error_handling
                                Flag to determine which error concealment
                                method to use.
            useDisplaySmooothing
                                flag to enable extra buffering in DPB output
                                so that application may read output pictures
                                one by one
        Outputs:
            dec_inst             pointer to initialized instance is stored here

        Returns:
            H264DEC_OK        successfully initialized the instance
            H264DEC_INITFAIL  initialization failed
            H264DEC_PARAM_ERROR invalid parameters
            H264DEC_MEMFAIL   memory allocation failed
            H264DEC_DWL_ERROR error initializing the system interface
------------------------------------------------------------------------------*/

H264DecRet H264DecInit(H264DecInst * dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                       const void *dwl,
#endif
                       u32 no_output_reordering,
                       enum DecErrorHandling error_handling,
                       u32 use_display_smoothing,
                       enum DecDpbFlags dpb_flags,
                       u32 use_adaptive_buffers,
                       u32 n_guard_size,
                       u32 use_secure_mode,
                       struct DecDownscaleCfg *dscale_cfg) {

  /*@null@ */ decContainer_t *dec_cont;
#ifndef USE_EXTERNAL_BUFFER
  /*@null@ */ const void *dwl;

  struct DWLInitParam dwl_init;
#endif

  DWLHwConfig hw_cfg;
  u32 asic_id;
  u32 reference_frame_format;

  DEC_API_TRC("H264DecInit#\n");

  /* check that right shift on negative numbers is performed signed */
  /*lint -save -e* following check causes multiple lint messages */
#if (((-1) >> 1) != (-1))
#error Right bit-shifting (>>) does not preserve the sign
#endif
  /*lint -restore */

  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecInit# ERROR: dec_inst == NULL");
    return (H264DEC_PARAM_ERROR);
  }

  *dec_inst = NULL;   /* return NULL instance for any error */

  /* check for proper hardware */
  asic_id = DWLReadAsicID(DWL_CLIENT_TYPE_H264_DEC);

  if((asic_id >> 16) < 0x8170U &&
      (asic_id >> 16) != 0x6731U ) {
    DEC_API_TRC("H264DecInit# ERROR: HW not recognized/unsupported!\n");
    return H264DEC_FORMAT_NOT_SUPPORTED;
  }

  /* check that H.264 decoding supported in HW */
  (void) DWLmemset(&hw_cfg, 0, sizeof(DWLHwConfig));
  DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_H264_DEC);
  if(!hw_cfg.h264_support) {
    DEC_API_TRC("H264DecInit# ERROR: H264 not supported in HW\n");
    return H264DEC_FORMAT_NOT_SUPPORTED;
  }

  if(!hw_cfg.addr64_support && sizeof(void *) == 8) {
    DEC_API_TRC("H264DecInit# ERROR: HW not support 64bit address!\n");
    return (H264DEC_PARAM_ERROR);
  }

#ifndef USE_EXTERNAL_BUFFER
  dwl_init.client_type = DWL_CLIENT_TYPE_H264_DEC;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    DEC_API_TRC("H264DecInit# ERROR: DWL Init failed\n");
    return (H264DEC_DWL_ERROR);
  }
#endif

  dec_cont = (decContainer_t *) DWLmalloc(sizeof(decContainer_t));

  if(dec_cont == NULL) {
#ifndef USE_EXTERNAL_BUFFER
    (void) DWLRelease(dwl);

    DEC_API_TRC("H264DecInit# ERROR: Memory allocation failed\n");
#endif
    return (H264DEC_MEMFAIL);
  }

  (void) DWLmemset(dec_cont, 0, sizeof(decContainer_t));
  dec_cont->dwl = dwl;

  h264bsdInit(&dec_cont->storage, no_output_reordering,
              use_display_smoothing);

  dec_cont->dec_stat = H264DEC_INITIALIZED;

  SetDecRegister(dec_cont->h264_regs, HWIF_DEC_MODE, DEC_X170_MODE_H264);

  SetCommonConfigRegs(dec_cont->h264_regs,DWL_CLIENT_TYPE_H264_DEC);

  /* Set prediction filter taps */
  SetDecRegister(dec_cont->h264_regs, HWIF_PRED_BC_TAP_0_0, 1);
  SetDecRegister(dec_cont->h264_regs, HWIF_PRED_BC_TAP_0_1, (u32)(-5));
  SetDecRegister(dec_cont->h264_regs, HWIF_PRED_BC_TAP_0_2, 20);
  pthread_mutex_init(&dec_cont->protect_mutex, NULL);

  /* save HW version so we dont need to check it all the time when deciding the control stuff */
  dec_cont->is8190 = (asic_id >> 16) != 0x8170U ? 1 : 0;
  dec_cont->h264_profile_support = hw_cfg.h264_support;

  if((asic_id >> 16)  == 0x8170U)
    error_handling = 0;

  /* save ref buffer support status */
  dec_cont->ref_buf_support = hw_cfg.ref_buf_support;
  reference_frame_format = dpb_flags & DEC_REF_FRM_FMT_MASK;
  if(reference_frame_format == DEC_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!hw_cfg.tiled_mode_support) {
      return H264DEC_FORMAT_NOT_SUPPORTED;
    }
    dec_cont->tiled_mode_support = hw_cfg.tiled_mode_support;
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
    return (H264DEC_PARAM_ERROR);
  } else {
    u32 scale_table[9] = {0, 0, 1, 0, 2, 0, 0, 0, 3};

    dec_cont->pp_enabled = 1;
    dec_cont->down_scale_x = dscale_cfg->down_scale_x;
    dec_cont->down_scale_y = dscale_cfg->down_scale_y;

    dec_cont->storage.pp_enabled = 1;
    dec_cont->storage.down_scale_x_shift = scale_table[dscale_cfg->down_scale_x];
    dec_cont->storage.down_scale_y_shift = scale_table[dscale_cfg->down_scale_y];
    dec_cont->dscale_shift_x = dec_cont->storage.down_scale_x_shift;
    dec_cont->dscale_shift_y = dec_cont->storage.down_scale_y_shift;
  }
  dec_cont->storage.release_buffer = 0;

  dec_cont->pp_buffer_queue = InputQueueInit(0);
  if (dec_cont->pp_buffer_queue == NULL) {
    return (H264DEC_MEMFAIL);
  }
  dec_cont->storage.pp_buffer_queue = dec_cont->pp_buffer_queue;

  /* Custom DPB modes require tiled support >= 2 */
  dec_cont->allow_dpb_field_ordering = 0;
  dec_cont->dpb_mode = DEC_DPB_NOT_INITIALIZED;
  if( dpb_flags & DEC_DPB_ALLOW_FIELD_ORDERING ) {
    dec_cont->allow_dpb_field_ordering = hw_cfg.field_dpb_support;
  }
  dec_cont->storage.intra_freeze = error_handling == DEC_EC_VIDEO_FREEZE;
#ifndef _DISABLE_PIC_FREEZE
  if (error_handling == DEC_EC_PARTIAL_FREEZE)
    dec_cont->storage.partial_freeze = 1;
  else if (error_handling == DEC_EC_PARTIAL_IGNORE)
    dec_cont->storage.partial_freeze = 2;
#endif
  dec_cont->storage.picture_broken = HANTRO_FALSE;

  dec_cont->max_dec_pic_width = hw_cfg.max_dec_pic_width;    /* max decodable picture width */

  dec_cont->checksum = dec_cont;  /* save instance as a checksum */

#ifdef _ENABLE_2ND_CHROMA
  dec_cont->storage.enable2nd_chroma = 1;
#endif

  InitWorkarounds(DEC_X170_MODE_H264, &dec_cont->workarounds);
  if (dec_cont->workarounds.h264.frame_num)
    dec_cont->frame_num_mask = 0x1000;

  /*  default single Core */
  dec_cont->n_cores = 1;

  /* Init frame buffer list */
  InitList(&dec_cont->fb_list);

  dec_cont->storage.dpbs[0]->fb_list = &dec_cont->fb_list;
  dec_cont->storage.dpbs[1]->fb_list = &dec_cont->fb_list;

  dec_cont->use_adaptive_buffers = use_adaptive_buffers;
  dec_cont->n_guard_size = n_guard_size;

  dec_cont->secure_mode = use_secure_mode;
  if (dec_cont->secure_mode)
    dec_cont->ref_buf_support = 0;

#ifdef USE_RANDOM_TEST
  /*********************************************************/
  /** Developers can change below parameters to generate  **/
  /** different kinds of error stream.                    **/
  /*********************************************************/
  dec_cont->error_params.seed = 66;
  strcpy(dec_cont->error_params.truncate_stream_odds , "1 : 6");
  strcpy(dec_cont->error_params.swap_bit_odds, "1 : 100000");
  strcpy(dec_cont->error_params.packet_loss_odds, "1 : 6");
  /*********************************************************/

  if (strcmp(dec_cont->error_params.swap_bit_odds, "0") != 0)
    dec_cont->error_params.swap_bits_in_stream = 0;

  if (strcmp(dec_cont->error_params.packet_loss_odds, "0") != 0)
    dec_cont->error_params.lose_packets = 1;

  if (strcmp(dec_cont->error_params.truncate_stream_odds, "0") != 0)
    dec_cont->error_params.truncate_stream = 1;

  dec_cont->ferror_stream = fopen("random_error.h264", "wb");
  if(dec_cont->ferror_stream == NULL) {
    DEBUG_PRINT(("Unable to open file error.h264\n"));
    return H264DEC_MEMFAIL;
  }

  if (dec_cont->error_params.swap_bits_in_stream ||
      dec_cont->error_params.lose_packets ||
      dec_cont->error_params.truncate_stream) {
    dec_cont->error_params.random_error_enabled = 1;
    InitializeRandom(dec_cont->error_params.seed);
  }
#endif

  *dec_inst = (H264DecInst) dec_cont;

  DEC_API_TRC("H264DecInit# OK\n");

  return (H264DEC_OK);
}

/*------------------------------------------------------------------------------

    Function: H264DecGetInfo()

        Functional description:
            This function provides read access to decoder information. This
            function should not be called before H264DecDecode function has
            indicated that headers are ready.

        Inputs:
            dec_inst     decoder instance

        Outputs:
            dec_info    pointer to info struct where data is written

        Returns:
            H264DEC_OK            success
            H264DEC_PARAM_ERROR     invalid parameters
            H264DEC_HDRS_NOT_RDY  information not available yet

------------------------------------------------------------------------------*/
H264DecRet H264DecGetInfo(H264DecInst dec_inst, H264DecInfo * dec_info) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
#ifdef USE_EXTERNAL_BUFFER
  u32 max_dpb_size,no_reorder;
#endif
  storage_t *storage;

  DEC_API_TRC("H264DecGetInfo#");

  if(dec_inst == NULL || dec_info == NULL) {
    DEC_API_TRC("H264DecGetInfo# ERROR: dec_inst or dec_info is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecGetInfo# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  storage = &dec_cont->storage;

  if(storage->active_sps == NULL || storage->active_pps == NULL) {
    DEC_API_TRC("H264DecGetInfo# ERROR: Headers not decoded yet\n");
    return (H264DEC_HDRS_NOT_RDY);
  }

  /* h264bsdPicWidth and -Height return dimensions in macroblock units,
   * pic_width and -Height in pixels */
  if (!dec_cont->pp_enabled) {
    dec_info->pic_width = h264bsdPicWidth(storage) << 4;
    dec_info->pic_height = h264bsdPicHeight(storage) << 4;
  } else {
    dec_info->pic_width = (h264bsdPicWidth(storage) << 4) >> dec_cont->dscale_shift_x;
    dec_info->pic_height = (h264bsdPicHeight(storage) << 4) >> dec_cont->dscale_shift_y;
  }

  dec_info->video_range = h264bsdVideoRange(storage);
  dec_info->matrix_coefficients = h264bsdMatrixCoefficients(storage);
  dec_info->mono_chrome = h264bsdIsMonoChrome(storage);
  dec_info->interlaced_sequence = storage->active_sps->frame_mbs_only_flag == 0 ? 1 : 0;
#ifndef USE_EXTERNAL_BUFFER
  dec_info->pic_buff_size = storage->dpb->dpb_size + 1;
#else
  if(storage->no_reordering ||
      storage->active_sps->pic_order_cnt_type == 2 ||
      (storage->active_sps->vui_parameters_present_flag &&
       storage->active_sps->vui_parameters->bitstream_restriction_flag &&
       !storage->active_sps->vui_parameters->num_reorder_frames))
    no_reorder = HANTRO_TRUE;
  else
    no_reorder = HANTRO_FALSE;
  if(storage->view == 0)
    max_dpb_size = storage->active_sps->max_dpb_size;
  else {
    /* stereo view dpb size at least equal to base view size (to make sure
     * that base view pictures get output in correct display order) */
    max_dpb_size = MAX(storage->active_sps->max_dpb_size,
                       storage->active_view_sps[0]->max_dpb_size);
  }
  /* restrict max dpb size of mvc (stereo high) streams, make sure that
   * base address 15 is available/restricted for inter view reference use */
  if(storage->mvc_stream)
    max_dpb_size = MIN(max_dpb_size, 8);
  if(no_reorder)
    dec_info->pic_buff_size = MAX(storage->active_sps->num_ref_frames,1) + 1;
  else
    dec_info->pic_buff_size = max_dpb_size + 1;
#endif
  dec_info->multi_buff_pp_size =  storage->dpb->no_reordering? 2 : dec_info->pic_buff_size;
  dec_info->dpb_mode = dec_cont->dpb_mode;

  if (storage->mvc)
    dec_info->multi_buff_pp_size *= 2;

  h264GetSarInfo(storage, &dec_info->sar_width, &dec_info->sar_height);

  h264bsdCroppingParams(storage, &dec_info->crop_params);

  if(dec_cont->tiled_mode_support) {
    if(dec_info->interlaced_sequence &&
        (dec_info->dpb_mode != DEC_DPB_INTERLACED_FIELD)) {
      if(dec_info->mono_chrome)
        dec_info->output_format = H264DEC_YUV400;
      else
        dec_info->output_format = H264DEC_SEMIPLANAR_YUV420;
    } else
      dec_info->output_format = H264DEC_TILED_YUV420;
  } else if(dec_info->mono_chrome)
    dec_info->output_format = H264DEC_YUV400;
  else
    dec_info->output_format = H264DEC_SEMIPLANAR_YUV420;

  DEC_API_TRC("H264DecGetInfo# OK\n");

  return (H264DEC_OK);
}

#ifdef USE_EXTERNAL_BUFFER
u32 IsDpbRealloc(decContainer_t *dec_cont) {
  storage_t *storage = &dec_cont->storage;
  dpbStorage_t *dpb = storage->dpb;
  seqParamSet_t *p_sps = storage->active_sps;
  u32 is_high_supported = (dec_cont->h264_profile_support == H264_HIGH_PROFILE) ? 1 : 0;
  u32 n_cores = dec_cont->n_cores;
  u32 max_dpb_size, new_pic_size_in_mbs, new_pic_size, new_tot_buffers, dpb_size, max_ref_frames;
  u32 no_reorder;
  struct dpbInitParams dpb_params;

  new_pic_size_in_mbs = 0;

  if (!dec_cont->use_adaptive_buffers)
    return 1;

  if(dec_cont->b_mvc == 0)
    new_pic_size_in_mbs = p_sps->pic_width_in_mbs * p_sps->pic_height_in_mbs;
  else if(dec_cont->b_mvc == 1) {
    if(storage->sps[1] != 0)
      new_pic_size_in_mbs = MAX(storage->sps[0]->pic_width_in_mbs * storage->sps[0]->pic_height_in_mbs,
                                storage->sps[1]->pic_width_in_mbs * storage->sps[1]->pic_height_in_mbs);
    else
      new_pic_size_in_mbs = storage->sps[0]->pic_width_in_mbs * storage->sps[0]->pic_height_in_mbs;

  }

  /* dpb output reordering disabled if
   * 1) application set no_reordering flag
   * 2) POC type equal to 2
   * 3) num_reorder_frames in vui equal to 0 */
  if(storage->no_reordering ||
      p_sps->pic_order_cnt_type == 2 ||
      (p_sps->vui_parameters_present_flag &&
       p_sps->vui_parameters->bitstream_restriction_flag &&
       !p_sps->vui_parameters->num_reorder_frames))
    no_reorder = HANTRO_TRUE;
  else
    no_reorder = HANTRO_FALSE;

  if (storage->view == 0)
    max_dpb_size = p_sps->max_dpb_size;
  else {
    /* stereo view dpb size at least equal to base view size (to make sure
     * that base view pictures get output in correct display order) */
    max_dpb_size = MAX(p_sps->max_dpb_size, storage->active_view_sps[0]->max_dpb_size);
  }
  /* restrict max dpb size of mvc (stereo high) streams, make sure that
   * base address 15 is available/restricted for inter view reference use */
  if (storage->mvc_stream)
    max_dpb_size = MIN(max_dpb_size, 8);

  dpb_params.pic_size_in_mbs = new_pic_size_in_mbs;
  dpb_params.dpb_size = max_dpb_size;
  dpb_params.max_ref_frames = p_sps->num_ref_frames;
  dpb_params.max_frame_num = p_sps->max_frame_num;
  dpb_params.no_reordering = no_reorder;
  dpb_params.display_smoothing = storage->use_smoothing;
  dpb_params.mono_chrome = p_sps->mono_chrome;
  dpb_params.is_high_supported = is_high_supported;
  dpb_params.enable2nd_chroma = storage->enable2nd_chroma && !p_sps->mono_chrome;
  dpb_params.multi_buff_pp = storage->multi_buff_pp;
  dpb_params.n_cores = n_cores;
  dpb_params.mvc_view = storage->view;

  if(dpb_params.is_high_supported) {
    /* yuv picture + direct mode motion vectors */
    new_pic_size = dpb_params.pic_size_in_mbs * ((dpb_params.mono_chrome ? 256 : 384) + 64);

    /* allocate 32 bytes for multicore status fields */
    /* locate it after picture and direct MV */
    new_pic_size += 32;
  } else {
    new_pic_size = dpb_params.pic_size_in_mbs * 384;
  }
  if (dpb_params.enable2nd_chroma && !dpb_params.mono_chrome) {
    new_pic_size += new_pic_size_in_mbs * 128;
  }

  dpb->n_new_pic_size = new_pic_size;
  max_ref_frames = MAX(dpb_params.max_ref_frames, 1);

  if(dpb_params.no_reordering)
    dpb_size = max_ref_frames;
  else
    dpb_size = dpb_params.dpb_size;

  /* max DPB size is (16 + 1) buffers */
  new_tot_buffers = dpb_size + 1;

  /* figure out extra buffers for smoothing, pp, multicore, etc... */
  if (n_cores == 1) {
    /* single Core configuration */
    if (storage->use_smoothing)
      new_tot_buffers += no_reorder ? 1 : dpb_size + 1;
    else if (storage->multi_buff_pp)
      new_tot_buffers++;
  } else {
    /* multi Core configuration */

    if (storage->use_smoothing && !no_reorder) {
      /* at least double buffers for smooth output */
      if (new_tot_buffers > n_cores) {
        new_tot_buffers *= 2;
      } else {
        new_tot_buffers += n_cores;
      }
    } else {
      /* one extra buffer for each Core */
      /* do not allocate twice for multiview */
      if(!storage->view)
        new_tot_buffers += n_cores;
    }
  }

  if ((dpb->n_new_pic_size <= dec_cont->n_ext_buf_size) &&
      new_tot_buffers + dpb->n_guard_size <= dpb->tot_buffers)
    return 0;
  return 1;
}
#endif

/*------------------------------------------------------------------------------

    Function: H264DecRelease()

        Functional description:
            Release the decoder instance. Function calls h264bsdShutDown to
            release instance data and frees the memory allocated for the
            instance.

        Inputs:
            dec_inst     Decoder instance

        Outputs:
            none

        Returns:
            none

------------------------------------------------------------------------------*/

void H264DecRelease(H264DecInst dec_inst) {

  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  const void *dwl;

  DEC_API_TRC("H264DecRelease#\n");

  if(dec_cont == NULL) {
    DEC_API_TRC("H264DecRelease# ERROR: dec_inst == NULL\n");
    return;
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecRelease# ERROR: Decoder not initialized\n");
    return;
  }

#ifdef USE_RANDOM_TEST
  if (dec_cont->ferror_stream)
    fclose(dec_cont->ferror_stream);
#endif


  /* PP instance must be already disconnected at this point */
  ASSERT(dec_cont->pp.pp_instance == NULL);

  dwl = dec_cont->dwl;

  /* make sure all in sync in multicore mode, hw idle, output empty */
  if(dec_cont->b_mc) {
    h264MCWaitPicReadyAll(dec_cont);
  } else {
    u32 i;
    const dpbStorage_t *dpb = dec_cont->storage.dpb;

    /* Empty the output list. This is just so that fb_list does not
     * complaint about still referenced pictures
     */
    for(i = 0; i < dpb->tot_buffers; i++) {
      if(dpb->pic_buff_id[i] != FB_NOT_VALID_ID &&
        IsBufferOutput(&dec_cont->fb_list, dpb->pic_buff_id[i])) {
        ClearOutput(&dec_cont->fb_list, dpb->pic_buff_id[i]);
      }
    }
  }

  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->h264_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;

    /* Decrement usage for DPB buffers */
    DecrementDPBRefCount(&dec_cont->storage.dpb[1]);
  } else if (dec_cont->keep_hw_reserved)
    DWLReleaseHw(dwl, dec_cont->core_id);  /* release HW lock */
  pthread_mutex_destroy(&dec_cont->protect_mutex);

#ifndef USE_EXT_BUF_SAFE_RELEASE
    MarkListNotInUse(&dec_cont->fb_list);
#endif

  h264bsdShutdown(&dec_cont->storage);

  h264bsdFreeDpb(
//#ifndef USE_EXTERNAL_BUFFER
    dwl,
//#endif
    dec_cont->storage.dpbs[0]);
  if (dec_cont->storage.dpbs[1]->dpb_size)
    h264bsdFreeDpb(
//#ifndef USE_EXTERNAL_BUFFER
      dwl,
//#endif
      dec_cont->storage.dpbs[1]);

  ReleaseAsicBuffers(dwl, dec_cont->asic_buff);

  ReleaseList(&dec_cont->fb_list);
  dec_cont->checksum = NULL;
  DWLfree(dec_cont);
#ifndef USE_EXTERNAL_BUFFER
  (void) DWLRelease(dwl);

  DEC_API_TRC("H264DecRelease# OK\n");
#endif

  return;
}

/*------------------------------------------------------------------------------

    Function: H264DecDecode

        Functional description:
            Decode stream data. Calls h264bsdDecode to do the actual decoding.

        Input:
            dec_inst     decoder instance
            input      pointer to input struct

        Outputs:
            output     pointer to output struct

        Returns:
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet
            H264DEC_PARAM_ERROR         invalid parameters

            H264DEC_STRM_PROCESSED    stream buffer decoded
            H264DEC_HDRS_RDY    headers decoded, stream buffer not finished
            H264DEC_PIC_DECODED decoding of a picture finished
            H264DEC_STRM_ERROR  serious error in decoding, no valid parameter
                                sets available to decode picture data
            H264DEC_PENDING_FLUSH   next invocation of H264DecDecode() function
                                    flushed decoded picture buffer, application
                                    needs to read all output pictures (using
                                    H264DecNextPicture function) before calling
                                    H264DecDecode() again. Used only when
                                    use_display_smoothing was enabled in init.

            H264DEC_HW_BUS_ERROR    decoder HW detected a bus error
            H264DEC_SYSTEM_ERROR    wait for hardware has failed
            H264DEC_MEMFAIL         decoder failed to allocate memory
            H264DEC_DWL_ERROR   System wrapper failed to initialize
            H264DEC_HW_TIMEOUT  HW timeout
            H264DEC_HW_RESERVED HW could not be reserved
------------------------------------------------------------------------------*/
H264DecRet H264DecDecode(H264DecInst dec_inst, const H264DecInput * input,
                         H264DecOutput * output) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  u32 strm_len;
  u32 input_data_len;  // used to generate error stream
  const u8 *tmp_stream;
  u32 index = 0;
  const u8 *ref_data = NULL;
  H264DecRet return_value = H264DEC_STRM_PROCESSED;

  DEC_API_TRC("H264DecDecode#\n");
  /* Check that function input parameters are valid */
  if(input == NULL || output == NULL || dec_inst == NULL) {
    DEC_API_TRC("H264DecDecode# ERROR: NULL arg(s)\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecDecode# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  input_data_len = input->data_len;

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort) {
    return (H264DEC_ABORTED);
  }
#endif

#ifdef USE_RANDOM_TEST
  if (dec_cont->error_params.random_error_enabled) {
    // error type: lose packets;
    if (dec_cont->error_params.lose_packets && !dec_cont->stream_not_consumed) {
      u8 lose_packet = 0;
      if (RandomizePacketLoss(dec_cont->error_params.packet_loss_odds,
                              &lose_packet)) {
        DEBUG_PRINT(("Packet loss simulator error (wrong config?)\n"));
      }
      if (lose_packet) {
        input_data_len = 0;
        output->data_left = 0;
        dec_cont->stream_not_consumed = 0;
        return H264DEC_STRM_PROCESSED;
      }
    }

    // error type: truncate stream(random len for random packet);
    if (dec_cont->error_params.truncate_stream && !dec_cont->stream_not_consumed) {
      u8 truncate_stream = 0;
      if (RandomizePacketLoss(dec_cont->error_params.truncate_stream_odds,
                              &truncate_stream)) {
        DEBUG_PRINT(("Truncate stream simulator error (wrong config?)\n"));
      }
      if (truncate_stream) {
        u32 random_size = input_data_len;
        if (RandomizeU32(&random_size)) {
          DEBUG_PRINT(("Truncate randomizer error (wrong config?)\n"));
        }
        input_data_len = random_size;
      }

      dec_cont->prev_input_len = input_data_len;

      if (input_data_len == 0) {
        output->data_left = 0;
        dec_cont->stream_not_consumed = 0;
        return H264DEC_STRM_PROCESSED;
      }
    }

    /*  stream is truncated but not consumed at first time, the same truncated length
        at the second time */
    if (dec_cont->error_params.truncate_stream && dec_cont->stream_not_consumed)
      input_data_len = dec_cont->prev_input_len;

    // error type: swap bits;
    if (dec_cont->error_params.swap_bits_in_stream && !dec_cont->stream_not_consumed) {
      u8 *p_tmp = (u8 *)input->stream;
      if (RandomizeBitSwapInStream(p_tmp, input_data_len,
                                   dec_cont->error_params.swap_bit_odds)) {
        DEBUG_PRINT(("Bitswap randomizer error (wrong config?)\n"));
      }
    }
  }
#endif

  if(input->data_len == 0 ||
      input->data_len > DEC_X170_MAX_STREAM ||
      X170_CHECK_VIRTUAL_ADDRESS(input->stream) ||
      X170_CHECK_BUS_ADDRESS(input->stream_bus_address)) {
    DEC_API_TRC("H264DecDecode# ERROR: Invalid arg value\n");
    return H264DEC_PARAM_ERROR;
  }

#ifdef H264DEC_EVALUATION
  if(dec_cont->pic_number > H264DEC_EVALUATION) {
    DEC_API_TRC("H264DecDecode# H264DEC_EVALUATION_LIMIT_EXCEEDED\n");
    return H264DEC_EVALUATION_LIMIT_EXCEEDED;
  }
#endif

  dec_cont->stream_pos_updated = 0;
  output->strm_curr_pos = NULL;
  dec_cont->hw_stream_start_bus = input->stream_bus_address;
  dec_cont->p_hw_stream_start = input->stream;
  strm_len = dec_cont->hw_length = input_data_len;
  tmp_stream = input->stream;
  dec_cont->stream_consumed_callback.p_strm_buff = input->stream;
  dec_cont->stream_consumed_callback.p_user_data = input->p_user_data;

  dec_cont->skip_non_reference = input->skip_non_reference;

  dec_cont->force_nal_mode = 0;

  /* Switch to RLC mode, i.e. sw performs entropy decoding */
  if(dec_cont->reallocate) {
    DEBUG_PRINT(("H264DecDecode: Reallocate\n"));
    dec_cont->rlc_mode = 1;
    dec_cont->reallocate = 0;

    /* Reallocate only once */
    if(dec_cont->asic_buff->mb_ctrl.virtual_address == NULL) {
      if(h264AllocateResources(dec_cont) != 0) {
        /* signal that decoder failed to init parameter sets */
        dec_cont->storage.active_pps_id = MAX_NUM_PIC_PARAM_SETS;
        DEC_API_TRC("H264DecDecode# ERROR: Reallocation failed\n");
        return H264DEC_MEMFAIL;
      }

      h264bsdResetStorage(&dec_cont->storage);
    }

  }

  /* get PP pipeline status at the beginning of each new picture */
  if(dec_cont->pp.pp_instance != NULL &&
      dec_cont->storage.pic_started == HANTRO_FALSE) {
    /* store old multibuffer status to compare with new */
    const u32 old_multi = dec_cont->pp.pp_info.multi_buffer;
    u32 max_id = dec_cont->storage.dpb->no_reordering ? 1 :
                 dec_cont->storage.dpb->dpb_size;

    ASSERT(dec_cont->pp.PPConfigQuery != NULL);
    dec_cont->pp.pp_info.tiled_mode =
      dec_cont->tiled_reference_enable;
    dec_cont->pp.PPConfigQuery(dec_cont->pp.pp_instance,
                               &dec_cont->pp.pp_info);
    /* Make sure pipeline turned off for interlaced streams (there is a
     * possibility for having prog pictures in the stream --> these are
     * not run in pipeline anymore) */
    if(dec_cont->storage.active_sps &&
        !dec_cont->storage.active_sps->frame_mbs_only_flag)
      dec_cont->pp.pp_info.pipeline_accepted = 0;

    TRACE_PP_CTRL("H264DecDecode: PP pipeline_accepted = %d\n",
                  dec_cont->pp.pp_info.pipeline_accepted);
    TRACE_PP_CTRL("H264DecDecode: PP multi_buffer = %d\n",
                  dec_cont->pp.pp_info.multi_buffer);

    /* increase number of buffers if multiview decoding */
    if (dec_cont->storage.num_views) {
      max_id += dec_cont->storage.dpb->no_reordering ? 1 :
                dec_cont->storage.dpb->dpb_size + 1;
      max_id = MIN(max_id, 16);
    }

    if (old_multi != dec_cont->pp.pp_info.multi_buffer) {
      h264PpMultiInit(dec_cont, max_id);
    }
    /* just increase amount of buffers */
    else if (max_id > dec_cont->pp.multi_max_id)
      h264PpMultiMvc(dec_cont, max_id);
  }

  do {
    u32 dec_result;
    u32 num_read_bytes = 0;
    storage_t *storage = &dec_cont->storage;

    DEBUG_PRINT(("H264DecDecode: mode is %s\n",
                 dec_cont->rlc_mode ? "RLC" : "VLC"));

    if(dec_cont->dec_stat == H264DEC_NEW_HEADERS) {
      dec_result = H264BSD_HDRS_RDY;
      dec_cont->dec_stat = H264DEC_INITIALIZED;
    } else if(dec_cont->dec_stat == H264DEC_BUFFER_EMPTY) {
      DEBUG_PRINT(("H264DecDecode: Skip h264bsdDecode\n"));
      DEBUG_PRINT(("H264DecDecode: Jump to H264BSD_PIC_RDY\n"));
      /* update stream pointers */
      strmData_t *strm = dec_cont->storage.strm;
      strm->p_strm_buff_start = tmp_stream;
      strm->strm_buff_size = strm_len;

      dec_result = H264BSD_PIC_RDY;
    }
#ifdef USE_EXTERNAL_BUFFER
    else if(dec_cont->dec_stat == H264DEC_WAITING_FOR_BUFFER) {
      dec_result = H264BSD_BUFFER_NOT_READY;
    }
#endif
    else {
      if (!dec_cont->no_decoding_buffer)
        dec_result = h264bsdDecode(dec_cont, tmp_stream, strm_len,
                                  input->pic_id, &num_read_bytes);

      /* "no_decoding_buffer" was set in previous loop, so do not
        enter h264bsdDecode() twice for the same slice */
      else {
        num_read_bytes = dec_cont->num_read_bytes;
        dec_result = dec_cont->dec_result;
      }

      /* "alloc_buffer" is set in h264bsdDecode() to indicate if
         new output buffer is needed */
      if (dec_cont->alloc_buffer == 1) {
        storage->curr_image->data =
          h264bsdAllocateDpbImage(storage->dpb);
        if(storage->curr_image->data == NULL) {
#ifdef USE_OUTPUT_RELEASE
          if (dec_cont->abort)
            return H264DEC_ABORTED;
#endif
#ifdef GET_FREE_BUFFER_NON_BLOCK
          dec_cont->no_decoding_buffer = 1;
          dec_cont->dec_result = dec_result;
          dec_cont->num_read_bytes = num_read_bytes;
          dec_cont->stream_pos_updated = 0;
          return_value = H264DEC_NO_DECODING_BUFFER;
          break;
#endif
        }
        storage->curr_image->pp_data = storage->dpb->current_out->ds_data;
        storage->dpb->current_out->data = storage->curr_image->data;
        dec_cont->alloc_buffer = 0;
        dec_cont->no_decoding_buffer = 0;
      }

      ASSERT(num_read_bytes <= strm_len);

      bsdDecodeReturn(dec_result);
    }

    tmp_stream += num_read_bytes;
    strm_len -= num_read_bytes;
    switch (dec_result) {
#ifndef USE_EXTERNAL_BUFFER
    case H264BSD_HDRS_RDY: {
      h264CheckReleasePpAndHw(dec_cont);

      dec_cont->storage.multi_buff_pp =
        dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.pp_info.multi_buffer;

      if(storage->dpb->flushed && storage->dpb->num_out) {
        /* output first all DPB stored pictures */
        storage->dpb->flushed = 0;
        dec_cont->dec_stat = H264DEC_NEW_HEADERS;
        /* if display smoothing used -> indicate that all pictures
        * have to be read out */
        if((storage->dpb->tot_buffers >
            storage->dpb->dpb_size + 1) &&
            !dec_cont->storage.multi_buff_pp) {
          return_value = H264DEC_PENDING_FLUSH;
        } else {
          return_value = H264DEC_PIC_DECODED;
        }

        /* base view -> flush another view dpb */
        if (dec_cont->storage.num_views &&
            dec_cont->storage.view == 0) {
          h264bsdFlushDpb(storage->dpbs[1]);
        }

        DEC_API_TRC
        ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");
        strm_len = 0;

        break;
      } else if ((storage->dpb->tot_buffers >
                  storage->dpb->dpb_size + 1) && storage->dpb->num_out) {
        /* flush buffered output for display smoothing */
        storage->dpb->delayed_out = 0;
        storage->second_field = 0;
        dec_cont->dec_stat = H264DEC_NEW_HEADERS;
        return_value = H264DEC_PENDING_FLUSH;
        strm_len = 0;

        break;
      } else if (storage->pending_flush) {
        storage->pending_flush = 0;
        dec_cont->dec_stat = H264DEC_NEW_HEADERS;
        return_value = H264DEC_PENDING_FLUSH;
        strm_len = 0;

        if(dec_cont->b_mc) {
          h264MCWaitPicReadyAll(dec_cont);
          h264bsdFlushDpb(storage->dpb);
        }
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp.pp_instance == NULL) {
#if 0
          WaitListNotInUse(&dec_cont->fb_list);
          if (dec_cont->pp_enabled) {
            InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);
          }
#endif
          h264bsdFlushDpb(storage->dpb);
        }
#endif
        DEC_API_TRC
        ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");

        break;
      }

      /* Make sure that all frame buffers are not in use before
      * reseting DPB (i.e. all HW cores are idle and all output
      * processed) */
      if(dec_cont->b_mc)
        h264MCWaitPicReadyAll(dec_cont);
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp.pp_instance == NULL && !dec_cont->b_mc) {

#ifndef USE_EXT_BUF_SAFE_RELEASE
        MarkListNotInUse(&dec_cont->fb_list);
#else
        WaitListNotInUse(&dec_cont->fb_list);
        if (dec_cont->pp_enabled) {
          InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);
        }
#endif
      }
#endif

      if(!h264SpsSupported(dec_cont)) {
        storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
        storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;
        storage->active_view_sps_id[0] =
          storage->active_view_sps_id[1] =
            MAX_NUM_SEQ_PARAM_SETS;
        storage->pic_started = HANTRO_FALSE;
        dec_cont->dec_stat = H264DEC_INITIALIZED;
        storage->prev_buf_not_finished = HANTRO_FALSE;
        output->data_left = 0;

        if(dec_cont->b_mc) {
          /* release buffer fully processed by SW */
          if(dec_cont->stream_consumed_callback.fn)
            dec_cont->stream_consumed_callback.fn((u8*)input->stream,
                                                  (void*)dec_cont->stream_consumed_callback.p_user_data);
        }
        DEC_API_TRC
        ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED\n");
#ifdef USE_RANDOM_TEST
        fwrite(input->stream, 1, input_data_len, dec_cont->ferror_stream);
        dec_cont->stream_not_consumed = 0;
#endif
        return H264DEC_STREAM_NOT_SUPPORTED;
      } else if((h264bsdAllocateSwResources(dec_cont->dwl,
                                            storage,
                                            (dec_cont->
                                             h264_profile_support == H264_HIGH_PROFILE) ? 1 :
                                            0,
                                            dec_cont->n_cores) != 0) ||
                (h264AllocateResources(dec_cont) != 0)) {
        /* signal that decoder failed to init parameter sets */
        /* TODO: what about views? */
        storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
        storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;

        /* reset strm_len to force memfail return also for secondary
         * view */
        strm_len = 0;

        return_value = H264DEC_MEMFAIL;
        DEC_API_TRC("H264DecDecode# H264DEC_MEMFAIL\n");
      } else {
        dec_cont->asic_buff->enable_dmv_and_poc = 0;
        storage->dpb->interlaced = (storage->active_sps->frame_mbs_only_flag == 0) ? 1 : 0;

        if((storage->active_pps->num_slice_groups != 1) &&
            (h264StreamIsBaseline(dec_cont) == 0)) {
          storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
          storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;

          return_value = H264DEC_STREAM_NOT_SUPPORTED;
          DEC_API_TRC
          ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED, FMO in Main/High Profile\n");
        } else if ((storage->active_pps->num_slice_groups != 1) && dec_cont->secure_mode) {
          storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
          storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS
          return_value = H264DEC_STRM_ERROR;
          DEC_API_TRC
              ("H264DecDecode# H264DEC_STREAM_ERROR, FMO in Secure Mode\n");
        }
        /* FMO always decoded in rlc mode */
        else if((storage->active_pps->num_slice_groups != 1) &&
            (dec_cont->rlc_mode == 0)) {
          /* set to uninit state */
          storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
          storage->active_view_sps_id[0] =
            storage->active_view_sps_id[1] =
              MAX_NUM_SEQ_PARAM_SETS;
          storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;
          storage->pic_started = HANTRO_FALSE;
          dec_cont->dec_stat = H264DEC_INITIALIZED;

          dec_cont->rlc_mode = 1;
          storage->prev_buf_not_finished = HANTRO_FALSE;
          DEC_API_TRC
          ("H264DecDecode# H264DEC_ADVANCED_TOOLS, FMO\n");

          return_value = H264DEC_ADVANCED_TOOLS;
        } else {
          u32 max_id = dec_cont->storage.dpb->no_reordering ? 1 :
                       dec_cont->storage.dpb->dpb_size;

          /* enable direct MV writing and POC tables for
           * high/main streams.
           * enable it also for any "baseline" stream which have
           * main/high tools enabled */
          if((storage->active_sps->profile_idc > 66 &&
              storage->active_sps->constrained_set0_flag == 0) ||
              (h264StreamIsBaseline(dec_cont) == 0)) {
            dec_cont->asic_buff->enable_dmv_and_poc = 1;
          }

          /* increase number of buffers if multiview decoding */
          if (dec_cont->storage.num_views) {
            max_id += dec_cont->storage.dpb->no_reordering ? 1 :
                      dec_cont->storage.dpb->dpb_size + 1;
            max_id = MIN(max_id, 16);
          }

          /* reset multibuffer status */
          if (storage->view == 0)
            h264PpMultiInit(dec_cont, max_id);
          else if (max_id > dec_cont->pp.multi_max_id)
            h264PpMultiMvc(dec_cont, max_id);

          DEC_API_TRC("H264DecDecode# H264DEC_HDRS_RDY\n");
          return_value = H264DEC_HDRS_RDY;
        }
      }

      if (!storage->view) {
        /* reset strm_len only for base view -> no HDRS_RDY to
         * application when param sets activated for stereo view */
        strm_len = 0;
        dec_cont->storage.second_field = 0;
      }

      /* Initialize DPB mode */
      if( !dec_cont->storage.active_sps->frame_mbs_only_flag &&
          dec_cont->allow_dpb_field_ordering )
        dec_cont->dpb_mode = DEC_DPB_INTERLACED_FIELD;
      else
        dec_cont->dpb_mode = DEC_DPB_FRAME;

      /* Initialize tiled mode */
      if( dec_cont->tiled_mode_support &&
          DecCheckTiledMode( dec_cont->tiled_mode_support,
                             dec_cont->dpb_mode,
                             !dec_cont->storage.active_sps->frame_mbs_only_flag ) !=
          HANTRO_OK ) {
        return_value = H264DEC_PARAM_ERROR;
        DEC_API_TRC
        ("H264DecDecode# H264DEC_PARAM_ERROR, tiled reference "\
         "mode invalid\n");
      }

      /* reset reference addresses, this is important for multicore
       * as we use this list to track ref picture usage
       */
      DWLmemset(dec_cont->asic_buff->ref_pic_list, 0,
                sizeof(dec_cont->asic_buff->ref_pic_list));

      break;
    }
#else
    case H264BSD_HDRS_RDY: {
      u32 dpb_realloc;
      dec_cont->storage.dpb->b_updated = 0;
      dec_cont->storage.dpb->n_ext_buf_size_added = dec_cont->n_ext_buf_size;
      dec_cont->storage.dpb->use_adaptive_buffers = dec_cont->use_adaptive_buffers;
      dec_cont->storage.dpb->n_guard_size = dec_cont->n_guard_size;
      dec_cont->pic_number = 0;

      dpb_realloc = IsDpbRealloc(dec_cont);

      h264CheckReleasePpAndHw(dec_cont);

      dec_cont->storage.multi_buff_pp =
        dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.pp_info.multi_buffer;

      if(storage->dpb->flushed && storage->dpb->num_out) {
        /* output first all DPB stored pictures */
        storage->dpb->flushed = 0;
        dec_cont->dec_stat = H264DEC_NEW_HEADERS;
        /* if display smoothing used -> indicate that all pictures
        * have to be read out */
        if((storage->dpb->tot_buffers >
            storage->dpb->dpb_size + 1) &&
            !dec_cont->storage.multi_buff_pp) {
          return_value = H264DEC_PENDING_FLUSH;
        } else {
          return_value = H264DEC_PIC_DECODED;
        }

        /* base view -> flush another view dpb */
        if (dec_cont->storage.num_views &&
            dec_cont->storage.view == 0) {
          h264bsdFlushDpb(storage->dpbs[1]);
        }
        DEC_API_TRC
        ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");
        strm_len = 0;

        break;
      } else if ((storage->dpb->tot_buffers >
                  storage->dpb->dpb_size + 1) && storage->dpb->num_out) {
        /* flush buffered output for display smoothing */
        storage->dpb->delayed_out = 0;
        storage->second_field = 0;
        dec_cont->dec_stat = H264DEC_NEW_HEADERS;
        return_value = H264DEC_PENDING_FLUSH;
        strm_len = 0;

        break;
      } else if (storage->pending_flush) {
        storage->pending_flush = 0;
        dec_cont->dec_stat = H264DEC_NEW_HEADERS;
        return_value = H264DEC_PENDING_FLUSH;
        strm_len = 0;

        if(dec_cont->b_mc) {
          h264MCWaitPicReadyAll(dec_cont);
          h264bsdFlushDpb(storage->dpb);
        }
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp.pp_instance == NULL) {
#if 0
          WaitListNotInUse(&dec_cont->fb_list);
          if (dec_cont->pp_enabled) {
            InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);
          }
#endif
          h264bsdFlushDpb(storage->dpb);
        }
#endif
        DEC_API_TRC
        ("H264DecDecode# H264DEC_PIC_DECODED (DPB flush caused by new SPS)\n");

        break;
      }

      if (dpb_realloc) {
        /* Make sure that all frame buffers are not in use before
        * reseting DPB (i.e. all HW cores are idle and all output
        * processed) */
        if(dec_cont->b_mc)
          h264MCWaitPicReadyAll(dec_cont);
#ifdef USE_OUTPUT_RELEASE
        if(dec_cont->pp.pp_instance == NULL && !dec_cont->b_mc) {

#ifndef USE_EXT_BUF_SAFE_RELEASE
          MarkListNotInUse(&dec_cont->fb_list);
#else
          WaitListNotInUse(&dec_cont->fb_list);
          if (dec_cont->pp_enabled) {
            InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);
          }
#endif
          WaitOutputEmpty(&dec_cont->fb_list);
        }
#endif
      }

      if(!h264SpsSupported(dec_cont)) {
        storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
        storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;
        storage->active_view_sps_id[0] =
          storage->active_view_sps_id[1] =
            MAX_NUM_SEQ_PARAM_SETS;
        storage->pic_started = HANTRO_FALSE;
        dec_cont->dec_stat = H264DEC_INITIALIZED;
        storage->prev_buf_not_finished = HANTRO_FALSE;
        output->data_left = 0;

        if(dec_cont->b_mc) {
          /* release buffer fully processed by SW */
          if(dec_cont->stream_consumed_callback.fn)
            dec_cont->stream_consumed_callback.fn((u8*)input->stream,
                                                  (void*)dec_cont->stream_consumed_callback.p_user_data);
        }

#ifdef USE_RANDOM_TEST
        fwrite(input->stream, 1, input_data_len, dec_cont->ferror_stream);
        dec_cont->stream_not_consumed = 0;
#endif

        DEC_API_TRC
        ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED\n");
        return H264DEC_STREAM_NOT_SUPPORTED;
      } else if (dpb_realloc) {
        if(dec_cont->b_mvc == 0)
          h264SetExternalBufferInfo(dec_cont, storage);
        else if(dec_cont->b_mvc == 1) {
          h264SetMVCExternalBufferInfo(dec_cont, storage);
        }
        dec_result = H264BSD_BUFFER_NOT_READY;
        dec_cont->dec_stat = H264DEC_WAITING_FOR_BUFFER;
        strm_len = 0;
        PushOutputPic(&dec_cont->fb_list, NULL, -2);
        return_value = H264DEC_HDRS_RDY;
      } else {
        dec_result = H264BSD_BUFFER_NOT_READY;
        dec_cont->dec_stat = H264DEC_WAITING_FOR_BUFFER;
        /* Need to exit the loop give a chance to call FinalizeOutputAll() */
        /* to output all the pending frames even when there is no need to */
        /* re-allocate external buffers. */
        strm_len = 0;
        return_value = H264DEC_STRM_PROCESSED;
        break;
      }

      if( !dec_cont->storage.active_sps->frame_mbs_only_flag &&
          dec_cont->allow_dpb_field_ordering )
        dec_cont->dpb_mode = DEC_DPB_INTERLACED_FIELD;
      else
        dec_cont->dpb_mode = DEC_DPB_FRAME;
      break;
    }
    case H264BSD_BUFFER_NOT_READY: {
      u32 ret = HANTRO_OK;
      if(dec_cont->b_mvc == 0)
        ret = h264bsdAllocateSwResources(dec_cont->dwl, storage,
                                         (dec_cont->
                                          h264_profile_support == H264_HIGH_PROFILE) ? 1 :
                                         0,
                                         dec_cont->n_cores);
      else if(dec_cont->b_mvc == 1) {
        ret = h264bsdMVCAllocateSwResources(dec_cont->dwl, storage,
                                            (dec_cont->
                                             h264_profile_support == H264_HIGH_PROFILE) ? 1 :
                                            0,
                                            dec_cont->n_cores);
        dec_cont->b_mvc = 2;
      }
      if (ret != HANTRO_OK) goto RESOURCE_NOT_READY;
      ret =  h264AllocateResources(dec_cont);
      if (ret != HANTRO_OK) goto RESOURCE_NOT_READY;

RESOURCE_NOT_READY:
      if(ret) {
        if(ret == H264DEC_WAITING_FOR_BUFFER) {
          dec_cont->buffer_index[0] = dec_cont->buffer_index[1] = 0;
          return_value = ret;
          strm_len = 0;
          break;
        } else {
          /* signal that decoder failed to init parameter sets */
          /* TODO: what about views? */
          storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
          storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;

          /* reset strm_len to force memfail return also for secondary
           * view */
          strm_len = 0;

          return_value = H264DEC_MEMFAIL;
          DEC_API_TRC("H264DecDecode# H264DEC_MEMFAIL\n");
        }
        strm_len = 0;
      } else {
        if((storage->active_pps->num_slice_groups != 1) &&
            (h264StreamIsBaseline(dec_cont) == 0)) {
          storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
          storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;

          return_value = H264DEC_STREAM_NOT_SUPPORTED;
          DEC_API_TRC
          ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED, FMO in Main/High Profile\n");
        }

        dec_cont->asic_buff->enable_dmv_and_poc = 0;
        storage->dpb->interlaced = (storage->active_sps->frame_mbs_only_flag == 0) ? 1 : 0;

        /* FMO always decoded in rlc mode */
        if((storage->active_pps->num_slice_groups != 1) &&
            (dec_cont->rlc_mode == 0)) {
          /* set to uninit state */
          storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
          storage->active_view_sps_id[0] =
            storage->active_view_sps_id[1] =
              MAX_NUM_SEQ_PARAM_SETS;
          storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;
          storage->pic_started = HANTRO_FALSE;
          dec_cont->dec_stat = H264DEC_INITIALIZED;

          dec_cont->rlc_mode = 1;
          storage->prev_buf_not_finished = HANTRO_FALSE;
          DEC_API_TRC
          ("H264DecDecode# H264DEC_ADVANCED_TOOLS, FMO\n");

          return_value = H264DEC_ADVANCED_TOOLS;
        } else {
          u32 max_id = dec_cont->storage.dpb->no_reordering ? 1 :
                       dec_cont->storage.dpb->dpb_size;

          /* enable direct MV writing and POC tables for
           * high/main streams.
           * enable it also for any "baseline" stream which have
           * main/high tools enabled */
          if((storage->active_sps->profile_idc > 66 &&
              storage->active_sps->constrained_set0_flag == 0) ||
              (h264StreamIsBaseline(dec_cont) == 0)) {
            dec_cont->asic_buff->enable_dmv_and_poc = 1;
          }

          /* increase number of buffers if multiview decoding */
          if (dec_cont->storage.num_views) {
            max_id += dec_cont->storage.dpb->no_reordering ? 1 :
                      dec_cont->storage.dpb->dpb_size + 1;
            max_id = MIN(max_id, 16);
          }

          /* reset multibuffer status */
          if (storage->view == 0)
            h264PpMultiInit(dec_cont, max_id);
          else if (max_id > dec_cont->pp.multi_max_id)
            h264PpMultiMvc(dec_cont, max_id);

          DEC_API_TRC("H264DecDecode# H264DEC_HDRS_RDY\n");
        }
      }

      if (!storage->view) {
        /* reset strm_len only for base view -> no HDRS_RDY to
         * application when param sets activated for stereo view */
        strm_len = 0;
        dec_cont->storage.second_field = 0;
      }

      /* Initialize DPB mode */
      if( !dec_cont->storage.active_sps->frame_mbs_only_flag &&
          dec_cont->allow_dpb_field_ordering )
        dec_cont->dpb_mode = DEC_DPB_INTERLACED_FIELD;
      else
        dec_cont->dpb_mode = DEC_DPB_FRAME;

      /* Initialize tiled mode */
      if( dec_cont->tiled_mode_support &&
          DecCheckTiledMode( dec_cont->tiled_mode_support,
                             dec_cont->dpb_mode,
                             !dec_cont->storage.active_sps->frame_mbs_only_flag ) !=
          HANTRO_OK ) {
        return_value = H264DEC_PARAM_ERROR;
        DEC_API_TRC
        ("H264DecDecode# H264DEC_PARAM_ERROR, tiled reference "\
         "mode invalid\n");
      }

      /* reset reference addresses, this is important for multicore
       * as we use this list to track ref picture usage
       */
      DWLmemset(dec_cont->asic_buff->ref_pic_list, 0,
                sizeof(dec_cont->asic_buff->ref_pic_list));
      dec_cont->dec_stat = H264DEC_INITIALIZED;
      break;
    }
#endif
    case H264BSD_PIC_RDY: {
      u32 asic_status;
      u32 picture_broken;
      DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

      picture_broken = (storage->picture_broken && storage->intra_freeze &&
                        !IS_IDR_NAL_UNIT(storage->prev_nal_unit));

      /* frame number workaround */
      if (!dec_cont->rlc_mode && dec_cont->workarounds.h264.frame_num &&
          !dec_cont->secure_mode) {
        u32 tmp;
        dec_cont->force_nal_mode =
          h264bsdFixFrameNum((u8*)tmp_stream - num_read_bytes,
                             strm_len + num_read_bytes,
                             storage->slice_header->frame_num,
                             storage->active_sps->max_frame_num, &tmp);

        /* stuff skipped before slice start code */
        if (dec_cont->force_nal_mode && tmp > 0) {
          dec_cont->p_hw_stream_start += tmp;
          dec_cont->hw_stream_start_bus += tmp;
          dec_cont->hw_length -= tmp;
        }
      }

      if( dec_cont->dec_stat != H264DEC_BUFFER_EMPTY && !picture_broken) {
        /* setup the reference frame list; just at picture start */
        dpbStorage_t *dpb = storage->dpb;
        dpbPicture_t *buffer = dpb->buffer;

        /* list in reorder */
        u32 i;

        if(!h264PpsSupported(dec_cont)) {
          storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
          storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;

          return_value = H264DEC_STREAM_NOT_SUPPORTED;
          DEC_API_TRC
          ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED, Main/High Profile tools detected\n");
          goto end;
        }

        if((dec_cont->is8190 == 0) && (dec_cont->rlc_mode == 0)) {
          for(i = 0; i < dpb->dpb_size; i++) {
            p_asic_buff->ref_pic_list[i] =
              buffer[dpb->list[i]].data->bus_address;
          }
        } else {
          for(i = 0; i < dpb->dpb_size; i++) {
            p_asic_buff->ref_pic_list[i] =
              buffer[i].data->bus_address;
          }
        }

        /* Multicore: increment usage for DPB buffers */
        IncrementDPBRefCount(dpb);

        p_asic_buff->max_ref_frm = dpb->max_ref_frames;
        p_asic_buff->out_buffer = storage->curr_image->data;
        p_asic_buff->out_pp_buffer = storage->curr_image->pp_data;

        p_asic_buff->chroma_qp_index_offset =
          storage->active_pps->chroma_qp_index_offset;
        p_asic_buff->chroma_qp_index_offset2 =
          storage->active_pps->chroma_qp_index_offset2;
        p_asic_buff->filter_disable = 0;

        h264bsdDecodePicOrderCnt(storage->poc,
                                 storage->active_sps,
                                 storage->slice_header,
                                 storage->prev_nal_unit);

#ifdef ENABLE_DPB_RECOVER
        if (storage->dpb->try_recover_dpb
            /* && storage->slice_header->firstMbInSlice */
            && IS_I_SLICE(storage->slice_header->slice_type)
            && !IS_IDR_NAL_UNIT(storage->prev_nal_unit)
            && 2 * storage->dpb->max_ref_frames <= storage->dpb->max_frame_num)
          h264DpbRecover(storage->dpb, storage->slice_header->frame_num,
                         MIN(storage->poc->pic_order_cnt[0], storage->poc->pic_order_cnt[1]));
#endif
        if(dec_cont->rlc_mode) {
          if(storage->num_concealed_mbs == storage->pic_size_in_mbs) {
            p_asic_buff->whole_pic_concealed = 1;
            p_asic_buff->filter_disable = 1;
            p_asic_buff->chroma_qp_index_offset = 0;
            p_asic_buff->chroma_qp_index_offset2 = 0;
          } else {
            p_asic_buff->whole_pic_concealed = 0;
          }

          PrepareIntra4x4ModeData(storage, p_asic_buff);
          PrepareMvData(storage, p_asic_buff);
          PrepareRlcCount(storage, p_asic_buff);
        } else {
          H264SetupVlcRegs(dec_cont);
        }

        H264ErrorRecover(dec_cont);

        DEBUG_PRINT(("Save DPB status\n"));
        /* we trust our memcpy; ignore return value */
        (void) DWLmemcpy(&storage->dpb[1], &storage->dpb[0],
                         sizeof(*storage->dpb));

        DEBUG_PRINT(("Save POC status\n"));
        (void) DWLmemcpy(&storage->poc[1], &storage->poc[0],
                         sizeof(*storage->poc));

        h264bsdCroppingParams(storage,
                              &storage->dpb->current_out->crop);

        /* create output picture list */
        h264UpdateAfterPictureDecode(dec_cont);

        /* enable output writing by default */
        dec_cont->asic_buff->disable_out_writing = 0;

        /* prepare PP if needed */
        h264PreparePpRun(dec_cont);
      } else {
        dec_cont->dec_stat = H264DEC_INITIALIZED;
      }

      /* disallow frame-mode DPB and tiled mode when decoding interlaced
       * content */
      if( dec_cont->dpb_mode == DEC_DPB_FRAME &&
          dec_cont->storage.active_sps &&
          !dec_cont->storage.active_sps->frame_mbs_only_flag &&
          dec_cont->tiled_reference_enable ) {
        DEC_API_TRC("DPB mode does not support tiled reference "\
                    "pictures");
        return H264DEC_STRM_ERROR;
      }

      if (dec_cont->storage.partial_freeze) {
        PreparePartialFreeze((u8*)storage->curr_image->data->virtual_address,
                             h264bsdPicWidth(&dec_cont->storage),
                             h264bsdPicHeight(&dec_cont->storage));
      }

      /* run asic and react to the status */
      if( !picture_broken ) {
        asic_status = H264RunAsic(dec_cont, p_asic_buff);
      } else {
        if( dec_cont->storage.pic_started ) {
          if( !storage->slice_header->field_pic_flag ||
              !storage->second_field ) {
            h264InitPicFreezeOutput(dec_cont, 0);
            h264UpdateAfterPictureDecode(dec_cont);
          }
        }
        asic_status = DEC_8190_IRQ_ERROR;
      }

      if (storage->view)
        storage->non_inter_view_ref = 0;

      /* Handle system error situations */
      if(asic_status == X170_DEC_TIMEOUT) {
        /* This timeout is DWL(software/os) generated */
        DEC_API_TRC
        ("H264DecDecode# H264DEC_HW_TIMEOUT, SW generated\n");
        return H264DEC_HW_TIMEOUT;
      } else if(asic_status == X170_DEC_SYSTEM_ERROR) {
        DEC_API_TRC("H264DecDecode# H264DEC_SYSTEM_ERROR\n");
        return H264DEC_SYSTEM_ERROR;
      } else if(asic_status == X170_DEC_HW_RESERVED) {
        DEC_API_TRC("H264DecDecode# H264DEC_HW_RESERVED\n");
        return H264DEC_HW_RESERVED;
      }

      /* Handle possible common HW error situations */
      if(asic_status & DEC_8190_IRQ_BUS) {
        output->strm_curr_pos = (u8 *) input->stream;
        output->strm_curr_bus_address = input->stream_bus_address;
        output->data_left = input_data_len;

#ifdef USE_RANDOM_TEST
        dec_cont->stream_not_consumed = 1;
#endif

        DEC_API_TRC("H264DecDecode# H264DEC_HW_BUS_ERROR\n");
        return H264DEC_HW_BUS_ERROR;
      } else if(asic_status &
                (DEC_8190_IRQ_TIMEOUT | DEC_8190_IRQ_ABORT)) {
        /* This timeout is HW generated */
        DEBUG_PRINT(("IRQ: HW %s\n",
                     (asic_status & DEC_8190_IRQ_TIMEOUT) ?
                     "TIMEOUT" : "ABORT"));

#ifdef H264_TIMEOUT_ASSERT
        ASSERT(0);
#endif
        if(dec_cont->packet_decoded != HANTRO_TRUE) {
          DEBUG_PRINT(("reset pic_started\n"));
          dec_cont->storage.pic_started = HANTRO_FALSE;
        }

        /* for concealment after a HW error report we use the saved reference list */
        if (dec_cont->storage.partial_freeze) {
          dpbStorage_t *dpb_partial = &dec_cont->storage.dpb[1];
          do {
            ref_data = h264bsdGetRefPicDataVlcMode(dpb_partial,
                                                   dpb_partial->list[index], 0);
            index++;
          } while(index < 16 && ref_data == NULL);
        }

        if (!dec_cont->storage.partial_freeze ||
            !ProcessPartialFreeze((u8*)storage->curr_image->data->virtual_address,
                                  ref_data,
                                  h264bsdPicWidth(&dec_cont->storage),
                                  h264bsdPicHeight(&dec_cont->storage),
                                  dec_cont->storage.partial_freeze == 1)) {
          dec_cont->storage.picture_broken = HANTRO_TRUE;
          h264InitPicFreezeOutput(dec_cont, 1);
        } else {
          asic_status &= ~DEC_8190_IRQ_TIMEOUT;
          asic_status |= DEC_8190_IRQ_RDY;
          dec_cont->storage.picture_broken = HANTRO_FALSE;
        }

        /* PP has to run again for the concealed picture */
        if(dec_cont->pp.pp_instance != NULL) {
          TRACE_PP_CTRL
          ("H264DecDecode: Concealed picture, PP should run again\n");
          dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;

          if(dec_cont->pp.pp_info.multi_buffer != 0) {
            if(dec_cont->pp.dec_pp_if.use_pipeline != 0) {
              /* remove pipelined pic from PP list */
              h264PpMultiRemovePic(dec_cont, storage->curr_image->data);
            }

            if(dec_cont->pp.queued_pic_to_pp[storage->view] ==
                storage->curr_image->data) {
              /* current picture cannot be in the queue */
              dec_cont->pp.queued_pic_to_pp[storage->view] =
                NULL;
            }
          }
        }

        if(!dec_cont->rlc_mode) {
          strmData_t *strm = dec_cont->storage.strm;
          const u8 *next =
            h264bsdFindNextStartCode(strm->p_strm_buff_start,
                                     strm->strm_buff_size);

          if(next != NULL) {
            u32 consumed;

            tmp_stream -= num_read_bytes;
            strm_len += num_read_bytes;

            consumed = (u32) (next - tmp_stream);
            tmp_stream += consumed;
            strm_len -= consumed;
          }
        }

        dec_cont->stream_pos_updated = 0;
        dec_cont->pic_number++;

        dec_cont->packet_decoded = HANTRO_FALSE;
        storage->skip_redundant_slices = HANTRO_TRUE;

        /* Remove this NAL unit from stream */
        strm_len = 0;
        DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED\n");
        return_value = H264DEC_PIC_DECODED;
        break;
      }

      if(dec_cont->rlc_mode) {
        if(asic_status & DEC_8190_IRQ_ERROR) {
          DEBUG_PRINT
          (("H264DecDecode# IRQ_STREAM_ERROR in RLC mode)!\n"));
        }

        /* It was rlc mode, but switch back to vlc when allowed */
        if(dec_cont->try_vlc) {
          storage->prev_buf_not_finished = HANTRO_FALSE;
          DEBUG_PRINT(("H264DecDecode: RLC mode used, but try VLC again\n"));
          /* start using VLC mode again */
          dec_cont->rlc_mode = 0;
          dec_cont->try_vlc = 0;
          dec_cont->mode_change = 0;
        }

        dec_cont->pic_number++;

#ifdef FFWD_WORKAROUND
        storage->prev_idr_pic_ready =
          IS_IDR_NAL_UNIT(storage->prev_nal_unit);
#endif /* FFWD_WORKAROUND */



        DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED\n");
        return_value = H264DEC_PIC_DECODED;
        strm_len = 0;

        break;
      }

      /* from down here we handle VLC mode */

      /* in High/Main streams if HW model returns ASO interrupt, it
       * really means that there is a generic stream error. */
      if((asic_status & DEC_8190_IRQ_ASO) &&
          (p_asic_buff->enable_dmv_and_poc != 0 ||
           (h264StreamIsBaseline(dec_cont) == 0))) {
        DEBUG_PRINT(("ASO received in High/Main stream => STREAM_ERROR\n"));
        asic_status &= ~DEC_8190_IRQ_ASO;
        asic_status |= DEC_8190_IRQ_ERROR;
      }

      /* in Secure mode if HW model returns ASO interrupt, decoder treat
         it as error */
      if ((asic_status & DEC_8190_IRQ_ASO) && dec_cont->secure_mode) {
        DEBUG_PRINT(("ASO received in secure mode => STREAM_ERROR\n"));
        asic_status &= ~DEC_8190_IRQ_ASO;
        asic_status |= DEC_8190_IRQ_ERROR;
      }

      /* Check for CABAC zero words here */
      if( asic_status & DEC_8190_IRQ_BUFFER) {
        if( dec_cont->storage.active_pps->entropy_coding_mode_flag && !dec_cont->secure_mode) {
          u32 tmp;
          u32 check_words = 1;
          strmData_t strm_tmp = *dec_cont->storage.strm;
          tmp = dec_cont->p_hw_stream_start - strm_tmp.p_strm_buff_start;
          strm_tmp.strm_curr_pos = dec_cont->p_hw_stream_start;
          strm_tmp.strm_buff_read_bits = 8*tmp;
          strm_tmp.bit_pos_in_word = 0;
          strm_tmp.strm_buff_size = input_data_len - (strm_tmp.p_strm_buff_start - input->stream);

          tmp = GetDecRegister(dec_cont->h264_regs, HWIF_START_CODE_E );
          /* In NAL unit mode, if NAL unit was of type
           * "reserved" or sth other unsupported one, we need
           * to skip zero word check. */
          if( tmp == 0 ) {
            tmp = input->stream[0] & 0x1F;
            if( tmp != NAL_CODED_SLICE &&
                tmp != NAL_CODED_SLICE_IDR )
              check_words = 0;
          }

          if(check_words) {
            tmp = h264CheckCabacZeroWords( &strm_tmp );
            if( tmp != HANTRO_OK ) {
              DEBUG_PRINT(("CABAC_ZERO_WORD error after packet => STREAM_ERROR\n"));
            } /* cabacZeroWordError */
          }
        }
      }

      /* Handle ASO */
      if(asic_status & DEC_8190_IRQ_ASO) {
        DEBUG_PRINT(("IRQ: ASO dedected\n"));
        ASSERT(dec_cont->rlc_mode == 0);

        dec_cont->reallocate = 1;
        dec_cont->try_vlc = 1;
        dec_cont->mode_change = 1;

        /* restore DPB status */
        DEBUG_PRINT(("Restore DPB status\n"));

        /* remove any pictures marked for output */
        if (!dec_cont->storage.sei.buffering_period_info.exist_flag || !dec_cont->storage.sei.pic_timing_info.exist_flag)
          RemoveTempOutputAll(&dec_cont->fb_list);
        else
          h264RemoveNoBumpOutput(&storage->dpb[0], (&storage->dpb[0])->num_out - (&storage->dpb[1])->num_out);

        if(dec_cont->pp_enabled) {
          InputQueueReturnBuffer(dec_cont->pp_buffer_queue, storage->dpb->current_out->ds_data->virtual_address);
        }
        /* we trust our memcpy; ignore return value */
        (void) DWLmemcpy(&storage->dpb[0], &storage->dpb[1],
                         sizeof(dpbStorage_t));

        DEBUG_PRINT(("Restore POC status\n"));
        (void) DWLmemcpy(&storage->poc[0], &storage->poc[1],
                         sizeof(*storage->poc));

        storage->skip_redundant_slices = HANTRO_FALSE;
        storage->aso_detected = 1;

        DEC_API_TRC("H264DecDecode# H264DEC_ADVANCED_TOOLS, ASO\n");
        return_value = H264DEC_ADVANCED_TOOLS;

        /* PP has to run again for ASO picture */
        if(dec_cont->pp.pp_instance != NULL) {
          TRACE_PP_CTRL
          ("H264DecDecode: ASO detected, PP should run again\n");
          dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;

          if(dec_cont->pp.pp_info.multi_buffer != 0) {
            if(dec_cont->pp.dec_pp_if.use_pipeline != 0) {
              /* remove pipelined pic from PP list */
              h264PpMultiRemovePic(dec_cont, storage->curr_image->data);
            }

            if(dec_cont->pp.queued_pic_to_pp[storage->view] ==
                storage->curr_image->data) {
              /* current picture cannot be in the queue */
              dec_cont->pp.queued_pic_to_pp[storage->view] =
                NULL;
            }
          }
        }

        goto end;
      } else if(asic_status & DEC_8190_IRQ_BUFFER) {
        DEBUG_PRINT(("IRQ: BUFFER EMPTY\n"));

        /* a packet successfully decoded, don't reset pic_started flag if
         * there is a need for rlc mode */
        dec_cont->dec_stat = H264DEC_BUFFER_EMPTY;
        dec_cont->packet_decoded = HANTRO_TRUE;
        output->data_left = 0;

        if (dec_cont->force_nal_mode) {
          u32 tmp;
          const u8 *next;

          next =
            h264bsdFindNextStartCode(dec_cont->p_hw_stream_start,
                                     dec_cont->hw_length);

          if(next != NULL) {
            tmp = next - dec_cont->p_hw_stream_start;
            dec_cont->p_hw_stream_start += tmp;
            dec_cont->hw_stream_start_bus += tmp;
            dec_cont->hw_length -= tmp;
            tmp_stream = dec_cont->p_hw_stream_start;
            strm_len = dec_cont->hw_length;
            continue;
          }
        }

#ifdef USE_RANDOM_TEST
        fwrite(input->stream, 1, input_data_len, dec_cont->ferror_stream);
        dec_cont->stream_not_consumed = 0;
#endif

        DEC_API_TRC
        ("H264DecDecode# H264DEC_STRM_PROCESSED, give more data\n");
        return H264DEC_BUF_EMPTY;
      }
      /* Handle stream error detected in HW */
      else if(asic_status & DEC_8190_IRQ_ERROR) {
        DEBUG_PRINT(("IRQ: STREAM ERROR detected\n"));
        if(dec_cont->packet_decoded != HANTRO_TRUE) {
          DEBUG_PRINT(("reset pic_started\n"));
          dec_cont->storage.pic_started = HANTRO_FALSE;
        }
        {
          strmData_t *strm = dec_cont->storage.strm;
          const u8 *next =
            h264bsdFindNextStartCode(strm->p_strm_buff_start,
                                     strm->strm_buff_size);

          if(next != NULL) {
            u32 consumed;

            tmp_stream -= num_read_bytes;
            strm_len += num_read_bytes;

            consumed = (u32) (next - tmp_stream);
            tmp_stream += consumed;
            strm_len -= consumed;
          }
        }

        /* REMEMBER TO UPDATE(RESET) STREAM POSITIONS */
        ASSERT(dec_cont->rlc_mode == 0);

        /* for concealment after a HW error report we use the saved reference list */
        if (dec_cont->storage.partial_freeze) {
          dpbStorage_t *dpb_partial = &dec_cont->storage.dpb[1];
          do {
            ref_data = h264bsdGetRefPicDataVlcMode(dpb_partial,
                                                   dpb_partial->list[index], 0);
            index++;
          } while(index < 16 && ref_data == NULL);
        }

        if (!dec_cont->storage.partial_freeze ||
            !ProcessPartialFreeze((u8*)storage->curr_image->data->virtual_address,
                                  ref_data,
                                  h264bsdPicWidth(&dec_cont->storage),
                                  h264bsdPicHeight(&dec_cont->storage),
                                  dec_cont->storage.partial_freeze == 1)) {
          dec_cont->storage.picture_broken = HANTRO_TRUE;
          dec_cont->storage.dpb->try_recover_dpb = HANTRO_TRUE;
          h264InitPicFreezeOutput(dec_cont, 1);
        } else {
          asic_status &= ~DEC_8190_IRQ_ERROR;
          asic_status |= DEC_8190_IRQ_RDY;
          dec_cont->storage.picture_broken = HANTRO_FALSE;
        }

        /* PP has to run again for the concealed picture */
        if(dec_cont->pp.pp_instance != NULL) {

          TRACE_PP_CTRL
          ("H264DecDecode: Concealed picture, PP should run again\n");
          dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;

          if(dec_cont->pp.pp_info.multi_buffer != 0) {
            if(dec_cont->pp.dec_pp_if.use_pipeline != 0) {
              /* remove pipelined pic from PP list */
              h264PpMultiRemovePic(dec_cont, storage->curr_image->data);
            }

            if(dec_cont->pp.queued_pic_to_pp[storage->view] ==
                storage->curr_image->data) {
              /* current picture cannot be in the queue */
              dec_cont->pp.queued_pic_to_pp[storage->view] =
                NULL;
            }
          }
        }

        /* HW returned stream position is not valid in this case */
        dec_cont->stream_pos_updated = 0;
      } else { /* OK in here */
        if( IS_IDR_NAL_UNIT(storage->prev_nal_unit) ) {
          dec_cont->storage.picture_broken = HANTRO_FALSE;
        }
      }

      if( dec_cont->storage.active_pps->entropy_coding_mode_flag &&
          (asic_status & DEC_8190_IRQ_ERROR) == 0 && !dec_cont->secure_mode) {
        u32 tmp;

        strmData_t strm_tmp = *dec_cont->storage.strm;
        tmp = dec_cont->p_hw_stream_start - strm_tmp.p_strm_buff_start;
        strm_tmp.strm_curr_pos = dec_cont->p_hw_stream_start;
        strm_tmp.strm_buff_read_bits = 8*tmp;
        strm_tmp.bit_pos_in_word = 0;
        strm_tmp.strm_buff_size = input_data_len - (strm_tmp.p_strm_buff_start - input->stream);
        tmp = h264CheckCabacZeroWords( &strm_tmp );
        if( tmp != HANTRO_OK ) {
          DEBUG_PRINT(("Error decoding CABAC zero words\n"));
          {
            strmData_t *strm = dec_cont->storage.strm;
            const u8 *next =
              h264bsdFindNextStartCode(strm->p_strm_buff_start,
                                       strm->strm_buff_size);

            if(next != NULL) {
              u32 consumed;

              tmp_stream -= num_read_bytes;
              strm_len += num_read_bytes;

              consumed = (u32) (next - tmp_stream);
              tmp_stream += consumed;
              strm_len -= consumed;
            }
          }

          ASSERT(dec_cont->rlc_mode == 0);
        } else {
          i32 remain = input_data_len - (strm_tmp.strm_curr_pos - input->stream);

          /* byte stream format if starts with 0x000001 or 0x000000 */
          if (remain > 3 && strm_tmp.strm_curr_pos[0] == 0x00 &&
              strm_tmp.strm_curr_pos[1] == 0x00 &&
              (strm_tmp.strm_curr_pos[2] & 0xFE) == 0x00) {
            const u8 *next =
                h264bsdFindNextStartCode(strm_tmp.strm_curr_pos, remain);

            u32 consumed;
            if(next != NULL)
              consumed = (u32) (next - input->stream);
            else
              consumed = input_data_len;

            if(consumed != 0) {
              dec_cont->stream_pos_updated = 0;
              tmp_stream = input->stream + consumed;
            }
          }
        }
      }

      /* For the switch between modes */
      /* this is a sign for RLC mode + mb error conceal NOT to reset pic_started flag */
      dec_cont->packet_decoded = HANTRO_FALSE;

      DEBUG_PRINT(("Skip redundant VLC\n"));
      storage->skip_redundant_slices = HANTRO_TRUE;
      dec_cont->pic_number++;

#ifdef FFWD_WORKAROUND
      storage->prev_idr_pic_ready =
        IS_IDR_NAL_UNIT(storage->prev_nal_unit);
#endif /* FFWD_WORKAROUND */

      return_value = H264DEC_PIC_DECODED;
      strm_len = 0;
      break;
    }
    case H264BSD_PARAM_SET_ERROR: {
      if(!h264bsdCheckValidParamSets(&dec_cont->storage) &&
          strm_len == 0) {
        DEC_API_TRC
        ("H264DecDecode# H264DEC_STRM_ERROR, Invalid parameter set(s)\n");
        return_value = H264DEC_STRM_ERROR;
      }

      /* update HW buffers if VLC mode */
      if(!dec_cont->rlc_mode) {
        dec_cont->hw_length -= num_read_bytes;
        dec_cont->hw_stream_start_bus = input->stream_bus_address +
                                        (u32) (tmp_stream - input->stream);

        dec_cont->p_hw_stream_start = tmp_stream;
      }

      /* If no active sps, no need to check */
      if(storage->active_sps && !h264SpsSupported(dec_cont)) {
        storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
        storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;
        storage->active_view_sps_id[0] =
          storage->active_view_sps_id[1] =
            MAX_NUM_SEQ_PARAM_SETS;
        storage->pic_started = HANTRO_FALSE;
        dec_cont->dec_stat = H264DEC_INITIALIZED;
        storage->prev_buf_not_finished = HANTRO_FALSE;

        if(dec_cont->b_mc) {
          /* release buffer fully processed by SW */
          if(dec_cont->stream_consumed_callback.fn)
            dec_cont->stream_consumed_callback.fn((u8*)input->stream,
                                                  (void*)dec_cont->stream_consumed_callback.p_user_data);
        }
        DEC_API_TRC
        ("H264DecDecode# H264DEC_STREAM_NOT_SUPPORTED\n");
        return_value = H264DEC_STREAM_NOT_SUPPORTED;
        goto end;
      }

      break;
    }
    case H264BSD_NEW_ACCESS_UNIT: {
      h264CheckReleasePpAndHw(dec_cont);

      dec_cont->stream_pos_updated = 0;

      dec_cont->storage.picture_broken = HANTRO_TRUE;
      h264InitPicFreezeOutput(dec_cont, 0);

      h264UpdateAfterPictureDecode(dec_cont);
      if(dec_cont->pp_enabled)
        InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->storage.dpb->current_out->ds_data->virtual_address);

      /* PP will run in H264DecNextPicture() for this concealed picture */

      DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED, NEW_ACCESS_UNIT\n");
      return_value = H264DEC_PIC_DECODED;

      dec_cont->pic_number++;
      strm_len = 0;

      break;
    }
    case H264BSD_FMO: { /* If picture parameter set changed and FMO detected */
      DEBUG_PRINT(("FMO detected\n"));

      ASSERT(dec_cont->rlc_mode == 0);
      ASSERT(dec_cont->reallocate == 1);

      /* tmp_stream = input->stream; */

      DEC_API_TRC("H264DecDecode# H264DEC_ADVANCED_TOOLS, FMO\n");
      return_value = H264DEC_ADVANCED_TOOLS;

      strm_len = 0;
      break;
    }
    case H264BSD_UNPAIRED_FIELD: {
      /* unpaired field detected and PP still running (wait after
       * second field decoded) -> wait here */
      h264CheckReleasePpAndHw(dec_cont);

      DEC_API_TRC("H264DecDecode# H264DEC_PIC_DECODED, UNPAIRED_FIELD\n");
      return_value = H264DEC_PIC_DECODED;

      strm_len = 0;
      break;
    }
#ifdef USE_OUTPUT_RELEASE
    case H264BSD_ABORTED:
      dec_cont->dec_stat = H264DEC_ABORTED;
      return H264DEC_ABORTED;
#endif
    case H264BSD_ERROR_DETECTED: {
      return_value = H264DEC_STREAM_ERROR_DEDECTED;
      strm_len = 0;
      break;
    }
    case H264BSD_NONREF_PIC_SKIPPED:
      return_value = H264DEC_NONREF_PIC_SKIPPED;
    /* fall through */
    default: { /* case H264BSD_ERROR, H264BSD_RDY */
      dec_cont->hw_length -= num_read_bytes;
      dec_cont->hw_stream_start_bus = input->stream_bus_address +
                                      (u32) (tmp_stream - input->stream);

      dec_cont->p_hw_stream_start = tmp_stream;
    }
    }
  } while(strm_len);

end:

  /*  If Hw decodes stream, update stream buffers from "storage" */
  if(dec_cont->stream_pos_updated) {
    if (dec_cont->secure_mode)
      output->data_left = 0;
    else {
      output->strm_curr_pos = (u8 *) dec_cont->p_hw_stream_start;
      output->strm_curr_bus_address = dec_cont->hw_stream_start_bus;
      output->data_left = dec_cont->hw_length;
    }
  } else {
    /* else update based on SW stream decode stream values */
    u32 data_consumed = (u32) (tmp_stream - input->stream);

    output->strm_curr_pos = (u8 *) tmp_stream;
    output->strm_curr_bus_address = input->stream_bus_address + data_consumed;

    output->data_left = input_data_len - data_consumed;
  }
  if (dec_cont->storage.sei.buffering_period_info.exist_flag && dec_cont->storage.sei.pic_timing_info.exist_flag) {
    if(return_value == H264DEC_PIC_DECODED && dec_cont->dec_stat != H264DEC_NEW_HEADERS) {
      dec_cont->storage.sei.compute_time_info.access_unit_size = output->strm_curr_pos - input->stream;
      dec_cont->storage.sei.bumping_flag = 1;
    }
  }

  /* Workaround for too big data_left value from error stream */
  if (output->data_left > input_data_len) {
    output->data_left = input_data_len;
  }

#ifdef USE_RANDOM_TEST
  fwrite(input->stream, 1, (input_data_len - output->data_left), dec_cont->ferror_stream);

  if (output->data_left == input_data_len)
    dec_cont->stream_not_consumed = 1;
  else
    dec_cont->stream_not_consumed = 0;
#endif

  FinalizeOutputAll(&dec_cont->fb_list);

  if(return_value == H264DEC_PIC_DECODED || return_value == H264DEC_STREAM_ERROR_DEDECTED) {
    dec_cont->gaps_checked_for_this = HANTRO_FALSE;
  }
#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp.pp_instance == NULL && !dec_cont->b_mc) {
    u32 ret;
    H264DecPicture output;
    u32 flush_all = 0;

    if (return_value == H264DEC_PENDING_FLUSH)
      flush_all = 1;

    //if(return_value == H264DEC_PIC_DECODED || return_value == H264DEC_PENDING_FLUSH)
    {
      do {
        ret = H264DecNextPicture_INTERNAL(dec_cont, &output, flush_all);
      } while( ret == H264DEC_PIC_RDY);
    }
  }
#endif
  if(dec_cont->b_mc) {
    if(return_value == H264DEC_PIC_DECODED || return_value == H264DEC_PENDING_FLUSH) {
      h264MCPushOutputAll(dec_cont);
    } else if(output->data_left == 0) {
      /* release buffer fully processed by SW */
      if(dec_cont->stream_consumed_callback.fn)
        dec_cont->stream_consumed_callback.fn((u8*)input->stream,
                                              (void*)dec_cont->stream_consumed_callback.p_user_data);

    }
  }
#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->abort)
    return(H264DEC_ABORTED);
  else
#endif
    return (return_value);
}

/*------------------------------------------------------------------------------
    Function name : H264DecGetAPIVersion
    Description   : Return the API version information

    Return type   : H264DecApiVersion
    Argument      : void
------------------------------------------------------------------------------*/
H264DecApiVersion H264DecGetAPIVersion(void) {
  H264DecApiVersion ver;

  ver.major = H264DEC_MAJOR_VERSION;
  ver.minor = H264DEC_MINOR_VERSION;

  DEC_API_TRC("H264DecGetAPIVersion# OK\n");

  return ver;
}

/*------------------------------------------------------------------------------
    Function name : H264DecGetBuild
    Description   : Return the SW and HW build information

    Return type   : H264DecBuild
    Argument      : void
------------------------------------------------------------------------------*/
H264DecBuild H264DecGetBuild(void) {
  H264DecBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_H264_DEC);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_H264_DEC);

  DEC_API_TRC("H264DecGetBuild# OK\n");

  return build_info;
}

/*------------------------------------------------------------------------------

    Function: H264DecNextPicture

        Functional description:
            Get next picture in display order if any available.

        Input:
            dec_inst     decoder instance.
            end_of_stream force output of all buffered pictures

        Output:
            output     pointer to output structure

        Returns:
            H264DEC_OK            no pictures available for display
            H264DEC_PIC_RDY       picture available for display
            H264DEC_PARAM_ERROR     invalid parameters
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet

------------------------------------------------------------------------------*/
H264DecRet H264DecNextPicture(H264DecInst dec_inst,
                              H264DecPicture * output, u32 end_of_stream) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  const dpbOutPicture_t *out_pic = NULL;
  dpbStorage_t *out_dpb;

  DEC_API_TRC("H264DecNextPicture#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("H264DecNextPicture# ERROR: dec_inst or output is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecNextPicture# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

#ifdef USE_OUTPUT_RELEASE
  if(dec_cont->pp.pp_instance == NULL) {
    u32 ret = H264DEC_OK;

    if(dec_cont->dec_stat == H264DEC_END_OF_STREAM &&
        IsOutputEmpty(&dec_cont->fb_list)) {
      DEC_API_TRC("H264DecNextPicture# H264DEC_END_OF_STREAM\n");
      return (H264DEC_END_OF_STREAM);
    }

    if((ret = PeekOutputPic(&dec_cont->fb_list, output))) {
      if(ret == ABORT_MARKER) {
        DEC_API_TRC("H264DecNextPicture# H264DEC_ABORTED\n");
        return (H264DEC_ABORTED);
      }
      if(ret == FLUSH_MARKER) {
        DEC_API_TRC("H264DecNextPicture# H264DEC_FLUSHED\n");
        return (H264DEC_FLUSHED);
      }
      DEC_API_TRC("H264DecNextPicture# H264DEC_PIC_RDY\n");
      return (H264DEC_PIC_RDY);
    } else {
      DEC_API_TRC("H264DecNextPicture# H264DEC_OK\n");
      return (H264DEC_OK);
    }
  }
#endif

  if(end_of_stream) {
    if(dec_cont->asic_running) {
      /* stop HW */
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT, 0);
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ, 0);
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_E, 0);
      DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                   dec_cont->h264_regs[1] | DEC_IRQ_DISABLE);

      /* Wait for PP to end also */
      if(dec_cont->pp.pp_instance != NULL &&
          (dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING ||
           dec_cont->pp.dec_pp_if.pp_status == DECPP_PIC_NOT_FINISHED)) {
        dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

        TRACE_PP_CTRL("H264RunAsic: PP Wait for end\n");

        dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

        TRACE_PP_CTRL("H264RunAsic: PP Finished\n");
      }

      DWLReleaseHw(dec_cont->dwl, dec_cont->core_id); /* release HW lock */

      /* Decrement usage for DPB buffers */
      DecrementDPBRefCount(&dec_cont->storage.dpb[1]);

      dec_cont->asic_running = 0;
      dec_cont->dec_stat = H264DEC_INITIALIZED;
      h264InitPicFreezeOutput(dec_cont, 1);
    } else if (dec_cont->keep_hw_reserved) {
      DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);
      dec_cont->keep_hw_reserved = 0;
    }
    /* only one field of last frame decoded, PP still running (wait after
     * second field decoded) -> wait here */
    if (dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.dec_pp_if.pp_status == DECPP_PIC_NOT_FINISHED) {
      dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;
      dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);
    }

    h264bsdFlushBuffer(&dec_cont->storage);

    FinalizeOutputAll(&dec_cont->fb_list);
  }

  /* pp and decoder running in parallel, decoder finished first field ->
   * decode second field and wait PP after that */
  if (dec_cont->pp.pp_instance != NULL &&
      dec_cont->pp.dec_pp_if.pp_status == DECPP_PIC_NOT_FINISHED)
    return (H264DEC_OK);

  out_dpb = dec_cont->storage.dpbs[dec_cont->storage.out_view];

  /* if display order is the same as decoding order and PP is used and
   * cannot be used in pipeline (rotation) -> do not perform PP here but
   * while decoding next picture (parallel processing instead of
   * DEC followed by PP followed by DEC...) */
  if (dec_cont->storage.pending_out_pic) {
    out_pic = dec_cont->storage.pending_out_pic;
    dec_cont->storage.pending_out_pic = NULL;
  } else if(out_dpb->no_reordering == 0) {
    if(!out_dpb->delayed_out) {
      if (dec_cont->pp.pp_instance && dec_cont->pp.dec_pp_if.pp_status ==
          DECPP_PIC_READY)
        out_dpb->no_output = 0;

      dec_cont->storage.dpb =
        dec_cont->storage.dpbs[dec_cont->storage.out_view];

      out_pic = h264bsdNextOutputPicture(&dec_cont->storage);

      if ( (dec_cont->storage.num_views ||
            dec_cont->storage.out_view) && out_pic != NULL) {
        output->view_id =
          dec_cont->storage.view_id[dec_cont->storage.out_view];
        dec_cont->storage.out_view ^= 0x1;
      }
    }
  } else {
    /* no reordering of output pics AND stereo was activated after base
     * picture was output -> output stereo view pic if available */
    if (dec_cont->storage.num_views &&
        dec_cont->storage.view && dec_cont->storage.out_view == 0 &&
        out_dpb->num_out == 0 &&
        dec_cont->storage.dpbs[dec_cont->storage.view]->num_out > 0) {
      dec_cont->storage.out_view ^= 0x1;
      out_dpb = dec_cont->storage.dpbs[dec_cont->storage.out_view];
    }

    if(out_dpb->num_out > 1 || end_of_stream ||
        dec_cont->storage.prev_nal_unit->nal_ref_idc == 0 ||
        dec_cont->pp.pp_instance == NULL || dec_cont->pp.dec_pp_if.use_pipeline ||
        dec_cont->storage.view != dec_cont->storage.out_view) {
      if(!end_of_stream &&
          ((out_dpb->num_out == 1 && out_dpb->delayed_out) ||
           (dec_cont->storage.slice_header->field_pic_flag &&
            dec_cont->storage.second_field))) {
      } else {
        dec_cont->storage.dpb =
          dec_cont->storage.dpbs[dec_cont->storage.out_view];
        out_pic = h264bsdNextOutputPicture(&dec_cont->storage);
        output->view_id =
          dec_cont->storage.view_id[dec_cont->storage.out_view];
        if ( (dec_cont->storage.num_views ||
              dec_cont->storage.out_view) && out_pic != NULL)
          dec_cont->storage.out_view ^= 0x1;
      }
    }
  }

  if(out_pic != NULL) {
    if (!dec_cont->storage.num_views)
      output->view_id = 0;

    output->output_picture = out_pic->data->virtual_address;
    output->output_picture_bus_address = out_pic->data->bus_address;
    output->pic_id = out_pic->pic_id;
    output->pic_coding_type[0] = out_pic->pic_code_type[0];
    output->pic_coding_type[1] = out_pic->pic_code_type[1];
    output->is_idr_picture[0] = out_pic->is_idr[0];
    output->is_idr_picture[1] = out_pic->is_idr[1];
    output->decode_id[0] = out_pic->decode_id[0];
    output->decode_id[1] = out_pic->decode_id[1];
    output->nbr_of_err_mbs = out_pic->num_err_mbs;

    output->interlaced = out_pic->interlaced;
    output->field_picture = out_pic->field_picture;
    output->top_field = out_pic->top_field;

    output->pic_width = out_pic->pic_width;
    output->pic_height = out_pic->pic_height;

    output->output_format = out_pic->tiled_mode ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;
    output->pic_struct = out_pic->pic_struct;

    /* Pending flush caused by new SPS. Decoded output pictures in DPB */
    /* may have different dimensions than defined in new headers. */
    /* Use dimensions from previous picture */
    if (end_of_stream && (dec_cont->dec_stat == H264DEC_NEW_HEADERS) &&
        (dec_cont->pp.pp_instance != NULL)) {
      output->pic_width = dec_cont->storage.prev_pic_width;
      output->pic_height = dec_cont->storage.prev_pic_height;
    }

    dec_cont->storage.prev_pic_width = output->pic_width;
    dec_cont->storage.prev_pic_height = output->pic_height;

    output->crop_params = out_pic->crop;

    if(dec_cont->pp.pp_instance != NULL && (dec_cont->pp.pp_info.multi_buffer == 0)) {
      /* single buffer legacy mode */
      DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;

      if(dec_pp_if->pp_status == DECPP_PIC_READY) {
        dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;
        TRACE_PP_CTRL
        ("H264DecNextPicture: PP already ran for this picture\n");
      } else {
        TRACE_PP_CTRL("H264DecNextPicture: PP has to run\n");

        dec_pp_if->use_pipeline = 0;   /* we are in standalone mode */

        dec_pp_if->inwidth = output->pic_width;
        dec_pp_if->inheight = output->pic_height;
        dec_pp_if->cropped_w = output->pic_width;
        dec_pp_if->cropped_h = output->pic_height;
        dec_pp_if->tiled_input_mode =
          (output->output_format == DEC_OUT_FRM_RASTER_SCAN) ? 0 : 1;
        dec_pp_if->progressive_sequence =
          dec_cont->storage.active_sps->frame_mbs_only_flag;

        if(output->interlaced == 0) {
          dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
        } else {
          u32 pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
          if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD )
            pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD;

          if(output->field_picture == 0) {
            dec_pp_if->pic_struct = pic_struct;
          } else {
            /* TODO: missing field, is this OK? */
            dec_pp_if->pic_struct = pic_struct;
          }
        }

        dec_pp_if->input_bus_luma = output->output_picture_bus_address;
        dec_pp_if->input_bus_chroma = dec_pp_if->input_bus_luma +
                                      output->pic_width * output->pic_height;

        if(dec_pp_if->pic_struct != DECPP_PIC_FRAME_OR_TOP_FIELD) {
          if( dec_cont->dpb_mode == DEC_DPB_FRAME ) {
            dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                         dec_pp_if->inwidth;
            dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                           dec_pp_if->inwidth;
          }
          if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
            dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                         (dec_pp_if->inwidth*dec_pp_if->inheight >> 1);
            dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                           (dec_pp_if->inwidth*dec_pp_if->inheight >> 2);
          }
        } else {
          dec_pp_if->bottom_bus_luma = (u32)(-1);
          dec_pp_if->bottom_bus_chroma = (u32)(-1);
        }

        dec_pp_if->little_endian =
          GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUT_ENDIAN);
        dec_pp_if->word_swap =
          GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUTSWAP32_E);

        dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);

        TRACE_PP_CTRL("H264DecNextPicture: PP wait to be done\n");

        dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

        TRACE_PP_CTRL("H264DecNextPicture: PP Finished\n");

      }
    }

    if(dec_cont->pp.pp_instance != NULL && (dec_cont->pp.pp_info.multi_buffer != 0)) {
      /* multibuffer mode */
      DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;
      u32 id;

      if(dec_pp_if->pp_status == DECPP_PIC_READY) {
        dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;
        TRACE_PP_CTRL("H264DecNextPicture: PP processed a picture\n");
      }

      id = h264PpMultiFindPic(dec_cont, out_pic->data);
      if(id <= dec_cont->pp.multi_max_id) {
        TRACE_PP_CTRL("H264DecNextPicture: PPNextDisplayId = %d\n", id);
        TRACE_PP_CTRL("H264DecNextPicture: PP already ran for this picture\n");
        dec_cont->pp.PPNextDisplayId(dec_cont->pp.pp_instance, id);
        h264PpMultiRemovePic(dec_cont, out_pic->data);
      } else {
        if (!end_of_stream && GetFreeBufferCount(&dec_cont->fb_list) &&
            dec_cont->pp.queued_pic_to_pp[dec_cont->storage.view] ==
            out_pic->data &&
            dec_cont->dec_stat != H264DEC_NEW_HEADERS &&
            !dec_cont->pp.pp_info.pipeline_accepted) {
          dec_cont->storage.pending_out_pic = out_pic;
          return H264DEC_OK;
        }
        TRACE_PP_CTRL("H264DecNextPicture: PP has to run\n");

        id = h264PpMultiAddPic(dec_cont, out_pic->data);

        ASSERT(id <= dec_cont->pp.multi_max_id);

        TRACE_PP_CTRL("H264RunAsic: PP Multibuffer index = %d\n", id);
        TRACE_PP_CTRL("H264DecNextPicture: PPNextDisplayId = %d\n", id);
        dec_pp_if->buffer_index = id;
        dec_pp_if->display_index = id;
        h264PpMultiRemovePic(dec_cont, out_pic->data);

        if(dec_cont->pp.queued_pic_to_pp[dec_cont->storage.view] == out_pic->data) {
          dec_cont->pp.queued_pic_to_pp[dec_cont->storage.view] = NULL; /* remove it from queue */
        }

        dec_pp_if->use_pipeline = 0;   /* we are in standalone mode */

        dec_pp_if->inwidth = output->pic_width;
        dec_pp_if->inheight = output->pic_height;
        dec_pp_if->cropped_w = output->pic_width;
        dec_pp_if->cropped_h = output->pic_height;
        dec_pp_if->tiled_input_mode =
          (output->output_format == DEC_OUT_FRM_RASTER_SCAN) ? 0 : 1;
        dec_pp_if->progressive_sequence =
          dec_cont->storage.active_sps->frame_mbs_only_flag;

        if(output->interlaced == 0) {
          dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
        } else {
          u32 pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
          if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD )
            pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD;

          if(output->field_picture == 0) {
            dec_pp_if->pic_struct = pic_struct;
          } else {
            /* TODO: missing field, is this OK? */
            dec_pp_if->pic_struct = pic_struct;
          }
        }

        dec_pp_if->input_bus_luma = output->output_picture_bus_address;
        dec_pp_if->input_bus_chroma = dec_pp_if->input_bus_luma +
                                      output->pic_width * output->pic_height;

        if(dec_pp_if->pic_struct != DECPP_PIC_FRAME_OR_TOP_FIELD) {
          if( dec_cont->dpb_mode == DEC_DPB_FRAME ) {
            dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                         dec_pp_if->inwidth;
            dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                           dec_pp_if->inwidth;
          }
          if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
            dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                         (dec_pp_if->inwidth*dec_pp_if->inheight >> 1);
            dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                           (dec_pp_if->inwidth*dec_pp_if->inheight >> 2);
          }
        } else {
          dec_pp_if->bottom_bus_luma = (u32)(-1);
          dec_pp_if->bottom_bus_chroma = (u32)(-1);
        }

        dec_pp_if->little_endian =
          GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUT_ENDIAN);
        dec_pp_if->word_swap =
          GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUTSWAP32_E);

        dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);

        TRACE_PP_CTRL("H264DecNextPicture: PP wait to be done\n");

        dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

        TRACE_PP_CTRL("H264DecNextPicture: PP Finished\n");
      }
    }

    if (dec_cont->pp.pp_info.deinterlace) {
      output->interlaced = 0;
      output->field_picture = 0;
      output->top_field = 0;
    }

    ClearOutput(&dec_cont->fb_list, out_pic->mem_idx);

    DEC_API_TRC("H264DecNextPicture# H264DEC_PIC_RDY\n");
    return (H264DEC_PIC_RDY);
  } else {
    DEC_API_TRC("H264DecNextPicture# H264DEC_OK\n");
    return (H264DEC_OK);
  }

}

/*------------------------------------------------------------------------------
    Function name : h264UpdateAfterPictureDecode
    Description   :

    Return type   : void
    Argument      : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
void h264UpdateAfterPictureDecode(decContainer_t * dec_cont) {

  u32 tmp_ret = HANTRO_OK;
  storage_t *storage = &dec_cont->storage;
  sliceHeader_t *slice_header = storage->slice_header;
  u32 interlaced;
  u32 pic_code_type;
  u32 second_field = 0;
  i32 poc;

  h264bsdResetStorage(storage);

  ASSERT((storage));

  /* determine initial reference picture lists */
  H264InitRefPicList(dec_cont);

  if(storage->slice_header->field_pic_flag == 0)
    storage->curr_image->pic_struct = FRAME;
  else
    storage->curr_image->pic_struct = storage->slice_header->bottom_field_flag;

  if (storage->curr_image->pic_struct < FRAME) {
    if (storage->dpb->current_out->status[(u32)!storage->curr_image->pic_struct] != EMPTY)
      second_field = 1;
  }

  h264GetSarInfo(storage, &storage->curr_image->sar_width, &storage->curr_image->sar_height);

  if(storage->poc->contains_mmco5) {
    u32 tmp;

    tmp = MIN(storage->poc->pic_order_cnt[0], storage->poc->pic_order_cnt[1]);
    storage->poc->pic_order_cnt[0] -= tmp;
    storage->poc->pic_order_cnt[1] -= tmp;
  }

  storage->current_marked = storage->valid_slice_in_access_unit;

  /* Setup tiled mode for picture before updating DPB */
  interlaced = !dec_cont->storage.active_sps->frame_mbs_only_flag;
  if( dec_cont->tiled_mode_support) {
    dec_cont->tiled_reference_enable =
      DecSetupTiledReference( dec_cont->h264_regs,
                              dec_cont->tiled_mode_support,
                              dec_cont->dpb_mode,
                              interlaced );
  } else {
    dec_cont->tiled_reference_enable = 0;
  }

  if(storage->valid_slice_in_access_unit) {
    if(IS_I_SLICE(slice_header->slice_type))
      pic_code_type = DEC_PIC_TYPE_I;
    else if(IS_P_SLICE(slice_header->slice_type))
      pic_code_type = DEC_PIC_TYPE_P;
    else
      pic_code_type = DEC_PIC_TYPE_B;

#ifdef SKIP_OPENB_FRAME
    if (!dec_cont->first_entry_point) {
      if(storage->curr_image->pic_struct < FRAME) {
        if (!second_field)
          dec_cont->entry_is_IDR = IS_IDR_NAL_UNIT(storage->prev_nal_unit);
        else {
          dec_cont->entry_POC = MIN(storage->poc->pic_order_cnt[0],
                                   storage->poc->pic_order_cnt[1]);
          dec_cont->first_entry_point = 1;
        }
      } else {
        dec_cont->entry_is_IDR = IS_IDR_NAL_UNIT(storage->prev_nal_unit);
        dec_cont->entry_POC = MIN(storage->poc->pic_order_cnt[0],
                                 storage->poc->pic_order_cnt[1]);
        dec_cont->first_entry_point = 1;
      }
    }
    if (dec_cont->skip_b < 2) {
      if (storage->curr_image->pic_struct < FRAME) {
        if(second_field) {
          if((pic_code_type == DEC_PIC_TYPE_I) || (pic_code_type == DEC_PIC_TYPE_P) ||
             (storage->dpb->current_out->pic_code_type[(u32)!storage->curr_image->pic_struct] == DEC_PIC_TYPE_I) ||
             (storage->dpb->current_out->pic_code_type[(u32)!storage->curr_image->pic_struct] == DEC_PIC_TYPE_P))
            dec_cont->skip_b++;
          else {
            poc = MIN(storage->poc->pic_order_cnt[0],
                      storage->poc->pic_order_cnt[1]);
            if(!dec_cont->entry_is_IDR && (poc < dec_cont->entry_POC))
              storage->dpb->current_out->openB_flag = 1;
          }
        }
      } else {
        if((pic_code_type == DEC_PIC_TYPE_I) || (pic_code_type == DEC_PIC_TYPE_P))
          dec_cont->skip_b++;
        else {
          poc = MIN(storage->poc->pic_order_cnt[0],
                    storage->poc->pic_order_cnt[1]);
          if(!dec_cont->entry_is_IDR && (poc < dec_cont->entry_POC))
            storage->dpb->current_out->openB_flag = 1;
        }
      }
    }
#endif

    if(storage->prev_nal_unit->nal_ref_idc) {
      tmp_ret = h264bsdMarkDecRefPic(storage->dpb,
                                     &slice_header->dec_ref_pic_marking,
                                     storage->curr_image,
                                     slice_header->frame_num,
                                     storage->poc->pic_order_cnt,
                                     IS_IDR_NAL_UNIT(storage->prev_nal_unit) ?
                                     HANTRO_TRUE : HANTRO_FALSE,
                                     storage->current_pic_id,
                                     storage->num_concealed_mbs,
                                     dec_cont->tiled_reference_enable,
                                     pic_code_type);
    } else {
      /* non-reference picture, just store for possible display
       * reordering */
      tmp_ret = h264bsdMarkDecRefPic(storage->dpb, NULL,
                                     storage->curr_image,
                                     slice_header->frame_num,
                                     storage->poc->pic_order_cnt,
                                     HANTRO_FALSE,
                                     storage->current_pic_id,
                                     storage->num_concealed_mbs,
                                     dec_cont->tiled_reference_enable,
                                     pic_code_type);
    }

    if (tmp_ret != HANTRO_OK && storage->view == 0)
      storage->second_field = 0;

    if(storage->dpb->delayed_out == 0) {
      h264DpbUpdateOutputList(storage->dpb);

      if (storage->view == 0)
        storage->last_base_num_out = storage->dpb->num_out;
      /* make sure that there are equal number of output pics available
       * for both views */
      else if (storage->dpb->num_out < storage->last_base_num_out)
        h264DpbAdjStereoOutput(storage->dpb, storage->last_base_num_out);
      else if (storage->last_base_num_out &&
               storage->last_base_num_out + 1 < storage->dpb->num_out)
        h264DpbAdjStereoOutput(storage->dpbs[0],
                               storage->dpb->num_out - 1);
      else if (storage->last_base_num_out == 0 && storage->dpb->num_out)
        h264DpbAdjStereoOutput(storage->dpbs[0],
                               storage->dpb->num_out);

      /* check if current_out already in output buffer and second
       * field to come -> delay output */
      if(storage->curr_image->pic_struct != FRAME &&
          (storage->view == 0 ? storage->second_field :
           !storage->base_opposite_field_pic)) {
        u32 i, tmp;

        tmp = storage->dpb->out_index_r;
        for (i = 0; i < storage->dpb->num_out; i++, tmp++) {
          if (tmp == storage->dpb->dpb_size + 1)
            tmp = 0;

          if(storage->dpb->current_out->data ==
              storage->dpb->out_buf[tmp].data) {
            storage->dpb->delayed_id = tmp;
            DEBUG_PRINT(
              ("h264UpdateAfterPictureDecode: Current frame in output list; "));
            storage->dpb->delayed_out = 1;
            break;
          }
        }
      }
    } else {
      if (!storage->dpb->no_reordering)
        h264DpbUpdateOutputList(storage->dpb);
      DEBUG_PRINT(
        ("h264UpdateAfterPictureDecode: Output all delayed pictures!\n"));
      storage->dpb->delayed_out = 0;
      storage->dpb->current_out->to_be_displayed = 0;   /* remove it from output list */
    }

  } else {
    storage->dpb->delayed_out = 0;
    storage->second_field = 0;
  }

  if ((storage->valid_slice_in_access_unit && tmp_ret == HANTRO_OK) ||
      storage->view)
    storage->next_view ^= 0x1;
  storage->pic_started = HANTRO_FALSE;
  storage->valid_slice_in_access_unit = HANTRO_FALSE;
  storage->aso_detected = 0;
}

/*------------------------------------------------------------------------------
    Function name : h264SpsSupported
    Description   :

    Return type   : u32
    Argument      : const decContainer_t * dec_cont
------------------------------------------------------------------------------*/
u32 h264SpsSupported(const decContainer_t * dec_cont) {
  const seqParamSet_t *sps = dec_cont->storage.active_sps;

  if(sps == NULL)
    return 0;

  /* check picture size */
  if(sps->pic_width_in_mbs * 16 > dec_cont->max_dec_pic_width ||
      sps->pic_width_in_mbs < 3 || sps->pic_height_in_mbs < 3 ||
      (sps->pic_width_in_mbs * sps->pic_height_in_mbs) > ((4096 >> 4) * (4096 >> 4))) {
    DEBUG_PRINT(("Picture size not supported!\n"));
    return 0;
  }

  /* If tile mode is enabled, should take DTRC minimum size(96x48) into consideration */
  if(dec_cont->tiled_mode_support) {
    if (sps->pic_width_in_mbs < 6) {
      DEBUG_PRINT(("Picture size not supported!\n"));
      return 0;
    }
  }

  if(dec_cont->h264_profile_support == H264_BASELINE_PROFILE) {
    if(sps->frame_mbs_only_flag != 1) {
      DEBUG_PRINT(("INTERLACED!!! Not supported in baseline decoder\n"));
      return 0;
    }
    if(sps->chroma_format_idc != 1) {
      DEBUG_PRINT(("CHROMA!!! Only 4:2:0 supported in baseline decoder\n"));
      return 0;
    }
    if(sps->scaling_matrix_present_flag != 0) {
      DEBUG_PRINT(("SCALING Matrix!!! Not supported in baseline decoder\n"));
      return 0;
    }
  }

  return 1;
}

/*------------------------------------------------------------------------------
    Function name : h264PpsSupported
    Description   :

    Return type   : u32
    Argument      : const decContainer_t * dec_cont
------------------------------------------------------------------------------*/
u32 h264PpsSupported(const decContainer_t * dec_cont) {
  const picParamSet_t *pps = dec_cont->storage.active_pps;

  if(dec_cont->h264_profile_support == H264_BASELINE_PROFILE) {
    if(pps->entropy_coding_mode_flag != 0) {
      DEBUG_PRINT(("CABAC!!! Not supported in baseline decoder\n"));
      return 0;
    }
    if(pps->weighted_pred_flag != 0 || pps->weighted_bi_pred_idc != 0) {
      DEBUG_PRINT(("WEIGHTED Pred!!! Not supported in baseline decoder\n"));
      return 0;
    }
    if(pps->transform8x8_flag != 0) {
      DEBUG_PRINT(("TRANSFORM 8x8!!! Not supported in baseline decoder\n"));
      return 0;
    }
    if(pps->scaling_matrix_present_flag != 0) {
      DEBUG_PRINT(("SCALING Matrix!!! Not supported in baseline decoder\n"));
      return 0;
    }
  }
  return 1;
}

/*------------------------------------------------------------------------------
    Function name   : h264StreamIsBaseline
    Description     :
    Return type     : u32
    Argument        : const decContainer_t * dec_cont
------------------------------------------------------------------------------*/
u32 h264StreamIsBaseline(const decContainer_t * dec_cont) {
  const picParamSet_t *pps = dec_cont->storage.active_pps;
  const seqParamSet_t *sps = dec_cont->storage.active_sps;

  if(sps->frame_mbs_only_flag != 1) {
    return 0;
  }
  if(sps->chroma_format_idc != 1) {
    return 0;
  }
  if(sps->scaling_matrix_present_flag != 0) {
    return 0;
  }
  if(pps->entropy_coding_mode_flag != 0) {
    return 0;
  }
  if(pps->weighted_pred_flag != 0 || pps->weighted_bi_pred_idc != 0) {
    return 0;
  }
  if(pps->transform8x8_flag != 0) {
    return 0;
  }
  if(pps->scaling_matrix_present_flag != 0) {
    return 0;
  }
  return 1;
}

/*------------------------------------------------------------------------------
    Function name : h264AllocateResources
    Description   :

    Return type   : u32
    Argument      : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
u32 h264AllocateResources(decContainer_t * dec_cont) {
  u32 ret, mbs_in_pic;
  DecAsicBuffers_t *asic = dec_cont->asic_buff;
  storage_t *storage = &dec_cont->storage;

  const seqParamSet_t *sps = storage->active_sps;

  SetDecRegister(dec_cont->h264_regs, HWIF_PIC_MB_WIDTH, sps->pic_width_in_mbs);
  SetDecRegister(dec_cont->h264_regs, HWIF_PIC_MB_HEIGHT_P,
                 sps->pic_height_in_mbs);
  SetDecRegister(dec_cont->h264_regs, HWIF_AVS_H264_H_EXT,
                 sps->pic_height_in_mbs >> 8);

  ReleaseAsicBuffers(dec_cont->dwl, asic);

  ret = AllocateAsicBuffers(dec_cont, asic, storage->pic_size_in_mbs);
  if(ret == 0) {
    SET_ADDR_REG(dec_cont->h264_regs, HWIF_INTRA_4X4_BASE,
                 asic->intra_pred.bus_address);
    SET_ADDR_REG(dec_cont->h264_regs, HWIF_DIFF_MV_BASE,
                 asic->mv.bus_address);

    if(dec_cont->rlc_mode) {
      /* release any previously allocated stuff */
      FREE(storage->mb);
      FREE(storage->slice_group_map);

      mbs_in_pic = sps->pic_width_in_mbs * sps->pic_height_in_mbs;

      DEBUG_PRINT(("ALLOCATE storage->mb            - %8d bytes\n",
                   mbs_in_pic * sizeof(mbStorage_t)));
      storage->mb = DWLcalloc(mbs_in_pic, sizeof(mbStorage_t));

      DEBUG_PRINT(("ALLOCATE storage->slice_group_map - %8d bytes\n",
                   mbs_in_pic * sizeof(u32)));

      ALLOCATE(storage->slice_group_map, mbs_in_pic, u32);

      if(storage->mb == NULL || storage->slice_group_map == NULL) {
        ret = MEMORY_ALLOCATION_ERROR;
      } else {
        h264bsdInitMbNeighbours(storage->mb, sps->pic_width_in_mbs,
                                storage->pic_size_in_mbs);
      }
    } else {
      storage->mb = NULL;
      storage->slice_group_map = NULL;
    }
  }

  return ret;
}

/*------------------------------------------------------------------------------
    Function name : h264InitPicFreezeOutput
    Description   :

    Return type   : u32
    Argument      : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
void h264InitPicFreezeOutput(decContainer_t * dec_cont, u32 from_old_dpb) {

  storage_t *storage = &dec_cont->storage;

  /* for concealment after a HW error report we use the saved reference list */
  dpbStorage_t *dpb = &storage->dpb[from_old_dpb];

  /* update status of decoded image (relevant only for  multi-Core) */
  /* current out is always in dpb[0] */
  {
    dpbPicture_t *current_out = storage->dpb->current_out;

    u8 *p_sync_mem = (u8*)current_out->data->virtual_address +
                     dpb->sync_mc_offset;
    h264MCSetRefPicStatus(p_sync_mem, current_out->is_field_pic,
                          current_out->is_bottom_field);
  }

#ifndef _DISABLE_PIC_FREEZE
  u32 index = 0;
  const u8 *ref_data;
  do {
    ref_data = h264bsdGetRefPicDataVlcMode(dpb, dpb->list[index], 0);
    index++;
  } while(index < 16 && ref_data == NULL);
#endif

  /* "freeze" whole picture if not field pic or if opposite field of the
   * field pic does not exist in the buffer */
  if(dec_cont->storage.slice_header->field_pic_flag == 0 ||
      storage->dpb[0].current_out->status[
        !dec_cont->storage.slice_header->bottom_field_flag] == EMPTY) {
    storage->dpb[0].current_out->corrupted_first_field_or_frame = 1;
#ifndef _DISABLE_PIC_FREEZE
    /* reset DMV storage for erroneous pictures */
    if(dec_cont->asic_buff->enable_dmv_and_poc != 0) {
      const u32 dvm_mem_size = storage->pic_size_in_mbs * 64;
      void * dvm_base = (u8*)storage->curr_image->data->virtual_address +
                        dec_cont->storage.dpb->dir_mv_offset;

      (void) DWLmemset(dvm_base, 0, dvm_mem_size);
    }

    if(ref_data == NULL) {
      DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memset\n"));
      (void) DWLmemset(storage->curr_image->data->
                       virtual_address, 128,
                       dec_cont->storage.active_sps->mono_chrome ?
                       256 * storage->pic_size_in_mbs :
                       384 * storage->pic_size_in_mbs);
      if (storage->enable2nd_chroma &&
          !storage->active_sps->mono_chrome)
        (void) DWLmemset((u8*)storage->curr_image->data->virtual_address +
                         dpb->ch2_offset, 128,
                         128 * storage->pic_size_in_mbs);
    } else {
      DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memcopy\n"));
      (void) DWLmemcpy(storage->curr_image->data->virtual_address,
                       ref_data,
                       dec_cont->storage.active_sps->mono_chrome ?
                       256 * storage->pic_size_in_mbs :
                       384 * storage->pic_size_in_mbs);
      if (storage->enable2nd_chroma &&
          !storage->active_sps->mono_chrome)
        (void) DWLmemcpy((u8*)storage->curr_image->data->virtual_address +
                         dpb->ch2_offset, ref_data + dpb->ch2_offset,
                         128 * storage->pic_size_in_mbs);
    }
#endif
  } else {
    if (!storage->dpb[0].current_out->corrupted_first_field_or_frame) {
      storage->dpb[0].current_out->corrupted_second_field = 1;
      if (dec_cont->storage.slice_header->bottom_field_flag != 0)
        storage->dpb[0].current_out->pic_struct = TOPFIELD;
      else
        storage->dpb[0].current_out->pic_struct = BOTFIELD;
    }

#ifndef _DISABLE_PIC_FREEZE
    u32 i;
    u32 field_offset = storage->active_sps->pic_width_in_mbs * 16;
    u8 *lum_base = (u8*)storage->curr_image->data->virtual_address;
    u8 *ch_base = (u8*)storage->curr_image->data->virtual_address + storage->pic_size_in_mbs * 256;
    u8 *ch2_base = (u8*)storage->curr_image->data->virtual_address + dpb->ch2_offset;
    const u8 *ref_ch_data = ref_data + storage->pic_size_in_mbs * 256;
    const u8 *ref_ch2_data = ref_data + dpb->ch2_offset;

    /*Base for second field*/
    u8 *lum_base1 = lum_base;
    u8 *ch_base1 = ch_base;
    u8 *ch2_base1 = ch2_base;

    if (dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD) {
      if (dec_cont->storage.slice_header->bottom_field_flag != 0) {
        lum_base1 = lum_base + 256 * storage->pic_size_in_mbs / 2;
        ch_base1 = ch_base + 128 * storage->pic_size_in_mbs / 2;
        ch2_base1 = ch2_base + 128 * storage->pic_size_in_mbs / 2;
      }
      else {
        lum_base = lum_base + 256 * storage->pic_size_in_mbs / 2;
        ch_base = ch_base + 128 * storage->pic_size_in_mbs / 2;
        ch2_base = ch2_base + 128 * storage->pic_size_in_mbs / 2;
      }

      if (ref_data == NULL) {
        (void) DWLmemcpy(lum_base1, lum_base,
                         256 * storage->pic_size_in_mbs / 2);
        (void) DWLmemcpy(ch_base1, ch_base,
                         128 * storage->pic_size_in_mbs / 2);
         if (storage->enable2nd_chroma &&
            !storage->active_sps->mono_chrome)
          (void) DWLmemcpy(ch2_base, ch2_base1,
                           128 * storage->pic_size_in_mbs);
      }
      else {
        if (dec_cont->storage.slice_header->bottom_field_flag != 0) {
          ref_data = ref_data + 256 * storage->pic_size_in_mbs / 2;
          ref_ch_data = ref_ch_data + 128 * storage->pic_size_in_mbs / 2;
          ref_ch2_data = ref_ch2_data + 128 * storage->pic_size_in_mbs / 2;
        }
        (void) DWLmemcpy(lum_base1, ref_data,
                         256 * storage->pic_size_in_mbs / 2);
        (void) DWLmemcpy(ch_base1, ref_ch_data,
                         128 * storage->pic_size_in_mbs / 2);

        if (storage->enable2nd_chroma &&
            !storage->active_sps->mono_chrome)
          (void) DWLmemcpy(ch2_base1, ref_ch2_data,
                           128 * storage->pic_size_in_mbs);
      }
    }
    else {
    if(dec_cont->storage.slice_header->bottom_field_flag != 0) {
      lum_base += field_offset;
      ch_base += field_offset;
      ch2_base += field_offset;
    }

    if(ref_data == NULL) {
      DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memset, one field\n"));

      for(i = 0; i < (storage->active_sps->pic_height_in_mbs*8); i++) {
        (void) DWLmemset(lum_base, 128, field_offset);
        if((dec_cont->storage.active_sps->mono_chrome == 0) && (i & 0x1U)) {
          (void) DWLmemset(ch_base, 128, field_offset);
          ch_base += 2*field_offset;
          if (storage->enable2nd_chroma) {
            (void) DWLmemset(ch2_base, 128, field_offset);
            ch2_base += 2*field_offset;
          }
        }
        lum_base += 2*field_offset;
      }
    } else {
      if(dec_cont->storage.slice_header->bottom_field_flag != 0) {
        ref_data += field_offset;
        ref_ch_data += field_offset;
        ref_ch2_data += field_offset;
      }

      DEBUG_PRINT(("h264InitPicFreezeOutput: pic freeze memcopy, one field\n"));
      for(i = 0; i < (storage->active_sps->pic_height_in_mbs*8); i++) {
        (void) DWLmemcpy(lum_base, ref_data, field_offset);
        if((dec_cont->storage.active_sps->mono_chrome == 0) && (i & 0x1U)) {
          (void) DWLmemcpy(ch_base, ref_ch_data, field_offset);
          ch_base += 2*field_offset;
          ref_ch_data += 2*field_offset;
          if (storage->enable2nd_chroma) {
            (void) DWLmemcpy(ch2_base, ref_ch2_data, field_offset);
            ch2_base += 2*field_offset;
            ref_ch2_data += 2*field_offset;
          }
        }
        lum_base += 2*field_offset;
        ref_data += 2*field_offset;
      }
    }
    }
#endif
  }

  dpb = &storage->dpb[0]; /* update results for current output */

  {
    i32 i = dpb->num_out;
    u32 tmp = dpb->out_index_r;

    while((i--) > 0) {
      if (tmp == dpb->dpb_size + 1)
        tmp = 0;

      if(dpb->out_buf[tmp].data == storage->curr_image->data) {
        dpb->out_buf[tmp].num_err_mbs = storage->pic_size_in_mbs;
        break;
      }
      tmp++;
    }

    i = dpb->dpb_size + 1;

    while((i--) > 0) {
      if(dpb->buffer[i].data == storage->curr_image->data) {
        dpb->buffer[i].num_err_mbs = storage->pic_size_in_mbs;
        ASSERT(&dpb->buffer[i] == dpb->current_out);
        break;
      }
    }
  }

  dec_cont->storage.num_concealed_mbs = storage->pic_size_in_mbs;

}

/*------------------------------------------------------------------------------
    Function name : bsdDecodeReturn
    Description   :

    Return type   : void
    Argument      : bsd decoder return value
------------------------------------------------------------------------------*/
static void bsdDecodeReturn(u32 retval) {

  DEBUG_PRINT(("H264bsdDecode returned: "));
  switch (retval) {
  case H264BSD_PIC_RDY:
    DEBUG_PRINT(("H264BSD_PIC_RDY\n"));
    break;
  case H264BSD_RDY:
    DEBUG_PRINT(("H264BSD_RDY\n"));
    break;
  case H264BSD_HDRS_RDY:
    DEBUG_PRINT(("H264BSD_HDRS_RDY\n"));
    break;
  case H264BSD_ERROR:
    DEBUG_PRINT(("H264BSD_ERROR\n"));
    break;
  case H264BSD_PARAM_SET_ERROR:
    DEBUG_PRINT(("H264BSD_PARAM_SET_ERROR\n"));
    break;
  case H264BSD_NEW_ACCESS_UNIT:
    DEBUG_PRINT(("H264BSD_NEW_ACCESS_UNIT\n"));
    break;
  case H264BSD_FMO:
    DEBUG_PRINT(("H264BSD_FMO\n"));
    break;
  default:
    DEBUG_PRINT(("UNKNOWN\n"));
    break;
  }
}

/*------------------------------------------------------------------------------
    Function name   : h264GetSarInfo
    Description     : Returns the sample aspect ratio size info
    Return type     : void
    Argument        : storage_t *storage - decoder storage
    Argument        : u32 * sar_width - SAR width returned here
    Argument        : u32 *sar_height - SAR height returned here
------------------------------------------------------------------------------*/
void h264GetSarInfo(const storage_t * storage, u32 * sar_width,
                    u32 * sar_height) {
  switch (h264bsdAspectRatioIdc(storage)) {
  case 0:
    *sar_width = 0;
    *sar_height = 0;
    break;
  case 1:
    *sar_width = 1;
    *sar_height = 1;
    break;
  case 2:
    *sar_width = 12;
    *sar_height = 11;
    break;
  case 3:
    *sar_width = 10;
    *sar_height = 11;
    break;
  case 4:
    *sar_width = 16;
    *sar_height = 11;
    break;
  case 5:
    *sar_width = 40;
    *sar_height = 33;
    break;
  case 6:
    *sar_width = 24;
    *sar_height = 1;
    break;
  case 7:
    *sar_width = 20;
    *sar_height = 11;
    break;
  case 8:
    *sar_width = 32;
    *sar_height = 11;
    break;
  case 9:
    *sar_width = 80;
    *sar_height = 33;
    break;
  case 10:
    *sar_width = 18;
    *sar_height = 11;
    break;
  case 11:
    *sar_width = 15;
    *sar_height = 11;
    break;
  case 12:
    *sar_width = 64;
    *sar_height = 33;
    break;
  case 13:
    *sar_width = 160;
    *sar_height = 99;
    break;
  case 255:
    h264bsdSarSize(storage, sar_width, sar_height);
    break;
  default:
    *sar_width = 0;
    *sar_height = 0;
  }
}

/*------------------------------------------------------------------------------
    Function name   : h264CheckReleasePpAndHw
    Description     : Release HW lock and wait for PP to finish, need to be
                      called if errors/problems after first field of a picture
                      finished and PP left running
    Return type     : void
    Argument        :
    Argument        :
    Argument        :
------------------------------------------------------------------------------*/
void h264CheckReleasePpAndHw(decContainer_t *dec_cont) {

  if(dec_cont->pp.pp_instance != NULL &&
      (dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING ||
       dec_cont->pp.dec_pp_if.pp_status == DECPP_PIC_NOT_FINISHED)) {
    dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;
    dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);
  }

  if (dec_cont->keep_hw_reserved) {
    dec_cont->keep_hw_reserved = 0;
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);
  }

}

/*------------------------------------------------------------------------------

    Function: H264DecPeek

        Functional description:
            Get last decoded picture if any available. No pictures are removed
            from output nor DPB buffers.

        Input:
            dec_inst     decoder instance.

        Output:
            output     pointer to output structure

        Returns:
            H264DEC_OK            no pictures available for display
            H264DEC_PIC_RDY       picture available for display
            H264DEC_PARAM_ERROR   invalid parameters

------------------------------------------------------------------------------*/
H264DecRet H264DecPeek(H264DecInst dec_inst, H264DecPicture * output) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  dpbPicture_t *current_out = dec_cont->storage.dpb->current_out;

  DEC_API_TRC("H264DecPeek#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("H264DecPeek# ERROR: dec_inst or output is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecPeek# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  if (dec_cont->dec_stat != H264DEC_NEW_HEADERS &&
      dec_cont->storage.dpb->fullness && current_out != NULL &&
      (current_out->status[0] != EMPTY || current_out->status[1] != EMPTY)) {

    output->output_picture = current_out->data->virtual_address;
    output->output_picture_bus_address = current_out->data->bus_address;
    output->pic_id = current_out->pic_id;
    output->pic_coding_type[0] = current_out->pic_code_type[0];
    output->pic_coding_type[1] = current_out->pic_code_type[1];
    output->is_idr_picture[0] = current_out->is_idr[0];
    output->is_idr_picture[1] = current_out->is_idr[1];
    output->decode_id[0] = current_out->decode_id[0];
    output->decode_id[1] = current_out->decode_id[1];
    output->nbr_of_err_mbs = current_out->num_err_mbs;

    output->interlaced = dec_cont->storage.dpb->interlaced;
    output->field_picture = current_out->is_field_pic;
    output->output_format = current_out->tiled_mode ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;
    output->top_field = 0;
    output->pic_struct = current_out->pic_struct;

    if (output->field_picture) {
      /* just one field in buffer -> that is the output */
      if(current_out->status[0] == EMPTY || current_out->status[1] == EMPTY) {
        output->top_field = (current_out->status[0] == EMPTY) ? 0 : 1;
      }
      /* both fields decoded -> check field parity from slice header */
      else
        output->top_field =
          dec_cont->storage.slice_header->bottom_field_flag == 0;
    } else
      output->top_field = 1;

    output->pic_width = h264bsdPicWidth(&dec_cont->storage) << 4;
    output->pic_height = h264bsdPicHeight(&dec_cont->storage) << 4;

    output->crop_params = current_out->crop;


    DEC_API_TRC("H264DecPeek# H264DEC_PIC_RDY\n");
    return (H264DEC_PIC_RDY);
  } else {
    DEC_API_TRC("H264DecPeek# H264DEC_OK\n");
    return (H264DEC_OK);
  }

}

/*------------------------------------------------------------------------------

    Function: H264DecSetMvc()

        Functional description:
            This function configures decoder to decode both views of MVC
            stereo high profile compliant streams.

        Inputs:
            dec_inst     decoder instance

        Outputs:

        Returns:
            H264DEC_OK            success
            H264DEC_PARAM_ERROR   invalid parameters
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet

------------------------------------------------------------------------------*/
H264DecRet H264DecSetMvc(H264DecInst dec_inst) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  DWLHwConfig hw_cfg;

  DEC_API_TRC("H264DecSetMvc#");

  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecSetMvc# ERROR: dec_inst is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecSetMvc# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  (void) DWLmemset(&hw_cfg, 0, sizeof(DWLHwConfig));
  DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_H264_DEC);
  if(!hw_cfg.mvc_support) {
    DEC_API_TRC("H264DecSetMvc# ERROR: H264 MVC not supported in HW\n");
    return H264DEC_FORMAT_NOT_SUPPORTED;
  }

  dec_cont->storage.mvc = HANTRO_TRUE;

  DEC_API_TRC("H264DecSetMvc# OK\n");

  return (H264DEC_OK);
}

#ifdef USE_OUTPUT_RELEASE
/*------------------------------------------------------------------------------

    Function: H264DecPictureConsumed()

        Functional description:
            Release the frame displayed and sent by APP

        Inputs:
            dec_inst     Decoder instance

            picture    pointer of picture structure to be released

        Outputs:
            none

        Returns:
            H264DEC_PARAM_ERROR       Decoder instance or picture is null
            H264DEC_NOT_INITIALIZED   Decoder instance isn't initialized
            H264DEC_OK                picture release success

------------------------------------------------------------------------------*/
H264DecRet H264DecPictureConsumed(H264DecInst dec_inst,
                                  const H264DecPicture *picture) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  const dpbStorage_t *dpb;
  u32 id = FB_NOT_VALID_ID, i;

  DEC_API_TRC("H264DecPictureConsumed#\n");

  if(dec_inst == NULL || picture == NULL) {
    DEC_API_TRC("H264DecPictureConsumed# ERROR: dec_inst or picture is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecPictureConsumed# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  if (!dec_cont->pp_enabled) {
    /* find the mem descriptor for this specific buffer, base view first */
    dpb = dec_cont->storage.dpbs[0];
    for(i = 0; i < dpb->tot_buffers; i++) {
      if(picture->output_picture_bus_address == dpb->pic_buffers[i].bus_address &&
          picture->output_picture == dpb->pic_buffers[i].virtual_address) {
        id = i;
        break;
      }
    }

    /* if not found, search other view for MVC mode */
    if(id == FB_NOT_VALID_ID && dec_cont->storage.mvc == HANTRO_TRUE) {
      dpb = dec_cont->storage.dpbs[1];
      /* find the mem descriptor for this specific buffer */
      for(i = 0; i < dpb->tot_buffers; i++) {
        if(picture->output_picture_bus_address == dpb->pic_buffers[i].bus_address &&
            picture->output_picture == dpb->pic_buffers[i].virtual_address) {
          id = i;
          break;
        }
      }
    }

    if(id == FB_NOT_VALID_ID)
      return H264DEC_PARAM_ERROR;

    PopOutputPic(&dec_cont->fb_list, dpb->pic_buff_id[id]);
  } else {
    InputQueueReturnBuffer(dec_cont->pp_buffer_queue, picture->output_picture);
  }

  return H264DEC_OK;
}


/*------------------------------------------------------------------------------

    Function: H264DecNextPicture_INTERNAL

        Functional description:
            Push next picture in display order into output list if any available.

        Input:
            dec_inst     decoder instance.
            end_of_stream force output of all buffered pictures

        Output:
            output     pointer to output structure

        Returns:
            H264DEC_OK            no pictures available for display
            H264DEC_PIC_RDY       picture available for display
            H264DEC_PARAM_ERROR     invalid parameters
            H264DEC_NOT_INITIALIZED   decoder instance not initialized yet

------------------------------------------------------------------------------*/
H264DecRet H264DecNextPicture_INTERNAL(H264DecInst dec_inst,
                                       H264DecPicture * output,
                                       u32 end_of_stream) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  const dpbOutPicture_t *out_pic = NULL;
  dpbStorage_t *out_dpb;
  storage_t *storage;
  sliceHeader_t *p_slice_hdr;

  DEC_API_TRC("H264DecNextPicture_INTERNAL#\n");

  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("H264DecNextPicture_INTERNAL# ERROR: dec_inst or output is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecNextPicture_INTERNAL# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  storage = &dec_cont->storage;
  p_slice_hdr = storage->slice_header;
  out_dpb = dec_cont->storage.dpbs[dec_cont->storage.out_view];

  /* if display order is the same as decoding order and PP is used and
   * cannot be used in pipeline (rotation) -> do not perform PP here but
   * while decoding next picture (parallel processing instead of
   * DEC followed by PP followed by DEC...) */
  if (dec_cont->storage.pending_out_pic) {
    out_pic = dec_cont->storage.pending_out_pic;
    dec_cont->storage.pending_out_pic = NULL;
  } else if(out_dpb->no_reordering == 0) {
    if(!out_dpb->delayed_out) {
      if (dec_cont->pp.pp_instance && dec_cont->pp.dec_pp_if.pp_status ==
          DECPP_PIC_READY)
        out_dpb->no_output = 0;

      dec_cont->storage.dpb =
        dec_cont->storage.dpbs[dec_cont->storage.out_view];

      out_pic = h264bsdNextOutputPicture(&dec_cont->storage);

      if ( (dec_cont->storage.num_views ||
            dec_cont->storage.out_view) && out_pic != NULL) {
        output->view_id =
          dec_cont->storage.view_id[dec_cont->storage.out_view];
        dec_cont->storage.out_view ^= 0x1;
      }
    }
  } else {
    /* no reordering of output pics AND stereo was activated after base
     * picture was output -> output stereo view pic if available */
    if (dec_cont->storage.num_views &&
        dec_cont->storage.view && dec_cont->storage.out_view == 0 &&
        out_dpb->num_out == 0 &&
        dec_cont->storage.dpbs[dec_cont->storage.view]->num_out > 0) {
      dec_cont->storage.out_view ^= 0x1;
      out_dpb = dec_cont->storage.dpbs[dec_cont->storage.out_view];
    }

    if(out_dpb->num_out > 1 || end_of_stream ||
        storage->prev_nal_unit->nal_ref_idc == 0 ||
        dec_cont->pp.pp_instance == NULL ||
        dec_cont->pp.dec_pp_if.use_pipeline ||
        storage->view != storage->out_view) {
      if(!end_of_stream &&
          ((out_dpb->num_out == 1 && out_dpb->delayed_out) ||
           (p_slice_hdr->field_pic_flag && storage->second_field))) {
      } else {
        dec_cont->storage.dpb =
          dec_cont->storage.dpbs[dec_cont->storage.out_view];

        out_pic = h264bsdNextOutputPicture(&dec_cont->storage);

        output->view_id =
          dec_cont->storage.view_id[dec_cont->storage.out_view];
        if ( (dec_cont->storage.num_views ||
              dec_cont->storage.out_view) && out_pic != NULL)
          dec_cont->storage.out_view ^= 0x1;
      }
    }
  }

  if(out_pic != NULL) {
    if (!dec_cont->storage.num_views)
      output->view_id = 0;

    if (!dec_cont->pp_enabled) {
      output->output_picture = out_pic->data->virtual_address;
      output->output_picture_bus_address = out_pic->data->bus_address;
      output->output_format = out_pic->tiled_mode ?
                              DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;
      output->pic_width = out_pic->pic_width;
      output->pic_height = out_pic->pic_height;
    } else {
      output->output_picture = out_pic->pp_data->virtual_address;
      output->output_picture_bus_address = out_pic->pp_data->bus_address;
      output->output_format = DEC_OUT_FRM_RASTER_SCAN;
      output->pic_width = out_pic->pic_width >> dec_cont->dscale_shift_x;
      output->pic_height = out_pic->pic_height >> dec_cont->dscale_shift_y;
    }

    output->sar_width = out_pic->sar_width;
    output->sar_height = out_pic->sar_height;
    output->pic_id = out_pic->pic_id;
    output->pic_coding_type[0] = out_pic->pic_code_type[0];
    output->pic_coding_type[1] = out_pic->pic_code_type[1];
    output->is_idr_picture[0] = out_pic->is_idr[0];
    output->is_idr_picture[1] = out_pic->is_idr[1];
    output->decode_id[0] = out_pic->decode_id[0];
    output->decode_id[1] = out_pic->decode_id[1];
    output->nbr_of_err_mbs = out_pic->num_err_mbs;

    output->interlaced = out_pic->interlaced;
    output->field_picture = out_pic->field_picture;
    output->top_field = out_pic->top_field;

    output->crop_params = out_pic->crop;
    if (output->field_picture)
      output->pic_struct = output->top_field ? TOPFIELD : BOTFIELD;
    else
      output->pic_struct = out_pic->pic_struct;

    DEC_API_TRC("H264DecNextPicture_INTERNAL# H264DEC_PIC_RDY\n");

    if (output->nbr_of_err_mbs && !out_pic->corrupted_second_field)
      ClearOutput(&dec_cont->fb_list, out_pic->mem_idx);
    else
      PushOutputPic(&dec_cont->fb_list, output, out_pic->mem_idx);

    /* Consume reference buffer when only output pp buffer. */
    if (dec_cont->pp_enabled) {
      InputQueueSetBufAsUsed(dec_cont->pp_buffer_queue, output->output_picture);
      PopOutputPic(&dec_cont->fb_list, out_pic->mem_idx);
    }

    return (H264DEC_PIC_RDY);
  } else {
    DEC_API_TRC("H264DecNextPicture_INTERNAL# H264DEC_OK\n");
    return (H264DEC_OK);
  }
}


H264DecRet H264DecEndOfStream(H264DecInst dec_inst, u32 strm_end_flag) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  u32 count = 0;

  DEC_API_TRC("H264DecEndOfStream#\n");

  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecEndOfStream# ERROR: dec_inst is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecEndOfStream# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }
  pthread_mutex_lock(&dec_cont->protect_mutex);
  if(dec_cont->dec_stat == H264DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (H264DEC_OK);
  }

  if(dec_cont->asic_running) {
    /* stop HW */
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->h264_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->asic_running = 0;

    /* Decrement usage for DPB buffers */
    DecrementDPBRefCount(&dec_cont->storage.dpb[1]);
    dec_cont->dec_stat = H264DEC_INITIALIZED;
    h264InitPicFreezeOutput(dec_cont, 1);
  } else if (dec_cont->keep_hw_reserved) {
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    dec_cont->keep_hw_reserved = 0;
  }

  /* flush any remaining pictures form DPB */
  h264bsdFlushBuffer(&dec_cont->storage);

  FinalizeOutputAll(&dec_cont->fb_list);

  {
    H264DecPicture output;

    while(H264DecNextPicture_INTERNAL(dec_inst, &output, 1) == H264DEC_PIC_RDY) {
      count++;
    }
  }

  /* After all output pictures were pushed, update decoder status to
   * reflect the end-of-stream situation. This way the H264DecMCNextPicture
   * will not block anymore once all output was handled.
   */
  if(strm_end_flag)
    dec_cont->dec_stat = H264DEC_END_OF_STREAM;

  /* wake-up output thread */
  PushOutputPic(&dec_cont->fb_list, NULL, -1);

  /* TODO(atna): should it be enough to wait until all cores idle and
   *             not that output is empty !?
   */
  if (strm_end_flag) {
#ifdef USE_EXT_BUF_SAFE_RELEASE
    WaitListNotInUse(&dec_cont->fb_list);
    if (dec_cont->pp_enabled)
      InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);
#endif
  }

  pthread_mutex_unlock(&dec_cont->protect_mutex);

  DEC_API_TRC("H264DecEndOfStream# H264DEC_OK\n");
  return (H264DEC_OK);
}

#endif


#ifdef USE_EXTERNAL_BUFFER
void h264SetExternalBufferInfo(H264DecInst dec_inst, storage_t *storage) {
  decContainer_t *dec_cont = (decContainer_t *)dec_inst;
  u32 pic_size_in_mbs = storage->active_sps->pic_width_in_mbs * storage->active_sps->pic_height_in_mbs;
  u32 pic_size = pic_size_in_mbs * (storage->active_sps->mono_chrome ? 256 : 384);

  /* buffer size of dpb pic = pic_size + dir_mv_size + tbl_size */
  u32 dmv_mem_size = pic_size_in_mbs * 64;
  u32 ref_buff_size = pic_size  + dmv_mem_size + 32;
  u32 min_buffer_num, max_dpb_size, no_reorder, tot_buffers;
  u32 pp_buff_size, ext_buffer_size;

  if(storage->no_reordering ||
      storage->active_sps->pic_order_cnt_type == 2 ||
      (storage->active_sps->vui_parameters_present_flag &&
       storage->active_sps->vui_parameters->bitstream_restriction_flag &&
       !storage->active_sps->vui_parameters->num_reorder_frames))
    no_reorder = HANTRO_TRUE;
  else
    no_reorder = HANTRO_FALSE;

  if(storage->view == 0)
    max_dpb_size = storage->active_sps->max_dpb_size;
  else {
    /* stereo view dpb size at least equal to base view size (to make sure
     * that base view pictures get output in correct display order) */
    max_dpb_size = MAX(storage->active_sps->max_dpb_size, storage->active_view_sps[0]->max_dpb_size);
  }
  /* restrict max dpb size of mvc (stereo high) streams, make sure that
   * base address 15 is available/restricted for inter view reference use */
  if(storage->mvc_stream)
    max_dpb_size = MIN(max_dpb_size, 8);

  if(no_reorder)
    tot_buffers = MAX(storage->active_sps->num_ref_frames,1) + 1;
  else
    tot_buffers = max_dpb_size + 1;

  if(tot_buffers > MAX_FRAME_BUFFER_NUMBER)
    tot_buffers = MAX_FRAME_BUFFER_NUMBER;

  min_buffer_num = tot_buffers;
  ext_buffer_size =  ref_buff_size;

  if (dec_cont->pp_enabled) {
    u32 pp_width, pp_height, pp_stride;
    pp_width = (storage->active_sps->pic_width_in_mbs * 16) >> dec_cont->dscale_shift_x;
    pp_height = (storage->active_sps->pic_height_in_mbs * 16) >> dec_cont->dscale_shift_y;
    pp_stride = ((pp_width + 16) >> 4) << 4;
    pp_buff_size = pp_stride * pp_height * (storage->active_sps->mono_chrome ? 2 : 3) / 2;
    ext_buffer_size = pp_buff_size;
  }

  dec_cont->buf_num = min_buffer_num;
  dec_cont->next_buf_size = ext_buffer_size;
}

void h264SetMVCExternalBufferInfo(H264DecInst dec_inst, storage_t *storage) {
  decContainer_t *dec_cont = (decContainer_t *)dec_inst;
  u32 pic_size_in_mbs, pic_size;

  if(storage->sps[1] != 0)
    pic_size_in_mbs = MAX(storage->sps[0]->pic_width_in_mbs * storage->sps[0]->pic_height_in_mbs,
                          storage->sps[1]->pic_width_in_mbs * storage->sps[1]->pic_height_in_mbs);
  else
    pic_size_in_mbs = storage->sps[0]->pic_width_in_mbs * storage->sps[0]->pic_height_in_mbs;

  pic_size = pic_size_in_mbs * (storage->sps[0]->mono_chrome ? 256 : 384);

  /* buffer size of dpb pic = pic_size + dir_mv_size + tbl_size */
  u32 dmv_mem_size = pic_size_in_mbs * 64;
  dec_cont->next_buf_size = pic_size  + dmv_mem_size + 32;

  dec_cont->buf_num = 0;
  u32 j = 0;
  for(u32 i = 0; i < 2; i ++) {
    u32 max_dpb_size, no_reorder, tot_buffers;
    if(storage->no_reordering ||
        storage->sps[j]->pic_order_cnt_type == 2 ||
        (storage->sps[j]->vui_parameters_present_flag &&
         storage->sps[j]->vui_parameters->bitstream_restriction_flag &&
         !storage->sps[j]->vui_parameters->num_reorder_frames))
      no_reorder = HANTRO_TRUE;
    else
      no_reorder = HANTRO_FALSE;

    max_dpb_size = storage->sps[j]->max_dpb_size;

    /* restrict max dpb size of mvc (stereo high) streams, make sure that
    * base address 15 is available/restricted for inter view reference use */
    max_dpb_size = MIN(max_dpb_size, 8);

    if(no_reorder)
      tot_buffers = MAX(storage->sps[j]->num_ref_frames, 1) + 1;
    else
      tot_buffers = max_dpb_size + 1;

    dec_cont->buf_num += tot_buffers;
    if(storage->sps[1] != 0)
      j ++;
  }

  if(dec_cont->buf_num > MAX_FRAME_BUFFER_NUMBER)
    dec_cont->buf_num = MAX_FRAME_BUFFER_NUMBER;
}


H264DecRet H264DecGetBufferInfo(H264DecInst dec_inst, H264DecBufferInfo *mem_info) {
  decContainer_t  * dec_cont = (decContainer_t *)dec_inst;

  struct DWLLinearMem empty;
  (void)DWLmemset(&empty, 0, sizeof(struct DWLLinearMem));
  struct DWLLinearMem *buffer = NULL;

  if(dec_cont == NULL || mem_info == NULL) {
    return H264DEC_PARAM_ERROR;
  }

  if (dec_cont->storage.release_buffer) {
    /* Release old buffers from input queue. */
    //buffer = InputQueueGetBuffer(dec_cont->pp_buffer_queue, 0);
    buffer = NULL;
    if (dec_cont->ext_buffer_num) {
      buffer = &dec_cont->ext_buffers[dec_cont->ext_buffer_num - 1];
      dec_cont->ext_buffer_num--;
    }
    if (buffer == NULL) {
      /* All buffers have been released. */
      dec_cont->storage.release_buffer = 0;
      InputQueueRelease(dec_cont->pp_buffer_queue);
      dec_cont->pp_buffer_queue = InputQueueInit(0);
      if (dec_cont->pp_buffer_queue == NULL) {
        return (H264DEC_MEMFAIL);
      }
      dec_cont->storage.pp_buffer_queue = dec_cont->pp_buffer_queue;
      dec_cont->storage.ext_buffer_added = 0;
      mem_info->buf_to_free = empty;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return H264DEC_OK;
    } else {
      mem_info->buf_to_free = *buffer;
      mem_info->next_buf_size = 0;
      mem_info->buf_num = 0;
      return H264DEC_WAITING_FOR_BUFFER;
    }
  }

  if(dec_cont->buf_to_free == NULL && dec_cont->next_buf_size == 0) {
    /* External reference buffer: release done. */
    mem_info->buf_to_free = empty;
    mem_info->next_buf_size = dec_cont->next_buf_size;
    mem_info->buf_num = dec_cont->buf_num;
    return H264DEC_OK;
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

  return H264DEC_WAITING_FOR_BUFFER;
}

H264DecRet H264DecAddBuffer(H264DecInst dec_inst, struct DWLLinearMem *info) {
  decContainer_t *dec_cont = (decContainer_t *)dec_inst;
  H264DecRet dec_ret = H264DEC_OK;

  if(dec_inst == NULL || info == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(info->virtual_address) ||
      X170_CHECK_BUS_ADDRESS_AGLINED(info->bus_address) ||
      info->size < dec_cont->next_buf_size) {
    return H264DEC_PARAM_ERROR;
  }

  dec_cont->n_ext_buf_size = dec_cont->storage.ext_buffer_size = info->size;
  if (dec_cont->pp_enabled) {
    dec_cont->ext_buffers[dec_cont->ext_buffer_num] = *info;
    dec_cont->ext_buffer_num++;
  }
  if(!dec_cont->b_mvc) {
    u32 i = dec_cont->buffer_index[0];
    u32 id;
    dpbStorage_t *dpb = dec_cont->storage.dpbs[0];
    if (dec_cont->pp_enabled == 0) {
      if(i < dpb->tot_buffers) {
        dpb->pic_buffers[i] = *info;
        if(i < dpb->dpb_size + 1) {
          id = AllocateIdUsed(dpb->fb_list, dpb->pic_buffers + i);
          if(id == FB_NOT_VALID_ID) {
            return MEMORY_ALLOCATION_ERROR;
          }

          dpb->buffer[i].data = dpb->pic_buffers + i;
          dpb->buffer[i].mem_idx = id;
          dpb->buffer[i].num_err_mbs = -1;
          dpb->pic_buff_id[i] = id;
        } else {
          id = AllocateIdFree(dpb->fb_list, dpb->pic_buffers + i);
          if(id == FB_NOT_VALID_ID) {
            return MEMORY_ALLOCATION_ERROR;
          }

          dpb->pic_buff_id[i] = id;
        }

        void *base =
          (char *)(dpb->pic_buffers[i].virtual_address) + dpb->dir_mv_offset;
        (void)DWLmemset(base, 0, info->size - dpb->dir_mv_offset);

        dec_cont->buffer_index[0]++;
        if(dec_cont->buffer_index[0] < dpb->tot_buffers)
          dec_ret = H264DEC_WAITING_FOR_BUFFER;
      } else {
        /* Adding extra buffers. */
        if(dec_cont->buffer_index[0] >= MAX_FRAME_BUFFER_NUMBER) {
          /* Too much buffers added. */
          dec_cont->ext_buffer_num--;
          return H264DEC_EXT_BUFFER_REJECTED;
        }

        dpb->pic_buffers[i] = *info;
        dpb[1].pic_buffers[i] = *info;
        /* Need the allocate a USED id to be added as free buffer in SetFreePicBuffer. */
        id = AllocateIdUsed(dpb->fb_list, dpb->pic_buffers + i);
        if(id == FB_NOT_VALID_ID) {
          return MEMORY_ALLOCATION_ERROR;
        }
        dpb->pic_buff_id[i] = id;
        dpb[1].pic_buff_id[i] = id;

        void *base =
          (char *)(dpb->pic_buffers[i].virtual_address) + dpb->dir_mv_offset;
        (void)DWLmemset(base, 0, info->size - dpb->dir_mv_offset);

        dec_cont->buffer_index[0]++;
        dpb->tot_buffers++;
        dpb[1].tot_buffers++;

        SetFreePicBuffer(dpb->fb_list, id);
      }
    } else {
      /* Add down scale buffer. */
      InputQueueAddBuffer(dec_cont->pp_buffer_queue, info);

      dec_cont->storage.ext_buffer_added = 1;
    }
  } else {
    u32 * idx = dec_cont->buffer_index;
    if (dec_cont->pp_enabled == 0) {
      if(idx[0] < dec_cont->storage.dpbs[0]->tot_buffers || idx[1] < dec_cont->storage.dpbs[1]->tot_buffers) {
        for(u32 i = 0; i < 2; i ++) {
          u32 id;
          dpbStorage_t *dpb = dec_cont->storage.dpbs[i];
          if(idx[i] < dpb->tot_buffers) {
            dpb->pic_buffers[idx[i]] = *info;
            if(idx[i] < dpb->dpb_size + 1) {
              id = AllocateIdUsed(dpb->fb_list, dpb->pic_buffers + idx[i]);
              if(id == FB_NOT_VALID_ID) {
                return MEMORY_ALLOCATION_ERROR;
              }

            dpb->buffer[idx[i]].data = dpb->pic_buffers + idx[i];
            dpb->buffer[idx[i]].mem_idx = id;
            dpb->pic_buff_id[idx[i]] = id;
          } else {
            id = AllocateIdFree(dpb->fb_list, dpb->pic_buffers + idx[i]);
            if(id == FB_NOT_VALID_ID) {
              return MEMORY_ALLOCATION_ERROR;
            }

            dpb->pic_buff_id[idx[i]] = id;
          }

          void *base =
            (char *)(dpb->pic_buffers[idx[i]].virtual_address) + dpb->dir_mv_offset;
          (void)DWLmemset(base, 0, info->size - dpb->dir_mv_offset);

          dec_cont->buffer_index[i]++;
          if(dec_cont->buffer_index[i] < dpb->tot_buffers)
            dec_ret = H264DEC_WAITING_FOR_BUFFER;
          break;
        }
      }
    } else {
      /* Adding extra buffers. */
      if((idx[0] + idx[1]) >= MAX_FRAME_BUFFER_NUMBER) {
        /* Too much buffers added. */
        dec_cont->ext_buffer_num--;
        return H264DEC_EXT_BUFFER_REJECTED;
      }
      u32 i = idx[0] < idx[1] ? 0 : 1;
      dpbStorage_t *dpb = dec_cont->storage.dpbs[i];
      dpb->pic_buffers[idx[i]] = *info;
      /* Need the allocate a USED id to be added as free buffer in SetFreePicBuffer. */
      u32 id = AllocateIdUsed(dpb->fb_list, dpb->pic_buffers + idx[i]);
      if(id == FB_NOT_VALID_ID) {
        return MEMORY_ALLOCATION_ERROR;
      }
      dpb->pic_buff_id[idx[i]] = id;

      void *base =
        (char *)(dpb->pic_buffers[idx[i]].virtual_address) + dpb->dir_mv_offset;
      (void)DWLmemset(base, 0, info->size - dpb->dir_mv_offset);

      dec_cont->buffer_index[i]++;
      dpb->tot_buffers++;

        SetFreePicBuffer(dpb->fb_list, id);
      }
    }else {
      /* Add down scale buffer. */
      InputQueueAddBuffer(dec_cont->pp_buffer_queue, info);

      dec_cont->storage.ext_buffer_added = 1;
    }
  }

  return dec_ret;
}
#endif


#ifdef USE_OUTPUT_RELEASE
void h264EnterAbortState(decContainer_t *dec_cont) {
  SetAbortStatusInList(&dec_cont->fb_list);
  dec_cont->abort = 1;
}

void h264ExistAbortState(decContainer_t *dec_cont) {
  ClearAbortStatusInList(&dec_cont->fb_list);
  dec_cont->abort = 0;
}

void h264StateReset(decContainer_t *dec_cont) {
  dpbStorage_t *dpb = dec_cont->storage.dpbs[0];

  /* Clear parameters in dpb */
  h264EmptyDpb(dpb);
  if (dec_cont->storage.mvc_stream) {
    dpb = dec_cont->storage.dpbs[1];
    h264EmptyDpb(dpb);
  }

  /* Clear parameters in storage */
  h264bsdClearStorage(&dec_cont->storage);

  /* Clear parameters in decContainer */
  dec_cont->dec_stat = H264DEC_INITIALIZED;
  dec_cont->pic_number = 0;
#ifdef CLEAR_HDRINFO_IN_SEEK
  dec_cont->rlc_mode = 0;
  dec_cont->try_vlc = 0;
  dec_cont->mode_change = 0;
#endif
  dec_cont->reallocate = 0;
  dec_cont->gaps_checked_for_this = 0;
  dec_cont->packet_decoded = 0;
  dec_cont->keep_hw_reserved = 0;
  dec_cont->force_nal_mode = 0;
#ifdef USE_OMXIL_BUFFER
  dec_cont->buffer_index[0] = 0;
  dec_cont->buffer_index[1] = 0;
  dec_cont->ext_buffer_num = 0;
#endif

#ifdef SKIP_OPENB_FRAME
  dec_cont->entry_is_IDR = 0;
  dec_cont->entry_POC = 0;
  dec_cont->first_entry_point = 0;
  dec_cont->skip_b = 0;
#endif

  dec_cont->alloc_buffer = 0;
  dec_cont->no_decoding_buffer = 0;
}

H264DecRet H264DecAbort(H264DecInst dec_inst) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecAbort# ERROR: dec_inst is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecSetMvc# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);
  /* Abort frame buffer waiting and rs/ds buffer waiting */
  h264EnterAbortState(dec_cont);
  pthread_mutex_unlock(&dec_cont->protect_mutex);
  return (H264DEC_OK);
}

H264DecRet H264DecAbortAfter(H264DecInst dec_inst) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  u32 i;
  i32 core_id;

  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecAbortAfter# ERROR: dec_inst is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecAbortAfter# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);

#if 0
  /* If normal EOS is waited, return directly */
  if(dec_cont->dec_stat == H264DEC_END_OF_STREAM) {
    pthread_mutex_unlock(&dec_cont->protect_mutex);
    return (H264DEC_OK);
  }
#endif

  if(dec_cont->asic_running && !dec_cont->b_mc) {
    /* stop HW */
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_E, 0);
    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->h264_regs[1] | DEC_IRQ_DISABLE);
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);  /* release HW lock */
    DecrementDPBRefCount(dec_cont->storage.dpb);
    dec_cont->asic_running = 0;

  }

  /* In multi-Core senario, waithwready is executed through listener thread,
          here to check whether HW is finished */
  if(dec_cont->b_mc) {
    for(i = 0; i < dec_cont->n_cores; i++) {
      DWLReserveHw(dec_cont->dwl, &core_id);
    }
    /* All HW Core finished */
    for(i = 0; i < dec_cont->n_cores; i++) {
      DWLReleaseHw(dec_cont->dwl, i);
    }
  }


  /* Clear internal parameters */
  h264StateReset(dec_cont);
  h264ExistAbortState(dec_cont);

#ifdef USE_OMXIL_BUFFER
  pthread_mutex_lock(&dec_cont->fb_list.ref_count_mutex);
  for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
    dec_cont->fb_list.fb_stat[i].n_ref_count = 0;
  }
  pthread_mutex_unlock(&dec_cont->fb_list.ref_count_mutex);
#endif

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  return (H264DEC_OK);
}
#endif

H264DecRet H264DecSetNoReorder(H264DecInst dec_inst, u32 no_output_reordering) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecSetNoReorder# ERROR: dec_inst is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecSetNoReorder# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  pthread_mutex_lock(&dec_cont->protect_mutex);

  dec_cont->storage.no_reordering = no_output_reordering;
  if(dec_cont->storage.dpb != NULL)
    dec_cont->storage.dpb->no_reordering = no_output_reordering;

  if (!no_output_reordering && dec_cont->storage.dpb != NULL) {
    if (dec_cont->storage.active_sps->pic_order_cnt_type == 2 ||
      (dec_cont->storage.active_sps->vui_parameters_present_flag &&
      dec_cont->storage.active_sps->vui_parameters->bitstream_restriction_flag &&
      !dec_cont->storage.active_sps->vui_parameters->num_reorder_frames))
      dec_cont->storage.dpb->no_reordering = HANTRO_TRUE;
    else
      dec_cont->storage.dpb->no_reordering = HANTRO_FALSE;
  }

  pthread_mutex_unlock(&dec_cont->protect_mutex);
  return (H264DEC_OK);
}

H264DecRet H264DecSetInfo(H264DecInst dec_inst,
                          H264DecConfig *dec_cfg) {

  /*@null@ */ decContainer_t *dec_cont = (decContainer_t *)dec_inst;
  DWLHwConfig hw_cfg;
  u32 reference_frame_format;
  storage_t *storage = &dec_cont->storage;
  u32 asic_id = DWLReadAsicID(DWL_CLIENT_TYPE_H264_DEC);

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecGetInfo# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  /* check that H.264 decoding supported in HW */
  (void) DWLmemset(&hw_cfg, 0, sizeof(DWLHwConfig));
  DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_H264_DEC);
  if(!hw_cfg.h264_support) {
    DEC_API_TRC("H264DecSetInfo# ERROR: H264 not supported in HW\n");
    return H264DEC_FORMAT_NOT_SUPPORTED;
  }

  if(!hw_cfg.addr64_support && sizeof(void *) == 8) {
    DEC_API_TRC("H264DecSetInfo# ERROR: HW not support 64bit address!\n");
    return (H264DEC_PARAM_ERROR);
  }

  reference_frame_format = dec_cfg->dpb_flags & DEC_REF_FRM_FMT_MASK;
  if(reference_frame_format == DEC_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!hw_cfg.tiled_mode_support) {
      return H264DEC_FORMAT_NOT_SUPPORTED;
    }
    dec_cont->tiled_mode_support = hw_cfg.tiled_mode_support;
  } else
    dec_cont->tiled_mode_support = 0;

  /* Custom DPB modes require tiled support >= 2 */
  dec_cont->allow_dpb_field_ordering = 0;
  if( dec_cfg->dpb_flags & DEC_DPB_ALLOW_FIELD_ORDERING ) {
    dec_cont->allow_dpb_field_ordering = hw_cfg.field_dpb_support;
  }
  /* Initialize DPB mode */
  if( !dec_cont->storage.active_sps->frame_mbs_only_flag &&
      dec_cont->allow_dpb_field_ordering )
    dec_cont->dpb_mode = DEC_DPB_INTERLACED_FIELD;
  else
    dec_cont->dpb_mode = DEC_DPB_FRAME;

  /* Initialize tiled mode */
  if( dec_cont->tiled_mode_support &&
    DecCheckTiledMode( dec_cont->tiled_mode_support,
                       dec_cont->dpb_mode,
                       !dec_cont->storage.active_sps->frame_mbs_only_flag ) !=
                        HANTRO_OK ) {
    return (H264DEC_PARAM_ERROR);
  }
#ifdef USE_EXTERNAL_BUFFER
  dec_cont->n_guard_size = dec_cfg->guard_size;
  dec_cont->use_adaptive_buffers = dec_cfg->use_adaptive_buffers;
#endif
  dec_cont->secure_mode = dec_cfg->use_secure_mode;
  if (dec_cont->secure_mode)
    dec_cont->ref_buf_support = 0;
#ifdef USE_EXTERNAL_BUFFER
  if(dec_cont->b_mvc == 0)
    h264SetExternalBufferInfo(dec_cont, storage);
  else if(dec_cont->b_mvc == 1) {
    h264SetMVCExternalBufferInfo(dec_cont, storage);
  }
#endif
  return (H264DEC_OK);
}
