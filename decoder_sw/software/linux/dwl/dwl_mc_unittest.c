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

#include <stdlib.h>
#include <string.h>
#include "basetype.h"
#include "dwl.h"
#include "dwlthread.h"
#include "asic.h"
#include "assert.h"
#include "regdrv_g1.h"

static void* MCDecodeThread(void*);
static u32 picSize(void);

void *p_mccore;
u8 * p_mcunit_out;
u32 *p_mcbase;
u32 keep_running;

pthread_t test_mcthread;
pthread_mutex_t unit_core_start;
pthread_mutex_t unit_core_done;

void MCUnitTestInit(void) {
  keep_running = 1;
  p_mcunit_out = NULL;

  p_mccore = (void*)HwCoreInit();
  assert(p_mccore != NULL);

  p_mcbase = HwCoreGetBase(p_mccore);

  pthread_mutex_init(&unit_core_start, NULL);
  pthread_mutex_lock(&unit_core_start);

  pthread_mutex_init(&unit_core_done, NULL);
  pthread_mutex_lock(&unit_core_done);

  pthread_create( &test_mcthread, NULL, MCDecodeThread, NULL);
}

void MCUnitTestRelease(void) {
  keep_running=0;
  pthread_mutex_unlock(&unit_core_start);
  free(p_mcunit_out);
  HwCoreRelease(p_mccore);
  pthread_mutex_destroy(&unit_core_start);
  pthread_mutex_destroy(&unit_core_done);

}

void MCUnitStartHW(u32 * reg_base, u32 num_regs) {
  u32 i;

  /* Prepare unittest Core for decode. */
  for(i=0; i<num_regs; i++)
    p_mcbase[i] = reg_base[i];

  /* Alloc output buffer. */
  if(p_mcunit_out == NULL)
    p_mcunit_out = calloc(picSize(), sizeof(u8));
  /* Write in our own picture buffer. */
  p_mcbase[13] = (u32) p_mcunit_out;
  /* Unit test Core gets go-ahead. */
  pthread_mutex_unlock(&unit_core_start);
}

u32 MCUnitOutputError(u32 * output) {
  /* Wait until unittest Core is finished. */
  pthread_mutex_lock(&unit_core_done);
  return memcmp(p_mcunit_out, output, picSize());
}

static void* MCDecodeThread(void* param) {
  UNUSED(param); /* Suppress compiler warning. */
  while(keep_running) {
    /* Waiting for go-ahead from main thread. */
    pthread_mutex_lock(&unit_core_start);
    runAsic(p_mccore);
    pthread_mutex_unlock(&unit_core_done);
  }
  pthread_exit((void*)0);
}

static u32 picSize(void) {
  /* MB width * MB Height * 16 * 16 * 1.5 */
  u32 size = ((p_mcbase[4] >> 23)&0x1FF) * ((p_mcbase[4] >> 11)&0xFF) * 16* 24;
  assert(size != 0);
  return size;
}
