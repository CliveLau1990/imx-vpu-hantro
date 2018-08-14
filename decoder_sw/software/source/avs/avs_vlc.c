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

#include "avs_vlc.h"
#include "basetype.h"
#include "avs_utils.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* definition of special code num, this along with the return value is used
 * to handle code num in the range [0, 2^32] in the DecodeExpGolombUnsigned
 * function */
#define BIG_CODE_NUM 0xFFFFFFFFU

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------

   5.1  Function: AvsDecodeExpGolombUnsigned

        Functional description:
            Decode unsigned Exp-Golomb code. This is the same as code_num used
            in other Exp-Golomb code mappings. Code num (i.e. the decoded
            symbol) is determined as

                code_num = 2^leadingZeros - 1 + GetBits(leadingZeros)

            Normal decoded symbols are in the range [0, 2^32 - 2]. Symbol
            2^32-1 is indicated by BIG_CODE_NUM with return value HANTRO_OK
            while symbol 2^32  is indicated by BIG_CODE_NUM with return value
            HANTRO_NOK.  These two symbols are special cases with code length
            of 65, i.e.  32 '0' bits, a '1' bit, and either 0 or 1 represented
            by 32 bits.

            Symbol 2^32 is out of unsigned 32-bit range but is needed for
            DecodeExpGolombSigned to express value -2^31.

        Inputs:
            dec_cont       pointer to stream data structure

        Outputs:
            code_num         decoded code word is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      failure, no valid code word found, note exception
                            with BIG_CODE_NUM

------------------------------------------------------------------------------*/

u32 AvsDecodeExpGolombUnsigned(DecContainer *dec_cont, u32 *code_num) {

  /* Variables */

  u32 bits, num_zeros;

  /* Code */

  ASSERT(dec_cont);
  ASSERT(code_num);

  bits = AvsStrmDec_ShowBits(dec_cont, 32);

  /* first bit is 1 -> code length 1 */
  if (bits >= 0x80000000) {
    if (AvsStrmDec_FlushBits(dec_cont, 1)== END_OF_STREAM)
      return(HANTRO_NOK);
    *code_num = 0;
    return(HANTRO_OK);
  }
  /* second bit is 1 -> code length 3 */
  else if (bits >= 0x40000000) {
    if (AvsStrmDec_FlushBits(dec_cont, 3) == END_OF_STREAM)
      return(HANTRO_NOK);
    *code_num = 1 + ((bits >> 29) & 0x1);
    return(HANTRO_OK);
  }
  /* third bit is 1 -> code length 5 */
  else if (bits >= 0x20000000) {
    if (AvsStrmDec_FlushBits(dec_cont, 5) == END_OF_STREAM)
      return(HANTRO_NOK);
    *code_num = 3 + ((bits >> 27) & 0x3);
    return(HANTRO_OK);
  }
  /* fourth bit is 1 -> code length 7 */
  else if (bits >= 0x10000000) {
    if (AvsStrmDec_FlushBits(dec_cont, 7) == END_OF_STREAM)
      return(HANTRO_NOK);
    *code_num = 7 + ((bits >> 25) & 0x7);
    return(HANTRO_OK);
  }
  /* other code lengths */
  else {
    num_zeros = 4 + AvsStrmDec_CountLeadingZeros(bits, 28);

    /* all 32 bits are zero */
    if (num_zeros == 32) {
      *code_num = 0;
      if (AvsStrmDec_FlushBits(dec_cont,32) == END_OF_STREAM)
        return(HANTRO_NOK);
      bits = AvsStrmDec_GetBits(dec_cont, 1);
      /* check 33rd bit, must be 1 */
      if (bits == 1) {
        /* cannot use AvsGetBits, limited to 31 bits */
        bits = AvsStrmDec_ShowBits(dec_cont,32);
        if (AvsStrmDec_FlushBits(dec_cont, 32) == END_OF_STREAM)
          return(HANTRO_NOK);
        /* code num 2^32 - 1, needed for unsigned mapping */
        if (bits == 0) {
          *code_num = BIG_CODE_NUM;
          return(HANTRO_OK);
        }
        /* code num 2^32, needed for unsigned mapping
         * (results in -2^31) */
        else if (bits == 1) {
          *code_num = BIG_CODE_NUM;
          return(HANTRO_NOK);
        }
      }
      /* if more zeros than 32, it is an error */
      return(HANTRO_NOK);
    } else if (AvsStrmDec_FlushBits(dec_cont,num_zeros+1) == END_OF_STREAM)
      return(HANTRO_NOK);


    bits = AvsStrmDec_GetBits(dec_cont, num_zeros);
    if (bits == END_OF_STREAM)
      return(HANTRO_NOK);

    *code_num = (1 << num_zeros) - 1 + bits;

  }

  return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

   5.2  Function: AvsDecodeExpGolombSigned

        Functional description:
            Decode signed Exp-Golomb code. Code num is determined by
            AvsDecodeExpGolombUnsigned and then mapped to signed
            representation as

                symbol = (-1)^(code_num+1) * (code_num+1)/2

            Signed symbols shall be in the range [-2^31, 2^31 - 1]. Symbol
            -2^31 is obtained when code_num is 2^32, which cannot be expressed
            by unsigned 32-bit value. This is signaled as a special case from
            the AvsDecodeExpGolombUnsigned by setting code_num to
            BIG_CODE_NUM and returning HANTRO_NOK status.

        Inputs:
            dec_cont       pointer to stream data structure

        Outputs:
            value           decoded code word is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      failure, no valid code word found

------------------------------------------------------------------------------*/

u32 AvsDecodeExpGolombSigned(DecContainer *dec_cont, i32 *value) {

  /* Variables */

  u32 status, code_num = 0;

  /* Code */

  ASSERT(dec_cont);
  ASSERT(value);

  status = AvsDecodeExpGolombUnsigned(dec_cont, &code_num);

  if (code_num == BIG_CODE_NUM) {
    /* BIG_CODE_NUM and HANTRO_OK status means code_num 2^32-1 which would
     * result in signed integer valued 2^31 (i.e. out of 32-bit signed
     * integer range) */
    if (status == HANTRO_OK)
      return(HANTRO_NOK);
    /* BIG_CODE_NUM and HANTRO_NOK status means code_num 2^32 which results
     * in signed integer valued -2^31 */
    else {
      *value = (i32)(2147483648U);
      return (HANTRO_OK);
    }
  } else if (status == HANTRO_OK) {
    /* (-1)^(code_num+1) results in positive sign if code_num is odd,
     * negative when it is even. (code_num+1)/2 is obtained as
     * (code_num+1)>>1 when value is positive and as (-code_num)>>1 for
     * negative value */
    /*lint -e702 */
    *value = (code_num & 0x1) ? (i32)((code_num + 1) >> 1) :
             -(i32)((code_num + 1) >> 1);
    /*lint +e702 */
    return(HANTRO_OK);
  }

  return(HANTRO_NOK);

}
