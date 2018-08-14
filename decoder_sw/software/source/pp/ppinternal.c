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
#include "ppapi.h"
#include "ppinternal.h"
#include "dwl.h"
#include "regdrv_g1.h"
#include "ppdebug.h"
#include "ppcfg.h"
#include "decapicommon.h"

static void PPSetFrmBufferWriting(PPContainer * pp_c);
static void PPSetRgbBitmask(PPContainer * pp_c);
static void PPSetRgbTransformCoeffs(PPContainer * pp_c);
static void PPSetDithering(PPContainer * pp_c);

static u32 PPIsInPixFmtOk(u32 pix_fmt, const PPContainer * pp_c);
static u32 PPIsOutPixFmtOk(u32 pix_fmt, const PPContainer * pp_c);

static i32 PPCheckAllWidthParams(PPConfig * pp_cfg, u32 blend_ena, u32 pix_acc,
                                 u32 ablend_crop);
static i32 PPCheckAllHeightParams(PPConfig * pp_cfg, u32 pix_acc);

static u32 PPFindFirstNonZeroBit(u32 mask);
static void PPSetRgbBitmaskCustom(PPContainer * pp_c, u32 rgb16);

static i32 PPContinuousCheck(u32 value);
static i32 PPCheckOverlapping(u32 a, u32 b, u32 c, u32 d);

static u32 PPCountOnes(u32 value);
static u32 PPSelectDitheringValue(u32 mask);

#if (PP_X170_DATA_BUS_WIDTH != 4) && (PP_X170_DATA_BUS_WIDTH != 8)
#error "Bad data bus width specified PP_X170_DATA_BUS_WIDTH"
#endif

#define PP_FAST_SCALING_UNINITIALIZED   (0)
#define PP_FAST_SCALING_SUPPORTED       (1)
#define PP_FAST_SCALING_NOT_SUPPORTED   (2)

/*------------------------------------------------------------------------------
    Function name   : PPGetStatus
    Description     :
    Return type     : u32
    Argument        : PPContainer *pp_c
------------------------------------------------------------------------------*/
u32 PPGetStatus(const PPContainer * pp_c) {
  ASSERT(pp_c != NULL);
  return pp_c->status;
}

/*------------------------------------------------------------------------------
    Function name   : PPSetStatus
    Description     :
    Return type     : void
    Argument        : PPContainer *pp_c
    Argument        : u32 status
------------------------------------------------------------------------------*/
void PPSetStatus(PPContainer * pp_c, u32 status) {
  ASSERT(pp_c != NULL);
  pp_c->status = status;
}

/*------------------------------------------------------------------------------
    Function name   : PPRefreshRegs
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPRefreshRegs(PPContainer * pp_c) {
  i32 i;
  u32 offset = PP_X170_REG_START;

  u32 *pp_regs = pp_c->pp_regs;

  for(i = PP_X170_REGISTERS; i > 0; i--) {
    *pp_regs++ = DWLReadReg(pp_c->dwl, pp_c->core_id, offset);
    offset += 4;
  }
#ifdef USE_64BIT_ENV
  offset = PP_X170_EXPAND_REG_START;
  pp_regs = pp_c->pp_regs + ((PP_X170_EXPAND_REG_START - PP_X170_REG_START) >> 2);
  for(i = PP_X170_EXPAND_REGS; i > 0; i--) {
    *pp_regs++ = DWLReadReg(pp_c->dwl, pp_c->core_id, offset);
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : PPFlushRegs
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPFlushRegs(PPContainer * pp_c) {
  i32 i;
  u32 offset = PP_X170_REG_START;
  u32 *pp_regs = pp_c->pp_regs;

  for(i = PP_X170_REGISTERS; i > 0; i--) {
    DWLWriteReg(pp_c->dwl, pp_c->core_id, offset, *pp_regs);
    pp_regs++;
    offset += 4;
  }
#ifdef USE_64BIT_ENV
  offset = PP_X170_EXPAND_REG_START;
  pp_regs = pp_c->pp_regs + ((PP_X170_EXPAND_REG_START - PP_X170_REG_START) >> 2) ;
  for(i = PP_X170_EXPAND_REGS; i > 0; i--) {
    DWLWriteReg(pp_c->dwl, pp_c->core_id, offset, *pp_regs);
    pp_regs++;
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : PPInitHW
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPInitHW(PPContainer * pp_c) {
  DWLHwConfig hw_cfg;
  u32 asic_id;
  u32 *pp_regs = pp_c->pp_regs;

  (void) DWLmemset(pp_regs, 0, TOTAL_X170_REGISTERS * sizeof(*pp_regs));

#if( PP_X170_USING_IRQ == 0 )
  SetPpRegister(pp_regs, HWIF_PP_IRQ_DIS, 1);
#endif

#if (PP_X170_OUTPUT_PICTURE_ENDIAN > 1)
#error "Bad value specified for PP_X170_OUTPUT_PICTURE_ENDIAN"
#endif

#if (PP_X170_INPUT_PICTURE_ENDIAN > 1)
#error "Bad value specified for PP_X170_INPUT_PICTURE_ENDIAN"
#endif

#if (PP_X170_BUS_BURST_LENGTH > 31)
#error "Bad value specified for PP_X170_BUS_BURST_LENGTH"
#endif

  SetPpRegister(pp_regs, HWIF_PP_IN_ENDIAN, PP_X170_INPUT_PICTURE_ENDIAN);
  SetPpRegister(pp_regs, HWIF_PP_MAX_BURST, PP_X170_BUS_BURST_LENGTH);

  DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_PP);

  /* Check to see if HW supports extended endianess modes */
  pp_c->hw_endian_ver = (hw_cfg.pp_config & PP_OUTP_ENDIAN) ? 1 : 0;
  pp_c->tiled_mode_support = (hw_cfg.pp_config & PP_TILED_INPUT) >> 14;
  pp_c->jpeg16k_support = hw_cfg.webp_support; /* flag reused for JPEG as
                                              * well */

  asic_id = DWLReadAsicID(DWL_CLIENT_TYPE_PP);
  if((asic_id & ~0x7) == 0x67311148) /* Workaround needed for this HW build */
    pp_c->c_hworkaround_flag = 1;
  else
    pp_c->c_hworkaround_flag = 0;

#if ( PP_X170_DATA_DISCARD_ENABLE != 0 )
  SetPpRegister(pp_regs, HWIF_PP_DATA_DISC_E, 1);
#else
  SetPpRegister(pp_regs, HWIF_PP_DATA_DISC_E, 0);
#endif

  /* Note; output endianess settings moved to PPSetupHW(), these left
   * here as reminder and placeholder for possible future updates */
  /*
      SetPpRegister(pp_regs, HWIF_PP_OUT_ENDIAN, PP_X170_OUTPUT_PICTURE_ENDIAN);

  #if ( PP_X170_SWAP_32_WORDS != 0 )
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 1);
  #else
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 0);
  #endif

      if( pp_c->hw_endian_ver )
      {
  #if ( PP_X170_SWAP_16_WORDS != 0 )
          SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 1);
  #else
          SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 0);
  #endif
      }
      */

#if ( PP_X170_SWAP_32_WORDS_INPUT != 0 )
  SetPpRegister(pp_regs, HWIF_PP_IN_SWAP32_E, 1);
  SetPpRegister(pp_regs, HWIF_PP_IN_A1_SWAP32, 1);
#else
  SetPpRegister(pp_regs, HWIF_PP_IN_SWAP32_E, 0);
  SetPpRegister(pp_regs, HWIF_PP_IN_A1_SWAP32, 0);
#endif

#if ( PP_X170_INTERNAL_CLOCK_GATING != 0 )
  SetPpRegister(pp_regs, HWIF_PP_CLK_GATE_E, 1);
#else
  SetPpRegister(pp_regs, HWIF_PP_CLK_GATE_E, 0);
#endif

  /* set AXI RW IDs */
  SetPpRegister(pp_regs, HWIF_PP_AXI_RD_ID, (PP_X170_AXI_ID_R & 0xFFU));
  SetPpRegister(pp_regs, HWIF_PP_AXI_WR_ID, (PP_X170_AXI_ID_W & 0xFFU));

  SetPpRegister(pp_regs, HWIF_PP_SCMD_DIS, PP_X170_SCMD_DISABLE);

  /* use alpha blend source 1 endian mode for alpha blend source 2 */
  SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_A2_ENDSEL, 1);


  return;
}

/*------------------------------------------------------------------------------
    Function name   : PPInitDataStructures
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPInitDataStructures(PPContainer * pp_c) {
  PPOutImage *pp_out_img;
  PPInImage *pp_in_img;

  PPOutRgb *pp_out_rgb;

  ASSERT(pp_c != NULL);

  (void) DWLmemset(&pp_c->pp_cfg, 0, sizeof(PPConfig));

  pp_out_img = &pp_c->pp_cfg.pp_out_img;
  pp_in_img = &pp_c->pp_cfg.pp_in_img;

  pp_out_rgb = &pp_c->pp_cfg.pp_out_rgb;

  pp_in_img->width = 720;
  pp_in_img->height = 576;
  pp_in_img->pix_format = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;

  pp_c->in_format = PP_ASIC_IN_FORMAT_420_SEMIPLANAR;

  pp_out_img->width = 720;
  pp_out_img->height = 576;
  pp_out_img->pix_format = PP_PIX_FMT_RGB32;

  pp_c->out_format = PP_ASIC_OUT_FORMAT_RGB;
  pp_c->rgb_depth = 32;

  pp_out_rgb->rgb_transform = PP_YCBCR2RGB_TRANSFORM_BT_601;

  pp_out_rgb->rgb_transform_coeffs.a = 298;
  pp_out_rgb->rgb_transform_coeffs.b = 409;
  pp_out_rgb->rgb_transform_coeffs.c = 208;
  pp_out_rgb->rgb_transform_coeffs.d = 100;
  pp_out_rgb->rgb_transform_coeffs.e = 516;

  pp_c->frm_buffer_luma_or_rgb_offset = 0;
  pp_c->frm_buffer_chroma_offset = 0;

}

/*------------------------------------------------------------------------------
    Function name   : PPSetupHW
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPSetupHW(PPContainer * pp_c) {

  PPOutMask1 *pp_out_mask1;
  PPOutMask2 *pp_out_mask2;
  PPOutImage *pp_out_img;
  PPInImage *pp_in_img;
  PPInCropping *pp_in_crop;
  PPOutDeinterlace *pp_out_deint;
  PPOutRgb *pp_out_rgb;

  PPInRotation *pp_in_rot;

  u32 *pp_regs;

  ASSERT(pp_c != NULL);

  pp_out_mask1 = &pp_c->pp_cfg.pp_out_mask1;
  pp_out_mask2 = &pp_c->pp_cfg.pp_out_mask2;
  pp_out_img = &pp_c->pp_cfg.pp_out_img;
  pp_in_img = &pp_c->pp_cfg.pp_in_img;
  pp_in_crop = &pp_c->pp_cfg.pp_in_crop;
  pp_out_deint = &pp_c->pp_cfg.pp_out_deinterlace;

  pp_in_rot = &pp_c->pp_cfg.pp_in_rotation;
  pp_out_rgb = &pp_c->pp_cfg.pp_out_rgb;

  pp_regs = pp_c->pp_regs;
  /* frame buffer setup */
  PPSetFrmBufferWriting(pp_c);

  /* setup output endianess */
  if( (pp_c->out_format == PP_ASIC_OUT_FORMAT_RGB) &&
      pp_c->hw_endian_ver ) {
    u32 is_rgb16 = (pp_c->rgb_depth == 16);
    SetPpRegister(pp_regs, HWIF_PP_OUT_ENDIAN,
                  is_rgb16 ? PP_X170_OUTPUT_PICTURE_ENDIAN_RGB16 :
                  PP_X170_OUTPUT_PICTURE_ENDIAN_RGB32 );

    if(is_rgb16) {
#if ( PP_X170_SWAP_32_WORDS_RGB16 != 0 )
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 1);
#else
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 0);
#endif

#if ( PP_X170_SWAP_16_WORDS_RGB16 != 0 )
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 1);
#else
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 0);
#endif
    } else {
#if ( PP_X170_SWAP_32_WORDS_RGB32 != 0 )
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 1);
#else
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 0);
#endif

#if ( PP_X170_SWAP_16_WORDS_RGB32 != 0 )
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 1);
#else
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 0);
#endif
    }
  } else { /* Output not RGB, or no extended endian support */
    SetPpRegister(pp_regs, HWIF_PP_OUT_ENDIAN, PP_X170_OUTPUT_PICTURE_ENDIAN);

#if ( PP_X170_SWAP_32_WORDS != 0 )
    SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 1);
#else
    SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP32_E, 0);
#endif

    if( pp_c->hw_endian_ver ) {
#if ( PP_X170_SWAP_16_WORDS != 0 )
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 1);
#else
      SetPpRegister(pp_regs, HWIF_PP_OUT_SWAP16_E, 0);
#endif
    }
  }

  /* output buffer setup */
  SET_PP_ADDR_REG(pp_regs, HWIF_PP_OUT_LU_BASE,
                  (pp_out_img->buffer_bus_addr +
                   pp_c->frm_buffer_luma_or_rgb_offset));

  /* chromas not needed for RGB and YUYV 422 out */
  if(pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR) {
    SET_PP_ADDR_REG(pp_regs, HWIF_PP_OUT_CH_BASE,
                    (pp_out_img->buffer_chroma_bus_addr +
                     pp_c->frm_buffer_chroma_offset));
  }

  SetPpRegister(pp_regs, HWIF_PP_OUT_FORMAT, pp_c->out_format);
  if(pp_c->out_format == PP_ASIC_OUT_FORMAT_422) {
    SetPpRegister(pp_regs, HWIF_PP_OUT_START_CH, pp_c->out_start_ch);
    SetPpRegister(pp_regs, HWIF_PP_OUT_CR_FIRST, pp_c->out_cr_first);
    SetPpRegister(pp_regs, HWIF_PP_OUT_TILED_E, pp_c->out_tiled4x4 );
  }
  SetPpRegister(pp_regs, HWIF_PP_OUT_WIDTH, pp_out_img->width);
  SetPpRegister(pp_regs, HWIF_PP_OUT_HEIGHT, pp_out_img->height);
  SetPpRegister(pp_regs, HWIF_PP_OUT_W_EXT, pp_out_img->width >> 11);
  SetPpRegister(pp_regs, HWIF_PP_OUT_H_EXT, pp_out_img->height >> 11);

  /* deinterlacing parameters */
  SetPpRegister(pp_regs, HWIF_DEINT_E, pp_out_deint->enable);

  if(pp_out_deint->enable) {
    /* deinterlacing default parameters */
    SetPpRegister(pp_regs, HWIF_DEINT_BLEND_E, 0);
    SetPpRegister(pp_regs, HWIF_DEINT_THRESHOLD, 25);
    SetPpRegister(pp_regs, HWIF_DEINT_EDGE_DET, 25);
  }

  /* input setup */
  if(pp_c->dec_inst == NULL) {
    SetPpRegister(pp_regs, HWIF_PP_IN_STRUCT, pp_in_img->pic_struct);

    if(pp_in_img->pic_struct != PP_PIC_BOT_FIELD &&
        pp_in_img->pic_struct != PP_PIC_BOT_FIELD_FRAME) {
      SET_PP_ADDR_REG(pp_regs, HWIF_PP_IN_LU_BASE, pp_in_img->buffer_bus_addr);
      SET_PP_ADDR_REG(pp_regs, HWIF_PP_IN_CB_BASE, pp_in_img->buffer_cb_bus_addr);
    }

    if(pp_in_img->pic_struct != PP_PIC_FRAME_OR_TOP_FIELD &&
        pp_in_img->pic_struct != PP_PIC_TOP_FIELD_FRAME) {
      SET_PP_ADDR_REG(pp_regs, HWIF_PP_BOT_YIN_BASE,
                      pp_in_img->buffer_bus_addr_bot);
      SET_PP_ADDR_REG(pp_regs, HWIF_PP_BOT_CIN_BASE,
                      pp_in_img->buffer_bus_addr_ch_bot);
    }

    if(pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_PLANAR) {
      SET_PP_ADDR_REG(pp_regs, HWIF_PP_IN_CR_BASE, pp_in_img->buffer_cr_bus_addr);
    }
  }

  SetPpRegister(pp_regs, HWIF_EXT_ORIG_WIDTH, (pp_in_img->width + 15) / 16);
  if(pp_c->in_format < PP_ASIC_IN_FORMAT_EXTENSION)
    SetPpRegister(pp_regs, HWIF_PP_IN_FORMAT, pp_c->in_format);
  else {
    SetPpRegister(pp_regs, HWIF_PP_IN_FORMAT, PP_ASIC_IN_FORMAT_EXTENSION);
    SetPpRegister(pp_regs, HWIF_PP_IN_FORMAT_ES,
                  pp_c->in_format - PP_ASIC_IN_FORMAT_EXTENSION);
  }

  if(pp_c->in_format == PP_ASIC_IN_FORMAT_422) {
    SetPpRegister(pp_regs, HWIF_PP_IN_START_CH, pp_c->in_start_ch);
    SetPpRegister(pp_regs, HWIF_PP_IN_CR_FIRST, pp_c->in_cr_first);
  }

  if(!pp_in_crop->enable) {
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_W_EXT,
                  (((pp_in_img->width / 16) & 0xE00) >> 9));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_WIDTH,
                  ((pp_in_img->width / 16) & 0x1FF));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_H_EXT,
                  (((pp_in_img->height / 16) & 0x700) >> 8));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT,
                  ((pp_in_img->height / 16) & 0x0FF));
    SetPpRegister(pp_regs, HWIF_CROP_STARTX_EXT, 0);
    SetPpRegister(pp_regs, HWIF_CROP_STARTX, 0);
    SetPpRegister(pp_regs, HWIF_CROP_STARTY_EXT, 0);
    SetPpRegister(pp_regs, HWIF_CROP_STARTY, 0);
    SetPpRegister(pp_regs, HWIF_PP_CROP8_R_E, 0);
    SetPpRegister(pp_regs, HWIF_PP_CROP8_D_E, 0);

    pp_c->in_width = pp_in_img->width;
    pp_c->in_height = pp_in_img->height;
  } else {
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_W_EXT,
                  ((((pp_in_crop->width + 15) / 16) & 0xE00) >> 9));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_WIDTH,
                  (((pp_in_crop->width + 15) / 16) & 0x1FF));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_H_EXT,
                  ((((pp_in_crop->height + 15) / 16) & 0x700) >> 8));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT,
                  (((pp_in_crop->height + 15) / 16) & 0x0FF));
    SetPpRegister(pp_regs, HWIF_CROP_STARTX_EXT,
                  (((pp_in_crop->origin_x / 16) & 0xE00) >> 9));
    SetPpRegister(pp_regs, HWIF_CROP_STARTX,
                  ((pp_in_crop->origin_x / 16) & 0x1FF));
    SetPpRegister(pp_regs, HWIF_CROP_STARTY_EXT,
                  (((pp_in_crop->origin_y / 16) & 0x700) >> 8));
    SetPpRegister(pp_regs, HWIF_CROP_STARTY,
                  ((pp_in_crop->origin_y / 16) & 0x0FF));

    if(pp_in_crop->width & 0x0F) {
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_R_E, 1);
    } else {
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_R_E, 0);
    }

    if(pp_in_crop->height & 0x0F) {
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 1);
    } else {
      SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 0);
    }

    pp_c->in_width = pp_in_crop->width;
    pp_c->in_height = pp_in_crop->height;
  }

  /* setup scaling */
  PPSetupScaling(pp_c, pp_out_img);

  /* YUV range */
  SetPpRegister(pp_regs, HWIF_YCBCR_RANGE, pp_in_img->video_range);

  if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB_MASK) {
    /* setup RGB conversion */
    PPSetRgbTransformCoeffs(pp_c);

    if(pp_out_rgb->dithering_enable) {
      PPSetDithering(pp_c);
    }
    /* setup RGB bitmasks */
    PPSetRgbBitmask(pp_c);

  }

  if(pp_c->dec_inst == NULL) {
    /* set up range expansion/mapping */

    SetPpRegister(pp_c->pp_regs, HWIF_PP_VC1_ADV_E,
                  pp_in_img->vc1_advanced_profile);

    if(pp_in_img->vc1_range_red_frm) {
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_Y_E, 1);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_COEF_Y, 7 + 9);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_C_E, 1);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_COEF_C, 7 + 9);
    } else {
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_Y_E,
                    pp_in_img->vc1_range_map_yenable);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_COEF_Y,
                    pp_in_img->vc1_range_map_ycoeff + 9);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_C_E,
                    pp_in_img->vc1_range_map_cenable);
      SetPpRegister(pp_c->pp_regs, HWIF_RANGEMAP_COEF_C,
                    pp_in_img->vc1_range_map_ccoeff + 9);
    }
    /* for pipeline, this is set up in PipelineStart */
  }

  /* setup rotation/flip */
  SetPpRegister(pp_regs, HWIF_ROTATION_MODE, pp_in_rot->rotation);

  /* setup masks */
  SetPpRegister(pp_regs, HWIF_MASK1_E, pp_out_mask1->enable);

  /* Alpha blending mask 1 */
  if(pp_out_mask1->enable && pp_out_mask1->alpha_blend_ena && pp_c->blend_ena) {
    SetPpRegister(pp_regs, HWIF_MASK1_ABLEND_E, 1);
    if(pp_c->blend_crop_support) {
      addr_t blend_component_base = pp_out_mask1->blend_component_base;
      blend_component_base += 4*(
                                pp_out_mask1->blend_origin_x +
                                pp_out_mask1->blend_origin_y * pp_out_mask1->blend_width);

      SET_PP_ADDR_REG(pp_regs, HWIF_ABLEND1_BASE, blend_component_base);
      SetPpRegister(pp_regs, HWIF_ABLEND1_SCANL, pp_out_mask1->blend_width );

    } else { /* legacy mode */
      SET_PP_ADDR_REG(pp_regs, HWIF_ABLEND1_BASE,
                      pp_out_mask1->blend_component_base);
    }
  } else {
    SetPpRegister(pp_regs, HWIF_MASK1_ABLEND_E, 0);
  }

  if(pp_out_mask1->enable) {
    u32 start_x, start_y;
    i32 end_x, end_y;

    if(pp_out_mask1->origin_x < 0) {
      start_x = 0;
    } else if(pp_out_mask1->origin_x > (i32) pp_out_img->width) {
      start_x = pp_out_img->width;
    } else {
      start_x = (u32) pp_out_mask1->origin_x;
    }

    SetPpRegister(pp_regs, HWIF_MASK1_STARTX, start_x);
    SetPpRegister(pp_regs, HWIF_MASK1_STARTX_EXT, start_x >> 11);

    if(pp_out_mask1->origin_y < 0) {
      start_y = 0;
    } else if(pp_out_mask1->origin_y > (i32) pp_out_img->height) {
      start_y = pp_out_img->height;
    } else {
      start_y = (u32) pp_out_mask1->origin_y;
    }

    SetPpRegister(pp_regs, HWIF_MASK1_STARTY, start_y);
    SetPpRegister(pp_regs, HWIF_MASK1_STARTY_EXT, start_y >> 11);

    end_x = pp_out_mask1->origin_x + (i32) pp_out_mask1->width;
    if(end_x > (i32) pp_out_img->width) {
      end_x = (i32) pp_out_img->width;
    } else if(end_x < 0) {
      end_x = 0;
    }

    SetPpRegister(pp_regs, HWIF_MASK1_ENDX, (u32) end_x);
    SetPpRegister(pp_regs, HWIF_MASK1_ENDX_EXT, (u32) end_x >> 11);

    end_y = pp_out_mask1->origin_y + (i32) pp_out_mask1->height;
    if(end_y > (i32) pp_out_img->height) {
      end_y = (i32) pp_out_img->height;
    } else if(end_y < 0) {
      end_y = 0;
    }

    SetPpRegister(pp_regs, HWIF_MASK1_ENDY, (u32) end_y);
    SetPpRegister(pp_regs, HWIF_MASK1_ENDY_EXT, (u32) end_y >> 11);
  }

  SetPpRegister(pp_regs, HWIF_MASK2_E, pp_out_mask2->enable);

  /* Alpha blending mask 2 */
  if(pp_out_mask2->enable && pp_out_mask2->alpha_blend_ena && pp_c->blend_ena) {
    SetPpRegister(pp_regs, HWIF_MASK2_ABLEND_E, 1);
    if(pp_c->blend_crop_support) {
      addr_t blend_component_base = pp_out_mask2->blend_component_base;
      blend_component_base += 4*(
                                pp_out_mask2->blend_origin_x +
                                pp_out_mask2->blend_origin_y * pp_out_mask2->blend_width);

      SET_PP_ADDR_REG(pp_regs, HWIF_ABLEND2_BASE, blend_component_base);
      SetPpRegister(pp_regs, HWIF_ABLEND2_SCANL, pp_out_mask2->blend_width );

    } else { /* legacy mode */
      SET_PP_ADDR_REG(pp_regs, HWIF_ABLEND2_BASE,
                      pp_out_mask2->blend_component_base);
    }
  } else {
    SetPpRegister(pp_regs, HWIF_MASK2_ABLEND_E, 0);
  }

  if(pp_out_mask2->enable) {
    u32 start_x, start_y;
    i32 end_x, end_y;

    if(pp_out_mask2->origin_x < 0) {
      start_x = 0;
    } else if(pp_out_mask2->origin_x > (i32) pp_out_img->width) {
      start_x = pp_out_img->width;
    } else {
      start_x = (u32) pp_out_mask2->origin_x;
    }

    SetPpRegister(pp_regs, HWIF_MASK2_STARTX, start_x);
    SetPpRegister(pp_regs, HWIF_MASK2_STARTX_EXT, start_x >> 11);

    if(pp_out_mask2->origin_y < 0) {
      start_y = 0;
    } else if(pp_out_mask2->origin_y > (i32) pp_out_img->height) {
      start_y = pp_out_img->height;
    } else {
      start_y = (u32) pp_out_mask2->origin_y;
    }

    SetPpRegister(pp_regs, HWIF_MASK2_STARTY, start_y);
    SetPpRegister(pp_regs, HWIF_MASK2_STARTY_EXT, start_y >> 11);

    end_x = pp_out_mask2->origin_x + (i32) pp_out_mask2->width;
    if(end_x > (i32) pp_out_img->width) {
      end_x = (i32) pp_out_img->width;
    } else if(end_x < 0) {
      end_x = 0;
    }

    SetPpRegister(pp_regs, HWIF_MASK2_ENDX, (u32) end_x);
    SetPpRegister(pp_regs, HWIF_MASK2_ENDX_EXT, (u32) end_x >> 11);

    end_y = pp_out_mask2->origin_y + (i32) pp_out_mask2->height;
    if(end_y > (i32) pp_out_img->height) {
      end_y = (i32) pp_out_img->height;
    } else if(end_y < 0) {
      end_y = 0;
    }

    SetPpRegister(pp_regs, HWIF_MASK2_ENDY, (u32) end_y);
    SetPpRegister(pp_regs, HWIF_MASK2_ENDY_EXT, (u32) end_y >> 11);
  }
}

