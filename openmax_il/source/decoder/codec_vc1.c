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

#include <string.h>
#include "OSAL.h"
#include "codec_vc1.h"
#include "post_processor.h"
#include "vc1decapi.h"
#ifdef ENABLE_PP
#include "ppapi.h"
#endif
#include "util.h"
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX VC1"
#define MAX_BUFFERS 16

/* number of frame buffers decoder should allocate 0=AUTO */
//#define FRAME_BUFFERS 3
#define FRAME_BUFFERS 0

#define SHOW4(p) (p[0]) | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p+=4;
#define ALIGN(offset, align)   ((((unsigned int)offset) + align-1) & ~(align-1))
//#define ALIGN(offset, align)    offset

typedef enum VC1_CODEC_STATE
{
    VC1_PARSE_METADATA,
    VC1_DECODE
} VC1_CODEC_STATE;

typedef struct CODEC_VC1
{
    CODEC_PROTOTYPE base;

    VC1DecInst instance;
    VC1DecMetaData metadata;
    VC1_CODEC_STATE state;
    OMX_U32 framesize;
    // needed post-proc params
    // multiRes
    // rangeRed
    OMX_U32 picId;
#ifdef ENABLE_PP
    PPConfig pp_config;
    PPInst pp_instance;
    PP_TRANSFORMS transforms;
#endif
    PP_STATE pp_state;
    OMX_BOOL pipeline_enabled;
    OMX_BOOL update_pp_out;
    OMX_BOOL resolution_changed;
    OMX_U32 codedWidth;
    OMX_U32 codedHeight;
    OMX_U32 out_count;
    OMX_U32 out_index_r;
    OMX_U32 out_index_w;
    OMX_U32 out_num;
    VC1DecPicture out_pic[MAX_BUFFERS];
    const void *DWLInstance;
    OMX_BOOL interlacedSequence;
    enum DecDpbFlags dpbFlags;
    //OMX_BOOL extraEosLoopDone;
    OMX_BOOL bEnableAdaptiveBuffers;
    OMX_U32 nGuardSize;
    OMX_HANDLETYPE inst_create_event;
    OMX_U32 abort;
} CODEC_VC1;

CODEC_STATE decoder_setframebuffer_vc1(CODEC_PROTOTYPE * arg, BUFFER *buff,
                                        OMX_U32 available_buffers);
CODEC_STATE decoder_pictureconsumed_vc1(CODEC_PROTOTYPE * arg, BUFFER *buff);
FRAME_BUFFER_INFO decoder_getframebufferinfo_vc1(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_abort_vc1(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_abortafter_vc1(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_setnoreorder_vc1(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder);
CODEC_STATE decoder_setinfo_vc1(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf);
// destroy codec instance
static void decoder_destroy_vc1(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VC1 *this = (CODEC_VC1 *) arg;

    if (this)
    {
        this->base.decode = 0;
        this->base.getframe = 0;
        this->base.getinfo = 0;
        this->base.destroy = 0;
        this->base.scanframe = 0;
        this->base.setppargs = 0;
        this->base.endofstream = 0;
        this->base.pictureconsumed = 0;
        this->base.setframebuffer = 0;
        this->base.getframebufferinfo = 0;
        this->base.abort = 0;
        this->base.abortafter = 0;
        this->base.setnoreorder = 0;
        this->base.setinfo = 0;
#ifdef ENABLE_PP
        if (this->pp_instance)
        {
            if (this->pipeline_enabled != OMX_FALSE)
            {
                HantroHwDecOmx_pp_pipeline_disable(this->pp_instance,
                                                   this->instance);
                this->pp_state = PP_DISABLED;
            }
            HantroHwDecOmx_pp_destroy(&this->pp_instance);
        }
#endif
        if (this->inst_create_event)
            OSAL_EventDestroy(this->inst_create_event);

        if (this->instance)
        {
            VC1DecRelease(this->instance);
            this->instance = 0;
        }

        OSAL_Free(this);
    }
    DBGT_EPILOG("");
}

// try to consume stream data
static CODEC_STATE decoder_decode_vc1(CODEC_PROTOTYPE * arg,
                                      STREAM_BUFFER * buf, OMX_U32 * consumed,
                                      FRAME * frame)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VC1 *this = (CODEC_VC1 *) arg;

    DBGT_ASSERT(this);
    DBGT_ASSERT(buf);
    DBGT_ASSERT(consumed);

    frame->size = 0;
    VC1DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;
    struct DecDownscaleCfg dscale_cfg;
    dscale_cfg.down_scale_x = 0;
    dscale_cfg.down_scale_y = 0;

    if (this->resolution_changed)
    {
        this->resolution_changed = OMX_FALSE;
#ifdef ENABLE_PP
        if (this->pp_state == PP_PIPELINE)
        {
            this->pp_config.pp_in_img.width = this->codedWidth;
            this->pp_config.pp_in_img.height = this->codedHeight;
            this->update_pp_out = OMX_TRUE;
        }
#endif
    }

    if (this->state == VC1_PARSE_METADATA)
    {
        DBGT_PDEBUG("Parse metadata");
        if ((buf->bus_data[0] == 0x00) && (buf->bus_data[1] == 0x00) &&
           (buf->bus_data[2] == 0x01))
        {
            DBGT_PDEBUG("Advanced profile");
            // is advanced profile
            this->metadata.profile = 8;

#ifdef IS_G1_DECODER
            ret = VC1DecInit(&this->instance,
#ifdef USE_EXTERNAL_BUFFER
                    this->DWLInstance,
#endif
                    &this->metadata,
                    ERROR_HANDLING, FRAME_BUFFERS,
                    this->dpbFlags,
                    this->bEnableAdaptiveBuffers,
                    this->nGuardSize,
                    &dscale_cfg);
#else
            ret = VC1DecInit(&this->instance, &this->metadata,
                    USE_VIDEO_FREEZE_CONCEALMENT, FRAME_BUFFERS);
#endif
            if (ret == VC1DEC_PARAM_ERROR)
            {
                DBGT_CRITICAL("VC1DecInit failed (err=%d)", ret);
                DBGT_EPILOG("");
                return CODEC_ERROR_INVALID_ARGUMENT;
            }

            this->state = VC1_DECODE;

            if (OSAL_EventSet(this->inst_create_event) != OMX_ErrorNone)
                return CODEC_ERROR_UNSPECIFIED;

#ifdef ENABLE_PP
            if (this->pp_state == PP_PIPELINE)
            {
                PPResult res;

                res =
                    HantroHwDecOmx_pp_pipeline_enable(this->pp_instance,
                                                      this->instance,
                                                      PP_PIPELINED_DEC_TYPE_VC1);
                if (res != PP_OK)
                {
                    DBGT_CRITICAL("HantroHwDecOmx_pp_pipeline_enable (err=%d)", res);
                    DBGT_EPILOG("");
                    return CODEC_ERROR_UNSPECIFIED;
                }
                DBGT_PDEBUG("PP_COMBINED_MODE");
                this->pipeline_enabled = OMX_TRUE;
            }
#endif
            *consumed = 0;
            DBGT_EPILOG("");
            return CODEC_NEED_MORE;
        }
        else
        {
            DBGT_PDEBUG("Main or simple profile");
            OMX_U8 *p = buf->bus_data + 12;

            this->metadata.max_coded_height = SHOW4(p);
            this->metadata.max_coded_width = SHOW4(p);
            if ((this->metadata.max_coded_height * this->metadata.max_coded_width)
                  > MAX_VIDEO_RESOLUTION)
            {
                DBGT_ERROR("Video stream resolution exceeds the supported video resolution");
                DBGT_EPILOG("");
                return CODEC_ERROR_STREAM_NOT_SUPPORTED;
            }
            ret = VC1DecUnpackMetaData(buf->bus_data + 8, 4, &this->metadata);

            if (ret == VC1DEC_PARAM_ERROR)
            {
                DBGT_CRITICAL("VC1DecUnpackMetaData VC1DEC_PARAM_ERROR");
                DBGT_EPILOG("");
                return CODEC_ERROR_INVALID_ARGUMENT;
            }
            else if (ret == VC1DEC_METADATA_FAIL)
            {
                DBGT_CRITICAL("VC1DecUnpackMetaData VC1DEC_METADATA_FAIL");
                DBGT_EPILOG("");
                *consumed = buf->streamlen;
                return CODEC_ERROR_STREAM;
            }
        }
        if (ret == VC1DEC_OK)
        {
#ifdef IS_G1_DECODER
            ret = VC1DecInit(&this->instance,
#ifdef USE_EXTERNAL_BUFFER
                    this->DWLInstance,
#endif
                    &this->metadata,
                    ERROR_HANDLING, FRAME_BUFFERS,
                    this->dpbFlags,
                    this->bEnableAdaptiveBuffers,
                    this->nGuardSize,
                    &dscale_cfg);
#else
            ret = VC1DecInit(&this->instance, &this->metadata,
                    USE_VIDEO_FREEZE_CONCEALMENT, FRAME_BUFFERS);
#endif
            if (ret == VC1DEC_PARAM_ERROR)
            {
                DBGT_CRITICAL("VC1DecInit failed VC1DEC_PARAM_ERROR");
                DBGT_EPILOG("");
                return CODEC_ERROR_INVALID_ARGUMENT;
            }

            this->state = VC1_DECODE;

            if (OSAL_EventSet(this->inst_create_event) != OMX_ErrorNone)
                return CODEC_ERROR_UNSPECIFIED;

#ifdef ENABLE_PP
            if (this->pp_state == PP_PIPELINE)
            {
                PPResult res;
                res =
                    HantroHwDecOmx_pp_pipeline_enable(this->pp_instance,
                                                      this->instance,
                                                      PP_PIPELINED_DEC_TYPE_VC1);
                if (res != PP_OK)
                {
                    DBGT_CRITICAL("HantroHwDecOmx_pp_pipeline_enable (err=%d)", res);
                    DBGT_EPILOG("");
                    return CODEC_ERROR_UNSPECIFIED;
                }
                DBGT_PDEBUG("PP_COMBINED_MODE");
                HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
                this->pipeline_enabled = OMX_TRUE;
            }
#endif
            if (this->metadata.profile != 8)
            {
                // RVC2
                if (buf->bus_data[3] & 0x40)
                    *consumed = 36;
                // RCV1
                else
                    *consumed = 20;
            }
            DBGT_EPILOG("");
            return CODEC_HAS_INFO;
        }
    }
    else if (this->state == VC1_DECODE)
    {
        VC1DecInput input;

        memset(&input, 0, sizeof(VC1DecInput));

        input.stream = buf->bus_data;
        input.stream_bus_address = buf->bus_address;
        input.stream_size = buf->streamlen;
        input.pic_id = buf->picId;
        input.skip_non_reference = 0;
        DBGT_PDEBUG("Pic id %d, stream length %d ", (int)this->picId, input.stream_size);
#ifdef ENABLE_PP
        if (this->pp_state == PP_PIPELINE && this->update_pp_out == OMX_TRUE)
        {
            HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
            PPResult res =
                HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config);
            this->update_pp_out = OMX_FALSE;
            if (res != PP_OK)
            {
                DBGT_CRITICAL("HantroHwDecOmx_pp_set failed (err=%d)", res);
                DBGT_EPILOG("");
                return CODEC_ERROR_INVALID_ARGUMENT;
            }
        }
#endif
        VC1DecOutput output;

        ret = VC1DecDecode(this->instance, &input, &output);

        switch (ret)
        {
        case VC1DEC_PIC_DECODED:
            this->picId++;
            stat = CODEC_HAS_FRAME;
            break;
        case VC1DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case VC1DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case VC1DEC_END_OF_SEQ:
            stat = CODEC_NEED_MORE; //??
            break;
        case VC1DEC_HDRS_RDY:
#ifdef ENABLE_PP
            if (this->pp_state == PP_PIPELINE)
            {
                HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
            }
#endif
            stat = CODEC_HAS_INFO;
            break;
        case VC1DEC_RESOLUTION_CHANGED:
            DBGT_PDEBUG("Resolution changed");
            this->resolution_changed = OMX_TRUE;
            VC1DecInfo info;
            VC1DecGetInfo(this->instance, &info);
            this->codedWidth = info.coded_width;
            this->codedHeight = info.coded_height;
            stat = CODEC_HAS_FRAME; // flush picture buffer
            break;
        case VC1DEC_STRM_PROCESSED:
            stat = CODEC_NEED_MORE;
            break;
        case VC1DEC_BUF_EMPTY:
            stat = CODEC_BUFFER_EMPTY;
            break;
        case VC1DEC_NONREF_PIC_SKIPPED:
            DBGT_PDEBUG("Nonreference picture skipped");
            output.data_left = 0;
            stat = CODEC_PIC_SKIPPED;
            break;
#ifdef USE_EXTERNAL_BUFFER
        case VC1DEC_WAITING_FOR_BUFFER:
            DBGT_PDEBUG("Waiting for frame buffer");
            VC1DecBufferInfo bufInfo;

            ret = VC1DecGetBufferInfo(this->instance, &bufInfo);
            DBGT_PDEBUG("Buffer size %d, number of buffers %d",
                bufInfo.next_buf_size, bufInfo.buf_num);
            stat = CODEC_WAITING_FRAME_BUFFER;

            // Reset output relatived parameters
            this->out_index_w = 0;
            this->out_index_r = 0;
            this->out_num = 0;
            memset(this->out_pic, 0, sizeof(VC1DecPicture)*MAX_BUFFERS);
            break;
#endif
#ifdef USE_OUTPUT_RELEASE
        case VC1DEC_ABORTED:
            DBGT_PDEBUG("Decoding aborted");
            *consumed = input.stream_size;
            DBGT_EPILOG("");
            return CODEC_ABORTED;
#endif
        case VC1DEC_NO_DECODING_BUFFER:
            stat = CODEC_NO_DECODING_BUFFER;
            break;
        case VC1DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case VC1DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case VC1DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case VC1DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case VC1DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case VC1DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case VC1DEC_FORMAT_NOT_SUPPORTED:
            stat = CODEC_ERROR_FORMAT_NOT_SUPPORTED;
            break;
        default:
            DBGT_ASSERT(!"unhandled VC1DecRet");
            break;
        }

        if (stat != CODEC_ERROR_UNSPECIFIED)
        {
            if (this->metadata.profile == 8)
                *consumed = input.stream_size - output.data_left;
            else
                *consumed = buf->streamlen;

            if (ret == VC1DEC_RESOLUTION_CHANGED)
                *consumed = 0;
#ifdef USE_EXTERNAL_BUFFER
            if (ret == VC1DEC_WAITING_FOR_BUFFER)
                *consumed = 0;
#endif
            if (ret == VC1DEC_NO_DECODING_BUFFER)
                *consumed = 0;
            return stat;
        }
    }
    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

// get stream info
static CODEC_STATE decoder_getinfo_vc1(CODEC_PROTOTYPE * arg, STREAM_INFO * pkg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VC1 *this = (CODEC_VC1 *) arg;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(pkg);

    pkg->isVc1Stream = OMX_TRUE;

    if (this->metadata.profile == 8) //advanced
    {
        VC1DecInfo info;

        memset(&info, 0, sizeof(VC1DecInfo));
        VC1DecRet ret = VC1DecGetInfo(this->instance, &info);

        switch (ret)
        {
        case VC1DEC_OK:
            break;
        case VC1DEC_PARAM_ERROR:
            DBGT_CRITICAL("VC1DEC_PARAM_ERROR");
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        default:
            DBGT_ASSERT(!"unhandled VC1DecRet");
            return CODEC_ERROR_UNSPECIFIED;
        }

        if ((info.max_coded_width * info.max_coded_height) > MAX_VIDEO_RESOLUTION)
        {
            DBGT_ERROR("Video stream resolution exceeds the supported video resolution");
            DBGT_EPILOG("");
            return CODEC_ERROR_STREAM_NOT_SUPPORTED;
        }
        if (info.interlaced_sequence)
        {
            DBGT_PDEBUG("Interlaced sequence");
            this->interlacedSequence = OMX_TRUE;
        }

        pkg->width = ALIGN(info.max_coded_width, 8);
        pkg->height = ALIGN(info.max_coded_height, 8);
        pkg->stride = ALIGN(info.max_coded_width, 8);
        pkg->sliceheight = ALIGN(info.max_coded_height, 8);
        pkg->framesize = pkg->width * pkg->height * 3 / 2;
        pkg->interlaced = info.interlaced_sequence;

        if (info.output_format == VC1DEC_TILED_YUV420)
            pkg->format = OMX_COLOR_FormatYUV420SemiPlanar8x4Tiled;
        else
            pkg->format = OMX_COLOR_FormatYUV420PackedSemiPlanar;

#ifdef SET_OUTPUT_CROP_RECT
        pkg->crop_available = OMX_FALSE;
        if ((pkg->width != info.coded_width) ||
            (pkg->height != info.coded_height))
        {
            pkg->crop_left = 0;
            pkg->crop_top = 0;
            pkg->crop_width = info.coded_width;
            pkg->crop_height = info.coded_height;
            pkg->crop_available = OMX_TRUE;
            DBGT_PDEBUG("Crop left %d, top %d, width %d, height %d", (int)pkg->crop_left,
                    (int)pkg->crop_top, (int)pkg->crop_width, (int)pkg->crop_height);
        }
#endif
    }
    else
    {
        VC1DecInfo info;

        memset(&info, 0, sizeof(VC1DecInfo));
        VC1DecRet ret = VC1DecGetInfo(this->instance, &info);

        switch (ret)
        {
        case VC1DEC_OK:
            break;
        case VC1DEC_PARAM_ERROR:
            DBGT_CRITICAL("VC1DEC_PARAM_ERROR");
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        default:
            DBGT_ASSERT(!"unhandled VC1DecRet");
            return CODEC_ERROR_UNSPECIFIED;
        }

        pkg->width = ALIGN(this->metadata.max_coded_width, 8);
        pkg->height = ALIGN(this->metadata.max_coded_height, 8);
        pkg->stride = ALIGN(this->metadata.max_coded_width, 8);
        pkg->sliceheight = ALIGN(this->metadata.max_coded_height, 8);
        pkg->framesize = pkg->width * pkg->height * 3 / 2;

        if (info.output_format == VC1DEC_TILED_YUV420)
            pkg->format = OMX_COLOR_FormatYUV420SemiPlanar8x4Tiled;
        else
            pkg->format = OMX_COLOR_FormatYUV420PackedSemiPlanar;
    }
#ifdef ENABLE_PP
    if (this->pp_state != PP_DISABLED)
    {
        if (this->transforms == PPTR_DEINTERLACE)
        {
            this->update_pp_out = OMX_TRUE;
        }
        HantroHwDecOmx_pp_set_info(&this->pp_config, pkg, this->transforms);
        PPResult res =
            HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config);
        if (res != PP_OK)
        {
            DBGT_CRITICAL("HantroHwDecOmx_pp_set failed (err=%d)", res);
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        }
        // prevent multiple rotations if VC1DEC_HDRS_RDY is returned more than once.
        // TODO: better solution
        this->transforms &= ~PPTR_ROTATE;
    }
#endif
#ifdef USE_EXTERNAL_BUFFER
        VC1DecBufferInfo bufInfo;

        VC1DecGetBufferInfo(this->instance, &bufInfo);
        pkg->framesize = bufInfo.next_buf_size;
        pkg->frame_buffers = bufInfo.buf_num;
        DBGT_PDEBUG("Frame size %lu", pkg->framesize);
#endif
    this->framesize = pkg->framesize;
    DBGT_EPILOG("");
    return CODEC_OK;
}

// get decoded frame
static CODEC_STATE decoder_getframe_vc1(CODEC_PROTOTYPE * arg, FRAME * frame,
                                        OMX_BOOL eos)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VC1 *this = (CODEC_VC1 *) arg;

    DBGT_ASSERT(this != 0);

#if 0
    // For VC1 the decoder instance may be not built yet before output thread created.
    while (this->instance == NULL && !this->abort)
    {
        OSAL_BOOL timeout;
        OSAL_EventWait(this->inst_create_event, INFINITE_WAIT, &timeout);
    }
#else
    if (this->instance == NULL)
      return CODEC_OK;
#endif

    if (this->instance)
    {
        if (OSAL_EventReset(this->inst_create_event) != OMX_ErrorNone)
            return CODEC_ERROR_UNSPECIFIED;
    }
    else
        return CODEC_ABORTED;

    DBGT_ASSERT(frame);

#ifdef ENABLE_PP
    if (this->resolution_changed) // flush picture buffer
        eos = 1;

    if (this->pp_state == PP_PIPELINE &&
       (eos || this->update_pp_out == OMX_TRUE))
    {
        DBGT_PDEBUG("change pp output, EOS: %d", eos);
        HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
        PPResult res =
            HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config);
        this->update_pp_out = OMX_FALSE;
        if (res != PP_OK)
        {
            DBGT_CRITICAL("HantroHwDecOmx_pp_set (err=%d)", res);
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        }
    }
