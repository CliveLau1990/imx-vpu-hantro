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
------------------------------------------------------------------------------*/

#ifndef VP8PUT_BITS_H
#define VP8PUT_BITS_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "basetype.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/
typedef struct {
	u8 *data;		/* Pointer to next byte of data buffer */
	u8 *pData;		/* Pointer to beginning of data buffer */
	i32 size;		/* Size of *data in bytes */
	i32 byteCnt;		/* Data buffer stream byte count */

	i32 range;		/* Bool encoder range [128, 255] */
	i32 bottom;		/* Bool encoder left endpoint */
	i32 bitsLeft;		/* Bool encoder bits left before flush bottom */
} vp8buffer;

typedef struct {
	i32 value;		/* Bits describe the bool tree  */
	i32 number;		/* Number, valid bit count in above tree */
	i32 index[9];		/* Probability table index */
} tree;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/
i32 VP8SetBuffer(vp8buffer *, u8 *, i32);
i32 VP8BufferOverflow(vp8buffer *);
i32 VP8BufferGap(vp8buffer *buffer, i32 gap);
void VP8PutByte(vp8buffer *buffer, i32 byte);
void VP8PutLit(vp8buffer *, i32, i32);
void VP8PutBool(vp8buffer *buffer, i32 prob, i32 boolValue);
void VP8PutTree(vp8buffer *buffer, tree const  *tree, i32 *prob);
void VP8FlushBuffer(vp8buffer *buffer);

#endif
