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
#include "codec_mpeg4.h"
#include "post_processor.h"
#include "codec.h"
#include <mp4decapi.h>
#include <ppapi.h>
#include <OSAL.h>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

////////////////////////////////////////////////////////////
// decoder stub implementations
////////////////////////////////////////////////////////////

static MP4DecRet DecInitStatus = MP4DEC_PARAM_ERROR;
static MP4DecRet DecDecodeStatus = MP4DEC_PARAM_ERROR;
static MP4DecRet DecGetInfoStatus = MP4DEC_PARAM_ERROR;

const static u8 DecOutputPicture[352*288*3/2] ={0};

MP4DecRet MP4DecInit(MP4DecInst * pDecInst, u32 disablePicFreezeErrorConceal)
{
    if (DecInitStatus == MP4DEC_OK)
    {
        *pDecInst = OMX_OSAL_Malloc(sizeof(MP4DecInst));
    }

    return DecInitStatus;
}

MP4DecRet MP4DecDecode(MP4DecInst decInst,
                       MP4DecInput         * pInput,
                       MP4DecOutput        * pOutput)
{
    pOutput->pStrmCurrPos = const_cast<u8*>(pInput->pStream) + pInput->dataLen;
    pOutput->strmCurrBusAddress = pInput->streamBusAddress + pInput->dataLen;

    return DecDecodeStatus;
}

MP4DecRet MP4DecGetInfo(MP4DecInst decInst,
                            MP4DecInfo  * pDecInfo)
{
    pDecInfo->picWidth = 352;
    pDecInfo->picHeight = 288;
    return DecGetInfoStatus;
}

void  MP4DecRelease(MP4DecInst decInst)
{
    BOOST_REQUIRE( decInst != 0 );

    OMX_OSAL_Free( const_cast<void *>(decInst) );
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
    DecInitStatus = MP4DEC_MEMFAIL;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_mpeg4(OMX_FALSE);
    BOOST_REQUIRE( i == NULL );

    DecInitStatus = MP4DEC_OK;
    i = HantroHwDecOmx_decoder_create_mpeg4(OMX_FALSE);
    BOOST_REQUIRE( i );
    i->destroy(i);
}

void test_getinfo(MP4DecRet ret, CODEC_STATE state )
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

    DecInitStatus = MP4DEC_OK;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_mpeg4(OMX_FALSE);
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
    test_getinfo(MP4DEC_OK, CODEC_OK);

    /*
    Error in parameter structures. Possibly a NULL pointer.
    */
    test_getinfo(MP4DEC_PARAM_ERROR, CODEC_ERROR_INVALID_ARGUMENT);

    /*
    Stream headers have not been decoded. Stream information is available.
    */
    test_getinfo(MP4DEC_HDRS_NOT_RDY, CODEC_ERROR_STREAM);

}

void test_decode(MP4DecRet ret, CODEC_STATE state, OMX_U32 size )
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

    DecInitStatus = MP4DEC_OK;
    CODEC_PROTOTYPE* i = HantroHwDecOmx_decoder_create_mpeg4(OMX_FALSE);
    BOOST_REQUIRE( i != NULL );

    STREAM_INFO info;
    DecGetInfoStatus = MP4DEC_OK;
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

    OMX_OSAL_Free(frame.fb_bus_data);
    i->destroy(i);
}


void decode_tests()
{
    /*
    Stream buffer processed, but new frame was not ready.
    */
    test_decode(MP4DEC_STRM_PROCESSED, CODEC_NEED_MORE, 0);

    /*
    All the data in the stream buffer was processed. Stream buffer must be
    updated before calling MP4DecDecode again.
    */
    test_decode(MP4DEC_PIC_RDY,CODEC_NEED_MORE, 1);

    /*
    The decoder output picture is ready and the stream buffer is not empty. The
    input stream buffer still contains not decoded data and the stream buffer
    parameters (start address and stream length) have to be recalculated
    based on the information in the MP4DecOutput structure. MP4DecOutput
    structure. An example of this is given in the Decoding chapter.
    */
    test_decode(MP4DEC_PIC_RDY_BUFF_NOT_EMPTY,CODEC_NEED_MORE, 1);
    
    /*
    Error in stream. Picture is corrupted and previous picture shall be displayed.
    */
    test_decode(MP4DEC_FREEZED_PIC_RDY,CODEC_NEED_MORE, 0);

    /*
    Error in stream. Picture is corrupted and previous picture shall be displayed.
    The input stream buffer still contains not decoded data and the stream
    buffer parameters (start address and stream length) have to be
    recalculated based on the information in the MP4DecOutput structure. An
    example of this is given in the Decoding chapter.
    */
    test_decode(MP4DEC_FREEZED_PIC_RDY_BUFF_NOT_EMPTY, CODEC_NEED_MORE, 0);

    /*
    A not coded picture was decoded and previous picture shall be displayed.
    MP4DEC_NOT_CODED_PIC_RDY_BUFF_NOT_EMPTY
    A not coded picture was decoded and previous picture shall be displayed.
    The input stream buffer still contains not decoded data and the stream
    buffer parameters (start address and stream length) have to be
    recalculated based on the information in the MP4DecOutput structure. An
    example of this is given in the Decoding chapter.
    */
    test_decode(MP4DEC_NOT_CODED_PIC_RDY, CODEC_NEED_MORE, 0);

    /*
    Video object stream end code encountered.
    */
    test_decode(MP4DEC_VOS_END, CODEC_NEED_MORE, 0); 

    /*
    Headers decoded. Stream header information is now readable with the
    function MP4DecGetInfo.
    */
    test_decode(MP4DEC_HDRS_RDY, CODEC_HAS_INFO, 0);

    /*
    Headers decoded. Stream header information is now readable with the
    function MP4DecGetInfo. The decoder has to reallocate resources in order to
    continue decoding.
    */
    test_decode(MP4DEC_DP_HDRS_RDY, CODEC_HAS_INFO, 0);

    /*
    Headers decoded and stream buffer is not empty. Stream header
    information is now readable with the function MP4DecGetInfo. The input
    stream buffer still contains not decoded data and the stream buffer
    parameters (start address and stream length) have to be recalculated
    based on the information in the MP4DecOutput structure. MP4DecOutput
    structure. An example of this is given in the Decoding chapter.
    */
    test_decode(MP4DEC_HDRS_RDY_BUFF_NOT_EMPTY,CODEC_HAS_INFO, 0);

    /*
    Headers decoded and stream buffer is not empty. Stream header
    information is now readable with the function MP4DecGetInfo. The decoder
    has to reallocate resources in order to continue decoding. The input stream
    buffer still contains not decoded data and the stream buffer parameters
    (start address and stream length) have to be recalculated based on the
    information in the MP4DecOutput structure. MP4DecOutput structure. An
    example of this is given in the Decoding chapter.
    */
    test_decode(MP4DEC_DP_HDRS_RDY_BUFF_NOT_EMPTY,CODEC_HAS_INFO, 0);

    /*
    A stream error was detected and the decoder must reallocate resources in
    order to continue decoding. The input stream buffer still contains not
    decoded data and the stream buffer parameters (start address and stream
    length) have to be recalculated based on the information in the
    MP4DecOutput structure. An example of this is given in the Decoding
    chapter.
    */
    test_decode(MP4DEC_MEMORY_REALLOCATION_BUFF_NOT_EMPTY,CODEC_NEED_MORE, 0);

    /*
    Error in parameter structures. Stream decoding is not started. Possibly a
    NULL pointer or stream length is 0. Check structures.
    */
    test_decode(MP4DEC_PARAM_ERROR,CODEC_ERROR_INVALID_ARGUMENT, 0);

    /*
    Error in stream decoding. Can not recover before new headers decoded.
    */
    test_decode(MP4DEC_STRM_ERROR,CODEC_NEED_MORE, 0);

    /*
    Error in stream decoding and buffer is not empty. The input stream buffer
    still contains not decoded data and buffer parameters (start address and
    stream length) have to be recalculated based on the information in the
    MP4DecOutput structure. An example of this is given in the Decoding
    chapter.
    */
    test_decode(MP4DEC_STRM_ERROR_BUFF_NOT_EMPTY,CODEC_NEED_MORE, 0);

    /*
    Decoder instance is not initialized. Stream decoding is not started.
    MP4DecInit must be called before decoding can be started.
    */
    test_decode(MP4DEC_NOT_INITIALIZED,CODEC_ERROR_INVALID_ARGUMENT, 0);

    /*
    A bus error occurred in the hardware operation of the decoder. The validity
    of each bus address should be checked. The decoding can not continue. The
    decoder instance has to be released.
    */
    test_decode(MP4DEC_HW_BUS_ERROR,CODEC_ERROR_HW_BUS_ERROR, 0);

    /*
    Error, the wait for a hardware finish has timed out. The current frame is
    lost. New frame decoding has to be started.
    */
    test_decode(MP4DEC_HW_TIMEOUT,CODEC_ERROR_HW_TIMEOUT, 0);

    /*
    Error, a fatal system error was caught. The decoding can not continue. The
    decoder instance has to be released.
    */
    test_decode(MP4DEC_SYSTEM_ERROR,CODEC_ERROR_SYS, 0);

    /*
    Failed to reserve HW for current decoder instance. The current frame is lost.
    New frame decoding has to be started.
    */
    test_decode(MP4DEC_HW_RESERVED,CODEC_ERROR_HW_TIMEOUT, 0);

    /*
    The decoder was not able to allocate memory.
    */
    test_decode(MP4DEC_MEMFAIL,CODEC_ERROR_DWL, 0);
}

int test_main(int, char* [])
{
    cout << "running " <<__FILE__ << " tests\n";

    init_tests();
 //   scanframe_tests();
    getinfo_tests();
    decode_tests();

    return 0;
}
