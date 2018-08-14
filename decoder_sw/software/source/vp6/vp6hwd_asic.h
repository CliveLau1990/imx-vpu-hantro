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

#ifndef __VP6HWD_ASIC_H__
#define __VP6HWD_ASIC_H__

#include "basetype.h"
#include "vp6hwd_container.h"
#include "regdrv_g1.h"

#define DEC_8190_ALIGN_MASK         0x07
#define DEC_8190_MODE_VP6           0x07U

#define VP6HWDEC_HW_RESERVED         0x0100
#define VP6HWDEC_SYSTEM_ERROR        0x0200
#define VP6HWDEC_SYSTEM_TIMEOUT      0x0300

void VP6HwdAsicInit(VP6DecContainer_t * dec_cont);
i32 VP6HwdAsicAllocateMem(VP6DecContainer_t * dec_cont);
void VP6HwdAsicReleaseMem(VP6DecContainer_t * dec_cont);
i32 VP6HwdAsicAllocatePictures(VP6DecContainer_t * dec_cont);
void VP6HwdAsicReleasePictures(VP6DecContainer_t * dec_cont);

void VP6HwdAsicInitPicture(VP6DecContainer_t * dec_cont);
void VP6HwdAsicStrmPosUpdate(VP6DecContainer_t * dec_cont);
u32 VP6HwdAsicRun(VP6DecContainer_t * dec_cont);

void VP6HwdAsicProbUpdate(VP6DecContainer_t * dec_cont);

#endif /* __VP6HWD_ASIC_H__ */
