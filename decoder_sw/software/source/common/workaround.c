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

#include "workaround.h"
#include "dwl.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

#define MAGIC_WORD_LENGTH   (8)
#define MB_OFFSET           (4)

static const u8 magic_word[MAGIC_WORD_LENGTH] = "Rosebud\0";


/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

static u32 GetMbOffset( u32 mb_num, u32 vop_width );

/*------------------------------------------------------------------------------

   5.1  Function name: GetMbOffset

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
u32 GetMbOffset( u32 mb_num, u32 vop_width) {
  u32 mb_row, mb_col;
  u32 offset;

  mb_row = mb_num / vop_width;
  mb_col = mb_num % vop_width;
  offset = mb_row*16*16*vop_width + mb_col*16;

  return offset;
}

/*------------------------------------------------------------------------------

   5.1  Function name: PrepareStuffingWorkaround

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
void StuffMacroblock( u32 mb_num, u8 * p_dec_out, u8 *p_ref_pic, u32 vop_width,
                      u32 vop_height ) {

  u32 pix_width;
  u32 mb_row, mb_col;
  u32 offset;
  u32 luma_size;
  u8 *p_src;
  u8 *p_dst;
  u32 x, y;

  pix_width = 16*vop_width;

  mb_row = mb_num / vop_width;
  mb_col = mb_num % vop_width;

  offset = mb_row*16*pix_width + mb_col*16;
  luma_size = 256*vop_width*vop_height;

  if(p_ref_pic) {

    p_dst = p_dec_out;
    p_src = p_ref_pic;

    p_dst += offset;
    p_src += offset;
    /* Copy luma data */
    for( y = 0 ; y < 16 ; ++y ) {
      for( x = 0 ; x < 16 ; ++x ) {
        p_dst[x] = p_src[x];
      }
      p_dst += pix_width;
      p_src += pix_width;
    }

    /* Chroma data */
    offset = mb_row*8*pix_width + mb_col*16;

    p_dst = p_dec_out;
    p_src = p_ref_pic;

    p_dst += luma_size;
    p_src += luma_size;
    p_dst += offset;
    p_src += offset;

    for( y = 0 ; y < 8 ; ++y ) {
      for( x = 0 ; x < 16 ; ++x ) {
        p_dst[x] = p_src[x];
      }
      p_dst += pix_width;
      p_src += pix_width;
    }
  } else {
    p_dst = p_dec_out + offset;
    /* Copy luma data */
    for( y = 0 ; y < 16 ; ++y ) {
      for( x = 0 ; x < 16 ; ++x ) {
        i32 tmp;
        if( mb_col )
          tmp = p_dst[x-pix_width] + p_dst[x-1] - p_dst[x-pix_width-1];
        else
          tmp = p_dst[x-pix_width];
        if( tmp < 0 )           tmp = 0;
        else if ( tmp > 255 )   tmp = 255;
        p_dst[x] = tmp;
      }
      p_dst += pix_width;
    }

    /* Chroma data */
    offset = mb_row*8*pix_width + mb_col*16;

    p_dst = p_dec_out + luma_size + offset;

    for( y = 0 ; y < 8 ; ++y ) {
      for( x = 0 ; x < 16 ; ++x ) {
        i32 tmp;
        if( mb_col )
          tmp = p_dst[x-pix_width] + p_dst[x-2] - p_dst[x-pix_width-2];
        else
          tmp = p_dst[x-pix_width];
        if( tmp < 0 )           tmp = 0;
        else if ( tmp > 255 )   tmp = 255;
        p_dst[x] = tmp;
      }
      p_dst += pix_width;
    }
  }
}

/*------------------------------------------------------------------------------

   5.1  Function name: PrepareStuffingWorkaround

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
void PrepareStuffingWorkaround( u8 *p_dec_out, u32 vop_width, u32 vop_height ) {

  u32 i;
  u8 * p_base;

  p_base = p_dec_out + GetMbOffset(vop_width*vop_height - (MB_OFFSET + vop_width * vop_height / 6), vop_width);

  for( i = 0 ; i < MAGIC_WORD_LENGTH ; ++i )
    p_base[i] = magic_word[i];

}

/*------------------------------------------------------------------------------

   5.1  Function name: ProcessStuffingWorkaround

        Purpose: Check bytes written in PrepareStuffingWorkaround(). If bytes
                 match magic word, then error happened earlier on in the picture.
                 If bytes mismatch, then HW got to end of picture and error
                 interrupt is most likely because of faulty stuffing. In this
                 case we either conceal tail end of the frame or copy it from
                 previous frame.

        Input:

        Output:
            HANTRO_TRUE
            HANTRO_FALSE

------------------------------------------------------------------------------*/
u32  ProcessStuffingWorkaround( u8 * p_dec_out, u8 * p_ref_pic, u32 vop_width,
                                u32 vop_height ) {

  u32 i;
  u8 * p_base;
  u32 num_mbs;
  u32 match = HANTRO_TRUE;

  num_mbs = vop_width*vop_height;

  p_base = p_dec_out + GetMbOffset(num_mbs - (MB_OFFSET + vop_width * vop_height / 6), vop_width);

  for( i = 0 ; i < MAGIC_WORD_LENGTH && match ; ++i )
    if( p_base[i] != magic_word[i] )
      match = HANTRO_FALSE;

#if 0
  /* If 4th last macroblock is overwritten, then assume it's a stuffing
   * error. Copy remaining three macroblocks from previous ref frame. */
  if( !match ) {
    for ( i = 1+num_mbs - MB_OFFSET ; i < num_mbs ; ++i ) {
      StuffMacroblock( i, p_dec_out, p_ref_pic, vop_width, vop_height );
    }
  }
#endif
  (void) p_ref_pic;

  return match ? HANTRO_FALSE : HANTRO_TRUE;

}

/*------------------------------------------------------------------------------

   5.1  Function name: ProcessStuffingWorkaround

        Purpose:

        Input:

        Output:

------------------------------------------------------------------------------*/
void InitWorkarounds(u32 dec_mode, workaround_t *p_workarounds) {
  u32 asic_id = DWLReadAsicID(dec_mode+1);
  u32 asic_ver = asic_id >> 16;
  u32 asic_build = asic_id & 0xFFFF;

  /* set all workarounds off by default */
  p_workarounds->mpeg.stuffing = HANTRO_FALSE;
  p_workarounds->mpeg.start_code = HANTRO_FALSE;
  p_workarounds->rv.multibuffer = HANTRO_FALSE;

  /* exception; set h264 frame_num workaround on by default */
  if (dec_mode == 0)
    p_workarounds->h264.frame_num = HANTRO_TRUE;

  /* 8170 decoder does not support bad stuffing bytes. */
  if( asic_ver == 0x8170U) {
    if( dec_mode == 5 || dec_mode == 6 || dec_mode == 1)
      p_workarounds->mpeg.stuffing = HANTRO_TRUE;
    else if ( dec_mode == 8)
      p_workarounds->rv.multibuffer = HANTRO_TRUE;
  } else if( asic_ver == 0x8190U ) {
    switch(dec_mode) {
    case 1: /* MPEG4 */
      if( asic_build < 0x2570 )
        p_workarounds->mpeg.stuffing = HANTRO_TRUE;
      break;
    case 2: /* H263 */
      /* No HW tag supports this */
      p_workarounds->mpeg.stuffing = HANTRO_TRUE;
      break;
    case 5: /* MPEG2 */
    case 6: /* MPEG1 */
      if( asic_build < 0x2470 )
        p_workarounds->mpeg.stuffing = HANTRO_TRUE;
      break;
    case 8: /* RV */
      p_workarounds->rv.multibuffer = HANTRO_TRUE;
    }
  } else if(asic_ver == 0x9170U) {
    if( dec_mode == 8 && asic_build < 0x1270 )
      p_workarounds->rv.multibuffer = HANTRO_TRUE;
  } else if(asic_ver == 0x9190U) {
    if( dec_mode == 8 && asic_build < 0x1460 )
      p_workarounds->rv.multibuffer = HANTRO_TRUE;
  } else if(asic_ver == 0x6731U) { /* G1 */
    if( dec_mode == 8 && asic_build < 0x1070 )
      p_workarounds->rv.multibuffer = HANTRO_TRUE;
    if (dec_mode == 0 && asic_build >= 0x2390)
      p_workarounds->h264.frame_num = HANTRO_FALSE;
#if 0
    p_workarounds->mpeg.stuffing = HANTRO_TRUE;
#endif
  }


  if (dec_mode == 5 /*MPEG2*/ )
    p_workarounds->mpeg.start_code = HANTRO_TRUE;

}

/*------------------------------------------------------------------------------

   5.1  Function name: PrepareStartCodeWorkaround

        Purpose: Prepare for start code workaround checking; write magic word
            to last 8 bytes of the picture (frame or field)

        Input:

        Output:

------------------------------------------------------------------------------*/
void PrepareStartCodeWorkaround( u8 *p_dec_out, u32 vop_width, u32 vop_height,
                                 u32 top_field, u32 dpb_mode) {

  u32 i;
  u8 * p_base;

  p_base = p_dec_out + vop_width*vop_height*256 - 8;
  if (top_field) {
    if(dpb_mode == DEC_DPB_FRAME )
      p_base -= 16*vop_width;
    else if (dpb_mode == DEC_DPB_INTERLACED_FIELD )
      p_base -= 128*vop_width*vop_height;
  }

  for( i = 0 ; i < MAGIC_WORD_LENGTH ; ++i )
    p_base[i] = magic_word[i];

}

/*------------------------------------------------------------------------------

   5.1  Function name: ProcessStartCodeWorkaround

        Purpose: Check bytes written in PrepareStartCodeWorkaround(). If bytes
                 match magic word, then error happened earlier on in the picture.
                 If bytes mismatch, then HW got to end of picture and timeout
                 interrupt is most likely because of corrupted startcode. In
                 this case we just ignore timeout.

                 Note: in addition to ignoring timeout, SW needs to find
                 next start code as HW does not update stream end pointer
                 properly. Methods of searching next startcode are mode
                 specific and cannot be done here.

        Input:

        Output:
            HANTRO_TRUE
            HANTRO_FALSE

------------------------------------------------------------------------------*/
u32  ProcessStartCodeWorkaround( u8 * p_dec_out, u32 vop_width, u32 vop_height,
                                 u32 top_field, u32 dpb_mode) {

  u32 i;
  u8 * p_base;
  u32 match = HANTRO_TRUE;

  p_base = p_dec_out + vop_width*vop_height*256 - 8;
  if (top_field) {
    if(dpb_mode == DEC_DPB_FRAME )
      p_base -= 16*vop_width;
    else if (dpb_mode == DEC_DPB_INTERLACED_FIELD )
      p_base -= 128*vop_width*vop_height;
  }

  for( i = 0 ; i < MAGIC_WORD_LENGTH && match ; ++i )
    if( p_base[i] != magic_word[i] )
      match = HANTRO_FALSE;

  return match ? HANTRO_FALSE : HANTRO_TRUE;

}
