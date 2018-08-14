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

#include "hevc_util.h"
#include "hevc_pic_order_cnt.h"

/* Compute picture order count for a picture. */
void HevcDecodePicOrderCnt(struct PocStorage *poc, u32 max_pic_order_cnt_lsb,
                           const struct SliceHeader *slice_header,
                           const struct NalUnit *nal_unit) {

  i32 pic_order_cnt = 0;

  ASSERT(poc);
  ASSERT(slice_header);
  ASSERT(nal_unit);

  /* set prev_pic_order_cnt values for IDR frame */
  if (IS_IDR_NAL_UNIT(nal_unit)) {
    poc->prev_pic_order_cnt_msb = 0;
    poc->prev_pic_order_cnt_lsb = 0;
  }

  if (poc->pic_order_cnt != INIT_POC) {
    /* compute pic_order_cnt_msb (stored in pic_order_cnt variable) */
    if ((slice_header->pic_order_cnt_lsb < poc->prev_pic_order_cnt_lsb) &&
        ((poc->prev_pic_order_cnt_lsb - slice_header->pic_order_cnt_lsb) >=
         max_pic_order_cnt_lsb / 2)) {
      pic_order_cnt = poc->prev_pic_order_cnt_msb + (i32)max_pic_order_cnt_lsb;
    } else if ((slice_header->pic_order_cnt_lsb >
                poc->prev_pic_order_cnt_lsb) &&
               ((slice_header->pic_order_cnt_lsb -
                 poc->prev_pic_order_cnt_lsb) > max_pic_order_cnt_lsb / 2)) {
      pic_order_cnt = poc->prev_pic_order_cnt_msb - (i32)max_pic_order_cnt_lsb;
    } else
      pic_order_cnt = poc->prev_pic_order_cnt_msb;
  }

  /* standard specifies that prev_pic_order_cnt_msb is from previous
   * rererence frame with temporal_id equal to 0 -> replace old value
   * only if current frame matches */
  if ((nal_unit->nal_unit_type == NAL_CODED_SLICE_TRAIL_R ||
       nal_unit->nal_unit_type == NAL_CODED_SLICE_TSA_R ||
       nal_unit->nal_unit_type == NAL_CODED_SLICE_STSA_R ||
       (nal_unit->nal_unit_type >= NAL_CODED_SLICE_BLA_W_LP &&
        nal_unit->nal_unit_type <= NAL_CODED_SLICE_CRA)) &&
      nal_unit->temporal_id == 0) {
    poc->prev_pic_order_cnt_msb = pic_order_cnt;
    poc->prev_pic_order_cnt_lsb = slice_header->pic_order_cnt_lsb;
  }

  pic_order_cnt += (i32)slice_header->pic_order_cnt_lsb;

  poc->pic_order_cnt = pic_order_cnt;
}
