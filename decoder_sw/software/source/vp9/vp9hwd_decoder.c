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

#include "basetype.h"
#include "vp9hwd_headers.h"
#include "vp9hwd_probs.h"
#include "vp9hwd_container.h"

void Vp9ResetDecoder(struct Vp9Decoder *dec, struct DecAsicBuffers *asic_buff) {
  i32 i;

  /* Clear all previous segment data */
  DWLmemset(dec->segment_feature_enable, 0,
            sizeof(dec->segment_feature_enable));
  DWLmemset(dec->segment_feature_data, 0, sizeof(dec->segment_feature_data));

#ifndef USE_EXTERNAL_BUFFER
  if (asic_buff->segment_map[0].virtual_address)
    DWLmemset(asic_buff->segment_map[0].virtual_address, 0,
              asic_buff->segment_map_size);
  if (asic_buff->segment_map[1].virtual_address)
    DWLmemset(asic_buff->segment_map[1].virtual_address, 0,
              asic_buff->segment_map_size);
#else
  if (asic_buff->segment_map.virtual_address)
    DWLmemset(asic_buff->segment_map.virtual_address, 0,
              asic_buff->segment_map.logical_size);
#endif
  Vp9ResetProbs(dec);

  dec->frame_context_idx = 0;
  dec->ref_frame_sign_bias[0] =
    dec->ref_frame_sign_bias[1] =
      dec->ref_frame_sign_bias[2] =
        dec->ref_frame_sign_bias[3] = 0;
  dec->allow_comp_inter_inter = 0;
  for (i = 0; i < NUM_REF_FRAMES; i++) {
    dec->ref_frame_map[i] = i;
  }
}
