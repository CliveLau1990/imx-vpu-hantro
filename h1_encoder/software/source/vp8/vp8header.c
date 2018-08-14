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
--  Abstract  :   VP8 stream headers
--
------------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "vp8header.h"
#include "vp8entropy.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/
static void Segmentation(vp8buffer *buffer, sps *sps, ppss *ppss,
        entropy *entropy, mbs *mbs);
static void FilterLevelDelta(vp8buffer *buffer, sps *sps);
static void QuantDelta(vp8buffer *buffer, sps *sps);

/*------------------------------------------------------------------------------
	FrameHeader
------------------------------------------------------------------------------*/
void VP8FrameHeader(vp8Instance_s *container)
{
	refPic *cur_pic = container->picBuffer.cur_pic;
	refPic *refPicList = container->picBuffer.refPicList;
	entropy *entropy = container->entropy;
	sps *sps = &container->sps;                 /* Sequence container */
	pps *pps = container->ppss.pps;
	vp8buffer *buffer = &container->buffer[1];  /* "Frame header" buffer */

	/* Color space and pixel Type (Key frames only) */
	if (cur_pic->i_frame) {
		VP8PutLit(buffer, sps->colorType, 1);
		COMMENT("Frame header, buffer 1, color space type");

		VP8PutLit(buffer, sps->clampType, 1);
		COMMENT("Frame header, buffer 1, clamping type");
	}

	/* Segmentation */
	VP8PutLit(buffer, pps->segmentEnabled, 1);
	COMMENT("Frame header, buffer 1, segmentation flag");
	if (pps->segmentEnabled) {
		Segmentation(buffer, &container->sps, &container->ppss,
				entropy, &container->mbs);
	}

	VP8PutLit(buffer, sps->filterType, 1);
	COMMENT("Frame header, buffer 1, filter type");

	VP8PutLit(buffer, sps->filterLevel, 6);
	COMMENT("Frame header, buffer 1, filter level");

	VP8PutLit(buffer, sps->filterSharpness, 3);
	COMMENT("Frame header, buffer 1, filter sharpness level");

	/* Loop filter adjustments */
	VP8PutLit(buffer, sps->filterDeltaEnable, 1);
	COMMENT("Frame header, buffer 1, loop filter adjustments");
	if (sps->filterDeltaEnable) {
		/* Filter level delta references reset by key frame */
		if (cur_pic->i_frame) {
			EWLmemset(sps->oldRefDelta, 0, sizeof(sps->refDelta));
			EWLmemset(sps->oldModeDelta, 0, sizeof(sps->modeDelta));
		}
		FilterLevelDelta(buffer, sps);
	}

	VP8PutLit(buffer, sps->dctPartitions, 2);
	COMMENT("Frame header, buffer 1, token partition");

	VP8PutLit(buffer, container->rateControl.qpHdr, 7);
	COMMENT("Frame header, buffer 1, YacQi quantizer index");

	/* Delta quantization index */
	QuantDelta(buffer, sps);

	/* Update grf and arf buffers and sing bias, see decodframe.c 863.
	 * TODO swaping arg->grf and grf->arf in the same time is not working
	 * because of bug in the libvpx? */
	if (!cur_pic->i_frame) {
		/* Input picture after reconstruction is set to new grf/arf */
		VP8PutLit(buffer, cur_pic->grf, 1);	/* Grf refresh */
		COMMENT("Frame header, buffer 1, grf refresh");
		VP8PutLit(buffer, cur_pic->arf, 1);	/* Arf refresh */
		COMMENT("Frame header, buffer 1, arf refresh");

		if (!cur_pic->grf) {
			if (refPicList[0].grf) {
				VP8PutLit(buffer, 1, 2);	/* Ipf -> grf */
				COMMENT("Frame header, buffer 1, ipf -> grf");
			} else if (refPicList[2].grf) {
				VP8PutLit(buffer, 2, 2);	/* Arf -> grf */
				COMMENT("Frame header, buffer 1, arf -> grf");
			} else {
				VP8PutLit(buffer, 0, 2);	/* Not updated */
				COMMENT("Frame header, buffer 1, no update");
			}
		}

		if (!cur_pic->arf) {
			if (refPicList[0].arf) {
				VP8PutLit(buffer, 1, 2);	/* Ipf -> arf */
				COMMENT("Frame header, buffer 1, ipf -> arf");
			} else if (refPicList[1].arf) {
				VP8PutLit(buffer, 2, 2);	/* Grf -> arf */
				COMMENT("Frame header, buffer 1, grf -> arf");
			} else {
				VP8PutLit(buffer, 0, 2);	/* Not updated */
				COMMENT("Frame header, buffer 1, no update");
			}
		}

		/* Sing bias. TODO adaptive sing bias. */
		VP8PutLit(buffer,sps->singBias[1], 1);	/* Grf */
		COMMENT("Frame header, buffer 1, grf sign bias");
		VP8PutLit(buffer,sps->singBias[2], 1);	/* Arf */
		COMMENT("Frame header, buffer 1, arf sign bias");
	}


	/* RefreshEntropyProbs, if 0 -> put default proabilities. If 1
	 * use previous frame probabilities */
	VP8PutLit(buffer, sps->refreshEntropy, 1);
	COMMENT("Frame header, buffer 1, Refresh entropy probs flag");

	/* RefreshLastFrame flag. Note that key frame always updates ipf */
	if (!cur_pic->i_frame) {
		VP8PutLit(buffer, cur_pic->ipf, 1);
		COMMENT("Frame header, buffer 1, ipf refresh last frame flag");
	}

	/* Coeff probabilities, TODO: real updates */
	CoeffProb(buffer, entropy->coeffProb, entropy->oldCoeffProb);

	/*  mb_no_coeff_skip . This flag indicates at the frame level if
	 *  skipping of macroblocks with no non-zero coefficients is enabled.
	 *  If it is set to 0 then prob_skip_false is not read and
	 *  mb_skip_coeff is forced to 0 for all macroblocks (see Sections 11.1
	 *  and 12.1). TODO  */
	VP8PutLit(buffer, 1, 1);
	COMMENT("Frame header, buffer 1, mb no coeff skip");
	/* Probability used for decoding noCoeff flag, depens above flag TODO*/
	VP8PutLit(buffer, entropy->skipFalseProb, 8);
	COMMENT("Frame header, buffer 1, skip false prob");

	if (cur_pic->i_frame) return;

	/* The rest are inter frame only */

	/* Macroblock is intra predicted probability */
	VP8PutLit(buffer, entropy->intraProb, 8);
	COMMENT("Frame header, buffer 1, intra prob");

	/* Inter is predicted from immediately previous frame probability */
	VP8PutLit(buffer, entropy->lastProb, 8);
	COMMENT("Frame header, buffer 1, last prob");

	/* Inter is predicted from golden frame probability */
	VP8PutLit(buffer, entropy->gfProb, 8);
	COMMENT("Frame header, buffer 1, gf prob");

	/* Intra mode probability updates not supported yet TODO */
	VP8PutLit(buffer, 0, 1);
	COMMENT("Frame header, buffer 1, intra mode prob update");

	/* Intra chroma probability updates not supported yet TODO */
	VP8PutLit(buffer, 0, 1);
	COMMENT("Frame header, buffer 1, intra chroma prob update");

	/* Motion vector probability update not supported yet TOTO real updates */
	MvProb(buffer, entropy->mvProb, entropy->oldMvProb);
}

