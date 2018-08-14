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
#include "commonconfig.h"
#include "dwl.h"
#include "errorhandling.h"

#include "h264hwd_dpb_lock.h"
#include "h264decmc_internals.h"

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


extern void h264InitPicFreezeOutput(decContainer_t *dec_cont, u32 from_old_dpb);
extern const u32 ref_base[16];
extern const u32 ref_base_msb[16];

u32 H264DecMCGetCoreCount(void) {
  return DWLReadAsicCoreCount();
}

H264DecRet H264DecMCInit(H264DecInst *dec_inst, H264DecMCConfig *p_mcinit_cfg) {
  H264DecRet ret;
  decContainer_t *dec_cont;
  u32 i;
  u32 dpb_flags = DEC_REF_FRM_RASTER_SCAN; /* no support for tiled mode */
#ifdef USE_EXTERNAL_BUFFER
  const void *dwl;
  struct DWLInitParam dwl_init;
  dwl_init.client_type = DWL_CLIENT_TYPE_H264_DEC;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    DEC_API_TRC("H264DecInit# ERROR: DWL Init failed\n");
    return (H264DEC_DWL_ERROR);
  }
#endif

  DEC_API_TRC("H264DecMCInit#\n");

  if(dec_inst == NULL || p_mcinit_cfg == NULL) {
    DEC_API_TRC("H264DecMCInit# ERROR: dec_inst or p_mcinit_cfg is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  if(p_mcinit_cfg->dpb_flags & DEC_DPB_ALLOW_FIELD_ORDERING)
    dpb_flags |= DEC_DPB_ALLOW_FIELD_ORDERING;

  ret = H264DecInit( dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                     dwl,
#endif
                     p_mcinit_cfg->no_output_reordering,
                     /* DEC_EC_PICTURE_FREEZE, */
                     DEC_EC_PARTIAL_FREEZE,
                     p_mcinit_cfg->use_display_smoothing,
                     dpb_flags,
                     0, 0, 0, &p_mcinit_cfg->pp_cfg);

  if(ret != H264DEC_OK ) {
    DEC_API_TRC("H264DecMCInit# ERROR: Failed to create instance\n");
    return ret;
  }

  dec_cont = (decContainer_t *) (*dec_inst);

  dec_cont->b_mc = 1;

  dec_cont->n_cores = DWLReadAsicCoreCount();

  DWLReadMCAsicConfig(dec_cont->hw_cfg);

  /* check how many cores support H264 */
  for(i = 0; i < dec_cont->n_cores; i++) {
    if(!dec_cont->hw_cfg[i].h264_support)
      dec_cont->n_cores--;
  }

  dec_cont->stream_consumed_callback.fn = p_mcinit_cfg->stream_consumed_callback;

  /* enable synchronization writing in multi-Core HW */
  if(dec_cont->n_cores > 1) {
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_MULTICORE_E, 1);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_WRITESTAT_E, 1);
  }

  return ret;
}

H264DecRet H264DecMCDecode(H264DecInst dec_inst,
                           const H264DecInput *input,
                           H264DecOutput *output) {
  H264DecRet ret;
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;

  DEC_API_TRC("H264DecMCDecode#\n");

  /* Check that function input parameters are valid */
  if(input == NULL || output == NULL || dec_inst == NULL) {
    DEC_API_TRC("H264DecMCDecode# ERROR: NULL arg(s)\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecMCDecode# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  /* Currently we just call the single Core version, but we may change
   * our mind in the future. Do not call directly H264DecDecode() in any
   * multicore application */
  ret = H264DecDecode(dec_inst, input, output);

  return ret;
}


/*!\brief Mark last output picture consumed
 *
 * Application calls this after it has finished processing the picture
 * returned by H264DecMCNextPicture.
 */

H264DecRet H264DecMCPictureConsumed(H264DecInst dec_inst,
                                    const H264DecPicture *picture) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  const dpbStorage_t *dpb;
  u32 id = FB_NOT_VALID_ID, i;

  DEC_API_TRC("H264DecMCPictureConsumed#\n");

  if(dec_inst == NULL || picture == NULL) {
    DEC_API_TRC("H264DecMCPictureConsumed# ERROR: dec_inst or picture is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecMCPictureConsumed# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

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

  return H264DEC_OK;
}

/*------------------------------------------------------------------------------

    Function: H264DecMCNextPicture

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

------------------------------------------------------------------------------*/
H264DecRet H264DecMCNextPicture_INTERNAL(H264DecInst dec_inst,
    H264DecPicture * output,
    u32 end_of_stream) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  const dpbOutPicture_t *out_pic = NULL;
  dpbStorage_t *out_dpb;
  storage_t *storage;
  sliceHeader_t *p_slice_hdr;

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

    output->pic_width = h264bsdPicWidth(&dec_cont->storage) << 4;
    output->pic_height = h264bsdPicHeight(&dec_cont->storage) << 4;
    output->output_format = out_pic->tiled_mode ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;

    output->crop_params = out_pic->crop;

    DEC_API_TRC("H264DecNextPicture# H264DEC_PIC_RDY\n");


    PushOutputPic(&dec_cont->fb_list, output, out_pic->mem_idx);

    return (H264DEC_PIC_RDY);
  } else {
    DEC_API_TRC("H264DecNextPicture# H264DEC_OK\n");
    return (H264DEC_OK);
  }

}

H264DecRet H264DecMCEndOfStream(H264DecInst dec_inst) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;
  u32 count = 0;
  i32 core_id;

  DEC_API_TRC("H264DecMCEndOfStream#\n");

  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecMCEndOfStream# ERROR: dec_inst is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecMCEndOfStream# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  /* Check all Core in idle state */
  for(count = 0; count < dec_cont->n_cores; count++) {
    DWLReserveHw(dec_cont->dwl, &core_id);
  }
  /* All HW Core finished */
  for(count = 0; count < dec_cont->n_cores; count++) {
    DWLReleaseHw(dec_cont->dwl, count);
  }
  count = 0;

  /* flush any remaining pictures form DPB */
  h264bsdFlushBuffer(&dec_cont->storage);

  FinalizeOutputAll(&dec_cont->fb_list);

  {
    H264DecPicture output;

    while(H264DecMCNextPicture_INTERNAL(dec_inst, &output, 1) == H264DEC_PIC_RDY) {
      count++;
    }
  }

  /* After all output pictures were pushed, update decoder status to
   * reflect the end-of-stream situation. This way the H264DecMCNextPicture
   * will not block anymore once all output was handled.
   */
  dec_cont->dec_stat = H264DEC_END_OF_STREAM;

  /* wake-up output thread */
  PushOutputPic(&dec_cont->fb_list, NULL, -1);

#if defined(USE_EXTERNAL_BUFFER) && !defined(H264_EXT_BUF_SAFE_RELEASE)
  pthread_mutex_lock(&dec_cont->fb_list.ref_count_mutex);
  for (count = 0; count < MAX_FRAME_BUFFER_NUMBER; count++) {
    dec_cont->fb_list.fb_stat[count].n_ref_count = 0;
  }
  pthread_mutex_unlock(&dec_cont->fb_list.ref_count_mutex);
#endif

  h264MCWaitPicReadyAll(dec_cont);

  DEC_API_TRC("H264DecMCEndOfStream# H264DEC_OK\n");
  return (H264DEC_OK);
}

