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

#include "dwl.h"
#include "h264hwd_container.h"

#include "h264hwd_slice_data.h"
#include "h264hwd_util.h"
#include "h264hwd_vlc.h"

#include "h264hwd_exports.h"

/*------------------------------------------------------------------------------

   5.1  Function name: h264bsdDecodeSliceData

        Functional description:
            Decode one slice. Function decodes stream data, i.e. macroblocks
            and possible skip_run fields. h264bsdDecodeMacroblock function is
            called to handle all other macroblock related processing.
            Macroblock to slice group mapping is considered when next
            macroblock to process is determined (h264bsdNextMbAddress function)
            map

        Inputs:
            p_strm_data       pointer to stream data structure
            storage        pointer to storage structure
            currImage       pointer to current processed picture, needed for
                            intra prediction of the macroblocks
            p_slice_header    pointer to slice header of the current slice

        Outputs:
            currImage       processed macroblocks are written to current image
            storage        mbStorage structure of each processed macroblock
                            is updated here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid stream data

------------------------------------------------------------------------------*/

u32 h264bsdDecodeSliceData(decContainer_t * dec_cont, strmData_t * p_strm_data,
                           sliceHeader_t * p_slice_header) {

  /* Variables */

  u32 tmp;
  u32 skip_run;
  u32 prev_skipped;
  u32 curr_mb_addr;
  u32 more_mbs;
  u32 mb_count;
  i32 qp_y;
  macroblockLayer_t *mb_layer;

  storage_t *storage;
  DecAsicBuffers_t *p_asic_buff = NULL;
  sliceStorage_t *slice;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_slice_header);
  ASSERT(dec_cont);

  storage = &dec_cont->storage;
  mb_layer = storage->mb_layer;
  slice = storage->slice;

  p_asic_buff = dec_cont->asic_buff;

  curr_mb_addr = p_slice_header->first_mb_in_slice;

  ASSERT(curr_mb_addr < storage->pic_size_in_mbs);

  skip_run = 0;
  prev_skipped = HANTRO_FALSE;

  /* increment slice index, will be one for decoding of the first slice of
   * the picture */
  slice->slice_id++;

  /* last_mb_addr stores address of the macroblock that was last successfully
   * decoded, needed for error handling */
  slice->last_mb_addr = 0;

  mb_count = 0;
  /* initial quantization parameter for the slice is obtained as the sum of
   * initial QP for the picture and slice_qp_delta for the current slice */
  qp_y = (i32) storage->active_pps->pic_init_qp + p_slice_header->slice_qp_delta;

  do {
    mbStorage_t *mb = storage->mb + curr_mb_addr;

    /* primary picture and already decoded macroblock -> error */
    if(!p_slice_header->redundant_pic_cnt && mb->decoded) {
      ERROR_PRINT("Primary and already decoded");
      return (HANTRO_NOK);
    }

    mb->slice_id = slice->slice_id;

    if(!IS_I_SLICE(p_slice_header->slice_type)) {
      {
        if(!prev_skipped) {
          tmp = h264bsdDecodeExpGolombUnsigned(p_strm_data, &skip_run);
          if(tmp != HANTRO_OK)
            return (tmp);
          if(skip_run == (storage->pic_size_in_mbs << 1) &&
              p_slice_header->frame_num == 0xF) {
            skip_run = MIN(0, storage->pic_size_in_mbs - curr_mb_addr);
          }
          /* skip_run shall be less than or equal to number of
           * macroblocks left */
#ifdef HANTRO_PEDANTIC_MODE
          else if(skip_run > (storage->pic_size_in_mbs - curr_mb_addr)) {
            ERROR_PRINT("skip_run");
            return (HANTRO_NOK);
          }
#endif /* HANTRO_PEDANTIC_MODE */

          if(skip_run) {
            /*mbPred_t *mbPred = &mb_layer->mbPred; */

            prev_skipped = HANTRO_TRUE;
            /*DWLmemset(&mb_layer->mbPred, 0, sizeof(mbPred_t)); */
            /*DWLmemset(mbPred->remIntra4x4PredMode, 0, sizeof(mbPred->remIntra4x4PredMode)); */
            /*DWLmemset(mbPred->refIdxL0, 0, sizeof(mbPred->refIdxL0)); */
            /* mark current macroblock skipped */
            mb_layer->mbType = P_Skip;
          }
        }
      }
    }
    mb_layer->mb_qp_delta = 0;

    {
      if(skip_run) {
        /*DEBUG_PRINT(("Skipping macroblock %d\n", curr_mb_addr)); */
        skip_run--;
      } else {
        prev_skipped = HANTRO_FALSE;
        tmp = h264bsdDecodeMacroblockLayerCavlc(p_strm_data, mb_layer,
                                                mb, p_slice_header);
        if(tmp != HANTRO_OK) {
          ERROR_PRINT("macroblock_layer");
          return (tmp);
        }
      }
    }

    mb_layer->filter_offset_a = p_slice_header->slice_alpha_c0_offset;
    mb_layer->filter_offset_b = p_slice_header->slice_beta_offset;
    mb_layer->disable_deblocking_filter_idc =
      p_slice_header->disable_deblocking_filter_idc;

    p_asic_buff->current_mb = curr_mb_addr;
    tmp = h264bsdDecodeMacroblock(storage, curr_mb_addr, &qp_y, p_asic_buff);

    if(tmp != HANTRO_OK) {
      ERROR_PRINT("MACRO_BLOCK");
      return (tmp);
    }

    /* increment macroblock count only for macroblocks that were decoded
     * for the first time (redundant slices) */
    if(mb->decoded == 1)
      mb_count++;

    {
      /* keep on processing as long as there is stream data left or
       * processing of macroblocks to be skipped based on the last skip_run is
       * not finished */
      more_mbs = (h264bsdMoreRbspData(p_strm_data) ||
                  skip_run) ? HANTRO_TRUE : HANTRO_FALSE;
    }

    /* last_mb_addr is only updated for intra slices (all macroblocks of
     * inter slices will be lost in case of an error) */
    if(IS_I_SLICE(p_slice_header->slice_type))
      slice->last_mb_addr = curr_mb_addr;

    curr_mb_addr = h264bsdNextMbAddress(storage->slice_group_map,
                                        storage->pic_size_in_mbs, curr_mb_addr);
    /* data left in the buffer but no more macroblocks for current slice
     * group -> error */
    if(more_mbs && !curr_mb_addr) {
      ERROR_PRINT("Next mb address");
      return (HANTRO_NOK);
    }

  } while(more_mbs);

  if((slice->num_decoded_mbs + mb_count) > storage->pic_size_in_mbs) {
    ERROR_PRINT("Num decoded mbs");
    return (HANTRO_NOK);
  }

  slice->num_decoded_mbs += mb_count;

  return (HANTRO_OK);

}

