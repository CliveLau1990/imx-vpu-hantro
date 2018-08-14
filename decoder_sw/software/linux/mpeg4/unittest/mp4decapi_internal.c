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
#include "mp4dechwd_headers.h"
#include "mp4dechwd_container.h"
#include "mp4dechwd_utils.h"
#include "../../../source/mpeg4/mp4decapi_internal.c"
#include "../../../source/mpeg4/mp4dechwd_utils.c"
#include "unit_dwl.c"

extern DWLHwConfig hw_config;
const u8 asic_pos_no_rlc[6] = { 27, 26, 25, 24, 23, 22 };


void set_simple_profile(DecContainer *dec_cont) {
  dec_cont->Hdrs.quant_type = 0;
  dec_cont->Hdrs.low_delay = 1;
  dec_cont->Hdrs.interlaced = 0;
  dec_cont->Hdrs.quarterpel = 0;
}

void test_MP4DecCheckProfileSupport(void) {

  DecContainer container;
  set_simple_profile(&container);
  container.StrmStorage.sorenson_spark = 0;

  /* simple */
  hw_config.mpeg4_support = MPEG4_SIMPLE_PROFILE;
  assert(!MP4DecCheckProfileSupport(&container));

  container.Hdrs.quant_type = 1;
  assert(MP4DecCheckProfileSupport(&container));

  container.StrmStorage.sorenson_spark = 1;
  assert(!MP4DecCheckProfileSupport(&container));
  container.StrmStorage.sorenson_spark = 0;
  set_simple_profile(&container);

  container.Hdrs.low_delay = 0;
  assert(MP4DecCheckProfileSupport(&container));
  set_simple_profile(&container);

  container.Hdrs.interlaced = 1;
  assert(MP4DecCheckProfileSupport(&container));
  set_simple_profile(&container);

  container.Hdrs.quarterpel = 1;
  assert(MP4DecCheckProfileSupport(&container));
  set_simple_profile(&container);
  /* ASP */

  hw_config.mpeg4_support = MPEG4_ADVANCED_SIMPLE_PROFILE;
  assert(!MP4DecCheckProfileSupport(&container));

  container.Hdrs.quant_type = 1;
  container.Hdrs.low_delay = 0;
  container.Hdrs.interlaced = 1;
  container.Hdrs.quarterpel = 1;

  hw_config.mpeg4_support = MPEG4_ADVANCED_SIMPLE_PROFILE;
  assert(!MP4DecCheckProfileSupport(&container));

  /* sorenson */

  container.StrmStorage.sorenson_spark = 1;
  assert(!MP4DecCheckProfileSupport(&container));

  container.StrmStorage.sorenson_spark = 0;
  assert(!MP4DecCheckProfileSupport(&container));

  set_simple_profile(&container);

  container.StrmStorage.sorenson_spark = 1;
  assert(!MP4DecCheckProfileSupport(&container));

  container.StrmStorage.sorenson_spark = 0;
  assert(!MP4DecCheckProfileSupport(&container));


}










int main(void) {

  test_MP4DecCheckProfileSupport();
  return 0;
}
