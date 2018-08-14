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

#include "vp8decapi.h"

#include "basetype.h"
#include "dwl.h"
#include "dwlthread.h"
#include "vp8decmc_internals.h"
#include "vp8hwd_asic.h"
#include "vp8hwd_container.h"
#include "vp8hwd_debug.h"
#include "vp8hwd_buffer_queue.h"


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

#define EOS_MARKER (-1)

static i32 FindIndex(VP8DecContainer_t* dec_cont, const u32* address);
static i32 NextOutput(VP8DecContainer_t *dec_cont);

/*------------------------------------------------------------------------------
    Function name   : VP8DecMCGetCoreCount
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
u32 VP8DecMCGetCoreCount(void) {
  ASSERT(0);
  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecMCInit
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
VP8DecRet VP8DecMCInit(VP8DecInst * dec_inst, const void *dwl, VP8DecMCConfig *p_mcinit_cfg) {
  *dec_inst = NULL;   /* return NULL instance for any error */
  if (p_mcinit_cfg == NULL) {
    return VP8DEC_PARAM_ERROR;
  }

  /* Call the common initialization first. */
  VP8DecRet ret = VP8DecInit(dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                             dwl,
#endif
                             VP8DEC_VP8,
                             1,
                             5,
                             DEC_REF_FRM_RASTER_SCAN, 0, 0);
  if (ret != VP8DEC_OK) {
    return ret;
  }
  /* Do the multicore specifics. */
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) *dec_inst;
  if (p_mcinit_cfg->stream_consumed_callback == NULL)
    return VP8DEC_PARAM_ERROR;
  if (FifoInit(VP8DEC_MAX_PIC_BUFFERS, &dec_cont->fifo_out) != FIFO_OK)
    return VP8DEC_MEMFAIL;

  if (FifoInit(VP8DEC_MAX_PIC_BUFFERS, &dec_cont->fifo_display) != FIFO_OK)
    return VP8DEC_MEMFAIL;

  dec_cont->num_cores = DWLReadAsicCoreCount();
  /* Enable multicore in the VP8 registers. */
  if(dec_cont->num_cores>1) {
    SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_MULTICORE_E,
                   dec_cont->intra_only ? 0 : 1);
    SetDecRegister(dec_cont->vp8_regs, HWIF_DEC_WRITESTAT_E,
                   dec_cont->intra_only ? 0 : 1);
  }
  dec_cont->stream_consumed_callback = p_mcinit_cfg->stream_consumed_callback;

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecMCDecode
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
    Argument        : const VP8DecInput *input
    Argument        : VP8DecOutput *output
------------------------------------------------------------------------------*/
VP8DecRet VP8DecMCDecode(VP8DecInst dec_inst,
                         const VP8DecInput *input,
                         VP8DecOutput *output) {
  VP8DecRet ret;
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;

  DEC_API_TRC("VP8DecMCDecode#\n");

  /* Check that function input parameters are valid */
  if(input == NULL || output == NULL || dec_inst == NULL) {
    DEC_API_TRC("VP8DecMCDecode# ERROR: NULL arg(s)\n");
    return (VP8DEC_PARAM_ERROR);
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecMCDecode# ERROR: Decoder not initialized\n");
    return (VP8DEC_NOT_INITIALIZED);
  }

  /* Currently we just call the single Core version, but we may change
   * our mind in the future. Do not call directly VP8DecDecode() in any
   * multicore application */
  ret = VP8DecDecode(dec_inst, input, output);

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecMCNextPicture
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
                      VP8DecPicture output
                      u32 endOfStream
------------------------------------------------------------------------------*/
VP8DecRet VP8DecMCNextPicture(VP8DecInst dec_inst,
                              VP8DecPicture * output) {
  i32 i;
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *) dec_inst;
  if(dec_inst == NULL || output == NULL) {
    DEC_API_TRC("VP8DecNextPicture# ERROR: dec_inst or output is NULL\n");
    return VP8DEC_PARAM_ERROR;
  }

  /* Check for valid decoder instance */
  if(dec_cont->checksum != dec_cont) {
    DEC_API_TRC("VP8DecNextPicture# ERROR: Decoder not initialized\n");
    return VP8DEC_NOT_INITIALIZED;
  }

  /*  NextOutput will block until there is an output. */
  i = NextOutput(dec_cont);
  if (i == EOS_MARKER) {
    DEC_API_TRC("VP8DecNextPicture# H264DEC_END_OF_STREAM\n");
    return VP8DEC_END_OF_STREAM;
  }

  ASSERT(i >= 0 && (u32)i < dec_cont->num_buffers);
  dec_cont->out_count--;
  *output = dec_cont->asic_buff->picture_info[i];
  output->pic_id = dec_cont->pic_number++;

  DEC_API_TRC("VP8DecNextPicture# VP8DEC_PIC_RDY\n");
  return VP8DEC_PIC_RDY;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecPictureConsumed
    Description     :
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
VP8DecRet VP8DecMCPictureConsumed(VP8DecInst dec_inst,
                                  const VP8DecPicture * picture) {
  if (dec_inst == NULL || picture == NULL) {
    return VP8DEC_PARAM_ERROR;
  }
  /* Remove the reference to the buffer. */
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;
  VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                             FindIndex(dec_cont, picture->p_output_frame));
  DEC_API_TRC("VP8DecMCPictureConsumed# VP8DEC_OK\n");
  return VP8DEC_OK;
}

/*------------------------------------------------------------------------------
    Function name   : VP8DecMCEndOfStream
    Description     : Used for signalling stream end from the input thread
    Return type     : VP8DecRet
    Argument        : VP8DecInst dec_inst
------------------------------------------------------------------------------*/
VP8DecRet VP8DecMCEndOfStream(VP8DecInst dec_inst) {
  if (dec_inst == NULL) {
    return VP8DEC_PARAM_ERROR;
  }
  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)dec_inst;

  /* Don't do end of stream twice. This is not thread-safe, so it must be
   * called from the single input thread that is also used to call
   * VP8DecDecode. */
  if (dec_cont->dec_stat == VP8DEC_END_OF_STREAM) {
    ASSERT(0);  /* Let the ASSERT kill the stuff in debug mode */
    return VP8DEC_END_OF_STREAM;
  }

  /* If buffer queue has been already initialized, we can use it to track
   * pending cores and outputs safely. */
  if (dec_cont->bq) {
    /* if the references and queue were already flushed, cannot do it again. */
    if(dec_cont->asic_buff->out_buffer_i != VP8_UNDEFINED_BUFFER) {
      /* Workaround for ref counting since this buffer is never used. */
      VP8HwdBufferQueueRemoveRef(dec_cont->bq,
                                 dec_cont->asic_buff->out_buffer_i);
      dec_cont->asic_buff->out_buffer_i = VP8_UNDEFINED_BUFFER;
      VP8HwdBufferQueueRemoveRef(
        dec_cont->bq, VP8HwdBufferQueueGetPrevRef(dec_cont->bq));
      VP8HwdBufferQueueRemoveRef(
        dec_cont->bq, VP8HwdBufferQueueGetGoldenRef(dec_cont->bq));
      VP8HwdBufferQueueRemoveRef(
        dec_cont->bq, VP8HwdBufferQueueGetAltRef(dec_cont->bq));
      VP8HwdBufferQueueWaitPending(dec_cont->bq);
    }
  }

  dec_cont->dec_stat = VP8DEC_END_OF_STREAM;
  FifoPush(dec_cont->fifo_out, EOS_MARKER, FIFO_EXCEPTION_DISABLE);
  return VP8DEC_OK;
}

