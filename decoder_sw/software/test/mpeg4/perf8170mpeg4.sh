TRACE=trace.log
EXECUTABLE=mx170dec_ads11
TF=tempf

#ARMCPU=ARM926EJ-S
#ARMCLOCK=266M
#MCCFG=2 # bus speed is ARMCLOCK/MCCFG

ARMCPU=ARM1136J-S
ARMCLOCK=399M
MCCFG=3 # bus speed is ARMCLOCK/MCCFG




MY_ARMCONF=`env |grep ARMCONF| awk 'BEGIN{RS="[;,|}]"} /ARMCONF/{print}'|sed -e 's/ARMCONF=//'`
echo
echo my armconf is $MY_ARMCONF
echo
if [ -z "$MY_ARMCONF" ]
then
    echo "Copy arm configuration settings to your home directory "
    echo "and set ARMCONF environment variable properly"
    exit
fi

cat $MY_ARMCONF/default.ami |sed -e 's/^MCCFG=[0-9]/MCCFG='$MCCFG'/' >$MY_ARMCONF/tmp.ami
mv $MY_ARMCONF/tmp.ami $MY_ARMCONF/default.ami

armsd -cpu $ARMCPU -clock $ARMCLOCK -script s.txt $EXECUTABLE -X $1 > $TRACE

# armsd script tags cycle count numbers with "cycles_", grep with that
grep ^cycles_ $TRACE > $TF
sed  's/cycles_//g' $TF > cpu_$TRACE

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
    echo $tot_cycles
done

echo "cycles $tot_cycles for $1. $[($tot_cycles*$MCCFG+500)/1000] kHz"
echo "$1  $[$tot_cycles*$MCCFG]" >> performance.log

