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

#--  Abstract : Makefile for AVS decoder control SW
#--
#-------------------------------------------------------------------------------

# Open below compiling options by default to support NXP's request
USE_OUTPUT_RELEASE = y
USE_EXTERNAL_BUFFER = y
USE_NON_BLOCKING = y
USE_ONE_THREAD_WAIT = y

# Comment/uncomment the following line to disable/enable debugging
DEBUG = y
CLEAR_HDRINFO_IN_SEEK = n
# Add your debugging flags (or not) to CFLAGS. Do not change these!
ifeq ($(DEBUG),y)
  DEBFLAGS = -g -DDEBUG -D_ASSERT_USED
else
  DEBFLAGS = -O3 -DNDEBUG
endif

# Add your debugging flag (or not) to CFLAGS. Enable, if needed
#DEBFLAGS += -D_DWL_DEBUG # for wrapper layer trace
#DEBFLAGS += -D_DEBUG_PRINT -D_ERROR_PRINT # decoder trace
#DEBFLAGS += -DMEMORY_USAGE_TRACE #trace all memory allocations
#DEBFLAGS += -D_X170_DEMO_DEVEL # Enable demo development functionality

#Set up envirinment:

#Output mode can be:
# DEC_X170_OUTPUT_FORMAT_RASTER_SCAN
# DEC_X170_OUTPUT_FORMAT_TILED

#SETTING_FLAGS = -DDEC_X170_OUTPUT_FORMAT=DEC_X170_OUTPUT_FORMAT_TILED
SETTING_FLAGS = -DDEC_X170_OUTPUT_FORMAT=DEC_X170_OUTPUT_FORMAT_RASTER_SCAN

COMMON_SOURCE_DIR = ../../source

# Architecture flags for gcc
ARCH =
CROSS_COMPILER =

# C -compiler name, can be replaced by another compiler(replace gcc)
CC = $(CROSS_COMPILER)gcc

# show the path to compiler, where to find header files and libraries
INCLUDE = -I. -I$(COMMON_SOURCE_DIR)/inc -I$(COMMON_SOURCE_DIR)/dwl \
          -I$(COMMON_SOURCE_DIR)/avs -I$(COMMON_SOURCE_DIR)/config \
          -I$(COMMON_SOURCE_DIR)/common -I$(COMMON_SOURCE_DIR)/../test/common/utils

# compiler switches
CFLAGS += -Wall -Wextra -std=c99 -pedantic -fPIC
CFLAGS += $(ARCH) $(DEBFLAGS) $(INCLUDE) $(SETTING_FLAGS)

ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS += -DASIC_TRACE_SUPPORT
endif

# set for 64bit compilation
ifeq ($(M32), -m64)
	CFLAGS += -DUSE_64BIT_ENV
endif

ifeq ($(USE_OUTPUT_RELEASE), y)
	CFLAGS += -DUSE_OUTPUT_RELEASE
	CFLAGS += -DFIFO_DATATYPE=addr_t
endif

ifeq ($(USE_EXTERNAL_BUFFER), y)
	CFLAGS += -DUSE_EXTERNAL_BUFFER
endif

ifeq ($(CLEAR_HDRINFO_IN_SEEK), y)
	CFLAGS += -DCLEAR_HDRINFO_IN_SEEK
endif

ifeq ($(USE_PICTURE_DISCARD), y)
	CFLAGS += -DUSE_PICTURE_DISCARD
endif

# If decoder can not get free buffer, return AVSDEC_NO_DECODING_BUFFER.
# Don't open it if "USE_OUTPUT_RELEASE" is not enabled.
ifeq ($(USE_NON_BLOCKING), y)
	CFLAGS += -DGET_FREE_BUFFER_NON_BLOCK
endif

ifeq ($(USE_ONE_THREAD_WAIT), y)
	CFLAGS += -DGET_OUTPUT_BUFFER_NON_BLOCK
endif

ifeq ($(USE_OMXIL_BUFFER), y)
	CFLAGS += -DUSE_OMXIL_BUFFER
endif

# Enable WaitListNotInUse so that buffer can be released safely.
#CFLAGS += -DUSE_EXT_BUF_SAFE_RELEASE

# list of used sourcefiles
SRC_AVS := \
	avsdecapi.c \
	avsdecapi_internal.c \
	avs_strm.c \
	avs_headers.c \
	avs_utils.c \
	avs_vlc.c \
	regdrv_g1.c \
	refbuffer.c \
	tiledref.c \
	bqueue.c \
	errorhandling.c \
	commonconfig_g1.c \
	input_queue.c \
	fifo.c

SRCS = $(SRC_AVS)

#source search path
vpath %.c
vpath %.c $(COMMON_SOURCE_DIR)/avs $(COMMON_SOURCE_DIR)/common

vpath %.o
vpath %.o obj

# object files will be generated from .c sourcefiles
OBJS    = $(SRCS:.c=.o)

# name of the outputfile (library)
DECLIB = libdecx170a.a

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
	@echo "    $$ make arm_pclinux"
	@echo

$(OBJDIR):
	mkdir $@

ads: CC=armcc
ads: AR=armar -rcv
ads: CFLAGS = --cpu ARM1136J-S -O2 -Otime $(INCLUDE)
ads: $(DECLIB)

ads11: CC=armcc
ads11: AR=armar -rcv
ads11: CFLAGS = -g --cpu ARM1136J-S -O2 -Otime $(INCLUDE)
ads11: $(DECLIB)

ads9: CC=armcc
ads9: AR=armar -rcv
ads9: CFLAGS = -g --cpu ARM926EJ-S -O2 -Otime $(INCLUDE)
ads9: $(DECLIB)

pcie: ARCH = -fpic
pcie: $(DECLIB)

coverage: CC = covc --abs --retain gcc
coverage: $(DECLIB)

pclinux: $(DECLIB)

arm_pclinux: CROSS_COMPILER = aarch64-linux-gnu-
arm_pclinux: M32=
arm_pclinux: $(DECLIB)

pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: $(DECLIB)
pclinux_eval: CC=gcc

integrator: CROSS_COMPILER = arm-linux-
integrator: ARCH = -march=armv4 -mtune=arm9tdmi -fpic
integrator: $(DECLIB)

#versatile: CROSS_COMPILER = arm-none-linux-gnueabi-
versatile: CROSS_COMPILER = aarch64-linux-gnu-
versatile: M32=
#versatile: ARCH = -mtune=arm926ej-s -fpic
versatile: $(DECLIB)

versatile_plain_lib: CROSS_COMPILER = arm-linux-
versatile_plain_lib: ARCH = -mtune=arm926ejs -fpic
versatile_plain_lib: $(DECLIB)


$(DECLIB): depend $(OBJDIR) $(OBJS)
	$(AR) $(DECLIB) $(patsubst %,$(OBJDIR)/%, $(OBJS))

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $(ENVSET) $< -o $(OBJDIR)/$@


.PHONY: clean
clean:
	$(RM) $(DECLIB)
	$(RM) .depend
	$(RM) -r $(OBJDIR)

lint: $(SRC_AVS)
        # -e537 removes warnings about multiple include
        # -e641 removes warnings about conevrting enum to int
	-lint-nt -w2 -e537 -e641 $(INCLUDE) $^ > pc-lint-report.txt


depend: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

.PHONY: ads9 ads11 pclinux pclinux_eval integrator versatile clean lint tar

#ifeq (.depend, $(wildcard .depend))
#include .depend
#endif

ifeq (,$(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif
