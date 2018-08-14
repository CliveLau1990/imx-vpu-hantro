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

#--  Abstract : Makefile for VP7/VP8 decoder control SW
#--
#-------------------------------------------------------------------------------

# Open below compiling options by default to support NXP's request
USE_OUTPUT_RELEASE = y
USE_EXTERNAL_BUFFER = y
USE_NON_BLOCKING = y
USE_ONE_THREAD_WAIT = y

# set this to 'n' or 'y' in order to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -g -DDEBUG -D_ASSERT_USED -D_RANGE_CHECK -D_ERROR_PRINT
else
  DEBFLAGS = -O3 -DNDEBUG
endif

#DEBFLAGS += -D_VP8DEC_TRACE # for API tracing (VP8DecTrace has to be implemented)
#DEBFLAGS += -D_DWL_DEBUG # for wrapper layer trace
#DEBFLAGS += -DVP8_ASIC_TRACE # trace ASIC data
#DEBFLAGS += -DTRACE_START_MARKER # logic analiser start marker generation
#DEBFLAGS += -D_DEBUG_PRINT -D_ERROR_PRINT # decoder trace
#DEBFLAGS += -DMEMORY_USAGE_TRACE #trace all memory allocations

# define the decoder output format
DEBFLAGS += -DDEC_X170_OUTPUT_FORMAT=0 # raster scan output
#DEBFLAGS += -DDEC_X170_OUTPUT_FORMAT=1 # tiled output

# Define the empty picture data TESTING ONLY!!!!!
ifneq ($(SET_EMPTY_PICTURE_DATA),)
        DEBFLAGS += -DSET_EMPTY_PICTURE_DATA=$(SET_EMPTY_PICTURE_DATA)
endif

# system independent code location
COMMON_SOURCE_DIR = ../../source

# Architecture flags for gcc
ARCH =
CROSS_COMPILER =

# C -compiler name, can be replaced by another compiler(replace gcc)
CC = $(CROSS_COMPILER)gcc

# MACRO for cleaning object -files
RM  = rm -f

# MACRO for creating library that includes all the object files
AR  = $(CROSS_COMPILER)ar rcv

# show the path to compiler, where to find header files and libraries
INCLUDE = -I. -I$(COMMON_SOURCE_DIR)/inc -I$(COMMON_SOURCE_DIR)/dwl \
          -I$(COMMON_SOURCE_DIR)/vp8 -I$(COMMON_SOURCE_DIR)/config \
          -I$(COMMON_SOURCE_DIR)/common \
          -I$(COMMON_SOURCE_DIR)/../linux/dwl \
          -I$(COMMON_SOURCE_DIR)/../test/common/utils


# compiler switches
CFLAGS += -Wall -Wextra -std=c99 -pedantic -fPIC
CFLAGS += $(ARCH) -D_GNU_SOURCE $(DEBFLAGS) $(INCLUDE)
CFLAGS += -DFIFO_DATATYPE=addr_t

ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS+=-DASIC_TRACE_SUPPORT
	INCLUDE+=-I../../test/common/swhw
endif

# set for 64 bit compilation
ifeq ($(M32), -m64)
	DEBFLAGS += -DUSE_64BIT_ENV
endif

ifeq ($(USE_OUTPUT_RELEASE), y)
	DEBFLAGS += -DUSE_OUTPUT_RELEASE
endif

ifeq ($(USE_EXTERNAL_BUFFER), y)
	DEBFLAGS += -DUSE_EXTERNAL_BUFFER
endif

ifeq ($(USE_PICTURE_DISCARD), y)
	DEBFLAGS += -DUSE_PICTURE_DISCARD
endif

# If decoder can not get free buffer, return VP8DEC_NO_DECODING_BUFFER.
# Don't open it if "USE_OUTPUT_RELEASE" is not enabled.
ifeq ($(USE_NON_BLOCKING), y)
	DEBFLAGS += -DGET_FREE_BUFFER_NON_BLOCK
endif

ifeq ($(USE_ONE_THREAD_WAIT), y)
	DEBFLAGS += -DGET_OUTPUT_BUFFER_NON_BLOCK
endif

ifeq ($(USE_OMXIL_BUFFER), y)
	DEBFLAGS += -DUSE_OMXIL_BUFFER
endif

# Enable WaitListNotInUse so that buffer can be released safely.
#DEBFLAGS += -DUSE_EXT_BUF_SAFE_RELEASE

# list of used sourcefiles
SRC_VP8 :=	\
    vp8decapi.c \
    vp8decmcapi.c \
    vp8hwd_bool.c \
    vp8hwd_buffer_queue.c \
    vp8hwd_probs.c \
    vp8hwd_headers.c \
    vp8hwd_decoder.c \
    vp8hwd_pp_pipeline.c \
    vp8hwd_asic.c \
    vp8hwd_error.c \
    regdrv_g1.c \
    refbuffer.c \
    tiledref.c \
    bqueue.c \
    errorhandling.c \
    commonconfig_g1.c \
    fifo.c

#source search path
vpath %.c
vpath %.c $(COMMON_SOURCE_DIR)/vp8 $(COMMON_SOURCE_DIR)/common

vpath %.o
vpath %.o obj

SRCS = $(SRC_VP8)

# object files will be generated from .c sourcefiles
OBJS    = $(SRCS:.c=.o)

# name of the outputfile (library)
DECLIB = libdecx170vp8.a

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
ads9: CFLAGS = --cpu ARM926EJ-S -O2 -Otime $(INCLUDE)
ads9: $(DECLIB)

pclinux: $(DECLIB)

arm_pclinux: CROSS_COMPILER = aarch64-linux-gnu-
arm_pclinux: M32=
arm_pclinux: $(DECLIB)

pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: $(DECLIB)
pclinux_eval: CC=gcc

coverage: CC = covc --abs --retain gcc
coverage: $(DECLIB)

integrator: ARCH = -mtune=arm9tdmi -fpic
integrator: CROSS_COMPILER = arm-linux-
integrator: $(DECLIB)


#versatile: CROSS_COMPILER = arm-none-linux-gnueabi-
versatile: CROSS_COMPILER = aarch64-linux-gnu-
versatile: M32=
#versatile: ARCH = -mtune=arm926ej-s -fpic
versatile: $(DECLIB)

pcie: ARCH = -fpic
pcie: $(DECLIB)

versatile_plain_lib: CROSS_COMPILER = arm-linux-
versatile_plain_lib: ARCH = -mtune=arm926ejs -fpic
versatile_plain_lib: $(DECLIB)


$(DECLIB): .depend $(OBJDIR) $(OBJS)
	$(AR) $(DECLIB) $(patsubst %,$(OBJDIR)/%, $(OBJS))

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $(ENVSET) $< -o $(OBJDIR)/$@

clean:
	$(RM) $(DECLIB)
	$(RM) .depend
	$(RM) -r $(OBJDIR)

lint: $(SRC_VP8)
        # -e537 removes warnings about multiple include
        # -e641 removes warnings about conevrting enum to int
	-lint-nt -w2 -e537 -e641 $(INCLUDE) $^ > pc-lint-report.txt

depend .depend: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

tar:
	tar -cvf decx170v.tar $(DECLIB)
	tar -uvf decx170v.tar -C $(COMMON_SOURCE_DIR)/inc vp8decapi.h basetype.h


.PHONY: ads pclinux pclinux_eval integrator versatile clean lint tar

ifeq (,$(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif
