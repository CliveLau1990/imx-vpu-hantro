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

#include "post_processor.h"
#include "util.h"
#include "dbgtrace.h"
#include "vsi_vendor_ext.h"

#ifdef ENABLE_PP

#undef DBGT_PREFIX
#define DBGT_PREFIX "OMX PP"


static OMX_S32 omxformat_to_pp(OMX_COLOR_FORMATTYPE format)
{
    CALLSTACK;
    switch ((OMX_U32)format)
    {
    case OMX_COLOR_FormatL8:
        return PP_PIX_FMT_YCBCR_4_0_0;
    case OMX_COLOR_FormatYUV420PackedPlanar:
    case OMX_COLOR_FormatYUV420Planar:
        return PP_PIX_FMT_YCBCR_4_2_0_PLANAR;
    case OMX_COLOR_FormatYUV420SemiPlanar:
    case OMX_COLOR_FormatYUV420PackedSemiPlanar:
        return PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
    case OMX_COLOR_FormatYUV422PackedSemiPlanar:
    case OMX_COLOR_FormatYUV422SemiPlanar:
        return PP_PIX_FMT_YCBCR_4_2_2_SEMIPLANAR;

    case OMX_COLOR_FormatYCbYCr:
        return PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED;
    case OMX_COLOR_FormatYCrYCb:
        return PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED;
    case OMX_COLOR_FormatCbYCrY:
        return PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED;
    case OMX_COLOR_FormatCrYCbY:
        return PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED;

    case OMX_COLOR_Format32bitARGB8888:
        return PP_PIX_FMT_RGB32;
    case OMX_COLOR_Format32bitBGRA8888:
        return PP_PIX_FMT_RGB32_CUSTOM;
    case OMX_COLOR_Format16bitARGB1555:
        return PP_PIX_FMT_RGB16_5_5_5;
    case OMX_COLOR_Format16bitARGB4444:
        return PP_PIX_FMT_RGB16_CUSTOM;
    case OMX_COLOR_Format16bitRGB565:
        return PP_PIX_FMT_RGB16_5_6_5;
    case OMX_COLOR_Format16bitBGR565:
        return PP_PIX_FMT_BGR16_5_6_5;

    case OMX_COLOR_FormatYUV411SemiPlanar:
    case OMX_COLOR_FormatYUV411PackedSemiPlanar:
        return PP_PIX_FMT_YCBCR_4_1_1_SEMIPLANAR;
    case OMX_COLOR_FormatYUV444SemiPlanar:
    case OMX_COLOR_FormatYUV444PackedSemiPlanar:
        return PP_PIX_FMT_YCBCR_4_4_4_SEMIPLANAR;
    case OMX_COLOR_FormatYUV440SemiPlanar:
    case OMX_COLOR_FormatYUV440PackedSemiPlanar:
        return PP_PIX_FMT_YCBCR_4_4_0;

    case OMX_COLOR_FormatYUV420SemiPlanar8x4Tiled:
        return PP_PIX_FMT_YCBCR_4_2_0_TILED;

    default:
        DBGT_PDEBUG("Color format %d", format);
        DBGT_ASSERT(!"Unknown OMX->PP format");
        return -1;
    }
    return 0;
}

void get_rgb_mask(OMX_COLOR_FORMATTYPE format, PPRgbBitmask * rgb_mask)
{
    CALLSTACK;

    switch ((OMX_U32)format)
    {
    case OMX_COLOR_Format32bitBGRA8888:
        rgb_mask->mask_b =       0xFF000000;
        rgb_mask->mask_g =       0x00FF0000;
        rgb_mask->mask_r =       0x0000FF00;
        rgb_mask->mask_alpha =   0x000000FF;
        break;
    case OMX_COLOR_Format16bitARGB4444:
        rgb_mask->mask_r =       0x0F00;
        rgb_mask->mask_g =       0x00F0;
        rgb_mask->mask_b =       0x000F;
        rgb_mask->mask_alpha =   0xF000;
        break;
    default:
        DBGT_ASSERT(!"Unsupported OMX color format");
        break;
    }
}

PPResult HantroHwDecOmx_pp_init(PPInst * pp_instance, PPConfig * pp_config)
{
    CALLSTACK;
    DBGT_PROLOG("");

    PPResult ret = PP_OK;

    if (!*pp_instance)
    {
        ret = PPInit(pp_instance);
    }

    if (ret == PP_OK)
    {
        ret = PPGetConfig(*pp_instance, pp_config);
    }

    DBGT_EPILOG("");
    return ret;
}

void HantroHwDecOmx_pp_destroy(PPInst * pp_instance)
{
    CALLSTACK;
    DBGT_PROLOG("");

    PPRelease(*pp_instance);

    *pp_instance = 0;
    DBGT_EPILOG("");
}

PP_TRANSFORMS HantroHwDecOmx_pp_set_output(PPConfig * pp_config, PP_ARGS * args,
                                           OMX_BOOL bDeIntEna)
{
    CALLSTACK;
    DBGT_PROLOG("");

    PP_TRANSFORMS state = PPTR_NONE;

    // set input video cropping parameters
    if (args->crop.width > 0 && args->crop.height > 0 &&
       args->crop.top >= 0 && args->crop.left >= 0)
    {
        pp_config->pp_in_crop.enable = 1;
        pp_config->pp_in_crop.width = args->crop.width;
        pp_config->pp_in_crop.height = args->crop.height;
        pp_config->pp_in_crop.origin_x = args->crop.left;
        pp_config->pp_in_crop.origin_y = args->crop.top;
        pp_config->pp_out_img.width = args->crop.width;
        pp_config->pp_out_img.height = args->crop.height;
        state |= PPTR_CROP;
        state |= PPTR_SCALE;
    }
    else
    {
        pp_config->pp_in_crop.enable = 0;
    }

    if (args->mask1.height && args->mask1.width)
    {
        pp_config->pp_out_mask1.origin_x = args->mask1.originX;
        pp_config->pp_out_mask1.origin_y = args->mask1.originY;
        pp_config->pp_out_mask1.height = args->mask1.height;
        pp_config->pp_out_mask1.width = args->mask1.width;
        pp_config->pp_out_mask1.blend_component_base = args->blend_mask_base;
        pp_config->pp_out_mask1.enable = 1;
        pp_config->pp_out_mask1.alpha_blend_ena = args->blend_mask_base != 0;
        state |= PPTR_MASK1;
    }
    else
    {
        pp_config->pp_out_mask1.enable = 0;
    }

    if (args->mask2.height && args->mask2.width)
    {
        pp_config->pp_out_mask2.origin_x = args->mask2.originX;
        pp_config->pp_out_mask2.origin_y = args->mask2.originY;
        pp_config->pp_out_mask2.width = args->mask2.width;
        pp_config->pp_out_mask2.height = args->mask2.height;
        pp_config->pp_out_mask2.enable = 1;
        state |= PPTR_MASK2;
    }
    else
    {
        pp_config->pp_out_mask2.enable = 0;
    }

    // set output video resolution
    if (args->scale.width > 0 && args->scale.height > 0)
    {
        pp_config->pp_out_img.width = args->scale.width;
        pp_config->pp_out_img.height = args->scale.height;
        state |= PPTR_SCALE;
        DBGT_PDEBUG("Scaled width x height: %dx%d",(int)args->scale.width, (int)args->scale.height);
    }

    // set output color format
    DBGT_PDEBUG("PP output format: %s", HantroOmx_str_omx_color(args->format));
    if (args->format != OMX_COLOR_FormatUnused &&
       (args->format != OMX_COLOR_FormatYUV420SemiPlanar ||
		args->format != OMX_COLOR_FormatYUV420PackedSemiPlanar))
    {
        OMX_S32 format = omxformat_to_pp(args->format);

        if (format == -1)
            return PPTR_ARG_ERROR;
        else if (format == PP_PIX_FMT_RGB16_CUSTOM ||
                 format == PP_PIX_FMT_RGB32_CUSTOM)
        {
            PPRgbBitmask mask;

            get_rgb_mask(args->format, &mask);

            pp_config->pp_out_rgb.rgb_bitmask.mask_r = mask.mask_r;
            pp_config->pp_out_rgb.rgb_bitmask.mask_g = mask.mask_g;
            pp_config->pp_out_rgb.rgb_bitmask.mask_b = mask.mask_b;
            pp_config->pp_out_rgb.rgb_bitmask.mask_alpha = mask.mask_alpha;

            DBGT_PDEBUG("Custom RGB mask:\n R 0x%08x\n G 0x%08x\n B 0x%08x\n A 0x%08x",
            mask.mask_r, mask.mask_g, mask.mask_b, mask.mask_alpha);
        }

        pp_config->pp_out_img.pix_format = omxformat_to_pp(args->format);

        DBGT_PDEBUG("PP output format 0x%x", pp_config->pp_out_img.pix_format);
#ifdef OMX_DECODER_VIDEO_DOMAIN
        state |= PPTR_FORMAT;
#endif

        if (format == PP_PIX_FMT_RGB32 ||
            format == PP_PIX_FMT_BGR32 ||
            format == PP_PIX_FMT_RGB16_5_5_5 ||
            format == PP_PIX_FMT_RGB16_5_6_5 ||
            format == PP_PIX_FMT_BGR16_5_6_5 ||
            format == PP_PIX_FMT_RGB16_CUSTOM ||
            format == PP_PIX_FMT_RGB32_CUSTOM)
        {

            pp_config->pp_in_img.video_range = 0;
            pp_config->pp_out_rgb.rgb_transform = PP_YCBCR2RGB_TRANSFORM_BT_601;
            pp_config->pp_out_rgb.contrast = args->contrast;
            pp_config->pp_out_rgb.brightness = args->brightness;
            pp_config->pp_out_rgb.saturation = args->saturation;
            pp_config->pp_out_rgb.alpha = args->alpha;
            pp_config->pp_out_rgb.dithering_enable = args->dither;
        }
    }
    else
    {
        pp_config->pp_out_img.pix_format = PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR;
    }

    // set rotation
    if (args->rotation != ROTATE_NONE)
    {
        switch (args->rotation)
        {
        case ROTATE_LEFT_90:
            pp_config->pp_in_rotation.rotation = PP_ROTATION_LEFT_90;
            break;
        case ROTATE_RIGHT_90:
            pp_config->pp_in_rotation.rotation = PP_ROTATION_RIGHT_90;
            break;
        case ROTATE_180:
            pp_config->pp_in_rotation.rotation = PP_ROTATION_180;
            break;
        case ROTATE_FLIP_VERTICAL:
            pp_config->pp_in_rotation.rotation = PP_ROTATION_VER_FLIP;
            break;
        case ROTATE_FLIP_HORIZONTAL:
            pp_config->pp_in_rotation.rotation = PP_ROTATION_HOR_FLIP;
            break;
        default:
            DBGT_ASSERT(!"Unknown rotation mode");
            break;
        }
        state |= PPTR_ROTATE;
    }
    else
    {
        pp_config->pp_in_rotation.rotation = PP_ROTATION_NONE;
    }

    if (bDeIntEna == OMX_TRUE)
    {
        state |= PPTR_DEINTERLACE;
    }
    DBGT_EPILOG("");
    return state;
}

// setup input image params
void HantroHwDecOmx_pp_set_info(PPConfig * pp_config, STREAM_INFO * info,
                                PP_TRANSFORMS state)
{
    CALLSTACK;
    DBGT_PROLOG("");

    if (!(state & PPTR_CROP))
    {
        if (state & PPTR_ROTATE)
        {
            // perform automatic cropping of image down to next even multiple
            // of 16 pixels. This is because when frame has "padding" and it
            // is rotated the padding is also rotated. With cropping this problem can
            // be removed. The price of this is some lost image data.

            if (!info->isVc1Stream) // Don't align VC1 dimensions
            {
                info->width = (info->width / 16) * 16;
                info->height = (info->height / 16) * 16;
            }
            pp_config->pp_in_crop.enable = 1;
            pp_config->pp_in_crop.width = info->width;
            pp_config->pp_in_crop.height = info->height;
        }
    }

    pp_config->pp_in_img.width = info->width; //stride
    pp_config->pp_in_img.height = info->height;   //sliceheight

    DBGT_PDEBUG("PP input width x height: %dx%d", (int)info->width, (int)info->height);
    DBGT_PDEBUG("PP crop width x height: %dx%d", (int)pp_config->pp_in_crop.width, (int)pp_config->pp_in_crop.height);

    if (!(state & PPTR_SCALE))
    {
        pp_config->pp_out_img.width = info->width;
        pp_config->pp_out_img.height = info->height;
    }

    if (state & PPTR_ROTATE)
    {
        if (pp_config->pp_in_rotation.rotation == PP_ROTATION_LEFT_90 ||
           pp_config->pp_in_rotation.rotation == PP_ROTATION_RIGHT_90)
        {
            OMX_S32 temp = pp_config->pp_out_img.width;

            pp_config->pp_out_img.width = pp_config->pp_out_img.height;
            pp_config->pp_out_img.height = temp;
        }
    }

    info->width = pp_config->pp_out_img.width;
    info->height = pp_config->pp_out_img.height;
    info->sliceheight = pp_config->pp_out_img.height;
    info->stride = pp_config->pp_out_img.width;

    DBGT_PDEBUG("PP out width x height: %dx%d", (int)info->width, (int)info->height);
    DBGT_PDEBUG("PP sliceheight: %d", (int)info->sliceheight);
    DBGT_PDEBUG("PP stride: %d", (int)info->stride);

    if (state & PPTR_DEINTERLACE)
    {
        pp_config->pp_out_deinterlace.enable = info->interlaced;
    }

    pp_config->pp_in_img.pix_format = omxformat_to_pp(info->format);
    DBGT_PDEBUG("PP input format: %s", HantroOmx_str_omx_color(info->format));

    info->framesize = HantroHwDecOmx_pp_get_framesize(pp_config);
    DBGT_EPILOG("");
}

// setup output image
void HantroHwDecOmx_pp_set_output_buffer(PPConfig * pp_config, FRAME * frame)
{
    CALLSTACK;
    DBGT_PROLOG("");

    if(!frame->fb_bus_address)
    {
        DBGT_CRITICAL("PP output bus address is NULL");
    }
    pp_config->pp_out_img.buffer_bus_addr = frame->fb_bus_address;
    pp_config->pp_out_img.buffer_chroma_bus_addr = frame->fb_bus_address +
        (pp_config->pp_out_img.width * pp_config->pp_out_img.height);
    DBGT_PDEBUG("  bufferBusAddr = 0x%08lx", pp_config->pp_out_img.buffer_bus_addr);
    DBGT_EPILOG("");
}

// setup input image (standalone mode only)
void HantroHwDecOmx_pp_set_input_buffer(PPConfig * pp_config, OSAL_BUS_WIDTH bus_addr)
{
    CALLSTACK;
    DBGT_PROLOG("");

    pp_config->pp_in_img.buffer_bus_addr = bus_addr;
    pp_config->pp_in_img.buffer_cb_bus_addr = bus_addr +
        (pp_config->pp_in_img.width * pp_config->pp_in_img.height);

    pp_config->pp_in_img.buffer_cr_bus_addr = bus_addr +
        (pp_config->pp_in_img.width * pp_config->pp_in_img.height) +
        (pp_config->pp_in_img.width / 2 * pp_config->pp_in_img.height / 2);
    DBGT_EPILOG("");
}

