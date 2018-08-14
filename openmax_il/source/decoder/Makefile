#-------------------------------------------------------------------------------
#-       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
#-                                                                            --
#- This software is confidential and proprietary and may be used only as      --
#-   expressly authorized by VeriSilicon in a written licensing agreement.    --
#-                                                                            --
#-         This entire notice must be reproduced on all copies                --
#-                       and may not be removed.                              --
#-                                                                            --
#-------------------------------------------------------------------------------
#- Redistribution and use in source and binary forms, with or without         --
#- modification, are permitted provided that the following conditions are met:--
#-   * Redistributions of source code must retain the above copyright notice, --
#-       this list of conditions and the following disclaimer.                --
#-   * Redistributions in binary form must reproduce the above copyright      --
#-       notice, this list of conditions and the following disclaimer in the  --
#-       documentation and/or other materials provided with the distribution. --
#-   * Neither the names of Google nor the names of its contributors may be   --
#-       used to endorse or promote products derived from this software       --
#-       without specific prior written permission.                           --
#-------------------------------------------------------------------------------
#- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
#- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
#- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
#- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
#- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
#- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
#- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
#- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
#- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
#- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
#- POSSIBILITY OF SUCH DAMAGE.                                                --
#-------------------------------------------------------------------------------
#-----------------------------------------------------------------------------*/


BELLAGIO_ROOT = ../../../libomxil-bellagio-0.9.3
DECODER_RELEASE = ../../../decoder_sw
G2_DECODER_RELEASE = ../../../decoder_sw
INSTALL_DIR = $(BELLAGIO_ROOT)/build/lib/bellagio

## Select decoder HW
#DECODER = G1
DECODER = G2
#DECODER = 81x0

## Default build is for 32-bit platform
## Set this to 'y' when target is 64-bit platform
64BIT_BUILD = y

### G1 settings ###
ifeq ($(DECODER), G1)
## Set this to 'n' or 'y' in order to disable/enable HW model library
## Do not enable when using HW platform
USE_HWMODEL = y

## Enable if SW tag is GoldenG1_6_1 or greater
MULTICORE_SW = y

CODECS += -DENABLE_CODEC_VP6
CODECS += -DENABLE_CODEC_H263
CODECS += -DENABLE_CODEC_MPEG4
CODECS += -DENABLE_CODEC_DIVX
CODECS += -DENABLE_CODEC_MPEG2
CODECS += -DENABLE_CODEC_VC1
CODECS += -DENABLE_CODEC_JPEG
CODECS += -DENABLE_CODEC_RV
CODECS += -DENABLE_CODEC_H264
#CODECS += -DENABLE_PP
CODECS += -DENABLE_CODEC_AVS
CODECS += -DENABLE_CODEC_VP8
CODECS += -DENABLE_CODEC_WEBP
#CODECS += -DENABLE_CODEC_MJPEG

## This flag enables SW workaround for VP8 decoder HW timeout issue
## found in hwg1_1_68, hwg1_2_5_r, hwg1_2_39_r, hwg1_2_86 and older
CFLAGS += -DVP8_HWTIMEOUT_WORKAROUND

## If PP is enabled, the output image is automatically scaled to output port width and height
## This flag disables the output scaling. No effect without PP
#CFLAGS += -DDISABLE_OUTPUT_SCALING

## Use externally allocated frame buffers and picture consumed call.
## Requires support from G1 SW
CFLAGS += -DUSE_EXTERNAL_BUFFER -DUSE_OUTPUT_RELEASE

endif
###################

### G2 settings ###
ifeq ($(DECODER), G2)
## Set this to 'n' or 'y' in order to disable/enable HW model library
## Do not enable when using HW platform
USE_G2_HWMODEL = y

CODECS += -DENABLE_CODEC_HEVC
CODECS += -DENABLE_CODEC_VP9

## Use Verisilicon G2 API
CFLAGS += -DVSI_API

## Use externally allocated frame buffers. Requires support from G2 SW
CFLAGS += -DUSE_EXTERNAL_BUFFER

## G2 SW build enables down scaler support by default, so this is needed also for OMX
CFLAGS += -DDOWN_SCALER
endif
###################

## Debug trace flags
#CFLAGS += -DENABLE_DBGT_TRACE
CFLAGS += -DDBGT_CONFIG_AUTOVAR

## Enable OMX IL tunnel support
#CFLAGS += -DOMX_DECODER_TUNNELING_SUPPORT

## If enabled, OMX component fills output crop info for OMX_IndexConfigCommonOutputCrop
## This info can be used to crop the output with GPU or display controller
#CFLAGS += -DSET_OUTPUT_CROP_RECT

## Enable OSAL memory allocation trace
#CFLAGS += -DOMX_MEM_TRC

## Use DWL for memory allocation
CFLAGS += -DUSE_DWL

## Enable only for OMX IL conformance testing ##
#CFLAGS += -DCONFORMANCE

## These flags disable dynamic calculation of OMX port buffer size.
## Default sizes are defined in codec.h
#CFLAGS += -DUSE_DEFAULT_INPUT_BUFFER_SIZE
#CFLAGS += -DUSE_DEFAULT_OUTPUT_BUFFER_SIZE

## This flag enables temporary input buffer usage when OMX client uses OMX_UseBuffer
## macro for input buffers. Enable if input buffers are not accessible by the HW decoder
#CFLAGS += -DUSE_TEMP_INPUT_BUFFER

## This flag enables temporary output buffer usage when OMX client uses OMX_UseBuffer
## macro for output buffers. Enable if output buffers are not accessible by the HW decoder
#CFLAGS += -DUSE_TEMP_OUTPUT_BUFFER

## This flag enables usage of ALLOC_PRIVATE struct.
## The data is accessed via pInputPortPrivate
#CFLAGS += -DUSE_ALLOC_PRIVATE

## This flag enables usage of OUTPUT_BUFFER_PRIVATE struct.
## The data is accessed via pOutputPortPrivate
## Required for VP9 tiled mode and RFC table data access
CFLAGS += -DUSE_OUTPUT_BUFFER_PRIVATE

## This flag used to determine OMX continue decoding or not after receive CODEC_ERROR_STREAM_NOT_SUPPORTED.
## If SKIP_NOT_SUPPORTED_STREAM is not defined, component would go to INVALID state.
CFLAGS += -DSKIP_NOT_SUPPORTED_STREAM

CFLAGS += -pthread

# If the machine is 64-bit and we are compiling for PC, force 32-bit compilation
ifeq ($(shell uname -m),x86_64)
#	ifneq (,$(findstring arm,$(MAKECMDGOALS)))
		export M32 = -m32
#	endif
endif

ifeq ($(64BIT_BUILD), y)
export M32 = -m64
endif

ifeq ($(DECODER), G1)
CFLAGS += -DIS_G1_DECODER
VIDEOLIB = libOMX.hantro.G1.video.decoder.so
IMAGELIB = libOMX.hantro.G1.image.decoder.so
endif

ifeq ($(DECODER), G2)
CFLAGS += -DIS_G2_DECODER
VIDEOLIB = libOMX.hantro.G2.video.decoder.so
endif

ifeq ($(DECODER), 81x0)
CFLAGS += -DIS_8190_DECODER
VIDEOLIB = libOMX.hantro.81x0.video.decoder.so
IMAGELIB = libOMX.hantro.81x0.image.decoder.so
endif

HANTRO_LIBS_VIDEO =
HANTRO_LIBS_IMAGE =

ifneq (,$(findstring $(DECODER), G1-81x0))
CFLAGS += -I$(DECODER_RELEASE)/software/source/h264high

CFLAGS += -I.. \
    -I../../headers \
    -I$(BELLAGIO_ROOT)/src \
    -I$(DECODER_RELEASE)/software/source/inc \
    -I$(DECODER_RELEASE)/software/source/common \
    -I$(DECODER_RELEASE)/software/source/pp \
    -I$(DECODER_RELEASE)/software/test/common \
    -I$(DECODER_RELEASE)/software/test/common/swhw \
    -I$(DECODER_RELEASE)/software/linux/dwl \
    -I$(DECODER_RELEASE)/software/linux/memalloc

HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/dwl/libdwlx170.a
HANTRO_LIBS_IMAGE += $(DECODER_RELEASE)/software/linux/dwl/libdwlx170.a
DEC_DIR = $(DECODER_RELEASE)
endif

ifneq (,$(findstring $(DECODER), G2))
CFLAGS += -I.. \
    -I../../headers \
    -I$(BELLAGIO_ROOT)/src \
    -I$(G2_DECODER_RELEASE)/ \
    -I$(G2_DECODER_RELEASE)/software/source/inc \
    -I$(G2_DECODER_RELEASE)/software/source/common \
    -I$(G2_DECODER_RELEASE)/software/test/common/swhw \
    -I$(G2_DECODER_RELEASE)/software/linux/dwl \
    -I$(G2_DECODER_RELEASE)/software/linux/memalloc

HANTRO_LIBS_VIDEO += $(G2_DECODER_RELEASE)/out/*linux/debug/libdwlg2.a
DEC_DIR = $(G2_DECODER_RELEASE)
endif

CFLAGS += $(CODECS)

ifeq ($(MULTICORE_SW),y)
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/test/common/utils/libutils.a
HANTRO_LIBS_IMAGE += $(DECODER_RELEASE)/software/test/common/utils/libutils.a
endif

ifeq ($(USE_HWMODEL),y)
## For testing with SW model
CFLAGS += -DINCLUDE_TB

HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/system/models/g1hw/lib8170hw.a \
	$(DECODER_RELEASE)/software/test/common/swhw/libtbcommon.a
HANTRO_LIBS_IMAGE += $(DECODER_RELEASE)/system/models/g1hw/lib8170hw.a \
	$(DECODER_RELEASE)/software/test/common/swhw/libtbcommon.a
endif

ifeq ($(USE_G2_HWMODEL),y)
CFLAGS += -DINCLUDE_TB

HANTRO_LIBS_VIDEO += $(G2_DECODER_RELEASE)/out/*linux/debug/libg2hw.a
HANTRO_LIBS_VIDEO += $(G2_DECODER_RELEASE)/out/*linux/debug/libg2tbcommon.a
endif

ifneq (,$(findstring -DIS_G2_DECODER, $(CFLAGS)))
HANTRO_LIBS_VIDEO += $(G2_DECODER_RELEASE)/out/*linux/debug/libg2common.a
endif

ifneq (,$(findstring -DENABLE_CODEC_H264, $(CODECS)))
VIDEO_SRCS += codec_h264.c
ifneq (,$(findstring -DIS_8190_DECODER, $(CFLAGS)) || ,$(findstring -DIS_G1_DECODER, $(CFLAGS)))
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/h264high/libdecx170h.a
else
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/h264/libdecx170h.a
endif
endif

ifneq (,$(findstring -DENABLE_CODEC_MPEG4, $(CODECS)))
VIDEO_SRCS += codec_mpeg4.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/mpeg4/libdecx170m.a
endif

ifneq (,$(findstring -DENABLE_CODEC_MPEG2, $(CODECS)))
VIDEO_SRCS += codec_mpeg2.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/mpeg2/libdecx170m2.a
endif

ifneq (,$(findstring -DENABLE_CODEC_VC1, $(CODECS)))
VIDEO_SRCS += codec_vc1.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/vc1/libdecx170v.a
endif

ifneq (,$(findstring -DENABLE_CODEC_VP6, $(CODECS)))
VIDEO_SRCS += codec_vp6.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/vp6/libdec8190vp6.a
endif

ifneq (,$(findstring -DENABLE_CODEC_MJPEG, $(CODECS)))
VIDEO_SRCS += codec_jpeg.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/jpeg/libx170j.a
endif

ifneq (,$(findstring -DENABLE_CODEC_VP8, $(CODECS)))
VIDEO_SRCS += codec_vp8.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/vp8/libdecx170vp8.a
endif

ifneq (,$(findstring -DENABLE_CODEC_AVS, $(CODECS)))
VIDEO_SRCS += codec_avs.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/avs/libdecx170a.a
endif

ifneq (,$(findstring -DENABLE_CODEC_RV, $(CODECS)))
VIDEO_SRCS += codec_rv.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/rv/libdecx170rv.a
endif

ifneq (,$(findstring -DENABLE_CODEC_JPEG, $(CODECS)))
IMAGE_SRCS = codec_jpeg.c
HANTRO_LIBS_IMAGE += $(DECODER_RELEASE)/software/linux/jpeg/libx170j.a
endif

ifneq (,$(findstring -DENABLE_CODEC_WEBP, $(CODECS)))
IMAGE_SRCS += codec_webp.c
HANTRO_LIBS_IMAGE += $(DECODER_RELEASE)/software/linux/vp8/libdecx170vp8.a
endif

ifneq (,$(findstring -DENABLE_PP, $(CODECS)))
IMAGE_SRCS += codec_pp.c post_processor.c
VIDEO_SRCS += codec_pp.c post_processor.c
HANTRO_LIBS_VIDEO += $(DECODER_RELEASE)/software/linux/pp/libdecx170p.a
HANTRO_LIBS_IMAGE += $(DECODER_RELEASE)/software/linux/pp/libdecx170p.a
endif

ifneq (,$(findstring -DENABLE_CODEC_HEVC, $(CODECS)))
VIDEO_SRCS += codec_hevc.c
HANTRO_LIBS_VIDEO += $(G2_DECODER_RELEASE)/out/*linux/debug/libg2hevc.a
endif

ifneq (,$(findstring -DENABLE_CODEC_VP9, $(CODECS)))
VIDEO_SRCS += codec_vp9.c
HANTRO_LIBS_VIDEO += $(G2_DECODER_RELEASE)/out/*linux/debug/libg2vp9.a
endif

base_SRCS = ../msgque.c ../OSAL.c ../basecomp.c ../port.c ../util.c
libhantrovideodec_SRCS = $(VIDEO_SRCS)
libhantrovideodec_OBJS = $(base_SRCS:.c=.o) $(libhantrovideodec_SRCS:.c=.o) decoder_video.o library_entry_point_video.o
libhantroimagedec_SRCS = $(IMAGE_SRCS)
libhantroimagedec_OBJS = $(base_SRCS:.c=.o) $(libhantroimagedec_SRCS:.c=.o) decoder_image.o library_entry_point_image.o


all:
	@echo ""
	@echo "MAKE for OpenMAX IL Decoder Component"
	@echo ""
	@echo " Available targets:"
	@echo ""
	@echo "  pclinux          build image AND video decoder omxil component for HW model testing"
	@echo "  arm_pclinux      build image AND video decoder omxil component for HW model testing at ARM platform"
	@echo "  arm              build image AND video decoder omxil component for ARM platform"
	@echo "  install          installs omxil component with Bellagio"
	@echo "  clean            deletes generated output"
	@echo ""
	@echo " Current settings:"
	@echo ""
	@echo "  G1 Decoder Directory:      $(DECODER_RELEASE)"
	@echo "  G2 Decoder Directory:      $(G2_DECODER_RELEASE)"
	@echo "  Bellagio Root:             $(BELLAGIO_ROOT)"
	@echo "  Install Dir:               $(INSTALL_DIR)"
	@echo ""

.PHONY: pclinux
pclinux: CC = gcc
pclinux: CFLAGS += $(M32) -O2 -finline-functions -W -Wno-inline -Wall -fPIC -Wunused-variable -DNDEBUG
pclinux: LDFLAGS = $(M32)
pclinux: videolib imagelib

.PHONY: arm_pclinux
#arm_pclinux: CC = arm-none-linux-gnueabi-gcc
arm_pclinux: CC = aarch64-linux-gnu-gcc
arm_pclinux: CFLAGS += -O2 -fpic
arm_pclinux: LDFLAGS =
arm_pclinux: videolib imagelib
.PHONY: arm
#arm: CC = arm-none-linux-gnueabi-gcc
#arm: CFLAGS += -O2 -mtune=arm926ej-s -fpic -DMEMALLOCHW
arm: CC = aarch64-linux-gnu-gcc
arm: CFLAGS += -O2 -fpic -DMEMALLOCHW
arm: LDFLAGS =
arm: videolib imagelib

ifeq ($(DECODER), G1)
install:
	cp -vf $(VIDEOLIB) $(INSTALL_DIR)/
	cp -vf $(IMAGELIB) $(INSTALL_DIR)/
	$(BELLAGIO_ROOT)/build/bin/omxregister-bellagio $(INSTALL_DIR)/
endif

ifeq ($(DECODER), G2)
install:
	cp -vf $(VIDEOLIB) $(INSTALL_DIR)/
	$(BELLAGIO_ROOT)/build/bin/omxregister-bellagio $(INSTALL_DIR)/
endif

install_video:
	cp -vf $(VIDEOLIB) $(INSTALL_DIR)/
	$(BELLAGIO_ROOT)/build/bin/omxregister-bellagio $(INSTALL_DIR)/

ifeq ($(DECODER), G1)
clean:
	rm -f $(libhantrovideodec_OBJS) $(VIDEOLIB) $(INSTALL_DIR)/$(VIDEOLIB) *.o *.so
	rm -f $(libhantroimagedec_OBJS) $(IMAGELIB) $(INSTALL_DIR)/$(IMAGELIB) *.o *.so
endif

ifeq ($(DECODER), G2)
clean:
	rm -f $(libhantrovideodec_OBJS) $(VIDEOLIB) $(INSTALL_DIR)/$(VIDEOLIB) *.o *.so
endif

test: test.o
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--start-group $(HANTRO_LIBS_IMAGE) -lrt -W1,--end-group -o jpegtest test.c codec_jpeg.c post_processor.c ../OSAL.c
	cp -vf jpegtest $(INSTALL_DIR)/

videolib: $(libhantrovideodec_OBJS) $(HANTRO_LIBS_VIDEO)
	$(CC) $(LDFLAGS) -o $(VIDEOLIB) -Wl,-h -Wl,$(VIDEOLIB) -shared -Wl,--start-group $(libhantrovideodec_OBJS) $(HANTRO_LIBS_VIDEO) -lrt -Wl,--end-group

ifeq ($(DECODER), G1)
imagelib: $(libhantroimagedec_OBJS) $(HANTRO_LIBS_IMAGE)
	$(CC) $(LDFLAGS) -o $(IMAGELIB) -Wl,-h -Wl,$(IMAGELIB) -shared -Wl,--start-group $(libhantroimagedec_OBJS) $(HANTRO_LIBS_IMAGE) -lrt -Wl,--end-group
endif

ifeq ($(DECODER), G2)
imagelib:
endif

decoder_video.o: decoder.c
	$(CC) $(CFLAGS) -DOMX_DECODER_VIDEO_DOMAIN -c $< -o $@

decoder_image.o: decoder.c
	$(CC) $(CFLAGS) -DOMX_DECODER_IMAGE_DOMAIN -c $< -o $@

library_entry_point_video.o: library_entry_point.c
	$(CC) $(CFLAGS) -DOMX_DECODER_VIDEO_DOMAIN -c $< -o $@

library_entry_point_image.o: library_entry_point.c
	$(CC) $(CFLAGS) -DOMX_DECODER_IMAGE_DOMAIN -c $< -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@
