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

#include "rv_rpr.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/
static void Up2x( u8 * inp, u8 *inp_chr, u32 inp_w, u32 inp_wfrm, u32 inp_h, u32 inp_hfrm,
                  u8 * outp, u8 *outp_chr, u32 outp_w, u32 outp_wfrm, u32 outp_h, u32 outp_hfrm,
                  u32 round );
static void Down2x( u8 * inp, u8 *inp_chr, u32 inp_w, u32 inp_wfrm, u32 inp_h, u32 inp_hfrm,
                    u8 * outp, u8 *outp_chr, u32 outp_w, u32 outp_wfrm, u32 outp_h, u32 outp_hfrm,
                    u32 round );
static void ResampleY( u32 inp_w, u32 outp_w, u32 outp_wfrm, u32 outp_h, u8 *p_dest_row,
                       u8 **p_src_row_buf, u16*src_col, u8*coeff_y, u8*coeff_x,
                       u32 left_edge, u32 use_right_edge, u32 right_edge, u32 rnd_val );
static void ResampleChr( u32 inp_w, u32 outp_w, u32 outp_wfrm, u32 outp_h, u8 *p_dest_row,
                         u8 **p_src_row_buf, u16*src_col, u8*coeff_y, u8*coeff_x,
                         u32 left_edge, u32 use_right_edge, u32 right_edge, u32 rnd_val );
static void CoeffTables( u32 inp_w, u32 inp_h, u32 outp_w, u32 outp_h,
                         u32 luma_coeffs, u8 *inp, u32 inp_wfrm,
                         u16 *p_src_col, u8 **p_src_row_buf,
                         u8 *p_coeff_x, u8 *p_coeff_y,
                         u32 *p_left_edge, u32 *p_use_right_edge, u32* p_right_edge);
static void RvResample( u8 * inp, u8 *inp_chr, u32 inp_w, u32 inp_wfrm, u32 inp_h,
                        u32 inp_hfrm, u8 * outp, u8 *outp_chr, u32 outp_w,
                        u32 outp_wfrm, u32 outp_h, u32 outp_hfrm,
                        u32 round, u8 *work_memory, u32 tiled_mode );

/*------------------------------------------------------------------------------

    Function name: ResampleY

        Functional description:
            Resample luma component using bilinear algorithm.

        Inputs:

        Outputs:
            p_dest_row        Resampled luma image

        Returns:
            HANTRO_OK       Decoding successful
            HANTRO_NOK      Error in decoding
            END_OF_STREAM   End of stream encountered

------------------------------------------------------------------------------*/
void ResampleY( u32 inp_w, u32 outp_w, u32 outp_wfrm, u32 outp_h, u8 *p_dest_row,
                u8 **p_src_row_buf, u16*src_col, u8*coeff_y, u8*coeff_x,
                u32 left_edge, u32 use_right_edge, u32 right_edge, u32 rnd_val ) {
  u32 col;
  u8 *p_src_row0;
  u8 *p_src_row1;
  u32 mid_pels;
  u32 right_pels;

  /* Calculate amount of pixels without overfill and with right edge
   * overfill */
  if(use_right_edge == 0) {
    mid_pels = outp_w - left_edge;
    right_pels = 0;
  } else {
    mid_pels = right_edge - left_edge;
    right_pels = outp_w - right_edge;
  }

  inp_w--; /* We don't need original inp_w for anything, but right edge overfill
             * requires inp_w-1 */
  while(outp_h--) {
    u32 coeff_y0, coeff_y1;
    u8 * p_dest = p_dest_row;
    u16 * p_src_col = src_col;
    u8 * p_coeff_xptr;
    coeff_y1 = *coeff_y++;
    coeff_y0 = 16-coeff_y1;
    p_src_row0 = *p_src_row_buf++;
    p_src_row1 = *p_src_row_buf++;

    /* First pel(s) -- there may be overfill required on left edge, this
     * part of code takes that into account */
    if(left_edge) {
      u32 A,C, pel;
      A = *p_src_row0;
      C = *p_src_row1;
      pel = (coeff_y0*16*A +
             coeff_y1*16*C +
             rnd_val) / 256;
      col = left_edge;
      while(col--)
        *p_dest++ = pel;
    }

    /* Middle pels on a row. By definition, no overfill is required */
    p_coeff_xptr = coeff_x;
    col = mid_pels;
    while(col--) {
      u32 c0;
      u32 A, B, C, D;
      u32 pel;
      u32 top, bot;
      u32 tmp;
      u32 coeff_x0, coeff_x1;

      coeff_x1 = *p_coeff_xptr++;
      coeff_x0 = 16-coeff_x1;
      c0 = *p_src_col++;

      /* Get input pixels */
      A = p_src_row0[c0];
      C = p_src_row1[c0];
      c0++;
      B = p_src_row0[c0];
      D = p_src_row1[c0];
      /* Weigh source rows */
      top = coeff_x0*A + coeff_x1*B;
      bot = coeff_x0*C + coeff_x1*D;
      /* Combine rows and round */
      tmp = coeff_y0*top + coeff_y1*bot;
      pel = ( tmp + rnd_val) / 256;
      *p_dest++ = pel;
    } /* for col */

    /* If right-edge overfill is required, this part takes care of that */
    if(use_right_edge) {
      u32 A, C, pel;
      A = p_src_row0[inp_w];
      C = p_src_row1[inp_w];
      pel = (coeff_y0*16*A +
             coeff_y1*16*C +
             rnd_val) / 256;
      col = right_pels;
      while(col--)
        *p_dest++ = pel;
    }
    p_dest_row += outp_wfrm;

  } /* for row */

}

/*------------------------------------------------------------------------------

    Function name: ResampleChr

        Functional description:
            Resample semi-planar chroma components using bilinear algorithm.

        Inputs:

        Outputs:
            p_dest_row        Resampled chroma image

        Returns:
            HANTRO_OK       Decoding successful
            HANTRO_NOK      Error in decoding
            END_OF_STREAM   End of stream encountered

------------------------------------------------------------------------------*/
void ResampleChr( u32 inp_w, u32 outp_w, u32 outp_wfrm, u32 outp_h, u8 *p_dest_row,
                  u8 **p_src_row_buf, u16*src_col, u8*coeff_y, u8*coeff_x,
                  u32 left_edge, u32 use_right_edge, u32 right_edge, u32 rnd_val ) {
  u32 col;
  u8 *p_src_row0;
  u8 *p_src_row1;
  u32 mid_pels;
  u32 right_pels;

  /* Calculate amount of pixels without overfill and with right edge
   * overfill */
  if(use_right_edge == 0) {
    mid_pels = outp_w - left_edge;
    right_pels = 0;
  } else {
    mid_pels = right_edge - left_edge;
    right_pels = outp_w - right_edge;
  }

  inp_w -= 2; /* We don't need original inp_w for anything, but right edge
                * overfill requires inp_w-1 */
  while(outp_h--) {
    u32 coeff_y0, coeff_y1;
    u8 * p_dest = p_dest_row;
    u16 * p_src_col = src_col;
    u8 * p_coeff_xptr;
    coeff_y1 = *coeff_y++;
    coeff_y0 = 16-coeff_y1;
    p_src_row0 = (u8*)*p_src_row_buf++;
    p_src_row1 = (u8*)*p_src_row_buf++;

    /* First pel(s) -- there may be overfill required on left edge, this
     * part of code takes that into account */
    if(left_edge) {
      u32 A, B, C, D, pel_cb, pel_cr;
      A = p_src_row0[0];    /* Cb */
      B = p_src_row0[1];    /* Cr */
      C = p_src_row1[0];    /* Cb */
      D = p_src_row1[1];    /* Cr */
      pel_cb = (coeff_y0*(16*A) +
                coeff_y1*(16*C) +
                rnd_val) / 256;
      pel_cr = (coeff_y0*(16*B) +
                coeff_y1*(16*D) +
                rnd_val) / 256;
      col = left_edge;
      while(col--) {
        *p_dest++ = pel_cb;
        *p_dest++ = pel_cr;
      }
    }

    /* Middle pels on a row. By definition, no overfill is required */
    p_coeff_xptr = coeff_x;
    col = mid_pels;
    while(col--) {
      u32 c0;
      u32 A, B, C, D, E, F;
      u32 pel_cb, pel_cr;
      u32 top, bot;
      u32 tmp;
      u32 coeff_x0, coeff_x1;

      coeff_x1 = *p_coeff_xptr++;
      coeff_x0 = 16-coeff_x1;
      c0 = *p_src_col++;

      /* Get Cb input pixels */
      A = p_src_row0[c0];
      C = p_src_row1[c0];
      c0 += 2;
      B = p_src_row0[c0];
      D = p_src_row1[c0];
      /* Weigh source rows */
      top = coeff_x0*A + coeff_x1*B;
      bot = coeff_x0*C + coeff_x1*D;
      /* Combine rows and round */
      tmp = coeff_y0*top + coeff_y1*bot;
      pel_cb = ( tmp + rnd_val) / 256;

      /* Get Cr input pixels */
      c0--;
      E = p_src_row0[c0];
      F = p_src_row1[c0];
      c0 += 2;
      A = p_src_row0[c0];
      B = p_src_row1[c0];
      /* Weigh source rows */
      top = coeff_x0*E + coeff_x1*A;
      bot = coeff_x0*F + coeff_x1*B;
      /* Combine rows and round */
      tmp = coeff_y0*top + coeff_y1*bot;
      pel_cr = ( tmp + rnd_val) / 256;

      *p_dest++ = pel_cb;
      *p_dest++ = pel_cr;
    } /* for col */

    /* If right-edge overfill is required, this part takes care of that */
    if(right_pels) {
      u32 A, B, C, D, pel_cb, pel_cr;
      A = p_src_row0[inp_w];
      B = p_src_row0[inp_w+1];
      C = p_src_row1[inp_w];
      D = p_src_row1[inp_w+1];
      pel_cb = (coeff_y0*(16*A) +
                coeff_y1*(16*C) +
                rnd_val) / 256;
      pel_cr = (coeff_y0*(16*B) +
                coeff_y1*(16*D) +
                rnd_val) / 256;
      col = right_pels;
      while(col--) {
        *p_dest++ = pel_cb;
        *p_dest++ = pel_cr;
      }
    }
    p_dest_row += outp_wfrm;

  } /* for row */

}

/*------------------------------------------------------------------------------

    Function name: CoeffTables

        Functional description:
            Create coefficient and row look-up tables for bilinear algotihm.
            Also evaluates amounts of overfill required per row.

        Inputs:

        Outputs:
            p_src_col         Array translating output columns to source columns.
            p_src_row_buf      Array translating output rows to source rows.
            p_coeff_x         X coefficients per output column.
            p_coeff_y         Y coefficients per output column.

        Returns:
            HANTRO_OK       Decoding successful
            HANTRO_NOK      Error in decoding
            END_OF_STREAM   End of stream encountered

------------------------------------------------------------------------------*/
void CoeffTables( u32 inp_w, u32 inp_h, u32 outp_w, u32 outp_h,
                  u32 luma_coeffs, u8 *inp, u32 inp_wfrm,
                  u16 *p_src_col, u8 **p_src_row_buf,
                  u8 *p_coeff_x, u8 *p_coeff_y,
                  u32 *p_left_edge, u32 *p_use_right_edge, u32* p_right_edge) {

  /* Variables */

  u32 last_col, last_row;
  u32 m,n;
  u32 tmp;
  u32 D;
  const u32 P = 16;
  u32 Hprime, Vprime;
  i32 uxl, uxr; /* row start/end offsets -- see notes */
  i32 uy0_v, uy00, uyl_b;
  i32 uyl_num;     /* Uyl numerator */
  i32 uyl_inc;     /* Uyl numerator increment each row */
  i32 ax_initial;
  i32 ax_increment;
  i32 ay_initial;
  i32 ay_increment;
  i32 ax, ay;
  u32 i, j = 0;
  u32 left_edge = 0, use_right_edge = 0, right_edge = outp_w;

  /* Code */

  /* initialize vars */
  last_col = (inp_w>>(1-luma_coeffs)) - 1;
  last_row = (inp_h>>(1-luma_coeffs)) - 1;
  m = 0;
  tmp = inp_w;
  while (tmp > 0) {
    m++;
    tmp >>= 1;
  }
  /* check for case when inp_w is power of two */
  if (inp_w == (u32)(1<<(m-1))) m--;
  Hprime = 1<<m;
  D = (64*Hprime)/P;

  n = 0;
  tmp = inp_h;
  while (tmp > 0) {
    n++;
    tmp >>= 1;
  }
  /* check for case when inp_h is power of two */
  if (inp_h == (u32)(1<<(n-1))) n--;
  Vprime = 1<<n;

  /* uxl and uxr are independent of row, so compute once only */
  uxl = 0;
  uxr = (outp_w - Hprime)*uxl + ((((inp_w - outp_w)<<1))<<(4+m));    /* numerator part */
  /* complete uxr init by dividing by H with rounding to nearest integer, */
  /* half-integers away from 0 */
  if (uxr >= 0)
    uxr = (uxr + (outp_w>>1))/outp_w;
  else {
    uxr = (uxr - (i32)(outp_w>>1))/(i32)outp_w;
  }

  /* initial x displacement and the x increment are independent of row */
  /* so compute once only */
  ax_initial = (uxl<<(m+luma_coeffs)) + (uxr - uxl) + (D>>1);
  ax_increment = (Hprime<<6) + ((uxr - uxl)<<1);

  /* most contributions to calculation of uyl do not depend upon row, */
  /* compute once only */
  uy00 = 0;
  uy0_v = ((inp_h - outp_h)<<1)<<4;
  uyl_b = outp_h*uy00 + ((uy0_v - uy00)<<n); /* numerator */
  /* complete uyl_b by dividing by V with rounding to nearest integer, */
  /* half-integers away from 0 */
  if (uyl_b >= 0)
    uyl_b = (uyl_b + (outp_h>>1))/outp_h;
  else
    uyl_b = (uyl_b - (i32)(outp_h>>1))/(i32)outp_h;
  uyl_inc = (uyl_b - uy00)<<1;
  uyl_num = ((Vprime<<luma_coeffs) - 1)*uy00 + uyl_b;

  ay_initial = D/2;
  ay_increment = D*P;
  ay = ay_initial;
  ax = ax_initial;
  if(!luma_coeffs) {
    outp_w >>= 1;
    outp_h >>= 1;
  }
  for( i = 0 ; i < outp_w ; ++i ) {
    i32 x0, x1;
    x0 = ax >> (m+6);
    x1 = x0 + 1;
    if( x0 < 0 ) {
      x0 = 0;
      left_edge = i+1;
    } else if ( x0 > (i32)last_col )    x0 = last_col;
    if( x1 < 0 )                x1 = 0;
    else if ( x1 > (i32)last_col ) {
      x1 = last_col;
      use_right_edge = 1;
      if(i < right_edge)
        right_edge = i;
    }
    if( x0 != x1 ) {
      p_src_col[j] = luma_coeffs ? x0 : 2*x0;
      p_coeff_x[j] = (ax >> (m+2)) & 0xf;
      j++;
    }
    ax += ax_increment;
  }

  for( i = 0 ; i < outp_h ; ++i ) {
    i32 add;
    i32 ay_row;
    i32 y0, y1;
    /* ay var is constant for all columns */

    add = (uyl_num >> (n + luma_coeffs))
          << (m + luma_coeffs);
    ay_row = ay + add;

    y0 = ( ay_row ) >> (m+6);
    y1 = y0 + 1;
    if( y0 < 0 )                y0 = 0;
    else if ( y0 > (i32)last_row )    y0 = last_row;
    if( y1 < 0 )                y1 = 0;
    else if ( y1 > (i32)last_row )    y1 = last_row;
    *p_src_row_buf++ = inp + y0*inp_wfrm;
    *p_src_row_buf++ = inp + y1*inp_wfrm;
    p_coeff_y[i] = ((ay_row) >> (m+2)) & 0xf;

    ay += ay_increment;
    uyl_num += uyl_inc;
  }

  *p_left_edge = left_edge;
  *p_use_right_edge = use_right_edge;
  *p_right_edge = right_edge;

}

/*------------------------------------------------------------------------------

    Function name: Up2x

        Functional description:
            Perform fast 2x downsampling on both X and Y axis

        Inputs:

        Outputs:

        Returns:
            HANTRO_OK       Decoding successful
            HANTRO_NOK      Error in decoding
            END_OF_STREAM   End of stream encountered

------------------------------------------------------------------------------*/
void Up2x( u8 * inp, u8 *inp_chr, u32 inp_w, u32 inp_wfrm, u32 inp_h, u32 inp_hfrm,
           u8 * outp, u8 *outp_chr, u32 outp_w, u32 outp_wfrm, u32 outp_h, u32 outp_hfrm,
           u32 round ) {

  /* Variables */

  u32 mid_cols;
  u32 mid_rows;
  i32 col, row;
  u8 * p_src0, * p_dest0;
  u8 * p_src1, * p_dest1;
  i32 inp_skip, outp_skip;
  u32 A, B;
  u32 round8;

  /* Code */
  UNUSED(inp_hfrm);
  UNUSED(outp_h);
  UNUSED(outp_hfrm);

  mid_cols = inp_w-1;
  mid_rows = inp_h-1;
  round8 = 7+round;
  round++;

  inp_skip = inp_wfrm - inp_w;
  outp_skip = 2*outp_wfrm - outp_w;

  /* Luma */

  /* Process first row */
  p_src0 = inp;
  p_dest0 = outp;
  A = *p_src0++;
  *p_dest0++ = A;
  col = mid_cols;
  while( col-- ) {
    u32 pel0, pel1;
    B = *p_src0++;
    pel0 = ( A*3 + B + round ) >> 2;
    pel1 = ( B*3 + A + round ) >> 2;
    *p_dest0++ = pel0;
    *p_dest0++ = pel1;
    A = B;
  }
  *p_dest0++ = A;

  /* Process middle rows */
  p_src0 = inp;
  p_src1 = p_src0 + inp_wfrm;
  p_dest0 = outp + outp_wfrm;
  p_dest1 = p_dest0 + outp_wfrm;
  row = mid_rows;
  while( row-- ) {
    u32 C, D;
    u32 pel0, pel1;
    u32 tmp0;
    u32 tmp1;
    u32 tmp2;
    u32 tmp3;
    A = *p_src0++;
    B = *p_src0++;
    C = *p_src1++;
    D = *p_src1++;
    /* First pels */
    pel0 = ( A*3 + C + round ) >> 2;
    pel1 = ( C*3 + A + round ) >> 2;
    *p_dest0++ = pel0;
    *p_dest1++ = pel1;
    col = mid_cols >> 1;

    while( col-- ) {
      /* 1st part */
      tmp0 = A*3 + B;
      tmp1 = B*3 + A;
      tmp2 = C*3 + D;
      tmp3 = D*3 + C;
      pel0 = ( 3*tmp0 + tmp2 + round8 ) >> 4;
      pel1 = ( 3*tmp1 + tmp3 + round8 ) >> 4;
      *p_dest0++ = pel0;
      *p_dest0++ = pel1;
      pel0 = ( tmp0 + 3*tmp2 + round8 ) >> 4;
      pel1 = ( tmp1 + 3*tmp3 + round8 ) >> 4;
      *p_dest1++ = pel0;
      *p_dest1++ = pel1;
      A = *p_src0++;
      C = *p_src1++;

      /* 2nd part (same as 1st but pixel positions reversed) */
      tmp1 = A*3 + B;
      tmp0 = B*3 + A;
      tmp3 = C*3 + D;
      tmp2 = D*3 + C;
      pel0 = ( 3*tmp0 + tmp2 + round8 ) >> 4;
      pel1 = ( 3*tmp1 + tmp3 + round8 ) >> 4;
      *p_dest0++ = pel0;
      *p_dest0++ = pel1;
      pel0 = ( tmp0 + 3*tmp2 + round8 ) >> 4;
      pel1 = ( tmp1 + 3*tmp3 + round8 ) >> 4;
      *p_dest1++ = pel0;
      *p_dest1++ = pel1;
      B = *p_src0++;
      D = *p_src1++;
    }
    /* extra 1st part, required because mid_cols is always odd */
    tmp0 = A*3 + B;
    tmp1 = B*3 + A;
    tmp2 = C*3 + D;
    tmp3 = D*3 + C;
    pel0 = ( 3*tmp0 + tmp2 + round8 ) >> 4;
    pel1 = ( 3*tmp1 + tmp3 + round8 ) >> 4;
    *p_dest0++ = pel0;
    *p_dest0++ = pel1;
    pel0 = ( tmp0 + 3*tmp2 + round8 ) >> 4;
    pel1 = ( tmp1 + 3*tmp3 + round8 ) >> 4;
    *p_dest1++ = pel0;
    *p_dest1++ = pel1;

    pel0 = ( B*3 + D + round ) >> 2;
    pel1 = ( D*3 + B + round ) >> 2;
    *p_dest0++ = pel0;
    *p_dest1++ = pel1;

    p_src0 += inp_skip;
    p_src1 += inp_skip;
    p_dest0 += outp_skip;
    p_dest1 += outp_skip;
  }

  /* Last row */
  A = *p_src0++;
  *p_dest0++ = A;
  col = mid_cols;
  while( col-- ) {
    u32 pel0, pel1;
    B = *p_src0++;
    pel0 = ( A*3 + B + round ) >> 2;
    pel1 = ( B*3 + A + round ) >> 2;
    *p_dest0++ = pel0;
    *p_dest0++ = pel1;
    A = B;
  }
  *p_dest0++ = A;

  /* Chroma */
  inp_w >>= 1;
  inp_h >>= 1;
  mid_cols = inp_w-1;
  mid_rows = inp_h-1;
  inp_skip -= 2;

  /* Process first row */
  p_src0 = inp_chr;
  p_dest0 = outp_chr;
  A = *p_src0++;
  B = *p_src0++;
  *p_dest0++ = A;
  *p_dest0++ = B;
  col = mid_cols;
  while( col-- ) {
    u32 C, D;
    u32 pel0, pel1;
    u32 pel2, pel3;
    C = *p_src0++;
    D = *p_src0++;
    pel0 = ( A*3 + C + round ) >> 2;
    pel1 = ( C*3 + A + round ) >> 2;
    pel2 = ( B*3 + D + round ) >> 2;
    pel3 = ( D*3 + B + round ) >> 2;
    *p_dest0++ = pel0;
    *p_dest0++ = pel2;
    *p_dest0++ = pel1;
    *p_dest0++ = pel3;
    A = C;
    B = D;
  }
  *p_dest0++ = A;
  *p_dest0++ = B;

  /* Process middle rows */
  p_src0 = inp_chr;
  p_src1 = p_src0 + inp_wfrm;
  p_dest0 = outp_chr + outp_wfrm;
  p_dest1 = p_dest0 + outp_wfrm;
  row = mid_rows;
  while( row-- ) {
    u32 C, D, E, F, G, H;
    u32 tmp0;
    u32 tmp1;
    u32 tmp2;
    u32 tmp3;
    A = *p_src0++;
    E = *p_src0++;
    C = *p_src1++;
    G = *p_src1++;
    B = *p_src0++;
    F = *p_src0++;
    D = *p_src1++;
    H = *p_src1++;
    /* First pels */
    tmp0 = ( A*3 + C + round ) >> 2;
    tmp1 = ( C*3 + A + round ) >> 2;
    tmp2 = ( E*3 + G + round ) >> 2;
    tmp3 = ( G*3 + E + round ) >> 2;
    *p_dest0++ = tmp0;
    *p_dest0++ = tmp2;
    *p_dest1++ = tmp1;
    *p_dest1++ = tmp3;
    col = mid_cols;
    while( col-- ) {
      u32 pel0, pel1;
      /* Cb */
      tmp0 = A*3 + B;
      tmp1 = B*3 + A;
      tmp2 = C*3 + D;
      tmp3 = D*3 + C;
      pel0 = ( 3*tmp0 + tmp2 + round8 ) >> 4;
      pel1 = ( 3*tmp1 + tmp3 + round8 ) >> 4;
      p_dest0[0] = pel0;
      p_dest0[2] = pel1;
      pel0 = ( tmp0 + 2*tmp2 + tmp2 + round8 ) >> 4;
      pel1 = ( tmp1 + 2*tmp3 + tmp3 + round8 ) >> 4;
      p_dest1[0] = pel0;
      p_dest1[2] = pel1;

      /* Cr */
      tmp0 = E*3 + F;
      tmp1 = F*3 + E;
      tmp2 = G*3 + H;
      tmp3 = H*3 + G;
      pel0 = ( 3*tmp0 + tmp2 + round8 ) >> 4;
      pel1 = ( 3*tmp1 + tmp3 + round8 ) >> 4;
      p_dest0[1] = pel0;
      p_dest0[3] = pel1;
      pel0 = ( tmp0 + 3*tmp2 + round8 ) >> 4;
      pel1 = ( tmp1 + 3*tmp3 + round8 ) >> 4;
      p_dest1[1] = pel0;
      p_dest1[3] = pel1;

      p_dest0 += 4;
      p_dest1 += 4;

      A = B;
      C = D;
      E = F;
      G = H;
      B = *p_src0++;
      F = *p_src0++;
      D = *p_src1++;
      H = *p_src1++;
    }

    tmp0 = ( A*3 + C + round ) >> 2;
    tmp1 = ( C*3 + A + round ) >> 2;
    tmp2 = ( E*3 + G + round ) >> 2;
    tmp3 = ( G*3 + E + round ) >> 2;
    *p_dest0++ = tmp0;
    *p_dest0++ = tmp2;
    *p_dest1++ = tmp1;
    *p_dest1++ = tmp3;

    p_src0 += inp_skip;
    p_src1 += inp_skip;
    p_dest0 += outp_skip;
    p_dest1 += outp_skip;
  }

  /* Last row */
  A = *p_src0++;
  B = *p_src0++;
  *p_dest0++ = A;
  *p_dest0++ = B;
  col = mid_cols;
  while( col-- ) {
    u32 C, D;
    u32 pel0, pel1;
    u32 pel2, pel3;
    C = *p_src0++;
    D = *p_src0++;
    pel0 = ( A*3 + C + round ) >> 2;
    pel1 = ( C*3 + A + round ) >> 2;
    pel2 = ( B*3 + D + round ) >> 2;
    pel3 = ( D*3 + B + round ) >> 2;
    *p_dest0++ = pel0;
    *p_dest0++ = pel2;
    *p_dest0++ = pel1;
    *p_dest0++ = pel3;
    A = C;
    B = D;
  }
  *p_dest0++ = A;
  *p_dest0++ = B;

  /* TODO padding? */

}

/*------------------------------------------------------------------------------

    Function name: Down2x

        Functional description:
            Perform fast 2x downsampling on both X and Y axis

        Inputs:

        Outputs:

        Returns:
            HANTRO_OK       Decoding successful
            HANTRO_NOK      Error in decoding
            END_OF_STREAM   End of stream encountered

------------------------------------------------------------------------------*/
void Down2x( u8 * inp, u8 *inp_chr, u32 inp_w, u32 inp_wfrm, u32 inp_h, u32 inp_hfrm,
             u8 * outp, u8 *outp_chr, u32 outp_w, u32 outp_wfrm, u32 outp_h, u32 outp_hfrm,
             u32 round ) {

  /* Variables */

  u32 row, col;
  u8 *p_src_row0, *p_src_row1;
  u8 *p_dest;
  u32 inp_skip, outp_skip;

  /* Code */
  UNUSED(inp_hfrm);
  UNUSED(outp_h);
  UNUSED(outp_hfrm);

  p_src_row0 = inp;
  p_src_row1 = inp + inp_wfrm;
  p_dest = outp;

  inp_skip = 2*inp_wfrm - inp_w;
  outp_skip = outp_wfrm - outp_w;

  /* Luma */

  row = inp_h >> 1;
  round++;
  while(row--) {
    col = inp_w >> 2;
    while(col--) {
      u32 pel0, pel1;
      u32 A, B, C, D, E, F, G, H;
      /* Do two output pixels at a time */
      A = *p_src_row0++;
      B = *p_src_row0++;
      C = *p_src_row0++;
      D = *p_src_row0++;
      E = *p_src_row1++;
      F = *p_src_row1++;
      G = *p_src_row1++;
      H = *p_src_row1++;
      pel0 = (A + B + E + F + round) >> 2;
      pel1 = (C + D + G + H + round) >> 2;
      *p_dest++ = pel0;
      *p_dest++ = pel1;
    }

    p_src_row0 += inp_skip;
    p_src_row1 += inp_skip;
    p_dest += outp_skip;
  }

  p_src_row0 = inp_chr;
  p_src_row1 = inp_chr + inp_wfrm;
  p_dest = outp_chr;

  /* Chroma */

  row = inp_h >> 2;
  while(row--) {
    col = inp_w >> 2;
    while(col--) {
      u32 pel_cb, pel_cr;
      u32 A, B, C, D;
      u32 E, F, G, H;
      A = *p_src_row0++;
      B = *p_src_row0++;
      C = *p_src_row0++;
      D = *p_src_row0++;
      E = *p_src_row1++;
      F = *p_src_row1++;
      G = *p_src_row1++;
      H = *p_src_row1++;
      pel_cb = (A + C + E + G + round) >> 2;
      pel_cr = (B + D + F + H + round) >> 2;
      *p_dest++ = pel_cb;
      *p_dest++ = pel_cr;
    }

    p_src_row0 += inp_skip;
    p_src_row1 += inp_skip;
    p_dest += outp_skip;
  }

  /* TODO padding? */
}


/*------------------------------------------------------------------------------

    Function name: RvRasterToTiled8x4

        Functional description:
            Convert 8x4 tiled output to raster scan output.

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/
void RvTiledToRaster8x4( u32 *p_in, u32 *p_out, u32 pic_width, u32 pic_height ) {
  u32 i, j;
  u32 tiles_w;
  u32 tiles_h;
  u32 skip;
  u32 *p_out0 = p_out;
  u32 *p_out1 = p_out +   (pic_width/4);
  u32 *p_out2 = p_out + 2*(pic_width/4);
  u32 *p_out3 = p_out + 3*(pic_width/4);

  tiles_w = pic_width/8;
  tiles_h = pic_height/4;
  skip = pic_width-pic_width/4;

  for( i = 0 ; i < tiles_h ; ++i) {
    for( j = 0 ; j < tiles_w ; ++j ) {
      *p_out0++ = *p_in++;
      *p_out0++ = *p_in++;
      *p_out1++ = *p_in++;
      *p_out1++ = *p_in++;
      *p_out2++ = *p_in++;
      *p_out2++ = *p_in++;
      *p_out3++ = *p_in++;
      *p_out3++ = *p_in++;
    }
    p_out0 += skip;
    p_out1 += skip;
    p_out2 += skip;
    p_out3 += skip;
  }
}


/*------------------------------------------------------------------------------

    Function name: RvRasterToTiled8x4

        Functional description:
            Convert raster scan output to 8x4 tiled output.

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/
void RvRasterToTiled8x4( u32 *inp, u32 *outp, u32 width, u32 height) {
  u32 i, j;
  u32 tiles_w;
  u32 tiles_h;
  u32 skip;
  u32 *p_in0 = inp,
       *p_in1 = inp +   (width/4),
        *p_in2 = inp + 2*(width/4),
         *p_in3 = inp + 3*(width/4);

  tiles_w = width/8;
  tiles_h = height/4;
  skip = width-width/4;

  for( i = 0 ; i < tiles_h ; ++i) {
    for( j = 0 ; j < tiles_w ; ++j ) {
      *outp++ = *p_in0++;
      *outp++ = *p_in0++;
      *outp++ = *p_in1++;
      *outp++ = *p_in1++;
      *outp++ = *p_in2++;
      *outp++ = *p_in2++;
      *outp++ = *p_in3++;
      *outp++ = *p_in3++;
    }

    p_in0 += skip;
    p_in1 += skip;
    p_in2 += skip;
    p_in3 += skip;
  }
}

/*------------------------------------------------------------------------------

    Function name: RvResample

        Functional description:
            Create coefficient and row look-up tables for bilinear algotihm.
            Also evaluates amounts of overfill required per row.

        Inputs:

        Outputs:
            p_src_col         Array translating output columns to source columns.
            p_src_row_buf      Array translating output rows to source rows.
            p_coeff_x         X coefficients per output column.
            p_coeff_y         Y coefficients per output column.

        Returns:
            HANTRO_OK       Decoding successful
            HANTRO_NOK      Error in decoding
            END_OF_STREAM   End of stream encountered

------------------------------------------------------------------------------*/
void RvResample( u8 * inp, u8 *inp_chr, u32 inp_w, u32 inp_wfrm, u32 inp_h,
                 u32 inp_hfrm, u8 * outp, u8 *outp_chr, u32 outp_w,
                 u32 outp_wfrm, u32 outp_h, u32 outp_hfrm,
                 u32 round, u8 *work_memory, u32 tiled_mode ) {

  /* Variables */

  u8  **p_src_row_buf;
  u16 *src_col;
  u8  *coeff_x;
  u8  *coeff_y;
  u32 rnd_val;
  u32 left_edge = 0;
  u32 right_edge = outp_w;
  u32 use_right_edge = 0;

  /* Code */

  /* If we are using tiled mode do:
   * 1) tiled-to-raster conversion from inp-buffer to outp-buffer
   * 2) resampling from outp-buffer to inp-buffer
   * 3) raster-to-tiled conversion from inp-buffer to outp-buffer
   * Buffer sizes are always ok because they are allocated
   * according to maximum frame dimensions for the stream.
   */
  if(tiled_mode) {
    u8 *p_tmp;
    RvTiledToRaster8x4( (u32*)inp, (u32*)outp, inp_wfrm, inp_hfrm );
    RvTiledToRaster8x4( (u32*)inp_chr, (u32*)(outp+inp_wfrm*inp_hfrm),
                        inp_w, inp_h/2 );
    /* Swap ptrs */
    p_tmp = inp;
    inp = outp;
    outp = p_tmp;
    inp_chr = inp+inp_wfrm*inp_hfrm;
    outp_chr = outp+outp_wfrm*outp_hfrm;
  }

  if ((inp_w<<1) == outp_w && (inp_h<<1) == outp_h ) {
    Up2x( inp, inp_chr, inp_w, inp_wfrm, inp_h, inp_hfrm,
          outp, outp_chr, outp_w, outp_wfrm, outp_h, outp_hfrm,
          round );
  } else if ((inp_w>>1) == outp_w && (inp_h>>1) == outp_h ) {
    Down2x( inp, inp_chr, inp_w, inp_wfrm, inp_h, inp_hfrm,
            outp, outp_chr, outp_w, outp_wfrm, outp_h, outp_hfrm,
            round );
  } else { /* Arbitrary scale ratios */
    /* Assign work memory */
    p_src_row_buf = (u8**)work_memory;
    src_col = (u16*)(p_src_row_buf + outp_h*2);
    coeff_x = ((u8*)src_col) + 2*outp_w;
    coeff_y = coeff_x + outp_w;

    rnd_val = 127 + round;

    /* Process luma */
    CoeffTables( inp_w, inp_h, outp_w, outp_h,
                 1/*luma_coeffs*/, inp, inp_wfrm,
                 src_col, p_src_row_buf,
                 coeff_x, coeff_y,
                 &left_edge, &use_right_edge, &right_edge );

    ResampleY( inp_w, outp_w, outp_wfrm, outp_h, outp, p_src_row_buf, src_col, coeff_y, coeff_x,
               left_edge, use_right_edge, right_edge, rnd_val );
    /* And then process chroma */
    CoeffTables( inp_w, inp_h, outp_w, outp_h,
                 0/*luma_coeffs*/, inp_chr,
                 inp_wfrm,
                 src_col, p_src_row_buf,
                 coeff_x, coeff_y,
                 &left_edge, &use_right_edge, &right_edge );
    ResampleChr( inp_w, outp_w>>1, outp_wfrm, outp_h>>1,
                 outp_chr, p_src_row_buf,
                 src_col, coeff_y, coeff_x,
                 left_edge, use_right_edge, right_edge, rnd_val );
  }

  if(tiled_mode) {
    RvRasterToTiled8x4( (u32*)outp, (u32*)inp, outp_wfrm, outp_hfrm );
    RvRasterToTiled8x4( (u32*)outp_chr, (u32*)(inp+outp_wfrm*outp_hfrm),
                        outp_w, outp_h/2 );
  }
}


void rvRpr( picture_t *p_src,
            picture_t *p_dst,
            struct DWLLinearMem *rpr_work_buffer,
            u32 round,
            u32 new_coded_width, u32 new_coded_height,
            u32 tiled_mode ) {

  /* Variables */

  u8 * p_inp_y, * p_inp_chr;
  u8 * p_outp_y, * p_outp_chr;
  u32 inp_w, inp_wfrm;
  u32 inp_h, inp_hfrm;
  u32 outp_w, outp_wfrm;
  u32 outp_h, outp_hfrm;
  u8 *memory;

  /* Get pointers etc */
  inp_w = p_src->coded_width;
  inp_wfrm = p_src->frame_width;
  inp_h = p_src->coded_height;
  inp_hfrm = p_src->coded_height;
  outp_w = new_coded_width;
  outp_wfrm = ( 15 + outp_w ) & ~15;
  outp_h = new_coded_height;
  outp_hfrm = ( 15 + outp_h ) & ~15;
  p_inp_y = (u8*)p_src->data.virtual_address;
  p_inp_chr = p_inp_y + inp_wfrm*inp_hfrm;
  p_outp_y = (u8*)p_dst->data.virtual_address;
  p_outp_chr = p_outp_y + outp_wfrm*outp_hfrm;
  memory = (u8*)rpr_work_buffer->virtual_address;

  /* Resample */
  RvResample( p_inp_y, p_inp_chr, inp_w, inp_wfrm, inp_h, inp_hfrm,
              p_outp_y, p_outp_chr, outp_w, outp_wfrm, outp_h, outp_hfrm,
              round, memory, tiled_mode);
}


