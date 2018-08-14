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
#include "codec_rv.h"
#include "post_processor.h"
#include "rvdecapi.h"
#ifdef ENABLE_PP
#include "ppapi.h"
#endif
#include "util.h"
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX RV"
#define MAX_BUFFERS 16


/* number of frame buffers decoder should allocate 0=AUTO */
//#define FRAME_BUFFERS 3
#define FRAME_BUFFERS 0

#define SHOW4(p) (p[0]) | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p+=4;
#define ALIGN(offset, align)   ((((unsigned int)offset) + align-1) & ~(align-1))
//#define ALIGN(offset, align)    offset

typedef struct CODEC_RV
{
    CODEC_PROTOTYPE base;

    RvDecInst instance;
#ifdef ENABLE_PP
    PPConfig pp_config;
    PP_TRANSFORMS transforms;
    PPInst pp_instance;
#endif
    PP_STATE pp_state;
    OMX_U32 maxWidth;
    OMX_U32 maxHeight;
    OMX_U32 pctszSize;
    OMX_BOOL bIsRV8;
    OMX_U32 framesize;
    OMX_BOOL update_pp_out;
    OMX_U32 picId;
    OMX_U32 sliceInfoNum;
    OMX_U8 pSliceInfo;
    OMX_U32 out_count;
    OMX_U32 out_index_r;
    OMX_U32 out_index_w;
    OMX_U32 out_num;
    RvDecPicture out_pic[MAX_BUFFERS];
} CODEC_RV;

CODEC_STATE decoder_setframebuffer_rv(CODEC_PROTOTYPE * arg, BUFFER *buff,
                                        OMX_U32 available_buffers);
CODEC_STATE decoder_pictureconsumed_rv(CODEC_PROTOTYPE * arg, BUFFER *buff);
FRAME_BUFFER_INFO decoder_getframebufferinfo_rv(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_abort_rv(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_abortafter_rv(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_setnoreorder_rv(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder);
CODEC_STATE decoder_setinfo_rv(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf);
// destroy codec instance
static void decoder_destroy_rv(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_RV *this = (CODEC_RV *) arg;

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
            if (this->pp_state == PP_PIPELINE)
            {
                HantroHwDecOmx_pp_pipeline_disable(this->pp_instance,
                                                   this->instance);
                this->pp_state = PP_DISABLED;
            }
            HantroHwDecOmx_pp_destroy(&this->pp_instance);
        }
#endif
        if (this->instance)
        {
            RvDecRelease(this->instance);
            this->instance = 0;
        }

        OSAL_Free(this);
    }
    DBGT_EPILOG("");
}


// try to consume stream data
static CODEC_STATE decoder_decode_rv(CODEC_PROTOTYPE * arg,
                                      STREAM_BUFFER * buf, OMX_U32 * consumed,
                                      FRAME * frame)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_RV *this = (CODEC_RV *) arg;

    DBGT_ASSERT(this);
    DBGT_ASSERT(buf);
    DBGT_ASSERT(consumed);

    RvDecInput input;
    RvDecOutput output;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    memset(&input, 0, sizeof(RvDecInput));
    memset(&output, 0, sizeof(RvDecOutput));

    input.stream = buf->bus_data;
    input.stream_bus_address = buf->bus_address;
    input.data_len = buf->streamlen;
    frame->size = 0;
    input.pic_id = buf->picId;
    DBGT_PDEBUG("Pic id %d, stream length %d ", (int)this->picId, input.data_len);

    input.slice_info_num = buf->sliceInfoNum;
    input.slice_info = (RvDecSliceInfo*) buf->pSliceInfo;
    input.timestamp = 0;
    input.skip_non_reference = 0;
#ifdef ENABLE_PP
    if (this->pp_state == PP_PIPELINE && this->update_pp_out == OMX_TRUE)
    {
        HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
        PPResult res = HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config);
        this->update_pp_out = OMX_FALSE;
        if (res != PP_OK)
        {
            DBGT_CRITICAL("HantroHwDecOmx_pp_set failed (err=%d)", res);
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        }
    }
#endif

    RvDecRet ret = RvDecDecode(this->instance, &input, &output);

    switch (ret)
    {
        case RVDEC_STRM_PROCESSED:
            stat = CODEC_NEED_MORE;
            break;
        case RVDEC_BUF_EMPTY:
            stat = CODEC_BUFFER_EMPTY;
            break;
        case RVDEC_HDRS_RDY:
#ifdef ENABLE_PP
            if (this->pp_state == PP_PIPELINE)
            {
                HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
            }
#endif
            stat = CODEC_HAS_INFO;
            break;
        case RVDEC_PIC_DECODED:
            this->picId++;
            stat = CODEC_HAS_FRAME;
            break;
        case RVDEC_NONREF_PIC_SKIPPED:
            DBGT_PDEBUG("Nonreference picture skipped");
            this->picId++;
            output.data_left = 0;
            stat = CODEC_PIC_SKIPPED;
            break;
#ifdef USE_EXTERNAL_BUFFER
        case RVDEC_WAITING_FOR_BUFFER:
            DBGT_PDEBUG("Waiting for frame buffer");
            RvDecBufferInfo info;

            ret = RvDecGetBufferInfo(this->instance, &info);
            DBGT_PDEBUG("Buffer size %d, number of buffers %d",
                info.next_buf_size, info.buf_num);
            stat = CODEC_WAITING_FRAME_BUFFER;

            // Reset output relatived parameters
            this->out_index_w = 0;
            this->out_index_r = 0;
            this->out_num = 0;
            memset(this->out_pic, 0, sizeof(RvDecPicture)*MAX_BUFFERS);
            break;
#endif
#ifdef USE_OUTPUT_RELEASE
        case RVDEC_ABORTED:
            DBGT_PDEBUG("Decoding aborted");
            *consumed = input.data_len;
            DBGT_EPILOG("");
            return CODEC_ABORTED;
#endif
        case RVDEC_NO_DECODING_BUFFER:
            stat = CODEC_NO_DECODING_BUFFER;
            break;
        case RVDEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case RVDEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case RVDEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        case RVDEC_FORMAT_NOT_SUPPORTED:
            stat = CODEC_ERROR_FORMAT_NOT_SUPPORTED;
            break;
        case RVDEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case RVDEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case RVDEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case RVDEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case RVDEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        default:
            DBGT_ASSERT(!"unhandled RvDecRet");
            break;
    }

    if (stat != CODEC_ERROR_UNSPECIFIED)
    {
            if (ret == RVDEC_HDRS_RDY)
               *consumed = 0;
            else if (ret == RVDEC_PIC_DECODED && output.data_left == 1)
                *consumed = input.data_len; // fix for raw RV stream
            else
                *consumed = input.data_len - output.data_left;
    }
    DBGT_EPILOG("");
    return stat;
}

// get stream info
static CODEC_STATE decoder_getinfo_rv(CODEC_PROTOTYPE * arg, STREAM_INFO * pkg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_RV *this = (CODEC_RV *) arg;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(pkg);

    RvDecInfo info;

    memset(&info, 0, sizeof(RvDecInfo));
    RvDecRet ret = RvDecGetInfo(this->instance, &info);

    if (ret == RVDEC_OK)
    {
        if ((info.frame_width * info.frame_height * 256) > MAX_VIDEO_RESOLUTION)
        {
            DBGT_ERROR("Video stream resolution exceeds the supported video resolution");
            DBGT_EPILOG("");
            return CODEC_ERROR_STREAM_NOT_SUPPORTED;
        }
        pkg->width = info.frame_width*16;
        pkg->height = info.frame_height*16;
        pkg->stride = info.frame_width*16;
        pkg->sliceheight = info.frame_height*16;
        pkg->interlaced = 0;
        pkg->framesize = pkg->width * pkg->height * 3 / 2;

        if (info.output_format == RVDEC_TILED_YUV420)
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

        /* information about output size is now available */
        /* update output settings before decoding */
#ifdef ENABLE_PP
        this->update_pp_out = OMX_TRUE;
        HantroHwDecOmx_pp_set_info(&this->pp_config, pkg, this->transforms);

        if (HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config) != PP_OK)
        {
            DBGT_CRITICAL("HantroHwDecOmx_pp_set failed (err=%d)", ret);
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        }
#endif

#ifdef USE_EXTERNAL_BUFFER
        RvDecBufferInfo bufInfo;

        RvDecGetBufferInfo(this->instance, &bufInfo);

        pkg->frame_buffers = bufInfo.buf_num;
        DBGT_PDEBUG("Frame size %lu", pkg->framesize);
#endif
        this->framesize = pkg->framesize;
        DBGT_EPILOG("");
        return CODEC_OK;
    }
    else if (ret == RVDEC_HDRS_NOT_RDY)
    {
        DBGT_CRITICAL("RVDEC_HDRS_NOT_RDY");
        DBGT_EPILOG("");
        return CODEC_ERROR_STREAM;
    }
    else if (ret == RVDEC_PARAM_ERROR)
    {
        DBGT_CRITICAL("RVDEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }
    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

// get decoded frame
static CODEC_STATE decoder_getframe_rv(CODEC_PROTOTYPE * arg, FRAME * frame,
                                        OMX_BOOL eos)
{
    CALLSTACK;

    DBGT_PROLOG("");

    RvDecPicture picture;

    CODEC_RV *this = (CODEC_RV *) arg;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(frame);
#ifdef ENABLE_PP
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

    memset(&picture, 0, sizeof(RvDecPicture));

    u32 endofStream = eos == OMX_TRUE ? 1 : 0;

    RvDecRet ret = RvDecNextPicture(this->instance, &picture, endofStream);

    if (ret == RVDEC_PIC_RDY)
    {
        DBGT_ASSERT(this->framesize);

        DBGT_PDEBUG("end of stream %d", endofStream);
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
            // gots to copy the frame from the buffer reserved by the codec
            // into to the specified output buffer
            // there's no indication of the framesize in the output picture structure
            // so we have to use the framesize from before
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
        frame->outBufPrivate.nFrameWidth = picture.frame_width;
        frame->outBufPrivate.nFrameHeight = picture.frame_height;
        frame->outBufPrivate.nPicId[0] = frame->outBufPrivate.nPicId[1] = picture.decode_id;
        DBGT_PDEBUG("RvDecNextPicture: outputPictureBusAddress %lu", picture.output_picture_bus_address);
        DBGT_PDEBUG("RvDecNextPicture: nChromaBusAddress %llu", frame->outBufPrivate.nChromaBusAddress);
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
    }
    else if (ret == RVDEC_OK)
    {
        DBGT_EPILOG("");
        return CODEC_OK;
    }
    else if (ret == RVDEC_PARAM_ERROR)
    {
        DBGT_CRITICAL("RVDEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }
    else if (ret == RVDEC_NOT_INITIALIZED)
    {
        DBGT_CRITICAL("RVDEC_NOT_INITIALIZED");
        DBGT_EPILOG("");
        return CODEC_ERROR_SYS;
    }
#ifdef USE_OUTPUT_RELEASE
    else if (ret == RVDEC_END_OF_STREAM)
    {
        DBGT_PDEBUG("RvDecNextPicture: End of stream");
        DBGT_EPILOG("");
        return CODEC_END_OF_STREAM;
    }
    else if (ret ==  RVDEC_ABORTED)
    {
        DBGT_PDEBUG("RvDecNextPicture: aborted");
        DBGT_EPILOG("");
        return CODEC_ABORTED;
    }
    else if (ret == RVDEC_FLUSHED)
    {
        DBGT_PDEBUG("RvDecNextPicture: flushed");
        DBGT_EPILOG("");
        return CODEC_FLUSHED;
    }

#endif
    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

static OMX_S32 decoder_scanframe_rv(CODEC_PROTOTYPE * arg, STREAM_BUFFER * buf,
                                 OMX_U32 * first, OMX_U32 * last)
{
    CALLSTACK;

    CODEC_RV *this = (CODEC_RV *) arg;

    DBGT_ASSERT(this);

    *first = 0;
    *last = buf->streamlen;
    return 1;
}

static CODEC_STATE decoder_setppargs_rv(CODEC_PROTOTYPE * codec,
                                         PP_ARGS * args)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_RV *this = (CODEC_RV *) codec;

    DBGT_ASSERT(this);
    DBGT_ASSERT(args);
#ifdef ENABLE_PP
    PPResult ret;
    OMX_BOOL enableCombinedMode = OMX_FALSE;

    if (this->pp_instance == NULL)
    {
        enableCombinedMode = OMX_TRUE;
    }

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
            DBGT_PDEBUG("PP_COMBINED_MODE");
            PPResult res;

            if (enableCombinedMode)
            {
                res = HantroHwDecOmx_pp_pipeline_enable(this->pp_instance,
                                                      this->instance,
                                                      PP_PIPELINED_DEC_TYPE_RV);
                if (res != PP_OK)
                {
                    DBGT_CRITICAL("HantroHwDecOmx_pp_pipeline_enable (err=%d)", res);
                    DBGT_EPILOG("");
                    return CODEC_ERROR_UNSPECIFIED;
                }
            }

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

static CODEC_STATE decoder_endofstream_rv(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_RV *this = (CODEC_RV *)arg;
    RvDecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = RvDecEndOfStream(this->instance, 1);

    switch (ret)
    {
        case RVDEC_OK:
            stat = CODEC_OK;
            break;
        case RVDEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case RVDEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case RVDEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case RVDEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case RVDEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case RVDEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case RVDEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case RVDEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case RVDEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        case RVDEC_ABORTED:
            stat = CODEC_ABORTED;
            break;
        default:
            DBGT_PDEBUG("RvDecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled RvDecRet");
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
CODEC_PROTOTYPE *HantroHwDecOmx_decoder_create_rv(const void *DWLInstance,
                                                  OMX_BOOL bIsRV8, OMX_U32 frame_code_length, OMX_U32 *frame_sizes,
                                                  OMX_U32 maxWidth, OMX_U32 maxHeight,
                                                  OMX_VIDEO_PARAM_G1CONFIGTYPE *g1Conf)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_RV *this = OSAL_Malloc(sizeof(CODEC_RV));
    RvDecApiVersion decApi;
    RvDecBuild decBuild;
#ifdef ENABLE_PP
    PPApiVersion ppVer;
    PPBuild ppBuild;
#endif

    memset(this, 0, sizeof(CODEC_RV));

    this->base.destroy = decoder_destroy_rv;
    this->base.decode = decoder_decode_rv;
    this->base.getinfo = decoder_getinfo_rv;
    this->base.getframe = decoder_getframe_rv;
    this->base.scanframe = decoder_scanframe_rv;
    this->base.setppargs = decoder_setppargs_rv;
    this->base.endofstream = decoder_endofstream_rv;
    this->base.pictureconsumed = decoder_pictureconsumed_rv;
    this->base.setframebuffer = decoder_setframebuffer_rv;
#ifdef USE_EXTERNAL_BUFFER
    this->base.getframebufferinfo = decoder_getframebufferinfo_rv;
#endif
    this->base.abort = decoder_abort_rv;
    this->base.abortafter = decoder_abortafter_rv;
    this->base.setnoreorder = decoder_setnoreorder_rv;
    this->base.setinfo = decoder_setinfo_rv;
    this->instance = 0;
    this->picId = 0;

    /* Print API version number */
    decApi = RvDecGetAPIVersion();
    decBuild = RvDecGetBuild();
    DBGT_PDEBUG("X170 RV Decoder API v%d.%d - SW build: %d.%d - HW build: %x",
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


#ifdef IS_G1_DECODER
    enum DecDpbFlags dpbFlags = DEC_REF_FRM_RASTER_SCAN;
    struct DecDownscaleCfg dscale_cfg;
    dscale_cfg.down_scale_x = 0;
    dscale_cfg.down_scale_y = 0;

    if (g1Conf->bEnableTiled)
        dpbFlags = DEC_REF_FRM_TILED_DEFAULT;

    if (g1Conf->bAllowFieldDBP)
        dpbFlags |= DEC_DPB_ALLOW_FIELD_ORDERING;

    DBGT_PDEBUG("dpbFlags 0x%x", dpbFlags);

    RvDecRet ret = RvDecInit(&this->instance,
#ifdef USE_EXTERNAL_BUFFER
                            DWLInstance,
#endif
                            ERROR_HANDLING,
                            frame_code_length, bIsRV8 == OMX_TRUE ?  frame_sizes : NULL ,
                            bIsRV8 ? 0 : 1,
                            maxWidth, maxHeight, FRAME_BUFFERS, dpbFlags,
                            g1Conf->bEnableAdaptiveBuffers,
                            g1Conf->nGuardSize,
                            &dscale_cfg);
#else
    RvDecRet ret = RvDecInit(&this->instance,
                            USE_VIDEO_FREEZE_CONCEALMENT,
                            frame_code_length, bIsRV8 == OMX_TRUE ?  frame_sizes : NULL ,
                            bIsRV8 ? 0 : 1,
                            maxWidth, maxHeight, FRAME_BUFFERS);
#endif
    (void)DWLInstance;
    if (ret != RVDEC_OK)
    {
        decoder_destroy_rv((CODEC_PROTOTYPE *) this);
        DBGT_CRITICAL("RvDecInit error");
        DBGT_EPILOG("");
        return NULL;
    }
    DBGT_EPILOG("");
    return (CODEC_PROTOTYPE *) this;
}

#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_rv(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_RV *this = (CODEC_RV *)arg;
    RvDecBufferInfo info;
    FRAME_BUFFER_INFO bufInfo;
    memset(&bufInfo, 0, sizeof(FRAME_BUFFER_INFO));

    RvDecGetBufferInfo(this->instance, &info);

    bufInfo.bufferSize = info.next_buf_size;
    bufInfo.numberOfBuffers = info.buf_num;
    DBGT_PDEBUG("bufferSize %d, numberOfBuffers %d", (int)bufInfo.bufferSize, (int)bufInfo.numberOfBuffers);

    DBGT_EPILOG("");
    return bufInfo;
}
#endif

CODEC_STATE decoder_setframebuffer_rv(CODEC_PROTOTYPE * arg, BUFFER *buff, OMX_U32 available_buffers)
{
    CALLSTACK;

    DBGT_PROLOG("");
#ifdef USE_EXTERNAL_BUFFER
    UNUSED_PARAMETER(available_buffers);
    CODEC_RV *this = (CODEC_RV *)arg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;
    struct DWLLinearMem mem;
    RvDecBufferInfo info;
    RvDecRet ret;

    memset(&info, 0, sizeof(RvDecBufferInfo));

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

    ret = RvDecAddBuffer(this->instance, &mem);
    DBGT_PDEBUG("RvDecAddBuffer ret (%d)", ret);

    switch (ret)
    {
        case RVDEC_OK:
            stat = CODEC_OK;
            break;
        case RVDEC_WAITING_FOR_BUFFER:
            stat = CODEC_NEED_MORE;
            break;
        case RVDEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case RVDEC_MEMFAIL:
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

CODEC_STATE decoder_pictureconsumed_rv(CODEC_PROTOTYPE * arg, BUFFER *buff)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_RV *this = (CODEC_RV *)arg;
    RvDecPicture pic;
    OMX_U32 i, j;

    DBGT_PDEBUG("Consumed: bus_address %lu", buff->bus_address);

    for (i=0; i<MAX_BUFFERS; i++)
    {
        if (buff->bus_address == this->out_pic[i].output_picture_bus_address)
        {
            DBGT_PDEBUG("Found out_pic[%d]: bus_address %lu", (int)i, buff->bus_address);
            pic = this->out_pic[i];
            RvDecRet ret = RvDecPictureConsumed(this->instance, &pic);
            DBGT_PDEBUG("RvDecPictureConsumed ret (%d)", ret);
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


CODEC_STATE decoder_abort_rv(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_RV *this = (CODEC_RV *)arg;
    RvDecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = RvDecAbort(this->instance);

    switch (ret)
    {
        case RVDEC_OK:
            stat = CODEC_OK;
            break;
        case RVDEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case RVDEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case RVDEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case RVDEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case RVDEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case RVDEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case RVDEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case RVDEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case RVDEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("RvDecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled RvDecRet");
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


CODEC_STATE decoder_abortafter_rv(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_RV *this = (CODEC_RV *)arg;
    RvDecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = RvDecAbortAfter(this->instance);

    switch (ret)
    {
        case RVDEC_OK:
            stat = CODEC_OK;
            break;
        case RVDEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case RVDEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case RVDEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case RVDEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case RVDEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case RVDEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case RVDEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case RVDEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case RVDEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("RvDecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled RvDecRet");
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

CODEC_STATE decoder_setnoreorder_rv(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder)
{
    CALLSTACK;

    DBGT_PROLOG("");
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(no_reorder);
    DBGT_EPILOG("");
    return CODEC_OK;
}

CODEC_STATE decoder_setinfo_rv(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf)
{
   return CODEC_OK;
}
