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
#include "codec_jpeg.h"
#include "post_processor.h"
#include "jpegdecapi.h"
#ifdef ENABLE_PP
#include "ppapi.h"
#endif
#include "util.h"
#include "dbgtrace.h"
#include "vsi_vendor_ext.h"
#include "test/queue.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX JPEG"

#define MCU_SIZE 16
#ifdef IS_G1_DECODER
#define MCU_MAX_COUNT (8100)
#else
#define MCU_MAX_COUNT (4096)
#endif

#define MAX_RETRY_TIMES 2

typedef enum JPEG_DEC_STATE
{
    JPEG_PARSE_HEADERS,
    JPEG_WAITING_FRAME_BUFFER,
    JPEG_DECODE,
    JPEG_END_OF_STREAM
} JPEG_DEC_STATE;

typedef struct CODEC_JPEG
{
    CODEC_PROTOTYPE base;
    OSAL_ALLOCATOR alloc;
    JpegDecInst instance;
    JpegDecImageInfo info;
    JpegDecInput input;
    JPEG_DEC_STATE state;
    OMX_U32 sliceWidth;
    OMX_U32 sliceHeight;
    OMX_U32 scanLineSize;
    OMX_U32 scanLinesLeft;
#ifdef ENABLE_PP
    PPConfig pp_config;
    PPInst pp_instance;
    PP_TRANSFORMS transforms;
#endif
    PP_STATE pp_state;

    struct buffer
    {
        OSAL_BUS_WIDTH bus_address;
        OMX_U8 *bus_data;
        OMX_U32 capacity;
        OMX_U32 size;
    } Y, CbCr;

    OMX_BOOL mjpeg;
    OMX_BOOL forcedSlice;
    OMX_U32 imageSize;
    OMX_BOOL ppInfoSet;
    QUEUE_CLASS queue;
    FRAME frame;
    OMX_U32 retry;
} CODEC_JPEG;

CODEC_STATE decoder_setinfo_jpeg(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf);
// ratio for calculating chroma offset for slice data
static OMX_U32 decoder_offset_ratio(OMX_U32 sampling)
{
    DBGT_PROLOG("");
    OMX_U32 ratio = 1;

    switch (sampling)
    {
    case JPEGDEC_YCbCr400:
        ratio = 1;
        break;
    case JPEGDEC_YCbCr420_SEMIPLANAR:
    case JPEGDEC_YCbCr411_SEMIPLANAR:
        ratio = 2;
        break;
    case JPEGDEC_YCbCr422_SEMIPLANAR:
        ratio = 1;
        break;
    case JPEGDEC_YCbCr444_SEMIPLANAR:
        ratio = 1;
        break;

    default:
        ratio = 1;
    }

    DBGT_EPILOG("");
    return ratio;
}

// destroy codec instance
static void decoder_destroy_jpeg(CODEC_PROTOTYPE * arg)
{
    DBGT_PROLOG("");

    CODEC_JPEG *this = (CODEC_JPEG *) arg;

    if (this)
    {
        this->base.decode = 0;
        this->base.getframe = 0;
        this->base.getinfo = 0;
        this->base.destroy = 0;
        this->base.scanframe = 0;
        this->base.setppargs = 0;
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
            JpegDecRelease(this->instance);
            this->instance = 0;
        }
        if (this->Y.bus_address)
            OSAL_AllocatorFreeMem(&this->alloc, this->Y.capacity,
                                  this->Y.bus_data, this->Y.bus_address);
        if (this->CbCr.bus_address)
            OSAL_AllocatorFreeMem(&this->alloc, this->CbCr.capacity,
                                  this->CbCr.bus_data, this->CbCr.bus_address);

        queue_clear(&this->queue);
        OSAL_AllocatorDestroy(&this->alloc);
        OSAL_Free(this);
    }
    DBGT_EPILOG("");
}

// try to consume stream data
static CODEC_STATE decoder_decode_jpeg(CODEC_PROTOTYPE * arg,
                                       STREAM_BUFFER * buf, OMX_U32 * consumed,
                                       FRAME * frame)
{
    DBGT_PROLOG("");
    CODEC_JPEG *this = (CODEC_JPEG *) arg;

    DBGT_ASSERT(arg);
    DBGT_ASSERT(buf);
    DBGT_ASSERT(consumed);

    OMX_U32 old_width;
    OMX_U32 old_height;

    this->input.stream_buffer.virtual_address = (u32 *) buf->bus_data;
    this->input.stream_buffer.bus_address = buf->bus_address;
    this->input.stream_length = buf->streamlen;
    this->input.buffer_size = 0; // note: use this if streamlen > 2097144
    this->input.dec_image_type = JPEGDEC_IMAGE;   // Only fullres supported

    DBGT_PDEBUG("streamLength %lu", this->input.stream_length);
    if (this->state == JPEG_PARSE_HEADERS)
    {
        old_width = this->info.display_width;
        old_height = this->info.display_height;

        DBGT_PDEBUG("JpegDecGetImageInfo");
        JpegDecRet ret =
            JpegDecGetImageInfo(this->instance, &this->input, &this->info);
        *consumed = 0;
        switch (ret)
        {
        case JPEGDEC_OK:
            if ((this->info.output_width * this->info.output_height) > MAX_IMAGE_RESOLUTION)
            {
                DBGT_ERROR("Image resolution exceeds the supported resolution");
                DBGT_EPILOG("");
                return CODEC_ERROR_STREAM_NOT_SUPPORTED;
            }
#ifdef ENABLE_PP
            if (this->pp_state != PP_PIPELINE && 
                (this->pp_config.pp_out_img.pix_format != 
                 this->info.output_format))
            {
                PPResult res =
                    HantroHwDecOmx_pp_pipeline_enable(this->pp_instance,
                                                      this->instance,
                                                      PP_PIPELINED_DEC_TYPE_JPEG);
                if (res != PP_OK)
                {
                    DBGT_CRITICAL("HantroHwDecOmx_pp_pipeline_enable failed (err=%d)", res);
                    DBGT_EPILOG("");
                    return CODEC_ERROR_INVALID_ARGUMENT;
                }
                this->pp_state = PP_PIPELINE;
            }
#endif
            if (this->info.display_width != old_width || this->info.display_height != old_height)
            {
                this->state = JPEG_WAITING_FRAME_BUFFER;
                return CODEC_HAS_INFO;
            }
            else
            {
                this->scanLinesLeft = this->info.output_height;
                this->state = JPEG_DECODE;
                return CODEC_NEED_MORE;
            }

        case JPEGDEC_PARAM_ERROR:
            return CODEC_ERROR_INVALID_ARGUMENT;
        case JPEGDEC_ERROR:
        case JPEGDEC_STRM_ERROR:
            return CODEC_ERROR_STREAM;
        case JPEGDEC_INVALID_INPUT_BUFFER_SIZE:
        case JPEGDEC_INVALID_STREAM_LENGTH:
        case JPEGDEC_INCREASE_INPUT_BUFFER:
            return CODEC_ERROR_INVALID_ARGUMENT;
        case JPEGDEC_UNSUPPORTED:
            return CODEC_ERROR_STREAM_NOT_SUPPORTED;
        default:
            DBGT_ASSERT(!"unhandled JpegDecRet");
            break;
        }
    }
    else if (this->state == JPEG_WAITING_FRAME_BUFFER)
    {
            this->state = JPEG_DECODE;
            BUFFER buff;
            int found = 1;

            while(found) queue_pop_noblock (&this->queue, &buff, &found);

            return CODEC_WAITING_FRAME_BUFFER;
    }
    else
    {
        // decode
        JpegDecOutput output;
        OMX_U32 sliceOffset = 0;
        OMX_U32 offset = this->sliceWidth;
        BUFFER buff;
        int found = 0;

        memset(&output, 0, sizeof(JpegDecOutput));

        offset *= this->info.output_height;
        if (!this->frame.fb_bus_address)
        {
          queue_pop_noblock (&this->queue, &buff, &found);
          if(!found)
            return CODEC_NO_DECODING_BUFFER;
          this->frame.fb_bus_data = buff.bus_data;
          this->frame.fb_bus_address = buff.bus_address;
        }

        if (this->pp_state == PP_DISABLED)
        {
            // Offset created by the slices decoded so far
            sliceOffset =
                this->sliceWidth * (this->info.output_height -
                                    this->scanLinesLeft);
            DBGT_PDEBUG("Slice offset %lu", sliceOffset);
        }

        if (this->pp_state != PP_STANDALONE)
        {
            DBGT_PDEBUG("Using external output buffer. Bus address: %lu", this->frame.fb_bus_address);
            OMX_U32 ratio = decoder_offset_ratio(this->info.output_format);

            this->input.picture_buffer_y.virtual_address =
                (u32 *) this->frame.fb_bus_data + sliceOffset;
            this->input.picture_buffer_y.bus_address =
                this->frame.fb_bus_address + sliceOffset;

            this->input.picture_buffer_cb_cr.virtual_address =
                (u32 *) (this->frame.fb_bus_data + offset + sliceOffset / ratio);
            this->input.picture_buffer_cb_cr.bus_address =
                this->frame.fb_bus_address + offset + sliceOffset / ratio;
        }
        else
        {
            DBGT_PDEBUG("Allocating internal output buffer");
            // Let the jpeg decoder allocate output buffer
            this->input.picture_buffer_y.virtual_address = (u32 *) NULL;
            this->input.picture_buffer_y.bus_address = 0;

            this->input.picture_buffer_cb_cr.virtual_address = (u32 *) NULL;
            this->input.picture_buffer_cb_cr.bus_address = 0;
        }
#ifdef ENABLE_PP
        if (this->pp_state == PP_PIPELINE)
        {
            HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
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
        JpegDecRet ret = JpegDecDecode(this->instance, &this->input, &output);

        this->frame.size = 0;
        switch (ret)
        {
        case JPEGDEC_FRAME_READY:
            this->retry = 0;
#ifdef ENABLE_PP
            if (this->Y.bus_address)
            {
                DBGT_ASSERT(this->pp_state == PP_STANDALONE);
                OMX_U32 luma = this->info.output_width * this->scanLinesLeft;

                OMX_U32 chroma = 0;

                switch (this->info.output_format)
                {
                case JPEGDEC_YCbCr420_SEMIPLANAR:
                    chroma = (luma * 3 / 2) - luma;
                    break;
                case JPEGDEC_YCbCr422_SEMIPLANAR:
                    chroma = luma;
                    break;
                }
                
                memcpy(this->Y.bus_data + this->Y.size,
                       output.output_picture_y.virtual_address, luma);
                this->Y.size += luma;
                memcpy(this->CbCr.bus_data + this->CbCr.size,
                       output.output_picture_cb_cr.virtual_address, chroma);
                this->CbCr.size += chroma;

                HantroHwDecOmx_pp_set_input_buffer_planes(&this->pp_config,
                                                          this->Y.bus_address,
                                                          this->CbCr.
                                                          bus_address);
                HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
                PPResult res =
                    HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config);
                if (res != PP_OK)
                {
                    DBGT_CRITICAL("HantroHwDecOmx_pp_set failed (err=%d)", res);
                    DBGT_EPILOG("");
                    return CODEC_ERROR_UNSPECIFIED;
                }
                res = HantroHwDecOmx_pp_execute(this->pp_instance);
                if (res != PP_OK)
                {
                    DBGT_CRITICAL("HantroHwDecOmx_pp_execute failed (err=%d)", res);
                    DBGT_EPILOG("");
                    return CODEC_ERROR_UNSPECIFIED;
                }
                OSAL_AllocatorFreeMem(&this->alloc, this->Y.capacity,
                                      this->Y.bus_data, this->Y.bus_address);
                if (this->CbCr.bus_data)
                    OSAL_AllocatorFreeMem(&this->alloc, this->CbCr.capacity,
                                          this->CbCr.bus_data,
                                          this->CbCr.bus_address);

                memset(&this->Y, 0, sizeof(this->Y));
                memset(&this->CbCr, 0, sizeof(this->CbCr));

                this->frame.size = HantroHwDecOmx_pp_get_framesize(&this->pp_config);
                this->scanLinesLeft -= this->scanLinesLeft;
            }
            else
            {
                if (this->pp_state == PP_STANDALONE)
                {
                    HantroHwDecOmx_pp_set_input_buffer(&this->pp_config,
                                                       output.output_picture_y.
                                                       bus_address);
                    HantroHwDecOmx_pp_set_output_buffer(&this->pp_config,
                                                        frame);
                    PPResult res = HantroHwDecOmx_pp_set(this->pp_instance,
                                                         &this->pp_config);

                    if (res != PP_OK)
                    {
                        DBGT_CRITICAL("HantroHwDecOmx_pp_set failed (err=%d)", res);
                        DBGT_EPILOG("");
                        return CODEC_ERROR_UNSPECIFIED;
                    }
                    res = HantroHwDecOmx_pp_execute(this->pp_instance);
                    if (res != PP_OK)
                    {
                        DBGT_CRITICAL("HantroHwDecOmx_pp_execute failed (err=%d)", res);
                        DBGT_EPILOG("");
                        return CODEC_ERROR_UNSPECIFIED;
                    }
                }
                this->frame.size = this->scanLineSize * this->scanLinesLeft;
                this->scanLinesLeft -= this->scanLinesLeft;
                this->frame.size = HantroHwDecOmx_pp_get_framesize(&this->pp_config);
                this->scanLinesLeft -= this->scanLinesLeft;
            }
#endif
            if (this->pp_state == PP_DISABLED)
                this->frame.size = this->imageSize;
#ifdef ENABLE_CODEC_MJPEG
            if (this->mjpeg)
            {
                // next frame starts with headers
                this->state = JPEG_PARSE_HEADERS;
            }
#endif
            *consumed = buf->streamlen;
            DBGT_EPILOG("");
            return CODEC_HAS_FRAME;

        case JPEGDEC_SLICE_READY:
            {
                if (!this->forcedSlice)
                    this->frame.size = this->scanLineSize * this->sliceHeight;
                this->scanLinesLeft -= this->sliceHeight;

                OMX_U32 luma = this->info.output_width * this->sliceHeight;

                OMX_U32 chroma = 0;

                switch (this->info.output_format)
                {
                case JPEGDEC_YCbCr420_SEMIPLANAR:
                    chroma = (luma * 3 / 2) - luma;
                    break;
                case JPEGDEC_YCbCr422_SEMIPLANAR:
                    chroma = luma;
                    break;
                }
#ifdef ENABLE_PP
                if (this->pp_state == PP_STANDALONE)
                {
                    if (this->Y.bus_address == 0)
                    {
                        this->Y.capacity =
                            this->info.output_width * this->info.output_height;
                        if (OSAL_AllocatorAllocMem
                           (&this->alloc, &this->Y.capacity, &this->Y.bus_data,
                            &this->Y.bus_address) != OMX_ErrorNone)
                        {
                            DBGT_CRITICAL("OSAL_AllocatorAllocMem failed");
                            DBGT_EPILOG("");
                            return CODEC_ERROR_UNSPECIFIED;
                        }
                    }
                    // append frame luma in the temp buffer
                    memcpy(this->Y.bus_data + this->Y.size,
                           output.output_picture_y.virtual_address, luma);
                    this->Y.size += luma;

                    if (this->info.output_format != JPEGDEC_YCbCr400)
                    {
                        if (this->CbCr.bus_address == 0)
                        {
                            OMX_U32 lumasize =
                                this->info.output_width *
                                this->info.output_height;
                            OMX_U32 capacity = 0;

                            switch (this->info.output_format)
                            {
                            case JPEGDEC_YCbCr420_SEMIPLANAR:
                                capacity = lumasize * 3 / 2;
                                break;
                            case JPEGDEC_YCbCr422_SEMIPLANAR:
                                capacity = lumasize * 2;
                                break;
                            }
                            this->CbCr.capacity = capacity - lumasize;
                            if (OSAL_AllocatorAllocMem(&this->alloc,
                                                      &this->CbCr.capacity,
                                                      &this->CbCr.bus_data,
                                                      &this->CbCr.
                                                      bus_address) !=
                               OMX_ErrorNone)
                            {
                                DBGT_CRITICAL("OSAL_AllocatorAllocMem failed");
                                DBGT_EPILOG("");
                                return CODEC_ERROR_UNSPECIFIED;
                            }
                        }
                        // append frame CbCr in the temp buffer
                        memcpy(this->CbCr.bus_data + this->CbCr.size,
                               output.output_picture_cb_cr.virtual_address,
                               chroma);
                        this->CbCr.size += chroma;
                    }
                    // because the image is being assembled from slices and then fed to the post proc
                    // we tell the client that no frames were available.
                    this->frame.size = 0;
                }
#endif
            }
            DBGT_EPILOG("");
            return CODEC_NEED_MORE;

        case JPEGDEC_STRM_PROCESSED:
            return CODEC_NEED_MORE;
        case JPEGDEC_HW_RESERVED:
            return CODEC_ERROR_HW_TIMEOUT;
        case JPEGDEC_PARAM_ERROR:
        case JPEGDEC_INVALID_STREAM_LENGTH:
        case JPEGDEC_INVALID_INPUT_BUFFER_SIZE:
            return CODEC_ERROR_INVALID_ARGUMENT;
        case JPEGDEC_UNSUPPORTED:
            return CODEC_ERROR_STREAM_NOT_SUPPORTED;
        case JPEGDEC_DWL_HW_TIMEOUT:
            return CODEC_ERROR_HW_TIMEOUT;
        case JPEGDEC_SYSTEM_ERROR:
            return CODEC_ERROR_SYS;
        case JPEGDEC_HW_BUS_ERROR:
            return CODEC_ERROR_HW_BUS_ERROR;
        case JPEGDEC_ERROR:
        case JPEGDEC_STRM_ERROR:
            DBGT_ERROR("JPEGDEC_STRM_ERROR");
            *consumed = buf->streamlen;
            this->state = JPEG_PARSE_HEADERS;
            DBGT_EPILOG("");
            return CODEC_ERROR_STREAM;
        case JPEGDEC_HW_TIMEOUT:
            this->retry++;
            if (this->retry > MAX_RETRY_TIMES) {
              this->retry = 0;
              *consumed = buf->streamlen;
            } else
              *consumed = 0;

            this->state = JPEG_PARSE_HEADERS;
            DBGT_EPILOG("");
            return CODEC_NEED_MORE;
        default:
            DBGT_ASSERT(!"unhandled JpegDecRet");
            break;
        }
    }
    return CODEC_ERROR_UNSPECIFIED;
}

// get stream info
static CODEC_STATE decoder_getinfo_jpeg(CODEC_PROTOTYPE * arg,
                                        STREAM_INFO * pkg)
{
    DBGT_PROLOG("");

    CODEC_JPEG *this = (CODEC_JPEG *) arg;

    DBGT_ASSERT(this != 0);
    DBGT_ASSERT(this->instance != 0);
    DBGT_ASSERT(pkg);

    this->sliceWidth = this->info.output_width;
    this->scanLinesLeft = this->info.output_height;

    // Calculate slice size
    if (this->info.output_width / MCU_SIZE * this->info.output_height / MCU_SIZE
       > MCU_MAX_COUNT)
    {
        this->input.slice_mb_set =
            MCU_MAX_COUNT / (this->info.output_width / MCU_SIZE);
        if (this->pp_state == PP_DISABLED || this->pp_state == PP_STANDALONE)
        {
            this->sliceHeight = this->input.slice_mb_set * MCU_SIZE;
            this->forcedSlice = OMX_TRUE;
        }
        else
        {
            this->sliceHeight = this->info.output_height;
        }
    }
    else
    {
        this->input.slice_mb_set = 0;
        this->sliceHeight = this->info.output_height;
    }

    switch (this->info.output_format)
    {
    case JPEGDEC_YCbCr400:
        this->input.slice_mb_set /= 2;
        this->sliceHeight = this->input.slice_mb_set * MCU_SIZE;
        pkg->format = OMX_COLOR_FormatL8;
        this->scanLineSize = this->info.output_width;
        break;
    case JPEGDEC_YCbCr420_SEMIPLANAR:
        pkg->format = OMX_COLOR_FormatYUV420PackedSemiPlanar;
        this->scanLineSize = (this->info.output_width * 3) / 2;
        break;
    case JPEGDEC_YCbCr422_SEMIPLANAR:
        pkg->format = OMX_COLOR_FormatYUV422PackedSemiPlanar;
        this->scanLineSize = this->info.output_width * 2;
        break;
    case JPEGDEC_YCbCr440:
        pkg->format = OMX_COLOR_FormatYUV440PackedSemiPlanar;
        this->scanLineSize = this->info.output_width * 2;
        break;
    case JPEGDEC_YCbCr411_SEMIPLANAR:
        pkg->format = OMX_COLOR_FormatYUV411PackedSemiPlanar;
        this->scanLineSize = (this->info.output_width * 3) / 2;
        break;
    case JPEGDEC_YCbCr444_SEMIPLANAR:
        pkg->format = OMX_COLOR_FormatYUV444PackedSemiPlanar;
        this->scanLineSize = this->info.output_width * 3;
        break;
    default:
        DBGT_ASSERT(!"Unknown output format");
        break;
    }
    // calculate minimum frame size
    pkg->framesize = this->scanLineSize * this->sliceHeight;
    {
        OMX_U32 samplingFactor = 4;

        switch (this->info.output_format)
        {
        case JPEGDEC_YCbCr420_SEMIPLANAR:
        case JPEGDEC_YCbCr411_SEMIPLANAR:
            samplingFactor = 3;
            break;
        case JPEGDEC_YCbCr422_SEMIPLANAR:
        case JPEGDEC_YCbCr440:
            samplingFactor = 4;
            break;
        case JPEGDEC_YCbCr400:
            samplingFactor = 2;
            break;
        case JPEGDEC_YCbCr444_SEMIPLANAR:
            samplingFactor = 6;
            break;
        default:
            break;
        }

        if (this->pp_state == PP_DISABLED)
        {
            this->imageSize = pkg->imageSize = this->info.output_width *
                this->info.output_height * samplingFactor / 2;
        }
    }

    pkg->width = this->info.output_width;
    pkg->height = this->info.output_height;
    pkg->stride = this->info.output_width;
    pkg->sliceheight = this->sliceHeight;
    pkg->frame_buffers = 1;

#ifdef SET_OUTPUT_CROP_RECT
    pkg->crop_available = OMX_FALSE;
    if ((this->info.output_width != this->info.display_width) ||
        (this->info.output_height != this->info.display_height))
    {
        pkg->crop_left = 0;
        pkg->crop_top = 0;
        pkg->crop_width = this->info.display_width;
        pkg->crop_height = this->info.display_height;
        pkg->crop_available = OMX_TRUE;
        DBGT_PDEBUG("Crop left %d, top %d, width %d, height %d", (int)pkg->crop_left,
                (int)pkg->crop_top, (int)pkg->crop_width, (int)pkg->crop_height);
    }
#endif

    if (this->mjpeg)
        pkg->framesize = this->imageSize;

#ifdef ENABLE_PP
    if (this->pp_state == PP_STANDALONE)
        pkg->sliceheight = this->info.output_height;

    if (this->pp_state != PP_DISABLED)
    {
        // prevent multiple updates when decoding mjpeg
        if (this->ppInfoSet == OMX_FALSE)
            HantroHwDecOmx_pp_set_info(&this->pp_config, pkg, this->transforms);

        this->ppInfoSet = OMX_TRUE;
        pkg->imageSize = HantroHwDecOmx_pp_get_framesize(&this->pp_config);
    }
#endif
    //this->state = JPEG_DECODE;
    DBGT_EPILOG("");
    return CODEC_OK;
}

// get decoded frame
static CODEC_STATE decoder_getframe_jpeg(CODEC_PROTOTYPE * arg, FRAME * frame,
                                         OMX_BOOL eos)
{
    CALLSTACK;
    UNUSED_PARAMETER(eos);
    DBGT_PROLOG("");

    CODEC_JPEG *this = (CODEC_JPEG *) arg;

    if (this->frame.size)
    {
        memcpy(frame, &this->frame, sizeof(FRAME));
        frame->outBufPrivate.nFrameWidth = this->info.display_width;
        frame->outBufPrivate.nFrameHeight = this->info.display_height;
        this->frame.fb_bus_address = 0;
        this->frame.size = 0;
        //As for jpeg, set state to end_of_stream once one picture is outputted.
        if (!this->mjpeg)
            this->state = JPEG_END_OF_STREAM;

        DBGT_EPILOG("");
        return CODEC_HAS_FRAME;
    }
    else if (this->state == JPEG_END_OF_STREAM)
    {
      return CODEC_END_OF_STREAM;
    }


    DBGT_EPILOG("");
    return CODEC_OK;
}


static CODEC_STATE decoder_abort_jpeg(CODEC_PROTOTYPE * arg)
{
    UNUSED_PARAMETER(arg);

    CODEC_JPEG *this = (CODEC_JPEG *) arg;
    this->state = JPEG_PARSE_HEADERS;
    return CODEC_ERROR_UNSPECIFIED;
}


static CODEC_STATE decoder_abortafter_jpeg(CODEC_PROTOTYPE * arg)
{
    UNUSED_PARAMETER(arg);

    return CODEC_ERROR_UNSPECIFIED;
}

static CODEC_STATE decoder_setnoreorder_jpeg(CODEC_PROTOTYPE * arg, OMX_BOOL no_reorder)
{
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(no_reorder);

    return CODEC_ERROR_UNSPECIFIED;
}


static OMX_S32 decoder_scanframe_jpeg(CODEC_PROTOTYPE * arg, STREAM_BUFFER * buf,
                                  OMX_U32 * first, OMX_U32 * last)
{
#ifdef ENABLE_CODEC_MJPEG
    CODEC_JPEG *this = (CODEC_JPEG *) arg;
#ifdef USE_SCANFRAME
    if (this->mjpeg)
    {
        OMX_U32 i;
        *first = 0;
        *last = 0;

        for(i = 0; i < buf->streamlen; ++i)
        {
            if (0xFF == buf->bus_data[i])
            {
                if (((i + 1) < buf->streamlen) && 0xD9 == buf->bus_data[i + 1])
                {
                    *last = i+2; /* add 16 bits to get the new start code */
                    return 1;
                }
            }
        }
    }
    else
        return -1;
#else
    if (this->mjpeg)
    {
        *first = 0;
        *last = buf->streamlen;
        return 1;
    }
    else
        return -1;
#endif
#else
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(buf);
    UNUSED_PARAMETER(first);
    UNUSED_PARAMETER(last);
#endif /* ENABLE_CODEC_MJPEG */
    return -1;
}

static CODEC_STATE decoder_setppargs_jpeg(CODEC_PROTOTYPE * codec,
                                          PP_ARGS * args)
{
    DBGT_PROLOG("");

    CODEC_JPEG *this = (CODEC_JPEG *) codec;

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
            HantroHwDecOmx_pp_set_output(&this->pp_config, args, OMX_FALSE);
        if (this->transforms == PPTR_ARG_ERROR)
        {
            DBGT_CRITICAL("HantroHwDecOmx_pp_set_output ((this->transforms == PPTR_ARG_ERROR))");
            DBGT_EPILOG("");
            return CODEC_ERROR_INVALID_ARGUMENT;
        }

        if (this->transforms == PPTR_NONE)
        {
            this->pp_state = PP_DISABLED;
        }
        else
        {
            if (this->transforms & PPTR_ROTATE || this->transforms & PPTR_CROP)
            {
                this->pp_state = PP_STANDALONE;
            }
            else
            {
                DBGT_PDEBUG("PP_COMBINED_MODE");
                PPResult res;

                if (enableCombinedMode)
                {
                    res = HantroHwDecOmx_pp_pipeline_enable(this->pp_instance,
                                                          this->instance,
                                                          PP_PIPELINED_DEC_TYPE_JPEG);
                    if (res != PP_OK)
                    {
                        DBGT_CRITICAL("HantroHwDecOmx_pp_pipeline_enable (err=%d)", (int)res);
                        DBGT_EPILOG("");
                        return CODEC_ERROR_UNSPECIFIED;
                    }
                }

                this->pp_state = PP_PIPELINE;
            }
        }
        DBGT_EPILOG("");
        return CODEC_OK;
    }
#endif
    this->pp_state = PP_DISABLED;
    DBGT_EPILOG("");
    return CODEC_OK;
}

static CODEC_STATE decoder_endofstream_jpeg(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;

    DBGT_PROLOG("");
    CODEC_JPEG *this = (CODEC_JPEG *)arg;
    this->state = JPEG_END_OF_STREAM;
    UNUSED_PARAMETER(arg);
    DBGT_EPILOG("");
    return CODEC_OK;
}

#ifdef USE_EXTERNAL_BUFFER
FRAME_BUFFER_INFO decoder_getframebufferinfo_jpeg(CODEC_PROTOTYPE * arg)
{
    CALLSTACK;
    DBGT_PROLOG("");
    FRAME_BUFFER_INFO bufInfo;
    memset(&bufInfo, 0, sizeof(FRAME_BUFFER_INFO));

    CODEC_JPEG *this = (CODEC_JPEG *)arg;

    bufInfo.bufferSize = this->imageSize;
    bufInfo.numberOfBuffers = 1;
    DBGT_PDEBUG("bufferSize %d, numberOfBuffers %d", (int)bufInfo.bufferSize, (int)bufInfo.numberOfBuffers);

    DBGT_EPILOG("");
    return bufInfo;
}
#endif

CODEC_STATE decoder_setframebuffer_jpeg(CODEC_PROTOTYPE * arg, BUFFER *buff, OMX_U32 available_buffers)
{
    CALLSTACK;

    DBGT_PROLOG("");
#ifdef USE_EXTERNAL_BUFFER
    UNUSED_PARAMETER(available_buffers);
    CODEC_JPEG *this = (CODEC_JPEG *)arg;

    queue_push(&this->queue, buff);
    DBGT_EPILOG("");
    return CODEC_OK;
#else
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(buff);
    UNUSED_PARAMETER(available_buffers);
#endif
    DBGT_EPILOG("");
    return CODEC_OK;
}

CODEC_STATE decoder_pictureconsumed_jpeg(CODEC_PROTOTYPE * arg, BUFFER *buff)
{
    CALLSTACK;

    DBGT_PROLOG("");

#ifdef USE_OUTPUT_RELEASE
    CODEC_JPEG *this = (CODEC_JPEG *)arg;
    queue_push(&this->queue, buff);
#else
    UNUSED_PARAMETER(arg);
    UNUSED_PARAMETER(buff);
#endif
    DBGT_EPILOG("");
    return CODEC_OK;
}

// create codec instance and initialize it
CODEC_PROTOTYPE *HantroHwDecOmx_decoder_create_jpeg(OMX_BOOL motion_jpeg)
{
    DBGT_PROLOG("");

    CODEC_JPEG *this = OSAL_Malloc(sizeof(CODEC_JPEG));
    JpegDecApiVersion decApi;
    JpegDecBuild decBuild;
#ifdef ENABLE_PP
    PPApiVersion ppVer;
    PPBuild ppBuild;
#endif

    memset(this, 0, sizeof(CODEC_JPEG));

    this->base.destroy = decoder_destroy_jpeg;
    this->base.decode = decoder_decode_jpeg;
    this->base.getinfo = decoder_getinfo_jpeg;
    this->base.getframe = decoder_getframe_jpeg;
    this->base.scanframe = decoder_scanframe_jpeg;
    this->base.setppargs = decoder_setppargs_jpeg;
    this->base.endofstream = decoder_endofstream_jpeg;
    this->base.pictureconsumed = decoder_pictureconsumed_jpeg;
    this->base.setframebuffer = decoder_setframebuffer_jpeg;
#ifdef USE_EXTERNAL_BUFFER
    this->base.getframebufferinfo = decoder_getframebufferinfo_jpeg;
#endif
    this->base.abort = decoder_abort_jpeg;
    this->base.abortafter = decoder_abortafter_jpeg;
    this->base.setnoreorder = decoder_setnoreorder_jpeg;
    this->base.setinfo = decoder_setinfo_jpeg;
    this->instance = 0;
    this->state = JPEG_PARSE_HEADERS;

    /* Print API version number */
    decApi = JpegGetAPIVersion();
    decBuild = JpegDecGetBuild();
    DBGT_PDEBUG("X170 Jpeg Decoder API v%d.%d - SW build: %d.%d - HW build: %x",
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

    JpegDecRet ret = JpegDecInit(&this->instance);

#ifdef ENABLE_CODEC_MJPEG
    this->mjpeg = motion_jpeg;
#else
    UNUSED_PARAMETER(motion_jpeg);
#endif
    this->forcedSlice = OMX_FALSE;
    this->ppInfoSet = OMX_FALSE;

    if (ret != JPEGDEC_OK)
    {
        OSAL_Free(this);
        DBGT_CRITICAL("JpegDecInit error");
        DBGT_EPILOG("");
        return NULL;
    }
    if (OSAL_AllocatorInit(&this->alloc) != OMX_ErrorNone)
    {
        JpegDecRelease(this->instance);
        OSAL_Free(this);
        DBGT_CRITICAL("JpegDecInit error");
        DBGT_EPILOG("");
        return NULL;
    }

    queue_init(&this->queue, sizeof(BUFFER));
    DBGT_EPILOG("");
    return (CODEC_PROTOTYPE *) this;
}

CODEC_STATE decoder_setinfo_jpeg(CODEC_PROTOTYPE * arg, OMX_VIDEO_PARAM_CONFIGTYPE *conf)
{
   return CODEC_OK;
}
