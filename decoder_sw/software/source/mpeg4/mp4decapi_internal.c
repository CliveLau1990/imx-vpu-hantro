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
#include "mp4dechwd_container.h"
#include "mp4deccfg.h"
#include "mp4decapi.h"
#include "mp4decdrv.h"
#include "mp4debug.h"
#include "mp4dechwd_utils.h"
#include "mp4decapi_internal.h"
#include "input_queue.h"
#ifdef MP4_ASIC_TRACE
#include "mpeg4asicdbgtrace.h"
#endif

static u32 MP4DecCheckProfileSupport(DecContainer * dec_cont);

/*------------------------------------------------------------------------------

        Function name:  ClearDataStructures()

        Purpose:        Initialize Data Structures in DecContainer.

        Input:          DecContainer *dec_cont

        Output:         u32

------------------------------------------------------------------------------*/
void MP4API_InitDataStructures(DecContainer * dec_cont) {

  /*
   *  have to be initialized into 1 to enable
   *  decoding stream without VO-headers
   */
  dec_cont->Hdrs.visual_object_verid = 1;
  dec_cont->Hdrs.video_format = 5;
  dec_cont->Hdrs.colour_primaries = 1;
  dec_cont->Hdrs.transfer_characteristics = 1;
  dec_cont->Hdrs.matrix_coefficients = 1;
  dec_cont->Hdrs.low_delay = 0;

  dec_cont->StrmStorage.vp_qp = 1;
  dec_cont->ApiStorage.first_headers = 1;

  dec_cont->StrmStorage.work_out = 0;
  dec_cont->StrmStorage.work0 = dec_cont->StrmStorage.work1 =
                                  INVALID_ANCHOR_PICTURE;

}

/*------------------------------------------------------------------------------

        Function name:  MP4DecTimeCode

        Purpose:        Write time data to output

        Input:          DecContainer *dec_cont, MP4DecTime *time_code

        Output:         void

------------------------------------------------------------------------------*/

void MP4DecTimeCode(DecContainer * dec_cont, MP4DecTime * time_code) {

#define DEC_VOPD dec_cont->VopDesc
#define DEC_HDRS dec_cont->Hdrs
#define DEC_STST dec_cont->StrmStorage
#define DEC_SVDS dec_cont->SvDesc

  ASSERT(dec_cont);
  ASSERT(time_code);

  if(DEC_STST.short_video) {

    u32 time_step;

    if (DEC_SVDS.cpcf) {
      DEC_HDRS.vop_time_increment_resolution = 1800000;
      if (DEC_SVDS.cpcfc >> 7) /* factor 1001 (5.1.7) */
        time_step = (DEC_SVDS.cpcfc & 0x7F) * 1001;
      else                     /* factor 1000 (5.1.7) */
        time_step = (DEC_SVDS.cpcfc & 0x7F) * 1000;
    } else {
      DEC_HDRS.vop_time_increment_resolution = 30000;
      time_step = 1001;
    }

    DEC_VOPD.vop_time_increment +=
      (dec_cont->VopDesc.tics_from_prev * time_step);
    while(DEC_VOPD.vop_time_increment >= DEC_HDRS.vop_time_increment_resolution) {
      DEC_VOPD.vop_time_increment -= DEC_HDRS.vop_time_increment_resolution;
      DEC_VOPD.time_code_seconds++;
      if(DEC_VOPD.time_code_seconds > 59) {
        DEC_VOPD.time_code_minutes++;
        DEC_VOPD.time_code_seconds = 0;
        if(DEC_VOPD.time_code_minutes > 59) {
          DEC_VOPD.time_code_hours++;
          DEC_VOPD.time_code_minutes = 0;
          if(DEC_VOPD.time_code_hours > 23) {
            DEC_VOPD.time_code_hours = 0;
          }
        }
      }
    }
  }
  time_code->hours = DEC_VOPD.time_code_hours;
  time_code->minutes = DEC_VOPD.time_code_minutes;
  time_code->seconds = DEC_VOPD.time_code_seconds;
  time_code->time_incr = DEC_VOPD.vop_time_increment;
  time_code->time_res = DEC_HDRS.vop_time_increment_resolution;

#undef DEC_VOPD
#undef DEC_HDRS
#undef DEC_STST
#undef DEC_SVDS

}

/*------------------------------------------------------------------------------

       Function name:  MP4NotCodedVop

       Purpose:        prepare HW for not coded VOP, rlc mode

       Input:          DecContainer *dec_cont, TimeCode *time_code

       Output:         void

------------------------------------------------------------------------------*/

void MP4NotCodedVop(DecContainer * dec_container) {

  extern const u8 asic_pos_no_rlc[6];
  u32 asic_tmp = 0;
  u32 i = 0;

  asic_tmp |= (1U << ASICPOS_VPBI);
  asic_tmp |= (1U << ASICPOS_MBTYPE);
  asic_tmp |= (1U << ASICPOS_MBNOTCODED);

  asic_tmp |= (dec_container->StrmStorage.q_p << ASICPOS_QP);
  for(i = 0; i < 6; i++) {
    asic_tmp |= (1 << asic_pos_no_rlc[i]);
  }

  *dec_container->MbSetDesc.p_ctrl_data_addr = asic_tmp;

  /* only first has VP boundary */

  asic_tmp &= ~(1U << ASICPOS_VPBI);

  for(i = 1; i < dec_container->VopDesc.total_mb_in_vop; i++) {
    *(dec_container->MbSetDesc.p_ctrl_data_addr + i) = asic_tmp;
    dec_container->MbSetDesc.p_mv_data_addr[i*NBR_MV_WORDS_MB] = 0;
  }
  dec_container->MbSetDesc.p_mv_data_addr[0*NBR_MV_WORDS_MB] = 0;

}

/*------------------------------------------------------------------------------

       Function name:  MP4AllocateBuffers

       Purpose:        Allocate memory

       Input:          DecContainer *dec_cont

       Output:         MP4DEC_MEMFAIL/MP4DEC_OK

------------------------------------------------------------------------------*/

MP4DecRet MP4AllocateBuffers(DecContainer * dec_cont) {
#define DEC_VOPD dec_cont->VopDesc

  u32 i;
  i32 ret = 0;
  u32 size_tmp = 0;
  u32 buffers = 0;
  /* Allocate mb control buffer */

  ASSERT(DEC_VOPD.total_mb_in_vop != 0);

  if (dec_cont->rlc_mode) {
    if (MP4AllocateRlcBuffers(dec_cont) != MP4DEC_OK)
      return (MP4DEC_MEMFAIL);
  }

  size_tmp = (MP4API_DEC_FRAME_BUFF_SIZE * DEC_VOPD.total_mb_in_vop * 4);

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
    buffers =  2; /*dec_cont->Hdrs.interlaced ? 1 : 2;*/
    if( dec_cont->StrmStorage.num_pp_buffers < buffers )
      dec_cont->StrmStorage.num_pp_buffers = buffers;
  } else { /* Dec only or separate PP */
    dec_cont->StrmStorage.num_buffers = dec_cont->StrmStorage.max_num_buffers;
    dec_cont->StrmStorage.num_pp_buffers = 0;
    if( dec_cont->StrmStorage.num_buffers < buffers )
      dec_cont->StrmStorage.num_buffers = buffers;
  }
#ifndef USE_OUTPUT_RELEASE
  ret = BqueueInit(&dec_cont->StrmStorage.bq,
                   dec_cont->StrmStorage.num_buffers );
#else
  ret = BqueueInit2(&dec_cont->StrmStorage.bq,
                    dec_cont->StrmStorage.num_buffers );
#endif
  if( ret != HANTRO_OK )
    return MP4DEC_MEMFAIL;

  ret = BqueueInit(&dec_cont->StrmStorage.bq_pp,
                   dec_cont->StrmStorage.num_pp_buffers );
  if( ret != HANTRO_OK )
    return MP4DEC_MEMFAIL;

#ifndef USE_EXTERNAL_BUFFER
  for (i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
    ret |= DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                           &dec_cont->StrmStorage.data[i]);
    dec_cont->StrmStorage.p_pic_buf[i].data_index = i;
    if(dec_cont->StrmStorage.data[i].bus_address == 0) {
      return (MP4DEC_MEMFAIL);
    }
    if (dec_cont->pp_enabled) {
      /* Add PP output buffers. */
      struct DWLLinearMem pp_buffer;
      u32 pp_width, pp_height, pp_stride, pp_buff_size;

      pp_width = (dec_cont->VopDesc.vop_width * 16) >> dec_cont->dscale_shift_x;
      pp_height = (dec_cont->VopDesc.vop_height * 16) >> dec_cont->dscale_shift_y;
      pp_stride = ((pp_width + 15) >> 4) << 4;
      pp_buff_size = pp_stride * pp_height * 3 / 2;
      if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
        return (MP4DEC_MEMFAIL);

      dec_cont->StrmStorage.pp_buffer[i] = pp_buffer;
      InputQueueAddBuffer(dec_cont->pp_buffer_queue, &pp_buffer);
    }
  }
#else
  if (dec_cont->pp_enabled) {
    for (i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
      ret |= DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                             &dec_cont->StrmStorage.data[i]);
      dec_cont->StrmStorage.p_pic_buf[i].data_index = i;
      if(dec_cont->StrmStorage.data[i].bus_address == 0) {
        return (MP4DEC_MEMFAIL);
      }
    }
  }
#endif
  ret = DWLMallocLinear(dec_cont->dwl,
                        ((DEC_VOPD.total_mb_in_vop+1)&~0x1)*4*sizeof(u32),
                        &dec_cont->StrmStorage.direct_mvs);
  ret |= DWLMallocLinear(dec_cont->dwl, 2*64,
                         &dec_cont->StrmStorage.quant_mat_linear);
  if(ret) {
    return (MP4DEC_MEMFAIL);
  }

  /* initialize quantization tables */
  if(dec_cont->Hdrs.quant_type)
    MP4SetQuantMatrix(dec_cont);
  /*
   *  dec_cont->MbSetDesc
   */

  dec_cont->MbSetDesc.odd_rlc = 0;

  /* initialize first picture buffer grey, may be used as reference
   * in certain error cases */
#ifndef USE_EXTERNAL_BUFFER
  (void) DWLmemset(dec_cont->StrmStorage.data[0].virtual_address,
                   128, 384 * DEC_VOPD.total_mb_in_vop);
#endif
  return MP4DEC_OK;

#undef DEC_VOPD
}

/*------------------------------------------------------------------------------

        Function name:  MP4FreeBuffers

        Purpose:        Free memory

        Input:          DecContainer *dec_cont

        Output:

------------------------------------------------------------------------------*/

void MP4FreeBuffers(DecContainer * dec_cont) {
  u32 i=0;

  /* Allocate mb control buffer */
#ifndef USE_OUTPUT_RELEASE
  BqueueRelease(&dec_cont->StrmStorage.bq);
#else
  BqueueRelease2(&dec_cont->StrmStorage.bq);
#endif
  BqueueRelease(&dec_cont->StrmStorage.bq_pp);

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
  if(dec_cont->StrmStorage.direct_mvs.virtual_address != NULL) {
    DWLFreeLinear(dec_cont->dwl, &dec_cont->StrmStorage.direct_mvs);
    dec_cont->StrmStorage.direct_mvs.virtual_address = NULL;
  }

  if(dec_cont->StrmStorage.quant_mat_linear.virtual_address != NULL) {
    DWLFreeLinear(dec_cont->dwl, &dec_cont->StrmStorage.quant_mat_linear);
    dec_cont->StrmStorage.quant_mat_linear.virtual_address = NULL;
  }
#ifndef USE_EXTERNAL_BUFFER
  for(i = 0; i < dec_cont->StrmStorage.num_buffers ; i++) {
    if(dec_cont->StrmStorage.data[i].virtual_address != NULL) {
      DWLFreeRefFrm(dec_cont->dwl, &dec_cont->StrmStorage.data[i]);
      dec_cont->StrmStorage.data[i].virtual_address = NULL;
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
      return (MP4DEC_MEMFAIL);
    }
  }
#else
  if (dec_cont->pp_enabled) {
    if(dec_cont->StrmStorage.data[i].virtual_address != NULL) {
      DWLFreeRefFrm(dec_cont->dwl, &dec_cont->StrmStorage.data[i]);
      dec_cont->StrmStorage.data[i].virtual_address = NULL;
    }
  }
#endif

}

/*------------------------------------------------------------------------------

        Function name:  MP4AllocateRlcBuffers

        Purpose:        Allocate memory for rlc mode

        Input:          DecContainer *dec_cont

        Output:         MP4DEC_MEMFAIL/MP4DEC_OK

------------------------------------------------------------------------------*/

MP4DecRet MP4AllocateRlcBuffers(DecContainer * dec_cont) {
#define DEC_VOPD dec_cont->VopDesc

  i32 ret = 0;
  u32 size_rlc = 0, size_mv = 0, size_control = 0, size_dc = 0;

  ASSERT(DEC_VOPD.total_mb_in_vop != 0);

  if (dec_cont->rlc_mode) {
    size_control = NBR_OF_WORDS_MB * DEC_VOPD.total_mb_in_vop * 4;

    ret |= DWLMallocLinear(dec_cont->dwl, size_control,
                           &dec_cont->MbSetDesc.ctrl_data_mem);

    dec_cont->MbSetDesc.p_ctrl_data_addr =
      dec_cont->MbSetDesc.ctrl_data_mem.virtual_address;

    /* Allocate motion vector data buffer */
    size_mv = NBR_MV_WORDS_MB * DEC_VOPD.total_mb_in_vop * 4;
    ret |= DWLMallocLinear(dec_cont->dwl, size_mv,
                           &dec_cont->MbSetDesc.mv_data_mem);
    dec_cont->MbSetDesc.p_mv_data_addr =
      dec_cont->MbSetDesc.mv_data_mem.virtual_address;

    /* RLC data buffer */

    size_rlc = (_MP4_RLC_BUFFER_SIZE * DEC_VOPD.total_mb_in_vop * 4);

    ret |= DWLMallocLinear(dec_cont->dwl, size_rlc,
                           &dec_cont->MbSetDesc.rlc_data_mem);
    dec_cont->MbSetDesc.rlc_data_buffer_size = size_rlc;
    dec_cont->MbSetDesc.p_rlc_data_addr =
      dec_cont->MbSetDesc.rlc_data_mem.virtual_address;
    dec_cont->MbSetDesc.p_rlc_data_curr_addr = dec_cont->MbSetDesc.p_rlc_data_addr;
    dec_cont->MbSetDesc.p_rlc_data_vp_addr = dec_cont->MbSetDesc.p_rlc_data_addr;

    /* Separate DC component data buffer */

    size_dc = (NBR_DC_WORDS_MB * DEC_VOPD.total_mb_in_vop * 4);

    ret |= DWLMallocLinear(dec_cont->dwl, size_dc,
                           &dec_cont->MbSetDesc.DcCoeffMem);
    dec_cont->MbSetDesc.p_dc_coeff_data_addr =
      dec_cont->MbSetDesc.DcCoeffMem.virtual_address;

    if(ret)
      return (MP4DEC_MEMFAIL);
  }

  /* reset memories */

  (void)DWLmemset(dec_cont->MbSetDesc.ctrl_data_mem.virtual_address,
                  0x0, size_control);
  (void)DWLmemset(dec_cont->MbSetDesc.mv_data_mem.virtual_address,
                  0x0, size_mv);
  (void)DWLmemset(dec_cont->MbSetDesc.rlc_data_mem.virtual_address,
                  0x0, size_rlc);
  (void)DWLmemset(dec_cont->MbSetDesc.DcCoeffMem.virtual_address,
                  0x0, size_dc);

  return MP4DEC_OK;

#undef DEC_VOPD
}

/*------------------------------------------------------------------------------

        Function name:  MP4DecAllocExtraBPic

        Purpose:        allocate b picture after normal allocation

        Input:          DecContainer *dec_cont

        Output:         MP4DEC_STRMERROR/MP4DEC_OK

------------------------------------------------------------------------------*/

MP4DecRet MP4DecAllocExtraBPic(DecContainer * dec_cont) {
  i32 ret = 0;
  u32 size_tmp = 0;
  u32 extra_buffer = 0;

  /* If we already have enough buffers, do nothing. */
  if( dec_cont->StrmStorage.num_buffers >= 3)
    return MP4DEC_OK;

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

  size_tmp = (MP4API_DEC_FRAME_BUFF_SIZE * dec_cont->VopDesc.total_mb_in_vop * 4);

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
    return (MP4DEC_MEMFAIL);

  ret = DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                        &dec_cont->StrmStorage.data[2]);
  dec_cont->StrmStorage.p_pic_buf[2].data_index = 2;
  if(dec_cont->StrmStorage.data[2].bus_address == 0 || ret) {
    return (MP4DEC_MEMFAIL);
  }
  if (dec_cont->pp_enabled) {
    /* Add PP output buffers. */
    struct DWLLinearMem pp_buffer;
    u32 pp_width, pp_height, pp_stride, pp_buff_size;

    pp_width = (dec_cont->VopDesc.vop_width * 16) >> dec_cont->dscale_shift_x;
    pp_height = (dec_cont->VopDesc.vop_height * 16) >> dec_cont->dscale_shift_y;
    pp_stride = ((pp_width + 15) >> 4) << 4;
    pp_buff_size = pp_stride * pp_height * 3 / 2;
    if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
      return (MP4DEC_MEMFAIL);

    dec_cont->StrmStorage.pp_buffer[2] = pp_buffer;
    InputQueueAddBuffer(dec_cont->pp_buffer_queue, &pp_buffer);
  }

  /* Allocate "extra" extra B buffer */
  if(extra_buffer) {
    ret = DWLMallocRefFrm(dec_cont->dwl, size_tmp,
                          &dec_cont->StrmStorage.data[3]);
    if(dec_cont->StrmStorage.data[3].bus_address == 0 || ret) {
      return (MP4DEC_MEMFAIL);
    }
    if (dec_cont->pp_enabled) {
      /* Add PP output buffers. */
      struct DWLLinearMem pp_buffer;
      u32 pp_width, pp_height, pp_stride, pp_buff_size;

      pp_width = (dec_cont->VopDesc.vop_width * 16) >> dec_cont->dscale_shift_x;
      pp_height = (dec_cont->VopDesc.vop_height * 16) >> dec_cont->dscale_shift_y;
      pp_stride = ((pp_width + 15) >> 4) << 4;
      pp_buff_size = pp_stride * pp_height * 3 / 2;
      if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
        return (MP4DEC_MEMFAIL);

      dec_cont->StrmStorage.pp_buffer[3] = pp_buffer;
      InputQueueAddBuffer(dec_cont->pp_buffer_queue, &pp_buffer);
    }
  }
  return (MP4DEC_OK);
}

/*------------------------------------------------------------------------------

        Function name:  MP4DecCheckSupport

        Purpose:        Check picture sizes etc

        Input:          DecContainer *dec_cont

        Output:         MP4DEC_STRM_NOT_SUPPORTED/MP4DEC_OK

------------------------------------------------------------------------------*/

MP4DecRet MP4DecCheckSupport(DecContainer * dec_cont) {
#define DEC_VOPD dec_cont->VopDesc
  DWLHwConfig hw_config;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_MPEG4_DEC);

  if((dec_cont->VopDesc.vop_height > (hw_config.max_dec_pic_width >> 4)) ||
      (dec_cont->VopDesc.vop_height < (dec_cont->min_dec_pic_height >> 4))) {

    MP4DEC_DEBUG(("MP4DecCheckSupport# Height not supported %d \n",
                  DEC_VOPD.vop_height));
    return MP4DEC_STRM_NOT_SUPPORTED;
  }

  if((DEC_VOPD.vop_width > (hw_config.max_dec_pic_width >> 4)) ||
      (DEC_VOPD.vop_width < (dec_cont->min_dec_pic_width >> 4))) {

    MP4DEC_DEBUG(("MP4DecCheckSupport# Width not supported %d \n",
                  DEC_VOPD.vop_width));
    return MP4DEC_STRM_NOT_SUPPORTED;
  }

  /* Check height of interlaced pic */

  if(dec_cont->VopDesc.vop_height < (dec_cont->min_dec_pic_height >> 3)
      && dec_cont->Hdrs.interlaced ) {
    MP4DEC_DEBUG(("Interlaced height not supported\n"));
    return MP4DEC_STRM_NOT_SUPPORTED;
  }

  if(DEC_VOPD.total_mb_in_vop > MP4API_DEC_MBS) {
    MP4DEC_DEBUG(("Maximum number of macroblocks exceeded %d \n",
                  DEC_VOPD.total_mb_in_vop));
    return MP4DEC_STRM_NOT_SUPPORTED;
  }

  if(MP4DecCheckProfileSupport(dec_cont)) {
    MP4DEC_DEBUG(("Profile not supported\n"));
    return MP4DEC_STRM_NOT_SUPPORTED;
  }
  return MP4DEC_OK;

#undef DEC_VOPD
}


/*------------------------------------------------------------------------------

   x.x Function name:  MP4DecPixelAspectRatio

       Purpose:        Set pixel aspext ratio values for GetInfo


       Input:          DecContainer *dec_cont
                       MP4DecInfo * dec_info    pointer to DecInfo

       Output:         void

------------------------------------------------------------------------------*/
void MP4DecPixelAspectRatio(DecContainer * dec_cont, MP4DecInfo * dec_info) {


  MP4DEC_DEBUG(("PAR %d\n", dec_cont->Hdrs.aspect_ratio_info));


  switch(dec_cont->Hdrs.aspect_ratio_info) {

  case 0x2: /* 0010 12:11 */
    dec_info->par_width = 12;
    dec_info->par_height = 11;
    break;

  case 0x3: /* 0011 10:11 */
    dec_info->par_width = 10;
    dec_info->par_height = 11;
    break;

  case 0x4: /* 0100 16:11 */
    dec_info->par_width = 16;
    dec_info->par_height = 11;
    break;

  case 0x5: /* 0101 40:11 */
    dec_info->par_width = 40;
    dec_info->par_height = 33;
    break;

  case 0xF: /* 1111 Extended PAR */
    dec_info->par_width = dec_cont->Hdrs.par_width;
    dec_info->par_height = dec_cont->Hdrs.par_height;
    break;

  default: /* Square */
    dec_info->par_width = dec_info->par_height = 1;
    break;
  }
  return;
}

/*------------------------------------------------------------------------------

        Function name:  MP4DecBufferPicture

        Purpose:        Rotate buffers and store information about picture


        Input:          DecContainer *dec_cont
                        pic_id, vop type, return value and time information

        Output:         HANTRO_OK / HANTRO_NOK

------------------------------------------------------------------------------*/
void MP4DecBufferPicture(DecContainer *dec_cont, u32 pic_id, u32 vop_type,
                         u32 nbr_err_mbs) {

  i32 i, j;
  u32 pic_type;

  ASSERT(dec_cont);
  ASSERT(dec_cont->StrmStorage.out_count <=
         dec_cont->StrmStorage.num_buffers - 1);

  if( vop_type != BVOP ) { /* Buffer I or P picture */
    i = dec_cont->StrmStorage.out_index + dec_cont->StrmStorage.out_count;
    if( i >= MP4_MAX_BUFFERS ) {
      i -= MP4_MAX_BUFFERS;
    }
  } else { /* Buffer B picture */
    j = dec_cont->StrmStorage.out_index + dec_cont->StrmStorage.out_count;
    i = j - 1;
    if( j >= MP4_MAX_BUFFERS ) j -= MP4_MAX_BUFFERS;
    if( i >= MP4_MAX_BUFFERS ) i -= MP4_MAX_BUFFERS;
    if( i < 0 ) i += MP4_MAX_BUFFERS;
    dec_cont->StrmStorage.out_buf[j] = dec_cont->StrmStorage.out_buf[i];
  }
  j = dec_cont->StrmStorage.work_out;

  dec_cont->StrmStorage.out_buf[i] = j;
  dec_cont->StrmStorage.p_pic_buf[j].pic_id = pic_id;
  dec_cont->StrmStorage.p_pic_buf[j].is_inter = vop_type;
  if(vop_type == IVOP)
    pic_type = DEC_PIC_TYPE_I;
  else if(vop_type == PVOP)
    pic_type = DEC_PIC_TYPE_P;
  else
    pic_type = DEC_PIC_TYPE_B;
  dec_cont->StrmStorage.p_pic_buf[j].pic_type = pic_type;
  dec_cont->StrmStorage.p_pic_buf[j].nbr_err_mbs = nbr_err_mbs;
  dec_cont->StrmStorage.p_pic_buf[j].tiled_mode =
    dec_cont->tiled_reference_enable;

  MP4DecTimeCode(dec_cont, &dec_cont->StrmStorage.p_pic_buf[j].time_code);

  dec_cont->StrmStorage.out_count++;
#ifdef USE_OUTPUT_RELEASE
  dec_cont->fullness = dec_cont->StrmStorage.out_count;
#endif
}
/*------------------------------------------------------------------------------

       Function name:  MP4SetQuantMatrix

       Purpose:        Set hw to use stream defined or default matrises



       Input:          DecContainer *dec_cont

       Output:         void

------------------------------------------------------------------------------*/
void MP4SetQuantMatrix(DecContainer * dec_cont) {

  u32 i, tmp;
  u8 *p;
  u32 *p_lin;

  const u8 default_intra_mat[64] = {
    8, 17, 18, 19, 21, 23, 25, 27, 17, 18, 19, 21, 23, 25, 27, 28,
    20, 21, 22, 23, 24, 26, 28, 30, 21, 22, 23, 24, 26, 28, 30, 32,
    22, 23, 24, 26, 28, 30, 32, 35, 23, 24, 26, 28, 30, 32, 35, 38,
    25, 26, 28, 30, 32, 35, 38, 41, 27, 28, 30, 32, 35, 38, 41, 45
  };

  const u8 default_non_intra_mat[64] = {
    16, 17, 18, 19, 20, 21, 22, 23, 17, 18, 19, 20, 21, 22, 23, 24,
    18, 19, 20, 21, 22, 23, 24, 25, 19, 20, 21, 22, 23, 24, 26, 27,
    20, 21, 22, 23, 25, 26, 27, 28, 21, 22, 23, 24, 26, 27, 28, 30,
    22, 23, 24, 26, 27, 28, 30, 31, 23, 24, 25, 27, 28, 30, 31, 33
  };


  p = (u8 *)dec_cont->StrmStorage.quant_mat;
  p_lin = (u32 *)dec_cont->StrmStorage.quant_mat_linear.virtual_address;

  if(p[0]) {
    for (i = 0; i < 16; i++) {
      tmp = (p[4*i+0]<<24) | (p[4*i+1]<<16) |
            (p[4*i+2]<<8)  | (p[4*i+3]<<0);
      p_lin[i] = tmp;
    }
  } else { /* default */
    for (i = 0; i < 16; i++) {
      tmp = (default_intra_mat[4*i+0]<<24) |
            (default_intra_mat[4*i+1]<<16) |
            (default_intra_mat[4*i+2]<<8) |
            (default_intra_mat[4*i+3]<<0);
      p_lin[i] = tmp;
    }
  }

  if(p[64]) {
    for (i = 16; i < 32; i++) {
      tmp = (p[4*i+0]<<24) | (p[4*i+1]<<16) |
            (p[4*i+2]<<8)  | (p[4*i+3]<<0);
      p_lin[i] = tmp;
    }
  } else {
    for (i = 0; i < 16; i++) {
      tmp = (default_non_intra_mat[4*i+0]<<24) |
            (default_non_intra_mat[4*i+1]<<16) |
            (default_non_intra_mat[4*i+2]<<8) |
            (default_non_intra_mat[4*i+3]<<0);
      p_lin[i+16] = tmp;
    }
  }

}
/*------------------------------------------------------------------------------

       Function name:  MP4DecCheckProfileSupport

       Purpose:        Check support for ASP tools

       Input:          DecContainer *dec_cont

       Output:         void

------------------------------------------------------------------------------*/
static u32 MP4DecCheckProfileSupport(DecContainer * dec_cont) {
  u32 ret = 0;
  DWLHwConfig hw_config;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_MPEG4_DEC);

  if(hw_config.mpeg4_support == MPEG4_SIMPLE_PROFILE &&
      !dec_cont->StrmStorage.sorenson_spark) {

    if(dec_cont->Hdrs.quant_type)
      ret++;

    if(!dec_cont->Hdrs.low_delay)
      ret++;

    if(dec_cont->Hdrs.interlaced)
      ret++;

    if(dec_cont->Hdrs.quarterpel)
      ret++;
  }

  return ret;
}


