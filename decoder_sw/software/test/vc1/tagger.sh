#!/bin/bash
#create tag
cvs stat Makefile          | grep atus
cvs tag $1 .

cvs tag $1 ../scripts/
cvs tag $1 ../common/

cvs tag $1 ../../linux/dwl/
cvs tag $1 ../../linux/memalloc/
cvs tag $1 ../../linux/ldriver/kernel_26x

cvs tag $1 ../../linux/vc1/

cvs tag $1 ../../source/config/

cvs tag $1 ../../source/vc1/

cvs tag $1 ../../source/common/

cvs tag $1 ../../source/inc/vc1decapi.h ../../source/inc/basetype.h ../../source/inc/dwl.h ../../source/inc/decapicommon.h

cd ../pp/
./tagger.sh $1
cd -
