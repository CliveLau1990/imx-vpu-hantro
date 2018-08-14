PERF_DIR=/cygdrive/e/projects/Testing/performance_data/mpeg4

PERF_DIR_ASP=/cygdrive/e/projects/Testing/performance_data/mpeg4_asp/progressive
PERF_DIR_ASP_I=/cygdrive/e/projects/Testing/performance_data/mpeg4_asp/interlaced


mkdir tmp_stream
mv *mpeg4 tmp_stream


cp $PERF_DIR/*.mpeg4 .
#cp $PERF_DIR_ASP/*.mpeg4 .
cp $PERF_DIR_ASP_I/*.mpeg4 .

for i in *.mpeg4
do
    echo $i
    ./perf8170mpeg4.sh $i
done
