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

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "avs_container.h"

#ifdef _ASSERT_USED
#include <assert.h>
#endif

#ifdef _UTEST
#include <stdio.h>
#endif

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/* constant definitions */
#ifndef OK
#define OK 0
#endif

#ifndef NOK
#define NOK 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef NULL
#define NULL 0
#endif

/* decoder states */
enum {
  STATE_OK,
  STATE_NOT_READY,
  STATE_SYNC_LOST
};

#define HANTRO_OK 0
#define HANTRO_NOK 1

#ifndef NULL
#define NULL 0
#endif

/* picture structure */
#define FIELDPICTURE 0
#define FRAMEPICTURE 1

/* Error concealment */
#define FREEZED_PIC_RDY 1

/* start codes */
enum {
  SC_SLICE = 0x00,    /* throug AF */
  SC_SEQUENCE = 0xB0,
  SC_SEQ_END = 0xB1,
  SC_USER_DATA = 0xB2,
  SC_I_PICTURE = 0xB3,
  SC_EXTENSION = 0xB5,
  SC_PB_PICTURE = 0xB6,
  SC_NOT_FOUND = 0xFFFE,
  SC_ERROR = 0xFFFF
};

/* start codes */
enum {
  SC_SEQ_DISPLAY_EXT = 0x02
};

enum {
  IFRAME = 1,
  PFRAME = 2,
  BFRAME = 3
};

enum {
  OUT_OF_BUFFER = 0xFF
};

/* value to be returned by GetBits if stream buffer is empty */
#define END_OF_STREAM 0xFFFFFFFFU

/* macro for assertion, used only if compiler flag _ASSERT_USED is defined */
#ifdef _ASSERT_USED
#define ASSERT(expr) assert(expr)
#else
#define ASSERT(expr)
#endif

#ifdef _DEBUG_PRINT
#include <stdio.h>
#define AVSDEC_DEBUG(args) printf args
#else
#define AVSDEC_DEBUG(args)
#endif

/* macro to check if stream ends */
#define IS_END_OF_STREAM(p_container) \
    ( (p_container)->StrmDesc.strm_buff_read_bits == \
      (8*(p_container)->StrmDesc.strm_buff_size) )

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

u32 AvsStrmDec_GetBits(DecContainer *, u32 num_bits);
u32 AvsStrmDec_ShowBits(DecContainer *, u32 num_bits);
u32 AvsStrmDec_ShowBits32(DecContainer *);
u32 AvsStrmDec_ShowBitsAligned(DecContainer *, u32 num_bits, u32 num_bytes);
u32 AvsStrmDec_FlushBits(DecContainer *, u32 num_bits);
u32 AvsStrmDec_UnFlushBits(DecContainer *, u32 num_bits);

u32 AvsStrmDec_NextStartCode(DecContainer *);

u32 AvsStrmDec_NumBits(u32 value);
u32 AvsStrmDec_CountLeadingZeros(u32 value, u32 len);

#endif
