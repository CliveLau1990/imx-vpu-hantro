LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    avs_headers.c \
    avs_strm.c \
    avs_utils.c \
    avs_vlc.c \
    avsdecapi.c \
    avsdecapi_internal.c \
	../common/regdrv_g1.c \
	../common/refbuffer.c \
	../common/tiledref.c \
	../common/bqueue.c \
	../common/errorhandling.c \
	../common/commonconfig_g1.c \
	../common/input_queue.c \
	../common/fifo.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)

LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK
LOCAL_CFLAGS += -DFIFO_DATATYPE=addr_t
LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += ../common

LOCAL_MODULE:= lib_imx_vsi_avs
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

