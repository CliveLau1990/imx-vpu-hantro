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

#include "basetype.h"
#include "vp8hwd_headers.h"
#include "vp8hwd_probs.h"
#include "vp8hwd_container.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------
    ResetScan

        Reset decoder to initial state
------------------------------------------------------------------------------*/
void vp8hwdPrepareVp7Scan( vp8_decoder_t * dec, u32 * new_order ) {
  u32 i;
  static const u32 Vp7DefaultScan[] = {
    0,  1,  4,  8,  5,  2,  3,  6,
    9, 12, 13, 10,  7, 11, 14, 15,
  };

  if(!new_order) {
    for( i = 0 ; i < 16 ; ++i )
      dec->vp7_scan_order[i] = Vp7DefaultScan[i];
  } else {
    for( i = 0 ; i < 16 ; ++i )
      dec->vp7_scan_order[i] = Vp7DefaultScan[new_order[i]];
  }
}

/*------------------------------------------------------------------------------
    vp8hwdResetDecoder

        Reset decoder to initial state
------------------------------------------------------------------------------*/
void vp8hwdResetDecoder( vp8_decoder_t * dec) {
  vp8hwdResetProbs( dec );
  vp8hwdPrepareVp7Scan( dec, NULL );
}

/*------------------------------------------------------------------------------
    vp8hwdResetDecoder

        Reset decoder to initial state
------------------------------------------------------------------------------*/
void vp8hwdResetSegMap( vp8_decoder_t * dec, DecAsicBuffers_t *asic_buff, u32 core_id) {
  UNUSED(dec);
  if (asic_buff->segment_map[core_id].virtual_address) {
    DWLmemset(asic_buff->segment_map[core_id].virtual_address,
              0, asic_buff->segment_map_size);
  }
}
