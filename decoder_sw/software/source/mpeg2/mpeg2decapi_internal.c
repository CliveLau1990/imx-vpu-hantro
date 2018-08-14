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
#include "mpeg2hwd_container.h"
#include "mpeg2hwd_cfg.h"
#include "mpeg2decapi.h"
#include "mpeg2hwd_debug.h"
#include "mpeg2hwd_utils.h"
#include "mpeg2decapi_internal.h"
#include "regdrv_g1.h"
#ifdef MPEG2_ASIC_TRACE
#include "mpeg2asicdbgtrace.h"
#endif

#include "bqueue.h"
#include "input_queue.h"

/*------------------------------------------------------------------------------

    5.1 Function name:  mpeg2_api_init_data_structures()

        Purpose:        Initialize Data Structures in DecContainer.

        Input:          DecContainer *dec_cont

        Output:         u32

------------------------------------------------------------------------------*/
void mpeg2_api_init_data_structures(DecContainer * dec_cont) {
  /*
   *  have to be initialized into 1 to enable
   *  decoding stream without VO-headers
   */
  dec_cont->Hdrs.video_format = 5;
  dec_cont->Hdrs.transfer_characteristics = 1;
  dec_cont->Hdrs.matrix_coefficients = 1;
  dec_cont->Hdrs.progressive_sequence = 1;
  dec_cont->Hdrs.picture_structure = 3;
  dec_cont->Hdrs.frame_pred_frame_dct = 1;
  dec_cont->Hdrs.first_field_in_frame = 0;
  dec_cont->Hdrs.field_index = -1;
  dec_cont->Hdrs.field_out_index = 1;
  dec_cont->Hdrs.video_range = 0;
  dec_cont->StrmStorage.vp_qp = 1;
  dec_cont->ApiStorage.first_headers = 1;
  dec_cont->StrmStorage.work_out = 0;
  dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work1 =
                                  INVALID_ANCHOR_PICTURE;
  dec_cont->StrmStorage.error_in_hdr = 0;
}

/*------------------------------------------------------------------------------

    5.5 Function name:  mpeg2DecTimeCode();

        Purpose:        Write time data to output

        Input:          DecContainer *dec_cont, Mpeg2DecTime *time_code

        Output:         void

------------------------------------------------------------------------------*/

void mpeg2DecTimeCode(DecContainer * dec_cont, Mpeg2DecTime * time_code) {

#define DEC_FRAMED dec_cont->FrameDesc
#define DEC_HDRS dec_cont->Hdrs
#define DEC_STST dec_cont->StrmStorage
#define DEC_SVDS dec_cont->SvDesc

  ASSERT(dec_cont);
  ASSERT(time_code);

  time_code->hours = DEC_FRAMED.time_code_hours;
  time_code->minutes = DEC_FRAMED.time_code_minutes;
  time_code->seconds = DEC_FRAMED.time_code_seconds;
  time_code->pictures = DEC_FRAMED.frame_time_pictures;

#undef DEC_FRAMED
#undef DEC_HDRS
#undef DEC_STST
#undef DEC_SVDS

}

/*------------------------------------------------------------------------------

    x.x Function name:  mpeg2AllocateBuffers

        Purpose:        Allocate memory

        Input:          DecContainer *dec_cont

        Output:         MPEG2DEC_MEMFAIL/MPEG2DEC_OK

------------------------------------------------------------------------------*/
Mpeg2DecRet mpeg2AllocateBuffers(DecContainer * dec_cont) {
#define DEC_FRAMED dec_cont->FrameDesc

  u32 i;
  i32 ret = 0;
  u32 size_tmp = 0;
  u32 buffers = 0;

  ASSERT(DEC_FRAMED.total_mb_in_frame != 0);

  /* allocate Q-table buffer */
  ret |= DWLMallocLinear(dec_cont->dwl, MPEG2DEC_TABLE_SIZE,
                         &dec_cont->ApiStorage.p_qtable_base);
  if(ret)
    return (MPEG2DEC_MEMFAIL);

  MPEG2DEC_DEBUG(("Q-table: %x, %x\n",
                  (u32) dec_cont->ApiStorage.p_qtable_base.virtual_address,
                  dec_cont->ApiStorage.p_qtable_base.bus_address));

  /* Reference images */
  if(!dec_cont->ApiStorage.external_buffers) {
    size_tmp =
      (MPEG2API_DEC_FRAME_BUFF_SIZE * DEC_FRAMED.total_mb_in_frame * 4);

    /* Calculate minimum amount of buffers */
#ifndef USE_EXTERNAL_BUFFER
    buffers = dec_cont->Hdrs.low_delay ? 2 : 3;
#else
    buffers = 3;
#endif
    dec_cont->StrmStorage.parallel_mode2 = 0;
    dec_cont->pp_control.prev_anchor_display_index = BQUEUE_UNUSED;
    if( dec_cont->pp_instance ) { /* Combined mode used */
      /* If low-delay not set, then we might need extra buffers in
       * case we're using multibuffer mode. */
#ifndef USE_EXTERNAL_BUFFER
      if(!dec_cont->Hdrs.low_delay)
#endif
      {
        dec_cont->pp_config_query.tiled_mode =
          dec_cont->tiled_reference_enable;
        dec_cont->PPConfigQuery(dec_cont->pp_instance,
                                &dec_cont->pp_config_query);
        if(dec_cont->pp_config_query.multi_buffer)
          buffers = 4;
      }

      dec_cont->StrmStorage.num_pp_buffers = dec_cont->StrmStorage.max_num_buffers;
      dec_cont->StrmStorage.num_buffers = buffers; /* Use bare minimum in decoder */
      /*buffers =  dec_cont->Hdrs.interlaced ? 1 : 2;*/
      buffers = 2;
      if( dec_cont->StrmStorage.num_pp_buffers < buffers )
        dec_cont->StrmStorage.num_pp_buffers = buffers;
    } else { /* Dec only or separate PP */
      dec_cont->StrmStorage.num_buffers = dec_cont->StrmStorage.max_num_buffers;
      dec_cont->StrmStorage.num_pp_buffers = 0;
      if( dec_cont->StrmStorage.num_buffers < buffers )
        dec_cont->StrmStorage.num_buffers = buffers;
    }
#ifdef USE_OUTPUT_RELEASE
    ret = BqueueInit2(&dec_cont->StrmStorage.bq,
                      dec_cont->StrmStorage.num_buffers );
#else
    ret = BqueueInit(&dec_cont->StrmStorage.bq,
                     dec_cont->StrmStorage.num_buffers );
#endif
    if( ret != HANTRO_OK )
      return MPEG2DEC_MEMFAIL;

    ret = BqueueInit(&dec_cont->StrmStorage.bq_pp,
                     dec_cont->StrmStorage.num_pp_buffers );
    if( ret != HANTRO_OK )
      return MPEG2DEC_MEMFAIL;
#ifndef USE_EXTERNAL_BUFFER
    for(i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
      ret |= DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                             &dec_cont->StrmStorage.p_pic_buf[i].data);

      MPEG2DEC_DEBUG(("PicBuffer[%d]: %x, %x\n",
                      i,
                      (u32) dec_cont->StrmStorage.p_pic_buf[i].data.
                      virtual_address,
                      dec_cont->StrmStorage.p_pic_buf[i].data.bus_address));

      if(dec_cont->StrmStorage.p_pic_buf[i].data.bus_address == 0) {
        return (MPEG2DEC_MEMFAIL);
      }
      if (dec_cont->pp_enabled) {
        /* Add PP output buffers. */
        struct DWLLinearMem pp_buffer;
        u32 pp_width, pp_height, pp_stride, pp_buff_size;

        pp_width = (dec_cont->FrameDesc.frame_width * 16) >> dec_cont->dscale_shift_x;
        pp_height = (dec_cont->FrameDesc.frame_height * 16) >> dec_cont->dscale_shift_y;
        pp_stride = ((pp_width + 15) >> 4) << 4;
        pp_buff_size = pp_stride * pp_height * 3 / 2;
        if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
          return (MPEG2DEC_MEMFAIL);

        dec_cont->StrmStorage.pp_buffer[i] = pp_buffer;
        InputQueueAddBuffer(dec_cont->pp_buffer_queue, &pp_buffer);
      }
    }
    /* initialize first picture buffer (work_out is 1 for the first picture)
     * grey, may be used as reference in certain error cases */
    (void) DWLmemset(dec_cont->StrmStorage.p_pic_buf[1].data.virtual_address,
                     128, 384 * dec_cont->FrameDesc.total_mb_in_frame);
#else
    if (dec_cont->pp_enabled) {
      for(i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
        ret |= DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                               &dec_cont->StrmStorage.p_pic_buf[i].data);

        MPEG2DEC_DEBUG(("PicBuffer[%d]: %x, %x\n",
                        i,
                        (u32) dec_cont->StrmStorage.p_pic_buf[i].data.
                        virtual_address,
                        dec_cont->StrmStorage.p_pic_buf[i].data.bus_address));

        if(dec_cont->StrmStorage.p_pic_buf[i].data.bus_address == 0) {
          return (MPEG2DEC_MEMFAIL);
        }
      }
      /* initialize first picture buffer (work_out is 1 for the first picture)
       * grey, may be used as reference in certain error cases */
      (void) DWLmemset(dec_cont->StrmStorage.p_pic_buf[1].data.virtual_address,
                       128, 384 * dec_cont->FrameDesc.total_mb_in_frame);
    }
#endif
  }

  if(ret) {
    return (MPEG2DEC_MEMFAIL);
  }

  /*
   *  dec_cont->MbSetDesc
   */

  return MPEG2DEC_OK;

#undef DEC_FRAMED
}

/*------------------------------------------------------------------------------

    x.x Function name:  mpeg2DecAllocExtraBPic

        Purpose:        Allocate memory

        Input:          DecContainer *dec_cont

        Output:         MPEG2DEC_MEMFAIL/MPEG2DEC_OK

------------------------------------------------------------------------------*/
Mpeg2DecRet mpeg2DecAllocExtraBPic(DecContainer * dec_cont) {
  i32 ret = 0;
  u32 size_tmp = 0;
  u32 extra_buffer = 0;

  /* If we already have enough buffers, do nothing. */
  if( dec_cont->StrmStorage.num_buffers >= 3)
    return MPEG2DEC_OK;

  dec_cont->StrmStorage.num_buffers = 3;
  /* We need one more buffer if using PP multibuffer */
  if(dec_cont->pp_instance != NULL) {
    dec_cont->pp_config_query.tiled_mode =
      dec_cont->tiled_reference_enable;
    dec_cont->PPConfigQuery(dec_cont->pp_instance,
                            &dec_cont->pp_config_query);
    if(dec_cont->pp_config_query.multi_buffer) {
      dec_cont->StrmStorage.num_buffers = 4;
      extra_buffer = 1;
    }
  }

  size_tmp =
    (MPEG2API_DEC_FRAME_BUFF_SIZE * dec_cont->FrameDesc.total_mb_in_frame * 4);
#ifndef USE_OUTPUT_RELEASE
  BqueueRelease(&dec_cont->StrmStorage.bq);
  ret = BqueueInit(&dec_cont->StrmStorage.bq,
                   dec_cont->StrmStorage.num_buffers );
#else
  BqueueRelease2(&dec_cont->StrmStorage.bq);
  ret = BqueueInit2(&dec_cont->StrmStorage.bq,
                    dec_cont->StrmStorage.num_buffers );
#endif
  if(ret != HANTRO_OK)
    return (MPEG2DEC_MEMFAIL);

  ret = DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                        &dec_cont->StrmStorage.p_pic_buf[2].data);
  if(dec_cont->StrmStorage.p_pic_buf[2].data.bus_address == 0 || ret) {
    return (MPEG2DEC_MEMFAIL);
  }
  if (dec_cont->pp_enabled) {
    /* Add PP output buffers. */
    struct DWLLinearMem pp_buffer;
    u32 pp_width, pp_height, pp_stride, pp_buff_size;

    pp_width = (dec_cont->FrameDesc.frame_width * 16) >> dec_cont->dscale_shift_x;
    pp_height = (dec_cont->FrameDesc.frame_height * 16) >> dec_cont->dscale_shift_y;
    pp_stride = ((pp_width + 15) >> 4) << 4;
    pp_buff_size = pp_stride * pp_height * 3 / 2;
    if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
      return (MPEG2DEC_MEMFAIL);

    dec_cont->StrmStorage.pp_buffer[2] = pp_buffer;
    InputQueueAddBuffer(dec_cont->pp_buffer_queue, &pp_buffer);
  }

  /* Allocate "extra" extra B buffer */
  if(extra_buffer) {
    ret = DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                          &dec_cont->StrmStorage.p_pic_buf[3].data);
    if(dec_cont->StrmStorage.p_pic_buf[3].data.bus_address == 0 || ret) {
      return (MPEG2DEC_MEMFAIL);
    }
    if (dec_cont->pp_enabled) {
      /* Add PP output buffers. */
      struct DWLLinearMem pp_buffer;
      u32 pp_width, pp_height, pp_stride, pp_buff_size;

      pp_width = (dec_cont->FrameDesc.frame_width * 16) >> dec_cont->dscale_shift_x;
      pp_height = (dec_cont->FrameDesc.frame_height * 16) >> dec_cont->dscale_shift_y;
      pp_stride = ((pp_width + 15) >> 4) << 4;
      pp_buff_size = pp_stride * pp_height * 3 / 2;
      if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
        return (MPEG2DEC_MEMFAIL);

      dec_cont->StrmStorage.pp_buffer[3] = pp_buffer;
      InputQueueAddBuffer(dec_cont->pp_buffer_queue, &pp_buffer);
    }
  }

  return (MPEG2DEC_OK);
}

