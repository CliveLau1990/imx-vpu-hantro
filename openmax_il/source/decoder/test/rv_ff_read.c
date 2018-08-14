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

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "basetype.h"
#include "rm_parse.h"
#include "rv_depack.h"
#include "rv_decode.h"
#include "rvdecapi.h"
#include "rv_ff_read.h"
#include "util.h"

#define RM2YUV_INITIAL_READ_SIZE    16

#define MB_MULTIPLE(x)  (((x)+15)&~15)

/* Linear memory area descriptor */
typedef struct DWLLinearMem
{
    u32 *virtualAddress;
    u32 busAddress;
    u32 size;
} DWLLinearMem_t;

extern pthread_mutex_t buff_mx;
extern pthread_cond_t fillbuffer;

extern int empty_buffer_avail;
extern int headers_ready;
extern int stream_end;

u32 pctszSize = 0;
u32 maxCodedWidth = 0;
u32 maxCodedHeight = 0;
u32 bIsRV8 = 0;

u32 maxFrameWidth = 0;
u32 maxFrameHeight = 0;
u32 enableFramePicture = 1;
u8 *frameBuffer;
FILE *frameOut = NULL;
char outFileName[256] = "";
char outFileNameTiled[256] = "out_tiled.yuv";
u32 disableOutputWriting = 0;
u32 planarOutput = 0;
u32 tiledOutput = 0;
u32 frameNumber = 0;
u32 numberOfWrittenFrames = 0;
FILE *fTiledOutput = NULL;

u32 rvInput = 0;

    RvSliceInfo slcInfo[128];
    rv_decode  frameInfo;
    rv_frame                   inputFrame;
    rv_backend_in_params       inputParams;

#ifdef RV_TIME_TRACE
    #include "../timer/timer.h"
    #define TIMER_MAX_COUNTER   0xFFFFFFFF
    #define TIMER_FACTOR_MSECOND 1000
    u32 ulStartTime_DEC, ulStopTime_DEC;    /* HW decode time (us) */
    u32 ulStartTime_LCD, ulStopTime_LCD;    /* display time (us) */
    u32 ulStartTime_Full, ulStopTime_Full;  /* total time (us), including parser(sw), driver(sw) & decoder(hw) */
    double fMaxTime_DEC, fTime_DEC, fTotalTime_DEC;
    double fMaxTime_LCD, fTime_LCD, fTotalTime_LCD;
    double fMaxTime_Full, fTime_Full, fTotalTime_Full;
    char strTimeInfo[0x100];
#endif

#define DWLFile FILE

#define DWL_SEEK_CUR    SEEK_CUR
#define DWL_SEEK_END    SEEK_END
#define DWL_SEEK_SET    SEEK_SET

#define DWLfopen(filename,mode)                 fopen(filename, mode)
#define DWLfread(buf,size,_size_t,filehandle)   fread(buf,size,_size_t,filehandle)
#define DWLfwrite(buf,size,_size_t,filehandle)  fwrite(buf,size,_size_t,filehandle)
#define DWLftell(filehandle)                    ftell(filehandle)
#define DWLfseek(filehandle,offset,whence)      fseek(filehandle,offset,whence)
#define DWLfrewind(filehandle)                  rewind(filehandle)
#define DWLfclose(filehandle)                   fclose(filehandle)
#define DWLfeof(filehandle)                     feof(filehandle)

#define DEBUG_PRINT(msg)
#define ERROR_PRINT(msg)
#define ASSERT(s)

static u32 g_iswritefile = 0;
static u32 g_isdisplay = 0;
static u32 g_displaymode = 0;
static u32 g_bfirstdisplay = 0;
static u32 g_rotationmode = 0;
static u32 g_blayerenable = 0;
static DWLFile *g_fpTimeLog = NULL;


typedef struct
{
    char         filePath[0x100];
    DWLFile*     fpOut;
    rv_depack*   pDepack;
    rv_decode*   pDecode;
    BYTE*        pOutFrame;
    UINT32       ulOutFrameSize;
    UINT32       ulWidth;
    UINT32       ulHeight;
    UINT32       ulNumInputFrames;
    UINT32       ulNumOutputFrames;
    UINT32       ulMaxNumDecFrames;
    UINT32       ulStartFrameID;    /* 0-based */
    UINT32       bDecEnd;
    UINT32       ulTotalTime;       /* ms */
} rm2yuv_info;

