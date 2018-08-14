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

#include "vc1hwd_picture_layer.h"
#include "vc1hwd_vlc.h"
#include "vc1hwd_bitplane.h"
#include "vc1hwd_util.h"
#include "vc1hwd_storage.h"
#include "vc1hwd_decoder.h"
#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

/*------------------------------------------------------------------------------
    External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

static void DecodeQpInfo( swStrmStorage_t* storage,
                          pictureLayer_t* p_pic_layer,
                          strmData_t* p_strm_data );

static void DecodeInterInformation( pictureLayer_t* p_pic_layer,
                                    strmData_t* p_strm_data,
                                    u32 vs_transform,
                                    u16x dquant );

static void DecodeIntensityCompensationInfo(strmData_t * const p_strm_data,
    i32 *i_shift, i32 *i_scale );

static void NewFrameDimensions( swStrmStorage_t *storage, resPic_e res );

static void ProcessBitplanes( swStrmStorage_t* storage,
                              pictureLayer_t * p_pic_layer );

static fieldType_e GetFieldType( pictureLayer_t * const p_pic_layer,
                                 u16x is_first_field );

/*------------------------------------------------------------------------------

    Function name: GetFieldType

        Functional description:
            Determines current field type I / P / B / BI

        Inputs:
            p_pic_layer       Pointer to picture layer data structure
            is_first_field    is the first or the second field of the frame

        Outputs:
            None

        Returns:
            Current field type

------------------------------------------------------------------------------*/
fieldType_e GetFieldType( pictureLayer_t * const p_pic_layer,
                          u16x is_first_field ) {
  if (is_first_field) {
    p_pic_layer->is_ff = HANTRO_TRUE;
    p_pic_layer->top_field = (p_pic_layer->tff) ? 1 : 0;

    switch (p_pic_layer->field_pic_type) {
    case FP_I_I:
    case FP_I_P:
      p_pic_layer->pic_type = PTYPE_I;
      return FTYPE_I;
    case FP_P_I:
    case FP_P_P:
      p_pic_layer->pic_type = PTYPE_P;
      return FTYPE_P;
    case FP_B_B:
    case FP_B_BI:
      p_pic_layer->pic_type = PTYPE_B;
      return FTYPE_B;
    default:
      p_pic_layer->pic_type = PTYPE_BI;
      return FTYPE_BI;
    }
  } else {
    p_pic_layer->is_ff = HANTRO_FALSE;
    p_pic_layer->top_field = (p_pic_layer->tff) ? 0 : 1;

    switch (p_pic_layer->field_pic_type) {
    case FP_I_I:
    case FP_P_I:
      p_pic_layer->pic_type = PTYPE_I;
      return FTYPE_I;
    case FP_I_P:
    case FP_P_P:
      p_pic_layer->pic_type = PTYPE_P;
      return FTYPE_P;
    case FP_B_B:
    case FP_BI_B:
      p_pic_layer->pic_type = PTYPE_B;
      return FTYPE_B;
    default:
      p_pic_layer->pic_type = PTYPE_BI;
      return FTYPE_BI;
    }
  }
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdDecodeFieldLayer

        Functional description:
            Decodes field headers

        Inputs:
            storage        Stream storage descriptor
            p_strm_data       Pointer to stream data structure
            is_first_field    is the first or the second field of the frame

        Outputs:
            Field header information

        Returns:
            VC1HWD_FIELD_HDRS_RDY / HANTRO_NOK

------------------------------------------------------------------------------*/
u16x vc1hwdDecodeFieldLayer( swStrmStorage_t *storage,
                             strmData_t *p_strm_data,
                             u16x is_first_field ) {
  u16x ret;
  pictureLayer_t *p_pic_layer;
  fieldType_e ft;
  u32 read_bits_start;

  ASSERT(storage);
  ASSERT(p_strm_data);

  p_pic_layer = &storage->pic_layer;
  p_pic_layer->raw_mask = 0;
  p_pic_layer->dq_profile = DQPROFILE_N_A;

  read_bits_start = p_strm_data->strm_buff_read_bits;

  ft = GetFieldType(p_pic_layer, is_first_field);

  (void)DWLmemset(storage->p_mb_flags, 0,((storage->num_of_mbs+9)/10)*10 );

  /* PQINDEX, HALFQP, PQUANTIZER */
  DecodeQpInfo(storage, p_pic_layer, p_strm_data);

  /* POSTPROQ */
  if (storage->post_proc_flag) {
    p_pic_layer->post_proc = vc1hwdGetBits(p_strm_data, 2);
  }

  /* NUMREF */
  if (ft == FTYPE_P) {
    p_pic_layer->num_ref = vc1hwdGetBits(p_strm_data, 1);
    /* REFFIELD */
    if (p_pic_layer->num_ref == 0)
      p_pic_layer->ref_field = vc1hwdGetBits(p_strm_data, 1);
  }

  if ( (ft == FTYPE_P) || (ft == FTYPE_B) ) {
    /* MVRANGE */
    if (storage->extended_mv)
      p_pic_layer->mv_range = vc1hwdDecodeMvRange(p_strm_data);
    else
      p_pic_layer->mv_range = 0;

    /* DMVRANGE */
    if (storage->extended_dmv)
      p_pic_layer->dmv_range = vc1hwdDecodeDmvRange(p_strm_data);
    else
      p_pic_layer->dmv_range = 0;

    /* MVMODE/MVMODE2 */
    if (ft == FTYPE_P) {
      p_pic_layer->mvmode = vc1hwdDecodeMvMode(p_strm_data,
                            HANTRO_FALSE, p_pic_layer->pquant,
                            &p_pic_layer->intensity_compensation);

      if (p_pic_layer->intensity_compensation) {
        /* INTCOMPFIELD */
        p_pic_layer->int_comp_field = vc1hwdDecodeIntCompField(p_strm_data);

        /* LUMSCALE and LUMSHIFT */
        if (p_pic_layer->int_comp_field == IC_BOTTOM_FIELD) {
          DecodeIntensityCompensationInfo(p_strm_data,
                                          &p_pic_layer->i_shift2,
                                          &p_pic_layer->i_scale2);
        } else {
          DecodeIntensityCompensationInfo(p_strm_data,
                                          &p_pic_layer->i_shift,
                                          &p_pic_layer->i_scale);
        }
        if (p_pic_layer->int_comp_field == IC_BOTH_FIELDS) {
          /* LUMSCALE2 and LUMSHIFT2 */
          DecodeIntensityCompensationInfo(p_strm_data,
                                          &p_pic_layer->i_shift2,
                                          &p_pic_layer->i_scale2);
        }
      } else {
        p_pic_layer->int_comp_field = IC_NONE;
      }
    } else { /* FTYPE_B */
      p_pic_layer->num_ref = 1; /* Always 2 reference fields for B field
                                    * pictures */
      p_pic_layer->mvmode = vc1hwdDecodeMvModeB(p_strm_data,
                            p_pic_layer->pquant);

      /* FORWARDMB Bitplane */
      ret = vc1hwdDecodeBitPlane(p_strm_data,
                                 storage->pic_width_in_mbs,
                                 ( storage->pic_height_in_mbs + 1 ) / 2,
                                 storage->p_mb_flags,
                                 MB_FORWARD_MB, &p_pic_layer->raw_mask,
                                 MB_FORWARD_MB, storage->sync_marker );
      if (ret != HANTRO_OK)
        return(ret);

    }
  }

  if ( (ft == FTYPE_P) || (ft == FTYPE_B) ) {
    /* MBMODETAB */
    p_pic_layer->mb_mode_tab = vc1hwdGetBits(p_strm_data, 3);

    /* IMVTAB */
    if ((ft == FTYPE_P) && (p_pic_layer->num_ref == 0))
      p_pic_layer->mv_table_index = vc1hwdGetBits(p_strm_data, 2);
    else
      p_pic_layer->mv_table_index = vc1hwdGetBits(p_strm_data, 3);
    /* ICBPTAB */
    p_pic_layer->cbp_table_index = vc1hwdGetBits(p_strm_data, 3);
    /* 4MVBPTAB */
    if (p_pic_layer->mvmode == MVMODE_MIXEDMV )
      p_pic_layer->mvbp_table_index4 = vc1hwdGetBits(p_strm_data, 2);
    /* VOPDQUANT */
    if (storage->dquant != 0) {
      vc1hwdDecodeVopDquant(p_strm_data, storage->dquant, p_pic_layer);

      /* Note: if DQ_PROFILE is ALL_MACROBLOCKS and DQBILEVEL
       * is zero then set HALFQP to zero as in that case no
       * MQUANT is considered to be derived from PQUANT. */
      if( p_pic_layer->dq_profile == DQPROFILE_ALL_MACROBLOCKS &&
          p_pic_layer->dqbi_level == 0 )
        p_pic_layer->half_qp = 0;
    }
    /* TTMBF */
    p_pic_layer->mb_level_transform_type_flag = 1;
    p_pic_layer->tt_frm = TT_8x8;
    if ( storage->vs_transform ) {
      p_pic_layer->mb_level_transform_type_flag = vc1hwdGetBits(p_strm_data, 1);
      if( p_pic_layer->mb_level_transform_type_flag == 1 ) {
        /* TTFRM */
        p_pic_layer->tt_frm = (transformType_e)vc1hwdGetBits(p_strm_data, 2);
      }
    }
  }

  if ( (ft == FTYPE_I) || (ft == FTYPE_BI) ) {
    /* ACPRED Bitplane */
    ret = vc1hwdDecodeBitPlane(p_strm_data,
                               storage->pic_width_in_mbs,
                               ( storage->pic_height_in_mbs + 1 ) / 2,
                               storage->p_mb_flags,
                               MB_AC_PRED, &p_pic_layer->raw_mask,
                               MB_AC_PRED, storage->sync_marker );
    if (ret != HANTRO_OK)
      return(ret);

    if( storage->overlap && (p_pic_layer->pquant <= 8) ) {
      /* CONDOVER */
      p_pic_layer->cond_over = vc1hwdDecodeCondOver(p_strm_data);

      /* OVERFLAGS Bitplane */
      if (p_pic_layer->cond_over == 3) {
        ret = vc1hwdDecodeBitPlane(p_strm_data,
                                   storage->pic_width_in_mbs,
                                   ( storage->pic_height_in_mbs + 1 ) / 2,
                                   storage->p_mb_flags,
                                   MB_OVERLAP_SMOOTH, &p_pic_layer->raw_mask,
                                   MB_OVERLAP_SMOOTH, storage->sync_marker);
        if (ret != HANTRO_OK)
          return(ret);
      } else {
        p_pic_layer->raw_mask |= MB_OVERLAP_SMOOTH;
      }
    } else {
      p_pic_layer->raw_mask |= MB_OVERLAP_SMOOTH;
    }
  }
  /* TRANSACFRM */
  p_pic_layer->ac_coding_set_index_cb_cr = vc1hwdDecodeTransAcFrm(p_strm_data);

  /* TRANSACFRM2 */
  if ((ft == FTYPE_I) || (ft == FTYPE_BI)) {
    p_pic_layer->ac_coding_set_index_y = vc1hwdDecodeTransAcFrm(p_strm_data);
  } else {
    p_pic_layer->ac_coding_set_index_y = p_pic_layer->ac_coding_set_index_cb_cr;
  }
  /* TRANSDCTAB */
  p_pic_layer->intra_transform_dc_index = vc1hwdGetBits(p_strm_data, 1);

  /* VOPDQUANT */
  if ( (storage->dquant != 0) &&
       ((ft == FTYPE_I) || (ft == FTYPE_BI))) {
    vc1hwdDecodeVopDquant(p_strm_data, storage->dquant, p_pic_layer);

    /* Note: if DQ_PROFILE is ALL_MACROBLOCKS and DQBILEVEL is zero then set
     * HALFQP to zero as in that case no MQUANT is considered to be derived
     * from PQUANT. */
    if( p_pic_layer->dq_profile == DQPROFILE_ALL_MACROBLOCKS &&
        p_pic_layer->dqbi_level == 0 )
      p_pic_layer->half_qp = 0;
  }

  p_pic_layer->field_header_bits = p_strm_data->strm_buff_read_bits - read_bits_start;

#ifdef ASIC_TRACE_SUPPORT
  {
    mvmode_e mv_mode = p_pic_layer->mvmode;

    if (MVMODE_1MV == mv_mode || MVMODE_MIXEDMV == mv_mode) {
      trace_vc1_dec_tools.qpel_luma = 1;
      if (!storage->fast_uv_mc)
        trace_vc1_dec_tools.qpel_chroma = 1;
    }

  }
#endif

  ProcessBitplanes( storage, p_pic_layer );

  return VC1HWD_FIELD_HDRS_RDY;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdDecodePictureLayerAP

        Functional description:
            Decodes advanced profile picture layer headers

        Inputs:
            storage    Stream storage descriptor
            p_strm_data   Pointer to stream data structure

        Outputs:
            Picture layer information

        Returns:
            VC1HWD_PIC_HDRS_RDY / VC1HWD_FIELD_HDRS_RDY / HANTRO_NOK
------------------------------------------------------------------------------*/
u16x vc1hwdDecodePictureLayerAP( swStrmStorage_t *storage,
                                 strmData_t *p_strm_data ) {
  pictureLayer_t *p_pic_layer;
  u16x ret, i;
  u16x number_of_pan_scan_windows = 0;

  ASSERT(storage);
  ASSERT(p_strm_data);

  p_pic_layer = &storage->pic_layer;

  p_pic_layer->raw_mask = 0;
  p_pic_layer->intensity_compensation = 0;

  (void)DWLmemset(storage->p_mb_flags, 0,((storage->num_of_mbs+9)/10)*10 );

  /* init these here */
  p_pic_layer->is_ff = HANTRO_TRUE;
  p_pic_layer->top_field = HANTRO_TRUE;
  p_pic_layer->int_comp_field = IC_NONE;
  p_pic_layer->dquant_in_frame = HANTRO_FALSE;
  p_pic_layer->dq_profile = DQPROFILE_N_A;

  /* FCM */
  if (storage->interlace)
    p_pic_layer->fcm = vc1hwdDecodeFcm(p_strm_data);
  else
    p_pic_layer->fcm = PROGRESSIVE;

  /* pic_type is set for field pictures in GetFieldType function */
  if (storage->interlace && (p_pic_layer->fcm == FIELD_INTERLACE)) {
    /* FPTYPE */
    p_pic_layer->field_pic_type = (fpType_e)vc1hwdGetBits(p_strm_data, 3);
    /* set pic_type for current field */
    (void)GetFieldType(p_pic_layer, 1);
  } else {
    /* PTYPE */
    p_pic_layer->pic_type =
      vc1hwdDecodePtype(p_strm_data, HANTRO_TRUE, storage->max_bframes);
  }
  /* TFCNTR */
  if (storage->tfcntr_flag && (p_pic_layer->pic_type != PTYPE_Skip))
    p_pic_layer->tfcntr = vc1hwdGetBits(p_strm_data, 8);

  if (storage->pull_down) {
    /* RPTFRM */
    if ( (storage->interlace == 0) || (storage->psf == 1) ) {
      p_pic_layer->rptfrm = vc1hwdGetBits(p_strm_data, 2);
    } else {
      /* TFF and RFF */
      p_pic_layer->tff = vc1hwdGetBits(p_strm_data, 1);
      p_pic_layer->rff = vc1hwdGetBits(p_strm_data, 1);
    }
  } else {
    p_pic_layer->rptfrm = 0;
    p_pic_layer->tff = HANTRO_TRUE;
    p_pic_layer->rff = HANTRO_FALSE;
  }
  if (storage->pan_scan_flag) {
    /* PSPRESENT */
    p_pic_layer->ps_present = vc1hwdGetBits(p_strm_data, 1);
    if (p_pic_layer->ps_present) {
      if ((storage->interlace == 1) && (storage->psf == 0)) {
        if (storage->pull_down)
          number_of_pan_scan_windows = 2 + p_pic_layer->rff;
        else
          number_of_pan_scan_windows = 2;
      } else {
        if (storage->pull_down)
          number_of_pan_scan_windows = 1 + p_pic_layer->rptfrm;
        else
          number_of_pan_scan_windows = 1;
      }
      /* PS_HOFFSET, PS_VOFFSET, PS_WIDTH, PS_HEIGHT */
      for (i = 0; i < number_of_pan_scan_windows; i++) {
        p_pic_layer->psw.h_offset = vc1hwdGetBits(p_strm_data, 18);
        p_pic_layer->psw.v_offset = vc1hwdGetBits(p_strm_data, 18);
        p_pic_layer->psw.width = vc1hwdGetBits(p_strm_data, 14);
        p_pic_layer->psw.height = vc1hwdGetBits(p_strm_data, 14);
      }
    }
  }

  /* return if skipped progressive frame */
  if (p_pic_layer->pic_type == PTYPE_Skip) {
    return VC1HWD_PIC_HDRS_RDY;
  }

  /* RNDCTRL */
  storage->rnd = 1 - vc1hwdGetBits(p_strm_data, 1);
  /* UVSAMP */
  if (storage->interlace)
    p_pic_layer->uv_samp = vc1hwdGetBits(p_strm_data, 1);
  /* INTERPFRM */
  if (storage->finterp_flag && (p_pic_layer->fcm == PROGRESSIVE))
    p_pic_layer->interp_frm = vc1hwdGetBits(p_strm_data, 1);

  /* REFDIST */
  if ( storage->ref_dist_flag &&
       p_pic_layer->fcm == FIELD_INTERLACE ) {
    if (p_pic_layer->field_pic_type < 4)
      p_pic_layer->ref_dist = vc1hwdDecodeRefDist( p_strm_data );
    /* Use previous anchor value for B/B etc field pairs */
  } else
    p_pic_layer->ref_dist = 0;

  /* BFRACTION */
  if( ((p_pic_layer->pic_type == PTYPE_B) && (p_pic_layer->fcm == PROGRESSIVE)) ||
      ((p_pic_layer->field_pic_type > 3) && (p_pic_layer->fcm == FIELD_INTERLACE))) {
    p_pic_layer->bfraction = vc1hwdDecodeBfraction( p_strm_data,
                             &p_pic_layer->scale_factor );
  }

  /* Decode rest of interlace field picture headers */
  if (p_pic_layer->fcm == FIELD_INTERLACE) {
    ret = vc1hwdDecodeFieldLayer(storage, p_strm_data, HANTRO_TRUE);
    return ret;
  }

  /* PQINDEX, HALFQP, PQUANTIZER */
  DecodeQpInfo(storage, p_pic_layer, p_strm_data);

  /* POSTPROQ */
  if (storage->post_proc_flag) {
    p_pic_layer->post_proc = vc1hwdGetBits(p_strm_data, 2);
  }

  /* BFRACTION */
  if ((p_pic_layer->pic_type == PTYPE_B) && (p_pic_layer->fcm == FRAME_INTERLACE)) {
    p_pic_layer->bfraction = vc1hwdDecodeBfraction( p_strm_data,
                             &p_pic_layer->scale_factor );
  }

  if ((p_pic_layer->pic_type == PTYPE_I) || (p_pic_layer->pic_type == PTYPE_BI)) {

    if (p_pic_layer->fcm == FRAME_INTERLACE) {
      /* FIELDTX */
      ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                                 storage->pic_height_in_mbs, storage->p_mb_flags,
                                 MB_FIELD_TX, &p_pic_layer->raw_mask,
                                 MB_FIELD_TX, storage->sync_marker );
      if (ret != HANTRO_OK)
        return(ret);
    }

    /* ACPRED Bitplane */
    ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                               storage->pic_height_in_mbs, storage->p_mb_flags,
                               MB_AC_PRED, &p_pic_layer->raw_mask,
                               MB_AC_PRED, storage->sync_marker );
    if (ret != HANTRO_OK)
      return(ret);

    if( storage->overlap && (p_pic_layer->pquant <= 8) ) {
      /* CONDOVER */
      p_pic_layer->cond_over = vc1hwdDecodeCondOver(p_strm_data);

      /* OVERFLAGS Bitplane */
      if (p_pic_layer->cond_over == 3) {
        ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                                   storage->pic_height_in_mbs, storage->p_mb_flags,
                                   MB_OVERLAP_SMOOTH, &p_pic_layer->raw_mask,
                                   MB_OVERLAP_SMOOTH, storage->sync_marker);
        if (ret != HANTRO_OK)
          return(ret);
      } else {
        p_pic_layer->raw_mask |= MB_OVERLAP_SMOOTH;
      }
    } else {
      p_pic_layer->raw_mask |= MB_OVERLAP_SMOOTH;
    }
  }
  if ((p_pic_layer->pic_type == PTYPE_P) || (p_pic_layer->pic_type == PTYPE_B)) {
    /* MVRANGE */
    if (storage->extended_mv)
      p_pic_layer->mv_range = vc1hwdDecodeMvRange(p_strm_data);
    else
      p_pic_layer->mv_range = 0;

    /* DMVRANGE */
    if (storage->extended_dmv && (p_pic_layer->fcm != PROGRESSIVE))
      p_pic_layer->dmv_range = vc1hwdDecodeDmvRange(p_strm_data);
    else
      p_pic_layer->dmv_range = 0;

    /* MVMODE/MVMODE2 */
    if (p_pic_layer->fcm == PROGRESSIVE) {
      p_pic_layer->mvmode =
        vc1hwdDecodeMvMode(p_strm_data, (p_pic_layer->pic_type == PTYPE_B),
                           p_pic_layer->pquant, &p_pic_layer->intensity_compensation);
    }
  }

  if ( p_pic_layer->fcm == FRAME_INTERLACE ) {
    /* 4MVSWITCH */
    if (p_pic_layer->pic_type == PTYPE_P) {
      p_pic_layer->mv_switch = vc1hwdGetBits(p_strm_data, 1);
      p_pic_layer->mvmode =
        p_pic_layer->mv_switch ? MVMODE_MIXEDMV : MVMODE_1MV;
    } else {
      p_pic_layer->mv_switch = 0;
      p_pic_layer->mvmode = MVMODE_1MV;
    }
  }

  /* INTCOMP */
  if ( ((p_pic_layer->pic_type == PTYPE_B) ||
        (p_pic_layer->pic_type == PTYPE_P)) &&
       (p_pic_layer->fcm == FRAME_INTERLACE))
    p_pic_layer->intensity_compensation = vc1hwdGetBits(p_strm_data, 1);

  /* LUMSCALE and LUMSHIFT */
  if ( p_pic_layer->intensity_compensation &&
       (p_pic_layer->fcm == FRAME_INTERLACE)) {
    p_pic_layer->int_comp_field = IC_BOTH_FIELDS;
    DecodeIntensityCompensationInfo(p_strm_data,
                                    &p_pic_layer->i_shift,
                                    &p_pic_layer->i_scale);
  }

  if ((p_pic_layer->pic_type == PTYPE_P) && (p_pic_layer->fcm == PROGRESSIVE)) {
    if (p_pic_layer->intensity_compensation) {
      /* LUMSCALE and LUMSHIFT */
      p_pic_layer->int_comp_field = IC_BOTH_FIELDS;
      DecodeIntensityCompensationInfo(p_strm_data,
                                      &p_pic_layer->i_shift,
                                      &p_pic_layer->i_scale);
    }
    /* MVTYPEMB Bitplane */
    if( p_pic_layer->mvmode == MVMODE_MIXEDMV ) {
      ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                                 storage->pic_height_in_mbs, storage->p_mb_flags,
                                 MB_4MV, &p_pic_layer->raw_mask, MB_4MV, storage->sync_marker);
      if (ret != HANTRO_OK)
        return(ret);
    }
  }
  if (p_pic_layer->pic_type == PTYPE_B) {
    /* DIRECTMB Bitplane */
    ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                               storage->pic_height_in_mbs, storage->p_mb_flags,
                               MB_4MV, &p_pic_layer->raw_mask,  MB_DIRECT, storage->sync_marker);
    if (ret != HANTRO_OK) {
      return(ret);
    }
  }
  if ((p_pic_layer->pic_type == PTYPE_P) || (p_pic_layer->pic_type == PTYPE_B)) {
    /* SKIPMB Bitplane */
    ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                               storage->pic_height_in_mbs, storage->p_mb_flags,
                               MB_SKIPPED, &p_pic_layer->raw_mask,
                               MB_SKIPPED, storage->sync_marker );
    if (ret != HANTRO_OK)
      return(ret);

    /* Decode Inter Stuff */
    if (p_pic_layer->fcm == PROGRESSIVE) {
      DecodeInterInformation( p_pic_layer, p_strm_data,
                              storage->vs_transform,
                              storage->dquant);
    }

    if( p_pic_layer->fcm == FRAME_INTERLACE ) {
      /* MBMODETAB */
      p_pic_layer->mb_mode_tab = vc1hwdGetBits(p_strm_data, 2);
      /* IMVTAB */
      p_pic_layer->mv_table_index = vc1hwdGetBits(p_strm_data, 2);
      /* ICBPTAB */
      p_pic_layer->cbp_table_index = vc1hwdGetBits(p_strm_data, 3);
      /* 2MVBPTAB */
      p_pic_layer->mvbp_table_index2 = vc1hwdGetBits(p_strm_data, 2);
      /* 4MVBPTAB */
      if ( (p_pic_layer->pic_type == PTYPE_B) ||
           ((p_pic_layer->pic_type == PTYPE_P) && (p_pic_layer->mv_switch==1)) )
        p_pic_layer->mvbp_table_index4 = vc1hwdGetBits(p_strm_data, 2);
    }

    /* VOPDQUANT */
    if( p_pic_layer->fcm != PROGRESSIVE) {
      if (storage->dquant != 0) {
        vc1hwdDecodeVopDquant(p_strm_data, storage->dquant, p_pic_layer);

        /* Note: if DQ_PROFILE is ALL_MACROBLOCKS and DQBILEVEL
         * is zero then set HALFQP to zero as in that case no
         * MQUANT is considered to be derived from PQUANT. */
        if( p_pic_layer->dq_profile == DQPROFILE_ALL_MACROBLOCKS &&
            p_pic_layer->dqbi_level == 0 )
          p_pic_layer->half_qp = 0;
      }

      /* TTMBF */
      p_pic_layer->mb_level_transform_type_flag = 1;
      p_pic_layer->tt_frm = TT_8x8;
      if ( storage->vs_transform ) {
        p_pic_layer->mb_level_transform_type_flag=vc1hwdGetBits(p_strm_data, 1);
        if( p_pic_layer->mb_level_transform_type_flag == 1 ) {
          /* TTFRM */
          p_pic_layer->tt_frm =
            (transformType_e)vc1hwdGetBits(p_strm_data, 2);
        }
      }
    }
  }

  /* TRANSACFRM */
  p_pic_layer->ac_coding_set_index_cb_cr = vc1hwdDecodeTransAcFrm(p_strm_data);

  /* TRANSACFRM2 */
  if ((p_pic_layer->pic_type == PTYPE_I) || (p_pic_layer->pic_type == PTYPE_BI)) {
    p_pic_layer->ac_coding_set_index_y = vc1hwdDecodeTransAcFrm(p_strm_data);
  } else {
    p_pic_layer->ac_coding_set_index_y = p_pic_layer->ac_coding_set_index_cb_cr;
  }
  /* TRANSDCTAB */
  p_pic_layer->intra_transform_dc_index = vc1hwdGetBits(p_strm_data, 1);

  /* VOPDQUANT */
  if ( (storage->dquant != 0) &&
       ((p_pic_layer->pic_type == PTYPE_I) || (p_pic_layer->pic_type == PTYPE_BI))) {
    vc1hwdDecodeVopDquant(p_strm_data, storage->dquant, p_pic_layer);

    /* Note: if DQ_PROFILE is ALL_MACROBLOCKS and DQBILEVEL is zero then set
     * HALFQP to zero as in that case no MQUANT is considered to be derived
     * from PQUANT. */
    if( p_pic_layer->dq_profile == DQPROFILE_ALL_MACROBLOCKS &&
        p_pic_layer->dqbi_level == 0 )
      p_pic_layer->half_qp = 0;
  }

#ifdef ASIC_TRACE_SUPPORT
  {
    mvmode_e mv_mode = p_pic_layer->mvmode;

    if (MVMODE_1MV == mv_mode || MVMODE_MIXEDMV == mv_mode) {
      trace_vc1_dec_tools.qpel_luma = 1;
      if (!storage->fast_uv_mc)
        trace_vc1_dec_tools.qpel_chroma = 1;
    }

  }
#endif

  ProcessBitplanes( storage, p_pic_layer );

  return VC1HWD_PIC_HDRS_RDY;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdDecodePictureLayer

        Functional description:
            Decodes simple and main profile picture layer

        Inputs:
            storage    Stream storage descriptor
            p_strm_data   Pointer to stream data structure

        Outputs:
            Picture layer information

        Returns:
            VC1HWD_PIC_HDRS_RDY / HANTRO_NOK

------------------------------------------------------------------------------*/

u16x vc1hwdDecodePictureLayer(swStrmStorage_t *storage, strmData_t *p_strm_data) {
  pictureLayer_t *p_pic_layer;
  i16x tmp;
  u16x ret;
  resPic_e    res_pic;

  ASSERT(storage);
  ASSERT(p_strm_data);

  p_pic_layer = &storage->pic_layer;

  p_pic_layer->raw_mask = 0;

  /* init values for siple and main profiles */
  p_pic_layer->fcm = PROGRESSIVE;
  p_pic_layer->is_ff = HANTRO_TRUE;
  p_pic_layer->top_field = HANTRO_TRUE;
  p_pic_layer->tff = HANTRO_TRUE;
  p_pic_layer->int_comp_field = IC_NONE;
  p_pic_layer->dq_profile = DQPROFILE_N_A;

  /* INTERPFRM */
  if (storage->frame_interp_flag) {
    p_pic_layer->interp_frm = vc1hwdGetBits(p_strm_data, 1);
  }

  /* FRMCNT */
  p_pic_layer->frame_count = vc1hwdGetBits(p_strm_data, 2);

  /* RANGEREDFRM */
  if (storage->range_red) {
    p_pic_layer->range_red_frm = vc1hwdGetBits(p_strm_data,1);
  }

  /* PTYPE */
  p_pic_layer->pic_type =
    vc1hwdDecodePtype(p_strm_data,HANTRO_FALSE,storage->max_bframes);

#ifdef _DPRINT_PRINT
  switch(p_pic_layer->pic_type) {
  case PTYPE_P:
    DPRINT(("PTYPE=P\n"));
    break;
  case PTYPE_I:
    DPRINT(("PTYPE=I\n"));
    break;
  case PTYPE_B:
    DPRINT(("PTYPE=B\n"));
    break;
  case PTYPE_BI:
    DPRINT(("PTYPE=BI\n"));
    break;
  }
#endif /*_DPRINT_PRINT*/

  /* BFRACTION */
  if( p_pic_layer->pic_type == PTYPE_B) {
    p_pic_layer->bfraction = vc1hwdDecodeBfraction( p_strm_data,
                             &p_pic_layer->scale_factor );

    if( p_pic_layer->bfraction == BFRACT_PTYPE_BI ) {
      p_pic_layer->pic_type = PTYPE_BI;
    }

#ifdef _DPRINT_PRINT
    DPRINT(("BFRACTION: "));
    switch( p_pic_layer->bfraction ) {
    case BFRACT_1_2:
      DPRINT(("1/2\n"));
      break;
    case BFRACT_1_3:
      DPRINT(("1/3\n"));
      break;
    case BFRACT_2_3:
      DPRINT(("2/3\n"));
      break;
    case BFRACT_1_4:
      DPRINT(("1/4\n"));
      break;
    case BFRACT_3_4:
      DPRINT(("3/4\n"));
      break;
    case BFRACT_1_5:
      DPRINT(("1/5\n"));
      break;
    case BFRACT_2_5:
      DPRINT(("2/5\n"));
      break;
    case BFRACT_3_5:
      DPRINT(("3/5\n"));
      break;
    case BFRACT_4_5:
      DPRINT(("4/5\n"));
      break;
    case BFRACT_1_6:
      DPRINT(("1/6\n"));
      break;
    case BFRACT_5_6:
      DPRINT(("5/6\n"));
      break;
    case BFRACT_1_7:
      DPRINT(("1/7\n"));
      break;
    case BFRACT_2_7:
      DPRINT(("2/7\n"));
      break;
    case BFRACT_3_7:
      DPRINT(("3/7\n"));
      break;
    case BFRACT_4_7:
      DPRINT(("4/7\n"));
      break;
    case BFRACT_5_7:
      DPRINT(("5/7\n"));
      break;
    case BFRACT_6_7:
      DPRINT(("6/7\n"));
      break;
    case BFRACT_1_8:
      DPRINT(("1/8\n"));
      break;
    case BFRACT_3_8:
      DPRINT(("3/8\n"));
      break;
    case BFRACT_5_8:
      DPRINT(("5/8\n"));
      break;
    case BFRACT_7_8:
      DPRINT(("7/8\n"));
      break;
    case BFRACT_SMPTE_RESERVED:
      DPRINT(("SMPTE Reserved\n"));
      break;
    case BFRACT_PTYPE_BI:
      DPRINT(("BI\n"));
      break;
    }

#endif


  }

  /* Initialize dquantInFrm for intra pictures... */
  if (p_pic_layer->pic_type == PTYPE_I ||
      p_pic_layer->pic_type == PTYPE_BI ) {
    p_pic_layer->dquant_in_frame = HANTRO_FALSE;
  }

  /* BF */
  if (p_pic_layer->pic_type == PTYPE_I ||
      p_pic_layer->pic_type == PTYPE_BI ) {
    p_pic_layer->buffer_fullness = vc1hwdGetBits(p_strm_data, 7);

  }

  /* PQINDEX, HALFQP, PQUANTIZER */
  DecodeQpInfo(storage, p_pic_layer, p_strm_data);

  /* MVRANGE */
  if (storage->extended_mv) {
    p_pic_layer->mv_range = vc1hwdDecodeMvRange(p_strm_data);
  } else
    p_pic_layer->mv_range = 0;

  /* RESPIC */
  if (((p_pic_layer->pic_type == PTYPE_I) ||
       (p_pic_layer->pic_type == PTYPE_P)) &&
      storage->multi_res ) {
    res_pic = (resPic_e)vc1hwdGetBits(p_strm_data, 2);

#ifdef ASIC_TRACE_SUPPORT
    if (res_pic)
      trace_vc1_dec_tools.multi_resolution = 1;
#endif

    if( res_pic != p_pic_layer->res_pic ) {
      if( p_pic_layer->pic_type == PTYPE_I ) {
        /* Picture dimensions changed */
        DPRINT(("Picture dimensions changed! (%d)-->(%d)\n",
                p_pic_layer->res_pic, res_pic));
        NewFrameDimensions( storage, res_pic );
        p_pic_layer->res_pic = res_pic;

        storage->resolution_changed = HANTRO_TRUE;

        /* Check against minimum size.
         * Maximum size is check in Seq layer */
        if ( (storage->cur_coded_width < storage->min_coded_width) ||
             (storage->cur_coded_height < storage->min_coded_height) ) {
          EPRINT("Error! Changed picture dimensions are smaller "\
                 "than the allowed minimum!");
          return (HANTRO_NOK);
        }
      } else {
        EPRINT("Error! RESPIC element of P picture differs from "\
               "previous I picture!");
        return(HANTRO_NOK);
      }
    }
  }

  if (p_pic_layer->pic_type == PTYPE_P) {

    (void)DWLmemset(storage->p_mb_flags, 0,((storage->num_of_mbs+9)/10)*10 );

    /* MVMODE */
    p_pic_layer->mvmode =
      vc1hwdDecodeMvMode(p_strm_data, HANTRO_FALSE, p_pic_layer->pquant,
                         &p_pic_layer->intensity_compensation);

    if (p_pic_layer->mvmode > MVMODE_MIXEDMV)
      return(HANTRO_NOK);
#if defined(_DPRINT_PRINT)
    DPRINT(("MVMODE: "));
    switch( p_pic_layer->mvmode ) {
    case MVMODE_MIXEDMV:
      DPRINT(("Mixed-MV\n"));
      break;
    case MVMODE_1MV:
      DPRINT(("1MV\n"));
      break;
    case MVMODE_1MV_HALFPEL:
      DPRINT(("1MV-halfpel\n"));
      break;
    case MVMODE_1MV_HALFPEL_LINEAR:
      DPRINT(("1MV-halfpel-linear\n"));
      break;
    }
#endif  /* defined(_DPRINT_PRINT) */

    if (p_pic_layer->intensity_compensation) {
      DPRINT(("Intensity compensation used.\n"));
      p_pic_layer->int_comp_field = IC_BOTH_FIELDS;
      DecodeIntensityCompensationInfo(p_strm_data,
                                      &p_pic_layer->i_shift,
                                      &p_pic_layer->i_scale);
    }

    /* MVTYPEMB Bitplane */
    if( p_pic_layer->mvmode == MVMODE_MIXEDMV ) {
      ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                                 storage->pic_height_in_mbs, storage->p_mb_flags,
                                 MB_4MV, &p_pic_layer->raw_mask, MB_4MV, storage->sync_marker );
      if (ret != HANTRO_OK)
        return(ret);
    }

    /* SKIPMB Bitplane */
    ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                               storage->pic_height_in_mbs, storage->p_mb_flags,
                               MB_SKIPPED, &p_pic_layer->raw_mask,
                               MB_SKIPPED, storage->sync_marker );
    if (ret != HANTRO_OK) {
      if (p_pic_layer->frame_count == 3 &&
          vc1hwdShowBits(p_strm_data, 8) == 0x0F) {
        (void)vc1hwdFlushBits(p_strm_data, 8);
        p_pic_layer->pic_type = PTYPE_I;
      } else
        return(ret);
    }

    /* Decode Inter Stuff */
    DecodeInterInformation(p_pic_layer, p_strm_data,
                           storage->vs_transform,
                           storage->dquant);
  } else if (p_pic_layer->pic_type == PTYPE_B) {
    (void)DWLmemset(storage->p_mb_flags, 0,((storage->num_of_mbs+9)/10)*10 );

    /* MVMODE */
    p_pic_layer->mvmode =
      vc1hwdDecodeMvMode(p_strm_data, HANTRO_TRUE, p_pic_layer->pquant,
                         (u32*)&tmp ); /* preserve intensity compensation status from
                            * previous anchor frame */

#if defined(_DPRINT_PRINT)
    DPRINT(("MVMODE: "));
    switch( p_pic_layer->mvmode ) {
    case MVMODE_MIXEDMV:
      DPRINT(("Mixed-MV\n"));
      break;
    case MVMODE_1MV:
      DPRINT(("1MV\n"));
      break;
    case MVMODE_1MV_HALFPEL:
      DPRINT(("1MV-halfpel\n"));
      break;
    case MVMODE_1MV_HALFPEL_LINEAR:
      DPRINT(("1MV-halfpel-linear\n"));
      break;
    }
#endif  /* defined(_DPRINT_PRINT) */

    /* DIRECTMB Bitplane */
    ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                               storage->pic_height_in_mbs, storage->p_mb_flags,
                               MB_4MV, &p_pic_layer->raw_mask,  MB_DIRECT, storage->sync_marker);
    if (ret != HANTRO_OK) {
      return(ret);
    }

    /* SKIPMB Bitplane */
    ret = vc1hwdDecodeBitPlane(p_strm_data, storage->pic_width_in_mbs,
                               storage->pic_height_in_mbs, storage->p_mb_flags,
                               MB_SKIPPED, &p_pic_layer->raw_mask,
                               MB_SKIPPED, storage->sync_marker);
    if (ret != HANTRO_OK) {
      return(ret);
    }

    /* Decode Inter Stuff */
    DecodeInterInformation(p_pic_layer, p_strm_data,
                           storage->vs_transform,
                           storage->dquant);
  } else if (p_pic_layer->pic_type == PTYPE_I) {
    p_pic_layer->intensity_compensation = HANTRO_FALSE;
  }

  /* AC pred always raw coded for SP/MP content */
  if( p_pic_layer->pic_type == PTYPE_I || p_pic_layer->pic_type == PTYPE_BI )
    p_pic_layer->raw_mask |= MB_AC_PRED;

  /* TRANSACFRM */
  p_pic_layer->ac_coding_set_index_cb_cr = vc1hwdDecodeTransAcFrm(p_strm_data);

  /* TRANSACFRM2 */
  if ((p_pic_layer->pic_type == PTYPE_I) || (p_pic_layer->pic_type == PTYPE_BI)) {
    p_pic_layer->ac_coding_set_index_y = vc1hwdDecodeTransAcFrm(p_strm_data);

  } else {
    p_pic_layer->ac_coding_set_index_y = p_pic_layer->ac_coding_set_index_cb_cr;
  }
  /* TRANSDCTAB */
  p_pic_layer->intra_transform_dc_index = vc1hwdGetBits(p_strm_data, 1);

#ifdef ASIC_TRACE_SUPPORT
  {
    mvmode_e mv_mode = p_pic_layer->mvmode;

    if (MVMODE_1MV == mv_mode || MVMODE_MIXEDMV == mv_mode) {
      trace_vc1_dec_tools.qpel_luma = 1;
      if (!storage->fast_uv_mc)
        trace_vc1_dec_tools.qpel_chroma = 1;
    }

  }
#endif

  ProcessBitplanes( storage, p_pic_layer );
  return VC1HWD_PIC_HDRS_RDY;
}

/*------------------------------------------------------------------------------

    Function name: DecodeQpInfo

        Functional description:
            Decodes quantization parameters

        Inputs:
            storage        Pointer to stream storage descriptor
            p_pic_layer       Pointer to picture layer descriptor
            p_strm_data       Pointer to stream data

        Outputs:
            pq_index, pquant, uniform_quantizer, half_qp

        Returns:
            None

------------------------------------------------------------------------------*/
void DecodeQpInfo( swStrmStorage_t* storage,
                   pictureLayer_t* p_pic_layer,
                   strmData_t* p_strm_data ) {
  u32 tmp;

  /* PQINDEX */
  tmp = vc1hwdGetBits(p_strm_data, 5);

  if (tmp == 0)
    tmp = 1; /* force to valid PQINDEX value */

  /* store pq_index */
  p_pic_layer->pq_index = tmp;
  if (storage->quantizer == 0) {
    /* Implicit quantizer (Table 36 in standard)*/
    if (tmp <= 8) {
      p_pic_layer->pquant = tmp;
      p_pic_layer->uniform_quantizer = HANTRO_TRUE;
    } else if (tmp >= 29) {
      if (tmp == 29)
        p_pic_layer->pquant = 27;
      else if (tmp == 30)
        p_pic_layer->pquant = 29;
      else /*if (tmp == 31)*/
        p_pic_layer->pquant = 31;

      p_pic_layer->uniform_quantizer = HANTRO_FALSE;
    } else {
      p_pic_layer->pquant = tmp-3;
      p_pic_layer->uniform_quantizer = HANTRO_FALSE;
    }
  } else {
    /* Explicit quantizer */
    p_pic_layer->pquant = tmp;
    if( storage->quantizer == 2 )
      p_pic_layer->uniform_quantizer = HANTRO_FALSE;
    else if( storage->quantizer == 3 )
      p_pic_layer->uniform_quantizer = HANTRO_TRUE;
  }
  /* HALFQP */
  if (tmp <= 8)
    p_pic_layer->half_qp = vc1hwdGetBits(p_strm_data, 1);
  else
    p_pic_layer->half_qp = 0;

  /* PQUANTIZER */
  if (storage->quantizer == 1) {
    p_pic_layer->uniform_quantizer = vc1hwdGetBits(p_strm_data, 1);
  }

}

/*------------------------------------------------------------------------------

    Function name: DecodeInterInformation

        Functional description:
            Decodes some Inter frame specific syntax elements from stream

        Inputs:
            p_pic_layer       Pointer to picture layer structure
            p_strm_data       Pointer to stream data
            vs_transform     value of vs_transform
            dquant          is dquant present

        Outputs:

        Returns:
            None
------------------------------------------------------------------------------*/
void DecodeInterInformation( pictureLayer_t* p_pic_layer,
                             strmData_t* p_strm_data,
                             u32 vs_transform,
                             u16x dquant ) {

  /* MVTAB */
  p_pic_layer->mv_table_index = vc1hwdGetBits(p_strm_data, 2);

  /* CBPTAB */
  p_pic_layer->cbp_table_index = vc1hwdGetBits(p_strm_data, 2);

  vc1hwdDecodeVopDquant(p_strm_data, dquant, p_pic_layer);

  /* Note: if DQ_PROFILE is ALL_MACROBLOCKS and DQBILEVEL is zero then set
   * HALFQP to zero as in that case no MQUANT is considered to be derived
   * from PQUANT. */
  if( p_pic_layer->dq_profile == DQPROFILE_ALL_MACROBLOCKS &&
      p_pic_layer->dqbi_level == 0 )
    p_pic_layer->half_qp = 0;

  /* TTMBF */
  p_pic_layer->mb_level_transform_type_flag = 1;
  p_pic_layer->tt_frm = TT_8x8;
  if ( vs_transform ) {
    p_pic_layer->mb_level_transform_type_flag = vc1hwdGetBits(p_strm_data, 1);
    if( p_pic_layer->mb_level_transform_type_flag == 1 ) {
      /* TTFRM */
      p_pic_layer->tt_frm = (transformType_e)vc1hwdGetBits(p_strm_data, 2);
    }
  }
}
/*------------------------------------------------------------------------------

    Function name: DecodeIntensityCompensationInfo
    Functional description:
            This function generages intensity compensation tables needed
            in motion compensation.

        Inputs:
            p_strm_data   pointer to stream data

        Outputs:
            i_shift      pointer to pPiclayer->i_shift
            i_scale      pointer to pPiclayer->i_scale

        Returns:
            none

------------------------------------------------------------------------------*/
void DecodeIntensityCompensationInfo( strmData_t * const p_strm_data,
                                      i32 *i_shift, i32 *i_scale ) {
  i16x lum_scale, lum_shift;

  /* LUMSCALE */
  lum_scale = vc1hwdGetBits(p_strm_data, 6);

  /* LUMSHIFT */
  lum_shift = vc1hwdGetBits(p_strm_data, 6);

  /* calculate scale and shift */
  if (lum_scale == 0) {
    *i_scale = -64;
    *i_shift = (255*64) - (lum_shift*2*64);
    if (lum_shift > 31)
      *i_shift += 128*64;
  } else {
    *i_scale = lum_scale+32;
    if (lum_shift > 31)
      *i_shift = (lum_shift*64) - (64*64);
    else
      *i_shift = lum_shift*64;
  }

}



/*------------------------------------------------------------------------------

    Function name: NewFrameDimensions

        Functional description:
            Calculate new frame dimensions after a RESPIC element is read
            from bitstream.

        Inputs:
            storage    pointer to stream storage
            res         new resolution

        Outputs:
            storage    picture size and macroblock amount constants
                        reinitialized

        Returns:
            none

------------------------------------------------------------------------------*/
void NewFrameDimensions( swStrmStorage_t *storage, resPic_e res ) {

  /* Variables */

  i16x x, y;

  /* Code */

  ASSERT(storage);

  x = storage->max_coded_width;
  y = storage->max_coded_height;

  if( res == RESPIC_HALF_FULL || res == RESPIC_HALF_HALF ) {
    x /= 2;
  }

  if( res == RESPIC_FULL_HALF || res == RESPIC_HALF_HALF ) {
    y /= 2;
  }

  storage->pic_width_in_mbs     = ( x + 15 ) >> 4;
  storage->pic_height_in_mbs    = ( y + 15 ) >> 4;
  storage->num_of_mbs          = (storage->pic_width_in_mbs *
                                  storage->pic_height_in_mbs);

  /* init coded image size to max coded image size */
  storage->cur_coded_width = x;
  storage->cur_coded_height = y;
}




/*------------------------------------------------------------------------------

    Function name: ProcessBitplanes

        Functional description:

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/
void ProcessBitplanes( swStrmStorage_t* storage, pictureLayer_t * p_pic_layer ) {

  /* Variables */

  u8 * p_tmp;
  u32 mbs;
  u32 num_skipped = 0;
  static const u32 raw_masks_main_prof[4] = {
    0L,
    MB_4MV | MB_SKIPPED,
    MB_DIRECT | MB_SKIPPED,
    0L
  };
  static const u32 raw_masks_adv_prof[3][4] = {
    /* Progressive */
    {
      MB_AC_PRED | MB_OVERLAP_SMOOTH,     /* I */
      MB_4MV | MB_SKIPPED,                /* P */
      MB_DIRECT | MB_SKIPPED,             /* B */
      MB_AC_PRED | MB_OVERLAP_SMOOTH
    },   /* BI */
    /* Frame interlaced */
    {
      MB_AC_PRED | MB_OVERLAP_SMOOTH | MB_FIELD_TX,   /* I */
      MB_SKIPPED,                                     /* P */
      MB_DIRECT | MB_SKIPPED,                         /* B */
      MB_AC_PRED | MB_OVERLAP_SMOOTH | MB_FIELD_TX
    },  /* BI */
    /* Field interlaced */
    {
      MB_AC_PRED | MB_OVERLAP_SMOOTH,     /* I */
      0,                                  /* P */
      MB_FORWARD_MB,                      /* B */
      MB_AC_PRED | MB_OVERLAP_SMOOTH
    },   /* BI */
  };

  /* Code */

  /* Set "unused" bitplanes to raw mode */
  if( storage->profile == VC1_ADVANCED )
    p_pic_layer->raw_mask |=
      ( (~raw_masks_adv_prof[ p_pic_layer->fcm ][ p_pic_layer->pic_type ]) & 7 );
  else
    p_pic_layer->raw_mask |=
      ( (~raw_masks_main_prof[ p_pic_layer->pic_type ]) & 7 );

  /* Now check if all MBs are skipped */
  mbs = storage->num_of_mbs;
  p_tmp = storage->p_mb_flags;

  if( p_pic_layer->fcm != FIELD_INTERLACE &&
      (p_pic_layer->pic_type == PTYPE_B || p_pic_layer->pic_type == PTYPE_P ) &&
      (p_pic_layer->raw_mask & MB_SKIPPED) == 0 ) {
    while(mbs--) {
      if ( (*p_tmp++) & MB_SKIPPED )
        num_skipped++;
    }
  }

  if( num_skipped == storage->num_of_mbs ) {
    /* Todo change picture type */
  }

}
