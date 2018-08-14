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
#include "mp4dechwd_container.h"
#include "mp4deccfg.h"
#include "mp4decapi.h"
#include "mp4decdrv.h"
#include "mp4debug.h"

#include "mp4dechwd_utils.h"

#include <stdio.h>
#include <assert.h>

void MP4RegDump(const void *dwl) {
#if 0
  MP4DEC_DEBUG(("GetMP4DecId   %x\n", GetMP4DecId(dwl)));
  MP4DEC_DEBUG(("GetMP4DecPictureWidth  %d\n", GetMP4DecPictureWidth(dwl)));
  MP4DEC_DEBUG(("GetMP4DecPictureHeight   %d\n", GetMP4DecPictureHeight(dwl)));
  MP4DEC_DEBUG(("GetMP4DecondingMode   %d\n", GetMP4DecDecondingMode(dwl)));
  MP4DEC_DEBUG(("GetMP4RoundingCtrl   %d\n", GetMP4DecRoundingCtrl(dwl)));
  MP4DEC_DEBUG(("GetMP4DecDisableOutputWrite   %d\n",
                GetMP4DecDisableOutputWrite(dwl)));
  MP4DEC_DEBUG(("GetMP4DecFilterDisable   %d\n", GetMP4DecFilterDisable(dwl)));
  MP4DEC_DEBUG(("GetMP4DecChromaQpOffset  %d\n", GetMP4DecChromaQpOffset(dwl)));
  /*    MP4DEC_DEBUG(("GetMP4DecIrqEnable   %d\n",GetMP4DecIrqEnable(dwl)));*/
  MP4DEC_DEBUG(("GetMP4DecIrqStat   %d\n", GetMP4DecIrqStat(dwl)));
  MP4DEC_DEBUG(("GetMP4DecIrqLine   %d\n", GetMP4DecIrqLine(dwl)));
  MP4DEC_DEBUG(("GetMP4DecEnable   %d\n", GetMP4DecEnable(dwl)));
  MP4DEC_DEBUG(("GetMP4DecMbCtrlBaseAddress   %x\n",
                GetMP4DecMbCtrlBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecMvBaseAddress   %x\n", GetMP4DecMvBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecDcCompBaseAddress   %x\n",
                GetMP4DecDcCompBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecRlcBaseAddress   %x\n", GetMP4DecRlcBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecRefPictBaseAddress   %x\n",
                GetMP4DecRefPictBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecRlcAmount   %d\n", GetMP4DecRlcAmount(dwl)));
  MP4DEC_DEBUG(("GetMP4DecOutputPictBaseAddress   %x\n",
                GetMP4DecOutputPictBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecVopFCode   %d\n", GetMP4DecVopFCode(dwl)));
  MP4DEC_DEBUG(("GetMP4DecOutputPictEndian   %d\n", GetMP4DecOutputPictEndian(dwl)));
  MP4DEC_DEBUG(("GetMP4DecAmbaBurstLength   %d\n", GetMP4DecAmbaBurstLength(dwl)));
  MP4DEC_DEBUG(("GetMP4DecAsicServicePriority   %d\n",
                GetMP4DecAsicServicePriority(dwl)));
#else
  UNUSED(dwl);
#endif
}

static u32 qp_array[MP4API_DEC_MBS];

void WriteAsicCtrl(DecContainer * dec_container) {
  FILE *fctrl = 0;
  FILE *fctrla = 0;
  FILE *fmv = 0;
  FILE *fdc = 0;
  FILE *fdc_hex = 0;
  FILE *hex_motion_vectors = 0;

  u32 i, j, tmp2, tmp = 0;
  i32 tmp3 = 0, hor_mv = 0, ver_mv = 0;
  u32 *p_mv, *p_dc;
  extern const u8 asic_pos_no_rlc[6];

  printf("AsicCtrl \n");
  fflush(stdout);
  fctrl = fopen("mbcontrol.hex", "at");
  fctrla = fopen("mbcontrol.trc", "at");
  fmv = fopen("motion_vectors.trc", "at");
  hex_motion_vectors = fopen("motion_vectors.hex", "at");
  fdc = fopen("dc_separate_coeffs.trc", "at");
  fdc_hex = fopen("dc_separate_coeffs.hex", "at");
  if(fctrl == NULL || fctrla == NULL ||
      fmv == NULL || fdc == NULL || hex_motion_vectors == NULL || fdc_hex == NULL) {
    return;
  }

  if(fctrl != NULL) {
    /*p_dc = dec_container->MbSetDesc.p_dc_coeff_data_addr;
     * p_mv = dec_container->MbSetDesc.p_mv_data_addr; */
    for(j = 0; j < dec_container->VopDesc.total_mb_in_vop; j++) {

      p_dc = &dec_container->MbSetDesc.
             p_dc_coeff_data_addr[NBR_DC_WORDS_MB * j];
      p_mv = &dec_container->MbSetDesc.
             p_mv_data_addr[NBR_MV_WORDS_MB * j];

      for(tmp = 0; tmp > 1000; tmp++) {
        *(dec_container->MbSetDesc.
          p_dc_coeff_data_addr + tmp) = 0;
      }
      /* HEX */

      /* invert bits for no RLC data */

      tmp = dec_container->MbSetDesc.
            p_ctrl_data_addr[NBR_OF_WORDS_MB * j];

      fprintf(fctrl, "%08x\n",
              dec_container->MbSetDesc.
              p_ctrl_data_addr[NBR_OF_WORDS_MB * j]);

      /* invert all block no rlc bits in a loop for each block */

      for(i = 0; i < 6; i++) {
        tmp ^= (0x1 << asic_pos_no_rlc[i]);

      }

      /* DEC */

      tmp2 = dec_container->MbSetDesc.
             p_ctrl_data_addr[NBR_OF_WORDS_MB * j];

      /* NOTE! Also prints the separ DC / 4MV info */
      fprintf(fctrla, "%-3d", (tmp2 >> ASICPOS_4MV) & 0x3);
      fprintf(fctrla, "%-3d", (tmp2 >> ASICPOS_VPBI) & 0x1);
      fprintf(fctrla, "%-3d", (tmp2 >> ASICPOS_ACPREDFLAG) & 0x1);

      for(i = 0; i < 6; i++) {
        fprintf(fctrla, "%-3d", ((tmp2 >> asic_pos_no_rlc[i]) & 0x1));
      }

      fprintf(fctrla, "%-3d", (tmp2 >> ASICPOS_QP) & 0x1F);
      /*fprintf(fctrla, "%d", (tmp2) & 0x1FF); */

      fprintf(fctrla, "%-3d", (tmp2 >> ASICPOS_CONCEAL) & 0x1);

      fprintf(fctrla, "%-3d", (tmp2 >> ASICPOS_MBNOTCODED) & 0x1);

      if(dec_container->VopDesc.vop_coded)
        qp_array[j] = (tmp2 >> ASICPOS_QP) & 0x1F;

      fprintf(fctrla, "  picture = %d, mb = %d\n",
              dec_container->VopDesc.vop_number - 1, j);

      if((tmp2 & (1 << ASICPOS_USEINTRADCVLC)) && MB_IS_INTRA(j)) {

        /* Separate DC */
        for(i = 0; i < 2; i++) {
          /* TODO 1ff instead of 3ff ?  */
          tmp = *p_dc++;
          fprintf(fdc_hex, "%x\n",  tmp);
          /* trick for the correct sign */
          tmp3 = (tmp >> 20) & 0x1FF;
          tmp3 <<= 23;
          tmp3 >>= 23;
          fprintf(fdc, "%-3d ", tmp3);
          tmp3 = (tmp >> 10) & 0x1FF;
          tmp3 <<= 23;
          tmp3 >>= 23;
          fprintf(fdc, "%-3d ", tmp3);
          tmp3 = (tmp & 0x1FF);
          tmp3 <<= 23;
          tmp3 >>= 23;
          fprintf(fdc, "%-3d ", tmp3);

        }
        fprintf(fdc, " picture = %d, mb = %-3d\n",
                dec_container->VopDesc.vop_number - 1, j);
      } else {
        for(i = 0; i < 6; i++) {
          fprintf(fdc, "%-3d ", 0);
          if(i&2)/* Do twice*/
            fprintf(fdc_hex, "%x\n",  0);

        }
        fprintf(fdc, " picture = %d, mb = %-3d\n",
                dec_container->VopDesc.vop_number - 1, j);
      }

      if(fmv && MB_IS_INTER(j)) {

        if(MB_IS_INTER4V(j)) {

          /*MP4DEC_DEBUG(("mv pointer p_mv %x\n", p_mv)); */
          for(i = 0; i < 4; i++) {

            if(hex_motion_vectors) {
              fprintf(hex_motion_vectors, "%x\n", *p_mv);
            }
            tmp = *p_mv++;

            tmp2 = (tmp >> 17) & 0x7FFF;
            if(tmp2 > 0x1FFF) {
              hor_mv = (i32) (((~0) << 14) | tmp2);
            } else {
              hor_mv = (i32) tmp2;
            }

            tmp2 = (tmp >> 4) & 0x1FFF;
            if(tmp2 > 0x7FF) {
              ver_mv = (i32) (((~0) << 12) | tmp2);
            } else {
              ver_mv = (i32) tmp2;
            }

            fprintf(fmv, "%-3d ", hor_mv);
            fprintf(fmv, "%-3d 0 ", ver_mv);
            fprintf(fmv, "picture = %d, mb = %d\n",
                    dec_container->VopDesc.vop_number - 1, j);

          }
        } else {
          for(i = 0; i < 1; i++) {

            tmp = *p_mv++;

            tmp2 = (tmp >> 17) & 0x7FFF;
            if(tmp2 > 0x1FFF) {
              hor_mv = (i32) (((~0) << 14) | tmp2);
            } else {
              hor_mv = (i32) tmp2;
            }

            tmp2 = (tmp >> 4) & 0x1FFF;
            if(tmp2 > 0x7FF) {
              ver_mv = (i32) (((~0) << 12) | tmp2);
            } else {
              ver_mv = (i32) tmp2;
            }
            for(i = 0; i < 4; i++) {
              if(hex_motion_vectors) {
                fprintf(hex_motion_vectors, "%x\n", *(p_mv - 1));
              }
              fprintf(fmv, "%-3d ", hor_mv);
              fprintf(fmv, "%-3d 0 ", ver_mv);
              fprintf(fmv, "picture = %d, mb = %d\n",
                      dec_container->VopDesc.vop_number - 1,
                      j);
            }
          }
        }

      } else {
        /* intra mb, write zero vectors */
        for(i = 0; i < 4; i++) {
          if(hex_motion_vectors) {
            fprintf(hex_motion_vectors, "0\n");
          }
          fprintf(fmv, "0 ");
          fprintf(fmv, "  0   0 ");
          fprintf(fmv, "picture = %d, mb = %d\n",
                  dec_container->VopDesc.vop_number - 1, j);
        }
      }
    }
  }
  if(fctrl)
    fclose(fctrl);
  if(fctrla)
    fclose(fctrla);
  if(fmv)
    fclose(fmv);
  if(fdc)
    fclose(fdc);
  if(fdc_hex)
    fclose(fdc_hex);
  if(hex_motion_vectors)
    fclose(hex_motion_vectors);
}

void WriteAsicRlc(DecContainer * dec_container, u32 * phalves, u32 * pnum_addr) {
  FILE *frlc;
  FILE *frlca;

  u32 tmp, tmp2, i, coded = 0;
  u32 *ptr;
  static u32 count = 0;
  u32 put_comment = 1;
  u32 mb = 0, block = 0;
  u32 check_disable = 0;
  u32 halves = 0;
  u32 num_addr = 0;
  extern const u8 asic_pos_no_rlc[6];

  frlc = fopen("rlc.hex", "at");
  frlca = fopen("rlc.trc", "at");

  if(frlc == NULL || frlca == NULL) {
    return;
  }

  printf("AsicRlc\n");
  fflush(stdout);

  num_addr = 0;
  halves = 0;
  ptr = dec_container->MbSetDesc.p_rlc_data_addr;

  /* compute number of addresses to be written */
  /*    for (i=0;i<6*dec_container->MbSetDesc.
              nbrOfMbInMbSet;i++)
      {
          num_addr += (dec_container->ApiStorage.pNbrRlc[i])&0x3F;
      }*/
#if 0
  fprintf(frlc, "Picture = %d\n",
          /*
           * tmp2 & 0xFFFF, */
          dec_container->VopDesc.vop_number - 1);
#endif

  num_addr = (num_addr + 0xf) & ~(0x7);

  /*    for(i=1000;i<3500;i++){
          *(dec_container->MbSetDesc.p_rlc_data_addr +i) = 0;
      }*/

  count += dec_container->MbSetDesc.
           p_rlc_data_curr_addr - ptr;
  /*printf("Writing %d (%d) words, %d\n",dec_container->MbSetDesc.p_rlc_data_curr_addr-ptr,
   * num_addr,count); */
  /*printf("addrs %d\nn",
   * (dec_container->MbSetDesc.p_rlc_data_curr_addr+1 -ptr )); */

  while(ptr < dec_container->MbSetDesc.
        p_rlc_data_curr_addr + 1)
    /*for (i = 0; i < (dec_container->MbSetDesc.p_rlc_data_curr_addr-ptr)+3&3; i++)
     * for (i = 0; i < num_addr; i++) */
  {
    tmp2 = *ptr++;
    tmp2 = (tmp2 << 16) | (tmp2 >> 16);
    if(frlc != NULL &&
        ((((((addr_t) dec_container->MbSetDesc.
             p_rlc_data_curr_addr -
             (addr_t) dec_container->MbSetDesc.
             p_rlc_data_addr + 3) & (~3)) >> 1)))) {
#if 0
      if(tmpmb != mb) {

        fprintf(frlc, "Picture = %d, mb %d\n",
                dec_container->VopDesc.vop_number - 1, mb);
        tmpmb = mb;
      }
#endif
      fprintf(frlc, "%x\n", (tmp2 << 16) | (tmp2 >> 16));
    }
    if(frlca != NULL &&
        ((((((addr_t) dec_container->MbSetDesc.
             p_rlc_data_curr_addr -
             (addr_t) dec_container->MbSetDesc.
             p_rlc_data_addr + 3) & (~3)) >> 1)))) {

      if(put_comment) {
        coded =
          !((dec_container->MbSetDesc.
             p_ctrl_data_addr[NBR_OF_WORDS_MB *
                              mb] >> asic_pos_no_rlc[block]) & 0x1);
        if(mb == dec_container->VopDesc.total_mb_in_vop) {
          coded = 0;

          /*fprintf(frlca, "make it zero\n"); */
        }   /*fprintf(frlca, "coded 1 %d\n", coded); */
      }

      while(!coded) {
        block++;
        if(block == 6) {
          block = 0;
          mb++;
          /*fprintf(frlca, "mb++ %d\n", mb); */
        }
        coded =
          !((dec_container->MbSetDesc.
             p_ctrl_data_addr[NBR_OF_WORDS_MB *
                              mb] >> asic_pos_no_rlc[block]) & 0x1);

        /*fprintf(frlca, "coded2 %d\n", coded); */
        if(mb == dec_container->VopDesc.total_mb_in_vop) {
          coded = 0;
        }

        if(mb == dec_container->VopDesc.total_mb_in_vop + 1) {
          /*fprintf(frlca,"break1\n"); */
          break;
        }
      }

      if(mb == dec_container->VopDesc.total_mb_in_vop + 1) {
        /*fprintf(frlca, "break2\n"); */
        break;
      }
      /* new block, put comment */
      if(put_comment) {
        halves++;
        fprintf(frlca, "%-8d Picture = %d, MB = %d, block = %d\n", tmp2 & 0xFFFF,   /*
                                                                                             * tmp2 & 0xFFFF, */
                dec_container->VopDesc.vop_number - 1, mb, block++);

        put_comment = 0;

        if(block == 6) {
          block = 0;
          mb++;
          /*fprintf(frlca, "mb++ %d\n", mb); */
        }

      } else {

        halves++;
        fprintf(frlca, "%d\n", tmp2 & 0xFFFF /*, tmp2 & 0xFFFF */ );
      }

      /* if last, print comment next time */
      if((tmp2 & 0x8000) && (check_disable == 0)) {
        put_comment = 1;
      }

      if(!(tmp2 & 0x1FF)) {
        check_disable = 1;
        /*fprintf(frlca, "Check Disable1\n"); */
      } else {
        check_disable = 0;
      }

      if(put_comment) {
        coded =
          !((dec_container->MbSetDesc.
             p_ctrl_data_addr[NBR_OF_WORDS_MB *
                              mb] >> asic_pos_no_rlc[block]) & 0x1);
        /*fprintf(frlca, "coded3 %d\n", coded); */
        if(mb == dec_container->VopDesc.total_mb_in_vop) {
          coded = 0;

          /*fprintf(frlca, "make it zero\n"); */
        }
      }

      while(!coded) {
        block++;
        if(block == 6) {
          block = 0;
          mb++;
          /*fprintf(frlca, "mb++ %d\n", mb); */
        }
        coded =
          !((dec_container->MbSetDesc.
             p_ctrl_data_addr[NBR_OF_WORDS_MB *
                              mb] >> asic_pos_no_rlc[block]) & 0x1);

        /*fprintf(frlca, "coded4 %d\n", coded); */
        if(mb == dec_container->VopDesc.total_mb_in_vop) {
          coded = 0;

        }
        if(mb == dec_container->VopDesc.total_mb_in_vop + 1) {

          /*fprintf(frlca, "break3 mb%d\n", mb); */
          break;
        }
      }
      if(mb == dec_container->VopDesc.total_mb_in_vop + 1) {
        /*fprintf(frlca, "break4, mb %d\n", mb); */
        break;
      }
      /* new block, put comment */
      if(put_comment) {
        halves++;
        fprintf(frlca, "%-8d Picture = %d, MB = %d, block = %d\n", (tmp2 >> 16) & 0xFFFF,   /*
                                                                                                     * (tmp2 >> 16) & 0xFFFF, */
                dec_container->VopDesc.vop_number - 1, mb, block++);

        put_comment = 0;

        if(block == 6) {
          block = 0;
          mb++;
          /*fprintf(frlca, "mb++ %d\n", mb); */
        }
      } else {
        halves++;
        fprintf(frlca, "%d\n", (tmp2 >> 16) & 0xFFFF    /*,
                                                                 * (tmp2 >> 16) & 0xFFFF */ );
      }
      /* if last, print comment next time */
      if((tmp2 & 0x80000000) && (check_disable == 0)) {
        put_comment = 1;
      }
      /* if big level, disable last check next time */
      if(!(tmp2 & 0x1FF0000)) {
        check_disable = 1;
        /*    fprintf(frlca, "Check Disable2\n"); */
      } else {
        check_disable = 0;
      }

    }
    /*            *(dec_container->MbSetDesc.p_rlc_data_curr_addr-1) = 0;
                *(dec_container->MbSetDesc.p_rlc_data_curr_addr-2) = 0;
                *(dec_container->MbSetDesc.p_rlc_data_curr_addr-3) = 0;
                for(tmp =0; tmp < 3900 ; tmp++){
                   * (dec_container->MbSetDesc.p_rlc_data_addr+tmp) = 0;
                }
                *(dec_container->MbSetDesc.p_rlc_data_curr_addr-4) = 0;*/
  }

  /* fprintf(frlc,"VOP end %d \n", dec_container->VopDesc.vop_number - 1); */
  /*printf("dec_container->VopDesc.total_mb_in_vop %d \n", dec_container->VopDesc.total_mb_in_vop);*/
  tmp = (halves + 3) & (~3);
  tmp2 = tmp - halves;

  /*printf("tmp2 %d, halves %d\n", tmp2, halves); */
  for(i = 0; i < tmp2; i++) {
    fprintf(frlca, "0\n");
  }
  halves = tmp;
  /*printf("numadd %d i %d\n", num_addr, i); */
  ASSERT(block < 6);
  /*    ASSERT(mb <  dec_container->VopDesc.total_mb_in_vop);*/

  *phalves = halves;
  *pnum_addr = num_addr;
  /*printf("*tmp2 %x tmp2 %x\n", ptr, *ptr); */
  if(frlc)
    fclose(frlc);
  if(frlca)
    fclose(frlca);
}

void writePictureCtrl(DecContainer * dec_container, u32 * phalves,
                      u32 * pnum_addr) {
  dec_container = dec_container;
  phalves = phalves;
  pnum_addr = pnum_addr;
#if 0
  u32 tmp = 0;

  u32 offset[2] = { 0, 0 };

  FILE *fid = NULL;

  u32 halves = *phalves;

  printf("pciture_ctrl\n");
  fflush(stdout);
  fid = fopen("picture_ctrl_dec.trc", "at");

  if(fid == NULL) {
    return;
  }
  if(fid != NULL) {
    offset[1] = dec_container->VopDesc.total_mb_in_vop * 384;

    fprintf(fid, "------------------picture = %-3d "
            "-------------------------------------\n",
            dec_container->VopDesc.vop_number - 1);

    fprintf(fid, "%d        decoding mode (h264/mpeg4/h263/jpeg)\n",
            dec_container->StrmStorage.short_video + 1);

    fprintf(fid, "%d        Picture Width in macro blocks\n",
            dec_container->VopDesc.vop_width);
    fprintf(fid, "%d        Picture Height in macro blocks\n",
            dec_container->VopDesc.vop_height);

    fprintf(fid, "%d        Rounding control bit for MPEG4\n",
            dec_container->VopDesc.vop_rounding_type);
    fprintf(fid, "%d        Filtering disable\n",
            dec_container->ApiStorage.disable_filter);

    fprintf(fid, "%-3d      Chrominance QP filter offset\n", 0);
    fprintf(fid, "%-3d      Amount of motion vectors / current picture\n",
            NBR_MV_WORDS_MB * dec_container->VopDesc.total_mb_in_vop);

    /* IF thr == 7, use ac tables */
    tmp = (dec_container->VopDesc.intra_dc_vlc_thr == 7) ? 0 : 1;

    fprintf(fid, "%-3d      h264:amount of 4x4 mb/"
            " mpeg4:separate dc mb / picture\n", tmp);

    /* Number of 16 bit rlc words, rounded up to be divisible by 4 */
    fprintf(fid, "%-8d h264:amount of P_SKIP"
            " mbs mpeg4jpeg: amount of rlc/pic\n", halves
            /*      (((((u32)dec_container->MbSetDesc.p_rlc_data_curr_addr
             * - (u32)dec_container->MbSetDesc.p_rlc_data_addr+3)&(~3))>>1))
             */
            /* (dec_container->MbSetDesc.p_rlc_data_curr_addr
             * -dec_container->MbSetDesc.p_rlc_data_addr+3)&3 *//*(((num_addr)<<1)+3)&~(3), num_addr */
           );
    fprintf(fid, "%-8d Base address offset for reference index 0\n" "0        Base address offset for reference index 1\n" "0        Base address offset for reference index 2\n" "0        Base address offset for reference index 3\n" "0        Base address offset for reference index 4\n" "0        Base address offset for reference index 5\n" "0        Base address offset for reference index 6\n" "0        Base address offset for reference index 7\n" "0        Base address offset for reference index 8\n" "0        Base address offset for reference index 9\n" "0        Base address offset for reference index 10\n" "0        Base address offset for reference index 11\n" "0        Base address offset for reference index 12\n" "0        Base address offset for reference index 13\n" "0        Base address offset for reference index 14\n" "0        Base address offset for reference index 15\n" "%-8d Base address offset for decoder output picture\n", offset[(dec_container->VopDesc.vop_number - 1) & 1], /* odd or even? */
            offset[(dec_container->VopDesc.vop_number) & 1]);
    fprintf(fid, "0        decoutDisable\n");

    fprintf(fid,
            "%-3d      Vop Fcode Forward (MPEG4)\n",
            dec_container->VopDesc.fcodeFwd);
  }
  if(fid)
    fclose(fid);
#endif
}

u32 rlcBufferLoad(u32 used, u32 size, u32 print, u32 voptype, u32 vop) {
#if 0
  u32 tmp = 0;
  static u32 load = 0, biggestused = 0, biggestsize = 0, type = 0, vopnum = 0;

  if(!size)
    size = 1;

  if(print) {

    printf
    ("MAXIMUM RLC BUFFER LOAD: %d percent (used %d  max %d) type %d number %d\n",
     load, biggestused, biggestsize, type, vopnum);

  } else {

    tmp = (used * 100) / size;

    if(tmp > load) {
      load = tmp;
      biggestused = used;
      biggestsize = size;
      type = voptype;
      vopnum = vop;

    }

  }
#else
  UNUSED(used);
  UNUSED(size);
  UNUSED(print);
  UNUSED(voptype);
  UNUSED(vop);
#endif
  return 0; /* to prevent compiler warning */
}
void writePictureCtrlHex( DecContainer *dec_container, u32 rlcMode) {



  /* Variables */

  FILE *pcHex = NULL;

  /* Code */
  pcHex = fopen("picture_ctrl_dec.hex", "at");

#if 0
  u32 offset[2]= {0,0};
  offset[1] = dec_container->VopDesc.total_mb_in_vop * 384;
  if(fid != NULL) {
    fprintf(fid, "----------------picture = %u -------------------\n",
            dec_container->VopDesc.vop_number - 1);

    fprintf(fid, "%u\tdecoding mode (h264/mpeg4/h263/jpeg)\n",
            dec_container->StrmStorage.short_video+1);
    fprintf(fid, "%u\tRLC mode enable\n",
            rlcMode);

    fprintf(fid, "%u\tpicture width in macro blocks\n",
            dec_container->VopDesc.vop_width);
    fprintf(fid, "%u\tpicture height in macro blocks\n",
            dec_container->VopDesc.vop_height);

    fprintf(fid, "%u\trounding control bit\n",
            dec_container->VopDesc.vop_rounding_type);
    fprintf(fid, "%u\tfiltering disable\n",
            !dec_container->ApiStorage.deblockEna);

    fprintf(fid, "%u\tchrominance QP filter offset\n", 0);
    fprintf(fid, "%u\tamount of mvs / picture\n",
            NBR_MV_WORDS_MB * dec_container->VopDesc.total_mb_in_vop);

    /* IF thr == 7, use ac tables */
    tmp = (dec_container->VopDesc.intra_dc_vlc_thr == 7)   ? 0 : 1;

    fprintf(fid, "%u\tseparate dc mb / picture\n", tmp);

    /* Number of 16 bit rlc words, rounded up to be divisible by 4 */
    fprintf(fid, "%u\tamount of rlc / picture\n", halves);
    fprintf(fid, "%u\tbase address offset for reference pic\n",
            offset[(dec_container->VopDesc.vop_number - 1)&1]);
    fprintf(fid, "%u\tbase address offset for decoder output pic\n",
            offset[(dec_container->VopDesc.vop_number)&1]);
    fprintf(fid, "0\tdecoutDisable\n");
    fprintf(fid, "%u\tvop fcode forward\n",
            dec_container->VopDesc.fcodeFwd);
    fprintf(fid, "%u\tintra DC VLC threshold\n",
            dec_container->VopDesc.intra_dc_vlc_thr);
    fprintf(fid, "%u\tvop type, '1' = inter, '0' = intra\n",
            dec_container->VopDesc.vop_coding_type);
    fprintf(fid, "%u\tvideo packet enable, '1' = enabled, '0' = disabled\n",
            dec_container->Hdrs.resyncMarkerDisable ? 0 : 1);
    fprintf(fid, "%u\tvop time increment resolution\n",
            dec_container->Hdrs.vopTimeIncrementResolution);
    fprintf(fid, "%u\tinitial value for QP\n",
            dec_container->VopDesc.qP);
  }
#else
  UNUSED(dec_container);
  UNUSED(rlcMode);
#endif
  if(pcHex)
    fclose(pcHex);

#if 0
  MP4DEC_DEBUG(("GetMP4DecId   %x\n", GetMP4DecId(dwl)));
  MP4DEC_DEBUG(("GetMP4DecPictureWidth  %d\n", GetMP4DecPictureWidth(dwl)));
  MP4DEC_DEBUG(("GetMP4DecPictureHeight   %d\n", GetMP4DecPictureHeight(dwl)));
  MP4DEC_DEBUG(("GetMP4DecondingMode   %d\n", GetMP4DecDecondingMode(dwl)));
  MP4DEC_DEBUG(("GetMP4RoundingCtrl   %d\n", GetMP4DecRoundingCtrl(dwl)));
  MP4DEC_DEBUG(("GetMP4DecDisableOutputWrite   %d\n",
                GetMP4DecDisableOutputWrite(dwl)));
  MP4DEC_DEBUG(("GetMP4DecFilterDisable   %d\n", GetMP4DecFilterDisable(dwl)));
  MP4DEC_DEBUG(("GetMP4DecChromaQpOffset  %d\n", GetMP4DecChromaQpOffset(dwl)));
  /*    MP4DEC_DEBUG(("GetMP4DecIrqEnable   %d\n",GetMP4DecIrqEnable(dwl)));*/
  MP4DEC_DEBUG(("GetMP4DecIrqStat   %d\n", GetMP4DecIrqStat(dwl)));
  MP4DEC_DEBUG(("GetMP4DecIrqLine   %d\n", GetMP4DecIrqLine(dwl)));
  MP4DEC_DEBUG(("GetMP4DecEnable   %d\n", GetMP4DecEnable(dwl)));
  MP4DEC_DEBUG(("GetMP4DecMbCtrlBaseAddress   %x\n",
                GetMP4DecMbCtrlBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecMvBaseAddress   %x\n", GetMP4DecMvBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecDcCompBaseAddress   %x\n",
                GetMP4DecDcCompBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecRlcBaseAddress   %x\n", GetMP4DecRlcBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecRefPictBaseAddress   %x\n",
                GetMP4DecRefPictBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecRlcAmount   %d\n", GetMP4DecRlcAmount(dwl)));
  MSetMP4DecDecodingModeP4DEC_DEBUG(("GetMP4DecOutputPictBaseAddress   %x\n",
                                     GetMP4DecOutputPictBaseAddress(dwl)));
  MP4DEC_DEBUG(("GetMP4DecVopFCode   %d\n", GetMP4DecVopFCode(dwl)));
  MP4DEC_DEBUG(("GetMP4DecOutputPictEndian   %d\n", GetMP4DecOutputPictEndian(dwl)));
  MP4DEC_DEBUG(("GetMP4DecAmbaBurstLength   %d\n", GetMP4DecAmbaBurstLength(dwl)));
  MP4DEC_DEBUG(("GetMP4DecAsicServicePriority   %d\n",
                GetMP4DecAsicServicePriority(dwl)));
#endif

}
/*------------------------------------------------------------------------------

    Function name: PrintDecPpUsage

        Functional description:

        Inputs:
            dec_cont    Pointer to decoder structure
            ff          Is current field first or second field of the frame
            pic_index    Picture buffer index
            dec_status   DEC / PP print
            pic_id       Picture ID of the field/frame

        Outputs:
            None

        Returns:
            None

------------------------------------------------------------------------------*/

#ifdef _DEC_PP_USAGE
void MP4DecPpUsagePrint(DecContainer *dec_cont,
                        u32 ppmode,
                        u32 pic_index,
                        u32 dec_status,
                        u32 pic_id) {
  FILE *fp;
  picture_t *p_pic;
  p_pic = (picture_t*)dec_cont->StrmStorage.p_pic_buf;

  fp = fopen("dec_pp_usage.txt", "at");
  if (fp == NULL)
    return;

  if (dec_status) {

    fprintf(fp, "\n================================================================================\n");

    fprintf(fp, "%10s%10s%10s%10s%10s%10s%10s%10s\n",
            "Component", "PicId", "PicType", "Fcm", "Field",
            "PPMode", "BuffIdx", "MultiBuf");

    /* Component and PicId */
    fprintf(fp, "\n%10.10s%10d", "DEC", pic_id);
    /* Pictype */
    switch (dec_cont->VopDesc.vop_coding_type) {
    case PVOP:
      fprintf(fp, "%10s","P");
      break;
    case IVOP:
      fprintf(fp, "%10s","I");
      break;
    case BVOP:
      fprintf(fp, "%10s","B");
      break;
    default:
      ASSERT(0);
      break;
    }
    /* Frame coding mode */
    switch (dec_cont->Hdrs.interlaced) {
    case 0:
      fprintf(fp, "%10s","PR");
      break;
    default:
      fprintf(fp, "%10s","FR");
      break;
    }
    /* Field */
    if ( dec_cont->Hdrs.interlaced)
      if(dec_cont->VopDesc.top_field_first)
        fprintf(fp, "%10s","TOP 1ST");
      else
        fprintf(fp, "%10s","BOT 1ST");
    else

      fprintf(fp, "%10s","PROG");
    /* PPMode and BuffIdx */
    fprintf(fp, "%10s%10d%10s\n", "---",pic_index, "---");



  } else { /* pp status */
    /* Component and PicId + pic type */
    /* for pipelined, the picture has not been indexed to
     * buffer yet, so you don't get correct values from p_pic */
    if(ppmode == DECPP_PIPELINED) {
      fprintf(fp, "%10s%10s", "PP", "''");
      fprintf(fp, "%10s","''");
    } else {
      fprintf(fp, "%10s%10d", "PP", pic_id);
      switch (p_pic[pic_index].pic_type) {
      case PVOP:
        fprintf(fp, "%10s","P");
        break;
      case IVOP:
        fprintf(fp, "%10s","I");
        break;
      case BVOP:
        fprintf(fp, "%10s","B");
        break;
      default:
        ASSERT(0);
        break;
      }
    }

    /* Frame coding mode */
    switch (dec_cont->pp_control.pic_struct) {
    case DECPP_PIC_FRAME_OR_TOP_FIELD:
      fprintf(fp, "%10s","PR");
      break;
    case DECPP_PIC_TOP_FIELD_FRAME:
      fprintf(fp, "%10s","FR FIO TOP");
      break;
    case DECPP_PIC_BOT_FIELD_FRAME:
      fprintf(fp, "%10s","FR FIO BOT");
      break;
    case DECPP_PIC_TOP_AND_BOT_FIELD_FRAME:
      fprintf(fp, "%10s","FR FRO");
      break;
    }
    /* Field */
    fprintf(fp, "%10s","-");

    /* PPMode and BuffIdx */
    switch (ppmode) {
    case DECPP_STAND_ALONE:
      fprintf(fp, "%10s%10d", "STAND",pic_index);
      break;
    case DECPP_PARALLEL:
      fprintf(fp, "%10s%10d", "PARAL",pic_index);
      break;
    case DECPP_PIPELINED:
      fprintf(fp, "%10s%10d", "PIPEL",pic_index);
      break;
    default:
      break;
    }
    switch (dec_cont->pp_control.multi_buf_stat) {
    case MULTIBUFFER_SEMIMODE:
      fprintf(fp, "%10s\n", "SEMI");
      break;
    case MULTIBUFFER_FULLMODE:
      fprintf(fp, "%10s\n", "FULL");
      break;
    case MULTIBUFFER_DISABLED:
      fprintf(fp, "%10s\n", "DISA");
      break;
    default:
      break;
    }
  }

  if (fp) {
    fclose(fp);
    fp = NULL;
  }
}
#endif
