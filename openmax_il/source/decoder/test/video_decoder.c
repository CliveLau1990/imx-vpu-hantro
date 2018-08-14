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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <OMX_Core.h>
#include <OMX_Video.h>
#include <OMX_Types.h>
#include <OMX_Component.h>
#include "vsi_vendor_ext.h"

#include "OSAL.h"
#include "util.h"

#include "basetype.h"
#include "ivf.h"
#include "version.h"
#include "file_reader.h"

#ifdef ENABLE_CODEC_RV
#include "rm_parse.h"
#include "rv_depack.h"
#include "rv_decode.h"
#include "rvdecapi.h"
#include "rv_ff_read.h"
#endif

#define DBGT_DECLARE_AUTOVAR
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "VIDEO_DECODER"

#define INPUT_BUFFER_COUNT          10
#define OUTPUT_BUFFER_COUNT         10
#define MAX_BUFFER_COUNT            32
#define DEFAULT_BUFFER_SIZE_INPUT   262144 * 5
#define DEFAULT_BUFFER_SIZE_OUTPUT  1920 * 1088 * 4
#define RM2YUV_INITIAL_READ_SIZE    16
#define EXTRA_BUFFERS               3 + 1
#define CODEC_VIDEO                 0
#define CODEC_IMAGE                 1

#define MIN_REF_SIZE  3655712       // miminum buffer size for 1080P streams
// for testing on PC only. does not work on HW
#define CLIENT_ALLOC

#define INIT_OMX_TYPE(f) \
    memset(&f, 0, sizeof(f)); \
  (f).nSize = sizeof(f);      \
  (f).nVersion.s.nVersionMajor = 1; \
  (f).nVersion.s.nVersionMinor = 1


  // actual video properties when it comes out from the decoder
typedef struct VIDEOPROPS
{
    int framecount;
    int displaywidth;
    int displayheight;
    int stride;
    int sliceheight;
    int framelimit;
} VIDEOPROPS;

  // test case parameters
  // use 0 for "don't care"
typedef struct TESTARGS
{
    char* input;                // input file to be decoded
    char* reference_file;       // reference file (if there is a single reference. in case of multiple ref files this is NULL)
    char* reference_dir;        // reference file directory
    char* blend_input;          // blend input data for mask1 if any
    char* test_id;              // testcase ID
    char* output_file;
    int input_format;           // input video color format in case of raw input
    int width;                  // output frame width (scale X)
    int height;                 // output frame height (scale Y)
    int rotation;               // output rotation
    int mirroring;              // output mirroring (exlusive with rotation) 1 for vertical 2 for horizontal
    int output_format;          // output color format
    int crop_x;                 // input video frame cropping X start offset
    int crop_y;                 // input video frame cropping Y start offset
    int crop_width;             // cropping width
    int crop_height;            // cropping height
    int input_width;            // input video width in case of raw input
    int input_height;           // input video height in case of raw input
    int domain;                 // 0 for video, 1 for image
    int fail;                   // should this testcase fail to decode
    int no_compare;             // only decode, do not compare
    int coding_type;
    OMX_STRING name;            // component name
    int buffer_size_in;
    int buffer_size_out;
    int limit;                  // frame limit.
    int deinterlace;
    int dithering;
    int deblocking;
    int mvc_stream;
    int tiled;
    int pixFormat;
    int rfc;
    int adaptive;
    int guard_buffers;
    int secure;

    struct rgb
    {
        int alpha;
        int contrast;
        int brightness;
        int saturation;
    } rgb;

    struct mask
    {
        int originX;
        int originY;
        int width;
        int height;
    } mask1, mask2;
} TESTARGS;

typedef struct HEADERLIST
{
   OMX_BUFFERHEADERTYPE* hdrs[MAX_BUFFER_COUNT];
   OMX_U32 readpos;
   OMX_U32 writepos;
} HEADERLIST;

OMX_HANDLETYPE queue_mutex;
volatile OMX_BOOL   EOS;
volatile OMX_BOOL   ERROR;
volatile OMX_BOOL   DO_DECODE;
OMX_BOOL            VERBOSE_OUTPUT;
volatile OMX_BOOL   reconfigPending;
volatile OMX_BOOL   waitingBuffers;

HEADERLIST src_queue;
HEADERLIST dst_queue;

HEADERLIST input_queue;
HEADERLIST output_queue;
HEADERLIST input_ret_queue;
HEADERLIST output_ret_queue;

OMX_BUFFERHEADERTYPE* alpha_mask_buffer;
ALLOC_PRIVATE bufferPrivInput[MAX_BUFFER_COUNT];
ALLOC_PRIVATE bufferPrivOutput[MAX_BUFFER_COUNT];

typedef struct NOREALLOCLIST
{
    OMX_U64 address[MAX_BUFFER_COUNT];
    OMX_U32 size[MAX_BUFFER_COUNT];
    OMX_U32 count;
}NOREALLOCLIST;

NOREALLOCLIST noReallocBufList;

FILE* vid_out;
VIDEOPROPS* vid_props;

/* divx3 */
int divx3_width = 0;
int divx3_height = 0;
OMX_U32 offset;
int start_DIVX3;

u32 traceUsedStream;
u32 previousUsed;
int formatCheck;

#ifdef ENABLE_CODEC_RV
/* real media */
pthread_t threads[1];
OMX_VIDEO_PARAM_RVTYPE rv;
thread_data buffering_data;
pthread_mutex_t buff_mx;
pthread_cond_t fillbuffer;
int empty_buffer_avail;
int headers_ready = 0;
int stream_end;
OMX_BOOL rawRVFile;
OMX_BOOL rawRV8;

/* real parameters from fileformat */
extern    u32 maxCodedWidth;
extern    u32 maxCodedHeight;
extern    u32 bIsRV8;
extern    u32 pctszSize;

void startRealFileReader(FILE * pFile, OMX_U32** pBuffer, OMX_U32* pAllocLen, OMX_BOOL* eof);
#endif

u32 test_case_id = 0;

typedef int (*read_func)(FILE*, char*, int, void*, OMX_BOOL*);

void init_list(HEADERLIST* list)
{
    memset(list, 0, sizeof(HEADERLIST));
}

void push_header(HEADERLIST* list, OMX_BUFFERHEADERTYPE* header)
{
    DBGT_ASSERT(list->writepos < MAX_BUFFER_COUNT);
    list->hdrs[list->writepos++] = header;
}

void get_header(HEADERLIST* list, OMX_BUFFERHEADERTYPE** header)
{
    if (list->readpos == list->writepos)
    {
        *header = NULL;
        return;
    }
    *header = list->hdrs[list->readpos++];
}

void copy_list(HEADERLIST* dst, HEADERLIST* src)
{
    memcpy(dst, src, sizeof(HEADERLIST));
}

void fill_alloc_private(ALLOC_PRIVATE* allocPriv, void* pBuffer, OMX_U64 buffer, OMX_U32 size)
{
    allocPriv->pBufferData = (OMX_U8*)pBuffer;
    allocPriv->nBusAddress = buffer;
    allocPriv->nBufferSize = size;
}

