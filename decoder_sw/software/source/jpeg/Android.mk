LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    jpegdecapi.c \
    jpegdechdrs.c \
    jpegdecinternal.c \
    jpegdecscan.c \
    jpegdecutils.c \
	../common/regdrv_g1.c \
    ../common/commonconfig_g1.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)



LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK

LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += ../common

LOCAL_MODULE:= lib_imx_vsi_jpeg
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

