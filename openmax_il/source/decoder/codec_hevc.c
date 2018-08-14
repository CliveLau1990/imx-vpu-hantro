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
#include "hevcdecapi.h"
#include "OSAL.h"
#include "codec_hevc.h"
#include "util.h"
#include "dbgtrace.h"
#include "post_processor.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX HEVC"

#define DISABLE_OUTPUT_REORDER 0
#define MAX_BUFFERS 34

typedef struct CODEC_HEVC
{
    CODEC_PROTOTYPE base;
    OMX_U32 framesize;
    HevcDecInst instance;
    PP_STATE pp_state;
    OMX_U32 picId;
    //OMX_BOOL extraEosLoopDone;
    OMX_BOOL update_pp_out;
    OMX_BOOL frame_sent;
    OMX_BOOL pending_flush;
    OMX_U32 out_count;
    OMX_U32 consumed;
    OMX_U32 out_index_r;
    OMX_U32 out_index_w;
    OMX_U32 out_num;
    struct HevcDecPicture out_pic[MAX_BUFFERS];
} CODEC_HEVC;

CODEC_STATE decoder_setframebuffer_hevc(CODEC_PROTOTYPE * arg, BUFFER *buff,
                                        OMX_U32 available_buffers);
CODEC_STATE decoder_pictureconsumed_hevc(CODEC_PROTOTYPE * arg, BUFFER *buff);
#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_hevc(CODEC_PROTOTYPE * arg);
#endif
CODEC_STATE decoder_abort_hevc(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_abortafter_hevc(CODEC_PROTOTYPE * arg);
CODEC_STATE decoder_setnoreorder_hevc(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder);
CODEC_STATE decoder_setinfo_hevc(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf);
// destroy codec instance
static void decoder_destroy_hevc(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *) arg;

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
            HevcDecRelease(this->instance);
            this->instance = 0;
        }

        OSAL_Free(this);
    }
    DBGT_EPILOG("");
}

// try to consume stream data
static CODEC_STATE decoder_decode_hevc(CODEC_PROTOTYPE * arg,
                                       STREAM_BUFFER * buf, OMX_U32 * consumed,
                                       FRAME * frame)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *) arg;

    DBGT_ASSERT(this);
    DBGT_ASSERT(this->instance);
    DBGT_ASSERT(buf);
    DBGT_ASSERT(consumed);

    struct HevcDecInput input;
    struct HevcDecOutput output;

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
    DBGT_PDEBUG("buf->bus_data %p, buf->bus_address %lu", buf->bus_data, buf->bus_address);
    DBGT_PDEBUG("Pic id %d, stream length %d", (int)this->picId, input.data_len);
    *consumed = 0;
    frame->size = 0;
    this->pending_flush = OMX_FALSE;

    enum DecRet ret;
    ret = HevcDecDecode(this->instance, &input, &output);

    DBGT_PDEBUG("HevcDecDecode ret %d", ret);
    switch (ret)
    {
    case DEC_PENDING_FLUSH:
        DBGT_PDEBUG("Pending flush");
        //this->picId++;
        this->pending_flush = OMX_TRUE;
        stat = CODEC_PENDING_FLUSH;
        break;
    case DEC_PIC_DECODED:
        this->picId++;
        stat = CODEC_HAS_FRAME;
        break;
    case DEC_HDRS_RDY:
        DBGT_PDEBUG("HEADERS READY");
        stat = CODEC_HAS_INFO;
        break;
    case DEC_ADVANCED_TOOLS:
        stat = CODEC_NEED_MORE;
        break;
    case DEC_STRM_PROCESSED:
        stat = CODEC_NEED_MORE;
        break;
    case DEC_BUF_EMPTY:
        stat = CODEC_BUFFER_EMPTY;
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
        struct HevcDecBufferInfo info;
        enum DecRet rv;

        rv = HevcDecGetBufferInfo(this->instance, &info);
        //DBGT_PDEBUG("HevcDecGetBufferInfo ret %d", rv);
        UNUSED_PARAMETER(rv);
        DBGT_PDEBUG("Buffer size %d, number of buffers %d",
            info.next_buf_size, info.buf_num);
        stat = CODEC_WAITING_FRAME_BUFFER;

        // Reset output relatived parameters
        this->out_index_w = 0;
        this->out_index_r = 0;
        this->out_num = 0;
        memset(this->out_pic, 0, sizeof(struct HevcDecPicture)*MAX_BUFFERS);
    }
        break;
    case DEC_ABORTED:
        DBGT_PDEBUG("Decoding aborted");
        *consumed = input.data_len;
        DBGT_EPILOG("");
        return CODEC_ABORTED;
#endif
    case DEC_NO_DECODING_BUFFER:
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

    if (stat != CODEC_ERROR_UNSPECIFIED)
    {
        *consumed = input.data_len - output.data_left;
        DBGT_PDEBUG("Decoder data left %d", output.data_left);
    }

    DBGT_EPILOG("");
    return stat;
}

// get stream info
static CODEC_STATE decoder_getinfo_hevc(CODEC_PROTOTYPE * arg,
                                        STREAM_INFO * pkg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *) arg;
    struct HevcDecInfo decinfo;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(pkg);

    memset(&decinfo, 0, sizeof(decinfo));

    // the headers are ready, get stream information
    enum DecRet ret = HevcDecGetInfo(this->instance, &decinfo);

    if (ret == DEC_OK)
    {
        if ((decinfo.pic_width * decinfo.pic_height) > MAX_VIDEO_RESOLUTION)
        {
            DBGT_ERROR("Video stream resolution exceeds the supported video resolution");
            DBGT_EPILOG("");
            return CODEC_ERROR_STREAM_NOT_SUPPORTED;
        }

        if (decinfo.output_format == DEC_OUT_FRM_TILED_4X4)
            pkg->format = OMX_COLOR_FormatYUV420SemiPlanar4x4Tiled;
        else if ((decinfo.pixel_format == DEC_OUT_PIXEL_P010) && (decinfo.bit_depth > 8))
            pkg->format = OMX_COLOR_FormatYUV420SemiPlanarP010;
        else
            pkg->format = OMX_COLOR_FormatYUV420PackedSemiPlanar;

        pkg->width = decinfo.pic_width;//decinfo.cropParams.cropOutWidth;
        pkg->height = decinfo.pic_height;//decinfo.cropParams.cropOutHeight;
        pkg->sliceheight = decinfo.pic_height;
        pkg->stride = decinfo.pic_stride;
        pkg->bit_depth = decinfo.bit_depth;
        DBGT_PDEBUG("Dec info stride %d", decinfo.pic_stride);
        DBGT_PDEBUG("Dec info bit_depth %d", decinfo.bit_depth);
        DBGT_PDEBUG("outf %u pixf %u", decinfo.output_format, decinfo.pixel_format);
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

        pkg->interlaced = decinfo.interlaced_sequence;

        DBGT_PDEBUG("Interlaced sequence %d", (int)pkg->interlaced);
#ifdef USE_EXTERNAL_BUFFER
        struct HevcDecBufferInfo bufInfo;

        HevcDecGetBufferInfo(this->instance, &bufInfo);

        pkg->framesize = bufInfo.next_buf_size;
        pkg->frame_buffers = bufInfo.buf_num;
#else
        pkg->frame_buffers = decinfo.pic_buff_size;
        pkg->framesize = (pkg->sliceheight * pkg->stride) * 3 / 2;
#endif

        // HDR10 metadata
        pkg->hdr10_available = decinfo.hdr10_metadata.present_flag;
        pkg->hdr10_metadata.redPrimary[0] = decinfo.hdr10_metadata.red_primary_x;
        pkg->hdr10_metadata.redPrimary[1] = decinfo.hdr10_metadata.red_primary_y;
        pkg->hdr10_metadata.greenPrimary[0] = decinfo.hdr10_metadata.green_primary_x;
        pkg->hdr10_metadata.greenPrimary[1] = decinfo.hdr10_metadata.green_primary_y;
        pkg->hdr10_metadata.bluePrimary[0] = decinfo.hdr10_metadata.blue_primary_x;
        pkg->hdr10_metadata.bluePrimary[1] = decinfo.hdr10_metadata.blue_primary_y;
        pkg->hdr10_metadata.whitePoint[0] = decinfo.hdr10_metadata.white_point_x;
        pkg->hdr10_metadata.whitePoint[1] = decinfo.hdr10_metadata.white_point_y;
        pkg->hdr10_metadata.maxMasteringLuminance = decinfo.hdr10_metadata.max_mastering_luminance;
        pkg->hdr10_metadata.minMasteringLuminance = decinfo.hdr10_metadata.min_mastering_luminance;
        pkg->hdr10_metadata.maxContentLightLevel = decinfo.hdr10_metadata.max_content_light_level;
        pkg->hdr10_metadata.maxFrameAverageLightLevel = decinfo.hdr10_metadata.max_frame_average_light_level;

        // HDR video signal info
        pkg->video_full_range_flag = decinfo.video_full_range_flag;
        pkg->colour_desc_available = decinfo.colour_description_present_flag;
        pkg->colour_primaries = decinfo.colour_primaries;
        pkg->transfer_characteristics = decinfo.transfer_characteristics;
        pkg->matrix_coeffs = decinfo.matrix_coeffs;
        pkg->chroma_loc_info_available = decinfo.chroma_loc_info_present_flag;
        pkg->chroma_sample_loc_type_top_field = decinfo.chroma_sample_loc_type_top_field;
        pkg->chroma_sample_loc_type_bottom_field = decinfo.chroma_sample_loc_type_bottom_field;

        this->framesize = pkg->framesize;

        DBGT_PDEBUG("Required number of frame buffers %lu", pkg->frame_buffers);

        DBGT_EPILOG("");
        return CODEC_OK;
    }
    else if (ret == DEC_PARAM_ERROR)
    {
        DBGT_CRITICAL("HEVCDEC_PARAM_ERROR");
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }
    else if (ret == DEC_HDRS_NOT_RDY)
    {
        DBGT_CRITICAL("HEVCDEC_HDRS_NOT_RDY");
        DBGT_EPILOG("");
        return CODEC_ERROR_STREAM;
    }

    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

// get decoded frame
static CODEC_STATE decoder_getframe_hevc(CODEC_PROTOTYPE * arg, FRAME * frame,
                                         OMX_BOOL eos)
{
    CALLSTACK;
    UNUSED_PARAMETER(eos);
    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *) arg;
    struct HevcDecPicture picture;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(frame);

    memset(&picture, 0, sizeof(picture));

    //u32 endofStream = eos == OMX_TRUE ? 1 : 0;

    enum DecRet ret = HevcDecNextPicture(this->instance, &picture);
        DBGT_PDEBUG("HevcDecNextPicture %d", ret);
    this = (CODEC_HEVC *) arg;

    if (ret == DEC_PIC_RDY)
    {
        DBGT_ASSERT(this->framesize);

        //DBGT_PDEBUG("End of stream %d", endofStream);
        DBGT_PDEBUG("Picture corrupted %d", picture.pic_corrupt);
        DBGT_PDEBUG("Display ID %d", picture.pic_id);
        DBGT_PDEBUG("Resolution %dx%d", picture.pic_width, picture.pic_height);
        DBGT_PDEBUG("Stride %d", picture.pic_stride);
        DBGT_PDEBUG("Bit depth luma %d", picture.bit_depth_luma);
        DBGT_PDEBUG("Bit depth chroma %d", picture.bit_depth_chroma);
        DBGT_PDEBUG("outf %u pixf %u", picture.output_format, picture.pixel_format);

        frame->fb_bus_address = picture.output_picture_bus_address;
        frame->fb_bus_data = (u8*)picture.output_picture;
        frame->outBufPrivate.pLumaBase = (u8*)picture.output_picture;
        frame->outBufPrivate.nLumaBusAddress = picture.output_picture_bus_address;
        frame->outBufPrivate.nLumaSize = picture.pic_stride * picture.pic_height;
        frame->outBufPrivate.pChromaBase = frame->outBufPrivate.pLumaBase + frame->outBufPrivate.nLumaSize;
        frame->outBufPrivate.nChromaBusAddress = frame->outBufPrivate.nLumaBusAddress + frame->outBufPrivate.nLumaSize;
        frame->outBufPrivate.nChromaSize = frame->outBufPrivate.nLumaSize / 2;
        frame->outBufPrivate.nBitDepthLuma = picture.bit_depth_luma;
        frame->outBufPrivate.nBitDepthChroma = picture.bit_depth_chroma;
        frame->outBufPrivate.nStride = picture.pic_stride;
        frame->outBufPrivate.nFrameWidth = picture.crop_params.crop_out_width;
        frame->outBufPrivate.nFrameHeight = picture.crop_params.crop_out_height;
        frame->outBufPrivate.nPicId[0] = frame->outBufPrivate.nPicId[1] = picture.decode_id;
        DBGT_PDEBUG("HevcDecNextPicture: output_picture_bus_address %lu", picture.output_picture_bus_address);
        DBGT_PDEBUG("HevcDecNextPicture: output_chroma_bus_address %llu", frame->outBufPrivate.nChromaBusAddress);
        DBGT_PDEBUG("output_luma_base %p, output_chroma_base %p", frame->outBufPrivate.pLumaBase, frame->outBufPrivate.pChromaBase);
        DBGT_PDEBUG("Luma size %lu", frame->outBufPrivate.nLumaSize);
        DBGT_PDEBUG("Chroma size %lu", frame->outBufPrivate.nChromaSize);

        frame->outBufPrivate.sRfcTable.pLumaBase = (u8*)picture.output_rfc_luma_base;
        frame->outBufPrivate.sRfcTable.nLumaBusAddress = picture.output_rfc_luma_bus_address;
        frame->outBufPrivate.sRfcTable.pChromaBase = (u8*)picture.output_rfc_chroma_base;
        frame->outBufPrivate.sRfcTable.nChromaBusAddress = picture.output_rfc_chroma_bus_address;
        DBGT_PDEBUG("RFC: luma base %p, chroma base %p", picture.output_rfc_luma_base, picture.output_rfc_chroma_base);

        frame->size = (picture.pic_stride * picture.pic_height * 3) / 2;
        frame->MB_err_count = picture.pic_corrupt; // Indicates that picture is corrupt
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
        DBGT_PDEBUG("HevcDecNextPicture: End of stream");
        DBGT_EPILOG("");
        return CODEC_END_OF_STREAM;
    }
    else if (ret == DEC_ABORTED)
    {
        DBGT_PDEBUG("HevcDecNextPicture: aborted");
        DBGT_EPILOG("");
        return CODEC_ABORTED;
    }
    else if (ret == DEC_FLUSHED)
    {
        DBGT_PDEBUG("HevcDecNextPicture: flushed");
        DBGT_EPILOG("");
        return CODEC_FLUSHED;
    }


    DBGT_CRITICAL("CODEC_ERROR_UNSPECIFIED");
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