/*------------------------------------------------------------------------------
    Function name   : PPCheckTiledOutput
    Description     :
    Return type     : i32
    Argument        : PPConfig *pp_cfg
------------------------------------------------------------------------------*/
i32 PPCheckTiledOutput( PPConfig *pp_cfg ) {
  i32 ret = PP_OK;
  PPOutImage *pp_out_img;
  PPOutFrameBuffer *pp_out_frm_buffer;
  PPOutMask1 *pp_out_mask1;
  PPOutMask2 *pp_out_mask2;

  u32 mask = 0x3; /* fixed for 4x4 blocks */

  pp_out_img = &pp_cfg->pp_out_img;
  pp_out_frm_buffer = &pp_cfg->pp_out_frm_buffer;
  pp_out_mask1 = &pp_cfg->pp_out_mask1;
  pp_out_mask2 = &pp_cfg->pp_out_mask2;

  /* Assert output image format */
  if( pp_out_img->pix_format != PP_PIX_FMT_YCBCR_4_2_2_TILED_4X4 &&
      pp_out_img->pix_format != PP_PIX_FMT_YCRYCB_4_2_2_TILED_4X4 &&
      pp_out_img->pix_format != PP_PIX_FMT_CBYCRY_4_2_2_TILED_4X4 &&
      pp_out_img->pix_format != PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4 ) {
    ret = (i32) PP_SET_OUT_FORMAT_INVALID;
  }

  /* All scaling boundaries must be 4x4 */
  if(pp_out_img->width & mask)
    ret = (i32) PP_SET_OUT_SIZE_INVALID;
  if(pp_out_img->height & mask)
    ret = (i32) PP_SET_OUT_SIZE_INVALID;

  /* All mask boundaries must be 4x4 */
  if(pp_out_mask1->enable) {
    if(pp_out_mask1->width & mask)
      ret = (i32) PP_SET_MASK1_INVALID;
    if(pp_out_mask1->origin_x & mask)
      ret = (i32) PP_SET_MASK1_INVALID;
    if(pp_out_mask1->height & mask)
      ret = (i32) PP_SET_MASK1_INVALID;
    if(pp_out_mask1->origin_y & mask)
      ret = (i32) PP_SET_MASK1_INVALID;
  }
  if(pp_out_mask2->enable) {
    if(pp_out_mask2->width & mask)
      ret = (i32) PP_SET_MASK2_INVALID;
    if(pp_out_mask2->origin_x & mask)
      ret = (i32) PP_SET_MASK2_INVALID;
    if(pp_out_mask2->height & mask)
      ret = (i32) PP_SET_MASK2_INVALID;
    if(pp_out_mask2->origin_y & mask)
      ret = (i32) PP_SET_MASK2_INVALID;
  }

  /* Frame buffer mustn't be specified */
  if(pp_out_frm_buffer->enable) {
    ret = (i32) PP_SET_FRAMEBUFFER_INVALID;
  }

  return ret;

}

