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

SRCS = async_task.c
ASYNC_UTEST_SRCS = async_task_unittest.c
OBJS = $(SRCS:.c=.o)
ASYNC_UTEST_OBJS = $(ASYNC_UTEST_SRCS:.c=.o)
LIB = libutils.a
INCLUDE = -I../../../source/inc

ifeq ($(M32), -m64)
DEFINES+=-DUSE_64BIT_ENV
endif

.PHONY: all
all: lib

utils: CFLAGS=-g -c -Wall -Wextra -pedantic $(INCLUDE) $(DEFINES) $(M32) -fPIC -std=c99
utils: LIBS=-lpthread -lrt
utils: $(OBJS)
	$(CC) $(CFLAGS) $(LIBS)

arm: CC=aarch64-linux-gnu-gcc
arm: CFLAGS=-g -c -Wall -Wextra -pedantic $(INCLUDE) $(DEFINES)  -fPIC  -std=c99
arm: LIBS=-lpthread -lrt
arm: $(OBJS)
	$(CC) $(CFLAGS) $(LIBS)

lib: utils
	$(AR) rcv $(LIB) $(OBJS)

pclinux: lib

arm_pclinux: arm
	$(AR) rcv $(LIB) $(OBJS)

versatile: arm
	$(AR) rcv $(LIB) $(OBJS)

async_task_test: EXEC=async_task
async_task_test: CFLAGS=-g -Wall $(INCLUDE) $(DEFINES) $(M32)
async_task_test: LIBS=-lpthread
async_task_test: lib utils $(ASYNC_UTEST_OBJS)
	$(CC) $(CFLAGS) $(ASYNC_UTEST_OBJS) $(LIBS) $(LIB) -o $(EXEC)

clean:
	rm -f fifo
	rm -f async_task
	rm -f $(LIB)
	rm -f *.o
