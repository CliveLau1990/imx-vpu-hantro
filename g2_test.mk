LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_SRC_FILES := \
			decoder_sw/software/source/common/decapi.c  \
			decoder_sw/software/test/common/dectestbench.c \
			decoder_sw/software/test/common/bytestream_parser.c \
			decoder_sw/software/test/common/command_line_parser.c \
			decoder_sw/software/test/common/error_simulator.c \
			decoder_sw/software/test/common/file_sink.c \
			decoder_sw/software/test/common/md5_sink.c \
			decoder_sw/software/test/common/null_sink.c \
			decoder_sw/software/test/common/vpxfilereader.c \
			decoder_sw/software/test/common/yuvfilters.c \
			decoder_sw/software/test/common/null_trace.c


LOCAL_SRC_FILES += decoder_sw/software/test/common/swhw/tb_stream_corrupt.c \
           decoder_sw/software/test/common/swhw/tb_params.c \
           decoder_sw/software/test/common/swhw/tb_cfg.c \
           decoder_sw/software/test/common/swhw/tb_md5.c \
           decoder_sw/software/test/common/swhw/md5.c \
           decoder_sw/software/test/common/swhw/tb_tiled.c

LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)
LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)


LOCAL_CFLAGS += -D_SW_DEBUG_PRINT -DTB_PP
LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += device/fsl/common/kernel-headers
LOCAL_C_INCLUDES += $(LOCAL_PATH)/decoder_sw \
	$(LOCAL_PATH)/decoder_sw/software/test/common \
	$(LOCAL_PATH)/decoder_sw/software/test/common/swhw

LOCAL_SHARED_LIBRARIES := libhantro
LOCAL_32_BIT_ONLY := true
LOCAL_MODULE:= g2dec_test
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