void HantroHwDecOmx_pp_set_input_buffer_planes(PPConfig * pp_config,
                                               OSAL_BUS_WIDTH bus_addr_y,
                                               OSAL_BUS_WIDTH bus_addr_cbcr)
{
    CALLSTACK;
    DBGT_PROLOG("");
    pp_config->pp_in_img.buffer_bus_addr = bus_addr_y;
    pp_config->pp_in_img.buffer_cb_bus_addr = bus_addr_cbcr;
    DBGT_EPILOG("");
}

// get result
PPResult HantroHwDecOmx_pp_execute(PPInst pp_instance)
{
    CALLSTACK;
    DBGT_PROLOG("");
    PPResult ret = PP_OK;

    ret = PPGetResult(pp_instance);

    DBGT_EPILOG("");
    return ret;
}

// set configuration to pp
PPResult HantroHwDecOmx_pp_set(PPInst pp_instance, PPConfig * pp_config)
{
    CALLSTACK;
    DBGT_PROLOG("");
    PPResult ret = PP_OK;

    ret = PPSetConfig(pp_instance, pp_config);

    DBGT_EPILOG("");
    return ret;
}

PPResult HantroHwDecOmx_pp_pipeline_enable(PPInst pp_instance,
                                           const void *codec_instance, u32 type)
{
    CALLSTACK;
    DBGT_PROLOG("");
    PPResult ret = PP_OK;

    ret = PPDecCombinedModeEnable(pp_instance, codec_instance, type);

    DBGT_EPILOG("");
    return ret;
}

void HantroHwDecOmx_pp_pipeline_disable(PPInst pp_instance,
                                        const void *codec_instance)
{
    CALLSTACK;
    DBGT_PROLOG("");

    PPResult ret = PPDecCombinedModeDisable(pp_instance, codec_instance);

    if (ret == PP_BUSY)
    {
        PPContainer *ppC;
        ppC = (PPContainer *) pp_instance;
        DBGT_PDEBUG("PP is busy... waiting");
        ret = WaitForPp(ppC);
        DBGT_PDEBUG("PP retuns %d", ret);
    }

    DBGT_EPILOG("");
}

OMX_S32 HantroHwDecOmx_pp_get_framesize(const PPConfig * pp_config)
{
    CALLSTACK;
    DBGT_PROLOG("");
    DBGT_ASSERT(pp_config);

    OMX_S32 framesize = pp_config->pp_out_img.width * pp_config->pp_out_img.height;

    switch (pp_config->pp_out_img.pix_format)
    {
    case PP_PIX_FMT_YCBCR_4_0_0:
        break;
    case PP_PIX_FMT_YCBCR_4_2_0_PLANAR:
    case PP_PIX_FMT_YCBCR_4_2_0_SEMIPLANAR:
        framesize = framesize * 3 / 2;
        break;
    case PP_PIX_FMT_YCBCR_4_2_2_SEMIPLANAR:
    case PP_PIX_FMT_YCBCR_4_2_2_INTERLEAVED:
    case PP_PIX_FMT_YCRYCB_4_2_2_INTERLEAVED:
    case PP_PIX_FMT_CBYCRY_4_2_2_INTERLEAVED:
    case PP_PIX_FMT_CRYCBY_4_2_2_INTERLEAVED:
        framesize *= 2;
        break;
    case PP_PIX_FMT_RGB32:
    case PP_PIX_FMT_BGR32:
    case PP_PIX_FMT_RGB32_CUSTOM:
        framesize *= 4;
        break;
    case PP_PIX_FMT_RGB16_5_5_5:
    case PP_PIX_FMT_RGB16_5_6_5:
    case PP_PIX_FMT_BGR16_5_6_5:
    case PP_PIX_FMT_RGB16_CUSTOM:
        framesize *= 2;
        break;
    default:
        DBGT_ASSERT(!"pp_config->ppOutImg.pixFormat");
        DBGT_EPILOG("");
        return -1;
    }
    DBGT_PDEBUG("Frame size %d", (int)framesize);
    DBGT_EPILOG("");
    return framesize;
}
#endif
