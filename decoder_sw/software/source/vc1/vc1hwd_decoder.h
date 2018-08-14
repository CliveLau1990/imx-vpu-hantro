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

#ifndef VC1HWD_DECODER_H
#define VC1HWD_DECODER_H

/*------------------------------------------------------------------------------
    Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "vc1decapi.h"
#include "vc1hwd_container.h"
#include "vc1hwd_storage.h"
#include "vc1hwd_util.h"
#include "vc1hwd_stream.h"
#include "vc1hwd_headers.h"

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/* MAX_BUFFER_NUMBERS */
#define INVALID_ANCHOR_PICTURE  ((u16x)(16))

#define VC1_MIN_WIDTH   48
#define VC1_MIN_HEIGHT  48
#define VC1_MIN_WIDTH_EN_DTRC   96
#define VC1_MIN_HEIGHT_EN_DTRC  48

#define MAX_NUM_MBS     ((4096>>4)*(4096>>4))

/* enumerated return values of the functions */
enum {
  VC1HWD_OK,
  VC1HWD_NOT_CODED_PIC,
  VC1HWD_PIC_RDY,
  VC1HWD_SEQ_HDRS_RDY,
  VC1HWD_ENTRY_POINT_HDRS_RDY,
  VC1HWD_END_OF_SEQ,
  VC1HWD_PIC_HDRS_RDY,
  VC1HWD_FIELD_HDRS_RDY,
  VC1HWD_ERROR,
  VC1HWD_METADATA_ERROR,
  VC1HWD_MEMORY_FAIL,
  VC1HWD_USER_DATA_RDY,
  VC1HWD_HDRS_ERROR
};

/*------------------------------------------------------------------------------
    Data types
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Function prototypes
------------------------------------------------------------------------------*/

u16x vc1hwdInit( const void *dwl, swStrmStorage_t *storage,
                 const VC1DecMetaData *p_meta_data,
                 u32 num_frame_buffers );

u16x vc1hwdDecode( decContainer_t *dec_cont,
                   swStrmStorage_t *storage,
                   strmData_t *strm_data );

u16x vc1hwdRelease(const void *dwl,
                   swStrmStorage_t *storage );

u16x vc1hwdUnpackMetaData( const u8 *p_buffer, VC1DecMetaData *MetaData );

void vc1hwdErrorConcealment( const u16x flush,
                             swStrmStorage_t * storage );

u16x vc1hwdNextPicture( swStrmStorage_t * storage, u16x * p_next_picture,
                        u32* p_field_to_ret,  u16x end_of_stream, u32 deinterlace,
                        u32* p_pic_id, u32* decode_id, u32* err_mbs );

u16x vc1hwdBufferPicture( decContainer_t *dec_cont, u16x pic_to_buffer,
                          u16x buffer_b, u16x pic_id, u16x err_mbs );

u32 vc1hwdSeekFrameStart( swStrmStorage_t * storage,
                          strmData_t *p_strm_data );

void vc1hwdSetPictureInfo( decContainer_t *dec_cont, u32 pic_id );

void vc1hwdUpdateWorkBufferIndexes( decContainer_t *dec_cont, u32 is_bpic );

#endif /* #ifndef VC1HWD_DECODER_H */

