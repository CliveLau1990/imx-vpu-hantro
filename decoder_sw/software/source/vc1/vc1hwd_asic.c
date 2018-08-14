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

#include "vc1hwd_asic.h"
#include "vc1hwd_regdrv.h"
#include "vc1hwd_util.h"
#include "vc1hwd_headers.h"
#include "vc1hwd_decoder.h"
#include "refbuffer.h"

/*------------------------------------------------------------------------------
    External compiler flags
------------------------------------------------------------------------------*/

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

static void SetIntensityCompensationParameters(decContainer_t *dec_cont);

static void SetReferenceBaseAddress(decContainer_t *dec_cont);

static void SetPp(decContainer_t *dec_cont);

void Vc1DecPpInit( vc1DecPp_t *p_dec_pp, struct BufferQueue *bq );
void Vc1DecPpSetFieldOutput( vc1DecPp_t *p_dec_pp, u32 field_output );
void Vc1DecPpBuffer( vc1DecPp_t *p_dec_pp, u32 dec_out, u32 b_frame );
void Vc1DecPpSetPpOutp( vc1DecPp_t *p_dec_pp, DecPpInterface *pc );
void Vc1DecPpSetPpProc( vc1DecPp_t *p_dec_pp, DecPpInterface *pc );

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

    Function name: VC1RunAsic

        Functional description:

        Inputs:
            dec_cont        Pointer to decoder container
            p_strm           Pointer to stream data structure
            strm_bus_addr     Stream bus address for hw

        Outputs:
            None

        Returns:
            asic_status      Hardware status

------------------------------------------------------------------------------*/
u32 VC1RunAsic(decContainer_t *dec_cont, strmData_t *p_strm, addr_t strm_bus_addr) {

  u32 i;
  addr_t tmp;
  i32 ret;
  i32 itmp;
  u32 asic_status = 0;
  u32 refbu_flags = 0;

  ASSERT(dec_cont);
  if (!dec_cont->asic_running) {
    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_INTERLACE_E,
                   (dec_cont->storage.pic_layer.fcm != PROGRESSIVE) ? 1 : 0);
    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_FIELDMODE_E,
                   (dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) ? 1 : 0);
    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_B_E,
                   dec_cont->storage.pic_layer.pic_type&2 ? 1 : 0);

    if (dec_cont->storage.pic_layer.pic_type == PTYPE_B) {
      SetDecRegister(dec_cont->vc1_regs, HWIF_FWD_INTERLACE_E,
                     (dec_cont->storage.p_pic_buf[dec_cont->storage.work1].fcm ==
                      PROGRESSIVE) ? 0 : 1);
    } else if (dec_cont->storage.pic_layer.pic_type == PTYPE_P) {
      SetDecRegister(dec_cont->vc1_regs, HWIF_FWD_INTERLACE_E,
                     (dec_cont->storage.p_pic_buf[(i32)dec_cont->storage.work0].fcm ==
                      PROGRESSIVE) ? 0 : 1);
    }

    switch(dec_cont->storage.pic_layer.pic_type) {
    case PTYPE_I:
    case PTYPE_BI:
      tmp = 0;
      break;
    case PTYPE_P:
    case PTYPE_B:
      tmp = 1;
      break;
    default:
      tmp = 1;
    }
    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_INTER_E, tmp);

    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_TOPFIELD_E,
                   dec_cont->storage.pic_layer.is_ff ==
                   dec_cont->storage.pic_layer.tff);
    SetDecRegister(dec_cont->vc1_regs, HWIF_TOPFIELDFIRST_E,
                   dec_cont->storage.pic_layer.tff);

    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_MB_WIDTH,
                   dec_cont->storage.pic_width_in_mbs);
    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_MB_HEIGHT_P,
                   dec_cont->storage.pic_height_in_mbs);
    SetDecRegister(dec_cont->vc1_regs, HWIF_VC1_HEIGHT_EXT,
                   dec_cont->storage.pic_height_in_mbs >> 8);

    /* set offset only for advanced profile */
    if (dec_cont->storage.profile == VC1_ADVANCED) {
      SetDecRegister(dec_cont->vc1_regs, HWIF_MB_WIDTH_OFF,
                     (dec_cont->storage.cur_coded_width & 0xF));
      SetDecRegister(dec_cont->vc1_regs, HWIF_MB_HEIGHT_OFF,
                     (dec_cont->storage.cur_coded_height & 0xF));
    } else {
      SetDecRegister(dec_cont->vc1_regs, HWIF_MB_WIDTH_OFF, 0);
      SetDecRegister(dec_cont->vc1_regs, HWIF_MB_HEIGHT_OFF, 0);
    }

    if ( (dec_cont->storage.pic_layer.pic_type == PTYPE_P) &&
         (dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) &&
         (dec_cont->storage.pic_layer.num_ref == 0) ) {
      /* Current field and reference field shall have same TFF value.
       * If reference frame is coded as a progressive or an interlaced
       * frame then the TFF value of the reference frame shall be
       * assumed to be the same as the TFF value of the current frame
       * [10.3.1] */
      if (dec_cont->storage.pic_layer.ref_field == 0) {
        tmp = dec_cont->storage.pic_layer.top_field ^ 1;
      } else { /* (ref_field == 1) */
        tmp = dec_cont->storage.pic_layer.top_field;
      }
      SetDecRegister(dec_cont->vc1_regs, HWIF_REF_TOPFIELD_E, tmp);
    } else {
      SetDecRegister(dec_cont->vc1_regs, HWIF_REF_TOPFIELD_E, 0 );
    }

    SetDecRegister(dec_cont->vc1_regs, HWIF_FILTERING_DIS,
                   dec_cont->storage.loop_filter ? 0 : 1);
    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_FIXED_QUANT,
                   (dec_cont->storage.dquant == 0) ? 1 : 0);

    if (dec_cont->storage.pic_layer.pic_type == PTYPE_P) {
      if(dec_cont->storage.max_bframes) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_WRITE_MVS_E, 1);
        ASSERT((dec_cont->direct_mvs.bus_address != 0));
      }

    } else if (dec_cont->storage.pic_layer.pic_type == PTYPE_B) {
      SetDecRegister(dec_cont->vc1_regs, HWIF_WRITE_MVS_E, 0);
    } else {
      SetDecRegister(dec_cont->vc1_regs, HWIF_WRITE_MVS_E, 0);
    }
    SetDecRegister(dec_cont->vc1_regs, HWIF_SYNC_MARKER_E,
                   dec_cont->storage.sync_marker);
    SetDecRegister(dec_cont->vc1_regs,
                   HWIF_DQ_PROFILE,
                   dec_cont->storage.pic_layer.dq_profile ==
                   DQPROFILE_ALL_MACROBLOCKS ? 1 : 0 );
    SetDecRegister(dec_cont->vc1_regs,
                   HWIF_DQBI_LEVEL,
                   dec_cont->storage.pic_layer.dqbi_level );

    if (dec_cont->storage.pic_layer.pic_type == PTYPE_B) {
      /* force RANGEREDFRM to be same as in the subsequent anchor frame.
       * Reference decoder seems to work this way when current frame and
       * backward reference frame have different (illagal) values
       * [7.1.1.3] */
      SetDecRegister(dec_cont->vc1_regs, HWIF_RANGE_RED_FRM_E,
                     (u32)(dec_cont->storage.
                           p_pic_buf[(i32)dec_cont->storage.work0].range_red_frm));
    } else {
      SetDecRegister(dec_cont->vc1_regs, HWIF_RANGE_RED_FRM_E,
                     (u32)(dec_cont->storage.
                           p_pic_buf[dec_cont->storage.work_out].range_red_frm));
    }
    SetDecRegister(dec_cont->vc1_regs, HWIF_FAST_UVMC_E,
                   dec_cont->storage.fast_uv_mc);
    SetDecRegister(dec_cont->vc1_regs, HWIF_TRANSDCTAB,
                   dec_cont->storage.pic_layer.intra_transform_dc_index);
    SetDecRegister(dec_cont->vc1_regs, HWIF_TRANSACFRM,
                   dec_cont->storage.pic_layer.ac_coding_set_index_cb_cr);
    SetDecRegister(dec_cont->vc1_regs, HWIF_TRANSACFRM2,
                   dec_cont->storage.pic_layer.ac_coding_set_index_y);
    SetDecRegister(dec_cont->vc1_regs, HWIF_MB_MODE_TAB,
                   dec_cont->storage.pic_layer.mb_mode_tab);
    SetDecRegister(dec_cont->vc1_regs, HWIF_MVTAB,
                   dec_cont->storage.pic_layer.mv_table_index);
    SetDecRegister(dec_cont->vc1_regs, HWIF_CBPTAB,
                   dec_cont->storage.pic_layer.cbp_table_index);
    SetDecRegister(dec_cont->vc1_regs, HWIF_2MV_BLK_PAT_TAB,
                   dec_cont->storage.pic_layer.mvbp_table_index2);
    SetDecRegister(dec_cont->vc1_regs, HWIF_4MV_BLK_PAT_TAB,
                   dec_cont->storage.pic_layer.mvbp_table_index4);


    SetDecRegister(dec_cont->vc1_regs, HWIF_REF_FRAMES,
                   dec_cont->storage.pic_layer.num_ref);

    SetDecRegister(dec_cont->vc1_regs, HWIF_START_CODE_E,
                   (dec_cont->storage.profile == VC1_ADVANCED) ? 1 : 0);
    SetDecRegister(dec_cont->vc1_regs, HWIF_INIT_QP,
                   dec_cont->storage.pic_layer.pquant);

    if( dec_cont->storage.pic_layer.pic_type == PTYPE_P) {
      if( dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE0_E, 0 );
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE1_E, 0 );
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE2_E, 0 );
      } else {
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE0_E,
                       dec_cont->storage.pic_layer.raw_mask & MB_SKIPPED ? 0 : 1);
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE1_E,
                       dec_cont->storage.pic_layer.raw_mask & MB_4MV ? 0 : 1);
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE2_E, 0 );
      }
    } else if( dec_cont->storage.pic_layer.pic_type == PTYPE_B ) {
      if (dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE0_E,
                       dec_cont->storage.pic_layer.raw_mask & MB_FORWARD_MB ? 0 : 1);
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE1_E, 0 );
      } else {
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE0_E,
                       dec_cont->storage.pic_layer.raw_mask & MB_SKIPPED ? 0 : 1);
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE1_E,
                       dec_cont->storage.pic_layer.raw_mask & MB_DIRECT ? 0 : 1);
      }
      SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE2_E, 0 );
    } else {
      /* acpred flag */
      SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE0_E,
                     dec_cont->storage.pic_layer.raw_mask & MB_AC_PRED ? 0 : 1);
      SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE1_E,
                     dec_cont->storage.pic_layer.raw_mask & MB_OVERLAP_SMOOTH ? 0 : 1);
      if (dec_cont->storage.pic_layer.fcm == FRAME_INTERLACE) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE2_E,
                       dec_cont->storage.pic_layer.raw_mask & MB_FIELD_TX ? 0 : 1);
      } else {
        SetDecRegister(dec_cont->vc1_regs, HWIF_BITPLANE2_E, 0 );
      }
    }
    SetDecRegister(dec_cont->vc1_regs, HWIF_ALT_PQUANT,
                   dec_cont->storage.pic_layer.alt_pquant );
    SetDecRegister(dec_cont->vc1_regs, HWIF_DQ_EDGES,
                   dec_cont->storage.pic_layer.dq_edges );
    SetDecRegister(dec_cont->vc1_regs, HWIF_TTMBF,
                   dec_cont->storage.pic_layer.mb_level_transform_type_flag);
    SetDecRegister(dec_cont->vc1_regs, HWIF_PQINDEX,
                   dec_cont->storage.pic_layer.pq_index);
    SetDecRegister(dec_cont->vc1_regs, HWIF_BILIN_MC_E,
                   dec_cont->storage.pic_layer.pic_type &&
                   (dec_cont->storage.pic_layer.mvmode == MVMODE_1MV_HALFPEL_LINEAR));
    SetDecRegister(dec_cont->vc1_regs, HWIF_UNIQP_E,
                   dec_cont->storage.pic_layer.uniform_quantizer);
    SetDecRegister(dec_cont->vc1_regs, HWIF_HALFQP_E,
                   dec_cont->storage.pic_layer.half_qp);
    SetDecRegister(dec_cont->vc1_regs, HWIF_TTFRM,
                   dec_cont->storage.pic_layer.tt_frm);
    SetDecRegister(dec_cont->vc1_regs, HWIF_DQUANT_E,
                   dec_cont->storage.pic_layer.dquant_in_frame );
    SetDecRegister(dec_cont->vc1_regs, HWIF_VC1_ADV_E,
                   (dec_cont->storage.profile == VC1_ADVANCED) ? 1 : 0);

    if ( (dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) &&
         (dec_cont->storage.pic_layer.pic_type == PTYPE_B)) {
      /* Calculate forward and backward reference distances */
      tmp =
        ( dec_cont->storage.pic_layer.ref_dist *
          dec_cont->storage.pic_layer.scale_factor ) >> 8;
      SetDecRegister(dec_cont->vc1_regs, HWIF_REF_DIST_FWD, tmp);
      itmp = (i32) dec_cont->storage.pic_layer.ref_dist - tmp - 1;
      if (itmp < 0) itmp = 0;
      SetDecRegister(dec_cont->vc1_regs, HWIF_REF_DIST_BWD, itmp );
    } else {
      SetDecRegister(dec_cont->vc1_regs, HWIF_REF_DIST_FWD,
                     dec_cont->storage.pic_layer.ref_dist);
      SetDecRegister(dec_cont->vc1_regs, HWIF_REF_DIST_BWD, 0);
    }

    SetDecRegister(dec_cont->vc1_regs, HWIF_MV_SCALEFACTOR,
                   dec_cont->storage.pic_layer.scale_factor );

    if (dec_cont->storage.pic_layer.pic_type != PTYPE_I)
      SetIntensityCompensationParameters(dec_cont);

    tmp = p_strm->strm_curr_pos - p_strm->p_strm_buff_start;
    tmp = strm_bus_addr + tmp;

    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);

    SetDecRegister(dec_cont->vc1_regs, HWIF_STRM_START_BIT,
                   8*(tmp & 0x7) + p_strm->bit_pos_in_word);
    tmp = p_strm->strm_buff_size - ((tmp & ~0x7) - strm_bus_addr);
    SetDecRegister(dec_cont->vc1_regs, HWIF_STREAM_LEN, tmp );

    if( dec_cont->storage.profile == VC1_ADVANCED &&
        p_strm->strm_curr_pos > p_strm->p_strm_buff_start &&
        p_strm->p_strm_buff_start + p_strm->strm_buff_size
        - p_strm->strm_curr_pos >= 3 &&
        p_strm->strm_curr_pos[-1] == 0x00 &&
        p_strm->strm_curr_pos[ 0] == 0x00 &&
        p_strm->strm_curr_pos[ 1] == 0x03 ) {
      SetDecRegister(dec_cont->vc1_regs,
                     HWIF_2ND_BYTE_EMUL_E, HANTRO_TRUE );
    } else {
      SetDecRegister(dec_cont->vc1_regs,
                     HWIF_2ND_BYTE_EMUL_E, HANTRO_FALSE );
    }

    if (dec_cont->storage.pic_layer.top_field) {
      /* start of top field line or frame */
      SET_ADDR_REG(dec_cont->vc1_regs, HWIF_DEC_OUT_BASE,
                   (dec_cont->storage.p_pic_buf[dec_cont->storage.work_out].
                    data.bus_address));
      SetDecRegister( dec_cont->vc1_regs, HWIF_DPB_ILACE_MODE,
                      dec_cont->dpb_mode );
    } else {
      /* start of bottom field line */
      if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
        SET_ADDR_REG(dec_cont->vc1_regs, HWIF_DEC_OUT_BASE,
                     (dec_cont->storage.p_pic_buf[dec_cont->storage.work_out].
                      data.bus_address + (dec_cont->storage.pic_width_in_mbs<<4)));
      } else if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
        SET_ADDR_REG(dec_cont->vc1_regs, HWIF_DEC_OUT_BASE,
                     (dec_cont->storage.p_pic_buf[dec_cont->storage.work_out].
                      data.bus_address));
      } else {
        ASSERT(0);
      }
      SetDecRegister( dec_cont->vc1_regs, HWIF_DPB_ILACE_MODE,
                      dec_cont->dpb_mode );
    }

    SetDecRegister( dec_cont->vc1_regs, HWIF_PP_PIPELINE_E_U, dec_cont->pp_enabled );
    if (dec_cont->pp_enabled) {
      u32 dsw, dsh;
#define NEXT_MULTIPLE(value, n) (((value) + (n) - 1) & ~((n) - 1))

      dsw = NEXT_MULTIPLE((dec_cont->storage.pic_width_in_mbs * 16 >> dec_cont->dscale_shift_x) * 8, 16 * 8) / 8;
      dsh = (dec_cont->storage.pic_height_in_mbs * 16 >> dec_cont->dscale_shift_y);
      if (dec_cont->dscale_shift_x == 0) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_HOR_SCALE_MODE_U, 0);
        SetDecRegister(dec_cont->vc1_regs, HWIF_WSCALE_INVRA_U, 0);
      } else {
        /* down scale */
        SetDecRegister(dec_cont->vc1_regs, HWIF_HOR_SCALE_MODE_U, 2);
        SetDecRegister(dec_cont->vc1_regs, HWIF_WSCALE_INVRA_U,
                       1<<(16-dec_cont->dscale_shift_x));
      }

      if (dec_cont->dscale_shift_y == 0) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_VER_SCALE_MODE_U, 0);
        SetDecRegister(dec_cont->vc1_regs, HWIF_HSCALE_INVRA_U, 0);
      } else {
        /* down scale */
        SetDecRegister(dec_cont->vc1_regs, HWIF_VER_SCALE_MODE_U, 2);
        SetDecRegister(dec_cont->vc1_regs, HWIF_HSCALE_INVRA_U,
                       1<<(16-dec_cont->dscale_shift_y));
      }
      SET_ADDR64_REG(dec_cont->vc1_regs, HWIF_PP_OUT_LU_BASE_U,
                     dec_cont->storage.p_pic_buf[dec_cont->
                         storage.work_out].
                     pp_data->bus_address);
      SET_ADDR64_REG(dec_cont->vc1_regs, HWIF_PP_OUT_CH_BASE_U,
                     dec_cont->storage.p_pic_buf[dec_cont->
                         storage.work_out].
                     pp_data->bus_address + dsw * dsh);

      SetPpRegister(dec_cont->vc1_regs, HWIF_PP_IN_FORMAT_U, 1);
    }

    if (dec_cont->storage.pic_layer.pic_type != PTYPE_I)
      SetReferenceBaseAddress(dec_cont);

    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_HEADER_LEN,
                   dec_cont->storage.pic_layer.pic_header_bits);
    SetDecRegister(dec_cont->vc1_regs, HWIF_PIC_4MV_E,
                   dec_cont->storage.pic_layer.pic_type &&
                   (dec_cont->storage.pic_layer.mvmode == MVMODE_MIXEDMV));

    /* is inter */
    if( dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE ) {
      tmp = (u32)( dec_cont->storage.pic_layer.is_ff ==
                   dec_cont->storage.pic_layer.tff ) ^ 1;
    } else {
      tmp = 0;
    }

    SetDecRegister(dec_cont->vc1_regs, HWIF_PREV_ANC_TYPE,
                   dec_cont->storage.anchor_inter[ tmp ] );

    if (dec_cont->storage.profile != VC1_ADVANCED) {
      if (dec_cont->storage.pic_layer.pic_type == PTYPE_P) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_RANGE_RED_REF_E,
                       (dec_cont->storage.p_pic_buf[(i32)dec_cont->storage.work0].range_red_frm));
      } else if (dec_cont->storage.pic_layer.pic_type == PTYPE_B) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_RANGE_RED_REF_E,
                       (i32)(dec_cont->storage.
                             p_pic_buf[dec_cont->storage.work1].range_red_frm));
      }
    } else {
      SetDecRegister(dec_cont->vc1_regs, HWIF_RANGE_RED_REF_E, 0 );
    }
    if (dec_cont->storage.profile == VC1_ADVANCED) {
      SetDecRegister(dec_cont->vc1_regs, HWIF_VC1_DIFMV_RANGE,
                     dec_cont->storage.pic_layer.dmv_range);
    }
    SetDecRegister(dec_cont->vc1_regs, HWIF_MV_RANGE,
                   dec_cont->storage.pic_layer.mv_range);

    if ( dec_cont->storage.overlap &&
         ((dec_cont->storage.pic_layer.pic_type ==  PTYPE_I) ||
          (dec_cont->storage.pic_layer.pic_type ==  PTYPE_P) ||
          (dec_cont->storage.pic_layer.pic_type ==  PTYPE_BI)) ) {
      /* reset this */
      SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_E, 0);

      if (dec_cont->storage.pic_layer.pquant >= 9) {
        SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_E, 1);
        SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_METHOD, 1);
      } else {
        if(dec_cont->storage.pic_layer.pic_type ==  PTYPE_P ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_E, 0);
          SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_METHOD, 0);
        } else { /* I and BI */
          switch (dec_cont->storage.pic_layer.cond_over) {
          case 0:
            tmp = 0;
            break;
          case 2:
            tmp = 1;
            break;
          case 3:
            tmp = 2;
            break;
          default:
            tmp = 0;
            break;
          }
          SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_METHOD, tmp);
          if (tmp != 0)
            SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_E, 1);
        }
      }
    } else {
      SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_E, 0);
      SetDecRegister(dec_cont->vc1_regs, HWIF_OVERLAP_METHOD, 0);
    }

    SetDecRegister(dec_cont->vc1_regs, HWIF_MV_ACCURACY_FWD,
                   dec_cont->storage.pic_layer.pic_type &&
                   (dec_cont->storage.pic_layer.mvmode == MVMODE_1MV_HALFPEL ||
                    dec_cont->storage.pic_layer.mvmode ==
                    MVMODE_1MV_HALFPEL_LINEAR) ? 0 : 1);
    SetDecRegister(dec_cont->vc1_regs, HWIF_MPEG4_VC1_RC,
                   dec_cont->storage.rnd ? 0 : 1);

    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_BITPL_CTRL_BASE,
                 dec_cont->bit_plane_ctrl.bus_address);

    /* For field pictures, select offset to correct field */
    tmp = dec_cont->direct_mvs.bus_address;

    if( dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE &&
        dec_cont->storage.pic_layer.is_ff != dec_cont->storage.pic_layer.tff ) {
      tmp += 4 * 2 * ((dec_cont->storage.pic_width_in_mbs *
                       ((dec_cont->storage.pic_height_in_mbs+1)/2)+3)&~0x3);
    }

    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_DIR_MV_BASE,
                 tmp);

    /* Setup reference picture buffer */
    if( dec_cont->ref_buf_support ) {
      RefbuSetup( &dec_cont->ref_buffer_ctrl, dec_cont->vc1_regs,
                  (dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) ?
                  REFBU_FIELD : REFBU_FRAME,
                  (dec_cont->storage.pic_layer.pic_type == PTYPE_I ||
                   dec_cont->storage.pic_layer.pic_type == PTYPE_BI),
                  dec_cont->storage.pic_layer.pic_type == PTYPE_B,
                  0, 2, refbu_flags);
    }

    if (!dec_cont->storage.keep_hw_reserved) {
      ret = DWLReserveHw(dec_cont->dwl, &dec_cont->core_id);
      if (ret != DWL_OK) {
        return X170_DEC_HW_RESERVED;
      }
    }

    /* Reserve HW, enable, release HW */
    dec_cont->asic_running = 1;

    /* PP setup and start */
    if (dec_cont->pp_instance != NULL)
      SetPp(dec_cont);

    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 0x4, 0);

    for (i = 2; i < DEC_X170_REGISTERS; i++) {
      DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4*i,
                  dec_cont->vc1_regs[i]);
      dec_cont->vc1_regs[i] = 0;
    }
    u32 offset;
#if 0
    offset = TOTAL_X170_ORIGIN_REGS * 0x04;
    u32 *dec_regs = dec_cont->vc1_regs + TOTAL_X170_ORIGIN_REGS;
    for (i = DEC_X170_EXPAND_REGS; i > 0; --i) {
      DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *dec_regs);
      dec_regs++;
      offset += 4;
    }
#endif
    offset = PP_START_UNIFIED_REGS * 0x04;
    u32 *pp_regs =  dec_cont->vc1_regs + PP_START_UNIFIED_REGS;
    for(i = PP_UNIFIED_REGS; i > 0; --i) {
      DWLWriteReg(dec_cont->dwl, dec_cont->core_id, offset, *pp_regs);
      pp_regs++;
      offset += 4;
    }
    SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_E, 1);
    DWLEnableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                dec_cont->vc1_regs[1]);
  } else { /* buffer was empty and now we restart with new stream values */
    tmp = p_strm->strm_curr_pos - p_strm->p_strm_buff_start;
    tmp = strm_bus_addr + tmp;

    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_RLC_VLC_BASE, tmp & ~0x7);

    SetDecRegister(dec_cont->vc1_regs, HWIF_STRM_START_BIT,
                   8*(tmp & 0x7) + p_strm->bit_pos_in_word);
    SetDecRegister(dec_cont->vc1_regs, HWIF_STREAM_LEN,
                   p_strm->strm_buff_size - ((tmp & ~0x7) - strm_bus_addr));

    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 5,
                dec_cont->vc1_regs[5]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 6,
                dec_cont->vc1_regs[6]);
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 12,
                dec_cont->vc1_regs[12]);
#ifdef USE_64BIT_ENV
    DWLWriteReg(dec_cont->dwl, dec_cont->core_id, 4 * 122,
                dec_cont->vc1_regs[122]);
#endif

    /* start HW by clearing IRQ_BUFFER_EMPTY status bit */
    DWLEnableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                dec_cont->vc1_regs[1]);
  }


  ret = DWLWaitHwReady(dec_cont->dwl, dec_cont->core_id,
                       (u32)DEC_X170_TIMEOUT_LENGTH);

  for (i = 0; i < DEC_X170_REGISTERS; i++) {
    dec_cont->vc1_regs[i] = DWLReadReg(dec_cont->dwl, dec_cont->core_id, 4*i);
  }
  u32 offset;
#if 0
  offset = TOTAL_X170_ORIGIN_REGS * 0x04;
  u32 *dec_regs = dec_cont->vc1_regs + TOTAL_X170_ORIGIN_REGS;
  for (i = DEC_X170_EXPAND_REGS; i > 0; --i) {
    *dec_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
#endif
  offset = PP_START_UNIFIED_REGS * 0x04;
  u32 *pp_regs =  dec_cont->vc1_regs + PP_START_UNIFIED_REGS;
  for(i = PP_UNIFIED_REGS; i > 0; --i) {
    *pp_regs++ = DWLReadReg(dec_cont->dwl, dec_cont->core_id, offset);
    offset += 4;
  }
  /* Get current stream position from HW */
  tmp = GET_ADDR_REG(dec_cont->vc1_regs, HWIF_RLC_VLC_BASE);


  if (ret == DWL_HW_WAIT_OK) {
    asic_status = GetDecRegister(dec_cont->vc1_regs, HWIF_DEC_IRQ_STAT);
  } else {
    /* reset HW */
    SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_IRQ_STAT, 0);
    SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_IRQ, 0); /* just in case */
    SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_E, 0);

    DWLDisableHw(dec_cont->dwl, dec_cont->core_id, 4 * 1,
                 dec_cont->vc1_regs[1]);

    /* End PP co-operation */
    if(dec_cont->pp_control.pp_status == DECPP_RUNNING ||
        dec_cont->pp_control.pp_status == DECPP_PIC_NOT_FINISHED) {
      if(dec_cont->pp_instance != NULL)
        dec_cont->PPEndCallback(dec_cont->pp_instance);

      dec_cont->pp_control.pp_status = DECPP_PIC_READY;
    }

    asic_status = (ret == DWL_HW_WAIT_TIMEOUT) ?
                  X170_DEC_TIMEOUT : X170_DEC_SYSTEM_ERROR;

    return (asic_status);
  }

  if(asic_status & (DEC_X170_IRQ_BUFFER_EMPTY|DEC_X170_IRQ_DEC_RDY)) {
    if ( (tmp - strm_bus_addr) <= (strm_bus_addr + p_strm->strm_buff_size)) {
      /* current stream position is in the valid range */
      p_strm->strm_curr_pos =
        p_strm->p_strm_buff_start +
        (tmp - strm_bus_addr);
    } else {
      /* error situation, consider that whole stream buffer was consumed */
      p_strm->strm_curr_pos = p_strm->p_strm_buff_start + p_strm->strm_buff_size;
    }

    p_strm->strm_buff_read_bits =
      8 * (p_strm->strm_curr_pos - p_strm->p_strm_buff_start);
    p_strm->bit_pos_in_word = 0;
  }

  SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_IRQ_STAT, 0);
  SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_IRQ, 0);    /* just in case */

  if (!(asic_status & DEC_X170_IRQ_BUFFER_EMPTY)) {
    dec_cont->storage.keep_hw_reserved = 0;
    /* End PP co-operation */
    if( (dec_cont->pp_control.pp_status == DECPP_RUNNING ||
         dec_cont->pp_control.pp_status == DECPP_PIC_NOT_FINISHED) &&
        dec_cont->pp_instance != NULL) {
      if(dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE &&
          dec_cont->storage.pic_layer.is_ff &&
          dec_cont->pp_config_query.deinterlace &&
          /* if decoding was not ok -> don't leave PP running */
          (asic_status & DEC_X170_IRQ_DEC_RDY)) {
        dec_cont->pp_control.pp_status = DECPP_PIC_NOT_FINISHED;
        dec_cont->storage.keep_hw_reserved = 1;
      } else {
        TRACE_PP_CTRL("VC1RunAsic: PP Wait for end\n");
        dec_cont->PPEndCallback(dec_cont->pp_instance);
        TRACE_PP_CTRL("VC1RunAsic: PP Finished\n");
        dec_cont->pp_control.pp_status = DECPP_PIC_READY;
      }
    }

    if (!dec_cont->storage.keep_hw_reserved)
      (void)DWLReleaseHw(dec_cont->dwl, dec_cont->core_id);

    /* HW done, release it! */
    dec_cont->asic_running = 0;
  }

  if( dec_cont->storage.pic_layer.pic_type != PTYPE_B &&
      dec_cont->ref_buf_support &&
      (asic_status & DEC_X170_IRQ_DEC_RDY) &&
      dec_cont->asic_running == 0) {
    u8 *p_tmp = (u8*)dec_cont->direct_mvs.virtual_address;
    if( dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE &&
        dec_cont->storage.pic_layer.is_ff != dec_cont->storage.pic_layer.tff ) {
      p_tmp += 4 * 2 * ((dec_cont->storage.pic_width_in_mbs *
                         ((dec_cont->storage.pic_height_in_mbs+1)/2)+3)&~0x3);
    }

    RefbuMvStatistics( &dec_cont->ref_buffer_ctrl,
                       dec_cont->vc1_regs,
                       (u32*)p_tmp,
                       dec_cont->storage.max_bframes, /* B frames <=> mv data */
                       dec_cont->storage.pic_layer.pic_type == PTYPE_I ||
                       dec_cont->storage.pic_layer.pic_type == PTYPE_BI );
  }

  return(asic_status);

}

/*------------------------------------------------------------------------------

    Function name: SetPp

        Functional description:
            Determines how field are run through the post-processor
            PIPELINE / PARALLEL / STAND_ALONE.

        Inputs:
            dec_cont        Decoder container

        Outputs:
            None

        Returns:
            None

------------------------------------------------------------------------------*/
void SetPp(decContainer_t *dec_cont) {
  picture_t *p_pic;
  u32 w0, w_out;
  u32 pp_idx;
  u32 tmp;
  u32 next_buffer_index = 0;
  u32 pipeline_off = 0;

  /* PP not connected or still running (not waited when first field of frame
   * finished */
  if (dec_cont->pp_control.pp_status == DECPP_PIC_NOT_FINISHED) {
    if(dec_cont->storage.parallel_mode2)
      Vc1DecPpBuffer( &dec_cont->storage.dec_pp,
                      dec_cont->storage.work_out,
                      dec_cont->storage.pic_layer.pic_type == PTYPE_B ||
                      dec_cont->storage.pic_layer.pic_type == PTYPE_BI);
    return;
  }

  p_pic = (picture_t*)dec_cont->storage.p_pic_buf;

  w0 = dec_cont->storage.work0;
  w_out = dec_cont->storage.work_out;
  pp_idx = w_out;

  /* reset use pipeline */
  dec_cont->pp_control.use_pipeline = HANTRO_FALSE;

  dec_cont->pp_config_query.tiled_mode =
    dec_cont->tiled_reference_enable;
  dec_cont->PPConfigQuery(dec_cont->pp_instance, &dec_cont->pp_config_query);

  /* Check whether to enable parallel mode 2 */
  if( (!dec_cont->pp_config_query.pipeline_accepted ||
       dec_cont->storage.interlace) &&
      dec_cont->pp_control.multi_buf_stat != MULTIBUFFER_DISABLED &&
      (dec_cont->storage.max_bframes > 0) &&
      !dec_cont->storage.parallel_mode2 &&
      dec_cont->pp_config_query.multi_buffer) {
    dec_cont->storage.parallel_mode2 = 1;
    dec_cont->storage.pm2_all_processed_flag = 0;
    dec_cont->storage.pm2lock_buf    = dec_cont->pp_control.prev_anchor_display_index;
    dec_cont->storage.pm2_start_point =
      dec_cont->pic_number;
    Vc1DecPpInit( &dec_cont->storage.dec_pp, &dec_cont->storage.bq_pp );
  }

  Vc1DecPpSetFieldOutput( &dec_cont->storage.dec_pp,
                          dec_cont->storage.interlace &&
                          !dec_cont->pp_config_query.deinterlace);


  /* If we have once enabled parallel mode 2, keep it on */
  if(dec_cont->storage.parallel_mode2)
    pipeline_off = 1;

  /* Enable decoder output writes */
  SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_OUT_DIS, 0);

  /* Check cases that can be post-processed in pipeline */
  if (dec_cont->pp_config_query.pipeline_accepted &&
      !pipeline_off) {
    if ((dec_cont->storage.max_bframes == 0) ||
        dec_cont->pp_config_query.multi_buffer) {
      /* use pipeline for I and P frames */
      dec_cont->pp_control.use_pipeline = HANTRO_TRUE;
      p_pic[w_out].field[0].dec_pp_stat = PIPELINED;
      p_pic[w_out].field[1].dec_pp_stat = PIPELINED;
    } else {
      /* use pipeline for progressive B frames */
      if (!dec_cont->storage.interlace &&
          ((dec_cont->storage.pic_layer.pic_type == PTYPE_B) ||
           (dec_cont->storage.pic_layer.pic_type == PTYPE_BI))) {
        dec_cont->pp_control.use_pipeline = HANTRO_TRUE;
        p_pic[w_out].field[0].dec_pp_stat = PIPELINED;
        p_pic[w_out].field[1].dec_pp_stat = PIPELINED;

        /* Don't write decoder output for B frames when pipelined */
        SetDecRegister(dec_cont->vc1_regs, HWIF_DEC_OUT_DIS, 1);
      }
    }
    dec_cont->pp_control.pic_struct = DECPP_PIC_FRAME_OR_TOP_FIELD;

    /* Multibuffer fullmode */
    if (dec_cont->pp_config_query.multi_buffer) {
      dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_FULLMODE;
      if( dec_cont->storage.max_bframes > 0 )
        dec_cont->storage.min_count = 1;
      else
        dec_cont->storage.min_count = 0;
      dec_cont->storage.previous_mode_full = 1;
    } else {
      dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_DISABLED;
    }
  }

  /* Select new PP buffer index to use. If multibuffer is disabled, use
   * previous buffer, otherwise select new buffer from queue. */
  if(dec_cont->pp_config_query.multi_buffer) {
    u32 buf0 = BQUEUE_UNUSED;
    /* In parallel mode 2, we must refrain from reusing future
     * anchor frame output buffer until it has been put out. */
    if(dec_cont->storage.parallel_mode2) {
      buf0 = dec_cont->storage.pm2lock_buf;
      Vc1DecPpBuffer( &dec_cont->storage.dec_pp,
                      dec_cont->storage.work_out,
                      dec_cont->storage.pic_layer.pic_type == PTYPE_B ||
                      dec_cont->storage.pic_layer.pic_type == PTYPE_BI);
      Vc1DecPpSetPpProc( &dec_cont->storage.dec_pp, &dec_cont->pp_control );
      /*Vc1DecPpSetPpOutp( &dec_cont->pp_control );*/
    } else {
      next_buffer_index = BqueueNext( &dec_cont->storage.bq_pp,
                                      buf0,
                                      BQUEUE_UNUSED,
                                      BQUEUE_UNUSED,
                                      dec_cont->storage.pic_layer.pic_type == PTYPE_B ||
                                      dec_cont->storage.pic_layer.pic_type == PTYPE_BI);
      dec_cont->pp_control.buffer_index = next_buffer_index;
    }
  } else {
    next_buffer_index = dec_cont->pp_control.buffer_index = 0;
  }

  if(dec_cont->storage.parallel_mode2) {
    /* Note: functionality moved to Vc1DecPpXXX() functions */
  } else if(dec_cont->storage.max_bframes == 0 ||
            (dec_cont->storage.pic_layer.pic_type == PTYPE_B ||
             dec_cont->storage.pic_layer.pic_type == PTYPE_BI)) {
    dec_cont->pp_control.display_index = dec_cont->pp_control.buffer_index;
  } else {
    dec_cont->pp_control.display_index = dec_cont->pp_control.prev_anchor_display_index;
  }

  /* Connect PP output buffer to decoder output buffer */
  {
    addr_t luma = 0;
    addr_t chroma = 0;
    addr_t bot_luma = 0, bot_chroma = 0;
    u32 work_buffer;

    if(dec_cont->storage.parallel_mode2)
      work_buffer = dec_cont->storage.work_out_prev;
    else
      work_buffer = dec_cont->storage.work_out;

    luma = dec_cont->storage.p_pic_buf[work_buffer].
           data.bus_address;
    chroma = luma + ((dec_cont->storage.pic_width_in_mbs * 16) *
                     (dec_cont->storage.pic_height_in_mbs * 16));
    if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
      bot_luma = luma + (dec_cont->storage.pic_width_in_mbs * 16);
      bot_chroma = chroma + (dec_cont->storage.pic_width_in_mbs * 16);
    } else if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
      bot_luma = luma + ((dec_cont->storage.pic_width_in_mbs * 16) *
                         (dec_cont->storage.pic_height_in_mbs * 8));
      bot_chroma = chroma + ((dec_cont->storage.pic_width_in_mbs * 16) *
                             (dec_cont->storage.pic_height_in_mbs * 4));
    }

    dec_cont->PPBufferData(dec_cont->pp_instance,
                           dec_cont->pp_control.buffer_index, luma, chroma, bot_luma, bot_chroma);
  }

  /* Check cases that will be processed in parallel or stand-alone */
  if (!dec_cont->pp_control.use_pipeline) {
    /* Set minimum count for picture buffer handling.
     * Behaviour is same as with max_bframes > 0. */
    dec_cont->storage.min_count = 1;

    if(!dec_cont->storage.previous_mode_full &&
        !dec_cont->storage.parallel_mode2) {
      dec_cont->pp_control.buffer_index = dec_cont->pp_control.display_index;
    }

    /* return if first frame of the sequence */
    if (dec_cont->storage.first_frame)
      return;

    /* if parallel mode2 used, always process previous output picture */
    if(dec_cont->storage.parallel_mode2) {
      pp_idx = dec_cont->storage.work_out_prev;

      /* If we got an anchor frame, lock the PP output buffer */
      if(!dec_cont->storage.previous_b) {
        dec_cont->storage.pm2lock_buf =
          dec_cont->pp_control.buffer_index;
      }

      if (dec_cont->storage.interlace) {
        /* deinterlace is ON -> both fields are processed
         * at the same time */
        if (dec_cont->pp_config_query.deinterlace) {
          /* wait until both fields are decoded */
          if (!dec_cont->storage.pic_layer.is_ff &&
              dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE)
            return;

          p_pic[pp_idx].field[0].dec_pp_stat = PARALLEL;
          p_pic[pp_idx].field[1].dec_pp_stat = PARALLEL;
          dec_cont->pp_control.pic_struct =
            DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
        } else { /* field picture */
          p_pic[pp_idx].field[0].dec_pp_stat = PARALLEL;

          if (dec_cont->storage.pic_layer.fcm != FIELD_INTERLACE) {
            p_pic[pp_idx].field[1].dec_pp_stat = STAND_ALONE;
          } else {
            /* Note: here could be optimized */
            p_pic[pp_idx].field[1].dec_pp_stat = STAND_ALONE;
          }

          p_pic[pp_idx].field[0].pp_buffer_index =
            dec_cont->pp_control.buffer_index;
          p_pic[pp_idx].field[1].pp_buffer_index =
            dec_cont->pp_control.buffer_index;

          if ( dec_cont->storage.pic_layer.is_ff ==
               p_pic[pp_idx].is_top_field_first ) {
            dec_cont->pp_control.pic_struct =
              DECPP_PIC_TOP_FIELD_FRAME;
          } else {
            dec_cont->pp_control.pic_struct =
              DECPP_PIC_BOT_FIELD_FRAME;
          }
        }
      } else { /* progressive */
        p_pic[pp_idx].field[0].dec_pp_stat = PARALLEL;
        p_pic[pp_idx].field[1].dec_pp_stat = PARALLEL;
        dec_cont->pp_control.pic_struct =
          DECPP_PIC_FRAME_OR_TOP_FIELD;
      }
    }
    /* if current picture is anchor frame -> start to
     * post-process previous anchor frame  */
    else if (p_pic[w_out].field[0].type < 2) { /* I or P picture */
      pp_idx = w0;
      if (dec_cont->storage.interlace) {
        /* deinterlace is ON -> both fields are processed
         * at the same time */
        if (dec_cont->pp_config_query.deinterlace) {
          /* wait until both fields are decoded */
          if (!dec_cont->storage.pic_layer.is_ff &&
              dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE)
            return;

          p_pic[w0].field[0].dec_pp_stat = PARALLEL;
          p_pic[w0].field[1].dec_pp_stat = PARALLEL;
          dec_cont->pp_control.pic_struct =
            DECPP_PIC_TOP_AND_BOT_FIELD_FRAME;
        } else { /* field picture */
          p_pic[w0].field[0].dec_pp_stat = PARALLEL;

          if (dec_cont->storage.pic_layer.fcm != FIELD_INTERLACE)
            p_pic[w0].field[1].dec_pp_stat = STAND_ALONE;
          else
            p_pic[w0].field[1].dec_pp_stat = PARALLEL;

          if ( dec_cont->storage.pic_layer.is_ff ==
               p_pic[w0].is_top_field_first ) {
            dec_cont->pp_control.pic_struct =
              DECPP_PIC_TOP_FIELD_FRAME;
          } else {
            dec_cont->pp_control.pic_struct =
              DECPP_PIC_BOT_FIELD_FRAME;
          }
        }
      } else { /* progressive */
        p_pic[w0].field[0].dec_pp_stat = PARALLEL;
        p_pic[w0].field[1].dec_pp_stat = PARALLEL;
        dec_cont->pp_control.pic_struct =
          DECPP_PIC_FRAME_OR_TOP_FIELD;
      }
    }
    /* post-processing will be started as a stand-alone mode
     * from Vc1DecNextPicture. (interlace is ON and B-picture) */
    else {
      ASSERT(dec_cont->storage.pic_layer.pic_type > 1); /* B or BI */

      p_pic[w_out].field[0].dec_pp_stat = STAND_ALONE;
      p_pic[w_out].field[1].dec_pp_stat = STAND_ALONE;
    }

    /* Multibuffer semimode */
    if (dec_cont->pp_config_query.multi_buffer) {
      dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_SEMIMODE;
      dec_cont->storage.previous_mode_full = 0;
    } else {
      dec_cont->pp_control.multi_buf_stat = MULTIBUFFER_DISABLED;
    }
  }

  if((dec_cont->storage.pic_layer.pic_type == PTYPE_B) ||
      (dec_cont->storage.pic_layer.pic_type == PTYPE_BI)) {
    dec_cont->storage.previous_b = 1;
  } else {
    dec_cont->storage.previous_b = 0;
  }

  tmp = (dec_cont->storage.pic_width_in_mbs * 16) *
        (dec_cont->storage.pic_height_in_mbs * 16);

  /* fill rest of pp_control parameters */
  dec_cont->pp_control.input_bus_luma = p_pic[pp_idx].data.bus_address;
  dec_cont->pp_control.input_bus_chroma = p_pic[pp_idx].data.bus_address + tmp;
  if(dec_cont->dpb_mode == DEC_DPB_FRAME ) {
    dec_cont->pp_control.bottom_bus_luma = p_pic[pp_idx].data.bus_address +
                                           (dec_cont->storage.pic_width_in_mbs * 16);
    dec_cont->pp_control.bottom_bus_chroma =
      dec_cont->pp_control.bottom_bus_luma + tmp;
  } else if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
    dec_cont->pp_control.bottom_bus_luma = p_pic[pp_idx].data.bus_address +
                                           tmp/2;
    dec_cont->pp_control.bottom_bus_chroma =
      p_pic[pp_idx].data.bus_address + tmp + (tmp/4) ;
  }
  dec_cont->pp_control.little_endian =
    GetDecRegister(dec_cont->vc1_regs, HWIF_DEC_OUT_ENDIAN);
  dec_cont->pp_control.word_swap =
    GetDecRegister(dec_cont->vc1_regs, HWIF_DEC_OUTSWAP32_E);
  dec_cont->pp_control.inwidth = dec_cont->storage.pic_width_in_mbs * 16;
  dec_cont->pp_control.inheight = dec_cont->storage.pic_height_in_mbs * 16;
  dec_cont->pp_control.tiled_input_mode = p_pic[pp_idx].tiled_mode;
  dec_cont->pp_control.progressive_sequence = !dec_cont->storage.interlace;
  dec_cont->pp_control.cropped_w = ((p_pic[pp_idx].coded_width+7) & ~7);
  dec_cont->pp_control.cropped_h = ((p_pic[pp_idx].coded_height+7) & ~7);

  if ( (dec_cont->pp_control.pic_struct == DECPP_PIC_TOP_FIELD_FRAME) ||
       (dec_cont->pp_control.pic_struct == DECPP_PIC_BOT_FIELD_FRAME) ) {
    /* input height is multiple of 16 */
    dec_cont->pp_control.inheight >>= 1;
    dec_cont->pp_control.inheight=((dec_cont->pp_control.inheight+15) & ~15);
    /* cropped height is multiple of 8 */
    dec_cont->pp_control.cropped_h >>= 1;
    dec_cont->pp_control.cropped_h=((dec_cont->pp_control.cropped_h + 7) & ~7);
  }
  dec_cont->pp_control.vc1_adv_enable =
    (dec_cont->storage.profile == VC1_ADVANCED) ? 1 : 0;
  dec_cont->pp_control.range_red = p_pic[pp_idx].range_red_frm;
  dec_cont->pp_control.range_map_yenable = p_pic[pp_idx].range_map_yflag;
  dec_cont->pp_control.range_map_ycoeff = p_pic[pp_idx].range_map_y;
  dec_cont->pp_control.range_map_cenable = p_pic[pp_idx].range_map_uv_flag;
  dec_cont->pp_control.range_map_ccoeff = p_pic[pp_idx].range_map_uv;

  if( dec_cont->dpb_mode == DEC_DPB_INTERLACED_FIELD ) {
    if( dec_cont->pp_config_query.deinterlace ) {
      dec_cont->pp_control.pic_struct = DECPP_PIC_TOP_AND_BOT_FIELD;
    } else {
      dec_cont->pp_control.pic_struct = (
                                          dec_cont->storage.pic_layer.is_ff ==
                                          p_pic[pp_idx].is_top_field_first) ?
                                        DECPP_PIC_FRAME_OR_TOP_FIELD : DECPP_PIC_BOT_FIELD;
    }
  }

  /* run pp if possible */
  tmp = dec_cont->storage.pic_layer.is_ff ^ 1;
  if ( p_pic[pp_idx].field[tmp].dec_pp_stat != STAND_ALONE) {
    ASSERT(dec_cont->pp_control.pp_status != DECPP_RUNNING);

    dec_cont->PPRun( dec_cont->pp_instance, &dec_cont->pp_control );
    dec_cont->pp_control.pp_status = DECPP_RUNNING;

#ifdef _DEC_PP_USAGE
    PrintDecPpUsage(dec_cont,
                    dec_cont->storage.pic_layer.is_ff,
                    pp_idx,
                    HANTRO_FALSE,
                    p_pic[pp_idx].field[tmp].pic_id);
#endif
  }

  if( dec_cont->storage.pic_layer.pic_type != PTYPE_B &&
      dec_cont->storage.pic_layer.pic_type != PTYPE_BI ) {
    dec_cont->pp_control.prev_anchor_display_index = next_buffer_index;
  }

  if( dec_cont->pp_control.input_bus_luma == 0 &&
      dec_cont->storage.parallel_mode2 ) {
    BqueueDiscard( &dec_cont->storage.bq_pp,
                   dec_cont->pp_control.buffer_index );
  }

}


