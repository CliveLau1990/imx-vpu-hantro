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

#include "h264hwd_cavlc.h"
#include "h264hwd_util.h"
#include "dwl.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* Following descriptions use term "information field" to represent combination
 * of certain decoded symbol value and the length of the corresponding variable
 * length code word. For example, total_zeros information field consists of
 * 4 bits symbol value (bits [4,7]) along with four bits to represent length
 * of the VLC code word (bits [0,3]) */

/* macro to obtain length of the coeff token information field, bits [0,4]  */
#define LENGTH_TC(vlc) ((vlc) & 0x1F)
/* macro to obtain length of the other information fields, bits [0,3] */
#define LENGTH(vlc) ((vlc) & 0xF)
/* macro to obtain code word from the information fields, bits [4,7] */
#define INFO(vlc) (((vlc) >> 4) & 0xF)  /* 4 MSB bits contain information */
/* macro to obtain trailing ones from the coeff token information word,
 * bits [5,10] */
#define TRAILING_ONES(coeff_token) ((coeff_token>>5) & 0x3F)
/* macro to obtain total coeff from the coeff token information word,
 * bits [11,15] */
#define TOTAL_COEFF(coeff_token) (((coeff_token) >> 11) & 0x1F)

#define VLC_NOT_FOUND 0xFFFFFFFEU

/* VLC tables for coeff_token. Because of long codes (max. 16 bits) some of the
 * tables have been splitted into multiple separate tables. Each array/table
 * element has the following structure:
 * [5 bits for tot.coeff.] [6 bits for tr.ones] [5 bits for VLC length]
 * If there is a 0x0000 value, it means that there is not corresponding VLC
 * codeword for that index. */

/* VLC lengths up to 6 bits, 0 <= nC < 2 */
static const u16 coeff_token0_0[32] = {
  0x0000, 0x0000, 0x0000, 0x2066, 0x1026, 0x0806, 0x1865, 0x1865,
  0x1043, 0x1043, 0x1043, 0x1043, 0x1043, 0x1043, 0x1043, 0x1043,
  0x0822, 0x0822, 0x0822, 0x0822, 0x0822, 0x0822, 0x0822, 0x0822,
  0x0822, 0x0822, 0x0822, 0x0822, 0x0822, 0x0822, 0x0822, 0x0822
};

/* VLC lengths up to 10 bits, 0 <= nC < 2 */
static const u16 coeff_token0_1[48] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x406a, 0x304a, 0x282a, 0x200a,
  0x3869, 0x3869, 0x2849, 0x2849, 0x2029, 0x2029, 0x1809, 0x1809,
  0x3068, 0x3068, 0x3068, 0x3068, 0x2048, 0x2048, 0x2048, 0x2048,
  0x1828, 0x1828, 0x1828, 0x1828, 0x1008, 0x1008, 0x1008, 0x1008,
  0x2867, 0x2867, 0x2867, 0x2867, 0x2867, 0x2867, 0x2867, 0x2867,
  0x1847, 0x1847, 0x1847, 0x1847, 0x1847, 0x1847, 0x1847, 0x1847
};

/* VLC lengths up to 14 bits, 0 <= nC < 2 */
static const u16 coeff_token0_2[56] = {
  0x606e, 0x584e, 0x502e, 0x500e, 0x586e, 0x504e, 0x482e, 0x480e,
  0x400d, 0x400d, 0x484d, 0x484d, 0x402d, 0x402d, 0x380d, 0x380d,
  0x506d, 0x506d, 0x404d, 0x404d, 0x382d, 0x382d, 0x300d, 0x300d,
  0x486b, 0x486b, 0x486b, 0x486b, 0x486b, 0x486b, 0x486b, 0x486b,
  0x384b, 0x384b, 0x384b, 0x384b, 0x384b, 0x384b, 0x384b, 0x384b,
  0x302b, 0x302b, 0x302b, 0x302b, 0x302b, 0x302b, 0x302b, 0x302b,
  0x280b, 0x280b, 0x280b, 0x280b, 0x280b, 0x280b, 0x280b, 0x280b
};

/* VLC lengths up to 16 bits, 0 <= nC < 2 */
static const u16 coeff_token0_3[32] = {
  0x0000, 0x0000, 0x682f, 0x682f, 0x8010, 0x8050, 0x8030, 0x7810,
  0x8070, 0x7850, 0x7830, 0x7010, 0x7870, 0x7050, 0x7030, 0x6810,
  0x706f, 0x706f, 0x684f, 0x684f, 0x602f, 0x602f, 0x600f, 0x600f,
  0x686f, 0x686f, 0x604f, 0x604f, 0x582f, 0x582f, 0x580f, 0x580f
};

/* VLC lengths up to 6 bits, 2 <= nC < 4 */
static const u16 coeff_token2_0[32] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x3866, 0x2046, 0x2026, 0x1006,
  0x3066, 0x1846, 0x1826, 0x0806, 0x2865, 0x2865, 0x1025, 0x1025,
  0x2064, 0x2064, 0x2064, 0x2064, 0x1864, 0x1864, 0x1864, 0x1864,
  0x1043, 0x1043, 0x1043, 0x1043, 0x1043, 0x1043, 0x1043, 0x1043
};

/* VLC lengths up to 9 bits, 2 <= nC < 4 */
static const u16 coeff_token2_1[32] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x4869, 0x3849, 0x3829, 0x3009,
  0x2808, 0x2808, 0x3048, 0x3048, 0x3028, 0x3028, 0x2008, 0x2008,
  0x4067, 0x4067, 0x4067, 0x4067, 0x2847, 0x2847, 0x2847, 0x2847,
  0x2827, 0x2827, 0x2827, 0x2827, 0x1807, 0x1807, 0x1807, 0x1807
};

/* VLC lengths up to 14 bits, 2 <= nC < 4 */
static const u16 coeff_token2_2[128] = {
  0x0000, 0x0000, 0x786d, 0x786d, 0x806e, 0x804e, 0x802e, 0x800e,
  0x782e, 0x780e, 0x784e, 0x702e, 0x704d, 0x704d, 0x700d, 0x700d,
  0x706d, 0x706d, 0x684d, 0x684d, 0x682d, 0x682d, 0x680d, 0x680d,
  0x686d, 0x686d, 0x604d, 0x604d, 0x602d, 0x602d, 0x600d, 0x600d,
  0x580c, 0x580c, 0x580c, 0x580c, 0x584c, 0x584c, 0x584c, 0x584c,
  0x582c, 0x582c, 0x582c, 0x582c, 0x500c, 0x500c, 0x500c, 0x500c,
  0x606c, 0x606c, 0x606c, 0x606c, 0x504c, 0x504c, 0x504c, 0x504c,
  0x502c, 0x502c, 0x502c, 0x502c, 0x480c, 0x480c, 0x480c, 0x480c,
  0x586b, 0x586b, 0x586b, 0x586b, 0x586b, 0x586b, 0x586b, 0x586b,
  0x484b, 0x484b, 0x484b, 0x484b, 0x484b, 0x484b, 0x484b, 0x484b,
  0x482b, 0x482b, 0x482b, 0x482b, 0x482b, 0x482b, 0x482b, 0x482b,
  0x400b, 0x400b, 0x400b, 0x400b, 0x400b, 0x400b, 0x400b, 0x400b,
  0x506b, 0x506b, 0x506b, 0x506b, 0x506b, 0x506b, 0x506b, 0x506b,
  0x404b, 0x404b, 0x404b, 0x404b, 0x404b, 0x404b, 0x404b, 0x404b,
  0x402b, 0x402b, 0x402b, 0x402b, 0x402b, 0x402b, 0x402b, 0x402b,
  0x380b, 0x380b, 0x380b, 0x380b, 0x380b, 0x380b, 0x380b, 0x380b
};

/* VLC lengths up to 6 bits, 4 <= nC < 8 */
static const u16 coeff_token4_0[64] = {
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x1806, 0x3846, 0x3826, 0x1006, 0x4866, 0x3046, 0x3026, 0x0806,
  0x2825, 0x2825, 0x2845, 0x2845, 0x2025, 0x2025, 0x2045, 0x2045,
  0x1825, 0x1825, 0x4065, 0x4065, 0x1845, 0x1845, 0x1025, 0x1025,
  0x3864, 0x3864, 0x3864, 0x3864, 0x3064, 0x3064, 0x3064, 0x3064,
  0x2864, 0x2864, 0x2864, 0x2864, 0x2064, 0x2064, 0x2064, 0x2064,
  0x1864, 0x1864, 0x1864, 0x1864, 0x1044, 0x1044, 0x1044, 0x1044,
  0x0824, 0x0824, 0x0824, 0x0824, 0x0004, 0x0004, 0x0004, 0x0004
};

/* VLC lengths up to 10 bits, 4 <= nC < 8 */
static const u16 coeff_token4_1[128] = {
  0x0000, 0x800a, 0x806a, 0x804a, 0x802a, 0x780a, 0x786a, 0x784a,
  0x782a, 0x700a, 0x706a, 0x704a, 0x702a, 0x680a, 0x6829, 0x6829,
  0x6009, 0x6009, 0x6849, 0x6849, 0x6029, 0x6029, 0x5809, 0x5809,
  0x6869, 0x6869, 0x6049, 0x6049, 0x5829, 0x5829, 0x5009, 0x5009,
  0x6068, 0x6068, 0x6068, 0x6068, 0x5848, 0x5848, 0x5848, 0x5848,
  0x5028, 0x5028, 0x5028, 0x5028, 0x4808, 0x4808, 0x4808, 0x4808,
  0x5868, 0x5868, 0x5868, 0x5868, 0x5048, 0x5048, 0x5048, 0x5048,
  0x4828, 0x4828, 0x4828, 0x4828, 0x4008, 0x4008, 0x4008, 0x4008,
  0x3807, 0x3807, 0x3807, 0x3807, 0x3807, 0x3807, 0x3807, 0x3807,
  0x3007, 0x3007, 0x3007, 0x3007, 0x3007, 0x3007, 0x3007, 0x3007,
  0x4847, 0x4847, 0x4847, 0x4847, 0x4847, 0x4847, 0x4847, 0x4847,
  0x2807, 0x2807, 0x2807, 0x2807, 0x2807, 0x2807, 0x2807, 0x2807,
  0x5067, 0x5067, 0x5067, 0x5067, 0x5067, 0x5067, 0x5067, 0x5067,
  0x4047, 0x4047, 0x4047, 0x4047, 0x4047, 0x4047, 0x4047, 0x4047,
  0x4027, 0x4027, 0x4027, 0x4027, 0x4027, 0x4027, 0x4027, 0x4027,
  0x2007, 0x2007, 0x2007, 0x2007, 0x2007, 0x2007, 0x2007, 0x2007
};

/* fixed 6 bit length VLC, nC <= 8 */
static const u16 coeff_token8[64] = {
  0x0806, 0x0826, 0x0000, 0x0006, 0x1006, 0x1026, 0x1046, 0x0000,
  0x1806, 0x1826, 0x1846, 0x1866, 0x2006, 0x2026, 0x2046, 0x2066,
  0x2806, 0x2826, 0x2846, 0x2866, 0x3006, 0x3026, 0x3046, 0x3066,
  0x3806, 0x3826, 0x3846, 0x3866, 0x4006, 0x4026, 0x4046, 0x4066,
  0x4806, 0x4826, 0x4846, 0x4866, 0x5006, 0x5026, 0x5046, 0x5066,
  0x5806, 0x5826, 0x5846, 0x5866, 0x6006, 0x6026, 0x6046, 0x6066,
  0x6806, 0x6826, 0x6846, 0x6866, 0x7006, 0x7026, 0x7046, 0x7066,
  0x7806, 0x7826, 0x7846, 0x7866, 0x8006, 0x8026, 0x8046, 0x8066
};

/* VLC lengths up to 3 bits, nC == -1 */
static const u16 coeff_token_minus1_0[8] = {
  0x0000, 0x1043, 0x0002, 0x0002, 0x0821, 0x0821, 0x0821, 0x0821
};

/* VLC lengths up to 8 bits, nC == -1 */
static const u16 coeff_token_minus1_1[32] = {
  0x2067, 0x2067, 0x2048, 0x2028, 0x1847, 0x1847, 0x1827, 0x1827,
  0x2006, 0x2006, 0x2006, 0x2006, 0x1806, 0x1806, 0x1806, 0x1806,
  0x1006, 0x1006, 0x1006, 0x1006, 0x1866, 0x1866, 0x1866, 0x1866,
  0x1026, 0x1026, 0x1026, 0x1026, 0x0806, 0x0806, 0x0806, 0x0806
};

/* VLC tables for total_zeros. One table containing longer code, totalZeros_1,
 * has been broken into two separate tables. Table elements have the
 * following structure:
 * [4 bits for info] [4 bits for VLC length] */

/* VLC lengths up to 5 bits */
static const u8 total_zeros_1_0[32] = {
  0x00, 0x00, 0x65, 0x55, 0x44, 0x44, 0x34, 0x34,
  0x23, 0x23, 0x23, 0x23, 0x13, 0x13, 0x13, 0x13,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};

/* VLC lengths up to 9 bits */
static const u8 total_zeros_1_1[32] = {
  0x00, 0xf9, 0xe9, 0xd9, 0xc8, 0xc8, 0xb8, 0xb8,
  0xa7, 0xa7, 0xa7, 0xa7, 0x97, 0x97, 0x97, 0x97,
  0x86, 0x86, 0x86, 0x86, 0x86, 0x86, 0x86, 0x86,
  0x76, 0x76, 0x76, 0x76, 0x76, 0x76, 0x76, 0x76
};

static const u8 total_zeros_2[64] = {
  0xe6, 0xd6, 0xc6, 0xb6, 0xa5, 0xa5, 0x95, 0x95,
  0x84, 0x84, 0x84, 0x84, 0x74, 0x74, 0x74, 0x74,
  0x64, 0x64, 0x64, 0x64, 0x54, 0x54, 0x54, 0x54,
  0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
  0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03
};

static const u8 total_zeros_3[64] = {
  0xd6, 0xb6, 0xc5, 0xc5, 0xa5, 0xa5, 0x95, 0x95,
  0x84, 0x84, 0x84, 0x84, 0x54, 0x54, 0x54, 0x54,
  0x44, 0x44, 0x44, 0x44, 0x04, 0x04, 0x04, 0x04,
  0x73, 0x73, 0x73, 0x73, 0x73, 0x73, 0x73, 0x73,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
  0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13
};

static const u8 total_zeros_4[32] = {
  0xc5, 0xb5, 0xa5, 0x05, 0x94, 0x94, 0x74, 0x74,
  0x34, 0x34, 0x24, 0x24, 0x83, 0x83, 0x83, 0x83,
  0x63, 0x63, 0x63, 0x63, 0x53, 0x53, 0x53, 0x53,
  0x43, 0x43, 0x43, 0x43, 0x13, 0x13, 0x13, 0x13
};

static const u8 total_zeros_5[32] = {
  0xb5, 0x95, 0xa4, 0xa4, 0x84, 0x84, 0x24, 0x24,
  0x14, 0x14, 0x04, 0x04, 0x73, 0x73, 0x73, 0x73,
  0x63, 0x63, 0x63, 0x63, 0x53, 0x53, 0x53, 0x53,
  0x43, 0x43, 0x43, 0x43, 0x33, 0x33, 0x33, 0x33
};

static const u8 total_zeros_6[64] = {
  0xa6, 0x06, 0x15, 0x15, 0x84, 0x84, 0x84, 0x84,
  0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93,
  0x73, 0x73, 0x73, 0x73, 0x73, 0x73, 0x73, 0x73,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x53, 0x53, 0x53, 0x53, 0x53, 0x53, 0x53, 0x53,
  0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23
};

static const u8 total_zeros_7[64] = {
  0x96, 0x06, 0x15, 0x15, 0x74, 0x74, 0x74, 0x74,
  0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43, 0x43,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
  0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52,
  0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52
};

static const u8 total_zeros_8[64] = {
  0x86, 0x06, 0x25, 0x25, 0x14, 0x14, 0x14, 0x14,
  0x73, 0x73, 0x73, 0x73, 0x73, 0x73, 0x73, 0x73,
  0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
  0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
  0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52,
  0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42
};

static const u8 total_zeros_9[64] = {
  0x16, 0x06, 0x75, 0x75, 0x24, 0x24, 0x24, 0x24,
  0x53, 0x53, 0x53, 0x53, 0x53, 0x53, 0x53, 0x53,
  0x62, 0x62, 0x62, 0x62, 0x62, 0x62, 0x62, 0x62,
  0x62, 0x62, 0x62, 0x62, 0x62, 0x62, 0x62, 0x62,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32
};

static const u8 total_zeros_10[32] = {
  0x15, 0x05, 0x64, 0x64, 0x23, 0x23, 0x23, 0x23,
  0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52, 0x52,
  0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32
};

static const u8 total_zeros_11[16] = {
  0x04, 0x14, 0x23, 0x23, 0x33, 0x33, 0x53, 0x53,
  0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41
};

static const u8 total_zeros_12[16] = {
  0x04, 0x14, 0x43, 0x43, 0x22, 0x22, 0x22, 0x22,
  0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31
};

static const u8 total_zeros_13[8] =
{ 0x03, 0x13, 0x32, 0x32, 0x21, 0x21, 0x21, 0x21 };

static const u8 total_zeros_14[4] = { 0x02, 0x12, 0x21, 0x21 };

/* VLC tables for run_before. Table elements have the following structure:
 * [4 bits for info] [4bits for VLC length]
 */

static const u8 run_before_6[8] =
{ 0x13, 0x23, 0x43, 0x33, 0x63, 0x53, 0x02, 0x02 };

static const u8 run_before_5[8] =
{ 0x53, 0x43, 0x33, 0x23, 0x12, 0x12, 0x02, 0x02 };

static const u8 run_before_4[8] =
{ 0x43, 0x33, 0x22, 0x22, 0x12, 0x12, 0x02, 0x02 };

static const u8 run_before_3[4] = { 0x32, 0x22, 0x12, 0x02 };

static const u8 run_before_2[4] = { 0x22, 0x12, 0x01, 0x01 };

static const u8 run_before_1[2] = { 0x11, 0x01 };

/* following four macros are used to handle stream buffer "cache" in the CAVLC
 * decoding function */

/* macro to initialize stream buffer cache, fills the buffer (32 bits) */
#define BUFFER_INIT(value, bits) \
{ \
    bits = 32; \
    value = h264bsdShowBits(p_strm_data,32); \
}

/* macro to read num_bits bits from the buffer, bits will be written to
 * out_val. Refills the buffer if not enough bits left */
#define BUFFER_SHOW(value, bits, out_val, num_bits) \
{ \
    if (bits < (num_bits)) \
    { \
        if(h264bsdFlushBits(p_strm_data,32-bits) == END_OF_STREAM) \
            return(~0); \
        value = h264bsdShowBits(p_strm_data,32); \
        bits = 32; \
    } \
    (out_val) = value >> (32 - (num_bits)); \
}

/* macro to flush num_bits bits from the buffer */
#define BUFFER_FLUSH(value, bits, num_bits) \
{ \
    value <<= (num_bits); \
    bits -= (num_bits); \
}

/* macro to read and flush  num_bits bits from the buffer, bits will be written
 * to out_val. Refills the buffer if not enough bits left */
#define BUFFER_GET(value, bits, out_val, num_bits) \
{ \
    if (bits < (num_bits)) \
    { \
        if(h264bsdFlushBits(p_strm_data,32-bits) == END_OF_STREAM) \
            return(~0); \
        value = h264bsdShowBits(p_strm_data,32); \
        bits = 32; \
    } \
    (out_val) = value >> (32 - (num_bits)); \
    value <<= (num_bits); \
    bits -= (num_bits); \
}

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 DecodeCoeffToken(u32 bits, u32 nc);

static u32 DecodeLevelPrefix(u32 bits);

static u32 DecodeTotalZeros(u32 bits, u32 total_coeff, u32 is_chroma_dc);

static u32 DecodeRunBefore(u32 bits, u32 zeros_left);

/*------------------------------------------------------------------------------

    Function: DecodeCoeffToken

        Functional description:
          Function to decode coeff_token information field from the stream.

        Inputs:
          u32 bits                  next 16 stream bits
          u32 nc                    nC, see standard for details

        Outputs:
          u32  information field (11 bits for value, 5 bits for length)

------------------------------------------------------------------------------*/

u32 DecodeCoeffToken(u32 bits, u32 nc) {

  /* Variables */

  u32 value;

  /* Code */

  /* standard defines that nc for decoding of chroma dc coefficients is -1,
   * represented by u32 here -> -1 maps to 2^32 - 1 */
  ASSERT(nc <= 16 || nc == (u32) (-1));

  if(nc < 2) {
    if(bits >= 0x8000) {
      value = 0x0001;
    } else if(bits >= 0x0C00)
      value = coeff_token0_0[bits >> 10];
    else if(bits >= 0x0100)
      value = coeff_token0_1[bits >> 6];
    else if(bits >= 0x0020)
      value = coeff_token0_2[(bits >> 2) - 8];
    else
      value = coeff_token0_3[bits];
  } else if(nc < 4) {
    if(bits >= 0x8000) {
      value = bits & 0x4000 ? 0x0002 : 0x0822;
    } else if(bits >= 0x1000)
      value = coeff_token2_0[bits >> 10];
    else if(bits >= 0x0200)
      value = coeff_token2_1[bits >> 7];
    else
      value = coeff_token2_2[bits >> 2];
  } else if(nc < 8) {
    value = coeff_token4_0[bits >> 10];
    if(!value)
      value = coeff_token4_1[bits >> 6];
  } else if(nc <= 16) {
    value = coeff_token8[bits >> 10];
  } else {
    value = coeff_token_minus1_0[bits >> 13];
    if(!value)
      value = coeff_token_minus1_1[bits >> 8];
  }

  return (value);

}

/*------------------------------------------------------------------------------

    Function: DecodeLevelPrefix

        Functional description:
          Function to decode level_prefix information field from the stream

        Inputs:
          u32 bits      next 16 stream bits

        Outputs:
          u32  level_prefix information field or VLC_NOT_FOUND

------------------------------------------------------------------------------*/

u32 DecodeLevelPrefix(u32 bits) {

  /* Variables */

  u32 num_zeros;

  /* Code */

  if(bits >= 0x8000)
    num_zeros = 0;
  else if(bits >= 0x4000)
    num_zeros = 1;
  else if(bits >= 0x2000)
    num_zeros = 2;
  else if(bits >= 0x1000)
    num_zeros = 3;
  else if(bits >= 0x0800)
    num_zeros = 4;
  else if(bits >= 0x0400)
    num_zeros = 5;
  else if(bits >= 0x0200)
    num_zeros = 6;
  else if(bits >= 0x0100)
    num_zeros = 7;
  else if(bits >= 0x0080)
    num_zeros = 8;
  else if(bits >= 0x0040)
    num_zeros = 9;
  else if(bits >= 0x0020)
    num_zeros = 10;
  else if(bits >= 0x0010)
    num_zeros = 11;
  else if(bits >= 0x0008)
    num_zeros = 12;
  else if(bits >= 0x0004)
    num_zeros = 13;
  else if(bits >= 0x0002)
    num_zeros = 14;
  else if(bits >= 0x0001)
    num_zeros = 15;
  else    /* more than 15 zeros encountered which is an error */
    return (VLC_NOT_FOUND);

  return (num_zeros);

}

/*------------------------------------------------------------------------------

    Function: DecodeTotalZeros

        Functional description:
          Function to decode total_zeros information field from the stream

        Inputs:
          u32 bits                  next 9 stream bits
          u32 total_coeff            total number of coefficients for the block
                                    being decoded
          u32 is_chroma_dc           flag to indicate chroma DC block

        Outputs:
          u32  information field (4 bits value, 4 bits length)

------------------------------------------------------------------------------*/

u32 DecodeTotalZeros(u32 bits, u32 total_coeff, u32 is_chroma_dc) {

  /* Variables */

  u32 value = 0x0;

  /* Code */

  ASSERT(total_coeff);

  if(!is_chroma_dc) {
    ASSERT(total_coeff < 16);
    switch (total_coeff) {
    case 1:
      value = total_zeros_1_0[bits >> 4];
      if(!value)
        value = total_zeros_1_1[bits];
      break;

    case 2:
      value = total_zeros_2[bits >> 3];
      break;

    case 3:
      value = total_zeros_3[bits >> 3];
      break;

    case 4:
      value = total_zeros_4[bits >> 4];
      break;

    case 5:
      value = total_zeros_5[bits >> 4];
      break;

    case 6:
      value = total_zeros_6[bits >> 3];
      break;

    case 7:
      value = total_zeros_7[bits >> 3];
      break;

    case 8:
      value = total_zeros_8[bits >> 3];
      break;

    case 9:
      value = total_zeros_9[bits >> 3];
      break;

    case 10:
      value = total_zeros_10[bits >> 4];
      break;

    case 11:
      value = total_zeros_11[bits >> 5];
      break;

    case 12:
      value = total_zeros_12[bits >> 5];
      break;

    case 13:
      value = total_zeros_13[bits >> 6];
      break;

    case 14:
      value = total_zeros_14[bits >> 7];
      break;

    default:   /* case 15 */
      value = (bits >> 8) ? 0x11 : 0x01;
      break;
    }
  } else {
    ASSERT(total_coeff < 4);
    bits >>= 6;
    if(bits > 3)
      value = 0x01;
    else {
      if(total_coeff == 3)
        value = 0x11;
      else if(bits > 1) {
        value = 0x12;
      } else if(total_coeff == 2)
        value = 0x22;
      else if(bits)
        value = 0x23;
      else
        value = 0x33;
    }
  }

  return (value);

}

/*------------------------------------------------------------------------------

    Function: DecodeRunBefore

        Functional description:
          Function to decode run_before information field from the stream

        Inputs:
          u32 bits                  next 11 stream bits
          u32 zeros_left             number of zeros left for the current block

        Outputs:
          u32  information field (4 bits value, 4 bits length)

------------------------------------------------------------------------------*/

u32 DecodeRunBefore(u32 bits, u32 zeros_left) {

  /* Variables */

  u32 value = 0x0;

  /* Code */

  switch (zeros_left) {
  case 1:
    value = run_before_1[bits >> 10];
    break;

  case 2:
    value = run_before_2[bits >> 9];
    break;

  case 3:
    value = run_before_3[bits >> 9];
    break;

  case 4:
    value = run_before_4[bits >> 8];
    break;

  case 5:
    value = run_before_5[bits >> 8];
    break;

  case 6:
    value = run_before_6[bits >> 8];
    break;

  default:
    if(bits >= 0x100)
      value = ((7 - (bits >> 8)) << 4) + 0x3;
    else if(bits >= 0x80)
      value = 0x74;
    else if(bits >= 0x40)
      value = 0x85;
    else if(bits >= 0x20)
      value = 0x96;
    else if(bits >= 0x10)
      value = 0xa7;
    else if(bits >= 0x8)
      value = 0xb8;
    else if(bits >= 0x4)
      value = 0xc9;
    else if(bits >= 0x2)
      value = 0xdA;
    else if(bits)
      value = 0xeB;
    if(INFO(value) > zeros_left)
      value = 0;
    break;
  }

  return (value);

}

/*------------------------------------------------------------------------------

    Function: DecodeResidualBlockCavlc

        Functional description:
          Function to decode one CAVLC coded block. This corresponds to
          syntax elements residual_block_cavlc() in the standard.

        Inputs:
          p_strm_data             pointer to stream data structure
          nc                    nC value
          max_num_coeff           maximum number of residual coefficients

        Outputs:
          coeff_level            stores decoded coefficient levels

        Returns:
          total_coeff            numebr of coeffs decoded
          (~0)                  end of stream or error in stream

------------------------------------------------------------------------------*/

