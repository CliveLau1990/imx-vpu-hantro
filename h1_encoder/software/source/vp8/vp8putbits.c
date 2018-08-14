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

#include "enccommon.h"
#include "vp8putbits.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    SetBuffer
    Input   buffer  Pointer to the buffer structure.
        data    Pointer to data buffer.
        size    Size of data buffer.
    Return  ENCHW_OK    Buffer status is OK.
        ENCHW_NOK   Buffer overflow.
------------------------------------------------------------------------------*/
i32 VP8SetBuffer(vp8buffer *buffer, u8 *data, i32 size)
{
    if ((buffer == NULL) || (data == NULL) || (size < 1)) return ENCHW_NOK;

    buffer->data = data;
    buffer->pData = data;       /* First position of buffer */
    buffer->size = size;        /* Buffer size in bytes */

    buffer->range = 255;
    buffer->bottom = 0;             /* PutBool bottom */
    buffer->bitsLeft = 24;

    buffer->byteCnt = 0;

    return ENCHW_OK;
}

/*------------------------------------------------------------------------------
    BufferOverflow checks fullness of buffer.
    Input   buffer  Pointer to the buffer stucture.
    Return  ENCHW_OK    Buffer is OK.
        ENCHW_NOK   Buffer overflow
------------------------------------------------------------------------------*/
i32 VP8BufferOverflow(vp8buffer *buffer)
{
    if (buffer->size > 0) {
        return ENCHW_OK;
    }

    return ENCHW_NOK;
}

/*------------------------------------------------------------------------------
    BufferGap checks fullness of buffer. If bytes left less than gap, set
    size to zero.
    Input   buffer  Pointer to the buffer stucture.
    Return  ENCHW_OK    Bytes left >= gap
            ENCHW_NOK   Byter left < gap
------------------------------------------------------------------------------*/
i32 VP8BufferGap(vp8buffer *buffer, i32 gap)
{
    if ((buffer->data - buffer->pData) + gap > buffer->size) {
        buffer->size = 0;
        return ENCHW_NOK;
    }

    return ENCHW_OK;
}

/*------------------------------------------------------------------------------
    PutByte write byte literallyt to next place of buffer and advance data
    pointer to next place

    Input   buffer      Pointer to the buffer stucture
            value       Byte
------------------------------------------------------------------------------*/
void VP8PutByte(vp8buffer *buffer, i32 byte)
{
    ASSERT((u32)byte < 256);
    ASSERT(buffer->data < buffer->pData + buffer->size);
    TRACE_BIT_STREAM(byte, 8);
    *buffer->data++ = byte;
    buffer->byteCnt++;
}

/*------------------------------------------------------------------------------
    PutBool128  Optimized with fixed prob=128
------------------------------------------------------------------------------*/
void VP8PutBool128(vp8buffer *buffer, i32 boolValue)
{
    i32 split = 1 + ((buffer->range - 1) >> 1);
    i32 lengthBits = 0;
    i32 bits = 0;

    if (boolValue) {
        buffer->bottom += split;
        buffer->range -= split;
    } else {
        buffer->range = split;
    }

#ifdef TRACE_STREAM
    while ((buffer->range << bits) < 128) bits++;
    EncTraceStream(boolValue, bits);
    bits = 0;
#endif

    while (buffer->range < 128) {
        /* Detect carry and add carry bit to already written
         * buffer->data if needed */
        if (buffer->bottom < 0) {
            u8 *data = buffer->data;
            while (*--data == 255) {
                *data = 0;
            }
            (*data)++;
        }
        buffer->range <<= 1;
        buffer->bottom <<= 1;

        if (!--buffer->bitsLeft) {
            lengthBits += 8;
            bits <<= 8;
            bits |= (buffer->bottom >> 24) & 0xff;
            *buffer->data++ = (buffer->bottom >> 24) & 0xff;
            buffer->byteCnt++;
            buffer->bottom &= 0xffffff;     /* Keep 3 bytes */
            buffer->bitsLeft = 8;
            /* TODO use big enough buffer and check buffer status
             * for example in the beginning of mb row */
            ASSERT(buffer->data < buffer->pData + buffer->size - 1);
        }
    }

}

/*------------------------------------------------------------------------------
    PutLit write "literal" bits to stream using PutBool() where probability
    is 128. Note that real bits written to stream are not necessarily same
    than literal value. Bit write order: MSB...LSB.
------------------------------------------------------------------------------*/
void VP8PutLit(vp8buffer *buffer, i32 value, i32 number)
{
    ASSERT(number < 32 && number > 0);
    ASSERT(((value & (-1 << number)) == 0));

    while (number--) {
        VP8PutBool128(buffer, (value >> number) & 0x1);
        if (number) {
            COMMENT("..");
        }
    }
}

/*------------------------------------------------------------------------------
    PutBool
------------------------------------------------------------------------------*/
void VP8PutBool(vp8buffer *buffer, i32 prob, i32 boolValue)
{
    i32 split = 1 + ((buffer->range - 1) * prob >> 8);
    i32 lengthBits = 0;
    i32 bits = 0;
#if 0
    i32 valueIn = buffer->bottom;
    i32 rangeIn = buffer->range;
#endif

    if (boolValue) {
        buffer->bottom += split;
        buffer->range -= split;
    } else {
        buffer->range = split;
    }

#ifdef TRACE_STREAM
    while ((buffer->range << bits) < 128) bits++;
    EncTraceStream(boolValue, bits);
    bits = 0;
#endif

    while (buffer->range < 128) {
        /* Detect carry and add carry bit to already written
         * buffer->data if needed */
        if (buffer->bottom < 0) {
            u8 *data = buffer->data;
            while (*--data == 255) {
                *data = 0;
            }
            (*data)++;
        }
        buffer->range <<= 1;
        buffer->bottom <<= 1;

        if (!--buffer->bitsLeft) {
            lengthBits += 8;
            bits <<= 8;
            bits |= (buffer->bottom >> 24) & 0xff;
            *buffer->data++ = (buffer->bottom >> 24) & 0xff;
            buffer->byteCnt++;
            buffer->bottom &= 0xffffff;     /* Keep 3 bytes */
            buffer->bitsLeft = 8;
            /* TODO use big enough buffer and check buffer status
             * for example in the beginning of mb row */
            ASSERT(buffer->data < buffer->pData + buffer->size - 1);
        }
    }

#if 0
    /* Debug bool enc */
    fprintf(stdout, "%3i %3i %3i %3i %3i %3i  %i %3i %3i %3i %3i %3i %2i  ",
            (valueIn>>24)&0xFF, (valueIn>>16)&0xFF, (valueIn>>8)&0xFF, valueIn&0xFF,
            rangeIn, prob, boolValue,
            (buffer->bottom>>24)&0xFF, (buffer->bottom>>16)&0xFF,
            (buffer->bottom>>8)&0xFF, buffer->bottom&0xFF,
            buffer->range, lengthBits);

    if (lengthBits > 16)
        fprintf(stdout, "%3u ", (bits>>16)&0xFFFF);   /* 16 MSB */
    if (lengthBits)
        fprintf(stdout, "%3u", bits&0xFFFF);        /* 16 LSB */

    fprintf(stdout, "\n");
#endif
}

/*------------------------------------------------------------------------------
    PutTree
------------------------------------------------------------------------------*/
void VP8PutTree(vp8buffer *buffer, tree const  *tree, i32 *prob)
{
    i32 value = tree->value;
    i32 number = tree->number;
    i32 const *index = tree->index;

    while (number--) {
        VP8PutBool(buffer, prob[*index++], (value >> number) & 1);
    }
}

/*------------------------------------------------------------------------------
    FlushBuffer put remaining buffer->bottom bits to the stream
------------------------------------------------------------------------------*/
void VP8FlushBuffer(vp8buffer *buffer)
{
    i32 bitsLeft = buffer->bitsLeft;
    i32 bottom = buffer->bottom;

    /* Detect (unlikely) carry and add carry bit to already written
     * buffer->data if needed */
    if (bottom & (1 << (32 - bitsLeft))) {
        u8 *data = buffer->data;
        while (*--data == 255) {
            *data = 0;
        }
        (*data)++;
    }

    /* Move remaining bits to left until byte boundary */
    bottom <<= (bitsLeft & 0x7);

    /* Move remaining bytes to left until word boundary */
    bottom <<= (bitsLeft >> 3) * 8;

    /* Write valid (and possibly padded) bits to stream */
    *buffer->data++ = 0xff & (bottom >> 24);
    *buffer->data++ = 0xff & (bottom >> 16);
    *buffer->data++ = 0xff & (bottom >> 8);
    *buffer->data++ = 0xff & bottom;
    buffer->byteCnt += 4;

    TRACE_BIT_STREAM(bottom, 32);

    COMMENT("flush");

}


