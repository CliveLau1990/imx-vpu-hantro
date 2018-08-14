LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_SRC_FILES := decoder_sw/software/test/common/swhw/tb_stream_corrupt.c \
           decoder_sw/software/test/common/swhw/tb_params_g1.c \
           decoder_sw/software/test/common/swhw/tb_cfg.c \
           decoder_sw/software/test/common/swhw/tb_md5.c \
           decoder_sw/software/test/common/swhw/md5.c \
           decoder_sw/software/test/common/swhw/tb_tiled.c \
		   decoder_sw/software/test/mpeg4/mpeg4dectest.c \
		   decoder_sw/software/test/mpeg4/rm_length.c
		   


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)
LOCAL_LDFLAGS += $(IMX_VPU_G1_LDFLAGS)

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -D_MP4_RLC_BUFFER_SIZE=384
LOCAL_CFLAGS += -DFIFO_DATATYPE=addr_t

LOCAL_CFLAGS += -D_SW_DEBUG_PRINT -DTB_PP
LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += device/fsl/common/kernel-headers
LOCAL_C_INCLUDES += $(LOCAL_PATH)/decoder_sw \
	$(LOCAL_PATH)/decoder_sw/software/test/common \
	$(LOCAL_PATH)/decoder_sw/software/test/common/swhw \
	$(LOCAL_PATH)/decoder_sw/software/source/mpeg4

LOCAL_SHARED_LIBRARIES  += libg1

LOCAL_32_BIT_ONLY := true
LOCAL_MODULE:= g1dec_mpeg4_test
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
