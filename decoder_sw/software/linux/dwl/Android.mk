LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    dwl_linux.c \
    dwl_linux_hw.c \
    dwl_activity_trace.c \
    dwl_buf_protect.c \

LOCAL_SRC_FILES += \
    dwl_linux_sc.c \
        
LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)


LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)
 
LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_C_INCLUDES += \
	$(LINUX_KERNEL_ROOT)/include/uapi

LOCAL_MODULE:= lib_imx_vsi_dwl
LOCAL_MODULE_TAGS := eng

LOCAL_SHARED_LIBRARY = pthread
include $(BUILD_STATIC_LIBRARY)

