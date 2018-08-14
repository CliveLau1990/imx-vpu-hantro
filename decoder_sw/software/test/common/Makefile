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
# Makefile for G2 decoder testbench.

G2_SRCS += $(DWL_SRCS)
G2_SRCS += $(COMMON_SRCS)
G2_SRCS += $(TEST_SRCS)
ifeq ($(findstring hevcdec_tb,$(MAKECMDGOALS)),hevcdec_tb)
G2_SRCS += $(HEVC_SRCS)
DEFINES += -DENABLE_HEVC_SUPPORT
endif
ifeq ($(findstring vp9dec_tb,$(MAKECMDGOALS)),vp9dec)
G2_SRCS += $(VP9_SRCS)
DEFINES += -DENABLE_VP9_SUPPORT
endif
ifeq ($(findstring vp9dec,$(MAKECMDGOALS)),vp9dec)
G2_SRCS += $(VP9_SRCS)
DEFINES += -DENABLE_VP9_SUPPORT
endif
ifeq ($(findstring g2dec,$(MAKECMDGOALS)),g2dec)
G2_SRCS += $(VP9_SRCS)
G2_SRCS += $(HEVC_SRCS)
DEFINES += -DENABLE_HEVC_SUPPORT
DEFINES += -DENABLE_VP9_SUPPORT
endif
TB_COMMON += software/source/common/decapi.c \
           software/test/common/dectestbench.c \
           software/test/common/bytestream_parser.c \
           software/test/common/command_line_parser.c \
           software/test/common/error_simulator.c \
           software/test/common/file_sink.c \
           software/test/common/md5_sink.c \
           software/test/common/null_sink.c \
           software/test/common/vpxfilereader.c \
           software/test/common/yuvfilters.c
G2_SRCS += $(TB_COMMON)
ifeq ($(strip $(USE_SDL)),y)
  G2_SRCS += software/test/common/sdl_sink.c
endif

ifeq ($(strip $(USE_MODEL_SIMULATION)),y)
  G2_SRCS += $(SYS_SRCS)
  G2_SRCS += software/test/common/simulation_trace.c
else
  G2_SRCS += software/test/common/null_trace.c
#  ifeq ($(strip $(USE_MODEL_LIB)),n)
#    G2_SRCS += $(SYS_SRCS)
#    G2_SRCS += software/test/common/simulation_trace.c
#  else
#    G2_SRCS += software/test/common/null_trace.c
#  endif
endif

ifeq ($(findstring hevcdec_tb,$(MAKECMDGOALS)),hevcdec_tb)
  DEC_SRCS += software/test/hevc/dectestbench.c
else
  ifeq ($(findstring vp9dec_tb,$(MAKECMDGOALS)),vp9dec_tb)
    DEC_SRCS += software/test/vp9/dectestbench.c
  else
    DEC_SRCS += software/test/common/dectestbench.c
  endif
endif
DEFINES += -DFIFO_DATATYPE=void*

G2_OBJS := $(sort $(patsubst %,$(OBJDIR)/%,$(G2_SRCS:.c=.o)))
DEC_OBJS := $(sort $(patsubst %,$(OBJDIR)/%,$(DEC_SRCS:.c=.o)))

OMX_LIB_G2_DWL          = libdwlg2.a
OMX_LIB_G2_COMMON       = libg2common.a
OMX_LIB_G2_HEVC         = libg2hevc.a
OMX_LIB_G2_VP9          = libg2vp9.a
OMX_LIB_G2_HW           = lib/libg2hw.a
OMX_LIB_G2_TBCOMMON     = libg2tbcommon.a

libdwlg2.a: $(sort $(patsubst %,$(OBJDIR)/%,$(DWL_SRCS:.c=.o)))
	@echo "[AR] $(OBJDIR)/$@"
	@$(AR) $(OBJDIR)/$@ $^

libg2common.a: $(sort $(patsubst %,$(OBJDIR)/%,$(COMMON_SRCS:.c=.o)))
	@echo "[AR] $(OBJDIR)/$@"
	@$(AR) $(OBJDIR)/$@ $^

libg2hevc.a: $(sort $(patsubst %,$(OBJDIR)/%,$(HEVC_SRCS:.c=.o)))
	@echo "[AR] $(OBJDIR)/$@"
	@$(AR) $(OBJDIR)/$@ $^

libg2vp9.a: $(sort $(patsubst %,$(OBJDIR)/%,$(VP9_SRCS:.c=.o)))
	@echo "[AR] $(OBJDIR)/$@"
	@$(AR) $(OBJDIR)/$@ $^

libg2tbcommon.a: $(sort $(patsubst %,$(OBJDIR)/%,$(TBCOMMON_SRCS:.c=.o)))
	@echo "[AR] $(OBJDIR)/$@"
	@$(AR) $(OBJDIR)/$@ $^

vp9dec: env depend $(LIBS) $(G2_OBJS) $(DEC_OBJS) $(OMX_LIB_G2_DWL) $(OMX_LIB_G2_COMMON) $(OMX_LIB_G2_HEVC) $(OMX_LIB_G2_VP9) $(OMX_LIB_G2_HW) $(OMX_LIB_G2_TBCOMMON)
	@echo -e "[LINK]\t$(OBJDIR)/$@"
	@$(CC) $(LDFLAGS) $(filter %.o, $^) $(LIBS) -o $(OBJDIR)/$@
ifeq ($(strip $(USE_STRIP)),y)
	@echo -e "[STRIP]\t$(OBJDIR)/$@"
	@$(STRIP) --strip-all $(OBJDIR)/$@
endif

hevcdec_tb: env depend $(LIBS) $(G2_OBJS) $(DEC_OBJS)
	@echo -e "[LINK]\t$(OBJDIR)/$@"
	@$(CC) $(LDFLAGS) $(filter %.o, $^) $(LIBS) -o $(OBJDIR)/$@
ifeq ($(strip $(USE_STRIP)),y)
	@echo -e "[STRIP]\t$(OBJDIR)/$@"
	@$(STRIP) --strip-all $(OBJDIR)/$@
endif

vp9dec_tb: env depend $(LIBS) $(G2_OBJS) $(DEC_OBJS)
	@echo -e "[LINK]\t$(OBJDIR)/$@"
	@$(CC) $(LDFLAGS) $(filter %.o, $^) $(LIBS) -o $(OBJDIR)/$@
ifeq ($(strip $(USE_STRIP)),y)
	@echo -e "[STRIP]\t$(OBJDIR)/$@"
	@$(STRIP) --strip-all $(OBJDIR)/$@
endif

g2dec: env depend $(LIBS) $(G2_OBJS) $(DEC_OBJS) $(OMX_LIB_G2_DWL) $(OMX_LIB_G2_COMMON)  $(OMX_LIB_G2_HEVC) $(OMX_LIB_G2_VP9) $(OMX_LIB_G2_HW) $(OMX_LIB_G2_TBCOMMON)
	@echo -e "[LINK]\t$(OBJDIR)/$@"
	$(CC) $(filter %.o, $^) $(LDFLAGS) -o $(OBJDIR)/$@
ifeq ($(strip $(SYMLINKS)),y)
	@ln -fs $@ $(OBJDIR)/hevcdec_tb
	@ln -fs $@ $(OBJDIR)/vp9dec
endif
ifeq ($(strip $(USE_STRIP)),y)
	@echo -e "[STRIP]\t$(OBJDIR)/$@"
	@$(STRIP) --strip-all $(OBJDIR)/$@
endif

