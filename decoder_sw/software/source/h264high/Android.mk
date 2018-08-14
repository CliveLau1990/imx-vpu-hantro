LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    h264_pp_pipeline.c \
    h264decapi.c \
    h264decapi_e.c \
    h264decmcapi.c \
    h264hwd_asic.c \
    h264hwd_cabac.c \
    h264hwd_cavlc.c \
    h264hwd_conceal.c \
    h264hwd_decoder.c \
    h264hwd_dpb.c \
    h264hwd_dpb_lock.c \
    h264hwd_inter_prediction.c \
    h264hwd_intra_prediction.c \
    h264hwd_macroblock_layer.c \
    h264hwd_slice_data.c \
    h264hwd_storage.c \
    legacy/h264hwd_byte_stream.c \
	legacy/h264hwd_nal_unit.c \
	legacy/h264hwd_neighbour.c \
	legacy/h264hwd_pic_order_cnt.c \
	legacy/h264hwd_pic_param_set.c \
	legacy/h264hwd_sei.c \
	legacy/h264hwd_seq_param_set.c \
	legacy/h264hwd_slice_group_map.c \
	legacy/h264hwd_slice_header.c \
	legacy/h264hwd_stream.c \
	legacy/h264hwd_util.c \
	legacy/h264hwd_vlc.c \
	legacy/h264hwd_vui.c \
    ../common/regdrv_g1.c \
    ../common/refbuffer.c \
    ../common/tiledref.c \
    ../common/workaround.c \
    ../common/errorhandling.c \
    ../common/commonconfig_g1.c \
    ../common/input_queue.c \
    ../common/fifo.c \
    ../common/stream_corrupt.c


LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)

LOCAL_CFLAGS += $(IMX_VPU_CFLAGS)
LOCAL_CFLAGS += $(IMX_VPU_G1_CFLAGS)

LOCAL_CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK -DGET_OUTPUT_BUFFER_NON_BLOCK \
	-DSKIP_OPENB_FRAME -DENABLE_DPB_RECOVER -D_DISABLE_PIC_FREEZE -DUSE_WORDACCESS -DDEC_X170_USING_IRQ=1

LOCAL_CFLAGS += -DFIFO_DATATYPE=void*
LOCAL_LDFLAGS += $(IMX_VPU_LDFLAGS)

LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV

LOCAL_C_INCLUDES += $(IMX_VPU_INCLUDES)
LOCAL_C_INCLUDES += $(LOCAL_PATH)/legacy \
	$(LOCAL_PATH)/../common

LOCAL_MODULE:= lib_imx_vsi_h264
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

