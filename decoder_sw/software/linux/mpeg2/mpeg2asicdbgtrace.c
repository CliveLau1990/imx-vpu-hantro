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
#include "mpeg2hwd_container.h"
#include "mpeg2hwd_cfg.h"
#include "mpeg2decapi.h"
#include "mpeg2hwd_debug.h"
#include "mpeg2hwd_utils.h"

#include <stdio.h>
#include <assert.h>

void MP2RegDump(const void *dwl) {
#if 0
  MP2DEC_DEBUG(("GetMP2DecId   %x\n", GetMP2DecId(dwl)));
  MP2DEC_DEBUG(("GetMP2DecPictureWidth  %d\n", GetMP2DecPictureWidth(dwl)));
  MP2DEC_DEBUG(("GetMP2DecPictureHeight   %d\n",
                GetMP2DecPictureHeight(dwl)));
  MP2DEC_DEBUG(("GetMP2DecondingMode   %d\n", GetMP2DecDecondingMode(dwl)));
  MP2DEC_DEBUG(("GetMP2RoundingCtrl   %d\n", GetMP2DecRoundingCtrl(dwl)));
  MP2DEC_DEBUG(("GetMP2DecDisableOutputWrite   %d\n",
                GetMP2DecDisableOutputWrite(dwl)));
  MP2DEC_DEBUG(("GetMP2DecFilterDisable   %d\n",
                GetMP2DecFilterDisable(dwl)));
  MP2DEC_DEBUG(("GetMP2DecChromaQpOffset  %d\n",
                GetMP2DecChromaQpOffset(dwl)));
  /*    MP2DEC_DEBUG(("GetMP2DecIrqEnable   %d\n",GetMP2DecIrqEnable(dwl)));*/
  MP2DEC_DEBUG(("GetMP2DecIrqStat   %d\n", GetMP2DecIrqStat(dwl)));
  MP2DEC_DEBUG(("GetMP2DecIrqLine   %d\n", GetMP2DecIrqLine(dwl)));
  MP2DEC_DEBUG(("GetMP2DecEnable   %d\n", GetMP2DecEnable(dwl)));
  MP2DEC_DEBUG(("GetMP2DecMbCtrlBaseAddress   %x\n",
                GetMP2DecMbCtrlBaseAddress(dwl)));
  MP2DEC_DEBUG(("GetMP2DecMvBaseAddress   %x\n",
                GetMP2DecMvBaseAddress(dwl)));
  MP2DEC_DEBUG(("GetMP2DecDcCompBaseAddress   %x\n",
                GetMP2DecDcCompBaseAddress(dwl)));
  MP2DEC_DEBUG(("GetMP2DecRlcBaseAddress   %x\n",
                GetMP2DecRlcBaseAddress(dwl)));
  MP2DEC_DEBUG(("GetMP2DecRefPictBaseAddress   %x\n",
                GetMP2DecRefPictBaseAddress(dwl)));
  MP2DEC_DEBUG(("GetMP2DecRlcAmount   %d\n", GetMP2DecRlcAmount(dwl)));
  MP2DEC_DEBUG(("GetMP2DecOutputPictBaseAddress   %x\n",
                GetMP2DecOutputPictBaseAddress(dwl)));
  MP2DEC_DEBUG(("GetMP2DecVopFCode   %d\n", GetMP2DecVopFCode(dwl)));
  MP2DEC_DEBUG(("GetMP2DecOutputPictEndian   %d\n",
                GetMP2DecOutputPictEndian(dwl)));
  MP2DEC_DEBUG(("GetMP2DecAmbaBurstLength   %d\n",
                GetMP2DecAmbaBurstLength(dwl)));
  MP2DEC_DEBUG(("GetMP2DecAsicServicePriority   %d\n",
                GetMP2DecAsicServicePriority(dwl)));
#else
  UNUSED(dwl);
#endif
}

void writePictureCtrl(DecContainer * dec_container, u32 * phalves,
                      u32 * pnum_addr) {
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
    offset[1] = dec_container->FrameDesc.total_mb_in_frame * 384;

    fprintf(fid, "------------------picture = %-3d "
            "-------------------------------------\n",
            dec_container->FrameDesc.frame_number - 1);

    fprintf(fid, "%d        decoding mode (h264/mpeg4/h263/mpeg2/jpeg)\n",
            dec_container->StrmStorage.short_video + 1);

    fprintf(fid, "%d        Picture Width in macro blocks\n",
            dec_container->FrameDesc.frame_width);
    fprintf(fid, "%d        Picture Height in macro blocks\n",
            dec_container->FrameDesc.frame_height);

    fprintf(fid, "%d        Rounding control bit for MPEG2\n",
            dec_container->FrameDesc.pic_rounding_type);
    fprintf(fid, "%d        Filtering disable\n",
            dec_container->ApiStorage.disable_filter);

    fprintf(fid, "%-3d      Chrominance QP filter offset\n", 0);
    fprintf(fid, "%-3d      Amount of motion vectors / current picture\n",
            NBR_MV_WORDS_MB * dec_container->FrameDesc.total_mb_in_frame);

    /* IF thr == 7, use ac tables */
    tmp = (dec_container->FrameDesc.intra_dc_vlc_thr == 7) ? 0 : 1;

    fprintf(fid, "%-3d      h264:amount of 4x4 mb/"
            " mpeg2:separate dc mb / picture\n", tmp);

    /* Number of 16 bit rlc words, rounded up to be divisible by 4 */
    fprintf(fid, "%-8d h264:amount of P_SKIP"
            " mbs mpeg2jpeg: amount of rlc/pic\n", halves
            /*      (((((u32)dec_container->MbSetDesc.pRlcDataCurrAddr
             * - (u32)dec_container->MbSetDesc.pRlcDataAddr+3)&(~3))>>1))
             */
            /* (dec_container->MbSetDesc.pRlcDataCurrAddr
             * -dec_container->MbSetDesc.pRlcDataAddr+3)&3 *//*(((numAddr)<<1)+3)&~(3), numAddr */
           );
    fprintf(fid, "%-8d Base address offset for reference index 0\n" "0        Base address offset for reference index 1\n" "0        Base address offset for reference index 2\n" "0        Base address offset for reference index 3\n" "0        Base address offset for reference index 4\n" "0        Base address offset for reference index 5\n" "0        Base address offset for reference index 6\n" "0        Base address offset for reference index 7\n" "0        Base address offset for reference index 8\n" "0        Base address offset for reference index 9\n" "0        Base address offset for reference index 10\n" "0        Base address offset for reference index 11\n" "0        Base address offset for reference index 12\n" "0        Base address offset for reference index 13\n" "0        Base address offset for reference index 14\n" "0        Base address offset for reference index 15\n" "%-8d Base address offset for decoder output picture\n", offset[(dec_container->FrameDesc.frame_number - 1) & 1], /* odd or even? */
            offset[(dec_container->FrameDesc.frame_number) & 1]);
    fprintf(fid, "0        decoutDisable\n");

    fprintf(fid,
            "%-3d      Vop Fcode Forward (MPEG2)\n",
            dec_container->FrameDesc.fcodeFwd);
  }
  if(fid)
    fclose(fid);
#else
  UNUSED(dec_container);
  UNUSED(phalves);
  UNUSED(pnum_addr);
#endif
}

