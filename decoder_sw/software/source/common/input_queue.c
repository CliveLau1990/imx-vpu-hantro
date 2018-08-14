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

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_queue.h"
#include "fifo.h"
//#define BUFFER_QUEUE_PRINT_STATUS
#ifdef BUFFER_QUEUE_PRINT_STATUS
#define PRINT_COUNTS(x) PrintCounts(x)
#else
#define PRINT_COUNTS(x)
#endif /* BUFFER_QUEUE_PRINT_STATUS */


/* Data structure to hold this picture buffer queue instance data. */
struct IQueue {
  pthread_mutex_t cs; /* Critical section to protect data. */
  i32 max_buffers;    /* Number of max buffers accepted. */
  i32 n_buffers;      /* Number of buffers contained in total. */
  struct DWLLinearMem buffers[MAX_PIC_BUFFERS];
  FifoInst fifo_in; /* Queue holding empty, unused input buffer indices. */
  i32 buffer_in_fifo[MAX_PIC_BUFFERS]; /* indicate buffer is in queue or not */
  u32 buffer_used[MAX_PIC_BUFFERS];
  pthread_mutex_t buf_release_mutex;
  pthread_cond_t buf_release_cv;
};

InputQueue InputQueueInit(i32 n_buffers) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(n_buffers=%i)", n_buffers);
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(n_buffers >= 0);
  struct IQueue* q = (struct IQueue*)calloc(1, sizeof(struct IQueue));
  if (q == NULL) {
    return NULL;
  }

  q->max_buffers = MAX_PIC_BUFFERS;
  q->n_buffers = 0;
  memset(q->buffers, 0, sizeof(q->buffers));
  memset(q->buffer_in_fifo, 0, sizeof(q->buffer_in_fifo));
  memset(q->buffer_used, 0, sizeof(q->buffer_used));
  pthread_mutex_init(&q->buf_release_mutex, NULL);
  pthread_cond_init(&q->buf_release_cv, NULL);

  if (FifoInit(MAX_PIC_BUFFERS, &q->fifo_in) != FIFO_OK ||
      pthread_mutex_init(&q->cs, NULL)) {
    InputQueueRelease(q);
    return NULL;
  }
  /* Add picture buffers among empty picture buffers. */
  /*
  for (i = 0; i < n_buffers; i++) {
    j = (FifoObject)(addr_t)i;
    ret = FifoPush(q->fifo_in, j, FIFO_EXCEPTION_ENABLE);
    if (ret != FIFO_OK) {
      InputQueueRelease(q);
      return NULL;
    }
    q->n_buffers++;
  }
  */
  (void)n_buffers;
  return q;
}
#ifdef USE_EXTERNAL_BUFFER
void InputQueueReset(InputQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  struct IQueue* q = (struct IQueue*)queue;
  i32 i;
  FifoObject j;
  enum FifoRet ret;
  if (q->fifo_in) {/* Empty the fifo before releasing. */
    FifoRelease(q->fifo_in);
    pthread_mutex_destroy(&q->cs);
    pthread_mutex_destroy(&q->buf_release_mutex);
    pthread_cond_destroy(&q->buf_release_cv);
  }
  FifoInit(MAX_PIC_BUFFERS, &q->fifo_in);
  assert(q->fifo_in);

#ifdef USE_OMXIL_BUFFER
  q->max_buffers = MAX_PIC_BUFFERS;
  q->n_buffers = 0;
  memset(q->buffers, 0, sizeof(q->buffers));
  memset(q->buffer_in_fifo, 0, sizeof(q->buffer_in_fifo));
#else
  for (i = 0; i < q->n_buffers; i++) {
    if (q->buffer_in_fifo[i]) {
      /* Push the buffers that are in fifo back. */
      j = (FifoObject)(addr_t)(q->buffers+i);

      ret = FifoPush(q->fifo_in, j, FIFO_EXCEPTION_ENABLE);
      assert(ret == FIFO_OK);
    }
  }
#endif
  pthread_mutex_init(&q->cs, NULL);
  pthread_mutex_init(&q->buf_release_mutex, NULL);
  pthread_cond_init(&q->buf_release_cv, NULL);
  (void)ret;
  (void)i;
  (void)j;
}
#endif

void InputQueueRelease(InputQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  struct IQueue* q = (struct IQueue*)queue;
  if (q->fifo_in) {/* Empty the fifo before releasing. */
#if !defined(USE_EXTERNAL_BUFFER) || defined(HEVC_EXT_BUF_SAFE_RELEASE)
    i32 i;
    FifoObject j;
    enum FifoRet ret;
    struct DWLLinearMem *buffer = NULL;
    for (i = 0; i < q->n_buffers; i++) {
      if (!q->buffer_in_fifo[i] && !q->buffer_used[i]) {
        q->buffer_in_fifo[i] = 1;
        buffer = &q->buffers[i];
        j = (FifoObject)(addr_t)buffer;
        ret = FifoPush(q->fifo_in, j, FIFO_EXCEPTION_ENABLE);
        assert(ret == FIFO_OK);
      }
      ret = FifoPop(q->fifo_in, &j, FIFO_EXCEPTION_DISABLE);
      assert(ret == FIFO_OK || ret == FIFO_ABORT);
      (void)ret;
    }
#endif
    FifoRelease(q->fifo_in);
  }
  pthread_mutex_destroy(&q->buf_release_mutex);
  pthread_cond_destroy(&q->buf_release_cv);
  pthread_mutex_destroy(&q->cs);
  free(q);
}

struct DWLLinearMem *InputQueueGetBuffer(InputQueue queue, u32 wait) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  struct DWLLinearMem *buffer;
  FifoObject j;
  i32 i;
  enum FifoRet ret;
  struct IQueue* q = (struct IQueue*)queue;
  assert(q->fifo_in);
  ret = FifoPop(q->fifo_in, &j, FIFO_EXCEPTION_ENABLE);
  if (ret == FIFO_EMPTY) {
    if (!wait) {
      return NULL;
    } else {/* wait for free one */
      ret = FifoPop(q->fifo_in, &j, FIFO_EXCEPTION_DISABLE);
      if(ret == FIFO_ABORT)
        return NULL;
    }
  } else if (ret == FIFO_ABORT)
    return NULL;
  assert(ret == FIFO_OK);
  buffer = (struct DWLLinearMem *)((addr_t )j);

  for (i = 0; i < q->n_buffers; i++) {
    if (q->buffers[i].virtual_address == buffer->virtual_address) {
      break;
    }
  }
  pthread_mutex_lock(&q->buf_release_mutex);
  while(q->buffer_used[i])
    pthread_cond_wait(&q->buf_release_cv, &q->buf_release_mutex);
  pthread_mutex_unlock(&q->buf_release_mutex);
  pthread_mutex_lock(&q->cs);
  q->buffer_in_fifo[i] = 0;
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("# %i\n", i);
#endif /* BUFFER_QUEUE_PRINT_STATUS */

  pthread_mutex_unlock(&q->cs);
  return buffer;
}

void InputQueueAddBuffer(InputQueue queue, struct DWLLinearMem *buffer) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  enum FifoRet ret;
  struct IQueue* q = (struct IQueue*)queue;
  FifoObject j;
  pthread_mutex_lock(&q->cs);

  q->buffers[q->n_buffers] = *buffer;
  /* Add one picture buffer among empty picture buffers. */
  j = (FifoObject)(addr_t)(q->buffers+q->n_buffers);

  ret = FifoPush(q->fifo_in, j, FIFO_EXCEPTION_ENABLE);
  assert(ret == FIFO_OK);
  (void)ret;
  q->buffer_in_fifo[q->n_buffers] = 1;
  q->n_buffers++;
  pthread_mutex_unlock(&q->cs);

#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
}

void InputQueueWaitPending(InputQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  struct IQueue* q = (struct IQueue*)queue;

  while (FifoCount(q->fifo_in) != (u32)q->n_buffers) sched_yield();

#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
}

struct DWLLinearMem *InputQueueReturnBuffer(InputQueue queue, const u32 *addr) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  enum FifoRet ret;
  struct IQueue* q = (struct IQueue*)queue;
  FifoObject j;
  i32 i;
  struct DWLLinearMem *buffer = NULL;

  for (i = 0; i < q->n_buffers; i++) {
    if (q->buffers[i].virtual_address == addr) {
      buffer = &q->buffers[i];
      break;
    }
  }
  if (buffer == NULL) {
    return NULL;
  }

  /* Add one picture buffer among empty picture buffers. */
  j = (FifoObject)(addr_t)buffer;
  if (q->buffer_in_fifo[i] == 0) {
    q->buffer_in_fifo[i] = 1;
    ret = FifoPush(q->fifo_in, j, FIFO_EXCEPTION_ENABLE);
    assert(ret == FIFO_OK);
    (void)ret;
  }
  pthread_mutex_lock(&q->buf_release_mutex);
  q->buffer_used[i] = 0;
  pthread_cond_signal(&q->buf_release_cv);
  pthread_mutex_unlock(&q->buf_release_mutex);

#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */

  return buffer;
}

#ifdef USE_EXTERNAL_BUFFER
void InputQueueSetAbort(InputQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  struct IQueue* q = (struct IQueue*)queue;
  assert(q->fifo_in);
  FifoSetAbort(q->fifo_in);
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
}

void InputQueueClearAbort(InputQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  struct IQueue* q = (struct IQueue*)queue;
  assert(q->fifo_in);
  FifoClearAbort(q->fifo_in);
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
}

void InputQueueReturnAllBuffer(InputQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  enum FifoRet ret;
  struct IQueue* q = (struct IQueue*)queue;
  FifoObject j;
  i32 i;
  struct DWLLinearMem *buffer = NULL;

  for (i = 0; i < q->n_buffers; i++) {
    buffer = &q->buffers[i];

    /* Add one picture buffer among empty picture buffers. */
    j = (FifoObject)(addr_t)buffer;
    if(q->buffer_in_fifo[i] == 0) {
      q->buffer_in_fifo[i] = 1;
      ret = FifoPush(q->fifo_in, j, FIFO_EXCEPTION_ENABLE);
      assert(ret == FIFO_OK);
      (void)ret;
    }
    pthread_mutex_lock(&q->buf_release_mutex);
    q->buffer_used[i] = 0;
    pthread_cond_signal(&q->buf_release_cv);
    pthread_mutex_unlock(&q->buf_release_mutex);
  }

#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
}
#endif

void InputQueueWaitNotUsed(InputQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  i32 i;
  struct IQueue* q = (struct IQueue*)queue;
  for(i = 0; i < q->n_buffers; i++) {
    pthread_mutex_lock(&q->buf_release_mutex);
    while (q->buffer_used[i])
      pthread_cond_wait(&q->buf_release_cv, &q->buf_release_mutex);
    pthread_mutex_unlock(&q->buf_release_mutex);
  }
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif
}

void InputQueueWaitBufNotUsed(InputQueue queue, const u32 *addr) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  i32 i;
  struct DWLLinearMem *buffer = NULL;
  struct IQueue* q = (struct IQueue*)queue;
  for (i = 0; i < q->n_buffers; i++) {
    if (q->buffers[i].virtual_address == addr) {
      buffer = &q->buffers[i];
      break;
    }
  }
  if (buffer == NULL) {
    return;
  }
  pthread_mutex_lock(&q->buf_release_mutex);
  while (q->buffer_used[i])
    pthread_cond_wait(&q->buf_release_cv, &q->buf_release_mutex);
  pthread_mutex_unlock(&q->buf_release_mutex);
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
}

void InputQueueSetBufAsUsed(InputQueue queue, const u32 *addr) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  assert(queue);
  i32 i;
  struct DWLLinearMem *buffer = NULL;
  struct IQueue* q = (struct IQueue*)queue;
  for (i = 0; i < q->n_buffers; i++) {
    if (q->buffers[i].virtual_address == addr) {
      buffer = &q->buffers[i];
      break;
    }
  }
  if (buffer == NULL) {
    return;
  }
  pthread_mutex_lock(&q->buf_release_mutex);
  q->buffer_used[i] = 1;
  pthread_mutex_unlock(&q->buf_release_mutex);
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
}
