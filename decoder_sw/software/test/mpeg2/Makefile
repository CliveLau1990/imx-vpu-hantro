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

#source search path
vpath %.c

SRCS = mpeg2dectest.c
OBJS    = $(SRCS:.c=.o)

INCLUDE = -I../../source/inc -I../../source/mpeg2/ -I../../source/config \
          -I../../source/common  -I../common/swhw \
          -I../common/utils

DECLIBDIR = -L../../linux/dwl -L../../linux/mpeg2 -L../common/swhw \
            -L../../../lib -L../common/utils
DECLIB =  libdecx170m2.a
DWLLIB = libdwlx170.a
TBLIB = libtbcommon.a
MODLIB = lib8170hw.a
TARGET_ENV=

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
	CFLAGS += -DUSE_64BIT_ENV
endif

export USE_OUTPUT_RELEASE
ifeq ($(USE_OUTPUT_RELEASE), y)
	CFLAGS += -DUSE_OUTPUT_RELEASE
	CFLAGS += -DFIFO_DATATYPE=addr_t
	export FIFO_DATATYPE = addr_t
endif

export USE_EXTERNAL_BUFFER
ifeq ($(USE_EXTERNAL_BUFFER), y)
	CFLAGS += -DUSE_EXTERNAL_BUFFER
endif

export CLEAR_HDRINFO_IN_SEEK
ifeq ($(CLEAR_HDRINFO_IN_SEEK), y)
	CFLAGS += -DCLEAR_HDRINFO_IN_SEEK
endif

export USE_RANDOM_TEST
ifeq ($(USE_RANDOM_TEST), y)
	CFLAGS+=-DUSE_RANDOM_TEST
endif

export USE_FRAME_PRED_CHECK_INTERLACE
ifeq ($(USE_FRAME_PRED_CHECK_INTERLACE), y)
	CFLAGS += -DUSE_FRAME_PRED_CHECK_INTERLACE
endif

export ASIC_TRACE_SUPPORT
ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS += -DASIC_TRACE_SUPPORT
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
#versatile: CFLAGS += -O -g -mtune=arm926ej-s -fpic $(INCLUDE) $(MD5SUM) -D_LARGEFILE64_SOURCE
versatile: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_LARGEFILE64_SOURCE
versatile: TESTDEC=m2x170dec_versatile
versatile: TARGET_ENV=versatile
versatile: LIBS = $(DECLIBDIR) -lpthread -ldecx170m2 -ldwlx170 -ltbcommon -lutils $(EFENCE) $(MD5SUM)
versatile: test

.PHONY: pcie
pcie: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_LARGEFILE64_SOURCE
pcie: TESTDEC=m2x170dec_pcie
pcie: TARGET_ENV=pcie
pcie: LIBS = $(DECLIBDIR) -pthread -ldecx170m2 -ldwlx170 -ltbcommon -lutils $(EFENCE) $(MD5SUM)
pcie: test


.PHONY: integrator
integrator: CC=arm-linux-gcc
integrator: CFLAGS = -O -g -mtune=arm920tdmi -fpic  $(INCLUDE) $(MD5SUM)
integrator: TESTDEC=m2x170dec_integrator
integrator: TARGET_ENV=integrator
integrator: LIBS = $(DECLIBDIR) -lpthread -ldecx170m2 -ldwlx170 -ltbcommon -lutils $(EFENCE)
integrator: test


.PHONY: ads11
ads11: CC=armcc
ads11: CFLAGS = -g --cpu ARM1136J-S --no_inline -O3 -Otime -DMPEG2DEC_EXTERNAL_ALLOC_DISABLE $(INCLUDE)
ads11: TESTDEC=m2x170dec_ads11
ads11: TARGET_ENV=ads11
ads11: LIBS = -L../../linux/mpeg2/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../../../system/models/g1hw/$(MODLIB) -L../common/swhw/$(TBLIB) -D_ADS
ads11: test

.PHONY: ads9
ads9: CC=armcc
ads9: CFLAGS = -g --cpu ARM926EJ-S --no_inline -O3 -Otime -DMPEG2DEC_EXTERNAL_ALLOC_DISABLE $(INCLUDE)
ads9: TESTDEC=m2x170dec_ads9
ads9: TARGET_ENV=ads9
ads9: LIBS = -L../../linux/mpeg2/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../../../system/models/g1hw/$(MODLIB) -L../common/swhw/$(TBLIB) -D_ADS
ads9: test

.PHONY: verification
verification: SYSTEM_VERIFICATION_TEST = -DSYSTEM_VERIFICATION
verification: CFLAGS+=-DSYSTEM_VERIFICATION
verification: pclinux

.PHONY: pclinux
pclinux: CC=gcc
pclinux: CFLAGS+= -g $(INCLUDE) $(MD5SUM) $(M32) -D_FILE_OFFSET_BITS=64 -DEXPIRY_DATE=1$(expiry) -D_LARGEFILE64_SOURCE
pclinux: TESTDEC=m2x170dec_pclinux
pclinux: TARGET_ENV=pclinux
pclinux: LIBS = $(DECLIBDIR) -pthread -ldecx170m2 -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux: test

.PHONY: arm_pclinux
arm_pclinux: CC=aarch64-linux-gnu-gcc
arm_pclinux: CFLAGS+= -g $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -DEXPIRY_DATE=1$(expiry) -D_LARGEFILE64_SOURCE
arm_pclinux: TESTDEC=m2x170dec_arm_pclinux
arm_pclinux: TARGET_ENV=arm_pclinux
arm_pclinux: LIBS = $(DECLIBDIR) -pthread -ldecx170m2 -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
arm_pclinux: test

.PHONY: coverage
coverage: CC=covc --abs --retain gcc
coverage: CFLAGS = -O -g $(INCLUDE) $(MD5SUM) $(M32)
coverage: TESTDEC=m2x170dec_coverage
coverage: TARGET_ENV=coverage
coverage: LIBS = $(DECLIBDIR) -ldecx170m2 -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
coverage: test

.PHONY: pclinux_eval_8170
pclinux_eval_8170: CFLAGS += -DMPEG2_EVALUATION_8170
pclinux_eval_8170: pclinux_eval

.PHONY: pclinux_eval_8190
pclinux_eval_8190: CFLAGS += -DMPEG2_EVALUATION_8190
pclinux_eval_8190: pclinux_eval

.PHONY: pclinux_eval_9170
pclinux_eval_9170: CFLAGS += -DMPEG2_EVALUATION_9170
pclinux_eval_9170: pclinux_eval

.PHONY: pclinux_eval_9190
pclinux_eval_9190: CFLAGS += -DMPEG2_EVALUATION_9190
pclinux_eval_9190: pclinux_eval

.PHONY: pclinux_eval_G1
pclinux_eval_G1: CFLAGS += -DMPEG2_EVALUATION_G1
pclinux_eval_G1: pclinux_eval

.PHONY: pclinux_eval
pclinux_eval: CC=gcc
pclinux_eval: CFLAGS += $(INCLUDE) $(MD5SUM) $(M32) -DMPEG2_EVALUATION -D_FILE_OFFSET_BITS=64 -DEXPIRY_DATE=1$(expiry) -D_LARGEFILE64_SOURCE
pclinux_eval: TESTDEC=m2x170dec_pclinux_eval
pclinux_eval: TARGET_ENV=pclinux_eval
pclinux_eval: LIBS = $(DECLIBDIR) -ldecx170m2 -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: test

export SYSTEM_VERIFICATION_TEST

test:  $(OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(OBJS) $(LIBS) -o $(TESTDEC)

lib:
	make -C ../../linux/mpeg2 $(TARGET_ENV)
	make -C ../../linux/dwl $(TARGET_ENV)
	make -C ../common/swhw $(TARGET_ENV)
	make -C ../common/utils $(TARGET_ENV)

clean:
	rm -f *.o
	rm -f .depend
	rm -f m2x170dec_*

libclean:
	make -C ../../linux/dwl clean
	make -C ../../linux/mpeg2 clean
	make -C ../common/swhw clean
	make -C ../common/utils clean

depend .depend dep: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

ifeq (.depend, $(wildcard .depend))
include .depend
endif
