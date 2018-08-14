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

#--  Abstract : Makefile for 6170 H.264 decoder control SW
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
  CFLAGS   = -g -O0
  DEBFLAGS = -DDEBUG -D_ASSERT_USED -D_ERROR_PRINT
else
  CFLAGS   = -O3
  DEBFLAGS = -DNDEBUG
endif

#DEBFLAGS += -DVP6DEC_TRACE # for API tracing (VP6DecTrace has to be implemented)
#DEBFLAGS += -D_DWL_DEBUG # for wrapper layer trace
#DEBFLAGS += -D_DEBUG_PRINT # decoder trace
#DEBFLAGS += -D_ERROR_PRINT # decoder error prints
#DEBFLAGS += -DMEMORY_USAGE_TRACE #trace all memory allocations
#DEBFLAGS += -DTRACE_START_MARKER # logic analiser start marker generation
#DEBFLAGS += -DTRACE_PP_CTRL # trace PP control in DEC software

# define the decoder output format
DEBFLAGS += -DDEC_X170_OUTPUT_FORMAT=0 # raster scan output
#DEBFLAGS += -DDEC_X170_OUTPUT_FORMAT=1 # tiled output

# Set length of SW timeout in milliseconds. default: infinite wait (-1)
# This is just a parameter passed to the wrapper layer, so the real
# waiting happens there and is based on that impelmentation
DEBFLAGS+= -DDEC_X170_TIMEOUT_LENGTH=-1

# Set evaluation limit in number of decoded frames
#DEBFLAGS+= -DVP6DEC_EVALUATION=1000

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
AR  = $(CROSS_COMPILER)ar rcs

# show the path to compiler, where to find header files
INCLUDE = -I. -I$(COMMON_SOURCE_DIR)/inc \
          -I$(COMMON_SOURCE_DIR)/vp6 \
          -I$(COMMON_SOURCE_DIR)/config \
          -I$(COMMON_SOURCE_DIR)/common \
          -I$(COMMON_SOURCE_DIR)/../test/common/utils


# extra compiler switches
CFLAGS += -Wall -std=c99 -pedantic -fPIC

ifeq ($(ASIC_TRACE_SUPPORT), y)
	CFLAGS += -DASIC_TRACE_SUPPORT
	INCLUDE += -I../../test/common/swhw
endif

# set for 64bit compilation
ifeq ($(M32), -m64)
	DEBFLAGS += -DUSE_64BIT_ENV
endif

ifeq ($(USE_OUTPUT_RELEASE), y)
	CFLAGS += -DUSE_OUTPUT_RELEASE
	CFLAGS += -DFIFO_DATATYPE=addr_t
endif

ifeq ($(USE_EXTERNAL_BUFFER), y)
	CFLAGS += -DUSE_EXTERNAL_BUFFER
endif

ifeq ($(USE_PICTURE_DISCARD), y)
	CFLAGS += -DUSE_PICTURE_DISCARD
endif

# If decoder can not get free buffer, return VC1DEC_NO_DECODING_BUFFER.
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
SRC_VP6 :=	\
    vp6booldec.c \
    vp6dec.c \
    vp6huffdec.c \
    vp6hwd_api.c \
    vp6hwd_asic.c \
    vp6strmbuffer.c \
    vp6decodemode.c \
    vp6decodemv.c \
    vp6scanorder.c \
    vp6gconst.c \
    vp6_pp_pipeline.c \
    tiledref.c \
    regdrv_g1.c \
    refbuffer.c \
    bqueue.c \
    errorhandling.c \
    commonconfig_g1.c \
    fifo.c

#source search path
vpath %.c
vpath %.c $(COMMON_SOURCE_DIR)/vp6 $(COMMON_SOURCE_DIR)/common

#SRCS = $(SRC_VP6) dwl_x170_vp6_pc.c

SRCS = $(SRC_VP6)

# object files will be generated from .c sourcefiles
OBJS    := $(SRCS:.c=.o)
OBJDIR  := obj

vpath %.o
vpath %.o $(OBJDIR)

# name of the outputfile (library)
DECLIB = libdec8190vp6.a


.PHONY: ads pclinux pclinux_eval integrator versatile clean lint tar

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
	@mkdir $@

ARMCPU=ARM1136J-S
ads: CC=armcc
ads: AR=armar -rcs
ads: ARCH= --cpu $(ARMCPU)
ads: CFLAGS = --asm -g -O3 -Otime
ads: DEBFLAGS=
ads: $(DECLIB)

ads11: CC=armcc
ads11: AR=armar -rcs
ads11: ARCH=--cpu ARM1136J-S
ads11: CFLAGS = -g -O3 -Otime
ads11: DEBFLAGS=
ads11: $(DECLIB)

ads9: CC=armcc
ads9: AR=armar -rcs
ads9: ARCH= --cpu ARM926EJ-S
ads9: CFLAGS = -g -O3 -Otime
ads9: DEBFLAGS=
ads9: $(DECLIB)

coverage: CC = covc --abs --retain gcc
coverage: $(DECLIB)

pcie: CC=gcc
pcie: $(DECLIB)

pclinux: $(DECLIB)

arm_pclinux: CROSS_COMPILER = aarch64-linux-gnu-
arm_pclinux: M32=
arm_pclinux: $(DECLIB)

pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: $(DECLIB)

integrator: ARCH = -mtune=arm9tdmi
integrator: CROSS_COMPILER = arm-linux-
integrator: $(DECLIB)

#versatile: CROSS_COMPILER = arm-none-linux-gnueabi-
versatile: CROSS_COMPILER = aarch64-linux-gnu-
versatile: M32=
#versatile: ARCH = -mtune=arm926ej-s
versatile: $(DECLIB)

info:
	@echo ""
	@echo -e " ARCH     = $(strip $(ARCH))"
	@echo -e " CFLAGS   = $(strip $(CFLAGS))"
	@echo -e " DEFINE   = "
	@for D in $(strip $(DEBFLAGS:-D%=%)) ; do echo -e   "            $$D" ; done
	@echo -e " INCLUDE  = "
	@for I in $(strip $(INCLUDE)) ; do echo -e   "            $$I" ; done


$(DECLIB): info .depend $(OBJDIR) $(OBJS)
	$(AR) $(DECLIB) $(patsubst %,$(OBJDIR)/%, $(OBJS))
	@echo -e "\n $(DECLIB) OK\n"


$(OBJS): %.o: %.c
	@echo -e "\n [CC]\t $<"
	@$(CC) -c $(CFLAGS) $(ARCH) $(DEBFLAGS) $(INCLUDE) $< -o $(OBJDIR)/$@

clean:
	$(RM) $(DECLIB)
	$(RM) .depend
	$(RM) -r $(OBJDIR)

lint: $(SRC_VP6)
        # -e537 removes warnings about multiple include
        # -e641 removes warnings about converting enum to int
	-lint-nt -w2 -e537 -e641 $(INCLUDE) $^ > pc-lint-report.txt

depend .depend: $(SRCS)
	@echo -e "\n [DEP]\t $^"
	@$(CC) -M $(CFLAGS) $(DEBFLAGS) $(INCLUDE) $^ > .depend

tar:
	tar -cvf dec8190vp6.tar $(DECLIB)
	tar -uvf dec8190vp6.tar -C $(COMMON_SOURCE_DIR)/inc \
                        vp6decapi.h decapicommon.h basetype.h dwl.h

ifeq (,$(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif
