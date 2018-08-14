/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "trace.h"

/* NOTE! This is needed when user allocated memory used */
#ifdef LINUX
#include <fcntl.h>
#include <sys/mman.h>
#endif /* #ifdef LINUX */

#include "jpegdecapi.h"
#include "dwl.h"
#include "jpegdeccontainer.h"

#ifdef PP_PIPELINE_ENABLED
#include "ppapi.h"
#include "pptestbench.h"
#endif

#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

#include "deccfg.h"
#include "tb_cfg.h"
#include "regdrv_g1.h"
#include "tb_sw_performance.h"
#include "tb_stream_corrupt.h"


#ifndef MAX_PATH_
#define MAX_PATH   256  /* maximum lenght of the file path */
#endif
#define DEFAULT -1
#define JPEG_INPUT_BUFFER 0x5120

/* SW/SW testing, read stream trace file */
FILE *f_stream_trace = NULL;

static u32 mem_allocation = 0;

/* memory parameters */
static u32 out_pic_size_luma;
static u32 out_pic_size_chroma;
static int fd_mem;
static JpegDecLinearMem output_address_y;
static JpegDecLinearMem output_address_cb_cr;
static u32 frame_ready = 0;
static u32 sliced_output_used = 0;
static u32 mode = 0;
static u32 ThumbDone = 0;
static u32 write_output = 1;
static u32 size_luma = 0;
static u32 size_chroma = 0;
static u32 slice_to_user = 0;
static u32 slice_size = 0;
static u32 non_interleaved = 0;
static i32 full_slice_counter = -1;
static u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
static u32 service_merge_disable = DEC_X170_SERVICE_MERGE_DISABLE;

/* stream start address */
u8 *byte_strm_start;

/* user allocated output */
struct DWLLinearMem user_alloc_luma;
struct DWLLinearMem user_alloc_chroma;
struct DWLLinearMem user_alloc_cr;

/* progressive parameters */
static u32 scan_counter = 0;
static u32 progressive = 0;
static u32 scan_ready = 0;
static u32 is8170_hw = 0;
static u32 nbr_of_images = 0;
static u32 nbr_of_thumb_images = 0;
static u32 nbr_of_images_total = 0;
static u32 nbr_of_thumb_images_total = 0;
static u32 nbr_of_images_to_out = 0;
static u32 nbr_of_thumb_images_to_out = 0;
static u32 thumb_in_stream = 0;
static u32 next_soi = 0;
static u32 stream_info_check = 0;
static u32 prev_output_width = 0;
static u32 prev_output_height = 0;
static u32 prev_output_format = 0;
static u32 prev_output_width_tn = 0;
static u32 prev_output_height_tn = 0;
static u32 prev_output_format_tn = 0;
static u32 pic_counter = 0;

/* prototypes */
u32 allocMemory(JpegDecInst dec_inst, JpegDecImageInfo * image_info,
                JpegDecInput * jpeg_in);
void calcSize(JpegDecImageInfo * image_info, u32 pic_mode);
void WriteOutput(u8 * data_luma, u32 pic_size_luma, u8 * data_chroma,
                 u32 pic_size_chroma, u32 pic_mode);
void WriteOutputLuma(u8 * data_luma, u32 pic_size_luma, u32 pic_mode);
void WriteOutputChroma(u8 * data_chroma, u32 pic_size_chroma, u32 pic_mode);
void WriteFullOutput(u32 pic_mode);

void handleSlicedOutput(JpegDecImageInfo * image_info, JpegDecInput * jpeg_in,
                        JpegDecOutput * jpeg_out);

void WriteCroppedOutput(JpegDecImageInfo * info, u8 * data_luma, u8 * data_cb,
                        u8 * data_cr);

void WriteProgressiveOutput(u32 size_luma, u32 size_chroma, u32 mode,
                            u8 * data_luma, u8 * data_cb, u8 * data_cr);
void printJpegVersion(void);

void decsw_performance(void) {
}

void *JpegDecMalloc(unsigned int size);
void *JpegDecMemset(void *ptr, int c, unsigned int size);
void JpegDecFree(void *ptr);

void PrintJpegRet(JpegDecRet * p_jpeg_ret);
void PrintGetImageInfo(JpegDecImageInfo * image_info);
u32 FindImageInfoEnd(u8 * stream, u32 stream_length, u32 * p_offset);
u32 FindImageEnd(u8 * stream, u32 stream_length, u32 * p_offset);
u32 FindImageEOI(u8 * stream, u32 stream_length, u32 * p_offset);
u32 FindImageTnEOI(u8 * stream, u32 stream_length, u32 * p_offset, u32 mode, u32 thumb_exist);

u32 planar_output = 0;
u32 only_full_resolution = 0;
u32 b_frames = 0;

#ifdef ASIC_TRACE_SUPPORT
u32 pic_number;
#endif

struct TBCfg tb_cfg;

#ifdef ASIC_TRACE_SUPPORT
extern u32 use_jpeg_idct;
extern u32 g_hw_ver;
#endif

#ifdef JPEG_EVALUATION
extern u32 g_hw_ver;
#endif

u32 crop = 0;
int main(int argc, char *argv[]) {
  u8 *input_buffer = NULL;
  u32 len;
  u32 stream_total_len = 0;
  u32 stream_seek_len = 0;

  u32 stream_in_file = 0;
  u32 mcu_size_divider = 0;

  i32 i, j = 0;
  u32 tmp = 0;
  u32 size = 0;
  u32 loop;
  u32 frame_counter = 0;
  u32 input_read_type = 0;
  i32 buffer_size = 0;
  u32 amount_of_mcus = 0;
  u32 mcu_in_row = 0;
  int ret;

  JpegDecInst jpeg;
  JpegDecRet jpeg_ret;
  JpegDecImageInfo image_info;
  JpegDecInput jpeg_in;
  JpegDecOutput jpeg_out;
  JpegDecApiVersion dec_ver;
  JpegDecBuild dec_build;

  struct DWLLinearMem stream_mem;

  u8 *p_image = NULL;

  FILE *fout = NULL;
  FILE *f_in = NULL;

  u32 clock_gating = DEC_X170_INTERNAL_CLOCK_GATING;
  u32 latency_comp = DEC_X170_LATENCY_COMPENSATION;
  u32 bus_burst_length = DEC_X170_BUS_BURST_LENGTH;
  u32 asic_service_priority = DEC_X170_ASIC_SERVICE_PRIORITY;
  u32 data_discard = DEC_X170_DATA_DISCARD_ENABLE;

#ifdef PP_PIPELINE_ENABLED
  PPApiVersion pp_ver;
  PPBuild pp_build;
#endif

  u32 stream_header_corrupt = 0;
  u32 seed_rnd = 0;
  u32 stream_bit_swap = 0;
  u32 stream_truncate = 0;
  FILE *f_tbcfg;
  u32 image_info_length = 0;
  u32 prev_ret = JPEGDEC_STRM_ERROR;

#ifdef ASIC_TRACE_SUPPORT
  g_hw_ver = 8190;   /* default to 8190 mode */
#endif

#ifdef JPEG_EVALUATION_8170
  g_hw_ver = 8170;
#elif JPEG_EVALUATION_8190
  g_hw_ver = 8190;
#elif JPEG_EVALUATION_9170
  g_hw_ver = 9170;
#elif JPEG_EVALUATION_9190
  g_hw_ver = 9190;
#elif JPEG_EVALUATION_G1
  g_hw_ver = 10000;
#endif

#ifndef EXPIRY_DATE
#define EXPIRY_DATE (u32)0xFFFFFFFF
#endif /* EXPIRY_DATE */

  /* expiry stuff */
  {
    u8 tm_buf[7];
    time_t sys_time;
    struct tm *tm;
    u32 tmp1;

    /* Check expiry date */
    time(&sys_time);
    tm = localtime(&sys_time);
    strftime(tm_buf, sizeof(tm_buf), "%y%m%d", tm);
    tmp1 = 1000000 + atoi(tm_buf);
    if(tmp1 > (EXPIRY_DATE) && (EXPIRY_DATE) > 1) {
      fprintf(stderr,
              "EVALUATION PERIOD EXPIRED.\n"
              "Please contact On2 Sales.\n");
      return -1;
    }
  }

  /* allocate memory for stream buffer. if unsuccessful -> exit */
  stream_mem.virtual_address = NULL;
  stream_mem.bus_address = 0;
  stream_mem.mem_type = DWL_MEM_TYPE_CPU;


  INIT_SW_PERFORMANCE;

  fprintf(stdout, "\n* * * * * * * * * * * * * * * * \n\n\n"
          "      "
          "X170 JPEG TESTBENCH\n" "\n\n* * * * * * * * * * * * * * * * \n");

  /* reset input */
  jpeg_in.stream_buffer.virtual_address = NULL;
  jpeg_in.stream_buffer.bus_address = 0;
  jpeg_in.stream_length = 0;
  jpeg_in.picture_buffer_y.virtual_address = NULL;
  jpeg_in.picture_buffer_y.bus_address = 0;
  jpeg_in.picture_buffer_cb_cr.virtual_address = NULL;
  jpeg_in.picture_buffer_cb_cr.bus_address = 0;

  /* reset output */
  jpeg_out.output_picture_y.virtual_address = NULL;
  jpeg_out.output_picture_y.bus_address = 0;
  jpeg_out.output_picture_cb_cr.virtual_address = NULL;
  jpeg_out.output_picture_cb_cr.bus_address = 0;
  jpeg_out.output_picture_cr.virtual_address = NULL;
  jpeg_out.output_picture_cr.bus_address = 0;

  /* reset image_info */
  image_info.display_width = 0;
  image_info.display_height = 0;
  image_info.output_width = 0;
  image_info.output_height = 0;
  image_info.version = 0;
  image_info.units = 0;
  image_info.x_density = 0;
  image_info.y_density = 0;
  image_info.output_format = 0;
  image_info.thumbnail_type = 0;
  image_info.display_width_thumb = 0;
  image_info.display_height_thumb = 0;
  image_info.output_width_thumb = 0;
  image_info.output_height_thumb = 0;
  image_info.output_format_thumb = 0;
  image_info.coding_mode = 0;
  image_info.coding_mode_thumb = 0;

  /* set default */
  buffer_size = 0;
  jpeg_in.slice_mb_set = 0;
  jpeg_in.buffer_size = 0;

#ifndef PP_PIPELINE_ENABLED
  if(argc < 2) {
#ifndef ASIC_TRACE_SUPPORT
    fprintf(stdout, "USAGE:\n%s [-X] [-S] [-P] [-M] stream.jpg\n", argv[0]);
    fprintf(stdout, "\t-X to not to write output picture\n");
    fprintf(stdout, "\t-S file.hex stream control trace file\n");
    fprintf(stdout, "\t-P write planar output\n");
    fprintf(stdout, "\t-M Force to full resolution decoding only\n");
    printJpegVersion();
#else
    fprintf(stdout, "USAGE:\n%s [-X] [-S] [-P] [-R] [-F] [-M] stream.jpg\n",
            argv[0]);
    fprintf(stdout, "\t-X to not to write output picture\n");
    fprintf(stdout, "\t-S file.hex stream control trace file\n");
    fprintf(stdout, "\t-P write planar output\n");
    fprintf(stdout, "\t-R use reference idct (implies cropping)\n");
    fprintf(stdout, "\t-F Force 8170 mode to HW model\n");
    fprintf(stdout, "\t-M Force to full resolution decoding only\n");
#endif
    exit(100);
  }

  /* read cmdl parameters */
  for(i = 1; i < argc - 1; i++) {
    if(strncmp(argv[i], "-X", 2) == 0) {
      write_output = 0;
    } else if(strncmp(argv[i], "-S", 2) == 0) {
      f_stream_trace = fopen((argv[i] + 2), "r");
    } else if(strncmp(argv[i], "-P", 2) == 0) {
      planar_output = 1;
    } else if(strncmp(argv[i], "-M", 2) == 0) {
      only_full_resolution = 1;
      printf("\n\nForce to decode only full resolution image from stream\n\n");
    }
#ifdef ASIC_TRACE_SUPPORT
    else if(strncmp(argv[i], "-R", 2) == 0) {
      use_jpeg_idct = 1;
      crop = 1;
    } else if(strcmp(argv[i], "-F") == 0) {
      g_hw_ver = 8170;
      is8170_hw = 1;
      printf("\n\nForce 8170 mode to HW model!!!\n\n");
    }
#endif
    else {
      fprintf(stdout, "UNKNOWN PARAMETER: %s\n", argv[i]);
      return 1;
    }
  }

  /* Print API and build version numbers */
  dec_ver = JpegGetAPIVersion();
  dec_build = JpegDecGetBuild();

  /* Version */
  fprintf(stdout,
          "\nX170 JPEG Decoder API v%d.%d - SW build: %d - HW build: %x\n",
          dec_ver.major, dec_ver.minor, dec_build.sw_build, dec_build.hw_build);
#else
  if(argc < 3) {
    fprintf(stdout, "USAGE:\n%s [-X] stream.jpg pp.cfg\n", argv[0]);
    fprintf(stdout, "\t-X to not to write output picture\n");
    fprintf(stdout, "\t-F Force 8170 mode to HW model\n");
    exit(100);
  }

  /* read cmdl parameters */
  for(i = 1; i < argc - 2; i++) {
    if(strncmp(argv[i], "-X", 2) == 0) {
      write_output = 0;
    } else if(strcmp(argv[i], "-F") == 0) {
      is8170_hw = 1;
      printf("\n\nForce 8170 mode to HW model!!!\n\n");
    } else {
      fprintf(stdout, "UNKNOWN PARAMETER: %s\n", argv[i]);
      return 1;
    }
  }

  /* Print API and build version numbers */
  dec_ver = JpegGetAPIVersion();
  dec_build = JpegDecGetBuild();

  /* Version */
  fprintf(stdout,
          "\nX170 JPEG Decoder API v%d.%d - SW build: %d - HW build: %x\n",
          dec_ver.major, dec_ver.minor, dec_build.sw_build, dec_build.hw_build);

  /* Print API and build version numbers */
  pp_ver = PPGetAPIVersion();
  pp_build = PPGetBuild();

  /* Version */
  fprintf(stdout,
          "\nX170 PP API v%d.%d - SW build: %d - HW build: %x\n",
          pp_ver.major, pp_ver.minor, pp_build.sw_build, pp_build.hw_build);

#endif

  /* check if 8170 HW */
  is8170_hw = (dec_build.hw_build >> 16) == 0x8170U ? 1 : 0;

  /* set test bench configuration */
  TBSetDefaultCfg(&tb_cfg);
  f_tbcfg = fopen("tb.cfg", "r");
  if(f_tbcfg == NULL) {
    printf("UNABLE TO OPEN INPUT FILE: \"tb.cfg\"\n");
    printf("USING DEFAULT CONFIGURATION\n");
  } else {
    fclose(f_tbcfg);
    if(TBParseConfig("tb.cfg", TBReadParam, &tb_cfg) == TB_FALSE)
      return -1;
    if(TBCheckCfg(&tb_cfg) != 0)
      return -1;
  }
  /*TBPrintCfg(&tb_cfg); */
  mem_allocation = TBGetDecMemoryAllocation(&tb_cfg);
  jpeg_in.buffer_size = tb_cfg.dec_params.jpeg_input_buffer_size;
  jpeg_in.slice_mb_set = tb_cfg.dec_params.jpeg_mcus_slice;
  clock_gating = TBGetDecClockGating(&tb_cfg);
  data_discard = TBGetDecDataDiscard(&tb_cfg);
  latency_comp = tb_cfg.dec_params.latency_compensation;
  output_picture_endian = TBGetDecOutputPictureEndian(&tb_cfg);
  bus_burst_length = tb_cfg.dec_params.bus_burst_length;
  asic_service_priority = tb_cfg.dec_params.asic_service_priority;
  service_merge_disable = TBGetDecServiceMergeDisable(&tb_cfg);
  printf("Decoder Memory Allocation %d\n", mem_allocation);
  printf("Decoder Jpeg Input Buffer Size %d\n", jpeg_in.buffer_size);
  printf("Decoder Slice MB Set %d\n", jpeg_in.slice_mb_set);
  printf("Decoder Clock Gating %d\n", clock_gating);
  printf("Decoder Data Discard %d\n", data_discard);
  printf("Decoder Latency Compensation %d\n", latency_comp);
  printf("Decoder Output Picture Endian %d\n", output_picture_endian);
  printf("Decoder Bus Burst Length %d\n", bus_burst_length);
  printf("Decoder Asic Service Priority %d\n", asic_service_priority);

  seed_rnd = tb_cfg.tb_params.seed_rnd;
  stream_header_corrupt = TBGetTBStreamHeaderCorrupt(&tb_cfg);
  stream_truncate = TBGetTBStreamTruncate(&tb_cfg);
  if(strcmp(tb_cfg.tb_params.stream_bit_swap, "0") != 0) {
    stream_bit_swap = 1;
  } else {
    stream_bit_swap = 0;
  }
  printf("TB Seed Rnd %d\n", seed_rnd);
  printf("TB Stream Truncate %d\n", stream_truncate);
  printf("TB Stream Header Corrupt %d\n", stream_header_corrupt);
  printf("TB Stream Bit Swap %d; odds %s\n", stream_bit_swap,
         tb_cfg.tb_params.stream_bit_swap);

  {
    remove("output.hex");
    remove("registers.hex");
    remove("picture_ctrl.hex");
    remove("picture_ctrl.trc");
    remove("jpeg_tables.hex");
    remove("out.yuv");
    remove("out_chroma.yuv");
    remove("out_tn.yuv");
    remove("out_chroma_tn.yuv");
  }

  /* after thumnails done ==> decode full images */
start_full_decode:

  /******** PHASE 1 ********/
  fprintf(stdout, "\nPhase 1: INIT JPEG DECODER\n");

  /* Jpeg initialization */
  START_SW_PERFORMANCE;
  decsw_performance();
  jpeg_ret = JpegDecInit(&jpeg);
  END_SW_PERFORMANCE;
  decsw_performance();
  if(jpeg_ret != JPEGDEC_OK) {
    /* Handle here the error situation */
    PrintJpegRet(&jpeg_ret);
    goto end;
  }

#ifdef PP_PIPELINE_ENABLED
  /* Initialize the post processer. If unsuccessful -> exit */
  if(pp_startup
      (argv[argc - 1], jpeg, PP_PIPELINED_DEC_TYPE_JPEG, &tb_cfg) != 0) {
    fprintf(stdout, "PP INITIALIZATION FAILED\n");
    goto end;
  }

  if(pp_update_config
      (jpeg, PP_PIPELINED_DEC_TYPE_JPEG, &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
    goto end;
  }
#endif

  /* NOTE: The registers should not be used outside decoder SW for other
   * than compile time setting test purposes */
  SetDecRegister(((JpegDecContainer *) jpeg)->jpeg_regs, HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((JpegDecContainer *) jpeg)->jpeg_regs, HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  SetDecRegister(((JpegDecContainer *) jpeg)->jpeg_regs, HWIF_DEC_OUT_ENDIAN,
                 output_picture_endian);
  SetDecRegister(((JpegDecContainer *) jpeg)->jpeg_regs, HWIF_DEC_MAX_BURST,
                 bus_burst_length);
  if ((DWLReadAsicID(DWL_CLIENT_TYPE_JPEG_DEC) >> 16) == 0x8170U) {
    SetDecRegister(((JpegDecContainer *) jpeg)->jpeg_regs, HWIF_PRIORITY_MODE,
                   asic_service_priority);
    SetDecRegister(((JpegDecContainer *) jpeg)->jpeg_regs, HWIF_DEC_DATA_DISC_E,
                   data_discard);
  }
  SetDecRegister(((JpegDecContainer *) jpeg)->jpeg_regs, HWIF_SERV_MERGE_DIS,
                 service_merge_disable);

  fprintf(stdout, "PHASE 1: INIT JPEG DECODER successful\n");

  /******** PHASE 2 ********/
  fprintf(stdout, "\nPhase 2: OPEN/READ FILE \n");

#ifdef ASIC_TRACE_SUPPORT
  tmp = openTraceFiles();
  if(!tmp) {
    fprintf(stdout, "Unable to open trace file(s)\n");
  }
#endif

reallocate_input_buffer:

#ifndef PP_PIPELINE_ENABLED
  /* Reading input file */
  f_in = fopen(argv[argc - 1], "rb");
  if(f_in == NULL) {
    fprintf(stdout, "Unable to open input file\n");
    exit(-1);
  }
#else
  /* Reading input file */
  f_in = fopen(argv[argc - 2], "rb");
  if(f_in == NULL) {
    fprintf(stdout, "Unable to open input file\n");
    exit(-1);
  }
#endif

  /* file i/o pointer to full */
  fseek(f_in, 0L, SEEK_END);
  len = ftell(f_in);
  rewind(f_in);

  if(!stream_info_check) {
    fprintf(stdout, "\nPhase 2: CHECK THE CONTENT OF STREAM BEFORE ACTIONS\n");

    /* NOTE: The DWL should not be used outside decoder SW
     * here we call it because it is the easiest way to get
     * dynamically allocated linear memory
     * */

    /* allocate memory for stream buffer. if unsuccessful -> exit */
    if(DWLMallocLinear
        (((JpegDecContainer *) jpeg)->dwl, len, &stream_mem) != DWL_OK) {
      fprintf(stdout, "UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n");
      goto end;
    }

    fprintf(stdout, "\t-Input: Allocated buffer: virt: 0x%16lx bus: 0x%16lx\n",
            (addr_t)stream_mem.virtual_address, stream_mem.bus_address);

    /* memset input */
    (void) DWLmemset(stream_mem.virtual_address, 0, len);

    byte_strm_start = (u8 *) stream_mem.virtual_address;
    if(byte_strm_start == NULL) {
      fprintf(stdout, "UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n");
      goto end;
    }

    /* read input stream from file to buffer and close input file */
    ret = fread(byte_strm_start, sizeof(u8), len, f_in);

    fclose(f_in);

    jpeg_ret = FindImageEnd(byte_strm_start, len, &image_info_length);

    if(jpeg_ret != 0) {
      printf("EOI missing from end of file!\n");
    }

    if(stream_mem.virtual_address != NULL)
      DWLFreeLinear(((JpegDecContainer *) jpeg)->dwl, &stream_mem);

    /* set already done */
    stream_info_check = 1;

    fprintf(stdout, "PHASE 2: CHECK THE CONTENT OF STREAM BEFORE ACTIONS successful\n\n");
  }

#ifndef PP_PIPELINE_ENABLED
  /* Reading input file */
  f_in = fopen(argv[argc - 1], "rb");
  if(f_in == NULL) {
    fprintf(stdout, "Unable to open input file\n");
    exit(-1);
  }
#else
  /* Reading input file */
  f_in = fopen(argv[argc - 2], "rb");
  if(f_in == NULL) {
    fprintf(stdout, "Unable to open input file\n");
    exit(-1);
  }
#endif

  /* file i/o pointer to full */
  fseek(f_in, 0L, SEEK_END);
  len = ftell(f_in);
  rewind(f_in);

  TBInitializeRandom(seed_rnd);

  /* sets the stream length to random value */
  if(stream_truncate) {
    u32 ret = 0;

    printf("\tStream length %d\n", len);
    ret = TBRandomizeU32(&len);
    if(ret != 0) {
      printf("RANDOM STREAM ERROR FAILED\n");
      goto end;
    }
    printf("\tRandomized stream length %d\n", len);
  }

  /* Handle input buffer load */
  if(jpeg_in.buffer_size) {
    if(len > jpeg_in.buffer_size) {
      stream_total_len = len;
      len = jpeg_in.buffer_size;
    } else {
      stream_total_len = len;
      len = stream_total_len;
      jpeg_in.buffer_size = 0;
    }
  } else {
    jpeg_in.buffer_size = 0;
    stream_total_len = len;
  }

  if(prev_ret != JPEGDEC_FRAME_READY && !stream_in_file)
    stream_in_file = stream_total_len;

  /* NOTE: The DWL should not be used outside decoder SW
   * here we call it because it is the easiest way to get
   * dynamically allocated linear memory
   * */

  /* allocate memory for stream buffer. if unsuccessful -> exit */
  if(DWLMallocLinear
      (((JpegDecContainer *) jpeg)->dwl, len, &stream_mem) != DWL_OK) {
    fprintf(stdout, "UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n");
    goto end;
  }

  fprintf(stdout, "\t-Input: Allocated buffer: virt: 0x%16lx bus: 0x%16lx\n",
          (addr_t)stream_mem.virtual_address, stream_mem.bus_address);

  /* memset input */
  (void) DWLmemset(stream_mem.virtual_address, 0, len);

  byte_strm_start = (u8 *) stream_mem.virtual_address;
  if(byte_strm_start == NULL) {
    fprintf(stdout, "UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n");
    goto end;
  }

  /* file i/o pointer to full */
  if(prev_ret == JPEGDEC_FRAME_READY)
    fseek(f_in, stream_seek_len, SEEK_SET);

  /* read input stream from file to buffer and close input file */
  ret = fread(byte_strm_start, sizeof(u8), len, f_in);

  fclose(f_in);

  /* initialize JpegDecDecode input structure */
  jpeg_in.stream_buffer.virtual_address = (u32 *) byte_strm_start;
  jpeg_in.stream_buffer.bus_address = stream_mem.bus_address;
  if(!pic_counter)
    jpeg_in.stream_length = stream_total_len;
  else
    jpeg_in.stream_length = stream_in_file;

  if(write_output)
    fprintf(stdout, "\t-File: Write output: YES: %d\n", write_output);
  else
    fprintf(stdout, "\t-File: Write output: NO: %d\n", write_output);
  fprintf(stdout, "\t-File: MbRows/slice: %d\n", jpeg_in.slice_mb_set);
  fprintf(stdout, "\t-File: Buffer size: %d\n", jpeg_in.buffer_size);
  fprintf(stdout, "\t-File: Stream size: %d\n", jpeg_in.stream_length);

  if(mem_allocation)
    fprintf(stdout, "\t-File: Output allocated by USER: %d\n",
            mem_allocation);
  else
    fprintf(stdout, "\t-File: Output allocated by DECODER: %d\n",
            mem_allocation);

  fprintf(stdout, "\nPhase 2: OPEN/READ FILE successful\n");

  /* jump here is frames still left */
decode:

  /******** PHASE 3 ********/
  fprintf(stdout, "\nPhase 3: GET IMAGE INFO\n");

  jpeg_ret = FindImageInfoEnd(byte_strm_start, len, &image_info_length);
  printf("\timage_info_length %d\n", image_info_length);
  /* If image info is not found, do not corrupt the header */
  if(jpeg_ret != 0) {
    if(stream_header_corrupt) {
      u32 ret = 0;

      ret =
        TBRandomizeBitSwapInStream(byte_strm_start, image_info_length,
                                   tb_cfg.tb_params.stream_bit_swap);
      if(ret != 0) {
        printf("RANDOM STREAM ERROR FAILED\n");
        goto end;
      }
    }
  }

  /* Get image information of the JFIF and decode JFIF header */
  START_SW_PERFORMANCE;
  decsw_performance();
  jpeg_ret = JpegDecGetImageInfo(jpeg, &jpeg_in, &image_info);
  END_SW_PERFORMANCE;
  decsw_performance();
  if(jpeg_ret != JPEGDEC_OK) {
    /* Handle here the error situation */
    PrintJpegRet(&jpeg_ret);
    if(JPEGDEC_INCREASE_INPUT_BUFFER == jpeg_ret) {
      DWLFreeLinear(((JpegDecContainer *) jpeg)->dwl, &stream_mem);
      jpeg_in.buffer_size += 256;
      goto reallocate_input_buffer;
    } else {
      /* printf JpegDecGetImageInfo() info */
      fprintf(stdout, "\n\t--------------------------------------\n");
      fprintf(stdout, "\tNote! IMAGE INFO WAS CHANGED!!!\n");
      fprintf(stdout, "\t--------------------------------------\n\n");
      PrintGetImageInfo(&image_info);
      fprintf(stdout, "\t--------------------------------------\n");

      /* check if MJPEG stream and Thumb decoding ==> continue to FULL */
      if(mode) {
        if( nbr_of_thumb_images &&
            image_info.output_width_thumb == prev_output_width_tn &&
            image_info.output_height_thumb == prev_output_height_tn &&
            image_info.output_format_thumb == prev_output_format_tn) {
          fprintf(stdout, "\n\t--------------------------------------\n");
          fprintf(stdout, "\tNote! THUMB INFO NOT CHANGED ==> DECODE!!!\n");
          fprintf(stdout, "\t--------------------------------------\n\n");
        } else {
          ThumbDone = 1;
          nbr_of_thumb_images = 0;
          pic_counter = 0;
          stream_seek_len = 0;
          stream_in_file = 0;
          goto end;
        }
      } else {
        /* if MJPEG and only THUMB changed ==> continue */
        if( image_info.output_width == prev_output_width &&
            image_info.output_height == prev_output_height &&
            image_info.output_format == prev_output_format) {
          fprintf(stdout, "\n\t--------------------------------------\n");
          fprintf(stdout, "\tNote! FULL IMAGE INFO NOT CHANGED ==> DECODE!!!\n");
          fprintf(stdout, "\t--------------------------------------\n\n");
        } else {
          nbr_of_images = 0;
          pic_counter = 0;
          stream_seek_len = 0;
          stream_in_file = 0;
          goto end;
        }
      }
    }
  }

  /* save for MJPEG check */
  /* full */
  prev_output_width = image_info.output_width;
  prev_output_height = image_info.output_height;
  prev_output_format = image_info.output_format;
  /* thumbnail */
  prev_output_width_tn = image_info.output_width_thumb;
  prev_output_height_tn = image_info.output_height_thumb;
  prev_output_format_tn = image_info.output_format_thumb;

  /* printf JpegDecGetImageInfo() info */
  PrintGetImageInfo(&image_info);

  /*  ******************** THUMBNAIL **************************** */
  /* Select if Thumbnail or full resolution image will be decoded */
  if(image_info.thumbnail_type == JPEGDEC_THUMBNAIL_JPEG) {
    /* if all thumbnails processed (MJPEG) */
    if(!ThumbDone)
      jpeg_in.dec_image_type = JPEGDEC_THUMBNAIL;
    else
      jpeg_in.dec_image_type = JPEGDEC_IMAGE;

    thumb_in_stream = 1;
  } else if(image_info.thumbnail_type == JPEGDEC_NO_THUMBNAIL)
    jpeg_in.dec_image_type = JPEGDEC_IMAGE;
  else if(image_info.thumbnail_type == JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT)
    jpeg_in.dec_image_type = JPEGDEC_IMAGE;

  /* check if forced to decode only full resolution images
      ==> discard thumbnail */
  if(only_full_resolution) {
    /* decode only full resolution image */
    fprintf(stdout,
            "\n\tNote! FORCED BY USER TO DECODE ONLY FULL RESOLUTION IMAGE\n");
    jpeg_in.dec_image_type = JPEGDEC_IMAGE;
  }

  fprintf(stdout, "PHASE 3: GET IMAGE INFO successful\n");

  /* TB SPECIFIC == LOOP IF THUMBNAIL IN JFIF */
  /* Decode JFIF */
  if(jpeg_in.dec_image_type == JPEGDEC_THUMBNAIL)
    mode = 1; /* TODO KIMA */
  else
    mode = 0;

#ifdef ASIC_TRACE_SUPPORT
  /* Handle incorrect slice size for HW testing */
  if(jpeg_in.slice_mb_set > (image_info.output_height >> 4)) {
    jpeg_in.slice_mb_set = (image_info.output_height >> 4);
    printf("FIXED Decoder Slice MB Set %d\n", jpeg_in.slice_mb_set);
  }
#endif

  /* no slice mode supported in progressive || non-interleaved ==> force to full mode */
  if((jpeg_in.dec_image_type == JPEGDEC_THUMBNAIL &&
      image_info.coding_mode_thumb == JPEGDEC_PROGRESSIVE) ||
      (jpeg_in.dec_image_type == JPEGDEC_IMAGE &&
       image_info.coding_mode == JPEGDEC_PROGRESSIVE))
    jpeg_in.slice_mb_set = 0;

  /******** PHASE 4 ********/
  /* Image mode to decode */
  if(mode)
    fprintf(stdout, "\nPhase 4: DECODE FRAME: THUMBNAIL\n");
  else
    fprintf(stdout, "\nPhase 4: DECODE FRAME: FULL RESOLUTION\n");

  /* if input (only full, not tn) > 4096 MCU      */
  /* ==> force to slice mode                                      */
  if(mode == 0) {
    /* calculate MCU's */
    if(image_info.output_format == JPEGDEC_YCbCr400 ||
        image_info.output_format == JPEGDEC_YCbCr444_SEMIPLANAR) {
      amount_of_mcus =
        ((image_info.output_width * image_info.output_height) / 64);
      mcu_in_row = (image_info.output_width / 8);
    } else if(image_info.output_format == JPEGDEC_YCbCr420_SEMIPLANAR) {
      /* 265 is the amount of luma samples in MB for 4:2:0 */
      amount_of_mcus =
        ((image_info.output_width * image_info.output_height) / 256);
      mcu_in_row = (image_info.output_width / 16);
    } else if(image_info.output_format == JPEGDEC_YCbCr422_SEMIPLANAR) {
      /* 128 is the amount of luma samples in MB for 4:2:2 */
      amount_of_mcus =
        ((image_info.output_width * image_info.output_height) / 128);
      mcu_in_row = (image_info.output_width / 16);
    } else if(image_info.output_format == JPEGDEC_YCbCr440) {
      /* 128 is the amount of luma samples in MB for 4:4:0 */
      amount_of_mcus =
        ((image_info.output_width * image_info.output_height) / 128);
      mcu_in_row = (image_info.output_width / 8);
    } else if(image_info.output_format == JPEGDEC_YCbCr411_SEMIPLANAR) {
      amount_of_mcus =
        ((image_info.output_width * image_info.output_height) / 256);
      mcu_in_row = (image_info.output_width / 32);
    }

    /* set mcu_size_divider for slice size count */
    if(image_info.output_format == JPEGDEC_YCbCr400 ||
        image_info.output_format == JPEGDEC_YCbCr440 ||
        image_info.output_format == JPEGDEC_YCbCr444_SEMIPLANAR)
      mcu_size_divider = 2;
    else
      mcu_size_divider = 1;

#ifdef ASIC_TRACE_SUPPORT
    if(is8170_hw) {
      /* over max MCU ==> force to slice mode */
      if((jpeg_in.slice_mb_set == 0) &&
          (amount_of_mcus > JPEGDEC_MAX_SLICE_SIZE)) {
        do {
          jpeg_in.slice_mb_set++;
        } while(((jpeg_in.slice_mb_set * (mcu_in_row / mcu_size_divider)) +
                 (mcu_in_row / mcu_size_divider)) <
                JPEGDEC_MAX_SLICE_SIZE);
        printf("Force to slice mode ==> Decoder Slice MB Set %d\n",
               jpeg_in.slice_mb_set);
      }
    } else {
      /* 8190 and over 16M ==> force to slice mode */
      if((jpeg_in.slice_mb_set == 0) &&
          ((image_info.output_width * image_info.output_height) >
           JPEGDEC_MAX_PIXEL_AMOUNT)) {
        do {
          jpeg_in.slice_mb_set++;
        } while(((jpeg_in.slice_mb_set * (mcu_in_row / mcu_size_divider)) +
                 (mcu_in_row / mcu_size_divider)) <
                JPEGDEC_MAX_SLICE_SIZE_8190);
        printf
        ("Force to slice mode (over 16M) ==> Decoder Slice MB Set %d\n",
         jpeg_in.slice_mb_set);
      }
    }
#else
    if(is8170_hw) {
      /* over max MCU ==> force to slice mode */
      if((jpeg_in.slice_mb_set == 0) &&
          (amount_of_mcus > JPEGDEC_MAX_SLICE_SIZE)) {
        do {
          jpeg_in.slice_mb_set++;
        } while(((jpeg_in.slice_mb_set * (mcu_in_row / mcu_size_divider)) +
                 (mcu_in_row / mcu_size_divider)) <
                JPEGDEC_MAX_SLICE_SIZE);
        printf("Force to slice mode ==> Decoder Slice MB Set %d\n",
               jpeg_in.slice_mb_set);
      }
    } else {
      /* 8190 and over 16M ==> force to slice mode */
      if((jpeg_in.slice_mb_set == 0) &&
          ((image_info.output_width * image_info.output_height) >
           JPEGDEC_MAX_PIXEL_AMOUNT)) {
        do {
          jpeg_in.slice_mb_set++;
        } while(((jpeg_in.slice_mb_set * (mcu_in_row / mcu_size_divider)) +
                 (mcu_in_row / mcu_size_divider)) <
                JPEGDEC_MAX_SLICE_SIZE_8190);
        printf
        ("Force to slice mode (over 16M) ==> Decoder Slice MB Set %d\n",
         jpeg_in.slice_mb_set);
      }
    }
#endif
  }

  /* if user allocated memory */
  if(mem_allocation) {
    fprintf(stdout, "\n\t-JPEG: USER ALLOCATED MEMORY\n");
    jpeg_ret = allocMemory(jpeg, &image_info, &jpeg_in);
    if(jpeg_ret != JPEGDEC_OK) {
      /* Handle here the error situation */
      PrintJpegRet(&jpeg_ret);
      goto end;
    }
    fprintf(stdout, "\t-JPEG: USER ALLOCATED MEMORY successful\n\n");
  }

  /*  Now corrupt only data beyong image info */
  if(stream_bit_swap) {
    jpeg_ret =
      TBRandomizeBitSwapInStream(byte_strm_start + image_info_length,
                                 len - image_info_length,
                                 tb_cfg.tb_params.stream_bit_swap);
    if(jpeg_ret != 0) {
      printf("RANDOM STREAM ERROR FAILED\n");
      goto end;
    }
  }

  /* decode */
  do {
    START_SW_PERFORMANCE;
    decsw_performance();
    jpeg_ret = JpegDecDecode(jpeg, &jpeg_in, &jpeg_out);
    END_SW_PERFORMANCE;
    decsw_performance();

    if(jpeg_ret == JPEGDEC_FRAME_READY) {
      fprintf(stdout, "\t-JPEG: JPEGDEC_FRAME_READY\n");

      /* check if progressive ==> planar output */
      if((image_info.coding_mode == JPEGDEC_PROGRESSIVE && mode == 0) ||
          (image_info.coding_mode_thumb == JPEGDEC_PROGRESSIVE &&
           mode == 1)) {
        progressive = 1;
      }

      if((image_info.coding_mode == JPEGDEC_NONINTERLEAVED && mode == 0)
          || (image_info.coding_mode_thumb == JPEGDEC_NONINTERLEAVED &&
              mode == 1))
        non_interleaved = 1;
      else
        non_interleaved = 0;

      if(jpeg_in.slice_mb_set && full_slice_counter == -1)
        sliced_output_used = 1;

      /* info to handleSlicedOutput */
      frame_ready = 1;
      if(!mode)
        nbr_of_images_to_out++;

      /* for input buffering */
      prev_ret = JPEGDEC_FRAME_READY;
    } else if(jpeg_ret == JPEGDEC_SCAN_PROCESSED) {
      /* TODO! Progressive scan ready... */
      fprintf(stdout, "\t-JPEG: JPEGDEC_SCAN_PROCESSED\n");

      /* progressive ==> planar output */
      if(image_info.coding_mode == JPEGDEC_PROGRESSIVE)
        progressive = 1;

      /* info to handleSlicedOutput */
      printf("SCAN %d READY\n", scan_counter);

      if(image_info.coding_mode == JPEGDEC_PROGRESSIVE) {
        /* calculate size for output */
        calcSize(&image_info, mode);

        printf("size_luma %d and size_chroma %d\n", size_luma,
               size_chroma);

        WriteProgressiveOutput(size_luma, size_chroma, mode,
                               (u8*)jpeg_out.output_picture_y.
                               virtual_address,
                               (u8*)jpeg_out.output_picture_cb_cr.
                               virtual_address,
                               (u8*)jpeg_out.output_picture_cr.
                               virtual_address);

        scan_counter++;
      }

      /* update/reset */
      progressive = 0;
      scan_ready = 0;

    } else if(jpeg_ret == JPEGDEC_SLICE_READY) {
      fprintf(stdout, "\t-JPEG: JPEGDEC_SLICE_READY\n");

      sliced_output_used = 1;

      /* calculate/write output of slice
       * and update output budder in case of
       * user allocated memory */
      if(jpeg_out.output_picture_y.virtual_address != NULL)
        handleSlicedOutput(&image_info, &jpeg_in, &jpeg_out);

      scan_counter++;
    } else if(jpeg_ret == JPEGDEC_STRM_PROCESSED) {
      fprintf(stdout,
              "\t-JPEG: JPEGDEC_STRM_PROCESSED ==> Load input buffer\n");

      /* update seek value */
      stream_in_file -= len;
      stream_seek_len += len;

      if(stream_in_file < 0) {
        fprintf(stdout, "\t\t==> Unable to load input buffer\n");
        fprintf(stdout,
                "\t\t\t==> TRUNCATED INPUT ==> JPEGDEC_STRM_ERROR\n");
        jpeg_ret = JPEGDEC_STRM_ERROR;
        goto strm_error;
      }

      if(stream_in_file < len) {
        len = stream_in_file;
      }

      /* update the buffer size in case last buffer
         doesn't have the same amount of data as defined */
      if(len < jpeg_in.buffer_size) {
        jpeg_in.buffer_size = len;
      }

#ifndef PP_PIPELINE_ENABLED
      /* Reading input file */
      f_in = fopen(argv[argc - 1], "rb");
      if(f_in == NULL) {
        fprintf(stdout, "Unable to open input file\n");
        exit(-1);
      }
#else
      /* Reading input file */
      f_in = fopen(argv[argc - 2], "rb");
      if(f_in == NULL) {
        fprintf(stdout, "Unable to open input file\n");
        exit(-1);
      }
#endif

      /* file i/o pointer to full */
      fseek(f_in, stream_seek_len, SEEK_SET);
      /* read input stream from file to buffer and close input file */
      ret = fread(byte_strm_start, sizeof(u8), len, f_in);
      fclose(f_in);

      /* update */
      jpeg_in.stream_buffer.virtual_address = (u32 *) byte_strm_start;
      jpeg_in.stream_buffer.bus_address = stream_mem.bus_address;

      if(stream_bit_swap) {
        jpeg_ret =
          TBRandomizeBitSwapInStream(byte_strm_start, len,
                                     tb_cfg.tb_params.
                                     stream_bit_swap);
        if(jpeg_ret != 0) {
          printf("RANDOM STREAM ERROR FAILED\n");
          goto end;
        }
      }
    } else if(jpeg_ret == JPEGDEC_STRM_ERROR) {
strm_error:

      if(jpeg_in.slice_mb_set && full_slice_counter == -1)
        sliced_output_used = 1;

      /* calculate/write output of slice
       * and update output budder in case of
       * user allocated memory */
      if(sliced_output_used &&
          jpeg_out.output_picture_y.virtual_address != NULL)
        handleSlicedOutput(&image_info, &jpeg_in, &jpeg_out);

      /* info to handleSlicedOutput */
      frame_ready = 1;
      sliced_output_used = 0;

      /* Handle here the error situation */
      PrintJpegRet(&jpeg_ret);
      if(mode == 1)
        break;
      else
        goto error;
    } else {
      /* Handle here the error situation */
      PrintJpegRet(&jpeg_ret);
      goto end;
    }
  } while(jpeg_ret != JPEGDEC_FRAME_READY);

error:

  /* calculate/write output of slice */
  if(sliced_output_used && jpeg_out.output_picture_y.virtual_address != NULL) {
    handleSlicedOutput(&image_info, &jpeg_in, &jpeg_out);
    sliced_output_used = 0;
  }

  if(jpeg_out.output_picture_y.virtual_address != NULL) {
    /* calculate size for output */
    calcSize(&image_info, mode);

    /* Thumbnail || full resolution */
    if(!mode)
      fprintf(stdout, "\n\t-JPEG: ++++++++++ FULL RESOLUTION ++++++++++\n");
    else
      fprintf(stdout, "\t-JPEG: ++++++++++ THUMBNAIL ++++++++++\n");
    fprintf(stdout, "\t-JPEG: Instance %p\n", (JpegDecContainer *) jpeg);
    fprintf(stdout, "\t-JPEG: Luma output: 0x%p size: %d\n",
            jpeg_out.output_picture_y.virtual_address, size_luma);
    fprintf(stdout, "\t-JPEG: Chroma output: 0x%p size: %d\n",
            jpeg_out.output_picture_cb_cr.virtual_address, size_chroma);
    fprintf(stdout, "\t-JPEG: Luma output bus: 0x%p\n",
            (u8 *) jpeg_out.output_picture_y.bus_address);
    fprintf(stdout, "\t-JPEG: Chroma output bus: 0x%p\n",
            (u8 *) jpeg_out.output_picture_cb_cr.bus_address);
  }

  fprintf(stdout, "PHASE 4: DECODE FRAME successful\n");

  /* if output write not disabled by TB */
  if(write_output) {
    /******** PHASE 5 ********/
    fprintf(stdout, "\nPhase 5: WRITE OUTPUT\n");

#ifndef PP_PIPELINE_ENABLED
    if(image_info.output_format) {
      switch (image_info.output_format) {
      case JPEGDEC_YCbCr400:
        fprintf(stdout, "\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr400\n");
        break;
      case JPEGDEC_YCbCr420_SEMIPLANAR:
        fprintf(stdout, "\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr420_SEMIPLANAR\n");
        break;
      case JPEGDEC_YCbCr422_SEMIPLANAR:
        fprintf(stdout, "\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr422_SEMIPLANAR\n");
        break;
      case JPEGDEC_YCbCr440:
        fprintf(stdout, "\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr440\n");
        break;
      case JPEGDEC_YCbCr411_SEMIPLANAR:
        fprintf(stdout, "\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr411_SEMIPLANAR\n");
        break;
      case JPEGDEC_YCbCr444_SEMIPLANAR:
        fprintf(stdout, "\t-JPEG: DECODER OUTPUT: JPEGDEC_YCbCr444_SEMIPLANAR\n");
        break;
      }
    }

    if(image_info.coding_mode == JPEGDEC_PROGRESSIVE)
      progressive = 1;

    /* write output */
    if(jpeg_in.slice_mb_set) {
      if(image_info.output_format != JPEGDEC_YCbCr400)
        WriteFullOutput(mode);
    } else {
      if(image_info.coding_mode != JPEGDEC_PROGRESSIVE) {
        WriteOutput(((u8 *) jpeg_out.output_picture_y.virtual_address),
                    size_luma,
                    ((u8 *) jpeg_out.output_picture_cb_cr.
                     virtual_address), size_chroma, mode);
      } else {
        /* calculate size for output */
        calcSize(&image_info, mode);

        printf("size_luma %d and size_chroma %d\n", size_luma, size_chroma);

        WriteProgressiveOutput(size_luma, size_chroma, mode,
                               (u8*)jpeg_out.output_picture_y.virtual_address,
                               (u8*)jpeg_out.output_picture_cb_cr.
                               virtual_address,
                               (u8*)jpeg_out.output_picture_cr.virtual_address);
      }

    }

    if(crop)
      WriteCroppedOutput(&image_info,
                         (u8*)jpeg_out.output_picture_y.virtual_address,
                         (u8*)jpeg_out.output_picture_cb_cr.virtual_address,
                         (u8*)jpeg_out.output_picture_cr.virtual_address);

    progressive = 0;
#else
    /* PP test bench will do the operations only if enabled */
    /*pp_set_rotation(); */

    fprintf(stdout, "\t-JPEG: PP_OUTPUT_WRITE\n");
    pp_write_output(0, 0, 0);
    pp_check_combined_status();
#endif
    fprintf(stdout, "PHASE 5: WRITE OUTPUT successful\n");
  } else {
    fprintf(stdout, "\nPhase 5: WRITE OUTPUT DISABLED\n");
  }

  /* more images to decode? */
  if(nbr_of_images || (nbr_of_thumb_images && !only_full_resolution)) {
    pic_counter++;

    if(mode) {
      nbr_of_thumb_images--;
      nbr_of_thumb_images_to_out++;
      if(nbr_of_thumb_images == 0) {
        /* set */
        ThumbDone = 1;
        pic_counter = 0;
        stream_seek_len = 0;
        stream_in_file = 0;
        goto end;
      }
    } else {
      nbr_of_images--;
      if(nbr_of_images == 0) {
        /* set */
        pic_counter = 0;
        stream_seek_len = 0;
        stream_in_file = 0;
        goto end;
      }
    }

    /* if input buffered load */
    if(jpeg_in.buffer_size) {
      u32 counter = 0;
      /* seek until next pic start */
      do {
        /* Seek next EOI */
        jpeg_ret = FindImageTnEOI(byte_strm_start, len, &image_info_length, mode, thumb_in_stream);

        /* check result */
        if(jpeg_ret == JPEGDEC_OK && next_soi)
          break;
        else {
          jpeg_ret = -1;
          counter++;
        }

        /* update seek value */
        stream_in_file -= len;
        stream_seek_len += len;

        if(stream_in_file <= 0) {
          fprintf(stdout, "\t\t==> Unable to load input buffer\n");
          fprintf(stdout,
                  "\t\t\t==> TRUNCATED INPUT ==> JPEGDEC_STRM_ERROR\n");
          jpeg_ret = JPEGDEC_STRM_ERROR;
          goto end;
        }

        if(stream_in_file < len)
          len = stream_in_file;

#ifndef PP_PIPELINE_ENABLED
        /* Reading input file */
        f_in = fopen(argv[argc - 1], "rb");
        if(f_in == NULL) {
          fprintf(stdout, "Unable to open input file\n");
          exit(-1);
        }
#else
        /* Reading input file */
        f_in = fopen(argv[argc - 2], "rb");
        if(f_in == NULL) {
          fprintf(stdout, "Unable to open input file\n");
          exit(-1);
        }
#endif

        /* file i/o pointer to full */
        fseek(f_in, stream_seek_len, SEEK_SET);
        /* read input stream from file to buffer and close input file */
        ret = fread(byte_strm_start, sizeof(u8), len, f_in);
        fclose(f_in);
      } while(jpeg_ret != 0);
    } else {
      /* Find next image */
      jpeg_ret = FindImageTnEOI(byte_strm_start, len, &image_info_length, mode, thumb_in_stream);
    }
    /* If image info is not found */
    if(jpeg_ret != 0) {
      printf("NO MORE IMAGES!\n");
      goto end;
    }

    /* update seek value */
    stream_in_file -= image_info_length;
    stream_seek_len += image_info_length;

    if(stream_in_file <= 0) {
      fprintf(stdout, "\t\t==> Unable to load input buffer\n");
      fprintf(stdout,
              "\t\t\t==> TRUNCATED INPUT ==> JPEGDEC_STRM_ERROR\n");
      jpeg_ret = JPEGDEC_STRM_ERROR;
      goto strm_error;
    }

    if(stream_in_file < len) {
      len = stream_in_file;
    }

    /* update the buffer size in case last buffer
       doesn't have the same amount of data as defined */
    if(len < jpeg_in.buffer_size) {
      jpeg_in.buffer_size = len;
    }

#ifndef PP_PIPELINE_ENABLED
    /* Reading input file */
    f_in = fopen(argv[argc - 1], "rb");
    if(f_in == NULL) {
      fprintf(stdout, "Unable to open input file\n");
      exit(-1);
    }
#else
    /* Reading input file */
    f_in = fopen(argv[argc - 2], "rb");
    if(f_in == NULL) {
      fprintf(stdout, "Unable to open input file\n");
      exit(-1);
    }
#endif

    /* file i/o pointer to full */
    fseek(f_in, stream_seek_len, SEEK_SET);
    /* read input stream from file to buffer and close input file */
    ret = fread(byte_strm_start, sizeof(u8), len, f_in);
    fclose(f_in);

    /* update */
    jpeg_in.stream_buffer.virtual_address = (u32 *) byte_strm_start;
    jpeg_in.stream_buffer.bus_address = stream_mem.bus_address;
    jpeg_in.stream_length = stream_in_file;

    /* loop back to start */
    goto decode;
  }

end:

  /******** PHASE 6 ********/
  fprintf(stdout, "\nPhase 6: RELEASE JPEG DECODER\n");

  /* reset output write option */
  progressive = 0;

#ifdef PP_PIPELINE_ENABLED
  pp_close();
#endif

  if(stream_mem.virtual_address != NULL)
    DWLFreeLinear(((JpegDecContainer *) jpeg)->dwl, &stream_mem);

  if(user_alloc_luma.virtual_address != NULL)
    DWLFreeRefFrm(((JpegDecContainer *) jpeg)->dwl, &user_alloc_luma);

  if(user_alloc_chroma.virtual_address != NULL)
    DWLFreeRefFrm(((JpegDecContainer *) jpeg)->dwl, &user_alloc_chroma);

  if(user_alloc_cr.virtual_address != NULL)
    DWLFreeRefFrm(((JpegDecContainer *) jpeg)->dwl, &user_alloc_cr);

  /* release decoder instance */
  START_SW_PERFORMANCE;
  decsw_performance();
  JpegDecRelease(jpeg);
  END_SW_PERFORMANCE;
  decsw_performance();

  fprintf(stdout, "PHASE 6: RELEASE JPEG DECODER successful\n\n");

  /* check if (thumbnail + full) ==> decode all full images */
  if(ThumbDone && nbr_of_images) {
    prev_ret = JPEGDEC_STRM_ERROR;
    goto start_full_decode;
  }

  if(input_read_type) {
    if(f_in) {
      fclose(f_in);
    }
  }

  if(fout) {
    fclose(fout);
    if(f_stream_trace)
      fclose(f_stream_trace);
  }

#ifdef ASIC_TRACE_SUPPORT
  trace_SequenceCtrl(pic_number + 1, b_frames);
  trace_JpegDecodingTools();
  closeTraceFiles();
#endif

  /* Leave properly */
  JpegDecFree(p_image);

  FINALIZE_SW_PERFORMANCE;

  /* amounf of decoded frames */
  if(nbr_of_images_to_out) {
    fprintf(stdout, "Information of decoded pictures:\n");
    fprintf(stdout, "\tPictures decoded: \t%d of total %d\n", nbr_of_images_to_out, nbr_of_images_total);
    if(nbr_of_thumb_images_to_out)
      fprintf(stdout, "\tThumbnails decoded: \t%d of total %d\n", nbr_of_thumb_images_to_out, nbr_of_thumb_images_total);

    if( (nbr_of_images_to_out != nbr_of_images_total) ||
        (nbr_of_thumb_images_to_out != nbr_of_thumb_images_total)) {
      fprintf(stdout, "\n\t-NOTE! \tCheck decoding log for the reason of \n");
      fprintf(stdout, "\t\tnot decoded, failed or unsupported pictures!\n");
      /* only full resolution */
      if(only_full_resolution)
        fprintf(stdout,"\n\t-NOTE! Forced by user to decode only full resolution image!\n");
    }

    fprintf(stdout, "\n");
  }

  fprintf(stdout, "TB: ...released\n");

  return 0;
}

/*------------------------------------------------------------------------------

Function name:  WriteOutput

Purpose:
    Write picture pointed by data to file. Size of the
    picture in pixels is indicated by picSize.

------------------------------------------------------------------------------*/
void
WriteOutput(u8 * data_luma, u32 pic_size_luma, u8 * data_chroma,
            u32 pic_size_chroma, u32 pic_mode) {
  u32 i;
  FILE *foutput = NULL;
  u8 *p_yuv_out = NULL;
  u8 file[256];

  if(!slice_to_user) {
    /* foutput is global file pointer */
    if(foutput == NULL) {
      if(pic_mode == 0)
        if(pic_counter == 0)
          foutput = fopen("out.yuv", "wb");
        else
          foutput = fopen("out.yuv", "ab");
      else if(pic_counter == 0)
        foutput = fopen("out_tn.yuv", "wb");
      else
        foutput = fopen("out_tn.yuv", "ab");

      if(foutput == NULL) {
        fprintf(stdout, "UNABLE TO OPEN OUTPUT FILE\n");
        return;
      }
    }
  } else {
    /* foutput is global file pointer */
    if(foutput == NULL) {
      if(pic_mode == 0) {
        sprintf(file, "out_%d.yuv", full_slice_counter);
        foutput = fopen(file, "wb");
      } else {
        sprintf(file, "tn_%d.yuv", full_slice_counter);
        foutput = fopen(file, "wb");
      }

      if(foutput == NULL) {
        fprintf(stdout, "UNABLE TO OPEN OUTPUT FILE\n");
        return;
      }
    }
  }

  if(foutput && data_luma) {
    if(1) {
      fprintf(stdout, "\t-JPEG: Luminance\n");
      /* write decoder output to file */
      p_yuv_out = data_luma;
      for(i = 0; i < (pic_size_luma >> 2); i++) {
#ifndef ASIC_TRACE_SUPPORT
        if(DEC_X170_BIG_ENDIAN == output_picture_endian) {
          fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1, foutput);
        } else {
#endif
          fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1, foutput);
#ifndef ASIC_TRACE_SUPPORT
        }
#endif
      }
    }
  }

  if(!non_interleaved) {
    /* progressive ==> planar */
    if(!progressive) {
      if(foutput && data_chroma) {
        fprintf(stdout, "\t-JPEG: Chrominance\n");
        /* write decoder output to file */
        p_yuv_out = data_chroma;
        if(!planar_output) {
          for(i = 0; i < (pic_size_chroma >> 2); i++) {
#ifndef ASIC_TRACE_SUPPORT
            if(DEC_X170_BIG_ENDIAN == output_picture_endian) {
              fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1,
                     foutput);
            } else {
#endif
              fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1,
                     foutput);
#ifndef ASIC_TRACE_SUPPORT
            }
#endif
          }
        } else {
          printf("BASELINE PLANAR\n");
          for(i = 0; i < pic_size_chroma / 2; i++)
            fwrite(p_yuv_out + 2 * i, sizeof(u8), 1, foutput);
          for(i = 0; i < pic_size_chroma / 2; i++)
            fwrite(p_yuv_out + 2 * i + 1, sizeof(u8), 1, foutput);
        }
      }
    } else {
      if(foutput && data_chroma) {
        fprintf(stdout, "\t-JPEG: Chrominance\n");
        /* write decoder output to file */
        p_yuv_out = data_chroma;
        if(!planar_output) {
          for(i = 0; i < (pic_size_chroma >> 2); i++) {
#ifndef ASIC_TRACE_SUPPORT
            if(DEC_X170_BIG_ENDIAN == output_picture_endian) {
              fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1,
                     foutput);
            } else {
#endif
              fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1,
                     foutput);
              fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1,
                     foutput);
#ifndef ASIC_TRACE_SUPPORT
            }
#endif
          }
        } else {
          printf("PROGRESSIVE PLANAR OUTPUT\n");
          for(i = 0; i < pic_size_chroma; i++)
            fwrite(p_yuv_out + (1 * i), sizeof(u8), 1, foutput);
        }
      }
    }
  } else {
    if(foutput && data_chroma) {
      fprintf(stdout, "\t-JPEG: Chrominance\n");
      /* write decoder output to file */
      p_yuv_out = data_chroma;

      printf("NONINTERLEAVED: PLANAR OUTPUT\n");
      for(i = 0; i < pic_size_chroma; i++)
        fwrite(p_yuv_out + (1 * i), sizeof(u8), 1, foutput);
    }
  }
  fclose(foutput);
}

/*------------------------------------------------------------------------------

    Function name:  WriteOutputLuma

    Purpose:
        Write picture pointed by data to file. Size of the
        picture in pixels is indicated by picSize.

------------------------------------------------------------------------------*/
void WriteOutputLuma(u8 * data_luma, u32 pic_size_luma, u32 pic_mode) {
  u32 i;
  FILE *foutput = NULL;
  u8 *p_yuv_out = NULL;

  /* foutput is global file pointer */
  if(foutput == NULL) {
    if(pic_mode == 0) {
      foutput = fopen("out.yuv", "ab");
    } else {
      foutput = fopen("out_tn.yuv", "ab");
    }

    if(foutput == NULL) {
      fprintf(stdout, "UNABLE TO OPEN OUTPUT FILE\n");
      return;
    }
  }

  if(foutput && data_luma) {
    if(1) {
      fprintf(stdout, "\t-JPEG: Luminance\n");
      /* write decoder output to file */
      p_yuv_out = data_luma;
      for(i = 0; i < (pic_size_luma >> 2); i++) {
#ifndef ASIC_TRACE_SUPPORT
        if(DEC_X170_BIG_ENDIAN == output_picture_endian) {
          fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1, foutput);
        } else {
#endif
          fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1, foutput);
          fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1, foutput);
#ifndef ASIC_TRACE_SUPPORT
        }
#endif
      }
    }
  }

  fclose(foutput);
}

/*------------------------------------------------------------------------------

    Function name:  WriteOutput

    Purpose:
        Write picture pointed by data to file. Size of the
        picture in pixels is indicated by picSize.

------------------------------------------------------------------------------*/
void WriteOutputChroma(u8 * data_chroma, u32 pic_size_chroma, u32 pic_mode) {
  u32 i;
  FILE *foutput_chroma = NULL;
  u8 *p_yuv_out = NULL;

  /* file pointer */
  if(foutput_chroma == NULL) {
    if(pic_mode == 0) {
      if(!progressive) {
        if(full_slice_counter == 0)
          foutput_chroma = fopen("out_chroma.yuv", "wb");
        else
          foutput_chroma = fopen("out_chroma.yuv", "ab");
      } else {
        if(!sliced_output_used) {
          foutput_chroma = fopen("out_chroma.yuv", "wb");
        } else {
          if(scan_counter == 0 || full_slice_counter == 0)
            foutput_chroma = fopen("out_chroma.yuv", "wb");
          else
            foutput_chroma = fopen("out_chroma.yuv", "ab");
        }
      }
    } else {
      if(!progressive) {
        if(full_slice_counter == 0)
          foutput_chroma = fopen("out_chroma_tn.yuv", "wb");
        else
          foutput_chroma = fopen("out_chroma_tn.yuv", "ab");
      } else {
        if(!sliced_output_used) {
          foutput_chroma = fopen("out_chroma_tn.yuv", "wb");
        } else {
          if(scan_counter == 0 || full_slice_counter == 0)
            foutput_chroma = fopen("out_chroma_tn.yuv", "wb");
          else
            foutput_chroma = fopen("out_chroma_tn.yuv", "ab");
        }
      }
    }

    if(foutput_chroma == NULL) {
      fprintf(stdout, "UNABLE TO OPEN OUTPUT FILE\n");
      return;
    }
  }

  if(foutput_chroma && data_chroma) {
    fprintf(stdout, "\t-JPEG: Chrominance\n");
    /* write decoder output to file */
    p_yuv_out = data_chroma;

    if(!progressive) {
      for(i = 0; i < (pic_size_chroma >> 2); i++) {
#ifndef ASIC_TRACE_SUPPORT
        if(DEC_X170_BIG_ENDIAN == output_picture_endian) {
          fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1, foutput_chroma);
          fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1, foutput_chroma);
          fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1, foutput_chroma);
          fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1, foutput_chroma);
        } else {
#endif
          fwrite(p_yuv_out + (4 * i) + 0, sizeof(u8), 1, foutput_chroma);
          fwrite(p_yuv_out + (4 * i) + 1, sizeof(u8), 1, foutput_chroma);
          fwrite(p_yuv_out + (4 * i) + 2, sizeof(u8), 1, foutput_chroma);
          fwrite(p_yuv_out + (4 * i) + 3, sizeof(u8), 1, foutput_chroma);
#ifndef ASIC_TRACE_SUPPORT
        }
#endif
      }
    } else {
      printf("PROGRESSIVE PLANAR OUTPUT CHROMA\n");
      for(i = 0; i < pic_size_chroma; i++)
        fwrite(p_yuv_out + (1 * i), sizeof(u8), 1, foutput_chroma);
    }
  }
  fclose(foutput_chroma);
}

/*------------------------------------------------------------------------------

    Function name:  WriteFullOutput

    Purpose:
        Write picture pointed by data to file.

------------------------------------------------------------------------------*/
void WriteFullOutput(u32 pic_mode) {
  u32 i;
  FILE *foutput = NULL;
  u8 *p_yuv_out_chroma = NULL;
  FILE *f_input_chroma = NULL;
  u32 length = 0;
  u32 chroma_len = 0;
  int ret;

  fprintf(stdout, "\t-JPEG: WriteFullOutput\n");

  /* if semi-planar output */
  if(!planar_output) {
    /* Reading chroma file */
    if(pic_mode == 0)
      ret = system("cat out_chroma.yuv >> out.yuv");
    else
      ret = system("cat out_chroma_tn.yuv >> out_tn.yuv");
  } else {
    /* Reading chroma file */
    if(pic_mode == 0)
      f_input_chroma = fopen("out_chroma.yuv", "rb");
    else
      f_input_chroma = fopen("out_chroma_tn.yuv", "rb");

    if(f_input_chroma == NULL) {
      fprintf(stdout, "Unable to open chroma output tmp file\n");
      exit(-1);
    }

    /* file i/o pointer to full */
    fseek(f_input_chroma, 0L, SEEK_END);
    length = ftell(f_input_chroma);
    rewind(f_input_chroma);

    /* check length */
    chroma_len = length;

    p_yuv_out_chroma = JpegDecMalloc(sizeof(u8) * (chroma_len));

    /* read output stream from file to buffer and close input file */
    ret = fread(p_yuv_out_chroma, sizeof(u8), chroma_len, f_input_chroma);

    fclose(f_input_chroma);

    /* foutput is global file pointer */
    if(pic_mode == 0)
      foutput = fopen("out.yuv", "ab");
    else
      foutput = fopen("out_tn.yuv", "ab");

    if(foutput == NULL) {
      fprintf(stdout, "UNABLE TO OPEN OUTPUT FILE\n");
      return;
    }

    if(foutput && p_yuv_out_chroma) {
      fprintf(stdout, "\t-JPEG: Chrominance\n");
      if(!progressive) {
        if(!planar_output) {
          /* write decoder output to file */
          for(i = 0; i < (chroma_len >> 2); i++) {
            fwrite(p_yuv_out_chroma + (4 * i) + 0, sizeof(u8), 1, foutput);
            fwrite(p_yuv_out_chroma + (4 * i) + 1, sizeof(u8), 1, foutput);
            fwrite(p_yuv_out_chroma + (4 * i) + 2, sizeof(u8), 1, foutput);
            fwrite(p_yuv_out_chroma + (4 * i) + 3, sizeof(u8), 1, foutput);
          }
        } else {
          for(i = 0; i < chroma_len / 2; i++)
            fwrite(p_yuv_out_chroma + 2 * i, sizeof(u8), 1, foutput);
          for(i = 0; i < chroma_len / 2; i++)
            fwrite(p_yuv_out_chroma + 2 * i + 1, sizeof(u8), 1, foutput);
        }
      } else {
        if(!planar_output) {
          /* write decoder output to file */
          for(i = 0; i < (chroma_len >> 2); i++) {
            fwrite(p_yuv_out_chroma + (4 * i) + 0, sizeof(u8), 1, foutput);
            fwrite(p_yuv_out_chroma + (4 * i) + 1, sizeof(u8), 1, foutput);
            fwrite(p_yuv_out_chroma + (4 * i) + 2, sizeof(u8), 1, foutput);
            fwrite(p_yuv_out_chroma + (4 * i) + 3, sizeof(u8), 1, foutput);
          }
        } else {
          printf("PROGRESSIVE FULL CHROMA %d\n", chroma_len);
          for(i = 0; i < chroma_len; i++)
            fwrite(p_yuv_out_chroma + i, sizeof(u8), 1, foutput);
        }
      }
    }
    fclose(foutput);

    /* Leave properly */
    JpegDecFree(p_yuv_out_chroma);
  }
}

