#------------------------------------------------------------------------------
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

## Select which decoder HW is used. Do not enable both at the same time.
ENABLE_G1 = n
ENABLE_G2 = y

## Set this to 'n' or 'y' in order to disable/enable realvideo support. G1 only
ENABLE_RV = n

## Default build is for 32-bit platform
## Set this to 'y' when target is 64-bit platform
64BIT_BUILD = y

G1_DEC_DIR = ../../../../decoder_sw
G2_DEC_DIR = ../../../../decoder_sw
REAL_PARSER_DIR = $(G1_DEC_DIR)/software/test/rv/rm_parser
INSTALL_DIR=.

CFLAGS = -g -finline-functions -Wno-inline -W -Wall -fPIC -Wunused-variable -pthread -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
LDFLAGS= -Wl,-R -Wl,".." -Wl,-rpath-link -Wl,".." -pthread

# If the machine is 64-bit and we are compiling for PC, force 32-bit compilation
ifeq ($(shell uname -m),x86_64)
#	ifneq (,$(findstring arm,$(MAKECMDGOALS)))
		export M32 = -m32
#	endif
endif

ifeq ($(64BIT_BUILD), y)
export M32 = -m64
endif

SRCS = video_decoder.c \
        file_reader.c \
        queue.c
LIBS =

CFLAGS += -I. -I../ -I"../.." -I"../../../headers"
CFLAGS += -DOMX_DECODER_VIDEO_DOMAIN -DOMX_DECODER_IMAGE_DOMAIN
CFLAGS += -DENABLE_DBGT_TRACE
CFLAGS += -DDBGT_CONFIG_AUTOVAR
CFLAGS += -DCOMPONENT_SETS_PORT_BUFFER_SIZE

ifeq ($(ENABLE_RV),y)
    CFLAGS += -DENABLE_CODEC_RV
    CFLAGS += -I"$(REAL_PARSER_DIR)/include"
    LIBS += $(REAL_PARSER_DIR)/librvparser.a
    SRCS += rv_ff_read.c
endif

ifeq ($(ENABLE_G1),y)
    CFLAGS += -I"$(G1_DEC_DIR)/software/source/inc" -I"$(G1_DEC_DIR)/software/test/vp8"
    CFLAGS += -DENABLE_CODEC_VP6 -DENABLE_CODEC_H263 -DENABLE_CODEC_MPEG4 -DENABLE_CODEC_DIVX -DENABLE_CODEC_MPEG2 -DENABLE_CODEC_VC1
    CFLAGS += -DENABLE_CODEC_JPEG -DENABLE_CODEC_H264 -DENABLE_CODEC_AVS -DENABLE_CODEC_VP8 -DENABLE_CODEC_WEBP -DENABLE_CODEC_MJPEG
    CFLAGS += -DENABLE_PP
    VIDEOLIB = ../libOMX.hantro.G1.video.decoder.so
    IMAGELIB = ../libOMX.hantro.G1.image.decoder.so
endif

ifeq ($(ENABLE_G2),y)
    CFLAGS += -I"$(G2_DEC_DIR)/software/source/inc" \
              -I"$(G2_DEC_DIR)/software/test/vp9" \
              -I"$(G2_DEC_DIR)/software/test/common/swhw"
	CFLAGS += -DVIDEO_ONLY
    VIDEOLIB = ../libOMX.hantro.G2.video.decoder.so
	IMAGELIB = 
endif

OBJS := $(SRCS:.c=.o)

all:
	@echo ""
	@echo "MAKE for OpenMAX IL Decoder TB"
	@echo ""
	@echo " Available targets:"
	@echo ""
	@echo "  pclinux          build OMX testbench for HW model testing"
	@echo "  arm_pclinux      build OMX testbench for HW model testing at ARM platform"
	@echo "  arm              build OMX testbench for ARM platform"
	@echo "  clean            deletes generated output"
	@echo ""
	@echo ""

.PHONY: pclinux
pclinux: CC = gcc
pclinux: CFLAGS += $(M32)
pclinux: LDFLAGS += $(M32)
pclinux: video_decoder

.PHONY: arm_pclinux
#pclinux: CC = arm-none-linux-gnueabi-gcc
arm_pclinux: CC = aarch64-linux-gnu-gcc
arm_pclinux: CFLAGS +=
arm_pclinux: LDFLAGS +=
arm_pclinux: video_decoder

.PHONY: arm
#arm: CC = arm-none-linux-gnueabi-gcc
#arm: CFLAGS += -march=armv6
arm: CC = aarch64-linux-gnu-gcc
arm: LDFLAGS +=
arm: video_decoder

clean:
	rm -f *.o video_decoder

ifneq (,$(findstring -DVIDEO_ONLY, $(CFLAGS)))
video_decoder: $(OBJS) $(VIDEOLIB) $(LIBS)
else
video_decoder: $(OBJS) $(VIDEOLIB) $(IMAGELIB) $(LIBS)
endif
	$(CC) $(LDFLAGS) -Wl,--start-group $^ -ldl -lm -lrt -Wl,--end-group -pthread -o $@

install:
	cp video_decoder $(INSTALL_DIR)

