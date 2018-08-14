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
#include "codec_vc1.h"
#include "post_processor.h"
#include "codec.h"
#include <vc1decapi.h>
#include <ppapi.h>
#include <OSAL.h>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////
// decoder stub implementations
////////////////////////////////////////////////////////////
static VC1DecRet DecInitStatus = VC1DEC_PARAM_ERROR;
static VC1DecRet DecDecodeStatus = VC1DEC_PARAM_ERROR;
static VC1DecRet DecGetInfoStatus = VC1DEC_PARAM_ERROR;
static VC1DecRet DecUnpackMetaDataStatus = VC1DEC_PARAM_ERROR;
static VC1DecRet DecNextPictureStatus = VC1DEC_PARAM_ERROR;


VC1DecRet VC1DecInit( VC1DecInst* pDecInst, VC1DecMetaData* pMetaData)
{
    if (DecInitStatus == VC1DEC_OK)
    {
        *pDecInst = OMX_OSAL_Malloc(sizeof(VC1DecInst));
    }
    return DecInitStatus;
}

VC1DecRet VC1DecDecode( VC1DecInst decInst,
                        VC1DecInput* pInput)
{
    return DecDecodeStatus;
}

void VC1DecRelease(VC1DecInst decInst)
{
    OMX_OSAL_Free( const_cast<void *>(decInst) );
}

VC1DecRet VC1DecGetInfo(VC1DecInst decInst, VC1DecInfo * pDecInfo)
{
    return DecGetInfoStatus;
}

VC1DecRet VC1DecUnpackMetaData( u8 *pBuffer, u32 bufferSize,
                                VC1DecMetaData *pMetaData )
{
    OMX_U8 tmp;
    OMX_U32 l;

    for (l = 0; l < bufferSize; l++)
    {
        tmp = *(pBuffer+l);
    }

    pMetaData->maxCodedHeight = 352;
    pMetaData->maxCodedWidth = 288;

    return DecUnpackMetaDataStatus;
}

static u8 DecOutputPicture[352*288*3/2];

VC1DecRet VC1DecNextPicture(VC1DecInst  decInst,
                        VC1DecPicture *pPicture,
                        u32 endOfStream)
{
    pPicture->codedWidth = 352;
    pPicture->codedHeight = 288;
    pPicture->pOutputPicture = DecOutputPicture;
    pPicture->outputPictureBusAddress = (u32) DecOutputPicture;
    return DecNextPictureStatus;
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
     CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_vc1();
     BOOST_REQUIRE(i);
     i->destroy(i);
}

void test_unpackmetadata(VC1DecRet ret, CODEC_STATE state)
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

    buf.bus_data = data;
    buf.streamlen = 10;

    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_vc1();
    BOOST_REQUIRE(i);
    
    DecUnpackMetaDataStatus = ret;
    DecInitStatus = VC1DEC_OK;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    BOOST_REQUIRE( s == state);
    i->destroy(i);
    OMX_OSAL_Free(frame.fb_bus_data);    
}

void unpackmetadata_tests()
{
    /*
    API function returned successfully.
    */
    test_unpackmetadata(VC1DEC_OK, CODEC_HAS_INFO);
    /*
    Buffer pointer pBuffer was NULL, pMetaData was NULL, or bufferSize was less
    than 4.
    */
    test_unpackmetadata(VC1DEC_PARAM_ERROR, CODEC_ERROR_INVALID_ARGUMENT);
 
    /*
    Metadata was in wrong format or indicated unsupported tools.
    */
    test_unpackmetadata(VC1DEC_METADATA_FAIL, CODEC_ERROR_STREAM);
}

void test_decode(VC1DecRet ret, CODEC_STATE state, OMX_U32 size )
{
    STREAM_BUFFER buf;
    STREAM_INFO info;
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

    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_vc1();
    BOOST_REQUIRE(i);
    
    DecUnpackMetaDataStatus = VC1DEC_OK;
    DecInitStatus = VC1DEC_OK;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    BOOST_REQUIRE( s == CODEC_HAS_INFO);

    s = i->getinfo(i, &info);
    CHECK_INTERNAL_API_GETINFO(s);
    BOOST_REQUIRE( s == CODEC_OK);

    DecDecodeStatus = ret;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    BOOST_REQUIRE( s == state);
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
    A picture was successfully decoded.
    */
    test_decode(VC1DEC_PIC_RDY, CODEC_HAS_FRAME, 0);
    
    /*
    All the data in the stream buffer was processed. This will be returned when a
    stream error was encountered and there is no previous picture to display.
    */
    test_decode(VC1DEC_STRM_PROCESSED, CODEC_NEED_MORE, 0);

    /*
    Error in stream. Picture is corrupted and previous picture shall be displayed.
    pOutputPicture and outputPictureBusAddress of the structure VC1DecPicture
    point to previous decoded picture. The error was encountered during the decoding
    of a picture which may be used as a reference when decoding other pictures in the
    video sequence. It is advisable not to display this or the following pictures before
    the next key frame is decoded. A key frame is indicated with the parameter
    keyFrame in the structure VC1DecPicture which is defined in paragraph 4.6.
    */
    test_decode(VC1DEC_FREEZED_PIC_RDY, CODEC_NEED_MORE, 0);


    /*
    Error in stream during the decoding of a B picture. Picture is corrupted and
    previous       picture     shall    be      displayed. and pOutputPicture
    outputPictureBusAddress of the structure VC1DecPicture point to previous
    decoded picture. Errors in B pictures do not affect the decoding process of following
    pictures and therefore this error will not corrupt other pictures in the video
    sequence.
    */
    test_decode(VC1DEC_FREEZED_B_PIC_RDY, CODEC_NEED_MORE, 0);
    
    /*
    Last decoded picture was a 'not coded picture' and previous picture shall be
    displayed. pOutputPicture and outputPictureBusAddress of the structure
    point to a previous decoded picture. pOutputPicture and VC1DecPicture
    outputPictureBusAddress are defined in the structure VC1DecPicture and are
    defined in paragraph 4.6.
    */
    test_decode(VC1DEC_NOT_CODED_PIC_RDY, CODEC_NEED_MORE, 0); 

    /*
    Error in calling parameters.
    */
    test_decode(VC1DEC_PARAM_ERROR, CODEC_ERROR_INVALID_ARGUMENT, 0);

    /*
    Attempted to decode with an uninitialized decoder instance. Stream decoding is not
    started. VC1DecInit must be called before decoding can be started.
    */
    test_decode(VC1DEC_NOT_INITIALIZED, CODEC_ERROR_INVALID_ARGUMENT, 0);

    /*
    A bus error occurred in the hardware operation of the decoder. The validity of each
    bus address should be checked. The decoding cannot be continued, and the
    decoder instance has to be released.
    */
    test_decode(VC1DEC_HW_BUS_ERROR, CODEC_ERROR_HW_BUS_ERROR, 0);

    /*
    Error, the wait for a hardware finish has timed out. The current frame is lost. New
    frame decoding has to be started.
    */
    test_decode(VC1DEC_HW_TIMEOUT, CODEC_ERROR_HW_TIMEOUT, 0);

    /*
    Error, a fatal system error was caught. The decoding cannot continue, and the
    decoder instance has to be released.
    */
    test_decode(VC1DEC_SYSTEM_ERROR, CODEC_ERROR_SYS, 0);

    /*
    Failed to reserve HW for current decoder instance. The current frame is lost. New
    frame decoding has to be started.
    */
    test_decode(VC1DEC_HW_RESERVED, CODEC_ERROR_HW_TIMEOUT, 0);
}

void test_nextpicture(VC1DecRet ret, CODEC_STATE state)
{
    STREAM_BUFFER buf;
    STREAM_INFO info;
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

    buf.bus_data = data;
    buf.streamlen = 10;

    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_vc1();
    BOOST_REQUIRE(i);
    
    DecUnpackMetaDataStatus = VC1DEC_OK;
    DecInitStatus = VC1DEC_OK;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    BOOST_REQUIRE( s == CODEC_HAS_INFO);

    s = i->getinfo(i, &info);
    CHECK_INTERNAL_API_GETINFO(s);
    BOOST_REQUIRE( s == CODEC_OK);

    DecDecodeStatus = VC1DEC_PIC_RDY;
    s = i->decode(i, &buf, &consumed, &frame);
    CHECK_INTERNAL_API_DECODE(s);
    BOOST_REQUIRE( s == CODEC_HAS_FRAME);

    DecNextPictureStatus = ret;
    s = i->getframe(i, &frame, OMX_FALSE);
    CHECK_INTERNAL_API_GETFRAME(s);
    BOOST_REQUIRE( s == state);
    
    i->destroy(i);
    OMX_OSAL_Free(frame.fb_bus_data);    
}


void nextpicture_tests()
{
    /*
    No output picture available.
    */
    test_nextpicture(VC1DEC_OK, CODEC_OK);

    /*
    Picture ready to be displayed.
    */
    test_nextpicture(VC1DEC_PIC_RDY, CODEC_HAS_FRAME);
    /*
    Error in calling parameters.
    */
    test_nextpicture(VC1DEC_PARAM_ERROR,CODEC_ERROR_INVALID_ARGUMENT);

    /*
    Attempt to use function with an uninitialized decoder instance.
    */
    test_nextpicture(VC1DEC_NOT_INITIALIZED, CODEC_ERROR_INVALID_ARGUMENT);

}

/*
 * Test frame scanning
 */
void test2()
{
    /*    CODEC_PROTOTYPE* codec = decoder_create_vc1();

     OMX_U32 first = 0;
     OMX_U32 last  = 0;

     vector<char> buff;
     buff.resize(1024);
     
     fill(buff.begin(), buff.end(), 0xAB);

     STREAM_BUFFER strm = {};
     strm.bus_data    = (OMX_U8*)&buff[0];
     strm.bus_address = (OSAL_BUS_WIDTH)&buff[0];
     strm.streamlen   = buff.size();
     

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
     BOOST_REQUIRE(ret   == 1);
     BOOST_REQUIRE(first == 450);
     BOOST_REQUIRE(last  == 934);
     
     fill(buff.begin(), buff.end(), 0xAB);
     
     buff[0] = 0x00;
     buff[1] = 0x00;
     buff[2] = 0x01;

     
     ret = codec->scanframe(codec, &strm, &first, &last);
     BOOST_REQUIRE(ret == 1);
     BOOST_REQUIRE(first == 0);
     BOOST_REQUIRE(last == 0);

     codec->destroy(codec);*/
}

int test_main(int, char* [])
{
    cout << "running " <<__FILE__ << " tests\n";

    init_tests();
    unpackmetadata_tests();
    decode_tests();
    nextpicture_tests();

    return 0;
}
