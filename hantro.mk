LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_SRC_FILES := \
    decoder_sw/software/source/hevc/hevc_asic.c \
    decoder_sw/software/source/hevc/hevc_byte_stream.c \
    decoder_sw/software/source/hevc/hevcdecapi.c \
    decoder_sw/software/source/hevc/hevc_decoder.c \
    decoder_sw/software/source/hevc/hevc_dpb.c \
    decoder_sw/software/source/hevc/hevc_fb_mngr.c \
    decoder_sw/software/source/hevc/hevc_nal_unit.c \
    decoder_sw/software/source/hevc/hevc_pic_order_cnt.c \
    decoder_sw/software/source/hevc/hevc_pic_param_set.c \
    decoder_sw/software/source/hevc/hevc_seq_param_set.c \
    decoder_sw/software/source/hevc/hevc_slice_header.c \
    decoder_sw/software/source/hevc/hevc_storage.c \
    decoder_sw/software/source/hevc/hevc_util.c \
    decoder_sw/software/source/hevc/hevc_exp_golomb.c \
    decoder_sw/software/source/hevc/hevc_vui.c \
    decoder_sw/software/source/hevc/hevc_sei.c \
    decoder_sw/software/source/hevc/hevc_video_param_set.c \

LOCAL_SRC_FILES += \
    decoder_sw/software/source/vp9/vp9decapi.c \
    decoder_sw/software/source/vp9/vp9hwd_asic.c \
    decoder_sw/software/source/vp9/vp9hwd_bool.c \
    decoder_sw/software/source/vp9/vp9hwd_buffer_queue.c \
    decoder_sw/software/source/vp9/vp9hwd_decoder.c \
    decoder_sw/software/source/vp9/vp9hwd_headers.c \
    decoder_sw/software/source/vp9/vp9hwd_output.c \
    decoder_sw/software/source/vp9/vp9hwd_probs.c \
    decoder_sw/software/source/vp9/vp9_modecontext.c \
    decoder_sw/software/source/vp9/vp9_entropymode.c \
    decoder_sw/software/source/vp9/vp9_entropymv.c \
    decoder_sw/software/source/vp9/vp9_treecoder.c \
    decoder_sw/software/source/vp9/vp9_modecont.c
	
LOCAL_SRC_FILES += \
    decoder_sw/software/linux/dwl/dwl_linux.c \
    decoder_sw/software/linux/dwl/dwl_linux_hw.c \
    decoder_sw/software/linux/dwl/dwl_activity_trace.c \
    decoder_sw/software/linux/dwl/dwl_buf_protect.c \

LOCAL_SRC_FILES += \
    decoder_sw/software/source/common/bqueue.c \
    decoder_sw/software/source/common/commonconfig.c \
    decoder_sw/software/source/common/fifo.c \
    decoder_sw/software/source/common/raster_buffer_mgr.c \
    decoder_sw/software/source/common/regdrv.c \
    decoder_sw/software/source/common/sw_stream.c \
    decoder_sw/software/source/common/input_queue.c \
    decoder_sw/software/source/common/sw_util.c \
    decoder_sw/software/source/common/stream_corrupt.c

LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)
LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV
LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_C_INCLUDES += device/fsl/common/kernel-headers \
	$(LOCAL_PATH)/decoder_sw

ifeq ($(ENABLE_HANTRO_DEBUG_LOG), true)
LOCAL_SHARED_LIBRARIES := liblog libcutils
endif

LOCAL_MODULE:= libhantro
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_SHARED_LIBRARY)
