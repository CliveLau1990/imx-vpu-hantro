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

#include "h264hwd_util.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

#ifdef HANTRO_PEDANTIC_MODE
/* look-up table for expected values of stuffing bits */
static const u32 stuffing_table[8] = {0x1,0x2,0x4,0x8,0x10,0x20,0x40,0x80};
#endif

/* look-up table for chroma quantization parameter as a function of luma QP */
const u32 h264bsd_qp_c[52] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,
                              20,21,22,23,24,25,26,27,28,29,29,30,31,32,32,33,34,34,35,35,36,36,37,37,37,
                              38,38,38,39,39,39,39
                             };

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 MoreRbspTrailingData(strmData_t *p_strm_data);
/*------------------------------------------------------------------------------

   5.1  Function: h264bsdCountLeadingZeros

        Functional description:
            Count leading zeros in a code word. Code word is assumed to be
            right-aligned, last bit of the code word in the lsb of the value.

        Inputs:
            value   code word
            length  number of bits in the code word

        Outputs:
            none

        Returns:
            number of leading zeros in the code word

------------------------------------------------------------------------------*/

u32 h264bsdCountLeadingZeros(u32 value, u32 length) {

  /* Variables */

  u32 zeros = 0;
  u32 mask = 1 << (length - 1);

  /* Code */

  ASSERT(length <= 32);

  while (mask && !(value & mask)) {
    zeros++;
    mask >>= 1;
  }
  return(zeros);

}

/*------------------------------------------------------------------------------

   5.2  Function: h264bsdRbspTrailingBits

        Functional description:
            Check Raw Byte Stream Payload (RBSP) trailing bits, i.e. stuffing.
            Rest of the current byte (whole byte if allready byte aligned)
            in the stream buffer shall contain a '1' bit followed by zero or
            more '0' bits.

        Inputs:
            p_strm_data   pointer to stream data structure

        Outputs:
            none

        Returns:
            HANTRO_OK      RBSP trailing bits found
            HANTRO_NOK     otherwise

------------------------------------------------------------------------------*/

u32 h264bsdRbspTrailingBits(strmData_t *p_strm_data) {

  /* Variables */

  u32 stuffing;
  u32 stuffing_length;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->bit_pos_in_word < 8);

  stuffing_length = 8 - p_strm_data->bit_pos_in_word;

  stuffing = h264bsdGetBits(p_strm_data, stuffing_length);
  if (stuffing == END_OF_STREAM)
    return(HANTRO_NOK);

#ifdef HANTRO_PEDANTIC_MODE
  if (stuffing != stuffing_table[stuffing_length - 1])
    return(HANTRO_NOK);
  else
#endif
    return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

   5.2  Function: h264bsdRbspSliceTrailingBits

        Functional description:
            Check slice trailing bits if CABAC is on.

        Inputs:
            p_strm_data   pointer to stream data structure

        Outputs:
            none

        Returns:
            HANTRO_OK      RBSP slice trailing bits found
            HANTRO_NOK     otherwise

------------------------------------------------------------------------------*/
#ifdef H264_CABAC
u32 h264bsdRbspSliceTrailingBits(strmData_t *p_strm_data) {

  /* Variables */

  u32 czw;
  u32 tmp;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->bit_pos_in_word < 8);

  if( p_strm_data->bit_pos_in_word > 0 )
    h264bsdRbspTrailingBits( p_strm_data );

  while(MoreRbspTrailingData( p_strm_data )) {
    /* Read 16 bits */
    czw = h264bsdShowBits(p_strm_data, 16 );
    tmp = h264bsdFlushBits(p_strm_data, 16);
    if (tmp == END_OF_STREAM || czw)
      return(END_OF_STREAM);
  }

  return (HANTRO_OK);
}
#endif /* H264_CABAC */

/*------------------------------------------------------------------------------

   5.6  Function: MoreRbspTrailingData

        Functional description:
          <++>
        Inputs:
          <++>
        Outputs:
          <++>

------------------------------------------------------------------------------*/

u32 MoreRbspTrailingData(strmData_t *p_strm_data) {

  /* Variables */

  i32 bits;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->strm_buff_read_bits <= 8 * p_strm_data->strm_buff_size);

  bits = (i32)p_strm_data->strm_buff_size*8 - (i32)p_strm_data->strm_buff_read_bits;
  if ( bits >= 8 )
    return(HANTRO_TRUE);
  else
    return(HANTRO_FALSE);

}

/*------------------------------------------------------------------------------

   5.3  Function: h264bsdMoreRbspData

        Functional description:
            Check if there is more data in the current RBSP. The standard
            defines this function so that there is more data if
                -more than 8 bits left or
                -last bits are not RBSP trailing bits

        Inputs:
            p_strm_data   pointer to stream data structure

        Outputs:
            none

        Returns:
            HANTRO_TRUE    there is more data
            HANTRO_FALSE   no more data

------------------------------------------------------------------------------*/

u32 h264bsdMoreRbspData(strmData_t *p_strm_data) {

  /* Variables */

  u32 bits;

  /* Code */

  ASSERT(p_strm_data);
  ASSERT(p_strm_data->strm_buff_read_bits <= 8 * p_strm_data->strm_buff_size);

  bits = p_strm_data->strm_buff_size * 8 - p_strm_data->strm_buff_read_bits;

  if (bits == 0)
    return(HANTRO_FALSE);

  if (bits > 8) {
    if (p_strm_data->remove_emul3_byte)
      return(HANTRO_TRUE);

    bits &= 0x7;
    if (!bits) bits = 8;
    if (h264bsdShowBits(p_strm_data, bits) != (1U << (bits-1)) ||
        (h264bsdShowBits(p_strm_data, 23+bits) << 9))
      return(HANTRO_TRUE);
    else
      return(HANTRO_FALSE);
  } else if (h264bsdShowBits(p_strm_data,bits) != (1U << (bits-1)) )
    return(HANTRO_TRUE);
  else
    return(HANTRO_FALSE);

}

/*------------------------------------------------------------------------------

   5.4  Function: h264bsdNextMbAddress

        Functional description:
            Get address of the next macroblock in the current slice group.

        Inputs:
            p_slice_group_map      slice group for each macroblock
            pic_size_in_mbs        size of the picture
            curr_mb_addr          where to start

        Outputs:
            none

        Returns:
            address of the next macroblock
            0   if none of the following macroblocks belong to same slice
                group as curr_mb_addr

------------------------------------------------------------------------------*/

u32 h264bsdNextMbAddress(u32 *p_slice_group_map, u32 pic_size_in_mbs, u32 curr_mb_addr) {

  /* Variables */

  u32 i, slice_group;

  /* Code */

  ASSERT(p_slice_group_map);
  ASSERT(pic_size_in_mbs);
  ASSERT(curr_mb_addr < pic_size_in_mbs);

  slice_group = p_slice_group_map[curr_mb_addr];

  i = curr_mb_addr + 1;

  while ((i < pic_size_in_mbs) && (p_slice_group_map[i] != slice_group))
    i++;

  if (i == pic_size_in_mbs)
    i = 0;

  return(i);

}

/*------------------------------------------------------------------------------

   5.4  Function: h264CheckCabacZeroWords

        Functional description:

        Inputs:

        Outputs:

        Returns:

------------------------------------------------------------------------------*/

u32 h264CheckCabacZeroWords( strmData_t *strm_data ) {

  /* Variables */

  u32 tmp;

  /* Code */

  ASSERT(strm_data);

  while (MoreRbspTrailingData(strm_data)) {
    tmp = h264bsdGetBits(strm_data, 16 );
    if( tmp == END_OF_STREAM )
      return HANTRO_OK;
    else if ( tmp )
      return HANTRO_NOK;
  }

  return HANTRO_OK;

}

