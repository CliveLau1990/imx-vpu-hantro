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
#include "vp9decapi.h"
#include "OSAL.h"
#include "port.h"
#include "codec_vp9.h"
#include "util.h"
#include "dbgtrace.h"
#include "post_processor.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX VP9"

#define FRAME_BUFFERS 6
#define DEC_DBP_FLAGS 0
#define MAX_BUFFERS 32

typedef struct CODEC_VP9
{
    CODEC_PROTOTYPE base;
    OMX_U32 framesize;
    Vp9DecInst instance;
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
    OMX_U32 consumed;
    OMX_U32 frame_count;
    struct Vp9DecPicture out_pic[MAX_BUFFERS];
} CODEC_VP9;

CODEC_STATE decoder_setframebuffer_vp9(CODEC_PROTOTYPE * arg, BUFFER *buff,
                                        OMX_U32 available_buffers);
CODEC_STATE decoder_pictureconsumed_vp9(CODEC_PROTOTYPE * arg, BUFFER *buff);
#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_vp9(CODEC_PROTOTYPE * arg);
#endif
CODEC_STATE decoder_abort_vp9(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_abortafter_vp9(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_setnoreorder_vp9(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder);
CODEC_STATE decoder_setinfo_vp9(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf);
// destroy codec instance
static void decoder_destroy_vp9(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *) arg;

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

        if (this->instance)
        {
            Vp9DecRelease(this->instance);
            this->instance = 0;
        }

        OSAL_Free(this);
    }
    DBGT_EPILOG("");
}

/* function copied from the libvpx */
static void ParseSuperframeIndex(const OMX_U8* data, size_t data_sz, OMX_U32 sizes[8],
                                 OMX_S32* count) {
    OMX_U8 marker;

    marker = data[data_sz - 1];
    *count = 0;

    if ((marker & 0xe0) == 0xc0) {
        const OMX_U32 frames = (marker & 0x7) + 1;
        const OMX_U32 mag = ((marker >> 3) & 0x3) + 1;
        const OMX_U32 index_sz = 2 + mag * frames;

        if (data_sz >= index_sz && data[data_sz - index_sz] == marker) {
            DBGT_PDEBUG("\nPARSE SUPER FRAME");
            DBGT_PDEBUG("Found valid superframe index");
            OMX_U32 i, j;
            const OMX_U8* x = data + data_sz - index_sz + 1;

            for (i = 0; i < frames; i++) {
                OMX_U32 this_sz = 0;

                for (j = 0; j < mag; j++) this_sz |= (*x++) << (j * 8);
                    sizes[i] = this_sz;
            }

            *count = frames;
        }
    }
}

// try to consume stream data
static CODEC_STATE decoder_decode_vp9(CODEC_PROTOTYPE * arg,
                                       STREAM_BUFFER * buf, OMX_U32 * consumed,
                                       FRAME * frame)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *) arg;

    DBGT_ASSERT(this);
    DBGT_ASSERT(this->instance);
    DBGT_ASSERT(buf);
    DBGT_ASSERT(consumed);

    enum DecRet ret = DEC_OK;
    struct Vp9DecInput input;
    struct Vp9DecOutput output;
    OMX_U32 sizes[8] = {0};
    OMX_S32 frames_this_pts;

    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));

    input.stream = buf->bus_data;
    input.stream_bus_address = buf->bus_address;
    input.data_len = buf->streamlen;
    input.pic_id = buf->picId;

    input.buffer = buf->buf_data;
    input.buffer_bus_address = buf->buf_address;
    input.buff_len = buf->allocsize;

    OMX_U32 data_sz = input.data_len;
    const OMX_U8* data_start = input.stream;
    const OMX_U8* data_begin = input.stream;
    const OMX_U8* data_end = data_start + data_sz;
    DBGT_PDEBUG("Input size %d", input.data_len);

    ParseSuperframeIndex(input.stream, data_sz, sizes, &frames_this_pts);

    do {
        if (data_sz && (*data_start & 0xe0) == 0xc0) {
            const OMX_U8 marker = *data_start;
            const OMX_U32 frames = (marker & 0x7) + 1;
            const OMX_U32 mag = ((marker >> 3) & 0x3) + 1;
            const OMX_U32 index_sz = 2 + mag * frames;

            if (data_sz >= index_sz && data_start[index_sz - 1] == marker) {
                data_start += index_sz;
                data_sz -= index_sz;
                if (data_start >= data_end)
                    continue;
                else
                    break;
            }
        }

        /* Use the correct size for this frame, if an index is present. */
        if (frames_this_pts) {
            OMX_U32 this_sz = sizes[this->frame_count];

            if (data_sz < this_sz) {
                DBGT_CRITICAL("Invalid frame size in index");
                *consumed = buf->streamlen;
                return CODEC_ERROR_STREAM;
            }

            data_sz = this_sz;
            this->frame_count++;
        }

        input.stream_bus_address =
            buf->bus_address + (OMX_U32)(data_start - data_begin);
        input.stream = (u8*)data_start;
        input.data_len = data_sz;

        DBGT_PDEBUG("Pic id %d, stream length %d, frame count %d, stream_bus_address: %p",
                    (int)this->picId, input.data_len, (int)this->frame_count, input.stream_bus_address);
        *consumed = 0;
        frame->size = 0;
        this->pending_flush = OMX_FALSE;

        ret = Vp9DecDecode(this->instance, &input, &output);

        DBGT_PDEBUG("Vp9DecDecode ret %d", ret);
        if (ret == DEC_HDRS_RDY || ret != DEC_PIC_DECODED)
            break;

        if (ret == DEC_PIC_DECODED)
            this->picId++;

        data_start += data_sz;

        while (data_start < data_end && *data_start == 0) data_start++;

        data_sz = data_end - data_start;

    } while (data_start < data_end);

    switch (ret)
    {
    case DEC_PENDING_FLUSH:
        DBGT_PDEBUG("Pending flush");
        //this->picId++;
        this->pending_flush = OMX_TRUE;
        stat = CODEC_PENDING_FLUSH;
        break;
    case DEC_PIC_DECODED:
        DBGT_PDEBUG("Pic decoded");
        stat = CODEC_HAS_FRAME;
        break;
    case DEC_HDRS_RDY:
        DBGT_PDEBUG("Headers ready");
        stat = CODEC_HAS_INFO;
        break;
    case DEC_ADVANCED_TOOLS:
        stat = CODEC_NEED_MORE;
        break;
    case DEC_STRM_PROCESSED:
        stat = CODEC_NEED_MORE;
        break;
    case DEC_NONREF_PIC_SKIPPED:
        DBGT_PDEBUG("Nonreference picture skipped");
        this->picId++;
        stat = CODEC_PIC_SKIPPED;
        break;
#ifdef USE_EXTERNAL_BUFFER
    case DEC_WAITING_FOR_BUFFER:
    {
        DBGT_PDEBUG("Waiting for frame buffer");
#if 0
        struct Vp9DecBufferInfo info;
        enum DecRet rv;
        rv = Vp9DecGetBufferInfo(this->instance, &info);
        //DBGT_PDEBUG("Vp9DecGetBufferInfo ret %d", rv);
        UNUSED_PARAMETER(rv);
        DBGT_PDEBUG("Buffer size %d, number of buffers %d",
            info.next_buf_size, info.buf_num);
#endif
        stat = CODEC_WAITING_FRAME_BUFFER;
    }
        break;
    case DEC_ABORTED:
        DBGT_PDEBUG("Decoding aborted");
        *consumed = buf->streamlen;
        DBGT_EPILOG("");
        return CODEC_ABORTED;
#endif
    case DEC_NO_DECODING_BUFFER:
        this->frame_count--;
        *consumed = data_start - data_begin;
        stat = CODEC_NO_DECODING_BUFFER;
        break;
    case DEC_PARAM_ERROR:
        stat = CODEC_ERROR_INVALID_ARGUMENT;
        break;
    case DEC_STRM_ERROR:
        stat = CODEC_ERROR_STREAM;
        break;
    case DEC_NOT_INITIALIZED:
        stat = CODEC_ERROR_NOT_INITIALIZED;
        break;
    case DEC_HW_BUS_ERROR:
        stat = CODEC_ERROR_HW_BUS_ERROR;
        break;
    case DEC_HW_TIMEOUT:
        stat = CODEC_ERROR_HW_TIMEOUT;
        break;
    case DEC_SYSTEM_ERROR:
        stat = CODEC_ERROR_SYS;
        break;
    case DEC_HW_RESERVED:
        stat = CODEC_ERROR_HW_RESERVED;
        break;
    case DEC_MEMFAIL:
        stat = CODEC_ERROR_MEMFAIL;
        break;
    case DEC_STREAM_NOT_SUPPORTED:
        stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
        break;
    case DEC_FORMAT_NOT_SUPPORTED:
        stat = CODEC_ERROR_FORMAT_NOT_SUPPORTED;
        break;
    default:
        DBGT_ASSERT(!"Unhandled DecRet");
        break;
    }

    if (stat != CODEC_ERROR_UNSPECIFIED && stat != CODEC_NO_DECODING_BUFFER)
    {
        DBGT_PDEBUG("Decoder data left %d", output.data_left);
        if (stat == CODEC_HAS_INFO || stat == CODEC_WAITING_FRAME_BUFFER)
            *consumed = 0;
        else
            *consumed = buf->streamlen;

        this->frame_count = 0;

        DBGT_PDEBUG("Consumed %d\n", (int) *consumed);
    }

    DBGT_EPILOG("");
    return stat;
}

// get stream info
static CODEC_STATE decoder_getinfo_vp9(CODEC_PROTOTYPE * arg,
                                        STREAM_INFO * pkg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *) arg;
    struct Vp9DecInfo decinfo;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(pkg);

    memset(&decinfo, 0, sizeof(decinfo));

    // the headers are ready, get stream information
    enum DecRet ret = Vp9DecGetInfo(this->instance, &decinfo);

    if (ret == DEC_OK)
    {
        if ((decinfo.frame_width * decinfo.frame_height) > MAX_VIDEO_RESOLUTION)
        {
            DBGT_ERROR("Video stream resolution exceeds the supported video resolution");
            DBGT_EPILOG("");
            return CODEC_ERROR_STREAM_NOT_SUPPORTED;
        }
        DBGT_PDEBUG("VP9 version %d, profile %d", decinfo.vp_version, decinfo.vp_profile);

        if (decinfo.output_format == DEC_OUT_FRM_TILED_4X4)
            pkg->format = OMX_COLOR_FormatYUV420SemiPlanar4x4Tiled;
        else if ((decinfo.pixel_format == DEC_OUT_PIXEL_P010) && (decinfo.bit_depth > 8))
            pkg->format = OMX_COLOR_FormatYUV420SemiPlanarP010;
        else
            pkg->format = OMX_COLOR_FormatYUV420PackedSemiPlanar;

        pkg->width = decinfo.frame_width;//decinfo.cropParams.cropOutWidth;
        pkg->height = decinfo.frame_height;//decinfo.cropParams.cropOutHeight;
        pkg->sliceheight = decinfo.frame_height;
        pkg->stride = decinfo.pic_stride;
        pkg->bit_depth = decinfo.bit_depth;
        DBGT_PDEBUG("Dec info stride %d", decinfo.pic_stride);
        DBGT_PDEBUG("Dec info bit_depth %d", decinfo.bit_depth);
        DBGT_PDEBUG("outf %u pixf %u", decinfo.output_format, decinfo.pixel_format);
#ifdef SET_OUTPUT_CROP_RECT
        pkg->crop_available = OMX_FALSE;
        pkg->crop_left = 0;
        pkg->crop_top = 0;
        pkg->crop_width = decinfo.coded_width;
        pkg->crop_height = decinfo.coded_height;
        if ((pkg->crop_left != 0) || (pkg->crop_top != 0) ||
            (pkg->crop_width != pkg->width) ||
            (pkg->crop_height != pkg->height))
        {
            pkg->crop_available = OMX_TRUE;
        }
        DBGT_PDEBUG("Crop left %d, top %d, width %d, height %d", (int)pkg->crop_left,
                (int)pkg->crop_top, (int)pkg->crop_width, (int)pkg->crop_height);
#endif

        pkg->interlaced = 0;

#ifdef USE_EXTERNAL_BUFFER
        struct Vp9DecBufferInfo bufInfo;

        Vp9DecGetBufferInfo(this->instance, &bufInfo);

        pkg->framesize = bufInfo.next_buf_size;
        pkg->frame_buffers = bufInfo.buf_num;
        DBGT_PDEBUG("Required number of frame buffers %lu", pkg->frame_buffers);
#else
        pkg->frame_buffers = decinfo.pic_buff_size;
        pkg->framesize = (pkg->sliceheight * pkg->stride) * 3 / 2;
#endif
        this->framesize = pkg->framesize;

        DBGT_EPILOG("");
        return CODEC_OK;
    }
    else if (ret == DEC_PARAM_ERROR)
    {
        DBGT_CRITICAL("VP9DEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }
    else if (ret == DEC_HDRS_NOT_RDY)
    {
        DBGT_CRITICAL("VP9DEC_HDRS_NOT_RDY");
        DBGT_EPILOG("");
        return CODEC_ERROR_STREAM;
    }

    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

// get decoded frame
static CODEC_STATE decoder_getframe_vp9(CODEC_PROTOTYPE * arg, FRAME * frame,
                                         OMX_BOOL eos)
{
    CALLSTACK;
    UNUSED_PARAMETER(eos);
    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *) arg;
    struct Vp9DecPicture picture;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(frame);

    memset(&picture, 0, sizeof(picture));

    //u32 endofStream = eos == OMX_TRUE ? 1 : 0;

    enum DecRet ret = Vp9DecNextPicture(this->instance, &picture);
        DBGT_PDEBUG("Vp9DecNextPicture ret %d", ret);
    this = (CODEC_VP9 *) arg;

    if (ret == DEC_PIC_RDY)
    {
        DBGT_ASSERT(this->framesize);

        //DBGT_PDEBUG("End of stream %d", endofStream);
        DBGT_PDEBUG("Number of concealed MB's %d", picture.nbr_of_err_mbs);
        DBGT_PDEBUG("Display ID %d", picture.pic_id);
        DBGT_PDEBUG("Frame dimensions %dx%d", picture.frame_width, picture.frame_height);
        DBGT_PDEBUG("Coded dimensions %dx%d", picture.coded_width, picture.coded_height);
        DBGT_PDEBUG("Stride %d", picture.pic_stride);
        DBGT_PDEBUG("Bit depth luma %d", picture.bit_depth_luma);
        DBGT_PDEBUG("Bit depth chroma %d", picture.bit_depth_chroma);
        DBGT_PDEBUG("outf %u pixf %u", picture.output_format, picture.pixel_format);

        frame->fb_bus_address = picture.output_luma_bus_address;
        frame->fb_bus_data = (u8*)picture.output_luma_base;
        frame->outBufPrivate.pLumaBase = (u8*)picture.output_luma_base;
        frame->outBufPrivate.nLumaBusAddress = picture.output_luma_bus_address;
        frame->outBufPrivate.nLumaSize = picture.pic_stride * picture.frame_height;
        frame->outBufPrivate.pChromaBase = (u8*)picture.output_chroma_base;
        frame->outBufPrivate.nChromaBusAddress = picture.output_chroma_bus_address;
        frame->outBufPrivate.nChromaSize = frame->outBufPrivate.nLumaSize / 2;
        frame->outBufPrivate.nFrameWidth = picture.frame_width;
        frame->outBufPrivate.nFrameHeight = picture.frame_height;
        frame->outBufPrivate.nPicId[0] = frame->outBufPrivate.nPicId[1] = picture.decode_id;
        DBGT_PDEBUG("Vp9DecNextPicture: output_luma_bus_address %lu", picture.output_luma_bus_address);
        DBGT_PDEBUG("Vp9DecNextPicture: output_chroma_bus_address %lu", picture.output_chroma_bus_address);
        DBGT_PDEBUG("output_luma_base %p, output_chroma_base %p", picture.output_luma_base, picture.output_chroma_base);
        DBGT_PDEBUG("Luma size %lu", frame->outBufPrivate.nLumaSize);
        DBGT_PDEBUG("Chroma size %lu", frame->outBufPrivate.nChromaSize);

        frame->outBufPrivate.sRfcTable.pLumaBase = (u8*)picture.output_rfc_luma_base;
        frame->outBufPrivate.sRfcTable.nLumaBusAddress = picture.output_rfc_luma_bus_address;
        frame->outBufPrivate.sRfcTable.pChromaBase = (u8*)picture.output_rfc_chroma_base;
        frame->outBufPrivate.sRfcTable.nChromaBusAddress = picture.output_rfc_chroma_bus_address;
        DBGT_PDEBUG("RFC: luma base %p, chroma base %p", picture.output_rfc_luma_base, picture.output_rfc_chroma_base);

        frame->size = (picture.pic_stride * picture.frame_height * 3) / 2;
        frame->MB_err_count = picture.nbr_of_err_mbs;
        frame->viewId = 0;

        //this->out_pic[this->out_count % MAX_BUFFERS] = picture;
        this->out_pic[this->out_index_w] = picture;
        this->out_count++;
        this->out_index_w++;
        if (this->out_index_w == MAX_BUFFERS) this->out_index_w = 0;
        this->out_num++;

        DBGT_EPILOG("");
        return CODEC_HAS_FRAME;
    }
    else if (ret == DEC_OK)
    {
        DBGT_EPILOG("");
        return CODEC_OK;
    }
    else if (ret == DEC_PARAM_ERROR)
    {
        DBGT_CRITICAL("DEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }
    else if (ret == DEC_END_OF_STREAM)
    {
        DBGT_PDEBUG("Vp9DecNextPicture: End of stream");
        DBGT_EPILOG("");
        return CODEC_END_OF_STREAM;
    }
    else if (ret == DEC_ABORTED)
    {
        DBGT_PDEBUG("Vp9DecNextPicture: aborted");
        DBGT_EPILOG("");
        return CODEC_ABORTED;
    }
    else if (ret == DEC_FLUSHED)
    {
        DBGT_PDEBUG("Vp9DecNextPicture: flushed");
        DBGT_EPILOG("");
        return CODEC_FLUSHED;
    }

    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

static OMX_S32 decoder_scanframe_vp9(CODEC_PROTOTYPE * arg, STREAM_BUFFER * buf,
                                  OMX_U32 * first, OMX_U32 * last)
{
    UNUSED_PARAMETER(arg);
    DBGT_PROLOG("");

    // find the first and last start code offsets.
    // returns 1 if start codes are found otherwise -1
    // this doesnt find anything if there's only a single NAL unit in the buffer?

    *first = 0;
    *last = buf->streamlen;
    DBGT_EPILOG("");
    return 1;
}

static CODEC_STATE decoder_setppargs_vp9(CODEC_PROTOTYPE * codec,
                                          PP_ARGS * args)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *) codec;

    DBGT_ASSERT(this);
    DBGT_ASSERT(args);

    this->pp_state = PP_DISABLED;
    DBGT_EPILOG("");
    return CODEC_OK;
}

static CODEC_STATE decoder_endofstream_vp9(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *)arg;
    enum DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = Vp9DecEndOfStream(this->instance);

    switch (ret)
    {
        case DEC_OK:
            stat = CODEC_OK;
            break;
        case DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        case DEC_ABORTED:
            stat = CODEC_ABORTED;
            break;
        default:
            DBGT_PDEBUG("DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled DecRet");
            break;
    }

    DBGT_EPILOG("");
    return stat;
}

// create codec instance and initialize it
CODEC_PROTOTYPE *HantroHwDecOmx_decoder_create_vp9(const void *DWLInstance,
                                        OMX_VIDEO_PARAM_G2CONFIGTYPE *g2Conf)
{
    CALLSTACK;

    DBGT_PROLOG("");
    DBGT_ASSERT(DWLInstance);

    CODEC_VP9 *this = OSAL_Malloc(sizeof(CODEC_VP9));
    //Vp9DecApiVersion decApi;
    Vp9DecBuild decBuild;

    memset(this, 0, sizeof(CODEC_VP9));

    this->base.destroy = decoder_destroy_vp9;
    this->base.decode = decoder_decode_vp9;
    this->base.getinfo = decoder_getinfo_vp9;
    this->base.getframe = decoder_getframe_vp9;
    this->base.scanframe = decoder_scanframe_vp9;
    this->base.setppargs = decoder_setppargs_vp9;
    this->base.endofstream = decoder_endofstream_vp9;
    this->base.pictureconsumed = decoder_pictureconsumed_vp9;
    this->base.setframebuffer = decoder_setframebuffer_vp9;
#ifdef USE_EXTERNAL_BUFFER
    this->base.getframebufferinfo = decoder_getframebufferinfo_vp9;
#endif
    this->base.abort = decoder_abort_vp9;
    this->base.abortafter = decoder_abortafter_vp9;
    this->base.setnoreorder = decoder_setnoreorder_vp9;
    this->base.setinfo = decoder_setinfo_vp9;
    this->instance = 0;
    this->picId = 0;
    this->frame_sent = OMX_FALSE;

    /* Print API version number */
    //decApi = Vp9DecGetAPIVersion();
    decBuild = Vp9DecGetBuild();
    DBGT_PDEBUG("G2 VP9 Decoder SW build: %d.%d - HW build: %x",
            decBuild.sw_build >> 16,
            decBuild.sw_build & 0xFFFF, decBuild.hw_build);
    UNUSED_PARAMETER(decBuild);

#ifdef VSI_API
    struct Vp9DecConfig dec_cfg;
    memset(&dec_cfg, 0, sizeof(dec_cfg));

    dec_cfg.use_video_freeze_concealment = ERROR_HANDLING;
    dec_cfg.use_video_compressor =  g2Conf->bEnableRFC;
    dec_cfg.use_ringbuffer =  g2Conf->bEnableRingBuffer;
    if (g2Conf->bEnableTiled)
        dec_cfg.output_format = DEC_OUT_FRM_TILED_4X4;
    else
        dec_cfg.output_format = DEC_OUT_FRM_RASTER_SCAN;
    dec_cfg.pixel_format = g2Conf->ePixelFormat;
    dec_cfg.num_frame_buffers = FRAME_BUFFERS;
#ifdef DOWN_SCALER
    dec_cfg.dscale_cfg.down_scale_x = 1;
    dec_cfg.dscale_cfg.down_scale_y = 1;
#endif
    DBGT_PDEBUG("Output format %u, pixel format %u, RFC %u",
        dec_cfg.output_format, dec_cfg.pixel_format, dec_cfg.use_video_compressor);
    enum DecRet ret = Vp9DecInit(&this->instance, DWLInstance, &dec_cfg);
#else
    enum DecRet ret = Vp9DecInit(&this->instance, ERROR_HANDLING,
                      FRAME_BUFFERS, DEC_DBP_FLAGS, DEC_OUT_FRM_RASTER_SCAN);
#endif

    if (ret != DEC_OK)
    {
        decoder_destroy_vp9((CODEC_PROTOTYPE *) this);
        DBGT_CRITICAL("Vp9DecInit error");
        DBGT_EPILOG("");
        return NULL;
    }

    DBGT_EPILOG("");
    return (CODEC_PROTOTYPE *) this;
}

#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_vp9(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *)arg;
    struct Vp9DecBufferInfo info;
    FRAME_BUFFER_INFO bufInfo;

    Vp9DecGetBufferInfo(this->instance, &info);

    bufInfo.bufferSize = info.next_buf_size;
    bufInfo.numberOfBuffers = info.buf_num;
    bufInfo.fb_bus_address = info.buf_to_free.bus_address;
    DBGT_PDEBUG("bufferSize %d, numberOfBuffers %d", (int)bufInfo.bufferSize, (int)bufInfo.numberOfBuffers);

    DBGT_EPILOG("");
    return bufInfo;
}
#endif

CODEC_STATE decoder_setframebuffer_vp9(CODEC_PROTOTYPE * arg, BUFFER *buff, OMX_U32 available_buffers)
{
    CALLSTACK;

    DBGT_PROLOG("");
#ifdef USE_EXTERNAL_BUFFER
    CODEC_VP9 *this = (CODEC_VP9 *)arg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;
    struct DWLLinearMem mem;
    struct Vp9DecBufferInfo info;
    enum DecRet ret;
    const int page_size = getpagesize();

    memset(&info, 0, sizeof(struct Vp9DecBufferInfo));

    ret = Vp9DecGetBufferInfo(this->instance, &info);

    if (info.buf_num > available_buffers)
    {
        DBGT_CRITICAL("Not enough frame buffers available. Required %d > available %d",
            (int)info.buf_num, (int)available_buffers);
        DBGT_EPILOG("");
        return CODEC_ERROR_NOT_ENOUGH_FRAME_BUFFERS;
    }
    if (info.next_buf_size > buff->allocsize)
    {
        DBGT_CRITICAL("Buffer size error. Required size %d > allocated size %d",
            (int)info.next_buf_size, (int)buff->allocsize);
        DBGT_EPILOG("");
        return CODEC_ERROR_BUFFER_SIZE;
    }

    mem.virtual_address = (u32*)buff->bus_data;
    mem.bus_address = buff->bus_address;
    mem.size = NEXT_MULTIPLE(buff->allocsize, page_size);   /* physical size (rounded to page multiple) */
    mem.logical_size = buff->allocsize;                     /* requested size in bytes */
    DBGT_PDEBUG("virtual_address %p, bus_address %lu, size %d, logical_size %d",
    mem.virtual_address, mem.bus_address, mem.size, mem.logical_size);

    ret = Vp9DecAddBuffer(this->instance, &mem);
    DBGT_PDEBUG("Vp9DecAddBuffer ret (%d)", ret);

    switch (ret)
    {
        case DEC_OK:
            stat = CODEC_OK;
            break;
        case DEC_WAITING_FOR_BUFFER:
            stat = CODEC_NEED_MORE;
            break;
        case DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case DEC_MEMFAIL:
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

CODEC_STATE decoder_pictureconsumed_vp9(CODEC_PROTOTYPE * arg, BUFFER *buff)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *)arg;
    struct Vp9DecPicture pic;
    OMX_U32 i, j;

    UNUSED_PARAMETER(buff);

    DBGT_PDEBUG("Consumed: bus_address %lu", buff->bus_address);

    for (i=0; i<MAX_BUFFERS; i++)
    {
        if (buff->bus_address == this->out_pic[i].output_luma_bus_address)
        {
            DBGT_PDEBUG("Found out_pic[%d]: bus_address %lu", (int)i, buff->bus_address);
            pic = this->out_pic[i];
            enum DecRet ret = Vp9DecPictureConsumed(this->instance, &pic);
            DBGT_PDEBUG("Vp9DecPictureConsumed ret (%d)", ret);
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

    DBGT_EPILOG("");
    return CODEC_OK;
}


CODEC_STATE decoder_abort_vp9(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_EXTERNAL_BUFFER
    CODEC_VP9 *this = (CODEC_VP9 *)arg;
    enum DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = Vp9DecAbort(this->instance);

    switch (ret)
    {
        case DEC_OK:
            stat = CODEC_OK;
            break;
        case DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled DecRet");
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

CODEC_STATE decoder_abortafter_vp9(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_EXTERNAL_BUFFER
    CODEC_VP9 *this = (CODEC_VP9 *)arg;
    enum DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = Vp9DecAbortAfter(this->instance);

    switch (ret)
    {
        case DEC_OK:
            stat = CODEC_OK;
            break;
        case DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled DecRet");
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


CODEC_STATE decoder_setnoreorder_vp9(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder)
{
    CALLSTACK;

    DBGT_PROLOG("");
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(no_reorder);
    DBGT_EPILOG("");
    return CODEC_OK;
}

CODEC_STATE decoder_setinfo_vp9(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_VP9 *this = (CODEC_VP9 *)arg;
    enum DecRet ret;
    struct Vp9DecConfig dec_cfg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    memset(&dec_cfg, 0, sizeof(struct Vp9DecConfig));
    dec_cfg.use_video_freeze_concealment = ERROR_HANDLING;
    dec_cfg.use_video_compressor = conf->g2_conf.bEnableRFC;
    dec_cfg.use_ringbuffer = conf->g2_conf.bEnableRingBuffer;
    dec_cfg.use_fetch_one_pic = conf->g2_conf.bEnableFetchOnePic;
    if (conf->g2_conf.bEnableTiled)
        dec_cfg.output_format = DEC_OUT_FRM_TILED_4X4;
    else
        dec_cfg.output_format = DEC_OUT_FRM_RASTER_SCAN;
    dec_cfg.pixel_format = conf->g2_conf.ePixelFormat;
    dec_cfg.num_frame_buffers = FRAME_BUFFERS;
#ifdef DOWN_SCALER
    dec_cfg.dscale_cfg.down_scale_x = 1;
    dec_cfg.dscale_cfg.down_scale_y = 1;
#endif
    ret = Vp9DecSetInfo(this->instance, &dec_cfg);

    switch (ret)
    {
        case DEC_OK:
            stat = CODEC_OK;
            break;
        case DEC_PARAM_ERROR:
            stat = CODEC_ERROR_INVALID_ARGUMENT;
            break;
        case DEC_STRM_ERROR:
            stat = CODEC_ERROR_STREAM;
            break;
        case DEC_NOT_INITIALIZED:
            stat = CODEC_ERROR_NOT_INITIALIZED;
            break;
        case DEC_HW_BUS_ERROR:
            stat = CODEC_ERROR_HW_BUS_ERROR;
            break;
        case DEC_HW_TIMEOUT:
            stat = CODEC_ERROR_HW_TIMEOUT;
            break;
        case DEC_SYSTEM_ERROR:
            stat = CODEC_ERROR_SYS;
            break;
        case DEC_HW_RESERVED:
            stat = CODEC_ERROR_HW_RESERVED;
            break;
        case DEC_MEMFAIL:
            stat = CODEC_ERROR_MEMFAIL;
            break;
        case DEC_STREAM_NOT_SUPPORTED:
            stat = CODEC_ERROR_STREAM_NOT_SUPPORTED;
            break;
        default:
            DBGT_PDEBUG("DecRet (%d)", ret);
            DBGT_ASSERT(!"unhandled DecRet");
            break;
    }
    DBGT_EPILOG("");
    return stat;
}
