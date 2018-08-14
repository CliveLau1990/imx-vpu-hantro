/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
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

#include "port.h"
#include "OSAL.h"
#include <string.h>
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX PORT"

typedef BUFFER** swap_type;


static 
void swap_ptr(swap_type* one, swap_type* two)
{
    swap_type temp = *one;
    *one = *two;
    *two = temp;
}

OMX_ERRORTYPE HantroOmx_bufferlist_init(BUFFERLIST* list, OMX_U32 size)
{ 
    DBGT_ASSERT(list);
    
    list->list = (BUFFER**)OSAL_Malloc(sizeof(BUFFER*) * size);
    if (!list->list)
        return OMX_ErrorInsufficientResources;
    
    memset(list->list, 0, sizeof(BUFFER*) * size);
    list->size     = 0;
    list->capacity = size;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE HantroOmx_bufferlist_reserve(BUFFERLIST* list, OMX_U32 newsize)
{
    DBGT_ASSERT(list);
    if (newsize < list->capacity)
        return OMX_ErrorBadParameter;
    
    BUFFER** data = (BUFFER**)OSAL_Malloc(sizeof(BUFFER**) * newsize);
    if (!data)
        return OMX_ErrorInsufficientResources;
    
    memset(data, 0, sizeof(BUFFER*) * newsize);
    memcpy(data, list->list, list->size * sizeof(BUFFER*));
    
    swap_ptr(&data, &list->list);

    list->capacity = newsize;
    OSAL_Free(data);
    return OMX_ErrorNone;
}

void HantroOmx_bufferlist_destroy(BUFFERLIST* list)
{
    DBGT_ASSERT(list);
    if (list->list)
        OSAL_Free(list->list);         
    memset(list, 0, sizeof(BUFFERLIST));
}

OMX_U32 HantroOmx_bufferlist_get_size(BUFFERLIST* list)
{
    DBGT_ASSERT(list);
    return list->size;
}

OMX_U32 HantroOmx_bufferlist_get_capacity(BUFFERLIST* list)
{
    DBGT_ASSERT(list);
    return list->capacity;
}

BUFFER** HantroOmx_bufferlist_at(BUFFERLIST* list, OMX_U32 i)
{
    DBGT_ASSERT(list);
    DBGT_ASSERT(i < list->size);
    return &list->list[i];
}

void HantroOmx_bufferlist_remove(BUFFERLIST* list, OMX_U32 i)
{
    DBGT_ASSERT(list);
    DBGT_ASSERT(i < list->size);
    memmove(list->list+i, list->list+i+1, (list->size-i-1)*sizeof(BUFFER*));
    --list->size;
}

void HantroOmx_bufferlist_clear(BUFFERLIST* list)
{
    DBGT_ASSERT(list);
    list->size = 0;
}

OMX_BOOL HantroOmx_bufferlist_push_back(BUFFERLIST* list, BUFFER* buff)
{
    DBGT_ASSERT(list);
    if (list->size == list->capacity)
        return OMX_FALSE;
    list->list[list->size++] = buff;
    return OMX_TRUE;
}

OMX_ERRORTYPE HantroOmx_port_init(PORT* p, OMX_U32 nBufferCountMin, OMX_U32 nBufferCountActual, OMX_U32 nBuffers, OMX_U32 nBufferSize)
{
    DBGT_ASSERT(p);
    memset(p, 0, sizeof(PORT));
 
    OMX_ERRORTYPE err = OMX_ErrorNone;
    err = OSAL_MutexCreate(&p->buffermutex);
    if (err != OMX_ErrorNone)
        goto INIT_FAIL;
    
    err = OSAL_EventCreate(&p->bufferevent);
    if (err != OMX_ErrorNone)
        goto INIT_FAIL;
   
    err = OSAL_EventCreate(&p->bufferRdy);
    if (err != OMX_ErrorNone)
        goto INIT_FAIL;

    if (nBuffers)
    {
        err = HantroOmx_bufferlist_init(&p->buffers, nBuffers);
        if (err != OMX_ErrorNone)
            goto INIT_FAIL;
        
        err = HantroOmx_bufferlist_init(&p->bufferqueue, nBuffers);
        if (err != OMX_ErrorNone)
            goto INIT_FAIL;
    }
    p->def.nBufferCountActual = nBufferCountActual;
    p->def.nBufferCountMin    = nBufferCountMin;
    p->def.nBufferSize        = nBufferSize;
    return OMX_ErrorNone;
    
 INIT_FAIL:
    if (p->buffermutex)
        OSAL_MutexDestroy(p->buffermutex);
    if (p->bufferevent)
        OSAL_EventDestroy(p->bufferevent);
    if (p->bufferRdy)
        OSAL_EventDestroy(p->bufferRdy);
    HantroOmx_bufferlist_destroy(&p->buffers);
    HantroOmx_bufferlist_destroy(&p->bufferqueue);
    return err;
}

void HantroOmx_port_destroy(PORT* p)
{
    DBGT_ASSERT(p);

    OMX_U32 i = 0;
    OMX_U32 x = HantroOmx_bufferlist_get_size(&p->buffers);
    for (i=0; i<x; ++i)
    {
        BUFFER* buff = *HantroOmx_bufferlist_at(&p->buffers, i);
        OSAL_Free(buff);
    }
    HantroOmx_bufferlist_destroy(&p->buffers);
    HantroOmx_bufferlist_destroy(&p->bufferqueue);
    
    OMX_ERRORTYPE err;
    err = OSAL_MutexDestroy(p->buffermutex);
    DBGT_ASSERT(err == OMX_ErrorNone);
    err = OSAL_EventDestroy(p->bufferevent);
    DBGT_ASSERT(err == OMX_ErrorNone);
    err = OSAL_EventDestroy(p->bufferRdy);
    DBGT_ASSERT(err == OMX_ErrorNone);

    memset(p, 0, sizeof(PORT));
}

OMX_BOOL HantroOmx_port_is_allocated(PORT* p)
{
    return HantroOmx_bufferlist_get_capacity(&p->buffers) > 0;
}

OMX_BOOL HantroOmx_port_is_ready(PORT* p)
{
    DBGT_ASSERT(p);
    OMX_BOOL populated;

    if (!p->def.bEnabled)
        return OMX_TRUE;

    OSAL_MutexLock(p->buffermutex);
    populated = p->def.bPopulated;
    OSAL_MutexUnlock(p->buffermutex);

    return populated;
}

OMX_BOOL HantroOmx_port_is_enabled(PORT* p)
{
    return p->def.bEnabled;
}

OMX_BOOL HantroOmx_port_has_buffers(PORT* p)
{
    OSAL_MutexLock(p->buffermutex);
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->buffers);
    OSAL_MutexUnlock(p->buffermutex);
    return size > 0;
}

OMX_BOOL HantroOmx_port_is_supplier(PORT* p)
{
    if (p->tunnelcomp == NULL)
        return OMX_FALSE;

    if (p->def.eDir == OMX_DirInput && 
        p->tunnel.eSupplier == OMX_BufferSupplyInput)
        return OMX_TRUE;
    
    if (p->def.eDir == OMX_DirOutput && 
        p->tunnel.eSupplier == OMX_BufferSupplyOutput)
        return OMX_TRUE;
    
    return OMX_FALSE;
}

OMX_BOOL HantroOmx_port_is_tunneled(PORT* p)
{
    return p->tunnelcomp != NULL;
}

OMX_BOOL HantroOmx_port_has_all_supplied_buffers(PORT* p)
{
    if (p->tunnelcomp == NULL)
        return OMX_TRUE;
    
    if (HantroOmx_port_buffer_count(p) == HantroOmx_port_buffer_queue_count(p))
        return OMX_TRUE;
    
    return OMX_FALSE;
}

void HantroOmx_port_setup_tunnel(PORT* p, OMX_HANDLETYPE comp, OMX_U32 port, OMX_BUFFERSUPPLIERTYPE type)
{
    DBGT_ASSERT(p);
    p->tunnelcomp = comp;
    p->tunnelport = port;
    p->tunnel.nTunnelFlags = 0;
    p->tunnel.eSupplier    = type;
}

BUFFER* HantroOmx_port_find_buffer(PORT* p, OMX_BUFFERHEADERTYPE* header)
{
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->buffers);
    if (size == 0)
        return NULL;

    OMX_U32 i = 0;
    for (i=0; i<size; ++i)
    {
        BUFFER* buff = *HantroOmx_bufferlist_at(&p->buffers, i);
        if (buff->header == header)
            return buff;
    }
    return NULL;
}

OMX_BOOL HantroOmx_port_allocate_next_buffer(PORT* p, BUFFER** buff)
{
    BUFFER* next = (BUFFER*)OSAL_Malloc(sizeof(BUFFER));
    if (next == NULL)
        return OMX_FALSE;
    
    memset(next, 0, sizeof(BUFFER));
    next->flags |= BUFFER_FLAG_IN_USE;
    // hack for tunneling.
    // The buffer header is always accessed through a pointer. In normal case
    // it just points to the header object within the buffer. But in case
    // of tunneling it can be made to point to a header allocated by the tunneling component.
    next->header = &next->headerdata;
    OMX_BOOL ret = HantroOmx_bufferlist_push_back(&p->buffers, next);
    if (ret == OMX_FALSE)
    {
        OMX_ERRORTYPE err;
        OMX_U32 capacity = HantroOmx_bufferlist_get_capacity(&p->buffers);
        if (capacity == 0) capacity = 5;
        err = HantroOmx_bufferlist_reserve(&p->buffers, capacity * 2);
        if (err != OMX_ErrorNone)
        {
            OSAL_Free(next);
            return OMX_FALSE;
        }
        HantroOmx_bufferlist_push_back(&p->buffers, next);
    }
    *buff = next;
    return OMX_TRUE;
}

OMX_BOOL HantroOmx_port_release_buffer(PORT* p, BUFFER* buff)
{
    OMX_U32 i=0; 
    OMX_U32 x=HantroOmx_bufferlist_get_size(&p->buffers);
    for (i=0; i<x; ++i)
    {
        BUFFER** buffer = HantroOmx_bufferlist_at(&p->buffers, i);
        if (*buffer == buff)
        {
            OSAL_Free(buff);
            OSAL_MutexLock(p->buffermutex);
            HantroOmx_bufferlist_remove(&p->buffers, i);
            OSAL_MutexUnlock(p->buffermutex);
            return OMX_TRUE;
        }
    }
    return OMX_FALSE;
}

OMX_BOOL HantroOmx_port_release_all_allocated(PORT* p)
{
    // release all allocated buffer objects
    OMX_U32 x=HantroOmx_bufferlist_get_size(&p->buffers);
    OMX_U32 i=0;
    for (i=0; i<x; ++i)
    {
        BUFFER** buffer = HantroOmx_bufferlist_at(&p->buffers, i);
        OSAL_Free(*buffer);
    }
    HantroOmx_bufferlist_clear(&p->buffers);
    return OMX_TRUE;
}

OMX_ERRORTYPE HantroOmx_port_push_buffer(PORT* p, BUFFER* buff)
{
    OMX_ERRORTYPE err;
    OMX_U32 ret = HantroOmx_bufferlist_push_back(&p->bufferqueue, buff);
    if (ret == OMX_FALSE)
    {
        OMX_U32 capacity = HantroOmx_bufferlist_get_capacity(&p->bufferqueue);
        err = HantroOmx_bufferlist_reserve(&p->bufferqueue, capacity * 2);
        if (err != OMX_ErrorNone)
            return err;
        HantroOmx_bufferlist_push_back(&p->bufferqueue, buff);
    }
    err = OSAL_EventSet(p->bufferevent);
    if (err != OMX_ErrorNone)
    {
        OMX_U32 size = HantroOmx_bufferlist_get_size(&p->bufferqueue);
        HantroOmx_bufferlist_remove(&p->bufferqueue, size-1);
        return err;
    }
    return OMX_ErrorNone;
}

OMX_BOOL HantroOmx_port_get_buffer(PORT* p, BUFFER** buff)
{
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->bufferqueue);
    if (size == 0)
    {
        *buff = NULL;
        return OMX_FALSE;
    }
    
    *buff = *HantroOmx_bufferlist_at(&p->bufferqueue, 0);
    return OMX_TRUE;
}

OMX_BOOL HantroOmx_port_get_buffer_at(PORT* p, BUFFER** buff, OMX_U32 i)
{
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->bufferqueue);
    if (!(i < size))
    {
        *buff = NULL;
        return OMX_FALSE;
    }
    *buff = *HantroOmx_bufferlist_at(&p->bufferqueue, i);
    return OMX_TRUE;
}

OMX_BOOL HantroOmx_port_get_allocated_buffer_at(PORT* p, BUFFER** buff, OMX_U32 i)
{
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->buffers);
    if (!(i < size))
    {
        *buff = NULL;
        return OMX_FALSE;
    }
    *buff = *HantroOmx_bufferlist_at(&p->buffers, i);
    return OMX_TRUE;
}

OMX_BOOL HantroOmx_port_pop_buffer(PORT* p)
{
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->bufferqueue);
    if (size==0)
        return OMX_FALSE;

    if (size-1 == 0)
    {
        OMX_ERRORTYPE err = OSAL_EventReset(p->bufferevent);
        if (err != OMX_ErrorNone)
            return OMX_FALSE;
    }
    HantroOmx_bufferlist_remove(&p->bufferqueue, 0);
    return OMX_TRUE;
}

OMX_BOOL HantroOmx_port_pop_buffer_at(PORT* p, OMX_U32 i)
{
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->bufferqueue);
    if (size == 0 || i > size)
        return OMX_FALSE;

    if (size-1 == 0)
    {
        OMX_ERRORTYPE err = OSAL_EventReset(p->bufferevent);
        if (err != OMX_ErrorNone)
            return OMX_FALSE;
    }
    HantroOmx_bufferlist_remove(&p->bufferqueue, i);
    return OMX_TRUE;
}

OMX_ERRORTYPE HantroOmx_port_lock_buffers(PORT* p)
{
    DBGT_ASSERT(p);
    return OSAL_MutexLock(p->buffermutex);
}

OMX_ERRORTYPE HantroOmx_port_unlock_buffers(PORT* p)
{
    DBGT_ASSERT(p);
    return OSAL_MutexUnlock(p->buffermutex);
}

OMX_U32 HantroOmx_port_buffer_count(PORT* p)
{
    return HantroOmx_bufferlist_get_size(&p->buffers);
}

OMX_U32 HantroOmx_port_buffer_queue_count(PORT* p)
{
    return HantroOmx_bufferlist_get_size(&p->bufferqueue);
}

void HantroOmx_port_buffer_queue_clear(PORT* p)
{
    OMX_U32 size = HantroOmx_bufferlist_get_size(&p->bufferqueue);
    if (size == 0)
        return;
    OSAL_EventReset(p->bufferevent);
    HantroOmx_bufferlist_clear(&p->bufferqueue);
}