static OMX_S32 decoder_scanframe_hevc(CODEC_PROTOTYPE * arg, STREAM_BUFFER * buf,
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

static CODEC_STATE decoder_setppargs_hevc(CODEC_PROTOTYPE * codec,
                                          PP_ARGS * args)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *) codec;

    DBGT_ASSERT(this);
    DBGT_ASSERT(args);

    this->pp_state = PP_DISABLED;
    DBGT_EPILOG("");
    return CODEC_OK;
}

static CODEC_STATE decoder_endofstream_hevc(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    enum DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = HevcDecEndOfStream(this->instance);

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
CODEC_PROTOTYPE *HantroHwDecOmx_decoder_create_hevc(const void *DWLInstance,
                                        OMX_VIDEO_PARAM_G2CONFIGTYPE *g2Conf)
{
    CALLSTACK;

    DBGT_PROLOG("");
    DBGT_ASSERT(DWLInstance);

    CODEC_HEVC *this = OSAL_Malloc(sizeof(CODEC_HEVC));
    //HevcDecApiVersion decApi;
    HevcDecBuild decBuild;

    memset(this, 0, sizeof(CODEC_HEVC));

    this->base.destroy = decoder_destroy_hevc;
    this->base.decode = decoder_decode_hevc;
    this->base.getinfo = decoder_getinfo_hevc;
    this->base.getframe = decoder_getframe_hevc;
    this->base.scanframe = decoder_scanframe_hevc;
    this->base.setppargs = decoder_setppargs_hevc;
    this->base.endofstream = decoder_endofstream_hevc;
    this->base.pictureconsumed = decoder_pictureconsumed_hevc;
    this->base.setframebuffer = decoder_setframebuffer_hevc;
#ifdef USE_EXTERNAL_BUFFER
    this->base.getframebufferinfo = decoder_getframebufferinfo_hevc;
#endif
    this->base.abort = decoder_abort_hevc;
    this->base.abortafter = decoder_abortafter_hevc;
    this->base.setnoreorder = decoder_setnoreorder_hevc;
    this->base.setinfo = decoder_setinfo_hevc;
    this->instance = 0;
    this->picId = 0;
    this->frame_sent = OMX_FALSE;

    /* Print API version number */
    //decApi = HevcDecGetAPIVersion();
    decBuild = HevcDecGetBuild();
    DBGT_PDEBUG("G2 HEVC Decoder SW build: %d.%d - HW build: %x",
            decBuild.sw_build >> 16,
            decBuild.sw_build & 0xFFFF, decBuild.hw_build);
    UNUSED_PARAMETER(decBuild);

#ifdef VSI_API
    struct HevcDecConfig dec_cfg;
    memset(&dec_cfg, 0, sizeof(dec_cfg));

    dec_cfg.no_output_reordering = DISABLE_OUTPUT_REORDER;
    dec_cfg.use_video_freeze_concealment = ERROR_HANDLING;
    dec_cfg.use_video_compressor = g2Conf->bEnableRFC;
    dec_cfg.use_ringbuffer =  g2Conf->bEnableRingBuffer;
    if (g2Conf->bEnableTiled)
        dec_cfg.output_format = DEC_OUT_FRM_TILED_4X4;
    else
        dec_cfg.output_format = DEC_OUT_FRM_RASTER_SCAN;
    dec_cfg.pixel_format = g2Conf->ePixelFormat;
#ifdef DOWN_SCALER
    dec_cfg.dscale_cfg.down_scale_x = 1;
    dec_cfg.dscale_cfg.down_scale_y = 1;
#endif
#ifdef USE_EXTERNAL_BUFFER
    dec_cfg.guard_size = g2Conf->nGuardSize;
    dec_cfg.use_adaptive_buffers = g2Conf->bEnableAdaptiveBuffers ? 1 : 0;
#endif
    dec_cfg.use_secure_mode = g2Conf->bEnableSecureMode;
    DBGT_PDEBUG("Output format %u, pixel format %u, RFC %u",
        dec_cfg.output_format, dec_cfg.pixel_format, dec_cfg.use_video_compressor);
    enum DecRet ret = HevcDecInit(&this->instance, DWLInstance, &dec_cfg);
#else
    enum DecRet ret = HevcDecInit(&this->instance, DISABLE_OUTPUT_REORDER,
                        ERROR_HANDLING, DEC_OUT_FRM_RASTER_SCAN);
#endif

    if (ret != DEC_OK)
    {
        decoder_destroy_hevc((CODEC_PROTOTYPE *) this);
        DBGT_CRITICAL("HevcDecInit error");
        DBGT_EPILOG("");
        return NULL;
    }

    //HevcDecUseExtraFrmBuffers(this->instance, 1);

    DBGT_EPILOG("");
    return (CODEC_PROTOTYPE *) this;
}

#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_hevc(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;
    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    struct HevcDecBufferInfo info;
    FRAME_BUFFER_INFO bufInfo;
    memset(&bufInfo, 0, sizeof(FRAME_BUFFER_INFO));

    HevcDecGetBufferInfo(this->instance, &info);

    bufInfo.bufferSize = info.next_buf_size;
    bufInfo.numberOfBuffers = info.buf_num;
    DBGT_PDEBUG("bufferSize %d, numberOfBuffers %d", (int)bufInfo.bufferSize, (int)bufInfo.numberOfBuffers);

    DBGT_EPILOG("");
    return bufInfo;
}
#endif

CODEC_STATE decoder_setframebuffer_hevc(CODEC_PROTOTYPE * arg, BUFFER *buff, OMX_U32 available_buffers)
{
    CALLSTACK;

    DBGT_PROLOG("");
#ifdef USE_EXTERNAL_BUFFER
    UNUSED_PARAMETER(available_buffers);
    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;
    struct DWLLinearMem mem;
    struct HevcDecBufferInfo info;
    enum DecRet ret;
    const int page_size = getpagesize();

    memset(&info, 0, sizeof(struct HevcDecBufferInfo));
#if 0
    ret = HevcDecGetBufferInfo(this->instance, &info);

    if (info.buf_num > available_buffers)
    {
        DBGT_CRITICAL("Not enough frame buffers available. Required %d > available %d",
            (int)info.buf_num, (int)available_buffers);
        DBGT_EPILOG("");
        return CODEC_ERROR_NOT_ENOUGH_FRAME_BUFFERS;
    }
#endif
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

    ret = HevcDecAddBuffer(this->instance, &mem);
    DBGT_PDEBUG("HevcDecAddBuffer ret (%d)", ret);

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

CODEC_STATE decoder_pictureconsumed_hevc(CODEC_PROTOTYPE * arg, BUFFER *buff)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    struct HevcDecPicture pic;
    OMX_U32 i, j;

    DBGT_PDEBUG("Consumed: bus_address %lu", buff->bus_address);

    for (i=0; i<MAX_BUFFERS; i++)
    {
        if (buff->bus_address == this->out_pic[i].output_picture_bus_address)
        {
            DBGT_PDEBUG("Found out_pic[%d]: bus_address %lu", (int)i, buff->bus_address);
            pic = this->out_pic[i];
            enum DecRet ret = HevcDecPictureConsumed(this->instance, &pic);
            DBGT_PDEBUG("HevcDecPictureConsumed ret (%d)", ret);
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


CODEC_STATE decoder_abort_hevc(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_EXTERNAL_BUFFER
    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    enum DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = HevcDecAbort(this->instance);

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

CODEC_STATE decoder_abortafter_hevc(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_EXTERNAL_BUFFER
    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    enum DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = HevcDecAbortAfter(this->instance);

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


CODEC_STATE decoder_setnoreorder_hevc(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    enum DecRet ret;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    ret = HevcDecSetNoReorder(this->instance, no_reorder);

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

CODEC_STATE decoder_setinfo_hevc(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf)
{
    CALLSTACK;

    DBGT_PROLOG("");

    CODEC_HEVC *this = (CODEC_HEVC *)arg;
    enum DecRet ret;
    struct HevcDecConfig dec_cfg;
    CODEC_STATE stat = CODEC_ERROR_UNSPECIFIED;

    memset(&dec_cfg, 0, sizeof(struct HevcDecConfig));
    dec_cfg.no_output_reordering = DISABLE_OUTPUT_REORDER;
    dec_cfg.use_video_freeze_concealment = ERROR_HANDLING;
    dec_cfg.use_video_compressor = conf->g2_conf.bEnableRFC;
    dec_cfg.use_ringbuffer = conf->g2_conf.bEnableRingBuffer;
    dec_cfg.use_fetch_one_pic = conf->g2_conf.bEnableFetchOnePic;
    if (conf->g2_conf.bEnableTiled)
        dec_cfg.output_format = DEC_OUT_FRM_TILED_4X4;
    else
        dec_cfg.output_format = DEC_OUT_FRM_RASTER_SCAN;
    dec_cfg.pixel_format = conf->g2_conf.ePixelFormat;
#ifdef DOWN_SCALER
    dec_cfg.dscale_cfg.down_scale_x = 1;
    dec_cfg.dscale_cfg.down_scale_y = 1;
#endif
#ifdef USE_EXTERNAL_BUFFER
    dec_cfg.guard_size = conf->g2_conf.nGuardSize;
    dec_cfg.use_adaptive_buffers = conf->g2_conf.bEnableAdaptiveBuffers ? 1 : 0;
#endif
    dec_cfg.use_secure_mode = conf->g2_conf.bEnableSecureMode;
    ret = HevcDecSetInfo(this->instance, &dec_cfg);

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
