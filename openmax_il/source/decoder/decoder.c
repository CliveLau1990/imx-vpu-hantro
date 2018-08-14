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

#include <pthread.h>
#include "OSAL.h"
#include "basecomp.h"
#include "port.h"
#include "util.h"
#include "version.h"
#include "codec.h"
#ifdef IS_G1_DECODER
#include "codec_h264.h"
#include "codec_jpeg.h"
#include "codec_mpeg4.h"
#include "codec_vc1.h"
#include "codec_rv.h"
#include "codec_mpeg2.h"
#include "codec_vp6.h"
#include "codec_avs.h"
#include "codec_vp8.h"
#include "codec_webp.h"
#include "codec_pp.h"
#endif
#ifdef IS_G2_DECODER
#include "codec_hevc.h"
#include "codec_vp9.h"
#endif

#define DBGT_DECLARE_AUTOVAR
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX "

#if defined (IS_G1_DECODER) && defined (IS_G2_DECODER)
#error "Please define only one decoder when building the OMX component library"
#endif

#ifdef USE_OPENCORE // To be use by opencore multimedia framework
#define PV_OMX_COMPONENT_CAPABILITY_TYPE_INDEX 0xFF7A347
#endif

// Needed for testing with HW model
#ifdef INCLUDE_TB
#include "tb_cfg.h"
#ifndef IS_G2_DECODER
#include "tb_defs.h"
struct TBCfg tb_cfg;
OMX_S32 b_frames = 0;
#else
struct TBCfg tb_cfg;
u32 test_case_id = 0;
#endif
#endif

#define PORT_INDEX_INPUT    0
#define PORT_INDEX_OUTPUT   1
#define PORT_INDEX_PP       2
#define RETRY_INTERVAL      5
#define TIMEOUT             2000
#define MAX_RETRIES         100000
#define MAX_TICK_COUNTS     64

#define MAX_PROPAGATE_BUFFER_SIZE 64

#define GET_DECODER(comp) (OMX_DECODER*)(((OMX_COMPONENTTYPE*)comp)->pComponentPrivate)

#define FRAME_BUFF_CAPACITY(fb) (fb)->capacity - (fb)->size

#define FRAME_BUFF_FREE(alloc, fb) \
  OSAL_AllocatorFreeMem((alloc), (fb)->capacity, (fb)->bus_data, (fb)->bus_address)

#ifdef OMX_SKIP64BIT
#define EARLIER(t1, t2) (((t1).nHighPart < (t2).nHighPart) || (((t1).nHighPart == (t2).nHighPart) && ((t1).nLowPart < (t2).nLowPart)))
#else
#define EARLIER(t1, t2) ((t1) < (t2))
#endif

#ifdef _MSC_VER  // For Visual Studio compiler.
#define HANTROOMXDEC_EXPORT __declspec (dllexport)
#else  // Default to GCC.
#define HANTROOMXDEC_EXPORT __attribute__ ((visibility("default")))
#endif

typedef struct TIMESTAMP_BUFFER
{
    OMX_TICKS *ts_data;
    OMX_U32 capacity;        // buffer capacity
    OMX_U32 count;           // how many timestamps are in the buffer currently
} TIMESTAMP_BUFFER;

typedef struct FRAME_BUFFER
{
    OSAL_BUS_WIDTH bus_address;
    OMX_U8 *bus_data;
    OMX_U32 capacity;        // buffer size
    OMX_U32 size;            // how many bytes is in the buffer currently
} FRAME_BUFFER;

typedef struct SHARED_DATA
{
    void*       decInstance;            /* Decoder instance */
    BUFFER      *inbuff;                /* Input buffer */
    pthread_t   output_thread_;         /* Handle to the output thread */
    OSAL_BUS_WIDTH eos_bus_address;     /* Bus address of EOS buffer */
    volatile OMX_BOOL    EOS;
    volatile OMX_BOOL    output_thread_run;
    volatile OMX_BOOL    hasFrame;
} SHARED_DATA;

typedef struct PROPAGATE_INPUT_DATA
{
    OMX_TICKS          ts_data;  // store the nTimeStamp of input buffer
    OMX_MARKTYPE       marks;    // store the pMarkData of input buffer
    OMX_U32            picIndex; // store the index of decode id
} PROPAGATE_INPUT_DATA;

typedef struct PROPAGATE_BUFFER
{
    PROPAGATE_INPUT_DATA *propagate_data;
    OMX_U32 capacity;
    OMX_U32 count;
} PROPAGATE_BUFFER;

typedef struct OMX_DECODER
{
    OMX_U8                  privatetable[256]; // Bellagio has room to store it's own privates here
    BASECOMP                base;
    volatile OMX_STATETYPE  state;
    OMX_HANDLETYPE          statemutex;     // mutex to protect state changes
    OMX_HANDLETYPE          timemutex;      // mutex to protect time stamp changes
    OMX_HANDLETYPE          threadmutex;    // mutex to shared data
    volatile OMX_STATETYPE  statetrans;
    volatile OMX_BOOL       run;
    OMX_U32                 priority_group;
    OMX_U32                 priority_id;
    OMX_BOOL                deblock;
    OMX_CALLBACKTYPE        callbacks;
    OMX_PTR                 appdata;
    BUFFER                  *buffer;        // outgoing buffer
    PORT                    in;             // regular input port
    PORT                    inpp;           // post processor input port
    PORT                    out;            // output port
    OMX_HANDLETYPE          self;           // handle to "this" component
    OSAL_ALLOCATOR          alloc;
    CODEC_PROTOTYPE         *codec;
    CODEC_STATE             codecstate;
    FRAME_BUFFER            frame_in;       // temporary input frame buffer
    FRAME_BUFFER            frame_out;      // temporary output frame buffer
    FRAME_BUFFER            mask;
    TIMESTAMP_BUFFER        ts_buf;
    OMX_U8                  role[128];
    OMX_CONFIG_ROTATIONTYPE conf_rotation;
    OMX_CONFIG_MIRRORTYPE   conf_mirror;
    OMX_CONFIG_CONTRASTTYPE conf_contrast;
    OMX_CONFIG_BRIGHTNESSTYPE conf_brightness;
    OMX_CONFIG_SATURATIONTYPE conf_saturation;
    OMX_CONFIG_PLANEBLENDTYPE conf_blend;
    OMX_CONFIG_RECTTYPE     conf_rect;
#ifdef SET_OUTPUT_CROP_RECT
    OMX_BOOL                output_cropping_set;
    OMX_CONFIG_RECTTYPE     output_cropping_rect;
#endif
    OMX_CONFIG_POINTTYPE    conf_mask_offset;
    OMX_CONFIG_RECTTYPE     conf_mask;
    OMX_CONFIG_DITHERTYPE   conf_dither;
    OMX_MARKTYPE            marks[10];
    OMX_U32                 mark_read_pos;
    OMX_U32                 mark_write_pos;
    OMX_U32                 mark_count;
    OMX_U32                 imageSize;
    OMX_BOOL                dispatchOnlyFrame;
    OMX_BOOL                bIsRV8;
    OMX_U32                 width;
    OMX_U32                 height;
    OMX_U32                 sliceInfoNum;   /* real slice data */
    OMX_U8                  *pSliceInfo;    /* real slice data */
    OMX_BOOL                portReconfigPending;
    OMX_U64                 ReallocBufferAddress; /* address of buffer need to be reallocated
                                                   Only used for VP9 dynamic port reconfig */
    OMX_VIDEO_WMVFORMATTYPE WMVFormat;
#ifdef ENABLE_CODEC_VP8
    OMX_BOOL                isIntra;
    OMX_BOOL                isGoldenOrAlternate;
#endif
#ifdef OMX_DECODER_IMAGE_DOMAIN
#ifdef CONFORMANCE
    OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE   quant_table;
    OMX_IMAGE_PARAM_HUFFMANTTABLETYPE       huffman_table;
#endif
#endif
    /* Fix deadlock on port enable/disable/flush with Stagefright */
    OMX_BOOL                disablingOutPort;
    OMX_BOOL                flushingOutPort;
    OMX_BOOL                releaseBuffers;

    OMX_BOOL                isMvcStream;
    PP_ARGS                 pp_args;  // post processor parameters
    SHARED_DATA             shared_data;
    BUFFER                  *prevBuffer;
#ifdef IS_G2_DECODER
    OMX_U32                 bitDepth;
    OMX_VIDEO_PARAM_G2CONFIGTYPE g2Conf;
#endif
#ifdef IS_G1_DECODER
    OMX_VIDEO_PARAM_G1CONFIGTYPE g1Conf;
#endif
    OMX_U32                 outputBufList[34];
    OMX_U32                 outputBufListPrev[34]; //store prev buf list, only used for VP9 dynamic port reconfig
    OMX_BOOL                checkExtraBuffers;
    OMX_BOOL                useExternalAlloc;
    ALLOC_PRIVATE           bufPriv;

    /* Paramters used to propagate timestamp/markerbuffer */
    PROPAGATE_BUFFER        propagate_buf;  // buffer used to store timestamp/markbuffer
    PROPAGATE_INPUT_DATA    propagateData;
    OMX_BOOL                propagateDataReceived;
    OMX_U32                 oldestPicIdInBuf; // the smallest pic id in propagate_buf
    OMX_U32                 prevPicIdList[64];  // store the pic id which has been propagated.
    OMX_U32                 prevPicIdWritePos;
    OMX_BOOL                inBufferIsUpdated; // indicate if more than one frames in one input buffer.

    /* Number of guard-buffer */
    OMX_U32                 nGuardSize;
} OMX_DECODER;

static void* output_thread(void* arg); /* Output loop. */

#if 0
static OMX_ERRORTYPE grow_timestamp_buffer(OMX_DECODER * dec, TIMESTAMP_BUFFER * fb,
                                    OMX_U32 capacity)
{
    UNUSED_PARAMETER(dec);
    CALLSTACK;
    DBGT_ASSERT(capacity >= fb->count);

    TIMESTAMP_BUFFER new_ts;

    memset(&new_ts, 0, sizeof(TIMESTAMP_BUFFER));
    new_ts.ts_data = malloc(capacity*sizeof(OMX_TICKS));

    memcpy(new_ts.ts_data, fb->ts_data, fb->count*sizeof(OMX_TICKS));
    new_ts.capacity = capacity;
    new_ts.count = fb->count;
    free(fb->ts_data);

    *fb = new_ts;
    return OMX_ErrorNone;
}

static void receive_timestamp(OMX_DECODER * dec, OMX_TICKS *timestamp)
{
    OMX_U32 i;
    TIMESTAMP_BUFFER *temp = &dec->ts_buf;

    if (temp->count >= temp->capacity)
    {
        OMX_U32 capacity = temp->capacity + 16;
        grow_timestamp_buffer(dec, temp, capacity);
    }

    for (i = 0; i < dec->ts_buf.count; i++)
    {
        if (EARLIER(*timestamp, dec->ts_buf.ts_data[i]))
        {
            break;
        }
    }

    if (i < dec->ts_buf.count)
    {
        memmove(&dec->ts_buf.ts_data[i+1], &dec->ts_buf.ts_data[i], sizeof(OMX_TICKS)*(dec->ts_buf.count-i));
    }

    dec->ts_buf.count++;

    //ts_queue.ticks[i] = timestamp;
    memcpy(&dec->ts_buf.ts_data[i], timestamp, sizeof(OMX_TICKS));
    DBGT_PDEBUG("Received timestamp: %lld count: %d", *timestamp, (int)dec->ts_buf.count);
}

static void pop_timestamp(OMX_DECODER * dec, OMX_TICKS *timestamp)
{
    if (dec->ts_buf.count == 0)
        return;

    if (timestamp != NULL)
    {
        memcpy(timestamp, &dec->ts_buf.ts_data[0], sizeof(OMX_TICKS));
    }

    if (dec->ts_buf.count > 1)
    {
        memmove(&dec->ts_buf.ts_data[0], &dec->ts_buf.ts_data[1], sizeof(OMX_TICKS)*(dec->ts_buf.count-1));
    }

    dec->ts_buf.count--;
    DBGT_PDEBUG("Pop timestamp %lld count %d", *timestamp, (int)dec->ts_buf.count);
}

static void flush_timestamps(OMX_DECODER * dec)
{
    if (dec->ts_buf.count == 0)
        return;

    while (dec->ts_buf.count > 0)
    {
        memset(&dec->ts_buf.ts_data[dec->ts_buf.count-1], 0, sizeof(OMX_TICKS));
        DBGT_PDEBUG("Clear timestamp buffer %d", (int)dec->ts_buf.count);
        dec->ts_buf.count--;
    }
}
#endif


static OMX_ERRORTYPE grow_propagate_buffer(OMX_DECODER * dec, PROPAGATE_BUFFER * fb,
                                    OMX_U32 capacity)
{
    UNUSED_PARAMETER(dec);
    CALLSTACK;
    DBGT_ASSERT(capacity >= fb->count);

    PROPAGATE_BUFFER new_pb;

    memset(&new_pb, 0, sizeof(PROPAGATE_BUFFER));
    new_pb.propagate_data = malloc(capacity*sizeof(PROPAGATE_INPUT_DATA));

    memcpy(new_pb.propagate_data, fb->propagate_data, fb->count*sizeof(PROPAGATE_INPUT_DATA));
    new_pb.capacity = capacity;
    new_pb.count = fb->count;
    free(fb->propagate_data);

    *fb = new_pb;
    return OMX_ErrorNone;
}


static void receive_propagate_data(OMX_DECODER * dec, PROPAGATE_INPUT_DATA *propagate_data)
{
    OMX_U32 i;
    PROPAGATE_BUFFER *temp = &dec->propagate_buf;
    if (temp->count >= MAX_PROPAGATE_BUFFER_SIZE)
    {
        memmove(&temp->propagate_data[0],
                &temp->propagate_data[1],
                sizeof(PROPAGATE_INPUT_DATA)*(temp->count-1));
        temp->count--;
    }

    if (temp->count >= temp->capacity)
    {
        OMX_U32 capacity = temp->capacity + 16;
        grow_propagate_buffer(dec, temp, capacity);
    }

    i = dec->propagate_buf.count;

    dec->propagate_buf.count++;

    memcpy(&dec->propagate_buf.propagate_data[i], propagate_data, sizeof(PROPAGATE_INPUT_DATA));

    // the propagate_data is ordered by picIndex in propagate_buf(small to big)
    dec->oldestPicIdInBuf = temp->propagate_data[0].picIndex;

    DBGT_PDEBUG("Received timestamp: %lld count: %d", propagate_data->ts_data, (int)dec->ts_buf.count);
}



static OMX_BOOL pop_propagate_data(OMX_DECODER * dec,
                                   PROPAGATE_INPUT_DATA *propagate_data,
                                   OMX_U32 picIndex)
{
    OMX_U32 i;

    if (propagate_data != NULL)
    {
        for (i = 0; i < dec->propagate_buf.count; i++)
        {
            if (picIndex == dec->propagate_buf.propagate_data[i].picIndex)
            {
                memcpy(propagate_data,
                       &dec->propagate_buf.propagate_data[i],
                       sizeof(PROPAGATE_INPUT_DATA));
                break;
            }
        }

        if (i < dec->propagate_buf.count)
        {
            memmove(&dec->propagate_buf.propagate_data[i],
                    &dec->propagate_buf.propagate_data[i+1],
                    sizeof(PROPAGATE_INPUT_DATA)*(dec->propagate_buf.count-i-1));

            dec->propagate_buf.count--;
            dec->prevPicIdList[dec->prevPicIdWritePos++] = picIndex;
            dec->prevPicIdWritePos = (dec->prevPicIdWritePos == 64) ?
                                      0 : dec->prevPicIdWritePos;
            DBGT_PDEBUG("Pop timestamp %lld count %d", propagate_data->ts_data, (int)dec->propagate_buf.count);
            return OMX_TRUE;
        }

        for (i = 0; i < 64; i++)
        {
            if (dec->prevPicIdList[i] == picIndex)
                return OMX_TRUE;
        }
    }
    return OMX_FALSE;
}

static void flush_propagate_data(OMX_DECODER * dec)
{
    if (dec->propagate_buf.count == 0)
        return;

    while (dec->propagate_buf.count > 0)
    {
        memset(&dec->propagate_buf.propagate_data[dec->propagate_buf.count-1],
               0, sizeof(PROPAGATE_INPUT_DATA));
        DBGT_PDEBUG("Clear timestamp buffer %d", (int)dec->propagate_buf.count);
        dec->propagate_buf.count--;
    }
}


#ifdef SET_OUTPUT_CROP_RECT
static void set_output_cropping(OMX_DECODER * dec, OMX_S32 nLeft, OMX_S32 nTop,
                                OMX_U32 nWidth, OMX_U32 nHeight)
{
    dec->output_cropping_rect.nLeft = nLeft;
    dec->output_cropping_rect.nTop = nTop;
    dec->output_cropping_rect.nWidth = nWidth;
    dec->output_cropping_rect.nHeight = nHeight;

    dec->callbacks.EventHandler(dec->self, dec->appdata,
            OMX_EventPortSettingsChanged, PORT_INDEX_OUTPUT,
            OMX_IndexConfigCommonOutputCrop, NULL);
}
#endif

static OMX_ERRORTYPE async_decoder_set_state(OMX_COMMANDTYPE, OMX_U32, OMX_PTR,
                                             OMX_PTR);
static OMX_ERRORTYPE async_decoder_disable_port(OMX_COMMANDTYPE, OMX_U32,
                                                OMX_PTR, OMX_PTR);
static OMX_ERRORTYPE async_decoder_enable_port(OMX_COMMANDTYPE, OMX_U32,
                                               OMX_PTR, OMX_PTR);
static OMX_ERRORTYPE async_decoder_flush_port(OMX_COMMANDTYPE, OMX_U32, OMX_PTR,
                                              OMX_PTR);
static OMX_ERRORTYPE async_decoder_mark_buffer(OMX_COMMANDTYPE, OMX_U32,
                                               OMX_PTR, OMX_PTR);
static OMX_ERRORTYPE async_decoder_decode(OMX_DECODER * dec);
static OMX_ERRORTYPE async_decoder_set_mask(OMX_DECODER * dec);
static OMX_ERRORTYPE async_get_frame_buffer(OMX_DECODER * dec, FRAME * frm);

static OMX_U32 decoder_thread_main(BASECOMP * base, OMX_PTR arg)
{
    UNUSED_PARAMETER(base);
    CALLSTACK;

    DBGT_PROLOG("");

    DBGT_ASSERT(arg);
    OMX_DECODER *this = (OMX_DECODER *) arg;

    OMX_HANDLETYPE handles[3];

    handles[0] = this->base.queue.event;    // event handle for the command queue
    handles[1] = this->in.bufferevent;      // event handle for the normal input port input buffer queue
    handles[2] = this->inpp.bufferevent;    // event handle for the post-processor port input buffer queue

    OMX_ERRORTYPE err = OMX_ErrorNone;

    OSAL_BOOL timeout = OSAL_FALSE;

    OSAL_BOOL signals[3];

    while(this->run)
    {
        // clear all signal indicators
        signals[0] = OSAL_FALSE;
        signals[1] = OSAL_FALSE;
        signals[2] = OSAL_FALSE;

        // wait for command messages and buffers
        DBGT_PDEBUG("Thread wait");
        err = OSAL_EventWaitMultiple(handles, signals, 3, INFINITE_WAIT, &timeout);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("ASYNC: waiting for events failed: %s",
                    HantroOmx_str_omx_err(err));
            break;
        }
        DBGT_PDEBUG("Thread event received");

        if (signals[0] == OSAL_TRUE)
        {
            CMD cmd;

            while(1)
            {
                OMX_BOOL ok = OMX_TRUE;

                err =
                    HantroOmx_basecomp_try_recv_command(&this->base, &cmd, &ok);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("ASYNC: basecomp_try_recv_command failed: %s",
                                HantroOmx_str_omx_err(err));
                    this->run = OMX_FALSE;
                    break;
                }
                if (ok == OMX_FALSE)
                    break;
                if (cmd.type == CMD_EXIT_LOOP)
                {
                    DBGT_PDEBUG("ASYNC: got CMD_EXIT_LOOP");
                    this->run = OMX_FALSE;
                    break;
                }
                HantroOmx_cmd_dispatch(&cmd, this);
            }
            continue;
        }
        if (signals[1] == OSAL_TRUE || signals[2] == OSAL_TRUE)
        {
            if (signals[2] == OSAL_TRUE && this->state == OMX_StateExecuting)
                // got a new input buffer for the post processor!
                async_decoder_set_mask(this);

            if (signals[1] == OSAL_TRUE && this->state == OMX_StateExecuting &&
                !this->portReconfigPending)

                // got a new input buffer, process it
                async_decoder_decode(this);
        }
    }
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("ASYNC: error: %s", HantroOmx_str_omx_err(err));
        DBGT_PDEBUG("ASYNC: new state: %s",
                    HantroOmx_str_omx_state(OMX_StateInvalid));
        this->state = OMX_StateInvalid;
        this->callbacks.EventHandler(this->self, this->appdata, OMX_EventError,
                                     OMX_ErrorInvalidState, 0, NULL);
    }
    DBGT_EPILOG("");
    return 0;
}

static
    OMX_ERRORTYPE decoder_get_version(OMX_IN OMX_HANDLETYPE hComponent,
                                      OMX_OUT OMX_STRING pComponentName,
                                      OMX_OUT OMX_VERSIONTYPE *
                                      pComponentVersion,
                                      OMX_OUT OMX_VERSIONTYPE * pSpecVersion,
                                      OMX_OUT OMX_UUIDTYPE * pComponentUUID)
{
    CALLSTACK;

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pComponentName);
    CHECK_PARAM_NON_NULL(pComponentVersion);
    CHECK_PARAM_NON_NULL(pSpecVersion);
    CHECK_PARAM_NON_NULL(pComponentUUID);

    //OMX_DECODER *dec = GET_DECODER(hComponent);

    DBGT_PROLOG("");

#ifdef OMX_DECODER_VIDEO_DOMAIN
    strncpy(pComponentName, COMPONENT_NAME_VIDEO, OMX_MAX_STRINGNAME_SIZE - 1);
#else
    strncpy(pComponentName, COMPONENT_NAME_IMAGE, OMX_MAX_STRINGNAME_SIZE - 1);
#endif

    pComponentVersion->s.nVersionMajor = COMPONENT_VERSION_MAJOR;
    pComponentVersion->s.nVersionMinor = COMPONENT_VERSION_MINOR;
    pComponentVersion->s.nRevision = COMPONENT_VERSION_REVISION;
    pComponentVersion->s.nStep = COMPONENT_VERSION_STEP;

    pSpecVersion->s.nVersionMajor = 1;  // this is the OpenMAX IL version. Has nothing to do with component version.
    pSpecVersion->s.nVersionMinor = 1;
    pSpecVersion->s.nRevision = 2;
    pSpecVersion->s.nStep = 0;

    HantroOmx_generate_uuid(hComponent, pComponentUUID);

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static void decoder_dealloc_buffers(OMX_DECODER * dec, PORT * p)
{
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(p);
    OMX_U32 count = HantroOmx_port_buffer_count(p);

    OMX_U32 i;

    for(i = 0; i < count; ++i)
    {
        BUFFER *buff = NULL;

        HantroOmx_port_get_allocated_buffer_at(p, &buff, i);
        DBGT_ASSERT(buff);
        if (buff->flags & BUFFER_FLAG_MY_BUFFER)
        {
            DBGT_ASSERT(buff->bus_address);
            DBGT_ASSERT(buff->bus_data);
            OSAL_AllocatorFreeMem(&dec->alloc, buff->allocsize, buff->bus_data,
                                  buff->bus_address);
        }
    }
    DBGT_EPILOG("");
}

static OMX_ERRORTYPE decoder_deinit(OMX_IN OMX_HANDLETYPE hComponent)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);

    if (((OMX_COMPONENTTYPE *) hComponent)->pComponentPrivate == NULL)
    {
        // play nice and handle destroying a component that was never created nicely...
        DBGT_PDEBUG("API: pComponentPrivate == NULL");
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    OMX_DECODER *dec = GET_DECODER(hComponent);
#ifdef IS_G2_DECODER
    if (!dec->shared_data.EOS && dec->codec)
    {
        DBGT_PDEBUG("Sending EOS to the codec");
        dec->codec->endofstream(dec->codec);
    }

    if (dec->shared_data.output_thread_)
    {
        DBGT_PDEBUG("Join output thread");
        pthread_join(dec->shared_data.output_thread_, NULL);
    }
#else
    if (dec->useExternalAlloc == OMX_TRUE)
    {
        if (!dec->shared_data.EOS && dec->codec)
        {
            DBGT_PDEBUG("Sending EOS to the codec");
            dec->codec->endofstream(dec->codec);
        }

        if (dec->shared_data.output_thread_)
        {
            DBGT_PDEBUG("Join output thread");
            pthread_join(dec->shared_data.output_thread_, NULL);
        }
    }
#endif
    DBGT_PDEBUG("API: waiting for thread to finish");
    DBGT_PDEBUG("API: current state: %s",
                HantroOmx_str_omx_state(dec->state));

    if (dec->base.thread)
    {
        // bring down the component thread, and join it
        dec->run = OMX_FALSE;
        CMD c;

        INIT_EXIT_CMD(c);
        HantroOmx_basecomp_send_command(&dec->base, &c);
        OSAL_ThreadSleep(RETRY_INTERVAL);
        HantroOmx_basecomp_destroy(&dec->base);
    }

    DBGT_ASSERT(HantroOmx_port_is_allocated(&dec->in) == OMX_TRUE);
    DBGT_ASSERT(HantroOmx_port_is_allocated(&dec->out) == OMX_TRUE);
    DBGT_ASSERT(HantroOmx_port_is_allocated(&dec->inpp) == OMX_TRUE);

    //if (dec->state == OMX_StateInvalid)
    if (dec->state != OMX_StateLoaded)
    {
        // if there's stuff in the input/output port buffer queues
        // simply ignore it

        // deallocate allocated buffers (if any)
        // this could have catastrophic consequences if someone somewhere is
        // is still holding a pointer to these buffers... (what could be done here, except leak?)

        decoder_dealloc_buffers(dec, &dec->in);
        decoder_dealloc_buffers(dec, &dec->out);
        decoder_dealloc_buffers(dec, &dec->inpp);
        DBGT_PDEBUG("API: delloc buffers done");
        if (dec->codec)
            dec->codec->destroy(dec->codec);

        if (dec->frame_in.bus_address)
            FRAME_BUFF_FREE(&dec->alloc, &dec->frame_in);
        if (dec->frame_out.bus_address)
            FRAME_BUFF_FREE(&dec->alloc, &dec->frame_out);
        if (dec->mask.bus_address)
            FRAME_BUFF_FREE(&dec->alloc, &dec->mask);

        // free time stamp buffer queue.
#if 0
        if (dec->ts_buf.ts_data)
            free(dec->ts_buf.ts_data);
#endif
        if (dec->propagate_buf.propagate_data)
            free(dec->propagate_buf.propagate_data);

        DBGT_PDEBUG("API: dealloc frame buffers done");
    }
    else
    {
        // ports should not have any queued buffers at this point anymore.
        // if there are this is a programming error somewhere.
        DBGT_ASSERT(HantroOmx_port_buffer_queue_count(&dec->in) == 0);
        DBGT_ASSERT(HantroOmx_port_buffer_queue_count(&dec->out) == 0);
        DBGT_ASSERT(HantroOmx_port_buffer_queue_count(&dec->inpp) == 0);

        // ports should not have any buffers allocated
        // at this point anymore
        DBGT_ASSERT(HantroOmx_port_has_buffers(&dec->in) == OMX_FALSE);
        DBGT_ASSERT(HantroOmx_port_has_buffers(&dec->out) == OMX_FALSE);
        DBGT_ASSERT(HantroOmx_port_has_buffers(&dec->inpp) == OMX_FALSE);

        // temporary frame buffers should not exist anymore
        DBGT_ASSERT(dec->frame_in.bus_data == NULL);
        DBGT_ASSERT(dec->frame_out.bus_data == NULL);
        DBGT_ASSERT(dec->ts_buf.ts_data == NULL);
        DBGT_ASSERT(dec->mask.bus_data == NULL);
    }
    HantroOmx_port_destroy(&dec->in);
    HantroOmx_port_destroy(&dec->out);
    HantroOmx_port_destroy(&dec->inpp);

    if (dec->statemutex)
        OSAL_MutexDestroy(dec->statemutex);

    if (dec->timemutex)
        OSAL_MutexDestroy(dec->timemutex);

    if (dec->threadmutex)
        OSAL_MutexDestroy(dec->threadmutex);

    OSAL_AllocatorDestroy(&dec->alloc);
    OSAL_Free(dec);
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_send_command(OMX_IN OMX_HANDLETYPE hComponent,
                                       OMX_IN OMX_COMMANDTYPE Cmd,
                                       OMX_IN OMX_U32 nParam1,
                                       OMX_IN OMX_PTR pCmdData)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);

    DBGT_PDEBUG("API: Cmd:%s nParam1:%u pCmdData:%p",
                HantroOmx_str_omx_cmd(Cmd), (unsigned) nParam1, pCmdData);

    CMD c;

    OMX_ERRORTYPE err = OMX_ErrorNotImplemented;

    switch (Cmd)
    {
        case OMX_CommandStateSet:
            if (dec->statetrans != dec->state)
            {
                // transition is already pending
                DBGT_CRITICAL("Transition is already pending (dec->statetrans != dec->state)");
                return OMX_ErrorIncorrectStateTransition;
            }
            DBGT_PDEBUG("API: next state:%s",
                        HantroOmx_str_omx_state((OMX_STATETYPE) nParam1));
            INIT_SEND_CMD(c, Cmd, nParam1, NULL, async_decoder_set_state);
            dec->statetrans = (OMX_STATETYPE) nParam1;

            if (dec->statetrans == OMX_StateIdle && dec->state == OMX_StateExecuting)
            {
                DBGT_PDEBUG("State transition: Executing -> Idle detected");
                dec->releaseBuffers = OMX_TRUE;
                DBGT_PDEBUG("releaseBuffers -> TRUE");
                // abort the decoder
                dec->codec->abort(dec->codec);
            }
            else if (dec->statetrans == OMX_StateLoaded && dec->state == OMX_StateIdle)
            {
                DBGT_PDEBUG("State transition: Idle -> Loaded detected");
            //    if(dec->releaseBuffers)
            //        dec->codec->stop(dec->codec);
            }
            err = HantroOmx_basecomp_send_command(&dec->base, &c);
            break;

        case OMX_CommandFlush:
            if (nParam1 > PORT_INDEX_PP && nParam1 != OMX_ALL)
            {
                DBGT_CRITICAL("API: bad port index:%u", (unsigned) nParam1);
                return OMX_ErrorBadPortIndex;
            }
            if (nParam1 == PORT_INDEX_OUTPUT || nParam1 == OMX_ALL)
                dec->flushingOutPort = OMX_TRUE;

            dec->releaseBuffers = OMX_TRUE;
            dec->codec->abort(dec->codec);

            INIT_SEND_CMD(c, Cmd, nParam1, NULL, async_decoder_flush_port);
            err = HantroOmx_basecomp_send_command(&dec->base, &c);
            break;

        case OMX_CommandPortDisable:
            if (nParam1 > PORT_INDEX_PP && nParam1 != OMX_ALL)
            {
                DBGT_CRITICAL("API: bad port index:%u", (unsigned) nParam1);
                return OMX_ErrorBadPortIndex;
            }
            if (nParam1 == PORT_INDEX_INPUT || nParam1 == OMX_ALL)
                dec->in.def.bEnabled = OMX_FALSE;
            if (nParam1 == PORT_INDEX_OUTPUT || nParam1 == OMX_ALL)
            {
                dec->out.def.bEnabled = OMX_FALSE;
                dec->disablingOutPort = OMX_TRUE;
            }
            if (nParam1 == PORT_INDEX_PP || nParam1 == OMX_ALL)
                dec->inpp.def.bEnabled = OMX_FALSE;

            INIT_SEND_CMD(c, Cmd, nParam1, NULL, async_decoder_disable_port);
            err = HantroOmx_basecomp_send_command(&dec->base, &c);
            break;

        case OMX_CommandPortEnable:
            if (nParam1 > PORT_INDEX_PP && nParam1 != OMX_ALL)
            {
                DBGT_CRITICAL("API: bad port index:%u", (unsigned) nParam1);
                return OMX_ErrorBadPortIndex;
            }
            if (nParam1 == PORT_INDEX_INPUT || nParam1 == OMX_ALL)
                dec->in.def.bEnabled = OMX_TRUE;
            if (nParam1 == PORT_INDEX_OUTPUT || nParam1 == OMX_ALL)
                dec->out.def.bEnabled = OMX_TRUE;
            if (nParam1 == PORT_INDEX_PP || nParam1 == OMX_ALL)
                dec->inpp.def.bEnabled = OMX_TRUE;

            INIT_SEND_CMD(c, Cmd, nParam1, NULL, async_decoder_enable_port);
            err = HantroOmx_basecomp_send_command(&dec->base, &c);
            break;

        case OMX_CommandMarkBuffer:
            if ((nParam1 != PORT_INDEX_INPUT) && (nParam1 != PORT_INDEX_PP))
            {
                DBGT_CRITICAL("API: bad port index:%u", (unsigned) nParam1);
                return OMX_ErrorBadPortIndex;
            }
            CHECK_PARAM_NON_NULL(pCmdData);
            OMX_MARKTYPE *mark = (OMX_MARKTYPE *) OSAL_Malloc(sizeof(OMX_MARKTYPE));

            if (!mark)
            {
                DBGT_CRITICAL("API: cannot marshall mark (OMX_ErrorInsufficientResources)");
                return OMX_ErrorInsufficientResources;
            }
            memcpy(mark, pCmdData, sizeof(OMX_MARKTYPE));
            INIT_SEND_CMD(c, Cmd, nParam1, mark, async_decoder_mark_buffer);
            err = HantroOmx_basecomp_send_command(&dec->base, &c);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("HantroOmx_basecomp_send_command failed");
                OSAL_Free(mark);
            }
            break;

        default:
            DBGT_ERROR("API: bad command: %s (%u)", HantroOmx_str_omx_cmd(Cmd), (unsigned) Cmd);
            err = OMX_ErrorBadParameter;
            break;
    }
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("API: error: %s", HantroOmx_str_omx_err(err));
    }
    DBGT_EPILOG("");
    return err;
}

static
    OMX_ERRORTYPE decoder_set_callbacks(OMX_IN OMX_HANDLETYPE hComponent,
                                        OMX_IN OMX_CALLBACKTYPE * pCallbacks,
                                        OMX_IN OMX_PTR pAppData)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pCallbacks);
    OMX_DECODER *dec = GET_DECODER(hComponent);

    DBGT_PDEBUG("API: pCallbacks:%p pAppData:%p", pCallbacks,
                pAppData);

    dec->callbacks = *pCallbacks;
    dec->appdata = pAppData;

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_get_state(OMX_IN OMX_HANDLETYPE hComponent,
                                    OMX_OUT OMX_STATETYPE * pState)
{
    CALLSTACK;
    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pState);
    OMX_DECODER *dec = GET_DECODER(hComponent);

    OSAL_MutexLock(dec->statemutex);
    *pState = dec->state;
    OSAL_MutexUnlock(dec->statemutex);
    return OMX_ErrorNone;
}

static PORT *decoder_map_index_to_port(OMX_DECODER * dec, OMX_U32 portIndex)
{
    CALLSTACK;
    switch (portIndex)
    {
        case PORT_INDEX_INPUT:
            return &dec->in;
        case PORT_INDEX_OUTPUT:
            return &dec->out;
        case PORT_INDEX_PP:
            return &dec->inpp;
    }
    return NULL;
}

static
    OMX_ERRORTYPE decoder_verify_buffer_allocation(OMX_DECODER * dec, PORT * p,
                                                   OMX_U32 buffSize)
{
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(dec);
    DBGT_ASSERT(p);

    OMX_ERRORTYPE err = OMX_ErrorIncorrectStateOperation;

    // buffers can only be allocated when the component is in one of the following states:
    // 1. OMX_StateLoaded and has already sent a request for state transition to OMX_StateIdle
    // 2. OMX_StateWaitForResources, the resources are available and the component
    //    is ready to go to the OMX_StateIdle state
    // 3. OMX_StateExecuting, OMX_StatePause or OMX_StateIdle and the port is disabled.
    if (p->def.bPopulated)
    {
        DBGT_WARNING("API: port is already populated");
        DBGT_EPILOG("");
        return err;
    }

    // No need check bufsize for vp9 dynamic port reconfig
    if (dec->ReallocBufferAddress == 0)
    {
        if (buffSize < p->def.nBufferSize)
        {
            DBGT_ERROR("API: buffer is too small, required:%u given:%u",
                        (unsigned) p->def.nBufferSize, (unsigned) buffSize);
            DBGT_EPILOG("");
            return OMX_ErrorBadParameter;
        }
    }

    // 3.2.2.15
    switch (dec->state)
    {
        case OMX_StateLoaded:
            if (dec->statetrans != OMX_StateIdle)
            {
                DBGT_ERROR("API: not in transition to idle");
                DBGT_EPILOG("");
                return err;
            }
            break;
        case OMX_StateWaitForResources:
            DBGT_CRITICAL("OMX_StateWaitForResources not implemented");
            DBGT_EPILOG("");
            return OMX_ErrorNotImplemented;

            //
            // These cases are in disagreement with the OMX_AllocateBuffer definition
            // in the specification in chapter 3.2.2.15. (And that chapter in turn seems to be
            // conflicting with chapter 3.2.2.6.)
            //
            // The bottom line is that the conformance tester sets the component to these states
            // and then wants to disable and enable ports and then allocate buffers. For example
            // Conformance tester PortCommunicationTest sets the component into executing state,
            // then disables all ports (-1), frees all buffers on those ports and finally tries
            // to enable all ports (-1), and then allocate buffers.
            // The specification says that if component is in executing state (or pause or idle)
            // the port must be disabled when allocating a buffer, but in order to pass the test
            // we must violate that requirement.
            //
            // A common guideline seems to be that when the tester and specification are in disagreement
            // the tester wins.
            //
        case OMX_StateIdle:
        case OMX_StateExecuting:
            break;
        default:
            if (p->def.bEnabled)
            {
                DBGT_CRITICAL("API: port is not disabled");
                DBGT_EPILOG("");
                return err;
            }
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_use_buffer(OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_INOUT OMX_BUFFERHEADERTYPE ** ppBuffer,
                                     OMX_IN OMX_U32 nPortIndex,
                                     OMX_IN OMX_PTR pAppPrivate,
                                     OMX_IN OMX_U32 nSizeBytes,
                                     OMX_IN OMX_U8 * pBuffer)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(ppBuffer);
    CHECK_PARAM_NON_NULL(pBuffer);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);

    DBGT_PDEBUG("API: nPortIndex:%u pAppPrivate:%p nSizeBytes:%u pBuffer:%p",
                (unsigned) nPortIndex, pAppPrivate, (unsigned) nSizeBytes,
                pBuffer);

    PORT *port = decoder_map_index_to_port(dec, nPortIndex);

    if (port == NULL)
    {
        DBGT_CRITICAL("API: bad port index:%u", (unsigned) nPortIndex);
        DBGT_EPILOG("");
        return OMX_ErrorBadPortIndex;
    }
    OMX_ERRORTYPE err = decoder_verify_buffer_allocation(dec, port, nSizeBytes);

    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("decoder_verify_buffer_allocation (err=%x)", err);
        DBGT_EPILOG("");
        return err;
    }

    DBGT_PDEBUG("API: port index:%u", (unsigned) nPortIndex);
    DBGT_PDEBUG("API: buffer size:%u", (unsigned) nSizeBytes);

    BUFFER *buff = NULL;

    HantroOmx_port_allocate_next_buffer(port, &buff);
    if (buff == NULL)
    {
        DBGT_CRITICAL("API: HantroOmx_port_allocate_next_buffer: no more buffers");
        DBGT_EPILOG("");
        return OMX_ErrorInsufficientResources;
    }

    // save the pointer here into the header. The data in this buffer
    // needs to be copied in to the DMA accessible frame buffer before decoding.

    INIT_OMX_VERSION_PARAM(*buff->header);
    buff->flags &= ~BUFFER_FLAG_MY_BUFFER;

#ifndef USE_TEMP_INPUT_BUFFER
    if (nPortIndex == 0)
        buff->flags |= BUFFER_FLAG_EXT_ALLOC;
#endif

#ifndef USE_TEMP_OUTPUT_BUFFER
    if (nPortIndex == 1)
        buff->flags |= BUFFER_FLAG_EXT_ALLOC;
#endif

#ifdef USE_ANDROID_NATIVE_BUFFER
    int physAddr, logAddr;
    void* pHdl;

    if (port->useAndroidNativeBuffer == OMX_TRUE)
    {
        DBGT_PDEBUG("API: use Android Native Buffer");
        buff->flags |= BUFFER_FLAG_ANDROID_NATIVE_BUFFER;
        if (!GetAddressFromNativeBuffer((void *) pBuffer, (unsigned long *)&physAddr, (unsigned long *)&logAddr, &pHdl))
        {
            DBGT_PDEBUG("API: Get nativebuffer address: phys=%x log=%x", physAddr, logAddr);
        }
        else
        {
            DBGT_CRITICAL("API: Error: get native buffer address");
            DBGT_EPILOG("");
            return OMX_ErrorUndefined;
        }
        buff->header->pBuffer = pBuffer;
        buff->bus_data = (OMX_U8*)logAddr;
        buff->bus_address = physAddr;
        buff->native_buffer_hdl = pHdl;
        //buff->bus_address = logAddr; // Uncomment this if you use SOFT model
    }
    else
#endif
    {
#ifdef USE_ALLOC_PRIVATE
        buff->header->pBuffer = pBuffer;
        buff->bus_data = pBuffer;
        buff->bus_address = 0;  // bus address is read from pInputPortPrivate
#else
        buff->header->pBuffer = pBuffer;
        buff->bus_data = pBuffer;
        buff->bus_address = (OSAL_BUS_WIDTH)pBuffer;
#endif
    }
    buff->header->pAppPrivate = pAppPrivate;
    buff->header->nAllocLen = nSizeBytes;
    buff->allocsize = nSizeBytes;

    if (HantroOmx_port_buffer_count(port) == port->def.nBufferCountActual)
    {
        DBGT_PDEBUG("API: port is populated");
        HantroOmx_port_lock_buffers(port);
        port->def.bPopulated = OMX_TRUE;
        OMX_ERRORTYPE err = OSAL_EventSet(port->bufferRdy);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("OSAL_EventSet failed");
            DBGT_EPILOG("");
            HantroOmx_port_unlock_buffers(port);
            return err;
        }
        HantroOmx_port_unlock_buffers(port);
    }
    if (nPortIndex == PORT_INDEX_INPUT || nPortIndex == PORT_INDEX_PP)
    {
        buff->header->nInputPortIndex = nPortIndex;
        buff->header->nOutputPortIndex = 0;
    }
    else
    {
        buff->header->nInputPortIndex = 0;
        buff->header->nOutputPortIndex = nPortIndex;
    }

    *ppBuffer = buff->header;
    DBGT_PDEBUG("API: pBufferHeader:%p", *ppBuffer);

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_allocate_buffer(OMX_IN OMX_HANDLETYPE hComponent,
                                          OMX_INOUT OMX_BUFFERHEADERTYPE **
                                          ppBuffer, OMX_IN OMX_U32 nPortIndex,
                                          OMX_IN OMX_PTR pAppPrivate,
                                          OMX_IN OMX_U32 nSizeBytes)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(ppBuffer);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);

    DBGT_PDEBUG("API: nPortIndex:%u pAppPrivate:%p nSizeBytes:%u",
                (unsigned) nPortIndex, pAppPrivate, (unsigned) nSizeBytes);
    PORT *port = decoder_map_index_to_port(dec, nPortIndex);

    if (port == NULL)
    {
        DBGT_CRITICAL("API: bad port index:%u", (unsigned) nPortIndex);
        DBGT_EPILOG("");
        return OMX_ErrorBadPortIndex;
    }
    OMX_ERRORTYPE err = decoder_verify_buffer_allocation(dec, port, nSizeBytes);

    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("decoder_verify_buffer_allocation (err=%x)", err);
        dec->state = OMX_StateInvalid;
        DBGT_EPILOG("");
        return err;
    }

    DBGT_PDEBUG("API: port index:%u", (unsigned) nPortIndex);
    DBGT_PDEBUG("API: buffer size:%u", (unsigned) nSizeBytes);

    // about locking here.
    // Only in case of tunneling is the component thread accessing the port's
    // buffer's directly. However this function should not be called at that time.
    // note: perhaps we could lock/somehow make sure that a misbehaving client thread
    // wont get a change to mess everything up?

    OMX_U8 *bus_data = NULL;

    OSAL_BUS_WIDTH bus_address = 0;

    OMX_U32 allocsize = nSizeBytes;

    // the conformance tester assumes that the allocated buffers equal nSizeBytes in size.
    // however when running on HW this might not be a valid assumption because the memory allocator
    // allocates memory always in fixed size chunks.
    DBGT_PDEBUG("decoder_allocate_buffer: OSAL_AllocatorAllocMem");

    err = OSAL_AllocatorAllocMem(&dec->alloc, &allocsize, &bus_data,
                               &bus_address);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("API: memory allocation failed: %s", HantroOmx_str_omx_err(err));
        DBGT_EPILOG("");
        return err;
    }

    DBGT_ASSERT(allocsize >= nSizeBytes);
    DBGT_PDEBUG("API: allocated buffer size:%u @physical addr:0x%08lx @logical addr:%p",
                (unsigned) allocsize, bus_address, bus_data);

    BUFFER *buff = NULL;

    DBGT_PDEBUG("decoder_allocate_buffer: HantroOmx_port_allocate_next_buffer");
    HantroOmx_port_allocate_next_buffer(port, &buff);
    if (!buff)
    {
        DBGT_CRITICAL("API: no more buffers");
        OSAL_AllocatorFreeMem(&dec->alloc, nSizeBytes, bus_data, bus_address);
        DBGT_EPILOG("");
        return OMX_ErrorInsufficientResources;
    }

    INIT_OMX_VERSION_PARAM(*buff->header);
    buff->flags |= BUFFER_FLAG_MY_BUFFER;
    buff->bus_data = bus_data;
    buff->bus_address = bus_address;
    buff->allocsize = allocsize;
    buff->header->pBuffer = bus_data;
    buff->header->pAppPrivate = pAppPrivate;
    buff->header->nAllocLen = nSizeBytes;   // the conformance tester assumes that the allocated buffers equal nSizeBytes in size.
    if (nPortIndex == PORT_INDEX_INPUT || nPortIndex == PORT_INDEX_PP)
    {
        buff->header->nInputPortIndex = nPortIndex;
        buff->header->nOutputPortIndex = 0;
    }
    else
    {
        buff->header->nInputPortIndex = 0;
        buff->header->nOutputPortIndex = nPortIndex;
    }
#ifdef USE_ALLOC_PRIVATE
    dec->bufPriv.pBufferData = bus_data;
    dec->bufPriv.nBusAddress = bus_address;
    dec->bufPriv.nBufferSize = allocsize;
    buff->header->pInputPortPrivate = &dec->bufPriv;
#endif
    if (HantroOmx_port_buffer_count(port) == port->def.nBufferCountActual)
    {
        HantroOmx_port_lock_buffers(port);
        port->def.bPopulated = OMX_TRUE;
        OMX_ERRORTYPE err = OSAL_EventSet(port->bufferRdy);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("OSAL_EventSet failed");
            DBGT_EPILOG("");
            HantroOmx_port_unlock_buffers(port);
            return err;
        }
        HantroOmx_port_unlock_buffers(port);
        DBGT_PDEBUG("API: port is populated");
    }
    *ppBuffer = buff->header;
    DBGT_PDEBUG("API: data (virtual address):%p pBufferHeader:%p", bus_data, *ppBuffer);

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_free_buffer(OMX_IN OMX_HANDLETYPE hComponent,
                                      OMX_IN OMX_U32 nPortIndex,
                                      OMX_IN OMX_BUFFERHEADERTYPE *
                                      pBufferHeader)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pBufferHeader);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    DBGT_PDEBUG("API: nPortIndex:%lu pBufferHeader:%p, pBuffer:%p",
                 nPortIndex, pBufferHeader, pBufferHeader->pBuffer);

    // 3.2.2.16
    // The call should be performed under the following conditions:
    // o  While the component is in the OMX_StateIdle state and the IL client has already
    //    sent a request for the state transition to OMX_StateLoaded (e.g., during the
    //    stopping of the component)
    // o  On a disabled port when the component is in the OMX_StateExecuting, the
    //    OMX_StatePause, or the OMX_StateIdle state.

    // the above is "should" only, in other words meaning that free buffer CAN be performed
    // at anytime. However the conformance tester expects that the component generates
    // an OMX_ErrorPortUnpopulated event, even though in the specification this is described as "may generate OMX_ErrorPortUnpopulated".

    PORT *port = decoder_map_index_to_port(dec, nPortIndex);

    if (port == NULL)
    {
        DBGT_CRITICAL("API: bad port index:%u", (unsigned) nPortIndex);
        DBGT_EPILOG("");
        return OMX_ErrorBadPortIndex;
    }
    OMX_BOOL violation = OMX_FALSE;

    if (port->def.bEnabled)
    {
        OSAL_MutexLock(dec->statemutex);
        if (dec->state == OMX_StateIdle && dec->statetrans != OMX_StateLoaded)
        {
            violation = OMX_TRUE;
        }
        OSAL_MutexUnlock(dec->statemutex);
    }

    // have to lock the buffers here, cause its possible that
    // we're trying to free up a buffer that is still in fact queued
    // up for the port in the input queue
    // if there is such a buffer this function should then fail gracefully

    // 25.2.2008, locking removed (for the worse). This is because
    // when transitioning to idle state from executing or disabling a port
    // the queued buffers are being returned the queue is locked.
    // However the conformance tester for example calls directly back at us
    // and then locks up.

    //OMX_ERRORTYPE err = port_lock_buffers(port);
    //if (err != OMX_ErrorNone)
    //return err;

    // theres still data queued up on the port
    // could be the same buffers we're freeing here
    //DBGT_ASSERT(port_buffer_queue_count(port) == 0);

    BUFFER *buff = HantroOmx_port_find_buffer(port, pBufferHeader);

    if (!buff)
    {
        DBGT_CRITICAL("API: HantroOmx_port_find_buffer: no such buffer");
        //HantroOmx_port_unlock_buffers(port);
        DBGT_EPILOG("");
        return OMX_ErrorBadParameter;
    }

    if (!(buff->flags & BUFFER_FLAG_IN_USE))
    {
        DBGT_CRITICAL("API: HantroOmx_port_find_buffer: buffer is not allocated");
        //HantroOmx_port_unlock_buffers(port);
        DBGT_EPILOG("");
        return OMX_ErrorBadParameter;
    }

    if (buff->flags & BUFFER_FLAG_MY_BUFFER)
    {
        DBGT_ASSERT(buff->bus_address && buff->bus_data);
        DBGT_ASSERT(buff->bus_data == buff->header->pBuffer);
        DBGT_ASSERT(buff->allocsize);

        OSAL_AllocatorFreeMem(&dec->alloc, buff->allocsize, buff->bus_data,
                              buff->bus_address);
    }

#ifdef USE_ANDROID_NATIVE_BUFFER
    if (buff->flags & BUFFER_FLAG_ANDROID_NATIVE_BUFFER)
    {
        void* pHdl = buff->native_buffer_hdl;

        if (!pHdl)
        {
            DBGT_CRITICAL("Null native buffer handle for buff=%p will leak... (size=%d)",
                           (char*)buff, (int)buff->header->nAllocLen);
        }
        else
        {
            ReleaseNativeBuffer(pHdl);
        }
    }
#endif

    HantroOmx_port_release_buffer(port, buff);

    if (HantroOmx_port_buffer_count(port) < port->def.nBufferCountActual)
    {
        HantroOmx_port_lock_buffers(port);
        port->def.bPopulated = OMX_FALSE;
        OMX_ERRORTYPE err = OSAL_EventReset(port->bufferRdy);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("OSAL_EventReset failed");
            DBGT_EPILOG("");
            HantroOmx_port_unlock_buffers(port);
            return err;
        }
        HantroOmx_port_unlock_buffers(port);
    }

    // remember to unlock!
    //port_unlock_buffers(port);

    // this is a hack because of the conformance tester.
    if (port->def.bPopulated == OMX_FALSE && violation == OMX_TRUE)
    {
        dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError,
                                    OMX_ErrorPortUnpopulated, 0, NULL);
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

#define CHECK_PORT_STATE(decoder, port)                          \
  if ((decoder)->state != OMX_StateLoaded && (port)->bEnabled)   \
  {                                                              \
      DBGT_ERROR("API: CHECK_PORT_STATE, port is not disabled"); \
      return OMX_ErrorIncorrectStateOperation;                   \
  }

#define CHECK_PORT_OUTPUT(param)                              \
  if ((param)->nPortIndex != PORT_INDEX_OUTPUT)               \
  {                                                           \
      DBGT_ERROR("CHECK_PORT_OUTPUT, OMX_ErrorBadPortIndex"); \
      return OMX_ErrorBadPortIndex;                           \
  }

#define CHECK_PORT_INPUT(param)                              \
  if ((param)->nPortIndex != PORT_INDEX_INPUT)               \
  {                                                          \
      DBGT_ERROR("CHECK_PORT_INPUT, OMX_ErrorBadPortIndex"); \
      return OMX_ErrorBadPortIndex;                          \
  }

static
    OMX_ERRORTYPE calculate_output_buffer_size(OMX_U32 width, OMX_U32 height,
                                               OMX_U32 colorFormat, OMX_U32* frameSize)
{
    DBGT_PROLOG("");
    OMX_U32 size;

#ifdef ENABLE_PP
    size = ((width + 7) & -8) * ((height + 1) & -2);
#else
    size = ((width + 15) & -16) * ((height + 15) & -16);
#endif

    switch (colorFormat)
    {
    case OMX_COLOR_FormatL8:
        break;
    case OMX_COLOR_FormatYUV420Planar:
    case OMX_COLOR_FormatYUV420PackedPlanar:
    case OMX_COLOR_FormatYUV420SemiPlanar:
    case OMX_COLOR_FormatYUV420PackedSemiPlanar:
    case OMX_COLOR_FormatYUV411SemiPlanar:
    case OMX_COLOR_FormatYUV411PackedSemiPlanar:
    case OMX_COLOR_FormatYUV420SemiPlanar4x4Tiled:
        *frameSize = size * 3 / 2;
        break;
    case OMX_COLOR_FormatYUV422SemiPlanar:
    case OMX_COLOR_FormatYUV422PackedSemiPlanar:
    case OMX_COLOR_FormatYCbYCr:
    case OMX_COLOR_FormatYCrYCb:
    case OMX_COLOR_FormatCbYCrY:
    case OMX_COLOR_FormatCrYCbY:
    case OMX_COLOR_FormatYUV440SemiPlanar:
    case OMX_COLOR_FormatYUV440PackedSemiPlanar:
        *frameSize = size *= 2;
        break;
    case OMX_COLOR_FormatYUV444SemiPlanar:
    case OMX_COLOR_FormatYUV444PackedSemiPlanar:
        *frameSize = size *= 3;
        break;
    case OMX_COLOR_Format32bitARGB8888:
    case OMX_COLOR_Format32bitBGRA8888:
    case OMX_COLOR_Format25bitARGB1888:
    case OMX_COLOR_Format24bitRGB888:
    case OMX_COLOR_Format24bitBGR888:
        *frameSize = size *= 4;
        break;
    case OMX_COLOR_Format16bitARGB1555:
    case OMX_COLOR_Format16bitRGB565:
    case OMX_COLOR_Format16bitBGR565:
    case OMX_COLOR_Format16bitARGB4444:
        *frameSize = size *= 2;
        break;
    default:
        DBGT_ASSERT(!"Unknown color format");
        DBGT_EPILOG("");
        return OMX_ErrorBadParameter;
    }

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

#if (defined OMX_DECODER_VIDEO_DOMAIN)
static
    OMX_ERRORTYPE decoder_set_parameter(OMX_IN OMX_HANDLETYPE hComponent,
                                        OMX_IN OMX_INDEXTYPE nIndex,
                                        OMX_IN OMX_PTR pParam)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pParam);

    OMX_DECODER *dec = GET_DECODER(hComponent);
    OMX_ERRORTYPE err;

    CHECK_STATE_INVALID(dec->state);
    CHECK_STATE_IDLE(dec->state);

    if (dec->state != OMX_StateLoaded && dec->state != OMX_StateWaitForResources && dec->state != OMX_StateExecuting)
    {
        DBGT_CRITICAL("API: unsupported state: %s", HantroOmx_str_omx_state(dec->state));
        return OMX_ErrorIncorrectStateOperation;
    }

    switch ((OMX_U32)nIndex)
    {
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_PARAM_PORTDEFINITIONTYPE *param =
                (OMX_PARAM_PORTDEFINITIONTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                port = &dec->in.def;
                CHECK_PORT_STATE(dec, port);

                port->nBufferCountActual = param->nBufferCountActual;
                /* port->nBufferSize = param->nBufferSize; // According to OMX IL spec, port buffer size is read only */
#ifndef USE_DEFAULT_INPUT_BUFFER_SIZE
                /* PP input is raw image data, so we can use calculate_output_buffer_size() function */
                if (port->format.video.eCompressionFormat == OMX_VIDEO_CodingUnused)
                {
                    err = calculate_output_buffer_size(param->format.video.nFrameWidth, param->format.video.nFrameHeight,
                                                param->format.video.eColorFormat, &port->nBufferSize);
                }
                /* Calculate input buffer size based on YUV 420 data size and minimum compression ratio 2 */
                else
                {       /* Some VP9 key frames are quite large */
                    if (port->format.video.eCompressionFormat == OMX_VIDEO_CodingVP9)
                        port->nBufferSize = (param->format.video.nFrameWidth * param->format.video.nFrameHeight);
                    else
                        port->nBufferSize = (param->format.video.nFrameWidth * param->format.video.nFrameHeight) * 3/4;
                }

                /* Limit the smallest input buffer size for small resolutions */
                if (port->nBufferSize < MIN_BUFFER_SIZE)
                {
                    port->nBufferSize = MIN_BUFFER_SIZE;
                }
#endif
                if (param->nBufferSize != port->nBufferSize)
                {
                    DBGT_PDEBUG("New input port buffer size: %d", (int)port->nBufferSize);
                }
                port->format.video.eCompressionFormat =
                    param->format.video.eCompressionFormat;
                port->format.video.eColorFormat =
                    param->format.video.eColorFormat;
                port->format.video.bFlagErrorConcealment =
                    param->format.video.bFlagErrorConcealment;
                // need these for raw YUV input
                port->format.video.nFrameWidth =
                    param->format.video.nFrameWidth;
                port->format.video.nFrameHeight =
                    param->format.video.nFrameHeight;
#ifdef CONFORMANCE
                if (strcmp((char*)dec->role, "video_decoder.mpeg4") == 0)
                    port->format.video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
                else if (strcmp((char*)dec->role, "video_decoder.avc") == 0)
                    port->format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
                else if (strcmp((char*)dec->role, "video_decoder.h263") == 0)
                    port->format.video.eCompressionFormat = OMX_VIDEO_CodingH263;
                else if (strcmp((char*)dec->role, "video_decoder.wmv") == 0)
                    port->format.video.eCompressionFormat = OMX_VIDEO_CodingWMV;
                else if (strcmp((char*)dec->role, "video_decoder.vp8") == 0)
                    port->format.video.eCompressionFormat = OMX_VIDEO_CodingVP8;
#endif
                break;

            case PORT_INDEX_OUTPUT:
                port = &dec->out.def;
                CHECK_PORT_STATE(dec, port);

                port->nBufferCountActual = param->nBufferCountActual;
                //port->nBufferSize = param->nBufferSize; // According to OMX IL spec, port buffer size is read only
                port->format.video.eColorFormat =
                    param->format.video.eColorFormat;
                port->format.video.nFrameWidth =
                     param->format.video.nFrameWidth;
                port->format.video.nFrameHeight =
                     param->format.video.nFrameHeight;

#ifndef USE_DEFAULT_OUTPUT_BUFFER_SIZE
                OMX_S32 width = port->format.video.nFrameWidth;

                if ((port->format.video.nStride > 0) && (port->format.video.nStride > width))
                    width = port->format.video.nStride;

                err = calculate_output_buffer_size(width, param->format.video.nFrameHeight,
                                                param->format.video.eColorFormat, &port->nBufferSize);
                if (err != OMX_ErrorNone)
                {
                    DBGT_EPILOG("");
                    return err;
                }

#ifdef USE_EXTERNAL_BUFFER
                if (dec->useExternalAlloc == OMX_TRUE)
                {
                    /* Compare with port buffer size from client
                       to avoid useless port reconfig */
                    if (param->nBufferSize > port->nBufferSize)
                        port->nBufferSize = param->nBufferSize;

                    /* Check if we can read the correct size from the codec instance */
                    if (dec->codec)
                    {
                        FRAME_BUFFER_INFO bufInfo;

                        bufInfo = dec->codec->getframebufferinfo(dec->codec);

                        if (bufInfo.bufferSize > 0)
                            port->nBufferSize = bufInfo.bufferSize;
                    }
                }
#endif

                if (param->nBufferSize != port->nBufferSize)
                {
                    DBGT_PDEBUG("New output port buffer size: %d color format: %s",
                        (int)port->nBufferSize, HantroOmx_str_omx_color(param->format.video.eColorFormat));
                }
#endif
#ifdef SET_OUTPUT_CROP_RECT
                if (dec->output_cropping_set == OMX_FALSE)
                {
                    // In Stagefright we assume that default cropping information should be extracted
                    // from first set parameter call, later SF retrieve this info through a GetConfig
                    DBGT_PDEBUG("API: Set output cropping (0,0) width=%d height=%d",
                        (int)param->format.video.nFrameWidth, (int)param->format.video.nFrameHeight);

                    dec->output_cropping_set = OMX_TRUE;
                    set_output_cropping(dec, 0, 0, param->format.video.nFrameWidth,
                                    param->format.video.nFrameHeight);
                }
#endif
                break;

            case PORT_INDEX_PP:
                port = &dec->inpp.def;
                CHECK_PORT_STATE(dec, port);
                port->nBufferCountActual = param->nBufferCountActual;
                port->format.image.nFrameWidth = param->format.image.nFrameWidth;   // mask1 width
                port->format.image.nFrameHeight = param->format.image.nFrameHeight; // mask2 height
                break;
            default:
                DBGT_CRITICAL("API: Bad port index: %u",
                            (unsigned) param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
        }
        break;

    case OMX_IndexParamVideoPortFormat:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_VIDEO_PARAM_PORTFORMATTYPE *param =
                (OMX_VIDEO_PARAM_PORTFORMATTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                port = &dec->in.def;
                CHECK_PORT_STATE(dec, port);
                port->format.video.eCompressionFormat =
                    param->eCompressionFormat;
                port->format.video.eColorFormat = param->eColorFormat;
                break;
            case PORT_INDEX_OUTPUT:
                port = &dec->out.def;
                CHECK_PORT_STATE(dec, port);
                port->format.video.eCompressionFormat =
                    param->eCompressionFormat;
                port->format.video.eColorFormat = param->eColorFormat;
                break;
            default:
                DBGT_CRITICAL("API: Bad port index:%u",
                            (unsigned) param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
        }
        break;

    case OMX_IndexParamImagePortFormat:
        {
            // note: this is not currently being used anywhere. the 2nd input port input format is set at construction.
        }
        break;

        // note: for conformance.
    case OMX_IndexParamVideoAvc:
    case OMX_IndexParamVideoMpeg4:
    case OMX_IndexParamVideoH263:
    case OMX_IndexParamVideoMpeg2:
    case OMX_IndexParamVideoVp8:
        break;

    case OMX_IndexParamVideoWmv:
        {
            OMX_VIDEO_PARAM_WMVTYPE *param = (OMX_VIDEO_PARAM_WMVTYPE *) pParam;
            dec->WMVFormat = param->eFormat;
        }
        break;

    case OMX_IndexParamVideoRv:
        {
            OMX_VIDEO_PARAM_RVTYPE *param = (OMX_VIDEO_PARAM_RVTYPE *) pParam;
            if (param->eFormat == OMX_VIDEO_RVFormat8)
            {
                dec->bIsRV8 = OMX_TRUE;
            }
            else
            {
                dec->bIsRV8 = OMX_FALSE;
            }
            dec->imageSize = param->nMaxEncodeFrameSize;
            dec->width = param->nPaddedWidth;
            dec->height = param->nPaddedHeight;
        }
        break;

    case OMX_IndexParamVideoMvcStream:
        {
            OMX_VIDEO_PARAM_MVCSTREAMTYPE *param = (OMX_VIDEO_PARAM_MVCSTREAMTYPE *) pParam;
            dec->isMvcStream = param->bIsMVCStream;
            DBGT_PDEBUG("OMX_IndexParamVideoMvcStream: dec->isMvcStream %d", dec->isMvcStream);
        }
        break;

        // component mandatory stuff
    case OMX_IndexParamCompBufferSupplier:
        {
            OMX_PARAM_BUFFERSUPPLIERTYPE *param =
                (OMX_PARAM_BUFFERSUPPLIERTYPE *) pParam;
            PORT *port = decoder_map_index_to_port(dec, param->nPortIndex);

            if (!port)
            {
                DBGT_CRITICAL("OMX_IndexParamCompBufferSupplier, NULL port");
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
            CHECK_PORT_STATE(dec, &port->def);

            port->tunnel.eSupplier = param->eBufferSupplier;

            DBGT_PDEBUG("API: new buffer supplier value:%s port:%d",
                        HantroOmx_str_omx_supplier(param->eBufferSupplier),
                        (int) param->nPortIndex);

            if (port->tunnelcomp && port->def.eDir == OMX_DirInput)
            {
                DBGT_PDEBUG("API: propagating value to tunneled component: %p port: %d",
                            port->tunnelcomp, (int) port->tunnelport);
                OMX_ERRORTYPE err;

                OMX_PARAM_BUFFERSUPPLIERTYPE foo;

                memset(&foo, 0, sizeof(foo));
                INIT_OMX_VERSION_PARAM(foo);
                foo.nPortIndex = port->tunnelport;
                foo.eBufferSupplier = port->tunnel.eSupplier;
                err = ((OMX_COMPONENTTYPE *) port->tunnelcomp)->
                        SetParameter(port->tunnelcomp,
                                 OMX_IndexParamCompBufferSupplier, &foo);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("API: tunneled component refused buffer supplier config:%s",
                                HantroOmx_str_omx_err(err));
                    DBGT_EPILOG("");
                    return err;
                }
            }
        }
        break;

    case OMX_IndexParamCommonDeblocking:
        {
            OMX_PARAM_DEBLOCKINGTYPE *param =
                (OMX_PARAM_DEBLOCKINGTYPE *) pParam;
            dec->deblock = param->bDeblocking;
        }
        break;

    case OMX_IndexParamStandardComponentRole:
        {
            OMX_PARAM_COMPONENTROLETYPE *param =
                (OMX_PARAM_COMPONENTROLETYPE *) pParam;
            strcpy((char *) dec->role, (const char *) param->cRole);
            //DBGT_PDEBUG("Codec role %s", dec->role);
#ifdef CONFORMANCE
                if (strcmp((char*)dec->role, "video_decoder.mpeg4") == 0)
                    dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
                else if (strcmp((char*)dec->role, "video_decoder.avc") == 0)
                    dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
                else if (strcmp((char*)dec->role, "video_decoder.h263") == 0)
                    dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingH263;
                else if (strcmp((char*)dec->role, "video_decoder.wmv") == 0)
                    dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingWMV;
                else if (strcmp((char*)dec->role, "video_decoder.vp8") == 0)
                    dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingVP8;
#endif
        }
        break;

    case OMX_IndexParamPriorityMgmt:
        {
            CHECK_STATE_NOT_LOADED(dec->state);
            OMX_PRIORITYMGMTTYPE *param = (OMX_PRIORITYMGMTTYPE *) pParam;

            dec->priority_group = param->nGroupPriority;
            dec->priority_id = param->nGroupID;
        }
        break;

#ifdef USE_ANDROID_NATIVE_BUFFER
    case (OMX_INDEXTYPE)OMX_google_android_index_enableAndroidNativeBuffers:
        {
            struct EnableAndroidNativeBuffersParams *param = (struct EnableAndroidNativeBuffersParams *) pParam;

            DBGT_PDEBUG("API: SetParameter OMX_google_android_index_enableAndroidNativeBuffers");
            PORT *port = decoder_map_index_to_port(dec, param->nPortIndex);
            if (!port)
            {
                DBGT_CRITICAL("OMX_google_android_index_enableAndroidNativeBuffers, NULL port");
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }

            CHECK_PORT_STATE(dec, &port->def);
            port->useAndroidNativeBuffer = param->enable;
        }
    break;
    case (OMX_INDEXTYPE) OMX_google_android_index_useAndroidNativeBuffer:
        {
            struct UseAndroidNativeBufferParams  *param = (struct UseAndroidNativeBufferParams*) pParam;
            DBGT_PDEBUG("API: SetParameter OMX_google_android_index_useAndroidNativeBuffer");
            PORT *port = decoder_map_index_to_port(dec, param->nPortIndex);
            if (!port)
            {
                DBGT_CRITICAL("OMX_google_android_index_useAndroidNativeBuffer, NULL port");
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
            CHECK_PORT_STATE(dec, &port->def);
        }
    break;
#endif
#ifdef IS_G2_DECODER
    case OMX_IndexParamVideoG2Config:
        {
            OMX_VIDEO_PARAM_G2CONFIGTYPE *param = (OMX_VIDEO_PARAM_G2CONFIGTYPE *) pParam;

            CHECK_PORT_OUTPUT(param);
            memcpy(&dec->g2Conf, param, param->nSize);
        }
        break;
#endif
#ifdef IS_G1_DECODER
    case OMX_IndexParamVideoG1Config:
        {
            OMX_VIDEO_PARAM_G1CONFIGTYPE *param = (OMX_VIDEO_PARAM_G1CONFIGTYPE *) pParam;

            CHECK_PORT_OUTPUT(param);
            memcpy(&dec->g1Conf, param, param->nSize);
        }
        break;
#endif
    default:
        DBGT_CRITICAL("API: unsupported settings index: %s", HantroOmx_str_omx_index(nIndex));
        DBGT_EPILOG("");
        return OMX_ErrorUnsupportedIndex;
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_get_parameter(OMX_IN OMX_HANDLETYPE hComponent,
                                        OMX_IN OMX_INDEXTYPE nIndex,
                                        OMX_INOUT OMX_PTR pParam)
{
    CALLSTACK;
    DBGT_PROLOG("");

    static OMX_S32 input_formats[][2] = {
#ifdef ENABLE_CODEC_H264
        {OMX_VIDEO_CodingAVC, OMX_COLOR_FormatUnused},  // H264
#endif
#ifdef ENABLE_CODEC_H263
        {OMX_VIDEO_CodingH263, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_MPEG4
        {OMX_VIDEO_CodingMPEG4, OMX_COLOR_FormatUnused},
        {OMX_VIDEO_CodingSORENSON, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_DIVX
        {OMX_VIDEO_CodingDIVX, OMX_COLOR_FormatUnused},
        {OMX_VIDEO_CodingDIVX3, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_RV
        {OMX_VIDEO_CodingRV, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_VP8
        {OMX_VIDEO_CodingVP8, OMX_COLOR_FormatUnused},
        // {OMX_VIDEO_CodingVPX, OMX_COLOR_FormatUnused}, // Android 4.x uses non-standard coding type
#endif
#ifdef ENABLE_CODEC_AVS
        {OMX_VIDEO_CodingAVS, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_VP6
        {OMX_VIDEO_CodingVP6, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_VC1
        {OMX_VIDEO_CodingWMV, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_MPEG2
        {OMX_VIDEO_CodingMPEG2, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_MJPEG
        {OMX_VIDEO_CodingMJPEG, OMX_COLOR_FormatUnused},
#endif
#ifdef IS_G2_DECODER
#ifdef ENABLE_CODEC_HEVC
        {OMX_VIDEO_CodingHEVC, OMX_COLOR_FormatUnused},
#endif
#ifdef ENABLE_CODEC_VP9
        {OMX_VIDEO_CodingVP9, OMX_COLOR_FormatUnused},
#endif
#endif
#ifdef ENABLE_PP // PP standalone inputs
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420Planar},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420PackedPlanar},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420SemiPlanar},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420PackedSemiPlanar},
#if defined (IS_8190) || defined (IS_G1_DECODER)
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatCrYCbY},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatCbYCrY},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYCrYCb},
#endif /* IS_8190 */
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYCbYCr}
#endif /* ENABLE_PP */
    };

    static OMX_S32 output_formats[][2] = {
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420SemiPlanar},        // normal video output format
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420PackedSemiPlanar}   // normal video output format
#ifdef CONFORMANCE
        ,{OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420Planar},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYUV420PackedPlanar}
#endif
#ifndef IS_G2_DECODER
#ifdef ENABLE_PP // post-processor output formats
        ,{OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYCbYCr},
#if defined (IS_8190) || defined (IS_G1_DECODER)
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatYCrYCb},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatCbYCrY},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_FormatCrYCbY},
#endif /* IS_8190 */
        {OMX_VIDEO_CodingUnused, OMX_COLOR_Format32bitARGB8888},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_Format32bitBGRA8888},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_Format16bitARGB1555},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_Format16bitARGB4444},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_Format16bitRGB565},
        {OMX_VIDEO_CodingUnused, OMX_COLOR_Format16bitBGR565}
#endif /* ENABLE_PP */
#endif /* IS_G2_DECODER */
    };

#ifdef ENABLE_CODEC_H263
    static OMX_S32 h263_profiles[][2] = {
        {OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level70}
    };
