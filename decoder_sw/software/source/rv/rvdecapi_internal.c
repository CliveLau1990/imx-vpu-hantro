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
#include "rv_container.h"
#include "rv_cfg.h"
#include "rvdecapi.h"
#include "rv_utils.h"
#include "rvdecapi_internal.h"
#include "rv_debug.h"
#include "rv_vlc.h"
#include "regdrv_g1.h"
#include "input_queue.h"

/*------------------------------------------------------------------------------

    5.1 Function name:  rv_api_init_data_structures()

        Purpose:        Initialize Data Structures in DecContainer.

        Input:          DecContainer *dec_cont

        Output:         u32

------------------------------------------------------------------------------*/
void rv_api_init_data_structures(DecContainer * dec_cont) {

  dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work1 =
                                  INVALID_ANCHOR_PICTURE;
}

/*------------------------------------------------------------------------------

    x.x Function name:  rvAllocateBuffers

        Purpose:        Allocate memory

        Input:          DecContainer *dec_cont

        Output:         RVDEC_MEMFAIL/RVDEC_OK

------------------------------------------------------------------------------*/
RvDecRet rvAllocateBuffers(DecContainer * dec_cont) {
  u32 i;
  u32 size_tmp = 0;
  i32 ret = 0;
  u32 buffers = 0;

  ASSERT(dec_cont->StrmStorage.max_mbs_per_frame != 0);

  /* Reference images */
  if(!dec_cont->ApiStorage.external_buffers) {
    /*size_tmp = 384 * dec_cont->FrameDesc.total_mb_in_frame;*/
    size_tmp = 384 * dec_cont->StrmStorage.max_mbs_per_frame;

    /* Calculate minimum amount of buffers */
    buffers = 3;

    if( dec_cont->pp_instance ) { /* Combined mode used */
      dec_cont->StrmStorage.num_pp_buffers = dec_cont->StrmStorage.max_num_buffers;
      dec_cont->StrmStorage.num_buffers = buffers; /* Use bare minimum in decoder */
      buffers =  2;
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
      return RVDEC_MEMFAIL;

    ret = BqueueInit(&dec_cont->StrmStorage.bq_pp,
                     dec_cont->StrmStorage.num_pp_buffers );
    if( ret != HANTRO_OK )
      return RVDEC_MEMFAIL;
#ifndef USE_EXTERNAL_BUFFER
    for(i = 0; i < dec_cont->StrmStorage.num_buffers; i++) {
      ret |= DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                             &dec_cont->StrmStorage.p_pic_buf[i].data);

      RVDEC_DEBUG(("PicBuffer[%d]: %x, %x\n",
                   i,
                   (u32) dec_cont->StrmStorage.p_pic_buf[i].data.
                   virtual_address,
                   dec_cont->StrmStorage.p_pic_buf[i].data.bus_address));

      if(dec_cont->StrmStorage.p_pic_buf[i].data.bus_address == 0) {
        return (RVDEC_MEMFAIL);
      }
      if (dec_cont->pp_enabled) {
        /* Add PP output buffers. */
        struct DWLLinearMem pp_buffer;
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
        if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
          return (RVDEC_MEMFAIL);

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
      for(i = 0; i < dec_cont->StrmStorage.num_buffers; i++) {
        ret |= DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                               &dec_cont->StrmStorage.p_pic_buf[i].data);

        RVDEC_DEBUG(("PicBuffer[%d]: %x, %x\n",
                     i,
                     (u32) dec_cont->StrmStorage.p_pic_buf[i].data.
                     virtual_address,
                     dec_cont->StrmStorage.p_pic_buf[i].data.bus_address));

        if(dec_cont->StrmStorage.p_pic_buf[i].data.bus_address == 0) {
          return (RVDEC_MEMFAIL);
        }
      }
      /* initialize first picture buffer (work_out is 1 for the first picture)
       * grey, may be used as reference in certain error cases */
      (void) DWLmemset(dec_cont->StrmStorage.p_pic_buf[1].data.virtual_address,
                       128, 384 * dec_cont->FrameDesc.total_mb_in_frame);
      ret = DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                            &dec_cont->StrmStorage.p_rpr_buf.data);

      RVDEC_DEBUG(("PicBuffer[%d]: %x, %x\n",
                   0,
                   (u32) dec_cont->StrmStorage.p_rpr_buf.data.
                   virtual_address,
                   dec_cont->StrmStorage.p_rpr_buf.data.bus_address));

      if(dec_cont->StrmStorage.p_rpr_buf.data.bus_address == 0) {
        return (RVDEC_MEMFAIL);
      }
    }
#endif
  }

  /* shared memory for vlc tables.  Number of table entries is
   * numIntraQpRegions * numTableEntriesPerIntraRegion +
   * numInterQpRegions * numTableEntriesPerInterRegion
   *
   * Each table needs 3*16 entries for "control" and number of different
   * codes entries for symbols */
#if 0
  size_tmp = 5 * (2 * (1296 + 4*16) + 3*864 + 2*108 + 864 + 2*108 + 32) +
             7 * (1296 + 4*16 + 864 + 2*108 + 864 + 2*108 + 32);
  size_tmp *= 2; /* each symbol stored as u16 */
  size_tmp += 3*16*4*(5*(2+8+3+2+1+2+1)+7*(1+4+1+2+1+2+1));
  ret = DWLMallocLinear(dec_cont->dwl, size_tmp,
                        &dec_cont->StrmStorage.vlc_tables);
#endif

  ret |= DWLMallocLinear(dec_cont->dwl,
                         ((dec_cont->FrameDesc.total_mb_in_frame+1)&~0x1) * 4 * sizeof(u32),
                         &dec_cont->StrmStorage.direct_mvs);

  if(ret)
    return (RVDEC_MEMFAIL);
  else
    return RVDEC_OK;

}

/*------------------------------------------------------------------------------

    x.x Function name:  rvDecCheckSupport

        Purpose:        Check picture sizes etc

        Input:          DecContainer *dec_cont

        Output:         RVDEC_STRMERROR/RVDEC_OK

------------------------------------------------------------------------------*/

RvDecRet rvDecCheckSupport(DecContainer * dec_cont) {
  DWLHwConfig hw_config;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_RV_DEC);

  if((dec_cont->FrameDesc.frame_height > (hw_config.max_dec_pic_width >> 4)) ||
      (dec_cont->FrameDesc.frame_height < (dec_cont->min_dec_pic_height >> 4))) {

    RVDEC_DEBUG(("RvDecCheckSupport# Height not supported %d \n",
                 dec_cont->FrameDesc.frame_height));
    return RVDEC_STREAM_NOT_SUPPORTED;
  }

  if((dec_cont->FrameDesc.frame_width > (hw_config.max_dec_pic_width >> 4)) ||
      (dec_cont->FrameDesc.frame_width < (dec_cont->min_dec_pic_width >> 4))) {

    RVDEC_DEBUG(("RvDecCheckSupport# Width not supported %d \n",
                 dec_cont->FrameDesc.frame_width));
    return RVDEC_STREAM_NOT_SUPPORTED;
  }

  if(dec_cont->FrameDesc.total_mb_in_frame > RVAPI_DEC_MBS) {
    RVDEC_DEBUG(("Maximum number of macroblocks exceeded %d \n",
                 dec_cont->FrameDesc.total_mb_in_frame));
    return RVDEC_STREAM_NOT_SUPPORTED;
  }

  return RVDEC_OK;

}

/*------------------------------------------------------------------------------

    x.x Function name:  rvDecPreparePicReturn

        Purpose:        Prepare return values for PIC returns
                        For use after HW start

        Input:          DecContainer *dec_cont
                        RvDecOutput *outData    currently used out

        Output:         void

------------------------------------------------------------------------------*/

void rvDecPreparePicReturn(DecContainer * dec_cont) {

  ASSERT(dec_cont != NULL);

  dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].ff[0] = 0;
  dec_cont->StrmStorage.p_pic_buf[dec_cont->StrmStorage.work_out].ff[1] = 0;

  return;

}

