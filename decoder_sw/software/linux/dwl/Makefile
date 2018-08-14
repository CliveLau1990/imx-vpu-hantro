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

#--  Abstract : Makefile for DWL
#--
#-------------------------------------------------------------------------------

# Comment/uncomment the following line to disable/enable debugging
DEBUG = n

# set this to 'y' for enabling IRQ mode for the decoder. You will need
# the hx170dec kernel driver loaded and a /dev/hx170 device node created
USE_DEC_IRQ = y

# set this 'y' for enabling sw reg tracing. NOTE! not all sw reagisters are 
# traced; only hw "return" values.
INTERNAL_TEST = n

# set this 'y' to enable G1 asic ID (0x6731).
_PRODUCT_G1 = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O0 -g -DDEBUG
else
  DEBFLAGS = -O2 -DNDEBUG
endif
SC_SW_ONLY = y
#DEBFLAGS += -D_STREAM_POS_TRACE
#DEBFLAGS += -D_DWL_DEBUG # for wrapper layer trace
#DEBFLAGS += -DMEMORY_USAGE_TRACE #trace all memory allocations
#DEBFLAGS += -D_READ_DEBUG_REGS
#DEBFLAGS += -D_DWL_FAKE_HW_TIMEOUT # Change stream error interrupt to hw timeouts

DEBFLAGS += -DDWL_DISABLE_REG_PRINTS # Do not trace all register accesses

DEBFLAGS += -DUSE_DDD_DEBUGGER # Enable when debugging with DDD debugger

# Enable HW performance measurements (only in interrupt mode)
ifeq ($(USE_DEC_IRQ),y)
#  DEBFLAGS += -D_DWL_HW_PERFORMANCE
endif

ifeq ($(INTERNAL_TEST),y)
   DEBFLAGS += -DINTERNAL_TEST
   SRCS +=internal_test.c
endif

# 2 cores supported by default
CORES ?= 2

# system independent code location
COMMON_SOURCE_DIR = ../../source

# Architecture flags for gcc
ARCH =
CROSS =

# C -compiler name, can be replaced by another compiler(replace gcc)
CC = $(CROSS)gcc

# MACRO for cleaning object -files
RM  = rm -f

# MACRO for creating library that includes all the object files
AR  = $(CROSS)ar rcs

# show the path to compiler, where to find header files and libraries
INCLUDE = -I. \
          -I$(COMMON_SOURCE_DIR)/inc \
          -I$(COMMON_SOURCE_DIR)/common \
          -I../memalloc \
          -I../ldriver \
          -I../../test/common/utils

# compiler switches
CFLAGS  = -Wall -Wextra -fPIC $(ARCH) $(DEBFLAGS) $(INCLUDE)
# disable missing field initializer from causing error since it is interpreted
# wrongly by some older GCC versions.
CFLAGS += -Wno-missing-field-initializers
CFLAGS += -DCORES=$(CORES)

ifdef DWL_PRESET_FAILING_ALLOC
	CFLAGS += -DDWL_PRESET_FAILING_ALLOC=$(DWL_PRESET_FAILING_ALLOC)
endif

ifeq ($(USE_DEC_IRQ),y)
	CFLAGS += -DDWL_USE_DEC_IRQ
endif

ifeq ($(DISABLE_REFBUFFER),y)
	CFLAGS += -DDWL_REFBUFFER_DISABLE # Disable reference frame buffering
endif

# list of used sourcefiles
ifeq ($(SC_SW_ONLY),y)
	SRC_DWL_ARM = dwl_linux.c dwl_linux_sc.c dwl_activity_trace.c
	CFLAGS+=-DDWL_SINGLE_CORE_SW_ONLY
else
	SRC_DWL_ARM = dwl_linux.c dwl_linux_mc.c dwl_activity_trace.c
endif

SRC_DWL_SIMULATION := dwl_pc.c \
                      dwl_hw_core_array.c \
                      dwl_hw_core.c \
                      dwl_swhw_sync.c \
                      dwl_activity_trace.c

