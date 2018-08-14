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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../source/pp/ppinternal.c"
#include "../../../source/common/regdrv.c"

/* stubs for DWL functions */
i32 DWLReserveHw(const void *dwl) {
  i32 ret_val = DWL_OK;
  /*    static u32 callCount = 0;

      switch (callCount)
      {
          case 0:
              ret_val = DWL_ERROR;
              break;
      }
      callCount++;*/
  return(ret_val);
}

void  DWLReleaseHw(const void *dwl) {
}

void DWLEnableHw(const void *dwl) {
}

void DWLWriteReg(const void *dwl, u32 offset, u32 val) {
}

u32 DWLReadReg(const void *dwl, u32 offset) {
  u32 ret_val = 0;
  /*    static u32 callCount = 0;

      if (offset == 0x4)
      {
          switch (callCount)
          {
              case 2:
                  ret_val = 0x00015000;
                  break;
              case 3:
                  ret_val = 0x00001000;
                  break;
              case 4:
                  ret_val = 0x00004000;
                  break;
          }
          callCount++;
      }*/
  return(ret_val);
}

i32 DWLWaitHwReady(const void *dwl, u32 time_out) {
  i32 ret_val = DWL_HW_WAIT_OK;
  /*   static u32 callCount = 0;

     switch (callCount)
     {
         case 0:
             ret_val = DWL_HW_WAIT_TIMEOUT;
             break;
         case 1:
             ret_val = DWL_HW_WAIT_ERROR;
             break;
     }
     callCount++;*/
  return(ret_val);
}
void* DWLmalloc(u32 size) {
  /*   if (++mallocCtr == mallocFail)
         return NULL;
  */
  return malloc(size);
}

i32 DWLMallocLinear(const void* dwl, u32 size, struct DWLLinearMem *info) {
  /*   if (++mallocCtr == mallocFail)
         return 1;
  */
  info->virtual_address = malloc(size);
  info->bus_address = (u32)info->virtual_address;
  return 0;
}
void DWLfree(void *ptr) {
  free(ptr);
}
void DWLFreeLinear(const void *dwl, struct DWLLinearMem *info) {
  free(info->virtual_address);
}

u32 DWLReadAsicID(void) {
  return 0xFF;
}
void *DWLmemset(void *d, i32 c, u32 n) {
  return memset(d, (int) c, (size_t) n);
}
void DWLReadAsicConfig(DWLHwConfig * hw_cfg,u32 client_type) {
#if 0
  DWLHwConfig config;
  config.max_decoding_width = 1280; /* Maximum decoding width supported
                                       by the HW */
  config.max_pp_out_width = 1920; /* Maximum output width of Post-Processor */
  config.h264_enabled = 1;  /* HW supports h.264 */
  config.jpeg_enabled = 1;  /* HW supports JPEG */
  config.mpeg4_enabled = 1;  /* HW supports MPEG-4 */
  config.vc1_enabled = 1;  /* HW supports VC-1 */
  config.pp_enabled = 1;  /* HW supports Post-Processor */
#endif
}

void test_PPCountOnes(void) {
  u32 ones;

  ones = PPCountOnes(0);
  assert(ones == 0);
  ones = PPCountOnes(1);
  assert(ones == 1);
  ones = PPCountOnes(0xFFFF);
  assert(ones == 16);

  ones = PPCountOnes(0xFFFFFFFF);
  assert(ones == 32);

  ones = PPCountOnes(0xFFFFFFFE);
  assert(ones == 31);

  ones = PPCountOnes(0x7FFFFFFE);
  assert(ones == 30);

  ones = PPCountOnes(0x300);
  assert(ones == 2);

  ones = PPCountOnes(0x3300);
  assert(ones == 4);

  ones = PPCountOnes(0x80000000);
  assert(ones == 1);
}

void test_PPSelectDitheringValue(void) {
  u32 value = 0;

  value = PPSelectDitheringValue(0);
  assert(value == 0);
  value = PPSelectDitheringValue(3);
  assert(value == 0);

  value = PPSelectDitheringValue(0xF);
  assert(value == 1);
  value = PPSelectDitheringValue(0x1F0);
  assert(value == 2);
  value = PPSelectDitheringValue(0xFC000000);
  assert(value == 3);

  value = PPSelectDitheringValue(0xFE000000);
  assert(value == 0);
  value = PPSelectDitheringValue(0xFFFFFFFF);
  assert(value == 0);
}

void test_PPSetDithering(void) {
  PPContainer pp_c;
  PPOutImage *pp_out_img;
  PPRgbBitmask *rgbbm;
  u32 *pp_regs;
  u32 enable = 0, tmp = 0;

  pp_out_img = &pp_c.pp_cfg.pp_out_img;
  pp_regs = &pp_c.pp_regs;
  rgbbm = &pp_c.pp_cfg.pp_out_rgb.rgb_bitmask;

  pp_out_img->pix_format =PP_PIX_FMT_RGB32;
  PPSetDithering(&pp_c);
  assert(GetPpRegister(pp_regs, HWIF_DITHERING_E) == 0);

  pp_out_img->pix_format = PP_PIX_FMT_YCBCR_4_2_0_PLANAR;
  PPSetDithering(&pp_c);
  assert(GetPpRegister(pp_regs, HWIF_DITHERING_E) == 0);

  pp_out_img->pix_format = PP_PIX_FMT_RGB16_5_5_5;
  rgbbm->mask_r = rgbbm->mask_g = rgbbm->mask_b = 0x1F;
  PPSetDithering(&pp_c);
  assert(GetPpRegister(pp_regs, HWIF_DITHERING_E) == 1);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_R) == 2);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_B) == 2);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_G) == 2);

  pp_out_img->pix_format = PP_PIX_FMT_RGB16_CUSTOM;
  rgbbm->mask_r = rgbbm->mask_g = rgbbm->mask_b = 0xF;
  PPSetDithering(&pp_c);
  assert(GetPpRegister(pp_regs, HWIF_DITHERING_E) == 1);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_R) == 1);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_B) == 1);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_G) == 1);

  pp_out_img->pix_format = PP_PIX_FMT_RGB32_CUSTOM;
  rgbbm->mask_r = rgbbm->mask_g = rgbbm->mask_b = 0x3F;
  PPSetDithering(&pp_c);
  assert(GetPpRegister(pp_regs, HWIF_DITHERING_E) == 1);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_R) == 3);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_B) == 3);
  assert(GetPpRegister(pp_regs, HWIF_DITHER_SELECT_G) == 3);

}

int main(void) {
  i32 ret = 0;

  PPContainer pp_c;
  PPConfig pp_cfg;
#if 0
  memset(&pp_cfg, 0, sizeof(PPConfig));

  /* set values for container */

  pp_c.max_out_width = 1920;
  pp_c.max_out_height = 1920;

  /* ppconfig */
  pp_cfg.pp_out_img.width = 72;
  pp_cfg.pp_out_img.height = 70;
  pp_cfg.pp_in_img.width = 48;
  pp_cfg.pp_in_img.height = 48;
  pp_cfg.pp_in_img.vc1_multi_res_enable = 1;

  pp_cfg.pp_in_img.pix_format = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
  pp_cfg.pp_out_img.buffer_bus_addr = 0x10;
  pp_cfg.pp_out_img.pix_format = PP_PIX_FMT_RGB32;

  ret = PPCheckConfig(&pp_c, &pp_cfg,
                      1, PP_PIPELINED_DEC_TYPE_VC1);

  assert( ret == 0);

  /* widht too big*/

  pp_cfg.pp_out_img.width = 73;
  pp_cfg.pp_out_img.height = 64;
  pp_cfg.pp_in_img.vc1_multi_res_enable = 1;
  ret = PPCheckConfig(&pp_c, &pp_cfg,
                      2, PP_PIPELINED_DEC_TYPE_VC1);
  assert( ret == PP_SET_OUT_SIZE_INVALID);

  /* height too big*/

  pp_cfg.pp_out_img.width = 64;
  pp_cfg.pp_out_img.height = 72;
  pp_cfg.pp_in_img.vc1_multi_res_enable = 1;
  ret = PPCheckConfig(&pp_c, &pp_cfg,
                      2, PP_PIPELINED_DEC_TYPE_VC1);
  assert( ret == PP_SET_OUT_SIZE_INVALID);

  /* But without multires, this is OK*/

  pp_cfg.pp_out_img.width = 64;
  pp_cfg.pp_out_img.height = 72;
  pp_cfg.pp_in_img.vc1_multi_res_enable = 0;
  ret = PPCheckConfig(&pp_c, &pp_cfg,
                      2, PP_PIPELINED_DEC_TYPE_VC1);
  assert( ret == 0);

  /* for h264 different limits*/
  pp_cfg.pp_out_img.width = 3*48;
  pp_cfg.pp_out_img.height = 3*48-2;
  pp_cfg.pp_in_img.vc1_multi_res_enable = 1;
  ret = PPCheckConfig(&pp_c, &pp_cfg,
                      1, PP_PIPELINED_DEC_TYPE_H264);

  assert( ret == 0);

  /* widht too big*/

  pp_cfg.pp_out_img.width = 3*48+WIDTH_MULTIPLE;
  pp_cfg.pp_out_img.height = 3*48-2;
  pp_cfg.pp_in_img.vc1_multi_res_enable = 1;
  ret = PPCheckConfig(&pp_c, &pp_cfg,
                      2, PP_PIPELINED_DEC_TYPE_H264);
  assert( ret == PP_SET_OUT_SIZE_INVALID);

  /* height too big*/

  pp_cfg.pp_out_img.width = 3*48;
  pp_cfg.pp_out_img.height = 3*48;
  pp_cfg.pp_in_img.vc1_multi_res_enable = 0;
  ret = PPCheckConfig(&pp_c, &pp_cfg,
                      2, PP_PIPELINED_DEC_TYPE_H264);
  assert( ret == PP_SET_OUT_SIZE_INVALID);
#endif

  test_PPCountOnes();
  test_PPSelectDitheringValue();
  test_PPSetDithering();
  return 0;
}