/*------------------------------------------------------------------------------

    x.x Function name:  rvDecBufferPicture

        Purpose:        Handles picture buffering

        Input:


        Output:

------------------------------------------------------------------------------*/
void rvDecBufferPicture(DecContainer * dec_cont, u32 pic_id, u32 buffer_b,
                        u32 is_inter, RvDecRet return_value, u32 nbr_err_mbs) {
  i32 i, j;
  DecHdrs * p_hdrs = &dec_cont->Hdrs;
  u32 frame_width;
  u32 frame_height;
  u32 pic_type;

  ASSERT(dec_cont);

  if(buffer_b == 0) {  /* Buffer I or P picture */
    i = dec_cont->StrmStorage.out_index + dec_cont->StrmStorage.out_count;
    if(i >= 16)
      i -= 16;
  } else { /* Buffer B picture */
    j = dec_cont->StrmStorage.out_index + dec_cont->StrmStorage.out_count;
    i = j - 1;
    if(j >= 16)
      j -= 16;
    if(i < 0)
      i += 16;
    dec_cont->StrmStorage.out_buf[j] = dec_cont->StrmStorage.out_buf[i];
  }
  j = dec_cont->StrmStorage.work_out;

  frame_width = ( 15 + p_hdrs->horizontal_size ) & ~15;
  frame_height = ( 15 + p_hdrs->vertical_size ) & ~15;
  dec_cont->StrmStorage.p_pic_buf[j].frame_width = frame_width;
  dec_cont->StrmStorage.p_pic_buf[j].frame_height = frame_height;
  dec_cont->StrmStorage.p_pic_buf[j].coded_width = p_hdrs->horizontal_size;
  dec_cont->StrmStorage.p_pic_buf[j].coded_height = p_hdrs->vertical_size;
  dec_cont->StrmStorage.p_pic_buf[j].tiled_mode =
    dec_cont->tiled_reference_enable;

  dec_cont->StrmStorage.out_buf[i] = j;
  dec_cont->StrmStorage.p_pic_buf[j].decode_id = pic_id;
  if (!buffer_b) {
    dec_cont->StrmStorage.p_pic_buf[j].pic_id = pic_id;
    dec_cont->StrmStorage.prev_pic_id = dec_cont->StrmStorage.pic_id;
    dec_cont->StrmStorage.pic_id = pic_id;
  } else {
    dec_cont->StrmStorage.p_pic_buf[j].pic_id =
      dec_cont->StrmStorage.prev_pic_id + dec_cont->StrmStorage.trb;
  }
  dec_cont->StrmStorage.p_pic_buf[j].ret_val = return_value;
  dec_cont->StrmStorage.p_pic_buf[j].is_inter = is_inter;
  dec_cont->StrmStorage.p_pic_buf[j].pic_type = !is_inter && !buffer_b;
  if(dec_cont->FrameDesc.pic_coding_type == RV_I_PIC)
    pic_type = DEC_PIC_TYPE_I;
  else if(dec_cont->FrameDesc.pic_coding_type == RV_P_PIC)
    pic_type = DEC_PIC_TYPE_P;
  else if(dec_cont->FrameDesc.pic_coding_type == RV_B_PIC)
    pic_type = DEC_PIC_TYPE_B;
  else
    pic_type = DEC_PIC_TYPE_FI;
  dec_cont->StrmStorage.p_pic_buf[j].pic_code_type = pic_type;

  dec_cont->StrmStorage.p_pic_buf[j].nbr_err_mbs = nbr_err_mbs;

  if(dec_cont->pp_instance != NULL && return_value == FREEZED_PIC_RDY)
    dec_cont->StrmStorage.p_pic_buf[j].send_to_pp = 2;

  dec_cont->StrmStorage.out_count++;
#ifdef USE_OUTPUT_RELEASE
  dec_cont->fullness = dec_cont->StrmStorage.out_count;
#endif

}

/*------------------------------------------------------------------------------

    x.x Function name:  rvFreeBuffers

        Purpose:        Allocate memory

        Input:          DecContainer *dec_cont

        Output:         RVDEC_MEMFAIL/RVDEC_OK

------------------------------------------------------------------------------*/
void rvFreeRprBuffer(DecContainer * dec_cont) {
#ifndef USE_EXTERNAL_BUFFER
  if(dec_cont->StrmStorage.p_rpr_buf.data.virtual_address != NULL) {
    DWLFreeRefFrm(dec_cont->dwl,
                  &dec_cont->StrmStorage.p_rpr_buf.data);
    dec_cont->StrmStorage.p_rpr_buf.data.virtual_address = NULL;
    dec_cont->StrmStorage.p_rpr_buf.data.bus_address = 0;
  }
#endif
  if(dec_cont->StrmStorage.rpr_work_buffer.virtual_address != NULL ) {
    DWLFreeLinear( dec_cont->dwl, &dec_cont->StrmStorage.rpr_work_buffer );
    dec_cont->StrmStorage.rpr_work_buffer.virtual_address = NULL;
    dec_cont->StrmStorage.rpr_work_buffer.bus_address = 0;
  }
}

