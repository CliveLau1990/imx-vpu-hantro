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

#include "tiledref.h"
#include "regdrv_g1.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*#define TILED_NOT_SUPPORTED             (0)*/
#define TILED_SUPPORT_PROGRESSIVE_8x4   (1)
#define TILED_SUPPORT_INTERLACED_8x4    (2)

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    DecCheckTiledMode
        Check if suggested tile-mode / DPB combination is valid.

        Returns OK if valid, otherwise NOK.
------------------------------------------------------------------------------*/
u32 DecCheckTiledMode( u32 tiled_mode_support, enum DecDpbMode dpb_mode,
                       u32 interlaced_stream ) {

  return 0;

  if(interlaced_stream) {
    if( (tiled_mode_support != TILED_SUPPORT_INTERLACED_8x4) ||
        (dpb_mode != DEC_DPB_INTERLACED_FIELD ))
      return 1;
  } else { /* progressive */
    if( dpb_mode != DEC_DPB_FRAME )
      return 1;
  }

  return 0;
}

/*------------------------------------------------------------------------------
    DecSetupTiledReference
        Enable/disable tiled reference mode on HW. See inside function for
        disable criterias.

        Returns tile mode.
------------------------------------------------------------------------------*/
u32 DecSetupTiledReference( u32 *reg_base, u32 tiled_mode_support,
                            enum DecDpbMode dpb_mode, u32 interlaced_stream ) {
  u32 tiled_allowed = 1;
  u32 mode = TILED_REF_NONE;

  if(!tiled_mode_support) {
    SetDecRegister(reg_base, HWIF_TILED_MODE_MSB, 0 );
    SetDecRegister(reg_base, HWIF_TILED_MODE_LSB, 0 );
    return TILED_REF_NONE;
  }

  /* disallow for interlaced streams if no support*/
  if(interlaced_stream &&
      (dpb_mode != DEC_DPB_INTERLACED_FIELD) )
    tiled_allowed = 0;

  /* if tiled mode allowed, pick a tile mode that suits us best (pretty easy
   * as we currently only support 8x4 */
  if(tiled_allowed) {
    mode = TILED_REF_8x4;
  }

  /* Setup HW registers */
  SetDecRegister(reg_base, HWIF_TILED_MODE_MSB, (mode >> 1) & 0x1 );
  SetDecRegister(reg_base, HWIF_TILED_MODE_LSB, mode & 0x1 );

  return mode;
}


