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
--  Abstract : VP8 Encoder API
--
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
       Version Information
------------------------------------------------------------------------------*/

#define VP8ENC_MAJOR_VERSION 1
#define VP8ENC_MINOR_VERSION 1

#define VP8ENC_BUILD_MAJOR 1
#define VP8ENC_BUILD_MINOR 51
#define VP8ENC_BUILD_REVISION 0
#define VP8ENC_SW_BUILD ((VP8ENC_BUILD_MAJOR * 1000000) + \
(VP8ENC_BUILD_MINOR * 1000) + VP8ENC_BUILD_REVISION)

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#include "vp8encapi.h"
#include "enccommon.h"
#include "vp8instance.h"
#include "vp8init.h"
#include "vp8codeframe.h"
#include "vp8ratecontrol.h"
#include "vp8putbits.h"
#include "encasiccontroller.h"

#ifdef INTERNAL_TEST
#include "vp8testid.h"
#endif

#ifdef VIDEOSTAB_ENABLED
#include "vidstabcommon.h"
#endif

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

#define EVALUATION_LIMIT 300    max number of frames to encode

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* Parameter limits */
#define VP8ENCSTRMENCODE_MIN_BUF       512
#define VP8ENC_MAX_PP_INPUT_WIDTH      8176
#define VP8ENC_MAX_PP_INPUT_HEIGHT     8176
#define VP8ENC_MAX_BITRATE             (60000000)

#define VP8_BUS_ADDRESS_VALID(bus_address)  (((bus_address) != 0) && \
                                              ((bus_address & 0x07) == 0))

/* HW ID check. VP8EncInit() will fail if HW doesn't match. */
#define HW_ID_MASK  0xFFFF0000
#define HW_ID       0x48310000

/* Tracing macro */
#ifdef VP8ENC_TRACE
#define APITRACE(str) VP8EncTrace(str)
#define APITRACEPARAM(str, val) \
  { char tmpstr[255]; sprintf(tmpstr, "  %s: %d", str, (int)val); VP8EncTrace(tmpstr); }
#else
#define APITRACE(str)
#define APITRACEPARAM(str, val)
#endif

#ifdef TRACE_STREAM
/* External variable for passing the test data trace file config */
extern char * H1EncTraceFileConfig;
extern int H1EncTraceFirstFrame;
#endif

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static i32 VSCheckSize(u32 inputWidth, u32 inputHeight, u32 stabilizedWidth,
                       u32 stabilizedHeight);

/*------------------------------------------------------------------------------

    Function name : VP8EncGetApiVersion
    Description   : Return the API version info

    Return type   : VP8EncApiVersion
    Argument      : void
------------------------------------------------------------------------------*/
VP8EncApiVersion VP8EncGetApiVersion(void)
{
    VP8EncApiVersion ver;

    ver.major = VP8ENC_MAJOR_VERSION;
    ver.minor = VP8ENC_MINOR_VERSION;

    APITRACE("VP8EncGetApiVersion# OK");
    return ver;
}

/*------------------------------------------------------------------------------
    Function name : VP8EncGetBuild
    Description   : Return the SW and HW build information

    Return type   : VP8EncBuild
    Argument      : void
------------------------------------------------------------------------------*/
VP8EncBuild VP8EncGetBuild(void)
{
    VP8EncBuild ver;

    ver.swBuild = VP8ENC_SW_BUILD;
    ver.hwBuild = EWLReadAsicID();

    APITRACE("VP8EncGetBuild# OK");

    return (ver);
}

/*------------------------------------------------------------------------------
    Function name : VP8EncInit
    Description   : Initialize an encoder instance and returns it to application

    Return type   : VP8EncRet
    Argument      : pEncCfg - initialization parameters
                    instAddr - where to save the created instance
------------------------------------------------------------------------------*/
VP8EncRet VP8EncInit(const VP8EncConfig * pEncCfg, VP8EncInst * instAddr)
{
    VP8EncRet ret;
    vp8Instance_s *pEncInst = NULL;

    APITRACE("VP8EncInit#");

    /* check that right shift on negative numbers is performed signed */
    /*lint -save -e* following check causes multiple lint messages */
#if (((-1) >> 1) != (-1))
#error Right bit-shifting (>>) does not preserve the sign
#endif
    /*lint -restore */

    /* Check for illegal inputs */
    if(pEncCfg == NULL || instAddr == NULL)
    {
        APITRACE("VP8EncInit: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    APITRACEPARAM("refFrameAmount", pEncCfg->refFrameAmount);
    APITRACEPARAM("width", pEncCfg->width);
    APITRACEPARAM("height", pEncCfg->height);
    APITRACEPARAM("frameRateNum", pEncCfg->frameRateNum);
    APITRACEPARAM("frameRateDenom", pEncCfg->frameRateDenom);
    APITRACEPARAM("scaledWidth", pEncCfg->scaledWidth);
    APITRACEPARAM("scaledHeight", pEncCfg->scaledHeight);

    /* Check for correct HW */
    if ((EWLReadAsicID() & HW_ID_MASK) != HW_ID)
    {
        APITRACE("VP8EncInit: ERROR Invalid HW ID");
        return VP8ENC_ERROR;
    }

    /* Check that configuration is valid */
    if(VP8CheckCfg(pEncCfg) == ENCHW_NOK)
    {
        APITRACE("VP8EncInit: ERROR Invalid configuration");
        return VP8ENC_INVALID_ARGUMENT;
    }

    /* Initialize encoder instance and allocate memories */
    ret = VP8Init(pEncCfg, &pEncInst);

    if(ret != VP8ENC_OK)
    {
        APITRACE("VP8EncInit: ERROR Initialization failed");
        return ret;
    }

    /* Status == INIT   Initialization succesful */
    pEncInst->encStatus = VP8ENCSTAT_INIT;

    pEncInst->inst = pEncInst;  /* used as checksum */

    *instAddr = (VP8EncInst) pEncInst;

    APITRACE("VP8EncInit: OK");
    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------

    Function name : VP8EncRelease
    Description   : Releases encoder instance and all associated resource

    Return type   : VP8EncRet
    Argument      : inst - the instance to be released
------------------------------------------------------------------------------*/
VP8EncRet VP8EncRelease(VP8EncInst inst)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;

    APITRACE("VP8EncRelease#");

    /* Check for illegal inputs */
    if(pEncInst == NULL)
    {
        APITRACE("VP8EncRelease: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncRelease: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

#ifdef TRACE_STREAM
    EncCloseStreamTrace();
#endif

    VP8Shutdown(pEncInst);

    APITRACE("VP8EncRelease: OK");
    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------

    Function name : VP8EncSetCodingCtrl
    Description   : Sets encoding parameters

    Return type   : VP8EncRet
    Argument      : inst - the instance in use
                    pCodeParams - user provided parameters
------------------------------------------------------------------------------*/
VP8EncRet VP8EncSetCodingCtrl(VP8EncInst inst,
                                const VP8EncCodingCtrl * pCodeParams)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;
    regValues_s *regs;
    sps *sps;
    u32 area1 = 0, area2 = 0;
    i32 i;

    APITRACE("VP8EncSetCodingCtrl#");

    /* Check for illegal inputs */
    if((pEncInst == NULL) || (pCodeParams == NULL))
    {
        APITRACE("VP8EncSetCodingCtrl: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    APITRACEPARAM("interpolationFilter", pCodeParams->interpolationFilter);
    APITRACEPARAM("filterType", pCodeParams->filterType);
    APITRACEPARAM("filterLevel", pCodeParams->filterLevel);
    APITRACEPARAM("filterSharpness", pCodeParams->filterSharpness);
    APITRACEPARAM("dctPartitions", pCodeParams->dctPartitions);
    APITRACEPARAM("errorResilient", pCodeParams->errorResilient);
    APITRACEPARAM("splitMv", pCodeParams->splitMv);
    APITRACEPARAM("quarterPixelMv", pCodeParams->quarterPixelMv);
    APITRACEPARAM("cirStart", pCodeParams->cirStart);
    APITRACEPARAM("cirInterval", pCodeParams->cirInterval);
    APITRACEPARAM("intraArea.enable", pCodeParams->intraArea.enable);
    APITRACEPARAM("intraArea.top", pCodeParams->intraArea.top);
    APITRACEPARAM("intraArea.bottom", pCodeParams->intraArea.bottom);
    APITRACEPARAM("intraArea.left", pCodeParams->intraArea.left);
    APITRACEPARAM("intraArea.right", pCodeParams->intraArea.right);
    APITRACEPARAM("roi1Area.enable", pCodeParams->roi1Area.enable);
    APITRACEPARAM("roi1Area.top", pCodeParams->roi1Area.top);
    APITRACEPARAM("roi1Area.bottom", pCodeParams->roi1Area.bottom);
    APITRACEPARAM("roi1Area.left", pCodeParams->roi1Area.left);
    APITRACEPARAM("roi1Area.right", pCodeParams->roi1Area.right);
    APITRACEPARAM("roi2Area.enable", pCodeParams->roi2Area.enable);
    APITRACEPARAM("roi2Area.top", pCodeParams->roi2Area.top);
    APITRACEPARAM("roi2Area.bottom", pCodeParams->roi2Area.bottom);
    APITRACEPARAM("roi2Area.left", pCodeParams->roi2Area.left);
    APITRACEPARAM("roi2Area.right", pCodeParams->roi2Area.right);
    APITRACEPARAM("roi1DeltaQp", pCodeParams->roi1DeltaQp);
    APITRACEPARAM("roi2DeltaQp", pCodeParams->roi2DeltaQp);
    APITRACEPARAM("deadzone", pCodeParams->deadzone);
    APITRACEPARAM("maxNumPasses", pCodeParams->maxNumPasses);
    APITRACEPARAM("qualityMetric", pCodeParams->qualityMetric);
    APITRACEPARAM("qpDelta[0]", pCodeParams->qpDelta[0]);
    APITRACEPARAM("qpDelta[1]", pCodeParams->qpDelta[1]);
    APITRACEPARAM("qpDelta[2]", pCodeParams->qpDelta[2]);
    APITRACEPARAM("qpDelta[3]", pCodeParams->qpDelta[3]);
    APITRACEPARAM("qpDelta[4]", pCodeParams->qpDelta[4]);
    APITRACEPARAM("adaptiveRoi", pCodeParams->adaptiveRoi);
    APITRACEPARAM("adaptiveRoiColor", pCodeParams->adaptiveRoiColor);

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncSetCodingCtrl: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

    /* check limits */
    if (pCodeParams->filterLevel > VP8ENC_FILTER_LEVEL_AUTO ||
        pCodeParams->filterType > 1 ||
        pCodeParams->filterSharpness > VP8ENC_FILTER_SHARPNESS_AUTO)
    {
        APITRACE("VP8EncSetCodingCtrl: ERROR Invalid filter value");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pCodeParams->dctPartitions > 2)
    {
        APITRACE("VP8EncSetCodingCtrl: ERROR Invalid partition value");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if (pCodeParams->splitMv > 2 ||
        pCodeParams->quarterPixelMv > 2)
    {
        APITRACE("VP8EncSetCodingCtrl: ERROR Invalid enable/disable value");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pCodeParams->cirStart > pEncInst->mbPerFrame ||
       pCodeParams->cirInterval > pEncInst->mbPerFrame)
    {
        APITRACE("VP8EncSetCodingCtrl: ERROR Invalid CIR value");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pCodeParams->intraArea.enable) {
        if(!(pCodeParams->intraArea.top <= pCodeParams->intraArea.bottom &&
           pCodeParams->intraArea.bottom < pEncInst->mbPerCol &&
           pCodeParams->intraArea.left <= pCodeParams->intraArea.right &&
           pCodeParams->intraArea.right < pEncInst->mbPerRow))
        {
            APITRACE("VP8EncSetCodingCtrl: ERROR Invalid intraArea");
            return VP8ENC_INVALID_ARGUMENT;
        }
    }

    if(pCodeParams->roi1Area.enable) {
        if(!(pCodeParams->roi1Area.top <= pCodeParams->roi1Area.bottom &&
           pCodeParams->roi1Area.bottom < pEncInst->mbPerCol &&
           pCodeParams->roi1Area.left <= pCodeParams->roi1Area.right &&
           pCodeParams->roi1Area.right < pEncInst->mbPerRow))
        {
            APITRACE("VP8EncSetCodingCtrl: ERROR Invalid roi1Area");
            return VP8ENC_INVALID_ARGUMENT;
        }
        area1 = (pCodeParams->roi1Area.right + 1 - pCodeParams->roi1Area.left)*
                (pCodeParams->roi1Area.bottom + 1 - pCodeParams->roi1Area.top);
    }

    if(pCodeParams->roi2Area.enable) {
        if (!pCodeParams->roi1Area.enable) {
            APITRACE("VP8EncSetCodingCtrl: ERROR Roi2 enabled but not Roi1");
            return VP8ENC_INVALID_ARGUMENT;
        }
        if(!(pCodeParams->roi2Area.top <= pCodeParams->roi2Area.bottom &&
           pCodeParams->roi2Area.bottom < pEncInst->mbPerCol &&
           pCodeParams->roi2Area.left <= pCodeParams->roi2Area.right &&
           pCodeParams->roi2Area.right < pEncInst->mbPerRow))
        {
            APITRACE("VP8EncSetCodingCtrl: ERROR Invalid roi2Area");
            return VP8ENC_INVALID_ARGUMENT;
        }
        area2 = (pCodeParams->roi2Area.right + 1 - pCodeParams->roi2Area.left)*
                (pCodeParams->roi2Area.bottom + 1 - pCodeParams->roi2Area.top);
    }

    if (area1 + area2 >= pEncInst->mbPerFrame) {
        APITRACE("VP8EncSetCodingCtrl: ERROR Invalid roi (whole frame)");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pCodeParams->roi1DeltaQp < -127 ||
       pCodeParams->roi1DeltaQp > 0 ||
       pCodeParams->roi2DeltaQp < -127 ||
       pCodeParams->roi2DeltaQp > 0 ||
       pCodeParams->adaptiveRoi < -127 ||
       pCodeParams->adaptiveRoi > 0)
    {
        APITRACE("VP8EncSetCodingCtrl: ERROR Invalid ROI delta QP");
        return VP8ENC_INVALID_ARGUMENT;
    }

    for (i = 0; i < 5; i++) {
        if ((pCodeParams->qpDelta[i] < -15) ||
            (pCodeParams->qpDelta[i] > VP8ENC_QPDELTA_AUTO)) {
            APITRACE("VP8EncSetCodingCtrl: ERROR Invalid QP delta value");
            return VP8ENC_INVALID_ARGUMENT;
        }
    }

    sps = &pEncInst->sps;

    if (pCodeParams->filterLevel == VP8ENC_FILTER_LEVEL_AUTO) {
        sps->autoFilterLevel = 1;
        sps->filterLevel     = 0;
    } else {
        sps->autoFilterLevel = 0;
        sps->filterLevel     = pCodeParams->filterLevel;
    }

    if (pCodeParams->filterSharpness == VP8ENC_FILTER_SHARPNESS_AUTO) {
        sps->autoFilterSharpness = 1;
        sps->filterSharpness     = 0;
    } else {
        sps->autoFilterSharpness = 0;
        sps->filterSharpness     = pCodeParams->filterSharpness;
    }

    sps->profile           = pCodeParams->interpolationFilter ? 1 : 0;
    sps->filterType        = pCodeParams->filterType;
    sps->dctPartitions     = pCodeParams->dctPartitions;
    sps->partitionCnt      = 2 + (1 << sps->dctPartitions);
    sps->refreshEntropy    = pCodeParams->errorResilient ? 0 : 1;
    sps->quarterPixelMv    = pCodeParams->quarterPixelMv;
    sps->splitMv           = pCodeParams->splitMv;

    regs = &pEncInst->asic.regs;
    regs->cirStart = pCodeParams->cirStart;
    regs->cirInterval = pCodeParams->cirInterval;
    if (pCodeParams->intraArea.enable) {
        regs->intraAreaTop = pCodeParams->intraArea.top;
        regs->intraAreaLeft = pCodeParams->intraArea.left;
        regs->intraAreaBottom = pCodeParams->intraArea.bottom;
        regs->intraAreaRight = pCodeParams->intraArea.right;
    } else {
        regs->intraAreaTop = regs->intraAreaLeft = regs->intraAreaBottom =
        regs->intraAreaRight = 255;
    }
    if (pCodeParams->roi1Area.enable) {
        regs->roi1Top = pCodeParams->roi1Area.top;
        regs->roi1Left = pCodeParams->roi1Area.left;
        regs->roi1Bottom = pCodeParams->roi1Area.bottom;
        regs->roi1Right = pCodeParams->roi1Area.right;
    } else {
        regs->roi1Top = regs->roi1Left = regs->roi1Bottom =
        regs->roi1Right = 255;
    }
    if (pCodeParams->roi2Area.enable) {
        regs->roi2Top = pCodeParams->roi2Area.top;
        regs->roi2Left = pCodeParams->roi2Area.left;
        regs->roi2Bottom = pCodeParams->roi2Area.bottom;
        regs->roi2Right = pCodeParams->roi2Area.right;
    } else {
        regs->roi2Top = regs->roi2Left = regs->roi2Bottom =
        regs->roi2Right = 255;
    }

    /* ROI setting updates the segmentation map usage */
    if (pCodeParams->roi1Area.enable ||
        pCodeParams->roi2Area.enable ||
        pCodeParams->adaptiveRoi)
        pEncInst->ppss.pps->segmentEnabled = 1;
    else {
        pEncInst->ppss.pps->segmentEnabled = 0;
    }

    regs->roiUpdate         = 1;    /* New ROI must be coded. */
    regs->roi1DeltaQp       = -pCodeParams->roi1DeltaQp;
    regs->roi2DeltaQp       = -pCodeParams->roi2DeltaQp;
    regs->deadzoneEnable    = pCodeParams->deadzone ? 1 : 0;
    
    pEncInst->maxNumPasses  = pCodeParams->maxNumPasses;
    pEncInst->qualityMetric = pCodeParams->qualityMetric;

    for (i = 0; i < 5; i++) {
        if (pCodeParams->qpDelta[i] == VP8ENC_QPDELTA_AUTO) {
            sps->autoQpDelta[i] = 1;
            sps->qpDelta[i]     = 0;
        } else {
            sps->autoQpDelta[i] = 0;
            sps->qpDelta[i]     = pCodeParams->qpDelta[i];
        }
    }

    pEncInst->preProcess.adaptiveRoi = pCodeParams->adaptiveRoi;
    pEncInst->preProcess.adaptiveRoiColor =
                                CLIP3(pCodeParams->adaptiveRoiColor, -10, 10);

    APITRACE("VP8EncSetCodingCtrl: OK");
    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------

    Function name : VP8EncGetCodingCtrl
    Description   : Returns current encoding parameters

    Return type   : VP8EncRet
    Argument      : inst - the instance in use
                    pCodeParams - palce where parameters are returned
------------------------------------------------------------------------------*/
VP8EncRet VP8EncGetCodingCtrl(VP8EncInst inst,
                                VP8EncCodingCtrl * pCodeParams)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;
    regValues_s *regs;
    sps *sps;
    i32 i;

    APITRACE("VP8EncGetCodingCtrl#");

    /* Check for illegal inputs */
    if((pEncInst == NULL) || (pCodeParams == NULL))
    {
        APITRACE("VP8EncGetCodingCtrl: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncGetCodingCtrl: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

    sps = &pEncInst->sps;

    pCodeParams->interpolationFilter    = sps->profile;
    pCodeParams->dctPartitions          = sps->dctPartitions;
    pCodeParams->quarterPixelMv         = sps->quarterPixelMv;
    pCodeParams->splitMv                = sps->splitMv;
    pCodeParams->filterType             = sps->filterType;
    pCodeParams->errorResilient         = !sps->refreshEntropy;

    if (sps->autoFilterLevel)
        pCodeParams->filterLevel            = VP8ENC_FILTER_LEVEL_AUTO;
    else
        pCodeParams->filterLevel            = sps->filterLevel;

    if (sps->autoFilterSharpness)
        pCodeParams->filterSharpness        = VP8ENC_FILTER_SHARPNESS_AUTO;
    else
        pCodeParams->filterSharpness        = sps->filterSharpness;

    regs = &pEncInst->asic.regs;
    pCodeParams->cirStart         = regs->cirStart;
    pCodeParams->cirInterval      = regs->cirInterval;
    pCodeParams->intraArea.enable = regs->intraAreaTop<pEncInst->mbPerCol ?1 :0;
    pCodeParams->intraArea.top    = regs->intraAreaTop;
    pCodeParams->intraArea.left   = regs->intraAreaLeft;
    pCodeParams->intraArea.bottom = regs->intraAreaBottom;
    pCodeParams->intraArea.right  = regs->intraAreaRight;
    pCodeParams->roi1Area.enable  = regs->roi1Top<pEncInst->mbPerCol ?1 :0;
    pCodeParams->roi1Area.top     = regs->roi1Top;
    pCodeParams->roi1Area.left    = regs->roi1Left;
    pCodeParams->roi1Area.bottom  = regs->roi1Bottom;
    pCodeParams->roi1Area.right   = regs->roi1Right;
    pCodeParams->roi2Area.enable  = regs->roi2Top<pEncInst->mbPerCol ?1 :0;
    pCodeParams->roi2Area.top     = regs->roi2Top;
    pCodeParams->roi2Area.left    = regs->roi2Left;
    pCodeParams->roi2Area.bottom  = regs->roi2Bottom;
    pCodeParams->roi2Area.right   = regs->roi2Right;
    pCodeParams->roi1DeltaQp      = -regs->roi1DeltaQp;
    pCodeParams->roi2DeltaQp      = -regs->roi2DeltaQp;
    pCodeParams->deadzone         = regs->deadzoneEnable;
    pCodeParams->maxNumPasses     = pEncInst->maxNumPasses;
    pCodeParams->qualityMetric    = pEncInst->qualityMetric;
    pCodeParams->adaptiveRoi      = pEncInst->preProcess.adaptiveRoi;
    pCodeParams->adaptiveRoiColor = pEncInst->preProcess.adaptiveRoiColor;

    for (i = 0; i < 5; i++) {
        if (sps->autoQpDelta[i]) pCodeParams->qpDelta[i] = VP8ENC_QPDELTA_AUTO;
        else                     pCodeParams->qpDelta[i] = sps->qpDelta[i];
    }

    APITRACE("VP8EncGetCodingCtrl: OK");
    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------

    Function name : VP8EncSetRateCtrl
    Description   : Sets rate control parameters

    Return type   : VP8EncRet
    Argument      : inst - the instance in use
                    pRateCtrl - user provided parameters
------------------------------------------------------------------------------*/
VP8EncRet VP8EncSetRateCtrl(VP8EncInst inst,
                              const VP8EncRateCtrl * pRateCtrl)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;
    vp8RateControl_s rc_tmp;
    u32 bitPerSecond, i;

    APITRACE("VP8EncSetRateCtrl#");

    /* Check for illegal inputs */
    if((pEncInst == NULL) || (pRateCtrl == NULL))
    {
        APITRACE("VP8EncSetRateCtrl: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    APITRACEPARAM("pictureRc", pRateCtrl->pictureRc);
    APITRACEPARAM("pictureSkip", pRateCtrl->pictureSkip);
    APITRACEPARAM("qpHdr", pRateCtrl->qpHdr);
    APITRACEPARAM("qpMin", pRateCtrl->qpMin);
    APITRACEPARAM("qpMax", pRateCtrl->qpMax);
    APITRACEPARAM("bitPerSecond", pRateCtrl->bitPerSecond);
    APITRACEPARAM("layerBitPerSecond[0]", pRateCtrl->layerBitPerSecond[0]);
    APITRACEPARAM("layerBitPerSecond[1]", pRateCtrl->layerBitPerSecond[1]);
    APITRACEPARAM("layerBitPerSecond[2]", pRateCtrl->layerBitPerSecond[2]);
    APITRACEPARAM("layerBitPerSecond[3]", pRateCtrl->layerBitPerSecond[3]);
    APITRACEPARAM("layerFrameRateDenom[0]", pRateCtrl->layerFrameRateDenom[0]);
    APITRACEPARAM("layerFrameRateDenom[1]", pRateCtrl->layerFrameRateDenom[1]);
    APITRACEPARAM("layerFrameRateDenom[2]", pRateCtrl->layerFrameRateDenom[2]);
    APITRACEPARAM("layerFrameRateDenom[3]", pRateCtrl->layerFrameRateDenom[3]);
    APITRACEPARAM("bitrateWindow", pRateCtrl->bitrateWindow);
    APITRACEPARAM("intraQpDelta", pRateCtrl->intraQpDelta);
    APITRACEPARAM("fixedIntraQp", pRateCtrl->fixedIntraQp);
    APITRACEPARAM("intraPictureRate", pRateCtrl->intraPictureRate);
    APITRACEPARAM("goldenPictureRate", pRateCtrl->goldenPictureRate);
    APITRACEPARAM("altrefPictureRate", pRateCtrl->altrefPictureRate);
    APITRACEPARAM("goldenPictureBoost", pRateCtrl->goldenPictureBoost);
    APITRACEPARAM("adaptiveGoldenBoost", pRateCtrl->adaptiveGoldenBoost);
    APITRACEPARAM("adaptiveGoldenUpdate", pRateCtrl->adaptiveGoldenUpdate);

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncSetRateCtrl: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

    /* Check for invalid input values */
    if(pRateCtrl->pictureRc > 1 || pRateCtrl->pictureSkip > 1)
    {
        APITRACE("VP8EncSetRateCtrl: ERROR Invalid enable/disable value");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pRateCtrl->qpHdr > 127 ||
       pRateCtrl->qpMin > 127 ||
       pRateCtrl->qpMax > 127 ||
       pRateCtrl->qpMax < pRateCtrl->qpMin)
    {
        APITRACE("VP8EncSetRateCtrl: ERROR Invalid QP");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if((pRateCtrl->qpHdr != -1) &&
      (pRateCtrl->qpHdr < (i32)pRateCtrl->qpMin ||
       pRateCtrl->qpHdr > (i32)pRateCtrl->qpMax))
    {
        APITRACE("VP8EncSetRateCtrl: ERROR QP out of range");
        return VP8ENC_INVALID_ARGUMENT;
    }
    if((u32)(pRateCtrl->intraQpDelta + 127) > 254)
    {
        APITRACE("VP8EncSetRateCtrl: ERROR intraQpDelta out of range");
        return VP8ENC_INVALID_ARGUMENT;
    }
    if(pRateCtrl->fixedIntraQp > 127)
    {
        APITRACE("VP8EncSetRateCtrl: ERROR fixedIntraQp out of range");
        return VP8ENC_INVALID_ARGUMENT;
    }
    if(pRateCtrl->bitrateWindow < 1 || pRateCtrl->bitrateWindow > 300)
    {
        APITRACE("VP8EncSetRateCtrl: ERROR Invalid bitrate window length");
        return VP8ENC_INVALID_ARGUMENT;
    }
    /* If layer bitrates present they override the total bitrate. */
    bitPerSecond = pRateCtrl->bitPerSecond;
    if(pRateCtrl->layerBitPerSecond[0]) {
        bitPerSecond = 
          pRateCtrl->layerBitPerSecond[0] + pRateCtrl->layerBitPerSecond[1] +
          pRateCtrl->layerBitPerSecond[2] + pRateCtrl->layerBitPerSecond[3];
    }

    /* Bitrate affects only when rate control is enabled */
    if((pRateCtrl->pictureRc || pRateCtrl->pictureSkip) &&
       (bitPerSecond < 10000 ||
        bitPerSecond > VP8ENC_MAX_BITRATE))
    {
        APITRACE("VP8EncSetRateCtrl: ERROR Invalid bitPerSecond");
        return VP8ENC_INVALID_ARGUMENT;
    }

    /* All values OK, use them. */
    rc_tmp = pEncInst->rateControl;
    rc_tmp.qpHdr                    = pRateCtrl->qpHdr;
    rc_tmp.picRc                    = pRateCtrl->pictureRc;
    rc_tmp.picSkip                  = pRateCtrl->pictureSkip;
    rc_tmp.qpMin                    = pRateCtrl->qpMin;
    rc_tmp.qpMax                    = pRateCtrl->qpMax;
    if(pRateCtrl->layerBitPerSecond[0]) {
      for (i = 0; i < 4; i++) {
        rc_tmp.virtualBuffer[i].bitRate = pRateCtrl->layerBitPerSecond[i];
        rc_tmp.virtualBuffer[i].outRateDenom = pRateCtrl->layerFrameRateDenom[i];
      }
    } else {
      rc_tmp.virtualBuffer[0].bitRate = pRateCtrl->bitPerSecond;
      rc_tmp.virtualBuffer[0].outRateDenom = pEncInst->rateControl.outRateDenom;
      for (i = 1; i < 4; i++) {
        rc_tmp.virtualBuffer[i].bitRate = 0;
        rc_tmp.virtualBuffer[i].outRateDenom = 0;
      }
    }
    /* When layers enabled, RC window tracks base layer frames only. */
    rc_tmp.windowLen                = pRateCtrl->bitrateWindow *
      pEncInst->rateControl.outRateDenom / rc_tmp.virtualBuffer[0].outRateDenom;
    if(rc_tmp.windowLen < 1) {
        APITRACE("VP8EncSetRateCtrl: ERROR Invalid bitrate window for layer 0");
        return VP8ENC_INVALID_ARGUMENT;
    }
    rc_tmp.intraQpDelta             = pRateCtrl->intraQpDelta;
    rc_tmp.fixedIntraQp             = pRateCtrl->fixedIntraQp;
    rc_tmp.intraPictureRate         = pRateCtrl->intraPictureRate;
    rc_tmp.goldenPictureRate        = 0;
    rc_tmp.altrefPictureRate        = 0;
    if (pEncInst->numRefBuffsLum > 1)
        rc_tmp.goldenPictureRate    = pRateCtrl->goldenPictureRate;
    if (pEncInst->numRefBuffsLum > 2)
        rc_tmp.altrefPictureRate    = pRateCtrl->altrefPictureRate;
    rc_tmp.goldenPictureBoost       = pRateCtrl->goldenPictureBoost;
    rc_tmp.adaptiveGoldenBoost      = pRateCtrl->adaptiveGoldenBoost;
    rc_tmp.adaptiveGoldenUpdate     = pRateCtrl->adaptiveGoldenUpdate;
    
    /* Reset stream bit counters for new stream and when changing bitrate. */
    VP8InitRc(&rc_tmp, (pEncInst->encStatus == VP8ENCSTAT_INIT) ||
            (rc_tmp.virtualBuffer[0].bitRate !=
             pEncInst->rateControl.virtualBuffer[0].bitRate));

    /* Set final values into instance */
    pEncInst->rateControl = rc_tmp;

    APITRACE("VP8EncSetRateCtrl: OK");
    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------

    Function name : VP8EncGetRateCtrl
    Description   : Return current rate control parameters

    Return type   : VP8EncRet
    Argument      : inst - the instance in use
                    pRateCtrl - place where parameters are returned
------------------------------------------------------------------------------*/
VP8EncRet VP8EncGetRateCtrl(VP8EncInst inst, VP8EncRateCtrl * pRateCtrl)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;

    APITRACE("VP8EncGetRateCtrl#");

    /* Check for illegal inputs */
    if((pEncInst == NULL) || (pRateCtrl == NULL))
    {
        APITRACE("VP8EncGetRateCtrl: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncGetRateCtrl: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

    pRateCtrl->qpHdr            = pEncInst->rateControl.qpHdr;
    pRateCtrl->pictureRc        = pEncInst->rateControl.picRc;
    pRateCtrl->pictureSkip      = pEncInst->rateControl.picSkip;
    pRateCtrl->qpMin            = pEncInst->rateControl.qpMin;
    pRateCtrl->qpMax            = pEncInst->rateControl.qpMax;
    pRateCtrl->layerBitPerSecond[0] = pEncInst->rateControl.virtualBuffer[0].bitRate;
    pRateCtrl->layerBitPerSecond[1] = pEncInst->rateControl.virtualBuffer[1].bitRate;
    pRateCtrl->layerBitPerSecond[2] = pEncInst->rateControl.virtualBuffer[2].bitRate;
    pRateCtrl->layerBitPerSecond[3] = pEncInst->rateControl.virtualBuffer[3].bitRate;
    pRateCtrl->layerFrameRateDenom[0] = pEncInst->rateControl.virtualBuffer[0].outRateDenom;
    pRateCtrl->layerFrameRateDenom[1] = pEncInst->rateControl.virtualBuffer[1].outRateDenom;
    pRateCtrl->layerFrameRateDenom[2] = pEncInst->rateControl.virtualBuffer[2].outRateDenom;
    pRateCtrl->layerFrameRateDenom[3] = pEncInst->rateControl.virtualBuffer[3].outRateDenom;
    pRateCtrl->bitPerSecond =
        pRateCtrl->layerBitPerSecond[0] + pRateCtrl->layerBitPerSecond[1] +
        pRateCtrl->layerBitPerSecond[2] + pRateCtrl->layerBitPerSecond[3];
    if (pRateCtrl->bitPerSecond == pRateCtrl->layerBitPerSecond[0])
        pRateCtrl->layerBitPerSecond[0] = 0;  /* Layer bitrates disabled */
    /* Layer RC windowLen tracks base layer only, convert to stream frames. */
    pRateCtrl->bitrateWindow    = pEncInst->rateControl.windowLen *
        pEncInst->rateControl.virtualBuffer[0].outRateDenom /
        pEncInst->rateControl.outRateDenom;
    pRateCtrl->intraQpDelta     = pEncInst->rateControl.intraQpDelta;
    pRateCtrl->fixedIntraQp     = pEncInst->rateControl.fixedIntraQp;
    pRateCtrl->intraPictureRate = pEncInst->rateControl.intraPictureRate;
    pRateCtrl->goldenPictureRate= pEncInst->rateControl.goldenPictureRate;
    pRateCtrl->altrefPictureRate= pEncInst->rateControl.altrefPictureRate;
    pRateCtrl->goldenPictureBoost=pEncInst->rateControl.goldenPictureBoost;
    pRateCtrl->adaptiveGoldenBoost = pEncInst->rateControl.adaptiveGoldenBoost;
    pRateCtrl->adaptiveGoldenUpdate= pEncInst->rateControl.adaptiveGoldenUpdate;
    
    APITRACE("VP8EncGetRateCtrl: OK");
    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------
    Function name   : VSCheckSize
    Description     :
    Return type     : i32
    Argument        : u32 inputWidth
    Argument        : u32 inputHeight
    Argument        : u32 stabilizedWidth
    Argument        : u32 stabilizedHeight
------------------------------------------------------------------------------*/
i32 VSCheckSize(u32 inputWidth, u32 inputHeight, u32 stabilizedWidth,
                u32 stabilizedHeight)
{
    /* Input picture minimum dimensions */
    if((inputWidth < 104) || (inputHeight < 104))
        return 1;

    /* Stabilized picture minimum  values */
    if((stabilizedWidth < 96) || (stabilizedHeight < 96))
        return 1;

    /* Stabilized dimensions multiple of 4 */
    if(((stabilizedWidth & 3) != 0) || ((stabilizedHeight & 3) != 0))
        return 1;

    /* Edge >= 4 pixels, not checked because stabilization can be
     * used without cropping for scene detection
    if((inputWidth < (stabilizedWidth + 8)) ||
       (inputHeight < (stabilizedHeight + 8)))
        return 1; */

    return 0;
}

/*------------------------------------------------------------------------------
    Function name   : VP8EncSetPreProcessing
    Description     : Sets the preprocessing parameters
    Return type     : VP8EncRet
    Argument        : inst - encoder instance in use
    Argument        : pPreProcCfg - user provided parameters
------------------------------------------------------------------------------*/
VP8EncRet VP8EncSetPreProcessing(VP8EncInst inst,
                                   const VP8EncPreProcessingCfg * pPreProcCfg)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;
    preProcess_s pp_tmp;

    APITRACE("VP8EncSetPreProcessing#");

    /* Check for illegal inputs */
    if((inst == NULL) || (pPreProcCfg == NULL))
    {
        APITRACE("VP8EncSetPreProcessing: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    APITRACEPARAM("origWidth", pPreProcCfg->origWidth);
    APITRACEPARAM("origHeight", pPreProcCfg->origHeight);
    APITRACEPARAM("xOffset", pPreProcCfg->xOffset);
    APITRACEPARAM("yOffset", pPreProcCfg->yOffset);
    APITRACEPARAM("inputType", pPreProcCfg->inputType);
    APITRACEPARAM("rotation", pPreProcCfg->rotation);
    APITRACEPARAM("videoStabilization", pPreProcCfg->videoStabilization);
    APITRACEPARAM("colorConversion.type", pPreProcCfg->colorConversion.type);
    APITRACEPARAM("colorConversion.coeffA", pPreProcCfg->colorConversion.coeffA);
    APITRACEPARAM("colorConversion.coeffB", pPreProcCfg->colorConversion.coeffB);
    APITRACEPARAM("colorConversion.coeffC", pPreProcCfg->colorConversion.coeffC);
    APITRACEPARAM("colorConversion.coeffE", pPreProcCfg->colorConversion.coeffE);
    APITRACEPARAM("colorConversion.coeffF", pPreProcCfg->colorConversion.coeffF);
    APITRACEPARAM("scaledOutput", pPreProcCfg->scaledOutput);

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncSetPreProcessing: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

    /* check HW limitations */
    {
        EWLHwConfig_t cfg = EWLReadAsicConfig();

        /* is video stabilization supported? */
        if(cfg.vsEnabled == EWL_HW_CONFIG_NOT_SUPPORTED &&
           pPreProcCfg->videoStabilization != 0)
        {
            APITRACE("VP8EncSetPreProcessing: ERROR Stabilization not supported");
            return VP8ENC_INVALID_ARGUMENT;
        }
        if(cfg.rgbEnabled == EWL_HW_CONFIG_NOT_SUPPORTED &&
           pPreProcCfg->inputType >= VP8ENC_RGB565 &&
           pPreProcCfg->inputType <= VP8ENC_BGR101010)
        {
            APITRACE("VP8EncSetPreProcessing: ERROR RGB input not supported");
            return VP8ENC_INVALID_ARGUMENT;
        }
        if(cfg.scalingEnabled == EWL_HW_CONFIG_NOT_SUPPORTED &&
           pPreProcCfg->scaledOutput != 0)
        {
            APITRACE("VP8EncSetPreProcessing: WARNING Scaling not supported, disabling output");
        }
    }

    if(pPreProcCfg->origWidth > VP8ENC_MAX_PP_INPUT_WIDTH ||
       pPreProcCfg->origHeight > VP8ENC_MAX_PP_INPUT_HEIGHT)
    {
        APITRACE("VP8EncSetPreProcessing: ERROR Too big input image");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pPreProcCfg->inputType > VP8ENC_BGR101010)
    {
        APITRACE("VP8EncSetPreProcessing: ERROR Invalid YUV type");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pPreProcCfg->rotation > VP8ENC_ROTATE_90L)
    {
        APITRACE("VP8EncSetPreProcessing: ERROR Invalid rotation");
        return VP8ENC_INVALID_ARGUMENT;
    }

    /* Pre-process struct as set in Init() */
    pp_tmp              = pEncInst->preProcess;

    pp_tmp.lumHeightSrc = pPreProcCfg->origHeight;
    pp_tmp.lumWidthSrc  = pPreProcCfg->origWidth;
    pp_tmp.rotation     = pPreProcCfg->rotation;
    pp_tmp.inputFormat  = pPreProcCfg->inputType;
    pp_tmp.videoStab    = (pPreProcCfg->videoStabilization != 0) ? 1 : 0;
    pp_tmp.scaledOutput = (pPreProcCfg->scaledOutput) ? 1 : 0;
    if (pEncInst->preProcess.scaledWidth*pEncInst->preProcess.scaledHeight == 0)
        pp_tmp.scaledOutput = 0;

    if(pPreProcCfg->videoStabilization == 0) {
        pp_tmp.horOffsetSrc = pPreProcCfg->xOffset;
        pp_tmp.verOffsetSrc = pPreProcCfg->yOffset;
    } else {
        pp_tmp.horOffsetSrc = pp_tmp.verOffsetSrc = 0;
    }

    /* Check for invalid values */
    if(EncPreProcessCheck(&pp_tmp) != ENCHW_OK) {
        APITRACE("VP8EncSetPreProcessing: ERROR Invalid cropping values");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pp_tmp.videoStab != 0) {
        u32 width = pp_tmp.lumWidth;
        u32 height = pp_tmp.lumHeight;

        if(pp_tmp.rotation) {
            u32 tmp = width;
            width = height;
            height = tmp;
        }

        if(VSCheckSize(pp_tmp.lumWidthSrc, pp_tmp.lumHeightSrc, width, height) != 0) {
            APITRACE("H264EncSetPreProcessing: ERROR Invalid size for stabilization");
            return VP8ENC_INVALID_ARGUMENT;
        }

#ifdef VIDEOSTAB_ENABLED
        VSAlgInit(&pEncInst->vsSwData, pp_tmp.lumWidthSrc, pp_tmp.lumHeightSrc,
                  width, height);

        VSAlgGetResult(&pEncInst->vsSwData, &pp_tmp.horOffsetSrc,
                       &pp_tmp.verOffsetSrc);
#endif
    }

    pp_tmp.colorConversionType = pPreProcCfg->colorConversion.type;
    pp_tmp.colorConversionCoeffA = pPreProcCfg->colorConversion.coeffA;
    pp_tmp.colorConversionCoeffB = pPreProcCfg->colorConversion.coeffB;
    pp_tmp.colorConversionCoeffC = pPreProcCfg->colorConversion.coeffC;
    pp_tmp.colorConversionCoeffE = pPreProcCfg->colorConversion.coeffE;
    pp_tmp.colorConversionCoeffF = pPreProcCfg->colorConversion.coeffF;
    EncSetColorConversion(&pp_tmp, &pEncInst->asic);

    /* Set final values into instance */
    pEncInst->preProcess = pp_tmp;

    APITRACE("VP8EncSetPreProcessing: OK");

    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------
    Function name   : VP8EncGetPreProcessing
    Description     : Returns current preprocessing parameters
    Return type     : VP8EncRet
    Argument        : inst - encoder instance
    Argument        : pPreProcCfg - place where the parameters are returned
------------------------------------------------------------------------------*/
VP8EncRet VP8EncGetPreProcessing(VP8EncInst inst,
                                   VP8EncPreProcessingCfg * pPreProcCfg)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;
    preProcess_s *pPP;

    APITRACE("VP8EncGetPreProcessing#");

    /* Check for illegal inputs */
    if((inst == NULL) || (pPreProcCfg == NULL))
    {
        APITRACE("VP8EncGetPreProcessing: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncGetPreProcessing: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

    pPP = &pEncInst->preProcess;

    pPreProcCfg->origWidth              = pPP->lumWidthSrc;
    pPreProcCfg->origHeight             = pPP->lumHeightSrc;
    pPreProcCfg->xOffset                = pPP->horOffsetSrc;
    pPreProcCfg->yOffset                = pPP->verOffsetSrc;
    pPreProcCfg->inputType              = (VP8EncPictureType)pPP->inputFormat;
    pPreProcCfg->rotation               = (VP8EncPictureRotation)pPP->rotation;
    pPreProcCfg->videoStabilization     = pPP->videoStab;
    pPreProcCfg->scaledOutput           = pPP->scaledOutput;
    pPreProcCfg->colorConversion.type   =
                            (VP8EncColorConversionType)pPP->colorConversionType;
    pPreProcCfg->colorConversion.coeffA = pPP->colorConversionCoeffA;
    pPreProcCfg->colorConversion.coeffB = pPP->colorConversionCoeffB;
    pPreProcCfg->colorConversion.coeffC = pPP->colorConversionCoeffC;
    pPreProcCfg->colorConversion.coeffE = pPP->colorConversionCoeffE;
    pPreProcCfg->colorConversion.coeffF = pPP->colorConversionCoeffF;

    APITRACE("VP8EncGetPreProcessing: OK");
    return VP8ENC_OK;
}

/*------------------------------------------------------------------------------
    Function name : SetStreamBuffer
    Description   : Divides the application given stream buffer for the
                    number of partitions that the HW encoder will create.
    Return type   : VP8EncRet
------------------------------------------------------------------------------*/
bool_e SetStreamBuffer(vp8Instance_s *pEncInst, const VP8EncIn * pEncIn,
                            VP8EncPictureCodingType ct)
{
    regValues_s *regs = &pEncInst->asic.regs;
    u8 *pStart = (u8*)pEncIn->pOutBuf;
    u8 *pEnd;
    ptr_t busAddr = pEncIn->busOutBuf;
    i32 bufSize = pEncIn->outBufSize;
    i32 status = ENCHW_OK;
    i32 partSize, i;

    /* Frame tag 10 bytes (I-frame) or 3 bytes (P-frame),
     * written by SW at end of frame */
    pEnd = pStart + 3;
    if (ct == VP8ENC_INTRA_FRAME) pEnd += 7;
    if(VP8SetBuffer(&pEncInst->buffer[0], pStart, pEnd-pStart) == ENCHW_NOK)
        status = ENCHW_NOK;

    /* StrmBase is not 64-bit aligned now but that will be taken care of
     * in CodeFrame() */
    busAddr += pEnd-pStart;
    regs->outputStrmBase = busAddr;

    /* 10% for frame header and mb control partition. */
    pStart = pEnd;
    pEnd = pStart + bufSize/10;
    pEnd = (u8*)((size_t)pEnd & ~0x7);   /* 64-bit aligned address */
    if(VP8SetBuffer(&pEncInst->buffer[1], pStart, pEnd-pStart) == ENCHW_NOK)
        status = ENCHW_NOK;

    busAddr += pEnd-pStart;
    regs->partitionBase[0] = busAddr;

    /* The rest is divided for one/two/four DCT partitions of equal size. */
    partSize = bufSize / 80 * (72 / (1 << pEncInst->sps.dctPartitions));

    /* ASIC stream buffer limit, the same limit is used for all partitions. */
    regs->outputStrmSize = partSize;

    for (i = 2; i < pEncInst->sps.partitionCnt; i++)
    {
        pStart = pEnd;
        pEnd = pStart + partSize;
        pEnd = (u8*)((size_t)pEnd & ~0x7);   /* 64-bit aligned address */
        if(VP8SetBuffer(&pEncInst->buffer[i], pStart, pEnd-pStart) == ENCHW_NOK)
            status = ENCHW_NOK;

        busAddr += pEnd-pStart;
        regs->partitionBase[i-1] = busAddr;
    }

    return status;
}

/*------------------------------------------------------------------------------

    Function name : VP8EncStrmEncode
    Description   : Encodes a new picture
    Return type   : VP8EncRet
    Argument      : inst - encoder instance
    Argument      : pEncIn - user provided input parameters
                    pEncOut - place where output info is returned
------------------------------------------------------------------------------*/
VP8EncRet VP8EncStrmEncode(VP8EncInst inst, const VP8EncIn * pEncIn,
                             VP8EncOut * pEncOut)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;
    regValues_s *regs;
    vp8EncodeFrame_e ret;
    picBuffer *picBuffer;
    i32 i;
    VP8EncPictureCodingType ct;
    i32 goldenRefresh = 0;
    i32 boostPct = 0;

    APITRACE("VP8EncStrmEncode#");

    /* Check for illegal inputs */
    if((pEncInst == NULL) || (pEncIn == NULL) || (pEncOut == NULL))
    {
        APITRACE("VP8EncStrmEncode: ERROR Null argument");
        return VP8ENC_NULL_ARGUMENT;
    }

    APITRACEPARAM("busLuma", pEncIn->busLuma);
    APITRACEPARAM("busChromaU", pEncIn->busChromaU);
    APITRACEPARAM("busChromaV", pEncIn->busChromaV);
    APITRACEPARAM("pOutBuf", pEncIn->pOutBuf);
    APITRACEPARAM("busOutBuf", pEncIn->busOutBuf);
    APITRACEPARAM("outBufSize", pEncIn->outBufSize);
    APITRACEPARAM("codingType", pEncIn->codingType);
    APITRACEPARAM("timeIncrement", pEncIn->timeIncrement);
    APITRACEPARAM("busLumaStab", pEncIn->busLumaStab);
    APITRACEPARAM("layerId", pEncIn->layerId);
    APITRACEPARAM("ipf", pEncIn->ipf);
    APITRACEPARAM("grf", pEncIn->grf);
    APITRACEPARAM("arf", pEncIn->arf);

    /* Check for existing instance */
    if(pEncInst->inst != pEncInst)
    {
        APITRACE("VP8EncStrmEncode: ERROR Invalid instance");
        return VP8ENC_INSTANCE_ERROR;
    }

    /* Check for valid layerId */
    if(pEncInst->rateControl.layerAmount > 1 && 
       pEncIn->layerId >= pEncInst->rateControl.layerAmount)
    {
        APITRACE("VP8EncStrmEncode: ERROR Invalid layerId");
        return VP8ENC_INVALID_ARGUMENT;
    }
    /* Temporal layer ID affects RC and prob updates. */
    if(pEncInst->rateControl.layerAmount > 1)
        pEncInst->layerId = pEncIn->layerId;
    else
        pEncInst->layerId = 0;

    /* some shortcuts */
    regs = &pEncInst->asic.regs;

    /* Clear the output structure */
    pEncOut->codingType = VP8ENC_NOTCODED_FRAME;
    pEncOut->frameSize = 0;
    pEncOut->ipf = pEncOut->grf = pEncOut->arf = 0;
    for (i = 0; i < 9; i++)
    {
        pEncOut->pOutBuf[i] = NULL;
        pEncOut->streamSize[i] = 0;
    }

    /* Output buffer for down-scaled picture, 0/NULL when disabled. */
    pEncOut->busScaledLuma    = regs->scaledLumBase;
    pEncOut->scaledPicture    = (u8*)pEncInst->asic.scaledImage.virtualAddress;

#ifdef EVALUATION_LIMIT
    /* Check for evaluation limit */
    if(pEncInst->frameCnt >= EVALUATION_LIMIT)
    {
        APITRACE("VP8EncStrmEncode: OK Evaluation limit exceeded");
        return VP8ENC_OK;
    }
#endif

    /* Check status, ERROR not allowed */
    if((pEncInst->encStatus != VP8ENCSTAT_INIT) &&
       (pEncInst->encStatus != VP8ENCSTAT_KEYFRAME) &&
       (pEncInst->encStatus != VP8ENCSTAT_START_FRAME))
    {
        APITRACE("VP8EncStrmEncode: ERROR Invalid status");
        return VP8ENC_INVALID_STATUS;
    }

    /* Check for invalid input values */
    if((!VP8_BUS_ADDRESS_VALID(pEncIn->busOutBuf)) ||
       (pEncIn->pOutBuf == NULL) ||
       (pEncIn->outBufSize < VP8ENCSTRMENCODE_MIN_BUF) ||
       (pEncIn->codingType > VP8ENC_PREDICTED_FRAME))
    {
        APITRACE("VP8EncStrmEncode: ERROR Invalid input. Output buffer");
        return VP8ENC_INVALID_ARGUMENT;
    }

    switch (pEncInst->preProcess.inputFormat)
    {
    case VP8ENC_YUV420_PLANAR:
        if(!VP8_BUS_ADDRESS_VALID(pEncIn->busChromaV))
        {
            APITRACE("VP8EncStrmEncode: ERROR Invalid input busChromaU");
            return VP8ENC_INVALID_ARGUMENT;
        }
        /* fall through */
    case VP8ENC_YUV420_SEMIPLANAR:
    case VP8ENC_YUV420_SEMIPLANAR_VU:
        if(!VP8_BUS_ADDRESS_VALID(pEncIn->busChromaU))
        {
            APITRACE("VP8EncStrmEncode: ERROR Invalid input busChromaU");
            return VP8ENC_INVALID_ARGUMENT;
        }
        /* fall through */
    case VP8ENC_YUV422_INTERLEAVED_YUYV:
    case VP8ENC_YUV422_INTERLEAVED_UYVY:
    case VP8ENC_RGB565:
    case VP8ENC_BGR565:
    case VP8ENC_RGB555:
    case VP8ENC_BGR555:
    case VP8ENC_RGB444:
    case VP8ENC_BGR444:
    case VP8ENC_RGB888:
    case VP8ENC_BGR888:
    case VP8ENC_RGB101010:
    case VP8ENC_BGR101010:
        if(!VP8_BUS_ADDRESS_VALID(pEncIn->busLuma))
        {
            APITRACE("VP8EncStrmEncode: ERROR Invalid input busLuma");
            return VP8ENC_INVALID_ARGUMENT;
        }
        break;
    default:
        APITRACE("VP8EncStrmEncode: ERROR Invalid input format");
        return VP8ENC_INVALID_ARGUMENT;
    }

    if(pEncInst->preProcess.videoStab)
    {
        if(!VP8_BUS_ADDRESS_VALID(pEncIn->busLumaStab))
        {
            APITRACE("VP8EncStrmEncode: ERROR Invalid input busLumaStab");
            return VP8ENC_INVALID_ARGUMENT;
        }
    }

    /* update in/out buffers */
    regs->inputLumBase = pEncIn->busLuma;
    regs->inputCbBase = pEncIn->busChromaU;
    regs->inputCrBase = pEncIn->busChromaV;

    /* Try to reserve the HW resource */
    if(EWLReserveHw(pEncInst->asic.ewl) == EWL_ERROR)
    {
        APITRACE("VP8EncStrmEncode: ERROR HW unavailable");
        return VP8ENC_HW_RESERVED;
    }

    /* setup stabilization */
    if(pEncInst->preProcess.videoStab)
        regs->vsNextLumaBase = pEncIn->busLumaStab;

    /* Choose frame coding type */
    ct = pEncIn->codingType;

    /* Status may affect the frame coding type */
    if ((pEncInst->encStatus == VP8ENCSTAT_INIT) ||
        (pEncInst->encStatus == VP8ENCSTAT_KEYFRAME))
        ct = VP8ENC_INTRA_FRAME;

#ifdef VIDEOSTAB_ENABLED
    if(pEncInst->vsSwData.sceneChange)
    {
        pEncInst->vsSwData.sceneChange = 0;
        ct = VP8ENC_INTRA_FRAME;
    }
#endif

    if (SetStreamBuffer(pEncInst, pEncIn, ct) == ENCHW_NOK)
    {
        APITRACE("VP8EncStrmEncode: ERROR Invalid output buffer");
        return VP8ENC_INVALID_ARGUMENT;
    }

    /* TODO: RC can set frame to grf and copy grf to arf */
    if (pEncInst->rateControl.goldenPictureRate) {
        if ((pEncInst->frameCnt%pEncInst->rateControl.goldenPictureRate) == 0) {
            if(!pEncInst->rateControl.adaptiveGoldenUpdate ||
               ProcessStatistics(pEncInst, &boostPct))
                goldenRefresh = 1;
        }
    }
    /* Rate control */
    VP8BeforePicRc(&pEncInst->rateControl, pEncIn->timeIncrement,
                    (ct == VP8ENC_INTRA_FRAME),
                    ((pEncIn->grf&VP8ENC_REFRESH) ? 1 : 0) ||
                    goldenRefresh,
                    boostPct, pEncInst->layerId);

    /* Rate control may choose to skip the frame */
    if(pEncInst->rateControl.frameCoded == ENCHW_NO)
    {
        APITRACE("VP8EncStrmEncode: OK, frame skipped");
        EWLReleaseHw(pEncInst->asic.ewl);

        return VP8ENC_FRAME_READY;
    }

    /* Initialize picture buffer and ref pic list according to frame type */
    picBuffer = &pEncInst->picBuffer;
    picBuffer->cur_pic->show    = 1;
    picBuffer->cur_pic->poc     = pEncInst->frameCnt;
    picBuffer->cur_pic->i_frame = pEncInst->preProcess.intra
                                = (ct == VP8ENC_INTRA_FRAME);
    InitializePictureBuffer(picBuffer);

    /* Set picture buffer according to frame coding type */
    if (ct == VP8ENC_PREDICTED_FRAME) {
        picBuffer->cur_pic->p_frame = 1;
        picBuffer->cur_pic->arf = (pEncIn->arf&VP8ENC_REFRESH) ? 1 : 0;
        picBuffer->cur_pic->grf = ((pEncIn->grf&VP8ENC_REFRESH) ? 1 : 0 ) ||
            goldenRefresh;
        picBuffer->cur_pic->ipf = (pEncIn->ipf&VP8ENC_REFRESH) ? 1 : 0;
        picBuffer->refPicList[0].search = (pEncIn->ipf&VP8ENC_REFERENCE) ? 1 : 0;
        picBuffer->refPicList[1].search = (pEncIn->grf&VP8ENC_REFERENCE) ? 1 : 0;
        picBuffer->refPicList[2].search = (pEncIn->arf&VP8ENC_REFERENCE) ? 1 : 0;
    }

/*
    if (picBuffer->cur_pic->grf && (ct == VP8ENC_PREDICTED_FRAME))
        pEncInst->rateControl.qpHdr = CLIP3(pEncInst->rateControl.qpHdr -
                pEncInst->rateControl.goldenPictureBoost, 0, 127); */

    /* Set some frame coding parameters before internal test configure */
    VP8SetFrameParams(pEncInst);

#ifdef TRACE_STREAM
    /* Open stream tracing */
    if (H1EncTraceFileConfig && pEncInst->frameCnt == (u32)H1EncTraceFirstFrame)
        EncOpenStreamTrace("stream.trc");

    traceStream.frameNum = pEncInst->frameCnt;
    traceStream.id = 0; /* Stream generated by SW */
    traceStream.bitCnt = 0;  /* New frame */
#endif

#ifdef INTERNAL_TEST
    /* Configure the encoder instance according to the test vector */
    Vp8ConfigureTestBeforeFrame(pEncInst);
#endif

    /* update any cropping/rotation/filling */
    EncPreProcess(&pEncInst->asic, &pEncInst->preProcess);

    /* Get the reference frame buffers from picture buffer */
    PictureBufferSetRef(picBuffer, &pEncInst->asic);

    /* Code one frame */
    if( pEncInst->maxNumPasses == 1 )
        ret = VP8CodeFrame(pEncInst);
    else
        ret = VP8CodeFrameMultiPass(pEncInst);

#ifdef TRACE_RECON
    EncDumpRecon(&pEncInst->asic);
#endif

    if(ret != VP8ENCODE_OK)
    {
        /* Error has occured and the frame is invalid */
        VP8EncRet to_user;

        switch (ret)
        {
        case VP8ENCODE_TIMEOUT:
            APITRACE("VP8EncStrmEncode: ERROR HW/IRQ timeout");
            to_user = VP8ENC_HW_TIMEOUT;
            break;
        case VP8ENCODE_HW_RESET:
            APITRACE("VP8EncStrmEncode: ERROR HW reset detected");
            to_user = VP8ENC_HW_RESET;
            break;
        case VP8ENCODE_HW_ERROR:
            APITRACE("VP8EncStrmEncode: ERROR HW bus access error");
            to_user = VP8ENC_HW_BUS_ERROR;
            break;
        case VP8ENCODE_SYSTEM_ERROR:
        default:
            /* System error has occured, encoding can't continue */
            pEncInst->encStatus = VP8ENCSTAT_ERROR;
            APITRACE("VP8EncStrmEncode: ERROR Fatal system error");
            to_user = VP8ENC_SYSTEM_ERROR;
        }

        return to_user;
    }

#ifdef VIDEOSTAB_ENABLED
    /* Finalize video stabilization */
    if(pEncInst->preProcess.videoStab) {
        u32 no_motion;

        VSReadStabData(pEncInst->asic.regs.regMirror, &pEncInst->vsHwData);

        no_motion = VSAlgStabilize(&pEncInst->vsSwData, &pEncInst->vsHwData);
        if(no_motion) {
            VSAlgReset(&pEncInst->vsSwData);
        }

        /* update offset after stabilization */
        VSAlgGetResult(&pEncInst->vsSwData, &pEncInst->preProcess.horOffsetSrc,
                       &pEncInst->preProcess.verOffsetSrc);
    }
#endif

    pEncOut->motionVectors = (i8*)pEncInst->asic.mvOutput.virtualAddress;

    /* After stream buffer overflow discard the coded frame */
    if(VP8BufferOverflow(&pEncInst->buffer[1]) == ENCHW_NOK)
    {
        /* Frame discarded, reference frame corrupted, next must be intra. */
        pEncInst->encStatus = VP8ENCSTAT_KEYFRAME;
        pEncInst->prevFrameLost = 1;
        APITRACE("VP8EncStrmEncode: ERROR Output buffer too small");
        return VP8ENC_OUTPUT_BUFFER_OVERFLOW;
    }

    /* Store the stream size and frame coding type in output structure */
    pEncOut->pOutBuf[0] = (u32*)pEncInst->buffer[0].pData;
    pEncOut->streamSize[0] = pEncInst->buffer[0].byteCnt +
                             pEncInst->buffer[1].byteCnt;
    for (i = 1; i < pEncInst->sps.partitionCnt; i++)
    {
        pEncOut->pOutBuf[i] = (u32*)pEncInst->buffer[i+1].pData;
        pEncOut->streamSize[i] = pEncInst->buffer[i+1].byteCnt;
    }

    /* Total frame size */
    pEncOut->frameSize = 0;
    for (i = 0; i < 9; i++)
        pEncOut->frameSize += pEncOut->streamSize[i];

    /* Rate control action after frame */
    VP8AfterPicRc(&pEncInst->rateControl, pEncOut->frameSize, pEncInst->layerId);

    if(picBuffer->cur_pic->i_frame) {
        pEncOut->codingType = VP8ENC_INTRA_FRAME;
        pEncOut->arf = pEncOut->grf = pEncOut->ipf = 0;
    } else {
        pEncOut->codingType = VP8ENC_PREDICTED_FRAME;
        pEncOut->ipf = picBuffer->refPicList[0].search ? VP8ENC_REFERENCE : 0;
        pEncOut->grf = picBuffer->refPicList[1].search ? VP8ENC_REFERENCE : 0;
        pEncOut->arf = picBuffer->refPicList[2].search ? VP8ENC_REFERENCE : 0;
    }

    /* Mark which reference frame was refreshed */
    pEncOut->arf |= picBuffer->cur_pic->arf ? VP8ENC_REFRESH : 0;
    pEncOut->grf |= picBuffer->cur_pic->grf ? VP8ENC_REFRESH : 0;
    pEncOut->ipf |= picBuffer->cur_pic->ipf ? VP8ENC_REFRESH : 0;
    pEncOut->mse_mul256 = regs->mse_mul256;

    UpdatePictureBuffer(picBuffer);

    /* Frame was encoded so increment frame number */
    pEncInst->frameCnt++;
    pEncInst->encStatus = VP8ENCSTAT_START_FRAME;
    pEncInst->prevFrameLost = 0;

    APITRACE("VP8EncStrmEncode: OK");
    return VP8ENC_FRAME_READY;
}


/*------------------------------------------------------------------------------
    Function name : VP8EncGetBitsPerPixel
    Description   : Returns the amount of bits per pixel for given format.
    Return type   : u32
------------------------------------------------------------------------------*/
u32 VP8EncGetBitsPerPixel(VP8EncPictureType type)
{
    switch (type)
    {
        case VP8ENC_YUV420_PLANAR:
        case VP8ENC_YUV420_SEMIPLANAR:
        case VP8ENC_YUV420_SEMIPLANAR_VU:
            return 12;
        case VP8ENC_YUV422_INTERLEAVED_YUYV:
        case VP8ENC_YUV422_INTERLEAVED_UYVY:
        case VP8ENC_RGB565:
        case VP8ENC_BGR565:
        case VP8ENC_RGB555:
        case VP8ENC_BGR555:
        case VP8ENC_RGB444:
        case VP8ENC_BGR444:
            return 16;
        case VP8ENC_RGB888:
        case VP8ENC_BGR888:
        case VP8ENC_RGB101010:
        case VP8ENC_BGR101010:
            return 32;
        default:
            return 0;
    }
}

/*------------------------------------------------------------------------------
    Function name : VP8EncSetTestId
    Description   : Sets the encoder configuration according to a test vector
    Return type   : VP8EncRet
    Argument      : inst - encoder instance
    Argument      : testId - test vector ID
------------------------------------------------------------------------------*/
VP8EncRet VP8EncSetTestId(VP8EncInst inst, u32 testId)
{
    vp8Instance_s *pEncInst = (vp8Instance_s *) inst;
    (void) pEncInst;
    (void) testId;

    APITRACE("VP8EncSetTestId#");
    APITRACEPARAM("testId", testId);

#ifdef INTERNAL_TEST
    pEncInst->testId = testId;

    APITRACE("VP8EncSetTestId# OK");
    return VP8ENC_OK;
#else
    /* Software compiled without testing support, return error always */
    APITRACE("VP8EncSetTestId# ERROR, testing disabled at compile time");
    return VP8ENC_ERROR;
#endif
}

/*------------------------------------------------------------------------------
    Function name : VP8EncGetMbInfo
    Description   : Set the motionVectors field of VP8EncOut structure to
                    point macroblock mbNum
    Return type   : VP8EncRet
    Argument      : inst - encoder instance
    Argument      : mbNum - macroblock number
------------------------------------------------------------------------------*/
VP8EncRet VP8EncGetMbInfo(VP8EncInst inst, VP8EncOut * pEncOut, u32 mbNum)
{
	vp8Instance_s *pEncInst = (vp8Instance_s *) inst;

	APITRACE("VP8EncSetTestId#");

	/* Check for illegal inputs */
	if (!pEncInst || !pEncOut) {
		APITRACE("VP8EncSetTestId: ERROR Null argument");
		return VP8ENC_NULL_ARGUMENT;
	}

	if (mbNum >= pEncInst->mbPerFrame) {
		APITRACE("VP8EncSetTestId: ERROR invalid argument");
		return VP8ENC_INVALID_ARGUMENT;
	}

	pEncOut->motionVectors = (i8 *)EncAsicGetMvOutput(&pEncInst->asic, mbNum);

	return VP8ENC_OK;
}
