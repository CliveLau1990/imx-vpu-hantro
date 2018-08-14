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

#include "msgque.h"
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX MSG "

OMX_ERRORTYPE HantroOmx_msgque_init(OMX_IN msgque* q)
{
    DBGT_ASSERT(q);
    q->head = 0;
    q->tail = 0;
    q->size = 0;
    
    OMX_ERRORTYPE err = OMX_ErrorNone;
    err = OSAL_MutexCreate(&q->mutex);
    if (err != OMX_ErrorNone)
        return err;
    
    err = OSAL_EventCreate(&q->event);
    if (err != OMX_ErrorNone)
        OSAL_MutexDestroy(q->mutex);

    return err;
}

void HantroOmx_msgque_destroy(OMX_IN msgque* q)
{
    DBGT_ASSERT(q);
    OMX_ERRORTYPE err = OMX_ErrorNone;

    err = OSAL_MutexLock(q->mutex);
    DBGT_ASSERT(err == OMX_ErrorNone);

    while (q->tail)
    {
        msg_node* next = q->tail->next;
        OSAL_Free(q->tail->data);
        q->tail->data = 0;
        OSAL_Free(q->tail);
        q->tail = next;
    }

    err = OSAL_MutexUnlock(q->mutex);  DBGT_ASSERT(err == OMX_ErrorNone);
    err = OSAL_MutexDestroy(q->mutex); DBGT_ASSERT(err == OMX_ErrorNone);
    err = OSAL_EventDestroy(q->event); DBGT_ASSERT(err == OMX_ErrorNone);
}

OMX_ERRORTYPE HantroOmx_msgque_push_back(OMX_IN msgque* q, OMX_IN OMX_PTR ptr)
{ 
    DBGT_ASSERT(q);
    DBGT_ASSERT(ptr);
    
    msg_node* tail = (msg_node*)OSAL_Malloc(sizeof(msg_node));
    if (!tail)
        return OMX_ErrorInsufficientResources;
    
    tail->next = q->tail;
    tail->prev = 0;
    tail->data = ptr;
    
    // get mutex now
    OMX_ERRORTYPE err = OMX_ErrorNone;
    err = OSAL_MutexLock(q->mutex);
    if (err != OMX_ErrorNone)
    {
        OSAL_Free(tail);
        return err;
    }

    // first set the signal if needed and once that is allright
    // only then change the queue, cause that cant fail
    
    if (q->size == 0)
    {
        err = OSAL_EventSet(q->event);
        if (err != OMX_ErrorNone)
        {
            OSAL_MutexUnlock(q->mutex);
            return err;
        }
    }
    
    q->size += 1;
    if (q->tail)
        q->tail->prev = tail;
    q->tail  = tail;
    if (!q->head)
        q->head = q->tail;

    err = OSAL_MutexUnlock(q->mutex); 
    DBGT_ASSERT(err == OMX_ErrorNone);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE HantroOmx_msgque_get_front(OMX_IN msgque* q, OMX_OUT OMX_PTR* ptr)
{
    DBGT_ASSERT(q);
    DBGT_ASSERT(ptr);

    // get mutex now
    OMX_ERRORTYPE err = OMX_ErrorNone;
    err = OSAL_MutexLock(q->mutex);
    if (err != OMX_ErrorNone)
        return err;

    if (q->size - 1 == 0)
    {
        // reset the signal to not set
        err = OSAL_EventReset(q->event);
        if (err != OMX_ErrorNone)
        {
            OSAL_MutexUnlock(q->mutex);
            return err;
        }
    }
    if (q->size == 0)
    {
        DBGT_ASSERT(q->head == 0);
        DBGT_ASSERT(q->tail == 0);
        *ptr = 0;
    }
    else
    {
        msg_node* head = q->head;
    /*    DBGT_ASSERT(head->next == 0); */
        *ptr = head->data;
        q->head  = head->prev;
        q->size -= 1;
        if (q->head)
            q->head->next = 0;
        else
            q->tail = 0;

        OSAL_Free(head);
    }
    err =  OSAL_MutexUnlock(q->mutex);
    DBGT_ASSERT(err == OMX_ErrorNone);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE HantroOmx_msgque_get_size(OMX_IN msgque* q, OMX_OUT OMX_U32* size)
{
    DBGT_ASSERT(q);
    DBGT_ASSERT(size);
    
    OMX_ERRORTYPE err = OMX_ErrorNone;
    err = OSAL_MutexLock(q->mutex);
    if (err != OMX_ErrorNone)
        return err;
    
    *size = q->size;
    err = OSAL_MutexUnlock(q->mutex);
    DBGT_ASSERT(err == OMX_ErrorNone);

    return OMX_ErrorNone;
}

