#------------------------------------------------------------------------------
#-       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
#-         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
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

#
# Abstract : Makefile containing common settings and rules used to compile
#            Hantro G2 decoder software.

# General off-the-shelf environment settings
ENV ?= x86_linux  # default
USE_64BIT_ENV ?= y
RESOLUTION_1080P ?= n
ifeq ($(strip $(ENV)),x86_linux)
  ARCH ?=
  CROSS ?=
  AR  = $(CROSS)ar rcs
  CC  = $(CROSS)gcc
  STRIP = $(CROSS)strip
  ifeq ($(strip $(USE_64BIT_ENV)),n)
    CFLAGS += -m32
    LDFLAGS += -m32
  endif
  CFLAGS += -fpic
  USE_MODEL_SIMULATION ?= y
endif
ifeq ($(strip $(ENV)),arm_linux)
  ARCH ?=
  #CROSS ?= arm-none-linux-gnueabi-
  CROSS ?= aarch64-linux-gnu-
  AR  = $(CROSS)ar rcs
  CC  = $(CROSS)gcc
  STRIP = $(CROSS)strip
  CFLAGS += -fpic
  INCLUDE += -Isoftware/linux/memalloc \
             -Isoftware/linux/ldriver/
  DEFINES += -DDEC_MODULE_PATH=\"/tmp/dev/hantrodec\" \
             -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
  USE_MODEL_SIMULATION ?= n
endif
ifeq ($(strip $(ENV)),arm_pclinux)
  ARCH ?=
  CROSS ?= aarch64-linux-gnu-
  AR  = $(CROSS)ar rcs
  CC  = $(CROSS)gcc
  STRIP = $(CROSS)strip
  ifeq ($(strip $(USE_64BIT_ENV)),n)
    CFLAGS += -m32
    LDFLAGS += -m32
  endif
  CFLAGS += -fpic
  USE_MODEL_SIMULATION ?= y
endif
ifeq ($(strip $(ENV)),x86_linux_pci)
  ARCH ?=
  CROSS ?=
  AR  = $(CROSS)ar rcs
  CC  = $(CROSS)gcc
  STRIP = $(CROSS)strip
  INCLUDE += -Isoftware/linux/memalloc \
             -Isoftware/linux/ldriver \
             -Isoftware/linux/pcidriver
  DEFINES += -DDEC_MODULE_PATH=\"/tmp/dev/hantrodec\" \
             -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
  USE_MODEL_SIMULATION ?= n
endif

ifeq ($(strip $(USE_64BIT_ENV)),y)
  DEFINES += -DUSE_64BIT_ENV
endif

ifeq ($(strip $(RESOLUTION_1080P)),y)
  DEFINES += -DRESOLUTION_1080P
endif

USE_HW_PIC_DIMENSIONS ?= n
ifeq ($(USE_HW_PIC_DIMENSIONS), y)
  DEFINES += -DHW_PIC_DIMENSIONS
endif

# Define for using prebuilt library
USE_MODEL_LIB ?= y

DEFINES += -DFIFO_DATATYPE=void*
# Common error flags for all targets
CFLAGS  += -Wall -ansi -std=c99 -pedantic
# DWL uses variadic macros for debug prints
CFLAGS += -Wno-variadic-macros

# Common libraries
LDFLAGS += -L$(OBJDIR) -pthread
LDFLAGS += -L./lib -lg2hw
# MACRO for cleaning object -files
RM  = rm -f

# Common configuration settings
RELEASE ?= n
USE_COVERAGE ?= n
USE_PROFILING ?= n
USE_SW_PERFORMANCE ?= n
# set this to 'y' for enabling IRQ mode for the decoder. You will need
# the hx170dec kernel driver loaded and a /dev/hx170 device node created
USE_DEC_IRQ ?= n
# set this 'y' for enabling sw reg tracing. NOTE! not all sw reagisters are
# traced; only hw "return" values.
USE_INTERNAL_TEST ?= n
# set this to 'y' to enable asic traces
USE_ASIC_TRACE ?= n
# set this tot 'y' to enable webm support, needs nestegg lib
WEBM_ENABLED ?= n
NESTEGG ?= $(HOME)/nestegg
# set this to 'y' to enable SDL support, needs sdl lib
USE_SDL ?= n
SDL_CFLAGS ?= $(shell sdl-config --cflags)
SDL_LDFLAGS ?= $(shell sdl-config --libs)
USE_TB_PP ?= y
USE_EXTERNAL_BUFFER ?= y
USE_FAST_EC ?= y
USE_VP9_EC ?= y
CLEAR_HDRINFO_IN_SEEK ?= n
USE_PICTURE_DISCARD ?= n
USE_RANDOM_TEST ?= n
USE_NON_BLOCKING ?= y
USE_ONE_THREAD_WAIT ?= y
USE_OMXIL_BUFFER ?= n
# Flags for >2GB file support.
DEFINES += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE

# Input buffer contains multiple frames?
# If yes, open it. If no, close it to improve sw performance.
#DEFINES += -DHEVC_INPUT_MULTI_FRM

ifeq ($(strip $(USE_SDL)),y)
  DEFINES += -DSDL_ENABLED
  CFLAGS += $(SDL_CFLAGS)
  LDFLAGS += $(SDL_LDFLAGS)
  LIBS += -lSDL -ldl -lrt -lm
endif

ifeq ($(strip $(RELEASE)),n)
  CFLAGS   += -g -O0
  DEFINES += -DDEBUG -D_ASSERT_USED -D_RANGE_CHECK -D_ERROR_PRINT
  BUILDCONFIG = debug
  USE_STRIP ?= n
else
  CFLAGS   += -O2
  DEFINES += -DNDEBUG
  BUILDCONFIG = release
  USE_STRIP ?= y
endif

# Directory where object and library files are placed.
BUILDROOT=out
OBJDIR=$(strip $(BUILDROOT))/$(strip $(ENV))/$(strip $(BUILDCONFIG))

ifeq ($(USE_ASIC_TRACE), y)
  DEFINES += -DASIC_TRACE_SUPPORT
endif

ifeq ($(USE_COVERAGE), y)
  CFLAGS += -coverage -fprofile-arcs -ftest-coverage
  LDFLAGS += -coverage
endif

ifeq ($(USE_PROFILING), y)
  CFLAGS += -pg
  LDFLAGS += -pg
endif

ifeq ($(USE_MODEL_SIMULATION), y)
  DEFINES += -DMODEL_SIMULATION
endif

ifeq ($(USE_DEC_IRQ), y)
  DEFINES +=  -DDWL_USE_DEC_IRQ
endif

ifeq ($(USE_TB_PP), y)
  DEFINES += -DTB_PP
endif

ifeq ($(CLEAR_HDRINFO_IN_SEEK), y)
  DEFINES += -DCLEAR_HDRINFO_IN_SEEK
endif

ifeq ($(USE_EXTERNAL_BUFFER), y)
  DEFINES += -DUSE_EXTERNAL_BUFFER
  DEFINES += -DUSE_FAKE_RFC_TABLE
endif

ifeq ($(USE_FAST_EC), y)
  DEFINES += -DUSE_FAST_EC
endif

ifeq ($(USE_PICTURE_DISCARD), y)
  DEFINES += -DUSE_PICTURE_DISCARD
endif

ifeq ($(USE_RANDOM_TEST), y)
  DEFINES += -DUSE_RANDOM_TEST
endif

ifeq ($(USE_VP9_EC), y)
  DEFINES += -DUSE_VP9_EC
endif

ifeq ($(USE_OMXIL_BUFFER), y)
  DEFINES += -DUSE_OMXIL_BUFFER
endif

# If decoder can not get free buffer, return DEC_NO_DECODING_BUFFER.
# Don't open it if "USE_EXTERNAL_BUFFER" is not enabled.
ifeq ($(USE_NON_BLOCKING), y)
  DEFINES += -DGET_FREE_BUFFER_NON_BLOCK
endif

ifeq ($(USE_ONE_THREAD_WAIT), y)
  DEFINES += -DGET_OUTPUT_BUFFER_NON_BLOCK
endif

# Enable WaitListNotInUse so that buffer can be released safely.
#DEFINES += -DUSE_EXT_BUF_SAFE_RELEASE

#  DEFINES += -DCLEAR_OUT_BUFFER

# Define the decoder output format.
# TODO(vmr): List the possible values.
DEFINES += -DDEC_X170_OUTPUT_FORMAT=0 # raster scan output

# Set length of SW timeout in milliseconds. default: infinite wait (-1)
# This is just a parameter passed to the wrapper layer, so the real
# waiting happens there and is based on that implementation
DEFINES += -DDEC_X170_TIMEOUT_LENGTH=-1

ifeq ($(USE_SW_PERFORMANCE), y)
  DEFINES += -DSW_PERFORMANCE
endif

ifeq ($(DISABLE_PIC_FREEZE_FLAG), y)
  DISABLE_PIC_FREEZE=-D_DISABLE_PIC_FREEZE
endif
ifeq ($(WEBM_ENABLED), y)
  DEFINES += -DWEBM_ENABLED
  INCLUDE += -I$(NESTEGG)/include
  LIBS    += $(NESTEGG)/lib/libnestegg.a
endif
