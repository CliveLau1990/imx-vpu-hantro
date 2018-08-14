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
#include "vp6booldec.h"

/****************************************************************************
 *
 *  ROUTINE       :     VP6HWDecodeBool
 *
 *  INPUTS        :     BOOL_CODER *br  : pointer to instance of a boolean decoder.
 *            int probability : probability that next symbol is a 0 (0-255)
 *
 *  OUTPUTS       :     None.
 *
 *  RETURNS       :   Next decoded symbol (0 or 1)
 *
 *  FUNCTION      :     Decodes the next symbol (0 or 1) using the specified
 *                      boolean decoder.
 *
 *  SPECIAL NOTES :     None.
 *
 ****************************************************************************/
u32 VP6HWDecodeBool(BOOL_CODER * br, i32 probability) {
  u32 bit = 0;
  u32 split;
  u32 bigsplit;
  u32 count = br->count;
  u32 range = br->range;
  u32 value = br->value;

  split = 1 + (((range - 1) * probability) >> 8);
  bigsplit = (split << 24);
  range = split;

  if(value >= bigsplit) {
    range = br->range - split;
    value = value - bigsplit;
    bit = 1;
  }

  if(range >= 0x80) {
    br->value = value;
    br->range = range;
    return bit;
  } else {
    do {
      range += range;
      value += value;

      if(!--count) {
        /* no more stream to read? */
        if (br->pos >= br->stream_end_pos) {
          br->strm_error = 1;
          break;
        }
        count = 8;
        value |= br->buffer[br->pos];
        br->pos++;
      }
    } while(range < 0x80);
  }

  br->count = count;
  br->value = value;
  br->range = range;

  return bit;
}

/****************************************************************************
 *
 *  ROUTINE       :     VP6HWDecodeBool128
 *
 *  INPUTS        :     BOOL_CODER *br : pointer to instance of a boolean decoder.
 *
 *  OUTPUTS       :     None.
 *
 *  RETURNS       :   int: Next decoded symbol (0 or 1)
 *
 *  FUNCTION      :     This function determines the next value stored in the
 *            boolean coder based upon a fixed probability of 0.5
 *                      (128 in normalized units).
 *
 *  SPECIAL NOTES :     VP6HWDecodeBool128() is a special case of VP6HWDecodeBool()
 *                      where the input probability is fixed at 128.
 *
 ****************************************************************************/
u32 VP6HWDecodeBool128(BOOL_CODER * br) {

  u32 bit;
  u32 split;
  u32 bigsplit;
  u32 count = br->count;
  u32 range = br->range;
  u32 value = br->value;

  split = (range + 1) >> 1;
  bigsplit = (split << 24);

  if(value >= bigsplit) {
    range = (range - split) << 1;
    value = (value - bigsplit) << 1;
    bit = 1;
  } else {
    range = split << 1;
    value = value << 1;
    bit = 0;
  }

  if(!--count) {
    /* no more stream to read? */
    if (br->pos >= br->stream_end_pos) {
      br->strm_error = 1;
      return 0; /* any value, not valid */
    }
    count = 8;
    value |= br->buffer[br->pos];
    br->pos++;
  }

  br->count = count;
  br->value = value;
  br->range = range;

  return bit;

}

/****************************************************************************
 *
 *  ROUTINE       :     VP6HWStartDecode
 *
 *  INPUTS        :     BOOL_CODER *bc      : pointer to instance of a boolean decoder.
 *            u8 *source : pointer to buffer of data to be decoded.
 *
 *  OUTPUTS       :     None.
 *
 *  RETURNS       :     void
 *
 *  FUNCTION      :     Performs initialization of the boolean decoder.
 *
 *  SPECIAL NOTES :     None.
 *
 ****************************************************************************/
void VP6HWStartDecode(BOOL_CODER * br, u8 * source, u32 len) {

  br->lowvalue = 0;
  br->range = 255;
  br->count = 8;
  br->buffer = source;
  br->pos = 0;

  br->value =
    (br->buffer[0] << 24) + (br->buffer[1] << 16) + (br->buffer[2] << 8) +
    (br->buffer[3]);

  br->pos += 4;

  br->stream_end_pos = len;
  br->strm_error = br->pos > br->stream_end_pos;
}

/****************************************************************************
 *
 *  ROUTINE       :     VP6HWStopDecode
 *
 *  INPUTS        :     BOOL_CODER *bc : pointer to instance of a boolean decoder (UNUSED).
 *
 *  OUTPUTS       :     None.
 *
 *  RETURNS       :     void
 *
 *  FUNCTION      :     Performs clean-up of the specified boolean decoder.
 *
 *  SPECIAL NOTES :     None.
 *
 ****************************************************************************/
void VP6HWStopDecode(BOOL_CODER * bc) {
  (void) bc;
}

/****************************************************************************
 *
 *  ROUTINE       :     VP6HWbitread
 *
 *  INPUTS        :     BOOL_CODER *br : Pointer to a Bool Decoder instance.
 *                      int bits       : Number of bits to be read from input stream.
 *
 *  OUTPUTS       :     None.
 *
 *  RETURNS       :     UINT32: The requested bits.
 *
 *  FUNCTION      :     Decodes the requested number of bits from the encoded data buffer.
 *
 *  SPECIAL NOTES :     None.
 *
 ****************************************************************************/
u32 VP6HWbitread(BOOL_CODER * br, i32 bits) {
  u32 z = 0;
  i32 bit;

  for(bit = bits - 1; bit >= 0; bit--) {
    z |= (VP6HWDecodeBool128(br) << bit);
  }

  return z;
}
