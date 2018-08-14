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
#include "codec_pp.h"
#include "post_processor.h"
#include "util.h"
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX PP"

typedef struct CODEC_PP
{
    CODEC_PROTOTYPE base;
    CODEC_STATE decode_state;
    OMX_U32 input_frame_size;
    OMX_U32 input_width;
    OMX_U32 input_height;
    OMX_COLOR_FORMATTYPE input_format;
    OMX_COLOR_FORMATTYPE output_format;
    PPConfig pp_config;
    PPInst pp_instance;
    PP_TRANSFORMS transforms;

} CODEC_PP;

static void codec_pp_destroy(CODEC_PROTOTYPE * codec)
{
    DBGT_PROLOG("");

    CODEC_PP *pp = (CODEC_PP *) codec;

    DBGT_ASSERT(pp);

    if (pp)
    {
        pp->base.decode = 0;
        pp->base.getframe = 0;
        pp->base.getinfo = 0;
        pp->base.destroy = 0;
        pp->base.scanframe = 0;
        pp->base.setppargs = 0;

        if (pp->pp_instance)
            HantroHwDecOmx_pp_destroy(&pp->pp_instance);
        OSAL_Free(pp);
    }
    DBGT_EPILOG("");
}

static
    CODEC_STATE codec_pp_decode(CODEC_PROTOTYPE * codec, STREAM_BUFFER * buf,
                                OMX_U32 * consumed, FRAME * frame)
{
    DBGT_PROLOG("");

    CODEC_PP *this = (CODEC_PP *) codec;

    DBGT_ASSERT(this);
    DBGT_ASSERT(this->pp_instance);
    DBGT_ASSERT(this->input_frame_size);

    if (this->decode_state == CODEC_HAS_INFO)
    {
        // if decode is being called for the first time we need to
        // pass the stream information upwards. Typically this
        // information would come from the stream after the stream headers
        // are parsed. But since we have just raw data here it must be faked.
        this->decode_state = CODEC_OK;
        DBGT_EPILOG("");
        return CODEC_HAS_INFO;
    }

    frame->size = HantroHwDecOmx_pp_get_framesize(&this->pp_config);
    frame->MB_err_count = 0;

    HantroHwDecOmx_pp_set_input_buffer(&this->pp_config, buf->bus_address);
    HantroHwDecOmx_pp_set_output_buffer(&this->pp_config, frame);
    PPResult res = HantroHwDecOmx_pp_set(this->pp_instance, &this->pp_config);

    if (res != PP_OK)
    {
        DBGT_CRITICAL("HantroHwDecOmx_pp_set failed (err=%d)", res);
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }

    res = HantroHwDecOmx_pp_execute(this->pp_instance);
    if (res != PP_OK)
    {
        DBGT_CRITICAL("HantroHwDecOmx_pp_execute failed (err=%d)", res);
        DBGT_EPILOG("");
        return CODEC_ERROR_INVALID_ARGUMENT;
    }

    *consumed = this->input_frame_size;
    DBGT_EPILOG("");
    return CODEC_HAS_FRAME;
}

static CODEC_STATE codec_pp_getinfo(CODEC_PROTOTYPE * codec, STREAM_INFO * info)
{
    DBGT_PROLOG("");

    CODEC_PP *this = (CODEC_PP *) codec;

    DBGT_ASSERT(this);
    DBGT_ASSERT(info);
    if ((this->input_width * this->input_height) > MAX_IMAGE_RESOLUTION_67MPIX)
    {
        DBGT_ERROR("Input image resolution exceeds the supported resolution");
        DBGT_EPILOG("");
        return CODEC_ERROR_STREAM_NOT_SUPPORTED;
    }
    info->format = this->input_format;
    info->width = this->input_width;
    info->height = this->input_height;
    info->stride = this->input_width;
    info->sliceheight = this->input_height;

    // apply info from the config
    HantroHwDecOmx_pp_set_info(&this->pp_config, info, this->transforms);

    info->format = this->output_format;
    DBGT_EPILOG("");
    return CODEC_OK;
}

static
    CODEC_STATE codec_pp_getframe(CODEC_PROTOTYPE * codec, FRAME * frame,
                                  OMX_BOOL eos)
{
    UNUSED_PARAMETER(codec);
    UNUSED_PARAMETER(frame);
    UNUSED_PARAMETER(eos);
    // nothing to do here, the output frames are ready in the call to decode
    return CODEC_OK;
}

static
    OMX_S32 codec_pp_scanframe(CODEC_PROTOTYPE * codec, STREAM_BUFFER * buf,
                           OMX_U32 * first, OMX_U32 * last)
{
    CODEC_PP *this = (CODEC_PP *) codec;
    DBGT_PROLOG("");

    DBGT_ASSERT(this);

    if (buf->streamlen < this->input_frame_size)
    {
        DBGT_EPILOG("");
        return -1;
    }

    DBGT_ASSERT(first);
    DBGT_ASSERT(last);
    *first = 0;
    *last = this->input_frame_size;
    DBGT_EPILOG("");
    return 1;
}

static CODEC_STATE codec_pp_setppargs(CODEC_PROTOTYPE * codec, PP_ARGS * args)
{
    DBGT_PROLOG("");

    DBGT_ASSERT(codec);
    DBGT_ASSERT(args);
    CODEC_PP *this = (CODEC_PP *) codec;

    if (args->scale.width == 0 && args->scale.height == 0)
    {
        // always should have size specified because
        // there is no video stream to decode and for getting the resolution from 
        args->scale.width = this->input_width;
        args->scale.height = this->input_height;
    }

    PPResult ret = HantroHwDecOmx_pp_init(&this->pp_instance, &this->pp_config);

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

        this->output_format = args->format;
        DBGT_EPILOG("");
        return CODEC_OK;
    }
    DBGT_CRITICAL("HantroHwDecOmx_pp_init failed (ret=%d)", ret);
    DBGT_EPILOG("");
    return CODEC_ERROR_UNSPECIFIED;
}

CODEC_PROTOTYPE *HantroHwDecOmx_decoder_create_pp(OMX_U32 input_width,
                                                  OMX_U32 input_height,
                                                  OMX_COLOR_FORMATTYPE
                                                  input_format)
{
    DBGT_PROLOG("");

    CODEC_PP *pp = OSAL_Malloc(sizeof(CODEC_PP));
    PPApiVersion ppVer;
    PPBuild ppBuild;

    if (pp)
    {
        memset(pp, 0, sizeof(CODEC_PP));
        pp->base.destroy = codec_pp_destroy;
        pp->base.decode = codec_pp_decode;
        pp->base.getinfo = codec_pp_getinfo;
        pp->base.getframe = codec_pp_getframe;
        pp->base.scanframe = codec_pp_scanframe;
        pp->base.setppargs = codec_pp_setppargs;
        pp->input_width = input_width;
        pp->input_height = input_height;
        pp->input_format = input_format;
        pp->input_frame_size = input_height * input_width;

        switch (input_format)
        {
        case OMX_COLOR_FormatYUV420PackedPlanar:
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420PackedSemiPlanar:
        case OMX_COLOR_FormatYUV420SemiPlanar:
            pp->input_frame_size = pp->input_frame_size * 3 / 2;
            break;
        case OMX_COLOR_FormatYCbYCr:   // 422 interleaved
            pp->input_frame_size *= 2;
            break;
        case OMX_COLOR_FormatYCrYCb:
            pp->input_frame_size *= 2;
            break;
        case OMX_COLOR_FormatCbYCrY:
            pp->input_frame_size *= 2;
            break;
        case OMX_COLOR_FormatCrYCbY:
            pp->input_frame_size *= 2;
            break;
        default:
            DBGT_CRITICAL("Unknown input format");
            OSAL_Free(pp);
            DBGT_EPILOG("");
            return NULL;
        }

        /* Print API and build version numbers */
        ppVer = PPGetAPIVersion();
        ppBuild = PPGetBuild();

        /* Version */
        DBGT_PDEBUG("X170 PP API v%d.%d - SW build: %d.%d - HW build: %x",
                ppVer.major, ppVer.minor, ppBuild.swBuild >> 16,
                ppBuild.swBuild & 0xFFFF, ppBuild.hwBuild);

        pp->decode_state = CODEC_HAS_INFO;
    }
    else
    {
        DBGT_CRITICAL("Could not allocate memory for PP");
        DBGT_EPILOG("");
        return NULL;
    }

    DBGT_EPILOG("");
    return (CODEC_PROTOTYPE *) pp;
}
