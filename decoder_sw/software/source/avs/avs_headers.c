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

#include "avs_headers.h"
#include "avs_utils.h"
#include "avs_strm.h"
#include "avs_cfg.h"
#include "avs_vlc.h"

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

   5.1  Function name:
                AvsStrmDec_DecodeSequenceHeader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 AvsStrmDec_DecodeSequenceHeader(DecContainer * dec_container) {
  u32 tmp;
  DecHdrs *p_hdr;

  ASSERT(dec_container);

  AVSDEC_DEBUG(("Decode Sequence Header Start\n"));

#ifdef USE_EXTERNAL_BUFFER
  dec_container->no_reallocation = 1;
#endif

  p_hdr = dec_container->StrmStorage.strm_dec_ready == FALSE ?
          &dec_container->Hdrs : &dec_container->tmp_hdrs;

  /* profile_id (needs to be checked, affects the fields that need to be
   * read from the stream) */
  tmp = AvsStrmDec_GetBits(dec_container, 8);
  if ((tmp != 0x20 && /* JIZHUN_PROFILE_ID */
       tmp != 0x48) || /* GUANGDIAN_PROFILE_ID, Broadcasting profile in avs+ */
      (!dec_container->avs_plus_support && tmp == 0x48)) { /* Check HW support for AVS+ */
    AVSDEC_DEBUG(("UNSUPPORTED PROFILE 0x%x\n", tmp));
    dec_container->StrmStorage.unsupported_features_present = 1;
    return HANTRO_NOK;
  }

  p_hdr->profile_id = tmp;

  /* level_id */
  tmp = p_hdr->level_id = AvsStrmDec_GetBits(dec_container, 8);

  tmp = p_hdr->progressive_sequence = AvsStrmDec_GetBits(dec_container, 1);

  tmp = p_hdr->horizontal_size = AvsStrmDec_GetBits(dec_container, 14);
  if(!tmp)
    return (HANTRO_NOK);

  tmp = p_hdr->vertical_size = AvsStrmDec_GetBits(dec_container, 14);
  if(!tmp)
    return (HANTRO_NOK);

  tmp = p_hdr->chroma_format = AvsStrmDec_GetBits(dec_container, 2);
  if (p_hdr->chroma_format != 1) { /* only 4:2:0 supported */
    AVSDEC_DEBUG(("UNSUPPORTED CHROMA_FORMAT\n"));
    dec_container->StrmStorage.unsupported_features_present = 1;
    return (HANTRO_NOK);
  }

  /* sample_precision, shall be 8-bit */
  tmp = AvsStrmDec_GetBits(dec_container, 3);
  if (tmp != 1) {
    dec_container->StrmStorage.unsupported_features_present = 1;
    return (HANTRO_NOK);
  }

  tmp = p_hdr->aspect_ratio = AvsStrmDec_GetBits(dec_container, 4);
  tmp = p_hdr->frame_rate_code = AvsStrmDec_GetBits(dec_container, 4);

  /* bit_rate_lower */
  tmp = p_hdr->bit_rate_value = AvsStrmDec_GetBits(dec_container, 18);

  /* marker */
  tmp = AvsStrmDec_GetBits(dec_container, 1);

  /* bit_rate_upper */
  tmp = AvsStrmDec_GetBits(dec_container, 12);
  p_hdr->bit_rate_value |= tmp << 18;

  tmp = dec_container->Hdrs.low_delay = AvsStrmDec_GetBits(dec_container, 1);

  /* marker */
  tmp = AvsStrmDec_GetBits(dec_container, 1);

  tmp = p_hdr->bbv_buffer_size = AvsStrmDec_GetBits(dec_container, 18);

  /* reserved_bits, shall be '000', not checked */
  tmp = AvsStrmDec_GetBits(dec_container, 3);

  if(dec_container->StrmStorage.strm_dec_ready) {
    /* Update parameters */
    dec_container->Hdrs.progressive_sequence = p_hdr->progressive_sequence;

    if (p_hdr->horizontal_size != dec_container->Hdrs.horizontal_size ||
        p_hdr->vertical_size != dec_container->Hdrs.vertical_size) {
      dec_container->ApiStorage.first_headers = 1;
      dec_container->StrmStorage.strm_dec_ready = HANTRO_FALSE;
      /* delayed resolution change */
      if (!dec_container->StrmStorage.sequence_low_delay) {
        dec_container->StrmStorage.new_headers_change_resolution = 1;
      } else {
        dec_container->Hdrs.horizontal_size = p_hdr->horizontal_size;
        dec_container->Hdrs.vertical_size = p_hdr->vertical_size;
        dec_container->Hdrs.aspect_ratio = p_hdr->aspect_ratio;
        dec_container->Hdrs.frame_rate_code =
          p_hdr->frame_rate_code;
        dec_container->Hdrs.bit_rate_value =
          p_hdr->bit_rate_value;
      }
    }

    if (dec_container->StrmStorage.sequence_low_delay &&
        !dec_container->Hdrs.low_delay)
      dec_container->StrmStorage.sequence_low_delay = 0;

  } else
    dec_container->StrmStorage.sequence_low_delay =
      dec_container->Hdrs.low_delay;

#ifdef USE_EXTERNAL_BUFFER
  if (((dec_container->Hdrs.horizontal_size + 15) >> 4) *
      ((dec_container->Hdrs.vertical_size + 15) >> 4) * 384 >
      (dec_container->use_adaptive_buffers ? dec_container->n_ext_buf_size :
       (dec_container->StrmStorage.frame_width * dec_container->StrmStorage.frame_height * 384)))
    dec_container->no_reallocation = 0;
#endif

  dec_container->StrmStorage.frame_width =
    (dec_container->Hdrs.horizontal_size + 15) >> 4;

  dec_container->StrmStorage.frame_height =
    (dec_container->Hdrs.vertical_size + 15) >> 4;

  /*    if(dec_container->Hdrs.progressive_sequence)
          dec_container->StrmStorage.frame_height =
              (dec_container->Hdrs.vertical_size + 15) >> 4;
      else
          dec_container->StrmStorage.frame_height =
              2 * ((dec_container->Hdrs.vertical_size + 31) >> 5);*/

  dec_container->StrmStorage.total_mbs_in_frame =
    (dec_container->StrmStorage.frame_width *
     dec_container->StrmStorage.frame_height);

  AVSDEC_DEBUG(("Decode Sequence Header Done\n"));

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                AvsStrmDec_GenWeightQuantParam

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 AvsStrmDec_GenWeightQuantParam(DecHdrs *p_hdr) {
  const u32 weighting_quant_param_default[] = {128, 98, 106, 116, 116, 128};
  const u32 weighting_quant_param_base1[]   = {135, 143, 143, 160, 160, 213};
  const u32 weighting_quant_param_base2[]   = {128, 98, 106, 116, 116, 128};
  u32 *wq_p = (u32 *)p_hdr->weighting_quant_param;
  u32 i;

  if (p_hdr->weighting_quant_flag == 0) {
    /* needn't generate this param */
    for(i=0; i<6; i++) {
      wq_p[i] = 128;
    }
    return 0;
  }

  if(p_hdr->weighting_quant_param_index == 0x0) {
    for(i=0; i<6; i++) {
      wq_p[i] = weighting_quant_param_default[i];
    }
  } else if (p_hdr->weighting_quant_param_index == 0x1) {
    for(i=0; i<6; i++) {
      wq_p[i] = weighting_quant_param_base1[i] +
                p_hdr->weighting_quant_param_delta1[i];
    }
  } else if (p_hdr->weighting_quant_param_index == 0x2) {
    for(i=0; i<6; i++) {
      wq_p[i] = weighting_quant_param_base2[i] +
                p_hdr->weighting_quant_param_delta2[i];
    }
  } else {
    /* shouldn't happen */
    AVSDEC_DEBUG(("AvsStrmDec_GenWeightQuantParam: Something went wrong!\n"));
    for(i=0; i<6; i++) {
      wq_p[i] = 128;
    }
  }

  return 1;
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                AvsStrmDec_DecodeIPictureHeader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 AvsStrmDec_DecodeIPictureHeader(DecContainer * dec_container) {
  u32 tmp, val;
  DecHdrs *p_hdr;

  ASSERT(dec_container);

  AVSDEC_DEBUG(("Decode I Picture Header Start\n"));

  p_hdr = &dec_container->Hdrs;

  p_hdr->pic_coding_type = IFRAME;

  /* bbv_delay */
  tmp = AvsStrmDec_GetBits(dec_container, 16);

  if (p_hdr->profile_id == 0x48) { /* broadcast profile in avs+ */
    /* marker_bit, its value should be 1 */
    tmp = AvsStrmDec_GetBits(dec_container, 1);
    /* bbv_delay_extension */
    tmp = AvsStrmDec_GetBits(dec_container, 7);
  }

  /* time_code_flag */
  tmp = AvsStrmDec_GetBits(dec_container, 1);
  if (tmp) {
    /* time_code */
    tmp = AvsStrmDec_GetBits(dec_container, 1); /* DropFrameFlag */
    tmp = AvsStrmDec_GetBits(dec_container, 5); /* TimeCodeHours */
    p_hdr->time_code.hours = tmp;
    tmp = AvsStrmDec_GetBits(dec_container, 6); /* TimeCodeMinutes */
    p_hdr->time_code.minutes = tmp;
    tmp = AvsStrmDec_GetBits(dec_container, 6); /* TimeCodeSeconds */
    p_hdr->time_code.seconds = tmp;
    tmp = AvsStrmDec_GetBits(dec_container, 6); /* TimeCodePictures */
    p_hdr->time_code.picture = tmp;
  }

  tmp = AvsStrmDec_GetBits(dec_container, 1);

  tmp = p_hdr->picture_distance = AvsStrmDec_GetBits(dec_container, 8);

  if (p_hdr->low_delay)
    /* bbv_check_times */
    tmp = AvsDecodeExpGolombUnsigned(dec_container, &val);

  tmp = p_hdr->progressive_frame = AvsStrmDec_GetBits(dec_container, 1);

  if (!tmp) {
    tmp = p_hdr->picture_structure = AvsStrmDec_GetBits(dec_container, 1);
  } else p_hdr->picture_structure = FRAMEPICTURE;

  tmp = p_hdr->top_field_first = AvsStrmDec_GetBits(dec_container, 1);
  tmp = p_hdr->repeat_first_field = AvsStrmDec_GetBits(dec_container, 1);
  tmp = p_hdr->fixed_picture_qp = AvsStrmDec_GetBits(dec_container, 1);
  tmp = p_hdr->picture_qp = AvsStrmDec_GetBits(dec_container, 6);

  if (p_hdr->progressive_frame == 0 && p_hdr->picture_structure == 0)
    tmp = p_hdr->skip_mode_flag = AvsStrmDec_GetBits(dec_container, 1);

  /* reserved_bits, shall be '0000', not checked */
  tmp = AvsStrmDec_GetBits(dec_container, 4);

  tmp = p_hdr->loop_filter_disable = AvsStrmDec_GetBits(dec_container, 1);
  if (!tmp) {
    tmp = AvsStrmDec_GetBits(dec_container, 1);
    if (tmp) {
      tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
      p_hdr->alpha_offset = (i32)val;
      if (p_hdr->alpha_offset < -8 || p_hdr->alpha_offset > 8)
        return (HANTRO_NOK);
      tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
      p_hdr->beta_offset = (i32)val;
      if (p_hdr->beta_offset < -8 || p_hdr->beta_offset > 8)
        return (HANTRO_NOK);
    }
  }

  /* for AvsSetRegs() setting reg convenience  */
  p_hdr->no_forward_reference_flag = 0;
  p_hdr->pb_field_enhanced_flag = 0;
  p_hdr->weighting_quant_flag = 0;
  p_hdr->aec_enable = 0;

  if (p_hdr->profile_id == 0x48) { /* broadcast profile in avs+ */
    /* weighting_quant_flag */
    tmp = AvsStrmDec_GetBits(dec_container, 1);
    p_hdr->weighting_quant_flag = tmp;
    if(tmp == 0x1) {
      u32 i;
      /* reserved_bits, shall be '0', not checked */
      tmp = AvsStrmDec_GetBits(dec_container, 1);
      /* chroma_quant_param_disable */
      tmp = AvsStrmDec_GetBits(dec_container, 1);
      p_hdr->chroma_quant_param_disable = tmp;
      if(tmp == 0x0) {
        /* chroma_quant_param_delta_cb */
        tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
        p_hdr->chroma_quant_param_delta_cb = (i32)val;
        /* chroma_quant_param_delta_cr */
        tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
        p_hdr->chroma_quant_param_delta_cr = (i32)val;
      }

      /* weighting_quant_param_index */
      tmp = AvsStrmDec_GetBits(dec_container, 2);
      p_hdr->weighting_quant_param_index = tmp;
      /* weighting_quant_model */
      tmp = AvsStrmDec_GetBits(dec_container, 2);
      p_hdr->weighting_quant_model = (tmp == 0x3) ? 0 : tmp;

      if(p_hdr->weighting_quant_param_index == 0x1) {
        for(i=0; i<6; i++) {
          tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
          p_hdr->weighting_quant_param_delta1[i] = (i32)val;
        }
      } else {
        for(i=0; i<6; i++) {
          p_hdr->weighting_quant_param_delta1[i] = 0;
        }
      }

      if(p_hdr->weighting_quant_param_index == 0x2) {
        for(i=0; i<6; i++) {
          tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
          p_hdr->weighting_quant_param_delta2[i] = (i32)val;
        }
      } else {
        for(i=0; i<6; i++) {
          p_hdr->weighting_quant_param_delta2[i] = 0;
        }
      }
    }

    /* generate wq_p[m][6] */
    AvsStrmDec_GenWeightQuantParam(p_hdr);

    /* aec_enable */
    p_hdr->aec_enable = AvsStrmDec_GetBits(dec_container, 1);
  }

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                AvsStrmDec_DecodePBPictureHeader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 AvsStrmDec_DecodePBPictureHeader(DecContainer * dec_container) {
  u32 tmp, val;
  DecHdrs *p_hdr;

  ASSERT(dec_container);

  AVSDEC_DEBUG(("Decode PB Picture Header Start\n"));

  p_hdr = &dec_container->Hdrs;

  /* bbv_delay */
  tmp = AvsStrmDec_GetBits(dec_container, 16);

  if (p_hdr->profile_id == 0x48) { /* broadcast profile in avs+ */
    /* marker_bit, its value should be 1 */
    tmp = AvsStrmDec_GetBits(dec_container, 1);
    /* bbv_delay_extension */
    tmp = AvsStrmDec_GetBits(dec_container, 7);
  }

  tmp = p_hdr->pic_coding_type = AvsStrmDec_GetBits(dec_container, 2)+1;
  if (tmp != PFRAME && tmp != BFRAME)
    return (HANTRO_NOK);

  tmp = p_hdr->picture_distance = AvsStrmDec_GetBits(dec_container, 8);

  if (p_hdr->low_delay)
    /* bbv_check_times */
    tmp = AvsDecodeExpGolombUnsigned(dec_container, &val);

  tmp = p_hdr->progressive_frame = AvsStrmDec_GetBits(dec_container, 1);

  if (!tmp) {
    tmp = p_hdr->picture_structure = AvsStrmDec_GetBits(dec_container, 1);
    if (tmp == 0)
      tmp = p_hdr->advanced_pred_mode_disable =
              AvsStrmDec_GetBits(dec_container, 1);
  } else p_hdr->picture_structure = FRAMEPICTURE;

  tmp = p_hdr->top_field_first = AvsStrmDec_GetBits(dec_container, 1);
  tmp = p_hdr->repeat_first_field = AvsStrmDec_GetBits(dec_container, 1);
  tmp = p_hdr->fixed_picture_qp = AvsStrmDec_GetBits(dec_container, 1);
  tmp = p_hdr->picture_qp = AvsStrmDec_GetBits(dec_container, 6);

  if (!(p_hdr->pic_coding_type == BFRAME && p_hdr->picture_structure == 1)) {
    tmp = p_hdr->picture_reference_flag = AvsStrmDec_GetBits(dec_container, 1);
  }

  if (p_hdr->profile_id == 0x48) {
    /* no_forward_reference_flag */
    p_hdr->no_forward_reference_flag = AvsStrmDec_GetBits(dec_container, 1);
    /* pb_field_enhanced_flag */
    p_hdr->pb_field_enhanced_flag = AvsStrmDec_GetBits(dec_container, 1);
  } else {
    /* no_forward_reference_flag */
    p_hdr->no_forward_reference_flag = AvsStrmDec_GetBits(dec_container, 1);
    /* TODO AVS should be confirmed? */
    p_hdr->no_forward_reference_flag = 0;
    /* pb_field_enhanced_flag */
    p_hdr->pb_field_enhanced_flag = AvsStrmDec_GetBits(dec_container, 1);
    p_hdr->pb_field_enhanced_flag = 0;
  }
  /* reserved_bits, shall be '00', not checked */
  tmp = AvsStrmDec_GetBits(dec_container, 2);

  tmp = p_hdr->skip_mode_flag = AvsStrmDec_GetBits(dec_container, 1);

  tmp = p_hdr->loop_filter_disable = AvsStrmDec_GetBits(dec_container, 1);
  if (!tmp) {
    tmp = AvsStrmDec_GetBits(dec_container, 1);
    if (tmp) {
      tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
      p_hdr->alpha_offset = (i32)val;
      if (p_hdr->alpha_offset < -8 || p_hdr->alpha_offset > 8)
        return (HANTRO_NOK);
      tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
      p_hdr->beta_offset = (i32)val;
      if (p_hdr->beta_offset < -8 || p_hdr->beta_offset > 8)
        return (HANTRO_NOK);
    }
  }

  p_hdr->weighting_quant_flag = 0;
  p_hdr->aec_enable = 0;

  if (p_hdr->profile_id == 0x48) { /* broadcast profile in avs+ */
    /* weighting_quant_flag */
    tmp = AvsStrmDec_GetBits(dec_container, 1);
    p_hdr->weighting_quant_flag = tmp;
    if (tmp == 0x1) {
      u32 i;
      /* reserved_bits, shall be '0', not checked */
      tmp = AvsStrmDec_GetBits(dec_container, 1);
      /* chroma_quant_param_disable */
      tmp = AvsStrmDec_GetBits(dec_container, 1);
      p_hdr->chroma_quant_param_disable = tmp;
      if (tmp == 0x0) {
        /* chroma_quant_param_delta_cb */
        tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
        p_hdr->chroma_quant_param_delta_cb = (i32)val;
        /* chroma_quant_param_delta_cr */
        tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
        p_hdr->chroma_quant_param_delta_cr = (i32)val;
      }

      /* weighting_quant_param_index */
      tmp = AvsStrmDec_GetBits(dec_container, 2);
      p_hdr->weighting_quant_param_index = tmp;
      /* weighting_quant_model */
      tmp = AvsStrmDec_GetBits(dec_container, 2);
      p_hdr->weighting_quant_model = (tmp == 0x3) ? 0 : tmp;

      if (p_hdr->weighting_quant_param_index == 0x1) {
        for(i=0; i<6; i++) {
          tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
          p_hdr->weighting_quant_param_delta1[i] = (i32)val;
        }
      }

      if (p_hdr->weighting_quant_param_index == 0x2) {
        for(i=0; i<6; i++) {
          tmp = AvsDecodeExpGolombSigned(dec_container, (i32*)&val);
          p_hdr->weighting_quant_param_delta2[i] = (i32)val;
        }
      }
    }

    /* generate wq_p[m][6] */
    AvsStrmDec_GenWeightQuantParam(p_hdr);

    /* aec_enable */
    p_hdr->aec_enable = AvsStrmDec_GetBits(dec_container, 1);
  }

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.1  Function name:
                AvsStrmDec_DecodeExtensionHeader

        Purpose:
                Decodes AVS extension headers

        Input:
                pointer to DecContainer

        Output:
                status (OK/NOK/END_OF_STREAM/... enum in .h file!)

------------------------------------------------------------------------------*/
u32 AvsStrmDec_DecodeExtensionHeader(DecContainer * dec_container) {
  u32 extension_start_code;
  u32 status = HANTRO_OK;

  /* get extension header ID */
  extension_start_code = AvsStrmDec_GetBits(dec_container, 4);

  switch (extension_start_code) {
  case SC_SEQ_DISPLAY_EXT:
    /* sequence display extension header */
    status = AvsStrmDec_DecodeSeqDisplayExtHeader(dec_container);
    break;

  default:
    break;
  }

  return (status);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                AvsStrmDec_DecodeSeqDisplayExtHeader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 AvsStrmDec_DecodeSeqDisplayExtHeader(DecContainer * dec_container) {
  u32 tmp;

  ASSERT(dec_container);

  AVSDEC_DEBUG(("Decode Sequence Display Extension Header Start\n"));

  tmp = dec_container->Hdrs.video_format =
          AvsStrmDec_GetBits(dec_container, 3);
  tmp = dec_container->Hdrs.sample_range =
          AvsStrmDec_GetBits(dec_container, 1);
  tmp = dec_container->Hdrs.color_description =
          AvsStrmDec_GetBits(dec_container, 1);

  if(dec_container->Hdrs.color_description) {
    tmp = dec_container->Hdrs.color_primaries =
            AvsStrmDec_GetBits(dec_container, 8);
    tmp = dec_container->Hdrs.transfer_characteristics =
            AvsStrmDec_GetBits(dec_container, 8);
    tmp = dec_container->Hdrs.matrix_coefficients =
            AvsStrmDec_GetBits(dec_container, 8);
  }

  tmp = dec_container->Hdrs.display_horizontal_size =
          AvsStrmDec_GetBits(dec_container, 14);

  /* marker bit ==> flush */
  tmp = AvsStrmDec_FlushBits(dec_container, 1);

  tmp = dec_container->Hdrs.display_vertical_size =
          AvsStrmDec_GetBits(dec_container, 14);

  /* reserved_bits */
  tmp = AvsStrmDec_GetBits(dec_container, 2);

  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  AVSDEC_DEBUG(("Decode Sequence Display Extension Header Done\n"));

  return (HANTRO_OK);
}
