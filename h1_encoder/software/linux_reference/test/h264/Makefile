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
#--  Abstract : Makefile for encoder testbench
#--
#-------------------------------------------------------------------------------

# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y
#USE_64BIT_ENV=y

# Include API extension and test ID for Hantro internal testing
INCLUDE_TESTING = n

# Set PCIE FPGA Verification env
ifeq (pcie,$(findstring pcie, $(MAKECMDGOALS)))
# Setup PCIE FPGA Verification env
PCIE_FPGA_VERIFICATION = y

# Test hardware handshake mode of input line buffer
PCIE_FPGA_VERI_LINEBUF = n
endif

ifeq ($(PCIE_FPGA_VERIFICATION), y)
INCLUDE_TESTING = y
endif
ifneq (,$(findstring testdata,$(MAKECMDGOALS)))
INCLUDE_TESTING = y
endif

# Set this to 'y' for Electric Fence checking
USE_EFENCE = n

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O1 -g -DDEBUG
else
  DEBFLAGS = -O2 -DNDEBUG
endif

#DEBFLAGS+=-DNO_OUTPUT_WRITE

# Architecture flags for gcc
#ARCH = 
#CROSS_COMPILE =

ifeq ($(shell uname -m),x86_64)
	ifneq (,$(findstring pclinux,$(MAKECMDGOALS)))
		ifeq ($(USE_64BIT_ENV), y)
		export ARCH = -m64
		else
		export ARCH = -m32
		endif
	endif
	ifneq (,$(findstring system,$(MAKECMDGOALS)))
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
		export ARCH = -m32
	endif
	ifneq (,$(findstring pcie,$(MAKECMDGOALS)))
		ifeq ($(USE_64BIT_ENV), y)
		export ARCH = -m64
		else
		export ARCH = -m32
		endif
	endif
endif

# The path where to find header files
INCFLAGS = -I../../../inc -I../../../source/h264 \
           -I../../../source/common -I../../../source/camstab \
           -I../../debug_trace

ifeq ($(USE_EFENCE), y)
        EFENCE= -DUSE_EFENCE -I/afs/hantro.com/projects/adder/users/atna/efence_2_4_13 \
                -L/afs/hantro.com/projects/adder/users/atna/efence_2_4_13 \
                -lefence -lpthread
endif

CC = $(CROSS_COMPILE)gcc

# Compiler switches
CFLAGS  = $(ARCH) -Wall -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE \
           -D_FILE_OFFSET_BITS=64 $(DEBFLAGS) $(INCFLAGS)

ifeq ($(USE_64BIT_ENV), y)
CFLAGS += -DADDRESS_WIDTH_64
endif

GCC_GTEQ_440 := $(shell expr `gcc -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/'` \>= 40400)
ifeq "$(GCC_GTEQ_440)" "1"
    CFLAGS += -Wno-unused-result
endif

# List of used sourcefiles
SRCS = H264TestBench.c EncGetOption.c ssim.c

ifeq ($(INCLUDE_TESTING),y)
# For internal tests
SRCS += h264encapi_ext.c
CFLAGS += -DINTERNAL_TEST
endif

vpath %.c
vpath %.c ../../../source/h264 

OBJS = $(SRCS:.c=.o)

# Name of the control code library
LIB = ../../libh1enc.a

# System model library
MODELLIB = ../../../../system/models/ench1_asic_model.a

# Name of the output executable
TARGET = h264_testenc

# MACRO for cleaning object -files
RM  = rm -f


# Here are rules for building codes and generating executable
all:    tags
	@echo ---------------------------------------
	@echo "Usage: make [ system | eval | versatile ] <flags>"
	@echo "system     - PC system model (== pclinux)"
	@echo "eval       - PC system model for evaluation with frame limit"
	@echo "pcie       - PC with FPGA HW"
	@echo "integrator - ARM integrator with FPGA HW"
	@echo "versatile  - ARM versatile with FPGA HW"
	@echo ""
	@echo "Additional flags:"
	@echo "DEBUG=y              Enables debugging"
	@echo "INCLUDE_TESTING=y    API extension and test ID for Hantro internal testing"
	@echo ""
	@echo "NOTE! Make sure to do 'make libclean'"
	@echo "between compiling to different targets!"
	@echo ---------------------------------------

# Use other makefiles to build the libraries
$(MODELLIB):
	$(MAKE) -w -C ../../../../system/models DEBUG=$(DEBUG) USE_64BIT_ENV=$(USE_64BIT_ENV)

