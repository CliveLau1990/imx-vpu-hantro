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

#include "mp4dechwd_strmdec.h"
#include "mp4dechwd_utils.h"
#include "mp4dechwd_headers.h"
#include "mp4dechwd_vop.h"
#include "mp4dechwd_videopacket.h"
#include "mp4dechwd_shortvideo.h"
#include "mp4dechwd_generic.h"
#include "mp4dechwd_error_conceal.h"
#include "workaround.h"
#include "mp4debug.h"
/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum {
  CONTINUE
};

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

u32 StrmDec_PrepareConcealment(DecContainer * dec_container, u32 start_code);

/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_DecoderInit

        Purpose: initialize stream decoding related parts of DecContainer

        Input:
            Pointer to DecContainer structure
                -initializes StrmStorage

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

void StrmDec_DecoderInit(DecContainer * dec_container) {
  /* initialize video packet qp to 1 -> reasonable qp value if first vop
   * has to be concealed */
  dec_container->StrmStorage.vp_qp = 1;
  dec_container->packed_mode = 0;
  dec_container->StrmStorage.reload_quant_matrices = HANTRO_FALSE;
}

/*------------------------------------------------------------------------------

   5.2  Function name: StrmDec_Decode

        Purpose: Decode MPEG4 stream. Continues decoding until END_OF_STREAM
        encountered or whole VOP decoded. Returns after decoding of VOL header.
        Also returns after checking source format in case short video header
        stream is decoded.

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmStorage
                -uses and updates StrmDesc
                -uses and updates VopDesc
                -uses Hdrs
                -uses MbSetDesc

        Output:
            DEC_RDY if everything was ok but no VOP finished
            DEC_HDR_RDY if headers decoded
            DEC_HDR_RDY_BUF_NOT_EMPTY if headers decoded but buffer not empty
            DEC_VOP_RDY if whole VOP decoded
            DEC_VOP_RDY_BUF_NOT_EMPTY if whole VOP decoded but buffer not empty
            DEC_END_OF_STREAM if eos encountered while decoding
            DEC_OUT_OF_BUFFER if asic input rlc buffer full
            DEC_ERROR if such an error encountered that recovery needs initial
            headers (in short video case only vop_with_sv_header needed)

------------------------------------------------------------------------------*/

u32 StrmDec_Decode(DecContainer * dec_container) {

  u32 tmp = 0;
  u32 status;
  u32 start_code = 0;
#ifndef HANTRO_PEDANTIC_MODE
  UNUSED(tmp);
#endif  /* HANTRO_PEDANTIC_MODE */

  /* flag to prevent concealment after failure in decoding of repeated
   * headers (VOS,VISO,VO,VOL) */
  u32 header_failure;

  MP4DEC_DEBUG(("entry StrmDec_Decode\n"));

  status = HANTRO_OK;
  if(dec_container->StrmStorage.status == STATE_OK)
    header_failure = HANTRO_FALSE;
  else
    header_failure = HANTRO_TRUE;

  /* make sure that rlc buffer is large enough for decoding */
  ASSERT((dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) ||
         !dec_container->Hdrs.data_partitioned ||
         (dec_container->MbSetDesc.rlc_data_buffer_size >= 1750));
  ASSERT((dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) ||
         (dec_container->rlc_mode == 0) ||
         (dec_container->MbSetDesc.rlc_data_buffer_size >= 64));
  ASSERT(!dec_container->Hdrs.data_partitioned ||
         dec_container->rlc_mode);

  if( dec_container->StrmStorage.custom_strm_headers ) {
    return StrmDec_DecodeCustomHeaders( dec_container );
  }

  /* reset last sync pointer if in the beginning of stream */
  if(dec_container->StrmDesc.strm_curr_pos ==
      dec_container->StrmDesc.p_strm_buff_start) {
    dec_container->StrmStorage.p_last_sync =
      dec_container->StrmDesc.p_strm_buff_start;
  }
  /* initialize p_rlc_data_vp_addr pointer (pointer to beginning of rlc data of
   * current video packet) */
  if (dec_container->StrmStorage.status == STATE_OK) {
    dec_container->MbSetDesc.p_rlc_data_vp_addr =
      dec_container->MbSetDesc.p_rlc_data_curr_addr;
    dec_container->MbSetDesc.odd_rlc_vp =
      dec_container->MbSetDesc.odd_rlc;
  }

  /* reset error counter in the beginning of VOP */
  if(dec_container->StrmStorage.vp_mb_number == 0) {
    dec_container->StrmStorage.num_err_mbs = 0;
  }

  /* keep decoding till something ready or something wrong */
  do {
    if(dec_container->StrmStorage.status == STATE_OK) {
      start_code = StrmDec_GetStartCode(dec_container);
      if(start_code == END_OF_STREAM) {
        if(dec_container->StrmStorage.strm_dec_ready == HANTRO_TRUE) {
          return (DEC_END_OF_STREAM);
        } else {
          return (DEC_ERROR);
        }
      }
    }
    /* sync lost -> find sync and conceal */
    else {
      start_code = StrmDec_FindSync(dec_container);
      /* return DEC_END_OF_STREAM only if resync markers are enabled
       * or vop has not been started yet (header not
       * decoded successfully), return DEC_ERROR if decoder not ready,
       * otherwise VOP is concealed */
      if(start_code == END_OF_STREAM) {
        if(dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) {
          return (DEC_ERROR);
        } else if(!dec_container->Hdrs.resync_marker_disable ||
                  (dec_container->StrmStorage.valid_vop_header ==
                   HANTRO_FALSE)) {
          return (DEC_END_OF_STREAM);
        }
      }

      if(dec_container->StrmStorage.strm_dec_ready == HANTRO_TRUE) {
        /* concealment if sync loss was not due to errors in decoding
         * of initial headers (repeated headers as strm_dec_ready is
         * true here) */
        if(start_code == SC_RESYNC && header_failure == HANTRO_TRUE)
          header_failure = HANTRO_FALSE;

        if(header_failure == HANTRO_FALSE && dec_container->rlc_mode &&
            dec_container->VopDesc.vop_coding_type != BVOP) {
          if(dec_container->MbSetDesc.ctrl_data_mem.virtual_address ==
              NULL) {
            return DEC_VOP_HDR_RDY_ERROR;
          }
          status = StrmDec_PrepareConcealment(dec_container,
                                              start_code);
        } else {
          dec_container->StrmStorage.status = STATE_OK;
          status = CONTINUE;
        }

        dec_container->StrmStorage.start_code_loss = HANTRO_FALSE;

        /* decoding of VOP finished -> return */
        if(status == DEC_VOP_RDY) {
          if(start_code == END_OF_STREAM) {
            return (DEC_VOP_RDY);
          } else {
            return (DEC_VOP_RDY_BUF_NOT_EMPTY);
          }
        }
      } else {
        dec_container->StrmStorage.status = STATE_OK;
        dec_container->StrmStorage.start_code_loss = HANTRO_FALSE;
      }
    }

    header_failure = HANTRO_FALSE;

    switch (start_code) {
    case SC_VO_START:
      break;
    case SC_VOS_START:
    case SC_VISO_START:
    case SC_VOL_START:

      dec_container->StrmStorage.mpeg4_video = HANTRO_TRUE;
      if((dec_container->Hdrs.lock == HANTRO_FALSE) &&
          (dec_container->StrmStorage.strm_dec_ready == HANTRO_TRUE)) {
        StrmDec_DecoderInit(dec_container);
        dec_container->StrmStorage.p_last_sync =
          dec_container->StrmDesc.strm_curr_pos;
        if (start_code == SC_VOL_START) {
          dec_container->tmp_hdrs = dec_container->Hdrs;
          StrmDec_ClearHeaders(&(dec_container->Hdrs));
        }
      }
      status = StrmDec_DecodeHdrs(dec_container, start_code);
      if (status != HANTRO_OK &&
          start_code == SC_VOL_START) {
        dec_container->Hdrs = dec_container->tmp_hdrs;
      }

      if(dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE ||
          dec_container->StrmStorage.interlaced !=
          dec_container->Hdrs.interlaced ||
          dec_container->StrmStorage.low_delay !=
          dec_container->Hdrs.low_delay ||
          dec_container->StrmStorage.video_object_layer_width !=
          dec_container->Hdrs.video_object_layer_width ||
          dec_container->StrmStorage.video_object_layer_height !=
          dec_container->Hdrs.video_object_layer_height ) {
        /* VOL decoded -> set vop size parameters and set
         * strm_dec_ready. Return control to caller with
         * indication whether buffer is empty or not */
        if(dec_container->Hdrs.last_header_type == SC_VOL_START) {
          /* error in header decoding and decoder not ready (i.e.
           * this is not repetition of header info) -> return
           * control to caller */
          if(status != HANTRO_OK) {
            dec_container->StrmStorage.status = STATE_SYNC_LOST;
            if(status != END_OF_STREAM)
              return (DEC_ERROR_BUF_NOT_EMPTY);
            else
              return (DEC_ERROR);
          }

          dec_container->VopDesc.vop_width =
            (dec_container->Hdrs.video_object_layer_width + 15) >> 4;
          dec_container->VopDesc.vop_height =
            (dec_container->Hdrs.video_object_layer_height + 15) >> 4;
          dec_container->VopDesc.total_mb_in_vop =
            dec_container->VopDesc.vop_width *
            dec_container->VopDesc.vop_height;

          dec_container->StrmStorage.strm_dec_ready = HANTRO_TRUE;

          /* store interlace flag, low_delay flag and image dimensions
           * to see if they change */
          dec_container->StrmStorage.interlaced =
            dec_container->Hdrs.interlaced;
          dec_container->StrmStorage.low_delay =
            dec_container->Hdrs.low_delay;
          dec_container->StrmStorage.video_object_layer_width =
            dec_container->Hdrs.video_object_layer_width;
          dec_container->StrmStorage.video_object_layer_height =
            dec_container->Hdrs.video_object_layer_height;

          if(IS_END_OF_STREAM(dec_container))
            return (DEC_HDRS_RDY);
          else
            return (DEC_HDRS_RDY_BUF_NOT_EMPTY);
        }
        /* check if short video header -> continue decoding.
         * Otherwise return ontrol */
        /*
        else if(StrmDec_ShowBits(dec_container, 22) != SC_SV_START)
        {
            return (DEC_RDY);
        }
        */
      } else if (dec_container->Hdrs.quant_type)
        dec_container->StrmStorage.reload_quant_matrices = HANTRO_TRUE;
      /* set flag to prevent concealment of vop (lost just repeated
       * header information and should be able to decode following
       * vops with no problems) */
      else if(status != HANTRO_OK) {
        header_failure = HANTRO_TRUE;
      }
      break;

    case SC_GVOP_START:
      status = StrmDec_DecodeGovHeader(dec_container);
      break;

    case SC_VOP_START:
      /*MP4DEC_DEBUG(("before vop decode pDecCont->MbSetDesc.pCtrl %x %x\n",
       * (u32)dec_container->MbSetDesc.pCtrlDataAddr,
       * *dec_container->MbSetDesc.pCtrlDataAddr)); */
      if (dec_container->rlc_mode) {
        status = StrmDec_DecodeVop(dec_container);
        if(dec_container->VopDesc.vop_coding_type == BVOP &&
            status == HANTRO_OK) {
          if(dec_container->Hdrs.low_delay)
            return(DEC_VOP_SUPRISE_B);
          else
            return(DEC_VOP_HDR_RDY);
        }
      } else {
        status = StrmDec_DecodeVopHeader(dec_container);
        if(status == HANTRO_OK) {
          if(dec_container->Hdrs.low_delay &&
              dec_container->VopDesc.vop_coding_type == BVOP) {
            return(DEC_VOP_SUPRISE_B);
          } else
            return(DEC_VOP_HDR_RDY);
        }
      }
      /*MP4DEC_DEBUG(("after vop decode pDecCont->MbSetDesc.pCtrl %x %x\n",
       * (u32)dec_container->MbSetDesc.pCtrlDataAddr,
       * *dec_container->MbSetDesc.pCtrlDataAddr)); */
      break;

    case SC_RESYNC:
      /* for REVIEW */
      /* TODO Decode only if rlc_mode */
      if(dec_container->StrmStorage.short_video == HANTRO_TRUE) {
        dec_container->StrmStorage.gob_resync_flag = HANTRO_TRUE;

        /* TODO change, */
        /* error situation */
        status = StrmDec_DecodeGobLayer(dec_container); /* REMOVE */
      } else {
        status = StrmDec_DecodeVideoPacket(dec_container);
      }
      break;

    case SC_SV_START:
      /* Re-init workarounds for H263/SVH data */
      InitWorkarounds(2, &dec_container->workarounds);
      /* TODO remove */
      if (dec_container->rlc_mode) {
        status = StrmDec_DecodeShortVideo(dec_container);
      } else {
        status = StrmDec_DecodeShortVideoHeader(dec_container);
        if (status == HANTRO_OK &&
            dec_container->StrmStorage.strm_dec_ready == HANTRO_TRUE) {
          return(DEC_VOP_HDR_RDY);
        }
      }
      if(dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) {
        MP4DEC_DEBUG(("dec_container->SvDesc.source_format %d \n",
                      dec_container->SvDesc.source_format));
        /* first visit and source format set -> return */
        if((status == HANTRO_OK) && dec_container->SvDesc.source_format) {
          dec_container->StrmStorage.strm_dec_ready = HANTRO_TRUE;
          return (DEC_HDRS_RDY_BUF_NOT_EMPTY);
        } else {
          dec_container->StrmStorage.status = STATE_SYNC_LOST;
          if(status == END_OF_STREAM) {
            return (DEC_ERROR);
          } else
            return (DEC_ERROR_BUF_NOT_EMPTY);
        }
      }
      break;

    case SC_SORENSON_START:
      status = StrmDec_DecodeSorensonSparkHeader(dec_container);
      if (status == HANTRO_OK &&
          dec_container->StrmStorage.strm_dec_ready == HANTRO_TRUE)
        return(DEC_VOP_HDR_RDY);
      if(dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) {
        MP4DEC_DEBUG(("dec_container->SvDesc.source_format %d \n",
                      dec_container->SvDesc.source_format));
        /* first visit and source format set -> return */
        if(status == HANTRO_OK) {
          dec_container->StrmStorage.strm_dec_ready = HANTRO_TRUE;
          return (DEC_HDRS_RDY_BUF_NOT_EMPTY);
        } else {
          dec_container->StrmStorage.status = STATE_SYNC_LOST;
          if(status == END_OF_STREAM)
            return (DEC_ERROR);
          else
            return (DEC_ERROR_BUF_NOT_EMPTY);
        }
      }
      break;

    case SC_SV_END:
      /* remove stuffing */
      status = HANTRO_OK;
      if(dec_container->StrmDesc.bit_pos_in_word) {
        tmp = StrmDec_GetBits(dec_container,
                              8 -
                              dec_container->StrmDesc.bit_pos_in_word);
#ifdef HANTRO_PEDANTIC_MODE
        if(tmp) {
          status = HANTRO_NOK;
        }
#endif
      }
      break;

    case SC_VOS_END:
      return (DEC_VOS_END);

    case SC_NOT_FOUND:
      /* start code not found and decoding short video stream ->
       * try to decode gob layer */ /* TODO REMOVE, RESYNC TO SV_HEADER*/
      if(dec_container->rlc_mode &&
          dec_container->StrmStorage.short_video == HANTRO_TRUE) {
        dec_container->StrmStorage.gob_resync_flag = HANTRO_FALSE;
        status = StrmDec_DecodeGobLayer(dec_container);
      } else {
        if(IS_END_OF_STREAM(dec_container))
          return (DEC_VOP_RDY);
        else {
          status = HANTRO_NOK;
          header_failure = HANTRO_TRUE;
          dec_container->StrmStorage.status = STATE_SYNC_LOST;
          continue;
        }
      }
      break;

    default:
      if(dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) {
        dec_container->StrmStorage.status = STATE_SYNC_LOST;
        if(IS_END_OF_STREAM(dec_container))
          return (DEC_ERROR);
        else
          return (DEC_ERROR_BUF_NOT_EMPTY);
      }
    }

    if(status != HANTRO_OK) {
      if (!dec_container->rlc_mode &&
          (start_code == SC_VOP_START || start_code == SC_SV_START)) {
        return(DEC_VOP_HDR_RDY_ERROR);
      }
      dec_container->StrmStorage.status = STATE_SYNC_LOST;
    }
    /* status HANTRO_OK in all cases below */
    /* decoding ok, whole VOP decoded -> return DEC_VOP_RDY */
    else if((dec_container->StrmStorage.strm_dec_ready == HANTRO_TRUE) &&
            (dec_container->StrmStorage.vp_mb_number ==
             dec_container->VopDesc.total_mb_in_vop)) {
      if(IS_END_OF_STREAM(dec_container)) {
        return (DEC_VOP_RDY);
      } else {
        return (DEC_VOP_RDY_BUF_NOT_EMPTY);
      }
    }
    /* decoding ok but stream ends -> get away */
    else if(IS_END_OF_STREAM(dec_container)) {
      return (DEC_RDY);
    }
  }
  /*lint -e(506) */ while(1);

  /* execution never reaches this point (hope so) */
  /*lint -e(527) */ return (DEC_END_OF_STREAM);
  /*lint -restore */
}

