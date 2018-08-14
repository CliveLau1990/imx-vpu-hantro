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

typedef enum {
  TOOL_KEYFRAMES, /* done */
  TOOL_NON_KEYFRAMES, /* done */
  TOOL_HUFFMAN_CODED_DCT, /* done */
  TOOL_BOOLCODED_DCT, /* done */
  TOOL_MULTI_STREAM, /* done */
  TOOL_SINGLE_STREAM, /* done */
  TOOL_CUSTOM_SCAN_ORDER, /* done */
  TOOL_MODE_PROBABILITY_UPDATES,  /* done */
  TOOL_MV_TREE_UPDATES, /* done */
  TOOL_COEFF_PROBABILITY_UPDATES_DC, /* done */
  TOOL_COEFF_PROBABILITY_UPDATES_AC, /* done */
  TOOL_COEFF_PROBABILITY_UPDATES_ZRL, /* done */
  TOOL_LOOP_FILTER, /* done */
  TOOL_GOLDEN_FRAME_UPDATES, /* done */
  TOOL_GOLDEN_FRAME_PREDICTION, /* done */
  TOOL_FOURMV, /* done */
  TOOL_BILINEAR_FILTER, /* done */
  TOOL_BICUBIC_FILTER, /* done */
  TOOL_VARIANCE_THRESHOLD, /* done */
  TOOL_MV_SIZE_THRESHOLD, /* done */
  TOOL_MULTIPLE_FILTER_ALPHA, /* done */
  TOOL_MV_LEN_OVERFLOW, /* done */
  TOOL_DC_COEFF_SATURATION,
  TOOL_OUTPUT_SCALING,

  AMOUNT_OF_DECODING_TOOLS = TOOL_OUTPUT_SCALING + 1
} traceDecodingTool_e;

void trace_DecodingToolUsed( traceDecodingTool_e tool );

