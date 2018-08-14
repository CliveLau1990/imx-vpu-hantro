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
#include "basetype.h"
#include "ppapi.h"
#include "ppinternal.h"
#include "regdrv_g1.h"
#include "dwl.h"
#include "decppif.h"
#include "ppcfg.h"

#include "ppdebug.h"

/*------------------------------------------------------------------------------
       Version Information
------------------------------------------------------------------------------*/

#define PP_MAJOR_VERSION 1
#define PP_MINOR_VERSION 1

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

#ifdef PP_TRACE
#define PP_API_TRC(str)    PPTrace((str))
#else
#define PP_API_TRC(str)
#endif

#ifdef PP_H264DEC_PIPELINE_SUPPORT
#include "h264_pp_pipeline.h"
#endif

#ifdef PP_MPEG4DEC_PIPELINE_SUPPORT
#include "mpeg4_pp_pipeline.h"
#endif

#ifdef PP_JPEGDEC_PIPELINE_SUPPORT
#include "jpeg_pp_pipeline.h"
#endif

#ifdef PP_VC1DEC_PIPELINE_SUPPORT
#include "vc1hwd_pp_pipeline.h"
#endif

#ifdef PP_MPEG2DEC_PIPELINE_SUPPORT
#include "mpeg2hwd_pp_pipeline.h"
#endif

#ifdef PP_RVDEC_PIPELINE_SUPPORT
#include "rv_pp_pipeline.h"
#endif

#ifdef PP_VP6DEC_PIPELINE_SUPPORT
#include "vp6_pp_pipeline.h"
#endif

#ifdef PP_VP8DEC_PIPELINE_SUPPORT
#include "vp8hwd_pp_pipeline.h"
#endif

#ifdef PP_AVSDEC_PIPELINE_SUPPORT
#include "avs_pp_pipeline.h"
#endif

#if defined(PP_JPEGDEC_PIPELINE_SUPPORT) ||\
    defined(PP_MPEG4DEC_PIPELINE_SUPPORT) ||\
    defined(PP_H264DEC_PIPELINE_SUPPORT) ||\
    defined(PP_VC1DEC_PIPELINE_SUPPORT) ||\
    defined(PP_MPEG2DEC_PIPELINE_SUPPORT) ||\
    defined(PP_RVDEC_PIPELINE_SUPPORT) ||\
    defined(PP_AVSDEC_PIPELINE_SUPPORT) ||\
    defined(PP_VP6DEC_PIPELINE_SUPPORT) ||\
    defined(PP_VP8DEC_PIPELINE_SUPPORT)
#define PP_DEC_PIPELINE_SUPPORT
static void PPDecStartPp(const void *post_pinst, const DecPpInterface *);
static void PPDecEndCallback(const void *post_pinst);
static void PPDecConfigQueryFromDec(const void *post_pinst, DecPpQuery *);
static void PPDecSetUpDeinterlace(PPContainer * pp_c,
                                  const DecPpInterface * decpp);
static void PPDecSetOutBuffer(PPContainer * pp_c, const DecPpInterface * decpp);
#else
#undef PP_DEC_PIPELINE_SUPPORT
#endif
#if defined(PP_MPEG4DEC_PIPELINE_SUPPORT) ||\
    defined(PP_H264DEC_PIPELINE_SUPPORT) ||\
    defined(PP_VC1DEC_PIPELINE_SUPPORT) ||\
    defined(PP_MPEG2DEC_PIPELINE_SUPPORT) ||\
    defined(PP_RVDEC_PIPELINE_SUPPORT) ||\
    defined(PP_AVSDEC_PIPELINE_SUPPORT)
static void PPDecDisplayIndex(const void *post_pinst, u32 index);
#endif
#if defined(PP_MPEG4DEC_PIPELINE_SUPPORT) ||\
    defined(PP_VC1DEC_PIPELINE_SUPPORT) ||\
    defined(PP_MPEG2DEC_PIPELINE_SUPPORT) ||\
    defined(PP_RVDEC_PIPELINE_SUPPORT) ||\
    defined(PP_AVSDEC_PIPELINE_SUPPORT)
static void PPDecBufferData(const void *post_pinst, u32 buffer_index,
                            addr_t input_bus_luma, addr_t input_bus_chroma,
                            addr_t bottom_bus_luma, addr_t bottom_bus_chroma );
#endif

#define PP_IS_JOINED(pp_c) (pp_c->dec_inst != NULL ? 1 : 0)

/*------------------------------------------------------------------------------
    Function name   : PPInit
    Description     : initialize pp
    Return type     : PPResult
    Argument        : PPInst * post_pinst
------------------------------------------------------------------------------*/
PPResult PPInit(PPInst * p_post_pinst) {
  const void *dwl;
  PPContainer *p_pp_cont;
  struct DWLInitParam dwl_init;
  u32 hw_id, product;
  DWLHwConfig hw_config;

  PP_API_TRC("PPInit #");
  if(p_post_pinst == NULL) {
    PP_API_TRC("PPInit# ERROR: PPInst == NULL");
    return (PP_PARAM_ERROR);
  }

  *p_post_pinst = NULL; /* return NULL instance for any error */

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_PP);

  if(!hw_config.addr64_support && sizeof(void *) == 8) {
    PP_API_TRC("PPInit# ERROR: HW not support 64bit address!\n");
    return (PP_PARAM_ERROR);
  }

  /* check that we have correct HW */
  hw_id = DWLReadAsicID(DWL_CLIENT_TYPE_PP);
  product = hw_id >> 16;

  PPDEBUG_PRINT(("Product %x\n", product));

  if(product < 0x8170 &&
      product != 0x6731 ) {
    PP_API_TRC("PPInit# ERROR: Unknown hardware");
    return PP_DWL_ERROR;
  }

  dwl_init.client_type = DWL_CLIENT_TYPE_PP;

  dwl = DWLInit(&dwl_init);

  if(dwl == NULL) {
    PP_API_TRC("PPInit# ERROR: DWL Init failed");
    return (PP_DWL_ERROR);
  }

  p_pp_cont = (PPContainer *) DWLmalloc(sizeof(PPContainer));

  if(p_pp_cont == NULL) {
    PP_API_TRC("PPInit# ERROR: Memory allocation failed");
    (void) DWLRelease(dwl);
    return (PP_MEMFAIL);
  }

  (void) DWLmemset(p_pp_cont, 0, sizeof(PPContainer));

  p_pp_cont->dwl = dwl;

  PPInitDataStructures(p_pp_cont);

  PPInitHW(p_pp_cont);

  if(PPSelectOutputSize(p_pp_cont) != PP_OK) {
    PP_API_TRC("PPInit# ERROR: Ilegal output size");
    DWLfree(p_pp_cont);
    (void) DWLRelease(dwl);
    return (PP_DWL_ERROR);
  }

  p_pp_cont->dec_inst = NULL;
  p_pp_cont->pipeline = 0;
  p_pp_cont->hw_id = product;    /* save product id */
  PPSetStatus(p_pp_cont, PP_STATUS_IDLE);

#if ( PP_X170_FAST_VERTICAL_DOWNSCALE_DISABLE != 0 )
  p_pp_cont->fast_vertical_downscale_disable = 1;
#endif

#if ( PP_X170_FAST_HORIZONTAL_DOWNSCALE_DISABLE != 0 )
  p_pp_cont->fast_horizontal_downscale_disable = 1;
#endif

  *p_post_pinst = p_pp_cont;

  return (PP_OK);
}

/*------------------------------------------------------------------------------
    Function name   : PPGetConfig
    Description     :
    Return type     : PPResult
    Argument        : PPInst post_pinst
    Argument        : PPConfig * p_pp_conf
------------------------------------------------------------------------------*/
PPResult PPGetConfig(PPInst post_pinst, PPConfig * p_pp_conf) {
  PPContainer *pp_c;

  PP_API_TRC("PPGetConfig #");

  if(post_pinst == NULL || p_pp_conf == NULL) {
    return (PP_PARAM_ERROR);
  }

  pp_c = (PPContainer *) post_pinst;

  (void) DWLmemcpy(p_pp_conf, &pp_c->pp_cfg, sizeof(PPConfig));

  return PP_OK;
}

/*------------------------------------------------------------------------------
    Function name   : PPSetConfig
    Description     :
    Return type     : PPResult
    Argument        : PPInst post_pinst
    Argument        : PPConfig * p_pp_conf
------------------------------------------------------------------------------*/
PPResult PPSetConfig(PPInst post_pinst, PPConfig * p_pp_conf) {

  PPContainer *pp_c;

  PPOutImage *pp_out_img;
  PPInImage *pp_in_img;
  PPOutRgb *pp_out_rgb;
  PPRgbTransform *rgb_t;

  i32 tmp;

  PP_API_TRC("PPSetConfig #\n");
  if(post_pinst == NULL || p_pp_conf == NULL) {
    return (PP_PARAM_ERROR);
  }

  pp_c = (PPContainer *) post_pinst;

  pp_out_img = &pp_c->pp_cfg.pp_out_img;
  pp_in_img = &pp_c->pp_cfg.pp_in_img;
  pp_out_rgb = &pp_c->pp_cfg.pp_out_rgb;
  rgb_t = &pp_out_rgb->rgb_transform_coeffs;

  if(pp_c->multi_buffer) {
    /* we set these to make PPCheckConfig() happy */
    p_pp_conf->pp_out_img.buffer_bus_addr =
      pp_c->combined_mode_buffers.pp_output_buffers[0].buffer_bus_addr;
    p_pp_conf->pp_out_img.buffer_chroma_bus_addr =
      pp_c->combined_mode_buffers.pp_output_buffers[0].buffer_chroma_bus_addr;
  } else {
    /* If multibuffer mode is not used, then the amount of
     * buffers is not initialized. */
    if( pp_c->combined_mode_buffers.nbr_of_buffers == 0 )
      pp_c->combined_mode_buffers.nbr_of_buffers = 1;
  }

  tmp =
    PPCheckConfig(pp_c, p_pp_conf, (pp_c->dec_inst != NULL ? 1 : 0),
                  pp_c->dec_type);

  if(tmp != (i32) PP_OK) {
    return (PPResult) tmp;
  }

  /* store previous cfg in order to notice any changes that require
   * rerun of buffered pictures */
  (void) DWLmemcpy(&pp_c->prev_cfg, &pp_c->pp_cfg, sizeof(PPConfig));

  (void) DWLmemcpy(&pp_c->pp_cfg, p_pp_conf, sizeof(PPConfig));

  switch (pp_out_rgb->rgb_transform) {
  case PP_YCBCR2RGB_TRANSFORM_CUSTOM:
    /* coeffs are coming from user */
    break;
  case PP_YCBCR2RGB_TRANSFORM_BT_601:
    /* Bt.601 */
    if(pp_in_img->video_range == 0) {
      rgb_t->a = 298;
      rgb_t->b = 409;
      rgb_t->c = 208;
      rgb_t->d = 100;
      rgb_t->e = 516;
    } else {
      rgb_t->a = 256;
      rgb_t->b = 350;
      rgb_t->c = 179;
      rgb_t->d = 86;
      rgb_t->e = 443;
    }
    break;
  case PP_YCBCR2RGB_TRANSFORM_BT_709:
    /* Bt.709 */
    if(pp_in_img->video_range == 0) {
      rgb_t->a = 298;
      rgb_t->b = 459;
      rgb_t->c = 137;
      rgb_t->d = 55;
      rgb_t->e = 544;
    } else {
      rgb_t->a = 256;
      rgb_t->b = 403;
      rgb_t->c = 120;
      rgb_t->d = 48;
      rgb_t->e = 475;
    }
    break;
  default:
    ASSERT(0);
  }

  /* initialize alpha blend source endianness to input picture endianness.
   * Cannot be done in Init() because output format is configured here and
   * PP_X170_IGNORE_ABLEND_ENDIANNESS flag affects only RGB32 */
  SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_A1_ENDIAN,
                PP_X170_INPUT_PICTURE_ENDIAN);

  switch (pp_out_img->pix_format & 0xFF0000) {
  case PP_PIXEL_FORMAT_RGB_MASK:
    pp_c->out_format = PP_ASIC_OUT_FORMAT_RGB;
    if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB32_MASK) {
      pp_c->rgb_depth = 32;
#if ( PP_X170_IGNORE_ABLEND_ENDIANNESS != 0 )
      /* if input endianness ignored for alpha blend sources -> set big
       * endian */
      SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_A1_ENDIAN,
                    PP_X170_PICTURE_BIG_ENDIAN);
#endif
    } else {
      pp_c->rgb_depth = 16;
    }
    break;
  case PP_PIXEL_FORMAT_YUV420_MASK:
    pp_c->out_format = PP_ASIC_OUT_FORMAT_420;
    break;
  case PP_PIXEL_FORMAT_YUV422_MASK:
    pp_c->out_format = PP_ASIC_OUT_FORMAT_422;
    pp_c->out_start_ch =
      (pp_out_img->pix_format == PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED ||
       pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED ||
       pp_out_img->pix_format == PP_PIX_FMT_CBYCRY_4_2_2_TILED_4X4 ||
       pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4);
    pp_c->out_cr_first =
      (pp_out_img->pix_format == PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED ||
       pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED ||
       pp_out_img->pix_format == PP_PIX_FMT_YCRYCB_4_2_2_TILED_4X4 ||
       pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4);
    pp_c->out_tiled4x4 =
      (pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_2_TILED_4X4 ||
       pp_out_img->pix_format == PP_PIX_FMT_YCRYCB_4_2_2_TILED_4X4 ||
       pp_out_img->pix_format == PP_PIX_FMT_CBYCRY_4_2_2_TILED_4X4 ||
       pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4);
    break;
  default:
    ASSERT(0);
  }

  switch (pp_in_img->pix_format) {
  case PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED:
    pp_c->in_format = PP_ASIC_IN_FORMAT_422;
    pp_c->in_start_ch = 0;
    pp_c->in_cr_first = 0;
    break;
  case PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED:
    pp_c->in_format = PP_ASIC_IN_FORMAT_422;
    pp_c->in_start_ch = 0;
    pp_c->in_cr_first = 1;
    break;
  case PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED:
    pp_c->in_format = PP_ASIC_IN_FORMAT_422;
    pp_c->in_start_ch = 1;
    pp_c->in_cr_first = 0;
    break;
  case PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED:
    pp_c->in_format = PP_ASIC_IN_FORMAT_422;
    pp_c->in_start_ch = 1;
    pp_c->in_cr_first = 1;
    break;
  case PP_PIX_FMT_YCBCR_4_2_0_PLANAR:
    pp_c->in_format = PP_ASIC_IN_FORMAT_420_PLANAR;
    break;
  case PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR:
    pp_c->in_format = PP_ASIC_IN_FORMAT_420_SEMIPLANAR;
    break;
  case PP_PIX_FMT_YCBCR_4_0_0:
    pp_c->in_format = PP_ASIC_IN_FORMAT_400;
    break;
  case PP_PIX_FMT_YCBCR_4_2_2_SEMIPLANAR:
    pp_c->in_format = PP_ASIC_IN_FORMAT_422_SEMIPLANAR;
    break;
  case PP_PIX_FMT_YCBCR_4_2_0_TILED:
    pp_c->in_format = PP_ASIC_IN_FORMAT_420_TILED;
    break;
  case PP_PIX_FMT_YCBCR_4_4_0:
    pp_c->in_format = PP_ASIC_IN_FORMAT_440_SEMIPLANAR;
    break;
  case PP_PIX_FMT_YCBCR_4_1_1_SEMIPLANAR:
    pp_c->in_format = PP_ASIC_IN_FORMAT_411_SEMIPLANAR;
    break;
  case PP_PIX_FMT_YCBCR_4_4_4_SEMIPLANAR:
    pp_c->in_format = PP_ASIC_IN_FORMAT_444_SEMIPLANAR;
    break;
  default:
    ASSERT(0);
  }

  /* if config has changed in a way that messes up the multi-buffering,
   * mark previously decoded pics dirty */

  if(pp_c->multi_buffer) {
    if(PPCheckSetupChanges(&pp_c->prev_cfg, &pp_c->pp_cfg)) {
      pp_c->current_setup_id++;
    }

  }

  PPSetupHW(pp_c);

  return (PP_OK);
}

/*------------------------------------------------------------------------------
    Function name   : PPRelease
    Description     :
    Return type     : void
    Argument        : PPInst post_pinst
------------------------------------------------------------------------------*/
void PPRelease(PPInst post_pinst) {
  PPContainer *pp_c;
  const void *dwl;

  if(post_pinst == NULL) {
    return;
  }

  pp_c = (PPContainer *) post_pinst;

  if(pp_c->dec_inst != NULL) {
    (void) PPDecCombinedModeDisable(pp_c, pp_c->dec_inst);
  }

  dwl = pp_c->dwl;

  DWLfree(pp_c);

  (void) DWLRelease((void *) dwl);
}

/*------------------------------------------------------------------------------
    Function name   : PPGetResult
    Description     :
    Return type     : PPResult
    Argument        : PPInst post_pinst
------------------------------------------------------------------------------*/
PPResult PPGetResult(PPInst post_pinst) {
  PPContainer *pp_c;
  PPResult ret = PP_OK;

  PP_API_TRC("PPGetResult #");
  if(post_pinst == NULL) {
    return (PP_PARAM_ERROR);
  }

  pp_c = (PPContainer *) post_pinst;

  if(PPGetStatus(pp_c) != PP_STATUS_IDLE) {
    return PP_BUSY;
  }

  if(PP_IS_JOINED(pp_c)) {
    ret = pp_c->PPCombinedRet;
  } else {
    if(PPRun(pp_c) != PP_OK) {
      return PP_BUSY;
    }

    ret = WaitForPp(pp_c);
  }

  PP_API_TRC("PPGetResult # exit");
  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : PPDecWaitResult
    Description     : Wait for PP, not started here i.e PPRun not called
    Return type     : PPResult
    Argument        : PPInst post_pinst
------------------------------------------------------------------------------*/
PPResult PPDecWaitResult(PPInst post_pinst) {
  PPContainer *pp_c;
  PPResult ret = PP_OK;

  PP_API_TRC("PPGetResult #");

  if(post_pinst == NULL) {
    PP_API_TRC("PPGetResult paramerr");
    return (PP_PARAM_ERROR);
  }

  pp_c = (PPContainer *) post_pinst;

  ASSERT(pp_c->pipeline == 0);

  if(pp_c->dec_inst == NULL) {
    PP_API_TRC("PPGetResult paramerr");
    return (PP_PARAM_ERROR);
  }

  if(PPGetStatus(pp_c) != PP_STATUS_RUNNING) {

    PP_API_TRC("PPGetResult pp_busy");
    return PP_BUSY;
  }

  ret = WaitForPp(pp_c);
  PP_API_TRC("PPGetResult # exit");
  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : PPGetAPIVersion
    Description     :
    Return type     : PPApiVersion
------------------------------------------------------------------------------*/
PPApiVersion PPGetAPIVersion() {
  PPApiVersion ver;

  ver.major = PP_MAJOR_VERSION;
  ver.minor = PP_MINOR_VERSION;

  PP_API_TRC("PPGetAPIVersion");

  return ver;
}

/*------------------------------------------------------------------------------
    Function name   : PPGetBuild
    Description     :
    Return type     : PPBuild
    Argument        : void
------------------------------------------------------------------------------*/
PPBuild PPGetBuild(void) {
  PPBuild build_info;

  (void)DWLmemset(&build_info, 0, sizeof(build_info));

  build_info.sw_build = HANTRO_DEC_SW_BUILD;
  build_info.hw_build = DWLReadAsicID(DWL_CLIENT_TYPE_PP);

  DWLReadAsicConfig(build_info.hw_config,DWL_CLIENT_TYPE_PP);

  PP_API_TRC("PPGetBuild#");

  return build_info;
}

/*------------------------------------------------------------------------------
    Function name   : PPDecStartPp
    Description     : set up and run pp based on information from the decoder
    Return type     : void
    Argument        : pp instance and control information struct
------------------------------------------------------------------------------*/

#ifdef PP_DEC_PIPELINE_SUPPORT

static void PPDecStartPp(const void *post_pinst, const DecPpInterface * decpp) {
  PPContainer *pp_c;
  PPInImage *pp_in_img;
  PPOutImage *pp_out_img;
  PPOutDeinterlace *pp_out_deint;
  u32 tmp = 0;
  u32 pix_format_ok;

  PP_API_TRC("PPDecStartPp #");
  pp_c = (PPContainer *) post_pinst;

  ASSERT(pp_c != NULL);
  ASSERT(decpp != NULL);

  if(pp_c == NULL) {
    return;
  }

  if(decpp == NULL) {
    pp_c->PPCombinedRet = PP_PARAM_ERROR;
    return;
  }

  pp_c->PPCombinedRet = PP_OK;

  pp_out_deint = &pp_c->pp_cfg.pp_out_deinterlace;

  pp_in_img = &pp_c->pp_cfg.pp_in_img;
  pp_out_img = &pp_c->pp_cfg.pp_out_img;

  if(decpp->tiled_input_mode) {
    pp_c->fast_vertical_downscale_disable = 1;
  }

  /* PP does not currently support arbitrary luma/chroma strides so bail
   * out if they are used. */
  if(decpp->luma_stride && decpp->luma_stride != pp_in_img->width ) {
    pp_c->PPCombinedRet = PP_DEC_RUNTIME_ERROR;
    return;
  }

  if(decpp->chroma_stride && decpp->chroma_stride != pp_in_img->width ) {
    pp_c->PPCombinedRet = PP_DEC_RUNTIME_ERROR;
    return;
  }

  /* Set VC-1 specific parameters */
  if(pp_c->dec_type == PP_PIPELINED_DEC_TYPE_VC1) {

    pp_c->in_width = decpp->cropped_w;
    pp_c->in_height = decpp->cropped_h;

    PPSetupScaling(pp_c, pp_out_img);

    /* HW must discard 8 pixels from input picture.
     * Notice that decpp->inwidth is always multiple of 16 and
     * decpp->cropped_w is always multiple of 8. Same for height. */
    if(decpp->inwidth != decpp->cropped_w)
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_R_E, 1);
    else
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_R_E, 0);

    if(decpp->inheight != decpp->cropped_h)
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 1);

    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_WIDTH, decpp->inwidth >> 4);
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT, decpp->inheight >> 4);

    SetPpRegister(pp_c->pp_regs, HWIF_PP_VC1_ADV_E, decpp->vc1_adv_enable);

    if(decpp->range_red) {
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_Y_E, 1);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_COEF_Y, 7 + 9);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_C_E, 1);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_COEF_C, 7 + 9);
    } else {
      SetPpRegister(pp_c->pp_regs,
                    HWIF_RANGEMAP_Y_E, decpp->range_map_yenable);
      SetPpRegister(pp_c->pp_regs,
                    HWIF_RANGEMAP_COEF_Y, decpp->range_map_ycoeff + 9);
      SetPpRegister(pp_c->pp_regs,
                    HWIF_RANGEMAP_C_E, decpp->range_map_cenable);
      SetPpRegister(pp_c->pp_regs,
                    HWIF_RANGEMAP_COEF_C, decpp->range_map_ccoeff + 9);
    }
  } else if(pp_c->dec_type == PP_PIPELINED_DEC_TYPE_MPEG4) {
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT, decpp->inheight >> 4);
    /* crop extra block if height is odd and fields */
    if(decpp->cropped_h != decpp->inheight) {
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 1);
    }
  }

  /* update output buffer when multibuffering used */
  if((pp_c->multi_buffer != 0) &&
      (pp_c->dec_type == PP_PIPELINED_DEC_TYPE_MPEG4 ||
       pp_c->dec_type == PP_PIPELINED_DEC_TYPE_MPEG2 ||
       pp_c->dec_type == PP_PIPELINED_DEC_TYPE_H264 ||
       pp_c->dec_type == PP_PIPELINED_DEC_TYPE_VC1 ||
       pp_c->dec_type == PP_PIPELINED_DEC_TYPE_RV ||
       pp_c->dec_type == PP_PIPELINED_DEC_TYPE_AVS)) {
    if(decpp->buffer_index >= pp_c->combined_mode_buffers.nbr_of_buffers) {
      PP_API_TRC
      ("PPDecStartPp # exit runtime error (out-of-range buffer index)");
      pp_c->PPCombinedRet = PP_PARAM_ERROR;
      return;
    }

    PPDecSetOutBuffer(pp_c, decpp);
  }

  /* If tiled-mode supported on PP, forward status of current picture */
  if(decpp->tiled_input_mode) {
    if(!pp_c->tiled_mode_support) {
      pp_c->PPCombinedRet = PP_DEC_RUNTIME_ERROR;
      return;
    }
    SetPpRegister( pp_c->pp_regs, HWIF_PP_TILED_MODE, decpp->tiled_input_mode );
  } else {
    SetPpRegister( pp_c->pp_regs, HWIF_PP_TILED_MODE, 0 );
  }

  if(decpp->progressive_sequence &&
      pp_out_deint->enable) {
    pp_c->PPCombinedRet = PP_DEC_RUNTIME_ERROR;
    return;
  }

  /* check that input size is same as set in PPSetConfig.
   * pp_in_img->width is rounded up to next multiple of 8 as well as cropped_w */
  if(decpp->cropped_w != pp_in_img->width) {
    PP_API_TRC("PPDecStartPp # exit runtime error");
    pp_c->PPCombinedRet = PP_DEC_RUNTIME_ERROR;
    return;
  }

  /* Double-check picture dimensions here in case pipeline turned
   * off at some point */
  {
    /* Max size for pipelined pic, 511MB for JPEG etc, 1024MB for WEBP */
    u32 max_mbs =
      ((pp_c->dec_type == PP_PIPELINED_DEC_TYPE_WEBP) ||
       ((pp_c->dec_type == PP_PIPELINED_DEC_TYPE_JPEG) &&
        pp_c->jpeg16k_support)) ?
      1024U : 511U;
    if((pp_in_img->width < PP_IN_MIN_WIDTH(decpp->use_pipeline)) ||
        (pp_in_img->height < PP_IN_MIN_HEIGHT(decpp->use_pipeline)) ||
        (pp_in_img->width > PP_IN_MAX_WIDTH_EXT(decpp->use_pipeline, max_mbs)) ||
        (pp_in_img->height > PP_IN_MAX_HEIGHT_EXT(decpp->use_pipeline, max_mbs))) {
      pp_c->PPCombinedRet = PP_DEC_RUNTIME_ERROR;
      return;
    }
  }

  if(decpp->cropped_h != pp_in_img->height) {
    PP_API_TRC("PPDecStartPp # exit runtime error");
    pp_c->PPCombinedRet = PP_DEC_RUNTIME_ERROR;
    return;
  }

  /* Set PP input image dimensions. Cropping dimensions are used
   * if Zooming is enabled in pp configuration */
  if(pp_c->pp_cfg.pp_in_crop.enable) {
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_W_EXT,
                  ((((pp_c->pp_cfg.pp_in_crop.width + 15) >> 4) & 0xE00) >> 9));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_WIDTH,
                  (((pp_c->pp_cfg.pp_in_crop.width + 15) >> 4) & 0x1FF));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_H_EXT,
                  ((((pp_c->pp_cfg.pp_in_crop.height +
                      15) >> 4) & 0x700) >> 8));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT,
                  (((pp_c->pp_cfg.pp_in_crop.height + 15) >> 4) & 0x0FF));
  }

  if(decpp->use_pipeline) {
    PP_API_TRC("pipelined #\n");
    pp_c->pipeline = 1;
    SetPpRegister(pp_c->pp_regs, HWIF_PP_PIPELINE_E, 1);

  } else {
    /* check for valid pic_struct for deinterlacing */
    if(pp_out_deint->enable &&
        ((decpp->pic_struct == DECPP_PIC_TOP_AND_BOT_FIELD_FRAME) ||
         (decpp->pic_struct == DECPP_PIC_TOP_AND_BOT_FIELD)) &&
        !pp_c->fast_vertical_downscale) {
      PPDecSetUpDeinterlace(pp_c, decpp);
    } else {
      /* disable deinterlacing for any other pic_struct */
      SetPpRegister(pp_c->pp_regs, HWIF_DEINT_E, 0);
      PPDEBUG_PRINT(("Deinterlacing DISABLED because not valid dec input!\n"));

      SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_STRUCT, decpp->pic_struct);

#if 0
      if(decpp->pic_struct == DECPP_PIC_TOP_FIELD_FRAME ||
          decpp->pic_struct == DECPP_PIC_FRAME_OR_TOP_FIELD) {

        SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_LU_BASE,
                      decpp->input_bus_luma);
        SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_CB_BASE,
                      decpp->input_bus_chroma);

      } else {
        SetPpRegister(pp_c->pp_regs, HWIF_PP_BOT_YIN_BASE,
                      decpp->bottom_bus_luma);
        SetPpRegister(pp_c->pp_regs, HWIF_PP_BOT_CIN_BASE,
                      decpp->bottom_bus_chroma);
      }
#endif
      /* set all base addresses */
      /* based on HWIF_PP_IN_STRUCT, HW will use the relevant ones only */
      SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_LU_BASE, decpp->input_bus_luma);

      if(pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_0_0) {
        /* this is workaround for standalone 4:0:0 in HW */
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_CB_BASE,
                        decpp->input_bus_luma);
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_YIN_BASE,
                        decpp->bottom_bus_luma);
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_CIN_BASE,
                        decpp->bottom_bus_luma);
      } else {
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_CB_BASE,
                        decpp->input_bus_chroma);
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_YIN_BASE,
                        decpp->bottom_bus_luma);
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_CIN_BASE,
                        decpp->bottom_bus_chroma);
      }
    }
    PP_API_TRC("non pipelined #\n");
    pp_c->pipeline = 0;
    SetPpRegister(pp_c->pp_regs, HWIF_PP_PIPELINE_E, 0);

    tmp = decpp->little_endian ?
          PP_X170_PICTURE_LITTLE_ENDIAN : PP_X170_PICTURE_BIG_ENDIAN;

    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_ENDIAN, tmp);

    tmp = decpp->word_swap ? 1 : 0;
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_SWAP32_E, tmp);
  }

  /* Only 4:2:0 and 4:0:0 inputs supported */
  pix_format_ok =
    (pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_PLANAR ||
     pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR ||
     pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_TILED ||
     pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_0_0);

  /* Check for faster downscale possibility */
  if(pp_c->fast_vertical_downscale) {
    u32 in_height;
    u32 rotation90;

    /* Determine input height */
    if(pp_c->pp_cfg.pp_in_crop.enable)
      in_height = pp_c->pp_cfg.pp_in_crop.height;
    else
      in_height = pp_in_img->height;

    /* Rotation */
    rotation90 = (pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_LEFT_90 ||
                  pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_RIGHT_90);

    /* Vertical downscaling shortcut available if:
     * - no pipeline
     * - progressive input data
     * - pixel format either 4:2:0 or 4:0:0
     * If rotation is used then shortcut affects horizontal scaling, at
     * which point we need to cross-check with fast horizontal scaling
     * mode as well.
     */
    if(pp_c->pipeline == 0 &&
        pix_format_ok &&
        !decpp->tiled_input_mode &&
        (decpp->pic_struct == PP_PIC_FRAME_OR_TOP_FIELD ||
         (pp_out_deint->enable &&
          decpp->pic_struct == PP_PIC_TOP_AND_BOT_FIELD_FRAME))) {

      /* possible */

      /* Set new image height and cropping parameters */
      SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_STRUCT, PP_PIC_TOP_FIELD_FRAME );
      SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_H_EXT,
                    ((((in_height+31) / 32) & 0x700) >> 8));
      SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT,
                    (((in_height+31) / 32) & 0x0FF));
      if(in_height & 0x1F)
        SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 1);
      else
        SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 0);

      if(pp_c->pp_cfg.pp_in_crop.enable) {
        SetPpRegister(pp_c->pp_regs, HWIF_CROP_STARTY_EXT,
                      (((pp_c->pp_cfg.pp_in_crop.origin_y / 32) & 0x700) >> 8));
        SetPpRegister(pp_c->pp_regs, HWIF_CROP_STARTY,
                      ((pp_c->pp_cfg.pp_in_crop.origin_y / 32) & 0x0FF));
      }

      /* Disable deinterlacing */
      SetPpRegister(pp_c->pp_regs, HWIF_DEINT_E, 0 );

      /* Set new scaling coefficient */
      if(rotation90) {
        /* Set fast scale mode */
        SetPpRegister(pp_c->pp_regs, HWIF_HOR_SCALE_MODE, pp_c->fast_scale_mode);
        if(pp_c->fast_horizontal_downscale)
          SetPpRegister(pp_c->pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hfast4x);
        else
          SetPpRegister(pp_c->pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hfast);
      } else {
        /* Set fast scale mode */
        SetPpRegister(pp_c->pp_regs, HWIF_VER_SCALE_MODE, pp_c->fast_scale_mode);

        SetPpRegister(pp_c->pp_regs, HWIF_HSCALE_INVRA, pp_c->c_vfast);
      }
    } else {
      /* not possible */

      SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_H_EXT,
                    ((((in_height+15) / 16) & 0x700) >> 8));
      SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT,
                    (((in_height+15) / 16) & 0x0FF));
      if(in_height & 0x0F)
        SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 1);
      else
        SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 0);

      if(pp_c->pp_cfg.pp_in_crop.enable) {
        SetPpRegister(pp_c->pp_regs, HWIF_CROP_STARTY_EXT,
                      (((pp_c->pp_cfg.pp_in_crop.origin_y / 16) & 0x700) >> 8));
        SetPpRegister(pp_c->pp_regs, HWIF_CROP_STARTY,
                      ((pp_c->pp_cfg.pp_in_crop.origin_y / 16) & 0x0FF));
      }

      if(rotation90) {
        SetPpRegister(pp_c->pp_regs, HWIF_HOR_SCALE_MODE, 2);
        if(pp_c->fast_horizontal_downscale)
          SetPpRegister(pp_c->pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hfast);
        else
          SetPpRegister(pp_c->pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hnorm);
      } else {
        SetPpRegister(pp_c->pp_regs, HWIF_VER_SCALE_MODE, 2);
        SetPpRegister(pp_c->pp_regs, HWIF_HSCALE_INVRA, pp_c->c_vnorm);
      }
    }
  }

  /* Disallow fast horizontal scale if 4:1:1 sampling is used */
  if(pp_c->fast_horizontal_downscale && !pix_format_ok ) {
    SetPpRegister(pp_c->pp_regs, HWIF_PP_FAST_SCALE_E, 0 );
    SetPpRegister(pp_c->pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hnorm);
  }

  if(PPRun(pp_c) != PP_OK) {
    PP_API_TRC("PPDecStartPp # exit PPRun failed");
    pp_c->PPCombinedRet = PP_BUSY;
    return;
  }

  pp_c->PPCombinedRet = PP_OK;
  PP_API_TRC("PPDecStartPp # exit");
  return;

}

/*------------------------------------------------------------------------------
    Function name   : PPDecConfigQueryFromDec
                        Used by decoder to ask about pp setup
    Description     : if pipeline proposed, evaluate reasons who not
    Return type     : void
    Argument        : const void *post_pinst
------------------------------------------------------------------------------*/
static void PPDecConfigQueryFromDec(const void *post_pinst,
                                    DecPpQuery * dec_pp_query) {
  PPContainer *pp_c;

  ASSERT(post_pinst != NULL);
  if(post_pinst == NULL) {
    return;
  }
  ASSERT(dec_pp_query != NULL);
  if(dec_pp_query == NULL) {
    return;
  }

  pp_c = (PPContainer *) post_pinst;

  dec_pp_query->pipeline_accepted = 1;
  dec_pp_query->pp_config_changed = 0;

  dec_pp_query->deinterlace = pp_c->pp_cfg.pp_out_deinterlace.enable ? 1 : 0;

  dec_pp_query->multi_buffer = pp_c->multi_buffer;

  if(dec_pp_query->deinterlace) {
    dec_pp_query->pipeline_accepted = 0;
  }

  /* Fast vertical downscale shortcut disables pipeline automatically; however
   * at some cases SW might still be thinking that it can be used even though
   * the input pixel format is invalid. */
  if(pp_c->fast_vertical_downscale &&
      (pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_2_0_PLANAR ||
       pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR ||
       pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_2_0_TILED ||
       pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_0_0)) {
    dec_pp_query->pipeline_accepted = 0;
  }

  if(pp_c->pp_cfg.pp_in_rotation.rotation != PP_ROTATION_NONE) {
    dec_pp_query->pipeline_accepted = 0;

  }

  if(pp_c->pp_cfg.pp_in_crop.enable != 0 &&
      pp_c->dec_type != PP_PIPELINED_DEC_TYPE_JPEG) {
    u32 tmp = dec_pp_query->pipeline_accepted;
    dec_pp_query->pipeline_accepted = 0;
    /* pipelined cropping allowed if webp width > max video width
     * or height >= 8176 px.
     * (this implies filter turned off which in turn enables
     *  pipelined cropping). */
    if((pp_c->dec_type == PP_PIPELINED_DEC_TYPE_WEBP) ||
        ((pp_c->dec_type == PP_PIPELINED_DEC_TYPE_JPEG) && pp_c->jpeg16k_support)) {
      DWLHwConfig hw_cfg;
      DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_PP);
      if( pp_c->pp_cfg.pp_in_img.width > hw_cfg.max_dec_pic_width ||
          pp_c->pp_cfg.pp_in_img.height >= 8176 ) /* rollback */
        dec_pp_query->pipeline_accepted = tmp;
    }
  }

  if(pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_NONE &&
      pp_c->prev_cfg.pp_in_rotation.rotation != PP_ROTATION_NONE) {
    dec_pp_query->pp_config_changed = 1;
  }

  /* Some formats' pipelined block size limits max vertical upscaling ratio
   * to 2.5x */
  if(pp_c->dec_type == PP_PIPELINED_DEC_TYPE_RV ||
      pp_c->dec_type == PP_PIPELINED_DEC_TYPE_VP6 ||
      pp_c->dec_type == PP_PIPELINED_DEC_TYPE_VP8 ) {
    if( (pp_c->pp_cfg.pp_out_img.height > pp_c->pp_cfg.pp_in_img.height) &&
        (2*pp_c->pp_cfg.pp_out_img.height >
         (5*pp_c->pp_cfg.pp_in_img.height-2))) {
      dec_pp_query->pipeline_accepted = 0;
    }
  }

  return;
}

/*------------------------------------------------------------------------------
    Function name   : PPDecEndCallback
    Description     : Decoder/pp cooperation ending
    Return type     : void
    Argument        : const void *post_pinst
------------------------------------------------------------------------------*/
void PPDecEndCallback(const void *post_pinst) {
  PPContainer *pp_c;
  PPResult pp_ret;

  PP_API_TRC("PPDecEndCallback #");
  pp_c = (PPContainer *) post_pinst;

  if(pp_c->PPCombinedRet == PP_OK) {
    if(pp_c->pipeline) {
      PPRefreshRegs(pp_c);

      /* turn pipeline OFF so it doesn't interfere with other instances */
      pp_c->pipeline = 0;
      SetPpRegister(pp_c->pp_regs, HWIF_PP_PIPELINE_E, 0);

      /* make sure ASIC is OFF */
      SetPpRegister(pp_c->pp_regs, HWIF_PP_E, 0);
      SetPpRegister(pp_c->pp_regs, HWIF_PP_IRQ, 0);
      SetPpRegister(pp_c->pp_regs, HWIF_PP_IRQ_STAT, 0);

      DWLDisableHw(pp_c->dwl, pp_c->core_id, PP_X170_REG_START,
                   pp_c->pp_regs[0]);

      /* H264/VP8 decoder has reserved the PP hardware for pipelined
       * operation and it will release it too
       */
      if(pp_c->dec_type != PP_PIPELINED_DEC_TYPE_H264 &&
          pp_c->dec_type != PP_PIPELINED_DEC_TYPE_VP8) {
        DWLReleaseHw(pp_c->dwl, pp_c->core_id);
      }

      PPSetStatus(pp_c, PP_STATUS_IDLE);
    } else {
      pp_ret = PPDecWaitResult(post_pinst);
      pp_c->PPCombinedRet = pp_ret;
    }
  }
  PP_API_TRC("PPDecEndCallback # exit");
}

/*------------------------------------------------------------------------------
    Function name   : PPDecSetUpDeinterlace
    Description     : Decoder/pp cooperation, set up reg for deinterlace
    Return type     : void
    Argument        : void *post_pinst, DecPpInterface
------------------------------------------------------------------------------*/
static void PPDecSetUpDeinterlace(PPContainer * pp_c,
                                  const DecPpInterface * decpp) {

  ASSERT((decpp->pic_struct == DECPP_PIC_TOP_AND_BOT_FIELD_FRAME) ||
         (decpp->pic_struct == DECPP_PIC_TOP_AND_BOT_FIELD));

  SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_STRUCT, decpp->pic_struct );

  if(pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_0_0) {
    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_LU_BASE, decpp->input_bus_luma);
    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_CB_BASE, decpp->input_bus_luma);

    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_YIN_BASE, decpp->bottom_bus_luma);
    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_CIN_BASE, decpp->bottom_bus_luma);
  } else {
    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_LU_BASE, decpp->input_bus_luma);
    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_CB_BASE, decpp->input_bus_chroma);

    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_YIN_BASE, decpp->bottom_bus_luma);
    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_CIN_BASE,
                    decpp->bottom_bus_chroma);
  }

}

#endif /* PP_DEC_PIPELINE_SUPPORT */

/*------------------------------------------------------------------------------
    Function name   : PPDecCombinedModeEnable
    Description     :
    Return type     : PPResult
    Argument        : PPInst post_pinst
    Argument        : const void *dec_inst
    Argument        : u32 dec_type
------------------------------------------------------------------------------*/
PPResult PPDecCombinedModeEnable(PPInst post_pinst, const void *dec_inst,
                                 u32 dec_type) {
  PPContainer *pp_c;
  i32 ret = ~0;

  pp_c = (PPContainer *) post_pinst;
  PP_API_TRC("Pipeline enable #");
  PP_API_TRC("Pipeline enable #");

  if(pp_c == NULL) {
    PP_API_TRC("PPDecCombinedModeEnable# ERROR: NULL pp instance");
    return PP_PARAM_ERROR;
  }

  if(dec_inst == NULL) {
    PP_API_TRC("PPDecCombinedModeEnable# ERROR: NULL decoder instance");
    return (PP_PARAM_ERROR);
  }

  if(dec_type == 0 || dec_type > 11) {
    PP_API_TRC
    ("PPDecCombinedModeEnable# ERROR: decoder type not defined correctly");
    return (PP_PARAM_ERROR);
  }

  if(PPGetStatus(pp_c) != PP_STATUS_IDLE) {
    PP_API_TRC("PPDecCombinedModeEnable# ERROR: pp status not idle");
    return PP_BUSY;
  }

  if(pp_c->dec_inst != NULL) {
    PP_API_TRC
    ("PPDecCombinedModeEnable# ERROR: PP already connected to a decoder");
    return PP_DEC_COMBINED_MODE_ERROR;
  }

  switch (dec_type) {

#ifdef PP_H264DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_H264:
    PPDEBUG_PRINT(("Pipeline enable h264 #\n"));
    ret = h264RegisterPP(dec_inst, pp_c, PPDecStartPp,
                         PPDecEndCallback, PPDecConfigQueryFromDec,
                         PPDecDisplayIndex);
    break;
#endif

#ifdef PP_MPEG4DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_MPEG4:
    PPDEBUG_PRINT(("Pipeline enable mpeg-4 #\n"));
    ret = mpeg4RegisterPP(dec_inst, pp_c, PPDecStartPp,
                          PPDecEndCallback, PPDecConfigQueryFromDec,
                          PPDecDisplayIndex, PPDecBufferData);
    break;
#endif

#ifdef PP_JPEGDEC_PIPELINE_SUPPORT
    PPDEBUG_PRINT(("Pipeline enable JPEG #\n"));
  case PP_PIPELINED_DEC_TYPE_JPEG:
    ret = jpegRegisterPP(dec_inst, pp_c, PPDecStartPp,
                         PPDecEndCallback, PPDecConfigQueryFromDec);

    break;
#endif

#ifdef PP_VC1DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_VC1:
    PPDEBUG_PRINT(("Pipeline enable vc-1 #\n"));
    ret = vc1RegisterPP(dec_inst, pp_c, PPDecStartPp,
                        PPDecEndCallback, PPDecConfigQueryFromDec,
                        PPDecDisplayIndex, PPDecBufferData);
    break;
#endif

#ifdef PP_MPEG2DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_MPEG2:
    PPDEBUG_PRINT(("Pipeline enable mpeg-2 #\n"));
    ret = mpeg2RegisterPP(dec_inst, pp_c, PPDecStartPp,
                          PPDecEndCallback, PPDecConfigQueryFromDec,
                          PPDecDisplayIndex, PPDecBufferData);
    break;
#endif

#ifdef PP_VP6DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_VP6:
    PPDEBUG_PRINT(("Pipeline enable VP6 #\n"));
    ret = vp6RegisterPP(dec_inst, pp_c, PPDecStartPp,
                        PPDecEndCallback, PPDecConfigQueryFromDec);
    break;
#endif

#ifdef PP_VP8DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_VP8:
  case PP_PIPELINED_DEC_TYPE_WEBP:
    PPDEBUG_PRINT(("Pipeline enable VP8 #\n"));
    ret = vp8RegisterPP(dec_inst, pp_c, PPDecStartPp,
                        PPDecEndCallback, PPDecConfigQueryFromDec);
    break;
#endif

#ifdef PP_RVDEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_RV:
    PPDEBUG_PRINT(("Pipeline enable real #\n"));
    ret = rvRegisterPP(dec_inst, pp_c, PPDecStartPp,
                       PPDecEndCallback, PPDecConfigQueryFromDec,
                       PPDecDisplayIndex, PPDecBufferData);
    break;
#endif

#ifdef PP_AVSDEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_AVS:
    PPDEBUG_PRINT(("Pipeline enable AVS #\n"));
    ret = avsRegisterPP(dec_inst, pp_c, PPDecStartPp,
                        PPDecEndCallback, PPDecConfigQueryFromDec,
                        PPDecDisplayIndex, PPDecBufferData);
    break;
#endif
  default:
    PP_API_TRC("PPDecCombinedModeEnable# unknown decoder type \n");
    return PP_PARAM_ERROR;
  }

  /*lint -e(527)   */
  if(ret != 0) {
    return PP_DEC_COMBINED_MODE_ERROR;
  }

  pp_c->dec_type = dec_type;
  pp_c->dec_inst = dec_inst;

  PP_API_TRC("PPDecCombinedModeEnable# OK \n");
  return PP_OK;
}