$(LIB):
	$(MAKE) -w -C ../.. $(TARGETENV) INCLUDE_JPEG=n INCLUDE_VP8=n \
                INCLUDE_VIDSTAB=y USE_EFENCE=$(USE_EFENCE) \
                DEBUG=$(DEBUG) INCLUDE_TESTING=$(INCLUDE_TESTING) USE_64BIT_ENV=$(USE_64BIT_ENV) \
                PCIE_FPGA_VERIFICATION=$(PCIE_FPGA_VERIFICATION) \
                PCIE_FPGA_VERI_LINEBUF=$(PCIE_FPGA_VERI_LINEBUF)

.PHONY: eval
evaluation: eval
eval: TARGET = h264_enc_h1_eval
eval: TARGETENV = eval
eval: DEBUG = n
eval: INCLUDE_TESTING = n
eval: CFLAGS += -DEVALUATION_LIMIT=1000 -DPSNR
eval: $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET)

pclinux: system

.PHONY: system
system: TARGETENV = system
system: CFLAGS += -DPSNR
system: .depend $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET)

.PHONY: testdata
testdata: TARGETENV = testdata
testdata: CFLAGS += -DPSNR
testdata: .depend $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET)

.PHONY: system_multifile
system_multifile: TARGET = h264_testenc_multifile
system_multifile: TARGETENV = system
system_multifile: CFLAGS += -DPSNR
system_multifile: CFLAGS += -DMULTIFILEINPUT
system_multifile: .depend $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET)

.PHONY: system_static
system_static: TARGETENV = system
system_static: CFLAGS += -DPSNR
system_static: .depend $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET) -static

.PHONY: versatile
versatile: TARGETENV = versatile
versatile: CROSS_COMPILE = arm-none-linux-gnueabi-
versatile: ARCH = -mcpu=arm926ej-s -mtune=arm926ej-s
versatile: LIB += -lpthread
versatile: $(OBJS)
	$(MAKE) -w -C ../.. $@ INCLUDE_JPEG=n INCLUDE_VIDSTAB=y \
            USE_EFENCE=$(USE_EFENCE) CROSS_COMPILE=$(CROSS_COMPILE) ARCH="$(ARCH)"
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(EFENCE) -o $(TARGET) 

.PHONY: versatile_multifile
versatile_multifile: TARGETENV = versatile
versatile_multifile: CROSS_COMPILE = arm-none-linux-gnueabi-
versatile_multifile: ARCH = -mcpu=arm926ej-s -mtune=arm926ej-s
versatile_multifile: LIB += -lpthread
versatile_multifile: CFLAGS += -DMULTIFILEINPUT
versatile_multifile: TARGET = h264_testenc_multifile
versatile_multifile: $(OBJS)
	$(MAKE) -w -C ../.. versatile INCLUDE_JPEG=n INCLUDE_VIDSTAB=y \
            USE_EFENCE=$(USE_EFENCE) CROSS_COMPILE=$(CROSS_COMPILE) ARCH="$(ARCH)"
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(EFENCE) -o $(TARGET)

.PHONY: integrator
integrator: TARGETENV = integrator
integrator: CROSS_COMPILE= arm-linux-
integrator: ARCH = -mcpu=arm9tdmi -mtune=arm9tdmi
integrator: LIB += -lpthread
integrator: $(OBJS)
	$(MAKE) -w -C ../.. $@ INCLUDE_JPEG=n INCLUDE_VIDSTAB=y \
            USE_EFENCE=$(USE_EFENCE) CROSS_COMPILE=$(CROSS_COMPILE) ARCH="$(ARCH)"
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(EFENCE) -o $(TARGET)

.PHONY: pcie
pcie: TARGETENV = pcie
pcie: LIB += -lpthread
pcie: $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) -o $(TARGET)


system_cov: CC = covc --retain -t!H264TestBench.c,!EncGetOption.c g++
system_cov: TARGETENV = system_cov
system_cov: $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -o $(TARGET)

        
.PHONY: clean
clean:
	$(RM) *.o core* *~ $(TARGET) $(TARGET).* .depend

.PHONY: libclean
libclean: clean
	$(MAKE) -w -C ../.. clean

.PHONY: tags
tags:
	ctags ../../../inc/*.h *.c ../../../source/h264/*.[ch] \
            ../../../source/common/*.[ch] ../../ewl/*c

depend .depend: $(SRCS)
	@echo -e "\n [DEP]\t $^"
	@$(CC) -M $(DEBFLAGS) $(INCFLAGS) $^ > .depend

ifeq (,$(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif

