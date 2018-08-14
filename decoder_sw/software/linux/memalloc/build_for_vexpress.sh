#!/bin/sh

KDIR=/wd500gb/export/Testing/Board_Version_Control/SW_Common/VExpress/linux-linaro-3.2-2012.01-0

HLINA_START=0x84000000
HLINA_SIZE=400 #Size in megabytes

HLINA_END=$(($HLINA_START + $HLINA_SIZE*1024*1024))

echo
echo "KDIR=$KDIR"
echo "CROSS=$CROSS"
echo
echo   "Linear memory base   = $HLINA_START"
printf "Linear top-of-memory = 0x%x\n" $HLINA_END
echo   "Linear memory size   = ${HLINA_SIZE}MB"
echo

make KDIR=$KDIR CROSS=$CROSS HLINA_START=${HLINA_START}U HLINA_SIZE=${HLINA_SIZE}U $1

