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

#include "regdrv_g1.h"

/*------------------------------------------------------------------------------
    External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

#ifdef _ASSERT_USED
#ifndef ASSERT
#include <assert.h>
#define ASSERT(expr) assert(expr)
#endif
#else
#define ASSERT(expr)
#endif

static const u32 reg_mask[33] = { 0x00000000,
                                  0x00000001, 0x00000003, 0x00000007, 0x0000000F,
                                  0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
                                  0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
                                  0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
                                  0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
                                  0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
                                  0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
                                  0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
                                };

/* { SWREG, BITS, POSITION } */
static const u32 hw_dec_reg_spec[HWIF_LAST_REG + 1][3] = {
  /* include script-generated part */
#include "8170table_g1.h"
  /* HWIF_DEC_IRQ_STAT */ {1, 8, 11},
  /* HWIF_PP_IRQ_STAT */ {60, 2, 12},
  /* dummy entry */ {0, 0, 0}
};

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function name: SetDecRegister

        Functional description:

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/
void SetDecRegister(u32 *reg_base, u32 id, u32 value) {
  u32 tmp;

  ASSERT(id < HWIF_LAST_REG);

  tmp = reg_base[hw_dec_reg_spec[id][0]];
  tmp &= ~(reg_mask[hw_dec_reg_spec[id][1]] << hw_dec_reg_spec[id][2]);
  tmp |= (value & reg_mask[hw_dec_reg_spec[id][1]]) << hw_dec_reg_spec[id][2];
  reg_base[hw_dec_reg_spec[id][0]] = tmp;
}

void SetPpRegister(u32 *pp_reg_base, u32 id, u32 value) {
  u32 tmp;

  ASSERT(id < HWIF_LAST_REG);

  /* hw reg spec is based on decoder so we have to adjust the register offset
   * because we are getting PP base register as parameter
   */

  tmp = pp_reg_base[hw_dec_reg_spec[id][0] - PP_REG_START];
  tmp &= ~(reg_mask[hw_dec_reg_spec[id][1]] << hw_dec_reg_spec[id][2]);
  tmp |= (value & reg_mask[hw_dec_reg_spec[id][1]]) << hw_dec_reg_spec[id][2];
  pp_reg_base[hw_dec_reg_spec[id][0] - PP_REG_START] = tmp;
}
/*------------------------------------------------------------------------------

    Function name: GetDecRegister

        Functional description:

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/
u32 GetDecRegister(const u32 *reg_base, u32 id) {
  u32 tmp;

  ASSERT(id < HWIF_LAST_REG);

  tmp = reg_base[hw_dec_reg_spec[id][0]];
  tmp = tmp >> hw_dec_reg_spec[id][2];
  tmp &= reg_mask[hw_dec_reg_spec[id][1]];
  return (tmp);
}

u32 GetPpRegister(const u32 *pp_reg_base, u32 id) {
  u32 tmp;

  ASSERT(id < HWIF_LAST_REG);

  /* hw reg spec is based on decoder so we have to adjust the register offset
   * because we are getting PP base register as parameter
   */

  tmp = pp_reg_base[hw_dec_reg_spec[id][0] - PP_REG_START];
  tmp = tmp >> hw_dec_reg_spec[id][2];
  tmp &= reg_mask[hw_dec_reg_spec[id][1]];
  return (tmp);
}