#endif
#ifdef ENABLE_CODEC_MPEG2
    static OMX_S32 mpeg2_profiles[][2] = {
        { OMX_VIDEO_MPEG2ProfileSimple,    OMX_VIDEO_MPEG2LevelHL },
        { OMX_VIDEO_MPEG2ProfileMain,      OMX_VIDEO_MPEG2LevelHL }
    };
#endif
#ifdef ENABLE_CODEC_MPEG4
    static OMX_S32 mpeg4_profiles[][2] = {
        { OMX_VIDEO_MPEG4ProfileSimple,    OMX_VIDEO_MPEG4Level5 },
        { OMX_VIDEO_MPEG4ProfileAdvancedSimple,      OMX_VIDEO_MPEG4Level5 }
    };
#endif
#ifdef ENABLE_CODEC_VC1
static OMX_S32 wmv_profiles[][2] = {
        {OMX_VIDEO_WMVFormat7, 0},
        {OMX_VIDEO_WMVFormat8, 0},
        {OMX_VIDEO_WMVFormat9, 0},
    };
#endif
#ifdef ENABLE_CODEC_RV
    static OMX_S32 rv_profiles[][2] = {
        {OMX_VIDEO_RVFormat8, 0},
        {OMX_VIDEO_RVFormat9, 0},
        {OMX_VIDEO_RVFormatG2, 0}
    };
#endif
#ifdef ENABLE_CODEC_H264
    static OMX_S32 avc_profiles[][2] = {
        { OMX_VIDEO_AVCProfileBaseline,    OMX_VIDEO_AVCLevel51 },
        { OMX_VIDEO_AVCProfileMain,        OMX_VIDEO_AVCLevel51 },
        { OMX_VIDEO_AVCProfileHigh,        OMX_VIDEO_AVCLevel51 }
    };
#endif
#ifdef ENABLE_CODEC_VP8
    static OMX_S32 vp8_profiles[][2] = {
        { OMX_VIDEO_VP8ProfileMain,     OMX_VIDEO_VP8Level_Version0 }
    };
#endif
#ifdef IS_G2_DECODER
#ifdef ENABLE_CODEC_HEVC
    static OMX_S32 hevc_profiles[][2] = {
        { OMX_VIDEO_HEVCProfileMain,     OMX_VIDEO_HEVCLevel51 },
        { OMX_VIDEO_HEVCProfileMain10,     OMX_VIDEO_HEVCLevel51 }
    };
#endif
#ifdef ENABLE_CODEC_VP9
    static OMX_S32 vp9_profiles[][2] = {
        { OMX_VIDEO_VP9Profile0, 0},
        { OMX_VIDEO_VP9Profile2, 0}
    };
