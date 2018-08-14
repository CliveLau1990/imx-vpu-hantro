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

# set this to 'n' or 'y' in order to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DDEBUG -D_ASSERT_USED
else
  DEBFLAGS = -O3 -DNDEBUG
endif

# Architecture flags for gcc
ARCH=
CROSS=

# C -compiler name, can be replaced by another compiler(replace gcc)
CC=$(CROSS)gcc

# MACRO for cleaning object -files
RM  = rm -f

# MACRO for creating library that includes all the object files
AR=$(CROSS)ar rcv

# show the path to compiler, where to find header files and libraries
INCLUDE = -I.\
                 -I../../../source/inc/\
		 -I../../../source/config\
		 -I../../../source/common

# compiler switches
CFLAGS  = -Wall -ansi $(ARCH) -D_GNU_SOURCE $(DEBFLAGS) $(INCLUDE) $(M32) -D_FILE_OFFSET_BITS=64 -fPIC

# for 64bit compilation
ifeq ($(M32), -m64)
	CFLAGS+=-DUSE_64BIT_ENV
endif

# list of used sourcefiles
SRCS =  tb_stream_corrupt.c \
        tb_params_g1.c \
        tb_cfg.c \
        tb_md5.c \
        md5.c \
        tb_tiled.c

# HW internal tracing not possible on Versatile
ifeq (,$(findstring versatile,$(MAKECMDGOALS)))
	ifneq ($(VP6),1)
        SRCS += trace_g1.c
	endif
endif

vpath %.c

vpath %.o
vpath %.o obj

# object files will be generated from .c sourcefiles
OBJS = $(SRCS:.c=.o)

# name of the outputfile (library)
LIB = libtbcommon.a

OBJDIR = obj

#Here are rules for building codes and generating object library.
all:
	@echo
	@echo ERROR! run one of:
	@echo "    $$ make versatile"
	@echo "    $$ make pclinux"
	@echo "    $$ make pclinux_eval"	
	@echo "    $$ make coverage"
	@echo "    $$ make arm_pclinux"	
	@echo

$(OBJDIR):
	mkdir $@

pclinux: $(LIB)
coverage: $(LIB)

ARMCPU=ARM1136J-S
.PHONY: ads
ads: CC=armcc
ads: AR=armar -rcs
ads: CFLAGS = --cpu $(ARMCPU) -g --gnu -O2 -Otime $(INCLUDE)
ads: $(LIB)

ads9: CC=armcc
ads9: AR=armar -rcs
ads9: CFLAGS = --cpu ARM926EJ-S -g --gnu -O2 -Otime $(INCLUDE)
ads9: $(LIB)

ads11: CC=armcc
ads11: AR=armar -rcs
ads11: CFLAGS = --cpu ARM1136J-S -g --gnu -O2 -Otime $(INCLUDE)
ads11: $(LIB)

.PHONY: pclinux_eval
pclinux_eval: DEBFLAGS = -O3 -DNDEBUG
pclinux_eval: $(LIB)

arm_pclinux: CROSS = aarch64-linux-gnu-
arm_pclinux: M32 =
arm_pclinux: $(LIB)

#coverage: CC=covc --abs --retain gcc
coverage: pclinux

pcie: pclinux

#versatile: CROSS = arm-none-linux-gnueabi-
versatile: CROSS = aarch64-linux-gnu-
#versatile: ARCH = -mtune=arm926ej-s
versatile: M32=
versatile: $(LIB)

linaro: CROSS = arm-linux-gnueabi-
linaro: ARCH = -march=armv7-a
linaro: $(LIB)

$(LIB): .depend $(OBJDIR) $(OBJS)
	$(AR) $(LIB) $(patsubst %,$(OBJDIR)/%, $(OBJS))

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $(ENVSET) $< -o $(OBJDIR)/$@

clean:
	$(RM) $(LIB)
	$(RM) .depend
	$(RM) -r $(OBJDIR)

depend .depend: $(SRCS)
	$(CC) $(CFLAGS) -M  $^ > .depend

.PHONY: pclinux pclinux_eval versatile clean coverage

ifeq (,$(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif
