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
# Top-level makefile to build Hantro G2 decoder software.

# Include global common settings
include software/common/common.mk

INCLUDE += -I. \
           -Isoftware/source/inc \
           -Isoftware/source/hevc \
           -Isoftware/source/config \
           -Isoftware/source/dwl \
           -Isoftware/source/common \
           -Isoftware/source/vp9 \
           -Isoftware/test/common/swhw \
           -Isystem/models/g2hw \
           -Isystem/models/g2hw/streamd_internal \
           -Isystem/models/g2hw/intrap_internal

# source and object file search paths
vpath %.o $(OBJDIR)

# Common targets used across components.
.PHONY: help clean env coverage
all: help

# Include targets defined in subdirectories.
-include system/models/g2hw/Makefile
include software/linux/dwl/G2Makefile
include software/source/common/Makefile
-include software/source/hevc/Makefile
-include software/source/vp9/Makefile
include software/test/common/swhw/G2Makefile
include software/test/common/Makefile
-include software/test/hevc/Makefile

help:
	@echo ""
	@echo "Hantro G2 Decoder Sofware Makefile"
	@echo ""
	@echo " Usage:"
	@echo "  make [target] ENV=[environment] [variables]"
	@echo ""
	@echo " Available targets:"
	@echo "  vp9dec       - VP9 decoder command line binary"
	@echo "  hevcdec      - HEVC decoder command line binary"
	@echo "  libg2hw.a    - G2 decoder system model library"
	@echo "  g2dec        - G2 decoder command line binary"
	@echo "  coverage     - Coverage report (after coverage-enabled build & run)"
	@echo "  env          - Print environment variables"
	@echo ""
	@echo " Available environments:"
	@echo "  x86_linux (default)"
	@echo "  arm_linux"
	@echo "  arm_pclinux"
	@echo "  x86_linux_pci"
	@echo ""
	@echo -e " Configuration variables"
	@echo -e "  RELEASE       = $(strip $(RELEASE)) -- chooses debug or release build"
	@echo -e "  STRIP         = $(strip $(USE_STRIP)) -- strips the symbols from the resulting binary"
	@echo -e "  USE_ASIC_TRACE= $(strip $(USE_ASIC_TRACE)) -- enables asic traces in compilation"
	@echo -e "  USE_COVERAGE  = $(strip $(USE_COVERAGE)) -- enables coverage information in compilation"
	@echo -e "  USE_DEC_IRQ   = $(strip $(USE_DEC_IRQ)) -- chooses IRQ or polling mode"
	@echo -e "  USE_INTERNAL_TEST = $(strip $(USE_INTERNAL_TEST)) -- enables internal test traces"
	@echo -e "  USE_MODEL_SIMULATION = $(strip $(USE_MODEL_SIMULATION)) -- chooses sysmodel simulation or real hw"
	@echo -e "  USE_HW_PIC_DIMENSIONS = $(strip $(USE_HW_PIC_DIMENSIONS)) -- chooses HW Min resolution(180x180) as decoding picture size limitation"
	@echo -e "  USE_EXTERNAL_BUFFER  = $(strip $(USE_EXTERNAL_BUFFER)) -- enables or disables allocating buffers externally"
	@echo -e "  CLEAR_HDRINFO_IN_SEEK = $(strip $(CLEAR_HDRINFO_IN_SEEK)) -- enables or disables clearing SPS/PPS parameters after SEEK operation"
	@echo -e "  USE_PICTURE_DISCARD = $(strip $(USE_PICTURE_DISCARD)) -- enables or disables discarding the repeat displayed frame"
	@echo -e "  USE_RANDOM_TEST = $(strip $(USE_RANDOM_TEST)) -- enables or disables random error generation in control SW(Only used in internal test)"
	@echo -e "  USE_NON_BLOCKING = $(strip $(USE_NON_BLOCKING)) -- enables or disables non-blocking of decoder when get free output buffer"
	@echo -e "  USE_ONE_THREAD_WAIT = $(strip $(USE_ONE_THREAD_WAIT)) -- enables or disables decoding picture and outputing picture in one thread"
	@echo -e "  USE_OMXIL_BUFFER = $(strip $(USE_OMXIL_BUFFER)) -- enables or disables supporting OpenMAX"
	@echo -e "  USE_PROFILING = $(strip $(USE_PROFILING)) -- enables gprof profiling information in compilation"
	@echo -e "  USE_SW_PERFORMANCE = $(strip $(USE_SW_PERFORMANCE)) -- enables or disables sw performance traces"
	@echo -e "  USE_TB_PP     = $(strip $(USE_TB_PP)) -- enables or disables 10->16 bits conversion in test bench"
	@echo -e "  USE_64BIT_ENV = $(strip $(USE_64BIT_ENV)) -- enables or disables 64-bit bus support"
	@echo -e "  RESOLUTION_1080P = $(strip $(RESOLUTION_1080P)) -- cache ram size is decreased when it's defined"
	@echo -e "  WEBM_ENABLED = $(strip $(WEBM_ENABLED)) -- enables or disables WEBM support"
	@echo -e "  NESTEGG = $(strip $(NESTEGG)) -- nestegg base path"
	@echo -e "  USE_SDL = $(strip $(USE_SDL)) -- enables or disables SDL support"
	@echo -e "  SDL_CFLAGS = $(strip $(SDL_LIB_PATH)) -- SDL C flags"
	@echo -e "  SDL_LDFLAGS = $(strip $(SDL_LIB_PATH)) -- SDL LD flags"
	@echo ""
	@echo " To override environment variables give them on command line, e.g."
	@echo ""
	@echo "  make hevcdec ENV=x86_linux USE_COVERAGE=y"
	@echo ""
	@echo -e " Get Nestegg sources from: https://github.com/kinetiknz/nestegg"
	@echo -e " You can clone from https://github.com/kinetiknz/nestegg.git"
	@echo ""

env:
	@echo ""
	@echo -e " Build variables"
	@echo -e "  ENV      = $(strip $(ENV))"
	@echo -e "  ARCH     = $(strip $(ARCH))"
	@echo -e "  CC       = $(strip $(CC))"
	@echo -e "  CFLAGS   = $(strip $(CFLAGS))"
	@echo -e "  LDFLAGS  = $(strip $(LDFLAGS))"
	@echo -e "  LIBS     = $(strip $(LIBS))"
	@echo -e "  AR       = $(strip $(AR))"
	@echo -e "  DEFINES  = "        
	@for D in $(strip $(DEFINES:-D%=%)) ; do echo -e   "            $$D" ; done
	@echo -e "  INCLUDE  = "        
	@for I in $(strip $(INCLUDE)) ; do echo -e   "            $$I" ; done

# Generic file conversion rules.
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	@echo -e "[CC]\t $<"
	@test -d $(@D) || mkdir -p $(@D)
	@$(CC) -c $(CFLAGS) $(ARCH) $(DEFINES) $(INCLUDE) $< -o $@

# Dependency for coverage information.
%.gcda: %.o
	$(error To get coverage report you need to: \
1. do a clean build with USE_COVERAGE=y to build binary capturing coverage, and\
2. run some tests with the binaries to produce the coverage data.)

coverage: $(patsubst %, $(OBJDIR)/%, $(SRCS:.c=.gcda))
	@echo -e "[COVERAGE] $(BUILDROOT)/$@/index.html"
	@lcov --directory $(BUILDROOT) --base-directory . -c -q -o $(BUILDROOT)/g2cov.info
	@genhtml -q -o $(BUILDROOT)/$@ $(BUILDROOT)/g2cov.info || rm $(BUILDROOT)/g2cov.info

# Dependency generation rules.
depend .depend: $(SRCS)
	@echo -e "[DEP]\t $^"
	@$(CC) -M $(DEFINES) $(INCLUDE) $^ > .depend

# Include dependency information in all non-clean builds
ifeq (,$(findstring clean, $(MAKECMDGOALS)))
ifeq (.depend, $(wildcard .depend))
include .depend
endif
endif

$(OBJDIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILDROOT)
	$(RM) .depend
