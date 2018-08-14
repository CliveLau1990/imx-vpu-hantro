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
--  Abstract : H264 Encoder Extended API (just for testing)
--
------------------------------------------------------------------------------*/
#include "h264encapi.h"
#include "h264encapi_ext.h"
#include "H264Instance.h"
#include "H264TestId.h"

H264EncRet H264EncSetFilter(H264EncInst inst, const H264EncFilter * pEncCfg)
{

    h264Instance_s *pEncInst = (h264Instance_s *) inst;

    if(pEncInst->picParameterSet.deblockingFilterControlPresent == ENCHW_NO)
        return H264ENC_INVALID_STATUS;

    pEncInst->slice.disableDeblocking = pEncCfg->disableDeblocking;

#if 0
    if(pEncCfg->disableDeblocking != 1)
    {
        pEncInst->slice.filterOffsetA = pEncCfg->filterOffsetA;
        pEncInst->slice.filterOffsetB = pEncCfg->filterOffsetB;
    }
    else
    {
        pEncInst->slice.filterOffsetA = 0;
        pEncInst->slice.filterOffsetB = 0;
    }
#else
    pEncInst->slice.filterOffsetA = pEncCfg->filterOffsetA;
    pEncInst->slice.filterOffsetB = pEncCfg->filterOffsetB;
#endif

    return H264ENC_OK;
}

H264EncRet H264EncGetFilter(H264EncInst inst, H264EncFilter * pEncCfg)
{
    h264Instance_s *pEncInst = (h264Instance_s *) inst;

    pEncCfg->disableDeblocking = pEncInst->slice.disableDeblocking;
    pEncCfg->filterOffsetA = pEncInst->slice.filterOffsetA;
    pEncCfg->filterOffsetB = pEncInst->slice.filterOffsetB;

    return H264ENC_OK;
}

H264EncRet H264EncSetChromaQpIndexOffset(H264EncInst inst, i32 offset)
{
    h264Instance_s *pEncInst = (h264Instance_s *) inst;

    ASSERT(inst != NULL);

    if(offset < -12 || offset > 12)
    {
        return H264ENC_INVALID_ARGUMENT;
    }

    /* Check status, only INIT is allowed */
    if(pEncInst->encStatus != H264ENCSTAT_INIT)
    {
        return H264ENC_INVALID_STATUS;
    }

    pEncInst->picParameterSet.chromaQpIndexOffset = offset;

    return H264ENC_OK;
}

H264EncRet H264EncSetHwBurstSize(H264EncInst inst, u32 burst)
{
    h264Instance_s *pEncInst = (h264Instance_s *) inst;

    ASSERT(inst != NULL);
    ASSERT(burst < 64);

    pEncInst->asic.regs.asicCfgReg &=  ~(63 << 8);
    pEncInst->asic.regs.asicCfgReg |=  ((burst & (63)) << 8);

    return H264ENC_OK;
}

H264EncRet H264EncSetHwBurstType(H264EncInst inst, u32 bursttype)
{
    h264Instance_s *pEncInst = (h264Instance_s *) inst;

    ASSERT(inst != NULL);

    pEncInst->asic.regs.asicCfgReg &=  ~(1 << 6);
    pEncInst->asic.regs.asicCfgReg |=  ((bursttype & (1)) << 6);

    return H264ENC_OK;
}

H264EncRet H264EncTestInputLineBuf(H264EncInst inst)
{
    h264Instance_s *pEncInst = (h264Instance_s *) inst;

    ASSERT(inst != NULL);

    H264InputLineBufDepthTest(pEncInst);

    return H264ENC_OK;
}

H264EncRet H264EncTestCropping(H264EncInst inst)
{
    h264Instance_s *pEncInst = (h264Instance_s *) inst;

    ASSERT(inst != NULL);

    H264CroppingTest(pEncInst);

    return H264ENC_OK;
}

