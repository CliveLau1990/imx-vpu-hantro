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
#include "dwl.h"
#ifdef _DWL_PCLINUX
#include "asic.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef USE_EFENCE
#include "efence.h"
#endif

#ifndef PP_MAX_OUTPUT
#define PP_MAX_OUTPUT 1920
#endif

#define DEC_X170_REGS 256

/* Keep track of allocated memories */
u32 reference_total = 0;
u32 reference_alloc_count = 0;
u32 linear_total = 0;
i32 linear_alloc_count = 0;
u32 alloc = 0;
#ifdef _DWL_DEBUG
#define REG_DUMP_FILE "swregdump.log"
FILE *reg_dump;
#endif
static u32 AsicVirtualIO[DEC_X170_REGS+1] = { 0 };
u32 reg_base[DEC_X170_REGS];

/*static void readStreamTrace(const void *instance);*/

typedef struct DWLInstance {
  u32 clien_type;
  u8 *free_ref_frm_mem;
  u8 *frm_base;

}
DWLInstance_t;

DWLHwConfig hw_config;
u32 asicid = (0x81701000);

/*------------------------------------------------------------------------------
    Function name   : DWLReadAsicID
    Description     : Read the HW ID. Does not need a DWL instance to run

    Return type     : u32 - the HW ID
------------------------------------------------------------------------------*/
u32 DWLReadAsicID() {
  /* Synthesis configuration register pp */
  /*AsicVirtualIO[DEC_X170_REGS] = 0x1000 | PP_MAX_OUTPUT;*/
  return asicid;
}
/*------------------------------------------------------------------------------
    Function name   : DWLReadAsicConfig
    Description     : Read HW configuration. Does not need a DWL instance to run

    Return type     : DWLHwConfig - structure with HW configuration
------------------------------------------------------------------------------*/
void DWLReadAsicConfig(DWLHwConfig * hw_cfg) {

  memcpy(hw_cfg, &hw_config, sizeof(DWLHwConfig));

#if 0

  DWLHwConfig config;

  hw_cfg->max_dec_pic_width = 1920;  /* Maximum decoding width supported by the HW */
  hw_cfg->max_pp_out_pic_width = 1920;    /* Maximum output width of Post-Processor */

  hw_cfg->h264_support = H264_HIGH_PROFILE;    /* HW supports baseline profile h.264 */
  hw_cfg->jpeg_support = JPEG_BASELINE;    /* HW supports baseline JPEG */
  hw_cfg->mpeg4_support = MPEG4_ADVANCED_SIMPLE_PROFILE;   /* HW supports advanced simple profile MPEG-4 */
  hw_cfg->mpeg2_support = MPEG2_MAIN_PROFILE;  /* HW supports main profile MPEG-2/MPEG-1 */
  hw_cfg->vc1_support = VC1_ADVANCED_PROFILE;  /* HW supports advanced profile VC-1 */
  hw_cfg->pp_support = PP_SUPPORTED;   /* HW supports Post-Processor */

  hw_cfg->pp_config = PP_DITHERING | PP_SCALING | PP_DEINTERLACING | PP_ALPHA_BLENDING;    /* PP has all optional functions */

#endif
}
/*------------------------------------------------------------------------------
    Function name   : DWLInit
    Description     : Initialize a DWL instance

    Return type     : const void * - pointer to a DWL instance

    Argument        : struct DWLInitParam * param - initialization params
------------------------------------------------------------------------------*/
const void *DWLInit(struct DWLInitParam * param) {
  DWLInstance_t *dwl_inst;
  dwl_inst = (DWLInstance_t *) calloc(1, sizeof(DWLInstance_t));
#ifdef _STREAM_POS_TRACE
  trace_used_stream = 0;
#endif
  reference_total = 0;
  linear_total = 0;
  switch (param->client_type) {
  case DWL_CLIENT_TYPE_H264_DEC:
    printf("DWL initialized by an H264 decoder instance...\n");
    break;
  case DWL_CLIENT_TYPE_MPEG4_DEC:
#ifdef _STREAM_POS_TRACE
    printf("DWL initialized by an MPEG4 decoder instance...%d\n", trace_used_stream);
#else
    printf("DWL initialized by an MPEG4 decoder instance...\n");
#endif
    break;
  case DWL_CLIENT_TYPE_JPEG_DEC:
    printf("DWL initialized by a JPEG decoder instance...\n");
    break;
  case DWL_CLIENT_TYPE_PP:
    printf("DWL initialized by a PP instance...\n");
    break;
  case DWL_CLIENT_TYPE_VC1_DEC:
    printf("DWL initialized by an VC1 decoder instance...\n");
    break;
  default:
    printf("ERROR: DWL client type has to be always specified!\n");
    return NULL;
  }

#ifdef _DWL_DEBUG
  reg_dump = fopen(REG_DUMP_FILE, "w");
  if(NULL == reg_dump) {
    printf("DWL: failed to open: %s\n", REG_DUMP_FILE);
  }
#endif

  dwl_inst->clien_type = param->client_type;
  dwl_inst->frm_base = NULL;
  dwl_inst->free_ref_frm_mem = NULL;
  return (void *) dwl_inst;
}

