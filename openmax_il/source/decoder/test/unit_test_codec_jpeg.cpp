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

#include <boost/test/minimal.hpp>
#include "codec_jpeg.h"
#include "post_processor.h"
#include "codec.h"
#include <jpegdecapi.h>
#include <ppapi.h>
#include <OSAL.h>
#include <iostream>

using namespace std;

////////////////////////////////////////////////////////////
// decoder stub implementations
////////////////////////////////////////////////////////////

static JpegDecRet DecInitStatus = JPEGDEC_OK;
static JpegDecRet DecDecodeStatus = JPEGDEC_STRM_PROCESSED;
static JpegDecRet DecGetInfoStatus = JPEGDEC_OK;

JpegDecRet JpegDecInit(JpegDecInst *pDecInst)
{
    if (DecInitStatus == JPEGDEC_OK)
    {
        *pDecInst = OMX_OSAL_Malloc(sizeof(JpegDecInst));
    }

    return DecInitStatus;
}

void JpegDecRelease(JpegDecInst decInst)
{
    BOOST_REQUIRE( decInst != 0 );

    OMX_OSAL_Free( const_cast<void *>(decInst) );
}

JpegDecRet JpegDecDecode(JpegDecInst decInst,
                         JpegDecInput *pDecIn,
                         JpegDecOutput *pDecOut)
{
    BOOST_REQUIRE( decInst != 0 );
    BOOST_REQUIRE( pDecIn != 0 );
    BOOST_REQUIRE( pDecOut != 0 );

    memcpy( &pDecOut->outputPictureY, &pDecIn->pictureBufferY,
            sizeof(JpegDecLinearMem));
    memcpy( &pDecOut->outputPictureCbCr, &pDecIn->pictureBufferCbCr,
            sizeof(JpegDecLinearMem));
    memset(pDecOut->outputPictureY.pVirtualAddress, 1, 640*480);
    memset(pDecOut->outputPictureCbCr.pVirtualAddress, 2, 640*480/4);

    return DecDecodeStatus;
}

JpegDecRet JpegDecGetImageInfo(JpegDecInst decInst,
                               JpegDecInput *pDecIn,
                               JpegDecImageInfo *pImageInfo)
{
    pImageInfo->displayHeight = 639;
    pImageInfo->displayWidth = 479;
    pImageInfo->outputWidth = 640;
    pImageInfo->outputHeight = 480;
    pImageInfo->version = 1;
    pImageInfo->units = JPEGDEC_DOTS_PER_INCH;
    pImageInfo->xDensity = 3;
    pImageInfo->yDensity = 4;
    pImageInfo->outputFormat = JPEGDEC_YCbCr420_SEMIPLANAR;
    pImageInfo->thumbnailType = JPEGDEC_THUMBNAIL_JPEG;
    pImageInfo->displayHeightThumb = 15;
    pImageInfo->displayWidthThumb = 15;
    pImageInfo->outputWidthThumb = 16;
    pImageInfo->outputHeightThumb = 16;
    pImageInfo->outputFormatThumb = JPEGDEC_YCbCr422_SEMIPLANAR;
    return DecGetInfoStatus;
}

// create pp instance & get default config
PPResult HantroHwDecOmx_pp_init(PPInst* pp_instance,PPConfig* pp_config)
{
    *pp_instance = OMX_OSAL_Malloc(sizeof(PPInst));
    
    return PP_OK;
}

// destroy pp instance
void HantroHwDecOmx_pp_destroy(PPInst* pp_instance)
{
    OMX_OSAL_Free(pp_instance);
}

// setup necessary transformations
PP_TRANSFORMS HantroHwDecOmx_pp_set_output(PPConfig* pp_config, PP_ARGS* args)
{
    return PPTR_NONE;
}

// setup input image params
void HantroHwDecOmx_pp_set_info(PPConfig* pp_config, STREAM_INFO* info, PP_TRANSFORMS state)
{
}

// setup input image (standalone mode only)
void HantroHwDecOmx_pp_set_input_buffer(PPConfig* pp_config, u32 bus_addr)
{
}

// setup output image
void HantroHwDecOmx_pp_set_output_buffer(PPConfig* pp_config, FRAME* frame)
{
}

// set configuration to pp
PPResult HantroHwDecOmx_pp_set(PPInst pp_instance,PPConfig* pp_config)
{
    return PP_OK;
}

// get result (standalone mode only)
PPResult HantroHwDecOmx_pp_execute(PPInst pp_instance)
{
    return PP_OK;
}

