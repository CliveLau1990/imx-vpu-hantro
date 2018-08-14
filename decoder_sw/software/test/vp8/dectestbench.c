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

#include "basetype.h"
#include "ivf.h"

#include "vp8decapi.h"
#include "dwl.h"
#include "trace.h"
#include <unistd.h>

#include "vp8hwd_container.h"

#include "vp8filereader.h"
#ifdef USE_EFENCE
#include "efence.h"
#endif

#include "regdrv_g1.h"
#include "tb_cfg.h"
#include "tb_tiled.h"

#ifdef PP_PIPELINE_ENABLED
#include "pptestbench.h"
#include "ppapi.h"
#endif

#include "md5.h"
#include "tb_md5.h"

#include "tb_sw_performance.h"
#include "tb_stream_corrupt.h"

#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#ifdef USE_EXTERNAL_BUFFER
#include <assert.h>
#endif

#include "vp8filereader.h"

struct TBCfg tb_cfg;
/* for tracing */
u32 b_frames=0;
#ifdef ASIC_TRACE_SUPPORT
extern u32 hw_dec_pic_count;
extern u32 g_hw_ver;
#endif

#ifdef VP8_EVALUATION
extern u32 g_hw_ver;
#endif

#define MAX_BUFFERS 32

u32 enable_frame_picture = 0;
u32 number_of_written_frames = 0;
u32 num_frame_buffers = 0;

/* SW/SW testing, read stream trace file */
FILE * f_stream_trace = NULL;

u32 disable_output_writing = 0;
u32 display_order = 0;
u32 clock_gating = DEC_X170_INTERNAL_CLOCK_GATING;
u32 data_discard = DEC_X170_DATA_DISCARD_ENABLE;
u32 latency_comp = DEC_X170_LATENCY_COMPENSATION;
u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
u32 bus_burst_length = DEC_X170_BUS_BURST_LENGTH;
u32 asic_service_priority = DEC_X170_ASIC_SERVICE_PRIORITY;
u32 output_format = DEC_X170_OUTPUT_FORMAT;
u32 service_merge_disable = DEC_X170_SERVICE_MERGE_DISABLE;
u32 planar_output = 0;
u32 height_crop = 0;
u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 convert_tiled_output = 0;

u32 use_peek_output = 0;
u32 snap_shot = 0;
u32 forced_slice_mode = 0;

u32 stream_packet_loss = 0;
u32 stream_truncate = 0;
u32 stream_header_corrupt = 0;
u32 stream_bit_swap = 0;
u32 hdrs_rdy = 0;
u32 pic_rdy = 0;

u32 frame_picture = 0;
u32 out_frame_width = 0;
u32 out_frame_height = 0;
u8 *p_frame_pic = NULL;
u32 include_strides = 0;
i32 extra_strm_buffer_bits = 0; /* For HW bug workaround */

u32 user_mem_alloc = 0;
u32 alternate_alloc_order = 0;
u32 interleaved_user_buffers = 0;
struct DWLLinearMem user_alloc_luma[MAX_BUFFERS];
struct DWLLinearMem user_alloc_chroma[MAX_BUFFERS];
VP8DecPictureBufferProperties pbp = { 0 };

char *pic_big_endian = NULL;
size_t pic_big_endian_size = 0;

void decsw_performance(void);

void writeRawFrame(FILE * fp, unsigned char *buffer,
                   unsigned char *buffer_c, int frame_width,
                   int frame_height, int crop_width, int crop_height, int planar,
                   int tiled_mode, u32 luma_stride, u32 chroma_stride, u32 md5sum );

void writeSlice(FILE * fp, VP8DecPicture *dec_pic);

void VP8DecTrace(const char *string) {
  printf("%s\n", string);
}

void FramePicture( u8 *p_in, u8 *p_ch, i32 in_width, i32 in_height,
                   i32 in_frame_width, i32 in_frame_height,
                   u8 *p_out, i32 out_width, i32 out_height,
                   u32 luma_stride, u32 chroma_stride );

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 pic_num, VP8DecInst decoder);
#endif
/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

#define DEBUG_PRINT(str) printf str

#define VP8_MAX_STREAM_SIZE  DEC_X170_MAX_STREAM>>1

#ifdef ADS_PERFORMANCE_SIMULATION
include_strides
volatile u32 tttttt = 0;

void trace_perf() {
  tttttt++;
}

#undef START_SW_PERFORMANCE
#undef END_SW_PERFORMANCE

#define START_SW_PERFORMANCE trace_perf();
#define END_SW_PERFORMANCE trace_perf();

#endif

VP8DecInst dec_inst;
#ifdef USE_EXTERNAL_BUFFER
const void *dwl_inst = NULL;
u32 use_extra_buffers = 0;
#ifdef USE_OUTPUT_RELEASE
u32 allocate_extra_buffers_in_output = 0;
#endif
u32 buffer_size;
u32 num_buffers;  /* external buffers allocated yet. */
u32 add_buffer_thread_run = 0;
pthread_t add_buffer_thread;
pthread_mutex_t ext_buffer_contro;
struct DWLLinearMem ext_buffers[MAX_BUFFERS];

u32 add_extra_flag = 0;
/* Fixme: this value should be set based on option "-d" when invoking testbench. */
u32 pp_enabled = 0;

u32 res_changed = 0;

static void *AddBufferThread(void *arg) {
  usleep(100000);
  while(add_buffer_thread_run) {
    pthread_mutex_lock(&ext_buffer_contro);
    if(add_extra_flag && (num_buffers < MAX_BUFFERS)) {
      struct DWLLinearMem mem;
      i32 dwl_ret;
      if (pp_enabled)
        dwl_ret = DWLMallocLinear(dwl_inst, buffer_size, &mem);
      else
        dwl_ret = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
      if(dwl_ret == DWL_OK) {
        VP8DecRet rv = VP8DecAddBuffer(dec_inst, &mem);
        if(rv != VP8DEC_OK && rv != VP8DEC_WAITING_FOR_BUFFER) {
          if (pp_enabled)
            DWLFreeLinear(dwl_inst, &mem);
          else
            DWLFreeRefFrm(dwl_inst, &mem);
        } else {
          ext_buffers[num_buffers++] = mem;
        }
      }
    }
    pthread_mutex_unlock(&ext_buffer_contro);
    sched_yield();
  }
  return NULL;
}

void ReleaseExtBuffers() {
  int i;
  pthread_mutex_lock(&ext_buffer_contro);
  for(i=0; i<num_buffers; i++) {
    DEBUG_PRINT(("Freeing buffer %p\n", ext_buffers[i].virtual_address));
    if (pp_enabled)
      DWLFreeLinear(dwl_inst, &ext_buffers[i]);
    else
      DWLFreeRefFrm(dwl_inst, &ext_buffers[i]);
    DWLmemset(&ext_buffers[i], 0, sizeof(ext_buffers[i]));
  }
  pthread_mutex_unlock(&ext_buffer_contro);
}
#endif

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
FILE *fout;
u32 md5sum = 0; /* flag to enable md5sum output */
u32 slice_mode = 0;
pthread_t output_thread;
pthread_t release_thread;
int output_thread_run = 0;

sem_t buf_release_sem;
VP8DecPicture buf_list[100];
u32 buf_status[100] = {0};
u32 list_pop_index = 0;
u32 list_push_index = 0;
u32 last_pic_flag = 0;

/* buf release thread entry point. */
static void* buf_release_thread(void* arg) {
  while(1) {
    /* Pop output buffer from buf_list and consume it */
    if(buf_status[list_pop_index]) {
      sem_wait(&buf_release_sem);
      VP8DecPictureConsumed(dec_inst, &buf_list[list_pop_index]);
      buf_status[list_pop_index] = 0;
      list_pop_index++;
      if(list_pop_index == 100)
        list_pop_index = 0;

#ifdef USE_EXTERNAL_BUFFER
      if(allocate_extra_buffers_in_output) {
        pthread_mutex_lock(&ext_buffer_contro);
        if(add_extra_flag && num_buffers < MAX_BUFFERS) {
          struct DWLLinearMem mem;
          i32 dwl_ret;
          if (pp_enabled)
            dwl_ret  = DWLMallocLinear(dwl_inst, buffer_size, &mem);
          else
            dwl_ret  = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
          if(dwl_ret == DWL_OK) {
            VP8DecRet rv = VP8DecAddBuffer(dec_inst, &mem);
            if(rv != VP8DEC_OK && rv != VP8DEC_WAITING_FOR_BUFFER) {
              if (pp_enabled)
                DWLFreeLinear(dwl_inst, &mem);
              else
                DWLFreeRefFrm(dwl_inst, &mem);
            } else {
              ext_buffers[num_buffers++] = mem;
            }
          }
        }
        pthread_mutex_unlock(&ext_buffer_contro);
      }
#endif
    }
    if(last_pic_flag && buf_status[list_pop_index] == 0)
      break;
    usleep(10000);
  }
  return NULL;
}

/* Output thread entry point. */
static void* vp8_output_thread(void* arg) {
  VP8DecPicture dec_picture;
  u32 pic_display_number = 1;
  u32 pic_size = 0;

#ifdef TESTBENCH_WATCHDOG
  /* fpga watchdog: 30 sec timer for frozen decoder */
  {
    struct itimerval t = {{30,0}, {30,0}};
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = watchdog1;
    sa.sa_flags |= SA_RESTART;  /* restart of system calls */
    sigaction(SIGALRM, &sa, NULL);

    setitimer(ITIMER_REAL, &t, NULL);
  }
#endif
  while(output_thread_run) {
    u8 *image_data;
    VP8DecRet ret;
    u32 tmp;
    ret = VP8DecNextPicture(dec_inst, &dec_picture, 0);
    if(ret == VP8DEC_PIC_RDY) {
      if(!use_peek_output) {
#ifndef PP_PIPELINE_ENABLED
        if (!frame_picture ||
            dec_picture.num_slice_rows ||
            (out_frame_width &&
             out_frame_width < dec_picture.frame_width) ||
            (out_frame_height &&
             out_frame_height < dec_picture.frame_height)) {
          if (dec_picture.num_slice_rows) {
            writeSlice(fout, &dec_picture);
          } else {
            writeRawFrame(fout,
                          (unsigned char *) dec_picture.p_output_frame,
                          (unsigned char *) dec_picture.p_output_frame_c,
                          dec_picture.frame_width, dec_picture.frame_height,
                          dec_picture.coded_width, dec_picture.coded_height,
                          planar_output,
                          dec_picture.output_format,
                          dec_picture.luma_stride,
                          dec_picture.chroma_stride, md5sum);
          }
        } else {
          if (!out_frame_width)
            out_frame_width = dec_picture.frame_width;
          if (!out_frame_height)
            out_frame_height = dec_picture.frame_height;
          if( p_frame_pic == NULL)
            p_frame_pic = (u8*)malloc(
                            out_frame_width * out_frame_height * 3/2 *
                            sizeof(u8));

          FramePicture( (u8*)dec_picture.p_output_frame,
                        (u8*)dec_picture.p_output_frame_c,
                        dec_picture.coded_width,
                        dec_picture.coded_height,
                        dec_picture.frame_width,
                        dec_picture.frame_height,
                        p_frame_pic, out_frame_width, out_frame_height,
                        dec_picture.luma_stride,
                        dec_picture.chroma_stride );
          writeRawFrame(fout,
                        p_frame_pic,
                        NULL,
                        out_frame_width, out_frame_height,
                        out_frame_width, out_frame_height, 1,
                        dec_picture.output_format,
                        out_frame_width,
                        out_frame_width, md5sum);
        }
#else
        HandlePpOutput(pic_display_number++, dec_inst);
#endif
        if(dec_picture.num_slice_rows)
          slice_mode = 1;
      }

      /* Push output buffer into buf_list and wait to be consumed */
      buf_list[list_push_index] = dec_picture;
      buf_status[list_push_index] = 1;
      list_push_index++;
      if(list_push_index == 100)
        list_push_index = 0;

      sem_post(&buf_release_sem);

      pic_display_number++;
    }

    else if(ret == VP8DEC_END_OF_STREAM) {
      last_pic_flag = 1;
#ifdef USE_EXTERNAL_BUFFER
      add_buffer_thread_run = 0;
#endif
      break;
    }
    usleep(10000);
  }
  return NULL;
}
#endif

/*------------------------------------------------------------------------------
    Local functions
------------------------------------------------------------------------------*/
static u32 FfReadFrame( reader_inst reader, const u8 *buffer,
                        u32 max_buffer_size, u32 *frame_size, u32 pedantic );

/*------------------------------------------------------------------------------

    main

        Main function

------------------------------------------------------------------------------*/
int main(int argc, char**argv) {
  reader_inst reader;
  u32 i, tmp;
  u32 size;
  u32 more_frames;
  u32 pedantic_reading = 1;
  u32 max_num_pics = 0;
  u32 pic_decode_number = 0;
  u32 pic_display_number = 0;
  struct DWLLinearMem stream_mem;
  DWLHwConfig hw_config;
  VP8DecRet ret;
  VP8DecRet tmpret;
  VP8DecFormat dec_format;
  VP8DecInput dec_input;
  VP8DecOutput dec_output;
  VP8DecPicture dec_picture;
  VP8DecInfo info;
#ifdef USE_EXTERNAL_BUFFER
  u32 prev_width = 0, prev_height = 0;
  u32 min_buffer_num = 0;
  u32 buffer_release_flag = 1;
  u32 id = 0;
  VP8DecBufferInfo hbuf;
  VP8DecRet rv;
  memset(ext_buffers, 0, sizeof(ext_buffers));
  pthread_mutex_init(&ext_buffer_contro, NULL);
  struct DWLInitParam dwl_init;
  dwl_init.client_type = DWL_CLIENT_TYPE_VP8_DEC;
#endif
  u32 webp_loop = 0;
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  u32 md5sum = 0; /* flag to enable md5sum output */
  FILE *fout;
#endif
  FILE *f_tbcfg;
  u32 seed_rnd = 0;
  char out_file_name[256] = "out.yuv";

#ifdef ASIC_TRACE_SUPPORT
  g_hw_ver = 10000; /* default to g1 mode */
#endif

#ifdef VP8_EVALUATION_G1
  g_hw_ver = 10000;
#endif

#ifndef EXPIRY_DATE
#define EXPIRY_DATE (u32)0xFFFFFFFF
#endif /* EXPIRY_DATE */

  /* expiry stuff */
  {
    u8 tm_buf[7];
    time_t sys_time;
    struct tm * tm;
    u32 tmp1;

    /* Check expiry date */
    time(&sys_time);
    tm = localtime(&sys_time);
    strftime(tm_buf, sizeof(tm_buf), "%y%m%d", tm);
    tmp1 = 1000000+atoi(tm_buf);
    if (tmp1 > (EXPIRY_DATE) && (EXPIRY_DATE) > 1 ) {
      fprintf(stderr,
              "EVALUATION PERIOD EXPIRED.\n"
              "Please contact On2 Sales.\n");
      return -1;
    }
  }

  stream_mem.virtual_address = NULL;
  stream_mem.bus_address = 0;
  stream_mem.mem_type = DWL_MEM_TYPE_CPU;
  dec_picture.num_slice_rows = 0;

#ifndef PP_PIPELINE_ENABLED
  if (argc < 2) {
    printf("Usage: %s [options] file.ivf\n", argv[0]);
    printf("\t-Nn forces decoding to stop after n pictures\n");
    printf("\t-Ooutfile write output to \"outfile\" (default out.yuv)\n");
    printf("\t--md5 Output frame based md5 checksum. No YUV output!\n");
    printf("\t-C display cropped image\n");
    printf("\t-P write planar output.\n");
    printf("\t-E use tiled reference frame format.\n");
    printf("\t-G convert tiled output pictures to raster scan\n");
    printf("\t-F Enable frame picture writing (filled black).\n");
    printf("\t-W Set frame picture width (default 1. frame width).\n");
    printf("\t-H Set frame picture height (default 1. frame height).\n");
    printf("\t-Bn to use n frame buffers in decoder\n");
    printf("\t-Z output pictures using VP8DecPeek() function\n");
    printf("\t-ln Set luma buffer stride\n");
    printf("\t-cn Set chroma buffer stride\n");
    printf("\t-X user allocates picture buffers\n");
    printf("\t-Xa same as above but alternate allocation order\n");
    printf("\t-I use interleaved frame buffers (requires stride mode and "\
           "user allocated buffers\n");
    printf("\t-R write uncropped output (if strides used)\n");
    printf("\t-xn Add n bytes of extra space after "\
           "stream buffer for decoder\n");
#ifdef USE_EXTERNAL_BUFFER
    printf("\t-A add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
    printf("\t-a add extra external buffer in ouput thread\n");
#endif
#endif

    return 0;
  }

  /* read command line arguments */
  for (i = 1; i < (u32)(argc-1); i++) {
    if (strncmp(argv[i], "-N", 2) == 0)
      max_num_pics = (u32)atoi(argv[i] + 2);
    else if (strncmp(argv[i], "-P", 2) == 0)
      planar_output = 1;
    else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if (strncmp(argv[i], "-G", 2) == 0)
      convert_tiled_output = 1;
    else if (strncmp(argv[i], "-C", 2) == 0)
      height_crop = 1;
    else if (strncmp(argv[i], "-F", 2) == 0)
      frame_picture = 1;
    else if (strncmp(argv[i], "-W", 2) == 0)
      out_frame_width = (u32)atoi(argv[i] + 2);
    else if (strncmp(argv[i], "-H", 2) == 0)
      out_frame_height = (u32)atoi(argv[i] + 2);
    else if (strncmp(argv[i], "-O", 2) == 0)
      strcpy(out_file_name, argv[i] + 2);
    else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
      if(num_frame_buffers > MAX_BUFFERS)
        num_frame_buffers = MAX_BUFFERS;
    } else if (strncmp(argv[i], "-Z", 2) == 0)
      use_peek_output = 1;
    else if (strncmp(argv[i], "-S", 2) == 0)
      snap_shot = 1;
    else if (strncmp(argv[i], "-x", 2) == 0)
      extra_strm_buffer_bits = atoi(argv[i] + 2);
    else if (strncmp(argv[i], "-l", 2) == 0) {
      pbp.luma_stride = (u32)atoi(argv[i] + 2);
    } else if (strncmp(argv[i], "-c", 2) == 0) {
      pbp.chroma_stride = (u32)atoi(argv[i] + 2);
    } else if (strncmp(argv[i], "-X", 2) == 0) {
#ifndef USE_EXTERNAL_BUFFER
      user_mem_alloc = 1;
      if( strncmp(argv[i], "-Xa", 3) == 0)
        alternate_alloc_order = 1;
#endif
    } else if (strncmp(argv[i], "-I", 2) == 0)
      interleaved_user_buffers = 1;
    else if (strncmp(argv[i], "-R", 2) == 0)
      include_strides = 1;
    else if(strcmp(argv[i], "--md5") == 0) {
      md5sum = 1;
    }
#ifdef USE_EXTERNAL_BUFFER
    else if(strcmp(argv[i], "-A") == 0) {
      use_extra_buffers = 1;
    }
#ifdef USE_OUTPUT_RELEASE
    else if(strcmp(argv[i], "-a") == 0) {
      use_extra_buffers = 0;
      allocate_extra_buffers_in_output = 1;
    }
#endif
#endif
    else {
      DEBUG_PRINT(("UNKNOWN PARAMETER: %s\n", argv[i]));
      return 1;
    }
  }

  /* For stride support always force frame_picture on; TODO check compatibility
   * issues */
  if(md5sum && (pbp.chroma_stride || pbp.luma_stride)) {
    frame_picture = 1;
  }

  reader = rdr_open(argv[argc-1]);
  if (reader == NULL) {
    fprintf(stderr, "Unable to open input file\n");
    exit(100);
  }

  fout = fopen(out_file_name, "wb");
  if (fout == NULL) {
    fprintf(stderr, "Unable to open output file\n");
    exit(100);
  }
#else
  if (argc < 3) {
    printf("Usage: %s [options] file.ivf\n", argv[0]);
    printf("\t-Bn to use n frame buffers in decoder\n");
    printf("\t-E use tiled reference frame format.\n");
    printf("\t-Nn forces decoding to stop after n pictures\n");
    printf("\t-X user allocates picture buffers\n");
    printf("\t-Xa same as above but alternate allocation order\n");
    printf("\t-I use interleaved frame buffers (requires stride mode and "\
           "user allocated buffers\n");
    printf("\t-xn Add n bytes of extra space after "\
           "stream buffer for decoder\n");
#ifdef USE_EXTERNAL_BUFFER
    printf("\t-A add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
    printf("\t-a add extra external buffer in output thread\n");
#endif
#endif
    return 0;
  }

  /* read command line arguments */
  for (i = 1; i < (u32)(argc-1); i++) {
    if (strncmp(argv[i], "-N", 2) == 0)
      max_num_pics = (u32)atoi(argv[i] + 2);
    else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if (strncmp(argv[i], "-x", 2) == 0)
      extra_strm_buffer_bits = atoi(argv[i] + 2);
    else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
      if(num_frame_buffers > MAX_BUFFERS)
        num_frame_buffers = MAX_BUFFERS;
    } else if (strncmp(argv[i], "-X", 2) == 0) {
      user_mem_alloc = 1;
      if( strncmp(argv[i], "-Xa", 3) == 0)
        alternate_alloc_order = 1;
    }
#ifdef USE_EXTERNAL_BUFFER
    else if(strcmp(argv[i], "-A") == 0) {
      use_extra_buffers = 1;
    }
#ifdef USE_OUTPUT_RELEASE
    else if(strcmp(argv[i], "-a") == 0) {
      use_extra_buffers = 0;
      allocate_extra_buffers_in_output = 1;
    }
#endif
#endif
  }

  reader = rdr_open(argv[argc-2]);
  if (reader == NULL) {
    fprintf(stderr, "Unable to open input file\n");
    exit(100);
  }
#endif

#ifdef ASIC_TRACE_SUPPORT
  tmp = openTraceFiles();
  if (!tmp) {
    DEBUG_PRINT(("UNABLE TO OPEN TRACE FILE(S)\n"));
  }
#endif

  /* set test bench configuration */
  TBSetDefaultCfg(&tb_cfg);
  f_tbcfg = fopen("tb.cfg", "r");
  if (f_tbcfg == NULL) {
    DEBUG_PRINT(("UNABLE TO OPEN TEST BENCH CONFIGURATION FILE: \"tb.cfg\"\n"));
    DEBUG_PRINT(("USING DEFAULT CONFIGURATION\n"));
  } else {
    fclose(f_tbcfg);
    if (TBParseConfig("tb.cfg", TBReadParam, &tb_cfg) == TB_FALSE)
      return -1;
    if (TBCheckCfg(&tb_cfg) != 0)
      return -1;
  }

  user_mem_alloc |= TBGetDecMemoryAllocation(&tb_cfg);
  clock_gating = TBGetDecClockGating(&tb_cfg);
  data_discard = TBGetDecDataDiscard(&tb_cfg);
  latency_comp = tb_cfg.dec_params.latency_compensation;
  output_picture_endian = TBGetDecOutputPictureEndian(&tb_cfg);
  bus_burst_length = tb_cfg.dec_params.bus_burst_length;
  asic_service_priority = tb_cfg.dec_params.asic_service_priority;
  output_format = TBGetDecOutputFormat(&tb_cfg);
  service_merge_disable = TBGetDecServiceMergeDisable(&tb_cfg);

  DEBUG_PRINT(("Decoder Clock Gating %d\n", clock_gating));
  DEBUG_PRINT(("Decoder Data Discard %d\n", data_discard));
  DEBUG_PRINT(("Decoder Latency Compensation %d\n", latency_comp));
  DEBUG_PRINT(("Decoder Output Picture Endian %d\n", output_picture_endian));
  DEBUG_PRINT(("Decoder Bus Burst Length %d\n", bus_burst_length));
  DEBUG_PRINT(("Decoder Asic Service Priority %d\n", asic_service_priority));
  DEBUG_PRINT(("Decoder Output Format %d\n", output_format));
  seed_rnd = tb_cfg.tb_params.seed_rnd;
  stream_header_corrupt = TBGetTBStreamHeaderCorrupt(&tb_cfg);
  /* if headers are to be corrupted
    -> do not wait the picture to readeralize before starting stream corruption */
  if (stream_header_corrupt)
    pic_rdy = 1;
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
  DEBUG_PRINT(("TB Seed Rnd %d\n", seed_rnd));
  DEBUG_PRINT(("TB Stream Truncate %d\n", stream_truncate));
  DEBUG_PRINT(("TB Stream Header Corrupt %d\n", stream_header_corrupt));
  DEBUG_PRINT(("TB Stream Bit Swap %d; odds %s\n",
               stream_bit_swap, tb_cfg.tb_params.stream_bit_swap));
  DEBUG_PRINT(("TB Stream Packet Loss %d; odds %s\n",
               stream_packet_loss, tb_cfg.tb_params.stream_packet_loss));

  INIT_SW_PERFORMANCE;

  {
    VP8DecApiVersion dec_api;
    VP8DecBuild dec_build;

    /* Print API version number */
    dec_api = VP8DecGetAPIVersion();
    dec_build = VP8DecGetBuild();
    DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_VP8_DEC);
    DEBUG_PRINT((
                  "\n8170 VP8 Decoder API v%d.%d - SW build: %d - HW build: %x\n",
                  dec_api.major, dec_api.minor, dec_build.sw_build, dec_build.hw_build));
    DEBUG_PRINT((
                  "HW Supports video decoding up to %d pixels,\n",
                  hw_config.max_dec_pic_width));

    DEBUG_PRINT((
                  "supported codecs: %s%s\n",
                  hw_config.vp7_support ? "VP-7 " : "",
                  hw_config.vp8_support ? "VP-8" : ""));

    if(hw_config.pp_support)
      DEBUG_PRINT((
                    "Maximum Post-processor output size %d pixels\n\n",
                    hw_config.max_pp_out_pic_width));
    else
      DEBUG_PRINT(("Post-Processor not supported\n\n"));
  }

  /* check format */
  switch (rdr_identify_format(reader)) {
  case BITSTREAM_VP7:
    dec_format = VP8DEC_VP7;
    break;
  case BITSTREAM_VP8:
    dec_format = VP8DEC_VP8;
    break;
  case BITSTREAM_WEBP:
    dec_format = VP8DEC_WEBP;
    pedantic_reading = 0;  /* With WebP we rely on non-pedantic reading
                                      mode for correct operation. */
    break;
  }
  if (snap_shot)
    dec_format = VP8DEC_WEBP;
  if (dec_format == VP8DEC_WEBP)
    snap_shot = 1;

#ifdef USE_EXTERNAL_BUFFER
  dwl_inst = DWLInit(&dwl_init);
  if(dwl_inst == NULL) {
    DEBUG_PRINT(("H264DecInit# ERROR: DWL Init failed\n"));
    goto end;
  }
#endif
  /* initialize decoder. If unsuccessful -> exit */
  decsw_performance();
  START_SW_PERFORMANCE;
  ret = VP8DecInit(&dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                   dwl_inst,
#endif
                   dec_format, TBGetDecErrorConcealment( &tb_cfg ),
                   num_frame_buffers, tiled_output, 0, 0 );
  END_SW_PERFORMANCE;
  decsw_performance();

  if (ret != VP8DEC_OK) {
    fprintf(stderr,"DECODER INITIALIZATION FAILED\n");
    goto end;
  }

  /* Set ref buffer test mode */
  ((VP8DecContainer_t *) dec_inst)->ref_buffer_ctrl.test_function = TBRefbuTestMode;

  TBSetRefbuMemModel( &tb_cfg,
                      ((VP8DecContainer_t *) dec_inst)->vp8_regs,
                      &((VP8DecContainer_t *) dec_inst)->ref_buffer_ctrl );

#ifdef PP_PIPELINE_ENABLED
  /* Initialize the post processor. If unsuccessful -> exit */

  if(pp_startup
      (argv[argc - 1], dec_inst,
       snap_shot ?  PP_PIPELINED_DEC_TYPE_WEBP : PP_PIPELINED_DEC_TYPE_VP8,
       &tb_cfg) != 0) {
    fprintf(stderr, "PP INITIALIZATION FAILED\n");
    goto end;
  }
  if(pp_update_config
      (dec_inst,
       snap_shot ? PP_PIPELINED_DEC_TYPE_WEBP : PP_PIPELINED_DEC_TYPE_VP8,
       &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
  }

  /* If unspecified at cmd line, use minimum # of buffers, otherwise
   * use specified amount. */
  if(num_frame_buffers == 0)
    pp_number_of_buffers(1);
  else
    pp_number_of_buffers(num_frame_buffers);
#endif

  SetDecRegister(((VP8DecContainer_t *) dec_inst)->vp8_regs, HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((VP8DecContainer_t *) dec_inst)->vp8_regs, HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  SetDecRegister(((VP8DecContainer_t *) dec_inst)->vp8_regs, HWIF_DEC_OUT_TILED_E,
                 output_format);
  SetDecRegister(((VP8DecContainer_t *) dec_inst)->vp8_regs, HWIF_DEC_OUT_ENDIAN,
                 output_picture_endian);
  SetDecRegister(((VP8DecContainer_t *) dec_inst)->vp8_regs, HWIF_DEC_MAX_BURST,
                 bus_burst_length);
  SetDecRegister(((VP8DecContainer_t *) dec_inst)->vp8_regs, HWIF_DEC_DATA_DISC_E,
                 data_discard);
  SetDecRegister(((VP8DecContainer_t *) dec_inst)->vp8_regs, HWIF_SERV_MERGE_DIS,
                 service_merge_disable);

  TBInitializeRandom(seed_rnd);

  /* Allocate stream memory */
  if(DWLMallocLinear(((VP8DecContainer_t *) dec_inst)->dwl,
                     VP8_MAX_STREAM_SIZE,
                     &stream_mem) != DWL_OK ) {
    fprintf(stderr,"UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n");
    return -1;
  }

  DWLmemset(&dec_input, 0, sizeof(dec_input));
  DWLmemset(&dec_output, 0, sizeof(dec_output));

  dec_input.slice_height = tb_cfg.dec_params.jpeg_mcus_slice;

  /* slice mode disabled if dec+pp */
#ifdef PP_PIPELINE_ENABLED
  dec_input.slice_height = 0;
#endif
  /* No slice mode for VP8 video */
  /*    if(!snap_shot)
          dec_input.slice_height = 0;*/

  /* Start decode loop */
  do {
    /* read next frame from file format */
    if (!webp_loop && !dec_output.data_left) {
      tmp = FfReadFrame( reader, (u8*)stream_mem.virtual_address,
                         VP8_MAX_STREAM_SIZE, &size, pedantic_reading );
      /* ugly hack for large webp streams. drop reserved stream buffer
       * and reallocate new with correct stream size. */
      if( tmp == HANTRO_NOK &&
          dec_format == VP8DEC_WEBP &&
          size + extra_strm_buffer_bits > VP8_MAX_STREAM_SIZE ) {
        DWLFreeLinear(((VP8DecContainer_t *) dec_inst)->dwl, &stream_mem);
        /* Allocate MORE stream memory */
        if(DWLMallocLinear(((VP8DecContainer_t *) dec_inst)->dwl,
                           size + extra_strm_buffer_bits,
                           &stream_mem) != DWL_OK ) {
          fprintf(stderr,"UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n");
          return -1;
        }
        /* try again */
        tmp = FfReadFrame( reader, (u8*)stream_mem.virtual_address,
                           size, &size, pedantic_reading );

      }
      more_frames = (tmp==HANTRO_OK) ? HANTRO_TRUE : HANTRO_FALSE;
    }
    if( (more_frames && size != (u32)-1) || dec_output.data_left) {
      /* Decode */
      if (!webp_loop && !dec_output.data_left) {
        dec_input.stream = (const u8*)stream_mem.virtual_address;
        dec_input.data_len = size + extra_strm_buffer_bits;
        dec_input.stream_bus_address = (addr_t)stream_mem.bus_address;
      }

      decsw_performance();
      START_SW_PERFORMANCE;
      do {
        ret = VP8DecDecode(dec_inst, &dec_input, &dec_output);
      } while(dec_input.data_len == 0 && ret == VP8DEC_NO_DECODING_BUFFER);
      END_SW_PERFORMANCE;
      decsw_performance();
      if (ret == VP8DEC_HDRS_RDY) {
#ifdef USE_EXTERNAL_BUFFER
        rv = VP8DecGetBufferInfo(dec_inst, &hbuf);
        DEBUG_PRINT(("VP8DecGetBufferInfo ret %d\n", rv));
        DEBUG_PRINT(("buf_to_free %p, next_buf_size %d, buf_num %d\n",
                     (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num));
#endif
        i32 mcu_in_row;
        i32 mcu_size_divider = 1;
        hdrs_rdy = 1;
        ret = VP8DecGetInfo(dec_inst, &info);
        mcu_in_row = (info.frame_width / 16);

#ifndef PP_PIPELINE_ENABLED
#ifdef ASIC_TRACE_SUPPORT
        /* Handle incorrect slice size for HW testing */
        if(dec_input.slice_height > (info.frame_height >> 4)) {
          dec_input.slice_height = (info.frame_height >> 4);
          printf("FIXED Decoder Slice MB Set %d\n", dec_input.slice_height);
        }
#endif /* ASIC_TRACE_SUPPORT */

#ifdef ASIC_TRACE_SUPPORT
        /* 8190 and over 16M ==> force to slice mode */
        if((dec_input.slice_height == 0) &&
            (snap_shot) &&
            ((info.frame_width * info.frame_height) >
             VP8DEC_MAX_PIXEL_AMOUNT)) {
          do {
            dec_input.slice_height++;
          } while(((dec_input.slice_height * (mcu_in_row / mcu_size_divider)) +
                   (mcu_in_row / mcu_size_divider)) <
                  VP8DEC_MAX_SLICE_SIZE);
          printf
          ("Force to slice mode (over 16M) ==> Decoder Slice MB Set %d\n",
           dec_input.slice_height);
          forced_slice_mode = 1;
        }
#else
        /* 8190 and over 16M ==> force to slice mode */
        if((dec_input.slice_height == 0) &&
            (snap_shot) &&
            ((info.frame_width * info.frame_height) >
             VP8DEC_MAX_PIXEL_AMOUNT)) {
          do {
            dec_input.slice_height++;
          } while(((dec_input.slice_height * (mcu_in_row / mcu_size_divider)) +
                   (mcu_in_row / mcu_size_divider)) <
                  VP8DEC_MAX_SLICE_SIZE);
          printf
          ("Force to slice mode (over 16M) ==> Decoder Slice MB Set %d\n",
           dec_input.slice_height);
          forced_slice_mode = 1;
        }
#endif /* ASIC_TRACE_SUPPORT */
#endif /* PP_PIPELINE_ENABLED */
        DEBUG_PRINT(("\nStream info:\n"));
        DEBUG_PRINT(("VP Version %d, Profile %d\n", info.vp_version, info.vp_profile));
        DEBUG_PRINT(("Frame size %dx%d\n", info.frame_width, info.frame_height));
        DEBUG_PRINT(("Coded size %dx%d\n", info.coded_width, info.coded_height));
        DEBUG_PRINT(("Scaled size %dx%d\n", info.scaled_width, info.scaled_height));
        DEBUG_PRINT(("Output format %s\n\n", info.output_format == VP8DEC_SEMIPLANAR_YUV420
                     ? "VP8DEC_SEMIPLANAR_YUV420" : "VP8DEC_TILED_YUV420"));

        if (user_mem_alloc) {
          u32 size_luma;
          u32 size_chroma;
          u32 rotation = 0;
          u32 cropping = 0;
          u32 slice_height = 0;
          u32 width_y, width_c;

          width_y = pbp.luma_stride ? pbp.luma_stride : info.frame_width;
          width_c = pbp.chroma_stride ? pbp.chroma_stride : info.frame_width;

          for( i = 0 ; i < MAX_BUFFERS ; ++i ) {
            if(user_alloc_luma[i].virtual_address)
              DWLFreeRefFrm(((VP8DecContainer_t *) dec_inst)->dwl, &user_alloc_luma[i]);
            if(user_alloc_chroma[i].virtual_address)
              DWLFreeRefFrm(((VP8DecContainer_t *) dec_inst)->dwl, &user_alloc_chroma[i]);
          }

          DEBUG_PRINT(("User allocated memory,width=%d,height=%d\n",
                       info.frame_width, info.frame_height));

          slice_height = ((VP8DecContainer_t *) dec_inst)->slice_height;
          if(forced_slice_mode)
            slice_height = dec_input.slice_height;
          size_luma = slice_height ?
                      (slice_height + 1) * 16 * width_y :
                      info.frame_height * width_y;

          size_chroma = slice_height ?
                        (slice_height + 1) * 8 * width_c :
                        info.frame_height * width_c / 2;

          if(dec_format == VP8DEC_WEBP) {

#ifdef PP_PIPELINE_ENABLED
            rotation = pp_rotation_used();
            cropping = pp_cropping_used();
            if (rotation || (cropping && (info.frame_width <= hw_config.max_dec_pic_width || info.frame_height < 4096)))
              printf("User allocated output memory");
            else
              size_luma = size_chroma = 2; /* ugly hack*/
#endif

            if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                size_luma, &user_alloc_luma[0]) != DWL_OK) {
              fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
              return -1;
            }
            if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                size_chroma, &user_alloc_chroma[0]) != DWL_OK) {
              fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
              return -1;
            }

            dec_input.p_pic_buffer_y = user_alloc_luma[0].virtual_address;
            dec_input.pic_buffer_bus_address_y = user_alloc_luma[0].bus_address;
            dec_input.p_pic_buffer_c = user_alloc_chroma[0].virtual_address;
            dec_input.pic_buffer_bus_address_c = user_alloc_chroma[0].bus_address;
            if(pbp.luma_stride || pbp.chroma_stride ) {
              if( VP8DecSetPictureBuffers( dec_inst, &pbp ) != VP8DEC_OK ) {
                fprintf(stderr, "ERROR IN SETUP OF CUSTOM FRAME STRIDES\n");
                return -1;
              }
            }
          } else { /* VP8 */
            u32 *p_pic_buffer_y[MAX_BUFFERS];
            u32 *p_pic_buffer_c[MAX_BUFFERS];
            addr_t pic_buffer_bus_address_y[MAX_BUFFERS];
            addr_t pic_buffer_bus_address_c[MAX_BUFFERS];
            pbp.num_buffers = num_frame_buffers;
            if( pbp.num_buffers < 5 )
              pbp.num_buffers = 5;

            /* Custom use case: interleaved buffers (strides must
             * meet strict requirements here). If met, only one or
             * two buffers will be allocated, into which all ref
             * pictures' data will be interleaved into. */
            if(interleaved_user_buffers) {
              u32 size_buffer;
              /* Mode 1: luma / chroma strides same; both can be interleaved */
              if( ((pbp.luma_stride == pbp.chroma_stride) ||
                   ((2*pbp.luma_stride) == pbp.chroma_stride)) &&
                  pbp.luma_stride >= info.frame_width*2*pbp.num_buffers) {
                DEBUG_PRINT(("Interleave mode 1: One buffer\n"));
                size_buffer = pbp.luma_stride * (info.frame_height+1);
                if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                    size_buffer, &user_alloc_luma[0]) != DWL_OK) {
                  fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
                  return -1;
                }

                for( i = 0 ; i < pbp.num_buffers ; ++i ) {
                  p_pic_buffer_y[i] = user_alloc_luma[0].virtual_address +
                                      (info.frame_width*2*i)/4;
                  pic_buffer_bus_address_y[i] = user_alloc_luma[0].bus_address +
                                                info.frame_width*2*i;
                  p_pic_buffer_c[i] = user_alloc_luma[0].virtual_address +
                                      (info.frame_width*(2*i+1))/4;
                  pic_buffer_bus_address_c[i] = user_alloc_luma[0].bus_address +
                                                info.frame_width*(2*i+1);
                }

              } else { /* Mode 2: separate buffers for luma and chroma */
                DEBUG_PRINT(("Interleave mode 2: Two buffers\n"));
                if( (pbp.luma_stride < info.frame_width*pbp.num_buffers) ||
                    (pbp.chroma_stride < info.frame_width*pbp.num_buffers)) {
                  fprintf(stderr, "CHROMA STRIDE LENGTH TOO SMALL FOR INTERLEAVED FRAME BUFFERS\n");
                  return -1;
                }

                size_buffer = pbp.luma_stride * (info.frame_height+1);
                if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                    size_buffer, &user_alloc_luma[0]) != DWL_OK) {
                  fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
                  return -1;
                }
                size_buffer = pbp.chroma_stride * (info.frame_height+1);
                if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                    size_buffer, &user_alloc_chroma[0]) != DWL_OK) {
                  fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
                  return -1;
                }
                for( i = 0 ; i < pbp.num_buffers ; ++i ) {
                  p_pic_buffer_y[i] = user_alloc_luma[0].virtual_address +
                                      (info.frame_width*i)/4;
                  pic_buffer_bus_address_y[i] = user_alloc_luma[0].bus_address +
                                                info.frame_width*i;
                  p_pic_buffer_c[i] = user_alloc_chroma[0].virtual_address +
                                      (info.frame_width*i)/4;
                  pic_buffer_bus_address_c[i] = user_alloc_chroma[0].bus_address +
                                                info.frame_width*i;
                }
              }
            } else { /* dedicated buffers */

              if(alternate_alloc_order) /* alloc all lumas first
                                                     * and only then chromas */
              {
                for( i = 0 ; i < pbp.num_buffers ; ++i ) {
                  if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                      size_luma, &user_alloc_luma[i]) != DWL_OK) {
                    fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
                    return -1;
                  }
                  p_pic_buffer_y[i] = user_alloc_luma[i].virtual_address;
                  pic_buffer_bus_address_y[i] = user_alloc_luma[i].bus_address;
                }
                for( i = 0 ; i < pbp.num_buffers ; ++i ) {
                  if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                      size_chroma, &user_alloc_chroma[i]) != DWL_OK) {
                    fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
                    return -1;
                  }
                  p_pic_buffer_c[i] = user_alloc_chroma[i].virtual_address;
                  pic_buffer_bus_address_c[i] = user_alloc_chroma[i].bus_address;
                }
              } else {

                for( i = 0 ; i < pbp.num_buffers ; ++i ) {
                  if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                      size_luma, &user_alloc_luma[i]) != DWL_OK) {
                    fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
                    return -1;
                  }
                  if (DWLMallocRefFrm(((VP8DecContainer_t *) dec_inst)->dwl,
                                      size_chroma, &user_alloc_chroma[i]) != DWL_OK) {
                    fprintf(stderr,"UNABLE TO ALLOCATE PICTURE MEMORY\n");
                    return -1;
                  }
                  p_pic_buffer_y[i] = user_alloc_luma[i].virtual_address;
                  pic_buffer_bus_address_y[i] = user_alloc_luma[i].bus_address;
                  p_pic_buffer_c[i] = user_alloc_chroma[i].virtual_address;
                  pic_buffer_bus_address_c[i] = user_alloc_chroma[i].bus_address;
                }
              }
            }
            pbp.p_pic_buffer_y = p_pic_buffer_y;
            pbp.pic_buffer_bus_address_y = pic_buffer_bus_address_y;
            pbp.p_pic_buffer_c = p_pic_buffer_c;
            pbp.pic_buffer_bus_address_c = pic_buffer_bus_address_c;

            if( VP8DecSetPictureBuffers( dec_inst, &pbp ) != VP8DEC_OK ) {
              fprintf(stderr, "ERROR IN SETUP OF CUSTOM FRAME BUFFERS\n");
              return -1;
            }
          }
        } else if( pbp.luma_stride || pbp.chroma_stride ) {
          if( VP8DecSetPictureBuffers( dec_inst, &pbp ) != VP8DEC_OK ) {
            fprintf(stderr, "ERROR IN SETUP OF CUSTOM FRAME STRIDES\n");
            return -1;
          }
        }
