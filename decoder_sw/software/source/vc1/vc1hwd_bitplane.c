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

#include "vc1hwd_bitplane.h"
#include "vc1hwd_stream.h"
#include "vc1hwd_util.h"

/*------------------------------------------------------------------------------
    External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

#define INVALID_TILE_VLC    ((u16x)(-1))

/* 3x2 and 2x3 tiles - VLC length 8 bits -> t[n]
                       VLC length 13 bits -> use 63 - t[n] */
static const u16x code_tile_8[15] = {
  3, 5, 6, 9, 10, 12, 17, 18, 20, 24, 33, 34, 36, 40, 48
};

/* 3x2 and 2x3 tiles - VLC length 9 bits */
static const u16x code_tile_9[6] = {
  62, 61, 59, 55, 47, 31
};

/* 3x2 and 2x3 tiles - VLC length 10 bits */
static const u16x code_tile_10[26] = {
  35, INVALID_TILE_VLC, 37, 38, 7, INVALID_TILE_VLC, 41, 42, 11, 44, 13, 14,
  INVALID_TILE_VLC, INVALID_TILE_VLC, 49, 50, 19, 52, 21, 22,
  INVALID_TILE_VLC, 56, 25, 26, INVALID_TILE_VLC, 28
};

/*------------------------------------------------------------------------------
    Local function prototypes
------------------------------------------------------------------------------*/

static u16x DecodeTile( u32 bits, u32 *bits_used );
static void DecodeNormal2( strmData_t * const strm_data, i16x count,
                           u8 *p_data, const u16x bit );
static u16x DecodeNormal6( strmData_t * strm_data, const u16x col_mb,
                           const u16x row_mb, u8 *p_data,
                           const u16x bit, u16x invert );
static void DecodeDifferential2( strmData_t * const strm_data, i16x count,
                                 u8 *p_data, const u16x bit,
                                 const u16x col_mb );
static u16x DecodeDifferential6( strmData_t * strm_data, const u16x col_mb,
                                 const u16x row_mb, u8 *p_data,
                                 const u16x bit, u16x invert );

/*------------------------------------------------------------------------------
    Functions
------------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------

   Function: DecodeTile

        Functional description:
            Decode 2x3 or 3x2 tile code word.

        Inputs:
            strm_data        Pointer to stream data descriptor.

        Outputs:
            u16x            Tile code word [0, 63]

------------------------------------------------------------------------------*/
u16x DecodeTile( u32 bits, u32 *bits_used ) {

  /* Variables */

  /*u16x bits;*/
  u16x code;
  u16x len;
  u16x tmp;

  /* Code */

  /* Max VLC codeword size 13 bits */
  /*bits = vc1hwdShowBits( strm_data, 13 );*/
  bits = (bits << *bits_used);

  if( bits >= (4096U<<19) ) {
    len = 1;
    code = 0;
  } else {
    tmp = bits >> (8+19);
    if( tmp == 2 ) {
      /* 10-bit code */
      tmp = ( ( bits >> (3+19) ) & 31 ) - 3;
      len = 10;
      if( tmp <= 25 )
        code = code_tile_10[tmp];
      else
        code = INVALID_TILE_VLC;
    } else if ( tmp == 3 ) {
      tmp = (bits >> 19) & 255;
      if( tmp >= 128 ) {
        len = 6;
        code = 63;
      } else if( tmp >= 16 ) {
        /* 9-bit code */
        len = 9;
        tmp = ( tmp >> 4 ) - 2;
        if( tmp <= 5 )
          code = code_tile_9[tmp];
        else
          code = INVALID_TILE_VLC; /* Invalid VLC encountered */
      } else {
        /* 13-bit code */
        len = 13;
        if( tmp == 15 )
          code = INVALID_TILE_VLC; /* Invalid VLC encountered */
        else
          code = 63 - code_tile_8[ tmp ];
      }
    } else if( bits >= (512U<<19) ) {
      /* 4-bit code */
      tmp = bits >> (9+19);
      len = 4;
      code = 1 << ( tmp - 2 );
    } else {
      /* 8-bit code */
      len = 8;
      tmp = bits >> (5+19);
      if( tmp == 15 )
        code = INVALID_TILE_VLC; /* Invalid VLC encountered */
      else
        code = code_tile_8[ tmp ];
    }
  }

  /* Flush data */
  /*(void)vc1hwdFlushBits(strm_data, len);*/
  *bits_used += len;

  return code;
}

/*------------------------------------------------------------------------------

    Function name: DecodeNormal2

        Functional description:
            Decode Normal-2 mode bitplane.

        Inputs:
            strm_data        Pointer to stream data descriptor.
            count           Amount of bits to read. If < 0 then INVERT bit is
                            set and amount of bits is -count.
            bit             Bit value.

        Outputs:

------------------------------------------------------------------------------*/
void DecodeNormal2( strmData_t * const strm_data, i16x count, u8 *p_data,
                    const u16x bit ) {

  /* Variables */

  u32 tmp;
  u32 bits_used = 0;
  u32 bits = 0;

  /* Code */

  ASSERT( strm_data );
  ASSERT( p_data );
  ASSERT( count != 0 );
  ASSERT( bit != 0 );

  if ( count < 0 ) { /* INVERT = 1 */
    count = -count;
    /* Decode odd symbol */
    if( count & 1 ) {
      (*p_data++) |= ( vc1hwdGetBits( strm_data, 1 ) == 0 ) ? bit : 0;
      count--;
    }

    /*lint -save -e702 */
    count >>= 1; /* What follows are pairs */
    /*lint -restore */

    /* Decode subsequent symbol pairs */
    bits = vc1hwdShowBits( strm_data, 32 );
    while( count-- ) {
      tmp = ( bits << bits_used ); /* (u16x)vc1hwdShowBits(strm_data, 3);*/

      if( tmp < (4U<<29) ) {
        /* Codeword 0, symbols 1 and 1 */
        bits_used++;
        *p_data++ |= (u8)bit;
        *p_data++ |= (u8)bit;
      } else {
        if( tmp < (6U<<29) ) {
          /* Codeword 100 or 101 */
          tmp = (~tmp) >> 29;
          p_data[tmp & 1] |= (u8)bit;
          bits_used ++;
        }
        bits_used += 2;
        p_data += 2;
      }
      if( bits_used > 29 ) {
        (void)vc1hwdFlushBits( strm_data, bits_used );
        bits = vc1hwdShowBits( strm_data, 32 );
        bits_used = 0;
      }
    }
    (void)vc1hwdFlushBits( strm_data, bits_used );
  } else { /* INVERT = 0 */
    /* Decode odd symbol */
    if( count & 1 ) {
      *p_data++ |= ( vc1hwdGetBits( strm_data, 1 ) == 1 ) ? bit : 0;
      count--;
    }

    /*lint -save -e702 */
    count >>= 1; /* What follows are pairs */
    /*lint -restore */

    /* Decode subsequent symbol pairs */
    bits = vc1hwdShowBits( strm_data, 32 );
    while( count-- ) {
      tmp = ( bits << bits_used ); /* (u16x)vc1hwdShowBits(strm_data, 3);*/
      if( tmp < (4U<<29) ) {
        /* Codeword 0, symbols 0 and 0 */
        bits_used++;
        p_data += 2;
      } else {
        if( tmp >= (6U<<29) ) {
          /* Codeword 11, symbols 1 and 1 */
          bits_used += 2;
          *p_data++ |= (u8)bit;
          *p_data++ |= (u8)bit;
        } else {
          /* Codeword 100 or 101 */
          tmp >>= 29;
          bits_used += 3;
          p_data[tmp & 1] |= (u8)bit;
          p_data += 2;
        }
      }
      if( bits_used > 29 ) {
        (void)vc1hwdFlushBits( strm_data, bits_used );
        bits_used = 0;
        bits = vc1hwdShowBits( strm_data, 32 );
      }
    }
    (void)vc1hwdFlushBits( strm_data, bits_used );
  }
}


/*------------------------------------------------------------------------------

    Function name: DecodeDifferential2

        Functional description:
            Decode Differential-2 mode bitplane.

        Inputs:
            strm_data        Pointer to stream data descriptor.
            count           Amount of bits to read. If < 0 then INVERT bit is
                            set and amount of bits is -count.
            bit             Bit value.
            col_mb           Number of macroblock columns.

        Outputs:

------------------------------------------------------------------------------*/
void DecodeDifferential2( strmData_t * const strm_data, i16x count,
                          u8 *p_data, const u16x bit,
                          const u16x col_mb ) {

  /* Variables */

  u16x tmp, k, i, j;
  u16x a; /* predictor 'A' in standard */
  u8 *p_tmp;
  u32 bits_used = 0;
  u32 bits = 0;

  /* Code */

  ASSERT( strm_data );
  ASSERT( p_data );
  ASSERT( count != 0 );
  ASSERT( bit != 0 );

  if( count < 0 ) {
    a = bit;
    count = -count;
  } else {
    a = 0 ;
  }
  p_tmp = p_data;
  k = (u16x)count;

  /* Decode odd symbol */
  if( count & 1 ) {
    *p_tmp++ |= ( vc1hwdGetBits( strm_data, 1 ) == 1 ) ? bit : 0;
    k--;
  }

  k >>= 1; /* What follows are pairs */

  /* Decode subsequent symbol pairs */
  bits = vc1hwdShowBits( strm_data, 32 );
  while( k-- ) {
    tmp = (bits << bits_used ); /* (u16x) vc1hwdShowBits( strm_data, 3 );*/
    if( tmp < (4U<<29) ) {
      /* Codeword 0, symbols 0 and 0 */
      bits_used++;
      p_tmp += 2;
    } else {
      if( tmp >= (6U<<29) ) {
        /* Codeword 11, symbols 1 and 1 */
        bits_used += 2;
        *p_tmp++ |= (u8)bit;
        *p_tmp++ |= (u8)bit;
      } else {
        /* Codeword 100 or 101 */
        tmp >>= 29;
        p_tmp[tmp & 1] |= (u8)bit;
        bits_used += 3;
        p_tmp += 2;
      }
    }
    if( bits_used > 29 ) {
      (void)vc1hwdFlushBits( strm_data, bits_used );
      bits = vc1hwdShowBits( strm_data, 32 );
      bits_used = 0;
    }
  }
  (void)vc1hwdFlushBits( strm_data, bits_used );

  /* Perform differential operation */
  p_tmp = p_data;
  k = (u16x)count-1;
  i = 1;
  j = 0;
  *p_tmp ^= (u8)a;
  while( k-- ) {
    if( i == col_mb ) {
      i = 0;
      j++;
    }
    if( i == 0 )
      tmp = *(p_tmp-col_mb+1) & bit;
    else if ( j > 0 && ( ( *(p_tmp-col_mb+1) ^ *p_tmp) & bit ) )
      tmp = a;
    else
      tmp = *p_tmp & bit;
    p_tmp++;
    *p_tmp ^= (u8)tmp;
    i++;
  }
}


/*------------------------------------------------------------------------------

    Function name: DecodeNormal6

        Functional description:
            Decode Normal-6 mode bitplane.

        Inputs:
            strm_data        Pointer to stream data descriptor.

        Outputs:

------------------------------------------------------------------------------*/
u16x DecodeNormal6( strmData_t * const strm_data, const u16x col_mb,
                    const u16x row_mb, u8 *p_data, const u16x bit,
                    u16x invert ) {

  /* Variables */

  u16x tmp, k, i;
  u16x tile_w, tile_h; /* width in tiles */
  u16x rowskip, colskip;
  u16x rv = HANTRO_OK;
  u8 * p_tmp;
  u32 bits;
  u32 bits_used;

  /* Code */

  ASSERT( strm_data );
  ASSERT( p_data );

  bits = vc1hwdShowBits( strm_data, 32 );
  bits_used = 0;

  if(!invert) {
    tmp = col_mb % 3;
    if( ( row_mb % 3 ) == 0 && tmp > 0 ) { /* 2x3 tiles */
      tile_w = col_mb >> 1;
      tile_h = row_mb / 3;
      i = 0;
      colskip = col_mb & 1;
      rowskip = 0;
      p_tmp = p_data + colskip;
      bits = vc1hwdShowBits( strm_data, 32 );
      bits_used = 0;
      while( tile_h ) {
        /* Decode next tile codeword */
        k = DecodeTile( bits, &bits_used );
        if( bits_used > 19 ) {
          (void)vc1hwdFlushBits( strm_data, bits_used );
          bits = vc1hwdShowBits( strm_data, 32 );
          bits_used = 0;
        }
        if( k == INVALID_TILE_VLC ) {
          /* Error handling */
          EPRINT(("DecodeNormal6: Invalid VLC code word encountered.\n"));
          rv = HANTRO_NOK;
          k = 0;
        }
        /* Process 2x3 tile */
        p_tmp[0] |= (u8)k & 1 ? bit : 0;
        p_tmp[col_mb] |= (u8)k & 4 ? bit : 0;
        p_tmp[2*col_mb] |= (u8)k & 16 ? bit : 0;
        p_tmp++;
        p_tmp[0] |= (u8)k & 2 ? bit : 0;
        p_tmp[col_mb] |= (u8)k & 8 ? bit : 0;
        p_tmp[2*col_mb] |= (u8)k & 32 ? bit : 0;
        p_tmp++;
        /* Skip to next tile row */
        if( ++i == tile_w ) {
          p_tmp += 2*col_mb + colskip;
          i = 0;
          tile_h--;
        }
      }
    } else { /* 3x2 tiles */
      tile_w = col_mb / 3;
      tile_h = row_mb >> 1;
      i = 0;
      colskip = tmp;
      rowskip = row_mb & 1;
      p_tmp = p_data + colskip + rowskip*col_mb;
      while( tile_h ) {
        /* Decode next tile codeword */
        k = DecodeTile( bits, &bits_used );
        if( bits_used > 19 ) {
          (void)vc1hwdFlushBits( strm_data, bits_used );
          bits = vc1hwdShowBits( strm_data, 32 );
          bits_used = 0;
        }
        if( k == INVALID_TILE_VLC ) {
          /* Error handling */
          EPRINT(("DecodeNormal6: Invalid VLC code word encountered.\n"));
          rv = HANTRO_NOK;
          k = 0;
        }
        /* Process 3x2 tile */
        p_tmp[0] |= (u8)k & 1 ? bit : 0;
        p_tmp[col_mb] |= (u8)k & 8 ? bit : 0;
        p_tmp++;
        p_tmp[0] |= (u8)k & 2 ? bit : 0;
        p_tmp[col_mb] |= (u8)k & 16 ? bit : 0;
        p_tmp++;
        p_tmp[0] |= (u8)k & 4 ? bit : 0;
        p_tmp[col_mb] |= (u8)k & 32 ? bit : 0;
        p_tmp++;
        /* Skip to next tile row */
        if( ++i == tile_w ) {
          p_tmp += col_mb + colskip;
          i = 0;
          tile_h--;
        }
      }
    }

    (void)vc1hwdFlushBits( strm_data, bits_used );

    /* Process column-skip tiles */
    if( colskip > 0 ) {
      DPRINT(("Col-skip %d\n", colskip));
      for( i = 0 ; i < colskip ; ++i ) {
        if( vc1hwdGetBits( strm_data, 1 ) == 0 ) {
          /* Process inverted columns */
          if(invert) {
            k = row_mb;
            p_tmp = p_data + i;
            while( k-- ) {
              *p_tmp |= (u8)bit;
              p_tmp += col_mb;
            }
          }
          continue;
        }
        p_tmp = p_data + i;
        k = row_mb >> 2;
        while ( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 4 );
          *p_tmp |= (u8)tmp & 8 ? bit : 0;
          p_tmp += col_mb;
          *p_tmp |= (u8)tmp & 4 ? bit : 0;
          p_tmp += col_mb;
          *p_tmp |= (u8)tmp & 2 ? bit : 0;
          p_tmp += col_mb;
          *p_tmp |= (u8)tmp & 1 ? bit : 0;
          p_tmp += col_mb;
        }
        k = row_mb & 3;
        while ( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 1 ) ? bit : 0;
          *p_tmp |= (u8)tmp;
          p_tmp += col_mb;
        }
      }
    }

    /* Process row-skip tiles */
    if( rowskip > 0 ) {
      DPRINT(("Row-skip %d\n", rowskip));
      ASSERT( rowskip == 1 );
      if( vc1hwdGetBits( strm_data, 1 ) == 1 ) {
        p_tmp = p_data + colskip;
        colskip = col_mb - colskip;
        k = colskip >> 2;
        while ( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 4 );
          *p_tmp |= (u8)tmp & 8 ? bit : 0;
          p_tmp++;
          *p_tmp |= (u8)tmp & 4 ? bit : 0;
          p_tmp++;
          *p_tmp |= (u8)tmp & 2 ? bit : 0;
          p_tmp++;
          *p_tmp |= (u8)tmp & 1 ? bit : 0;
          p_tmp++;
        }
        k = colskip & 3;
        while( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 1 ) ? bit : 0;
          *p_tmp |= (u8)tmp;
          p_tmp++;
        }
      } else {
        /* Process inverted row */
        if( invert ) {
          k = col_mb - colskip;
          p_tmp = p_data + colskip;
          while(k--) {
            *p_tmp |= (u8)bit;
            p_tmp++;
          }
        }
      }
    }
  } else {
    tmp = col_mb % 3;
    if( ( row_mb % 3 ) == 0 && tmp > 0 ) { /* 2x3 tiles */
      tile_w = col_mb >> 1;
      tile_h = row_mb / 3;
      i = 0;
      colskip = col_mb & 1;
      rowskip = 0;
      p_tmp = p_data + colskip;
      bits = vc1hwdShowBits( strm_data, 32 );
      bits_used = 0;
      while( tile_h ) {
        /* Decode next tile codeword */
        k = DecodeTile( bits, &bits_used );
        if( bits_used > 19 ) {
          (void)vc1hwdFlushBits( strm_data, bits_used );
          bits = vc1hwdShowBits( strm_data, 32 );
          bits_used = 0;
        }
        if( k == INVALID_TILE_VLC ) {
          /* Error handling */
          EPRINT(("DecodeNormal6: Invalid VLC code word encountered.\n"));
          rv = HANTRO_NOK;
          k = 0;
        }
        /* Process 2x3 tile */
        p_tmp[0] |= (u8)k & 1 ? 0 : bit;
        p_tmp[col_mb] |= (u8)k & 4 ? 0 : bit;
        p_tmp[2*col_mb] |= (u8)k & 16 ? 0 : bit;
        p_tmp++;
        p_tmp[0] |= (u8)k & 2 ? 0 : bit;
        p_tmp[col_mb] |= (u8)k & 8 ? 0 : bit;
        p_tmp[2*col_mb] |= (u8)k & 32 ? 0 : bit;
        p_tmp++;
        /* Skip to next tile row */
        if( ++i == tile_w ) {
          p_tmp += 2*col_mb + colskip;
          i = 0;
          tile_h--;
        }
      }
    } else { /* 3x2 tiles */
      tile_w = col_mb / 3;
      tile_h = row_mb >> 1;
      i = 0;
      colskip = tmp;
      rowskip = row_mb & 1;
      p_tmp = p_data + colskip + rowskip*col_mb;
      while( tile_h ) {
        /* Decode next tile codeword */
        k = DecodeTile( bits, &bits_used );
        if( bits_used > 19 ) {
          (void)vc1hwdFlushBits( strm_data, bits_used );
          bits = vc1hwdShowBits( strm_data, 32 );
          bits_used = 0;
        }
        if( k == INVALID_TILE_VLC ) {
          /* Error handling */
          EPRINT(("DecodeNormal6: Invalid VLC code word encountered.\n"));
          rv = HANTRO_NOK;
          k = 0;
        }
        /* Process 3x2 tile */
        p_tmp[0] |= (u8)k & 1 ? 0 : bit;
        p_tmp[col_mb] |= (u8)k & 8 ? 0 : bit;
        p_tmp++;
        p_tmp[0] |= (u8)k & 2 ? 0 : bit;
        p_tmp[col_mb] |= (u8)k & 16 ? 0 : bit;
        p_tmp++;
        p_tmp[0] |= (u8)k & 4 ? 0 : bit;
        p_tmp[col_mb] |= (u8)k & 32 ? 0 : bit;
        p_tmp++;
        /* Skip to next tile row */
        if( ++i == tile_w ) {
          p_tmp += col_mb + colskip;
          i = 0;
          tile_h--;
        }
      }
    }

    (void)vc1hwdFlushBits( strm_data, bits_used );

    /* Process column-skip tiles */
    if( colskip > 0 ) {
      DPRINT(("Col-skip %d\n", colskip));
      for( i = 0 ; i < colskip ; ++i ) {
        if( vc1hwdGetBits( strm_data, 1 ) == 0 ) {
          /* Process inverted columns */
          if(invert) {
            k = row_mb;
            p_tmp = p_data + i;
            while( k-- ) {
              *p_tmp |= (u8)bit;
              p_tmp += col_mb;
            }
          }
          continue;
        }
        p_tmp = p_data + i;
        k = row_mb >> 2;
        while ( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 4 );
          *p_tmp |= (u8)tmp & 8 ? 0 : bit;
          p_tmp += col_mb;
          *p_tmp |= (u8)tmp & 4 ? 0 : bit;
          p_tmp += col_mb;
          *p_tmp |= (u8)tmp & 2 ? 0 : bit;
          p_tmp += col_mb;
          *p_tmp |= (u8)tmp & 1 ? 0 : bit;
          p_tmp += col_mb;
        }
        k = row_mb & 3;
        while ( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 1 ) ? 0 : bit;
          *p_tmp |= (u8)tmp;
          p_tmp += col_mb;
        }
      }
    }

    /* Process row-skip tiles */
    if( rowskip > 0 ) {
      DPRINT(("Row-skip %d\n", rowskip));
      ASSERT( rowskip == 1 );
      if( vc1hwdGetBits( strm_data, 1 ) == 1 ) {
        p_tmp = p_data + colskip;
        colskip = col_mb - colskip;
        k = colskip >> 2;
        while ( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 4 );
          *p_tmp |= (u8)tmp & 8 ? 0 : bit;
          p_tmp++;
          *p_tmp |= (u8)tmp & 4 ? 0 : bit;
          p_tmp++;
          *p_tmp |= (u8)tmp & 2 ? 0 : bit;
          p_tmp++;
          *p_tmp |= (u8)tmp & 1 ? 0 : bit;
          p_tmp++;
        }
        k = colskip & 3;
        while( k-- ) {
          tmp = vc1hwdGetBits( strm_data, 1 ) ? 0 : bit;
          *p_tmp |= (u8)tmp;
          p_tmp++;
        }
      } else {
        /* Process inverted row */
        if( invert ) {
          k = col_mb - colskip;
          p_tmp = p_data + colskip;
          while(k--) {
            *p_tmp |= (u8)bit;
            p_tmp++;
          }
        }
      }
    }
  }

  return rv;
}


/*------------------------------------------------------------------------------

    Function name: DecodeDifferential6

        Functional description:
            Decode Differential-6 mode bitplane.

        Inputs:
            strm_data        Pointer to stream data descriptor.
            col_mb           Number of macroblock columns.
            row_mb           Number of macroblock rows.
            mbs             Macroblock array.
            bit             Bit to set.
            invert          INVERT bit

        Outputs:

------------------------------------------------------------------------------*/
u16x DecodeDifferential6( strmData_t * const strm_data, const u16x col_mb,
                          const u16x row_mb, u8 *p_data,
                          const u16x bit, const u16x invert ) {

  /* Variables */

  u16x tmp, tmp2, k, i;
  u16x tile_w, tile_h; /* width in tiles */
  u16x rowskip, colskip;
  u16x rv = HANTRO_OK;
  u16x a; /* predictor 'A' in standard */
  u8 * p_tmp;
  u32 bits;
  u32 bits_used;

  /* Code */

  ASSERT( strm_data );
  ASSERT( p_data );

  if(invert) a = bit;
  else a = 0;

  bits = vc1hwdShowBits( strm_data, 32 );
  bits_used = 0;

  tmp = col_mb % 3;
  if( ( row_mb % 3 ) == 0 && tmp > 0 ) { /* 2x3 tiles */
    tile_w = col_mb >> 1;
    tile_h = row_mb / 3;
    i = 0;
    colskip = col_mb & 1;
    rowskip = 0;
    p_tmp = p_data + colskip;
    while( tile_h ) {
      /* Decode next tile codeword */
      k = DecodeTile( bits, &bits_used );
      if( bits_used > (32-13) ) {
        (void)vc1hwdFlushBits( strm_data, bits_used );
        bits = vc1hwdShowBits( strm_data, 32 );
        bits_used = 0;
      }
      if( k == INVALID_TILE_VLC ) {
        /* Error handling */
        EPRINT(("DecodeDifferential6: Invalid VLC code "
                "word encountered.\n"));
        rv = HANTRO_NOK;
        k = 0;
      }
      /* Process 2x3 tile */
      tmp = k & 1 ? bit : 0;
      p_tmp[0] |= (u8)tmp;
      tmp = k & 4 ? bit : 0;
      p_tmp[col_mb] |= (u8)tmp;
      tmp = k & 16 ? bit : 0;
      p_tmp[2*col_mb] |= (u8)tmp;
      p_tmp++;
      tmp = k & 2 ? bit : 0;
      p_tmp[0] |= (u8)tmp;
      tmp = k & 8 ? bit : 0;
      p_tmp[col_mb] |= (u8)tmp;
      tmp = k & 32 ? bit : 0;
      p_tmp[2*col_mb] |= (u8)tmp;
      p_tmp++;
      /* Skip to next tile row */
      if( ++i == tile_w ) {
        p_tmp += 2*col_mb + colskip;
        i = 0;
        tile_h--;
      }
    }
  } else { /* 3x2 tiles */
    tile_w = col_mb / 3;
    tile_h = row_mb >> 1;
    i = 0;
    colskip = tmp;
    rowskip = row_mb & 1;
    p_tmp = p_data + colskip + rowskip*col_mb;
    while( tile_h ) {
      /* Decode next tile codeword */
      k = DecodeTile( bits, &bits_used );
      if( bits_used > (32-13) ) {
        (void)vc1hwdFlushBits( strm_data, bits_used );
        bits = vc1hwdShowBits( strm_data, 32 );
        bits_used = 0;
      }
      if( k == INVALID_TILE_VLC ) {
        /* Error handling */
        EPRINT(("DecodeDifferential6: Invalid VLC code "
                "word encountered.\n"));
        rv = HANTRO_NOK;
        k = 0;
      }
      /* Process 3x2 tile */
      tmp = k & 1 ? bit : 0;
      p_tmp[0] |= (u8)tmp;
      tmp = k & 8 ? bit : 0;
      p_tmp[col_mb] |= (u8)tmp;
      p_tmp++;
      tmp = k & 2 ? bit : 0;
      p_tmp[0] |= (u8)tmp;
      tmp = k & 16 ? bit : 0;
      p_tmp[col_mb] |= (u8)tmp;
      p_tmp++;
      tmp = k & 4 ? bit : 0;
      p_tmp[0] |= (u8)tmp;
      tmp = k & 32 ? bit : 0;
      p_tmp[col_mb] |= (u8)tmp;
      p_tmp++;
      /* Skip to next tile row */
      if( ++i == tile_w ) {
        p_tmp += col_mb + colskip;
        i = 0;
        tile_h--;
      }
    }
  }

  (void)vc1hwdFlushBits( strm_data, bits_used );

  /* Process column-skip tiles */
  if( colskip > 0 ) {
    DPRINT(("Col-skip %d\n", colskip));
    for( i = 0 ; i < colskip ; ++i ) {
      if( vc1hwdGetBits( strm_data, 1 ) == 0 ) {
        continue;
      }
      p_tmp = p_data + i;
      k = row_mb >> 2;
      while ( k-- ) {
        tmp = vc1hwdGetBits( strm_data, 4 );
        tmp2 = tmp & 8 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp += col_mb;
        tmp2 = tmp & 4 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp += col_mb;
        tmp2 = tmp & 2 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp += col_mb;
        tmp2 = tmp & 1 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp += col_mb;
      }
      k = row_mb & 3;
      while ( k-- ) {
        tmp = vc1hwdGetBits( strm_data, 1 ) ? bit : 0;
        *p_tmp |= (u8)tmp;
        p_tmp += col_mb;
      }
    }
  }

  /* Process row-skip tiles */
  if( rowskip > 0 ) {
    DPRINT(("Row-skip %d\n", rowskip));
    ASSERT( rowskip == 1 );
    if( vc1hwdGetBits( strm_data, 1 ) == 1 ) {
      p_tmp = p_data + colskip;
      colskip = col_mb - colskip;
      k = colskip >> 2;
      while ( k-- ) {
        tmp = vc1hwdGetBits( strm_data, 4 );
        tmp2 = tmp & 8 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp++;
        tmp2 = tmp & 4 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp++;
        tmp2 = tmp & 2 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp++;
        tmp2 = tmp & 1 ? bit : 0;
        *p_tmp |= (u8)tmp2;
        p_tmp++;
      }
      k = colskip & 3;
      while( k-- ) {
        tmp = vc1hwdGetBits( strm_data, 1 ) ? bit : 0;
        *p_tmp |= (u8)tmp;
        p_tmp++;
      }
    }
  }

  /* Perform differential operation */
  p_tmp = p_data;
  k = (u16x)(row_mb*col_mb)-1;
  i = 1;
  tmp2 = 0;
  *p_tmp ^= (u8)a;
  while( k-- ) {
    if( i == col_mb ) {
      i = 0;
      tmp2++;
    }
    if( i == 0 )
      tmp = *(p_tmp-col_mb+1) & bit;
    else if ( tmp2 > 0 &&
              ( ( *(p_tmp-col_mb+1) ^ *p_tmp) & bit ) )
      tmp = a;
    else
      tmp = *p_tmp & bit;
    p_tmp++;
    *p_tmp ^= tmp;
    i++;
  }

  return rv;
}

