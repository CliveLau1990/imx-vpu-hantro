LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    bqueue.c \
    commonconfig.c \
    fifo.c \
    raster_buffer_mgr.c \
    regdrv.c \
    sw_stream.c \
    input_queue.c \
    sw_util.c \
    stream_corrupt.c \
	workaround.c \
	refbuffer.c \
	tiledref.c \
	errorhandling.c \
	../pp/ppapi.c \
	../pp/ppinternal.c \
	regdrv_g1.c
        
LOCAL_CFLAGS += $(IMX_VPU_CFLAGS) 

LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)
 
LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)

LOCAL_MODULE:= lib_imx_vsi_common
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

