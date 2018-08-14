/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
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

#include "dwl.h"

#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include "vp9decapi.h"

#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

#include "vp9hwd_container.h"
#include "software/test/common/vpxfilereader.h"

#include "tb_cfg.h"
#include "tb_tiled.h"
#include "regdrv.h"

#ifdef MD5SUM
#include "tb_md5.h"
#endif

#include "tb_stream_corrupt.h"
#include "tb_sw_performance.h"

/* Debug prints */
#undef DEBUG_PRINT
#define DEBUG_PRINT(argv) printf argv

#define NUM_RETRY 100 /* how many retries after HW timeout */
#define ALIGNMENT_MASK 7
#define MAX_BUFFERS 20
#define BUFFER_ALIGN_MASK 0xF

void WriteOutput(const char *filename, const char *filename_tiled, u8 *data, u8 *data_ch,
                 u32 pic_width, u32 pic_height, u32 frame_number,
                 u32 mono_chrome, u32 view, u32 tiled_mode,
                 u32 bit_depth_luma, u32 bit_depth_chroma, u32 pic_stride);
u32 NextPacket(u8 **strm);
u32 NextPacketFromFile(u8 **strm);
u32 CropPicture(u8 *out_image, u8 *in_image, u32 frame_width, u32 frame_height,
                const struct Vp9CropParams *crop_params, u32 mono_chrome,
                u32 convert2_planar);
static void print_decode_return(i32 retval);

#include "md5.h"
u32 md5 = 0;
struct MD5Context md5_ctx;

u32 fill_buffer(u8 *stream);
/* Global variables for stream handling */
u8 *stream_stop = NULL;
u32 packetize = 0;
u32 nal_unit_stream = 0;
FILE *foutput = NULL, *foutput2 = NULL;
FILE *f_tiled_output = NULL;
FILE *fchroma2 = NULL;

FILE *findex = NULL;

/* stream start address */
u8 *byte_strm_start;
u32 trace_used_stream = 0;

/* SW/SW testing, read stream trace file */
FILE *f_stream_trace = NULL;

/* Pack each pixel in 16 bits based on pixel bit width. */
u32 output16 = 0;
FILE *f_output16 = NULL;

/* output file writing disable */
u32 disable_output_writing = 0;
u32 retry = 0;

u32 clock_gating = DEC_X170_INTERNAL_CLOCK_GATING;
u32 clock_gating_runtime = DEC_X170_INTERNAL_CLOCK_GATING_RUNTIME;
u32 data_discard = DEC_X170_DATA_DISCARD_ENABLE;
u32 latency_comp = DEC_X170_LATENCY_COMPENSATION;
u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
u32 bus_burst_length = DEC_X170_BUS_BURST_LENGTH;
u32 asic_service_priority = DEC_X170_ASIC_SERVICE_PRIORITY;
u32 output_format = DEC_X170_OUTPUT_FORMAT;
u32 service_merge_disable = DEC_X170_SERVICE_MERGE_DISABLE;
u32 bus_width = DEC_X170_BUS_WIDTH;

u32 strm_swap = HANTRODEC_STREAM_SWAP;
u32 pic_swap = HANTRODEC_STREAM_SWAP;
u32 dirmv_swap = HANTRODEC_STREAM_SWAP;
u32 tab0_swap = HANTRODEC_STREAM_SWAP;
u32 tab1_swap = HANTRODEC_STREAM_SWAP;
u32 tab2_swap = HANTRODEC_STREAM_SWAP;
u32 tab3_swap = HANTRODEC_STREAM_SWAP;
u32 rscan_swap = HANTRODEC_STREAM_SWAP;
u32 max_burst = HANTRODEC_MAX_BURST;
u32 double_ref_buffer = HANTRODEC_INTERNAL_DOUBLE_REF_BUFFER;
u32 timeout_cycles = HANTRODEC_TIMEOUT_OVERRIDE;

u32 stream_truncate = 0;
u32 stream_packet_loss = 0;
u32 stream_header_corrupt = 0;
u32 hdrs_rdy = 0;
u32 pic_rdy = 0;
struct TBCfg tb_cfg;

u32 long_stream = 0;
FILE *finput;
u32 planar_output = 0;
/*u32 tiled_output = 0;*/

u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 dpb_mode = DEC_DPB_DEFAULT;
u32 convert_tiled_output = 0;

u32 use_peek_output = 0;
u32 skip_non_reference = 0;
u32 convert_to_frame_dpb = 0;

u32 write_raster_out = 0;

/* variables for indexing */
u32 save_index = 0;
u32 use_index = 0;
off64_t cur_index = 0;
off64_t next_index = 0;
/* indicate when we save index */
u8 save_flag = 0;

u32 b_frames;

u32 test_case_id = 0;

u8 *grey_chroma = NULL;
size_t grey_chroma_size = 0;

u8 *raster_scan = NULL;
size_t raster_scan_size = 0;

u8 *pic_big_endian = NULL;
size_t pic_big_endian_size = 0;

#ifdef ASIC_TRACE_SUPPORT
extern u32 hw_dec_pic_count;
u32 hevc_support;
#endif

#ifdef HEVC_EVALUATION
u32 hevc_support;
#endif

#ifdef ADS_PERFORMANCE_SIMULATION

volatile u32 tttttt = 0;

void Traceperf() {
  tttttt++;
}

#undef START_SW_PERFORMANCE
#undef END_SW_PERFORMANCE

#define START_SW_PERFORMANCE Traceperf();
#define END_SW_PERFORMANCE Traceperf();

#endif

Vp9DecInst dec_inst;
const void *dwl_inst = NULL;

const char *out_file_name = "out.yuv";
const char *out_file_name_tiled = "out_tiled.yuv";
const char *out_file_name_planar = "out_planar.yuv";
const char *out_file_name_16 = "out16.yuv";

u32 crop_display = 0;
u32 pic_size = 0;
u8 *cropped_pic = NULL;
size_t cropped_pic_size = 0;
struct Vp9DecInfo dec_info;

pid_t main_pid;
pthread_t output_thread;
int output_thread_run = 0;
u32 num_errors = 0;

/* Testing for external buffer  */
u32 use_extra_buffers = 0;
u32 buffer_size;
volatile u32 num_buffers;  /* external buffers allocated yet. */
u32 add_buffer_thread_run = 0;
pthread_t add_buffer_thread;

#ifdef TESTBENCH_WATCHDOG
static u32 old_pic_display_number = 0;

static void WatchdogCb(int signal_number) {
  if (pic_display_number == old_pic_display_number) {
    fprintf(stderr, "\n\nTestbench TIMEOUT\n");
    kill(main_pid, SIGTERM);
  } else {
    old_pic_display_number = pic_display_number;
  }
}
#endif

#ifdef USE_EXTERNAL_BUFFER

struct DWLLinearMem ext_buffers[MAX_BUFFERS];
u32 buffer_consumed[MAX_BUFFERS];

u32 add_extra_flag = 0;
u32 extra_buffer_num = 0;

u32 FindExtBufferIndex(u32 *addr) {
  u32 i;
  for (i = 0; i < num_buffers; i++) {
    if (ext_buffers[i].virtual_address == addr)
      break;
  }

  ASSERT(i < num_buffers);
  return i;
}

u32 FindEmptyIndex() {
  u32 i;
  for (i = 0; i < MAX_BUFFERS; i++) {
    if (ext_buffers[i].virtual_address == NULL)
      break;
  }

  ASSERT(i < MAX_BUFFERS);
  return i;
}

static void *AddBufferThread(void *arg) {
  u32 sec;
  struct DWLLinearMem mem;
  enum DecRet rv;

  sleep(3);

  while (add_buffer_thread_run) {
    if (!add_extra_flag) continue;

    if (num_buffers < MAX_BUFFERS) {
      DWLMallocLinear(dwl_inst, buffer_size, &mem);
      rv = Vp9DecAddBuffer(dec_inst, &mem);
      DEBUG_PRINT(("Vp9DecAddBuffer ret %d (%d extra buffers)\n", rv, ++extra_buffer_num));
      buffer_consumed[num_buffers] = 1;
      ext_buffers[num_buffers++] = mem;
    }

    /* After sleeping random seconds, add one external buffer.*/
    sec = 3;
    TBRandomizeU32(&sec);
    if (!sec) sec = 1;
    sleep(sec);
  }
  return NULL;
}

void ReleaseExtBuffers() {
  DEBUG_PRINT(("Releasing %d external frame buffers\n", num_buffers));
  for (int i=0; i<num_buffers; i++) {
    while (buffer_consumed[i] == 0) sched_yield();

    if (ext_buffers[i].virtual_address != NULL) {
      DEBUG_PRINT(("Freeing buffer %p\n", ext_buffers[i].virtual_address));
      DWLFreeLinear(dwl_inst, &ext_buffers[i]);
      DWLmemset(&ext_buffers[i], 0, sizeof(ext_buffers[i]));
      //DEBUG_PRINT(("DWLFreeLinear ret %d\n", rv));
    }
  }
}
#endif

static void *Vp9OutputThread(void *arg) {
  struct Vp9DecPicture dec_picture;
  u32 pic_display_number = 1;

#ifdef TESTBENCH_WATCHDOG
  /* fpga watchdog: 30 sec timer for frozen decoder */
  {
    struct itimerval t = {{30, 0}, {30, 0}};
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = WatchdogCb;
    sa.sa_flags |= SA_RESTART; /* restart of system calls */
    sigaction(SIGALRM, &sa, NULL);

    setitimer(ITIMER_REAL, &t, NULL);
  }
#endif

  while (output_thread_run) {
    enum DecRet ret;
#ifdef USE_EXTERNAL_BUFFER
    u32 id;
#endif

    ret = Vp9DecNextPicture(dec_inst, &dec_picture);

    if (ret == DEC_PIC_RDY) {
      DEBUG_PRINT(("PIC %d/%d, type %s", pic_display_number, dec_picture.pic_id,
                   dec_picture.is_intra_frame ? "    IDR" : "NON-IDR"));

      DEBUG_PRINT((", %d x %d, crop: (%d, %d) @ %p\n",
                   dec_picture.coded_width, dec_picture.coded_height,
                   dec_picture.frame_width,
                   dec_picture.frame_height,
                   (void *)dec_picture.output_luma_base));

#ifdef USE_EXTERNAL_BUFFER
      id = FindExtBufferIndex(dec_picture.output_luma_base);
      buffer_consumed[id] = 0;
#endif

      /* count erroneous pictures */
      if (dec_picture.nbr_of_err_mbs) num_errors++;

      /* Write output picture to file */
      WriteOutput(out_file_name, out_file_name_tiled,
                  (u8 *)dec_picture.output_luma_base,
                  (u8 *)dec_picture.output_chroma_base,
                  dec_picture.coded_width, dec_picture.coded_height,
                  pic_display_number - 1, 0, 0,
                  dec_picture.output_format,
                  dec_picture.bit_depth_luma, dec_picture.bit_depth_chroma, dec_picture.pic_stride);

      Vp9DecPictureConsumed(dec_inst, &dec_picture);

#ifdef USE_EXTERNAL_BUFFER
      buffer_consumed[id] = 1;
#endif

      // usleep(10000); /* 10ms  sleep */

      pic_display_number++;
    } else if (ret == DEC_END_OF_STREAM) {
      DEBUG_PRINT(("END-OF-STREAM received in output thread\n"));
      add_buffer_thread_run = 0;
      break;
    }
  }

  return NULL;
}

void ParseSuperframeIndex(const u8* data, u32 data_sz,
                          u32 sizes[8], u32* count) {
  const u8 marker = data[data_sz - 1];
  *count = 0;

  if ((marker & 0xe0) == 0xc0) {
    const int frames = (marker & 0x7) + 1;
    const int mag = ((marker >> 3) & 0x3) + 1;
    const u32 index_sz = 2 + mag * frames;

    if (data_sz >= index_sz && data[data_sz - index_sz] == marker) {
      // found a valid superframe index
      const u8* x = data + data_sz - index_sz + 1;

      for (int i = 0; i < frames; ++i) {
        uint32 this_sz = 0;

        for (int j = 0; j < mag; ++j) {
          this_sz |= (*x++) << (j * 8);
        }
        sizes[i] = this_sz;
      }
      *count = frames;
    }
  }
}

int main(int argc, char **argv) {

  u32 i, tmp;
  u32 max_num_pics = 0;
  long int strm_len;
  enum DecRet ret;
  struct Vp9DecInput dec_input;
  struct Vp9DecOutput dec_output;
  struct DWLLinearMem stream_mem[2];
  u32 strm_mem_id = 0;
  u32 prev_width, prev_height;
#ifdef USE_EXTERNAL_BUFFER
  struct Vp9DecBufferInfo hbuf;
  enum DecRet rv;
  memset(ext_buffers, 0, sizeof(ext_buffers));
#endif

  u32 pic_decode_number = 1;
  u32 disable_output_reordering = 0;
  u32 use_extra_dpb_frms = 0;
  u32 rlc_mode = 0;
  u32 trace_target = 0;
  u32 mb_error_concealment = 0;
  u32 force_whole_stream = 0;
  const u8 *ptmpstream = NULL;
  u32 stream_will_end = 0;

  FILE *f_tbcfg;
  u32 seed_rnd = 0;
  u32 stream_bit_swap = 0;
  i32 corrupted_bytes = 0; /*  */
  u32 bus_width_in_bytes;

  struct DWLInitParam dwl_params = {DWL_CLIENT_TYPE_HEVC_DEC};
  u32 min_buffer_num = 0;

  VpxReaderInst vpx_reader_inst;
  u8 *stream[2];
  u32 sizes[8];         /* size of each frames in current super frame */
  u32 frames = 0;       /* frame count in current super frame */
  u32 frame_index = 0;  /* index of frame being decoded in current super frame */

#ifdef ASIC_TRACE_SUPPORT
  hevc_support = 1;
#endif

#ifndef EXPIRY_DATE
#define EXPIRY_DATE (u32)0xFFFFFFFF
#endif /* EXPIRY_DATE */

  main_pid = getpid();

  /* expiry stuff */
  {
    char tm_buf[7];
    time_t sys_time;
    struct tm *tm;
    u32 tmp1;

    /* Check expiry date */
    time(&sys_time);
    tm = localtime(&sys_time);
    strftime(tm_buf, sizeof(tm_buf), "%y%m%d", tm);
    tmp1 = 1000000 + atoi(tm_buf);
    if (tmp1 > (EXPIRY_DATE) && (EXPIRY_DATE) > 1) {
      fprintf(stderr,
              "EVALUATION PERIOD EXPIRED.\n"
              "Please contact On2 Sales.\n");
      return -1;
    }
  }

  stream_mem[0].virtual_address = NULL;
  stream_mem[0].bus_address = 0;
  stream_mem[1].virtual_address = NULL;
  stream_mem[1].bus_address = 0;

  INIT_SW_PERFORMANCE;

  {
    Vp9DecBuild dec_build;

    dec_build = Vp9DecGetBuild();
    DEBUG_PRINT(("\nVp9 SW build: %d - HW build: %x\n\n", dec_build.sw_build,
                 dec_build.hw_build));
  }

  /* Check that enough command line arguments given, if not -> print usage
   * information out */
  if (argc < 2) {
    DEBUG_PRINT(("Usage: %s [options] file.hevc\n", argv[0]));
    DEBUG_PRINT(("\t-Nn forces decoding to stop after n pictures\n"));
    DEBUG_PRINT(
      ("\t-Ooutfile write output to \"outfile\" (default "
       "out_wxxxhyyy.yuv)\n"));
    DEBUG_PRINT(("\t-X Disable output file writing\n"));
    DEBUG_PRINT(("\t-C display cropped image (default decoded image)\n"));
    DEBUG_PRINT(("\t-R disable DPB output reordering\n"));
    DEBUG_PRINT(("\t-J<n> use 'n' extra frames in DPB\n"));
    DEBUG_PRINT(("\t-Sfile.hex stream control trace file\n"));
    DEBUG_PRINT(
      ("\t-W disable packetizing even if stream does not fit to buffer.\n"
       "\t   Only the first 0x1FFFFF bytes of the stream are decoded.\n"));
    DEBUG_PRINT(("\t-E use tiled reference frame format.\n"));
    DEBUG_PRINT(("\t-G convert tiled output pictures to raster scan\n"));
    DEBUG_PRINT(("\t-L enable support for long streams\n"));
    DEBUG_PRINT(("\t-P write planar output\n"));
    DEBUG_PRINT(("\t-I save index file\n"));
    /*        DEBUG_PRINT(("\t-T write tiled output (out_tiled.yuv) by
     * converting raster scan output\n"));*/
    DEBUG_PRINT(("\t-Z output pictures using Vp9DecPeek() function\n"));
    DEBUG_PRINT(("\t-r trace file format for RTL  (extra CU ctrl)\n"));
    DEBUG_PRINT(("\t--raster-out write additional raster scan output pic\n"));
    DEBUG_PRINT(
      ("\t--packet-by-packet parse stream and decode packet-by-packet\n"));
    DEBUG_PRINT(
      ("\t--md5 write md5 checksum for whole output (not frame-by-frame)\n"));
    DEBUG_PRINT(("\t-x add extra external buffer randomly\n"));
#ifdef ASIC_TRACE_SUPPORT
    DEBUG_PRINT(("\t-Q Skip decoding non-reference pictures.\n"));
    DEBUG_PRINT(("\t--output16 Output each pixel in 16 bits based on pixel bit width.\n"));
#endif
    return 0;
  }

  /* read command line arguments */
  for (i = 1; i < (u32)(argc - 1); i++) {
    if (strncmp(argv[i], "-N", 2) == 0) {
      max_num_pics = (u32)atoi(argv[i] + 2);
    } else if (strncmp(argv[i], "-O", 2) == 0) {
      out_file_name = argv[i] + 2;
    } else if (strcmp(argv[i], "-X") == 0) {
      disable_output_writing = 1;
    } else if (strcmp(argv[i], "-C") == 0) {
      crop_display = 1;
    } else if (strcmp(argv[i], "-E") == 0) {
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
      convert_tiled_output = 0;
    } else if (strncmp(argv[i], "-G", 2) == 0)
      convert_tiled_output = 1;
    else if (strcmp(argv[i], "-R") == 0) {
      disable_output_reordering = 1;
    } else if (strncmp(argv[i], "-J", 2) == 0) {
      use_extra_dpb_frms = (u32)atoi(argv[i] + 2);
    } else if (strncmp(argv[i], "-S", 2) == 0) {
      f_stream_trace = fopen((argv[i] + 2), "r");
    } else if (strcmp(argv[i], "-W") == 0) {
      force_whole_stream = 1;
    } else if (strcmp(argv[i], "-L") == 0) {
      long_stream = 1;
    } else if (strcmp(argv[i], "-P") == 0) {
      planar_output = 1;
    } else if (strcmp(argv[i], "-I") == 0) {
      save_index = 1;
    } else if (strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    } else if (strcmp(argv[i], "--separate-fields-in-dpb") == 0) {
      dpb_mode = DEC_DPB_INTERLACED_FIELD;
    } else if (strcmp(argv[i], "--output-frame-dpb") == 0) {
      convert_to_frame_dpb = 1;
    } else if (strcmp(argv[i], "-Z") == 0) {
      use_peek_output = 1;
    } else if (strcmp(argv[i], "-x") == 0) {
      use_extra_buffers = 1;
    } else if (strcmp(argv[i], "-r") == 0) {
      trace_target = 1;
    } else if (strcmp(argv[i], "--raster-out") == 0 ||
               strcmp(argv[i], "-Ers") == 0) {
      write_raster_out = 1;
    } else if (strcmp(argv[i], "--packet-by-packet") == 0) {
      packetize = 1;
    } else if (strcmp(argv[i], "--md5") == 0) {
      md5 = 1;
    } else if (strcmp(argv[i], "--output16") == 0) {
      output16 = 1;
    } else {
      DEBUG_PRINT(("UNKNOWN PARAMETER: %s\n", argv[i]));
      return 1;
    }
  }

  if (md5) {
    MD5Init(&md5_ctx);
  }

  if (planar_output) {
    if (!write_raster_out) {
      DEBUG_PRINT(("-P can only be used when \"-Ers\" is enabled.\n"));
      return -1;
    }
  }

  /* open input file for reading, file name given by user. If file open
   * fails -> exit */
  finput = fopen(argv[argc - 1], "rb");
  if (finput == NULL) {
    DEBUG_PRINT(("UNABLE TO OPEN INPUT FILE\n"));
    return -1;
  }

  vpx_reader_inst = VpxRdrOpen(argv[argc - 1], 0);

  /* determine test case id from input file name (if contains "case_") */
  {
    char *pc, *pe;
    char in[256];
    strncpy(in, argv[argc - 1], sizeof(in));
    pc = strstr(in, "case_");
    if (pc != NULL) {
      pc += 5;
      pe = strstr(pc, "/");
      if (pe == NULL) pe = strstr(pc, ".");
      if (pe != NULL) {
        *pe = '\0';
        test_case_id = atoi(pc);
      }
    }
  }

  /* open index file for saving */
  if (save_index) {
    findex = fopen("stream.cfg", "w");
    if (findex == NULL) {
      DEBUG_PRINT(("UNABLE TO OPEN INDEX FILE\n"));
      return -1;
    }
  }
  /* try open index file */
  else {
    findex = fopen("stream.cfg", "r");
    if (findex != NULL) {
      use_index = 1;
    }
  }

  /* set test bench configuration */
  TBSetDefaultCfg(&tb_cfg);
  f_tbcfg = fopen("tb.cfg", "r");
  if (f_tbcfg == NULL) {
    DEBUG_PRINT(("UNABLE TO OPEN INPUT FILE: \"tb.cfg\"\n"));
    DEBUG_PRINT(("USING DEFAULT CONFIGURATION\n"));
  } else {
    fclose(f_tbcfg);
    if (TBParseConfig("tb.cfg", TBReadParam, &tb_cfg) == TB_FALSE) return -1;
    if (TBCheckCfg(&tb_cfg) != 0) return -1;
  }
  /*TBPrintCfg(&tb_cfg); */
  mb_error_concealment = 0; /* TBGetDecErrorConcealment(&tb_cfg); */
  rlc_mode = TBGetDecRlcModeForced(&tb_cfg);
  clock_gating = TBGetDecClockGating(&tb_cfg);
  clock_gating_runtime = TBGetDecClockGatingRuntime(&tb_cfg);
  data_discard = TBGetDecDataDiscard(&tb_cfg);
  latency_comp = tb_cfg.dec_params.latency_compensation;
  output_picture_endian = TBGetDecOutputPictureEndian(&tb_cfg);
  bus_burst_length = tb_cfg.dec_params.bus_burst_length;
  asic_service_priority = tb_cfg.dec_params.asic_service_priority;
  output_format = TBGetDecOutputFormat(&tb_cfg);
  service_merge_disable = TBGetDecServiceMergeDisable(&tb_cfg);
  bus_width = TBGetDecBusWidth(&tb_cfg);
  /* bus_width 0 -> 32b, 1 -> 64b etc */
  bus_width_in_bytes = 4 << bus_width;
  /*#if MD5SUM
      output_picture_endian = DEC_X170_LITTLE_ENDIAN;
      printf("Decoder Output Picture Endian forced to %d\n",
             output_picture_endian);
  #endif*/

  strm_swap = tb_cfg.dec_params.strm_swap;
  pic_swap = tb_cfg.dec_params.pic_swap;
  dirmv_swap = tb_cfg.dec_params.dirmv_swap;
  tab0_swap = tb_cfg.dec_params.tab0_swap;
  tab1_swap = tb_cfg.dec_params.tab1_swap;
  tab2_swap = tb_cfg.dec_params.tab2_swap;
  tab3_swap = tb_cfg.dec_params.tab3_swap;
  rscan_swap = tb_cfg.dec_params.rscan_swap;
  max_burst = tb_cfg.dec_params.max_burst;

  double_ref_buffer = tb_cfg.dec_params.ref_double_buffer_enable;
  timeout_cycles = tb_cfg.dec_params.timeout_cycles;

  DEBUG_PRINT(
    ("Decoder Macro Block Error Concealment %d\n", mb_error_concealment));
  DEBUG_PRINT(("Decoder RLC %d\n", rlc_mode));
  DEBUG_PRINT(("Decoder Clock Gating %d\n", clock_gating));
  DEBUG_PRINT(("Decoder Clock Gating Runtime%d\n", clock_gating_runtime));
  DEBUG_PRINT(("Decoder Data Discard %d\n", data_discard));
  DEBUG_PRINT(("Decoder Latency Compensation %d\n", latency_comp));
  DEBUG_PRINT(("Decoder Output Picture Endian %d\n", output_picture_endian));
  DEBUG_PRINT(("Decoder Bus Burst Length %d\n", bus_burst_length));
  DEBUG_PRINT(("Decoder Asic Service Priority %d\n", asic_service_priority));
  DEBUG_PRINT(("Decoder Output Format %d\n", output_format));

  seed_rnd = tb_cfg.tb_params.seed_rnd;
  stream_header_corrupt = TBGetTBStreamHeaderCorrupt(&tb_cfg);
  /* if headers are to be corrupted
   * -> do not wait the picture to finalize before starting stream corruption */
  if (stream_header_corrupt) pic_rdy = 1;
  stream_truncate = TBGetTBStreamTruncate(&tb_cfg);
  if (strcmp(tb_cfg.tb_params.stream_bit_swap, "0") != 0) {
    stream_bit_swap = 1;
  } else {
    stream_bit_swap = 0;
  }
  if (strcmp(tb_cfg.tb_params.stream_packet_loss, "0") != 0) {
    stream_packet_loss = 1;
  } else {
    stream_packet_loss = 0;
  }

  packetize = packetize ? 1 : TBGetTBPacketByPacket(&tb_cfg);
  nal_unit_stream = TBGetTBNalUnitStream(&tb_cfg);
  DEBUG_PRINT(("TB Packet by Packet  %d\n", packetize));
  DEBUG_PRINT(("TB Nal Unit Stream %d\n", nal_unit_stream));
  DEBUG_PRINT(("TB Seed Rnd %d\n", seed_rnd));
  DEBUG_PRINT(("TB Stream Truncate %d\n", stream_truncate));
  DEBUG_PRINT(("TB Stream Header Corrupt %d\n", stream_header_corrupt));
  DEBUG_PRINT(("TB Stream Bit Swap %d; odds %s\n", stream_bit_swap,
               tb_cfg.tb_params.stream_bit_swap));
  DEBUG_PRINT(("TB Stream Packet Loss %d; odds %s\n", stream_packet_loss,
               tb_cfg.tb_params.stream_packet_loss));

  {
    remove("regdump.txt");
    remove("mbcontrol.hex");
    remove("intra4x4_modes.hex");
    remove("motion_vectors.hex");
    remove("rlc.hex");
    remove("picture_ctrl_dec.trc");
  }

  if (trace_target) tb_cfg.tb_params.extra_cu_ctrl_eof = 1;

#ifdef ASIC_TRACE_SUPPORT
  /* open tracefiles */
  tmp = OpenAsicTraceFiles();
  if (!tmp) {
    DEBUG_PRINT(("UNABLE TO OPEN TRACE FILE(S)\n"));
  }
#if 0
  if(nal_unit_stream)
    trace_hevc_decoding_tools.stream_mode.nal_unit_strm = 1;
  else
    trace_hevc_decoding_tools.stream_mode.byte_strm = 1;
#endif
#endif

  dwl_inst = DWLInit(&dwl_params);

  /* initialize decoder. If unsuccessful -> exit */
  START_SW_PERFORMANCE;
  {
    struct Vp9DecConfig dec_cfg;
    enum DecDpbFlags flags = 0;
    if (tiled_output) flags |= DEC_REF_FRM_TILED_DEFAULT;
    if (dpb_mode == DEC_DPB_INTERLACED_FIELD)
      flags |= DEC_DPB_ALLOW_FIELD_ORDERING;
    dec_cfg.use_video_freeze_concealment = TBGetDecIntraFreezeEnable(&tb_cfg);
    dec_cfg.num_frame_buffers = 6;
    dec_cfg.use_video_compressor = 0;
    dec_cfg.use_fetch_one_pic = 0;
    dec_cfg.use_ringbuffer = 0;
    dec_cfg.output_format = write_raster_out ? DEC_OUT_FRM_RASTER_SCAN : DEC_OUT_FRM_TILED_4X4;
    dec_cfg.pixel_format = DEC_OUT_PIXEL_DEFAULT;
    dec_cfg.dscale_cfg.down_scale_x = 1;
    dec_cfg.dscale_cfg.down_scale_y = 1;
    ret = Vp9DecInit(&dec_inst, dwl_inst, &dec_cfg);
  }
  END_SW_PERFORMANCE;
  if (ret != DEC_OK) {
    DEBUG_PRINT(("DECODER INITIALIZATION FAILED\n"));
    goto end;
  }

  if (use_extra_dpb_frms)
    Vp9DecUseExtraFrmBuffers(dec_inst, use_extra_dpb_frms);


  /*
  SetDecRegister(((struct DecContainer *) dec_inst)->vp9_regs,
  HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((struct DecContainer *) dec_inst)->vp9_regs,
  HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  SetDecRegister(((struct DecContainer *) dec_inst)->vp9_regs,
  HWIF_DEC_OUT_BYTESWAP,
                 output_picture_endian);
  SetDecRegister(((struct DecContainer *) dec_inst)->vp9_regs,
  HWIF_DEC_MAX_BURST,
                 bus_burst_length);
  */

  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_BUSWIDTH, bus_width);

  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_STRM_SWAP, strm_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_PIC_SWAP, pic_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_DIRMV_SWAP, dirmv_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_TAB0_SWAP, tab0_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_TAB1_SWAP, tab1_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_TAB2_SWAP, tab2_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_TAB3_SWAP, tab3_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_RSCAN_SWAP, rscan_swap);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_MAX_BURST, max_burst);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_REFER_DOUBLEBUFFER_E, double_ref_buffer);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_DEC_BUSWIDTH, bus_width);

  if (clock_gating) {
    SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                   HWIF_DEC_CLK_GATE_E, clock_gating);
    SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                   HWIF_DEC_CLK_GATE_IDLE_E, clock_gating_runtime);
  }
  /*
  SetDecRegister(((struct Vp9DecContainer *) dec_inst)->vp9_regs,
  HWIF_SERV_MERGE_DIS,
                 service_merge_disable);
  */

  /* APF disabled? */
  if (tb_cfg.dec_params.apf_disable != 0)
    SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                   HWIF_APF_DISABLE, tb_cfg.dec_params.apf_disable);

  /* APF threshold disabled? */
  if (!TBGetDecApfThresholdEnabled(&tb_cfg))
    SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                   HWIF_APF_THRESHOLD, 0);
  else if (tb_cfg.dec_params.apf_threshold_value != -1)
    SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                   HWIF_APF_THRESHOLD, tb_cfg.dec_params.apf_threshold_value);

  /* Set timeouts. Value of 0 implies use of hardware default. */
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_TIMEOUT_OVERRIDE_E, timeout_cycles ? 1 : 0);
  SetDecRegister(((struct Vp9DecContainer *)dec_inst)->vp9_regs,
                 HWIF_TIMEOUT_CYCLES, timeout_cycles);

  TBInitializeRandom(seed_rnd);

  /* check size of the input file -> length of the stream in bytes */
  fseek(finput, 0L, SEEK_END);
  strm_len = ftell(finput);
  rewind(finput);

  /* REMOVED: dec_input.skip_non_reference = skip_non_reference; */

  if (!long_stream) {
    /* If the decoder can not handle the whole stream at once, force
     * packet-by-packet mode */
    if (!force_whole_stream) {
      if (strm_len > DEC_X170_MAX_STREAM_G2) {
        packetize = 1;
      }
    } else {
      if (strm_len > DEC_X170_MAX_STREAM_G2) {
        packetize = 0;
        strm_len = DEC_X170_MAX_STREAM_G2;
      }
    }

    /* sets the stream length to random value */
    if (stream_truncate && !packetize && !nal_unit_stream) {
      DEBUG_PRINT(("strm_len %d\n", strm_len));
      ret = TBRandomizeU32(&strm_len);
      if (ret != 0) {
        DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
        goto end;
      }
      DEBUG_PRINT(("Randomized strm_len %d\n", strm_len));
    }

    /* NOTE: The struct DWL should not be used outside decoder SW.
     * here we call it because it is the easiest way to get
     * dynamically allocated linear memory
     * */

    /* allocate memory for stream buffer. if unsuccessful -> exit */
    if (DWLMallocLinear(dwl_inst, strm_len,
                        &stream_mem[0]) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
    if (DWLMallocLinear(dwl_inst, strm_len,
                        &stream_mem[1]) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }

    byte_strm_start = (u8 *)stream_mem[0].virtual_address;

    if (byte_strm_start == NULL) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }

    /* read input stream from file to buffer and close input file */
    fread(byte_strm_start, 1, strm_len, finput);

    /* initialize Vp9DecDecode() input structure */
    stream_stop = byte_strm_start + strm_len;
    dec_input.stream = byte_strm_start;
    dec_input.stream_bus_address = (addr_t)stream_mem[0].bus_address;

    dec_input.data_len = strm_len;
  }
