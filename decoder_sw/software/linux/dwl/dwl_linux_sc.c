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
#include "dwl_linux.h"
#include "dwl.h"
#include "dwlthread.h"
#include <linux/hantrodec.h>
#include "memalloc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/* the decoder device driver nod */
const char *dec_dev = DEC_MODULE_PATH;
/* the memalloc device driver nod */
const char *mem_dev = MEMALLOC_MODULE_PATH;

/* a mutex protecting the wrapper init */
static pthread_mutex_t x170_init_mutex = PTHREAD_MUTEX_INITIALIZER;

/* counters for Core usage statistics */
u32 core_usage_counts[MAX_ASIC_CORES] = {0};

/*------------------------------------------------------------------------------
    Function name   : DWLInit
    Description     : Initialize a DWL instance

    Return type     : const void * - pointer to a DWL instance

    Argument        : void * param - not in use, application passes NULL
------------------------------------------------------------------------------*/
const void *DWLInit(struct DWLInitParam * param) {
  struct HX170DWL *dec_dwl;
  
  int core_id;
  
  DWL_DEBUG("%s","INITIALIZE\n");

  dec_dwl = (struct HX170DWL *)calloc(1, sizeof(struct HX170DWL));

  if (dec_dwl == NULL) {
	DWL_DEBUG("%s","failed to alloc struct HX170DWL struct\n");
	return NULL;
  }

  dec_dwl->client_type = param->client_type;

  pthread_mutex_lock(&x170_init_mutex);

#ifdef INTERNAL_TEST
  InternalTestInit();
#endif

  dec_dwl->fd = -1;
  dec_dwl->fd_mem = -1;
  dec_dwl->fd_memalloc = -1;

  /* open the device */
  dec_dwl->fd = open(dec_dev, O_RDWR);
  if(dec_dwl->fd == -1) {
	DWL_DEBUG("failed to open '%s'\n", dec_dev);
	goto err;
  }

  /* Linear memories not needed in pp */
  if(dec_dwl->client_type != DWL_CLIENT_TYPE_PP) {
	/* open memalloc for linear memory allocation */
	dec_dwl->fd_memalloc = open(mem_dev, O_RDWR | O_SYNC);

	if(dec_dwl->fd_memalloc == -1) {
	  DWL_DEBUG("failed to open: %s\n", mem_dev);
	  goto err;
	}
  }

  //dec_dwl->fd_mem = open("/dev/mem", O_RDWR | O_SYNC);

  //if(dec_dwl->fd_mem == -1) {
  //	DWL_DEBUG("failed to open: %s\n", "/dev/mem");
  //	goto err;
  //}

  switch (dec_dwl->client_type) {
  case DWL_CLIENT_TYPE_H264_DEC:
  case DWL_CLIENT_TYPE_MPEG4_DEC:
  case DWL_CLIENT_TYPE_JPEG_DEC:
  case DWL_CLIENT_TYPE_VC1_DEC:
  case DWL_CLIENT_TYPE_MPEG2_DEC:
  case DWL_CLIENT_TYPE_VP6_DEC:
  case DWL_CLIENT_TYPE_VP8_DEC:
  case DWL_CLIENT_TYPE_RV_DEC:
  case DWL_CLIENT_TYPE_AVS_DEC:
  case DWL_CLIENT_TYPE_PP: {
	break;
  }
  default: {
	DWL_DEBUG("Unknown client type no. %d\n", dec_dwl->client_type);
	goto err;
  }
  }

  if (ioctl(dec_dwl->fd, HANTRODEC_IOC_MC_CORES, &dec_dwl->num_cores) == -1) {
	DWL_DEBUG("%s","ioctl HANTRODEC_IOC_MC_CORES failed\n");
	goto err;
  }

  assert(dec_dwl->num_cores <= MAX_ASIC_CORES);

  /*need to get core id*/
  core_id =
	  ioctl(dec_dwl->fd, HANTRODEC_IOCG_CORE_ID, dec_dwl->client_type);
  
  /* negative value signals an error */
  if (core_id < 0) {
	DWL_DEBUG("ioctl HANTRODEC_IOCS_%s_reserve failed, %d\n",
			"DEC", core_id);
	goto err;
  }

  /*input cord_id and output asic_id*/
  dec_dwl->asic_id = core_id;
  if (ioctl(dec_dwl->fd, HANTRODEC_IOX_ASIC_ID, &dec_dwl->asic_id) <0) {
	DWL_DEBUG("%s","ioctl HANTRODEC_IOX_ASIC_ID failed\n");
	dec_dwl->asic_id = 0;
	goto err;
  }
  
  /*input cord_id and output IO size*/
  dec_dwl->reg_size = core_id;
  if (ioctl(dec_dwl->fd, HANTRODEC_IOCGHWIOSIZE, &dec_dwl->reg_size) <0) {
	DWL_DEBUG("%s","ioctl HANTRODEC_IOCGHWIOSIZE failed\n");
	goto err;
  }

  DWL_DEBUG("%s","SUCCESS\n");

  pthread_mutex_unlock(&x170_init_mutex);
  return dec_dwl;

err:

  DWL_DEBUG("%s","FAILED\n");
  pthread_mutex_unlock(&x170_init_mutex);
  DWLRelease(dec_dwl);

  return NULL;
}

/*------------------------------------------------------------------------------
    Function name   : DWLRelease
    Description     : Release a DWl instance

    Return type     : i32 - 0 for success or a negative error code

    Argument        : const void * instance - instance to be released
------------------------------------------------------------------------------*/
i32 DWLRelease(const void *instance) {
  struct HX170DWL *dec_dwl = (struct HX170DWL *)instance;
  unsigned int i = 0;

  DWL_DEBUG("%s","RELEASE\n");

  assert(dec_dwl != NULL);

  if (dec_dwl == NULL) return DWL_OK;

  pthread_mutex_lock(&x170_init_mutex);

  if (dec_dwl->fd_mem != -1) close(dec_dwl->fd_mem);

  if (dec_dwl->fd != -1) close(dec_dwl->fd);

  /* linear memory allocator */
  if (dec_dwl->fd_memalloc != -1) close(dec_dwl->fd_memalloc);

  /* print Core usage stats */
  if (dec_dwl->client_type != DWL_CLIENT_TYPE_PP) {
	u32 total_usage = 0;
	u32 cores = dec_dwl->num_cores;
	for (i = 0; i < cores; i++) total_usage += core_usage_counts[i];
	/* avoid zero division */
	total_usage = total_usage ? total_usage : 1;

	DWL_DEBUG("\nMulti-Core usage statistics:\n");
	for (i = 0; i < cores; i++)
	  DWL_DEBUG("\tCore[%2d] used %6d times (%2d%%)\n", i, core_usage_counts[i],
			 (core_usage_counts[i] * 100) / total_usage);

	DWL_DEBUG("\n");
  }

#ifdef INTERNAL_TEST
  InternalTestFinalize();
#endif

  ActivityTraceRelease(&dec_dwl->activity);
  free(dec_dwl);

  pthread_mutex_unlock(&x170_init_mutex);

  DWL_DEBUG("%s","SUCCESS\n");

  return (DWL_OK);
}

/* HW locking */

/*------------------------------------------------------------------------------
    Function name   : DWLReserveHwPipe
    Description     :
    Return type     : i32
    Argument        : const void *instance
    Argument        : i32 *core_id - ID of the reserved HW Core
------------------------------------------------------------------------------*/
i32 DWLReserveHwPipe(const void *instance, i32 *core_id) {
  i32 ret;
  struct HX170DWL *dec_dwl = (struct HX170DWL *) instance;

  assert(dec_dwl != NULL);
  assert(dec_dwl->client_type != DWL_CLIENT_TYPE_PP);

  DWL_DEBUG("Start\n");

  /* reserve decoder */
  *core_id = ioctl(dec_dwl->fd, HANTRODEC_IOCH_DEC_RESERVE,
                   dec_dwl->client_type);

  if (*core_id != 0) {
    return DWL_ERROR;
  }

  /* reserve PP */
  ret = ioctl(dec_dwl->fd, HANTRODEC_IOCQ_PP_RESERVE);

  /* for pipeline we expect the core id is 0 and the pp is reserved */
  if (ret != 0) {
    /* release the decoder */
    ioctl(dec_dwl->fd, HANTRODEC_IOCT_DEC_RELEASE, core_id);
    return DWL_ERROR;
  }

  dec_dwl->b_ppreserved = 1;

  DWL_DEBUG("Reserved DEC+PP Core %d\n", *core_id);

  return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLReserveHw
    Description     :
    Return type     : i32
    Argument        : const void *instance
    Argument        : i32 *core_id - ID of the reserved HW Core
------------------------------------------------------------------------------*/
i32 DWLReserveHw(const void *instance, i32 *core_id) {
  struct HX170DWL *dec_dwl = (struct HX170DWL *) instance;
  int is_pp;

  assert(dec_dwl != NULL);

  is_pp = dec_dwl->client_type == DWL_CLIENT_TYPE_PP ? 1 : 0;

  DWL_DEBUG(" %s\n", is_pp ? "PP" : "DEC");

  if (is_pp) {
    *core_id = ioctl(dec_dwl->fd, HANTRODEC_IOCQ_PP_RESERVE);

    /* PP is single Core so we expect a zero return value */
    if (*core_id != 0) {
      return DWL_ERROR;
    }
  } else {
    *core_id = ioctl(dec_dwl->fd, HANTRODEC_IOCH_DEC_RESERVE,
                     dec_dwl->client_type);
  }

  /* negative value signals an error */
  if (*core_id < 0) {
    DWL_DEBUG("ioctl HANTRODEC_IOCS_%s_reserve failed\n",
              is_pp ? "PP" : "DEC");
    return DWL_ERROR;
  }

  DWL_DEBUG("Reserved %s Core %d\n", is_pp ? "PP" : "DEC", *core_id);

  return DWL_OK;
}

/*------------------------------------------------------------------------------
    Function name   : DWLReleaseHw
    Description     :
    Return type     : void
    Argument        : const void *instance
------------------------------------------------------------------------------*/
void DWLReleaseHw(const void *instance, i32 core_id) {
  struct HX170DWL *dec_dwl = (struct HX170DWL *) instance;
  int is_pp;

  assert((u32)core_id < dec_dwl->num_cores);
  assert(dec_dwl != NULL);

  is_pp = dec_dwl->client_type == DWL_CLIENT_TYPE_PP ? 1 : 0;

  if ((u32) core_id >= dec_dwl->num_cores)
    return;

  DWL_DEBUG(" %s Core %d\n", is_pp ? "PP" : "DEC", core_id);

  if (is_pp) {
    assert(core_id == 0);

    ioctl(dec_dwl->fd, HANTRODEC_IOCT_PP_RELEASE, core_id);
  } else {
    if (dec_dwl->b_ppreserved) {
      /* decoder has reserved PP also => release it */
      DWL_DEBUG("DEC released PP Core %d\n", core_id);

      dec_dwl->b_ppreserved = 0;

      assert(core_id == 0);

      ioctl(dec_dwl->fd, HANTRODEC_IOCT_PP_RELEASE, core_id);
    }

    ioctl(dec_dwl->fd, HANTRODEC_IOCT_DEC_RELEASE, core_id);
  }
}

void DWLSetIRQCallback(const void *instance, i32 core_id,
                       DWLIRQCallbackFn *callback_fn, void* arg) {
  /* not in use with single Core only control code */
  UNUSED(instance);
  UNUSED(core_id);
  UNUSED(callback_fn);
  UNUSED(arg);

  assert(0);
}
