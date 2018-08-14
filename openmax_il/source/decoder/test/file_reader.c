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

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <OMX_Types.h>

#include "file_reader.h"
#include "basetype.h"
#include "util.h"
#include "dbgtrace.h"
#include "ivf.h"

#ifdef ENABLE_CODEC_RV
#include "rm_parse.h"
#include "rv_depack.h"
#include "rv_decode.h"
#include "rvdecapi.h"
#include "rv_ff_read.h"
#endif

#define DBGT_DECLARE_AUTOVAR
#include "dbgtrace.h"

#undef DBGT_PREFIX
#define DBGT_PREFIX "VIDEO_DECODER"

#define STREAMBUFFER_BLOCKSIZE 0xfffff

/*
 * VP6 stream file handling macros
 * */
#define leRushort( P, V) { \
    register ushort v = (uchar) *P++; \
    (V) = v + ( (ushort) (uchar) *P++ << 8); \
}
#define leRushortF( F, V) { \
    char b[2], *p = b; \
    fread( (void *) b, 1, 2, F); \
    leRushort( p, V); \
}
#define leRulong( P, V) { \
    register ulong v = (uchar) *P++; \
    v += (ulong) (uchar) *P++ << 8; \
    v += (ulong) (uchar) *P++ << 16; \
    (V) = v + ( (ulong) (uchar) *P++ << 24); \
}
#define leRulongF( F, V) { \
    char b[4] = {0,0,0,0}, *p = b; \
    V = 0; \
    fread( (void *) b, 1, 4, F); \
    leRulong( p, V); \
}
#if 0
#   define leWchar( P, V)  { * ( (char *) P)++ = (char) (V);}
#else
#   define leWchar( P, V)  { *P++ = (char) (V);}
#endif

#define leWshort( P, V)  { \
    register short v = (V); \
    leWchar( P, v) \
    leWchar( P, v >> 8) \
}
#define leWshortF( F, V) { \
    char b[2], *p = b; \
    leWshort( p, V); \
    fwrite( (void *) b, 1, 2, F); \
}

#define leWlong( P, V)  { \
    register long v = (V); \
    leWchar( P, v) \
    leWchar( P, v >> 8) \
    leWchar( P, v >> 16) \
    leWchar( P, v >> 24) \
}
#define leWlongF( F, V)  { \
    char b[4], *p = b; \
    leWlong( p, V); \
    fwrite( (void *) b, 1, 4, F); \
}
/*
 * VP6 stream file handling macros end
 * */

typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned char uchar;

extern int formatCheck;
extern OMX_BOOL VERBOSE_OUTPUT;
extern u32 traceUsedStream;
extern u32 previousUsed;

/* divx3 */
extern int divx3_width;
extern int divx3_height;
extern OMX_U32 offset;
extern int start_DIVX3;

int read_any_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    int ret = fread(buffer, 1, bufflen, strm);

    *eof = feof(strm);

    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", ret);

    DBGT_EPILOG("");
    return ret;
}

int read_mjpeg_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);

    u32 idx = 0;
    u32 eoi = 0;
    u8 *tempBuffer;
    fpos_t strmPos;
    DBGT_PROLOG("");

    tempBuffer = (u8*) buffer;

    if(fgetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION GET ERROR");
        DBGT_EPILOG("");
        return 0;
    }

    fread(tempBuffer+idx, sizeof(u8), 1, strm);
    if(ferror(strm))
    {
        DBGT_CRITICAL("STREAM READ ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    if(feof(strm))
    {
        //printf("END OF STREAM\n");
        *eof = feof(strm);
        DBGT_EPILOG("");
        return 0;
    }
    idx++;

    while(!eoi)
    {
        fread(tempBuffer+idx, sizeof(u8), 1, strm);
        if(feof(strm))
        {
            //printf("END OF STREAM\n");
            *eof = feof(strm);
            DBGT_EPILOG("");
            return 0;
        }

        if((0xFF == tempBuffer[idx-1]) && (0xD9 == tempBuffer[idx]))
            eoi = 1;

        idx++;
    }

    DBGT_EPILOG("");
    return idx;
}

int read_mpeg2_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    u32 idx = 0, VopStart = 0;
    u8 *tempBuffer;
    u32 StartCode = 0;
    fpos_t strmPos;

    tempBuffer = (u8*) buffer;

    if(fgetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION GET ERROR");
        DBGT_EPILOG("");
        return 0;
    }

    while(!VopStart)
    {
        fread(tempBuffer+idx, sizeof(u8), 1, strm);

        if(feof(strm))
        {
            fprintf(stdout, "TB: End of stream\n");
            *eof = 1;
            idx += 4;
            break;
        }

        if(idx > 7)
        {
            if((tempBuffer[idx - 3] == 0x00) &&
                        (tempBuffer[idx - 2] == 0x00) &&
                        (tempBuffer[idx - 1] == 0x01) &&
                        (tempBuffer[idx] == 0x00))
            {
                VopStart = 1;
                StartCode = ((tempBuffer[idx] << 24) |
                                (tempBuffer[idx - 1] << 16) |
                                (tempBuffer[idx - 2] << 8) |
                                tempBuffer[idx - 3]);
            }
        }
        idx++;
    }

    idx -= 4; // Next VOP start code was also read so we need to decrease 4
    traceUsedStream = previousUsed;
    previousUsed += idx;

    if(fsetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION SET ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    /* Read the rewind stream */
    fread(buffer, sizeof(u8), idx, strm);
    if(feof(strm))
    {
        *eof = feof(strm);
        DBGT_CRITICAL("TRYING TO READ STREAM BEYOND STREAM END");
        DBGT_EPILOG("");
        return 0;
    }
    if(ferror(strm))
    {
        DBGT_CRITICAL("FILE ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", idx);

    DBGT_EPILOG("");
    return (idx);
}

int read_mpeg4_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    u32 idx = 0, VopStart = 0;
    u8 *tempBuffer;
    u32 StartCode = 0;
    fpos_t strmPos;

    tempBuffer = (u8*) buffer;

    if(fgetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION GET ERROR");
        DBGT_EPILOG("");
        return 0;
    }

    while(!VopStart)
    {
        fread(tempBuffer+idx, sizeof(u8), 1, strm);

        if(feof(strm))
        {
            fprintf(stdout, "TB: End of stream\n");
            *eof = 1;
            idx += 4;
            break;
        }

        if(idx > 7)
        {
            if((tempBuffer[idx - 3] == 0x00) &&
                        (tempBuffer[idx - 2] == 0x00) &&
                        (tempBuffer[idx - 1] == 0x01) &&
                        ((tempBuffer[idx] == 0xB6) || (tempBuffer[idx] == 0xB1)))
            {
                VopStart = 1;
                StartCode = ((tempBuffer[idx] << 24) |
                                (tempBuffer[idx - 1] << 16) |
                                (tempBuffer[idx - 2] << 8) |
                                tempBuffer[idx - 3]);
            }
        }
        idx++;
    }

    idx -= 4; // Next VOP start code was also read so we need to decrease 4
    traceUsedStream = previousUsed;
    previousUsed += idx;

    if(fsetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION SET ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    /* Read the rewind stream */
    fread(buffer, sizeof(u8), idx, strm);
    if(feof(strm))
    {
        *eof = feof(strm);
        DBGT_CRITICAL("TRYING TO READ STREAM BEYOND STREAM END");
        DBGT_EPILOG("");
        return 0;
    }
    if(ferror(strm))
    {
        DBGT_CRITICAL("FILE ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", idx);

    DBGT_EPILOG("");
    return (idx);
}

int read_sorenson_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    u32 idx = 0, VopStart = 0;
    u8 *tempBuffer;
    u32 StartCode = 0;
    fpos_t strmPos;

    tempBuffer = (u8*) buffer;

    if(fgetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION GET ERROR");
        DBGT_EPILOG("");
        return 0;
    }

    while(!VopStart)
    {
        fread(tempBuffer+idx, sizeof(u8), 1, strm);

        if(feof(strm))
        {
            fprintf(stdout, "TB: End of stream\n");
            *eof = 1;
            idx += 4;
            break;
        }

        if(idx > 7)
        {
            if((tempBuffer[idx - 3] == 0x00) &&
                (tempBuffer[idx - 2] == 0x00) &&
                ((tempBuffer[idx - 1] & 0xF8) == 0x80))
            {
                VopStart = 1;
                StartCode = ((tempBuffer[idx] << 24) |
                                (tempBuffer[idx - 1] << 16) |
                                (tempBuffer[idx - 2] << 8) |
                                tempBuffer[idx - 3]);
            }
        }
        idx++;
    }

    idx -= 4; // Next VOP start code was also read so we need to decrease 4
    traceUsedStream = previousUsed;
    previousUsed += idx;

    if(fsetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION SET ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    /* Read the rewind stream */
    fread(buffer, sizeof(u8), idx, strm);
    if(feof(strm))
    {
        DBGT_CRITICAL("TRYING TO READ STREAM BEYOND STREAM END");
        DBGT_EPILOG("");
        *eof = feof(strm);
        return 0;
    }
    if(ferror(strm))
    {
        DBGT_CRITICAL("FILE ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", idx);

    DBGT_EPILOG("");
    return (idx);
}

int read_h263_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    u32 idx = 0, VopStart = 0;
    u8 *tempBuffer;
    u32 StartCode = 0;
    fpos_t strmPos;

    tempBuffer = (u8*) buffer;

    if(fgetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION GET ERROR");
        DBGT_EPILOG("");
        return 0;
    }

    while(!VopStart)
    {
        fread(tempBuffer+idx, sizeof(u8), 1, strm);

        if(feof(strm))
        {
            fprintf(stdout, "TB: End of stream\n");
            *eof = 1;
            idx += 4;
            break;
        }

        if(idx > 7)
        {
            if((tempBuffer[idx - 3] == 0x00) &&
                (tempBuffer[idx - 2] == 0x00) &&
                ((tempBuffer[idx - 1] & 0xFC) == 0x80))
            {
                VopStart = 1;
                StartCode = ((tempBuffer[idx] << 24) |
                                (tempBuffer[idx - 1] << 16) |
                                (tempBuffer[idx - 2] << 8) |
                                tempBuffer[idx - 3]);
            }
        }
        idx++;
    }

    idx -= 4; // Next VOP start code was also read so we need to decrease 4
    traceUsedStream = previousUsed;
    previousUsed += idx;

    if(fsetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION SET ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    /* Read the rewind stream */
    fread(buffer, sizeof(u8), idx, strm);
    if(feof(strm))
    {
        DBGT_CRITICAL("TRYING TO READ STREAM BEYOND STREAM END");
        DBGT_EPILOG("");
        *eof = feof(strm);
        return 0;
    }
    if(ferror(strm))
    {
        DBGT_CRITICAL("FILE ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", idx);

    return (idx);
}

int read_h264_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    //UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    u32 index = 0;
    u32 zeroCount;
    u8 byte;
    //u8 nextPacket = 0;
    i32 ret = 0;
    u8 firstRead = 1;
    fpos_t strmPos;
    //static u8 *stream = NULL;

    if(fgetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION GET ERROR");
        DBGT_EPILOG("");
        return 0;
    }

    /* test end of stream */
    ret = fread(&byte, sizeof(byte), 1, strm);
    if(ferror(strm))
    {
        DBGT_CRITICAL("STREAM READ ERROR");
        DBGT_EPILOG("");
        return 0;
    }
    if(feof(strm))
    {
        //printf("END OF STREAM\n");
        *eof = feof(strm);
        DBGT_EPILOG("");
        return 0;
    }

    /* leading zeros of first NAL unit */
    do
    {
        index++;
        /* the byte is already read to test the end of stream */
        if(!firstRead)
        {
            ret = fread(&byte, sizeof(byte), 1, strm);
            if(ferror(strm))
            {
                DBGT_CRITICAL("STREAM READ ERROR");
                DBGT_EPILOG("");
                return 0;
            }
        }
        else
        {
            firstRead = 0;
        }
    }
    while(byte != 1 && !feof(strm));

    /* invalid start code prefix */
    if(feof(strm) || index < 3)
    {
        DBGT_CRITICAL("INVALID BYTE STREAM");
        DBGT_EPILOG("");
        return 0;
    }

    zeroCount = 0;

    /* Search stream for next start code prefix */
    /*lint -e(716) while(1) used consciously */
    while(1)
    {
        /*byte = stream[index++]; */
        index++;
        ret = fread(&byte, sizeof(byte), 1, strm);
        if(ferror(strm))
        {
            DBGT_CRITICAL("FILE ERROR");
            DBGT_EPILOG("");
            return 0;
        }
        if(!byte)
            zeroCount++;

        if((byte == 0x01) && (zeroCount >= 2))
        {
            /* Start code prefix has two zeros
             * Third zero is assumed to be leading zero of next packet
             * Fourth and more zeros are assumed to be trailing zeros of this
             * packet */
            if(zeroCount > 3)
            {
                index -= 4;
                zeroCount -= 3;
            }
            else
            {
                index -= zeroCount + 1;
                zeroCount = 0;
            }
            break;
        }
        else if(byte)
            zeroCount = 0;

        if(feof(strm))
        {
            --index;
            break;
        }
    }

    /* Store pointer to the beginning of the packet */
    if(fsetpos(strm, &strmPos))
    {
        DBGT_CRITICAL("FILE POSITION SET ERROR");
        DBGT_EPILOG("");
        return 0;
    }

    if(index > (ulong)bufflen)
    {
        /* too big a frame */
        DBGT_CRITICAL("Input frame size %d > input buffer %lu", 
            index, (ulong)bufflen);
        DBGT_EPILOG("");
        return -1;
    }

    /* Read the rewind stream */
    fread(buffer, sizeof(byte), index, strm);
    if(feof(strm))
    {
        *eof = feof(strm);
        DBGT_CRITICAL("TRYING TO READ STREAM BEYOND STREAM END");
        DBGT_EPILOG("");
        return 0;
    }
   /* if(ferror(strm))
    {
        printf("FILE ERROR\n");
        return 0;
    }*/
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", index);

    DBGT_EPILOG("");
    return index;
}

int read_vp6_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    ulong frameSize = 0;

    leRulongF(strm, frameSize);

    if( frameSize > (ulong)bufflen)
    {
        /* too big a frame */
        DBGT_CRITICAL("Input frame size > input buffer");
        DBGT_EPILOG("");
        return -1;
    }

    fread(buffer, 1, frameSize, strm);

    *eof = feof(strm);
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %lu\n", frameSize);

    DBGT_EPILOG("");
    return (int) frameSize;

}

int read_DIVX3_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    OMX_U8  sizeTmp[4];
    int size, tmp;
    OMX_U8 mask = 0xff;
    OMX_U8 *tmpBuffer = (OMX_U8*) buffer;

    switch (start_DIVX3)
    {
        case 0:
        {
            /* set divx3 stream width and height */
            tmpBuffer[0] = divx3_width & mask;
            tmpBuffer[1] = (divx3_width >> 8) & mask;
            tmpBuffer[2] = (divx3_width >> 16) & mask;
            tmpBuffer[3] = (divx3_width >> 24) & mask;
            tmpBuffer[4] = divx3_height & mask;
            tmpBuffer[5] = (divx3_height >> 8) & mask;
            tmpBuffer[6] = (divx3_height >> 16) & mask;
            tmpBuffer[7] = (divx3_height >> 24) & mask;
            start_DIVX3 = 1;
            DBGT_EPILOG("");
            return 8;
        }
        break;
        default:
        {
            /* skip "00dc" from frame beginning (may signal video chunk start code) */
            for(;;)
            {
                fseek( strm, offset, SEEK_SET );
                fread( &sizeTmp, sizeof(OMX_U8), 4, strm );
                if(feof(strm))
                {
                    *eof = feof(strm);
                    DBGT_EPILOG("");
                    return 0;
                }
                if( (sizeTmp[0] == '0' &&
                    sizeTmp[1] == '0' &&
                    sizeTmp[2] == 'd' &&
                    sizeTmp[3] == 'c')  ||
                    ( sizeTmp[0] == 0x0 &&
                      sizeTmp[1] == 0x0 &&
                      sizeTmp[2] == 0x0 &&
                      sizeTmp[3] == 0x0 ) )
                {
                    offset += 4;
                    continue;
                }
                break;
            }

            size = (sizeTmp[0]) +
                   (sizeTmp[1] << 8) +
                   (sizeTmp[2] << 16) +
                   (sizeTmp[3] << 24);

            if( size == -1 )
            {
                *eof = 1;
                offset = 0;
                return 0;
            }

            tmp = fread( buffer, sizeof(OMX_U8), size, strm );
            if( size != tmp )
            {
                *eof = 1;
                offset = 0;
                return 0;
            }

            offset += size + 4;
            *eof = 0;
            if (VERBOSE_OUTPUT)
                printf("READ DECODE UNIT %d\n", size);

            DBGT_EPILOG("");
            return size;
        }
        break;
    }
    DBGT_ASSERT(0);
    DBGT_EPILOG("");
    return 0; // make gcc happy
}

int read_vp8_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    union {
        IVF_FRAME_HEADER ivf;
        u8 p[12];
    } fh;

    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    u32 ret = 0;
    u32 frameSize = 0;
    IVF_HEADER ivf;

    if (formatCheck == 0)
    {
        /* check format */
        char id[5] = "DKIF";
        char string[5];

        formatCheck = 1;

        if (fread(string, 1, 5, strm) == 5)
        {
            if (strncmp(id, string, 5))
            {
                DBGT_CRITICAL("ERROR: NOT VP8");
                DBGT_EPILOG("");
                return 0;
            }
        }
        rewind(strm);

        /* Read VP8 IVF file header */
        ret = fread( &ivf, sizeof(char), sizeof(IVF_HEADER), strm );
        if( ret == 0 )
        {
            DBGT_CRITICAL("Read IVF file header failed");
            DBGT_EPILOG("");
            return 0;
        }
    }

    /* Read frame header */
    ret = fread( &fh, sizeof(char), sizeof(IVF_FRAME_HEADER), strm );
    if( ret == 0 )
    {
        if(feof(strm))
        {
            *eof = 1;
            DBGT_EPILOG("");
            return 0;
        }
        DBGT_CRITICAL("Read frame header failed");
        DBGT_EPILOG("");
        return 0;
    }

    frameSize =
         fh.p[0] +
        (fh.p[1] << 8) +
        (fh.p[2] << 16) +
        (fh.p[3] << 24);

    if(feof(strm))
    {
        *eof = 1;
        DBGT_PDEBUG("EOF");
        DBGT_EPILOG("");
        return 0;
    }

    if(frameSize > (u32)bufflen)
    {
        DBGT_CRITICAL("VP8 frame size %d > buffer size %d",
            frameSize, bufflen );
        *eof = 1;
        DBGT_EPILOG("");
        return -1;
    }

    size_t result = fread( buffer, sizeof(u8), frameSize, strm );

    if (result != frameSize)
    {
        *eof = 1;
        frameSize = 0;
        return 0;
    }

    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %u\n", frameSize);

    DBGT_EPILOG("");
    return frameSize;
}

int read_webp_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    char signature[] = "WEBP";
    char format[] = "VP8 ";
    u8 tmp[4];
    u32 frameSize = 0;
    
    fseek(strm, 8, SEEK_CUR);
    fread(tmp, sizeof(u8), 4, strm);
    
    if (strncmp(signature, (char*)tmp, 4))
    {
        DBGT_EPILOG("");
        return 0;
    }
    
    fread(tmp, sizeof(u8), 4, strm);
    
    if (strncmp(format, (char*)tmp, 4))
    {
        DBGT_EPILOG("");
        return 0;
    }
    
    fread(tmp, sizeof(u8), 4, strm);
    
    frameSize = 
         tmp[0] +
        (tmp[1] << 8) +
        (tmp[2] << 16) +
        (tmp[3] << 24);
    
    if(frameSize > (u32)bufflen)
    {
        DBGT_CRITICAL("WebP frame size %d > buffer size %d",
            frameSize, bufflen );
        DBGT_EPILOG("");
        return -1;
    }

    fread( buffer, sizeof(u8), frameSize, strm );
    
    *eof = 1;

    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %u\n", frameSize);

    DBGT_EPILOG("");
    return frameSize;
}