/*------------------------------------------------------------------------------

    x.x Function name:  rvFreeBuffers

        Purpose:        Allocate memory

        Input:          DecContainer *dec_cont

        Output:         RVDEC_MEMFAIL/RVDEC_OK

------------------------------------------------------------------------------*/
void rvFreeBuffers(DecContainer * dec_cont) {
  u32 i;

#ifdef USE_OUTPUT_RELEASE
  BqueueRelease2(&dec_cont->StrmStorage.bq);
#else
  BqueueRelease(&dec_cont->StrmStorage.bq);
#endif

#ifndef USE_EXTERNAL_BUFFER
  for(i = 0; i < 16; i++) {
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
      return (RVDEC_MEMFAIL);
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
    if(dec_cont->StrmStorage.p_rpr_buf.data.virtual_address != NULL) {
      DWLFreeRefFrm(dec_cont->dwl,
                    &dec_cont->StrmStorage.p_rpr_buf.data);
      dec_cont->StrmStorage.p_rpr_buf.data.virtual_address = NULL;
      dec_cont->StrmStorage.p_rpr_buf.data.bus_address = 0;
    }
  }
#endif

  if (dec_cont->StrmStorage.direct_mvs.virtual_address != NULL)
    DWLFreeLinear(dec_cont->dwl, &dec_cont->StrmStorage.direct_mvs);
  if (dec_cont->StrmStorage.vlc_tables.virtual_address != NULL)
    DWLFreeLinear(dec_cont->dwl, &dec_cont->StrmStorage.vlc_tables);

  rvFreeRprBuffer( dec_cont );

}


/*------------------------------------------------------------------------------

    x.x Function name:  rvAllocateBuffers

        Purpose:        Allocate memory

        Input:          DecContainer *dec_cont

        Output:         RVDEC_MEMFAIL/RVDEC_OK

------------------------------------------------------------------------------*/
RvDecRet rvAllocateRprBuffer(DecContainer * dec_cont) {

  i32 ret = 0;
  u32 size_tmp = 0;

  ASSERT(dec_cont->StrmStorage.max_mbs_per_frame != 0);
#ifndef USE_EXTERNAL_BUFFER
  if(dec_cont->StrmStorage.p_rpr_buf.data.virtual_address != NULL)
    return RVDEC_OK; /* already allocated */
#else
  if(dec_cont->StrmStorage.rpr_work_buffer.virtual_address != NULL)
    return RVDEC_OK; /* already allocated */
#endif

  /* Reference images */
  /*size_tmp = 384 * dec_cont->FrameDesc.total_mb_in_frame;*/
  size_tmp = 384 * dec_cont->StrmStorage.max_mbs_per_frame;
#ifndef USE_EXTERNAL_BUFFER
  ret = DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                        &dec_cont->StrmStorage.p_rpr_buf.data);

  RVDEC_DEBUG(("PicBuffer[%d]: %x, %x\n",
               0,
               (u32) dec_cont->StrmStorage.p_rpr_buf.data.
               virtual_address,
               dec_cont->StrmStorage.p_rpr_buf.data.bus_address));

  if(dec_cont->StrmStorage.p_rpr_buf.data.bus_address == 0) {
    return (RVDEC_MEMFAIL);
  }
#endif

  /* Allocate work buffer for look-up tables:
   *  - 2*ptr*height for row pointers
   *  - u16*width for column offsets
   *  - u8*width for x coeffs
   *  - u8*height for y coeffs
   */
  size_tmp = 2*sizeof(u8*)*dec_cont->StrmStorage.max_frame_height +
             sizeof(u16)*dec_cont->StrmStorage.max_frame_width +
             sizeof(u8)*dec_cont->StrmStorage.max_frame_width +
             sizeof(u8)*dec_cont->StrmStorage.max_frame_height;

  ret = DWLMallocLinear(dec_cont->dwl, size_tmp,
                        &dec_cont->StrmStorage.rpr_work_buffer);
  if(dec_cont->StrmStorage.rpr_work_buffer.bus_address == 0 ) {
    return (RVDEC_MEMFAIL);
  }

  if(ret)
    return (RVDEC_MEMFAIL);
  else
    return RVDEC_OK;

}


