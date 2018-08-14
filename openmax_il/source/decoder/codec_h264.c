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
#include "codec_h264.h"
#include "h264decapi.h"
#include "post_processor.h"
#ifdef ENABLE_PP
#include "ppapi.h"
#endif
#include "util.h"
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX H264"

#define DISABLE_OUTPUT_REORDER 0
#define USE_DISPLAY_SMOOTHING 0
#define MAX_BUFFERS 34

typedef struct CODEC_H264
{
    CODEC_PROTOTYPE base;
    OMX_U32 framesize;
    H264DecInst instance;
#ifdef ENABLE_PP
    PPConfig pp_config;
    PPInst pp_instance;
    PP_TRANSFORMS transforms;
#endif
    PP_STATE pp_state;
    OMX_U32 picId;
    //OMX_BOOL extraEosLoopDone;
    OMX_BOOL update_pp_out;
    OMX_BOOL frame_sent;
    OMX_BOOL pending_flush;
    OMX_U32 out_count;
    OMX_U32 out_index_r;
    OMX_U32 out_index_w;
    OMX_U32 out_num;
    H264DecPicture out_pic[MAX_BUFFERS];
} CODEC_H264;

CODEC_STATE decoder_setframebuffer_h264(CODEC_PROTOTYPE * arg, BUFFER *buff,
                                        OMX_U32 available_buffers);
CODEC_STATE decoder_pictureconsumed_h264(CODEC_PROTOTYPE * arg, BUFFER *buff);
CODEC_STATE decoder_abort_h264(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_abortafter_h264(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_setnoreorder_h264(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder);
CODEC_STATE decoder_setinfo_h264(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf);
FRAME_BUFFER_INFO decoder_getframebufferinfo_h264(CODEC_PROTOTYPE * arg);

// destroy codec instance
static void decoder_destroy_h264(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *) arg;

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
            H264DecRelease(this->instance);
            this->instance = 0;
        }

        OSAL_Free(this);
    }
    DBGT_EPILOG("");
}

// try to consume stream data
static CODEC_STATE decoder_decode_h264(CODEC_PROTOTYPE * arg,
                                       STREAM_BUFFER * buf, OMX_U32 * consumed,
                                       FRAME * frame)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *) arg;

    DBGT_ASSERT(this);
    DBGT_ASSERT(this->instance);
    DBGT_ASSERT(buf);
    DBGT_ASSERT(consumed);

    H264DecInput input;
    H264DecOutput output;

    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    memset(&input, 0, sizeof(H264DecInput));
    memset(&output, 0, sizeof(H264DecOutput));

    input.stream = buf->bus_data;
    input.stream_bus_address = buf->bus_address;
    input.data_len = buf->streamlen;
    input.pic_id = buf->picId;
    input.skip_non_reference = 0;
    DBGT_PDEBUG("Pic id %d, stream length %d", (int)this->picId, input.data_len);
    *consumed = 0;
    frame->size = 0;
    this->pending_flush = OMX_FALSE;

#ifdef ENABLE_PP
    if (this->pp_config.pp_out_img.buffer_bus_addr != output.strm_curr_bus_address)
    {
        this->update_pp_out = OMX_TRUE;
    }

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

    H264DecRet ret = H264DecDecode(this->instance, &input, &output);

    switch (ret)
    {
    case H264DEC_PENDING_FLUSH:
        DBGT_PDEBUG("Pending flush");
        //this->picId++;
        //this->pending_flush = OMX_TRUE;
        stat = CODEC_PENDING_FLUSH;
        break;
    case H264DEC_PIC_DECODED:
        this->picId++;
        stat = CODEC_HAS_FRAME;
        break;
    case H264DEC_HDRS_RDY:
#ifdef ENABLE_PP
        if (this->pp_state == PP_PIPELINE)
        {
            HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
        }
#endif
        stat = CODEC_HAS_INFO;
        break;
    case H264DEC_ADVANCED_TOOLS:
        stat = CODEC_NEED_MORE;
        break;
    case H264DEC_STRM_PROCESSED:
        stat = CODEC_NEED_MORE;
        break;
    case H264DEC_BUF_EMPTY:
        stat = CODEC_BUFFER_EMPTY;
        break;
    case H264DEC_NONREF_PIC_SKIPPED:
        DBGT_PDEBUG("Nonreference picture skipped");
        this->picId++;
        output.data_left = 0;
        stat = CODEC_PIC_SKIPPED;
        break;
#ifdef USE_EXTERNAL_BUFFER
    case H264DEC_WAITING_FOR_BUFFER:
    {
        DBGT_PDEBUG("Waiting for frame buffer");
        H264DecBufferInfo info;

        ret = H264DecGetBufferInfo(this->instance, &info);
        DBGT_PDEBUG("Buffer size %d, number of buffers %d",
            info.next_buf_size, info.buf_num);
        stat = CODEC_WAITING_FRAME_BUFFER;

        // Reset output relatived parameters
        this->out_index_w = 0;
        this->out_index_r = 0;
        this->out_num = 0;
        memset(this->out_pic, 0, sizeof(H264DecPicture)*MAX_BUFFERS);
    }
        break;
#endif
#ifdef USE_OUTPUT_RELEASE
    case H264DEC_ABORTED:
        DBGT_PDEBUG("Decoding aborted");
        *consumed = input.data_len;
        DBGT_EPILOG("");
        return CODEC_ABORTED;
#endif
    case H264DEC_NO_DECODING_BUFFER:
        stat = CODEC_NO_DECODING_BUFFER;
        break;
    case H264DEC_PARAM_ERROR:
        stat = CODEC_ERROR_INVALID_ARGUMENT;
        break;
    case H264DEC_STRM_ERROR:
        stat = CODEC_ERROR_STREAM;
        break;
    case H264DEC_NOT_INITIALIZED:
        stat = CODEC_ERROR_NOT_INITIALIZED;
        break;
    case H264DEC_HW_BUS_ERROR:
        stat = CODEC_ERROR_HW_BUS_ERROR;
        break;
    case H264DEC_HW_TIMEOUT:
        stat = CODEC_ERROR_HW_TIMEOUT;
        break;
    case H264DEC_SYSTEM_ERROR:
        stat = CODEC_ERROR_SYS;
        break;
    case H264DEC_HW_RESERVED:
        stat = CODEC_ERROR_HW_RESERVED;
        break;
    case H264DEC_MEMFAIL:
        stat = CODEC_ERROR_MEMFAIL;
        break;
    case H264DEC_STREAM_NOT_SUPPORTED:
        stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
        break;
    case H264DEC_FORMAT_NOT_SUPPORTED:
        stat = CODEC_ERROR_FORMAT_NOT_SUPPORTED;
        break;
    default:
        DBGT_ASSERT(!"Unhandled H264DecRet");
        break;
    }

    if (stat != CODEC_ERROR_UNSPECIFIED)
    {
        DBGT_PDEBUG("Decoder data left %d", output.data_left);

       // if (stat == CODEC_HAS_INFO)
       //     *consumed = 0;
       // else if (this->pending_flush)
            *consumed = input.data_len - output.data_left;
       // else
       //     *consumed = input.dataLen;
        DBGT_PDEBUG("Consumed %d", (int) *consumed);
    }

    DBGT_EPILOG("");
    return stat;
}

    // get stream info
static CODEC_STATE decoder_getinfo_h264(CODEC_PROTOTYPE * arg,
                                        STREAM_INFO * pkg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *) arg;
    H264DecInfo decinfo;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(pkg);

    memset(&decinfo, 0, sizeof(H264DecInfo));

    // the headers are ready, get stream information
    H264DecRet ret = H264DecGetInfo(this->instance, &decinfo);

    if (ret == H264DEC_OK)
    {
        if ((decinfo.pic_width * decinfo.pic_height) > MAX_VIDEO_RESOLUTION)
        {
            DBGT_ERROR("Video stream resolution exceeds the supported video resolution");
            DBGT_EPILOG("");
            return CODEC_ERROR_STREAM_NOT_SUPPORTED;
        }

        pkg->width = decinfo.pic_width;//decinfo.cropParams.cropOutWidth;
        pkg->height = decinfo.pic_height;//decinfo.cropParams.cropOutHeight;
        pkg->sliceheight = decinfo.pic_height;
        pkg->stride = decinfo.pic_width;

        if (decinfo.output_format == H264DEC_TILED_YUV420)
            pkg->format = OMX_COLOR_FormatYUV420SemiPlanar8x4Tiled;
        else
            pkg->format = OMX_COLOR_FormatYUV420PackedSemiPlanar;

#ifdef SET_OUTPUT_CROP_RECT
        pkg->crop_available = OMX_FALSE;
        pkg->crop_left = decinfo.crop_params.crop_left_offset;
        pkg->crop_top = decinfo.crop_params.crop_top_offset;
        pkg->crop_width = decinfo.crop_params.crop_out_width;
        pkg->crop_height = decinfo.crop_params.crop_out_height;
        if ((pkg->crop_left != 0) || (pkg->crop_top != 0) ||
            (pkg->crop_width != pkg->width) ||
            (pkg->crop_height != pkg->height))
        {
            pkg->crop_available = OMX_TRUE;
        }
        DBGT_PDEBUG("Crop left %d, top %d, width %d, height %d", (int)pkg->crop_left,
                (int)pkg->crop_top, (int)pkg->crop_width, (int)pkg->crop_height);
#endif

#if defined (IS_8190) || defined (IS_G1_DECODER)
        pkg->interlaced = decinfo.interlaced_sequence;
#else
        pkg->interlaced = 0;
#endif
        DBGT_PDEBUG("Interlaced sequence %d", (int)pkg->interlaced);

#ifdef USE_EXTERNAL_BUFFER
        H264DecBufferInfo bufInfo;

        H264DecGetBufferInfo(this->instance, &bufInfo);

        pkg->framesize = bufInfo.next_buf_size;
        pkg->frame_buffers = bufInfo.buf_num;
#else
        pkg->frame_buffers = decinfo.pic_buff_size;
        pkg->framesize = (pkg->sliceheight * pkg->stride) * 3 / 2;
#endif

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
        }
#endif
        this->framesize = pkg->framesize;
        DBGT_EPILOG("");
        return CODEC_OK;
    }
    else if (ret == H264DEC_PARAM_ERROR)
    {
        DBGT_CRITICAL("H264DEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }
    else if (ret == H264DEC_HDRS_NOT_RDY)
    {
        DBGT_CRITICAL("H264DEC_HDRS_NOT_RDY");
        DBGT_EPILOG("");
        return CODEC_ERROR_STREAM;
    }
    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

// get decoded frame
static CODEC_STATE decoder_getframe_h264(CODEC_PROTOTYPE * arg, FRAME * frame,
                                         OMX_BOOL eos)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *) arg;
    H264DecPicture picture;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(frame);

#ifdef ENABLE_PP
    if ((this->pp_state == PP_PIPELINE) &&
        (eos || (this->pp_config.pp_out_img.buffer_bus_addr != frame->fb_bus_address)))
    {
        DBGT_PDEBUG("Change pp output, EOS: %d", eos);
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

    if (USE_DISPLAY_SMOOTHING && this->frame_sent && !this->pending_flush && !eos)
    {
        DBGT_PDEBUG("Display smoothing: One frame already sent... Return");
        this->frame_sent = OMX_FALSE;
        DBGT_EPILOG("");
        return CODEC_OK;
    }

    /* stream ended but we know there is picture to be outputted. We
     * have to loop one more time without "eos" and on next round
     * NextPicture is called with eos "force last output"
     */

    memset(&picture, 0, sizeof(H264DecPicture));

    u32 endofStream = eos == OMX_TRUE ? 1 : 0;

    /* Flush internal picture buffers */
    if (this->pending_flush)
        endofStream = 1;

    H264DecRet ret = H264DecNextPicture(this->instance, &picture, endofStream);

    this = (CODEC_H264 *) arg;

    if (ret == H264DEC_PIC_RDY)
    {
        DBGT_ASSERT(this->framesize);

        DBGT_PDEBUG("End of stream %d", endofStream);
        DBGT_PDEBUG("Err mbs %d", picture.nbr_of_err_mbs);
        DBGT_PDEBUG("View ID %d", picture.view_id);
        DBGT_PDEBUG("Pic size %dx%d", picture.pic_width, picture.pic_height);
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
/*            if (this->pp_state == PP_STANDALONE)
            {
                HantroHwDecOmx_pp_set_input_buffer(&this->pp_config, picture.outputPictureBusAddress);
                HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
                PPResult res = HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config);
                if (res!=PP_OK)
                {
                    return CODEC_ERROR_UNSPECIFIED;
                }
                res = HantroHwDecOmx_pp_execute(this->pp_instance);
                if (res!=PP_OK)
                {
                    return CODEC_ERROR_UNSPECIFIED;
                }
           }*/
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
        frame->outBufPrivate.nLumaSize = picture.pic_width * picture.pic_height;
        frame->outBufPrivate.pChromaBase = frame->outBufPrivate.pLumaBase + frame->outBufPrivate.nLumaSize;
        frame->outBufPrivate.nChromaBusAddress = frame->outBufPrivate.nLumaBusAddress + frame->outBufPrivate.nLumaSize;
        frame->outBufPrivate.nChromaSize = frame->outBufPrivate.nLumaSize / 2;
        frame->outBufPrivate.nFrameWidth = picture.crop_params.crop_out_width;
        frame->outBufPrivate.nFrameHeight = picture.crop_params.crop_out_height;
        frame->outBufPrivate.nPicId[0] = picture.decode_id[0];
        frame->outBufPrivate.nPicId[1] = picture.decode_id[1];
        frame->outBufPrivate.singleField = picture.field_picture;
        DBGT_PDEBUG("H264DecNextPicture: outputPictureBusAddress %lu", picture.output_picture_bus_address);
        DBGT_PDEBUG("H264DecNextPicture: nChromaBusAddress %llu", frame->outBufPrivate.nChromaBusAddress);
        DBGT_PDEBUG("pLumaBase %p, pChromaBase %p", frame->outBufPrivate.pLumaBase, frame->outBufPrivate.pChromaBase);
        DBGT_PDEBUG("Luma size %lu", frame->outBufPrivate.nLumaSize);
        DBGT_PDEBUG("Chroma size %lu", frame->outBufPrivate.nChromaSize);
#endif
        //frame->size = this->framesize;
        frame->size = (picture.pic_width * picture.pic_height * 3) / 2;
        frame->MB_err_count = picture.nbr_of_err_mbs;
        frame->viewId = picture.view_id;

        //this->out_pic[this->out_count % MAX_BUFFERS] = picture;
        this->out_pic[this->out_index_w] = picture;
        this->out_count++;
        this->out_index_w++;
        if (this->out_index_w == MAX_BUFFERS) this->out_index_w = 0;
        this->out_num++;

        if (USE_DISPLAY_SMOOTHING && !this->pending_flush && !eos)
        {
            this->frame_sent = OMX_TRUE;
        }
        DBGT_EPILOG("");
        return CODEC_HAS_FRAME;
    }
    else if (ret == H264DEC_OK)
    {
        DBGT_EPILOG("");
        return CODEC_OK;
    }
    else if (ret == H264DEC_PARAM_ERROR)
    {
        DBGT_CRITICAL("H264DEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }
    else if (ret == H264DEC_END_OF_STREAM)
    {
        DBGT_PDEBUG("H264DecNextPicture: End of stream");
        DBGT_EPILOG("");
        return CODEC_END_OF_STREAM;
    }
#ifdef USE_OUTPUT_RELEASE
    else if (ret == H264DEC_ABORTED)
    {
        DBGT_PDEBUG("H264DecNextPicture: aborted");
        DBGT_EPILOG("");
        return CODEC_ABORTED;
    }
    else if (ret == H264DEC_FLUSHED)
    {
        DBGT_PDEBUG("H264DecNextPicture: flushed");
        DBGT_EPILOG("");
        return CODEC_FLUSHED;
    }
#endif

    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

static OMX_S32 decoder_scanframe_h264(CODEC_PROTOTYPE * arg, STREAM_BUFFER * buf,
                                  OMX_U32 * first, OMX_U32 * last)
{
    UNUSED_PARAMETER(arg);
    DBGT_PROLOG("");

    // find the first and last start code offsets.
    // returns 1 if start codes are found otherwise -1
    // this doesnt find anything if there's only a single NAL unit in the buffer?

    *first = 0;
    *last = 0;
#ifdef USE_SCANFRAME
    // scan for a NAL
    OMX_S32 i = 0;
    OMX_S32 z = 0;

    CALLSTACK;

    for(; i < (OMX_S32)buf->streamlen; ++i)
    {
        if (!buf->bus_data[i])
            ++z;
        else if (buf->bus_data[i] == 0x01 && z >= 2)
        {
            *first = i - z;
            break;
        }
        else
            z = 0;
    }
    for(i = buf->streamlen - 3; i >= 0; --i)
    {
        if (buf->bus_data[i] == 0 && buf->bus_data[i + 1] == 0
           && buf->bus_data[i + 2] == 1)
        {
            /* Check for leading zeros */
            while(i > 0)
            {
                if (buf->bus_data[i])
                {
                    *last = i + 1;
                    break;
                }
                --i;
            }
            DBGT_EPILOG("");
            return 1;
        }
    }
    DBGT_EPILOG("");
    return -1;
#else
        *first = 0;
        *last = buf->streamlen;
        DBGT_EPILOG("");
        return 1;
#endif
}

static CODEC_STATE decoder_setppargs_h264(CODEC_PROTOTYPE * codec,
                                          PP_ARGS * args)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *) codec;

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
                                                        PP_PIPELINED_DEC_TYPE_H264);
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

static CODEC_STATE decoder_endofstream_h264(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_H264 *this = (CODEC_H264 *)arg;
    H264DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = H264DecEndOfStream(this->instance, 1);

    switch (ret)
    {
        case H264DEC_OK:
            stat = CODEC_OK;
            break;
        case H264DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case H264DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case H264DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case H264DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case H264DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case H264DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case H264DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case H264DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case H264DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        case H264DEC_ABORTED:
            stat = CODEC_ABORTED;
            break;
        default:
            DBGT_PDEBUG("H264DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled H264DecRet");
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
CODEC_PROTOTYPE *HantroHwDecOmx_decoder_create_h264(const void *DWLInstance,
                                        OMX_BOOL mvc_stream,
                                        OMX_VIDEO_PARAM_G1CONFIGTYPE *g1Conf)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_H264 *this = OSAL_Malloc(sizeof(CODEC_H264));
    H264DecApiVersion decApi;
    H264DecBuild decBuild;
#ifdef ENABLE_PP
    PPApiVersion ppVer;
    PPBuild ppBuild;
#endif
#ifndef USE_EXTERNAL_BUFFER
    UNUSED_PARAMETER(DWLInstance);
#endif

    memset(this, 0, sizeof(CODEC_H264));

    this->base.destroy = decoder_destroy_h264;
    this->base.decode = decoder_decode_h264;
    this->base.getinfo = decoder_getinfo_h264;
    this->base.getframe = decoder_getframe_h264;
    this->base.scanframe = decoder_scanframe_h264;
    this->base.setppargs = decoder_setppargs_h264;
    this->base.endofstream = decoder_endofstream_h264;
    this->base.pictureconsumed = decoder_pictureconsumed_h264;
    this->base.setframebuffer = decoder_setframebuffer_h264;
#ifdef USE_EXTERNAL_BUFFER
    this->base.getframebufferinfo = decoder_getframebufferinfo_h264;
#endif
    this->base.abort = decoder_abort_h264;
    this->base.abortafter = decoder_abortafter_h264;
    this->base.setnoreorder = decoder_setnoreorder_h264;
    this->base.setinfo = decoder_setinfo_h264;
    this->instance = 0;
    this->picId = 0;
    this->frame_sent = OMX_FALSE;

    /* Print API version number */
    decApi = H264DecGetAPIVersion();
    decBuild = H264DecGetBuild();
    DBGT_PDEBUG("X170 H.264 Decoder API v%d.%d - SW build: %d.%d - HW build: %x",
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
            ppVer.major, ppVer.minor, ppBuild.swBuild >> 16,
            ppBuild.swBuild & 0xFFFF, ppBuild.hwBuild);
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

    H264DecRet ret = H264DecInit(&this->instance,
#ifdef USE_EXTERNAL_BUFFER
                        DWLInstance,
#endif
                        DISABLE_OUTPUT_REORDER,
                        ERROR_HANDLING, USE_DISPLAY_SMOOTHING,
                        dpbFlags,
                        g1Conf->bEnableAdaptiveBuffers,
                        g1Conf->nGuardSize,
                        g1Conf->bEnableSecureMode,
                        &dscale_cfg);

    if (ret == H264DEC_OK && mvc_stream)
        ret = H264DecSetMvc(this->instance);
#endif

#ifdef IS_8190
    H264DecRet ret = H264DecInit(&this->instance, DISABLE_OUTPUT_REORDER,
                        USE_VIDEO_FREEZE_CONCEALMENT, USE_DISPLAY_SMOOTHING);
#endif

#if !defined (IS_8190) && !defined (IS_G1_DECODER)
    H264DecRet ret = H264DecInit(&this->instance, DISABLE_OUTPUT_REORDER);
#endif

    if (ret != H264DEC_OK)
    {
        decoder_destroy_h264((CODEC_PROTOTYPE *) this);
        DBGT_CRITICAL("H264DecInit error");
        DBGT_EPILOG("");
        return NULL;
    }

    DBGT_EPILOG("");
    return (CODEC_PROTOTYPE *) this;
}

#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_h264(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *)arg;
    H264DecBufferInfo info;
    FRAME_BUFFER_INFO bufInfo;
    memset(&bufInfo, 0, sizeof(FRAME_BUFFER_INFO));

    H264DecGetBufferInfo(this->instance, &info);

    bufInfo.bufferSize = info.next_buf_size;
    bufInfo.numberOfBuffers = info.buf_num;
    DBGT_PDEBUG("bufferSize %d, numberOfBuffers %d", (int)bufInfo.bufferSize, (int)bufInfo.numberOfBuffers);

    DBGT_EPILOG("");
    return bufInfo;
}
#endif

CODEC_STATE decoder_setframebuffer_h264(CODEC_PROTOTYPE * arg, BUFFER *buff, OMX_U32 available_buffers)
{
    CALLSTACK;

    DBGT_PROLOG("");
#ifdef USE_EXTERNAL_BUFFER
    UNUSED_PARAMETER(available_buffers);
    CODEC_H264 *this = (CODEC_H264 *)arg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;
    struct DWLLinearMem mem;
    H264DecBufferInfo info;
    H264DecRet ret;

    memset(&info, 0, sizeof(H264DecBufferInfo));

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
    //mem.size = NEXT_MULTIPLE(buff->allocsize, page_size);   /* physical size (rounded to page multiple) */
    //mem.logical_size = buff->allocsize;                     /* requested size in bytes */
    DBGT_PDEBUG("virtual_address %p, bus_address %lu, size %d",
    mem.virtual_address, mem.bus_address, mem.size);

    ret = H264DecAddBuffer(this->instance, &mem);
    DBGT_PDEBUG("H264DecAddBuffer ret (%d)", ret);

    switch (ret)
    {
        case H264DEC_OK:
            stat = CODEC_OK;
            break;
        case H264DEC_WAITING_FOR_BUFFER:
            stat = CODEC_NEED_MORE;
            break;
        case H264DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case H264DEC_MEMFAIL:
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

CODEC_STATE decoder_pictureconsumed_h264(CODEC_PROTOTYPE * arg, BUFFER *buff)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_H264 *this = (CODEC_H264 *)arg;
    H264DecPicture pic;
    OMX_U32 i, j;

    DBGT_PDEBUG("Consumed: bus_address %lu", buff->bus_address);

    for (i=0; i<MAX_BUFFERS; i++)
    {
        if (buff->bus_address == this->out_pic[i].output_picture_bus_address)
        {
            DBGT_PDEBUG("Found out_pic[%d]: bus_address %lu", (int)i, buff->bus_address);
            pic = this->out_pic[i];
            H264DecRet ret = H264DecPictureConsumed(this->instance, &pic);
            DBGT_PDEBUG("H264DecPictureConsumed ret (%d)", ret);
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


CODEC_STATE decoder_abort_h264(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_H264 *this = (CODEC_H264 *)arg;
    H264DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = H264DecAbort(this->instance);

    switch (ret)
    {
        case H264DEC_OK:
            stat = CODEC_OK;
            break;
        case H264DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case H264DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case H264DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case H264DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case H264DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case H264DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case H264DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case H264DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case H264DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("H264DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled H264DecRet");
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

CODEC_STATE decoder_abortafter_h264(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_H264 *this = (CODEC_H264 *)arg;
    H264DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = H264DecAbortAfter(this->instance);

    switch (ret)
    {
        case H264DEC_OK:
            stat = CODEC_OK;
            break;
        case H264DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case H264DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case H264DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case H264DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case H264DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case H264DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case H264DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case H264DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case H264DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("H264DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled H264DecRet");
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


CODEC_STATE decoder_setnoreorder_h264(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *)arg;
    H264DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = H264DecSetNoReorder(this->instance, no_reorder);

    switch (ret)
    {
        case H264DEC_OK:
            stat = CODEC_OK;
            break;
        case H264DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case H264DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case H264DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case H264DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case H264DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case H264DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case H264DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case H264DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case H264DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("H264DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled H264DecRet");
            break;
    }

    DBGT_EPILOG("");
    return stat;
}

CODEC_STATE decoder_setinfo_h264(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_H264 *this = (CODEC_H264 *)arg;
    H264DecRet ret;
    H264DecConfig dec_cfg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    memset(&dec_cfg, 0, sizeof(H264DecConfig));
    if (conf->g1_conf.bEnableTiled)
        dec_cfg.dpb_flags = DEC_REF_FRM_TILED_DEFAULT;
    else
        dec_cfg.dpb_flags = DEC_REF_FRM_RASTER_SCAN;

    if (conf->g1_conf.bAllowFieldDBP)
        dec_cfg.dpb_flags |= DEC_DPB_ALLOW_FIELD_ORDERING;

    DBGT_PDEBUG("dpbFlags 0x%x", dec_cfg.dpb_flags);
    dec_cfg.use_adaptive_buffers = conf->g1_conf.bEnableAdaptiveBuffers;
    dec_cfg.guard_size = conf->g1_conf.nGuardSize;
    dec_cfg.use_secure_mode = conf->g1_conf.bEnableSecureMode;
    ret = H264DecSetInfo(this->instance, &dec_cfg);

    switch (ret)
    {
        case H264DEC_OK:
            stat = CODEC_OK;
            break;
        case H264DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case H264DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case H264DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case H264DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case H264DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case H264DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case H264DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case H264DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case H264DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("H264DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled H264DecRet");
            break;
    }

    DBGT_EPILOG("");
    return stat;
}
