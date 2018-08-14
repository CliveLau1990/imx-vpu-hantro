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

#include "vc1decapi.h"
#include "vc1hwd_decoder.h"
#include "vc1hwd_stream.h"
#include "vc1hwd_picture_layer.h"
#include "refbuffer.h"
#include "vc1hwd_regdrv.h"
#include "bqueue.h"
#include "vc1hwd_asic.h"
#include "input_queue.h"
#include "vc1hwd_storage.h"

/*------------------------------------------------------------------------------
    External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

#define SHOW1(p) (p[0]); p+=1;
#define SHOW4(p) (p[0]) | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p+=4;

#define    BIT0(tmp)  ((tmp & 1)   >>0);
#define    BIT1(tmp)  ((tmp & 2)   >>1);
#define    BIT2(tmp)  ((tmp & 4)   >>2);
#define    BIT3(tmp)  ((tmp & 8)   >>3);
#define    BIT4(tmp)  ((tmp & 16)  >>4);
#define    BIT5(tmp)  ((tmp & 32)  >>5);
#define    BIT6(tmp)  ((tmp & 64)  >>6);
#define    BIT7(tmp)  ((tmp & 128) >>7);

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

static u16x ValidateMetadata(swStrmStorage_t * storage,
                             const VC1DecMetaData *p_meta_data);
#ifdef USE_EXTERNAL_BUFFER
u16x AllocateMemories( decContainer_t *dec_cont,
                       swStrmStorage_t *storage,
                       const void *dwl );
#else
static u16x AllocateMemories( decContainer_t *dec_cont,
                              swStrmStorage_t *storage,
                              const void *dwl );
#endif

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function name: vc1hwdInit

        Functional description:
                    Initializes decoder and allocates memories.

        Inputs:
            dwl             Instance of decoder wrapper layer
            storage        Stream storage descriptor
            p_meta_data       Pointer to metadata information

        Outputs:
            None

        Returns:
            VC1HWD_METADATA_ERROR / VC1HWD_OK

------------------------------------------------------------------------------*/
u16x vc1hwdInit(const void *dwl, swStrmStorage_t *storage,
                const VC1DecMetaData *p_meta_data,
                u32 num_frame_buffers ) {
  u16x rv;

  ASSERT(storage);
  ASSERT(p_meta_data);
  UNUSED(dwl);

  /* check initialization data if not advanced profile stream */
  if (p_meta_data->profile != 8) {
    rv = ValidateMetadata(storage, p_meta_data);
    if (rv != VC1HWD_OK)
      return (VC1HWD_METADATA_ERROR);
  }
  /* Advanced profile */
  else {
    storage->max_bframes = 7;
    storage->profile = VC1_ADVANCED;
  }

  if( storage->max_bframes > 0 )
    storage->min_count = 1;
  else
    storage->min_count = 0;

  if( num_frame_buffers > 16 )
    num_frame_buffers = 16;

  storage->max_num_buffers = num_frame_buffers;

  storage->work0 = storage->work1 = INVALID_ANCHOR_PICTURE;
  storage->work_out_prev =
    storage->work_out = BqueueNext(
                          &storage->bq,
                          storage->work0,
                          storage->work1,
                          BQUEUE_UNUSED,
                          0 );
  storage->first_frame = HANTRO_TRUE;
  storage->picture_broken = HANTRO_FALSE;

  return (VC1HWD_OK);

}
/*------------------------------------------------------------------------------

    Function name: AllocateMemories

        Functional description:
            Allocates decoder internal memories

        Inputs:
            dec_cont    Decoder container
            storage    Stream storage descriptor
            dwl         Instance of decoder wrapper layer

        Outputs:
            None

        Returns:
            VC1HWD_MEMORY_FAIL / VC1HWD_OK

------------------------------------------------------------------------------*/
u16x AllocateMemories( decContainer_t *dec_cont,
                       swStrmStorage_t *storage,
                       const void *dwl ) {
  u16x i;
  u16x rv;
  u16x size;
  picture_t *p_pic = NULL;
  u32 buffers;

  if( storage->max_bframes > 0 ) {
    buffers = 3;
  } else {
    buffers = 2;
  }

  /* Calculate minimum amount of buffers */
  storage->parallel_mode2 = 0;
  dec_cont->pp_control.prev_anchor_display_index = BQUEUE_UNUSED;
  if( dec_cont->pp_instance ) { /* Combined mode used */

    /* If low-delay not set, then we might need extra buffers in
     * case we're using multibuffer mode. */
    if( storage->max_bframes > 0 ) {
      /*
      dec_cont->PPConfigQuery(dec_cont->pp_instance,
                                   &dec_cont->ppConfigQuery);
      if(dec_cont->ppConfigQuery.multiBuffer)
      */
      buffers = 4;
    }

    storage->num_pp_buffers = storage->max_num_buffers;
    storage->work_buf_amount = buffers; /* Use bare minimum in decoder */
    buffers = 2; /*storage->interlace ? 1 : 2;*/
    if( storage->num_pp_buffers < buffers )
      storage->num_pp_buffers = buffers;
  } else { /* Dec only or separate PP */
    storage->work_buf_amount = storage->max_num_buffers;
    storage->num_pp_buffers = 0;
    if( storage->work_buf_amount < buffers )
      storage->work_buf_amount = buffers;
  }
#ifndef USE_OUTPUT_RELEASE
  rv = BqueueInit(&storage->bq,
                  storage->work_buf_amount );
#else
  rv = BqueueInit2(&storage->bq,
                   storage->work_buf_amount );
#endif
  if(rv != HANTRO_OK) {
    (void)vc1hwdRelease(dwl,
                        storage);
    return (VC1HWD_MEMORY_FAIL);
  }

  rv = BqueueInit(&storage->bq_pp,
                  storage->num_pp_buffers );
  if(rv != HANTRO_OK) {
    (void)vc1hwdRelease(dwl,
                        storage);
    return (VC1HWD_MEMORY_FAIL);
  }

  /* Memory for all picture_t structures */
  /* One more element's space is alloced to avoid the overflow */
  p_pic = (picture_t*)DWLmalloc(sizeof(picture_t) * (16+1));
  if(p_pic == NULL) {
    (void)vc1hwdRelease(dwl,
                        storage);
    return (VC1HWD_MEMORY_FAIL);
  }

  (void)DWLmemset(p_pic, 0, sizeof(picture_t)*(16+1));

  /* set pointer to picture_t buffer */
  storage->p_pic_buf = (struct picture*)p_pic;

  size = storage->num_of_mbs*384;      /* size of YUV picture */

#ifndef USE_EXTERNAL_BUFFER
  /* memory for pictures */
  for (i = 0; i < storage->work_buf_amount; i++) {
    if (DWLMallocRefFrm(dwl, size, &p_pic[i].data) != 0) {
      (void)vc1hwdRelease(dwl, storage);
      return (VC1HWD_MEMORY_FAIL);
    }
    if (dec_cont->pp_enabled) {
      /* Add PP output buffers. */
      struct DWLLinearMem pp_buffer;
      u32 pp_width, pp_height, pp_stride, pp_buff_size;

      pp_width = (dec_cont->storage.pic_width_in_mbs * 16) >> dec_cont->dscale_shift_x;
      pp_height = (dec_cont->storage.pic_height_in_mbs * 16) >> dec_cont->dscale_shift_y;
      pp_stride = ((pp_width + 15) >> 4) << 4;
      pp_buff_size = pp_stride * pp_height * 3 / 2;
      if(DWLMallocLinear(dec_cont->dwl, pp_buff_size, &pp_buffer) != 0)
        return (VC1HWD_MEMORY_FAIL);

      dec_cont->storage.pp_buffer[i] = pp_buffer;
      InputQueueAddBuffer(dec_cont->pp_buffer_queue, &pp_buffer);
    }
    (void)DWLmemset(p_pic[i].data.virtual_address, 0 , size);
    /* init coded image size to max coded image size */
    p_pic[i].coded_width = storage->max_coded_width;
    p_pic[i].coded_height = storage->max_coded_height;
  }
#else
  if (dec_cont->pp_enabled) {
    for (i = 0; i < storage->work_buf_amount; i++) {
      if (DWLMallocRefFrm(dwl, size, &p_pic[i].data) != 0) {
        (void)vc1hwdRelease(dwl, storage);
        return (VC1HWD_MEMORY_FAIL);
      }
      (void)DWLmemset(p_pic[i].data.virtual_address, 0 , size);
      /* init coded image size to max coded image size */
      p_pic[i].coded_width = storage->max_coded_width;
      p_pic[i].coded_height = storage->max_coded_height;
    }
  }
#endif
  storage->p_mb_flags = (u8*)DWLmalloc(((storage->num_of_mbs+9)/10)*10);
  if (storage->p_mb_flags == NULL) {
    (void)vc1hwdRelease(dwl,
                        storage);
    return (VC1HWD_MEMORY_FAIL);
  }
  (void)DWLmemset(storage->p_mb_flags, 0, ((storage->num_of_mbs+9)/10)*10);

  /* bit plane coded data, 3 bits per macroblock, have to allocate integer
   * number of 4-byte words */
  rv = DWLMallocLinear(dwl,
                       (storage->num_of_mbs + 9)/10 * sizeof(u32),
                       &dec_cont->bit_plane_ctrl);
  if ((rv != 0) ||
      X170_CHECK_VIRTUAL_ADDRESS( dec_cont->bit_plane_ctrl.virtual_address ) ||
      X170_CHECK_BUS_ADDRESS( dec_cont->bit_plane_ctrl.bus_address )) {
    (void)vc1hwdRelease(dwl,
                        &dec_cont->storage);
    return (VC1HWD_MEMORY_FAIL);
  }

  /* If B pictures in the stream allocate space for direct mode mvs */
  if(dec_cont->storage.max_bframes) {
    /* allocate for even number of macroblock rows to accommodate direct
     * mvs of field pictures */
    if (dec_cont->storage.pic_height_in_mbs & 0x1) {
      rv = DWLMallocLinear(dec_cont->dwl,
                           ((dec_cont->storage.num_of_mbs +
                             dec_cont->storage.pic_width_in_mbs+7) & ~0x7) * 2 * sizeof(u32),
                           &dec_cont->direct_mvs);
    } else {
      rv = DWLMallocLinear(dec_cont->dwl,
                           ((dec_cont->storage.num_of_mbs+7) & ~0x7) * 2 * sizeof(u32),
                           &dec_cont->direct_mvs);
    }
    if (rv != 0 ||
        X170_CHECK_VIRTUAL_ADDRESS( dec_cont->direct_mvs.virtual_address ) ||
        X170_CHECK_BUS_ADDRESS( dec_cont->direct_mvs.bus_address )) {
      DWLFreeLinear(dwl, &dec_cont->bit_plane_ctrl);
      (void)vc1hwdRelease(dwl,
                          &dec_cont->storage);
      return (VC1HWD_MEMORY_FAIL);
    }
  } else
    dec_cont->direct_mvs.virtual_address = 0;

  return (VC1HWD_OK);
}

/*------------------------------------------------------------------------------

    Function name: ValidateMetadata

        Functional description:
                    Function checks that all necessary initialization metadata
                    are present in metadata structure and values are sane.

        Inputs:
            storage        Stream storage descriptor
            p_meta_data       Pointer to metadata information

        Outputs:

        Returns:
            VC1HWD_OK / VC1HWD_METADATA_ERROR

------------------------------------------------------------------------------*/
u16x ValidateMetadata(swStrmStorage_t *storage,const VC1DecMetaData *p_meta_data) {

  DWLHwConfig config;

  (void)DWLmemset(&config, 0, sizeof(DWLHwConfig));

  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_VC1_DEC);

  /* check metadata information */
  if ( p_meta_data->max_coded_width < storage->min_coded_width ||
       p_meta_data->max_coded_width > config.max_dec_pic_width ||
       p_meta_data->max_coded_height < storage->min_coded_height ||
       p_meta_data->max_coded_height > config.max_dec_pic_width ||
       (p_meta_data->max_coded_width & 0x1) ||
       (p_meta_data->max_coded_height & 0x1) ||
       p_meta_data->quantizer > 3 )
    return (VC1HWD_METADATA_ERROR);

  storage->cur_coded_width     = storage->max_coded_width
                                 = p_meta_data->max_coded_width;
  storage->cur_coded_height    = storage->max_coded_height
                                 = p_meta_data->max_coded_height;
  storage->pic_width_in_mbs  = (p_meta_data->max_coded_width+15) >> 4;
  storage->pic_height_in_mbs = (p_meta_data->max_coded_height+15) >> 4;
  storage->num_of_mbs       = (storage->pic_width_in_mbs *
                               storage->pic_height_in_mbs);

  if (storage->num_of_mbs > MAX_NUM_MBS)
    return(VC1HWD_METADATA_ERROR);

  storage->vs_transform       = p_meta_data->vs_transform ? 1 : 0;
  storage->overlap           = p_meta_data->overlap ? 1 : 0;
  storage->sync_marker        = p_meta_data->sync_marker ? 1 : 0;
  storage->frame_interp_flag   = p_meta_data->frame_interp ? 1 : 0;
  storage->quantizer         = p_meta_data->quantizer;

  storage->max_bframes        = p_meta_data->max_bframes;
  storage->fast_uv_mc          = p_meta_data->fast_uv_mc ? 1 : 0;
  storage->extended_mv        = p_meta_data->extended_mv ? 1 : 0;
  storage->multi_res          = p_meta_data->multi_res ? 1 : 0;
  storage->range_red          = p_meta_data->range_red ? 1 : 0;
  storage->dquant            = p_meta_data->dquant;
  storage->loop_filter        = p_meta_data->loop_filter ? 1 : 0;
  storage->profile           =
    (p_meta_data->profile == 0) ? VC1_SIMPLE : VC1_MAIN;

  /* is dquant valid */
  if (storage->dquant > 2)
    return (VC1HWD_METADATA_ERROR);


  /* Quantizer specification. > 3 is SMPTE reserved */
  if (p_meta_data->quantizer > 3)
    return (VC1HWD_METADATA_ERROR);

  /* maximum number of consecutive B frames. > 7 SMPTE reserved */
  if (p_meta_data->max_bframes > 7)
    return (VC1HWD_METADATA_ERROR);

  return (VC1HWD_OK);
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdDecode

        Functional description:
            Control for sequence and picture layer decoding

        Inputs:
            dec_cont    Decoder container
            storage    Stream storage descriptor
            stream_data  Descriptor for stream data

        Outputs:
            None

        Returns:
            VC1HWD_ERROR
            VC1HWD_END_OF_SEQ
            VC1HWD_USER_DATA_RDY
            VC1HWD_METADATA_ERROR
            VC1HWD_SEQ_HDRS_RDY
            VC1HWD_PIC_HDRS_RDY
            VC1HWD_FIELD_HDRS_RDY
            VC1HWD_ENTRY_POINT_HDRS_RDY
            VC1HWD_USER_DATA_RDY
            VC1HWD_NOT_CODED_PIC
            VC1HWD_MEMORY_FAIL
            VC1HWD_HDRS_ERROR

------------------------------------------------------------------------------*/
u16x vc1hwdDecode( decContainer_t *dec_cont,
                   swStrmStorage_t *storage,
                   strmData_t *stream_data ) {
  /* Variables */

  u32 start_code;
  u32 tmp;
  u32 read_bits_start, read_bits_end;
  u16x ret_val = HANTRO_OK;
  swStrmStorage_t tmp_storage;

  /* Code */

  ASSERT(storage);
  ASSERT(stream_data);

  /* ADVANCED PROFILE */
  if (storage->profile == VC1_ADVANCED) {
    storage->slice = HANTRO_FALSE;
    storage->missing_field = HANTRO_FALSE;
    do {
      /* Get Start Code */
      start_code = vc1hwdGetStartCode(stream_data);
      switch (start_code) {
      case SC_SEQ:
        DPRINT(("\nSc_seq found\n"));
        /* Don't skip seq layer decoding after the first
         * pic is successfully decoded to avoid seq dynamic change */
        tmp_storage = dec_cont->storage;
        ret_val = vc1hwdDecodeSequenceLayer(&tmp_storage, stream_data);
        if (ret_val == VC1HWD_SEQ_HDRS_RDY) {
          dec_cont->storage = tmp_storage;
          if( dec_cont->ref_buf_support ) {
            RefbuInit( &dec_cont->ref_buffer_ctrl, DEC_X170_MODE_VC1,
                       dec_cont->storage.pic_width_in_mbs,
                       dec_cont->storage.pic_height_in_mbs,
                       dec_cont->ref_buf_support );
          }
        }
        break;
      case SC_SEQ_UD:
        DPRINT(("\nSc_seq_ud found\n"));
        ret_val = vc1hwdGetUserData(storage, stream_data);
        ret_val = VC1HWD_USER_DATA_RDY;
        break;
      case SC_ENTRY_POINT:
        DPRINT(("\nSc_entry_point found\n"));

        /* Check that sequence headers are decoded succesfully */
        if (!(storage->hdrs_decoded & HDR_SEQ)) {
          ret_val = vc1hwdGetUserData(storage, stream_data);
          ret_val = VC1HWD_USER_DATA_RDY;
          break;
        }
        //    return VC1HWD_HDRS_ERROR;

        ret_val = vc1hwdDecodeEntryPointLayer(storage, stream_data);
        break;
      case SC_ENTRY_POINT_UD:
        DPRINT(("\nSc_entry_point_ud found\n"));
        ret_val = vc1hwdGetUserData(storage, stream_data);
        ret_val = VC1HWD_USER_DATA_RDY;
        break;
      case SC_FRAME:
        DPRINT(("\nSc_frame found\n"));
        /* Check that headers are decoded succesfully */
        if (storage->hdrs_decoded != HDR_BOTH) {
          ret_val = vc1hwdGetUserData(storage, stream_data);
          ret_val = VC1HWD_USER_DATA_RDY;
          break;
        }
        //    return VC1HWD_HDRS_ERROR;

        stream_data->slice_piclayer_emulation_bits = 0;
        read_bits_start = stream_data->strm_buff_read_bits;
        ret_val = vc1hwdDecodePictureLayerAP(storage, stream_data);

        read_bits_end = stream_data->strm_buff_read_bits;
        storage->pic_layer.pic_header_bits =
          read_bits_end - read_bits_start;

        storage->pic_layer.pic_header_bits -=
          stream_data->slice_piclayer_emulation_bits ;

        /* Set anchor frame/field inter/intra status */
        if( storage->pic_layer.fcm == FIELD_INTERLACE )
          tmp = storage->pic_layer.tff ^ 1;
        else
          tmp = 0;
        if( storage->pic_layer.pic_type == PTYPE_I )
          storage->anchor_inter[ tmp ] = HANTRO_FALSE;
        else if ( (storage->pic_layer.pic_type == PTYPE_P) ||
                  (storage->pic_layer.pic_type == PTYPE_Skip) )
          storage->anchor_inter[ tmp ] = HANTRO_TRUE;

        /* flag used to detect missing field */
        if( storage->pic_layer.fcm == FIELD_INTERLACE )
          storage->ff_start = HANTRO_TRUE;
        else
          storage->ff_start = HANTRO_FALSE;

        /* previous frame was field coded and
         * second field missing */
        if ( (storage->p_pic_buf[storage->work_out].fcm ==
              FIELD_INTERLACE) &&
             (storage->p_pic_buf[storage->work_out].is_first_field ==
              HANTRO_TRUE) ) {
          /* replace orphan 1.st field of previous frame in
           * frame buffer */
          if (storage->field_count)
            storage->field_count--;
          if (storage->outp_count)
            storage->outp_count--;
          BqueueDiscard( &storage->bq, storage->work_out );
          storage->work_out_prev =
            storage->work_out = storage->work0;
          DPRINT(("MISSING SECOND FIELD\n"));
          storage->missing_field = HANTRO_TRUE;
        }
        break;
      case SC_FRAME_UD:
        DPRINT(("\nSc_frame_ud found\n"));
        ret_val = vc1hwdGetUserData(storage, stream_data);
        ret_val = VC1HWD_USER_DATA_RDY;
        break;
      case SC_FIELD:
        DPRINT(("\nSc_field found\n"));
        /* If FCM at this point is other than field interlace,
         * then we have an extra field in the stream. Let's skip
         * it altogether and move on to the next SC. */
        if( storage->pic_layer.fcm != FIELD_INTERLACE )
          continue;

        /* Check that headers are decoded succesfully */
        if (storage->hdrs_decoded != HDR_BOTH) {
          ret_val = vc1hwdGetUserData(storage, stream_data);
          ret_val = VC1HWD_USER_DATA_RDY;
          break;
        }
        //    return VC1HWD_HDRS_ERROR;

        /* previous frame finished and field SC found
         * -> first field missing -> conceal */
        if( (storage->pic_layer.fcm == FIELD_INTERLACE) &&
            (storage->ff_start == HANTRO_FALSE)) {
          DPRINT(("MISSING FIRST FIELD\n"));
          storage->missing_field = HANTRO_TRUE;
          return VC1HWD_ERROR;
        }
        storage->ff_start = HANTRO_FALSE;
        stream_data->slice_piclayer_emulation_bits = 0;

        /* Substract field header bits from total length */
        storage->pic_layer.pic_header_bits -=
          storage->pic_layer.field_header_bits;

        ret_val = vc1hwdDecodeFieldLayer( storage,
                                          stream_data,
                                          HANTRO_FALSE );
        /* And add length of new header */
        storage->pic_layer.pic_header_bits +=
          storage->pic_layer.field_header_bits;

        storage->pic_layer.pic_header_bits -=
          stream_data->slice_piclayer_emulation_bits ;

        /* Set anchor field inter/intra status */
        if( storage->pic_layer.pic_type == PTYPE_I ) {
          storage->anchor_inter[ storage->pic_layer.tff ] =
            HANTRO_FALSE;
        } else if ( (storage->pic_layer.pic_type == PTYPE_P) ||
                    (storage->pic_layer.pic_type == PTYPE_Skip) ) {
          storage->anchor_inter[ storage->pic_layer.tff ] =
            HANTRO_TRUE;
        }
        break;
      case SC_FIELD_UD:
        DPRINT(("\nSc_field_ud found\n"));
        ret_val = vc1hwdGetUserData(storage, stream_data);
        ret_val = VC1HWD_USER_DATA_RDY;
        break;
      case SC_SLICE:
        DPRINT(("\nSc_slice found\n"));

        /* Check that headers are decoded succesfully */
        if (storage->hdrs_decoded != HDR_BOTH) {
          ret_val = vc1hwdGetUserData(storage, stream_data);
          ret_val = VC1HWD_USER_DATA_RDY;
          break;
        }
        //    return VC1HWD_HDRS_ERROR;

        /* If we found slice start code, and picture type is skipped,
         * it must be an error. */
        if( storage->pic_layer.pic_type == PTYPE_Skip )
          return VC1HWD_ERROR;

        ret_val = VC1HWD_PIC_HDRS_RDY;
        storage->slice = HANTRO_TRUE;

        /* unflush startcode */
        stream_data->strm_buff_read_bits-=32;
        stream_data->strm_curr_pos -= 4;
        break;
      case SC_SLICE_UD:
        DPRINT(("\nSc_slice_ud found\n"));
        ret_val = VC1HWD_USER_DATA_RDY;
        break;
      case SC_END_OF_SEQ:
        storage->first_frame = 0;
        DPRINT(("\nSc_end_of_seq found\n"));
        return (VC1HWD_END_OF_SEQ);
      case SC_NOT_FOUND:
        ret_val = vc1hwdFlushBits(stream_data, 8);
        break;
      default:
        EPRINT(("\nSc_error found\n"));
        ret_val = HANTRO_NOK;
        if (storage->hdrs_decoded != HDR_BOTH) {
          ret_val = vc1hwdGetUserData(storage, stream_data);
          ret_val = VC1HWD_USER_DATA_RDY;
          break;
        }
        //    return (VC1HWD_HDRS_ERROR);
        break;
      }
      /* check stream exhaustion */
      if(vc1hwdIsExhausted(stream_data)) {
        /* Notice!! VC1HWD_USER_DATA_RDY is used to return
         * VC1DEC_STRM_PROCESSED from the API in these cases */
        if ( (start_code == SC_NOT_FOUND) ||
             (ret_val == VC1HWD_USER_DATA_RDY) ) {
          return VC1HWD_USER_DATA_RDY;
        }

        EPRINT(("Stream exhausted!"));
        ret_val = HANTRO_NOK;
      }
    } while ( (ret_val != HANTRO_NOK) &&
              (ret_val != VC1HWD_METADATA_ERROR ) &&
              (ret_val != VC1HWD_SEQ_HDRS_RDY) &&
              (ret_val != VC1HWD_PIC_HDRS_RDY) &&
              (ret_val != VC1HWD_FIELD_HDRS_RDY) &&
              (ret_val != VC1HWD_ENTRY_POINT_HDRS_RDY) &&
              (ret_val != VC1HWD_USER_DATA_RDY) );
  }
  /* SIMPLE AND MAIN PROFILE */
  else {
    /* if size of the stream is either 1 or 0 bytes,
     * then picture is skipped and it shall be reconstructed
     * as a P frame which is identical to its reference frame */
    if (stream_data->strm_buff_size <= 1) {
      if(stream_data->strm_buff_size == 1) {
        ret_val = vc1hwdFlushBits(stream_data, 8);
      }
      /* just adjust picture buffer indexes
       * to correct positions */
      if (storage->max_bframes > 0) {
        /* Increase references to reference picture */
        if( storage->work1 != storage->work0) {
          storage->work1 = storage->work0;
        }
        /* Skipped frame shall be reconstructed as a
         * P-frame which is identical to its reference */
        storage->pic_layer.pic_type = PTYPE_P;

        BqueueDiscard( &storage->bq, storage->work_out );
        storage->work_out_prev = storage->work_out;
        storage->work_out = storage->work0;
#ifdef USE_OUTPUT_RELEASE
        BqueueWaitBufNotInUse( &dec_cont->storage.bq, dec_cont->storage.work_out);
#endif
        if(dec_cont->pp_enabled) {
          InputQueueWaitBufNotUsed(dec_cont->pp_buffer_queue,dec_cont->storage.p_pic_buf[dec_cont->storage.work_out].pp_data->virtual_address);
        }
        EPRINT(("Skipped picture with MAXBFRAMES>0!"));
        return(VC1HWD_ERROR);
      } else {
        return VC1HWD_NOT_CODED_PIC;
      }
    }
    /* Allocate memories */
    if (dec_cont->dec_stat == VC1DEC_INITIALIZED) {
#ifdef USE_OUTPUT_RELEASE
      if(dec_cont->pp_instance == NULL)
        BqueueWaitNotInUse(&storage->bq);
#endif
      if (dec_cont->pp_enabled)
        InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);

#ifndef USE_EXTERNAL_BUFFER
      tmp = AllocateMemories(dec_cont, storage, dec_cont->dwl);
      if (tmp != HANTRO_OK)
        return (VC1HWD_MEMORY_FAIL);
      else
        dec_cont->dec_stat = VC1DEC_RESOURCES_ALLOCATED;
#endif
    }
    /* Decode picture layer headers */
    ret_val = vc1hwdDecodePictureLayer(storage, stream_data);

    /* check stream exhaustion */
    if(vc1hwdIsExhausted(stream_data)) {
      EPRINT(("Stream exhausted!"));
      ret_val = HANTRO_NOK;
    }

    if (!dec_cont->same_pic_header) {
      if (storage->pic_layer.pic_type == PTYPE_I ||
          storage->pic_layer.pic_type == PTYPE_BI )
        storage->rnd = 0;
      else if (storage->pic_layer.pic_type == PTYPE_P)
        storage->rnd = 1 - storage->rnd;
    }

    if( storage->pic_layer.pic_type == PTYPE_I )
      storage->anchor_inter[ 0 ] = HANTRO_FALSE;
    else if ( storage->pic_layer.pic_type == PTYPE_P )
      storage->anchor_inter[ 0 ] = HANTRO_TRUE;

  }

  /* Allocate memories (advanced profile) */
  if (ret_val == VC1HWD_SEQ_HDRS_RDY) {
#ifndef USE_EXTERNAL_BUFFER
    /* Allocate memories */
    if (dec_cont->dec_stat != VC1DEC_INITIALIZED) {
      (void)vc1hwdRelease(dec_cont->dwl, storage);
      if(dec_cont->bit_plane_ctrl.virtual_address)
        DWLFreeLinear(dec_cont->dwl, &dec_cont->bit_plane_ctrl);
      if(dec_cont->direct_mvs.virtual_address)
        DWLFreeLinear(dec_cont->dwl, &dec_cont->direct_mvs);
    }
#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->pp_instance == NULL)
      BqueueWaitNotInUse(&storage->bq);
#endif
    if (dec_cont->pp_enabled)
      InputQueueWaitNotUsed(dec_cont->pp_buffer_queue);
    tmp = AllocateMemories(dec_cont, storage, dec_cont->dwl);
    if (tmp != HANTRO_OK)
      return (VC1HWD_MEMORY_FAIL);
    else
      dec_cont->dec_stat = VC1DEC_RESOURCES_ALLOCATED;
#endif
  }

  if( (ret_val == VC1HWD_PIC_HDRS_RDY) ||
      (ret_val == VC1HWD_FIELD_HDRS_RDY) ) {
    if( storage->pic_layer.pic_type == PTYPE_Skip ) {
      /* Increase references to reference picture */
      if( storage->work1 != storage->work0) {
        storage->work1 = storage->work0;
      }
      storage->work_out_prev = storage->work_out;
      storage->work_out = storage->work0;
      return VC1HWD_NOT_CODED_PIC;
    }

    /* field interlace */
    if (storage->pic_layer.fcm == FIELD_INTERLACE) {
      /* no reference picture for P field */
      if ( (storage->pic_layer.field_pic_type == FP_I_P) &&
           (storage->pic_layer.is_ff == HANTRO_FALSE) &&
           (storage->first_frame) &&
           (storage->pic_layer.num_ref == 0) &&
           (storage->pic_layer.ref_field != 0) ) {
        DPRINT(("No anchor picture for P field"));
        ret_val = HANTRO_NOK;
      }

      /* invalid field parity */
      if ( (storage->pic_layer.is_ff == HANTRO_FALSE) &&
           (storage->p_pic_buf[storage->work_out].is_top_field_first ==
            storage->pic_layer.top_field) ) {
        DPRINT(("Same parity for successive fields in the frame"));
        ret_val = HANTRO_NOK;
      }
    }
    /* if P frame, check that anchor available */
    if( (storage->pic_layer.is_ff == HANTRO_TRUE) &&
        (storage->pic_layer.pic_type == PTYPE_P) &&
        (storage->work0 == INVALID_ANCHOR_PICTURE) ) {
      DPRINT(("No anchor picture for P picture"));
      ret_val = HANTRO_NOK;
    } else if( dec_cont->storage.pic_layer.pic_type == PTYPE_B &&
               (dec_cont->storage.work1 == INVALID_ANCHOR_PICTURE ||
                dec_cont->storage.work0 == INVALID_ANCHOR_PICTURE)) {
      DPRINT(("No anchor pictures for B picture!"));
      ret_val = HANTRO_NOK;
    }
  }

  if (ret_val == HANTRO_NOK) {
    storage->picture_broken = HANTRO_TRUE;
    return(VC1HWD_ERROR);
  } else
    return(ret_val);

}

/*------------------------------------------------------------------------------

    Function name: vc1hwdRelease

        Functional description:
            Release allocated resources

        Inputs:
            dwl             DWL instance
            storage        Stream storage descriptor.

        Outputs:
            None

        Returns:
            HANTRO_OK

------------------------------------------------------------------------------*/
u16x vc1hwdRelease(const void *dwl,
                   swStrmStorage_t *storage) {

  /* Variables */
  u16x i;
  picture_t *p_pic = NULL;

  /* Code */
  ASSERT(storage);
#ifndef USE_OUTPUT_RELEASE
  BqueueRelease(&storage->bq);
#else
  BqueueRelease2(&storage->bq);
#endif

  p_pic = (picture_t*)storage->p_pic_buf;

  /* free all allocated picture buffers */
  if (p_pic != NULL) {
#ifndef USE_EXTERNAL_BUFFER
    for (i = 0; i < storage->work_buf_amount; i++) {
      DWLFreeRefFrm(dwl, &p_pic[i].data);
    }
    if (((decContainer_t *)(storage->dec_cont))->pp_enabled) {
      for (i = 0; i < storage->work_buf_amount; i++) {
        DWLFreeLinear(((decContainer_t *)(storage->dec_cont))->dwl,
                      &storage->pp_buffer[i]);
      }
      InputQueueRelease(((decContainer_t *)(storage->dec_cont))->pp_buffer_queue);
      ((decContainer_t *)(storage->dec_cont))->pp_buffer_queue = InputQueueInit(0);
      if (((decContainer_t *)(storage->dec_cont))->pp_buffer_queue == NULL) {
        return (VC1DEC_MEMFAIL);
      }
    }
#else
    if (((decContainer_t *)(storage->dec_cont))->pp_enabled) {
      for (i = 0; i < storage->work_buf_amount; i++) {
        DWLFreeRefFrm(dwl, &p_pic[i].data);
      }
    }
#endif
    DWLfree((picture_t*)storage->p_pic_buf);

    storage->p_pic_buf = NULL;
  }
  if(storage->p_mb_flags) {
    DWLfree(storage->p_mb_flags);
    storage->p_mb_flags = NULL;
  }

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    Function: vc1hwdUnpackMetaData

        Functional description:
            Unpacks metadata elements from buffer, when metadata is packed
            according to SMPTE VC-1 Standard Annexes J and L.

        Inputs:
            p_buffer         Buffer containing packed metadata

        Outputs:
            p_meta_data       Unpacked metadata

        Returns:
            HANTRO_OK       Metadata unpacked OK
            HANTRO_NOK      Metadata is in wrong format or indicates
                            unsupported tools

------------------------------------------------------------------------------*/
u16x vc1hwdUnpackMetaData(const u8 * p_buffer, VC1DecMetaData *p_meta_data) {

  /* Variables */

  u32 tmp1, tmp2;

  /* Code */

  DPRINT(("\nUnpacking META DATA"));
  DPRINT(("\nStruct_sequence_header_c\n"));

  /* Initialize sequence header C elements */
  p_meta_data->loop_filter =
    p_meta_data->multi_res =
      p_meta_data->fast_uv_mc =
        p_meta_data->extended_mv =
          p_meta_data->dquant =
            p_meta_data->vs_transform =
              p_meta_data->overlap =
                p_meta_data->sync_marker =
                  p_meta_data->range_red =
                    p_meta_data->max_bframes =
                      p_meta_data->quantizer =
                        p_meta_data->frame_interp = 0;

  tmp1 = SHOW1(p_buffer);
  tmp2  = BIT7(tmp1);
  tmp2 <<=1;
  tmp2 |= BIT6(tmp1);
  tmp2 <<=1;
  tmp2 |= BIT5(tmp1);
  tmp2 <<=1;
  tmp2 |= BIT4(tmp1);

  DPRINT(("PROFILE: \t %d\n",tmp2));

  if (tmp2 == 8) { /* Advanced profile */
    p_meta_data->profile = 8;
  } else {
    p_meta_data->profile = tmp2;

    tmp1 = SHOW1(p_buffer);

    tmp2 = BIT3(tmp1);
    DPRINT(("LOOPFILTER: \t %d\n",tmp2));
    p_meta_data->loop_filter = tmp2;
    tmp2 = BIT2(tmp1);
    DPRINT(("Reserved3: \t %d\n",tmp2));
#ifdef HANTRO_PEDANTIC_MODE
    if( tmp2 != 0 ) return HANTRO_NOK;
#endif /* HANTRO_PEDANTIC_MODE */
    tmp2 = BIT1(tmp1);
    DPRINT(("MULTIRES: \t %d\n",tmp2));
    p_meta_data->multi_res = tmp2;
    tmp2 = BIT0(tmp1);
    DPRINT(("Reserved4: \t %d\n",tmp2));
#ifdef HANTRO_PEDANTIC_MODE
    if( tmp2 != 1 ) return HANTRO_NOK;
#endif /* HANTRO_PEDANTIC_MODE */
    tmp1 = SHOW1(p_buffer);
    tmp2 = BIT7(tmp1);
    DPRINT(("FASTUVMC: \t %d\n",tmp2));
    p_meta_data->fast_uv_mc = tmp2;
    tmp2 = BIT6(tmp1);
    DPRINT(("EXTENDED_MV: \t %d\n",tmp2));
    p_meta_data->extended_mv = tmp2;
    tmp2 = BIT5(tmp1);
    tmp2 <<=1;
    tmp2 |= BIT4(tmp1);
    DPRINT(("DQUANT: \t %d\n",tmp2));
    p_meta_data->dquant = tmp2;
    if(p_meta_data->dquant > 2)
      return HANTRO_NOK;

    tmp2 = BIT3(tmp1);
    p_meta_data->vs_transform = tmp2;
    DPRINT(("VTRANSFORM: \t %d\n",tmp2));
    tmp2 = BIT2(tmp1);
    DPRINT(("Reserved5: \t %d\n",tmp2));
    /* Reserved5 needs to be checked, it affects stream syntax. */
    if( tmp2 != 0 ) return HANTRO_NOK;
    tmp2 = BIT1(tmp1);
    p_meta_data->overlap = tmp2;
    DPRINT(("OVERLAP: \t %d\n",tmp2));
    tmp2 = BIT0(tmp1);
    p_meta_data->sync_marker = tmp2;
    DPRINT(("SYNCMARKER: \t %d\n",tmp2));

    tmp1 = SHOW1(p_buffer);
    tmp2 = BIT7(tmp1);
    DPRINT(("RANGERED: \t %d\n",tmp2));
    p_meta_data->range_red = tmp2;
    tmp2 =  BIT6(tmp1);
    tmp2 <<=1;
    tmp2 |= BIT5(tmp1);
    tmp2 <<=1;
    tmp2 |= BIT4(tmp1);
    DPRINT(("MAXBFRAMES: \t %d\n",tmp2));
    p_meta_data->max_bframes = tmp2;
    tmp2 = BIT3(tmp1);
    tmp2 <<=1;
    tmp2 |= BIT2(tmp1);
    p_meta_data->quantizer = tmp2;
    DPRINT(("QUANTIZER: \t %d\n",tmp2));
    tmp2 = BIT1(tmp1);
    p_meta_data->frame_interp = tmp2;
    DPRINT(("FINTERPFLAG: \t %d\n",tmp2));
    tmp2 = BIT0(tmp1);
    DPRINT(("Reserved6: \t %d\n", tmp2));
    /* Reserved6 needs to be checked, it affects stream syntax. */
    if( tmp2 != 1 ) return HANTRO_NOK;
  }

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdErrorConcealment

        Functional description:
            Perform error concealment.

        Inputs:
            flush           HANTRO_TRUE    - Initialize picture to 128
                            HANTRO_FALSE   - Freeze to previous picture
            storage        Stream storage descriptor.

        Outputs:

        Returns:
            None

------------------------------------------------------------------------------*/
void vc1hwdErrorConcealment( const u16x flush,
                             swStrmStorage_t * storage ) {

  /* Variables */
  u32 tmp;
  u32 tmp_out;

  /* Code */

  tmp_out = storage->work_out;
  if(flush) {
    (void)DWLmemset(
      storage->p_pic_buf[ storage->work_out ].data.virtual_address,
      128,
      storage->num_of_mbs * 384 );
    /* if other buffer contains non-paired field -> throw away */
    if (storage->p_pic_buf[1-(i32)storage->work_out].fcm == FIELD_INTERLACE &&
        storage->p_pic_buf[1-(i32)storage->work_out].is_first_field == HANTRO_TRUE) {
      /* replace orphan 1.st field of previous frame in
       * frame buffer */
      if (storage->field_count)
        storage->field_count--;
      if (storage->outp_count)
        storage->outp_count--;
    }
  } else {
    /* we don't change indexes for broken B frames
     * because those are not buffered */
    if ( (storage->pic_layer.pic_type == PTYPE_I) ||
         (storage->pic_layer.pic_type == PTYPE_P) ||
         storage->intra_freeze ) {
      BqueueDiscard( &storage->bq, storage->work_out );
      storage->work_out = storage->work0;
      storage->work_out_prev = storage->work_out;
      if (!storage->p_pic_buf[tmp_out].is_first_field)
        tmp_out = storage->work_out;
    }
  }

  /* unbuffer first field if second field is corrupted */
  if ((!storage->pic_layer.is_ff && !storage->missing_field)) {
    /* replace orphan 1.st field of previous frame in
     * frame buffer */
    if (storage->field_count)
      storage->field_count--;
    if (storage->outp_count)
      storage->outp_count--;
  }
  /* mark fcm as frame interlaced because concealment is frame based */
  if (storage->pic_layer.fcm == FIELD_INTERLACE) {
    /* conceal whole frame */
    storage->pic_layer.fcm = FRAME_INTERLACE;
  }
  /* skip B frames after corrupted anchor frame */
  if ( (storage->pic_layer.pic_type == PTYPE_I) ||
       (storage->pic_layer.pic_type == PTYPE_P) ) {
    storage->skip_b = 2;
    tmp = storage->work_out;
  } else
    tmp = storage->prev_bidx; /* concealing B frame */

  /* both fields are concealed */
  ((picture_t*)storage->p_pic_buf)[tmp].is_first_field = HANTRO_FALSE;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdSeekFrameStart

        Functional description:

        Inputs:
            storage    Stream storage descriptor.
            p_strm_data   Input stream data.

        Outputs:
            None

        Returns:
            OK/NOK/END_OF_STREAM

------------------------------------------------------------------------------*/
u32 vc1hwdSeekFrameStart(swStrmStorage_t * storage,
                         strmData_t *p_strm_data ) {
  u32 rv;
  u32 sc;
  u32 tmp;
  u8 *p_strm;

  if (storage->profile == VC1_ADVANCED) {
    /* Seek next frame start */
    tmp = p_strm_data->bit_pos_in_word;
    if (tmp)
      rv = vc1hwdFlushBits(p_strm_data, 8-tmp);

    do {
      p_strm = p_strm_data->strm_curr_pos;

      /* Check that we have enough stream data */
      if (((p_strm_data->strm_buff_read_bits>>3)+4) <= p_strm_data->strm_buff_size) {
        sc = ( ((u32)p_strm[0]) << 24 ) +
             ( ((u32)p_strm[1]) << 16 ) +
             ( ((u32)p_strm[2]) << 8  ) +
             ( ((u32)p_strm[3]) );
      } else
        sc = 0;

      if (sc == SC_FRAME ||
          sc == SC_SEQ ||
          sc == SC_ENTRY_POINT ||
          sc == SC_END_OF_SEQ ||
          sc == END_OF_STREAM ) {
        break;
      }
    } while (vc1hwdFlushBits(p_strm_data, 8) == HANTRO_OK);
  } else {
    /* mark all stream processed for simple/main profiles */
    p_strm_data->strm_curr_pos = p_strm_data->p_strm_buff_start +
                                 p_strm_data->strm_buff_size;
  }

  if (vc1hwdIsExhausted(p_strm_data))
    rv = END_OF_STREAM;
  else
    rv = HANTRO_OK;

  return rv;
}

/*------------------------------------------------------------------------------

    Function name: BufferPicture

        Functional description:
            Perform error concealment.

        Inputs:
            dec_cont        Decoder container
            pic_to_buffer     Picture buffer index of pic to buffer.

        Outputs:

        Returns:
            HANTRO_OK       Picture buffered ok
            HANTRO_NOK      Buffer full

------------------------------------------------------------------------------*/
u16x vc1hwdBufferPicture( decContainer_t *dec_cont, u16x pic_to_buffer,
                          u16x buffer_b, u16x pic_id, u16x err_mbs ) {

  /* Variables */

  i32 i, j;
  u32 ff_index;
  swStrmStorage_t * storage = &dec_cont->storage;
  /* Code */
  ASSERT(storage);

  /* Index 0 for first field, 1 for second field */
  ff_index = (storage->pic_layer.is_ff) ? 0 : 1;

  if (storage->pic_layer.fcm == FIELD_INTERLACE) {
    storage->field_count++;
    /* for error concealment purposes */
    if (storage->field_count >= 2)
      storage->first_frame = HANTRO_FALSE;

    /* Just return if second field of the frame */
    if (ff_index) {
      /* store pic_id and number of err MBs for second field also */
      storage->out_pic_id[ff_index][storage->prev_outp_idx] = pic_id;
      storage->out_err_mbs[storage->prev_outp_idx] = err_mbs;

      return HANTRO_OK;
    } else {
      if ( (storage->field_count >=3) && (storage->outp_count >= 2) ) {
        if( storage->parallel_mode2) {
          if ( (storage->field_count >=5) && (storage->outp_count >= 3) ) {
            /* force output count to be 1 if first field
             * and outp_count == 2. This prevents buffer overflow
             * in some rare error cases */
            EPRINT("Picture buffer output count exceeded. Overwriting picture!!!");
            storage->outp_count = 2;
          }

        } else {
          /* force output count to be 1 if first field
           * and outp_count == 2. This prevents buffer overflow
           * in some rare error cases */
          EPRINT("Picture buffer output count exceeded. Overwriting picture!!!");
          storage->outp_count = 1;
        }
      }
    }
  } else {
    storage->field_count += 2;
    /* for error concealment purposes */
    if (storage->field_count >= 2)
      storage->first_frame = HANTRO_FALSE;
  }

  if ( storage->outp_count >= MAX_OUTPUT_PICS ) {
    return HANTRO_NOK;
  }

  if( buffer_b == 0 ) { /* Buffer I or P picture */
    i = storage->outp_idx + storage->outp_count;
    if( i >= MAX_OUTPUT_PICS ) i -= MAX_OUTPUT_PICS;
  } else { /* Buffer B picture */
    j = storage->outp_idx + storage->outp_count;
    i = j - 1;
    if( j >= MAX_OUTPUT_PICS ) j -= MAX_OUTPUT_PICS;
    if( i < 0 ) i += MAX_OUTPUT_PICS;
    /* Added check due to max 2 pic latency */
    else if( i >= MAX_OUTPUT_PICS ) i -= MAX_OUTPUT_PICS;

    storage->outp_buf[j] = storage->outp_buf[i];
    storage->out_pic_id[0][j] = storage->out_pic_id[0][i];
    storage->out_pic_id[1][j] = storage->out_pic_id[1][i];
    storage->out_err_mbs[j] = storage->out_err_mbs[i];
  }
  storage->prev_outp_idx = i;
  storage->outp_buf[i] = pic_to_buffer;
  storage->out_err_mbs[i] = err_mbs;
  storage->p_pic_buf[pic_to_buffer].buffered++;

  if (storage->pic_layer.fcm == FIELD_INTERLACE) {
    storage->out_pic_id[ff_index][i] = pic_id;
  } else {
    /* set same pic_id for both fields of frame */
    storage->out_pic_id[0][i] = pic_id;
    storage->out_pic_id[1][i] = pic_id;
  }

  storage->outp_count++;

#ifdef USE_OUTPUT_RELEASE
  dec_cont->fullness = storage->outp_count;
#endif

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdNextPicture

        Functional description:
            Perform error concealment.

        Inputs:
            storage        Stream storage descriptor.
            end_of_stream     Indicates whether end of stream has been reached.

        Outputs:
            p_next_picture    Next picture buffer index

        Returns:
            HANTRO_OK       Picture ok
            HANTRO_NOK      No more pictures

------------------------------------------------------------------------------*/
u16x vc1hwdNextPicture( swStrmStorage_t * storage, u16x * p_next_picture,
                        u32 *p_field_to_ret, u16x end_of_stream, u32 deinterlace,
                        u32* p_pic_id, u32* decode_id, u32* p_err_mbs) {

  /* Variables */

  u32 i;
  u32 min_count;

  /* Code */

  ASSERT(storage);
  ASSERT(p_next_picture);

  min_count = 0;
  /* Determine how many pictures we are willing to give out... */
  if(storage->min_count > 0 && !end_of_stream)
    min_count = 1;

  if(storage->parallel_mode2 && !end_of_stream)
    min_count = 2;

  /* when deinterlacing is ON, we don't give first field out
   * before second field is also decoded */
  if ((storage->field_count % 2) && deinterlace) {
    return HANTRO_NOK;
  }

  if (storage->outp_count <= min_count) {
    return HANTRO_NOK;
  }

  if (storage->interlace && !deinterlace) {
    if ((storage->field_count < 3) && !end_of_stream)
      return HANTRO_NOK;

    if ((storage->field_count < 5) && !end_of_stream &&
        storage->parallel_mode2 )
      return HANTRO_NOK;

    i = storage->outp_idx;
    *p_field_to_ret = storage->field_to_return;
    *p_next_picture = storage->outp_buf[i];
    *p_pic_id = storage->out_pic_id[storage->field_to_return][i];
    decode_id[0] = storage->out_pic_id[0][i];
    decode_id[1] = storage->out_pic_id[1][i];
    *p_err_mbs = storage->out_err_mbs[i];

    if (storage->field_to_return == 1) { /* return second field */
      storage->outp_count--;
      i++;
      if( i == MAX_OUTPUT_PICS ) i = 0;
      storage->outp_idx = i;
    }

    storage->field_to_return = 1-storage->field_to_return;
    storage->field_count--;
  } else {
    i = storage->outp_idx;
    storage->outp_count--;
    *p_pic_id = storage->out_pic_id[0][i];
    decode_id[0] = decode_id[1] = storage->out_pic_id[0][i];
    *p_err_mbs = storage->out_err_mbs[i];
    *p_next_picture = storage->outp_buf[i++];
    if( i == MAX_OUTPUT_PICS ) i = 0;
    storage->outp_idx = i;

    storage->field_count -= 2;
  }

  return HANTRO_OK;

}

/*------------------------------------------------------------------------------

    Function name: vc1hwdSetPictureInfo

        Functional description:
            Fill picture layer information to picture_t structure.

        Inputs:
            dec_cont        Container for the decoder
            pic_id           Current picture id

        Outputs:

        Returns:
            None

------------------------------------------------------------------------------*/
void vc1hwdSetPictureInfo( decContainer_t *dec_cont, u32 pic_id ) {

  /* Variables */
  u32 work_index;
  picture_t *p_pic;
  u32 ff_index;
  u32 pic_type;

  /* Code */
  ASSERT(dec_cont);

  p_pic = (picture_t*)dec_cont->storage.p_pic_buf;
  work_index = dec_cont->storage.work_out;

  p_pic[ work_index ].coded_width  = dec_cont->storage.cur_coded_width;
  p_pic[ work_index ].coded_height = dec_cont->storage.cur_coded_height;
  p_pic[ work_index ].range_red_frm = dec_cont->storage.pic_layer.range_red_frm;
  p_pic[ work_index ].fcm = dec_cont->storage.pic_layer.fcm;

  if (dec_cont->storage.pic_layer.fcm == PROGRESSIVE ||
      dec_cont->storage.pic_layer.fcm == FRAME_INTERLACE ) {
    p_pic[ work_index ].key_frame =
      ( dec_cont->storage.pic_layer.pic_type == PTYPE_I ) ?
      HANTRO_TRUE : HANTRO_FALSE;

    if(dec_cont->storage.pic_layer.pic_type == PTYPE_I)
      pic_type = DEC_PIC_TYPE_I;
    else if(dec_cont->storage.pic_layer.pic_type == PTYPE_P)
      pic_type = DEC_PIC_TYPE_P;
    else if(dec_cont->storage.pic_layer.pic_type == PTYPE_B)
      pic_type = DEC_PIC_TYPE_B;
    else
      pic_type = DEC_PIC_TYPE_BI;

    p_pic[ work_index ].pic_code_type[0] = pic_type;
    p_pic[ work_index ].pic_code_type[1] = pic_type;
  } else {
    p_pic[ work_index ].key_frame =
      ( dec_cont->storage.pic_layer.field_pic_type == FP_I_I ||
        dec_cont->storage.pic_layer.field_pic_type == FP_I_P ||
        dec_cont->storage.pic_layer.field_pic_type == FP_P_I) ?
      HANTRO_TRUE : HANTRO_FALSE;

    switch( dec_cont->storage.pic_layer.field_pic_type ) {
    case FP_I_I:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_I;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_I;
      break;
    case FP_I_P:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_I;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_P;
      break;
    case FP_P_I:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_P;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_I;
      break;
    case FP_P_P:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_P;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_P;
      break;
    case FP_B_B:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_B;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_B;
      break;
    case FP_B_BI:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_B;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_BI;
      break;
    case FP_BI_B:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_BI;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_B;
      break;
    case FP_BI_BI:
      p_pic[ work_index ].pic_code_type[0] = DEC_PIC_TYPE_BI;
      p_pic[ work_index ].pic_code_type[1] = DEC_PIC_TYPE_BI;
      break;
    default:
      EPRINT("Unknown field_pic_type!");
      break;
    }
  }
  p_pic[ work_index ].tiled_mode      = dec_cont->tiled_reference_enable;
  p_pic[ work_index ].range_map_yflag  = dec_cont->storage.range_map_yflag;
  p_pic[ work_index ].range_map_y      = dec_cont->storage.range_map_y;
  p_pic[ work_index ].range_map_uv_flag = dec_cont->storage.range_map_uv_flag;
  p_pic[ work_index ].range_map_uv     = dec_cont->storage.range_map_uv;

  p_pic[ work_index ].is_first_field = dec_cont->storage.pic_layer.is_ff;
  p_pic[ work_index ].is_top_field_first = dec_cont->storage.pic_layer.tff;
  p_pic[ work_index ].rff = dec_cont->storage.pic_layer.rff;
  p_pic[ work_index ].rptfrm = dec_cont->storage.pic_layer.rptfrm;

  /* fill correct field structure */
  ff_index = (dec_cont->storage.pic_layer.is_ff) ? 0 : 1;
  p_pic[ work_index ].field[ff_index].int_comp_f =
    dec_cont->storage.pic_layer.int_comp_field;
  p_pic[ work_index ].field[ff_index].i_scale_a =
    dec_cont->storage.pic_layer.i_scale;
  p_pic[ work_index ].field[ff_index].i_shift_a =
    dec_cont->storage.pic_layer.i_shift;
  p_pic[ work_index ].field[ff_index].i_scale_b =
    dec_cont->storage.pic_layer.i_scale2;
  p_pic[ work_index ].field[ff_index].i_shift_b =
    dec_cont->storage.pic_layer.i_shift2;
  p_pic[ work_index ].field[ff_index].type =
    dec_cont->storage.pic_layer.pic_type;
  p_pic[ work_index ].field[ff_index].pic_id = pic_id;
  /* reset post-processing status */
  p_pic[ work_index ].field[ff_index].dec_pp_stat = NONE;

}

/*------------------------------------------------------------------------------

    Function name: vc1hwdUpdateWorkBufferIndexes

        Functional description:
            Check anchor availability and update picture buffer work indexes.

        Inputs:
            dec_cont        Container for the decoder.
            is_bpic          Is current picture B or BI picture

        Outputs:

        Returns:
            None

------------------------------------------------------------------------------*/
void vc1hwdUpdateWorkBufferIndexes( decContainer_t *dec_cont, u32 is_bpic ) {
  swStrmStorage_t * storage = &dec_cont->storage;
  u32 i, flag;

  if (storage->pic_layer.is_ff == HANTRO_TRUE) {
    u32 work0, work1;

    /* Determine work buffers (updating is done only after the frame
     * so here we assume situation after decoding frame */
    if( is_bpic ) {
      work0 = storage->work0;
      work1 = storage->work1;
    } else {
      work0 = storage->work_out;
      work1 = storage->work0;
    }
    /* Get free buffer */
    storage->work_out_prev = storage->work_out;
#ifdef USE_OUTPUT_RELEASE
    if(dec_cont->pp_instance == NULL) {
      storage->work_out = BqueueNext2(
                            &storage->bq,
                            work0,
                            work1,
                            BQUEUE_UNUSED,
                            is_bpic );
      if(storage->work_out == (u32)0xFFFFFFFFU)
        return;
      storage->p_pic_buf[storage->work_out].first_show = 1;
    } else {
      storage->work_out = BqueueNext(
                            &storage->bq,
                            work0,
                            work1,
                            BQUEUE_UNUSED,
                            is_bpic );
    }
#else
    storage->work_out = BqueueNext(
                          &storage->bq,
                          work0,
                          work1,
                          BQUEUE_UNUSED,
                          is_bpic );
#endif
    if (dec_cont->pp_enabled) {
      do {
        dec_cont->storage.p_pic_buf[dec_cont->storage.work_out].pp_data = InputQueueGetBuffer(dec_cont->pp_buffer_queue, 1);
        flag = 0;
        for (i = 0; i < MAX_OUTPUT_PICS; i++) {
          if ((dec_cont->storage.p_pic_buf[dec_cont->storage.outp_buf[i]].pp_data) &&
              dec_cont->storage.p_pic_buf[dec_cont->storage.outp_buf[i]].buffered &&
              (dec_cont->storage.outp_buf[i] != dec_cont->storage.work_out) &&
              (dec_cont->storage.p_pic_buf[dec_cont->storage.outp_buf[i]].pp_data ==
               dec_cont->storage.p_pic_buf[dec_cont->storage.work_out].pp_data)) {
            flag = 1;
            InputQueueReturnBuffer(dec_cont->pp_buffer_queue, dec_cont->storage.p_pic_buf[dec_cont->storage.work_out].pp_data->virtual_address);
          }
        }
      } while (flag == 1);
    }
    Vc1DecPpNextInput( &storage->dec_pp,
                       storage->pic_layer.fcm != FIELD_INTERLACE );

    if( is_bpic ) {
      storage->prev_bidx = storage->work_out;

    }
  }
}

