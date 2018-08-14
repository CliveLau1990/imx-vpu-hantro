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

#include "mp4dechwd_utils.h"
#include "mp4dechwd_videopacket.h"

#include "mp4debug.h"
/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum {
  SV_MARKER_MASK = 0x1FFFF,
  SV_END_MASK = 0x3FFFFF,
  SECOND_BYTE_ZERO_MASK = 0x00FF0000
};

/* to check first 23 bits */
#define START_CODE_MASK      0xFFFFFE00
/* to check first 16 bits */
#define RESYNC_MASK          0xFFFF0000

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

static const u32 StuffingTable[8] =
{ 0x0, 0x1, 0x3, 0x7, 0xF, 0x1F, 0x3F, 0x7F };

/* table containing start code id for each possible start code suffix. Start
 * code suffixes that are either reserved or non-applicable have been marked
 * with 0 */
static const u32 start_code_table[256] = {
  SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START,
  SC_VO_START,
  SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START,
  SC_VO_START,
  SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START,
  SC_VO_START,
  SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START,
  SC_VO_START,
  SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START, SC_VO_START,
  SC_VO_START,
  SC_VO_START, SC_VO_START,
  SC_VOL_START, SC_VOL_START, SC_VOL_START, SC_VOL_START, SC_VOL_START,
  SC_VOL_START, SC_VOL_START, SC_VOL_START, SC_VOL_START, SC_VOL_START,
  SC_VOL_START, SC_VOL_START, SC_VOL_START, SC_VOL_START, SC_VOL_START,
  SC_VOL_START,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0,
  SC_VOS_START, SC_VOS_END, SC_UD_START, SC_GVOP_START, 0, SC_VISO_START,
  SC_VOP_START,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*------------------------------------------------------------------------------

   5.1  Function name: StrmDec_GetBits

        Purpose: read and remove bits from mpeg-4 input stream

        Input:
            Pointer to DecContainer structure
            Number of bits to read [0,31]

        Output:
            u32 containing bits read from stream. Value END_OF_STREAM
            reserved to indicate failure.

------------------------------------------------------------------------------*/

u32 StrmDec_GetBits(DecContainer * dec_container, u32 num_bits) {

  u32 out, tmp;

  ASSERT(num_bits < 32);

  /*out = StrmDec_ShowBits(dec_container, num_bits); */
  SHOWBITS32(out);
  out = out >> (32 - num_bits);

  if((dec_container->StrmDesc.strm_buff_read_bits + num_bits) >
      (8 * dec_container->StrmDesc.strm_buff_size)) {
    dec_container->StrmDesc.strm_buff_read_bits =
      8 * dec_container->StrmDesc.strm_buff_size;
    dec_container->StrmDesc.bit_pos_in_word = 0;
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmDesc.p_strm_buff_start +
      dec_container->StrmDesc.strm_buff_size;
    return (END_OF_STREAM);
  } else {
    dec_container->StrmDesc.strm_buff_read_bits += num_bits;
    tmp = dec_container->StrmDesc.bit_pos_in_word + num_bits;
    dec_container->StrmDesc.strm_curr_pos += tmp >> 3;
    dec_container->StrmDesc.bit_pos_in_word = tmp & 0x7;
    return (out);
  }
  /*if (StrmDec_FlushBits(dec_container, num_bits) == HANTRO_OK)
   * {
   * return(out);
   * }
   * else
   * {
   * return(END_OF_STREAM);
   * } */

}

/*------------------------------------------------------------------------------

   5.2  Function name: StrmDec_ShowBits

        Purpose: read bits from mpeg-4 input stream. Bits are located right
        aligned in the 32-bit output word. In case stream ends,
        function fills the word with zeros. For example, num_bits = 18 and
        there are 7 bits left in the stream buffer -> return
            00000000000000xxxxxxx00000000000,
        where 'x's represent actual bits read from buffer.

        Input:
            Pointer to DecContainer structure
                -uses but does not update StrmDesc
            Number of bits to read [0,32]

        Output:
            u32 containing bits read from stream

------------------------------------------------------------------------------*/

u32 StrmDec_ShowBits(DecContainer * dec_container, u32 num_bits) {

  u32 i;
  i32 bits, shift;
  u32 out;
  const u8 *pstrm = dec_container->StrmDesc.strm_curr_pos;

  ASSERT(num_bits <= 32);

  /* bits left in the buffer */
  bits = (i32) dec_container->StrmDesc.strm_buff_size * 8 -
         (i32) dec_container->StrmDesc.strm_buff_read_bits;

  if(!num_bits || !bits) {
    return (0);
  }

  /* at least 32-bits in the buffer -> get 32 bits and drop extra bits out */
  if(bits >= 32) {
    out = ((u32) pstrm[0] << 24) | ((u32) pstrm[1] << 16) |
          ((u32) pstrm[2] << 8) | ((u32) pstrm[3]);
    if(dec_container->StrmDesc.bit_pos_in_word) {
      out <<= dec_container->StrmDesc.bit_pos_in_word;
      out |=
        (u32) pstrm[4] >> (8 -
                           dec_container->StrmDesc.bit_pos_in_word);
    }
  } else {
    shift = 24 + dec_container->StrmDesc.bit_pos_in_word;
    out = (u32) pstrm[0] << shift;
    bits -= 8 - dec_container->StrmDesc.bit_pos_in_word;
    i = 1;
    while(bits > 0) {
      shift -= 8;
      out |= (u32) pstrm[i] << shift;
      bits -= 8;
      i++;
    }
  }

  return (out >> (32 - num_bits));

}

/*------------------------------------------------------------------------------

   5.3  Function name: StrmDec_ShowBitsAligned

        Purpose: read bits from mpeg-4 input stream at byte aligned position
        given as parameter (offset from current stream position).

        Input:
            Pointer to DecContainer structure
                -uses but does not update StrmDesc
            Number of bits to read [1,32]
            Byte offset from current stream position

        Output:
            u32 containing bits read from stream

------------------------------------------------------------------------------*/

u32 StrmDec_ShowBitsAligned(DecContainer * dec_container, u32 num_bits,
                            u32 byte_offset) {

  u32 i;
  u32 out;
  u32 bytes, shift;
  const u8 *pstrm = dec_container->StrmDesc.strm_curr_pos + byte_offset;

  ASSERT(num_bits);
  ASSERT(num_bits <= 32);
  out = 0;

  /* at least four bytes available starting byte_offset bytes ahead */
  if((dec_container->StrmDesc.strm_buff_size >= (4 + byte_offset)) &&
      ((dec_container->StrmDesc.strm_buff_read_bits >> 3) <=
       (dec_container->StrmDesc.strm_buff_size - byte_offset - 4))) {
    out = ((u32) pstrm[0] << 24) | ((u32) pstrm[1] << 16) |
          ((u32) pstrm[2] << 8) | ((u32) pstrm[3]);
    out >>= (32 - num_bits);
  } else {
    bytes = dec_container->StrmDesc.strm_buff_size -
            (dec_container->StrmDesc.strm_buff_read_bits >> 3);
    if(bytes > byte_offset) {
      bytes -= byte_offset;
    } else {
      bytes = 0;
    }

    shift = 24;
    i = 0;
    while(bytes) {
      out |= (u32) pstrm[i] << shift;
      i++;
      shift -= 8;
      bytes--;
    }

    out >>= (32 - num_bits);
  }

  return (out);

}

/*------------------------------------------------------------------------------

   5.4  Function name: StrmDec_FlushBits

        Purpose: removes bits from mpeg-4 input stream

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc
            Number of bits to remove [0,2^32)

        Output:
            HANTRO_OK if operation was successful
            END_OF_STREAM if stream buffer run empty

------------------------------------------------------------------------------*/

u32 StrmDec_FlushBits(DecContainer * dec_container, u32 num_bits) {

  u32 tmp;

  if((dec_container->StrmDesc.strm_buff_read_bits + num_bits) >
      (8 * dec_container->StrmDesc.strm_buff_size)) {
    dec_container->StrmDesc.strm_buff_read_bits =
      8 * dec_container->StrmDesc.strm_buff_size;
    dec_container->StrmDesc.bit_pos_in_word = 0;
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmDesc.p_strm_buff_start +
      dec_container->StrmDesc.strm_buff_size;
    return (END_OF_STREAM);
  } else {
    dec_container->StrmDesc.strm_buff_read_bits += num_bits;
    tmp = dec_container->StrmDesc.bit_pos_in_word + num_bits;
    dec_container->StrmDesc.strm_curr_pos += tmp >> 3;
    dec_container->StrmDesc.bit_pos_in_word = tmp & 0x7;
    return (HANTRO_OK);
  }

}

/*------------------------------------------------------------------------------

   5.5  Function name: StrmDec_GetStuffing

        Purpose: removes mpeg-4 stuffing from input stream

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc

        Output:
            HANTRO_OK if operation was successful
            HANTRO_NOK if error in stuffing (i.e. not 0111...)
            END_OF_STREAM

------------------------------------------------------------------------------*/

u32 StrmDec_GetStuffing(DecContainer * dec_container) {

  u32 stuffing;
  u32 stuffing_length = 8 - dec_container->StrmDesc.bit_pos_in_word;

  ASSERT(stuffing_length && (stuffing_length <= 8));

#ifdef HANTRO_PEDANTIC_MODE

  stuffing = StrmDec_GetBits(dec_container, stuffing_length);

  if(stuffing == END_OF_STREAM) {
    return (END_OF_STREAM);
  }

  if(stuffing != StuffingTable[stuffing_length - 1])
    return (HANTRO_NOK);
  else
    return (HANTRO_OK);

#else

  stuffing = StrmDec_ShowBits(dec_container, stuffing_length);
  if(stuffing != StuffingTable[stuffing_length - 1])
    return (HANTRO_OK);

  stuffing = StrmDec_GetBits(dec_container, stuffing_length);
  return (HANTRO_OK);

#endif

}

/*------------------------------------------------------------------------------

   5.5  Function name: StrmDec_CheckStuffing

        Purpose: checks mpeg-4 stuffing from input stream

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc

        Output:
            HANTRO_OK if operation was successful
            HANTRO_NOK if error in stuffing (i.e. not 0111...)

------------------------------------------------------------------------------*/

u32 StrmDec_CheckStuffing(DecContainer * dec_container) {

  u32 length;

  ASSERT(dec_container);
  ASSERT(dec_container->StrmDesc.bit_pos_in_word < 8);

  length = 8 - dec_container->StrmDesc.bit_pos_in_word;
  if ( StrmDec_ShowBits(dec_container, length) == StuffingTable[length-1] )
    return (HANTRO_OK);
  else
    return (HANTRO_NOK);

}

/*------------------------------------------------------------------------------

   5.6  Function name: StrmDec_FindSync

        Purpose: searches next syncronization point in mpeg-4 stream. Takes
        into account StrmDecStorage.status as follows:
            STATE_NOT_READY -> only accept SC_VOS_START, SC_VO_START,
                SC_VISO_START and SC_VOL_START codes (to get initial headers
                for decoding). Also accept SC_SV_START as short video vops
                provide all information needed in decoding.

        In addition to above mentioned condition short video markers are
        accepted only in case short_video in DecStrmStrorage is HANTRO_TRUE.

        Function starts searching sync four bytes back from current stream
        position. However, if this would result starting before last found sync,
        search is started one byte ahead fo last found sync. Search is always
        started at byte aligned position.

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc
                -uses and updates StrmStorage

        Output:
            Start code value in case one was found, END_OF_STREAM if stream
            end was encountered before sync was found. SC_ERROR if start code
            prefix correct but suffix does not match or more than 23
            successive zeros found.

------------------------------------------------------------------------------*/

u32 StrmDec_FindSync(DecContainer * dec_container) {

  u32 i, tmp;
  u32 code;
  u32 status;
  u32 code_length;
  u32 mask;
  u32 marker_length;

  if(dec_container->StrmStorage.valid_vop_header == HANTRO_TRUE) {
    marker_length = dec_container->StrmStorage.resync_marker_length;
  } else {
    marker_length = 0;
  }

  /* go back 4 bytes and to beginning of byte (or to beginning of stream)
   * before starting search */
  if(dec_container->StrmDesc.strm_buff_read_bits > 39) {
    dec_container->StrmDesc.strm_buff_read_bits -= 32;
    /* drop 3 lsbs (i.e. make read bits next lowest multiple of byte) */
    dec_container->StrmDesc.strm_buff_read_bits &= 0xFFFFFFF8;
    dec_container->StrmDesc.bit_pos_in_word = 0;
    dec_container->StrmDesc.strm_curr_pos -= 4;
  } else {
    dec_container->StrmDesc.strm_buff_read_bits = 0;
    dec_container->StrmDesc.bit_pos_in_word = 0;
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmDesc.p_strm_buff_start;
  }

  /* beyond last sync point -> go to last sync point */
  if(dec_container->StrmDesc.strm_curr_pos <
      dec_container->StrmStorage.p_last_sync) {
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmStorage.p_last_sync;
    dec_container->StrmDesc.strm_buff_read_bits = 8 *
        (dec_container->StrmDesc.strm_curr_pos -
         dec_container->StrmDesc.p_strm_buff_start);
  }

  code = 0;
  code_length = 0;

  while(!code) {
    tmp = StrmDec_ShowBits(dec_container, 32);
    /* search if two first bytes equal to zero (all start codes have at
     * least 16 zeros in the beginning) or short video case (not
     * necessarily byte aligned start codes) */
    if(tmp && (!(tmp & RESYNC_MASK) ||
               (dec_container->StrmStorage.short_video == HANTRO_TRUE))) {
      if(dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) {
        if(((tmp & 0xFFFFFFE0) == SC_VO_START) ||
            ((tmp & 0xFFFFFFF0) == SC_VOL_START) ||
            (tmp == SC_VISO_START) || (tmp == SC_VOS_START)) {
          code = start_code_table[tmp & 0xFF];
          code_length = 32;
        } else if((tmp >> 10) == SC_SV_START) {
          code = tmp >> 10;
          code_length = 22;
        }
      } else if(dec_container->StrmStorage.short_video == HANTRO_FALSE) {
        if(((tmp >> 8) == 0x01) && start_code_table[tmp & 0xFF]) {
          code = start_code_table[tmp & 0xFF];
          /* do not accept user data start code here */
          if(code == SC_UD_START) {
            code = 0;
          }
          code_length = 32;
        }
        /* either start code prefix or 24 zeros -> start code error */
        else if(dec_container->rlc_mode && (tmp & 0xFFFFFE00) == 0) {
          /* consider VOP start code lost */
          code_length = 32;
          dec_container->StrmStorage.start_code_loss = HANTRO_TRUE;
        } else if(dec_container->rlc_mode &&
                  !dec_container->Hdrs.resync_marker_disable) {
          /* resync marker length known? */
          if(marker_length) {
            if((tmp >> (32 - marker_length)) == 0x01) {
              code = SC_RESYNC;
              code_length = marker_length;
            }
          }
          /* try all possible lengths [17,23] if third byte contains
           * at least one '1' */
          else {
            marker_length = 17;
            mask = 0x8000;
            while(!(tmp & mask)) {
              mask >>= 1;
              marker_length++;
            }
            code = SC_RESYNC;
            code_length = marker_length;
            dec_container->StrmStorage.resync_marker_length =
              marker_length;
          }
        }
      } else { /* short video */
        if(((tmp & 0xFFFFFFE0) == SC_VO_START) ||
            (tmp == SC_VOS_START) ||
            (tmp == SC_VOS_END) || (tmp == SC_VISO_START)) {
          code = start_code_table[tmp & 0xFF];
          code_length = 32;
        } else if((tmp >> 10) == SC_SV_START) {
          code = tmp >> 10;
          code_length = 22;
        }
        /* check short video resync and end code at each possible bit
         * position [0,7] if second byte of tmp is 0 */
        else if(dec_container->rlc_mode &&
                !(tmp & SECOND_BYTE_ZERO_MASK)) {
          for(i = 15; i >= 8; i--) {
            if(((tmp >> i) & SV_MARKER_MASK) == SC_RESYNC) {
              /* Not short video end marker */
              if(((tmp >> (i - 5)) & SV_END_MASK) != SC_SV_END) {
                code = SC_RESYNC;
                code_length = 32 - i;
                break;
              }
            }
          }
        }
      }
    }
    if(code_length) {
      status = StrmDec_FlushBits(dec_container, code_length);
      dec_container->StrmStorage.p_last_sync =
        dec_container->StrmDesc.strm_curr_pos;
      code_length = 0;
    } else {
      status = StrmDec_FlushBits(dec_container, 8);
    }
    if(status == END_OF_STREAM) {
      return (END_OF_STREAM);
    }
  }

  return (code);

}

/*------------------------------------------------------------------------------

   5.7  Function name: StrmDec_GetStartCode

        Purpose: tries to read next start code from input stream. Start code
        is not searched but it is assumed that if there is start code it is at
        current stream position.

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc
                -uses and updates StrmStorage

        Output:
            Start code value if found, SC_NOT_FOUND if no start code detected
            and SC_ERROR if start code prefix correct but suffix does not match
            or more than 23 successive zeros found. END_OF_STREAM if stream
            ended.

------------------------------------------------------------------------------*/

u32 StrmDec_GetStartCode(DecContainer * dec_container) {

  u32 tmp;
  u32 marker_length;
  u32 code_length = 0;
  u32 start_code = SC_NOT_FOUND;

  marker_length = dec_container->StrmStorage.resync_marker_length;

  /* reset at next byte boundary */
  if(!dec_container->rlc_mode) {
    if( dec_container->StrmDesc.bit_pos_in_word ) {
      tmp = StrmDec_FlushBits( dec_container,
                               8-dec_container->StrmDesc.bit_pos_in_word );
      if( tmp == END_OF_STREAM )
        return END_OF_STREAM;
    }
  }

  tmp = StrmDec_ShowBits(dec_container, 32);
  while( tmp <= 0x1 &&
         dec_container->StrmStorage.short_video == HANTRO_FALSE ) {
    tmp = StrmDec_FlushBits( dec_container, 8 );
    if( tmp == END_OF_STREAM )
      return (END_OF_STREAM);
    tmp = StrmDec_ShowBits(dec_container, 32);
  }

  if (dec_container->StrmStorage.sorenson_spark) {
    if ((tmp >> 11) == SC_SORENSON_START) {
      code_length = 21;
      start_code = tmp >> 11;
    }
  }
  /* only check VisualObjectSequence, VisualObject, VisualObjectLayer and
   * short video start codes if decoder is not ready yet */
  else if(dec_container->StrmStorage.strm_dec_ready == HANTRO_FALSE) {
    if(((tmp & 0xFFFFFFE0) == SC_VO_START) ||
        ((tmp & 0xFFFFFFF0) == SC_VOL_START) ||
        (tmp == SC_VOS_START) ||
        (tmp == SC_VOS_END) || (tmp == SC_VISO_START)) {
      code_length = 32;
      start_code = start_code_table[tmp & 0xFF];
    } else if((tmp >> 10) == SC_SV_START) {
      code_length = 22;
      start_code = tmp >> 10;
    }
  }

  else if((dec_container->StrmStorage.short_video == HANTRO_FALSE) &&
          ((tmp >> 8) == 0x01) && start_code_table[tmp & 0xFF]) {
    code_length = 32;
    start_code = start_code_table[tmp & 0xFF];
  } else if((dec_container->StrmStorage.short_video == HANTRO_TRUE) &&
            (((tmp & 0xFFFFFFE0) == SC_VO_START) || (tmp == SC_VOS_START) ||
             (tmp == SC_VOS_END) || (tmp == SC_VISO_START))) {
    code_length = 32;
    start_code = start_code_table[tmp & 0xFF];
  } else if((dec_container->StrmStorage.short_video == HANTRO_TRUE) &&
            (((tmp >> 10) == SC_SV_START) || ((tmp >> 10) == SC_SV_END))) {
    code_length = 22;
    start_code = tmp >> 10;
  }
  /* accept resync marker only if in RLC mode */
  else if(dec_container->rlc_mode &&
          (tmp >> (32 - marker_length)) == SC_RESYNC) {
    code_length = marker_length;
    start_code = SC_RESYNC;
  } else if((tmp & 0xFFFFFE00) == 0) {
    code_length = 32;
    start_code = SC_ERROR;
    /* consider VOP start code lost */
    dec_container->StrmStorage.start_code_loss = HANTRO_TRUE;
  }

  /* Hack for H.263 streams -- SC_SV_END is not necessarily stuffed with
   * zeroes. */
  if((dec_container->StrmStorage.short_video == HANTRO_TRUE) &&
      start_code == SC_NOT_FOUND ) {
    u32 i;
    u32 stuffing_bits = 0;
    /* There might be 1...7 bits of zeroes required to form an end
     * marker. */
    for( i = 1 ; i <= 7 ; ++i ) {
      if( tmp >> (10+i) == SC_SV_END ) {
        stuffing_bits = i;
        break;
      }
    }

    if( stuffing_bits ) {
      /* Get last byte from bitstream. */
      tmp = dec_container->StrmStorage.last_packet_byte;

      if( (tmp & (( 1 << stuffing_bits ) - 1 )) == 0x0 ) {
        start_code = SC_SV_END;
        code_length = 22 - stuffing_bits;
        dec_container->StrmStorage.last_packet_byte = 0xFF;
      }
    }

  }

  if(start_code != SC_NOT_FOUND) {
    tmp = StrmDec_FlushBits(dec_container, code_length);
    if(tmp == END_OF_STREAM)
      return (END_OF_STREAM);
    dec_container->StrmStorage.p_last_sync =
      dec_container->StrmDesc.strm_curr_pos;
  }

  return (start_code);

}

/*------------------------------------------------------------------------------

   5.8  Function name: StrmDec_NumBits

        Purpose: computes number of bits needed to represent value given as
        argument

        Input:
            u32 value [0,2^32)

        Output:
            Number of bits needed to represent input value

------------------------------------------------------------------------------*/

u32 StrmDec_NumBits(u32 value) {

  u32 num_bits = 0;

  while(value) {
    value >>= 1;
    num_bits++;
  }

  if(!num_bits) {
    num_bits = 1;
  }

  return (num_bits);

}

/*------------------------------------------------------------------------------

   5.9  Function name: StrmDec_UnFlushBits

        Purpose: unflushes bits from mpeg-4 input stream

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc
            Number of bits to unflush [0,2^32)

        Output:
            HANTRO_OK if operation was successful

------------------------------------------------------------------------------*/

u32 StrmDec_UnFlushBits(DecContainer * dec_container, u32 num_bits) {

  if(dec_container->StrmDesc.strm_buff_read_bits < num_bits) {
    dec_container->StrmDesc.strm_buff_read_bits = 0;
    dec_container->StrmDesc.bit_pos_in_word = 0;
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmDesc.p_strm_buff_start;
  } else {
    dec_container->StrmDesc.strm_buff_read_bits -= num_bits;
    dec_container->StrmDesc.bit_pos_in_word =
      dec_container->StrmDesc.strm_buff_read_bits & 0x7;
    dec_container->StrmDesc.strm_curr_pos =
      dec_container->StrmDesc.p_strm_buff_start +
      (dec_container->StrmDesc.strm_buff_read_bits >> 3);
  }

  return (HANTRO_OK);
}


/*------------------------------------------------------------------------------

   5.9  Function name: StrmDec_ProcessPacketFooter

        Purpose: store last byte of processed packet for checking SC_SV_END
           existence.

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc

        Output:

------------------------------------------------------------------------------*/

void StrmDec_ProcessPacketFooter( DecContainer * dec_cont ) {

  /* Variables */

  u32 last_byte;

  /* Code */

  if ( dec_cont->StrmStorage.short_video == HANTRO_TRUE &&
       dec_cont->StrmDesc.strm_curr_pos > dec_cont->StrmDesc.p_strm_buff_start ) {
    last_byte = dec_cont->StrmDesc.strm_curr_pos[-1];
  } else {
    last_byte = 0xFF;
  }

  dec_cont->StrmStorage.last_packet_byte = last_byte;

}

/*------------------------------------------------------------------------------

   5.9  Function name: StrmDec_ProcessBvopExtraResync

        Purpose:

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmDesc

        Output:

------------------------------------------------------------------------------*/

u32 StrmDec_ProcessBvopExtraResync(DecContainer *dec_cont) {

  /* Variables */

  u32 marker_length;
  u32 tmp;
  u32 prev_vp_mb_num;

  /* Code */

  if(dec_cont->StrmStorage.valid_vop_header == HANTRO_TRUE) {
    marker_length = dec_cont->StrmStorage.resync_marker_length;

    tmp = StrmDec_ShowBits(dec_cont, marker_length);
    while( tmp == 0x01 ) {
      tmp = StrmDec_FlushBits(dec_cont, marker_length);
      if( tmp == END_OF_STREAM )
        return HANTRO_NOK;
      /* We just want to decode the video packet header, so cheat on the
       * macroblock number */
      prev_vp_mb_num = dec_cont->StrmStorage.vp_mb_number;
      dec_cont->StrmStorage.vp_mb_number =
        StrmDec_CheckNextVpMbNumber( dec_cont );
      tmp = StrmDec_DecodeVideoPacketHeader( dec_cont );
      if( tmp != HANTRO_OK )
        return tmp;
      tmp = StrmDec_GetStuffing( dec_cont );
      if( tmp != HANTRO_OK )
        return tmp;

      dec_cont->StrmStorage.vp_mb_number = prev_vp_mb_num;

      /* Show next bits and loop again. */
      tmp = StrmDec_ShowBits(dec_cont, marker_length);
    }
  }

  return HANTRO_OK;
}
