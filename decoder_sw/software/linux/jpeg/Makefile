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

#--  Abstract : Makefile for x170 JPEG decoder control SW
#--
#-------------------------------------------------------------------------------

# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Error resilience tool
ERROR_RESILIENCE  = n

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -g -DDEBUG -D_ASSERT_USED -D_RANGE_CHECK -D_ERROR_PRINT
else
  DEBFLAGS = -O3 -DNDEBUG
endif

# Add error resilience flag (or not) to CFLAGS
ifeq ($(ERROR_RESILIENCE),y)
  DEBFLAGS += -DJPEGDEC_ERROR_RESILIENCE
endif

#DEBFLAGS += -DJPEGDEC_TRACE # for API tracing (JpegDecTrace has to be implemented)
#DEBFLAGS += -DJPEGDEC_ASIC_TRACE # for JPEG/HW data tracing
#DEBFLAGS += -DJPEGDEC_TRACE_INTERNAL # for JPEG/HW data tracing
#DEBFLAGS += -DJPEGDEC_API_TRC # for control trace
#DEBFLAGS += -D_DEBUG_PRINT -D_ERROR_PRINT # decoder trace
#DEBFLAGS += -DMEMORY_USAGE_TRACE #trace all memory allocations
#DEBFLAGS += -DJPEGDEC_INTEGRATOR # for integrator
#DEBFLAGS += -DJPEGDEC_PP_TRACE # for PP Tracing
#DEBFLAGS += -D_ASSERT_USED

# Define this for performance measurement
#DEBFLAGS += -DJPEGDEC_PERFORMANCE # for performance

# Define this for testing error cases
#DEBFLAGS += -DJPEGDEC_RESET_OUTPUT

COMMON_SOURCE_DIR = ../../source

# Architecture flags for gcc
ARCH =
CROSS_COMPILER =

# C -compiler name, can be replaced by another compiler(replace gcc)
CC = $(CROSS_COMPILER)gcc

# show the path to compiler, where to find header files and libraries
INCLUDE = -I. -I$(COMMON_SOURCE_DIR)/inc -I$(COMMON_SOURCE_DIR)/dwl \
          -I$(COMMON_SOURCE_DIR)/jpeg -I$(COMMON_SOURCE_DIR)/config \
          -I$(COMMON_SOURCE_DIR)/common \
          -I$(COMMON_SOURCE_DIR)/../linux/memalloc_g1


# compiler switches
CFLAGS += -Wall -Wextra -std=c99 -pedantic -fPIC
CFLAGS += $(ARCH) -D_GNU_SOURCE $(DEBFLAGS) $(INCLUDE)

ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS+=-DASIC_TRACE_SUPPORT
	INCLUDE+=-I../../test/common/swhw
endif

# set for 64bit compilation
ifeq ($(M32), -m64)
	CFLAGS+=-DUSE_64BIT_ENV
endif

# list of used sourcefiles
SRC_JPEG := \
    jpegdecapi.c \
    jpegdechdrs.c \
    jpegdecinternal.c \
    jpegdecscan.c \
    jpegdecutils.c \
    regdrv_g1.c \
    commonconfig_g1.c

SRC_JPEG_TRACE := jpegasicdbgtrace.c

ifeq ($(DEBUG),y)
	SRCS = $(SRC_JPEG) $(SRC_JPEG_TRACE)
else
	SRCS = $(SRC_JPEG)
endif

#source search path
vpath %.c
vpath %.c $(COMMON_SOURCE_DIR)/jpeg $(COMMON_SOURCE_DIR)/common

vpath %.o
vpath %.o obj

# object files will be generated from .c sourcefiles
OBJS    = $(SRCS:.c=.o)

# name of the outputfile (library)
DECLIB = libx170j.a

# MACRO for cleaning object -files
RM  = rm -f

# MACRO for creating library that includes all the object files
#AR  = ar rcv
AR  = $(CROSS_COMPILER)ar rcs

OBJDIR := obj

#Here are rules for building codes and generating object library.
all:
	@echo
	@echo ERROR! run one of:
	@echo "    $$ make integrator"
	@echo "    $$ make versatile"
	@echo "    $$ make pclinux"
	@echo "    $$ make ads"
	@echo "    $$ make pclinux_eval"	
	@echo "    $$ make arm_pclinux"
	@echo

$(OBJDIR):
	mkdir $@

.PHONY: ads
ads: CC=armcc
ads: AR=armar -rcv
ads: CFLAGS =  -cpu ARM926EJ-S -O2 -Otime $(INCLUDE)
ads: $(DECLIB)

ads11: CC=armcc
ads11: AR=armar -r -c -v
ads11: CFLAGS = -g --cpu ARM1136J-S -O2 -Otime $(INCLUDE)
ads11: $(DECLIB)

ads9: CC=armcc
ads9: AR=armar -r -c -v
ads9: CFLAGS = -g --cpu ARM926EJ-S -O2 -Otime $(INCLUDE)
ads9: $(DECLIB)

pc_plain_lib: OBJS = $(SRC_JPEG:.c=.o)
pc_plain_lib: $(DECLIB)

pcie: CC=gcc
pcie: $(DECLIB)

.PHONY: pclinux
pclinux: CFLAGS += -DPJPEG_COMPONENT_TRACE
pclinux: $(DECLIB)

.PHONY: arm_pclinux
arm_pclinux: CROSS_COMPILER = aarch64-linux-gnu-
arm_pclinux: M32=
arm_pclinux: CFLAGS += -DPJPEG_COMPONENT_TRACE
arm_pclinux: $(DECLIB)

.PHONY: integrator
integrator: ENVSET  = -DSDRAM_LM_BASE=0x80000000 \
                      -DDEC_MODULE_PATH=\"/dev/hx170dec\" \
                      -DDEC_IO_BASE=0xD0000000
integrator: INCLUDE += -Ildriver/kernel_24x # linux kernel 2.4.x
integrator: ARCH = -mtune=arm9tdmi -fpic
integrator: CROSS_COMPILER = arm-linux-
integrator: $(DECLIB)

.PHONY: versatile
versatile: ENVSET  = -DSDRAM_LM_BASE=0x00000000 \
                     -DDEC_MODULE_PATH=\"/dev/hx170dec\" \
                     -DDEC_IO_BASE=0xC0000000
#versatile: CROSS_COMPILER = arm-none-linux-gnueabi-
versatile: CROSS_COMPILER = aarch64-linux-gnu-
versatile: M32=
#versatile: ARCH = -mtune=arm926ej-s -fpic
versatile: $(DECLIB)

.PHONY: pclinux_eval
pclinux_eval: INCLUDE += -I../../test/common/swhw/
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: $(DECLIB)
pclinux_eval: CC=gcc


$(DECLIB): depend $(OBJDIR) $(OBJS)
	$(AR) $(DECLIB) $(patsubst %,$(OBJDIR)/%, $(OBJS))

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $(ENVSET) $< -o $(OBJDIR)/$@


.PHONY: clean
clean:
	$(RM) $(DECLIB)
	$(RM) .depend
	$(RM) -r $(OBJDIR)


lint: $(SRC_JPEG)
        # -e537 removes warnings about multiple include
        # -e641 removes warnings about conevrting enum to int
	-lint-nt -w2 -e537 -e641 $(INCLUDE) $^ > pc-lint-report.txt


depend: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

ifeq (.depend, $(wildcard .depend))
include .depend
endif