/*------------------------------------------------------------------------------
	FrameHeaderFinish   Frame header is valid, store the new values as reference
------------------------------------------------------------------------------*/
void VP8FrameHeaderFinish(vp8Instance_s *container)
{
	ppss *ppss = &container->ppss;
	pps *pps = container->ppss.pps;
	sps *sps = &container->sps;                 /* Sequence container */

	if (pps->segmentEnabled) {
		/* This point new segmentation data is written to the stream, save new
		 * values because they are reference values of next frame */
		EWLmemcpy(ppss->qpSgm, pps->qpSgm, sizeof(ppss->qpSgm));
		EWLmemcpy(ppss->levelSgm, pps->levelSgm, sizeof(ppss->levelSgm));
	}

	if (sps->filterDeltaEnable) {
		/* Store the new values as reference for next frame */
		EWLmemcpy(sps->oldRefDelta, sps->refDelta, sizeof(sps->refDelta));
		EWLmemcpy(sps->oldModeDelta, sps->modeDelta, sizeof(sps->modeDelta));
	}
}

/*------------------------------------------------------------------------------
	FrameTag
------------------------------------------------------------------------------*/
void VP8FrameTag(vp8Instance_s *container)
{
	picBuffer *picBuffer = &container->picBuffer;
	refPic *cur_pic = picBuffer->cur_pic;
	sps *sps = &container->sps;                 /* Sequence container */
	vp8buffer *buffer = &container->buffer[0];  /* Frame tag buffer */
	i32 tmp;

	/* Frame tag contains (lsb first):
	 * 1. A 1-bit frame type (0 for key frames, 1 for inter frames)
	 * 2. A 3-bit version number (0 - 3 are defined as 4 different profiles
	 * 3. A 1-bit showFrame flag (1 when current frame is display)
	 * 4. A 19-bit size of the first data partition in bytes
	 * Note that frame tag is written to the stream in little endian mode */

	tmp = ((container->buffer[1].byteCnt) << 5) |
		((cur_pic->show ? 1 : 0) << 4) |
		(container->sps.profile << 1) |
		(cur_pic->i_frame ? 0 : 1);

	/* Note that frame tag is written _really_ literal to buffer, don't use
	 * VP8PutLit() use VP8PutBit() instead */

	VP8PutByte(buffer, tmp & 0xff);
	COMMENT("Frame tag, buffer 0, The first byte");

	VP8PutByte(buffer, (tmp>>8) & 0xff);
	COMMENT("Frame tag, buffer 0, The second byte");

	VP8PutByte(buffer, (tmp>>16) & 0xff);
	COMMENT("Frame tag, buffer 0, The third byte");

	if (!cur_pic->i_frame) return;

	/* For key frames this is followed by a further 7 bytes of uncompressed
	 * data as follows */
	VP8PutByte(buffer, 0x9d);
	COMMENT("Frame tag, buffer 0, 0x9d");
	VP8PutByte(buffer, 0x01);
	COMMENT("Frame tag, buffer 0, 0x01");
	VP8PutByte(buffer, 0x2a);
	COMMENT("Frame tag, buffer 0, 0x2a");

	tmp = sps->picWidthInPixel | (sps->horizontalScaling << 14);
	VP8PutByte(buffer, tmp & 0xff);
	COMMENT("Frame tag, buffer 0, width lsb");
	VP8PutByte(buffer, tmp >> 8);
	COMMENT("Frame tag, buffer 0, width msb");

	tmp = sps->picHeightInPixel | (sps->verticalScaling << 14);
	VP8PutByte(buffer, tmp & 0xff);
	COMMENT("Frame tag, buffer 0, height lsb");
	VP8PutByte(buffer, tmp >> 8);
	COMMENT("Frame tag, buffer 0, height msb");
}

/*------------------------------------------------------------------------------
	DataPartitionSizes. In this implementation data partition sizes are
	written directly in the end of buffer[1].
------------------------------------------------------------------------------*/
void VP8DataPartitionSizes(vp8Instance_s *container)
{
	sps *sps = &container->sps;		/* Sequence container */
	vp8buffer *buffer = container->buffer;	/* Data buffers */
	i32 i, tmp;

	/* No data partitioning */
	if (!sps->dctPartitions) return;

	for (i = 2; i < sps->partitionCnt - 1; i++) {
		tmp = buffer[i].data - buffer[i].pData;
		VP8PutByte(&buffer[1], tmp & 0xff);
	        COMMENT("Partition size LSB");
		VP8PutByte(&buffer[1], (tmp>>8) & 0xff);
	        COMMENT("Partition size");
		VP8PutByte(&buffer[1], (tmp>>16) & 0xff);
	        COMMENT("Partition size MSB");
	}
}

/*------------------------------------------------------------------------------
	Segmentation
------------------------------------------------------------------------------*/
void Segmentation(vp8buffer *buffer, sps *sps, ppss *ppss, entropy *entropy,
		mbs *mbs)
{
	pps *pps = ppss->pps;
	sgm *sgm = &ppss->pps->sgm;		/* New segmentation data */
	i32 i, tmp;
	bool dataModified = false;

	/* Do we need to updata segmentation data */
	if (EWLmemcmp(ppss->qpSgm, pps->qpSgm, sizeof(ppss->qpSgm)))
		dataModified = true;

	if (EWLmemcmp(ppss->levelSgm, pps->levelSgm, sizeof(ppss->levelSgm)))
		dataModified = true;

	/* Update segmentation map only if there are no previous map or
	 * previous map differs or previous frame did not use segmentation at
	 * all. Note also that API set mapModified=true if user changes
	 * segmentation map */
	if (!ppss->prevPps) {
		sgm->mapModified = true;
	}

	VP8PutLit(buffer, sgm->mapModified, 1);
	COMMENT("Frame header, buffer 1, segmentation map modified");
	VP8PutLit(buffer, dataModified, 1);
	COMMENT("Frame header, buffer 1, segmentation data modified");

	if (dataModified) {
		/* ABS=1 vs. Deltas=0 */
		VP8PutLit(buffer, 1, 1);
		COMMENT("Frame header, buffer 1, segmentation data abs");

		for (i = 0; i < SGM_CNT; i++) {
			tmp = pps->qpSgm[i];
			VP8PutLit(buffer, 1, 1);
		        COMMENT("Frame header, buffer 1, segment QP enable");
			VP8PutLit(buffer, ABS(tmp), 7);
		        COMMENT("Frame header, buffer 1, segment QP");
			VP8PutLit(buffer, tmp<0, 1);
		        COMMENT("Frame header, buffer 1, segment QP sign");
		}

		for (i = 0; i < SGM_CNT; i++) {
			tmp = pps->levelSgm[i];
			VP8PutLit(buffer, 1, 1);
		        COMMENT("Frame header, buffer 1, segment level enable");
			VP8PutLit(buffer, ABS(tmp), 6);
		        COMMENT("Frame header, buffer 1, segment level");
			VP8PutLit(buffer, tmp<0, 1);
		        COMMENT("Frame header, buffer 1, segment level sign");
		}
	}

	/* Segmentation map probabilities */
	if (sgm->mapModified) {
		i32 sum1 = sgm->idCnt[0] + sgm->idCnt[1];
		i32 sum2 = sgm->idCnt[2] + sgm->idCnt[3];

		ASSERT(sum1);

		tmp = 255 * sum1 / (sum1 + sum2);
		entropy->segmentProb[0] = CLIP3(tmp, 1, 255);

		tmp = sum1 ? 255 * sgm->idCnt[0]/sum1 : 255;
		entropy->segmentProb[1] = CLIP3(tmp, 1, 255);

		tmp = sum2 ? 255 * sgm->idCnt[2]/sum2 : 255;
		entropy->segmentProb[2] = CLIP3(tmp, 1, 255);

		for (i = 0; i < 3; i++) {
			if (sgm->idCnt[i] != 0) {
				VP8PutLit(buffer, 1, 1);
				COMMENT("Frame header, buffer 1, segment prob enable");
				VP8PutLit(buffer, entropy->segmentProb[i], 8);
				COMMENT("Frame header, buffer 1, segment prob");
			} else {
				VP8PutLit(buffer, 0, 1);
				COMMENT("Frame header, buffer 1, no segment prob");
			}
		}
	}
}

/*------------------------------------------------------------------------------
    FilterLevelDelta
------------------------------------------------------------------------------*/
void FilterLevelDelta(vp8buffer *buffer, sps *sps)
{
    i32 i, tmp;
    i32 modeUpdate[4];
    i32 refUpdate[4];
    bool update = false;

    /* Find out what delta values are changed */
    for (i = 0; i < 4; i++) {
        modeUpdate[i] = sps->modeDelta[i] != sps->oldModeDelta[i];
        refUpdate[i] = sps->refDelta[i] != sps->oldRefDelta[i];
        if (modeUpdate[i] || refUpdate[i])
            update = true;
    }

    /* With error resilient mode update the level values for every frame. */
    if (!sps->refreshEntropy)
        update = true;

    /* Do the deltas need to be updated */
    VP8PutLit(buffer, update, 1);
    COMMENT("Frame header, buffer 1, filter delta update");
    if (!update) return;

    /* Reference frame mode based deltas */
    for (i = 0; i < 4; i++) {
        VP8PutLit(buffer, refUpdate[i], 1);
        COMMENT("Frame header, buffer 1, filter ref frame update");
        if (refUpdate[i]) {
            tmp = sps->refDelta[i];
            VP8PutLit(buffer, ABS(tmp), 6);    /* Delta */
            COMMENT("Frame header, buffer 1, filter delta");
            VP8PutLit(buffer, tmp < 0, 1); /* Sign */
            COMMENT("Frame header, buffer 1, filter delta sign");
        }
    }

    /* Macroblock mode based deltas */
    for (i = 0; i < 4; i++) {
        VP8PutLit(buffer, modeUpdate[i], 1);
        COMMENT("Frame header, buffer 1, filter mode deltas");
        if (modeUpdate[i]) {
            tmp = sps->modeDelta[i];
            VP8PutLit(buffer, ABS(tmp), 6);    /* Delta */
            COMMENT("Frame header, buffer 1, filter delta");
            VP8PutLit(buffer, tmp < 0, 1); /* Sign */
            COMMENT("Frame header, buffer 1, filter delta sign");
        }
    }
}
/*------------------------------------------------------------------------------
    QuantDelta
------------------------------------------------------------------------------*/
void QuantDelta(vp8buffer *buffer, sps *sps)
{

    VP8PutLit(buffer, sps->qpDelta[0] != 0, 1);
    COMMENT("Frame header, buffer 1, YdcDelta present");
    if (sps->qpDelta[0]) {
        VP8PutLit(buffer, ABS(sps->qpDelta[0]), 4);
        COMMENT("Frame header, buffer 1, YdcDelta magnitude");
        VP8PutLit(buffer, sps->qpDelta[0] < 0, 1);
        COMMENT("Frame header, buffer 1, YdcDelta sign");
    }
    VP8PutLit(buffer, sps->qpDelta[1] != 0, 1);
    COMMENT("Frame header, buffer 1, Y2dcDelta present");
    if (sps->qpDelta[1]) {
        VP8PutLit(buffer, ABS(sps->qpDelta[1]), 4);
        COMMENT("Frame header, buffer 1, Y2dcDelta magnitude");
        VP8PutLit(buffer, sps->qpDelta[1] < 0, 1);
        COMMENT("Frame header, buffer 1, Y2dcDelta sign");
    }
    VP8PutLit(buffer, sps->qpDelta[2] != 0, 1);
    COMMENT("Frame header, buffer 1, Y2acDelta present");
    if (sps->qpDelta[2]) {
        VP8PutLit(buffer, ABS(sps->qpDelta[2]), 4);
        COMMENT("Frame header, buffer 1, Y2acDelta magnitude");
        VP8PutLit(buffer, sps->qpDelta[2] < 0, 1);
        COMMENT("Frame header, buffer 1, Y2acDelta sign");
    }
    VP8PutLit(buffer, sps->qpDelta[3] != 0, 1);
    COMMENT("Frame header, buffer 1, UVdcDelta present");
    if (sps->qpDelta[3]) {
        VP8PutLit(buffer, ABS(sps->qpDelta[3]), 4);
        COMMENT("Frame header, buffer 1, UVdcDelta magnitude");
        VP8PutLit(buffer, sps->qpDelta[3] < 0, 1);
        COMMENT("Frame header, buffer 1, UVdcDelta sign");
    }
    VP8PutLit(buffer, sps->qpDelta[4] != 0, 1);
    COMMENT("Frame header, buffer 1, UVacDelta present");
    if (sps->qpDelta[4]) {
        VP8PutLit(buffer, ABS(sps->qpDelta[4]), 4);
        COMMENT("Frame header, buffer 1, UVacDelta magnitude");
        VP8PutLit(buffer, sps->qpDelta[4] < 0, 1);
        COMMENT("Frame header, buffer 1, UVacDelta sign");
    }
}
