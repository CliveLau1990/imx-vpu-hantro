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

#include "mpeg2hwd_strm.h"
#include "mpeg2hwd_utils.h"
#include "mpeg2hwd_headers.h"
#include "mpeg2hwd_debug.h"
/*------------------------------------------------------------------------------
    2. External identifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

enum {
  CONTINUE
};

/*------------------------------------------------------------------------------
    4. Module indentifiers
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.1  Function name: mpeg2_strm_dec_decode

        Purpose: Decode MPEG2 stream. Continues decoding until END_OF_STREAM
        encountered or whole frame decoded. Returns after decoding of sequence layer header.

        Input:
            Pointer to DecContainer structure
                -uses and updates StrmStorage
                -uses and updates StrmDesc
                -uses and updates FrameDesc
                -uses Hdrs
                -uses MbSetDesc

        Output:
            DEC_RDY if everything was ok but no FRAME finished
            DEC_HDR_RDY if headers decoded
            DEC_HDR_RDY_BUF_NOT_EMPTY if headers decoded but buffer not empty
            DEC_PIC_RDY if whole FRAME decoded
            DEC_PIC_RDY_BUF_NOT_EMPTY if whole FRAME decoded but buffer not empty
            DEC_END_OF_STREAM if eos encountered while decoding
            DEC_ERROR if such an error encountered that recovery needs initial
                      headers

------------------------------------------------------------------------------*/

u32 mpeg2_strm_dec_decode(DecContainer * dec_container) {

  u32 status;
  u32 start_code;

  MPEG2DEC_DEBUG(("Entry StrmDec_Decode\n"));

  status = HANTRO_OK;

  /* keep decoding till something ready or something wrong */
  do {
    start_code = mpeg2_strm_dec_next_start_code(dec_container);

    /* parse headers */
    switch (start_code) {
    case SC_SEQUENCE:
      /* Sequence header */
      status = mpeg2_strm_dec_decode_sequence_header(dec_container);
      dec_container->StrmStorage.valid_sequence = status == HANTRO_OK;
      if( dec_container->StrmStorage.new_headers_change_resolution)
        return DEC_END_OF_STREAM;
      break;

    case SC_GROUP:
      /* GOP header */
      status = mpeg2_strm_dec_decode_gopheader(dec_container);
      break;

    case SC_EXTENSION:
      /* Extension headers */
      status = mpeg2_strm_dec_decode_extension_header(dec_container);
      if(status == DEC_PIC_HDR_RDY_ERROR)
        return DEC_PIC_HDR_RDY_ERROR;
      break;

    case SC_PICTURE:
      /* Picture header */
      /* decoder still in "initialization" phase and sequence headers
       * successfully decoded -> set to normal state */
      if(dec_container->StrmStorage.strm_dec_ready == FALSE &&
          dec_container->StrmStorage.valid_sequence) {
        dec_container->StrmStorage.strm_dec_ready = TRUE;
        dec_container->StrmDesc.strm_buff_read_bits -= 32;
        dec_container->StrmDesc.strm_curr_pos -= 4;
        return (DEC_HDRS_RDY);
      } else if(dec_container->StrmStorage.strm_dec_ready) {
        status = mpeg2_strm_dec_decode_picture_header(dec_container);
        if(status != HANTRO_OK)
          return (DEC_PIC_HDR_RDY_ERROR);
        dec_container->StrmStorage.valid_pic_header = 1;

        if(dec_container->Hdrs.low_delay &&
            dec_container->Hdrs.picture_coding_type == BFRAME) {
          return (DEC_PIC_SUPRISE_B);
        }
      }
      break;

    case SC_SLICE:
      /* start decoding picture data (HW) if decoder is in normal
       * decoding state and picture headers have been successfully
       * decoded */
      if(dec_container->StrmStorage.strm_dec_ready == TRUE &&
          dec_container->StrmStorage.valid_pic_header &&
          (!dec_container->Hdrs.mpeg2_stream ||
           dec_container->StrmStorage.valid_pic_ext_header)) {
        /* handle stream positions and return */
        dec_container->StrmDesc.strm_buff_read_bits -= 32;
        dec_container->StrmDesc.strm_curr_pos -= 4;
        return (DEC_PIC_HDR_RDY);
      }
      break;

    case END_OF_STREAM:
      return (DEC_END_OF_STREAM);

    default:
      break;
    }

  }
  /*lint -e(506) */ while(1);

  /* execution never reaches this point (hope so) */
  /*lint -e(527) */ return (DEC_END_OF_STREAM);
  /*lint -restore */
}
