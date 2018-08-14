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

#include "basetype.h"
#include "h264hwd_regdrv.h"
#include "h264hwd_asic.h"
#include "h264hwd_container.h"
#include "h264hwd_debug.h"
#include "h264hwd_util.h"
#include "h264hwd_cabac.h"
#include "dwl.h"
#include "refbuffer.h"
#include "h264_pp_multibuffer.h"

#include "h264decmc_internals.h"

#define ASIC_HOR_MV_MASK            0x07FFFU
#define ASIC_VER_MV_MASK            0x01FFFU

#define ASIC_HOR_MV_OFFSET          17U
#define ASIC_VER_MV_OFFSET          4U

static void H264RefreshRegs(decContainer_t * dec_cont);
static void H264FlushRegs(decContainer_t * dec_cont);
static void h264StreamPosUpdate(decContainer_t * dec_cont);
static void H264PrepareCabacInitTables(u32 *cabac_init);
static void h264PreparePOC(decContainer_t *dec_cont);
static void h264PrepareScaleList(decContainer_t *dec_cont);
static void h264CopyPocToHw(decContainer_t *dec_cont);

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

const u32 ref_base[16] = {
  HWIF_REFER0_BASE, HWIF_REFER1_BASE, HWIF_REFER2_BASE,
  HWIF_REFER3_BASE, HWIF_REFER4_BASE, HWIF_REFER5_BASE,
  HWIF_REFER6_BASE, HWIF_REFER7_BASE, HWIF_REFER8_BASE,
  HWIF_REFER9_BASE, HWIF_REFER10_BASE, HWIF_REFER11_BASE,
  HWIF_REFER12_BASE, HWIF_REFER13_BASE, HWIF_REFER14_BASE,
  HWIF_REFER15_BASE
};
const u32 ref_base_msb[16] = {
  HWIF_REFER0_BASE_MSB, HWIF_REFER1_BASE_MSB, HWIF_REFER2_BASE_MSB,
  HWIF_REFER3_BASE_MSB, HWIF_REFER4_BASE_MSB, HWIF_REFER5_BASE_MSB,
  HWIF_REFER6_BASE_MSB, HWIF_REFER7_BASE_MSB, HWIF_REFER8_BASE_MSB,
  HWIF_REFER9_BASE_MSB, HWIF_REFER10_BASE_MSB, HWIF_REFER11_BASE_MSB,
  HWIF_REFER12_BASE_MSB, HWIF_REFER13_BASE_MSB, HWIF_REFER14_BASE_MSB,
  HWIF_REFER15_BASE_MSB
};
const u32 ref_field_mode[16] = {
  HWIF_REFER0_FIELD_E, HWIF_REFER1_FIELD_E, HWIF_REFER2_FIELD_E,
  HWIF_REFER3_FIELD_E, HWIF_REFER4_FIELD_E, HWIF_REFER5_FIELD_E,
  HWIF_REFER6_FIELD_E, HWIF_REFER7_FIELD_E, HWIF_REFER8_FIELD_E,
  HWIF_REFER9_FIELD_E, HWIF_REFER10_FIELD_E, HWIF_REFER11_FIELD_E,
  HWIF_REFER12_FIELD_E, HWIF_REFER13_FIELD_E, HWIF_REFER14_FIELD_E,
  HWIF_REFER15_FIELD_E
};

const u32 ref_topc[16] = {
  HWIF_REFER0_TOPC_E, HWIF_REFER1_TOPC_E, HWIF_REFER2_TOPC_E,
  HWIF_REFER3_TOPC_E, HWIF_REFER4_TOPC_E, HWIF_REFER5_TOPC_E,
  HWIF_REFER6_TOPC_E, HWIF_REFER7_TOPC_E, HWIF_REFER8_TOPC_E,
  HWIF_REFER9_TOPC_E, HWIF_REFER10_TOPC_E, HWIF_REFER11_TOPC_E,
  HWIF_REFER12_TOPC_E, HWIF_REFER13_TOPC_E, HWIF_REFER14_TOPC_E,
  HWIF_REFER15_TOPC_E
};

const u32 ref_pic_num[16] = {
  HWIF_REFER0_NBR, HWIF_REFER1_NBR, HWIF_REFER2_NBR,
  HWIF_REFER3_NBR, HWIF_REFER4_NBR, HWIF_REFER5_NBR,
  HWIF_REFER6_NBR, HWIF_REFER7_NBR, HWIF_REFER8_NBR,
  HWIF_REFER9_NBR, HWIF_REFER10_NBR, HWIF_REFER11_NBR,
  HWIF_REFER12_NBR, HWIF_REFER13_NBR, HWIF_REFER14_NBR,
  HWIF_REFER15_NBR
};


/*------------------------------------------------------------------------------
                Reference list initialization
------------------------------------------------------------------------------*/
#define IS_SHORT_TERM_FRAME(a) ((a).status[0] == SHORT_TERM && \
                                (a).status[1] == SHORT_TERM)
#define IS_SHORT_TERM_FRAME_F(a) ((a).status[0] == SHORT_TERM || \
                                (a).status[1] == SHORT_TERM)
#define IS_LONG_TERM_FRAME(a) ((a).status[0] == LONG_TERM && \
                                (a).status[1] == LONG_TERM)
#define IS_LONG_TERM_FRAME_F(a) ((a).status[0] == LONG_TERM || \
                                (a).status[1] == LONG_TERM)
#define IS_REF_FRAME(a) ((a).status[0] && (a).status[0] != EMPTY && \
                         (a).status[1] && (a).status[1] != EMPTY )
#define IS_REF_FRAME_F(a) (((a).status[0] && (a).status[0] != EMPTY) || \
                           ((a).status[1] && (a).status[1] != EMPTY) )

#define FRAME_POC(a) MIN((a).pic_order_cnt[0], (a).pic_order_cnt[1])
#define FIELD_POC(a) MIN(((a).status[0] != EMPTY) ? \
                          (a).pic_order_cnt[0] : 0x7FFFFFFF,\
                         ((a).status[1] != EMPTY) ? \
                          (a).pic_order_cnt[1] : 0x7FFFFFFF)

#define INVALID_IDX 0xFFFFFFFF
#define MIN_POC 0x80000000
#define MAX_POC 0x7FFFFFFF

#define ABS(a) (((a) < 0) ? -(a) : (a))
#define GET_POC(pic)            GetPoc(&(pic))
#define IS_REFERENCE_F(a)       IsReferenceField(&(a))
#define IS_REFERENCE(a,f)       IsReference(a,f)

const u32 ref_pic_list0[16] = {
  HWIF_BINIT_RLIST_F0, HWIF_BINIT_RLIST_F1, HWIF_BINIT_RLIST_F2,
  HWIF_BINIT_RLIST_F3, HWIF_BINIT_RLIST_F4, HWIF_BINIT_RLIST_F5,
  HWIF_BINIT_RLIST_F6, HWIF_BINIT_RLIST_F7, HWIF_BINIT_RLIST_F8,
  HWIF_BINIT_RLIST_F9, HWIF_BINIT_RLIST_F10, HWIF_BINIT_RLIST_F11,
  HWIF_BINIT_RLIST_F12, HWIF_BINIT_RLIST_F13, HWIF_BINIT_RLIST_F14,
  HWIF_BINIT_RLIST_F15
};

const u32 ref_pic_list1[16] = {
  HWIF_BINIT_RLIST_B0, HWIF_BINIT_RLIST_B1, HWIF_BINIT_RLIST_B2,
  HWIF_BINIT_RLIST_B3, HWIF_BINIT_RLIST_B4, HWIF_BINIT_RLIST_B5,
  HWIF_BINIT_RLIST_B6, HWIF_BINIT_RLIST_B7, HWIF_BINIT_RLIST_B8,
  HWIF_BINIT_RLIST_B9, HWIF_BINIT_RLIST_B10, HWIF_BINIT_RLIST_B11,
  HWIF_BINIT_RLIST_B12, HWIF_BINIT_RLIST_B13, HWIF_BINIT_RLIST_B14,
  HWIF_BINIT_RLIST_B15
};

const u32 ref_pic_list_p[16] = {
  HWIF_PINIT_RLIST_F0, HWIF_PINIT_RLIST_F1, HWIF_PINIT_RLIST_F2,
  HWIF_PINIT_RLIST_F3, HWIF_PINIT_RLIST_F4, HWIF_PINIT_RLIST_F5,
  HWIF_PINIT_RLIST_F6, HWIF_PINIT_RLIST_F7, HWIF_PINIT_RLIST_F8,
  HWIF_PINIT_RLIST_F9, HWIF_PINIT_RLIST_F10, HWIF_PINIT_RLIST_F11,
  HWIF_PINIT_RLIST_F12, HWIF_PINIT_RLIST_F13, HWIF_PINIT_RLIST_F14,
  HWIF_PINIT_RLIST_F15
};

static i32 GetPoc(dpbPicture_t *pic)
{
  i32 poc0 = (pic->status[0] == EMPTY ? 0x7FFFFFFF : pic->pic_order_cnt[0]);
  i32 poc1 = (pic->status[1] == EMPTY ? 0x7FFFFFFF : pic->pic_order_cnt[1]);
  return MIN(poc0,poc1);
}

static u32 IsReferenceField( const dpbPicture_t *a)
{
  return (a->status[0] != UNUSED && a->status[0] != EMPTY) ||
    (a->status[1] != UNUSED && a->status[1] != EMPTY);
}
#if 0
static u32 IsReference( const dpbPicture_t a, const u32 f ) {
  switch(f) {
    case TOPFIELD: return a.status[0] && a.status[0] != EMPTY;
    case BOTFIELD: return a.status[1] && a.status[1] != EMPTY;
    default: return a.status[0] && a.status[0] != EMPTY &&
                 a.status[1] && a.status[1] != EMPTY;
  }
}
#endif

