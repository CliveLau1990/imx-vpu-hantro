LOCAL_PATH := $(call my-dir)
DECODER_RELEASE := ../../../g1_decoder/software
SYSTEM_MODEL := ../../../g1_decoder/system/models/golden

############################################################
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := libdecx170m
LOCAL_MODULE_TAGS := optional

#DECODER_RELEASE := ../../../g1_decoder/software
INCLUDES := $(LOCAL_PATH)/$(DECODER_RELEASE)

LOCAL_SRC_FILES := \
    $(DECODER_RELEASE)/source/mpeg4/mp4dechwd_error_conceal.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4decapi.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4decapi_internal.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_rvlc.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_strmdec.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_headers.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_motiontexture.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_shortvideo.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_utils.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_videopacket.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_vlc.c \
	$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_vop.c \
	$(DECODER_RELEASE)/source/common/regdrv.c \
	$(DECODER_RELEASE)/source/common/tiledref.c \
	$(DECODER_RELEASE)/source/common/refbuffer.c \
	$(DECODER_RELEASE)/source/common/workaround.c \
	$(DECODER_RELEASE)/source/common/bqueue.c

ifeq ($(CUSTOM_FMT_SUPPORT),y)
	LOCAL_SRC_FILES += \
		$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_custom.c
else
	LOCAL_SRC_FILES += \
		$(DECODER_RELEASE)/source/mpeg4/mp4dechwd_generic.c
endif

ifeq ($(123b),y)
	LOCAL_SRC_FILES += \
	$(DECODER_RELEASE)/source/common/tiledref.c
endif

ifeq ($(168b),y)
	LOCAL_SRC_FILES += \
	$(DECODER_RELEASE)/source/common/commonconfig.c
endif

LOCAL_C_INCLUDES := \
    $(INCLUDES)/source/inc \
    $(INCLUDES)/source/common \
    $(INCLUDES)/source/config \
    $(INCLUDES)/source/mpeg4 \
    $(INCLUDES)/linux/mpeg4

LOCAL_CFLAGS := \
    -DDEC_X170_OUTPUT_FORMAT=DEC_X170_OUTPUT_FORMAT_RASTER_SCAN \
    -D_MP4_RLC_BUFFER_SIZE=384
    
include $(BUILD_STATIC_LIBRARY)

############################################################
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := libdecx170h
LOCAL_MODULE_TAGS := optional

INCLUDES := $(LOCAL_PATH)/$(DECODER_RELEASE)

LOCAL_SRC_FILES := \
    $(DECODER_RELEASE)/source/h264high/h264decapi.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_asic.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_intra_prediction.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_inter_prediction.c \
    $(DECODER_RELEASE)/source/h264high/legacy/h264hwd_util.c \
    $(DECODER_RELEASE)/source/h264high/legacy/h264hwd_byte_stream.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_seq_param_set.c \
    $(DECODER_RELEASE)/source/h264high/legacy/h264hwd_pic_param_set.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_slice_header.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_slice_data.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_macroblock_layer.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_stream.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_vlc.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_cavlc.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_nal_unit.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_neighbour.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_storage.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_slice_group_map.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_dpb.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_vui.c \
	$(DECODER_RELEASE)/source/h264high/legacy/h264hwd_pic_order_cnt.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_decoder.c \
	$(DECODER_RELEASE)/source/h264high/h264hwd_conceal.c \
    $(DECODER_RELEASE)/source/h264high/h264_pp_pipeline.c \
    $(DECODER_RELEASE)/source/h264high/h264hwd_cabac.c \
    $(DECODER_RELEASE)/source/h264high/h264decapi_e.c \
	$(DECODER_RELEASE)/source/common/regdrv.c \
	$(DECODER_RELEASE)/source/common/tiledref.c \
	$(DECODER_RELEASE)/source/common/refbuffer.c \
	$(DECODER_RELEASE)/source/common/workaround.c

ifeq ($(123b),y)
	LOCAL_SRC_FILES += \
	$(DECODER_RELEASE)/source/common/tiledref.c
endif

ifeq ($(168b),y)
	LOCAL_SRC_FILES += \
	$(DECODER_RELEASE)/source/common/commonconfig.c
endif

LOCAL_C_INCLUDES := \
    $(INCLUDES)/source/inc \
    $(INCLUDES)/source/common \
    $(INCLUDES)/source/config \
    $(INCLUDES)/source/h264high \
    $(INCLUDES)/source/h264high/legacy \
    $(INCLUDES)/linux/h264high

#    $(SYSMODEL)

LOCAL_CFLAGS := \
    -DDEC_X170_OUTPUT_FORMAT=DEC_X170_OUTPUT_FORMAT_RASTER_SCAN

