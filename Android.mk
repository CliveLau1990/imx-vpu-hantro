ifeq ($(BOARD_HAVE_VPU), true)
ifeq ($(BOARD_VPU_TYPE), hantro)

LOCAL_PATH := $(call my-dir)
IMX_VPU_PATH := $(LOCAL_PATH)
ifeq ($(KERNEL_IMX_PATH),)
LINUX_KERNEL_ROOT := kernel_imx
else
LINUX_KERNEL_ROOT := $(KERNEL_IMX_PATH)/kernel_imx
endif
include $(CLEAR_VARS)

IMX_VPU_CFLAGS += -DANDROID_BUILD -D_POSIX_SOURCE -UDOMX_MEM_CHECK -Wno-unused-parameter -Wno-missing-field-initializers

IMX_VPU_CFLAGS += -DDEC_MODULE_PATH=\"/dev/mxc_hantro\" -DUSE_FAKE_RFC_TABLE -DFIFO_DATATYPE=void* -DNDEBUG -DDOWN_SCALER \
           -DUSE_EXTERNAL_BUFFER -DUSE_FAST_EC -DUSE_VP9_EC -DGET_FREE_BUFFER_NON_BLOCK \
           -DDEC_X170_OUTPUT_FORMAT=0 -DDEC_X170_TIMEOUT_LENGTH=-1 -DENABLE_HEVC_SUPPORT \
           -DENABLE_VP9_SUPPORT -DUSE_ION

IMX_VPU_CFLAGS += -DDWL_DISABLE_REG_PRINTS
IMX_VPU_CFLAGS += -DDWL_USE_DEC_IRQ
#this macro is added in sub makefile as LOCAL_CFLAGS_arm64 += -DUSE_64BIT_ENV
#IMX_VPU_CFLAGS += -DUSE_64BIT_ENV
IMX_VPU_CFLAGS += -DGET_OUTPUT_BUFFER_NON_BLOCK
IMX_VPU_CFLAGS +=  -DHANTRODEC_STREAM_SWAP=15
IMX_VPU_CFLAGS +=  -DUSE_OUTPUT_RELEASE
#add this flag 
IMX_VPU_CFLAGS +=  -DUSE_PICTURE_DISCARD
#disable debug macro
#ENABLE_HANTRO_DEBUG_LOG := true
ifeq ($(ENABLE_HANTRO_DEBUG_LOG), true)
IMX_VPU_CFLAGS += -D_DWL_DEBUG -DENABLE_DBGT_TRACE -DDBGT_CONFIG_AUTOVAR -DDBGT_VAR=0xFFF -DDBGT_TAG=dwl -DDEBUG -g
endif

IMX_VPU_CFLAGS += -O3

IMX_VPU_G1_CFLAGS := -DSET_OUTPUT_CROP_RECT -DUSE_EXTERNAL_BUFFER -DUSE_OUTPUT_RELEASE -DVSI_API \
             -DIS_G1_DECODER -DENABLE_CODEC_VP8 -DVP8_HWTIMEOUT_WORKAROUND -DENABLE_CODEC_MJPEG \
             -DGET_FREE_BUFFER_NON_BLOCK -DDOWN_SCALER -UCLEAR_HDRINFO_IN_SEEK -UFIFO_DATATYPE 

IMX_VPU_LDFLAGS := -shared -nostartfiles -Wl -Bsymbolic -Wl,--fatal-warnings

IMX_VPU_G1_LDFLAGS := -shared -nostartfiles -Wl,-Bsymbolic -Wl,-z -Wl,muldefs -Wl

IMX_VPU_INCLUDES := \
	$(IMX_VPU_PATH)/decoder_sw/software/linux/memalloc \
	$(IMX_VPU_PATH)/decoder_sw/software/linux/pcidriver \
	$(IMX_VPU_PATH)/decoder_sw/software/linux/pp \
	$(IMX_VPU_PATH)/decoder_sw/software/source/inc \
	$(IMX_VPU_PATH)/decoder_sw/software/source/common \
	$(IMX_VPU_PATH)/decoder_sw/software/source/hevc \
	$(IMX_VPU_PATH)/decoder_sw/software/source/config \
	$(IMX_VPU_PATH)/openmax_il/headers \
	$(IMX_VPU_PATH)/openmax_il/source \
	device/fsl/common/kernel-headers \
	system/core/libion/kernel-headers/linux

IMX_VPU_INCLUDES += $(LOCAL_PATH)/decoder_sw/software/linux/h264high
	
IMX_VPU_CFLAGS += -DHAVE_ARCH_STRUCT_FLOCK64

include $(IMX_VPU_PATH)/hantro.mk

#include $(IMX_VPU_PATH)/decoder_sw/software/source/common/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/h264high/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/avs/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/jpeg/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/mpeg2/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/mpeg4/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/rv/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/vc1/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/vp6/Android.mk
include $(IMX_VPU_PATH)/decoder_sw/software/source/vp8/Android.mk

include $(IMX_VPU_PATH)/g1.mk

include $(IMX_VPU_PATH)/openmax_il/source/decoder/Android_codec.mk

#do not build binrary for test, it may meet build error when enable them
#include $(IMX_VPU_PATH)/g2_test.mk
#include $(IMX_VPU_PATH)/g1_h264_test.mk
#include $(IMX_VPU_PATH)/g1_vp6_test.mk
#include $(IMX_VPU_PATH)/g1_vp8_test.mk
#include $(IMX_VPU_PATH)/g1_mpeg4_test.mk
#include $(IMX_VPU_PATH)/g1_vc1_test.mk
#include $(IMX_VPU_PATH)/g1_mpeg2_test.mk

endif
endif
