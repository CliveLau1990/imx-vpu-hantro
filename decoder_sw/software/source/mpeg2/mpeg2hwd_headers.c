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

#include "mpeg2hwd_headers.h"
#include "mpeg2hwd_utils.h"
#include "mpeg2hwd_strm.h"
#include "mpeg2hwd_debug.h"
#include "mpeg2hwd_cfg.h"
#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif
/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/
#define ZIGZAG         0
#define ALTERNATE      1

/* zigzag/alternate */
static const u8 scan_order[2][64] = {
  {
    /* zig-zag */
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
  },

  {
    /* Alternate */
    0, 8, 16, 24, 1, 9, 2, 10,
    17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18, 3, 11, 4, 12,
    19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28, 5, 13, 6, 14,
    21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30, 7, 15, 23, 31,
    38, 46, 54, 62, 39, 47, 55, 63
  }
};

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

/* Default intra matrix */
static const u8 intra_default_qmatrix[64] = {
  8, 16, 19, 22, 26, 27, 29, 34,
  16, 16, 22, 24, 27, 29, 34, 37,
  19, 22, 26, 27, 29, 34, 34, 38,
  22, 22, 26, 27, 29, 34, 37, 40,
  22, 26, 27, 29, 32, 35, 40, 48,
  26, 27, 29, 32, 35, 40, 48, 58,
  26, 27, 29, 34, 38, 46, 56, 69,
  27, 29, 35, 38, 46, 56, 69, 83
};

#define DIMENSION_MASK 0x0FFF

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.1  Function name:
                mpeg2_strm_dec_decode_sequence_header

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_sequence_header(DecContainer * dec_container) {
  u32 i;
  u32 tmp;
  DecHdrs *p_hdr;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode Sequence Header Start\n"));
#ifndef USE_EXTERNAL_BUFFER
  dec_container->no_reallocation = 0;
#else
  dec_container->no_reallocation = 1;
#endif

  p_hdr = dec_container->StrmStorage.strm_dec_ready == FALSE ?
          &dec_container->Hdrs : &dec_container->tmp_hdrs;

  /* read parameters from stream */
  tmp = p_hdr->horizontal_size = mpeg2_strm_dec_get_bits(dec_container, 12);
  if(!tmp)
    return (HANTRO_NOK);
  tmp = p_hdr->vertical_size = mpeg2_strm_dec_get_bits(dec_container, 12);
  if(!tmp)
    return (HANTRO_NOK);
  tmp = p_hdr->aspect_ratio_info = mpeg2_strm_dec_get_bits(dec_container, 4);
  tmp = p_hdr->frame_rate_code = mpeg2_strm_dec_get_bits(dec_container, 4);
  tmp = p_hdr->bit_rate_value = mpeg2_strm_dec_get_bits(dec_container, 18);

  /* marker bit ==> flush */
  tmp = mpeg2_strm_dec_flush_bits(dec_container, 1);

  tmp = p_hdr->vbv_buffer_size = mpeg2_strm_dec_get_bits(dec_container, 10);

  tmp = p_hdr->constr_parameters = mpeg2_strm_dec_get_bits(dec_container, 1);

  /* check if need to load an q-matrix */
  tmp = p_hdr->load_intra_matrix = mpeg2_strm_dec_get_bits(dec_container, 1);

  if(p_hdr->load_intra_matrix == 1) {
    /* load intra matrix */
    for(i = 0; i < 64; i++) {
      tmp = p_hdr->q_table_intra[scan_order[ZIGZAG][i]] =
              mpeg2_strm_dec_get_bits(dec_container, 8);
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
    }
  } else {
    /* use default intra matrix */
    for(i = 0; i < 64; i++) {
      p_hdr->q_table_intra[i] = intra_default_qmatrix[i];
    }
  }

  tmp = p_hdr->load_non_intra_matrix = mpeg2_strm_dec_get_bits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  if(p_hdr->load_non_intra_matrix) {
    /* load non-intra matrix */
    for(i = 0; i < 64; i++) {
      tmp = p_hdr->q_table_non_intra[scan_order[ZIGZAG][i]] =
              mpeg2_strm_dec_get_bits(dec_container, 8);
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
    }

  } else {
    /* use default non-intra matrix */
    for(i = 0; i < 64; i++) {
      p_hdr->q_table_non_intra[i] = 16;
    }
  }

  /* headers already successfully decoded -> only use new quantization
   * tables */
  if(dec_container->StrmStorage.strm_dec_ready) {
    for(i = 0; i < 64; i++) {
      dec_container->Hdrs.q_table_intra[i] =
        p_hdr->q_table_intra[i];
      dec_container->Hdrs.q_table_non_intra[i] =
        p_hdr->q_table_non_intra[i];
    }

#ifdef ENABLE_NON_STANDARD_FEATURES
    /* Update parameters */
    if(p_hdr->horizontal_size != dec_container->Hdrs.horizontal_size ||
        p_hdr->vertical_size != dec_container->Hdrs.vertical_size) {
      dec_container->ApiStorage.first_headers = 1;
      dec_container->StrmStorage.strm_dec_ready = HANTRO_FALSE;
      /* If there have been B frames yet, we need to do extra round
       * in the API to get the last frame out */
      if( !dec_container->Hdrs.low_delay ) {
        dec_container->StrmStorage.new_headers_change_resolution = 1;
        /* Resolution change delayed */
      } else {
        dec_container->Hdrs.horizontal_size = p_hdr->horizontal_size;
        dec_container->Hdrs.vertical_size = p_hdr->vertical_size;
      }
    }
    if(p_hdr->aspect_ratio_info != dec_container->Hdrs.aspect_ratio_info) {
      dec_container->StrmStorage.strm_dec_ready = HANTRO_FALSE;
#ifndef USE_EXTERNAL_BUFFER
      dec_container->no_reallocation = 1;
#endif
    }

    /* Set rest of parameters */
    dec_container->Hdrs.bit_rate_value = p_hdr->bit_rate_value;
    dec_container->Hdrs.vbv_buffer_size = p_hdr->vbv_buffer_size;
    dec_container->Hdrs.constr_parameters = p_hdr->constr_parameters;
    dec_container->Hdrs.frame_rate_code = p_hdr->frame_rate_code;
    dec_container->Hdrs.aspect_ratio_info = p_hdr->aspect_ratio_info;
#endif
  }
#ifdef USE_EXTERNAL_BUFFER
  if ((((dec_container->Hdrs.horizontal_size + 15) >> 4) *
       ((dec_container->Hdrs.vertical_size + 15) >> 4) * 384 >
       (dec_container->use_adaptive_buffers ? dec_container->n_ext_buf_size :
        (dec_container->FrameDesc.frame_width * dec_container->FrameDesc.frame_height * 384))) ||
      dec_container->StrmStorage.num_buffers == 0)
    dec_container->no_reallocation = 0;
#endif

  dec_container->FrameDesc.frame_width =
    (dec_container->Hdrs.horizontal_size + 15) >> 4;
  dec_container->FrameDesc.frame_height =
    (dec_container->Hdrs.vertical_size + 15) >> 4;
  dec_container->FrameDesc.total_mb_in_frame =
    (dec_container->FrameDesc.frame_width *
     dec_container->FrameDesc.frame_height);

  MPEG2DEC_DEBUG(("Decode Sequence Header Done\n"));

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

   5.2  Function name:
                mpeg2_strm_dec_decode_gopheader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_gopheader(DecContainer * dec_container) {
  u32 tmp;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode GOP (Group of Pictures) Header Start\n"));

  /* read parameters from stream */
  tmp = dec_container->Hdrs.time.drop_flag =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->FrameDesc.time_code_hours =
          mpeg2_strm_dec_get_bits(dec_container, 5);
  tmp = dec_container->FrameDesc.time_code_minutes =
          mpeg2_strm_dec_get_bits(dec_container, 6);

  /* marker bit ==> flush */
  tmp = mpeg2_strm_dec_flush_bits(dec_container, 1);

  tmp = dec_container->FrameDesc.time_code_seconds =
          mpeg2_strm_dec_get_bits(dec_container, 6);
  tmp = dec_container->FrameDesc.frame_time_pictures =
          mpeg2_strm_dec_get_bits(dec_container, 6);

  tmp = dec_container->Hdrs.closed_gop =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.broken_link =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  MPEG2DEC_DEBUG(("Decode GOP (Group of Pictures) Header Done\n"));

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                mpeg2_strm_dec_decode_picture_header

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_picture_header(DecContainer * dec_container) {
  u32 tmp;
  u32 extra_info_byte_count = 0;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode Picture Header Start\n"));

  /* read parameters from stream */
  tmp = dec_container->Hdrs.temporal_reference =
          mpeg2_strm_dec_get_bits(dec_container, 10);
  tmp = dec_container->Hdrs.picture_coding_type =
          dec_container->FrameDesc.pic_coding_type =
            mpeg2_strm_dec_get_bits(dec_container, 3);

  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);
  if(tmp < IFRAME || tmp > DFRAME) {
    dec_container->FrameDesc.pic_coding_type = PFRAME;
    return (HANTRO_NOK);
  }

#ifdef _MPEG2_8170_UNIT_TEST
  if(dec_container->Hdrs.picture_coding_type == IFRAME)
    printf("PICTURE CODING TYPE: IFRAME\n");
  else if(dec_container->Hdrs.picture_coding_type == PFRAME)
    printf("PICTURE CODING TYPE: PFRAME\n");
  else if(dec_container->Hdrs.picture_coding_type == BFRAME)
    printf("PICTURE CODING TYPE: BFRAME\n");
  else
    printf("PICTURE CODING TYPE: DFRAME\n");
#endif

#if 0
#ifdef ASIC_TRACE_SUPPORT
  if(dec_container->Hdrs.picture_coding_type == IFRAME)
    trace_mpeg2_dec_tools.pic_coding_type.i_coded = 1;

  else if(dec_container->Hdrs.picture_coding_type == PFRAME)
    trace_mpeg2_dec_tools.pic_coding_type.p_coded = 1;

  else if(dec_container->Hdrs.picture_coding_type == BFRAME)
    trace_mpeg2_dec_tools.pic_coding_type.b_coded = 1;

  else if(dec_container->Hdrs.picture_coding_type == DFRAME)
    trace_mpeg2_dec_tools.d_coded = 1;
#endif
#endif

  tmp = dec_container->Hdrs.vbv_delay =
          mpeg2_strm_dec_get_bits(dec_container, 16);

  if(dec_container->Hdrs.picture_coding_type == PFRAME ||
      dec_container->Hdrs.picture_coding_type == BFRAME) {
    tmp = dec_container->Hdrs.f_code[0][0] =
            mpeg2_strm_dec_get_bits(dec_container, 1);
    tmp = dec_container->Hdrs.f_code[0][1] =
            mpeg2_strm_dec_get_bits(dec_container, 3);
    if(dec_container->Hdrs.mpeg2_stream == MPEG1 &&
        dec_container->Hdrs.f_code[0][1] == 0) {
      return (HANTRO_NOK);
    }
  }

  if(dec_container->Hdrs.picture_coding_type == BFRAME) {
    tmp = dec_container->Hdrs.f_code[1][0] =
            mpeg2_strm_dec_get_bits(dec_container, 1);
    tmp = dec_container->Hdrs.f_code[1][1] =
            mpeg2_strm_dec_get_bits(dec_container, 3);
    if(dec_container->Hdrs.mpeg2_stream == MPEG1 &&
        dec_container->Hdrs.f_code[1][1] == 0)
      return (HANTRO_NOK);
  }

  if(dec_container->Hdrs.mpeg2_stream == MPEG1) {
    /* forward */
    dec_container->Hdrs.f_code_fwd_hor = dec_container->Hdrs.f_code[0][1];
    dec_container->Hdrs.f_code_fwd_ver = dec_container->Hdrs.f_code[0][1];

    /* backward */
    dec_container->Hdrs.f_code_bwd_hor = dec_container->Hdrs.f_code[1][1];
    dec_container->Hdrs.f_code_bwd_ver = dec_container->Hdrs.f_code[1][1];
  }

  /* handle extra bit picture */
  while(mpeg2_strm_dec_get_bits(dec_container, 1)) {
    /* flush */
    tmp = mpeg2_strm_dec_flush_bits(dec_container, 8);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    extra_info_byte_count++;
  }

  /* update extra info byte count */
  dec_container->Hdrs.extra_info_byte_count = extra_info_byte_count;

  MPEG2DEC_DEBUG(("Decode Picture Header Done\n"));

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.1  Function name:
                mpeg2_strm_dec_decode_extension_header

        Purpose:
                Decodes MPEG-2 extension headers

        Input:
                pointer to DecContainer

        Output:
                status (OK/NOK/END_OF_STREAM/... enum in .h file!)

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_extension_header(DecContainer * dec_container) {
  u32 extension_start_code;
  u32 status = HANTRO_OK;

  /* get extension header ID */
  extension_start_code = mpeg2_strm_dec_get_bits(dec_container, 4);

  switch (extension_start_code) {
  case SC_SEQ_EXT:
    /* sequence extension header, decoded only if
     * 1) decoder still in "initialization" phase
     * 2) sequence header was properly decoded */
    if(!dec_container->StrmStorage.error_in_hdr) {
      status = mpeg2_strm_dec_decode_seq_ext_header(dec_container);
      if(status != HANTRO_OK)
        dec_container->StrmStorage.valid_sequence = 0;
    }
    break;

  case SC_SEQ_DISPLAY_EXT:
    /* sequence display extension header */
    status = mpeg2_strm_dec_decode_seq_display_ext_header(dec_container);
    break;

  case SC_QMATRIX_EXT:
    /* Q matrix extension header */
    status = mpeg2_strm_dec_decode_qmatrix_ext_header(dec_container);
    break;

  case SC_PIC_DISPLAY_EXT:
    /* picture display extension header */
    status = mpeg2_strm_dec_decode_pic_display_ext_header(dec_container);
    break;

  case SC_PIC_CODING_EXT:
    /* picture coding extension header, decoded only if decoder is locked
     * to MPEG-2 mode */
    if(dec_container->StrmStorage.strm_dec_ready &&
        dec_container->Hdrs.mpeg2_stream) {
      status = mpeg2_strm_dec_decode_pic_coding_ext_header(dec_container);
      if(status != HANTRO_OK)
        status = DEC_PIC_HDR_RDY_ERROR;
      /* wrong parity field, or wrong coding type for second field */
      else if(!dec_container->ApiStorage.ignore_field &&
              !dec_container->ApiStorage.first_field &&
              (((dec_container->ApiStorage.parity == 0 &&
                 dec_container->Hdrs.picture_structure != 2) ||
                (dec_container->ApiStorage.parity == 1 &&
                 dec_container->Hdrs.picture_structure != 1)) ||
               ((dec_container->Hdrs.picture_coding_type !=
                 dec_container->StrmStorage.prev_pic_coding_type) &&
                (dec_container->Hdrs.picture_coding_type == BFRAME ||
                 dec_container->StrmStorage.prev_pic_coding_type ==
                 BFRAME)))) {
        /* prev pic I/P and new B  ->.field) -> freeze previous and
         * ignore curent field/frame */
        if(dec_container->Hdrs.picture_coding_type == BFRAME &&
            dec_container->StrmStorage.prev_pic_coding_type != BFRAME) {
          status = DEC_PIC_HDR_RDY_ERROR;
          dec_container->Hdrs.picture_coding_type =
            dec_container->StrmStorage.prev_pic_coding_type;
          dec_container->Hdrs.picture_structure =
            dec_container->StrmStorage.prev_pic_structure;
        }
        /* continue decoding, assume first field */
        else {
          dec_container->StrmStorage.valid_pic_ext_header = 1;
          dec_container->ApiStorage.parity =
            dec_container->Hdrs.picture_structure == 2;
          dec_container->ApiStorage.first_field = 1;
        }
        /* set flag to release HW and wait PP if left running */
        dec_container->unpaired_field = 1;
      }
      /* first field resulted in picture freeze -> ignore 2. field */
      else if(dec_container->ApiStorage.ignore_field &&
              ((dec_container->ApiStorage.parity == 0 &&
                dec_container->Hdrs.picture_structure == 2) ||
               (dec_container->ApiStorage.parity == 1 &&
                dec_container->Hdrs.picture_structure == 1))) {
        dec_container->ApiStorage.first_field = 1;
        dec_container->ApiStorage.parity = 0;
      } else {
        dec_container->StrmStorage.valid_pic_ext_header = 1;
        dec_container->ApiStorage.parity =
          dec_container->Hdrs.picture_structure == 2;
        /* non-matching field after error -> assume start of
         * new picture */
        if(dec_container->ApiStorage.ignore_field)
          dec_container->ApiStorage.first_field = 1;
      }
      dec_container->ApiStorage.ignore_field = 0;
      dec_container->StrmStorage.prev_pic_coding_type =
        dec_container->Hdrs.picture_coding_type;
      dec_container->StrmStorage.prev_pic_structure =
        dec_container->Hdrs.picture_structure;
    }
    break;

  default:
    break;
  }

  return (status);

}

/*------------------------------------------------------------------------------

   5.2  Function name:
                mpeg2_strm_dec_decode_seq_ext_header

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_seq_ext_header(DecContainer * dec_container) {
  u32 tmp;
  DecHdrs *p_hdr;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode Sequence Extension Header Start\n"));

  p_hdr = dec_container->StrmStorage.strm_dec_ready == FALSE ?
          &dec_container->Hdrs : &dec_container->tmp_hdrs;

#if 0
#ifdef ASIC_TRACE_SUPPORT
  trace_mpeg2_dec_tools.decoding_mode = TRACE_MPEG2;
  /* the seuqnce type will be decided later and no MPEG-1 default is used */
  trace_mpeg2_dec_tools.sequence_type.interlaced = 0;
  trace_mpeg2_dec_tools.sequence_type.progressive = 0;
#endif
#endif

  /* read parameters from stream */
  tmp = p_hdr->profile_and_level_indication =
          mpeg2_strm_dec_get_bits(dec_container, 8);
  tmp = p_hdr->progressive_sequence =
          mpeg2_strm_dec_get_bits(dec_container, 1);

  tmp = p_hdr->chroma_format =
          mpeg2_strm_dec_get_bits(dec_container, 2);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);
  if(tmp != 1)    /* 4:2:0 */
    return (HANTRO_NOK);
  tmp = p_hdr->hor_size_extension =
          mpeg2_strm_dec_get_bits(dec_container, 2);
  tmp = p_hdr->ver_size_extension =
          mpeg2_strm_dec_get_bits(dec_container, 2);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  /* update parameters */
  p_hdr->horizontal_size =
    ((p_hdr->hor_size_extension << 12) | (dec_container->Hdrs.
                                          horizontal_size &
                                          DIMENSION_MASK));

  p_hdr->vertical_size =
    ((p_hdr->ver_size_extension << 12) | (dec_container->Hdrs.
                                          vertical_size &
                                          DIMENSION_MASK));

  tmp = p_hdr->bit_rate_extension =
          mpeg2_strm_dec_get_bits(dec_container, 12);

  /* marker bit ==> flush */
  tmp = mpeg2_strm_dec_flush_bits(dec_container, 1);

  tmp = p_hdr->vbv_buffer_size_extension =
          mpeg2_strm_dec_get_bits(dec_container, 8);

  tmp = p_hdr->low_delay = mpeg2_strm_dec_get_bits(dec_container, 1);

  tmp = p_hdr->frame_rate_extension_n =
          mpeg2_strm_dec_get_bits(dec_container, 2);
  tmp = p_hdr->frame_rate_extension_d =
          mpeg2_strm_dec_get_bits(dec_container, 5);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

#ifdef ENABLE_NON_STANDARD_FEATURES
  if(dec_container->StrmStorage.strm_dec_ready) {
    /* Update parameters */
    dec_container->Hdrs.profile_and_level_indication = p_hdr->profile_and_level_indication;
    dec_container->Hdrs.progressive_sequence = p_hdr->progressive_sequence;
    dec_container->Hdrs.chroma_format = p_hdr->chroma_format;
    dec_container->Hdrs.hor_size_extension = p_hdr->hor_size_extension;
    dec_container->Hdrs.ver_size_extension = p_hdr->ver_size_extension;
    dec_container->Hdrs.bit_rate_extension = p_hdr->bit_rate_extension;
    dec_container->Hdrs.vbv_buffer_size_extension = p_hdr->vbv_buffer_size_extension;
    dec_container->Hdrs.low_delay = p_hdr->low_delay;
    dec_container->Hdrs.frame_rate_extension_n = p_hdr->frame_rate_extension_n;
    dec_container->Hdrs.frame_rate_extension_d = p_hdr->frame_rate_extension_d;

    if(p_hdr->horizontal_size != dec_container->Hdrs.horizontal_size ||
        p_hdr->vertical_size != dec_container->Hdrs.vertical_size) {
      dec_container->ApiStorage.first_headers = 1;
      dec_container->StrmStorage.strm_dec_ready = FALSE;
      /* If there have been B frames yet, we need to do extra round
       * in the API to get the last frame out */
      if( !dec_container->Hdrs.low_delay ) {
        dec_container->StrmStorage.new_headers_change_resolution = 1;
        /* Resolution change delayed */
      } else {
        dec_container->Hdrs.horizontal_size = p_hdr->horizontal_size;
        dec_container->Hdrs.vertical_size = p_hdr->vertical_size;
      }
    }
  }
#endif

  /* update image dimensions */
  dec_container->FrameDesc.frame_width =
    (dec_container->Hdrs.horizontal_size + 15) >> 4;
  if(dec_container->Hdrs.progressive_sequence)
    dec_container->FrameDesc.frame_height =
      (dec_container->Hdrs.vertical_size + 15) >> 4;
  else
    dec_container->FrameDesc.frame_height =
      2 * ((dec_container->Hdrs.vertical_size + 31) >> 5);
  dec_container->FrameDesc.total_mb_in_frame =
    (dec_container->FrameDesc.frame_width *
     dec_container->FrameDesc.frame_height);

  MPEG2DEC_DEBUG(("Decode Sequence Extension Header Done\n"));

  /* Mark as a MPEG-2 stream */
  dec_container->Hdrs.mpeg2_stream = MPEG2;

  /* check if interlaced content */
  if(dec_container->Hdrs.progressive_sequence == 0)
    dec_container->Hdrs.interlaced = 1;
  else
    dec_container->Hdrs.interlaced = 0;

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                mpeg2_strm_dec_decode_seq_display_ext_header

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_seq_display_ext_header(DecContainer * dec_container) {
  u32 tmp;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode Sequence Display Extension Header Start\n"));

  tmp = dec_container->Hdrs.video_format =
          mpeg2_strm_dec_get_bits(dec_container, 3);
  tmp = dec_container->Hdrs.color_description =
          mpeg2_strm_dec_get_bits(dec_container, 1);

  if(dec_container->Hdrs.color_description) {
    tmp = dec_container->Hdrs.color_primaries =
            mpeg2_strm_dec_get_bits(dec_container, 8);
    tmp = dec_container->Hdrs.transfer_characteristics =
            mpeg2_strm_dec_get_bits(dec_container, 8);
    tmp = dec_container->Hdrs.matrix_coefficients =
            mpeg2_strm_dec_get_bits(dec_container, 8);
  }

  tmp = dec_container->Hdrs.display_horizontal_size =
          mpeg2_strm_dec_get_bits(dec_container, 14);

  /* marker bit ==> flush */
  tmp = mpeg2_strm_dec_flush_bits(dec_container, 1);

  tmp = dec_container->Hdrs.display_vertical_size =
          mpeg2_strm_dec_get_bits(dec_container, 14);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  MPEG2DEC_DEBUG(("Decode Sequence Display Extension Header Done\n"));

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                mpeg2StrmDec_DecodeSeqQMatrixExtHeader

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_qmatrix_ext_header(DecContainer * dec_container) {
  u32 i;
  u32 tmp;
  DecHdrs *p_hdr;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode Sequence Quant Matrix Extension Header Start\n"));

  p_hdr = &dec_container->tmp_hdrs;

  /* check if need to load an q-matrix */
  tmp = p_hdr->load_intra_matrix = mpeg2_strm_dec_get_bits(dec_container, 1);

  if(p_hdr->load_intra_matrix == 1) {
    /* load intra matrix */
    for(i = 0; i < 64; i++) {
      tmp = p_hdr->q_table_intra[scan_order[ZIGZAG][i]] =
              mpeg2_strm_dec_get_bits(dec_container, 8);
    }
  }

  tmp = p_hdr->load_non_intra_matrix = mpeg2_strm_dec_get_bits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  if(p_hdr->load_non_intra_matrix) {
    /* load non-intra matrix */
    for(i = 0; i < 64; i++) {
      tmp = p_hdr->q_table_non_intra[scan_order[ZIGZAG][i]] =
              mpeg2_strm_dec_get_bits(dec_container, 8);
      if(tmp == END_OF_STREAM)
        return (END_OF_STREAM);
    }
  }

  /* successfully decoded -> overwrite previous tables */
  if(p_hdr->load_intra_matrix) {
    for(i = 0; i < 64; i++)
      dec_container->Hdrs.q_table_intra[i] = p_hdr->q_table_intra[i];
  }
  if(p_hdr->load_non_intra_matrix) {
    for(i = 0; i < 64; i++)
      dec_container->Hdrs.q_table_non_intra[i] = p_hdr->q_table_non_intra[i];
  }

  MPEG2DEC_DEBUG(("Decode Sequence Quant Matrix Extension Header Done\n"));

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                mpeg2_strm_dec_decode_pic_coding_ext_header

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_pic_coding_ext_header(DecContainer * dec_container) {
  u32 i, j;
  u32 tmp;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode Picture Coding Extension Header Start\n"));

  /* read parameters from stream */
  for(i = 0; i < 2; i++) {
    for(j = 0; j < 2; j++) {
      tmp = dec_container->Hdrs.f_code[i][j] =
              mpeg2_strm_dec_get_bits(dec_container, 4);
      if(tmp == 0 && dec_container->Hdrs.picture_coding_type != IFRAME)
        return (HANTRO_NOK);
    }
  }

  /* forward */
  dec_container->Hdrs.f_code_fwd_hor = (dec_container->Hdrs.f_code[0][0]);
  dec_container->Hdrs.f_code_fwd_ver = (dec_container->Hdrs.f_code[0][1]);

  /* backward */
  dec_container->Hdrs.f_code_bwd_hor = (dec_container->Hdrs.f_code[1][0]);
  dec_container->Hdrs.f_code_bwd_ver = (dec_container->Hdrs.f_code[1][1]);

  tmp = dec_container->Hdrs.intra_dc_precision =
          mpeg2_strm_dec_get_bits(dec_container, 2);
  tmp = dec_container->Hdrs.picture_structure =
          mpeg2_strm_dec_get_bits(dec_container, 2);
  if (tmp == 0) /* reserved value -> assume equal to prev */
    dec_container->Hdrs.picture_structure =
      dec_container->StrmStorage.prev_pic_structure ?
      dec_container->StrmStorage.prev_pic_structure : FRAMEPICTURE;

  if(dec_container->Hdrs.picture_structure == TOPFIELD) {
    MPEG2DEC_DEBUG(("PICTURE STURCTURE: TOPFIELD\n"));
    dec_container->FrameDesc.field_coding_type[0] =
      dec_container->FrameDesc.pic_coding_type;
  } else if(dec_container->Hdrs.picture_structure == BOTTOMFIELD) {
    MPEG2DEC_DEBUG(("PICTURE STURCTURE: BOTTOMFIELD\n"));
    dec_container->FrameDesc.field_coding_type[1] =
      dec_container->FrameDesc.pic_coding_type;
  } else {
    MPEG2DEC_DEBUG(("PICTURE STURCTURE: FRAMEPICTURE\n"));
    dec_container->FrameDesc.field_coding_type[0] =
      dec_container->FrameDesc.field_coding_type[1] =
        dec_container->FrameDesc.pic_coding_type;
  }

  tmp = dec_container->Hdrs.top_field_first =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.frame_pred_frame_dct =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  if (dec_container->Hdrs.picture_structure < FRAMEPICTURE)
    dec_container->Hdrs.frame_pred_frame_dct = 0;
  tmp = dec_container->Hdrs.concealment_motion_vectors =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.quant_type =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.intra_vlc_format =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.alternate_scan =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.repeat_first_field =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.chroma420_type =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  tmp = dec_container->Hdrs.progressive_frame =
          mpeg2_strm_dec_get_bits(dec_container, 1);

#ifdef _MPEG2_8170_UNIT_TEST
  if(dec_container->Hdrs.picture_structure == TOPFIELD)
    printf("PICTURE STURCTURE: TOPFIELD\n");
  else if(dec_container->Hdrs.picture_structure == BOTTOMFIELD)
    printf("PICTURE STURCTURE: BOTTOMFIELD\n");
  else
    printf("PICTURE STURCTURE: FRAMEPICTURE\n");
#endif

  /* check if interlaced content */
  if(dec_container->Hdrs.progressive_sequence == 0 /*&&
                                                     * dec_container->Hdrs.progressive_frame == 0 */ )
    dec_container->Hdrs.interlaced = 1;
  else
    dec_container->Hdrs.interlaced = 0;

#if 0
#ifdef ASIC_TRACE_SUPPORT
  if(dec_container->Hdrs.progressive_sequence == 0 &&
      dec_container->Hdrs.progressive_frame == 0) {
    trace_mpeg2_dec_tools.sequence_type.interlaced = 1;
  }

  else {
    trace_mpeg2_dec_tools.sequence_type.progressive = 1;
  }
#endif
#endif

  tmp = dec_container->Hdrs.composite_display_flag =
          mpeg2_strm_dec_get_bits(dec_container, 1);
  if(tmp == END_OF_STREAM)
    return (END_OF_STREAM);

  if(dec_container->Hdrs.composite_display_flag) {
    tmp = dec_container->Hdrs.v_axis =
            mpeg2_strm_dec_get_bits(dec_container, 1);
    tmp = dec_container->Hdrs.field_sequence =
            mpeg2_strm_dec_get_bits(dec_container, 3);
    tmp = dec_container->Hdrs.sub_carrier =
            mpeg2_strm_dec_get_bits(dec_container, 1);
    tmp = dec_container->Hdrs.burst_amplitude =
            mpeg2_strm_dec_get_bits(dec_container, 7);
    tmp = dec_container->Hdrs.sub_carrier_phase =
            mpeg2_strm_dec_get_bits(dec_container, 8);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
  }

  MPEG2DEC_DEBUG(("Decode Picture Coding Extension Header Done\n"));

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:
                mpeg2_strm_dec_decode_pic_display_ext_header

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 mpeg2_strm_dec_decode_pic_display_ext_header(DecContainer * dec_container) {
  u32 i;
  u32 tmp;
  u32 frame_center_offsets = 0;

  ASSERT(dec_container);

  MPEG2DEC_DEBUG(("Decode Picture Display Extension Header Start\n"));

  /* calculate number of frameCenterOffset's */
  if(dec_container->Hdrs.progressive_sequence) {
    if(!dec_container->Hdrs.repeat_first_field) {
      frame_center_offsets = 1;
    } else {
      if(dec_container->Hdrs.top_field_first) {
        frame_center_offsets = 3;
      } else {
        frame_center_offsets = 2;
      }
    }
  } else {
    if(dec_container->Hdrs.picture_structure == FRAMEPICTURE) {
      if(dec_container->Hdrs.repeat_first_field) {
        frame_center_offsets = 3;
      } else {
        frame_center_offsets = 2;
      }
    } else {
      frame_center_offsets = 1;
    }
  }

  /* todo */
  dec_container->Hdrs.repeat_frame_count = frame_center_offsets;

  /* Read parameters from stream */
  for(i = 0; i < frame_center_offsets; i++) {
    tmp = dec_container->Hdrs.frame_centre_hor_offset[i] =
            mpeg2_strm_dec_get_bits(dec_container, 16);
    /* marker bit ==> flush */
    tmp = mpeg2_strm_dec_flush_bits(dec_container, 1);
    tmp = dec_container->Hdrs.frame_centre_ver_offset[i] =
            mpeg2_strm_dec_get_bits(dec_container, 16);
    /* marker bit ==> flush */
    tmp = mpeg2_strm_dec_flush_bits(dec_container, 1);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
  }

  MPEG2DEC_DEBUG(("Decode Picture Display Extension Header DONE\n"));

  return (HANTRO_OK);
}
