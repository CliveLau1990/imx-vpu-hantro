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
#--  Abstract : Makefile for encoder camera stabilization testbench
#--
#-------------------------------------------------------------------------------

# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

# Set this to 'y' for Electric Fence checking
USE_EFENCE = n

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O1 -g -DDEBUG
else
  DEBFLAGS = -O2 -DNDEBUG
endif

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

# The path where to find header files
INCFLAGS = -I../../../inc -I../../../source/camstab \
           -I../../../source/common -I../../debug_trace

ifeq ($(USE_EFENCE), y)
        EFENCE= -DUSE_EFENCE -I/afs/hantro.com/projects/adder/users/atna/efence_2_4_13 \
                -L/afs/hantro.com/projects/adder/users/atna/efence_2_4_13 \
                -lefence -lpthread
endif

CC = $(CROSS_COMPILE)gcc

# Compiler switches
CFLAGS  = $(ARCH) -Wall -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE \
           $(DEBFLAGS) $(INCFLAGS)

# Set this for 64bit compilation
ifeq ($(USE_64BIT_ENV), y)
	CFLAGS  += -DADDRESS_WIDTH_64
endif


GCC_GTEQ_440 := $(shell expr `gcc -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/'` \>= 40400)
ifeq "$(GCC_GTEQ_440)" "1"
    CFLAGS += -Wno-unused-result
endif

# List of used sourcefiles
SRCS = standalone.c EncGetOption.c

vpath %.c
vpath %.c .

OBJS = $(SRCS:.c=.o)

# Name of the input library
LIB = ../../libh1enc.a

# System model library
MODELLIB = ../../../../system/models/ench1_asic_model.a

# Name of the output executable
TARGET = videostabtest

# MACRO for cleaning object -files
RM  = rm -f


# Here are rules for building codes and generating executable
all:    tags
	@echo ---------------------------------------
	@echo "Usage: make [ system | eval | versatile ] <flags>"
	@echo "system     - PC system model (==pclinux)"
	@echo "eval       - PC system model for evaluation with frame limit"
	@echo "pcie       - PC with FPGA HW"
	@echo "integrator - ARM integrator with FPGA HW"
	@echo "versatile  - ARM versatile with FPGA HW"
	@echo ""
	@echo "Additional flags:"
	@echo "DEBUG=y              Enables debugging"
	@echo ""
	@echo "NOTE! Make sure to do 'make libclean'"
	@echo "between compiling to different targets!"
	@echo ---------------------------------------

# Use other makefiles to build the libraries
$(MODELLIB):
	$(MAKE) -w -C ../../../../system/models DEBUG=$(DEBUG)

$(LIB):
	$(MAKE) -w -C ../.. $(TARGETENV) INCLUDE_JPEG=n INCLUDE_VP8=n \
                INCLUDE_H264=n INCLUDE_TESTING=$(INCLUDE_TESTING) \
                USE_EFENCE=$(USE_EFENCE) DEBUG=$(DEBUG)

.PHONY: eval
evaluation: eval
eval: TARGET = vs_h1_eval
eval: TARGETENV = eval
eval: DEBUG = n
eval: INCLUDE_TESTING = n
eval: $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -o $(TARGET)

pclinux: system

.PHONY: system
system: TARGETENV = system
system: .depend $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET)

.PHONY: system_static
system_static: TARGETENV = system
system_static: .depend $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET) -static

.PHONY: testdata
testdata: TARGETENV = testdata
testdata: CFLAGS += -DTEST_DATA
testdata: .depend $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -lm -o $(TARGET)

.PHONY: versatile
versatile: TARGETENV = versatile
versatile: CROSS_COMPILE = arm-none-linux-gnueabi-
versatile: ARCH = -mcpu=arm926ej-s -mtune=arm926ej-s
versatile: CFLAGS+= -DMEMALLOC_MODULE_PATH=\"/tmp/dev/memalloc\"
versatile: LIB += -lpthread
versatile: $(OBJS)
	$(MAKE) -w -C ../.. $@ INCLUDE_JPEG=n INCLUDE_VP8=n INCLUDE_H264=n \
            INCLUDE_TESTING=n USE_EFENCE=$(USE_EFENCE) \
            CROSS_COMPILE=$(CROSS_COMPILE) ARCH="$(ARCH)"
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(EFENCE) -o $(TARGET) 

.PHONY: integrator
integrator: TARGETENV = integrator
integrator: CROSS_COMPILE= arm-linux-
integrator: ARCH = -mcpu=arm9tdmi -mtune=arm9tdmi
integrator: CFLAGS+= -DSDRAM_LM_BASE=0x80000000 
integrator: CFLAGS+= -DMEMALLOC_MODULE_PATH=\"/dev/memalloc\"
integrator: LIB += -lpthread
integrator: $(OBJS)
	$(MAKE) -w -C ../.. $@ INCLUDE_JPEG=n INCLUDE_VP8=n INCLUDE_H264=n \
            INCLUDE_TESTING=n USE_EFENCE=$(USE_EFENCE) \
            CROSS_COMPILE=$(CROSS_COMPILE) ARCH="$(ARCH)"
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(EFENCE) -o $(TARGET)

.PHONY: pcie
pcie: TARGETENV = pcie
pcie: LIB += -lpthread
pcie: $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) -o $(TARGET)

system_cov: CC = covc --retain -t!standalone.c,!EncGetOption.c g++
system_cov: TARGETENV = system_cov
system_cov: CFLAGS += -DTEST_DATA
system_cov: $(MODELLIB) $(LIB) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(MODELLIB) -o $(TARGET)

.PHONY: clean
clean:
	$(RM) *.o core* *~ $(TARGET) tags .depend

.PHONY: libclean
libclean: clean
	$(MAKE) -w -C ../.. clean

.PHONY: tags
tags:
	ctags ../../../inc/*.h *.c ../../../source/camstab/*.[ch] \
            ../../../source/common/*.[ch] ../../ewl/*c

depend .depend: $(SRCS)
	@echo -e "\n [DEP]\t $^"
	@$(CC) -M $(DEBFLAGS) $(INCFLAGS) $^ > .depend

ifeq (,$(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif
