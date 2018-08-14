#!/bin/bash

export TEST_DATA_HOME=/tmp/h1_sig

(
    cd ../../system
    make libclean
    rm -f sig_memory

    for testcase in 1900 1902 1904 1906 1909 3900 3902 3904 3906 3909;
    do
        test_data/test_data.sh $testcase
        grep Memory $TEST_DATA_HOME/case_$testcase/encoder.log >> sig_memory
    done
)

# Create csv with all memory chunks
sed "s/Memory:/Memory,/" ../../system/sig_memory > sig_memory_all.csv

# Order
grep "Total HW Memory" sig_memory_all.csv > sig_memory.csv
grep "SWHW Memory" sig_memory_all.csv >> sig_memory.csv
grep "Total SW Memory" sig_memory_all.csv >> sig_memory.csv
grep "Total Memory" sig_memory_all.csv >> sig_memory.csv

echo "Created sig_memory.csv"

