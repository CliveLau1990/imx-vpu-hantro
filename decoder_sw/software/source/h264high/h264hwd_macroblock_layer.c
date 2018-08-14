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

#include "h264hwd_macroblock_layer.h"
#include "h264hwd_slice_header.h"
#include "h264hwd_util.h"
#include "h264hwd_vlc.h"
#include "h264hwd_cavlc.h"
#include "h264hwd_nal_unit.h"
#include "h264hwd_neighbour.h"

#include "h264hwd_dpb.h"
#include "dwl.h"
#include "h264hwd_exports.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 DecodeMbPred(strmData_t *, mbPred_t *, mbType_e, u32,
                        mbStorage_t * );
static u32 DecodeSubMbPred(strmData_t *, subMbPred_t *, mbType_e, u32,
                           mbStorage_t * );
static u32 DecodeResidual(strmData_t *, macroblockLayer_t *, mbStorage_t *);
static u32 DetermineNc(mbStorage_t *, u32, u8 *);

static u32 CbpIntra16x16(mbType_e);

static u32 h264bsdNumMbPart(mbType_e mbType);

static void WritePCMToAsic(const u8 *, DecAsicBuffers_t *);
static void WriteRlcToAsic(mbType_e, u32 cbp, residual_t *, DecAsicBuffers_t *);

static void WriteBlock(const u16 *, u32 *, u32 **, u32 *);
static void WriteSubBlock(const u16 *, u32 *, u32 **, u32 *);

/*------------------------------------------------------------------------------

    Function name: h264bsdDecodeMacroblockLayerCavlc

        Functional description:
          Parse macroblock specific information from bit stream. Called
          when entropy_coding_mode_flag = 0.

        Inputs:
          p_strm_data         pointer to stream data structure
          p_mb               pointer to macroblock storage structure
          p_slice_hdr         pointer to slice header data

        Outputs:
          p_mb_layer          stores the macroblock data parsed from stream

        Returns:
          HANTRO_OK         success
          HANTRO_NOK        end of stream or error in stream

------------------------------------------------------------------------------*/

u32 h264bsdDecodeMacroblockLayerCavlc(strmData_t * p_strm_data,
                                      macroblockLayer_t * p_mb_layer,
                                      mbStorage_t * p_mb,
                                      const sliceHeader_t * p_slice_hdr ) {

  /* Variables */

  u32 tmp, i, value;
  i32 itmp;
  mbPartPredMode_e part_mode;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_mb_layer);

  (void)DWLmemset(p_mb_layer->residual.total_coeff, 0, 24);

  tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);

  if(IS_I_SLICE(p_slice_hdr->slice_type)) {
    if((value + 6) > 31 || tmp != HANTRO_OK)
      return (HANTRO_NOK);
    p_mb_layer->mbType = (mbType_e) (value + 6);
  } else {
    if((value + 1) > 31 || tmp != HANTRO_OK)
      return (HANTRO_NOK);
    p_mb_layer->mbType = (mbType_e) (value + 1);
  }

  if(p_mb_layer->mbType == I_PCM) {
    u8 *level;

    while(!h264bsdIsByteAligned(p_strm_data)) {
      /* pcm_alignment_zero_bit */
      tmp = h264bsdGetBits(p_strm_data, 1);
      if(tmp)
        return (HANTRO_NOK);
    }

    level = (u8 *) p_mb_layer->residual.rlc;
    for(i = 384; i > 0; i--) {
      value = h264bsdGetBits(p_strm_data, 8);
      if(value == END_OF_STREAM)
        return (HANTRO_NOK);
      *level++ = (u8) value;
    }
  } else {
    part_mode = h264bsdMbPartPredMode(p_mb_layer->mbType);
    if((part_mode == PRED_MODE_INTER) &&
        (h264bsdNumMbPart(p_mb_layer->mbType) == 4)) {
      tmp = DecodeSubMbPred(p_strm_data, &p_mb_layer->subMbPred,
                            p_mb_layer->mbType,
                            p_slice_hdr->num_ref_idx_l0_active,
                            p_mb );
    } else {
      tmp = DecodeMbPred(p_strm_data, &p_mb_layer->mbPred,
                         p_mb_layer->mbType, p_slice_hdr->num_ref_idx_l0_active,
                         p_mb );
    }
    if(tmp != HANTRO_OK)
      return (tmp);

    if(part_mode != PRED_MODE_INTRA16x16) {
      tmp = h264bsdDecodeExpGolombMapped(p_strm_data, &value,
                                         (u32) (part_mode ==
                                             PRED_MODE_INTRA4x4));
      if(tmp != HANTRO_OK)
        return (tmp);
      p_mb_layer->coded_block_pattern = value;
    } else {
      p_mb_layer->coded_block_pattern = CbpIntra16x16(p_mb_layer->mbType);
    }

    if(p_mb_layer->coded_block_pattern || (part_mode == PRED_MODE_INTRA16x16)) {
      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
      if(tmp != HANTRO_OK ||
          (u32) (itmp + 26) > 51U /*(itmp >= -26) || (itmp < 26) */ )
        return (HANTRO_NOK);

      p_mb_layer->mb_qp_delta = itmp;

      tmp = DecodeResidual(p_strm_data, p_mb_layer, p_mb);

      if(tmp != HANTRO_OK)
        return (tmp);
    }
  }

  return (HANTRO_OK);
}


/*------------------------------------------------------------------------------

    Function: h264bsdMbPartPredMode

        Functional description:
          Returns the prediction mode of a macroblock type

------------------------------------------------------------------------------*/

mbPartPredMode_e h264bsdMbPartPredMode(mbType_e mbType) {

  ASSERT(mbType <= 31);

  if((mbType <= P_8x8ref0))
    return (PRED_MODE_INTER);
  else if(mbType == I_4x4)
    return (PRED_MODE_INTRA4x4);
  else
    return (PRED_MODE_INTRA16x16);

}

/*------------------------------------------------------------------------------

    Function: h264bsdNumMbPart

        Functional description:
          Returns the amount of macroblock partitions in a macroblock type

------------------------------------------------------------------------------*/

u32 h264bsdNumMbPart(mbType_e mbType) {
  ASSERT(h264bsdMbPartPredMode(mbType) == PRED_MODE_INTER);

  switch (mbType) {
  case P_L0_16x16:
  case P_Skip:
    return (1);

  case P_L0_L0_16x8:
  case P_L0_L0_8x16:
    return (2);

  /* P_8x8 or P_8x8ref0 */
  default:
    return (4);
  }
}

/*------------------------------------------------------------------------------

    Function: h264bsdNumSubMbPart

        Functional description:
          Returns the amount of sub-partitions in a sub-macroblock type

------------------------------------------------------------------------------*/

u32 h264bsdNumSubMbPart(subMbType_e subMbType) {
  ASSERT(subMbType <= P_L0_4x4);

  switch (subMbType) {
  case P_L0_8x8:
    return (1);

  case P_L0_8x4:
  case P_L0_4x8:
    return (2);

  /* P_L0_4x4 */
  default:
    return (4);
  }
}

/*------------------------------------------------------------------------------

    Function: DecodeMbPred

        Functional description:
          Parse macroblock prediction information from bit stream and store
          in 'p_mb_pred'.

------------------------------------------------------------------------------*/

u32 DecodeMbPred(strmData_t * p_strm_data, mbPred_t * p_mb_pred, mbType_e mbType,
                 u32 num_ref_idx_active, mbStorage_t * p_mb ) {

  /* Variables */

  u32 tmp, i, j, value;
  i32 itmp;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_mb_pred);

  switch (h264bsdMbPartPredMode(mbType)) {
  case PRED_MODE_INTER:  /* PRED_MODE_INTER */
    if(num_ref_idx_active > 1) {
      u8 *ref_idx_l0 = p_mb->ref_idx_l0;

      for(i = h264bsdNumMbPart(mbType); i--;) {
        tmp = h264bsdDecodeExpGolombTruncated(p_strm_data, &value,
                                              (u32) (num_ref_idx_active >
                                                  2));
        if(tmp != HANTRO_OK || value >= num_ref_idx_active)
          return (HANTRO_NOK);

        *ref_idx_l0++ = value;
      }
    } else {
      u8 *ref_idx_l0 = p_mb->ref_idx_l0;

      for(i = 4; i > 0; i--) {
        *ref_idx_l0++ = 0;
      }
    }

    /* mvd decoding */
    {
      mv_t *mvd_l0 = p_mb->mv;
      u32 offs_to_next;

      if( mbType == P_L0_L0_8x16 )    offs_to_next = 4;
      else                            offs_to_next = 8; /* "incorrect" for
                                                             * 16x16, but it
                                                             * doesn't matter */
      for(i = h264bsdNumMbPart(mbType); i--;) {
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
        if(tmp != HANTRO_OK)
          return (tmp);
        if (itmp < -16384 || itmp > 16383)
          return(HANTRO_NOK);
        mvd_l0->hor = (i16) itmp;
        tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &itmp);
        if(tmp != HANTRO_OK)
          return (tmp);
        if (itmp < -4096 || itmp > 4095)
          return(HANTRO_NOK);
        mvd_l0->ver = (i16) itmp;

        mvd_l0 += offs_to_next;
      }
    }
    break;

  case PRED_MODE_INTRA4x4: {
    u32 *prev_intra4x4_pred_mode_flag = p_mb_pred->prev_intra4x4_pred_mode_flag;
    u32 *rem_intra4x4_pred_mode = p_mb_pred->rem_intra4x4_pred_mode;

    for(i = 2; i > 0; i--) {
      value = h264bsdShowBits(p_strm_data,32);
      tmp = 0;
      for(j = 8; j--;) {
        u32 b = value & 0x80000000 ? HANTRO_TRUE : HANTRO_FALSE;

        *prev_intra4x4_pred_mode_flag++ = b;

        value <<= 1;

        if(!b) {
          *rem_intra4x4_pred_mode++ = value >> 29;
          value <<= 3;
          tmp++;
        } else {
          rem_intra4x4_pred_mode++;
        }
      }

      if(h264bsdFlushBits(p_strm_data, 8 + 3 * tmp) == END_OF_STREAM)
        return (HANTRO_NOK);
    }
  }
  /* fall-through */

  case PRED_MODE_INTRA16x16:
    tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
    if(tmp != HANTRO_OK || value > 3)
      return (HANTRO_NOK);
    p_mb_pred->intra_chroma_pred_mode = value;
    break;
  }

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: DecodeSubMbPred

        Functional description:
          Parse sub-macroblock prediction information from bit stream and
          store in 'p_mb_pred'.

------------------------------------------------------------------------------*/

u32 DecodeSubMbPred(strmData_t * p_strm_data, subMbPred_t * p_sub_mb_pred,
                    mbType_e mbType, u32 num_ref_idx_active,
                    mbStorage_t * p_mb ) {

  /* Variables */

  u32 tmp, i, j;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_sub_mb_pred);
  ASSERT(h264bsdMbPartPredMode(mbType) == PRED_MODE_INTER);

  {
    subMbType_e *subMbType = p_sub_mb_pred->subMbType;

    for(i = 4; i > 0; i--) {
      u32 value;

      tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &value);
      if(tmp != HANTRO_OK || value > 3)
        return (HANTRO_NOK);
      *subMbType++ = (subMbType_e) value;
    }
  }

  if((num_ref_idx_active > 1) && (mbType != P_8x8ref0)) {
    u8 *ref_idx_l0 = p_mb->ref_idx_l0;
    u32 greater_than_one = (num_ref_idx_active > 2) ? HANTRO_TRUE : HANTRO_FALSE;

    for(i = 4; i > 0; i--) {
      u32 value;

      tmp = h264bsdDecodeExpGolombTruncated(p_strm_data, &value,
                                            greater_than_one);
      if(tmp != HANTRO_OK || value >= num_ref_idx_active)
        return (HANTRO_NOK);
      *ref_idx_l0++ = value;
    }
  } else {
    u8 *ref_idx_l0 = p_mb->ref_idx_l0;

    for(i = 4; i > 0; i--) {
      *ref_idx_l0++ = 0;
    }
  }

  for(i = 0; i < 4; i++) {
    mv_t *mvd_l0 = p_mb->mv + i * 4;
    subMbType_e subMbType;
    u32 offs_to_next;
    static const u32 mvd_offs[4] = { 0, 2, 1, 1 }; /* offset to next sub mb
                                                       * part motion vector */
    subMbType = p_sub_mb_pred->subMbType[i];
    offs_to_next = mvd_offs[(u32)subMbType];

    for(j = h264bsdNumSubMbPart(subMbType); j--;) {
      i32 value;

      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &value);
      if(tmp != HANTRO_OK)
        return (tmp);
      if (value < -16384 || value > 16383)
        return(HANTRO_NOK);
      mvd_l0->hor = (i16) value;

      tmp = h264bsdDecodeExpGolombSigned(p_strm_data, &value);
      if(tmp != HANTRO_OK)
        return (tmp);
      if (value < -4096 || value > 4095)
        return(HANTRO_NOK);
      mvd_l0->ver = (i16) value;

      mvd_l0 += offs_to_next;
    }
  }

  return (HANTRO_OK);

}


/*------------------------------------------------------------------------------

    Function: DetermineNc

        Functional description:
          Returns the nC of a block.

------------------------------------------------------------------------------*/

u32 DetermineNc(mbStorage_t * p_mb, u32 block_index, u8 * p_total_coeff) {

  /* Variables */

  u32 n, tmp;
  const neighbour_t *neighbour_a, *neighbour_b;
  u8 neighbour_aindex, neighbour_bindex;

  /* Code */

  ASSERT(block_index < 24);

  /* if neighbour block belongs to current macroblock total_coeff array
   * mbStorage has not been set/updated yet -> use p_total_coeff */
  neighbour_a = h264bsdNeighbour4x4BlockA(block_index);
  neighbour_b = h264bsdNeighbour4x4BlockB(block_index);
  neighbour_aindex = neighbour_a->index;
  neighbour_bindex = neighbour_b->index;
  if(neighbour_a->mb == MB_CURR && neighbour_b->mb == MB_CURR) {
    n = (p_total_coeff[neighbour_aindex] +
         p_total_coeff[neighbour_bindex] + 1) >> 1;
  } else if(neighbour_a->mb == MB_CURR) {
    n = p_total_coeff[neighbour_aindex];
    if(h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_b)) {
      n = (n + p_mb->mb_b->total_coeff[neighbour_bindex] + 1) >> 1;
    }
  } else if(neighbour_b->mb == MB_CURR) {
    n = p_total_coeff[neighbour_bindex];
    if(h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_a)) {
      n = (n + p_mb->mb_a->total_coeff[neighbour_aindex] + 1) >> 1;
    }
  } else {
    n = tmp = 0;
    if(h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_a)) {
      n = p_mb->mb_a->total_coeff[neighbour_aindex];
      tmp = 1;
    }
    if(h264bsdIsNeighbourAvailable(p_mb, p_mb->mb_b)) {
      if(tmp)
        n = (n + p_mb->mb_b->total_coeff[neighbour_bindex] + 1) >> 1;
      else
        n = p_mb->mb_b->total_coeff[neighbour_bindex];
    }
  }

  return (n);

}

/*------------------------------------------------------------------------------

    Function: DecodeResidual

        Functional description:
          Parse residual information from bit stream and store in 'p_residual'.

------------------------------------------------------------------------------*/