#if 0
  else {
#ifndef ADS_PERFORMANCE_SIMULATION
    if (DWLMallocLinear(((struct Vp9DecContainer *)dec_inst)->dwl,
                        DEC_X170_MAX_STREAM_G2, &stream_mem) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#else
    stream_mem.virtual_address = malloc(DEC_X170_MAX_STREAM_G2);
    stream_mem.bus_address = (size_t)stream_mem.virtual_address;

    if (stream_mem.virtual_address == NULL) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#endif

    /* initialize Vp9DecDecode() input structure */
    dec_input.stream = (u8 *)stream_mem.virtual_address;
    dec_input.stream_bus_address = (u32)stream_mem.bus_address;
  }
#endif

  if (long_stream && !packetize && !nal_unit_stream) {
    if (use_index == 1) {
      u32 amount = 0;
      cur_index = 0;

      /* read index */
      fscanf(findex, "%llu", &next_index);

      {
        /* check if last index -> stream end */
        u32 byte = 0;
        fread(&byte, 2, 1, findex);
        if (feof(findex)) {
          DEBUG_PRINT(("STREAM WILL END\n"));
          stream_will_end = 1;
        } else {
          fseek(findex, -2, SEEK_CUR);
        }
      }

      amount = next_index - cur_index;
      cur_index = next_index;

      /* read first block */
      dec_input.data_len = fread(dec_input.stream, 1, amount, finput);
    } else {
      dec_input.data_len =
        fread(dec_input.stream, 1, DEC_X170_MAX_STREAM_G2, finput);
    }
    /*DEBUG_PRINT(("BUFFER READ\n")); */
    if (feof(finput)) {
      DEBUG_PRINT(("STREAM WILL END\n"));
      stream_will_end = 1;
    }
  } else {
    if (use_index) {
      if (!nal_unit_stream) fscanf(findex, "%llu", &cur_index);
    }

    /* get pointer to next packet and the size of packet
     * (for packetize or nal_unit_stream modes) */
    ptmpstream = dec_input.stream;
    /*
    if ((tmp = NextPacket((u8 **)(&dec_input.stream))) != 0) {
      dec_input.data_len = tmp;
      dec_input.stream_bus_address += (u32)(dec_input.stream - ptmpstream);
    }
    */
    u32 tmpsize = strm_len;
    dec_input.data_len = VpxRdrReadFrame(vpx_reader_inst, dec_input.stream, stream, &tmpsize, 0);
  }

  /* main decoding loop */
  do {
    save_flag = 1;
    /*DEBUG_PRINT(("dec_input.data_len %d\n", dec_input.data_len));
     * DEBUG_PRINT(("dec_input.stream %d\n", dec_input.stream)); */

    if (stream_truncate && pic_rdy && (hdrs_rdy || stream_header_corrupt) &&
        (long_stream || (!long_stream && (packetize || nal_unit_stream)))) {
      i32 ret;

      ret = TBRandomizeU32(&dec_input.data_len);
      if (ret != 0) {
        DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
        return 0;
      }
      DEBUG_PRINT(("Randomized stream size %d\n", dec_input.data_len));
    }

    /* If enabled, break the stream */
    if (stream_bit_swap) {
      if ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt) {
        /* Picture needs to be ready before corrupting next picture */
        if (pic_rdy && corrupted_bytes <= 0) {
          ret = TBRandomizeBitSwapInStream(dec_input.stream, dec_input.data_len,
                                           tb_cfg.tb_params.stream_bit_swap);
          if (ret != 0) {
            DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
            goto end;
          }

          corrupted_bytes = dec_input.data_len;
          DEBUG_PRINT(("corrupted_bytes %d\n", corrupted_bytes));
        }
      }
    }

    /* Picture ID is the picture number in decoding order */
    dec_input.pic_id = pic_decode_number;

    /* Process super frame. */
    if (frames == 0) {
      ParseSuperframeIndex(dec_input.stream, dec_input.data_len, sizes, &frames);
      if (frames) dec_input.data_len = sizes[0];
    } else {
      dec_input.data_len = sizes[frame_index];
    }


    /* call API function to perform decoding */
    START_SW_PERFORMANCE;
#if 0
    dec_input.buffer = dec_input.stream;
    dec_input.buffer_bus_address = dec_input.stream_bus_address;
    dec_input.buff_len = dec_input.data_len;
#else
    /* buffer related info should be aligned to 16 */
    dec_input.buffer = (u8 *)((addr_t)dec_input.stream & (~BUFFER_ALIGN_MASK));
    dec_input.buffer_bus_address = dec_input.stream_bus_address & (~BUFFER_ALIGN_MASK);
    dec_input.buff_len = dec_input.data_len + (dec_input.stream_bus_address & BUFFER_ALIGN_MASK);
#endif
    ret = Vp9DecDecode(dec_inst, &dec_input, &dec_output);
    END_SW_PERFORMANCE;
    print_decode_return(ret);
    switch (ret) {
    case DEC_STREAM_NOT_SUPPORTED: {
      DEBUG_PRINT(("ERROR: UNSUPPORTED STREAM!\n"));
      goto end;
    }
    case DEC_HDRS_RDY: {
      save_flag = 0;
      /* Set a flag to indicate that headers are ready */
      hdrs_rdy = 1;

      /* Stream headers were successfully decoded
       * -> stream information is available for query now */

      START_SW_PERFORMANCE;
      tmp = Vp9DecGetInfo(dec_inst, &dec_info);
      END_SW_PERFORMANCE;
      if (tmp != DEC_OK) {
        DEBUG_PRINT(("ERROR in getting stream info!\n"));
        goto end;
      }

      DEBUG_PRINT(
        ("Width %d Height %d\n", dec_info.frame_width, dec_info.frame_height));

      DEBUG_PRINT(("coded params: (%d, %d)\n",
                   dec_info.coded_width,
                   dec_info.coded_height));


      DEBUG_PRINT(("Pictures in DPB = %d\n", dec_info.pic_buff_size));

#ifdef USE_EXTERNAL_BUFFER
      if (dec_info.pic_buff_size != min_buffer_num ||
          (dec_info.frame_width * dec_info.frame_height > prev_width * prev_height)) {
        /* Reset buffers added and stop adding extra buffers when a new header comes. */
        ReleaseExtBuffers();
        num_buffers = 0;
        add_extra_flag = 0;
        extra_buffer_num = 0;
      }

      rv = Vp9DecGetBufferInfo(dec_inst, &hbuf);
      DEBUG_PRINT(("Vp9DecGetBufferInfo ret %d\n", rv));
      DEBUG_PRINT(("buf_to_free %p, next_buf_size %d, buf_num %d\n",
                   (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num));
#endif
      prev_width = dec_info.frame_width;
      prev_height = dec_info.frame_height;
      min_buffer_num = dec_info.pic_buff_size;
      //if (dec_info.pic_buff_size < 10)
      //  Vp9DecUseExtraFrmBuffers(dec_inst, 10 - dec_info.pic_buff_size);

      dpb_mode = dec_info.dpb_mode;
      /* Decoder output frame size in planar YUV 4:2:0 */
      pic_size = dec_info.frame_width * dec_info.frame_height;
      pic_size = (3 * pic_size) / 2;

      /* No data consumed when returning DEC_HDRS_RDY. */
      dec_output.data_left = dec_input.data_len;
      dec_output.strm_curr_pos = dec_input.stream;

      break;
    }
    case DEC_ADVANCED_TOOLS: {
      /* ASO/STREAM ERROR was noticed in the stream. The decoder has to
       * reallocate resources */
      assert(dec_output.data_left);
      /* we should have some data left */ /* Used to indicate that picture
                                             decoding needs to finalized
                                             prior to corrupting next
                                             picture */

      /* Used to indicate that picture decoding needs to finalized prior to
       * corrupting next picture
       * pic_rdy = 0; */
      break;
    }
    case DEC_PIC_DECODED:
      /* If enough pictures decoded -> force decoding to end
       * by setting that no more stream is available */
      if (pic_decode_number == max_num_pics) dec_input.data_len = 0;

      printf("DECODED PICTURE %d\n", pic_decode_number);
      /* Increment decoding number for every decoded picture */
      pic_decode_number++;

      if (!output_thread_run) {
        output_thread_run = 1;
        pthread_create(&output_thread, NULL, Vp9OutputThread, NULL);
      }

#ifdef USE_EXTERNAL_BUFFER
      if (use_extra_buffers && !add_buffer_thread_run) {
        add_buffer_thread_run = 1;
        pthread_create(&add_buffer_thread, NULL, AddBufferThread, NULL);
      }
#endif
      dec_output.data_left = 0;

    case DEC_PENDING_FLUSH:
      /* case DEC_FREEZED_PIC_RDY: */
      /* Picture is now ready */
      pic_rdy = 1;
      /* use function Vp9DecNextPicture() to obtain next picture
       * in display order. Function is called until no more images
       * are ready for display */

      retry = 0;
      break;

    case DEC_STRM_PROCESSED:
    case DEC_NONREF_PIC_SKIPPED:
    case DEC_STRM_ERROR: {
      /* Used to indicate that picture decoding needs to finalized prior to
       * corrupting next picture
       * pic_rdy = 0; */

      break;
    }
    case DEC_WAITING_FOR_BUFFER: {
#ifdef USE_EXTERNAL_BUFFER
      DEBUG_PRINT(("Waiting for frame buffers\n"));
      struct DWLLinearMem mem;

      rv = Vp9DecGetBufferInfo(dec_inst, &hbuf);
      DEBUG_PRINT(("Vp9DecGetBufferInfo ret %d\n", rv));
      DEBUG_PRINT(("buf_to_free %p, next_buf_size %d, buf_num %d\n",
                   (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num));

      /* For HEVC, all external buffers will be freed after new headers comes. */
      if (rv == DEC_WAITING_FOR_BUFFER && hbuf.buf_to_free.virtual_address) {
        DWLFreeLinear(dwl_inst, &hbuf.buf_to_free);
        u32 id = FindExtBufferIndex(hbuf.buf_to_free.virtual_address);
        ext_buffers[id].virtual_address = NULL;
        ext_buffers[id].bus_address = 0;
        if (id == num_buffers - 1)
          num_buffers--;
      }

      if(hbuf.next_buf_size) {
        /* Only add minimum required buffers at first. */
        //extra_buffer_num = hbuf.buf_num - min_buffer_num;
        buffer_size = hbuf.next_buf_size;
        for (int i=0; i<hbuf.buf_num; i++) {
          DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
          rv = Vp9DecAddBuffer(dec_inst, &mem);
          DEBUG_PRINT(("Vp9DecAddBuffer ret %d\n", rv));
          u32 id = FindEmptyIndex();
          ext_buffers[id] = mem;
          buffer_consumed[id] = 1;
          if (id >= num_buffers) num_buffers++;
        }
        /* Extra buffers are allowed when minimum required buffers have been added.*/
        //num_buffers = min_buffer_num;
        if (num_buffers >= min_buffer_num)
          add_extra_flag = 1;
      }
      /* No data consumed when returning DEC_WAITING_FOR_BUFFER. */
      dec_output.data_left = dec_input.data_len;
      dec_output.strm_curr_pos = dec_input.stream;
#endif
    }
    break;
    case DEC_OK:
      /* nothing to do, just call again */
      break;
    case DEC_HW_TIMEOUT:
      DEBUG_PRINT(("Timeout\n"));
      goto end;
    default:
      DEBUG_PRINT(("FATAL ERROR: %d\n", ret));
      goto end;
    }

    /* break out of do-while if max_num_pics reached (data_len set to 0) */
    if (dec_input.data_len == 0) break;

    if (long_stream && !packetize && !nal_unit_stream) {
      if (stream_will_end) {
        corrupted_bytes -= (dec_input.data_len - dec_output.data_left);
        dec_input.data_len = dec_output.data_left;
        dec_input.stream = dec_output.strm_curr_pos;
        dec_input.stream_bus_address = dec_output.strm_curr_bus_address;
      } else {
        if (use_index == 1) {
          if (dec_output.data_left) {
            dec_input.stream_bus_address +=
              (dec_output.strm_curr_pos - dec_input.stream);
            corrupted_bytes -= (dec_input.data_len - dec_output.data_left);
            dec_input.data_len = dec_output.data_left;
            dec_input.stream = dec_output.strm_curr_pos;
          } else {
            dec_input.stream_bus_address = (addr_t)stream_mem[0].bus_address;
            dec_input.stream = (u8 *)stream_mem[0].virtual_address;
            dec_input.data_len = fill_buffer(dec_input.stream);
          }
        } else {
          if (fseek(finput, -dec_output.data_left, SEEK_CUR) == -1) {
            DEBUG_PRINT(("SEEK FAILED\n"));
            dec_input.data_len = 0;
          } else {
            /* store file index */
            if (save_index && save_flag) {
              fprintf(findex, "%llu\n", ftello64(finput));
            }

            dec_input.data_len =
              fread(dec_input.stream, 1, DEC_X170_MAX_STREAM_G2, finput);
          }
        }

        if (feof(finput)) {
          DEBUG_PRINT(("STREAM WILL END\n"));
          stream_will_end = 1;
        }

        corrupted_bytes = 0;
      }
    } else {
      if (dec_output.data_left) {
        dec_input.stream_bus_address +=
          (dec_output.strm_curr_pos - dec_input.stream);
        corrupted_bytes -= (dec_input.data_len - dec_output.data_left);
        dec_input.data_len = dec_output.data_left;
        dec_input.stream = dec_output.strm_curr_pos;
      } else {
        if (frame_index+1 >= frames) {
          frames = 0;
          frame_index = 0;
          strm_mem_id = 1 - strm_mem_id;
          dec_input.stream_bus_address = (addr_t)stream_mem[strm_mem_id].bus_address;
          dec_input.stream = (u8 *)stream_mem[strm_mem_id].virtual_address;
          /*u32 stream_packet_loss_tmp = stream_packet_loss;
           *
           * if(!pic_rdy)
           * {
           * stream_packet_loss = 0;
           * } */
          /* Read a new frame from ivf file. */
          ptmpstream = dec_input.stream;
          u32 tmpsize = strm_len;
          dec_input.data_len = VpxRdrReadFrame(vpx_reader_inst, dec_input.stream, stream, &tmpsize, 0);
          if (dec_input.data_len == -1)
            dec_input.data_len = 0;   // end of file
        } else {
          /* Super frame: decode next frame in the super frame. */
          dec_input.stream += sizes[frame_index];
          dec_input.stream_bus_address += sizes[frame_index];
          frame_index++;
        }
      }
    }

    /* keep decoding until all data from input stream buffer consumed
     * and all the decoded/queued frames are ready */
  } while (dec_input.data_len > 0);

end:

  DEBUG_PRINT(("Decoding ended, flush the DPB\n"));

  Vp9DecEndOfStream(dec_inst);

  if (output_thread) pthread_join(output_thread, NULL);

  byte_strm_start = NULL;
  fflush(stdout);

  if (stream_mem[0].virtual_address != NULL) {
#ifndef ADS_PERFORMANCE_SIMULATION
    if (dec_inst)
      DWLFreeLinear(dwl_inst, &stream_mem[0]);
#else
    free(stream_mem[0].virtual_address);
#endif
  }
  if (stream_mem[1].virtual_address != NULL) {
#ifndef ADS_PERFORMANCE_SIMULATION
    if (dec_inst)
      DWLFreeLinear(dwl_inst, &stream_mem[1]);
#else
    free(stream_mem[1].virtual_address);
#endif
  }

#ifdef USE_EXTERNAL_BUFFER
  ReleaseExtBuffers();
#endif

  /* release decoder instance */
  START_SW_PERFORMANCE;
  Vp9DecRelease(dec_inst);
  END_SW_PERFORMANCE;
  DWLRelease(dwl_inst);

  if (md5) {
    unsigned char digest[16];

    MD5Final(digest, &md5_ctx);

    for (i = 0; i < sizeof digest; i++) {
      fprintf(foutput, "%02x", digest[i]);
    }
    fprintf(foutput, "  %s\n", out_file_name);
  }

  strm_len = 0;
  if (foutput) {
    fseek(foutput, 0L, SEEK_END);
    strm_len = (u32)ftell(foutput);
    fclose(foutput);
  }
  if (foutput2) fclose(foutput2);
  if (fchroma2) fclose(fchroma2);
  if (f_tiled_output) fclose(f_tiled_output);
  if (f_stream_trace) fclose(f_stream_trace);
  if (finput) fclose(finput);
  if (f_output16) fclose(f_output16);

  /* free allocated buffers */
  if (cropped_pic != NULL) free(cropped_pic);
  if (grey_chroma != NULL) free(grey_chroma);
  if (pic_big_endian) free(pic_big_endian);
  if (raster_scan != NULL) free(raster_scan);

  VpxRdrClose(vpx_reader_inst);

  DEBUG_PRINT(("Output file: %s\n", out_file_name));

  DEBUG_PRINT(("OUTPUT_SIZE %d\n", strm_len));

  FINALIZE_SW_PERFORMANCE;

  DEBUG_PRINT(("DECODING DONE\n"));

#ifdef ASIC_TRACE_SUPPORT
  TraceSequenceCtrl(hw_dec_pic_count, 0);
  /*TraceVp9DecodingTools();*/
  /* close trace files */
  CloseAsicTraceFiles();
#endif

  if (retry > NUM_RETRY) {
    return -1;
  }

  if (num_errors) {
    DEBUG_PRINT(("ERRORS FOUND %d %d\n", num_errors, pic_decode_number));
    /*return 1;*/
    return 0;
  }

  return 0;
}


/* Write picture pointed by data to file. Size of the picture in pixels is
 * indicated by pic_size. */
void WriteOutput(const char *filename, const char *filename_tiled, u8 *data,
                 u8 *data_ch, u32 pic_width, u32 pic_height, u32 frame_number,
                 u32 mono_chrome, u32 view, u32 tiled_mode,
                 u32 bit_depth_luma,
                 u32 bit_depth_chroma, u32 pic_stride) {

  FILE **fout;
  u32 pixel_width = 8;
  u32 bit_depth = 8;

  if (disable_output_writing != 0) {
    return;
  }

  fout = &foutput;
  /* foutput is global file pointer */
  if (*fout == NULL) {
    /* open output file for writing, can be disabled with define.
     * If file open fails -> exit */
    if (strcmp(filename, "none") != 0) {
      *fout = fopen(filename, "wb");
      if (*fout == NULL) {
        DEBUG_PRINT(("UNABLE TO OPEN OUTPUT FILE\n"));
        return;
      }
    }
  }

  if (bit_depth_luma != 8 || bit_depth_chroma != 8) {
    pixel_width = 10;
    bit_depth = 10;
  }

#if 0
  if(f_tiled_output == NULL && tiled_output) {
    /* open output file for writing, can be disabled with define.
     * If file open fails -> exit */
    if(strcmp(filename_tiled, "none") != 0) {
      f_tiled_output = fopen(filename_tiled, "wb");
      if(f_tiled_output == NULL) {
        DEBUG_PRINT(("UNABLE TO OPEN TILED OUTPUT FILE\n"));
        return;
      }
    }
  }
#endif

  /* Convert back to raster scan format if decoder outputs
   * tiled format */
  if (tiled_mode == DEC_OUT_FRM_TILED_4X4 && convert_tiled_output) {
    u32 eff_height = (pic_height + 15) & (~15);

    if (raster_scan_size != (pic_width * eff_height * 3 / 2 * pixel_width / 8)) {
      raster_scan_size = (pic_width * eff_height * 3 / 2 * pixel_width / 8);
      if (raster_scan != NULL) free(raster_scan);

      raster_scan = (u8 *)malloc(raster_scan_size);
      if (!raster_scan) {
        fprintf(stderr,
                "error allocating memory for tiled"
                "-->raster conversion!\n");
        return;
      }
    }

    TBTiledToRaster(1, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME, data,
                    raster_scan, pic_width, eff_height);
    if (!mono_chrome)
      TBTiledToRaster(1, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                      data + pic_width * eff_height,
                      raster_scan + pic_width * eff_height, pic_width,
                      eff_height / 2);
    data = raster_scan;
  } else {
    /* raster scan output always 16px multiple */
    pic_size = pic_stride * pic_height;
  }

#if 0
  if (crop_display || planar_output) {
    /* Cropped frame size in planar YUV 4:2:0 */
    size_t new_size;
    if (crop_display)
      new_size = crop->crop_out_width * crop->crop_out_height;
    else
      new_size = pic_width * pic_height;

    if (!mono_chrome) new_size = (3 * new_size) / 2;

    if (new_size > cropped_pic_size) {
      if (cropped_pic != NULL) free(cropped_pic);
      cropped_pic_size = new_size;
      cropped_pic = malloc(cropped_pic_size);
      if (cropped_pic == NULL) {
        DEBUG_PRINT(("ERROR in allocating cropped image!\n"));
        exit(1);
      }
    }
    if (crop_display) {
      if(CropPicture(cropped_pic, data,
                     pic_width, pic_height,
                     crop, mono_chrome, planar_output)) {
        DEBUG_PRINT(("ERROR in cropping!\n"));
        exit(100);
      }
    } else
      Semiplanar2Planar(data,
                        cropped_pic,
                        pic_width, pic_height);

    data = cropped_pic;
    pic_width = crop->crop_out_width;
    pic_height= crop->crop_out_height;
    pic_size = cropped_pic_size;
  }
#endif


  if (mono_chrome) {
    // allocate "neutral" chroma buffer
    if (grey_chroma_size != (pic_size / 2)) {
      if (grey_chroma != NULL) free(grey_chroma);

      grey_chroma = (u8 *)malloc(pic_size / 2);
      if (grey_chroma == NULL) {
        DEBUG_PRINT(("UNABLE TO ALLOCATE GREYSCALE CHROMA BUFFER\n"));
        return;
      }
      grey_chroma_size = pic_size / 2;
      memset(grey_chroma, 128, grey_chroma_size);
    }
  }

  if (*fout == NULL || data == NULL) {
    return;
  }

#ifndef ASIC_TRACE_SUPPORT
  if (output_picture_endian == DEC_X170_BIG_ENDIAN) {
    if (pic_big_endian_size != pic_size) {
      if (pic_big_endian != NULL) free(pic_big_endian);

      pic_big_endian = (u8 *)malloc(pic_size);
      if (pic_big_endian == NULL) {
        DEBUG_PRINT(("MALLOC FAILED @ %s %d", __FILE__, __LINE__));
        return;
      }

      pic_big_endian_size = pic_size;
    }

    memcpy(pic_big_endian, data, pic_size);
    TbChangeEndianess(pic_big_endian, pic_size);
    data = pic_big_endian;
  }
#endif

#ifdef MD5SUM
  TBWriteFrameMD5Sum(*fout, data, pic_size, frame_number);
#else
  if (md5) {
    MD5Update(&md5_ctx, data, pic_size);
    if (mono_chrome) {
      MD5Update(&md5_ctx, grey_chroma, grey_chroma_size);
    }
  } else {
#if 1
    fwrite(data, 1, pic_size, *fout);
    fwrite(data_ch, 1, pic_size/2, *fout);
    if (mono_chrome) {
      fwrite(grey_chroma, 1, grey_chroma_size, *fout);
    }
#else
    u32 nbytes = 0;
    while (nbytes < pic_size)
      nbytes += fwrite(data + nbytes, 1, pic_size - nbytes, *fout);
    nbytes = 0;
    while (nbytes < pic_size/2)
      nbytes += fwrite(data_ch + nbytes, 1, pic_size/2 - nbytes, *fout);
    if (mono_chrome) {
      fwrite(grey_chroma, 1, grey_chroma_size, *fout);
    }
    printf("DBG: write %d bytes: mono %d %d -> %d\n", pic_size, mono_chrome, grey_chroma_size, nbytes);
    {
      fflush(foutput);
      u32 strm_len = (u32)ftell(foutput);
      printf("DBG: Total %d bytes\n", strm_len);
    }
#endif
  }
#endif

}


/* Example implementation of Vp9DecTrace function. Prototype of this function
 * is given in Vp9DecApi.h. This implementation appends trace messages to file
 * named 'dec_api.trc'. */
void Vp9DecTrace(const char *string) {
  FILE *fp;

#if 0
  fp = fopen("dec_api.trc", "at");
#else
  fp = stderr;
#endif

  if (!fp) return;

  fprintf(fp, "%s", string);

  if (fp != stderr) fclose(fp);
}

static void print_decode_return(i32 retval) {

  DEBUG_PRINT(("TB: Vp9DecDecode returned: "));
  switch (retval) {

  case DEC_OK:
    DEBUG_PRINT(("DEC_OK\n"));
    break;
  case DEC_NONREF_PIC_SKIPPED:
    DEBUG_PRINT(("DEC_NONREF_PIC_SKIPPED\n"));
    break;
  case DEC_STRM_PROCESSED:
    DEBUG_PRINT(("DEC_STRM_PROCESSED\n"));
    break;
  case DEC_PIC_RDY:
    DEBUG_PRINT(("DEC_PIC_RDY\n"));
    break;
  case DEC_PIC_DECODED:
    DEBUG_PRINT(("DEC_PIC_DECODED\n"));
    break;
  case DEC_ADVANCED_TOOLS:
    DEBUG_PRINT(("DEC_ADVANCED_TOOLS\n"));
    break;
  case DEC_HDRS_RDY:
    DEBUG_PRINT(("DEC_HDRS_RDY\n"));
    break;
  case DEC_STREAM_NOT_SUPPORTED:
    DEBUG_PRINT(("DEC_STREAM_NOT_SUPPORTED\n"));
    break;
  case DEC_DWL_ERROR:
    DEBUG_PRINT(("DEC_DWL_ERROR\n"));
    break;
  case DEC_HW_TIMEOUT:
    DEBUG_PRINT(("DEC_HW_TIMEOUT\n"));
    break;
  default:
    DEBUG_PRINT(("Other %d\n", retval));
    break;
  }
}

u32 fill_buffer(u8 *stream) {
  u32 amount = 0;
  u32 data_len = 0;

  if (cur_index != ftell(finput)) {
    fseeko64(finput, cur_index, SEEK_SET);
  }

  /* read next index */
  fscanf(findex, "%llu", &next_index);
  amount = next_index - cur_index;
  cur_index = next_index;

  /* read data */
  data_len = fread(stream, 1, amount, finput);

  return data_len;
}