OMX_ERRORTYPE fill_buffer_done(OMX_HANDLETYPE comp, OMX_PTR appdata, OMX_BUFFERHEADERTYPE* buffer)
{
    UNUSED_PARAMETER(appdata);
    DBGT_PROLOG("");

    if(reconfigPending == OMX_TRUE && !buffer->nFilledLen)
    {
#ifdef CLIENT_ALLOC
        OUTPUT_BUFFER_PRIVATE *output_data = (OUTPUT_BUFFER_PRIVATE*) buffer->pOutputPortPrivate;
        if (buffer->pOutputPortPrivate && !output_data->realloc)
        {
            noReallocBufList.address[noReallocBufList.count] = buffer->pBuffer;
            noReallocBufList.size[noReallocBufList.count] = buffer->nAllocLen;
            noReallocBufList.count++;
        }
        else
        {
            if (buffer->pBuffer) free(buffer->pBuffer);
        }

#endif
        if (buffer)
            ((OMX_COMPONENTTYPE*)comp)->FreeBuffer(comp, 1, buffer);
        return OMX_ErrorNone;
    }

    if (EOS == OMX_TRUE || DO_DECODE == OMX_FALSE /*|| reconfigPending == OMX_TRUE*/)
        return OMX_ErrorNone; // this is a buffer that is returned on transition from executing to idle. do not queue it up anymore

    if (VERBOSE_OUTPUT)
    {
        printf("\rFill buffer done: nFilledLen:%d nOffset:%d frame:%d timestamp:%lld\n",
               (int)buffer->nFilledLen,
               (int)buffer->nOffset,
               vid_props->framecount,
                buffer->nTimeStamp);
        fflush(stdout);
    }

    if(!vid_props->framelimit || (vid_props->framecount < vid_props->framelimit))
    {
        if (vid_out)
        {
            if (buffer->pOutputPortPrivate != NULL) // Get address from output port private pointer
            {
                OUTPUT_BUFFER_PRIVATE *output_data = (OUTPUT_BUFFER_PRIVATE*) buffer->pOutputPortPrivate;

                printf("Write luma buffer %p (bus: %llu)\n", output_data->pLumaBase, output_data->nLumaBusAddress);
                fwrite(output_data->pLumaBase, 1, output_data->nLumaSize, vid_out);
                fflush(vid_out);
                printf("Write chroma buffer %p (bus: %llu)\n", output_data->pChromaBase, output_data->nChromaBusAddress);
                fwrite(output_data->pChromaBase, 1, output_data->nChromaSize, vid_out);
                fflush(vid_out);
                printf("RFC: luma base %p, chroma base %p\n", output_data->sRfcTable.pLumaBase, output_data->sRfcTable.pChromaBase);
            }
            else
            {
                printf("Write buffer %p (bus: %lu)\n", buffer->pBuffer, (OMX_U32) buffer->pBuffer);
                fwrite(buffer->pBuffer, 1, buffer->nFilledLen, vid_out);
                fflush(vid_out);
            }
        }
        else
        {
            FILE* strm = NULL;
            char framename[255];
            sprintf(framename, "frame%d.yuv", vid_props->framecount);
            strm = fopen(framename, "wb");
            if (strm)
            {
                if (buffer->pOutputPortPrivate != NULL) // Get address from output port private pointer
                {
                    OUTPUT_BUFFER_PRIVATE *output_data = (OUTPUT_BUFFER_PRIVATE*) buffer->pOutputPortPrivate;

                    printf("Write luma buffer %p (bus: %llu)\n", output_data->pLumaBase, output_data->nLumaBusAddress);
                    fwrite(output_data->pLumaBase, 1, output_data->nLumaSize, strm);
                    fflush(vid_out);
                    printf("Write chroma buffer %p (bus: %llu)\n", output_data->pChromaBase, output_data->nChromaBusAddress);
                    fwrite(output_data->pChromaBase, 1, output_data->nChromaSize, strm);
                    fflush(vid_out);
                }
                else
                {
                    printf("Write buffer %p (bus: %lu)\n", buffer->pBuffer, (OMX_U32) buffer->pBuffer);
                    fwrite(buffer->pBuffer, 1, buffer->nFilledLen, strm);
                    fflush(strm);
                }
                fclose(strm);
            }
        }
    }
    vid_props->framecount++;

    if (buffer->nFlags & OMX_BUFFERFLAG_EOS)
    {
        if (VERBOSE_OUTPUT)
            printf("\tOMX_BUFFERFLAG_EOS\n");

        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    buffer->nFilledLen = 0;
    buffer->nOffset    = 0;
    buffer->nTimeStamp = 0;
    ((OMX_COMPONENTTYPE*)comp)->FillThisBuffer(comp, buffer);

    // Push remaining output buffers to decoder
    OMX_BUFFERHEADERTYPE* output = NULL;
    OMX_ERRORTYPE err = OMX_ErrorNone;
    do
    {
        OSAL_MutexLock(queue_mutex);
        get_header(&output_queue, &output);
        OSAL_MutexUnlock(queue_mutex);
        if (output)
        {
            OMX_U32 j;
            // Find correct ALLOC_PRIVATE struct and set the pInputPortPrivate pointer
            for (j=0; j<MAX_BUFFER_COUNT; j++)
            {
                if (bufferPrivOutput[j].pBufferData == output->pBuffer)
                {
                    output->pInputPortPrivate = &bufferPrivOutput[j];
                    break;
                }
            }
            output->nOutputPortIndex = 1;
            err = ((OMX_COMPONENTTYPE*)comp)->FillThisBuffer(comp, output);
            if (err != OMX_ErrorNone)
            {
                printf("FillThisBuffer err %d\n", err);
                return err;
            }
        }
    }
    while (output);

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

OMX_ERRORTYPE empty_buffer_done(OMX_HANDLETYPE comp, OMX_PTR appdata, OMX_BUFFERHEADERTYPE* buffer)
{
    UNUSED_PARAMETER(comp);
    UNUSED_PARAMETER(appdata);
    DBGT_PROLOG("");

    if (buffer->nInputPortIndex == 2)
    {
        DBGT_EPILOG("");
        return OMX_ErrorNone; // do not queue the alpha blend buffer in the "normal" input buffer queue
    }

    OSAL_MutexLock(queue_mutex);
    {
        if (input_ret_queue.writepos >= MAX_BUFFER_COUNT)
            printf("No space in return queue\n");
        else
            push_header(&input_ret_queue, buffer);
    }
    OSAL_MutexUnlock(queue_mutex);
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

OMX_ERRORTYPE comp_event(OMX_HANDLETYPE comp, OMX_PTR appdata, OMX_EVENTTYPE event, OMX_U32 nData1, OMX_U32 nData2, OMX_PTR eventdata)
{
    UNUSED_PARAMETER(appdata);
    UNUSED_PARAMETER(eventdata);
    DBGT_PROLOG("");
    DBGT_ASSERT(comp);
    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_U32 i;

    if (VERBOSE_OUTPUT)
    {
        printf("Got component event: event:%s data1:%u data2:%u\n",
               HantroOmx_str_omx_event(event),
               (unsigned)nData1,
               (unsigned)nData2);
    }

    switch (event)
    {
#if 1
        case OMX_EventPortSettingsChanged:
            {
                OMX_U32 portIndex = nData1;
                OMX_PARAM_PORTDEFINITIONTYPE port;
                INIT_OMX_TYPE(port);
                port.nPortIndex = portIndex;

                /* Check output crop info */
                if ((portIndex == 1) && (nData2 == OMX_IndexConfigCommonOutputCrop))
                {
                    OMX_CONFIG_RECTTYPE crop;
                    INIT_OMX_TYPE(crop);
                    crop.nPortIndex = portIndex;

                    err = ((OMX_COMPONENTTYPE*)comp)->GetConfig(comp, OMX_IndexConfigCommonOutputCrop, &crop);
                    DBGT_ASSERT(err == OMX_ErrorNone);

                    printf("New crop info\n");
                    printf("Left %ld, Top %ld, Width %lu, Height %lu\n", crop.nLeft, crop.nTop,
                                                                    crop.nWidth, crop.nHeight);
                    return err;
                }
                err = ((OMX_COMPONENTTYPE*)comp)->GetParameter(comp, OMX_IndexParamPortDefinition, &port);
                DBGT_ASSERT(err == OMX_ErrorNone);
                if (VERBOSE_OUTPUT)
                {
                    printf("Port settings changed, port: %d\n", (int)portIndex);
                    printf("New port settings:\n"
                           "  nBufferCountActual:%d\n"
                           "  nBufferCountMin:%d\n"
                           "  nBufferSize:%d\n"
                           "  nFrameWidth:%d\n"
                           "  nFrameHeight:%d\n"
                           "  nStride:%d\n"
                           "  nSliceHeight:%d\n"
                           "  colorformat:%s\n",
                           (int)port.nBufferCountActual,
                           (int)port.nBufferCountMin,
                           (int)port.nBufferSize,
                           (int)port.format.video.nFrameWidth,
                           (int)port.format.video.nFrameHeight,
                           (int)port.format.video.nStride,
                           (int)port.format.video.nSliceHeight,
                           HantroOmx_str_omx_color(port.format.video.eColorFormat));
                }

                vid_props->displaywidth  = port.format.video.nFrameWidth;
                vid_props->displayheight = port.format.video.nFrameHeight;
                vid_props->stride        = port.format.video.nStride;
                vid_props->sliceheight   = port.format.video.nSliceHeight;

                reconfigPending = OMX_TRUE;

                /* disable port */
                err = ((OMX_COMPONENTTYPE*)comp)->SendCommand(comp, OMX_CommandPortDisable, port.nPortIndex, NULL);
                if(err != OMX_ErrorNone)
                {
                    return err;
                }

                /* Free held buffers */
                OMX_BUFFERHEADERTYPE* output = NULL;
                OMX_ERRORTYPE err = OMX_ErrorNone;
                do
                {
                    OSAL_MutexLock(queue_mutex);
                    get_header(&output_queue, &output);
                    OSAL_MutexUnlock(queue_mutex);
                    if (output)
                    {
                        output->nOutputPortIndex = 1;
#ifdef CLIENT_ALLOC
                        OMX_U32 i;
                        for (i = 0; i < noReallocBufList.count; i++)
                        {
                            if (noReallocBufList.address[i] == output->pBuffer)
                                break;
                        }

                        if (output->pBuffer && i == noReallocBufList.count) free(output->pBuffer);

#endif
                        if (output)
                            ((OMX_COMPONENTTYPE*)comp)->FreeBuffer(comp, 1, output);
                            if (err != OMX_ErrorNone)
                            {
                                printf("FreeBuffer err 0x%x\n", err);
                                return err;
                            }
                    }
                }
                while (output);

            }
            break;
#endif
        case OMX_EventCmdComplete:
            {
                OMX_U32 cmd = nData1;
                printf("Got component event: event:%s cmd:%s\n",
                       HantroOmx_str_omx_event(event),
                       HantroOmx_str_omx_cmd(cmd));

                switch (cmd)
                {
                    case OMX_CommandPortDisable:
                    {
                        OMX_PARAM_PORTDEFINITIONTYPE port;
                        INIT_OMX_TYPE(port);
                        port.nPortIndex = nData2;

                        printf("Port disable complete, port: %d\n", (int)port.nPortIndex);
#if 1
                        if (reconfigPending)
                        {
                            printf("Enable port: %d\n", (int)nData2);

                            // get the expected buffer sizes
                            OMX_PARAM_PORTDEFINITIONTYPE port;
                            INIT_OMX_TYPE(port);
                            port.nPortIndex = nData2; // output port

                            ((OMX_COMPONENTTYPE*)comp)->GetParameter(comp, OMX_IndexParamPortDefinition, &port);

                            if(!port.nBufferSize)
                            {
                                port.nBufferSize = DEFAULT_BUFFER_SIZE_OUTPUT;
                                DBGT_ERROR("Output port size is not set!");
                                DBGT_ERROR("Using default size: %d", (int)DEFAULT_BUFFER_SIZE_OUTPUT);
                            }

                            port.nBufferCountActual = port.nBufferCountMin + EXTRA_BUFFERS;
                            ((OMX_COMPONENTTYPE*)comp)->SetParameter(comp, OMX_IndexParamPortDefinition, &port);

                            init_list(&dst_queue);
                            init_list(&output_queue);

                            /* Check if buffer size is changed */
                            ((OMX_COMPONENTTYPE*)comp)->GetParameter(comp, OMX_IndexParamPortDefinition, &port);

                            for (i=0; i<port.nBufferCountActual; ++i)
                            {
                                OMX_U32 nBufferSize = port.nBufferSize > MIN_REF_SIZE ? port.nBufferSize : MIN_REF_SIZE;
                                OMX_BUFFERHEADERTYPE* header = NULL;
                                void* mem = NULL;
#ifdef CLIENT_ALLOC
                                if(noReallocBufList.count)
                                {
                                    mem = noReallocBufList.address[noReallocBufList.count - 1];
                                    nBufferSize = noReallocBufList.size[noReallocBufList.count - 1];
                                    noReallocBufList.count--;
                                }
                                else
                                {
                                    printf("Allocating output buffer size %d\n", (int)nBufferSize);
                                    mem = memalign(16, nBufferSize);
                                    DBGT_ASSERT(mem);
                                }

                                err = ((OMX_COMPONENTTYPE*)comp)->UseBuffer(comp, &header, port.nPortIndex, NULL, nBufferSize, mem);

                                // ALLOC_PRIVATE struct is accessed through pInputPortPrivate
                                fill_alloc_private(&bufferPrivOutput[i], mem, (OMX_U64)mem, nBufferSize);
                                printf("bufPrivateOutput.nBusAddress %llu\n", bufferPrivOutput[i].nBusAddress);
                                header->pInputPortPrivate = &bufferPrivOutput[i];
#else
                                err = ((OMX_COMPONENTTYPE*)comp)->AllocateBuffer(comp, &header, port.nPortIndex, NULL, port.nBufferSize);

                                if (header->pInputPortPrivate)
                                {
                                    ALLOC_PRIVATE *bufPrivate = (ALLOC_PRIVATE*) header->pInputPortPrivate;
                                    fill_alloc_private(&bufferPrivOutput[i], bufPrivate->pBufferData, bufPrivate->nBusAddress, bufPrivate->nBufferSize);
                                    printf("bufPrivateOutput %p / %llu\n", bufferPrivOutput[i].pBufferData, bufferPrivOutput[i].nBusAddress);
                                }
#endif
                                if (err != OMX_ErrorNone)
                                {
                                    if (mem) free(mem);
                                    printf("Allocating output buffer failed, err 0x%x\n", err);
                                    ERROR = OMX_TRUE;
                                    return err;
                                }
                                DBGT_ASSERT(header);
                                OSAL_MutexLock(queue_mutex);
                                push_header(&dst_queue, header);
                                push_header(&output_queue, header);
                                OSAL_MutexUnlock(queue_mutex);
                            }

                            /* enable port */
                            err = ((OMX_COMPONENTTYPE*)comp)->SendCommand(comp, OMX_CommandPortEnable, port.nPortIndex, NULL);
                            if(err != OMX_ErrorNone)
                            {
                                return err;
                            }
                        }
#endif
                    }

                    break;
                    case OMX_CommandPortEnable:
                    {
                        OMX_U32 portIndex = nData2;

                        printf("Port enable completed, port: %d\n", (int)portIndex);
                        OMX_BUFFERHEADERTYPE* output;
#if 0
                        do
                        {
                            output = NULL;
                            OSAL_MutexLock(queue_mutex);
                            //get_header(&dst_queue, &output);
                            get_header(&output_queue, &output);
                            OSAL_MutexUnlock(queue_mutex);
                            if (output)
                            {
                                output->nOutputPortIndex = 1;
                                err = ((OMX_COMPONENTTYPE*)comp)->FillThisBuffer(comp, output);
                                if (err != OMX_ErrorNone)
                                {
                                    return err;
                                }
                            }
                        }
                        while (output);
#else
                        OMX_U32 i;
                        OMX_PARAM_PORTDEFINITIONTYPE port;
                        INIT_OMX_TYPE(port);
                        port.nPortIndex = nData2; // output port
#if 0
                        ((OMX_COMPONENTTYPE*)comp)->GetParameter(comp, OMX_IndexParamPortDefinition, &port);
                        for (i=0; i<port.nBufferCountMin; ++i)
                        {
                            OSAL_MutexLock(queue_mutex);
                            get_header(&output_queue, &output);
                            OSAL_MutexUnlock(queue_mutex);
                            if (output)
                            {
                                OMX_U32 j;
                                // Find correct ALLOC_PRIVATE struct and set the pInputPortPrivate pointer
                                for (j=0; j<MAX_BUFFER_COUNT; j++)
                                {
                                    if (bufferPrivOutput[j].pBufferData == output->pBuffer)
                                    {
                                        output->pInputPortPrivate = &bufferPrivOutput[j];
                                        break;
                                    }
                                }
                                output->nOutputPortIndex = 1;
                                err = ((OMX_COMPONENTTYPE*)comp)->FillThisBuffer(comp, output);
                                if (err != OMX_ErrorNone)
                                {
                                    return err;
                                }
                            }
                        }
#endif
                        reconfigPending = OMX_FALSE;
                        waitingBuffers = OMX_TRUE;
#endif

                    }
                    break;
                    case OMX_CommandStateSet:
                    {
                        printf("State set to: %s\n", HantroOmx_str_omx_state(nData2));
                        OMX_STATETYPE state;
                        ((OMX_COMPONENTTYPE*)comp)->GetState(comp, &state);
                        printf("Now in state: %s\n", HantroOmx_str_omx_state(state));
                    }
                    break;
                    default:
                        printf("Got command %s\n", HantroOmx_str_omx_cmd(cmd));
                    break;
                }
            }
            break;
        case OMX_EventBufferFlag:
            {
                printf("EventBufferFlag received. Output port index %lu, nFlags %lu\n", nData1, nData2);
                EOS = OMX_TRUE;
            }
            break;

        case OMX_EventError:
            {
                DBGT_CRITICAL("\tError: %s", HantroOmx_str_omx_err((OMX_ERRORTYPE)nData1));
                ERROR = OMX_TRUE;
            }
            break;

        default:
            printf("Got component event: %s\n", HantroOmx_str_omx_event(event));
            break;

    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

OMX_COMPONENTTYPE* create_decoder(int codec, OMX_STRING name)
{
    DBGT_PROLOG("");

    OMX_ERRORTYPE err = OMX_ErrorBadParameter;
    OMX_COMPONENTTYPE* comp = (OMX_COMPONENTTYPE*)malloc(sizeof(OMX_COMPONENTTYPE));
    if (!comp)
    {
        err = OMX_ErrorInsufficientResources;
        goto FAIL;
    }
    memset(comp, 0, sizeof(OMX_COMPONENTTYPE));

    // somewhat hackish method for creating the components...
    OMX_ERRORTYPE HantroHwDecOmx_video_constructor(OMX_COMPONENTTYPE*, OMX_STRING);
#ifndef VIDEO_ONLY
    OMX_ERRORTYPE HantroHwDecOmx_image_constructor(OMX_COMPONENTTYPE*, OMX_STRING);
#endif
    if (codec == CODEC_IMAGE)
    {
#ifndef VIDEO_ONLY
        err = HantroHwDecOmx_image_constructor(comp, name);
#endif
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    else
    {
        err = HantroHwDecOmx_video_constructor(comp, name);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }

    OMX_CALLBACKTYPE cb;
    cb.EventHandler    = comp_event;
    cb.EmptyBufferDone = empty_buffer_done;
    cb.FillBufferDone  = fill_buffer_done;

    err = comp->SetCallbacks(comp, &cb, NULL);
    if (err != OMX_ErrorNone)
        goto FAIL;

    DBGT_EPILOG("");
    return comp;
 FAIL:
    if (comp)
        free(comp);
    DBGT_CRITICAL("error: %s\n", HantroOmx_str_omx_err(err));
    DBGT_EPILOG("");
    return NULL;
}

OMX_ERRORTYPE setup_component(OMX_COMPONENTTYPE* comp, const TESTARGS* args)
{
    DBGT_PROLOG("");
    DBGT_ASSERT(args);
    DBGT_ASSERT(comp);

    OMX_ERRORTYPE err;
    // get the expected buffer sizes
    OMX_PARAM_PORTDEFINITIONTYPE src;
    OMX_PARAM_PORTDEFINITIONTYPE dst;
    OMX_PARAM_PORTDEFINITIONTYPE pp;
    INIT_OMX_TYPE(src);
    INIT_OMX_TYPE(dst);
    INIT_OMX_TYPE(pp);
    src.nPortIndex = 0; // input port
    dst.nPortIndex = 1; // output port
    pp.nPortIndex  = 2; // post-processor input

    comp->GetParameter(comp, OMX_IndexParamPortDefinition, &src);
    comp->GetParameter(comp, OMX_IndexParamPortDefinition, &dst);
    comp->GetParameter(comp, OMX_IndexParamPortDefinition, &pp);

    // set buffers and input video format
    src.nBufferCountActual = INPUT_BUFFER_COUNT;
    if (args->domain == CODEC_IMAGE)
    {
        src.format.image.eCompressionFormat = args->coding_type;
        src.format.image.eColorFormat       = args->input_format;
        src.format.image.nFrameWidth        = args->input_width;
        src.format.image.nFrameHeight       = args->input_height;
    }
    else
    {
        src.format.video.eCompressionFormat = args->coding_type;
        src.format.video.eColorFormat       = args->input_format;
        src.format.video.nFrameWidth        = args->input_width;
        src.format.video.nFrameHeight       = args->input_height;
    }

    if(src.format.video.nFrameWidth < 48 || src.format.video.nFrameHeight < 48)
    {
        DBGT_CRITICAL("Too small input buffer dimensions (%dx%d)",
            (int)src.format.video.nFrameWidth, (int)src.format.video.nFrameHeight);
        err = OMX_ErrorBadParameter;
        goto FAIL;
    }

    printf("Input buffer width x height (%dx%d)\n", (int)src.format.video.nFrameWidth,
            (int)src.format.video.nFrameHeight);
    err = comp->SetParameter(comp, OMX_IndexParamPortDefinition, &src);
    if (err != OMX_ErrorNone)
        goto FAIL;

    dst.nBufferCountActual = dst.nBufferCountMin + EXTRA_BUFFERS;
    if (args->output_format)
    {
        // setup color conversion
        if (args->domain == CODEC_IMAGE)
            dst.format.image.eColorFormat = args->output_format;
        else
            dst.format.video.eColorFormat = args->output_format;
    }
    if (args->width && args->height)
    {
        // setup scaling
        if (args->domain == CODEC_IMAGE)
        {
            dst.format.image.nFrameWidth  = args->width;
            dst.format.image.nFrameHeight = args->height;
        }
        else
        {
            dst.format.video.nFrameWidth  = args->width;
            dst.format.video.nFrameHeight = args->height;
        }
    }
    err = comp->SetParameter(comp, OMX_IndexParamPortDefinition, &dst);
    if (err != OMX_ErrorNone)
        goto FAIL;

    pp.format.image.nFrameWidth  = args->mask1.width;
    pp.format.image.nFrameHeight = args->mask1.height;
    err = comp->SetParameter(comp, OMX_IndexParamPortDefinition, &pp);

    if (err != OMX_ErrorNone)
        goto FAIL;

    if (args->rotation)
    {
        // setup rotation
        OMX_CONFIG_ROTATIONTYPE rot;
        INIT_OMX_TYPE(rot);
        rot.nRotation = args->rotation;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonRotate, &rot);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    if (args->mirroring)
    {
        OMX_CONFIG_MIRRORTYPE mir;
        INIT_OMX_TYPE(mir);
        if (args->mirroring == 1)
            mir.eMirror = OMX_MirrorVertical;
        if (args->mirroring == 2)
            mir.eMirror = OMX_MirrorHorizontal;

        err = comp->SetConfig(comp, OMX_IndexConfigCommonMirror, &mir);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    if (args->dithering)
    {
        OMX_CONFIG_DITHERTYPE dither;
        INIT_OMX_TYPE(dither);
        dither.eDither = OMX_DitherOther;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonDithering, &dither);
        if(err != OMX_ErrorNone)
           goto FAIL;
    }
    if (args->deblocking)
    {
       OMX_PARAM_DEBLOCKINGTYPE deblock;
       INIT_OMX_TYPE(deblock);
       deblock.bDeblocking = 1;
       err = comp->SetParameter(comp, OMX_IndexParamCommonDeblocking, &deblock);
       if(err != OMX_ErrorNone)
           goto FAIL;
    }

    if (args->crop_width && args->crop_height)
    {
        // setup cropping
        OMX_CONFIG_RECTTYPE rect;
        INIT_OMX_TYPE(rect);
        rect.nLeft   = args->crop_x;
        rect.nTop    = args->crop_y;
        rect.nWidth  = args->crop_width;
        rect.nHeight = args->crop_height;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonInputCrop, &rect);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    if (args->rgb.alpha)
    {
        // setup fixed alpha value for RGB conversion
        OMX_CONFIG_PLANEBLENDTYPE blend;
        INIT_OMX_TYPE(blend);
        blend.nAlpha = args->rgb.alpha;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonPlaneBlend, &blend);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    if (args->rgb.contrast)
    {
        // setup contrast for RGB conversion
        OMX_CONFIG_CONTRASTTYPE ctr;
        INIT_OMX_TYPE(ctr);
        ctr.nContrast = args->rgb.contrast;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonContrast, &ctr);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }

    if (args->rgb.brightness)
    {
        // setup brightness for RGB conversion
        OMX_CONFIG_BRIGHTNESSTYPE brig;
        INIT_OMX_TYPE(brig);
        brig.nBrightness = args->rgb.brightness;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonBrightness, &brig);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }

    if (args->rgb.saturation)
    {
        // setup saturation for RGB conversion
        OMX_CONFIG_SATURATIONTYPE satur;
        INIT_OMX_TYPE(satur);
        satur.nSaturation = args->rgb.saturation;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonSaturation, &satur);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    if (args->mask1.width && args->mask1.height)
    {
        // setup blending mask1
        pp.format.image.nFrameWidth  = args->mask1.width;
        pp.format.image.nFrameHeight = args->mask1.height;
        err = comp->SetParameter(comp, OMX_IndexParamPortDefinition, &pp);
        if (err != OMX_ErrorNone)
            goto FAIL;

        OMX_CONFIG_POINTTYPE point;
        INIT_OMX_TYPE(point);
        point.nX = args->mask1.originX;
        point.nY = args->mask1.originY;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonOutputPosition, &point);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }

    if (args->mask2.width && args->mask2.height)
    {
        // setup non-blending (mask2)
        OMX_CONFIG_RECTTYPE rect;
        INIT_OMX_TYPE(rect);
        rect.nLeft   = args->mask2.originX;
        rect.nTop    = args->mask2.originY;
        rect.nWidth  = args->mask2.width;
        rect.nHeight = args->mask2.height;
        err = comp->SetConfig(comp, OMX_IndexConfigCommonExclusionRect, &rect);
        if (err != OMX_ErrorNone)
            goto FAIL;

    }

#ifdef ENABLE_CODEC_RV
    if (args->coding_type == OMX_VIDEO_CodingRV)
    {
        INIT_OMX_TYPE(rv);

        if (rawRVFile == OMX_TRUE)
        {
            rv.nMaxEncodeFrameSize = 0;
            rv.nPaddedWidth = 0;
            rv.nPaddedHeight = 0;
        }
        else
        {
            rv.nMaxEncodeFrameSize = pctszSize;
            rv.nPaddedWidth = maxCodedWidth;
            rv.nPaddedHeight = maxCodedHeight;
        }
        if (bIsRV8 || rawRV8 == OMX_TRUE)
            rv.eFormat = OMX_VIDEO_RVFormat8;
        else
            rv.eFormat = OMX_VIDEO_RVFormat9;

        err = comp->SetParameter(comp, OMX_IndexParamVideoRv, &rv);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
#endif

    if (args->mvc_stream)
    {
        OMX_VIDEO_PARAM_MVCSTREAMTYPE mvc;
        INIT_OMX_TYPE(mvc);
        mvc.bIsMVCStream = OMX_TRUE;
        err = comp->SetParameter(comp, OMX_IndexParamVideoMvcStream, &mvc);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }

    if (args->tiled)
    {
        if((args->coding_type == OMX_VIDEO_CodingHEVC) || (args->coding_type == OMX_VIDEO_CodingVP9))
        {
            OMX_VIDEO_PARAM_G2CONFIGTYPE g2conf;
            INIT_OMX_TYPE(g2conf);
            g2conf.nPortIndex = 1; // output port
            comp->GetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
            g2conf.bEnableTiled = OMX_TRUE;
            err = comp->SetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
            if (err != OMX_ErrorNone)
                goto FAIL;
        }
        else
        {
            OMX_VIDEO_PARAM_G1CONFIGTYPE g1conf;
            INIT_OMX_TYPE(g1conf);
            g1conf.nPortIndex = 1; // output port
            comp->GetParameter(comp, OMX_IndexParamVideoG1Config, &g1conf);
            g1conf.bEnableTiled = OMX_TRUE;
            g1conf.bAllowFieldDBP = OMX_TRUE;
            err = comp->SetParameter(comp, OMX_IndexParamVideoG1Config, &g1conf);
            if (err != OMX_ErrorNone)
                goto FAIL;
        }
    }

    if (args->pixFormat != 1)
    {
        OMX_VIDEO_PARAM_G2CONFIGTYPE g2conf;
        INIT_OMX_TYPE(g2conf);
        g2conf.nPortIndex = 1; // output port
        comp->GetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
        g2conf.ePixelFormat = args->pixFormat;
        err = comp->SetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }

    if (args->rfc)
    {
        OMX_VIDEO_PARAM_G2CONFIGTYPE g2conf;
        INIT_OMX_TYPE(g2conf);
        g2conf.nPortIndex = 1; // output port
        comp->GetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
        g2conf.bEnableRFC = OMX_TRUE;
        err = comp->SetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }

    if (args->secure)
    {
        if((args->coding_type == OMX_VIDEO_CodingHEVC) || (args->coding_type == OMX_VIDEO_CodingVP9))
        {
            OMX_VIDEO_PARAM_G2CONFIGTYPE g2conf;
            INIT_OMX_TYPE(g2conf);
            g2conf.nPortIndex = 1; // output port
            comp->GetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
            g2conf.bEnableSecureMode = OMX_TRUE;
            err = comp->SetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
            if (err != OMX_ErrorNone)
                goto FAIL;
        }
        else
        {
            OMX_VIDEO_PARAM_G1CONFIGTYPE g1conf;
            INIT_OMX_TYPE(g1conf);
            g1conf.nPortIndex = 1; // output port
            comp->GetParameter(comp, OMX_IndexParamVideoG1Config, &g1conf);
            g1conf.bEnableSecureMode = OMX_TRUE;
            err = comp->SetParameter(comp, OMX_IndexParamVideoG1Config, &g1conf);
            if (err != OMX_ErrorNone)
                goto FAIL;
        }
    }

    if (args->adaptive)
    {
        if((args->coding_type == OMX_VIDEO_CodingHEVC) || (args->coding_type == OMX_VIDEO_CodingVP9))
        {
            OMX_VIDEO_PARAM_G2CONFIGTYPE g2conf;
            INIT_OMX_TYPE(g2conf);
            g2conf.nPortIndex = 1; // output port
            comp->GetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
            g2conf.bEnableAdaptiveBuffers = OMX_TRUE;
            g2conf.nGuardSize = args->guard_buffers;
            err = comp->SetParameter(comp, OMX_IndexParamVideoG2Config, &g2conf);
            if (err != OMX_ErrorNone)
                goto FAIL;
        }
        else
        {
            OMX_VIDEO_PARAM_G1CONFIGTYPE g1conf;
            INIT_OMX_TYPE(g1conf);
            g1conf.nPortIndex = 1; // output port
            comp->GetParameter(comp, OMX_IndexParamVideoG1Config, &g1conf);
            g1conf.bEnableAdaptiveBuffers = OMX_TRUE;
            g1conf.nGuardSize = args->guard_buffers;
            err = comp->SetParameter(comp, OMX_IndexParamVideoG1Config, &g1conf);
            if (err != OMX_ErrorNone)
                goto FAIL;
        }
    }

    // create the buffers
    err = comp->SendCommand(comp, OMX_CommandStateSet, OMX_StateIdle, NULL);
    if (err != OMX_ErrorNone)
        goto FAIL;

    // create the post-proc input buffer
    alpha_mask_buffer = NULL;
    int alpha_blend_buffer_size = args->mask1.width * args->mask1.height * 4; // 32bit ARGB
    if (alpha_blend_buffer_size == 0)
        alpha_blend_buffer_size = 4;
    err = comp->AllocateBuffer(comp, &alpha_mask_buffer, 2, NULL, alpha_blend_buffer_size);
    if (err != OMX_ErrorNone)
        goto FAIL;

    DBGT_ASSERT(alpha_mask_buffer);
    DBGT_ASSERT(args->buffer_size_in);
    DBGT_ASSERT(args->buffer_size_out);

    OMX_U32 i = 0;
/* Input buffers */
    for (i=0; i<src.nBufferCountActual; ++i)
    {
        OMX_BUFFERHEADERTYPE* header = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE in;
        INIT_OMX_TYPE(in);
        in.nPortIndex = 0; // input port

#ifdef COMPONENT_SETS_PORT_BUFFER_SIZE
        // get the expected buffer sizes
        comp->GetParameter(comp, OMX_IndexParamPortDefinition, &in);
        if(!in.nBufferSize)
        {
            in.nBufferSize = args->buffer_size_in;
            DBGT_ERROR("Input port size is not set!");
            DBGT_ERROR("Using default size: %d", args->buffer_size_in);
        }
#else
        in.nBufferSize = args->buffer_size_in;
#endif  // COMPONENT_SETS_PORT_BUFFER_SIZE

#ifdef CLIENT_ALLOC
        printf("Allocating input buffer size %d\n", (int)in.nBufferSize);
        void* mem = memalign(16, in.nBufferSize);
        DBGT_ASSERT(mem);
        err = comp->UseBuffer(comp, &header, in.nPortIndex, NULL, in.nBufferSize, mem);

        // ALLOC_PRIVATE struct is accessed through pInputPortPrivate
        fill_alloc_private(&bufferPrivInput[i], mem, (OMX_U64)mem, in.nBufferSize);
        printf("bufPrivateInput %p / %llu\n", bufferPrivInput[i].pBufferData, bufferPrivInput[i].nBusAddress);
        header->pInputPortPrivate = &bufferPrivInput[i];
#else
        err = comp->AllocateBuffer(comp, &header, in.nPortIndex, NULL, in.nBufferSize);

        if (header->pInputPortPrivate)
        {
            ALLOC_PRIVATE *bufPrivate = (ALLOC_PRIVATE*) header->pInputPortPrivate;
            fill_alloc_private(&bufferPrivInput[i], bufPrivate->pBufferData, bufPrivate->nBusAddress, bufPrivate->nBufferSize);
            printf("bufPrivateInput %p / %llu\n", bufferPrivInput[i].pBufferData, bufferPrivInput[i].nBusAddress);
        }
#endif // CLIENT_ALLOC
        if (err != OMX_ErrorNone)
            goto FAIL;
        DBGT_ASSERT(header);
        OSAL_MutexLock(queue_mutex);
        push_header(&src_queue, header);
        push_header(&input_queue, header);
        OSAL_MutexUnlock(queue_mutex);
    }
/* Output buffers */
    for (i=0; i<dst.nBufferCountActual; ++i)
    {
        OMX_BUFFERHEADERTYPE* header = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE out;
        INIT_OMX_TYPE(out);
        out.nPortIndex = 1; // output port

#ifdef COMPONENT_SETS_PORT_BUFFER_SIZE
        // get the expected buffer sizes
        comp->GetParameter(comp, OMX_IndexParamPortDefinition, &out);
        if(!out.nBufferSize)
        {
            out.nBufferSize = args->buffer_size_out;
            DBGT_ERROR("Output port size is not set!");
            DBGT_ERROR("Using default size: %d", args->buffer_size_out);
        }
#else
        out.nBufferSize = args->buffer_size_out;
#endif  // COMPONENT_SETS_PORT_BUFFER_SIZE

#ifdef CLIENT_ALLOC
        printf("Allocating output buffer size %d\n", (int)out.nBufferSize);
        void* mem = memalign(16, out.nBufferSize);
        DBGT_ASSERT(mem);
        err = ((OMX_COMPONENTTYPE*)comp)->UseBuffer(comp, &header, out.nPortIndex, NULL, out.nBufferSize, mem);

        // ALLOC_PRIVATE struct is accessed through pInputPortPrivate
        fill_alloc_private(&bufferPrivOutput[i], mem, (OMX_U64)mem, out.nBufferSize);
        printf("bufPrivateOutput %p / %llu\n", bufferPrivOutput[i].pBufferData, bufferPrivOutput[i].nBusAddress);
        header->pInputPortPrivate = &bufferPrivOutput[i];
#else
        err = comp->AllocateBuffer(comp, &header, out.nPortIndex, NULL, out.nBufferSize);

        if (header->pInputPortPrivate)
        {
            ALLOC_PRIVATE *bufPrivate = (ALLOC_PRIVATE*) header->pInputPortPrivate;
            fill_alloc_private(&bufferPrivOutput[i], bufPrivate->pBufferData, bufPrivate->nBusAddress, bufPrivate->nBufferSize);
            printf("bufPrivateOutput %p / %llu\n", bufferPrivOutput[i].pBufferData, bufferPrivOutput[i].nBusAddress);
        }
#endif // CLIENT_ALLOC
        if (err != OMX_ErrorNone)
            goto FAIL;
        DBGT_ASSERT(header);
        OSAL_MutexLock(queue_mutex);
        push_header(&dst_queue, header);
        push_header(&output_queue, header);
        OSAL_MutexUnlock(queue_mutex);
    }

    // should have transitioned to idle state now

    OMX_STATETYPE state = OMX_StateLoaded;
    while (state != OMX_StateIdle && state != OMX_StateInvalid)
    {
        err = comp->GetState(comp, &state);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    if (state == OMX_StateInvalid)
        goto FAIL;

    err = comp->SendCommand(comp, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    if (err != OMX_ErrorNone)
        goto FAIL;

    while (state != OMX_StateExecuting && state != OMX_StateInvalid)
    {
        err = comp->GetState(comp, &state);
        if (err != OMX_ErrorNone)
            goto FAIL;
    }
    if (state == OMX_StateInvalid)
        goto FAIL;

    DBGT_EPILOG("");
    return OMX_ErrorNone;
 FAIL:
    // todo: should deallocate buffers and stuff
    DBGT_CRITICAL("error: %s\n", HantroOmx_str_omx_err(err));
    DBGT_EPILOG("");
    return err;
}


void destroy_decoder(OMX_COMPONENTTYPE* comp)
{
    DBGT_PROLOG("");

    OMX_ERRORTYPE err;
    OMX_U32 i;
    OMX_STATETYPE state = OMX_StateExecuting;

    comp->GetState(comp, &state);
    if (state == OMX_StateExecuting)
    {
        comp->SendCommand(comp, OMX_CommandStateSet, OMX_StateIdle, NULL);
        while (state != OMX_StateIdle)
            comp->GetState(comp, &state);
    }

    if (state == OMX_StateIdle)
        comp->SendCommand(comp, OMX_CommandStateSet, OMX_StateLoaded, NULL);

    if (alpha_mask_buffer)
        comp->FreeBuffer(comp, 2, alpha_mask_buffer);

    for (i=0; i<INPUT_BUFFER_COUNT; ++i)
    {
        OMX_BUFFERHEADERTYPE* src_hdr = NULL;
        OSAL_MutexLock(queue_mutex);
        get_header(&src_queue, &src_hdr);
        OSAL_MutexUnlock(queue_mutex);

#ifdef CLIENT_ALLOC
        if (src_hdr)
            if (src_hdr->pBuffer) free(src_hdr->pBuffer);
#endif
        if (src_hdr)
            comp->FreeBuffer(comp, 0, src_hdr);
    }

    OMX_PARAM_PORTDEFINITIONTYPE port;
    INIT_OMX_TYPE(port);
    port.nPortIndex = 1;

    err = ((OMX_COMPONENTTYPE*)comp)->GetParameter(comp, OMX_IndexParamPortDefinition, &port);

    for (i=0; i<port.nBufferCountActual; ++i)
    {
        OMX_BUFFERHEADERTYPE* dst_hdr = NULL;
        OSAL_MutexLock(queue_mutex);
        get_header(&dst_queue, &dst_hdr);
        OSAL_MutexUnlock(queue_mutex);

#ifdef CLIENT_ALLOC
        if (dst_hdr)
            if (dst_hdr->pBuffer) free(dst_hdr->pBuffer);
#endif

        if (dst_hdr)
            comp->FreeBuffer(comp, 1, dst_hdr);
    }

    while (state != OMX_StateLoaded && state != OMX_StateInvalid)
        comp->GetState(comp, &state);

    comp->ComponentDeInit(comp);
    free(comp);
    DBGT_EPILOG("");
}

int decode_file(const char* input_file, const char* yuv_file,  VIDEOPROPS* props, const TESTARGS* args, read_func rf, void* readstate)
{
    DBGT_PROLOG("");

    init_list(&src_queue);
    init_list(&dst_queue);
    init_list(&input_queue);
    init_list(&output_queue);
    init_list(&input_ret_queue);
    init_list(&output_ret_queue);
    memset(props, 0, sizeof(VIDEOPROPS));
    vid_out = NULL;
    vid_props = props;
    int ret = 0;
    OMX_BOOL eof = OMX_FALSE;
    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_COMPONENTTYPE* dec = NULL;
    FILE* video = NULL;
    FILE* yuv   = NULL;
    FILE* mask  = NULL;

#ifdef ENABLE_CODEC_RV
    void *thread_ret;
#endif

    OMX_VIDEO_VP8REFERENCEFRAMEINFOTYPE Vp8RefType;
    INIT_OMX_TYPE(Vp8RefType);
    Vp8RefType.nPortIndex = 1;

    vid_props->framelimit = args->limit;

    if (input_file)
    {
        video = fopen(input_file, "rb");
        if (video == NULL)
        {
            DBGT_CRITICAL("Failed to open input file:%s", input_file);
            ret = -1;
            goto FAIL;
        }
    }
    else
    {
        DBGT_CRITICAL("Input filename is NULL");
        ret = -1;
        goto FAIL;
    }

    if (yuv_file)
    {
        yuv = fopen(yuv_file, "wb");
        if (yuv == NULL)
        {
            DBGT_CRITICAL("Failed to open output file");
            ret = -1;
            goto FAIL;
        }
    }

#ifdef ENABLE_CODEC_RV
    if (args->coding_type == OMX_VIDEO_CodingRV)
    {
        /* Real fileformat thread is started here */
        /* Start Real file format thread */
        OMX_U32** ppStreamBuffer = NULL;
        OMX_U32* pAllocLen = NULL;
        BYTE ucBuf[RM2YUV_INITIAL_READ_SIZE];
        OMX_S32 lBytesRead = 0;

        /* Read the first few bytes of the file */
        lBytesRead = (OMX_S32) fread((void*) ucBuf, 1, RM2YUV_INITIAL_READ_SIZE, video);
        if (lBytesRead != RM2YUV_INITIAL_READ_SIZE)
        {
            DBGT_CRITICAL("Could not read %d bytes at the beginning of %s",
            RM2YUV_INITIAL_READ_SIZE, input_file);
            ret = -1;
            goto FAIL;
        }
        /* Seek back to the beginning */
        fseek(video, 0, SEEK_SET);

        if (!rm_parser_is_rm_file(ucBuf, RM2YUV_INITIAL_READ_SIZE))
        {
            if(ucBuf[0] == 0 && ucBuf[1] == 0 && ucBuf[2] == 1)
                rawRV8 = OMX_TRUE;
            else
                rawRV8 = OMX_FALSE;

            rawRVFile = OMX_TRUE;

            printf("Raw RV file, RV8 %d\n", rawRV8);
        }
        else
        {
            pthread_mutex_init(&buff_mx, NULL);
            pthread_cond_init(&fillbuffer, NULL);

            startRealFileReader(video, ppStreamBuffer, pAllocLen, &eof);

            pthread_mutex_lock(&buff_mx);

            if(!headers_ready)
            {
                /* wait for signal */
               pthread_cond_wait(&fillbuffer, &buff_mx);
            }
            /* unlock mutex */
            pthread_mutex_unlock(&buff_mx);
        }
    }
#endif

    dec = create_decoder(args->domain, args->name);
    if (!dec)
    {
        ret = -1;
        goto FAIL;
    }

    if (setup_component(dec, args) != OMX_ErrorNone)
    {
        ret = -1;
        goto FAIL;
    }

    vid_out = yuv;

    if (args->blend_input)
    {
        // send blend input to the decoder
        DBGT_ASSERT(alpha_mask_buffer);
        mask = fopen(args->blend_input, "rb");
        if (mask == NULL)
        {
            DBGT_CRITICAL("Failed to open mask file: %s", args->blend_input);
            ret = -1;
            goto FAIL;
        }
        // read the whole thing into the buffer
        fseek(mask, 0, SEEK_END);
        int size = ftell(mask);
        fseek(mask, 0, SEEK_SET);
        if (size > (int)alpha_mask_buffer->nAllocLen)
        {
            DBGT_CRITICAL("Alpha mask data size greater than buffer size. Mask ignored");
        }
        else
        {
            fread(alpha_mask_buffer->pBuffer, 1, size, mask);
            alpha_mask_buffer->nInputPortIndex = 2;
            alpha_mask_buffer->nOffset         = 0;
            alpha_mask_buffer->nFilledLen      = size;
            err = dec->EmptyThisBuffer(dec, alpha_mask_buffer);
            if (err != OMX_ErrorNone)
            {
                ret = -1;
                goto FAIL;
            }
        }
    }

    OMX_PARAM_PORTDEFINITIONTYPE port;
    INIT_OMX_TYPE(port);
    port.nPortIndex = 1;

    err = ((OMX_COMPONENTTYPE*)dec)->GetParameter(dec, OMX_IndexParamPortDefinition, &port);
    OMX_BUFFERHEADERTYPE* output = NULL;
#if 0
    do
    {
        OSAL_MutexLock(queue_mutex);
        get_header(&output_queue, &output);
        OSAL_MutexUnlock(queue_mutex);
        if (output)
        {
            output->nOutputPortIndex = 1;
            err = dec->FillThisBuffer(dec, output);
            if (err != OMX_ErrorNone)
            {
                ret = -1;
                goto FAIL;
            }
        }
    }
    while (output);
#else
    OMX_U32 i;
    // Push all buffers to decoder
    for (i=0; i<port.nBufferCountActual; ++i)
    {
        OSAL_MutexLock(queue_mutex);
        get_header(&output_queue, &output);
        OSAL_MutexUnlock(queue_mutex);
        if (output)
        {
            OMX_U32 j;
            // Find correct ALLOC_PRIVATE struct and set the pInputPortPrivate pointer
            for (j=0; j<MAX_BUFFER_COUNT; j++)
            {
                if (bufferPrivOutput[j].pBufferData == output->pBuffer)
                {
                    output->pInputPortPrivate = &bufferPrivOutput[j];
                    break;
                }
            }
            output->nOutputPortIndex = 1;
            err = dec->FillThisBuffer(dec, output);
            if (err != OMX_ErrorNone)
            {
                ret = -1;
                goto FAIL;
            }
        }
    }
#endif

    EOS          = OMX_FALSE;
    ERROR        = OMX_FALSE;
    DO_DECODE    = OMX_TRUE;
    int over_the_limit = 0;
    OMX_S64 timeStamp = 0;

    while (eof == OMX_FALSE && ERROR == OMX_FALSE)
    {
        if (waitingBuffers)
        {
            OMX_U32 i;
            OMX_PARAM_PORTDEFINITIONTYPE port;
            INIT_OMX_TYPE(port);
            port.nPortIndex = 1; // output port

            ((OMX_COMPONENTTYPE*)dec)->GetParameter(dec, OMX_IndexParamPortDefinition, &port);
            for (i=0; i<port.nBufferCountActual; ++i)
            {
                OSAL_MutexLock(queue_mutex);
                get_header(&output_queue, &output);
                OSAL_MutexUnlock(queue_mutex);
                if (output)
                {
                    OMX_U32 j;
                    // Find correct ALLOC_PRIVATE struct and set the pInputPortPrivate pointer
                    for (j=0; j<MAX_BUFFER_COUNT; j++)
                    {
                        if (bufferPrivOutput[j].pBufferData == output->pBuffer)
                        {
                            output->pInputPortPrivate = &bufferPrivOutput[j];
                            break;
                        }
                    }
                    output->nOutputPortIndex = 1;
                    err = ((OMX_COMPONENTTYPE*)dec)->FillThisBuffer(dec, output);
                    if (err != OMX_ErrorNone)
                    {
                        return err;
                    }
                }
            }
            waitingBuffers = OMX_FALSE;
        }

        OMX_BUFFERHEADERTYPE* input = NULL;
        OSAL_MutexLock(queue_mutex);
        get_header(&input_queue, &input);
        OSAL_MutexUnlock(queue_mutex);

        if (input == NULL)
        {
            OSAL_MutexLock(queue_mutex);
            copy_list(&input_queue, &input_ret_queue);
            init_list(&input_ret_queue);
            get_header(&input_queue, &input);
            OSAL_MutexUnlock(queue_mutex);
            if (input == NULL)
            {
                usleep(1000);
                continue;
            }
        }
        DBGT_ASSERT(input);
        input->nInputPortIndex = 0;

        ret = rf(video, (char*)input->pBuffer, input->nAllocLen, readstate, &eof);
        if (ret < 0)
            goto FAIL;

        //over_the_limit = 0;
        if (args->limit && props->framecount >= args->limit)
        {
            printf("Frame limit reached\n");
            over_the_limit = 1;
            input->nFlags |= OMX_BUFFERFLAG_EOS;
        }

        input->nOffset     = 0;
        input->nFilledLen  = ret;
        input->nTimeStamp  = timeStamp;
        timeStamp += 10;

        //eof = feof(video) != 0;
        if (eof)
            input->nFlags |= OMX_BUFFERFLAG_EOS;

#ifdef ENABLE_CODEC_RV
        if (args->coding_type == OMX_VIDEO_CodingRV && rawRVFile == OMX_FALSE)
        {
            input->nFlags |= OMX_BUFFERFLAG_EXTRADATA;
        }
#endif
        // Find correct ALLOC_PRIVATE struct and set the pInputPortPrivate pointer
        for (i=0; i<MAX_BUFFER_COUNT; i++)
        {
            if (bufferPrivInput[i].pBufferData == input->pBuffer)
            {
                input->pInputPortPrivate = &bufferPrivInput[i];
                break;
            }
        }

        err = dec->EmptyThisBuffer(dec, input);
        if (err != OMX_ErrorNone)
        {
            ret = -1;
            goto FAIL;
        }
        // usleep(0);

        if (args->coding_type == OMX_VIDEO_CodingVP8)
        {
            dec->GetConfig(dec, OMX_IndexConfigVideoVp8ReferenceFrameType, &Vp8RefType);
            if (VERBOSE_OUTPUT)
                printf("Fill buffer: isIntra %d isGoldenOrAlternate %d\n", Vp8RefType.bIsIntraFrame, Vp8RefType.bIsGoldenOrAlternateFrame);
        }
        if (over_the_limit)
            break;
    }

#ifdef ENABLE_CODEC_RV
    empty_buffer_avail = 1;
    headers_ready = 0;
    stream_end = 0;
#endif

    // get stream end event
    while (EOS == OMX_FALSE && ERROR == OMX_FALSE)
    {
        // This is needed for short video streams with only couple of frames
        if (waitingBuffers)
        {
            OMX_U32 i;
            OMX_PARAM_PORTDEFINITIONTYPE port;
            INIT_OMX_TYPE(port);
            port.nPortIndex = 1; // output port

            ((OMX_COMPONENTTYPE*)dec)->GetParameter(dec, OMX_IndexParamPortDefinition, &port);
            for (i=0; i<port.nBufferCountActual; ++i)
            {
                OSAL_MutexLock(queue_mutex);
                get_header(&output_queue, &output);
                OSAL_MutexUnlock(queue_mutex);
                if (output)
                {
                    OMX_U32 j;
                    // Find correct ALLOC_PRIVATE struct and set the pInputPortPrivate pointer
                    for (j=0; j<MAX_BUFFER_COUNT; j++)
                    {
                        if (bufferPrivOutput[j].pBufferData == output->pBuffer)
                        {
                            output->pInputPortPrivate = &bufferPrivOutput[j];
                            break;
                        }
                    }
                    output->nOutputPortIndex = 1;
                    err = ((OMX_COMPONENTTYPE*)dec)->FillThisBuffer(dec, output);
                    if (err != OMX_ErrorNone)
                    {
                        return err;
                    }
                }
            }
            waitingBuffers = OMX_FALSE;
        }
        usleep(1000);
    }

    ret = !ERROR;

 FAIL:
    DO_DECODE = OMX_FALSE;
    if (video) fclose(video);
    if (yuv)   fclose(yuv);
    if (mask)  fclose(mask);
    if (dec)   destroy_decoder(dec);
#ifdef ENABLE_CODEC_RV
    if(threads[0]) // join real file format reader thread
        pthread_join(threads[0], &thread_ret);
#endif
    DBGT_EPILOG("");
    return ret;
}

#define CMP_BUFF_SIZE 10*1024

int compare_output(const char* reference, const char* temp)
{
    DBGT_PROLOG("");

    printf("Comparing files\n");
    printf("Temporary file:%s\nReference file:%s\n", temp, reference);
    int success    = 0;
    char* buff_tmp = NULL;
    char* buff_ref = NULL;
    FILE* file_tmp = fopen(temp, "rb");
    FILE* file_ref = fopen(reference, "rb");
    if (!file_tmp)
    {
        DBGT_CRITICAL("Failed to open temp file");
        goto FAIL;
    }
    if (!file_ref)
    {
        DBGT_CRITICAL("Failed to open reference file");
        goto FAIL;
    }
    fseek(file_tmp, 0, SEEK_END);
    fseek(file_ref, 0, SEEK_END);
    int tmp_size = ftell(file_tmp);
    int ref_size = ftell(file_ref);
    int min_size = tmp_size;
    int wrong_size = 0;
    if (tmp_size != ref_size)
    {
        min_size = tmp_size < ref_size ? tmp_size : ref_size;
        DBGT_CRITICAL("File sizes do not match: temp: %d reference: %d bytes", tmp_size, ref_size);
        DBGT_CRITICAL("Comparing first %d bytes", min_size);
        if(tmp_size == 0)
        {
            wrong_size = 1;
        }
    }
    if(wrong_size)
    {
        success = 0;
    }
    else
    {
        fseek(file_tmp, 0, SEEK_SET);
        fseek(file_ref, 0, SEEK_SET);

        buff_tmp = (char*)malloc(CMP_BUFF_SIZE);
        buff_ref = (char*)malloc(CMP_BUFF_SIZE);

        int pos = 0;
        int min = 0;
        int rem = 0;

        success = 1;
        while (pos < min_size)
        {
            rem = min_size - pos;
            min = rem < CMP_BUFF_SIZE ? rem : CMP_BUFF_SIZE;
            fread(buff_tmp, min, 1, file_tmp);
            fread(buff_ref, min, 1, file_ref);

            if (memcmp(buff_tmp, buff_ref, min))
            {
                success = 0;
            }
            pos += min;
        }
    }

    printf("%s file cmp done, %s\n", reference, success ? "SUCCESS!" : "FAIL");

FAIL:
    if (file_tmp) fclose(file_tmp);
    if (file_ref) fclose(file_ref);
    if (buff_tmp) free(buff_tmp);
    if (buff_ref) free(buff_ref);
    DBGT_EPILOG("");
    return success;
}

typedef struct SCRIPVAR {
    char* name;
    char* value;
} SCRIPTVAR;

int map_format_string(const char* format)
{
    char name[255];
    memset(name, 0, sizeof(name));
    if (strncmp(format, "OMX_COLOR_Format", 16)!=0)
        strcat(name, "OMX_COLOR_Format");
    strcat(name, format);

    int i = OMX_COLOR_FormatUnused;
    for (; i <= OMX_COLOR_Format24BitABGR6666; ++i)
    {
        if (strcmp(name, HantroOmx_str_omx_color(i))==0)
            return i;
    }

    i = OMX_COLOR_FormatVsiStartUnused;
    for (; i <= OMX_COLOR_FormatYUV444PackedSemiPlanar; ++i)
    {
        if (strcmp(name, HantroOmx_str_omx_color(i))==0)
            return i;
    }

    return -1;
}

const char* format_to_file_ext(int format)
{
    switch (format)
    {
        case OMX_COLOR_FormatUnused:
        case OMX_COLOR_FormatL8:
            return "yuv";
        case OMX_COLOR_Format16bitRGB565:
        case OMX_COLOR_Format16bitBGR565:
        case OMX_COLOR_Format16bitARGB1555:
        case OMX_COLOR_Format16bitARGB4444:
            return "rgb16";
        case OMX_COLOR_Format32bitARGB8888:
        case OMX_COLOR_Format32bitBGRA8888:
            return "rgb32";
        case OMX_COLOR_FormatYCbYCr:
            return "yuyv";
        case OMX_COLOR_FormatYCrYCb:
            return "yuyv";
        case OMX_COLOR_FormatCbYCrY:
            return "yuyv";
        case OMX_COLOR_FormatCrYCbY:
            return "yuyv";
        default:
            DBGT_ASSERT(!"Unknown color format");
    }
    return NULL;
}

#define ASSURE_NEXT_ARG(i, argc)                \
  if (!((i)+1 < (argc)))                        \
      return 0

int parse_argument_vector(const char** argv, int argc, TESTARGS* args)
{
    int i;

    args->name = (char *) calloc(1, OMX_MAX_STRINGNAME_SIZE);
    for(i=0; i<argc; ++i)
    {
        if (argv[i]== NULL)
            continue;
        if (!strcmp(argv[i], "-w"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->width = atoi(argv[++i]);
            if (args->width < 48)
            {
                DBGT_CRITICAL("Invalid width: %d", args->width);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-h"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->height = atoi(argv[++i]);
            if (args->height < 48)
            {
                DBGT_CRITICAL("Invalid height: %d", args->height);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-iw"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->input_width = atoi(argv[++i]);
            if (args->input_width < 48)
            {
                DBGT_CRITICAL("Invalid input_width: %d", args->input_width);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-ih"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->input_height = atoi(argv[++i]);
            if (args->input_height < 48)
            {
                DBGT_CRITICAL("Invalid input_height: %d", args->input_height);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-dw"))
        {
            ASSURE_NEXT_ARG(i, argc);
            divx3_width = atoi(argv[++i]);
            args->input_width = divx3_width;
            if (args->input_width < 48)
            {
                DBGT_CRITICAL("Invalid input_width: %d", args->input_width);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-dh"))
        {
            ASSURE_NEXT_ARG(i, argc);
            divx3_height = atoi(argv[++i]);
            args->input_height = divx3_height;
            if (args->input_height < 48)
            {
                DBGT_CRITICAL("Invalid input_height: %d", args->input_height);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-r"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->rotation = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-of"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->output_format = map_format_string(argv[++i]);
            if (args->output_format == -1)
            {
                DBGT_CRITICAL("Unknown color format: %s", argv[i]);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-if"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->input_format = map_format_string(argv[++i]);
            if (args->input_format == -1)
            {
                DBGT_CRITICAL("Unknown color format: %s", argv[i]);
                return -1;
            }
        }
        else if (!strcmp(argv[i], "-ib"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->buffer_size_in = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-ob"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->buffer_size_out = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-id"))
        {
            ASSURE_NEXT_ARG(i, argc);
            const char* arg = argv[++i];
            args->test_id = (char*)malloc(strlen(arg)+1);
            memset(args->test_id, 0, strlen(arg)+1);
            strcpy(args->test_id, arg);
        }
        else if (!strcmp(argv[i], "-i"))
        {
            ASSURE_NEXT_ARG(i, argc);
            const char* arg = argv[++i];
            args->input = (char*)malloc(strlen(arg)+1);
            memset(args->input, 0, strlen(arg)+1);
            strcpy(args->input, arg);
        }
        else if (!strcmp(argv[i], "-O"))
        {
            ASSURE_NEXT_ARG(i, argc);
            const char* arg = argv[++i];
            args->output_file = (char*)malloc(strlen(arg)+1);
            memset(args->output_file, 0, strlen(arg)+1);
            strcpy(args->output_file, arg);
        }
        else if (!strcmp(argv[i], "-bi"))
        {
            ASSURE_NEXT_ARG(i, argc);
            const char* arg = argv[++i];
            args->blend_input = (char*)malloc(strlen(arg)+1);
            memset(args->blend_input, 0, strlen(arg)+1);
            strcpy(args->blend_input, arg);
        }
        else if (!strcmp(argv[i], "-rf"))
        {
            ASSURE_NEXT_ARG(i, argc);
            const char* arg = argv[++i];
            args->reference_file = (char*)malloc(strlen(arg)+1);
            memset(args->reference_file, 0, strlen(arg)+1);
            strcpy(args->reference_file, arg);
        }
        else if (!strcmp(argv[i], "-rd"))
        {
            ASSURE_NEXT_ARG(i, argc);
            const char* arg = argv[++i];
            args->reference_dir = (char*)malloc(strlen(arg)+1);
            memset(args->reference_dir, 0, strlen(arg)+1);
            strcpy(args->reference_dir, arg);
        }
        else if (!strcmp(argv[i], "-cx"))
        {
            // crop x offset
            ASSURE_NEXT_ARG(i, argc);
            args->crop_x = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-cy"))
        {
            // crop y offset
            ASSURE_NEXT_ARG(i, argc);
            args->crop_y = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-cw"))
        {
            // crop width
            ASSURE_NEXT_ARG(i, argc);
            args->crop_width = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-ch"))
        {
            // crop height
            ASSURE_NEXT_ARG(i, argc);
            args->crop_height = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-mx"))
        {
            // mask origin X
            ASSURE_NEXT_ARG(i, argc);
            args->mask1.originX = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-my"))
        {
            // mask origin Y
            ASSURE_NEXT_ARG(i, argc);
            args->mask1.originY = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-mw"))
        {
            // mask width
            ASSURE_NEXT_ARG(i, argc);
            args->mask1.width = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-mh"))
        {
            // mask height
            ASSURE_NEXT_ARG(i, argc);
            args->mask1.height = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-m2x"))
        {
            // mask origin X
            ASSURE_NEXT_ARG(i, argc);
            args->mask2.originX = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-m2y"))
        {
            // mask origin Y
            ASSURE_NEXT_ARG(i, argc);
            args->mask2.originY = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-m2w"))
        {
            // mask width
            ASSURE_NEXT_ARG(i, argc);
            args->mask2.width = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-m2h"))
        {
            // mask height
            ASSURE_NEXT_ARG(i, argc);
            args->mask2.height = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-fl"))
        {
            // frame limit
            ASSURE_NEXT_ARG(i, argc);
            args->limit = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-fv"))
        {
            args->mirroring = 1;
        }
        else if (!strcmp(argv[i], "-fh"))
        {
            args->mirroring = 2;
        }
        else if (!strcmp(argv[i], "-nc"))
        {
            args->no_compare = 1;
        }
        else if(!strcmp(argv[i], "-dith"))
        {
            args->dithering = 1;
        }
        else if(!strcmp(argv[i], "-debloc"))
        {
            args->deblocking = 1;
        }
        else if(!strcmp(argv[i], "-mvc"))
        {
            args->mvc_stream = 1;
        }
        else if(!strcmp(argv[i], "-contra"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->rgb.contrast = atoi(argv[++i]);
        }
        else if(!strcmp(argv[i], "-brig"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->rgb.brightness = atoi(argv[++i]);
        }
        else if(!strcmp(argv[i], "-satur"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->rgb.saturation = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-h263"))
        {
            args->coding_type = OMX_VIDEO_CodingH263;
            strncpy(args->name, COMPONENT_NAME_MPEG4,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-h264"))
        {
            args->coding_type = OMX_VIDEO_CodingAVC;
            strncpy(args->name, COMPONENT_NAME_H264,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-mpeg4"))
        {
            args->coding_type = OMX_VIDEO_CodingMPEG4;
            strncpy(args->name, COMPONENT_NAME_MPEG4,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-wmv"))
        {
            args->coding_type = OMX_VIDEO_CodingWMV;
            strncpy(args->name, COMPONENT_NAME_VC1,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-mpeg2"))
        {
            args->coding_type = OMX_VIDEO_CodingMPEG2;
            strncpy(args->name, COMPONENT_NAME_MPEG2,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-sorenson"))
        {
            args->coding_type = OMX_VIDEO_CodingSORENSON;
            strncpy(args->name, COMPONENT_NAME_MPEG4,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-divx"))
        {
            args->coding_type = OMX_VIDEO_CodingDIVX;
            strncpy(args->name, COMPONENT_NAME_MPEG4,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-divx3"))
        {
            args->coding_type = OMX_VIDEO_CodingDIVX3;
            strncpy(args->name, COMPONENT_NAME_MPEG4,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
#ifdef ENABLE_CODEC_RV
        else if (!strcmp(argv[i], "-rv"))
        {
            args->coding_type = OMX_VIDEO_CodingRV;
            strncpy(args->name, COMPONENT_NAME_RV,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
#else
        else if (!strcmp(argv[i], "-rv"))
        {
            DBGT_CRITICAL("Unsupported input format (RealVideo)");
            return -1;
        }
#endif
        else if (!strcmp(argv[i], "-vp6"))
        {
            args->coding_type = OMX_VIDEO_CodingVP6;
            strncpy(args->name, COMPONENT_NAME_VP6,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-avs"))
        {
            args->coding_type = OMX_VIDEO_CodingAVS;
            strncpy(args->name, COMPONENT_NAME_AVS,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-vp8"))
        {
            args->coding_type = OMX_VIDEO_CodingVP8;
            strncpy(args->name, COMPONENT_NAME_VP8,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-mjpeg"))
        {
            args->coding_type = OMX_VIDEO_CodingMJPEG;
            strncpy(args->name, COMPONENT_NAME_MJPEG,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-hevc"))
        {
            args->coding_type = OMX_VIDEO_CodingHEVC;
            strncpy(args->name, COMPONENT_NAME_HEVC,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-vp9"))
        {
            args->coding_type = OMX_VIDEO_CodingVP9;
            strncpy(args->name, COMPONENT_NAME_VP9,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-jpeg"))
        {
            args->coding_type = OMX_IMAGE_CodingJPEG;
            args->domain = CODEC_IMAGE;
            strncpy(args->name, COMPONENT_NAME_JPEG,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
        else if (!strcmp(argv[i], "-webp"))
        {
            args->coding_type = OMX_IMAGE_CodingWEBP;
            args->domain = CODEC_IMAGE;
            strncpy(args->name, COMPONENT_NAME_WEBP,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
#ifdef ENABLE_PP
        else if (!strcmp(argv[i], "-none"))
        {
            args->coding_type = OMX_VIDEO_CodingUnused;
            strncpy(args->name, COMPONENT_NAME_PP,
            OMX_MAX_STRINGNAME_SIZE - 1);
        }
#endif
        else if (!strcmp(argv[i], "-fail"))
            args->fail = 1;
        else if (!strcmp(argv[i], "-tiled"))
            args->tiled = 1;
        else if (!strcmp(argv[i], "-sec"))
            args->secure = 1;
        else if (!strcmp(argv[i], "-rfc"))
            args->rfc = 1;
        else if (!strcmp(argv[i], "-pf"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->pixFormat = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-v"))
            VERBOSE_OUTPUT = OMX_TRUE;
        else if (!strcmp(argv[i], "-guard"))
        {
            ASSURE_NEXT_ARG(i, argc);
            args->guard_buffers = atoi(argv[++i]);
            args->adaptive = 1;
        }
        else
        {
            DBGT_CRITICAL("Unknown argument: %s", argv[i]);
            return -1;
        }
    }
    return 1;
}

int parse_argument_line(const char* line, size_t len, TESTARGS* args)
{
    char* argv[100];
    int argc = 0;
    memset(argv, 0, sizeof(argv));

    const char* start = line;
    size_t tokenlen   = 0;

    size_t i;
    for (i=0; i<len; ++i)
    {
        if (line[i] == ' ')
        {
            if (tokenlen)
            {
                char* str = (char*)malloc(tokenlen+1);
                memset(str, 0, tokenlen+1);
                strncpy(str, start, tokenlen);
                argv[argc++] = str;
            }
            tokenlen = 0;
            start = &line[i+1];
        }
        else
        {
            ++tokenlen;
        }
    }
    if (tokenlen)
    {
        char* str = (char*)malloc(tokenlen+1);
        memset(str, 0, tokenlen+1);
        strncpy(str, start, tokenlen);
        argv[argc++] = str;
    }

    int ret = parse_argument_vector((const char**)argv, argc, args);

    for (i=0; i<sizeof(argv)/sizeof(const char*); ++i)
        if (argv[i])
            free(argv[i]);

    return ret;
}

int parse_script_var(const char* line, size_t len, SCRIPTVAR* var)
{
    DBGT_ASSERT(line[0] == '!');
    line++;
    len--;
    size_t i;
    for (i=0; i<len; ++i)
    {
        if (line[i] == '=')
            break;
    }
    var->name = (char*)malloc(i + 1);
    memset(var->name, 0, i + 1);
    strncat(var->name, line, i);
    ++i; // skip the '='
    line += i;
    len  -= i;
    var->value = (char*)malloc(len + 1);
    memset(var->value, 0, len + 1);
    strncat(var->value, line, len);
    return 1;
}

int set_variable(SCRIPTVAR** array, size_t arraysize, SCRIPTVAR* var)
{
    DBGT_ASSERT(var);
    DBGT_ASSERT(var->name && var->value);
    size_t i=0;
    for (i=0; i<arraysize; ++i)
    {
        const SCRIPTVAR* old = array[i];
        if (old == NULL)
            break;
        if (strcmp(old->name, var->name)==0)
            break;
    }
    if (!(i < arraysize))
        return 0;

    if (array[i] == NULL)
    {
        array[i] = var;
    }
    else
    {
        DBGT_ASSERT(strcmp(array[i]->name, var->name)==0);
        free(array[i]->value);
        array[i]->value = var->value;
        free(var->name);
        free(var);
    }
    return 0;
}

char* expand_variable(SCRIPTVAR** array, size_t arraysize, const char* str)
{
    char* expstr = NULL;
    size_t i=0;
    for (i=0; i<arraysize; ++i)
    {
        const SCRIPTVAR* var = array[i];
        if (var)
        {
            char* needle = (char*)malloc(strlen(var->name) + 4);
            memset(needle, 0, strlen(var->name)+4);
            strcat(needle, "$(");
            strcat(needle, var->name);
            strcat(needle, ")");
            char* ret = strstr(str, needle);
            if (ret)
            {
                int len = strlen(var->value) + strlen(str) - strlen(needle);
                expstr  = (char*)malloc(len + 1);
                memset(expstr, 0, len+1);
                strncat(expstr, str, ret-str);
                strcat(expstr, var->value);
                strcat(expstr, ret+strlen(needle));
                break;
            }
        }
    }
    return expstr;
}

char* find_variable(SCRIPTVAR** array, size_t arraysize, const char* name)
{
    size_t i=0;
    for (i=0; i<arraysize; ++i)
    {
        const SCRIPTVAR* var = array[i];
        if (var)
        {
            if (strcmp(name, var->name)==0)
                return var->value;
        }
    }
    return NULL;
}

typedef enum TEST_RESULT { TEST_FAIL, TEST_PASS, TEST_UNDECIDED } TEST_RESULT;

TEST_RESULT execute_test(const TESTARGS* test)
{
    DBGT_PROLOG("");

    VIDEOPROPS props;
    RCVSTATE rcv;
    memset(&props, 0, sizeof(props));
    memset(&rcv, 0, sizeof(rcv));

    if (test->test_id)
        printf("RUNNING: %s\n", test->test_id);

    const char* output = test->reference_file ? "temp.yuv" : NULL;
    read_func   func;

    if (test->domain == CODEC_VIDEO)
    {
        if (test->coding_type == OMX_VIDEO_CodingVP8)
        {
            formatCheck = 0;
            func = read_vp8_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingAVC)
        {
            func = read_h264_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingAVS)
        {
            traceUsedStream = 0;
            previousUsed = 0;
            //func = read_any_file;
            func = read_avs_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingSORENSON)
        {
            traceUsedStream = 0;
            previousUsed = 0;
            func = read_sorenson_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingVP6)
        {
            func = read_vp6_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingDIVX)
        {
            traceUsedStream = 0;
            previousUsed = 0;
            func = read_mpeg4_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingDIVX3)
        {
            start_DIVX3 = 0;
            offset = 0;
            func = read_DIVX3_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingMPEG4)
        {
            traceUsedStream = 0;
            previousUsed = 0;
            func = read_mpeg4_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingMPEG2)
        {
            traceUsedStream = 0;
            previousUsed = 0;
            func = read_mpeg2_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingH263)
        {
            traceUsedStream = 0;
            previousUsed = 0;
            func = read_h263_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingHEVC)
        {
            traceUsedStream = 0;
            previousUsed = 0;
            func = read_h264_file;
        }
        else if (test->coding_type == OMX_VIDEO_CodingVP9)
        {
            formatCheck = 0;
            func = read_vp8_file;
        }
#ifdef ENABLE_CODEC_RV
        else if (test->coding_type == OMX_VIDEO_CodingRV)
        {
            func = read_rv_file;
            rawRVFile = OMX_FALSE;
            rawRV8 = OMX_FALSE;
        }
#endif
        else if (test->coding_type == OMX_VIDEO_CodingMJPEG)
        {
            //formatCheck = 0;
            func = read_mjpeg_file;
        }
        else
        {
            func = test->coding_type == OMX_VIDEO_CodingWMV ? read_rcv_file : read_any_file;
        }
    }
    else
    {
        if (test->coding_type == OMX_IMAGE_CodingJPEG)
        {
            func = read_any_file;
        }
        else if(test->coding_type == OMX_IMAGE_CodingWEBP)
        {
            func = read_webp_file;
        }
    }

    if (test->no_compare && !test->output_file)
        output = "temp.yuv";
    else if (test->no_compare && test->output_file)
        output = test->output_file;

    TEST_RESULT res = TEST_FAIL;
    if (decode_file(test->input, output, &props, test, func, &rcv))
    {
        if (output && !test->no_compare)
        {
            res = TEST_FAIL;
            // if there's a single file output compare that
            char output[256];
            memset(output, 0, sizeof(output));
            strcat(output, test->reference_dir);
            strcat(output, "/");
            strcat(output, test->reference_file);
            if (compare_output(output, "temp.yuv"))
            {
                res = TEST_PASS;
                printf("PASS: %s\n", test->test_id);
            }


            if (res == TEST_FAIL && test->test_id)
            {
                memset(output, 0, sizeof(output));
                sprintf(output, "mv temp.yuv %s.fail.%s", test->test_id, format_to_file_ext(test->output_format));
                system(output);
                fprintf(stderr, "FAILED: %s stride:%d sliceh:%d\n", test->test_id, props.stride, props.sliceheight);
            }
        }
        else if (test->reference_dir && !test->no_compare && res != TEST_FAIL)
        {
            res = TEST_FAIL;
            // otherwise compare the frames we got to the
            // frames in the reference directory.
            char frame[255];
            char output[255];
            DBGT_ASSERT(props.framecount);
            int i=0;
            for (i=0; i<props.framecount; ++i)
            {
                memset(frame, 0, sizeof(frame));
                memset(output, 0, sizeof(output));
                sprintf(frame, "frame%d.yuv", i);
                sprintf(output, "%s/out_%d.yuv", test->reference_dir, i);
                if (!compare_output(output, frame))
                    break;
            }
            if (i==props.framecount) res = TEST_PASS;

            if (res == TEST_FAIL)
            {
                memset(frame, 0, sizeof(frame));
                sprintf(frame, "mv frame%d.yuv %s.frame%d.fail.%s", i, test->test_id, i, format_to_file_ext(test->output_format));
                system(frame);
                fprintf(stderr, "FAILED: %s stride:%d sliceh:%d\n", test->test_id, props.stride, props.sliceheight);
            }
        }
    }
    else
    {
        if (test->test_id)
        {
            char output[256];
            memset(output, 0, sizeof(output));
            sprintf(output, "mv temp.yuv %s.fail.%s", test->test_id, format_to_file_ext(test->output_format));
            system(output);
            fprintf(stderr, "FAILED: %s\n", test->test_id);
        }
        res = TEST_FAIL;
    }
    DBGT_EPILOG("");
    return res;
}

int main(int argc, const char* argv[])
{
    printf("OMX component test video decoder\n");
    printf(__DATE__", "__TIME__"\n");

    DBGT_TRACE_INIT(video_decoder);

    OMX_ERRORTYPE err = OSAL_MutexCreate(&queue_mutex);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("Mutex create error: %s", HantroOmx_str_omx_err(err));
        return 1;
    }

    TESTARGS test;
    VIDEOPROPS props;
    memset(&test, 0, sizeof(test));
    memset(&props, 0, sizeof(props));
    int test_count = 0;
    int pass_count = 0;
    int fail_count = 0;
    int undc_count = 0;
    reconfigPending = OMX_FALSE;
    waitingBuffers = OMX_FALSE;

    if (argc > 2)
    {
        //VERBOSE_OUTPUT = OMX_TRUE;
        argv[0] = NULL;
        test.buffer_size_in  = DEFAULT_BUFFER_SIZE_INPUT;
        test.buffer_size_out = DEFAULT_BUFFER_SIZE_OUTPUT;
        test.rgb.alpha       = 0xFF;
        test.pixFormat = 1; // Default value for G2 decoder
        if (parse_argument_vector(argv, argc, &test) != -1)
        {
            ++test_count;
            switch (execute_test(&test))
            {
                case TEST_PASS:      ++pass_count; break;
                case TEST_FAIL:      ++fail_count; break;
                case TEST_UNDECIDED: ++undc_count; break;
            }
            if (test.input) free(test.input);
            if (test.reference_file) free(test.reference_file);
            if (test.reference_dir) free(test.reference_dir);
            if (test.test_id) free(test.test_id);
            if (test.blend_input) free(test.blend_input);
            if (test.name)    free(test.name);
        }
        else
        {
            DBGT_CRITICAL("Unknown arguments");
            DBGT_EPILOG("");
            return 1;
        }
    }
    else
    {
        FILE* strm = fopen("test_script.txt", "r");
        if (!strm)
        {
            DBGT_CRITICAL("Failed to open test_script.txt");
            OSAL_MutexDestroy(queue_mutex);
            DBGT_EPILOG("");
            return 1;
        }

        char* line = NULL;
        size_t linelen = 0;
        SCRIPTVAR* svars[100]; // TODO:bounds checking below
        memset(svars, 0, sizeof(svars));

        while (getline(&line, &linelen, strm) != -1)
        {
            DBGT_ASSERT(line);
            if (*line == '#' || *line == '\n')
                continue;

            linelen = strlen(line);
            if (line[linelen-1] == '\n')
                --linelen;

            if (line[0] == '!')
            {
                SCRIPTVAR* var = (SCRIPTVAR*)malloc(sizeof(SCRIPTVAR));
                parse_script_var(line, linelen, var);
                set_variable(svars, sizeof(svars)/sizeof(SCRIPTVAR*), var);
            }
            else
            {
                memset(&test, 0, sizeof(TESTARGS));

                // set some hardcoded defaults
                test.buffer_size_in  = DEFAULT_BUFFER_SIZE_INPUT;
                test.buffer_size_out = DEFAULT_BUFFER_SIZE_OUTPUT;
                test.rgb.alpha       = 0xFF;
                test.pixFormat = 1; // Default value for G2 decoder
                if (parse_argument_line(line, linelen, &test) != -1)
                {
                    char* exp = NULL;
                    exp = expand_variable(svars, sizeof(svars)/sizeof(SCRIPTVAR*), test.input);
                    if (exp)
                    {
                        free(test.input);
                        test.input = exp;
                    }
                    exp = expand_variable(svars, sizeof(svars)/sizeof(SCRIPTVAR*), test.reference_dir);
                    if (exp)
                    {
                        free(test.reference_dir);
                        test.reference_dir = exp;
                    }
                    if (test.blend_input)
                    {
                        exp = expand_variable(svars, sizeof(svars)/sizeof(SCRIPTVAR*), test.blend_input);
                        if (exp)
                        {
                            free(test.blend_input);
                            test.blend_input = exp;
                        }
                    }
                    ++test_count;
                    switch (execute_test(&test))
                    {
                        case TEST_PASS:
                            {
                                ++pass_count;
                            }
                            break;
                        case TEST_FAIL:
                            {
                                ++fail_count;
                                if (test.test_id)
                                    fprintf(stderr, "FAILED: %s\n", test.test_id);
                            }
                            break;
                        case TEST_UNDECIDED: ++undc_count; break;
                    }
                }
                else
                {
                    printf("Unknown argument line:%s\n", line);
                    printf("skipping...\n");
                }
                if (test.input) free(test.input);
                if (test.reference_file) free(test.reference_file);
                if (test.reference_dir) free(test.reference_dir);
                if (test.test_id) free(test.test_id);
                if (test.blend_input) free(test.blend_input);
                if (test.name)    free(test.name);
            }
            free(line);
            line = NULL;
            linelen = 0;
        }
        free(line);
        line = NULL;
        linelen = 0;
        fclose(strm);

        size_t i;
        for (i=0; i<sizeof(svars)/sizeof(SCRIPTVAR*); ++i)
        {
            if (svars[i])
            {
                if (svars[i]->name)  free(svars[i]->name);
                if (svars[i]->value) free(svars[i]->value);
                free(svars[i]);
            }
        }
    }

    if (!test.no_compare)
        printf("\nResult summary:\n"
           "Tests run:\t%d\n"
           "Tests pass:\t%d\n"
           "Tests fail:\t%d\n"
           "Tests else:\t%d\n",
           test_count,
           pass_count,
           fail_count,
           undc_count);

    OSAL_MutexDestroy(queue_mutex);

    if ((pass_count != test_count - undc_count) && !test.no_compare)
    {
        printf("*** FAIL ***\n");
        DBGT_EPILOG("");
        return -1;
    }
    else
        printf("Done\n");

    DBGT_EPILOG("");
    return 0;
}

#ifdef ENABLE_CODEC_RV
void startRealFileReader(FILE * pFile, OMX_U32** pBuffer, OMX_U32* pAllocLen, OMX_BOOL* eof)
{
    DBGT_PROLOG("");
    int rc;

    buffering_data.pFile = pFile;
    buffering_data.pBuffer = pBuffer;
    buffering_data.pAllocLen = pAllocLen;
    buffering_data.eof = eof;

    rc = pthread_create(&threads[0], NULL, (void *)rv_display, (void *)&buffering_data);
    DBGT_EPILOG("");
}
#endif

/*------------------------------------------------------------------------------

    Function name:  PPTrace

    Purpose:
        Example implementation of PPTrace function. Prototype of this
        function is given in ppapi.h. This implementation appends
        trace messages to file named 'pp_api.trc'.

------------------------------------------------------------------------------*/
void PPTrace(const char *string)
{
    printf("%s", string);
}
