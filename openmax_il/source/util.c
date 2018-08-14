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

#include "util.h"
#include "android_ext.h"
#include "dbgtrace.h"
#include "vsi_vendor_ext.h"

#define CASE(x) case x: return #x

const char* HantroOmx_str_omx_state(OMX_STATETYPE s)
{
    switch (s)
    {
        CASE(OMX_StateLoaded);
        CASE(OMX_StateIdle);
        CASE(OMX_StateExecuting);
        CASE(OMX_StatePause);
        CASE(OMX_StateWaitForResources);
        CASE(OMX_StateInvalid);
        default: return "unknown state value";
    }
    return 0;
}

const char* HantroOmx_str_omx_err(OMX_ERRORTYPE e)
{
    switch (e)
    {
        CASE(OMX_ErrorNone);
        CASE(OMX_ErrorInsufficientResources);
        CASE(OMX_ErrorUndefined);
        CASE(OMX_ErrorInvalidComponentName);
        CASE(OMX_ErrorComponentNotFound);
        CASE(OMX_ErrorInvalidComponent);
        CASE(OMX_ErrorBadParameter);
        CASE(OMX_ErrorNotImplemented);
        CASE(OMX_ErrorUnderflow);
        CASE(OMX_ErrorOverflow);
        CASE(OMX_ErrorHardware);
        CASE(OMX_ErrorInvalidState);
        CASE(OMX_ErrorStreamCorrupt);
        CASE(OMX_ErrorPortsNotCompatible);
        CASE(OMX_ErrorResourcesLost);
        CASE(OMX_ErrorNoMore);
        CASE(OMX_ErrorVersionMismatch);
        CASE(OMX_ErrorNotReady);
        CASE(OMX_ErrorTimeout);
        CASE(OMX_ErrorSameState);
        CASE(OMX_ErrorResourcesPreempted);
        CASE(OMX_ErrorUnsupportedSetting);
        CASE(OMX_ErrorUnsupportedIndex);
        CASE(OMX_ErrorIncorrectStateTransition);
        CASE(OMX_ErrorIncorrectStateOperation);
        CASE(OMX_ErrorBadPortIndex);
        default:
            DBGT_PDEBUG("Error value %d", e);
            return "unknown error value";
    }
    return 0;
}

const char* HantroOmx_str_omx_cmd(OMX_COMMANDTYPE c)
{
    switch (c)
    {
        CASE(OMX_CommandStateSet);
        CASE(OMX_CommandFlush);
        CASE(OMX_CommandPortDisable);
        CASE(OMX_CommandPortEnable);
        CASE(OMX_CommandMarkBuffer);
        default: return "unknown command value";
    }
    return 0;
}

const char* HantroOmx_str_omx_event(OMX_EVENTTYPE e)
{
    switch (e)
    {
        CASE(OMX_EventCmdComplete);
        CASE(OMX_EventError);
        CASE(OMX_EventMark);
        CASE(OMX_EventPortSettingsChanged);
        CASE(OMX_EventBufferFlag);
        CASE(OMX_EventResourcesAcquired);
        CASE(OMX_EventComponentResumed);
        CASE(OMX_EventDynamicResourcesAvailable);
        default: return "unknown event value";
    }
    return 0;
}

const char* HantroOmx_str_omx_index(OMX_INDEXTYPE i)
{
    switch ((OMX_U32)i)
    {
        CASE(OMX_IndexParamPortDefinition);
        CASE(OMX_IndexParamVideoPortFormat);
        CASE(OMX_IndexParamImagePortFormat);
        CASE(OMX_IndexConfigVideoFramerate);
        CASE(OMX_IndexParamVideoAvc);
        CASE(OMX_IndexParamVideoMpeg4);
        CASE(OMX_IndexParamVideoH263);
        CASE(OMX_IndexParamVideoWmv);
        CASE(OMX_IndexParamVideoProfileLevelQuerySupported);
        CASE(OMX_IndexParamVideoProfileLevelCurrent);
        CASE(OMX_IndexParamPriorityMgmt);
        CASE(OMX_IndexParamAudioInit);
        CASE(OMX_IndexParamOtherInit);
        CASE(OMX_IndexParamVideoInit);
        CASE(OMX_IndexParamImageInit);
        CASE(OMX_IndexParamCommonDeblocking);
        CASE(OMX_IndexParamStandardComponentRole);
        CASE(OMX_IndexParamCompBufferSupplier);
        CASE(OMX_IndexConfigCommonRotate);
        CASE(OMX_IndexConfigCommonMirror);
        CASE(OMX_IndexConfigCommonContrast);
        CASE(OMX_IndexConfigCommonLightness);
        CASE(OMX_IndexConfigCommonSaturation);
        CASE(OMX_IndexConfigCommonPlaneBlend);
        CASE(OMX_IndexConfigCommonInputCrop);
        CASE(OMX_IndexConfigCommonOutputCrop);
        CASE(OMX_IndexConfigCommonOutputPosition);
        CASE(OMX_IndexConfigCommonExclusionRect);
        CASE(OMX_IndexParamVideoBitrate);
        CASE(OMX_IndexParamVideoVp8);
        CASE(OMX_IndexConfigVideoVp8ReferenceFrameType);
        CASE(OMX_IndexConfigVideoVp8ReferenceFrame);
        CASE(OMX_IndexParamQFactor);
        CASE(OMX_IndexParamVideoIntraRefresh);
        CASE(OMX_IndexConfigVideoIntraVOPRefresh);
        CASE(OMX_IndexConfigVideoAVCIntraPeriod);
        CASE(OMX_IndexParamVideoQuantization);
        CASE(OMX_IndexParamVideoMvcStream);
        CASE(OMX_IndexConfigVideoIntraArea);
        CASE(OMX_IndexConfigVideoRoiArea);
        CASE(OMX_IndexConfigVideoRoiDeltaQp);
        CASE(OMX_IndexConfigVideoAdaptiveRoi);
        CASE(OMX_IndexConfigVideoVp8TemporalLayers);
        CASE(OMX_IndexParamVideoHevc);
        CASE(OMX_IndexParamVideoVp9);
        CASE(OMX_IndexConfigVideoBitrate);
        CASE(OMX_IndexParamVideoG2Config);
#ifdef USE_ANDROID_NATIVE_BUFFER
        CASE(OMX_google_android_index_enableAndroidNativeBuffers);
        CASE(OMX_google_android_index_getAndroidNativeBufferUsage);
        CASE(OMX_google_android_index_useAndroidNativeBuffer);
#endif
        default:
            DBGT_PDEBUG("Index value 0x0%x", i);
            return "unknown index value";
    }
    return 0;
}

const char* HantroOmx_str_omx_color(OMX_COLOR_FORMATTYPE f)
{
    switch ((OMX_U32)f)
    {
        CASE(OMX_COLOR_FormatUnused);
        CASE(OMX_COLOR_FormatL8);
        CASE(OMX_COLOR_FormatYUV420Planar);
        CASE(OMX_COLOR_FormatYUV420PackedPlanar);
        CASE(OMX_COLOR_FormatYUV420SemiPlanar);
        CASE(OMX_COLOR_FormatYUV420PackedSemiPlanar);
        CASE(OMX_COLOR_FormatYUV422PackedSemiPlanar);
        CASE(OMX_COLOR_FormatYCbYCr);
        CASE(OMX_COLOR_Format32bitARGB8888);
        CASE(OMX_COLOR_Format32bitBGRA8888);
        CASE(OMX_COLOR_Format16bitARGB1555);
        CASE(OMX_COLOR_Format16bitARGB4444);
        CASE(OMX_COLOR_Format16bitRGB565);
        CASE(OMX_COLOR_Format16bitBGR565);
        CASE(OMX_COLOR_FormatYCrYCb);
        CASE(OMX_COLOR_FormatCbYCrY);
        CASE(OMX_COLOR_FormatCrYCbY);
        CASE(OMX_COLOR_Format12bitRGB444);
        CASE(OMX_COLOR_Format24bitRGB888);
        CASE(OMX_COLOR_Format24bitBGR888);
        CASE(OMX_COLOR_Format25bitARGB1888);
        CASE(OMX_COLOR_FormatYUV411SemiPlanar);
        CASE(OMX_COLOR_FormatYUV411PackedSemiPlanar);
        CASE(OMX_COLOR_FormatYUV440SemiPlanar);
        CASE(OMX_COLOR_FormatYUV440PackedSemiPlanar);
        CASE(OMX_COLOR_FormatYUV444SemiPlanar);
        CASE(OMX_COLOR_FormatYUV444PackedSemiPlanar);
        CASE(OMX_COLOR_FormatYUV420SemiPlanar4x4Tiled);
        CASE(OMX_COLOR_FormatYUV420SemiPlanar8x4Tiled);
        CASE(OMX_COLOR_FormatYUV420SemiPlanarP010);
        default:
            DBGT_PDEBUG("Color format %d", f);
            return "unknown color format";
    }
    return 0;
}

const char* HantroOmx_str_omx_supplier(OMX_BUFFERSUPPLIERTYPE bst)
{
    switch (bst)
    {
        CASE(OMX_BufferSupplyUnspecified);
        CASE(OMX_BufferSupplyInput);
        CASE(OMX_BufferSupplyOutput);
        default: return "unknown buffer supplier value";
    }
    return 0;
}

OMX_U32 HantroOmx_make_int_ver(OMX_U8 major, OMX_U8 minor)
{
    OMX_VERSIONTYPE ver;
    ver.s.nVersionMajor = major;
    ver.s.nVersionMinor = minor;
    ver.s.nRevision     = 1; /* TODO: correct value? */
    ver.s.nStep         = 0;
    return ver.nVersion;
}
