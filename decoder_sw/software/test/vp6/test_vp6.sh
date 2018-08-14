#! /bin/bash

testcase_list_file=/afs/hantro.com/projects/golden8170/users/atna/from_cvs/8170_decoder/system/scripts/testcase_list_8190

. $testcase_list_file

test_case_dir=/home/atna/decoder_test_data/testdata

vp6dec=./vp6dec_pclinux

ref_vp6dec=/afs/hantro.com/projects/codec7/users/atna/from_cvs/8190_decoder/system/models/vp6_ref_enc/vp6dec

all_tests="$vp6_intra $vp6_inter $vp6_huffman"

# Check parameter
if [ $# -eq 0 ]; then
    echo -e "\nUsage:"
    echo -e "\t$0 <test_case_number>"
    echo -e "\t$0 <case1> <case2> ..."
    echo -e "\t$0 <all>"
    echo -e ""    
    exit
fi

case "$1" in
    all)
    tests=$all_tests;
    ;;
    *)
    tests=$@
    ;;
esac

rm -f dec.log ref.log

dt=$(date +%F-%T)

passed="0"
failed="0"

for n in $tests
do
    echo -en "\ncase_$n "

    if [ ! -e $test_case_dir/case_$n ]; then
        echo -e "\tFAILED Directory $test_case_dir/case_$n doesn't exist!"
        failed=$(($failed + 1))
        continue
    fi
        
    echo -e "\ncase_$n" >> dec.log
    $vp6dec -O case_$n.yuv $test_case_dir/case_$n/stream.vp6 >> dec-$dt.log
    
    echo -e "\ncase_$n" >> ref_dec.log
    $ref_vp6dec $test_case_dir/case_$n/stream.vp6 ref_case_$n.yuv >> ref-dec-$dt.log
    
    cmp case_$n.yuv ref_case_$n.yuv > /dev/null 2>&1
    
    if [ $? != 0 ]
    then
        echo -e "\t\033[31mFAILED\033[0m"
        failed=$(($failed + 1)) 
    else
        echo -e "\t\033[32mOK\033[0m"
        passed=$(($passed + 1))
    fi
    
    rm -f case_$n.yuv ref_case_$n.yuv
    
done


echo -e "\nPassed \033[32m$passed\033[0m"
echo -e   "Failed \033[31m$failed\033[0m"
echo -e   "Total  $(($passed + $failed))"
echo -e   "Finished!\n"
  