/*------------------------------------------------------------------------------
    Function name   : NextOutput
    Description     : Find next picture in output order. If a fifo (fifo_display)
                      is used store outputs not yet ready display.
    Return type     : i32 - index to picture buffer
    Argument        : VP8DecContainer_t *
------------------------------------------------------------------------------*/
i32 NextOutput(VP8DecContainer_t *dec_cont) {
  i32 i;
  u32 j;
  i32 output_i = -1;
  u32 size;


  size = FifoCount(dec_cont->fifo_display);

  /* If there are pictures in the display reordering buffer, check them
   * first to see if our next output is there. */
  for(j=0; j<size; j++) {
    FifoPop(dec_cont->fifo_display, (FifoObject *)&i, FIFO_EXCEPTION_DISABLE);

    if(dec_cont->asic_buff->display_index[i] == dec_cont->pic_number) {
      /*  fifo_display had the right output. */
      output_i = i;
      break;
    } else {
      FifoPush(dec_cont->fifo_display, i, FIFO_EXCEPTION_DISABLE);
    }
  }

  /* Look for output in decode ordered outFifo. */
  while(output_i < 0) {
    /* Blocks until next output is available */
    FifoPop(dec_cont->fifo_out, (FifoObject *)&i, FIFO_EXCEPTION_DISABLE);
    if (i == EOS_MARKER)
      return i;

    if(dec_cont->asic_buff->display_index[i] == dec_cont->pic_number) {
      /*  fifo_out had the right output. */
      output_i = i;
    } else {
      /* Until we get the next picture in display order, push the outputs
      * to the display reordering fifo */
      FifoPush(dec_cont->fifo_display, (FifoObject)i, FIFO_EXCEPTION_DISABLE);
    }
  }

  return output_i;
}

#define HWIF_DEC_IRQ_REG      4

void VP8DecIRQCallbackFn(void* arg, i32 core_id) {
  u32 dec_regs[DEC_X170_REGISTERS];

  VP8DecContainer_t *dec_cont = (VP8DecContainer_t *)arg;
  HwRdyCallbackArg info;
  const void *dwl;

  u32 core_status;
  i32 i;

  ASSERT(dec_cont != NULL);
  ASSERT(core_id < MAX_ASIC_CORES);

  info = dec_cont->hw_rdy_callback_arg[core_id];

  dwl = dec_cont->dwl;

  /* read all hw regs */
  for (i = 1; i < DEC_X170_REGISTERS; i++) {
    dec_regs[i] = DWLReadReg(dwl, core_id, i * 4);
  }

  /* React to the HW return value */
  core_status = GetDecRegister(dec_regs, HWIF_DEC_IRQ_STAT);

  /* check if DEC_RDY, all other status are errors */
  if (core_status != DEC_8190_IRQ_RDY) {
    /* TODO(vmr): Handle unrecoverable system errors properly. */

    /* reset HW if still enabled */
    if (core_status & 0x01)
      DWLDisableHw(dwl, info.core_id, 0x04, 0);
    if(dec_cont->num_cores > 1) {
      /* Fake reference availability to allow pending cores to go. */
      info.p_ref_status[0] = 0xFF;
      info.p_ref_status[1] = 0xFF;
    }
  }
  /* Remove references this decoded picture needed. */
  VP8HwdBufferQueueRemoveRef(info.bq, info.index_p);
  VP8HwdBufferQueueRemoveRef(info.bq, info.index_a);
  VP8HwdBufferQueueRemoveRef(info.bq, info.index_g);
  DWLReleaseHw(dwl, info.core_id);

  /* Let app know we're done with the buffer. */
  ASSERT(info.stream_consumed_callback);
  info.stream_consumed_callback((u8*)info.stream, info.p_user_data);

  /* If frame is to be outputted, fill in the information before pushing to
   * output fifo. */
  info.pic.nbr_of_err_mbs = 0; /* TODO(vmr): Fill correctly. */
  dec_cont->asic_buff->picture_info[info.index] = info.pic;
  if(info.show_frame) {
    FifoPush(info.fifo_out, info.index, FIFO_EXCEPTION_DISABLE);
  }
}