// enable pipeline mode
PPResult HantroHwDecOmx_pp_pipeline_enable(PPInst pp_instance, const void* codec_instance, u32 type)
{
    return PP_OK;
}

// disable pipeline mode
void HantroHwDecOmx_pp_pipeline_disable(PPInst pp_instance, const void* codec_instance)
{
}

void HantroHwDecOmx_pp_set_input_buffer_planes(PPConfig*, u32, u32)
{
}

int HantroHwDecOmx_pp_get_framesize(const PPConfig*)
{
    return 0;
}

////////////////////////////////////////////////////////////
// test cases
////////////////////////////////////////////////////////////

#define CHECK_INTERNAL_API_DECODE(a) BOOST_REQUIRE( a < 0 || \
        a == CODEC_NEED_MORE || \
        a == CODEC_HAS_INFO || \
        a == CODEC_HAS_FRAME)

#define CHECK_INTERNAL_API_GETINFO(a) BOOST_REQUIRE( a < 0 || \
        a == CODEC_OK)

#define CHECK_INTERNAL_API_GETFRAME(a) BOOST_REQUIRE( a < 0 || \
        a == CODEC_OK || \
        a == CODEC_HAS_FRAME )

void init_tests()
{
    CODEC_PROTOTYPE* i;
    DecInitStatus = JPEGDEC_MEMFAIL;
    i = HantroHwDecOmx_decoder_create_jpeg();
    BOOST_REQUIRE( i == NULL );

    DecInitStatus = JPEGDEC_OK;
    i = HantroHwDecOmx_decoder_create_jpeg();
    BOOST_REQUIRE( i != NULL );
    i->destroy(i);
}

void test_getinfo( JpegDecRet ret, CODEC_STATE state )
{
    STREAM_BUFFER buf;
    OMX_U8 data[] =
    {
    0, 1, 2, 3, 4, 5, 6, 7
    };
    OMX_U32 consumed;
    CODEC_STATE s;
    FRAME frame;
    frame.fb_size = 640*480 * 3/2;
    frame.fb_bus_data = (OMX_U8*) OMX_OSAL_Malloc(frame.fb_size);

    buf.bus_data = data;
    buf.streamlen = 10;
    STREAM_INFO info;
    
    DecInitStatus = JPEGDEC_OK;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_jpeg();
    BOOST_REQUIRE(i);

    DecGetInfoStatus = ret;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    
    if( s == CODEC_HAS_INFO )
    {
        s = i->getinfo(i, &info);
        CHECK_INTERNAL_API_GETINFO(s);
    }
    BOOST_REQUIRE( s == state );

    i->destroy(i);
    OMX_OSAL_Free(frame.fb_bus_data);
}

void getinfo_tests()
{

    /*
    API function returned successfully.
    */
    test_getinfo(JPEGDEC_OK, CODEC_OK );
    
    /*
    Error in calling parameters, one of them has been set to an invalid NULL pointer.
    */
    test_getinfo(JPEGDEC_PARAM_ERROR, CODEC_ERROR_INVALID_ARGUMENT);

    /*
    Decoding error occurred during header decoding. Stream cannot be decoded.
    */
    test_getinfo(JPEGDEC_ERROR, CODEC_ERROR_STREAM);

    /*
    Stream error occurred during decoding. Stream is only partially decoded.
    */
    test_getinfo(JPEGDEC_STRM_ERROR, CODEC_ERROR_STREAM);

    /*
    The JPEG image contains features that the decoder does not support.
    */
    test_getinfo(JPEGDEC_UNSUPPORTED, CODEC_ERROR_STREAM);

    /*
    The stream length is equal to 0 or over the maximum supported stream length. In
    the latter case the input buffering should be used for decoding the stream.
    */
    test_getinfo(JPEGDEC_INVALID_STREAM_LENGTH, CODEC_ERROR_STREAM);

    /*
    The input buffer size is not within the supported buffer size limits or the buffer size
    is not multiple of 256 bytes.
    */
    test_getinfo(JPEGDEC_INVALID_INPUT_BUFFER_SIZE, CODEC_ERROR_STREAM);

    /*
    The input buffer does not contain all the required information for decoding. Bigger
    input buffer needs to be allocated. This return value can be received if the stream
    contains a large amount of EXIF data.
    */
    test_getinfo(JPEGDEC_INCREASE_INPUT_BUFFER, CODEC_ERROR_INVALID_ARGUMENT);

}