include $(BUILD_STATIC_LIBRARY)

############################################################
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := libdecx170vp8
LOCAL_MODULE_TAGS := optional

INCLUDES := $(LOCAL_PATH)/$(DECODER_RELEASE)

LOCAL_SRC_FILES := \
    $(DECODER_RELEASE)/source/vp8/vp8decapi.c \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_bool.c \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_probs.c \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_headers.c \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_decoder.c \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_pp_pipeline.c \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_asic.c \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_error.c \
	$(DECODER_RELEASE)/source/common/regdrv.c \
	$(DECODER_RELEASE)/source/common/tiledref.c \
	$(DECODER_RELEASE)/source/common/refbuffer.c \
	$(DECODER_RELEASE)/source/common/bqueue.c

ifeq ($(123b),y)
	LOCAL_SRC_FILES += \
	$(DECODER_RELEASE)/source/vp8/vp8hwd_error.c \
	$(DECODER_RELEASE)/source/common/tiledref.c
endif

ifeq ($(168b),y)
	LOCAL_SRC_FILES += \
	$(DECODER_RELEASE)/source/common/commonconfig.c
endif

LOCAL_C_INCLUDES := \
    $(INCLUDES)/source/inc \
    $(INCLUDES)/source/common \
    $(INCLUDES)/source/config \
    $(INCLUDES)/source/vp8

LOCAL_CFLAGS := \
    -DDEC_X170_OUTPUT_FORMAT=DEC_X170_OUTPUT_FORMAT_RASTER_SCAN

include $(BUILD_STATIC_LIBRARY)
############################################################
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := libdecx170p
LOCAL_MODULE_TAGS := optional

INCLUDES := $(LOCAL_PATH)/$(DECODER_RELEASE)

LOCAL_SRC_FILES := \
    $(DECODER_RELEASE)/source/pp/ppapi.c \
    $(DECODER_RELEASE)/source/pp/ppinternal.c \
    $(DECODER_RELEASE)/source/common/regdrv.c

LOCAL_C_INCLUDES := \
    $(INCLUDES)/source/inc \
    $(INCLUDES)/source/common \
    $(INCLUDES)/source/config \
    $(INCLUDES)/linux/pp \
    $(INCLUDES)/source/h264high \
    $(INCLUDES)/source/mpeg4 \
    $(INCLUDES)/source/vp8

LOCAL_CFLAGS := \

#    -DPP_PIPELINE_ENABLED \
#    -DPP_MPEG4DEC_PIPELINE_SUPPORT \
#    -DPP_H264DEC_PIPELINE_SUPPORT \
#    -DPP_VP8DEC_PIPELINE_SUPPORT

include $(BUILD_STATIC_LIBRARY)

############################################################
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := libtbcommon
LOCAL_MODULE_TAGS := optional

INCLUDES := $(LOCAL_PATH)/$(DECODER_RELEASE)
INCLUDES_MODEL := $(LOCAL_PATH)/$(SYSTEM_MODEL)
MODEL_PATH := $(SYSTEM_MODEL)

LOCAL_SRC_FILES := \
    $(DECODER_RELEASE)/test/common/swhw/tb_stream_corrupt.c \
    $(DECODER_RELEASE)/test/common/swhw/tb_params.c \
    $(DECODER_RELEASE)/test/common/swhw/tb_cfg.c \
    $(DECODER_RELEASE)/test/common/swhw/tb_md5.c \
    $(DECODER_RELEASE)/test/common/swhw/md5.c \
    $(DECODER_RELEASE)/test/common/swhw/tb_tiled.c \
    $(DECODER_RELEASE)/test/common/swhw/trace.c

LOCAL_C_INCLUDES := \
    $(INCLUDES)/source/inc \
    $(INCLUDES)/source/common \
    $(INCLUDES)/source/config \

LOCAL_CFLAGS := \

include $(BUILD_STATIC_LIBRARY)

############################################################
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := libdwlx170
LOCAL_MODULE_TAGS := optional

INCLUDES := $(LOCAL_PATH)/$(DECODER_RELEASE)
MODEL := $(LOCAL_PATH)/$(SYSTEM_MODEL)

LOCAL_SRC_FILES := \
    $(DECODER_RELEASE)/linux/dwl/dwl_x170_pc.c

LOCAL_C_INCLUDES := \
    $(INCLUDES)/source/inc \
    $(INCLUDES)/source/common \
    $(MODEL)

LOCAL_CFLAGS := \
    -D_DWL_PCLINUX

include $(BUILD_STATIC_LIBRARY)

############################################################