void writePictureCtrlHex(DecContainer * dec_container, u32 rlcMode) {

  /* Variables */

  u32 offset[2] = { 0, 0 };
  //u32 *regBase = dec_container->mpeg2Regs;
  //static u32 memoryStartPointer = 0xFFFFFFFF;
  //FILE *pcHex = NULL;

  /* Code */
  //pcHex = fopen("picture_ctrl_dec.hex", "at");

  offset[1] = dec_container->FrameDesc.total_mb_in_frame * 384;
  (void) offset;
#if 0
  if(fid != NULL) {
    fprintf(fid, "----------------picture = %u -------------------\n",
            dec_container->FrameDesc.frame_number - 1);

    fprintf(fid, "%u\tdecoding mode (h264/mpeg4/h263/mpeg2/jpeg)\n",
            dec_container->StrmStorage.short_video + 1);
    fprintf(fid, "%u\tRLC mode enable\n", rlcMode);

    fprintf(fid, "%u\tpicture width in macro blocks\n",
            dec_container->FrameDesc.frame_width);
    fprintf(fid, "%u\tpicture height in macro blocks\n",
            dec_container->FrameDesc.frame_height);

    fprintf(fid, "%u\trounding control bit\n",
            dec_container->FrameDesc.frameRoundingType);
    fprintf(fid, "%u\tfiltering disable\n",
            !dec_container->ApiStorage.deblockEna);

    fprintf(fid, "%u\tchrominance QP filter offset\n", 0);
    fprintf(fid, "%u\tamount of mvs / picture\n",
            NBR_MV_WORDS_MB * dec_container->FrameDesc.total_mb_in_frame);

    /* IF thr == 7, use ac tables */
    tmp = (dec_container->FrameDesc.intra_dc_vlc_thr == 7) ? 0 : 1;

    fprintf(fid, "%u\tseparate dc mb / picture\n", tmp);

    /* Number of 16 bit rlc words, rounded up to be divisible by 4 */
    fprintf(fid, "%u\tamount of rlc / picture\n", halves);
    fprintf(fid, "%u\tbase address offset for reference pic\n",
            offset[(dec_container->FrameDesc.frame_number - 1) & 1]);
    fprintf(fid, "%u\tbase address offset for decoder output pic\n",
            offset[(dec_container->FrameDesc.frame_number) & 1]);
    fprintf(fid, "0\tdecoutDisable\n");
    fprintf(fid, "%u\tvop fcode forward\n",
            dec_container->FrameDesc.fcodeFwd);
    fprintf(fid, "%u\tintra DC VLC threshold\n",
            dec_container->FrameDesc.intra_dc_vlc_thr);
    fprintf(fid, "%u\tFrame type, '1' = inter, '0' = intra\n",
            dec_container->FrameDesc.frameCodingType);
    fprintf(fid, "%u\tvideo packet enable, '1' = enabled, '0' = disabled\n",
            dec_container->Hdrs.resyncMarkerDisable ? 0 : 1);
    fprintf(fid, "%u\tFrame time increment resolution\n",
            dec_container->Hdrs.picTimeIncrementResolution);
    fprintf(fid, "%u\tinitial value for QP\n", dec_container->FrameDesc.qP);
  }
#else
  UNUSED(rlcMode);
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
void Mpeg2DecPpUsagePrint(DecContainer *dec_cont,
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
    switch (dec_cont->FrameDesc.pic_coding_type) {
    case PFRAME:
      fprintf(fp, "%10s","P");
      break;
    case IFRAME:
      fprintf(fp, "%10s","I");
      break;
    case BFRAME:
      fprintf(fp, "%10s","B");
      break;
    case DFRAME:
      fprintf(fp, "%10s","D");
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
      /*  if(dec_cont->FrameDesc.topFieldFirst)
            fprintf(fp, "%10s","TOP 1ST");
        else
            fprintf(fp, "%10s","BOT 1ST");*/
      fprintf(fp, "%10s","INT");
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
      /*switch (p_pic[pic_index].picType)*/
      switch (dec_cont->FrameDesc.pic_coding_type) {
      case IFRAME:
        fprintf(fp, "%10s","I");
        break;
      case PFRAME:
        fprintf(fp, "%10s","P");
        break;
      case BFRAME:
        fprintf(fp, "%10s","B");
        break;
      case DFRAME:
        fprintf(fp, "%10s","D");
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
