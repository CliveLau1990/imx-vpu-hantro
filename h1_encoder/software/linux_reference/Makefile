#-------------------------------------------------------------------------------
#-                                                                            --
#-       This software is confidential and proprietary and may be used        --
#-        only as expressly authorized by a licensing agreement from          --
#-                                                                            --
#-                            Hantro Products Oy.                             --
#-                                                                            --
#-                   (C) COPYRIGHT 2006 HANTRO PRODUCTS OY                    --
#-                            ALL RIGHTS RESERVED                             --
#-                                                                            --
#-                 The entire notice above must be reproduced                 --
#-                  on all copies and should not be removed.                  --
#-                                                                            --
#-------------------------------------------------------------------------------
#-
#--  Abstract : Makefile for encoder library
#--
#-------------------------------------------------------------------------------

# Comment/uncomment the following lines to define which control codes to
# include in the library build
INCLUDE_H264 = y
INCLUDE_VP8 = y
INCLUDE_JPEG = y
INCLUDE_VIDSTAB = y

# Include internal testing with testId-parameter
INCLUDE_TESTING = n
ifneq (,$(findstring testdata,$(MAKECMDGOALS)))
INCLUDE_TESTING = y
endif

# Set HW SDRAM base address
ifeq (pcie,$(findstring pcie, $(MAKECMDGOALS)))
CHW_BASE_ADDRESS = 0x40000000
endif

# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

# Set this to 'y' for Electric Fence checking
USE_EFENCE = n

# Don't use IRQs
ifeq ($(PCIE_FPGA_VERIFICATION), y)
POLLING = y
else
POLLING = n
endif

ifeq ($(USE_EFENCE), y)
  EFENCE= -DUSE_EFENCE -I/afs/hantro.com/projects/adder/users/atna/efence_2_4_13
endif

# Add your debugging flags (or not)
ifeq ($(DEBUG),y)
  DEBFLAGS = -g -O0 -DDEBUG -D_DEBUG_PRINT -D_ASSERT_USED
  DEBFLAGS += -DH1_HAVE_ENCDEBUG_H
  DEBFLAGS += -DH1_HAVE_ENCTRACE_H
  DEBFLAGS += -DH1_HAVE_VIDSTABDEBUG_H
  
  #DEBFLAGS += -DASIC_WAVE_TRACE_TRIGGER  # enable logic analyzer trigger
  #DEBFLAGS += -DTRACE_EWL       # ewl.trc for wrapper layer trace
  #DEBFLAGS += -DTRACE_RC        # rc.trc Rate Control trace
  #DEBFLAGS += -DTRACE_REGS      # sw_reg.trc register dump
  #DEBFLAGS += -DTRACE_RECON     # sw_recon_internal.yuv tiled yuv dump
  #DEBFLAGS += -DTRACE_STREAM    # stream.trc
  #DEBFLAGS += -DTRACE_PROBS     # sw_prob_count.trc for VP8
  #DEBFLAGS += -DTRACE_SEGMENTS  # sw_segments.trc
  #DEBFLAGS += -DTRACE_PREPROCESS # sw_preprocess.trc
  #DEBFLAGS += -DTRACE_HWOUTPUT_PIC=0 # sw_mem_dump.trc for HW output stream of pic=0

  #DEBFLAGS += -DTRACE_VIDEOSTAB_INTERNAL # prints stabilization info
  #DEBFLAGS += -DTRACE_DENOISE_INTERNAL   # prints denoise parameters.

  #DEBFLAGS += -DTRACE_PIC_TIMING # trace SEI picture timing values

  #DEBFLAGS += -DH264ENC_TRACE   # api.trc
  #DEBFLAGS += -DVP8ENC_TRACE    # api.trc
  #DEBFLAGS += -DJPEGENC_TRACE   # api.trc
  #DEBFLAGS += -DVIDEOSTB_TRACE  # api.trc
else
  DEBFLAGS = -O2 -DNDEBUG
endif

# Architecture flags for gcc
#ARCH= 
#CROSS_COMPILE=

# C -compiler name, can be replaced by another compiler(replace gcc)
CC = $(CROSS_COMPILE)gcc

ifeq ($(shell uname -m),x86_64)
	ifneq (,$(findstring pclinux,$(MAKECMDGOALS)))
		ifeq ($(USE_64BIT_ENV), y)
		export ARCH = -m64
		else
		export ARCH = -m32
		endif
	endif
	ifneq (,$(findstring testdata,$(MAKECMDGOALS)))
		ifeq ($(USE_64BIT_ENV), y)
		export ARCH = -m64
		else
		export ARCH = -m32
		endif
	endif
	ifneq (,$(findstring eval,$(MAKECMDGOALS)))
		ifeq ($(USE_64BIT_ENV), y)
		export ARCH = -m64
		else
		export ARCH = -m32
		endif
	endif
	ifneq (,$(findstring pcie,$(MAKECMDGOALS)))
		ifeq ($(USE_64BIT_ENV), y)
		export ARCH = -m64
		else
		export ARCH = -m32
		endif
	endif
endif

# System model library
MODELLIB = ../../system/models/ench1_asic_model.a

# System model library that writes test data traces
TESTDATALIB = ../../system/models/ench1_asic_model_trace.a


# Show the path to compiler, where to find header files
INCLUDE = -I../source/h264 -I../source/vp8 -I../source/jpeg \
          -I../inc -I../source/common -Iewl -Idebug_trace \
          -I../source/camstab

ifeq ($(PCIE_FPGA_VERIFICATION),y)
INCLUDE += -Ikernel_module/pcie_fpga_veri -Imemalloc/pcie_fpga_veri
else
INCLUDE += -Ikernel_module -Imemalloc
endif

# Compiler switches
CFLAGS  = $(ARCH) -Wall -fPIC -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE \
          $(DEBFLAGS) $(INCLUDE)
CFLAGS += -Wsign-compare -Wmissing-field-initializers

ifeq ($(USE_64BIT_ENV), y)
CFLAGS += -DADDRESS_WIDTH_64
endif

# For GDR
ifeq ($(SEARCH_AREA_SMALL),1)
    CFLAGS+=-DSEARCH_AREA_SMALL
endif

GCC_GTEQ_435 := $(shell expr `gcc -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/'` \>= 40305)
ifeq "$(GCC_GTEQ_435)" "1"
    CFLAGS += -Wempty-body -Wtype-limits
endif

# Encoder compiler flags
#CFLAGS += -DH1_MEMCPY_CABAC_INIT   # Cabac init using memcpy for cache issues
#CFLAGS += -DH1_PRECALC_CABAC_INIT  # Cabac init using pre-calculated tables (142kB)
ifeq ($(PCIE_FPGA_VERI_LINEBUF),y)
CFLAGS += -DPCIE_FPGA_VERI_LINEBUF
endif
ifeq ($(PCIE_FPGA_VERIFICATION),y)
CFLAGS += -DPCIE_FPGA_VERIFICATION
endif

# list of used sourcefiles
SRC_ENC_COMMON := encasiccontroller_v2.c \
                  encasiccontroller.c \
                  encpreprocess.c \
                  encInputLineBuffer.c

SRC_H264 := H264CodeFrame.c\
            H264Init.c\
            H264NalUnit.c\
            H264Denoise.c\
            H264PictureParameterSet.c\
            H264PictureBuffer.c\
            H264PutBits.c\
            H264RateControl.c\
            H264SequenceParameterSet.c\
            H264Slice.c\
            H264EncApi.c\
            H264Cabac.c\
            H264Mad.c\
            H264Sei.c 

SRC_VP8  := vp8codeframe.c\
            vp8init.c\
            vp8putbits.c\
            vp8header.c\
            vp8picturebuffer.c\
            vp8picparameterset.c\
            vp8entropy.c\
            vp8macroblocktools.c\
            vp8ratecontrol.c\
            vp8encapi.c

