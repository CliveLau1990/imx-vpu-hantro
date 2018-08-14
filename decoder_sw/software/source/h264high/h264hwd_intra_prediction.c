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

static u32 Intra16x16Prediction(mbStorage_t * p_mb,
                                macroblockLayer_t * mb_layer,
                                u32 constrained_intra_pred,
                                DecAsicBuffers_t * p_asic_buff);

static u32 Intra4x4Prediction(mbStorage_t * p_mb, macroblockLayer_t * mb_layer,
                              u32 constrained_intra_pred,
                              DecAsicBuffers_t * p_asic_buff);

static u32 DetermineIntra4x4PredMode(macroblockLayer_t * p_mb_layer,
                                     u32 available, neighbour_t * n_a,
                                     neighbour_t * n_b, u32 index,
                                     /*@null@ */ mbStorage_t * n_mb_a,
                                     /*@null@ */ mbStorage_t * n_mb_b);

static u32 GetIntraNeighbour(u32 slice_id, mbStorage_t * n_mb);

static u32 CheckIntraChromaPrediction(u32 pred_mode, u32 available_a,
                                      u32 available_b, u32 available_d);

/*------------------------------------------------------------------------------
    Function name   : PrepareIntraPrediction
    Description     :
    Return type     : u32
    Argument        : mbStorage_t * p_mb
    Argument        : macroblockLayer_t * mb_layer
    Argument        : u32 constrained_intra_pred
    Argument        : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
u32 PrepareIntraPrediction(mbStorage_t * p_mb, macroblockLayer_t * mb_layer,
                           u32 constrained_intra_pred,
                           DecAsicBuffers_t * p_asic_buff) {
  u32 tmp;

  if(h264bsdMbPartPredMode(p_mb->mbType) == PRED_MODE_INTRA4x4) {
    tmp = Intra4x4Prediction(p_mb, mb_layer, constrained_intra_pred, p_asic_buff);
    if(tmp != HANTRO_OK)
      return (tmp);
  } else {
    tmp =
      Intra16x16Prediction(p_mb, mb_layer, constrained_intra_pred, p_asic_buff);
    if(tmp != HANTRO_OK)
      return (tmp);
  }

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------
    Function name   : Intra16x16Prediction
    Description     :
    Return type     : u32
    Argument        : mbStorage_t * p_mb
    Argument        : macroblockLayer_t * mb_layer
    Argument        : u32 constrained_intra_pred
    Argument        : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
u32 Intra16x16Prediction(mbStorage_t * p_mb, macroblockLayer_t * mb_layer,
                         u32 constrained_intra_pred,
                         DecAsicBuffers_t * p_asic_buff) {
  u32 mode, tmp;
  u32 available_a, available_b, available_d;

  ASSERT(h264bsdPredModeIntra16x16(p_mb->mbType) < 4);

  available_a = h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_a);
  if(available_a && constrained_intra_pred &&
      (h264bsdMbPartPredMode(p_mb->mb_a->mbType) == PRED_MODE_INTER))
    available_a = HANTRO_FALSE;
  available_b = h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_b);
  if(available_b && constrained_intra_pred &&
      (h264bsdMbPartPredMode(p_mb->mb_b->mbType) == PRED_MODE_INTER))
    available_b = HANTRO_FALSE;
  available_d = h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_d);
  if(available_d && constrained_intra_pred &&
      (h264bsdMbPartPredMode(p_mb->mb_d->mbType) == PRED_MODE_INTER))
    available_d = HANTRO_FALSE;

  mode = h264bsdPredModeIntra16x16(p_mb->mbType);

  switch (mode) {
  case 0:    /* Intra_16x16_Vertical */
    if(!available_b)
      return (HANTRO_NOK);
    break;

  case 1:    /* Intra_16x16_Horizontal */
    if(!available_a)
      return (HANTRO_NOK);
    break;

  case 2:    /* Intra_16x16_DC */
    break;

  default:   /* case 3: Intra_16x16_Plane */
    if(!available_a || !available_b || !available_d)
      return (HANTRO_NOK);
    break;
  }

  tmp = CheckIntraChromaPrediction(mb_layer->mbPred.intra_chroma_pred_mode,
                                   available_a, available_b, available_d);
  if(tmp != HANTRO_OK)
    return (tmp);

  if(p_mb->decoded > 1) {
    goto end;
  }

  /* update ASIC MB control field */
  {
    u32 tmp2;
    u32 *p_asic_ctrl =
      p_asic_buff->mb_ctrl.virtual_address +
      (p_asic_buff->current_mb * (ASIC_MB_CTRL_BUFFER_SIZE / 4));

    tmp2 = (u32) HW_I_16x16 << 29;
    tmp2 |= mode << 27;

    tmp2 |= mb_layer->mbPred.intra_chroma_pred_mode << 25;
    tmp2 |= ((available_a == HANTRO_TRUE ? 1U : 0U) << 24);
    tmp2 |= ((available_b == HANTRO_TRUE ? 1U : 0U) << 23);

    tmp2 |= p_mb->qp_y << 15;
    tmp2 |= (u32) (mb_layer->filter_offset_a & 0x0F) << 11;
    tmp2 |= (u32) (mb_layer->filter_offset_b & 0x0F) << 7;

    tmp2 |= p_asic_buff->not_coded_mask;

    p_asic_ctrl[0] = tmp2;

    tmp2 = GetIntraNeighbour(p_mb->slice_id, p_mb->mb_d) << 31;
    tmp2 |= GetIntraNeighbour(p_mb->slice_id, p_mb->mb_b) << 30;
    tmp2 |= GetIntraNeighbour(p_mb->slice_id, p_mb->mb_c) << 29;
    tmp2 |= GetIntraNeighbour(p_mb->slice_id, p_mb->mb_a) << 28;

    tmp2 |= p_asic_buff->rlc_words << 19;
    tmp2 |= mb_layer->disable_deblocking_filter_idc << 17;

    p_asic_ctrl[1] = tmp2;
  }

end:
  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------
    Function name   : Intra4x4Prediction
    Description     :
    Return type     : u32
    Argument        : mbStorage_t * p_mb
    Argument        : macroblockLayer_t * mb_layer
    Argument        : u32 constrained_intra_pred
    Argument        : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
u32 Intra4x4Prediction(mbStorage_t * p_mb, macroblockLayer_t * mb_layer,
                       u32 constrained_intra_pred, DecAsicBuffers_t * p_asic_buff) {
  u32 block;
  u32 mode, tmp;
  neighbour_t neighbour, neighbour_b;
  mbStorage_t *n_mb, *n_mb2;

  u32 available_a, available_b, available_c, available_d;

  for(block = 0; block < 16; block++) {
    ASSERT(p_mb->intra4x4_pred_mode[block] < 9);

    neighbour = *h264bsdNeighbour4x4BlockA(block);
    n_mb = h264bsdGetNeighbourMb(p_mb, neighbour.mb);
    available_a = h264bsdIsNeighbourAvailable(p_mb, n_mb);
    if(available_a && constrained_intra_pred &&
        (h264bsdMbPartPredMode(n_mb->mbType) == PRED_MODE_INTER)) {
      available_a = HANTRO_FALSE;
    }

    neighbour_b = *h264bsdNeighbour4x4BlockB(block);
    n_mb2 = h264bsdGetNeighbourMb(p_mb, neighbour_b.mb);
    available_b = h264bsdIsNeighbourAvailable(p_mb, n_mb2);
    if(available_b && constrained_intra_pred &&
        (h264bsdMbPartPredMode(n_mb2->mbType) == PRED_MODE_INTER)) {
      available_b = HANTRO_FALSE;
    }

    mode = DetermineIntra4x4PredMode(mb_layer,
                                     (u32) (available_a && available_b),
                                     &neighbour, &neighbour_b, block, n_mb,
                                     n_mb2);
    p_mb->intra4x4_pred_mode[block] = (u8) mode;

    if(p_mb->decoded == 1) {
      p_mb->intra4x4_pred_mode_asic[block] = (u8) mode;
    }

    neighbour = *h264bsdNeighbour4x4BlockD(block);
    n_mb = h264bsdGetNeighbourMb(p_mb, neighbour.mb);
    available_d = h264bsdIsNeighbourAvailable(p_mb, n_mb);
    if(available_d && constrained_intra_pred &&
        (h264bsdMbPartPredMode(n_mb->mbType) == PRED_MODE_INTER)) {
      available_d = HANTRO_FALSE;
    }

    switch (mode) {
    case 0:    /* Intra_4x4_Vertical */
      if(!available_b)
        return (HANTRO_NOK);
      break;
    case 1:    /* Intra_4x4_Horizontal */
      if(!available_a)
        return (HANTRO_NOK);
      break;
    case 2:    /* Intra_4x4_DC */
      break;
    case 3:    /* Intra_4x4_Diagonal_Down_Left */
      if(!available_b)
        return (HANTRO_NOK);
      break;
    case 4:    /* Intra_4x4_Diagonal_Down_Right */
      if(!available_a || !available_b || !available_d)
        return (HANTRO_NOK);
      break;
    case 5:    /* Intra_4x4_Vertical_Right */
      if(!available_a || !available_b || !available_d)
        return (HANTRO_NOK);
      break;
    case 6:    /* Intra_4x4_Horizontal_Down */
      if(!available_a || !available_b || !available_d)
        return (HANTRO_NOK);
      break;
    case 7:    /* Intra_4x4_Vertical_Left */
      if(!available_b)
        return (HANTRO_NOK);
      break;
    default:   /* case 8 Intra_4x4_Horizontal_Up */
      if(!available_a)
        return (HANTRO_NOK);
      break;
    }
  }

  available_a = h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_a);
  if(available_a && constrained_intra_pred &&
      (h264bsdMbPartPredMode(p_mb->mb_a->mbType) == PRED_MODE_INTER))
    available_a = HANTRO_FALSE;
  available_b = h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_b);
  if(available_b && constrained_intra_pred &&
      (h264bsdMbPartPredMode(p_mb->mb_b->mbType) == PRED_MODE_INTER))
    available_b = HANTRO_FALSE;
  available_c = h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_c);
  if(available_c && constrained_intra_pred &&
      (h264bsdMbPartPredMode(p_mb->mb_c->mbType) == PRED_MODE_INTER))
    available_c = HANTRO_FALSE;
  available_d = h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_d);
  if(available_d && constrained_intra_pred &&
      (h264bsdMbPartPredMode(p_mb->mb_d->mbType) == PRED_MODE_INTER))
    available_d = HANTRO_FALSE;

  tmp = CheckIntraChromaPrediction(mb_layer->mbPred.intra_chroma_pred_mode,
                                   available_a, available_b, available_d);
  if(tmp != HANTRO_OK)
    return (tmp);

  if(p_mb->decoded > 1) {
    goto end;
  }

  /* update ASIC MB control field */
  {
    u32 tmp2;
    u32 *p_asic_ctrl =
      p_asic_buff->mb_ctrl.virtual_address +
      (p_asic_buff->current_mb * (ASIC_MB_CTRL_BUFFER_SIZE / 4));

    tmp2 = (u32) HW_I_4x4 << 29;

    tmp2 |= mb_layer->mbPred.intra_chroma_pred_mode << 25;
    tmp2 |= ((available_a == HANTRO_TRUE ? 1U : 0U) << 24);
    tmp2 |= ((available_b == HANTRO_TRUE ? 1U : 0U) << 23);
    tmp2 |= ((available_c == HANTRO_TRUE ? 1U : 0U) << 22);

    tmp2 |= p_mb->qp_y << 15;
    tmp2 |= (u32) (mb_layer->filter_offset_a & 0x0F) << 11;
    tmp2 |= (u32) (mb_layer->filter_offset_b & 0x0F) << 7;

    tmp2 |= p_asic_buff->not_coded_mask;

    p_asic_ctrl[0] = tmp2;

    tmp2 = GetIntraNeighbour(p_mb->slice_id, p_mb->mb_d) << 31;
    tmp2 |= GetIntraNeighbour(p_mb->slice_id, p_mb->mb_b) << 30;
    tmp2 |= GetIntraNeighbour(p_mb->slice_id, p_mb->mb_c) << 29;
    tmp2 |= GetIntraNeighbour(p_mb->slice_id, p_mb->mb_a) << 28;

    tmp2 |= p_asic_buff->rlc_words << 19;
    tmp2 |= mb_layer->disable_deblocking_filter_idc << 17;

    p_asic_ctrl[1] = tmp2;
  }

end:
  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------
    Function name   : DetermineIntra4x4PredMode
    Description     :
    Return type     : u32
    Argument        : macroblockLayer_t * p_mb_layer
    Argument        : u32 available
    Argument        : neighbour_t * n_a
    Argument        : neighbour_t * n_b
    Argument        : u32 index
    Argument        : mbStorage_t * n_mb_a
    Argument        : mbStorage_t * n_mb_b
------------------------------------------------------------------------------*/
u32 DetermineIntra4x4PredMode(macroblockLayer_t * p_mb_layer,
                              u32 available, neighbour_t * n_a,
                              neighbour_t * n_b, u32 index, mbStorage_t * n_mb_a,
                              mbStorage_t * n_mb_b) {
  u32 mode1, mode2;
  mbStorage_t *p_mb;

  ASSERT(p_mb_layer);

  if(!available)  /* dc only prediction? */
    mode1 = 2;
  else {
    p_mb = n_mb_a;
    if(h264bsdMbPartPredMode(p_mb->mbType) == PRED_MODE_INTRA4x4) {
      mode1 = (u32) p_mb->intra4x4_pred_mode[n_a->index];
    } else
      mode1 = 2;

    p_mb = n_mb_b;
    if(h264bsdMbPartPredMode(p_mb->mbType) == PRED_MODE_INTRA4x4) {
      mode2 = (u32) p_mb->intra4x4_pred_mode[n_b->index];
    } else
      mode2 = 2;

    mode1 = MIN(mode1, mode2);
  }

  {
    mbPred_t *mbPred = &p_mb_layer->mbPred;

    if(!mbPred->prev_intra4x4_pred_mode_flag[index]) {
      if(mbPred->rem_intra4x4_pred_mode[index] < mode1) {
        mode1 = mbPred->rem_intra4x4_pred_mode[index];
      } else {
        mode1 = mbPred->rem_intra4x4_pred_mode[index] + 1;
      }
    }
  }
  return (mode1);
}

/*------------------------------------------------------------------------------

    Function: GetIntraNeighbour

        Functional description:
            Checks if macroblock 'n_mb' is part of slice 'slice_id'

------------------------------------------------------------------------------*/
u32 GetIntraNeighbour(u32 slice_id, mbStorage_t * n_mb) {
  if(n_mb && (slice_id == n_mb->slice_id)) {
    return 1;
  } else {
    return 0;
  }
}

/*------------------------------------------------------------------------------

    Function: CheckIntraChromaPrediction

        Functional description:
         Check that the intra chroma prediction mode is valid!

------------------------------------------------------------------------------*/
u32 CheckIntraChromaPrediction(u32 pred_mode, u32 available_a, u32 available_b,
                               u32 available_d) {
  switch (pred_mode) {
  case 0:    /* Intra_Chroma_DC */
    break;
  case 1:    /* Intra_Chroma_Horizontal */
    if(!available_a)
      return (HANTRO_NOK);
    break;
  case 2:    /* Intra_Chroma_Vertical */
    if(!available_b)
      return (HANTRO_NOK);
    break;
  case 3:    /* Intra_Chroma_Plane */
    if(!available_a || !available_b || !available_d)
      return (HANTRO_NOK);
    break;
  default:
    ASSERT(pred_mode < 4);
    return HANTRO_NOK;
  }

  return (HANTRO_OK);
}
