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

SRCS = dectestbench.c
OBJS    = $(SRCS:.c=.o)

INCLUDE = -I../../source/inc -I../../source/jpeg -I../../source/dwl \
	  -I../../source/config -I../../source/common -I../common/swhw

DECLIBDIR = -L../../linux/jpeg -L../../linux/dwl -L../common/swhw \
	    -L../../../lib -L../common/utils
DECLIB = libx170j.a
DWLLIB = libdwlx170.a
TBLIB = libtbcommon.a
MODLIB = lib8170hw.a

TESTDEC=jpegdec
TARGET_ENV=

# Enable SW performance  measurement
#CFLAGS = -DSW_PERFORMANCE

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

all: versatile

.PHONY: versatile
#versatile: CC=arm-none-linux-gnueabi-gcc
versatile: CC=aarch64-linux-gnu-gcc
#versatile: CFLAGS += -O -g -mtune=arm926ej-s -fpic $(INCLUDE) -DLINUX -DSDRAM_LM_BASE=0x00000000 \
                      -DDEC_MODULE_PATH=\"/dev/hx170dec\" -DDEC_IO_BASE=0xC0000000
versatile: CFLAGS += -O -g -fpic $(INCLUDE) -DLINUX -DSDRAM_LM_BASE=0x00000000 \
                      -DDEC_MODULE_PATH=\"/dev/hx170dec\" -DDEC_IO_BASE=0xC0000000
versatile: TESTDEC=jx170dec_versatile
versatile: TARGET_ENV=versatile
versatile: LIBS = $(DECLIBDIR) -lpthread -lx170j -ldwlx170 -ltbcommon -lutils $(EFENCE)
versatile: test

.PHONY: pcie
pcie: CFLAGS += -O -g -fpic $(INCLUDE) $(MD5SUM) -D_LARGEFILE64_SOURCE
pcie: TESTDEC=jx170dec_pcie
pcie: TARGET_ENV=pcie
pcie: LIBS = $(DECLIBDIR) -pthread -lx170j -ldwlx170 -ltbcommon -lutils $(EFENCE) $(MD5SUM)
pcie: test


.PHONY: integrator
integrator: CC=arm-linux-gcc
integrator: CFLAGS += -O -g -mtune=arm920t -fpic $(INCLUDE) -DLINUX -DSDRAM_LM_BASE=0x80000000 \
                      -DDEC_MODULE_PATH=\"/dev/hx170dec\" -DDEC_IO_BASE=0xD0000000
integrator: TESTDEC=jx170dec_integrator
integrator: TARGET_ENV=integrator
integrator: LIBS = $(DECLIBDIR) -lpthread -lx170j -ldwlx170 -ltbcommon -lutils $(EFENCE)
integrator: test


.PHONY: ads
ads: CC=armcc
ads: CFLAGS = -g --cpu ARM926EJ-S --no_inline -O3 -Otime $(INCLUDE)
ads: TESTDEC=jx170dec_ads
ads: TARGET_ENV=ads
ads: LIBS = -L../../linux/jpeg/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../../../system/models/g1hw/$(MODLIB) -L../common/swhw/$(TBLIB) -D_ADS 
ads: test

.PHONY: ads11
ads11: CC=armcc
ads11: CFLAGS = -g --cpu ARM1136J-S --no_inline -O3 -Otime $(INCLUDE)
ads11: TESTDEC=jx170dec_ads11
ads11: TARGET_ENV=ads11
ads11: LIBS = -L../../linux/jpeg/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../../../system/models/g1hw/$(MODLIB) -L../common/swhw/$(TBLIB) -D_ADS
ads11: test

.PHONY: ads9
ads9: CC=armcc
ads9: CFLAGS = -g --cpu ARM926EJ-S --no_inline -O3 -Otime $(INCLUDE)
ads9: TESTDEC=jx170dec_ads9
ads9: TARGET_ENV=ads9
ads9: LIBS = -L../../linux/jpeg/$(DECLIB) -L../../linux/dwl/$(DWLLIB) -L../../../system/models/g1hw/$(MODLIB) -L../common/swhw/$(TBLIB) -D_ADS
ads9: test

.PHONY: pclinux
pclinux: CC=gcc
pclinux: CFLAGS += -O -g $(INCLUDE) $(M32) -DLINUX -DEXPIRY_DATE=1$(expiry)
pclinux: TESTDEC=jx170dec_pclinux
pclinux: TARGET_ENV=pclinux
pclinux: LIBS = $(DECLIBDIR) -pthread -lx170j -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux: test

.PHONY: arm_pclinux
arm_pclinux: CC=aarch64-linux-gnu-gcc
arm_pclinux: CFLAGS += -O -g $(INCLUDE) -DLINUX -DEXPIRY_DATE=1$(expiry)
arm_pclinux: TESTDEC=jx170dec_arm_pclinux
arm_pclinux: TARGET_ENV=arm_pclinux
arm_pclinux: LIBS = $(DECLIBDIR) -pthread -lx170j -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
arm_pclinux: test

.PHONY: verification
verification: SYSTEM_VERIFICATION_TEST = -DSYSTEM_VERIFICATION
verification: CFLAGS+=-DSYSTEM_VERIFICATION
verification: pclinux

.PHONY: pclinux_eval_8170
pclinux_eval_8170: CFLAGS += -DJPEG_EVALUATION_8170
pclinux_eval_8170: pclinux_eval

.PHONY: pclinux_eval_8190
pclinux_eval_8190: CFLAGS += -DJPEG_EVALUATION_8190
pclinux_eval_8190: pclinux_eval

.PHONY: pclinux_eval_9170
pclinux_eval_9170: CFLAGS += -DJPEG_EVALUATION_9170
pclinux_eval_9170: pclinux_eval

.PHONY: pclinux_eval_9190
pclinux_eval_9190: CFLAGS += -DJPEG_EVALUATION_9190
pclinux_eval_9190: pclinux_eval

.PHONY: pclinux_eval_G1
pclinux_eval_G1: CFLAGS += -DJPEG_EVALUATION_G1
pclinux_eval_G1: pclinux_eval

.PHONY: pclinux_eval
pclinux_eval: CC=gcc
pclinux_eval: CFLAGS += $(INCLUDE) $(M32) -DLINUX -DJPEG_EVALUATION -DEXPIRY_DATE=1$(expiry)
pclinux_eval: TESTDEC=jx170dec_pclinux_eval
pclinux_eval: TARGET_ENV=pclinux_eval
pclinux_eval: LIBS = $(DECLIBDIR) -lx170j -ldwlx170 -l8170hw -ltbcommon -lutils $(EFENCE)
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: test

export SYSTEM_VERIFICATION_TEST

test:  $(OBJS) 
	$(CC) $(LDFLAGS) $(CFLAGS) $(OBJS) $(LIBS) -o $(TESTDEC) 

lib:
	make -C ../../linux/jpeg $(TARGET_ENV)
	make -C ../../linux/dwl $(TARGET_ENV)
	make -C ../common/swhw $(TARGET_ENV)
	make -C ../common/utils $(TARGET_ENV)
        
clean:
	rm -f *.o
	rm -f .depend
	rm -f jx170dec_*
        
libclean:
	make -C ../../linux/jpeg clean
	make -C ../../linux/dwl clean
	make -C ../common/swhw clean
	make -C ../common/utils clean

depend .depend dep: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend
        
ifeq (.depend, $(wildcard .depend))
include .depend
endif
