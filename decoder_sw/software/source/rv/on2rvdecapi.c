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

#include "rvdecapi.h"
#include "rv_container.h"
#include "on2rvdecapi.h"
#include "regdrv_g1.h"

#define GET_MAJOR_VER(ver) ((ver>>28)&0x0F)
#define GET_MINOR_VER(ver) ((ver>>20)&0xFF)
#define MAX_NUM_RPR_SIZES 8

On2RvDecRet On2RvDecInit(void *p_rv10_init,
#ifdef USE_EXTERNAL_BUFFER
                         const void *dwl,
#endif
                         void **decoder_state,
                         struct DecDownscaleCfg *dscale_cfg) {

  RvDecRet ret;
  u32 major, minor;
  u32 is_rv8;

  On2DecoderInit *p_init = (On2DecoderInit *)p_rv10_init;

  if (!p_rv10_init || !decoder_state)
    return ON2RVDEC_POINTER;

  major = GET_MAJOR_VER(p_init->ul_stream_version);
  minor = GET_MINOR_VER(p_init->ul_stream_version);

  /* check that streams is either RV8 or RV9 */
  if (!(major == 4 && minor == 0) && !(major == 3 && minor == 2))
    return ON2RVDEC_INVALID_PARAMETER;

  is_rv8 = (major == 3) && (minor == 2);

  ret = RvDecInit((RvDecInst*)decoder_state,
#ifdef USE_EXTERNAL_BUFFER
                  dwl,
#endif
                  0, /* useVideoFreezeConcealment */
                  0,
                  NULL,
                  is_rv8 ? 0 : 1,
                  p_init->pels,
                  p_init->lines,
                  0,
                  DEC_REF_FRM_RASTER_SCAN, 0, 0, dscale_cfg);

  switch (ret) {
  case RVDEC_OK:
    return ON2RVDEC_OK;

  case RVDEC_PARAM_ERROR:
    return ON2RVDEC_INVALID_PARAMETER;

  case RVDEC_MEMFAIL:
    return ON2RVDEC_OUTOFMEMORY;

  default:
    return ON2RVDEC_FAIL;
  }

}

On2RvDecRet On2RvDecFree(void *global) {

  RvDecRelease((RvDecInst)global);

  return ON2RVDEC_OK;

}

On2RvDecRet On2RvDecDecode(u8 *p_rv10_packets, u8 *p_decoded_frame_buffer,
                           void *p_input_params, void *p_output_params, void *global) {

  u32 i;
  DecContainer *dec_cont;
  On2DecoderOutParams *out;
  On2DecoderInParams *in;
  RvDecInput rv_in;
  RvDecOutput rv_out;
  RvDecPicture rv_pic;
  RvDecSliceInfo slice_info[128];
  RvDecRet ret;
  u32 more = 0;
  UNUSED(p_decoded_frame_buffer);

  if (!p_rv10_packets || !p_input_params || !global)
    return ON2RVDEC_POINTER;

  dec_cont = (DecContainer*)global;
  if (dec_cont->StrmStorage.is_rv8 &&
      dec_cont->StrmStorage.frame_code_length == 0)
    return ON2RVDEC_FAIL;

  in = (On2DecoderInParams*)p_input_params;
  out = (On2DecoderOutParams*)p_output_params;

  rv_in.stream = (u8*)p_rv10_packets;
  rv_in.stream_bus_address = in->stream_bus_addr;
  rv_in.data_len = in->data_length;
  rv_in.timestamp = in->timestamp;
  rv_in.pic_id = in->timestamp;
  rv_in.slice_info_num = in->num_data_segments;
  rv_in.slice_info = slice_info;
  rv_in.skip_non_reference = in->skip_non_reference;
  for (i = 0; i < rv_in.slice_info_num; i++) {
    rv_in.slice_info[i].offset = in->p_data_segments[i].ul_segment_offset;
    rv_in.slice_info[i].is_valid = in->p_data_segments[i].b_is_valid;
  }

  out->num_frames = 0;
  out->notes = 0;
  out->timestamp = 0;

  if ((in->flags & ON2RV_DECODE_MORE_FRAMES) ||
      (in->flags & ON2RV_DECODE_LAST_FRAME)) {
#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->pp_instance == NULL) {
      RvDecEndOfStream(dec_cont, (in->flags & ON2RV_DECODE_LAST_FRAME));
      return ON2RVDEC_OK;
    } else
      ret = RvDecNextPicture((RvDecInst)global, &rv_pic,
                             (in->flags & ON2RV_DECODE_LAST_FRAME));
#else
    ret = RvDecNextPicture((RvDecInst)global, &rv_pic,
                           (in->flags & ON2RV_DECODE_LAST_FRAME));
#endif
    if (ret == RVDEC_PIC_RDY && !(in->flags & ON2RV_DECODE_DONT_DRAW)) {
      out->num_frames = 1;
      out->width = rv_pic.coded_width;
      out->height = rv_pic.coded_height;
      out->p_out_frame = rv_pic.output_picture;
      if (rv_pic.key_picture)
        out->notes |= ON2RV_DECODE_KEY_FRAME;
      out->timestamp = rv_pic.pic_id;
      out->output_format = rv_pic.output_format;
    }
    return ON2RVDEC_OK;
  }

  do {
    more = 0;
    ret = RvDecDecode((RvDecInst)global, &rv_in, &rv_out);

    switch (ret) {
    case RVDEC_HDRS_RDY:
#ifdef USE_EXTERNAL_BUFFER
      return ON2RVDEC_HDRS_RDY;
#else
      more = 1;
      break;
#endif

#ifdef USE_EXTERNAL_BUFFER
    case RVDEC_WAITING_FOR_BUFFER:
      return ON2RVDEC_WAITING_BUFFER;
#endif
    case RVDEC_OK:
    case RVDEC_PIC_DECODED:
    case RVDEC_STRM_PROCESSED:
    case RVDEC_BUF_EMPTY:
    case RVDEC_NONREF_PIC_SKIPPED:
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp_instance == NULL)
        return ON2RVDEC_OK;
      else
        ret = RvDecNextPicture((RvDecInst)global, &rv_pic, 0);
#else
      ret = RvDecNextPicture((RvDecInst)global, &rv_pic, 0);
#endif
      if (ret == RVDEC_PIC_RDY && !(in->flags & ON2RV_DECODE_DONT_DRAW)) {
        out->num_frames = 1;
        out->width = rv_pic.coded_width;
        out->height = rv_pic.coded_height;
        out->p_out_frame = rv_pic.output_picture;
        if (rv_pic.key_picture)
          out->notes |= ON2RV_DECODE_KEY_FRAME;
        out->timestamp = rv_pic.pic_id;
        out->output_format = rv_pic.output_format;
      }
      break;

    default:
      return ON2RVDEC_FAIL;
    }

  } while (more);

  return ON2RVDEC_OK;

}

On2RvDecRet On2RvDecCustomMessage(void *msg_id, void *global) {

  u32 i;
  u32 tmp;
  DecContainer *dec_cont;
  On2RvCustomMessage_ID *p_id = (On2RvCustomMessage_ID*)msg_id;
  On2RvMsgSetDecoderRprSizes *msg;

  if (!msg_id || !global)
    return ON2RVDEC_POINTER;

  if (*p_id != ON2RV_MSG_ID_Set_RVDecoder_RPR_Sizes)
    return ON2RVDEC_NOTIMPL;

  dec_cont = (DecContainer*)global;

  msg = (On2RvMsgSetDecoderRprSizes *)msg_id;
  if (msg->num_sizes > MAX_NUM_RPR_SIZES)
    return ON2RVDEC_FAIL;

  tmp = 1;
  while (msg->num_sizes > (u32)(1<<tmp)) tmp++;

  dec_cont->StrmStorage.frame_code_length = tmp;

  for (i = 0; i < 2*msg->num_sizes; i++)
    dec_cont->StrmStorage.frame_sizes[i] = msg->sizes[i];

  SetDecRegister(dec_cont->rv_regs, HWIF_FRAMENUM_LEN, tmp);

  return ON2RVDEC_OK;
}

On2RvDecRet On2RvDecHiveMessage(void *msg, void *global) {
  UNUSED(msg);
  UNUSED(global);
  return ON2RVDEC_NOTIMPL;
}

On2RvDecRet On2RvDecPeek(void *p_output_params, void *global) {

  On2DecoderOutParams *out;
  DecContainer *dec_cont;

  if (!p_output_params || !global)
    return ON2RVDEC_POINTER;

  dec_cont = (DecContainer *) global;

  /* Check if decoder is in an incorrect mode */
  if(dec_cont->ApiStorage.DecStat == UNINIT)
    return ON2RVDEC_FAIL;

  out = (On2DecoderOutParams*)p_output_params;

  /* Nothing to send out */
  if(dec_cont->StrmStorage.out_count == 0) {
    (void) DWLmemset(out, 0, sizeof(On2DecoderOutParams));
  } else {
    picture_t *p_pic;

    p_pic = (picture_t *) dec_cont->StrmStorage.p_pic_buf +
            dec_cont->StrmStorage.work_out;

    out->num_frames = 1;
    out->notes = 0;
    out->width = p_pic->coded_width;
    out->height = p_pic->coded_height;
    out->timestamp = p_pic->pic_id;
    out->p_out_frame = (u8*)p_pic->data.virtual_address;
    out->output_format = p_pic->tiled_mode ?
                         DEC_OUT_FRM_TILED_8X4 : DEC_OUT_FRM_RASTER_SCAN;
  }

  return ON2RVDEC_OK;

}

On2RvDecRet On2RvDecSetNbrOfBuffers( u32 nbr_buffers, void *global ) {

  DecContainer *dec_cont;

  if (!global)
    return ON2RVDEC_POINTER;

  dec_cont = (DecContainer *) global;

  /* Check if decoder is in an incorrect mode */
  if(dec_cont->ApiStorage.DecStat == UNINIT)
    return ON2RVDEC_FAIL;

  if(!nbr_buffers)
    return ON2RVDEC_INVALID_PARAMETER;

  /* If buffers have already been allocated, return FAIL */
  if(dec_cont->StrmStorage.num_buffers ||
      dec_cont->StrmStorage.num_pp_buffers )
    return ON2RVDEC_FAIL;

  dec_cont->StrmStorage.max_num_buffers = nbr_buffers;

  return ON2RVDEC_OK;

}

On2RvDecRet On2RvDecSetReferenceFrameFormat(
  On2RvRefFrmFormat reference_frame_format, void *global ) {

  DecContainer *dec_cont;
  DWLHwConfig config;

  if (!global)
    return ON2RVDEC_POINTER;

  dec_cont = (DecContainer *) global;

  /* Check if decoder is in an incorrect mode */
  if(dec_cont->ApiStorage.DecStat == UNINIT)
    return ON2RVDEC_FAIL;

  if(reference_frame_format != ON2RV_REF_FRM_RASTER_SCAN &&
      reference_frame_format != ON2RV_REF_FRM_TILED_DEFAULT)
    return ON2RVDEC_INVALID_PARAMETER;

  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_RV_DEC);

  if(reference_frame_format == ON2RV_REF_FRM_TILED_DEFAULT) {
    /* Assert support in HW before enabling.. */
    if(!config.tiled_mode_support) {
      return ON2RVDEC_FAIL;
    }
    dec_cont->tiled_mode_support = config.tiled_mode_support;
  } else
    dec_cont->tiled_mode_support = 0;


  return ON2RVDEC_OK;

}

#ifdef USE_EXTERNAL_BUFFER
On2RvDecRet On2RvDecGetInfo(On2DecoderInfo *dec_info, void *global) {
  DecContainer  *dec_cont = (DecContainer *)global;
  RvDecRet ret;
  RvDecInfo rv_dec_info;

  ret = RvDecGetInfo(dec_cont, &rv_dec_info);

  if(ret == RVDEC_PARAM_ERROR || ret == RVDEC_HDRS_NOT_RDY)
    return ON2RVDEC_INVALID_PARAMETER;

  if(ret == RVDEC_OK) {
    dec_info->frame_width = rv_dec_info.frame_width;
    dec_info->frame_height = rv_dec_info.frame_height;
    dec_info->pic_buff_size = rv_dec_info.pic_buff_size;
    return ON2RVDEC_OK;
  }

  return ON2RVDEC_FAIL;
}

On2RvDecRet On2RvDecGetBufferInfo(On2DecoderBufferInfo *mem_info, void *global) {
  DecContainer  *dec_cont = (DecContainer *)global;
  RvDecRet ret;
  RvDecBufferInfo buf_info;

  ret = RvDecGetBufferInfo(dec_cont, &buf_info);

  if(ret == RVDEC_PARAM_ERROR)
    return ON2RVDEC_INVALID_PARAMETER;

  if(ret == RVDEC_OK) {
    mem_info->buf_to_free = buf_info.buf_to_free;
    mem_info->next_buf_size = buf_info.next_buf_size;
    mem_info->buf_num = buf_info.buf_num;
    return ON2RVDEC_OK;
  }

  if(ret == RVDEC_WAITING_FOR_BUFFER) {
    mem_info->buf_to_free = buf_info.buf_to_free;
    mem_info->next_buf_size = buf_info.next_buf_size;
    mem_info->buf_num = buf_info.buf_num;
    return ON2RVDEC_WAITING_BUFFER;
  }

  return ON2RVDEC_FAIL;
}

On2RvDecRet On2RvDecAddBuffer(struct DWLLinearMem *info, void *global) {
  DecContainer  *dec_cont = (DecContainer *)global;
  RvDecRet ret;

  ret = RvDecAddBuffer(dec_cont, info);

  if(ret == RVDEC_PARAM_ERROR)
    return ON2RVDEC_INVALID_PARAMETER;
  if(ret == RVDEC_OK)
    return ON2RVDEC_OK;
  if(ret == RVDEC_WAITING_FOR_BUFFER)
    return ON2RVDEC_WAITING_BUFFER;
  if(ret == RVDEC_EXT_BUFFER_REJECTED)
    return ON2RVDEC_EXT_BUFFER_REJECTED;

  return ON2RVDEC_FAIL;
}
#endif

#ifdef USE_OUTPUT_RELEASE
On2RvDecRet On2RvDecNextPicture(void *p_output_params, void *global) {

  On2DecoderOutParams *out;
  RvDecPicture rv_pic;
  DecContainer *dec_cont;
  RvDecRet ret;

  if (!p_output_params || !global)
    return ON2RVDEC_POINTER;

  dec_cont = (DecContainer *) global;

  /* Check if decoder is in an incorrect mode */
  if(dec_cont->ApiStorage.DecStat == UNINIT)
    return ON2RVDEC_FAIL;

  out = (On2DecoderOutParams*)p_output_params;

  ret = RvDecNextPicture(dec_cont, &rv_pic, 0);

  if (ret == RVDEC_PIC_RDY) {
    out->num_frames = 1;
    out->width = rv_pic.coded_width;
    out->height = rv_pic.coded_height;
    out->p_out_frame = rv_pic.output_picture;
    if (rv_pic.key_picture)
      out->notes |= ON2RV_DECODE_KEY_FRAME;
    out->timestamp = rv_pic.pic_id;
    out->output_format = rv_pic.output_format;
    out->out_bus_addr = rv_pic.output_picture_bus_address;
    return ON2RVDEC_OK;
  } else if(ret == RVDEC_END_OF_STREAM)
    return ON2RVDEC_END_OF_STREAM;

  return (ON2RVDEC_FAIL);
}

On2RvDecRet On2RvDecPictureConsumed(void *p_output_params, void *global) {
  /* Variables */
  On2DecoderOutParams *out;
  DecContainer *dec_cont;
  u32 i=0;

  if (!p_output_params || !global)
    return ON2RVDEC_POINTER;

  dec_cont = (DecContainer *) global;

  /* Check if decoder is in an incorrect mode */
  if(dec_cont->ApiStorage.DecStat == UNINIT)
    return ON2RVDEC_FAIL;

  out = (On2DecoderOutParams*)p_output_params;

  if (!dec_cont->pp_enabled) {
    for(i = 0; i < dec_cont->StrmStorage.num_buffers; i++) {
      if(out->out_bus_addr == dec_cont->StrmStorage.p_pic_buf[i].data.bus_address) {

        if(dec_cont->pp_instance == NULL) {
          BqueuePictureRelease(&dec_cont->StrmStorage.bq, i);
        }
        return (ON2RVDEC_OK);
      }
    }
  } else {
    InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->StrmStorage.p_pic_buf[i].pp_data->virtual_address);
    return (ON2RVDEC_OK);
  }
  return (ON2RVDEC_FAIL);
}
#endif
