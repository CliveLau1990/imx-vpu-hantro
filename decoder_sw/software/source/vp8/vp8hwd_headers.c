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
#include "vp8hwd_headers.h"
#include "vp8hwd_probs.h"

#include "dwl.h"

#include "vp8hwd_debug.h"

#if 0
#define STREAM_TRACE(x,y) printf("%-24s-%9d\n", x, y);
#define VP8DEC_DEBUG(x) printf x
#else
#define STREAM_TRACE(x,y)
#define VP8DEC_DEBUG(x)
#endif

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/* Define here if it's necessary to print errors */
#define DEC_HDRS_ERR(x)

#define VP8_KEY_FRAME_START_CODE    0x9d012a

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

/*static void SetupVersion( vp8_decoder_t *dec );*/
static u32 ScaleDimension( u32 orig, u32 scale );

static u32 DecodeSegmentationData( vpBoolCoder_t *bc, vp8_decoder_t *dec );
static u32 DecodeMbLfAdjustments( vpBoolCoder_t *bc, vp8_decoder_t *dec );
static i32 DecodeQuantizerDelta(vpBoolCoder_t*bc );
static u32 ReadPartitionSize(const u8 *cx_size);

static u32 DecodeVp8FrameHeader( const u8 *p_strm, u32 strm_len, vpBoolCoder_t*bc,
                                 vp8_decoder_t* dec );

static u32 DecodeVp7FrameHeader( vpBoolCoder_t*bc, vp8_decoder_t* dec );


/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    SetupVersion

        Set internal flags according to version number in bitstream.
------------------------------------------------------------------------------*/
#if 0
void SetupVersion( vp8_decoder_t *dec ) {
}
#endif

/*------------------------------------------------------------------------------
    ScaleDimension

        Scale frame dimension
------------------------------------------------------------------------------*/
u32 ScaleDimension( u32 orig, u32 scale ) {
  switch(scale) {
  case 0:
    return orig;
    break;
  case 1: /* 5/4 */
    return (5*orig)/4;
    break;
  case 2: /* 5/3 */
    return (5*orig)/3;
    break;
  case 3: /* 2 */
    return 2*orig;
    break;
  }
  ASSERT(0);
  return 0;
}

/*------------------------------------------------------------------------------
    vp8hwdDecodeFrameTag

        Decode 3-byte frame tag. p_strm is assumed to contain at least 3
        bytes of valid data.
------------------------------------------------------------------------------*/
void vp8hwdDecodeFrameTag( const u8 *p_strm, vp8_decoder_t* dec ) {
  u32 key_frame = 0;
  u32 show_frame = 1;
  u32 version = 0;
  u32 part_size = 0;

  part_size = (p_strm[1] << 3) |
              (p_strm[2] << 11);
  key_frame = p_strm[0] & 0x1;
  version = (p_strm[0] >> 1) & 0x7;
  if(dec->dec_mode == VP8HWD_VP7) {
    part_size <<= 1;
    part_size = part_size | ((p_strm[0] >> 4) & 0xF);
    dec->frame_tag_size = version >= 1 ? 3 : 4;
  } else {
    show_frame = (p_strm[0] >> 4) & 0x1;
    part_size = part_size | ((p_strm[0] >> 5) & 0x7);
    dec->frame_tag_size = 3;
  }

  dec->show_frame          = show_frame;
  dec->vp_version          = version;
  dec->offset_to_dct_parts   = part_size;
  dec->key_frame           = !key_frame;

  VP8DEC_DEBUG(("#### FRAME TAG ####\n"));
  VP8DEC_DEBUG(("First partition size    = %d\n", part_size ));
  VP8DEC_DEBUG(("VP version              = %d\n", version ));
  VP8DEC_DEBUG(("Key frame ?             = %d\n", dec->key_frame ));
  VP8DEC_DEBUG(("Show frame ?            = %d\n", show_frame ));

}


/*------------------------------------------------------------------------------
    vp8hwdDecodeFrameHeader

        Decode frame header, either VP7 or VP8.
------------------------------------------------------------------------------*/
u32 vp8hwdDecodeFrameHeader( const u8 *p_strm, u32 strm_len, vpBoolCoder_t*bc,
                             vp8_decoder_t* dec ) {
  if(dec->dec_mode == VP8HWD_VP8) {
    return DecodeVp8FrameHeader( p_strm, strm_len, bc, dec );
  } else {
    /* Start bool coder for legacy VP7 header here already. */
    vp8hwdBoolStart(bc, p_strm, strm_len);
    return DecodeVp7FrameHeader( bc, dec );
  }
}

/*------------------------------------------------------------------------------
    DecodeVp8FrameHeader

        Decode VP8 frame header.
------------------------------------------------------------------------------*/
u32 DecodeVp8FrameHeader( const u8 *p_strm, u32 strm_len, vpBoolCoder_t*bc,
                          vp8_decoder_t* dec ) {
  u32 tmp;
  u32 i;

  if(dec->key_frame) {
    /* Check stream length */
    if( strm_len >= 7 ) {
      /* Read our "start code" */
      tmp = (p_strm[0] << 16)|
            (p_strm[1] << 8 )|
            (p_strm[2] << 0 );
    } else { /* Too few bytes in buffer */
      tmp = ~VP8_KEY_FRAME_START_CODE;
    }

    if( tmp != VP8_KEY_FRAME_START_CODE ) {
      DEC_HDRS_ERR("VP8 Key-frame start code missing or invalid!\n");
      return(HANTRO_NOK);
    }

    p_strm += 3; /* Skip used bytes */

    tmp = (p_strm[1] << 8)|(p_strm[0]); /* Read 16-bit chunk */
    /* Frame width */
    dec->width = tmp & 0x3fff;
    STREAM_TRACE("frame_width", dec->width );

    /* Scaled width */
    tmp >>= 14;
    dec->scaled_width = ScaleDimension( dec->width, tmp );
    STREAM_TRACE("scaled_width", dec->scaled_width );

    p_strm += 2; /* Skip used bytes */

    tmp = (p_strm[1] << 8)|(p_strm[0]); /* Read 16-bit chunk */
    /* Frame height */
    dec->height = tmp & 0x3fff;
    STREAM_TRACE("frame_height", dec->height );

    /* Scaled height */
    tmp >>= 14;
    dec->scaled_height = ScaleDimension( dec->height, tmp );
    STREAM_TRACE("scaled_height", dec->scaled_height );

    p_strm += 2; /* Skip used bytes */
    strm_len -= 7;

    if(dec->width == 0 ||
        dec->height == 0 ) {
      DEC_HDRS_ERR("Invalid size!\n");
      return(HANTRO_NOK);
    }

  }

  /* Start bool code, note that we skip first 3/4 bytes used by the
   * frame tag */
  vp8hwdBoolStart(bc, p_strm, strm_len );

  if(dec->key_frame) {
    /* Color space type */
    dec->color_space = (vpColorSpace_e)vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("vp8_color_space", dec->color_space );

    /* Clamping type */
    dec->clamping = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("vp8_clamping", dec->clamping);
  }

  /* Segment based adjustments */
  tmp = DecodeSegmentationData( bc, dec );
  if( tmp != HANTRO_OK )
    return tmp;

  /* Loop filter adjustments */
  dec->loop_filter_type      = vp8hwdReadBits( bc, 1 );
  dec->loop_filter_level     = vp8hwdReadBits( bc, 6 );
  dec->loop_filter_sharpness = vp8hwdReadBits( bc, 3 );
  STREAM_TRACE("loop_filter_type", dec->loop_filter_type);
  STREAM_TRACE("loop_filter_level", dec->loop_filter_level);
  STREAM_TRACE("loop_filter_sharpness", dec->loop_filter_sharpness);

  tmp = DecodeMbLfAdjustments( bc, dec );
  if( tmp != HANTRO_OK )
    return tmp;

  /* Number of DCT partitions */
  tmp = vp8hwdReadBits( bc, 2 );
  STREAM_TRACE("nbr_of_dct_partitions", tmp);
  dec->nbr_dct_partitions = 1<<tmp;

  /* Quantizers */
  dec->qp_yac = vp8hwdReadBits( bc, 7 );
  STREAM_TRACE("qp_y_ac", dec->qp_yac);
  dec->qp_ydc = DecodeQuantizerDelta( bc );
  dec->qp_y2_dc = DecodeQuantizerDelta( bc );
  dec->qp_y2_ac = DecodeQuantizerDelta( bc );
  dec->qp_ch_dc = DecodeQuantizerDelta( bc );
  dec->qp_ch_ac = DecodeQuantizerDelta( bc );

  /* Frame buffer operations */
  if( !dec->key_frame ) {
    /* Refresh golden */
    dec->refresh_golden = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("refresh_golden", dec->refresh_golden);

    /* Refresh alternate */
    dec->refresh_alternate = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("refresh_alternate", dec->refresh_alternate);

    if( dec->refresh_golden == 0 ) {
      /* Copy to golden */
      dec->copy_buffer_to_golden = vp8hwdReadBits( bc, 2 );
      STREAM_TRACE("copy_buffer_to_golden", dec->copy_buffer_to_golden);
    } else
      dec->copy_buffer_to_golden = 0;

    if( dec->refresh_alternate == 0 ) {
      /* Copy to alternate */
      dec->copy_buffer_to_alternate = vp8hwdReadBits( bc, 2 );
      STREAM_TRACE("copy_buffer_to_alternate", dec->copy_buffer_to_alternate);
    } else
      dec->copy_buffer_to_alternate = 0;

    /* Sign bias for golden frame */
    dec->ref_frame_sign_bias[0] = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("sign_bias_golden", dec->ref_frame_sign_bias[0]);

    /* Sign bias for alternate frame */
    dec->ref_frame_sign_bias[1] = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("sign_bias_alternate", dec->ref_frame_sign_bias[1]);

    /* Refresh entropy probs */
    dec->refresh_entropy_probs = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("refresh_entropy_probs", dec->refresh_entropy_probs);

    /* Refresh last */
    dec->refresh_last = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("refresh_last", dec->refresh_last);
  } else { /* Key frame */
    dec->refresh_golden          = HANTRO_TRUE;
    dec->refresh_alternate       = HANTRO_TRUE;
    dec->copy_buffer_to_golden     = 0;
    dec->copy_buffer_to_alternate  = 0;

    /* Refresh entropy probs */
    dec->refresh_entropy_probs = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("refresh_entropy_probs", dec->refresh_entropy_probs);

    dec->ref_frame_sign_bias[0] =
      dec->ref_frame_sign_bias[1] = 0;
    dec->refresh_last = HANTRO_TRUE;
  }

  /* Make a "backup" of current entropy probabilities if refresh is
   * not set */
  if(dec->refresh_entropy_probs == HANTRO_FALSE) {
    DWLmemcpy( &dec->entropy_last, &dec->entropy,
               sizeof(vp8EntropyProbs_t));
    DWLmemcpy( dec->vp7_prev_scan_order, dec->vp7_scan_order,
               sizeof(dec->vp7_scan_order));
  }

  /* Coefficient probability update */
  tmp = vp8hwdDecodeCoeffUpdate(bc, dec);
  if( tmp != HANTRO_OK )
    return (tmp);

  dec->probs_decoded = 1;

  /* Coeff skip element used */
  tmp = vp8hwdReadBits( bc, 1 );
  STREAM_TRACE("no_coeff_skip", tmp);
  dec->coeff_skip_mode = tmp;

  if( !dec->key_frame ) {
    /* Skipped MB probability */
    if(dec->coeff_skip_mode) {
      tmp = vp8hwdReadBits( bc, 8 );
      STREAM_TRACE("prob_skip_mb", tmp);
      dec->prob_mb_skip_false = tmp;
    }

    /* Intra MB probability */
    tmp = vp8hwdReadBits( bc, 8 );
    STREAM_TRACE("prob_intra_mb", tmp);
    dec->prob_intra = tmp;

    /* Last ref frame probability */
    tmp = vp8hwdReadBits( bc, 8 );
    STREAM_TRACE("prob_ref_frame_0", tmp);
    dec->prob_ref_last = tmp;

    /* Golden ref frame probability */
    tmp = vp8hwdReadBits( bc, 8 );
    STREAM_TRACE("prob_ref_frame_1", tmp);
    dec->prob_ref_golden = tmp;

    /* Intra 16x16 pred mode probabilities */
    tmp = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("intra_16x16_prob_update_flag", tmp);
    if( tmp ) {
      for( i = 0 ; i < 4 ; ++i ) {
        tmp = vp8hwdReadBits( bc, 8 );
        STREAM_TRACE("intra_16x16_prob", tmp);
        dec->entropy.prob_luma16x16_pred_mode[i] = tmp;
      }
    }

    /* Chroma pred mode probabilities */
    tmp = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("chroma_prob_update_flag", tmp);
    if( tmp ) {
      for( i = 0 ; i < 3 ; ++i ) {
        tmp = vp8hwdReadBits( bc, 8 );
        STREAM_TRACE("chroma_prob", tmp);
        dec->entropy.prob_chroma_pred_mode[i] = tmp;
      }
    }

    /* Motion vector tree update */
    tmp = vp8hwdDecodeMvUpdate( bc, dec );
    if( tmp != HANTRO_OK )
      return (tmp);
  } else {
    /* Skipped MB probability */
    if(dec->coeff_skip_mode) {
      tmp = vp8hwdReadBits( bc, 8 );
      STREAM_TRACE("prob_skip_mb", tmp);
      dec->prob_mb_skip_false = tmp;
    }
  }

  if(bc->strm_error)
    return (HANTRO_NOK);

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------
    DecodeVp7FrameHeader

        Decode VP7 frame header.
------------------------------------------------------------------------------*/
u32 DecodeVp7FrameHeader( vpBoolCoder_t*bc, vp8_decoder_t* dec ) {
  u32 tmp;
  u32 i, j;

  if(dec->key_frame) {
    /* Frame width */
    dec->width = vp8hwdReadBits( bc, 12 );
    STREAM_TRACE("frame_width", dec->width );

    /* Frame height */
    dec->height = vp8hwdReadBits( bc, 12 );
    STREAM_TRACE("frame_height", dec->height );

    /* Scaled width */
    tmp = vp8hwdReadBits( bc, 2 );
    STREAM_TRACE("scaled_width", tmp );
    dec->scaled_width = ScaleDimension( dec->width, tmp );

    /* Scaled height */
    tmp = vp8hwdReadBits( bc, 2 );
    STREAM_TRACE("scaled_height", tmp );
    dec->scaled_height = ScaleDimension( dec->height, tmp );
  }

  /* Feature bits */
  {
    const u32 vp70_feature_bits[4] = { 7, 6, 0, 8 };
    const u32 vp71_feature_bits[4] = { 7, 6, 0, 5 };
    const u32 *feature_bits;
    if( dec->vp_version == 0 )   feature_bits = vp70_feature_bits;
    else                        feature_bits = vp71_feature_bits;
    for( i = 0 ; i < MAX_NBR_OF_VP7_MB_FEATURES ; ++i ) {
      /* Feature enabled? */
      if( vp8hwdReadBits( bc, 1 ) ) {
        /* MB-level probability flag */
        tmp = vp8hwdReadBits( bc, 8 );

        /* Feature tree probabilities */
        for( j = 0 ; j < 3 ; ++j ) {
          if( vp8hwdReadBits( bc, 1 ) )
            tmp = vp8hwdReadBits( bc, 8 );
        }

        if(feature_bits[i]) {
          for( j = 0 ; j < 4 ; ++j ) {
            if( vp8hwdReadBits( bc, 1 ) )
              tmp = vp8hwdReadBits( bc, feature_bits[i] );
          }
        }

        DEC_HDRS_ERR("VP7 MB-level features enabled!\n");
        return(HANTRO_NOK);
      }
    }

    dec->nbr_dct_partitions = 1;
  }

  /* Quantizers */
  dec->qp_yac = vp8hwdReadBits( bc, 7 );
  STREAM_TRACE("qp_y_ac", dec->qp_yac);
  dec->qp_ydc  = vp8hwdReadBits( bc, 1 ) ? (i32)vp8hwdReadBits( bc, 7 ) : dec->qp_yac;
  dec->qp_y2_dc = vp8hwdReadBits( bc, 1 ) ? (i32)vp8hwdReadBits( bc, 7 ) : dec->qp_yac;
  dec->qp_y2_ac = vp8hwdReadBits( bc, 1 ) ? (i32)vp8hwdReadBits( bc, 7 ) : dec->qp_yac;
  dec->qp_ch_dc = vp8hwdReadBits( bc, 1 ) ? (i32)vp8hwdReadBits( bc, 7 ) : dec->qp_yac;
  dec->qp_ch_ac = vp8hwdReadBits( bc, 1 ) ? (i32)vp8hwdReadBits( bc, 7 ) : dec->qp_yac;
  STREAM_TRACE("qp_y_dc", dec->qp_ydc);
  STREAM_TRACE("qp_y2_dc", dec->qp_y2_dc);
  STREAM_TRACE("qp_y2_ac", dec->qp_y2_ac);
  STREAM_TRACE("qp_ch_dc", dec->qp_ch_dc);
  STREAM_TRACE("qp_ch_ac", dec->qp_ch_ac);

  /* Frame buffer operations */
  if( !dec->key_frame ) {
    /* Refresh golden */
    dec->refresh_golden = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("refresh_golden", dec->refresh_golden);

    if( dec->vp_version >= 1) {
      /* Refresh entropy probs */
      dec->refresh_entropy_probs = vp8hwdReadBits( bc, 1 );
      STREAM_TRACE("refresh_entropy_probs", dec->refresh_entropy_probs);

      /* Refresh last */
      dec->refresh_last = vp8hwdReadBits( bc, 1 );
      STREAM_TRACE("refresh_last", dec->refresh_last);
    } else {
      dec->refresh_entropy_probs = HANTRO_TRUE;
      dec->refresh_last = HANTRO_TRUE;
    }
  } else { /* Key frame */
    dec->refresh_golden          = HANTRO_TRUE;
    dec->refresh_alternate       = HANTRO_TRUE;
    dec->copy_buffer_to_golden     = 0;
    dec->copy_buffer_to_alternate  = 0;

    /* Refresh entropy probs */
    if( dec->vp_version >= 1 ) {
      dec->refresh_entropy_probs = vp8hwdReadBits( bc, 1 );
      STREAM_TRACE("refresh_entropy_probs", dec->refresh_entropy_probs);
    } else {
      dec->refresh_entropy_probs = HANTRO_TRUE;
    }

    dec->ref_frame_sign_bias[0] =
      dec->ref_frame_sign_bias[1] = 0;
    dec->refresh_last = HANTRO_TRUE;
  }

  /* Make a "backup" of current entropy probabilities if refresh is
   * not set */
  if(dec->refresh_entropy_probs == HANTRO_FALSE) {
    DWLmemcpy( &dec->entropy_last, &dec->entropy,
               sizeof(vp8EntropyProbs_t));
    DWLmemcpy( dec->vp7_prev_scan_order, dec->vp7_scan_order,
               sizeof(dec->vp7_scan_order));
  }

  /* Faded reference frame (NOT IMPLEMENTED) */
  if(dec->refresh_last != 0) {
    tmp = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("use_faded_reference", tmp);
    if( tmp == 1 ) {
      /* Alpha */
      tmp = vp8hwdReadBits( bc, 8 );
      /* Beta */
      tmp = vp8hwdReadBits( bc, 8 );
      DEC_HDRS_ERR("Faded reference used!\n");
      return(HANTRO_NOK);
    }
  }
  /* Loop filter type (for VP7 version 0) */
  if(dec->vp_version == 0) {
    dec->loop_filter_type = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("loop_filter_type", dec->loop_filter_type);
  }

  /* Scan order update? */
  tmp = vp8hwdReadBits( bc, 1 );
  STREAM_TRACE("vp7_scan_order_update_flag", tmp);
  if(tmp) {
    u32 new_order[16] = { 0 };
    for( i = 1 ; i < 16 ; ++i ) {
      tmp = vp8hwdReadBits( bc, 4 );
      STREAM_TRACE("scan_index", tmp);
      new_order[i] = tmp;
    }
    vp8hwdPrepareVp7Scan( dec, new_order );
  }

  /* Loop filter type (for VP7 version 1 and later) */
  if(dec->vp_version >= 1) {
    dec->loop_filter_type = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("loop_filter_type", dec->loop_filter_type);
  }

  /* Loop filter adjustments */
  dec->loop_filter_level     = vp8hwdReadBits( bc, 6 );
  dec->loop_filter_sharpness = vp8hwdReadBits( bc, 3 );
  STREAM_TRACE("loop_filter_level", dec->loop_filter_level);
  STREAM_TRACE("loop_filter_sharpness", dec->loop_filter_sharpness);

  /* Coefficient probability update */
  tmp = vp8hwdDecodeCoeffUpdate(bc, dec);
  if( tmp != HANTRO_OK )
    return (tmp);

  if( !dec->key_frame ) {
    /* Intra MB probability */
    tmp = vp8hwdReadBits( bc, 8 );
    STREAM_TRACE("prob_intra_mb", tmp);
    dec->prob_intra = tmp;

    /* Last ref frame probability */
    tmp = vp8hwdReadBits( bc, 8 );
    STREAM_TRACE("prob_ref_frame_0", tmp);
    dec->prob_ref_last = tmp;

    /* Intra 16x16 pred mode probabilities */
    tmp = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("intra_16x16_prob_update_flag", tmp);
    if( tmp ) {
      for( i = 0 ; i < 4 ; ++i ) {
        tmp = vp8hwdReadBits( bc, 8 );
        STREAM_TRACE("intra_16x16_prob", tmp);
        dec->entropy.prob_luma16x16_pred_mode[i] = tmp;
      }
    }

    /* Chroma pred mode probabilities */
    tmp = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("chroma_prob_update_flag", tmp);
    if( tmp ) {
      for( i = 0 ; i < 3 ; ++i ) {
        tmp = vp8hwdReadBits( bc, 8 );
        STREAM_TRACE("chroma_prob", tmp);
        dec->entropy.prob_chroma_pred_mode[i] = tmp;
      }
    }

    /* Motion vector tree update */
    tmp = vp8hwdDecodeMvUpdate( bc, dec );
    if( tmp != HANTRO_OK )
      return (tmp);
  }

  if(bc->strm_error)
    return (HANTRO_NOK);

  return (HANTRO_OK);
}


/*------------------------------------------------------------------------------
    DecodeQuantizerDelta

        Decode VP8 delta-coded quantizer value
------------------------------------------------------------------------------*/
i32 DecodeQuantizerDelta(vpBoolCoder_t*bc ) {
  u32 sign;
  i32 delta;

  if( vp8hwdReadBits( bc, 1 ) ) {
    delta = vp8hwdReadBits(bc, 4);
    sign = vp8hwdReadBits(bc, 1);
    if( sign )
      delta = -delta;
    return delta;
  } else {
    return 0;
  }
}

/*------------------------------------------------------------------------------
    vp8hwdSetPartitionOffsets

        Read partition offsets from stream and initialize into internal
        structures.
------------------------------------------------------------------------------*/
u32 vp8hwdSetPartitionOffsets( const u8 *stream, u32 len, vp8_decoder_t *dec ) {
  u32 i = 0;
  u32 offset = 0;
  u32 base_offset;
  u32 extra_bytes_packed = 0;
  u32 ret_val = HANTRO_OK;

  if(dec->dec_mode == VP8HWD_VP8 &&
      dec->key_frame)
    extra_bytes_packed += 7;

  stream += dec->frame_tag_size;

  base_offset = dec->frame_tag_size + dec->offset_to_dct_parts +
                3*(dec->nbr_dct_partitions-1);

  stream += dec->offset_to_dct_parts + extra_bytes_packed;
  for( i = 0 ; i < dec->nbr_dct_partitions - 1 ; ++i ) {
    dec->dct_partition_offsets[i] = base_offset + offset;
    if (dec->dct_partition_offsets[i] < len) {
      offset += ReadPartitionSize( stream );
    } else {
      dec->dct_partition_offsets[i] = len - 1;
    }
    stream += 3;
  }
  dec->dct_partition_offsets[i] = base_offset + offset;
  if (dec->dct_partition_offsets[i] >= len) {
    dec->dct_partition_offsets[i] = len - 1;
    ret_val = HANTRO_NOK;
  }

  return ret_val;

}


/*------------------------------------------------------------------------------
    ReadPartitionSize

        Read partition size from stream.
------------------------------------------------------------------------------*/
u32 ReadPartitionSize(const u8 *cx_size) {
  u32 size;
  size =  (u32)(*cx_size)
          +  ((u32)(* (cx_size+1))<<8)
          +  ((u32)(* (cx_size+2))<<16);
  return size;
}


/*------------------------------------------------------------------------------
    DecodeSegmentationData

        Decode segment-based adjustments from bitstream.
------------------------------------------------------------------------------*/
u32 DecodeSegmentationData( vpBoolCoder_t *bc, vp8_decoder_t *dec ) {
  u32 tmp;
  u32 sign;
  u32 j;

  /* Segmentation enabled? */
  dec->segmentation_enabled = vp8hwdReadBits( bc, 1 );
  STREAM_TRACE("segmentation_enabled", dec->segmentation_enabled);
  if( dec->segmentation_enabled ) {
    /* Segmentation map update */
    dec->segmentation_map_update = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("segmentation_map_update", dec->segmentation_map_update);
    /* Segment feature data update */
    tmp = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("segment_feature_data_update", tmp);
    if( tmp ) {
      /* Absolute/relative mode */
      dec->segment_feature_mode = vp8hwdReadBits( bc, 1 );
      STREAM_TRACE("segment_feature_mode", dec->segment_feature_mode);

      /* TODO: what to do with negative numbers if absolute mode? */
      /* Quantiser */
      for( j = 0 ; j < MAX_NBR_OF_SEGMENTS ; ++j ) {
        /* Feature data update ? */
        tmp = vp8hwdReadBits( bc, 1 );
        STREAM_TRACE("quantizer_update_flag", tmp);
        if( tmp ) {
          /* Payload */
          tmp = vp8hwdReadBits( bc, 7 );
          /* Sign */
          sign = vp8hwdReadBits( bc, 1 );
          STREAM_TRACE("quantizer_payload", tmp);
          STREAM_TRACE("quantizer_sign", sign);
          dec->segment_qp[j] = tmp;
          if( sign )
            dec->segment_qp[j] = -dec->segment_qp[j];
        } else {
          dec->segment_qp[j] = 0;
        }
      }

      /* Loop filter level */
      for( j = 0 ; j < MAX_NBR_OF_SEGMENTS ; ++j ) {
        /* Feature data update ? */
        tmp = vp8hwdReadBits( bc, 1 );
        STREAM_TRACE("loop_filter_update_flag", tmp);
        if( tmp ) {
          /* Payload */
          tmp = vp8hwdReadBits( bc, 6 );
          /* Sign */
          sign = vp8hwdReadBits( bc, 1 );
          STREAM_TRACE("loop_filter_payload", tmp);
          STREAM_TRACE("loop_filter_sign", sign);
          dec->segment_loopfilter[j] = tmp;
          if( sign )
            dec->segment_loopfilter[j] = -dec->segment_loopfilter[j];
        } else {
          dec->segment_loopfilter[j] = 0;
        }
      }

    }

    /* Segment probabilities */
    if(dec->segmentation_map_update) {
      dec->prob_segment[0] =
        dec->prob_segment[1] =
          dec->prob_segment[2] = 255;
      for( j = 0 ; j < 3 ; ++j ) {
        tmp = vp8hwdReadBits( bc, 1 );
        STREAM_TRACE("segment_prob_update_flag", tmp);
        if( tmp ) {
          tmp = vp8hwdReadBits( bc, 8 );
          STREAM_TRACE("segment_prob", tmp);
          dec->prob_segment[j] = tmp;
        }
      }
    }
  } /* SegmentationEnabled */
  else {
    dec->segmentation_map_update = 0;
  }

  if(bc->strm_error)
    return (HANTRO_NOK);

  return (HANTRO_OK);
}


/*------------------------------------------------------------------------------
    DecodeMbLfAdjustments

        Decode MB loop filter adjustments from bitstream.
------------------------------------------------------------------------------*/
u32 DecodeMbLfAdjustments( vpBoolCoder_t*bc, vp8_decoder_t* dec ) {
  u32 sign;
  u32 tmp;
  u32 j;

  /* Adjustments enabled? */
  dec->mode_ref_lf_enabled = vp8hwdReadBits( bc, 1 );
  STREAM_TRACE("loop_filter_adj_enable", dec->mode_ref_lf_enabled);

  if( dec->mode_ref_lf_enabled ) {
    /* Mode update? */
    tmp = vp8hwdReadBits( bc, 1 );
    STREAM_TRACE("loop_filter_adj_update_flag", tmp);
    if( tmp ) {
      /* Reference frame deltas */
      for ( j = 0; j < MAX_NBR_OF_MB_REF_LF_DELTAS; j++ ) {
        tmp = vp8hwdReadBits( bc, 1 );
        STREAM_TRACE("ref_frame_delta_update_flag", tmp);
        if( tmp ) {
          /* Payload */
          tmp = vp8hwdReadBits( bc, 6 );
          /* Sign */
          sign = vp8hwdReadBits( bc, 1 );
          STREAM_TRACE("loop_filter_payload", tmp);
          STREAM_TRACE("loop_filter_sign", sign);

          dec->mb_ref_lf_delta[j] = tmp;
          if( sign )
            dec->mb_ref_lf_delta[j] = -dec->mb_ref_lf_delta[j];
        }
      }

      /* Mode deltas */
      for ( j = 0; j < MAX_NBR_OF_MB_MODE_LF_DELTAS; j++ ) {
        tmp = vp8hwdReadBits( bc, 1 );
        STREAM_TRACE("mb_type_delta_update_flag", tmp);
        if( tmp ) {
          /* Payload */
          tmp = vp8hwdReadBits( bc, 6 );
          /* Sign */
          sign = vp8hwdReadBits( bc, 1 );
          STREAM_TRACE("loop_filter_payload", tmp);
          STREAM_TRACE("loop_filter_sign", sign);

          dec->mb_mode_lf_delta[j] = tmp;
          if( sign )
            dec->mb_mode_lf_delta[j] = -dec->mb_mode_lf_delta[j];
        }
      }
    }
  } /* Mb mode/ref lf adjustment */

  if(bc->strm_error)
    return (HANTRO_NOK);

  return (HANTRO_OK);
}
