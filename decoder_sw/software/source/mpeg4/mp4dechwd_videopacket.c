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

#include "mp4dechwd_videopacket.h"
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

/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_DecodeVideoPacketHeader

        Purpose: Decode video packet header

        Input:
            Pointer to DecContainer structure
                -uses and updates VopDesc

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeVideoPacketHeader(DecContainer * dec_container) {

  u32 i, tmp;
  i32 itmp;

  dec_container->StrmStorage.vp_num_mbs = 0;
  MP4DEC_DEBUG(("Decoding video packet header\n"));

  /* length of macro_block_number determined by size of the VOP in mbs */
  tmp = StrmDec_NumBits(dec_container->VopDesc.total_mb_in_vop - 1);
  tmp = StrmDec_GetBits(dec_container, tmp);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  /* erroneous macro block number */
  if(tmp != dec_container->StrmStorage.vp_mb_number) {
    return (HANTRO_NOK);
  }

  /* QP */
  tmp = StrmDec_GetBits(dec_container, 5);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);
  if(tmp == 0) {
    return (HANTRO_NOK);
  }

  dec_container->StrmStorage.q_p = tmp;
  dec_container->StrmStorage.prev_qp = tmp;
  dec_container->StrmStorage.vp_qp = tmp;

  /* HEC */
  tmp = StrmDec_GetBits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

#ifdef ASIC_TRACE_SUPPORT
  if (tmp)
    trace_mpeg4_dec_tools.hdr_extension_code = 1;
#endif

  /* decode header extension. Values are used only if vop header was
   * lost for any reason. Otherwise values are just compared to ones
   * received in vop header and errors are reported if they do not match */
  if(tmp) {
    /* modulo time base */
    i = 0;
    while((tmp = StrmDec_GetBits(dec_container, 1)) == 1)
      i++;

    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);

    /* update time codes if vop header not valid */
    if(dec_container->StrmStorage.valid_vop_header == HANTRO_FALSE) {
      dec_container->VopDesc.time_code_seconds += i;
      /* to support modulo_time_base values higher than 60 -> while */
      while(dec_container->VopDesc.time_code_seconds >= 60) {
        dec_container->VopDesc.time_code_seconds -= 60;
        dec_container->VopDesc.time_code_minutes++;
        if(dec_container->VopDesc.time_code_minutes >= 60) {
          dec_container->VopDesc.time_code_minutes -= 60;
          dec_container->VopDesc.time_code_hours++;
        }
      }
      dec_container->VopDesc.modulo_time_base = i;
    } else if(i != dec_container->VopDesc.modulo_time_base) {
      return (HANTRO_NOK);
    }

    /* marker */
    tmp = StrmDec_GetBits(dec_container, 1);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
#ifdef HANTRO_PEDANTIC_MODE
    if(tmp == 0) {
      return (HANTRO_NOK);
    }
#endif /* HANTRO_PEDANTIC_MODE */

    /* number of bits needed to represent
     * [0,vop_time_increment_resolution) */
    i = StrmDec_NumBits(dec_container->Hdrs.vop_time_increment_resolution
                        - 1);
    tmp = StrmDec_GetBits(dec_container, i);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    if(tmp >= dec_container->Hdrs.vop_time_increment_resolution) {
      return (HANTRO_NOK);
    }

    if(dec_container->StrmStorage.valid_vop_header == HANTRO_FALSE) {
      if (!dec_container->same_vop_header) {
      /* compute tics since previous picture */
      itmp =
        (i32) tmp - (i32) dec_container->VopDesc.vop_time_increment +
        (i32) dec_container->VopDesc.modulo_time_base *
        dec_container->Hdrs.vop_time_increment_resolution;

      dec_container->VopDesc.tics_from_prev = (itmp >= 0) ? itmp :
                                              (i32)(itmp + dec_container->Hdrs.vop_time_increment_resolution);
      if(dec_container->StrmStorage.gov_time_increment) {
        dec_container->VopDesc.tics_from_prev +=
          dec_container->StrmStorage.gov_time_increment;
        dec_container->StrmStorage.gov_time_increment = 0;
      }
      }
      dec_container->VopDesc.vop_time_increment = tmp;
    } else if(tmp != dec_container->VopDesc.vop_time_increment) {
      return (HANTRO_NOK);
    }

    /* marker */
    tmp = StrmDec_GetBits(dec_container, 1);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
#ifdef HANTRO_PEDANTIC_MODE
    if(tmp == 0) {
      return (HANTRO_NOK);
    }
#endif /* HANTRO_PEDANTIC_MODE */

    tmp = StrmDec_GetBits(dec_container, 2);    /* vop_coding_type */
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    if(dec_container->StrmStorage.valid_vop_header == HANTRO_TRUE) {
      if(tmp != dec_container->VopDesc.vop_coding_type) {
        return (HANTRO_NOK);
      }
    } else {
      if((tmp != IVOP) && (tmp != PVOP)) {
        return (HANTRO_NOK);
      } else {
        dec_container->VopDesc.vop_coding_type = tmp;
      }
    }

    tmp = StrmDec_GetBits(dec_container, 3);    /* intra_dc_vlc_thr */
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);

    if(dec_container->StrmStorage.valid_vop_header == HANTRO_TRUE) {
      if(tmp != dec_container->VopDesc.intra_dc_vlc_thr) {
        return (HANTRO_NOK);
      }
    } else {
      dec_container->VopDesc.intra_dc_vlc_thr = tmp;
    }

    if(dec_container->VopDesc.vop_coding_type != IVOP) {
      tmp = StrmDec_GetBits(dec_container, 3);    /* vop_fcode_fwd */
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
      if(dec_container->StrmStorage.valid_vop_header == HANTRO_TRUE) {
        if(tmp != dec_container->VopDesc.fcode_fwd) {
          return (HANTRO_NOK);
        }
      } else {
        if(tmp == 0) {
          return (HANTRO_NOK);
        } else {
          dec_container->VopDesc.fcode_fwd = tmp;
        }
      }

      if(dec_container->VopDesc.vop_coding_type == BVOP) {
        tmp = StrmDec_GetBits(dec_container, 3);    /* vop_fcode_backward */
        if(tmp == END_OF_STREAM)
          return (END_OF_STREAM);
        if(dec_container->StrmStorage.valid_vop_header == HANTRO_TRUE) {
          if(tmp != dec_container->VopDesc.fcode_bwd) {
            return (HANTRO_NOK);
          }
        } else {
          if(tmp == 0) {
            return (HANTRO_NOK);
          } else {
            dec_container->VopDesc.fcode_bwd = tmp;
          }
        }



      }


    } else {
      /* set vop_fcode_fwd of intra VOP to 1 for resync marker length
       * computation */
      dec_container->VopDesc.fcode_fwd = 1;
    }

    dec_container->StrmStorage.resync_marker_length =
      dec_container->VopDesc.fcode_fwd + 16;

    if(dec_container->StrmStorage.valid_vop_header == HANTRO_FALSE) {
      dec_container->StrmStorage.valid_vop_header = HANTRO_TRUE;
    }
  }

  if(dec_container->StrmStorage.valid_vop_header == HANTRO_TRUE) {
    return (HANTRO_OK);
  } else {
    return (HANTRO_NOK);
  }
}

/*------------------------------------------------------------------------------

   5.2  Function name: StrmDec_DecodeVideoPacket

        Purpose: Decode video packet

        Input:
            Pointer to DecContainer structure
                -uses and updates VopDesc

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeVideoPacket(DecContainer * dec_container) {

  u32 tmp;
  u32 status = HANTRO_OK;
  status = StrmDec_DecodeVideoPacketHeader(dec_container);
  if(status != HANTRO_OK)
    return (status);

  status = StrmDec_DecodeMotionTexture(dec_container);
  if(status != HANTRO_OK)
    return (status);

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
        /* ignore anything that follows after VOP end */
        (((dec_container->StrmStorage.vp_mb_number +
           dec_container->StrmStorage.vp_num_mbs) !=
          dec_container->VopDesc.total_mb_in_vop) &&
         ((tmp >> (32 - dec_container->StrmStorage.resync_marker_length))
          != 0x01))) {
      return (HANTRO_NOK);
    }
  } else if(!IS_END_OF_STREAM(dec_container)) {
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

   5.3  Function name: StrmDec_CheckNextVpMbNumber

        Purpose:

        Input:
            Pointer to DecContainer structure
                -uses VopDesc

        Output:
            macro block number
            0 if non-valid number found in stream

------------------------------------------------------------------------------*/

u32 StrmDec_CheckNextVpMbNumber(DecContainer * dec_container) {

  u32 tmp;
  u32 mb_number;

  tmp = StrmDec_NumBits(dec_container->VopDesc.total_mb_in_vop - 1);

  mb_number = StrmDec_ShowBits(dec_container, tmp);

  /* use zero value to indicate non valid macro block number */
  if(mb_number >= dec_container->VopDesc.total_mb_in_vop) {
    mb_number = 0;
  }

  return (mb_number);

}
