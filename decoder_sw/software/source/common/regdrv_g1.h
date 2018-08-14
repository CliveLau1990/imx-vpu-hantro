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

#ifndef REGDRV_H
#define REGDRV_H

/*------------------------------------------------------------------------------
    Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

#define DEC_8170_IRQ_RDY            0x02
#define DEC_8170_IRQ_BUS            0x04
#define DEC_8170_IRQ_BUFFER         0x08
#define DEC_8170_IRQ_ASO            0x10
#define DEC_8170_IRQ_ERROR          0x20
#define DEC_8170_IRQ_SLICE          0x40
#define DEC_8170_IRQ_TIMEOUT        0x80

#define DEC_8190_IRQ_ABORT          0x01
#define DEC_8190_IRQ_RDY            DEC_8170_IRQ_RDY
#define DEC_8190_IRQ_BUS            DEC_8170_IRQ_BUS
#define DEC_8190_IRQ_BUFFER         DEC_8170_IRQ_BUFFER
#define DEC_8190_IRQ_ASO            DEC_8170_IRQ_ASO
#define DEC_8190_IRQ_ERROR          DEC_8170_IRQ_ERROR
#define DEC_8190_IRQ_SLICE          DEC_8170_IRQ_SLICE
#define DEC_8190_IRQ_TIMEOUT        DEC_8170_IRQ_TIMEOUT

#define DEC_IRQ_DISABLE             0x10
#define DEC_ABORT                   0x20

#define PP_REG_START                60

#ifdef USE_64BIT_ENV

#define SET_ADDR_REG(reg_base, REGBASE, addr) do {\
    SetDecRegister((reg_base), REGBASE, (u32)(addr));  \
    SetDecRegister((reg_base), REGBASE##_MSB, (u32)((addr) >> 32)); \
  } while (0)

#define SET_ADDR_REG2(reg_base, lsb, msb, addr) do {\
    SetDecRegister((reg_base), (lsb), (u32)(addr));  \
    SetDecRegister((reg_base), (msb), (u32)((addr) >> 32)); \
  } while (0)

#define SET_PP_ADDR_REG(reg_base, REGBASE, addr) do {\
    SetPpRegister((reg_base), REGBASE, (u32)(addr));  \
    SetPpRegister((reg_base), REGBASE##_MSB, (u32)((addr) >> 32)); \
  } while (0)

#define SET_PP_ADDR_REG2(reg_base, lsb, msb, addr) do {\
    SetPpRegister((reg_base), (lsb), (u32)(addr));  \
    SetPpRegister((reg_base), (msb), (u32)((addr) >> 32)); \
  } while (0)

#define GET_ADDR_REG(reg_base, REGBASE)  \
  (((addr_t)GetDecRegister((reg_base), REGBASE)) |  \
  (((addr_t)GetDecRegister((reg_base), REGBASE##_MSB)) << 32))

#define GET_ADDR_REG2(reg_base, lsb, msb)  \
  (((addr_t)GetDecRegister((reg_base), (lsb))) |  \
  (((addr_t)GetDecRegister((reg_base), (msb))) << 32))

#define GET_PP_ADDR_REG(reg_base, REGBASE)  \
  (((addr_t)GetPpRegister((reg_base), REGBASE)) |  \
  (((addr_t)GetPpRegister((reg_base), REGBASE##_MSB)) << 32))

#define GET_PP_ADDR_REG2(reg_base, lsb, msb)  \
  (((addr_t)GetPpRegister((reg_base), (lsb))) |  \
  (((addr_t)GetPpRegister((reg_base), (msb))) << 32))

#else

#define SET_ADDR_REG(reg_base, REGBASE, addr) do {\
    SetDecRegister((reg_base), REGBASE, (u32)(addr));  \
  } while (0)

#define SET_ADDR_REG2(reg_base, lsb, msb, addr) do {\
    SetDecRegister((reg_base), (lsb), (u32)(addr));  \
    SetDecRegister((reg_base), (msb), 0); \
  } while (0)

#define SET_PP_ADDR_REG(reg_base, REGBASE, addr) do {\
    SetPpRegister((reg_base), REGBASE, (u32)(addr));  \
  } while (0)

#define SET_PP_ADDR_REG2(reg_base, lsb, msb, addr) do {\
    SetPpRegister((reg_base), (lsb), (u32)(addr));  \
    SetPpRegister((reg_base), (msb), 0); \
  } while (0)

#define GET_ADDR_REG(reg_base, REGID)  \
  ((addr_t)GetDecRegister((reg_base), REGID))

#define GET_ADDR_REG2(reg_base, lsb, msb)  \
  (((addr_t)GetDecRegister((reg_base), (lsb))) |  \
  (((addr_t)GetDecRegister((reg_base), (msb))) & 0))

#define GET_PP_ADDR_REG(reg_base, REGID)  \
  ((addr_t)GetDecRegister((reg_base), REGID))

#define GET_PP_ADDR_REG2(reg_base, lsb, msb)  \
  ((addr_t)GetPpRegister((reg_base), (lsb)))

#endif

#ifdef USE_64BIT_ENV
#define SET_ADDR64_REG(reg_base, REGBASE, addr) do {\
    SetDecRegister((reg_base), REGBASE##_LSB, (u32)(addr));  \
    SetDecRegister((reg_base), REGBASE##_MSB, (u32)((addr) >> 32)); \
  } while (0)
#define GET_ADDR64_REG(reg_base, REGBASE)  \
  (((addr_t)GetDecRegister((reg_base), REGBASE##_LSB)) |  \
  (((addr_t)GetDecRegister((reg_base), REGBASE##_MSB)) << 32))
#else
#define SET_ADDR64_REG(reg_base, REGBASE, addr) do {\
    SetDecRegister((reg_base), REGBASE##_LSB, (u32)(addr));  \
    SetDecRegister((reg_base), REGBASE##_MSB, 0); \
  } while (0)
#define GET_ADDR64_REG(reg_base, REGBASE)  \
  (((addr_t)GetDecRegister((reg_base), REGBASE##_LSB)) |  \
  (((addr_t)GetDecRegister((reg_base), REGBASE##_MSB)) << 32))
#endif

typedef enum {
  /* include script-generated part */
#include "8170enum_g1.h"
  HWIF_DEC_IRQ_STAT,
  HWIF_PP_IRQ_STAT,
  HWIF_LAST_REG,

  /* aliases */
  HWIF_MPEG4_DC_BASE = HWIF_I4X4_OR_DC_BASE,
  HWIF_MPEG4_DC_BASE_MSB = HWIF_I4X4_OR_DC_BASE_MSB,
  HWIF_INTRA_4X4_BASE = HWIF_I4X4_OR_DC_BASE,
  HWIF_INTRA_4X4_BASE_MSB = HWIF_I4X4_OR_DC_BASE_MSB,
  /* VP6 */
  HWIF_VP6HWGOLDEN_BASE = HWIF_REFER4_BASE,
  HWIF_VP6HWGOLDEN_BASE_MSB = HWIF_REFER4_BASE_MSB,
  HWIF_VP6HWPART1_BASE = HWIF_REFER13_BASE,
  HWIF_VP6HWPART1_BASE_MSB = HWIF_REFER13_BASE_MSB,
  HWIF_VP6HWPART2_BASE = HWIF_RLC_VLC_BASE,
  HWIF_VP6HWPART2_BASE_MSB = HWIF_RLC_VLC_BASE_MSB,
  HWIF_VP6HWPROBTBL_BASE = HWIF_QTABLE_BASE,
  HWIF_VP6HWPROBTBL_BASE_MSB = HWIF_QTABLE_BASE_MSB,
  /* progressive JPEG */
  HWIF_PJPEG_COEFF_BUF = HWIF_DIR_MV_BASE,
  HWIF_PJPEG_COEFF_BUF_MSB = HWIF_DIR_MV_BASE_MSB,

  /* MVC */
  HWIF_INTER_VIEW_BASE = HWIF_REFER15_BASE,
  HWIF_INTER_VIEW_BASE_MSB = HWIF_REFER15_BASE_MSB

} hwIfName_e;

/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

void SetDecRegister(u32 *reg_base, u32 id, u32 value);
u32 GetDecRegister(const u32 *reg_base, u32 id);

void SetPpRegister(u32 *pp_reg_base, u32 id, u32 value);
u32 GetPpRegister(const u32 *pp_reg_base, u32 id);

#endif /* #ifndef REGDRV_H */
