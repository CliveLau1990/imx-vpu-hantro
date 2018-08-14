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

#include "basecomp.h"
#include <string.h>
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX BASE "

typedef struct thread_param
{
    BASECOMP*       self;
    OMX_PTR         arg;
    thread_main_fun fun;
} thread_param;

static
OMX_U32 HantroOmx_basecomp_thread_main(void* unsafe)
{
    thread_param* param = (thread_param*)(unsafe);

    OMX_U32 ret = param->fun(param->self, param->arg);
    OSAL_Free(param);
    return ret;
}


OMX_ERRORTYPE HantroOmx_basecomp_init(BASECOMP* b, thread_main_fun f, OMX_PTR arg)
{
    DBGT_ASSERT(b);
    DBGT_ASSERT(f);
    memset(b, 0, sizeof(BASECOMP));

    OMX_ERRORTYPE err = HantroOmx_msgque_init(&b->queue);
    if (err != OMX_ErrorNone)
        return err;
      
    thread_param* param = (thread_param*)OSAL_Malloc(sizeof(thread_param));
    if (!param)
    {
        err = OMX_ErrorInsufficientResources;
        goto FAIL;
    }

    param->self = b;
    param->arg  = arg;
    param->fun  = f;

    err = OSAL_ThreadCreate(HantroOmx_basecomp_thread_main, param, 0, &b->thread);
    if (err != OMX_ErrorNone)
        goto FAIL;

    return OMX_ErrorNone;

 FAIL:
    HantroOmx_msgque_destroy(&b->queue);
    if (param)
        OSAL_Free(param);
    return err;
}


OMX_ERRORTYPE HantroOmx_basecomp_destroy(BASECOMP* b)
{
    DBGT_ASSERT(b);
    DBGT_ASSERT(b->thread);
    OMX_ERRORTYPE err = OSAL_ThreadDestroy(b->thread);
    DBGT_ASSERT(err == OMX_ErrorNone);

    HantroOmx_msgque_destroy(&b->queue);
    memset(b, 0, sizeof(BASECOMP));

    if (err != OMX_ErrorNone)
        return err;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE HantroOmx_basecomp_send_command(BASECOMP* b, CMD* c)
{
    DBGT_ASSERT(b && c);
    
    CMD* ptr = (CMD*)OSAL_Malloc(sizeof(CMD));
    if (!ptr)
        return OMX_ErrorInsufficientResources;
    
    memcpy(ptr, c, sizeof(CMD));
    
    OMX_ERRORTYPE err = HantroOmx_msgque_push_back(&b->queue, ptr);
    if (err != OMX_ErrorNone)
    {
        OSAL_Free(ptr);
        return err;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE HantroOmx_basecomp_recv_command(BASECOMP* b, CMD* c)
{
    DBGT_ASSERT(b && c);
    
    void* unsafe = 0;
    OMX_ERRORTYPE err = HantroOmx_msgque_get_front(&b->queue, &unsafe);
    if (err != OMX_ErrorNone)
        return err;
    
    DBGT_ASSERT(unsafe);
    CMD* ptr = (CMD*)unsafe;
    memcpy(c, ptr, sizeof(CMD));
    OSAL_Free(ptr);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE HantroOmx_basecomp_try_recv_command(BASECOMP* b, CMD* c, OMX_BOOL* ok)
{
    DBGT_ASSERT(b && c);
    void* unsafe = NULL;
    OMX_ERRORTYPE err = HantroOmx_msgque_get_front(&b->queue, &unsafe);
    if (err != OMX_ErrorNone)
        return err;
    
    *ok = OMX_FALSE;
    if (unsafe)
    {
        CMD* ptr = (CMD*)unsafe;
        memcpy(c, ptr, sizeof(CMD));
        OSAL_Free(ptr);
        *ok = OMX_TRUE;
    }
    return OMX_ErrorNone;
}


void HantroOmx_generate_uuid(OMX_HANDLETYPE comp, OMX_UUIDTYPE* uuid)
{
    DBGT_ASSERT(comp && uuid);
    
    OMX_U32 i;
    OMX_U32 ival = (OMX_U32)comp;

    for (i=0; i<sizeof(OMX_UUIDTYPE); ++i)
    {
        (*uuid)[i] = ival >> (i % sizeof(OMX_U32));
    }
}


OMX_ERRORTYPE HantroOmx_cmd_dispatch(CMD* cmd, OMX_PTR arg)
{
    DBGT_ASSERT(cmd);
    OMX_ERRORTYPE err = OMX_ErrorNone;
    switch (cmd->type)
    {
        case CMD_SEND_COMMAND:
            err = cmd->arg.fun(cmd->arg.cmd, cmd->arg.param1, cmd->arg.data, arg);
            break;
        default:
            DBGT_ASSERT(0);
            break;
    }
    return err;
}



