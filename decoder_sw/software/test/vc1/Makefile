#------------------------------------------------------------------------------
#-       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
#-         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
#-         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
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

#--  Abstract : Makefile for decoder testbench
#--
#-------------------------------------------------------------------------------

# Open below compiling options by default to support NXP's request
USE_64BIT_ENV = y
USE_OUTPUT_RELEASE = y
USE_EXTERNAL_BUFFER = y
USE_NON_BLOCKING = y
USE_ONE_THREAD_WAIT = y

# set this to 'y' for md5 checksum calculation of the frames instead of yuv
USE_MD5SUM = n

SRCS = dectestbench.c
#SRCS = decpp.c
#SRCS += yuvtorgb16.c yuvtorgbinit.c
OBJS    = $(SRCS:.c=.o)

INCLUDE = -I../../source/inc -I../../source/vc1 -I../../source/config \
          -I../../source/dwl -I../../source/common -I../common/swhw \
          -I../common/utils

DECLIBDIR = -L../../linux/vc1 -L../../linux/dwl -L../common/swhw \
	    -L../../../lib -L../common/utils
DECLIB = libdecx170v.a
DWLLIB = libdwlx170.a
TBLIB = libtbcommon.a

TESTDEC=vx170dec
TARGET_ENV=integrator

# Enable SW performance  measurement
#CFLAGS = -DSW_PERFORMANCE

ifeq ($(USE_MD5SUM), y)
        MD5SUM = -DMD5SUM
endif

ifeq ($(shell uname -m),x86_64)
	ifneq (,$(findstring pclinux,$(MAKECMDGOALS)))
		export M32 = -m32
	endif
	ifneq (,$(findstring coverage,$(MAKECMDGOALS)))
		export M32 = -m32
	endif
	ifneq (,$(findstring verification,$(MAKECMDGOALS)))
		export M32 = -m32
	endif
endif

# Set for 64bit compilation
ifeq ($(USE_64BIT_ENV), y)
	export M32 = -m64
	CFLAGS+=-DUSE_64BIT_ENV
endif

export ASIC_TRACE_SUPPORT
ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS+=-DASIC_TRACE_SUPPORT
endif

export USE_OUTPUT_RELEASE
ifeq ($(USE_OUTPUT_RELEASE), y)
	CFLAGS+=-DUSE_OUTPUT_RELEASE
	FIFO_DATATYPE=addr_t
	DEFINES = -DFIFO_DATATYPE=$(FIFO_DATATYPE)
	CFLAGS += $(DEFINES)
	export FIFO_DATATYPE
endif

export USE_EXTERNAL_BUFFER
ifeq ($(USE_EXTERNAL_BUFFER), y)
	CFLAGS += -DUSE_EXTERNAL_BUFFER
endif

export CLEAR_HDRINFO_IN_SEEK
ifeq ($(CLEAR_HDRINFO_IN_SEEK), y)
	CFLAGS += -DCLEAR_HDRINFO_IN_SEEK
endif

export USE_PICTURE_DISCARD
ifeq ($(USE_PICTURE_DISCARD), y)
	CFLAGS += -DUSE_PICTURE_DISCARD
endif

export USE_NON_BLOCKING
ifeq ($(USE_NON_BLOCKING), y)
	CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK
endif

export USE_ONE_THREAD_WAIT
ifeq ($(USE_ONE_THREAD_WAIT), y)
	CFLAGS += -DGET_OUTPUT_BUFFER_NON_BLOCK
endif

all: versatile

.PHONY: versatile
#versatile: CC=arm-none-linux-gnueabi-gcc
versatile: CC=aarch64-linux-gnu-gcc
#versatile: CFLAGS += -O -g -mtune=arm926ej-s -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
versatile: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
versatile: TESTDEC=vx170dec_versatile
versatile: TARGET_ENV=versatile
versatile: LIBS = $(DECLIBDIR) -lpthread -ldecx170v -ldwlx170 -ltbcommon -lutils $(EFENCE)
versatile: test

.PHONY: pcie
pcie: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
pcie: TESTDEC=vx170dec_pcie
pcie: TARGET_ENV=pcie
pcie: LIBS = $(DECLIBDIR) -pthread -ldecx170v -ldwlx170 -ltbcommon -lutils $(EFENCE)
pcie: test


.PHONY: integrator
integrator: CC=arm-linux-gcc
integrator: CFLAGS += -O -g -mtune=arm920tdmi -fpic $(INCLUDE) $(MD5SUM)
integrator: TESTDEC=vx170dec_integrator
integrator: TARGET_ENV=integrator
integrator: LIBS = $(DECLIBDIR) -lpthread -ldecx170v -ldwlx170 -ltbcommon -lutils $(EFENCE)
integrator: test


.PHONY: ads9
ads9: CC=armcc
ads9: CFLAGS = --no_inline --cpu ARM926EJ-S -O2 -Otime $(INCLUDE)
ads9: TESTDEC=vx170dec_ads9.axf
ads9: TARGET_ENV=ads9
ads9: LIBS = -L../../linux/vc1/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../common/swhw/$(TBLIB)
ads9: test

.PHONY: ads
ads: CC=armcc
ads: CFLAGS = --no_inline --cpu ARM1136J-S -O3 -Otime $(INCLUDE)
ads: TESTDEC=vx170dec_ads.axf
ads: TARGET_ENV=ads
ads: LIBS = -L../../linux/vc1/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../common/swhw/$(TBLIB)
ads: test

.PHONY: pclinux
pclinux: CC=gcc
pclinux: CFLAGS += -g $(INCLUDE) $(MD5SUM) $(M32) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry)
pclinux: TESTDEC=vx170dec_pclinux
pclinux: TARGET_ENV=pclinux
pclinux: LIBS = $(DECLIBDIR) -pthread -ldecx170v -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux: test

.PHONY: arm_pclinux
arm_pclinux: CC=aarch64-linux-gnu-gcc
arm_pclinux: CFLAGS += -g $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry)
arm_pclinux: TESTDEC=vx170dec_arm_pclinux
arm_pclinux: TARGET_ENV=arm_pclinux
arm_pclinux: LIBS = $(DECLIBDIR) -pthread -ldecx170v -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
arm_pclinux: test

.PHONY: pclinux_eval_8170
pclinux_eval_8170: CFLAGS += -DVC1_EVALUATION_8170
pclinux_eval_8170: _PRODUCT_8170 = y
pclinux_eval_8170: pclinux_eval

.PHONY: pclinux_eval_8190
pclinux_eval_8190: CFLAGS += -DVC1_EVALUATION_8190
pclinux_eval_8190: _PRODUCT_8190 = y
pclinux_eval_8190: pclinux_eval

.PHONY: pclinux_eval_9170
pclinux_eval_9170: CFLAGS += -DVC1_EVALUATION_9170
pclinux_eval_9170: _PRODUCT_9170 = y
pclinux_eval_9170: pclinux_eval

.PHONY: pclinux_eval_9190
pclinux_eval_9190: CFLAGS += -DVC1_EVALUATION_9190
pclinux_eval_9190: _PRODUCT_9190 = y
pclinux_eval_9190: pclinux_eval

.PHONY: pclinux_eval_G1
pclinux_eval_G1: CFLAGS += -DVC1_EVALUATION_G1
pclinux_eval_G1: _PRODUCT_G1 = y
pclinux_eval_G1: pclinux_eval

.PHONY: pclinux_eval
pclinux_eval: CC=gcc
pclinux_eval: CFLAGS += $(INCLUDE) $(MD5SUM) $(M32) -DVC1_EVALUATION -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry)
pclinux_eval: TESTDEC=vx170dec_pclinux_eval
pclinux_eval: TARGET_ENV=pclinux_eval
pclinux_eval: LIBS = $(DECLIBDIR) -lpthread -ldecx170v -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: test

export _PRODUCT_8170
export _PRODUCT_8190
export _PRODUCT_9170
export _PRODUCT_9190
export _PRODUCT_G1

test: $(OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(OBJS) $(LIBS) -o $(TESTDEC)

lib:
	make -C ../../linux/vc1 $(TARGET_ENV)
	make -C ../../linux/dwl $(TARGET_ENV)
	make -C ../common/swhw $(TARGET_ENV)
	make -C ../common/utils $(TARGET_ENV)

clean:
	rm -f *.o
	rm -f .depend
	rm -f vx170dec_*

libclean:
	make -C ../../linux/vc1 clean
	make -C ../../linux/dwl clean
	make -C ../common/swhw clean
	make -C ../common/utils clean

depend .depend dep: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

ifeq (.depend, $(wildcard .depend))
include .depend
endif