# simulation target settings
ifneq (,$(findstring pclinux,$(MAKECMDGOALS)))
	INCLUDE += -I../../../system/models/g1hw
ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS+=-DASIC_TRACE_SUPPORT
	INCLUDE+=-I../../test/common/swhw
endif
endif

#ifeq ($(shell uname -m),x86_64)
#	M32=-m32
#endif

# set for 64bit compilation
ifeq ($(M32), -m64)
    CFLAGS += -DUSE_64BIT_ENV
endif

#set these for evaluation
ifeq ($(_PRODUCT_8170), y)
    CFLAGS += -DDWL_EVALUATION_8170
endif

ifeq ($(_PRODUCT_8190), y)
    CFLAGS += -DDWL_EVALUATION_8190
endif

ifeq ($(_PRODUCT_9170), y)
    CFLAGS += -DDWL_EVALUATION_9170
endif

ifeq ($(_PRODUCT_9190), y)
    CFLAGS += -DDWL_EVALUATION_9190
endif

ifeq ($(_PRODUCT_G1), y)
    CFLAGS += -DDWL_EVALUATION_G1
endif

ifneq (,$(findstring pclinux, $(MAKECMDGOALS)))
    SRCS += $(SRC_DWL_SIMULATION)
else
    SRCS += $(SRC_DWL_ARM)
endif

#source search path
vpath %.o
vpath %.o obj

# object files will be generated from .c sourcefiles
OBJS    = $(SRCS:.c=.o)

# name of the outputfile (library)
DECLIB = libdwlx170.a

OBJDIR := obj

#Here are rules for building codes and generating object library.
all:
	@echo
	@echo Specify:
	@echo "    $$ make linaro"
	@echo "    $$ make versatile"
	@echo "    $$ make pclinux"
	@echo "    $$ make pclinux_eval"
	@echo "    $$ make arm_pclinux"
	@echo

$(OBJDIR):
	mkdir $@

pclinux: CFLAGS += $(M32)
pclinux: $(DECLIB)
#	make -C ../../../system/models/g1hw
	
arm_pclinux: CROSS = aarch64-linux-gnu-
arm_pclinux: $(DECLIB)
#	make -C ../../../system/models/g1hw arm_pclinux

pclinux_eval: CFLAGS += $(M32)
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: $(DECLIB)
	make -C ../../../system/models/g1hw pclinux_eval

.PHONY: coverage
coverage: CFLAGS += $(M32)
coverage: CC=covc --abs --retain gcc
coverage: $(DECLIB)
	make -C ../../../system/models/g1hw


versatile: ENVSET  = -DDEC_MODULE_PATH=\"/tmp/dev/hantrodec\" \
                     -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
#versatile: CROSS = arm-none-linux-gnueabi-
versatile: CROSS = aarch64-linux-gnu-
#versatile: ARCH = -mtune=arm926ej-s
versatile: $(DECLIB)

linaro: ENVSET  = -DDEC_MODULE_PATH=\"/tmp/dev/hx170\" \
                  -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
linaro: CROSS = arm-linux-gnueabi-
linaro: ARCH = -march=armv7-a
linaro: $(DECLIB)

pcie: ENVSET  = -DDEC_MODULE_PATH=\"/tmp/dev/hx170\" \
                  -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
pcie: INCLUDE += -I../ldriver/kernel_26x
pcie: CFLAGS  += -lpthread
pcie: $(DECLIB)



$(DECLIB): .depend $(OBJDIR) $(OBJS)
	$(AR) $(DECLIB) $(patsubst %,$(OBJDIR)/%, $(OBJS))

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $(ENVSET) $< -o $(OBJDIR)/$(@F)

clean:
	$(RM) $(DECLIB)
	$(RM) .depend
	$(RM) -r $(OBJDIR)
#	@-make -C ../../../system/models/g1hw/ clean 2>/dev/null

depend .depend: $(SRCS)
	$(CC) $(CFLAGS) $(ENVSET) -M  $^ > .depend

tar:
	$(RM) dwlx170.tar
	tar -cf dwlx170.tar $(DECLIB)

.PHONY: pclinux pclinux_eval versatile linaro clean lint tar

ifneq ( , $( findstring clean , $(MAKECMDGOALS) ))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif

