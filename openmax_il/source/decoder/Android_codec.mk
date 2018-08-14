LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    codec_hevc.c \
    codec_vp9.c \
    codec_h264.c \
    codec_avs.c \
    codec_jpeg.c \
    codec_mpeg2.c \
    codec_mpeg4.c \
    codec_rv.c \
    codec_vc1.c \
    codec_vp8.c \
    codec_webp.c \
	codec_vp6.c \
    ../OSAL.c \
	test/queue.c

        
LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += -DSET_OUTPUT_CROP_RECT -DUSE_EXTERNAL_BUFFER -DUSE_OUTPUT_RELEASE -DVSI_API -DIS_G1_DECODER -DENABLE_CODEC_VP8 -DVP8_HWTIMEOUT_WORKAROUND -DENABLE_CODEC_MJPEG -DGET_FREE_BUFFER_NON_BLOCK -DDOWN_SCALER


LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)
LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += ../

LOCAL_SHARED_LIBRARIES  += libhantro
LOCAL_SHARED_LIBRARIES  += liblog
LOCAL_SHARED_LIBRARIES  += libutils
LOCAL_SHARED_LIBRARIES  += libhantro libg1
LOCAL_MODULE:= libcodec
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)

