/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2006 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
--
--  Abstract  :   NAL unit handling
--
------------------------------------------------------------------------------*/

#ifndef __H264_NAL_UNIT_H__
#define __H264_NAL_UNIT_H__

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "basetype.h"
#include "H264PutBits.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

typedef struct
{
    u32 anchorPicFlag;
    u32 priorityId;
    u32 viewId;
    u32 temporalId;
    u32 interViewFlag;
} mvc_s;

#define MAX_GOP_SIZE 16

typedef struct
{
    int layer;
    int isRef;
    int orderCmd;
    int markCmd;
} gopinfo_s;

typedef struct 
{
    u32 level;
    gopinfo_s gop[MAX_GOP_SIZE];
    i32 gopIndex;  /* -1: IDR; 0~GOP_SIZE-1: index in gop */
    i32 gopLength;
} svc_s;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/
void H264NalUnitHdr(stream_s * stream, i32 nalRefIdc, nalUnitType_e
                    nalUnitType, true_e byteStream);
void H264NalUnitHdrMvcExtension(stream_s * stream, mvc_s * mvc);
void H264NalUnitHdrSvcExtension(stream_s * stream, svc_s * svc);
void H264NalUnitTrailinBits(stream_s * stream, true_e byteStream);
u32 H264FillerNALU(stream_s * sp, i32 cnt, true_e byteStream);

#endif
