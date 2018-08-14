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
#include "codec_h264.h"
#include "codec.h"
#include <h264decapi.h>
#include <post_processor.h>
#include <OSAL.h>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////
// decoder stub implementations
////////////////////////////////////////////////////////////

static H264DecRet DecInitStatus = H264DEC_PARAM_ERROR;
static H264DecRet DecDecodeStatus = H264DEC_PARAM_ERROR;
static H264DecRet DecGetInfoStatus = H264DEC_PARAM_ERROR;
static H264DecRet DecNextPictureStatus = H264DEC_PARAM_ERROR;

static u32 DecOutputPicture[(352*288*3/2)/4];


H264DecRet H264DecInit(H264DecInst * pDecInst, u32 noOutputReordering,
                       u32 disablePicFreezeErrorConceal)
{
    if (DecInitStatus == H264DEC_OK)
    {
        *pDecInst = OMX_OSAL_Malloc(sizeof(H264DecInst));
    }

    return DecInitStatus;
}

void H264DecRelease(H264DecInst decInst)
{
    BOOST_REQUIRE( decInst != 0 );

    OMX_OSAL_Free( const_cast<void *>(decInst) );
}

H264DecRet H264DecDecode(H264DecInst decInst,
                         H264DecInput * pInput, H264DecOutput * pOutput)
{
    BOOST_REQUIRE( decInst != 0 );
    BOOST_REQUIRE( pInput != 0 );
    BOOST_REQUIRE( pOutput != 0 );

    pOutput->pStrmCurrPos = const_cast<u8*>(pInput->pStream) + pInput->dataLen;
    pOutput->strmCurrBusAddress = pInput->streamBusAddress + pInput->dataLen;

    return DecDecodeStatus;
}

H264DecRet H264DecNextPicture(H264DecInst decInst,
                              H264DecPicture * pOutput, u32 endOfStream)
{
    pOutput->pOutputPicture = DecOutputPicture;
    return DecNextPictureStatus;
}

H264DecRet H264DecGetInfo(H264DecInst decInst, H264DecInfo * pDecInfo)
{
    pDecInfo->picWidth = 352;
    pDecInfo->picHeight = 288;
    pDecInfo->videoRange = 1;
    pDecInfo->matrixCoefficients = 0;
    pDecInfo->cropParams.cropTopOffset = 1;
    pDecInfo->cropParams.cropLeftOffset = 1;
    pDecInfo->cropParams.cropOutWidth = 350;
    pDecInfo->cropParams.cropOutWidth = 280;
    pDecInfo->outputFormat = H264DEC_SEMIPLANAR_YUV420;
    pDecInfo->sarWidth = 0;
    pDecInfo->sarHeight = 0;
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
    DecInitStatus = H264DEC_MEMFAIL;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_h264(OMX_TRUE);
    BOOST_REQUIRE( i == NULL );

    DecInitStatus = H264DEC_OK;
    i = HantroHwDecOmx_decoder_create_h264(OMX_TRUE);
    BOOST_REQUIRE( i );
    i->destroy(i);
}

void test_decode(H264DecRet ret, CODEC_STATE state, OMX_U32 size )
{
    STREAM_BUFFER buf;
    OMX_U8 data[] =
    {
    0, 1, 2, 3, 4, 5, 6, 7
    };
    OMX_U32 consumed;
    CODEC_STATE s;

    FRAME frame;
    frame.fb_size = 352*288*3/2;
    frame.size = frame.fb_size;
    frame.fb_bus_data = (OMX_U8*) OMX_OSAL_Malloc(frame.fb_size);
    frame.fb_bus_address = (OMX_U32) frame.fb_bus_data;
    frame.MB_err_count = 0;

    if(size == 0)
    {
        frame.size = 1;
    }
    else
    {
        frame.size = 0;
    }

    buf.bus_data = data;
    buf.streamlen = 10;
    
    DecInitStatus = H264DEC_OK;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_h264(OMX_TRUE);
    BOOST_REQUIRE( i != NULL );

    DecDecodeStatus = ret;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE( s );
    BOOST_REQUIRE( s == state );
    if(size == 0)
    {
        BOOST_REQUIRE(frame.size == 0);
    }
    else
    {
        BOOST_REQUIRE(frame.size > 0);
    }

    OMX_OSAL_Free(frame.fb_bus_data);
    i->destroy(i);
}

void decode_tests()
{

    /*          
    All the data in the stream buffer was processed. Stream buffer must be updated
    before calling H264Decode again.
    */
    test_decode(H264DEC_STRM_PROCESSED,CODEC_NEED_MORE, 0);

    /*
    A new picture is ready. Stream buffer must be updated before calling
    H264DecDecode again.
    */
    test_decode(H264DEC_PIC_RDY,CODEC_HAS_FRAME, 0 );

    /*
    A new picture is ready and the stream buffer is not empty. The input stream buffer
    still contains not decoded data and the stream buffer parameters (start address
    and stream length) have to be recalculated based on the information in the
    H264DecOutput structure. An example of this is given in the Decoding chapter.
    */
    test_decode(H264DEC_PIC_RDY_BUFF_NOT_EMPTY,CODEC_HAS_FRAME, 0);

    /*
    Error in stream. Picture is corrupted and previous picture shall be displayed.
    pOutputPicture and outputPictureBusAddress of the structure H264DecPicture
    point     to    a    previous     decoded     picture.          pOutputPicture
    outputPictureBusAddress are defined in the structure H264DecPicture and are
    defined in the H264DecNextPicture chapter.
    */
    test_decode(H264DEC_FREEZED_PIC_RDY,CODEC_HAS_FRAME, 0);

    /*
    Error in stream. Picture is corrupted and previous picture shall be displayed.
    pOutputPicture and outputPictureBusAddress of the structure H264DecPicture
    point     to    a    previous     decoded     picture. and pOutputPicture
    outputPictureBusAddress and are defined in the structure H264DecPicture and
    are defined in the H264DecNextPicture chapter. The input stream buffer still
    contains not decoded data and the stream buffer parameters (start address and
    stream length) have to be recalculated based on the information in the
    H264DecOutput structure. An example of this is given in the Decoding chapter.
    */
    test_decode(H264DEC_FREEZED_PIC_RDY_BUFF_NOT_EMPTY,CODEC_HAS_FRAME, 0);

    /*
    Headers decoded and activated. Stream header information is now readable with
    the function H264DecGetInfo. The input stream buffer still contains not decoded
    data and the stream buffer parameters (start address and stream length) have to
    be recalculated based on the information in the H264DecOutput structure. An
    example of this is given in the Decoding chapter.
    */
    test_decode(H264DEC_HDRS_RDY_BUFF_NOT_EMPTY,CODEC_HAS_INFO, 0);

    /*
    Resource reallocation is required due to a stream which uses advanced coding
    tools. The input stream buffer still contains not decoded data and the stream buffer
    parameters (start address and stream length) have to be recalculated based on
    the information in the H264DecOutput structure. An example of this is given in the
    Decoding chapter.
    */
    test_decode(H264DEC_ADVANCED_TOOLS_BUFF_NOT_EMPTY,CODEC_ERROR_UNSPECIFIED, 0); 

    /*
    Error in calling parameters.
    */
    test_decode(H264DEC_PARAM_ERROR,CODEC_ERROR_INVALID_ARGUMENT, 0);

    /*
    Error in stream decoding. Invalid picture and sequence parameter set combination
    received before trying to decode picture data. New stream data required.
    */
    test_decode(H264DEC_STRM_ERROR,CODEC_ERROR_STREAM, 0);

    /*
    Decoder instance is not initialized. Stream decoding is not started. H264DecInit
    must be called before decoding can be started.
    */
    test_decode(H264DEC_NOT_INITIALIZED,CODEC_ERROR_UNSPECIFIED, 0);

    /*
    A bus error occurred in the hardware operation of the decoder. The validity of each
    bus address should be checked. The decoding can not continue. The decoder
    instance has to be released.
    */
    test_decode(H264DEC_HW_BUS_ERROR,CODEC_ERROR_HW_BUS_ERROR, 0);

    /*
    Error, the wait for a hardware finish has timed out. The current frame is lost. New
    frame decoding has to be started.
    */
    test_decode(H264DEC_HW_TIMEOUT,CODEC_ERROR_HW_TIMEOUT, 0);

    /*Error, a fatal system error was caught. The decoding can not continue. The decoder
    instance has to be released.
    */
    test_decode(H264DEC_SYSTEM_ERROR,CODEC_ERROR_SYS, 0);

    /*
    Failed to reserve HW for current decoder instance. The current frame is lost. New
    frame decoding has to be started.
    */
    test_decode(H264DEC_HW_RESERVED,CODEC_ERROR_HW_TIMEOUT, 0);

    /*
    The decoder failed to activate headers because it could not allocate memories.
    */
    test_decode(H264DEC_MEMFAIL,CODEC_ERROR_UNSPECIFIED, 0);

    /*
    Unsupported picture size.
    */
    test_decode(H264DEC_STREAM_NOT_SUPPORTED,CODEC_ERROR_STREAM, 0);
}

void test_getinfo(H264DecRet ret, CODEC_STATE state )
{
    STREAM_BUFFER buf;
    OMX_U8 data[] =
    {
    0, 1, 2, 3, 4, 5, 6, 7
    };
    CODEC_STATE s;

    FRAME frame;
    frame.fb_size = 352*288*3/2;
    frame.size = frame.fb_size;
    frame.fb_bus_data = (OMX_U8*) OMX_OSAL_Malloc(frame.fb_size);
    frame.fb_bus_address = (OMX_U32) frame.fb_bus_data;
    frame.MB_err_count = 0;

    buf.bus_data = data;
    buf.streamlen = 10;
    
    DecInitStatus = H264DEC_OK;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_h264(OMX_TRUE);
    BOOST_REQUIRE( i != NULL );

    STREAM_INFO info;
    DecGetInfoStatus = ret;
    s = i->getinfo(i, &info);
    CHECK_INTERNAL_API_GETINFO(s);
    BOOST_REQUIRE( s == state );

    OMX_OSAL_Free(frame.fb_bus_data);
    i->destroy(i);
}