#endif
#endif

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pParam);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);

    DBGT_PDEBUG("API: getting index: %s", HantroOmx_str_omx_index(nIndex));

    switch ((OMX_U32)nIndex)
    {
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_PARAM_PORTDEFINITIONTYPE *param =
                (OMX_PARAM_PORTDEFINITIONTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                port = &dec->in.def;
                break;
            case PORT_INDEX_OUTPUT:
                port = &dec->out.def;
                break;
            case PORT_INDEX_PP:
                port = &dec->inpp.def;
                break;
            default:
                DBGT_CRITICAL("OMX_IndexParamPortDefinition, wrong index (%u)",
                                (unsigned int)param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
            DBGT_ASSERT(param->nSize);
            memcpy(param, port, param->nSize);
        }
        break;

        // this is used to enumerate the formats supported by the ports
    case OMX_IndexParamVideoPortFormat:
        {
            OMX_VIDEO_PARAM_PORTFORMATTYPE *param =
                (OMX_VIDEO_PARAM_PORTFORMATTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                if (param->nIndex >= sizeof(input_formats) / (sizeof(int) * 2))
                {
                    DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nIndex);
                    DBGT_EPILOG("");
                    return OMX_ErrorNoMore;
                }
                param->xFramerate = 0;
                param->eCompressionFormat = input_formats[param->nIndex][0];
                param->eColorFormat = input_formats[param->nIndex][1];
                break;
            case PORT_INDEX_OUTPUT:
                if (param->nIndex >= sizeof(output_formats) / (sizeof(int) * 2))
                {
                    DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nIndex);
                    DBGT_EPILOG("");
                    return OMX_ErrorNoMore;
                }
                param->xFramerate = 0;
                param->eCompressionFormat = output_formats[param->nIndex][0];
                //param->eColorFormat = OMX_COLOR_Format16bitRGB565;
                //param->eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
                param->eColorFormat = output_formats[param->nIndex][1];
                break;
            default:
                DBGT_CRITICAL("OMX_ErrorBadPortIndex (%u)", (unsigned int)param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
        }
        break;

// this is for the post-processor input
    case OMX_IndexParamImagePortFormat:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_IMAGE_PARAM_PORTFORMATTYPE *param =
                (OMX_IMAGE_PARAM_PORTFORMATTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_PP:
                if (param->nIndex >= 1)
                {
                    DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nIndex);
                    DBGT_EPILOG("");
                    return OMX_ErrorNoMore;
                }
                port = &dec->inpp.def;
                param->eCompressionFormat = port->format.image.eCompressionFormat;  // image coding not used
                param->eColorFormat = port->format.image.eColorFormat;  // ARGB8888
                break;
            default:
                DBGT_CRITICAL("OMX_ErrorBadPortIndex (%u)", (unsigned int)param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
        }
        break;

    case OMX_IndexParamVideoAvc:
        {
            OMX_VIDEO_PARAM_AVCTYPE *param = (OMX_VIDEO_PARAM_AVCTYPE *) pParam;

            CHECK_PORT_INPUT(param);
            param->eProfile = OMX_VIDEO_AVCProfileBaseline;
            param->eLevel = OMX_VIDEO_AVCLevel31;   // not good according to the conformance tester
#ifdef CONFORMANCE
            param->eLevel               = OMX_VIDEO_AVCLevel1;
#endif
            param->nAllowedPictureTypes =
                OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;
        }
        break;

    case OMX_IndexParamVideoMpeg4:
        {
            OMX_VIDEO_PARAM_MPEG4TYPE *param =
                (OMX_VIDEO_PARAM_MPEG4TYPE *) pParam;
            CHECK_PORT_INPUT(param);
            param->eProfile = OMX_VIDEO_MPEG4ProfileSimple;
            param->eLevel = OMX_VIDEO_MPEG4Level5;
#ifdef CONFORMANCE
            param->eLevel = OMX_VIDEO_MPEG4Level1;
#endif
            param->nAllowedPictureTypes =
                OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;
        }
        break;

    case OMX_IndexParamVideoH263:
        {
            OMX_VIDEO_PARAM_H263TYPE *param =
                (OMX_VIDEO_PARAM_H263TYPE *) pParam;
            CHECK_PORT_INPUT(param);
            param->eProfile = OMX_VIDEO_H263ProfileBaseline;
            param->eLevel = OMX_VIDEO_H263Level70;
            param->bPLUSPTYPEAllowed = OMX_TRUE;
#ifdef CONFORMANCE
            param->eLevel = OMX_VIDEO_H263Level10;
            param->bPLUSPTYPEAllowed = OMX_FALSE;
            param->bForceRoundingTypeToZero = OMX_TRUE;
#endif
            param->nAllowedPictureTypes =
                OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;
        }
        break;

   case OMX_IndexParamVideoRv:
        {
            OMX_VIDEO_PARAM_RVTYPE *param = (OMX_VIDEO_PARAM_RVTYPE *) pParam;
            CHECK_PORT_INPUT(param);

            if (dec->bIsRV8)
            {
                 param->eFormat = OMX_VIDEO_RVFormat8;
            }
            else
            {
                param->eFormat = OMX_VIDEO_RVFormat9;
            }
            param->nMaxEncodeFrameSize = dec->imageSize;
            param->nPaddedWidth = dec->width;
            param->nPaddedHeight = dec->height;
        }
        break;

    case OMX_IndexParamVideoWmv:
        {
            OMX_VIDEO_PARAM_WMVTYPE *param = (OMX_VIDEO_PARAM_WMVTYPE *) pParam;

            CHECK_PORT_INPUT(param);
            param->eFormat = dec->WMVFormat;
        }
        break;

    case OMX_IndexParamVideoMpeg2:
        {
            OMX_VIDEO_PARAM_MPEG2TYPE *param =
                (OMX_VIDEO_PARAM_MPEG2TYPE *) pParam;
            CHECK_PORT_INPUT(param);
            param->eProfile = OMX_VIDEO_MPEG2ProfileSimple;
            param->eLevel = OMX_VIDEO_MPEG2LevelHL;
        }
        break;

    case OMX_IndexParamVideoVp8:
        {
            OMX_VIDEO_PARAM_VP8TYPE *param = (OMX_VIDEO_PARAM_VP8TYPE *) pParam;

            CHECK_PORT_INPUT(param);
            param->eProfile = OMX_VIDEO_VP8ProfileMain;
            param->eLevel = OMX_VIDEO_VP8Level_Version0;
        }
        break;
#ifdef IS_G2_DECODER
    case OMX_IndexParamVideoVp9:
        {
            OMX_VIDEO_PARAM_VP9TYPE *param = (OMX_VIDEO_PARAM_VP9TYPE *) pParam;

            CHECK_PORT_INPUT(param);
            param->eProfile = OMX_VIDEO_VP9Profile0;
            param->nBitDepthLuma = dec->bitDepth;
            param->nBitDepthChroma = dec->bitDepth;
        }
        break;

    case OMX_IndexParamVideoHevc:
        {
            OMX_VIDEO_PARAM_HEVCTYPE *param = (OMX_VIDEO_PARAM_HEVCTYPE *) pParam;

            CHECK_PORT_INPUT(param);
            param->eProfile = OMX_VIDEO_HEVCProfileMain;
            param->eLevel = OMX_VIDEO_HEVCLevel51;
            param->nBitDepthLuma = dec->bitDepth;
            param->nBitDepthChroma = dec->bitDepth;
        }
        break;

    case OMX_IndexParamVideoG2Config:
        {
            OMX_VIDEO_PARAM_G2CONFIGTYPE *param = (OMX_VIDEO_PARAM_G2CONFIGTYPE *) pParam;

            CHECK_PORT_OUTPUT(param);
            memcpy(param, &dec->g2Conf, param->nSize);
        }
        break;
#endif
#ifdef IS_G1_DECODER
    case OMX_IndexParamVideoG1Config:
        {
            OMX_VIDEO_PARAM_G1CONFIGTYPE *param = (OMX_VIDEO_PARAM_G1CONFIGTYPE *) pParam;

            CHECK_PORT_OUTPUT(param);
            memcpy(param, &dec->g1Conf, param->nSize);
        }
        break;
#endif
    case OMX_IndexParamVideoProfileLevelCurrent:
    case OMX_IndexParamVideoProfileLevelQuerySupported:
        {
            OMX_VIDEO_PARAM_PROFILELEVELTYPE *param =
                (OMX_VIDEO_PARAM_PROFILELEVELTYPE *) pParam;
            CHECK_PORT_INPUT(param);
            switch ((OMX_U32)dec->in.def.format.video.eCompressionFormat)
            {
#ifdef ENABLE_CODEC_H263
                case OMX_VIDEO_CodingH263:
                    if (param->nProfileIndex >= sizeof(h263_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = h263_profiles[param->nProfileIndex][0];
                    param->eLevel   = h263_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef ENABLE_CODEC_MPEG2
                case OMX_VIDEO_CodingMPEG2:
                    if (param->nProfileIndex >= sizeof(mpeg2_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = mpeg2_profiles[param->nProfileIndex][0];
                    param->eLevel   = mpeg2_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef ENABLE_CODEC_MPEG4
                case OMX_VIDEO_CodingMPEG4:
                    if (param->nProfileIndex >= sizeof(mpeg4_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = mpeg4_profiles[param->nProfileIndex][0];
                    param->eLevel   = mpeg4_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef ENABLE_CODEC_VC1
                case OMX_VIDEO_CodingWMV:
                    if (param->nProfileIndex >= sizeof(wmv_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = wmv_profiles[param->nProfileIndex][0];
                    param->eLevel   = wmv_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef ENABLE_CODEC_RV
                case OMX_VIDEO_CodingRV:
                    if (param->nProfileIndex >= sizeof(rv_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = rv_profiles[param->nProfileIndex][0];
                    param->eLevel   = rv_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef ENABLE_CODEC_H264
                case OMX_VIDEO_CodingAVC:
                    if (param->nProfileIndex >= sizeof(avc_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = avc_profiles[param->nProfileIndex][0];
                    param->eLevel   = avc_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef ENABLE_CODEC_VP8
                case OMX_VIDEO_CodingVP8:
                    if (param->nProfileIndex >= sizeof(vp8_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = vp8_profiles[param->nProfileIndex][0];
                    param->eLevel   = vp8_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef IS_G2_DECODER
#ifdef ENABLE_CODEC_HEVC
                case OMX_VIDEO_CodingHEVC:
                    if (param->nProfileIndex >= sizeof(hevc_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = hevc_profiles[param->nProfileIndex][0];
                    param->eLevel   = hevc_profiles[param->nProfileIndex][1];
                    break;
#endif
#ifdef ENABLE_CODEC_VP9
                case OMX_VIDEO_CodingVP9:
                    if (param->nProfileIndex >= sizeof(vp9_profiles)/(sizeof(int)*2))
                    {
                        DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nProfileIndex);
                        DBGT_EPILOG("");
                        return OMX_ErrorNoMore;
                    }
                    param->eProfile = vp9_profiles[param->nProfileIndex][0];
                    param->eLevel   = vp9_profiles[param->nProfileIndex][1];
                    break;
#endif
#endif
                default:
                    DBGT_ERROR("Unsupported format (dec->in.def.format.video.eCompressionFormat=%x)",
                          dec->in.def.format.video.eCompressionFormat);
                    DBGT_EPILOG("");
                    return OMX_ErrorNoMore;
            }
        }
        break;

    case OMX_IndexParamCommonDeblocking:
        {
            OMX_PARAM_DEBLOCKINGTYPE *param =
                (OMX_PARAM_DEBLOCKINGTYPE *) pParam;
            param->bDeblocking = dec->deblock;
        }
        break;

    case OMX_IndexParamVideoMvcStream:
        {
            OMX_VIDEO_PARAM_MVCSTREAMTYPE *param = (OMX_VIDEO_PARAM_MVCSTREAMTYPE *) pParam;
            param->bIsMVCStream = dec->isMvcStream;
            DBGT_PDEBUG("OMX_IndexParamVideoMvcStream: dec->isMvcStream %d", dec->isMvcStream);
        }
        break;

        // component mandatory stuff

    case OMX_IndexParamCompBufferSupplier:
        {
            OMX_PARAM_BUFFERSUPPLIERTYPE *param =
                (OMX_PARAM_BUFFERSUPPLIERTYPE *) pParam;
            PORT *port = decoder_map_index_to_port(dec, param->nPortIndex);

            if (!port)
            {
                DBGT_CRITICAL("NULL port, OMX_ErrorBadPortIndex");
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
            param->eBufferSupplier = port->tunnel.eSupplier;
        }
        break;

    case OMX_IndexParamStandardComponentRole:
        {
            OMX_PARAM_COMPONENTROLETYPE *param =
                (OMX_PARAM_COMPONENTROLETYPE *) pParam;
            strcpy((char *) param->cRole, (const char *) dec->role);
        }
        break;

    case OMX_IndexParamPriorityMgmt:
        {
            OMX_PRIORITYMGMTTYPE *param = (OMX_PRIORITYMGMTTYPE *) pParam;

            param->nGroupPriority = dec->priority_group;
            param->nGroupID = dec->priority_id;
        }
        break;

    case OMX_IndexParamAudioInit:
    case OMX_IndexParamOtherInit:
        {
            OMX_PORT_PARAM_TYPE *param = (OMX_PORT_PARAM_TYPE *) pParam;

            param->nPorts = 0;
            param->nStartPortNumber = 0;
        }
        break;
    case OMX_IndexParamVideoInit:
        {
            OMX_PORT_PARAM_TYPE *param = (OMX_PORT_PARAM_TYPE *) pParam;

            param->nPorts = 2;
            param->nStartPortNumber = 0;
        }
        break;
    case OMX_IndexParamImageInit:
        {
            OMX_PORT_PARAM_TYPE *param = (OMX_PORT_PARAM_TYPE *) pParam;

            param->nPorts = 1;
            param->nStartPortNumber = 2;
        }
        break;
#ifdef USE_OPENCORE
     /* Opencore specific */
     case PV_OMX_COMPONENT_CAPABILITY_TYPE_INDEX:
        {
            PV_OMXComponentCapabilityFlagsType *param = (PV_OMXComponentCapabilityFlagsType *) pParam;
            param->iIsOMXComponentMultiThreaded = OMX_TRUE;
            param->iOMXComponentSupportsExternalOutputBufferAlloc = OMX_FALSE;
            param->iOMXComponentSupportsExternalInputBufferAlloc = OMX_FALSE;
            param->iOMXComponentSupportsMovableInputBuffers = OMX_TRUE;
            param->iOMXComponentSupportsPartialFrames = OMX_FALSE;
            param->iOMXComponentUsesNALStartCode = OMX_TRUE;
            param->iOMXComponentCanHandleIncompleteFrames = OMX_FALSE;
            param->iOMXComponentUsesFullAVCFrames = OMX_TRUE;
            DBGT_PDEBUG("PV Capability Flags set");
        }
        break;
#endif
#ifdef USE_ANDROID_NATIVE_BUFFER
    case OMX_google_android_index_getAndroidNativeBufferUsage:
       {
           struct GetAndroidNativeBufferUsageParams *params = (struct GetAndroidNativeBufferUsageParams *) pParam;
           DBGT_PDEBUG("API: GetParameter OMX_google_android_index_getAndroidNativeBufferUsage");
           params->nUsage =
               GRALLOC_USAGE_SW_READ_OFTEN |
               GRALLOC_USAGE_SW_WRITE_OFTEN |
               GRALLOC_USAGE_HW_TEXTURE |
               GRALLOC_USAGE_HW_RENDER |
               GRALLOC_USAGE_HW_2D;
       }
       break;
#endif
    default:
        DBGT_CRITICAL("API: unsupported settings index: %s", HantroOmx_str_omx_index(nIndex));
        DBGT_EPILOG("");
        return OMX_ErrorUnsupportedIndex;
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

#endif
#ifdef OMX_DECODER_IMAGE_DOMAIN
static
    OMX_ERRORTYPE decoder_set_parameter(OMX_IN OMX_HANDLETYPE hComponent,
                                        OMX_IN OMX_INDEXTYPE nIndex,
                                        OMX_IN OMX_PTR pParam)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pParam);

    OMX_DECODER *dec = GET_DECODER(hComponent);
    OMX_ERRORTYPE err;

    CHECK_STATE_INVALID(dec->state);
    CHECK_STATE_IDLE(dec->state);

    if (dec->state != OMX_StateLoaded && dec->state != OMX_StateWaitForResources)
    {
        DBGT_CRITICAL("API: unsupported state: %s", HantroOmx_str_omx_state(dec->state));
        return OMX_ErrorIncorrectStateOperation;
    }

    switch (nIndex)
    {
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_PARAM_PORTDEFINITIONTYPE *param =
                (OMX_PARAM_PORTDEFINITIONTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                port = &dec->in.def;
                CHECK_PORT_STATE(dec, port);
                port->nBufferCountActual = param->nBufferCountActual;
#ifndef USE_DEFAULT_INPUT_BUFFER_SIZE
                /* PP input is raw image data, so we can use calculate_output_buffer_size() function */
                if (port->format.image.eCompressionFormat == OMX_IMAGE_CodingUnused)
                {
                    err = calculate_output_buffer_size(param->format.image.nFrameWidth, param->format.image.nFrameHeight,
                                                param->format.image.eColorFormat, &port->nBufferSize);
                }
                /* Calculate input buffer size based on YUV 420 data size and minimum compression ratio 2 */
                else
                {
                   port->nBufferSize = (param->format.image.nFrameWidth * param->format.image.nFrameHeight) * 3/4;
                }

                /* Limit the smallest input buffer size for small resolutions */
                if (port->nBufferSize < MIN_BUFFER_SIZE)
                {
                    port->nBufferSize = MIN_BUFFER_SIZE;
                }
#endif
                if (param->nBufferSize != port->nBufferSize)
                {
                    DBGT_PDEBUG("New input port buffer size: %d", (int)port->nBufferSize);
                }
                port->format.image.eColorFormat =
                    param->format.image.eColorFormat;
                port->format.image.eCompressionFormat =
                    param->format.image.eCompressionFormat;
                // need these for raw YUV input
                port->format.image.nFrameWidth =
                    param->format.image.nFrameWidth;
                port->format.image.nFrameHeight =
                    param->format.image.nFrameHeight;
#ifdef CONFORMANCE
                if (strcmp((char*)dec->role, "image_decoder.jpeg") == 0)
                    port->format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
                else if (strcmp((char*)dec->role, "image_decoder.webp") == 0)
                    port->format.image.eCompressionFormat = OMX_IMAGE_CodingWEBP;
#endif
                break;
            case PORT_INDEX_OUTPUT:
                port = &dec->out.def;
                CHECK_PORT_STATE(dec, port);
                port->nBufferCountActual = param->nBufferCountActual;
                //port->nBufferSize = param->nBufferSize; // According to OMX IL spec, port buffer size is read only
                port->format.image.eColorFormat =
                    param->format.image.eColorFormat;
                port->format.image.nFrameHeight =
                    param->format.image.nFrameHeight;
                port->format.image.nFrameWidth =
                    param->format.image.nFrameWidth;

#ifndef USE_DEFAULT_OUTPUT_BUFFER_SIZE
                err = calculate_output_buffer_size(param->format.image.nFrameWidth, param->format.image.nFrameHeight,
                                                param->format.image.eColorFormat, &port->nBufferSize);
                if (err != OMX_ErrorNone)
                {
                    DBGT_EPILOG("");
                    return err;
                }

                if (param->nBufferSize != port->nBufferSize)
                {
                    DBGT_PDEBUG("New output port buffer size: %d color format: %s",
                        (int)port->nBufferSize, HantroOmx_str_omx_color(param->format.image.eColorFormat));
                }
#endif
                break;
            case PORT_INDEX_PP:
                port = &dec->inpp.def;
                CHECK_PORT_STATE(dec, port);
                port->nBufferCountActual = param->nBufferCountActual;
                port->format.image.nFrameWidth = param->format.image.nFrameWidth;   // mask1 width
                port->format.image.nFrameHeight = param->format.image.nFrameHeight; // mask1 height
                break;
            default:
                DBGT_CRITICAL("API: no such port: %u", (unsigned) param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
        }
        break;

    case OMX_IndexParamImagePortFormat:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_IMAGE_PARAM_PORTFORMATTYPE *param =
                (OMX_IMAGE_PARAM_PORTFORMATTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                port = &dec->in.def;
                CHECK_PORT_STATE(dec, port);
                port->format.image.eCompressionFormat =
                    param->eCompressionFormat;
                port->format.image.eColorFormat = param->eColorFormat;
                break;
            case PORT_INDEX_OUTPUT:
                port = &dec->out.def;
                CHECK_PORT_STATE(dec, port);
                port->format.image.eCompressionFormat =
                    param->eCompressionFormat;
                port->format.image.eColorFormat = param->eColorFormat;
                break;
            case PORT_INDEX_PP:
                break;
            default:
                DBGT_CRITICAL("API: no such image port:%u",
                            (unsigned) param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
        }

    case OMX_IndexParamCompBufferSupplier:
        {
            OMX_PARAM_BUFFERSUPPLIERTYPE *param =
                (OMX_PARAM_BUFFERSUPPLIERTYPE *) pParam;
            PORT *port = decoder_map_index_to_port(dec, param->nPortIndex);

            if (!port)
            {
                DBGT_CRITICAL("decoder_map_index_to_port (NULL port)");
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
            CHECK_PORT_STATE(dec, &port->def);

            port->tunnel.eSupplier = param->eBufferSupplier;

            DBGT_PDEBUG("API: new buffer supplier value:%s port:%d",
                        HantroOmx_str_omx_supplier(param->eBufferSupplier),
                        (int) param->nPortIndex);

            if (port->tunnelcomp && port->def.eDir == OMX_DirInput)
            {
                DBGT_PDEBUG("API: propagating value to tunneled component: %p port: %d",
                            port->tunnelcomp, (int) port->tunnelport);
                OMX_ERRORTYPE err;

                OMX_PARAM_BUFFERSUPPLIERTYPE foo;

                memset(&foo, 0, sizeof(foo));
                INIT_OMX_VERSION_PARAM(foo);
                foo.nPortIndex = port->tunnelport;
                foo.eBufferSupplier = port->tunnel.eSupplier;
                err =
                    ((OMX_COMPONENTTYPE *) port->tunnelcomp)->
                    SetParameter(port->tunnelcomp,
                                 OMX_IndexParamCompBufferSupplier, &foo);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("API: tunneled component refused buffer supplier config:%s",
                                HantroOmx_str_omx_err(err));
                    DBGT_EPILOG("");
                    return err;
                }
            }
        }
        break;

    case OMX_IndexParamStandardComponentRole:
        {
            OMX_PARAM_COMPONENTROLETYPE *param =
                (OMX_PARAM_COMPONENTROLETYPE *) pParam;
            strcpy((char *) dec->role, (const char *) param->cRole);
#ifdef CONFORMANCE
                if (strcmp((char*)dec->role, "image_decoder.jpeg") == 0)
                    dec->in.def.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
                else if (strcmp((char*)dec->role, "image_decoder.webp") == 0)
                    dec->in.def.format.image.eCompressionFormat = OMX_IMAGE_CodingWEBP;
#endif
        }
        break;

    case OMX_IndexParamPriorityMgmt:
        {
            CHECK_STATE_NOT_LOADED(dec->state);
            OMX_PRIORITYMGMTTYPE *param = (OMX_PRIORITYMGMTTYPE *) pParam;

            dec->priority_group = param->nGroupPriority;
            dec->priority_id = param->nGroupID;
        }
        break;
#ifdef CONFORMANCE
    case OMX_IndexParamHuffmanTable:
        {
            OMX_IMAGE_PARAM_HUFFMANTTABLETYPE *param =
                (OMX_IMAGE_PARAM_HUFFMANTTABLETYPE *) pParam;
            memcpy(&dec->huffman_table, param, param->nSize);
        }
        break;

    case OMX_IndexParamQuantizationTable:
        {
            OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE *param =
                (OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE *) pParam;
            memcpy(&dec->quant_table, param, param->nSize);
        }
        break;
#endif
    default:
        DBGT_CRITICAL("API: unsupported settings index: %s", HantroOmx_str_omx_index(nIndex));
        DBGT_EPILOG("");
        return OMX_ErrorUnsupportedIndex;
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_get_parameter(OMX_IN OMX_HANDLETYPE hComponent,
                                        OMX_IN OMX_INDEXTYPE nIndex,
                                        OMX_INOUT OMX_PTR pParam)
{
    CALLSTACK;
    DBGT_PROLOG("");

    static OMX_S32 input_formats[][2] = {
#ifdef ENABLE_CODEC_JPEG
        {OMX_IMAGE_CodingJPEG, OMX_COLOR_FormatUnused}  // normal JPEG input
#endif
#ifdef ENABLE_CODEC_WEBP
        ,{OMX_IMAGE_CodingWEBP, OMX_COLOR_FormatUnused} // normal WebP input
#endif
#ifdef ENABLE_PP // PP standalone inputs
        ,{OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420Planar},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420PackedPlanar},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420SemiPlanar},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420PackedSemiPlanar},
#if defined (IS_8190) || defined (IS_G1_DECODER)
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatCbYCrY},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatCrYCbY},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYCrYCb},
#endif
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYCbYCr}
#endif /* ENABLE_PP */
    };

    static OMX_S32 output_formats[][2] = {
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420SemiPlanar},         // normal image output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420PackedSemiPlanar}    // normal image output format
#ifdef CONFORMANCE
        ,{OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420Planar},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV420PackedPlanar}
#endif
#ifdef ENABLE_CODEC_JPEG
        ,{OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV422SemiPlanar},        // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV422PackedSemiPlanar},   // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV411SemiPlanar},         // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV411PackedSemiPlanar},   // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV440SemiPlanar},        // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV440PackedSemiPlanar},   // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV444SemiPlanar},        // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYUV444PackedSemiPlanar},   // JPEG output format
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatL8}                        // JPEG output format (grayscale)
#endif
#ifdef ENABLE_PP // post-processor output formats
        ,{OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYCbYCr},
#if defined (IS_8190) || defined (IS_G1_DECODER)
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatYCrYCb},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatCbYCrY},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_FormatCrYCbY},
#endif /* IS_8190 */
        {OMX_IMAGE_CodingUnused, OMX_COLOR_Format32bitARGB8888},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_Format32bitBGRA8888},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_Format16bitARGB1555},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_Format16bitARGB4444},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_Format16bitRGB565},
        {OMX_IMAGE_CodingUnused, OMX_COLOR_Format16bitBGR565}
#endif /* ENABLE_PP */
    };

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pParam);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);
    DBGT_PDEBUG("API: getting index: %s",
                HantroOmx_str_omx_index(nIndex));

    switch ((OMX_U32)nIndex)
    {
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_PARAM_PORTDEFINITIONTYPE *param =
                (OMX_PARAM_PORTDEFINITIONTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                port = &dec->in.def;
                break;
            case PORT_INDEX_OUTPUT:
                port = &dec->out.def;
                break;
            case PORT_INDEX_PP:
                port = &dec->inpp.def;
                break;
            default:
                DBGT_CRITICAL("Bad port index (%x)", (unsigned int)param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
            DBGT_ASSERT(param->nSize);
            memcpy(param, port, param->nSize);
        }
        break;

    case OMX_IndexParamImagePortFormat:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;

            OMX_IMAGE_PARAM_PORTFORMATTYPE *param =
                (OMX_IMAGE_PARAM_PORTFORMATTYPE *) pParam;
            switch (param->nPortIndex)
            {
            case PORT_INDEX_INPUT:
                if (param->nIndex >= sizeof(input_formats) / (sizeof(int) * 2))
                {
                    DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nIndex);
                    DBGT_EPILOG("");
                    return OMX_ErrorNoMore;
                }

                param->eCompressionFormat = input_formats[param->nIndex][0];
                param->eColorFormat = input_formats[param->nIndex][1];
                break;
            case PORT_INDEX_OUTPUT:
                if (param->nIndex >= sizeof(output_formats) / (sizeof(int) * 2))
                {
                    DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nIndex);
                    DBGT_EPILOG("");
                    return OMX_ErrorNoMore;
                }

                param->eCompressionFormat = output_formats[param->nIndex][0];
                param->eColorFormat = output_formats[param->nIndex][1];
                break;
            case PORT_INDEX_PP:
                if (param->nIndex >= 1)
                {
                    DBGT_ERROR("OMX_ErrorNoMore (%u)", (unsigned int)param->nIndex);
                    DBGT_EPILOG("");
                    return OMX_ErrorNoMore;
                }

                port = &dec->inpp.def;
                param->eCompressionFormat =
                    port->format.image.eCompressionFormat;
                param->eColorFormat = port->format.image.eColorFormat;
                break;
            default:
                DBGT_CRITICAL("Bad port index (%x)", (unsigned int)param->nPortIndex);
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
        }
        break;

        // these are specified for standard JPEG decoder
#ifdef CONFORMANCE
        case OMX_IndexParamHuffmanTable:
        {
            OMX_IMAGE_PARAM_HUFFMANTTABLETYPE *param =
                (OMX_IMAGE_PARAM_HUFFMANTTABLETYPE *) pParam;
            memcpy(param, &dec->huffman_table, param->nSize);
        }
        break;
        case OMX_IndexParamQuantizationTable:
        {
            OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE *param =
                (OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE *) pParam;
            memcpy(param, &dec->quant_table, param->nSize);
        }
        break;
#endif

    case OMX_IndexParamCompBufferSupplier:
        {
            OMX_PARAM_BUFFERSUPPLIERTYPE *param =
                (OMX_PARAM_BUFFERSUPPLIERTYPE *) pParam;
            PORT *port = decoder_map_index_to_port(dec, param->nPortIndex);

            if (!port)
            {
                DBGT_CRITICAL("decoder_map_index_to_port NULL port");
                DBGT_EPILOG("");
                return OMX_ErrorBadPortIndex;
            }
            param->eBufferSupplier = port->tunnel.eSupplier;
        }
        break;

    case OMX_IndexParamStandardComponentRole:
        {
            OMX_PARAM_COMPONENTROLETYPE *param =
                (OMX_PARAM_COMPONENTROLETYPE *) pParam;
            strcpy((char *) param->cRole, (const char *) dec->role);
        }
        break;

    case OMX_IndexParamPriorityMgmt:
        {
            OMX_PRIORITYMGMTTYPE *param = (OMX_PRIORITYMGMTTYPE *) pParam;

            param->nGroupPriority = dec->priority_group;
            param->nGroupID = dec->priority_id;
        }
        break;

    case OMX_IndexParamAudioInit:
    case OMX_IndexParamOtherInit:
    case OMX_IndexParamVideoInit:
        {
            OMX_PORT_PARAM_TYPE *param = (OMX_PORT_PARAM_TYPE *) pParam;

            param->nPorts = 0;
            param->nStartPortNumber = 0;
        }
        break;

    case OMX_IndexParamImageInit:
        {
            OMX_PORT_PARAM_TYPE *param = (OMX_PORT_PARAM_TYPE *) pParam;

            param->nPorts = 3;
            param->nStartPortNumber = 0;
        }
        break;
    default:
        DBGT_CRITICAL("API: unsupported index: %s", HantroOmx_str_omx_index(nIndex));
        DBGT_EPILOG("");
        return OMX_ErrorUnsupportedIndex;
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

#endif // OMX_DECODER_IMAGE_DOMAIN

static
    OMX_ERRORTYPE decoder_set_config(OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_IN OMX_INDEXTYPE nIndex,
                                     OMX_IN OMX_PTR pParam)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pParam);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);

    DBGT_PDEBUG("API: config index: %s", HantroOmx_str_omx_index(nIndex));
    /*if (dec->state != OMX_StateLoaded && dec->state != OMX_StateIdle)
    {
        // by an agreement with the client. Simplify parameter setting by allowing
        // parameters to be set only in the loaded/idle state.
        // OMX specification does not know about such constraint, but an implementation is allowed to do this.
        DBGT_CRITICAL("API: unsupported state: %s", HantroOmx_str_omx_state(dec->state));
        return OMX_ErrorUnsupportedSetting;
    }*/

    switch (nIndex)
    {
    case OMX_IndexConfigCommonRotate:
        {
            OMX_CONFIG_ROTATIONTYPE *param = (OMX_CONFIG_ROTATIONTYPE *) pParam;

            memcpy(&dec->conf_rotation, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonMirror:
        {
            OMX_CONFIG_MIRRORTYPE *param = (OMX_CONFIG_MIRRORTYPE *) pParam;

            memcpy(&dec->conf_mirror, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonContrast:
        {
            OMX_CONFIG_CONTRASTTYPE *param = (OMX_CONFIG_CONTRASTTYPE *) pParam;

            memcpy(&dec->conf_contrast, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonBrightness:
        {
            OMX_CONFIG_BRIGHTNESSTYPE *param =
                (OMX_CONFIG_BRIGHTNESSTYPE *) pParam;
            memcpy(&dec->conf_brightness, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonSaturation:
        {
            OMX_CONFIG_SATURATIONTYPE *param =
                (OMX_CONFIG_SATURATIONTYPE *) pParam;
            memcpy(&dec->conf_saturation, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonPlaneBlend:
        {
            OMX_CONFIG_PLANEBLENDTYPE *param =
                (OMX_CONFIG_PLANEBLENDTYPE *) pParam;
            memcpy(&dec->conf_blend, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonDithering:
        {
            OMX_CONFIG_DITHERTYPE *param = (OMX_CONFIG_DITHERTYPE *) pParam;

            memcpy(&dec->conf_dither, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonInputCrop:
        {
            OMX_CONFIG_RECTTYPE *param = (OMX_CONFIG_RECTTYPE *) pParam;

            //memcpy(&dec->conf_rect, param, param->nSize);
            dec->conf_rect.nLeft = param->nLeft;
            dec->conf_rect.nTop = param->nTop;
            dec->conf_rect.nWidth = (param->nWidth + 7) & ~7;
            dec->conf_rect.nHeight = (param->nHeight + 7) & ~7;

            if((param->nWidth != dec->conf_rect.nWidth) || (param->nHeight != dec->conf_rect.nHeight))
            {
                DBGT_ERROR("Crop width and crop height must be a multiple of 8. Forcing alingment!");
                DBGT_PDEBUG("Crop rect forced to (%d, %d, %d, %d)",
                    (int)dec->conf_rect.nLeft, (int)dec->conf_rect.nTop,
                    (int)dec->conf_rect.nWidth, (int)dec->conf_rect.nHeight);
            }
        }
        break;

        // for masking
    case OMX_IndexConfigCommonOutputPosition:
        {
            OMX_CONFIG_POINTTYPE *param = (OMX_CONFIG_POINTTYPE *) pParam;

            memcpy(&dec->conf_mask_offset, param, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonExclusionRect:
        {
            OMX_CONFIG_RECTTYPE *param = (OMX_CONFIG_RECTTYPE *) pParam;

            memcpy(&dec->conf_mask, param, param->nSize);
        }
        break;

    default:
        DBGT_CRITICAL("Bad index: %s", HantroOmx_str_omx_index(nIndex));
        DBGT_EPILOG("");
        return OMX_ErrorUnsupportedIndex;
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_get_config(OMX_IN OMX_HANDLETYPE hComponent,
                                     OMX_IN OMX_INDEXTYPE nIndex,
                                     OMX_INOUT OMX_PTR pParam)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pParam);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);

    DBGT_PDEBUG("API: Get config index: %s", HantroOmx_str_omx_index(nIndex));

    switch ((OMX_U32)nIndex)
    {
    case OMX_IndexConfigCommonRotate:
        {
            OMX_CONFIG_ROTATIONTYPE *param = (OMX_CONFIG_ROTATIONTYPE *) pParam;

            memcpy(param, &dec->conf_rotation, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonMirror:
        {
            OMX_CONFIG_MIRRORTYPE *param = (OMX_CONFIG_MIRRORTYPE *) pParam;

            memcpy(param, &dec->conf_mirror, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonContrast:
        {
            OMX_CONFIG_CONTRASTTYPE *param = (OMX_CONFIG_CONTRASTTYPE *) pParam;

            memcpy(param, &dec->conf_contrast, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonBrightness:
        {
            OMX_CONFIG_BRIGHTNESSTYPE *param =
                (OMX_CONFIG_BRIGHTNESSTYPE *) pParam;
            memcpy(param, &dec->conf_brightness, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonSaturation:
        {
            OMX_CONFIG_SATURATIONTYPE *param =
                (OMX_CONFIG_SATURATIONTYPE *) pParam;
            memcpy(param, &dec->conf_saturation, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonPlaneBlend:
        {
            OMX_CONFIG_PLANEBLENDTYPE *param =
                (OMX_CONFIG_PLANEBLENDTYPE *) pParam;
            memcpy(param, &dec->conf_blend, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonDithering:
        {
            OMX_CONFIG_DITHERTYPE *param = (OMX_CONFIG_DITHERTYPE *) pParam;

            memcpy(param, &dec->conf_dither, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonInputCrop:
        {
            OMX_CONFIG_RECTTYPE *param = (OMX_CONFIG_RECTTYPE *) pParam;

            memcpy(param, &dec->conf_rect, param->nSize);
        }
        break;
#ifdef SET_OUTPUT_CROP_RECT
    case OMX_IndexConfigCommonOutputCrop:
        {
            OMX_CONFIG_RECTTYPE *param = (OMX_CONFIG_RECTTYPE *) pParam;

            memcpy(param, &dec->output_cropping_rect, param->nSize);
        }
        break;
#endif

    case OMX_IndexConfigCommonOutputPosition:
        {
            OMX_CONFIG_POINTTYPE *param = (OMX_CONFIG_POINTTYPE *) pParam;

            memcpy(param, &dec->conf_mask_offset, param->nSize);
        }
        break;

    case OMX_IndexConfigCommonExclusionRect:
        {
            OMX_CONFIG_RECTTYPE *param = (OMX_CONFIG_RECTTYPE *) pParam;

            memcpy(param, &dec->conf_mask, param->nSize);
        }
        break;
#ifdef ENABLE_CODEC_VP8
    case OMX_IndexConfigVideoVp8ReferenceFrameType:
        {
            OMX_VIDEO_VP8REFERENCEFRAMEINFOTYPE *param =
                (OMX_VIDEO_VP8REFERENCEFRAMEINFOTYPE *) pParam;
            param->bIsIntraFrame = dec->isIntra;
            param->bIsGoldenOrAlternateFrame = dec->isGoldenOrAlternate;
        }
        break;
#endif
    default:
        DBGT_CRITICAL("Bad index: %s", HantroOmx_str_omx_index(nIndex));
        DBGT_EPILOG("");
        return OMX_ErrorUnsupportedIndex;
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE decoder_push_buffer(OMX_HANDLETYPE hComponent,
                                      OMX_BUFFERHEADERTYPE * pBufferHeader,
                                      OMX_U32 portindex)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pBufferHeader);

    OMX_DECODER *dec = GET_DECODER(hComponent);

    CHECK_STATE_INVALID(dec->state);
    DBGT_PDEBUG("API: header:%p port index:%u", pBufferHeader,
                (unsigned) portindex);

    if (dec->state != OMX_StateExecuting && dec->state != OMX_StatePause &&
       dec->state != OMX_StateIdle)
    {
        DBGT_CRITICAL("API: incorrect decoder state: %s",
                    HantroOmx_str_omx_state(dec->state));
        DBGT_EPILOG("");
        return OMX_ErrorIncorrectStateOperation;
    }

    PORT *port = decoder_map_index_to_port(dec, portindex);

    if (!port)
    {
        DBGT_CRITICAL("API: no such port");
        DBGT_EPILOG("");
        return OMX_ErrorBadPortIndex;
    }

    // In case of a tunneled port the client will request to disable a port on the buffer supplier,
    // and then on the non-supplier. The non-supplier needs to be able to return the supplied
    // buffer to our queue. So in this case this function will be invoked on a disabled port.
    // Then on the other hand the conformance tester (PortCommunicationTest) tests to see that
    // when a port is disabled it returns an appropriate error when a buffer is sent to it.
    //
    // In IOP/PortDisableEnable test tester disables all of our ports. Then destroys the TTC
    // and creates a new TTC. The new TTC is told to allocate buffers for our output port.
    // The the tester tells the TTC to transit to Executing state, at which point it is trying to
    // initiate buffer trafficing by calling our FillThisBuffer. However at this point the
    // port is still disabled. Doh.
    //
#ifdef CONFORMANCE
    if (!HantroOmx_port_is_tunneled(port))
    {
        if (!port->def.bEnabled)
        {
            DBGT_ERROR("API: port is disabled");
            DBGT_EPILOG("");
            return OMX_ErrorIncorrectStateOperation;
        }
    }
#endif
    // Lock the port's buffer queue here.
    OMX_ERRORTYPE err = HantroOmx_port_lock_buffers(port);

    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("API: failed to lock port: %s",
                    HantroOmx_str_omx_err(err));
        DBGT_EPILOG("");
        return err;
    }

    BUFFER *buff = HantroOmx_port_find_buffer(port, pBufferHeader);

    if (!buff)
    {
        HantroOmx_port_unlock_buffers(port);
        DBGT_CRITICAL("API: no such buffer");
        DBGT_EPILOG("");
        return OMX_ErrorBadParameter;
    }

    err = HantroOmx_port_push_buffer(port, buff);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("API: failed to queue buffer: %s",
                    HantroOmx_str_omx_err(err));
        HantroOmx_port_unlock_buffers(port);
        DBGT_EPILOG("");
        return err;
    }

    // remember to unlock the queue too!
    HantroOmx_port_unlock_buffers(port);
    DBGT_EPILOG("");
    return err;
}

static
    OMX_ERRORTYPE decoder_fill_this_buffer(OMX_IN OMX_HANDLETYPE hComponent,
                                           OMX_IN OMX_BUFFERHEADERTYPE *
                                           pBufferHeader)
{
    CALLSTACK;
    DBGT_PROLOG("");

    BUFFER *buff = NULL;

    // this function gives us a databuffer into which store
    // decoded data, i.e. the video frames
    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pBufferHeader);

    // note: version checks should be made backwards compatible.
    if (pBufferHeader->nSize != sizeof(OMX_BUFFERHEADERTYPE))
    {
        DBGT_CRITICAL("API: buffer header size mismatch");
        DBGT_EPILOG("");
        return OMX_ErrorBadParameter;
    }
/*    if (pBufferHeader->nVersion.nVersion != HantroOmx_make_int_ver(COMPONENT_VERSION_MAJOR, COMPONENT_VERSION_MINOR))
    {
        DBGT_CRITICAL("API: buffer header version mismatch");
        return OMX_ErrorVersionMismatch;
    }
*/
    OMX_DECODER *dec = GET_DECODER(hComponent);

    if (dec->useExternalAlloc == OMX_TRUE)
    {
        OMX_ERRORTYPE err = HantroOmx_port_lock_buffers(&dec->out);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("HantroOmx_port_lock_buffers(out) (err=%x)", err);
            DBGT_EPILOG("");
            return err;
        }
        buff = HantroOmx_port_find_buffer(&dec->out, pBufferHeader);
        // Read output buffer physical address from buffer private
#ifdef USE_ALLOC_PRIVATE
        DBGT_ASSERT(pBufferHeader->pInputPortPrivate);
        ALLOC_PRIVATE *bufPrivate = (ALLOC_PRIVATE*) pBufferHeader->pInputPortPrivate;
        DBGT_PDEBUG("bufPrivate nBusAddress %llu", bufPrivate->nBusAddress);
        DBGT_ASSERT(buff->bus_data == bufPrivate->pBufferData);
        buff->bus_address = bufPrivate->nBusAddress;
#endif
        err = HantroOmx_port_unlock_buffers(&dec->out);

        if (!buff)
        {
            DBGT_CRITICAL("API: HantroOmx_port_find_buffer: no such buffer");
            //HantroOmx_port_unlock_buffers(port);
            DBGT_EPILOG("");
            return OMX_ErrorBadParameter;
        }

        buff->header->pOutputPortPrivate = NULL;

        if (dec->checkExtraBuffers)
        {
            OMX_U32 i;
            OMX_BOOL found = OMX_FALSE;
            for (i=0; i<dec->out.def.nBufferCountActual; i++)
            {
                DBGT_PDEBUG("outputBufList %lu", dec->outputBufList[i]);
                if (buff->bus_address == dec->outputBufList[i])
                    found = OMX_TRUE;

                if ((dec->outputBufList[i] == 0) && !found)
                {
                    CODEC_STATE ret;
                    dec->outputBufList[i] = buff->bus_address;
                    ret = dec->codec->setframebuffer(dec->codec, buff, dec->out.def.nBufferCountMin);

                    if (ret == CODEC_ERROR_NOT_ENOUGH_FRAME_BUFFERS ||
                        ret == CODEC_ERROR_BUFFER_SIZE)
                    {
                        dec->shared_data.EOS = OMX_TRUE;
                        DBGT_EPILOG("");
                        return OMX_ErrorInsufficientResources;
                    }
                    break;
                }
            }
        }

//        dec->codec->pictureconsumed(dec->codec, buff);
    }
#ifdef USE_ALLOC_PRIVATE
    else
    {
        OMX_ERRORTYPE err = HantroOmx_port_lock_buffers(&dec->out);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("HantroOmx_port_lock_buffers(out) (err=%x)", err);
            DBGT_EPILOG("");
            return err;
        }
        buff = HantroOmx_port_find_buffer(&dec->out, pBufferHeader);
        // Read output buffer physical address from buffer private
        DBGT_ASSERT(pBufferHeader->pInputPortPrivate);
        ALLOC_PRIVATE *bufPrivate = (ALLOC_PRIVATE*) pBufferHeader->pInputPortPrivate;
        DBGT_PDEBUG("bufPrivate nBusAddress %llu", bufPrivate->nBusAddress);
        DBGT_ASSERT(buff->bus_data == bufPrivate->pBufferData);
        buff->bus_address = bufPrivate->nBusAddress;
        err = HantroOmx_port_unlock_buffers(&dec->out);
    }
#endif

    DBGT_EPILOG("");
    OMX_ERRORTYPE ret = decoder_push_buffer(hComponent, pBufferHeader,
                               pBufferHeader->nOutputPortIndex);

    // Buffer should be consumed after decoder_push_buffer() called
    if (dec->useExternalAlloc == OMX_TRUE)
    {
        if (buff->bus_address == dec->shared_data.eos_bus_address)
            dec->shared_data.eos_bus_address = 0;
        // pictureconsumed() should be called for VP9 to aviod the
        // deadlock in VP9 port reconfig.
        else if (dec->checkExtraBuffers ||
                 dec->in.def.format.video.eCompressionFormat == OMX_VIDEO_CodingVP9)
            dec->codec->pictureconsumed(dec->codec, buff);
    }

    return ret;
}

static
    OMX_ERRORTYPE decoder_empty_this_buffer(OMX_IN OMX_HANDLETYPE hComponent,
                                            OMX_IN OMX_BUFFERHEADERTYPE *
                                            pBufferHeader)
{
    CALLSTACK;
    DBGT_PROLOG("");
    // this function gives us an input data buffer from
    // which data is decoded
    CHECK_PARAM_NON_NULL(hComponent);
    CHECK_PARAM_NON_NULL(pBufferHeader);

    if (/* pBufferHeader->nFilledLen == 0 || */ pBufferHeader->nFilledLen >
       pBufferHeader->nAllocLen)
    {
        DBGT_CRITICAL("API: incorrect nFilledLen value: %u nAllocLen: %u",
                    (unsigned) pBufferHeader->nFilledLen,
                    (unsigned) pBufferHeader->nAllocLen);
        DBGT_EPILOG("");
        return OMX_ErrorBadParameter;
    }

    // note: version checks should be backwards compatible.
    if (pBufferHeader->nSize != sizeof(OMX_BUFFERHEADERTYPE))
    {
        DBGT_CRITICAL("API: buffer header size mismatch");
        DBGT_EPILOG("");
        return OMX_ErrorBadParameter;
    }

    DBGT_PDEBUG("API: nFilledLen:%u nFlags:%x",
                (unsigned) pBufferHeader->nFilledLen,
                (unsigned) pBufferHeader->nFlags);

    if (pBufferHeader->nFlags & OMX_BUFFERFLAG_EOS)
    {
        DBGT_PDEBUG("API: OMX_BUFFERFLAG_EOS received");
    }

    if (pBufferHeader->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
    {
        DBGT_PDEBUG("API: OMX_BUFFERFLAG_CODECCONFIG received");
    }

#if 0
#ifdef OMX_SKIP64BIT
    if ((pBufferHeader->nTimeStamp.nLowPart != -1) && (pBufferHeader->nTimeStamp.nHighPart != -1))
    {
        if ( !((pBufferHeader->nTimeStamp.nLowPart == 0) && (pBufferHeader->nTimeStamp.nHighPart == 0)) &&
             !((pBufferHeader->nFilledLen == 0) && (pBufferHeader->nFlags & OMX_BUFFERFLAG_EOS)) &&
             !(pBufferHeader->nFlags & OMX_BUFFERFLAG_CODECCONFIG) )
        {
            OSAL_MutexLock(dec->timemutex);
            receive_timestamp(dec, &pBufferHeader->nTimeStamp);
            OSAL_MutexUnlock(dec->timemutex);
        }
    }
#else
    if (pBufferHeader->nTimeStamp != -1)
    {
        if ( !(pBufferHeader->nTimeStamp == 0 && pBufferHeader->nFilledLen == 0) &&
             !(pBufferHeader->nFlags & OMX_BUFFERFLAG_EOS) &&
             !(pBufferHeader->nFlags & OMX_BUFFERFLAG_CODECCONFIG) )
        {
            OSAL_MutexLock(dec->timemutex);
            receive_timestamp(dec, &pBufferHeader->nTimeStamp);
            OSAL_MutexUnlock(dec->timemutex);
        }
    }
#endif
#endif

    DBGT_EPILOG("");
    return decoder_push_buffer(hComponent, pBufferHeader,
                               pBufferHeader->nInputPortIndex);
}

static
    OMX_ERRORTYPE decoder_component_tunnel_request(OMX_IN OMX_HANDLETYPE
                                                   hComponent,
                                                   OMX_IN OMX_U32 nPort,
                                                   OMX_IN OMX_HANDLETYPE
                                                   hTunneledComp,
                                                   OMX_IN OMX_U32 nTunneledPort,
                                                   OMX_INOUT OMX_TUNNELSETUPTYPE
                                                   * pTunnelSetup)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CHECK_PARAM_NON_NULL(hComponent);
    OMX_DECODER *dec = GET_DECODER(hComponent);

    OMX_ERRORTYPE err = OMX_ErrorNone;

    PORT *port = decoder_map_index_to_port(dec, nPort);

    if (port == NULL)
    {
        DBGT_CRITICAL("API: bad port index:%d", (int) nPort);
        DBGT_EPILOG("");
        return OMX_ErrorBadPortIndex;
    }
    if (dec->state != OMX_StateLoaded && port->def.bEnabled)
    {
        DBGT_CRITICAL("API: port is not disabled");
        DBGT_EPILOG("");
        return OMX_ErrorIncorrectStateOperation;
    }

    DBGT_PDEBUG("API: setting up tunnel on port: %d", (int) nPort);
    DBGT_PDEBUG("API: tunnel component:%p tunnel port:%d",
                hTunneledComp, (int) nTunneledPort);

    if (hTunneledComp == NULL)
    {
        HantroOmx_port_setup_tunnel(port, NULL, 0, OMX_BufferSupplyUnspecified);
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }
#ifndef OMX_DECODER_TUNNELING_SUPPORT
    DBGT_CRITICAL("API: ERROR Tunneling unsupported");
    return OMX_ErrorTunnelingUnsupported;
#endif
    CHECK_PARAM_NON_NULL(pTunnelSetup);
    if (port->def.eDir == OMX_DirOutput)
    {
        // 3.3.11
        // the component that provides the output port of the tunneling has to do the following:
        // 1. Indicate its supplier preference in pTunnelSetup.
        // 2. Set the OMX_PORTTUNNELFLAG_READONLY flag to indicate that buffers
        //    from this output port are read-only and that the buffers cannot be shared
        //    through components or modified.

        // do not overwrite if something has been specified with SetParameter
        if (port->tunnel.eSupplier == OMX_BufferSupplyUnspecified)
            port->tunnel.eSupplier = OMX_BufferSupplyOutput;

        // if the component that provides the input port
        // wants to override the buffer supplier setting it will call our SetParameter
        // to override the setting put here.
        pTunnelSetup->eSupplier = port->tunnel.eSupplier;
        pTunnelSetup->nTunnelFlags = OMX_PORTTUNNELFLAG_READONLY;
        HantroOmx_port_setup_tunnel(port, hTunneledComp, nTunneledPort,
                                    port->tunnel.eSupplier);
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }
    else
    {
        // the input port is responsible for checking that the
        // ports are compatible
        // so get the portdefinition from the output port
        OMX_PARAM_PORTDEFINITIONTYPE other;

        memset(&other, 0, sizeof(other));
        INIT_OMX_VERSION_PARAM(other);
        other.nPortIndex = nTunneledPort;
        err =
            ((OMX_COMPONENTTYPE *) hTunneledComp)->GetParameter(hTunneledComp,
                                                                OMX_IndexParamPortDefinition,
                                                                &other);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("GetParameter failed (err=%x)", err);
            DBGT_EPILOG("");
            return err;
        }

        // next do the port compatibility checking
        if (port->def.eDomain != other.eDomain)
        {
            DBGT_CRITICAL("API: ports are not compatible (incompatible domains)");
            DBGT_EPILOG("");
            return OMX_ErrorPortsNotCompatible;
        }
        if (port == &dec->in)
        {
#ifdef OMX_DECODER_VIDEO_DOMAIN
            switch ((OMX_U32)other.format.video.eCompressionFormat)
            {
            case OMX_VIDEO_CodingAVC:
                break;
            case OMX_VIDEO_CodingH263:
                break;
            case OMX_VIDEO_CodingMPEG4:
                break;
            case OMX_VIDEO_CodingSORENSON:
                break;
            case OMX_VIDEO_CodingDIVX:
                break;
            case OMX_VIDEO_CodingDIVX3:
                break;
            case OMX_VIDEO_CodingAVS:
                break;
            case OMX_VIDEO_CodingVP8:
            // case OMX_VIDEO_CodingVPX: // Android 4.x uses non-standard coding type
                break;
            case OMX_VIDEO_CodingRV:
                break;
            case OMX_VIDEO_CodingWMV:
                break;
            case OMX_VIDEO_CodingMPEG2:
                break;
            case OMX_VIDEO_CodingVP6:
                break;
            case OMX_VIDEO_CodingMJPEG:
                break;
#ifdef IS_G2_DECODER
            case OMX_VIDEO_CodingHEVC:
                break;
            case OMX_VIDEO_CodingVP9:
                break;
#endif
            case OMX_VIDEO_CodingUnused:
                switch (other.format.video.eColorFormat)
                {
                case OMX_COLOR_FormatYUV420PackedPlanar:
                    break;
                case OMX_COLOR_FormatYUV420Planar:
                    break;
                case OMX_COLOR_FormatYUV420PackedSemiPlanar:
                    break;
                case OMX_COLOR_FormatYUV420SemiPlanar:
                    break;
                case OMX_COLOR_FormatYCbYCr:
                    break;
#if defined (IS_8190) || defined (IS_G1_DECODER)
                case OMX_COLOR_FormatYCrYCb:
                    break;
                case OMX_COLOR_FormatCbYCrY:
                    break;
                case OMX_COLOR_FormatCrYCbY:
                    break;
#endif
                default:
                    DBGT_CRITICAL("API: ports are not compatible (incompatible color format)");
                    DBGT_EPILOG("");
                    return OMX_ErrorPortsNotCompatible;
                }
                break;
            default:
                DBGT_CRITICAL("API: ports are not compatible (incompatible video coding)");
                DBGT_EPILOG("");
                return OMX_ErrorPortsNotCompatible;
            }
#endif // OMX_DECODER_VIDEO_DOMAIN
#ifdef OMX_DECODER_IMAGE_DOMAIN
            switch ((OMX_U32)other.format.image.eCompressionFormat)
            {
            case OMX_IMAGE_CodingJPEG:
                break;
            case OMX_IMAGE_CodingWEBP:
                break;
            case OMX_IMAGE_CodingUnused:
                break;
                switch (other.format.image.eColorFormat)
                {
                case OMX_COLOR_FormatYUV420PackedPlanar:
                    break;
                case OMX_COLOR_FormatYUV420Planar:
                    break;
                case OMX_COLOR_FormatYUV420PackedSemiPlanar:
                    break;
                case OMX_COLOR_FormatYUV420SemiPlanar:
                    break;
                case OMX_COLOR_FormatYCbYCr:
                    break;
#if defined (IS_8190) || defined (IS_G1_DECODER)
                case OMX_COLOR_FormatYCrYCb:
                    break;
                case OMX_COLOR_FormatCbYCrY:
                    break;
                case OMX_COLOR_FormatCrYCbY:
                    break;
#endif
                default:
                    DBGT_CRITICAL("API: ports are not compatible (incompatible color format)");
                    DBGT_EPILOG("");
                    return OMX_ErrorPortsNotCompatible;
                }
                break;
            default:
                DBGT_CRITICAL("ASYNC: ports are not compatible (incompatible image coding)");
                DBGT_EPILOG("");
                return OMX_ErrorPortsNotCompatible;
            }
#endif
        }
        if (port == &dec->inpp)
        {
            // was there any other post-processor input formats?
            if (other.format.image.eCompressionFormat != OMX_IMAGE_CodingUnused
               || other.format.image.eColorFormat !=
               OMX_COLOR_Format32bitARGB8888)
            {
                DBGT_CRITICAL("API: ports are not compatible (post-processor)");
                DBGT_EPILOG("");
                return OMX_ErrorPortsNotCompatible;
            }
        }
        if (pTunnelSetup->eSupplier == OMX_BufferSupplyUnspecified)
            pTunnelSetup->eSupplier = OMX_BufferSupplyInput;

        // need to send back the result of the buffer supply negotiation
        // to the component providing the output port.
        OMX_PARAM_BUFFERSUPPLIERTYPE param;

        memset(&param, 0, sizeof(param));
        INIT_OMX_VERSION_PARAM(param);

        param.eBufferSupplier = pTunnelSetup->eSupplier;
        param.nPortIndex = nTunneledPort;
        err =
            ((OMX_COMPONENTTYPE *) hTunneledComp)->SetParameter(hTunneledComp,
                                                                OMX_IndexParamCompBufferSupplier,
                                                                &param);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("SetParameter failed (err=%x)", err);
            DBGT_EPILOG("");
            return err;
        }

        // save tunneling details somewhere
        HantroOmx_port_setup_tunnel(port, hTunneledComp, nTunneledPort,
                                    pTunnelSetup->eSupplier);
        DBGT_PDEBUG("API: tunnel supplier: %s",
                    HantroOmx_str_omx_supplier(pTunnelSetup->eSupplier));
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

#ifdef USE_ANDROID_NATIVE_BUFFER
static OMX_ERRORTYPE decoder_get_extension_index(OMX_HANDLETYPE h,
                                               OMX_STRING cParameterName,
                                               OMX_INDEXTYPE* pIndexType)
{
    DBGT_PROLOG("");
    DBGT_PDEBUG("API: GetIndexExtension %s", cParameterName);

    if (strncmp(cParameterName, "OMX.google.android.index.enableAndroidNativeBuffers", 51) == 0)
    {
        *pIndexType = (OMX_INDEXTYPE)OMX_google_android_index_enableAndroidNativeBuffers;
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    if (strncmp(cParameterName, "OMX.google.android.index.getAndroidNativeBufferUsage", 52) == 0)
    {
        *pIndexType = (OMX_INDEXTYPE)OMX_google_android_index_getAndroidNativeBufferUsage;
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    if (strncmp(cParameterName, "OMX.google.android.index.useAndroidNativeBuffer2", 48) == 0)
    {
        *pIndexType = (OMX_INDEXTYPE)OMX_google_android_index_useAndroidNativeBuffer;
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }
    DBGT_EPILOG("");
    return OMX_ErrorUnsupportedIndex;
}
#else

static OMX_ERRORTYPE decoder_get_extension_index(
        OMX_IN  OMX_HANDLETYPE hComponent,
        OMX_IN  OMX_STRING cParameterName,
        OMX_OUT OMX_INDEXTYPE* pIndexType)
{
    UNUSED_PARAMETER(hComponent);
    UNUSED_PARAMETER(cParameterName);
    UNUSED_PARAMETER(pIndexType);

    DBGT_ERROR("API: extensions not supported");
    return OMX_ErrorNotImplemented;
}
#endif

static OMX_ERRORTYPE decoder_useegl_image(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE** ppBufferHdr, OMX_U32 nPortIndex, OMX_PTR pAppPrivate, void* pBuffer)
{
    UNUSED_PARAMETER(hComponent);
    UNUSED_PARAMETER(ppBufferHdr);
    UNUSED_PARAMETER(nPortIndex);
    UNUSED_PARAMETER(pAppPrivate);
    UNUSED_PARAMETER(pBuffer);

    DBGT_ERROR("API: Use egl image not implemented");
    return OMX_ErrorNotImplemented;
}

static OMX_ERRORTYPE decoder_enum_roles(OMX_HANDLETYPE hComponent, OMX_U8 *cRole, OMX_U32 nIndex)
{
    UNUSED_PARAMETER(hComponent);
    UNUSED_PARAMETER(cRole);
    UNUSED_PARAMETER(nIndex);

    DBGT_ERROR("API: enum roles not implemented");
    return OMX_ErrorNotImplemented;
}

#ifdef OMX_DECODER_VIDEO_DOMAIN
HANTROOMXDEC_EXPORT
    OMX_ERRORTYPE HantroHwDecOmx_video_constructor(OMX_COMPONENTTYPE * comp,
                                               OMX_STRING name)
#endif
#ifdef OMX_DECODER_IMAGE_DOMAIN
HANTROOMXDEC_EXPORT
    OMX_ERRORTYPE HantroHwDecOmx_image_constructor(OMX_COMPONENTTYPE * comp,
                                                   OMX_STRING name)
#endif
{
    CALLSTACK;

#ifdef IS_G1_DECODER
    DBGT_TRACE_INIT(g1dec);
#elif IS_G2_DECODER
     DBGT_TRACE_INIT(g2dec);
#else
    DBGT_TRACE_INIT(81x0dec);
#endif

    CHECK_PARAM_NON_NULL(comp);
    CHECK_PARAM_NON_NULL(name);

    DBGT_ASSERT(comp->pComponentPrivate == 0);

    OMX_ERRORTYPE err = OMX_ErrorNone;

#ifdef OMX_DECODER_VIDEO_DOMAIN
    /* Check that the prefix is correct */
    if (strncmp(name, COMPONENT_NAME_VIDEO, strlen(COMPONENT_NAME_VIDEO)))
    {
        DBGT_ERROR("Invalid component name, got %s, expected %s", name, COMPONENT_NAME_VIDEO);
        return OMX_ErrorInvalidComponentName;
    }

    /* Check for the specific name and set the decoder according to it */
    char *specificName = name + strlen(COMPONENT_NAME_VIDEO);
    DBGT_PDEBUG("specific component name = %s", specificName);
#else
    /* Check that the prefix is correct */
    if (strncmp(name, COMPONENT_NAME_IMAGE, strlen(COMPONENT_NAME_IMAGE)))
    {
        DBGT_ERROR("Invalid component name, got %s, expected %s", name, COMPONENT_NAME_IMAGE);
        return OMX_ErrorInvalidComponentName;
    }

    /* Check for the specific name and set the decoder according to it */
    char *specificName = name + strlen(COMPONENT_NAME_IMAGE);
    DBGT_PDEBUG("specific component name = %s", specificName);
#endif

    // work around an issue in the conformance tester.
    // In the ResourceExhaustion test the conformance tester tries to create
    // components more than the system can handle. However if it is the constructor
    // that fails instead of the Idle state transition,
    // (the component is never actually created but only the handle IS created)
    // the tester passes the component to the core for cleaning up which then just ends up calling the DeInit function
    // on an object that hasn't ever even been constructed.
    // To work around this problem we set the DeInit function pointer here and then in the DeInit function
    // check if the pComponentPrivate is set to a non NULL value. (i.e. has the object really been created). Lame.
    //
    comp->ComponentDeInit = decoder_deinit;

#ifdef INCLUDE_TB
#ifndef IS_G2_DECODER
    TBSetDefaultCfg(&tb_cfg);
    tb_cfg.dec_params.hw_version = 10000;
    tb_cfg.dec_params.tiled_ref_support = 1;
    tb_cfg.pp_params.tiled_ref_support = 1;
    tb_cfg.dec_params.field_dpb_support = 1;
#else
    TBSetDefaultCfg(&tb_cfg);
#endif
#endif

    OMX_DECODER *dec = (OMX_DECODER *) OSAL_Malloc(sizeof(OMX_DECODER));

    if (dec == 0)
    {
        DBGT_CRITICAL("OSAL_Malloc failed");
        return OMX_ErrorInsufficientResources;
    }

    DBGT_PDEBUG("Component version: %d.%d.%d.%d",
             COMPONENT_VERSION_MAJOR, COMPONENT_VERSION_MINOR,
             COMPONENT_VERSION_REVISION, COMPONENT_VERSION_STEP);

    memset(dec, 0, sizeof(OMX_DECODER));

    err = OSAL_MutexCreate(&dec->statemutex);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("OSAL_MutexCreate statemutex failed (err=%x)", err);
        goto INIT_FAILURE;
    }

    err = OSAL_MutexCreate(&dec->timemutex);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("OSAL_MutexCreate timemutex failed (err=%x)", err);
        goto INIT_FAILURE;
    }

    err = OSAL_MutexCreate(&dec->threadmutex);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("OSAL_MutexCreate threadmutex failed (err=%x)", err);
        goto INIT_FAILURE;
    }

    dec->frame_out.bus_address = 0;

    err = HantroOmx_port_init(&dec->in, 4, 4, 4, DEFAULT_INPUT_BUFFER_SIZE);

    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("HantroOmx_port_init (in) failed (err=%x)", err);
        goto INIT_FAILURE;
    }

    err = HantroOmx_port_init(&dec->out, 6, 6, 6, DEFAULT_OUTPUT_BUFFER_SIZE);

    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("HantroOmx_port_init (out) failed (err=%x)", err);
        goto INIT_FAILURE;
    }

    err = HantroOmx_port_init(&dec->inpp, 1, 1, 1, 4);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("HantroOmx_port_init (inpp) failed (err=%x)", err);
        goto INIT_FAILURE;
    }

    err = OSAL_AllocatorInit(&dec->alloc);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("OSAL_AllocatorInit failed (err=%x)", err);
        goto INIT_FAILURE;
    }

    INIT_OMX_VERSION_PARAM(dec->in.def);
    INIT_OMX_VERSION_PARAM(dec->out.def);
    INIT_OMX_VERSION_PARAM(dec->inpp.def);
    INIT_OMX_VERSION_PARAM(dec->conf_rotation);
    INIT_OMX_VERSION_PARAM(dec->conf_mirror);
    INIT_OMX_VERSION_PARAM(dec->conf_contrast);
    INIT_OMX_VERSION_PARAM(dec->conf_brightness);
    INIT_OMX_VERSION_PARAM(dec->conf_saturation);
    INIT_OMX_VERSION_PARAM(dec->conf_blend);
    INIT_OMX_VERSION_PARAM(dec->conf_rect);
#ifdef SET_OUTPUT_CROP_RECT
    INIT_OMX_VERSION_PARAM(dec->output_cropping_rect);
#endif
    INIT_OMX_VERSION_PARAM(dec->conf_mask_offset);
    INIT_OMX_VERSION_PARAM(dec->conf_mask);
    INIT_OMX_VERSION_PARAM(dec->conf_dither);

    dec->state = OMX_StateLoaded;
    dec->statetrans = OMX_StateLoaded;
    dec->run = OMX_TRUE;
    dec->portReconfigPending = OMX_FALSE;
    dec->disablingOutPort = OMX_FALSE;
    dec->flushingOutPort = OMX_FALSE;
    dec->isMvcStream = OMX_FALSE;
    dec->self = comp;   // hold a backpointer to the component handle.
    // callback interface requires this

    dec->in.def.nPortIndex = PORT_INDEX_INPUT;
    dec->in.def.eDir = OMX_DirInput;
    dec->in.def.bEnabled = OMX_TRUE;
    dec->in.def.bPopulated = OMX_FALSE;

    dec->out.def.nPortIndex = PORT_INDEX_OUTPUT;
    dec->out.def.eDir = OMX_DirOutput;
    dec->out.def.bEnabled = OMX_TRUE;
    dec->out.def.bPopulated = OMX_FALSE;

    dec->shared_data.decInstance = dec;
    dec->shared_data.inbuff = NULL;
    dec->shared_data.EOS = OMX_FALSE;
    dec->shared_data.output_thread_run = OMX_FALSE;
    dec->shared_data.eos_bus_address = 0;
    dec->useExternalAlloc = OMX_FALSE;
    memset(dec->prevPicIdList, -1, sizeof(dec->prevPicIdList));

#ifdef IS_G2_DECODER
    dec->bitDepth = 8;
    INIT_OMX_VERSION_PARAM(dec->g2Conf);
    dec->g2Conf.nPortIndex = PORT_INDEX_OUTPUT;
    dec->g2Conf.bEnableTiled = OMX_FALSE;
    dec->g2Conf.ePixelFormat = OMX_VIDEO_G2PixelFormat_8bit;
    dec->g2Conf.bEnableRFC = OMX_FALSE;
    dec->g2Conf.bEnableSecureMode = OMX_FALSE;
#endif

#ifdef IS_G1_DECODER
    INIT_OMX_VERSION_PARAM(dec->g1Conf);
    dec->g1Conf.nPortIndex = PORT_INDEX_OUTPUT;
    dec->g1Conf.bEnableTiled = OMX_FALSE;
    dec->g1Conf.bAllowFieldDBP = OMX_FALSE;
    dec->g1Conf.bEnableSecureMode = OMX_FALSE;
#endif

#ifdef OMX_DECODER_VIDEO_DOMAIN
    if (specificName == NULL || strcmp(specificName,".avc") == 0) {
        DBGT_PDEBUG("Creating h264 decoder");
        strcpy((char *) dec->role, "video_decoder.avc");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName,".mpeg4") == 0) {
        DBGT_PDEBUG("Creating mpeg4 decoder");
        strcpy((char *) dec->role, "video_decoder.mpeg4");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName,".h263") == 0) {
        DBGT_PDEBUG("Creating h263 decoder");
        strcpy((char *) dec->role, "video_decoder.h263");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingH263;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName,".vp8") == 0) {
        DBGT_PDEBUG("Creating vp8 decoder");
        strcpy((char *) dec->role, "video_decoder.vp8");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingVP8;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".vp6") == 0) {
        DBGT_PDEBUG("Creating vp6 decoder");
        strcpy((char *) dec->role, "video_decoder.vp6");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingVP6;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".avs") == 0) {
        DBGT_PDEBUG("Creating avs decoder");
        strcpy((char *) dec->role, "video_decoder.avs");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVS;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".rv") == 0) {
        DBGT_PDEBUG("Creating rv decoder");
        strcpy((char *) dec->role, "video_decoder.rv");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingRV;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".mpeg2") == 0) {
        DBGT_PDEBUG("Creating mpeg2 decoder");
        strcpy((char *) dec->role, "video_decoder.mpeg2");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingMPEG2;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".wmv") == 0) {
        DBGT_PDEBUG("Creating vc1 decoder");
        strcpy((char *) dec->role, "video_decoder.wmv");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingWMV;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".jpeg") == 0) {
        DBGT_PDEBUG("Creating mjpeg decoder");
        strcpy((char *) dec->role, "video_decoder.jpeg");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingMJPEG;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".pp") == 0) {
        DBGT_PDEBUG("Creating postprocessor");
        strcpy((char *) dec->role, "video_decoder.pp");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    }
#ifdef IS_G2_DECODER
    else if (strcmp(specificName, ".hevc") == 0) {
        DBGT_PDEBUG("Creating hevc decoder");
        strcpy((char *) dec->role, "video_decoder.hevc");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingHEVC;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName, ".vp9") == 0) {
        DBGT_PDEBUG("Creating vp9 decoder");
        strcpy((char *) dec->role, "video_decoder.vp9");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingVP9;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
#endif
    else {
        DBGT_PDEBUG("Creating h264 decoder");
        strcpy((char *) dec->role, "video_decoder.avc");
        dec->in.def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }


    dec->in.def.eDomain = OMX_PortDomainVideo;
    dec->in.def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    dec->in.def.format.video.nFrameWidth = 0;
    dec->in.def.format.video.nFrameHeight = 0;
    dec->in.def.format.video.nBitrate = 0;
    dec->in.def.format.video.xFramerate = 0;
    // conformance tester wants to check these in standard component tests
#ifdef CONFORMANCE
    dec->in.def.format.video.nFrameWidth         = 176;
    dec->in.def.format.video.nFrameHeight        = 144;
    dec->in.def.format.video.nBitrate            = 64000;
    dec->in.def.format.video.xFramerate          = 15 << 16;
#endif

    dec->out.def.eDomain = OMX_PortDomainVideo;
    dec->out.def.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    dec->out.def.format.video.eColorFormat =
        OMX_COLOR_FormatYUV420PackedSemiPlanar;
    dec->out.def.format.video.nFrameWidth = 0;
    dec->out.def.format.video.nFrameHeight = 0;
    dec->out.def.format.video.nStride = -1;
    dec->out.def.format.video.nSliceHeight = -1;
#ifdef CONFORMANCE
    dec->out.def.format.video.eColorFormat = OMX_COLOR_FormatYUV420Planar;
    dec->out.def.format.video.nFrameWidth = 176;
    dec->out.def.format.video.nFrameHeight = 144;
    dec->out.def.format.video.nStride = 176;
    dec->out.def.format.video.nSliceHeight = 144;
#endif
    dec->WMVFormat = OMX_VIDEO_WMVFormat9; /* for conformance tester */
#endif //OMX_DECODER_VIDEO_DOMAIN
#ifdef OMX_DECODER_IMAGE_DOMAIN

    if (specificName == NULL || strcmp(specificName,".jpeg") == 0) {
        DBGT_PDEBUG("Creating jpeg decoder");
        strcpy((char *) dec->role, "image_decoder.jpeg");
        dec->in.def.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else if (strcmp(specificName,".webp") == 0) {
        DBGT_PDEBUG("Creating webp decoder");
        strcpy((char *) dec->role, "image_decoder.webp");
        dec->in.def.format.image.eCompressionFormat = OMX_IMAGE_CodingWEBP;
#ifdef USE_EXTERNAL_BUFFER
        dec->useExternalAlloc = OMX_TRUE;
#endif
    }
    else {
        DBGT_PDEBUG("Creating jpeg decoder");
        strcpy((char *) dec->role, "image_decoder.jpeg");
        dec->in.def.format.image.eCompressionFormat = OMX_IMAGE_CodingJPEG;
    }
    dec->in.def.eDomain = OMX_PortDomainImage;
    dec->in.def.format.image.eColorFormat = OMX_COLOR_FormatUnused;
    dec->in.def.format.image.nFrameWidth = 640;
    dec->in.def.format.image.nFrameHeight = 480;

    dec->out.def.eDomain = OMX_PortDomainImage;
    dec->out.def.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
    dec->out.def.format.image.eColorFormat = OMX_COLOR_FormatYUV420PackedSemiPlanar;
    dec->out.def.format.image.nFrameWidth = 0;
    dec->out.def.format.image.nFrameHeight = 0;
    dec->out.def.format.image.nStride = -1;
    dec->out.def.format.image.nSliceHeight = -1;
#ifdef CONFORMANCE
    dec->out.def.format.image.eColorFormat = OMX_COLOR_FormatYUV420Planar;
    dec->out.def.format.image.nFrameWidth = 640;
    dec->out.def.format.image.nFrameHeight = 480;
    dec->out.def.format.image.nStride = 640;
    dec->out.def.format.image.nSliceHeight = 16;
#endif

#endif // OMX_DECODER_IMAGE_DOMAIN

    dec->inpp.def.nPortIndex = PORT_INDEX_PP;
    dec->inpp.def.eDir = OMX_DirInput;
    dec->inpp.def.bEnabled = OMX_TRUE;
    dec->inpp.def.bPopulated = OMX_FALSE;
    dec->inpp.def.eDomain = OMX_PortDomainImage;
    dec->inpp.def.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
    dec->inpp.def.format.image.eColorFormat = OMX_COLOR_Format32bitARGB8888;

    // set the component interfarce functions.
    // here one could use the nVersion of the OMX_COMPONENTTYPE
    // to set these functions specifially for OpenMAX 1.0 version.
    comp->GetComponentVersion = decoder_get_version;
    comp->SendCommand = decoder_send_command;
    comp->GetParameter = decoder_get_parameter;
    comp->SetParameter = decoder_set_parameter;
    comp->SetCallbacks = decoder_set_callbacks;
    comp->GetConfig = decoder_get_config;
    comp->SetConfig = decoder_set_config;
    comp->GetExtensionIndex = decoder_get_extension_index;
    comp->GetState = decoder_get_state;
    comp->ComponentTunnelRequest = decoder_component_tunnel_request;
    comp->UseBuffer = decoder_use_buffer;
    comp->AllocateBuffer = decoder_allocate_buffer;
    comp->FreeBuffer = decoder_free_buffer;
    comp->EmptyThisBuffer = decoder_empty_this_buffer;
    comp->FillThisBuffer = decoder_fill_this_buffer;
    comp->ComponentDeInit = decoder_deinit;
    comp->UseEGLImage = decoder_useegl_image;
    comp->ComponentRoleEnum = decoder_enum_roles;
    comp->pComponentPrivate = dec;

    // this needs to be the last thing to be done in the code
    // cause this will create the component thread which will access the
    // decoder object we set up above. So, we better make sure that the object
    // is fully constructed.
    // note: could set the interface pointers to zero or something should this fail
    err = HantroOmx_basecomp_init(&dec->base, decoder_thread_main, dec);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("basecomp_init failed");
        goto INIT_FAILURE;
    }
    return OMX_ErrorNone;

  INIT_FAILURE:
    DBGT_ASSERT(dec);
    DBGT_CRITICAL("%s %s", "init failure",
                HantroOmx_str_omx_err(err));

    if (dec->statemutex)
        OSAL_MutexDestroy(dec->statemutex);

    if (dec->timemutex)
        OSAL_MutexDestroy(dec->timemutex);

    if (dec->threadmutex)
        OSAL_MutexDestroy(dec->threadmutex);

#ifndef CONFORMANCE // conformance tester calls deinit
    if (HantroOmx_port_is_allocated(&dec->in))
        HantroOmx_port_destroy(&dec->in);
    if (HantroOmx_port_is_allocated(&dec->out))
        HantroOmx_port_destroy(&dec->out);
    if (HantroOmx_port_is_allocated(&dec->inpp))
        HantroOmx_port_destroy(&dec->inpp);
    //HantroOmx_basecomp_destroy(&dec->base);
    if (OSAL_AllocatorIsReady(&dec->alloc))
        OSAL_AllocatorDestroy(&dec->alloc);
    OSAL_Free(dec);
#endif
    return err;
}

static OMX_ERRORTYPE supply_tunneled_port(OMX_DECODER * dec, PORT * port)
{
    CALLSTACK;

    DBGT_PROLOG("");

    DBGT_ASSERT(port->tunnelcomp);

    DBGT_PDEBUG("ASYNC: supplying buffers for: %p (%d)",
                port->tunnelcomp, (int) port->tunnelport);

    OMX_ERRORTYPE err = OMX_ErrorNone;

    OMX_PARAM_PORTDEFINITIONTYPE param;

    memset(&param, 0, sizeof(param));
    INIT_OMX_VERSION_PARAM(param);
    param.nPortIndex = port->tunnelport;
    // get the port definition, cause we need the number of buffers
    // that we need to allocate for this port
    err =
        ((OMX_COMPONENTTYPE *) port->tunnelcomp)->GetParameter(port->tunnelcomp,
                                                               OMX_IndexParamPortDefinition,
                                                               &param);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("GetParameter failed (err=%x)", err);
        DBGT_EPILOG("");
        return err;
    }

    // this operation should be fine without locking.
    // there's no access to the supply queue through the public API,
    // so the component thread is the only thread doing the access here.

    // 2.1.7.2
    // 2. Allocate buffers according to the maximum of its own requirements and the
    //    requirements of the tunneled port.

    OMX_U32 count =
        param.nBufferCountActual >
        port->def.nBufferCountActual ? param.nBufferCountActual : port->def.
        nBufferCountActual;
    OMX_U32 size =
        param.nBufferSize >
        port->def.nBufferSize ? param.nBufferSize : port->def.nBufferSize;
    DBGT_PDEBUG("ASYNC: allocating %d buffers", (int) count);

    OMX_U32 i = 0;

    for(i = 0; i < count; ++i)
    {
        OMX_U8 *bus_data = NULL;

        OSAL_BUS_WIDTH bus_address = 0;

        OMX_U32 allocsize = size;

        // allocate the memory chunk for the buffer
        DBGT_PDEBUG("supply_tunneled_port: OSAL_AllocatorAllocMem");

        err =
            OSAL_AllocatorAllocMem(&dec->alloc, &allocsize, &bus_data,
                                   &bus_address);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("ASYNC: failed to supply buffer (%d bytes)",
                        (int) param.nBufferSize);
            goto FAIL;
        }
        DBGT_PDEBUG("API: allocated supply buffer size:%u @physical addr:0x%08lx @logical addr:%p",
                (unsigned) allocsize, bus_address, bus_data);

        // allocate the BUFFER object
        BUFFER *buff = NULL;

        HantroOmx_port_allocate_next_buffer(port, &buff);
        if (buff == NULL)
        {
            DBGT_CRITICAL("ASYNC: failed to supply buffer object");
            OSAL_AllocatorFreeMem(&dec->alloc, allocsize, bus_data,
                                  bus_address);
            goto FAIL;
        }
        buff->flags |= BUFFER_FLAG_MY_BUFFER;
        buff->bus_data = bus_data;
        buff->bus_address = bus_address;
        buff->allocsize = allocsize;
        buff->header->pBuffer = bus_data;
        buff->header->pAppPrivate = NULL;
        buff->header->nAllocLen = size;
        // the header will remain empty because the
        // tunneled port allocates it.
        buff->header = NULL;
        err =
            ((OMX_COMPONENTTYPE *) port->tunnelcomp)->UseBuffer(port->
                                                                tunnelcomp,
                                                                &buff->header,
                                                                port->
                                                                tunnelport,
                                                                NULL, allocsize,
                                                                bus_data);

        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("ASYNC: use buffer call failed on tunneled component:%s",
                        HantroOmx_str_omx_err(err));
            goto FAIL;
        }
        // the tunneling component is responsible for allocating the
        // buffer header and filling in the buffer information.
        DBGT_ASSERT(buff->header);
        DBGT_ASSERT(buff->header != &buff->headerdata);
        DBGT_ASSERT(buff->header->nSize);
        DBGT_ASSERT(buff->header->nVersion.nVersion);
        DBGT_ASSERT(buff->header->nAllocLen);

        DBGT_PDEBUG("ASYNC: supplied buffer data virtual address:%p size:%d header:%p",
                    bus_data, (int) allocsize, buff->header);
    }
    DBGT_ASSERT(HantroOmx_port_buffer_count(port) >= port->def.nBufferCountActual);

    DBGT_PDEBUG("ASYNC: port is populated");
    port->def.bPopulated = OMX_TRUE;
    err = OSAL_EventSet(port->bufferRdy);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("OSAL_EventSet failed");
        DBGT_EPILOG("");
        return err;
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
  FAIL:
    DBGT_PDEBUG("ASYNC: freeing already supplied buffers");
    // must free any buffers we have allocated
    count = HantroOmx_port_buffer_count(port);
    for(i = 0; i < count; ++i)
    {
        BUFFER *buff = NULL;

        HantroOmx_port_get_allocated_buffer_at(port, &buff, i);
        DBGT_ASSERT(buff);
        DBGT_ASSERT(buff->bus_data);
        DBGT_ASSERT(buff->bus_address);

        if (buff->header)
            ((OMX_COMPONENTTYPE *) port->tunnelcomp)->FreeBuffer(port->
                                                                 tunnelcomp,
                                                                 port->
                                                                 tunnelport,
                                                                 buff->header);

        OSAL_AllocatorFreeMem(&dec->alloc, buff->allocsize, buff->bus_data,
                              buff->bus_address);
    }
    HantroOmx_port_release_all_allocated(port);
    DBGT_EPILOG("");
    return err;
}

static OMX_ERRORTYPE unsupply_tunneled_port(OMX_DECODER * dec, PORT * port)
{
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(port->tunnelcomp);

    DBGT_PDEBUG("ASYNC: removing buffers from: %p (%d)",
                port->tunnelcomp, (int) port->tunnelport);

    // tell the non-supplier to release them buffers
    OMX_U32 count = HantroOmx_port_buffer_count(port);

    OMX_U32 i;

    for(i = 0; i < count; ++i)
    {
        BUFFER *buff = NULL;

        HantroOmx_port_get_allocated_buffer_at(port, &buff, i);
        DBGT_ASSERT(buff);
        DBGT_ASSERT(buff->bus_data);
        DBGT_ASSERT(buff->bus_address);
        DBGT_ASSERT(buff->header != &buff->headerdata);
        ((OMX_COMPONENTTYPE *) port->tunnelcomp)->FreeBuffer(port->tunnelcomp,
                                                             port->tunnelport,
                                                             buff->header);
        OSAL_AllocatorFreeMem(&dec->alloc, buff->allocsize, buff->bus_data,
                              buff->bus_address);
    }
    HantroOmx_port_release_all_allocated(port);
    port->def.bPopulated = OMX_FALSE;
    OMX_ERRORTYPE err = OSAL_EventReset(port->bufferRdy);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("OSAL_EventReset failed");
        DBGT_EPILOG("");
        return err;
    }

    // since we've allocated the buffers, and they have been
    // destroyed empty the port's buffer queue
    OMX_BOOL loop = OMX_TRUE;

    while(loop)
    {
        loop = HantroOmx_port_pop_buffer(port);
    }

    if (port == &dec->out)
        dec->buffer = NULL;

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE transition_to_idle_from_loaded(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    // the state transition cannot complete until all
    // enabled ports are populated and the component has acquired
    // all of its static resources.
    DBGT_ASSERT(dec->state == OMX_StateLoaded);
    DBGT_ASSERT(dec->statetrans == OMX_StateIdle);
    DBGT_ASSERT(dec->codec == NULL);
    DBGT_ASSERT(dec->frame_in.bus_data == NULL);
    DBGT_ASSERT(dec->ts_buf.ts_data== NULL);
    DBGT_ASSERT(dec->frame_out.bus_data == NULL);
    DBGT_ASSERT(dec->mask.bus_data == NULL);

    OMX_ERRORTYPE err = OMX_ErrorHardware;

    DBGT_PDEBUG("ASYNC: input port 0 is tunneled with: %p port: %d supplier:%s",
                dec->in.tunnelcomp, (int) dec->in.tunnelport,
                HantroOmx_str_omx_supplier(dec->in.tunnel.eSupplier));

    DBGT_PDEBUG("ASYNC: input port 2 is tunneled with: %p port: %d supplier:%s",
                dec->inpp.tunnelcomp, (int) dec->inpp.tunnelport,
                HantroOmx_str_omx_supplier(dec->inpp.tunnel.eSupplier));

    DBGT_PDEBUG("ASYNC: output port 1 is tunneled with: %p port: %d supplier:%s",
                dec->out.tunnelcomp, (int) dec->out.tunnelport,
                HantroOmx_str_omx_supplier(dec->out.tunnel.eSupplier));

    if (HantroOmx_port_is_supplier(&dec->in))
        if (supply_tunneled_port(dec, &dec->in) != OMX_ErrorNone) {
            DBGT_CRITICAL("supply_tunneled_port (in) failed");
            goto FAIL;
        }

    if (HantroOmx_port_is_supplier(&dec->out))
        if (supply_tunneled_port(dec, &dec->out) != OMX_ErrorNone) {
            DBGT_CRITICAL("supply_tunneled_port (out) failed");
            goto FAIL;
        }

    if (HantroOmx_port_is_supplier(&dec->inpp))
        if (supply_tunneled_port(dec, &dec->inpp) != OMX_ErrorNone) {
            DBGT_CRITICAL("supply_tunneled_port (inpp) failed");
            goto FAIL;
        }

    DBGT_PDEBUG("ASYNC: waiting for buffers now!");

    while(!HantroOmx_port_is_ready(&dec->in) ||
          !HantroOmx_port_is_ready(&dec->out) /*||
          !HantroOmx_port_is_ready(&dec->inpp)*/)
    {
        OSAL_BOOL timeout;

        OSAL_EventWait(dec->in.bufferRdy, TIMEOUT, &timeout);
        if (timeout == OMX_TRUE)
        {
            err = OMX_ErrorTimeout;
            goto FAIL;
        }

        OSAL_EventWait(dec->out.bufferRdy, TIMEOUT, &timeout);
        if (timeout == OMX_TRUE)
        {
            err = OMX_ErrorTimeout;
            goto FAIL;
        }
    }
    DBGT_PDEBUG("ASYNC: got all buffers");

#if (defined OMX_DECODER_VIDEO_DOMAIN)
    switch ((OMX_U32)dec->in.def.format.video.eCompressionFormat)
    {
#ifdef ENABLE_CODEC_H264
    case OMX_VIDEO_CodingAVC:
        strcpy((char *) dec->role, "video_decoder.avc");
        dec->codec =
            HantroHwDecOmx_decoder_create_h264(dec->alloc.pdwl,
                                               dec->isMvcStream,
                                               &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created h264 codec");
        break;
#endif

#ifdef ENABLE_CODEC_MPEG4
    case OMX_VIDEO_CodingH263:
        strcpy((char *) dec->role, "video_decoder.h263");
        dec->codec =
            HantroHwDecOmx_decoder_create_mpeg4(dec->alloc.pdwl,
                                                dec->deblock,
                                                MPEG4FORMAT_H263,
                                                &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created h263 codec");
        break;
    case OMX_VIDEO_CodingMPEG4:
        strcpy((char *) dec->role, "video_decoder.mpeg4");
        dec->codec =
            HantroHwDecOmx_decoder_create_mpeg4(dec->alloc.pdwl,
                                                dec->deblock,
                                                MPEG4FORMAT_MPEG4,
                                                &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created mpeg4 codec");
        break;
    case OMX_VIDEO_CodingSORENSON:
        strcpy((char *) dec->role, "video_decoder.mpeg4");
        dec->codec =
            HantroHwDecOmx_decoder_create_mpeg4(dec->alloc.pdwl,
                                                dec->deblock,
                                                MPEG4FORMAT_SORENSON,
                                                &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created Sorenson codec");
        break;
#endif

#ifdef ENABLE_CODEC_DIVX
    case OMX_VIDEO_CodingDIVX:
        strcpy((char *) dec->role, "video_decoder.mpeg4");
        dec->codec =
            HantroHwDecOmx_decoder_create_mpeg4(dec->alloc.pdwl,
                                                dec->deblock,
                                                MPEG4FORMAT_CUSTOM_1,
                                                &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created DivX codec");
        break;
    case OMX_VIDEO_CodingDIVX3:
        strcpy((char *) dec->role, "video_decoder.mpeg4");
        dec->codec =
            HantroHwDecOmx_decoder_create_mpeg4(dec->alloc.pdwl,
                                                dec->deblock,
                                                MPEG4FORMAT_CUSTOM_1_3,
                                                &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created DivX 3 codec");
        break;
#endif

#ifdef ENABLE_CODEC_RV
    case OMX_VIDEO_CodingRV:
        strcpy((char *) dec->role, "video_decoder.rv");
        dec->codec =
            HantroHwDecOmx_decoder_create_rv(dec->alloc.pdwl,
                                             dec->bIsRV8, dec->imageSize,
                                             dec->width, dec->height,
                                             &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created RV codec");
        break;
#endif

#ifdef ENABLE_CODEC_VC1
    case OMX_VIDEO_CodingWMV:
        strcpy((char *) dec->role, "video_decoder.wmv");
        dec->codec = HantroHwDecOmx_decoder_create_vc1(dec->alloc.pdwl,
                                                       &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created vc1 codec");
        break;
#endif

#ifdef ENABLE_CODEC_VP8
    case OMX_VIDEO_CodingVP8:
    // case OMX_VIDEO_CodingVPX: // Android 4.x uses non-standard coding type
        strcpy((char *) dec->role, "video_decoder.vp8");
        dec->codec = HantroHwDecOmx_decoder_create_vp8(dec->alloc.pdwl,
                                                       &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created vp8 codec");
        break;
#endif

#ifdef ENABLE_CODEC_AVS
    case OMX_VIDEO_CodingAVS:
        strcpy((char *) dec->role, "video_decoder.avs");
        dec->codec = HantroHwDecOmx_decoder_create_avs(dec->alloc.pdwl,
                                                       &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created avs codec");
        break;
#endif

#ifdef ENABLE_CODEC_VP6
    case OMX_VIDEO_CodingVP6:
        strcpy((char *) dec->role, "video_decoder.vp6");
        dec->codec = HantroHwDecOmx_decoder_create_vp6(dec->alloc.pdwl,
                                                        &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created vp6 codec");
        break;
#endif

#ifdef ENABLE_CODEC_MPEG2
    case OMX_VIDEO_CodingMPEG2:
        strcpy((char *) dec->role, "video_decoder.mpeg2");
        dec->codec = HantroHwDecOmx_decoder_create_mpeg2(dec->alloc.pdwl,
                                                         &dec->g1Conf);
        DBGT_PDEBUG("ASYNC: created mpeg2 codec");
        break;
#endif

#ifdef ENABLE_CODEC_MJPEG
    case OMX_VIDEO_CodingMJPEG:
        strcpy((char *) dec->role, "video_decoder.jpeg");
        dec->codec = HantroHwDecOmx_decoder_create_jpeg(OMX_TRUE);
        DBGT_PDEBUG("ASYNC: created mjpeg codec");
        break;
#endif

#ifdef IS_G2_DECODER
#ifdef ENABLE_CODEC_HEVC
    case OMX_VIDEO_CodingHEVC:
        strcpy((char *) dec->role, "video_decoder.hevc");
        dec->codec = HantroHwDecOmx_decoder_create_hevc(dec->alloc.pdwl,
                                                        &dec->g2Conf);
        DBGT_PDEBUG("ASYNC: created hevc codec");
        break;
#endif

#ifdef ENABLE_CODEC_VP9
    case OMX_VIDEO_CodingVP9:
        strcpy((char *) dec->role, "video_decoder.vp9");
        dec->codec = HantroHwDecOmx_decoder_create_vp9(dec->alloc.pdwl,
                                                       &dec->g2Conf);
        DBGT_PDEBUG("ASYNC: created vp9 codec");
        break;
#endif
#endif

#ifdef ENABLE_PP
    case OMX_VIDEO_CodingUnused:
        switch (dec->in.def.format.video.eColorFormat)
        {
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420PackedPlanar:
        case OMX_COLOR_FormatYUV420SemiPlanar:
        case OMX_COLOR_FormatYUV420PackedSemiPlanar:
        case OMX_COLOR_FormatYCbYCr:   // 422-interleaved
#if defined (IS_8190) || defined (IS_G1_DECODER)
        case OMX_COLOR_FormatYCrYCb:
        case OMX_COLOR_FormatCbYCrY:
        case OMX_COLOR_FormatCrYCbY:
#endif
            // post-proc also supports 420 semi-planar in tiled format, but there's no OpenMAX color format for this.
            strcpy((char *) dec->role, "video_decoder.pp");
            dec->codec =
                HantroHwDecOmx_decoder_create_pp(dec->in.def.format.video.
                                                 nFrameWidth,
                                                 dec->in.def.format.video.
                                                 nFrameHeight,
                                                 dec->in.def.format.video.
                                                 eColorFormat);
            DBGT_PDEBUG("ASYNC: created post processor");
            DBGT_PDEBUG("ASYNC: raw frame width: %d frame height: %d color format: %s",
                        (int) dec->in.def.format.video.nFrameWidth,
                        (int) dec->in.def.format.video.nFrameHeight,
                        HantroOmx_str_omx_color(dec->in.def.format.video.
                                                eColorFormat));
            break;
        default:
            DBGT_CRITICAL("ASYNC: unsupported input color format %x", dec->in.def.format.video.eColorFormat);
            err = OMX_ErrorUnsupportedSetting;
        }
        break;
#endif
    default:
        DBGT_CRITICAL("ASYNC: unsupported input format. No such video codec");
        err = OMX_ErrorUnsupportedSetting;
        break;
    }

#endif
#ifdef OMX_DECODER_IMAGE_DOMAIN
    switch ((OMX_U32)dec->in.def.format.image.eCompressionFormat)
    {
#ifdef ENABLE_CODEC_JPEG
    case OMX_IMAGE_CodingJPEG:
        strcpy((char *) dec->role, "image_decoder.jpeg");
        dec->codec = HantroHwDecOmx_decoder_create_jpeg(OMX_FALSE);
        DBGT_PDEBUG("ASYNC: created jpeg codec");
        break;
#endif
#ifdef ENABLE_CODEC_WEBP
    case OMX_IMAGE_CodingWEBP:
        strcpy((char *) dec->role, "image_decoder.webp");
        dec->codec = HantroHwDecOmx_decoder_create_webp(dec->alloc.pdwl);
        DBGT_PDEBUG("ASYNC: created webp codec");
        break;
#endif
#ifdef ENABLE_PP
    case OMX_IMAGE_CodingUnused:
        switch (dec->in.def.format.image.eColorFormat)
        {
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420PackedPlanar:
        case OMX_COLOR_FormatYUV420SemiPlanar:
        case OMX_COLOR_FormatYUV420PackedSemiPlanar:
        case OMX_COLOR_FormatYCbYCr:   // 422 interleaved
#if defined (IS_8190) || defined (IS_G1_DECODER)
        case OMX_COLOR_FormatYCrYCb:
        case OMX_COLOR_FormatCbYCrY:
        case OMX_COLOR_FormatCrYCbY:
#endif
            dec->codec =
                HantroHwDecOmx_decoder_create_pp(dec->in.def.format.image.
                                                 nFrameWidth,
                                                 dec->in.def.format.image.
                                                 nFrameHeight,
                                                 dec->in.def.format.image.
                                                 eColorFormat);
            DBGT_PDEBUG("ASYNC: created post processor");
            DBGT_PDEBUG("ASYNC: raw frame width: %d frame height: %d color format: %s",
                        (int) dec->in.def.format.image.nFrameWidth,
                        (int) dec->in.def.format.image.nFrameHeight,
                        HantroOmx_str_omx_color(dec->in.def.format.image.
                                                eColorFormat));
            break;
        default:
            DBGT_CRITICAL("ASYNC: unsupported input color format");
            err = OMX_ErrorUnsupportedSetting;
        }
        break;
#endif
    default:
        DBGT_CRITICAL("ASYNC: unsupported input format. No such image codec, %x",
                        dec->in.def.format.image.eCompressionFormat);
        err = OMX_ErrorUnsupportedSetting;
        break;
    }
#endif // OMX_DECODER_IMAGE_DOMAIN
    if (!dec->codec)
        goto FAIL;

    DBGT_ASSERT(dec->codec->destroy);
    DBGT_ASSERT(dec->codec->decode);
    DBGT_ASSERT(dec->codec->getinfo);
    DBGT_ASSERT(dec->codec->getframe);
    DBGT_ASSERT(dec->codec->scanframe);
    DBGT_ASSERT(dec->codec->setppargs);

    OMX_U32 input_buffer_size = dec->in.def.nBufferSize;

    OMX_U32 output_buffer_size = dec->out.def.nBufferSize;

#if 0
    OMX_U32 ts_buffer_size = MAX_TICK_COUNTS;
#endif
    OMX_U32 propagate_buffer_size = MAX_TICK_COUNTS;

    OMX_U32 mask_buffer_size =
        dec->inpp.def.format.image.nFrameWidth *
        dec->inpp.def.format.image.nFrameHeight * 4;

    // create the temporary frame buffers. These are needed when
    // we are either using a buffer that was allocated by the client
    // or when the data in the buffers contains partial decoding units
    err =
        OSAL_AllocatorAllocMem(&dec->alloc, &input_buffer_size,
                               &dec->frame_in.bus_data,
                               &dec->frame_in.bus_address);
    if (err != OMX_ErrorNone)
    {
      DBGT_CRITICAL("OSAL_AllocatorAllocMem (frame in) failed (size=%d)", (int)input_buffer_size);
        goto FAIL;
    }
    DBGT_PDEBUG("API: allocated frame in buffer size:%u @physical addr:0x%08lx @logical addr:%p",
                (unsigned) input_buffer_size, dec->frame_in.bus_address,
                dec->frame_in.bus_data);

    dec->frame_in.capacity = input_buffer_size;

    err =
        OSAL_AllocatorAllocMem(&dec->alloc, &output_buffer_size,
                               &dec->frame_out.bus_data,
                               &dec->frame_out.bus_address);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("OSAL_AllocatorAllocMem (frame out) failed (size=%d)", (int)output_buffer_size);
        goto FAIL;
    }

    DBGT_PDEBUG("API: allocated frame out size:%u @physical addr:0x%08lx @logical addr:%p",
                (unsigned) output_buffer_size, dec->frame_out.bus_address,
                dec->frame_out.bus_data);

    dec->frame_out.capacity = output_buffer_size;

    if (dec->inpp.def.bEnabled)
    {
        if (mask_buffer_size > 0)
        {
            err =
                OSAL_AllocatorAllocMem(&dec->alloc, &mask_buffer_size,
                                       &dec->mask.bus_data, &dec->mask.bus_address);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("OSAL_AllocatorAllocMem(pp mask) failed (size=%d)", (int)mask_buffer_size);
                goto FAIL;
            }
             DBGT_PDEBUG("API: allocated pp mask buffer size:%u @physical addr:0x%08lx @logical addr:%p",
                         (unsigned) mask_buffer_size, dec->mask.bus_address,
                         dec->mask.bus_data);
        }
        /* else
        {
            //mask_buffer_size = 38016;
            err =
                OSAL_AllocatorAllocMem(&dec->alloc, &mask_buffer_size,
                                       &dec->mask.bus_data, &dec->mask.bus_address);
            if (err != OMX_ErrorNone)
                goto FAIL;
        } */
        dec->mask.capacity = mask_buffer_size;
    }

#if 0
    // init pts buffer queue.
    dec->ts_buf.ts_data = malloc(ts_buffer_size * sizeof(OMX_TICKS));
    dec->ts_buf.capacity = ts_buffer_size;
    dec->ts_buf.count = 0;
#endif
    // init propagate buffer queue.
    dec->propagate_buf.propagate_data = malloc(propagate_buffer_size * sizeof(PROPAGATE_INPUT_DATA));
    dec->propagate_buf.capacity = propagate_buffer_size;
    dec->propagate_buf.count = 0;

    memset(&dec->pp_args, 0, sizeof(PP_ARGS));
#ifdef OMX_DECODER_VIDEO_DOMAIN
    dec->pp_args.format = dec->out.def.format.video.eColorFormat;
#ifndef DISABLE_OUTPUT_SCALING
    dec->pp_args.scale.width = dec->out.def.format.video.nFrameWidth;
    dec->pp_args.scale.height = dec->out.def.format.video.nFrameHeight;
#endif
#endif

#ifdef OMX_DECODER_IMAGE_DOMAIN
    dec->pp_args.format = dec->out.def.format.image.eColorFormat;
#ifndef DISABLE_OUTPUT_SCALING
    dec->pp_args.scale.width = dec->out.def.format.image.nFrameWidth;
    dec->pp_args.scale.height = dec->out.def.format.image.nFrameHeight;
#endif
#endif
    dec->pp_args.crop.left = dec->conf_rect.nLeft;
    dec->pp_args.crop.top = dec->conf_rect.nTop;
    dec->pp_args.crop.width = dec->conf_rect.nWidth;
    dec->pp_args.crop.height = dec->conf_rect.nHeight;
    dec->pp_args.rotation = dec->conf_rotation.nRotation;

    if (dec->conf_mirror.eMirror != OMX_MirrorNone)
    {
        switch (dec->conf_mirror.eMirror)
        {
        case OMX_MirrorHorizontal:
            dec->pp_args.rotation = ROTATE_FLIP_HORIZONTAL;
            break;
        case OMX_MirrorVertical:
            dec->pp_args.rotation = ROTATE_FLIP_VERTICAL;
            break;
        default:
            DBGT_CRITICAL("ASYNC: unsupported mirror value");
            break;
        }
    }

    dec->pp_args.contrast = dec->conf_contrast.nContrast;
    dec->pp_args.brightness = dec->conf_brightness.nBrightness;
    dec->pp_args.saturation = dec->conf_saturation.nSaturation;
    dec->pp_args.alpha = dec->conf_blend.nAlpha;
    dec->pp_args.dither = dec->conf_dither.eDither;

    dec->pp_args.mask1.originX = dec->conf_mask_offset.nX;
    dec->pp_args.mask1.originY = dec->conf_mask_offset.nY;
    dec->pp_args.mask1.width = dec->inpp.def.format.image.nFrameWidth;
    dec->pp_args.mask1.height = dec->inpp.def.format.image.nFrameHeight;
    dec->pp_args.blend_mask_base = dec->mask.bus_address;

    dec->pp_args.mask2.originX = dec->conf_mask.nLeft;
    dec->pp_args.mask2.originY = dec->conf_mask.nTop;
    dec->pp_args.mask2.width = dec->conf_mask.nWidth;
    dec->pp_args.mask2.height = dec->conf_mask.nHeight;

    DBGT_PDEBUG("Set Post-Processor arguments");
    if (dec->codec->setppargs(dec->codec, &dec->pp_args) != CODEC_OK)
    {
        err = OMX_ErrorBadParameter;
        DBGT_CRITICAL("ASYNC: failed to set Post-Processor arguments");
        goto FAIL;
    }
    dec->codecstate = CODEC_OK;
    DBGT_EPILOG("");
    return OMX_ErrorNone;

  FAIL:
    DBGT_CRITICAL("ASYNC: error: %s", HantroOmx_str_omx_err(err));
    if (dec->frame_out.bus_address)
    {
        FRAME_BUFF_FREE(&dec->alloc, &dec->frame_out);
        memset(&dec->frame_out, 0, sizeof(FRAME_BUFFER));
    }
    if (dec->frame_in.bus_data)
    {
        FRAME_BUFF_FREE(&dec->alloc, &dec->frame_in);
        memset(&dec->frame_in, 0, sizeof(FRAME_BUFFER));
    }

#if 0
    // reset pts buffer queue
    if (dec->ts_buf.ts_data)
    {
        free(dec->ts_buf.ts_data);
        memset(&dec->ts_buf, 0, sizeof(TIMESTAMP_BUFFER));
    }
#endif

    // reset propagate buffer queue
    if (dec->propagate_buf.propagate_data)
    {
        free(dec->propagate_buf.propagate_data);
        memset(&dec->propagate_buf, 0, sizeof(PROPAGATE_BUFFER));
        memset(&dec->propagateData, 0, sizeof(PROPAGATE_INPUT_DATA));
        memset(dec->prevPicIdList, -1, sizeof(dec->prevPicIdList));
        dec->propagateDataReceived = 0;
    }

    if (dec->mask.bus_data)
    {
        FRAME_BUFF_FREE(&dec->alloc, &dec->mask);
        memset(&dec->mask, 0, sizeof(FRAME_BUFFER));
    }
    if (dec->codec)
    {
        dec->codec->destroy(dec->codec);
        dec->codec = NULL;
    }
    DBGT_EPILOG("");
    return err;
}

static OMX_ERRORTYPE async_decoder_return_buffers(OMX_DECODER * dec, PORT * p)
{
    CALLSTACK;
    DBGT_PROLOG("");

    if (HantroOmx_port_is_supplier(p))
    {
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    DBGT_PDEBUG("ASYNC: returning allocated buffers on port %d to %p %d",
                (int) p->def.nPortIndex, p->tunnelcomp, (int) p->tunnelport);

   // Make sure no frames to be dispalyed in decoder
#ifdef IS_G2_DECODER
    while (p->def.eDir == OMX_DirOutput)
    {
        OSAL_MutexLock(dec->threadmutex);
        if (!dec->shared_data.hasFrame)
        {
            dec->shared_data.hasFrame = OMX_TRUE;
            OSAL_MutexUnlock(dec->threadmutex);
            break;
        }
        OSAL_MutexUnlock(dec->threadmutex);
        OSAL_ThreadSleep(RETRY_INTERVAL);
    }
#else
    if (dec->useExternalAlloc == OMX_TRUE)
    {
        while (p->def.eDir == OMX_DirOutput)
        {
            OSAL_MutexLock(dec->threadmutex);
            if (!dec->shared_data.hasFrame)
            {
                dec->shared_data.hasFrame = OMX_TRUE;
                OSAL_MutexUnlock(dec->threadmutex);
                break;
            }
            OSAL_MutexUnlock(dec->threadmutex);
            OSAL_ThreadSleep(RETRY_INTERVAL);
        }
    }
#endif

#ifdef IS_G1_DECODER
    if ((dec->useExternalAlloc == OMX_FALSE) && (p == &dec->out) && dec->buffer)
    {
        CODEC_STATE state = CODEC_HAS_FRAME;
        FRAME frm;
        memset(&frm, 0, sizeof(FRAME));

        if ((dec->buffer->flags & BUFFER_FLAG_MY_BUFFER) || (dec->buffer->flags & BUFFER_FLAG_ANDROID_NATIVE_BUFFER))
        {
            DBGT_PDEBUG("ASYNC: Flushing decoder output frames");

            while(state == CODEC_HAS_FRAME)
            {
                DBGT_ASSERT(dec->buffer->bus_data);
                DBGT_ASSERT(dec->buffer->bus_address);

                frm.fb_bus_data = dec->buffer->bus_data;
                frm.fb_bus_address = dec->buffer->bus_address;
                frm.fb_size = dec->buffer->header->nAllocLen;

                state = dec->codec->getframe(dec->codec, &frm, 1);
                DBGT_PDEBUG("ASYNC getframe stat %d", state);
                if (state == CODEC_OK)
                    break;
                if (state < 0)
                {
                    DBGT_CRITICAL("ASYNC: getframe error:%d", state);
                    break;
                }
            }
        }

        // returned the held output buffer (if any)
        if (HantroOmx_port_is_tunneled(p))
        {
            ((OMX_COMPONENTTYPE *) p->tunnelcomp)->EmptyThisBuffer(p->
                                                                   tunnelcomp,
                                                                   dec->buffer->
                                                                   header);
            dec->buffer = NULL;
        }
        else
        {
            dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                          dec->buffer->header);
            dec->buffer = NULL;
        }
    }
#endif
    // return the queued buffers to the suppliers
    // Danger. The port's locked and there are callbacks into the unknown.

    OMX_ERRORTYPE err = HantroOmx_port_lock_buffers(p);

    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("HantroOmx_port_lock_buffers failed (err=%x)", err);
        DBGT_EPILOG("");
        return err;
    }

    OMX_U32 i;

    OMX_U32 count = HantroOmx_port_buffer_queue_count(p);

    for(i = 0; i < count; ++i)
    {
        BUFFER *buff = 0;
        FRAME frm;
        memset(&frm, 0, sizeof(frm));

        HantroOmx_port_get_buffer_at(p, &buff, i);
        DBGT_ASSERT(buff);

        if (p->def.eDir == OMX_DirOutput)
        {
            // For vp9 dynamic port reconfig, use pOutputPortPrivate to notify the client
            // which buffer need to be reallocated.
            if (dec->ReallocBufferAddress)
            {
                if (dec->ReallocBufferAddress == buff->bus_address)
                    frm.outBufPrivate.realloc = 1;
                else
                    frm.outBufPrivate.realloc = 0;

                buff->header->pOutputPortPrivate = &frm.outBufPrivate;
            }
            else
                buff->header->pOutputPortPrivate = NULL;

            // Clear buffer header parameters
            buff->header->nFilledLen = 0;
            buff->header->nOffset = 0;
            buff->header->nTickCount = 0;
#ifdef OMX_SKIP64BIT
            buff->header->nTimeStamp.nLowPart = buff->header->nTimeStamp.nHighPart = 0;
#else
            buff->header->nTimeStamp = 0;
#endif
            buff->header->hMarkTargetComponent = NULL;
            buff->header->pMarkData = NULL;
            buff->header->nFlags &= ~OMX_BUFFERFLAG_EOS;
        }

        if (HantroOmx_port_is_tunneled(p))
        {
            // tunneled but someone else is supplying. i.e. we have allocated the header.
            DBGT_ASSERT(buff->header == &buff->headerdata);
            // return the buffer to the supplier
            if (p->def.eDir == OMX_DirInput)
                ((OMX_COMPONENTTYPE *) p->tunnelcomp)->FillThisBuffer(p->
                                                                      tunnelcomp,
                                                                      buff->
                                                                      header);
            if (p->def.eDir == OMX_DirOutput)
                ((OMX_COMPONENTTYPE *) p->tunnelcomp)->EmptyThisBuffer(p->
                                                                       tunnelcomp,
                                                                       buff->
                                                                       header);
        }
        else
        {
            // return the buffer to the client
            if (p->def.eDir == OMX_DirInput)
                dec->callbacks.EmptyBufferDone(dec->self, dec->appdata,
                                               buff->header);
            if (p->def.eDir == OMX_DirOutput)
                dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                              buff->header);
        }
    }

    if (dec->prevBuffer)
    {
        BUFFER *tmpBuffer = dec->prevBuffer;
        if (tmpBuffer->header->hMarkTargetComponent == dec->self)
        {
            dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventMark, 0,
                                        0, tmpBuffer->header->pMarkData);
            tmpBuffer->header->hMarkTargetComponent = NULL;
            tmpBuffer->header->pMarkData = NULL;
        }
        if (HantroOmx_port_is_tunneled(&dec->in))
        {
            ((OMX_COMPONENTTYPE *) dec->in.tunnelcomp)->FillThisBuffer(dec->in.
                                                                       tunnelcomp,
                                                                       tmpBuffer->
                                                                       header);
        }
        else
        {
            DBGT_PDEBUG("!!! EmptyBufferDone callback, buffer: %p", tmpBuffer);
            dec->callbacks.EmptyBufferDone(dec->self, dec->appdata, tmpBuffer->header);
        }
        dec->prevBuffer = NULL;
    }


    // if input buffers are returned, clear also timestamps
    if (p->def.eDir == OMX_DirInput)
    {
        //flush_timestamps(dec);
        flush_propagate_data(dec);
        memset(dec->prevPicIdList, -1, sizeof(dec->prevPicIdList));
        memset(&dec->propagateData, 0, sizeof(PROPAGATE_INPUT_DATA));
        dec->propagateDataReceived = 0;
    }

    HantroOmx_port_buffer_queue_clear(p);
    HantroOmx_port_unlock_buffers(p);
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE transition_to_idle_from_paused(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(dec->state == OMX_StatePause);
    DBGT_ASSERT(dec->statetrans == OMX_StateIdle);

    // return queued buffers
    async_decoder_return_buffers(dec, &dec->in);
    async_decoder_return_buffers(dec, &dec->out);
    async_decoder_return_buffers(dec, &dec->inpp);

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE wait_supplied_buffers(OMX_DECODER * dec, PORT * port)
{
    CALLSTACK;
    DBGT_PROLOG("");

    if (!HantroOmx_port_is_supplier(port))
    {
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    // buffer supplier component cannot transition
    // to idle untill all the supplied buffers have been returned
    // by the buffer user via a call to our FillThisBuffer
    OMX_U32 loop = 1;

    while(loop)
    {
        HantroOmx_port_lock_buffers(port);
        OMX_U32 queued = HantroOmx_port_buffer_queue_count(port);

        if (port == &dec->out && dec->buffer)
            ++queued;   // include the held buffer in the queue size.

        //if (port_has_all_supplied_buffers(port))
        if (HantroOmx_port_buffer_count(port) == queued)
            loop = 0;

        DBGT_PDEBUG("ASYNC: port %d has %d buffers out of %d supplied",
                    (int) port->def.nPortIndex, (int) queued,
                    (int) HantroOmx_port_buffer_count(port));
        HantroOmx_port_unlock_buffers(port);
        if (loop)
            OSAL_ThreadSleep(RETRY_INTERVAL);
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE transition_to_idle_from_executing(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(dec->state == OMX_StateExecuting);
    DBGT_ASSERT(dec->statetrans == OMX_StateIdle);

    // return queued buffers
    async_decoder_return_buffers(dec, &dec->in);
    async_decoder_return_buffers(dec, &dec->out);
    async_decoder_return_buffers(dec, &dec->inpp);

    // wait for the buffers supplied by this component
    // to be returned by the buffer users.
    wait_supplied_buffers(dec, &dec->in);
    wait_supplied_buffers(dec, &dec->out);
    wait_supplied_buffers(dec, &dec->inpp);

    // Clear dynamic parameters
    dec->frame_in.size = 0;
    memset(dec->outputBufList, 0, sizeof(dec->outputBufList));

    dec->releaseBuffers = OMX_FALSE;
    dec->portReconfigPending = OMX_FALSE;
    dec->run = OMX_TRUE;
    dec->codecstate = CODEC_OK;
    dec->buffer = NULL;

    // Clear share data
    dec->shared_data.EOS = OMX_FALSE;
    dec->shared_data.eos_bus_address = 0;

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE transition_to_loaded_from_idle(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    // the state transition cannot complete untill all
    // enabled ports have their buffers freed

    DBGT_ASSERT(dec->state == OMX_StateIdle);
    DBGT_ASSERT(dec->statetrans == OMX_StateLoaded);

    if (HantroOmx_port_is_supplier(&dec->in))
        unsupply_tunneled_port(dec, &dec->in);
    if (HantroOmx_port_is_supplier(&dec->out))
        unsupply_tunneled_port(dec, &dec->out);
    if (HantroOmx_port_is_supplier(&dec->inpp))
        unsupply_tunneled_port(dec, &dec->inpp);

    // should be okay without locking
    DBGT_ASSERT(HantroOmx_port_buffer_queue_count(&dec->in) == 0);
    DBGT_ASSERT(HantroOmx_port_buffer_queue_count(&dec->out) == 0);
    DBGT_ASSERT(HantroOmx_port_buffer_queue_count(&dec->inpp) == 0);

    DBGT_PDEBUG("ASYNC: waiting for ports to be non-populated");

    while(HantroOmx_port_has_buffers(&dec->in) ||
          HantroOmx_port_has_buffers(&dec->out) ||
          HantroOmx_port_has_buffers(&dec->inpp))
    {
        OSAL_ThreadSleep(RETRY_INTERVAL);
    }

    DBGT_PDEBUG("ASYNC: destroying codec");
    DBGT_ASSERT(dec->codec);
    dec->codec->destroy(dec->codec);
    DBGT_PDEBUG("ASYNC: freeing internal frame buffers");
    if (dec->frame_in.bus_address)
        FRAME_BUFF_FREE(&dec->alloc, &dec->frame_in);

#if 0
    if (dec->ts_buf.ts_data)
        free(dec->ts_buf.ts_data);
#endif

    if (dec->propagate_buf.propagate_data)
        free(dec->propagate_buf.propagate_data);

    if (dec->frame_out.bus_address)
        FRAME_BUFF_FREE(&dec->alloc, &dec->frame_out);

    if (dec->mask.bus_address)
        FRAME_BUFF_FREE(&dec->alloc, &dec->mask);

    // Clear dynamic parameters
    dec->codec = NULL;
    dec->buffer = NULL;
    dec->codecstate = CODEC_OK;
    dec->run = OMX_TRUE;
    dec->checkExtraBuffers = OMX_FALSE;
    dec->portReconfigPending = OMX_FALSE;
    dec->disablingOutPort = OMX_FALSE;
    dec->flushingOutPort = OMX_FALSE;
    dec->releaseBuffers = OMX_FALSE;

    // Clear share data
    dec->shared_data.EOS = OMX_FALSE;
    dec->shared_data.inbuff = NULL;
    dec->shared_data.output_thread_run = OMX_FALSE;
    dec->shared_data.output_thread_ = 0;
    dec->shared_data.hasFrame = OMX_FALSE;
    dec->shared_data.eos_bus_address = 0;

    // Clear propagate data
    memset(&dec->propagateData, 0, sizeof(PROPAGATE_INPUT_DATA));
    memset(dec->prevPicIdList, -1, sizeof(dec->prevPicIdList));
    dec->propagateDataReceived = 0;
    dec->mark_count = 0;
    dec->mark_read_pos = 0;
    dec->mark_write_pos = 0;

    memset(&dec->frame_in, 0, sizeof(FRAME_BUFFER));
#if 0
    memset(&dec->ts_buf, 0, sizeof(TIMESTAMP_BUFFER));
#endif
    memset(&dec->propagate_buf, 0, sizeof(PROPAGATE_BUFFER));
    memset(&dec->frame_out, 0, sizeof(FRAME_BUFFER));
    memset(&dec->mask, 0, sizeof(FRAME_BUFFER));
    DBGT_PDEBUG("ASYNC: freed internal frame buffers");

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE transition_to_loaded_from_wait_for_resources(OMX_DECODER *
                                                               dec)
{
    CALLSTACK;
    DBGT_PROLOG("");
    DBGT_ASSERT(dec->state == OMX_StateWaitForResources);
    DBGT_ASSERT(dec->statetrans == OMX_StateLoaded);

    // note: could unregister with a custom resource manager here
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE transition_to_executing_from_paused(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");
    DBGT_ASSERT(dec->state == OMX_StatePause);
    DBGT_ASSERT(dec->statetrans == OMX_StateExecuting);

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE transition_to_executing_from_idle(OMX_DECODER * dec)
{
    CALLSTACK;
    // note: could check for suspension here

    DBGT_PROLOG("");
    DBGT_ASSERT(dec->state == OMX_StateIdle);
    DBGT_ASSERT(dec->statetrans == OMX_StateExecuting);

    dec->releaseBuffers = OMX_FALSE;
#ifdef IS_G1_DECODER
    if ((dec->useExternalAlloc == OMX_TRUE) && !dec->shared_data.output_thread_)
    {
        DBGT_PDEBUG("ASYNC: create G1 output thread");
        dec->shared_data.output_thread_run = OMX_TRUE;
        pthread_create(&dec->shared_data.output_thread_, NULL, output_thread,
                &dec->shared_data);
    }
    dec->nGuardSize = dec->g1Conf.nGuardSize;
#endif

#ifdef IS_G2_DECODER
    if (!dec->shared_data.output_thread_)
    {
        DBGT_PDEBUG("ASYNC: create G2 output thread");
        dec->shared_data.output_thread_run = OMX_TRUE;
        pthread_create(&dec->shared_data.output_thread_, NULL, output_thread,
               &dec->shared_data);
    }
    dec->nGuardSize = dec->g2Conf.nGuardSize;
#endif

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE startup_tunnel(OMX_DECODER * dec, PORT * port)
{
    CALLSTACK;
    DBGT_PROLOG("");
    OMX_U32 buffers = 0;

    OMX_U32 i = 0;

    OMX_ERRORTYPE err = OMX_ErrorNone;

    if (HantroOmx_port_is_enabled(port) && HantroOmx_port_is_supplier(port))
    {
        if (port == &dec->out)
        {
            // queue the buffers into own queue since the
            // output port is the supplier the tunneling component
            // cannot fill the buffer queue.
            buffers = HantroOmx_port_buffer_count(port);
            for(i = 0; i < buffers; ++i)
            {
                BUFFER *buff = NULL;

                HantroOmx_port_get_allocated_buffer_at(port, &buff, i);
                HantroOmx_port_push_buffer(port, buff);
            }
        }
        else
        {
            buffers = HantroOmx_port_buffer_count(port);
            for(; i < buffers; ++i)
            {
                BUFFER *buff = NULL;

                HantroOmx_port_get_allocated_buffer_at(port, &buff, i);
                DBGT_ASSERT(buff);
                DBGT_ASSERT(buff->header != &buff->headerdata);
                err =
                    ((OMX_COMPONENTTYPE *) port->tunnelcomp)->
                    FillThisBuffer(port->tunnelcomp, buff->header);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("ASYNC: tunneling component failed to fill buffer: %s",
                                HantroOmx_str_omx_err(err));
                    DBGT_EPILOG("");
                    return err;
                }
            }
        }
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE transition_to_wait_for_resources_from_loaded(OMX_DECODER *
                                                               dec)
{
    CALLSTACK;
    DBGT_PROLOG("");
    DBGT_ASSERT(dec->state == OMX_StateLoaded);
    DBGT_ASSERT(dec->statetrans == OMX_StateWaitForResources);
    // note: could register with a custom resource manager for availability of resources.
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE async_decoder_set_state(OMX_COMMANDTYPE Cmd, OMX_U32 nParam1,
                                          OMX_PTR pCmdData, OMX_PTR arg)
{
    UNUSED_PARAMETER(Cmd);
    UNUSED_PARAMETER(pCmdData);
    CALLSTACK;
    DBGT_PROLOG("");

    OMX_ERRORTYPE err = OMX_ErrorIncorrectStateTransition;
    OMX_DECODER *dec = (OMX_DECODER *) arg;
    OMX_STATETYPE nextstate = (OMX_STATETYPE) nParam1;

    if (nextstate == dec->state)
    {
        DBGT_PDEBUG("ASYNC: same state:%s",
                    HantroOmx_str_omx_state(dec->state));
        dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError,
                                    OMX_ErrorSameState, 0, NULL);
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    // state transition state switch. ugliest thing EVER.

    switch (nextstate)
    {
    case OMX_StateIdle:
        switch (dec->state)
        {
        case OMX_StateLoaded:
            err = transition_to_idle_from_loaded(dec);
            break;
        case OMX_StatePause:
            err = transition_to_idle_from_paused(dec);
            break;
        case OMX_StateExecuting:
#ifdef IS_G2_DECODER
            if (dec->shared_data.output_thread_)
            {
                DBGT_PDEBUG("Join output thread");
                pthread_join(dec->shared_data.output_thread_, NULL);
                dec->shared_data.output_thread_run = OMX_FALSE;
                dec->shared_data.output_thread_ = 0;
            }
#endif
#ifdef IS_G1_DECODER
            if (dec->useExternalAlloc == OMX_TRUE)
            {
                if (dec->shared_data.output_thread_)
                {
                    DBGT_PDEBUG("Join output thread");
                    pthread_join(dec->shared_data.output_thread_, NULL);
                    dec->shared_data.output_thread_run = OMX_FALSE;
                    dec->shared_data.output_thread_ = 0;
                }
            }
#endif
            dec->codec->abortafter(dec->codec);

            err = transition_to_idle_from_executing(dec);
            break;
        default:
            break;
        }
        break;
    case OMX_StateLoaded:
        if (dec->state == OMX_StateIdle)
            err = transition_to_loaded_from_idle(dec);
        if (dec->state == OMX_StateWaitForResources)
            err = transition_to_loaded_from_wait_for_resources(dec);
        break;
    case OMX_StateExecuting:
        if (dec->state == OMX_StatePause)    /// OMX_StatePause to OMX_StateExecuting
            err = transition_to_executing_from_paused(dec);
        if (dec->state == OMX_StateIdle)
            err = transition_to_executing_from_idle(dec);   /// OMX_StateIdle to OMX_StateExecuting
        break;
    case OMX_StatePause:
        {
            if (dec->state == OMX_StateIdle) /// OMX_StateIdle to OMX_StatePause
                err = OMX_ErrorNone;
            if (dec->state == OMX_StateExecuting)    /// OMX_StateExecuting to OMX_StatePause
                err = OMX_ErrorNone;
        }
        break;
    case OMX_StateWaitForResources:
        {
            if (dec->state == OMX_StateLoaded)
                err = transition_to_wait_for_resources_from_loaded(dec);
        }
        break;
    case OMX_StateInvalid:
        DBGT_CRITICAL("Invalid state");
        err = OMX_ErrorInvalidState;
        break;
    default:
        DBGT_ASSERT(!"weird state");
        break;
    }

    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("ASYNC: set state error:%s",
                    HantroOmx_str_omx_err(err));
        if (err != OMX_ErrorIncorrectStateTransition)
        {
            dec->state = OMX_StateInvalid;
            dec->run = OMX_FALSE;
        }
        else
        {
            // clear state transition flag
            dec->statetrans = dec->state;
        }
        dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError,
                                    err, 0, NULL);
    }
    else
    {
        // transition complete
        DBGT_PDEBUG("ASYNC: set state complete:%s",
                    HantroOmx_str_omx_state(nextstate));

        OSAL_MutexLock(dec->statemutex);
        dec->statetrans = nextstate;
        dec->state = nextstate;
        OSAL_MutexUnlock(dec->statemutex);

        dec->callbacks.EventHandler(dec->self, dec->appdata,
                                    OMX_EventCmdComplete, OMX_CommandStateSet,
                                    dec->state, NULL);

        if (dec->state == OMX_StateExecuting)
        {
            err = startup_tunnel(dec, &dec->in);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("ASYNC: failed to startup buffer processing: %s",
                            HantroOmx_str_omx_err(err));
                dec->state = OMX_StateInvalid;
                dec->run = OMX_FALSE;
            }
            err = startup_tunnel(dec, &dec->inpp);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("ASYNC: failed to startup buffer processing: %s",
                            HantroOmx_str_omx_err(err));
                dec->state = OMX_StateInvalid;
                dec->run = OMX_FALSE;
            }
            err = startup_tunnel(dec, &dec->out);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("ASYNC: failed to startup buffer processing: %s",
                            HantroOmx_str_omx_err(err));
                dec->state = OMX_StateInvalid;
                dec->run = OMX_FALSE;
            }
        }
    }
    DBGT_EPILOG("");
    return err;
}

static void async_decoder_wait_port_buffer_dealloc(OMX_DECODER * dec, PORT * p)
{
    UNUSED_PARAMETER(dec);
    CALLSTACK;
    DBGT_PROLOG("");
    while(HantroOmx_port_has_buffers(p))
    {
        OSAL_ThreadSleep(RETRY_INTERVAL);
    }
    DBGT_EPILOG("");
}

static
    OMX_ERRORTYPE async_decoder_disable_port(OMX_COMMANDTYPE Cmd,
                                             OMX_U32 nParam1, OMX_PTR pCmdData,
                                             OMX_PTR arg)
{
    UNUSED_PARAMETER(Cmd);
#ifndef ENABLE_DBGT_TRACE
    UNUSED_PARAMETER(pCmdData);
#endif
    CALLSTACK;
    DBGT_PROLOG("");

    OMX_DECODER *dec = (OMX_DECODER *) arg;

    OMX_U32 portIndex = nParam1;

    // return the queue'ed buffers to the suppliers
    // and wait untill all the buffers have been free'ed.
    // The component must generate port disable event for each port disabled
    // 3.2.2.5

    DBGT_PDEBUG("ASYNC: nParam1:%u pCmdData:%p", (unsigned) nParam1,
                pCmdData);

    if (portIndex == OMX_ALL)
    {
        DBGT_PDEBUG("ASYNC: disabling all ports");
        async_decoder_return_buffers(dec, &dec->in);
        async_decoder_return_buffers(dec, &dec->out);
        async_decoder_return_buffers(dec, &dec->inpp);

        wait_supplied_buffers(dec, &dec->in);
        wait_supplied_buffers(dec, &dec->out);
        wait_supplied_buffers(dec, &dec->inpp);

        if (HantroOmx_port_is_supplier(&dec->in))
            unsupply_tunneled_port(dec, &dec->in);
        if (HantroOmx_port_is_supplier(&dec->out))
            unsupply_tunneled_port(dec, &dec->out);
        if (HantroOmx_port_is_supplier(&dec->inpp))
            unsupply_tunneled_port(dec, &dec->inpp);

        async_decoder_wait_port_buffer_dealloc(dec, &dec->in);
        async_decoder_wait_port_buffer_dealloc(dec, &dec->out);
        async_decoder_wait_port_buffer_dealloc(dec, &dec->inpp);

        // generate port disable events
        OMX_U32 i = 0;

        for(i = 0; i < 3; ++i)
        {
            dec->callbacks.EventHandler(dec->self, dec->appdata,
                                        OMX_EventCmdComplete,
                                        OMX_CommandPortDisable, i, NULL);
        }
    }
    else
    {
        PORT *ports[] = { &dec->in, &dec->out, &dec->inpp };
        OMX_U32 i = 0;

        for(i = 0; i < sizeof(ports) / sizeof(PORT *); ++i)
        {
            if (portIndex == i)
            {
                DBGT_PDEBUG("ASYNC: disabling port: %d", (int)i);
                async_decoder_return_buffers(dec, ports[i]);
                wait_supplied_buffers(dec, ports[i]);
                if (HantroOmx_port_is_supplier(ports[i]))
                    unsupply_tunneled_port(dec, ports[i]);

                async_decoder_wait_port_buffer_dealloc(dec, ports[i]);
                break;
            }
        }
        dec->callbacks.EventHandler(dec->self, dec->appdata,
                                    OMX_EventCmdComplete,
                                    OMX_CommandPortDisable, portIndex, NULL);
    }

    if (dec->disablingOutPort &&
    (portIndex == PORT_INDEX_OUTPUT || portIndex == OMX_ALL))
    {
        dec->disablingOutPort = OMX_FALSE;
        memcpy(dec->outputBufListPrev, dec->outputBufList, sizeof(dec->outputBufList));
        memset(dec->outputBufList, 0, sizeof(dec->outputBufList));
        dec->checkExtraBuffers = OMX_FALSE;
    }

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE async_decoder_enable_port(OMX_COMMANDTYPE Cmd,
                                            OMX_U32 nParam1, OMX_PTR pCmdData,
                                            OMX_PTR arg)
{
    UNUSED_PARAMETER(Cmd);
#ifndef ENABLE_DBGT_TRACE
    UNUSED_PARAMETER(pCmdData);
#endif
    CALLSTACK;
    DBGT_PROLOG("");

    OMX_DECODER *dec = (OMX_DECODER *) arg;
    OMX_U32 portIndex = nParam1;
    OMX_ERRORTYPE err = OMX_ErrorNone;

    DBGT_PDEBUG("ASYNC: port index:%u pCmdData:%p", (unsigned) nParam1,
                pCmdData);

    // brutally wait until all buffers for the enabled
    // port have been allocated

    if (portIndex == PORT_INDEX_INPUT || portIndex == OMX_ALL)
    {
        if (dec->state != OMX_StateLoaded)
        {
            if (HantroOmx_port_is_supplier(&dec->in))
            {
                err = supply_tunneled_port(dec, &dec->in);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("supply_tunneled_port(in) (err=%x)", err);
                    DBGT_EPILOG("");
                    goto FAIL;
                }
            }
            while(!HantroOmx_port_is_ready(&dec->in))
            {
                OSAL_BOOL timeout;
                OSAL_EventWait(dec->in.bufferRdy, TIMEOUT, &timeout);
                if (timeout == OMX_TRUE)
                {
                    err = OMX_ErrorTimeout;
                    goto FAIL;
                }
            }
        }
        if (dec->state == OMX_StateExecuting)
            startup_tunnel(dec, &dec->in);

        dec->callbacks.EventHandler(dec->self, dec->appdata,
                                    OMX_EventCmdComplete, OMX_CommandPortEnable,
                                    PORT_INDEX_INPUT, NULL);
        DBGT_PDEBUG("ASYNC: input port enabled");
    }
    if (portIndex == PORT_INDEX_OUTPUT || portIndex == OMX_ALL)
    {
        if (dec->state != OMX_StateLoaded)
        {
            if (HantroOmx_port_is_supplier(&dec->out))
            {
                err = supply_tunneled_port(dec, &dec->out);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("supply_tunneled_port(out) (err=%x)", err);
                    DBGT_EPILOG("");
                    goto FAIL;
                }
            }
            while(!HantroOmx_port_is_ready(&dec->out))
            {
                OSAL_BOOL timeout;
                OSAL_EventWait(dec->out.bufferRdy, TIMEOUT, &timeout);
                if (timeout == OMX_TRUE)
                {
                    err = OMX_ErrorTimeout;
                    goto FAIL;
                }
            }
        }
        if (dec->state == OMX_StateExecuting)
            startup_tunnel(dec, &dec->out);

        dec->callbacks.EventHandler(dec->self, dec->appdata,
                                    OMX_EventCmdComplete, OMX_CommandPortEnable,
                                    PORT_INDEX_OUTPUT, NULL);
        dec->portReconfigPending = OMX_FALSE;
        DBGT_PDEBUG("ASYNC: output port enabled");
    }
    if (portIndex == PORT_INDEX_PP || portIndex == OMX_ALL)
    {
        if (dec->state != OMX_StateLoaded)
        {
            if (HantroOmx_port_is_supplier(&dec->inpp))
            {
                err = supply_tunneled_port(dec, &dec->inpp);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("supply_tunneled_port(inpp) (err=%x)", err);
                    DBGT_EPILOG("");
                    goto FAIL;
                }
            }
            while(!HantroOmx_port_is_ready(&dec->inpp))
            {
                OSAL_BOOL timeout;
                OSAL_EventWait(dec->inpp.bufferRdy, TIMEOUT, &timeout);
                if (timeout == OMX_TRUE)
                {
                    err = OMX_ErrorTimeout;
                    goto FAIL;
                }
            }
        }
        if (dec->state == OMX_StateExecuting)
            startup_tunnel(dec, &dec->inpp);

        dec->callbacks.EventHandler(dec->self, dec->appdata,
                                    OMX_EventCmdComplete, OMX_CommandPortEnable,
                                    PORT_INDEX_PP, NULL);
        DBGT_PDEBUG("ASYNC: post processor input port enabled");
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;

FAIL:
    dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError,
                                err, 0, NULL);
    return err;

}

static
    OMX_ERRORTYPE async_decoder_flush_port(OMX_COMMANDTYPE Cmd, OMX_U32 nParam1,
                                           OMX_PTR pCmdData, OMX_PTR arg)
{
    UNUSED_PARAMETER(Cmd);
#ifndef ENABLE_DBGT_TRACE
    UNUSED_PARAMETER(pCmdData);
#endif
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(arg);
    OMX_DECODER *dec = (OMX_DECODER *) arg;
    OMX_U32 portindex = nParam1;
    OMX_ERRORTYPE err = OMX_ErrorNone;

    DBGT_PDEBUG("ASYNC: nParam1:%u pCmdData:%p", (unsigned) nParam1,
                pCmdData);

    if (portindex == OMX_ALL)
    {
#ifdef IS_G2_DECODER
        if (dec->shared_data.output_thread_)
        {
            DBGT_PDEBUG("Join output thread");
            pthread_join(dec->shared_data.output_thread_, NULL);
            dec->shared_data.output_thread_run = OMX_FALSE;
            dec->shared_data.output_thread_ = 0;
        }
#endif
#ifdef IS_G1_DECODER
        if (dec->useExternalAlloc == OMX_TRUE)
        {
            if (dec->shared_data.output_thread_)
            {
                DBGT_PDEBUG("Join output thread");
                pthread_join(dec->shared_data.output_thread_, NULL);
                dec->shared_data.output_thread_run = OMX_FALSE;
                dec->shared_data.output_thread_ = 0;
            }
        }
#endif
        dec->codec->abortafter(dec->codec);

        DBGT_PDEBUG("ASYNC: flushing all ports");
        err = async_decoder_return_buffers(dec, &dec->in);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("async_decoder_return_buffers(in) failed (err=%x)", err);
            goto FAIL;
        }
        err = async_decoder_return_buffers(dec, &dec->out);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("async_decoder_return_buffers(out) failed (err=%x)", err);
            goto FAIL;
        }
        err = async_decoder_return_buffers(dec, &dec->inpp);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("async_decoder_return_buffers(inpp) failed (err=%x)", err);
            goto FAIL;
        }

        // Clear dynamic parameters
        dec->frame_in.size = 0;
        memset(dec->outputBufList, 0, sizeof(dec->outputBufList));

        dec->releaseBuffers = OMX_FALSE;
        dec->portReconfigPending = OMX_FALSE;
        dec->run = OMX_TRUE;
        dec->codecstate = CODEC_OK;
        dec->buffer = NULL;

        // Clear share data
        dec->shared_data.EOS = OMX_FALSE;
        dec->shared_data.eos_bus_address = 0;

#ifdef IS_G1_DECODER
        if ((dec->useExternalAlloc == OMX_TRUE) && !dec->shared_data.output_thread_)
        {
            DBGT_PDEBUG("ASYNC: create G1 output thread");
            dec->shared_data.output_thread_run = OMX_TRUE;
            pthread_create(&dec->shared_data.output_thread_, NULL, output_thread,
                    &dec->shared_data);
        }
#endif

#ifdef IS_G2_DECODER
        if (!dec->shared_data.output_thread_)
        {
            DBGT_PDEBUG("ASYNC: create G2 output thread");
            dec->shared_data.output_thread_run = OMX_TRUE;
            pthread_create(&dec->shared_data.output_thread_, NULL, output_thread,
                   &dec->shared_data);
        }
#endif

        OMX_U32 i = 0;

        for(i = 0; i < 3; ++i)
        {
            dec->callbacks.EventHandler(dec->self, dec->appdata,
                                        OMX_EventCmdComplete, OMX_CommandFlush,
                                        i, NULL);
        }
        dec->flushingOutPort = OMX_FALSE;
        DBGT_PDEBUG("ASYNC: all buffers flushed");
    }
    else
    {
        if (portindex == PORT_INDEX_INPUT)
        {
            err = async_decoder_return_buffers(dec, &dec->in);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("async_decoder_return_buffers(in) (err=%x)", err);
                goto FAIL;
            }

            dec->callbacks.EventHandler(dec->self, dec->appdata,
                                        OMX_EventCmdComplete, OMX_CommandFlush,
                                        portindex, NULL);
            DBGT_PDEBUG("ASYNC: input port flushed");
        }
        else if (portindex == PORT_INDEX_OUTPUT)
        {
            err = async_decoder_return_buffers(dec, &dec->out);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("async_decoder_return_buffers(out) (err=%x)", err);
                goto FAIL;
            }

            dec->callbacks.EventHandler(dec->self, dec->appdata,
                                        OMX_EventCmdComplete, OMX_CommandFlush,
                                        portindex, NULL);
            dec->flushingOutPort = OMX_FALSE;
            DBGT_PDEBUG("ASYNC: output port flushed");
        }
        else if (portindex == PORT_INDEX_PP)
        {
            err = async_decoder_return_buffers(dec, &dec->inpp);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("async_decoder_return_buffers(inpp) (err=%x)", err);
                goto FAIL;
            }

            dec->callbacks.EventHandler(dec->self, dec->appdata,
                                        OMX_EventCmdComplete, OMX_CommandFlush,
                                        portindex, NULL);
            DBGT_PDEBUG("ASYNC: post-processor port flushed");
        }
    }

    // flush the PTS buffer
#if 0
    memset(dec->ts_buf.ts_data, 0, sizeof(OMX_TICKS)*dec->ts_buf.capacity);
    dec->ts_buf.count = 0;
#endif

    // flush the Propagate buffer
    memset(dec->propagate_buf.propagate_data, 0, sizeof(PROPAGATE_INPUT_DATA)*dec->propagate_buf.capacity);
    memset(&dec->propagateData, 0, sizeof(PROPAGATE_INPUT_DATA));
    memset(dec->prevPicIdList, -1, sizeof(dec->prevPicIdList));
    dec->propagate_buf.count = 0;
    dec->propagateDataReceived = 0;

    DBGT_EPILOG("");
    return OMX_ErrorNone;
  FAIL:
    DBGT_ASSERT(err != OMX_ErrorNone);
    DBGT_CRITICAL("ASYNC: error while flushing port: %s",
                HantroOmx_str_omx_err(err));
    dec->state = OMX_StateInvalid;
    dec->run = OMX_FALSE;
    dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError, err, 0,
                                NULL);
    DBGT_EPILOG("");
    return err;
}

static void get_mark_data(OMX_DECODER * dec, OMX_MARKTYPE *mark, BUFFER * buff)
{
    if (buff->header->hMarkTargetComponent == dec->self)
    {
        dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventMark, 0,
                                    0, buff->header->pMarkData);
        buff->header->hMarkTargetComponent = NULL;
        buff->header->pMarkData = NULL;
    }

    mark->hMarkTargetComponent = buff->header->hMarkTargetComponent;
    mark->pMarkData = buff->header->pMarkData;
}


static
    OMX_ERRORTYPE async_decoder_mark_buffer(OMX_COMMANDTYPE Cmd,
                                            OMX_U32 nParam1, OMX_PTR pCmdData,
                                            OMX_PTR arg)
{
    UNUSED_PARAMETER(Cmd);
    UNUSED_PARAMETER(nParam1);
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(arg);
    DBGT_ASSERT(pCmdData);
    OMX_MARKTYPE *mark = (OMX_MARKTYPE *) pCmdData;
    OMX_DECODER *dec = (OMX_DECODER *) arg;

    if (dec->mark_count < sizeof(dec->marks) / sizeof(OMX_MARKTYPE))
    {
        dec->marks[dec->mark_write_pos] = *mark;
        dec->mark_count++;
        DBGT_PDEBUG("ASYNC: set mark in index: %d",
                    (int) dec->mark_write_pos);
        dec->mark_write_pos++;
        if(dec->mark_write_pos == sizeof(dec->marks) / sizeof(OMX_MARKTYPE))
            dec->mark_write_pos = 0;
    }
    else
    {
        DBGT_ERROR("ASYNC: no space for mark");
        dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError,
                                    OMX_ErrorUndefined, 0, NULL);
    }
    OSAL_Free(mark);
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE async_get_info(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    // get the information from the codec and create port settings changed event
    STREAM_INFO info;
    OMX_BOOL reconfig = OMX_FALSE;

    memset(&info, 0, sizeof(STREAM_INFO));

    CODEC_STATE ret = dec->codec->getinfo(dec->codec, &info);

    if (ret != CODEC_OK)
    {
        DBGT_CRITICAL("ASYNC: getinfo error: %d", ret);
#ifdef IS_G2_DECODER
        dec->shared_data.EOS = OMX_TRUE;
#else
        if (dec->useExternalAlloc == OMX_TRUE)
        {
            dec->shared_data.EOS = OMX_TRUE;
        }
#endif
        DBGT_EPILOG("");
        return OMX_ErrorHardware;
    }

//#ifdef OMX_DECODER_IMAGE_DOMAIN
    dec->imageSize = info.imageSize;
//#endif
    //dec->out.def.nBufferSize = info.framesize;
    dec->out.def.format.video.nSliceHeight = info.sliceheight;
    dec->out.def.format.video.nStride = info.stride;
    DBGT_PDEBUG("ASYNC: video props: width: %lu height: %lu buffersize: %lu stride: %lu sliceheight: %lu",
        info.width, info.height, info.framesize, info.stride, info.sliceheight);

#ifdef IS_G2_DECODER
    dec->bitDepth = info.bit_depth;
    DBGT_PDEBUG("bit depth: %lu", info.bit_depth);
#endif

#ifdef SET_OUTPUT_CROP_RECT
    if (info.crop_available ||
       ((dec->output_cropping_rect.nWidth > info.crop_width) && (info.crop_width > 0)) ||
       ((dec->output_cropping_rect.nHeight > info.crop_height) && (info.crop_height > 0)))
    {
        set_output_cropping(dec, info.crop_left, info.crop_top, info.crop_width, info.crop_height);
    }
#endif

    if (dec->out.def.format.video.eColorFormat != info.format)
        dec->out.def.format.video.eColorFormat = info.format;
    DBGT_PDEBUG("ASYNC: Output port color format: %s", HantroOmx_str_omx_color(dec->out.def.format.video.eColorFormat));

    if (dec->out.def.format.video.nFrameWidth * dec->out.def.format.video.nFrameHeight < info.width * info.height)
    {
        DBGT_PDEBUG("ASYNC: Output port video resolution changed: %lux%lu -> %lux%lu",
            dec->out.def.format.video.nFrameWidth, dec->out.def.format.video.nFrameHeight,
            info.width, info.height);
        if ((info.width < 48) || (info.height < 48))
        {
            DBGT_CRITICAL("Unsupported video resolution (%lux%lu)", info.width, info.height);
            return OMX_ErrorHardware;
        }

        dec->out.def.format.video.nFrameWidth = info.width;
        dec->out.def.format.video.nFrameHeight = info.height;
        //reconfig = OMX_TRUE;
    }

    if (dec->out.def.nBufferSize < info.framesize)
    {
        DBGT_PDEBUG("ASYNC: Output port buffer size changed: %lu -> %lu",
            dec->out.def.nBufferSize, info.framesize);
        dec->out.def.nBufferSize = info.framesize;
        reconfig = OMX_TRUE;
    }

#ifdef USE_EXTERNAL_BUFFER
    if ((dec->useExternalAlloc == OMX_TRUE) && (dec->out.def.nBufferCountMin < info.frame_buffers))
    {
        DBGT_PDEBUG("ASYNC: Output port buffer count changed: %lu -> %lu",
            dec->out.def.nBufferCountMin, info.frame_buffers);
        dec->out.def.nBufferCountMin = info.frame_buffers;
        if (info.frame_buffers + dec->nGuardSize > dec->out.def.nBufferCountActual)
        {
            dec->out.def.nBufferCountActual = info.frame_buffers + dec->nGuardSize;
            reconfig = OMX_TRUE;
        }
    }
#endif

    if (reconfig)
    {
        dec->portReconfigPending = OMX_TRUE;
        dec->callbacks.EventHandler(dec->self, dec->appdata,
                                    OMX_EventPortSettingsChanged, PORT_INDEX_OUTPUT,
                                    0, NULL);
    }

    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE grow_frame_buffer(OMX_DECODER * dec, FRAME_BUFFER * fb,
                                    OMX_U32 capacity)
{
    CALLSTACK;
    DBGT_PROLOG("");
    DBGT_PDEBUG("ASYNC: fb size: %u capacity: %u new capacity:%u",
                (unsigned) fb->size, (unsigned) fb->capacity,
                (unsigned) capacity);

    DBGT_ASSERT(capacity >= fb->size);

    FRAME_BUFFER new;

    memset(&new, 0, sizeof(FRAME_BUFFER));
    DBGT_PDEBUG("grow_frame_buffer: OSAL_AllocatorAllocMem");
    OMX_ERRORTYPE err =
        OSAL_AllocatorAllocMem(&dec->alloc, &capacity, &new.bus_data,
                               &new.bus_address);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("ASYNC: frame buffer reallocation failed: %s",
                    HantroOmx_str_omx_err(err));
        DBGT_EPILOG("");
        return err;
    }
    DBGT_PDEBUG("API: allocated grow frame buffer size:%u @physical addr:0x%08lx @logical addr:%p",
                (unsigned) capacity, new.bus_address, new.bus_data);

    memcpy(new.bus_data, fb->bus_data, fb->size);
    new.capacity = capacity;
    new.size = fb->size;
    OSAL_AllocatorFreeMem(&dec->alloc, fb->capacity, fb->bus_data,
                          fb->bus_address);
    *fb = new;
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE async_get_frame_buffer(OMX_DECODER * dec, FRAME * frm)
{
    CALLSTACK;
    DBGT_PROLOG("");

    OMX_U32 framesize = dec->out.def.nBufferSize;
    OMX_ERRORTYPE err = OMX_ErrorNone;
    BUFFER *buff = NULL;

    err = HantroOmx_port_lock_buffers(&dec->out);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("HantroOmx_port_lock_buffers(out) (err=%x)", err);
        DBGT_EPILOG("");
        return err;
    }

    HantroOmx_port_get_buffer(&dec->out, &buff);
    HantroOmx_port_unlock_buffers(&dec->out);

    OMX_S32 retry = MAX_RETRIES; // needed for stagefright when video is paused (no transition to pause)
    while (buff == NULL && retry > 0 && dec->state != OMX_StatePause && dec->statetrans != OMX_StatePause && dec->statetrans != OMX_StateIdle)
    {
        OSAL_ThreadSleep(RETRY_INTERVAL);

        /* Fix deadlock on port flush/disable with Stagefright */
        if ((buff == NULL) && (dec->disablingOutPort))
        {
            DBGT_ERROR("Force exit because of pending PortDisable to avoid deadlock.");
            DBGT_EPILOG("");
            return OMX_ErrorNoMore;
        }
        if ((buff == NULL) && (dec->flushingOutPort))
        {
            DBGT_PDEBUG("Force exit because of pending PortFlush to avoid deadlock.");
            DBGT_EPILOG("");
            return OMX_ErrorNoMore;
        }

        err = HantroOmx_port_lock_buffers(&dec->out);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("HantroOmx_port_lock_buffers(out) (err=%x)", err);
            DBGT_EPILOG("");
            return err;
        }

        HantroOmx_port_get_buffer(&dec->out, &buff);
        HantroOmx_port_unlock_buffers(&dec->out);
        retry--;
    }

    if (buff == NULL)
    {
        if (dec->statetrans == OMX_StateIdle)
        {
            DBGT_EPILOG("");
            return OMX_ErrorNoMore;
        }

        if (dec->state != OMX_StatePause || dec->statetrans != OMX_StatePause)
        {
            DBGT_EPILOG("");
            return OMX_ErrorNone;
        }
        DBGT_CRITICAL("ASYNC: there's no output buffer!");
        DBGT_EPILOG("");
        return OMX_ErrorOverflow;
    }

    if (framesize > buff->header->nAllocLen)
    {
        DBGT_CRITICAL("ASYNC: frame is too big for output buffer. framesize:%u nAllocLen:%u",
                    (unsigned) framesize, (unsigned) buff->header->nAllocLen);
        DBGT_EPILOG("");
        return OMX_ErrorOverflow;
    }

    if ((buff->flags & BUFFER_FLAG_MY_BUFFER) || (buff->flags & BUFFER_FLAG_ANDROID_NATIVE_BUFFER) || (buff->flags & BUFFER_FLAG_EXT_ALLOC))
    {
        // can stick the data directly into the output buffer
        DBGT_ASSERT(buff->bus_data);
        DBGT_ASSERT(buff->bus_address);
        if (!(buff->flags & BUFFER_FLAG_ANDROID_NATIVE_BUFFER))
            DBGT_ASSERT(buff->header->pBuffer == buff->bus_data);

        frm->fb_bus_data = buff->bus_data;
        frm->fb_bus_address = buff->bus_address;
        frm->fb_size = buff->header->nAllocLen;
        DBGT_PDEBUG("ASYNC: using output buffer:%p header:%p", frm->fb_bus_data, buff->header);
    }
    else
    {
        DBGT_PDEBUG("ASYNC: using temporary output buffer");
        // need to stick the data into the temporary output buffer
        FRAME_BUFFER *temp = &dec->frame_out;

        DBGT_ASSERT(temp->bus_data);
        DBGT_ASSERT(temp->bus_address);
        DBGT_ASSERT(temp->size == 0);
        if (framesize > temp->capacity)
        {
            OMX_U32 capacity = temp->capacity;

            if (capacity == 0)
                capacity = 384; // TODO

            while(capacity < framesize)
                capacity *= 2;
            if (capacity != temp->capacity)
            {
                err = grow_frame_buffer(dec, temp, capacity);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("grow_frame_buffer (err=%x)", err);
                    DBGT_EPILOG("");
                    return OMX_ErrorInsufficientResources;
                }
            }
        }
        DBGT_ASSERT(temp->capacity >= framesize);
        frm->fb_bus_data = temp->bus_data;
        frm->fb_bus_address = temp->bus_address;
        frm->fb_size = temp->capacity;
        DBGT_PDEBUG("ASYNC: temp buffer: bus address %lu, size %d", frm->fb_bus_address, (int)frm->fb_size);
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    void async_dispatch_frame_buffer(OMX_DECODER * dec, OMX_BOOL EOS,
                                     BUFFER * inbuff, FRAME * frm)
{
    UNUSED_PARAMETER(EOS);
    CALLSTACK;
    DBGT_PROLOG("");
    OMX_U32 i = 0;

    //DBGT_ASSERT(frm->size);
    //DBGT_ASSERT(frm->fb_bus_data);

    if ((frm->fb_bus_data == 0) || (frm->size == 0))
    {
        DBGT_PDEBUG("ASYNC: Frame bus data or size is zero. Nothing to dispatch.");
        DBGT_EPILOG("");
        return;
    }

    BUFFER *buff = NULL;

    if (dec->useExternalAlloc == OMX_TRUE)
    {
        HantroOmx_port_lock_buffers(&dec->out);
        OMX_U32 x = HantroOmx_port_buffer_queue_count(&dec->out);

        for (i=0; i<x; ++i)
        {
            HantroOmx_port_get_buffer_at(&dec->out, &buff, i);

            if (buff->bus_address && (buff->bus_address == frm->fb_bus_address))
            {
                DBGT_PDEBUG("Found buffer %p; address %lu, %lu", buff->bus_data, buff->bus_address, i);
                break;
            }
        }
        HantroOmx_port_unlock_buffers(&dec->out);
        if (i >= x)
        {
            DBGT_ASSERT(0);
        }
    }
    else
    {
        HantroOmx_port_lock_buffers(&dec->out);
        HantroOmx_port_get_buffer(&dec->out, &buff);
        HantroOmx_port_unlock_buffers(&dec->out);
    }

    DBGT_ASSERT(buff);
    DBGT_ASSERT(inbuff);

    if (frm->fb_bus_data == dec->frame_out.bus_data)
    {
        DBGT_PDEBUG("Copy data from the temporary output buffer");
        DBGT_ASSERT(buff->header->nAllocLen >= frm->size);
        // copy data from the temporary output buffer
        // into the real output buffer
        memcpy(buff->header->pBuffer, frm->fb_bus_data, frm->size);
    }
    buff->header->nOffset = 0;
    buff->header->nFilledLen = frm->size;
    buff->header->nTimeStamp = inbuff->header->nTimeStamp;

#ifdef USE_OUTPUT_BUFFER_PRIVATE
    // pOutputPortPrivate points to OUTPUT_BUFFER_PRIVATE structure
    if (frm->outBufPrivate.nLumaBusAddress)
        buff->header->pOutputPortPrivate = &frm->outBufPrivate;
    else
        buff->header->pOutputPortPrivate = NULL;
#endif

    if (strcmp((char*)dec->role, "video_decoder.jpeg") == 0)
    {
        dec->buffer = buff;
    }

    PROPAGATE_INPUT_DATA tmp_propagate_data;
    OMX_BOOL tmp_ret;
    OMX_U32 tmp_id;
    memset(&tmp_propagate_data, 0, sizeof(PROPAGATE_INPUT_DATA));

    if (frm->outBufPrivate.nPicId[0] == frm->outBufPrivate.nPicId[1])
        tmp_id = frm->outBufPrivate.nPicId[0];
    else if ((int)frm->outBufPrivate.nPicId[0] == -1)
        tmp_id = frm->outBufPrivate.nPicId[1];
    else if ((int)frm->outBufPrivate.nPicId[1] == -1)
        tmp_id = frm->outBufPrivate.nPicId[0];
    else
    {
        // remove the propagate_data of the other field in propagate_buf.
        tmp_id = (frm->outBufPrivate.nPicId[0] > frm->outBufPrivate.nPicId[1]) ?
                  frm->outBufPrivate.nPicId[0] : frm->outBufPrivate.nPicId[1];

        // tmp_id is bigger than oldestPicId, the corresponding propagate_data
        // must be in propagate_buf.
        if (tmp_id >= dec->oldestPicIdInBuf)
        {
            OSAL_MutexLock(dec->timemutex);
            tmp_ret = pop_propagate_data(dec, &tmp_propagate_data, tmp_id);
            OSAL_MutexUnlock(dec->timemutex);

            while(tmp_ret != OMX_TRUE)
            {
                if (dec->releaseBuffers)
                {
                    tmp_ret = OMX_FALSE;
                    break;
                }
                OSAL_MutexLock(dec->timemutex);
                tmp_ret = pop_propagate_data(dec, &tmp_propagate_data, tmp_id);
                OSAL_MutexUnlock(dec->timemutex);
                OSAL_ThreadSleep(RETRY_INTERVAL);
            }
        }

        tmp_id = (frm->outBufPrivate.nPicId[0] < frm->outBufPrivate.nPicId[1]) ?
                  frm->outBufPrivate.nPicId[0] : frm->outBufPrivate.nPicId[1];
    }

    if (tmp_id >= dec->oldestPicIdInBuf)
    {
        OSAL_MutexLock(dec->timemutex);
        tmp_ret = pop_propagate_data(dec, &tmp_propagate_data, tmp_id);
        OSAL_MutexUnlock(dec->timemutex);

        while(tmp_ret != OMX_TRUE)
        {
            if (dec->releaseBuffers)
            {
                tmp_ret = OMX_FALSE;
                break;
            }
            OSAL_MutexLock(dec->timemutex);
            tmp_ret = pop_propagate_data(dec, &tmp_propagate_data, tmp_id);
            OSAL_MutexUnlock(dec->timemutex);
            OSAL_ThreadSleep(RETRY_INTERVAL);
        }

        if (tmp_ret == OMX_TRUE)
        {
            buff->header->nTimeStamp = tmp_propagate_data.ts_data;

            if (dec->mark_count)
            {
                DBGT_PDEBUG("ASYNC: got %d marks pending", (int)markcount);
                buff->header->hMarkTargetComponent =
                    dec->marks[dec->mark_read_pos].hMarkTargetComponent;
                buff->header->pMarkData = dec->marks[dec->mark_read_pos].pMarkData;
                dec->mark_read_pos++;

                if (dec->mark_read_pos == sizeof(dec->marks) / sizeof(OMX_MARKTYPE))
                    dec->mark_read_pos = 0;

                if (--dec->mark_count == 0)
                {
                    dec->mark_read_pos = 0;
                    dec->mark_write_pos = 0;
                    DBGT_PDEBUG("ASYNC: mark buffer empty!");
                }
            }
            else
            {
                buff->header->hMarkTargetComponent = tmp_propagate_data.marks.hMarkTargetComponent;
                buff->header->pMarkData = tmp_propagate_data.marks.pMarkData;
            }
        }
        // decoder aborted but the timestamp of this buffer is not received yet,
        // clear the buffer header.
        else
        {
            buff->header->nFilledLen = 0;
            buff->header->nOffset = 0;
            buff->header->nTickCount = 0;
#ifdef OMX_SKIP64BIT
            buff->header->nTimeStamp.nLowPart = buff->header->nTimeStamp.nHighPart = 0;
#else
            buff->header->nTimeStamp = 0;
#endif
            buff->header->hMarkTargetComponent = NULL;
            buff->header->pMarkData = NULL;
            buff->header->nFlags &= ~OMX_BUFFERFLAG_EOS;
        }
    }
    // the propagate_data with picIndex=tmp_has not been in buffer any more.
    else
    {
#ifdef OMX_SKIP64BIT
        buff->header->nTimeStamp.nLowPart = buff->header->nTimeStamp.nHighPart = 0;
#else
        buff->header->nTimeStamp = 0;
#endif

        buff->header->hMarkTargetComponent = NULL;
        buff->header->pMarkData = NULL;
    }

    buff->header->nFlags = inbuff->header->nFlags;
    buff->header->nFlags &= ~OMX_BUFFERFLAG_EOS;

    if(frm->viewId > 0)
    {
        buff->header->nFlags |= OMX_BUFFERFLAG_SECOND_VIEW;
        DBGT_PDEBUG("Set OMX_BUFFERFLAG_SECOND_VIEW");
    }
#if 0
    OMX_S32 markcount = dec->mark_write_pos - dec->mark_read_pos;

    if (markcount)
    {
        DBGT_PDEBUG("ASYNC: got %d marks pending", (int)markcount);
        buff->header->hMarkTargetComponent =
            dec->marks[dec->mark_read_pos].hMarkTargetComponent;
        buff->header->pMarkData = dec->marks[dec->mark_read_pos].pMarkData;
        dec->mark_read_pos++;
        if (--markcount == 0)
        {
            dec->mark_read_pos = 0;
            dec->mark_write_pos = 0;
            DBGT_PDEBUG("ASYNC: mark buffer empty!");
        }
    }
    else
    {
        buff->header->hMarkTargetComponent =
            inbuff->header->hMarkTargetComponent;
        buff->header->pMarkData = inbuff->header->pMarkData;
    }
#endif
#ifdef IS_G1_DECODER
    if (dec->useExternalAlloc == OMX_FALSE)
    {
        if (dec->buffer)
        {
            if (HantroOmx_port_is_tunneled(&dec->out))
            {
                ((OMX_COMPONENTTYPE *) dec->out.tunnelcomp)->EmptyThisBuffer(dec->
                                                                             out.
                                                                             tunnelcomp,
                                                                             dec->
                                                                             buffer->
                                                                             header);
                dec->buffer = NULL;
            }
            else
            {
                //DBGT_PDEBUG("ASYNC: firing FillBufferDone header:%p", &dec->buffer->header);
                dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                              dec->buffer->header);
                dec->buffer = NULL;
            }
        }
        // defer returning the buffer untill later time.
        // this is because we dont know when we're sending the last buffer cause it
        // is possible that the codec has internal buffering for frames, and only after codec::getframe
        // has been invoked do we know when there are no more frames available. Unless the buffer sending
        // was deferred, there would be no way to stamp the last buffer with OMX_BUFFERFLAG_EOS
        dec->buffer = buff;
    }
    if ((dec->useExternalAlloc == OMX_TRUE) && buff)
    {
        if (HantroOmx_port_is_tunneled(&dec->out))
        {
            ((OMX_COMPONENTTYPE *) dec->out.tunnelcomp)->EmptyThisBuffer(dec->
                                                                         out.
                                                                         tunnelcomp,
                                                                         buff->
                                                                         header);
            dec->buffer = NULL;
        }
        else
        {
            DBGT_PDEBUG("ASYNC: FillBufferDone Buffer: %p", buff);
            dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                          buff->header);
            dec->buffer = NULL;
        }
    }
#else
    if (buff)
    {
        if (HantroOmx_port_is_tunneled(&dec->out))
        {
            ((OMX_COMPONENTTYPE *) dec->out.tunnelcomp)->EmptyThisBuffer(dec->
                                                                         out.
                                                                         tunnelcomp,
                                                                         buff->
                                                                         header);
            dec->buffer = NULL;
        }
        else
        {
            DBGT_PDEBUG("ASYNC: FillBufferDone Buffer: %p", buff);
            dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                          buff->header);
            dec->buffer = NULL;
        }
    }
#endif

    HantroOmx_port_lock_buffers(&dec->out);
    if (dec->useExternalAlloc == OMX_TRUE)
    {
        HantroOmx_port_pop_buffer_at(&dec->out, i);
    }
    else
    {
        HantroOmx_port_pop_buffer(&dec->out);
    }
    HantroOmx_port_unlock_buffers(&dec->out);

    DBGT_EPILOG("");
}

#ifndef IS_G2_DECODER
static void async_dispatch_last_frame(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    BUFFER *buff = NULL;

#ifdef OMX_DECODER_IMAGE_DOMAIN
    if (strcmp((char*)dec->role, "image_decoder.webp") != 0)
    {
        HantroOmx_port_lock_buffers(&dec->out);
        HantroOmx_port_get_buffer(&dec->out, &buff);
        HantroOmx_port_unlock_buffers(&dec->out);

        buff->header->nOffset = 0;
        buff->header->nFilledLen = dec->imageSize;
#ifdef OMX_SKIP64BIT
        buff->header->nTimeStamp.nLowPart = buff->header->nTimeStamp.nHighPart = 0;
#else
        buff->header->nTimeStamp = 0;
#endif
        buff->header->nFlags |= OMX_BUFFERFLAG_EOS;
        dec->buffer = buff;
    }

    if (HantroOmx_port_is_tunneled(&dec->out))
    {
        ((OMX_COMPONENTTYPE *) dec->out.tunnelcomp)->EmptyThisBuffer(dec->out.
                                                                        tunnelcomp,
                                                                        dec->
                                                                        buffer->
                                                                        header);
        dec->buffer = NULL;
    }
    else
    {
        dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                        dec->buffer->header);
        dec->buffer = NULL;
    }

    if (strcmp((char*)dec->role, "image_decoder.webp") != 0)
    {
        HantroOmx_port_lock_buffers(&dec->out);
        HantroOmx_port_pop_buffer(&dec->out);
        HantroOmx_port_unlock_buffers(&dec->out);
    }
#else /* Video decoder */
    OMX_BOOL sendExtraBuffer = OMX_FALSE;

    if (dec->buffer == NULL)
    {
        DBGT_EPILOG("");
        return;
    }

    if (dec->buffer->header->nFilledLen != 0)
    {
        DBGT_PDEBUG("Decoder has data... getting extra buffer");
        HantroOmx_port_lock_buffers(&dec->out);
        sendExtraBuffer = HantroOmx_port_get_buffer(&dec->out, &buff);
        HantroOmx_port_unlock_buffers(&dec->out);
    }

    if (sendExtraBuffer == OMX_FALSE)
    {
        dec->buffer->header->nFlags |= OMX_BUFFERFLAG_EOS;
    }

    if (HantroOmx_port_is_tunneled(&dec->out))
    {
        ((OMX_COMPONENTTYPE *) dec->out.tunnelcomp)->EmptyThisBuffer(dec->out.
                                                                     tunnelcomp,
                                                                     dec->
                                                                     buffer->
                                                                     header);
        dec->buffer = NULL;
    }
    else
    {
        DBGT_PDEBUG("async_dispatch_last_frame: FillBufferDone Buffer: %p", dec->buffer);
        dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                      dec->buffer->header);
        dec->buffer = NULL;
    }

#ifdef ANDROID
    /* Send the extra buffer with EOS flag and nFilledLen = 0 to meet Stagefright requirement */
    if (sendExtraBuffer)
    {
        DBGT_ASSERT(buff);
        DBGT_PDEBUG("Sending extra buffer with EOS flag and nFilledLen = 0");
        dec->buffer = buff;
        dec->buffer->header->nFlags |= OMX_BUFFERFLAG_EOS;
        dec->buffer->header->nFilledLen = 0;
#ifdef OMX_SKIP64BIT
        buff->header->nTimeStamp.nLowPart = buff->header->nTimeStamp.nHighPart = 0;
#else
        buff->header->nTimeStamp = 0;
#endif

        if(HantroOmx_port_is_tunneled(&dec->out))
        {
            ((OMX_COMPONENTTYPE *) dec->out.tunnelcomp)->EmptyThisBuffer(dec->out.
                                                                         tunnelcomp,
                                                                         dec->
                                                                         buffer->
                                                                         header);
            dec->buffer = NULL;
        }
        else
        {
            dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                          dec->buffer->header);
            dec->buffer = NULL;
        }

        HantroOmx_port_lock_buffers(&dec->out);
        HantroOmx_port_pop_buffer(&dec->out);
        HantroOmx_port_unlock_buffers(&dec->out);
    }
#endif // ANDROID
#endif // video domain
    DBGT_EPILOG("");
}
#endif // IS_G2_DECODER

static
    OMX_ERRORTYPE async_get_frames(OMX_DECODER * dec, OMX_BOOL EOS,
                                   BUFFER * inbuff)
{
    CALLSTACK;
    DBGT_PROLOG("");

    OMX_ERRORTYPE err = OMX_ErrorNone;
    CODEC_STATE state = CODEC_HAS_FRAME;

    while(state == CODEC_HAS_FRAME)
    {
        FRAME frm;

        memset(&frm, 0, sizeof(FRAME));

        err = async_get_frame_buffer(dec, &frm);
        if (err == OMX_ErrorNoMore)
        {
            goto bail;
        }
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("async_get_frame_buffer (err=%x)", err);
            DBGT_EPILOG("");
            return err;
        }

        state = dec->codec->getframe(dec->codec, &frm, EOS);
        //DBGT_PDEBUG("ASYNC getframe stat %d", state);
        //DBGT_PDEBUG("ASYNC getframe isIntra %d isGoldenOrAlternate %d", dec->isIntra, dec->isGoldenOrAlternate);
        if (state == CODEC_OK)
            break;
#ifdef ENABLE_CODEC_VP8
        dec->isIntra = frm.isIntra;
        dec->isGoldenOrAlternate = frm.isGoldenOrAlternate;
#endif
        if (state < 0)
        {
            DBGT_CRITICAL("ASYNC: getframe error:%d", state);
            DBGT_EPILOG("");
            return OMX_ErrorHardware;
        }
        if (frm.size)
            async_dispatch_frame_buffer(dec, OMX_FALSE, inbuff, &frm);
    }

bail:
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static
    OMX_ERRORTYPE async_decode_data(OMX_DECODER * dec, OMX_U8 * bus_data,
                                    OSAL_BUS_WIDTH bus_address, OMX_U32 datalen,
                                    BUFFER * buff, OMX_U32 buff_len,
                                    OMX_U32 * retlen)
{
    CALLSTACK;
    DBGT_PROLOG("");

    DBGT_ASSERT(dec);
    DBGT_ASSERT(retlen);

    OMX_BOOL EOS =
        buff->header->nFlags & OMX_BUFFERFLAG_EOS ? OMX_TRUE : OMX_FALSE;
    OMX_BOOL sendEOS = OMX_FALSE;
    OMX_ERRORTYPE err = OMX_ErrorNone;

#if 0
    {
        /* Dump input buffer into file for tracing */
        static int frameCtr = 0;

        char filename[30];

        FILE *fp;

        sprintf(filename, "input%d.dump", frameCtr++);
        fp = fopen(filename, "wb");
        if (fp)
        {
            fwrite(bus_data, datalen, 1, fp);
            fclose(fp);
        }
    }
#endif
#ifdef IS_G1_DECODER
    if (dec->pp_args.rotation != dec->conf_rotation.nRotation)
    {
        OMX_BOOL changeDimensions = OMX_FALSE;

        DBGT_PDEBUG("Rotation changed. old: %d, new: %d",
            (int)dec->pp_args.rotation, (int)dec->conf_rotation.nRotation);

        if ((dec->pp_args.rotation == 0 || dec->pp_args.rotation == 180) &&
            (dec->conf_rotation.nRotation == 90 || dec->conf_rotation.nRotation == -90))
        {
            changeDimensions = OMX_TRUE;
        }
        else if ((dec->pp_args.rotation == 90 || dec->pp_args.rotation == -90) &&
            (dec->conf_rotation.nRotation == 0 || dec->conf_rotation.nRotation == 180))
        {
            changeDimensions = OMX_TRUE;
        }

        if (changeDimensions)
        {
            OMX_U32 tmp;
            DBGT_PDEBUG("Port dimensions changed");

            tmp = dec->out.def.format.video.nFrameWidth;
            dec->out.def.format.video.nFrameWidth =
                dec->out.def.format.video.nFrameHeight;
            dec->out.def.format.video.nFrameHeight = tmp;

            tmp = dec->out.def.format.video.nStride;
            dec->out.def.format.video.nStride =
                dec->out.def.format.video.nSliceHeight;
            dec->out.def.format.video.nSliceHeight = tmp;

#ifndef DISABLE_OUTPUT_SCALING
            dec->pp_args.scale.width = dec->out.def.format.video.nFrameWidth;
            dec->pp_args.scale.height = dec->out.def.format.video.nFrameHeight;
#endif
        }

        dec->pp_args.rotation = dec->conf_rotation.nRotation;

        DBGT_PDEBUG("Set Post-Processor arguments");
        if (dec->codec->setppargs(dec->codec, &dec->pp_args) != CODEC_OK)
        {
            err = OMX_ErrorBadParameter;
            DBGT_CRITICAL("ASYNC: failed to set Post-Processor arguments");
            return err;
        }

        if (changeDimensions)
        {
            dec->portReconfigPending = OMX_TRUE;
            dec->callbacks.EventHandler(dec->self, dec->appdata,
                OMX_EventPortSettingsChanged, PORT_INDEX_OUTPUT,
                OMX_IndexConfigCommonRotate, NULL);
        }
    }
#endif

    if(datalen == 0)
    {
#ifdef IS_G1_DECODER
        if (dec->useExternalAlloc == OMX_TRUE)
        {
            OSAL_MutexLock(dec->threadmutex);
            dec->shared_data.inbuff = buff;
            OSAL_MutexUnlock(dec->threadmutex);
        }
#else
        OSAL_MutexLock(dec->threadmutex);
        dec->shared_data.inbuff = buff;
        OSAL_MutexUnlock(dec->threadmutex);
#endif
    }

    STREAM_BUFFER stream;
    stream.buf_data = bus_data;
    stream.buf_address = bus_address;

    while((datalen > 0) && !dec->portReconfigPending && !dec->releaseBuffers)
    {
        OMX_U32 first = 0;   // offset to the start of the first frame in the buffer
        OMX_U32 last = 0;    // offset to the start of the last frame in the buffer

        stream.bus_data = bus_data; // + offset
        stream.bus_address = bus_address;   // + offset
        stream.streamlen = datalen; // - offset
        /* For partially decoded input buffer, we use dec->frame_in instead of
           input buffer. */
        stream.allocsize = buff_len; //buff->allocsize;

        // see if we can find complete frames in the buffer
        OMX_S32 ret = dec->codec->scanframe(dec->codec, &stream, &first, &last);

        if (ret == -1 || first == last)
        {
            DBGT_PDEBUG("ASYNC: no decoding unit in input buffer");
            if (EOS == OMX_FALSE)
                break;
            // assume that remaining data contains a single complete decoding unit
            // fingers crossed..
            first = 0;
            last = datalen;
            // if there _isn't_ that remaining frame then an output buffer
            // with EOS flag is never sent. oh well.
            sendEOS = OMX_TRUE;
        }

        // got at least one complete frame between first and last
        stream.bus_data = bus_data + first;
        stream.bus_address = bus_address + first;   // is this ok?
        stream.streamlen = last - first;
        stream.sliceInfoNum =  dec->sliceInfoNum;
        stream.pSliceInfo =  dec->pSliceInfo;
        stream.picId = dec->propagateData.picIndex;

        OMX_U32 bytes = 0;
        FRAME frm;
        memset(&frm, 0, sizeof(FRAME));

#ifdef IS_G1_DECODER
        if (dec->useExternalAlloc == OMX_FALSE)
        {
            err = async_get_frame_buffer(dec, &frm);
            if (err == OMX_ErrorNoMore)
            {
                goto bail;
            }
            if (err != OMX_ErrorNone)
            {
                if (err == OMX_ErrorOverflow)
                {
                    DBGT_PDEBUG("ASYNC: firing OMX_EventErrorOverflow");
                    dec->callbacks.EventHandler(dec->self, dec->appdata,
                                                OMX_EventError, OMX_ErrorOverflow,
                                                0, NULL);
                    *retlen = datalen;
                    DBGT_EPILOG("");
                    return OMX_ErrorNone;
                }
                DBGT_CRITICAL("async_get_frame_buffer (err=%x)", err);
                DBGT_EPILOG("");
                return err;
            }
        }
        else
        {
            OSAL_MutexLock(dec->threadmutex);
            dec->shared_data.inbuff = buff;
            OSAL_MutexUnlock(dec->threadmutex);
        }
#else
        OSAL_MutexLock(dec->threadmutex);
        dec->shared_data.inbuff = buff;
        OSAL_MutexUnlock(dec->threadmutex);
#endif

        CODEC_STATE codec =
            dec->codec->decode(dec->codec, &stream, &bytes, &frm);

#ifndef OMX_DECODER_IMAGE_DOMAIN
        if (frm.size)
            async_dispatch_frame_buffer(dec, OMX_FALSE, buff, &frm);
#endif
        // note: handle stream errors

        OMX_BOOL dobreak = OMX_FALSE;

        switch (codec)
        {
        case CODEC_OK:
        case CODEC_NEED_MORE:
            break;
        case CODEC_NO_DECODING_BUFFER:
            usleep(10);
            break;
        case CODEC_BUFFER_EMPTY:
            if(!dec->propagateDataReceived)
            {
#ifdef OMX_SKIP64BIT
                if ((buff->header->nTimeStamp.nLowPart != -1) && (buff->header->nTimeStamp.nHighPart != -1))
                {
                    if ( !((buff->header->nTimeStamp.nLowPart == 0) && (buff->header->nTimeStamp.nHighPart == 0)) &&
                         !((buff->header->nFilledLen == 0) /*&& (buff->header->nFlags & OMX_BUFFERFLAG_EOS)*/) &&
                         !(buff->header->nFlags & OMX_BUFFERFLAG_CODECCONFIG) )
                    {
                        OSAL_MutexLock(dec->timemutex);
                        dec->propagateData.ts_data = buff->header->nTimeStamp;
                        get_mark_data(dec, &dec->propagateData.marks, buff);

                        if(dec->inBufferIsUpdated)
                            dec->inBufferIsUpdated = 0;
                        else
                        {
                            memset(&dec->propagateData.ts_data, 0, sizeof(OMX_TICKS));
                            memset(&dec->propagateData.marks, 0, sizeof(OMX_MARKTYPE));
                        }

                        receive_propagate_data(dec, &dec->propagateData);
                        OSAL_MutexUnlock(dec->timemutex);
                    }
                }
#else
                if (buff->header->nTimeStamp != -1)
                {
                    if ( !(buff->header->nTimeStamp == 0 && buff->header->nFilledLen == 0) &&
                         /*!(buff->header->nFlags & OMX_BUFFERFLAG_EOS) && */
                         !(buff->header->nFlags & OMX_BUFFERFLAG_CODECCONFIG) )
                    {
                        OSAL_MutexLock(dec->timemutex);
                        dec->propagateData.ts_data = buff->header->nTimeStamp;
                        get_mark_data(dec, &dec->propagateData.marks, buff);

                        if(dec->inBufferIsUpdated)
                            dec->inBufferIsUpdated = 0;
                        else
                        {
                            memset(&dec->propagateData.ts_data, 0, sizeof(OMX_TICKS));
                            memset(&dec->propagateData.marks, 0, sizeof(OMX_MARKTYPE));
                        }

                        receive_propagate_data(dec, &dec->propagateData);
                        OSAL_MutexUnlock(dec->timemutex);
                    }
                }
#endif
            dec->propagateDataReceived = 1;
            }
            break;
        case CODEC_HAS_INFO:
            err = async_get_info(dec);
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("async_get_info, (err=%x)", err);
                DBGT_EPILOG("");
                return err;
            }
            break;
        case CODEC_HAS_FRAME:
            if(!dec->propagateDataReceived)
            {
#ifdef OMX_SKIP64BIT
                if ((buff->header->nTimeStamp.nLowPart != -1) && (buff->header->nTimeStamp.nHighPart != -1))
                {
                    if ( !((buff->header->nTimeStamp.nLowPart == 0) && (buff->header->nTimeStamp.nHighPart == 0)) &&
                         !((buff->header->nFilledLen == 0) /*&& (buff->header->nFlags & OMX_BUFFERFLAG_EOS)*/) &&
                         !(buff->header->nFlags & OMX_BUFFERFLAG_CODECCONFIG) )
                    {
                        OSAL_MutexLock(dec->timemutex);
                        dec->propagateData.ts_data = buff->header->nTimeStamp;
                        get_mark_data(dec, &dec->propagateData.marks, buff);

                        if(dec->inBufferIsUpdated)
                            dec->inBufferIsUpdated = 0;
                        else
                        {
                            memset(&dec->propagateData.ts_data, 0, sizeof(OMX_TICKS));
                            memset(&dec->propagateData.marks, 0, sizeof(OMX_MARKTYPE));
                        }

                        receive_propagate_data(dec, &dec->propagateData);
                        OSAL_MutexUnlock(dec->timemutex);
                    }
                }
#else
                if (buff->header->nTimeStamp != -1)
                {
                    if ( !(buff->header->nTimeStamp == 0 && buff->header->nFilledLen == 0) &&
                         /*!(buff->header->nFlags & OMX_BUFFERFLAG_EOS) &&*/
                         !(buff->header->nFlags & OMX_BUFFERFLAG_CODECCONFIG) )
                    {
                        OSAL_MutexLock(dec->timemutex);
                        dec->propagateData.ts_data = buff->header->nTimeStamp;
                        get_mark_data(dec, &dec->propagateData.marks, buff);

                        if(dec->inBufferIsUpdated)
                            dec->inBufferIsUpdated = 0;
                        else
                        {
                            memset(&dec->propagateData.ts_data, 0, sizeof(OMX_TICKS));
                            memset(&dec->propagateData.marks, 0, sizeof(OMX_MARKTYPE));
                        }

                        receive_propagate_data(dec, &dec->propagateData);
                        OSAL_MutexUnlock(dec->timemutex);
                    }
                }
#endif
            }
            dec->propagateData.picIndex++;
            dec->propagateDataReceived = 0;

        case CODEC_PENDING_FLUSH:
#ifdef IS_G1_DECODER
            if (dec->useExternalAlloc == OMX_FALSE)
            {
                err = async_get_frames(dec, sendEOS, buff);
                if (err != OMX_ErrorNone)
                {
                    if (err == OMX_ErrorOverflow)
                    {
                        DBGT_PDEBUG("ASYNC: saving decode state");
                        dec->codecstate = CODEC_HAS_FRAME;
                        dobreak = OMX_TRUE;
                        break;
                    }
                    DBGT_CRITICAL("async_get_frames (err=%x)", err);
                    DBGT_EPILOG("");
                    return err;
                }
            }
/*
            else if ((dec->useExternalAlloc == OMX_TRUE) && !dec->shared_data.output_thread_)
            {
                DBGT_PDEBUG("ASYNC: create G1 output thread");
                dec->shared_data.output_thread_run = OMX_TRUE;
                pthread_create(&dec->shared_data.output_thread_, NULL, output_thread,
                        &dec->shared_data);
            }
*/

#endif
#ifdef IS_G2_DECODER
/*
            if (!dec->shared_data.output_thread_)
            {
                DBGT_PDEBUG("ASYNC: create G2 output thread");
                dec->shared_data.output_thread_run = OMX_TRUE;
                pthread_create(&dec->shared_data.output_thread_, NULL, output_thread,
                       &dec->shared_data);
            }
*/
#endif
            break;
        case CODEC_ABORTED:
            return OMX_ErrorNone;
#ifdef USE_EXTERNAL_BUFFER
        case CODEC_WAITING_FRAME_BUFFER:
            {
                OMX_U32 i = 0;
                FRAME_BUFFER_INFO bufInfo;

                bufInfo = dec->codec->getframebufferinfo(dec->codec);

                /* Only one buffer need to be realloced for vp9, should do reconfig */
                if (bufInfo.fb_bus_address != 0)
                {
                    dec->ReallocBufferAddress = bufInfo.fb_bus_address;
                    dec->out.def.nBufferSize = bufInfo.bufferSize;

                    dec->portReconfigPending = OMX_TRUE;
                    dec->callbacks.EventHandler(dec->self, dec->appdata,
                                                OMX_EventPortSettingsChanged, PORT_INDEX_OUTPUT,
                                                0, NULL);
                    break;
                }
                /* Check buffer availability and size. If there is not enough buffers or buffer size is too small */
                /* send port settings changed event. */
                else if (bufInfo.numberOfBuffers > dec->out.def.nBufferCountMin || bufInfo.bufferSize > dec->out.def.nBufferSize)
                {
                    DBGT_PDEBUG("ASYNC: Output port buffer parameters changed");
                    DBGT_PDEBUG("ASYNC: nBufferCountMin %d", (int)bufInfo.numberOfBuffers);

                    if (bufInfo.bufferSize > dec->out.def.nBufferSize)
                        dec->portReconfigPending = OMX_TRUE;

                    if (bufInfo.numberOfBuffers + dec->nGuardSize > dec->out.def.nBufferCountActual)
                    {
                        dec->portReconfigPending = OMX_TRUE;
                        dec->out.def.nBufferCountActual = bufInfo.numberOfBuffers + dec->nGuardSize;
                    }

                    dec->out.def.nBufferCountMin = bufInfo.numberOfBuffers;
                    dec->out.def.nBufferSize = bufInfo.bufferSize;

                    if (dec->portReconfigPending == OMX_TRUE)
                        dec->callbacks.EventHandler(dec->self, dec->appdata,
                                                    OMX_EventPortSettingsChanged, PORT_INDEX_OUTPUT,
                                                    0, NULL);
                    break;
                }

                for (i=0; i<dec->out.def.nBufferCountActual; ++i)
                {
                    BUFFER *buffer = NULL;
                    CODEC_STATE ret = CODEC_ERROR_UNSPECIFIED;

                    err = HantroOmx_port_lock_buffers(&dec->out);
                    if (err != OMX_ErrorNone)
                    {
                        DBGT_CRITICAL("HantroOmx_port_lock_buffers(in)");
                        DBGT_EPILOG("");
                        return err;
                    }
                    HantroOmx_port_get_buffer_at(&dec->out, &buffer, i);
                    HantroOmx_port_unlock_buffers(&dec->out);

                    // Wait for output buffers if not available
                    while (buffer == NULL)
                    {
                        DBGT_PDEBUG("Waiting output buffer");
                        OSAL_ThreadSleep(RETRY_INTERVAL);

                        err = HantroOmx_port_lock_buffers(&dec->out);
                        if (err != OMX_ErrorNone)
                        {
                            DBGT_CRITICAL("HantroOmx_port_lock_buffers(out) (err=%x)", err);
                            DBGT_EPILOG("");
                            return err;
                        }

                        HantroOmx_port_get_buffer_at(&dec->out, &buffer, i);
                        HantroOmx_port_unlock_buffers(&dec->out);

                        if (dec->releaseBuffers)
                            return OMX_ErrorNone;
                    }

                    if (buffer && (buffer->bus_address != 0))
                        dec->outputBufList[i] = buffer->bus_address;
                    else
                    {
                        DBGT_CRITICAL("No output buffers available");
                        return OMX_ErrorInsufficientResources;
                    }
                    DBGT_PDEBUG("Found buffer %d: %p\n  bus_data %p bus_address %lu", (int)i, buffer, buffer->bus_data, buffer->bus_address);

                    if (!dec->ReallocBufferAddress)
                    {
                        ret = dec->codec->setframebuffer(dec->codec, buffer, dec->out.def.nBufferCountMin);

                        if (ret == CODEC_ERROR_NOT_ENOUGH_FRAME_BUFFERS ||
                            ret == CODEC_ERROR_BUFFER_SIZE)
                        {
                            dec->shared_data.EOS = OMX_TRUE;
                            DBGT_EPILOG("");
                            return OMX_ErrorInsufficientResources;
                        }
                    }
                    /* Vp9 dynamic port reconfig: add one realloced buffer into decoder */
                    else if(dec->ReallocBufferAddress)
                    {
                        OMX_U32 x;
                        for (x = 0; x < 34; x++)
                        {
                            if (dec->outputBufListPrev[x] == buffer->bus_address)
                                break;
                        }

                        if (x == 34 || buffer->bus_address == dec->ReallocBufferAddress)
                            ret = dec->codec->setframebuffer(dec->codec, buffer, dec->out.def.nBufferCountMin);
                    }
                }
                dec->ReallocBufferAddress = 0;
                dec->checkExtraBuffers = OMX_TRUE;
            }
            break;
#endif
        case CODEC_PIC_SKIPPED:
#if 0
            DBGT_PDEBUG("Picture skipped -> flush time stamp from buffer");
            OMX_TICKS nTimeStamp;
            pop_timestamp(dec, &nTimeStamp);
#endif
            break;
        case CODEC_ERROR_STREAM:
            /* Stream error occured, force frame output
             * This is because in conformance tests the EOS flag
             * is forced and stream ends prematurely. */
            if (strcmp((char*)dec->role, "image_decoder.jpeg") != 0)
            {
                frm.size = dec->out.def.nBufferSize;
                async_dispatch_frame_buffer(dec, EOS, buff, &frm);
            }
            break;
        case CODEC_ERROR_STREAM_NOT_SUPPORTED:
#ifdef SKIP_NOT_SUPPORTED_STREAM
            break;
#else
            DBGT_ERROR("Stream not supported (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorNotImplemented;
#endif
        case CODEC_ERROR_FORMAT_NOT_SUPPORTED:
            DBGT_ERROR("Format not supported (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorNotImplemented;
        case CODEC_ERROR_INVALID_ARGUMENT:
            DBGT_ERROR("Invalid argument (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorBadParameter;
        case CODEC_ERROR_HW_TIMEOUT:
            DBGT_ERROR("HW timeout (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorTimeout;
        case CODEC_ERROR_HW_BUS_ERROR:
            DBGT_ERROR("HW bus error (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorHardware;
        case CODEC_ERROR_SYS:
            DBGT_ERROR("HW system error (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorHardware;
        case CODEC_ERROR_MEMFAIL:
            DBGT_ERROR("HW memory error (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorHardware;
        case CODEC_ERROR_NOT_INITIALIZED:
            DBGT_ERROR("HW not initialized (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorHardware;
        case CODEC_ERROR_HW_RESERVED:
            DBGT_ERROR("Unable to reserve HW (dec->codec->decode)");
            DBGT_EPILOG("");
            return OMX_ErrorHardware;
        default:
            DBGT_CRITICAL("ASYNC: unexpected codec return value: %d",
                        codec);
            DBGT_EPILOG("");
            return OMX_ErrorUndefined;
        }
        if (bytes > 0)
        {
            // ugh, crop consumed bytes from the buffer, maintain alignment
            OMX_U32 rem = datalen - bytes - first;

            //memmove(bus_data, bus_data + first + bytes, rem);
            bus_data += first + bytes;
            bus_address += first + bytes;
            datalen -= (bytes + first);
        }
        if (dobreak)
            break;
    }

#ifdef IS_G1_DECODER
bail:
#endif
    *retlen = datalen;
    if (EOS == OMX_TRUE && !dec->portReconfigPending)
    {
#ifndef OMX_DECODER_IMAGE_DOMAIN
        // flush decoder output buffers
        if (1/*strcmp((char*)dec->role, "video_decoder.jpeg") != 0*/)
        {
            DBGT_PDEBUG("ASYNC: Flush video decoder output buffers");
#ifdef IS_G2_DECODER
            if(!dec->shared_data.EOS)
            {
                dec->codec->endofstream(dec->codec);
                //dec->shared_data.EOS = OMX_TRUE;
            }
#else
            if ((dec->useExternalAlloc == OMX_TRUE) && !dec->shared_data.EOS)
            {
                dec->codec->endofstream(dec->codec);
                //dec->shared_data.EOS = OMX_TRUE;
            }
            else
            {
                err = async_get_frames(dec, 1, buff);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("ASYNC: EOS get frames err: %d", err);
                }
            }
#endif // IS_G2_DECODER
        }
#endif

#ifdef IS_G1_DECODER
        if (dec->useExternalAlloc == OMX_FALSE)
        {
            async_dispatch_last_frame(dec);
            DBGT_PDEBUG("ASYNC: sending OMX_EventBufferFlag");
            dec->callbacks.EventHandler(dec->self, dec->appdata,
                                        OMX_EventBufferFlag, PORT_INDEX_OUTPUT,
                                        buff->header->nFlags, NULL);
        }
#endif
    }

    if (err == OMX_ErrorOverflow)
    {
        DBGT_CRITICAL("ASYNC: firing OMX_EventErrorOverflow");
        dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError,
                                    err, 0, NULL);
    }
    DBGT_EPILOG("");
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE async_decoder_decode(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    if (dec->releaseBuffers)
    {
        DBGT_PDEBUG("ASYNC: releaseBuffers -> return");
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    DBGT_ASSERT(dec->codec);

    // grab the next input buffer and decode data out of it
    BUFFER *buff = NULL;

    OMX_ERRORTYPE err = OMX_ErrorNone;

    err = HantroOmx_port_lock_buffers(&dec->in);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("HantroOmx_port_lock_buffers(in)");
        goto INVALID_STATE;
    }

    HantroOmx_port_get_buffer(&dec->in, &buff);
    dec->inBufferIsUpdated = 1;
    // Read input buffer physical address from buffer private
#ifdef USE_ALLOC_PRIVATE
    DBGT_ASSERT(buff->header->pInputPortPrivate);
    ALLOC_PRIVATE *bufPrivate = (ALLOC_PRIVATE*) buff->header->pInputPortPrivate;
    DBGT_PDEBUG("allocPrivate %p %llu", bufPrivate->pBufferData, bufPrivate->nBusAddress);
    DBGT_ASSERT(buff->bus_data == bufPrivate->pBufferData);
    buff->bus_address = bufPrivate->nBusAddress;
#endif
    HantroOmx_port_unlock_buffers(&dec->in);
    if (!buff)
    {
        DBGT_PDEBUG("ASYNC: No buffer");
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }
#ifdef OMX_DECODER_IMAGE_DOMAIN
    if (buff->header->nFilledLen == 0)
    {
        // this hack is because in the BufferFlag test the component needs
        // to output a buffer and propage the EOS flag. However without
        // getting an input EOS there's never going to be any output, since
        // the input EOS is used to indicate the end of stream and that we have
        // a complete JPEG decoding unit in the buffer.
        buff->header->nFlags |= OMX_BUFFERFLAG_EOS;
        DBGT_PDEBUG("ASYNC: faking EOS flag!");
    }
#endif

    if (buff->header->nFlags & OMX_BUFFERFLAG_EXTRADATA)
    {
        OMX_OTHER_EXTRADATATYPE * segDataBuffer;
        DBGT_PDEBUG("ASYNC: Extracting EXTRA data from buffer");

        segDataBuffer = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U32)buff->header->pBuffer + buff->header->nFilledLen +3)& ~3);

        dec->sliceInfoNum = segDataBuffer->nDataSize/8;
        dec->pSliceInfo = segDataBuffer->data;
    }

    if (dec->codecstate == CODEC_HAS_FRAME)
    {
        DBGT_PDEBUG("Has frame");
        OMX_BOOL EOS = buff->header->nFlags & OMX_BUFFERFLAG_EOS;

        err = async_get_frames(dec, EOS, buff);
        if (err == OMX_ErrorOverflow)
        {
            DBGT_CRITICAL("ASYNC: firing OMX_EventErrorOverflow");
            dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError,
                                        OMX_ErrorOverflow, 0, NULL);
            DBGT_EPILOG("");
            return OMX_ErrorNone;
        }
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("async_get_frames (err=%x)", err);
            goto INVALID_STATE;
        }

        dec->codecstate = CODEC_OK;
    }

    DBGT_ASSERT(buff);
    DBGT_ASSERT(buff->header->nInputPortIndex == 0 &&
           "post processor input buffer in wrong place");

    FRAME_BUFFER *temp = &dec->frame_in;

    // if there is previous data in the frame buffer left over from a previous call to decode
    // or if the buffer has possibly misaligned offset or if the buffer is allocated by the client
    // invoke the decoding through the temporary frame buffer
    if (temp->size != 0 || buff->header->nOffset != 0 ||
       (!(buff->flags & BUFFER_FLAG_MY_BUFFER) && !(buff->flags & BUFFER_FLAG_EXT_ALLOC)))
    {
        DBGT_PDEBUG("ASYNC: Using temp input buffer");
        DBGT_PDEBUG("inputBuffer->header->nOffset %d", (int)buff->header->nOffset);
        DBGT_PDEBUG("tempBuffer->size %d", (int)temp->size);
        DBGT_PDEBUG("Input buffer flag 'My buffer': %s", (buff->flags & BUFFER_FLAG_MY_BUFFER) ?
                    "YES" : "NO");

        OMX_PARAM_PORTDEFINITIONTYPE *port = NULL;
        port = &dec->in.def;

        if (temp->size == 0 || port->format.image.eCompressionFormat == OMX_IMAGE_CodingJPEG)
        {
            OMX_U32 capacity = temp->capacity;

            OMX_U32 needed = buff->header->nFilledLen;
            while(1)
            {
                OMX_U32 available = capacity - temp->size;

                if (available > needed)
                    break;
                capacity *= 2;
            }
            if (capacity != temp->capacity)
            {
                err = grow_frame_buffer(dec, temp, capacity);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("grow_frame_buffer (err=%x)", err);
                    goto INVALID_STATE;
                }
            }
            DBGT_ASSERT(temp->capacity - temp->size >= buff->header->nFilledLen);

            // copy input buffer data into the temporary input buffer
            OMX_U32 len = buff->header->nFilledLen;
            OMX_U8 *src = buff->header->pBuffer + buff->header->nOffset;
            OMX_U8 *dst = temp->bus_data + temp->size;  // append to the buffer

            memcpy(dst, src, len);
            temp->size += len;
        }
        else
        {
            DBGT_PDEBUG("ASYNC: Temp buffer has data: %d bytes. Skip copying next buffer", (int)temp->size);
        }

        OMX_U32 retlen = 0;

        // decode data as much as we can
        err =
            async_decode_data(dec, temp->bus_data, temp->bus_address,
                              temp->size, buff, temp->capacity, &retlen);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("async_decode_data (err=%x)", err);
            goto INVALID_STATE;
        }

        temp->size = retlen;

        if (dec->portReconfigPending && (retlen > 0))
        {
            DBGT_PDEBUG("ASYNC: portReconfigPending. Postpone sending output buffer.");
            DBGT_EPILOG("");
            return OMX_ErrorNone;
        }
    }
    else
    {
        // take input directly from the input buffer
        DBGT_ASSERT(buff->header->nOffset == 0);
        DBGT_ASSERT(buff->bus_data == buff->header->pBuffer);
        DBGT_ASSERT(buff->flags & BUFFER_FLAG_MY_BUFFER || (buff->flags & BUFFER_FLAG_EXT_ALLOC));

        OMX_U8 *bus_data = buff->bus_data;

        OSAL_BUS_WIDTH bus_address = buff->bus_address;

        OMX_U32 filledlen = buff->header->nFilledLen;

        OMX_U32 retlen = 0;

        // decode data as much as we can
        err =
            async_decode_data(dec, bus_data, bus_address, filledlen, buff,
                              buff->allocsize, &retlen);
        if (err != OMX_ErrorNone)
        {
            DBGT_CRITICAL("async_decode_data (err=%x)", err);
            goto INVALID_STATE;
        }


        // if some data was left over (implying partial decoding units)
        // copy this data into the temporary buffer, which should be handled
        // before returning portReconfigPending.
        if (retlen)
        {
            OMX_U32 capacity = temp->capacity;

            OMX_U32 needed = retlen;
            while(1)
            {
                OMX_U32 available = capacity - temp->size;

                if (available > needed)
                    break;
                capacity *= 2;
            }
            if (capacity != temp->capacity)
            {
                err = grow_frame_buffer(dec, temp, capacity);
                if (err != OMX_ErrorNone)
                {
                    DBGT_CRITICAL("grow_frame_buffer (err=%x)", err);
                    goto INVALID_STATE;
                }
            }
            DBGT_ASSERT(temp->capacity - temp->size >= retlen);

            OMX_U8 *dst = temp->bus_data + temp->size;

            OMX_U8 *src = bus_data;

            memcpy(dst, src, retlen);
            temp->size += retlen;
        }

        if(dec->portReconfigPending)
        {
            DBGT_PDEBUG("ASYNC: portReconfigPending. Postpone sending output buffer.");
            DBGT_EPILOG("");
            return OMX_ErrorNone;
        }
    }

    // A component generates the OMX_EventMark event when it receives a marked buffer.
    // When a component receives a buffer, it shall compare its own pointer to the
    // pMarkTargetComponent field contained in the buffer. If the pointers match, then the
    // component shall send a mark event including pMarkData as a parameter, immediately
    // after the component has finished processing the buffer.

    // note that this *needs* to be before the emptybuffer done callback because the tester
    // clears the mark fields.
#ifdef IS_G2_DECODER
    if (dec->prevBuffer)
    {
        BUFFER *tmpBuffer = dec->prevBuffer;
        if (tmpBuffer->header->hMarkTargetComponent == dec->self)
        {
            dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventMark, 0,
                                        0, tmpBuffer->header->pMarkData);
            tmpBuffer->header->hMarkTargetComponent = NULL;
            tmpBuffer->header->pMarkData = NULL;
        }
        if (HantroOmx_port_is_tunneled(&dec->in))
        {
            ((OMX_COMPONENTTYPE *) dec->in.tunnelcomp)->FillThisBuffer(dec->in.
                                                                       tunnelcomp,
                                                                       tmpBuffer->
                                                                       header);
        }
        else
        {
            DBGT_PDEBUG("!!! EmptyBufferDone callback, buffer: %p", tmpBuffer);
            dec->callbacks.EmptyBufferDone(dec->self, dec->appdata, tmpBuffer->header);
        }
    }
#else
    if (dec->useExternalAlloc == OMX_TRUE)
    {
        if (dec->prevBuffer)
        {
            BUFFER *tmpBuffer = dec->prevBuffer;
            if (tmpBuffer->header->hMarkTargetComponent == dec->self)
            {
                dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventMark, 0,
                                            0, tmpBuffer->header->pMarkData);
                tmpBuffer->header->hMarkTargetComponent = NULL;
                tmpBuffer->header->pMarkData = NULL;
            }
            if (HantroOmx_port_is_tunneled(&dec->in))
            {
                ((OMX_COMPONENTTYPE *) dec->in.tunnelcomp)->FillThisBuffer(dec->in.
                                                                       tunnelcomp,
                                                                       tmpBuffer->
                                                                       header);
            }
            else
            {
                DBGT_PDEBUG("!!! EmptyBufferDone callback, buffer: %p", tmpBuffer);
                dec->callbacks.EmptyBufferDone(dec->self, dec->appdata, tmpBuffer->header);
            }
        }
    }
    else
    {
        if (buff->header->hMarkTargetComponent == dec->self)
        {
            dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventMark, 0,
                                        0, buff->header->pMarkData);
            buff->header->hMarkTargetComponent = NULL;
            buff->header->pMarkData = NULL;
        }
        if (HantroOmx_port_is_tunneled(&dec->in))
        {
            ((OMX_COMPONENTTYPE *) dec->in.tunnelcomp)->FillThisBuffer(dec->in.
                                                                        tunnelcomp,
                                                                        buff->
                                                                        header);
        }
        else
        {
            DBGT_PDEBUG("!!! EmptyBufferDone callback, buffer: %p", buff);
            dec->callbacks.EmptyBufferDone(dec->self, dec->appdata, buff->header);
        }
    }