#endif
    /* stream ended but we know there is picture to be outputted. We
     * have to loop one more time without "eos" and on next round
     * NextPicture is called with eos "force last output"
     */

    /*if (eos && this->extraEosLoopDone == OMX_FALSE)
    {
        this->extraEosLoopDone = OMX_TRUE;
        eos = OMX_FALSE;
    }*/

    VC1DecPicture picture;

    memset(&picture, 0, sizeof(VC1DecPicture));

    VC1DecRet ret = VC1DecNextPicture(this->instance, &picture, eos);

    switch (ret)
    {
    case VC1DEC_OK:
        return CODEC_OK;
    case VC1DEC_PIC_RDY:
        DBGT_ASSERT(this->framesize);

        DBGT_PDEBUG("end of stream %d", eos);
        DBGT_PDEBUG("err mbs %d", picture.number_of_err_mbs);
        DBGT_PDEBUG("Frame size %dx%d", picture.frame_width, picture.frame_height);
        DBGT_PDEBUG("Coded size %dx%d", picture.coded_width, picture.coded_height);
#ifdef ENABLE_PP
        if (this->pp_state != PP_DISABLED)
        {
            this->update_pp_out = OMX_TRUE;
            PPResult res = HantroHwDecOmx_pp_execute(this->pp_instance);

            if (res != PP_OK)
            {
                DBGT_CRITICAL("HantroHwDecOmx_pp_execute (err=%d)", res);
                DBGT_EPILOG("");
                return CODEC_ERROR_UNSPECIFIED;
            }
        }
        else
#endif
        {
            if (this->interlacedSequence && !picture.field_picture && picture.first_field)
            {
                DBGT_PDEBUG("First field -> return");
                frame->size = 0;
#ifdef USE_OUTPUT_RELEASE
                if (!picture.interlaced)
                {
                    //VC1DecRet ret = VC1DecPictureConsumed(this->instance, &picture);
                    //DBGT_PDEBUG("VC1DecPictureConsumed ret (%d)", ret);
                    //UNUSED_PARAMETER(ret);
                }
                DBGT_EPILOG("");
                return CODEC_OK; // do not send the first field of frame
#else
                DBGT_EPILOG("");
                return CODEC_HAS_FRAME; // do not send the first of field frame
#endif
            }

            if (this->interlacedSequence && picture.field_picture && picture.first_field)
            {
                DBGT_EPILOG("");
                return CODEC_HAS_FRAME;
            }

            this->framesize = picture.frame_width * picture.frame_height * 3 / 2;
#ifndef USE_EXTERNAL_BUFFER
            memcpy(frame->fb_bus_data, picture.output_picture, this->framesize);
#endif
        }
#ifdef USE_EXTERNAL_BUFFER
        frame->fb_bus_address = picture.output_picture_bus_address;
        frame->fb_bus_data = (u8*)picture.output_picture;
        frame->outBufPrivate.pLumaBase = (u8*)picture.output_picture;
        frame->outBufPrivate.nLumaBusAddress = picture.output_picture_bus_address;
        frame->outBufPrivate.nLumaSize = picture.frame_width * picture.frame_height;
        frame->outBufPrivate.pChromaBase = frame->outBufPrivate.pLumaBase + frame->outBufPrivate.nLumaSize;
        frame->outBufPrivate.nChromaBusAddress = frame->outBufPrivate.nLumaBusAddress + frame->outBufPrivate.nLumaSize;
        frame->outBufPrivate.nChromaSize = frame->outBufPrivate.nLumaSize / 2;
        frame->outBufPrivate.nFrameWidth = picture.coded_width;
        frame->outBufPrivate.nFrameHeight = picture.coded_height;
        frame->outBufPrivate.nPicId[0] = picture.decode_id[0];
        frame->outBufPrivate.nPicId[1] = picture.decode_id[1];
        DBGT_PDEBUG("VC1DecNextPicture: outputPictureBusAddress %lu", picture.output_picture_bus_address);
        DBGT_PDEBUG("VC1DecNextPicture: nChromaBusAddress %llu", frame->outBufPrivate.nChromaBusAddress);
        DBGT_PDEBUG("pLumaBase %p, pChromaBase %p", frame->outBufPrivate.pLumaBase, frame->outBufPrivate.pChromaBase);
        DBGT_PDEBUG("Luma size %lu", frame->outBufPrivate.nLumaSize);
        DBGT_PDEBUG("Chroma size %lu", frame->outBufPrivate.nChromaSize);
#endif
        //frame->size = this->framesize;
        frame->size = (picture.frame_width * picture.frame_height * 3) / 2;
        frame->MB_err_count = picture.number_of_err_mbs;

        //this->out_pic[this->out_count % MAX_BUFFERS] = picture;
        this->out_pic[this->out_index_w] = picture;
        this->out_count++;
        this->out_index_w++;
        if (this->out_index_w == MAX_BUFFERS) this->out_index_w = 0;
        this->out_num++;

        DBGT_EPILOG("");
        return CODEC_HAS_FRAME;
    case VC1DEC_PARAM_ERROR:
        DBGT_CRITICAL("VC1DEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    case VC1DEC_NOT_INITIALIZED:
#ifndef USE_OUTPUT_RELEASE
        // For VC1 the decoder instance may be not built yet before output thread created.
        DBGT_CRITICAL("VC1DEC_NOT_INITIALIZED");
#endif
        DBGT_EPILOG("");
        return CODEC_ERROR_SYS;
#ifdef USE_OUTPUT_RELEASE
    case VC1DEC_END_OF_STREAM:
        DBGT_PDEBUG("VC1DecNextPicture: End of stream");
        DBGT_EPILOG("");
        return CODEC_END_OF_STREAM;
    case VC1DEC_ABORTED:
        DBGT_PDEBUG("VC1DecNextPicture: aborted");
        DBGT_EPILOG("");
        return CODEC_ABORTED;
    case VC1DEC_FLUSHED:
        DBGT_PDEBUG("VC1DecNextPicture: flushed");
        DBGT_EPILOG("");
        return CODEC_FLUSHED;
#endif
    default:
        DBGT_ASSERT(!"unhandled VC1DecRet");
        break;
    }
    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

static OMX_S32 decoder_scanframe_vc1(CODEC_PROTOTYPE * arg, STREAM_BUFFER * buf,
                                 OMX_U32 * first, OMX_U32 * last)
{
    DBGT_PROLOG("");

    CALLSTACK;

    CODEC_VC1 *this = (CODEC_VC1 *) arg;

    DBGT_ASSERT(this);

#ifdef USE_SCANFRAME
    if (this->metadata.profile == 8) //advanced
    {
        *first = 0;
        *last = 0;

        // scan for start code
        OMX_S32 i = 0;

        OMX_S32 z = 0;

        for(; i < (OMX_S32)buf->streamlen; ++i)
        {
            if (!buf->bus_data[i])
                ++z;
            else if (buf->bus_data[i] == 0x01 && z >= 2)
            {
                *first = i - z;
                /*DBGT_PDEBUG("first: %02x, %02x, %02x, %02x\n", buf->bus_data[i - 2],
                      buf->bus_data[i - 1], buf->bus_data[i],
                      buf->bus_data[i + 1]);*/
                break;
            }
            else
                z = 0;
        }

        for(i = buf->streamlen - 3; i >= 0; --i)
        {
            if ((buf->bus_data[i] == 0x00) &&
               (buf->bus_data[i + 1] == 0x00) && (buf->bus_data[i + 2] == 0x01))
            {
                *last = i;
                /*DBGT_PDEBUG(" last: %02x, %02x, %02x, %02x\n", buf->bus_data[i],
                      buf->bus_data[i + 1], buf->bus_data[i + 2],
                      buf->bus_data[i + 3]);*/
                DBGT_EPILOG("");
                return 1;
            }
        }
        DBGT_EPILOG("");
        return -1;
    }
    else
    {
        *first = 0;
        *last = buf->streamlen;
        DBGT_EPILOG("");
        return 1;
    }
#else
    *first = 0;
    *last = buf->streamlen;
    DBGT_EPILOG("");
    return 1;
#endif
}

static CODEC_STATE decoder_setppargs_vc1(CODEC_PROTOTYPE * codec,
                                         PP_ARGS * args)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VC1 *this = (CODEC_VC1 *) codec;

    DBGT_ASSERT(this);
    DBGT_ASSERT(args);
#ifdef ENABLE_PP
    PPResult ret;
    ret = HantroHwDecOmx_pp_init(&this->pp_instance, &this->pp_config);

    if (ret == PP_OK)
    {
        this->transforms =
            HantroHwDecOmx_pp_set_output(&this->pp_config, args, OMX_TRUE);
        if (this->transforms == PPTR_ARG_ERROR)
        {
            DBGT_CRITICAL("HantroHwDecOmx_pp_set_output ((this->transforms == PPTR_ARG_ERROR))");
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        }

        if (this->transforms == PPTR_NONE)
        {
            DBGT_PDEBUG("PP_DISABLED");
            this->pp_state = PP_DISABLED;
        }
        else
        {
            this->pp_state = PP_PIPELINE;
        }
        DBGT_EPILOG("");
        return CODEC_OK;
    }
#endif
    this->pp_state = PP_DISABLED;
    DBGT_EPILOG("");
    return CODEC_OK;
}

static CODEC_STATE decoder_endofstream_vc1(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_VC1 *this = (CODEC_VC1*)arg;
    VC1DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = VC1DecEndOfStream(this->instance, 1);

    switch (ret)
    {
        case VC1DEC_OK:
            stat = CODEC_OK;
            break;
        case VC1DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case VC1DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case VC1DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case VC1DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case VC1DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case VC1DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case VC1DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case VC1DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case VC1DEC_ABORTED:
            stat = CODEC_ABORTED;
            break;
        default:
            DBGT_PDEBUG("VC1DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled VC1DecRet");
            break;
    }

    DBGT_EPILOG("");
    return stat;
#else
    UNUSED_PARAMETER(arg);
    DBGT_EPILOG("");
    return CODEC_OK;
#endif
}

// create codec instance and initialize it
CODEC_PROTOTYPE *HantroHwDecOmx_decoder_create_vc1(const void *DWLInstance,
                                        OMX_VIDEO_PARAM_G1CONFIGTYPE *g1Conf)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VC1 *this = OSAL_Malloc(sizeof(CODEC_VC1));
    VC1DecApiVersion decApi;
    VC1DecBuild decBuild;
#ifdef ENABLE_PP
    PPApiVersion ppVer;
    PPBuild ppBuild;
#endif

    memset(this, 0, sizeof(CODEC_VC1));

    this->base.destroy = decoder_destroy_vc1;
    this->base.decode = decoder_decode_vc1;
    this->base.getinfo = decoder_getinfo_vc1;
    this->base.getframe = decoder_getframe_vc1;
    this->base.scanframe = decoder_scanframe_vc1;
    this->base.setppargs = decoder_setppargs_vc1;
    this->base.endofstream = decoder_endofstream_vc1;
    this->base.pictureconsumed = decoder_pictureconsumed_vc1;
    this->base.setframebuffer = decoder_setframebuffer_vc1;
#ifdef USE_EXTERNAL_BUFFER
    this->base.getframebufferinfo = decoder_getframebufferinfo_vc1;
#endif
    this->base.abort = decoder_abort_vc1;
    this->base.abortafter = decoder_abortafter_vc1;
    this->base.setnoreorder = decoder_setnoreorder_vc1;
    this->base.setinfo = decoder_setinfo_vc1;
    this->instance = 0;
    this->picId = 0;
    this->pipeline_enabled = OMX_FALSE;
    this->resolution_changed = OMX_FALSE;
    this->DWLInstance = DWLInstance;
    this->state = VC1_PARSE_METADATA;
    this->dpbFlags = DEC_REF_FRM_RASTER_SCAN;

    if (g1Conf->bEnableTiled)
        this->dpbFlags = DEC_REF_FRM_TILED_DEFAULT;

    if (g1Conf->bAllowFieldDBP)
        this->dpbFlags |= DEC_DPB_ALLOW_FIELD_ORDERING;

    this->bEnableAdaptiveBuffers = g1Conf->bEnableAdaptiveBuffers;
    this->nGuardSize = g1Conf->nGuardSize;

    DBGT_PDEBUG("dpbFlags 0x%x", this->dpbFlags);

    /* Print API version number */
    decApi = VC1DecGetAPIVersion();
    decBuild = VC1DecGetBuild();
    DBGT_PDEBUG("X170 VC1 Decoder API v%d.%d - SW build: %d.%d - HW build: %x",
            decApi.major, decApi.minor, decBuild.sw_build >> 16,
            decBuild.sw_build & 0xFFFF, decBuild.hw_build);
    UNUSED_PARAMETER(decApi);
    UNUSED_PARAMETER(decBuild);

#ifdef ENABLE_PP
    /* Print API and build version numbers */
    ppVer = PPGetAPIVersion();
    ppBuild = PPGetBuild();

    /* Version */
    DBGT_PDEBUG("X170 PP API v%d.%d - SW build: %d.%d - HW build: %x",
            ppVer.major, ppVer.minor, ppBuild.sw_build >> 16,
            ppBuild.sw_build & 0xFFFF, ppBuild.hw_build);
#endif

    if (OSAL_EventCreate(&this->inst_create_event) != OMX_ErrorNone)
        return NULL;

    DBGT_EPILOG("");

    return (CODEC_PROTOTYPE *) this;
}

#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_vc1(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_VC1 *this = (CODEC_VC1 *)arg;
    VC1DecBufferInfo info;
    FRAME_BUFFER_INFO bufInfo;
    memset(&bufInfo, 0, sizeof(FRAME_BUFFER_INFO));

    VC1DecGetBufferInfo(this->instance, &info);

    bufInfo.bufferSize = info.next_buf_size;
    bufInfo.numberOfBuffers = info.buf_num;
    DBGT_PDEBUG("bufferSize %d, numberOfBuffers %d", (int)bufInfo.bufferSize, (int)bufInfo.numberOfBuffers);

    DBGT_EPILOG("");
    return bufInfo;
}
#endif

CODEC_STATE decoder_setframebuffer_vc1(CODEC_PROTOTYPE * arg, BUFFER *buff, OMX_U32 available_buffers)
{
    CALLSTACK;

    DBGT_PROLOG("");
#ifdef USE_EXTERNAL_BUFFER
    UNUSED_PARAMETER(available_buffers);
    CODEC_VC1 *this = (CODEC_VC1 *)arg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;
    struct DWLLinearMem mem;
    VC1DecBufferInfo info;
    VC1DecRet ret;

    memset(&info, 0, sizeof(VC1DecBufferInfo));

    if (info.next_buf_size > buff->allocsize)
    {
        DBGT_CRITICAL("Buffer size error. Required size %d > allocated size %d",
            (int)info.next_buf_size, (int)buff->allocsize);
        DBGT_EPILOG("");
        return CODEC_ERROR_BUFFER_SIZE;
    }

    mem.virtual_address = (u32*)buff->bus_data;
    mem.bus_address = buff->bus_address;
    mem.size = buff->allocsize;
    DBGT_PDEBUG("virtual_address %p, bus_address %lu, size %d",
    mem.virtual_address, mem.bus_address, mem.size);

    ret = VC1DecAddBuffer(this->instance, &mem);
    DBGT_PDEBUG("VC1DecAddBuffer ret (%d)", ret);

    switch (ret)
    {
        case VC1DEC_OK:
            stat = CODEC_OK;
            break;
        case VC1DEC_WAITING_FOR_BUFFER:
            stat = CODEC_NEED_MORE;
            break;
        case VC1DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case VC1DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        default:
            DBGT_PDEBUG("DecRet (%d)", ret);
            DBGT_ASSERT(!"Unhandled DecRet");
            break;
    }

    DBGT_EPILOG("");
    return stat;
#else
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(buff);
    UNUSED_PARAMETER(available_buffers);
#endif
    DBGT_EPILOG("");
    return CODEC_OK;
}

CODEC_STATE decoder_pictureconsumed_vc1(CODEC_PROTOTYPE * arg, BUFFER *buff)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_VC1*this = (CODEC_VC1 *)arg;
    VC1DecPicture pic;
    OMX_U32 i, j;

    DBGT_PDEBUG("Consumed: bus_address %lu", buff->bus_address);

    for (i=0; i<MAX_BUFFERS; i++)
    {
        if (buff->bus_address == this->out_pic[i].output_picture_bus_address)
        {
            DBGT_PDEBUG("Found out_pic[%d]: bus_address %lu", (int)i, buff->bus_address);
            pic = this->out_pic[i];
            VC1DecRet ret = VC1DecPictureConsumed(this->instance, &pic);
            DBGT_PDEBUG("VC1DecPictureConsumed ret (%d)", ret);
            UNUSED_PARAMETER(ret);
            memset(&this->out_pic[i], 0, sizeof(this->out_pic[i]));
            break;
        }
    }

    // This condition may be happened in CommandFlush/PortReconfig/StateChange seq.
    // Show the warning message in verbose mode.
    if (i >= MAX_BUFFERS)
    {
        DBGT_PDEBUG("Output picture not found");
        DBGT_EPILOG("");
        return CODEC_ERROR_UNSPECIFIED;
    }

    j = (i + MAX_BUFFERS - this->out_index_r) % MAX_BUFFERS;
    while (j > 0)
    {
        if (i == 0)
        {
            this->out_pic[0] = this->out_pic[MAX_BUFFERS - 1];
            i = MAX_BUFFERS;
        }
        else
            this->out_pic[i] = this->out_pic[i - 1];
        i--;
        j--;
    }
    memset(&this->out_pic[this->out_index_r], 0, sizeof(this->out_pic[0]));
    this->out_index_r++;
    if (this->out_index_r == MAX_BUFFERS)
        this->out_index_r = 0;
    this->out_num--;
#else
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(buff);
#endif
    DBGT_EPILOG("");
    return CODEC_OK;
}


CODEC_STATE decoder_abort_vc1(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_VC1 *this = (CODEC_VC1*)arg;
    VC1DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = VC1DecAbort(this->instance);

    switch (ret)
    {
        case VC1DEC_OK:
            stat = CODEC_OK;
            break;
        case VC1DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case VC1DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case VC1DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case VC1DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case VC1DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case VC1DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case VC1DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case VC1DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        default:
            DBGT_PDEBUG("VC1DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled VC1DecRet");
            break;
    }

    this->abort = 1;
    if (OSAL_EventSet(this->inst_create_event) != OMX_ErrorNone)
        stat = CODEC_ERROR_UNSPECIFIED;

    DBGT_EPILOG("");
    return stat;
#else
    UNUSED_PARAMETER(arg);
    DBGT_EPILOG("");
    return CODEC_OK;
#endif
}



CODEC_STATE decoder_abortafter_vc1(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_VC1 *this = (CODEC_VC1*)arg;
    VC1DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = VC1DecAbortAfter(this->instance);

    switch (ret)
    {
        case VC1DEC_OK:
            stat = CODEC_OK;
            break;
        case VC1DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case VC1DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case VC1DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case VC1DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case VC1DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case VC1DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case VC1DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case VC1DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        default:
            DBGT_PDEBUG("VC1DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled VC1DecRet");
            break;
    }

    this->abort = 0;
    if (OSAL_EventReset(this->inst_create_event) != OMX_ErrorNone)
        stat = CODEC_ERROR_UNSPECIFIED;

    DBGT_EPILOG("");
    return stat;
#else
    UNUSED_PARAMETER(arg);
    DBGT_EPILOG("");
    return CODEC_OK;
#endif
}

CODEC_STATE decoder_setnoreorder_vc1(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder)
{
    CALLSTACK;

    DBGT_PROLOG("");
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(no_reorder);
    DBGT_EPILOG("");
    return CODEC_OK;
}

CODEC_STATE decoder_setinfo_vc1(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf)
{
   return CODEC_OK;
}
