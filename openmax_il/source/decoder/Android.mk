#jlk set arm7-a in /build/core/combo/linux-arm.mk
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
HANTRO_LIBS := ../../../libs
HANTRO_TOP := $(VSI_TOP)

ifeq ($(USE_LIBS),true)
LOCAL_PREBUILT_LIBS := \
    $(HANTRO_LIBS)/libdwlx170.a \
    $(HANTRO_LIBS)/libdecx170m.a \
    $(HANTRO_LIBS)/libdecx170h.a \
    $(HANTRO_LIBS)/libdecx170vp8.a \
    $(HANTRO_LIBS)/libdecx170p.a \
    $(HANTRO_LIBS)/lib8170hw.a \
    $(HANTRO_LIBS)/libtbcommon.a

include $(BUILD_MULTI_PREBUILT)
else
include $(LOCAL_PATH)/declibs.mk
include $(LOCAL_PATH)/model.mk
endif

include $(CLEAR_VARS)
LOCAL_ARM_MODE := arm
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libOMX.hantro.G1.video.decoder

#BELLAGIO_ROOT := external/hantro/libomxil-bellagio-0.9.2
DECODER_RELEASE := $(VSI_TOP)/g1_decoder/software

LOCAL_SRC_FILES := \
    ../msgque.c \
    ../OSAL.c \
    ../basecomp.c \
    ../port.c \
    ../util.c \
    codec_h264.c \
    codec_mpeg4.c \
    codec_vp8.c \
    codec_pp.c \
    post_processor.c \
    decoder.c

#    ../nativebuffer.cpp
#    library_entry_point.c

LOCAL_STATIC_LIBRARIES := \
    libdecx170m \
    libdecx170h \
    libdecx170vp8 \
    libdwlx170 \
    libdecx170p \
lib8170hw \
libtbcommon

LOCAL_SHARED_LIBRARIES += liblog
LOCAL_SHARED_LIBRARIES += libcutils

LOCAL_CFLAGS := \
    -DENABLE_CODEC_H264 \
    -DENABLE_CODEC_H263 \
    -DENABLE_CODEC_MPEG4 \
    -DENABLE_CODEC_VP8 \
    -DIS_G1_DECODER \
    -DENABLE_DBGT_TRACE \
    -DDBGT_CONFIG_AUTOVAR \
    -DOMX_DECODER_VIDEO_DOMAIN \
    -DINCLUDE_TB \
    -DSET_OUTPUT_CROP_RECT

#    -DDISABLE_OUTPUT_SCALING
#    -DUSE_ANDROID_NATIVE_BUFFER
#    -DENABLE_PP

LOCAL_LDLIBS += -lpthread

LOCAL_C_INCLUDES := \
    . \
    $(HANTRO_TOP)/openmax_il/source \
    $(HANTRO_TOP)/openmax_il/headers \
    $(DECODER_RELEASE)/source/inc \
	$(DECODER_RELEASE)/source/h264high \
    $(DECODER_RELEASE)/source/h264high/legacy \
	$(DECODER_RELEASE)/source/mpeg4 \
	$(DECODER_RELEASE)/source/vp8 \
    $(DECODER_RELEASE)/source/common \
    $(DECODER_RELEASE)/source/config \
    $(DECODER_RELEASE)/source/pp \
    $(DECODER_RELEASE)/test/common \
    $(DECODER_RELEASE)/test/common/swhw \
    $(DECODER_RELEASE)/linux/memalloc \
    $(DECODER_RELEASE)/linux/mpeg4 \
    $(DECODER_RELEASE)/linux/vp8 \
    $(DECODER_RELEASE)/linux/dwl

#    $(BELLAGIO_ROOT)/src \

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)

include $(BUILD_SHARED_LIBRARY)

