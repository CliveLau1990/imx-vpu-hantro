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

#ifndef REGDRV_H
#define REGDRV_H

#include "basetype.h"

#define DEC_HW_ALIGN_MASK 0x0F

#define DEC_HW_IRQ_RDY 0x01
#define DEC_HW_IRQ_BUS 0x02
#define DEC_HW_IRQ_BUFFER 0x04
#define DEC_HW_IRQ_ASO 0x08
#define DEC_HW_IRQ_ERROR 0x10
#define DEC_HW_IRQ_TIMEOUT 0x40

enum HwIfName {
  /* include script-generated part */
#include "8170enum.h"
  HWIF_DEC_IRQ_STAT,
  HWIF_PP_IRQ_STAT,
  HWIF_LAST_REG,
};

/* { SWREG, BITS, POSITION, WRITABLE } */
static const u32 hw_dec_reg_spec[HWIF_LAST_REG + 1][4] = {
  /* include script-generated part */
#include "8170table.h"
  /* HWIF_DEC_IRQ_STAT */ {1, 7, 12, 0},
  /* HWIF_PP_IRQ_STAT */ {60, 2, 12, 0},
  /* dummy entry */ {0, 0, 0, 0}
};



/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

void SetDecRegister(u32* reg_base, u32 id, u32 value);
u32 GetDecRegister(const u32* reg_base, u32 id);
void FlushDecRegisters(const void* dwl, i32 core_id, u32* regs);
void RefreshDecRegisters(const void* dwl, i32 core_id, u32* regs);

#ifdef USE_64BIT_ENV
#define SET_ADDR_REG(reg_base, REGBASE, addr) do {\
    SetDecRegister((reg_base), REGBASE##_LSB, (u32)(addr));  \
    SetDecRegister((reg_base), REGBASE##_MSB, (u32)((addr) >> 32)); \
  } while (0)

#define SET_ADDR_REG2(reg_base, lsb, msb, addr) do {\
    SetDecRegister((reg_base), (lsb), (u32)(addr));  \
    SetDecRegister((reg_base), (msb), (u32)((addr) >> 32)); \
  } while (0)
#else
#define SET_ADDR_REG(reg_base, REGBASE, addr) do {\
    SetDecRegister((reg_base), REGBASE##_LSB, (u32)(addr));  \
  } while (0)
#define SET_ADDR_REG2(reg_base, lsb, msb, addr) do {\
    SetDecRegister((reg_base), (lsb), (u32)(addr));  \
    SetDecRegister((reg_base), (msb), 0); \
  } while (0)
#endif

#endif /* #ifndef REGDRV_H */
