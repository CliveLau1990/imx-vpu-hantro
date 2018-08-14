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
#include "vp8entropy.h"
#include "vp8entropytable.h"
#include "vp8macroblocktools.h"
#include "enccommon.h"
#include "ewl.h"

/*------------------------------------------------------------------------------
	External compiler flags
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
	Module defines
------------------------------------------------------------------------------*/

/* Approximate bit cost of bin at given probability prob */
#define COST_BOOL(prob, bin)\
	((bin) ? vp8_prob_cost[255 - (prob)] : vp8_prob_cost[prob])

enum {
    MAX_BAND = 7,
    MAX_CTX  = 3,
};

/*------------------------------------------------------------------------------
	Local function prototypes
------------------------------------------------------------------------------*/
static void InitDeadzone(vp8Instance_s *inst);
static i32 CostTree(tree const *tree, i32 *prob);
static void UpdateEntropy(vp8Instance_s *inst);

/*------------------------------------------------------------------------------
	EncSwapEndianess
------------------------------------------------------------------------------*/
void EncSwapEndianess(u32 * buf, u32 sizeBytes)
{
#if (ENCH1_OUTPUT_SWAP_8 == 1)
    u32 i = 0;
    i32 words = sizeBytes / 4;

    ASSERT((sizeBytes % 8) == 0);

    while(words > 0)
    {
        u32 val = buf[i];
        u32 tmp = 0;

        tmp |= (val & 0xFF) << 24;
        tmp |= (val & 0xFF00) << 8;
        tmp |= (val & 0xFF0000) >> 8;
        tmp |= (val & 0xFF000000) >> 24;

#if(ENCH1_OUTPUT_SWAP_32 == 1)    /* need this for 64-bit HW */
        {
            u32 val2 = buf[i + 1];
            u32 tmp2 = 0;

            tmp2 |= (val2 & 0xFF) << 24;
            tmp2 |= (val2 & 0xFF00) << 8;
            tmp2 |= (val2 & 0xFF0000) >> 8;
            tmp2 |= (val2 & 0xFF000000) >> 24;

            buf[i] = tmp2;
            words--;
            i++;
        }
#endif
        buf[i] = tmp;
        words--;
        i++;
    }
#endif

}

/*------------------------------------------------------------------------------
	InitEntropy
------------------------------------------------------------------------------*/
void InitEntropy(vp8Instance_s *inst)
{
        entropy *entropy = inst->entropy;

        ASSERT(sizeof(defaultCoeffProb) == sizeof(entropy->coeffProb));
        ASSERT(sizeof(defaultCoeffProb) == sizeof(coeffUpdateProb));
        ASSERT(sizeof(defaultMvProb) == sizeof(mvUpdateProb));
        ASSERT(sizeof(defaultMvProb) == sizeof(entropy->mvProb));

        UpdateEntropy(inst);

        /* Default propability */
        /*entropy->skipFalseProb = defaultSkipFalseProb[inst->rateControl.qpHdr];*/
        /*entropy->intraProb = 63;*/    /* Stetson-Harrison method TODO */

        /* Probability for ipf/grf/arf usage. Only two of them are used at a
         * time so the probabilities are based on the ref frames in use. */
        if (inst->picBuffer.refPicList[0].search) {
            entropy->lastProb = 255;    /* Ipf is mostly the best match. */
            if (inst->picBuffer.refPicList[2].search)
                entropy->gfProb = 0;    /* Always arf. */
            else
                entropy->gfProb = 255;  /* Always grf. */
        } else {
            entropy->lastProb = 0;      /* Ipf not used. */
            if (inst->picBuffer.refPicList[2].search)
                entropy->gfProb = 128;  /* Assume grf/arf equally used. */
            else
                entropy->gfProb = 255;  /* Only grf used. */
        }

        EWLmemcpy(entropy->YmodeProb, YmodeProb, sizeof(YmodeProb));
        EWLmemcpy(entropy->UVmodeProb, UVmodeProb, sizeof(UVmodeProb));

        /* When temporal layers enabled, only base layer used for updates. */
        if ((inst->rateControl.layerAmount > 1) && (inst->layerId == 0))
            EWLmemcpy(inst->probCountStore, inst->asic.probCount.virtualAddress,
	              ASIC_VP8_PROB_COUNT_SIZE);

}

/*------------------------------------------------------------------------------
	WriteEntropyTables
------------------------------------------------------------------------------*/
void WriteEntropyTables(vp8Instance_s *inst, u32 fullRefresh)
{
        entropy *entropy = inst->entropy;
        u8 *table = (u8 *)inst->asic.cabacCtx.virtualAddress;
        i32 i, j, k, l;

        ASSERT(table);

        /* Write probability tables to ASIC linear memory, reg + mem */
        EWLmemset(table, 0, 56);
        table[0] = entropy->skipFalseProb;
        table[1] = entropy->intraProb;
        table[2] = entropy->lastProb;
        table[3] = entropy->gfProb;
        table[4] = entropy->segmentProb[0];
        table[5] = entropy->segmentProb[1];
        table[6] = entropy->segmentProb[2];

        table[8]  = entropy->YmodeProb[0];
        table[9]  = entropy->YmodeProb[1];
        table[10] = entropy->YmodeProb[2];
        table[11] = entropy->YmodeProb[3];
        table[12] = entropy->UVmodeProb[0];
        table[13] = entropy->UVmodeProb[1];
        table[14] = entropy->UVmodeProb[2];

        /* MV probabilities x+y: short, sign, size 8-9 */
        table[16] = entropy->mvProb[1][0];
        table[17] = entropy->mvProb[0][0];
        table[18] = entropy->mvProb[1][1];
        table[19] = entropy->mvProb[0][1];
        table[20] = entropy->mvProb[1][17];
        table[21] = entropy->mvProb[1][18];
        table[22] = entropy->mvProb[0][17];
        table[23] = entropy->mvProb[0][18];

        /* MV X size */
        for (i = 0; i < 8; i++)
            table[24+i] = entropy->mvProb[1][9+i];

        /* MV Y size */
        for (i = 0; i < 8; i++)
            table[32+i] = entropy->mvProb[0][9+i];

        /* MV X short tree */
        for (i = 0; i < 7; i++)
            table[40+i] = entropy->mvProb[1][2+i];

        /* MV Y short tree */
        for (i = 0; i < 7; i++)
            table[48+i] = entropy->mvProb[0][2+i];

        /* Update the ASIC table when needed. */
        if (entropy->updateCoeffProbFlag || (fullRefresh == true)) {
            table += 56;
            /* DCT coeff probabilities 0-2, two fields per line. */
            for (i = 0; i < 4; i++)
                for (j = 0; j < 8; j++)
                    for (k = 0; k < 3; k++) {
                        for (l = 0; l < 3; l++) {
                            *table++ = entropy->coeffProb[i][j][k][l];
                        }
                        *table++ = 0;
                    }

            /* ASIC second probability table in ext mem.
             * DCT coeff probabilities 4 5 6 7 8 9 10 3 on each line.
             * coeff 3 was moved from first table to second so it is last. */
            for (i = 0; i < 4; i++)
                for (j = 0; j < 8; j++)
                    for (k = 0; k < 3; k++) {
                        for (l = 4; l < 11; l++) {
                            *table++ = entropy->coeffProb[i][j][k][l];
                        }
                        *table++ = entropy->coeffProb[i][j][k][3];
                    }
        }

        table = (u8 *)inst->asic.cabacCtx.virtualAddress;
        if (entropy->updateCoeffProbFlag || (fullRefresh == true))
            EncSwapEndianess((u32*)table, 56 + 8*48 + 8*96);
        else
            EncSwapEndianess((u32*)table, 56);

        InitDeadzone(inst);
}


/*------------------------------------------------------------------------------
    InitDeadzone
------------------------------------------------------------------------------*/
void InitDeadzone(vp8Instance_s *inst)
{
        entropy *entropy = inst->entropy;
        i32 plane, band, ctx, token, rate, dq, s;
        i32 *prob;
        const tree *tree;
        u8 *table;

        /* Update the ASIC table when needed. */
        if (inst->asic.regs.deadzoneEnable) {
            /* Initialize dead zone coeff rate values. */
            table = inst->asic.regs.dzCoeffRate;
            for (plane = 0; plane < 4; plane++)
                for (ctx = 0; ctx < 3; ctx++)
                    for (token = 0; token < 2; token++)
                        for (band = 0; band < 8; band++) {
                            prob = entropy->coeffProb[plane][band][ctx];
                            tree = ctx ? dctTree : dctTreeNoEOB;
                            rate = (CostTree(&tree[token], prob) + 8) >> 4;
                            if (!rate) rate++;
                            if (token) rate += 16; /* Sign bit. */
                            *table++ = MIN(255, rate);
                        }

            /* Initialize dead zone eob rate values. */
            table = inst->asic.regs.dzEobRate;
            for (plane = 0; plane < 4; plane++)
                for (ctx = 0; ctx < 2; ctx++)
                    for (band = 0; band < 8; band++) {
                        prob = entropy->coeffProb[plane][band][ctx+1];
                        rate = (CostTree(&dctTree[11], prob) + 8) >> 4;
                        if (!rate) rate++;
                        *table++ = MIN(255, rate);
                    }
        }

        /* Set penalty values for every segment based on segment QP. */
        for (s = 0; s < SGM_CNT; s++) {
            u32 tmp;
            i32 qp = inst->ppss.pps->qpSgm[s];
            const i32 dzLambda[2][4] = { { 112, 80, 130, 124 },   /* PSNR set */
                                         { 134, 60,  78, 148 } }; /* SSIM set */
            const i32 skipLambda[2] = { 52, 90 };                                         
            i32 setIdx = inst->qualityMetric; /* NOTE enumeration must match */

            /* Rate multiplier for each plane. */
            dq = inst->qpY1[qp].dequant[0];
            inst->asic.regs.pen[s][ASIC_PENALTY_DZ_RATE0] = 
                (dzLambda[setIdx][0]*dq*dq + 500)/1000;
            dq = inst->qpY2[qp].dequant[0];
            inst->asic.regs.pen[s][ASIC_PENALTY_DZ_RATE1] = 
                (dzLambda[setIdx][1]*dq*dq + 500)/1000;
            dq = inst->qpCh[qp].dequant[0];
            inst->asic.regs.pen[s][ASIC_PENALTY_DZ_RATE2] = 
                (dzLambda[setIdx][2]*dq*dq + 500)/1000;
            dq = inst->qpY1[qp].dequant[0];
            inst->asic.regs.pen[s][ASIC_PENALTY_DZ_RATE3] = 
                (dzLambda[setIdx][3]*dq*dq + 500)/1000;

            /* Skip rate is 0.052*QP^2. HW rate accumulations omit 4 bits, and
             * final RD decision rounds 8 bits off, which is how we end up
             * dividing the result with 4096000 ( == 1000*16*256 ) */
            tmp = COST_BOOL(entropy->skipFalseProb, 0);
            tmp *= skipLambda[setIdx]*qp*qp;
            tmp += 2048000;
            tmp /= 4096000;
            inst->asic.regs.pen[s][ASIC_PENALTY_DZ_SKIP0] = tmp;
            tmp = COST_BOOL(entropy->skipFalseProb, 1);
            tmp *= skipLambda[setIdx]*qp*qp;
            tmp += 2048000;
            tmp /= 4096000;
            inst->asic.regs.pen[s][ASIC_PENALTY_DZ_SKIP1] = tmp;
        }
}

/*------------------------------------------------------------------------------
	InitTreePenaltyTables
------------------------------------------------------------------------------*/
void InitTreePenaltyTables(vp8Instance_s *container)
{
	mbs *mbs = &container->mbs;		/* Macroblock related stuff */
	i32 tmp, i;

	/* Calculate bit cost for each 16x16 mode, uses p-frame probs */
	for (i = DC_PRED; i <= B_PRED; i++) {
		tmp = CostTree(YmodeTree[i], (i32 *)YmodeProb);
		mbs->intra16ModeBitCost[i] = tmp;
	}

	/* Calculate bit cost for each 4x4 mode, uses p-frame probs */
	for (i = B_DC_PRED; i <= B_HU_PRED; i++) {
		tmp = CostTree(BmodeTree[i], (i32 *)BmodeProb);
		mbs->intra4ModeBitCost[i] = tmp;
	}
}

/*------------------------------------------------------------------------------
	CostTree returns average bit usage of tree, see PutTree().
------------------------------------------------------------------------------*/
i32 CostTree(tree const *tree, i32 *prob)
{
	i32 value = tree->value;
	i32 number = tree->number;
	i32 const *index = tree->index;
	i32 bitCost = 0;

	while (number--) {
		bitCost += COST_BOOL(prob[*index++], (value >> number) & 1);
	}

	return bitCost;
}

/*------------------------------------------------------------------------------
	CostMv returns average bit usage of mvd, see PutMv().
------------------------------------------------------------------------------*/
i32 CostMv(i32 mvd, i32 *mvProb)
{
	i32 i, tmp, bitCost = 0;

	/* Luma motion vectors are doubled, see 18.1 in "VP8 Data Format and
	 * Decoding Guide". */
	ASSERT(!(mvd&1));
	tmp = ABS(mvd>>1);

	/* Short Tree */
	if (tmp < 8) {
		bitCost += COST_BOOL(mvProb[0], 0);
		bitCost += CostTree(&mvTree[tmp], mvProb + 2);
		if (!tmp) return bitCost;

		/* Sign */
		bitCost += COST_BOOL(mvProb[1], mvd < 0);
		return bitCost;
	}

	/* Long Tree */
	bitCost += COST_BOOL(mvProb[0], 1);

	/* Bits 0, 1, 2 */
	for (i = 0; i < 3; i++) {
		bitCost += COST_BOOL(mvProb[9 + i], (tmp>>i) & 1);
	}

	/* Bits 9, 8, 7, 6, 5, 4 */
	for (i = 9; i > 3; i--) {
		bitCost += COST_BOOL(mvProb[9 + i], (tmp>>i) & 1);
	}

	/* Bit 3: if ABS(mvd) < 8, it is coded with short tree, so if here
	 * ABS(mvd) <= 15, bit 3 must be one (because here we code values
	 * 8,...,15) and is not explicitly coded. */
	if (tmp > 15) {
		bitCost += COST_BOOL(mvProb[9 + 3], (tmp>>3) & 1);
	}

	/* Sign */
	bitCost += COST_BOOL(mvProb[1], mvd < 0);

	return bitCost;
}

/*------------------------------------------------------------------------------
	CoeffProb
------------------------------------------------------------------------------*/
void CoeffProb(vp8buffer *buffer, i32 curr[4][8][3][11], i32 prev[4][8][3][11])
{
	i32 i, j, k, l;
	i32 prob, new, old;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 8; j++) {
			for (k = 0; k < 3; k++) {
				for (l = 0; l < 11; l++) {
					prob = coeffUpdateProb[i][j][k][l];
					old = prev[i][j][k][l];
					new = curr[i][j][k][l];

					if (new == old) {
						VP8PutBool(buffer, prob, 0);
						COMMENT("Coeff prob update");
					} else {
						VP8PutBool(buffer, prob, 1);
						COMMENT("Coeff prob update");
						VP8PutLit(buffer, new, 8);
						COMMENT("New prob");
					}
				}
			}
		}
	}
}

/*------------------------------------------------------------------------------
	MvProb
------------------------------------------------------------------------------*/
void MvProb(vp8buffer *buffer, i32 curr[2][19], i32 prev[2][19])
{
	i32 i, j;
	i32 prob, new, old;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 19; j++) {
			prob = mvUpdateProb[i][j];
			old = prev[i][j];
			new = curr[i][j];

			if (new == old) {
				VP8PutBool(buffer, prob, 0);
				COMMENT("MV prob update");
			} else {
				VP8PutBool(buffer, prob, 1);
				COMMENT("MV prob update");
				VP8PutLit(buffer, new>>1, 7);
				COMMENT("New prob");
			}
		}
	}
}

/*------------------------------------------------------------------------------
	updated
	determine if given probability is to be updated (savings larger than
	cost of update)
------------------------------------------------------------------------------*/
u32 update(u32 updP, u32 left, u32 right, u32 oldP, u32 newP, u32 fixed)
{
	i32 u, s;

	/* how much it costs to update a coeff */
	u = (i32)fixed + ((vp8_prob_cost[255-updP] - vp8_prob_cost[updP]) >> 8);
	/* bit savings if updated */
	s = ((i32)left * /* zero branch count */
		/* diff cost for '0' bin */
		(vp8_prob_cost[oldP] - vp8_prob_cost[newP]) +
		(i32)right * /* one branch count */
		/* diff cost for '1' bin */
		(vp8_prob_cost[255-oldP] - vp8_prob_cost[255-newP]))>>8;

	return (s > u);
}

/*------------------------------------------------------------------------------
	mvprob
	compute new mv probability
------------------------------------------------------------------------------*/
u32 mvprob(u32 left, u32 right, u32 oldP)
{
	u32 p;

	if (left+right)
	{
		p = (left * 255)/(left+right);
		p &= -2;
		if (!p) p = 1;
	}
	else
		p = oldP;

	return p;
}

/*------------------------------------------------------------------------------
	UpdateEntropy
------------------------------------------------------------------------------*/
void UpdateEntropy(vp8Instance_s *inst)
{
	entropy *entropy = inst->entropy;
	i32 i, j, k, l, tmp, ii;
	u16 *pCnt = (u16*)inst->asic.probCount.virtualAddress;
	u16 *pTmp;
	u32 p, left, right, oldP, updP;
	u32 type;
	u32 branchCnt[2];
	const i32 offset[] = {
         -1, -1, -1,  0,  1,  2, -1,  3,  4, -1,  5,  6, -1,  7,  8, -1,
          9, 10, -1, 11, 12, 13, 14, 15, -1, 16, 17, -1, 18, 19, -1, 20,
         21, -1, 22, 23, -1, 24, 25, -1, 26, 27, 28, 29, 30, -1, 31, 32,
         -1, 33, 34, -1, 35, 36, -1, 37, 38, -1, 39, 40, -1, 41, 42, 43,
         44, 45, -1, 46, 47, -1, 48, 49, -1, 50, 51, -1, 52, 53, -1, 54,
         55, -1, 56, 57, -1, -1, -1, 58, 59, 60, 61, 62, 63, 64, 65, 66,
         67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82,
         83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98,
         99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,
        115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,
        131,132,133,134,135,136,137,138, -1, -1, -1,139,140,141,142,143,
        144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
        160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
        176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
        192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
        208,209,210,211,212,213,214,215,216,217,218,219};

	/* Update the HW prob table only when needed. */
	entropy->updateCoeffProbFlag = 0;

	/* Use default propabilities as reference when needed. */
	if (!inst->sps.refreshEntropy || inst->picBuffer.cur_pic->i_frame)
	{
		/* Only do the copying when something has changed. */
		if (!entropy->defaultCoeffProbFlag) {
			EWLmemcpy(entropy->coeffProb, defaultCoeffProb,
					sizeof(defaultCoeffProb));
			entropy->updateCoeffProbFlag = 1;
		}
		EWLmemcpy(entropy->mvProb, defaultMvProb, sizeof(defaultMvProb));
		entropy->defaultCoeffProbFlag = 1;
	}

	/* store current probs */
	EWLmemcpy(entropy->oldCoeffProb, entropy->coeffProb, sizeof(entropy->coeffProb));
	if (inst->frameCnt == 0 || !inst->picBuffer.cur_pic->i_frame)
		EWLmemcpy(entropy->oldMvProb, entropy->mvProb, sizeof(entropy->mvProb));

	/* init probs */
	entropy->skipFalseProb = defaultSkipFalseProb[inst->rateControl.qpHdr];

	/* Do not update on first frame, token/branch counters not valid yet. */
	if ((inst->frameCnt == 0) && (inst->passNbr == 0)) return;

        /* Do not update probabilities for droppable frames. */
        if (!inst->picBuffer.cur_pic->ipf && !inst->picBuffer.cur_pic->grf &&
            !inst->picBuffer.cur_pic->arf) return;

        /* If previous frame was lost the prob counters are not valid. */
        if (inst->prevFrameLost) return;

        /* Allow probability update only on temporal layer 0 (base layer). */
        if (inst->layerId > 0) return;

        /* When temporal layers in use update from previous base layer frame. */
        if (inst->rateControl.layerAmount > 1)
            pCnt = inst->probCountStore;

        /* Limit entropy prob updates to lower software load. */
        if ((ENCH1_VP8_ENTROPY_UPDATE_DISTANCE > 1) &&
            (inst->frameCnt % ENCH1_VP8_ENTROPY_UPDATE_DISTANCE) != 0) return;

#ifdef TRACE_PROBS
	/* Trace HW output prob counters into file */
	EncTraceProbs(pCnt, ASIC_VP8_PROB_COUNT_SIZE);
#endif

	/* All four block types */
	for (i = 0; i < 4; i++)
	{
		/* All but last (==7) bands */
		for (j = 0; j < MAX_BAND; j++)
			/* All three neighbour contexts */
			for (k = 0; k < MAX_CTX; k++)
			{
				/* last token of current (type,band,ctx) */
				tmp = i * MAX_BAND*MAX_CTX + j * MAX_CTX + k;
				tmp += 2 * 4*MAX_BAND*MAX_CTX;
				ii = offset[tmp];

				right = ii >= 0 ? pCnt[ii] : 0;

				/* first two branch probabilities */
				for (l = 2; l--;)
				{
					oldP = entropy->coeffProb[i][j][k][l];
					updP = coeffUpdateProb[i][j][k][l];

					tmp -= 4*MAX_BAND*MAX_CTX;
					ii = offset[tmp];
					left = ii >= 0 ? pCnt[ii] : 0;
					/* probability of 0 for current branch */
					if (left+right)
					{
						p = ((left * 256) + ((left+right)>>1))/(left+right);
						if (p > 255) p = 255;
					}
					else
						p = oldP;

					if (update(updP, left, right, oldP, p, 8))
					{
						entropy->coeffProb[i][j][k][l] = p;
						entropy->updateCoeffProbFlag = 1;
					}
					right += left;
				}
			}
	}

	/* If updating coeffProbs the defaults are no longer in use. */
	if (entropy->updateCoeffProbFlag)
		entropy->defaultCoeffProbFlag = 0;

	/* skip prob */
	pTmp = pCnt + ASIC_VP8_PROB_COUNT_MODE_OFFSET;
	p = pTmp[0] * 256 / inst->mbPerFrame;
	entropy->skipFalseProb = CLIP3(256-(i32)p, 0, 255);
	
	/* bail out here if doing n:th pass of I frame */
	if( inst->passNbr && inst->picBuffer.cur_pic->i_frame )
	    return;

	/* intra prob,, do not update if previous was I frame */
	if ((!inst->picBuffer.last_pic->i_frame) ||
	    inst->passNbr )
	{
		p = pTmp[1] * 255 / inst->mbPerFrame;
		entropy->intraProb = CLIP3((i32)p, 0, 255);
	}
	else
		entropy->intraProb = 1; /* TODO default value */

	/* MV probs shouldn't be updated if previous or current frame is intra */
	if (inst->picBuffer.last_pic->i_frame || inst->picBuffer.cur_pic->i_frame)
		return;

	/* mv probs */
	pTmp = pCnt + ASIC_VP8_PROB_COUNT_MV_OFFSET;
	for (i = 0; i < 2; i++)
	{
		/* is short prob */
		left = *pTmp++; /* short */
		right = *pTmp++; /* long */

		p = mvprob(left, right, entropy->oldMvProb[i][0]);
		if (update(mvUpdateProb[i][0], left, right,
				   entropy->oldMvProb[i][0], p, 6))
			entropy->mvProb[i][0] = p;

		/* sign prob */
		right += left; /* total mvs */
		left = *pTmp++; /* num positive */
		/* amount of negative vectors = total - positive - zero vectors */
		right -= left - pTmp[0];

		p = mvprob(left, right, entropy->oldMvProb[i][1]);
		if (update(mvUpdateProb[i][1], left, right,
				   entropy->oldMvProb[i][1], p, 6))
			entropy->mvProb[i][1] = p;

		/* short mv probs, branches 2 and 3 (0/1 and 2/3) */
		for (j = 0; j < 2; j++)
		{
			left = *pTmp++;
			right = *pTmp++;
			p = mvprob(left, right, entropy->oldMvProb[i][4+j]);
			if (update(mvUpdateProb[i][4+j], left, right,
					   entropy->oldMvProb[i][4+j], p, 6))
				entropy->mvProb[i][4+j] = p;
			branchCnt[j] = left + right;
		}
		/* short mv probs, branch 1 */
		p = mvprob(branchCnt[0], branchCnt[1], entropy->oldMvProb[i][3]);
		if (update(mvUpdateProb[i][3], branchCnt[0], branchCnt[1],
				   entropy->oldMvProb[i][3], p, 6))
			entropy->mvProb[i][3] = p;

		/* store total count for branch 0 computation */
		type = branchCnt[0] + branchCnt[1];

		/* short mv probs, branches 5 and 6 (4/5 and 6/7) */
		for (j = 0; j < 2; j++)
		{
			left = *pTmp++;
			right = *pTmp++;
			p = mvprob(left, right, entropy->oldMvProb[i][7+j]);
			if (update(mvUpdateProb[i][7+j], left, right,
					   entropy->oldMvProb[i][7+j], p, 6))
				entropy->mvProb[i][7+j] = p;
			branchCnt[j] = left + right;
		}
		/* short mv probs, branch 4 */
		p = mvprob(branchCnt[0], branchCnt[1], entropy->oldMvProb[i][6]);
		if (update(mvUpdateProb[i][6], branchCnt[0], branchCnt[1],
				   entropy->oldMvProb[i][6], p, 6))
			entropy->mvProb[i][6] = p;

		/* short mv probs, branch 0 */
		p = mvprob(type, branchCnt[0] + branchCnt[1],
			entropy->oldMvProb[i][2]);
		if (update(mvUpdateProb[i][2], type, branchCnt[0] + branchCnt[1],
				   entropy->oldMvProb[i][2], p, 6))
			entropy->mvProb[i][2] = p;
	}
}

/*------------------------------------------------------------------------------
	SetModeCosts
------------------------------------------------------------------------------*/
void SetModeCosts(vp8Instance_s *inst, i32 coeff, i32 segment)
{
    entropy *entropy = inst->entropy;
    regValues_s *regs = &inst->asic.regs;
    i32 costIntra, costInter;
    i32 tmp;

    costIntra = COST_BOOL(entropy->intraProb, 0);
    costInter = COST_BOOL(entropy->intraProb, 1) +
                COST_BOOL(entropy->lastProb, 0);
    /* all penalties in regs->pen[] are u32, this needs to be handled as i32 */
    tmp = (i32)regs->pen[segment][ASIC_PENALTY_INTER_FAVOR] +
        (costIntra * coeff / 2 >> 8);
    tmp = CLIP3(tmp, -32768, 32767);
    regs->pen[segment][ASIC_PENALTY_INTER_FAVOR] = tmp;
    regs->pen[segment][ASIC_PENALTY_COST_INTER] = costInter * coeff / 2 >> 8;

}

