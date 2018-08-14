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

#include <dwlthread.h>
#include "vp8hwd_buffer_queue.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "async_task.h"
#include "fifo.h"

#define BUFFER_COUNT 16

pthread_mutex_t crit_sec = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  u32 n_buffers;
  u32 done;
  BufferQueue q;
  fifo_inst fifo;
} test_params;

typedef struct {
  u32 prv;
  u32 gld;
  u32 alt;
} references;

void* test_task(void* params) {
  i32 buffer;
  struct timespec tv;
  test_params* p = (test_params*)params;
  do {
    FifoPop(p->fifo, &buffer, FIFO_EXCEPTION_DISABLE);
    tv.tv_sec = 0;
    tv.tv_nsec = rand() % 1000000; /* Random delay to simulate rendering. */
    nanosleep(&tv, NULL);
    VP8HwdBufferQueueReleaseRef(p->q, buffer);
    pthread_mutex_lock(&crit_sec);
    p->n_buffers--;
    if (p->done && p->n_buffers == 0) {
      pthread_mutex_unlock(&crit_sec);
      break;
    }
    pthread_mutex_unlock(&crit_sec);
  } while(1);
  return NULL;
}

int rand_bool() {
  return rand() % 2;
}

references rand_references() {
  references refer;
  refer.prv = rand_bool() ? 1 : 0;
  refer.gld = rand_bool() ? 1 : 0;
  refer.alt = rand_bool() ? 1 : 0;
  return refer;
}

int main(int argc, char* argv[]) {
  test_params p;
  u32 buffer_count = rand() % 1000;
  p.n_buffers = 0;
  p.done = 0;
  p.q = VP8HwdBufferQueueInitialize(BUFFER_COUNT);
  if (!p.q)
    return -1;
  if (FifoInit(1, &p.fifo) != FIFO_OK)
    return -1;
  async_task task = run_task(test_task, &p);
  do {
    /* Simulated decode thread. */
    struct timespec tv;
    i32 buffer;
    i32 prev_ref;
    i32 golden_ref;
    i32 alt_ref;
    i32 update_flags;
    i32 intra_frame;
    references refer;
    references update;
    memset(&refer, 0, sizeof(refer));
    memset(&update, 0, sizeof(update));
    intra_frame = !(rand() % 90);
    if (!intra_frame) {
      refer = rand_references();
      update = rand_references();
    }
    buffer = VP8HwdBufferQueueGetBuffer(p.q);
    if (refer.prv) {
      prev_ref = VP8HwdBufferQueueGetPrevRef(p.q);
      if (prev_ref != REFERENCE_NOT_SET)
        VP8HwdBufferQueueAddRef(p.q, prev_ref);
    }
    if (refer.gld) {
      golden_ref = VP8HwdBufferQueueGetGoldenRef(p.q);
      if (golden_ref != REFERENCE_NOT_SET)
        VP8HwdBufferQueueAddRef(p.q, golden_ref);
    }
    if (refer.alt) {
      alt_ref = VP8HwdBufferQueueGetAltRef(p.q);
      if (alt_ref != REFERENCE_NOT_SET)
        VP8HwdBufferQueueAddRef(p.q, alt_ref);
    }
    update_flags = 0;
    if (update.prv)
      update_flags |= BQUEUE_FLAG_PREV;
    if (update.gld)
      update_flags |= BQUEUE_FLAG_GOLDEN;
    if (update.alt)
      update_flags |= BQUEUE_FLAG_ALT;
    if (update_flags)
      VP8HwdBufferQueueUpdateRef(p.q, update_flags, buffer);
    /* Sleeping time simulates decode time. */
    tv.tv_sec = 0;
    tv.tv_nsec = rand() % 1000000; /* Random delay up to 1ms. */
    nanosleep(&tv, NULL);
    /* Now update the references accordingly */
    if (refer.prv) {
      if (prev_ref != REFERENCE_NOT_SET)
        VP8HwdBufferQueueReleaseRef(p.q, prev_ref);
    }
    if (refer.gld) {
      if (golden_ref != REFERENCE_NOT_SET)
        VP8HwdBufferQueueReleaseRef(p.q, golden_ref);
    }
    if (refer.alt) {
      if (alt_ref != REFERENCE_NOT_SET)
        VP8HwdBufferQueueReleaseRef(p.q, alt_ref);
    }
    FifoPush(p.fifo, buffer, FIFO_EXCEPTION_DISABLE);
    pthread_mutex_lock(&crit_sec);
    p.n_buffers++;
    pthread_mutex_unlock(&crit_sec);
  } while (--buffer_count);
  pthread_mutex_lock(&crit_sec);
  p.done = 1;
  pthread_mutex_unlock(&crit_sec);
  wait_for_task_completion(task);
  FifoRelease(p.fifo);
  VP8HwdBufferQueueRelease(p.q);
  return 0;
}

