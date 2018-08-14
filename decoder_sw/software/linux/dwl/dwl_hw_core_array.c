/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
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

#include "asic.h"
#include "dwl.h"
#include "dwl_hw_core_array.h"

#include <assert.h>
#include <semaphore.h>
#include <stdlib.h>

#ifdef CORES
#if CORES > MAX_ASIC_CORES
#error Too many cores! Check max number of cores in <decapicommon.h>
#else
#define HW_CORE_COUNT CORES
#endif
#else
#define HW_CORE_COUNT 1
#endif

struct HwCoreContainer {
  Core core;
};

struct HwCoreArrayInstance {
  u32 num_of_cores;
  struct HwCoreContainer* cores;
  sem_t core_lock;
  sem_t core_rdy;
};

HwCoreArray InitializeCoreArray() {
  u32 i;
  struct HwCoreArrayInstance* array =
    malloc(sizeof(struct HwCoreArrayInstance));
  array->num_of_cores = GetCoreCount();
  sem_init(&array->core_lock, 0, array->num_of_cores);

  sem_init(&array->core_rdy, 0, 0);

  array->cores = calloc(array->num_of_cores, sizeof(struct HwCoreContainer));
  assert(array->cores);
  for (i = 0; i < array->num_of_cores; i++) {
    array->cores[i].core = HwCoreInit();
    assert(array->cores[i].core);

    HwCoreSetid(array->cores[i].core, i);
    HwCoreSetHwRdySem(array->cores[i].core, &array->core_rdy);
  }
  return array;
}

void ReleaseCoreArray(HwCoreArray inst) {
  u32 i;

  struct HwCoreArrayInstance* array = (struct HwCoreArrayInstance*)inst;

  /* TODO(vmr): Wait for all cores to Finish. */
  for (i = 0; i < array->num_of_cores; i++) {
    HwCoreRelease(array->cores[i].core);
  }

  free(array->cores);
  sem_destroy(&array->core_lock);
  sem_destroy(&array->core_rdy);
  free(array);
}

Core BorrowHwCore(HwCoreArray inst) {
  u32 i = 0;
  struct HwCoreArrayInstance* array = (struct HwCoreArrayInstance*)inst;

  sem_wait(&array->core_lock);

  while (!HwCoreTryLock(array->cores[i].core)) {
    i++;
  }

  return array->cores[i].core;
}

void ReturnHwCore(HwCoreArray inst, Core Core) {
  struct HwCoreArrayInstance* array = (struct HwCoreArrayInstance*)inst;

  HwCoreUnlock(Core);

  sem_post(&array->core_lock);
}

u32 GetCoreCount() {
  /* TODO(vmr): implement dynamic mechanism for calculating. */
  return HW_CORE_COUNT;
}

Core GetCoreById(HwCoreArray inst, int nth) {
  struct HwCoreArrayInstance* array = (struct HwCoreArrayInstance*)inst;

  assert(nth < (int)GetCoreCount());

  return array->cores[nth].core;
}

int WaitAnyCoreRdy(HwCoreArray inst) {
  struct HwCoreArrayInstance* array = (struct HwCoreArrayInstance*)inst;

  return sem_wait(&array->core_rdy);
}

int StopCoreArray(HwCoreArray inst) {
  struct HwCoreArrayInstance* array = (struct HwCoreArrayInstance*)inst;
  return sem_post(&array->core_rdy);
}