#endif

    HantroOmx_port_lock_buffers(&dec->in);
    HantroOmx_port_pop_buffer(&dec->in);
    HantroOmx_port_unlock_buffers(&dec->in);

#ifdef IS_G2_DECODER
    dec->prevBuffer = buff;
#else
    if (dec->useExternalAlloc == OMX_TRUE)
    {
        dec->prevBuffer = buff;
    }
#endif

    DBGT_EPILOG("");
    return OMX_ErrorNone;

  INVALID_STATE:
    DBGT_ASSERT(err != OMX_ErrorNone);
    DBGT_CRITICAL("ASYNC: error while processing buffers: %s",
                HantroOmx_str_omx_err(err));
    if (buff != NULL)
    {
        DBGT_PDEBUG("ASYNC: current buffer: %p", buff);
    }
    dec->state = OMX_StateInvalid;
    dec->run = OMX_FALSE;

    if (dec->codec)
    {
        dec->codec->abort(dec->codec);
#ifdef IS_G2_DECODER
        if (dec->shared_data.output_thread_)
        {
            DBGT_PDEBUG("Join output thread");
            pthread_join(dec->shared_data.output_thread_, NULL);
            dec->shared_data.output_thread_run = OMX_FALSE;
            dec->shared_data.output_thread_ = 0;
        }
#endif
#ifdef IS_G1_DECODER
        if (dec->useExternalAlloc == OMX_TRUE)
        {
            if (dec->shared_data.output_thread_)
            {
                DBGT_PDEBUG("Join output thread");
                pthread_join(dec->shared_data.output_thread_, NULL);
                dec->shared_data.output_thread_run = OMX_FALSE;
                dec->shared_data.output_thread_ = 0;
            }
        }
#endif
        dec->codec->abortafter(dec->codec);
        dec->shared_data.EOS = OMX_TRUE;
    }

    dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError, err, 0,
                                NULL);
    DBGT_EPILOG("");
    return err;
}