void test_decode( JpegDecRet ret, CODEC_STATE state, OMX_U32 size )
{
    STREAM_BUFFER buf;
    OMX_U8 data[] =
    {
    0, 1, 2, 3, 4, 5, 6, 7
    };
    OMX_U32 consumed;
    CODEC_STATE s;
    FRAME frame;
    frame.fb_size = 640*480 * 3/2;
    frame.fb_bus_data = (OMX_U8*) OMX_OSAL_Malloc(frame.fb_size);
    
    if( size == 0)
    {
        frame.size = 1;
    }
    else
    {
        frame.size = 0;
    }

    buf.bus_data = data;
    buf.streamlen = 10;
    STREAM_INFO info;
    
    DecInitStatus = JPEGDEC_OK;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_jpeg();
    BOOST_REQUIRE(i);

    DecGetInfoStatus = JPEGDEC_OK;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    BOOST_REQUIRE( s == CODEC_HAS_INFO );
    s = i->getinfo(i, &info);
    CHECK_INTERNAL_API_GETINFO(s);
    BOOST_REQUIRE( s == CODEC_OK );

    DecDecodeStatus = ret;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    BOOST_REQUIRE( s == state );
    if(size == 0)
    {
        BOOST_REQUIRE(frame.size == 0);
    }
    else
    {
        BOOST_REQUIRE(frame.size > 0);
    }

    i->destroy(i);
    OMX_OSAL_Free(frame.fb_bus_data);
}


void decode_tests()
{
    /*
    JPEG decoding finished successfully.
    */
    test_decode(JPEGDEC_FRAME_READY, CODEC_NEED_MORE, 1);

    /*
    JPEG slice decoding finished successfully.
    */
    test_decode(JPEGDEC_SLICE_READY, CODEC_NEED_MORE, 1); //NOTE: correct state ?

    /*
    All the data in the stream buffer was processed. Stream buffer must be updated
    before calling JpegDecDecode again.
    */
    test_decode(JPEGDEC_STRM_PROCESSED, CODEC_NEED_MORE, 0);

    /*
    Failed to reserve HW for current decoder instance. The current frame is lost. New
    frame decoding has to be started.
    */

    test_decode(JPEGDEC_HW_RESERVED, CODEC_ERROR_HW_TIMEOUT, 0);
    /*
    Error in calling parameters, one of them has been set to an invalid NULL pointer.
    */
    test_decode(JPEGDEC_PARAM_ERROR, CODEC_ERROR_INVALID_ARGUMENT, 0);
    /*
    The JPEG image contains features that the decoder does not support.
    */
    test_decode(JPEGDEC_UNSUPPORTED, CODEC_ERROR_STREAM, 0);
    /*
    The wait for a hardware finish has timed out. The current frame is lost.
    */
    test_decode(JPEGDEC_DWL_HW_TIMEOUT, CODEC_ERROR_HW_TIMEOUT, 0);
    /*
    The system wrapper layer returned an error. The decoder should be shut down and
    reinitialized.
    */
    test_decode(JPEGDEC_SYSTEM_ERROR, CODEC_ERROR_SYS, 0);
    /*
    A bus error occurred in the hardware operation of the decoder. The validity of each
    bus address should be checked. The decoding cannot be continued, and the
    decoder instance has to be released.
    */
    test_decode(JPEGDEC_HW_BUS_ERROR, CODEC_ERROR_HW_BUS_ERROR, 0);
    /*
    Decoding error occurred during header decoding. Stream cannot be decoded.
    */
    test_decode(JPEGDEC_ERROR, CODEC_ERROR_STREAM, 0);
    /*
    Stream error occurred during decoding. Stream is only partially decoded.
    */
    test_decode(JPEGDEC_STRM_ERROR, CODEC_ERROR_STREAM, 0);
    /*
    The stream length is equal to 0 or over maximum supported stream length if input
    buffering is not used.*/
    test_decode(JPEGDEC_INVALID_STREAM_LENGTH, CODEC_ERROR_STREAM, 0);
    /*
    The input buffer size is not equal to minimum or maximum supported buffer size or
    buffer size is not multiple of 256 bytes or smaller input buffer than needed in
    JpegDecGetImageInfo is tried to use.
    */
    test_decode(JPEGDEC_INVALID_INPUT_BUFFER_SIZE, CODEC_ERROR_STREAM, 0);
}

int test_main(int, char* [])
{
    cout << "running " <<__FILE__ << " tests\n";

    init_tests();
    getinfo_tests();
    decode_tests();
    
    return 0;
}
