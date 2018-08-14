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

#include "hevc_nal_unit.h"
#include "hevc_util.h"

/* Decodes header of one NAL unit. */
u32 HevcDecodeNalUnit(struct StrmData *stream, struct NalUnit *nal_unit) {

  u32 tmp;

  ASSERT(stream);
  ASSERT(nal_unit);
  ASSERT(stream->bit_pos_in_word == 0);

  (void)DWLmemset(nal_unit, 0, sizeof(struct NalUnit));

  /* forbidden_zero_bit (not checked to be zero, errors ignored) */
  tmp = SwGetBits(stream, 1);
  if (tmp == END_OF_STREAM)
    return HANTRO_NOK;

  tmp = SwGetBits(stream, 6);
  if (tmp == END_OF_STREAM)
    return HANTRO_NOK;

  nal_unit->nal_unit_type = (enum NalUnitType)tmp;

  DEBUG_PRINT(("NAL TYPE %d\n", tmp));

  /* reserved_zero_6bits */
  tmp = SwGetBits(stream, 6);
  if (tmp == END_OF_STREAM)
    return HANTRO_NOK;

  tmp = SwGetBits(stream, 3);
  if (tmp == END_OF_STREAM)
    return HANTRO_NOK;

  nal_unit->temporal_id = tmp ? tmp - 1 : 0;

  return (HANTRO_OK);
}
