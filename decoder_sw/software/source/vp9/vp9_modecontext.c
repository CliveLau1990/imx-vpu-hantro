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

#include "vp9_entropymode.h"

const u8 vp9_kf_default_bmode_probs
[VP9_INTRA_MODES][VP9_INTRA_MODES][VP9_INTRA_MODES - 1] = {
  {
    /* above = dc */
    {137, 30, 42, 148, 151, 207, 70, 52, 91} /* left = dc */,
    {92, 45, 102, 136, 116, 180, 74, 90, 100} /* left = v */,
    {73, 32, 19, 187, 222, 215, 46, 34, 100} /* left = h */,
    {91, 30, 32, 116, 121, 186, 93, 86, 94} /* left = d45 */,
    {72, 35, 36, 149, 68, 206, 68, 63, 105} /* left = d135 */,
    {73, 31, 28, 138, 57, 124, 55, 122, 151} /* left = d117 */,
    {67, 23, 21, 140, 126, 197, 40, 37, 171} /* left = d153 */,
    {86, 27, 28, 128, 154, 212, 45, 43, 53} /* left = d27 */,
    {74, 32, 27, 107, 86, 160, 63, 134, 102} /* left = d63 */,
    {59, 67, 44, 140, 161, 202, 78, 67, 119} /* left = tm */
  },
  {
    /* above = v */
    {63, 36, 126, 146, 123, 158, 60, 90, 96} /* left = dc */,
    {43, 46, 168, 134, 107, 128, 69, 142, 92} /* left = v */,
    {44, 29, 68, 159, 201, 177, 50, 57, 77} /* left = h */,
    {58, 38, 76, 114, 97, 172, 78, 133, 92} /* left = d45 */,
    {46, 41, 76, 140, 63, 184, 69, 112, 57} /* left = d135 */,
    {38, 32, 85, 140, 46, 112, 54, 151, 133} /* left = d117 */,
    {39, 27, 61, 131, 110, 175, 44, 75, 136} /* left = d153 */,
    {52, 30, 74, 113, 130, 175, 51, 64, 58} /* left = d27 */,
    {47, 35, 80, 100, 74, 143, 64, 163, 74} /* left = d63 */,
    {36, 61, 116, 114, 128, 162, 80, 125, 82} /* left = tm */
  },
  {
    /* above = h */
    {82, 26, 26, 171, 208, 204, 44, 32, 105} /* left = dc */,
    {55, 44, 68, 166, 179, 192, 57, 57, 108} /* left = v */,
    {42, 26, 11, 199, 241, 228, 23, 15, 85} /* left = h */,
    {68, 42, 19, 131, 160, 199, 55, 52, 83} /* left = d45 */,
    {58, 50, 25, 139, 115, 232, 39, 52, 118} /* left = d135 */,
    {50, 35, 33, 153, 104, 162, 64, 59, 131} /* left = d117 */,
    {44, 24, 16, 150, 177, 202, 33, 19, 156} /* left = d153 */,
    {55, 27, 12, 153, 203, 218, 26, 27, 49} /* left = d27 */,
    {53, 49, 21, 110, 116, 168, 59, 80, 76} /* left = d63 */,
    {38, 72, 19, 168, 203, 212, 50, 50, 107} /* left = tm */
  },
  {
    /* above = d45 */
    {103, 26, 36, 129, 132, 201, 83, 80, 93} /* left = dc */,
    {59, 38, 83, 112, 103, 162, 98, 136, 90} /* left = v */,
    {62, 30, 23, 158, 200, 207, 59, 57, 50} /* left = h */,
    {67, 30, 29, 84, 86, 191, 102, 91, 59} /* left = d45 */,
    {60, 32, 33, 112, 71, 220, 64, 89, 104} /* left = d135 */,
    {53, 26, 34, 130, 56, 149, 84, 120, 103} /* left = d117 */,
    {53, 21, 23, 133, 109, 210, 56, 77, 172} /* left = d153 */,
    {77, 19, 29, 112, 142, 228, 55, 66, 36} /* left = d27 */,
    {61, 29, 29, 93, 97, 165, 83, 175, 162} /* left = d63 */,
    {47, 47, 43, 114, 137, 181, 100, 99, 95} /* left = tm */
  },
  {
    /* above = d135 */
    {69, 23, 29, 128, 83, 199, 46, 44, 101} /* left = dc */,
    {53, 40, 55, 139, 69, 183, 61, 80, 110} /* left = v */,
    {40, 29, 19, 161, 180, 207, 43, 24, 91} /* left = h */,
    {60, 34, 19, 105, 61, 198, 53, 64, 89} /* left = d45 */,
    {52, 31, 22, 158, 40, 209, 58, 62, 89} /* left = d135 */,
    {44, 31, 29, 147, 46, 158, 56, 102, 198} /* left = d117 */,
    {35, 19, 12, 135, 87, 209, 41, 45, 167} /* left = d153 */,
    {55, 25, 21, 118, 95, 215, 38, 39, 66} /* left = d27 */,
    {51, 38, 25, 113, 58, 164, 70, 93, 97} /* left = d63 */,
    {47, 54, 34, 146, 108, 203, 72, 103, 151} /* left = tm */
  },
  {
    /* above = d117 */
    {64, 19, 37, 156, 66, 138, 49, 95, 133} /* left = dc */,
    {46, 27, 80, 150, 55, 124, 55, 121, 135} /* left = v */,
    {36, 23, 27, 165, 149, 166, 54, 64, 118} /* left = h */,
    {53, 21, 36, 131, 63, 163, 60, 109, 81} /* left = d45 */,
    {40, 26, 35, 154, 40, 185, 51, 97, 123} /* left = d135 */,
    {35, 19, 34, 179, 19, 97, 48, 129, 124} /* left = d117 */,
    {36, 20, 26, 136, 62, 164, 33, 77, 154} /* left = d153 */,
    {45, 18, 32, 130, 90, 157, 40, 79, 91} /* left = d27 */,
    {45, 26, 28, 129, 45, 129, 49, 147, 123} /* left = d63 */,
    {38, 44, 51, 136, 74, 162, 57, 97, 121} /* left = tm */
  },
  {
    /* above = d153 */
    {75, 17, 22, 136, 138, 185, 32, 34, 166} /* left = dc */,
    {56, 39, 58, 133, 117, 173, 48, 53, 187} /* left = v */,
    {35, 21, 12, 161, 212, 207, 20, 23, 145} /* left = h */,
    {56, 29, 19, 117, 109, 181, 55, 68, 112} /* left = d45 */,
    {47, 29, 17, 153, 64, 220, 59, 51, 114} /* left = d135 */,
    {46, 16, 24, 136, 76, 147, 41, 64, 172} /* left = d117 */,
    {34, 17, 11, 108, 152, 187, 13, 15, 209} /* left = d153 */,
    {51, 24, 14, 115, 133, 209, 32, 26, 104} /* left = d27 */,
    {55, 30, 18, 122, 79, 179, 44, 88, 116} /* left = d63 */,
    {37, 49, 25, 129, 168, 164, 41, 54, 148} /* left = tm */
  },
  {
    /* above = d27 */
    {82, 22, 32, 127, 143, 213, 39, 41, 70} /* left = dc */,
    {62, 44, 61, 123, 105, 189, 48, 57, 64} /* left = v */,
    {47, 25, 17, 175, 222, 220, 24, 30, 86} /* left = h */,
    {68, 36, 17, 106, 102, 206, 59, 74, 74} /* left = d45 */,
    {57, 39, 23, 151, 68, 216, 55, 63, 58} /* left = d135 */,
    {49, 30, 35, 141, 70, 168, 82, 40, 115} /* left = d117 */,
    {51, 25, 15, 136, 129, 202, 38, 35, 139} /* left = d153 */,
    {68, 26, 16, 111, 141, 215, 29, 28, 28} /* left = d27 */,
    {59, 39, 19, 114, 75, 180, 77, 104, 42} /* left = d63 */,
    {40, 61, 26, 126, 152, 206, 61, 59, 93} /* left = tm */
  },
  {
    /* above = d63 */
    {78, 23, 39, 111, 117, 170, 74, 124, 94} /* left = dc */,
    {48, 34, 86, 101, 92, 146, 78, 179, 134} /* left = v */,
    {47, 22, 24, 138, 187, 178, 68, 69, 59} /* left = h */,
    {56, 25, 33, 105, 112, 187, 95, 177, 129} /* left = d45 */,
    {48, 31, 27, 114, 63, 183, 82, 116, 56} /* left = d135 */,
    {43, 28, 37, 121, 63, 123, 61, 192, 169} /* left = d117 */,
    {42, 17, 24, 109, 97, 177, 56, 76, 122} /* left = d153 */,
    {58, 18, 28, 105, 139, 182, 70, 92, 63} /* left = d27 */,
    {46, 23, 32, 74, 86, 150, 67, 183, 88} /* left = d63 */,
    {36, 38, 48, 92, 122, 165, 88, 137, 91} /* left = tm */
  },
  {
    /* above = tm */
    {65, 70, 60, 155, 159, 199, 61, 60, 81} /* left = dc */,
    {44, 78, 115, 132, 119, 173, 71, 112, 93} /* left = v */,
    {39, 38, 21, 184, 227, 206, 42, 32, 64} /* left = h */,
    {58, 47, 36, 124, 137, 193, 80, 82, 78} /* left = d45 */,
    {49, 50, 35, 144, 95, 205, 63, 78, 59} /* left = d135 */,
    {41, 53, 52, 148, 71, 142, 65, 128, 51} /* left = d117 */,
    {40, 36, 28, 143, 143, 202, 40, 55, 137} /* left = d153 */,
    {52, 34, 29, 129, 183, 227, 42, 35, 43} /* left = d27 */,
    {42, 44, 44, 104, 105, 164, 64, 130, 80} /* left = d63 */,
    {43, 81, 53, 140, 169, 204, 68, 84, 72} /* left = tm */
  }
};