#ifdef USE_EXTERNAL_BUFFER
        if((info.pic_buff_size != min_buffer_num) ||
            (info.frame_width * info.frame_height != prev_width * prev_height)) {
          /* Reset buffers added and stop adding extra buffers when a new header comes. */
          if (pp_enabled)
            res_changed = 1;
          else {
            add_extra_flag = 0;
            ReleaseExtBuffers();
            buffer_release_flag = 1;
            num_buffers = 0;
          }
        }
        prev_width = info.frame_width;
        prev_height = info.frame_height;
        min_buffer_num = info.pic_buff_size;
#endif

        decsw_performance();
        START_SW_PERFORMANCE;

        ret = VP8DecDecode(dec_inst, &dec_input, &dec_output);
        END_SW_PERFORMANCE;
        decsw_performance();
        TBSetRefbuMemModel( &tb_cfg,
                            ((VP8DecContainer_t *) dec_inst)->vp8_regs,
                            &((VP8DecContainer_t *) dec_inst)->ref_buffer_ctrl );

      }
#ifdef USE_EXTERNAL_BUFFER
      if (ret == VP8DEC_WAITING_FOR_BUFFER) {
        DEBUG_PRINT(("Waiting for frame buffers\n"));
        struct DWLLinearMem mem;
        mem.mem_type = DWL_MEM_TYPE_CPU;

        rv = VP8DecGetBufferInfo(dec_inst, &hbuf);
        DEBUG_PRINT(("VP8DecGetBufferInfo ret %d\n", rv));
        DEBUG_PRINT(("buf_to_free %p, next_buf_size %d, buf_num %d\n",
                     (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num));

#if 0
        while (rv == VP8DEC_WAITING_FOR_BUFFER) {
          if (hbuf.buf_to_free.virtual_address != NULL && res_changed) {
            add_extra_flag = 0;
            ReleaseExtBuffers();
            buffer_release_flag = 1;
            num_buffers = 0;
            res_changed = 0;
          }
          rv = VP8DecGetBufferInfo(dec_inst, &hbuf);
        }
#endif

        buffer_size = hbuf.next_buf_size;
        if(buffer_release_flag && hbuf.next_buf_size) {
          /* Only add minimum required buffers at first. */
          //extra_buffer_num = hbuf.buf_num - min_buffer_num;
          for(i=0; i<hbuf.buf_num; i++) {
            if (pp_enabled)
              DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
            else
              DWLMallocRefFrm(dwl_inst, hbuf.next_buf_size, &mem);
            rv = VP8DecAddBuffer(dec_inst, &mem);
            DEBUG_PRINT(("VP8DecAddBuffer ret %d\n", rv));
            if(rv != VP8DEC_OK && rv != VP8DEC_WAITING_FOR_BUFFER) {
              if (pp_enabled)
                DWLFreeLinear(dwl_inst, &mem);
              else
                DWLFreeRefFrm(dwl_inst, &mem);
            } else {
              ext_buffers[i] = mem;
            }
          }
          /* Extra buffers are allowed when minimum required buffers have been added.*/
          num_buffers = hbuf.buf_num;
          add_extra_flag = 1;
        }
        ret = VP8DecDecode(dec_inst, &dec_input, &dec_output);
        END_SW_PERFORMANCE;
        decsw_performance();
        TBSetRefbuMemModel( &tb_cfg,
                            ((VP8DecContainer_t *) dec_inst)->vp8_regs,
                            &((VP8DecContainer_t *) dec_inst)->ref_buffer_ctrl );
      }
#endif
      else if (ret != VP8DEC_PIC_DECODED &&
               ret != VP8DEC_SLICE_RDY) {
        continue;
      }

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
      if (!output_thread_run) {
        output_thread_run = 1;
        sem_init(&buf_release_sem, 0, 0);
        pthread_create(&output_thread, NULL, vp8_output_thread, NULL);
        pthread_create(&release_thread, NULL, buf_release_thread, NULL);
      }
#endif

      webp_loop = (ret == VP8DEC_SLICE_RDY);

      pic_rdy = 1;

      decsw_performance();
      START_SW_PERFORMANCE;
#ifdef USE_EXTERNAL_BUFFER
      if(use_extra_buffers && !add_buffer_thread_run) {
        add_buffer_thread_run = 1;
        pthread_create(&add_buffer_thread, NULL, AddBufferThread, NULL);
      }
#endif

      if (use_peek_output &&
          VP8DecPeek(dec_inst, &dec_picture) == VP8DEC_PIC_RDY) {
        END_SW_PERFORMANCE;
        decsw_performance();

        if (!frame_picture ||
            (out_frame_width && out_frame_width < dec_picture.frame_width) ||
            (out_frame_height && out_frame_height < dec_picture.frame_height))
          writeRawFrame(fout,
                        (unsigned char *) dec_picture.p_output_frame,
                        (unsigned char *) dec_picture.p_output_frame_c,
                        dec_picture.frame_width, dec_picture.frame_height,
                        dec_picture.coded_width, dec_picture.coded_height,
                        planar_output,
                        dec_picture.output_format,
                        dec_picture.luma_stride,
                        dec_picture.chroma_stride, md5sum);
        else {
          if (!out_frame_width)
            out_frame_width = dec_picture.frame_width;
          if (!out_frame_height)
            out_frame_height = dec_picture.frame_height;
          if( p_frame_pic == NULL)
            p_frame_pic = (u8*)malloc(
                            out_frame_width * out_frame_height * 3/2 * sizeof(u8));

          FramePicture( (u8*)dec_picture.p_output_frame,
                        (u8*)dec_picture.p_output_frame_c,
                        dec_picture.coded_width,
                        dec_picture.coded_height,
                        dec_picture.frame_width,
                        dec_picture.frame_height,
                        p_frame_pic, out_frame_width, out_frame_height,
                        dec_picture.luma_stride,
                        dec_picture.chroma_stride );
          writeRawFrame(fout,
                        p_frame_pic,
                        NULL,
                        out_frame_width, out_frame_height,
                        out_frame_width, out_frame_height, 1,
                        dec_picture.output_format,
                        out_frame_width,
                        out_frame_width, md5sum);
        }

        decsw_performance();
        START_SW_PERFORMANCE;

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
        while (VP8DecNextPicture(dec_inst, &dec_picture, 0) ==
               VP8DEC_PIC_RDY);
#endif

        END_SW_PERFORMANCE;
        decsw_performance();

      }
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      else if(!use_peek_output) {
        while (VP8DecNextPicture(dec_inst, &dec_picture, 0) ==
               VP8DEC_PIC_RDY) {
          END_SW_PERFORMANCE;
          decsw_performance();

#ifndef PP_PIPELINE_ENABLED
          if (!frame_picture ||
              dec_picture.num_slice_rows ||
              (out_frame_width &&
               out_frame_width < dec_picture.frame_width) ||
              (out_frame_height &&
               out_frame_height < dec_picture.frame_height)) {
            if (dec_picture.num_slice_rows) {
              writeSlice(fout, &dec_picture);
            } else {
              writeRawFrame(fout,
                            (unsigned char *) dec_picture.p_output_frame,
                            (unsigned char *) dec_picture.p_output_frame_c,
                            dec_picture.frame_width, dec_picture.frame_height,
                            dec_picture.coded_width, dec_picture.coded_height,
                            planar_output,
                            dec_picture.output_format,
                            dec_picture.luma_stride,
                            dec_picture.chroma_stride, md5sum);
            }
          } else {
            if (!out_frame_width)
              out_frame_width = dec_picture.frame_width;
            if (!out_frame_height)
              out_frame_height = dec_picture.frame_height;
            if( p_frame_pic == NULL)
              p_frame_pic = (u8*)malloc(
                              out_frame_width * out_frame_height * 3/2 *
                              sizeof(u8));

            FramePicture( (u8*)dec_picture.p_output_frame,
                          (u8*)dec_picture.p_output_frame_c,
                          dec_picture.coded_width,
                          dec_picture.coded_height,
                          dec_picture.frame_width,
                          dec_picture.frame_height,
                          p_frame_pic, out_frame_width, out_frame_height,
                          dec_picture.luma_stride,
                          dec_picture.chroma_stride );
            writeRawFrame(fout,
                          p_frame_pic,
                          NULL,
                          out_frame_width, out_frame_height,
                          out_frame_width, out_frame_height, 1,
                          dec_picture.output_format,
                          out_frame_width,
                          out_frame_width, md5sum);
          }
#else
          HandlePpOutput(pic_display_number++, dec_inst);
#endif
        }
      }
#endif
      END_SW_PERFORMANCE;
      decsw_performance();
    }
    if (ret != VP8DEC_SLICE_RDY)
      pic_decode_number++;
#if 0
    if (pic_decode_number == 10) {
      VP8DecRet tmp_ret = VP8DecAbort(dec_inst);
      tmp_ret = VP8DecAbortAfter(dec_inst);
      if(reader)
        rdr_close(reader);
      reader = rdr_open(argv[argc-1]);
    }
#endif
  } while( more_frames && (pic_decode_number != max_num_pics || !max_num_pics) );

  decsw_performance();
  START_SW_PERFORMANCE;

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  while (!use_peek_output &&
         VP8DecNextPicture(dec_inst, &dec_picture, 1) == VP8DEC_PIC_RDY) {

    END_SW_PERFORMANCE;
    decsw_performance();

#ifndef PP_PIPELINE_ENABLED
    writeRawFrame(fout,
                  (unsigned char *) dec_picture.p_output_frame,
                  (unsigned char *) dec_picture.p_output_frame_c,
                  dec_picture.frame_width, dec_picture.frame_height,
                  dec_picture.coded_width, dec_picture.coded_height,
                  planar_output,
                  dec_picture.output_format,
                  dec_picture.luma_stride,
                  dec_picture.chroma_stride,
                  md5sum);
#else
    HandlePpOutput(pic_display_number++, dec_inst);
#endif
  }
#else
  VP8DecEndOfStream(dec_inst, 1);
#endif

end:
#ifdef USE_EXTERNAL_BUFFER
  add_buffer_thread_run = 0;
#endif

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
  if(output_thread)
    pthread_join(output_thread, NULL);
  if(release_thread)
    pthread_join(release_thread, NULL);

#endif

  if(stream_mem.virtual_address)
    DWLFreeLinear(((VP8DecContainer_t *) dec_inst)->dwl, &stream_mem);

#ifdef PP_PIPELINE_ENABLED
  pp_close();
#endif

  if (user_mem_alloc) {
    for( i = 0 ; i < MAX_BUFFERS ; ++i ) {
      if(user_alloc_luma[i].virtual_address)
        DWLFreeRefFrm(((VP8DecContainer_t *) dec_inst)->dwl, &user_alloc_luma[i]);
      if(user_alloc_chroma[i].virtual_address)
        DWLFreeRefFrm(((VP8DecContainer_t *) dec_inst)->dwl, &user_alloc_chroma[i]);
    }
  }

  decsw_performance();
  START_SW_PERFORMANCE;
  VP8DecRelease(dec_inst);
  END_SW_PERFORMANCE;
  decsw_performance();
#ifdef USE_EXTERNAL_BUFFER
  ReleaseExtBuffers();
  pthread_mutex_destroy(&ext_buffer_contro);
  DWLRelease(dwl_inst);
#endif

#ifdef ASIC_TRACE_SUPPORT
  trace_SequenceCtrl(hw_dec_pic_count, 0);
  trace_RefbufferHitrate();
  /*trace_AVSDecodingTools();*/
  closeTraceFiles();
#endif

  if(reader)
    rdr_close(reader);
#ifndef PP_PIPELINE_ENABLED
  if(fout)
    fclose(fout);
#endif /* PP_PIPELINE_ENABLED */

  if(p_frame_pic)
    free(p_frame_pic);

  if (pic_big_endian)
    free(pic_big_endian);

  if(md5sum) {
    /* slice mode output is semiplanar yuv, convert to md5sum */
#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
    if (slice_mode)
#else
    if (dec_picture.num_slice_rows)
#endif
    {
      char cmd[256];
      sprintf(cmd, "md5sum %s | tr 'a-z' 'A-Z' | sed 's/ .*//' > .md5sum.txt", out_file_name);
      ret = system(cmd);
      sprintf(cmd, "mv .md5sum.txt %s", out_file_name);
      ret = system(cmd);
    }
  }

  return 0;
}

/*------------------------------------------------------------------------------

    Function name: decsw_performance

    Functional description: breakpoint for performance

    Inputs:  void

    Outputs:
chromaBufOff
    Returns: void

------------------------------------------------------------------------------*/
void decsw_performance(void) {
}

void writeRawFrame(FILE * fp, unsigned char *buffer,
                   unsigned char *buffer_ch, int frame_width,
                   int frame_height, int cropped_width, int cropped_height, int planar,
                   int tiled_mode, u32 luma_stride, u32 chroma_stride, u32 md5sum) {

  int luma_size = luma_stride * frame_height;
  int frame_size = luma_size + (chroma_stride*frame_height/2);
  u8 *raster_scan = NULL;
  static int pic_number = 0;
  static struct MD5Context ctx;
  unsigned char digest[16];
  int i = 0;
  unsigned char *cb,*cr;
  DEBUG_PRINT(("WRITING PICTURE %d\n", pic_number++));

  /* Convert back to raster scan format if decoder outputs
   * tiled format */
  if(tiled_mode && convert_tiled_output) {
    raster_scan = (u8*)malloc(frame_size);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBTiledToRaster( tiled_mode, DEC_DPB_FRAME, buffer,
                     raster_scan, frame_width, frame_height );
    buffer = raster_scan;

    TBTiledToRaster( tiled_mode, DEC_DPB_FRAME, buffer_ch,
                     raster_scan+luma_size, frame_width, frame_height/2 );
    buffer_ch = raster_scan+luma_size;
  }

#ifndef ASIC_TRACE_SUPPORT
  if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
    if(pic_big_endian_size != frame_size) {
      if(pic_big_endian != NULL)
        free(pic_big_endian);

      pic_big_endian = (u8 *) malloc(frame_size);
      if(pic_big_endian == NULL) {
        DEBUG_PRINT(("MALLOC FAILED @ %s %d", __FILE__, __LINE__));
        if(raster_scan)
          free(raster_scan);
        return;
      }

      pic_big_endian_size = frame_size;
    }

    memcpy(pic_big_endian, buffer, frame_size);
    TbChangeEndianess(pic_big_endian, frame_size);
    buffer = pic_big_endian;
  }
#endif
  if(md5sum) {
    /* chroma should be right after luma */
    if (!user_mem_alloc) {
      MD5Init(&ctx);
      MD5Update(&ctx, buffer, frame_size);
      MD5Final(digest, &ctx);

      for(i = 0; i < sizeof digest; i++) {
        fprintf(fp, "%02X", digest[i]);
      }
      fprintf(fp, "\n");

      if(raster_scan)
        free(raster_scan);

      return;
    }
  }

  if (buffer_ch == NULL) {
    buffer_ch = buffer + luma_size;
  }

  if (!height_crop || (cropped_height == frame_height && cropped_width == frame_width)) {
    u32 i, j;
    u8 *buffer_tmp;
    buffer_tmp = buffer;

    for( i = 0 ; i < frame_height ; ++i ) {
      fwrite(buffer_tmp, include_strides ? luma_stride : frame_width, 1, fp );
      buffer_tmp += luma_stride;
    }

    /*fwrite(buffer, luma_size, 1, fp);*/
    if (!planar) {
      buffer_tmp = buffer_ch;
      for( i = 0 ; i < frame_height / 2 ; ++i ) {
        fwrite( buffer_tmp, include_strides ? chroma_stride : frame_width, 1, fp );
        buffer_tmp += chroma_stride;
      }
    } else {
      buffer_tmp = buffer_ch;
      for(i = 0; i < frame_height / 2; i++) {
        for( j = 0 ; j < (include_strides ? chroma_stride / 2 : frame_width / 2); ++j) {
          fwrite(buffer_tmp + j * 2, 1, 1, fp);
        }
        buffer_tmp += chroma_stride;
      }
      buffer_tmp = buffer_ch + 1;
      for(i = 0; i < frame_height / 2; i++) {
        for( j = 0 ; j < (include_strides ? chroma_stride / 2: frame_width / 2); ++j) {
          fwrite(buffer_tmp + j * 2, 1, 1, fp);
        }
        buffer_tmp += chroma_stride;
      }
    }
  } else {
    u32 row;
    for( row = 0 ; row < cropped_height ; row++) {
      fwrite(buffer + row*luma_stride, cropped_width, 1, fp);
    }
    if (!planar) {
      if(cropped_height &1)
        cropped_height++;
      if(cropped_width & 1)
        cropped_width++;
      for( row = 0 ; row < cropped_height/2 ; row++)
        fwrite(buffer_ch + row*chroma_stride, (cropped_width*2)/2, 1, fp);
    } else {
      u32 i, tmp;
      tmp = frame_width*cropped_height/4;

      if(cropped_height &1)
        cropped_height++;
      if(cropped_width & 1)
        cropped_width++;

      for( row = 0 ; row < cropped_height/2 ; ++row ) {
        for(i = 0; i < cropped_width/2; i++)
          fwrite(buffer_ch + row*chroma_stride + i * 2, 1, 1, fp);
      }
      for( row = 0 ; row < cropped_height/2 ; ++row ) {
        for(i = 0; i < cropped_width/2; i++)
          fwrite(buffer_ch + 1 + row*chroma_stride + i * 2, 1, 1, fp);
      }
    }
  }

  if(raster_scan)
    free(raster_scan);

}

void FramePicture( u8 *p_in, u8* p_ch, i32 in_width, i32 in_height,
                   i32 in_frame_width, i32 in_frame_height,
                   u8 *p_out, i32 out_width, i32 out_height,
                   u32 luma_stride, u32 chroma_stride ) {

  /* Variables */

  i32 x, y;

  /* Code */

  memset( p_out, 0, out_width*out_height );
  memset( p_out+out_width*out_height, 128, out_width*out_height/2 );

  /* Luma */
  for ( y = 0 ; y < in_height ; ++y ) {
    for( x = 0 ; x < in_width; ++x )
      *p_out++ = *p_in++;
    p_in += ( luma_stride - in_width );
    p_out += ( out_width - in_width );
  }

  p_out += out_width * ( out_height - in_height );
  if(p_ch)
    p_in = p_ch;
  else
    p_in += luma_stride * ( in_frame_height - in_height );

  in_frame_height /= 2;
  in_frame_width /= 2;
  out_height /= 2;
  out_width /= 2;
  in_height /= 2;
  in_width /= 2;

  /* Chroma */
  for ( y = 0 ; y < in_height ; ++y ) {
    for( x = 0 ; x < 2*in_width; ++x )
      *p_out++ = *p_in++;
    p_in += 2 * ( (chroma_stride/2) - in_width );
    p_out += 2 * ( out_width - in_width );
  }

}

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 pic_num, VP8DecInst decoder) {
  PPResult res;

  res = pp_check_combined_status();

  if(res == PP_OK) {
    pp_write_output(pic_num, 0, 0);
    pp_read_blend_components(((VP8DecContainer_t *) decoder)->pp.pp_instance);
  }
  if(pp_update_config
      (decoder,
       snap_shot ?  PP_PIPELINED_DEC_TYPE_WEBP : PP_PIPELINED_DEC_TYPE_VP8,
       &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
  }

}
#endif


void writeSlice(FILE * fp, VP8DecPicture *dec_pic) {

  int luma_size = include_strides ? dec_pic->luma_stride * dec_pic->frame_height :
                  dec_pic->frame_width * dec_pic->frame_height;
  int slice_size = include_strides ? dec_pic->luma_stride * dec_pic->num_slice_rows :
                   dec_pic->frame_width * dec_pic->num_slice_rows;
  int chroma_size = include_strides ? dec_pic->chroma_stride * dec_pic->frame_height / 2:
                    dec_pic->frame_width * dec_pic->frame_height / 2;
  static u8 *tmp_ch = NULL;
  static u32 row_count = 0;
  u32 slice_rows;
  u32 luma_stride, chroma_stride;
  u32 i, j;
  u8 *ptr = (u8*)dec_pic->p_output_frame;

  if (tmp_ch == NULL) {
    tmp_ch = (u8*)malloc(chroma_size);
  }

  slice_rows = dec_pic->num_slice_rows;

  DEBUG_PRINT(("WRITING SLICE, rows %d\n",dec_pic->num_slice_rows));

  luma_stride = dec_pic->luma_stride;
  chroma_stride = dec_pic->chroma_stride;

  if(!height_crop) {
    /*fwrite(dec_pic->p_output_frame, 1, slice_size, fp);*/
    for ( i = 0 ; i < dec_pic->num_slice_rows ; ++i ) {
      fwrite(ptr, 1, include_strides ? luma_stride : dec_pic->coded_width, fp );
      ptr += luma_stride;
    }
  } else {
    if(row_count + slice_rows > dec_pic->coded_height )
      slice_rows -=
        (dec_pic->frame_height - dec_pic->coded_height);
    for ( i = 0 ; i < slice_rows ; ++i ) {
      fwrite(ptr, 1, include_strides ? luma_stride : dec_pic->coded_width, fp );
      ptr += luma_stride;
    }
  }

  for( i = 0 ; i < dec_pic->num_slice_rows/2 ; ++i ) {
    memcpy(tmp_ch + ((i+(row_count/2)) * (include_strides ? chroma_stride : dec_pic->frame_width)),
           dec_pic->p_output_frame_c + (i*chroma_stride)/4,
           include_strides ? chroma_stride : dec_pic->frame_width );
  }
  /*    memcpy(tmp_ch + row_count/2 * dec_pic->frame_width, dec_pic->p_output_frame_c,
          slice_size/2);*/

  row_count += dec_pic->num_slice_rows;

  if (row_count == dec_pic->frame_height) {
    if(!height_crop) {
      if (!planar_output) {
        fwrite(tmp_ch, luma_size/2, 1, fp);
        /*
        ptr = tmp_ch;
        for ( i = 0 ; i < dec_pic->num_slice_rows/2 ; ++i )
        {
            fwrite(ptr, 1, dec_pic->coded_width, fp );
            ptr += chroma_stride;
        } */
      } else {
        u32 i, tmp;
        tmp = chroma_size / 2;
        for(i = 0; i < tmp; i++)
          fwrite(tmp_ch + i * 2, 1, 1, fp);
        for(i = 0; i < tmp; i++)
          fwrite(tmp_ch + 1 + i * 2, 1, 1, fp);
      }
    } else {
      if(!planar_output) {
        ptr = tmp_ch;
        for ( i = 0 ; i < dec_pic->coded_height/2 ; ++i ) {
          fwrite(ptr, 1, dec_pic->coded_width, fp );
          ptr += dec_pic->frame_width;
        }
      } else {
        ptr = tmp_ch;
        for ( i = 0 ; i < dec_pic->coded_height/2 ; ++i ) {
          for( j = 0 ; j < dec_pic->coded_width/2 ; ++j )
            fwrite(ptr + 2*j, 1, 1, fp );
          ptr += dec_pic->frame_width;
        }
        ptr = tmp_ch+1;
        for ( i = 0 ; i < dec_pic->coded_height/2 ; ++i ) {
          for( j = 0 ; j < dec_pic->coded_width/2 ; ++j )
            fwrite(ptr + 2*j, 1, 1, fp );
          ptr += dec_pic->frame_width;
        }
      }
    }
  }

}

u32 FfReadFrame( reader_inst reader, const u8 *buffer, u32 max_buffer_size,
                 u32 *frame_size, u32 pedantic ) {
  u32 tmp;
  u32 ret = rdr_read_frame(reader, buffer, max_buffer_size, frame_size,
                           pedantic);
  if (ret != HANTRO_OK)
    return ret;

  /* stream corruption, packet loss etc */
  if (stream_packet_loss) {
    ret =  TBRandomizePacketLoss(tb_cfg.tb_params.stream_packet_loss,(u8 *)&tmp);
    if (ret == 0 && tmp)
      return FfReadFrame(reader, buffer, max_buffer_size, frame_size,
                         pedantic);
  }

  if (stream_truncate && pic_rdy && (hdrs_rdy || stream_header_corrupt)) {
    DEBUG_PRINT(("strm_len %d\n", *frame_size));
    ret = TBRandomizeU32(frame_size);
    DEBUG_PRINT(("Randomized strm_len %d\n", *frame_size));
  }
  if (stream_bit_swap) {
    if (stream_header_corrupt || hdrs_rdy) {
      if (pic_rdy) {
        ret = TBRandomizeBitSwapInStream((u8 *)buffer,
                                         *frame_size, tb_cfg.tb_params.stream_bit_swap);
      }
    }
  }
  return HANTRO_OK;
}
