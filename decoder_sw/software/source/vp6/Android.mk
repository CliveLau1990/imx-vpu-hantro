LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    vp6booldec.c \
    vp6dec.c \
    vp6huffdec.c \
    vp6hwd_api.c \
    vp6hwd_asic.c \
    vp6strmbuffer.c \
    vp6decodemode.c \
    vp6decodemv.c \
    vp6scanorder.c \
    vp6gconst.c \
    vp6_pp_pipeline.c \
    ../common/tiledref.c \
    ../common/regdrv_g1.c \
    ../common/refbuffer.c \
    ../common/bqueue.c \
    ../common/errorhandling.c \
    ../common/commonconfig_g1.c \
    ../common/fifo.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)



LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -UCLEAR_HDRINFO_IN_SEEK
LOCAL_CFLAGS += -DFIFO_DATATYPE=addr_t

LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += ../common

LOCAL_MODULE:= lib_imx_vsi_vp6
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

