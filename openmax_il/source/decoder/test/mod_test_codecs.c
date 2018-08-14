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

//#include <string.h>
#include <stdio.h>
#include <codec.h>
#include <codec_jpeg.h>
#include <codec_h264.h>
#include <codec_mpeg4.h>
#include <codec_vc1.h>
#include <OSAL.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////
// test cases
////////////////////////////////////////////////////////////

//#define PIPELINE



int update_frame(STREAM_INFO* info, FRAME* frame, OMX_U32* slice_count,
        OMX_U32* frames, OMX_U8* yuvdata, OMX_U32* y_offset, OMX_U32* c_offset)
{
    int lines_left = info->height - (*slice_count*info->sliceheight);
    int left = frame->size;
    if (frame->size == info->framesize)
    {
        // got whole slice/frame
        memcpy(yuvdata + *y_offset, frame->fb_bus_data, info->stride
                *info->sliceheight);
        *y_offset+=info->stride*info->sliceheight;
        left -=info->stride*info->sliceheight;
        memcpy(yuvdata + *c_offset, frame->fb_bus_data + (info->stride
                *info->sliceheight), left);
        *c_offset+=left;
    }
    else
    {
        // got partial slice
        memcpy(yuvdata + *y_offset, frame->fb_bus_data, info->stride*lines_left);
        *y_offset+=info->stride*lines_left;
        left -=info->stride*lines_left;
        memcpy(yuvdata + *c_offset,
                frame->fb_bus_data + info->stride*lines_left, left);
        *c_offset+=left;
    }

    // Check is frame completed
    if (lines_left <= info->sliceheight)
    {
        *y_offset = 0;
        *c_offset = info->stride*info->height;
        *slice_count = 0;
//        printf("frame %d decoded.\n", *frames);
        (*frames)++;
        return 1;
    }
    else
    {
//        printf("slice %d decoded.\n", *slice_count);
        (*slice_count)++;
    }
    return 0;
}

void store_frame(STREAM_INFO* info, FILE* out, OMX_U8* yuvdata)
{
    if (info->format == OMX_COLOR_FormatL8)
    {
        fwrite(yuvdata, info->width*info->height, 1, out);
    }
    else if (info->format == OMX_COLOR_FormatYUV420SemiPlanar)
    {
        fwrite(yuvdata, (info->width*info->height*3)/2, 1, out);
    }
    else if (info->format == OMX_COLOR_FormatYUV422SemiPlanar)
    {
        fwrite(yuvdata, info->width*info->height*2, 1, out);
    }
}

//
// create, decode, destroy
//
void test(char *filename, char *outfilename)
{
    OMX_U32 eos = 0;
    STREAM_BUFFER buf;
    bzero( &buf, sizeof(STREAM_BUFFER));
    OMX_U8* data = (OMX_U8*) OSAL_Malloc( 1024*1024*2);
    bzero(data, 1024*1024*2);

    STREAM_INFO info;
    bzero( &info, sizeof(FRAME));

    FRAME frame;
    bzero( &frame, sizeof(FRAME));

    OMX_U32 slice_count = 0;
    OMX_U32 y_offset = 0;
    OMX_U32 c_offset = 0;
    int outWidth = 4672;
    int outHeight = 3504;

    OMX_U8* yuvdata = (OMX_U8*) OSAL_Malloc(outWidth*outHeight*2);

    OMX_U32 consumed = 0;
    int datalen = 0;
    OMX_U32 first = 0;
    OMX_U32 last = 0;
    OMX_U32 frames = 0;
    CODEC_STATE s = CODEC_NEED_MORE;

    FILE* in = fopen(filename, "rb");
    assert( in );
    FILE* out = fopen(outfilename, "wb");
    assert( out );

    frame.fb_size = outWidth*outHeight*2;
    frame.fb_bus_data = (OMX_U8*) OSAL_Malloc(frame.fb_size);
    frame.fb_bus_address = (OMX_U32) frame.fb_bus_data;
    
    buf.bus_data = data;
    buf.bus_address = (OSAL_BUS_WIDTH) data;

    datalen = fread(data + buf.streamlen, 1, 1024*1024*2, in);
    buf.streamlen += datalen;
    if(datalen < 1024*1024*2)
    {
        eos = 1;
    }

    CODEC_PROTOTYPE* i= NULL;
    if (strstr(filename, ".h264") )
    {
        i = HantroHwDecOmx_decoder_create_h264(OMX_TRUE);
    }
    else if (strstr(filename, ".jpg") )
    {
        // TODO: hack
        buf.streamlen += 8 - (buf.streamlen % 8);
        i = HantroHwDecOmx_decoder_create_jpeg();
    }
    else if (strstr(filename, ".rcv") )
    {
        i = HantroHwDecOmx_decoder_create_vc1();
    }
    else if (strstr(filename, ".mpeg4") )
    {
        i = HantroHwDecOmx_decoder_create_mpeg4(OMX_TRUE);
    }
    assert( i );

    PP_ARGS args;
    memset( &args, 0, sizeof(PP_ARGS) );

    if(0)
    {
        args.crop.top = 128;
        args.crop.left = 128;
        args.crop.width = 128;
        args.crop.height = 128;
    }
    if(1)
    {
        args.scale.width = 352/2;//1920;
        args.scale.height = 288/2;//1920;
    }
    args.format = OMX_COLOR_FormatUnused;
//    args.format = OMX_COLOR_FormatYUV420SemiPlanar;

    s = i->setppargs( i, &args );
    assert( s == CODEC_OK );
    
    while (buf.streamlen > 0)
    {
        switch (s)
        {
            case CODEC_HAS_INFO:
            {
                s = i->getinfo(i, &info);
                if (s < 0)
                {
                    assert( s >= 0 );
                }
                y_offset = 0;
                c_offset = info.width*info.height;
            }
            break;

            case CODEC_HAS_FRAME:
            {
                frame.size = 0;
                frame.MB_err_count = 0;
                s = i->getframe(i, &frame, OMX_FALSE);
                if( s < 0 )
                {
                    //              assert( s >= 0 );
                }
    
                if( frame.size > 0 )
                {
                    if( update_frame( &info, &frame, &slice_count, &frames, yuvdata, &y_offset, &c_offset) )
                    {
                        store_frame( &info, out, yuvdata);
                    }
                }
            }
            break;

            case CODEC_OK:
            case CODEC_NEED_MORE:
            {
                OMX_S32 ret = i->scanframe(i, &buf, &first, &last);

                if ((ret && last != 0) || eos)
                {
                    memset(frame.fb_bus_data, 127, frame.fb_size);
                    frame.size = 0;
                    s = i->decode(i, &buf, &consumed, &frame);
                    if( s < 0 )
                    {
                        //              assert( s >= 0 );
                    }
    
                    if( frame.size > 0 )
                    {
                        if( update_frame( &info, &frame, &slice_count, &frames, yuvdata, &y_offset, &c_offset) )
                        {
                            store_frame( &info, out, yuvdata);
                        }
                    }
    
//                    printf("%u bytes decoded.\n", consumed );
                    buf.streamlen -=consumed;
                    memmove(data, data + consumed, buf.streamlen);
                    consumed = 0;
                }
                else
                {
                    datalen = fread(data + buf.streamlen, 1, 1024*1024, in);
                    buf.streamlen += datalen;
                    if(datalen < 1024*1024*2)
                    {
                        eos = 1;
                    }
                }
            }
            break;

            default:
            {
                printf("unhandled state %d\n", s);
                buf.streamlen = 0;
            }
            break;
        }
    }

    i->destroy(i);

    OSAL_Free(data);
    OSAL_Free( yuvdata );
    fclose(out);
    fclose(in);

    printf(" - - - decoding ended. %d frames decoded\n", (int)frames);
    printf("disp size = %dx%d, coded size = %dx%d\n", (int)info.width, (int)info.height, (int)info.stride, (int)info.sliceheight);
    if( info.format == OMX_COLOR_FormatL8 )
        printf("./yay-bw");
    else if( info.format == OMX_COLOR_FormatYUV420SemiPlanar )
        printf("./yay-420");
    else if( info.format == OMX_COLOR_FormatYUV422SemiPlanar )
        printf("./yay-422");
    else
    {
        printf("invalid format type !\n");
        exit(0);
    }
    printf(" -s %dx%d %s\n", (int)info.stride, (int)info.sliceheight, outfilename);
}



int main(int argc, const char* argv[])
{

    if(argc < 2)
    {
        printf("Usage: %s h264/jpg/vc1/mpeg4\n", argv[0]);
        exit(0);
    }

    if(strstr(argv[1],"h264"))
    {
        test("testdata_decoder/errorfree/case_703/stream.h264","case_703.yuv");
        test("testdata_decoder/errorcase/case_708/stream.h264","case_708-err.yuv");
        test("testdata_decoder/errorcase/case_952/stream.h264","case_952-err.yuv");
    }
    else if(strstr(argv[1],"jpg"))
    {
        test("testdata_decoder/errorfree/case_1000/stream.jpg","1000-1280x855.yuv");
        test("testdata_decoder/errorfree/case_1001/stream.jpg","1001-640x480.yuv");
        test("testdata_decoder/errorfree/case_1007/stream.jpg","1007-4672x3504.yuv");
        test("testdata_decoder/errorfree/case_1009/stream.jpg","1009-1280x855-yuv422.yuv");
        test("testdata_decoder/errorfree/case_1013/stream.jpg","1013-1269x854.yuv");
        test("testdata_decoder/errorfree/case_1026/stream.jpg","1026-4672x3504-bw.yuv");
        test("testdata_decoder/errorcase/case_1023/stream.jpg","1023-640x480-err.yuv");
    }
    else if(strstr(argv[1],"vc1"))
    {
        test("testdata_decoder/errorfree/case_1510/stream.rcv","case_1510.yuv");
        test("testdata_decoder/errorcase/case_1702/stream.rcv","case_1702-err.yuv");
    }
    else if(strstr(argv[1],"mpeg4"))
    {
        test("testdata_decoder/errorfree/case_12/stream.mpeg4","case_12.yuv");
        test("testdata_decoder/errorfree/case_46/stream.mpeg4","case_46.yuv");
        test("testdata_decoder/errorfree/case_10/stream.mpeg4","case_10.yuv");
        test("testdata_decoder/errorfree/case_34/stream.mpeg4","case_34.yuv");
        test("testdata_decoder/errorfree/case_127/stream.mpeg4","case_127.yuv");
        test("testdata_decoder/errorcase/case_55/stream.mpeg4","case_55-err.yuv");
    }
    return 0;
}
