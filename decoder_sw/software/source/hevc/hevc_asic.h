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

#ifndef HEVC_ASIC_H_
#define HEVC_ASIC_H_

#include "basetype.h"
#include "dwl.h"
#include "hevc_container.h"
#include "hevc_storage.h"

#define ASIC_CABAC_INIT_BUFFER_SIZE 3 * 160
/* 8 dc + 8 to boundary + 6*16 + 2*6*64 + 2*64 -> 63 * 16 bytes */
#define ASIC_SCALING_LIST_SIZE 16 * 64

/* tile border coefficients of filter */
#define ASIC_VERT_SAO_RAM_SIZE 48 /* bytes per pixel */

#define X170_DEC_TIMEOUT 0x00FFU
#define X170_DEC_SYSTEM_ERROR 0x0FFFU
#define X170_DEC_HW_RESERVED 0xFFFFU

u32 AllocateAsicBuffers(struct HevcDecContainer *dec_cont,
                        struct HevcDecAsic *asic_buff);
#ifndef USE_EXTERNAL_BUFFER
void ReleaseAsicBuffers(const void *dwl, struct HevcDecAsic *asic_buff);
#else
i32 ReleaseAsicBuffers(struct HevcDecContainer *dec_cont,
                       struct HevcDecAsic *asic_buff);
#endif

u32 AllocateAsicTileEdgeMems(struct HevcDecContainer *dec_cont);
void ReleaseAsicTileEdgeMems(struct HevcDecContainer *dec_cont);

void HevcSetupVlcRegs(struct HevcDecContainer *dec_cont);

void HevcInitRefPicList(struct HevcDecContainer *dec_cont);

u32 HevcRunAsic(struct HevcDecContainer *dec_cont,
                struct HevcDecAsic *asic_buff);
void HevcSetRegs(struct HevcDecContainer *dec_cont);

#endif /* HEVC_ASIC_H_ */
