#!/bin/bash

DATE=`date --rfc-3339=date`

if [ -f test_tmp.log ]; then
    rm test_tmp.log
fi

if [ -f test_script.txt ]; then
    rm test_script.txt
fi

cat g2_customer_test_cases.txt >> test_script.txt

./video_decoder &> test_tmp.log

grep cmp test_tmp.log > test_g2_"$DATE".log
grep ^FAILED test_tmp.log >> test_g2_"$DATE".log
grep -A4 Result test_tmp.log >> test_g2_"$DATE".log
