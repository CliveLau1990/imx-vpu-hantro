/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
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

/* Create CABAC context tables using ContextTables.h from reference model. */

typedef unsigned char UChar;

#include "ContextTables.h"
#include <stdio.h>

typedef struct {
  const UChar *p;
  int size;
  int num_elems;
} entry;

UChar cbf_luma[3][2] = {{153, 111}, {153, 111}, {111, 141}};
UChar cbf_ch[3][4] = {{149, 92, 167, 154}, {149, 107, 167, 154},
  {94, 138, 182, 154}
};

UChar empty[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

entry init_values[] = {
  {INIT_SAO_MERGE_FLAG, NUM_SAO_MERGE_FLAG_CTX, 0},
  {INIT_SAO_TYPE_IDX, NUM_SAO_TYPE_IDX_CTX, 0},
  {INIT_SPLIT_FLAG, NUM_SPLIT_FLAG_CTX, 0},
  {INIT_SKIP_FLAG, NUM_SKIP_FLAG_CTX, 0},
  {INIT_DQP, NUM_DELTA_QP_CTX, 0},
  {INIT_PRED_MODE, NUM_PRED_MODE_CTX, 0},
  {INIT_PART_SIZE, NUM_PART_SIZE_CTX, 0},
  {INIT_CU_AMP_POS, NUM_CU_AMP_CTX, 0},
  {INIT_INTRA_PRED_MODE, NUM_ADI_CTX, 0},
  {INIT_CHROMA_PRED_MODE, NUM_CHROMA_PRED_CTX, 0},
  {INIT_MERGE_FLAG_EXT, NUM_MERGE_FLAG_EXT_CTX, 0},
  {INIT_MERGE_IDX_EXT, NUM_MERGE_IDX_EXT_CTX, 0},
  {INIT_INTER_DIR, NUM_INTER_DIR_CTX, 0},
  {INIT_REF_PIC, NUM_REF_NO_CTX, 0},
  {INIT_MVD, NUM_MV_RES_CTX, 0}, /* greater0 + greater1 in standard */
  {INIT_MVP_IDX, NUM_MVP_IDX_CTX, 0},
  {INIT_QT_ROOT_CBF, NUM_QT_ROOT_CBF_CTX, 0}, /* no_residual_data_flag */
  {INIT_TRANS_SUBDIV_FLAG, NUM_TRANS_SUBDIV_FLAG_CTX, 0},
  {cbf_luma, 2, 0},
  {cbf_ch, 4, 0},
  {INIT_TRANSFORMSKIP_FLAG, 2 * NUM_TRANSFORMSKIP_FLAG_CTX, 0},
  {INIT_CU_TRANSQUANT_BYPASS_FLAG, NUM_CU_TRANSQUANT_BYPASS_FLAG_CTX, 0},
  {INIT_LAST, 2 * NUM_CTX_LAST_FLAG_XY, 18},
  {INIT_LAST, 2 * NUM_CTX_LAST_FLAG_XY, 18},
  /* standard has distings x/y tables, same values */
  {INIT_SIG_CG_FLAG, 2 * NUM_SIG_CG_FLAG_CTX, 0},
  {INIT_SIG_FLAG, NUM_SIG_FLAG_CTX, 0},
  {INIT_ONE_FLAG, NUM_ONE_FLAG_CTX, 0},
  {INIT_ABS_FLAG, NUM_ABS_FLAG_CTX, 0},
  {empty, 2 /* to next multiple of 8 */, 0},
  {NULL, 0, 0}
};

static unsigned int count = 0;
static unsigned int val = 0;

void PrintTable(entry *entry, int init_type) {
  int j;
  int num_elems = entry->num_elems ? entry->num_elems : entry->size;

  for (j = 0; j < num_elems; j++) {
    if (!(count & 15)) printf("\n    ");
    val = (val << 8) | entry->p[init_type * entry->size + j];
    count++;
    if (!(count & 3)) {
      printf("0x%08x,", val);
      val = 0;
    }
  }
  fprintf(stderr, "COUNT %d\n", count);
}

void main(void) {

  int i;

  printf("/* GENERATED by hevc_create_cabac.c */\n\n");
  printf("#include \"basetype.h\"\n\n");

  printf("const u32 cabac_init_values[] = {");
  for (i = 0; i < 3; i++) { /* initialisation_type */
    entry *iter = init_values;
    while (iter->p) {
      PrintTable(iter, i);
      iter++;
    }
    fprintf(stderr, "TOT COUNT %d\n", count);
  }
  /* finalize */
  if ((count & 3)) {
    i = count & 3;
    printf("0x%08x,\n", val << ((4 - i) * 8));
  }

  printf("};\n");
}
