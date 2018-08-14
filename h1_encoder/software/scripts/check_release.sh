#!/bin/sh

PROJECT_GIT_BASE=/afs/hantro.com/projects/gforce/git/h1_encoder
PROJECT_BASE=h1_encoder
LOGFILE=build.log

if [ $# -eq 0 ]; then
    echo "Usage: $0 <common_tag_name>"
    exit
fi

rm -f $LOGFILE

echo -e "\nGIT checkout of: $PROJECT_GIT_BASE with TAG=$1"

res=`git clone -n $PROJECT_GIT_BASE >> $LOGFILE 2>&1`

if [ $? != 0 ]
then
    echo -e "ERROR! git clone"
    exit
fi

cd $PROJECT_BASE

res=`git checkout $1 >> $LOGFILE 2>&1`

if [ $? != 0 ]
then
    echo -e "ERROR! git checkout"
    exit
fi

echo -e "\nBuilding ALL packages for Versatile platform\n"

echo -en "Building full SW library ... "
res=`make -C $PROJECT_BASE/software/linux_reference clean versatile >> $LOGFILE 2>&1`
if [ $? != 0 ]
then
    echo -e "\tERROR! Failed to build library!"
else
    echo -e "OK"
fi

echo -en "Building memalloc kernel driver ... "
res=`make -C $PROJECT_BASE/software/linux_reference/memalloc clean >> $LOGFILE 2>&1`
res=`make -C $PROJECT_BASE/software/linux_reference/memalloc >> $LOGFILE 2>&1`
if [ $? != 0 ]
then
    echo -e "\tERROR! Failed to build memalloc driver!"
else
    echo -e "OK"
fi

echo -en "Building hx280enc kernel driver ... "
res=`make -C $PROJECT_BASE/software/linux_reference/kernel_module clean >> $LOGFILE 2>&1`
res=`make -C $PROJECT_BASE/software/linux_reference/kernel_module >> $LOGFILE 2>&1`
if [ $? != 0 ]
then
    echo -e "\tERROR! Failed to build hx280 driver!"
else
    echo -e "OK"
fi

echo -en "Building VP8 testbench ... "
res=`make -C $PROJECT_BASE/software/linux_reference/test/vp8 clean libclean versatile >> $LOGFILE 2>&1`
if [ $? != 0 ]
then
    echo -e "\tERROR! Failed to build VP8 testbench!"
else
    echo -e "OK"
fi

echo -en "Building H264 testbench ... "
res=`make -C $PROJECT_BASE/software/linux_reference/test/h264 clean libclean versatile >> $LOGFILE 2>&1`
if [ $? != 0 ]
then
    echo -e "\tERROR! Failed to build H264 testbench!"
else
    echo -e "OK"
fi

echo -en "Building JPEG testbench ... "
res=`make -C $PROJECT_BASE/software/linux_reference/test/jpeg clean libclean versatile >> $LOGFILE 2>&1`
if [ $? != 0 ]
then
    echo -e "\tERROR! Failed to build JPEG testbench!"
else
    echo -e "OK"
fi

echo -en "Building VIDEOSTAB testbench ... "
res=`make -C $PROJECT_BASE/software/linux_reference/test/camstab clean libclean versatile >> $LOGFILE 2>&1`
if [ $? != 0 ]
then
    echo -e "\tERROR! Failed to build VIDEOSTAB testbench!"
else
    echo -e "OK"
fi

echo -e "\nLogs available in <$LOGFILE>\n"


