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

#include "OSAL.h"


#define _XOPEN_SOURCE 600

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "util.h"
#include "dbgtrace.h"

#undef DBGT_PREFIX
#undef DBGT_LAYER
#define DBGT_PREFIX "OSAL"
#define DBGT_LAYER 3

#ifdef MEMALLOCHW
#include <memalloc.h>
#endif

#ifdef OMX_MEM_TRC
 #include <stdio.h>
 FILE * pf = NULL;
#endif

#ifdef USE_DWL
#include "dwl_linux.h"
#include "dwl.h"
#endif

#ifdef USE_EWL
#include "ewl.h"
#endif

/*------------------------------------------------------------------------------
    Definitions
------------------------------------------------------------------------------*/

#define MEMORY_SENTINEL 0xACDCACDC;

typedef struct OSAL_THREADDATATYPE {
    pthread_t oPosixThread;
    pthread_attr_t oThreadAttr;
    OSAL_U32 (*pFunc)(OSAL_PTR pParam);
    OSAL_PTR pParam;
    OSAL_U32 uReturn;
} OSAL_THREADDATATYPE;

typedef struct {
    OSAL_BOOL       bSignaled;
    pthread_mutex_t mutex;
    int             fd[2];
} OSAL_THREAD_EVENT;

/*------------------------------------------------------------------------------
    External compiler flags
--------------------------------------------------------------------------------

MEMALLOCHW: If defined we will use kernel driver for HW memory allocation


--------------------------------------------------------------------------------
    OSAL_Malloc
------------------------------------------------------------------------------*/
OSAL_PTR OSAL_Malloc(OSAL_U32 size)
{
    DBGT_PROLOG("");

    OSAL_U32 extra = sizeof(OSAL_U32) * 2;
    OSAL_U8*  data = (OSAL_U8*)malloc(size + extra);
    OSAL_U32 sentinel = MEMORY_SENTINEL;

    if (!data) {
        DBGT_CRITICAL("No more memory (size=%d)", (int)(size + extra));
        return(0);
    }

    memcpy(data, &size, sizeof(size));
    memcpy(&data[size + sizeof(size)], &sentinel, sizeof(sentinel));

    DBGT_EPILOG("");
    return data + sizeof(size);
}

/*------------------------------------------------------------------------------
    OSAL_Free
------------------------------------------------------------------------------*/
void OSAL_Free(OSAL_PTR pData)
{
    DBGT_PROLOG("");

    OSAL_U8* block    = ((OSAL_U8*)pData) - sizeof(OSAL_U32);

    OSAL_U32 sentinel = MEMORY_SENTINEL;
    OSAL_U32 size     = *((OSAL_U32*)block);

    DBGT_ASSERT(memcmp(&block[size+sizeof(size)], &sentinel, sizeof(sentinel))==0 &&
            "mem corruption detected");

    free(block);
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorInit
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_AllocatorInit(OSAL_ALLOCATOR* alloc)
{
    DBGT_PROLOG("");

#ifdef OMX_MEM_TRC
        pf = fopen("omx_mem_trc.csv", "w");
        if (pf)
            fprintf(pf,
            "linead memory usage in bytes;linear memory usage in 4096 pages;\n");
#endif

#ifdef USE_DWL
    if (alloc->pdwl)
        return OSAL_ERRORNONE;

    DBGT_PDEBUG("OSAL_Init");

#ifndef IS_G2_DECODER
    struct DWLInitParam dwlInit;
    dwlInit.client_type = DWL_CLIENT_TYPE_H264_DEC;
#else
    struct DWLInitParam dwlInit;
    dwlInit.client_type = DWL_CLIENT_TYPE_HEVC_DEC;
#endif
    alloc->pdwl = (void*)DWLInit(&dwlInit);

    if (!alloc->pdwl)
    {
        DBGT_CRITICAL("DWLInit failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
#endif

#ifdef USE_EWL
    if (alloc->pewl)
        return OSAL_ERRORNONE;

    DBGT_PDEBUG("OSAL_Init");

    EWLInitParam_t ewlInit;
#ifdef ENCH2
    ewlInit.clientType = EWL_CLIENT_TYPE_HEVC_ENC;
#else
    ewlInit.clientType = EWL_CLIENT_TYPE_H264_ENC;
#endif

    alloc->pewl = (void*)EWLInit(&ewlInit);

    if (!alloc->pewl)
    {
        DBGT_CRITICAL("EWLInit failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
#endif

#ifdef MEMALLOCHW
    OSAL_ERRORTYPE err = OSAL_ERRORNONE;
    // open memalloc for linear memory allocation
    alloc->fd_memalloc = open("/tmp/dev/memalloc", O_RDWR | O_SYNC);
    //alloc->fd_memalloc = open("/dev/memalloc", O_RDWR | O_SYNC);
    if (alloc->fd_memalloc == -1)
    {
        DBGT_CRITICAL("memalloc not found\n");
        err = OSAL_ERROR_UNDEFINED;
        goto FAIL;
    }
    // open raw memory for memory mapping
    alloc->fd_mem = open("/dev/mem", O_RDWR | O_SYNC);
    //alloc->fd_mem = open("/dev/uio0", O_RDWR | O_SYNC);
    if (alloc->fd_mem == -1)
    {
        DBGT_CRITICAL("uio0 not found");
        err = OSAL_ERROR_UNDEFINED;
        goto FAIL;
    }
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
 FAIL:
    if (alloc->fd_memalloc > 0) close(alloc->fd_memalloc);
    if (alloc->fd_mem > 0)      close(alloc->fd_mem);
    DBGT_EPILOG("");
    return err;
#endif

#if !defined (USE_DWL) && !defined (MEMALLOCHW) && !defined (USE_EWL)
    UNUSED_PARAMETER(alloc);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
#endif
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorDestroy
------------------------------------------------------------------------------*/
void OSAL_AllocatorDestroy(OSAL_ALLOCATOR* alloc)
{
    DBGT_PROLOG("");

#ifdef OMX_MEM_TRC
    if (pf)
    {
        fclose(pf);
        pf =NULL;
    }
#endif

#ifdef USE_DWL
    DBGT_ASSERT(alloc);
    if (alloc->pdwl)
        DWLRelease(alloc->pdwl);
    alloc->pdwl = NULL;
#endif

#ifdef USE_EWL
    DBGT_ASSERT(alloc);
    if (alloc->pewl)
        EWLRelease(alloc->pewl);
    alloc->pewl = NULL;
#endif

#ifdef MEMALLOCHW
    DBGT_ASSERT(alloc->fd_memalloc > 0);
    DBGT_ASSERT(alloc->fd_mem > 0);
    int ret = 0;
    ret = close(alloc->fd_memalloc);
    DBGT_ASSERT(ret == 0);
    ret= close(alloc->fd_mem);
    DBGT_ASSERT(ret == 0);

    alloc->fd_memalloc = 0;
    alloc->fd_mem      = 0;
#endif

#if !defined (USE_DWL) && !defined (MEMALLOCHW) && !defined (USE_EWL)
    UNUSED_PARAMETER(alloc);
    DBGT_EPILOG("");
#endif
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorAllocMem
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_AllocatorAllocMem(OSAL_ALLOCATOR* alloc, OSAL_U32* size,
        OSAL_U8** bus_data, OSAL_BUS_WIDTH* bus_address)
{
    DBGT_PROLOG("");

#ifdef OMX_MEM_TRC

    if (pf)
        fprintf(pf, "%d bytes; %d chunks;\n", *size, *size/4096);

#endif

#ifdef USE_DWL
#ifndef IS_G2_DECODER
    struct DWLLinearMem *info = malloc(sizeof(*info));
#else
    struct DWLLinearMem *info = malloc(sizeof(*info));
#endif

    DBGT_PDEBUG("OSAL_AllocatorAllocMem size=%d", (int)*size);

    if (alloc->pdwl == 0)
    {
        OSAL_ALLOCATOR a;
        OSAL_AllocatorInit(&a);
        alloc->pdwl = a.pdwl;
    }

    //int ret = DWLMallocLinear(alloc->pdwl, *size, &info);
    int ret = DWLMallocLinear(alloc->pdwl, *size, info);
    if (ret == 0)
    {
        *bus_data = (OSAL_U8*)info->virtual_address;
        *bus_address = (OSAL_BUS_WIDTH)info->bus_address;
        DBGT_PDEBUG("OSAL_AllocatorAllocMem OK\n    bus addr = 0x%08lx\n    vir addr = %p",
            (OSAL_BUS_WIDTH)info->bus_address, info->virtual_address);
        free(info);
        DBGT_EPILOG("");
        return OSAL_ERRORNONE;
    }
    else
    {
        DBGT_CRITICAL("MallocLinear error %d", ret);
        free(info);
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }
#endif

#ifdef USE_EWL
    EWLLinearMem_t *info = malloc(sizeof(EWLLinearMem_t));

    DBGT_PDEBUG("OSAL_AllocatorAllocMem size=%d", (int)*size);

    if (alloc->pewl == 0)
    {
        OSAL_ALLOCATOR a;
        OSAL_AllocatorInit(&a);
        alloc->pewl = a.pewl;
    }

    int ret = EWLMallocLinear(alloc->pewl, *size, info);
    if (ret == 0)
    {
        *bus_data = (OSAL_U8*)info->virtualAddress;
        *bus_address = (OSAL_BUS_WIDTH)info->busAddress;
        DBGT_PDEBUG("OSAL_AllocatorAllocMem OK\n    bus addr = 0x%08lx\n    vir addr = %p",
            (OSAL_BUS_WIDTH)info->busAddress, info->virtualAddress);
        free(info);
        DBGT_EPILOG("");
        return OSAL_ERRORNONE;
    }
    else
    {
        DBGT_CRITICAL("MallocLinear error %d", ret);
        free(info);
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }
#endif

#ifdef MEMALLOCHW
    DBGT_ASSERT(alloc->fd_memalloc > 0);
    DBGT_ASSERT(alloc->fd_mem > 0);
    int pgsize = getpagesize();

    MemallocParams params;
    memset(&params, 0, sizeof(MemallocParams));

    *size = (*size + pgsize) & (~(pgsize - 1));
    params.size = *size;
    *bus_data   = MAP_FAILED;
    // get linear memory buffer
    ioctl(alloc->fd_memalloc, MEMALLOC_IOCXGETBUFFER, &params);
    if (params.bus_address == 0)
    {
        DBGT_CRITICAL("alloc->fd_memalloc failed (size=%d) - OSAL_ERROR_INSUFFICIENT_RESOURCES", params.size);
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }
    // map the bus address to a virtual address

    DBGT_PDEBUG("alloc success. bus addr = 0x%x", params.busAddress);

    DBGT_PDEBUG("mmap(0, %d, %x, %x, %d, 0x%x);", *size, PROT_READ | PROT_WRITE, MAP_SHARED, alloc->fd_mem, params.busAddress);
    *bus_data = (OSAL_U8*)mmap(0, *size, PROT_READ | PROT_WRITE, MAP_SHARED, alloc->fd_mem, params.bus_address);
    if (*bus_data == MAP_FAILED)
    {
        DBGT_CRITICAL("mmap failed %s", strerror(errno));
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }

    DBGT_PDEBUG("mmap success. vir addr = 0x%x", *bus_data);
    memset(*bus_data, 0, *size);

    DBGT_PDEBUG("memset OK");
    *bus_address = params.bus_address;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
#endif

#if !defined (USE_DWL) && !defined (MEMALLOCHW) && !defined (USE_EWL)
    UNUSED_PARAMETER(alloc);
    OSAL_U32 extra = sizeof(OSAL_U32);
    OSAL_U8* data  = (OSAL_U8*)malloc(*size + extra);
    if (data == NULL)
    {
        DBGT_CRITICAL("malloc failed (size=%d) - OSAL_ERROR_INSUFFICIENT_RESOURCES", (int)(*size + extra));
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    OSAL_U32 sentinel = MEMORY_SENTINEL;
    // copy sentinel at the end of mem block
    memcpy(&data[*size], &sentinel, sizeof(OSAL_U32));

    *bus_data    = data;
    *bus_address = (OSAL_BUS_WIDTH)data;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
#endif
}

/*------------------------------------------------------------------------------
    OSAL_AllocatorFreeMem
------------------------------------------------------------------------------*/
void OSAL_AllocatorFreeMem(OSAL_ALLOCATOR* alloc, OSAL_U32 size,
        OSAL_U8* bus_data, OSAL_BUS_WIDTH bus_address)
{
    DBGT_PROLOG("");

#ifdef USE_DWL
    DBGT_PDEBUG("OSAL_AllocatorFreeMem");

    struct DWLLinearMem *info = malloc(sizeof(*info));

    info->size = NEXT_MULTIPLE(size, 32);
    info->virtual_address = (u32*)bus_data;
    info->bus_address = bus_address;

    DWLFreeLinear(alloc->pdwl, info);
    free(info);

    DBGT_PDEBUG("OSAL_AllocatorFreeMem %x ok", (unsigned int)bus_address);
#endif

#ifdef USE_EWL
    DBGT_PDEBUG("OSAL_AllocatorFreeMem");

    EWLLinearMem_t * info = malloc(sizeof(EWLLinearMem_t));

    info->size = size;
    info->virtualAddress = (u32*)bus_data;
    info->busAddress = bus_address;

    EWLFreeLinear(alloc->pewl, info);
    free(info);

    DBGT_PDEBUG("OSAL_AllocatorFreeMem %x ok", (unsigned int)bus_address);
#endif


#ifdef MEMALLOCHW
    DBGT_ASSERT(alloc->fd_memalloc > 0);
    DBGT_ASSERT(alloc->fd_mem > 0);

    if (bus_address)
        ioctl(alloc->fd_memalloc, MEMALLOC_IOCSFREEBUFFER, &bus_address);
    if (bus_data != MAP_FAILED)
        munmap(bus_data, size);
#endif

#if !defined (USE_DWL) && !defined (MEMALLOCHW) && !defined (USE_EWL)
    DBGT_ASSERT(((OSAL_BUS_WIDTH)bus_data) == bus_address);
    OSAL_U32 sentinel = MEMORY_SENTINEL;
    DBGT_ASSERT(memcmp(&bus_data[size], &sentinel, sizeof(OSAL_U32)) == 0 &&
            "memory corruption detected");

    UNUSED_PARAMETER(alloc);
    /*UNUSED_PARAMETER(size);
    UNUSED_PARAMETER(bus_address);*/
    free(bus_data);
#endif
    DBGT_EPILOG("");
}


/*------------------------------------------------------------------------------
    OSAL_AllocatorIsReady
------------------------------------------------------------------------------*/
OSAL_BOOL OSAL_AllocatorIsReady(const OSAL_ALLOCATOR* alloc)
{
    DBGT_PROLOG("");

#ifdef USE_DWL
    if (alloc->pdwl)
    {
        DBGT_EPILOG("");
        return 1;
    }
    else
    {
        DBGT_EPILOG("");
        return 0;
    }
#endif

#ifdef USE_EWL
    if (alloc->pewl)
    {
        DBGT_EPILOG("");
        return 1;
    }
    else
    {
        DBGT_EPILOG("");
        return 0;
    }
#endif

#ifdef MEMALLOCHW
    if (alloc->fd_memalloc > 0 && alloc->fd_mem > 0)
    {
        DBGT_EPILOG("");
        return 1;
    }
    else
    {
        DBGT_EPILOG("");
        return 0;
    }
#endif

#if !defined (USE_DWL) && !defined (MEMALLOCHW) && !defined (USE_EWL)
    UNUSED_PARAMETER(alloc);
    DBGT_EPILOG("");
    return 1;
#endif
}


/*------------------------------------------------------------------------------
    OSAL_Memset
------------------------------------------------------------------------------*/
OSAL_PTR OSAL_Memset(OSAL_PTR pDest, OSAL_U32 cChar, OSAL_U32 nCount)
{
    return memset(pDest, cChar, nCount);
}

/*------------------------------------------------------------------------------
    OSAL_Memcpy
------------------------------------------------------------------------------*/
OSAL_PTR OSAL_Memcpy(OSAL_PTR pDest, OSAL_PTR pSrc, OSAL_U32 nCount)
{
    return memcpy(pDest, pSrc, nCount);
}

/*------------------------------------------------------------------------------
    BlockSIGIO      Linux EWL uses SIGIO to signal interrupt
------------------------------------------------------------------------------*/
static void BlockSIGIO()
{
    DBGT_PROLOG("");
    sigset_t set, oldset;

    /* Block SIGIO from the main thread to make sure that it will be handled
     * in the encoding thread */

    sigemptyset(&set);
    sigemptyset(&oldset);
    sigaddset(&set, SIGIO);
    pthread_sigmask(SIG_BLOCK, &set, &oldset);
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    threadFunc
------------------------------------------------------------------------------*/
static void *threadFunc(void *pParameter)
{
    DBGT_PROLOG("");

    OSAL_THREADDATATYPE *pThreadData;
    pThreadData = (OSAL_THREADDATATYPE *)pParameter;
    pThreadData->uReturn = pThreadData->pFunc(pThreadData->pParam);
    DBGT_EPILOG("");
    return pThreadData;
}

/*------------------------------------------------------------------------------
    OSAL_ThreadCreate
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_ThreadCreate(OSAL_U32 (*pFunc)(OSAL_PTR pParam),
        OSAL_PTR pParam, OSAL_U32 nPriority, OSAL_PTR *phThread)
{
    DBGT_PROLOG("");

    OSAL_THREADDATATYPE *pThreadData;
    struct sched_param sched;

    pThreadData = (OSAL_THREADDATATYPE*)OSAL_Malloc(sizeof(OSAL_THREADDATATYPE));
    if (pThreadData == NULL)
    {
        DBGT_CRITICAL("OSAL_Malloc failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    pThreadData->pFunc = pFunc;
    pThreadData->pParam = pParam;
    pThreadData->uReturn = 0;

    pthread_attr_init(&pThreadData->oThreadAttr);

    pthread_attr_getschedparam(&pThreadData->oThreadAttr, &sched);
    sched.sched_priority += nPriority;
    pthread_attr_setschedparam(&pThreadData->oThreadAttr, &sched);

    if (pthread_create(&pThreadData->oPosixThread,
                       &pThreadData->oThreadAttr,
                       threadFunc,
                       pThreadData)) {
        DBGT_CRITICAL("pthread_create failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        OSAL_Free(pThreadData);
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    BlockSIGIO();

    *phThread = (OSAL_PTR)pThreadData;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_ThreadDestroy
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_ThreadDestroy(OSAL_PTR hThread)
{
    DBGT_PROLOG("");

    OSAL_THREADDATATYPE *pThreadData = (OSAL_THREADDATATYPE *)hThread;
    void *retVal = &pThreadData->uReturn;

    if (pThreadData == NULL) {
        DBGT_CRITICAL("(pThreadData == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    //pthread_cancel(pThreadData->oPosixThread);

    if (pthread_join(pThreadData->oPosixThread, &retVal)) {
        DBGT_CRITICAL("pthread_join failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    OSAL_Free(pThreadData);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_ThreadSleep
------------------------------------------------------------------------------*/
void OSAL_ThreadSleep(OSAL_U32 ms)
{
    DBGT_PROLOG("");
    usleep(ms*1000);
    DBGT_EPILOG("");
}

/*------------------------------------------------------------------------------
    OSAL_MutexCreate
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexCreate(OSAL_PTR *phMutex)
{
    DBGT_PROLOG("");

    pthread_mutex_t *pMutex = (pthread_mutex_t *)
                                OSAL_Malloc(sizeof(pthread_mutex_t));
    static pthread_mutexattr_t oAttr;
    static pthread_mutexattr_t *pAttr = NULL;

    if (pAttr == NULL &&
        !pthread_mutexattr_init(&oAttr) &&
        !pthread_mutexattr_settype(&oAttr, PTHREAD_MUTEX_RECURSIVE))
    {
        pAttr = &oAttr;
    }

    if (pMutex == NULL)
    {
        DBGT_CRITICAL("OSAL_Malloc failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    if (pthread_mutex_init(pMutex, pAttr)) {
        DBGT_CRITICAL("pthread_mutex_init failed - OSAL_ERROR_INSUFFICIENT_RESOURCES");
        OSAL_Free(pMutex);
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    *phMutex = (void *)pMutex;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}


/*------------------------------------------------------------------------------
    OSAL_MutexDestroy
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexDestroy(OSAL_PTR hMutex)
{
    DBGT_PROLOG("");
    pthread_mutex_t *pMutex = (pthread_mutex_t *)hMutex;

    if (pMutex == NULL) {
        DBGT_CRITICAL("(pMutex == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_destroy(pMutex)) {
        DBGT_CRITICAL("pthread_mutex_destroy failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    OSAL_Free(pMutex);
    pMutex = NULL;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_MutexLock
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexLock(OSAL_PTR hMutex)
{
    DBGT_PROLOG("");

    pthread_mutex_t *pMutex = (pthread_mutex_t *)hMutex;
    int err;

    if (pMutex == NULL) {
        DBGT_CRITICAL("(pMutex == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    err = pthread_mutex_lock(pMutex);
    switch (err) {
    case 0:
        DBGT_EPILOG("");
        return OSAL_ERRORNONE;
    case EINVAL:
        DBGT_CRITICAL("pthread_mutex_lock EINVAL");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    case EDEADLK:
        DBGT_CRITICAL("pthread_mutex_lock EDEADLK");
        DBGT_EPILOG("");
        return OSAL_ERROR_NOT_READY;
    default:
        DBGT_CRITICAL("pthread_mutex_lock undefined err");
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }

    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_MutexUnlock
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_MutexUnlock(OSAL_PTR hMutex)
{
    DBGT_PROLOG("");

    pthread_mutex_t *pMutex = (pthread_mutex_t *)hMutex;
    int err;

    if (pMutex == NULL) {
        DBGT_CRITICAL("(pMutex == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    err = pthread_mutex_unlock(pMutex);
    switch (err) {
    case 0:
        DBGT_EPILOG("");
        return OSAL_ERRORNONE;
    case EINVAL:
        DBGT_CRITICAL("pthread_mutex_unlock EINVAL");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    case EPERM:
        DBGT_CRITICAL("pthread_mutex_unlock EPERM");
        DBGT_EPILOG("");
        return OSAL_ERROR_NOT_READY;
    default:
        DBGT_CRITICAL("pthread_mutex_unlock undefined err");
        DBGT_EPILOG("");
        return OSAL_ERROR_UNDEFINED;
    }

    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventCreate
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventCreate(OSAL_PTR *phEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = OSAL_Malloc(sizeof(OSAL_THREAD_EVENT));

    if (pEvent == NULL) {
        DBGT_CRITICAL("OSAL_Malloc failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    pEvent->bSignaled = 0;

    if (pipe(pEvent->fd) == -1)
    {
        DBGT_CRITICAL("pipe(pEvent->fd) failed");
        OSAL_Free(pEvent);
        pEvent = NULL;
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    if (pthread_mutex_init(&pEvent->mutex, NULL))
    {
        DBGT_CRITICAL("pthread_mutex_init failed");
        close(pEvent->fd[0]);
        close(pEvent->fd[1]);
        OSAL_Free(pEvent);
        pEvent = NULL;
        DBGT_EPILOG("");
        return OSAL_ERROR_INSUFFICIENT_RESOURCES;
    }

    *phEvent = (OSAL_PTR)pEvent;
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventDestroy
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventDestroy(OSAL_PTR hEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = (OSAL_THREAD_EVENT *)hEvent;
    if (pEvent == NULL) {
        DBGT_CRITICAL("(pEvent == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_lock(&pEvent->mutex)) {
        DBGT_CRITICAL("pthread_mutex_lock failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    int err = 0;
    err = close(pEvent->fd[0]); DBGT_ASSERT(err == 0);
    err = close(pEvent->fd[1]); DBGT_ASSERT(err == 0);

    pthread_mutex_unlock(&pEvent->mutex);
    pthread_mutex_destroy(&pEvent->mutex);

    OSAL_Free(pEvent);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventReset
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventReset(OSAL_PTR hEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = (OSAL_THREAD_EVENT *)hEvent;
    if (pEvent == NULL) {
        DBGT_CRITICAL("(pEvent == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_lock(&pEvent->mutex)) {
        DBGT_CRITICAL("pthread_mutex_lock failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pEvent->bSignaled)
    {
        // empty the pipe
        char c = 1;
        int ret = read(pEvent->fd[0], &c, 1);
        if (ret == -1) {
            DBGT_CRITICAL("read(pEvent->fd[0], &c, 1) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
        pEvent->bSignaled = 0;
    }

    pthread_mutex_unlock(&pEvent->mutex);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_GetTime
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventSet(OSAL_PTR hEvent)
{
    DBGT_PROLOG("");

    OSAL_THREAD_EVENT *pEvent = (OSAL_THREAD_EVENT *)hEvent;
    if (pEvent == NULL) {
        DBGT_CRITICAL("(pEvent == NULL)");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (pthread_mutex_lock(&pEvent->mutex)) {
        DBGT_CRITICAL("pthread_mutex_lock failed");
        DBGT_EPILOG("");
        return OSAL_ERROR_BAD_PARAMETER;
    }

    if (!pEvent->bSignaled)
    {
        char c = 1;
        int ret = write(pEvent->fd[1], &c, 1);
        if (ret == -1) {
            DBGT_CRITICAL("write(pEvent->fd[1], &c, 1) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
        pEvent->bSignaled = 1;
    }

    pthread_mutex_unlock(&pEvent->mutex);
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}

/*------------------------------------------------------------------------------
    OSAL_EventWait
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventWait(OSAL_PTR hEvent, OSAL_U32 uMsec,
        OSAL_BOOL* pbTimedOut)
{
    OSAL_BOOL signaled = 0;
    return OSAL_EventWaitMultiple(&hEvent, &signaled, 1, uMsec, pbTimedOut);
}

/*------------------------------------------------------------------------------
    OSAL_EventWaitMultiple
------------------------------------------------------------------------------*/
OSAL_ERRORTYPE OSAL_EventWaitMultiple(OSAL_PTR* hEvents,
        OSAL_BOOL* bSignaled, OSAL_U32 nCount, OSAL_U32 mSecs,
        OSAL_BOOL* pbTimedOut)
{
    DBGT_PROLOG("");

    DBGT_ASSERT(hEvents);
    DBGT_ASSERT(bSignaled);

    fd_set read;
    FD_ZERO(&read);

    int max = 0;
    unsigned i = 0;
    for (i=0; i<nCount; ++i)
    {
        OSAL_THREAD_EVENT* pEvent = (OSAL_THREAD_EVENT*)(hEvents[i]);

        if (pEvent == NULL) {
            DBGT_CRITICAL("(pEvent == NULL)");
            DBGT_EPILOG("");
            return OSAL_ERROR_BAD_PARAMETER;
        }

        int fd = pEvent->fd[0];
        if (fd > max)
            max = fd;

        FD_SET(fd, &read);
    }

    if (mSecs == INFINITE_WAIT)
    {
        int ret = select(max+1, &read, NULL, NULL, NULL);
        if (ret == -1) {
            DBGT_CRITICAL("select(max+1, &read, NULL, NULL, NULL) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
    }
    else
    {
        struct timeval tv;
        memset(&tv, 0, sizeof(struct timeval));
        tv.tv_usec = mSecs * 1000;
        int ret = select(max+1, &read, NULL, NULL, &tv);
        if (ret == -1) {
            DBGT_CRITICAL("select(max+1, &read, NULL, NULL, &tv) failed");
            DBGT_EPILOG("");
            return OSAL_ERROR_UNDEFINED;
        }
        if (ret == 0)
        {
            *pbTimedOut =  1;
        }
    }

    for (i=0; i<nCount; ++i)
    {
        OSAL_THREAD_EVENT* pEvent = (OSAL_THREAD_EVENT*)hEvents[i];

        if (pEvent == NULL) {
            DBGT_CRITICAL("(pEvent == NULL)");
            DBGT_EPILOG("");
            return OSAL_ERROR_BAD_PARAMETER;
        }

        int fd = pEvent->fd[0];
        if (FD_ISSET(fd, &read))
            bSignaled[i] = 1;
        else
            bSignaled[i] = 0;
    }
    DBGT_EPILOG("");
    return OSAL_ERRORNONE;
}


/*------------------------------------------------------------------------------
    OSAL_GetTime
------------------------------------------------------------------------------*/
OSAL_U32 OSAL_GetTime()
{
    DBGT_PROLOG("");

    struct timeval now;
    gettimeofday(&now, NULL);
    DBGT_EPILOG("");
    return ((OSAL_U32)now.tv_sec) * 1000 + ((OSAL_U32)now.tv_usec) / 1000;
}