void VP8MCSetHwRdyCallback(VP8DecContainer_t  *dec_cont) {
  HwRdyCallbackArg *args = &dec_cont->hw_rdy_callback_arg[dec_cont->core_id];
  args->dec_cont = dec_cont;
  args->core_id = dec_cont->core_id;
  args->bq = dec_cont->bq;
  args->index = dec_cont->asic_buff->out_buffer_i;
  args->index_p = VP8HwdBufferQueueGetPrevRef(dec_cont->bq);
  args->index_a = VP8HwdBufferQueueGetAltRef(dec_cont->bq);
  args->index_g = VP8HwdBufferQueueGetGoldenRef(dec_cont->bq);
  args->stream = dec_cont->stream;
  args->p_user_data = dec_cont->p_user_data;
  args->stream_consumed_callback = dec_cont->stream_consumed_callback;
  args->fifo_out = dec_cont->fifo_out;
  if (dec_cont->decoder.show_frame) {
    dec_cont->asic_buff->display_index[args->index] =
      dec_cont->display_number++;
  }
  args->show_frame = dec_cont->decoder.show_frame;
  args->p_ref_status = (u8 *)VP8HwdRefStatusAddress(dec_cont);
  /* Fill in the picture information for everything we know. */
  args->pic.is_intra_frame = dec_cont->decoder.key_frame;
  args->pic.is_golden_frame = (dec_cont->decoder.refresh_golden ||
                               dec_cont->decoder.copy_buffer_to_golden) ? 1 : 0;
  /* Frame size and format information. */
  args->pic.frame_width = (dec_cont->width + 15) & ~15;
  args->pic.frame_height = (dec_cont->height + 15) & ~15;
  args->pic.coded_width = dec_cont->width;
  args->pic.coded_height = dec_cont->height;
  args->pic.luma_stride = dec_cont->asic_buff->luma_stride ?
                          dec_cont->asic_buff->luma_stride : dec_cont->asic_buff->width;
  args->pic.chroma_stride = dec_cont->asic_buff->chroma_stride ?
                            dec_cont->asic_buff->chroma_stride : dec_cont->asic_buff->width;
  args->pic.output_format = dec_cont->tiled_reference_enable ?
                            DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;
  /* Buffer addresses. */
  const struct DWLLinearMem *out_pic = &dec_cont->asic_buff->pictures[args->index];
  const struct DWLLinearMem *out_pic_c = &dec_cont->asic_buff->pictures_c[args->index];
  args->pic.p_output_frame = out_pic->virtual_address;
  args->pic.output_frame_bus_address = out_pic->bus_address;
  if(dec_cont->asic_buff->strides_used || dec_cont->asic_buff->custom_buffers) {
    args->pic.p_output_frame_c = out_pic_c->virtual_address;
    args->pic.output_frame_bus_address_c = out_pic_c->bus_address;
  } else {
    u32 chroma_buf_offset = args->pic.frame_width  * args->pic.frame_height;
    args->pic.p_output_frame_c = args->pic.p_output_frame + chroma_buf_offset / 4;
    args->pic.output_frame_bus_address_c = args->pic.output_frame_bus_address +
                                           chroma_buf_offset;
  }
  /* Finally, set the information we don't know yet to 0. */
  args->pic.nbr_of_err_mbs = 0;  /* To be set after decoding. */
  args->pic.pic_id = 0;  /* To be set after output reordering. */
  DWLSetIRQCallback(dec_cont->dwl, dec_cont->core_id,
                    VP8DecIRQCallbackFn, dec_cont);
}


static i32 FindIndex(VP8DecContainer_t* dec_cont, const u32* address) {
  i32 i;
  for (i = 0; i < (i32)dec_cont->num_buffers; i++)
    if (dec_cont->asic_buff->pictures[i].virtual_address == address)
      break;
  ASSERT((u32)i < dec_cont->num_buffers);
  return i;
}
