LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    vp8decapi.c \
    vp8decmcapi.c \
    vp8hwd_bool.c \
    vp8hwd_buffer_queue.c \
    vp8hwd_probs.c \
    vp8hwd_headers.c \
    vp8hwd_decoder.c \
    vp8hwd_pp_pipeline.c \
    vp8hwd_asic.c \
    vp8hwd_error.c \
    ../common/regdrv_g1.c \
    ../common/refbuffer.c \
    ../common/tiledref.c \
    ../common/bqueue.c \
    ../common/errorhandling.c \
    ../common/commonconfig_g1.c \
    ../common/fifo.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)



LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK
LOCAL_CFLAGS +=  -DFIFO_DATATYPE=addr_t
LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += ../common

LOCAL_MODULE:= lib_imx_vsi_vp8
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

