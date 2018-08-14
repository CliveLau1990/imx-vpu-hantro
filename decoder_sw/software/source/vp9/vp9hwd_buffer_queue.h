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

#ifndef VP9HWD_PICTURE_BUFFER_QUEUE_H_
#define VP9HWD_PICTURE_BUFFER_QUEUE_H_

#include "basetype.h"
#include "dwl.h"

/* BufferQueue is picture queue indexing module, which manages the
 * buffer references on client's behalf. Module will maintain an index of
 * available and used buffers. When free  buffer is not available module
 * will block the thread asking for a buffer index. */

typedef void* BufferQueue; /* Opaque instance variable. */

/* Special value for index to tell it is unset. */
#define REFERENCE_NOT_SET 0xFFFFFFFF

#define ABORT_MARKER (-2)

/* Functions to initialize and release the BufferQueue. */
BufferQueue Vp9BufferQueueInitialize(i32 n_buffers);
void Vp9BufferQueueRelease(BufferQueue queue, i32 safe);

i32 Vp9BufferQueueGetRef(BufferQueue queue, u32 index);

/* Functions to manage the reference picture state. These are to be called
 * only from the decoding thread to get and manipulate the current decoder
 * reference buffer status. When a reference is pointing to a specific buffer
 * BufferQueue will automatically increment the reference counter to the given
 * buffer and decrement the reference counter to the previous reference buffer.
 */
void Vp9BufferQueueUpdateRef(BufferQueue queue, u32 ref_flags, i32 buffer);

/* Functions to manage references to the picture buffers. Caller is responsible
 * for calling AddRef when somebody will be using the given buffer and
 * RemoveRef each time somebody stops using a given buffer. When reference count
 * reaches 0, buffer is automatically added to the pool of free buffers. */
void Vp9BufferQueueAddRef(BufferQueue queue, i32 buffer);
void Vp9BufferQueueRemoveRef(BufferQueue queue, i32 buffer);

/* Function to get free buffers from the queue. Returns negative value if the
   free buffer was not found and the buffer count is below the limit. Otherwise
   blocks until the requested buffer is available.  Automatic +1 ref count. */
i32 Vp9BufferQueueGetBuffer(BufferQueue queue, u32 limit);

/* Function to wait until all buffers are in available status. */
void Vp9BufferQueueWaitPending(BufferQueue queue);

void Vp9BufferQueueAddBuffer(BufferQueue queue);

void Vp9BufferQueueResetReferences(BufferQueue queue);

u32 Vp9BufferQueueCountReferencedBuffers(BufferQueue queue);

void Vp9BufferQueueSetAbort(BufferQueue queue);

void Vp9BufferQueueClearAbort(BufferQueue queue);

void Vp9BufferQueueEmptyRef(BufferQueue queue, i32 buffer);

void Vp9BufferQueueReset(BufferQueue queue);
#endif /* VP9HWD_PICTURE_BUFFER_QUEUE_H_ */
