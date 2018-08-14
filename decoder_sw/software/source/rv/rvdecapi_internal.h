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

#ifndef RV_DECAPI_INTERNAL_H
#define RV_DECAPI_INTERNAL_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "rv_cfg.h"
#include "rv_utils.h"
#include "rvdecapi.h"
#include "regdrv_g1.h"

/*------------------------------------------------------------------------------
    2. Internal Definitions
------------------------------------------------------------------------------*/

#define RV_DEC_X170_IRQ_DEC_RDY         DEC_8190_IRQ_RDY
#define RV_DEC_X170_IRQ_BUS_ERROR       DEC_8190_IRQ_BUS
#define RV_DEC_X170_IRQ_BUFFER_EMPTY    DEC_8190_IRQ_BUFFER
#define RV_DEC_X170_IRQ_STREAM_ERROR    DEC_8190_IRQ_ERROR
#define RV_DEC_X170_IRQ_TIMEOUT         DEC_8190_IRQ_TIMEOUT
#define RV_DEC_X170_IRQ_ABORT           DEC_8190_IRQ_ABORT
#define RV_DEC_X170_IRQ_CLEAR_ALL       0xFF

#define RV_DEC_X170_MAX_NUM_SLICES  (128)

/*
 *  Size of internal frame buffers (in 32bit-words) per macro block
 */
#define RVAPI_DEC_FRAME_BUFF_SIZE  96

#ifndef NULL
#define NULL 0
#endif

#define SWAP_POINTERS(A, B, T) T = A; A = B; B = T;

/* MAX_BUFFER_NUMBERS */
#define INVALID_ANCHOR_PICTURE ((u32)16)

/*------------------------------------------------------------------------------
    3. Prototypes of Decoder API internal functions
------------------------------------------------------------------------------*/

void rv_api_init_data_structures(DecContainer * dec_cont);
RvDecRet rvAllocateBuffers(DecContainer * dec_cont);
RvDecRet rvDecCheckSupport(DecContainer * dec_cont);
void rvDecPreparePicReturn(DecContainer * dec_cont);
void rvDecAspectRatio(DecContainer * dec_cont, RvDecInfo * dec_info);
void rvDecBufferPicture(DecContainer * dec_cont, u32 pic_id, u32 buffer_b,
                        u32 is_inter, RvDecRet return_value, u32 nbr_err_mbs);
void rvFreeBuffers(DecContainer * dec_cont);
void rvInitVlcTables(DecContainer * dec_cont);
RvDecRet rvAllocateRprBuffer(DecContainer * dec_cont);

#endif /* RV_DECAPI_INTERNAL_H */
