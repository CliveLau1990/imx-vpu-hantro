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

#include "mp4decapi.h"
#include "mp4dechwd_container.h"
#include "mp4dechwd_vop.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.1  Function name: MP4GetResyncLength

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/

u32 MP4GetResyncLength(MP4DecInst dec_inst, u8 *p_strm) {

  u32 i, tmp;
  i32 itmp;
  u32 modulo_time_base, vop_time_increment;
  DecContainer *dec_container = (DecContainer*)dec_inst;
  DecContainer copy_container = *dec_container;

  copy_container.StrmDesc.strm_curr_pos =
    copy_container.StrmDesc.p_strm_buff_start = p_strm;
  copy_container.StrmDesc.strm_buff_read_bits =
    copy_container.StrmDesc.bit_pos_in_word = 0;
  copy_container.StrmDesc.strm_buff_size = 1024;

  /* check that memories allocated already */
  if(dec_container->StrmStorage.data[0].bus_address != 0)
    if (!StrmDec_DecodeVopHeader(&copy_container))
      if (copy_container.VopDesc.vop_coding_type < 2 ||
          copy_container.VopDesc.fcode_fwd >
          copy_container.VopDesc.fcode_bwd)
        return(16+copy_container.VopDesc.fcode_fwd);
      else
        return(16+copy_container.VopDesc.fcode_bwd);
    else
      return 17;
  return 0;

}
