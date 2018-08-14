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

#ifndef _PPAPI_INTERNAL_H_
#define _PPAPI_INTERNAL_H_

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "ppapi.h"
#include "basetype.h"
#include "regdrv_g1.h"

/*------------------------------------------------------------------------------
    2. Internal Definitions
------------------------------------------------------------------------------*/

#define PP_X170_REGISTERS       (41)
#define PP_X170_REG_START       (240)
#define PP_X170_EXPAND_REG_START (146 * 4)
#define PP_X170_EXPAND_REGS     (9)
#define TOTAL_X170_REGISTERS    (36 + 119)
#define TOTAL_X170_ORIGIN_REGS  (119)
#define TOTAL_X170_EXPAND_REGS  (36)

#define PP_OUT_MAX_WIDTH_D1     720
#define PP_OUT_MIN_WIDTH    16
#define PP_OUT_MAX_HEIGHT_D1    720
#define PP_OUT_MIN_HEIGHT   16

#define PP_OUT_D1   0
#define PP_OUT_XGA  1

#define PP_OUT_MAX_WIDTH_UPSCALED(d,vc1_p) ((vc1_p) == 0 ? 3*(d) : d)
#define PP_OUT_MAX_HEIGHT_UPSCALED(d,vc1_p ) ((vc1_p) == 0 ?  3*(d) - 2 : d)

/* Minimum downscaling factor, i.e. smaller than this not
 * supported by HW */
#define PP_OUT_MIN_DOWNSCALING_FACTOR (70)

/* Max width of scanline in alphablending (if supported by HW) */
#define PP_IN_MAX_BLEND_SCANLINE    8191

/* Max out width height in JPEG in 292*16 */
#define PP_IN_MAX_WIDTH(pipeline)   ((pipeline) == 0 ? 2048U : 292U*16U)
#define PP_IN_MIN_WIDTH(pipeline)   ((pipeline) == 0 ? 16U : 48U)
#define PP_IN_MAX_HEIGHT(pipeline)  ((pipeline) == 0 ? 2048U : 292U*16U)
#define PP_IN_MIN_HEIGHT(pipeline)  ((pipeline) == 0 ? 16U : 16U)

/* Max out width height in JPEG in 511*16 */
#define PP_IN_MAX_WIDTH_EXT(pipeline,m)   ((pipeline) == 0 ? 4096U : (m)*16U)
#define PP_IN_MAX_HEIGHT_EXT(pipeline,m)  ((pipeline) == 0 ? 4096U : (m)*16U)

#define PP_IN_DIVISIBILITY(linked)       (linked == 0 ? 0xF : 0x7)

#define PP_MAX_FRM_BUFF_WIDTH       4096

#define PP_PIXEL_FORMAT_YUV420_MASK         0x020000U
#define PP_PIXEL_FORMAT_YUV422_MASK         0x010000U
#define PP_PIXEL_FORMAT_RGB_MASK            0x040000U
#define PP_PIXEL_FORMAT_RGB32_MASK          0x001000U

#define PP_ASIC_OUT_FORMAT_RGB          0
#define PP_ASIC_OUT_FORMAT_422          3
#define PP_ASIC_OUT_FORMAT_420          5

#define PP_ASIC_IN_FORMAT_422               0
#define PP_ASIC_IN_FORMAT_420_SEMIPLANAR    1
#define PP_ASIC_IN_FORMAT_420_PLANAR        2
#define PP_ASIC_IN_FORMAT_420_TILED         5
#define PP_ASIC_IN_FORMAT_440_SEMIPLANAR    6

/* extended formats */
#define PP_ASIC_IN_FORMAT_EXTENSION         7
#define PP_ASIC_IN_FORMAT_444_SEMIPLANAR    7
#define PP_ASIC_IN_FORMAT_411_SEMIPLANAR    8

/* inputs supported just in JPEG pipeline */
#define PP_ASIC_IN_FORMAT_400               3
#define PP_ASIC_IN_FORMAT_422_SEMIPLANAR    4

/* Width multiple of: */
#define WIDTH_MULTIPLE 8
#define HEIGHT_MULTIPLE 2

#define TOFIX(d, q) ((u32)( (d) * (u32)(1<<(q)) ))
#define FDIVI(a, b) ((a)/(b))

#define PP_STATUS_IDLE      0
#define PP_STATUS_RUNNING   1

/* it is allowed to allocate full register amount for the shadow regs:
 * but they are not used. so here the constant boolean is OK */
/*lint -e(506) */

/* PPBufferData multibuffer: buffer config data
 * ID and input source pic addr. With this info we can later rerun pictures */
typedef struct BufferData_ {
  addr_t input_bus_luma;
  addr_t input_bus_chroma;
  addr_t bottom_bus_luma;
  addr_t bottom_bus_chroma;
  u32 setup_id;
} PPBufferData;

typedef struct PPContainer_ {
  u32 pp_regs[TOTAL_X170_REGISTERS];
  PPConfig pp_cfg;
  PPConfig prev_cfg;
  PPOutputBuffers combined_mode_buffers;
  PPBufferData buffer_data[PP_MAX_MULTIBUFFER];
  u32 display_index;
  u32 current_setup_id; /* The setup ID given to the previous decoded pic */
  u32 prev_out_setup_id; /* The setup ID of previous output picture */

  PPResult PPCombinedRet; /* pp stores here combined mode errors */

  u32 status;
  u32 pipeline;
  u32 multi_buffer;
  const void *dwl;
  i32 core_id;
  const void *dec_inst;
  u32 dec_type;

  i32 frm_buffer_luma_or_rgb_offset;
  i32 frm_buffer_chroma_offset;
  u32 out_format;
  u32 out_start_ch;
  u32 out_cr_first;
  u32 out_tiled4x4;
  u32 in_format;
  u32 in_start_ch;
  u32 in_cr_first;
  u32 rgb_depth;
  u32 in_width;
  u32 in_height;
  u32 alt_regs;
  u32 max_out_width;
  u32 max_out_height;
  u32 blend_ena;
  u32 deint_ena;
  u32 dither_ena;
  u32 scaling_ena;
  u32 tiled_ena;
  u32 pix_acc_support;
  u32 blend_crop_support;
  u32 fast_scaling_support;
  u32 fast_vertical_downscale;
  u32 fast_horizontal_downscale;

  /* for TB control over scaling shortcut features */
  u32 fast_vertical_downscale_disable;
  u32 fast_horizontal_downscale_disable;

  u32 c_vnorm;
  u32 c_vfast;

  u32 c_hnorm;
  u32 c_hfast;
  u32 c_hfast4x;
  u32 c_hworkaround_flag;
  u32 fast_scale_mode;

  u32 hw_id;
  u32 hw_endian_ver;
  u32 tiled_mode_support;
  u32 jpeg16k_support;
} PPContainer;

/*------------------------------------------------------------------------------
    3. Prototypes of Decoder API internal functions
------------------------------------------------------------------------------*/

void PPInitDataStructures(PPContainer * pp_c);
void PPInitHW(PPContainer * pp_c);
void PPSetupHW(PPContainer * pp_c);
i32 PPCheckConfig(PPContainer * pp_c, PPConfig * pp_cfg,
                  u32 dec_linked, u32 dec_type);
void PPSetupScaling(PPContainer * pp_c, const PPOutImage * pp_out_img);

PPResult PPRun(PPContainer * pp_c);
void PPRefreshRegs(PPContainer * pp_c);
void PPFlushRegs(PPContainer * pp_c);

u32 PPGetStatus(const PPContainer * pp_c);
void PPSetStatus(PPContainer * pp_c, u32 status);

i32 PPSelectOutputSize(PPContainer * pp_c);
PPResult WaitForPp(PPContainer * pp_c);
u32 PPCheckSetupChanges(PPConfig * prev_cfg, PPConfig * new_cfg);
void PPSetConfigIdData(PPContainer * pp_c);

#endif
