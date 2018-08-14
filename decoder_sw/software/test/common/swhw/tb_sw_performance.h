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

#ifndef TB_SW_PERFORMANCE_H
#define TB_SW_PERFORMANCE_H

#include "basetype.h"
#include "time.h"

#define SW_PERFORMANCE  //always be enabled
#ifdef SW_PERFORMANCE
#define INIT_SW_PERFORMANCE   \
  double dec_cpu_time = 0;    \
  clock_t dec_start_time = 0; \
  clock_t dec_end_time = 0;
#else
#define INIT_SW_PERFORMANCE
#endif

#ifdef SW_PERFORMANCE
#define START_SW_PERFORMANCE dec_start_time = clock();
#else
#define START_SW_PERFORMANCE
#endif

#ifdef SW_PERFORMANCE
#define END_SW_PERFORMANCE \
  dec_end_time = clock();  \
  dec_cpu_time += ((double)(dec_end_time - dec_start_time)) / CLOCKS_PER_SEC;
#else
#define END_SW_PERFORMANCE
#endif

#ifdef SW_PERFORMANCE
#define FINALIZE_SW_PERFORMANCE printf("SW_PERFORMANCE %0.5f\n", dec_cpu_time);
#else
#define FINALIZE_SW_PERFORMANCE
#endif

#ifdef SW_PERFORMANCE
#define FINALIZE_SW_PERFORMANCE_PP \
  printf("SW_PERFORMANCE_PP %0.5f\n", dec_cpu_time);
#else
#define FINALIZE_SW_PERFORMANCE_PP
#endif

#endif /* TB_SW_PERFORMANCE_H */
