LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_SRC_FILES := \
    decoder_sw/software/linux/dwl/dwl_linux.c \
    decoder_sw/software/linux/dwl/dwl_linux_sc.c \
    decoder_sw/software/linux/dwl/dwl_activity_trace.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)


LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_LDFLAGS += -shared -nostartfiles -Wl,-Bsymbolic -Wl,-z -Wl,muldefs
LOCAL_LDFLAGS += $(IMX_VPU_G1_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += device/fsl/common/kernel-headers

LOCAL_C_INCLUDES += $(LOCAL_PATH)/legacy \
	$(LOCAL_PATH)/decoder_sw/software/source/avs \
	$(LOCAL_PATH)/decoder_sw/software/source/h264high \
	$(LOCAL_PATH)/decoder_sw/software/source/h264high/legacy \
	$(LOCAL_PATH)/decoder_sw/software/source/jpeg \
	$(LOCAL_PATH)/decoder_sw/software/source/mpeg2 \
	$(LOCAL_PATH)/decoder_sw/software/linux/mpeg2 \
	$(LOCAL_PATH)/decoder_sw/software/linux/mpeg4 \
	$(LOCAL_PATH)/decoder_sw/software/linux/rv \

LOCAL_WHOLE_STATIC_LIBRARIES := lib_imx_vsi_avs lib_imx_vsi_h264 lib_imx_vsi_jpeg \
	lib_imx_vsi_mpeg2 lib_imx_vsi_mpeg4 lib_imx_vsi_rv lib_imx_vsi_vp6 lib_imx_vsi_vp8 lib_imx_vsi_vc1

ifeq ($(ENABLE_HANTRO_DEBUG_LOG), true)
LOCAL_SHARED_LIBRARIES := liblog libcutils
endif

LOCAL_MODULE:= libg1
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)


