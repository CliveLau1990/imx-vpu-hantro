#!/bin/sh
#-------------------------------------------------------------------------------
#-                                                                            --
#-       This software is confidential and proprietary and may be used        --
#-        only as expressly authorized by a licensing agreement from          --
#-                                                                            --
#-                            Hantro Products Oy.                             --
#-                                                                            --
#-                   (C) COPYRIGHT 2011 HANTRO PRODUCTS OY                    --
#-                            ALL RIGHTS RESERVED                             --
#-                                                                            --
#-                 The entire notice above must be reproduced                 --
#-                  on all copies and should not be removed.                  --
#-                                                                            --
#-------------------------------------------------------------------------------
#-
#--   Abstract     : sw/sw testing
#--                                                                           --
#-------------------------------------------------------------------------------


# This script compares SW and system model outputs.

export SYSTEM_HOME=$PWD/../../../../7190_decoder/system

sys_dir=$SYSTEM_HOME/scripts/vc1

export VC1_MODEL_HOME=$SYSTEM_HOME/models/vc1

[ ! -e system ] && mkdir system

export TESTDATA_OUT=$PWD/system
export TEST_DATA_FORMAT=TRC

#make testdecoder
make clean >/dev/null 2>&1
make libclean >/dev/null 2>&1
make pclinux >/dev/null 2>&1

#include list of testcases
. $sys_dir/../testcase_list
#for case_nr in $vc1_cases
for case_nr in $vc1_cases
#for case_nr in $vc1_main_B
do
    mkdir system/case_$case_nr 2>/dev/null
    echo -n "Running case $case_nr..."
    $sys_dir/test.sh  $case_nr 15 >/dev/null  2>&1
    ./vx170dec_pclinux -F -N15  -Osystem/out.yuv system/case_$case_nr/stream.rcv >/dev/null 2>&1
    cmp system/out.yuv system/case_$case_nr/out_w*yuv
    if [ $? != 0 ]
    then
	echo "FAILED"
    else
        echo "OK"
    fi
    rm -rf system/case_$case_nr
done
