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

#include "mp4dechwd_container.h"
#include "mp4dechwd_error_conceal.h"
#include "mp4dechwd_utils.h"
#include "mp4debug.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum {
  MASK_BIT0 = 0x1,
  MASK_BIT1 = 0x2
};

enum {
  EC_ABOVE,
  EC_ABOVELEFT,
  EC_ABOVERIGHT,
  EC_NOCANDO
};

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

static void PConcealment(DecContainer * dec_cont, u32 mb_number);

static void IConcealment(DecContainer * dec_cont, u32 mb_number);

static void ITextureConcealment(DecContainer * dec_cont, u32 mb_number);

static void MotionVectorConcealment(DecContainer *, u32);

/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_ErrorConcealment

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 StrmDec_ErrorConcealment(DecContainer * dec_container, u32 start,
                             u32 end) {

  extern const u8 asic_pos_no_rlc[6];
  u32 i, j;

  u32 vop_coding_type = 0;
  u32 control_bits = 0;
  u32 *p_ctrl;

  ASSERT(end <= dec_container->VopDesc.total_mb_in_vop);
  ASSERT(start <= end);

  MP4DEC_API_DEBUG(("ErrorConcealment # %d end \n", end));
  vop_coding_type = dec_container->VopDesc.vop_coding_type;

  /* qp to control word */
  control_bits = (u32) (31 & 0x1F) << ASICPOS_QP;
  /* set block type to inter */
  control_bits |= ((u32) 1 << ASICPOS_MBTYPE);

  control_bits |= ((u32) 1 << ASICPOS_CONCEAL);

  control_bits |= ((u32) 1 << ASICPOS_MBNOTCODED);

  for(j = 0; j < 6; j++) {
    control_bits |= (1 << asic_pos_no_rlc[j]);
  }

  for(i = start; i <= end; i++) {

    /* pointer to control */
    p_ctrl = dec_container->MbSetDesc.p_ctrl_data_addr + i * NBR_OF_WORDS_MB;
    /* video package boundary */
    if((i == dec_container->StrmStorage.vp_mb_number) &&
        (dec_container->StrmStorage.short_video == HANTRO_FALSE)) {
      control_bits |= (1 << ASICPOS_VPBI);
    }

    /* write control bits */
    *p_ctrl = control_bits;

    if( ( (vop_coding_type == PVOP) ||
          (dec_container->StrmStorage.valid_vop_header == HANTRO_FALSE) ) &&
        dec_container->VopDesc.vop_number) {
      PConcealment(dec_container, i);
    } else if(vop_coding_type == IVOP) {
      IConcealment(dec_container, i);
      ITextureConcealment(dec_container, i);
    }

  }

  /* update number of concealed blocks */
  dec_container->StrmStorage.num_err_mbs += end - start + 1;

  return (EC_OK);
}

/*------------------------------------------------------------------------------

   5.2  Function name:PConcealment

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/

static void PConcealment(DecContainer * dec_container, u32 mb_number) {
  MotionVectorConcealment(dec_container, mb_number);

  dec_container->MBDesc[mb_number].error_status |= 0x80;
  dec_container->MBDesc[mb_number].type_of_mb = MB_INTER;

}

/*------------------------------------------------------------------------------

   5.4  Function name:IConcealment;

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/

static void IConcealment(DecContainer * dec_container, u32 mb_number) {

  dec_container->MBDesc[mb_number].error_status |= 0x80;
  dec_container->MBDesc[mb_number].type_of_mb = MB_INTRA;

}

/*------------------------------------------------------------------------------

   5.5  Function name: ITextureConcealment

        Purpose: i-vop texture concealment

        Input:

        Output:

------------------------------------------------------------------------------*/
static void ITextureConcealment(DecContainer * dec_container, u32 mb_number) {

  u32 *p_ctrl;
  u32 tmp = 0;

  /* pointer to last word of control bits of first block */
  p_ctrl = dec_container->MbSetDesc.p_ctrl_data_addr + mb_number * NBR_OF_WORDS_MB;

  tmp = (((u32) 1 << ASICPOS_ACPREDFLAG) | ((u32) 1 << ASICPOS_MBTYPE));

  *p_ctrl &= ~(tmp);

  dec_container->MBDesc[mb_number].error_status |= 0x80;

}

/*------------------------------------------------------------------------------

   5.6  Function name: MotionVectorConcealment

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
static void MotionVectorConcealment(DecContainer * dec_container,
                                    u32 mb_number) {

  *(dec_container->MbSetDesc.p_mv_data_addr + mb_number*NBR_MV_WORDS_MB) = 0;
  *(dec_container->MbSetDesc.p_mv_data_addr + mb_number*NBR_MV_WORDS_MB + 1) = 0;
  *(dec_container->MbSetDesc.p_mv_data_addr + mb_number*NBR_MV_WORDS_MB + 2) = 0;
  *(dec_container->MbSetDesc.p_mv_data_addr + mb_number*NBR_MV_WORDS_MB + 3) = 0;

}
