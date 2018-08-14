LOCAL_PATH := $(call my-dir)
DECODER_RELEASE := ../../../g1_decoder/software
SYSTEM_MODEL := ../../../g1_decoder/system/models/golden

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := lib8170hw
LOCAL_MODULE_TAGS := optional

INCLUDES := $(LOCAL_PATH)/$(DECODER_RELEASE)
INCLUDES_MODEL := $(LOCAL_PATH)/$(SYSTEM_MODEL)
MODEL_PATH := $(SYSTEM_MODEL)

LOCAL_SRC_FILES := \
    $(MODEL_PATH)/asic.c \
	$(MODEL_PATH)/streamd.c \
	$(MODEL_PATH)/mvd.c \
	$(MODEL_PATH)/scd.c \
	$(MODEL_PATH)/acdcd.c \
	$(MODEL_PATH)/transd.c \
	$(MODEL_PATH)/inter_pred.c \
	$(MODEL_PATH)/intra_prediction.c \
	$(MODEL_PATH)/pred.c \
	$(MODEL_PATH)/bsd.c \
	$(MODEL_PATH)/filterd.c \
	$(MODEL_PATH)/bus_ifd.c \
	$(MODEL_PATH)/util.c \
	$(MODEL_PATH)/asic_trace.c \
	$(MODEL_PATH)/ref_bufferd.c \
    $(MODEL_PATH)/pp.c \
	$(MODEL_PATH)/pp_internal/cfg.c \
	$(MODEL_PATH)/pp_internal/frame.c \
	$(MODEL_PATH)/pp_internal/scaling.c \
	$(MODEL_PATH)/pp_internal/pp_trace.c \
    $(MODEL_PATH)/intrap_internal/intra_4x4_prediction.c \
	$(MODEL_PATH)/intrap_internal/intra_8x8_prediction.c \
	$(MODEL_PATH)/intrap_internal/intra_16x16_prediction.c \
	$(MODEL_PATH)/intrap_internal/intra_chroma_prediction.c \
	$(MODEL_PATH)/intrap_internal/avs_intra_prediction.c \
	$(MODEL_PATH)/intrap_internal/hukka_intra_prediction.c \
	$(MODEL_PATH)/intrap_internal/vp78_intra_prediction.c \
    $(MODEL_PATH)/mvd_internal/neighbour.c \
	$(MODEL_PATH)/mvd_internal/h264_mvd.c \
	$(MODEL_PATH)/mvd_internal/avs_mvd.c \
	$(MODEL_PATH)/mvd_internal/hukka_mvd.c \
	$(MODEL_PATH)/mvd_internal/vp78_mvd.c \
    $(MODEL_PATH)/streamd_internal/exp_golomb.c \
	$(MODEL_PATH)/streamd_internal/mpeg4_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/mpeg4_vlc.c \
	$(MODEL_PATH)/streamd_internal/residual_block.c \
	$(MODEL_PATH)/streamd_internal/rlc_mode.c \
	$(MODEL_PATH)/streamd_internal/slice_header.c \
	$(MODEL_PATH)/streamd_internal/stream.c \
	$(MODEL_PATH)/streamd_internal/streamd_util.c \
	$(MODEL_PATH)/streamd_internal/vc1_bitplane.c \
	$(MODEL_PATH)/streamd_internal/vc1_block_vld.c \
	$(MODEL_PATH)/streamd_internal/vc1_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/vc1_vlc.c \
	$(MODEL_PATH)/streamd_internal/jpeg_scan.c \
	$(MODEL_PATH)/streamd_internal/mpeg2_vlc.c \
	$(MODEL_PATH)/streamd_internal/mpeg2_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/h264_slice_header.c \
	$(MODEL_PATH)/streamd_internal/h264_slice_data.c \
	$(MODEL_PATH)/streamd_internal/h264_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/h264_residual.c \
	$(MODEL_PATH)/streamd_internal/h264_residual_cabac.c \
	$(MODEL_PATH)/streamd_internal/h264_residual_cavlc.c \
	$(MODEL_PATH)/streamd_internal/cabac.c \
	$(MODEL_PATH)/streamd_internal/avs_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/avs_rlc.c \
	$(MODEL_PATH)/streamd_internal/susi3_vlc.c \
	$(MODEL_PATH)/streamd_internal/susi3_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/hukka_slice_header.c \
	$(MODEL_PATH)/streamd_internal/hukka8_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/hukka9_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/hukka_intra_pred_modes.c \
	$(MODEL_PATH)/streamd_internal/hukka8_vlc.c \
	$(MODEL_PATH)/streamd_internal/vp78_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/vp78_tokens.c \
	$(MODEL_PATH)/streamd_internal/vp_boolcoder.c \
	$(MODEL_PATH)/streamd_internal/vp6_macroblock_layer.c \
	$(MODEL_PATH)/streamd_internal/vp6_tokens.c

LOCAL_C_INCLUDES := \
    $(INCLUDES)/source/inc \
    $(INCLUDES)/source/common \
    $(INCLUDES)/test/common/swhw \
    $(INCLUDES_MODEL) \
    $(INCLUDES_MODEL)/mvd_internal \
    $(INCLUDES_MODEL)/streamd_internal \
    $(INCLUDES_MODEL)/bsd_internal \
    $(INCLUDES_MODEL)/intrap_internal \
    $(INCLUDES_MODEL)/pp_internal

#LOCAL_CFLAGS := -fno-exceptions

include $(BUILD_STATIC_LIBRARY)

####################################################

