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

# Open below compiling options by default to support NXP's request
USE_64BIT_ENV = y
USE_OUTPUT_RELEASE = y
USE_EXTERNAL_BUFFER = y
USE_NON_BLOCKING = y
USE_ONE_THREAD_WAIT = y

# set this to 'y' for md5 checksum calculation of the frames instead of yuv
USE_MD5SUM = n

# set this to 'y' to enable WebM file format support
ENABLE_WEBM = n

SRCS = ivf.c vp8filereader.c vp8writeoutput.c vp8bufferalloc.c
SRCS_SINGLE = dectestbench.c
SRCS_MULTI = mcdectestbench.c
OBJS = $(SRCS:.c=.o)
OBJS_SINGLE = $(SRCS_SINGLE:.c=.o)
OBJS_MULTI = $(SRCS_MULTI:.c=.o)

INCLUDE = -I../../source/inc -I../../source/vp8 -I../../source/config \
          -I../../source/dwl -I../common/swhw \
          -I../../source/common -I../../linux/dwl \
          -I../common/utils

DECLIBDIR = -L../../linux/vp8 -L../../linux/dwl -L../common/swhw \
			-L../../../lib -L../common/utils
DECLIB = libdecx170vp8.a
DWLLIB = libdwlx170.a
TBLIB = libtbcommon.a
FIFO_DATATYPE="struct decoder_cb_data_ *"
DEFINES = -DFIFO_DATATYPE=$(FIFO_DATATYPE)
SYSLIBS = -lpthread -lrt

TESTDEC=vp8x170dec
TARGET_ENV=integrator

# Enable SW performance  measurement
#CFLAGS = -DSW_PERFORMANCE

ifeq ($(USE_MD5SUM), y)
        MD5SUM = -DMD5SUM
endif

ifeq ($(ENABLE_WEBM), y)
	WEBM = -DWEBM_ENABLED
	SRCS += nestegg/src/nestegg.c nestegg/halloc/src/halloc.c
	INCLUDE+=-Inestegg/include -Inestegg/halloc
	DIRCHECK = dircheck
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

#Set this for 64bit compilation
ifeq ($(USE_64BIT_ENV), y)
	export M32 = -m64
	CFLAGS += -DUSE_64BIT_ENV
endif

export USE_OUTPUT_RELEASE
ifeq ($(USE_OUTPUT_RELEASE), y)
	CFLAGS += -DUSE_OUTPUT_RELEASE
endif

export USE_EXTERNAL_BUFFER
ifeq ($(USE_EXTERNAL_BUFFER), y)
	CFLAGS += -DUSE_EXTERNAL_BUFFER
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
#versatile: CFLAGS += -O -g -mtune=arm926ej-s -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE $(WEBM) $(DEFINES)
versatile: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE $(WEBM) $(DEFINES)
versatile: TESTDEC=vp8x170dec_versatile
versatile: TARGET_ENV=versatile
versatile: LIBS = $(DECLIBDIR) -lpthread -ldecx170vp8 -ldwlx170 -ltbcommon -lutils $(SYSLIBS) $(EFENCE)
versatile: test

.PHONY: pcie
pcie: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE $(WEBM) $(DEFINES)
pcie: TESTDEC=vp8x170dec_pcie
pcie: TARGET_ENV=pcie
pcie: LIBS = $(DECLIBDIR) -pthread -ldecx170vp8 -ldwlx170 -ltbcommon -lutils $(EFENCE)
pcie: test


.PHONY: integrator
integrator: CC=arm-linux-gcc
integrator: CFLAGS += -O -g -mtune=arm920tdmi -fpic $(INCLUDE) $(MD5SUM) $(WEBM)
integrator: TESTDEC=vp8x170dec_integrator
integrator: TARGET_ENV=integrator
integrator: LIBS = $(DECLIBDIR) -lpthread -ldecx170vp8 -ldwlx170 -ltbcommon -lutils $(SYSLIBS) $(EFENCE)
integrator: test

.PHONY: ads11
ads11: CC=armcc
ads11: CFLAGS = --no_inline --cpu ARM1136J-S -O2 -Otime $(INCLUDE) -DADS_PERFORMANCE_SIMULATION $(WEBM) $(DEFINES)
ads11: TESTDEC=vp8x170dec_ads11
ads11: TARGET_ENV=ads11
ads11: LIBS = -L../../linux/vp8/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../common/swhw/$(TBLIB)
ads11: test

.PHONY: ads9
ads9: CC=armcc
ads9: CFLAGS = --no_inline --cpu ARM926EJ-S -O2 -Otime $(INCLUDE) -DADS_PERFORMANCE_SIMULATION $(WEBM) $(DEFINES)
ads9: TESTDEC=vp8x170dec_ads9
ads9: TARGET_ENV=ads9
ads9: LIBS = -L../../linux/vp8/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../common/swhw/$(TBLIB)
ads9: test

.PHONY: ads
ads: CC=armcc
ads: CFLAGS = --no_inline --cpu ARM1136J-S -O3 -Otime $(INCLUDE) -DADS_PERFORMANCE_SIMULATION $(WEBM) $(DEFINES)
ads: TESTDEC=vp8x170dec_ads
ads: TARGET_ENV=ads
ads: LIBS = -L../../linux/vp8/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../common/swhw/$(TBLIB)
ads: test

.PHONY: pclinux
pclinux: CC=gcc
pclinux: CFLAGS += -O0 -g $(INCLUDE) $(MD5SUM) $(M32) $(DEFINES) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry) $(WEBM)
pclinux: TESTDEC=vp8x170dec_pclinux
pclinux: TARGET_ENV=pclinux
pclinux: LIBS = $(DECLIBDIR) -pthread -ldecx170vp8 -ldwlx170 -l8170hw -ltbcommon -lutils $(SYSLIBS) $(EFENCE)
pclinux: test

.PHONY: arm_pclinux
arm_pclinux: CC=aarch64-linux-gnu-gcc
arm_pclinux: CFLAGS += -O0 -g $(INCLUDE) $(MD5SUM) $(DEFINES) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry) $(WEBM)
arm_pclinux: TESTDEC=vp8x170dec_arm_pclinux
arm_pclinux: TARGET_ENV=arm_pclinux
arm_pclinux: LIBS = $(DECLIBDIR) -pthread -ldecx170vp8 -ldwlx170 -l8170hw -ltbcommon -lutils $(SYSLIBS) $(EFENCE)
arm_pclinux: test

.PHONY: coverage
coverage: CC=covc --abs --retain gcc
coverage: CFLAGS = -O -g $(INCLUDE) $(MD5SUM) $(M32) $(WEBM) $(DEFINES)
coverage: TESTDEC=vp8x170dec_coverage
coverage: TARGET_ENV=coverage
coverage: LIBS = $(DECLIBDIR) -ldecx170vp8 -ldwlx170 -l8170hw -ltbcommon -lutils $(SYSLIBS) $(EFENCE)
coverage: test

.PHONY: pclinux_eval_G1
pclinux_eval_G1: CFLAGS += -DVP8_EVALUATION_G1 $(WEBM) $(DEFINES)
pclinux_eval_G1: pclinux_eval

.PHONY: pclinux_eval
pclinux_eval: CC=gcc
pclinux_eval: CFLAGS += $(INCLUDE) $(MD5SUM) $(M32) -DVP8_EVALUATION -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DEXPIRY_DATE=1$(expiry) $(WEBM) $(DEFINES)
pclinux_eval: TESTDEC=vp8x170dec_pclinux_eval
pclinux_eval: TARGET_ENV=pclinux_eval
pclinux_eval: LIBS = $(DECLIBDIR) -ldecx170vp8 -ldwlx170 -l8170hw -ltbcommon -lutils $(SYSLIBS) $(EFENCE)
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: test

export FIFO_DATATYPE

.PHONY: test
test: $(DIRCHECK) $(OBJS) $(OBJS_SINGLE) $(OBJS_MULTI)
	$(CC) $(LDFLAGS) $(CFLAGS) $(INCLUDE) $(OBJS_MULTI) $(OBJS) $(LIBS) -o $(TESTDEC)-mc
	$(CC) $(LDFLAGS) $(CFLAGS) $(INCLUDE) $(OBJS_SINGLE) $(OBJS) $(LIBS) -o $(TESTDEC)

.PHONY: dircheck
dircheck:
	@ if ! test -d ./nestegg; then\
		echo "";\
		echo "In order to enable WebM support, nestegg library must be installed:";\
		echo "";\
		echo "git clone https://github.com/kinetiknz/nestegg.git";\
		echo "cd nestegg";\
		echo "autoreconf --install";\
		echo "./configure";\
		echo "cd ..";\
		echo "";\
		echo "Note: nestegg configuration uses autoconf and libtool. These can be downloaded from:";\
		echo "";\
		echo "  - http://www.gnu.org/s/autoconf/";\
		echo "  - http://www.gnu.org/software/libtool/";\
		echo "";\
		exit 1;\
	fi

lib:
	make -C ../../linux/vp8 $(TARGET_ENV)
	make -C ../../linux/dwl $(TARGET_ENV)
	make -C ../common/swhw $(TARGET_ENV)
	make -C ../common/utils CC=$(CC) $(TARGET_ENV)

.PHONY: clean libclean
clean:
	rm -f *.o nestegg/src/nestegg.o nestegg/halloc/src/halloc.o
	rm -f .depend
	rm -f vp8x170dec_*

libclean:
	make -C ../../linux/vp8 clean
	make -C ../../linux/dwl clean
	make -C ../common/swhw clean
	make -C ../common/utils clean

depend .depend dep: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

ifeq (.depend, $(wildcard .depend))
include .depend
endif