/*------------------------------------------------------------------------------
    Function name   : PPCheckConfig
    Description     :
    Return type     : i32
    Argument        : PPConfig * pp_cfg
    Argument        : u32 pipeline
    Argument        : u32 dec_type
------------------------------------------------------------------------------*/
i32 PPCheckConfig(PPContainer * pp_c, PPConfig * pp_cfg,
                  u32 dec_linked, u32 dec_type) {
  PPOutImage *pp_out_img;
  PPInImage *pp_in_img;
  PPInCropping *pp_in_crop;
  PPOutRgb *pp_out_rgb;
  PPOutFrameBuffer *pp_out_frm_buffer;
  PPInRotation *pp_in_rotation;
  PPOutDeinterlace *pp_out_deint;

  PPOutMask1 *pp_out_mask1;
  PPOutMask2 *pp_out_mask2;

  const u32 address_mask = (PP_X170_DATA_BUS_WIDTH - 1);

  ASSERT(pp_cfg != NULL);

  pp_out_img = &pp_cfg->pp_out_img;
  pp_in_img = &pp_cfg->pp_in_img;
  pp_in_crop = &pp_cfg->pp_in_crop;
  pp_out_rgb = &pp_cfg->pp_out_rgb;
  pp_out_frm_buffer = &pp_cfg->pp_out_frm_buffer;
  pp_in_rotation = &pp_cfg->pp_in_rotation;
  pp_out_deint = &pp_cfg->pp_out_deinterlace;

  pp_out_mask1 = &pp_cfg->pp_out_mask1;
  pp_out_mask2 = &pp_cfg->pp_out_mask2;

  /* PPInImage check */

  if(!PPIsInPixFmtOk(pp_in_img->pix_format, pp_c)) {
    return (i32) PP_SET_IN_FORMAT_INVALID;
  }

  if(!dec_linked) {
    if(pp_in_img->pic_struct != PP_PIC_BOT_FIELD &&
        pp_in_img->pic_struct != PP_PIC_BOT_FIELD_FRAME) {
      if((pp_in_img->buffer_bus_addr == 0) ||
          (pp_in_img->buffer_bus_addr & address_mask)) {
        return (i32) PP_SET_IN_ADDRESS_INVALID;
      }

      if(pp_in_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) {
        if(pp_in_img->buffer_cb_bus_addr == 0 ||
            (pp_in_img->buffer_cb_bus_addr & address_mask))
          return (i32) PP_SET_IN_ADDRESS_INVALID;
      }

      if(pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_PLANAR) {
        if(pp_in_img->buffer_cr_bus_addr == 0 ||
            (pp_in_img->buffer_cr_bus_addr & address_mask))
          return (i32) PP_SET_IN_ADDRESS_INVALID;
      }
    }
    if(pp_in_img->pic_struct != PP_PIC_FRAME_OR_TOP_FIELD &&
        pp_in_img->pic_struct != PP_PIC_TOP_FIELD_FRAME) {
      if((pp_in_img->buffer_bus_addr_bot == 0) ||
          (pp_in_img->buffer_bus_addr_bot & address_mask)) {
        return (i32) PP_SET_IN_ADDRESS_INVALID;
      }

      if(pp_in_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) {
        if(pp_in_img->buffer_bus_addr_ch_bot == 0 ||
            (pp_in_img->buffer_bus_addr_ch_bot & address_mask))
          return (i32) PP_SET_IN_ADDRESS_INVALID;
      }
    }
  }

  if(pp_c->hw_id == 0x8170U) {
    if((pp_in_img->width < PP_IN_MIN_WIDTH(dec_linked)) ||
        (pp_in_img->height < PP_IN_MIN_HEIGHT(dec_linked)) ||
        (pp_in_img->width > PP_IN_MAX_WIDTH(dec_linked)) ||
        (pp_in_img->height > PP_IN_MAX_HEIGHT(dec_linked)) ||
        (pp_in_img->width & PP_IN_DIVISIBILITY(dec_linked)) ||
        (pp_in_img->height & PP_IN_DIVISIBILITY(dec_linked))) {
      return (i32) PP_SET_IN_SIZE_INVALID;
    }
  } else {
    /* Max size for pipelined pic, 511MB for JPEG etc, 1024MB for WEBP */
    u32 max_mbs =
      ((dec_type == PP_PIPELINED_DEC_TYPE_WEBP) ||
       ((dec_type == PP_PIPELINED_DEC_TYPE_JPEG) && pp_c->jpeg16k_support)) ?
      1024U : 511U;
    if((pp_in_img->width < PP_IN_MIN_WIDTH(dec_linked)) ||
        (pp_in_img->height < PP_IN_MIN_HEIGHT(dec_linked)) ||
        (pp_in_img->width > PP_IN_MAX_WIDTH_EXT(dec_linked, max_mbs)) ||
        (pp_in_img->height > PP_IN_MAX_HEIGHT_EXT(dec_linked, max_mbs)) ||
        (pp_in_img->width & PP_IN_DIVISIBILITY(dec_linked)) ||
        (pp_in_img->height & PP_IN_DIVISIBILITY(dec_linked))) {
      return (i32) PP_SET_IN_SIZE_INVALID;
    }
  }

  if(pp_in_img->pic_struct > PP_PIC_BOT_FIELD_FRAME) {
    return (i32) PP_SET_IN_STRUCT_INVALID;
  } else if(pp_in_img->pic_struct != PP_PIC_FRAME_OR_TOP_FIELD &&
            pp_in_img->pix_format != PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR &&
            pp_in_img->pix_format != PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED &&
            pp_in_img->pix_format != PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED &&
            pp_in_img->pix_format != PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED &&
            pp_in_img->pix_format != PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED) {
    return (i32) PP_SET_IN_STRUCT_INVALID;
  }

  /* cropping check */
  if(pp_in_crop->enable != 0) {
    if((pp_in_crop->width < PP_IN_MIN_WIDTH(dec_linked)) ||
        (pp_in_crop->height < PP_IN_MIN_HEIGHT(dec_linked)) ||
        (pp_in_crop->width > pp_in_img->width) ||
        (pp_in_crop->origin_x > pp_in_img->width) ||
        (pp_in_crop->height > pp_in_img->height) ||
        (pp_in_crop->origin_y > pp_in_img->height) ||
        (pp_in_crop->width & 0x07) ||
        (pp_in_crop->height & 0x07) ||
        (pp_in_crop->origin_x & 0x0F) || (pp_in_crop->origin_y & 0x0F)) {
      return (i32) PP_SET_CROP_INVALID;
    }
#if 0
    /* when deinterlacing the cropped size has to be 16 multiple */
    if(pp_cfg->pp_out_deinterlace.enable &&
        ((pp_in_crop->width & 0x0F) || (pp_in_crop->height & 0x0F))) {
      return (i32) PP_SET_CROP_INVALID;
    }
#endif
  }
  /* check rotation */
  switch (pp_in_rotation->rotation) {
  case PP_ROTATION_NONE:
  case PP_ROTATION_RIGHT_90:
  case PP_ROTATION_LEFT_90:
  case PP_ROTATION_HOR_FLIP:
  case PP_ROTATION_VER_FLIP:
  case PP_ROTATION_180:
    break;
  default:
    return (i32) PP_SET_ROTATION_INVALID;
  }

  /* jpeg dec linked, rotation not supported in 440, 422, 411 and 444 */
  if(dec_linked != 0 && pp_in_rotation->rotation != PP_ROTATION_NONE &&
      (pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_4_0 ||
       pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_2_2_SEMIPLANAR ||
       pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_1_1_SEMIPLANAR ||
       pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_4_4_SEMIPLANAR)) {
    return (i32) PP_SET_ROTATION_INVALID;
  }

  /* rotation not supported in jpeg 400 but supported in h264 */
  if(dec_linked != 0 && dec_type == PP_PIPELINED_DEC_TYPE_JPEG &&
      pp_in_img->pix_format == PP_PIX_FMT_YCBCR_4_0_0 &&
      pp_in_rotation->rotation != PP_ROTATION_NONE) {
    return (i32) PP_SET_ROTATION_INVALID;
  }

  if(pp_in_img->video_range > 1) {
    return (i32) PP_SET_IN_FORMAT_INVALID;
  }

  /* PPOutImage check */

  if(!PPIsOutPixFmtOk(pp_out_img->pix_format, pp_c)) {
    return (i32) PP_SET_OUT_FORMAT_INVALID;
  }

  if(pp_out_img->buffer_bus_addr == 0 || pp_out_img->buffer_bus_addr & address_mask) {
    return (i32) PP_SET_OUT_ADDRESS_INVALID;
  }

  if(pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR) {
    if(pp_out_img->buffer_chroma_bus_addr == 0 ||
        (pp_out_img->buffer_chroma_bus_addr & address_mask))
      return (i32) PP_SET_OUT_ADDRESS_INVALID;
  }

  if(pp_out_img->width < PP_OUT_MIN_WIDTH ||
      pp_out_img->height < PP_OUT_MIN_HEIGHT ||
      pp_out_img->width > pp_c->max_out_width ||
      pp_out_img->height > pp_c->max_out_height) {
    return (i32) PP_SET_OUT_SIZE_INVALID;
  }

  /* scale check */
  {
    u32 w, h, multires = 0;

    w = pp_in_crop->enable ? pp_in_crop->width : pp_in_img->width;
    h = pp_in_crop->enable ? pp_in_crop->height : pp_in_img->height;

    if(dec_type == PP_PIPELINED_DEC_TYPE_VC1)
      multires = pp_in_img->vc1_multi_res_enable ? 1 : 0;

    /* swap width and height if input is rotated first */
    if(pp_in_rotation->rotation == PP_ROTATION_LEFT_90 ||
        pp_in_rotation->rotation == PP_ROTATION_RIGHT_90) {
      u32 tmp = w;

      w = h;
      h = tmp;
    }

    if(!pp_c->scaling_ena) {
      if((w != pp_out_img->width) || (h != pp_out_img->height))
        return (i32) PP_SET_SCALING_UNSUPPORTED;
    }

    if((pp_out_img->width > w) &&
        (pp_out_img->width > PP_OUT_MAX_WIDTH_UPSCALED(w, multires))) {
      return (i32) PP_SET_OUT_SIZE_INVALID;
    }

    if(multires && pp_out_img->width != w)
      return (i32) PP_SET_OUT_SIZE_INVALID;

    if((pp_out_img->height > h) &&
        (pp_out_img->height > PP_OUT_MAX_HEIGHT_UPSCALED(h, multires))) {
      return (i32) PP_SET_OUT_SIZE_INVALID;
    }

    /* Enforce downscaling factor limitation */
    if( h > pp_out_img->height ||
        w > pp_out_img->width ) {
      u32 tmp;

      tmp = pp_out_img->height * PP_OUT_MIN_DOWNSCALING_FACTOR;
      if(multires)    tmp >>= 1;
      if( h > tmp ) {
        return (i32) PP_SET_OUT_SIZE_INVALID;
      }

      tmp = pp_out_img->width * PP_OUT_MIN_DOWNSCALING_FACTOR;
      if(multires)    tmp >>= 1;
      if( w > tmp ) {
        return (i32) PP_SET_OUT_SIZE_INVALID;
      }
    }

    if(multires && pp_out_img->height != h)
      return (i32) PP_SET_OUT_SIZE_INVALID;

    if(((pp_out_img->width > w) && (pp_out_img->height < h)) ||
        ((pp_out_img->width < w) && (pp_out_img->height > h))) {
      return (i32) PP_SET_OUT_SIZE_INVALID;
    }
  }

  /* PPOutFrameBuffer */
  if(pp_out_frm_buffer->enable) {
    if((pp_out_frm_buffer->frame_buffer_width > PP_MAX_FRM_BUFF_WIDTH) ||
        (pp_out_frm_buffer->write_origin_x >=
         (i32) pp_out_frm_buffer->frame_buffer_width) ||
        (pp_out_frm_buffer->write_origin_y >=
         (i32) pp_out_frm_buffer->frame_buffer_height) ||
        (pp_out_frm_buffer->write_origin_x + (i32) pp_out_img->width <= 0) ||
        (pp_out_frm_buffer->write_origin_y + (i32) pp_out_img->height <= 0)) {
      return (i32) PP_SET_FRAMEBUFFER_INVALID;
    }
    /* Divisibility */
    if((pp_out_frm_buffer->write_origin_y & 1) &&
        (pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK)) {
      return (i32) PP_SET_FRAMEBUFFER_INVALID;
    }

    if((pp_out_frm_buffer->frame_buffer_height & 1) &&
        (pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK)) {
      return (i32) PP_SET_FRAMEBUFFER_INVALID;
    }
  }

  /* 4x4 tiled output */
  if( pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_2_TILED_4X4 ||
      pp_out_img->pix_format == PP_PIX_FMT_YCRYCB_4_2_2_TILED_4X4 ||
      pp_out_img->pix_format == PP_PIX_FMT_CBYCRY_4_2_2_TILED_4X4 ||
      pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4 ) {
    i32 ret = PPCheckTiledOutput( pp_cfg );
    if(ret != (i32) PP_OK)
      return ret;
  }

  /* PPOutRgb */

  if((pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB_MASK)) {
    /* Check support in HW */
    if(!pp_c->dither_ena && pp_out_rgb->dithering_enable)
      return (i32) PP_SET_DITHERING_UNSUPPORTED;

    if((pp_out_rgb->rgb_transform != PP_YCBCR2RGB_TRANSFORM_CUSTOM) &&
        (pp_out_rgb->rgb_transform != PP_YCBCR2RGB_TRANSFORM_BT_601) &&
        (pp_out_rgb->rgb_transform != PP_YCBCR2RGB_TRANSFORM_BT_709)) {
      return (i32) PP_SET_VIDEO_ADJUST_INVALID;
    }

    if(pp_out_rgb->brightness < -128 || pp_out_rgb->brightness > 127) {
      return (i32) PP_SET_VIDEO_ADJUST_INVALID;
    }

    if(pp_out_rgb->saturation < -64 || pp_out_rgb->saturation > 128) {
      return (i32) PP_SET_VIDEO_ADJUST_INVALID;
    }

    if(pp_out_rgb->contrast < -64 || pp_out_rgb->contrast > 64) {
      return (i32) PP_SET_VIDEO_ADJUST_INVALID;
    }

    if((pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB32_MASK)) {
      if(pp_out_rgb->alpha > 255) {
        return (i32) PP_SET_VIDEO_ADJUST_INVALID;
      }
    } else /* 16 bits RGB */ if(pp_out_rgb->transparency > 1) {
      return (i32) PP_SET_VIDEO_ADJUST_INVALID;
    }

    if(pp_out_img->pix_format == PP_PIX_FMT_RGB32_CUSTOM) {
      PPRgbBitmask *rgbbm = &pp_out_rgb->rgb_bitmask;

      if((rgbbm->mask_r & rgbbm->mask_g & rgbbm->mask_b & rgbbm->
          mask_alpha) != 0) {
        return (i32) PP_SET_RGB_BITMASK_INVALID;
      }
    } else if(pp_out_img->pix_format == PP_PIX_FMT_RGB16_CUSTOM) {
      PPRgbBitmask *rgbbm = &pp_out_rgb->rgb_bitmask;

      if((rgbbm->mask_r & rgbbm->mask_g & rgbbm->mask_b & rgbbm->
          mask_alpha) != 0 ||
          (rgbbm->mask_r | rgbbm->mask_g | rgbbm->mask_b | rgbbm->
           mask_alpha) >= (1 << 16)) {
        return (i32) PP_SET_RGB_BITMASK_INVALID;
      }
    }
    if((pp_out_img->pix_format == PP_PIX_FMT_RGB16_CUSTOM) ||
        (pp_out_img->pix_format == PP_PIX_FMT_RGB32_CUSTOM)) {

      PPRgbBitmask *rgbbm = &pp_out_rgb->rgb_bitmask;

      if(PPCheckOverlapping(rgbbm->mask_r,
                            rgbbm->mask_g, rgbbm->mask_b, rgbbm->mask_alpha))
        return (i32) PP_SET_RGB_BITMASK_INVALID;

      if(PPContinuousCheck(rgbbm->mask_r) ||
          PPContinuousCheck(rgbbm->mask_g) ||
          PPContinuousCheck(rgbbm->mask_b) ||
          PPContinuousCheck(rgbbm->mask_alpha))
        return (i32) PP_SET_RGB_BITMASK_INVALID;

    }

  }

  if(pp_out_mask1->enable && pp_out_mask1->alpha_blend_ena) {
    if(pp_out_mask1->blend_component_base & address_mask ||
        pp_out_mask1->blend_component_base == 0)
      return (i32) PP_SET_MASK1_INVALID;
  }

  if(pp_out_mask2->enable && pp_out_mask2->alpha_blend_ena) {
    if(pp_out_mask2->blend_component_base & address_mask ||
        pp_out_mask2->blend_component_base == 0)
      return (i32) PP_SET_MASK2_INVALID;

  }

  {
    i32 ret = PPCheckAllWidthParams(pp_cfg, pp_c->blend_ena,
                                    pp_c->pix_acc_support,
                                    pp_c->blend_crop_support);

    if(ret != (i32) PP_OK)
      return ret;
  }
  {
    i32 ret = PPCheckAllHeightParams(pp_cfg, pp_c->pix_acc_support);

    if(ret != (i32) PP_OK)
      return ret;
  }

  /* deinterlacing only for semiplanar & planar 4:2:0 */
  if(pp_out_deint->enable) {
    if(!pp_c->deint_ena)
      return (i32) PP_SET_DEINTERLACING_UNSUPPORTED;

    if(pp_in_img->pix_format != PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR &&
        pp_in_img->pix_format != PP_PIX_FMT_YCBCR_4_2_0_PLANAR &&
        pp_in_img->pix_format != PP_PIX_FMT_YCBCR_4_0_0) {
      return (i32) PP_SET_DEINTERLACE_INVALID;
    }
  }

  if(pp_in_img->vc1_range_red_frm &&
      (pp_in_img->vc1_range_map_yenable || pp_in_img->vc1_range_map_cenable))
    return (i32) PP_SET_IN_RANGE_MAP_INVALID;
  else if(pp_in_img->vc1_range_map_ycoeff > 7 || pp_in_img->vc1_range_map_ccoeff > 7)
    return (i32) PP_SET_IN_RANGE_MAP_INVALID;

  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : PPRun
    Description     :
    Return type     : pp result
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
PPResult PPRun(PPContainer *pp_c) {
  u32 b_pp_hw_reserved = 0;

  PPSetStatus(pp_c, PP_STATUS_RUNNING);

  PPDEBUG_PRINT(("pp status 2%x\n", PPGetStatus(pp_c)));

  if(pp_c->pipeline) {
    /* decoder has reserved the PP hardware for pipelined operation */
    ASSERT(pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_NONE);
    /* Disable rotation for pipeline mode */
    pp_c->pp_cfg.pp_in_rotation.rotation = PP_ROTATION_NONE;
    SetPpRegister(pp_c->pp_regs, HWIF_ROTATION_MODE, 0);

    if(pp_c->dec_type == PP_PIPELINED_DEC_TYPE_H264 ||
        pp_c->dec_type == PP_PIPELINED_DEC_TYPE_VP8) {
      /* h264/vp8 decoder use DLWReserveHwPipe, which reserves both DEC
       * and PP hardware for pipeline operation.
       * Decoder also takes care of releasing PP hardware.
       */
      b_pp_hw_reserved = 1;
    }
  }

  if(!b_pp_hw_reserved) {
    if(DWLReserveHw(pp_c->dwl, &pp_c->core_id) != DWL_OK) {
      return PP_BUSY;
    }
  } else {
    pp_c->core_id = 0; /* we just assume single Core for PP */
  }

  PPFlushRegs(pp_c);

  if(!pp_c->pipeline) {
    /* turn ASIC ON by setting high the enable bit */
    SetPpRegister(pp_c->pp_regs, HWIF_PP_E, 1);
    DWLEnableHw(pp_c->dwl, pp_c->core_id, PP_X170_REG_START, pp_c->pp_regs[0]);
  } else {
    /* decoder turns PP ON in pipeline mode (leave enable bit low) */
    SetPpRegister(pp_c->pp_regs, HWIF_PP_E, 0);
    DWLEnableHw(pp_c->dwl, pp_c->core_id, PP_X170_REG_START, pp_c->pp_regs[0]);
  }

  return PP_OK;
}

/*------------------------------------------------------------------------------
    Function name   : PPSetFrmBufferWriting
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPSetFrmBufferWriting(PPContainer * pp_c) {
  PPOutImage *pp_out_img;
  PPOutFrameBuffer *pp_out_frm_buffer;
  u32 *pp_regs;

  ASSERT(pp_c != NULL);

  pp_out_img = &pp_c->pp_cfg.pp_out_img;
  pp_out_frm_buffer = &pp_c->pp_cfg.pp_out_frm_buffer;

  pp_regs = pp_c->pp_regs;

  if(pp_out_frm_buffer->enable) {

    i32 up, down, right, left, scanline;

    up = pp_out_frm_buffer->write_origin_y;
    left = pp_out_frm_buffer->write_origin_x;
    down =
      ((i32) pp_out_frm_buffer->frame_buffer_height - up) -
      (i32) pp_out_img->height;
    right =
      ((i32) pp_out_frm_buffer->frame_buffer_width - left) -
      (i32) pp_out_img->width;

    scanline = (i32) pp_out_frm_buffer->frame_buffer_width;

    if(left < 0) {
      SetPpRegister(pp_regs, HWIF_LEFT_CROSS, (u32) (-left));
      SetPpRegister(pp_regs, HWIF_LEFT_CROSS_EXT, (u32) (-left) >> 11);
      SetPpRegister(pp_regs, HWIF_LEFT_CROSS_E, 1);
    } else {
      SetPpRegister(pp_regs, HWIF_LEFT_CROSS_E, 0);
    }
    if(right < 0) {
      SetPpRegister(pp_regs, HWIF_RIGHT_CROSS, (u32) (-right));
      SetPpRegister(pp_regs, HWIF_RIGHT_CROSS_EXT, (u32) (-right) >> 11);
      SetPpRegister(pp_regs, HWIF_RIGHT_CROSS_E, 1);
    } else {
      SetPpRegister(pp_regs, HWIF_RIGHT_CROSS_E, 0);
    }

    if(up < 0) {
      SetPpRegister(pp_regs, HWIF_UP_CROSS, (u32) (-up));
      SetPpRegister(pp_regs, HWIF_UP_CROSS_EXT, (u32) (-up) >> 11);
      SetPpRegister(pp_regs, HWIF_UP_CROSS_E, 1);
    } else {
      SetPpRegister(pp_regs, HWIF_UP_CROSS_E, 0);
    }

    if(down < 0) {
      SetPpRegister(pp_regs, HWIF_DOWN_CROSS, (u32) (-down));
      SetPpRegister(pp_regs, HWIF_DOWN_CROSS_EXT, (u32) (-down) >> 11);
      SetPpRegister(pp_regs, HWIF_DOWN_CROSS_E, 1);
    } else {
      SetPpRegister(pp_regs, HWIF_DOWN_CROSS_E, 0);
    }

    SetPpRegister(pp_regs, HWIF_DISPLAY_WIDTH,
                  pp_out_frm_buffer->frame_buffer_width);

    if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB_MASK) {
      pp_c->frm_buffer_luma_or_rgb_offset =
        (scanline * up + left) * ((i32) pp_c->rgb_depth / 8);
    } else if(pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED ||
              pp_out_img->pix_format == PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED ||
              pp_out_img->pix_format == PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED ||
              pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED)

    {
      pp_c->frm_buffer_luma_or_rgb_offset = (scanline * up + left) * 2;
    } else { /* PP_PIX_FMT_YCBCR_4_2_0_CH_INTERLEAVED */
      pp_c->frm_buffer_luma_or_rgb_offset = (scanline * up + left);

      pp_c->frm_buffer_chroma_offset = (scanline * up) / 2 + left;
    }

  } else {
    SetPpRegister(pp_regs, HWIF_DOWN_CROSS_E, 0);
    SetPpRegister(pp_regs, HWIF_LEFT_CROSS_E, 0);
    SetPpRegister(pp_regs, HWIF_RIGHT_CROSS_E, 0);
    SetPpRegister(pp_regs, HWIF_UP_CROSS_E, 0);

    SetPpRegister(pp_regs, HWIF_DISPLAY_WIDTH, pp_out_img->width);

    pp_c->frm_buffer_luma_or_rgb_offset = 0;
    pp_c->frm_buffer_chroma_offset = 0;
  }
}

/*------------------------------------------------------------------------------
    Function name   : PPSetRgbBitmaskCustom
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
    Argument        : u32 rgb16
------------------------------------------------------------------------------*/
void PPSetRgbBitmaskCustom(PPContainer * pp_c, u32 rgb16) {
  u32 *pp_regs;
  u32 mask, pad, alpha;
  PPRgbBitmask *rgb_mask;

  ASSERT(pp_c != NULL);
  rgb_mask = &pp_c->pp_cfg.pp_out_rgb.rgb_bitmask;
  pp_regs = pp_c->pp_regs;

  alpha = rgb_mask->mask_alpha;

  if(rgb16) {
    alpha |= alpha << 16;
  }

  /* setup R */
  mask = rgb_mask->mask_r;

  if(rgb16) {
    mask |= mask << 16; /* duplicate mask for 16 bits RGB */
  }

  pad = PPFindFirstNonZeroBit(mask);
  SetPpRegister(pp_regs, HWIF_RGB_R_PADD, pad);
  SetPpRegister(pp_regs, HWIF_R_MASK, mask | alpha);

  /* setup G */
  mask = rgb_mask->mask_g;

  if(rgb16) {
    mask |= mask << 16; /* duplicate mask for 16 bits RGB */
  }

  pad = PPFindFirstNonZeroBit(mask);
  SetPpRegister(pp_regs, HWIF_RGB_G_PADD, pad);
  SetPpRegister(pp_regs, HWIF_G_MASK, mask | alpha);

  /* setup B */
  mask = rgb_mask->mask_b;

  if(rgb16) {
    mask |= mask << 16; /* duplicate mask for 16 bits RGB */
  }

  pad = PPFindFirstNonZeroBit(mask);
  SetPpRegister(pp_regs, HWIF_RGB_B_PADD, pad);
  SetPpRegister(pp_regs, HWIF_B_MASK, mask | alpha);
}

/*------------------------------------------------------------------------------
    Function name   : PPSetRgbBitmask
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPSetRgbBitmask(PPContainer * pp_c) {
  PPOutImage *pp_out_img;
  u32 *pp_regs;

  ASSERT(pp_c != NULL);

  pp_out_img = &pp_c->pp_cfg.pp_out_img;
  pp_regs = pp_c->pp_regs;

  switch (pp_out_img->pix_format) {
  case PP_PIX_FMT_BGR32:
    SetPpRegister(pp_regs, HWIF_B_MASK,
                  0x00FF0000 | (pp_c->pp_cfg.pp_out_rgb.alpha << 24));
    SetPpRegister(pp_regs, HWIF_G_MASK,
                  0x0000FF00 | (pp_c->pp_cfg.pp_out_rgb.alpha << 24));
    SetPpRegister(pp_regs, HWIF_R_MASK,
                  0x000000FF | (pp_c->pp_cfg.pp_out_rgb.alpha << 24));
    SetPpRegister(pp_regs, HWIF_RGB_B_PADD, 8);
    SetPpRegister(pp_regs, HWIF_RGB_G_PADD, 16);
    SetPpRegister(pp_regs, HWIF_RGB_R_PADD, 24);
    break;
  case PP_PIX_FMT_RGB32:
    SetPpRegister(pp_regs, HWIF_R_MASK,
                  0x00FF0000 | (pp_c->pp_cfg.pp_out_rgb.alpha << 24));
    SetPpRegister(pp_regs, HWIF_G_MASK,
                  0x0000FF00 | (pp_c->pp_cfg.pp_out_rgb.alpha << 24));
    SetPpRegister(pp_regs, HWIF_B_MASK,
                  0x000000FF | (pp_c->pp_cfg.pp_out_rgb.alpha << 24));
    SetPpRegister(pp_regs, HWIF_RGB_R_PADD, 8);
    SetPpRegister(pp_regs, HWIF_RGB_G_PADD, 16);
    SetPpRegister(pp_regs, HWIF_RGB_B_PADD, 24);
    break;

  case PP_PIX_FMT_RGB16_5_5_5: {
    u32 mask;

    mask = 0x7C00 | (pp_c->pp_cfg.pp_out_rgb.transparency << 15);
    SetPpRegister(pp_regs, HWIF_R_MASK, mask | (mask << 16));
    mask = 0x03E0 | (pp_c->pp_cfg.pp_out_rgb.transparency << 15);
    SetPpRegister(pp_regs, HWIF_G_MASK, mask | (mask << 16));
    mask = 0x001F | (pp_c->pp_cfg.pp_out_rgb.transparency << 15);
    SetPpRegister(pp_regs, HWIF_B_MASK, mask | (mask << 16));
    SetPpRegister(pp_regs, HWIF_RGB_R_PADD, 1);
    SetPpRegister(pp_regs, HWIF_RGB_G_PADD, 6);
    SetPpRegister(pp_regs, HWIF_RGB_B_PADD, 11);

  }
  break;
  case PP_PIX_FMT_BGR16_5_5_5: {
    u32 mask;

    mask = 0x7C00 | (pp_c->pp_cfg.pp_out_rgb.transparency << 15);
    SetPpRegister(pp_regs, HWIF_B_MASK, mask | (mask << 16));
    mask = 0x03E0 | (pp_c->pp_cfg.pp_out_rgb.transparency << 15);
    SetPpRegister(pp_regs, HWIF_G_MASK, mask | (mask << 16));
    mask = 0x001F | (pp_c->pp_cfg.pp_out_rgb.transparency << 15);
    SetPpRegister(pp_regs, HWIF_R_MASK, mask | (mask << 16));
    SetPpRegister(pp_regs, HWIF_RGB_B_PADD, 1);
    SetPpRegister(pp_regs, HWIF_RGB_G_PADD, 6);
    SetPpRegister(pp_regs, HWIF_RGB_R_PADD, 11);

  }

  break;

  case PP_PIX_FMT_RGB16_5_6_5:
    SetPpRegister(pp_regs, HWIF_R_MASK, 0xF800F800);
    SetPpRegister(pp_regs, HWIF_G_MASK, 0x07E007E0);
    SetPpRegister(pp_regs, HWIF_B_MASK, 0x001F001F);
    SetPpRegister(pp_regs, HWIF_RGB_R_PADD, 0);
    SetPpRegister(pp_regs, HWIF_RGB_G_PADD, 5);
    SetPpRegister(pp_regs, HWIF_RGB_B_PADD, 11);
    break;

  case PP_PIX_FMT_BGR16_5_6_5:
    SetPpRegister(pp_regs, HWIF_B_MASK, 0xF800F800);
    SetPpRegister(pp_regs, HWIF_G_MASK, 0x07E007E0);
    SetPpRegister(pp_regs, HWIF_R_MASK, 0x001F001F);
    SetPpRegister(pp_regs, HWIF_RGB_B_PADD, 0);
    SetPpRegister(pp_regs, HWIF_RGB_G_PADD, 5);
    SetPpRegister(pp_regs, HWIF_RGB_R_PADD, 11);
    break;

  case PP_PIX_FMT_RGB16_CUSTOM:
    PPSetRgbBitmaskCustom(pp_c, 1);
    break;

  case PP_PIX_FMT_RGB32_CUSTOM:
    PPSetRgbBitmaskCustom(pp_c, 0);
    break;
  default:
    ASSERT(0);  /* should never get here */
    break;
  }
}

/*------------------------------------------------------------------------------
    Function name   : PPSetRgbTransformCoeffs
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
------------------------------------------------------------------------------*/
void PPSetRgbTransformCoeffs(PPContainer * pp_c) {
  PPOutImage *pp_out_img;
  PPInImage *pp_in_img;
  PPOutRgb *pp_out_rgb;
  u32 *pp_regs;

  ASSERT(pp_c != NULL);

  pp_out_img = &pp_c->pp_cfg.pp_out_img;
  pp_in_img = &pp_c->pp_cfg.pp_in_img;
  pp_out_rgb = &pp_c->pp_cfg.pp_out_rgb;

  pp_regs = pp_c->pp_regs;

  if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB_MASK) {
    i32 satur = 0, tmp;
    PPRgbTransform *rgb_t = &pp_out_rgb->rgb_transform_coeffs;

    if(pp_c->rgb_depth == 32)
      SetPpRegister(pp_regs, HWIF_RGB_PIX_IN32, 0);
    else
      SetPpRegister(pp_regs, HWIF_RGB_PIX_IN32, 1);

    /*  Contrast */
    if(pp_out_rgb->contrast != 0) {
      i32 thr1y, thr2y, off1, off2, thr1, thr2, a1, a2;

      if(pp_in_img->video_range == 0) {
        i32 tmp1, tmp2;

        /* Contrast */
        thr1 = (219 * (pp_out_rgb->contrast + 128)) / 512;
        thr1y = (219 - 2 * thr1) / 2;
        thr2 = 219 - thr1;
        thr2y = 219 - thr1y;

        tmp1 = (thr1y * 256) / thr1;
        tmp2 = ((thr2y - thr1y) * 256) / (thr2 - thr1);
        off1 = ((thr1y - ((tmp2 * thr1) / 256)) * (i32) rgb_t->a) / 256;
        off2 = ((thr2y - ((tmp1 * thr2) / 256)) * (i32) rgb_t->a) / 256;

        tmp1 = (64 * (pp_out_rgb->contrast + 128)) / 128;
        tmp2 = 256 * (128 - tmp1);
        a1 = (tmp2 + off2) / thr1;
        a2 = a1 + (256 * (off2 - 1)) / (thr2 - thr1);
      } else {
        /* Contrast */
        thr1 = (64 * (pp_out_rgb->contrast + 128)) / 128;
        thr1y = 128 - thr1;
        thr2 = 256 - thr1;
        thr2y = 256 - thr1y;
        a1 = (thr1y * 256) / thr1;
        a2 = ((thr2y - thr1y) * 256) / (thr2 - thr1);
        off1 = thr1y - (a2 * thr1) / 256;
        off2 = thr2y - (a1 * thr2) / 256;
      }

      if(a1 > 1023)
        a1 = 1023;
      else if(a1 < 0)
        a1 = 0;

      if(a2 > 1023)
        a2 = 1023;
      else if(a2 < 0)
        a2 = 0;

      if(thr1 > 255)
        thr1 = 255;
      else if(thr1 < 0)
        thr1 = 0;

      if(thr2 > 255)
        thr2 = 255;
      else if(thr2 < 0)
        thr2 = 0;

      if(off1 > 511)
        off1 = 511;
      else if(off1 < -512)
        off1 = -512;

      if(off2 > 511)
        off2 = 511;
      else if(off2 < -512)
        off2 = -512;

      SetPpRegister(pp_regs, HWIF_CONTRAST_THR1, (u32) thr1);
      SetPpRegister(pp_regs, HWIF_CONTRAST_THR2, (u32) thr2);

      SetPpRegister(pp_regs, HWIF_CONTRAST_OFF1, off1);
      SetPpRegister(pp_regs, HWIF_CONTRAST_OFF2, off2);

      SetPpRegister(pp_regs, HWIF_COLOR_COEFFA1, (u32) a1);
      SetPpRegister(pp_regs, HWIF_COLOR_COEFFA2, (u32) a2);

    } else {
      SetPpRegister(pp_regs, HWIF_CONTRAST_THR1, 55);
      SetPpRegister(pp_regs, HWIF_CONTRAST_THR2, 165);

      SetPpRegister(pp_regs, HWIF_CONTRAST_OFF1, 0);
      SetPpRegister(pp_regs, HWIF_CONTRAST_OFF2, 0);

      tmp = rgb_t->a;

      if(tmp > 1023)
        tmp = 1023;
      else if(tmp < 0)
        tmp = 0;

      SetPpRegister(pp_regs, HWIF_COLOR_COEFFA1, tmp);
      SetPpRegister(pp_regs, HWIF_COLOR_COEFFA2, tmp);
    }

    /*  brightness */
    SetPpRegister(pp_regs, HWIF_COLOR_COEFFF, pp_out_rgb->brightness);

    /* saturation */
    satur = 64 + pp_out_rgb->saturation;

    tmp = (satur * (i32) rgb_t->b) / 64;
    if(tmp > 1023)
      tmp = 1023;
    else if(tmp < 0)
      tmp = 0;
    SetPpRegister(pp_regs, HWIF_COLOR_COEFFB, (u32) tmp);

    tmp = (satur * (i32) rgb_t->c) / 64;
    if(tmp > 1023)
      tmp = 1023;
    else if(tmp < 0)
      tmp = 0;
    SetPpRegister(pp_regs, HWIF_COLOR_COEFFC, (u32) tmp);

    tmp = (satur * (i32) rgb_t->d) / 64;
    if(tmp > 1023)
      tmp = 1023;
    else if(tmp < 0)
      tmp = 0;
    SetPpRegister(pp_regs, HWIF_COLOR_COEFFD, (u32) tmp);

    tmp = (satur * (i32) rgb_t->e) / 64;
    if(tmp > 1023)
      tmp = 1023;
    else if(tmp < 0)
      tmp = 0;

    SetPpRegister(pp_regs, HWIF_COLOR_COEFFE, (u32) tmp);
  }
}

/*------------------------------------------------------------------------------
    Function name   : PPFindFirstNonZeroBit
    Description     :
    Return type     : u32
    Argument        : u32 mask
------------------------------------------------------------------------------*/
u32 PPFindFirstNonZeroBit(u32 mask) {
  u32 offset = 0;

  while(!(mask & 0x80000000) && (offset < 32)) {
    mask <<= 1;
    offset++;
  }

  return offset & 0x1F;
}

/*------------------------------------------------------------------------------
    Function name   : PPIsInPixFmtOk
    Description     :
    Return type     : u32
    Argument        : u32 pix_fmt
    Argument        : const PPContainer * pp_c
------------------------------------------------------------------------------*/
u32 PPIsInPixFmtOk(u32 pix_fmt, const PPContainer * pp_c) {
  u32 ret = 1;
  const i32 dec_linked = pp_c->dec_inst == NULL ? 0 : 1;

  switch (pix_fmt) {
  case PP_PIX_FMT_YCBCR_4_2_0_TILED:
    if(pp_c->dec_type == PP_PIPELINED_DEC_TYPE_JPEG)
      ret = 0;
    break;
  case PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR:
    break;
  case PP_PIX_FMT_YCBCR_4_2_0_PLANAR:
  case PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED:
    /* these are not supported in pipeline */
    if(dec_linked != 0)
      ret = 0;
    break;
  case PP_PIX_FMT_YCBCR_4_0_0:
    /* this supported just in H264 and JPEG pipeline mode */
    if(dec_linked == 0 ||
        (pp_c->dec_type != PP_PIPELINED_DEC_TYPE_JPEG &&
         pp_c->dec_type != PP_PIPELINED_DEC_TYPE_H264))
      ret = 0;
    /* H264 monochrome not supported in 8170 */
    if((pp_c->hw_id == 0x8170U) &&
        (pp_c->dec_type == PP_PIPELINED_DEC_TYPE_H264))
      ret = 0;
    break;
  case PP_PIX_FMT_YCBCR_4_2_2_SEMIPLANAR:
  case PP_PIX_FMT_YCBCR_4_4_0:
  case PP_PIX_FMT_YCBCR_4_1_1_SEMIPLANAR:
  case PP_PIX_FMT_YCBCR_4_4_4_SEMIPLANAR:
    /* these supported just in JPEG pipeline mode */
    if(dec_linked == 0 || pp_c->dec_type != PP_PIPELINED_DEC_TYPE_JPEG)
      ret = 0;
    break;
  case PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED:
    /* these are not supported in pipeline and in 8170 */
    if(dec_linked != 0 || (pp_c->hw_id == 0x8170U))
      ret = 0;
    break;
  default:
    ret = 0;
  }

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : PPIsOutPixFmtOk
    Description     :
    Return type     : u32
    Argument        : u32 pix_fmt
    Argument        : const PPContainer * pp_c
------------------------------------------------------------------------------*/
u32 PPIsOutPixFmtOk(u32 pix_fmt, const PPContainer * pp_c) {
  switch (pix_fmt) {
  case PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR:
  case PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_RGB16_CUSTOM:
  case PP_PIX_FMT_RGB16_5_5_5:
  case PP_PIX_FMT_RGB16_5_6_5:
  case PP_PIX_FMT_BGR16_5_5_5:
  case PP_PIX_FMT_BGR16_5_6_5:
  case PP_PIX_FMT_RGB32_CUSTOM:
  case PP_PIX_FMT_RGB32:
  case PP_PIX_FMT_BGR32:
    return 1;
  case PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED:
    if(pp_c->hw_id == 0x8170U)
      return 0;
    else
      return 1;
  case PP_PIX_FMT_YCBCR_4_2_2_TILED_4X4:
  case PP_PIX_FMT_YCRYCB_4_2_2_TILED_4X4:
  case PP_PIX_FMT_CBYCRY_4_2_2_TILED_4X4:
  case PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4:
    if(pp_c->hw_id == 0x8170U)
      return 0;
    else
      return (pp_c->tiled_ena);
  default:
    return 0;
  }
}

/*------------------------------------------------------------------------------
    Function name   : PPIsOutPixFmtBlendOk
    Description     :
    Return type     : u32
    Argument        : u32 pix_fmt
------------------------------------------------------------------------------*/
u32 PPIsOutPixFmtBlendOk(u32 pix_fmt) {
  switch (pix_fmt) {
  case PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED:
  case PP_PIX_FMT_YCBCR_4_2_2_TILED_4X4:
  case PP_PIX_FMT_YCRYCB_4_2_2_TILED_4X4:
  case PP_PIX_FMT_CBYCRY_4_2_2_TILED_4X4:
  case PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4:
  case PP_PIX_FMT_RGB16_CUSTOM:
  case PP_PIX_FMT_RGB16_5_5_5:
  case PP_PIX_FMT_RGB16_5_6_5:
  case PP_PIX_FMT_BGR16_5_5_5:
  case PP_PIX_FMT_BGR16_5_6_5:
  case PP_PIX_FMT_RGB32_CUSTOM:
  case PP_PIX_FMT_RGB32:
  case PP_PIX_FMT_BGR32:
    return 1;
  default:
    return 0;
  }
}

/*------------------------------------------------------------------------------
    Function name   : PPSetupScaling
    Description     :
    Return type     : void
    Argument        : PPContainer * pp_c
    Argument        : const PPOutImage *pp_out_img
------------------------------------------------------------------------------*/
void PPSetupScaling(PPContainer * pp_c, const PPOutImage * pp_out_img) {
  u32 *pp_regs = pp_c->pp_regs;
  PPInCropping *pp_in_crop;
  u32 in_width, in_height;
  u32 in_act_height;
  u32 out_act_height;
  u32 pix_format_ok;

  pp_in_crop = &pp_c->pp_cfg.pp_in_crop;

  if(pp_in_crop->enable)
    in_act_height = pp_in_crop->height;
  else
    in_act_height = pp_c->in_height;

  /* swap width and height if input is rotated first */
  if(pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_LEFT_90 ||
      pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_RIGHT_90) {
    if(pp_in_crop->enable) {
      in_width = pp_in_crop->height;
      in_height = pp_in_crop->width;
    } else {
      in_width = pp_c->in_height;
      in_height = pp_c->in_width;
    }
    out_act_height = pp_out_img->width;
  } else {
    if(pp_in_crop->enable) {
      in_width = pp_in_crop->width;
      in_height = pp_in_crop->height;
    } else {
      in_width = pp_c->in_width;
      in_height = pp_c->in_height;
    }
    out_act_height = pp_out_img->height;
  }

  if(in_width < pp_out_img->width) {
    /* upscale */
    u32 W, inv_w;

    SetPpRegister(pp_regs, HWIF_HOR_SCALE_MODE, 1);

    W = FDIVI(TOFIX((pp_out_img->width - 1), 16), (in_width - 1));

    SetPpRegister(pp_regs, HWIF_SCALE_WRATIO, W);

    inv_w = FDIVI(TOFIX((in_width - 1), 16), (pp_out_img->width - 1));

    SetPpRegister(pp_regs, HWIF_WSCALE_INVRA, inv_w);
  } else if(in_width > pp_out_img->width) {
    /* downscale */

    SetPpRegister(pp_regs, HWIF_HOR_SCALE_MODE, 2);

    pp_c->c_hnorm = FDIVI(TOFIX((pp_out_img->width), 16), in_width);
    pp_c->c_hfast = FDIVI(TOFIX((2*pp_out_img->width), 16), in_width);
    pp_c->c_hfast4x = FDIVI(TOFIX((4*pp_out_img->width), 16), in_width);
    if(pp_c->c_hworkaround_flag) {
      u32 pos0, pos1;
      pos0 = pp_c->c_hnorm*in_width >> 16;
      pos1 = pp_c->c_hnorm*(in_width-1) >> 16;
      if(pos0 != pos1) { /* HW "leftover" bug */
        /* Increase scaling coefficient until full pixel generated
         * from last input-pixel. */
        while(pos0 < pp_out_img->width) {
          pp_c->c_hnorm++;
          pos0 = pp_c->c_hnorm*in_width>>16;
        }
      }
    }

    SetPpRegister(pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hnorm);
  } else {
    SetPpRegister(pp_regs, HWIF_WSCALE_INVRA, 0);
    SetPpRegister(pp_regs, HWIF_HOR_SCALE_MODE, 0);
  }

  if(in_height < pp_out_img->height) {
    /* upscale */
    u32 H, inv_h;

    SetPpRegister(pp_regs, HWIF_VER_SCALE_MODE, 1);

    H = FDIVI(TOFIX((pp_out_img->height - 1), 16), (in_height - 1));

    SetPpRegister(pp_regs, HWIF_SCALE_HRATIO, H);

    inv_h = FDIVI(TOFIX((in_height - 1), 16), (pp_out_img->height - 1));

    SetPpRegister(pp_regs, HWIF_HSCALE_INVRA, inv_h);
  } else if(in_height > pp_out_img->height) {
    /* downscale */
    u32 Cv;

    Cv = FDIVI(TOFIX((pp_out_img->height), 16), in_height) + 1;

    SetPpRegister(pp_regs, HWIF_VER_SCALE_MODE, 2);

    SetPpRegister(pp_regs, HWIF_HSCALE_INVRA, Cv);
  } else {
    SetPpRegister(pp_regs, HWIF_HSCALE_INVRA, 0);
    SetPpRegister(pp_regs, HWIF_VER_SCALE_MODE, 0);
  }

  /* Check vertical scaling shortcut via interlacing input content */
  if(in_act_height > out_act_height &&
      !pp_c->fast_vertical_downscale_disable &&
      pp_c->dec_inst &&
      pp_c->dec_type != PP_PIPELINED_DEC_TYPE_JPEG &&
      pp_c->dec_type != PP_PIPELINED_DEC_TYPE_WEBP) {
    /* downscale */

    pp_c->c_vnorm = FDIVI(TOFIX((out_act_height), 16), in_act_height) + 1;
    pp_c->c_vfast = FDIVI(TOFIX((out_act_height), 16), in_act_height/2) + 1;
    pp_c->fast_scale_mode = -1;

    /* If downscaling by at least factor of two, try field processing
     * to speed up things. Following criteria must be met:
     *  - standalone PP
     *  - no 8px crop
     *  - progressive input frame */
    if( pp_c->pp_cfg.pp_in_img.pic_struct == PP_PIC_FRAME_OR_TOP_FIELD &&
        in_act_height >= out_act_height * 2 &&
        (in_act_height & 0xF) == 0 ) {
      pp_c->fast_vertical_downscale = 1;

      /* Set scaling parameters */
      if( in_act_height == out_act_height * 2 ) {
        /* Scaling factor is 1/2, so just process top field */
        pp_c->fast_scale_mode = 0;
      } else {
        /* Scaling factor is < 1/2, so we must process fields *and*
         * scale */
        pp_c->fast_scale_mode = 2;
      }
    } else { /* Regular downscale */
      pp_c->fast_vertical_downscale = 0;
    }
  }

  /* Only 4:2:0 and 4:0:0 inputs supported */
  pix_format_ok =
    (pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_2_0_PLANAR ||
     pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR ||
     pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_2_0_TILED ||
     pp_c->pp_cfg.pp_in_img.pix_format == PP_PIX_FMT_YCBCR_4_0_0);

  /* If in stand-alone mode (without dec), apply fast vertical downscaling
   * settings now */
  /* NOTE: prefer quality in stand-alone mode; this is now disabled */
#if 0
  if(pp_c->fast_vertical_downscale &&
      pp_c->pp_cfg.pp_in_img.pic_struct == PP_PIC_FRAME_OR_TOP_FIELD &&
      pp_c->dec_inst == NULL &&
      pix_format_ok ) {
    u32 rotate90;

    rotate90 = (pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_LEFT_90 ||
                pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_RIGHT_90);

    /* Set new image height and cropping parameters */
    SetPpRegister(pp_regs, HWIF_PP_IN_STRUCT, PP_PIC_TOP_FIELD_FRAME );

    /* Set new scaling coefficient */
    if(rotate90)
      SetPpRegister(pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hfast);
    else
      SetPpRegister(pp_regs, HWIF_HSCALE_INVRA, pp_c->c_vfast);

    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_H_EXT,
                  ((((pp_c->in_height+31) / 32) & 0x700) >> 8));
    SetPpRegister(pp_c->pp_regs, HWIF_PP_IN_HEIGHT,
                  (((pp_c->in_height+31) / 32) & 0x0FF));
    if(in_height & 0x1F) SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 1);
    else                SetPpRegister(pp_c->pp_regs, HWIF_PP_CROP8_D_E, 0);

    /* Disable deinterlacing */
    SetPpRegister(pp_regs, HWIF_DEINT_E, 0 );

    /* Set fast scale mode */
    if(!rotate90)
      SetPpRegister(pp_regs, HWIF_VER_SCALE_MODE, pp_c->fast_scale_mode);
  } else {
    /*pp_c->fast_vertical_downscale = 0;*/
  }
