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

#include "vp8hwd_buffer_queue.h"

#include "dwlthread.h"

#include "fifo.h"

/* macro for assertion, used only when _ASSERT_USED is defined */
#ifdef _ASSERT_USED
#ifndef ASSERT
#include <assert.h>
#define ASSERT(expr) assert(expr)
#endif
#else
#define ASSERT(expr)
#endif

#ifdef BUFFER_QUEUE_PRINT_STATUS
#include <stdio.h>
#define PRINT_COUNTS(x) PrintCounts(x)
#else
#define PRINT_COUNTS(x)
#endif /* BUFFER_QUEUE_PRINT_STATUS */
#ifndef HANTRO_OK
#define HANTRO_OK 0
#endif

#ifndef HANTRO_NOK
#define HANTRO_NOK 1
#endif

/* Data structure containing the decoder reference frame status at the tail
 * of the queue. */
typedef struct DecoderRefStatus_ {
  i32 i_prev;    /* Index to the previous reference frame. */
  i32 i_alt;     /* Index to the alt reference frame. */
  i32 i_golden;  /* Index to the golden reference frame. */
} DecoderRefStatus;

/* Data structure to hold this picture buffer queue instance data. */
typedef struct BufferQueue_t_ {
  pthread_mutex_t cs;         /* Critical section to protect data. */
  pthread_cond_t pending_cv;/* Sync for DecreaseRefCount and WaitPending. */
  pthread_mutex_t pending;    /* Sync for DecreaseRefCount and WaitPending. */
  i32 n_buffers;         /* Number of buffers contained in total. */
  i32* n_references;     /* Reference counts on buffers. Index is buffer#.  */
#ifdef USE_OUTPUT_RELEASE
  u32* buf_used;         /* The buffer is used to output or not. Index is buffer#.  */
  pthread_mutex_t buf_release_mutex;
  pthread_cond_t buf_release_cv;
  u32 abort;
#endif
  DecoderRefStatus ref_status; /* Reference status of the decoder. */
  FifoInst empty_fifo;  /* Queue holding empty, unreferred buffer indices. */
} BufferQueue_t;

static void IncreaseRefCount(BufferQueue_t* q, i32 i);
static void DecreaseRefCount(BufferQueue_t* q, i32 i);
#ifdef BUFFER_QUEUE_PRINT_STATUS
static inline void PrintCounts(BufferQueue_t* q);
#endif

#ifdef USE_OUTPUT_RELEASE
static void ClearRefCount(BufferQueue_t* q, i32 i);
#endif

BufferQueue VP8HwdBufferQueueInitialize(i32 n_buffers) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(n_buffers=%i)", n_buffers);
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  ASSERT(n_buffers > 0);
  i32 i;
  BufferQueue_t* q = (BufferQueue_t*)DWLcalloc(1, sizeof(BufferQueue_t));

  if (q == NULL) {
    return NULL;
  }
#ifndef USE_EXTERNAL_BUFFER
  q->n_references = (i32*)DWLcalloc(n_buffers, sizeof(i32));
#else
  q->n_references = (i32*)DWLcalloc(VP8DEC_MAX_PIC_BUFFERS, sizeof(i32));
#endif

#ifdef USE_OUTPUT_RELEASE
#ifndef USE_EXTERNAL_BUFFER
  q->buf_used = (u32*)DWLcalloc(n_buffers, sizeof(i32));
#else
  q->buf_used = (u32*)DWLcalloc(VP8DEC_MAX_PIC_BUFFERS, sizeof(i32));
#endif
  pthread_mutex_init(&q->buf_release_mutex, NULL);
  pthread_cond_init(&q->buf_release_cv, NULL);
#endif

  if (q->n_references == NULL ||
#ifdef USE_OUTPUT_RELEASE
    q->buf_used == NULL ||
#endif

#ifndef USE_EXTERNAL_BUFFER
    FifoInit(n_buffers, &q->empty_fifo) != FIFO_OK ||
#else
    FifoInit(VP8DEC_MAX_PIC_BUFFERS, &q->empty_fifo) != FIFO_OK ||
#endif
    pthread_mutex_init(&q->cs, NULL) ||
    pthread_mutex_init(&q->pending, NULL) ||
    pthread_cond_init(&q->pending_cv, NULL)) {
    VP8HwdBufferQueueRelease(q);
    return NULL;
  }
  /* Add picture buffers among empty picture buffers. */
  for (i = 0; i < n_buffers; i++) {
    q->n_references[i] = 0;
#ifdef USE_OUTPUT_RELEASE
    q->buf_used[i] = 0;
#endif
    FifoPush(q->empty_fifo, (FifoObject)i, FIFO_EXCEPTION_DISABLE);
    q->n_buffers++;
  }
  q->ref_status.i_prev = q->ref_status.i_golden = q->ref_status.i_alt =
                           REFERENCE_NOT_SET;
  return q;
}

void VP8HwdBufferQueueRelease(BufferQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  ASSERT(queue);
  BufferQueue_t* q = (BufferQueue_t*)queue;
  if (q->empty_fifo) {
    /* Empty the fifo before releasing. */
#if !defined(USE_OUTPUT_RELEASE) && !defined(BUFFER_QUEUE_SAFE_RELEASE)
    i32 i;
    FifoObject j;
    for (i = 0; i < q->n_buffers; i++)
      FifoPop(q->empty_fifo, &j, FIFO_EXCEPTION_DISABLE);
#endif
    FifoRelease(q->empty_fifo);
  }
  pthread_mutex_destroy(&q->cs);
  pthread_cond_destroy(&q->pending_cv);
  pthread_mutex_destroy(&q->pending);
  DWLfree(q->n_references);
#ifdef USE_OUTPUT_RELEASE
  pthread_mutex_destroy(&q->buf_release_mutex);
  pthread_cond_destroy(&q->buf_release_cv);
  DWLfree(q->buf_used);
#endif
  DWLfree(q);
}

void VP8HwdBufferQueueUpdateRef(BufferQueue queue, u32 ref_flags, i32 buffer) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(ref_flags=0x%X, buffer=%i)", ref_flags, buffer);
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  ASSERT(queue);
  BufferQueue_t* q = (BufferQueue_t*)queue;
  ASSERT(buffer >= 0 && buffer < q->n_buffers);
  pthread_mutex_lock(&q->cs);
  /* Check for each type of reference frame update need. */
  if (ref_flags & BQUEUE_FLAG_PREV && buffer != q->ref_status.i_prev) {
    q->ref_status.i_prev = buffer;
  }
  if (ref_flags & BQUEUE_FLAG_GOLDEN && buffer != q->ref_status.i_golden) {
    q->ref_status.i_golden = buffer;
  }
  if (ref_flags & BQUEUE_FLAG_ALT && buffer != q->ref_status.i_alt) {
    q->ref_status.i_alt = buffer;
  }
  PRINT_COUNTS(q);
  pthread_mutex_unlock(&q->cs);
}

i32 VP8HwdBufferQueueGetPrevRef(BufferQueue queue) {
  BufferQueue_t* q = (BufferQueue_t*)queue;
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf(" # %d\n", q->ref_status.i_prev);
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  return q->ref_status.i_prev;
}

i32 VP8HwdBufferQueueGetGoldenRef(BufferQueue queue) {
  BufferQueue_t* q = (BufferQueue_t*)queue;
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf(" # %d\n", q->ref_status.i_golden);
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  return q->ref_status.i_golden;
}

i32 VP8HwdBufferQueueGetAltRef(BufferQueue queue) {
  BufferQueue_t* q = (BufferQueue_t*)queue;
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf(" # %d\n", q->ref_status.i_alt);
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  return q->ref_status.i_alt;
}

void VP8HwdBufferQueueAddRef(BufferQueue queue, i32 buffer) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(buffer=%i)", buffer);
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  BufferQueue_t* q = (BufferQueue_t*)queue;
  ASSERT(buffer >= 0 && buffer < q->n_buffers);
  pthread_mutex_lock(&q->cs);
  IncreaseRefCount(q, buffer);
  pthread_mutex_unlock(&q->cs);
}

void VP8HwdBufferQueueRemoveRef(BufferQueue queue,
                                i32 buffer) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(buffer=%i)", buffer);
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  BufferQueue_t* q = (BufferQueue_t*)queue;
  ASSERT(buffer >= 0 && buffer < q->n_buffers);
  pthread_mutex_lock(&q->cs);
  DecreaseRefCount(q, buffer);
  pthread_mutex_unlock(&q->cs);
}

i32 VP8HwdBufferQueueGetBuffer(BufferQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  i32 i;
  FifoObject j;
  enum FifoRet ret;
  BufferQueue_t* q = (BufferQueue_t*)queue;
#ifdef USE_OUTPUT_RELEASE
  u32 k;

#ifndef GET_FREE_BUFFER_NON_BLOCK
  while(!FifoCount(q->empty_fifo))
    sched_yield();
#else
  if (!FifoCount(q->empty_fifo))
    return ((i32)0xFFFFFFFF);
#endif

  u32 buf_num = FifoCount(q->empty_fifo);

  for(k = 0; k < buf_num; k++) {
    ret = FifoPop(q->empty_fifo, &j, FIFO_EXCEPTION_DISABLE);
    if(ret == FIFO_ABORT)
      return ((i32)0xFFFFFFFF);

    i = *(addr_t *)&j;
    pthread_mutex_lock(&q->buf_release_mutex);
    if(!q->buf_used[i]) {
      pthread_mutex_unlock(&q->buf_release_mutex);
      break;
    }
    pthread_mutex_unlock(&q->buf_release_mutex);
    FifoPush(q->empty_fifo, (FifoObject)i, FIFO_EXCEPTION_DISABLE);
  }

  if(k == buf_num) {
#ifdef GET_FREE_BUFFER_NON_BLOCK
    return ((i32)0xFFFFFFFF);
#else
    ret = FifoPop(q->empty_fifo, &j, FIFO_EXCEPTION_DISABLE);
    if(ret == FIFO_ABORT)
      return ((i32)0xFFFFFFFF);

    i = *(addr_t *)&j;
    pthread_mutex_lock(&q->buf_release_mutex);
    while(q->buf_used[i] && !q->abort)
      pthread_cond_wait(&q->buf_release_cv, &q->buf_release_mutex);
    pthread_mutex_unlock(&q->buf_release_mutex);
#endif

    if(q->abort)
      return ((i32)0xFFFFFFFF);
  }
#else
  FifoPop(q->empty_fifo, &j, FIFO_EXCEPTION_DISABLE);
  i = *(addr_t *)&j;
#endif
  pthread_mutex_lock(&q->cs);
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("# %i\n", i);
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  IncreaseRefCount(q, i);
  pthread_mutex_unlock(&q->cs);
  return i;
}

void VP8HwdBufferQueueWaitPending(BufferQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  ASSERT(queue);
  BufferQueue_t* q = (BufferQueue_t*)queue;

  pthread_mutex_lock(&q->pending);

  while (FifoCount(q->empty_fifo) != (u32)q->n_buffers)
    pthread_cond_wait(&q->pending_cv, &q->pending);

  pthread_mutex_unlock(&q->pending);

#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("#\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
}

static void IncreaseRefCount(BufferQueue_t* q, i32 i) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(buffer=%i)", i);
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  q->n_references[i]++;
  ASSERT(q->n_references[i] >= 0);   /* No negative references. */
  PRINT_COUNTS(q);
}

