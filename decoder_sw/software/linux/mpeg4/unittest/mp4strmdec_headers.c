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
#include "../../../source/mpeg4/mp4dechwd_headers.c"
#include "../../../source/mpeg4/mp4dechwd_utils.c"


#define LOAD_STREAM_START    container.StrmDesc.bit_pos_in_word = 0;\
    container.StrmDesc.strm_buff_size = 3*64;\
    container.StrmDesc.strm_buff_read_bits = 0;\
    container.StrmDesc.strm_curr_pos = (u8 *)stream

void *DWLmemset(void *d, i32 c, u32 n) {
  return memset(d, (int) c, (size_t) n);
}


void lowdelay(u32 n) {
  DecContainer container;

  container.Hdrs.profile_and_level_indication = n;
  MP4DecSetLowDelay( &container );
  assert( container.Hdrs.low_delay );
}
void highdelay(u32 n) {
  DecContainer container;

  container.Hdrs.profile_and_level_indication = n;
  MP4DecSetLowDelay( &container );
  assert( !container.Hdrs.low_delay );
}

void MP4DecSetLowDelay_tester(void) {
  highdelay(0);
  lowdelay(1);
  lowdelay(3);
  lowdelay(3);
  lowdelay(4);
  lowdelay(5);
  highdelay(6);
  highdelay(7);

  lowdelay(8);
  lowdelay(9);

  highdelay(10);
  lowdelay(50);
  lowdelay(51);
  lowdelay(52);
}





int main(void) {
  u32 ret;
  /*u32 i;*/
  u8 *quant_matrix;

  u8 stream[3*64] =  {0x01, 0x02, 0x00, 0x01, 0x20, 0x00,
                      0x84, 0x40, 0x00, 0xa8, 0x0c, 0x20,
                      0x30, 0xa3, 0x8f
                     };

  DecContainer container;

  /*
   * QuantMat
   * */

  stream[0] = 8;
  stream[1] = 0;
  stream[2] = 2;
  stream[3] = 0;
  /*
      container.StrmStorage.quant_mat = (u8 *)malloc(64*2);
  */
  quant_matrix = (u8 *)container.StrmStorage.quant_mat;

  LOAD_STREAM_START;

  ret = QuantMat(&container, 1);
  assert(!ret); /* HANTRO_OK*/
  ret = QuantMat(&container, 0);
  assert(!ret); /* HANTRO_OK*/
  assert(quant_matrix[0] == 8);
  assert(quant_matrix[1] == 8);
  assert(quant_matrix[63] == 8);
  assert(quant_matrix[64] == 2);
  assert(quant_matrix[65] == 2);

  LOAD_STREAM_START;
  stream[0] = 2;
  ret = QuantMat(&container, 1);
  assert(ret == HANTRO_OK); /* HANTRO_OK */
  LOAD_STREAM_START;
  ret = QuantMat(&container, 0);
  assert(ret == HANTRO_OK); /* HANTRO_OK*/


  MP4DecSetLowDelay_tester();


  return 0;
}