#endif

  /* Fast horizontal downscaling allowed if
   * 1) Downscaling ratio at least 1/2
   * 2) No 8px crop
   * 3) HW supports it (and not disabled explicitly)
   * 4) Pixel format is valid (i.e. 4:2:0 or 4:0:0)
   * 5) Using dec+pp in video mode
   */
  pp_c->fast_horizontal_downscale = 0;
  if(pp_c->fast_scaling_support == PP_FAST_SCALING_SUPPORTED &&
      !pp_c->fast_horizontal_downscale_disable &&
      pix_format_ok &&
      pp_out_img->width*2 < in_width &&
      !(pp_in_crop->width & 0x0F) &&
      pp_c->dec_inst &&
      pp_c->dec_type != PP_PIPELINED_DEC_TYPE_JPEG &&
      pp_c->dec_type != PP_PIPELINED_DEC_TYPE_WEBP ) {
    /* Recalculate horizontal coefficient */
    SetPpRegister(pp_regs, HWIF_PP_FAST_SCALE_E, 1);
    pp_c->fast_horizontal_downscale = 1;

    /* Check special case with +-90 deg rotation */
    if(pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_LEFT_90 ||
        pp_c->pp_cfg.pp_in_rotation.rotation == PP_ROTATION_RIGHT_90) {
      /* If scaled width is less than 1/4 of original, we can use
       * both horizontal and vertical shortcuts here (i.e. use
       * 4x scaling coefficient */
      if( (pp_out_img->width*4 < in_width) && pp_c->fast_vertical_downscale)
        SetPpRegister(pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hfast4x);
      /* If scaled width is >= 1/4x, we must turn off either of
       * the optimizations, in this case we choose to turn off
       * horizontal shortcut */
      else if( pp_c->fast_vertical_downscale ) {
        SetPpRegister(pp_regs, HWIF_PP_FAST_SCALE_E, 0);
        pp_c->fast_horizontal_downscale = 0;
      }
      /* Vertical downscale shortcut not used, so just enable
       * horizontal component */
      else
        SetPpRegister(pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hfast);
    } else {
      SetPpRegister(pp_regs, HWIF_WSCALE_INVRA, pp_c->c_hfast);
    }
  } else {
    SetPpRegister(pp_regs, HWIF_PP_FAST_SCALE_E, 0);
  }
}

/*------------------------------------------------------------------------------
    Function name   : PPCheckAllXParams
    Description     :
    Return type     : u32
    Argument        : PPConfig * pp_cfg
------------------------------------------------------------------------------*/
static i32 PPCheckAllWidthParams(PPConfig * pp_cfg, u32 blend_ena, u32 pix_acc,
                                 u32 ablend_crop ) {
  PPOutMask1 *pp_out_mask1;
  PPOutMask2 *pp_out_mask2;
  PPOutImage *pp_out_img;
  PPOutFrameBuffer *pp_out_frm_buffer;
  u32 frame_buffer_required = 0; /* Flag to signal that frame buffer is required. This is the
                                  * case if out size is "pixel accurate" */

  i32 ret = (i32) PP_OK;
  u32 multiple;
  u32 multiple_frm_buffer;

  ASSERT(pp_cfg != NULL);
  pp_out_mask1 = &pp_cfg->pp_out_mask1;
  pp_out_mask2 = &pp_cfg->pp_out_mask2;

  pp_out_img = &pp_cfg->pp_out_img;
  pp_out_frm_buffer = &pp_cfg->pp_out_frm_buffer;

  multiple_frm_buffer = PP_X170_DATA_BUS_WIDTH;
  if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB_MASK) {
    if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB32_MASK) {
      multiple_frm_buffer = multiple_frm_buffer / 4;    /* 4 bytes per pixel */
    } else {
      multiple_frm_buffer = multiple_frm_buffer / 2;    /* 2 bytes per pixel */
    }
  } else if(pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED ||
            pp_out_img->pix_format == PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED ||
            pp_out_img->pix_format == PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED ||
            pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED) {
    multiple_frm_buffer = multiple_frm_buffer / 2;    /* 2 bytes per pixel */
  }

  if(pix_acc) {
    if((pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) ||
        (pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV422_MASK)) {
      multiple = 2;
    } else { /* RGB, YUV4:4:4, YUV4:0:0 */
      multiple = 1;
    }
  } else {
    multiple = multiple_frm_buffer;
  }

  if(!pix_acc) {
    if(pp_out_img->width & (WIDTH_MULTIPLE - 1))
      ret = (i32) PP_SET_OUT_SIZE_INVALID;
  } else {
    if(pp_out_img->width & (multiple - 1))
      ret = (i32) PP_SET_OUT_SIZE_INVALID;
    /* Do we have "pixel accurate output width? */
    if(pp_out_img->width & (WIDTH_MULTIPLE - 1)) {
      /* If output width is in "pixel domain", we must have a frame
       * buffer specified. Exception to this check happens when PP
       * output is in tiled mode; here we skip the frame buffer
       * check for that and trust on PPCheckTiledOutput() */
      if( pp_out_img->pix_format != PP_PIX_FMT_YCBCR_4_2_2_TILED_4X4 &&
          pp_out_img->pix_format != PP_PIX_FMT_YCRYCB_4_2_2_TILED_4X4 &&
          pp_out_img->pix_format != PP_PIX_FMT_CBYCRY_4_2_2_TILED_4X4 &&
          pp_out_img->pix_format != PP_PIX_FMT_CRYCBY_4_2_2_TILED_4X4 ) {
        frame_buffer_required = 1;
      }
    }
  }

  if(pp_out_mask1->enable && (pp_out_mask1->width & (multiple - 1)))
    ret = (i32) PP_SET_MASK1_INVALID;
  if(pp_out_mask1->enable && (pp_out_mask1->origin_x & (multiple - 1)))
    ret = (i32) PP_SET_MASK1_INVALID;
  if(pp_out_mask2->enable && (pp_out_mask2->width & (multiple - 1)))
    ret = (i32) PP_SET_MASK2_INVALID;
  if(pp_out_mask2->enable && (pp_out_mask2->origin_x & (multiple - 1)))
    ret = (i32) PP_SET_MASK2_INVALID;

  if(pp_out_mask1->enable && (pp_out_mask1->width == 0 ||
                              pp_out_mask1->height == 0 ))
    ret = (i32) PP_SET_MASK1_INVALID;
  if(pp_out_mask2->enable && (pp_out_mask2->width == 0 ||
                              pp_out_mask2->height == 0 ))
    ret = (i32) PP_SET_MASK2_INVALID;

  /* Check if blending is enabled in HW */
  if(pp_out_mask1->alpha_blend_ena && !blend_ena) {
    ret = (i32) PP_SET_ABLEND_UNSUPPORTED;
  }

  if(pp_out_mask2->alpha_blend_ena && !blend_ena) {
    ret = (i32) PP_SET_ABLEND_UNSUPPORTED;
  }

  if(pp_out_mask1->enable && pp_out_mask1->alpha_blend_ena) {
    /* Blending masks only for 422 and rbg */
    if(!PPIsOutPixFmtBlendOk(pp_out_img->pix_format)) {
      ret = (i32) PP_SET_OUT_FORMAT_INVALID; /*PP_SET_MASK1_INVALID;*/
    }

    if((pp_out_mask1->width + pp_out_mask1->origin_x) > pp_out_img->width)
      ret = (i32) PP_SET_MASK1_INVALID;

    if(pp_out_mask1->origin_y < 0)
      ret = (i32) PP_SET_MASK1_INVALID;

    if((pp_out_mask1->height + pp_out_mask1->origin_y) > pp_out_img->height)
      ret = (i32) PP_SET_MASK1_INVALID;

    if(pp_out_mask1->origin_x < 0)
      ret = (i32) PP_SET_MASK1_INVALID;

    /* More checks if cropped alphablending supported */
    if(ablend_crop) {
      if(pp_out_mask1->blend_width > PP_IN_MAX_BLEND_SCANLINE )
        ret = (i32) PP_SET_MASK1_INVALID;

      if(pp_out_mask1->blend_width == 0 ||
          pp_out_mask1->blend_height == 0 )
        ret = (i32) PP_SET_MASK1_INVALID;

      if(pp_out_mask1->origin_x < 0)
        ret = (i32) PP_SET_MASK1_INVALID;

      if(pp_out_mask1->origin_y < 0)
        ret = (i32) PP_SET_MASK1_INVALID;

      /* Check blend area issues */
      if((pp_out_mask1->width + pp_out_mask1->blend_origin_x) > pp_out_mask1->blend_width ) {
        ret = (i32) PP_SET_MASK1_INVALID;
      }

      if((pp_out_mask1->height + pp_out_mask1->blend_origin_y) > pp_out_mask1->blend_height) {
        ret = (i32) PP_SET_MASK1_INVALID;;
      }

      /* Blend area width must be multiple of 2 */
      if( pp_out_mask1->blend_width & 1 )
        ret = (i32) PP_SET_MASK1_INVALID;
    } else {
      /* if alpha blend crop feature is not supported by HW, these
       * must be left to zero */
      if( pp_out_mask1->blend_origin_x != 0 ||
          pp_out_mask1->blend_origin_y != 0 ||
          pp_out_mask1->blend_width != 0 ||
          pp_out_mask1->blend_height != 0 )
        ret = (i32) PP_SET_MASK1_INVALID;
    }

  }

  if(pp_out_mask2->enable && pp_out_mask2->alpha_blend_ena) {
    /* Blending masks only for 422 and rbg */
    if(!PPIsOutPixFmtBlendOk(pp_out_img->pix_format)) {
      ret = (i32) PP_SET_OUT_FORMAT_INVALID; /*PP_SET_MASK2_INVALID;*/
    }

    if((pp_out_mask2->width + pp_out_mask2->origin_x) > pp_out_img->width)
      ret = (i32) PP_SET_MASK2_INVALID;

    if(pp_out_mask2->origin_y < 0)
      ret = (i32) PP_SET_MASK2_INVALID;

    if((pp_out_mask2->height + pp_out_mask2->origin_y) > pp_out_img->height)
      ret = (i32) PP_SET_MASK2_INVALID;

    if(pp_out_mask2->origin_x < 0)
      ret = (i32) PP_SET_MASK2_INVALID;

    /* More checks if cropped alphablending supported */
    if(ablend_crop) {
      if(pp_out_mask2->blend_width > PP_IN_MAX_BLEND_SCANLINE )
        ret = (i32) PP_SET_MASK2_INVALID;

      if(pp_out_mask2->blend_width == 0 ||
          pp_out_mask2->blend_height == 0 )
        ret = (i32) PP_SET_MASK2_INVALID;

      if(pp_out_mask2->origin_x < 0)
        ret = (i32) PP_SET_MASK2_INVALID;

      if(pp_out_mask2->origin_y < 0)
        ret = (i32) PP_SET_MASK2_INVALID;

      /* Check blend area issues */
      if((pp_out_mask2->width + pp_out_mask2->blend_origin_x) > pp_out_mask2->blend_width )
        ret = (i32) PP_SET_MASK2_INVALID;;

      if((pp_out_mask2->height + pp_out_mask2->blend_origin_y) > pp_out_mask2->blend_height)
        ret = (i32) PP_SET_MASK2_INVALID;;

      /* Blend area width must be multiple of 2 */
      if( pp_out_mask2->blend_width & 1 )
        ret = (i32) PP_SET_MASK2_INVALID;
    } else {
      /* if alpha blend crop feature is not supported by HW, these
       * must be left to zero */
      if( pp_out_mask2->blend_origin_x != 0 ||
          pp_out_mask2->blend_origin_y != 0 ||
          pp_out_mask2->blend_width != 0 ||
          pp_out_mask2->blend_height != 0 )
        ret = (i32) PP_SET_MASK2_INVALID;
    }

  }

  if(pp_out_frm_buffer->enable) {
    i32 tmp;

    if((pp_out_frm_buffer->frame_buffer_width & (multiple_frm_buffer - 1)))
      ret = (i32) PP_SET_FRAMEBUFFER_INVALID;
    if((pp_out_frm_buffer->write_origin_x & (multiple - 1)))
      ret = (i32) PP_SET_FRAMEBUFFER_INVALID;

    tmp = pp_out_frm_buffer->write_origin_x;
    tmp = tmp > 0 ? tmp : (-1) * tmp;

    if(((u32) tmp & (multiple - 1)))
      ret = (i32) PP_SET_FRAMEBUFFER_INVALID;
  } else if(frame_buffer_required) {
    ret = (i32) PP_SET_FRAMEBUFFER_INVALID;
  }

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : PPCheckAllHeightParams
    Description     :
    Return type     : u32
    Argument        : PPConfig * pp_cfg
