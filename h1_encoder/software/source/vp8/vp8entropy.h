/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2006 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
--
--  Abstract : Encoder initialization and setup
--
------------------------------------------------------------------------------*/

#ifndef VP8ENTROPY_H
#define VP8ENTROPY_H

/*------------------------------------------------------------------------------
	Include headers
------------------------------------------------------------------------------*/
#include "vp8entropytools.h"
#include "vp8putbits.h"
#include "vp8instance.h"

/*------------------------------------------------------------------------------
	External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
	Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Function prototypes
------------------------------------------------------------------------------*/
void EncSwapEndianess(u32 * buf, u32 sizeBytes);
void InitEntropy(vp8Instance_s *inst);
void WriteEntropyTables(vp8Instance_s *inst, u32 fullRefresh);
void CoeffProb(vp8buffer *buffer, i32 curr[4][8][3][11], i32 prev[4][8][3][11]);
void MvProb(vp8buffer *buffer, i32 curr[2][19], i32 prev[2][19]);
i32 CostMv(i32 mvd, i32 *mvProb);
void SetModeCosts(vp8Instance_s *inst, i32 coeff, i32 segment);

#endif
