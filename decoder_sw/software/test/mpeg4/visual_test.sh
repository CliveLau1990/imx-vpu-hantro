#!/bin/bash
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
#--   Abstract     : Functions for the SW verification of x170 HW decoder     --
#--                                                                           --
#-------------------------------------------------------------------------------

#=====-------------------------------------------------------------------------------=====#
#=====---                     Decode and perform visual check                   -----=====#
#=====-------------------------------------------------------------------------------=====#

. ../scripts/config.sh
. ../scripts/functions.sh



# Get command line arguments
if [ $# -lt 1 ] || [ $# -gt 3 ]
then
  # Invalid amount of arguments, show instructions and exit
  echo "usage: ./visual_test.sh <case> <reference: y=reference compare n=visual check><use ASP reference>"
  echo "case can also be asp"
else

    if [ "$coverage" == "y" ]
    then
        SW=mx170dec_coverage
    else
        SW=mx170dec_pclinux
    fi

    DIR=vs
    CASE=$1
    COMPILE=n
    REFMODDIR=refmod
    REFERENCE=$2
    ASP=$3
    REFOUT=out.yuv
    CONFIG_FILE=tb.cfg

    # Initialisation run variable
    initialized=0

    export CURRENT_MODEL="mpeg4"

    echo "copy <name>.yuv files to <name>.bak"
    for oldyuv in *.yuv
    do
        mv $oldyuv $oldyuv.bak
    done


    mkdir $DIR
    cd $DIR

    mv $CONFIG_FILE _tb.cfg


    if [ "$packetmode" == "y" ]
    then
    echo "creating tb"
     echo "TbParams {" >> $CONFIG_FILE
     echo    "PacketByPacket = ENABLED;" >> $CONFIG_FILE
     echo    "}" >> $CONFIG_FILE
     cp ../$CONFIG_FILE ../_tb.cfg
     cp $CONFIG_FILE ../
    fi

    if [ ! -e 8170_decoder/system/ ]
    then
        cvs co 8170_decoder/system/
    else
        cvs update 8170_decoder/system/scripts/testcase_list
    fi

    . 8170_decoder/system/scripts/testcase_list
    cd -

    if [ "$CASE" == "asp" ]
    then
        echo "$CASE"
        caselist=$mpeg4_asp_conformance
        $ASP = y
    elif [ "$CASE" == "error" ]
    then
        caselist=$mpeg4_error
        $ASP = n
    else
        echo case
        echo "$CASE"
        caselist=$CASE
    fi

    echo $caselist

    for CURRENT_CASE in $caselist
    do
        if [ $REFERENCE == y ]
        then
            if [ ! -e $REFMODDIR ]
            then
                mkdir $REFMODDIR
                cd  $REFMODDIR
                cvs co -r sw8170mpeg4_0_3_a 8170_decoder/software
                cvs co -r syshw8170_0_3_a 8170_decoder/system/
                make -C 8170_decoder/software/test/mpeg4/ pclinux
                cd -
            fi
        fi


        if [ ! -e ""$SW"" ] || [  "$COMPILE" == y  ]
        then
            if [ "$coverage" == "y" ]
            then
                echo "Make coverage"
                make coverage
            else
                make pclinux
            fi
        fi

        rm $DIR/*.yuv
        cp $SW $DIR

        cd $DIR
        cvs co decoder_test_data/testdata/case_$CURRENT_CASE

        # Run SW

        ./$SW -P $mpeg4_whole_stream decoder_test_data/testdata/case_$CURRENT_CASE/stream.mpeg4

        cd ..
        pwd

        if [ ! -e testdata ]
        then
            mkdir testdata
        fi
        pwd

        ls $CONFIG_FILE

        rm -f testdata/semplanar.yuv out.yuv

        if [ "$ASP" == y ]
        then
            $REFMODDIR/8170_decoder/software/test/mpeg4/mx170dec_pclinux -P  \
            -Otestdata/semplanar.yuv $DIR/decoder_test_data/testdata/case_$CURRENT_CASE/stream.mpeg4
        else
            echo trc > trace.cfg
            /home/mahe/mp4sw/mp4_mod_adder  -F \
                $DIR/decoder_test_data/testdata/case_$CURRENT_CASE/stream.mpeg4
            mv ref.yuv testdata/semplanar.yuv
        fi

        ln -s $DIR/*.yuv out.yuv
    #    cp $REFOUT testdata/semplanar.yuv
        ls testdata/


        if [ "$REFERENCE" == y ]
        then

            export CURRENT_CASENUM=$CURRENT_CASE
            checkCase
            reportCSV

        else
            /afs/hantro.com/projects/adder/users/mahe/viewsemiplanar/viewyuv -c $DIR/*.yuv testdata/semplanar.yuv
        fi
    done

    if [ "$REFERENCE" == y ]
    then
        writeCSVFile
    fi
fi