static OMX_ERRORTYPE async_decoder_set_mask(OMX_DECODER * dec)
{
    CALLSTACK;
    DBGT_PROLOG("");

    BUFFER *buff = NULL;

    OMX_ERRORTYPE err = OMX_ErrorNone;

    err = HantroOmx_port_lock_buffers(&dec->inpp);
    if (err != OMX_ErrorNone)
    {
        DBGT_CRITICAL("HantroOmx_port_lock_buffers (err=%x)", err);
        goto INVALID_STATE;
    }

    HantroOmx_port_get_buffer(&dec->inpp, &buff);
    HantroOmx_port_unlock_buffers(&dec->inpp);
    if (!buff)
    {
        DBGT_EPILOG("");
        return OMX_ErrorNone;
    }

    DBGT_ASSERT(dec->mask.bus_data);
    DBGT_ASSERT(dec->mask.bus_address);

    if (buff->header->nFilledLen != dec->mask.capacity)
    {
        // note: should one generate some kind of an error
        // if there's less data provided than what has been specified
        // in the port dimensions??
        DBGT_PDEBUG("ASYNC: alpha blend mask buffer data length does not match expected data length nFilledLen: %d expected: %d",
                    (int) buff->header->nFilledLen, (int) dec->mask.capacity);
    }

    OMX_U32 minbytes =
        buff->header->nFilledLen <
        dec->mask.capacity ? buff->header->nFilledLen : dec->mask.capacity;

    DBGT_ASSERT(dec->mask.bus_data);
    //DBGT_ASSERT(dec->mask.capacity >= buff->header->nFilledLen);
    // copy the mask data into the internal buffer
    memcpy(dec->mask.bus_data, buff->header->pBuffer, minbytes);
    //memcpy(dec->mask.bus_data, buff->header->pBuffer, buff->header->nFilledLen);

    if (buff->header->hMarkTargetComponent == dec->self)
    {
        dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventMark, 0,
                                    0, buff->header->pMarkData);
        buff->header->pMarkData = NULL;
        buff->header->hMarkTargetComponent = NULL;
    }
    if (HantroOmx_port_is_tunneled(&dec->inpp))
    {
        ((OMX_COMPONENTTYPE *) dec->inpp.tunnelcomp)->FillThisBuffer(dec->inpp.
                                                                     tunnelcomp,
                                                                     buff->
                                                                     header);
    }
    else
    {
        dec->callbacks.EmptyBufferDone(dec->self, dec->appdata, buff->header);
    }

    HantroOmx_port_lock_buffers(&dec->inpp);
    HantroOmx_port_pop_buffer(&dec->inpp);
    HantroOmx_port_unlock_buffers(&dec->inpp);
    DBGT_EPILOG("");
    return OMX_ErrorNone;

  INVALID_STATE:
    DBGT_ASSERT(err != OMX_ErrorNone);
    DBGT_CRITICAL("ASYNC: error while processing buffer: %s",
                HantroOmx_str_omx_err(err));
    dec->state = OMX_StateInvalid;
    dec->run = OMX_FALSE;
    dec->callbacks.EventHandler(dec->self, dec->appdata, OMX_EventError, err, 0,
                                NULL);
    DBGT_EPILOG("");
    return err;
}

static void *output_thread(void *arg)
{
    DBGT_PROLOG("");
    DBGT_ASSERT(arg);
    CODEC_STATE state;

    SHARED_DATA* shared_data = (SHARED_DATA*) arg;
    OMX_DECODER *dec = (OMX_DECODER *) shared_data->decInstance;
    OMX_U32 pic_display_number = 1;
    shared_data->hasFrame = OMX_FALSE;

    while (shared_data->output_thread_run && !dec->releaseBuffers)
    {
        FRAME frm;
        memset(&frm, 0, sizeof(FRAME));
//        shared_data->hasFrame = OMX_FALSE;
        DBGT_PDEBUG("shared_data->EOS: %d", shared_data->EOS);

#ifdef USE_EXTERNAL_BUFFER
        state = dec->codec->getframe(dec->codec, &frm, shared_data->EOS);

        // For StateChange (Exec->Idle) and FLUSH command,
        // output thread should be aborted.
        if(state == CODEC_ABORTED)
        {
            OSAL_MutexLock(dec->threadmutex);
            shared_data->hasFrame = OMX_FALSE;
            OSAL_MutexUnlock(dec->threadmutex);
            shared_data->output_thread_run = OMX_FALSE;
            DBGT_EPILOG("");
            return NULL;
        }
        // No more frames to be displayed, the dpb in decoder has been flushed.
        else if(state == CODEC_FLUSHED)
        {
            OSAL_MutexLock(dec->threadmutex);
            shared_data->hasFrame = OMX_FALSE;
            OSAL_MutexUnlock(dec->threadmutex);
            DBGT_EPILOG("");
            continue;
        }

        if(state == CODEC_HAS_FRAME)
        {
            OSAL_MutexLock(dec->threadmutex);
            shared_data->hasFrame = OMX_TRUE;
            OSAL_MutexUnlock(dec->threadmutex);
            pic_display_number++;
        }
#else
        FRAME tmpFrm;
        memset(&tmpFrm, 0, sizeof(FRAME));

        state = dec->codec->getframe(dec->codec, &tmpFrm, shared_data->EOS);

        if(state == CODEC_HAS_FRAME)
        {
            BUFFER buff;
            OMX_ERRORTYPE err = OMX_ErrorNone;
            OSAL_MutexLock(dec->threadmutex);
            shared_data->hasFrame = OMX_TRUE;
            OSAL_MutexUnlock(dec->threadmutex);
            pic_display_number++;

            err = async_get_frame_buffer(dec, &frm);

            if (err == OMX_ErrorNoMore)
            {
                DBGT_CRITICAL("async_get_frame_buffer OMX_ErrorNoMore");
            }
            if (err != OMX_ErrorNone)
            {
                DBGT_CRITICAL("async_get_frame_buffer (err=%x)", err);
                DBGT_EPILOG("");
                pthread_exit(NULL);
            }
            // Copy output data from decoder's internal buffer to OMX buffer
            memcpy(frm.fb_bus_data, tmpFrm.outBufPrivate.pLumaBase, tmpFrm.outBufPrivate.nLumaSize);
            memcpy(frm.fb_bus_data + tmpFrm.outBufPrivate.nLumaSize, tmpFrm.outBufPrivate.pChromaBase, tmpFrm.outBufPrivate.nChromaSize);
            // Fill OUTPUT_BUFFER_PRIVATE
            frm.outBufPrivate.pLumaBase = frm.fb_bus_data;
            frm.outBufPrivate.nLumaBusAddress = frm.fb_bus_address;
            frm.outBufPrivate.nLumaSize = tmpFrm.outBufPrivate.nLumaSize;
            frm.outBufPrivate.pChromaBase = frm.fb_bus_data + tmpFrm.outBufPrivate.nLumaSize;
            frm.outBufPrivate.nChromaBusAddress = frm.fb_bus_address + tmpFrm.outBufPrivate.nLumaSize;
            frm.outBufPrivate.nChromaSize = tmpFrm.outBufPrivate.nChromaSize;
            // RFC table data points to decoder's internal mememory when USE_EXTERNAL_BUFFER is disabled.
            memcpy(&frm.outBufPrivate.sRfcTable, &tmpFrm.outBufPrivate.sRfcTable, sizeof(RFC_TABLE));
            frm.size = tmpFrm.size;

            buff.bus_address = tmpFrm.fb_bus_address;
            buff.bus_data = tmpFrm.fb_bus_data;

            dec->codec->pictureconsumed(dec->codec, &buff);
        }
#endif

        if (state < 0)
        {
            if (dec->state == OMX_StateInvalid)
            {
                DBGT_CRITICAL("ASYNC: getframe error:%d", state);
                OSAL_MutexLock(dec->threadmutex);
                shared_data->hasFrame = OMX_FALSE;
                OSAL_MutexUnlock(dec->threadmutex);
                shared_data->output_thread_run = OMX_FALSE;
                dec->shared_data.EOS = OMX_TRUE;
                return NULL;
            }

            // Sometimes the decoder instance has not been built after output created(VC1)
            OSAL_ThreadSleep(RETRY_INTERVAL);
        }
        if (frm.size && !dec->releaseBuffers)
        {
            BUFFER *tmp_buffer;

            OSAL_MutexLock(dec->threadmutex);
            tmp_buffer = shared_data->inbuff;
            OSAL_MutexUnlock(dec->threadmutex);
            //async_dispatch_frame_buffer(dec, OMX_FALSE, shared_data->inbuff, &frm);
            async_dispatch_frame_buffer(dec, OMX_FALSE, tmp_buffer, &frm);
        }
        if (state == CODEC_END_OF_STREAM)
        {
            BUFFER *buff;
            DBGT_PDEBUG("END-OF-STREAM received in output thread");

            if (!dec->releaseBuffers)
            {
                HantroOmx_port_lock_buffers(&dec->out);
                HantroOmx_port_get_buffer(&dec->out, &buff);
                HantroOmx_port_unlock_buffers(&dec->out);

                while (buff == NULL && !dec->releaseBuffers)
                {
                    OSAL_ThreadSleep(RETRY_INTERVAL);
                    HantroOmx_port_lock_buffers(&dec->out);
                    HantroOmx_port_get_buffer(&dec->out, &buff);
                    HantroOmx_port_unlock_buffers(&dec->out);
                }

                if(!dec->releaseBuffers)
                {
                    buff->header->nFlags |= OMX_BUFFERFLAG_EOS;
                    buff->header->nFilledLen = 0;
                    buff->header->pOutputPortPrivate = NULL;
#ifdef OMX_SKIP64BIT
                    buff->header->nTimeStamp.nLowPart = buff->header->nTimeStamp.nHighPart = 0;
#else
                    buff->header->nTimeStamp = 0;
#endif
                    OMX_U32 nFlags = buff->header->nFlags;

                    shared_data->eos_bus_address = buff->bus_address;

                    if(HantroOmx_port_is_tunneled(&dec->out))
                    {
                        ((OMX_COMPONENTTYPE *) dec->out.tunnelcomp)->EmptyThisBuffer(dec->out.
                                                                                 tunnelcomp,
                                                                                 buff->header);
                    }
                    else
                    {
                        dec->callbacks.FillBufferDone(dec->self, dec->appdata,
                                                      buff->header);
                    }

                    HantroOmx_port_lock_buffers(&dec->out);
                    HantroOmx_port_pop_buffer(&dec->out);
                    HantroOmx_port_unlock_buffers(&dec->out);

                    //async_dispatch_last_frame(dec);
                    //shared_data->inbuff->header->nFlags |= OMX_BUFFERFLAG_EOS;
                    DBGT_PDEBUG("ASYNC: sending OMX_EventBufferFlag");
                    dec->callbacks.EventHandler(dec->self, dec->appdata,
                                            OMX_EventBufferFlag, PORT_INDEX_OUTPUT,
                                            nFlags, NULL);

                    OSAL_MutexLock(dec->threadmutex);
                    shared_data->EOS = OMX_TRUE;
                    OSAL_MutexUnlock(dec->threadmutex);
                }
            }

            shared_data->output_thread_run = OMX_FALSE;
            OSAL_MutexLock(dec->threadmutex);
            shared_data->hasFrame = OMX_FALSE;
            OSAL_MutexUnlock(dec->threadmutex);
            DBGT_EPILOG("");
            return NULL;
        }
    }

    shared_data->output_thread_run = OMX_FALSE;
    OSAL_MutexLock(dec->threadmutex);
    shared_data->hasFrame = OMX_FALSE;
    OSAL_MutexUnlock(dec->threadmutex);
    DBGT_EPILOG("");
    return NULL;
}
