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

#include "h264hwd_container.h"
#include "h264hwd_util.h"
#include "h264hwd_macroblock_layer.h"
#include "h264hwd_neighbour.h"
#include "h264hwd_exports.h"

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 MvPredictionSkip(mbStorage_t * p_mb, dpbStorage_t * dpb);

static u32 MvPrediction16x16(mbStorage_t * p_mb, dpbStorage_t * dpb);
static u32 MvPrediction16x8(mbStorage_t * p_mb, dpbStorage_t * dpb);
static u32 MvPrediction8x16(mbStorage_t * p_mb, dpbStorage_t * dpb);
static u32 MvPrediction8x8(mbStorage_t * p_mb, dpbStorage_t * dpb);

static u32 GetInterNeighbour(u32 slice_id, /*@null@ */ mbStorage_t * n_mb);

/*------------------------------------------------------------------------------
    Function name   : PrepareInterPrediction
    Description     : Processes one inter macroblock. Writes MB control data
    Return type     : u32 - 0 for success or a negative error code
    Argument        : mbStorage_t * p_mb - pointer to macroblock specific information
    Argument        : macroblockLayer_t * p_mb_layer - macroblock layer data
    Argument        : dpbStorage_t * dpb - DPB data
    Argument        : DecAsicBuffers_t * p_asic_buff - SW/HW interface
------------------------------------------------------------------------------*/
u32 PrepareInterPrediction(mbStorage_t * p_mb, macroblockLayer_t * p_mb_layer,
                           dpbStorage_t * dpb, DecAsicBuffers_t * p_asic_buff) {

  ASSERT(p_mb);
  ASSERT(h264bsdMbPartPredMode(p_mb->mbType) == PRED_MODE_INTER);
  ASSERT(p_mb_layer);

  /* if decoded flag > 1 -> mb has already been successfully decoded and
   * written to output -> do not write again */
  if(p_mb->decoded > 1)
    goto end;

  switch (p_mb->mbType) {
  case P_Skip:
    if(MvPredictionSkip(p_mb, dpb) != HANTRO_OK)
      return (HANTRO_NOK);
    break;

  case P_L0_16x16:
    if(MvPrediction16x16(p_mb, dpb) != HANTRO_OK)
      return (HANTRO_NOK);

    break;

  case P_L0_L0_16x8:
    if(MvPrediction16x8(p_mb, dpb) != HANTRO_OK)
      return (HANTRO_NOK);

    break;

  case P_L0_L0_8x16:
    if(MvPrediction8x16(p_mb, dpb) != HANTRO_OK)
      return (HANTRO_NOK);
    break;

  default:   /* P_8x8 and P_8x8ref0 */
    if(MvPrediction8x8(p_mb, dpb) != HANTRO_OK)
      return (HANTRO_NOK);
    break;
  }

  /* update ASIC MB control field */
  {
    u32 tmp;
    u32 *p_asic_ctrl =
      p_asic_buff->mb_ctrl.virtual_address +
      (p_asic_buff->current_mb * (ASIC_MB_CTRL_BUFFER_SIZE / 4));

    switch (p_mb->mbType) {
    case P_Skip:
      tmp = (u32) HW_P_SKIP << 29;
      break;

    case P_L0_16x16:
      tmp = (u32) HW_P_16x16 << 29;
      break;

    case P_L0_L0_16x8:
      tmp = (u32) HW_P_16x8 << 29;
      break;

    case P_L0_L0_8x16:
      tmp = (u32) HW_P_8x16 << 29;
      break;

    default:   /* P_8x8 and P_8x8ref0 */
      tmp = (u32) HW_P_8x8 << 29;
      tmp |= p_mb_layer->subMbPred.subMbType[0] << 27;
      tmp |= p_mb_layer->subMbPred.subMbType[1] << 25;
      tmp |= p_mb_layer->subMbPred.subMbType[2] << 23;
      tmp |= p_mb_layer->subMbPred.subMbType[3] << 21;

      break;
    }

    tmp |= p_mb->qp_y << 15;
    tmp |= (u32) (p_mb_layer->filter_offset_a & 0x0F) << 11;
    tmp |= (u32) (p_mb_layer->filter_offset_b & 0x0F) << 7;

    tmp |= p_asic_buff->not_coded_mask;

    p_asic_ctrl[0] = tmp;

    {
      tmp = GetInterNeighbour(p_mb->slice_id, p_mb->mb_d) << 31;
      tmp |= GetInterNeighbour(p_mb->slice_id, p_mb->mb_b) << 30;
      tmp |= GetInterNeighbour(p_mb->slice_id, p_mb->mb_c) << 29;
      tmp |= GetInterNeighbour(p_mb->slice_id, p_mb->mb_a) << 28;
      tmp |= p_asic_buff->rlc_words << 19;
    }

    tmp |= p_mb_layer->disable_deblocking_filter_idc << 17;

    p_asic_ctrl[1] = tmp;
  }

end:
  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function: MvPrediction16x16

        Functional description:
            Motion vector prediction for 16x16 partition mode

------------------------------------------------------------------------------*/

u32 MvPrediction16x16(mbStorage_t * p_mb, dpbStorage_t * dpb) {
  u32 ref_index;
  i32 tmp;

  ref_index = p_mb->ref_idx_l0[0];

  tmp = h264bsdGetRefPicData(dpb, ref_index);
  if(tmp == -1)
    return (HANTRO_NOK);

  p_mb->ref_id[0] = (u8) tmp;

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function: MvPredictionSkip

        Functional description:
            Motion vector prediction skipped macroblock

------------------------------------------------------------------------------*/

u32 MvPredictionSkip(mbStorage_t * p_mb, dpbStorage_t * dpb) {
  u32 ref_index = 0;
  i32 tmp;

  tmp = h264bsdGetRefPicData(dpb, ref_index);
  if(tmp == -1)
    return (HANTRO_NOK);

  p_mb->ref_id[0] = (u8) tmp;

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function: MvPrediction16x8

        Functional description:
            Motion vector prediction for 16x8 partition mode

------------------------------------------------------------------------------*/

u32 MvPrediction16x8(mbStorage_t * p_mb, dpbStorage_t * dpb) {
  u32 ref_index;
  i32 tmp;

  /* first partition */
  ref_index = p_mb->ref_idx_l0[0];
  tmp = h264bsdGetRefPicData(dpb, ref_index);
  if(tmp == -1)
    return (HANTRO_NOK);

  p_mb->ref_id[0] = (u8) tmp;

  /* second partition */
  ref_index = p_mb->ref_idx_l0[1];

  tmp = h264bsdGetRefPicData(dpb, ref_index);
  if(tmp == -1)
    return (HANTRO_NOK);

  p_mb->ref_id[1] = (u8) tmp;

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: MvPrediction8x16

        Functional description:
            Motion vector prediction for 8x16 partition mode

------------------------------------------------------------------------------*/

u32 MvPrediction8x16(mbStorage_t * p_mb, dpbStorage_t * dpb) {
  u32 ref_index;
  i32 tmp;

  /* first partition */
  ref_index = p_mb->ref_idx_l0[0];

  tmp = h264bsdGetRefPicData(dpb, ref_index);
  if(tmp == -1)
    return (HANTRO_NOK);

  p_mb->ref_id[0] = (u8) tmp;

  /* second partition */
  ref_index = p_mb->ref_idx_l0[1];

  tmp = h264bsdGetRefPicData(dpb, ref_index);
  if(tmp == -1)
    return (HANTRO_NOK);

  p_mb->ref_id[1] = (u8) tmp;

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function: MvPrediction8x8

        Functional description:
            Motion vector prediction for 8x8 partition mode

------------------------------------------------------------------------------*/
u32 MvPrediction8x8(mbStorage_t * p_mb, dpbStorage_t * dpb) {
  u32 i;
  const u8 *ref_idx_l0 = p_mb->ref_idx_l0;
  u8 *ref_id = p_mb->ref_id;

  for(i = 4; i > 0; i--) {
    u32 ref_index;
    i32 tmp;

    ref_index = *ref_idx_l0++;
    tmp = h264bsdGetRefPicData(dpb, ref_index);
    if(tmp == -1)
      return (HANTRO_NOK);

    *ref_id++ = (u8) tmp;

  }

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function: GetInterNeighbour

        Functional description:
            Checks if macroblock 'n_mb' is part of slice 'slice_id'

------------------------------------------------------------------------------*/

u32 GetInterNeighbour(u32 slice_id, mbStorage_t * n_mb) {
  if(n_mb && (slice_id == n_mb->slice_id)) {
    return 1;
  } else {
    return 0;
  }
}
