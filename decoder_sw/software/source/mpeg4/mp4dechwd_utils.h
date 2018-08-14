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

#ifndef STRMDEC_UTILS_H_DEFINED
#define STRMDEC_UTILS_H_DEFINED

#include "mp4dechwd_container.h"

/* constant definitions */
#ifndef HANTRO_OK
#define HANTRO_OK 0
#endif

#ifndef HANTRO_NOK
#define HANTRO_NOK 1
#endif

#ifndef HANTRO_FALSE
#define HANTRO_FALSE 0
#endif

#ifndef HANTRO_TRUE
#define HANTRO_TRUE 1
#endif

#define AMBIGUOUS 2

#ifndef NULL
#define NULL 0
#endif

/* decoder states */
enum {
  STATE_OK,
  STATE_NOT_READY,
  STATE_SYNC_LOST
};

/* start codes */
enum {
  SC_VO_START = 0x00000100,
  SC_VOL_START = 0x00000120,
  SC_VOS_START = 0x000001B0,
  SC_VOS_END = 0x000001B1,
  SC_UD_START = 0x000001B2,
  SC_GVOP_START = 0x000001B3,
  SC_VISO_START = 0x000001B5,
  SC_VOP_START = 0x000001B6,
  SC_RESYNC = 0x1,
  SC_SV_START = 0x20,
  SC_SORENSON_START = 0x10,
  SC_SV_END = 0x3F,
  SC_NOT_FOUND = 0xFFFE,
  SC_ERROR = 0xFFFF
};

enum {
  MB_INTER = 0,
  MB_INTERQ = 1,
  MB_INTER4V = 2,
  MB_INTRA = 3,
  MB_INTRAQ = 4,
  MB_STUFFING = 5
};

enum {
  IVOP = 0,
  PVOP = 1,
  BVOP = 2,
  NOT_SET
};

/*enum {
    DEC = 0,
    HEX = 1,
    BIN = 2
};*/

/* value to be returned by StrmDec_GetBits if stream buffer is empty */
#define END_OF_STREAM 0xFFFFFFFFU

/* How many control words(32bit) does one mb take */

#define NBR_OF_WORDS_MB 1

/* how many motion vector data words for mb */

#define NBR_MV_WORDS_MB 4

/* how many DC VLC data words for mb */

#define NBR_DC_WORDS_MB 2

enum {
  OUT_OF_BUFFER = 0xFF
};

/* Bit positions in Asic CtrlBits memory ( specified in top level spec ) */

enum {
  ASICPOS_MBTYPE = 31,
  ASICPOS_4MV = 30,
  ASICPOS_USEINTRADCVLC = 30,
  ASICPOS_VPBI = 29,
  ASICPOS_ACPREDFLAG = 28,
  ASICPOS_QP = 16,
  ASICPOS_CONCEAL = 15,
  ASICPOS_MBNOTCODED = 14
};

/* Boundary strenght positions in ctrlBits  */

enum {
  BS_VER_00 = 13,
  BS_HOR_00 = 10,
  BS_VER_02 = 7,
  BS_HOR_01 = 4,
  BS_VER_04 = 1,
  BS_HOR_02_MSB = 0,
  BS_HOR_02_LSB = 30,
  BS_VER_06 = 27,
  BS_HOR_03 = 24,
  BS_VER_08 = 21,
  BS_HOR_08 = 18,
  BS_VER_10 = 15,
  BS_HOR_09 = 12,
  BS_VER_12 = 9,
  BS_HOR_10 = 6,
  BS_VER_14 = 3,
  BS_HOR_11 = 0
};

enum {

  ASIC_ZERO_MOTION_VECTORS_LUM = 0,
  ASIC_ZERO_MOTION_VECTORS_CHR = 0
};

enum {
  MB_NOT_CODED = 0x80
};

#define MB_IS_INTRA(mb_number) \
    ( (dec_container->MBDesc[mb_number].type_of_mb == MB_INTRA) || \
      (dec_container->MBDesc[mb_number].type_of_mb == MB_INTRAQ) )

#define MB_IS_INTER(mb_number) \
    (dec_container->MBDesc[mb_number].type_of_mb <= MB_INTER4V)