u32 DecodeResidual(strmData_t * p_strm_data, macroblockLayer_t * p_mb_layer,
                   mbStorage_t * p_mb) {
  u32 i, tmp;
  u32 block_coded, block_index;
  u32 is16x16 = 0;    /* no I_16x16 by default */
  u8 *coeff;
  u16 *level;

  residual_t *p_residual = &p_mb_layer->residual;
  mbType_e mbType = p_mb_layer->mbType;
  u32 coded_block_pattern = p_mb_layer->coded_block_pattern;

  ASSERT(p_strm_data);
  ASSERT(p_residual);

  level = p_residual->rlc;
  coeff = p_residual->total_coeff;

  /* luma DC is at index 24 */
  if(h264bsdMbPartPredMode(mbType) == PRED_MODE_INTRA16x16) {
    i32 nc = (i32) DetermineNc(p_mb, 0, p_residual->total_coeff);

    tmp =
      h264bsdDecodeResidualBlockCavlc(p_strm_data, &level[24 * 18], nc, 16);
    if(tmp == (u32)(~0))
      return (tmp);

    coeff[24] = tmp;

    is16x16 = 1;
  }

  block_index = 0;
  /* rest of luma */
  for(i = 4; i > 0; i--) {
    /* luma cbp in bits 0-3 */
    block_coded = coded_block_pattern & 0x1;
    coded_block_pattern >>= 1;
    if(block_coded) {
      u32 j;

      for(j = 4; j > 0; j--) {
        u32 max_coeffs = 16;

        i32 nc =
          (i32) DetermineNc(p_mb, block_index, p_residual->total_coeff);

        if(is16x16) {
          max_coeffs = 15;
        }

        tmp = h264bsdDecodeResidualBlockCavlc(p_strm_data,
                                              level, nc, max_coeffs);
        if(tmp == (u32)(~0))
          return (tmp);

        *coeff++ = tmp;
        level += 18;
        block_index++;
      }
    } else {
      coeff += 4;
      level += 4 * 18;
      block_index += 4;
    }
  }

  level = p_residual->rlc + 25 * 18;
  coeff = p_residual->total_coeff + 25;
  /* chroma DC block are at indices 25 and 26 */
  if(coded_block_pattern) {
    tmp = h264bsdDecodeResidualBlockCavlc(p_strm_data, level, -1, 4);
    if(tmp == (u32)(~0))
      return (tmp);

    *coeff++ = tmp;

    tmp = h264bsdDecodeResidualBlockCavlc(p_strm_data, level + 6, -1, 4);
    if(tmp == (u32)(~0))
      return (tmp);

    *coeff = tmp;
  }

  level = p_residual->rlc + 16 * 18;
  coeff = p_residual->total_coeff + 16;
  /* chroma AC */
  block_coded = coded_block_pattern & 0x2;
  if(block_coded) {
    for(i = 8; i > 0; i--) {
      i32 nc = (i32) DetermineNc(p_mb, block_index, p_residual->total_coeff);

      tmp = h264bsdDecodeResidualBlockCavlc(p_strm_data, level, nc, 15);
      if(tmp == (u32)(~0))
        return (tmp);

      *coeff++ = tmp;

      level += 18;
      block_index++;
    }
  }

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

    Function: CbpIntra16x16

        Functional description:
          Returns the coded block pattern for intra 16x16 macroblock.

------------------------------------------------------------------------------*/

u32 CbpIntra16x16(mbType_e mbType) {

  /* Variables */

  u32 cbp;
  u32 tmp;

  /* Code */

  ASSERT(mbType >= I_16x16_0_0_0 && mbType <= I_16x16_3_2_1);

  if(mbType >= I_16x16_0_0_1)
    cbp = 15;
  else
    cbp = 0;

  /* tmp is 0 for I_16x16_0_0_0 mb type */
  /* ignore lint warning on arithmetic on enum's */
  tmp = /*lint -e(656) */ ((u32) mbType - (u32) I_16x16_0_0_0) >> 2;
  if(tmp > 2)
    tmp -= 3;

  cbp += tmp << 4;

  return (cbp);

}

/*------------------------------------------------------------------------------

    Function: h264bsdPredModeIntra16x16

        Functional description:
          Returns the prediction mode for intra 16x16 macroblock.

------------------------------------------------------------------------------*/

u32 h264bsdPredModeIntra16x16(mbType_e mbType) {

  /* Variables */

  u32 tmp;

  /* Code */

  ASSERT(mbType >= I_16x16_0_0_0 && mbType <= I_16x16_3_2_1);

  /* tmp is 0 for I_16x16_0_0_0 mb type */
  /* ignore lint warning on arithmetic on enum's */
  tmp = /*lint -e(656) */ (mbType - I_16x16_0_0_0);

  return (tmp & 0x3);

}

/*------------------------------------------------------------------------------

    Function: h264bsdDecodeMacroblock

        Functional description:
          Decode one macroblock and write into output image.

        Inputs:
          storage      decoder storage
          mb_num         current macroblock number
          qp_y           pointer to slice QP
          p_asic_buff     asic interface

        Outputs:
          p_mb           structure is updated with current macroblock
          currImage     decoded macroblock is written into output image

        Returns:
          HANTRO_OK     success
          HANTRO_NOK    error in macroblock decoding

------------------------------------------------------------------------------*/

u32 h264bsdDecodeMacroblock(storage_t * storage, u32 mb_num, i32 * qp_y,
                            DecAsicBuffers_t * p_asic_buff) {

  u32 tmp;
  mbType_e mbType;

  macroblockLayer_t *p_mb_layer = storage->mb_layer;
  mbStorage_t *p_mb = storage->mb + mb_num;
  residual_t *residual = &p_mb_layer->residual;

  ASSERT(p_mb);
  ASSERT(p_mb_layer);
  ASSERT(qp_y && *qp_y < 52);

  mbType = p_mb_layer->mbType;
  p_mb->mbType = mbType;

  if(!p_mb->decoded)
    p_mb->mb_type_asic = mbType;

  p_mb->decoded++;

  if(mbType == I_PCM) {
    p_mb->qp_y = 0;

    /* if decoded flag > 1 -> mb has already been successfully decoded and
     * written to output -> do not write again */
    (void)DWLmemset(p_mb->total_coeff, 16, 24);

    if(p_mb->decoded <= 1) {
      /* write out PCM data to residual buffer */
      WritePCMToAsic((u8 *) residual->rlc, p_asic_buff);
      /* update ASIC MB control field */
      {
        u32 *p_asic_ctrl = p_asic_buff->mb_ctrl.virtual_address +
                           (p_asic_buff->current_mb * (ASIC_MB_CTRL_BUFFER_SIZE/4));

        *p_asic_ctrl++ = ((u32) (HW_I_PCM) << 29) |
                         ((u32) (p_mb_layer->filter_offset_a & 0x0F) << 11) |
                         ((u32) (p_mb_layer->filter_offset_b & 0x0F) << 7);
        tmp = 0;
        if (p_mb->mb_d && p_mb->slice_id == p_mb->mb_d->slice_id)
          tmp |= 1U<<31;
        if (p_mb->mb_b && p_mb->slice_id == p_mb->mb_b->slice_id)
          tmp |= 1U<<30;
        if (p_mb->mb_c && p_mb->slice_id == p_mb->mb_c->slice_id)
          tmp |= 1U<<29;
        if (p_mb->mb_a && p_mb->slice_id == p_mb->mb_a->slice_id)
          tmp |= 1U<<28;
        tmp |= ((384U/2) << 19);
        tmp |= p_mb_layer->disable_deblocking_filter_idc << 17;
        *p_asic_ctrl = tmp;
      }
    }

    return (HANTRO_OK);
  }

  /* else */

  if(mbType != P_Skip) {
    i32 tmp_qp = *qp_y;

    (void)DWLmemcpy(p_mb->total_coeff, residual->total_coeff, 24);

    /* update qp_y */
    if(p_mb_layer->mb_qp_delta) {
      tmp_qp += p_mb_layer->mb_qp_delta;
      if(tmp_qp < 0)
        tmp_qp += 52;
      else if(tmp_qp > 51)
        tmp_qp -= 52;
    }

    p_mb->qp_y = (u32) tmp_qp;
    *qp_y = tmp_qp;

    /* write out residual to ASIC */
    if(p_mb->decoded <= 1) {
      WriteRlcToAsic(mbType, p_mb_layer->coded_block_pattern, residual,
                     p_asic_buff);
    }

  } else {
    (void)DWLmemset(p_mb->total_coeff, 0, 24);
    p_mb->qp_y = (u32) * qp_y;
    p_asic_buff->not_coded_mask = 0x3F;
    p_asic_buff->rlc_words = 0;
  }

  if(h264bsdMbPartPredMode(mbType) != PRED_MODE_INTER) {
    u32 cipf = storage->active_pps->constrained_intra_pred_flag;

    tmp = PrepareIntraPrediction(p_mb, p_mb_layer, cipf, p_asic_buff);
  } else {
    dpbStorage_t *dpb = storage->dpb;

    tmp = PrepareInterPrediction(p_mb, p_mb_layer, dpb, p_asic_buff);
  }

  return (tmp);
}

/*------------------------------------------------------------------------------

    Function: h264bsdSubMbPartMode

        Functional description:
          Returns the macroblock's sub-partition mode.

------------------------------------------------------------------------------*/
#if 0  /* not used */
subMbPartMode_e h264bsdSubMbPartMode(subMbType_e subMbType) {
  ASSERT(subMbType < 4);

  return ((subMbPartMode_e) subMbType);
}
#endif
/*------------------------------------------------------------------------------
    Function name : WritePCMToAsic
    Description   :

    Return type   : void
    Argument      : const u8 * lev
    Argument      : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
void WritePCMToAsic(const u8 * lev, DecAsicBuffers_t * p_asic_buff) {
  i32 i;

  u32 *p_res = (p_asic_buff->residual.virtual_address +
                p_asic_buff->current_mb * (880 / 4));
  for(i = 384 / 4; i > 0; i--) {
    u32 tmp;

    tmp = (*lev++) << 24;
    tmp |= (*lev++) << 16;
    tmp |= (*lev++) << 8;
    tmp |= (*lev++);

    *p_res++ = tmp;
  }
}

/*------------------------------------------------------------------------------
    Function name   : WriteRlcToAsic
    Description     :
    Return type     : void
    Argument        : mbType_e mbType
    Argument        : u32 cbp
    Argument        : residual_t * residual
    Argument        : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
void WriteRlcToAsic(mbType_e mbType, u32 cbp, residual_t * residual,
                    DecAsicBuffers_t * p_asic_buff) {
  u32 block;
  u32 nc;
  u32 wrt_buff;
  u32 word_count;

  const u16 *rlc = residual->rlc;

  u32 *p_res = p_asic_buff->residual.virtual_address +
               p_asic_buff->current_mb * (ASIC_MB_RLC_BUFFER_SIZE / 4);

  ASSERT(p_asic_buff->residual.virtual_address != NULL);
  ASSERT(p_res != NULL);

  nc = 0;
  word_count = 0;
  wrt_buff = 0;

  /* write out luma DC for intra 16x16 */
  if(h264bsdMbPartPredMode(mbType) == PRED_MODE_INTRA16x16) {
    const u16 *p_tmp = (rlc + 24 * 18);
    const u8 *coeff = residual->total_coeff;

    WriteSubBlock(p_tmp, &wrt_buff, &p_res, &word_count);
    for(block = 4; block > 0; block--) {
      u32 j, bc = 0;

      for(j = 4; j > 0; j--) {
        if(*coeff++ != 0)
          bc++;
      }
      if(!bc) {
        cbp &= (~(u32)(1 << (4 - block)));
      }
    }
  } else if(cbp == 0) { /* empty macroblock */
    nc = 0x3F;
    goto end;
  }

  /* write out rest of luma */
  for(block = 4; block > 0; block--) {
    nc <<= 1;

    /* update notcoded block mask */
    if((cbp & 0x01)) {
      WriteBlock(rlc, &wrt_buff, &p_res, &word_count);
    } else {
      nc |= 1;
    }

    rlc += 4 * 18;
    cbp >>= 1;
  }

  /* chroma DC always written out */
  if(cbp == 0) {
    const u16 dc_rlc = 0;

    WriteSubBlock(&dc_rlc, &wrt_buff, &p_res, &word_count);
    WriteSubBlock(&dc_rlc, &wrt_buff, &p_res, &word_count);
  } else {
    const u16 *dc_rlc = residual->rlc + (25 * 18);

    WriteSubBlock(dc_rlc, &wrt_buff, &p_res, &word_count);
    WriteSubBlock(dc_rlc + 6, &wrt_buff, &p_res, &word_count);
  }

  if((cbp & 0x02) == 0) { /* no chroma AC */
    nc = (nc << 2) | 0x03;
  } else {
    /* write out chroma */
    const u8 *coeff = residual->total_coeff + 16;

    nc <<= 1;

    /* update notcoded block mask */
    /* Cb */
    if((coeff[0] == 0) && (coeff[1] == 0) &&
        (coeff[2] == 0) && (coeff[3] == 0)) {
      nc |= 1;
    } else {
      WriteBlock(rlc, &wrt_buff, &p_res, &word_count);
    }

    rlc += 4 * 18;

    nc <<= 1;

    coeff += 4;
    /* Cr */
    if((coeff[0] == 0) && (coeff[1] == 0) &&
        (coeff[2] == 0) && (coeff[3] == 0)) {
      nc |= 1;
    } else {
      WriteBlock(rlc, &wrt_buff, &p_res, &word_count);
    }
  }

end:

  if(word_count & 0x01) {
    *p_res = wrt_buff;
  }

  p_asic_buff->not_coded_mask = nc;
  p_asic_buff->rlc_words = word_count;
}