/*------------------------------------------------------------------------------

    Function name: vc1hwdDecodeBitPlane

        Functional description:
            Decode bitplane.

        Inputs:
            strm_data        Pointer to stream data descriptor.
            col_mb           Number of macroblock columns.
            row_mb           Number of macroblock rows.
            p_data
            bit             Bit value to set.
            p_raw_mask        Mask to indicate if bits are raw-coded.
            maskbit         Bit value to set in the rawmask.

        Outputs
            mbs             Bitplane, if coding mode other than raw is used.
            p_raw_mask        Respective invert and raw bits will be set if
                            bitplane uses raw coding mode.

        Returns:
            HANTRO_OK       Bitplane decoded successfully.
            HANTRO_NOK      There was an error in decoding.

------------------------------------------------------------------------------*/
u16x vc1hwdDecodeBitPlane( strmData_t * const strm_data, const u16x col_mb,
                           const u16x row_mb, u8 *p_data, const u16x bit,
                           u16x * const p_raw_mask,  const u16x maskbit,
                           const u16x sync_marker ) {

  /* Variables */

  u16x invert = 0;
  u16x count;
  u16x tmp, tmp2, tmp3, tmp4;
  u16x len;
  u16x rv = HANTRO_OK;
  BitPlaneCodingMode_e imode;
  static const BitPlaneCodingMode_e bpcm_table[3] = {
    BPCM_DIFFERENTIAL_2, BPCM_ROW_SKIP, BPCM_COLUMN_SKIP
  };
  i16x tmp5;
#if defined(_DEBUG_PRINT)
  u8 *p_data_tmp = p_data;
#endif

  /* Code */

  ASSERT( strm_data );
  ASSERT( p_data );
  ASSERT( p_raw_mask );

  /* Decode INVERT and IMODE */
  tmp = vc1hwdShowBits( strm_data, 5 );
  if( tmp >= 16 ) {
    tmp -= 16;
    invert = 1;
    DPRINT(("INVERT=1\n"));
  }
  if( tmp >= 8 ) {
    len = 3;
    if( tmp >= 12 )
      imode = BPCM_NORMAL_6;
    else
      imode = BPCM_NORMAL_2;
  } else if ( tmp >= 2 ) {
    tmp = (tmp >> 1) - 1;
    len = 4;
    imode = bpcm_table[tmp];
  } else if ( tmp ) {
    len = 5;
    imode = BPCM_DIFFERENTIAL_6;
  } else {
    len = 5;
    imode = BPCM_RAW;
  }
  (void)vc1hwdFlushBits( strm_data, len );

  /* Decode DATABITS */
  if( imode == BPCM_NORMAL_2 ) {
    if(invert) tmp5 = - (i16x)(row_mb*col_mb);
    else tmp5 = (i16x)(row_mb*col_mb);
    DecodeNormal2( strm_data, tmp5, p_data, bit );
  } else if ( imode == BPCM_DIFFERENTIAL_2 ) {
    if(invert) tmp5 = - (i16x)(row_mb*col_mb);
    else tmp5 = (i16x)(row_mb*col_mb);
    DecodeDifferential2( strm_data, tmp5, p_data, bit, col_mb);
  } else if ( imode == BPCM_NORMAL_6 ) {
    rv = DecodeNormal6( strm_data, col_mb, row_mb, p_data, bit, invert );
  } else if ( imode == BPCM_DIFFERENTIAL_6 ) {
    rv = DecodeDifferential6( strm_data, col_mb, row_mb, p_data, bit, invert );
  } else { /* use in-line decoding */
    invert *= bit;
    if ( imode == BPCM_ROW_SKIP ) { /* Row-skip mode */
      tmp = row_mb;
      while(tmp--) {
        /* Read ROWSKIP element */
        if(vc1hwdGetBits(strm_data, 1)) {
          /* read ROWBITS */
          count = col_mb;
          tmp2 = count >> 2;
          /* Read 4 bit chunks */
          while( tmp2-- ) {
            tmp3 = vc1hwdGetBits( strm_data, 4 );
            tmp4 = tmp3 & 8 ? bit : 0;
            *p_data++ |= (u8)(tmp4 ^ invert);
            tmp4 = tmp3 & 4 ? bit : 0;
            *p_data++ |= (u8)(tmp4 ^ invert);
            tmp4 = tmp3 & 2 ? bit : 0;
            *p_data++ |= (u8)(tmp4 ^ invert);
            tmp4 = tmp3 & 1 ? bit : 0;
            *p_data++ |= (u8)(tmp4 ^ invert);
          }
          /* Read remainder */
          for( tmp2 = count & 3 ; tmp2 ; tmp2-- ) {
            *p_data++ |=
              ((vc1hwdGetBits( strm_data, 1)) ? bit : 0 ) ^ invert;
          }
        } else {
          /* skip row */
          if(invert)
            for( tmp3 = col_mb ; tmp3-- ; )
              *p_data++ |= (u8)invert;
          else
            p_data += col_mb;
        }
      }
    } else if ( imode == BPCM_COLUMN_SKIP ) { /* Column-skip mode */
      tmp = col_mb;
      while( tmp-- ) {
        /* Read COLUMNSKIP element */
        if(vc1hwdGetBits(strm_data, 1)) {
          /* read COLUMNBITS */
          count = row_mb;
          tmp2 = count >> 2;
          /* Read 4 bit chunks */
          while( tmp2-- ) {
            tmp3 = vc1hwdGetBits( strm_data, 4 );
            tmp4 = tmp3 & 8 ? bit : 0;
            *p_data |= (u8)(tmp4 ^ invert);
            p_data += col_mb;
            tmp4 = tmp3 & 4 ? bit : 0;
            *p_data |= (u8)(tmp4 ^ invert);
            p_data += col_mb;
            tmp4 = tmp3 & 2 ? bit : 0;
            *p_data |= (u8)(tmp4 ^ invert);
            p_data += col_mb;
            tmp4 = tmp3 & 1 ? bit : 0;
            *p_data |= (u8)(tmp4 ^ invert);
            p_data += col_mb;
          }
          /* Read remainder */
          for( tmp2 = count & 3 ; tmp2 ; tmp2-- ) {
            *p_data |=
              ((vc1hwdGetBits( strm_data, 1)) ? bit : 0 ) ^ invert;
            p_data += col_mb;
          }
          p_data -= col_mb * row_mb - 1;
        } else {
          /* skip column */
          if(invert)
            for( tmp3 = row_mb, tmp2 = 0 ; tmp3-- ; tmp2 += col_mb )
              p_data[tmp2] |= (u8)invert;
          p_data ++;
        }
      }
    } else { /* Raw Mode */
      *p_raw_mask |= maskbit;
    }
  } /* Decode DATABITS */

  /* when syncmarkers enabled -> should be raw coded */
  if (sync_marker && ((*p_raw_mask & maskbit) == 0))
    rv = HANTRO_NOK;

#if defined(_DEBUG_PRINT)
  DPRINT(("vc1hwdDecodeBitPlane\n"));
  for( tmp = 0 ; tmp < row_mb ; ++tmp ) {
    for( tmp2 = 0 ; tmp2 < col_mb ; ++tmp2 ) {
      DPRINT(("%d", (*p_data_tmp) & bit ? 1 : 0 ));
      p_data_tmp++;
    }
    DPRINT(("\n"));
  }
#endif /* defined(_DEBUG_PRINT) */
  return rv;
}

