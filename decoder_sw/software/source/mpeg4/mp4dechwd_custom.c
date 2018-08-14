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

#include "mp4dechwd_strmdec.h"
#include "mp4dechwd_utils.h"
#include "mp4dechwd_headers.h"
#include "mp4dechwd_generic.h"
#include "mp4debug.h"
#include "mp4decdrv.h"
#include "mp4decapi_internal.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

static u32 StrmDec_DecodeSusi3Header(DecContainer * dec_container);
static u32 DecodeC2(DecContainer * dec_container);


u32 DecodeC2(DecContainer * dec_container) {
  u32 tmp;
  tmp = StrmDec_GetBits( dec_container, 1 );
  if( tmp == END_OF_STREAM )
    return END_OF_STREAM;
  if( tmp ) {
    tmp = StrmDec_GetBits( dec_container, 1 );
    if( tmp == END_OF_STREAM )
      return END_OF_STREAM;
    return 1+tmp;
  }
  return tmp;
}


/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_DecodeSusi3Header

        Purpose: initialize stream decoding related parts of DecContainer

        Input:
            Pointer to DecContainer structure
                -initializes StrmStorage

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/
u32 StrmDec_DecodeCustomHeaders(DecContainer * dec_container) {
  u32 tmp;

  tmp = StrmDec_DecodeSusi3Header( dec_container );
  if( tmp != HANTRO_OK )
    return DEC_VOP_HDR_RDY_ERROR;

  return DEC_VOP_HDR_RDY;
}

/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_DecodeSusi3Header

        Purpose: initialize stream decoding related parts of DecContainer

        Input:
            Pointer to DecContainer structure
                -initializes StrmStorage

        Output:
            HANTRO_OK/NOK

------------------------------------------------------------------------------*/
u32 StrmDec_DecodeSusi3Header(DecContainer * dec_container) {
  u32 tmp;
  u32 pic_type;
  u32 slice_code = 0;
  u32 rlc_table_y, rlc_table_c;
  u32 dc_table;
  u32 mv_table = 0;
  u32 skip_mb_code = 0;

  dec_container->StrmStorage.valid_vop_header = HANTRO_FALSE;
  tmp = StrmDec_ShowBits(dec_container, 32);

  /* picture_type */
  pic_type = StrmDec_GetBits(dec_container, 2);
  if( pic_type > 1 ) {
    return HANTRO_NOK;
  }
  dec_container->VopDesc.vop_coding_type = ( pic_type ? PVOP : IVOP );

  /* quant_scale */
  dec_container->VopDesc.q_p = StrmDec_GetBits(dec_container, 5);

  if( pic_type == 0 ) { /* Intra picture */
    slice_code = StrmDec_GetBits(dec_container, 5);
    if( slice_code < 0x17 )
      return HANTRO_NOK;
    slice_code -= 0x16;
    rlc_table_c = DecodeC2( dec_container );
    rlc_table_y = DecodeC2( dec_container );
    tmp = dc_table = StrmDec_GetBits(dec_container, 1);
    dec_container->VopDesc.vop_rounding_type = 1;
  } else { /* P picture */
    skip_mb_code = StrmDec_GetBits(dec_container, 1);
    rlc_table_c = rlc_table_y =
                    DecodeC2( dec_container );
    dc_table = StrmDec_GetBits(dec_container, 1);
    tmp = mv_table = StrmDec_GetBits(dec_container, 1);
    if( dec_container->Hdrs.flip_flop_rounding )
      dec_container->VopDesc.vop_rounding_type = 1 -
          dec_container->VopDesc.vop_rounding_type;
    else {
      /* should it be 1 or 0 */
    }
  }

  if( tmp == END_OF_STREAM )
    return END_OF_STREAM;

  if( pic_type == 0 ) {
    dec_container->Hdrs.num_rows_in_slice =
      dec_container->VopDesc.vop_height / slice_code;
    if( dec_container->Hdrs.num_rows_in_slice == 0) {
      return (HANTRO_NOK);
    }
  }
  dec_container->Hdrs.rlc_table_y = rlc_table_y;
  dec_container->Hdrs.rlc_table_c = rlc_table_c;
  dec_container->Hdrs.dc_table = dc_table;
  dec_container->Hdrs.mv_table = mv_table;
  dec_container->Hdrs.skip_mb_code = skip_mb_code;

  dec_container->StrmStorage.vp_mb_number = 0;
  dec_container->StrmStorage.vp_first_coded_mb = 0;

  /* successful decoding -> set valid vop header */
  dec_container->StrmStorage.valid_vop_header = HANTRO_TRUE;

  return (HANTRO_OK);
}

/*------------------------------------------------------------------------------

   Function name:
                ProcessUserData

        Purpose:

        Input:
                pointer to DecContainer

        Output:
                status (HANTRO_OK/NOK/END_OF_STREAM)

------------------------------------------------------------------------------*/

void ProcessUserData(DecContainer * dec_container) {

  u32 i, tmp;
  u32 bytes = 0;
  u32 SusI = 1147762264;
  u32 uild = ('u' << 24) | ('i' << 16) | ('l' << 8) | 'd';
  /* looking for string "SusINNBuildMMp" or "SusINNbMMp", where NN and MM are
   * version and build (any number of characters) */
  tmp = StrmDec_ShowBits(dec_container, 32);
  if(tmp == SusI) {
    bytes += 4;
    /* version */
    for (i = 0;; i++) {
      tmp = StrmDec_ShowBitsAligned(dec_container, 8, bytes);
      if (tmp < '0' || tmp > '9')
        break;
      /* Check 1st character of the version */
      if( i == 0 ) {
        if( tmp <= '4' )
          dec_container->StrmStorage.custom_strm_ver = CUSTOM_STRM_1;
        else
          dec_container->StrmStorage.custom_strm_ver = CUSTOM_STRM_2;
      }
      bytes++;
    }
    if (i == 0)
      return;

    /* "b" or "Build" */
    if (tmp == 'b')
      bytes++;
    else if (tmp == 'B' &&
             StrmDec_ShowBitsAligned(dec_container, 8, bytes) == uild)
      bytes += 5;
    else
      return;

    /* build */
    for (i = 0;; i++) {
      tmp = StrmDec_ShowBitsAligned(dec_container, 8, bytes);
      if (tmp < '0' || tmp > '9')
        break;
      /*build = 10*build + (tmp - '0');*/
      bytes++;
    }

    if (i == 0)
      return;

    if (tmp == 'p')
      dec_container->packed_mode = 1;
  }

}


/*------------------------------------------------------------------------------

    Function: SetConformanceFlags

        Functional description:
            Set some flags to get best conformance to different Susi versions.

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

void SetConformanceFlags( DecContainer * dec_cont ) {

  /* Variables */

  DWLHwConfig hw_cfg;

  /* Code */

  DWLReadAsicConfig(&hw_cfg,DWL_CLIENT_TYPE_MPEG4_DEC);

  /* CUSTOM_STRM_0 needs to have HW support! */
  if(hw_cfg.custom_mpeg4_support == MPEG4_CUSTOM_NOT_SUPPORTED &&
      dec_cont->StrmStorage.custom_strm_ver == CUSTOM_STRM_0 ) {
    dec_cont->StrmStorage.unsupported_features_present =
      HANTRO_TRUE;
    return;
  }

  if(hw_cfg.custom_mpeg4_support == MPEG4_CUSTOM_NOT_SUPPORTED ||
      dec_cont->StrmStorage.custom_strm_ver == 0 ) {
    if (dec_cont->StrmStorage.custom_strm_ver)
      dec_cont->StrmStorage.unsupported_features_present =
        HANTRO_TRUE;
    dec_cont->StrmStorage.custom_idct        = 0;
    dec_cont->StrmStorage.custom_overfill    = 0;
    dec_cont->StrmStorage.custom_strm_headers = 0;
    return;
  }

  switch( dec_cont->StrmStorage.custom_strm_ver ) {
  case CUSTOM_STRM_0:
    dec_cont->StrmStorage.custom_idct        = 0;
    dec_cont->StrmStorage.custom_overfill    = 0;
    dec_cont->StrmStorage.custom_strm_headers = 1;
    break;
  case CUSTOM_STRM_1:
    dec_cont->StrmStorage.custom_idct        = 1;
    dec_cont->StrmStorage.custom_overfill    = 1;
    dec_cont->StrmStorage.custom_strm_headers = 0;
    break;
  default: /* CUSTOM_STRM_2 and higher */
    dec_cont->StrmStorage.custom_idct        = 1;
    dec_cont->StrmStorage.custom_overfill    = 0;
    dec_cont->StrmStorage.custom_strm_headers = 0;
    break;
  }

}



/*------------------------------------------------------------------------------

    Function: ProcessHwOutput

        Functional description:
            Read flip-flop rounding type for Susi3

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

void ProcessHwOutput( DecContainer * dec_cont ) {

  if(dec_cont->StrmStorage.custom_strm_ver == CUSTOM_STRM_0 &&
      dec_cont->VopDesc.vop_coding_type == IVOP) {
    /* use picture information flag to get rounding type */
    dec_cont->Hdrs.flip_flop_rounding =
      GetDecRegister(dec_cont->mp4_regs, HWIF_DEC_PIC_INF);
    SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_PIC_INF, 0);
  }
}

/*------------------------------------------------------------------------------

    Function: CustomInit

        Functional description:

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

void SetCustomInfo(DecContainer * dec_cont, u32 width, u32 height ) {

  u32 mb_width, mb_height;
  u32 ret;

  mb_width = (width+15)>>4;
  mb_height = (height+15)>>4;

  dec_cont->VopDesc.total_mb_in_vop = mb_width * mb_height;
  dec_cont->VopDesc.vop_width = mb_width;
  dec_cont->VopDesc.vop_height = mb_height;

  dec_cont->Hdrs.low_delay = HANTRO_TRUE;
  dec_cont->StrmStorage.custom_strm_ver = CUSTOM_STRM_0;

  dec_cont->StrmStorage.resync_marker_length = 17;
  dec_cont->StrmStorage.short_video = HANTRO_FALSE;

  dec_cont->VopDesc.vop_rounding_type = 0;
  dec_cont->VopDesc.fcode_fwd = 1;
  dec_cont->VopDesc.intra_dc_vlc_thr = 0;
  dec_cont->VopDesc.vop_coded = 1;

  dec_cont->Hdrs.video_object_layer_width = width;
  dec_cont->Hdrs.video_object_layer_height = height;

  dec_cont->Hdrs.vop_time_increment_resolution = 30000;
  dec_cont->Hdrs.data_partitioned = HANTRO_FALSE;
  dec_cont->Hdrs.resync_marker_disable = HANTRO_TRUE;

  dec_cont->Hdrs.colour_primaries = 1;
  dec_cont->Hdrs.transfer_characteristics = 1;
  dec_cont->Hdrs.matrix_coefficients = 6;

  dec_cont->StrmStorage.video_object_layer_width = width;
  dec_cont->StrmStorage.video_object_layer_height = height;

  SetDecRegister(dec_cont->mp4_regs, HWIF_PIC_MB_WIDTH,
                 dec_cont->VopDesc.vop_width);
  SetDecRegister(dec_cont->mp4_regs, HWIF_PIC_MB_HEIGHT_P,
                 dec_cont->VopDesc.vop_height);
  SetDecRegister(dec_cont->mp4_regs, HWIF_PIC_MB_H_EXT,
                 dec_cont->VopDesc.vop_height >> 8);
  SetDecRegister(dec_cont->mp4_regs, HWIF_DEC_MODE,
                 MP4_DEC_X170_MODE_MPEG4);
  /* Note: Version 3 uses strictly MB boundary overfill */
  SetDecRegister(dec_cont->mp4_regs, HWIF_MB_WIDTH_OFF, 0);
  SetDecRegister(dec_cont->mp4_regs, HWIF_MB_HEIGHT_OFF, 0);

  SetConformanceFlags( dec_cont );

  RefbuInit( &dec_cont->ref_buffer_ctrl, MP4_DEC_X170_MODE_MPEG4,
             dec_cont->VopDesc.vop_width,
             dec_cont->VopDesc.vop_height,
             dec_cont->ref_buf_support );
  (void)ret;
}


/*------------------------------------------------------------------------------

    Function: Susi3Init

        Functional description:

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/

void SetConformanceRegs( DecContainer * dec_container ) {
  /* Set correct transform */
  SetDecRegister(dec_container->mp4_regs, HWIF_DIVX_IDCT_E,
                 dec_container->StrmStorage.custom_idct);

  if(dec_container->StrmStorage.custom_strm_ver == CUSTOM_STRM_0) {
    SetDecRegister(dec_container->mp4_regs, HWIF_DIVX3_E, 1 );
    SetDecRegister(dec_container->mp4_regs, HWIF_DIVX3_SLICE_SIZE,
                   dec_container->Hdrs.num_rows_in_slice);

    SetDecRegister(dec_container->mp4_regs, HWIF_TRANSDCTAB,
                   dec_container->Hdrs.dc_table);
    SetDecRegister(dec_container->mp4_regs, HWIF_TRANSACFRM,
                   dec_container->Hdrs.rlc_table_c);
    SetDecRegister(dec_container->mp4_regs, HWIF_TRANSACFRM2,
                   dec_container->Hdrs.rlc_table_y);

    if( dec_container->VopDesc.vop_coding_type == IVOP ) {
      SetDecRegister(dec_container->mp4_regs, HWIF_MVTAB, 0 );
      SetDecRegister(dec_container->mp4_regs, HWIF_SKIP_MODE, 0 );
    } else {
      SetDecRegister(dec_container->mp4_regs, HWIF_MVTAB,
                     dec_container->Hdrs.mv_table);
      SetDecRegister(dec_container->mp4_regs, HWIF_SKIP_MODE,
                     dec_container->Hdrs.skip_mb_code);
    }

    SetDecRegister(dec_container->mp4_regs, HWIF_TYPE1_QUANT_E, 0 );
    SetDecRegister(dec_container->mp4_regs, HWIF_MV_ACCURACY_FWD, 0);
  } else {
    SetDecRegister(dec_container->mp4_regs, HWIF_DIVX3_E, 0 );
  }

}


/*------------------------------------------------------------------------------

    Function: SetStrmFmt

        Functional description:

        Inputs:
            none

        Outputs:
            none

        Returns:
            API version

------------------------------------------------------------------------------*/
void SetStrmFmt( DecContainer * dec_cont, u32 strm_fmt ) {
  dec_cont->StrmStorage.sorenson_spark = ( strm_fmt == MP4DEC_SORENSON );
  dec_cont->StrmStorage.custom_strm_ver = ( strm_fmt == MP4DEC_CUSTOM_1 ) ? CUSTOM_STRM_1 : 0;
}
