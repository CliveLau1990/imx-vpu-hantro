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

#include "vc1hwd_headers.h"
#include "vc1hwd_picture_layer.h"
#include "vc1hwd_vlc.h"
#include "vc1hwd_bitplane.h"
#include "vc1hwd_util.h"
#include "vc1hwd_storage.h"
#include "vc1hwd_decoder.h"
#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

/*------------------------------------------------------------------------------
    External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function name: vc1hwdGetStartCode

        Functional description:
            Function to seek start codes from the stream

        Inputs:
            p_strm_data   pointer to stream data structure

        Outputs:

        Returns:
            startcode

------------------------------------------------------------------------------*/
u32 vc1hwdGetStartCode(strmData_t *p_strm_data) {
  u32 tmp;
  u32 sc = 0;
  u8 * p_strm;

  /* Check that we are in bytealigned position */
  tmp = p_strm_data->bit_pos_in_word;
  if (tmp) {
    if (vc1hwdFlushBits(p_strm_data, 8-tmp) != HANTRO_OK)
      return SC_NOT_FOUND;
  }
  /* Check that we have enough stream data */
  if (((p_strm_data->strm_buff_read_bits>>3) + 4) > p_strm_data->strm_buff_size) {
    return SC_NOT_FOUND;
  }

  p_strm = p_strm_data->strm_curr_pos;

  sc = ( ((u32)p_strm[0]) << 16 ) +
       ( ((u32)p_strm[1]) << 8  ) +
       ( ((u32)p_strm[2]) );

  if ( sc != 0x000001 ) {
    sc = SC_NOT_FOUND;
  } else {
    sc = (sc << 8 ) + p_strm[3];
    p_strm_data->strm_curr_pos += 4;
    p_strm_data->strm_buff_read_bits += 32;
  }

  return sc;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdGetUserData

        Functional description:
            Flushes user data bytes

        Inputs:
            storage    Stream storage descriptor
            p_strm_data   Pointer to input stream data structure

        Outputs:

        Returns:
            VC1HWD_OK

------------------------------------------------------------------------------*/
u32 vc1hwdGetUserData( swStrmStorage_t *storage,
                       strmData_t *p_strm_data ) {
  /* Variables */
  u32 sc;
  u8 *p_strm;

  /* Code */
  UNUSED(storage);
  do {
    p_strm = p_strm_data->strm_curr_pos;

    /* Check that we have enough stream data */
    if (((p_strm_data->strm_buff_read_bits>>3)+3) <= p_strm_data->strm_buff_size) {
      sc = ( ((u32)p_strm[0]) << 16 ) +
           ( ((u32)p_strm[1]) << 8  ) +
           ( ((u32)p_strm[2]) );
    } else
      sc = 0;

    /* start code prefix found */
    if ( sc == 0x000001 ) {
      break;
    }
  } while (vc1hwdFlushBits(p_strm_data, 8) == HANTRO_OK);

  if (vc1hwdIsExhausted(p_strm_data))
    return END_OF_STREAM;
  else
    return HANTRO_OK;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdDecodeSequenceLayer

        Functional description:
            Decodes sequence layer of video bit stream

        Inputs:
            storage    Stream storage descriptor
            p_strm_data   Pointer to input stream data structure

        Outputs:

        Returns:
            VC1HWD_SEQ_HDRS_RDY
            VC1HWD_METADATA_ERROR
            VC1HWD_MEMORY_FAIL

------------------------------------------------------------------------------*/

u32 vc1hwdDecodeSequenceLayer( swStrmStorage_t *storage,
                               strmData_t *p_strm_data ) {
  i32 tmp;
  u32 w, h, i;
  DWLHwConfig config;

  ASSERT(storage);
  ASSERT(p_strm_data);

  (void)DWLmemset(&config, 0, sizeof(DWLHwConfig));
  DWLReadAsicConfig(&config,DWL_CLIENT_TYPE_VC1_DEC);

  /* reset flag indicating already decoded sequence layer header */
  storage->hdrs_decoded &= ~HDR_SEQ;

  /* PROFILE */
  tmp = vc1hwdGetBits(p_strm_data, 2);
#ifdef HANTRO_PEDANTIC_MODE
  if(tmp != 3)
    return HANTRO_NOK;
#endif
  /* LEVEL */
  storage->level = vc1hwdGetBits(p_strm_data, 3);
#ifdef HANTRO_PEDANTIC_MODE
  if(storage->level >= 5)
    return HANTRO_NOK;
#endif
  /* COLORDIFF_FORMAT */
  storage->color_diff_format = vc1hwdGetBits(p_strm_data, 2);
#ifdef HANTRO_PEDANTIC_MODE
  if(storage->color_diff_format != 1)
    return HANTRO_NOK;
#endif
  /* FRMRTQ_POSTPROC */
  storage->frmrtq_post_proc = vc1hwdGetBits(p_strm_data, 3);
  /* BITRTQ_POSTPROC */
  storage->bitrtq_post_proc = vc1hwdGetBits(p_strm_data, 5);
  /* POSTPROCFLAG */
  storage->post_proc_flag = vc1hwdGetBits(p_strm_data, 1);

  /* MAX_CODED_WIDTH */
  tmp = vc1hwdGetBits(p_strm_data, 12);
  storage->max_coded_width = 2*tmp + 2;
  /* MAX_CODED_HEIGHT */
  tmp = vc1hwdGetBits(p_strm_data, 12);
  storage->max_coded_height = 2*tmp + 2;

  /* check size information */
  if ( storage->max_coded_width < storage->min_coded_width||
       storage->max_coded_width > config.max_dec_pic_width ||
       storage->max_coded_height < storage->min_coded_height ||
       storage->max_coded_height > config.max_dec_pic_width ||
       (storage->max_coded_width & 0x1) ||
       (storage->max_coded_height & 0x1) )
    return (VC1HWD_METADATA_ERROR);

  storage->pic_width_in_mbs  = (storage->max_coded_width+15) >> 4;
  storage->pic_height_in_mbs = (storage->max_coded_height+15) >> 4;
  storage->num_of_mbs       = (storage->pic_width_in_mbs *
                               storage->pic_height_in_mbs);

  if (storage->num_of_mbs > MAX_NUM_MBS)
    return (VC1HWD_METADATA_ERROR);

  if ((storage->cur_coded_width &&
       storage->cur_coded_width != storage->max_coded_width) ||
      (storage->cur_coded_height &&
       storage->cur_coded_height != storage->max_coded_height))
    storage->resolution_changed = HANTRO_TRUE;

  storage->cur_coded_width = storage->max_coded_width;
  storage->cur_coded_height = storage->max_coded_height;

  /* PULLDOWN */
  storage->pull_down = vc1hwdGetBits(p_strm_data, 1);
  /* INTERLACE */
  storage->interlace = vc1hwdGetBits(p_strm_data, 1);

  /* height of field must be atleast 48 pixels */
  if (storage->interlace &&
      ((storage->max_coded_height>>1) < storage->min_coded_height))
    return VC1HWD_METADATA_ERROR;

  /* TFCNTRFLAG */
  storage->tfcntr_flag = vc1hwdGetBits(p_strm_data, 1);
  /* FINTERPFLAG */
  storage->finterp_flag = vc1hwdGetBits(p_strm_data, 1);
  /* RESERVED */
  tmp = vc1hwdGetBits(p_strm_data, 1);
#ifdef HANTRO_PEDANTIC_MODE
  if(tmp != 1)
    return HANTRO_NOK;
#endif
  /* PSF */
  storage->psf = vc1hwdGetBits(p_strm_data, 1);
  /* DISPLAY_EXT */
  tmp = vc1hwdGetBits(p_strm_data, 1);
  if (tmp) {
    /* DISP_HORIZ_SIZE */
    storage->disp_horiz_size = vc1hwdGetBits(p_strm_data, 14);
    /* DISP_VERT_SIZE */
    storage->disp_vert_size = vc1hwdGetBits(p_strm_data, 14);
    /* ASPECT_RATIO_FLAG */
    tmp = vc1hwdGetBits(p_strm_data, 1);
    if (tmp) {
      /* ASPECT_RATIO */
      tmp = vc1hwdGetBits(p_strm_data, 4);
      switch (tmp) {
      case ASPECT_RATIO_UNSPECIFIED:
        w =   0;
        h =  0;
        break;
      case ASPECT_RATIO_1_1:
        w =   1;
        h =  1;
        break;
      case ASPECT_RATIO_12_11:
        w =  12;
        h = 11;
        break;
      case ASPECT_RATIO_10_11:
        w =  10;
        h = 11;
        break;
      case ASPECT_RATIO_16_11:
        w =  16;
        h = 11;
        break;
      case ASPECT_RATIO_40_33:
        w =  40;
        h = 33;
        break;
      case ASPECT_RATIO_24_11:
        w =  24;
        h = 11;
        break;
      case ASPECT_RATIO_20_11:
        w =  20;
        h = 11;
        break;
      case ASPECT_RATIO_32_11:
        w =  32;
        h = 11;
        break;
      case ASPECT_RATIO_80_33:
        w =  80;
        h = 33;
        break;
      case ASPECT_RATIO_18_11:
        w =  18;
        h = 11;
        break;
      case ASPECT_RATIO_15_11:
        w =  15;
        h = 11;
        break;
      case ASPECT_RATIO_64_33:
        w =  64;
        h = 33;
        break;
      case ASPECT_RATIO_160_99:
        w = 160;
        h = 99;
        break;
      case ASPECT_RATIO_EXTENDED: /* 15 */
        w = vc1hwdGetBits(p_strm_data, 8);
        h = vc1hwdGetBits(p_strm_data, 8);
        if ((w == 0) || (h == 0))
          w = h = 0;
        break;
      default:
        w = 0;
        h = 0;
        break;
      }
      /* aspect ratio width */
      storage->aspect_horiz_size = w;
      /* aspect ratio height */
      storage->aspect_vert_size = h;
    }
    /* FRAMERATE_FLAG */
    storage->frame_rate_flag = vc1hwdGetBits(p_strm_data, 1);
    if (storage->frame_rate_flag) {
      /* FRAMERATEIND */
      storage->frame_rate_ind = vc1hwdGetBits(p_strm_data, 1);
      if (storage->frame_rate_ind == 0) {
        /* FRAMERATENR */
        tmp = vc1hwdGetBits(p_strm_data, 8);
        switch (tmp) {
        case 1:
          storage->frame_rate_nr = 24*1000;
          break;
        case 2:
          storage->frame_rate_nr = 25*1000;
          break;
        case 3:
          storage->frame_rate_nr = 30*1000;
          break;
        case 4:
          storage->frame_rate_nr = 50*1000;
          break;
        case 5:
          storage->frame_rate_nr = 60*1000;
          break;
        case 6:
          storage->frame_rate_nr = 48*1000;
          break;
        case 7:
          storage->frame_rate_nr = 72*1000;
          break;
        default:
          storage->frame_rate_nr = (u32)-1;
          break;
        }
        /* FRAMERATEDR */
        tmp = vc1hwdGetBits(p_strm_data, 4);
        switch (tmp) {
        case 1:
          storage->frame_rate_dr = 1000;
          break;
        case 2:
          storage->frame_rate_dr = 1001;
          break;
        default:
          storage->frame_rate_dr = (u32)-1;
          break;
        }
      } else {
        /* FRAMERATEEXP */
        tmp = vc1hwdGetBits(p_strm_data, 16);
        storage->frame_rate_nr = (tmp+1);
        storage->frame_rate_dr = 32000;
      }
    }
    /* COLOR_FORMAT_FLAG */
    storage->color_format_flag = vc1hwdGetBits(p_strm_data, 1);
    if (storage->color_format_flag) {
      /* COLOR_PRIM */
      storage->color_prim = vc1hwdGetBits(p_strm_data, 8);
      /* TRANSFER_CHAR */
      storage->transfer_char = vc1hwdGetBits(p_strm_data, 8);
      /* MATRIX_COEFF */
      storage->matrix_coef = vc1hwdGetBits(p_strm_data, 8);
    }

  } /* end of DISPLAY_EXT */

  /* HDR_PARAM_FLAG */
  storage->hrd_param_flag = vc1hwdGetBits(p_strm_data, 1);

  if( storage->hrd_param_flag ) {
    /* HRD_NUM_LEAKY_BUCKETS */
    storage->hrd_num_leaky_buckets = vc1hwdGetBits(p_strm_data, 5);
    /* BIT_RATE_EXPONENT */
    storage->bit_rate_exponent = vc1hwdGetBits(p_strm_data, 4);
    /* BUFFER_SIZE_EXPONENT */
    storage->buffer_size_exponent = vc1hwdGetBits(p_strm_data, 4);

    /* check if case of re-initialization */
    if (storage->hrd_rate) {
      DWLfree(storage->hrd_rate);
      storage->hrd_rate = NULL;
    }
    if (storage->hrd_buffer) {
      DWLfree(storage->hrd_buffer);
      storage->hrd_buffer = NULL;
    }
    storage->hrd_rate =
      DWLmalloc( storage->hrd_num_leaky_buckets * sizeof(u32) );
    storage->hrd_buffer =
      DWLmalloc( storage->hrd_num_leaky_buckets * sizeof(u32) );

    if ( (storage->hrd_rate == NULL) || (storage->hrd_buffer == NULL) ) {
      DWLfree(storage->hrd_rate);
      DWLfree(storage->hrd_buffer);
      storage->hrd_buffer = NULL;
      storage->hrd_rate = NULL;

      return (VC1HWD_MEMORY_FAIL);
    }

    for (i = 0; i < storage->hrd_num_leaky_buckets; i++) {
      /* HRD_RATE */
      storage->hrd_rate[i] = vc1hwdGetBits(p_strm_data, 16);
      /* HRD_BUFFER */
      storage->hrd_buffer[i] = vc1hwdGetBits(p_strm_data, 16);
    }
  }

  /* Sequence headers succesfully decoded */
  storage->hdrs_decoded |= HDR_SEQ;

  return VC1HWD_SEQ_HDRS_RDY;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdDecodeEntryPointLayer

        Functional description:
            Decodes entry-point layer of video bit stream

        Inputs:
            storage    Stream storage descriptor
            p_strm_data   Pointer to input stream data structure

        Outputs:

        Returns:
            VC1HWD_MEMORY_FAIL
            VC1HWD_METADATA_ERROR
            VC1HWD_ENTRY_POINT_HDRS_RDY

------------------------------------------------------------------------------*/
u32 vc1hwdDecodeEntryPointLayer( swStrmStorage_t *storage,
                                 strmData_t *p_strm_data ) {

  /* Variables */

  i32 tmp;
  u32 i;
  u32 w, h;

  /* Code */

  ASSERT(storage);
  ASSERT(p_strm_data);

  /* reset flag indicating already decoded entry point  header */
  storage->hdrs_decoded &= ~HDR_ENTRY;

  /* BROKEN_LINK */
  storage->broken_link = vc1hwdGetBits(p_strm_data, 1);
  /* CLOSED_ENTRY */
  storage->closed_entry = vc1hwdGetBits(p_strm_data, 1);

  /* PANSCAN_FLAG */
  storage->pan_scan_flag = vc1hwdGetBits(p_strm_data, 1);
  /* REFDIST_FLAG */
  storage->ref_dist_flag = vc1hwdGetBits(p_strm_data, 1);
  /* LOOPFILTER */
  storage->loop_filter = vc1hwdGetBits(p_strm_data, 1);
  /* FASTUVMC */
  storage->fast_uv_mc = vc1hwdGetBits(p_strm_data, 1);
  /* EXTENDED_MV */
  storage->extended_mv = vc1hwdGetBits(p_strm_data, 1);
  /* DQUANT */
  storage->dquant = vc1hwdGetBits(p_strm_data, 2);
  /* is dquant valid */
  if (storage->dquant > 2)
    return (VC1HWD_METADATA_ERROR);

  /* VSTRANSFORM */
  storage->vs_transform = vc1hwdGetBits(p_strm_data, 1);
  /* OVERLAP */
  storage->overlap = vc1hwdGetBits(p_strm_data, 1);
  /* QUANTIZER */
  storage->quantizer = vc1hwdGetBits(p_strm_data, 2);

  /* HRD_FULLNESS */
  if (storage->hrd_param_flag) {
    /* re-initialize if headers repeated */
    if (storage->hrd_fullness) {
      DWLfree(storage->hrd_fullness);
      storage->hrd_fullness = NULL;
    }
    storage->hrd_fullness =
      DWLmalloc(storage->hrd_num_leaky_buckets * sizeof(u32));

    if(storage->hrd_fullness == NULL)
      return (VC1HWD_MEMORY_FAIL);

    for (i = 0; i < storage->hrd_num_leaky_buckets; i++)
      storage->hrd_fullness[i] = vc1hwdGetBits(p_strm_data, 8);
  }
  /* CODED_SIZE_FLAG */
  tmp = vc1hwdGetBits(p_strm_data, 1);
  if (tmp) {
    tmp = vc1hwdGetBits(p_strm_data, 12);
    w = 2*tmp + 2;
    tmp = vc1hwdGetBits(p_strm_data, 12);
    h = 2*tmp + 2;

    if ( (storage->cur_coded_width != w) ||
         (storage->cur_coded_height != h) ) {
      storage->resolution_changed = HANTRO_TRUE;
    }
    storage->cur_coded_width = w;
    storage->cur_coded_height = h;

    if ((w > storage->max_coded_width) ||
        (h > storage->max_coded_height) ) {
      return VC1HWD_METADATA_ERROR;
    }
#ifdef ASIC_TRACE_SUPPORT
    if (storage->cur_coded_width != storage->max_coded_width ||
        storage->cur_coded_height != storage->max_coded_height)
      trace_vc1_dec_tools.multi_resolution = 1;
#endif
  } else {
    if ( (storage->cur_coded_width != storage->max_coded_width) ||
         (storage->cur_coded_height != storage->max_coded_height) ) {
      storage->resolution_changed = HANTRO_TRUE;
    }
    storage->cur_coded_width = storage->max_coded_width;
    storage->cur_coded_height = storage->max_coded_height;
  }
  /* Check against minimum size. Maximum size is check in Seq layer */
  if ( (storage->cur_coded_width < storage->min_coded_width) ||
       (storage->cur_coded_height < storage->min_coded_height) ||
       (storage->interlace &&
        ((storage->max_coded_height>>1) < storage->min_coded_height)) )
    return (VC1HWD_METADATA_ERROR);

  storage->pic_width_in_mbs  = (storage->cur_coded_width+15) >> 4;
  storage->pic_height_in_mbs = (storage->cur_coded_height+15) >> 4;
  storage->num_of_mbs       = (storage->pic_width_in_mbs *
                               storage->pic_height_in_mbs);
  if (storage->num_of_mbs > MAX_NUM_MBS)
    return (VC1HWD_METADATA_ERROR);

  /* EXTENDED_DMV */
  if (storage->extended_mv)
    storage->extended_dmv = vc1hwdGetBits(p_strm_data, 1);
  /* RANGE_MAPY_FLAG */
  storage->range_map_yflag = vc1hwdGetBits(p_strm_data, 1);
  /* RANGE_MAPY */
  if (storage->range_map_yflag)
    storage->range_map_y = vc1hwdGetBits(p_strm_data, 3);
  /* RANGE_MAPUV_FLAG */
  storage->range_map_uv_flag = vc1hwdGetBits(p_strm_data, 1);
  /* RANGE_MAPUV */
  if (storage->range_map_uv_flag)
    storage->range_map_uv = vc1hwdGetBits(p_strm_data, 3);
#ifdef ASIC_TRACE_SUPPORT
  if (storage->range_map_yflag || storage->range_map_uv_flag)
    trace_vc1_dec_tools.range_mapping = 1;
#endif

  /* Entry-Point headers succesfully decoded */
  storage->hdrs_decoded |= HDR_ENTRY;

  return VC1HWD_ENTRY_POINT_HDRS_RDY;
}