/*------------------------------------------------------------------------------

    Function: H264DecMCNextPicture

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

------------------------------------------------------------------------------*/
H264DecRet H264DecMCNextPicture(H264DecInst dec_inst, H264DecPicture * picture) {
  decContainer_t *dec_cont = (decContainer_t *) dec_inst;

  DEC_API_TRC("H264DecMCNextPicture#\n");

  if(dec_inst == NULL || picture == NULL) {
    DEC_API_TRC("H264DecMCNextPicture# ERROR: dec_inst or output is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("H264DecMCNextPicture# ERROR: Decoder not initialized\n");
    return (H264DEC_NOT_INITIALIZED);
  }

  if(dec_cont->dec_stat == H264DEC_END_OF_STREAM &&
      IsOutputEmpty(&dec_cont->fb_list)) {
    DEC_API_TRC("H264DecMCNextPicture# H264DEC_END_OF_STREAM\n");
    return (H264DEC_END_OF_STREAM);
  }

  if(PeekOutputPic(&dec_cont->fb_list, picture)) {
    DEC_API_TRC("H264DecMCNextPicture# H264DEC_PIC_RDY\n");
    return (H264DEC_PIC_RDY);
  } else {
    DEC_API_TRC("H264DecMCNextPicture# H264DEC_OK\n");
    return (H264DEC_OK);
  }
}

#ifdef USE_EXTERNAL_BUFFER
H264DecRet H264DecMCGetBufferInfo(H264DecInst dec_inst, H264DecBufferInfo *mem_info) {
  H264DecRet ret;
  DEC_API_TRC("H264DecMCGetBufferInfo#\n");
  ret = H264DecGetBufferInfo(dec_inst, mem_info);
  if (ret < 0)
    DEC_API_TRC("H264DecMCGetBufferInfo# Error!\n");
  return ret;
}

H264DecRet H264DecMCAddBuffer(H264DecInst dec_inst, struct DWLLinearMem *info) {
  DEC_API_TRC("H264DecMCAddBuffer#\n");
  H264DecRet ret;
  ret = H264DecAddBuffer(dec_inst, info);
  if (ret < 0)
    DEC_API_TRC("H264DecMCAddBuffer# Error!\n");
  return ret;
}
#endif


#ifdef USE_OUTPUT_RELEASE
H264DecRet H264DecMCAbort(H264DecInst dec_inst) {
  DEC_API_TRC("H264DecMCAbort#\n");
  H264DecRet ret;
  ret = H264DecAbort(dec_inst);
  if (ret < 0)
    DEC_API_TRC("H264DecMCAbort# Error!\n");

  return ret;
}

H264DecRet H264DecMCAbortAfter(H264DecInst dec_inst) {
  DEC_API_TRC("H264DecMCAbortAfter#\n");
  H264DecRet ret;
  ret = H264DecAbortAfter(dec_inst);
  if (ret < 0)
    DEC_API_TRC("H264DecMCAbortAfter# Error!\n");

  return ret;
}
#endif


H264DecRet H264DecMCSetNoReorder(H264DecInst dec_inst, u32 no_output_reordering) {
  DEC_API_TRC("H264DecMCSetNoReorder#\n");
  if(dec_inst == NULL) {
    DEC_API_TRC("H264DecMCSetNoReorder# ERROR: dec_inst is NULL\n");
    return (H264DEC_PARAM_ERROR);
  }

  return H264DecSetNoReorder(dec_inst, no_output_reordering);
}



void h264MCPushOutputAll(decContainer_t *dec_cont) {
  u32 ret;
  H264DecPicture output;
  do {
    ret = H264DecMCNextPicture_INTERNAL(dec_cont, &output, 0);
  } while( ret == H264DEC_PIC_RDY );
}

void h264MCWaitOutFifoEmpty(decContainer_t *dec_cont) {
  WaitOutputEmpty(&dec_cont->fb_list);
}

void h264MCWaitPicReadyAll(decContainer_t *dec_cont) {
  WaitListNotInUse(&dec_cont->fb_list);
}

void h264MCSetRefPicStatus(volatile u8 *p_sync_mem, u32 is_field_pic,
                           u32 is_bottom_field) {
  if (is_field_pic == 0) {
    /* frame status */
    DWLmemset((void*)p_sync_mem, 0xFF, 32);
  } else if (is_bottom_field == 0) {
    /* top field status */
    DWLmemset((void*)p_sync_mem, 0xFF, 16);
  } else {
    /* bottom field status */
    p_sync_mem += 16;
    DWLmemset((void*)p_sync_mem, 0xFF, 16);
  }
}

static u32 MCGetRefPicStatus(const u8 *p_sync_mem, u32 is_field_pic,
                             u32 is_bottom_field) {
  u32 ret;

  if (is_field_pic == 0) {
    /* frame status */
    ret = ( p_sync_mem[0] << 8) + p_sync_mem[1];
  } else if (is_bottom_field == 0) {
    /* top field status */
    ret = ( p_sync_mem[0] << 8) + p_sync_mem[1];
  } else {
    /* bottom field status */
    p_sync_mem += 16;
    ret = ( p_sync_mem[0] << 8) + p_sync_mem[1];
  }
  return ret;
}

static void MCValidateRefPicStatus(const u32 *h264_regs,
                                   H264HwRdyCallbackArg *info) {
  const u8* p_ref_stat;
  const struct DWLLinearMem *p_out;
  const dpbStorage_t *dpb = info->current_dpb;
  u32 status, expected;

  p_out = (struct DWLLinearMem *)GetDataById(dpb->fb_list, info->out_id);

  p_ref_stat = (u8*)p_out->virtual_address + dpb->sync_mc_offset;

  status = MCGetRefPicStatus(p_ref_stat, info->is_field_pic, info->is_bottom_field);

  expected = GetDecRegister(h264_regs, HWIF_PIC_MB_HEIGHT_P);

  expected *= 16;

  if(info->is_field_pic)
    expected /= 2;

  if(status < expected) {
    ASSERT(status == expected);
    h264MCSetRefPicStatus((u8*)p_ref_stat,
                          info->is_field_pic, info->is_bottom_field);
  }
}

void h264MCHwRdyCallback(void *args, i32 core_id) {
  u32 dec_regs[DEC_X170_REGISTERS];

  decContainer_t *dec_cont = (decContainer_t *)args;
  H264HwRdyCallbackArg info;

  const void *dwl;
  const dpbStorage_t *dpb;

  u32 core_status, type;
  u32 i;
  struct DWLLinearMem *p_out;
  u32 num_concealed_mbs;

  ASSERT(dec_cont != NULL);
  ASSERT(core_id < MAX_ASIC_CORES);

  /* take a copy of the args as after we release the HW they
   * can be overwritten.
   */
  info = dec_cont->hw_rdy_callback_arg[core_id];

  dwl = dec_cont->dwl;
  dpb = info.current_dpb;

  /* read all hw regs */
  for (i = 1; i < DEC_X170_REGISTERS; i++) {
    dec_regs[i] = DWLReadReg(dwl, core_id, i * 4);
  }

  /* React to the HW return value */
  core_status = GetDecRegister(dec_regs, HWIF_DEC_IRQ_STAT);

  p_out = (struct DWLLinearMem *)GetDataById(dpb->fb_list,info.out_id);

  /* check if DEC_RDY, all other status are errors */
  if (core_status != DEC_8190_IRQ_RDY) {
#ifdef DEC_PRINT_BAD_IRQ
    fprintf(stderr, "\nCore %d \"bad\" IRQ = 0x%08x\n",
            core_id, core_status);
#endif

    /* reset HW if still enabled */
    if (core_status & DEC_8190_IRQ_BUFFER) {
      /*  reset HW; we don't want an IRQ after reset so disable it */
      DWLDisableHw(dwl, core_id, 0x04,
                   core_status | DEC_IRQ_DISABLE | DEC_ABORT);
    }

    /* reset DMV storage for erroneous pictures */
    {
      u32 dvm_mem_size = dec_cont->storage.pic_size_in_mbs * 64;
      u8 *dvm_base = (u8*)p_out->virtual_address;

      dvm_base += dpb->dir_mv_offset;

      if(info.is_field_pic) {
        dvm_mem_size /= 2;
        if(info.is_bottom_field)
          dvm_base += dvm_mem_size;
      }

      (void) DWLmemset(dvm_base, 0, dvm_mem_size);
    }

    h264MCSetRefPicStatus((u8*)p_out->virtual_address + dpb->sync_mc_offset,
                          info.is_field_pic, info.is_bottom_field);

    if (dec_cont->storage.partial_freeze == 1)
      num_concealed_mbs = GetPartialFreezePos((u8*)p_out->virtual_address,
                                              dec_cont->storage.curr_image[0].width,
                                              dec_cont->storage.curr_image[0].height);
    else
      num_concealed_mbs = dec_cont->storage.pic_size_in_mbs;

    /* mark corrupt picture in output queue */
    MarkOutputPicCorrupt(dpb->fb_list, info.out_id,
                         num_concealed_mbs);

    /* ... and in DPB */
    i = dpb->dpb_size + 1;
    while((i--) > 0) {
      dpbPicture_t *dpb_pic = (dpbPicture_t *)dpb->buffer + i;
      if(dpb_pic->data == p_out) {
        dpb_pic->num_err_mbs = num_concealed_mbs;
        break;
      }
    }
#ifdef USE_EC_MC
    dec_cont->storage.num_concealed_mbs = num_concealed_mbs;
#endif
  } else {
    MCValidateRefPicStatus(dec_regs, &info);
#ifdef USE_EC_MC
    num_concealed_mbs = 0;

    /* if current decoding frame's ref frame has some error,
              this decoding frame will also be treated as the errors from same position */
    for (i = 0; i < dpb->dpb_size; i++) {
      if ( dpb->buffer[dpb->list[i]].num_err_mbs ) {
        num_concealed_mbs = dpb->buffer[dpb->list[i]].num_err_mbs;
        break;
      }
    }

    if ( info.is_idr )
      num_concealed_mbs = 0;

    /* mark corrupt picture in output queue */
    MarkOutputPicCorrupt(dpb->fb_list, info.out_id, num_concealed_mbs);

    /* ... and in DPB */
    i = dpb->dpb_size + 1;
    while((i--) > 0) {
      dpbPicture_t *dpb_pic = (dpbPicture_t *)dpb->buffer + i;
      if(dpb_pic->data == p_out) {
        dpb_pic->num_err_mbs = num_concealed_mbs;
        break;
      }
    }
    pDecCont->storage.numConcealedMbs = numConcealedMbs;
#endif
  }

  /* clear IRQ status reg and release HW Core */
  DWLReleaseHw(dwl, info.core_id);

  H264UpdateAfterHwRdy(dec_cont, dec_regs);

  /* release the stream buffer. Callback provided by app */
  if(dec_cont->stream_consumed_callback.fn)
    dec_cont->stream_consumed_callback.fn((u8*)info.stream,
                                          (void*)info.p_user_data);

  if(info.is_field_pic) {
    if(info.is_bottom_field)
      type = FB_HW_OUT_FIELD_BOT;
    else
      type = FB_HW_OUT_FIELD_TOP;
  } else {
    type = FB_HW_OUT_FRAME;
  }

  ClearHWOutput(dpb->fb_list, info.out_id, type);

  /* decrement buffer usage in our buffer handling */
  for (i = 0; i < dpb->dpb_size; i++) {
    DecrementRefUsage(dpb->fb_list, info.ref_id[i]);
  }
}

void h264MCSetHwRdyCallback(decContainer_t *dec_cont) {
  dpbStorage_t *dpb = dec_cont->storage.dpb;
  u32 type, i;

  H264HwRdyCallbackArg *arg = &dec_cont->hw_rdy_callback_arg[dec_cont->core_id];

  arg->core_id = dec_cont->core_id;
  arg->stream = dec_cont->stream_consumed_callback.p_strm_buff;
  arg->p_user_data = dec_cont->stream_consumed_callback.p_user_data;
  arg->is_field_pic = dpb->current_out->is_field_pic;
  arg->is_bottom_field = dpb->current_out->is_bottom_field;
  arg->out_id = dpb->current_out->mem_idx;
  arg->current_dpb = dpb;
#ifdef USE_EC_MC
  arg->is_idr = IS_IDR_NAL_UNIT(dec_cont->storage.prev_nal_unit);
#endif

  for (i = 0; i < dpb->dpb_size; i++) {
    const struct DWLLinearMem *ref;
    ref = (struct DWLLinearMem *)GetDataById(&dec_cont->fb_list, dpb->ref_id[i]);

    ASSERT(ref->bus_address == (dec_cont->asic_buff->ref_pic_list[i] & (~3)));
    (void)ref;

    arg->ref_id[i] = dpb->ref_id[i];

  }

  DWLSetIRQCallback(dec_cont->dwl, dec_cont->core_id, h264MCHwRdyCallback,
                    dec_cont);

  if(arg->is_field_pic) {
    if(arg->is_bottom_field)
      type = FB_HW_OUT_FIELD_BOT;
    else
      type = FB_HW_OUT_FIELD_TOP;
  } else {
    type = FB_HW_OUT_FRAME;
  }

  MarkHWOutput(&dec_cont->fb_list, dpb->current_out->mem_idx, type);
}
