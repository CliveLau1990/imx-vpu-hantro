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

#ifndef HEVC_NAL_UNIT_TYPE_H_
#define HEVC_NAL_UNIT_TYPE_H_

enum NalUnitType {
  NAL_CODED_SLICE_TRAIL_N = 0,
  NAL_CODED_SLICE_TRAIL_R = 1,
  NAL_CODED_SLICE_TSA_N = 2,
  NAL_CODED_SLICE_TSA_R = 3,
  NAL_CODED_SLICE_STSA_N = 4,
  NAL_CODED_SLICE_STSA_R = 5,
  NAL_CODED_SLICE_RADL_N = 6,
  NAL_CODED_SLICE_RADL_R = 7,
  NAL_CODED_SLICE_RASL_N = 8,
  NAL_CODED_SLICE_RASL_R = 9,
  NAL_CODED_SLICE_BLA_W_LP = 16,
  NAL_CODED_SLICE_BLA_W_DLP = 17,
  NAL_CODED_SLICE_BLA_N_LP = 18,
  NAL_CODED_SLICE_IDR_W_LP = 19,
  NAL_CODED_SLICE_IDR_N_LP = 20,
  NAL_CODED_SLICE_CRA = 21,
  NAL_RSV_RAP_VCL22 = 22,
  NAL_RSV_RAP_VCL23 = 23,
  NAL_VIDEO_PARAM_SET = 32,
  NAL_SEQ_PARAM_SET = 33,
  NAL_PIC_PARAM_SET = 34,
  NAL_ACCESS_UNIT_DELIMITER = 35,
  NAL_END_OF_SEQUENCE = 36,
  NAL_END_OF_BITSTREAM = 37,
  NAL_FILLER_DATA = 38,
  NAL_PREFIX_SEI = 39,
  NAL_SUFFIX_SEI = 40,
  NAL_MAX_TYPE_VALUE = 63
};

#endif
