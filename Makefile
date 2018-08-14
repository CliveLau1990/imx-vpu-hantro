CC = $(CROSS_COMPILE)gcc --sysroot=$(SDKTARGETSYSROOT)
AR = $(CROSS_COMPILE)ar
CFLAGS ?= -O2

SOURCE_ROOT = decoder_sw/software
OMX_ROOT = openmax_il

INCLUDE_HEADERS = -I./decoder_sw -I$(SOURCE_ROOT)/source/inc -I$(SOURCE_ROOT)/source/hevc \
           -I$(SOURCE_ROOT)/source/config -I$(SOURCE_ROOT)/source/dwl -I$(SOURCE_ROOT)/source/common -I$(SOURCE_ROOT)/source/vp9

INCLUDE_HEADERS += -I$(SOURCE_ROOT)/linux/memalloc
#INCLUDE_HEADERS += -I$(SOURCE_ROOT)/linux/ldriver
INCLUDE_HEADERS += -I$(LINUX_KERNEL_ROOT)/include/uapi -I$(LINUX_KERNEL_ROOT)/include

CFLAGS += -DDEC_MODULE_PATH=\"/dev/mxc_hantro\" -DUSE_FAKE_RFC_TABLE -DFIFO_DATATYPE=void* -DNDEBUG -DDOWN_SCALER \
           -DUSE_EXTERNAL_BUFFER -DUSE_FAST_EC -DUSE_VP9_EC -DGET_FREE_BUFFER_NON_BLOCK \
           -DDEC_X170_OUTPUT_FORMAT=0 -DDEC_X170_TIMEOUT_LENGTH=-1 -DENABLE_HEVC_SUPPORT \
           -DENABLE_VP9_SUPPORT -DUSE_ION

#CFLAGS += -D_SW_DEBUG_PRINT -D_DWL_DEBUG
#CFLAGS += -DCLEAR_HDRINFO_IN_SEEK
CFLAGS += -DDWL_DISABLE_REG_PRINTS
CFLAGS += -DDWL_USE_DEC_IRQ
CFLAGS += -DUSE_64BIT_ENV
CFLAGS += -DGET_OUTPUT_BUFFER_NON_BLOCK
CFLAGS +=  -DHANTRODEC_STREAM_SWAP=15
#CFLAGS +=  -DUSE_OMXIL_BUFFER
CFLAGS +=  -DUSE_OUTPUT_RELEASE
CFLAGS += -DUSE_PICTURE_DISCARD  # for vp9

CFLAGS += $(INCLUDE_HEADERS)

DWL_OBJS = $(SOURCE_ROOT)/linux/dwl/dwl_linux.o \
           $(SOURCE_ROOT)/linux/dwl/dwl_linux_hw.o \
           $(SOURCE_ROOT)/linux/dwl/dwl_activity_trace.o \
           $(SOURCE_ROOT)/linux/dwl/dwl_buf_protect.o

COMMON_OBJS = $(SOURCE_ROOT)/source/common/bqueue.o \
           $(SOURCE_ROOT)/source/common/commonconfig.o \
           $(SOURCE_ROOT)/source/common/fifo.o \
           $(SOURCE_ROOT)/source/common/raster_buffer_mgr.o \
           $(SOURCE_ROOT)/source/common/regdrv.o \
           $(SOURCE_ROOT)/source/common/sw_stream.o \
           $(SOURCE_ROOT)/source/common/input_queue.o \
           $(SOURCE_ROOT)/source/common/sw_util.o \
           $(SOURCE_ROOT)/source/common/stream_corrupt.o

HEVC_OBJS = $(SOURCE_ROOT)/source/hevc/hevc_asic.o \
           $(SOURCE_ROOT)/source/hevc/hevc_byte_stream.o \
           $(SOURCE_ROOT)/source/hevc/hevcdecapi.o \
           $(SOURCE_ROOT)/source/hevc/hevc_decoder.o \
           $(SOURCE_ROOT)/source/hevc/hevc_dpb.o \
           $(SOURCE_ROOT)/source/hevc/hevc_fb_mngr.o \
           $(SOURCE_ROOT)/source/hevc/hevc_nal_unit.o \
           $(SOURCE_ROOT)/source/hevc/hevc_pic_order_cnt.o \
           $(SOURCE_ROOT)/source/hevc/hevc_pic_param_set.o \
           $(SOURCE_ROOT)/source/hevc/hevc_seq_param_set.o \
           $(SOURCE_ROOT)/source/hevc/hevc_slice_header.o \
           $(SOURCE_ROOT)/source/hevc/hevc_storage.o \
           $(SOURCE_ROOT)/source/hevc/hevc_util.o \
           $(SOURCE_ROOT)/source/hevc/hevc_exp_golomb.o \
           $(SOURCE_ROOT)/source/hevc/hevc_vui.o \
           $(SOURCE_ROOT)/source/hevc/hevc_sei.o \
           $(SOURCE_ROOT)/source/hevc/hevc_video_param_set.o

VP9_OBJS = $(SOURCE_ROOT)/source/vp9/vp9decapi.o \
           $(SOURCE_ROOT)/source/vp9/vp9hwd_asic.o \
           $(SOURCE_ROOT)/source/vp9/vp9hwd_bool.o \
           $(SOURCE_ROOT)/source/vp9/vp9hwd_buffer_queue.o \
           $(SOURCE_ROOT)/source/vp9/vp9hwd_decoder.o \
           $(SOURCE_ROOT)/source/vp9/vp9hwd_headers.o \
           $(SOURCE_ROOT)/source/vp9/vp9hwd_output.o \
           $(SOURCE_ROOT)/source/vp9/vp9hwd_probs.o \
           $(SOURCE_ROOT)/source/vp9/vp9_modecontext.o \
           $(SOURCE_ROOT)/source/vp9/vp9_entropymode.o \
           $(SOURCE_ROOT)/source/vp9/vp9_entropymv.o \
           $(SOURCE_ROOT)/source/vp9/vp9_treecoder.o \
           $(SOURCE_ROOT)/source/vp9/vp9_modecont.o

DWL_OBJS_G1 = $(SOURCE_ROOT)/linux/dwl/dwl_linux.o \
           $(SOURCE_ROOT)/linux/dwl/dwl_linux_sc.o \
           $(SOURCE_ROOT)/linux/dwl/dwl_activity_trace.o

OBJ = $(DWL_OBJS) $(COMMON_OBJS) $(HEVC_OBJS) $(VP9_OBJS)

LIBG1NAME = libg1
LIBG1_LIBS = $(SOURCE_ROOT)/linux/h264high/libdecx170h.a 
LIBG1_LIBS += $(SOURCE_ROOT)/linux/mpeg4/libdecx170m.a 
LIBG1_LIBS += $(SOURCE_ROOT)/linux/mpeg2/libdecx170m2.a \
                     $(SOURCE_ROOT)/linux/vc1/libdecx170v.a $(SOURCE_ROOT)/linux/vp6/libdec8190vp6.a $(SOURCE_ROOT)/linux/vp8/libdecx170vp8.a \
                     $(SOURCE_ROOT)/linux/avs/libdecx170a.a $(SOURCE_ROOT)/linux/jpeg/libx170j.a $(SOURCE_ROOT)/linux/rv/libdecx170rv.a

LIBNAME = libhantro
SONAMEVERSION=1

LIBCODECNAME = libcodec
LIBSCODEC = -L./ -lhantro -lg1 -lpthread

RELEASE_BIN=bin
LIBG1COMMONNAME=libcommon_g1

VERSION = imx8mq

all: $(LIBNAME).so $(LIBNAME).a $(LIBG1NAME).so $(LIBG1NAME).a $(LIBCODECNAME) test

install: install_headers
	@mkdir -p $(DEST_DIR)$(libdir)
	@mkdir -p $(DEST_DIR)/unit_tests/VPU/hantro
	cp -P $(LIBNAME).so* $(DEST_DIR)$(libdir)
	cp -P $(LIBG1NAME).so* $(DEST_DIR)$(libdir)
	cp -P $(LIBCODECNAME).so* $(DEST_DIR)$(libdir)
	cp -P $(LIBG1COMMONNAME).so* $(DEST_DIR)$(libdir)
	cp $(RELEASE_BIN)/* $(DEST_DIR)/unit_tests/VPU/hantro

install_headers:
	@mkdir -p $(DEST_DIR)/usr/include
	cp $(SOURCE_ROOT)/source/inc/*.h $(DEST_DIR)/usr/include
	cp $(OMX_ROOT)/source/decoder/*.h $(DEST_DIR)/usr/include
	cp $(OMX_ROOT)/source/*.h $(DEST_DIR)/usr/include
	cp $(OMX_ROOT)/headers/*.h $(DEST_DIR)/usr/include

%.o: %.c
	$(CC) -Wall -fPIC $(CFLAGS) -c $^ -o $@

$(LIBCODECNAME):
	make -f Makefile_codec CC="$(CC)" AR="$(AR)" LIBNAME="$(LIBCODECNAME)"

$(LIBG1NAME).so.$(SONAMEVERSION): $(LIBG1NAME).a $(DWL_OBJS_G1)
#	$(CC) -o $@ $(LDFLAGS) -shared -nostartfiles -Wl,-soname,$@ -Wl,--whole-archive $(LIBG1_LIBS) -Wl,--no-whole-archive
	$(CC) -o $@ $(LDFLAGS) -shared -nostartfiles -Wl,-Bsymbolic -Wl,-z -Wl,muldefs -Wl,-soname,$@ $(DWL_OBJS_G1) -Wl,--whole-archive $(LIBG1_LIBS) -Wl,--no-whole-archive

$(LIBG1NAME).so: $(LIBG1NAME).so.$(SONAMEVERSION)
	ln -fs $< $@

$(LIBG1NAME).a:
	make -C $(SOURCE_ROOT)/linux/h264high versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/mpeg4 versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y USE_PICTURE_DISCARD=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/mpeg2 versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y USE_PICTURE_DISCARD=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/vc1 versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y USE_PICTURE_DISCARD=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/vp6 versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y USE_PICTURE_DISCARD=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/vp8 versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y USE_PICTURE_DISCARD=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/jpeg versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/rv versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y USE_PICTURE_DISCARD=y CC="$(CC)" AR="$(AR) -rc"
	make -C $(SOURCE_ROOT)/linux/avs versatile M32=-m64 CLEAR_HDRINFO_IN_SEEK=n USE_EXTERNAL_BUFFER=y USE_OUTPUT_RELEASE=y USE_DEC_IRQ=y USE_NON_BLOCKING=y USE_ONE_THREAD_WAITCC=y USE_PICTURE_DISCARD=y CC="$(CC)" AR="$(AR) -rc"
	$(AR) -rc $@ $(LIBG1_LIBS)


#$(LIBNAME).so.$(SONAMEVERSION): $(OBJ) $(LIBG1NAME).a
#	$(CC) -o $@ $(LDFLAGS) -shared -nostartfiles -Wl,-soname,$@ $(OBJ) $(LIBG1_LIBS)

$(LIBNAME).so.$(SONAMEVERSION): $(OBJ)
	$(CC) -o $@ $(LDFLAGS) -shared -nostartfiles -Wl,-Bsymbolic -Wl,-soname,$@ $(OBJ)

$(LIBNAME).so: $(LIBNAME).so.$(SONAMEVERSION)
	ln -fs $< $@

#$(LIBNAME).a: $(OBJ) $(LIBG1NAME).a
$(LIBNAME).a: $(OBJ)
	$(AR) -rc $@  $^

test:
	make -f Makefile_test RELEASE_DIR="$(RELEASE_BIN)" LIBCOMMONG1="$(LIBG1COMMONNAME)"

.PHONY: clean
clean:
	rm -f $(LIBNAME).* $(OBJ)
	rm -f $(LIBG1NAME).* $(DWL_OBJS_G1)
	rm -f $(LIBCODECNAME)*
	make -C $(SOURCE_ROOT)/linux/h264high clean
	make -C $(SOURCE_ROOT)/linux/mpeg4 clean
	make -C $(SOURCE_ROOT)/linux/mpeg2 clean
	make -C $(SOURCE_ROOT)/linux/vc1 clean
	make -C $(SOURCE_ROOT)/linux/vp6 clean
	make -C $(SOURCE_ROOT)/linux/vp8 clean
	make -C $(SOURCE_ROOT)/linux/jpeg clean
	make -C $(SOURCE_ROOT)/linux/rv clean
	make -C $(SOURCE_ROOT)/linux/avs clean
	make -f Makefile_codec LIBNAME="$(LIBCODECNAME)" clean
	make -f Makefile_test RELEASE_DIR="$(RELEASE_BIN)" LIBCOMMONG1="$(LIBG1COMMONNAME)" clean
