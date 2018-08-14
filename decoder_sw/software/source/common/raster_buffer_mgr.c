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

#include "software/source/common/raster_buffer_mgr.h"
#include "input_queue.h"
#include "hevc_container.h"
#include "fifo.h"

#ifndef USE_EXTERNAL_BUFFER
struct BufferPair {
  struct DWLLinearMem tiled_buffer;
  struct DWLLinearMem pp_buffer;
};

typedef struct {
  u32 num_buffers;
  struct BufferPair* buffer_map;
  const void* dwl;
} RasterBufferMgrInst;

#else

typedef struct {
  u32 num_buffers;
  const void* dwl;
  u32 ext_buffer_config;
  InputQueue pp_queue;
} RasterBufferMgrInst;

#endif

#ifndef USE_EXTERNAL_BUFFER
RasterBufferMgr RbmInit(struct RasterBufferParams params) {
  RasterBufferMgrInst* inst = DWLmalloc(sizeof(RasterBufferMgrInst));
  inst->buffer_map = DWLcalloc(params.num_buffers, sizeof(struct BufferPair));
  inst->num_buffers = params.num_buffers;
  inst->dwl = params.dwl;
  u32 size = params.width * params.height * 3 / 2;
  struct DWLLinearMem empty = {0};

  for (int i = 0; i < inst->num_buffers; i++) {
    if (size) {
      inst->buffer_map[i].pp_buffer.mem_type = DWL_MEM_TYPE_DPB;
      if (DWLMallocLinear(inst->dwl, size, &inst->buffer_map[i].pp_buffer)) {
        RbmRelease(inst);
        return NULL;
      }
    } else {
      inst->buffer_map[i].pp_buffer = empty;
    }

    inst->buffer_map[i].tiled_buffer = params.tiled_buffers[i];
  }
  return inst;
}

struct DWLLinearMem *RbmGetPpBuffer(RasterBufferMgr instance,
                                        struct DWLLinearMem key) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  for (int i = 0; i < inst->num_buffers; i++)
    if (inst->buffer_map[i].tiled_buffer.virtual_address == key.virtual_address)
      return &inst->buffer_map[i].pp_buffer;

  return NULL;
}

struct DWLLinearMem RbmGetTiledBuffer(RasterBufferMgr instance,
                                      struct DWLLinearMem key) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  for (int i = 0; i < inst->num_buffers; i++)
    if (inst->buffer_map[i].pp_buffer.virtual_address ==
        key.virtual_address)
      return inst->buffer_map[i].tiled_buffer;
  struct DWLLinearMem empty = {0};
  return empty;
}

void RbmRelease(RasterBufferMgr instance) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  for (int i = 0; i < inst->num_buffers; i++) {
    if (inst->buffer_map[i].pp_buffer.virtual_address != NULL)
      DWLFreeLinear(inst->dwl, &inst->buffer_map[i].pp_buffer);
  }
  DWLfree(inst->buffer_map);
  DWLfree(inst);
}

#else

/* Allocate internal buffers here. */
RasterBufferMgr RbmInit(struct RasterBufferParams params) {
  RasterBufferMgrInst* inst = DWLmalloc(sizeof(RasterBufferMgrInst));
  inst->num_buffers = params.num_buffers;
  inst->dwl = params.dwl;
  inst->ext_buffer_config = params.ext_buffer_config;
  u32 size = params.width * params.height * 3 / 2;

  inst->pp_queue = NULL;
  if (size) {
    inst->pp_queue = InputQueueInit(inst->num_buffers);
    if (!inst->pp_queue)
      RbmRelease(inst);
  }
  return inst;
}

struct DWLLinearMem *RbmGetPpBuffer(RasterBufferMgr instance,
                                        struct DWLLinearMem key) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  struct DWLLinearMem *buffer;

  if (!inst->pp_queue) return NULL;

#ifdef GET_FREE_BUFFER_NON_BLOCK
  buffer = InputQueueGetBuffer(inst->pp_queue, 0);
#else
  buffer = InputQueueGetBuffer(inst->pp_queue, 1);
#endif

  return (buffer);
}


void RbmSetAbortStatus(RasterBufferMgr instance) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  if (inst == NULL) return;

  if(inst->pp_queue)
    InputQueueSetAbort(inst->pp_queue);
}

void RbmClearAbortStatus(RasterBufferMgr instance) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  if (inst == NULL) return;

  if(inst->pp_queue)
    InputQueueClearAbort(inst->pp_queue);
}

/* Make sure all the external buffers have been released before calling RbmRelease. */
void RbmRelease(RasterBufferMgr instance) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  if (inst == NULL) return;

  if (inst->pp_queue) {
    ASSERT(IS_EXTERNAL_BUFFER(inst->ext_buffer_config, DOWNSCALE_OUT_BUFFER) ||
           IS_EXTERNAL_BUFFER(inst->ext_buffer_config, RASTERSCAN_OUT_BUFFER));
    InputQueueRelease(inst->pp_queue);
  }

  DWLfree(inst);
}

/* Return next external buffer to be released, otherwise return empty buffer. */
struct DWLLinearMem RbmNextReleaseBuffer(RasterBufferMgr instance) {
  struct DWLLinearMem empty = {0};
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;
  struct DWLLinearMem *buffer;

  if (IS_EXTERNAL_BUFFER(inst->ext_buffer_config, RASTERSCAN_OUT_BUFFER) ||
      IS_EXTERNAL_BUFFER(inst->ext_buffer_config, DOWNSCALE_OUT_BUFFER)) {
    ASSERT(inst->pp_queue);

    buffer = InputQueueGetBuffer(inst->pp_queue, 0);
    if (buffer) {
      return *buffer;
    }
  }

  return empty;
}

void RbmAddPpBuffer(RasterBufferMgr instance, struct DWLLinearMem *pp_buffer, i32 i) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;

  InputQueueAddBuffer(inst->pp_queue, pp_buffer);
}

struct DWLLinearMem * RbmReturnPpBuffer(RasterBufferMgr instance, const u32 *addr) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;

  return (InputQueueReturnBuffer(inst->pp_queue, addr));
}

void RbmReturnAllPpBuffer(RasterBufferMgr instance) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;

  InputQueueReturnAllBuffer(inst->pp_queue);
}

void RbmResetPpBuffer(RasterBufferMgr instance) {
  RasterBufferMgrInst* inst = (RasterBufferMgrInst*)instance;

  InputQueueReset(inst->pp_queue);
}
#endif
