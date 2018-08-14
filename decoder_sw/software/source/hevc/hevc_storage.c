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

#include "hevc_storage.h"
#include "hevc_dpb.h"
#include "hevc_nal_unit.h"
#include "hevc_slice_header.h"
#include "hevc_seq_param_set.h"
#include "hevc_util.h"
#include "hevc_vui.h"
#include "dwl.h"
#include "deccfg.h"
#include "hevc_container.h"

/* Initializes storage structure. Sets contents of the storage to '0' except
 * for the active parameter set ids, which are initialized to invalid values.
 * */
void HevcInitStorage(struct Storage *storage) {

  ASSERT(storage);

  (void)DWLmemset(storage, 0, sizeof(struct Storage));

  storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
  storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;
  storage->active_vps_id = MAX_NUM_VIDEO_PARAM_SETS;
  storage->old_sps_id = MAX_NUM_SEQ_PARAM_SETS;
  storage->aub->first_call_flag = HANTRO_TRUE;
  storage->poc->pic_order_cnt = INIT_POC;
}

/* Store sequence parameter set into the storage. If active SPS is overwritten
 * -> check if contents changes and if it does, set parameters to force
 *  reactivation of parameter sets. */
u32 HevcStoreSeqParamSet(struct Storage *storage,
                         struct SeqParamSet *seq_param_set) {

  u32 id;

  ASSERT(storage);
  ASSERT(seq_param_set);
  ASSERT(seq_param_set->seq_parameter_set_id < MAX_NUM_SEQ_PARAM_SETS);

  id = seq_param_set->seq_parameter_set_id;

  /* seq parameter set with id not used before -> allocate memory */
  if (storage->sps[id] == NULL) {
    ALLOCATE(storage->sps[id], 1, struct SeqParamSet);
    if (storage->sps[id] == NULL) return (MEMORY_ALLOCATION_ERROR);
  }
  /* sequence parameter set with id equal to id of active sps */
  else if (id == storage->active_sps_id) {
    /* TODO: if seq parameter set contents changes
     *    -> overwrite and re-activate when next IDR picture decoded
     *    ids of active param sets set to invalid values to force
     *    re-activation. Memories allocated for old sps freed
     * otherwise free memeries allocated for just decoded sps and
     * continue */
    if (HevcCompareSeqParamSets(seq_param_set, storage->active_sps) == 0 /*&&
        (seq_param_set->pic_width != storage->active_sps->pic_width ||
        seq_param_set->pic_height != storage->active_sps->pic_height)*/) {
      storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS + 1;
      storage->active_sps = NULL;
      storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS + 1;
      storage->active_pps = NULL;
    } else {
      return (HANTRO_OK);
    }
  }

  *storage->sps[id] = *seq_param_set;

  return (HANTRO_OK);
}

/* Store picture parameter set into the storage. If active PPS is overwritten
 * -> check if active SPS changes and if it does -> set parameters to force
 *  reactivation of parameter sets. */
u32 HevcStorePicParamSet(struct Storage *storage,
                         struct PicParamSet *pic_param_set) {

  u32 id;

  ASSERT(storage);
  ASSERT(pic_param_set);
  ASSERT(pic_param_set->pic_parameter_set_id < MAX_NUM_PIC_PARAM_SETS);
  ASSERT(pic_param_set->seq_parameter_set_id < MAX_NUM_SEQ_PARAM_SETS);

  id = pic_param_set->pic_parameter_set_id;

  /* pic parameter set with id not used before -> allocate memory */
  if (storage->pps[id] == NULL) {
    ALLOCATE(storage->pps[id], 1, struct PicParamSet);
    if (storage->pps[id] == NULL) return (MEMORY_ALLOCATION_ERROR);
  }
  /* picture parameter set with id equal to id of active pps */
  else if (id == storage->active_pps_id) {
    /* check whether seq param set changes, force re-activation of
     * param set if it does. Set active_sps_id to invalid value to
     * accomplish this */
    if (pic_param_set->seq_parameter_set_id != storage->active_sps_id) {
      storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS + 1;
    }
  }
  /* overwrite pic param set other than active one -> free memories
   * allocated for old param set */
  else {
  }

  *storage->pps[id] = *pic_param_set;

  return (HANTRO_OK);
}

/* Store video parameter set into the storage. If active VPS is overwritten
 * -> check if active VPS changes and if it does -> set parameters to force
 *  reactivation of parameter sets. */
u32 HevcStoreVideoParamSet(struct Storage *storage,
                           struct VideoParamSet *video_param_set) {

  u32 id;

  ASSERT(storage);
  ASSERT(video_param_set);
  ASSERT(video_param_set->vps_video_parameter_set_id < MAX_NUM_VIDEO_PARAM_SETS);

  id = video_param_set->vps_video_parameter_set_id;

  /* video parameter set with id not used before -> allocate memory */
  if (storage->vps[id] == NULL) {
    ALLOCATE(storage->vps[id], 1, struct VideoParamSet);
    if (storage->vps[id] == NULL) return (MEMORY_ALLOCATION_ERROR);
  }
  /*video parameter set with id equal to id of active vps */
  else if (id == storage->active_vps_id) {
    /* check whether video param set changes, force re-activation of
     * param set if it does. Set active_vps_id to invalid value to
     * accomplish this */
    if (HevcCompareVideoParamSets(video_param_set, storage->active_vps) == 0) {
      storage->active_vps_id = MAX_NUM_VIDEO_PARAM_SETS + 1;
      storage->active_vps = NULL;
      storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS + 1;
      storage->active_sps = NULL;
      storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS + 1;
      storage->active_pps = NULL;
    } else {
      return (HANTRO_OK);
    }
  }
  *storage->vps[id] = *video_param_set;

  return (HANTRO_OK);
}