/*------------------------------------------------------------------------------

       Function name:  MP4DecBFrameSupport

       Purpose:        Check support for B frames

       Input:          DecContainer *dec_cont

       Output:         void

------------------------------------------------------------------------------*/
u32 MP4DecBFrameSupport(DecContainer * dec_cont) {
  u32 ret = HANTRO_TRUE;
  DWLHwConfig hw_config;
  UNUSED(dec_cont);

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_MPEG4_DEC);

  if(hw_config.mpeg4_support == MPEG4_SIMPLE_PROFILE )
    ret = HANTRO_FALSE;

  return ret;
}
/*------------------------------------------------------------------------------

        Function name:  MP4DecResolveVirtual

        Purpose:        Get virtual address for this picture

        Input:          DecContainer *dec_cont

        Output:         void

------------------------------------------------------------------------------*/
u32 * MP4DecResolveVirtual(DecContainer * dec_cont, u32 index ) {
  if( (i32)index < 0 )
    return NULL;
  return dec_cont->StrmStorage.data[dec_cont->StrmStorage.
                                    p_pic_buf[index].data_index].virtual_address;
}

/*------------------------------------------------------------------------------

        Function name:  MP4DecResolveBus

        Purpose:        Get bus address for this picture

        Input:          DecContainer *dec_cont

        Output:         void

------------------------------------------------------------------------------*/
addr_t MP4DecResolveBus(DecContainer * dec_cont, u32 index ) {
  if( (i32)index < 0 )
    return 0;
  return dec_cont->StrmStorage.data[dec_cont->StrmStorage.
                                    p_pic_buf[index].data_index].bus_address;
}

/*------------------------------------------------------------------------------

        Function name:  MP4DecChangeDataIndex

        Purpose:        Move picture storage to point to a different physical
                        picture

        Input:          DecContainer *dec_cont

        Output:         void

------------------------------------------------------------------------------*/
void MP4DecChangeDataIndex( DecContainer * dec_cont, u32 to, u32 from) {

  dec_cont->StrmStorage.p_pic_buf[to].data_index =
    dec_cont->StrmStorage.p_pic_buf[from].data_index;
}