#define MB_IS_INTER4V(mb_number) \
    (dec_container->MBDesc[mb_number].type_of_mb == MB_INTER4V)

#define MB_IS_STUFFING(mb_number) \
    (dec_container->MBDesc[mb_number].type_of_mb == MB_STUFFING)

#define MB_HAS_DQUANT(mb_number) \
    ( (dec_container->MBDesc[mb_number].type_of_mb == MB_INTERQ) || \
      (dec_container->MBDesc[mb_number].type_of_mb == MB_INTRAQ) )

/* macro to check if stream ends */
#define IS_END_OF_STREAM(p_container) \
    ( (p_container)->StrmDesc.strm_buff_read_bits == \
      (8*(p_container)->StrmDesc.strm_buff_size) )

#define SATURATE(min,value,max) \
    if ((value) < (min)) (value) = (min); \
    else if ((value) > (max)) (value) = (max);

#define SHOWBITS32(tmp) \
{ \
    i32 bits, shift; \
    const u8 *pstrm = dec_container->StrmDesc.strm_curr_pos; \
    bits = (i32)dec_container->StrmDesc.strm_buff_size*8 - \
               (i32)dec_container->StrmDesc.strm_buff_read_bits; \
    if (bits >= 32) \
    { \
        tmp = ((u32)pstrm[0] << 24) | ((u32)pstrm[1] << 16) |  \
                  ((u32)pstrm[2] <<  8) | ((u32)pstrm[3]); \
        if (dec_container->StrmDesc.bit_pos_in_word) \
        { \
            tmp <<= dec_container->StrmDesc.bit_pos_in_word; \
            tmp |= \
                (u32)pstrm[4]>>(8-dec_container->StrmDesc.bit_pos_in_word); \
        } \
    } \
    else if (bits) \
    { \
        shift = 24 + dec_container->StrmDesc.bit_pos_in_word; \
        tmp = (u32)(*pstrm++) << shift; \
        bits -= 8 - dec_container->StrmDesc.bit_pos_in_word; \
        while (bits > 0) \
        { \
            shift -= 8; \
            tmp |= (u32)(*pstrm++) << shift; \
            bits -= 8; \
        } \
    } \
    else \
        tmp = 0; \
}
#define FLUSHBITS(bits) \
{ \
    u32 FBtmp;\
    if ( (dec_container->StrmDesc.strm_buff_read_bits + bits) > \
         (8*dec_container->StrmDesc.strm_buff_size) ) \
    { \
    dec_container->StrmDesc.strm_buff_read_bits = \
            8 * dec_container->StrmDesc.strm_buff_size; \
        dec_container->StrmDesc.bit_pos_in_word = 0; \
        dec_container->StrmDesc.strm_curr_pos = \
            dec_container->StrmDesc.p_strm_buff_start + \
            dec_container->StrmDesc.strm_buff_size;\
        return(END_OF_STREAM);\
    }\
    else\
    {\
        dec_container->StrmDesc.strm_buff_read_bits += bits;\
        FBtmp = dec_container->StrmDesc.bit_pos_in_word + bits;\
        dec_container->StrmDesc.strm_curr_pos += FBtmp >> 3;\
        dec_container->StrmDesc.bit_pos_in_word = FBtmp & 0x7;\
    }\
}

/* function prototypes */

u32 StrmDec_GetBits(DecContainer *, u32 num_bits);
u32 StrmDec_ShowBits(DecContainer *, u32 num_bits);
u32 StrmDec_ShowBitsAligned(DecContainer *, u32 num_bits, u32 num_bytes);
u32 StrmDec_FlushBits(DecContainer *, u32 num_bits);
u32 StrmDec_UnFlushBits(DecContainer *, u32 num_bits);
void StrmDec_ProcessPacketFooter( DecContainer * );

u32 StrmDec_GetStuffing(DecContainer *);
u32 StrmDec_CheckStuffing(DecContainer *);

u32 StrmDec_FindSync(DecContainer *);
u32 StrmDec_GetStartCode(DecContainer *);
u32 StrmDec_ProcessBvopExtraResync(DecContainer *);

u32 StrmDec_NumBits(u32 value);

#endif
