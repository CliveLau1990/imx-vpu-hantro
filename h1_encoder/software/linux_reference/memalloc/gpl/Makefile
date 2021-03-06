#
# Makefile for memalloc kernel driver
#
# COPYRIGHT(C) 2014 VERISILICON.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

ifeq ($(obj),)
obj = .
endif

#################################################
# configuration

MDIR		:= hantro

# base address and end address for linear memory allocation
HLINA_START = 0x02000000
HLINA_END   = 0x08000000

# drivers objects
list-multi	:= memalloc.o

# what to build
obj-m		:= memalloc.o

tardest		:= .


KDIR_BASE := /afs/hantro.com/projects/Testing/Board_Version_Control

#KDIR := $(KDIR_BASE)/Realview_EB/SW/Linux/v0_0/linux-2.6.19-arm2/
#KDIR := $(KDIR_BASE)/Realview_EB/SW/Linux/linux-2.6.21-arm1/v0_0/linux-2.6.21-arm1
#KDIR := $(KDIR_BASE)/Realview_PB/PB926EJS/SW/Linux/linux-2.6.24-arm2-spnlck/v0_1/linux-2.6.24-arm2-spnlck
#KDIR := $(KDIR_BASE)/Realview_PB/PB926EJS/SW/Linux/linux-2.6.28-arm1/v0_1/linux-2.6.28-arm1
KDIR := $(KDIR_BASE)/SW_Common/ARM_realview_v6/2.6.28-arm1/v0_1-v6/linux-2.6.28-arm1

#KDIR	:= /usr/src/linux

#################################################
# compile modules

ifneq ($(KERNELRELEASE),)
# recursive call from kernel build system
dummy		:= $(shell echo $(KERNELRELEASE) > $(obj)/.version)

ifeq ($(VERSION).$(PATCHLEVEL),2.6)
 export-objs	:=
 list-multi	:=
else
 multi-m	:= $(filter $(list-multi), $(obj-m))
 int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))
 export-objs	:= $(filter $(int-m) $(obj-m),$(export-objs))
endif

CC += -I$(obj) -I../../source/inc
EXTRA_CFLAGS += -g -DHLINA_START_ADDRESS=$(HLINA_START) -DHLINA_END_ADDRESS=$(HLINA_END)
#EXTRA_CFLAGS += -DMEMALLOC_DEBUG

snapshot := $(wildcard $(obj)/.snapshot)
ifneq ($(snapshot),)
SNAPSHOT_CFLAGS	:= -DSNAPSHOT='$(shell cat $(snapshot))'
EXTRA_CFLAGS	+= $(SNAPSHOT_CFLAGS)
endif

-include $(TOPDIR)/Rules.make
else
# take version info from last module build if available
KERNELRELEASE	:= $(shell cat $(obj)/.version 2>/dev/null || uname -r)
endif
PWD		:= $(shell pwd)
DEST		:= /lib/modules/$(KERNELRELEASE)/$(MDIR)

# which files to install?
inst-m		:= $(wildcard *.ko)
ifeq ($(inst-m),)
  inst-m	:= $(obj-m)
endif

# locales seem to cause trouble sometimes.
LC_ALL = POSIX
export LC_ALL

default::
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

install::
	strip --strip-debug $(inst-m)
	-su -c "mkdir -p $(DEST); cp -v $(inst-m) $(DEST); depmod -a"

clean::
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
	-rm -f .version

#################################################
# build tarballs

thisdir	:= $(notdir $(PWD))
name	:= $(shell echo $(thisdir) | sed 's/-.*//')
ver	:= $(shell echo $(thisdir) | sed 's/.*-//')
date    := $(shell date +%Y%m%d)
tardest	?= .

snapdir := $(HOME)/snapshot
snap	?= $(name)

release: clean
	rm -f .snapshot
	(cd ..; tar cvzf $(tardest)/$(name)-$(ver).tar.gz $(thisdir))

snapshot snap tarball: clean
	echo $(date) > .snapshot
	(cd ..; tar czf $(snapdir)/$(snap)-$(date).tar.gz $(thisdir))
	$(MAKE) -C $(snapdir)

#################################################
# other stuff

%.asm: %.o
	objdump -S $< > $@