static void DecreaseRefCount(BufferQueue_t* q, i32 i) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(buffer=%i)", i);
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */

#ifdef USE_OUTPUT_RELEASE
  if(q->n_references[i] > 0)
    q->n_references[i]--;
  else
    return;
#else
  q->n_references[i]--;
#endif
  ASSERT(q->n_references[i] >= 0);   /* No negative references. */
  PRINT_COUNTS(q);
  if (q->n_references[i] == 0) {
    /* Once picture buffer is no longer referred to, it can be put to
       the empty fifo. */
#ifdef BUFFER_QUEUE_PRINT_STATUS
    printf("Buffer #%i put to empty pool\n", i);
    if(i == q->ref_status.i_prev || i == q->ref_status.i_golden || i == q->ref_status.i_alt) {
      printf("released but referenced %d\n", i);
    }
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
    FifoPush(q->empty_fifo, (FifoObject)i, FIFO_EXCEPTION_DISABLE);

    pthread_mutex_lock(&q->pending);
    if (FifoCount(q->empty_fifo) == (u32)q->n_buffers)
      pthread_cond_signal(&q->pending_cv);
    pthread_mutex_unlock(&q->pending);
  }
}

#ifdef BUFFER_QUEUE_PRINT_STATUS
static inline void PrintCounts(BufferQueue_t* q) {
  i32 i = 0;
  for (i = 0; i < q->n_buffers; i++)
    printf("%u", q->n_references[i]);
  printf(" |");
  printf(" P: %i |", q->ref_status.i_prev);
  printf(" G: %i |", q->ref_status.i_golden);
  printf(" A: %i |", q->ref_status.i_alt);
  printf("\n");
}
#endif /* BUFFER_QUEUE_PRINT_STATUS */

#ifdef USE_OUTPUT_RELEASE
void VP8HwdBufferQueueReleaseBuffer(BufferQueue queue, i32 i) {
  BufferQueue_t* q = (BufferQueue_t*)queue;

  if(q == NULL)
    return;

  pthread_mutex_lock(&q->buf_release_mutex);
  if(q->buf_used[i]) {
    q->buf_used[i] = 0;
    pthread_cond_signal(&q->buf_release_cv);
  }
  pthread_mutex_unlock(&q->buf_release_mutex);
}

void VP8HwdBufferQueueSetBufferAsUsed(BufferQueue queue, i32 i) {
  BufferQueue_t* q = (BufferQueue_t*)queue;

  if(q == NULL)
    return;

  pthread_mutex_lock(&q->buf_release_mutex);
  q->buf_used[i] = 1;
  pthread_mutex_unlock(&q->buf_release_mutex);
}

void VP8HwdBufferQueueWaitNotInUse(BufferQueue queue) {
  BufferQueue_t* q = (BufferQueue_t*)queue;
  i32 i;

  if(q == NULL)
    return;

  for(i = 0; i < q->n_buffers; i++) {
    pthread_mutex_lock(&q->buf_release_mutex);
    while(q->buf_used[i] && !q->abort)
      pthread_cond_wait(&q->buf_release_cv, &q->buf_release_mutex);
    pthread_mutex_unlock(&q->buf_release_mutex);
  }
}

void VP8HwdBufferQueueMarkNotInUse(BufferQueue queue) {
  BufferQueue_t* q = (BufferQueue_t*)queue;
  i32 i;

  if(q == NULL)
    return;

  for(i = 0; i < q->n_buffers; i++) {
    pthread_mutex_lock(&q->buf_release_mutex);
    q->buf_used[i] = 0;
    pthread_mutex_unlock(&q->buf_release_mutex);
  }
}


