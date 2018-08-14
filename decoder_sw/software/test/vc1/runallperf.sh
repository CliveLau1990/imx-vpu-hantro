#!/bin/bash

# Print usage if no parameters are given for script
if [ $# = 0 ]
then
    echo "----------------------------------------------------------------------"
    echo -e "\nUsage: ./runallperf.sh <StreamListFile> <MCCFG> <CPU> <CLK> <EXE>\n"
    echo -e "<StreamListFile>   contains absolute path of each test stream"
    echo -e "<MCCFG>            is CPU/BUS clock ratio"
    echo -e "<CPU>              is arm processor model (ARM926EJ-S)"
    echo -e "<CLK>              is arm processor core clock"
    echo -e "<EXE>              is name of executable (decoder.axf)\n"
    echo "----------------------------------------------------------------------"
    exit
fi

# Assign parameters to local variables
STREAM_LIST_FILE=$1
MCCFG=$2
CPU=$3
CLK=$4
EXE=$5

# Check that stream list file exists
if [ ! -e $STREAM_LIST_FILE ]
then
    echo "Stream list file: \""$STREAM_LIST_FILE"\" doesn't exist"
    exit
fi
DEC_TEST_STREAMS=`cat $STREAM_LIST_FILE`

# Check that other parameters are given
if [[ -z $MCCFG || -z $CPU || -z $CLK || -z $EXE ]]
then
    echo "Parameter missing"
    exit
fi

# Get ARMCONF environmental variable 
TEST_DIR=`pwd`
if [ "$OSTYPE" = "cygwin" ] 
then
    TEST_DIR=`echo $TEST_DIR|sed -e's/\/cygdrive\/\([a-z]\)/\1:/'`
    MY_ARMCONF=`env |grep ARMCONF| awk 'BEGIN{FS="[;]"} /ARMCONF/{print $1}'|sed -e 's/ARMCONF=//'`
else
    MY_ARMCONF=`env |grep ARMCONF| awk 'BEGIN{FS="[:]"} /ARMCONF/{print $1}'|sed -e 's/ARMCONF=//'`
fi


# Write script file for armulator
#echo "reload /callgraph" > armStarter.txt
echo "profon" >> armStarter.txt
echo "break @decsw_performance" >> armStarter.txt
echo "log tmp.log" >> armStarter.txt
echo "go; print/vc1cycles_%d \$statistics.Total; while 1" >> armStarter.txt
echo "profwrite tmp.prf" >> armStarter.txt
echo "print \$statistics" >> armStarter.txt
echo "print/%d \$cputime" >> armStarter.txt
echo "print \$memstats" >> armStarter.txt
echo "quit" >> armStarter.txt

# Write tb.cfg
echo "TbParams {" > tb.cfg
echo "    PacketByPacket = ENABLED;" >> tb.cfg
echo "}" >> tb.cfg

# Check and update bus clock divider in default.ami
if [ -z "$MY_ARMCONF" ]
then
    echo "copy arm configuration settings to your home directory "
    echo "and set ARMCONF environment variable properly"
    exit
fi
cat $MY_ARMCONF/default.ami |sed -e 's/^MCCFG=*.$/MCCFG='${MCCFG[$i]}'/' >$MY_ARMCONF/tmp.ami
mv $MY_ARMCONF/tmp.ami $MY_ARMCONF/default.ami


TF=tempf
TRACE=trace.log

# Run each stream in given stream list
for stream in $DEC_TEST_STREAMS
do
    echo $stream

    name=$(basename $stream)
    name=${name%.*}

    tot_cycles=0

    # run armulator
    armsd -cpu $CPU -clock $CLK -script armStarter.txt -O tmp.out $EXE -X $stream > /dev/null 
    cat tmp.out > $name.log
    cat tmp.log >> $name.log
    rm tmp.out tmp.log
    armprof tmp.prf > $name.prf
    rm tmp.prf

    # armsd script tags cycle count numbers with "cycles_", grep with that
    grep "vc1cycles_[1234567890]" $name.log > $TF
    sed  's/| vc1cycles_//g' $TF > cpu_$TRACE
   
    # get number of decoded frames
    numFrames=$(awk '/WRITTEN/ {frames = $NF; print frames}' "$name.log")
    
    # obtain framerate from filename, format xxx_yyy_zzfps
    frameRate=`echo $stream | cut -d"_" -f3`
    frameRate=${frameRate%fps}
    
    echo "Info frames $numFrames framerate $frameRate" 

    #first line of trace counts everything before first HW run

    LINE_COUNT=$(cat cpu_$TRACE | wc -l)
    LINE_COUNT=$[$LINE_COUNT/2]

    #there are breaks at the start and end of
    #decoding
    for i in $(seq 1 $LINE_COUNT);
    do
        # add even lines
        tot_cycles=$[$tot_cycles + $(sed -n "$[ $i*2 ] p" cpu_$TRACE)]
        # sub odd lines
        tot_cycles=$[$tot_cycles - $(sed -n "$[ $i*2 - 1] p" cpu_$TRACE)]
#        echo $tot_cycles
    done

    echo "cycles $tot_cycles for $stream"

    # Get MHz value and print to file
    MHz=$(echo "scale=4; ($MCCFG*$tot_cycles*$frameRate)/($numFrames*1000000)" | bc)
    echo $MHz Mhz
    printf "%s %10.1f %s\n" "$(basename $stream)" "$MHz" "MHz" >> performance_$CPU.log

done

# Remove temp files
rm armStarter.txt cpu_trace.log tempf

