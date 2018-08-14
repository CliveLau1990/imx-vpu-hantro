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

#include "mp4dechwd_motiontexture.h"
#include "mp4dechwd_vlc.h"
#include "mp4dechwd_utils.h"
#include "mp4dechwd_rvlc.h"

#include "mp4debug.h"
#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif
/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum {
  MOTION_MARKER = 0x1F001,/* 17 bits */
  DC_MARKER     = 0x6B001 /* 19 bits */
};

/* masks defining bits not to be changed when writing to ctrl register. Each
 * '1' in the mask means that bit value will not be changed. See AMBAIFD spec
 * for meaning of the bits (table 2-12) */
#define MASK1   0x000001FF  /* don't change bits [8,0] */

/* Mask 2: don't change VPBI, INTRA/INTER,DCincluded, QP, all BS bits */
#define MASK2   (((u32)1<<ASICPOS_MBTYPE)|((u32)1<<ASICPOS_VPBI)| \
                ((u32)1<<ASICPOS_USEINTRADCVLC)|((u32)0x1F<<ASICPOS_QP)| \
                0xFFFF)

#define MASK3   0x000001FF  /* don't change bits [8,0] */

#define ASIC_VPBI ((u32)1<<ASICPOS_VPBI)
#define ASIC_INTER ((u32)1<<ASICPOS_MBTYPE)
#define ASIC_INTER_VPBI1 (((u32)1<<ASICPOS_VPBI)|((u32)1<<ASICPOS_MBTYPE))

#define ASIC_SEPAR_DC ((u32)1<<ASICPOS_USEINTRADCVLC)

/* macro to write value into asic control bits register. 2 addresses per block
 * first for motion vectors. '1's in mask determine bits not to be changed. */
#define WRITE_ASIC_CTRL_BITS(mb,offset,value,mask) \
    dec_container->MbSetDesc.p_ctrl_data_addr[NBR_OF_WORDS_MB*(mb) + offset] = \
    (dec_container->MbSetDesc.p_ctrl_data_addr[NBR_OF_WORDS_MB*(mb) + offset] \
     & mask) | (value);

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/* value dquant[i] added to QP when value of dquant is i */
static const i32 dquant[4] = { -1, -2, 1, 2 };

/* intra DC vlc used at threshold i if QP less than value intra_dc_qp[i] */
static const u32 intra_dc_qp[8] = { 32, 13, 15, 17, 19, 21, 23, 0 };
const u8 asic_pos_no_rlc[6] = { 27, 26, 25, 24, 23, 22 };

u32 StrmDec_DecodeCombinedMT(DecContainer * dec_container);
u32 StrmDec_DecodePartitionedIVop(DecContainer * dec_container);
u32 StrmDec_DecodePartitionedPVop(DecContainer * dec_container);
u32 StrmDec_UseIntraDcVlc(DecContainer * dec_container, u32 mb_number);

/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_DecodeCombinedMT

        Purpose: Decode combined motion texture data

        Input:
            Pointer to DecContainer structure

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeCombinedMT(DecContainer * dec_container) {

  u32 tmp = 0;
  u32 mb_number = 0;
  u32 mb_counter = 0;

  MP4DEC_API_DEBUG(("entry: StrmDec_DecodeCombinedMT \n"));

  mb_number = dec_container->StrmStorage.vp_mb_number;
  dec_container->StrmStorage.vp_first_coded_mb = mb_number;

  do {
    tmp = StrmDec_DecodeMb(dec_container, mb_number);
    if(tmp != HANTRO_OK)
      return (tmp);

    if(!MB_IS_STUFFING(mb_number)) {
      mb_number++;
      mb_counter++;
      /* read remaining stuffing macro block if end of VOP */
      if(mb_number == dec_container->VopDesc.total_mb_in_vop) {
        tmp = 9 + (u32)(dec_container->VopDesc.vop_coding_type == PVOP);
        while(StrmDec_ShowBits(dec_container, tmp) == 0x1)
          (void) StrmDec_FlushBits(dec_container, tmp);
      }
    }
  } while( (mb_number < dec_container->VopDesc.total_mb_in_vop) &&
           ( (StrmDec_CheckStuffing(dec_container) != HANTRO_OK) ||
             StrmDec_ShowBitsAligned(dec_container, 16, 1) ) );

  dec_container->StrmStorage.vp_num_mbs = mb_counter;

  MP4DEC_API_DEBUG(("StrmDec_DecodeCombinedMT \n"));
  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name: StrmDec_DecodePartitionedIVop

        Purpose: Decode data partitioned motion texture data of I-VOP

        Input:
            Pointer to DecContainer structure

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodePartitionedIVop(DecContainer * dec_container) {

  u32 input_buffer;
  u32 used_bits;
  u32 i = 0, j, tmp, tmp_mask;
  u32 mb_number, mb_counter;
  i32 q_p;
  i32 dc_coeff;
  u32 coded;
  u32 control_bits;
  u32 *pu32;
  u32 status = HANTRO_OK;
  u32 dc_tmp0 = 0, dc_tmp1 = 0;

  MP4DEC_API_DEBUG(("entry: StrmDec_DecodePartitionedIVop # \n"));
  mb_counter = 0;
  mb_number = dec_container->StrmStorage.vp_mb_number;
  dec_container->StrmStorage.vp_first_coded_mb = mb_number;
  dec_container->StrmStorage.vp_num_mbs = 0;

  do {
    SHOWBITS32(input_buffer);
    used_bits = 0;
    control_bits = 0;
    /* set video packet boundary flag if first mb of video packet */
    if(mb_counter == 0) {
      control_bits |= ASIC_VPBI;
    }

    status = StrmDec_DecodeMcbpc(dec_container, mb_number,
                                 (input_buffer << used_bits) >> 23,
                                 &used_bits);
    if(status != HANTRO_OK)
      return (status);

    if(MB_IS_STUFFING(mb_number)) {
      FLUSHBITS(used_bits);
      used_bits = 0;
      continue;
    }

    dec_container->StrmStorage.prev_qp =
      dec_container->StrmStorage.q_p;
    if(dec_container->MBDesc[mb_number].type_of_mb == MB_INTRAQ) {
      tmp = (input_buffer << used_bits) >> 30;  /* dquant */
      used_bits += 2;

      q_p =
        (i32) (dec_container->StrmStorage.q_p) + dquant[tmp];
      SATURATE(1, q_p, 31);
      dec_container->StrmStorage.q_p = (u32) q_p;
    }

    FLUSHBITS(used_bits);
    used_bits = 0;

    control_bits |= dec_container->StrmStorage.q_p << ASICPOS_QP;

    pu32 = dec_container->MbSetDesc.p_ctrl_data_addr +
           NBR_OF_WORDS_MB * mb_number;
    if(StrmDec_UseIntraDcVlc(dec_container, mb_number)) {
      control_bits |= ASIC_SEPAR_DC;   /* if true == coded separately */
      dc_tmp0 = dc_tmp1 = 0;
      for(j = 0; j < 6; j++) {
        status =
          StrmDec_DecodeDcCoeff(dec_container, mb_number, j, &dc_coeff);
        if(status != HANTRO_OK) {
          return (status);
        }

        if (j < 3)
          dc_tmp0 = (dc_tmp0 << 10) | (dc_coeff & 0x3ff);
        else
          dc_tmp1 = (dc_tmp1 << 10) | (dc_coeff & 0x3ff);
      }

      /*MP4DEC_API_DEBUG(("datap ivop DC %d %d  #\n", dc_tmp0, dc_tmp1)); */
      dec_container->MbSetDesc.
      p_dc_coeff_data_addr[mb_number * NBR_DC_WORDS_MB] = dc_tmp0;
      dec_container->MbSetDesc.
      p_dc_coeff_data_addr[mb_number * NBR_DC_WORDS_MB + 1] = dc_tmp1;

    }

    *pu32 = control_bits;
    mb_counter++;
    mb_number++;

    /* last macro block of vop -> check stuffing macro blocks */
    if(mb_number == dec_container->VopDesc.total_mb_in_vop) {
      while(StrmDec_ShowBits(dec_container, 9) == 0x1) {
        tmp = StrmDec_FlushBits(dec_container, 9);
        if(tmp == END_OF_STREAM)
          return (END_OF_STREAM);
      }
      if(StrmDec_ShowBits(dec_container, 19) == DC_MARKER) {
        break;
      } else {
        return (HANTRO_NOK);
      }
    }

  } while(StrmDec_ShowBits(dec_container, 19) != DC_MARKER);

  status = StrmDec_FlushBits(dec_container, 19);
  if(status != HANTRO_OK)
    return (status);

  dec_container->StrmStorage.vp_num_mbs = mb_counter;

  mb_number = dec_container->StrmStorage.vp_mb_number;

  SHOWBITS32(input_buffer);
  used_bits = 0;

  for(i = mb_number; i < (mb_number + mb_counter); i++) {
    if(used_bits > 25) {
      FLUSHBITS(used_bits);
      used_bits = 0;
      SHOWBITS32(input_buffer);
    }
    control_bits = 0;
    /* ac prediction flag */
    tmp = (input_buffer << used_bits) >> 31;
    used_bits++;
#ifdef ASIC_TRACE_SUPPORT
    if (tmp)
      trace_mpeg4_dec_tools.ac_pred = 1;
#endif
    control_bits |= tmp << ASICPOS_ACPREDFLAG;

    status = StrmDec_DecodeCbpy(dec_container, i,
                                (input_buffer << used_bits) >> 26,
                                &used_bits);
    if(status != HANTRO_OK)
      return (status);

    /* write ASIC control bits */
    for(j = 0; j < 6; j++) {
      coded =
        !((dec_container->StrmStorage.
           coded_bits[i] >> (5 - j)) & 0x1);
      control_bits |= (coded << asic_pos_no_rlc[j]);
      /*WRITE_ASIC_CTRL_BITS(i,2*j+1,tmp,MASK2) moved couple lines ; */
    }
    WRITE_ASIC_CTRL_BITS(i, 0 /*2*j */ , control_bits, MASK2);

  }
  FLUSHBITS(used_bits);

  if(dec_container->Hdrs.reversible_vlc) {
    status = StrmDec_DecodeRvlc(dec_container, mb_number, mb_counter);
    if(status != HANTRO_OK)
      return (status);
  } else {
    for(i = mb_number; i < (mb_number + mb_counter); i++) {
      /* initialize mask for block number 0 */
      tmp_mask = 0x20;
      for(j = 0; j < 6; j++) {
        if(dec_container->StrmStorage.coded_bits[i] & tmp_mask) {
          status = StrmDec_DecodeVlcBlock(dec_container, i, j);
          if(status != HANTRO_OK)
            return (status);
        }
        tmp_mask >>= 1;
      }
    }
  }
  return (status);

}

/*------------------------------------------------------------------------------

   5.3  Function name: StrmDec_DecodePartitionedPVop

        Purpose: Decode data partitioned motion texture data of P-VOP

        Input:
            Pointer to DecContainer structure

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodePartitionedPVop(DecContainer * dec_container) {

  u32 input_buffer;
  u32 used_bits;
  u32 h, i, j, tmp, tmp_mask;
  u32 mb_number, mb_counter;
  i32 q_p;
  i32 dc_coeff;
  u32 control_bits;
  u32 coded;
  u32 intra_dc_vlc;
  u32 *pu32;
  u32 status = HANTRO_OK;
  u32 dc_tmp0 = 0, dc_tmp1 = 0;

  MP4DEC_API_DEBUG(("Decode PartitionedP #\n"));

  mb_counter = 0;
  mb_number = dec_container->StrmStorage.vp_mb_number;
  dec_container->StrmStorage.vp_first_coded_mb = mb_number;
  dec_container->StrmStorage.vp_num_mbs = 0;

  do {

    SHOWBITS32(input_buffer);
    used_bits = 0;
    tmp = input_buffer >> 31;    /* not_coded */
    used_bits++;

    if(!tmp) {
      status = StrmDec_DecodeMcbpc(dec_container, mb_number,
                                   (input_buffer << used_bits) >>
                                   23, &used_bits);
      if(status != HANTRO_OK)
        return (status);
      FLUSHBITS(used_bits);
      used_bits = 0;

      if(dec_container->MBDesc[mb_number].type_of_mb == MB_STUFFING) {
        continue;
      }

      if(MB_IS_INTER(mb_number)) {
        status = StrmDec_DecodeMv(dec_container, mb_number);
        if(status != HANTRO_OK)
          return (status);
      } else {
        /* write 0 motion vectors to asic control bits */
        dec_container->MbSetDesc.
        p_mv_data_addr[mb_number * NBR_MV_WORDS_MB]
          = ASIC_ZERO_MOTION_VECTORS_LUM;
      }
    }
    /* not coded -> increment first coded if necessary */
    else {
      FLUSHBITS(used_bits);
      used_bits = 0;
      if(mb_number == dec_container->StrmStorage.vp_first_coded_mb) {
        dec_container->StrmStorage.vp_first_coded_mb++;
      }
      dec_container->StrmStorage.coded_bits[mb_number] =
        MB_NOT_CODED;
      dec_container->MBDesc[mb_number].type_of_mb = MB_INTER;

      dec_container->MbSetDesc.p_mv_data_addr[mb_number * NBR_MV_WORDS_MB] =
        ASIC_ZERO_MOTION_VECTORS_LUM;

    }

    mb_counter++;
    mb_number++;

    /* last macro block of vop -> check stuffing macro blocks */
    if(mb_number == dec_container->VopDesc.total_mb_in_vop) {
      while(StrmDec_ShowBits(dec_container, 10) == 0x1) {
        tmp = StrmDec_FlushBits(dec_container, 10);
        if(tmp == END_OF_STREAM) {
          return (END_OF_STREAM);
        }
      }
      if(StrmDec_ShowBits(dec_container, 17) == MOTION_MARKER) {
        break;
      } else {
        return (HANTRO_NOK);
      }
    }

  } while(StrmDec_ShowBits(dec_container, 17) != MOTION_MARKER);

  status = StrmDec_FlushBits(dec_container, 17);
  if(status != HANTRO_OK)
    return (status);

  dec_container->StrmStorage.vp_num_mbs = mb_counter;

  mb_number = dec_container->StrmStorage.vp_mb_number;

  SHOWBITS32(input_buffer);
  used_bits = 0;
  for(i = mb_number; i < (mb_number + mb_counter); i++) {
    if(used_bits > 23) {
      FLUSHBITS(used_bits);
      used_bits = 0;
      SHOWBITS32(input_buffer);
    }
    control_bits = 0;
    /* set video packet boundary flag if first mb of video packet */
    if(i == mb_number) {

      control_bits |= (ASIC_VPBI);
      /*SET_CONCEALMENT_POINTERS(); */
    }
    if(MB_IS_INTER(i)) {
      control_bits |= ((u32) 0x1 << (ASICPOS_MBTYPE));
    }

    pu32 = dec_container->MbSetDesc.p_ctrl_data_addr
           + NBR_OF_WORDS_MB * i;
    if(dec_container->StrmStorage.coded_bits[i] != MB_NOT_CODED) {
      /* ac prediction flag */
      if(MB_IS_INTRA(i)) {
        tmp = (input_buffer << used_bits) >> 31;
        used_bits++;
#ifdef ASIC_TRACE_SUPPORT
        if (tmp)
          trace_mpeg4_dec_tools.ac_pred = 1;
#endif
        control_bits |= (tmp << ASICPOS_ACPREDFLAG);
      }

      status = StrmDec_DecodeCbpy(dec_container, i,
                                  (input_buffer << used_bits) >> 26,
                                  &used_bits);
      if(status != HANTRO_OK)
        return (status);

      dec_container->StrmStorage.prev_qp =
        dec_container->StrmStorage.q_p;
      if(MB_HAS_DQUANT(i)) {
        tmp = (input_buffer << used_bits) >> 30;  /* dquant */
        used_bits += 2;

        q_p = (i32) (dec_container->StrmStorage.q_p) +
              dquant[tmp];
        SATURATE(1, q_p, 31);
        dec_container->StrmStorage.q_p = (u32) q_p;
      }

      control_bits |= dec_container->StrmStorage.q_p << ASICPOS_QP;

      intra_dc_vlc = MB_IS_INTRA(i) &&
                     StrmDec_UseIntraDcVlc(dec_container, i);
      if(intra_dc_vlc) {
        FLUSHBITS(used_bits);
        used_bits = 0;
        control_bits |= ASIC_SEPAR_DC;
      } else {
        control_bits &= ~(ASIC_SEPAR_DC);
      }
      if(MB_IS_INTER4V(i)) {
        control_bits |= (0x1 << (ASICPOS_4MV));
      }

      dc_tmp0 = dc_tmp1 = 0;
      for(j = 0; j < 6; j++) {

        if(intra_dc_vlc) {
          status = StrmDec_DecodeDcCoeff(dec_container, i, j,
                                         &dc_coeff);
          if(status != HANTRO_OK)
            return (status);

          if (j < 3)
            dc_tmp0 = (dc_tmp0 << 10) | (dc_coeff & 0x3ff);
          else
            dc_tmp1 = (dc_tmp1 << 10) | (dc_coeff & 0x3ff);
        }
        coded =
          !((dec_container->StrmStorage.
             coded_bits[i] >> (5 - j)) & 0x1);
        control_bits |= (coded << asic_pos_no_rlc[j]);
      }
      *pu32 = control_bits;

      if(intra_dc_vlc) {
        /*MP4DEC_API_DEBUG(("combined ivop DC %d %d  #\n", dc_tmp0, dc_tmp1)); */
        SHOWBITS32(input_buffer);
        /* write DC coeffs to interface */
        dec_container->MbSetDesc.
        p_dc_coeff_data_addr[i * NBR_DC_WORDS_MB] = dc_tmp0;
        dec_container->MbSetDesc.
        p_dc_coeff_data_addr[i * NBR_DC_WORDS_MB + 1] = dc_tmp1;
      }
    }

    else {  /* not coded mb */
      control_bits |= (1 << ASICPOS_MBNOTCODED);
      control_bits |= dec_container->StrmStorage.q_p << ASICPOS_QP;
      for(h = 0; h < 6; h++) {
        control_bits |= (1 << asic_pos_no_rlc[h]);
      }
      /* write asic control bits */
      *pu32 = control_bits;
      pu32 += NBR_OF_WORDS_MB;

    }

  }
  if(used_bits)
    FLUSHBITS(used_bits);

  if(dec_container->Hdrs.reversible_vlc) {
    status = StrmDec_DecodeRvlc(dec_container, mb_number, mb_counter);
    if(status != HANTRO_OK)
      return (status);
  } else {
    for(i = mb_number; i < (mb_number + mb_counter); i++) {
      if(dec_container->StrmStorage.coded_bits[i] != MB_NOT_CODED) {
        /* initialize mask for block number 0 */
        tmp_mask = 0x20;
        for(j = 0; j < 6; j++) {
          if(dec_container->StrmStorage.coded_bits[i] & tmp_mask) {
            status = StrmDec_DecodeVlcBlock(dec_container, i, j);
            if(status != HANTRO_OK)
              return (status);
          }
          tmp_mask >>= 1;
        }
      }
    }
  }

  return (status);

}