/*------------------------------------------------------------------------------

   5.3  Function name: StrmDec_PrepareConcealment

        Purpose: Set data needed by error concealment and call concealment

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmStorage
                -uses and updates StrmDesc
                -uses VopDesc
                -updates MBDesc
                -uses MbSetDesc
            start_code

        Output:
            CONTINUE to indicate that decoding should be continued
            DEC_VOP_RDY to indicate that decoding of VOP was finished

------------------------------------------------------------------------------*/

u32 StrmDec_PrepareConcealment(DecContainer * dec_container, u32 start_code) {

  u32 i;
  u32 tmp;
  u32 first, last;

  /* first mb to be concealed */
  first = dec_container->StrmStorage.vp_mb_number;

  if(start_code == SC_RESYNC) {
    /* determine macro block number of next video packet */
    if(dec_container->StrmStorage.short_video == HANTRO_FALSE) {
      last = StrmDec_CheckNextVpMbNumber(dec_container);
    } else {
      last = StrmDec_CheckNextGobNumber(dec_container) *
             dec_container->VopDesc.vop_width;
    }

    /* last equal to zero -> incorrect number found from stream, cannot
     * do anything */
    if(!last) {
      return (CONTINUE);
    }

    /* mb number less than previous by macro_blocks_in_vop/8 or start code
     * loss detected (expection: if first is zero -> previous vop was
     * successfully decoded and we may have lost e.g. GVOP start code and
     * header) -> consider VOP start code and header lost, conceal rest of
     * the VOP. Beginning of next VOP will be concealed on next invocation
     * -> status left as STATE_SYNC_LOST */
    if(((last < first) &&
        ((first - last) >
         (dec_container->VopDesc.total_mb_in_vop >> 3))) ||
        (first &&
         (dec_container->StrmStorage.start_code_loss == HANTRO_TRUE))) {
      /* conceal rest of the vop */
      last = dec_container->VopDesc.total_mb_in_vop;
      /* return resync marker into stream */
      (void)StrmDec_UnFlushBits(dec_container,
                                dec_container->StrmStorage.
                                resync_marker_length);
      /* set last sync to beginning of marker -> will be found next
       * time (starting search before last sync point prohibited) */
      dec_container->StrmStorage.p_last_sync =
        dec_container->StrmDesc.strm_curr_pos;

    }
    /* cannot conceal -> continue and conceal at next sync point */
    else if(last < first) {
      return (CONTINUE);
    }
    /* macro block number in the stream matches expected -> continue
     * decoding without concealment */
    else if(last == first) {
      dec_container->StrmStorage.status = HANTRO_OK;
      dec_container->MbSetDesc.p_rlc_data_curr_addr =
        dec_container->MbSetDesc.p_rlc_data_vp_addr;
      dec_container->MbSetDesc.odd_rlc =
        dec_container->MbSetDesc.odd_rlc_vp;
      return (CONTINUE);
    }

    /* data partitioned and first partition ok -> texture lost
     * (vp_num_mbs set by PartitionedMotionTexture decoding
     * function right after detection of motion/dc marker) */
    if(dec_container->Hdrs.data_partitioned &&
        ((dec_container->StrmStorage.vp_mb_number +
          dec_container->StrmStorage.vp_num_mbs) == last))
      tmp = 0x1;
    /* otherwise everything lost */
    else
      tmp = 0x3;

    /* set error status for all macro blocks to be concealed */
    for(i = first; i < last; i++)
      dec_container->MBDesc[i].error_status = tmp;

    /*last = dec_container->VopDesc.total_mb_in_vop;*/
    (void)StrmDec_ErrorConcealment(dec_container, first, last - 1);

    /* set rlc buffer pointer to beginning of concealed area */
    if(dec_container->MbSetDesc.p_rlc_data_curr_addr !=
        dec_container->MbSetDesc.p_rlc_data_addr) {
      dec_container->MbSetDesc.p_rlc_data_curr_addr =
        dec_container->MbSetDesc.p_rlc_data_vp_addr;
      dec_container->MbSetDesc.odd_rlc =
        dec_container->MbSetDesc.odd_rlc_vp;
      /* half of the word written with corrupted data -> reset */
      if (dec_container->MbSetDesc.odd_rlc) {
        *dec_container->MbSetDesc.p_rlc_data_curr_addr &= 0xFFFF0000;
      }
    }

    if(last == dec_container->VopDesc.total_mb_in_vop) {
      return (DEC_VOP_RDY);
    } else {
      dec_container->StrmStorage.vp_mb_number = last;
      dec_container->StrmStorage.vp_num_mbs = 0;
      dec_container->StrmStorage.status = STATE_OK;
      return (CONTINUE);
    }
  }
  /* start code (or END_OF_STREAM) */
  else {
    /* conceal rest of the vop and return DEC_VOP_RDY */
    last = dec_container->VopDesc.total_mb_in_vop;

    dec_container->VopDesc.vop_coded = HANTRO_TRUE;

    /* return start code into stream */
    if((start_code == SC_SV_START) || (start_code == SC_SV_END)) {
      (void)StrmDec_UnFlushBits(dec_container, 22);
    }
    /* start code value END_OF_STREAM if nothing was found (resync markers
     * disabled -> conceal) */
    else if(start_code != END_OF_STREAM) {
      (void)StrmDec_UnFlushBits(dec_container, 32);
    }

    /* data partitioned and first partition ok -> texture lost
     * (vp_num_mbs set by PartitionedMotionTexture decoding
     * function right after detection of motion/dc marker) */
    if(dec_container->Hdrs.data_partitioned &&
        ((dec_container->StrmStorage.vp_mb_number +
          dec_container->StrmStorage.vp_num_mbs) == last))
      tmp = 0x1;
    /* otherwise everything lost */
    else
      tmp = 0x3;

    /* set error status for all macro blocks to be concealed */
    for(i = first; i < last; i++) {
      dec_container->MBDesc[i].error_status = tmp;
    }

    (void)StrmDec_ErrorConcealment(dec_container, first, last - 1);

    /* set rlc buffer pointer to beginning of concealed area */
    if(dec_container->MbSetDesc.p_rlc_data_curr_addr !=
        dec_container->MbSetDesc.p_rlc_data_addr) {
      dec_container->MbSetDesc.p_rlc_data_curr_addr =
        dec_container->MbSetDesc.p_rlc_data_vp_addr;
      dec_container->MbSetDesc.odd_rlc =
        dec_container->MbSetDesc.odd_rlc_vp;
    }

    dec_container->StrmStorage.status = STATE_OK;

    return (DEC_VOP_RDY);
  }
}
