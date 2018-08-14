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
#include <assert.h>
#include "mp4decapi.h"
#include "mp4dechwd_headers.h"
#include "mp4dechwd_container.h"
#include "mp4dechwd_utils.h"
#include "../../../source/mpeg4/mp4decapi.c"
#include "../../../source/mpeg4/mp4dechwd_utils.c"
#include "../../../source/mpeg4/mp4decapi_internal.c"
#include "unit_dwl.c"

extern DWLHwConfig hw_config;
extern u32 asicid;

const u8 asic_pos_no_rlc[6] = { 27, 26, 25, 24, 23, 22 };

u32 StrmDec_Decode(DecContainer * dec_container) {
  return 0;
}

u32 StrmDec_SaveUserData(DecContainer * dec_container, u32 u32_mode) {
  return 0;
}

void test_MP4DecGetBuild(void) {

  MP4DecBuild build;

  build = MP4DecGetBuild();

  assert((build.hw_build >> 16) == 0x8170);
}

void test_MP4DecNextPicture(void) {

  DecContainer container;
  MP4DecPicture picture;
  MP4DecRet ret;
  MP4DecInst inst;
  picture_t *p_pic;

  u8 image[64];

  container.Hdrs.low_delay = 0;
  container.StrmStorage.out_count = 1;
  container.ApiStorage.DecStat = INITIALIZED;

  container.StrmStorage.out_count = 1;
  container.StrmStorage.out_index = 0;
  container.StrmStorage.out_buf[0] = 0;
  /*
      p_pic = (picture_t*)container.StrmStorage.pPicBuf;
      p_pic[0].data.virtualAddress = &image;

      ret = MP4DecNextPicture(&container, &picture, 0);
      assert( ret == MP4DEC_OK );
      */
  /*
      container.StrmStorage.out_count = 2;

      ret = MP4DecNextPicture(&container, &picture, 0);
      assert( ret == MP4DEC_PIC_RDY );
      assert( picture.pOutputPicture == image);

      assert (container.StrmStorage.out_count == 1);
      assert (container.StrmStorage.out_index == 1);
  */

}


void tdr(void) {
  u32 i = 0;
  i32 itmp_old = 0;
  u32 itmp_new = 0;

  /*
      for(i=1; i<0xFFFFFFFF; i=i+0xF)
      {
          itmp_old = (((long long int)i<<27) + (i-1))/ i;
          itmp_new = (((long long int)i<<27) + (i-1))/ i;

          printf("%x", ((long long int)i<<27));

          printf("%d\n", i);
          assert(itmp_old == itmp_new);
      }
  */
}


test_MP4CheckFormatSupport(void) {
  /* mpeg-4 */
  hw_config.mpeg4_support = MPEG4_SIMPLE_PROFILE;
  assert(!MP4CheckFormatSupport(0));
  hw_config.mpeg4_support = MPEG4_ADVANCED_SIMPLE_PROFILE;
  assert(!MP4CheckFormatSupport(0));
  hw_config.mpeg4_support = MPEG4_NOT_SUPPORTED;
  assert(MP4CheckFormatSupport(0));

  /* sorenson */
  assert(MP4CheckFormatSupport(1));
  hw_config.sorenson_spark_support = SORENSON_SPARK_SUPPORTED;
  assert(!MP4CheckFormatSupport(1));
  hw_config.sorenson_spark_support = SORENSON_SPARK_NOT_SUPPORTED;
  assert(MP4CheckFormatSupport(1));

  /* product number check */
  hw_config.mpeg4_support = MPEG4_SIMPLE_PROFILE;
  assert(!MP4CheckFormatSupport(0));
  asicid = (0x81901000);
  assert(MP4CheckFormatSupport(0));

  asicid = (0x81701000);
}

int main(void) {

  test_MP4DecGetBuild();
  test_MP4DecNextPicture();
  test_MP4CheckFormatSupport();
  return 0;

}


