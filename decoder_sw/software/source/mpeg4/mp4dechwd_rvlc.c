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

#include "mp4dechwd_rvlc.h"
#include "mp4dechwd_utils.h"
#include "mp4debug.h"

/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

static u32 RvlcTableSearch(DecContainer *, u32, u32, u32 *);

enum
{ ERROR = 0x7EEEFFFF, EMPTY = 0x00000000, ESCAPE = 0x0000FFFF };

static const u16 u16_rvlc_table1_intra[234] = {
  1025,
  1537,
  514,
  4,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  2049,
  2561,
  5,
  6,
  34305,
  34817,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  3073,
  3585,
  1026,
  515,
  7,
  36353,
  36865,
  37377,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  4097,
  4609,
  1538,
  2050,
  516,
  517,
  8,
  9,
  32770,
  38913,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  5121,
  2562,
  1027,
  1539,
  518,
  10,
  11,
  33282,
  40449,
  40961,
  41473,
  41985,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  5633,
  6145,
  3074,
  3586,
  4098,
  2051,
  1028,
  519,
  12,
  13,
  14,
  43521,
  44033,
  44545,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  6657,
  4610,
  2563,
  3075,
  3587,
  1540,
  1029,
  1030,
  520,
  521,
  15,
  16,
  17,
  32771,
  33794,
  46081,
  0,
  0,
  0,
  0,
  0,
  0,
  5122,
  2052,
  2564,
  3076,
  1541,
  2053,
  522,
  18,
  19,
  22,
  33283,
  34306,
  34818,
  47617,
  48129,
  48641,
  49153,
  49665,
  0,
  0,
  0,
  0,
  7169,
  7681,
  5634,
  4099,
  4611,
  3588,
  1542,
  1031,
  1032,
  1033,
  523,
  20,
  21,
  23,
  32772,
  35330,
  35842,
  36354,
  36866,
  37378,
  0,
  0,
  8193,
  8705,
  9217,
  4100,
  2565,
  2054,
  2566,
  1543,
  1544,
  1034,
  1035,
  524,
  525,
  24,
  25,
  26,
  32773,
  33284,
  37890,
  38402,
  38914,
  52225,
  27,
  1545,
  3077,
  3589,
  4612,
  6146,
  9729,
  33285,
  33795,
  39426,
  53761,
  54273,
  54785,
  55297
};

static const u16 u16_rvlc_table1_inter[234] = {
  3,
  1537,
  2049,
  2561,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  514,
  3073,
  3585,
  4097,
  34305,
  34817,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  4,
  1026,
  4609,
  5121,
  5633,
  36353,
  36865,
  37377,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  5,
  6,
  515,
  1538,
  2050,
  6145,
  6657,
  7169,
  32770,
  38913,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  7,
  516,
  1027,
  2562,
  7681,
  8193,
  8705,
  33282,
  40449,
  40961,
  41473,
  41985,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  8,
  9,
  517,
  1539,
  3074,
  3586,
  4098,
  4610,
  9217,
  9729,
  10241,
  43521,
  44033,
  44545,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  10,
  11,
  518,
  1028,
  2051,
  2563,
  5122,
  10753,
  11265,
  11777,
  12289,
  12801,
  13313,
  32771,
  33794,
  46081,
  0,
  0,
  0,
  0,
  0,
  0,
  12,
  519,
  1029,
  1540,
  3075,
  3587,
  5634,
  13825,
  14337,
  14849,
  33283,
  34306,
  34818,
  47617,
  48129,
  48641,
  49153,
  49665,
  0,
  0,
  0,
  0,
  13,
  14,
  15,
  16,
  520,
  1541,
  2052,
  2564,
  4099,
  6146,
  15361,
  15873,
  16385,
  16897,
  32772,
  35330,
  35842,
  36354,
  36866,
  37378,
  0,
  0,
  17,
  18,
  521,
  522,
  1030,
  1031,
  1542,
  3076,
  4611,
  6658,
  7170,
  7682,
  8194,
  17409,
  17921,
  18433,
  32773,
  33284,
  37890,
  38402,
  38914,
  52225,
  19,
  1543,
  2053,
  3588,
  8706,
  18945,
  19457,
  33285,
  33795,
  39426,
  53761,
  54273,
  54785,
  55297
};

static const u16 u16_rvlc_table2_intra[20] = {
  33281,
  33793,
  35329,
  35841,
  37889,
  38401,
  39425,
  39937,
  42497,
  43009,
  45057,
  45569,
  46593,
  47105,
  50177,
  50689,
  51201,
  51713,
  52737,
  53249
};

static const u16 u16_rvlc_table2_inter[20] = {
  33281,
  33793,
  35329,
  35841,
  37889,
  38401,
  39425,
  39937,
  42497,
  43009,
  45057,
  45569,
  46593,
  47105,
  50177,
  50689,
  51201,
  51713,
  52737,
  53249
};

