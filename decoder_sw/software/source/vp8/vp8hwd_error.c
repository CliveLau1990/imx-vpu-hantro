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
#include "dwl.h"
#include "vp8hwd_error.h"
#include "vp8hwd_debug.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

#define EXTRACT_BITS(x,cnt,pos) \
        (((x) & (((1<<(cnt))-1)<<(pos)))>>(pos))

#define MV_HOR(p) (((i32)EXTRACT_BITS((p), 14, 18)<<18)>>18)
#define MV_VER(p) (((i32)EXTRACT_BITS((p), 13, 5)<<19)>>19)
#define REF_PIC(p) ((p) & 0x7)

#define MAX3(a,b,c) ((b) > (a) ? ((c) > (b) ? 2 : 1) : ((c) > (a) ? 2 : 0))

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    vp8hwdInitEc

        Allocate memory for error concealment, buffers shared by SW/HW
        are allocated elsewhere.
------------------------------------------------------------------------------*/
u32 vp8hwdInitEc(vp8ec_t *ec, u32 w, u32 h, u32 num_mvs_per_mb) {

  ASSERT(ec);

  w /= 16;
  h /= 16;
  ec->width = w;
  ec->height = h;
  ec->num_mvs_per_mb = num_mvs_per_mb;
  ec->mvs = DWLmalloc(w * h * num_mvs_per_mb * sizeof(ecMv_t));
  if (ec->mvs == NULL)
    return HANTRO_NOK;

  return HANTRO_OK;
}

/*------------------------------------------------------------------------------
    vp8hwdReleaseEc
------------------------------------------------------------------------------*/
void vp8hwdReleaseEc(vp8ec_t *ec) {

  ASSERT(ec);

  if (ec->mvs)
    DWLfree(ec->mvs);

}

/*------------------------------------------------------------------------------
    updateMv

        Add contribution of extrapolated mv to internal struct if inside
        picture area. Used reference picture determines which component is
        updated, weigth used in summing is w
------------------------------------------------------------------------------*/
void updateMv( vp8ec_t *ec, i32 x, i32 y, i32 hor, i32 ver, u32 ref, i32 w) {

  u32 b;

  ASSERT(ec);

  if (x >= 0 && x < (i32)ec->width*4 &&
      y >= 0 && y < (i32)ec->height*4) {
    /* mbNum */
    b = (y & ~0x3) * ec->width * 4 + (x & ~0x3) * 4;
    /* mv/block within mb */
    b += (y & 0x3) * 4 + (x & 0x3);

    ec->mvs[b].tot_weight[ref] += w;
    ec->mvs[b].tot_mv[ref].hor += w * hor;
    ec->mvs[b].tot_mv[ref].ver += w * ver;
  }

}

/*------------------------------------------------------------------------------
    getNeighbors

        Determine neighbor mvs of current mb. Return index of ref pic
        referenced most in the neighborhood
------------------------------------------------------------------------------*/
u32 getNeighbors(u32 *p, mv_t *nmv, u32 *nref, u32 row, u32 col,
                 u32 height, u32 width) {
  u32 i = 0, j;
  u32 ref_cnt[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  DWLmemset(nmv, 0, sizeof(mv_t)*20);
  /* initialize to anything else but 0 */
  DWLmemset(nref, 0xFF, sizeof(u32)*20);

  /* above left */
  if (row) {
    if (col) {
      nref[i] = REF_PIC(p[-16*width-1]);
      nmv[i].hor = MV_HOR(p[-16*width-1]);
      nmv[i].ver = MV_VER(p[-16*width-1]);
      ref_cnt[nref[i]]++;
    }
    i++;

    for (j = 0; j < 4; j++, i++) {
      nref[i] = REF_PIC(p[-16*width+12+j]);
      nmv[i].hor = MV_HOR(p[-16*width+12+j]);
      nmv[i].ver = MV_VER(p[-16*width+12+j]);
    }
    ref_cnt[nref[i-1]] += 4;
  } else
    i += 5;

  if (col < width - 1) {
    if (row) {
      nref[i] = REF_PIC(p[16-16*width]);
      nmv[i].hor = MV_HOR(p[16-16*width]);
      nmv[i].ver = MV_VER(p[16-16*width]);
      ref_cnt[nref[i]]++;
    }
    i++;
    for (j = 0; j < 16; j += 4, i++) {
      nref[i] = REF_PIC(p[16+j]);
      nmv[i].hor = MV_HOR(p[16+j]);
      nmv[i].ver = MV_VER(p[16+j]);
    }
    ref_cnt[nref[i-1]] += 4;
  } else
    i += 5;

  if (row < height - 1) {
    if (col < width - 1) {
      nref[i] = REF_PIC(p[16+16*width]);
      nmv[i].hor = MV_HOR(p[16+16*width]);
      nmv[i].ver = MV_VER(p[16+16*width]);
      ref_cnt[nref[i]]++;
    }
    i++;
    for (j = 0; j < 4; j++, i++) {
      nref[i] = REF_PIC(p[16*width+j]);
      nmv[i].hor = MV_HOR(p[16*width+j]);
      nmv[i].ver = MV_VER(p[16*width+j]);
    }
    ref_cnt[nref[i-1]] += 4;
  } else
    i += 5;

  if (col) {
    if (row < height - 1) {
      nref[i] = REF_PIC(p[-13+16*width]);
      nmv[i].hor = MV_HOR(p[-13+16*width]);
      nmv[i].ver = MV_VER(p[-13+16*width]);
    }
    ref_cnt[nref[i]]++;
    i++;
    for (j = 0; j < 16; j+=4, i++) {
      nref[i] = REF_PIC(p[-13+j]);
      nmv[i].hor = MV_HOR(p[-13+j]);
      nmv[i].ver = MV_VER(p[-13+j]);
    }
    ref_cnt[nref[i-1]] += 4;
  }

  /* 0=last ref, 4=golden, 5=altref */
  return ref_cnt[0] >= ref_cnt[4] ? (ref_cnt[0] >= ref_cnt[5] ? 0 : 5) :
         (ref_cnt[4] >= ref_cnt[5] ? 4 : 5);

}

/*------------------------------------------------------------------------------
    vp8hwdEc

        Main error concealment function.

        Implements error concealment algorithm close to algorithm described
        in "Error Concealment Algorithm for VP8". (don't know how to properly
        refer to particular doc in Google Docs?).

        First loop goes through all the motion vectors of the reference picture
        and extrapolates motion to current picture (stored in internal
        structures). Latter loops go through motion vectors of the current
        picture that need to be concealed (first intra macroblocks whose
        residual is lost, then completely lost macroblocks).

        Function shall be generalized to compute num_mvs_per_mb motion vectors
        for each mb of the current picture, currently assumes that all
        16 mvs are computed (probably too CPU intensive with high resolution
        video).

------------------------------------------------------------------------------*/
void vp8hwdEc( vp8ec_t *ec, u32 *p_ref, u32 *p_out, u32 start_mb, u32 all) {

  u32 i, j, tmp;
  u32 num_mbs,num_mvs;
  i32 hor, ver;
  i32 mb_x, mb_y, x, y;
  i32 wx, wy;
  u32 ref;
  u32 start_all;
  u32 *p = p_ref;
  mv_t nmv[20];
  u32 nref[20];

  ASSERT(ec);
  ASSERT(p_ref);
  ASSERT(p_out);
  ASSERT(ec->num_mvs_per_mb == 16); /* assumed here and there, fixed later */

  num_mbs = ec->width * ec->height;
  num_mvs = num_mbs * ec->num_mvs_per_mb;

  /* motion vector extrapolation if part (or all) of control partition lost */
  if (all) {

    if (p_ref == p_out)
      return;

    DWLmemset(ec->mvs, 0, num_mvs * sizeof(ecMv_t));

    /* determine overlaps from ref mvs */
    mb_x = mb_y = x = y = 0;
    for (i = 0; i < num_mbs; i++) {
      ref = REF_PIC(*p);
      /* only consider previous ref (index 0) */
      if (ref == 0) {
        for (j = 0; j < ec->num_mvs_per_mb; j++, p++) {
          hor = MV_HOR(*p);
          ver = MV_VER(*p);
          /* (x,y) indicates coordinates of the top-left corner of
           * the block to which mv points, 4pel units */
          /* TODO: lut for x/y offsets, depends on num_mvs_per_mb,
           * assumed
           * 16 initially */
          x = mb_x*4 + (j&0x3) + ((-hor) >> 4);
          y = mb_y*4 + (j>>2)  + ((-ver) >> 4);

          /* offset within 4x4 block, used to determine overlap
           * which is used as weight in averaging */
          wx = ((-hor) >> 2) & 0x3;
          wy = ((-ver) >> 2) & 0x3;

          /* update mv where top/left corner of the extrapolated
           * block hits */
          updateMv(ec, x    , y    , hor, ver, ref, (4-wx)*(4-wy));
          /* if not aligned -> update neighbors on right and bottom */
          if (wx || wy) {
            updateMv(ec, x + 1, y    , hor, ver, ref, (  wx)*(4-wy));
            updateMv(ec, x    , y + 1, hor, ver, ref, (4-wx)*(  wy));
            updateMv(ec, x + 1, y + 1, hor, ver, ref, (  wx)*(  wy));
          }
        }
      } else
        p += ec->num_mvs_per_mb;
      mb_x++;
      if (mb_x == (i32)ec->width) {
        mb_x = 0;
        mb_y++;
      }
    }
  }

  /* determine final concealment mv and write to shared mem */
  /* if all is set -> error was found in control partition and we assume
   * that all residual was lost -> conceal all intra mbs and everything
   * starting from start_mb */
  if (all) {
    start_all = start_mb * ec->num_mvs_per_mb;
    i = 0;
    mb_y = mb_x = 0;
  } else {
    start_all = num_mvs;
    i = start_mb * ec->num_mvs_per_mb;
    mb_y = start_mb / ec->width;
    mb_x = start_mb - mb_y * ec->width;
  }
  p = p_out + i;

  /* all intra mbs until start_all */
  for (; i < start_all; i+=16, p+=16) {
    /* intra is marked with refpic index 1 */
    if (REF_PIC(p[0]) == 1) {
      i32 w = 0;
      ref = getNeighbors(p, nmv, nref, mb_y, mb_x,
                         start_all/16 > ec->width*2 ? start_all/16/ec->width-1 : 1,
                         ec->width);
      /* sum of all neighbor mvs referencing the most referenced one */
      hor = ver = 0;
      for (j = 0; j < 20; j++) {
        if (nref[j] == ref) {
          w++;
          hor += nmv[j].hor;
          ver += nmv[j].ver;
        }
      }
      if (w) {
        hor /= w;
        ver /= w;
      }
      tmp = ((hor & 0x3FFF) << 18) | ((ver & 0x1FFF) <<  5) | (ref);
      /* same vector for all mvs of concealed intra mb */
      for (j = 0; j < 16; j++)
        p[j] = tmp;
    }
    mb_x++;
    if (mb_x == (i32)ec->width) {
      mb_x = 0;
      mb_y++;
    }
  }
  /* all mbs starting from start_all */
  for (; i < num_mvs; i++, p++) {
    /* always choose last ref, could probably drop all the computation
     * on golden and alt */
    ref = 0;

    /* any overlap in current block? */
    if ((x = ec->mvs[i].tot_weight[ref]) != 0) {
      hor = ec->mvs[i].tot_mv[ref].hor / x;
      ver = ec->mvs[i].tot_mv[ref].ver / x;
    } else if (1)
      hor = ver = 0;
#if 0
    else { /* no overlap -> average of left/above neighbors */
      u32 b = i & 0xF; /* mv within current mb */
      u32 w = 0;
      u32 ref1 = 0, ref2 = 0;
      hor = ver = 0;
      if (i) { /* either left or above exists */
        if (mb_x || (b & 0x3)) {
          /* left in current mb */
          if (b&0x3) {
            ref1 = p[-1] & 0x3;
            hor += MV_HOR(p[-1]);
            ver += MV_VER(p[-1]);
            w++;
          }
          /* left in previous mb, used if non-intra */
          else if (p[-13] & 0x3) {
            ref1 = p[-13] & 0x3;
            hor += MV_HOR(p[-13]);
            ver += MV_VER(p[-13]);
            w++;
          }
        }
        if (mb_y || b >= 4) {
          /* above in current mb */
          if (b >= 4) {
            ref2 = p[-4] & 0x3;
            if (!ref1 || ref2 == ref1) {
              hor += MV_HOR(p[-4]);
              ver += MV_VER(p[-4]);
              w++;
            } else if (ref2 == 1) { /* favor last ref */
              ref1 = 0;
              hor = MV_HOR(p[-4]);
              ver = MV_VER(p[-4]);
            }
          }
          /* above in mb above, used if non-intra */
          else if (p[12-16*ec->width] & 0x3) {
            ref2 = p[12-16*ec->width] & 0x3;
            if (!ref1 || ref2 == ref1) {
              hor += MV_HOR(p[12-16*ec->width]);
              ver += MV_VER(p[12-16*ec->width]);
              w++;
            } else if (ref2 == 1) {
              ref1 = 0;
              hor = MV_HOR(p[12-16*ec->width]);
              ver = MV_VER(p[12-16*ec->width]);
            }
          }
        }
        /* both above and left exist -> average */
        if (w == 2) {
          hor /= 2;
          ver /= 2;
        }
        ref = ref1 ? ref1 : ref2;
        ref--;
      }
    }
#endif

    tmp = ((hor & 0x3FFF) << 18) | ((ver & 0x1FFF) <<  5) | ref;

    *p = tmp;

    if ((i & 0xF) == 0xF) {
      mb_x++;
      if (mb_x == (i32)ec->width) {
        mb_x = 0;
        mb_y++;
      }
    }
  }

}
