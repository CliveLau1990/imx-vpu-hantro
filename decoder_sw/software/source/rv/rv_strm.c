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

#include "rv_strm.h"
#include "rv_utils.h"
#include "rv_headers.h"
#include "rv_debug.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum {
  CONTINUE
};

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.1  Function name: rvStrmDec_Decode

        Purpose: Decode RV stream

        Input:

        Output:

------------------------------------------------------------------------------*/

u32 rv_StrmDecode(DecContainer * dec_container) {

  u32 type;
  u32 status = HANTRO_OK;

  RVDEC_DEBUG(("Entry rv_StrmDecode\n"));

  /* keep decoding till something ready or something wrong */
  do {
    /* TODO: Where do we get type, what's going to be decoded, etc. */
    type = RV_SLICE;

    /* parse headers */
    switch (type) {
    case RV_SLICE:
      status = rv_DecodeSliceHeader(dec_container);
      if (!dec_container->StrmStorage.strm_dec_ready) {
        if (status == HANTRO_OK &&
            dec_container->Hdrs.horizontal_size &&
            dec_container->Hdrs.vertical_size &&
            dec_container->FrameDesc.pic_coding_type == RV_I_PIC) {
          dec_container->StrmStorage.strm_dec_ready = HANTRO_TRUE;
          return DEC_HDRS_RDY;
        }
        /* else try next slice or what */
      } else if( dec_container->StrmStorage.rpr_detected) {
        /*return DEC_HDRS_RDY;*/
        return DEC_PIC_HDR_RDY_RPR;
      } else if (status == HANTRO_OK)
        return DEC_PIC_HDR_RDY;
      else
        return DEC_PIC_HDR_RDY_ERROR;

      break;

    default:
      break;
    }

  } while(0);

  return (DEC_RDY);

}
