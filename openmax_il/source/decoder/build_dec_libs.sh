#!/bin/bash

DEC_PATH=../../../decoder_sw

ARCH_FLAG=-m32
FIFO_DATATYPE=g1_addr_t

if [ $# -eq 0 ]
  then
    echo ""
    echo " Usage: build_dec_libs.sh <target options>"
    echo ""
    echo " Available targets:"
    echo "  clean       clean build"
    echo "  pclinux     build decoder libraries without PP support for SW model testing"
    echo "  arm_pclinux build decoder libraries without PP support for SW model testing at arm platform"
    echo "  pclinux_pp  build decoder libraries with PP support for SW model testing"
    echo "  versatile   build decoder libraries for versatile platform"
    echo ""
    echo " Available options:"
    echo "  64          64-bit environment"
    echo "  ext         enable support for external frame buffers"
    echo ""
    exit 1
fi

if [ "$2" == "64" ] || [ "$3" == "64" ] ; then
    ARCH_FLAG=-m64
fi

if [ "$2" == "ext" ] || [ "$3" == "ext" ] ; then
    export USE_EXTERNAL_BUFFER="y" USE_OUTPUT_RELEASE="y" CLEAR_HDRINFO_IN_SEEK="y" USE_FRAME_PRED_CHECK_INTERLACE="n"
fi

export FIFO_DATATYPE

export USE_OMXIL_BUFFER="y"


if [ "$1" == "clean" ] ; then
    make -C ${DEC_PATH}/software/linux/avs/ clean
    make -C ${DEC_PATH}/software/linux/h264high/ clean
    make -C ${DEC_PATH}/software/linux/jpeg/ clean
    make -C ${DEC_PATH}/software/linux/mpeg2/ clean
    make -C ${DEC_PATH}/software/linux/mpeg4/ clean
    make -C ${DEC_PATH}/software/linux/rv/ clean
    make -C ${DEC_PATH}/software/linux/vc1/ clean
    make -C ${DEC_PATH}/software/linux/vp6/ clean
    make -C ${DEC_PATH}/software/linux/vp8/ clean
    make -C ${DEC_PATH}/software/linux/pp/ clean
    make -C ${DEC_PATH}/system/models/g1hw/ clean
    make -C ${DEC_PATH}/software/test/common/swhw/ clean
    make -C ${DEC_PATH}/software/linux/dwl/ clean
    make -C ${DEC_PATH}/software/test/rv/rm_parser/ clean
    make -C ${DEC_PATH}/software/test/common/utils/ clean
fi

if [ "$1" == "pclinux" ] ; then
    make -C ${DEC_PATH}/software/linux/avs/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/h264high/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/jpeg/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/mpeg2/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/mpeg4/ pclinux M32=${ARCH_FLAG} CUSTOM_FMT_SUPPORT="y"
    make -C ${DEC_PATH}/software/linux/rv/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/vc1/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/vp6/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/vp8/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/pp/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/test/common/swhw/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/dwl/ pclinux _DWL_PCLINUX=y  M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/system/models/g1hw/ M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/test/rv/rm_parser/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/test/common/utils/ pclinux M32=${ARCH_FLAG}
fi

if [ "$1" == "arm_pclinux" ] ; then
    make -C ${DEC_PATH}/software/linux/avs/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/h264high/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/jpeg/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/mpeg2/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/mpeg4/ arm_pclinux CUSTOM_FMT_SUPPORT="y"
    make -C ${DEC_PATH}/software/linux/rv/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/vc1/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/vp6/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/vp8/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/pp/ arm_pclinux
    make -C ${DEC_PATH}/software/test/common/swhw/ arm_pclinux
    make -C ${DEC_PATH}/software/linux/dwl/ arm_pclinux _DWL_PCLINUX=y
    make -C ${DEC_PATH}/system/models/g1hw/ arm_pclinux
    make -C ${DEC_PATH}/software/test/rv/rm_parser/ arm_pclinux
    make -C ${DEC_PATH}/software/test/common/utils/ arm_pclinux
fi

if [ "$1" == "versatile" ] ; then
    make -C ${DEC_PATH}/software/linux/avs/ versatile
    make -C ${DEC_PATH}/software/linux/h264high/ versatile
    make -C ${DEC_PATH}/software/linux/jpeg/ versatile
    make -C ${DEC_PATH}/software/linux/mpeg2/ versatile
    make -C ${DEC_PATH}/software/linux/mpeg4/ versatile CUSTOM_FMT_SUPPORT="y"
    make -C ${DEC_PATH}/software/linux/rv/ versatile
    make -C ${DEC_PATH}/software/linux/vc1/ versatile
    make -C ${DEC_PATH}/software/linux/vp6/ versatile
    make -C ${DEC_PATH}/software/linux/vp8/ versatile
    make -C ${DEC_PATH}/software/linux/pp/ versatile
    make -C ${DEC_PATH}/software/linux/dwl/ versatile
    make -C ${DEC_PATH}/software/test/rv/rm_parser/ versatile
    make -C ${DEC_PATH}/software/test/common/utils/ versatile
fi

PP_SUPPORT="-DPP_H264DEC_PIPELINE_SUPPORT -DPP_MPEG4DEC_PIPELINE_SUPPORT -DPP_JPEGDEC_PIPELINE_SUPPORT \
-DPP_VC1DEC_PIPELINE_SUPPORT -DPP_MPEG2DEC_PIPELINE_SUPPORT -DPP_RVDEC_PIPELINE_SUPPORT -DPP_AVSDEC_PIPELINE_SUPPORT \
-DPP_VP6DEC_PIPELINE_SUPPORT -DPP_VP8DEC_PIPELINE_SUPPORT"

if [ "$1" == "pclinux_pp" ] ; then
    make -C ${DEC_PATH}/software/linux/pp/ pclinux M32=${ARCH_FLAG} PIPELINE_SUPPORT="$PP_SUPPORT" IS_8190=1
    make -C ${DEC_PATH}/software/linux/avs/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/h264high/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/jpeg/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/mpeg2/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/mpeg4/ pclinux M32=${ARCH_FLAG} CUSTOM_FMT_SUPPORT="y"
    make -C ${DEC_PATH}/software/linux/rv/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/vc1/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/vp6/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/vp8/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/test/common/swhw/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/linux/dwl/ pclinux _DWL_PCLINUX=y M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/system/models/g1hw/ M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/test/rv/rm_parser/ pclinux M32=${ARCH_FLAG}
    make -C ${DEC_PATH}/software/test/common/utils/ M32=${ARCH_FLAG}
fi

if [ "$1" == "pclinux_pp_video" ] ; then
#    PP_SUPPORT="-DPP_H264DEC_PIPELINE_SUPPORT -DPP_VP8DEC_PIPELINE_SUPPORT"
PP_SUPPORT="-DPP_H264DEC_PIPELINE_SUPPORT -DPP_MPEG4DEC_PIPELINE_SUPPORT -DPP_VC1DEC_PIPELINE_SUPPORT \
-DPP_MPEG2DEC_PIPELINE_SUPPORT -DPP_RVDEC_PIPELINE_SUPPORT -DPP_AVSDEC_PIPELINE_SUPPORT \
-DPP_VP6DEC_PIPELINE_SUPPORT -DPP_VP8DEC_PIPELINE_SUPPORT"

    make -C ${DEC_PATH}/software/linux/pp/ pclinux M32=-m32 PIPELINE_SUPPORT="$PP_SUPPORT" IS_8190=1
    cp ${DEC_PATH}/software/linux/pp/libdecx170p.a ${DEC_PATH}/software/linux/pp/libdecx170p_v.a
fi

if [ "$1" == "pclinux_pp_image" ] ; then
    PP_SUPPORT="-DPP_JPEGDEC_PIPELINE_SUPPORT -DPP_VP8DEC_PIPELINE_SUPPORT"

    make -C ${DEC_PATH}/software/linux/pp/ pclinux M32=-m32 PIPELINE_SUPPORT="$PP_SUPPORT" IS_8190=1
    cp ${DEC_PATH}/software/linux/pp/libdecx170p.a ${DEC_PATH}/software/linux/pp/libdecx170p_i.a
fi