/*------------------------------------------------------------------------------
    Function name   : PPDecCombinedModeDisable
    Description     : decouple pp and dec
    Return type     : PPResult
    Argument        : PPInst post_pinst
    Argument        : const void *dec_inst
------------------------------------------------------------------------------*/
PPResult PPDecCombinedModeDisable(PPInst post_pinst, const void *dec_inst) {
  PPContainer *pp_c;

  pp_c = (PPContainer *) post_pinst;

  PP_API_TRC("PPDecCombinedModeDisable \n");
  ASSERT(pp_c != NULL && dec_inst != NULL);

  if(pp_c == NULL || dec_inst == NULL)
    return PP_PARAM_ERROR;

  if(pp_c->dec_inst != dec_inst)
    return PP_DEC_COMBINED_MODE_ERROR;

  if(PPGetStatus(pp_c) != PP_STATUS_IDLE)
    return PP_BUSY;

  switch (pp_c->dec_type) {
#ifdef PP_H264DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_H264:
    PPDEBUG_PRINT(("Pipeline disable h264 #\n"));
    (void) h264UnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_MPEG4DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_MPEG4:
    PPDEBUG_PRINT(("Pipeline disable mpeg4 #\n"));
    (void) mpeg4UnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_JPEGDEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_JPEG:
    (void) jpegUnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_VC1DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_VC1:
    PPDEBUG_PRINT(("Pipeline disable vc1 #\n"));
    (void) vc1UnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_MPEG2DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_MPEG2:
    PPDEBUG_PRINT(("Pipeline disable mpeg2 #\n"));
    (void) mpeg2UnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_RVDEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_RV:
    PPDEBUG_PRINT(("Pipeline disable real #\n"));
    (void) rvUnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_VP6DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_VP6:
    PPDEBUG_PRINT(("Pipeline disable VP6 #\n"));
    (void) vp6UnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_VP8DEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_VP8:
  case PP_PIPELINED_DEC_TYPE_WEBP:
    PPDEBUG_PRINT(("Pipeline disable VP8 #\n"));
    (void) vp8UnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

#ifdef PP_AVSDEC_PIPELINE_SUPPORT
  case PP_PIPELINED_DEC_TYPE_AVS:
    PPDEBUG_PRINT(("Pipeline disable avs #\n"));
    (void) avsUnregisterPP(pp_c->dec_inst, pp_c);
    pp_c->dec_inst = NULL;
    break;
#endif

  default:
    ASSERT(0);  /* should never get here */
    break;
  }

  SetPpRegister(pp_c->pp_regs, HWIF_PP_PIPELINE_E, 0);

  pp_c->pipeline = 0;
  pp_c->dec_inst = NULL;

  PP_API_TRC("PPDecCombinedModeDisable OK \n");
  return PP_OK;
}

PPResult PPDecSwapLastOutputBuffer(PPInst post_pinst,
                                   PPOutput *p_old_buffer,
                                   PPOutput *p_new_buffer) {
  PPContainer *pp_c;
  PPOutput *last_ppout;

  pp_c = (PPContainer *) post_pinst;

  PP_API_TRC("PPDecSwapLastOutputBuffer\n");

  if(pp_c->hw_id == 0x8170U) {
    PP_API_TRC("PPDecSwapLastOutputBuffer: Feature NOT suppported!\n");
    return PP_PARAM_ERROR;
  }

  if(pp_c == NULL || p_old_buffer == NULL || p_new_buffer == NULL)
    return PP_PARAM_ERROR;

  if(pp_c->dec_inst == NULL || (!pp_c->multi_buffer))
    return PP_PARAM_ERROR;

  if(p_new_buffer->buffer_bus_addr == 0)
    return PP_PARAM_ERROR;

  if(PPGetStatus(pp_c) != PP_STATUS_IDLE) {
    return PP_BUSY;
  }

  /* last multibuffer PP output */
  last_ppout = &pp_c->combined_mode_buffers.pp_output_buffers[pp_c->display_index];

  /* check that user swaps the last output */
  if((p_old_buffer->buffer_bus_addr != last_ppout->buffer_bus_addr) ||
      (p_old_buffer->buffer_chroma_bus_addr != last_ppout->buffer_chroma_bus_addr)) {
    return PP_PARAM_ERROR;
  }

  last_ppout->buffer_bus_addr = p_new_buffer->buffer_bus_addr;
  last_ppout->buffer_chroma_bus_addr = p_new_buffer->buffer_chroma_bus_addr;

  PP_API_TRC("PPDecSwapLastOutputBuffer OK \n");
  return PP_OK;
}


/*------------------------------------------------------------------------------
    Function name   : PPDecSetMultipleOutput
    Description     :
    Return type     :
    Argument        :
    Argument        :
------------------------------------------------------------------------------*/
PPResult PPDecSetMultipleOutput(PPInst post_pinst,
                                const PPOutputBuffers * p_buffers) {
  PPContainer *pp_c;
  u32 i = 0;

  pp_c = (PPContainer *) post_pinst;

  PP_API_TRC("PPDecSetMultipleOutput\n");

  if(pp_c->hw_id == 0x8170U) {
    PP_API_TRC("PPDecSetMultipleOutput: Feature NOT suppported!\n");
    return PP_PARAM_ERROR;
  }

  if(pp_c == NULL || p_buffers == NULL)
    return PP_PARAM_ERROR;

  if(pp_c->dec_inst == NULL)
    return PP_PARAM_ERROR;

#ifdef PP_H264DEC_PIPELINE_SUPPORT
  /* multibuffer cannot be used if PP in combined mode with H264 decoder
   * and decoder was initialized to use display smoothing */
  if (pp_c->dec_type == PP_PIPELINED_DEC_TYPE_H264 &&
      h264UseDisplaySmoothing(pp_c->dec_inst)) {
    return PP_PARAM_ERROR;
  }
#endif

  if(p_buffers->nbr_of_buffers == 0 ||
      p_buffers->nbr_of_buffers > PP_MAX_MULTIBUFFER)
    return PP_PARAM_ERROR;

  for(i = 0; i < p_buffers->nbr_of_buffers; i++)
    if(p_buffers->pp_output_buffers[i].buffer_bus_addr == 0)
      return PP_PARAM_ERROR;

  for(i = 0; i < p_buffers->nbr_of_buffers; i++)
    pp_c->buffer_data[i].setup_id = pp_c->current_setup_id;

  /* mark previously decoded pics dirty */
  pp_c->current_setup_id++;

  (void) DWLmemcpy(&pp_c->combined_mode_buffers, p_buffers,
                   sizeof(PPOutputBuffers));
  pp_c->multi_buffer = 1;

  PP_API_TRC("PPDecSetMultipleOutput OK \n");
  return PP_OK;

}

/*------------------------------------------------------------------------------
    Function name   : PPGetNextOutput
    Description     : API function for acquiring pp output location
    Return type     : PPResult
    Argument        : PPInst post_pinst
    Argument        :
------------------------------------------------------------------------------*/
PPResult PPGetNextOutput(PPInst post_pinst, PPOutput * p_out) {
  PPContainer *pp_c;
  PPResult ret = PP_OK;

  pp_c = (PPContainer *) post_pinst;

  PP_API_TRC("PPGetNextOutput\n");

  if(pp_c->hw_id == 0x8170U) {
    PP_API_TRC("PPGetNextOutput: Feature NOT suppported!\n");
    return PP_PARAM_ERROR;
  }

  if(pp_c == NULL || p_out == NULL)
    return PP_PARAM_ERROR;

  if(PPGetStatus(pp_c) != PP_STATUS_IDLE) {
    return PP_BUSY;
  }

  if(pp_c->multi_buffer) {
    u32 display_index = pp_c->display_index;

    if(display_index >= pp_c->combined_mode_buffers.nbr_of_buffers) {
      PP_API_TRC("PPGetNextOutput: Output index out of range!\n");
      return PP_DEC_RUNTIME_ERROR;
    }

    p_out->buffer_bus_addr =
      pp_c->combined_mode_buffers.pp_output_buffers[display_index].
      buffer_bus_addr;
    p_out->buffer_chroma_bus_addr =
      pp_c->combined_mode_buffers.pp_output_buffers[display_index].
      buffer_chroma_bus_addr;

    if(pp_c->current_setup_id != pp_c->buffer_data[display_index].setup_id ) {
      /* the config has changed after processing this picture */
      /* The input source information was stored here previously, so rerun */
      SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_LU_BASE,
                      pp_c->buffer_data[display_index].input_bus_luma);
      SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_IN_CB_BASE,
                      pp_c->buffer_data[display_index].input_bus_chroma);

      /* Process bottom fields also */
      if(pp_c->pp_cfg.pp_out_deinterlace.enable &&
          !pp_c->fast_vertical_downscale) {
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_YIN_BASE,
                        pp_c->buffer_data[display_index].bottom_bus_luma);
        SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_BOT_CIN_BASE,
                        pp_c->buffer_data[display_index].bottom_bus_chroma);
      }

      SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_OUT_LU_BASE,
                      pp_c->combined_mode_buffers.pp_output_buffers[display_index].
                      buffer_bus_addr);
      SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_OUT_CH_BASE,
                      pp_c->combined_mode_buffers.pp_output_buffers[display_index].
                      buffer_chroma_bus_addr);

      /* rerun picture because the configuration has changed */
      PP_API_TRC("PPGetNextOutput: Config changed... rerun\n");
      if(PPRun(pp_c) != PP_OK) {
        return PP_BUSY;
      }
      ret = WaitForPp(pp_c);
    }
  } else {
    p_out->buffer_bus_addr = pp_c->pp_cfg.pp_out_img.buffer_bus_addr;
    p_out->buffer_chroma_bus_addr = pp_c->pp_cfg.pp_out_img.buffer_chroma_bus_addr;
  }

  PP_API_TRC("PPGetNextOutput end\n");
  return ret;
}

#ifdef PP_DEC_PIPELINE_SUPPORT
/*------------------------------------------------------------------------------
    Function name   : PPDecSetOutBuffer
    Description     : set up multibuffer
    Return type     : void
    Argument        : container
------------------------------------------------------------------------------*/

#ifdef ASIC_TRACE_SUPPORT
extern u8 *p_display_y;
extern u8 *p_display_c;
#endif

static void PPDecSetOutBuffer(PPContainer * pp_c, const DecPpInterface * decpp) {

  PPOutImage *pp_out_img;

  pp_out_img = &pp_c->pp_cfg.pp_out_img;

  ASSERT(decpp->buffer_index < pp_c->combined_mode_buffers.nbr_of_buffers);

  pp_out_img->buffer_bus_addr =
    pp_c->combined_mode_buffers.pp_output_buffers[decpp->buffer_index].
    buffer_bus_addr;
  pp_out_img->buffer_chroma_bus_addr =
    pp_c->combined_mode_buffers.pp_output_buffers[decpp->buffer_index].
    buffer_chroma_bus_addr;

  pp_c->display_index = decpp->display_index;

#ifdef ASIC_TRACE_SUPPORT
  p_display_y = pp_out_img->buffer_bus_addr;
  p_display_c = pp_out_img->buffer_chroma_bus_addr;
#endif

  /* output buffer setup */
  SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_OUT_LU_BASE,
                  (pp_out_img->buffer_bus_addr +
                   pp_c->frm_buffer_luma_or_rgb_offset));

  /* chromas not needed for RGB and YUYV 422 out */
  if(pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR) {
    SET_PP_ADDR_REG(pp_c->pp_regs, HWIF_PP_OUT_CH_BASE,
                    (pp_out_img->buffer_chroma_bus_addr +
                     pp_c->frm_buffer_chroma_offset));
  }

  /* store some info if the config is changed and we need to rerun the pic */
  pp_c->buffer_data[decpp->buffer_index].setup_id = pp_c->current_setup_id;

  /* Code below is required for H264 multibuffer support, but must not
   * be used on any formats as they have different handling of PP input
   * buffers. */

#ifdef PP_H264DEC_PIPELINE_SUPPORT
  pp_c->buffer_data[decpp->buffer_index].input_bus_luma = decpp->input_bus_luma;
  pp_c->buffer_data[decpp->buffer_index].input_bus_chroma = decpp->input_bus_chroma;
  pp_c->buffer_data[decpp->buffer_index].bottom_bus_luma = decpp->bottom_bus_luma;
  pp_c->buffer_data[decpp->buffer_index].bottom_bus_chroma = decpp->bottom_bus_chroma;
#endif /* PP_H264DEC_PIPELINE_SUPPORT */

}

/*------------------------------------------------------------------------------
    Function name   : PPDecDisplayIndex
    Description     : set next output in display order
    Return type     : void
    Argument        : container
------------------------------------------------------------------------------*/
#if defined(PP_MPEG4DEC_PIPELINE_SUPPORT) ||\
    defined(PP_H264DEC_PIPELINE_SUPPORT) ||\
    defined(PP_VC1DEC_PIPELINE_SUPPORT) ||\
    defined(PP_MPEG2DEC_PIPELINE_SUPPORT) ||\
    defined(PP_RVDEC_PIPELINE_SUPPORT) ||\
    defined(PP_AVSDEC_PIPELINE_SUPPORT)
void PPDecDisplayIndex(const void *post_pinst, u32 index) {
  PPContainer *pp_c;

  PP_API_TRC("PPDecDisplayIndex #");
  pp_c = (PPContainer *) post_pinst;

  ASSERT(index < pp_c->combined_mode_buffers.nbr_of_buffers);

  pp_c->display_index = index;

  return;
}
#endif

/*------------------------------------------------------------------------------
    Function name   : PPDecBufferData
    Description     : Store some info if the config is changed
    Return type     : void
    Argument        : container
------------------------------------------------------------------------------*/
#if defined(PP_MPEG4DEC_PIPELINE_SUPPORT) ||\
    defined(PP_VC1DEC_PIPELINE_SUPPORT) ||\
    defined(PP_MPEG2DEC_PIPELINE_SUPPORT) ||\
    defined(PP_RVDEC_PIPELINE_SUPPORT) ||\
    defined(PP_AVSDEC_PIPELINE_SUPPORT)
void PPDecBufferData(const void *post_pinst, u32 buffer_index,
                     addr_t input_bus_luma, addr_t input_bus_chroma,
                     addr_t bottom_bus_luma, addr_t bottom_bus_chroma ) {
  PPContainer *pp_c;

  PP_API_TRC("PPDecBufferData #");
  pp_c = (PPContainer *) post_pinst;

  ASSERT(buffer_index < pp_c->combined_mode_buffers.nbr_of_buffers);

  /* store some info if the config is changed and we need to rerun the pic */
  pp_c->buffer_data[buffer_index].setup_id = pp_c->current_setup_id;
  pp_c->buffer_data[buffer_index].input_bus_luma = input_bus_luma;
  pp_c->buffer_data[buffer_index].input_bus_chroma = input_bus_chroma;
  pp_c->buffer_data[buffer_index].bottom_bus_luma = bottom_bus_luma;
  pp_c->buffer_data[buffer_index].bottom_bus_chroma = bottom_bus_chroma;

  return;
}
#endif

#endif /*PP_DEC_PIPELINE_SUPPORT */
