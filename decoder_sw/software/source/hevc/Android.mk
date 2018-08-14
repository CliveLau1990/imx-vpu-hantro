LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    hevc_asic.c \
    hevc_byte_stream.c \
    hevcdecapi.c \
    hevc_decoder.c \
    hevc_dpb.c \
    hevc_fb_mngr.c \
    hevc_nal_unit.c \
    hevc_pic_order_cnt.c \
    hevc_pic_param_set.c \
    hevc_seq_param_set.c \
    hevc_slice_header.c \
    hevc_storage.c \
    hevc_util.c \
    hevc_exp_golomb.c \
    hevc_vui.c \
    hevc_sei.c \
    hevc_video_param_set.c \

        
LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)


LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)
 
LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_MODULE:= lib_imx_vsi_hevc
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

