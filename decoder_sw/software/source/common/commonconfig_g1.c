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

#include "commonconfig.h"
#include "dwl.h"
#include "deccfg.h"
#include "regdrv_g1.h"

#ifdef _ASSERT_USED
#ifndef ASSERT
#include <assert.h>
#define ASSERT(expr) assert(expr)
#endif
#else
#define ASSERT(expr)
#endif


void SetCommonConfigRegs(u32 *regs,u32 client_type) {

  /* set parameters defined in deccfg.h */
  SetDecRegister(regs, HWIF_DEC_OUT_ENDIAN,
                 DEC_X170_OUTPUT_PICTURE_ENDIAN);

  SetDecRegister(regs, HWIF_DEC_IN_ENDIAN,
                 DEC_X170_INPUT_DATA_ENDIAN);

  SetDecRegister(regs, HWIF_DEC_STRENDIAN_E,
                 DEC_X170_INPUT_STREAM_ENDIAN);

  SetDecRegister(regs, HWIF_DEC_MAX_BURST,
                 DEC_X170_BUS_BURST_LENGTH);

  if ((DWLReadAsicID(client_type) >> 16) == 0x8170U) {
    SetDecRegister(regs, HWIF_PRIORITY_MODE,
                   DEC_X170_ASIC_SERVICE_PRIORITY);
  } else {
    SetDecRegister(regs, HWIF_DEC_SCMD_DIS,
                   DEC_X170_SCMD_DISABLE);
  }

#if (DEC_X170_APF_DISABLE)
  SetDecRegister(regs, HWIF_DEC_ADV_PRE_DIS, 1);
  SetDecRegister(regs, HWIF_APF_THRESHOLD, 0);
#else
  {
    u32 apf_tmp_threshold = 0;

    SetDecRegister(regs, HWIF_DEC_ADV_PRE_DIS, 0);

    if(DEC_X170_REFBU_SEQ)
      apf_tmp_threshold = DEC_X170_REFBU_NONSEQ/DEC_X170_REFBU_SEQ;
    else
      apf_tmp_threshold = DEC_X170_REFBU_NONSEQ;

    if( apf_tmp_threshold > 63 )
      apf_tmp_threshold = 63;

    SetDecRegister(regs, HWIF_APF_THRESHOLD, apf_tmp_threshold);
  }
#endif

  SetDecRegister(regs, HWIF_DEC_LATENCY,
                 DEC_X170_LATENCY_COMPENSATION);

  SetDecRegister(regs, HWIF_DEC_DATA_DISC_E,
                 DEC_X170_DATA_DISCARD_ENABLE);

  SetDecRegister(regs, HWIF_DEC_OUTSWAP32_E,
                 DEC_X170_OUTPUT_SWAP_32_ENABLE);

  SetDecRegister(regs, HWIF_DEC_INSWAP32_E,
                 DEC_X170_INPUT_DATA_SWAP_32_ENABLE);

  SetDecRegister(regs, HWIF_DEC_STRSWAP32_E,
                 DEC_X170_INPUT_STREAM_SWAP_32_ENABLE);

#if( DEC_X170_HW_TIMEOUT_INT_ENA  != 0)
  SetDecRegister(regs, HWIF_DEC_TIMEOUT_E, 1);
#else
  SetDecRegister(regs, HWIF_DEC_TIMEOUT_E, 0);
#endif

#if( DEC_X170_INTERNAL_CLOCK_GATING != 0)
  SetDecRegister(regs, HWIF_DEC_CLK_GATE_E, 1);
#else
  SetDecRegister(regs, HWIF_DEC_CLK_GATE_E, 0);
#endif

#if( DEC_X170_USING_IRQ  == 0)
  SetDecRegister(regs, HWIF_DEC_IRQ_DIS, 1);
#else
  SetDecRegister(regs, HWIF_DEC_IRQ_DIS, 0);
#endif

  SetDecRegister(regs, HWIF_SERV_MERGE_DIS,
                 DEC_X170_SERVICE_MERGE_DISABLE);

  /* set AXI RW IDs */
  SetDecRegister(regs, HWIF_DEC_AXI_RD_ID,
                 (DEC_X170_AXI_ID_R & 0xFFU));
  SetDecRegister(regs, HWIF_DEC_AXI_WR_ID,
                 (DEC_X170_AXI_ID_W & 0xFFU));

}

