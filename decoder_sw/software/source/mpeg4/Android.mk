LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mp4dechwd_error_conceal.c \
	mp4decapi.c \
	mp4decapi_internal.c \
	mp4dechwd_rvlc.c \
	mp4dechwd_strmdec.c \
	mp4dechwd_headers.c \
	mp4dechwd_motiontexture.c \
	mp4dechwd_shortvideo.c \
	mp4dechwd_utils.c \
	mp4dechwd_videopacket.c \
	mp4dechwd_vlc.c \
	mp4dechwd_vop.c \
	../common/regdrv_g1.c \
	../common/tiledref.c \
	../common/refbuffer.c \
	../common/workaround.c \
	../common/bqueue.c \
	../common/errorhandling.c \
	../common/commonconfig_g1.c \
     ../common/input_queue.c \
    ../common/fifo.c \
		mp4dechwd_custom.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)



LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -D_MP4_RLC_BUFFER_SIZE=384
LOCAL_CFLAGS += -DFIFO_DATATYPE=addr_t

LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += ../common \
	$(LOCAL_PATH)/../../linux/mpeg4

LOCAL_MODULE:= lib_imx_vsi_mpeg4
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

