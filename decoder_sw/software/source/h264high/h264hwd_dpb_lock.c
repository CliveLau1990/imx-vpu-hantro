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

#include "h264decapi.h"
#include "h264hwd_dpb_lock.h"
#include "h264hwd_dpb.h"
#include "h264hwd_storage.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#define FB_UNALLOCATED      0x00U
#define FB_FREE             0x01U
#define FB_ALLOCATED        0x02U
#define FB_OUTPUT           0x04U
#define FB_TEMP_OUTPUT      0x08U

#define FB_HW_ONGOING       0x30U


#ifdef DPB_LOCK_TRACE
#define DPB_TRACE(fmt, ...) fprintf(stderr, "%s(): " fmt,\
                                    __func__, ## __VA_ARGS__)
#else
#define DPB_TRACE(...) do {} while(0)
#endif

u32 InitList(FrameBufferList *fb_list) {
  (void) DWLmemset(fb_list, 0, sizeof(*fb_list));

  sem_init(&fb_list->out_count_sem, 0, 0);
  pthread_mutex_init(&fb_list->out_count_mutex, NULL);
  /* CV to be signaled when output  queue is empty */
  pthread_cond_init(&fb_list->out_empty_cv, NULL );

  pthread_mutex_init(&fb_list->ref_count_mutex, NULL );
  /* CV to be signaled when a buffer is not referenced anymore */
  pthread_cond_init(&fb_list->ref_count_cv, NULL );

  /* this CV is used to signal the HW has finished processing a picture
   * that is needed for output ( FB_OUTPUT | FB_HW_ONGOING )
   */
  pthread_cond_init(&fb_list->hw_rdy_cv, NULL);

  fb_list->b_initialized = 1;

  return 0;
}

void ReleaseList(FrameBufferList *fb_list) {
  int i;
  if (!fb_list->b_initialized)
    return;

  for(i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
    /* we shall clean our stuff graciously */
    assert(fb_list->fb_stat[i].n_ref_count == 0);
  }

  assert(fb_list->free_buffers == 0);

  fb_list->b_initialized = 0;

  pthread_mutex_destroy(&fb_list->ref_count_mutex);
  pthread_cond_destroy(&fb_list->ref_count_cv);

  pthread_mutex_destroy(&fb_list->out_count_mutex);
  pthread_cond_destroy(&fb_list->out_empty_cv);
  pthread_cond_destroy(&fb_list->hw_rdy_cv);

  sem_destroy(&fb_list->out_count_sem);
}

u32 AllocateIdUsed(FrameBufferList *fb_list, const void * data) {
  u32 id = 0;

  /* find first unallocated ID */
  do {
    if (fb_list->fb_stat[id].b_used == FB_UNALLOCATED)
      break;
    id++;
  } while (id < MAX_FRAME_BUFFER_NUMBER);

  if (id >= MAX_FRAME_BUFFER_NUMBER)
    return FB_NOT_VALID_ID;

  fb_list->fb_stat[id].b_used = FB_ALLOCATED;
  fb_list->fb_stat[id].n_ref_count = 0;
  fb_list->fb_stat[id].data = data;

  return id;
}

u32 AllocateIdFree(FrameBufferList *fb_list, const void * data) {
  u32 id = 0;

  /* find first unallocated ID */
  do {
    if (fb_list->fb_stat[id].b_used == FB_UNALLOCATED)
      break;
    id++;
  } while (id < MAX_FRAME_BUFFER_NUMBER);

  if (id >= MAX_FRAME_BUFFER_NUMBER)
    return FB_NOT_VALID_ID;

  fb_list->free_buffers++;

  fb_list->fb_stat[id].b_used = FB_FREE;
  fb_list->fb_stat[id].n_ref_count = 0;
  fb_list->fb_stat[id].data = data;
  return id;
}

void ReleaseId(FrameBufferList *fb_list, u32 id) {
  assert(id < MAX_FRAME_BUFFER_NUMBER);

  /* it is "bad" to release referenced or unallocated buffers */
  assert(fb_list->fb_stat[id].n_ref_count == 0);
#ifndef USE_OUTPUT_RELEASE
  assert(fb_list->fb_stat[id].b_used != FB_UNALLOCATED);
#else
  if(fb_list->fb_stat[id].b_used == FB_UNALLOCATED)
    return;
#endif

  if (id >= MAX_FRAME_BUFFER_NUMBER)
    return;

  if(fb_list->fb_stat[id].b_used == FB_FREE) {
    assert(fb_list->free_buffers > 0);
    fb_list->free_buffers--;
  }

  fb_list->fb_stat[id].b_used = FB_UNALLOCATED;
  fb_list->fb_stat[id].n_ref_count = 0;
  fb_list->fb_stat[id].data = NULL;
}

void * GetDataById(FrameBufferList *fb_list, u32 id) {
  assert(id < MAX_FRAME_BUFFER_NUMBER);
  assert(fb_list->fb_stat[id].b_used != FB_UNALLOCATED);

  return (void*) fb_list->fb_stat[id].data;
}

u32 GetIdByData(FrameBufferList *fb_list, const void *data) {
  u32 id = 0;
  assert(data);

  do {
    if (fb_list->fb_stat[id].data == data)
      break;
    id++;
  } while (id < MAX_FRAME_BUFFER_NUMBER);

  return id < MAX_FRAME_BUFFER_NUMBER ? id : FB_NOT_VALID_ID;
}
void IncrementRefUsage(FrameBufferList *fb_list, u32 id) {
  pthread_mutex_lock(&fb_list->ref_count_mutex);
  fb_list->fb_stat[id].n_ref_count++;
  DPB_TRACE("id = %d rc = %d\n", id, fb_list->fb_stat[id].n_ref_count);
  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

void DecrementRefUsage(FrameBufferList *fb_list, u32 id) {
  FrameBufferStatus *bs = fb_list->fb_stat + id;

  pthread_mutex_lock(&fb_list->ref_count_mutex);
  assert(bs->n_ref_count > 0);
  bs->n_ref_count--;

  if (bs->n_ref_count == 0) {
    if (bs->b_used == FB_FREE) {
      fb_list->free_buffers++;
      DPB_TRACE("FREE id = %d\n", id);
    }
    /* signal that this buffer is not referenced anymore */
    pthread_cond_signal(&fb_list->ref_count_cv);
  } else if (bs->b_used == FB_FREE) {
    DPB_TRACE("Free buffer id = %d still referenced\n", id);
  }

  DPB_TRACE("id = %d rc = %d\n", id, fb_list->fb_stat[id].n_ref_count);
  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

void MarkHWOutput(FrameBufferList *fb_list, u32 id, u32 type) {

  pthread_mutex_lock(&fb_list->ref_count_mutex);

  assert( fb_list->fb_stat[id].b_used & FB_ALLOCATED );
  assert( fb_list->fb_stat[id].b_used ^ type );

  fb_list->fb_stat[id].n_ref_count++;
  fb_list->fb_stat[id].b_used |= type;

  DPB_TRACE("id = %d rc = %d\n", id, fb_list->fb_stat[id].n_ref_count);

  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

void ClearHWOutput(FrameBufferList *fb_list, u32 id, u32 type) {
  FrameBufferStatus *bs = fb_list->fb_stat + id;

  pthread_mutex_lock(&fb_list->ref_count_mutex);

  assert(bs->b_used & (FB_HW_ONGOING | FB_ALLOCATED));

  bs->n_ref_count--;
  bs->b_used &= ~type;

  if (bs->n_ref_count == 0) {
    if (bs->b_used == FB_FREE) {
      fb_list->free_buffers++;
      DPB_TRACE("FREE id = %d\n", id);
    }
    /* signal that this buffer is not referenced anymore */
    pthread_cond_signal(&fb_list->ref_count_cv);
  }

  if((bs->b_used & FB_HW_ONGOING) == 0 &&  (bs->b_used & FB_OUTPUT))
    /* signal that this buffer is done by HW */
    pthread_cond_signal(&fb_list->hw_rdy_cv);

  DPB_TRACE("id = %d rc = %d\n", id, fb_list->fb_stat[id].n_ref_count);

  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

/* Mark a buffer as a potential (temporal) output. Output has to be marked
 * permanent by FinalizeOutputAll or reverted to non-output by
 * RemoveTempOutputAll.
 */
void MarkTempOutput(FrameBufferList *fb_list, u32 id) {
  DPB_TRACE(" id = %d\n", id);
  pthread_mutex_lock(&fb_list->ref_count_mutex);

  assert( fb_list->fb_stat[id].b_used & FB_ALLOCATED);

  fb_list->fb_stat[id].n_ref_count++;
  fb_list->fb_stat[id].b_used |= FB_TEMP_OUTPUT;

  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

/* Mark all temp output as valid output */
void FinalizeOutputAll(FrameBufferList *fb_list) {
  i32 i;
  pthread_mutex_lock(&fb_list->ref_count_mutex);

  for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
    if (fb_list->fb_stat[i].b_used & FB_TEMP_OUTPUT) {
      /* mark permanent output */
      fb_list->fb_stat[i].b_used |= FB_OUTPUT;
      /* clean TEMP flag from output */
      fb_list->fb_stat[i].b_used &= ~FB_TEMP_OUTPUT;

      DPB_TRACE("id = %d\n", i);
    }
  }

  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

void ClearOutput(FrameBufferList *fb_list, u32 id) {
  FrameBufferStatus *bs = fb_list->fb_stat + id;

  pthread_mutex_lock(&fb_list->ref_count_mutex);

  assert(bs->b_used & (FB_OUTPUT | FB_TEMP_OUTPUT));
#ifndef USE_OUTPUT_RELEASE
  assert(bs->n_ref_count > 0);
  bs->n_ref_count--;
#else
  if(bs->n_ref_count > 0)
    bs->n_ref_count--;
#endif

  bs->b_used &= ~(FB_OUTPUT | FB_TEMP_OUTPUT);

  if (bs->n_ref_count == 0) {
    if (bs->b_used == FB_FREE) {
      fb_list->free_buffers++;
      DPB_TRACE("FREE id = %d\n", id);
    }
    /* signal that this buffer is not referenced anymore */
    pthread_cond_signal(&fb_list->ref_count_cv);
  } else if(bs->b_used == FB_FREE) {
    DPB_TRACE("Free buffer id = %d still referenced\n", id);
  }

  DPB_TRACE("id = %d rc = %d\n", id, fb_list->fb_stat[id].n_ref_count);
  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

u32 PopFreeBuffer(FrameBufferList *fb_list) {
  u32 i = 0;
  FrameBufferStatus *bs = fb_list->fb_stat;
  do {
    if (bs->b_used == FB_FREE && bs->n_ref_count == 0) {
      bs->b_used = FB_ALLOCATED;
      break;
    }
    bs++;
    i++;
  } while (i < MAX_FRAME_BUFFER_NUMBER);

  assert(i < MAX_FRAME_BUFFER_NUMBER);

  fb_list->free_buffers--;

  DPB_TRACE("id = %d\n", i);

  return i;
}

void PushFreeBuffer(FrameBufferList *fb_list, u32 id) {
  assert(id < MAX_FRAME_BUFFER_NUMBER);
  assert(fb_list->fb_stat[id].b_used & FB_ALLOCATED);

  pthread_mutex_lock(&fb_list->ref_count_mutex);

  DPB_TRACE("id = %d\n", id);

  fb_list->fb_stat[id].b_used &= ~FB_ALLOCATED;
  fb_list->fb_stat[id].b_used |= FB_FREE;

  if (fb_list->fb_stat[id].n_ref_count == 0) {
    fb_list->free_buffers++;
    DPB_TRACE("FREE id = %d\n", id);

    /* signal that this buffer is not referenced anymore */
    pthread_cond_signal(&fb_list->ref_count_cv);
  } else
    DPB_TRACE("Free buffer id = %d still referenced\n", id);

  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

u32 GetFreePicBuffer(FrameBufferList *fb_list, u32* old_id, u32* is_free) {
  u32 id;
  u32 i = MAX_FRAME_BUFFER_NUMBER;

  pthread_mutex_lock(&fb_list->ref_count_mutex);

#ifndef GET_FREE_BUFFER_NON_BLOCK
  /* Wait until a free buffer is available or a buffer in "old_id" list
   * is not referenced anymore */
  while (fb_list->free_buffers == 0 &&
#ifdef USE_OUTPUT_RELEASE
         !fb_list->abort
#endif
        ) {
    for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
      if (old_id[i] == 0xFF)
        continue;
      else if (fb_list->fb_stat[old_id[i]].n_ref_count == 0)
        break;
    }

    if (i < MAX_FRAME_BUFFER_NUMBER)
      break;

    DPB_TRACE("NO FREE PIC BUFFER\n");
    pthread_cond_wait(&fb_list->ref_count_cv, &fb_list->ref_count_mutex);
  }
#else
  while (fb_list->free_buffers == 0 &&
#ifdef USE_OUTPUT_RELEASE
      !fb_list->abort
#endif
     ) {
    for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
      if (old_id[i] == 0xFF)
        continue;
      else if (fb_list->fb_stat[old_id[i]].n_ref_count == 0)
        break;
    }

    if (i < MAX_FRAME_BUFFER_NUMBER)
      break;

    pthread_mutex_unlock(&fb_list->ref_count_mutex);
    return FB_NOT_VALID_ID;
  }
#endif

#ifdef USE_OUTPUT_RELEASE
  if (fb_list->abort) {
    id = FB_NOT_VALID_ID;
  } else if (i != MAX_FRAME_BUFFER_NUMBER) {
    id = old_id[i];
    *is_free = 0;
  } else {
    id = PopFreeBuffer(fb_list);
    *is_free = 1;
  }
#else
  if (i != MAX_FRAME_BUFFER_NUMBER) {
    id = old_id[i];
    *is_free = 0;
  } else {
    id = PopFreeBuffer(fb_list);
    *is_free = 1;
  }
#endif

  DPB_TRACE("id = %d\n", id);

  pthread_mutex_unlock(&fb_list->ref_count_mutex);

  return id;
}

u32 GetFreeBufferCount(FrameBufferList *fb_list) {
  u32 free_buffers;
  pthread_mutex_lock(&fb_list->ref_count_mutex);
  free_buffers = fb_list->free_buffers;
  pthread_mutex_unlock(&fb_list->ref_count_mutex);

  return free_buffers;
}

void SetFreePicBuffer(FrameBufferList *fb_list, u32 id) {
  PushFreeBuffer(fb_list, id);
}

void IncrementDPBRefCount(dpbStorage_t *dpb) {
  u32 i;
  DPB_TRACE("\n");
  for (i = 0; i < dpb->dpb_size; i++) {
    IncrementRefUsage(dpb->fb_list, dpb->buffer[i].mem_idx);
    dpb->ref_id[i] = dpb->buffer[i].mem_idx;
  }
}

void DecrementDPBRefCount(dpbStorage_t *dpb) {
  u32 i;
  DPB_TRACE("\n");
  for (i = 0; i < dpb->dpb_size; i++) {
    DecrementRefUsage(dpb->fb_list, dpb->ref_id[i]);
  }
}

u32 IsBufferReferenced(FrameBufferList *fb_list, u32 id) {
  int n_ref_count;
  DPB_TRACE(" %d ? ref_count = %d\n", id, fb_list->fb_stat[id].n_ref_count);
  pthread_mutex_lock(&fb_list->ref_count_mutex);
  n_ref_count = fb_list->fb_stat[id].n_ref_count;
  pthread_mutex_unlock(&fb_list->ref_count_mutex);

  return n_ref_count != 0 ? 1 : 0;
}

u32 IsBufferOutput(FrameBufferList *fb_list, u32 id) {
  u32 b_output;
  pthread_mutex_lock(&fb_list->ref_count_mutex);
  b_output = fb_list->fb_stat[id].b_used & FB_OUTPUT ? 1 : 0;
  pthread_mutex_unlock(&fb_list->ref_count_mutex);

  return b_output;
}

void MarkOutputPicCorrupt(FrameBufferList *fb_list, u32 id, u32 errors) {
  i32 i, rd_id;

  pthread_mutex_lock(&fb_list->out_count_mutex);

  rd_id = fb_list->rd_id;

  for(i = 0; i < fb_list->num_out; i++) {
    if(fb_list->out_fifo[rd_id].mem_idx == id) {
      DPB_TRACE("id = %d\n", id);
      fb_list->out_fifo[rd_id].pic.nbr_of_err_mbs = errors;
      break;
    }

    rd_id = (rd_id + 1) % MAX_FRAME_BUFFER_NUMBER;
  }

  pthread_mutex_unlock(&fb_list->out_count_mutex);
}

void PushOutputPic(FrameBufferList *fb_list, const H264DecPicture *pic, u32 id) {
  if (pic != NULL ) {
    pthread_mutex_lock(&fb_list->out_count_mutex);

    assert(IsBufferOutput(fb_list, id));

    while(fb_list->num_out == MAX_FRAME_BUFFER_NUMBER) {
      /* make sure we do not overflow the output */
      /* pthread_cond_signal(&fb_list->out_empty_cv); */
      pthread_mutex_unlock(&fb_list->out_count_mutex);
      sched_yield();
      pthread_mutex_lock(&fb_list->out_count_mutex);
    }

    /* push to tail */
    fb_list->out_fifo[fb_list->wr_id].pic = *pic;
    fb_list->out_fifo[fb_list->wr_id].mem_idx = id;
    fb_list->num_out++;

    assert(fb_list->num_out <= MAX_FRAME_BUFFER_NUMBER);

    fb_list->wr_id++;
    if (fb_list->wr_id >= MAX_FRAME_BUFFER_NUMBER)
      fb_list->wr_id = 0;

    pthread_mutex_unlock(&fb_list->out_count_mutex);
  }

  if (pic != NULL)
    DPB_TRACE("num_out = %d\n",fb_list->num_out);
  else
    DPB_TRACE("EOS\n");
#ifdef USE_OUTPUT_RELEASE
  if (id == (u32)(-2))
    fb_list->flush_all = 1;
#endif
  /* pic == NULL signals the end of decoding in which case we just need to
   * wake-up the output thread (potentially sleeping) */
  sem_post(&fb_list->out_count_sem);
}

u32 PeekOutputPic(FrameBufferList *fb_list, H264DecPicture *pic) {
  u32 mem_idx;
  H264DecPicture *out;

#ifndef GET_OUTPUT_BUFFER_NON_BLOCK
  sem_wait(&fb_list->out_count_sem);
#endif

#ifdef USE_OUTPUT_RELEASE
  if (fb_list->abort)
    return ABORT_MARKER;
  if (fb_list->flush_all) {
    fb_list->flush_all = 0;
    return FLUSH_MARKER;
  }
#endif
  pthread_mutex_lock(&fb_list->out_count_mutex);
  if (!fb_list->num_out) {
    pthread_mutex_unlock(&fb_list->out_count_mutex);
    DPB_TRACE("Output empty, EOS\n");
    return 0;
  }

  pthread_mutex_unlock(&fb_list->out_count_mutex);

  out = &fb_list->out_fifo[fb_list->rd_id].pic;
  mem_idx = fb_list->out_fifo[fb_list->rd_id].mem_idx;

  pthread_mutex_lock(&fb_list->ref_count_mutex);

  while((fb_list->fb_stat[mem_idx].b_used & FB_HW_ONGOING) != 0)
    pthread_cond_wait(&fb_list->hw_rdy_cv, &fb_list->ref_count_mutex);

  pthread_mutex_unlock(&fb_list->ref_count_mutex);

  /* pop from head */
  (void)DWLmemcpy(pic, out, sizeof(H264DecPicture));

  DPB_TRACE("id = %d\n", mem_idx);

  pthread_mutex_lock(&fb_list->out_count_mutex);

  fb_list->num_out--;
  if (fb_list->num_out == 0) {
    pthread_cond_signal(&fb_list->out_empty_cv);
  }

  /* go to next output */
  fb_list->rd_id++;
  if (fb_list->rd_id >= MAX_FRAME_BUFFER_NUMBER)
    fb_list->rd_id = 0;

  pthread_mutex_unlock(&fb_list->out_count_mutex);

  return 1;
}

u32 PopOutputPic(FrameBufferList *fb_list, u32 id) {
  if(!IsBufferOutput(fb_list, id)) {
#ifndef USE_EXTERNAL_BUFFER
    assert(0);
#endif
    return 1;
  }

  ClearOutput(fb_list, id);

  return 0;
}

void RemoveTempOutputAll(FrameBufferList *fb_list) {
  i32 i;

  for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
    if (fb_list->fb_stat[i].b_used & FB_TEMP_OUTPUT) {
      ClearOutput(fb_list, i);
    }
  }
}

void ClearTempOut(FrameBufferList *fb_list, u32 id) {
  if (fb_list->fb_stat[id].b_used & FB_TEMP_OUTPUT) {
    ClearOutput(fb_list, id);
  }
}

#ifdef USE_OUTPUT_RELEASE
void RemoveOutputAll(FrameBufferList *fb_list) {
  i32 i;
#ifdef USE_OMXIL_BUFFER
  for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
    if (fb_list->fb_stat[i].b_used & FB_OUTPUT) {
      ClearOutput(fb_list, i);
    }
  }
#else
  i32 id;
  i32 rd_id = fb_list->rd_id;
  for (i = 0; i < fb_list->num_out; i++) {
    id = fb_list->out_fifo[rd_id].mem_idx;
    if (fb_list->fb_stat[id].b_used & FB_OUTPUT) {
      ClearOutput(fb_list, id);
    }

    rd_id = (rd_id + 1) % MAX_FRAME_BUFFER_NUMBER;
  }
#endif
}
#endif

u32 IsOutputEmpty(FrameBufferList *fb_list) {
  u32 num_out;
  pthread_mutex_lock(&fb_list->out_count_mutex);
  num_out = fb_list->num_out;
  pthread_mutex_unlock(&fb_list->out_count_mutex);

  return num_out == 0 ? 1 : 0;
}

void WaitOutputEmpty(FrameBufferList *fb_list) {
  if (!fb_list->b_initialized)
    return;

  pthread_mutex_lock(&fb_list->out_count_mutex);
  while (fb_list->num_out != 0) {
    pthread_cond_wait(&fb_list->out_empty_cv, &fb_list->out_count_mutex);
  }
  pthread_mutex_unlock(&fb_list->out_count_mutex);
}

void MarkListNotInUse(FrameBufferList *fb_list) {
  int i;
  pthread_mutex_lock(&fb_list->ref_count_mutex);
  for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++)
    fb_list->fb_stat[i].n_ref_count = 0;
  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

void WaitListNotInUse(FrameBufferList *fb_list) {
  int i;

  DPB_TRACE("\n");

  if (!fb_list->b_initialized)
    return;

  for (i = 0; i < MAX_FRAME_BUFFER_NUMBER; i++) {
    pthread_mutex_lock(&fb_list->ref_count_mutex);
    /* Wait until all buffers are not referenced */
#ifdef USE_OUTPUT_RELEASE
    while (fb_list->fb_stat[i].n_ref_count != 0 && !fb_list->abort)
#else
    while (fb_list->fb_stat[i].n_ref_count != 0)
#endif
    {
      pthread_cond_wait(&fb_list->ref_count_cv, &fb_list->ref_count_mutex);
    }
    pthread_mutex_unlock(&fb_list->ref_count_mutex);
  }
}

void AbortList(FrameBufferList *fb_list) {
  pthread_mutex_lock(&fb_list->ref_count_mutex);
#ifdef USE_OUTPUT_RELEASE
  fb_list->abort = 1;
#endif
  pthread_cond_signal(&fb_list->ref_count_cv);
  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

#ifdef USE_EXTERNAL_BUFFER
void MarkIdAllocated(FrameBufferList *fb_list, u32 id) {
  DPB_TRACE(" id = %d\n", id);
  pthread_mutex_lock(&fb_list->ref_count_mutex);

  if (fb_list->fb_stat[id].b_used | FB_FREE) {
    fb_list->fb_stat[id].b_used &= ~FB_FREE;
    if (fb_list->fb_stat[id].n_ref_count == 0)
      fb_list->free_buffers--;
  }
  fb_list->fb_stat[id].b_used |= FB_ALLOCATED;

  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

void MarkIdFree(FrameBufferList *fb_list, u32 id) {
  DPB_TRACE(" id = %d\n", id);
  pthread_mutex_lock(&fb_list->ref_count_mutex);

  if (fb_list->fb_stat[id].b_used | FB_ALLOCATED) {
    fb_list->fb_stat[id].b_used &= ~FB_ALLOCATED;
    if (fb_list->fb_stat[id].n_ref_count == 0)
      fb_list->free_buffers++;
  }
  fb_list->fb_stat[id].b_used |= FB_FREE;

  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}
#endif

#ifdef USE_OUTPUT_RELEASE
void SetAbortStatusInList(FrameBufferList *fb_list) {
  if(fb_list == NULL || !fb_list->b_initialized)
    return;

  pthread_mutex_lock(&fb_list->ref_count_mutex);
  fb_list->abort = 1;
  pthread_cond_signal(&fb_list->ref_count_cv);
  pthread_mutex_unlock(&fb_list->ref_count_mutex);
  sem_post(&fb_list->out_count_sem);
}

void ClearAbortStatusInList(FrameBufferList *fb_list) {
  if(fb_list == NULL || !fb_list->b_initialized)
    return;

  pthread_mutex_lock(&fb_list->ref_count_mutex);
  fb_list->abort = 0;
  pthread_mutex_unlock(&fb_list->ref_count_mutex);
}

void ResetOutFifoInList(FrameBufferList *fb_list) {
  (void)DWLmemset(fb_list->out_fifo, 0, MAX_FRAME_BUFFER_NUMBER * sizeof(struct OutElement_));
  fb_list->wr_id = 0;
  fb_list->rd_id = 0;
  fb_list->num_out = 0;
}

#endif
