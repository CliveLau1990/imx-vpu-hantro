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

#ifndef TB_CFG_H
#define TB_CFG_H

#include "refbuffer.h"
#include "tb_defs.h"
#include "dwl.h"

/*------------------------------------------------------------------------------
    Parse mode constants
------------------------------------------------------------------------------*/
enum TBCfgCallbackResult {
  TB_CFG_OK,
  TB_CFG_ERROR = 500,
  TB_CFG_INVALID_BLOCK = 501,
  TB_CFG_INVALID_PARAM = 502,
  TB_CFG_INVALID_VALUE = 503,
  TB_CFG_INVALID_CODE = 504,
  TB_CFG_DUPLICATE_BLOCK = 505
};

/*------------------------------------------------------------------------------
    Parse mode constants
------------------------------------------------------------------------------*/
enum TBCfgCallbackParam {
  TB_CFG_CALLBACK_BLK_START = 300,
  TB_CFG_CALLBACK_VALUE = 301,
};

/*------------------------------------------------------------------------------
    Interface to parsing
------------------------------------------------------------------------------*/
typedef enum TBCfgCallbackResult (*TBCfgCallback)(char*, char*, char*,
    enum TBCfgCallbackParam,
    void*);
enum TBCfgCallbackResult TBReadParam(char* block, char* key, char* value,
                                     enum TBCfgCallbackParam state,
                                     void* cb_param);

TBBool TBParseConfig(char* filename, TBCfgCallback callback, void* cb_param);
void TBSetDefaultCfg(struct TBCfg* tb_cfg);
void TBPrintCfg(const struct TBCfg* tb_cfg);
u32 TBCheckCfg(const struct TBCfg* tb_cfg);

u32 TBGetPPDataDiscard(const struct TBCfg* tb_cfg);
u32 TBGetPPClockGating(const struct TBCfg* tb_cfg);
u32 TBGetPPWordSwap(const struct TBCfg* tb_cfg);
u32 TBGetPPWordSwap16(const struct TBCfg* tb_cfg);
u32 TBGetPPInputPictureEndian(const struct TBCfg* tb_cfg);
u32 TBGetPPOutputPictureEndian(const struct TBCfg* tb_cfg);

u32 TBGetDecErrorConcealment(const struct TBCfg* tb_cfg);
u32 TBGetDecRlcModeForced(const struct TBCfg* tb_cfg);
u32 TBGetDecMemoryAllocation(const struct TBCfg* tb_cfg);
u32 TBGetDecDataDiscard(const struct TBCfg* tb_cfg);
u32 TBGetDecClockGating(const struct TBCfg* tb_cfg);
u32 TBGetDecClockGatingRuntime(const struct TBCfg* tb_cfg);
u32 TBGetDecOutputFormat(const struct TBCfg* tb_cfg);
u32 TBGetDecOutputPictureEndian(const struct TBCfg* tb_cfg);

u32 TBGetTBPacketByPacket(const struct TBCfg* tb_cfg);
u32 TBGetTBNalUnitStream(const struct TBCfg* tb_cfg);
u32 TBGetTBStreamHeaderCorrupt(const struct TBCfg* tb_cfg);
u32 TBGetTBStreamTruncate(const struct TBCfg* tb_cfg);
u32 TBGetTBSliceUdInPacket(const struct TBCfg* tb_cfg);
u32 TBGetTBFirstTraceFrame(const struct TBCfg* tb_cfg);

u32 TBGetDecRefbuEnabled(const struct TBCfg* tb_cfg);
u32 TBGetDecRefbuDisableEvalMode(const struct TBCfg* tb_cfg);
u32 TBGetDecRefbuDisableCheckpoint(const struct TBCfg* tb_cfg);
u32 TBGetDecRefbuDisableOffset(const struct TBCfg* tb_cfg);
u32 TBGetDec64BitEnable(const struct TBCfg* tb_cfg);
u32 TBGetDecSupportNonCompliant(const struct TBCfg* tb_cfg);
u32 TBGetDecIntraFreezeEnable(const struct TBCfg* tb_cfg);
u32 TBGetDecDoubleBufferSupported(const struct TBCfg* tb_cfg);
u32 TBGetDecTopBotSumSupported(const struct TBCfg* tb_cfg);
void TBGetHwConfig(const struct TBCfg* tb_cfg, DWLHwConfig* hw_cfg);
void TBSetRefbuMemModel( const struct TBCfg* tb_cfg, u32 *reg_base, struct refBuffer *p_refbu );
u32 TBGetDecForceMpeg4Idct(const struct TBCfg* tb_cfg);
u32 TBGetDecCh8PixIleavSupported(const struct TBCfg* tb_cfg);
void TBRefbuTestMode( struct refBuffer *p_refbu, u32 *reg_base,
                      u32 is_intra_frame, u32 mode );

u32 TBGetDecApfThresholdEnabled(const struct TBCfg* tb_cfg);

u32 TBGetDecServiceMergeDisable(const struct TBCfg* tb_cfg);

u32 TBGetDecBusWidth(const struct TBCfg* tb_cfg);

#endif /* TB_CFG_H */