SRC_JPEG := EncJpeg.c\
            EncJpegInit.c\
            EncJpegCodeFrame.c\
            EncJpegPutBits.c\
            JpegEncApi.c

SRC_VIDSTAB := vidstabcommon.c vidstabalg.c
SRC_VIDSTAB_API := vidstabapi.c vidstabinternal.c

SRC_TRACE = enctrace.c enctracestream.c

# Source files for test case specific test IDs, compiler flag INTERNAL_TEST
# If these are not included some tests will fail
SRC_TESTING = H264TestId.c h264encapi_ext.c vp8testid.c

SRC_EWL_PC := ewl_x280_file.c

SRC_EWL_ARM = ewl_x280_common.c ewl_linux_lock.c

ifeq ($(POLLING),y)
    SRC_EWL_ARM += ewl_x280_polling.c
else
    SRC_EWL_ARM += ewl_x280_irq.c
endif

SRCS = encswhwregisters.c

# Common parts only for encoder and not for video stab
INCLUDE_ENC_COMMON=n
    
# Combine the list of all the source files included in the library build
ifeq ($(INCLUDE_H264),y)
    SRCS += $(SRC_H264)
    INCLUDE_ENC_COMMON=y
endif
ifeq ($(INCLUDE_VP8),y)
    SRCS += $(SRC_VP8)
    INCLUDE_ENC_COMMON=y
endif
ifeq ($(INCLUDE_JPEG),y)
    SRCS += $(SRC_JPEG) 
    INCLUDE_ENC_COMMON=y
endif
ifeq ($(INCLUDE_VIDSTAB),y)
    SRCS += $(SRC_VIDSTAB) $(SRC_VIDSTAB_API)
    CFLAGS += -DVIDEOSTAB_ENABLED
    CONFFLAGS += -DVIDEOSTAB_ENABLED
endif
ifeq ($(INCLUDE_TESTING),y)
    SRCS += $(SRC_TESTING)
    CONFFLAGS += -DINTERNAL_TEST
endif

# Add common encoder files
ifeq ($(INCLUDE_ENC_COMMON),y)
    SRCS += $(SRC_ENC_COMMON)
endif

# If tracing flags are defined we need to compile the tracing functions
ifeq ($(DEBUG),y)
    SRCS += $(SRC_TRACE)
endif

# Choose EWL source, system model uses its own EWL
ifneq (,$(findstring pclinux, $(MAKECMDGOALS)))
    SRCS +=
else
ifneq (,$(findstring system, $(MAKECMDGOALS)))
    SRCS +=
else
ifneq (,$(findstring testdata, $(MAKECMDGOALS)))
    SRCS +=
else
ifneq (,$(findstring eval, $(MAKECMDGOALS)))
    SRCS +=
else
    SRCS += $(SRC_EWL_ARM)
endif
endif
endif
endif

# Source search path
vpath %.c
vpath %.c ../source/common ../source/h264 ../source/vp8 ../source/jpeg \
          ../source/camstab ./ewl ./debug_trace

# Name of the outputfile (library)
ENCLIB = libh1enc.a

# MACRO for cleaning object -files
RM  = rm -f

# MACRO for creating library that includes all the object files
AR  = $(CROSS_COMPILE)ar rcs

# Object files will be generated from .c sourcefiles
OBJS    = $(SRCS:.c=.o)


# Here are rules for building codes and generating object library.
all:    tags
	@echo ---------------------------------------
	@echo "Usage: make [ system | testdata | versatile | integrator ]"
	@echo "system     - PC system model (== pclinux)"
	@echo "testdata   - PC system model for test data creation"
	@echo "eval       - PC system model for evaluation with frame limit"
	@echo "pcie       - PC with FPGA HW"
	@echo "versatile  - ARM versatile with FPGA HW"
	@echo "integrator - ARM integrator with FPGA HW"
	@echo "NOTE! Make sure to do 'make clean'"
	@echo "between compiling to different targets!"
	@echo ---------------------------------------

