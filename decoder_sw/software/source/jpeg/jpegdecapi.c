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

#include "version.h"
#include "dwl.h"
#include "basetype.h"
#include "jpegdecapi.h"
#include "jpegdeccontainer.h"
#include "jpegdecmarkers.h"
#include "jpegdecinternal.h"
#include "jpegdecutils.h"
#include "jpegdechdrs.h"
#include "jpegdecscan.h"
#include "jpegregdrv.h"
#include "jpeg_pp_pipeline.h"
#include "commonconfig.h"

#ifdef JPEGDEC_ASIC_TRACE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jpegasicdbgtrace.h"
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

#ifdef JPEGDEC_PP_TRACE
#include "ppapi.h"
#endif /* #ifdef JPEGDEC_PP_TRACE */

static void JpegDecPreparePp(JpegDecContainer * jpeg_dec_cont);

/*------------------------------------------------------------------------------
       Version Information - DO NOT CHANGE!
------------------------------------------------------------------------------*/

#define JPG_MAJOR_VERSION 1
#define JPG_MINOR_VERSION 1

/*------------------------------------------------------------------------------
    2. External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/
#ifdef JPEGDEC_TRACE
#define JPEGDEC_API_TRC(str)    JpegDecTrace((str))
#else
#define JPEGDEC_API_TRC(str)
#endif

#define JPEGDEC_CLEAR_IRQ  SetDecRegister(PTR_JPGC->jpeg_regs, \
                                          HWIF_DEC_IRQ_STAT, 0); \
                           SetDecRegister(PTR_JPGC->jpeg_regs, \
                                          HWIF_DEC_IRQ, 0);

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    5. Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function name: JpegDecInit

        Functional description:
            Init jpeg decoder

        Inputs:
            JpegDecInst * dec_inst     a reference to the jpeg decoder instance is
                                         stored here

        Outputs:
            JPEGDEC_OK
            JPEGDEC_INITFAIL
            JPEGDEC_PARAM_ERROR
            JPEGDEC_DWL_ERROR
            JPEGDEC_MEMFAIL

------------------------------------------------------------------------------*/
JpegDecRet JpegDecInit(JpegDecInst * dec_inst) {
  JpegDecContainer *p_jpeg_cont;
  const void *dwl;
  u32 i = 0;
  u32 asic_id;
  u32 fuse_status = 0;
  u32 extensions_supported;
  u32 webp_support;

  struct DWLInitParam dwl_init;

  JPEGDEC_API_TRC("JpegDecInit#");

  /* check that right shift on negative numbers is performed signed */
  /*lint -save -e* following check causes multiple lint messages */
  if(((-1) >> 1) != (-1)) {
    JPEGDEC_API_TRC("JpegDecInit# ERROR: Right shift is not signed");
    return (JPEGDEC_INITFAIL);
  }

  /*lint -restore */
  if(dec_inst == NULL) {
    JPEGDEC_API_TRC("JpegDecInit# ERROR: dec_inst == NULL");
    return (JPEGDEC_PARAM_ERROR);
  }
  *dec_inst = NULL;   /* return NULL instance for any error */

  /* check for proper hardware */
  asic_id = DWLReadAsicID(DWL_CLIENT_TYPE_JPEG_DEC);

  {
    /* check that JPEG decoding supported in HW */
    DWLHwConfig hw_cfg;

    DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_JPEG_DEC);
    if(!hw_cfg.jpeg_support) {
      JPEGDEC_API_TRC(("JpegDecInit# ERROR: JPEG not supported in HW\n"));
      return JPEGDEC_FORMAT_NOT_SUPPORTED;
    }

    if(!hw_cfg.addr64_support && sizeof(void *) == 8) {
      JPEGDEC_API_TRC("JpegDecInit# ERROR: HW not support 64bit address!\n");
      return (JPEGDEC_PARAM_ERROR);
    }

    /* check progressive support */
    if((asic_id >> 16) != 0x8170U) {
      /* progressive decoder */
      if(hw_cfg.jpeg_support == JPEG_BASELINE)
        fuse_status = 1;
    }

    extensions_supported = hw_cfg.jpeg_esupport;
    webp_support = hw_cfg.webp_support;

  }

  dwl_init.client_type = DWL_CLIENT_TYPE_JPEG_DEC;

  /* Initialize Wrapper */
  dwl = DWLInit(&dwl_init);
  if(dwl == NULL) {
    JPEGDEC_API_TRC("JpegDecInit# ERROR: DWL Init failed");
    return (JPEGDEC_DWL_ERROR);
  }

  p_jpeg_cont = (JpegDecContainer *) DWLmalloc(sizeof(JpegDecContainer));
  if(p_jpeg_cont == NULL) {
    (void) DWLRelease(dwl);
    return (JPEGDEC_MEMFAIL);
  }

  (void)DWLmemset(p_jpeg_cont, 0, sizeof(JpegDecContainer));

  p_jpeg_cont->dwl = dwl;

  /* reset internal structures */
  JpegDecClearStructs(p_jpeg_cont, 0);

  /* Reset shadow registers */
  for(i = 1; i < TOTAL_X170_REGISTERS; i++) {
    p_jpeg_cont->jpeg_regs[i] = 0;
  }

  SetCommonConfigRegs(p_jpeg_cont->jpeg_regs,DWL_CLIENT_TYPE_JPEG_DEC);

  /* save HW version so we dont need to check it all
   * the time when deciding the control stuff */
  p_jpeg_cont->is8190 = (asic_id >> 16) != 0x8170U ? 1 : 0;
  /* set HW related config's */
  if(p_jpeg_cont->is8190) {
    p_jpeg_cont->fuse_burned = fuse_status;
    /* max */
    if(webp_support) { /* webp implicates 256Mpix support */
      p_jpeg_cont->max_supported_width = JPEGDEC_MAX_WIDTH_WEBP;
      p_jpeg_cont->max_supported_height = JPEGDEC_MAX_HEIGHT_WEBP;
      p_jpeg_cont->max_supported_pixel_amount = JPEGDEC_MAX_PIXEL_AMOUNT_WEBP;
      p_jpeg_cont->max_supported_slice_size = JPEGDEC_MAX_SLICE_SIZE_WEBP;
    } else {
      p_jpeg_cont->max_supported_width = JPEGDEC_MAX_WIDTH_8190;
      p_jpeg_cont->max_supported_height = JPEGDEC_MAX_HEIGHT_8190;
      p_jpeg_cont->max_supported_pixel_amount = JPEGDEC_MAX_PIXEL_AMOUNT_8190;
      p_jpeg_cont->max_supported_slice_size = JPEGDEC_MAX_SLICE_SIZE_8190;
    }
  } else {
    /* max */
    p_jpeg_cont->max_supported_width = JPEGDEC_MAX_WIDTH;
    p_jpeg_cont->max_supported_height = JPEGDEC_MAX_HEIGHT;
    p_jpeg_cont->max_supported_pixel_amount = JPEGDEC_MAX_PIXEL_AMOUNT;
    p_jpeg_cont->max_supported_slice_size = JPEGDEC_MAX_SLICE_SIZE;
  }

  /* min */
  p_jpeg_cont->min_supported_width = JPEGDEC_MIN_WIDTH;
  p_jpeg_cont->min_supported_height = JPEGDEC_MIN_HEIGHT;

  p_jpeg_cont->extensions_supported = extensions_supported;

  *dec_inst = (JpegDecContainer *) p_jpeg_cont;

  JPEGDEC_API_TRC("JpegDecInit# OK\n");
  return (JPEGDEC_OK);
}

/*------------------------------------------------------------------------------

    Function name: JpegDecRelease

        Functional description:
            Release Jpeg decoder

        Inputs:
            JpegDecInst dec_inst    jpeg decoder instance

            void

------------------------------------------------------------------------------*/
void JpegDecRelease(JpegDecInst dec_inst) {

#define PTR_JPGC ((JpegDecContainer *) dec_inst)

  const void *dwl;

  JPEGDEC_API_TRC("JpegDecRelease#");

  if(PTR_JPGC == NULL) {
    JPEGDEC_API_TRC("JpegDecRelease# ERROR: dec_inst == NULL");
    return;
  }

  dwl = PTR_JPGC->dwl;

  if(PTR_JPGC->asic_running) {
    /* Release HW */
    DWLDisableHw(PTR_JPGC->dwl, PTR_JPGC->core_id, 4 * 1, 0);
    DWLReleaseHw(PTR_JPGC->dwl, PTR_JPGC->core_id);
  }

  if(PTR_JPGC->vlc.ac_table0.vals) {
    DWLfree(PTR_JPGC->vlc.ac_table0.vals);
  }
  if(PTR_JPGC->vlc.ac_table1.vals) {
    DWLfree(PTR_JPGC->vlc.ac_table1.vals);
  }
  if(PTR_JPGC->vlc.ac_table2.vals) {
    DWLfree(PTR_JPGC->vlc.ac_table2.vals);
  }
  if(PTR_JPGC->vlc.ac_table3.vals) {
    DWLfree(PTR_JPGC->vlc.ac_table3.vals);
  }
  if(PTR_JPGC->vlc.dc_table0.vals) {
    DWLfree(PTR_JPGC->vlc.dc_table0.vals);
  }
  if(PTR_JPGC->vlc.dc_table1.vals) {
    DWLfree(PTR_JPGC->vlc.dc_table1.vals);
  }
  if(PTR_JPGC->vlc.dc_table2.vals) {
    DWLfree(PTR_JPGC->vlc.dc_table2.vals);
  }
  if(PTR_JPGC->vlc.dc_table3.vals) {
    DWLfree(PTR_JPGC->vlc.dc_table3.vals);
  }
  if(PTR_JPGC->frame.p_buffer) {
    DWLfree(PTR_JPGC->frame.p_buffer);
  }
  /* progressive */
  if(PTR_JPGC->info.p_coeff_base.virtual_address) {
    DWLFreeLinear(dwl, &(PTR_JPGC->info.p_coeff_base));
    PTR_JPGC->info.p_coeff_base.virtual_address = NULL;
  }
  if(PTR_JPGC->info.tmp_strm.virtual_address) {
    DWLFreeLinear(dwl, &(PTR_JPGC->info.tmp_strm));
    PTR_JPGC->info.tmp_strm.virtual_address = NULL;
  }
  if(PTR_JPGC->frame.p_table_base.virtual_address) {
    DWLFreeLinear(dwl, &(PTR_JPGC->frame.p_table_base));
    PTR_JPGC->frame.p_table_base.virtual_address = NULL;
  }
  /* if not user allocated memories */
  if(!PTR_JPGC->info.user_alloc_mem) {
    if(PTR_JPGC->asic_buff.out_luma_buffer.virtual_address != NULL) {
      DWLFreeRefFrm(dwl, &(PTR_JPGC->asic_buff.out_luma_buffer));
      PTR_JPGC->asic_buff.out_luma_buffer.virtual_address = NULL;
    }
    if(PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address != NULL) {
      DWLFreeRefFrm(dwl, &(PTR_JPGC->asic_buff.out_chroma_buffer));
      PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address = NULL;
    }
    if(PTR_JPGC->asic_buff.out_chroma_buffer2.virtual_address != NULL) {
      DWLFreeRefFrm(dwl, &(PTR_JPGC->asic_buff.out_chroma_buffer2));
      PTR_JPGC->asic_buff.out_chroma_buffer2.virtual_address = NULL;
    }
  } else {
    PTR_JPGC->asic_buff.out_luma_buffer.virtual_address = NULL;
    PTR_JPGC->asic_buff.out_chroma_buffer.virtual_address = NULL;
  }

  if(dec_inst) {
    DWLfree(PTR_JPGC);
  }
  (void) DWLRelease(dwl);

  JPEGDEC_API_TRC("JpegDecRelease# OK\n");

  return;

#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

    Function name: JpegDecGetImageInfo

        Functional description:
            Get image information of the JFIF

        Inputs:
            JpegDecInst dec_inst     jpeg decoder instance
            JpegDecInput *p_dec_in    input stream information
            JpegDecImageInfo *p_image_info
                    structure where the image info is written

        Outputs:
            JPEGDEC_OK
            JPEGDEC_ERROR
            JPEGDEC_UNSUPPORTED
            JPEGDEC_PARAM_ERROR
            JPEGDEC_INCREASE_INPUT_BUFFER
            JPEGDEC_INVALID_STREAM_LENGTH
            JPEGDEC_INVALID_INPUT_BUFFER_SIZE

------------------------------------------------------------------------------*/

/* Get image information of the JFIF */
JpegDecRet JpegDecGetImageInfo(JpegDecInst dec_inst, JpegDecInput * p_dec_in,
                               JpegDecImageInfo * p_image_info) {

#define PTR_JPGC ((JpegDecContainer *) dec_inst)

  u32 Nf = 0;
  u32 Ns = 0;
  u32 NsThumb = 0;
  u32 i, j = 0;
  u32 init = 0;
  u32 init_thumb = 0;
  u32 H[MAX_NUMBER_OF_COMPONENTS];
  u32 V[MAX_NUMBER_OF_COMPONENTS];
  u32 Htn[MAX_NUMBER_OF_COMPONENTS];
  u32 Vtn[MAX_NUMBER_OF_COMPONENTS];
  u32 Hmax = 0;
  u32 Vmax = 0;
  u32 header_length = 0;
  u32 current_byte = 0;
  u32 current_bytes = 0;
  u32 app_length = 0;
  u32 app_bits = 0;
  u32 thumbnail = 0;
  u32 error_code = 0;
  u32 new_header_value = 0;
  u32 marker_byte = 0;

#ifdef JPEGDEC_ERROR_RESILIENCE
  u32 error_resilience = 0;
  u32 error_resilience_thumb = 0;
#endif /* JPEGDEC_ERROR_RESILIENCE */

  StreamStorage stream;

  JPEGDEC_API_TRC("JpegDecGetImageInfo#");

  /* check pointers & parameters */
  if(dec_inst == NULL || p_dec_in == NULL || p_image_info == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(p_dec_in->stream_buffer.virtual_address) ||
      X170_CHECK_BUS_ADDRESS(p_dec_in->stream_buffer.bus_address)) {
    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: NULL parameter");
    return (JPEGDEC_PARAM_ERROR);
  }

  /* Check the stream lenth */
  if(p_dec_in->stream_length < 1) {
    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: p_dec_in->stream_length");
    return (JPEGDEC_INVALID_STREAM_LENGTH);
  }

  /* Check the stream lenth */
  if((p_dec_in->stream_length > DEC_X170_MAX_STREAM) &&
      (p_dec_in->buffer_size < JPEGDEC_X170_MIN_BUFFER ||
       p_dec_in->buffer_size > JPEGDEC_X170_MAX_BUFFER)) {
    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: p_dec_in->buffer_size");
    return (JPEGDEC_INVALID_INPUT_BUFFER_SIZE);
  }

  /* Check the stream buffer size */
  if(p_dec_in->buffer_size && (p_dec_in->buffer_size < JPEGDEC_X170_MIN_BUFFER ||
                               p_dec_in->buffer_size > JPEGDEC_X170_MAX_BUFFER)) {
    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: p_dec_in->buffer_size");
    return (JPEGDEC_INVALID_INPUT_BUFFER_SIZE);
  }

  /* Check the stream buffer size */
  if(p_dec_in->buffer_size && ((p_dec_in->buffer_size % 8) != 0)) {
    JPEGDEC_API_TRC
    ("JpegDecGetImageInfo# ERROR: p_dec_in->buffer_size % 8) != 0");
    return (JPEGDEC_INVALID_INPUT_BUFFER_SIZE);
  }

  /* reset sampling factors */
  for(i = 0; i < MAX_NUMBER_OF_COMPONENTS; i++) {
    H[i] = 0;
    V[i] = 0;
    Htn[i] = 0;
    Vtn[i] = 0;
  }

  /* imageInfo initialization */
  p_image_info->display_width = 0;
  p_image_info->display_height = 0;
  p_image_info->output_width = 0;
  p_image_info->output_height = 0;
  p_image_info->version = 0;
  p_image_info->units = 0;
  p_image_info->x_density = 0;
  p_image_info->y_density = 0;
  p_image_info->output_format = 0;

  /* Default value to "Thumbnail" */
  p_image_info->thumbnail_type = JPEGDEC_NO_THUMBNAIL;
  p_image_info->display_width_thumb = 0;
  p_image_info->display_height_thumb = 0;
  p_image_info->output_width_thumb = 0;
  p_image_info->output_height_thumb = 0;
  p_image_info->output_format_thumb = 0;

  /* utils initialization */
  stream.bit_pos_in_byte = 0;
  stream.p_curr_pos = (u8 *) p_dec_in->stream_buffer.virtual_address;
  stream.p_start_of_stream = (u8 *) p_dec_in->stream_buffer.virtual_address;
  stream.read_bits = 0;
  stream.appn_flag = 0;

  /* stream length */
  if(!p_dec_in->buffer_size)
    stream.stream_length = p_dec_in->stream_length;
  else
    stream.stream_length = p_dec_in->buffer_size;

  /* Read decoding parameters */
  for(stream.read_bits = 0; (stream.read_bits / 8) < stream.stream_length;
      stream.read_bits++) {
    /* Look for marker prefix byte from stream */
    marker_byte = JpegDecGetByte(&(stream));
    if(marker_byte == 0xFF) {
      current_byte = JpegDecGetByte(&(stream));

      /* switch to certain header decoding */
      switch (current_byte) {
      /* baseline marker */
      case SOF0:
      /* progresive marker */
      case SOF2:
        if(current_byte == SOF0)
          p_image_info->coding_mode = PTR_JPGC->info.operation_type =
                                        JPEGDEC_BASELINE;
        else
          p_image_info->coding_mode = PTR_JPGC->info.operation_type =
                                        JPEGDEC_PROGRESSIVE;
        /* Frame header */
        i++;
        Hmax = 0;
        Vmax = 0;

        /* SOF0/SOF2 length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }

        /* Sample precision (only 8 bits/sample supported) */
        current_byte = JpegDecGetByte(&(stream));
        if(current_byte != 8) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Sample precision");
          return (JPEGDEC_UNSUPPORTED);
        }

        /* Number of Lines */
        p_image_info->output_height = JpegDecGet2Bytes(&(stream));
        p_image_info->display_height = p_image_info->output_height;

        if(p_image_info->output_height < 1) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: p_image_info->output_height Unsupported");
          return (JPEGDEC_UNSUPPORTED);
        }

#ifdef JPEGDEC_ERROR_RESILIENCE
        if((p_image_info->output_height & 0xF) &&
            (p_image_info->output_height & 0xF) <= 8)
          error_resilience = 1;
#endif /* JPEGDEC_ERROR_RESILIENCE */

        /* round up to next multiple-of-16 */
        p_image_info->output_height += 0xf;
        p_image_info->output_height &= ~(0xf);

        /* Number of Samples per Line */
        p_image_info->output_width = JpegDecGet2Bytes(&(stream));
        p_image_info->display_width = p_image_info->output_width;
        if(p_image_info->output_width < 1) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: p_image_info->output_width unsupported");
          return (JPEGDEC_UNSUPPORTED);
        }
        p_image_info->output_width += 0xf;
        p_image_info->output_width &= ~(0xf);

        /* check if height changes (MJPEG) */
        if(PTR_JPGC->frame.hw_y != 0 &&
            (PTR_JPGC->frame.hw_y != p_image_info->output_height)) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: p_image_info->output_height changed (MJPEG)");
          new_header_value = 1;
        }

        /* check if width changes (MJPEG) */
        if(PTR_JPGC->frame.hw_x != 0 &&
            (PTR_JPGC->frame.hw_x != p_image_info->output_width)) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: p_image_info->output_width changed (MJPEG)");
          new_header_value = 1;
        }

        /* check for minimum and maximum dimensions */
        if(p_image_info->output_width < PTR_JPGC->min_supported_width ||
            p_image_info->output_height < PTR_JPGC->min_supported_height ||
            p_image_info->output_width > PTR_JPGC->max_supported_width ||
            p_image_info->output_height > PTR_JPGC->max_supported_height ||
            (p_image_info->output_width * p_image_info->output_height) >
            PTR_JPGC->max_supported_pixel_amount) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Unsupported size");
          return (JPEGDEC_UNSUPPORTED);
        }

        /* Number of Image Components per Frame */
        Nf = JpegDecGetByte(&(stream));
        if(Nf != 3 && Nf != 1) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Number of Image Components per Frame");
          return (JPEGDEC_UNSUPPORTED);
        }
        /* length 8 + 3 x Nf */
        if(header_length != (8 + (3 * Nf))) {
          JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Incorrect SOF0 header length");
          return (JPEGDEC_UNSUPPORTED);
        }
        for(j = 0; j < Nf; j++) {
          /* jump over component identifier */
          if(JpegDecFlushBits(&(stream), 8) == STRM_ERROR) {
            error_code = 1;
            break;
          }

          /* Horizontal sampling factor */
          current_byte = JpegDecGetByte(&(stream));
          H[j] = (current_byte >> 4);

          /* Vertical sampling factor */
          V[j] = (current_byte & 0xF);

          /* jump over Tq */
          if(JpegDecFlushBits(&(stream), 8) == STRM_ERROR) {
            error_code = 1;
            break;
          }

          if(H[j] > Hmax)
            Hmax = H[j];
          if(V[j] > Vmax)
            Vmax = V[j];
        }
        if(Hmax == 0 || Vmax == 0) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Hmax == 0 || Vmax == 0");
          return (JPEGDEC_UNSUPPORTED);
        }
#ifdef JPEGDEC_ERROR_RESILIENCE
        if(H[0] == 2 && V[0] == 2 &&
            H[1] == 1 && V[1] == 1 && H[2] == 1 && V[2] == 1) {
          p_image_info->output_format = JPEGDEC_YCbCr420_SEMIPLANAR;
        } else {
          /* check if fill needed */
          if(error_resilience) {
            p_image_info->output_height -= 16;
            p_image_info->display_height = p_image_info->output_height;
          }
        }
#endif /* JPEGDEC_ERROR_RESILIENCE */

        /* check format */
        if(H[0] == 2 && V[0] == 2 &&
            H[1] == 1 && V[1] == 1 && H[2] == 1 && V[2] == 1) {
          p_image_info->output_format = JPEGDEC_YCbCr420_SEMIPLANAR;
          PTR_JPGC->frame.num_mcu_in_row = (p_image_info->output_width / 16);
          PTR_JPGC->frame.num_mcu_in_frame = ((p_image_info->output_width *
                                               p_image_info->output_height) /
                                              256);
        } else if(H[0] == 2 && V[0] == 1 &&
                  H[1] == 1 && V[1] == 1 && H[2] == 1 && V[2] == 1) {
          p_image_info->output_format = JPEGDEC_YCbCr422_SEMIPLANAR;
          PTR_JPGC->frame.num_mcu_in_row = (p_image_info->output_width / 16);
          PTR_JPGC->frame.num_mcu_in_frame = ((p_image_info->output_width *
                                               p_image_info->output_height) /
                                              128);
        } else if(H[0] == 1 && V[0] == 2 &&
                  H[1] == 1 && V[1] == 1 && H[2] == 1 && V[2] == 1) {
          p_image_info->output_format = JPEGDEC_YCbCr440;
          PTR_JPGC->frame.num_mcu_in_row = (p_image_info->output_width / 8);
          PTR_JPGC->frame.num_mcu_in_frame = ((p_image_info->output_width *
                                               p_image_info->output_height) /
                                              128);
        } else if(H[0] == 1 && V[0] == 1 &&
                  H[1] == 0 && V[1] == 0 && H[2] == 0 && V[2] == 0) {
          p_image_info->output_format = JPEGDEC_YCbCr400;
          PTR_JPGC->frame.num_mcu_in_row = (p_image_info->output_width / 8);
          PTR_JPGC->frame.num_mcu_in_frame = ((p_image_info->output_width *
                                               p_image_info->output_height) /
                                              64);
        } else if(PTR_JPGC->extensions_supported &&
                  H[0] == 4 && V[0] == 1 &&
                  H[1] == 1 && V[1] == 1 && H[2] == 1 && V[2] == 1) {
          /* YUV411 output has to be 32 pixel multiple */
          if(p_image_info->output_width & 0x1F) {
            p_image_info->output_width += 16;
          }

          /* check for maximum dimensions */
          if(p_image_info->output_width > PTR_JPGC->max_supported_width ||
              (p_image_info->output_width * p_image_info->output_height) >
              PTR_JPGC->max_supported_pixel_amount) {
            JPEGDEC_API_TRC
            ("JpegDecGetImageInfo# ERROR: Unsupported size");
            return (JPEGDEC_UNSUPPORTED);
          }

          p_image_info->output_format = JPEGDEC_YCbCr411_SEMIPLANAR;
          PTR_JPGC->frame.num_mcu_in_row = (p_image_info->output_width / 32);
          PTR_JPGC->frame.num_mcu_in_frame = ((p_image_info->output_width *
                                               p_image_info->output_height) /
                                              256);
        } else if(PTR_JPGC->extensions_supported &&
                  H[0] == 1 && V[0] == 1 &&
                  H[1] == 1 && V[1] == 1 && H[2] == 1 && V[2] == 1) {
          p_image_info->output_format = JPEGDEC_YCbCr444_SEMIPLANAR;
          PTR_JPGC->frame.num_mcu_in_row = (p_image_info->output_width / 8);
          PTR_JPGC->frame.num_mcu_in_frame = ((p_image_info->output_width *
                                               p_image_info->output_height) /
                                              64);
        } else {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Unsupported YCbCr format");
          return (JPEGDEC_UNSUPPORTED);
        }

        /* check if output format changes (MJPEG) */
        if(PTR_JPGC->info.get_info_ycb_cr_mode != 0 &&
            (PTR_JPGC->info.get_info_ycb_cr_mode != p_image_info->output_format)) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: YCbCr format changed (MJPEG)");
          new_header_value = 1;
        }
        break;
      case SOS:
        /* SOS length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }

        /* check if interleaved or non-ibnterleaved */
        Ns = JpegDecGetByte(&(stream));
        if(Ns == MIN_NUMBER_OF_COMPONENTS &&
            p_image_info->output_format != JPEGDEC_YCbCr400 &&
            p_image_info->coding_mode == JPEGDEC_BASELINE) {
          p_image_info->coding_mode = PTR_JPGC->info.operation_type =
                                        JPEGDEC_NONINTERLEAVED;
        }
        /* Number of Image Components */
        if(Ns != 3 && Ns != 1) {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Number of Image Components");
          return (JPEGDEC_UNSUPPORTED);
        }
        /* length 6 + 2 x Ns */
        if(header_length != (6 + (2 * Ns))) {
          JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Incorrect SOS header length");
          return (JPEGDEC_UNSUPPORTED);
        }
        /* jump over SOS header */
        if(header_length != 0) {
          stream.read_bits += ((header_length * 8) - 16);
          stream.p_curr_pos += (((header_length * 8) - 16) / 8);
        }

        if((stream.read_bits + 8) < (8 * stream.stream_length)) {
          PTR_JPGC->info.init = 1;
          init = 1;
        } else {
          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Needs to increase input buffer");
          return (JPEGDEC_INCREASE_INPUT_BUFFER);
        }
        break;
      case DQT:
        /* DQT length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        /* length >= (2 + 65) (baseline) */
        if(header_length < (2 + 65)) {
          JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Incorrect DQT header length");
          return (JPEGDEC_UNSUPPORTED);
        }
        /* jump over DQT header */
        if(header_length != 0) {
          stream.read_bits += ((header_length * 8) - 16);
          stream.p_curr_pos += (((header_length * 8) - 16) / 8);
        }
        break;
      case DHT:
        /* DHT length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        /* length >= 2 + 17 */
        if(header_length < 19) {
          JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Incorrect DHT header length");
          return (JPEGDEC_UNSUPPORTED);
        }
        /* jump over DHT header */
        if(header_length != 0) {
          stream.read_bits += ((header_length * 8) - 16);
          stream.p_curr_pos += (((header_length * 8) - 16) / 8);
        }
        break;
      case DRI:
        /* DRI length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        /* length == 4 */
        if(header_length != 4) {
          JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Incorrect DRI header length");
          return (JPEGDEC_UNSUPPORTED);
        }
#if 0
        /* jump over DRI header */
        if(header_length != 0) {
          stream.read_bits += ((header_length * 8) - 16);
          stream.p_curr_pos += (((header_length * 8) - 16) / 8);
        }
#endif
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        PTR_JPGC->frame.Ri = header_length;
        break;
      /* application segments */
      case APP0:
        JPEGDEC_API_TRC("JpegDecGetImageInfo# APP0 in GetImageInfo");
        /* reset */
        app_bits = 0;
        app_length = 0;
        stream.appn_flag = 0;

        /* APP0 length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        /* length > 2 */
        if(header_length < 2)
          break;
        app_length = header_length;
        if(app_length < 16) {
          stream.appn_flag = 1;
          if(JpegDecFlushBits(&(stream), ((app_length * 8) - 16)) ==
              STRM_ERROR) {
            error_code = 1;
            break;
          }
          break;
        }
        app_bits += 16;

        /* check identifier */
        current_bytes = JpegDecGet2Bytes(&(stream));
        app_bits += 16;
        if(current_bytes != 0x4A46) {
          stream.appn_flag = 1;
          if(JpegDecFlushBits(&(stream), ((app_length * 8) - app_bits))
              == STRM_ERROR) {
            error_code = 1;
            break;
          }
          break;
        }
        current_bytes = JpegDecGet2Bytes(&(stream));
        app_bits += 16;
        if(current_bytes != 0x4946 && current_bytes != 0x5858) {
          stream.appn_flag = 1;
          if(JpegDecFlushBits(&(stream), ((app_length * 8) - app_bits))
              == STRM_ERROR) {
            error_code = 1;
            break;
          }
          break;
        }

        /* APP0 Extended */
        if(current_bytes == 0x5858) {
          thumbnail = 1;
        }
        current_byte = JpegDecGetByte(&(stream));
        app_bits += 8;
        if(current_byte != 0x00) {
          stream.appn_flag = 1;
          if(JpegDecFlushBits(&(stream), ((app_length * 8) - app_bits))
              == STRM_ERROR) {
            error_code = 1;
            break;
          }
          stream.appn_flag = 0;
          break;
        }

        /* APP0 Extended thumb type */
        if(thumbnail) {
          /* extension code */
          current_byte = JpegDecGetByte(&(stream));
          if(current_byte == JPEGDEC_THUMBNAIL_JPEG) {
            p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_JPEG;
            app_bits += 8;
            stream.appn_flag = 1;

            /* check thumbnail data */
            Hmax = 0;
            Vmax = 0;

            /* Read decoding parameters */
            for(; (stream.read_bits / 8) < stream.stream_length;
                stream.read_bits++) {
              /* Look for marker prefix byte from stream */
              app_bits += 8;
              marker_byte = JpegDecGetByte(&(stream));
              /* check if APP0 decoded */
              if( ((app_bits + 8) / 8) >= app_length)
                break;
              if(marker_byte == 0xFF) {
                /* switch to certain header decoding */
                app_bits += 8;

                current_byte = JpegDecGetByte(&(stream));
                switch (current_byte) {
                /* baseline marker */
                case SOF0:
                /* progresive marker */
                case SOF2:
                  if(current_byte == SOF0)
                    p_image_info->coding_mode_thumb =
                      PTR_JPGC->info.operation_type_thumb =
                        JPEGDEC_BASELINE;
                  else
                    p_image_info->coding_mode_thumb =
                      PTR_JPGC->info.operation_type_thumb =
                        JPEGDEC_PROGRESSIVE;
                  /* Frame header */
                  i++;

                  /* jump over Lf field */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR ||
                      ((stream.read_bits + ((header_length * 8) - 16)) >
                       (8 * stream.stream_length))) {
                    error_code = 1;
                    break;
                  }
                  app_bits += 16;

                  /* Sample precision (only 8 bits/sample supported) */
                  current_byte = JpegDecGetByte(&(stream));
                  app_bits += 8;
                  if(current_byte != 8) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Thumbnail Sample precision");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }

                  /* Number of Lines */
                  p_image_info->output_height_thumb =
                    JpegDecGet2Bytes(&(stream));
                  app_bits += 16;
                  p_image_info->display_height_thumb =
                    p_image_info->output_height_thumb;
                  if(p_image_info->output_height_thumb < 1) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: p_image_info->output_height_thumb unsupported");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }
#ifdef JPEGDEC_ERROR_RESILIENCE
                  if((p_image_info->output_height_thumb & 0xF) &&
                      (p_image_info->output_height_thumb & 0xF) <=
                      8)
                    error_resilience_thumb = 1;
#endif /* JPEGDEC_ERROR_RESILIENCE */

                  /* round up to next multiple-of-16 */
                  p_image_info->output_height_thumb += 0xf;
                  p_image_info->output_height_thumb &= ~(0xf);

                  /* Number of Samples per Line */
                  p_image_info->output_width_thumb =
                    JpegDecGet2Bytes(&(stream));
                  app_bits += 16;
                  p_image_info->display_width_thumb =
                    p_image_info->output_width_thumb;
                  if(p_image_info->output_width_thumb < 1) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: p_image_info->output_width_thumb unsupported");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }
                  p_image_info->output_width_thumb += 0xf;
                  p_image_info->output_width_thumb &= ~(0xf);

                  /* check if height changes (MJPEG) */
                  if(PTR_JPGC->frame.hw_ytn != 0 &&
                      (PTR_JPGC->frame.hw_ytn != p_image_info->output_height_thumb)) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: p_image_info->output_height_thumb changed (MJPEG)");
                    new_header_value = 1;
                  }

                  /* check if width changes (MJPEG) */
                  if(PTR_JPGC->frame.hw_xtn != 0 &&
                      (PTR_JPGC->frame.hw_xtn != p_image_info->output_width_thumb)) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: p_image_info->output_width_thumb changed (MJPEG)");
                    new_header_value = 1;
                  }

                  if(p_image_info->output_width_thumb <
                      PTR_JPGC->min_supported_width ||
                      p_image_info->output_height_thumb <
                      PTR_JPGC->min_supported_height ||
                      p_image_info->output_width_thumb >
                      JPEGDEC_MAX_WIDTH_TN ||
                      p_image_info->output_height_thumb >
                      JPEGDEC_MAX_HEIGHT_TN) {

                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Thumbnail Unsupported size");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }

                  /* Number of Image Components per Frame */
                  Nf = JpegDecGetByte(&(stream));
                  app_bits += 8;
                  if(Nf != 3 && Nf != 1) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Thumbnail Number of Image Components per Frame");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }
                  /* length 8 + 3 x Nf */
                  if(header_length != (8 + (3 * Nf))) {
                    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Thumbnail Incorrect SOF0 header length");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }
                  for(j = 0; j < Nf; j++) {

                    /* jump over component identifier */
                    if(JpegDecFlushBits(&(stream), 8) ==
                        STRM_ERROR) {
                      error_code = 1;
                      break;
                    }
                    app_bits += 8;

                    /* Horizontal sampling factor */
                    current_byte = JpegDecGetByte(&(stream));
                    app_bits += 8;
                    Htn[j] = (current_byte >> 4);

                    /* Vertical sampling factor */
                    Vtn[j] = (current_byte & 0xF);

                    /* jump over Tq */
                    if(JpegDecFlushBits(&(stream), 8) ==
                        STRM_ERROR) {
                      error_code = 1;
                      break;
                    }
                    app_bits += 8;

                    if(Htn[j] > Hmax)
                      Hmax = Htn[j];
                    if(Vtn[j] > Vmax)
                      Vmax = Vtn[j];
                  }
                  if(Hmax == 0 || Vmax == 0) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Thumbnail Hmax == 0 || Vmax == 0");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }
#ifdef JPEGDEC_ERROR_RESILIENCE
                  if(Htn[0] == 2 && Vtn[0] == 2 &&
                      Htn[1] == 1 && Vtn[1] == 1 &&
                      Htn[2] == 1 && Vtn[2] == 1) {
                    p_image_info->output_format_thumb =
                      JPEGDEC_YCbCr420_SEMIPLANAR;
                  } else {
                    /* check if fill needed */
                    if(error_resilience_thumb) {
                      p_image_info->output_height_thumb -= 16;
                      p_image_info->display_height_thumb =
                        p_image_info->output_height_thumb;
                    }
                  }
#endif /* JPEGDEC_ERROR_RESILIENCE */

                  /* check format */
                  if(Htn[0] == 2 && Vtn[0] == 2 &&
                      Htn[1] == 1 && Vtn[1] == 1 &&
                      Htn[2] == 1 && Vtn[2] == 1) {
                    p_image_info->output_format_thumb =
                      JPEGDEC_YCbCr420_SEMIPLANAR;
                  } else if(Htn[0] == 2 && Vtn[0] == 1 &&
                            Htn[1] == 1 && Vtn[1] == 1 &&
                            Htn[2] == 1 && Vtn[2] == 1) {
                    p_image_info->output_format_thumb =
                      JPEGDEC_YCbCr422_SEMIPLANAR;
                  } else if(Htn[0] == 1 && Vtn[0] == 2 &&
                            Htn[1] == 1 && Vtn[1] == 1 &&
                            Htn[2] == 1 && Vtn[2] == 1) {
                    p_image_info->output_format_thumb =
                      JPEGDEC_YCbCr440;
                  } else if(Htn[0] == 1 && Vtn[0] == 1 &&
                            Htn[1] == 0 && Vtn[1] == 0 &&
                            Htn[2] == 0 && Vtn[2] == 0) {
                    p_image_info->output_format_thumb =
                      JPEGDEC_YCbCr400;
                  } else if(PTR_JPGC->is8190 &&
                            Htn[0] == 4 && Vtn[0] == 1 &&
                            Htn[1] == 1 && Vtn[1] == 1 &&
                            Htn[2] == 1 && Vtn[2] == 1) {
                    p_image_info->output_format_thumb =
                      JPEGDEC_YCbCr411_SEMIPLANAR;
                  } else if(PTR_JPGC->is8190 &&
                            Htn[0] == 1 && Vtn[0] == 1 &&
                            Htn[1] == 1 && Vtn[1] == 1 &&
                            Htn[2] == 1 && Vtn[2] == 1) {
                    p_image_info->output_format_thumb =
                      JPEGDEC_YCbCr444_SEMIPLANAR;
                  } else {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Thumbnail Unsupported YCbCr format");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }

                  /* check if output format changes (MJPEG) */
                  if(PTR_JPGC->info.get_info_ycb_cr_mode_tn != 0 &&
                      (PTR_JPGC->info.get_info_ycb_cr_mode_tn != p_image_info->output_format_thumb)) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Thumbnail YCbCr format changed (MJPEG)");
                    new_header_value = 1;
                  }
                  break;
                case SOS:
                  /* SOS length */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR ||
                      ((stream.read_bits +
                        ((header_length * 8) - 16)) >
                       (8 * stream.stream_length))) {
                    error_code = 1;
                    break;
                  }

                  /* check if interleaved or non-ibnterleaved */
                  NsThumb = JpegDecGetByte(&(stream));
                  if(NsThumb == MIN_NUMBER_OF_COMPONENTS &&
                      p_image_info->output_format_thumb !=
                      JPEGDEC_YCbCr400 &&
                      p_image_info->coding_mode_thumb ==
                      JPEGDEC_BASELINE) {
                    p_image_info->coding_mode_thumb =
                      PTR_JPGC->info.operation_type_thumb =
                        JPEGDEC_NONINTERLEAVED;
                  }
                  if(NsThumb != 3 && NsThumb != 1) {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Thumbnail Number of Image Components");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }
                  /* length 6 + 2 x NsThumb */
                  if(header_length != (6 + (2 * NsThumb))) {
                    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Thumbnail Incorrect SOS header length");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                  }
                  /* jump over SOS header */
                  if(header_length != 0) {
                    stream.read_bits +=
                      ((header_length * 8) - 16);
                    stream.p_curr_pos +=
                      (((header_length * 8) - 16) / 8);
                  }

                  if((stream.read_bits + 8) <
                      (8 * stream.stream_length)) {
                    PTR_JPGC->info.init_thumb = 1;
                    init_thumb = 1;
                  } else {
                    JPEGDEC_API_TRC
                    ("JpegDecGetImageInfo# ERROR: Needs to increase input buffer");
                    return (JPEGDEC_INCREASE_INPUT_BUFFER);
                  }
                  break;
                case DQT:
                  /* DQT length */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR) {
                    error_code = 1;
                    break;
                  }
                  /* length >= (2 + 65) (baseline) */
                  if(header_length < (2 + 65)) {
                    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Thumbnail Incorrect DQT header length");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                    break;
                  }
                  /* jump over DQT header */
                  if(header_length != 0) {
                    stream.read_bits +=
                      ((header_length * 8) - 16);
                    stream.p_curr_pos +=
                      (((header_length * 8) - 16) / 8);
                  }
                  app_bits += (header_length * 8);
                  break;
                case DHT:
                  /* DHT length */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR) {
                    error_code = 1;
                    break;
                  }
                  /* length >= 2 + 17 */
                  if(header_length < 19) {
                    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Thumbnail Incorrect DHT header length");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                    break;
                  }
                  /* jump over DHT header */
                  if(header_length != 0) {
                    stream.read_bits +=
                      ((header_length * 8) - 16);
                    stream.p_curr_pos +=
                      (((header_length * 8) - 16) / 8);
                  }
                  app_bits += (header_length * 8);
                  break;
                case DRI:
                  /* DRI length */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR) {
                    error_code = 1;
                    break;
                  }
                  /* length == 4 */
                  if(header_length != 4) {
                    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Thumbnail Incorrect DRI header length");
                    p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
                    break;
                  }
                  /* jump over DRI header */
                  if(header_length != 0) {
                    stream.read_bits +=
                      ((header_length * 8) - 16);
                    stream.p_curr_pos +=
                      (((header_length * 8) - 16) / 8);
                  }
                  app_bits += (header_length * 8);
                  break;
                case APP0:
                case APP1:
                case APP2:
                case APP3:
                case APP4:
                case APP5:
                case APP6:
                case APP7:
                case APP8:
                case APP9:
                case APP10:
                case APP11:
                case APP12:
                case APP13:
                case APP14:
                case APP15:
                  /* APPn length */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR) {
                    error_code = 1;
                    break;
                  }
                  /* header_length > 2 */
                  if(header_length < 2)
                    break;
                  /* jump over APPn header */
                  if(header_length != 0) {
                    stream.read_bits +=
                      ((header_length * 8) - 16);
                    stream.p_curr_pos +=
                      (((header_length * 8) - 16) / 8);
                  }
                  app_bits += (header_length * 8);
                  break;
                case DNL:
                  /* DNL length */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR) {
                    error_code = 1;
                    break;
                  }
                  /* length == 4 */
                  if(header_length != 4)
                    break;
                  /* jump over DNL header */
                  if(header_length != 0) {
                    stream.read_bits +=
                      ((header_length * 8) - 16);
                    stream.p_curr_pos +=
                      (((header_length * 8) - 16) / 8);
                  }
                  app_bits += (header_length * 8);
                  break;
                case COM:
                  /* COM length */
                  header_length = JpegDecGet2Bytes(&(stream));
                  if(header_length == STRM_ERROR) {
                    error_code = 1;
                    break;
                  }
                  /* length > 2 */
                  if(header_length < 2)
                    break;
                  /* jump over COM header */
                  if(header_length != 0) {
                    stream.read_bits +=
                      ((header_length * 8) - 16);
                    stream.p_curr_pos +=
                      (((header_length * 8) - 16) / 8);
                  }
                  app_bits += (header_length * 8);
                  break;
                /* unsupported coding styles */
                case SOF1:
                case SOF3:
                case SOF5:
                case SOF6:
                case SOF7:
                case SOF9:
                case SOF10:
                case SOF11:
                case SOF13:
                case SOF14:
                case SOF15:
                case DAC:
                case DHP:
                  JPEGDEC_API_TRC
                  ("JpegDecGetImageInfo# ERROR: Unsupported coding styles");
                  return (JPEGDEC_UNSUPPORTED);
                default:
                  break;
                }
                if(PTR_JPGC->info.init_thumb && init_thumb) {
                  /* flush the rest of thumbnail data */
                  if(JpegDecFlushBits
                      (&(stream),
                       ((app_length * 8) - app_bits)) ==
                      STRM_ERROR) {
                    error_code = 1;
                    break;
                  }
                  stream.appn_flag = 0;
                  break;
                }
              } else {
                if(!PTR_JPGC->info.init_thumb &&
                    ((stream.read_bits + 8) >= (stream.stream_length * 8)) &&
                    p_dec_in->buffer_size)
                  return (JPEGDEC_INCREASE_INPUT_BUFFER);

                if(marker_byte == STRM_ERROR )
                  return (JPEGDEC_STRM_ERROR);
              }
            }
            if(!PTR_JPGC->info.init_thumb && !init_thumb) {
              JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Thumbnail contains no data");
              p_image_info->thumbnail_type = JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
            }
            break;
          } else {
            app_bits += 8;
            p_image_info->thumbnail_type =
              JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT;
            stream.appn_flag = 1;
            if(JpegDecFlushBits
                (&(stream),
                 ((app_length * 8) - app_bits)) == STRM_ERROR) {
              error_code = 1;
              break;
            }
            stream.appn_flag = 0;
            break;
          }
        } else {
          /* version */
          p_image_info->version = JpegDecGet2Bytes(&(stream));
          app_bits += 16;

          /* units */
          current_byte = JpegDecGetByte(&(stream));
          if(current_byte == 0) {
            p_image_info->units = JPEGDEC_NO_UNITS;
          } else if(current_byte == 1) {
            p_image_info->units = JPEGDEC_DOTS_PER_INCH;
          } else if(current_byte == 2) {
            p_image_info->units = JPEGDEC_DOTS_PER_CM;
          }
          app_bits += 8;

          /* Xdensity */
          p_image_info->x_density = JpegDecGet2Bytes(&(stream));
          app_bits += 16;

          /* Ydensity */
          p_image_info->y_density = JpegDecGet2Bytes(&(stream));
          app_bits += 16;

          /* jump over rest of header data */
          stream.appn_flag = 1;
          if(JpegDecFlushBits(&(stream), ((app_length * 8) - app_bits))
              == STRM_ERROR) {
            error_code = 1;
            break;
          }
          stream.appn_flag = 0;
          break;
        }
      case APP1:
      case APP2:
      case APP3:
      case APP4:
      case APP5:
      case APP6:
      case APP7:
      case APP8:
      case APP9:
      case APP10:
      case APP11:
      case APP12:
      case APP13:
      case APP14:
      case APP15:
        /* APPn length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        /* length > 2 */
        if(header_length < 2)
          break;
        /* jump over APPn header */
        if(header_length != 0) {
          stream.read_bits += ((header_length * 8) - 16);
          stream.p_curr_pos += (((header_length * 8) - 16) / 8);
        }
        break;
      case DNL:
        /* DNL length */
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        /* length == 4 */
        if(header_length != 4)
          break;
        /* jump over DNL header */
        if(header_length != 0) {
          stream.read_bits += ((header_length * 8) - 16);
          stream.p_curr_pos += (((header_length * 8) - 16) / 8);
        }
        break;
      case COM:
        header_length = JpegDecGet2Bytes(&(stream));
        if(header_length == STRM_ERROR ||
            ((stream.read_bits + ((header_length * 8) - 16)) >
             (8 * stream.stream_length))) {
          error_code = 1;
          break;
        }
        /* length > 2 */
        if(header_length < 2)
          break;
        /* jump over COM header */
        if(header_length != 0) {
          stream.read_bits += ((header_length * 8) - 16);
          stream.p_curr_pos += (((header_length * 8) - 16) / 8);
        }
        break;
      /* unsupported coding styles */
      case SOF1:
      case SOF3:
      case SOF5:
      case SOF6:
      case SOF7:
      case SOF9:
      case SOF10:
      case SOF11:
      case SOF13:
      case SOF14:
      case SOF15:
      case DAC:
      case DHP:
        JPEGDEC_API_TRC
        ("JpegDecGetImageInfo# ERROR: Unsupported coding styles");
        return (JPEGDEC_UNSUPPORTED);
      default:
        break;
      }
      if(PTR_JPGC->info.init && init) {
        if(!new_header_value) {
          PTR_JPGC->frame.hw_y = PTR_JPGC->frame.full_y = p_image_info->output_height;
          PTR_JPGC->frame.hw_x = PTR_JPGC->frame.full_x = p_image_info->output_width;
          /* restore output format */
          PTR_JPGC->info.y_cb_cr_mode = PTR_JPGC->info.get_info_ycb_cr_mode =
                                          p_image_info->output_format;
          if(thumbnail) {
            PTR_JPGC->frame.hw_ytn = p_image_info->output_height_thumb;
            PTR_JPGC->frame.hw_xtn = p_image_info->output_width_thumb;
            /* restore output format for thumb */
            PTR_JPGC->info.get_info_ycb_cr_mode_tn = p_image_info->output_format_thumb;
          }
          break;
        } else
          return (JPEGDEC_UNSUPPORTED);
      }

      if(error_code) {
        if(p_dec_in->buffer_size) {
          /* reset to ensure that big enough buffer will be allocated for decoding */
          if(new_header_value) {
            p_image_info->output_height = PTR_JPGC->frame.hw_y;
            p_image_info->output_width = PTR_JPGC->frame.hw_x;
            p_image_info->output_format = PTR_JPGC->info.get_info_ycb_cr_mode;
            if(thumbnail) {
              p_image_info->output_height_thumb = PTR_JPGC->frame.hw_ytn;
              p_image_info->output_width_thumb = PTR_JPGC->frame.hw_xtn;
              p_image_info->output_format_thumb = PTR_JPGC->info.get_info_ycb_cr_mode_tn;
            }
          }

          JPEGDEC_API_TRC
          ("JpegDecGetImageInfo# ERROR: Image info failed, Needs to increase input buffer");
          return (JPEGDEC_INCREASE_INPUT_BUFFER);
        } else {
          JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR: Stream error");
          return (JPEGDEC_STRM_ERROR);
        }
      }
    } else {
      if(!PTR_JPGC->info.init && (stream.read_bits + 8 >= (stream.stream_length * 8)) )
        return (JPEGDEC_INCREASE_INPUT_BUFFER);

      if(marker_byte == STRM_ERROR )
        return (JPEGDEC_STRM_ERROR);
    }
  }
  if(PTR_JPGC->info.init) {
    if(p_dec_in->buffer_size)
      PTR_JPGC->info.init_buffer_size = p_dec_in->buffer_size;

    JPEGDEC_API_TRC("JpegDecGetImageInfo# OK\n");
    return (JPEGDEC_OK);
  } else {
    JPEGDEC_API_TRC("JpegDecGetImageInfo# ERROR\n");
    return (JPEGDEC_ERROR);
  }

#undef PTR_JPGC
}

/*------------------------------------------------------------------------------

    Function name: JpegDecDecode

        Functional description:
            Decode JFIF

        Inputs:
            JpegDecInst dec_inst     jpeg decoder instance
            JpegDecInput *p_dec_in    pointer to structure where the decoder
                                    stores input information
            JpegDecOutput *p_dec_out  pointer to structure where the decoder
                                    stores output frame information

        Outputs:
            JPEGDEC_FRAME_READY
            JPEGDEC_PARAM_ERROR
            JPEGDEC_INVALID_STREAM_LENGTH
            JPEGDEC_INVALID_INPUT_BUFFER_SIZE
            JPEGDEC_UNSUPPORTED
            JPEGDEC_ERROR
            JPEGDEC_STRM_ERROR
            JPEGDEC_HW_BUS_ERROR
            JPEGDEC_DWL_HW_TIMEOUT
            JPEGDEC_HW_TIMEOUT
            JPEGDEC_SYSTEM_ERROR
            JPEGDEC_HW_RESERVED
            JPEGDEC_STRM_PROCESSED

  ------------------------------------------------------------------------------*/
JpegDecRet JpegDecDecode(JpegDecInst dec_inst, JpegDecInput * p_dec_in,
                         JpegDecOutput * p_dec_out) {

#define PTR_JPGC ((JpegDecContainer *) dec_inst)
#define JPG_FRM  PTR_JPGC->frame
#define JPG_INFO  PTR_JPGC->info

  i32 dwlret = -1;
  u32 i = 0;
  u32 current_byte = 0;
  u32 marker_byte = 0;
  u32 current_bytes = 0;
  u32 app_length = 0;
  u32 app_bits = 0;
  u32 asic_status = 0;
  u32 HINTdec = 0;
  u32 asic_slice_bit = 0;
  u32 int_dec = 0;
  addr_t current_pos = 0;
  u32 end_of_image = 0;
  u32 non_interleaved_rdy = 0;
  JpegDecRet info_ret;
  JpegDecRet ret_code; /* Returned code container */
  JpegDecImageInfo info_tmp;
  u32 mcu_size_divider = 0;
  u32 DHTfromStream = 0;

  JPEGDEC_API_TRC("JpegDecDecode#");

  /* check null */
  if(dec_inst == NULL || p_dec_in == NULL ||
      X170_CHECK_VIRTUAL_ADDRESS(p_dec_in->stream_buffer.virtual_address) ||
      X170_CHECK_BUS_ADDRESS(p_dec_in->stream_buffer.bus_address) ||
      p_dec_out == NULL) {
    JPEGDEC_API_TRC("JpegDecDecode# ERROR: NULL parameter");
    return (JPEGDEC_PARAM_ERROR);
  }

  /* check image decoding type */
  if(p_dec_in->dec_image_type != 0 && p_dec_in->dec_image_type != 1) {
    JPEGDEC_API_TRC("JpegDecDecode# ERROR: dec_image_type");
    return (JPEGDEC_PARAM_ERROR);
  }

  /* check user allocated null */
  if((p_dec_in->picture_buffer_y.virtual_address == NULL &&
      p_dec_in->picture_buffer_y.bus_address != 0) ||
      (p_dec_in->picture_buffer_y.virtual_address != NULL &&
       p_dec_in->picture_buffer_y.bus_address == 0) ||
      (p_dec_in->picture_buffer_cb_cr.virtual_address == NULL &&
       p_dec_in->picture_buffer_cb_cr.bus_address != 0) ||
      (p_dec_in->picture_buffer_cb_cr.virtual_address != NULL &&
       p_dec_in->picture_buffer_cb_cr.bus_address == 0)) {
    JPEGDEC_API_TRC("JpegDecDecode# ERROR: NULL parameter");
    return (JPEGDEC_PARAM_ERROR);
  }

  /* Check the stream lenth */
  if(p_dec_in->stream_length < 1) {
    JPEGDEC_API_TRC("JpegDecDecode# ERROR: p_dec_in->stream_length");
    return (JPEGDEC_INVALID_STREAM_LENGTH);
  }

  /* check the input buffer settings ==>
   * checks are discarded for last buffer */
  if(!PTR_JPGC->info.stream_end_flag) {
    /* Check the stream lenth */
    if(!PTR_JPGC->info.input_buffer_empty &&
        (p_dec_in->stream_length > DEC_X170_MAX_STREAM) &&
        (p_dec_in->buffer_size < JPEGDEC_X170_MIN_BUFFER ||
         p_dec_in->buffer_size > JPEGDEC_X170_MAX_BUFFER)) {
      JPEGDEC_API_TRC("JpegDecDecode# ERROR: p_dec_in->buffer_size");
      return (JPEGDEC_INVALID_INPUT_BUFFER_SIZE);
    }

    /* Check the stream buffer size */
    if(!PTR_JPGC->info.input_buffer_empty &&
        p_dec_in->buffer_size && (p_dec_in->buffer_size < JPEGDEC_X170_MIN_BUFFER
                                  || p_dec_in->buffer_size >
                                  JPEGDEC_X170_MAX_BUFFER)) {
      JPEGDEC_API_TRC("JpegDecDecode# ERROR: p_dec_in->buffer_size");
      return (JPEGDEC_INVALID_INPUT_BUFFER_SIZE);
    }

    /* Check the stream buffer size */
    if(!PTR_JPGC->info.input_buffer_empty &&
        p_dec_in->buffer_size && ((p_dec_in->buffer_size % 256) != 0)) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: p_dec_in->buffer_size % 256) != 0");
      return (JPEGDEC_INVALID_INPUT_BUFFER_SIZE);
    }

    if(!PTR_JPGC->info.input_buffer_empty &&
        PTR_JPGC->info.init &&
        (p_dec_in->buffer_size < PTR_JPGC->info.init_buffer_size)) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: Increase input buffer size!\n");
      return (JPEGDEC_INVALID_INPUT_BUFFER_SIZE);
    }
  }

  if(!PTR_JPGC->info.init && !PTR_JPGC->info.SliceReadyForPause &&
      !PTR_JPGC->info.input_buffer_empty) {
    JPEGDEC_API_TRC(("JpegDecDecode#: Get Image Info!\n"));

    info_ret = JpegDecGetImageInfo(dec_inst, p_dec_in, &info_tmp);

    if(info_ret != JPEGDEC_OK) {
      JPEGDEC_API_TRC(("JpegDecDecode# ERROR: Image info failed!"));
      return (info_ret);
    }
  }

  /* Store the stream parameters */
  if(PTR_JPGC->info.progressive_scan_ready == 0 &&
      PTR_JPGC->info.non_interleaved_scan_ready == 0) {
    PTR_JPGC->stream.bit_pos_in_byte = 0;
    PTR_JPGC->stream.p_curr_pos = (u8 *) p_dec_in->stream_buffer.virtual_address;
    PTR_JPGC->stream.stream_bus = p_dec_in->stream_buffer.bus_address;
    PTR_JPGC->stream.p_start_of_stream =
      (u8 *) p_dec_in->stream_buffer.virtual_address;
    PTR_JPGC->stream.read_bits = 0;
    PTR_JPGC->stream.stream_length = p_dec_in->stream_length;
    PTR_JPGC->stream.appn_flag = 0;
    p_dec_out->output_picture_y.virtual_address = NULL;
    p_dec_out->output_picture_y.bus_address = 0;
    p_dec_out->output_picture_cb_cr.virtual_address = NULL;
    p_dec_out->output_picture_cb_cr.bus_address = 0;
    p_dec_out->output_picture_cr.virtual_address = NULL;
    p_dec_out->output_picture_cr.bus_address = 0;
  } else {
    PTR_JPGC->image.header_ready = 0;
  }

  /* set mcu/slice value */
  PTR_JPGC->info.slice_mb_set_value = p_dec_in->slice_mb_set;

  /* check HW supported features */
  if(!PTR_JPGC->is8190) {
    /* return if not valid HW and unsupported operation type */
    if(PTR_JPGC->info.operation_type == JPEGDEC_NONINTERLEAVED ||
        PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: Operation type not supported");
      return (JPEGDEC_UNSUPPORTED);
    }

    if(PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr400 ||
        PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr440 ||
        PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr444_SEMIPLANAR)
      mcu_size_divider = 2;
    else
      mcu_size_divider = 1;

    /* check slice config */
    if((p_dec_in->slice_mb_set * (JPG_FRM.num_mcu_in_row / mcu_size_divider)) >
        PTR_JPGC->max_supported_slice_size) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: slice_mb_set  > JPEGDEC_MAX_SLICE_SIZE");
      return (JPEGDEC_PARAM_ERROR);
    }

    /* check frame size */
    if((!p_dec_in->slice_mb_set) &&
        JPG_FRM.num_mcu_in_frame > PTR_JPGC->max_supported_slice_size) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: mcu_in_frame > JPEGDEC_MAX_SLICE_SIZE");
      return (JPEGDEC_PARAM_ERROR);
    }
  } else {
    /* check if fuse was burned */
    if(PTR_JPGC->fuse_burned) {
      /* return if not valid HW and unsupported operation type */
      if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE) {
        JPEGDEC_API_TRC
        ("JpegDecDecode# ERROR: Operation type not supported");
        return (JPEGDEC_UNSUPPORTED);
      }
    }

    /* check slice config */
    if((p_dec_in->slice_mb_set && p_dec_in->dec_image_type == JPEGDEC_IMAGE &&
        PTR_JPGC->info.operation_type != JPEGDEC_BASELINE) ||
        (p_dec_in->slice_mb_set && p_dec_in->dec_image_type == JPEGDEC_THUMBNAIL &&
         PTR_JPGC->info.operation_type_thumb != JPEGDEC_BASELINE)) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: Slice mode not supported for this operation type");
      return (JPEGDEC_SLICE_MODE_UNSUPPORTED);
    }

    /* check if frame size over 16M */
    if((!p_dec_in->slice_mb_set) &&
        ((JPG_FRM.hw_x * JPG_FRM.hw_y) > JPEGDEC_MAX_PIXEL_AMOUNT)) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: Resolution > 16M ==> use slice mode!");
      return (JPEGDEC_PARAM_ERROR);
    }

    if(PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr400 ||
        PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr440 ||
        PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr444_SEMIPLANAR)
      mcu_size_divider = 2;
    else
      mcu_size_divider = 1;

    /* check slice config */
    if((p_dec_in->slice_mb_set * (JPG_FRM.num_mcu_in_row / mcu_size_divider)) >
        PTR_JPGC->max_supported_slice_size) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: slice_mb_set  > JPEGDEC_MAX_SLICE_SIZE");
      return (JPEGDEC_PARAM_ERROR);
    }
  }

  if(p_dec_in->slice_mb_set > 255) {
    JPEGDEC_API_TRC
    ("JpegDecDecode# ERROR: slice_mb_set  > Maximum slice size");
    return (JPEGDEC_PARAM_ERROR);
  }

  /* check slice size */
  if(p_dec_in->slice_mb_set &&
      !PTR_JPGC->info.SliceReadyForPause && !PTR_JPGC->info.input_buffer_empty) {
    if(PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr400 ||
        PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr440 ||
        PTR_JPGC->info.get_info_ycb_cr_mode == JPEGDEC_YCbCr444_SEMIPLANAR)
      mcu_size_divider = 2;
    else
      mcu_size_divider = 1;

    if((p_dec_in->slice_mb_set * (JPG_FRM.num_mcu_in_row / mcu_size_divider)) >
        JPG_FRM.num_mcu_in_frame) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# ERROR: (slice_mb_set * Number of MCU's in row) > Number of MCU's in frame");
      return (JPEGDEC_PARAM_ERROR);
    }
  }

  /* Handle stream/hw parameters after buffer empty */
  if(PTR_JPGC->info.input_buffer_empty) {
    /* Store the stream parameters */
    PTR_JPGC->stream.bit_pos_in_byte = 0;
    PTR_JPGC->stream.p_curr_pos = (u8 *) p_dec_in->stream_buffer.virtual_address;
    PTR_JPGC->stream.stream_bus = p_dec_in->stream_buffer.bus_address;
    PTR_JPGC->stream.p_start_of_stream =
      (u8 *) p_dec_in->stream_buffer.virtual_address;

    /* update hw parameters */
    PTR_JPGC->info.input_streaming = 1;
    if(p_dec_in->buffer_size)
      PTR_JPGC->info.input_buffer_len = p_dec_in->buffer_size;
    else
      PTR_JPGC->info.input_buffer_len = p_dec_in->stream_length;

    /* decoded stream */
    PTR_JPGC->info.decoded_stream_len += PTR_JPGC->info.input_buffer_len;

    if(PTR_JPGC->info.decoded_stream_len > p_dec_in->stream_length) {
      JPEGDEC_API_TRC
      ("JpegDecDecode# Error: All input stream already processed");
      return JPEGDEC_STRM_ERROR;
    }
  }

  /* update user allocated output */
  PTR_JPGC->info.given_out_luma.virtual_address =
    p_dec_in->picture_buffer_y.virtual_address;
  PTR_JPGC->info.given_out_luma.bus_address = p_dec_in->picture_buffer_y.bus_address;
  PTR_JPGC->info.given_out_chroma.virtual_address =
    p_dec_in->picture_buffer_cb_cr.virtual_address;
  PTR_JPGC->info.given_out_chroma.bus_address =
    p_dec_in->picture_buffer_cb_cr.bus_address;
  PTR_JPGC->info.given_out_chroma2.virtual_address =
    p_dec_in->picture_buffer_cr.virtual_address;
  PTR_JPGC->info.given_out_chroma2.bus_address =
    p_dec_in->picture_buffer_cr.bus_address;

  if(PTR_JPGC->info.progressive_finish) {
    /* output set */
    if(PTR_JPGC->pp_instance == NULL) {
      p_dec_out->output_picture_y.virtual_address =
        PTR_JPGC->info.out_luma.virtual_address;
      ASSERT(p_dec_out->output_picture_y.virtual_address);

      /* output set */
      p_dec_out->output_picture_y.bus_address =
        PTR_JPGC->info.out_luma.bus_address;
      ASSERT(p_dec_out->output_picture_y.bus_address);

      /* if not grayscale */
      if(PTR_JPGC->image.size_chroma) {
        p_dec_out->output_picture_cb_cr.virtual_address =
          PTR_JPGC->info.out_chroma.virtual_address;
        ASSERT(p_dec_out->output_picture_cb_cr.virtual_address);

        p_dec_out->output_picture_cb_cr.bus_address =
          PTR_JPGC->info.out_chroma.bus_address;
        ASSERT(p_dec_out->output_picture_cb_cr.bus_address);

        p_dec_out->output_picture_cr.virtual_address =
          PTR_JPGC->info.out_chroma2.virtual_address;
        p_dec_out->output_picture_cr.bus_address =
          PTR_JPGC->info.out_chroma2.bus_address;
      }
    }
    JpegDecInitHWEmptyScan(PTR_JPGC, PTR_JPGC->info.pf_comp_id);
    dwlret = DWLWaitHwReady(PTR_JPGC->dwl, PTR_JPGC->core_id, PTR_JPGC->info.timeout);
    ASSERT(dwlret == DWL_HW_WAIT_OK);
    JpegRefreshRegs(PTR_JPGC);
    asic_status = GetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_IRQ_STAT);
    ASSERT(asic_status == 1);

    i = PTR_JPGC->info.pf_comp_id + 1;
    while(i < 3 && PTR_JPGC->info.pf_needed[i] == 0)
      i++;
    if(i == 3) {
      PTR_JPGC->info.progressive_finish = 0;
      return (JPEGDEC_FRAME_READY);
    } else {
      PTR_JPGC->info.pf_comp_id = i;
      return (JPEGDEC_SCAN_PROCESSED);
    }
  }

  /* check if input streaming used */
  if(!PTR_JPGC->info.SliceReadyForPause &&
      !PTR_JPGC->info.input_buffer_empty && p_dec_in->buffer_size) {
    PTR_JPGC->info.input_streaming = 1;
    PTR_JPGC->info.input_buffer_len = p_dec_in->buffer_size;
    PTR_JPGC->info.decoded_stream_len += PTR_JPGC->info.input_buffer_len;
  }

  /* find markers and go ! */
  do {
    /* if slice mode/slice done return to hw handling */
    if(PTR_JPGC->image.header_ready && PTR_JPGC->info.SliceReadyForPause)
      break;

    /* Look for marker prefix byte from stream */
    marker_byte = JpegDecGetByte(&(PTR_JPGC->stream));
    if(marker_byte == 0xFF) {
      current_byte = JpegDecGetByte(&(PTR_JPGC->stream));

      /* switch to certain header decoding */
      switch (current_byte) {
      case 0x00:
        break;
      case SOF0:
      case SOF2:
        JPEGDEC_API_TRC("JpegDecDecode# JpegDecDecodeFrameHdr decode");
        /* Baseline/Progressive */
        PTR_JPGC->frame.coding_type = current_byte;
        /* Set operation type */
        if(PTR_JPGC->frame.coding_type == SOF0)
          PTR_JPGC->info.operation_type = JPEGDEC_BASELINE;
        else
          PTR_JPGC->info.operation_type = JPEGDEC_PROGRESSIVE;

        ret_code = JpegDecDecodeFrameHdr(PTR_JPGC);
        if(ret_code != JPEGDEC_OK) {
          if(ret_code == JPEGDEC_STRM_ERROR) {
            JPEGDEC_API_TRC("JpegDecDecode# ERROR: Stream error");
            return (ret_code);
          } else {
            JPEGDEC_API_TRC
            ("JpegDecDecode# ERROR: JpegDecDecodeFrameHdr err");
            return (ret_code);
          }
        }
        break;
      /* Start of Scan */
      case SOS:
        /* reset image ready */
        PTR_JPGC->image.image_ready = 0;
        JPEGDEC_API_TRC("JpegDecDecode# JpegDecDecodeScan dec");

        ret_code = JpegDecDecodeScan(PTR_JPGC);
        PTR_JPGC->image.header_ready = 1;
        if(ret_code != JPEGDEC_OK) {
          if(ret_code == JPEGDEC_STRM_ERROR) {
            JPEGDEC_API_TRC("JpegDecDecode# ERROR: Stream error");
            return (ret_code);
          } else {
            JPEGDEC_API_TRC
            ("JpegDecDecode# JpegDecDecodeScan err\n");
            return (ret_code);
          }
        }

        if(PTR_JPGC->stream.bit_pos_in_byte) {
          /* delete stuffing bits */
          current_byte = (8 - PTR_JPGC->stream.bit_pos_in_byte);
          if(JpegDecFlushBits
              (&(PTR_JPGC->stream),
               8 - PTR_JPGC->stream.bit_pos_in_byte) == STRM_ERROR) {
            JPEGDEC_API_TRC("JpegDecDecode# ERROR: Stream error");
            return (JPEGDEC_STRM_ERROR);
          }
        }
        JPEGDEC_API_TRC("JpegDecDecode# Stuffing bits deleted\n");
        break;
      /* Start of Huffman tables */
      case DHT:
        JPEGDEC_API_TRC
        ("JpegDecDecode# JpegDecDecodeHuffmanTables dec");
        ret_code = JpegDecDecodeHuffmanTables(PTR_JPGC);
        JPEGDEC_API_TRC
        ("JpegDecDecode# JpegDecDecodeHuffmanTables stops");
        if(ret_code != JPEGDEC_OK) {
          if(ret_code == JPEGDEC_STRM_ERROR) {
            JPEGDEC_API_TRC("JpegDecDecode# ERROR: Stream error");
            return (ret_code);
          } else {
            JPEGDEC_API_TRC
            ("JpegDecDecode# ERROR: JpegDecDecodeHuffmanTables err");
            return (ret_code);
          }
        }
        /* mark DHT got from stream */
        DHTfromStream = 1;
        break;
      /* start of Quantisation Tables */
      case DQT:
        JPEGDEC_API_TRC("JpegDecDecode# JpegDecDecodeQuantTables dec");
        ret_code = JpegDecDecodeQuantTables(PTR_JPGC);
        if(ret_code != JPEGDEC_OK) {
          if(ret_code == JPEGDEC_STRM_ERROR) {
            JPEGDEC_API_TRC("JpegDecDecode# ERROR: Stream error");
            return (ret_code);
          } else {
            JPEGDEC_API_TRC
            ("JpegDecDecode# ERROR: JpegDecDecodeQuantTables err");
            return (ret_code);
          }
        }
        break;
      /* Start of Image */
      case SOI:
        /* no actions needed, continue */
        break;
      /* End of Image */
      case EOI:
        if(PTR_JPGC->image.image_ready) {
          JPEGDEC_API_TRC("JpegDecDecode# EOI: OK\n");
          return (JPEGDEC_FRAME_READY);
        } else {
          JPEGDEC_API_TRC("JpegDecDecode# ERROR: EOI: NOK\n");
          return (JPEGDEC_ERROR);
        }
      /* Define Restart Interval */
      case DRI:
        JPEGDEC_API_TRC("JpegDecDecode# DRI");
        current_bytes = JpegDecGet2Bytes(&(PTR_JPGC->stream));
        if(current_bytes == STRM_ERROR) {
          JPEGDEC_API_TRC("JpegDecDecode# ERROR: Read bits ");
          return (JPEGDEC_STRM_ERROR);
        }
        PTR_JPGC->frame.Ri = JpegDecGet2Bytes(&(PTR_JPGC->stream));
        break;
      /* Restart with modulo 8 count m */
      case RST0:
      case RST1:
      case RST2:
      case RST3:
      case RST4:
      case RST5:
      case RST6:
      case RST7:
        /* initialisation of DC predictors to zero value !!! */
        for(i = 0; i < MAX_NUMBER_OF_COMPONENTS; i++) {
          PTR_JPGC->scan.pred[i] = 0;
        }
        JPEGDEC_API_TRC("JpegDecDecode# DC predictors init");
        break;
      /* unsupported features */
      case SOF1:
      case SOF3:
      case SOF5:
      case SOF6:
      case SOF7:
      case SOF9:
      case SOF10:
      case SOF11:
      case SOF13:
      case SOF14:
      case SOF15:
      case DAC:
      case DHP:
      case TEM:
        JPEGDEC_API_TRC("JpegDecDecode# ERROR: Unsupported Features");
        return (JPEGDEC_UNSUPPORTED);
      /* application data & comments */
      case APP0:
        JPEGDEC_API_TRC("JpegDecDecode# APP0 in Decode");
        /* APP0 Extended Thumbnail */
        if(p_dec_in->dec_image_type == JPEGDEC_THUMBNAIL) {
          /* reset */
          app_bits = 0;
          app_length = 0;

          /* length */
          app_length = JpegDecGet2Bytes(&(PTR_JPGC->stream));
          app_bits += 16;

          /* length > 2 */
          if(app_length < 2)
            break;

          /* check identifier */
          current_bytes = JpegDecGet2Bytes(&(PTR_JPGC->stream));
          app_bits += 16;
          if(current_bytes != 0x4A46) {
            PTR_JPGC->stream.appn_flag = 1;
            if(JpegDecFlushBits
                (&(PTR_JPGC->stream),
                 ((app_length * 8) - app_bits)) == STRM_ERROR) {
              JPEGDEC_API_TRC
              ("JpegDecDecode# ERROR: Stream error");
              return (JPEGDEC_STRM_ERROR);
            }
            PTR_JPGC->stream.appn_flag = 0;
            break;
          }
          current_bytes = JpegDecGet2Bytes(&(PTR_JPGC->stream));
          app_bits += 16;
          if(current_bytes != 0x5858) {
            PTR_JPGC->stream.appn_flag = 1;
            if(JpegDecFlushBits
                (&(PTR_JPGC->stream),
                 ((app_length * 8) - app_bits)) == STRM_ERROR) {
              JPEGDEC_API_TRC
              ("JpegDecDecode# ERROR: Stream error");
              return (JPEGDEC_STRM_ERROR);
            }
            PTR_JPGC->stream.appn_flag = 0;
            break;
          }
          current_byte = JpegDecGetByte(&(PTR_JPGC->stream));
          app_bits += 8;
          if(current_byte != 0x00) {
            PTR_JPGC->stream.appn_flag = 1;
            if(JpegDecFlushBits
                (&(PTR_JPGC->stream),
                 ((app_length * 8) - app_bits)) == STRM_ERROR) {
              JPEGDEC_API_TRC
              ("JpegDecDecode# ERROR: Stream error");
              return (JPEGDEC_STRM_ERROR);
            }
            PTR_JPGC->stream.appn_flag = 0;
            break;
          }
          /* extension code */
          current_byte = JpegDecGetByte(&(PTR_JPGC->stream));
          PTR_JPGC->stream.appn_flag = 0;
          if(current_byte != JPEGDEC_THUMBNAIL_JPEG) {
            JPEGDEC_API_TRC(("JpegDecDecode# ERROR: thumbnail unsupported"));
            return (JPEGDEC_UNSUPPORTED);
          }
          /* thumbnail mode */
          JPEGDEC_API_TRC("JpegDecDecode# Thumbnail data ok!");
          PTR_JPGC->stream.thumbnail = 1;
          break;
        } else {
          /* Flush unsupported thumbnail */
          current_bytes = JpegDecGet2Bytes(&(PTR_JPGC->stream));

          /* length > 2 */
          if(current_bytes < 2)
            break;

          PTR_JPGC->stream.appn_flag = 1;
          if(JpegDecFlushBits
              (&(PTR_JPGC->stream),
               ((current_bytes - 2) * 8)) == STRM_ERROR) {
            JPEGDEC_API_TRC("JpegDecDecode# ERROR: Stream error");
            return (JPEGDEC_STRM_ERROR);
          }
          PTR_JPGC->stream.appn_flag = 0;
          break;
        }
      case DNL:
        JPEGDEC_API_TRC("JpegDecDecode# DNL ==> flush");
        break;
      case APP1:
      case APP2:
      case APP3:
      case APP4:
      case APP5:
      case APP6:
      case APP7:
      case APP8:
      case APP9:
      case APP10:
      case APP11:
      case APP12:
      case APP13:
      case APP14:
      case APP15:
      case COM:
        JPEGDEC_API_TRC("JpegDecDecode# COM");
        current_bytes = JpegDecGet2Bytes(&(PTR_JPGC->stream));
        if(current_bytes == STRM_ERROR) {
          JPEGDEC_API_TRC("JpegDecDecode# ERROR: Read bits ");
          return (JPEGDEC_STRM_ERROR);
        }
        /* length > 2 */
        if(current_bytes < 2)
          break;
        /* jump over not supported header */
        if(current_bytes != 0) {
          PTR_JPGC->stream.read_bits += ((current_bytes * 8) - 16);
          PTR_JPGC->stream.p_curr_pos +=
            (((current_bytes * 8) - 16) / 8);
        }
        break;
      default:
        break;
      }
    } else {
      if(marker_byte == 0xFFFFFFFF) {
        break;
      }
    }

    if(PTR_JPGC->image.header_ready)
      break;
  } while((PTR_JPGC->stream.read_bits >> 3) <= PTR_JPGC->stream.stream_length);

  ret_code = JPEGDEC_OK;

  /* check if no DHT in stream and if already loaded (MJPEG) */
  if(!DHTfromStream && !PTR_JPGC->vlc.default_tables) {
    JPEGDEC_API_TRC("JpegDecDecode# No DHT tables in stream, use tables defined in JPEG Standard\n");
    /* use default tables defined in standard */
    JpegDecDefaultHuffmanTables(PTR_JPGC);
  }

  /* Handle decoded image here */
  if(PTR_JPGC->image.header_ready) {
    /* loop until decoding control should return for user */
    do {
      /* if pp enabled ==> set pp control */
      if(PTR_JPGC->pp_instance != NULL) {
        PTR_JPGC->pp_config_query.tiled_mode = 0;
        PTR_JPGC->PPConfigQuery(PTR_JPGC->pp_instance,
                                &PTR_JPGC->pp_config_query);

        PTR_JPGC->pp_control.use_pipeline =
          PTR_JPGC->pp_config_query.pipeline_accepted;

        /* set pp for combined mode */
        if(PTR_JPGC->pp_control.use_pipeline)
          JpegDecPreparePp(PTR_JPGC);
      }

      /* check if we had to load imput buffer or not */
      if(!PTR_JPGC->info.input_buffer_empty) {
        /* if slice mode ==> set slice height */
        if(PTR_JPGC->info.slice_mb_set_value &&
            PTR_JPGC->pp_control.use_pipeline == 0) {
          JpegDecSliceSizeCalculation(PTR_JPGC);
        }

        /* Start HW or continue after pause */
        if(!PTR_JPGC->info.SliceReadyForPause) {
          if(!PTR_JPGC->info.progressive_scan_ready ||
              PTR_JPGC->info.non_interleaved_scan_ready) {
            JPEGDEC_API_TRC("JpegDecDecode# Start HW init\n");
            ret_code = JpegDecInitHW(PTR_JPGC);
            PTR_JPGC->info.non_interleaved_scan_ready = 0;
            if(ret_code != JPEGDEC_OK) {
              /* return JPEGDEC_HW_RESERVED */
              return ret_code;
            }

          } else {
            JPEGDEC_API_TRC
            ("JpegDecDecode# Continue HW decoding after progressive scan ready\n");
            JpegDecInitHWProgressiveContinue(PTR_JPGC);
            PTR_JPGC->info.progressive_scan_ready = 0;

          }
        } else {
          JPEGDEC_API_TRC
          ("JpegDecDecode# Continue HW decoding after slice ready\n");
          JpegDecInitHWContinue(PTR_JPGC);
        }

        PTR_JPGC->info.SliceCount++;
      } else {
        JPEGDEC_API_TRC
        ("JpegDecDecode# Continue HW decoding after input buffer has been loaded\n");
        JpegDecInitHWInputBuffLoad(PTR_JPGC);

        /* buffer loaded ==> reset flag */
        PTR_JPGC->info.input_buffer_empty = 0;
      }

#ifdef JPEGDEC_PERFORMANCE
      dwlret = DWL_HW_WAIT_OK;
#else
      /* wait hw ready */
      dwlret = DWLWaitHwReady(PTR_JPGC->dwl, PTR_JPGC->core_id,
                              PTR_JPGC->info.timeout);
#endif /* #ifdef JPEGDEC_PERFORMANCE */

      /* Refresh regs */
      JpegRefreshRegs(PTR_JPGC);

      if(dwlret == DWL_HW_WAIT_OK) {
        JPEGDEC_API_TRC("JpegDecDecode# DWL_HW_WAIT_OK");

#ifdef JPEGDEC_ASIC_TRACE
        {
          JPEGDEC_TRACE_INTERNAL(("\nJpeg_dec_decode# AFTER DWL_HW_WAIT_OK\n"));
          PrintJPEGReg(PTR_JPGC->jpeg_regs);
        }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

        /* check && reset status */
        asic_status =
          GetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_IRQ_STAT);

        if(asic_status & JPEGDEC_X170_IRQ_BUS_ERROR) {
          /* check PP status... and go */
          if(PTR_JPGC->pp_instance != NULL) {
            /* End PP co-operation */
            if(PTR_JPGC->pp_control.pp_status == DECPP_RUNNING) {
              JPEGDEC_API_TRC("JpegDecDecode# PP END CALL");
              PTR_JPGC->PPEndCallback(PTR_JPGC->pp_instance);
              PTR_JPGC->pp_control.pp_status = DECPP_PIC_READY;
            }
          }

          JPEGDEC_API_TRC
          ("JpegDecDecode# JPEGDEC_X170_IRQ_BUS_ERROR");
          /* clear interrupts */
          JPEGDEC_CLEAR_IRQ;

          SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_E, 0);
          DWLDisableHw(PTR_JPGC->dwl, PTR_JPGC->core_id, 4 * 1,
                       PTR_JPGC->jpeg_regs[1]);

          /* Release HW */
          (void) DWLReleaseHw(PTR_JPGC->dwl, PTR_JPGC->core_id);

          /* update asic_running */
          PTR_JPGC->asic_running = 0;

          return JPEGDEC_HW_BUS_ERROR;
        } else if(asic_status & JPEGDEC_X170_IRQ_STREAM_ERROR ||
                  asic_status & JPEGDEC_X170_IRQ_TIMEOUT ||
                  asic_status & DEC_8190_IRQ_ABORT ) {
          /* check PP status... and go */
          if(PTR_JPGC->pp_instance != NULL) {
            /* End PP co-operation */
            if(PTR_JPGC->pp_control.pp_status == DECPP_RUNNING) {
              JPEGDEC_API_TRC("JpegDecDecode# PP END CALL");
              PTR_JPGC->PPEndCallback(PTR_JPGC->pp_instance);
            }

            PTR_JPGC->pp_control.pp_status = DECPP_PIC_READY;
          }

          if(asic_status & JPEGDEC_X170_IRQ_STREAM_ERROR) {
            JPEGDEC_API_TRC
            ("JpegDecDecode# JPEGDEC_X170_IRQ_STREAM_ERROR");
          } else if(asic_status & JPEGDEC_X170_IRQ_TIMEOUT) {
            JPEGDEC_API_TRC
            ("JpegDecDecode# JPEGDEC_X170_IRQ_TIMEOUT");
          } else {
            JPEGDEC_API_TRC
            ("JpegDecDecode# JPEGDEC_X170_IRQ_ABORT");
          }

          /* clear interrupts */
          JPEGDEC_CLEAR_IRQ;

          SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_E, 0);
          DWLDisableHw(PTR_JPGC->dwl, PTR_JPGC->core_id, 4 * 1,
                       PTR_JPGC->jpeg_regs[1]);

          /* Release HW */
          (void) DWLReleaseHw(PTR_JPGC->dwl, PTR_JPGC->core_id);

          /* update asic_running */
          PTR_JPGC->asic_running = 0;

          /* output set */
          if(PTR_JPGC->pp_instance == NULL) {
            p_dec_out->output_picture_y.virtual_address =
              PTR_JPGC->info.out_luma.virtual_address;
            ASSERT(p_dec_out->output_picture_y.virtual_address);

            /* output set */
            p_dec_out->output_picture_y.bus_address =
              PTR_JPGC->info.out_luma.bus_address;
            ASSERT(p_dec_out->output_picture_y.bus_address);

            /* if not grayscale */
            if(PTR_JPGC->image.size_chroma) {
              p_dec_out->output_picture_cb_cr.virtual_address =
                PTR_JPGC->info.out_chroma.virtual_address;
              ASSERT(p_dec_out->output_picture_cb_cr.virtual_address);

              p_dec_out->output_picture_cb_cr.bus_address =
                PTR_JPGC->info.out_chroma.bus_address;
              ASSERT(p_dec_out->output_picture_cb_cr.bus_address);

              p_dec_out->output_picture_cr.virtual_address =
                PTR_JPGC->info.out_chroma2.virtual_address;
              p_dec_out->output_picture_cr.bus_address =
                PTR_JPGC->info.out_chroma2.bus_address;
            }
          }

          /* reset */
          JpegDecClearStructs(dec_inst, 1);

          if (asic_status & JPEGDEC_X170_IRQ_TIMEOUT)
            return JPEGDEC_HW_TIMEOUT;
          else
            return JPEGDEC_STRM_ERROR;
        } else if(asic_status & JPEGDEC_X170_IRQ_BUFFER_EMPTY) {
          /* check if frame is ready */
          if(!(asic_status & JPEGDEC_X170_IRQ_DEC_RDY)) {
            JPEGDEC_API_TRC
            ("JpegDecDecode# JPEGDEC_X170_IRQ_BUFFER_EMPTY/STREAM PROCESSED");

            /* clear interrupts */
            SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_BUFFER_INT,
                           0);

            /* flag to load buffer */
            PTR_JPGC->info.input_buffer_empty = 1;

            /* check if all stream should be processed with the
             * next buffer ==> may affect to some API checks */
            if((PTR_JPGC->info.decoded_stream_len +
                PTR_JPGC->info.input_buffer_len) >=
                PTR_JPGC->stream.stream_length) {
              PTR_JPGC->info.stream_end_flag = 1;
            }

            /* output set */
            if(PTR_JPGC->pp_instance == NULL) {
              p_dec_out->output_picture_y.virtual_address =
                PTR_JPGC->info.out_luma.virtual_address;
              ASSERT(p_dec_out->output_picture_y.virtual_address);

              /* output set */
              p_dec_out->output_picture_y.bus_address =
                PTR_JPGC->info.out_luma.bus_address;
              ASSERT(p_dec_out->output_picture_y.bus_address);

              /* if not grayscale */
              if(PTR_JPGC->image.size_chroma) {
                p_dec_out->output_picture_cb_cr.virtual_address =
                  PTR_JPGC->info.out_chroma.virtual_address;
                ASSERT(p_dec_out->output_picture_cb_cr.
                       virtual_address);

                p_dec_out->output_picture_cb_cr.bus_address =
                  PTR_JPGC->info.out_chroma.bus_address;
                ASSERT(p_dec_out->output_picture_cb_cr.bus_address);

                p_dec_out->output_picture_cr.virtual_address =
                  PTR_JPGC->info.out_chroma2.virtual_address;
                p_dec_out->output_picture_cr.bus_address =
                  PTR_JPGC->info.out_chroma2.bus_address;
              }
            }

            return JPEGDEC_STRM_PROCESSED;
          }
        }

        SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_RDY_INT, 0);
        HINTdec = GetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_IRQ);
        if(HINTdec) {
          JPEGDEC_API_TRC("JpegDecDecode# CLEAR interrupt");
          SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_IRQ, 0);
        }

        /* check if slice ready */
        asic_slice_bit = GetDecRegister(PTR_JPGC->jpeg_regs,
                                        HWIF_JPEG_SLICE_H);
        int_dec = GetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_SLICE_INT);

        /* slice ready ==> reset interrupt */
        if(asic_slice_bit && int_dec) {
          /* if x170 pp not in use */
          if(PTR_JPGC->pp_instance == NULL)
            PTR_JPGC->info.SliceReadyForPause = 1;
          else
            PTR_JPGC->info.SliceReadyForPause = 0;

          if(PTR_JPGC->pp_instance != NULL &&
              !PTR_JPGC->pp_control.use_pipeline) {
            PTR_JPGC->info.SliceReadyForPause = 1;
          }

          /* if user allocated memory return given base */
          if(PTR_JPGC->info.user_alloc_mem == 1 &&
              PTR_JPGC->info.SliceReadyForPause == 1) {
            /* output addresses */
            p_dec_out->output_picture_y.virtual_address =
              PTR_JPGC->info.given_out_luma.virtual_address;
            p_dec_out->output_picture_y.bus_address =
              PTR_JPGC->info.given_out_luma.bus_address;
            if(PTR_JPGC->image.size_chroma) {
              p_dec_out->output_picture_cb_cr.virtual_address =
                PTR_JPGC->info.given_out_chroma.virtual_address;
              p_dec_out->output_picture_cb_cr.bus_address =
                PTR_JPGC->info.given_out_chroma.bus_address;
              p_dec_out->output_picture_cr.virtual_address =
                PTR_JPGC->info.given_out_chroma2.virtual_address;
              p_dec_out->output_picture_cr.bus_address =
                PTR_JPGC->info.given_out_chroma2.bus_address;
            }
          }

          /* if not user allocated memory return slice base */
          if(PTR_JPGC->info.user_alloc_mem == 0 &&
              PTR_JPGC->info.SliceReadyForPause == 1) {
            /* output addresses */
            p_dec_out->output_picture_y.virtual_address =
              PTR_JPGC->info.out_luma.virtual_address;
            p_dec_out->output_picture_y.bus_address =
              PTR_JPGC->info.out_luma.bus_address;
            if(PTR_JPGC->image.size_chroma) {
              p_dec_out->output_picture_cb_cr.virtual_address =
                PTR_JPGC->info.out_chroma.virtual_address;
              p_dec_out->output_picture_cb_cr.bus_address =
                PTR_JPGC->info.out_chroma.bus_address;
              p_dec_out->output_picture_cr.virtual_address =
                PTR_JPGC->info.out_chroma2.virtual_address;
              p_dec_out->output_picture_cr.bus_address =
                PTR_JPGC->info.out_chroma2.bus_address;
            }
          }

          /* No slice output in case decoder + PP (no pipeline) */
          if(PTR_JPGC->pp_instance != NULL &&
              PTR_JPGC->pp_control.use_pipeline == 0) {
            /* output addresses */
            p_dec_out->output_picture_y.virtual_address = NULL;
            p_dec_out->output_picture_y.bus_address = 0;
            if(PTR_JPGC->image.size_chroma) {
              p_dec_out->output_picture_cb_cr.virtual_address = NULL;
              p_dec_out->output_picture_cb_cr.bus_address = 0;
              p_dec_out->output_picture_cr.virtual_address = NULL;
              p_dec_out->output_picture_cr.bus_address = 0;
            }

            JPEGDEC_API_TRC(("JpegDecDecode# Decoder + PP (Rotation/Flip), Slice ready"));
            /* PP not in pipeline, continue do <==> while */
            PTR_JPGC->info.no_slice_irq_for_user = 1;
          } else {
            JPEGDEC_API_TRC(("JpegDecDecode# Slice ready"));
            return JPEGDEC_SLICE_READY;
          }
        } else {
          if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE ||
              PTR_JPGC->info.operation_type == JPEGDEC_NONINTERLEAVED) {
            current_pos =
              GET_ADDR_REG(PTR_JPGC->jpeg_regs,
                           HWIF_RLC_VLC_BASE);

            /* update input buffer address */
            PTR_JPGC->stream.p_curr_pos = ((u8 *) current_pos - 10);
            PTR_JPGC->stream.bit_pos_in_byte = 0;
            PTR_JPGC->stream.read_bits =
              ((PTR_JPGC->stream.p_curr_pos -
                PTR_JPGC->stream.p_start_of_stream) * 8);

            /* default if data ends */
            end_of_image = 1;

            /* check if last scan is decoded */
            for(i = 0;
                i <
                ((PTR_JPGC->stream.stream_length -
                  (PTR_JPGC->stream.read_bits / 8))); i++) {
              current_byte = PTR_JPGC->stream.p_curr_pos[i];
              if(current_byte == 0xFF) {
                current_byte = PTR_JPGC->stream.p_curr_pos[i + 1];
                if(current_byte == 0xD9) {
                  end_of_image = 1;
                  break;
                } else if(current_byte == 0xC4 ||
                          current_byte == 0xDA) {
                  end_of_image = 0;
                  break;
                }
              }
            }

            current_byte = 0;
            PTR_JPGC->info.SliceCount = 0;
            PTR_JPGC->info.SliceReadyForPause = 0;

            /* if not the last scan of the stream */
            if(end_of_image == 0) {
              /* output set */
              if(PTR_JPGC->pp_instance == NULL &&
                  !PTR_JPGC->info.no_slice_irq_for_user) {
                p_dec_out->output_picture_y.virtual_address =
                  PTR_JPGC->info.out_luma.virtual_address;
                ASSERT(p_dec_out->output_picture_y.virtual_address);

                /* output set */
                p_dec_out->output_picture_y.bus_address =
                  PTR_JPGC->info.out_luma.bus_address;
                ASSERT(p_dec_out->output_picture_y.bus_address);

                /* if not grayscale */
                if(PTR_JPGC->image.size_chroma) {
                  p_dec_out->output_picture_cb_cr.virtual_address =
                    PTR_JPGC->info.out_chroma.virtual_address;
                  ASSERT(p_dec_out->output_picture_cb_cr.
                         virtual_address);

                  p_dec_out->output_picture_cb_cr.bus_address =
                    PTR_JPGC->info.out_chroma.bus_address;
                  ASSERT(p_dec_out->output_picture_cb_cr.
                         bus_address);

                  p_dec_out->output_picture_cr.virtual_address =
                    PTR_JPGC->info.out_chroma2.
                    virtual_address;
                  p_dec_out->output_picture_cr.bus_address =
                    PTR_JPGC->info.out_chroma2.bus_address;
                }
              }

              /* PP not in pipeline, continue do <==> while */
              PTR_JPGC->info.no_slice_irq_for_user = 0;

              if(PTR_JPGC->info.operation_type ==
                  JPEGDEC_PROGRESSIVE)
                PTR_JPGC->info.progressive_scan_ready = 1;
              else
                PTR_JPGC->info.non_interleaved_scan_ready = 1;

              /* return control to application if progressive */
              if(PTR_JPGC->info.operation_type !=
                  JPEGDEC_NONINTERLEAVED) {
                /* non-interleaved scan ==> no output */
                if(PTR_JPGC->info.non_interleaved == 0)
                  PTR_JPGC->info.no_slice_irq_for_user = 1;
                else {
                  JPEGDEC_API_TRC
                  ("JpegDecDecode# SCAN PROCESSED");
                  return (JPEGDEC_SCAN_PROCESSED);
                }
              } else {
                /* set decoded component */
                PTR_JPGC->info.components[PTR_JPGC->info.
                                          component_id] = 1;

                /* check if we have decoded all components */
                if(PTR_JPGC->info.components[0] == 1 &&
                    PTR_JPGC->info.components[1] == 1 &&
                    PTR_JPGC->info.components[2] == 1) {
                  /* continue decoding next scan */
                  PTR_JPGC->info.no_slice_irq_for_user = 0;
                  non_interleaved_rdy = 0;
                } else {
                  /* continue decoding next scan */
                  PTR_JPGC->info.no_slice_irq_for_user = 1;
                  non_interleaved_rdy = 0;
                }
              }
            } else {
              if(PTR_JPGC->info.operation_type ==
                  JPEGDEC_NONINTERLEAVED) {
                /* set decoded component */
                PTR_JPGC->info.components[PTR_JPGC->info.
                                          component_id] = 1;

                /* check if we have decoded all components */
                if(PTR_JPGC->info.components[0] == 1 &&
                    PTR_JPGC->info.components[1] == 1 &&
                    PTR_JPGC->info.components[2] == 1) {
                  /* continue decoding next scan */
                  PTR_JPGC->info.no_slice_irq_for_user = 0;
                  non_interleaved_rdy = 1;
                } else {
                  /* continue decoding next scan */
                  PTR_JPGC->info.no_slice_irq_for_user = 1;
                  non_interleaved_rdy = 0;
                }
              }
            }
          } else {
            /* PP not in pipeline, continue do <==> while */
            PTR_JPGC->info.no_slice_irq_for_user = 0;
          }
        }

        if(PTR_JPGC->info.no_slice_irq_for_user == 0) {
          /* Release HW */
          (void) DWLReleaseHw(PTR_JPGC->dwl, PTR_JPGC->core_id);
          /* update asic_running */
          PTR_JPGC->asic_running = 0;

          JPEGDEC_API_TRC("JpegDecDecode# FRAME READY");

          /* set image ready */
          PTR_JPGC->image.image_ready = 1;
        }

        /* check PP status... and go */
        if(PTR_JPGC->pp_instance != NULL &&
            !PTR_JPGC->info.no_slice_irq_for_user) {
          /* set pp for stand alone */
          if(!PTR_JPGC->pp_control.use_pipeline) {
            JpegDecPreparePp(PTR_JPGC);

            JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set output write disabled\n"));
            SetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_OUT_DIS, 1);

            /* Enable pp */
            JPEGDEC_TRACE_INTERNAL(("INTERNAL: Set Enable pp\n"));
            PTR_JPGC->PPRun(PTR_JPGC->pp_instance,
                            &PTR_JPGC->pp_control);

            PTR_JPGC->pp_control.pp_status = DECPP_RUNNING;
            PTR_JPGC->info.pipeline = 1;

            /* Flush regs to hw register */
            JpegFlushRegs(PTR_JPGC);
          }

          /* End PP co-operation */
          if(PTR_JPGC->pp_control.pp_status == DECPP_RUNNING) {
            JPEGDEC_API_TRC("JpegDecDecode# PP END CALL");
            PTR_JPGC->PPEndCallback(PTR_JPGC->pp_instance);
          }

          PTR_JPGC->pp_control.pp_status = DECPP_PIC_READY;
        }

        /* output set */
        if(PTR_JPGC->pp_instance == NULL &&
            !PTR_JPGC->info.no_slice_irq_for_user) {
          p_dec_out->output_picture_y.virtual_address =
            PTR_JPGC->info.out_luma.virtual_address;
          ASSERT(p_dec_out->output_picture_y.virtual_address);

          /* output set */
          p_dec_out->output_picture_y.bus_address =
            PTR_JPGC->info.out_luma.bus_address;
          ASSERT(p_dec_out->output_picture_y.bus_address);

          /* if not grayscale */
          if(PTR_JPGC->image.size_chroma) {
            p_dec_out->output_picture_cb_cr.virtual_address =
              PTR_JPGC->info.out_chroma.virtual_address;
            ASSERT(p_dec_out->output_picture_cb_cr.virtual_address);

            p_dec_out->output_picture_cb_cr.bus_address =
              PTR_JPGC->info.out_chroma.bus_address;
            ASSERT(p_dec_out->output_picture_cb_cr.bus_address);

            p_dec_out->output_picture_cr.virtual_address =
              PTR_JPGC->info.out_chroma2.virtual_address;
            p_dec_out->output_picture_cr.bus_address =
              PTR_JPGC->info.out_chroma2.bus_address;
          }
        }

#ifdef JPEGDEC_ASIC_TRACE
        {
          JPEGDEC_TRACE_INTERNAL(("\nJpeg_dec_decode# TEST\n"));
          PrintJPEGReg(PTR_JPGC->jpeg_regs);
        }
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

        /* get the current stream address  */
        if(PTR_JPGC->info.operation_type == JPEGDEC_PROGRESSIVE ||
            (PTR_JPGC->info.operation_type == JPEGDEC_NONINTERLEAVED &&
             non_interleaved_rdy == 0)) {
          ret_code = JpegDecNextScanHdrs(PTR_JPGC);
          if(ret_code != JPEGDEC_OK && ret_code != JPEGDEC_FRAME_READY) {
            /* return */
            return ret_code;
          }
        }
      } else if(dwlret == DWL_HW_WAIT_TIMEOUT) {
        JPEGDEC_API_TRC("SCAN: DWL HW TIMEOUT\n");

        /* Release HW */
        (void) DWLReleaseHw(PTR_JPGC->dwl, PTR_JPGC->core_id);
        /* update asic_running */
        PTR_JPGC->asic_running = 0;

        return (JPEGDEC_DWL_HW_TIMEOUT);
      } else if(dwlret == DWL_HW_WAIT_ERROR) {
        JPEGDEC_API_TRC("SCAN: DWL HW ERROR\n");

        /* Release HW */
        (void) DWLReleaseHw(PTR_JPGC->dwl, PTR_JPGC->core_id);
        /* update asic_running */
        PTR_JPGC->asic_running = 0;

        return (JPEGDEC_SYSTEM_ERROR);
      }
    } while(!PTR_JPGC->image.image_ready);
  }

  if(PTR_JPGC->image.image_ready) {
    JPEGDEC_API_TRC("JpegDecDecode# IMAGE READY");
    JPEGDEC_API_TRC("JpegDecDecode# OK\n");

    /* reset image status */
    PTR_JPGC->image.image_ready = 0;
    PTR_JPGC->image.header_ready = 0;

    /* reset */
    JpegDecClearStructs(PTR_JPGC, 1);

    if(PTR_JPGC->info.operation_type == JPEGDEC_BASELINE) {
      return (JPEGDEC_FRAME_READY);
    } else {
      if(end_of_image == 0)
        return (JPEGDEC_SCAN_PROCESSED);
      else {
        if(PTR_JPGC->frame.Nf != 1) {
          /* determine first component that needs to be cheated */
          i = 0;
          while(i < 3 && PTR_JPGC->info.pf_needed[i] == 0)
            i++;
          if(i == 3)
            return (JPEGDEC_FRAME_READY);

          JpegDecInitHWEmptyScan(PTR_JPGC, i++);
          dwlret = DWLWaitHwReady(PTR_JPGC->dwl, PTR_JPGC->core_id,
                                  PTR_JPGC->info.timeout);
          ASSERT(dwlret == DWL_HW_WAIT_OK);
          JpegRefreshRegs(PTR_JPGC);
          asic_status =
            GetDecRegister(PTR_JPGC->jpeg_regs, HWIF_DEC_IRQ_STAT);
          ASSERT(asic_status == 1);

          while(i < 3 && PTR_JPGC->info.pf_needed[i] == 0)
            i++;
          if(i == 3)
            return (JPEGDEC_FRAME_READY);
          else {
            PTR_JPGC->info.progressive_finish = 1;
            PTR_JPGC->info.pf_comp_id = i;
            return (JPEGDEC_SCAN_PROCESSED);
          }
        } else
          return (JPEGDEC_FRAME_READY);
      }
    }
  } else {
    JPEGDEC_API_TRC("JpegDecDecode# ERROR\n");
    return (JPEGDEC_ERROR);
  }

#undef JPG_FRM
#undef PTR_JPGC
#undef PTR_INFO
}

/*------------------------------------------------------------------------------

    Function name: JpegDecPreparePp

    Functional description:
        Setup PP interface

    Input:
        container

    Return values:
        void

------------------------------------------------------------------------------*/
static void JpegDecPreparePp(JpegDecContainer * jpeg_dec_cont) {
  jpeg_dec_cont->pp_control.pic_struct = 0;
  jpeg_dec_cont->pp_control.top_field = 0;

  {
    u32 tmp = GetDecRegister(jpeg_dec_cont->jpeg_regs, HWIF_DEC_OUT_ENDIAN);

    jpeg_dec_cont->pp_control.little_endian =
      (tmp == DEC_X170_LITTLE_ENDIAN) ? 1 : 0;
  }
  jpeg_dec_cont->pp_control.word_swap = GetDecRegister(jpeg_dec_cont->jpeg_regs,
                                        HWIF_DEC_OUTSWAP32_E) ? 1
                                        : 0;

  /* pipeline */
  if(jpeg_dec_cont->pp_control.use_pipeline) {
    /* luma */
    jpeg_dec_cont->pp_control.input_bus_luma = 0;
    /* chroma */
    jpeg_dec_cont->pp_control.input_bus_chroma = 0;
  } else {
    /* luma */
    jpeg_dec_cont->pp_control.input_bus_luma =
      jpeg_dec_cont->asic_buff.out_luma_buffer.bus_address;
    /* chroma */
    jpeg_dec_cont->pp_control.input_bus_chroma =
      jpeg_dec_cont->asic_buff.out_chroma_buffer.bus_address;
  }

  /* dimensions */
  jpeg_dec_cont->pp_control.inwidth =
    jpeg_dec_cont->pp_control.cropped_w = jpeg_dec_cont->info.X;

  jpeg_dec_cont->pp_control.inheight =
    jpeg_dec_cont->pp_control.cropped_h = jpeg_dec_cont->info.Y;
}

/*------------------------------------------------------------------------------

    5.6. Function name: JpegGetAPIVersion

         Purpose:       Returns version information about this API

         Input:         void

         Output:        JpegDecApiVersion

------------------------------------------------------------------------------*/
JpegDecApiVersion JpegGetAPIVersion() {
  JpegDecApiVersion ver;

  ver.major = JPG_MAJOR_VERSION;
  ver.minor = JPG_MINOR_VERSION;
  JPEGDEC_API_TRC("JpegGetAPIVersion# OK\n");
  return ver;
}

/*------------------------------------------------------------------------------

    5.7. Function name: JpegDecGetBuild

         Purpose:       Returns the SW and HW build information

         Input:         void

         Output:        JpegDecGetBuild

------------------------------------------------------------------------------*/
JpegDecBuild JpegDecGetBuild(void) {
  JpegDecBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_JPEG_DEC);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_JPEG_DEC);

  JPEGDEC_API_TRC("JpegDecGetBuild# OK\n");

  return build_info;
}

/*------------------------------------------------------------------------------
    5.8. Function name   : jpegRegisterPP
         Description     : Called internally by PP to enable the pipeline
         Return type     : i32 - return 0 for success or a negative error code
         Argument        : const void * dec_inst - decoder instance
         Argument        : const void  *pp_inst - post-processor instance
         Argument        : (*PPRun)(const void *) - decoder calls this to start PP
         Argument        : void (*PPEndCallback)(const void *) - decoder calls this
                           to notify PP that a picture was done.
------------------------------------------------------------------------------*/
i32 jpegRegisterPP(const void *dec_inst, const void *pp_inst,
                   void (*PPRun) (const void *, const DecPpInterface *),
                   void (*PPEndCallback) (const void *),
                   void (*PPConfigQuery) (const void *, DecPpQuery *)) {
  JpegDecContainer *dec_cont;

  dec_cont = (JpegDecContainer *) dec_inst;

  if(dec_inst == NULL || dec_cont->pp_instance != NULL ||
      pp_inst == NULL || PPRun == NULL || PPEndCallback == NULL)
    return -1;

  if(dec_cont->asic_running)
    return -2;

  dec_cont->pp_instance = pp_inst;
  dec_cont->PPEndCallback = PPEndCallback;
  dec_cont->PPRun = PPRun;
  dec_cont->PPConfigQuery = PPConfigQuery;

  return 0;
}

/*------------------------------------------------------------------------------
    5.9. Function name   : jpegUnregisterPP
         Description     : Called internally by PP to disable the pipeline
         Return type     : i32 - return 0 for success or a negative error code
         Argument        : const void * dec_inst - decoder instance
         Argument        : const void  *pp_inst - post-processor instance
------------------------------------------------------------------------------*/
i32 jpegUnregisterPP(const void *dec_inst, const void *pp_inst) {
  JpegDecContainer *dec_cont;

  dec_cont = (JpegDecContainer *) dec_inst;

  ASSERT(dec_inst != NULL && pp_inst == dec_cont->pp_instance);

  if(dec_inst == NULL || pp_inst != dec_cont->pp_instance)
    return -1;

  if(dec_cont->asic_running)
    return -2;

  dec_cont->pp_instance = NULL;
  dec_cont->PPEndCallback = NULL;
  dec_cont->PPRun = NULL;

  return 0;
}
