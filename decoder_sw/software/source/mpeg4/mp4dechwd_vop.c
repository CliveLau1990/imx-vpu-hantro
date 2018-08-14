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

#include "mp4dechwd_vop.h"
#include "mp4dechwd_utils.h"
#include "mp4dechwd_motiontexture.h"
#include "mp4debug.h"
#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif
/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

u32 StrmDec_ReadVopComplexity(DecContainer * dec_container);

/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_DecodeVopHeader

        Purpose: Decode VideoObjectPlane header

        Input:
            Pointer to DecContainer structure
                -uses and updates VopDesc

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeVopHeader(DecContainer * dec_container) {

  u32 i, tmp;
  i32 itmp;
  u32 modulo_time_base, vop_time_increment;
  u32 status = HANTRO_OK;

  /* check that previous vop was finished, if it wasnt't return start code
   * into the stream and get away */
  if(dec_container->StrmStorage.vp_mb_number) {
    (void) StrmDec_UnFlushBits(dec_container, 32);
    dec_container->StrmStorage.p_last_sync =
      dec_container->StrmDesc.strm_curr_pos;
    return (HANTRO_NOK);
  }
  if (dec_container->rlc_mode)
    *dec_container->MbSetDesc.p_ctrl_data_addr = 0;

  /* vop header is non valid until successively decoded */
  dec_container->StrmStorage.valid_vop_header = HANTRO_FALSE;

  dec_container->StrmStorage.vp_mb_number = 0;
  dec_container->StrmStorage.vp_num_mbs = 0;

  dec_container->StrmStorage.resync_marker_length = 0;

  tmp = StrmDec_GetBits(dec_container, 2);    /* vop coding type */
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  dec_container->VopDesc.vop_coding_type = tmp;
  if((tmp != IVOP) && (tmp != PVOP) && (tmp != BVOP)) {
    return (HANTRO_NOK);
  }


#if 0
#ifdef ASIC_TRACE_SUPPORT
  if (dec_container->VopDesc.vop_coding_type == IVOP)
    trace_mpeg4_dec_tools.pic_coding_type.i_coded = 1;
  else if (dec_container->VopDesc.vop_coding_type == PVOP)
    trace_mpeg4_dec_tools.pic_coding_type.p_coded = 1;
  else if (dec_container->VopDesc.vop_coding_type == BVOP)
    trace_mpeg4_dec_tools.pic_coding_type.b_coded = 1;
#endif
#endif

  /* Time codes updated at the end of function so that they won't be updated
   * twice if header is lost (updated if vop header lost and header extension
   * received in video packet header) */

  /* modulo time base */
  modulo_time_base = 0;
  while((tmp = StrmDec_GetBits(dec_container, 1)) == 1)
    modulo_time_base++;

  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  /* marker */
  tmp = StrmDec_GetBits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);
#ifdef HANTRO_PEDANTIC_MODE
  if(tmp == 0) {
    return (HANTRO_NOK);
  }
#endif /* HANTRO_PEDANTIC_MODE */

  /* number of bits needed to represent [0,vop_time_increment_resolution) */
  i = StrmDec_NumBits(dec_container->Hdrs.vop_time_increment_resolution - 1);
  vop_time_increment = StrmDec_GetBits(dec_container, i);
  if(vop_time_increment == END_OF_STREAM)
    return (END_OF_STREAM);

#ifdef HANTRO_PEDANTIC_MODE
  if(vop_time_increment >=
      dec_container->Hdrs.vop_time_increment_resolution) {
    return (HANTRO_NOK);
  }
#endif /* HANTRO_PEDANTIC_MODE */

  /* marker */
  tmp = StrmDec_GetBits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);
#ifdef HANTRO_PEDANTIC_MODE
  if(tmp == 0) {
    return (HANTRO_NOK);
  }
#endif /* HANTRO_PEDANTIC_MODE */

  /* vop coded */
  tmp = StrmDec_GetBits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);
  dec_container->VopDesc.vop_coded = tmp;

  if(dec_container->VopDesc.vop_coded) {
    /* vop rounding type */
    if(dec_container->VopDesc.vop_coding_type == PVOP) {
      tmp = StrmDec_GetBits(dec_container, 1);
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
    } else {
      tmp = 0;
    }
    dec_container->VopDesc.vop_rounding_type = tmp;

    if(dec_container->Hdrs.complexity_estimation_disable == HANTRO_FALSE) {
      status = StrmDec_ReadVopComplexity(dec_container);
      if(status != HANTRO_OK)
        return (status);
    }

    tmp = StrmDec_GetBits(dec_container, 3);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    dec_container->VopDesc.intra_dc_vlc_thr = tmp;

    if(dec_container->Hdrs.interlaced) {
      tmp = StrmDec_GetBits(dec_container, 1);
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
      dec_container->VopDesc.top_field_first = tmp;

      tmp = StrmDec_GetBits(dec_container, 1);
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
      dec_container->VopDesc.alt_vertical_scan_flag = tmp;

    }


    dec_container->VopDesc.q_p = StrmDec_GetBits(dec_container, 5);
    if(dec_container->VopDesc.q_p == END_OF_STREAM)
      return (END_OF_STREAM);
    if(dec_container->VopDesc.q_p == 0) {
      return (HANTRO_NOK);
    }
    dec_container->StrmStorage.q_p = dec_container->VopDesc.q_p;
    dec_container->StrmStorage.prev_qp = dec_container->VopDesc.q_p;
    dec_container->StrmStorage.vp_qp = dec_container->VopDesc.q_p;

    if(dec_container->VopDesc.vop_coding_type != IVOP) {
      tmp = StrmDec_GetBits(dec_container, 3);
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
      if(tmp == 0) {
        return (HANTRO_NOK);
      }
      dec_container->VopDesc.fcode_fwd = tmp;

      if(dec_container->VopDesc.vop_coding_type == BVOP) {
        tmp = StrmDec_GetBits(dec_container, 3);
        if(tmp == END_OF_STREAM)
          return (END_OF_STREAM);
        if(tmp == 0) {
          return (HANTRO_NOK);
        }
        dec_container->VopDesc.fcode_bwd = tmp;
      }

    } else {
      /* set vop_fcode_fwd of intra VOP to 1 for resync marker length
       * computation */
      dec_container->VopDesc.fcode_fwd = 1;
    }
  }

  dec_container->StrmStorage.resync_marker_length =
    dec_container->VopDesc.fcode_fwd + 16;

  if (!dec_container->same_vop_header) {
  if (dec_container->VopDesc.vop_coding_type != BVOP) {
    /* update time codes */
    dec_container->VopDesc.time_code_seconds += modulo_time_base;
    /* to support modulo_time_base values higher than 60 -> while */
    while(dec_container->VopDesc.time_code_seconds >= 60) {
      dec_container->VopDesc.time_code_seconds -= 60;
      dec_container->VopDesc.time_code_minutes++;
      if(dec_container->VopDesc.time_code_minutes >= 60) {
        dec_container->VopDesc.time_code_minutes -= 60;
        dec_container->VopDesc.time_code_hours++;
      }
    }
    /* compute tics since previous picture */
    itmp = (i32) vop_time_increment -
           (i32) dec_container->VopDesc.vop_time_increment +
           (i32) modulo_time_base *
           (i32) dec_container->Hdrs.vop_time_increment_resolution;
    dec_container->VopDesc.tics_from_prev = (itmp >= 0) ? itmp :
                                            (i32)(itmp + dec_container->Hdrs.vop_time_increment_resolution);
    if(dec_container->StrmStorage.gov_time_increment) {
      dec_container->VopDesc.tics_from_prev +=
        dec_container->StrmStorage.gov_time_increment;
      dec_container->StrmStorage.gov_time_increment = 0;
    }
    dec_container->VopDesc.prev_vop_time_increment =
      dec_container->VopDesc.vop_time_increment;
    dec_container->VopDesc.prev_modulo_time_base =
      dec_container->VopDesc.modulo_time_base;

    dec_container->VopDesc.vop_time_increment = vop_time_increment;
    dec_container->VopDesc.modulo_time_base = modulo_time_base;
  } else {
    itmp = (i32) vop_time_increment -
           (i32) dec_container->VopDesc.prev_vop_time_increment +
           (i32) modulo_time_base *
           (i32) dec_container->Hdrs.vop_time_increment_resolution;
    dec_container->VopDesc.trb = (itmp >= 0) ? itmp :
                                 (i32)(itmp + dec_container->Hdrs.vop_time_increment_resolution);

    dec_container->VopDesc.trd = dec_container->VopDesc.tics_from_prev;
  }
  }

  /* everything ok this far -> set vop header valid. If vop was not coded
   * don't set the flag -> if there were errors in vop header decoding they
   * don't limit decoding of header extension in video packets. */
  if(dec_container->VopDesc.vop_coded) {
    dec_container->StrmStorage.valid_vop_header = HANTRO_TRUE;
  }

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name: StrmDec_DecodeVop

        Purpose: Decode VideoObjectPlane

        Input:
            Pointer to DecContainer structure
                -uses and updates VopDesc

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeVop(DecContainer * dec_container) {

  u32 tmp;
  u32 status = HANTRO_OK;

  /*ASSERT(dec_container->Hdrs.dataPartitioned);*/

  status = StrmDec_DecodeVopHeader(dec_container);
  if(status != HANTRO_OK)
    return (status);

  if(dec_container->VopDesc.vop_coded) {
    if(dec_container->VopDesc.vop_coding_type == BVOP) {
      dec_container->rlc_mode = 0;
      return HANTRO_OK;
    } else
      dec_container->rlc_mode = 1;


    status = StrmDec_DecodeMotionTexture(dec_container);

    if(status != HANTRO_OK)
      return (status);
  } else { /* not coded vop */
    dec_container->StrmStorage.vp_num_mbs =
      dec_container->VopDesc.total_mb_in_vop;
  }

  status = StrmDec_GetStuffing(dec_container);
  if(status != HANTRO_OK)
    return (status);

  /* there might be extra stuffing byte if next start code is video
   * object sequence start or end code */
  tmp = StrmDec_ShowBitsAligned(dec_container, 32, 1);
  if((tmp == SC_VOS_START) || (tmp == SC_VOS_END) ||
      (tmp == 0 && StrmDec_ShowBits(dec_container, 8) == 0x7F) ) {
    tmp = StrmDec_GetStuffing(dec_container);
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  /* stuffing ok -> check that there is at least 23 zeros after last
   * macroblock and either resync marker or 32 zeros (END_OF_STREAM) after
   * any other macroblock */
  tmp = StrmDec_ShowBits(dec_container, 32);

  /* JanSa: modified to handle extra zeros after VOP */
  if ( !(tmp>>8) &&
       ((dec_container->StrmStorage.vp_mb_number +
         dec_container->StrmStorage.vp_num_mbs) ==
        dec_container->VopDesc.total_mb_in_vop) ) {
    do {
      if (StrmDec_FlushBits(dec_container, 8) == END_OF_STREAM)
        break;
      tmp = StrmDec_ShowBits(dec_container,32);
    } while (!(tmp>>8));
  }

  if(tmp) {
    if((tmp >> 9) &&
        /* ignore any stuff that follows VOP end */
        (((dec_container->StrmStorage.vp_mb_number +
           dec_container->StrmStorage.vp_num_mbs) !=
          dec_container->VopDesc.total_mb_in_vop) &&
         ((tmp >> (32 - dec_container->StrmStorage.resync_marker_length))
          != 0x01))) {
      return (HANTRO_NOK);
    }
  }
  /* just zeros in the stream and not end of stream -> error */
  else if(!IS_END_OF_STREAM(dec_container)) {
    return (HANTRO_NOK);
  }
  /* end of stream -> check that whole vop was decoded in case resync
   * markers are disabled */
  else if(dec_container->Hdrs.resync_marker_disable &&
          ((dec_container->StrmStorage.vp_mb_number +
            dec_container->StrmStorage.vp_num_mbs) !=
           dec_container->VopDesc.total_mb_in_vop)) {
    return (HANTRO_NOK);
  }

  /* whole video packet decoded and stuffing ok -> set vp_mb_number in
   * StrmStorage so that this video packet won't be touched/concealed
   * anymore. Also set VpQP to QP so that concealment will use qp of last
   * decoded macro block */
  dec_container->StrmStorage.vp_mb_number +=
    dec_container->StrmStorage.vp_num_mbs;
  dec_container->StrmStorage.vp_qp = dec_container->StrmStorage.q_p;
  dec_container->MbSetDesc.p_rlc_data_vp_addr =
    dec_container->MbSetDesc.p_rlc_data_curr_addr;
  dec_container->MbSetDesc.odd_rlc_vp =
    dec_container->MbSetDesc.odd_rlc;

  dec_container->StrmStorage.vp_num_mbs = 0;

  return (status);

}

/*------------------------------------------------------------------------------

   5.3  Function name: StrmDec_ReadVopComplexity

        Purpose: read vop complexity estimation header

        Input:
            Pointer to DecContainer structure
                -uses and updates VopDesc
                -uses Hdrs

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_ReadVopComplexity(DecContainer * dec_container) {

  u32 tmp;

  tmp = 0;
  if(dec_container->Hdrs.estimation_method == 0) {
    /* common stuff for I- and P-VOPs */
    if(dec_container->Hdrs.opaque)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.transparent)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.intra_cae)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.inter_cae)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.no_update)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.upsampling)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.intra_blocks)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.not_coded_blocks)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.dct_coefs)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.dct_lines)
      tmp = StrmDec_GetBits(dec_container, 8);
    if(dec_container->Hdrs.vlc_symbols)
      tmp = StrmDec_GetBits(dec_container, 8);
    /* NOTE that this is just 4 bits long */
    if(dec_container->Hdrs.vlc_bits)
      tmp = StrmDec_GetBits(dec_container, 4);

    if(dec_container->VopDesc.vop_coding_type == IVOP) {
      if(dec_container->Hdrs.sadct)
        tmp = StrmDec_GetBits(dec_container, 8);
    } else { /* PVOP */
      if(dec_container->Hdrs.inter_blocks)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.inter4v_blocks)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.apm)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.npm)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.forw_back_mc_q)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.halfpel2)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.halfpel4)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.sadct)
        tmp = StrmDec_GetBits(dec_container, 8);
      if(dec_container->Hdrs.quarterpel)
        tmp = StrmDec_GetBits(dec_container, 8);
    }
  }
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  return (HANTRO_OK);

}
