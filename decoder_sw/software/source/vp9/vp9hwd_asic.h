/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
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

#ifndef __VP9_ASIC_H__
#define __VP9_ASIC_H__

#include "basetype.h"
#include "vp9hwd_container.h"
#include "regdrv.h"

#define DEC_8190_ALIGN_MASK 0x07U

#define VP9HWDEC_HW_RESERVED 0x0100
#define VP9HWDEC_SYSTEM_ERROR 0x0200
#define VP9HWDEC_SYSTEM_TIMEOUT 0x0300

void Vp9AsicInit(struct Vp9DecContainer *dec_cont);
i32 Vp9AsicAllocateMem(struct Vp9DecContainer *dec_cont);
#ifdef USE_EXTERNAL_BUFFER
void Vp9SetExternalBufferInfo(struct Vp9DecContainer *dec_cont);
i32 Vp9AsicReleaseMem(struct Vp9DecContainer *dec_cont);
#else
void Vp9AsicReleaseMem(struct Vp9DecContainer *dec_cont);
#endif
i32 Vp9AsicAllocateFilterBlockMem(struct Vp9DecContainer *dec_cont);
#ifdef USE_EXTERNAL_BUFFER
i32 Vp9AsicReleaseFilterBlockMem(struct Vp9DecContainer *dec_cont);
#else
void Vp9AsicReleaseFilterBlockMem(struct Vp9DecContainer *dec_cont);
#endif
i32 Vp9AsicAllocatePictures(struct Vp9DecContainer *dec_cont);
void Vp9AsicReleasePictures(struct Vp9DecContainer *dec_cont);

i32 Vp9AllocateFrame(struct Vp9DecContainer *dec_cont, u32 index);
i32 Vp9ReallocateFrame(struct Vp9DecContainer *dec_cont, u32 index);
void Vp9AsicInitPicture(struct Vp9DecContainer *dec_cont);
void Vp9AsicStrmPosUpdate(struct Vp9DecContainer *dec_cont, addr_t bus_address,
                          u32 data_len, addr_t buf_address, u32 buf_len);
u32 Vp9AsicRun(struct Vp9DecContainer *dec_cont, u32 pic_id);
u32 Vp9AsicSync(struct Vp9DecContainer *dec_cont);

void Vp9AsicProbUpdate(struct Vp9DecContainer *dec_cont);

void Vp9UpdateRefs(struct Vp9DecContainer *dec_cont, u32 corrupted);

i32 Vp9GetRefFrm(struct Vp9DecContainer *dec_cont, u32 id);
void Vp9UpdateProbabilities(struct Vp9DecContainer *dec_cont);
#ifdef USE_EXTERNAL_BUFFER
void Vp9AsicReset(struct Vp9DecContainer *dec_cont);
#endif

void Vp9FixChromaRFCTable(struct Vp9DecContainer *dec_cont);

#endif /* __VP9_ASIC_H__ */
