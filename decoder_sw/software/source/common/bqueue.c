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

#include "bqueue.h"
#include "dwl.h"
#ifndef HANTRO_OK
#define HANTRO_OK (0)
#endif /* HANTRO_TRUE */

#ifndef HANTRO_NOK
#define HANTRO_NOK (1)
#endif /* HANTRO_FALSE*/

#ifndef MAX_OUTPUT_BUFFERS
#define MAX_OUTPUT_BUFFERS 32
#endif

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    BqueueInit
        Initialize buffer queue
------------------------------------------------------------------------------*/
u32 BqueueInit(struct BufferQueue *bq, u32 num_buffers) {
  u32 i;

  if(DWLmemset(bq, 0, sizeof(*bq)) != bq)
    return HANTRO_NOK;

  if (num_buffers == 0) return HANTRO_OK;

#ifndef USE_EXTERNAL_BUFFER
  bq->pic_i = (u32*)DWLmalloc( sizeof(u32)*num_buffers);
#else
  bq->pic_i = (u32*)DWLmalloc( sizeof(u32)*MAX_OUTPUT_BUFFERS);
#endif
  if (bq->pic_i == NULL) {
    return HANTRO_NOK;
  }
#ifndef USE_EXTERNAL_BUFFER
  for( i = 0 ; i < num_buffers ; ++i ) {
    bq->pic_i[i] = 0;
  }
#else
  for( i = 0; i < MAX_OUTPUT_BUFFERS; ++i ) {
    bq->pic_i[i] = 0;
  }
#endif
  bq->queue_size = num_buffers;
  bq->ctr = 1;

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------
    BqueueRelease
------------------------------------------------------------------------------*/
void BqueueRelease(struct BufferQueue *bq) {
  if (bq->pic_i) {
    DWLfree(bq->pic_i);
    bq->pic_i = NULL;
  }
  bq->prev_anchor_slot = 0;
  bq->queue_size = 0;
}

/*------------------------------------------------------------------------------
    BqueueNext
        Return "oldest" available buffer.
------------------------------------------------------------------------------*/
u32 BqueueNext(struct BufferQueue *bq, u32 ref0, u32 ref1, u32 ref2,
               u32 b_pic) {
  u32 min_pic_i = 1 << 30;
  u32 next_out = (u32)0xFFFFFFFFU;
  u32 i;
  /* Find available buffer with smallest index number  */
  i = 0;

  while (i < bq->queue_size) {
    if (i == ref0 || i == ref1 || i == ref2) { /* Skip reserved anchor pictures */
      i++;
      continue;
    }
    if (bq->pic_i[i] < min_pic_i) {
      min_pic_i = bq->pic_i[i];
      next_out = i;
    }
    i++;
  }

  if (next_out == (u32)0xFFFFFFFFU) {
    return 0; /* No buffers available, shouldn't happen */
  }

  /* Update queue state */
  if (b_pic) {
    bq->pic_i[next_out] = bq->ctr - 1;
    bq->pic_i[bq->prev_anchor_slot]++;
  } else {
    bq->pic_i[next_out] = bq->ctr;
  }
  bq->ctr++;
  if (!b_pic) {
    bq->prev_anchor_slot = next_out;
  }

  return next_out;
}

/*------------------------------------------------------------------------------
    BqueueDiscard
        "Discard" output buffer, e.g. if error concealment used and buffer
        at issue is never going out.
------------------------------------------------------------------------------*/
void BqueueDiscard(struct BufferQueue *bq, u32 buffer) {
  bq->pic_i[buffer] = 0;
}

#ifdef USE_OUTPUT_RELEASE

/*------------------------------------------------------------------------------
    BqueueInit2
        Initialize buffer queue
------------------------------------------------------------------------------*/
u32 BqueueInit2( struct BufferQueue *bq, u32 num_buffers ) {
  u32 i;

  if(DWLmemset(bq, 0, sizeof(*bq)) != bq)
    return HANTRO_NOK;

  if( num_buffers == 0 )
    return HANTRO_OK;
#ifndef USE_EXTERNAL_BUFFER
  bq->pic_i = (u32*)DWLmalloc( sizeof(u32)*num_buffers);
#else
  bq->pic_i = (u32*)DWLmalloc( sizeof(u32)*MAX_OUTPUT_BUFFERS);
#endif
  if( bq->pic_i == NULL ) {
    return HANTRO_NOK;
  }
#ifndef USE_EXTERNAL_BUFFER
  for( i = 0 ; i < num_buffers ; ++i ) {
    bq->pic_i[i] = 0;
  }
#else
  for( i = 0; i < MAX_OUTPUT_BUFFERS; ++i ) {
    bq->pic_i[i] = 0;
  }
#endif
  bq->queue_size = num_buffers;
  bq->ctr = 1;
  bq->abort = 0;
  pthread_mutex_init(&bq->buf_release_mutex, NULL);
  pthread_cond_init(&bq->buf_release_cv, NULL);
#ifndef USE_EXTERNAL_BUFFER
  bq->buf_used = (u32*)DWLmalloc( sizeof(u32)*num_buffers);
#else
  bq->buf_used = (u32*)DWLmalloc( sizeof(u32)*MAX_OUTPUT_BUFFERS);
#endif
  if( bq->buf_used == NULL ) {
    return HANTRO_NOK;
  }
#ifndef USE_EXTERNAL_BUFFER
  for( i = 0; i < num_buffers; ++i ) {
    bq->buf_used[i] = 0;
  }
#else
  for( i = 0; i < MAX_OUTPUT_BUFFERS; ++i ) {
    bq->buf_used[i] = 0;
  }
#endif

  return HANTRO_OK;
}



/*------------------------------------------------------------------------------
    BqueueRelease2
------------------------------------------------------------------------------*/
void BqueueRelease2( struct BufferQueue *bq ) {
  if(bq->pic_i) {
    DWLfree(bq->pic_i);
    bq->pic_i = NULL;
  }
  bq->prev_anchor_slot  = 0;
  bq->queue_size       = 0;
  if(bq->buf_used) {
    DWLfree(bq->buf_used);
    bq->buf_used = NULL;
    pthread_mutex_destroy(&bq->buf_release_mutex);
    pthread_cond_destroy(&bq->buf_release_cv);
  }
}



/*------------------------------------------------------------------------------
    BqueuePictureRelease
        "Release" output buffer.
------------------------------------------------------------------------------*/
void BqueuePictureRelease( struct BufferQueue *bq, u32 buffer ) {
  pthread_mutex_lock(&bq->buf_release_mutex);
  bq->buf_used[buffer] = 0;
  pthread_cond_signal(&bq->buf_release_cv);
  pthread_mutex_unlock(&bq->buf_release_mutex);

}

/*------------------------------------------------------------------------------
    BqueueNext2
        Return "oldest" available buffer.
------------------------------------------------------------------------------*/
u32 BqueueNext2( struct BufferQueue *bq, u32 ref0, u32 ref1, u32 ref2, u32 b_pic) {
  u32 min_pic_i = 1<<30;
  u32 next_out = (u32)0xFFFFFFFFU;
  u32 i;
  /* Find available buffer with smallest index number  */
  i = 0;

  while( i < bq->queue_size ) {
    if(i == ref0 || i == ref1 || i == ref2) { /* Skip reserved anchor pictures */
      i++;
      continue;
    }

    pthread_mutex_lock(&bq->buf_release_mutex);
    if(!bq->buf_used[i] && !bq->abort) {
      next_out = i;
      pthread_mutex_unlock(&bq->buf_release_mutex);
      break;
    }
    pthread_mutex_unlock(&bq->buf_release_mutex);

    if( bq->pic_i[i] < min_pic_i ) {
      min_pic_i = bq->pic_i[i];
      next_out = i;
    }
    i++;
  }

  if( next_out == (u32)0xFFFFFFFFU) {
    return 0; /* No buffers available, shouldn't happen */
  }

  pthread_mutex_lock(&bq->buf_release_mutex);
#ifndef GET_FREE_BUFFER_NON_BLOCK
  while(bq->buf_used[next_out] && !bq->abort)
    pthread_cond_wait(&bq->buf_release_cv, &bq->buf_release_mutex);
#else
  if (bq->buf_used[next_out])
    next_out = (u32)0xFFFFFFFFU;
#endif

  if(bq->abort) {
    next_out = (u32)0xFFFFFFFFU;
  }
  pthread_mutex_unlock(&bq->buf_release_mutex);

  if(next_out == (u32)0xFFFFFFFFU) {
    return next_out;
  }

  /* Update queue state */
  if( b_pic ) {
    bq->pic_i[next_out] = bq->ctr-1;
    bq->pic_i[bq->prev_anchor_slot]++;
  } else {
    bq->pic_i[next_out] = bq->ctr;
  }
  bq->ctr++;
  if( !b_pic ) {
    bq->prev_anchor_slot = next_out;
  }

  return next_out;
}

u32 BqueueWaitNotInUse( struct BufferQueue *bq) {
  u32 i;
  for(i = 0; i < bq->queue_size; i++) {
    pthread_mutex_lock(&bq->buf_release_mutex);
    while(bq->buf_used[i] && !bq->abort)
      pthread_cond_wait(&bq->buf_release_cv, &bq->buf_release_mutex);
    pthread_mutex_unlock(&bq->buf_release_mutex);
  }

  if(bq->abort)
    return HANTRO_NOK;
  else
    return HANTRO_OK;
}

void BqueueMarkNotInUse( struct BufferQueue *bq) {
  u32 i;
  for(i = 0; i < bq->queue_size; i++) {
    pthread_mutex_lock(&bq->buf_release_mutex);
    bq->buf_used[i] = 0;
    pthread_mutex_unlock(&bq->buf_release_mutex);
  }
}


void BqueueSetBufferAsUsed(struct BufferQueue *bq, u32 buffer) {
  pthread_mutex_lock(&bq->buf_release_mutex);
  bq->buf_used[buffer] = 1;
  pthread_mutex_unlock(&bq->buf_release_mutex);
}

u32 BqueueWaitBufNotInUse(struct BufferQueue *bq, u32 buffer) {
  pthread_mutex_lock(&bq->buf_release_mutex);
  while(bq->buf_used[buffer] && !bq->abort)
    pthread_cond_wait(&bq->buf_release_cv, &bq->buf_release_mutex);
  pthread_mutex_unlock(&bq->buf_release_mutex);

  if(bq->abort)
    return HANTRO_NOK;
  else
    return HANTRO_OK;
}

void BqueueSetAbort(struct BufferQueue *bq) {
  pthread_mutex_lock(&bq->buf_release_mutex);
  bq->abort = 1;
  pthread_cond_signal(&bq->buf_release_cv);
  pthread_mutex_unlock(&bq->buf_release_mutex);
}

void BqueueClearAbort(struct BufferQueue *bq) {
  pthread_mutex_lock(&bq->buf_release_mutex);
  bq->abort = 0;
  pthread_mutex_unlock(&bq->buf_release_mutex);
}

void BqueueEmpty(struct BufferQueue *bq) {
  u32 i;
  if(bq->pic_i == NULL || bq->buf_used == NULL)
    return;

  pthread_mutex_lock(&bq->buf_release_mutex);
#ifndef USE_EXTERNAL_BUFFER
  for( i = 0 ; i < bq->queue_size ; ++i ) {
    bq->pic_i[i] = 0;
#ifdef USE_OMXIL_BUFFER
    bq->buf_used[i] = 0;
#endif
  }
#else
  for( i = 0; i < MAX_OUTPUT_BUFFERS; ++i ) {
    bq->pic_i[i] = 0;
#ifdef USE_OMXIL_BUFFER
    bq->buf_used[i] = 0;
#endif
  }
#endif

  bq->ctr = 1;
  bq->abort = 0;
  bq->prev_anchor_slot  = 0;
  pthread_mutex_unlock(&bq->buf_release_mutex);
}
#endif