.PHONY: pclinux system testdata integrator versatile clean tags depend pcie

evaluation: eval

eval: CFLAGS += -DEVALUATION_LIMIT=1000
eval: system

pclinux: system

system_static: system

system: .depend $(ENCLIB)

testdata: CFLAGS += -DTEST_DATA -DTRACE_STREAM
testdata: system

integrator: ENVSET  = -DSDRAM_LM_BASE=0x80000000 \
                      -DENC_MODULE_PATH=\"/dev/hx280\" \
                      -DMEMALLOC_MODULE_PATH=\"/dev/memalloc\"
integrator: ENVSET  += -DEWL_NO_HW_TIMEOUT                       
integrator: CROSS_COMPILE = arm-linux-
integrator: ARCH = -mcpu=arm9tdmi -mtune=arm9tdmi
integrator: $(ENCLIB)

versatile: ENVSET  = -DSDRAM_LM_BASE=0x00000000 \
                     -DENC_MODULE_PATH=\"/tmp/dev/hx280\" \
                     -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
versatile: ENVSET  += -DEWL_NO_HW_TIMEOUT                   
versatile: CROSS_COMPILE = arm-none-linux-gnueabi-
versatile: ARCH = -mcpu=arm926ej-s -mtune=arm926ej-s
versatile: $(ENCLIB)


versatile_tb: versatile
	$(MAKE) -w -C test/h264 versatile
	$(MAKE) -w -C test/vp8 versatile
	$(MAKE) -w -C test/camstab versatile
	$(MAKE) -w -C test/jpeg versatile


pcdemo: CFLAGS  = -O2 -g -Wall -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE \
                  -DDEBUG -D_ASSERT_USED -DH1_HAVE_ENCDEBUG_H -DH1_HAVE_VIDSTABDEBUG_H $(INCLUDE) 
pcdemo: $(SRC_H264:.c=.o) $(SRC_ENC_COMMON:.c=.o)  $(SRC_VIDSTAB:.c=.o)
	$(AR) $(ENCLIB) $+


pcie: ENVSET  = -DPC_PCI_FPGA_DEMO \
               -DSDRAM_LM_BASE=$(CHW_BASE_ADDRESS) \
               -DENC_MODULE_PATH=\"/tmp/dev/hx280\" \
               -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
pcie: ENVSET  += -DEWL_NO_HW_TIMEOUT
pcie: ENVSET  += -DENC_DEMO
pcie: $(ENCLIB)


system_cov: CC = covc --retain -t!debug_trace/enctrace.c,!debug_trace/enctracestream.c g++
system_cov: $(ENCLIB)


$(ENCLIB): $(OBJS)
	$(AR) $(ENCLIB) $(OBJS)

%.o: %.c
	$(CC) -c $(CFLAGS) $(ENVSET) $(CONFFLAGS) $(EFENCE) $< -o $@

clean:
	$(RM) $(ENCLIB)
	$(RM) .depend
	$(RM) *.o        

tags: 
	ctags ../inc/*h ../source/common/*[ch] ../source/camstab/*[ch]\
            ../source/h264/*[ch] ../source/vp8/*[ch] ../source/jpeg/*[ch]

lint: LINT_DEF=-dVIDEOSTAB_ENABLED
lint: $(SRC_H264) $(SRC_VP8) $(SRC_JPEG) $(SRC_VIDSTAB) $(SRC_VIDSTAB_API) # take out any not relevant sources
        # -e537 removes warnings about multiple include
        # -e641 removes warnings about converting enum to int
	-lint-nt -w2 -e537 -e641 $(LINT_DEF) $(INCLUDE) $^ > pc-lint-report.txt
        
depend .depend: $(SRCS)
	$(CC) $(INCLUDE) $(ENVSET) $(CONFFLAGS) $(EFENCE) -M  $^ > .depend

ifneq (clean, $(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif
