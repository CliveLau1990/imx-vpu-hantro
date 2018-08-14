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
#include "regdrv_g1.h"
#include "deccfg.h"
#include "refbuffer.h"

#include <stdio.h>

#define DEC_X170_MODE_H264  (0)
#define DEC_X170_MODE_MPEG4 (1)
#define DEC_X170_MODE_H263  (2)
#define DEC_X170_MODE_VC1   (4)
#define DEC_X170_MODE_MPEG2 (5)
#define DEC_X170_MODE_MPEG1 (6)
#define DEC_X170_MODE_VP6   (7)
#define DEC_X170_MODE_RV    (8)
#define DEC_X170_MODE_VP7   (9)
#define DEC_X170_MODE_VP8   (10)
#define DEC_X170_MODE_AVS   (11)

#define THR_ADJ_MAX (8)
#define THR_ADJ_MIN (1)

#define MAX_DIRECT_MVS      (3000)

#ifndef DEC_X170_REFBU_ADJUST_VALUE
#define DEC_X170_REFBU_ADJUST_VALUE 130
#endif

/* Read direct MV from output memory; indexes i0..i2 used to
 * handle both big- and little-endian modes. */
#define DIR_MV_VER(p,i0,i1,i2) \
    ((((u32)(p[(i0)])) << 3) | (((u32)(p[(i1)])) >> 5) | (((u32)(p[(i2)] & 0x3) ) << 11 ))
#define DIR_MV_BE_VER(p) \
    ((((u32)(p[1])) << 3) | (((u32)(p[0])) >> 5) | (((u32)(p[2] & 0x3) ) << 11 ))
#define DIR_MV_LE_VER(p) \
    ((((u32)(p[2])) << 3) | (((u32)(p[3])) >> 5) | (((u32)(p[1] & 0x3) ) << 11 ))
#define SIGN_EXTEND(value, bits) (value) = (((value)<<(32-bits))>>(32-bits))

/* Distribution ranges and zero point */
#define VER_DISTR_MIN           (-256)
#define VER_DISTR_MAX           (255)
#define VER_DISTR_RANGE         (512)
#define VER_DISTR_ZERO_POINT    (256)
#define VER_MV_RANGE            (16)
#define HOR_CALC_WIDTH          (32)

/* macro to get absolute value */
#define ABS(a) (((a) < 0) ? -(a) : (a))

static const memAccess_t mem_stats_per_format[] = {
  { 307, 36, 150 }, /* H.264 (upped 20%) */
  { 236, 29, 112 }, /* MPEG-4 */
  { 228, 25, 92  }, /* H.263 (based on MPEG-2) */
  {   0,  0,   0 }, /* JPEG */
  { 240, 29, 112 }, /* VC-1 */
  { 302, 25, 92 },  /* MPEG-2 */
  { 228, 25, 92  }, /* MPEG-1 */
  { 240, 29, 112 }, /* AVS (based on VC-1) */
  { 240, 29, 112 }, /* VP6 (based on VC-1) */
  { 240, 29, 112 }, /* RVx (based on VC-1) */
  { 240, 29, 112 }, /* VP7 (based on VC-1) */
  { 240, 29, 112 }, /* VP8 (based on VC-1) */
  { 240, 29, 112 }  /* AVS (based on VC-1) */
};

static const i32 mb_data_per_format[][2] = {
  { 734, 880 },   /* H.264 (upped 20%) */
  { 464, 535 },   /* MPEG-4 */
  { 435, 486 },   /* H.263 (same as MPEG-2 used) */
  { 0, 0 },       /* JPEG   */
  { 533, 644 },   /* VC-1   */
  { 435, 486 },   /* MPEG-2 */
  { 435, 486 },   /* MPEG-1 */
  { 533, 486 },   /* AVS (based now on VC-1) */
  { 533, 486 },   /* VP6 (based now on VC-1) */
  { 533, 486 },   /* RVx (based on VC-1) */
  { 533, 486 },   /* VP7 (based now on VC-1) */
  { 533, 486 },   /* VP8 (based now on VC-1) */
  { 533, 486 }    /* AVS (based now on VC-1) */
};


static u32 GetSettings( struct refBuffer *p_refbu, i32 *p_x, i32 *p_y, u32 is_bpic,
                        u32 is_field_pic );
static void IntraFrame( struct refBuffer *p_refbu );

/*------------------------------------------------------------------------------
    Function name : UpdateMemModel
    Description   : Update memory model for reference buffer

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void UpdateMemModel( struct refBuffer *p_refbu ) {

  /* Variables */

  i32 width_in_mbs;
  i32 height_in_mbs;
  i32 latency;
  i32 nonseq;
  i32 seq;

  i32 bus_width;
  i32 tmp, tmp2;

  /* Code */

#define DIV_CEIL(a,b) (((a)+(b)-1)/(b))

  width_in_mbs  = p_refbu->pic_width_in_mbs;
  height_in_mbs = p_refbu->pic_height_in_mbs;
  bus_width    = p_refbu->bus_width_in_bits;

  tmp = bus_width >> 2;    /* initially buffered mbs per row */
  tmp2 = bus_width >> 3;   /* n:o of mbs buffered at refresh */
  tmp = ( 1 + DIV_CEIL(width_in_mbs - tmp, tmp2 ) ); /* latencies per mb row */
  tmp2 = 24 * height_in_mbs; /* Number of rows to buffer in total */
  /* Latencies per frame */
  latency = 2 * tmp * height_in_mbs;
  nonseq  = tmp2 * tmp;
  seq = ( DIV_CEIL( 16*width_in_mbs, bus_width>>3 ) ) * tmp2 -
        nonseq;

  p_refbu->num_cycles_for_buffering =
    latency * p_refbu->curr_mem_model.latency +
    nonseq * ( 1 + p_refbu->curr_mem_model.nonseq ) +
    seq * ( 1 + p_refbu->curr_mem_model.seq );

  p_refbu->buffer_penalty =
    p_refbu->mem_access_stats.nonseq +
    p_refbu->mem_access_stats.seq;
  if( bus_width == 32 )
    p_refbu->buffer_penalty >>= 1;

  p_refbu->avg_cycles_per_mb =
    ( ( p_refbu->mem_access_stats.latency * p_refbu->curr_mem_model.latency ) / 100 ) +
    p_refbu->mem_access_stats.nonseq * ( 1 + p_refbu->curr_mem_model.nonseq ) +
    p_refbu->mem_access_stats.seq * ( 1 + p_refbu->curr_mem_model.seq );

#ifdef REFBUFFER_TRACE
  printf("***** ref buffer mem model trace *****\n");
  printf("latency             = %7d clk\n", p_refbu->curr_mem_model.latency );
  printf("sequential          = %7d clk\n", p_refbu->curr_mem_model.nonseq );
  printf("non-sequential      = %7d clk\n", p_refbu->curr_mem_model.seq );

  printf("latency (mb)        = %7d\n", p_refbu->mem_access_stats.latency );
  printf("sequential (mb)     = %7d\n", p_refbu->mem_access_stats.nonseq );
  printf("non-sequential (mb) = %7d\n", p_refbu->mem_access_stats.seq );

  printf("bus-width           = %7d\n", bus_width );

  printf("buffering cycles    = %7d\n", p_refbu->num_cycles_for_buffering );
  printf("buffer penalty      = %7d\n", p_refbu->buffer_penalty );
  printf("avg cycles per mb   = %7d\n", p_refbu->avg_cycles_per_mb );

  printf("***** ref buffer mem model trace *****\n");
#endif /* REFBUFFER_TRACE */

#undef DIV_CEIL

}

/*------------------------------------------------------------------------------
    Function name : IntraFrame
    Description   : Clear history buffers on intra frames

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void IntraFrame( struct refBuffer *p_refbu ) {
  p_refbu->oy[0] = p_refbu->oy[1] = p_refbu->oy[2] = 0;
  p_refbu->num_intra_blk[0] =
    p_refbu->num_intra_blk[1] = p_refbu->num_intra_blk[2] = (-1);
  p_refbu->coverage[0] = /*4 * tmp;*/
    p_refbu->coverage[1] =
      p_refbu->coverage[2] = (-1); /* initial value */
}

/*------------------------------------------------------------------------------
    Function name : RefbuGetHitThreshold
    Description   :

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
i32 RefbuGetHitThreshold( struct refBuffer *p_refbu ) {

  i32 required_hits_clk = 0;
  i32 required_hits_data = 0;
  i32 divisor;
  i32 tmp;

#ifdef REFBUFFER_TRACE
  i32 pred_miss;
  printf("***** ref buffer threshold trace *****\n");
#endif /* REFBUFFER_TRACE */

  divisor = p_refbu->avg_cycles_per_mb - p_refbu->buffer_penalty;

  if( divisor > 0 )
    required_hits_clk = ( 4 * p_refbu->num_cycles_for_buffering ) / divisor;

  divisor = p_refbu->mb_weight;
#ifdef REFBUFFER_TRACE
  printf("mb weight           = %7d\n", divisor );
#endif /* REFBUFFER_TRACE */

  if( divisor > 0) {

    divisor = ( divisor * p_refbu->data_excess_max_pct ) / 100;

#ifdef REFBUFFER_TRACE
    pred_miss = 4 * p_refbu->frm_size_in_mbs - p_refbu->pred_intra_blk -
                p_refbu->pred_coverage;
    printf("predicted misses    = %7d\n", pred_miss );
    printf("data excess %%       = %7d\n", p_refbu->data_excess_max_pct );
    printf("divisor             = %7d\n", divisor );
#endif /* REFBUFFER_TRACE */

    /*tmp = (( DATA_EXCESS_MAX_PCT - 100 ) * p_refbu->mb_weight * pred_miss ) / 400;*/
    tmp = 0;
    required_hits_data = ( 4 * p_refbu->total_data_for_buffering - tmp);
    required_hits_data /= divisor;
  }

  if(p_refbu->pic_height_in_mbs) {
    required_hits_clk /= p_refbu->pic_height_in_mbs;
    required_hits_data /= p_refbu->pic_height_in_mbs;
  }

#ifdef REFBUFFER_TRACE
  printf("target (clk)        = %7d\n", required_hits_clk );
  printf("target (data)       = %7d\n", required_hits_data );
  printf("***** ref buffer threshold trace *****\n");

#endif /* REFBUFFER_TRACE */

  return required_hits_clk > required_hits_data ?
         required_hits_clk : required_hits_data;
}

/*------------------------------------------------------------------------------
    Function name : InitMemAccess
    Description   :

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void InitMemAccess( struct refBuffer *p_refbu, u32 dec_mode, u32 bus_width ) {
  /* Initialize stream format memory model */
  p_refbu->mem_access_stats = mem_stats_per_format[dec_mode];
  p_refbu->mem_access_stats_flag = 0;
  if( bus_width == 64 ) {
    p_refbu->mem_access_stats.seq >>= 1;
    p_refbu->mb_weight = mb_data_per_format[dec_mode][1];
  } else {
    p_refbu->mb_weight = mb_data_per_format[dec_mode][0];
  }
}

/*------------------------------------------------------------------------------
    Function name : RefbuInit
    Description   :

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void RefbuInit( struct refBuffer *p_refbu, u32 dec_mode, u32 pic_width_in_mbs,
                u32 pic_height_in_mbs, u32 support_flags ) {

  /* Variables */

  u32 tmp;
  u32 i;

  /* Code */

  /* Ignore init's if image size doesn't change. For example H264 SW may
   * call here when moving to RLC mode. */
  if( (u32)p_refbu->pic_width_in_mbs == pic_width_in_mbs &&
      (u32)p_refbu->pic_height_in_mbs == pic_height_in_mbs ) {
    return;
  }

#ifdef REFBUFFER_TRACE
  printf("***** ref buffer initialized to %dx%d *****\n",
         pic_width_in_mbs, pic_height_in_mbs);
#endif /* REFBUFFER_TRACE */

  p_refbu->dec_mode = dec_mode;
  p_refbu->pic_width_in_mbs   = pic_width_in_mbs;
  p_refbu->pic_height_in_mbs  = pic_height_in_mbs;
  tmp                     = pic_width_in_mbs * pic_height_in_mbs;
  p_refbu->frm_size_in_mbs    = tmp;

  p_refbu->total_data_for_buffering
    = p_refbu->frm_size_in_mbs*384;
  tmp                     = pic_width_in_mbs * ((pic_height_in_mbs + 1) / 2);
  p_refbu->fld_size_in_mbs    = tmp;

  p_refbu->offset_support = (support_flags & REFBU_SUPPORT_OFFSET) ? 1 : 0;
  p_refbu->interlaced_support = (support_flags & REFBU_SUPPORT_INTERLACED) ? 1 : 0;
  p_refbu->double_support = (support_flags & REFBU_SUPPORT_DOUBLE) ? 1 : 0;
  p_refbu->thr_adj = THR_ADJ_MAX;
  p_refbu->data_excess_max_pct = DEC_X170_REFBU_ADJUST_VALUE;

  p_refbu->pred_coverage = p_refbu->pred_intra_blk = 0;
  IntraFrame( p_refbu );
  if( dec_mode == DEC_X170_MODE_H264 ) {
    p_refbu->mvs_per_mb = 16;
    p_refbu->filter_size = 3;
  } else if ( dec_mode == DEC_X170_MODE_VC1 ) {
    p_refbu->mvs_per_mb = 2;
    p_refbu->filter_size = 2;
  } else {
    p_refbu->mvs_per_mb = 1;
    p_refbu->filter_size = 1;
  }

  /* Initialize buffer memory model */
  p_refbu->bus_width_in_bits          = DEC_X170_REFBU_WIDTH;
  p_refbu->curr_mem_model.latency    = DEC_X170_REFBU_LATENCY;
  p_refbu->curr_mem_model.nonseq     = DEC_X170_REFBU_NONSEQ;
  p_refbu->curr_mem_model.seq        = DEC_X170_REFBU_SEQ;
  p_refbu->prev_latency             = -1;
  p_refbu->num_cycles_for_buffering   = 0;

  for ( i = 0 ; i < 3 ; ++i ) {
    p_refbu->fld_hits_p[i][0] =
      p_refbu->fld_hits_p[i][1] =
        p_refbu->fld_hits_b[i][0] =
          p_refbu->fld_hits_b[i][1] = -1;
  }
  p_refbu->fld_cnt = 0;

  /* Initialize stream format memory model */
  InitMemAccess( p_refbu, dec_mode, DEC_X170_REFBU_WIDTH );

  p_refbu->dec_mode_mb_weights[0] = mb_data_per_format[dec_mode][0];
  p_refbu->dec_mode_mb_weights[1] = mb_data_per_format[dec_mode][1];
}


/*------------------------------------------------------------------------------
    Function name : GetSettings
    Description   : Determine whether or not to enable buffer, and calculate
                    buffer offset.

    Return type   : u32
    Argument      :
------------------------------------------------------------------------------*/
u32 GetSettings( struct refBuffer *p_refbu, i32 *p_x, i32 *p_y, u32 is_bpic,
                 u32 is_field_pic) {

  /* Variables */

  i32 tmp;
  u32 enable = HANTRO_TRUE;
  i32 frm_size_in_mbs;
  i32 cov;
  i32 sign;
  i32 num_cycles_for_buffering;

  /* Code */

  frm_size_in_mbs = p_refbu->frm_size_in_mbs;
  *p_x = 0;
  *p_y = 0;

  /* Disable automatically for pictures less than 16MB wide */
  if( p_refbu->pic_width_in_mbs <= 16 ) {
    return HANTRO_FALSE;
  }

  num_cycles_for_buffering = p_refbu->num_cycles_for_buffering;
  if(is_field_pic)
    num_cycles_for_buffering /= 2;

  if( p_refbu->prev_used_double ) {
    cov = p_refbu->coverage[0];
    tmp = p_refbu->avg_cycles_per_mb * cov / 4;

    /* Check whether it is viable to enable buffering */
    tmp = (2*num_cycles_for_buffering < tmp);
    if( !tmp ) {
      p_refbu->thr_adj -= 2;
      if ( p_refbu->thr_adj < THR_ADJ_MIN )
        p_refbu->thr_adj = THR_ADJ_MIN;
    } else {
      p_refbu->thr_adj++;
      if ( p_refbu->thr_adj > THR_ADJ_MAX )
        p_refbu->thr_adj = THR_ADJ_MAX;
    }
  }

  if( !is_bpic ) {
    if( p_refbu->coverage[1] != -1 ) {
      cov = (5*p_refbu->coverage[0] - 1*p_refbu->coverage[1])/4;
      if( p_refbu->coverage[2] != -1 ) {
        cov = cov + ( p_refbu->coverage[0] + p_refbu->coverage[1] + p_refbu->coverage[2] ) / 3;
        cov /= 2;
      }

    } else if ( p_refbu->coverage[0] != -1 ) {
      cov = p_refbu->coverage[0];
    } else {
      cov = 4*frm_size_in_mbs;
    }
  } else {
    cov = p_refbu->coverage[0];
    if( cov == -1 ) {
      cov = 4*frm_size_in_mbs;
    }
    /* MPEG-4 B-frames have no intra coding possibility, so extrapolate
     * hit rate to match it */
    else if( p_refbu->pred_intra_blk < 4*frm_size_in_mbs &&
             p_refbu->dec_mode == DEC_X170_MODE_MPEG4 ) {
      cov *= (128*4*frm_size_in_mbs) / (4*frm_size_in_mbs-p_refbu->pred_intra_blk) ;
      cov /= 128;
    }
    /* Assume that other formats have less intra MBs in B pictures */
    else {
      cov *= (128*4*frm_size_in_mbs) / (4*frm_size_in_mbs-p_refbu->pred_intra_blk/2) ;
      cov /= 128;
    }
  }
  if( cov < 0 )   cov = 0;
  p_refbu->pred_coverage = cov;

  /* Check whether it is viable to enable buffering */
  /* 1.criteria = cycles */
  tmp = p_refbu->avg_cycles_per_mb * cov / 4;
  num_cycles_for_buffering += p_refbu->buffer_penalty * cov / 4;
  enable = (num_cycles_for_buffering < tmp);
  /* 2.criteria = data */
  /*
  tmp = ( DATA_EXCESS_MAX_PCT * cov ) / 400;
  tmp = tmp * p_refbu->mb_weight;
  enable = enable && (p_refbu->total_data_for_buffering < tmp);
  */

#ifdef REFBUFFER_TRACE
  printf("***** ref buffer algorithm trace *****\n");
  printf("predicted coverage  = %7d\n", cov );
  printf("bus width in calc   = %7d\n", p_refbu->bus_width_in_bits );
  printf("coverage history    = %7d%7d%7d\n",
         p_refbu->coverage[0],
         p_refbu->coverage[1],
         p_refbu->coverage[2] );
  printf("enable              = %d (%8d<%8d)\n", enable, num_cycles_for_buffering, tmp );
#endif /* REFBUFFER_TRACE */

  /* If still enabled, calculate offsets */
  if( enable ) {
    /* Round to nearest 16 multiple */
    tmp = (p_refbu->oy[0] + p_refbu->oy[1] + 1)/2;
    sign = ( tmp < 0 );
    if( p_refbu->prev_was_field )
      tmp /= 2;
    tmp = ABS(tmp);
    tmp = tmp & ~15;
    if( sign )  tmp = -tmp;
    *p_y = tmp;
  }

#ifdef REFBUFFER_TRACE
  printf("offset_x            = %7d\n", *p_x );
  printf("offset_y            = %7d (%d %d)\n", *p_y, p_refbu->oy[0], p_refbu->oy[1] );
  printf("***** ref buffer algorithm trace *****\n");
#endif /* REFBUFFER_TRACE */


  return enable;
}

/*------------------------------------------------------------------------------
    Function name : BuildDistribution
    Description   :

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void BuildDistribution( u32 *p_distr_ver,
                        u32 *p_mv, i32 frm_size_in_mbs,
                        u32 mvs_per_mb,
                        u32 big_endian,
                        i32 *min_y, i32 *max_y ) {

  /* Variables */

  i32 mb;
  i32 ver;
  u8 * p_mv_tmp;
  i32 mvs;
  u32 skip_mv = mvs_per_mb*4;
  u32 multiplier = 4;
  u32 div = 2;

  /* Code */

  mvs = frm_size_in_mbs;
  /* Try to keep total n:o of mvs checked under MAX_DIRECT_MVS */
  if( mvs > MAX_DIRECT_MVS ) {
    while( mvs/div > MAX_DIRECT_MVS )
      div++;

    mvs /= div;
    skip_mv *= div;
    multiplier *= div;
  }

  p_mv_tmp = (u8*)p_mv;
  if( big_endian ) {
    for( mb = 0 ; mb < mvs ; ++mb ) {
      {
        ver         = DIR_MV_BE_VER(p_mv_tmp);
        SIGN_EXTEND(ver, 13);
        /* Cut fraction and saturate */
        /*lint -save -e702 */
        ver >>= 2;
        /*lint -restore */
        if( ver >= VER_DISTR_MIN && ver <= VER_DISTR_MAX ) {
          p_distr_ver[ver] += multiplier;
          if( ver < *min_y )    *min_y = ver;
          if( ver > *max_y )    *max_y = ver;
        }
      }
      p_mv_tmp += skip_mv; /* Skip all other blocks for macroblock */
    }
  } else {
    for( mb = 0 ; mb < mvs ; ++mb ) {
      {
        ver         = DIR_MV_LE_VER(p_mv_tmp);
        SIGN_EXTEND(ver, 13);
        /* Cut fraction and saturate */
        /*lint -save -e702 */
        ver >>= 2;
        /*lint -restore */
        if( ver >= VER_DISTR_MIN && ver <= VER_DISTR_MAX ) {
          p_distr_ver[ver] += multiplier;
          if( ver < *min_y )    *min_y = ver;
          if( ver > *max_y )    *max_y = ver;
        }
      }
      p_mv_tmp += skip_mv; /* Skip all other blocks for macroblock */
    }
  }
}


/*------------------------------------------------------------------------------
    Function name : DirectMvStatistics
    Description   :

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void DirectMvStatistics( struct refBuffer *p_refbu, u32 *p_mv, i32 num_intra_blk,
                         u32 big_endian ) {

  /* Variables */

  i32 * p_tmp;
  i32 frm_size_in_mbs;
  i32 i;
  i32 oy = 0;
  i32 best = 0;
  i32 sum;
  i32 mvs_per_mb;
  i32 min_y = VER_DISTR_MAX, max_y = VER_DISTR_MIN;

  /* Mv distributions per component*/
  u32  distr_ver[VER_DISTR_RANGE] = { 0 };
  u32 *p_distr_ver = distr_ver + VER_DISTR_ZERO_POINT;

  /* Code */

  mvs_per_mb = p_refbu->mvs_per_mb;

  if( p_refbu->prev_was_field )
    frm_size_in_mbs = p_refbu->fld_size_in_mbs;
  else
    frm_size_in_mbs = p_refbu->frm_size_in_mbs;

  if( num_intra_blk < 4*frm_size_in_mbs ) {
    BuildDistribution( p_distr_ver,
                       p_mv,
                       frm_size_in_mbs,
                       mvs_per_mb,
                       big_endian,
                       &min_y, &max_y );

    /* Fix Intra occurences */
    p_distr_ver[0] -= num_intra_blk;

#if 0 /* Use median for MV calculation */
    /* Find median */
    {
      tmp = (frm_size_in_mbs - num_intra_mbs) / 2;
      sum = 0;
      i = VER_DISTR_MIN;
      for( i = VER_DISTR_MIN ; sum < tmp ; i++ )
        sum += p_distr_ver[i];
      oy = i-1;

      /* Calculate coverage percent */
      best = 0;
      i = MAX( VER_DISTR_MIN, oy-VER_MV_RANGE );
      limit = MIN( VER_DISTR_MAX, oy+VER_MV_RANGE );
      for( ; i < limit ; ++i )
        best += p_distr_ver[i];
    }
    else
#endif
    {
      i32 y;
      i32 penalty;

      /* Initial sum */
      sum = 0;
      min_y += VER_DISTR_ZERO_POINT;
      max_y += VER_DISTR_ZERO_POINT;

      for( i = 0 ; i < 2*VER_MV_RANGE ; ++i ) {
        sum += distr_ver[i];
      }
      best = 0;
      /* Other sums */
      max_y -= 2*VER_MV_RANGE;
      for( i = 0 ; i < VER_DISTR_RANGE-2*VER_MV_RANGE-1 ; ++i ) {
        sum -= distr_ver[i];
        sum += distr_ver[2*VER_MV_RANGE+i];
        y = VER_DISTR_MIN+VER_MV_RANGE+i+1;
        if ( ABS(y) > 8 ) {
          penalty = ABS(y)-8;
          penalty = (frm_size_in_mbs*penalty)/16;
        } else {
          penalty = 0;
        }
        /*if( (ABS(y) & 15) == 0 )*/
        {
          if( sum - penalty > best ) {
            best = sum - penalty;
            oy = y;
          } else if ( sum - penalty == best ) {
            if( ABS(y) < ABS(oy) )  oy = y;
          }
        }
      }
    }

    if(p_refbu->prev_was_field)
      best *= 2;
    p_refbu->coverage[0] = best;

    /* Store updated offsets */
    p_tmp = p_refbu->oy;
    p_tmp[2] = p_tmp[1];
    p_tmp[1] = p_tmp[0];
    p_tmp[0] = oy;

  } else {
    p_tmp = p_refbu->oy;
    p_tmp[2] = p_tmp[1];
    p_tmp[1] = p_tmp[0];
    p_tmp[0] = 0;
  }

}

/*------------------------------------------------------------------------------
    Function name : RefbuMvStatisticsB
    Description   :

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void RefbuMvStatisticsB( struct refBuffer *p_refbu, u32 *reg_base ) {

  /* Variables */

  i32 top_fld_cnt;
  i32 bot_fld_cnt;

  /* Code */

  top_fld_cnt = GetDecRegister( reg_base, HWIF_REFBU_TOP_SUM );
  bot_fld_cnt = GetDecRegister( reg_base, HWIF_REFBU_BOT_SUM );

  if( p_refbu->fld_cnt >= 2 &&
      GetDecRegister( reg_base, HWIF_FIELDPIC_FLAG_E ) &&
      (top_fld_cnt || bot_fld_cnt)) {
    p_refbu->fld_hits_b[2][0] = p_refbu->fld_hits_b[1][0];
    p_refbu->fld_hits_b[2][1] = p_refbu->fld_hits_b[1][1];
    p_refbu->fld_hits_b[1][0] = p_refbu->fld_hits_b[0][0];
    p_refbu->fld_hits_b[1][1] = p_refbu->fld_hits_b[0][1];
    if( GetDecRegister( reg_base, HWIF_PIC_TOPFIELD_E ) ) {
      p_refbu->fld_hits_b[0][0] = top_fld_cnt;
      p_refbu->fld_hits_b[0][1] = bot_fld_cnt;
    } else {
      p_refbu->fld_hits_b[0][0] = bot_fld_cnt;
      p_refbu->fld_hits_b[0][1] = top_fld_cnt;
    }
  }
  if( GetDecRegister( reg_base, HWIF_FIELDPIC_FLAG_E ) )
    p_refbu->fld_cnt++;
}

/*------------------------------------------------------------------------------
    Function name : RefbuMvStatistics
    Description   :

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
void RefbuMvStatistics( struct refBuffer *p_refbu, u32 *reg_base,
                        u32 *p_mv, u32 direct_mvs_available, u32 is_intra_picture ) {

  /* Variables */

  i32 * p_tmp;
  i32 tmp;
  i32 num_intra_blk;
  i32 top_fld_cnt;
  i32 bot_fld_cnt;
  u32 big_endian;

  /* Code */

  if( is_intra_picture ) {
    /*IntraFrame( p_refbu );*/
    return; /* Clear buffers etc. ? */
  }

  if(p_refbu->prev_was_field && !p_refbu->interlaced_support)
    return;

#ifdef ASIC_TRACE_SUPPORT
  /* big endian */
  big_endian = 1;
#else
  /* Determine HW endianness; this affects how we read motion vector
   * map from HW. Map endianness is same as decoder output endianness */
  if( GetDecRegister( reg_base, HWIF_DEC_OUT_ENDIAN ) == 0 ) {
    /* big endian */
    big_endian = 1;
  } else {
    /* little endian */
    big_endian = 0;
  }
#endif

  if(!p_refbu->offset_support || 1) /* Note: offset always disabled for now,
                                     * so always disallow direct mv data */
  {
    direct_mvs_available = 0;
  }

  num_intra_blk = GetDecRegister( reg_base, HWIF_REFBU_INTRA_SUM );
  top_fld_cnt = GetDecRegister( reg_base, HWIF_REFBU_TOP_SUM );
  bot_fld_cnt = GetDecRegister( reg_base, HWIF_REFBU_BOT_SUM );

#ifdef REFBUFFER_TRACE
  printf("***** ref buffer mv statistics trace *****\n");
  printf("intra blocks        = %7d\n", num_intra_blk );
  printf("top fields          = %7d\n", top_fld_cnt );
  printf("bottom fields       = %7d\n", bot_fld_cnt );
#endif /* REFBUFFER_TRACE */

  if( p_refbu->fld_cnt > 0 &&
      GetDecRegister( reg_base, HWIF_FIELDPIC_FLAG_E ) &&
      (top_fld_cnt || bot_fld_cnt)) {
    p_refbu->fld_hits_p[2][0] = p_refbu->fld_hits_p[1][0];
    p_refbu->fld_hits_p[2][1] = p_refbu->fld_hits_p[1][1];
    p_refbu->fld_hits_p[1][0] = p_refbu->fld_hits_p[0][0];
    p_refbu->fld_hits_p[1][1] = p_refbu->fld_hits_p[0][1];
    if( GetDecRegister( reg_base, HWIF_PIC_TOPFIELD_E ) ) {
      p_refbu->fld_hits_p[0][0] = top_fld_cnt;
      p_refbu->fld_hits_p[0][1] = bot_fld_cnt;
    } else {
      p_refbu->fld_hits_p[0][0] = bot_fld_cnt;
      p_refbu->fld_hits_p[0][1] = top_fld_cnt;
    }
  }
  if( GetDecRegister( reg_base, HWIF_FIELDPIC_FLAG_E ) )
    p_refbu->fld_cnt++;

  p_refbu->coverage[2] = p_refbu->coverage[1];
  p_refbu->coverage[1] = p_refbu->coverage[0];
  if(direct_mvs_available) {
    DirectMvStatistics( p_refbu, p_mv, num_intra_blk, big_endian );
  } else if(p_refbu->offset_support) {
    i32 inter_mvs;
    i32 sum;
    sum = GetDecRegister( reg_base, HWIF_REFBU_Y_MV_SUM );
    SIGN_EXTEND( sum, 22 );
    inter_mvs = (4*p_refbu->frm_size_in_mbs - num_intra_blk)/4;
    if( p_refbu->prev_was_field )
      inter_mvs *= 2;
    if( inter_mvs == 0 )
      inter_mvs = 1;
    /* Require at least 50% mvs present to calculate reliable avg offset */
    if( inter_mvs * 50 >= p_refbu->frm_size_in_mbs ) {
      /* Store updated offsets */
      p_tmp = p_refbu->oy;
      p_tmp[2] = p_tmp[1];
      p_tmp[1] = p_tmp[0];
      p_tmp[0] = sum/inter_mvs;
    }
  }

  /* Read buffer hits from previous frame. If number of hits < threshold
   * for the frame, then we know that HW switched buffering off. */
  tmp = GetDecRegister( reg_base, HWIF_REFBU_HIT_SUM );
  p_refbu->prev_frame_hit_sum = tmp;
  if ( tmp >= p_refbu->checkpoint && p_refbu->checkpoint ) {
    if(p_refbu->prev_was_field)
      tmp *= 2;
    p_refbu->coverage[0] = tmp;

#ifdef REFBUFFER_TRACE
    printf("actual coverage     = %7d\n", tmp );
#endif /* REFBUFFER_TRACE */

  } else if(!direct_mvs_available) {
    /* Buffering was disabled for previous frame, no direct mv
     * data available either, so assume we got a bit more hits than
     * the frame before that */
    if( p_refbu->coverage[1] != -1 ) {
      p_refbu->coverage[0] = ( 4 *
                               p_refbu->pic_width_in_mbs *
                               p_refbu->pic_height_in_mbs + 5 * p_refbu->coverage[1] ) / 6;
    } else
      p_refbu->coverage[0] = p_refbu->frm_size_in_mbs * 4;

#ifdef REFBUFFER_TRACE
    printf("calculated coverage = %7d\n", p_refbu->coverage[0] );
#endif /* REFBUFFER_TRACE */

  } else {

#ifdef REFBUFFER_TRACE
    printf("estimated coverage  = %7d\n", p_refbu->coverage[0] );
#endif /* REFBUFFER_TRACE */

  }

  /* Store intra counts for rate prediction */
  p_tmp = p_refbu->num_intra_blk;
  p_tmp[2] = p_tmp[1];
  p_tmp[1] = p_tmp[0];
  p_tmp[0] = num_intra_blk;

  /* Predict number of intra mbs for next frame */
  if(p_tmp[2] != -1)           tmp = (p_tmp[0] + p_tmp[1] + p_tmp[2])/3;
  else if( p_tmp[1] != -1 )    tmp = (p_tmp[0] + p_tmp[1])/2;
  else                        tmp = p_tmp[0];
  p_refbu->pred_intra_blk = MIN( p_tmp[0], tmp );

#ifdef REFBUFFER_TRACE
  printf("predicted intra blk = %7d\n", p_refbu->pred_intra_blk );
  printf("***** ref buffer mv statistics trace *****\n");
#endif /* REFBUFFER_TRACE */


}

/*------------------------------------------------------------------------------
    Function name : DecideParityMode
    Description   : Setup reference buffer for next frame.

    Return type   :
    Argument      : p_refbu              Reference buffer descriptor
                    is_bframe            Is frame B-coded
------------------------------------------------------------------------------*/
u32 DecideParityMode( struct refBuffer *p_refbu, u32 is_bframe ) {

  /* Variables */

  i32 same, opp;

  /* Code */

  if( p_refbu->dec_mode != DEC_X170_MODE_H264 ) {
    /* Don't use parity mode for other formats than H264
     * for now */
    return 0;
  }


  /* Read history */
  if( is_bframe ) {
    same = p_refbu->fld_hits_b[0][0];
    /*
    if( p_refbu->fld_hits_b[1][0] >= 0 )
        same += p_refbu->fld_hits_b[1][0];
    if( p_refbu->fld_hits_b[2][0] >= 0 )
        same += p_refbu->fld_hits_b[2][0];
        */
    opp  = p_refbu->fld_hits_b[0][1];
    /*
    if( p_refbu->fld_hits_b[1][1] >= 0 )
        opp += p_refbu->fld_hits_b[1][1];
    if( p_refbu->fld_hits_b[2][0] >= 0 )
        opp += p_refbu->fld_hits_b[2][1];*/
  } else {
    same = p_refbu->fld_hits_p[0][0];
    /*
    if( p_refbu->fld_hits_p[1][0] >= 0 )
        same += p_refbu->fld_hits_p[1][0];
    if( p_refbu->fld_hits_p[2][0] >= 0 )
        same += p_refbu->fld_hits_p[2][0];
        */
    opp  = p_refbu->fld_hits_p[0][1];
    /*
    if( p_refbu->fld_hits_p[1][1] >= 0 )
        opp += p_refbu->fld_hits_p[1][1];
    if( p_refbu->fld_hits_p[2][0] >= 0 )
        opp += p_refbu->fld_hits_p[2][1];
        */
  }

  /* If not enough data yet, bail out */
  if( same == -1 || opp == -1 )
    return 0;

  if( opp*2 <= same )
    return 1;

  return 0;

}

/*------------------------------------------------------------------------------
    Function name : RefbuSetup
    Description   : Setup reference buffer for next frame.

    Return type   :
    Argument      : p_refbu              Reference buffer descriptor
                    reg_base             Pointer to SW/HW control registers
                    isInterlacedField
                    is_intra_frame        Is frame intra-coded (or IDR for H.264)
                    is_bframe            Is frame B-coded
                    refPicId            pic_id for reference picture, if
                                        applicable. For H.264 pic_id for
                                        nearest reference picture of L0.
------------------------------------------------------------------------------*/
void RefbuSetup( struct refBuffer *p_refbu, u32 *reg_base,
                 refbuMode_e mode,
                 u32 is_intra_frame,
                 u32 is_bframe,
                 u32 ref_pic_id0, u32 ref_pic_id1, u32 flags ) {

  /* Variables */

  i32 ox = 0, oy = 0;
  i32 enable;
  i32 tmp;
  i32 thr2 = 0;
  u32 feature_disable = 0;
  u32 use_adaptive_mode = 0;
  u32 use_double_mode = 0;
  u32 disable_checkpoint = 0;
  u32 multiple_ref_frames = 1;
  u32 multiple_ref_fields = 1;
  u32 force_adaptive_single = 1;
  u32 single_ref_field = 0;
  u32 pic0 = 0, pic1 = 0;

  /* Code */

  SetDecRegister(reg_base, HWIF_REFBU_THR, 0 );
  SetDecRegister(reg_base, HWIF_REFBU2_THR, 0 );
  SetDecRegister(reg_base, HWIF_REFBU_PICID, 0 );
  SetDecRegister(reg_base, HWIF_REFBU_Y_OFFSET, 0 );

  multiple_ref_frames = ( flags & REFBU_MULTIPLE_REF_FRAMES ) ? 1 : 0;
  disable_checkpoint = ( flags & REFBU_DISABLE_CHECKPOINT ) ? 1 : 0;
  force_adaptive_single = ( flags & REFBU_FORCE_ADAPTIVE_SINGLE ) ? 1 : 0;

  p_refbu->prev_was_field = (mode == REFBU_FIELD && !is_bframe);

  /* Check supported features */
  if(mode != REFBU_FRAME && !p_refbu->interlaced_support)
    feature_disable = 1;

  /* check if SW wants to disable refbuff for this frame */
  if(flags & REFBU_DISABLE)
    feature_disable = 1;

  if(!is_intra_frame && !feature_disable) {
    if((u32)p_refbu->prev_latency != p_refbu->curr_mem_model.latency) {
      UpdateMemModel( p_refbu );
      p_refbu->prev_latency = p_refbu->curr_mem_model.latency;
    }

    if(flags & REFBU_DONT_USE_STATS)
      enable = p_refbu->pic_width_in_mbs > 16 ? 1 : 0;
    else
      enable = GetSettings( p_refbu, &ox, &oy,
                            is_bframe, mode == REFBU_FIELD);

    tmp = RefbuGetHitThreshold( p_refbu );
    p_refbu->checkpoint = tmp;

    if( mode == REFBU_FIELD ) {
      tmp = DecideParityMode( p_refbu, is_bframe );
      SetDecRegister( reg_base, HWIF_REFBU_FPARMOD_E, tmp );
      if( !tmp ) {
        p_refbu->thr_adj = 1;
      }
    } else {
      p_refbu->thr_adj = 1;
    }

    SetDecRegister(reg_base, HWIF_REFBU_E, enable );
    if( enable ) {
      /* Figure out which features to enable */
      if( p_refbu->double_support ) {
        if( !is_bframe ) { /* P field/frame */
          if( mode == REFBU_FIELD ) {
            if( single_ref_field ) {
              /* Buffer only reference field given in ref_pic_id0 */
            } else if (multiple_ref_fields ) {
              /* Let's not try to guess */
              use_double_mode = 1;
              use_adaptive_mode = 1;
              p_refbu->checkpoint /= p_refbu->thr_adj;
              thr2 = p_refbu->checkpoint ;
            } else {
              /* Buffer both reference fields explicitly */
              use_double_mode = 1;
              p_refbu->checkpoint /= p_refbu->thr_adj;
              thr2 = p_refbu->checkpoint;
            }
          } else if (force_adaptive_single) {
            use_adaptive_mode = 1;
            use_double_mode = 0;
          } else if( multiple_ref_frames ) {
            use_adaptive_mode = 1;
            use_double_mode = 1;
            p_refbu->checkpoint /= p_refbu->thr_adj;
            thr2 = p_refbu->checkpoint;
          } else {
            /* Content to buffer just one ref pic */
          }
        } else { /* B field/frame */
          if( mode == REFBU_FIELD ) {
            /* Let's not try to guess */
            use_adaptive_mode = 1;
            use_double_mode = 1;
            p_refbu->checkpoint /= p_refbu->thr_adj;
            /*p_refbu->checkpoint /= 2;*/
            thr2 = p_refbu->checkpoint;
          } else if (!multiple_ref_frames ) {
            /* Buffer forward and backward pictures as given in
             * ref_pic_id0 and ref_pic_id1 */
            use_double_mode = 1;
            p_refbu->checkpoint /= p_refbu->thr_adj;
            thr2 = p_refbu->checkpoint;
          } else {
            /* Let's not try to guess */
            use_double_mode = 1;
            use_adaptive_mode = 1;
            p_refbu->checkpoint /= p_refbu->thr_adj;
            thr2 = p_refbu->checkpoint;
          }
        }
      } else { /* Just single buffering supported */
        if( !is_bframe ) { /* P field/frame */
          if( mode == REFBU_FIELD ) {
            use_adaptive_mode = 1;
          } else if (force_adaptive_single) {
            use_adaptive_mode = 1;
          } else if (multiple_ref_frames) {
            /*use_adaptive_mode = 1;*/
          } else {
          }
        } else { /* B field/frame */
          use_adaptive_mode = 1;
        }
      }

      if(!use_adaptive_mode) {
        pic0 = ref_pic_id0;
        if( use_double_mode ) {
          pic1 = ref_pic_id1;
        }
      }

      SetDecRegister(reg_base, HWIF_REFBU_EVAL_E, use_adaptive_mode );

      /* Calculate amount of hits required for first mb row */

      if ( mode == REFBU_MBAFF ) {
        p_refbu->checkpoint *= 2;
        thr2 *= 2;
      }

      if( use_double_mode ) {
        oy = 0; /* Disable offset */
      }

      /* Limit offset */
      {
        i32 limit, height;
        height = p_refbu->pic_height_in_mbs;
        if( mode == REFBU_FIELD )
          height /= 2; /* adjust to field height */

        if( mode == REFBU_MBAFF )
          limit = 64; /* 4 macroblock rows */
        else
          limit = 48; /* 3 macroblock rows */

        if( (i32)(oy+limit) > (i32)(height*16) )
          oy = height*16-limit;
        if( (i32)((-oy)+limit) > (i32)(height*16) )
          oy = -(height*16-limit);
      }

      /* Disable offset just to make sure */
      if(!p_refbu->offset_support || 1) /* NOTE: always disabled for now */
        oy = 0;

      if(!disable_checkpoint)
        SetDecRegister(reg_base, HWIF_REFBU_THR, p_refbu->checkpoint );
      else
        SetDecRegister(reg_base, HWIF_REFBU_THR, 0 );
      SetDecRegister(reg_base, HWIF_REFBU_PICID, pic0 );
      SetDecRegister(reg_base, HWIF_REFBU_Y_OFFSET, oy );

      if(p_refbu->double_support) {
        /* Very much TODO */
        SetDecRegister(reg_base, HWIF_REFBU2_BUF_E, use_double_mode );
        SetDecRegister(reg_base, HWIF_REFBU2_THR, thr2 );
        SetDecRegister(reg_base, HWIF_REFBU2_PICID, pic1 );
        p_refbu->prev_used_double = use_double_mode;
      }
    }
    p_refbu->prev_was_field = (mode == REFBU_FIELD && !is_bframe);
  } else {
    p_refbu->checkpoint = 0;
    SetDecRegister(reg_base, HWIF_REFBU_E, HANTRO_FALSE );
  }

  if(p_refbu->test_function) {
    p_refbu->test_function(p_refbu, reg_base, is_intra_frame, mode );
  }

}

/*------------------------------------------------------------------------------
    Function name : RefbuGetVpxCoveragePrediction
    Description   : Return coverage and intra block prediction for VPx
                    "intelligent" ref buffer control. Prediction algorithm
                    to be finalized

    Return type   :
    Argument      :
------------------------------------------------------------------------------*/
u32 RefbuVpxGetPrevFrameStats( struct refBuffer *p_refbu) {
  i32 cov, tmp;

  tmp = p_refbu->prev_frame_hit_sum;
  if ( tmp >= p_refbu->checkpoint && p_refbu->checkpoint )
    cov = tmp/4;
  else
    cov = 0;
  return cov;
}