//i32 strmRew = 0;
int read_avs_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
#define FBLOCK 1024
    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    u32 idx = 0, VopStart = 0;
    u8 *tempBuffer;
    i32 buffBytes = 0;
    u32 StartCode;

    tempBuffer = (u8*) buffer;

    while(!VopStart)
    {
        if(buffBytes == 0 && feof(strm))
        {
            fprintf(stdout, "TB: End of stream\n");
            *eof = 1;
            if (VERBOSE_OUTPUT)
                printf("READ DECODE UNIT %d\n", idx);
            DBGT_EPILOG("");
            return idx;
        }

        if (buffBytes == 0)
        {
            buffBytes = fread(tempBuffer+idx, sizeof(u8), FBLOCK, strm);
        }

        if(idx >= 4)
        {
            if(( (tempBuffer[idx - 3] == 0x00) &&
                 (tempBuffer[idx - 2] == 0x00) &&
                (( (tempBuffer[idx - 1] == 0x01) &&
                    (tempBuffer[idx] == 0xB3 ||
                    tempBuffer[idx] == 0xB6)))))
            {
                VopStart = 1;
                StartCode = ( (tempBuffer[idx] << 24) |
                              (tempBuffer[idx - 1] << 16) |
                              (tempBuffer[idx - 2] << 8) |
                               tempBuffer[idx - 3]);
            }
        }
        if(idx >= STREAMBUFFER_BLOCKSIZE)
        {
            fprintf(stdout, "idx = %d,length = %d \n", idx, STREAMBUFFER_BLOCKSIZE);
            fprintf(stdout, "TB: Out Of Stream Buffer\n");
            *eof = 1;
            DBGT_EPILOG("");
            return 0;
        }
        /*if(idx > strmRew + 128)
        {
            idx -= strmRew;
        }*/
        idx++;
        buffBytes--;
    }
    idx -= 4; // Next VOP start code was also read so we need to decrease 4
    buffBytes += 4;
    traceUsedStream = previousUsed;
    previousUsed += idx;

    if (buffBytes)
    {
        fseek(strm, -buffBytes, SEEK_CUR);
    }
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", idx);

#undef FBLOCK
    DBGT_EPILOG("");
    return (idx);
}

#ifdef ENABLE_CODEC_RV
int read_rv_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    extern pthread_mutex_t buff_mx;
    extern pthread_cond_t fillbuffer;
    extern OMX_BOOL rawRVFile;
    extern OMX_BOOL rawRV8;
    extern int empty_buffer_avail;
    extern int stream_end;
    extern RvSliceInfo slcInfo[128];
    extern rv_frame inputFrame;
    extern rv_backend_in_params inputParams;

    UNUSED_PARAMETER(bufflen);
    UNUSED_PARAMETER(state);
    DBGT_PROLOG("");

    OMX_U32 filledLength = 0;

    if(rawRVFile == OMX_TRUE)
    {
        u32 idx = 0, VopStart = 0;
        u8 *tempBuffer;
        //u32 StartCode = 0;
        fpos_t strmPos;

        tempBuffer = (u8*) buffer;

        if(fgetpos(strm, &strmPos))
        {
            DBGT_CRITICAL("FILE POSITION GET ERROR");
            DBGT_EPILOG("");
            return 0;
        }

        while(!VopStart)
        {
            fread(tempBuffer+idx, sizeof(u8), 1, strm);

            if(feof(strm))
            {
                fprintf(stdout, "TB: End of stream, idx %d\n", idx);
                *eof = 1;
                idx += 8;
                if (rawRV8 != OMX_TRUE)
                    idx++;
                break;
            }

            if(idx > 8)
            {
                if (rawRV8 == OMX_TRUE)
                {
                    if (tempBuffer[idx - 7] == 0 && tempBuffer[idx - 6] == 0 &&
                        tempBuffer[idx - 5] == 1 && !(tempBuffer[idx - 4]&170) &&
                        !(tempBuffer[idx - 3]&170) && !(tempBuffer[idx - 2]&170) &&
                        (tempBuffer[idx - 1]&170) == 2)
                    {
                        VopStart = 1;
                    }
                }
                else
                {
                    if (tempBuffer[idx - 8] == 85  && tempBuffer[idx - 7] == 85 &&
                        tempBuffer[idx - 6] == 85  && tempBuffer[idx - 5] == 85 &&
                        !(tempBuffer[idx - 4]&170) && !(tempBuffer[idx - 3]&170) &&
                        !(tempBuffer[idx - 2]&170) && (tempBuffer[idx - 1]&170) == 2)
                    {
                        VopStart = 1;
                    }
                }
            }
            idx++;
        }

        idx -= 8; // Next VOP start code was also read so we need to decrease 8

        if (rawRV8 != OMX_TRUE)
            idx--;

        traceUsedStream = previousUsed;
        previousUsed += idx;

        if(fsetpos(strm, &strmPos))
        {
            DBGT_CRITICAL("FILE POSITION SET ERROR");
            DBGT_EPILOG("");
            return 0;
        }
        /* Read the rewind stream */
        fread(buffer, sizeof(u8), idx, strm);
        if(feof(strm))
        {
            *eof = feof(strm);
            DBGT_CRITICAL("TRYING TO READ STREAM BEYOND STREAM END");
            DBGT_EPILOG("");
            return 0;
        }
        if(ferror(strm))
        {
            DBGT_CRITICAL("FILE ERROR");
            DBGT_EPILOG("");
            return 0;
        }
        if (VERBOSE_OUTPUT)
            printf("READ DECODE UNIT %d\n", idx);
        DBGT_EPILOG("");
        return (idx);
    }
    else
    {
        pthread_mutex_lock(&buff_mx);

        if(empty_buffer_avail && !stream_end)
            pthread_cond_wait(&fillbuffer, &buff_mx);

        pthread_mutex_unlock(&buff_mx);

        if(stream_end == 1)
        {
            *eof = OMX_TRUE;
            filledLength = 0;
        }
        else
        {
            OMX_OTHER_EXTRADATATYPE * segDataBuffer;
            memcpy(buffer, inputFrame.pData, inputParams.dataLength);

            segDataBuffer = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U32)buffer + inputParams.dataLength +3)& ~3);

            segDataBuffer->nDataSize = 8 * (inputParams.numDataSegments+1);

            memcpy(segDataBuffer->data, slcInfo, segDataBuffer->nDataSize);

            memset(slcInfo,0,sizeof( RvDecSliceInfo)*128);

            filledLength = inputParams.dataLength;

            pthread_mutex_lock(&buff_mx);

            empty_buffer_avail++;
            /* start for file format */
            pthread_cond_signal(&fillbuffer);

            pthread_mutex_unlock(&buff_mx);
        }
    }
    if (VERBOSE_OUTPUT)
        printf("READ DECODE UNIT %d\n", (int)filledLength);
    DBGT_EPILOG("");
    return filledLength;
}
#endif

int read_rcv_file(FILE* strm, char* buffer, int bufflen, void* state, OMX_BOOL* eof)
{
    DBGT_PROLOG("");
    RCVSTATE* rcv = (RCVSTATE*)state;

    switch (ftell(strm))
    {
        case 0:
        {
            fseek(strm, 0, SEEK_END);
            rcv->filesize = ftell(strm);
            fseek(strm, 0, SEEK_SET);
            char buff[4];
            fread(buff, 1, 4, strm);
            rcv->advanced = buff[2];
            rcv->rcV1     = !(buff[3] & 0x40);
            fseek(strm, 0, SEEK_SET);
            if (rcv->advanced)
                break;
                //return fread(buffer, 1, bufflen, strm);

            if (rcv->rcV1)
            {
                DBGT_EPILOG("");
                return fread(buffer, 1, 20, strm);
            }
            else
            {
                DBGT_EPILOG("");
                return fread(buffer, 1, 36, strm);
            }
        }
        break;
        default:
        {
            if (rcv->advanced)
            {
                break;
/*                int ret = fread(buffer, 1, bufflen, strm);
                *eof = feof(strm);
                return ret;*/
            }

            int framesize = 0;
            int ret = 0;
            unsigned char buff[8];
            if (rcv->rcV1)
                fread(buff, 1, 4, strm);
            else
                fread(buff, 1, 8, strm);

            framesize |= buff[0];
            framesize |= buff[1] << 8;
            framesize |= buff[2] << 16;

            DBGT_ASSERT(framesize > 0);
            if (bufflen < framesize)
            {
                DBGT_CRITICAL("Data buffer is too small for vc-1 frame");
                DBGT_CRITICAL("buffer: %d framesize: %d", bufflen, framesize);
                DBGT_EPILOG("");
                return -1;
            }
            ret = fread(buffer, 1, framesize, strm);
            *eof = ftell(strm) == rcv->filesize;
            if (VERBOSE_OUTPUT)
                printf("READ DECODE UNIT %d\n", framesize);
            DBGT_EPILOG("");
            return ret;
        }
        break;
    }

    if (rcv->advanced)
    {
        u32 index = 0;
        u32 zeroCount;
        u8 byte;
        i32 ret = 0;
        u8 firstRead = 1;
        fpos_t strmPos;

        if(fgetpos(strm, &strmPos))
        {
            DBGT_CRITICAL("FILE POSITION GET ERROR");
            DBGT_EPILOG("");
            return 0;
        }

        /* test end of stream */
        ret = fread(&byte, sizeof(byte), 1, strm);
        if(ferror(strm))
        {
            DBGT_CRITICAL("STREAM READ ERROR");
            DBGT_EPILOG("");
            return 0;
        }
        if(feof(strm))
        {
            printf("END OF STREAM\n");
            *eof = feof(strm);
            return 0;
        }
        /* leading zeros */
        do
        {
            index++;
            /* the byte is already read to test the end of stream */
            if(!firstRead)
            {
                ret = fread(&byte, sizeof(byte), 1, strm);
                if(ferror(strm))
                {
                    DBGT_CRITICAL("STREAM READ ERROR");
                    DBGT_EPILOG("");
                    return 0;
                }
            }
            else
            {
                firstRead = 0;
            }
        }
        while(byte != 1 && !feof(strm));

        /* invalid start code prefix */
        if(feof(strm) || index < 3)
        {
            DBGT_CRITICAL("INVALID BYTE STREAM");
            DBGT_EPILOG("");
            return 0;
        }

        zeroCount = 0;

        /* Search stream for next start code prefix */
        /*lint -e(716) while(1) used consciously */
        while(1)
        {
            /*byte = stream[index++]; */
            index++;
            ret = fread(&byte, sizeof(byte), 1, strm);
            if(ferror(strm))
            {
                DBGT_CRITICAL("FILE ERROR");
                DBGT_EPILOG("");
                return 0;
            }
            if(!byte)
                zeroCount++;

            if((byte == 0x01) && (zeroCount >= 2))
            {
                /* Start code prefix has two zeros
                 * Third zero is assumed to be leading zero of next packet
                 * Fourth and more zeros are assumed to be trailing zeros of this
                 * packet */
                if(zeroCount > 3)
                {
                    index -= 4;
                    zeroCount -= 3;
                }
                else
                {
                    index -= zeroCount + 1;
                    zeroCount = 0;
                }
                break;
            }
            else if(byte)
                zeroCount = 0;

            if(feof(strm))
            {
                --index;
                break;
            }

        }
        /* Store pointer to the beginning of the packet */
        if(fsetpos(strm, &strmPos))
        {
            DBGT_CRITICAL("FILE POSITION SET ERROR");
            DBGT_EPILOG("");
            return 0;
        }

        /* Read the rewind stream */
        fread(buffer, sizeof(byte), index, strm);
        if(feof(strm))
        {
            *eof = feof(strm);
            DBGT_CRITICAL("TRYING TO READ STREAM BEYOND STREAM END");
            DBGT_EPILOG("");
            return 0;
        }
        /*if(ferror(strm))
        {
            printf("FILE ERROR\n");
            return 0;
        }*/
        if (VERBOSE_OUTPUT)
            printf("READ DECODE UNIT %d\n", index);
        DBGT_EPILOG("");
        return index;
    }
    DBGT_ASSERT(0);
    DBGT_EPILOG("");
    return 0; // make gcc happy
}

#if 0
int test_rv_file_read(const char* input_file, const char* yuv_file,  void* readstate)
{
    FILE* video = NULL;
    FILE* yuv   = NULL;
    int ret = 0;

    video = fopen(input_file, "rb");
    if (video==NULL)
    {
        printf("failed to open input file:%s\n", input_file);
        ret = -1;
    }
    if (yuv_file)
    {
        yuv = fopen(yuv_file, "wb");
        if (yuv==NULL)
        {
            printf("failed to open output file\n");
            ret = -1;
        }
    }

    OMX_BOOL eof                 = OMX_FALSE;
    int bufflen                  = 300000;

    void *buffer                 = malloc(bufflen);
    memset(buffer,0,bufflen);

    while (eof == OMX_FALSE)
    {
        int size = read_rv_file(video, buffer, bufflen, readstate, &eof);

        if (size != 0)
        {
            fwrite(buffer,1,size, yuv);
        }
    }
    printf("close files\n");
    if (video) fclose(video);

    if (yuv)
    {
        fclose(yuv);
    }
    printf("step3\n");
    return ret;
}
#endif