/*------------------------------------------------------------------------------

    Function name: SetReferenceBaseAddress

        Functional description:
            Updates base addresses of reference pictures

        Inputs:
            dec_cont        Decoder container

        Outputs:
            None

        Returns:
            None

------------------------------------------------------------------------------*/
void SetReferenceBaseAddress(decContainer_t *dec_cont) {
  u32 tmp, tmp1, tmp2;
  if (dec_cont->storage.max_bframes) {
    /* forward reference */
    if (dec_cont->storage.pic_layer.pic_type == PTYPE_B) {
      if ( (dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) ) {
        if (dec_cont->storage.pic_layer.is_ff == HANTRO_TRUE) {
          /* Use 1st and 2nd fields of temporally
           * previous anchor frame */
          SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                       (dec_cont->storage.
                        p_pic_buf[dec_cont->storage.work1].
                        data.bus_address));
          SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE,
                       (dec_cont->storage.
                        p_pic_buf[dec_cont->storage.work1].
                        data.bus_address));
        } else {
          /* Use 1st field of current frame and 2nd
           * field of prev frame */
          if( dec_cont->storage.pic_layer.tff ) {
            SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                         (dec_cont->storage.
                          p_pic_buf[dec_cont->storage.work_out].
                          data.bus_address));
            SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE,
                         (dec_cont->storage.
                          p_pic_buf[dec_cont->storage.work1].
                          data.bus_address));
          } else {
            SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                         (dec_cont->storage.
                          p_pic_buf[dec_cont->storage.work1].
                          data.bus_address));
            SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE,
                         (dec_cont->storage.
                          p_pic_buf[dec_cont->storage.work_out].
                          data.bus_address));
          }
        }
      } else {
        SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                     (dec_cont->storage.p_pic_buf[dec_cont->storage.work1].
                      data.bus_address));
        SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE,
                     (dec_cont->storage.p_pic_buf[dec_cont->storage.work1].
                      data.bus_address));

      }
    } else { /* PTYPE_P */
      if ( (dec_cont->storage.pic_layer.fcm == FIELD_INTERLACE) ) {
        if( dec_cont->storage.pic_layer.num_ref == 0 ) {
          if( dec_cont->storage.pic_layer.is_ff == HANTRO_FALSE &&
              dec_cont->storage.pic_layer.ref_field == 0 ) {
            tmp = dec_cont->storage.work_out;
          } else {
            tmp = dec_cont->storage.work0;
          }
          SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                       (dec_cont->storage.p_pic_buf[tmp].data.bus_address));
          SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE,
                       (dec_cont->storage.p_pic_buf[tmp].data.bus_address));
        } else { /* NUMREF == 1 */
          /* check that reference available */
          if( dec_cont->storage.work0 == INVALID_ANCHOR_PICTURE )
            tmp2 = dec_cont->storage.work_out_prev;
          else
            tmp2 = dec_cont->storage.work0;

          if( dec_cont->storage.pic_layer.is_ff == HANTRO_TRUE ) {
            /* Use both fields from previous frame */
            tmp = tmp2;
            tmp1 = tmp2;
          } else {
            /* Use previous field from this frame and second field
             * from previous frame */
            if( dec_cont->storage.pic_layer.tff ) {
              tmp = dec_cont->storage.work_out;
              tmp1 = tmp2;
            } else {
              tmp = tmp2;
              tmp1 = dec_cont->storage.work_out;
            }
          }
          SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                       (dec_cont->storage.p_pic_buf[tmp].data.bus_address));
          SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE,
                       (dec_cont->storage.p_pic_buf[tmp1].data.bus_address));
        }
      } else {
        SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                     (dec_cont->storage.p_pic_buf[(i32)dec_cont->storage.work0].
                      data.bus_address));
        SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE,
                     (dec_cont->storage.p_pic_buf[(i32)dec_cont->storage.work0].
                      data.bus_address));
      }
    }

    /* check that reference available */
    if( dec_cont->storage.work0 == INVALID_ANCHOR_PICTURE )
      tmp2 = dec_cont->storage.work_out_prev;
    else
      tmp2 = dec_cont->storage.work0;

    /* backward reference */
    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER2_BASE,
                 (dec_cont->storage.p_pic_buf[tmp2].
                  data.bus_address));

    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER3_BASE,
                 (dec_cont->storage.p_pic_buf[tmp2].
                  data.bus_address));

  } else {
    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER0_BASE,
                 (dec_cont->storage.p_pic_buf[(i32)dec_cont->storage.work0].
                  data.bus_address));
    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER1_BASE, (addr_t)0);
    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER2_BASE, (addr_t)0);
    SET_ADDR_REG(dec_cont->vc1_regs, HWIF_REFER3_BASE, (addr_t)0);
  }

}

/*------------------------------------------------------------------------------

    Function name: SetIntensityCompensationParameters

        Functional description:
            Updates intensity compensation parameters

        Inputs:
            dec_cont        Decoder container

        Outputs:
            None

        Returns:
            None

------------------------------------------------------------------------------*/
void SetIntensityCompensationParameters(decContainer_t *dec_cont) {
  picture_t *p_pic;
  i32 w0;
  i32 w1;
  u32 first_top;

  p_pic = (picture_t*)dec_cont->storage.p_pic_buf;

  if (dec_cont->storage.pic_layer.pic_type == PTYPE_B) {
    w0   = dec_cont->storage.work0;
    w1   = dec_cont->storage.work1;
  } else {
    w0   = dec_cont->storage.work_out;
    w1   = dec_cont->storage.work0;
  }

  /* first disable all */
  SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP0_E, 0);
  SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP1_E, 0);
  SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP2_E, 0);
  SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP3_E, 0);
  SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP4_E, 0);

  SetDecRegister(dec_cont->vc1_regs, HWIF_REFTOPFIRST_E,
                 p_pic[w1].is_top_field_first);

  /* frame coded */
  if (p_pic[w0].fcm != FIELD_INTERLACE) {
    /* set first */
    if (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) {
      SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP0_E, 1);
      SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE0,
                     (u32)p_pic[w0].field[0].i_scale_a);
      SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT0,
                     (u32)p_pic[w0].field[0].i_shift_a);
    }
    /* field picture */
    if (p_pic[w1].fcm == FIELD_INTERLACE) {
      /* set second */
      if (p_pic[w1].is_top_field_first) {
        if ( (p_pic[w1].field[1].int_comp_f == IC_BOTH_FIELDS) ||
             (p_pic[w1].field[1].int_comp_f == IC_TOP_FIELD) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP1_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE1,
                         (u32)p_pic[w1].field[1].i_scale_a);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT1,
                         (u32)p_pic[w1].field[1].i_shift_a);
        }
      } else { /* bottom field first */
        if ( (p_pic[w1].field[1].int_comp_f == IC_BOTH_FIELDS) ||
             (p_pic[w1].field[1].int_comp_f == IC_BOTTOM_FIELD) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP1_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE1,
                         (u32)p_pic[w1].field[1].i_scale_b);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT1,
                         (u32)p_pic[w1].field[1].i_shift_b);
        }
      }
    }
  } else { /* field */
    if (p_pic[w0].is_first_field) {
      if (p_pic[w0].is_top_field_first) {
        first_top = HANTRO_TRUE;
        if ( (p_pic[w0].field[0].int_comp_f == IC_BOTTOM_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP0_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE0,
                         (u32)p_pic[w0].field[0].i_scale_b);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT0,
                         (u32)p_pic[w0].field[0].i_shift_b);
        }

        if ( (p_pic[w0].field[0].int_comp_f == IC_TOP_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP1_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE1,
                         (u32)p_pic[w0].field[0].i_scale_a);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT1,
                         (u32)p_pic[w0].field[0].i_shift_a);
        }

      } else { /* bottom field first */
        first_top = HANTRO_FALSE;
        if ( (p_pic[w0].field[0].int_comp_f == IC_TOP_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP0_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE0,
                         (u32)p_pic[w0].field[0].i_scale_a);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT0,
                         (u32)p_pic[w0].field[0].i_shift_a);
        }

        if ( (p_pic[w0].field[0].int_comp_f == IC_BOTTOM_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP1_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE1,
                         (u32)p_pic[w0].field[0].i_scale_b);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT1,
                         (u32)p_pic[w0].field[0].i_shift_b);
        }
      }

      if (p_pic[w1].fcm == FIELD_INTERLACE) {
        if (first_top) {
          if ( (p_pic[w1].field[1].int_comp_f == IC_TOP_FIELD) ||
               (p_pic[w1].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
            SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP2_E, 1);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE2,
                           (u32)p_pic[w1].field[1].i_scale_a);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT2,
                           (u32)p_pic[w1].field[1].i_shift_a);
          }
        } else {
          if ( (p_pic[w1].field[1].int_comp_f == IC_BOTTOM_FIELD) ||
               (p_pic[w1].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
            SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP2_E, 1);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE2,
                           (u32)p_pic[w1].field[1].i_scale_b);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT2,
                           (u32)p_pic[w1].field[1].i_shift_b);
          }
        }
      }
    } else { /* second field */
      if (p_pic[w0].is_top_field_first) {
        first_top = HANTRO_TRUE;
        if ( (p_pic[w0].field[1].int_comp_f == IC_TOP_FIELD) ||
             (p_pic[w0].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP0_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE0,
                         (u32)p_pic[w0].field[1].i_scale_a);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT0,
                         (u32)p_pic[w0].field[1].i_shift_a);
        }

        if ( (p_pic[w0].field[1].int_comp_f == IC_BOTTOM_FIELD) ||
             (p_pic[w0].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP1_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE1,
                         (u32)p_pic[w0].field[1].i_scale_b);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT1,
                         (u32)p_pic[w0].field[1].i_shift_b);
        }

        if ( (p_pic[w0].field[0].int_comp_f == IC_BOTTOM_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP2_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE2,
                         (u32)p_pic[w0].field[0].i_scale_b);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT2,
                         (u32)p_pic[w0].field[0].i_shift_b);
        }

        if ( (p_pic[w0].field[0].int_comp_f == IC_TOP_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP3_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE3,
                         (u32)p_pic[w0].field[0].i_scale_a);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT3,
                         (u32)p_pic[w0].field[0].i_shift_a);
        }

        if (p_pic[w1].fcm == FIELD_INTERLACE) {
          if ( (p_pic[w1].field[1].int_comp_f == IC_TOP_FIELD) ||
               (p_pic[w1].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
            SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP4_E, 1);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE4,
                           (u32)p_pic[w1].field[1].i_scale_a);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT4,
                           (u32)p_pic[w1].field[1].i_shift_a);
          }
        }

      } else { /* bottom field first */
        first_top = HANTRO_FALSE;
        if ( (p_pic[w0].field[1].int_comp_f == IC_BOTTOM_FIELD) ||
             (p_pic[w0].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP0_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE0,
                         (u32)p_pic[w0].field[1].i_scale_b);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT0,
                         (u32)p_pic[w0].field[1].i_shift_b);
        }

        if ( (p_pic[w0].field[1].int_comp_f == IC_TOP_FIELD) ||
             (p_pic[w0].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP1_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE1,
                         (u32)p_pic[w0].field[1].i_scale_a);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT1,
                         (u32)p_pic[w0].field[1].i_shift_a);
        }

        if ( (p_pic[w0].field[0].int_comp_f == IC_TOP_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP2_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE2,
                         (u32)p_pic[w0].field[0].i_scale_a);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT2,
                         (u32)p_pic[w0].field[0].i_shift_a);
        }

        if ( (p_pic[w0].field[0].int_comp_f == IC_BOTTOM_FIELD) ||
             (p_pic[w0].field[0].int_comp_f == IC_BOTH_FIELDS) ) {
          SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP3_E, 1);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE3,
                         (u32)p_pic[w0].field[0].i_scale_b);
          SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT3,
                         (u32)p_pic[w0].field[0].i_shift_b);
        }

        if (p_pic[w1].fcm == FIELD_INTERLACE) {
          if ( (p_pic[w1].field[1].int_comp_f == IC_BOTTOM_FIELD) ||
               (p_pic[w1].field[1].int_comp_f == IC_BOTH_FIELDS) ) {
            SetDecRegister(dec_cont->vc1_regs, HWIF_ICOMP4_E, 1);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISCALE4,
                           (u32)p_pic[w1].field[1].i_scale_b);
            SetDecRegister(dec_cont->vc1_regs, HWIF_ISHIFT4,
                           (u32)p_pic[w1].field[1].i_shift_b);
          }
        }

      }
    }
  }
}
#ifdef _DEC_PP_USAGE
/*------------------------------------------------------------------------------

    Function name: PrintDecPpUsage

        Functional description:

        Inputs:
            dec_cont    Pointer to decoder structure
            ff          Is current field first or second field of the frame
            pic_index    Picture buffer index
            dec_status   DEC / PP print
            pic_id       Picture ID of the field/frame

        Outputs:
            None

        Returns:
            None

------------------------------------------------------------------------------*/
void PrintDecPpUsage( decContainer_t *dec_cont,
                      u32 ff,
                      u32 pic_index,
                      u32 dec_status,
                      u32 pic_id) {
  FILE *fp;
  picture_t *p_pic;
  p_pic = (picture_t*)dec_cont->storage.p_pic_buf;

  fp = fopen("dec_pp_usage.txt", "at");
  if (fp == NULL)
    return;

  if (dec_status) {
    if (dec_cont->storage.pic_layer.is_ff) {
      fprintf(fp, "\n======================================================================\n");

      fprintf(fp, "%10s%10s%10s%10s%10s%10s%10s\n",
              "Component", "PicId", "PicType", "Fcm", "Field",
              "PPMode", "BuffIdx");
    }
    /* Component and PicId */
    fprintf(fp, "\n%10.10s%10d", "DEC", pic_id);
    /* Pictype */
    switch (dec_cont->storage.pic_layer.pic_type) {
    case PTYPE_P:
      fprintf(fp, "%10s","P");
      break;
    case PTYPE_I:
      fprintf(fp, "%10s","I");
      break;
    case PTYPE_B:
      fprintf(fp, "%10s","B");
      break;
    case PTYPE_BI:
      fprintf(fp, "%10s","BI");
      break;
    case PTYPE_Skip:
      fprintf(fp, "%10s","Skip");
      break;
    }
    /* Frame coding mode */
    switch (dec_cont->storage.pic_layer.fcm) {
    case PROGRESSIVE:
      fprintf(fp, "%10s","PR");
      break;
    case FRAME_INTERLACE:
      fprintf(fp, "%10s","FR");
      break;
    case FIELD_INTERLACE:
      fprintf(fp, "%10s","FI");
      break;
    }
    /* Field */
    if (dec_cont->storage.pic_layer.top_field)
      fprintf(fp, "%10s","TOP");
    else
      fprintf(fp, "%10s","BOT");

    /* PPMode and BuffIdx */
    /* fprintf(fp, "%10s%10d\n", "---",pic_index);*/
    switch (dec_cont->pp_control.multi_buf_stat) {
    case MULTIBUFFER_FULLMODE:
      fprintf(fp, "%10s%10d\n", "FULL",pic_index);
      break;
    case MULTIBUFFER_SEMIMODE:
      fprintf(fp, "%10s%10d\n", "SEMI",pic_index);
      break;
    case MULTIBUFFER_DISABLED:
      fprintf(fp, "%10s%10d\n", "DISA",pic_index);
      break;
    case MULTIBUFFER_UNINIT:
      fprintf(fp, "%10s%10d\n", "UNIN",pic_index);
      break;
    default:
      break;
    }
  } else { /* pp status */
    /* Component and PicId */
    fprintf(fp, "%10s%10d", "PP", pic_id);
    /* Pictype */
    switch (p_pic[pic_index].field[!ff].type) {
    case PTYPE_P:
      fprintf(fp, "%10s","P");
      break;
    case PTYPE_I:
      fprintf(fp, "%10s","I");
      break;
    case PTYPE_B:
      fprintf(fp, "%10s","B");
      break;
    case PTYPE_BI:
      fprintf(fp, "%10s","BI");
      break;
    case PTYPE_Skip:
      fprintf(fp, "%10s","Skip");
      break;
    }
    /* Frame coding mode */
    switch (p_pic[pic_index].fcm) {
    case PROGRESSIVE:
      fprintf(fp, "%10s","PR");
      break;
    case FRAME_INTERLACE:
      fprintf(fp, "%10s","FR");
      break;
    case FIELD_INTERLACE:
      fprintf(fp, "%10s","FI");
      break;
    }
    /* Field */
    if (p_pic[pic_index].is_top_field_first == ff)
      fprintf(fp, "%10s","TOP");
    else
      fprintf(fp, "%10s","BOT");

    /* PPMode and BuffIdx */
    switch (p_pic[pic_index].field[!ff].dec_pp_stat) {
    case STAND_ALONE:
      fprintf(fp, "%10s%10d\n", "STAND",pic_index);
      break;
    case PARALLEL:
      fprintf(fp, "%10s%10d\n", "PARAL",pic_index);
      break;
    case PIPELINED:
      fprintf(fp, "%10s%10d\n", "PIPEL",pic_index);
      break;
    default:
      break;
    }
  }

  if (fp) {
    fclose(fp);
    fp = NULL;
  }
}
#endif

/*------------------------------------------------------------------------------
    Vc1DecPpInit()

        Setup dec+pp bookkeeping
------------------------------------------------------------------------------*/
void Vc1DecPpInit( vc1DecPp_t *p_dec_pp, struct BufferQueue *bq ) {
  p_dec_pp->dec_out.dec = BQUEUE_UNUSED;
  p_dec_pp->pp_proc.dec = BQUEUE_UNUSED;
  p_dec_pp->anchor.pp = BQUEUE_UNUSED;
  p_dec_pp->bq_pp = bq;
}

/*------------------------------------------------------------------------------
    Vc1DecPpSetFieldOutput()

        Setup dec+pp bookkeeping
------------------------------------------------------------------------------*/
void Vc1DecPpSetFieldOutput( vc1DecPp_t *p_dec_pp, u32 field_output ) {
  p_dec_pp->field_output = field_output;
}

/*------------------------------------------------------------------------------
    Vc1DecPpNextInput()

        Signal new input frame for dec+pp bookkeeping
------------------------------------------------------------------------------*/
void Vc1DecPpNextInput( vc1DecPp_t *p_dec_pp, u32 frame_pic ) {
  p_dec_pp->dec_out.processed = 0;
  p_dec_pp->dec_out.frame_pic = frame_pic;
}

/*------------------------------------------------------------------------------
    Vc1DecPpBuffer()

        Buffer picture for dec+pp bookkeeping
------------------------------------------------------------------------------*/
void Vc1DecPpBuffer( vc1DecPp_t *p_dec_pp, u32 dec_out, u32 b_frame ) {
  p_dec_pp->dec_out.dec    = dec_out;
  if(!p_dec_pp->field_output)
    p_dec_pp->dec_out.processed = 2;
  else
    p_dec_pp->dec_out.processed++;
  p_dec_pp->dec_out.b_frame = b_frame;
}

/*------------------------------------------------------------------------------
    Vc1DecPpSetPpOutp()

        Setup output frame for dec+pp bookkeeping
------------------------------------------------------------------------------*/
void Vc1DecPpSetPpOutp( vc1DecPp_t *p_dec_pp, DecPpInterface *pc ) {

  if(p_dec_pp->pp_proc.b_frame == 1 ) {
    p_dec_pp->pp_out = p_dec_pp->pp_proc;
  } else {
    p_dec_pp->pp_out = p_dec_pp->anchor;
  }

  pc->display_index = p_dec_pp->pp_out.pp;
}

/*------------------------------------------------------------------------------
    Vc1DecPpSetPpOutpStandalone()

        Setup last frame for dec+pp bookkeeping
------------------------------------------------------------------------------*/
void Vc1DecPpSetPpOutpStandalone( vc1DecPp_t *p_dec_pp, DecPpInterface *pc ) {
  pc->buffer_index = p_dec_pp->pp_out.pp;
  pc->display_index = p_dec_pp->pp_out.pp;
}

/*------------------------------------------------------------------------------
    Vc1DecPpSetPpProc()

        Setup dec+pp bookkeeping; next frame/field for PP to process
------------------------------------------------------------------------------*/
void Vc1DecPpSetPpProc( vc1DecPp_t *p_dec_pp, DecPpInterface *pc ) {
  u32 first_field = 0;


  if(p_dec_pp->pp_proc.dec != BQUEUE_UNUSED) {
    if(p_dec_pp->pp_proc.processed == 0) {
      /* allocate new PP buffer */
      p_dec_pp->pp_proc.pp = BqueueNext( p_dec_pp->bq_pp,
                                         p_dec_pp->anchor.pp,
                                         BQUEUE_UNUSED,
                                         BQUEUE_UNUSED,
                                         p_dec_pp->pp_proc.b_frame);

      pc->buffer_index = p_dec_pp->pp_proc.pp;

      p_dec_pp->pp_proc.processed = p_dec_pp->field_output ? 1 : 2;
      first_field = 1;
    } else {
      p_dec_pp->pp_proc.processed = 2;
    }
  }

  if(first_field)
    Vc1DecPpSetPpOutp( p_dec_pp, pc );

  /* Take anchor */
  if( p_dec_pp->pp_proc.b_frame == 0 ) {
    p_dec_pp->anchor = p_dec_pp->pp_proc;
  }

  /* roll over to next picture buffer */
  if( p_dec_pp->dec_out.processed == 2 ||
      p_dec_pp->dec_out.frame_pic ) {
    p_dec_pp->pp_proc = p_dec_pp->dec_out;
    p_dec_pp->pp_proc.processed = 0;
    p_dec_pp->pp_out.processed = 0;
    p_dec_pp->dec_out.dec = BQUEUE_UNUSED;
  }
}