static const u32 ShortIntra[32] = {
  ERROR, ESCAPE, 0x201, 0x3FF, EMPTY, EMPTY, EMPTY, EMPTY,
  EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
  EMPTY, EMPTY, EMPTY, EMPTY, 0x3, 0x1FD, 0x8001, 0x81FF,
  0x1, 0x1, 0x1FF, 0x1FF, 0x2, 0x2, 0x1FE, 0x1FE
};
static const u32 ShortInter[32] = {
  ERROR, ESCAPE, 0x2, 0x1FE, EMPTY, EMPTY, EMPTY, EMPTY,
  EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
  EMPTY, EMPTY, EMPTY, EMPTY, 0x401, 0x5FF, 0x8001, 0x81FF,
  0x1, 0x1, 0x1FF, 0x1FF, 0x201, 0x201, 0x3FF, 0x3FF
};

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/* ------------- RVLC Table search ------------------------------*/

u32 RvlcTableSearch(DecContainer * dec_container, u32 input, u32 mb_number,
                    u32 * plen) {
  u32 SecondZero;
  u32 i;
  u32 rlc;
  u32 index;
  i32 level;
  u32 length;
  u32 tmp = 0;
  u32 last_bit = 0;
  u32 sign = 0;

  SecondZero = 999;
  length = 0;

  if(input < 0x80000000) {
    /* scan next (2nd) zero */
    for(i = 0; i < 11; i++) {
      if(!(input & (0x40000000 >> i))) {
        SecondZero = i;
        break;
      }
    }
    if(SecondZero == 999) {
      return (ERROR);
    }
    /* scan next (3rd) zero, calculate rvlc length (without sign),
     * save the last bit of rvlc code and the sign bit */
    for(i = (SecondZero + 1); i < 13; i++) {
      if(!(input & (0x40000000 >> i))) {
        length = i + 4;
        last_bit = input & (0x20000000 >> i) ? 1 : 0;
        sign = input & (0x10000000 >> i);
        break;
      }
    }
    if(!length) {
      return (ERROR);
    }

    /* calculate index to table */

    index = ((length - 6) * 22) + (SecondZero * 2) + last_bit;
    if(index >= 234) {
      return (ERROR);
    }

    if(MB_IS_INTRA(mb_number)) {
      rlc = (u32) u16_rvlc_table1_intra[index];
    } else {
      rlc = (u32) u16_rvlc_table1_inter[index];
    }
    if(rlc == EMPTY) {
      return (ERROR);
    }
    *plen = length;
    if(tmp == END_OF_STREAM) {
      return (tmp);
    }
    if(!sign) {
      return (rlc);
    } else {
      level = rlc & 0x1FF;
      level = -level;
      rlc = rlc & 0xFE00;
      rlc = rlc | (level & 0x1FF);
      return (rlc);
    }
  } else {
    /* 1st bit is 1 (msb) */
    /* scan next high bit (starting from bit number 3) */
    for(i = 2; i < 12; i++) {
      if(input & (0x40000000 >> i)) {
        last_bit = input & (0x20000000 >> i) ? 1 : 0;
        sign = input & (0x10000000 >> i);
        length = i + 4;
        break;
      }
    }
    if(!length) {
      return (ERROR);
    }
    index = (length - 6) * 2 + last_bit;

    if(MB_IS_INTRA(mb_number)) {
      rlc = u16_rvlc_table2_intra[index];
    } else {
      rlc = u16_rvlc_table2_inter[index];
    }

    *plen = length;
    if(tmp == END_OF_STREAM) {
      return (tmp);
    }
    if(!sign) {
      return (rlc);
    } else {
      level = rlc & 0x1FF;
      level = -level;
      rlc = rlc & 0xFE00;
      rlc = rlc | (level & 0x1FF);
      return (rlc);
    }
  }
}

/*------------------------------------------------------------------------------

   5.1  Function name:  StrmDec_DecodeRvlc

        Purpose:        To decode forward direction

        Input:          MPEG-4 stream

        Output:         Transform coefficients (Last, run, level)

------------------------------------------------------------------------------*/
u32 StrmDec_DecodeRvlc(DecContainer * dec_container, u32 mb_number,
                       u32 mb_numbs) {

  u32 tmp_buf;
  u32 shiftt;
  u32 used_bits;
  u32 length;
  u32 last;
  u32 MbNo;
  u32 rlc;
  u32 block_no;
  u32 coded_blocks;
  u32 decode_block;
  u32 CodeCount;
  u32 escape_tmp;
  u32 run, sign;
  i32 level;
  u32 rlc_addr_count;
  u32 tmp = 0;

  MP4DEC_API_DEBUG((" Rvlc_Decode # \n"));
  /* read in 32 bits */
  shiftt = used_bits = 0;
  SHOWBITS32(tmp_buf);

  for(MbNo = 0; MbNo < mb_numbs; MbNo++) {

    if(dec_container->StrmStorage.coded_bits[mb_number + MbNo] & 0x3F) {
      coded_blocks =
        dec_container->StrmStorage.coded_bits[mb_number + MbNo];

      /* Check that there is enough 'space' in rlc data buffer (max
       * locations needed by block is 64) */
      if((i32)
          ((dec_container->MbSetDesc.p_rlc_data_curr_addr + 64 -
            dec_container->MbSetDesc.p_rlc_data_addr)) <=
          (i32) (dec_container->MbSetDesc.rlc_data_buffer_size)) {

        for(block_no = 0; block_no < 6; block_no++) {
          decode_block = coded_blocks & ((0x20) >> block_no);
          CodeCount = 0;
          if(decode_block) {
            u32 odd_rlc_tmp = 0;

            rlc_addr_count = 0;
            do {
              length = 0;
              /* short word search */
              if(MB_IS_INTRA(mb_number + MbNo)) {
                rlc = ShortIntra[tmp_buf >> 27];
              } else {
                rlc = ShortInter[tmp_buf >> 27];
              }

              if(rlc == EMPTY) {
                /* no escape or indexes between 0-4 or
                 * ERROR-sig, normal table search */
                rlc = RvlcTableSearch(dec_container, tmp_buf,
                                      (mb_number + MbNo),
                                      &length);
                used_bits += length;
                shiftt = length;
              } else if(rlc == ESCAPE) {
                /* escape code (25 + 5 bits long) */
                FLUSHBITS((used_bits + 5));
                SHOWBITS32(escape_tmp);
                used_bits = shiftt = 25;
                last = escape_tmp >> 31;
                run = (escape_tmp & 0x7FFFFFFF) >> 25;
                level = (escape_tmp & 0x00FFFFFF) >> 13;
                sign = (escape_tmp & 0x000000FF) >> 7;
                /* check marker bits & escape from the end */
                if(!(escape_tmp & 0x01000000) ||
                    !(escape_tmp & 0x00001000) ||
                    (escape_tmp & 0x00000F00)) {
                  return (HANTRO_NOK);
                }
                if(sign)
                  level = -level;
                if((level > 255) || (level < -256)) {
                  rlc = ((level << 16) | (last << 15) |
                         (run << 9)) & 0xFFFFFE00;
                } else {
                  rlc = (last << 15) | (run << 9) |
                        (level & 0x1ff);
                }
              } else {
                /* Short word -> update used bits */
                {
                  u32 cmp_tmp = 0;

                  cmp_tmp = (tmp_buf < 0xC0000000);
                  length = 4 + cmp_tmp;
                  used_bits += length;
                  shiftt = length;
                }
              }

              if(rlc == ERROR)
                return (HANTRO_NOK);

              if(used_bits > 16) {
                /* Not enough bits in input buffer */
                FLUSHBITS(used_bits);
                used_bits = shiftt = 0;
                SHOWBITS32(tmp_buf);
              } else {
                tmp_buf <<= shiftt;
                shiftt = 0;
              }

              /* SAVING RLC WORD'S */

              /* if half of a word was left empty last time, fill
               * it first */
              if(dec_container->MbSetDesc.odd_rlc) {
                odd_rlc_tmp =
                  dec_container->MbSetDesc.odd_rlc;
                tmp =
                  *dec_container->MbSetDesc.p_rlc_data_curr_addr;
              }
              /* odd address count -> start saving in 15:0 */
              /* odd_rlc_tmp means that a word was left halp empty
               * in the last block */

              if((rlc_addr_count + odd_rlc_tmp) & 0x01) {

                dec_container->MbSetDesc.odd_rlc = 0;
                rlc_addr_count++;
                if((rlc & 0x1FF) == 0) {
                  rlc_addr_count++;
                  tmp |= (0xFFFF & rlc);
                  *dec_container->MbSetDesc.
                  p_rlc_data_curr_addr++ = tmp;

                  tmp = (rlc & 0xFFFF0000);

                } else {
                  tmp = tmp | (rlc & 0xFFFF);
                }

              }

              /* even address count -> start saving in 31:16 */
              else {
                rlc_addr_count++;
                if((rlc & 0x1FF) == 0) { /* BIG level  */
                  rlc_addr_count++;

                  tmp = ((rlc & 0xFFFF) << 16);
                  tmp = tmp | (rlc >> 16 /*& 0xFFFF */ );

                } else {
                  tmp = (rlc << 16);
                }
              }

              if(((rlc_addr_count + odd_rlc_tmp) & 0x01) == 0) {
                *dec_container->MbSetDesc.
                p_rlc_data_curr_addr++ = tmp;
              }

              last = rlc & 0x8000;
              CodeCount += (((rlc & 0x7E00) >> 9) + 1);

              if(CodeCount > 64) {
                return (HANTRO_NOK);
              }

            } while(!last);

            /* lonely 16 bits stored in tmp -> write to asic input
             * buffer */
            if(((rlc_addr_count + odd_rlc_tmp) & 0x01) == 1) {
              *dec_container->MbSetDesc.
              p_rlc_data_curr_addr = tmp;
              dec_container->MbSetDesc.odd_rlc = 1;
            }
          }   /* end of decode_block */
        }   /* end of block-loop */

      } else {
        return (HANTRO_NOK);
      }

    }   /* end coded loop */
  }   /* end of mb-loop */
  /* flush used bits */
  if(used_bits)
    FLUSHBITS(used_bits);
  return (HANTRO_OK);
}   /* end of function */