/*------------------------------------------------------------------------------

   5.4  Function name: StrmDec_DecodeMotionTexture

        Purpose: Decode data partitioned motion texture data

        Input:
            Pointer to DecContainer structure

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeMotionTexture(DecContainer * dec_container) {

  u32 status = HANTRO_OK;

  if(dec_container->Hdrs.data_partitioned) {
    if(dec_container->VopDesc.vop_coding_type == IVOP)
      status = StrmDec_DecodePartitionedIVop(dec_container);
    else
      status = StrmDec_DecodePartitionedPVop(dec_container);
  } else {
    /* TODO REMOVE */
    status = StrmDec_DecodeCombinedMT(dec_container);
  }

  return (status);

}

/*------------------------------------------------------------------------------

   5.4  Function name: StrmDec_UseIntraDcVlc

        Purpose: determine whether to use intra DC vlc or not

        Input:
            Pointer to DecContainer structure
                -uses StrmStorage
            u32 MbNumber

        Output:
            0 don't use intra dc vlc
            1 use intra dc vlc

------------------------------------------------------------------------------*/

u32 StrmDec_UseIntraDcVlc(DecContainer * dec_container, u32 mb_number) {

  u32 q_p;

  if(mb_number == dec_container->StrmStorage.vp_first_coded_mb) {
    q_p = dec_container->StrmStorage.q_p;
  } else {
    q_p = dec_container->StrmStorage.prev_qp;
  }

  ASSERT((q_p > 0) && (q_p < 32));
  ASSERT(dec_container->VopDesc.intra_dc_vlc_thr < 8);

  if(q_p < intra_dc_qp[dec_container->VopDesc.intra_dc_vlc_thr]) {
    return (1);
  } else {
    return (0);
  }

}

/*------------------------------------------------------------------------------

   5.6  Function name:
                    StrmDec_DecodeMb

        Purpose:
                    Decodes Macro block from stream

        Input:
                    pointer to DecContainer

        Output:
                    status (HANTRO_OK/NOK/END_OF_STREAM)

------------------------------------------------------------------------------*/

u32 StrmDec_DecodeMb(DecContainer * dec_container, u32 mb_number) {
  u32 flush_ret = 0;
  u32 btmp = 0;
  u32 asic_tmp;
  u32 input_buffer;
  u32 used_bits;
  u32 *pasic_ctrl;
  u32 not_coded;
  u32 ac_pred_flag;
  u32 status;
  u32 use_intra_dc_vlc;
  u32 d_quant;
  u32 i;
  i32 q_p;
  u32 coded_block;
  i32 dc_coeff;
  u32 dc_tmp0 = 0, dc_tmp1 = 0;

  /*MP4DEC_API_DEBUG(("StrmDec_DecodeMb #\n")); */
  pasic_ctrl = &(dec_container->MbSetDesc.
                 p_ctrl_data_addr[NBR_OF_WORDS_MB * mb_number]);
  SHOWBITS32(input_buffer);

  if(dec_container->VopDesc.vop_coding_type == PVOP) {
    not_coded = input_buffer >> 31;
    used_bits = 1;
  } else {
    not_coded = 0;
    used_bits = 0;
  }

  if(!not_coded) {
    status = StrmDec_DecodeMcbpc(dec_container, mb_number,
                                 (input_buffer << used_bits) >> 23,
                                 &used_bits);
    if(status != HANTRO_OK)
      return (status);
    /* CHANGED BY JANSA 2407 */
    if(dec_container->MBDesc[mb_number].type_of_mb == MB_STUFFING) {
      status = StrmDec_FlushBits(dec_container, used_bits);
      return (HANTRO_OK);
    }
  }

  if(not_coded) {
    FLUSHBITS(used_bits);
    /* 1st in Video Packet? and Inter Mb info */
    if(((mb_number == dec_container->StrmStorage.vp_mb_number) &&
        (dec_container->StrmStorage.short_video == HANTRO_FALSE)) ||
        !mb_number) {
      asic_tmp = ASIC_INTER_VPBI1;
      for(i = 0; i < 6; i++) {
        asic_tmp |= (1 << asic_pos_no_rlc[i]);
      }
      /*SET_CONCEALMENT_POINTERS(); */
    } else {
      asic_tmp = ASIC_INTER;
      for(i = 0; i < 6; i++) {
        asic_tmp |= (1 << asic_pos_no_rlc[i]);
      }
    }
    if(!mb_number) {

      asic_tmp |= (1U << ASICPOS_VPBI);
      /*SET_CONCEALMENT_POINTERS(); */
    }
    if((mb_number == dec_container->StrmStorage.vp_mb_number) &&
        (dec_container->StrmStorage.short_video == HANTRO_FALSE)) {

      asic_tmp = asic_tmp | (1U << ASICPOS_VPBI);
      /*SET_CONCEALMENT_POINTERS(); */

    } else if((mb_number == dec_container->StrmStorage.vp_mb_number) &&
              (dec_container->StrmStorage.gob_resync_flag == HANTRO_TRUE)) {

      asic_tmp = asic_tmp | (1U << ASICPOS_VPBI);
      /*SET_CONCEALMENT_POINTERS(); */
    }

    if(dec_container->StrmStorage.vp_first_coded_mb == mb_number)
      dec_container->StrmStorage.vp_first_coded_mb++;
    asic_tmp |= (dec_container->StrmStorage.q_p << ASICPOS_QP);
    asic_tmp |= (1 << ASICPOS_MBNOTCODED);

    *pasic_ctrl = asic_tmp;

    /* write ctrl bits to memory and return(HANTRO_OK) */
    dec_container->MbSetDesc.p_mv_data_addr[NBR_MV_WORDS_MB * mb_number] =
      ASIC_ZERO_MOTION_VECTORS_LUM;

    /* this is needed for differential motion vector decoding */
    dec_container->StrmStorage.coded_bits[mb_number] = MB_NOT_CODED;
    dec_container->MBDesc[mb_number].type_of_mb = MB_INTER;

    /* check if stuffing macro blocks follow */
    while(StrmDec_ShowBits(dec_container, 10) == 0x01) {
      flush_ret = StrmDec_FlushBits(dec_container, 10);
      if(flush_ret != HANTRO_OK)
        return HANTRO_NOK;
    }

    return (HANTRO_OK);
  }

  /* svh and mbType 2 */

  if(dec_container->StrmStorage.short_video &&
      (dec_container->MBDesc[mb_number].type_of_mb == MB_INTER4V))
    return (HANTRO_NOK);

  asic_tmp = 0;
  if(MB_IS_INTER(mb_number)) {
    asic_tmp = ASIC_INTER;

    if(MB_IS_INTER4V(mb_number)) {
      asic_tmp |= (0x1 << ASICPOS_4MV);
    }
  }
  if(!mb_number) {
    asic_tmp = asic_tmp | (1U << ASICPOS_VPBI);
    /*SET_CONCEALMENT_POINTERS(); */
  }
  if((mb_number == dec_container->StrmStorage.vp_mb_number) &&
      (dec_container->StrmStorage.short_video == HANTRO_FALSE)) {

    asic_tmp = asic_tmp | (1U << ASICPOS_VPBI);
    /*SET_CONCEALMENT_POINTERS(); */

  } else if((mb_number == dec_container->StrmStorage.vp_mb_number) &&
            (dec_container->StrmStorage.gob_resync_flag == HANTRO_TRUE)) {

    asic_tmp = asic_tmp | (1U << ASICPOS_VPBI);
    /*SET_CONCEALMENT_POINTERS(); */
  }

  if(MB_IS_INTER(mb_number) || dec_container->StrmStorage.short_video)
    ac_pred_flag = 0;
  else {
    ac_pred_flag = (input_buffer << used_bits) >> 31;
    used_bits++;
  }
  asic_tmp = asic_tmp | (ac_pred_flag << ASICPOS_ACPREDFLAG);

#ifdef ASIC_TRACE_SUPPORT
  if (ac_pred_flag)
    trace_mpeg4_dec_tools.ac_pred = 1;
#endif

  status = StrmDec_DecodeCbpy(dec_container, mb_number,
                              (input_buffer << used_bits) >> 26,
                              &used_bits);
  if(status != HANTRO_OK)
    return (status);

  dec_container->StrmStorage.prev_qp = dec_container->StrmStorage.q_p;
  if(MB_HAS_DQUANT(mb_number)) {
    d_quant = (input_buffer << used_bits) >> 30;
    used_bits += 2;
    q_p = (i32) dec_container->StrmStorage.q_p +
          dquant[d_quant];
    SATURATE(1, q_p, 31);
    dec_container->StrmStorage.q_p = (u32) q_p;
  }
  asic_tmp =
    asic_tmp | (dec_container->StrmStorage.q_p << ASICPOS_QP);

  if(MB_IS_INTER(mb_number))
    use_intra_dc_vlc = 0;
  else {
    use_intra_dc_vlc = StrmDec_UseIntraDcVlc(dec_container, mb_number)
                       || dec_container->StrmStorage.short_video;
    if(use_intra_dc_vlc) {
      use_intra_dc_vlc = 1;
    }
    asic_tmp =
      asic_tmp | (use_intra_dc_vlc << ASICPOS_USEINTRADCVLC);
  }
  FLUSHBITS(used_bits);

  if(MB_IS_INTER(mb_number)) {
    status = StrmDec_DecodeMv(dec_container, mb_number);
    if(status != HANTRO_OK)
      return (status);
  }

  *pasic_ctrl = asic_tmp;
  for(i = 0; i < 6; i++) {

    coded_block = (dec_container->StrmStorage.coded_bits[mb_number]
                   & (0x20 >> i)) >> (5 - i);
    if(use_intra_dc_vlc) {
      status =
        StrmDec_DecodeDcCoeff(dec_container, mb_number, i, &dc_coeff);
      if(status != HANTRO_OK)
        return (status);

      if (i < 3)
        dc_tmp0 = (dc_tmp0 << 10) | (dc_coeff & 0x3ff);
      else
        dc_tmp1 = (dc_tmp1 << 10) | (dc_coeff & 0x3ff);

    }
    btmp = !(coded_block & 0x1);
    asic_tmp |= (btmp << asic_pos_no_rlc[i]);
    if(coded_block) {
      status = StrmDec_DecodeVlcBlock(dec_container, mb_number, i);
      if(status != HANTRO_OK)
        return (status);
    }
  }

  *pasic_ctrl = asic_tmp;
  if(use_intra_dc_vlc) {
    SHOWBITS32(input_buffer);
    /*MP4DEC_API_DEBUG(("combined ivop DC %d %d  #\n", dc_tmp0, dc_tmp1)); */
    /* write DC coeffs to interface */
    dec_container->MbSetDesc.
    p_dc_coeff_data_addr[NBR_DC_WORDS_MB * mb_number] = dc_tmp0;
    dec_container->MbSetDesc.
    p_dc_coeff_data_addr[NBR_DC_WORDS_MB * mb_number + 1] = dc_tmp1;
  }

  return (HANTRO_OK);
}
