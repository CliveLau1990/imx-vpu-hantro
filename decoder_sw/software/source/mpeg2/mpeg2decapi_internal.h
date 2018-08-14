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

#ifndef _MPEG2DECAPI_INTERNAL_H_
#define _MPEG2DECAPI_INTERNAL_H_

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "mpeg2hwd_cfg.h"
#include "mpeg2hwd_utils.h"
#include "mpeg2decapi.h"
#include "regdrv_g1.h"
/*------------------------------------------------------------------------------
    2. Internal Definitions
------------------------------------------------------------------------------*/
#define MPEG2_DEC_X170_MODE_MPEG2      5
#define MPEG2_DEC_X170_MODE_MPEG1      6

#define MPEG2_DEC_X170_IRQ_DEC_RDY          DEC_8190_IRQ_RDY
#define MPEG2_DEC_X170_IRQ_BUS_ERROR        DEC_8190_IRQ_BUS
#define MPEG2_DEC_X170_IRQ_BUFFER_EMPTY     DEC_8190_IRQ_BUFFER
#define MPEG2_DEC_X170_IRQ_ASO              DEC_8190_IRQ_ASO
#define MPEG2_DEC_X170_IRQ_STREAM_ERROR     DEC_8190_IRQ_ERROR
#define MPEG2_DEC_X170_IRQ_TIMEOUT          DEC_8190_IRQ_TIMEOUT
#define MPEG2_DEC_X170_IRQ_CLEAR_ALL        0xFF

/*
 *  Size of internal frame buffers (in 32bit-words) per macro block
 */
#define MPEG2API_DEC_FRAME_BUFF_SIZE  96

/*
 *  Size of CTRL buffer (macroblks * 4 * 32bit-words/Mb), same for MV and DC
 */
#define MPEG2API_DEC_CTRL_BUFF_SIZE   NBR_OF_WORDS_MB * MPEG2API_DEC_MBS

#define MPEG2API_DEC_MV_BUFF_SIZE     NBR_MV_WORDS_MB * MPEG2API_DEC_MBS

#define MPEG2API_DEC_DC_BUFF_SIZE     NBR_DC_WORDS_MB * MPEG2API_DEC_MBS

#define MPEG2API_DEC_NBOFRLC_BUFF_SIZE MPEG2API_DEC_MBS * 6

#ifndef NULL
#define NULL 0
#endif

#define SWAP_POINTERS(A, B, T) T = A; A = B; B = T;

/* MAX_BUFFER_NUMBERS */
#define INVALID_ANCHOR_PICTURE ((u32)32)

/*------------------------------------------------------------------------------
    3. Prototypes of Decoder API internal functions
------------------------------------------------------------------------------*/

/*void regDump(Mpeg2DecInst decInst);*/
void mpeg2_api_init_data_structures(DecContainer * dec_cont);
void mpeg2DecTimeCode(DecContainer * dec_cont, Mpeg2DecTime * time_code);
Mpeg2DecRet mpeg2AllocateBuffers(DecContainer * dec_cont);
void mpeg2HandleQTables(DecContainer * dec_cont);
void mpeg2HandleMpeg1Parameters(DecContainer * dec_cont);
Mpeg2DecRet mpeg2DecCheckSupport(DecContainer * dec_cont);
void mpeg2DecPreparePicReturn(DecContainer * dec_cont);
void mpeg2DecAspectRatio(DecContainer * dec_cont, Mpeg2DecInfo * dec_info);
void mpeg2DecBufferPicture(DecContainer * dec_cont, u32 pic_id, u32 buffer_b,
                           u32 is_inter, Mpeg2DecRet return_value, u32 nbr_err_mbs);
Mpeg2DecRet mpeg2DecAllocExtraBPic(DecContainer * dec_cont);
void mpeg2FreeBuffers(DecContainer * dec_cont);

#endif /* _MPEG2DECAPI_INTERNAL_H_ */