/*------------------------------------------------------------------------------
    Function name   : WriteSubBlock
    Description     :
    Return type     : void
    Argument        : const u16 * rlc
    Argument        : u32 * p_wrt_buff
    Argument        : u32 ** res
    Argument        : u32 * p_word_count
------------------------------------------------------------------------------*/
void WriteSubBlock(const u16 * rlc, u32 * p_wrt_buff, u32 ** res,
                   u32 * p_word_count) {
  /*lint -efunc(416, WriteSubBlock) */
  /*lint -efunc(661, WriteSubBlock) */
  /*lint -efunc(662, WriteSubBlock) */

  /* Reason: when rlc[1] words = 0 so, overflow cannot occure in p_tmp */
  /* this happens for 0 chromaDC values which are always written out */

  u32 wrt_buff = *p_wrt_buff;
  u32 *p_res = *res;
  u32 word_count = *p_word_count;

  const u16 *p_tmp = rlc;
  u16 words;

  {
    u16 rlc_ctrl = *p_tmp++;

    words = rlc_ctrl >> 11;

    if((word_count++) & 0x01) {
      wrt_buff |= rlc_ctrl;
      *p_res++ = wrt_buff;
    } else {
      wrt_buff = rlc_ctrl << 16;
    }

    if(rlc_ctrl & 0x01) {
      words++;
    } else {
      p_tmp++;
    }
  }

  for(; words > 0; words--) {
    if((word_count++) & 0x01) {
      wrt_buff |= *p_tmp++;
      *p_res++ = wrt_buff;
    } else {
      wrt_buff = (*p_tmp++) << 16;
    }
  }

  *p_wrt_buff = wrt_buff;
  *res = p_res;
  *p_word_count = word_count;
}

/*------------------------------------------------------------------------------
    Function name   : WriteBlock
    Description     :
    Return type     : void
    Argument        : const u16 * rlc
    Argument        : u32 * p_wrt_buff
    Argument        : u32 ** res
    Argument        : u32 * p_word_count
------------------------------------------------------------------------------*/
void WriteBlock(const u16 * rlc, u32 * p_wrt_buff, u32 ** res, u32 * p_word_count) {
  i32 i;

  for(i = 4; i > 0; i--) {
    WriteSubBlock(rlc, p_wrt_buff, res, p_word_count);

    rlc += 18;
  }
}
