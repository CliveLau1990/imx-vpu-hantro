LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    vc1decapi.c \
    vc1hwd_asic.c \
    vc1hwd_bitplane.c \
    vc1hwd_decoder.c \
    vc1hwd_picture_layer.c \
    vc1hwd_stream.c \
    vc1hwd_vlc.c \
    vc1hwd_headers.c \
    ../common/tiledref.c \
    ../common/regdrv_g1.c \
    ../common/refbuffer.c \
    ../common/bqueue.c \
    ../common/errorhandling.c \
    ../common/commonconfig_g1.c \
    ../common/input_queue.c \
    ../common/fifo.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)



LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -UCLEAR_HDRINFO_IN_SEEK
LOCAL_CFLAGS += -DFIFO_DATATYPE=addr_t

LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += ../common

LOCAL_MODULE:= lib_imx_vsi_vc1
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

