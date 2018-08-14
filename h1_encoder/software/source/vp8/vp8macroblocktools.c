/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--         only as expressly authorized by a licensing agreement from         --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--      In the event of publication, the following notice is applicable:      --
--                                                                            --
--                   (C) COPYRIGHT 2006 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--         The entire notice above must be reproduced on all copies.          --
--                                                                            --
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Include headers
------------------------------------------------------------------------------*/

#include "vp8macroblocktools.h"
#include "vp8quanttable.h"

/*------------------------------------------------------------------------------
	External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Module defines
------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
	Local function prototypes
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	InitQuantTables		TODO delta
------------------------------------------------------------------------------*/
void InitQuantTables(vp8Instance_s *mbs)
{
	i32 i, j, tmp;
	qp *qp;

	for (i = 0; i < QINDEX_RANGE; i++) {
		/* Quant table for 1'st order luminance */
		qp = &mbs->qpY1[i];
		for (j = 0; j < 2; j++) {
			if (j == 0) {
				tmp = DcQLookup[CLIP3(i, 0, QINDEX_RANGE - 1)];
			} else {
				tmp = AcQLookup[CLIP3(i, 0, QINDEX_RANGE - 1)];
			}
			qp->quant[j]   = MIN((1 << 16) / tmp, 0x3FFF);
			qp->zbin[j]    = ((QZbinFactors[i] * tmp) + 64) >> 7;
			qp->round[j]   = (QRoundingFactors[i] * tmp) >> 7;
			qp->dequant[j] = tmp;
		}

		/* Quant table for 2'st order luminance */
		qp = &mbs->qpY2[i];
		for (j = 0; j < 2; j++) {
			if (j == 0) {
				tmp = DcQLookup[CLIP3(i, 0, QINDEX_RANGE - 1)];
				tmp = tmp * 2;
			} else {
				tmp = AcQLookup[CLIP3(i, 0, QINDEX_RANGE - 1)];
				tmp = (tmp * 155) / 100;
				if (tmp < 8) tmp = 8;
			}
			qp->quant[j]   = MIN((1 << 16) / tmp, 0x3FFF);
			qp->zbin[j]    = ((QZbinFactors[i] * tmp) + 64) >> 7;
			qp->round[j]   = (QRoundingFactors[i] * tmp) >> 7;
			qp->dequant[j] = tmp;
		}

		/* Quant table for chrominance */
		qp = &mbs->qpCh[i];
		for (j = 0; j < 2; j++) {
			if (j == 0) {
				tmp = DcQLookup[CLIP3(i, 0, QINDEX_RANGE - 1)];
				if (tmp > 132) tmp = 132;
			} else {
				tmp = AcQLookup[CLIP3(i, 0, QINDEX_RANGE - 1)];
			}
			qp->quant[j]   = MIN((1 << 16) / tmp, 0x3FFF);
			qp->zbin[j]    = ((QZbinFactors[i] * tmp) + 64) >> 7;
			qp->round[j]   = (QRoundingFactors[i] * tmp) >> 7;
			qp->dequant[j] = tmp;
		}
	}
}