void getinfo_tests()
{
    /*
    API function returned successfully
    */
    test_getinfo(H264DEC_OK,CODEC_OK);

    /*
    Error in calling parameters.
    */
    test_getinfo(H264DEC_PARAM_ERROR,CODEC_ERROR_INVALID_ARGUMENT);

    /*
    Headers not decoded or not activated yet. Wait until H264DecDecode returns
          H264DEC_HDRS_RDY_BUFF_NOT_EMPTY.
    */
    test_getinfo(H264DEC_HDRS_NOT_RDY,CODEC_ERROR_STREAM);    
}

void test_nextpicture(H264DecRet ret, CODEC_STATE state )
{
    STREAM_BUFFER buf;
    OMX_U8 data[] =
    {
    0, 1, 2, 3, 4, 5, 6, 7
    };
    CODEC_STATE s;

    FRAME frame;
    frame.fb_size = 352*288*3/2;
    frame.size = frame.fb_size;
    frame.fb_bus_data = (OMX_U8*) OMX_OSAL_Malloc(frame.fb_size);
    frame.fb_bus_address = (OMX_U32) frame.fb_bus_data;
    frame.MB_err_count = 0;

    buf.bus_data = data;
    buf.streamlen = 10;

    DecInitStatus = H264DEC_OK;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_h264(OMX_TRUE);
    BOOST_REQUIRE( i != NULL );

    STREAM_INFO info;
    DecGetInfoStatus = H264DEC_OK;
    s = i->getinfo(i, &info);
    CHECK_INTERNAL_API_GETINFO(s);
    BOOST_REQUIRE( s == CODEC_OK );

    DecNextPictureStatus = ret;
    s = i->getframe(i, &frame, OMX_FALSE);
    CHECK_INTERNAL_API_GETFRAME(s);
    BOOST_REQUIRE( s == state);
    
    OMX_OSAL_Free(frame.fb_bus_data);
    i->destroy(i);
}

void nextpicture_tests()
{
    
    /*
    No pictures available for display.
    */
    test_nextpicture(H264DEC_OK,CODEC_OK);

    /*
    Picture available for display. Application shall handle (e.g. display) the picture and
    call this function again to check for any more pictures to be displayed.
    */
    test_nextpicture(H264DEC_PIC_RDY,CODEC_HAS_FRAME);

    /*
    Error in calling parameters.
    */
    test_nextpicture(H264DEC_PARAM_ERROR,CODEC_ERROR_INVALID_ARGUMENT);
}


/*
 * Test frame scanning
 */
void scanframe_tests()
{
    CODEC_PROTOTYPE* codec = HantroHwDecOmx_decoder_create_h264(OMX_TRUE);

    OMX_U32 first = 0;
    OMX_U32 last = 0;

    vector<char> buff;
    buff.resize(1024);

    fill(buff.begin(), buff.end(), 0xAB);

    STREAM_BUFFER strm =
    {
    };
    strm.bus_data = (OMX_U8*)&buff[0];
    strm.bus_address = (OSAL_BUS_WIDTH)&buff[0];
    strm.streamlen = buff.size();

    BOOST_REQUIRE(codec->scanframe(codec, &strm, &first, &last) == -1);

    buff[450] = 0x00;
    buff[451] = 0x00;
    buff[452] = 0x01;

    int ret = codec->scanframe(codec, &strm, &first, &last);
    BOOST_REQUIRE(ret == 1);
    BOOST_REQUIRE(first == 450);
    BOOST_REQUIRE(last == first);

    buff[700] = 0x00;
    buff[701] = 0x00;
    buff[702] = 0x01;

    buff[934] = 0x00;
    buff[935] = 0x00;
    buff[936] = 0x01;

    ret = codec->scanframe(codec, &strm, &first, &last);
    BOOST_REQUIRE(ret == 1);
    BOOST_REQUIRE(first == 450);
    BOOST_REQUIRE(last == 934);

    fill(buff.begin(), buff.end(), 0xAB);

    buff[0] = 0x00;
    buff[1] = 0x00;
    buff[2] = 0x01;

    ret = codec->scanframe(codec, &strm, &first, &last);
    BOOST_REQUIRE(ret == 1);
    BOOST_REQUIRE(first == 0);
    BOOST_REQUIRE(last == 0);

    codec->destroy(codec);
}

int test_main(int, char* [])
{
    cout << "running " <<__FILE__ << " tests\n";

    init_tests();
    scanframe_tests();
    getinfo_tests();
    decode_tests();
    nextpicture_tests();

    return 0;
}