/*------------------------------------------------------------------------------

   5.3  Function name: h264bsdMarkSliceCorrupted

        Functional description:
            Mark macroblocks of the slice corrupted. If last_mb_addr in the slice
            storage is set -> picWidhtInMbs (or at least 10) macroblocks back
            from  the last_mb_addr are marked corrupted. However, if last_mb_addr
            is not set -> all macroblocks of the slice are marked.

        Inputs:
            storage        pointer to storage structure
            first_mb_in_slice  address of the first macroblock in the slice, this
                            identifies the slice to be marked corrupted

        Outputs:
            storage        mbStorage for the corrupted macroblocks updated

        Returns:
            none

------------------------------------------------------------------------------*/

void h264bsdMarkSliceCorrupted(storage_t * storage, u32 first_mb_in_slice) {

  /* Variables */

  u32 tmp, i;
  u32 slice_id;
  u32 curr_mb_addr;

  /* Code */

  ASSERT(storage);
  ASSERT(first_mb_in_slice < storage->pic_size_in_mbs);

  curr_mb_addr = first_mb_in_slice;

  slice_id = storage->slice->slice_id;

  /* DecodeSliceData sets last_mb_addr for I slices -> if it was set, go back
   * MAX(pic_width_in_mbs, 10) macroblocks and start marking from there */
  if(storage->slice->last_mb_addr) {
    ASSERT(storage->mb[storage->slice->last_mb_addr].slice_id == slice_id);
    i = storage->slice->last_mb_addr - 1;
    tmp = 0;
    while(i > curr_mb_addr) {
      if(storage->mb[i].slice_id == slice_id) {
        tmp++;
        if(tmp >= MAX(storage->active_sps->pic_width_in_mbs, 10))
          break;
      }
      i--;
    }
    curr_mb_addr = i;
  }

  do {

    if((storage->mb[curr_mb_addr].slice_id == slice_id) &&
        (storage->mb[curr_mb_addr].decoded)) {
      storage->mb[curr_mb_addr].decoded--;
    } else {
      break;
    }

    curr_mb_addr = h264bsdNextMbAddress(storage->slice_group_map,
                                        storage->pic_size_in_mbs, curr_mb_addr);

  } while(curr_mb_addr);

}
