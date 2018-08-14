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

#include "mp4dechwd_headers.h"
#include "mp4dechwd_utils.h"
#include "mp4decapi_internal.h"
#include "mp4debug.h"
#include "mp4deccfg.h"
#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif
/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum {
  VIDEO_ID = 1
};
enum {
  SIMPLE_OBJECT_TYPE = 1
};

/* aspect ratio info */
enum {
  EXTENDED_PAR = 15
};
enum {
  RECTANGULAR = 0
};

enum {
  VERSION1 = 0,
  VERSION2 = 1
};

u32 StrmDec_DefineVopComplexityEstimationHdr(DecContainer * dec_container);

static const u32 zig_zag[64] = {
  0,  1,  8, 16,  9,  2,  3, 10,
  17, 24, 32, 25, 18, 11,  4,  5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13,  6,  7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

static u32 QuantMat(DecContainer * dec_container, u32 intra);

static void MP4DecSetLowDelay(DecContainer * dec_container);
extern void StrmDec_CheckPackedMode(DecContainer * dec_container);

void ProcessUserData(DecContainer * dec_container);

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.1  Function name:
                StrmDec_DecodeHdrs

        Purpose:
                Decodes MPEG-4 higher level (Up to VOL + GVOP) headers from
                input stream

        Input:
                pointer to DecContainer, name of first header to decode

        Output:
                status (HANTRO_OK/NOK/END_OF_STREAM/... enum in .h file!)

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeHdrs(DecContainer * dec_container, u32 mode) {

  u32 tmp;
  u32 marker_bit = 0;
  u32 tmp2 = 0;
#ifndef HANTRO_PEDANTIC_MODE
  UNUSED(marker_bit);
  UNUSED(tmp2);
#endif  /* HANTRO_PEDANTIC_MODE */

  if (dec_container->Hdrs.lock) {
    while ((mode == SC_VOS_START) || (mode == SC_VISO_START) ||
           (mode == SC_VO_START)  || (mode == SC_VOL_START) ) {
      mode = StrmDec_FindSync(dec_container);
    }
    if (mode == SC_SV_START) {
      (void)StrmDec_UnFlushBits(dec_container,22);
    } else if (mode == SC_RESYNC) {
      (void)StrmDec_UnFlushBits(dec_container,
                                dec_container->StrmStorage.resync_marker_length);
    } else if (mode != END_OF_STREAM) {
      (void)StrmDec_UnFlushBits(dec_container,32);
    }
    return(HANTRO_OK);
  }

  /* in following switch case syntax, the u32_mode is used only to decide
   * from witch header the decoding starts. After that the decoding will
   * run through all headers and will break after the VOL header is decoded.
   * GVOP header will be decoded separately and after that the execution
   * will jump back to the top level */

  switch (mode) {
  case SC_VOS_START:
    /* visual object sequence */
    tmp = StrmDec_GetBits(dec_container, 8);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    dec_container->Hdrs.profile_and_level_indication = tmp;
    dec_container->Hdrs.last_header_type = SC_VOS_START;
    tmp = StrmDec_SaveUserData(dec_container, SC_VOS_START);
    if(tmp == HANTRO_NOK)
      return (HANTRO_NOK);
    MP4DEC_DEBUG(("Decoded VOS Start\n"));
    break;

  case SC_VISO_START:
    /* visual object */
    tmp = dec_container->Hdrs.is_visual_object_identifier =
            StrmDec_GetBits(dec_container, 1);
    if(tmp) {
      tmp = dec_container->Hdrs.visual_object_verid =
              StrmDec_GetBits(dec_container, 4);
      dec_container->Hdrs.visual_object_priority =
        StrmDec_GetBits(dec_container, 3);
    } else
      dec_container->Hdrs.visual_object_verid = 0x1;
    tmp = dec_container->Hdrs.visual_object_type =
            StrmDec_GetBits(dec_container, 4);
    if((tmp != VIDEO_ID) && (tmp != END_OF_STREAM))
      return (HANTRO_NOK);
    tmp = dec_container->Hdrs.video_signal_type =
            StrmDec_GetBits(dec_container, 1);
    if(tmp) {
      dec_container->Hdrs.video_format =
        StrmDec_GetBits(dec_container, 3);
      dec_container->Hdrs.video_range =
        StrmDec_GetBits(dec_container, 1);
      tmp = dec_container->Hdrs.colour_description =
              StrmDec_GetBits(dec_container, 1);
      if(tmp) {
        tmp = dec_container->Hdrs.colour_primaries =
                StrmDec_GetBits(dec_container, 8);
#ifdef HANTRO_PEDANTIC_MODE
        if(tmp == 0)
          return (HANTRO_NOK);
#endif
        tmp = dec_container->Hdrs.transfer_characteristics =
                StrmDec_GetBits(dec_container, 8);
#ifdef HANTRO_PEDANTIC_MODE
        if(tmp == 0)
          return (HANTRO_NOK);
#endif
        tmp = dec_container->Hdrs.matrix_coefficients =
                StrmDec_GetBits(dec_container, 8);
#ifdef HANTRO_PEDANTIC_MODE
        if(tmp == 0)
          return (HANTRO_NOK);
#endif
      }
    }
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    /* stuffing */
    tmp = StrmDec_GetStuffing(dec_container);
    if(tmp != HANTRO_OK)
      return (tmp);
    dec_container->Hdrs.last_header_type = SC_VISO_START;
    /* user data */
    tmp = StrmDec_SaveUserData(dec_container, SC_VISO_START);
    if(tmp == HANTRO_NOK)
      return (HANTRO_NOK);
    MP4DEC_DEBUG(("Decoded VISO Start\n"));
    break;

  case SC_VO_START:
    /* video object */
    MP4DEC_DEBUG(("Decoded VO Start\n"));
    break;

  case SC_VOL_START:
    MP4DEC_DEBUG(("Decode VOL Start\n"));
    /* video object layer */
    dec_container->Hdrs.random_accessible_vol =
      StrmDec_GetBits(dec_container, 1);
    dec_container->Hdrs.video_object_type_indication =
      StrmDec_GetBits(dec_container, 8);
    /* NOTE: video_object_type cannot be checked if we want to be
     * able to decode conformance test streams (they contain
     * streams where type is e.g. main). However streams do not
     * utilize all tools of "higher" profiles and can be decoded
     * by our codec, even though we only support objects of type
     * simple and advanced simple */
    tmp = dec_container->Hdrs.is_object_layer_identifier =
            StrmDec_GetBits(dec_container, 1);
    if(tmp) {
      dec_container->Hdrs.video_object_layer_verid =
        StrmDec_GetBits(dec_container, 4);
      dec_container->Hdrs.video_object_layer_priority =
        StrmDec_GetBits(dec_container, 3);
    } else {
      dec_container->Hdrs.video_object_layer_verid =
        dec_container->Hdrs.visual_object_verid;
    }
    tmp = dec_container->Hdrs.aspect_ratio_info =
            StrmDec_GetBits(dec_container, 4);
    if(tmp == EXTENDED_PAR) {
      tmp = dec_container->Hdrs.par_width =
              StrmDec_GetBits(dec_container, 8);
#ifdef HANTRO_PEDANTIC_MODE
      if(tmp == 0)
        return (HANTRO_NOK);
#endif
      tmp = dec_container->Hdrs.par_height =
              StrmDec_GetBits(dec_container, 8);
#ifdef HANTRO_PEDANTIC_MODE
      if(tmp == 0)
        return (HANTRO_NOK);
#endif
    }
#ifdef HANTRO_PEDANTIC_MODE
    else if(tmp == 0)
      return (HANTRO_NOK);
#endif
    tmp = dec_container->Hdrs.vol_control_parameters =
            StrmDec_GetBits(dec_container, 1);
    if(tmp) {
      dec_container->Hdrs.chroma_format =
        StrmDec_GetBits(dec_container, 2);
      tmp = dec_container->Hdrs.low_delay =
              StrmDec_GetBits(dec_container, 1);
      tmp = dec_container->Hdrs.vbv_parameters =
              StrmDec_GetBits(dec_container, 1);
      if(tmp) {
        tmp = dec_container->Hdrs.first_half_bit_rate =
                StrmDec_GetBits(dec_container, 15);
        marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
        if(!marker_bit)
          return (HANTRO_NOK);
#endif
        tmp2 = dec_container->Hdrs.latter_half_bit_rate =
                 StrmDec_GetBits(dec_container, 15);
#ifdef HANTRO_PEDANTIC_MODE
        if((tmp == 0) && (tmp2 == 0))
          return (HANTRO_NOK);
#endif
        marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
        if(!marker_bit)
          return (HANTRO_NOK);
#endif
        tmp = dec_container->Hdrs.first_half_vbv_buffer_size =
                StrmDec_GetBits(dec_container, 15);
        marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
        if(!marker_bit)
          return (HANTRO_NOK);
#endif
        tmp2 = dec_container->Hdrs.latter_half_vbv_buffer_size =
                 StrmDec_GetBits(dec_container, 3);
#ifdef HANTRO_PEDANTIC_MODE
        if((tmp == 0) && (tmp2 == 0))
          return (HANTRO_NOK);
#endif
        dec_container->Hdrs.first_half_vbv_occupancy =
          StrmDec_GetBits(dec_container, 11);
        marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
        if(!marker_bit)
          return (HANTRO_NOK);
#endif
        dec_container->Hdrs.latter_half_vbv_occupancy =
          StrmDec_GetBits(dec_container, 15);
        marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
        if(!marker_bit)
          return (HANTRO_NOK);
#endif
      }
    } else {
      /* Set low delay based on profile */
      MP4DecSetLowDelay(dec_container);
    }

    tmp = dec_container->Hdrs.video_object_layer_shape =
            StrmDec_GetBits(dec_container, 2);
    if((tmp != RECTANGULAR) && (tmp != END_OF_STREAM))
      return (HANTRO_NOK);
    marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
    if(!marker_bit)
      return (HANTRO_NOK);
#endif
    tmp = dec_container->Hdrs.vop_time_increment_resolution =
            StrmDec_GetBits(dec_container, 16);
    if(tmp == 0)
      return (HANTRO_NOK);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
    if(!marker_bit)
      return (HANTRO_NOK);
#endif
    tmp = dec_container->Hdrs.fixed_vop_rate =
            StrmDec_GetBits(dec_container, 1);
    if(tmp) {
      tmp =
        StrmDec_NumBits(dec_container->Hdrs.
                        vop_time_increment_resolution - 1);
      tmp = dec_container->Hdrs.fixed_vop_time_increment =
              StrmDec_GetBits(dec_container, tmp);
      if (tmp == 0 ||
          tmp >= dec_container->Hdrs.vop_time_increment_resolution)
        return (HANTRO_NOK);
    }
    /* marker bit after if video_object_layer==rectangular */
    marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
    if(!marker_bit)
      return (HANTRO_NOK);
#endif
    tmp = dec_container->Hdrs.video_object_layer_width =
            StrmDec_GetBits(dec_container, 13);
    if(tmp == 0)
      return (HANTRO_NOK);
    marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
    if(!marker_bit)
      return (HANTRO_NOK);
#endif
    tmp = dec_container->Hdrs.video_object_layer_height =
            StrmDec_GetBits(dec_container, 13);
    if(tmp == 0)
      return (HANTRO_NOK);
    marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
    if(!marker_bit)
      return (HANTRO_NOK);
#endif
    tmp = dec_container->Hdrs.interlaced =
            StrmDec_GetBits(dec_container, 1);

    tmp = dec_container->Hdrs.obmc_disable =
            StrmDec_GetBits(dec_container, 1);
    if(!tmp)
      return (HANTRO_NOK);
    if(dec_container->Hdrs.video_object_layer_verid == 1) {
      tmp = dec_container->Hdrs.sprite_enable =
              StrmDec_GetBits(dec_container, 1);
    } else {
      tmp = dec_container->Hdrs.sprite_enable =
              StrmDec_GetBits(dec_container, 2);
    }

    if(dec_container->Hdrs.sprite_enable != 0) {
      if(tmp != END_OF_STREAM) {
        dec_container->StrmStorage.unsupported_features_present = 1;
#if 0
        return (HANTRO_NOK);
#else
        tmp = StrmDec_GetBits(dec_container, 6);
        tmp = StrmDec_GetBits(dec_container, 2);
        tmp = StrmDec_GetBits(dec_container, 1);
#endif

      }
    }

    tmp = dec_container->Hdrs.not8_bit =
            StrmDec_GetBits(dec_container, 1);
    if(tmp && (tmp != END_OF_STREAM))
      return (HANTRO_NOK);

    tmp = dec_container->Hdrs.quant_type =
            StrmDec_GetBits(dec_container, 1);
#ifdef ASIC_TRACE_SUPPORT
    if (tmp)
      trace_mpeg4_dec_tools.q_method1 = 1;
    else
      trace_mpeg4_dec_tools.q_method2 = 1;
#endif
    (void)DWLmemset(dec_container->StrmStorage.quant_mat,
                    0, MP4DEC_QUANT_TABLE_SIZE);
    if (dec_container->Hdrs.quant_type) {
      tmp = StrmDec_GetBits(dec_container, 1);
      if (tmp && tmp != END_OF_STREAM)
        tmp = QuantMat(dec_container, 1);
      tmp = StrmDec_GetBits(dec_container, 1);
      if (tmp && tmp != END_OF_STREAM)
        tmp = QuantMat(dec_container, 0);
    }

    if(dec_container->Hdrs.video_object_layer_verid != 1) {
      /* quarter_sample */
      tmp = StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.quarterpel = tmp;

    }
    tmp = dec_container->Hdrs.complexity_estimation_disable =
            StrmDec_GetBits(dec_container, 1);
    if(tmp == 0) {
      /* complexity estimation header */
      tmp = StrmDec_DefineVopComplexityEstimationHdr(dec_container);
#ifdef HANTRO_PEDANTIC_MODE
      if(tmp == HANTRO_NOK)
        return (HANTRO_NOK);
#endif
    }
    dec_container->Hdrs.resync_marker_disable = 0;
#ifdef ASIC_TRACE_SUPPORT
    tmp = StrmDec_GetBits(dec_container, 1);
    if (!tmp)
      trace_mpeg4_dec_tools.resync_marker = 1;
#else
    (void) StrmDec_GetBits(dec_container, 1);
#endif
    tmp = dec_container->Hdrs.data_partitioned =
            StrmDec_GetBits(dec_container, 1);
#ifdef ASIC_TRACE_SUPPORT
    if (tmp)
      trace_mpeg4_dec_tools.data_partition = 1;
#endif
    if(tmp) {
      dec_container->Hdrs.reversible_vlc =
        StrmDec_GetBits(dec_container, 1);
#ifdef ASIC_TRACE_SUPPORT
      if (dec_container->Hdrs.reversible_vlc)
        trace_mpeg4_dec_tools.reversible_vlc = 1;
#endif
    } else
      dec_container->Hdrs.reversible_vlc = 0;
    if(dec_container->Hdrs.video_object_layer_verid != 1) {
      /* newpred_enable */
      tmp = StrmDec_GetBits(dec_container, 1);
      if(tmp == 1)
        return (HANTRO_NOK);
      /* reduced_resolution_vop_enable */
      tmp = StrmDec_GetBits(dec_container, 1);
      if(tmp == 1)
        return (HANTRO_NOK);
    }
    tmp = dec_container->Hdrs.scalability =
            StrmDec_GetBits(dec_container, 1);
    if(tmp == 1)
      return (HANTRO_NOK);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    tmp = StrmDec_GetStuffing(dec_container);
    if(tmp != HANTRO_OK)
      return (tmp);
    dec_container->Hdrs.last_header_type = SC_VOL_START;
    /* user data */
    tmp = StrmDec_SaveUserData(dec_container, SC_VOL_START);
    if(tmp == HANTRO_NOK)
      return (HANTRO_NOK);
    MP4DEC_DEBUG(("Decoded VOL Start\n"));
    break;

  default:
    return (HANTRO_NOK);

  }
  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

   5.1  Function name:
                StrmDec_DecodeGovHeader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeGovHeader(DecContainer *dec_container) {
  u32 tmp;

  ASSERT(dec_container);

  MP4DEC_DEBUG(("Decode GVOP Start\n"));
  /* NOTE: in rare cases computation of GovTimeIncrement may cause
   * overflow. This requires that time code change is ~18 hours and
   * VopTimeIncrementResolution is ~2^16. */
  dec_container->StrmStorage.gov_time_increment =
    dec_container->VopDesc.time_code_hours * 3600 +
    dec_container->VopDesc.time_code_minutes * 60 +
    dec_container->VopDesc.time_code_seconds;

  tmp = dec_container->VopDesc.time_code_hours =
          StrmDec_GetBits(dec_container, 5);
  if((tmp > 23) && (tmp != END_OF_STREAM))
    return (HANTRO_NOK);
  tmp = dec_container->VopDesc.time_code_minutes =
          StrmDec_GetBits(dec_container, 6);
  if((tmp > 59) && (tmp != END_OF_STREAM))
    return (HANTRO_NOK);
  tmp = StrmDec_GetBits(dec_container, 1);
  if(tmp == 0)
    return (HANTRO_NOK);
  tmp = dec_container->VopDesc.time_code_seconds =
          StrmDec_GetBits(dec_container, 6);
  if((tmp > 59) && (tmp != END_OF_STREAM))
    return (HANTRO_NOK);
  tmp =
    dec_container->VopDesc.time_code_hours * 3600 +
    dec_container->VopDesc.time_code_minutes * 60 +
    dec_container->VopDesc.time_code_seconds;
  if(tmp == dec_container->StrmStorage.gov_time_increment) {
    dec_container->StrmStorage.gov_time_increment = 0;
  } else if(tmp > dec_container->StrmStorage.gov_time_increment) {
    dec_container->StrmStorage.gov_time_increment = tmp -
        dec_container->StrmStorage.gov_time_increment;
    dec_container->StrmStorage.gov_time_increment *=
      dec_container->Hdrs.vop_time_increment_resolution;
  } else {
    dec_container->StrmStorage.gov_time_increment =
      (24 * 3600 + tmp) -
      dec_container->StrmStorage.gov_time_increment;
    dec_container->StrmStorage.gov_time_increment *=
      dec_container->Hdrs.vop_time_increment_resolution;
  }

  dec_container->Hdrs.closed_gov = StrmDec_GetBits(dec_container, 1);
  dec_container->Hdrs.broken_link = StrmDec_GetBits(dec_container, 1);
  tmp = StrmDec_GetStuffing(dec_container);
  if(tmp != HANTRO_OK)
    return (tmp);
  dec_container->Hdrs.last_header_type = SC_GVOP_START;
  dec_container->VopDesc.gov_counter++;
  tmp = StrmDec_SaveUserData(dec_container, SC_GVOP_START);
  return (tmp);

}

/*------------------------------------------------------------------------------

   5.2  Function name:
                StrmDec_DefineVopComplexityEstimationHdr

        Purpose:
                Decodes fields from VOL define_vop_complexity_estimation_
                header

        Input:
                pointer to DecContainer

        Output:
                status (HANTRO_OK/NOK)

------------------------------------------------------------------------------*/
u32 StrmDec_DefineVopComplexityEstimationHdr(DecContainer * dec_container) {
  u32 tmp;
  u32 marker_bit = 0;
  u32 estimation_method;
#ifndef HANTRO_PEDANTIC_MODE
  UNUSED(marker_bit);
#endif  /* HANTRO_PEDANTIC_MODE */

  estimation_method = dec_container->Hdrs.estimation_method =
                        StrmDec_GetBits(dec_container, 2);
  if((estimation_method == VERSION1) ||
      (estimation_method == VERSION2)) {
    tmp = dec_container->Hdrs.shape_complexity_estimation_disable =
            StrmDec_GetBits(dec_container, 1);
    if(tmp == 0) {
      dec_container->Hdrs.opaque =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.transparent =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.intra_cae =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.inter_cae =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.no_update =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.upsampling =
        StrmDec_GetBits(dec_container, 1);
    } else {
      /* initialisation to zero */
      dec_container->Hdrs.opaque = 0;
      dec_container->Hdrs.transparent = 0;
      dec_container->Hdrs.intra_cae = 0;
      dec_container->Hdrs.inter_cae = 0;
      dec_container->Hdrs.no_update = 0;
      dec_container->Hdrs.upsampling = 0;
    }
    tmp =
      dec_container->Hdrs.texture_complexity_estimation_set1_disable =
        StrmDec_GetBits(dec_container, 1);
    if(tmp == 0) {
      dec_container->Hdrs.intra_blocks =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.inter_blocks =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.inter4v_blocks =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.not_coded_blocks =
        StrmDec_GetBits(dec_container, 1);
    } else {
      /* Initialization to zero */
      dec_container->Hdrs.intra_blocks = 0;
      dec_container->Hdrs.inter_blocks = 0;
      dec_container->Hdrs.inter4v_blocks = 0;
      dec_container->Hdrs.not_coded_blocks = 0;
    }
    marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
    if(!marker_bit)
      return (HANTRO_NOK);
#endif
    tmp =
      dec_container->Hdrs.texture_complexity_estimation_set2_disable =
        StrmDec_GetBits(dec_container, 1);
    if(tmp == 0) {
      dec_container->Hdrs.dct_coefs =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.dct_lines =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.vlc_symbols =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.vlc_bits =
        StrmDec_GetBits(dec_container, 1);
    } else {
      /* initialisation to zero */
      dec_container->Hdrs.dct_coefs = 0;
      dec_container->Hdrs.dct_lines = 0;
      dec_container->Hdrs.vlc_symbols = 0;
      dec_container->Hdrs.vlc_bits = 0;
    }
    tmp = dec_container->Hdrs.motion_compensation_complexity_disable =
            StrmDec_GetBits(dec_container, 1);
    if(tmp == 0) {
      dec_container->Hdrs.apm = StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.npm = StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.interpolate_mc_q =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.forw_back_mc_q =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.halfpel2 =
        StrmDec_GetBits(dec_container, 1);
      dec_container->Hdrs.halfpel4 =
        StrmDec_GetBits(dec_container, 1);
    } else {
      /* initilisation to zero */
      dec_container->Hdrs.apm = 0;
      dec_container->Hdrs.npm = 0;
      dec_container->Hdrs.interpolate_mc_q = 0;
      dec_container->Hdrs.forw_back_mc_q = 0;
      dec_container->Hdrs.halfpel2 = 0;
      dec_container->Hdrs.halfpel4 = 0;
    }
    marker_bit = StrmDec_GetBits(dec_container, 1);
#ifdef HANTRO_PEDANTIC_MODE
    if(!marker_bit)
      return (HANTRO_NOK);
#endif
    if(estimation_method == VERSION2) {
      tmp =
        dec_container->Hdrs.
        version2_complexity_estimation_disable =
          StrmDec_GetBits(dec_container, 1);
      if(tmp == 0) {
        dec_container->Hdrs.sadct =
          StrmDec_GetBits(dec_container, 1);
        dec_container->Hdrs.quarterpel =
          StrmDec_GetBits(dec_container, 1);
      } else {
        /* initialisation to zero */
        dec_container->Hdrs.sadct = 0;
        dec_container->Hdrs.quarterpel = 0;
      }
    } else {
      /* init to zero */
      dec_container->Hdrs.sadct = 0;
      dec_container->Hdrs.quarterpel = 0;
    }
  }
  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.3  Function name:
                StrmDec_SaveUserData

        Purpose:
                Saves user data from headers

        Input:
                pointer to DecContainer

        Output:
                status (HANTRO_OK/NOK/END_OF_STREAM)

------------------------------------------------------------------------------*/

u32 StrmDec_SaveUserData(DecContainer * dec_container, u32 u32_mode) {

  u32 tmp;
  u32 max_len;
  u32 len;
  u8 *output;
  u32 enable;
  u32 *plen;
  u32 status = HANTRO_OK;

  MP4DEC_DEBUG(("SAVE USER DATA\n"));

  tmp = StrmDec_ShowBits(dec_container, 32);
  if(tmp != SC_UD_START) {
    /* there is no user data */
    return (HANTRO_OK);
  } else {
    tmp = StrmDec_FlushBits(dec_container, 32);
    if (tmp == END_OF_STREAM)
      return (END_OF_STREAM);
  }
  MP4DEC_DEBUG(("SAVE USER DATA\n"));

  switch (u32_mode) {
  case SC_VOS_START:
    max_len = dec_container->StrmDesc.user_data_vosmax_len;
    plen = &(dec_container->StrmDesc.user_data_voslen);
    output = dec_container->StrmDesc.p_user_data_vos;
    break;

  case SC_VISO_START:
    max_len = dec_container->StrmDesc.user_data_vomax_len;
    plen = &(dec_container->StrmDesc.user_data_volen);
    output = dec_container->StrmDesc.p_user_data_vo;
    break;

  case SC_VOL_START:
    max_len = dec_container->StrmDesc.user_data_volmax_len;
    plen = &(dec_container->StrmDesc.user_data_vollen);
    output = dec_container->StrmDesc.p_user_data_vol;
    break;

  case SC_GVOP_START:
    max_len = dec_container->StrmDesc.user_data_govmax_len;
    plen = &(dec_container->StrmDesc.user_data_govlen);
    output = dec_container->StrmDesc.p_user_data_gov;
    break;

  default:
    return (HANTRO_NOK);
  }

  MP4DEC_DEBUG(("SAVE USER DATA\n"));

  if(max_len && output)
    enable = 1;
  else
    enable = 0;

  MP4DEC_DEBUG(("SAVE USER DATA\n"));
  len = 0;

  ProcessUserData(dec_container);

  /* read (and save) user data */
  while(!IS_END_OF_STREAM(dec_container)) {

    MP4DEC_DEBUG(("SAVING USER DATA\n"));
    tmp = StrmDec_ShowBits(dec_container, 32);
    if((tmp >> 8) == 0x01) {
      /* start code */
      if(tmp != SC_UD_START)
        break;
      else {
        (void) StrmDec_FlushBits(dec_container, 32);
        /* "new" user data -> have to process it also */
        ProcessUserData(dec_container);
        continue;
      }
    }
    tmp = tmp >> 24;
    (void) StrmDec_FlushBits(dec_container, 8);

    if(enable && (len < max_len)) {
      /*lint --e(613) */
      *output++ = (u8) tmp;
    }

    len++;
  }

  /*lint --e(613) */
  *plen = len;

  /* did we catch something unsupported from the user data */
  if( dec_container->StrmStorage.unsupported_features_present )
    return (HANTRO_NOK);

  return (status);

}

/*------------------------------------------------------------------------------

    5.4. Function name: StrmDec_ClearHeaders

         Purpose:   Initialize data to default values

         Input:     pointer to DecHdrs

         Output:

------------------------------------------------------------------------------*/

void StrmDec_ClearHeaders(DecHdrs * hdrs) {
  u32 tmp;

  tmp = hdrs->profile_and_level_indication;

  (void)DWLmemset(hdrs, 0, sizeof(DecHdrs));

  /* restore profile and level */
  hdrs->profile_and_level_indication = tmp;

  /* have to be initialized into 1 to enable decoding stream without
   * VO-headers */
  hdrs->visual_object_verid = 1;
  hdrs->video_format = 5;
  hdrs->colour_primaries = 1;
  hdrs->transfer_characteristics = 1;
  hdrs->matrix_coefficients = 1;
  /* TODO */
  /*hdrs->low_delay = 0;*/

  return;

}

/*------------------------------------------------------------------------------

        Function name:
                IntraQuantMat

        Purpose:
                Read quact matrises from the stream
        Input:
                pointer to DecContainer

        Output:
                status (HANTRO_OK/NOK/END_OF_STREAM)

------------------------------------------------------------------------------*/

u32 QuantMat(DecContainer * dec_container, u32 intra) {

  u32 i;
  u32 tmp;
  u8 *p;

  p = dec_container->StrmStorage.quant_mat;

  if (!intra)
    p += 64;

  i = 1;
  tmp = StrmDec_GetBits(dec_container, 8);

  if (tmp == 0)
    return(HANTRO_NOK);

  p[0] = tmp;
  while(i < 64) {
    tmp = StrmDec_GetBits(dec_container, 8);
    if (tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    if (tmp == 0)
      break;
    p[zig_zag[i++]] = tmp;
  }
  tmp = p[zig_zag[i-1]];
  for (; i < 64; i++)
    p[zig_zag[i]] = tmp;

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

        Function name:
                MP4DecSetLowDelay

        Purpose:
                Based on profile set in stream, decide value for low delay
                (deprecated)

        Input:
                pointer to DecContainer

        Output:
                status (HANTRO_OK/NOK/END_OF_STREAM)

------------------------------------------------------------------------------*/
static void MP4DecSetLowDelay(DecContainer * dec_container) {

  /*
  1=Simple Profile/Level 1
  2=Simple Profile/Level 2
  3=Simple Profile/Level 3
  4=Simple Profile/Level 4a
  5=Simple Profile/Level 5
  8=Simple Profile/Level 0
  9=Simple Profile/Level 0B
  50=Main Profile/Level 2
  51=Main Profile/Level 3
  52=Main Profile/Level 4
  */

#if 0
  tmp = dec_container->Hdrs.profile_and_level_indication;

  simple = (tmp == 1) || (tmp == 2) || (tmp == 3) ||
           (tmp == 4) || (tmp == 5) || (tmp == 8) || (tmp == 9) ||
           (tmp == 50) || (tmp == 51) || (tmp == 52) ||
           (tmp == 0) /* in case VOS headers missing */;

  dec_container->Hdrs.low_delay = simple ? 1 : 0;
#endif

  dec_container->Hdrs.low_delay = MP4DecBFrameSupport(dec_container) ? 0 : 1;

}