static HX_RESULT rv_frame_available(void* pAvail, UINT32 ulSubStreamNum, rv_frame* pFrame);
static HX_RESULT rv_decode_stream_decode(rv_decode* pFrontEnd);

static UINT32 rm_io_read(void* pUserRead, BYTE* pBuf, UINT32 ulBytesToRead);
static void rm_io_seek(void* pUserRead, UINT32 ulOffset, UINT32 ulOrigin);
static void* rm_memory_malloc(void* pUserMem, UINT32 ulSize);
static void rm_memory_free(void* pUserMem, void* ptr);
static void* rm_memory_create_ncnb(void* pUserMem, UINT32 ulSize);
static void rm_memory_destroy_ncnb(void* pUserMem, void* ptr);

//static void parse_path(const char *path, char *dir, char *file);
static void rm2yuv_error(void* pError, HX_RESULT result, const char* pszMsg);
static void RV_Time_Init(i32 islog2std, i32 islog2file);
static void RV_Time_Full_Reset(void);
static void RV_Time_Full_Pause(void);
static void RV_Time_Dec_Reset(void);
static void RV_Time_Dec_Pause(u32 picid, u32 pictype);

static u32 maxNumPics = 0;
static u32 picsDecoded = 0;

static void rm2yuv_error(void* pError, HX_RESULT result, const char* pszMsg)
{
    UNUSED_PARAMETER(pError);
    UNUSED_PARAMETER(result);
    UNUSED_PARAMETER(pszMsg);
}
/*-----------------------------------------------------------------------------------------------------
function:
    demux input rm file, decode rv stream threrein, and display on LCD or write to file if necessary

input:
    filename:       the file path and name to be display
    startframeid:   the beginning frame of the whole input stream to be decoded
    maxnumber:      the maximal number of frames to be decoded
    iswritefile:    write the decoded frame to local file or not, 0: no, 1: yes
    isdisplay:      send the decoded data to display device or not, 0: no, 1: yes
    displaymode:    display mode on LCD, 0: auto size, 1: full screen
    rotationmode:   rotation mode on LCD, 0: normal, 1: counter-clockwise, 2: clockwise
    islog2std:      output time log to console
    islog2file:     output time log to file
    errconceal:     error conceal mode (reserved)

return:
    NULL
------------------------------------------------------------------------------------------------------*/
u32 NextPacket(u8 ** pStrm, u8* streamStop, u32 isRv8)
{
    u32 index;
    u32 maxIndex;
    u32 zeroCount;
    static u32 prevIndex = 0;
    static u8 *stream = NULL;
    u8 *p;

    index = 0;

    if(stream == NULL)
        stream = *pStrm;
    else
        stream += prevIndex;

    maxIndex = (u32) (streamStop - stream);

    if(stream > streamStop)
        return (0);

    if(maxIndex == 0)
        return (0);

    zeroCount = 0;

    /* Search stream for next start code prefix */
    /*lint -e(716) while(1) used consciously */
    p = stream + 1;
    while(p < streamStop)
    {
        /* RV9 */
        if (isRv8)
        {
            if (p[0] == 0   && p[1] == 0   && p[2] == 1  &&
                !(p[3]&170) && !(p[4]&170) && !(p[5]&170) && (p[6]&170) == 2)
                break;
        }
        else
        {
            if (p[0] == 85  && p[1] == 85  && p[2] == 85  && p[3] == 85 &&
                !(p[4]&170) && !(p[5]&170) && !(p[6]&170) && (p[7]&170) == 2)
                break;
        }
        p++;

    }

    index = p - stream;
    /* Store pointer to the beginning of the packet */
    *pStrm = stream;
    prevIndex = index;

    return (index);
}


void rv_display(void * pTdata)
{
    HX_RESULT           retVal         = HXR_OK;
    DWLFile*            fpIn           = HXNULL;
    INT32               lBytesRead     = 0;
    UINT32              ulNumStreams   = 0;
    UINT32              i              = 0;
    UINT16              usStreamNum    = 0;
    UINT32              ulOutFrameSize = 0;
    UINT32              ulFramesPerSec = 0;
    rm_parser*          pParser        = HXNULL;
    rm_stream_header*   pHdr           = HXNULL;
    rm_packet*          pPacket        = HXNULL;
    rv_depack*          pDepack        = HXNULL;
    rv_decode*          pDecode        = HXNULL;
    rv_format_info*     pInfo          = HXNULL;
    rm2yuv_info         info;
    BYTE                ucBuf[RM2YUV_INITIAL_READ_SIZE];
    char                rv_outfilename[256];
    int islog2std, islog2file;
#ifdef RV_TIME_TRACE
    UINT32              ulStartTime = TIMER_GET_NOW_TIME();
    UINT32              ulStopTime = 0;
#endif

thread_data* pTd = (thread_data*) pTdata;



    /*RVDecInstInit RVInstInit;*/

    /* Initialize all static global variables */
    g_iswritefile = 1;
    g_isdisplay = 0;
    g_displaymode = 0;
    g_rotationmode = 0;
    g_bfirstdisplay = 0;
    g_blayerenable = 0;

#ifdef RV_ERROR_SIM
    g_randomerror = 0;
#endif

    islog2std = 1;
    islog2file = islog2file;

    RV_Time_Init(islog2std, islog2file);

    memset(rv_outfilename, 0, sizeof(rv_outfilename));

    /* NULL out the info */
    memset(info.filePath, 0, sizeof(info.filePath));
    info.fpOut             = HXNULL;
    info.pDepack           = HXNULL;
    info.pDecode           = HXNULL;
    info.pOutFrame         = HXNULL;
    info.ulWidth           = 0;
    info.ulHeight          = 0;
    info.ulOutFrameSize    = 0;
    info.ulNumInputFrames  = 0;
    info.ulNumOutputFrames = 0;
    info.ulMaxNumDecFrames = 100000000;
    info.ulStartFrameID    = 0;
    info.bDecEnd           = 0;
    info.ulTotalTime       = 0;

    fpIn = pTd->pFile;



    /* Read the first few bytes of the file */
    lBytesRead = (INT32) DWLfread((void*) ucBuf, 1, RM2YUV_INITIAL_READ_SIZE, fpIn);
    if (lBytesRead != RM2YUV_INITIAL_READ_SIZE)
    {
        ERROR_PRINT(("Could not read %d bytes at the beginning of %s\n",));
        goto cleanup;
    }
    /* Seek back to the beginning */
    DWLfseek(fpIn, 0, DWL_SEEK_SET);



    /* Create the parser struct */
    pParser = rm_parser_create2(HXNULL,
        rm2yuv_error,
        HXNULL,
        rm_memory_malloc,
        rm_memory_free);
    if (!pParser)
    {
        goto cleanup;
    }

    /* Set the file into the parser */
    retVal = rm_parser_init_io(pParser,
        (void *)fpIn,
        (rm_read_func_ptr)rm_io_read,
        (rm_seek_func_ptr)rm_io_seek);
    if (retVal != HXR_OK)
    {
        goto cleanup;
    }

    /* Read all the headers at the beginning of the .rm file */
    retVal = rm_parser_read_headers(pParser);
    if (retVal != HXR_OK)
    {
        goto cleanup;
    }

    /* Get the number of streams */
    ulNumStreams = rm_parser_get_num_streams(pParser);
    if (ulNumStreams == 0)
    {
        ERROR_PRINT(("Error: rm_parser_get_num_streams() returns 0\n"));
        goto cleanup;
    }

    /* Now loop through the stream headers and find the video */
    for (i = 0; i < ulNumStreams && retVal == HXR_OK; i++)
    {
        retVal = rm_parser_get_stream_header(pParser, i, &pHdr);
        if (retVal == HXR_OK)
        {
            if (rm_stream_is_realvideo(pHdr))
            {
                usStreamNum = (UINT16) i;
                break;
            }
            else
            {
                /* Destroy the stream header */
                rm_parser_destroy_stream_header(pParser, &pHdr);
            }
        }
    }

    /* Do we have a RealVideo stream in this .rm file? */
    if (!pHdr)
    {
        ERROR_PRINT(("There is no RealVideo stream in this file.\n"));
        goto cleanup;
    }

    /* Create the RealVideo depacketizer */
    pDepack = rv_depack_create2_ex((void*) &info,
        rv_frame_available,
        HXNULL,
        rm2yuv_error,
        HXNULL,
        rm_memory_malloc,
        rm_memory_free,
        rm_memory_create_ncnb,
        rm_memory_destroy_ncnb);
    if (!pDepack)
    {
        goto cleanup;
    }

    /* Assign the rv_depack pointer to the info struct */
    info.pDepack = pDepack;

    /* Initialize the RV depacketizer with the RealVideo stream header */
    retVal = rv_depack_init(pDepack, pHdr);
    if (retVal != HXR_OK)
    {
        goto cleanup;
    }

    /* Get the bitstream header information, create rv_infor_format struct and init it */
    retVal = rv_depack_get_codec_init_info(pDepack, &pInfo);
    if (retVal != HXR_OK)
    {
        goto cleanup;
    }

    /*
    * Get the frames per second. This value is in 32-bit
    * fixed point, where the upper 16 bits is the integer
    * part of the fps, and the lower 16 bits is the fractional
    * part. We're going to truncate to integer, so all
    * we need is the upper 16 bits.
    */
    ulFramesPerSec = pInfo->ufFramesPerSecond >> 16;

    /* Create an rv_decode object */
    pDecode = rv_decode_create2(HXNULL,
        rm2yuv_error,
        HXNULL,
        rm_memory_malloc,
        rm_memory_free);
    if (!pDecode)
    {
        goto cleanup;
    }

    /* Assign the decode object into the rm2yuv_info struct */
    info.pDecode = pDecode;

    /* Init the rv_decode object */
    retVal = rv_decode_init(pDecode, pInfo);
    if (retVal != HXR_OK)
    {
        goto cleanup;
    }

    /* Get the size of an output frame */
    ulOutFrameSize = rv_decode_max_output_size(pDecode);
    if (!ulOutFrameSize)
    {
        goto cleanup;
    }
    info.ulOutFrameSize = ulOutFrameSize;

    DEBUG_PRINT(("Start decode ...\n"));

    maxCodedWidth = pDecode->ulLargestPels;
    maxCodedHeight = pDecode->ulLargestLines;
    maxFrameWidth = MB_MULTIPLE( maxCodedWidth );
    maxFrameHeight = MB_MULTIPLE( maxCodedHeight );
    pctszSize = pDecode->ulPctszSize;
    bIsRV8 = pDecode->bIsRV8;



    /* Fill in the width and height */
    info.ulWidth  = (pDecode->ulLargestPels+15)&0xFFFFFFF0;
    info.ulHeight = (pDecode->ulLargestLines+15)&0xFFFFFFF0;

    RV_Time_Full_Reset();

    /* Signal that headers ready and decoder can start */
    pthread_mutex_lock(&buff_mx);
    headers_ready=1;
    pthread_cond_signal(&fillbuffer);
    pthread_mutex_unlock(&buff_mx);


    /* Wait for available buffer before getting first frame */
    pthread_mutex_lock(&buff_mx);
    if(!empty_buffer_avail)
    {
        /* wait for signal */
       pthread_cond_wait(&fillbuffer, &buff_mx);
    }
    /* unlock mutex */
    pthread_mutex_unlock(&buff_mx);

    /* Now keep getting packets until we receive an error */
    while (retVal == HXR_OK && !info.bDecEnd)
    {
        /* Get the next packet */
        retVal = rm_parser_get_packet(pParser, &pPacket);
        if (retVal == HXR_OK)
        {
            /* Is this a video packet? */
            if (pPacket->usStream == usStreamNum)
            {
                /*
                * Put the packet into the depacketizer. When frames
                * are available, we will get a callback to
                * rv_frame_available().
                */
                retVal = rv_depack_add_packet(pDepack, pPacket);
            }
            /* Destroy the packet */
            rm_parser_destroy_packet(pParser, &pPacket);
        }

        if (maxNumPics && picsDecoded >= maxNumPics)
            break;
    }

    /* Display results */
    DEBUG_PRINT(("Video stream in decoding complete: %lu input frames, %lu output frames\n",
        (const char*) info.ulNumInputFrames, info.ulNumOutputFrames));

#ifdef RV_TIME_TRACE
    ulStopTime = TIMER_GET_NOW_TIME();
    if (ulStopTime <= ulStartTime)
        info.ulTotalTime = (TIMER_MAX_COUNTER-ulStartTime+ulStopTime)/TIMER_FACTOR_MSECOND;
    else
        info.ulTotalTime = (ulStopTime-ulStartTime)/TIMER_FACTOR_MSECOND;

    TIME_PRINT(("Video stream in  decoding complete:\n");
    TIME_PRINT(("    width is %d, height is %d\n",info.ulWidth, info.ulHeight));
    TIME_PRINT(("    %lu input frames, %lu output frames\n",info.ulNumInputFrames, info.ulNumOutputFrames));
    TIME_PRINT(("    total time is %lu ms, avarage time is %lu ms, fps is %6.2f\n",
        info.ulTotalTime, info.ulTotalTime/info.ulNumOutputFrames, 1000.0/(info.ulTotalTime/info.ulNumOutputFrames)));
#endif

    /* If the error was just a normal "out-of-packets" error,
    then clean it up */
    if (retVal == HXR_NO_DATA)
    {
        retVal = HXR_OK;
    }

cleanup:

    /* Destroy the codec init info */
    if (pInfo)
    {
        rv_depack_destroy_codec_init_info(pDepack, &pInfo);
    }
    /* Destroy the depacketizer */
    if (pDepack)
    {
        rv_depack_destroy(&pDepack);
    }
    /* If we have a packet, destroy it */
    if (pPacket)
    {
        rm_parser_destroy_packet(pParser, &pPacket);
    }
    /* Destroy the stream header */
    if (pHdr)
    {
        rm_parser_destroy_stream_header(pParser, &pHdr);
    }
    /* Destroy the rv_decode object */
    if (pDecode)
    {
        rv_decode_destroy(pDecode);
        pDecode = HXNULL;
    }
    /* Destroy the parser */
    if (pParser)
    {
        rm_parser_destroy(&pParser);
    }

    /* Close the output file */
    if (info.fpOut)
    {
        DWLfclose(info.fpOut);
        info.fpOut = HXNULL;
    }

    if(fTiledOutput)
    {
        DWLfclose(fTiledOutput);
        fTiledOutput = NULL;
    }

    /* Close the time log file */
    if (g_fpTimeLog)
    {
        DWLfclose(g_fpTimeLog);
        g_fpTimeLog = HXNULL;
    }

    /* signal stream end */
    pthread_mutex_lock(&buff_mx);
    stream_end= 1;
    empty_buffer_avail = 0;

    pthread_cond_signal(&fillbuffer);
    pthread_mutex_unlock(&buff_mx);
    pthread_exit(NULL);
}

/*------------------------------------------------------------------------------
function
    rv_frame_available()

purpose
    If enough data have been extracted from rm packte(s) for an entire frame,
    this function will be called to decode the available stream data.
------------------------------------------------------------------------------*/

HX_RESULT rv_frame_available(void* pAvail, UINT32 ulSubStreamNum, rv_frame* pFrame)
{
    UNUSED_PARAMETER(ulSubStreamNum);
    HX_RESULT retVal = HXR_FAIL;

    if (pAvail && pFrame)
    {
        /* Get the info pointer */
        rm2yuv_info* pInfo = (rm2yuv_info*) pAvail;
        if (pInfo->pDepack && pInfo->pDecode)
        {
            /* Put the frame into rv_decode */
            retVal = rv_decode_stream_input(pInfo->pDecode, pFrame);
            if (HX_SUCCEEDED(retVal))
            {
                /* Increment the number of input frames */
                pInfo->ulNumInputFrames++;

                /* Decode frames until there aren't any more */
                do
                {
                    /* skip all B-Frames before Frame g_startframeid */
                    if (pInfo->pDecode->pInputFrame->usSequenceNum < pInfo->ulStartFrameID
                        && pInfo->pDecode->pInputFrame->ucCodType == 3/*RVDEC_TRUEBPIC*/)
                    {
                        retVal = HXR_OK;
                        break;
                    }
                    retVal = rv_decode_stream_decode(pInfo->pDecode);
                    RV_Time_Full_Pause();
                    RV_Time_Full_Reset();

                    if (HX_SUCCEEDED(retVal))
                    {
                    }
                    else
                    {
                        pInfo->bDecEnd = 1;
                    }
                }
                while (HX_SUCCEEDED(retVal) && rv_decode_more_frames(pInfo->pDecode));
            }
//foo:
            rv_depack_destroy_frame(pInfo->pDepack, &pFrame);
        }
    }

    return retVal;
}


/*------------------------------------------------------------------------------
function
    rv_decode_stream_decode()

purpose
    Calls the decoder backend (HW) to decode issued frame
    and output a decoded frame if any possible.

return
    Returns zero on success, negative result indicates failure.

------------------------------------------------------------------------------*/
HX_RESULT rv_decode_stream_decode(rv_decode *pFrontEnd)
{
    HX_RESULT retVal = HXR_FAIL;
    UINT32 i = 0;

    RV_Time_Dec_Reset();
    RV_Time_Dec_Pause(0,0);

    pFrontEnd->pOutputParams.notes |= RV_DECODE_DONT_DRAW;
    pFrontEnd->pOutputParams.notes &= ~RV_DECODE_MORE_FRAMES;
    retVal = HXR_OK;

    frameNumber++;

    inputFrame.pData = pFrontEnd->pInputFrame->pData;
    inputFrame.ulDataBusAddress = pFrontEnd->pInputFrame->ulDataBusAddress;
    inputFrame.usSequenceNum = pFrontEnd->pInputFrame->usSequenceNum;
    inputFrame.ulTimestamp  = pFrontEnd->pInputFrame->ulTimestamp;

    inputParams.dataLength =
        pFrontEnd->pInputParams.dataLength;
    inputParams.numDataSegments  =
        pFrontEnd->pInputParams.numDataSegments;

    for (i = 0; i < pFrontEnd->pInputParams.numDataSegments+1; i++)
    {
                slcInfo[i].offset  = pFrontEnd->pInputParams.pDataSegments[i].ulOffset;
                slcInfo[i].isValid = pFrontEnd->pInputParams.pDataSegments[i].bIsValid;
    }

/* Free buffer for use in decoder  */

    /* lock mutex */
    pthread_mutex_lock(&buff_mx);
    empty_buffer_avail = 0;
    /* signal buffer ready */
    pthread_cond_signal(&fillbuffer);
    /* unlock mutex */
    pthread_mutex_unlock(&buff_mx);

    /* for a free buffer */

    pthread_mutex_lock(&buff_mx);
    if(!empty_buffer_avail)
        pthread_cond_wait(&fillbuffer, &buff_mx);
    pthread_mutex_unlock(&buff_mx);

    return HXR_OK;
}

#if 0
/*------------------------------------------------------------------------------
function
    parse_path

purpose
    parse filename and path

parameter
    path(I):    full path of input file
    dir(O):     directory path
    file(O):    file name
-----------------------------------------------------------------------------*/
void parse_path(const char *path, char *dir, char *file)
{
    int len = 0;
    char *ptr = NULL;

    if (path == NULL)
        return;

    len = strlen(path);
    if (len == 0)
        return;

    ptr = path + len - 1;    // point to the last char
    while (ptr != path)
    {
        if ((*ptr == '\\') || (*ptr == '/'))
            break;

        ptr--;
    }

    if (file != NULL)
    {
        if (ptr != path)
            strcpy(file, ptr + 1);
        else
            strcpy(file, ptr);
    }

    if ((ptr != path) && (dir != NULL))
    {
        len = ptr - path + 1;    // with the "/" or "\"
        memcpy(dir, path, len);
        dir[len] = '\0';
    }
}
#endif

/*------------------------------------------------------------------------------
function
    rm_io_read

purpose
    io read interface for rm parse/depack
-----------------------------------------------------------------------------*/
UINT32 rm_io_read(void* pUserRead, BYTE* pBuf, UINT32 ulBytesToRead)
{
    UINT32 ulRet = 0;

    if (pUserRead && pBuf && ulBytesToRead)
    {
        /* The void* is a DWLFile* */
        DWLFile *fp = (DWLFile *) pUserRead;
        /* Read the number of bytes requested */
        ulRet = (UINT32) DWLfread(pBuf, 1, ulBytesToRead, fp);
    }

    return ulRet;
}

/*------------------------------------------------------------------------------
function
    rm_io_seek

purpose
    io seek interface for rm parse/depack
-----------------------------------------------------------------------------*/
void rm_io_seek(void* pUserRead, UINT32 ulOffset, UINT32 ulOrigin)
{
    if (pUserRead)
    {
        /* The void* is a DWLFile* */
        DWLFile *fp = (DWLFile *) pUserRead;
        /* Do the seek */
        DWLfseek(fp, ulOffset, ulOrigin);
    }
}

/*------------------------------------------------------------------------------
function
    rm_memory_malloc

purpose
    memory (sw only) allocation interface for rm parse/depack
-----------------------------------------------------------------------------*/
void* rm_memory_malloc(void* pUserMem, UINT32 ulSize)
{
    pUserMem = pUserMem;

    return (void*)malloc(ulSize);
}

/*------------------------------------------------------------------------------
function
    rm_memory_free

purpose
    memory (sw only) free interface for rm parse/depack
-----------------------------------------------------------------------------*/
void rm_memory_free(void* pUserMem, void* ptr)
{
    pUserMem = pUserMem;

    free(ptr);
}

/*------------------------------------------------------------------------------
function
    rm_memory_create_ncnb

purpose
    memory (sw/hw share) allocation interface for rm parse/depack
-----------------------------------------------------------------------------*/
void* rm_memory_create_ncnb(void* pUserMem, UINT32 ulSize)
{
    DWLLinearMem_t *pBuf = NULL;
    pUserMem = pUserMem;

    pBuf = (DWLLinearMem_t *)malloc(sizeof(DWLLinearMem_t));
    if (pBuf == NULL)
        return NULL;
    pBuf->virtualAddress = malloc(ulSize);
    pBuf->busAddress = (unsigned long)pBuf->virtualAddress;
    pBuf->size = ulSize;

    return pBuf;
}

/*------------------------------------------------------------------------------
function
    rm_memory_destroy_ncnb

purpose
    memory (sw/hw share) release interface for rm parse/depack
-----------------------------------------------------------------------------*/
void rm_memory_destroy_ncnb(void* pUserMem, void* ptr)
{
    UNUSED_PARAMETER(pUserMem);
    DWLLinearMem_t *pBuf = NULL;

    if (ptr == NULL)
        return;

    pBuf = *(DWLLinearMem_t **)ptr;

    if (pBuf == NULL)
        return;


    free(pBuf->virtualAddress);
    free(pBuf);
    *(DWLLinearMem_t **)ptr = NULL;
}

/*------------------------------------------------------------------------------
Function name:  RvDecTrace()

Functional description:
    This implementation appends trace messages to file named 'dec_api.trc'.

Argument:
    string - trace message, a null terminated string
------------------------------------------------------------------------------*/

void RvDecTrace(const char *str)
{
    DWLFile *fp = NULL;

    fp = DWLfopen("dec_api.trc", "a");
    if(!fp)
        return;

    DWLfwrite(str, 1, strlen(str), fp);
    DWLfwrite("\n", 1, 1, fp);

    DWLfclose(fp);
}


void RV_Time_Init(i32 islog2std, i32 islog2file)
{
#ifdef RV_TIME_TRACE
    g_islog2std = islog2std;
    g_islog2file = islog2file;

    if (g_islog2file)
    {
    #ifdef WIN32
        g_fpTimeLog = DWLfopen("rvtime.log", "wb");
    #else
        g_fpTimeLog = DWLfopen("rvtime.log", "w");
    #endif
    }

    fMaxTime_DEC = 0.0;
    fMaxTime_LCD = 0.0;
    fMaxTime_Full = 0.0;
    fTotalTime_DEC = 0.0;
    fTotalTime_LCD = 0.0;
    fTotalTime_Full = 0.0;
#else
    UNUSED_PARAMETER(islog2std);
    UNUSED_PARAMETER(islog2file);
#endif
}

void RV_Time_Full_Reset(void)
{
#ifdef RV_TIME_TRACE
    fTime_Full = 0.0;
    ulStartTime_Full = TIMER_GET_NOW_TIME();
#endif
}

void RV_Time_Full_Pause(void)
{
#ifdef RV_TIME_TRACE
    ulStopTime_Full = TIMER_GET_NOW_TIME();
    if (ulStopTime_Full <= ulStartTime_Full)
        fTime_Full += (double)((TIMER_MAX_COUNTER-ulStartTime_Full+ulStopTime_Full)/TIMER_FACTOR_MSECOND);
    else
        fTime_Full += (double)((ulStopTime_Full-ulStartTime_Full)/TIMER_FACTOR_MSECOND);

    if(fTime_Full > fMaxTime_Full)
        fMaxTime_Full = fTime_Full;

    fTotalTime_Full += fTime_Full;

    if (g_islog2std)
    {
        TIME_PRINT((" | \tFullTime= %-6.0f ms", fTime_Full));
    }
    if (g_islog2file && g_fpTimeLog)
    {
        sprintf(strTimeInfo, " | \tFullTime= %-6.0f ms", fTime_Full);
        DWLfwrite(strTimeInfo, 1, strlen(strTimeInfo), g_fpTimeLog);
    }
#endif
}

void RV_Time_Dec_Reset(void)
{
#ifdef RV_TIME_TRACE
    fTime_DEC = 0.0;
    ulStartTime_DEC = TIMER_GET_NOW_TIME();
#endif
}

void RV_Time_Dec_Pause(u32 picid, u32 pictype)
{
#ifdef RV_TIME_TRACE
    ulStopTime_Full = TIMER_GET_NOW_TIME();
    if (ulStopTime_Full <= ulStartTime_Full)
        fTime_Full += (double)((TIMER_MAX_COUNTER-ulStartTime_Full+ulStopTime_Full)/TIMER_FACTOR_MSECOND);
    else
        fTime_Full += (double)((ulStopTime_Full-ulStartTime_Full)/TIMER_FACTOR_MSECOND);

    ulStopTime_DEC = TIMER_GET_NOW_TIME();
    if (ulStopTime_DEC <= ulStartTime_DEC)
        fTime_DEC += (double)((TIMER_MAX_COUNTER-ulStartTime_DEC+ulStopTime_DEC)/TIMER_FACTOR_MSECOND);
    else
        fTime_DEC += (double)((ulStopTime_DEC-ulStartTime_DEC)/TIMER_FACTOR_MSECOND);

    if(fTime_DEC > fMaxTime_DEC)
        fMaxTime_DEC = fTime_DEC;

    fTotalTime_DEC += fTime_DEC;

    if (g_islog2std)
    {
        TIME_PRINT(("\nDecode:  ID= %-6d  PicType= %-4d  time= %-6.0f ms\t", picid, pictype, fTime_DEC));
    }
    if (g_islog2file && g_fpTimeLog)
    {
        sprintf(strTimeInfo, "\nDecode:  ID= %-6d  PicType= %-4d  time= %-6.0f ms\t", picid, pictype, fTime_DEC);
        DWLfwrite(strTimeInfo, 1, strlen(strTimeInfo), g_fpTimeLog);
    }

    ulStartTime_Full = TIMER_GET_NOW_TIME();
#else
    UNUSED_PARAMETER(picid);
    UNUSED_PARAMETER(pictype);
#endif
}