/*------------------------------------------------------------------------------
    Function name   : DWLRelease
    Description     : Release a DWl instance

    Return type     : i32 - 0 for success or a negative error code

    Argument        : const void * instance - instance to be released
------------------------------------------------------------------------------*/
i32 DWLRelease(const void *instance) {

#if 0
  assert(instance != NULL);
#if 0
  {
    FILE * fp;
    fp = fopen("mem_trace.txt", "a");
    fprintf(fp, "%d\n", alloc);
    fclose(fp);
  }
#endif
#ifdef _DWL_DEBUG
  fclose(reg_dump);
#endif
  free((void *) instance);

  return DWL_OK;

#endif
}

/*------------------------------------------------------------------------------
    Function name   : DWLMallocRefFrm
    Description     : Allocate a frame buffer (contiguous linear RAM memory)

    Return type     : i32 - 0 for success or a negative error code

    Argument        : const void * instance - DWL instance
    Argument        : u32 size - size in bytes of the requested memory
    Argument        : struct DWLLinearMem *info - place where the allocated memory
                        buffer parameters are returned
------------------------------------------------------------------------------*/
i32 DWLMallocRefFrm(const void *instance, u32 size, struct DWLLinearMem * info) {

#if 0
#ifdef _STREAM_POS_TRACE

  DWLInstance_t *dwl_inst = (DWLInstance_t *) instance;

  if(dwl_inst->frm_base== NULL) {
    dwl_inst->frm_base = malloc(0x300000);
    dwl_inst->free_ref_frm_mem = dwl_inst->frm_base;
  }

  info->virtual_address = (u32 *)dwl_inst->free_ref_frm_mem;
  dwl_inst->free_ref_frm_mem += size;

  printf("DWLMallocRefFrm: %8d %x\n", size, dwl_inst->free_ref_frm_mem - dwl_inst->frm_base);


  if(info->virtual_address == NULL)
    return DWL_ERROR;
  info->bus_address = (u32) info->virtual_address;
  info->size = size;
#else
  printf("DWLMallocRefFrm: %8d\n", size);
  info->virtual_address = malloc(size);
  if(info->virtual_address == NULL)
    return DWL_ERROR;
  info->bus_address = (u32) info->virtual_address;
  info->size = size;
#endif
  reference_total += size;
  reference_alloc_count++;
  printf("reference frame memory allocated %d, %d \n",
         reference_total, reference_alloc_count);

#endif
  return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLFreeRefFrm
    Description     : Release a frame buffer previously allocated with
                        DWLMallocRefFrm.

    Return type     : void

    Argument        : const void * instance - DWL instance
    Argument        : struct DWLLinearMem *info - frame buffer memory information
------------------------------------------------------------------------------*/
void DWLFreeRefFrm(const void *instance, struct DWLLinearMem * info) {
#ifdef _STREAM_POS_TRACE
  DWLInstance_t *dwl_inst = (DWLInstance_t *) instance;
  if(dwl_inst->frm_base) {
    dwl_inst->frm_base = NULL;
    free(dwl_inst->frm_base);
  }
#else
  free(info->virtual_address);
  info->size = 0;
#endif
  printf("DWLFreeRefFrm size %d\n", info->size);
  reference_total -= info->size;
  reference_alloc_count--;
  printf("not freed RefFrm  %d", reference_total);
  printf("%d buffers not freed\n", reference_alloc_count);
}

/*------------------------------------------------------------------------------
    Function name   : DWLMallocLinear
    Description     : Allocate a contiguous, linear RAM  memory buffer

    Return type     : i32 - 0 for success or a negative error code

    Argument        : const void * instance - DWL instance
    Argument        : u32 size - size in bytes of the requested memory
    Argument        : struct DWLLinearMem *info - place where the allocated
                        memory buffer parameters are returned
------------------------------------------------------------------------------*/
i32 DWLMallocLinear(const void *instance, u32 size, struct DWLLinearMem * info) {

  return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLFreeLinear
    Description     : Release a linera memory buffer, previously allocated with
                        DWLMallocLinear.

    Return type     : void

    Argument        : const void * instance - DWL instance
    Argument        : struct DWLLinearMem *info - linear buffer memory information
------------------------------------------------------------------------------*/
void DWLFreeLinear(const void *instance, struct DWLLinearMem * info) {
#if 0
  printf("DWLFreeLinear size %d, %d buffers not freed\n", info->size, --linear_alloc_count);
  free(info->virtual_address);
  info->size = 0;
#endif
}

/*------------------------------------------------------------------------------
    Function name   : DWLWriteReg
    Description     : Write a value to a hardware IO register

    Return type     : void

    Argument        : const void * instance - DWL instance
    Argument        : u32 offset - byte offset of the register to be written
    Argument        : u32 value - value to be written out
------------------------------------------------------------------------------*/
void DWLWriteReg(const void *instance, u32 offset, u32 value) {
#ifdef _DWL_DEBUG
  fprintf(reg_dump, "write:%d:0x%08x:0x%08x\n", offset / 4, offset,
          value);
#endif
  assert(offset <= DEC_X170_REGS * 4);
  AsicVirtualIO[offset >> 2] = value;
}

/*------------------------------------------------------------------------------
    Function name   : DWLReadReg
    Description     : Read the value of a hardware IO register

    Return type     : u32 - the value stored in the register

    Argument        : const void * instance - DWL instance
    Argument        : u32 offset - byte offset of the register to be read
------------------------------------------------------------------------------*/
u32 DWLReadReg(const void *instance, u32 offset) {
  /*
  assert(offset <= DEC_X170_REGS * 4);
  return AsicVirtualIO[offset >> 2];
  */
}

/*------------------------------------------------------------------------------
    Function name   : DWLWaitHwReady
    Description     : Wait until hardware has stopped running.
                      Used for synchronizing software runs with the hardware.
                      The wait could succed, timeout, or fail with an error.

    Return type     : i32 - one of the values DWL_HW_WAIT_OK
                                              DWL_HW_WAIT_TIMEOUT
                                              DWL_HW_WAIT_ERROR

    Argument        : const void * instance - DWL instance
------------------------------------------------------------------------------*/
i32 DWLWaitHwReady(const void *instance, u32 timeout) {
#if 0
  DWLInstance_t *dwl_inst = (DWLInstance_t *) instance;

#if defined(_DWL_PCLINUX)
  if (dwl_inst->clien_type == DWL_CLIENT_TYPE_MPEG4_DEC ||
      dwl_inst->clien_type == DWL_CLIENT_TYPE_VC1_DEC ||
      dwl_inst->clien_type == DWL_CLIENT_TYPE_JPEG_DEC ||
      dwl_inst->clien_type == DWL_CLIENT_TYPE_H264_DEC) {
    u32 i;
    for (i = 0; i < DEC_X170_REGS; i++)
      reg_base[i] = AsicVirtualIO[i];
    runAsic();
    for (i = 0; i < DEC_X170_REGS; i++)
      AsicVirtualIO[i] = reg_base[i];

    return (i32) DWL_HW_WAIT_OK;
  } else
#endif
    if(dwl_inst->clien_type != DWL_CLIENT_TYPE_PP) {
      /* regs updated readStreamTrace */
      (void)readStreamTrace(instance);
      return (i32) DWL_HW_WAIT_OK;
    } else {
      /* wait for PP */
#if 0
      AsicVirtualIO[60] |= (0x01 << 12);   /* set PP status */
#endif
      u32 i;
      for (i = 0; i < DEC_X170_REGS; i++) {
        reg_base[i] = AsicVirtualIO[i];
      }
      runAsic();
      for (i = 0; i < DEC_X170_REGS; i++)
        AsicVirtualIO[i] = reg_base[i];

      return (i32) DWL_HW_WAIT_OK;
    }
#endif
  return (i32) DWL_HW_WAIT_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLmalloc
    Description     : Allocate a memory block. Same functionality as
                      the ANSI C malloc()

    Return type     : void pointer to the allocated space, or NULL if there
                      is insufficient memory available

    Argument        : u32 n - Bytes to allocate
------------------------------------------------------------------------------*/
void *DWLmalloc(u32 n) {
#if 1
  printf("DWLmalloc: %8d\n", n);

  alloc += n;

  return malloc((size_t) n);
#else
  u8 * p_mem = NULL;
  printf("DWLmalloc: %8d\n", n);

  p_mem = malloc((size_t) n);
  memset(p_mem, 0xAB, n);
  return p_mem;
#endif
}

/*------------------------------------------------------------------------------
    Function name   : DWLfree
    Description     : Deallocates or frees a memory block. Same functionality as
                      the ANSI C free()

    Return type     : void

    Argument        : void *p - Previously allocated memory block to be freed
------------------------------------------------------------------------------*/
void DWLfree(void *p) {

  if(p != NULL)
    free(p);

}

/*------------------------------------------------------------------------------
    Function name   : DWLcalloc
    Description     : Allocates an array in memory with elements initialized
                      to 0. Same functionality as the ANSI C calloc()

    Return type     : void pointer to the allocated space, or NULL if there
                      is insufficient memory available

    Argument        : u32 n - Number of elements
    Argument        : u32 s - Length in bytes of each element.
------------------------------------------------------------------------------*/
void *DWLcalloc(u32 n, u32 s) {

  alloc += n;

  printf("DWLcalloc: %8d\n", n * s);
  return calloc((size_t) n, (size_t) s);

}

/*------------------------------------------------------------------------------
    Function name   : DWLmemcpy
    Description     : Copies characters between buffers. Same functionality as
                      the ANSI C memcpy()

    Return type     : The value of destination d

    Argument        : void *d - Destination buffer
    Argument        : const void *s - Buffer to copy from
    Argument        : u32 n - Number of bytes to copy
------------------------------------------------------------------------------*/
void *DWLmemcpy(void *d, const void *s, u32 n) {

  return memcpy(d, s, (size_t) n);

}

/*------------------------------------------------------------------------------
    Function name   : DWLmemset
    Description     : Sets buffers to a specified character. Same functionality
                      as the ANSI C memset()

    Return type     : The value of destination d

    Argument        : void *d - Pointer to destination
    Argument        : i32 c - Character to set
    Argument        : u32 n - Number of characters
------------------------------------------------------------------------------*/
void *DWLmemset(void *d, i32 c, u32 n) {

  return memset(d, (int) c, (size_t) n);

}

/*------------------------------------------------------------------------------
    Function name   : DWLReserveHw
    Description     :
    Return type     : i32
    Argument        : const void *instance
------------------------------------------------------------------------------*/
i32 DWLReserveHw(const void *instance) {

  return 0;
}
/*------------------------------------------------------------------------------
    Function name   : DWLReleaseHw
    Description     :
    Return type     : void
    Argument        : const void *instance
------------------------------------------------------------------------------*/
void DWLReleaseHw(const void *instance) {

}
/*------------------------------------------------------------------------------
    Function name   : DWLEnableHw
    Description     :
    Return type     : void
    Argument        : const void *instance
------------------------------------------------------------------------------*/
void DWLEnableHw(const void *instance, u32 offset, u32 value) {


}
void DWLDisableHw(const void *instance, u32 offset, u32 value) {
}


void SetDecRegister(u32 *reg_base, u32 id, u32 value) {
  /*
      u32 tmp;

      ASSERT(id < HWIF_LAST_REG);

      tmp = reg_base[vc1RegSpec[id][0]];
      tmp &= ~(regMask[vc1RegSpec[id][1]] << vc1RegSpec[id][2]);
      tmp |= (value & regMask[vc1RegSpec[id][1]]) << vc1RegSpec[id][2];
      reg_base[vc1RegSpec[id][0]] = tmp;;
  */
}

/*------------------------------------------------------------------------------

    Function name: GetDecRegister

        Functional description:

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/
u32 GetDecRegister(const u32 *reg_base, u32 id) {
  /*
      u32 tmp;

      ASSERT(id < HWIF_LAST_REG);

      tmp = reg_base[vc1RegSpec[id][0]];
      tmp = tmp >> vc1RegSpec[id][2];
      tmp &= regMask[vc1RegSpec[id][1]];
      return(tmp);
  */
  return 0;
}
