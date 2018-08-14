#!/bin/bash

DATE=`date --rfc-3339=date`

if [ $# -eq 0 ]
  then
    echo ""
    echo " Usage: customer_test_g1.sh <option>"
    echo ""
    echo " Available options:"
    echo "  0     G1 Post processor is disabled"
    echo "  1     G1 Post Processor is enabled"
    echo ""
    exit 1
fi

if [ -f test_tmp.log ]; then
    rm test_tmp.log
fi

if [ -f test_script.txt ]; then
    rm test_script.txt
fi

if [ $1 -eq 0 ]; then
    cat g1_customer_test_cases_no_pp.txt >> test_script.txt
elif [ $1 -eq 1 ]; then
    cat g1_customer_test_cases.txt >> test_script.txt
else
    echo "Invalid parameter"
    exit 1
fi

./video_decoder &> test_tmp.log

grep cmp test_tmp.log > test_g1_"$DATE".log
grep ^FAILED test_tmp.log >> test_g1_"$DATE".log
grep -A4 Result test_tmp.log >> test_g1_"$DATE".log

