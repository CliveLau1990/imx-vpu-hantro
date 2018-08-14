/*
 Copyright 2011 The LibYuv Project Authors. All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:
 
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in
 the documentation and/or other materials provided with the
 distribution.
 
 * Neither the name of Google nor the names of its contributors may
 be used to endorse or promote products derived from this software
 without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <float.h>
#include <math.h>
#include "basetype.h"


#ifdef PSNR

static const i64 cc1 =  26634;  // (64^2*(.01*255)^2
static const i64 cc2 = 239708;  // (64^2*(.03*255)^2

static double Ssim8x8_C(const u8* src_a, int stride_a,
                        const u8* src_b, int stride_b) {
    i64 sum_a = 0;
    i64 sum_b = 0;
    i64 sum_sq_a = 0;
    i64 sum_sq_b = 0;
    i64 sum_axb = 0;
    int i,j;
    for ( i = 0; i < 8; ++i) {
      for ( j = 0; j < 8; ++j) {
        sum_a += src_a[j];
        sum_b += src_b[j];
        sum_sq_a += src_a[j] * src_a[j];
        sum_sq_b += src_b[j] * src_b[j];
        sum_axb += src_a[j] * src_b[j];
      }

      src_a += stride_a;
      src_b += stride_b;
    }

    const i64 count = 64;
    // scale the constants by number of pixels
    const i64 c1 = (cc1 * count * count) >> 12;
    const i64 c2 = (cc2 * count * count) >> 12;

    const i64 sum_a_x_sum_b = sum_a * sum_b;

    const i64 ssim_n = (2 * sum_a_x_sum_b + c1) *
                         (2 * count * sum_axb - 2 * sum_a_x_sum_b + c2);

    const i64 sum_a_sq = sum_a*sum_a;
    const i64 sum_b_sq = sum_b*sum_b;

    const i64 ssim_d = (sum_a_sq + sum_b_sq + c1) *
                         (count * sum_sq_a - sum_a_sq +
                          count * sum_sq_b - sum_b_sq + c2);

    if (ssim_d == 0.0)
      return 999999.0;
    return ssim_n * 1.0 / ssim_d;
}

// We are using a 8x8 moving window with starting location of each 8x8 window
// on the 4x4 pixel grid. Such arrangement allows the windows to overlap
// block boundaries to penalize blocking artifacts.

double CalcFrameSsim(const u8* src_a, int stride_a,
                     const u8* src_b, int stride_b,
                     int width, int height) {
    int samples = 0;
    double ssim_total = 0;
    int i,j;

    double (*Ssim8x8)(const u8* src_a, int stride_a,
                      const u8* src_b, int stride_b);

    Ssim8x8 = Ssim8x8_C;

    // sample point start with each 4x4 location
    for ( i = 0; i < height - 8; i += 4) {
      for ( j = 0; j < width - 8; j += 4) {
        ssim_total += Ssim8x8(src_a + j, stride_a, src_b + j, stride_b);
        samples++;
      }

      src_a += stride_a * 4;
      src_b += stride_b * 4;
    }

    ssim_total /= samples;
    return ssim_total;
}
#endif

double I420Ssim(const u8 *src_y_a, int stride_y_a,
                       const u8 *src_u_a, int stride_u_a,
                       const u8 *src_v_a, int stride_v_a,
                       const u8 *src_y_b, int stride_y_b,
                       const u8 *src_u_b, int stride_u_b,
                       const u8 *src_v_b, int stride_v_b,
                       int width, int height)
 {
#ifdef PSNR
    const double ssim_y = CalcFrameSsim(src_y_a, stride_y_a,
                                        src_y_b, stride_y_b, width, height);
    const int width_uv = (width + 1) >> 1;
    const int height_uv = (height + 1) >> 1;
    const double ssim_u = CalcFrameSsim(src_u_a, stride_u_a,
                                        src_u_b, stride_u_b,
                                        width_uv, height_uv);
    const double ssim_v = CalcFrameSsim(src_v_a, stride_v_a,
                                        src_v_b, stride_v_b,
                                        width_uv, height_uv);
    return ssim_y * 0.8 + 0.1 * (ssim_u + ssim_v);
#else
    return 0.0;
#endif
}

