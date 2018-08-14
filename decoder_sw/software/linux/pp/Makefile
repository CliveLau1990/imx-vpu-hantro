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

#--  Abstract : Makefile for x170 PP decoder control SW
#--
#-------------------------------------------------------------------------------

# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Add your debugging flags (or not) to CFLAGS. Do not change these!
ifeq ($(DEBUG),y)
    DEBFLAGS = -g -DDEBUG -D_ASSERT_USED
else
    DEBFLAGS = -O3 -DNDEBUG
endif

# Add your debugging flag (or not) to CFLAGS. Enable, if needed
#DEBFLAGS += -DPP_TRACE
#DEBFLAGS += -D_DWL_DEBUG # for wrapper layer trace
#DEBFLAGS += -DPP_ASIC_TRACE # trace ASIC data
#DEBFLAGS += -D_DEBUG_PRINT -D_ERROR_PRINT # decoder trace
#DEBFLAGS += -DMEMORY_USAGE_TRACE #trace all memory allocations
#DEBFLAGS += -D_X170_DEMO_DEVEL # Enable demo development functionality


# Postprocessor and decoder pipeline support

#PIPELINE_SUPPORT += -DPP_H264DEC_PIPELINE_SUPPORT # H.264 decoder support
#PIPELINE_SUPPORT += -DPP_MPEG4DEC_PIPELINE_SUPPORT # MPEG4 decoder support
#PIPELINE_SUPPORT += -DPP_JPEGDEC_PIPELINE_SUPPORT # JPEG decoder support
#PIPELINE_SUPPORT += -DPP_VC1DEC_PIPELINE_SUPPORT # VC-1 decoder support
#PIPELINE_SUPPORT += -DPP_MPEG2DEC_PIPELINE_SUPPORT # MPEG2 decoder support
#PIPELINE_SUPPORT += -DPP_RVDEC_PIPELINE_SUPPORT # REAL decoder support

#Set up environment:

COMMON_SOURCE_DIR = ../../source

# Architecture flags for gcc
ARCH =
CROSS_COMPILER =

# C -compiler name, can be replaced by another compiler(replace gcc)
CC = $(CROSS_COMPILER)gcc

# show the path to compiler, where to find header files and libraries
INCLUDE = -I. \
          -I$(COMMON_SOURCE_DIR)/inc \
          -I$(COMMON_SOURCE_DIR)/dwl \
          -I$(COMMON_SOURCE_DIR)/mpeg4 \
          -I$(COMMON_SOURCE_DIR)/jpeg \
          -I$(COMMON_SOURCE_DIR)/vc1 \
          -I$(COMMON_SOURCE_DIR)/mpeg2 \
          -I$(COMMON_SOURCE_DIR)/vp6 \
          -I$(COMMON_SOURCE_DIR)/vp8 \
          -I$(COMMON_SOURCE_DIR)/avs \
          -I$(COMMON_SOURCE_DIR)/pp \
          -I$(COMMON_SOURCE_DIR)/config \
          -I$(COMMON_SOURCE_DIR)/common

ifeq ($(IS_8170),1)
        INCLUDE += -I$(COMMON_SOURCE_DIR)/h264
else
        INCLUDE += -I$(COMMON_SOURCE_DIR)/h264high \
                   -I$(COMMON_SOURCE_DIR)/rv
endif

# compiler switches
CFLAGS += -Wall -Wextra -std=c99 -pedantic -fPIC
CFLAGS += $(ARCH) $(DEBFLAGS) $(INCLUDE) $(M32)

ifeq ($(ASIC_TRACE_SUPPORT), y)
    CFLAGS+=-DASIC_TRACE_SUPPORT
    INCLUDE+=-I../../test/common/swhw
endif

# set for 64bit compilation
ifeq ($(M32), -m64)
    CFLAGS+=-DUSE_64BIT_ENV
endif

# list of used sourcefiles
SRC_PP := \
    ppapi.c \
    ppinternal.c \
    regdrv_g1.c

SRC_PP_TRACE :=

ifeq ($(DEBUG),y)
	SRCS = $(SRC_PP) $(SRC_PP_TRACE)
else
	SRCS = $(SRC_PP)
endif
#source search path
vpath %.c
vpath %.c $(COMMON_SOURCE_DIR)/pp $(COMMON_SOURCE_DIR)/common

vpath %.o
vpath %.o obj

# object files will be generated from .c sourcefiles
OBJS    = $(SRCS:.c=.o)

# name of the outputfile (library)
DECLIB = libdecx170p.a

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
	@echo "    $$ make pc_plain_lib"
	@echo "    $$ make ads"
	@echo "    $$ make pclinux_eval"
	@echo

$(OBJDIR):
	mkdir $@

ads11: CC=armcc
ads11: AR=armar -rcv
ads11: CFLAGS =  --cpu ARM1136J-S -O2 -Otime $(INCLUDE)
ads11: $(DECLIB)

ads9: CC=armcc
ads9: AR=armar -rcv
ads9: CFLAGS =  --cpu ARM926EJ-S -O2 -Otime $(INCLUDE)
ads9: $(DECLIB)

coverage: CC = covc --abs --retain gcc
coverage: $(DECLIB)

pclinux: $(DECLIB)

arm_pclinux: CROSS_COMPILER = aarch64-linux-gnu-
arm_pclinux: M32=
arm_pclinux: $(DECLIB)

pcie: $(DECLIB)

pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: $(DECLIB)

integrator: CROSS_COMPILER = arm-linux-
integrator: ARCH = -march=armv4 -mtune=arm9tdmi -fpic
integrator: $(DECLIB)

#versatile: CROSS_COMPILER = arm-none-linux-gnueabi-
versatile: CROSS_COMPILER = aarch64-linux-gnu-
versatile: ARCH = -mtune=arm926ej-s -fpic
versatile: $(DECLIB)

versatile_plain_lib: CROSS_COMPILER = arm-linux-
versatile_plain_lib: ARCH = -mtune=arm926ejs -fpic
versatile_plain_lib: $(DECLIB)

linaro: CROSS_COMPILER = arm-linux-gnueabi-
linaro: ARCH = -march=armv7-a
linaro: $(DECLIB)


$(DECLIB): depend $(OBJDIR) $(OBJS)
	$(AR) $(DECLIB) $(patsubst %,$(OBJDIR)/%, $(OBJS))

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $(ENVSET) $(PIPELINE_SUPPORT) $< -o $(OBJDIR)/$@


.PHONY: clean
clean:
	$(RM) $(DECLIB)
	$(RM) .depend
	$(RM) -r $(OBJDIR)

lint: $(SRC_PP)
        # -e537 removes warnings about multiple include
	-lint-nt -w2 -e537 $(INCLUDE) $(PIPELINE_SUPPORT) -dPP_H264DEC_PIPELINE_SUPPORT -dPP_PIPELINED_DEC_TYPE_MPEG4 -dPP_PIPELINED_DEC_TYPE_JPEG -dPP_VC1DEC_PIPELINE_SUPPORT $^ > pc-lint-report.txt


depend: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

.PHONY: ads9 ads11 pcie pclinux pclinux_eval integrator versatile clean lint tar

ifeq (.depend, $(wildcard .depend))
include .depend
endif