/*------------------------------------------------------------------------------
    Function name : AllocateAsicBuffers
    Description   :

    Return type   : i32
    Argument      : DecAsicBuffers_t * asic_buff
    Argument      : u32 mbs
------------------------------------------------------------------------------*/
u32 AllocateAsicBuffers(decContainer_t * dec_cont, DecAsicBuffers_t * asic_buff,
                        u32 mbs) {
  i32 ret = 0;
  u32 tmp, i;

  if(dec_cont->rlc_mode) {
    tmp = ASIC_MB_CTRL_BUFFER_SIZE;
    asic_buff[0].mb_ctrl.mem_type = DWL_MEM_TYPE_VPU_WORKING;
    ret = DWLMallocLinear(dec_cont->dwl, mbs * tmp, &asic_buff[0].mb_ctrl);

    tmp = ASIC_MB_MV_BUFFER_SIZE;
    asic_buff[0].mv.mem_type = DWL_MEM_TYPE_VPU_WORKING;
    ret |= DWLMallocLinear(dec_cont->dwl, mbs * tmp, &asic_buff[0].mv);

    tmp = ASIC_MB_I4X4_BUFFER_SIZE;
    asic_buff[0].intra_pred.mem_type = DWL_MEM_TYPE_VPU_WORKING;
    ret |=
      DWLMallocLinear(dec_cont->dwl, mbs * tmp, &asic_buff[0].intra_pred);

    tmp = ASIC_MB_RLC_BUFFER_SIZE;
    asic_buff[0].residual.mem_type = DWL_MEM_TYPE_VPU_WORKING;
    ret |= DWLMallocLinear(dec_cont->dwl, mbs * tmp, &asic_buff[0].residual);
  }

  asic_buff[0].buff_status = 0;
  asic_buff[0].pic_size_in_mbs = mbs;

  if(dec_cont->h264_profile_support != H264_BASELINE_PROFILE) {
    tmp = ASIC_CABAC_INIT_BUFFER_SIZE + ASIC_SCALING_LIST_SIZE +
          ASIC_POC_BUFFER_SIZE;

    for(i = 0; i < dec_cont->n_cores; i++) {
      asic_buff->cabac_init[i].mem_type = DWL_MEM_TYPE_VPU_WORKING;
      ret |= DWLMallocLinear(dec_cont->dwl, tmp,
                             asic_buff->cabac_init + i);
      if(ret == 0) {
        H264PrepareCabacInitTables(
          asic_buff->cabac_init[i].virtual_address);
      }
    }
  }

  if(dec_cont->ref_buf_support) {
    RefbuInit(&dec_cont->ref_buffer_ctrl, DEC_X170_MODE_H264,
              dec_cont->storage.active_sps->pic_width_in_mbs,
              dec_cont->storage.active_sps->pic_height_in_mbs,
              dec_cont->ref_buf_support);
  }

  if(ret != 0)
    return 1;
  else
    return 0;
}

/*------------------------------------------------------------------------------
    Function name : ReleaseAsicBuffers
    Description   :

    Return type   : void
    Argument      : DecAsicBuffers_t * asic_buff
------------------------------------------------------------------------------*/
void ReleaseAsicBuffers(const void *dwl, DecAsicBuffers_t * asic_buff) {
  i32 i;

  if(asic_buff[0].mb_ctrl.virtual_address != NULL) {
    DWLFreeLinear(dwl, &asic_buff[0].mb_ctrl);
    asic_buff[0].mb_ctrl.virtual_address = NULL;
  }
  if(asic_buff[0].residual.virtual_address != NULL) {
    DWLFreeLinear(dwl, &asic_buff[0].residual);
    asic_buff[0].residual.virtual_address = NULL;
  }

  if(asic_buff[0].mv.virtual_address != NULL) {
    DWLFreeLinear(dwl, &asic_buff[0].mv);
    asic_buff[0].mv.virtual_address = NULL;
  }

  if(asic_buff[0].intra_pred.virtual_address != NULL) {
    DWLFreeLinear(dwl, &asic_buff[0].intra_pred);
    asic_buff[0].intra_pred.virtual_address = NULL;
  }


  for(i = 0; i < MAX_ASIC_CORES; i++) {
    if(asic_buff[0].cabac_init[i].virtual_address != NULL) {
      DWLFreeLinear(dwl, asic_buff[0].cabac_init + i);
      asic_buff[0].cabac_init[i].virtual_address = NULL;
    }
  }

}

/*------------------------------------------------------------------------------
    Function name : H264RunAsic
    Description   :

    Return type   : hw status
    Argument      : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
u32 H264RunAsic(decContainer_t * dec_cont, DecAsicBuffers_t * p_asic_buff) {
  const seqParamSet_t *p_sps = dec_cont->storage.active_sps;
  const sliceHeader_t *p_slice_header = dec_cont->storage.slice_header;
  const picParamSet_t *p_pps = dec_cont->storage.active_pps;
  const storage_t *storage = &dec_cont->storage;
  dpbStorage_t *dpb = dec_cont->storage.dpb;

  u32 asic_status = 0;
  i32 ret = 0;

  if(!dec_cont->asic_running) {
    u32 reserve_ret = 0;
    u32 i;

    for(i = 0; i < 16; i++) {
      /* stream decoder may get invalid ref buffer index from errorneus
         stream. Initialize unused base addresses to safe values */
      if(p_asic_buff->ref_pic_list[i] == 0x0)
        SET_ADDR_REG2(dec_cont->h264_regs, ref_base[i], ref_base_msb[i],
                      p_asic_buff->ref_pic_list[0]);
      else
        SET_ADDR_REG2(dec_cont->h264_regs, ref_base[i], ref_base_msb[i],
                      p_asic_buff->ref_pic_list[i]);
    }
    /* inter-view reference picture */
    if (dec_cont->storage.view && !dec_cont->storage.non_inter_view_ref) {
      addr_t tmp;
      tmp = dec_cont->storage.dpbs[0]->current_out->data->bus_address;
      if (dec_cont->storage.dpbs[0]->current_out->is_field_pic)
        tmp |= 0x2;
      SET_ADDR_REG(dec_cont->h264_regs, HWIF_INTER_VIEW_BASE, tmp);
      SetDecRegister(dec_cont->h264_regs, HWIF_REFER_VALID_E,
                     GetDecRegister(dec_cont->h264_regs, HWIF_REFER_VALID_E) |
                     (p_slice_header->field_pic_flag ? 0x3 : 0x10000) );
      /* mark interview pic as long term, just to "cheat" mvd to never
       * set colZeroFlag */
      SetDecRegister(dec_cont->h264_regs, HWIF_REFER_LTERM_E,
                     GetDecRegister(dec_cont->h264_regs, HWIF_REFER_LTERM_E) |
                     (p_slice_header->field_pic_flag ? 0x3 : 0x10000) );
    }
    SetDecRegister(dec_cont->h264_regs, HWIF_MVC_E,
                   dec_cont->storage.view != 0);

    SetDecRegister(dec_cont->h264_regs, HWIF_FILTERING_DIS,
                   p_asic_buff->filter_disable);

#if 0
    fprintf(stderr, "\n\tCurrently decoding; %s 0x%08x\n",
            p_slice_header->field_pic_flag ?
            (p_slice_header->bottom_field_flag ? "FIELD BOTTOM" : "FIELD TOP") :
            "FRAME", p_asic_buff->out_buffer->bus_address);
#endif

    if(p_slice_header->field_pic_flag && p_slice_header->bottom_field_flag) {

      if(dec_cont->dpb_mode == DEC_DPB_FRAME) {
        SET_ADDR_REG(dec_cont->h264_regs, HWIF_DEC_OUT_BASE,
                     p_asic_buff->out_buffer->bus_address +
                     p_sps->pic_width_in_mbs * 16);
      } else {
        SET_ADDR_REG(dec_cont->h264_regs, HWIF_DEC_OUT_BASE,
                     p_asic_buff->out_buffer->bus_address);
      }
    } else {
      SET_ADDR_REG(dec_cont->h264_regs, HWIF_DEC_OUT_BASE,
                   p_asic_buff->out_buffer->bus_address);
    }
    SetDecRegister( dec_cont->h264_regs, HWIF_DPB_ILACE_MODE,
                    dec_cont->dpb_mode );

    SetDecRegister(dec_cont->h264_regs, HWIF_CH_QP_OFFSET,
                   p_asic_buff->chroma_qp_index_offset);
    SetDecRegister(dec_cont->h264_regs, HWIF_CH_QP_OFFSET2,
                   p_asic_buff->chroma_qp_index_offset2);

    if(dec_cont->rlc_mode) {
      SetDecRegister(dec_cont->h264_regs, HWIF_RLC_MODE_E, 1);

      SET_ADDR_REG(dec_cont->h264_regs, HWIF_MB_CTRL_BASE,
                   p_asic_buff->mb_ctrl.bus_address);

      SET_ADDR_REG(dec_cont->h264_regs, HWIF_RLC_VLC_BASE,
                   p_asic_buff->residual.bus_address);

      SetDecRegister(dec_cont->h264_regs, HWIF_REF_FRAMES,
                     p_sps->num_ref_frames);

      SET_ADDR_REG(dec_cont->h264_regs, HWIF_INTRA_4X4_BASE,
                   p_asic_buff->intra_pred.bus_address);

      SET_ADDR_REG(dec_cont->h264_regs, HWIF_DIFF_MV_BASE,
                   p_asic_buff->mv.bus_address);

      SetDecRegister(dec_cont->h264_regs, HWIF_STREAM_LEN, 0);
      SetDecRegister(dec_cont->h264_regs, HWIF_STRM_START_BIT, 0);
    } else {
      /* new 8190 stuff (not used/ignored in 8170) */
      if(dec_cont->h264_profile_support == H264_BASELINE_PROFILE) {
        goto skipped_high_profile;  /* leave high profile stuff disabled */
      }

      /* direct mv writing not enabled if HW config says that
       * SW_DEC_H264_PROF="10" */
      if(p_asic_buff->enable_dmv_and_poc && dec_cont->h264_profile_support != 2) {
        u32 dir_mv_offset = dpb->dir_mv_offset;

        if (dec_cont->storage.mvc && dec_cont->storage.view == 0)
          SetDecRegister(dec_cont->h264_regs, HWIF_WRITE_MVS_E,
                         dec_cont->storage.non_inter_view_ref == 0 ||
                         dec_cont->storage.prev_nal_unit->nal_ref_idc != 0);
        else
          SetDecRegister(dec_cont->h264_regs, HWIF_WRITE_MVS_E,
                         dec_cont->storage.prev_nal_unit->nal_ref_idc != 0);

        if(p_slice_header->bottom_field_flag)
          dir_mv_offset += dpb->pic_size_in_mbs * 32;

        SET_ADDR_REG(dec_cont->h264_regs, HWIF_DIR_MV_BASE,
                     p_asic_buff->out_buffer->bus_address + dir_mv_offset);
      } else
        SetDecRegister(dec_cont->h264_regs, HWIF_WRITE_MVS_E, 0);

      /* make sure that output pic sync memory is cleared */
      {
        char *sync_base =
          (char *) (p_asic_buff->out_buffer->virtual_address) +
          dpb->sync_mc_offset;
        u32 sync_size = 16; /* 16 bytes for each field */
        if (!p_slice_header->field_pic_flag) {
          sync_size += 16; /* reset all 32 bytes */
        } else if (p_slice_header->bottom_field_flag) {
          sync_base += 16; /* bottom field sync area*/
        }

        (void) DWLmemset(sync_base, 0, sync_size);
      }

      SetDecRegister(dec_cont->h264_regs, HWIF_DIR_8X8_INFER_E,
                     p_sps->direct8x8_inference_flag);
      SetDecRegister(dec_cont->h264_regs, HWIF_WEIGHT_PRED_E,
                     p_pps->weighted_pred_flag);
      SetDecRegister(dec_cont->h264_regs, HWIF_WEIGHT_BIPR_IDC,
                     p_pps->weighted_bi_pred_idc);
      SetDecRegister(dec_cont->h264_regs, HWIF_REFIDX1_ACTIVE,
                     p_pps->num_ref_idx_l1_active);
      SetDecRegister(dec_cont->h264_regs, HWIF_FIELDPIC_FLAG_E,
                     !p_sps->frame_mbs_only_flag);
      SetDecRegister(dec_cont->h264_regs, HWIF_PIC_INTERLACE_E,
                     !p_sps->frame_mbs_only_flag &&
                     (p_sps->mb_adaptive_frame_field_flag || p_slice_header->field_pic_flag));

      SetDecRegister(dec_cont->h264_regs, HWIF_PIC_FIELDMODE_E,
                     p_slice_header->field_pic_flag);

      DEBUG_PRINT(("framembs %d, mbaff %d, fieldpic %d\n",
                   p_sps->frame_mbs_only_flag,
                   p_sps->mb_adaptive_frame_field_flag, p_slice_header->field_pic_flag));

      SetDecRegister(dec_cont->h264_regs, HWIF_PIC_TOPFIELD_E,
                     !p_slice_header->bottom_field_flag);

      SetDecRegister(dec_cont->h264_regs, HWIF_SEQ_MBAFF_E,
                     p_sps->mb_adaptive_frame_field_flag);

      SetDecRegister(dec_cont->h264_regs, HWIF_8X8TRANS_FLAG_E,
                     p_pps->transform8x8_flag);

      SetDecRegister(dec_cont->h264_regs, HWIF_BLACKWHITE_E,
                     p_sps->profile_idc >= 100 &&
                     p_sps->chroma_format_idc == 0);

      SetDecRegister(dec_cont->h264_regs, HWIF_TYPE1_QUANT_E,
                     p_pps->scaling_matrix_present_flag);
    }

    /***************************************************************************/
skipped_high_profile:

    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUT_DIS,
                   p_asic_buff->disable_out_writing);

    /* Setup reference picture buffer */
    if(dec_cont->ref_buf_support) {
      u32 pic_id0 = 0;
      u32 pic_id1 = 0;
      u32 flags = dec_cont->b_mc ? REFBU_DONT_USE_STATS : 0;
      const u8 *pic_id0_valid = NULL;
      const u8 *pic_id1_valid = NULL;
      u32 is_intra_frame = IS_I_SLICE(p_slice_header->slice_type);
      refbuMode_e ref_buff_mode;

#ifdef DEC_REFBU_DONT_USE_STATS
      flags = REFBU_DONT_USE_STATS;
#endif

      if(p_slice_header->field_pic_flag)
        ref_buff_mode = REFBU_FIELD;
      else if(p_sps->mb_adaptive_frame_field_flag)
        ref_buff_mode = REFBU_MBAFF;
      else
        ref_buff_mode = REFBU_FRAME;

      /* Find first valid reference picture for pic_id0 */
      for (i = 0 ; i < 16 ; ++i) {
        pic_id0 = i;
        pic_id0_valid = h264bsdGetRefPicDataVlcMode(dpb + 1, pic_id0, 0);
        if(pic_id0_valid != NULL)
          break;
      }
      /* Find 2nd valid reference picture for pic_id1 */
      for (++i ; i < 16 ; ++i) {
        pic_id1 = i;
        pic_id1_valid = h264bsdGetRefPicDataVlcMode(dpb + 1, pic_id1, 0);
        if(pic_id1_valid != NULL)
          break;
      }

      /* If pic_id0 is not valid, just tell reference buffer that this is
         an Intra frame, which will implicitly disable buffering. */
      if(pic_id0_valid == NULL) {
        is_intra_frame = 1;
      } else if ((p_pps->num_ref_idx_l0_active > 1) && (pic_id1_valid != NULL)) {
        flags |= REFBU_MULTIPLE_REF_FRAMES;
      }

      if(pic_id1_valid == NULL) {
        /* make sure that double buffer uses valid picId */
        pic_id1 = pic_id0;
      }

#ifdef DEC_REFBU_INTERLACED_DISABLE
      if(dec_cont->b_mc && (!p_sps->frame_mbs_only_flag))
        flags = REFBU_DISABLE;
#endif

      RefbuSetup(&dec_cont->ref_buffer_ctrl, dec_cont->h264_regs,
                 ref_buff_mode, is_intra_frame,
                 IS_B_SLICE(p_slice_header->slice_type),
                 pic_id0, pic_id1, flags );
    }

    if (dec_cont->storage.enable2nd_chroma &&
        !dec_cont->storage.active_sps->mono_chrome) {
      SetDecRegister(dec_cont->h264_regs, HWIF_CH_8PIX_ILEAV_E, 1);
      if(p_slice_header->field_pic_flag && p_slice_header->bottom_field_flag) {
        SET_ADDR_REG(dec_cont->h264_regs, HWIF_DEC_CH8PIX_BASE,
                     p_asic_buff->out_buffer->bus_address + dpb->ch2_offset+
                     p_sps->pic_width_in_mbs * 16);
      } else {
        SET_ADDR_REG(dec_cont->h264_regs, HWIF_DEC_CH8PIX_BASE,
                     p_asic_buff->out_buffer->bus_address + dpb->ch2_offset);
      }
    } else
      SetDecRegister(dec_cont->h264_regs, HWIF_CH_8PIX_ILEAV_E, 0);

    if (!dec_cont->keep_hw_reserved) {

      if(dec_cont->pp.pp_instance != NULL &&
          dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING &&
          dec_cont->pp.dec_pp_if.use_pipeline) {
        /* reserved both DEC and PP hardware for pipeline */
        reserve_ret = DWLReserveHwPipe(dec_cont->dwl, &dec_cont->core_id);
      } else {
        reserve_ret = DWLReserveHw(dec_cont->dwl, &dec_cont->core_id);
      }

      if(reserve_ret != DWL_OK) {
        /* Decrement usage for DPB buffers */
        DecrementDPBRefCount(&storage->dpb[1]);

        return X170_DEC_HW_RESERVED;
      }
    }

    dec_cont->asic_running = 1;

#if 0
    if(dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.pp_info.multi_buffer == 0 &&
        dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
      /* legacy single buffer mode */
      DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;

      TRACE_PP_CTRL("H264RunAsic: PP Run\n");

      dec_pp_if->inwidth = p_sps->pic_width_in_mbs * 16;
      dec_pp_if->inheight = p_sps->pic_height_in_mbs * 16;
      dec_pp_if->cropped_w = dec_pp_if->inwidth;
      dec_pp_if->cropped_h = dec_pp_if->inheight;

      if(p_sps->frame_mbs_only_flag) {
        dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
      } else {
        /* interlaced */
        if( dec_cont->dpb_mode == DEC_DPB_FRAME )
          dec_pp_if->pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
        else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD )
          dec_pp_if->pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD;
        /* TODO: missing field? is this OK? */
      }

      dec_pp_if->little_endian =
        GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUT_ENDIAN);
      dec_pp_if->word_swap =
        GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUTSWAP32_E);
      dec_pp_if->tiled_input_mode = dec_cont->tiled_reference_enable;
      dec_pp_if->progressive_sequence =
        dec_cont->storage.active_sps->frame_mbs_only_flag;

      if(dec_pp_if->use_pipeline) {
        dec_pp_if->input_bus_luma = dpb->current_out->data->bus_address;
      } else {
        if (storage->out_view == storage->view)
          dec_pp_if->input_bus_luma =
            dpb->out_buf[dpb->out_index_r].data->bus_address;
        else /* starting pp for another view */
          dec_pp_if->input_bus_luma =
            storage->dpbs[storage->out_view]->
            out_buf[storage->dpbs[storage->out_view]->out_index_r].
            data->bus_address;
      }

      dec_pp_if->input_bus_chroma =
        dec_pp_if->input_bus_luma + dec_pp_if->inwidth * dec_pp_if->inheight;

      if(dec_pp_if->pic_struct != DECPP_PIC_FRAME_OR_TOP_FIELD) {
        if( dec_cont->dpb_mode == DEC_DPB_FRAME ) {
          dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                       dec_pp_if->inwidth;
          dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                         dec_pp_if->inwidth;
        }
        if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
          dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                       (dec_pp_if->inwidth*dec_pp_if->inheight >> 1);
          dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                         (dec_pp_if->inwidth*dec_pp_if->inheight >> 2);
        }
      } else {
        dec_pp_if->bottom_bus_luma = (u32) (-1);
        dec_pp_if->bottom_bus_chroma = (u32) (-1);
      }

      dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);
    }

    if(dec_cont->pp.pp_instance != NULL &&
        dec_cont->pp.pp_info.multi_buffer == 1 &&
        dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING) {
      /* multibuffer mode */
      DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;

      TRACE_PP_CTRL("H264RunAsic: PP Run\n");

      dec_pp_if->inwidth = p_sps->pic_width_in_mbs * 16;
      dec_pp_if->inheight = p_sps->pic_height_in_mbs * 16;
      dec_pp_if->cropped_w = dec_pp_if->inwidth;
      dec_pp_if->cropped_h = dec_pp_if->inheight;

      if(p_sps->frame_mbs_only_flag) {
        dec_pp_if->pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;
      } else {
        /* interlaced */
        if( dec_cont->dpb_mode == DEC_DPB_FRAME )
          dec_pp_if->pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
        else if ( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD )
          dec_pp_if->pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD;
      }

      dec_pp_if->little_endian =
        GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUT_ENDIAN);
      dec_pp_if->word_swap =
        GetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUTSWAP32_E);
      dec_pp_if->tiled_input_mode = dec_cont->tiled_reference_enable;
      dec_pp_if->progressive_sequence =
        dec_cont->storage.active_sps->frame_mbs_only_flag;

      if(dec_pp_if->use_pipeline) {
        u32 pic_id;
        dec_pp_if->input_bus_luma = storage->dpb->current_out->data->bus_address;
        pic_id = h264PpMultiAddPic(dec_cont, storage->dpb->current_out->data);

        ASSERT(pic_id <= dec_cont->pp.multi_max_id);
        TRACE_PP_CTRL("H264RunAsic: PP Multibuffer index = %d\n", pic_id);
        dec_pp_if->buffer_index = pic_id;
        dec_pp_if->display_index = (u32)(-1);

      } else {
        u32 pic_id;
        u32 view = storage->view;
        struct DWLLinearMem *pic =
          (struct DWLLinearMem *)dec_cont->pp.queued_pic_to_pp[storage->view];

        if (pic == NULL) {
          view = !storage->view;
          pic = (struct DWLLinearMem *)dec_cont->pp.queued_pic_to_pp[!storage->view];
        }

        /* send queued picture to PP */
        ASSERT(pic != NULL);

        dec_pp_if->input_bus_luma = pic->bus_address;
        pic_id = h264PpMultiAddPic(dec_cont, pic);

        ASSERT(pic_id <= dec_cont->pp.multi_max_id);
        TRACE_PP_CTRL("H264RunAsic: PP Multibuffer index = %d\n", pic_id);
        dec_pp_if->buffer_index = pic_id;
        dec_pp_if->display_index = (u32)(-1);

        /* queue curent picture for next PP processing */
        if(storage->view == 0 ? !storage->second_field :
            !storage->base_opposite_field_pic) {
          dec_cont->pp.queued_pic_to_pp[storage->view] =
            storage->dpbs[storage->view]->current_out->data;
        } else {
          /* do not queue first field */
          dec_cont->pp.queued_pic_to_pp[view] = NULL;
        }
      }

      dec_pp_if->input_bus_chroma =
        dec_pp_if->input_bus_luma + dec_pp_if->inwidth * dec_pp_if->inheight;

      if(dec_pp_if->pic_struct != DECPP_PIC_FRAME_OR_TOP_FIELD) {
        if( dec_cont->dpb_mode == DEC_DPB_FRAME ) {
          dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                       dec_pp_if->inwidth;
          dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                         dec_pp_if->inwidth;
        }
        if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
          dec_pp_if->bottom_bus_luma = dec_pp_if->input_bus_luma +
                                       (dec_pp_if->inwidth*dec_pp_if->inheight >> 1);
          dec_pp_if->bottom_bus_chroma = dec_pp_if->input_bus_chroma +
                                         (dec_pp_if->inwidth*dec_pp_if->inheight >> 2);
        }
      } else {
        dec_pp_if->bottom_bus_luma = (u32) (-1);
        dec_pp_if->bottom_bus_chroma = (u32) (-1);
      }

      dec_cont->pp.PPDecStart(dec_cont->pp.pp_instance, dec_pp_if);
    }
#endif

#if 0
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_DS_E, dec_cont->pp_enabled);
    if (dec_cont->pp_enabled) {
      u32 dsw, dsh ;
#define NEXT_MULTIPLE(value, n) (((value) + (n) - 1) & ~((n) - 1))

      dsw = NEXT_MULTIPLE((p_sps->pic_width_in_mbs * 16 >> dec_cont->storage.down_scale_x_shift) * 8, 16 * 8) / 8;
      dsh = (p_sps->pic_height_in_mbs * 16 >> dec_cont->storage.down_scale_y_shift);
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_DS_X,
                     dec_cont->dscale_shift_x);
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_DS_Y,
                     dec_cont->dscale_shift_y);
      SET_ADDR64_REG(dec_cont->h264_regs, HWIF_DEC_DSY_BASE,
                     p_asic_buff->out_pp_buffer->bus_address);
      SET_ADDR64_REG(dec_cont->h264_regs, HWIF_DEC_DSC_BASE,
                     p_asic_buff->out_pp_buffer->bus_address + dsw * dsh);
    }
#else
    SetDecRegister(dec_cont->h264_regs, HWIF_PP_PIPELINE_E_U, dec_cont->pp_enabled);
    if (dec_cont->pp_enabled) {
      u32 dsw, dsh ;
#define NEXT_MULTIPLE(value, n) (((value) + (n) - 1) & ~((n) - 1))

      dsw = NEXT_MULTIPLE((p_sps->pic_width_in_mbs * 16 >> dec_cont->storage.down_scale_x_shift) * 8, 16 * 8) / 8;
      dsh = (p_sps->pic_height_in_mbs * 16 >> dec_cont->storage.down_scale_y_shift);
      SetDecRegister(dec_cont->h264_regs, HWIF_PP_OUT_Y_STRIDE, dsw);
      SetDecRegister(dec_cont->h264_regs, HWIF_PP_OUT_C_STRIDE, dsw);
      if (dec_cont->storage.down_scale_x_shift == 0) {
        SetDecRegister(dec_cont->h264_regs, HWIF_HOR_SCALE_MODE_U, 0);
        SetDecRegister(dec_cont->h264_regs, HWIF_WSCALE_INVRA_U, 0);
      } else {
        /* down scale */
        SetDecRegister(dec_cont->h264_regs, HWIF_HOR_SCALE_MODE_U, 2);
        SetDecRegister(dec_cont->h264_regs, HWIF_WSCALE_INVRA_U,
                       1<<(16-dec_cont->storage.down_scale_x_shift));
      }

      if (dec_cont->storage.down_scale_y_shift == 0) {
        SetDecRegister(dec_cont->h264_regs, HWIF_VER_SCALE_MODE_U, 0);
        SetDecRegister(dec_cont->h264_regs, HWIF_HSCALE_INVRA_U, 0);
      } else {
        /* down scale */
        SetDecRegister(dec_cont->h264_regs, HWIF_VER_SCALE_MODE_U, 2);
        SetDecRegister(dec_cont->h264_regs, HWIF_HSCALE_INVRA_U,
                       1<<(16-dec_cont->storage.down_scale_y_shift));
      }

      SET_ADDR64_REG(dec_cont->h264_regs, HWIF_PP_OUT_LU_BASE_U,
                     p_asic_buff->out_pp_buffer->bus_address);
      SET_ADDR64_REG(dec_cont->h264_regs, HWIF_PP_OUT_CH_BASE_U,
                     p_asic_buff->out_pp_buffer->bus_address + dsw * dsh);

      SetPpRegister(dec_cont->h264_regs, HWIF_PP_IN_FORMAT_U, 1);
    }

#endif
    if(dec_cont->h264_profile_support != H264_BASELINE_PROFILE) {
      u32 core_id = dec_cont->b_mc ? dec_cont->core_id : 0;

      h264PrepareScaleList(dec_cont);

      SET_ADDR_REG(dec_cont->h264_regs, HWIF_QTABLE_BASE,
                   p_asic_buff->cabac_init[core_id].bus_address);
    }

    H264FlushRegs(dec_cont);

    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_E, 1);

    if(dec_cont->b_mc) {
      h264CopyPocToHw(dec_cont);
      h264MCSetHwRdyCallback(dec_cont);
    }

    DWLEnableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                dec_cont->h264_regs[1]);

    if(dec_cont->b_mc) {
      /* reset shadow HW status reg values so that we dont end up writing
       * some garbage to next Core regs */
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_E, 0);
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT, 0);
      SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ, 0);

      dec_cont->asic_running = 0;
      dec_cont->keep_hw_reserved = 0;
      return DEC_8190_IRQ_RDY;
    }
  } else { /* buffer was empty and now we restart with new stream values */
    ASSERT(!dec_cont->b_mc);

    h264StreamPosUpdate(dec_cont);

    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 5, dec_cont->h264_regs[5]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 6, dec_cont->h264_regs[6]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 12, dec_cont->h264_regs[12]);
#ifdef USE_64BIT_ENV
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 122, dec_cont->h264_regs[122]);
#endif

    /* start HW by clearing IRQ_BUFFER_EMPTY status bit */
    DWLEnableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1, dec_cont->h264_regs[1]);
  }

  ret = DWLWaitHwReady(dec_cont->dwl, dec_cont->core_id, (u32) DEC_X170_TIMEOUT_LENGTH);

  if(ret != DWL_HW_WAIT_OK) {
    ERROR_PRINT("DWLWaitHwReady");
    DEBUG_PRINT(("DWLWaitHwReady returned: %d\n", ret));

    /* reset HW */
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ, 0);
    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_E, 0);

    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1, dec_cont->h264_regs[1]);

    /* Wait for PP to end also */
    if(dec_cont->pp.pp_instance != NULL &&
        (dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING ||
         dec_cont->pp.dec_pp_if.pp_status == DECPP_PIC_NOT_FINISHED)) {
      dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

      TRACE_PP_CTRL("H264RunAsic: PP Wait for end\n");

      dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

      TRACE_PP_CTRL("H264RunAsic: PP Finished\n");
    }

    dec_cont->asic_running = 0;
    dec_cont->keep_hw_reserved = 0;
    DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

    /* Decrement usage for DPB buffers */
    DecrementDPBRefCount(&storage->dpb[1]);

    return (ret == DWL_HW_WAIT_ERROR) ?
           X170_DEC_SYSTEM_ERROR : X170_DEC_TIMEOUT;
  }

  H264RefreshRegs(dec_cont);

  /* React to the HW return value */

  asic_status = GetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT);

  SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT, 0);
  SetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ, 0);    /* just in case */

  /* any B stuff detected by HW? (note reg name changed from
   * b_detected to pic_inf) */
  if(GetDecRegister(dec_cont->h264_regs, HWIF_DEC_PIC_INF)) {
    ASSERT(dec_cont->h264_profile_support != H264_BASELINE_PROFILE);
    if((dec_cont->h264_profile_support != H264_BASELINE_PROFILE) &&
        (p_asic_buff->enable_dmv_and_poc == 0)) {
      DEBUG_PRINT(("HW Detected B slice in baseline profile stream!!!\n"));
      p_asic_buff->enable_dmv_and_poc = 1;
    }

    SetDecRegister(dec_cont->h264_regs, HWIF_DEC_PIC_INF, 0);
  }

  if(!(asic_status & DEC_8190_IRQ_BUFFER)) {
    /* HW done, release it! */
    dec_cont->asic_running = 0;

    dec_cont->keep_hw_reserved = 0;
    /* Wait for PP to end also */
    if(dec_cont->pp.pp_instance != NULL &&
        (dec_cont->pp.dec_pp_if.pp_status == DECPP_RUNNING ||
         dec_cont->pp.dec_pp_if.pp_status == DECPP_PIC_NOT_FINISHED)) {
      /* first field of a frame finished -> decode second one before
       * waiting for PP to finish */
      if ((dec_cont->storage.num_views == 0 ?
           dec_cont->storage.second_field :
           dec_cont->storage.slice_header->field_pic_flag &&
           dec_cont->storage.view == 0) &&
          /*!dec_cont->storage.num_views &&*/
          (asic_status & DEC_8190_IRQ_RDY)) {
        dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_NOT_FINISHED;
        dec_cont->keep_hw_reserved = 1;
      } else {
        dec_cont->pp.dec_pp_if.pp_status = DECPP_PIC_READY;

        TRACE_PP_CTRL("H264RunAsic: PP Wait for end\n");

        dec_cont->pp.PPDecWaitEnd(dec_cont->pp.pp_instance);

        TRACE_PP_CTRL("H264RunAsic: PP Finished\n");
      }
    }

    if (!dec_cont->keep_hw_reserved)
      DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

    /* Decrement usage for DPB buffers */
    DecrementDPBRefCount(&storage->dpb[1]);
  }

  /* update reference buffer stat */
  if(dec_cont->asic_running == 0 &&
      (asic_status & DEC_8190_IRQ_RDY)) {
    u32 *p_mv = p_asic_buff->out_buffer->virtual_address;
    u32 tmp = dpb->dir_mv_offset;

    if(p_slice_header->field_pic_flag && p_slice_header->bottom_field_flag) {
      tmp += dpb->pic_size_in_mbs * 32;
    }
    p_mv = (u32 *) ((u8*)p_mv + tmp);

    if(dec_cont->ref_buf_support &&
        !IS_B_SLICE(dec_cont->storage.slice_header->slice_type)) {
      RefbuMvStatistics(&dec_cont->ref_buffer_ctrl, dec_cont->h264_regs,
                        p_mv, p_asic_buff->enable_dmv_and_poc,
                        IS_IDR_NAL_UNIT(dec_cont->storage.prev_nal_unit));
    } else if ( dec_cont->ref_buf_support ) {
      RefbuMvStatisticsB( &dec_cont->ref_buffer_ctrl, dec_cont->h264_regs );
    }
  }

  if(!dec_cont->rlc_mode) {
    if (!dec_cont->secure_mode) {
      addr_t last_read_address;
      u32 bytes_processed;
      const addr_t start_address =
        dec_cont->hw_stream_start_bus & (~DEC_X170_ALIGN_MASK);
      const u32 offset_bytes =
        dec_cont->hw_stream_start_bus & DEC_X170_ALIGN_MASK;

      last_read_address =
        GET_ADDR_REG(dec_cont->h264_regs, HWIF_RLC_VLC_BASE);

      bytes_processed = last_read_address - start_address;

      DEBUG_PRINT(("HW updated stream position: %16x\n"
                   "           processed bytes: %8d\n"
                   "     of which offset bytes: %8d\n",
                   last_read_address, bytes_processed, offset_bytes));

      if(asic_status & DEC_8190_IRQ_BUFFER) {
        dec_cont->p_hw_stream_start += dec_cont->hw_length;
        dec_cont->hw_stream_start_bus += dec_cont->hw_length;
        dec_cont->hw_length = 0; /* no bytes left */
      } else if(!(asic_status & DEC_8190_IRQ_ASO)) {
        /* from start of the buffer add what HW has decoded */

        /* end - start smaller or equal than maximum */
        if((bytes_processed - offset_bytes) > dec_cont->hw_length) {

          if(!(asic_status & ( DEC_8190_IRQ_ABORT |
                               DEC_8190_IRQ_ERROR |
                               DEC_8190_IRQ_TIMEOUT |
                               DEC_8190_IRQ_BUS ))) {
            DEBUG_PRINT(("New stream position out of range!\n"));
            DEBUG_PRINT(("Buffer size %d, and next start would be %d\n",
                         dec_cont->hw_length,
                         (bytes_processed - offset_bytes)));
            ASSERT(0);
          }

          /* consider all buffer processed */
          dec_cont->p_hw_stream_start += dec_cont->hw_length;
          dec_cont->hw_stream_start_bus += dec_cont->hw_length;
          dec_cont->hw_length = 0; /* no bytes left */
        } else {
          dec_cont->hw_length -= (bytes_processed - offset_bytes);
          dec_cont->p_hw_stream_start += (bytes_processed - offset_bytes);
          dec_cont->hw_stream_start_bus += (bytes_processed - offset_bytes);
        }

      }
      /* else will continue decoding from the beginning of buffer */
    } else {
      dec_cont->p_hw_stream_start += dec_cont->hw_length;
      dec_cont->hw_stream_start_bus += dec_cont->hw_length;
      dec_cont->hw_length = 0; /* no bytes left */
    }

    dec_cont->stream_pos_updated = 1;
  }

  return asic_status;
}

/*------------------------------------------------------------------------------
    Function name : PrepareMvData
    Description   :

    Return type   : void
    Argument      : storage_t * storage
    Argument      : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
void PrepareMvData(storage_t * storage, DecAsicBuffers_t * p_asic_buff) {
  const mbStorage_t *p_mb = storage->mb;

  u32 mbs = storage->pic_size_in_mbs;
  u32 tmp, n;
  u32 *p_mv_ctrl, *p_mv_ctrl_base = p_asic_buff->mv.virtual_address;
  const u32 *p_mb_ctrl = p_asic_buff->mb_ctrl.virtual_address;

  if(p_asic_buff->whole_pic_concealed != 0) {
    if(p_mb->mb_type_asic == P_Skip) {
      tmp = p_mb->ref_id[0];

      p_mv_ctrl = p_mv_ctrl_base;

      for(n = mbs; n > 0; n--) {
        *p_mv_ctrl = tmp;
        p_mv_ctrl += (ASIC_MB_MV_BUFFER_SIZE / 4);
      }
    }

    return;
  }

  for(n = mbs; n > 0; n--, p_mb++, p_mv_ctrl_base += (ASIC_MB_MV_BUFFER_SIZE / 4),
      p_mb_ctrl += (ASIC_MB_CTRL_BUFFER_SIZE / 4)) {
    const mv_t *mv = p_mb->mv;

    p_mv_ctrl = p_mv_ctrl_base;

    switch (p_mb->mb_type_asic) {
    case P_Skip:
      tmp = p_mb->ref_id[0];
      *p_mv_ctrl++ = tmp;
      break;

    case P_L0_16x16:
      tmp = ((u32) (mv[0].hor & ASIC_HOR_MV_MASK)) << ASIC_HOR_MV_OFFSET;
      tmp |= ((u32) (mv[0].ver & ASIC_VER_MV_MASK)) << ASIC_VER_MV_OFFSET;
      tmp |= p_mb->ref_id[0];
      *p_mv_ctrl++ = tmp;

      break;
    case P_L0_L0_16x8:
      tmp = ((u32) (mv[0].hor & ASIC_HOR_MV_MASK)) << ASIC_HOR_MV_OFFSET;
      tmp |= ((u32) (mv[0].ver & ASIC_VER_MV_MASK)) << ASIC_VER_MV_OFFSET;
      tmp |= p_mb->ref_id[0];

      *p_mv_ctrl++ = tmp;

      tmp = ((u32) (mv[8].hor & ASIC_HOR_MV_MASK)) << ASIC_HOR_MV_OFFSET;
      tmp |= ((u32) (mv[8].ver & ASIC_VER_MV_MASK)) << ASIC_VER_MV_OFFSET;
      tmp |= p_mb->ref_id[1];

      *p_mv_ctrl++ = tmp;

      break;
    case P_L0_L0_8x16:
      tmp = ((u32) (mv[0].hor & ASIC_HOR_MV_MASK)) << ASIC_HOR_MV_OFFSET;
      tmp |= ((u32) (mv[0].ver & ASIC_VER_MV_MASK)) << ASIC_VER_MV_OFFSET;
      tmp |= p_mb->ref_id[0];

      *p_mv_ctrl++ = tmp;

      tmp = ((u32) (mv[4].hor & ASIC_HOR_MV_MASK)) << ASIC_HOR_MV_OFFSET;
      tmp |= ((u32) (mv[4].ver & ASIC_VER_MV_MASK)) << ASIC_VER_MV_OFFSET;
      tmp |= p_mb->ref_id[1];

      *p_mv_ctrl++ = tmp;

      break;
    case P_8x8:
    case P_8x8ref0: {
      u32 i;
      u32 subMbType = *p_mb_ctrl;

      for(i = 0; i < 4; i++) {
        switch ((subMbType >> (u32) (27 - 2 * i)) & 0x03) {
        case MB_SP_8x8:
          tmp =
            ((u32) ((*mv).hor & ASIC_HOR_MV_MASK)) <<
            ASIC_HOR_MV_OFFSET;
          tmp |=
            ((u32) ((*mv).ver & ASIC_VER_MV_MASK)) <<
            ASIC_VER_MV_OFFSET;
          tmp |= p_mb->ref_id[i];
          *p_mv_ctrl++ = tmp;
          mv += 4;
          break;

        case MB_SP_8x4:
          tmp =
            ((u32) ((*mv).hor & ASIC_HOR_MV_MASK)) <<
            ASIC_HOR_MV_OFFSET;
          tmp |=
            ((u32) ((*mv).ver & ASIC_VER_MV_MASK)) <<
            ASIC_VER_MV_OFFSET;
          tmp |= p_mb->ref_id[i];
          *p_mv_ctrl++ = tmp;
          mv += 2;

          tmp =
            ((u32) ((*mv).hor & ASIC_HOR_MV_MASK)) <<
            ASIC_HOR_MV_OFFSET;
          tmp |=
            ((u32) ((*mv).ver & ASIC_VER_MV_MASK)) <<
            ASIC_VER_MV_OFFSET;
          tmp |= p_mb->ref_id[i];
          *p_mv_ctrl++ = tmp;
          mv += 2;
          break;

        case MB_SP_4x8:
          tmp =
            ((u32) ((*mv).hor & ASIC_HOR_MV_MASK)) <<
            ASIC_HOR_MV_OFFSET;
          tmp |=
            ((u32) ((*mv).ver & ASIC_VER_MV_MASK)) <<
            ASIC_VER_MV_OFFSET;
          tmp |= p_mb->ref_id[i];
          *p_mv_ctrl++ = tmp;
          mv++;
          tmp =
            ((u32) ((*mv).hor & ASIC_HOR_MV_MASK)) <<
            ASIC_HOR_MV_OFFSET;
          tmp |=
            ((u32) ((*mv).ver & ASIC_VER_MV_MASK)) <<
            ASIC_VER_MV_OFFSET;
          tmp |= p_mb->ref_id[i];
          *p_mv_ctrl++ = tmp;
          mv += 3;

          break;

        case MB_SP_4x4: {
          u32 j;

          for(j = 4; j > 0; j--) {
            tmp =
              ((u32) ((*mv).hor & ASIC_HOR_MV_MASK)) <<
              ASIC_HOR_MV_OFFSET;
            tmp |=
              ((u32) ((*mv).ver & ASIC_VER_MV_MASK)) <<
              ASIC_VER_MV_OFFSET;
            tmp |= p_mb->ref_id[i];
            *p_mv_ctrl++ = tmp;
            mv++;
          }
        }
        break;
        default:
          ASSERT(0);
        }
      }
    }
    break;
    default:
      break;
    }
  }
}

/*------------------------------------------------------------------------------
    Function name : PrepareIntra4x4ModeData
    Description   :

    Return type   : void
    Argument      : storage_t * storage
    Argument      : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
void PrepareIntra4x4ModeData(storage_t * storage, DecAsicBuffers_t * p_asic_buff) {
  u32 n;
  u32 mbs = storage->pic_size_in_mbs;
  u32 *p_intra_pred, *p_intra_pred_base = p_asic_buff->intra_pred.virtual_address;
  const mbStorage_t *p_mb = storage->mb;

  if(p_asic_buff->whole_pic_concealed != 0) {
    return;
  }

  /* write out "INTRA4x4 mode" to ASIC */
  for(n = mbs; n > 0;
      n--, p_mb++, p_intra_pred_base += (ASIC_MB_I4X4_BUFFER_SIZE / 4)) {
    u32 tmp = 0, block;

    p_intra_pred = p_intra_pred_base;

    if(h264bsdMbPartPredMode(p_mb->mb_type_asic) != PRED_MODE_INTRA4x4)
      continue;

    for(block = 0; block < 16; block++) {
      u8 mode = p_mb->intra4x4_pred_mode_asic[block];

      tmp <<= 4;
      tmp |= mode;

      if(block == 7) {
        *p_intra_pred++ = tmp;
        tmp = 0;
      }
    }
    *p_intra_pred++ = tmp;
  }
}

/*------------------------------------------------------------------------------
    Function name   : PrepareRlcCount
    Description     :
    Return type     : void
    Argument        : storage_t * storage
    Argument        : DecAsicBuffers_t * p_asic_buff
------------------------------------------------------------------------------*/
void PrepareRlcCount(storage_t * storage, DecAsicBuffers_t * p_asic_buff) {
  u32 n;
  u32 mbs = storage->pic_size_in_mbs;
  u32 *p_mb_ctrl = p_asic_buff->mb_ctrl.virtual_address + 1;

  if(p_asic_buff->whole_pic_concealed != 0) {
    return;
  }

  for(n = mbs - 1; n > 0; n--, p_mb_ctrl += (ASIC_MB_CTRL_BUFFER_SIZE / 4)) {
    u32 tmp = (*(p_mb_ctrl + (ASIC_MB_CTRL_BUFFER_SIZE / 4))) << 4;

    (*p_mb_ctrl) |= (tmp >> 23);
  }
}

/*------------------------------------------------------------------------------
    Function name   : H264RefreshRegs
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
static void H264RefreshRegs(decContainer_t * dec_cont) {
  i32 i;
  u32 offset = 0x0;

  u32 *dec_regs = dec_cont->h264_regs;

  for(i = DEC_X170_REGISTERS; i > 0; --i) {
    *dec_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  dec_regs =  dec_cont->h264_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *dec_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name   : H264FlushRegs
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
static void H264FlushRegs(decContainer_t * dec_cont) {
  i32 i;
  u32 offset = 0x04;
  const u32 *dec_regs = dec_cont->h264_regs + 1;    /* Don't write ID reg */

#ifdef TRACE_START_MARKER
  /* write ID register to trigger logic analyzer */
  DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 0x00, ~0);
#endif

  /* don not start HW by this flush */
  ASSERT(GetDecRegister(dec_cont->h264_regs, HWIF_DEC_E) == 0);
  ASSERT(GetDecRegister(dec_cont->h264_regs, HWIF_DEC_IRQ_STAT) == 0);

  for(i = DEC_X170_REGISTERS; i > 1; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  dec_regs =  dec_cont->h264_regs + TOTAL_X170_ORIGIN_REGS;
  for(i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#endif
#if 0
  offset = PP_START_REGS * 0x04;
  dec_regs =  dec_cont->h264_regs + PP_START_REGS;
  for(i = TOTAL_X170_EXPAND_PP_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#else
  offset = PP_START_UNIFIED_REGS * 0x04;
  dec_regs =  dec_cont->h264_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
    dec_regs++;
    offset += 4;
  }
#endif
}

/*------------------------------------------------------------------------------
    Function name : H264SetupVlcRegs
    Description   : set up registers for vlc mode

    Return type   :
    Argument      : container
------------------------------------------------------------------------------*/
void H264SetupVlcRegs(decContainer_t * dec_cont) {
  u32 tmp, i = 0;
  //u32 j = 0;
  u32 long_termflags = 0;
  u32 valid_flags = 0;
  u32 long_term_tmp = 0;
  i32 diff_poc, curr_poc, itmp;
  i32 tmp_frame_num = 0;

  const seqParamSet_t *p_sps = dec_cont->storage.active_sps;
  const sliceHeader_t *p_slice_header = dec_cont->storage.slice_header;
  const picParamSet_t *p_pps = dec_cont->storage.active_pps;
  const dpbStorage_t *p_dpb = dec_cont->storage.dpb;
  const storage_t *storage = &dec_cont->storage;
  const u32 is8190 = dec_cont->is8190;

  SetDecRegister(dec_cont->h264_regs, HWIF_DEC_OUT_DIS, 0);

  SetDecRegister(dec_cont->h264_regs, HWIF_RLC_MODE_E, 0);

  SetDecRegister(dec_cont->h264_regs, HWIF_INIT_QP, p_pps->pic_init_qp);

  SetDecRegister(dec_cont->h264_regs, HWIF_REFIDX0_ACTIVE,
                 p_pps->num_ref_idx_l0_active);

  SetDecRegister(dec_cont->h264_regs, HWIF_REF_FRAMES, p_sps->num_ref_frames);

  i = 0;
  while(p_sps->max_frame_num >> i) {
    i++;
  }
  SetDecRegister(dec_cont->h264_regs, HWIF_FRAMENUM_LEN, i - 1);

  SetDecRegister(dec_cont->h264_regs, HWIF_FRAMENUM,
                 p_slice_header->frame_num & ~(dec_cont->frame_num_mask));

  SetDecRegister(dec_cont->h264_regs, HWIF_CONST_INTRA_E,
                 p_pps->constrained_intra_pred_flag);

  SetDecRegister(dec_cont->h264_regs, HWIF_FILT_CTRL_PRES,
                 p_pps->deblocking_filter_control_present_flag);

  SetDecRegister(dec_cont->h264_regs, HWIF_RDPIC_CNT_PRES,
                 p_pps->redundant_pic_cnt_present_flag);

  SetDecRegister(dec_cont->h264_regs, HWIF_REFPIC_MK_LEN,
                 p_slice_header->dec_ref_pic_marking.strm_len);

  SetDecRegister(dec_cont->h264_regs, HWIF_IDR_PIC_E,
                 IS_IDR_NAL_UNIT(storage->prev_nal_unit));
  SetDecRegister(dec_cont->h264_regs, HWIF_IDR_PIC_ID, p_slice_header->idr_pic_id);
  SetDecRegister(dec_cont->h264_regs, HWIF_PPS_ID, storage->active_pps_id);
  SetDecRegister(dec_cont->h264_regs, HWIF_POC_LENGTH,
                 p_slice_header->poc_length_hw);

  /* reference picture flags */

  /* TODO separate fields */
  if(p_slice_header->field_pic_flag) {
    ASSERT(dec_cont->h264_profile_support != H264_BASELINE_PROFILE);

    for(i = 0; i < 32; i++) {
      long_term_tmp = p_dpb->buffer[i / 2].status[i & 1] == 3;
      long_termflags = long_termflags << 1 | long_term_tmp;

      /* For case TOPFIELD (I) + BOTTOM (P), and BOTTOM P refers to P itself. */
      if (p_slice_header->bottom_field_flag &&
          p_slice_header->num_ref_idx_l0_active > 1 &&
          p_dpb->current_out == &p_dpb->buffer[i/2] &&
          (p_dpb->current_out->pic_code_type[0] == DEC_PIC_TYPE_I
           && p_dpb->current_out->is_idr[0] == 0) &&
          IS_P_SLICE(p_slice_header->slice_type) &&
          (i & 1) && dec_cont->pic_number == 1)
        tmp = h264bsdGetRefPicDataVlcMode(p_dpb, i-1, 1) != NULL;
      else
        tmp = h264bsdGetRefPicDataVlcMode(p_dpb, i, 1) != NULL;

      valid_flags = valid_flags << 1 | tmp;
    }
    SetDecRegister(dec_cont->h264_regs, HWIF_REFER_LTERM_E, long_termflags);
//        SetDecRegister(dec_cont->h264_regs, HWIF_REFER_VALID_E, valid_flags);
  } else {
    for(i = 0; i < 16; i++) {
      u32 n = is8190 ? i : storage->dpb[0].list[i];

      long_term_tmp = p_dpb->buffer[n].status[0] == 3 &&
                      p_dpb->buffer[n].status[1] == 3;
      long_termflags = long_termflags << 1 | long_term_tmp;

      tmp = h264bsdGetRefPicDataVlcMode(p_dpb, n, 0) != NULL;
      valid_flags = valid_flags << 1 | tmp;
    }
    valid_flags <<= 16;

    SetDecRegister(dec_cont->h264_regs, HWIF_REFER_LTERM_E,
                   long_termflags << 16);
//        SetDecRegister(dec_cont->h264_regs, HWIF_REFER_VALID_E,
//                       valid_flags << 16);
  }

  if(p_slice_header->field_pic_flag) {
    curr_poc = storage->poc->pic_order_cnt[p_slice_header->bottom_field_flag];
  } else {
    curr_poc = MIN(storage->poc->pic_order_cnt[0],
                   storage->poc->pic_order_cnt[1]);
  }
  for(i = 0; i < 16; i++) {
    u32 n = is8190 ? i : p_dpb->list[i];

    if(p_dpb->buffer[n].status[0] == 3 || p_dpb->buffer[n].status[1] == 3) {
      SetDecRegister(dec_cont->h264_regs, ref_pic_num[i],
                     p_dpb->buffer[n].pic_num);
    } else {
      /* if frame_num workaround activated and bit 12 of frame_num is
       * non-zero -> modify frame numbers of reference pictures so
       * that reference picture list reordering works as usual */
      if (p_slice_header->frame_num & dec_cont->frame_num_mask) {
        i32 tmp = p_dpb->buffer[n].frame_num - dec_cont->frame_num_mask;
        if (tmp < 0) tmp += p_sps->max_frame_num;
        SetDecRegister(dec_cont->h264_regs, ref_pic_num[i], tmp);
      } else {
        /*FIXME: below caode may cause side-effect, need to be refined */
#if 0
        if ((p_slice_header->ref_pic_list_reordering.ref_pic_list_reordering_flag_l0 ||
             p_slice_header->ref_pic_list_reordering_l1.ref_pic_list_reordering_flag_l0)) {
          if (IsExisting(&p_dpb->buffer[n], FRAME) ||
              (p_slice_header->field_pic_flag && p_slice_header->bottom_field_flag &&
               p_dpb->current_out == &p_dpb->buffer[n] && IsExisting(&p_dpb->buffer[n], TOPFIELD)))
            tmp_frame_num = p_dpb->buffer[n].frame_num;
          else if (j < p_dpb->invalid_pic_num_count) {
            /* next invalid pic_num */
            tmp_frame_num = p_dpb->pic_num_invalid[j++];
            if(p_slice_header->field_pic_flag)
              valid_flags |= 0x3 << (30-2*n);
            else
              valid_flags |= 1 << (31-n);
          }
        } else
#endif
        {
          tmp_frame_num = p_dpb->buffer[n].frame_num;
        }
        SetDecRegister(dec_cont->h264_regs, ref_pic_num[i], tmp_frame_num);

      }

    }
    diff_poc = ABS(p_dpb->buffer[i].pic_order_cnt[0] - curr_poc);
    itmp = ABS(p_dpb->buffer[i].pic_order_cnt[1] - curr_poc);

    if(dec_cont->asic_buff->ref_pic_list[i])
      dec_cont->asic_buff->ref_pic_list[i] |=
        (diff_poc < itmp ? 0x1 : 0) | (p_dpb->buffer[i].is_field_pic ? 0x2 : 0);
  }

  SetDecRegister(dec_cont->h264_regs, HWIF_REFER_VALID_E, valid_flags);

  if(dec_cont->h264_profile_support != H264_BASELINE_PROFILE) {
    h264PreparePOC(dec_cont);

    SetDecRegister(dec_cont->h264_regs, HWIF_CABAC_E,
                   p_pps->entropy_coding_mode_flag);
  }

  h264StreamPosUpdate(dec_cont);
}

/*------------------------------------------------------------------------------
    Function name   : H264InitRefPicList1
    Description     :
    Return type     : void
    Argument        : decContainer_t *dec_cont
    Argument        : u32 *list0
    Argument        : u32 *list1
------------------------------------------------------------------------------*/
void H264InitRefPicList1(decContainer_t * dec_cont, u32 * list0, u32 * list1) {
  u32 tmp, i;
  u32 idx, idx0, idx1, idx2;
  i32 ref_poc;
  const storage_t *storage = &dec_cont->storage;
  const dpbPicture_t *pic;

  ref_poc = MIN(storage->poc->pic_order_cnt[0], storage->poc->pic_order_cnt[1]);
  i = 0;

  pic = storage->dpb->buffer;
  while(IS_SHORT_TERM_FRAME(pic[list0[i]]) &&
        MIN(pic[list0[i]].pic_order_cnt[0], pic[list0[i]].pic_order_cnt[1]) <
        ref_poc)
    i++;

  idx0 = i;

  while(IS_SHORT_TERM_FRAME(pic[list0[i]]))
    i++;

  idx1 = i;

  while(IS_LONG_TERM_FRAME(pic[list0[i]]))
    i++;

  idx2 = i;

  /* list L1 */
  for(i = idx0, idx = 0; i < idx1; i++, idx++)
    list1[idx] = list0[i];

  for(i = 0; i < idx0; i++, idx++)
    list1[idx] = list0[i];

  for(i = idx1; idx < 16; idx++, i++)
    list1[idx] = list0[i];

  if(idx2 > 1) {
    tmp = 0;
    for(i = 0; i < idx2; i++) {
      tmp += (list0[i] != list1[i]) ? 1 : 0;
    }
    /* lists are equal -> switch list1[0] and list1[1] */
    if(!tmp) {
      i = list1[0];
      list1[0] = list1[1];
      list1[1] = i;
    }
  }
}

/*------------------------------------------------------------------------------
    Function name   : H264InitRefPicList1F
    Description     :
    Return type     : void
    Argument        : decContainer_t *dec_cont
    Argument        : u32 *list0
    Argument        : u32 *list1
------------------------------------------------------------------------------*/
void H264InitRefPicList1F(decContainer_t * dec_cont, u32 * list0, u32 * list1) {
  u32 i;
  u32 idx, idx0, idx1;
  i32 ref_poc;
  const storage_t *storage = &dec_cont->storage;
  const dpbPicture_t *pic;

  ref_poc =
    storage->poc->pic_order_cnt[storage->slice_header[0].bottom_field_flag];
  i = 0;

  pic = storage->dpb->buffer;
  while(IS_SHORT_TERM_FRAME_F(pic[list0[i]]) &&
        FIELD_POC(pic[list0[i]]) <= ref_poc)
    i++;

  idx0 = i;

  while(IS_SHORT_TERM_FRAME_F(pic[list0[i]]))
    i++;

  idx1 = i;

  /* list L1 */
  for(i = idx0, idx = 0; i < idx1; i++, idx++)
    list1[idx] = list0[i];

  for(i = 0; i < idx0; i++, idx++)
    list1[idx] = list0[i];

  for(i = idx1; idx < 16; idx++, i++)
    list1[idx] = list0[i];

}

/*------------------------------------------------------------------------------
    Function name   : H264InitRefPicList
    Description     :
    Return type     : void
    Argument        : decContainer_t *dec_cont
------------------------------------------------------------------------------*/
void H264InitRefPicList(decContainer_t * dec_cont) {
  sliceHeader_t *p_slice_header = dec_cont->storage.slice_header;
  pocStorage_t *poc = dec_cont->storage.poc;
  dpbStorage_t *dpb = dec_cont->storage.dpb;
  u32 i, is_idr;
  u32 list0[34] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33
  };
  u32 list1[34] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33
  };
  u32 list_p[34] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33
  };

  /* determine if alternate view is IDR -> reference only inter-view ref
   * pic */
  is_idr = IS_IDR_NAL_UNIT(dec_cont->storage.prev_nal_unit);

  /* B lists */
  if(!dec_cont->rlc_mode) {
    if(p_slice_header->field_pic_flag) {
      /* list 0 */
      ShellSortF(dpb, list0, 1,
                 poc->pic_order_cnt[p_slice_header->bottom_field_flag]);
      if (dec_cont->storage.view && !dec_cont->storage.non_inter_view_ref) {
        i = 0;
        while (!is_idr && IS_REF_FRAME_F(dpb->buffer[list0[i]])) i++;
        list0[i] = 15;
      }
      /* list 1 */
      H264InitRefPicList1F(dec_cont, list0, list1);
      for(i = 0; i < 16; i++) {
        SetDecRegister(dec_cont->h264_regs, ref_pic_list0[i], list0[i]);
        SetDecRegister(dec_cont->h264_regs, ref_pic_list1[i], list1[i]);
      }
    } else {
      /* list 0 */
      ShellSort(dpb, list0, 1,
                MIN(poc->pic_order_cnt[0], poc->pic_order_cnt[1]));
      if (dec_cont->storage.view && !dec_cont->storage.non_inter_view_ref) {
        i = 0;
        while (!is_idr && IS_REF_FRAME(dpb->buffer[list0[i]])) i++;
        list0[i] = 15;
      }
      /* list 1 */
      H264InitRefPicList1(dec_cont, list0, list1);
      for(i = 0; i < 16; i++) {
        SetDecRegister(dec_cont->h264_regs, ref_pic_list0[i], list0[i]);
        SetDecRegister(dec_cont->h264_regs, ref_pic_list1[i], list1[i]);
      }
    }
  }

  /* P list */
  if(p_slice_header->field_pic_flag) {
    if(!dec_cont->rlc_mode) {
      ShellSortF(dpb, list_p, 0, 0);
      if (dec_cont->storage.view && !dec_cont->storage.non_inter_view_ref) {
        i = 0;
        while (!is_idr && IS_REF_FRAME_F(dpb->buffer[list_p[i]])) i++;
        list_p[i] = 15;
      }
      for(i = 0; i < 16; i++) {
        SetDecRegister(dec_cont->h264_regs, ref_pic_list_p[i], list_p[i]);

        /* copy to dpb for error handling purposes */
        dpb[0].list[i] = list_p[i];
        dpb[1].list[i] = list_p[i];
      }
    }
  } else {
    ShellSort(dpb, list_p, 0, 0);
    if (dec_cont->storage.view && !dec_cont->storage.non_inter_view_ref) {
      i = 0;
      while (!is_idr && IS_REF_FRAME(dpb->buffer[list_p[i]])) i++;
      list_p[i] = 15;
    }
    for(i = 0; i < 16; i++) {
      if(!dec_cont->rlc_mode)
        SetDecRegister(dec_cont->h264_regs, ref_pic_list_p[i], list_p[i]);
      /* copy to dpb for error handling purposes */
      dpb[0].list[i] = list_p[i];
      dpb[1].list[i] = list_p[i];
    }
  }
}

/*------------------------------------------------------------------------------
    Function name : h264StreamPosUpdate
    Description   : Set stream base and length related registers

    Return type   :
    Argument      : container
------------------------------------------------------------------------------*/
void h264StreamPosUpdate(decContainer_t * dec_cont) {
  addr_t tmp;
  const u8 *p_tmp;

  DEBUG_PRINT(("h264StreamPosUpdate:\n"));
  tmp = 0;

  p_tmp = dec_cont->p_hw_stream_start;

  /* NAL start prefix in stream start is 0 0 0 or 0 0 1 */
  if(!(*dec_cont->p_hw_stream_start + *(dec_cont->p_hw_stream_start + 1))) {
    if(*(dec_cont->p_hw_stream_start + 2) < 2) {
      tmp = 1;
    }
  }

  /* stuff related to frame_num workaround, if used -> advance stream by
   * start code prefix and HW is set to decode in NAL unit mode */
  if (tmp && dec_cont->force_nal_mode) {
    tmp = 0;
    /* skip start code prefix */
    while (*p_tmp++ == 0);

    dec_cont->hw_stream_start_bus += p_tmp - dec_cont->p_hw_stream_start;
    dec_cont->p_hw_stream_start = p_tmp;
    dec_cont->hw_length -= p_tmp - dec_cont->p_hw_stream_start;
  }

  DEBUG_PRINT(("\tByte stream   %8d\n", tmp));
  SetDecRegister(dec_cont->h264_regs, HWIF_START_CODE_E, tmp);

  /* bit offset if base is unaligned */
  tmp = (dec_cont->hw_stream_start_bus & DEC_X170_ALIGN_MASK) * 8;

  DEBUG_PRINT(("\tStart bit pos %8d\n", tmp));

  SetDecRegister(dec_cont->h264_regs, HWIF_STRM_START_BIT, tmp);

  dec_cont->hw_bit_pos = tmp;

  tmp = dec_cont->hw_stream_start_bus;   /* unaligned base */
  tmp &= (~DEC_X170_ALIGN_MASK);  /* align the base */

  DEBUG_PRINT(("\tStream base   %08x\n", tmp));
  SET_ADDR_REG(dec_cont->h264_regs, HWIF_RLC_VLC_BASE, tmp);

  tmp = dec_cont->hw_length;   /* unaligned stream */
  tmp += dec_cont->hw_bit_pos / 8;  /* add the alignmet bytes */

  DEBUG_PRINT(("\tStream length %8d\n", tmp));
  SetDecRegister(dec_cont->h264_regs, HWIF_STREAM_LEN, tmp);

}

/*------------------------------------------------------------------------------
    Function name : H264PrepareCabacInitTables
    Description   : Prepare CABAC initialization tables

    Return type   : void
    Argument      : u32 *cabac_init
------------------------------------------------------------------------------*/
void H264PrepareCabacInitTables(u32 *cabac_init) {
  ASSERT(cabac_init != NULL);
  (void) DWLmemcpy(cabac_init, cabac_init_values, 4 * 460 * 2);
}

void h264CopyPocToHw(decContainer_t *dec_cont) {
  const u32 core_id = dec_cont->b_mc ? dec_cont->core_id : 0;
  struct DWLLinearMem *cabac_mem = dec_cont->asic_buff->cabac_init + core_id;
  i32 *poc_base = (i32 *) ((u8 *) cabac_mem->virtual_address +
                           ASIC_CABAC_INIT_BUFFER_SIZE);

  if(dec_cont->asic_buff->enable_dmv_and_poc)
    DWLmemcpy(poc_base, dec_cont->poc, sizeof(dec_cont->poc));
}

void h264PreparePOC(decContainer_t *dec_cont) {
  const sliceHeader_t *p_slice_header = dec_cont->storage.slice_header;
  const seqParamSet_t *p_sps = dec_cont->storage.active_sps;
  const dpbStorage_t *p_dpb = dec_cont->storage.dpb;
  const pocStorage_t *poc = dec_cont->storage.poc;

  int i;

  i32 *poc_base, curr_poc;

  if(dec_cont->b_mc) {
    /* For multicore we cannot write to HW buffer
     * as the Core is not yet reserved.
     */
    poc_base = dec_cont->poc;
  } else {
    struct DWLLinearMem *cabac_mem = dec_cont->asic_buff->cabac_init;
    poc_base = (i32 *) ((u8 *) cabac_mem->virtual_address +
                        ASIC_CABAC_INIT_BUFFER_SIZE);
  }

  if(!dec_cont->asic_buff->enable_dmv_and_poc) {
    SetDecRegister(dec_cont->h264_regs, HWIF_PICORD_COUNT_E, 0);
    return;
  }

  SetDecRegister(dec_cont->h264_regs, HWIF_PICORD_COUNT_E, 1);

  if (p_slice_header->field_pic_flag) {
    curr_poc = poc->pic_order_cnt[p_slice_header->bottom_field_flag ? 1 : 0];
  } else {
    curr_poc = MIN(poc->pic_order_cnt[0], poc->pic_order_cnt[1]);
  }

  for (i = 0; i < 32; i++) {
    *poc_base++ = p_dpb->buffer[i / 2].pic_order_cnt[i & 0x1];
  }

  if (p_slice_header[0].field_pic_flag || !p_sps->mb_adaptive_frame_field_flag) {
    *poc_base++ = curr_poc;
    *poc_base = 0;
  } else {
    *poc_base++ = poc->pic_order_cnt[0];
    *poc_base++ = poc->pic_order_cnt[1];
  }
}

void h264PrepareScaleList(decContainer_t *dec_cont) {
  const picParamSet_t *p_pps = dec_cont->storage.active_pps;
  const u8 (*scaling_list)[64];
  const u32 core_id = dec_cont->b_mc ? dec_cont->core_id : 0;
  u32 i, j, tmp;
  u32 *p;

  struct DWLLinearMem *cabac_mem = dec_cont->asic_buff->cabac_init + core_id;

  if (!p_pps->scaling_matrix_present_flag)
    return;

  p = (u32 *) ((u8 *) cabac_mem->virtual_address +
               ASIC_CABAC_INIT_BUFFER_SIZE +
               ASIC_POC_BUFFER_SIZE);

  scaling_list = p_pps->scaling_list;
  for (i = 0; i < 6; i++) {
    for (j = 0; j < 4; j++) {
      tmp = (scaling_list[i][4 * j + 0] << 24) |
            (scaling_list[i][4 * j + 1] << 16) |
            (scaling_list[i][4 * j + 2] << 8) |
            (scaling_list[i][4 * j + 3]);
      *p++ = tmp;
    }
  }
  for (i = 6; i < 8; i++) {
    for (j = 0; j < 16; j++) {
      tmp = (scaling_list[i][4 * j + 0] << 24) |
            (scaling_list[i][4 * j + 1] << 16) |
            (scaling_list[i][4 * j + 2] << 8) |
            (scaling_list[i][4 * j + 3]);
      *p++ = tmp;
    }
  }
}

void H264UpdateAfterHwRdy(decContainer_t *dec_cont, u32 *h264_regs) {
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;

  /* any B stuff detected by HW? (note reg name changed from
   * b_detected to pic_inf)
   */
  if(GetDecRegister(h264_regs, HWIF_DEC_PIC_INF)) {
    if((dec_cont->h264_profile_support != H264_BASELINE_PROFILE) &&
        (p_asic_buff->enable_dmv_and_poc == 0)) {
      DEBUG_PRINT(("HW Detected B slice in baseline profile stream!!!\n"));
      p_asic_buff->enable_dmv_and_poc = 1;
    }

    SetDecRegister(h264_regs, HWIF_DEC_PIC_INF, 0);
  }

  /*  TODO(atna): Do we support RefBuff in multicore? */
}

void H264ErrorRecover(decContainer_t *dec_cont)
{
  storage_t *storage = &dec_cont->storage;
  DecAsicBuffers_t *p_asic_buff = dec_cont->asic_buff;
  dpbStorage_t *dpb = dec_cont->storage.dpb;
  dpbPicture_t *buffer = dpb->buffer;
  u32 i;

  if (!dec_cont->rlc_mode) {
    u32 k = dpb->dpb_size + 1;
    i32 idx = 0x7FFFFFFF;

    /* find the buffer index of current image */
    while((k--) > 0) {
      if(dpb->buffer[k].data == storage->curr_image->data)
        break;
    }

    /* 1. replace the bus address of vicious reference frame with the address
       of other good frame, use the nearest pic in dispaly order(POC).
       2. set the numErrMbs of current frame if the vicious reference frame is
       in the refreence piciture list.
       which method is chose depends on macro DISCARD_ERROR_PICTURE defined or not.*/

    for (i = 0; i < dpb->dpb_size; i++) {
#ifdef DISCARD_ERROR_PICTURE
      if (buffer[i].num_err_mbs && IS_REFERENCE_F(buffer[i]))
        break;
#else
      if (buffer[i].num_err_mbs && IS_REFERENCE_F(buffer[i])) {
        i32 pic_order_cnt = GET_POC(dpb->buffer[i]);
        i32 tmp = idx = 0x7FFFFFFF;
        u32 j;
        for (j = 0; j <= dpb->dpb_size; j++) {
          if (j == i || (buffer[j].num_err_mbs > 0 ||
              buffer[j].num_err_mbs == -1) ||
             (j == k && !dpb->buffer[j].is_field_pic) ||
             (j == k && (dpb->buffer[j].is_field_pic &&
              !IS_REFERENCE_F(buffer[j]))))
            continue;
          else if (ABS(pic_order_cnt - GET_POC(dpb->buffer[j])) < tmp) {
            if (j != k) {
              tmp = ABS(pic_order_cnt - GET_POC(dpb->buffer[j]));
              idx = j;
            } else if (j == k && tmp == 0x7FFFFFFF)
              idx = j;
          }
        }

        if (idx == 0x7FFFFFFF)
          break;
        else
          p_asic_buff->ref_pic_list[i] = p_asic_buff->ref_pic_list[idx];
      } else
        idx = i;
#endif
    }

    if (i != dpb->dpb_size &&
        !IS_I_SLICE(storage->slice_header->slice_type)) {
      storage->num_concealed_mbs = storage->pic_size_in_mbs;
      u32 j = dpb->num_out;
      u32 tmp = dpb->out_index_r;
      while((j--) > 0) {
        if (tmp == dpb->dpb_size + 1)
          tmp = 0;

        if(dpb->out_buf[tmp].data == storage->curr_image->data) {
          dpb->out_buf[tmp].num_err_mbs = storage->pic_size_in_mbs;
          break;
        }
        tmp++;
      }
    } else {
      if (idx == 0x7FFFFFFF) idx = 0;
      for (i = dpb->dpb_size; i < 16; i++)
        p_asic_buff->ref_pic_list[i] = p_asic_buff->ref_pic_list[idx];
    }
  }
}