u32 VP8HwdBufferQueueWaitBufNotInUse(BufferQueue queue, i32 i) {
  BufferQueue_t* q = (BufferQueue_t*)queue;

  if(q == NULL)
    return -1;

  pthread_mutex_lock(&q->buf_release_mutex);
  while(q->buf_used[i] && !q->abort)
    pthread_cond_wait(&q->buf_release_cv, &q->buf_release_mutex);
  pthread_mutex_unlock(&q->buf_release_mutex);
  if(q->abort)
    return HANTRO_NOK;
  else
    return HANTRO_OK;
}
#endif

#ifdef USE_EXTERNAL_BUFFER
void VP8HwdBufferQueueAddBuffer(BufferQueue queue, u32 i) {
  BufferQueue_t* q = (BufferQueue_t*)queue;

  if(q == NULL)
    return;

  pthread_mutex_lock(&q->cs);
  q->n_references[i] = 0;
#ifdef USE_OUTPUT_RELEASE
  q->buf_used[i] = 0;
#endif
  FifoPush(q->empty_fifo, (FifoObject)i, FIFO_EXCEPTION_DISABLE);
  q->n_buffers++;
  pthread_mutex_unlock(&q->cs);
}
#endif

#ifdef USE_OUTPUT_RELEASE

static void ClearRefCount(BufferQueue_t* q, i32 i) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(buffer=%i)", i);
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */

  if(q->n_references[i] > 0)
    q->n_references[i] = 0;
  else
    return;

  ASSERT(q->n_references[i] >= 0);   /* No negative references. */
  PRINT_COUNTS(q);
  if (q->n_references[i] == 0) {
    /* Once picture buffer is no longer referred to, it can be put to
       the empty fifo. */
#ifdef BUFFER_QUEUE_PRINT_STATUS
    printf("Buffer #%i put to empty pool\n", i);
    if(i == q->ref_status.i_prev || i == q->ref_status.i_golden || i == q->ref_status.i_alt) {
      printf("released but referenced %d\n", i);
    }
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
    q->ref_status.i_prev = q->ref_status.i_golden = q->ref_status.i_alt =
                             REFERENCE_NOT_SET;

    FifoPush(q->empty_fifo, (FifoObject)i, FIFO_EXCEPTION_DISABLE);

    pthread_mutex_lock(&q->pending);
    if (FifoCount(q->empty_fifo) == (u32)q->n_buffers)
      pthread_cond_signal(&q->pending_cv);
    pthread_mutex_unlock(&q->pending);
  }
}


void VP8HwdBufferQueueSetAbort(BufferQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  BufferQueue_t* q = (BufferQueue_t*)queue;

  if(q == NULL)
    return;

  ASSERT(q->empty_fifo);
  FifoSetAbort(q->empty_fifo);

  pthread_mutex_lock(&q->buf_release_mutex);
  q->abort = 1;
  pthread_cond_signal(&q->buf_release_cv);
  pthread_mutex_unlock(&q->buf_release_mutex);
}


void VP8HwdBufferQueueClearAbort(BufferQueue queue) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("()");
  printf("\n");
#endif  /* BUFFER_QUEUE_PRINT_STATUS */
  BufferQueue_t* q = (BufferQueue_t*)queue;

  if(q == NULL)
    return;

  ASSERT(q->empty_fifo);
  FifoClearAbort(q->empty_fifo);

  pthread_mutex_lock(&q->buf_release_mutex);
  q->abort = 0;
  pthread_mutex_unlock(&q->buf_release_mutex);
}

void VP8HwdBufferQueueEmptyRef(BufferQueue queue, i32 buffer) {
#ifdef BUFFER_QUEUE_PRINT_STATUS
  printf(__FUNCTION__);
  printf("(buffer=%i)", buffer);
  printf("\n");
#endif /* BUFFER_QUEUE_PRINT_STATUS */
  BufferQueue_t* q = (BufferQueue_t*)queue;

  if(q == NULL)
    return;

  pthread_mutex_lock(&q->cs);
  ClearRefCount(q, buffer);
  pthread_mutex_unlock(&q->cs);
}

#endif