/*------------------------------------------------------------------------------

    Function name:  handleSlicedOutput

    Purpose:
        Calculates size for slice and writes sliced output

------------------------------------------------------------------------------*/
void
handleSlicedOutput(JpegDecImageInfo * image_info,
                   JpegDecInput * jpeg_in, JpegDecOutput * jpeg_out) {
  /* for output name */
  full_slice_counter++;

  /******** PHASE X ********/
  if(jpeg_in->slice_mb_set)
    fprintf(stdout, "\nPhase SLICE: HANDLE SLICE %d\n", full_slice_counter);

  /* save start pointers for whole output */
  if(full_slice_counter == 0) {
    /* virtual address */
    output_address_y.virtual_address =
      jpeg_out->output_picture_y.virtual_address;
    output_address_cb_cr.virtual_address =
      jpeg_out->output_picture_cb_cr.virtual_address;

    /* bus address */
    output_address_y.bus_address = jpeg_out->output_picture_y.bus_address;
    output_address_cb_cr.bus_address = jpeg_out->output_picture_cb_cr.bus_address;
  }

  /* if output write not disabled by TB */
  if(write_output) {
    /******** PHASE 5 ********/
    fprintf(stdout, "\nPhase 5: WRITE OUTPUT\n");

    if(image_info->output_format) {
      if(!frame_ready) {
        slice_size = jpeg_in->slice_mb_set * 16;
      } else {
        if(mode == 0)
          slice_size =
            (image_info->output_height -
             ((full_slice_counter) * (slice_size)));
        else
          slice_size =
            (image_info->output_height_thumb -
             ((full_slice_counter) * (slice_size)));
      }
    }

    /* slice interrupt from decoder */
    slice_to_user = 1;

    /* calculate size for output */
    calcSize(image_info, mode);

    /* test printf */
    fprintf(stdout, "\t-JPEG: ++++++++++ SLICE INFORMATION ++++++++++\n");
    fprintf(stdout, "\t-JPEG: Luma output: 0x%p size: %d\n",
            jpeg_out->output_picture_y.virtual_address, size_luma);
    fprintf(stdout, "\t-JPEG: Chroma output: 0x%p size: %d\n",
            jpeg_out->output_picture_cb_cr.virtual_address, size_chroma);
    fprintf(stdout, "\t-JPEG: Luma output bus: 0x%p\n",
            (u8 *) jpeg_out->output_picture_y.bus_address);
    fprintf(stdout, "\t-JPEG: Chroma output bus: 0x%p\n",
            (u8 *) jpeg_out->output_picture_cb_cr.bus_address);

    /* write slice output */
    WriteOutput(((u8 *) jpeg_out->output_picture_y.virtual_address),
                size_luma,
                ((u8 *) jpeg_out->output_picture_cb_cr.virtual_address),
                size_chroma, mode);

    /* write luma to final output file */
    WriteOutputLuma(((u8 *) jpeg_out->output_picture_y.virtual_address),
                    size_luma, mode);

    if(image_info->output_format != JPEGDEC_YCbCr400) {
      /* write chroam to tmp file */
      WriteOutputChroma(((u8 *) jpeg_out->output_picture_cb_cr.
                         virtual_address), size_chroma, mode);
    }

    fprintf(stdout, "PHASE 5: WRITE OUTPUT successful\n");
  } else {
    fprintf(stdout, "\nPhase 5: WRITE OUTPUT DISABLED\n");
  }

  if(frame_ready) {
    /* give start pointers for whole output write */

    /* virtual address */
    jpeg_out->output_picture_y.virtual_address =
      output_address_y.virtual_address;
    jpeg_out->output_picture_cb_cr.virtual_address =
      output_address_cb_cr.virtual_address;

    /* bus address */
    jpeg_out->output_picture_y.bus_address = output_address_y.bus_address;
    jpeg_out->output_picture_cb_cr.bus_address = output_address_cb_cr.bus_address;
  }

  if(frame_ready) {
    frame_ready = 0;
    slice_to_user = 0;

    /******** PHASE X ********/
    if(jpeg_in->slice_mb_set)
      fprintf(stdout, "\nPhase SLICE: HANDLE SLICE %d successful\n",
              full_slice_counter);

    full_slice_counter = -1;
  } else {
    /******** PHASE X ********/
    if(jpeg_in->slice_mb_set)
      fprintf(stdout, "\nPhase SLICE: HANDLE SLICE %d successful\n",
              full_slice_counter);
  }

}

/*------------------------------------------------------------------------------

    Function name:  calcSize

    Purpose:
        Calculate size

------------------------------------------------------------------------------*/
void calcSize(JpegDecImageInfo * image_info, u32 pic_mode) {

  u32 format;

  size_luma = 0;
  size_chroma = 0;

  format = pic_mode == 0 ?
           image_info->output_format : image_info->output_format_thumb;

  /* if slice interrupt not given to user */
  if(!slice_to_user || scan_ready) {
    if(pic_mode == 0) {  /* full */
      size_luma = (image_info->output_width * image_info->output_height);
    } else { /* thumbnail */
      size_luma =
        (image_info->output_width_thumb * image_info->output_height_thumb);
    }
  } else {
    if(pic_mode == 0) {  /* full */
      size_luma = (image_info->output_width * slice_size);
    } else { /* thumbnail */
      size_luma = (image_info->output_width_thumb * slice_size);
    }
  }

  if(format != JPEGDEC_YCbCr400) {
    if(format == JPEGDEC_YCbCr420_SEMIPLANAR ||
        format == JPEGDEC_YCbCr411_SEMIPLANAR) {
      size_chroma = (size_luma / 2);
    } else if(format == JPEGDEC_YCbCr444_SEMIPLANAR) {
      size_chroma = size_luma * 2;
    } else {
      size_chroma = size_luma;
    }
  }
}

/*------------------------------------------------------------------------------

    Function name:  allocMemory

    Purpose:
        Allocates user specific memory for output.

------------------------------------------------------------------------------*/
u32
allocMemory(JpegDecInst dec_inst, JpegDecImageInfo * image_info,
            JpegDecInput * jpeg_in) {
  u32 separate_chroma = 0;
  u32 rotation = 0;

  out_pic_size_luma = 0;
  out_pic_size_chroma = 0;
  jpeg_in->picture_buffer_y.virtual_address = NULL;
  jpeg_in->picture_buffer_y.bus_address = 0;
  jpeg_in->picture_buffer_cb_cr.virtual_address = NULL;
  jpeg_in->picture_buffer_cb_cr.bus_address = 0;
  jpeg_in->picture_buffer_cr.virtual_address = NULL;
  jpeg_in->picture_buffer_cr.bus_address = 0;

#ifdef PP_PIPELINE_ENABLED
  /* check if rotation used */
  rotation = pp_rotation_used();

  if(rotation)
    fprintf(stdout,
            "\t-JPEG: IN CASE ROTATION ==> USER NEEDS TO ALLOCATE FULL OUTPUT MEMORY\n");
#endif

  /* calculate sizes */
  if(jpeg_in->dec_image_type == 0) {
    /* luma size */
    if(jpeg_in->slice_mb_set && !rotation)
      out_pic_size_luma =
        (image_info->output_width * (jpeg_in->slice_mb_set * 16));
    else
      out_pic_size_luma =
        (image_info->output_width * image_info->output_height);

    /* chroma size ==> semiplanar output */
    if(image_info->output_format == JPEGDEC_YCbCr420_SEMIPLANAR ||
        image_info->output_format == JPEGDEC_YCbCr411_SEMIPLANAR)
      out_pic_size_chroma = out_pic_size_luma / 2;
    else if(image_info->output_format == JPEGDEC_YCbCr422_SEMIPLANAR ||
            image_info->output_format == JPEGDEC_YCbCr440)
      out_pic_size_chroma = out_pic_size_luma;
    else if(image_info->output_format == JPEGDEC_YCbCr444_SEMIPLANAR)
      out_pic_size_chroma = out_pic_size_luma * 2;

    if(image_info->coding_mode != JPEGDEC_BASELINE)
      separate_chroma = 1;
  } else {
    /* luma size */
    if(jpeg_in->slice_mb_set && !rotation)
      out_pic_size_luma =
        (image_info->output_width_thumb * (jpeg_in->slice_mb_set * 16));
    else
      out_pic_size_luma =
        (image_info->output_width_thumb * image_info->output_height_thumb);

    /* chroma size ==> semiplanar output */
    if(image_info->output_format_thumb == JPEGDEC_YCbCr420_SEMIPLANAR ||
        image_info->output_format_thumb == JPEGDEC_YCbCr411_SEMIPLANAR)
      out_pic_size_chroma = out_pic_size_luma / 2;
    else if(image_info->output_format_thumb == JPEGDEC_YCbCr422_SEMIPLANAR ||
            image_info->output_format_thumb == JPEGDEC_YCbCr440)
      out_pic_size_chroma = out_pic_size_luma;
    else if(image_info->output_format_thumb == JPEGDEC_YCbCr444_SEMIPLANAR)
      out_pic_size_chroma = out_pic_size_luma * 2;

    if(image_info->coding_mode_thumb != JPEGDEC_BASELINE)
      separate_chroma = 1;
  }

#ifdef LINUX
  {
    fprintf(stdout, "\t\t-JPEG: USER OUTPUT MEMORY ALLOCATION\n");

    jpeg_in->picture_buffer_y.virtual_address = NULL;
    jpeg_in->picture_buffer_cb_cr.virtual_address = NULL;
    jpeg_in->picture_buffer_cr.virtual_address = NULL;

    /**** memory area ****/

    /* allocate memory for stream buffer. if unsuccessful -> exit */
    user_alloc_luma.virtual_address = NULL;
    user_alloc_luma.bus_address = 0;

    /* allocate memory for stream buffer. if unsuccessful -> exit */
    if(DWLMallocRefFrm
        (((JpegDecContainer *) dec_inst)->dwl, out_pic_size_luma,
         &user_alloc_luma) != DWL_OK) {
      fprintf(stdout, "UNABLE TO ALLOCATE USER LUMA OUTPUT MEMORY\n");
      return JPEGDEC_MEMFAIL;
    }

    /* Luma Bus */
    jpeg_in->picture_buffer_y.virtual_address = user_alloc_luma.virtual_address;
    jpeg_in->picture_buffer_y.bus_address = user_alloc_luma.bus_address;

    /* memset output to gray */
    (void) DWLmemset(jpeg_in->picture_buffer_y.virtual_address, 128,
                     out_pic_size_luma);

    /* allocate chroma */
    if(out_pic_size_chroma) {
      /* Baseline ==> semiplanar */
      if(separate_chroma == 0) {
        /* allocate memory for stream buffer. if unsuccessful -> exit */
        if(DWLMallocRefFrm
            (((JpegDecContainer *) dec_inst)->dwl, out_pic_size_chroma,
             &user_alloc_chroma) != DWL_OK) {
          fprintf(stdout,
                  "UNABLE TO ALLOCATE USER CHROMA OUTPUT MEMORY\n");
          return JPEGDEC_MEMFAIL;
        }

        /* Chroma Bus */
        jpeg_in->picture_buffer_cb_cr.virtual_address =
          user_alloc_chroma.virtual_address;
        jpeg_in->picture_buffer_cb_cr.bus_address =
          user_alloc_chroma.bus_address;

        /* memset output to gray */
        (void) DWLmemset(jpeg_in->picture_buffer_cb_cr.virtual_address, 128,
                         out_pic_size_chroma);
      } else { /* Progressive or non-interleaved ==> planar */
        /* allocate memory for stream buffer. if unsuccessful -> exit */
        /* Cb */
        if(DWLMallocRefFrm
            (((JpegDecContainer *) dec_inst)->dwl,
             (out_pic_size_chroma / 2), &user_alloc_chroma) != DWL_OK) {
          fprintf(stdout,
                  "UNABLE TO ALLOCATE USER CHROMA OUTPUT MEMORY\n");
          return JPEGDEC_MEMFAIL;
        }

        /* Chroma Bus */
        jpeg_in->picture_buffer_cb_cr.virtual_address =
          user_alloc_chroma.virtual_address;
        jpeg_in->picture_buffer_cb_cr.bus_address =
          user_alloc_chroma.bus_address;

        /* Cr */
        if(DWLMallocRefFrm
            (((JpegDecContainer *) dec_inst)->dwl,
             (out_pic_size_chroma / 2), &user_alloc_cr) != DWL_OK) {
          fprintf(stdout,
                  "UNABLE TO ALLOCATE USER CHROMA OUTPUT MEMORY\n");
          return JPEGDEC_MEMFAIL;
        }

        /* Chroma Bus */
        jpeg_in->picture_buffer_cr.virtual_address =
          user_alloc_cr.virtual_address;
        jpeg_in->picture_buffer_cr.bus_address = user_alloc_cr.bus_address;

        /* memset output to gray */
        /* Cb */
        (void) DWLmemset(jpeg_in->picture_buffer_cb_cr.virtual_address, 128,
                         (out_pic_size_chroma / 2));

        /* Cr */
        (void) DWLmemset(jpeg_in->picture_buffer_cr.virtual_address, 128,
                         (out_pic_size_chroma / 2));
      }
    }
  }
#endif /* #ifdef LINUX */

#ifndef LINUX
  {
    fprintf(stdout, "\t\t-JPEG: MALLOC\n");

    /* allocate luma */
    jpeg_in->picture_buffer_y.virtual_address =
      (u32 *) JpegDecMalloc(sizeof(u8) * out_pic_size_luma);

    JpegDecMemset(jpeg_in->picture_buffer_y.virtual_address, 128,
                  out_pic_size_luma);

    /* allocate chroma */
    if(out_pic_size_chroma) {
      jpeg_in->picture_buffer_cb_cr.virtual_address =
        (u32 *) JpegDecMalloc(sizeof(u8) * out_pic_size_chroma);

      JpegDecMemset(jpeg_in->picture_buffer_cb_cr.virtual_address, 128,
                    out_pic_size_chroma);
    }
  }
#endif /* #ifndef LINUX */

  fprintf(stdout, "\t\t-JPEG: Allocate: Luma virtual %lx bus %lx size %d\n",
          (addr_t)jpeg_in->picture_buffer_y.virtual_address,
          jpeg_in->picture_buffer_y.bus_address, out_pic_size_luma);

  if(separate_chroma == 0) {
    fprintf(stdout,
            "\t\t-JPEG: Allocate: Chroma virtual %lx bus %lx size %d\n",
            (addr_t)jpeg_in->picture_buffer_cb_cr.virtual_address,
            jpeg_in->picture_buffer_cb_cr.bus_address, out_pic_size_chroma);
  } else {
    fprintf(stdout,
            "\t\t-JPEG: Allocate: Cb virtual %lx bus %lx size %d\n",
            (addr_t)jpeg_in->picture_buffer_cb_cr.virtual_address,
            jpeg_in->picture_buffer_cb_cr.bus_address,
            (out_pic_size_chroma / 2));

    fprintf(stdout,
            "\t\t-JPEG: Allocate: Cr virtual %lx bus %lx size %d\n",
            (addr_t)jpeg_in->picture_buffer_cr.virtual_address,
            jpeg_in->picture_buffer_cr.bus_address, (out_pic_size_chroma / 2));
  }

  return JPEGDEC_OK;
}

/*-----------------------------------------------------------------------------

Print JPEG api return value

-----------------------------------------------------------------------------*/
void PrintJpegRet(JpegDecRet * p_jpeg_ret) {

  assert(p_jpeg_ret);

  switch (*p_jpeg_ret) {
  case JPEGDEC_FRAME_READY:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_FRAME_READY\n");
    break;
  case JPEGDEC_OK:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_OK\n");
    break;
  case JPEGDEC_ERROR:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_ERROR\n");
    break;
  case JPEGDEC_DWL_HW_TIMEOUT:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_HW_TIMEOUT\n");
    break;
  case JPEGDEC_UNSUPPORTED:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_UNSUPPORTED\n");
    break;
  case JPEGDEC_PARAM_ERROR:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_PARAM_ERROR\n");
    break;
  case JPEGDEC_MEMFAIL:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_MEMFAIL\n");
    break;
  case JPEGDEC_INITFAIL:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_INITFAIL\n");
    break;
  case JPEGDEC_HW_BUS_ERROR:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_HW_BUS_ERROR\n");
    break;
  case JPEGDEC_SYSTEM_ERROR:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_SYSTEM_ERROR\n");
    break;
  case JPEGDEC_DWL_ERROR:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_DWL_ERROR\n");
    break;
  case JPEGDEC_INVALID_STREAM_LENGTH:
    fprintf(stdout,
            "TB: jpeg API returned : JPEGDEC_INVALID_STREAM_LENGTH\n");
    break;
  case JPEGDEC_STRM_ERROR:
    fprintf(stdout, "TB: jpeg API returned : JPEGDEC_STRM_ERROR\n");
    break;
  case JPEGDEC_INVALID_INPUT_BUFFER_SIZE:
    fprintf(stdout,
            "TB: jpeg API returned : JPEGDEC_INVALID_INPUT_BUFFER_SIZE\n");
    break;
  case JPEGDEC_INCREASE_INPUT_BUFFER:
    fprintf(stdout,
            "TB: jpeg API returned : JPEGDEC_INCREASE_INPUT_BUFFER\n");
    break;
  case JPEGDEC_SLICE_MODE_UNSUPPORTED:
    fprintf(stdout,
            "TB: jpeg API returned : JPEGDEC_SLICE_MODE_UNSUPPORTED\n");
    break;
  default:
    fprintf(stdout, "TB: jpeg API returned unknown status\n");
    break;
  }
}

/*-----------------------------------------------------------------------------

Print JpegDecGetImageInfo values

-----------------------------------------------------------------------------*/
void PrintGetImageInfo(JpegDecImageInfo * image_info) {
  assert(image_info);

  /* Select if Thumbnail or full resolution image will be decoded */
  if(image_info->thumbnail_type == JPEGDEC_THUMBNAIL_JPEG) {
    /* decode thumbnail */
    fprintf(stdout, "\t-JPEG THUMBNAIL IN STREAM\n");
    fprintf(stdout, "\t-JPEG THUMBNAIL INFO\n");
    fprintf(stdout, "\t\t-JPEG thumbnail width: %d\n",
            image_info->output_width_thumb);
    fprintf(stdout, "\t\t-JPEG thumbnail height: %d\n",
            image_info->output_height_thumb);

    /* stream type */
    switch (image_info->coding_mode_thumb) {
    case JPEGDEC_BASELINE:
      fprintf(stdout, "\t\t-JPEG: STREAM TYPE: JPEGDEC_BASELINE\n");
      break;
    case JPEGDEC_PROGRESSIVE:
      fprintf(stdout, "\t\t-JPEG: STREAM TYPE: JPEGDEC_PROGRESSIVE\n");
      break;
    case JPEGDEC_NONINTERLEAVED:
      fprintf(stdout, "\t\t-JPEG: STREAM TYPE: JPEGDEC_NONINTERLEAVED\n");
      break;
    }

    if(image_info->output_format_thumb) {
      switch (image_info->output_format_thumb) {
      case JPEGDEC_YCbCr400:
        fprintf(stdout,
                "\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr400\n");
        break;
      case JPEGDEC_YCbCr420_SEMIPLANAR:
        fprintf(stdout,
                "\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr420_SEMIPLANAR\n");
        break;
      case JPEGDEC_YCbCr422_SEMIPLANAR:
        fprintf(stdout,
                "\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr422_SEMIPLANAR\n");
        break;
      case JPEGDEC_YCbCr440:
        fprintf(stdout,
                "\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr440\n");
        break;
      case JPEGDEC_YCbCr411_SEMIPLANAR:
        fprintf(stdout,
                "\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr411_SEMIPLANAR\n");
        break;
      case JPEGDEC_YCbCr444_SEMIPLANAR:
        fprintf(stdout,
                "\t\t-JPEG: THUMBNAIL OUTPUT: JPEGDEC_YCbCr444_SEMIPLANAR\n");
        break;
      }
    }
  } else if(image_info->thumbnail_type == JPEGDEC_NO_THUMBNAIL) {
    /* decode full image */
    fprintf(stdout,
            "\t-NO THUMBNAIL IN STREAM ==> Decode full resolution image\n");
  } else if(image_info->thumbnail_type == JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT) {
    /* decode full image */
    fprintf(stdout,
            "\tNot SUPPORTED THUMBNAIL IN STREAM ==> Decode full resolution image\n");
  }

  fprintf(stdout, "\t-JPEG FULL RESOLUTION INFO\n");
  fprintf(stdout, "\t\t-JPEG width: %d\n", image_info->output_width);
  fprintf(stdout, "\t\t-JPEG height: %d\n", image_info->output_height);
  if(image_info->output_format) {
    switch (image_info->output_format) {
    case JPEGDEC_YCbCr400:
      fprintf(stdout,
              "\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr400\n");
#ifdef ASIC_TRACE_SUPPORT
      trace_jpeg_dec_tools.sampling_4_0_0 = 1;
#endif
      break;
    case JPEGDEC_YCbCr420_SEMIPLANAR:
      fprintf(stdout,
              "\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr420_SEMIPLANAR\n");
#ifdef ASIC_TRACE_SUPPORT
      trace_jpeg_dec_tools.sampling_4_2_0 = 1;
#endif
      break;
    case JPEGDEC_YCbCr422_SEMIPLANAR:
      fprintf(stdout,
              "\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr422_SEMIPLANAR\n");
#ifdef ASIC_TRACE_SUPPORT
      trace_jpeg_dec_tools.sampling_4_2_2 = 1;
#endif
      break;
    case JPEGDEC_YCbCr440:
      fprintf(stdout,
              "\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr440\n");
#ifdef ASIC_TRACE_SUPPORT
      trace_jpeg_dec_tools.sampling_4_4_0 = 1;
#endif
      break;
    case JPEGDEC_YCbCr411_SEMIPLANAR:
      fprintf(stdout,
              "\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr411_SEMIPLANAR\n");
#ifdef ASIC_TRACE_SUPPORT
      trace_jpeg_dec_tools.sampling_4_1_1 = 1;
#endif
      break;
    case JPEGDEC_YCbCr444_SEMIPLANAR:
      fprintf(stdout,
              "\t\t-JPEG: FULL RESOLUTION OUTPUT: JPEGDEC_YCbCr444_SEMIPLANAR\n");
#ifdef ASIC_TRACE_SUPPORT
      trace_jpeg_dec_tools.sampling_4_4_4 = 1;
#endif
      break;
    }
  }

  /* stream type */
  switch (image_info->coding_mode) {
  case JPEGDEC_BASELINE:
    fprintf(stdout, "\t\t-JPEG: STREAM TYPE: JPEGDEC_BASELINE\n");
    break;
  case JPEGDEC_PROGRESSIVE:
    fprintf(stdout, "\t\t-JPEG: STREAM TYPE: JPEGDEC_PROGRESSIVE\n");
#ifdef ASIC_TRACE_SUPPORT
    trace_jpeg_dec_tools.progressive = 1;
#endif
    break;
  case JPEGDEC_NONINTERLEAVED:
    fprintf(stdout, "\t\t-JPEG: STREAM TYPE: JPEGDEC_NONINTERLEAVED\n");
    break;
  }

  if(image_info->thumbnail_type == JPEGDEC_THUMBNAIL_JPEG) {
    fprintf(stdout, "\t-JPEG ThumbnailType: JPEG\n");
#ifdef ASIC_TRACE_SUPPORT
    trace_jpeg_dec_tools.thumbnail = 1;
#endif
  } else if(image_info->thumbnail_type == JPEGDEC_NO_THUMBNAIL)
    fprintf(stdout, "\t-JPEG ThumbnailType: NO THUMBNAIL\n");
  else if(image_info->thumbnail_type == JPEGDEC_THUMBNAIL_NOT_SUPPORTED_FORMAT)
    fprintf(stdout, "\t-JPEG ThumbnailType: NOT SUPPORTED THUMBNAIL\n");
}

/*------------------------------------------------------------------------------

    Function name:  JpegDecMalloc

------------------------------------------------------------------------------*/
void *JpegDecMalloc(unsigned int size) {
  void *mem_ptr = (char *) malloc(size);

  return mem_ptr;
}

/*------------------------------------------------------------------------------

    Function name:  JpegDecMemset

------------------------------------------------------------------------------*/
void *JpegDecMemset(void *ptr, int c, unsigned int size) {
  void *rv = NULL;

  if(ptr != NULL) {
    rv = memset(ptr, c, size);
  }
  return rv;
}

/*------------------------------------------------------------------------------

    Function name:  JpegDecFree

------------------------------------------------------------------------------*/
void JpegDecFree(void *ptr) {
  if(ptr != NULL) {
    free(ptr);
  }
}

/*------------------------------------------------------------------------------

    Function name:  JpegDecTrace

    Purpose:
        Example implementation of JpegDecTrace function. Prototype of this
        function is given in jpegdecapi.h. This implementation appends
        trace messages to file named 'dec_api.trc'.

------------------------------------------------------------------------------*/
void JpegDecTrace(const char *string) {
  FILE *fp;

  fp = fopen("dec_api.trc", "at");

  if(!fp)
    return;

  fwrite(string, 1, strlen(string), fp);
  fwrite("\n", 1, 1, fp);

  fclose(fp);
}

/*-----------------------------------------------------------------------------

    Function name:  FindImageInfoEnd

    Purpose:
        Finds 0xFFC4 from the stream and p_offset includes number of bytes to
        this marker. In case of an error returns != 0
        (i.e., the marker not found).

-----------------------------------------------------------------------------*/
u32 FindImageInfoEnd(u8 * stream, u32 stream_length, u32 * p_offset) {
  u32 i;

  for(i = 0; i < stream_length; ++i) {
    if(0xFF == stream[i]) {
      if(((i + 1) < stream_length) && 0xC4 == stream[i + 1]) {
        *p_offset = i;
        return 0;
      }
    }
  }
  return -1;
}

/*-----------------------------------------------------------------------------

    Function name:  FindImageEnd

    Purpose:
        Finds 0xFFD9 from the stream and p_offset includes number of bytes to
        this marker. In case of an error returns != 0
        (i.e., the marker not found).

-----------------------------------------------------------------------------*/
u32 FindImageEnd(u8 * stream, u32 stream_length, u32 * p_offset) {
  u32 i,j;
  u32 jpeg_thumb_in_stream = 0;
  u32 tmp, tmp1, tmp_total = 0;
  u32 last_marker = 0;

  for(i = 0; i < stream_length; ++i) {
    if(0xFF == stream[i]) {
      /* if 0xFFE1 to 0xFFFD ==> skip  */
      if( ((i + 1) < stream_length) && 0xE1 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE2 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE3 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE4 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE5 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE6 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE7 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE8 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xE9 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xEA == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xEB == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xEC == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xED == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xEE == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xEF == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF0 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF1 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF2 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF3 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF4 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF5 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF6 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF7 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF8 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xF9 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xFA == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xFB == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xFC == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xFD == stream[i + 1] ) {
        /* increase counter */
        i += 2;

        /* check length vs. data */
        if((i + 1) > (stream_length))
          return (-1);

        /* get length */
        tmp = stream[i];
        tmp1 = stream[i+1];
        tmp_total = (tmp << 8) | tmp1;

        /* check length vs. data */
        if((tmp_total + i) > (stream_length))
          return (-1);
        /* update */
        i += tmp_total-1;
        continue;
      }

      /* if 0xFFC2 to 0xFFCB ==> skip  */
      if( ((i + 1) < stream_length) && 0xC1 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xC2 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xC3 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xC5 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xC6 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xC7 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xC8 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xC9 == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xCA == stream[i + 1] ||
          ((i + 1) < stream_length) && 0xCB == stream[i + 1] ) {
        /* increase counter */
        i += 2;

        /* check length vs. data */
        if((i + 1) > (stream_length))
          return (-1);

        /* increase counter */
        i += 2;

        /* check length vs. data */
        if((i + 1) > (stream_length))
          return (-1);

        /* get length */
        tmp = stream[i];
        tmp1 = stream[i+1];
        tmp_total = (tmp << 8) | tmp1;

        /* check length vs. data */
        if((tmp_total + i) > (stream_length))
          return (-1);
        /* update */
        i += tmp_total-1;

        /* look for EOI */
        for(j = i; j < stream_length; ++j) {
          if(0xFF == stream[j]) {
            /* EOI */
            if(((j + 1) < stream_length) && 0xD9 == stream[j + 1]) {
              /* check length vs. data */
              if((j + 2) >= (stream_length)) {
                nbr_of_images_total = nbr_of_images;
                nbr_of_thumb_images_total = nbr_of_thumb_images;
                return (0);
              }
              /* update */
              i = j;
              /* stil data left ==> continue */
              continue;
            }
          }
        }
      }

      /* check if thumbnails in stream */
      if(((i + 1) < stream_length) && 0xE0 == stream[i + 1]) {
        if( ((i + 9) < stream_length) &&
            0x4A == stream[i + 4] &&
            0x46 == stream[i + 5] &&
            0x58 == stream[i + 6] &&
            0x58 == stream[i + 7] &&
            0x00 == stream[i + 8] &&
            0x10 == stream[i + 9]) {
          jpeg_thumb_in_stream = 1;
        }
        last_marker = 1;
      }

      /* EOI */
      if(((i + 1) < stream_length) && 0xD9 == stream[i + 1]) {
        *p_offset = i;
        /* update amount of thumbnail or full resolution image */
        if(jpeg_thumb_in_stream) {
          nbr_of_thumb_images++;
          jpeg_thumb_in_stream = 0;
        } else
          nbr_of_images++;

        last_marker = 2;
      }
    }
  }

  /* update total amount of pictures */
  nbr_of_images_total = nbr_of_images;
  nbr_of_thumb_images_total = nbr_of_thumb_images;

  /* continue until amount of frames counted */
  if(last_marker == 2)
    return 0;
  else
    return -1;
}

/*-----------------------------------------------------------------------------

    Function name:  FindImageEOI

    Purpose:
        Finds 0xFFD9 from the stream and p_offset includes number of bytes to
        this marker. In case of an error returns != 0
        (i.e., the marker not found).

-----------------------------------------------------------------------------*/
u32 FindImageEOI(u8 * stream, u32 stream_length, u32 * p_offset) {
  u32 i;

  for(i = 0; i < stream_length; ++i) {
    if(0xFF == stream[i]) {
      if(((i + 1) < stream_length) && 0xD9 == stream[i + 1]) {
        *p_offset = i+2;
        return 0;
      }
    }
  }
  return -1;
}
/*-----------------------------------------------------------------------------

    Function name:  FindImageTnEOI

    Purpose:
        Finds 0xFFD9 and next 0xFFE0 (containing THUMB) from the stream and
        p_offset includes number of bytes to this marker. In case of an error
        returns != 0 (i.e., the marker not found).

-----------------------------------------------------------------------------*/
u32 FindImageTnEOI(u8 * stream, u32 stream_length, u32 * p_offset, u32 mode, u32 thumb_exist) {
  u32 i,j,k;
  u32 h = 0;
  u32 tmp, tmp1, tmp_total = 0;

  /* reset */
  next_soi = 0;

  for(i = 0; i < stream_length; ++i) {
    if(0xFF == stream[i]) {
      if(((i + 1) < stream_length) && 0xD9 == stream[i + 1]) {
        if(thumb_exist) {
          for(j = (i+2); j < stream_length; ++j) {
            if(0xFF == stream[j]) {
              /* seek for next thumbnail in stream */
              if(((j + 1) < stream_length) && 0xE0 == stream[j + 1]) {
                if( ((j + 9) < stream_length) &&
                    0x4A == stream[j + 4] &&
                    0x46 == stream[j + 5] &&
                    0x58 == stream[j + 6] &&
                    0x58 == stream[j + 7] &&
                    0x00 == stream[j + 8] &&
                    0x10 == stream[j + 9]) {
                  next_soi = 1;
                  *p_offset = h;
                  return 0;
                }
              } else if(((j + 1) < stream_length) && 0xD9 == stream[j + 1]) {
                k = j+2;
                /* return if FULL */
                if(!mode) {
                  *p_offset = k;
                  return 0;
                }
              } else if(((j + 1) < stream_length) && 0xD8 == stream[j + 1]) {
                if(j) {
                  h = j;
                }
              }
            }
          }

          /* if no THUMB, but found next SOI ==> return */
          if(h) {
            *p_offset = h;
            next_soi = 1;
            return 0;
          } else
            return -1;
        } else {
          next_soi = 1;
          *p_offset = i+2;
          return 0;
        }
      }
      /* if 0xFFE1 to 0xFFFD ==> skip  */
      else if( ((i + 1) < stream_length) && 0xE1 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE2 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE3 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE4 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE5 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE6 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE7 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE8 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xE9 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xEA == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xEB == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xEC == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xED == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xEE == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xEF == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF0 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF1 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF2 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF3 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF4 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF5 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF6 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF7 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF8 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xF9 == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xFA == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xFB == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xFC == stream[i + 1] ||
               ((i + 1) < stream_length) && 0xFD == stream[i + 1] ) {
        /* increase counter */
        i += 2;
        /* check length vs. data */
        if((i + 1) > (stream_length))
          return (-1);

        /* get length */
        tmp = stream[i];
        tmp1 = stream[i+1];
        tmp_total = (tmp << 8) | tmp1;

        /* check length vs. data */
        if((tmp_total + i) > (stream_length))
          return (-1);
        /* update */
        i += tmp_total-1;
        continue;
      }
    }
  }
  return -1;
}

void WriteCroppedOutput(JpegDecImageInfo * info, u8 * data_luma, u8 * data_cb,
                        u8 * data_cr) {
  u32 i, j;
  FILE *foutput = NULL;
  u8 *p_yuv_out = NULL;
  u32 luma_w, luma_h, chroma_w, chroma_h, chroma_output_width, chroma_output_height;

  fprintf(stdout, "TB: WriteCroppedOut, display_w %d, display_h %d\n",
          info->display_width, info->display_height);

  foutput = fopen("cropped.yuv", "wb");
  if(foutput == NULL) {
    fprintf(stdout, "UNABLE TO OPEN OUTPUT FILE\n");
    return;
  }

  if(info->output_format == JPEGDEC_YCbCr420_SEMIPLANAR) {
    luma_w = (info->display_width + 1) & ~0x1;
    luma_h = (info->display_height + 1) & ~0x1;
    chroma_w = luma_w / 2;
    chroma_h = luma_h / 2;
    chroma_output_width = info->output_width / 2;
    chroma_output_height = info->output_height / 2;
  } else if(info->output_format == JPEGDEC_YCbCr422_SEMIPLANAR) {
    luma_w = (info->display_width + 1) & ~0x1;
    luma_h = info->display_height;
    chroma_w = luma_w / 2;
    chroma_h = luma_h;
    chroma_output_width = info->output_width / 2;
    chroma_output_height = info->output_height;
  } else if(info->output_format == JPEGDEC_YCbCr440) {
    luma_w = info->display_width;
    luma_h = (info->display_height + 1) & ~0x1;
    chroma_w = luma_w;
    chroma_h = luma_h / 2;
    chroma_output_width = info->output_width;
    chroma_output_height = info->output_height / 2;
  } else if(info->output_format == JPEGDEC_YCbCr411_SEMIPLANAR) {
    luma_w = (info->display_width + 3) & ~0x3;
    luma_h = info->display_height;
    chroma_w = luma_w / 4;
    chroma_h = luma_h;
    chroma_output_width = info->output_width / 4;
    chroma_output_height = info->output_height;
  } else if(info->output_format == JPEGDEC_YCbCr444_SEMIPLANAR) {
    luma_w = info->display_width;
    luma_h = info->display_height;
    chroma_w = luma_w;
    chroma_h = luma_h;
    chroma_output_width = info->output_width;
    chroma_output_height = info->output_height;
  } else {
    luma_w = info->display_width;
    luma_h = info->display_height;
    chroma_w = 0;
    chroma_h = 0;
    chroma_output_height = 0;
    chroma_output_height = 0;

  }

  /* write decoder output to file */
  p_yuv_out = data_luma;
  for(i = 0; i < luma_h; i++) {
    fwrite(p_yuv_out, sizeof(u8), luma_w, foutput);
    p_yuv_out += info->output_width;
  }

  p_yuv_out += (info->output_height - luma_h) * info->output_width;

  /* baseline -> output in semiplanar format */
  if(info->coding_mode != JPEGDEC_PROGRESSIVE) {
    for(i = 0; i < chroma_h; i++)
      for(j = 0; j < chroma_w; j++)
        fwrite(p_yuv_out + i * chroma_output_width * 2 + j * 2,
               sizeof(u8), 1, foutput);
    for(i = 0; i < chroma_h; i++)
      for(j = 0; j < chroma_w; j++)
        fwrite(p_yuv_out + i * chroma_output_width * 2 + j * 2 + 1,
               sizeof(u8), 1, foutput);
  } else {
    p_yuv_out = data_cb;
    for(i = 0; i < chroma_h; i++) {
      fwrite(p_yuv_out, sizeof(u8), chroma_w, foutput);
      p_yuv_out += chroma_output_width;
    }
    /*p_yuv_out += (chroma_output_height-chroma_h)*chroma_output_width; */
    p_yuv_out = data_cr;
    for(i = 0; i < chroma_h; i++) {
      fwrite(p_yuv_out, sizeof(u8), chroma_w, foutput);
      p_yuv_out += chroma_output_width;
    }
  }

  fclose(foutput);
}

void WriteProgressiveOutput(u32 size_luma, u32 size_chroma, u32 mode,
                            u8 * data_luma, u8 * data_cb, u8 * data_cr) {
  u32 i;
  FILE *foutput = NULL;

  fprintf(stdout, "TB: WriteProgressiveOutput\n");

  foutput = fopen("out.yuv", "ab");
  if(foutput == NULL) {
    fprintf(stdout, "UNABLE TO OPEN OUTPUT FILE\n");
    return;
  }

  /* write decoder output to file */
  fwrite(data_luma, sizeof(u8), size_luma, foutput);
  fwrite(data_cb, sizeof(u8), size_chroma / 2, foutput);
  fwrite(data_cr, sizeof(u8), size_chroma / 2, foutput);

  fclose(foutput);
}
/*------------------------------------------------------------------------------

    Function name: printJpegVersion

    Functional description: Print version info

    Inputs:

    Outputs:    NONE

    Returns:    NONE

------------------------------------------------------------------------------*/
void printJpegVersion(void) {

  JpegDecApiVersion dec_ver;
  JpegDecBuild dec_build;

  /*
   * Get decoder version info
   */

  dec_ver = JpegGetAPIVersion();
  printf("\nApi version:  %d.%d, ", dec_ver.major, dec_ver.minor);

  dec_build = JpegDecGetBuild();
  printf("sw build nbr: %d, hw build nbr: %x\n\n",
         dec_build.sw_build, dec_build.hw_build);

}
