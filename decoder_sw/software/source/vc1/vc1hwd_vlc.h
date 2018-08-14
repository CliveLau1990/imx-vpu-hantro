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

#ifndef VC1HWD_VLC_H
#define VC1HWD_VLC_H

/*------------------------------------------------------------------------------
    Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "vc1hwd_stream.h"
#include "vc1hwd_picture_layer.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/* Special cases for VLC value */
#define INVALID_VLC_VALUE   (-1)

/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

/* Picture layer codewords */
picType_e vc1hwdDecodePtype( strmData_t * const strm_data, const u32 advanced,
                             const u16x max_bframes);
mvmode_e vc1hwdDecodeMvMode( strmData_t * const strm_data, const u32 b_pic,
                             const u16x pquant, u32 *p_int_comp );
mvmode_e vc1hwdDecodeMvModeB( strmData_t * const strm_data, const u16x pquant);
u16x vc1hwdDecodeTransAcFrm( strmData_t * const strm_data );
void vc1hwdDecodeVopDquant( strmData_t * const strm_data, const u16x dquant,
                            pictureLayer_t * const p_layer );
u16x vc1hwdDecodeMvRange( strmData_t * const strm_data );
bfract_e vc1hwdDecodeBfraction( strmData_t * const strm_data,
                                i16x * p_scale_factor );

fcm_e vc1hwdDecodeFcm( strmData_t * const strm_data );
u16x vc1hwdDecodeCondOver( strmData_t * const strm_data );
u16x vc1hwdDecodeRefDist( strmData_t * const strm_data );
u16x vc1hwdDecodeDmvRange( strmData_t * const strm_data );
intCompField_e vc1hwdDecodeIntCompField( strmData_t * const strm_data );

#endif /* #ifndef VC1HWD_VLC_H */
