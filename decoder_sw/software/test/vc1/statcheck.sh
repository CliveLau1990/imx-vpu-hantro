#!/bin/bash
#create tag
cvs stat .          | grep atus

cvs stat ../scripts/          | grep atus
cvs stat ../common/          | grep atus

cvs stat ../../linux/dwl/          | grep atus
cvs stat ../../linux/memalloc/          | grep atus
cvs stat ../../linux/ldriver/kernel_26x/          | grep atus

cvs stat ../../linux/vc1/          | grep atus

cvs stat ../../source/config/      | grep atus

cvs stat ../../source/vc1/          | grep atus

cvs stat ../../source/inc/vc1decapi.h ../../source/inc/basetype.h ../../source/inc/dwl.h          | grep atus


cvs stat -v Makefile

