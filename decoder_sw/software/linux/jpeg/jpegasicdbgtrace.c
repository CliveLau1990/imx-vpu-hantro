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

#include "dwl.h"
#include "jpegregdrv.h"
#include "jpegasicdbgtrace.h"
#include "basetype.h"
#include "jpegdeccontainer.h"
#include <stdio.h>
#include <assert.h>

#ifdef JPEGDEC_PP_TRACE
#include "pphwregdrv_g1.h"
#endif /* #ifdef JPEGDEC_PP_TRACE */

#ifdef JPEGDEC_ASIC_TRACE
static u32 slice_counter = 0;
#endif /* #ifdef JPEGDEC_ASIC_TRACE */

/*------------------------------------------------------------------------------
    2. External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/
#ifdef JPEGDEC_ASIC_TRACE
/*------------------------------------------------------------------------------
    5. Functions
------------------------------------------------------------------------------*/
void DumpJPEGCtrlReg(u32 * reg_base, FILE * fd) {
  assert(fd);

  fprintf(fd,
          "------------------ picture = %d ------------------------------------\n",
          slice_counter);
  fprintf(fd, "%-10d decoding mode (0=h264, 1=mpeg4, 2=h263, 3=jpeg)\n",
          GetJpegDecodingMode(reg_base));
  fprintf(fd, "%-10d RLC mode enable (0=HW decodes VLC)\n",
          GetJpegDecRlcModeDisable(reg_base));
  fprintf(fd, "%-10d Picture width in macro blocks\n",
          GetJpegDecPictureWidth(reg_base));
  fprintf(fd, "%-10d Picture height in macro blocks\n",
          GetJpegDecPictureHeight(reg_base));
  fprintf(fd, "%-10d JPEG sampling format\n",
          GetJpegDecDecodingInputFormat(reg_base));
  fprintf(fd, "%-10d amount of quantization tables\n",
          GetJpegDecAmountOfQpTables(reg_base));
  /*fprintf(fd,"%-10d amount of rlc/pic\n",slice_counter); */
  fprintf(fd,
          "%-10d Defines which VLC table should be used for decoding AC for Cr\n",
          GetJpegDecAcTableSelectorBitCr(reg_base));
  fprintf(fd,
          "%-10d Defines which VLC table should be used for decoding AC for Cb\n",
          GetJpegDecAcTableSelectorBitCb(reg_base));
  fprintf(fd,
          "%-10d Defines which VLC table should be used for decoding DC for Cr\n",
          GetJpegDecDcTableSelectorBitCr(reg_base));
  fprintf(fd,
          "%-10d Defines which VLC table should be used for decoding DC for Cb\n",
          GetJpegDecDcTableSelectorBitCb(reg_base));
  fprintf(fd,
          "%-10d Defines which VLC table should be used for decoding QP for Cr\n",
          GetJpegDecQpTableSelectorBitCr(reg_base));
  fprintf(fd,
          "%-10d Defines which VLC table should be used for decoding QP for Cb\n",
          GetJpegDecQpTableSelectorBitCb(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 16\n",
          GetJpegDecAc1Len16(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 15\n",
          GetJpegDecAc1Len15(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 14\n",
          GetJpegDecAc1Len14(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 13\n",
          GetJpegDecAc1Len13(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 12\n",
          GetJpegDecAc1Len12(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 11\n",
          GetJpegDecAc1Len11(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 10\n",
          GetJpegDecAc1Len10(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 9\n",
          GetJpegDecAc1Len9(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 8\n",
          GetJpegDecAc1Len8(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 7\n",
          GetJpegDecAc1Len7(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 6\n",
          GetJpegDecAc1Len6(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 5\n",
          GetJpegDecAc1Len5(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 4\n",
          GetJpegDecAc1Len4(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 3\n",
          GetJpegDecAc1Len3(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 2\n",
          GetJpegDecAc1Len2(reg_base));
  fprintf(fd, "%-10d AC code words in table 1 of length 1\n",
          GetJpegDecAc1Len1(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 16\n",
          GetJpegDecAc2Len16(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 15\n",
          GetJpegDecAc2Len15(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 14\n",
          GetJpegDecAc2Len14(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 13\n",
          GetJpegDecAc2Len13(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 12\n",
          GetJpegDecAc2Len12(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 11\n",
          GetJpegDecAc2Len11(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 10\n",
          GetJpegDecAc2Len10(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 9\n",
          GetJpegDecAc2Len9(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 8\n",
          GetJpegDecAc2Len8(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 7\n",
          GetJpegDecAc2Len7(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 6\n",
          GetJpegDecAc2Len6(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 5\n",
          GetJpegDecAc2Len5(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 4\n",
          GetJpegDecAc2Len4(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 3\n",
          GetJpegDecAc2Len3(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 2\n",
          GetJpegDecAc2Len2(reg_base));
  fprintf(fd, "%-10d AC code words in table 2 of length 1\n",
          GetJpegDecAc2Len1(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 16\n",
          GetJpegDecDc1Len16(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 15\n",
          GetJpegDecDc1Len15(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 14\n",
          GetJpegDecDc1Len14(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 13\n",
          GetJpegDecDc1Len13(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 12\n",
          GetJpegDecDc1Len12(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 11\n",
          GetJpegDecDc1Len11(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 10\n",
          GetJpegDecDc1Len10(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 9\n",
          GetJpegDecDc1Len9(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 8\n",
          GetJpegDecDc1Len8(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 7\n",
          GetJpegDecDc1Len7(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 6\n",
          GetJpegDecDc1Len6(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 5\n",
          GetJpegDecDc1Len5(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 4\n",
          GetJpegDecDc1Len4(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 3\n",
          GetJpegDecDc1Len3(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 2\n",
          GetJpegDecDc1Len2(reg_base));
  fprintf(fd, "%-10d DC code words in table 1 of length 1\n",
          GetJpegDecDc1Len1(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 16\n",
          GetJpegDecDc2Len16(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 15\n",
          GetJpegDecDc2Len15(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 14\n",
          GetJpegDecDc2Len14(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 13\n",
          GetJpegDecDc2Len13(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 12\n",
          GetJpegDecDc2Len12(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 11\n",
          GetJpegDecDc2Len11(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 10\n",
          GetJpegDecDc2Len10(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 9\n",
          GetJpegDecDc2Len9(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 8\n",
          GetJpegDecDc2Len8(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 7\n",
          GetJpegDecDc2Len7(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 6\n",
          GetJpegDecDc2Len6(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 5\n",
          GetJpegDecDc2Len5(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 4\n",
          GetJpegDecDc2Len4(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 3\n",
          GetJpegDecDc2Len3(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 2\n",
          GetJpegDecDc2Len2(reg_base));
  fprintf(fd, "%-10d DC code words in table 2 of length 1\n",
          GetJpegDecDc2Len1(reg_base));

  slice_counter++;
}

void HexDumpJPEGCtrlReg(u32 * reg_base, FILE * fd) {
  assert(fd);

  fprintf(fd, "%08x\n", GetJpegDecodingMode(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecRlcModeDisable(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecPictureWidth(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecPictureHeight(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDecodingInputFormat(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAmountOfQpTables(reg_base));
  /*fprintf(fd,"%08x\n",slice_counter); */
  fprintf(fd, "%08x\n", GetJpegDecAcTableSelectorBitCr(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAcTableSelectorBitCb(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDcTableSelectorBitCr(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDcTableSelectorBitCb(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecQpTableSelectorBitCr(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecQpTableSelectorBitCb(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len16(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len15(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len14(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len13(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len12(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len11(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len10(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len9(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len8(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len7(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len6(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len5(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len4(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len3(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len2(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc1Len1(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len16(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len15(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len14(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len13(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len12(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len11(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len10(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len9(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len8(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len7(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len6(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len5(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len4(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len3(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len2(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecAc2Len1(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len16(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len15(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len14(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len13(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len12(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len11(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len10(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len9(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len8(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len7(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len6(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len5(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len4(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len3(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len2(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc1Len1(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len16(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len15(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len14(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len13(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len12(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len11(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len10(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len9(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len8(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len7(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len6(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len5(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len4(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len3(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len2(reg_base));
  fprintf(fd, "%08x\n", GetJpegDecDc2Len1(reg_base));
}

void HexDumpJPEGTables(u32 * reg_base, JpegDecContainer * jpeg_dec_cont,
                       FILE * fd) {
  u32 i, j = 0;
  u32 amount_of_param = 0;
  u32 count = 0;
  u32 table_word = 0;
  u32 table_word2 = 0;
  u8 table_byte[8] = { 0 };
  u32 *p_tables = NULL;

  assert(fd);

  /* pointer to table memory */
  p_tables = jpeg_dec_cont->frame.p_table_base.virtual_address;

  /* calculate amount of table variables */
  /* qp */
  amount_of_param = (jpeg_dec_cont->info.amount_of_qtables * 64);
  /* huffman */
  for(i = 0; i < 16; i++) {
    /* ac tables */
    amount_of_param += jpeg_dec_cont->vlc.ac_table0.bits[i];
    amount_of_param += jpeg_dec_cont->vlc.ac_table1.bits[i];
    /* dc tables */
    amount_of_param += jpeg_dec_cont->vlc.dc_table0.bits[i];
    amount_of_param += jpeg_dec_cont->vlc.dc_table1.bits[i];
  }

  /* round up to 64-bit memory */
  if((JPEGDEC_TABLE_SIZE % 8) != 0) {
    do {
      count++;
    } while(((JPEGDEC_TABLE_SIZE + count) % 8) != 0);
  }

  /* write data to trace */
  for(i = 0; i < ((JPEGDEC_TABLE_SIZE + count) / 4); i++) {
    table_word = p_tables[i];
    table_word2 = p_tables[i + 1];

    table_byte[0] = ((table_word & 0xFF000000) >> 24);
    table_byte[1] = ((table_word & 0x00FF0000) >> 16);
    table_byte[2] = ((table_word & 0x0000FF00) >> 8);
    table_byte[3] = ((table_word & 0x000000FF) >> 0);
    table_byte[4] = ((table_word2 & 0xFF000000) >> 24);
    table_byte[5] = ((table_word2 & 0x00FF0000) >> 16);
    table_byte[6] = ((table_word2 & 0x0000FF00) >> 8);
    table_byte[7] = ((table_word2 & 0x000000FF) >> 0);
    i++;

    if(i >= (JPEGDEC_TABLE_SIZE / 4)) {
      table_byte[4] = 0;
      table_byte[5] = 0;
      table_byte[6] = 0;
      table_byte[7] = 0;

    }

    if(i < (JPEGDEC_TABLE_SIZE / 4)) {
      for(j = 0; j < 8; j++) {
        fprintf(fd, "%02x ", table_byte[j]);
      }

      if((i) % 1 == 0) {
        fprintf(fd, "\n");
      }
    } else {
      for(j = 0; j < 4; j++) {
        fprintf(fd, "%02x ", table_byte[j]);
      }

      for(j = 0; j < count; j++) {
        fprintf(fd, "0  ");
        i++;
      }
    }
  }
  fprintf(fd, "\n");
}

void HexDumpRegs(u32 * reg_base, FILE * fd) {
  i32 i;

  assert(fd);

  fprintf(fd, "\n");

  for(i = 0; i < (DEC_X170_REGISTERS); i++) {
    fprintf(fd, "Offset %02x: %08x\n", i, reg_base[i]);
  }
  fprintf(fd, "\n");
}

void PrintJPEGReg(u32 * reg_base) {
  u32 reg, regs = 0;

  for(reg = 0; reg < DEC_X170_REGISTERS; reg++) {
    regs = reg_base[reg];
    printf("swreg%-10d  0x%08x\n", reg, regs);
  }
  printf("\n");
}

#endif /* #ifdef JPEGDEC_ASIC_TRACE */

#ifdef JPEGDEC_PP_TRACE

void ppRegDump(const u32 * reg_base) {

  printf("GetPpInFormat \t%d\n", GetPpInFormat(reg_base));
  printf("GetPpOutFormat(  ) %d\n", GetPpOutFormat(reg_base));
  printf("GetPpOutHeight \t%d\n", GetPpOutHeight(reg_base));
  printf("GetPpOutWidth \t%d\n", GetPpOutWidth(reg_base));
  printf("GetPpCtrlDecPipeline \t%d\n", GetPpCtrlDecPipeline(reg_base));
  printf("GetPpCtrlIrqDisabled \t%d\n", GetPpCtrlIrqDisabled(reg_base));
  printf("GetPpCtrlIrqStat \t%d\n", GetPpCtrlIrqStat(reg_base));
  printf("GetPpCtrlIrqStat \t%d\n", GetPpCtrlIrqStat(reg_base));

  printf("GetPpInHeight \t%d\n", GetPpInHeight(reg_base));
  printf("GetPpInWidth \t%d\n", GetPpInWidth(reg_base));

  printf("GetPpFrameBufferWidth(  ) %d\n", GetPpFrameBufferWidth(reg_base));
  printf("GetPpOutLumaOrRgbAddr \t%x\n", GetPpOutLumaOrRgbAddr(reg_base));
  printf("GetPpOutChromaAddr \t%x\n", GetPpOutChromaAddr(reg_base));
  printf("GetPpOutEndianess \t%x\n", GetPpOutEndianess(reg_base));
  printf("GetPpInEndianess \t%d\n", GetPpInEndianess(reg_base));

  printf("GetPpInLumAddr \t%x\n", GetPpInLumAddr(reg_base));
  printf("GetPpInCbAddr \t%x\n", GetPpInCbAddr(reg_base));
  printf("GetPpInCrAddr \t%x\n", GetPpInCrAddr(reg_base));

  printf("GetPpInYOffset \t%d\n", GetPpInYOffset(reg_base));
  printf("GetPpInXOffset \t%d\n", GetPpInXOffset(reg_base));
  printf("GetPpInCroppedWidth \t%d\n", GetPpInCroppedWidth(reg_base));
  printf("GetPpInEndianess \t%d\n", GetPpInEndianess(reg_base));

  printf("GetPpCtrlAmbaBurstLength \t%d\n",
         GetPpCtrlAmbaBurstLength(reg_base));

  printf("GetPpContrastThr1 \t%d\n", GetPpContrastThr1(reg_base));
  printf("GetPpContrastThr1 \t%d\n", GetPpContrastThr1(reg_base));
  printf("GetPpContrastThr2 \t%d\n", GetPpContrastThr2(reg_base));
  printf("GetPpContrastOffset1 \t%d\n", GetPpContrastOffset1(reg_base));
  printf("GetPpContrastOffset2 \t%d\n", GetPpContrastOffset2(reg_base));

  printf("\n");
}

#endif /* #ifdef JPEGDEC_PP_TRACE */
