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

#include "basetype.h"
#include "dwl_defs.h"
#include "dwl.h"
#include "dwl_activity_trace.h"
#include "memalloc.h"
#include <assert.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifndef ANDROID
#include <sys/timeb.h>
#endif
#include <sys/types.h>

#ifdef _DWL_DEBUG
#define DWL_DEBUG(fmt, args...) \
  printf(__FILE__ ":%d:%s() " fmt, __LINE__, __func__, ##args)
#else
#define DWL_DEBUG(fmt, args...) \
  do {                          \
  } while (0); /* not debugging: nothing */
#endif

#ifndef DEC_MODULE_PATH
#define DEC_MODULE_PATH "/tmp/dev/hantrodec"
#endif

#ifndef MEMALLOC_MODULE_PATH
#ifdef USE_ION
#define MEMALLOC_MODULE_PATH "/dev/ion"
#else
#define MEMALLOC_MODULE_PATH "/tmp/dev/memalloc"
#endif
#endif

#define HANTRODECPP_REG_START 0x400
#define HANTRODEC_REG_START 0x4

#define HANTRODECPP_FUSE_CFG 99
#define HANTRODEC_FUSE_CFG 57

#define DWL_DECODER_INT \
  ((DWLReadReg(dec_dwl, HANTRODEC_REG_START) >> 11) & 0xFFU)
#define DWL_PP_INT      ((DWLReadReg(dec_dwl, HANTRODECPP_REG_START) >> 11) & 0xFFU)

#define DEC_IRQ_ABORT (1 << 11)
#define DEC_IRQ_RDY (1 << 12)
#define DEC_IRQ_BUS (1 << 13)
#define DEC_IRQ_BUFFER (1 << 14)
#define DEC_IRQ_ASO (1 << 15)
#define DEC_IRQ_ERROR (1 << 16)
#define DEC_IRQ_SLICE (1 << 17)
#define DEC_IRQ_TIMEOUT (1 << 18)

#define PP_IRQ_RDY             (1 << 12)
#define PP_IRQ_BUS             (1 << 13)

#define DWL_HW_ENABLE_BIT 0x000001 /* 0th bit */

#ifdef _DWL_HW_PERFORMANCE
/* signal that decoder/pp is enabled */
void DwlDecoderEnable(void);
#endif

struct MCListenerThreadParams {
  int fd;
  int b_stopped;
  unsigned int n_dec_cores;
  unsigned int n_ppcores;
  sem_t sc_dec_rdy_sem[MAX_ASIC_CORES];
  sem_t sc_pp_rdy_sem[MAX_ASIC_CORES];
  DWLIRQCallbackFn *callback[MAX_ASIC_CORES];
  void *callback_arg[MAX_ASIC_CORES];
};

/* wrapper information */
struct HX170DWL {
  u32 client_type;
  int fd;          /* decoder device file */
  int fd_mem;      /* /dev/mem for mapping */
  int fd_memalloc; /* linear memory allocator */
  u32 num_cores;
  u32 reg_size;         /* IO mem size */
  addr_t free_lin_mem;     /* Start address of free linear memory */
  addr_t free_ref_frm_mem; /* Start address of free reference frame memory */
  int semid;
  struct MCListenerThreadParams *sync_params;
  struct ActivityTrace activity;
  u32 b_ppreserved;
  u32 asic_id;
};

i32 DWLWaitDecHwReady(const void *instance, i32 core_id, u32 timeout);
u32 *DWLMapRegisters(int mem_dev, unsigned long base, unsigned int reg_size,
                     u32 write);
void DWLUnmapRegisters(const void *io, unsigned int reg_size);