/* Activate certain SPS/PPS combination. This function shall be called in
 * the beginning of each picture. Picture parameter set can be changed as
 * wanted, but sequence parameter set may only be changed when the starting
 * picture is an IDR picture. */
u32 HevcActivateParamSets(struct Storage *storage, u32 pps_id, u32 is_idr) {

  ASSERT(storage);
  ASSERT(pps_id < MAX_NUM_PIC_PARAM_SETS);

  /* check that pps and corresponding sps exist */
  if ((storage->pps[pps_id] == NULL) ||
      (storage->sps[storage->pps[pps_id]->seq_parameter_set_id] == NULL) ||
      (storage->vps[storage->sps[storage->pps[pps_id]->seq_parameter_set_id]->vps_id] == NULL)) {
    return (HANTRO_NOK);
  }

  /* first activation */
  if (storage->active_pps_id == MAX_NUM_PIC_PARAM_SETS) {
    storage->active_pps_id = pps_id;
    storage->active_pps = storage->pps[pps_id];
    storage->active_sps_id = storage->active_pps->seq_parameter_set_id;
    storage->active_sps = storage->sps[storage->active_sps_id];
    storage->active_vps_id = storage->sps[storage->active_pps->seq_parameter_set_id]->vps_id;
    storage->active_vps = storage->vps[storage->active_vps_id];
  } else if (pps_id != storage->active_pps_id) {
    /* sequence parameter set shall not change but before an IDR picture */
    if (storage->pps[pps_id]->seq_parameter_set_id != storage->active_sps_id ||
        storage->sps[storage->pps[pps_id]->seq_parameter_set_id]->vps_id != storage->active_vps_id) {
      DEBUG_PRINT(("SEQ PARAM SET CHANGING...\n"));
      if (is_idr) {
        storage->active_pps_id = pps_id;
        storage->active_pps = storage->pps[pps_id];
        storage->active_sps_id = storage->active_pps->seq_parameter_set_id;
        storage->active_sps = storage->sps[storage->active_sps_id];
        storage->active_vps_id = storage->sps[storage->active_pps->seq_parameter_set_id]->vps_id;
        storage->active_vps = storage->vps[storage->active_vps_id];
      } else {
        DEBUG_PRINT(("TRYING TO CHANGE SPS IN NON-IDR SLICE\n"));
        return (HANTRO_NOK);
      }
    } else {
      storage->active_pps_id = pps_id;
      storage->active_pps = storage->pps[pps_id];
    }
  }

  return (HANTRO_OK);
}

/* Reset contents of the storage. This should be called before processing of
 * new image is started. */
void HevcResetStorage(struct Storage *storage) {

  ASSERT(storage);

#ifdef FFWD_WORKAROUND
  storage->prev_idr_pic_ready = HANTRO_FALSE;
#endif /* FFWD_WORKAROUND */
}

/* Determine if the decoder is in the start of a picture. This information is
 * needed to decide if HevcActivateParamSets function should be called.
 * Function considers that new picture is starting if no slice headers have
 * been successfully decoded for the current access unit. */
u32 HevcIsStartOfPicture(struct Storage *storage) {

  if (storage->valid_slice_in_access_unit == HANTRO_FALSE)
    return (HANTRO_TRUE);
  else
    return (HANTRO_FALSE);
}

/* Check if next NAL unit starts a new access unit. */
u32 HevcCheckAccessUnitBoundary(struct StrmData *strm, struct NalUnit *nu_next,
                                struct Storage *storage,
                                u32 *access_unit_boundary_flag) {

  ASSERT(strm);
  ASSERT(nu_next);
  ASSERT(storage);

  DEBUG_PRINT(("HevcCheckAccessUnitBoundary #\n"));
  /* initialize default output to HANTRO_FALSE */
  *access_unit_boundary_flag = HANTRO_FALSE;

  if (nu_next->nal_unit_type == NAL_END_OF_SEQUENCE)
    storage->no_rasl_output = 1;
  else if (nu_next->nal_unit_type < NAL_CODED_SLICE_CRA)
    storage->no_rasl_output = 0;

  if (nu_next->nal_unit_type == NAL_ACCESS_UNIT_DELIMITER ||
      nu_next->nal_unit_type == NAL_VIDEO_PARAM_SET ||
      nu_next->nal_unit_type == NAL_SEQ_PARAM_SET ||
      nu_next->nal_unit_type == NAL_PIC_PARAM_SET ||
      nu_next->nal_unit_type == NAL_PREFIX_SEI ||
      (nu_next->nal_unit_type >= 41 && nu_next->nal_unit_type <= 44)) {
    *access_unit_boundary_flag = HANTRO_TRUE;
    return (HANTRO_OK);
  } else if (!IS_SLICE_NAL_UNIT(nu_next)) {
    return (HANTRO_OK);
  }

  /* check if this is the very first call to this function */
  if (storage->aub->first_call_flag) {
    *access_unit_boundary_flag = HANTRO_TRUE;
    storage->aub->first_call_flag = HANTRO_FALSE;
  }

  if (SwShowBits(strm, 1)) *access_unit_boundary_flag = HANTRO_TRUE;

  *storage->aub->nu_prev = *nu_next;

  return (HANTRO_OK);
}

/* Check if any valid SPS/PPS combination exists in the storage.  Function
 * tries each PPS in the buffer and checks if corresponding SPS exists. */
u32 HevcValidParamSets(struct Storage *storage) {

  u32 i;

  ASSERT(storage);

  for (i = 0; i < MAX_NUM_PIC_PARAM_SETS; i++) {
    if (storage->pps[i] &&
        storage->sps[storage->pps[i]->seq_parameter_set_id]) {
      return (HANTRO_OK);
    }
  }

  return (HANTRO_NOK);
}

void HevcClearStorage(struct Storage *storage) {

  /* Variables */

  /* Code */
  u32 i;
  ASSERT(storage);

  HevcResetStorage(storage);

#ifdef CLEAR_HDRINFO_IN_SEEK
  storage->old_sps_id = MAX_NUM_SEQ_PARAM_SETS;
  storage->active_sps_id = MAX_NUM_SEQ_PARAM_SETS;
  storage->active_pps_id = MAX_NUM_PIC_PARAM_SETS;
  storage->active_sps = NULL;
  storage->active_pps = NULL;

  for (i = 0; i < MAX_NUM_VIDEO_PARAM_SETS; i++) {
    if (storage->vps[i]) {
      FREE(storage->vps[i]);
    }
  }

  for (i = 0; i < MAX_NUM_SEQ_PARAM_SETS; i++) {
    if (storage->sps[i]) {
      FREE(storage->sps[i]);
    }
  }

  for (i = 0; i < MAX_NUM_PIC_PARAM_SETS; i++) {
    if (storage->pps[i]) {
      FREE(storage->pps[i]);
    }
  }

  (void)DWLmemset(&storage->sei_param, 0, sizeof(struct SEIParameters));
#endif
  storage->sei_param.bufperiod_present_flag = 0;
  storage->sei_param.pictiming_present_flag = 0;
  storage->sei_param.mastering_display_present_flag = 0;
  storage->sei_param.lightlevel_present_flag = 0;
  storage->sei_param.bumping_flag = 0;
  storage->frame_rate = 0;
  storage->pic_started = HANTRO_FALSE;
  storage->valid_slice_in_access_unit = HANTRO_FALSE;
  storage->checked_aub = 0;
  storage->prev_buf_not_finished = HANTRO_FALSE;
  storage->prev_buf_pointer = NULL;
  storage->prev_bytes_consumed = 0;
  storage->picture_broken = 0;
  storage->poc_last_display = INIT_POC;
  storage->pending_out_pic = NULL;
  storage->no_rasl_output = 0;

  (void)DWLmemset(&storage->poc, 0, 2 * sizeof(struct PocStorage));
  (void)DWLmemset(&storage->aub, 0, sizeof(struct AubCheck));
  (void)DWLmemset(&storage->curr_image, 0, sizeof(struct Image));
  (void)DWLmemset(&storage->prev_nal_unit, 0, sizeof(struct NalUnit));
  (void)DWLmemset(&storage->slice_header, 0, 2 * sizeof(struct SliceHeader));
  (void)DWLmemset(&storage->strm, 0, sizeof(struct StrmData));

  (void)i;
}

/* Allocates SW resources after parameter set activation. */
#ifndef USE_EXTERNAL_BUFFER
u32 HevcAllocateSwResources(const void *dwl, struct Storage *storage) {
  u32 tmp;
  const struct SeqParamSet *sps = storage->active_sps;
  struct DpbInitParams dpb_params;
  u32 pixel_width = (sps->bit_depth_luma == 8 && sps->bit_depth_chroma == 8) ? 8 : 10;
  u32 rs_pixel_width = (storage->use_8bits_output || pixel_width == 8) ? 8 :
                       (storage->use_p010_output ? 16 : 10);

  /* TODO: next cb or ctb multiple? */
  storage->pic_width_in_cbs = sps->pic_width >> sps->log_min_coding_block_size;
  storage->pic_height_in_cbs =
    sps->pic_height >> sps->log_min_coding_block_size;
  storage->pic_width_in_ctbs =
    (sps->pic_width + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;
  storage->pic_height_in_ctbs =
    (sps->pic_height + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;

  storage->pic_size = sps->pic_width * sps->pic_height * pixel_width / 8;
  storage->curr_image->width = sps->pic_width;
  storage->curr_image->height = sps->pic_height;

  /* dpb output reordering disabled if
   * 1) application set no_reordering flag
   * 2) num_reorder_frames equal to 0 */
  if (storage->no_reordering)
    dpb_params.no_reordering = HANTRO_TRUE;
  else
    dpb_params.no_reordering = HANTRO_FALSE;

  dpb_params.dpb_size = sps->max_dpb_size;
  dpb_params.pic_size = storage->pic_size;
  dpb_params.n_extra_frm_buffers = storage->n_extra_frm_buffers;

  /* buffer size of dpb pic = pic_size + dir_mv_size */
  storage->dmv_mem_size =
    /* num ctbs */
    (storage->pic_width_in_ctbs * storage->pic_height_in_ctbs *
     /* num 16x16 blocks in ctbs */
     (1 << (2 * (sps->log_max_coding_block_size - 4))) *
     /* num bytes per 16x16 */
     16);
  dpb_params.buff_size = NEXT_MULTIPLE(storage->pic_size * 3 / 2, 16) + storage->dmv_mem_size;

  if (storage->use_video_compressor) {
    u32 pic_width_in_cbsy, pic_height_in_cbsy;
    u32 pic_width_in_cbsc, pic_height_in_cbsc;
    pic_width_in_cbsy = ((sps->pic_width + 8 - 1)/8);
    pic_width_in_cbsy = NEXT_MULTIPLE(pic_width_in_cbsy, 16);
    pic_width_in_cbsc = ((sps->pic_width + 16 - 1)/16);
    pic_width_in_cbsc = NEXT_MULTIPLE(pic_width_in_cbsc, 16);
    pic_height_in_cbsy = (sps->pic_height + 8 - 1)/8;
    pic_height_in_cbsc = (sps->pic_height/2 + 4 - 1)/4;

    /* luma table size */
    dpb_params.buff_size += NEXT_MULTIPLE(pic_width_in_cbsy * pic_height_in_cbsy, 16);
    /* chroma table size */
    dpb_params.buff_size += NEXT_MULTIPLE(pic_width_in_cbsc * pic_height_in_cbsc, 16);

    dpb_params.tbl_sizey = NEXT_MULTIPLE(pic_width_in_cbsy * pic_height_in_cbsy, 16);
    dpb_params.tbl_sizec = NEXT_MULTIPLE(pic_width_in_cbsc * pic_height_in_cbsc, 16);
  } else
    dpb_params.tbl_sizey = dpb_params.tbl_sizec = 0;

  /* note that calling ResetDpb here results in losing all
   * pictures currently in DPB -> nothing will be output from
   * the buffer even if no_output_of_prior_pics_flag is HANTRO_FALSE */

  tmp = HevcResetDpb(dwl, storage->dpb, &dpb_params);
  if (tmp != HANTRO_OK) return (tmp);

  storage->dpb->pic_width = sps->pic_width;
  storage->dpb->pic_height = sps->pic_height;
  storage->dpb->bit_depth_luma = sps->bit_depth_luma;
  storage->dpb->bit_depth_chroma = sps->bit_depth_chroma;

  /* Allocate raster resources. */
  if ((storage->raster_enabled || storage->down_scale_enabled) && storage->dpb->dpb_reset) {
    struct RasterBufferParams params;
    struct DWLLinearMem buffers[storage->dpb->tot_buffers];
    if (storage->raster_buffer_mgr) RbmRelease(storage->raster_buffer_mgr);
    params.dwl = dwl;
    params.num_buffers = storage->dpb->tot_buffers;
    for (int i = 0; i < params.num_buffers; i++)
      buffers[i] = storage->dpb[0].pic_buffers[i];
    params.tiled_buffers = buffers;
    params.width = 0;
    params.height = 0;
    /* Raster out writes to modulo-16 widths. */
    if (storage->raster_enabled) {
      params.width = NEXT_MULTIPLE(sps->pic_width * rs_pixel_width, 16 * 8) / 8;
      params.height = sps->pic_height;
    }
    if (storage->down_scale_enabled) {
      params.width = NEXT_MULTIPLE((sps->pic_width >> storage->down_scale_x_shift) * rs_pixel_width, 16 * 8) / 8;
      params.height = sps->pic_height >> storage->down_scale_y_shift;
    }
    storage->raster_buffer_mgr = RbmInit(params);
    if (storage->raster_buffer_mgr == NULL) return HANTRO_NOK;
  }

  {
    struct HevcCropParams *crop = &storage->dpb->crop_params;
    const struct SeqParamSet *sps = storage->active_sps;

    if (sps->pic_cropping_flag) {
      u32 tmp1 = 2, tmp2 = 1;

      crop->crop_left_offset = tmp1 * sps->pic_crop_left_offset;
      crop->crop_out_width =
        sps->pic_width -
        tmp1 * (sps->pic_crop_left_offset + sps->pic_crop_right_offset);

      crop->crop_top_offset = tmp1 * tmp2 * sps->pic_crop_top_offset;
      crop->crop_out_height =
        sps->pic_height - tmp1 * tmp2 * (sps->pic_crop_top_offset +
                                         sps->pic_crop_bottom_offset);
    } else {
      crop->crop_left_offset = 0;
      crop->crop_top_offset = 0;
      crop->crop_out_width = sps->pic_width;
      crop->crop_out_height = sps->pic_height;
    }
  }

  return HANTRO_OK;
}
#else
u32 IsExternalBuffersRealloc(void *dec_inst, struct Storage *storage) {
  const struct SeqParamSet *sps = storage->active_sps;
  struct HevcDecContainer *dec_cont = (struct HevcDecContainer *)dec_inst;
  u32 pixel_width = (sps->bit_depth_luma == 8 && sps->bit_depth_chroma == 8) ? 8 : 10;
  u32 rs_pixel_width = storage->use_8bits_output ? 8 :
                       pixel_width == 10 ? (storage->use_p010_output ? 16 : 10) : 8;
  u32 buff_size, rs_buff_size, ds_buff_size;
  //u32 is_frame_buffer;

  /* TODO: next cb or ctb multiple? */
  u32 pic_width_in_ctbs =
    (sps->pic_width + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;
  u32 pic_height_in_ctbs =
    (sps->pic_height + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;

  u32 pic_size = sps->pic_width * sps->pic_height * pixel_width / 8;

  u32 n_extra_frm_buffers = storage->n_extra_frm_buffers;

  /* buffer size of dpb pic = pic_size + dir_mv_size + tbl_size */
  u32 dmv_mem_size =
    /* num ctbs */
    (pic_width_in_ctbs * pic_height_in_ctbs *
     /* num 16x16 blocks in ctbs */
     (1 << (2 * (sps->log_max_coding_block_size - 4))) *
     /* num bytes per 16x16 */
     16);
  u32 ref_buff_size = NEXT_MULTIPLE(pic_size * 3 / 2, 16) + dmv_mem_size;
  u32 dpb_size = sps->max_dpb_size + 1;
  u32 min_buffer_num;
  enum BufferType buf_type;

  if (storage->use_video_compressor) {
    u32 pic_width_in_cbsy, pic_height_in_cbsy;
    u32 pic_width_in_cbsc, pic_height_in_cbsc;
    pic_width_in_cbsy = ((sps->pic_width + 8 - 1)/8);
    pic_width_in_cbsy = NEXT_MULTIPLE(pic_width_in_cbsy, 16);
    pic_width_in_cbsc = ((sps->pic_width + 16 - 1)/16);
    pic_width_in_cbsc = NEXT_MULTIPLE(pic_width_in_cbsc, 16);
    pic_height_in_cbsy = (sps->pic_height + 8 - 1)/8;
    pic_height_in_cbsc = (sps->pic_height/2 + 4 - 1)/4;

    /* luma table size */
    ref_buff_size += NEXT_MULTIPLE(pic_width_in_cbsy * pic_height_in_cbsy, 16);
    /* chroma table size */
    ref_buff_size += NEXT_MULTIPLE(pic_width_in_cbsc * pic_height_in_cbsc, 16);
  }

  rs_buff_size = NEXT_MULTIPLE(sps->pic_width * rs_pixel_width, 16 * 8) / 8 * sps->pic_height * 3 / 2;
  ds_buff_size = NEXT_MULTIPLE((sps->pic_width >> storage->down_scale_x_shift) * rs_pixel_width, 16 * 8) / 8
                 * (sps->pic_height >> storage->down_scale_y_shift) * 3 / 2;

  u32 tot_buffers = dpb_size + 2 + n_extra_frm_buffers;
  if (tot_buffers > MAX_FRAME_BUFFER_NUMBER)
    tot_buffers = MAX_FRAME_BUFFER_NUMBER;
  if (IS_EXTERNAL_BUFFER(dec_cont->ext_buffer_config, REFERENCE_BUFFER)) {
    min_buffer_num = dpb_size + 2;   /* We need at least (dpb_size+2) output buffers before starting decoding. */
    buff_size = ref_buff_size;
    buf_type = REFERENCE_BUFFER;
    //is_frame_buffer = 1;
  } else {
    min_buffer_num = dpb_size + 1;
    //is_frame_buffer = 0;
    if (IS_EXTERNAL_BUFFER(dec_cont->ext_buffer_config, DOWNSCALE_OUT_BUFFER)) {
      buff_size = ds_buff_size;
      buf_type = DOWNSCALE_OUT_BUFFER;
    } else
    {
      buff_size = rs_buff_size;
      buf_type = RASTERSCAN_OUT_BUFFER;
    }
  }

  if (buff_size <= dec_cont->ext_buffer_size &&
      min_buffer_num + dec_cont->guard_size <= dec_cont->buffer_num_added)
    dec_cont->reset_ext_buffer = 0;
  else
    dec_cont->reset_ext_buffer = 1;

  if (!dec_cont->use_adaptive_buffers)
    dec_cont->reset_ext_buffer = 1;

  (void)buf_type;

  return dec_cont->reset_ext_buffer;
}

void HevcSetExternalBufferInfo(void *dec_inst, struct Storage *storage) {
  const struct SeqParamSet *sps = storage->active_sps;
  struct HevcDecContainer *dec_cont = (struct HevcDecContainer *)dec_inst;
  u32 pixel_width = (sps->bit_depth_luma == 8 && sps->bit_depth_chroma == 8) ? 8 : 10;
  u32 rs_pixel_width = storage->use_8bits_output ? 8 :
                       pixel_width == 10 ? (storage->use_p010_output ? 16 : 10) : 8;
  u32 buff_size, rs_buff_size, ds_buff_size;
#ifdef ASIC_TRACE_SUPPORT
  u32 is_frame_buffer;
#endif

  /* TODO: next cb or ctb multiple? */
  u32 pic_width_in_ctbs =
    (sps->pic_width + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;
  u32 pic_height_in_ctbs =
    (sps->pic_height + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;

  u32 pic_size = sps->pic_width * sps->pic_height * pixel_width / 8;

  u32 n_extra_frm_buffers = storage->n_extra_frm_buffers;

  /* buffer size of dpb pic = pic_size + dir_mv_size + tbl_size */
  u32 dmv_mem_size =
    /* num ctbs */
    (pic_width_in_ctbs * pic_height_in_ctbs *
     /* num 16x16 blocks in ctbs */
     (1 << (2 * (sps->log_max_coding_block_size - 4))) *
     /* num bytes per 16x16 */
     16);
  u32 ref_buff_size = NEXT_MULTIPLE(pic_size * 3 / 2, 16) + dmv_mem_size;
  u32 dpb_size = sps->max_dpb_size + 1;
  u32 min_buffer_num;
  enum BufferType buf_type;

  if (storage->use_video_compressor) {
    u32 pic_width_in_cbsy, pic_height_in_cbsy;
    u32 pic_width_in_cbsc, pic_height_in_cbsc;
    pic_width_in_cbsy = ((sps->pic_width + 8 - 1)/8);
    pic_width_in_cbsy = NEXT_MULTIPLE(pic_width_in_cbsy, 16);
    pic_width_in_cbsc = ((sps->pic_width + 16 - 1)/16);
    pic_width_in_cbsc = NEXT_MULTIPLE(pic_width_in_cbsc, 16);
    pic_height_in_cbsy = (sps->pic_height + 8 - 1)/8;
    pic_height_in_cbsc = (sps->pic_height/2 + 4 - 1)/4;

    /* luma table size */
    ref_buff_size += NEXT_MULTIPLE(pic_width_in_cbsy * pic_height_in_cbsy, 16);
    /* chroma table size */
    ref_buff_size += NEXT_MULTIPLE(pic_width_in_cbsc * pic_height_in_cbsc, 16);
  }

  rs_buff_size = NEXT_MULTIPLE(sps->pic_width * rs_pixel_width, 16 * 8) / 8 * sps->pic_height * 3 / 2;
  ds_buff_size = NEXT_MULTIPLE((sps->pic_width >> storage->down_scale_x_shift) * rs_pixel_width, 16 * 8) / 8
                 * (sps->pic_height >> storage->down_scale_y_shift) * 3 / 2;

  u32 tot_buffers = dpb_size + 2 + n_extra_frm_buffers;
  if (tot_buffers > MAX_FRAME_BUFFER_NUMBER)
    tot_buffers = MAX_FRAME_BUFFER_NUMBER;
  if (IS_EXTERNAL_BUFFER(dec_cont->ext_buffer_config, REFERENCE_BUFFER)) {
    min_buffer_num = dpb_size + 2;   /* We need at least (dpb_size+2) output buffers before starting decoding. */
    buff_size = ref_buff_size;
    buf_type = REFERENCE_BUFFER;
#ifdef ASIC_TRACE_SUPPORT
    is_frame_buffer = 1;
#endif
  } else {
    min_buffer_num = dpb_size + 1;
#ifdef ASIC_TRACE_SUPPORT
    is_frame_buffer = 0;
#endif
    if (IS_EXTERNAL_BUFFER(dec_cont->ext_buffer_config, DOWNSCALE_OUT_BUFFER)) {
      buff_size = ds_buff_size;
      buf_type = DOWNSCALE_OUT_BUFFER;
    } else {
      buff_size = rs_buff_size;
      buf_type = RASTERSCAN_OUT_BUFFER;
    }
  }

  dec_cont->buf_num = min_buffer_num;
  dec_cont->next_buf_size = buff_size;
  dec_cont->buf_type = buf_type;
#ifdef ASIC_TRACE_SUPPORT
  dec_cont->is_frame_buffer = is_frame_buffer;
#endif
}

u32 HevcAllocateSwResources(const void *dwl, struct Storage *storage, void *dec_inst) {
  u32 tmp;
  const struct SeqParamSet *sps = storage->active_sps;
  struct DpbInitParams dpb_params;
  struct HevcDecContainer *dec_cont = (struct HevcDecContainer *)dec_inst;
  u32 pixel_width = (sps->bit_depth_luma == 8 && sps->bit_depth_chroma == 8) ? 8 : 10;
  u32 rs_pixel_width = storage->use_8bits_output ? 8 :
                       pixel_width == 10 ? (storage->use_p010_output ? 16 : 10) : 8;

  /* TODO: next cb or ctb multiple? */
  storage->pic_width_in_cbs = sps->pic_width >> sps->log_min_coding_block_size;
  storage->pic_height_in_cbs =
    sps->pic_height >> sps->log_min_coding_block_size;
  storage->pic_width_in_ctbs =
    (sps->pic_width + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;
  storage->pic_height_in_ctbs =
    (sps->pic_height + (1 << sps->log_max_coding_block_size) - 1) >>
    sps->log_max_coding_block_size;

  storage->pic_size = sps->pic_width * sps->pic_height * pixel_width / 8;
  storage->curr_image->width = sps->pic_width;
  storage->curr_image->height = sps->pic_height;

  /* dpb output reordering disabled if
   * 1) application set no_reordering flag
   * 2) num_reorder_frames equal to 0 */
  if (storage->no_reordering)
    dpb_params.no_reordering = HANTRO_TRUE;
  else
    dpb_params.no_reordering = HANTRO_FALSE;

  dpb_params.dpb_size = sps->max_dpb_size;
  dpb_params.pic_size = storage->pic_size;
  dpb_params.n_extra_frm_buffers = storage->n_extra_frm_buffers;

  /* buffer size of dpb pic = pic_size + dir_mv_size */
  storage->dmv_mem_size =
    /* num ctbs */
    (storage->pic_width_in_ctbs * storage->pic_height_in_ctbs *
     /* num 16x16 blocks in ctbs */
     (1 << (2 * (sps->log_max_coding_block_size - 4))) *
     /* num bytes per 16x16 */
     16);
  dpb_params.buff_size = NEXT_MULTIPLE(storage->pic_size * 3 / 2, 16) + storage->dmv_mem_size;

  if (storage->use_video_compressor) {
    u32 pic_width_in_cbsy, pic_height_in_cbsy;
    u32 pic_width_in_cbsc, pic_height_in_cbsc;
    pic_width_in_cbsy = ((sps->pic_width + 8 - 1)/8);
    pic_width_in_cbsy = NEXT_MULTIPLE(pic_width_in_cbsy, 16);
    pic_width_in_cbsc = ((sps->pic_width + 16 - 1)/16);
    pic_width_in_cbsc = NEXT_MULTIPLE(pic_width_in_cbsc, 16);
    pic_height_in_cbsy = (sps->pic_height + 8 - 1)/8;
    pic_height_in_cbsc = (sps->pic_height/2 + 4 - 1)/4;

    /* luma table size */
    dpb_params.buff_size += NEXT_MULTIPLE(pic_width_in_cbsy * pic_height_in_cbsy, 16);
    /* chroma table size */
    dpb_params.buff_size += NEXT_MULTIPLE(pic_width_in_cbsc * pic_height_in_cbsc, 16);

    dpb_params.tbl_sizey = NEXT_MULTIPLE(pic_width_in_cbsy * pic_height_in_cbsy, 16);
    dpb_params.tbl_sizec = NEXT_MULTIPLE(pic_width_in_cbsc * pic_height_in_cbsc, 16);
#ifdef USE_FAKE_RFC_TABLE
    dec_cont->asic_buff->fake_tbly_size = dpb_params.tbl_sizey;
    dec_cont->asic_buff->fake_tblc_size = dpb_params.tbl_sizec;
#endif
  } else
    dpb_params.tbl_sizey = dpb_params.tbl_sizec = 0;

  /* note that calling ResetDpb here results in losing all
   * pictures currently in DPB -> nothing will be output from
   * the buffer even if no_output_of_prior_pics_flag is HANTRO_FALSE */
  tmp = HevcResetDpb(dec_inst, storage->dpb, &dpb_params);

  storage->dpb->pic_width = sps->pic_width;
  storage->dpb->pic_height = sps->pic_height;
  storage->dpb->bit_depth_luma = sps->bit_depth_luma;
  storage->dpb->bit_depth_chroma = sps->bit_depth_chroma;

  {
    struct HevcCropParams *crop = &storage->dpb->crop_params;
    const struct SeqParamSet *sps = storage->active_sps;

    if (sps->pic_cropping_flag) {
      u32 tmp1 = 2, tmp2 = 1;

      crop->crop_left_offset = tmp1 * sps->pic_crop_left_offset;
      crop->crop_out_width =
        sps->pic_width -
        tmp1 * (sps->pic_crop_left_offset + sps->pic_crop_right_offset);

      crop->crop_top_offset = tmp1 * tmp2 * sps->pic_crop_top_offset;
      crop->crop_out_height =
        sps->pic_height - tmp1 * tmp2 * (sps->pic_crop_top_offset +
                                         sps->pic_crop_bottom_offset);
    } else {
      crop->crop_left_offset = 0;
      crop->crop_top_offset = 0;
      crop->crop_out_width = sps->pic_width;
      crop->crop_out_height = sps->pic_height;
    }
  }

  if (tmp != HANTRO_OK) return (tmp);


  /* Allocate raster resources. */
  /* Raster scan buffer allocation to bottom half of AddBuffer() when USE_EXTERNAL_BUFFER is defined. */
  if ((storage->raster_enabled || storage->down_scale_enabled) && storage->dpb->dpb_reset &&
      !IS_EXTERNAL_BUFFER(dec_cont->ext_buffer_config, REFERENCE_BUFFER) &&
      dec_cont->reset_ext_buffer) {
    struct RasterBufferParams params;
    //    struct DWLLinearMem buffers[storage->dpb->tot_buffers];
    if (storage->raster_buffer_mgr) {
      /* TODO(min): External buffer will be released outside. This is guaranteed by user. */
      /*
      dec_cont->_buf_to_free = RbmReleaseBuffer(storage->raster_buffer_mgr);
      if (dec_cont->_buf_to_free.virtual_address != 0) {
        dec_cont->buf_to_free = &dec_cont->_buf_to_free;
        dec_cont->next_buf_size = 0;
        dec_cont->rbm_release = 1;
        dec_cont->buf_num = 0;
#ifdef ASIC_TRACE_SUPPORT
        dec_cont->is_frame_buffer = 0;
#endif
        dec_cont->buf_type = RASTERSCAN_OUT_BUFFER;
      } else
      */
      {
        /* In case there are external buffers, RbmRelease() will be calleded in GetBufferInfo()
           after all external buffers have been released. */
        RbmRelease(storage->raster_buffer_mgr);
        dec_cont->rbm_release = 0;
      }
    }
    params.dwl = dwl;
    params.width = 0;
    params.height = 0;
    // TODO(min): sps->max_dpb_size + 2 output buffers are enough?
    // params.num_buffers = storage->dpb->tot_buffers;
    params.num_buffers = sps->max_dpb_size + 2;
    for (int i = 0; i < params.num_buffers; i++)
      dec_cont->tiled_buffers[i] = storage->dpb[0].pic_buffers[i];
    params.tiled_buffers = dec_cont->tiled_buffers;
    if (storage->raster_enabled) {
      /* Raster out writes to modulo-16 widths. */
      params.width = NEXT_MULTIPLE(sps->pic_width * rs_pixel_width, 16 * 8) / 8;
      params.height = sps->pic_height;
    }
    params.ext_buffer_config = dec_cont->ext_buffer_config;
    if (storage->down_scale_enabled) {
      params.width = NEXT_MULTIPLE((sps->pic_width >> storage->down_scale_x_shift) * rs_pixel_width, 16 * 8) / 8;
      params.height = sps->pic_height >> storage->down_scale_y_shift;
    }
    dec_cont->params = params;
    if (dec_cont->rbm_release) {
      return DEC_WAITING_FOR_BUFFER;
    } else {
      storage->raster_buffer_mgr = RbmInit(params);
      if (storage->raster_buffer_mgr == NULL) return HANTRO_NOK;
      dec_cont->buffer_num_added = 0;

      /* Issue the external buffer request here when necessary. */
      if (storage->raster_enabled &&
          IS_EXTERNAL_BUFFER(dec_cont->ext_buffer_config, RASTERSCAN_OUT_BUFFER)) {
        /* Allocate raster scan / down scale buffers. */
        dec_cont->buf_type = RASTERSCAN_OUT_BUFFER;
        dec_cont->next_buf_size = params.width * params.height * 3 / 2;
        dec_cont->buf_to_free = NULL;
        dec_cont->buf_num = params.num_buffers;
#ifdef ASIC_TRACE_SUPPORT
        dec_cont->is_frame_buffer = 0;
#endif

        //dec_cont->buffer_index = 0;
      }
      else if (dec_cont->down_scale_enabled &&
               IS_EXTERNAL_BUFFER(dec_cont->ext_buffer_config, DOWNSCALE_OUT_BUFFER)) {
        dec_cont->buf_type = DOWNSCALE_OUT_BUFFER;
        dec_cont->next_buf_size = params.width * params.height * 3 / 2;
        dec_cont->buf_to_free = NULL;
        dec_cont->buf_num = params.num_buffers;
#ifdef ASIC_TRACE_SUPPORT
        dec_cont->is_frame_buffer = 0;
#endif
      }

      return DEC_WAITING_FOR_BUFFER;
    }
  }

  /*
    {
      struct HevcCropParams *crop = &storage->dpb->crop_params;
      const struct SeqParamSet *sps = storage->active_sps;

      if (sps->pic_cropping_flag) {
        u32 tmp1 = 2, tmp2 = 1;

        crop->crop_left_offset = tmp1 * sps->pic_crop_left_offset;
        crop->crop_out_width =
            sps->pic_width -
            tmp1 * (sps->pic_crop_left_offset + sps->pic_crop_right_offset);

        crop->crop_top_offset = tmp1 * tmp2 * sps->pic_crop_top_offset;
        crop->crop_out_height =
            sps->pic_height - tmp1 * tmp2 * (sps->pic_crop_top_offset +
                                             sps->pic_crop_bottom_offset);
      } else {
        crop->crop_left_offset = 0;
        crop->crop_top_offset = 0;
        crop->crop_out_width = sps->pic_width;
        crop->crop_out_height = sps->pic_height;
      }
    }
  */

  return HANTRO_OK;
}
#endif

u32 HevcStoreSEIInfoForCurrentPic(struct Storage *storage) {
  u32 tmp;
  struct DpbStorage *dpb = storage->dpb;

  tmp = HevcDecodeRHDParameters(storage->sei_param.stream_len,
                                &storage->sei_param,
                                storage->prev_nal_unit, storage->active_sps);
  if (tmp != HANTRO_OK)
    return HANTRO_NOK;

  dpb->cpb_removal_time = storage->sei_param.time_parameter.cpb_removal_time;;
  dpb->current_out->dpb_output_time = storage->sei_param.time_parameter.dpb_output_time;
  dpb->current_out->pic_struct = storage->sei_param.pic_parameter.pic_struct;
  return HANTRO_OK;
}