/*------------------------------------------------------------------------------

    x.x Function name:  mpeg2HandleQTables

        Purpose:        Copy Q-tables to HW buffer

        Input:          DecContainer *dec_cont

        Output:

------------------------------------------------------------------------------*/
void mpeg2HandleQTables(DecContainer * dec_cont) {
  u32 i = 0;
  u32 shifter = 32;
  u32 table_word = 0;
  u32 *p_table_base = NULL;

  ASSERT(dec_cont->ApiStorage.p_qtable_base.virtual_address);
  ASSERT(dec_cont->ApiStorage.p_qtable_base.bus_address);

  p_table_base = dec_cont->ApiStorage.p_qtable_base.virtual_address;

  /* QP tables for intra */
  for(i = 0; i < 64; i++) {
    shifter -= 8;
    if(shifter == 24)
      table_word = (dec_cont->Hdrs.q_table_intra[i] << shifter);
    else
      table_word |= (dec_cont->Hdrs.q_table_intra[i] << shifter);

    if(shifter == 0) {
      *(p_table_base) = table_word;
      p_table_base++;
      shifter = 32;
    }
  }

  /* QP tables for non-intra */
  for(i = 0; i < 64; i++) {
    shifter -= 8;
    if(shifter == 24)
      table_word = (dec_cont->Hdrs.q_table_non_intra[i] << shifter);
    else
      table_word |= (dec_cont->Hdrs.q_table_non_intra[i] << shifter);

    if(shifter == 0) {
      *(p_table_base) = table_word;
      p_table_base++;
      shifter = 32;
    }
  }
}

/*------------------------------------------------------------------------------

    x.x Function name:  mpeg2HandleMpeg1Parameters

        Purpose:        Set MPEG-1 parameters

        Input:          DecContainer *dec_cont

        Output:

------------------------------------------------------------------------------*/

void mpeg2HandleMpeg1Parameters(DecContainer * dec_cont) {
  ASSERT(!dec_cont->Hdrs.mpeg2_stream);

  dec_cont->Hdrs.progressive_sequence = 1;
  dec_cont->Hdrs.chroma_format = 1;
  dec_cont->Hdrs.frame_rate_extension_n = dec_cont->Hdrs.frame_rate_extension_d = 0;
  dec_cont->Hdrs.intra_dc_precision = 0;
  dec_cont->Hdrs.picture_structure = 3;
  dec_cont->Hdrs.frame_pred_frame_dct = 1;
  dec_cont->Hdrs.concealment_motion_vectors = 0;
  dec_cont->Hdrs.intra_vlc_format = 0;
  dec_cont->Hdrs.alternate_scan = 0;
  dec_cont->Hdrs.repeat_first_field = 0;
  dec_cont->Hdrs.chroma420_type = 1;
  dec_cont->Hdrs.progressive_frame = 1;

}