------------------------------------------------------------------------------*/
i32 PPCheckAllHeightParams(PPConfig * pp_cfg, u32 pix_acc) {
  PPOutMask1 *pp_out_mask1;
  PPOutMask2 *pp_out_mask2;
  PPOutImage *pp_out_img;

  i32 ret = (i32) PP_OK;
  /*u32 multiple;*/

  ASSERT(pp_cfg != NULL);

  pp_out_mask1 = &pp_cfg->pp_out_mask1;
  pp_out_mask2 = &pp_cfg->pp_out_mask2;

  pp_out_img = &pp_cfg->pp_out_img;

#if 0
  multiple = PP_X170_DATA_BUS_WIDTH;

  if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB_MASK) {
    if(pp_out_img->pix_format & PP_PIXEL_FORMAT_RGB32_MASK) {
      multiple = multiple / 4;    /* 4 bytes per pixel */
    } else {
      multiple = multiple / 2;    /* 2 bytes per pixel */
    }
  } else if(pp_out_img->pix_format == PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED ||
            pp_out_img->pix_format == PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED ||
            pp_out_img->pix_format == PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED ||
            pp_out_img->pix_format == PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED) {
    multiple = multiple / 2;    /* 2 bytes per pixel */
  }
#endif


  if(!pix_acc) {
    if(pp_out_img->height & (HEIGHT_MULTIPLE - 1))
      return (i32) PP_SET_OUT_SIZE_INVALID;
  } else {
    u32 multiple = 0;
    /* pixel accurate output supported, figure out minimum granularity */
    if(pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) {
      multiple = 2;
    } else { /* RGB, YUV4:4:4, YUV4:2:2, YUV4:0:0 */
      multiple = 1;
    }
    if(pp_out_img->height & (multiple - 1))
      ret = (i32) PP_SET_OUT_SIZE_INVALID;
  }


  if(pp_out_mask1->enable &&
      (pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) &&
      (pp_out_mask1->origin_y & 1))
    ret = (i32) PP_SET_MASK1_INVALID;

  if(pp_out_mask1->enable &&
      (pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) &&
      (pp_out_mask1->height & 1))
    ret = (i32) PP_SET_MASK1_INVALID;

  if(pp_out_mask2->enable &&
      (pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) &&
      (pp_out_mask2->origin_y & 1))
    ret = (i32) PP_SET_MASK2_INVALID;

  if(pp_out_mask2->enable &&
      (pp_out_img->pix_format & PP_PIXEL_FORMAT_YUV420_MASK) &&
      (pp_out_mask2->height & 1))
    ret = (i32) PP_SET_MASK2_INVALID;

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : PPContinuosCheck
    Description     : Check are the ones only one-after-another
    Return type     : i32
    Argument        : u32
------------------------------------------------------------------------------*/
i32 PPContinuousCheck(u32 value) {

  i32 ret = (i32) PP_OK;
  u32 first = 0;
  u32 tmp = 0;

  if(value) {
    do {
      tmp = value & 1;
      if(tmp)
        ret = (i32) PP_OK;
      else
        ret = (i32) PP_PARAM_ERROR;

      first |= tmp;

      value = value >> 1;
      if(!tmp && !tmp && first)
        break;

    } while(value);
  }

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : PPCheckOverlapping
    Description     : Check if values overlap
    Return type     : i32
    Argument        : u32 a b c d
------------------------------------------------------------------------------*/

i32 PPCheckOverlapping(u32 a, u32 b, u32 c, u32 d) {

  if((a & b) || (a & c) || (a & d) || (b & c) || (b & d) || (c & d)) {
    return (i32) PP_PARAM_ERROR;
  } else {
    return (i32) PP_OK;
  }

}

/*------------------------------------------------------------------------------
    Function name   : PPSelectOutputSize
    Description     : Select output size  based on the HW version info
    Return type     : i32
    Argument        : pp container *
------------------------------------------------------------------------------*/

i32 PPSelectOutputSize(PPContainer * pp_c) {
  u32 tmp;
  DWLHwConfig hw_config;

  ASSERT(pp_c != NULL);

  pp_c->alt_regs = 1;

  DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_PP);
  pp_c->max_out_width = hw_config.max_pp_out_pic_width;
  pp_c->max_out_height = 4096;

  pp_c->blend_ena = hw_config.pp_config & PP_ALPHA_BLENDING ? 1 : 0;
  pp_c->deint_ena = hw_config.pp_config & PP_DEINTERLACING ? 1 : 0;
  pp_c->dither_ena = hw_config.pp_config & PP_DITHERING ? 1 : 0;
  pp_c->scaling_ena = hw_config.pp_config & PP_SCALING ? 1 : 0;
  pp_c->tiled_ena = hw_config.pp_config & PP_TILED_4X4 ? 1 : 0;
  pp_c->pix_acc_support = hw_config.pp_config & PP_PIX_ACC_OUTPUT ? 1 : 0;
  pp_c->blend_crop_support = hw_config.pp_config & PP_ABLEND_CROP ? 1 : 0;

  if( pp_c->fast_scaling_support == PP_FAST_SCALING_UNINITIALIZED ) {
#define PP_SCALING_BITS_OFFSET (26)

    /* scale_level needs to be 0x3 in order to support "fast" downscaling */
    tmp = (hw_config.pp_config & PP_SCALING) >> PP_SCALING_BITS_OFFSET;
    if( tmp == 0x3) {
      pp_c->fast_scaling_support = PP_FAST_SCALING_SUPPORTED;
    } else {
      pp_c->fast_scaling_support = PP_FAST_SCALING_NOT_SUPPORTED;
    }
  }

  PPDEBUG_PRINT(("Alt regs, output size %d\n", pp_c->max_out_height));
  return (i32) PP_OK;
}

/*------------------------------------------------------------------------------
    Function name   : PPSetDithering
    Description     : Set up dithering
    Return type     :
    Argument        : pp container *
------------------------------------------------------------------------------*/
static void PPSetDithering(PPContainer * pp_c) {
  PPOutImage *pp_out_img;
  PPRgbBitmask *rgbbm;
  u32 *pp_regs;
  u32 tmp = 0;

  ASSERT(pp_c != NULL);

  pp_out_img = &pp_c->pp_cfg.pp_out_img;
  pp_regs = pp_c->pp_regs;

  switch (pp_out_img->pix_format) {
  case PP_PIX_FMT_RGB16_5_5_5:
  case PP_PIX_FMT_BGR16_5_5_5:
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_R, 2);
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_G, 2);
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_B, 2);
    break;
  case PP_PIX_FMT_RGB16_5_6_5:
  case PP_PIX_FMT_BGR16_5_6_5:
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_R, 2);
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_G, 3);
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_B, 2);
    break;
  case PP_PIX_FMT_RGB16_CUSTOM:
  case PP_PIX_FMT_RGB32_CUSTOM:

    rgbbm = &pp_c->pp_cfg.pp_out_rgb.rgb_bitmask;

    tmp = PPSelectDitheringValue(rgbbm->mask_r);
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_R, tmp);

    tmp = PPSelectDitheringValue(rgbbm->mask_g);
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_G, tmp);

    tmp = PPSelectDitheringValue(rgbbm->mask_b);
    SetPpRegister(pp_regs, HWIF_DITHER_SELECT_B, tmp);

    break;

  default:
    break;
  }

}

/*------------------------------------------------------------------------------
    Function name   : PPCountOnes
    Description     : one ones in value
    Return type     : number of ones
    Argument        : u32 value
------------------------------------------------------------------------------*/
static u32 PPCountOnes(u32 value) {
  u32 ones = 0;

  if(value) {
    do {
      if(value & 1)
        ones++;

      value = value >> 1;
    } while(value);
  }
  return ones;
}

/*------------------------------------------------------------------------------
    Function name   : PPSelectDitheringValue
    Description     : select dithering matrix for color depth set in mask
    Return type     : u32, dithering value which is set to HW
    Argument        : u32 mask, mask for selecting color depth
------------------------------------------------------------------------------*/
static u32 PPSelectDitheringValue(u32 mask) {

  u32 ones = 0;
  u32 ret = 0;

  ones = PPCountOnes(mask);

  switch (ones) {
  case 4:
    ret = 1;
    break;
  case 5:
    ret = 2;
    break;
  case 6:
    ret = 3;
    break;
  default:
    ret = 0;
    break;
  }

  return ret;

}

/*------------------------------------------------------------------------------
    Function name   : WaitForPp
    Description     : Wait PP HW to finish
    Return type     : PPResult
    Argument        : PPContainer *
------------------------------------------------------------------------------*/
PPResult WaitForPp(PPContainer * pp_c) {
  const void *dwl;
  i32 dwlret = 0;
  u32 irq_stat;
  PPResult ret = PP_OK;

  dwl = pp_c->dwl;

  dwlret = DWLWaitHwReady(dwl, pp_c->core_id, (u32) (-1));

  if(dwlret == DWL_HW_WAIT_TIMEOUT) {
    ret = PP_HW_TIMEOUT;
  } else if(dwlret == DWL_HW_WAIT_ERROR) {
    ret = PP_SYSTEM_ERROR;
  }

  PPRefreshRegs(pp_c);

  irq_stat = GetPpRegister(pp_c->pp_regs, HWIF_PP_IRQ_STAT);

  /* make sure ASIC is OFF */
  SetPpRegister(pp_c->pp_regs, HWIF_PP_E, 0);
  SetPpRegister(pp_c->pp_regs, HWIF_PP_IRQ, 0);
  SetPpRegister(pp_c->pp_regs, HWIF_PP_IRQ_STAT, 0);
  /* also disable pipeline bit! */
  SetPpRegister(pp_c->pp_regs, HWIF_PP_PIPELINE_E, 0);

  DWLDisableHw(pp_c->dwl, pp_c->core_id, PP_X170_REG_START, pp_c->pp_regs[0]);
  DWLReleaseHw(pp_c->dwl, pp_c->core_id);

  PPSetStatus(pp_c, PP_STATUS_IDLE);

  if(irq_stat & DEC_8170_IRQ_BUS)
    ret = PP_HW_BUS_ERROR;

  return ret;
}

/*------------------------------------------------------------------------------
    Function name   : PPCheckSetupChanges
    Description     : Check changes in PP config
    Return type     : PPResult
    Argument        : PPContainer *
------------------------------------------------------------------------------*/
u32 PPCheckSetupChanges(PPConfig * prev_cfg, PPConfig * new_cfg) {

  u32 changes = 0;

  PPOutImage *prev_out_img, *new_out_img;
  PPInCropping *prev_in_cropping, *new_in_cropping;
  PPOutMask1 *prev_mask1, *new_mask1;
  PPOutMask2 *prev_mask2, *new_mask2;
  PPOutFrameBuffer *prev_frame_buffer, *new_frame_buffer;
  PPInRotation *prev_rotation, *new_rotation;

  prev_out_img = &prev_cfg->pp_out_img;
  prev_in_cropping = &prev_cfg->pp_in_crop;
  prev_mask1 = &prev_cfg->pp_out_mask1;
  prev_mask2 = &prev_cfg->pp_out_mask2;
  prev_frame_buffer = &prev_cfg->pp_out_frm_buffer;
  prev_rotation = &prev_cfg->pp_in_rotation;

  new_out_img = &new_cfg->pp_out_img;
  new_in_cropping = &new_cfg->pp_in_crop;
  new_mask1 = &new_cfg->pp_out_mask1;
  new_mask2 = &new_cfg->pp_out_mask2;
  new_frame_buffer = &new_cfg->pp_out_frm_buffer;
  new_rotation = &new_cfg->pp_in_rotation;

  /* output picture parameters */
  if(prev_out_img->width != new_out_img->width ||
      prev_out_img->height != new_out_img->height ||
      prev_out_img->pix_format != new_out_img->pix_format)
    changes++;

  /* checked bacause changes pipeline status */
  if(prev_in_cropping->enable != new_in_cropping->enable)
    changes++;

  /* checked bacause changes pipeline status */
  if(prev_rotation->rotation != new_rotation->rotation)
    changes++;

  if(prev_mask1->enable != new_mask1->enable ||
      prev_mask1->origin_x != new_mask1->origin_x ||
      prev_mask1->origin_y != new_mask1->origin_y ||
      prev_mask1->height != new_mask1->height ||
      prev_mask1->width != new_mask1->width)
    changes++;

  if(prev_mask2->enable != new_mask2->enable ||
      prev_mask2->origin_x != new_mask2->origin_x ||
      prev_mask2->origin_y != new_mask2->origin_y ||
      prev_mask2->height != new_mask2->height ||
      prev_mask2->width != new_mask2->width)
    changes++;

  if(prev_frame_buffer->enable != new_frame_buffer->enable ||
      prev_frame_buffer->write_origin_x != new_frame_buffer->write_origin_x ||
      prev_frame_buffer->write_origin_y != new_frame_buffer->write_origin_y ||
      prev_frame_buffer->frame_buffer_width != new_frame_buffer->frame_buffer_width ||
      prev_frame_buffer->frame_buffer_height != new_frame_buffer->frame_buffer_height)
    changes++;

  return changes;

}