u32 h264bsdDecodeResidualBlockCavlc(strmData_t * p_strm_data,
                                    u16 * coeff_level, i32 nc, u32 max_num_coeff) {

  /* Variables */

  u32 i, tmp, total_coeff, trailing_ones, suffix_length, level_prefix;
  u32 level_suffix, zeros_left, bit;
  i32 level[16];
  u32 run[16];

  /* stream "cache" */
  u32 buffer_value;
  u32 buffer_bits;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(coeff_level);
  ASSERT(nc > -2);
  ASSERT(max_num_coeff == 4 || max_num_coeff == 15 || max_num_coeff == 16);
  ASSERT(VLC_NOT_FOUND != END_OF_STREAM);

  /* assume that coeff_level array has been "cleaned" by caller */

  BUFFER_INIT(buffer_value, buffer_bits);

  /*lint -e774 disable lint warning on always false comparison */
  BUFFER_SHOW(buffer_value, buffer_bits, bit, 16);
  /*lint +e774 */
  tmp = DecodeCoeffToken(bit, (u32) nc);
  if(!tmp)
    return (~0);

  BUFFER_FLUSH(buffer_value, buffer_bits, LENGTH_TC(tmp));

  total_coeff = TOTAL_COEFF(tmp);
  if(total_coeff > max_num_coeff)
    return (~0);

  trailing_ones = TRAILING_ONES(tmp);

  if(total_coeff != 0) {
    i = 0;
    /* nonzero coefficients: +/- 1 */
    if(trailing_ones) {
      BUFFER_GET(buffer_value, buffer_bits, bit, trailing_ones);
      tmp = 1 << (trailing_ones - 1);
      for(; tmp; i++) {
        level[i] = bit & tmp ? -1 : 1;
        tmp >>= 1;
      }
    }

    /* other levels */
    if(total_coeff > 10 && trailing_ones < 3)
      suffix_length = 1;
    else
      suffix_length = 0;

    for(; i < total_coeff; i++) {
      BUFFER_SHOW(buffer_value, buffer_bits, bit, 16);
      level_prefix = DecodeLevelPrefix(bit);
      if(level_prefix == VLC_NOT_FOUND)
        return (~0);

      BUFFER_FLUSH(buffer_value, buffer_bits, level_prefix + 1);

      if(level_prefix < 14)
        tmp = suffix_length;
      else if(level_prefix == 14) {
        tmp = suffix_length ? suffix_length : 4;
      } else {
        /* setting suffix_length to 1 here corresponds to adding 15
         * to levelCode value if level_prefix == 15 and
         * suffix_length == 0 */
        if(!suffix_length)
          suffix_length = 1;
        tmp = 12;
      }

      if(suffix_length)
        level_prefix <<= suffix_length;

      if(tmp) {
        BUFFER_GET(buffer_value, buffer_bits, level_suffix, tmp);
        level_prefix += level_suffix;
      }

      tmp = level_prefix;

      if(i == trailing_ones && trailing_ones < 3)
        tmp += 2;

      level[i] = (tmp + 2) >> 1;

      if(suffix_length == 0)
        suffix_length = 1;

      if((level[i] > (3 << (suffix_length - 1))) && suffix_length < 6)
        suffix_length++;

      if(tmp & 0x1)
        level[i] = -level[i];
    }

    /* zero runs */
    if(total_coeff < max_num_coeff) {
      BUFFER_SHOW(buffer_value, buffer_bits, bit, 9);
      zeros_left = DecodeTotalZeros(bit, total_coeff,
                                    (u32) (max_num_coeff == 4));
      if(!zeros_left)
        return (~0);

      BUFFER_FLUSH(buffer_value, buffer_bits, LENGTH(zeros_left));
      zeros_left = INFO(zeros_left);
    } else
      zeros_left = 0;

    if((zeros_left + total_coeff) > max_num_coeff) {
      return (~0);
    }

    for(i = 0; i < total_coeff - 1; i++) {
      if(zeros_left > 0) {
        BUFFER_SHOW(buffer_value, buffer_bits, bit, 11);
        tmp = DecodeRunBefore(bit, zeros_left);
        if(!tmp)
          return (~0);

        BUFFER_FLUSH(buffer_value, buffer_bits, LENGTH(tmp));
        run[i] = INFO(tmp);
        zeros_left -= run[i];
      } else {
        run[i] = 0;
      }
    }

    run[i] = zeros_left; /* last run */

    /*lint -esym(771,level,run) level and run are always initialized */
    {
      u16 *p_tmp = coeff_level + 2;
      u16 big_level_mask = 0;

      for(i = total_coeff; i--;) {
        big_level_mask <<= 1;
        if(level[i] < 0)
          big_level_mask |= 1;

        tmp = (run[i] << 12) | (level[i] & 0x0FFF);

        *p_tmp++ = (u16) tmp;
      }

      tmp = total_coeff << 11;
      if(big_level_mask) {
        tmp |= 1;
        big_level_mask <<= (16 - total_coeff);
      }

      *coeff_level++ = (u16) tmp;
      *coeff_level = big_level_mask;
    }

  } else {
    *coeff_level = 0;    /* no coeffs for this subBlock */
  }

  if(h264bsdFlushBits(p_strm_data, 32 - buffer_bits) != HANTRO_OK)
    return (~0);

  return total_coeff;
}
