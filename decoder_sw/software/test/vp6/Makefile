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

# set this to 'y' to enable md5 checksum calculation (comandline option)
USE_MD5SUM = y

SRCS = on2vp6dec.c

OBJS    = $(SRCS:.c=.o)

INCLUDE = -I. -I../../source/inc \
          -I../../source/vp6 \
          -I../../source/config \
          -I../../source/common \
          -I../common/swhw \
          -I../common/utils

LIBDECDIR = -L../../linux/vp6 \
            -L../../linux/dwl \
            -L../common \
            -L../common/swhw \
            -L../common/utils

LIBDEC = dec8190vp6

TESTDEC=vp6dec
TARGET_ENV=integrator

# reference coded root  for hw model building; export

LIB_REF=on2vpplugin6
LIB_MODEL=vp6hwmodel

DECLIBDIR = -L../../linux/vp6 -L../../linux/dwl -L../common/swhw \
	    -L../../../lib -L../common/utils
DECLIB = libdec8190vp6.a

# Enable SW performance  measurement
#CFLAGS = -DSW_PERFORMANCE

ifeq ($(USE_MD5SUM), y)
        MD5SUM = -DMD5SUM
endif

ifeq ($(shell uname -m),x86_64)
	ifneq (,$(findstring pclinux,$(MAKECMDGOALS)))
		M32 = -m32
	endif
endif

ifeq ($(USE_64BIT_ENV), y)
	M32 = -m64
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
	CFLAGS+=-DUSE_EXTERNAL_BUFFER
endif

export ASIC_TRACE_SUPPORT
ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS+=-DASIC_TRACE_SUPPORT
endif

export USE_PICTURE_DISCARD
ifeq ($(USE_PICTURE_DISCARD), y)
	CFLAGS+=-DUSE_PICTURE_DISCARD
endif

export USE_NON_BLOCKING
ifeq ($(USE_NON_BLOCKING), y)
	CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK
endif

export USE_ONE_THREAD_WAIT
ifeq ($(USE_ONE_THREAD_WAIT), y)
	CFLAGS += -DGET_OUTPUT_BUFFER_NON_BLOCK
endif

export M32
export SET_EMPTY_PICTURE_DATA

ARMCPU=ARM1136J-S

all: versatile

.PHONY: versatile
#versatile: CC=arm-none-linux-gnueabi-gcc
versatile: CC=aarch64-linux-gnu-gcc
#versatile: CFLAGS += -O -g -mtune=arm926ej-s -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64
versatile: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64
versatile: TESTDEC=vp6dec_versatile
versatile: TARGET_ENV=versatile
versatile: DWL_TARGET_ENV=versatile
versatile: LIBS = $(LIBDECDIR) -lpthread -l$(LIBDEC) -ldwlx170  -ltbcommon -lutils $(EFENCE)
versatile: test

.PHONY: pcie
pcie: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_LARGEFILE64_SOURCE
pcie: TESTDEC=vp6dec_pcie
pcie: TARGET_ENV=pcie
pcie: LIBS = $(DECLIBDIR) -pthread -l$(LIBDEC) -ldwlx170 -ltbcommon -lutils $(EFENCE) $(MD5SUM)
pcie: test


.PHONY: integrator
integrator: CC=arm-linux-gcc
integrator: CFLAGS += -O -g -mtune=arm920tdmi -fpic $(INCLUDE) $(MD5SUM)
integrator: TESTDEC=vp6dec_integrator
integrator: TARGET_ENV=integrator
integrator: DWL_TARGET_ENV=integrator
integrator: LIBS = $(LIBDECDIR) -lpthread -l$(LIBDEC) -ldwlx170 -ltbcommon -lutils $(EFENCE)
integrator: test

.PHONY: ads
ads: CC=armcc
ads: CFLAGS = --cpu $(ARMCPU) -g -O2 -Otime --no_inline $(INCLUDE) -DADS_PERFORMANCE_SIMULATION
ads: TESTDEC=vp6dec_ads
ads: TARGET_ENV=ads
ads: DWL_TARGET_ENV=ads
ads: LIBS = $(LIBDECDIR) -lpthread -l$(LIBDEC) -ldwlx170 -ltbcommon -lutils $(EFENCE)
ads: ASIC_TRACE_SUPPORT=
ads: lib $(OBJS)
	armlink --noremove $(OBJS) $(LIBS) -o $(TESTDEC).axf

ads9: override ARMCPU=ARM926EJ-S
ads9: ads

ads11: override ARMCPU=ARM1136J-S
ads11: ads

.PHONY: pclinux
pclinux: CC=gcc
pclinux: CFLAGS+= -g $(INCLUDE) $(MD5SUM) $(M32) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry)
pclinux: TESTDEC=vp6dec_pclinux
pclinux: TARGET_ENV=pclinux
pclinux: LIBS = $(DECLIBDIR) -pthread -ldec8190vp6 -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux: test

.PHONY: arm_pclinux
arm_pclinux: CC=aarch64-linux-gnu-gcc
arm_pclinux: CFLAGS+= -g $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry)
arm_pclinux: TESTDEC=vp6dec_arm_pclinux
arm_pclinux: TARGET_ENV=arm_pclinux
arm_pclinux: LIBS = $(DECLIBDIR) -pthread -ldec8190vp6 -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
arm_pclinux: test

.PHONY: pclinux_eval_9190
pclinux_eval_9190: CFLAGS += -DVP6_EVALUATION_9190
pclinux_eval_9190: _PRODUCT_9190 = y
pclinux_eval_9190: pclinux_eval

.PHONY: pclinux_eval_G1
pclinux_eval_G1: CFLAGS += -DVP6_EVALUATION_G1
pclinux_eval_G1: _PRODUCT_G1 = y
pclinux_eval_G1: pclinux_eval

.PHONY: pclinux_eval
pclinux_eval: CFLAGS = -g $(INCLUDE) $(MD5SUM) $(M32)-D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry) -DVP6_EVALUATION
pclinux_eval: TESTDEC=vp6dec_pclinux_eval
pclinux_eval: TARGET_ENV=pclinux_eval
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: LIBS = $(DECLIBDIR) -ldec8190vp6 -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux_eval: test

export _PRODUCT_9190
export _PRODUCT_G1
export TESTDEC

test: $(OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(OBJS) $(LIBS) -o $(TESTDEC)

lib:
	make -C ../../linux/vp6 $(TARGET_ENV)
	make -C ../../linux/dwl $(TARGET_ENV)
	make -C ../common/swhw $(TARGET_ENV)
	make -C ../common/utils $(TARGET_ENV)

clean:
	rm -f *.o core.*
	rm -f .depend
	rm -f vp6dec_*

libclean:
	make -C ../../linux/vp6 clean
	make -C ../common/swhw clean
	make -C ../../linux/dwl clean
	make -C ../common/utils clean

depend .depend dep: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

ifeq (.depend, $(wildcard .depend))
include .depend
endif
