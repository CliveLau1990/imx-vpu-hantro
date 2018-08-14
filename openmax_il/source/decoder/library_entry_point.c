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

#include <st_static_component_loader.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"

#ifdef _MSC_VER  // For Visual Studio compiler.
#define HANTROOMXDEC_EXPORT __declspec (dllexport)
#else  // Default to GCC.
#define HANTROOMXDEC_EXPORT __attribute__ ((visibility("default")))
#endif

#ifdef OMX_DECODER_VIDEO_DOMAIN
OMX_ERRORTYPE HantroHwDecOmx_video_constructor(OMX_COMPONENTTYPE *, OMX_STRING);
#else
OMX_ERRORTYPE HantroHwDecOmx_image_constructor(OMX_COMPONENTTYPE *, OMX_STRING);
#endif

HANTROOMXDEC_EXPORT
    int omx_component_library_Setup(stLoaderComponentType ** stComponents)
{
    if (stComponents == NULL)
        return 1;
    int roles = 0;

#ifdef OMX_DECODER_VIDEO_DOMAIN
#ifdef ENABLE_CODEC_VP6
    roles++;
#endif  /* ENABLE_CODEC_VP6 */
#ifdef ENABLE_CODEC_VP8
    roles++;
#endif /* ENABLE_CODEC_VP8 */
#ifdef ENABLE_CODEC_AVS
    roles++;
#endif /* ENABLE_CODEC_AVS */
#ifdef ENABLE_CODEC_MPEG4
    roles++;
#endif /* ENABLE_CODEC_MPEG4 */
#ifdef ENABLE_CODEC_H264
    roles++;
#endif /* ENABLE_CODEC_H264 */
#ifdef ENABLE_CODEC_H263
    roles++;
#endif /* ENABLE_CODEC_H263 */
#ifdef ENABLE_CODEC_VC1
    roles++;
#endif /* ENABLE_CODEC_VC1 */
#ifdef ENABLE_CODEC_MPEG2
    roles++;
#endif /* ENABLE_CODEC_MPEG2 */
#ifdef ENABLE_CODEC_RV
    roles++;
#endif /* ENABLE_CODEC_RV */
#ifdef ENABLE_PP
    roles++;
#endif /* ENABLE_PP */
#ifdef ENABLE_CODEC_MJPEG
    roles++;
#endif /* ENABLE_CODEC_MJPEG */
#ifdef ENABLE_CODEC_HEVC
    roles++;
#endif /* ENABLE_CODEC_HEVC */
#ifdef ENABLE_CODEC_VP9
    roles++;
#endif /* ENABLE_CODEC_VP9 */
#else
#ifdef ENABLE_CODEC_JPEG
    roles++;
#endif /* ENABLE_CODEC_JPEG */
#ifdef ENABLE_CODEC_WEBP
    roles++;
#endif /* ENABLE_CODEC_WEBP */
#endif /* OMX_DECODER_VIDEO_DOMAIN */

    const int ROLES = roles;

    // Important. Cross module memory allocation is never a good idea,
    // but this is what the bellagio core forces upon us, so be aware of the possible issues.
    // I belive one could simply use string literals here and greatly simplify the code.

    // note: one could reduce memory overhead by allocating smaller chunks of memory than OMX_MAX_STRINGNAME_SIZE
    // cause that much is not needed
    stComponents[0]->name = (char *) calloc(1, OMX_MAX_STRINGNAME_SIZE);
    if (!stComponents[0]->name)
        return OMX_ErrorInsufficientResources;

#ifdef OMX_DECODER_VIDEO_DOMAIN
    strncpy(stComponents[0]->name, COMPONENT_NAME_VIDEO,
            OMX_MAX_STRINGNAME_SIZE - 1);
#else
    strncpy(stComponents[0]->name, COMPONENT_NAME_IMAGE,
            OMX_MAX_STRINGNAME_SIZE - 1);
#endif

    stComponents[0]->name_specific_length = ROLES;
    stComponents[0]->name_specific =
        (char **) calloc(1, ROLES * sizeof(char *));
    stComponents[0]->role_specific =
        (char **) calloc(1, ROLES * sizeof(char *));
    if (!stComponents[0]->name_specific || !stComponents[0]->role_specific)
    {
        //free(stComponents[0]->name);
        //stComponents[0]->name = 0;
        return OMX_ErrorInsufficientResources;
    }

    // allocate the array members
    int i;

    for(i = 0; i < ROLES; ++i)
    {
        stComponents[0]->name_specific[i] =
            (char *) calloc(1, OMX_MAX_STRINGNAME_SIZE);
        stComponents[0]->role_specific[i] =
            (char *) calloc(1, OMX_MAX_STRINGNAME_SIZE);
        if (stComponents[0]->role_specific[i] == 0 ||
           stComponents[0]->name_specific[i] == 0)
            return OMX_ErrorInsufficientResources;
    }

    int j = 0;
#ifdef OMX_DECODER_VIDEO_DOMAIN
#ifdef ENABLE_CODEC_MPEG4
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_MPEG4,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_MPEG4,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_H264
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_H264,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_H264,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_AVS
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_AVS,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_AVS,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_H263
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_H263,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_H263,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_VC1
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_VC1,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_VC1,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_VP6
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_VP6,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_VP6,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_MPEG2
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_MPEG2,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_MPEG2,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_VP8
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_VP8,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_VP8,
            OMX_MAX_STRINGNAME_SIZE - 1);
	j++;
#endif
#ifdef ENABLE_CODEC_RV
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_RV,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_RV,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_PP
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_PP,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_PP,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_MJPEG
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_MJPEG,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_MJPEG,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_HEVC
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_HEVC,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_HEVC,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_VP9
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_VP9,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_VP9,
            OMX_MAX_STRINGNAME_SIZE - 1);
#endif

    stComponents[0]->constructor = HantroHwDecOmx_video_constructor;
#endif //OMX_DECODER_VIDEO_DOMAIN


#ifdef OMX_DECODER_IMAGE_DOMAIN
    j = 0;
#ifdef ENABLE_CODEC_JPEG
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_JPEG,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_JPEG,
            OMX_MAX_STRINGNAME_SIZE - 1);
    j++;
#endif
#ifdef ENABLE_CODEC_WEBP
    strncpy(stComponents[0]->name_specific[j], COMPONENT_NAME_WEBP,
            OMX_MAX_STRINGNAME_SIZE - 1);
    strncpy(stComponents[0]->role_specific[j], COMPONENT_ROLE_WEBP,
            OMX_MAX_STRINGNAME_SIZE - 1);
#endif

    stComponents[0]->constructor = HantroHwDecOmx_image_constructor;
#endif //OMX_DECODER_IMAGE_DOMAIN
    return 1;
}
