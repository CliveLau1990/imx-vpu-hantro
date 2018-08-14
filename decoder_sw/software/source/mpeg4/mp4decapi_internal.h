/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
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

#ifndef _MP4DECAPI_INTERNAL_H_
#define _MP4DECAPI_INTERNAL_H_

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "mp4deccfg.h"
#include "mp4dechwd_utils.h"
#include "mp4decapi.h"

/*------------------------------------------------------------------------------
    2. Internal Definitions
------------------------------------------------------------------------------*/
/*
 *  Size of internal frame buffers (in 32bit-words) per macro block
 */
#define MP4API_DEC_FRAME_BUFF_SIZE  96

/*
 *  Size of CTRL buffer (macroblks * 4 * 32bit-words/Mb), same for MV and DC
 */
#define MP4API_DEC_CTRL_BUFF_SIZE   NBR_OF_WORDS_MB * MP4API_DEC_MBS

#define MPAPI_DEC_MV_BUFF_SIZE      NBR_MV_WORDS_MB * MP4API_DEC_MBS

#define MPAPI_DEC_DC_BUFF_SIZE      NBR_DC_WORDS_MB * MP4API_DEC_MBS

#define MP4API_DEC_NBOFRLC_BUFF_SIZE MP4API_DEC_MBS * 6

#ifndef NULL
#define NULL 0
#endif

#define SWAP_POINTERS(A, B, T) T = A; A = B; B = T;

/* MAX_BUFFER_NUMBERS */
#define INVALID_ANCHOR_PICTURE ((u32)32)

#define MP4DEC_QUANT_TABLE_SIZE (2*64)

/*------------------------------------------------------------------------------
    3. Prototypes of Decoder API internal functions
------------------------------------------------------------------------------*/

/*void regDump(MP4DecInst decInst);*/
void MP4NotCodedVop(DecContainer * dec_container);
void MP4API_InitDataStructures(DecContainer * dec_cont);
void MP4DecTimeCode(DecContainer * dec_cont, MP4DecTime * time_code);
MP4DecRet MP4AllocateBuffers(DecContainer * dec_cont);
void MP4FreeBuffers(DecContainer * dec_cont);
MP4DecRet MP4AllocateRlcBuffers(DecContainer * dec_cont);
MP4DecRet MP4DecCheckSupport(DecContainer * dec_cont);
void MP4DecPixelAspectRatio(DecContainer * dec_cont, MP4DecInfo * dec_info);
void MP4DecBufferPicture(DecContainer *dec_cont, u32 pic_id,
                         u32 vop_type, u32 nbr_err_mbs);
MP4DecRet MP4DecAllocExtraBPic(DecContainer * dec_cont);
u32 * MP4DecResolveVirtual(DecContainer * dec_cont, u32 index );
addr_t MP4DecResolveBus(DecContainer * dec_cont, u32 index );
void MP4DecChangeDataIndex( DecContainer * dec_cont, u32 to, u32 from);
u32 MP4DecBFrameSupport(DecContainer * dec_cont);
void MP4SetQuantMatrix(DecContainer * dec_cont);

#endif /* _MP4DECAPI_INTERNAL_H_ */