/*------------------------------------------------------------------------------

   x.x Function name:  mpeg2DecCheckSupport

       Purpose:        Check picture sizes etc

       Input:          DecContainer *dec_cont

       Output:         MPEG2DEC_STRMERROR/MPEG2DEC_OK

------------------------------------------------------------------------------*/

Mpeg2DecRet mpeg2DecCheckSupport(DecContainer * dec_cont) {
#define DEC_FRAMED dec_cont->FrameDesc
  DWLHwConfig hw_config;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_MPEG2_DEC);

  if((DEC_FRAMED.frame_height > (hw_config.max_dec_pic_width >> 4)) ||
      (DEC_FRAMED.frame_height < (dec_cont->min_dec_pic_height >> 4))) {

    MPEG2DEC_DEBUG(("Mpeg2DecCheckSupport# Height not supported %d \n",
                    DEC_FRAMED.frame_height));
    return MPEG2DEC_STREAM_NOT_SUPPORTED;
  }

  if((DEC_FRAMED.frame_width > (hw_config.max_dec_pic_width >> 4)) ||
      (DEC_FRAMED.frame_width < (dec_cont->min_dec_pic_width >> 4))) {

    MPEG2DEC_DEBUG(("Mpeg2DecCheckSupport# Width not supported %d \n",
                    DEC_FRAMED.frame_width));
    return MPEG2DEC_STREAM_NOT_SUPPORTED;
  }

  if(DEC_FRAMED.total_mb_in_frame > MPEG2API_DEC_MBS) {
    MPEG2DEC_DEBUG(("Maximum number of macroblocks exceeded %d \n",
                    DEC_FRAMED.total_mb_in_frame));
    return MPEG2DEC_STREAM_NOT_SUPPORTED;
  }

  return MPEG2DEC_OK;

#undef DEC_FRAMED
}

/*------------------------------------------------------------------------------

    x.x Function name:  mpeg2DecPreparePicReturn

        Purpose:        Prepare return values for PIC returns
                        For use after HW start

        Input:          DecContainer *dec_cont
                        Mpeg2DecOutput *outData    currently used out

        Output:         void

------------------------------------------------------------------------------*/

void mpeg2DecPreparePicReturn(DecContainer * dec_cont) {

#define DEC_FRAMED dec_cont->FrameDesc
#define DEC_MBSD dec_cont->MbSetDesc
#define DEC_STRM dec_cont->StrmDesc
#define DEC_HDRS dec_cont->Hdrs
#define DEC_SVDS dec_cont->SvDesc
#define API_STOR dec_cont->ApiStorage

  ASSERT(dec_cont != NULL);

  if(dec_cont->Hdrs.interlaced) {
    if(dec_cont->Hdrs.picture_structure != FRAMEPICTURE) {
      dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].
      ff[dec_cont->Hdrs.field_index] =
        dec_cont->Hdrs.first_field_in_frame;
    } else {
      dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].ff[0] =
        1;
      dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].ff[1] =
        0;
    }
  } else {
    dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].ff[0] = 0;
    dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].ff[1] = 0;
  }

  /* update index */
  if(dec_cont->Hdrs.field_index == 1)
    dec_cont->Hdrs.field_index = -1;

  if(dec_cont->Hdrs.first_field_in_frame == 1)
    dec_cont->Hdrs.first_field_in_frame = -1;

  return;

#undef DEC_FRAMED
#undef DEC_MBSD
#undef DEC_STRM
#undef DEC_HDRS
#undef DEC_SVDS
#undef API_STOR

}

/*------------------------------------------------------------------------------

   x.x Function name:  mpeg2DecAspectRatio

       Purpose:        Set aspect ratio values for GetInfo

       Input:          DecContainer *dec_cont
                       Mpeg2DecInfo * dec_info    pointer to DecInfo

       Output:         void

------------------------------------------------------------------------------*/

void mpeg2DecAspectRatio(DecContainer * dec_cont, Mpeg2DecInfo * dec_info) {

  MPEG2DEC_DEBUG(("SAR %d\n", dec_cont->Hdrs.aspect_ratio_info));

  /* If forbidden or reserved */
  if(dec_cont->Hdrs.aspect_ratio_info == 0 ||
      dec_cont->Hdrs.aspect_ratio_info > 4) {
    dec_info->display_aspect_ratio = 0;
    return;
  }

  switch (dec_cont->Hdrs.aspect_ratio_info) {
  case 0x2:  /* 0010 4:3 */
    dec_info->display_aspect_ratio = MPEG2DEC_4_3;
    break;

  case 0x3:  /* 0011 16:9 */
    dec_info->display_aspect_ratio = MPEG2DEC_16_9;
    break;

  case 0x4:  /* 0100 2.21:1 */
    dec_info->display_aspect_ratio = MPEG2DEC_2_21_1;
    break;

  default:   /* Square 0001 1/1 */
    dec_info->display_aspect_ratio = MPEG2DEC_1_1;
    break;
  }

  /* TODO!  "DAR" */
  MPEG2DEC_DEBUG(("DAR %d\n", dec_cont->Hdrs.aspect_ratio_info));

  return;
}

/*------------------------------------------------------------------------------

    x.x Function name:  mpeg2DecBufferPicture

        Purpose:        Handles picture buffering

        Input:


        Output:

------------------------------------------------------------------------------*/
void mpeg2DecBufferPicture(DecContainer * dec_cont, u32 pic_id, u32 buffer_b,
                           u32 is_inter, Mpeg2DecRet return_value, u32 nbr_err_mbs) {
  i32 i, j;
  u32 pic_type;

  ASSERT(dec_cont);
  ASSERT(dec_cont->StrmStorage.out_count <=
         dec_cont->StrmStorage.num_buffers - 1);

  if(buffer_b == 0) {  /* Buffer I or P picture */
    i = dec_cont->StrmStorage.out_index + dec_cont->StrmStorage.out_count;
    if(i >= MPEG2_MAX_BUFFERS)
      i -= MPEG2_MAX_BUFFERS;
  } else { /* Buffer B picture */
    j = dec_cont->StrmStorage.out_index + dec_cont->StrmStorage.out_count;
    i = j - 1;
    if(j >= MPEG2_MAX_BUFFERS) j -= MPEG2_MAX_BUFFERS;
    if(i >= MPEG2_MAX_BUFFERS) i -= MPEG2_MAX_BUFFERS; /* Added check due to max 2 pic latency */
    if(i < 0)
      i += MPEG2_MAX_BUFFERS;
    dec_cont->StrmStorage.out_buf[j] = dec_cont->StrmStorage.out_buf[i];
  }
  j = dec_cont->StrmStorage.work_out;

  dec_cont->StrmStorage.out_buf[i] = j;

  dec_cont->StrmStorage.p_pic_buf[j].pic_id = pic_id;

  if(dec_cont->FrameDesc.pic_coding_type == IFRAME)
    pic_type = DEC_PIC_TYPE_I;
  else if(dec_cont->FrameDesc.pic_coding_type == PFRAME)
    pic_type = DEC_PIC_TYPE_P;
  else if(dec_cont->FrameDesc.pic_coding_type == BFRAME)
    pic_type = DEC_PIC_TYPE_B;
  else
    pic_type = DEC_PIC_TYPE_D;

  if((dec_cont->FrameDesc.field_coding_type[0] == IFRAME &&
      dec_cont->FrameDesc.field_coding_type[1] == PFRAME) ||
      (dec_cont->FrameDesc.field_coding_type[0] == PFRAME &&
       dec_cont->FrameDesc.field_coding_type[1] == IFRAME)) {
    dec_cont->StrmStorage.p_pic_buf[j].pic_code_type[0] = DEC_PIC_TYPE_I;
    dec_cont->StrmStorage.p_pic_buf[j].pic_code_type[1] = DEC_PIC_TYPE_P;
    dec_cont->StrmStorage.p_pic_buf[j].pic_type = 1;
  } else {
    dec_cont->StrmStorage.p_pic_buf[j].pic_code_type[0] = pic_type;
    dec_cont->StrmStorage.p_pic_buf[j].pic_code_type[1] = pic_type;
    dec_cont->StrmStorage.p_pic_buf[j].pic_type = !is_inter && !buffer_b;
  }

  MPEG2DEC_DEBUG(("Field pictype [%d/%d]\n", dec_cont->FrameDesc.field_coding_type[0], dec_cont->FrameDesc.field_coding_type[1]));

  dec_cont->StrmStorage.p_pic_buf[j].ret_val = return_value;
  dec_cont->StrmStorage.p_pic_buf[j].is_inter = is_inter;
  dec_cont->StrmStorage.p_pic_buf[j].tiled_mode = dec_cont->tiled_reference_enable;
  dec_cont->StrmStorage.p_pic_buf[j].tf = dec_cont->Hdrs.top_field_first;
  dec_cont->StrmStorage.p_pic_buf[j].rff = dec_cont->Hdrs.repeat_first_field;
  dec_cont->StrmStorage.p_pic_buf[j].rfc = dec_cont->Hdrs.repeat_frame_count;
  dec_cont->StrmStorage.p_pic_buf[j].ps = dec_cont->Hdrs.picture_structure;

  if(dec_cont->Hdrs.picture_structure != FRAMEPICTURE)
    dec_cont->StrmStorage.p_pic_buf[j].nbr_err_mbs = nbr_err_mbs / 2;
  else
    dec_cont->StrmStorage.p_pic_buf[j].nbr_err_mbs = nbr_err_mbs;

  if(dec_cont->pp_instance != NULL && return_value == FREEZED_PIC_RDY)
    dec_cont->StrmStorage.p_pic_buf[j].send_to_pp = 2;

  mpeg2DecTimeCode(dec_cont, &dec_cont->StrmStorage.p_pic_buf[j].time_code);
  dec_cont->StrmStorage.out_count++;
#ifdef USE_OUTPUT_RELEASE
  dec_cont->fullness = dec_cont->StrmStorage.out_count;
#endif
}

