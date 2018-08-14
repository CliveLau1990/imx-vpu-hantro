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

#ifndef VC1HWD_PICTURE_LAYER_H
#define VC1HWD_PICTURE_LAYER_H

/*------------------------------------------------------------------------------
    Include headers
------------------------------------------------------------------------------*/

#include "vc1hwd_util.h"
#include "vc1hwd_stream.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/* Picture type */
typedef enum {
  PTYPE_I = 0,
  PTYPE_P = 1,
  PTYPE_B = 2,
  PTYPE_BI = 3,
  PTYPE_Skip
} picType_e;

/* Field type */
typedef enum {
  FTYPE_I = 0,
  FTYPE_P = 1,
  FTYPE_B = 2,
  FTYPE_BI = 3
} fieldType_e;

/* RESPIC syntax element (Main Profile) */
typedef enum {
  RESPIC_FULL_FULL = 0,
  RESPIC_HALF_FULL = 1,
  RESPIC_FULL_HALF = 2,
  RESPIC_HALF_HALF = 3
} resPic_e;

/* DQPROFILE syntax element */
typedef enum {
  DQPROFILE_N_A,
  DQPROFILE_ALL_FOUR,
  DQPROFILE_DOUBLE_EDGES,
  DQPROFILE_SINGLE_EDGES,
  DQPROFILE_ALL_MACROBLOCKS
} dqProfile_e;

/* DQSBEDGE and DQDBEDGE */
typedef enum {
  DQEDGE_LEFT     = 1,
  DQEDGE_TOP      = 2,
  DQEDGE_RIGHT    = 4,
  DQEDGE_BOTTOM   = 8
} dqEdge_e;

/* BFRACTION syntax element */
typedef enum {
  BFRACT_1_2,
  BFRACT_1_3,
  BFRACT_2_3,
  BFRACT_1_4,
  BFRACT_3_4,
  BFRACT_1_5,
  BFRACT_2_5,
  BFRACT_3_5,
  BFRACT_4_5,
  BFRACT_1_6,
  BFRACT_5_6,
  BFRACT_1_7,
  BFRACT_2_7,
  BFRACT_3_7,
  BFRACT_4_7,
  BFRACT_5_7,
  BFRACT_6_7,
  BFRACT_1_8,
  BFRACT_3_8,
  BFRACT_5_8,
  BFRACT_7_8,
  BFRACT_SMPTE_RESERVED,
  BFRACT_PTYPE_BI
} bfract_e;

/* Supported transform types */
typedef enum {
  TT_8x8 = 0,
  TT_8x4 = 1,
  TT_4x8 = 2,
  TT_4x4 = 3
} transformType_e;

/* MVMODE syntax element */
typedef enum {
  MVMODE_1MV_HALFPEL_LINEAR,
  MVMODE_1MV,
  MVMODE_1MV_HALFPEL,
  MVMODE_MIXEDMV,
  MVMODE_INVALID
} mvmode_e;

/* BMVTYPE syntax element */
typedef enum {
  BMV_BACKWARD,
  BMV_FORWARD,
  BMV_INTERPOLATED,
  BMV_DIRECT, /* used in mv prediction function */
  BMV_P_PICTURE
} bmvType_e;

/* FCM syntax element */
typedef enum {
  PROGRESSIVE = 0,
  FRAME_INTERLACE = 1,
  FIELD_INTERLACE = 2
} fcm_e;

/* Pan scan info */
typedef struct {
  u32 h_offset;
  u32 v_offset;
  u32 width;
  u32 height;
} psw_t;

/* FPTYPE */
typedef enum {
  FP_I_I = 0,
  FP_I_P = 1,
  FP_P_I = 2,
  FP_P_P = 3,
  FP_B_B = 4,
  FP_B_BI = 5,
  FP_BI_B = 6,
  FP_BI_BI = 7
} fpType_e;

/* INTCOMPFIELD */
typedef enum {
  IC_BOTH_FIELDS = 0,
  IC_TOP_FIELD = 1,
  IC_BOTTOM_FIELD = 2,
  IC_NONE
} intCompField_e;

/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/

/* Descriptor for the picture layer data */
typedef struct pictureLayer {
  picType_e pic_type;          /* Picture type */

  u16x pq_index;
  u16x pquant;                /* Picture level quantization parameters */
  u16x half_qp;
  u32 uniform_quantizer;

  resPic_e    res_pic;         /* Progressive picture resolution code [0,3] */
  u16x buffer_fullness;

  u32 interp_frm;
  u16x frame_count;

  mvmode_e mvmode;            /* Motion vector coding mode for frame */
  mvmode_e mvmode2;

  u16x raw_mask;               /* Raw mode mask; specifies which bitplanes are
                                 * coded with raw coding mode as well as invert
                                 * element status. */

  u16x mv_table_index;
  u16x cbp_table_index;
  u16x ac_coding_set_index_y;
  u16x ac_coding_set_index_cb_cr;
  u16x intra_transform_dc_index;

  u32 mb_level_transform_type_flag;  /* Transform type signaled in MB level */

  transformType_e tt_frm;      /* Frame level transform type */

  /* Main profile stuff */
  u32 intensity_compensation;
  u16x mv_range;               /* Motion vector range [0,3] */
  u16x dquant_in_frame;         /* Implied from decoding VOPDQUANT */
  bfract_e bfraction;         /* Temporal placement for B pictures */
  i16x scale_factor;

  u16x alt_pquant;
  dqProfile_e dq_profile;
  dqEdge_e dq_edges;           /* Edges to be quantized with ALTPQUANT
                                 * instead of PQUANT */

  u16x dqbi_level;             /* Used only if dq_profile==All
                                 * 0  mb may take any quantization step
                                 * 1  only PQUANT and ALTPQUANT allowed */
  i32 i_shift;
  i32 i_scale;
  u32 range_red_frm;

  u32 top_field;   /* 1 TOP, 0 BOTTOM */
  u32 is_ff;       /* is first field */

  /* advance profile stuff */
  fcm_e fcm;      /* frame coding mode */
  u32 tfcntr;     /* Temporal reference frame count */
  u32 tff;        /* Top field first TFF=1 -> top, TFF=0 -> bottom */
  u32 rff;        /* Repeat first field */
  u32 rptfrm;     /* Repeat Frame Count; Number of frames to repeat (0-3) */
  u32 ps_present;  /* Pan Scan Flag */
  psw_t psw;      /* Pan scan window information */

  u32 uv_samp;     /* subsampling of color-difference: progressive/interlace */
  u32 post_proc;   /* may be used in display process */
  u32 cond_over;   /* conditional overlap flag: 0b, 10b, 11b */

  u32 dmv_range;   /* extended differential mv range [0,3] */
  u32 mv_switch;   /* If 0, only 1mv for MB else 1,2 or 4 mvs for MB */
  u32 mb_mode_tab;  /* macroblock mode code table */
  u32 mvbp_table_index2;  /* 2-mv block pattern table */
  u32 mvbp_table_index4; /* 4-mv block pattern table */

  fpType_e field_pic_type; /* Field picture type */
  u32 ref_dist;    /* P reference distance */
  u32 num_ref;     /* 0 = one reference field, 1 = two reference fields */
  u32 ref_field;   /* 0 = closest, 1 = second most recent */
  intCompField_e int_comp_field; /* fields to intensity compensate */
  i32 i_shift2;    /* LUMSHIFT2 (shall be applied to bottom field) */
  i32 i_scale2;    /* LUMSCALE2 (shall be applied to bottom field) */

  u32 pic_header_bits;  /* number of bits in picture layer header */
  u32 field_header_bits; /* same for field header */

} pictureLayer_t;


/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

struct swStrmStorage;
u16x vc1hwdDecodePictureLayer( struct swStrmStorage *storage,
                               strmData_t *p_strm_data );

u16x vc1hwdDecodeFieldLayer( struct swStrmStorage *storage,
                             strmData_t *p_strm_data,
                             u16x is_first_field );

u16x vc1hwdDecodePictureLayerAP(struct swStrmStorage *storage,
                                strmData_t *p_strm_data );


#endif /* #ifndef VC1HWD_PICTURE_LAYER_H */

