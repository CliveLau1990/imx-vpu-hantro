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

#include "h264hwd_util.h"
#include "h264hwd_pic_order_cnt.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function: h264bsdDecodePicOrderCnt

        Functional description:
            Compute picture order count for a picture. Function implements
            computation of all POC types (0, 1 and 2), type is obtained from
            sps. See standard for description of the POC types and how POC is
            computed for each type.

            Function returns the minimum of top field and bottom field pic
            order counts.

        Inputs:
            poc         pointer to previous results
            sps         pointer to sequence parameter set
            slicHeader  pointer to current slice header, frame number and
                        other params needed for POC computation
            p_nal_unit    pointer to current NAL unit structrue, function needs
                        to know if this is an IDR picture and also if this is
                        a reference picture

        Outputs:
            poc         results stored here for computation of next POC

        Returns:
            picture order count

------------------------------------------------------------------------------*/

void h264bsdDecodePicOrderCnt(pocStorage_t *poc, const seqParamSet_t *sps,
                              const sliceHeader_t *p_slice_header, const nalUnit_t *p_nal_unit) {

  /* Variables */

  u32 i;
  i32 pic_order_cnt;
  u32 frame_num_offset, abs_frame_num, pic_order_cnt_cycle_cnt;
  u32 frame_num_in_pic_order_cnt_cycle;
  i32 expected_delta_pic_order_cnt_cycle;

  /* Code */

  ASSERT(poc);
  ASSERT(sps);
  ASSERT(p_slice_header);
  ASSERT(p_nal_unit);
  ASSERT(sps->pic_order_cnt_type <= 2);

#if 0
  /* JanSa: I don't think this is necessary, don't see any reason to
   * increment prev_frame_num one by one instead of one big increment.
   * However, standard specifies that this should be done -> if someone
   * figures out any case when the outcome would be different for step by
   * step increment, this part of the code should be enabled */

  /* if there was a gap in frame numbering and pic_order_cnt_type is 1 or 2 ->
   * "compute" pic order counts for non-existing frames. These are not
   * actually computed, but process needs to be done to update the
   * prev_frame_num and prev_frame_num_offset */
  if ( sps->pic_order_cnt_type > 0 &&
       p_slice_header->frame_num != poc->prev_frame_num &&
       p_slice_header->frame_num != ((poc->prev_frame_num + 1) % sps->max_frame_num)) {

    /* use variable i for unUsedShortTermFrameNum */
    i = (poc->prev_frame_num + 1) % sps->max_frame_num;

    do {
      if (poc->prev_frame_num > i)
        frame_num_offset = poc->prev_frame_num_offset + sps->max_frame_num;
      else
        frame_num_offset = poc->prev_frame_num_offset;

      poc->prev_frame_num_offset = frame_num_offset;
      poc->prev_frame_num = i;

      i = (i + 1) % sps->max_frame_num;

    } while (i != p_slice_header->frame_num);
  }
#endif

  /* check if current slice includes mmco equal to 5 */
  poc->contains_mmco5 = HANTRO_FALSE;
  if (p_slice_header->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag) {
    i = 0;
    while (p_slice_header->dec_ref_pic_marking.operation[i].
           memory_management_control_operation) {
      if (p_slice_header->dec_ref_pic_marking.operation[i].
          memory_management_control_operation == 5) {
        poc->contains_mmco5 = HANTRO_TRUE;
        break;
      }
      i++;
    }
  }
  switch (sps->pic_order_cnt_type) {

  case 0:
    /* set prevPicOrderCnt values for IDR frame */
    if (IS_IDR_NAL_UNIT(p_nal_unit)) {
      poc->prev_pic_order_cnt_msb = 0;
      poc->prev_pic_order_cnt_lsb = 0;
    }

    /* compute picOrderCntMsb (stored in pic_order_cnt variable) */
    if ( (p_slice_header->pic_order_cnt_lsb < poc->prev_pic_order_cnt_lsb) &&
         ((poc->prev_pic_order_cnt_lsb - p_slice_header->pic_order_cnt_lsb) >=
          sps->max_pic_order_cnt_lsb/2) ) {
      pic_order_cnt = poc->prev_pic_order_cnt_msb +
                      (i32)sps->max_pic_order_cnt_lsb;
    } else if ((p_slice_header->pic_order_cnt_lsb > poc->prev_pic_order_cnt_lsb) &&
               ((p_slice_header->pic_order_cnt_lsb - poc->prev_pic_order_cnt_lsb) >
                sps->max_pic_order_cnt_lsb/2) ) {
      pic_order_cnt = poc->prev_pic_order_cnt_msb -
                      (i32)sps->max_pic_order_cnt_lsb;
    } else
      pic_order_cnt = poc->prev_pic_order_cnt_msb;

    /* standard specifies that prev_pic_order_cnt_msb is from previous
     * rererence frame -> replace old value only if current frame is
     * rererence frame */
    if (p_nal_unit->nal_ref_idc)
      poc->prev_pic_order_cnt_msb = pic_order_cnt;

    /* compute top field order cnt (stored in pic_order_cnt) */
    pic_order_cnt += (i32)p_slice_header->pic_order_cnt_lsb;

    /* standard specifies that prev_pic_order_cnt_lsb is from previous
     * rererence frame -> replace old value only if current frame is
     * rererence frame */
    if (p_nal_unit->nal_ref_idc) {
      /* if current frame contains mmco5 -> modify values to be
       * stored */
      if (poc->contains_mmco5) {
        poc->prev_pic_order_cnt_msb = 0;
        /* prev_pic_order_cnt_lsb should be the top field pic_order_cnt
         * if previous frame included mmco5. Top field pic_order_cnt
         * for frames containing mmco5 is obtained by subtracting
         * the pic_order_cnt from original top field order count ->
         * value is zero if top field was the minimum, i.e. delta
         * for bottom was positive, otherwise value is
         * -delta_pic_order_cnt_bottom */
        if (p_slice_header->delta_pic_order_cnt_bottom < 0 &&
            !p_slice_header->bottom_field_flag)
          poc->prev_pic_order_cnt_lsb =
            (u32)(-p_slice_header->delta_pic_order_cnt_bottom);
        else
          poc->prev_pic_order_cnt_lsb = 0;
        /*pic_order_cnt = poc->prev_pic_order_cnt_lsb;*/
      } else {
        poc->prev_pic_order_cnt_lsb = p_slice_header->pic_order_cnt_lsb;
      }
    }

    /*if (!p_slice_header->field_pic_flag || !p_slice_header->bottom_field_flag)*/
    poc->pic_order_cnt[0] = pic_order_cnt;

    if (!p_slice_header->field_pic_flag)
      poc->pic_order_cnt[1] = pic_order_cnt +
                              p_slice_header->delta_pic_order_cnt_bottom;
    else
      /*else if (p_slice_header->bottom_field_flag)*/
      poc->pic_order_cnt[1] = pic_order_cnt;


    break;

  case 1:

    /* step 1 (in the description in the standard) */
    if (IS_IDR_NAL_UNIT(p_nal_unit))
      frame_num_offset = 0;
    else if (poc->prev_frame_num > p_slice_header->frame_num)
      frame_num_offset = poc->prev_frame_num_offset + sps->max_frame_num;
    else
      frame_num_offset = poc->prev_frame_num_offset;

    /* step 2 */
    if (sps->num_ref_frames_in_pic_order_cnt_cycle)
      abs_frame_num = frame_num_offset + p_slice_header->frame_num;
    else
      abs_frame_num = 0;

    if (p_nal_unit->nal_ref_idc == 0 && abs_frame_num > 0)
      abs_frame_num -= 1;

    /* step 4 */
    expected_delta_pic_order_cnt_cycle = 0;
    for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
      expected_delta_pic_order_cnt_cycle += sps->offset_for_ref_frame[i];

    /* step 3 */
    if (abs_frame_num > 0) {
      pic_order_cnt_cycle_cnt =
        (abs_frame_num - 1)/sps->num_ref_frames_in_pic_order_cnt_cycle;
      frame_num_in_pic_order_cnt_cycle =
        (abs_frame_num - 1)%sps->num_ref_frames_in_pic_order_cnt_cycle;

      /* step 5 (pic_order_cnt used to store expectedPicOrderCnt) */
      pic_order_cnt =
        (i32)pic_order_cnt_cycle_cnt * expected_delta_pic_order_cnt_cycle;
      for (i = 0; i <= frame_num_in_pic_order_cnt_cycle; i++)
        pic_order_cnt += sps->offset_for_ref_frame[i];
    } else
      pic_order_cnt = 0;

    if (p_nal_unit->nal_ref_idc == 0)
      pic_order_cnt += sps->offset_for_non_ref_pic;


    /* if current picture contains mmco5 -> set prev_frame_num_offset and
     * prev_frame_num to 0 for computation of pic_order_cnt of next
     * frame, otherwise store frame_num and frame_num_offset to poc
     * structure */
    if (!poc->contains_mmco5) {
      poc->prev_frame_num_offset = frame_num_offset;
      poc->prev_frame_num = p_slice_header->frame_num;
    } else {
      poc->prev_frame_num_offset = 0;
      poc->prev_frame_num = 0;
      pic_order_cnt = 0;
    }

    /* step 6 */

    if (!p_slice_header->field_pic_flag) {
      poc->pic_order_cnt[0] = pic_order_cnt +
                              p_slice_header->delta_pic_order_cnt[0];
      poc->pic_order_cnt[1] = poc->pic_order_cnt[0] +
                              sps->offset_for_top_to_bottom_field +
                              p_slice_header->delta_pic_order_cnt[1];
    } else if (!p_slice_header->bottom_field_flag)
      poc->pic_order_cnt[0] = poc->pic_order_cnt[1] =
                                pic_order_cnt + p_slice_header->delta_pic_order_cnt[0];
    else
      poc->pic_order_cnt[0] = poc->pic_order_cnt[1] =
                                pic_order_cnt + sps->offset_for_top_to_bottom_field +
                                p_slice_header->delta_pic_order_cnt[0];

    break;

  default: /* case 2 */
    /* derive frame_num_offset */
    if (IS_IDR_NAL_UNIT(p_nal_unit))
      frame_num_offset = 0;
    else if (poc->prev_frame_num > p_slice_header->frame_num)
      frame_num_offset = poc->prev_frame_num_offset + sps->max_frame_num;
    else
      frame_num_offset = poc->prev_frame_num_offset;

    /* derive pic_order_cnt (type 2 has same value for top and bottom
     * field order cnts) */
    if (IS_IDR_NAL_UNIT(p_nal_unit)) {
      pic_order_cnt = 0;
    } else if (p_nal_unit->nal_ref_idc == 0) {
      pic_order_cnt =
        2 * (i32)(frame_num_offset + p_slice_header->frame_num) - 1;
    } else {
      pic_order_cnt =
        2 * (i32)(frame_num_offset + p_slice_header->frame_num);
    }

    poc->pic_order_cnt[0] = poc->pic_order_cnt[1] = pic_order_cnt;

    /*
    if (!p_slice_header->field_pic_flag)
    {
        poc->pic_order_cnt[0] = poc->pic_order_cnt[1] = pic_order_cnt;
    }
    else if (!p_slice_header->bottom_field_flag)
        poc->pic_order_cnt[0] = pic_order_cnt;
    else
        poc->pic_order_cnt[1] = pic_order_cnt;
    */

    /* if current picture contains mmco5 -> set prev_frame_num_offset and
     * prev_frame_num to 0 for computation of pic_order_cnt of next
     * frame, otherwise store frame_num and frame_num_offset to poc
     * structure */
    if (!poc->contains_mmco5) {
      poc->prev_frame_num_offset = frame_num_offset;
      poc->prev_frame_num = p_slice_header->frame_num;
    } else {
      poc->prev_frame_num_offset = 0;
      poc->prev_frame_num = 0;
      pic_order_cnt = 0;
    }
    break;

  }

}