/*------------------------------------------------------------------------------

    x.x Function name:  mpeg2FreeBuffers

        Purpose:        Allocate memory

        Input:          DecContainer *dec_cont

        Output:         MPEG2DEC_MEMFAIL/MPEG2DEC_OK

------------------------------------------------------------------------------*/
void mpeg2FreeBuffers(DecContainer * dec_cont) {
  u32 i;
#define DEC_FRAMED dec_cont->FrameDesc
#ifndef USE_OUTPUT_RELEASE
  BqueueRelease(&dec_cont->StrmStorage.bq);
#else
  BqueueRelease2(&dec_cont->StrmStorage.bq);
#endif
  BqueueRelease(&dec_cont->StrmStorage.bq_pp);

  if(dec_cont->ApiStorage.p_qtable_base.virtual_address != NULL) {
    DWLFreeLinear(dec_cont->dwl, &dec_cont->ApiStorage.p_qtable_base);
    dec_cont->ApiStorage.p_qtable_base.virtual_address = NULL;
  }
#ifndef USE_EXTERNAL_BUFFER
  for(i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
    if(dec_cont->StrmStorage.p_pic_buf[i].data.virtual_address != NULL) {
      DWLFreeRefFrm(dec_cont->dwl,
                    &dec_cont->StrmStorage.p_pic_buf[i].data);
      dec_cont->StrmStorage.p_pic_buf[i].data.virtual_address = NULL;
      dec_cont->StrmStorage.p_pic_buf[i].data.bus_address = 0;
    }
  }
  if (dec_cont->pp_enabled) {
    for(i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
      if(dec_cont->StrmStorage.pp_buffer[i].virtual_address != NULL) {
        DWLFreeLinear(dec_cont->dwl,
                      &dec_cont->StrmStorage.pp_buffer[i]);
        dec_cont->StrmStorage.pp_buffer[i].virtual_address = NULL;
        dec_cont->StrmStorage.pp_buffer[i].bus_address = 0;
      }
    }
    InputQueueRelease(dec_cont->pp_buffer_queue);
    dec_cont->pp_buffer_queue = InputQueueInit(0);
    if (dec_cont->pp_buffer_queue == NULL) {
      return (MPEG2DEC_MEMFAIL);
    }
  }
#else
  if (dec_cont->pp_enabled) {
    for(i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
      if(dec_cont->StrmStorage.p_pic_buf[i].data.virtual_address != NULL) {
        DWLFreeRefFrm(dec_cont->dwl,
                      &dec_cont->StrmStorage.p_pic_buf[i].data);
        dec_cont->StrmStorage.p_pic_buf[i].data.virtual_address = NULL;
        dec_cont->StrmStorage.p_pic_buf[i].data.bus_address = 0;
      }
    }
  }
#endif
#undef DEC_FRAMED
}

