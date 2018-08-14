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

#include "h264decapi.h"
#include "dwl.h"
#include "dwlthread.h"


#include "bytestream_parser.h"
#include "libav-wrapper.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

#include "h264hwd_container.h"
#include "tb_cfg.h"
#include "tb_tiled.h"
#include "regdrv_g1.h"

#include "tb_md5.h"
#include "tb_sw_performance.h"
#include "tb_stream_corrupt.h"

#ifdef _ENABLE_2ND_CHROMA
#include "h264decapi_e.h"
#endif

/*------------------------------------------------------------------------------
    Module defines
------------------------------------------------------------------------------*/

/* Debug prints */
#undef DEBUG_PRINT
#define DEBUG_PRINT(argv) printf argv

#define NUM_RETRY   100 /* how many retries after HW timeout */
#define MAX_BUFFERS 34
#define ALIGNMENT_MASK 7

void WriteOutput(const char *filename, const char *filename_tiled, u8 * data, u32 pic_size,
                 u32 pic_width, u32 pic_height, u32 frame_number, u32 mono_chrome,
                 u32 view, u32 tiled_mode);
u32 NextPacket(u8 ** p_strm);

u32 CropPicture(u8 * p_out_image, u8 * p_in_image,
                u32 frame_width, u32 frame_height, H264CropParams * p_crop_params,
                u32 mono_chrome);
static void printDecodeReturn(i32 retval);
void printH264MCPicCodingType(u32 *pic_type);

/* Global variables for stream handling */
u8 *stream_stop = NULL;
u32 packetize = 0;
u32 nal_unit_stream = 0;
FILE *foutput = NULL, *foutput2 = NULL;
FILE *f_tiled_output = NULL;
FILE *fchroma2 = NULL;

/* flag to enable md5sum output */
u32 md5sum = 0;

FILE *findex = NULL;

/* stream start address */
u32 trace_used_stream = 0;

/* output file writing disable */
u32 disable_output_writing = 0;
u32 retry = 0;

u32 clock_gating = DEC_X170_INTERNAL_CLOCK_GATING;
u32 data_discard = DEC_X170_DATA_DISCARD_ENABLE;
u32 latency_comp = DEC_X170_LATENCY_COMPENSATION;
u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
u32 bus_burst_length = DEC_X170_BUS_BURST_LENGTH;
u32 asic_service_priority = DEC_X170_ASIC_SERVICE_PRIORITY;
u32 output_format = DEC_X170_OUTPUT_FORMAT;
u32 service_merge_disable = DEC_X170_SERVICE_MERGE_DISABLE;

u32 stream_truncate = 0;
u32 stream_packet_loss = 0;
u32 stream_header_corrupt = 0;
u32 hdrs_rdy = 0;
u32 pic_rdy = 0;
struct TBCfg tb_cfg;

u32 long_stream = 0;
FILE *finput;
u32 planar_output = 0;
u32 is_input_mp4 = 0;
void *mp4file = NULL;

const u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 dpb_mode = DEC_DPB_FRAME;
u32 convert_tiled_output = 0;

u32 use_peek_output = 0;
u32 enable_mvc = 0;
u32 mvc_separate_views = 0;
u32 skip_non_reference = 0;
u32 convert_to_frame_dpb = 0;

u32 b_frames;

char *grey_chroma = NULL;
size_t grey_chroma_size = 0;

char *pic_big_endian = NULL;
size_t pic_big_endian_size = 0;

#ifdef ASIC_TRACE_SUPPORT
extern u32 hw_dec_pic_count;
extern u32 g_hw_ver;
extern u32 h264_high_support;
#endif

#ifdef H264_EVALUATION
extern u32 g_hw_ver;
extern u32 h264_high_support;
#endif

u32 abort_test = 0;

#ifdef ADS_PERFORMANCE_SIMULATION

volatile u32 tttttt = 0;

void trace_perf() {
  tttttt++;
}

#undef START_SW_PERFORMANCE
#undef END_SW_PERFORMANCE

#define START_SW_PERFORMANCE trace_perf();
#define END_SW_PERFORMANCE trace_perf();

#endif

H264DecInst dec_inst;
H264DecInfo dec_info;

#ifdef USE_EXTERNAL_BUFFER
const void *dwl_inst = NULL;
u32 use_extra_buffers = 0;
u32 buffer_size;
u32 num_buffers;  /* external buffers allocated yet. */
u32 add_buffer_thread_run = 0;
pthread_t add_buffer_thread;
pthread_mutex_t ext_buffer_contro;
struct DWLLinearMem ext_buffers[MAX_BUFFERS];
#endif

#ifdef USE_OUTPUT_RELEASE
u32 allocate_extra_buffers_in_output = 0;
#endif

u32 add_extra_flag = 0;


const char *out_file_name = "out.yuv";
const char *out_file_name_tiled = "out_tiled.yuv";
u32 pic_size;
u32 crop_display = 0;
u8 *tmp_image = NULL;

pthread_t release_thread;
pthread_t output_thread;
int output_thread_run = 0;

#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>

u32 pic_display_number = 0;
u32 old_pic_display_number = 0;

pid_t main_pid;

#ifdef TESTBENCH_WATCHDOG
void watchdog1(int signal_number) {
  if(pic_display_number == old_pic_display_number) {
    fprintf(stderr, "\n\nTestbench TIMEOUT\n");
    kill(main_pid, SIGTERM);
  } else {
    old_pic_display_number = pic_display_number;
  }

}
#endif

#ifdef USE_EXTERNAL_BUFFER
static void *AddBufferThread(void *arg) {
  usleep(100000);
  while(add_buffer_thread_run) {
    pthread_mutex_lock(&ext_buffer_contro);
    if(add_extra_flag && (num_buffers < 34)) {
      struct DWLLinearMem mem;
      mem.mem_type = DWL_MEM_TYPE_DPB;
      if(DWLMallocLinear(dwl_inst, buffer_size, &mem) == DWL_OK) {
        H264DecRet rv = H264DecMCAddBuffer(dec_inst, &mem) ;
        if(rv != H264DEC_OK && rv != H264DEC_WAITING_FOR_BUFFER) {
          DWLFreeLinear(dwl_inst, &mem);
        } else {
          ext_buffers[num_buffers++] = mem;
        }
      }
    }
    pthread_mutex_unlock(&ext_buffer_contro);
    pthread_yield();
  }
  return NULL;
}

void ReleaseExtBuffers() {
  int i;
  pthread_mutex_lock(&ext_buffer_contro);
  for(i=0; i<num_buffers; i++) {
    DEBUG_PRINT(("Freeing buffer %p\n", ext_buffers[i].virtual_address));
    DWLFreeLinear(dwl_inst, &ext_buffers[i]);
    DWLmemset(&ext_buffers[i], 0, sizeof(ext_buffers[i]));
  }
  pthread_mutex_unlock(&ext_buffer_contro);
}

sem_t buf_release_sem;
H264DecPicture buf_list[100] = {0};
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
#ifdef GET_FREE_BUFFER_NON_BLOCK
      usleep(1000000);   /* Simulate the delayed release, so that there may be no free buffer to use. */
#endif
      H264DecMCPictureConsumed(dec_inst, &buf_list[list_pop_index]);
      buf_status[list_pop_index] = 0;
      list_pop_index++;
      if(list_pop_index == 100)
        list_pop_index = 0;
#ifdef USE_EXTERNAL_BUFFER
      if(allocate_extra_buffers_in_output) {
        pthread_mutex_lock(&ext_buffer_contro);
        if(add_extra_flag && (num_buffers < 34)) {
          struct DWLLinearMem mem;
          mem.mem_type = DWL_MEM_TYPE_DPB;
          if(DWLMallocLinear(dwl_inst, buffer_size, &mem) == DWL_OK) {
            H264DecRet rv = H264DecMCAddBuffer(dec_inst, &mem) ;
            if(rv != H264DEC_OK && rv != H264DEC_WAITING_FOR_BUFFER) {
              DWLFreeLinear(dwl_inst, &mem);
            } else {
              ext_buffers[num_buffers++] = mem;
            }
          }
        }
        pthread_mutex_unlock(&ext_buffer_contro);
      }
#endif
    }
    /* The last buffer has been consumed */
    if(last_pic_flag &&  buf_status[list_pop_index] == 0)
      break;
    usleep(10000);
  }
}
#endif




void SetMissingField2Const( u8 *output_picture,
                            u32 pic_width,
                            u32 pic_height,
                            u32 monochrome,
                            u32 top_field) {
  u8 *field_base;
  i32 i;

  /* TODO: Does not support tiled input */

  if (dpb_mode != DEC_DPB_FRAME) {
    /* luma */
    field_base = output_picture;

    if (!top_field) {
      /* bottom */
      field_base += pic_width * pic_height / 2;
    }

    memset(field_base, 128, pic_width * pic_height / 2);

    if (monochrome)
      return;

    /* chroma */
    field_base = output_picture + pic_width * pic_height;

    if (!top_field) {
      /* bottom */
      field_base += pic_width * ((pic_height / 2 + 1) / 2);
    }

    memset(field_base, 128, pic_width * ((pic_height / 2 + 1) / 2));

    return;
  }

  /* luma */
  field_base = output_picture;

  if (!top_field) {
    /* bottom */
    field_base += pic_width;
  }

  for (i = 0; i < pic_height / 2; i++) {
    memset(field_base, 128, pic_width);
    field_base += 2 * pic_width;
  }

  if (monochrome)
    return;

  /* chroma */
  field_base = output_picture + pic_width * pic_height;

  if (!top_field) {
    /* bottom */
    field_base += pic_width;
  }

  for (i = 0; i < (pic_height / 2 + 1) / 2; i++) {
    memset(field_base, 128, pic_width);
    field_base += 2 * pic_width;
  }
}

/* Output thread entry point. */
static void* h264_output_thread(void* arg) {
  H264DecPicture dec_picture;

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
    H264DecRet ret;

    ret = H264DecMCNextPicture(dec_inst, &dec_picture);

    if(ret == H264DEC_PIC_RDY) {
      DEBUG_PRINT(("PIC %d/%d, view %d, type [%s:%s], ",
                   pic_display_number,
                   dec_picture.pic_id,
                   dec_picture.view_id,
                   dec_picture.is_idr_picture[0] ? "IDR" : "NON-IDR",
                   dec_picture.is_idr_picture[1] ? "IDR" : "NON-IDR"));

      /* pic coding type */
      printH264MCPicCodingType(dec_picture.pic_coding_type);

      /* if(dec_picture.nbr_of_err_mbs) */ /* To check this info, always print it */
      {
        DEBUG_PRINT((", MB Error Concealed %d", dec_picture.nbr_of_err_mbs));
      }

      if(dec_picture.interlaced) {
        DEBUG_PRINT((", INTERLACED "));
        if(dec_picture.field_picture) {
          DEBUG_PRINT(("FIELD %s",
                       dec_picture.top_field ? "TOP" : "BOTTOM"));

          SetMissingField2Const((u8*)dec_picture.output_picture,
                                dec_picture.pic_width,
                                dec_picture.pic_height,
                                dec_info.mono_chrome,
                                !dec_picture.top_field );
        } else {
          DEBUG_PRINT(("FRAME"));
        }
      }

      DEBUG_PRINT((", Crop: (%d, %d), %dx%d\n",
                   dec_picture.crop_params.crop_left_offset,
                   dec_picture.crop_params.crop_top_offset,
                   dec_picture.crop_params.crop_out_width,
                   dec_picture.crop_params.crop_out_height));

      fflush(stdout);

      /* Write output picture to file */
      image_data = (u8 *) dec_picture.output_picture;

      if(crop_display) {
        int tmp = CropPicture(tmp_image, image_data,
                              dec_picture.pic_width,
                              dec_picture.pic_height,
                              &dec_picture.crop_params,
                              dec_info.mono_chrome);
        if(tmp) {
          DEBUG_PRINT(("ERROR in cropping!\n"));
        }
      }

      WriteOutput(out_file_name, out_file_name_tiled,
                  crop_display ? tmp_image : image_data, pic_size,
                  crop_display ? dec_picture.crop_params.crop_out_width : dec_picture.pic_width,
                  crop_display ? dec_picture.crop_params.crop_out_height : dec_picture.pic_height,
                  pic_display_number - 1, dec_info.mono_chrome,
                  dec_picture.view_id,
                  dec_picture.output_format);
#ifdef USE_EXTERNAL_BUFFER
      /* Push output buffer into buf_list and wait to be consumed */
      buf_list[list_push_index] = dec_picture;
      buf_status[list_push_index] = 1;
      list_push_index++;
      if(list_push_index == 100)
        list_push_index = 0;

      sem_post(&buf_release_sem);
#else
      H264DecMCPictureConsumed(dec_inst, &dec_picture);
#endif
      pic_display_number++;
    } else if(ret == H264DEC_END_OF_STREAM) {
#ifdef USE_EXTERNAL_BUFFER
      last_pic_flag = 1;
#endif
      DEBUG_PRINT(("END-OF-STREAM received in output thread\n"));
#ifdef USE_EXTERNAL_BUFFER
      add_buffer_thread_run = 0;
#endif
      break;
    }
  }
}

/* one extra stream buffer so that we can decode ahead,
 * and be ready when Core has finished
 */
#define MAX_STRM_BUFFERS    (MAX_ASIC_CORES + 1)

static struct DWLLinearMem stream_mem[MAX_STRM_BUFFERS];
static u32 stream_mem_status[MAX_STRM_BUFFERS];
static u32 allocated_buffers = 0;

static sem_t stream_buff_free;
static pthread_mutex_t strm_buff_stat_lock = PTHREAD_MUTEX_INITIALIZER;


void StreamBufferConsumed(void *stream, void *p_user_data) {
  int idx;
  pthread_mutex_lock(&strm_buff_stat_lock);

  idx = 0;
  do {
    if ((u8*)stream >= (u8*)stream_mem[idx].virtual_address &&
        (u8*)stream < (u8*)stream_mem[idx].virtual_address + stream_mem[idx].size) {
      stream_mem_status[idx] = 0;
      assert(p_user_data == stream_mem[idx].virtual_address);
      break;
    }
    idx++;
  } while(idx < allocated_buffers);

  assert(idx < allocated_buffers);

  pthread_mutex_unlock(&strm_buff_stat_lock);

  sem_post(&stream_buff_free);
}

int GetFreeStreamBuffer() {
  int idx;
  sem_wait(&stream_buff_free);

  pthread_mutex_lock(&strm_buff_stat_lock);

  idx = 0;
  while(stream_mem_status[idx]) {
    idx++;
  }
  assert(idx < allocated_buffers);

  stream_mem_status[idx] = 1;

  pthread_mutex_unlock(&strm_buff_stat_lock);

  return idx;
}

/*------------------------------------------------------------------------------

    Function name:  main

    Purpose:
        main function of decoder testbench. Provides command line interface
        with file I/O for H.264 decoder. Prints out the usage information
        when executed without arguments.

------------------------------------------------------------------------------*/

int main(int argc, char **argv) {

  u32 i, tmp;
  u32 max_num_pics = 0;
  u8 *image_data;
  long int strm_len;

  H264DecRet ret;
  H264DecInput dec_input;
  H264DecOutput dec_output;
  H264DecPicture dec_picture;

  i32 dwlret;
#ifdef USE_EXTERNAL_BUFFER
  u32 prev_width = 0, prev_height = 0;
  u32 min_buffer_num = 0;
  u32 buffer_release_flag = 1;
  H264DecBufferInfo hbuf;
  H264DecRet rv;
  memset(ext_buffers, 0, sizeof(ext_buffers));
  pthread_mutex_init(&ext_buffer_contro, NULL);
  struct DWLInitParam dwl_init;
  dwl_init.client_type = DWL_CLIENT_TYPE_H264_DEC;
#endif


  u32 pic_decode_number = 0;

  u32 num_errors = 0;
  u32 disable_output_reordering = 0;
  u32 use_display_smoothing = 0;
  u32 rlc_mode = 0;
  u32 mb_error_concealment = 0;
  u32 force_whole_stream = 0;
  const u8 *ptmpstream = NULL;
  u32 stream_will_end = 0;

  FILE *f_tbcfg;
  u32 seed_rnd = 0;
  u32 stream_bit_swap = 0;
  i32 corrupted_bytes = 0;  /*  */

  const char *in_file_name = argv[argc - 1];

#ifdef ASIC_TRACE_SUPPORT
  g_hw_ver = 8190; /* default to 8190 mode */
  h264_high_support = 1;
#endif

#ifdef H264_EVALUATION_8170
  g_hw_ver = 8170;
  h264_high_support = 0;
#elif H264_EVALUATION_8190
  g_hw_ver = 8190;
  h264_high_support = 1;
#elif H264_EVALUATION_9170
  g_hw_ver = 9170;
  h264_high_support = 1;
#elif H264_EVALUATION_9190
  g_hw_ver = 9190;
  h264_high_support = 1;
#elif H264_EVALUATION_G1
  g_hw_ver = 10000;
  h264_high_support = 1;
#endif

  main_pid = getpid();

#ifndef EXPIRY_DATE
#define EXPIRY_DATE (u32)0xFFFFFFFF
#endif /* EXPIRY_DATE */

  /* expiry stuff */
  {
    u8 tm_buf[7];
    time_t sys_time;
    struct tm * tm;
    u32 tmp1;

    /* Check expiration date */
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

  for(i = 0; i < MAX_STRM_BUFFERS; i++) {
    stream_mem[i].virtual_address = NULL;
    stream_mem[i].bus_address = 0;
  }

  /* set test bench configuration */
  TBSetDefaultCfg(&tb_cfg);
  f_tbcfg = fopen("tb.cfg", "r");
  if(f_tbcfg == NULL) {
    DEBUG_PRINT(("UNABLE TO OPEN INPUT FILE: \"tb.cfg\"\n"));
    DEBUG_PRINT(("USING DEFAULT CONFIGURATION\n"));
  } else {
    fclose(f_tbcfg);
    if(TBParseConfig("tb.cfg", TBReadParam, &tb_cfg) == TB_FALSE)
      return -1;
    if(TBCheckCfg(&tb_cfg) != 0)
      return -1;
  }

  INIT_SW_PERFORMANCE;

  {
    H264DecApiVersion dec_api;
    H264DecBuild dec_build;
    u32 n_cores;

    /* Print API version number */
    dec_api = H264DecGetAPIVersion();
    dec_build = H264DecGetBuild();
    n_cores = H264DecMCGetCoreCount();

    DEBUG_PRINT(("\nX170 H.264 Decoder, SW API v%d.%d - build: %d.%d"
                 "\n                    HW: %4x - build: %4x, %d cores\n\n",
                 dec_api.major, dec_api.minor,
                 dec_build.sw_build >> 16, dec_build.sw_build & 0xFFFF,
                 dec_build.hw_build >> 16, dec_build.hw_build & 0xFFFF,
                 n_cores));

    /* number of stream buffers to allocate */
    allocated_buffers = n_cores + 1;
  }

  /* Check that enough command line arguments given, if not -> print usage
   * information out */
  if(argc < 2) {
    DEBUG_PRINT(("Usage: %s [options] file.h264\n", argv[0]));
    DEBUG_PRINT(("\t-Nn forces decoding to stop after n pictures\n"));
    DEBUG_PRINT(("\t-O<file> write output to <file> (default out_wxxxhyyy.yuv)\n"));
    DEBUG_PRINT(("\t--md5 Output frame based md5 checksum. No YUV output!\n"));
    DEBUG_PRINT(("\t-X Disable output file writing\n"));
    DEBUG_PRINT(("\t-C display cropped image (default decoded image)\n"));
    DEBUG_PRINT(("\t-R disable DPB output reordering\n"));
    DEBUG_PRINT(("\t-J enable double DPB for smooth display\n"));
    DEBUG_PRINT(("\t-P write planar output\n"));
    DEBUG_PRINT(("\t-M Enable MVC decoding (use it only with MVC streams)\n"));
    DEBUG_PRINT(("\t--separate-fields-in-dpb DPB stores interlaced content"\
                 " as fields (default: frames)\n"));
    DEBUG_PRINT(("\t--output-frame-dpb Convert output to frame mode even if"\
                 " field DPB mode used\n"));
    DEBUG_PRINT(("\t-M Enable MVC decoding (use it only with MVC streams)\n"));
    DEBUG_PRINT(("\t-V Write MVC views to separate files\n"));
    DEBUG_PRINT(("\t-Q Skip decoding non-reference pictures.\n"));

#ifdef USE_EXTERNAL_BUFFER
    DEBUG_PRINT(("\t-A add extra external buffer randomly\n"));
#ifdef USE_OUTPUT_RELEASE
    DEBUG_PRINT(("\t-a allocate extra external buffer in output thread\n"));
#endif
#endif

    return 0;
  }

  /* read command line arguments */
  for(i = 1; i < (u32) (argc - 1); i++) {
    if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_pics = (u32) atoi(argv[i] + 2);
    } else if(strncmp(argv[i], "-O", 2) == 0) {
      out_file_name = argv[i] + 2;
    } else if(strcmp(argv[i], "-X") == 0) {
      disable_output_writing = 1;
    } else if(strcmp(argv[i], "-C") == 0) {
      crop_display = 1;
    } else if (strncmp(argv[i], "-E", 2) == 0) {
      DEBUG_PRINT(("WARNING! Tiled mode ignored, not supported with multicore!"));
    } else if (strncmp(argv[i], "-G", 2) == 0) {
      convert_tiled_output = 1;
    } else if(strcmp(argv[i], "-R") == 0) {
      disable_output_reordering = 1;
    } else if(strcmp(argv[i], "-J") == 0) {
#ifndef USE_EXTERNAL_BUFFER
      use_display_smoothing = 1;
#endif
    } else if(strcmp(argv[i], "-W") == 0) {
      force_whole_stream = 1;
    } else if(strcmp(argv[i], "-L") == 0) {
      long_stream = 1;
    } else if(strcmp(argv[i], "-P") == 0) {
      planar_output = 1;
    } else if(strcmp(argv[i], "-M") == 0) {
      enable_mvc = 1;
    } else if(strcmp(argv[i], "-V") == 0) {
      mvc_separate_views = 1;
    } else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    } else if(strcmp(argv[i], "--separate-fields-in-dpb") == 0) {
      dpb_mode = DEC_DPB_INTERLACED_FIELD;
    } else if(strcmp(argv[i], "--output-frame-dpb") == 0) {
      convert_to_frame_dpb = 1;
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
    else if(strcmp(argv[i], "-Z") == 0) {
      use_peek_output = 1;
    } else if(strcmp(argv[i], "--md5") == 0) {
      md5sum = 1;
    } else {
      DEBUG_PRINT(("UNKNOWN PARAMETER: %s\n", argv[i]));
      return 1;
    }
  }

  if(enable_mvc) {
    finput = fopen(in_file_name, "r");
    if(!finput) {
      fprintf(stderr, "Failed to open input file: %s\n", in_file_name);
      return -1;
    }
  } else {
    libav_init();

    if(libav_open(in_file_name) < 0)
      return -1;
  }

  /*TBPrintCfg(&tb_cfg); */
  mb_error_concealment = 0; /* TBGetDecErrorConcealment(&tb_cfg); */
  clock_gating = TBGetDecClockGating(&tb_cfg);
  data_discard = TBGetDecDataDiscard(&tb_cfg);
  latency_comp = tb_cfg.dec_params.latency_compensation;
  output_picture_endian = TBGetDecOutputPictureEndian(&tb_cfg);
  bus_burst_length = tb_cfg.dec_params.bus_burst_length;
  asic_service_priority = tb_cfg.dec_params.asic_service_priority;
  service_merge_disable = TBGetDecServiceMergeDisable(&tb_cfg);

  DEBUG_PRINT(("Decoder Macro Block Error Concealment %d\n",
               mb_error_concealment));
  DEBUG_PRINT(("Decoder Clock Gating %d\n", clock_gating));
  DEBUG_PRINT(("Decoder Data Discard %d\n", data_discard));
  DEBUG_PRINT(("Decoder Latency Compensation %d\n", latency_comp));
  DEBUG_PRINT(("Decoder Output Picture Endian %d\n", output_picture_endian));
  DEBUG_PRINT(("Decoder Bus Burst Length %d\n", bus_burst_length));
  DEBUG_PRINT(("Decoder Asic Service Priority %d\n", asic_service_priority));

  seed_rnd = tb_cfg.tb_params.seed_rnd;
  stream_header_corrupt = TBGetTBStreamHeaderCorrupt(&tb_cfg);
  /* if headers are to be corrupted
   * -> do not wait the picture to finalize before starting stream corruption */
  if(stream_header_corrupt)
    pic_rdy = 1;
  stream_truncate = TBGetTBStreamTruncate(&tb_cfg);
  if(strcmp(tb_cfg.tb_params.stream_bit_swap, "0") != 0) {
    stream_bit_swap = 1;
  } else {
    stream_bit_swap = 0;
  }
  if(strcmp(tb_cfg.tb_params.stream_packet_loss, "0") != 0) {
    stream_packet_loss = 1;
  } else {
    stream_packet_loss = 0;
  }

  long_stream = 1;
  packetize = 1;
  nal_unit_stream = 0;

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

#ifdef ASIC_TRACE_SUPPORT
  /* open tracefiles */
  tmp = openTraceFiles();
  if(!tmp) {
    DEBUG_PRINT(("UNABLE TO OPEN TRACE FILE(S)\n"));
  }
  if(nal_unit_stream)
    trace_h264_dec_tools.stream_mode.nal_unit_strm = 1;
  else
    trace_h264_dec_tools.stream_mode.byte_strm = 1;
#endif

#ifdef USE_EXTERNAL_BUFFER
  dwl_inst = DWLInit(&dwl_init);
  if(dwl_inst == NULL) {
    DEBUG_PRINT(("H264DecInit# ERROR: DWL Init failed\n"));
    goto end;
  }
#endif

  /* initialize multicore decoder. If unsuccessful -> exit */
  {
    H264DecMCConfig mc_init_cfg;

    sem_init(&stream_buff_free, 0, allocated_buffers);

    mc_init_cfg.no_output_reordering = disable_output_reordering;
    mc_init_cfg.use_display_smoothing = use_display_smoothing;

    mc_init_cfg.dpb_flags = dpb_mode ? DEC_DPB_ALLOW_FIELD_ORDERING : 0;
    mc_init_cfg.stream_consumed_callback = StreamBufferConsumed;

    START_SW_PERFORMANCE;
    ret = H264DecMCInit(&dec_inst, &mc_init_cfg);
    END_SW_PERFORMANCE;
  }

  if(ret != H264DEC_OK) {
    DEBUG_PRINT(("DECODER INITIALIZATION FAILED\n"));
    goto end;
  }

  /* configure decoder to decode both views of MVC stereo high streams */
  if (enable_mvc)
    H264DecSetMvc(dec_inst);

  /* Set ref buffer test mode */
  ((decContainer_t *) dec_inst)->ref_buffer_ctrl.test_function = TBRefbuTestMode;

  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_OUT_ENDIAN,
                 output_picture_endian);
  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_MAX_BURST,
                 bus_burst_length);
  if ((DWLReadAsicID(DWL_CLIENT_TYPE_H264_DEC) >> 16) == 0x8170U) {
    SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_PRIORITY_MODE,
                   asic_service_priority);
  }
  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_DATA_DISC_E,
                 data_discard);
  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_SERV_MERGE_DIS,
                 service_merge_disable);

#ifdef _ENABLE_2ND_CHROMA
  if (!TBGetDecCh8PixIleavOutput(&tb_cfg)) {
    ((decContainer_t *) dec_inst)->storage.enable2nd_chroma = 0;
  } else {
    /* cropping not supported when additional chroma output format used */
    crop_display = 0;
  }
#endif

  TBInitializeRandom(seed_rnd);

  dec_input.skip_non_reference = skip_non_reference;

  for(i = 0; i < allocated_buffers; i++) {
#ifdef USE_EXTERNAL_BUFFER
    stream_mem[i].mem_type = DWL_MEM_TYPE_SLICE;
    if(DWLMallocLinear(dwl_inst,
                       4096*1165, stream_mem + i) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#else
    stream_mem[i].mem_type = DWL_MEM_TYPE_SLICE;
    if(DWLMallocLinear(((decContainer_t *) dec_inst)->dwl,
                       4096*1165, stream_mem + i) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#endif
  }
  {
    int id = GetFreeStreamBuffer();

    dec_input.stream = (u8 *) stream_mem[id].virtual_address;
    dec_input.stream_bus_address = (u32) stream_mem[id].bus_address;

    /* stream processed callback param */
    dec_input.p_user_data = stream_mem[id].virtual_address;
  }

  /* get pointer to next packet and the size of packet
   * (for packetize or nal_unit_stream modes) */
  ptmpstream = dec_input.stream;
  if((tmp = NextPacket((u8 **) (&dec_input.stream))) != 0) {
    dec_input.data_len = tmp;
    dec_input.stream_bus_address += (u32) (dec_input.stream - ptmpstream);
  }

  pic_decode_number = pic_display_number = 1;

  /* main decoding loop */
  do {
    if(stream_truncate && pic_rdy && (hdrs_rdy || stream_header_corrupt) &&
        (long_stream || (!long_stream && (packetize || nal_unit_stream)))) {
      i32 ret;

      ret = TBRandomizeU32(&dec_input.data_len);
      if(ret != 0) {
        DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
        return 0;
      }
      DEBUG_PRINT(("Randomized stream size %d\n", dec_input.data_len));
    }

    /* If enabled, break the stream */
    if(stream_bit_swap) {
      if((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt) {
        /* Picture needs to be ready before corrupting next picture */
        if(pic_rdy && corrupted_bytes <= 0) {
          ret =
            TBRandomizeBitSwapInStream(dec_input.stream,
                                       dec_input.data_len,
                                       tb_cfg.tb_params.
                                       stream_bit_swap);
          if(ret != 0) {
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

    /* test decAbort function */
    if (abort_test && pic_decode_number > 3) {
      abort_test = 0;
      H264DecMCAbort(dec_inst);
    }

    /* call API function to perform decoding */
    START_SW_PERFORMANCE;
    ret = H264DecMCDecode(dec_inst, &dec_input, &dec_output);
    END_SW_PERFORMANCE;
    printDecodeReturn(ret);
    switch (ret) {
    case H264DEC_STREAM_NOT_SUPPORTED: {
      DEBUG_PRINT(("ERROR: UNSUPPORTED STREAM!\n"));
      goto end;
    }
    case H264DEC_HDRS_RDY: {
      /* Set a flag to indicate that headers are ready */
      hdrs_rdy = 1;
      TBSetRefbuMemModel( &tb_cfg,
                          ((decContainer_t *) dec_inst)->h264_regs,
                          &((decContainer_t *) dec_inst)->ref_buffer_ctrl );

      /* Stream headers were successfully decoded
       * -> stream information is available for query now */

      START_SW_PERFORMANCE;
      tmp = H264DecGetInfo(dec_inst, &dec_info);
      END_SW_PERFORMANCE;
      if(tmp != H264DEC_OK) {
        DEBUG_PRINT(("ERROR in getting stream info!\n"));
        goto end;
      }

      DEBUG_PRINT(("Width %d Height %d\n",
                   dec_info.pic_width, dec_info.pic_height));

      DEBUG_PRINT(("Cropping params: (%d, %d) %dx%d\n",
                   dec_info.crop_params.crop_left_offset,
                   dec_info.crop_params.crop_top_offset,
                   dec_info.crop_params.crop_out_width,
                   dec_info.crop_params.crop_out_height));

      DEBUG_PRINT(("MonoChrome = %d\n", dec_info.mono_chrome));
      DEBUG_PRINT(("Interlaced = %d\n", dec_info.interlaced_sequence));
      DEBUG_PRINT(("DPB mode   = %d\n", dec_info.dpb_mode));
      DEBUG_PRINT(("Pictures in DPB = %d\n", dec_info.pic_buff_size));
      DEBUG_PRINT(("Pictures in Multibuffer PP = %d\n", dec_info.multi_buff_pp_size));
#ifdef USE_EXTERNAL_BUFFER
      if((dec_info.pic_buff_size != min_buffer_num) ||
          (dec_info.pic_width * dec_info.pic_height != prev_width * prev_height)) {
        /* Reset buffers added and stop adding extra buffers when a new header comes. */
        add_extra_flag = 0;
        ReleaseExtBuffers();
        buffer_release_flag = 1;
        num_buffers = 0;
      }
      prev_width = dec_info.pic_width;
      prev_height = dec_info.pic_height;
      min_buffer_num = dec_info.pic_buff_size;
#endif

      dpb_mode = dec_info.dpb_mode;

      /* check if we do need to crop */
      if(dec_info.crop_params.crop_left_offset == 0 &&
          dec_info.crop_params.crop_top_offset == 0 &&
          dec_info.crop_params.crop_out_width == dec_info.pic_width &&
          dec_info.crop_params.crop_out_height == dec_info.pic_height ) {
        crop_display = 0;
      }

      /* crop if asked to do so */
      if(crop_display) {
        /* release the old one */
        if(tmp_image)
          free(tmp_image);

        /* Cropped frame size in planar YUV 4:2:0 */
        pic_size = dec_info.crop_params.crop_out_width *
                   dec_info.crop_params.crop_out_height;
        if(!dec_info.mono_chrome)
          pic_size = (3 * pic_size) / 2;
        tmp_image = malloc(pic_size);
        if(tmp_image == NULL) {
          DEBUG_PRINT(("ERROR in allocating cropped image!\n"));
          goto end;
        }
      } else {
        pic_size = dec_info.pic_width * dec_info.pic_height;
        if(!dec_info.mono_chrome)
          pic_size = (3 * pic_size) / 2;
      }

      DEBUG_PRINT(("video_range %d, matrix_coefficients %d\n",
                   dec_info.video_range, dec_info.matrix_coefficients));

#if !defined(USE_OUTPUT_RELEASE)
      if(!output_thread_run) {
        output_thread_run = 1;
        pthread_create(&output_thread, NULL, h264_output_thread, NULL);
      }
#endif

      break;
    }
    case H264DEC_ADVANCED_TOOLS: {
      /* ASO/FMO detected and not supported in multicore mode */
      DEBUG_PRINT(("ASO/FMO detected, decoding will stop\n", ret));
      goto end;
    }
    case H264DEC_PIC_DECODED:
      /* we should never return data in MC mode.
       * buffer shall contain one full frame
       */
      /* when new access unit is detected then is OK to return
       * all data
       */
      if(dec_output.data_left)
        DEBUG_PRINT(("\tUnfinished buffer, %d bytes\n",
                     dec_output.data_left));
    case H264DEC_PENDING_FLUSH:
      /* case H264DEC_FREEZED_PIC_RDY: */
      /* Picture is now ready */
      pic_rdy = 1;

      /*lint -esym(644,tmp_image,pic_size) variable initialized at
       * H264DEC_HDRS_RDY_BUFF_NOT_EMPTY case */

      /* If enough pictures decoded -> force decoding to end
       * by setting that no more stream is available */
      if(pic_decode_number == max_num_pics)
        dec_input.data_len = 0;

      printf("DECODED PICTURE %d\n", pic_decode_number);
      /* Increment decoding number for every decoded picture */
      pic_decode_number++;
#if defined(USE_OUTPUT_RELEASE)
      if(!output_thread_run) {
        output_thread_run = 1;
        sem_init(&buf_release_sem, 0, 0);
        pthread_create(&output_thread, NULL, h264_output_thread, NULL);
        pthread_create(&release_thread, NULL, buf_release_thread, NULL);
      }
#endif
#ifdef USE_EXTERNAL_BUFFER
      if(use_extra_buffers && !add_buffer_thread_run) {
        add_buffer_thread_run = 1;
        pthread_create(&add_buffer_thread, NULL, AddBufferThread, NULL);
      }
#endif

      retry = 0;
      break;

    case H264DEC_STRM_PROCESSED:
    case H264DEC_NONREF_PIC_SKIPPED:
    case H264DEC_STRM_ERROR: {
      /* Used to indicate that picture decoding needs to finalized prior to corrupting next picture
       * pic_rdy = 0; */

      break;
    }
#ifdef USE_EXTERNAL_BUFFER
    case H264DEC_WAITING_FOR_BUFFER: {
      DEBUG_PRINT(("Waiting for frame buffers\n"));
      struct DWLLinearMem mem;

      rv = H264DecMCGetBufferInfo(dec_inst, &hbuf);
      DEBUG_PRINT(("H264DecMCGetBufferInfo ret %d\n", rv));
      DEBUG_PRINT(("buf_to_free %p, next_buf_size %d, buf_num %d\n",
                   (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num));

      buffer_size = hbuf.next_buf_size;
      if(buffer_release_flag && hbuf.next_buf_size) {
        /* Only add minimum required buffers at first. */
        //extra_buffer_num = hbuf.buf_num - min_buffer_num;
        for(i=0; i<hbuf.buf_num; i++) {
          mem.mem_type = DWL_MEM_TYPE_DPB;
          DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
          rv = H264DecMCAddBuffer(dec_inst, &mem);
          DEBUG_PRINT(("H264DecMCAddBuffer ret %d\n", rv));
          if(rv != H264DEC_OK && rv != H264DEC_WAITING_FOR_BUFFER) {
            DWLFreeLinear(dwl_inst, &mem);
          } else {
            ext_buffers[i] = mem;
          }
        }
        /* Extra buffers are allowed when minimum required buffers have been added.*/
        num_buffers = hbuf.buf_num;
        add_extra_flag = 1;
        buffer_release_flag = 0;
      }
      break;
    }
#endif

#ifdef GET_FREE_BUFFER_NON_BLOCK
    case H264DEC_NO_DECODING_BUFFER: {
      /* Just for simulation: if no buffer, sleep 0.5 second and try decoding again. */
      usleep(500000);
      break;
    }
#endif

    case H264DEC_OK:
      /* nothing to do, just call again */
      break;
    case H264DEC_HW_TIMEOUT:
      DEBUG_PRINT(("Timeout\n"));
      goto end;
    case H264DEC_ABORTED:
      DEBUG_PRINT(("H264 decoder is aborted: %d\n", ret));
      H264DecMCAbortAfter(dec_inst);
      break;

    default:
      DEBUG_PRINT(("FATAL ERROR: %d\n", ret));
      goto end;
    }

    /* break out of do-while if max_num_pics reached (data_len set to 0) */
    if(dec_input.data_len == 0)
      break;

    if(dec_output.data_left) {
      dec_input.stream_bus_address += (dec_output.strm_curr_pos - dec_input.stream);
      corrupted_bytes -= (dec_input.data_len - dec_output.data_left);
      dec_input.data_len = dec_output.data_left;
      dec_input.stream = dec_output.strm_curr_pos;
    } else {

      {
        int id = GetFreeStreamBuffer();

        dec_input.stream = (u8 *) stream_mem[id].virtual_address;
        dec_input.stream_bus_address = stream_mem[id].bus_address;
        /* stream processed callback param */
        dec_input.p_user_data = stream_mem[id].virtual_address;
      }

      ptmpstream = dec_input.stream;

      dec_input.data_len = NextPacket((u8 **) (&dec_input.stream));

      dec_input.stream_bus_address +=
        (u32) (dec_input.stream - ptmpstream);

      corrupted_bytes = 0;
    }

    /* keep decoding until all data from input stream buffer consumed
     * and all the decoded/queued frames are ready */
  } while(dec_input.data_len > 0);

end:
  DEBUG_PRINT(("Decoding ended, flush the DPB\n"));

#if !defined(USE_OUTPUT_RELEASE)
  H264DecMCEndOfStream(dec_inst);

  if(output_thread)
    pthread_join(output_thread, NULL);
#endif

#ifdef USE_EXTERNAL_BUFFER
  add_buffer_thread_run = 0;
#endif

#if defined(USE_OUTPUT_RELEASE)
  H264DecMCEndOfStream(dec_inst);

  if(output_thread_run) {
    pthread_join(output_thread, NULL);
    pthread_join(release_thread, NULL);
    output_thread_run = 0;
  }
#endif

  sem_destroy(&stream_buff_free);

  fflush(stdout);

  /* have to release stream buffers before releasing decoder as we need DWL */
  for(i = 0; i < allocated_buffers; i++) {
    if(stream_mem[i].virtual_address != NULL) {
#ifdef USE_EXTERNAL_BUFFER
      DWLFreeLinear(dwl_inst, &stream_mem[i]);
#else
      DWLFreeLinear(((decContainer_t *) dec_inst)->dwl, &stream_mem[i]);
#endif
    }
  }


  /* release decoder instance */
  START_SW_PERFORMANCE;
  H264DecRelease(dec_inst);
  END_SW_PERFORMANCE;

#ifdef USE_EXTERNAL_BUFFER
  ReleaseExtBuffers();
  pthread_mutex_destroy(&ext_buffer_contro);
  DWLRelease(dwl_inst);
#endif

  libav_release();

  if(foutput)
    fclose(foutput);
  if(foutput2)
    fclose(foutput2);
  if(fchroma2)
    fclose(fchroma2);
  if(f_tiled_output)
    fclose(f_tiled_output);
  if(finput)
    fclose(finput);

  /* free allocated buffers */
  if(tmp_image != NULL)
    free(tmp_image);
  if(grey_chroma != NULL)
    free(grey_chroma);
  if(pic_big_endian)
    free(pic_big_endian);

  foutput = fopen(out_file_name, "rb");
  if(NULL == foutput) {
    strm_len = 0;
  } else {
    fseek(foutput, 0L, SEEK_END);
    strm_len = (u32) ftell(foutput);
    fclose(foutput);
  }

  DEBUG_PRINT(("Output file: %s\n", out_file_name));

  DEBUG_PRINT(("OUTPUT_SIZE %d\n", strm_len));

  FINALIZE_SW_PERFORMANCE;

  DEBUG_PRINT(("DECODING DONE\n"));

#ifdef ASIC_TRACE_SUPPORT
  trace_SequenceCtrl(hw_dec_pic_count, 0);
  trace_H264DecodingTools();
  /* close trace files */
  closeTraceFiles();
#endif

  if(retry > NUM_RETRY) {
    return -1;
  }

  if(num_errors || pic_decode_number == 1) {
    DEBUG_PRINT(("ERRORS FOUND %d %d\n", num_errors, pic_decode_number));
    /*return 1;*/
    return 0;
  }

  return 0;
}

/*------------------------------------------------------------------------------

    Function name:  WriteOutput

    Purpose:
        Write picture pointed by data to file. Size of the
        picture in pixels is indicated by pic_size.

------------------------------------------------------------------------------*/
void WriteOutput(const char *filename, const char *filename_tiled, u8 * data, u32 pic_size,
                 u32 pic_width, u32 pic_height, u32 frame_number, u32 mono_chrome,
                 u32 view, u32 tiled_mode) {

  u32 i, tmp;
  FILE **fout;
  char alt_file_name[256];
  char *fn;
  u8 *raster_scan = NULL;

  if(disable_output_writing != 0) {
    return;
  }

  fout = (view && mvc_separate_views) ? &foutput2 : &foutput;
  /* foutput is global file pointer */
  if(*fout == NULL) {
    if (view && mvc_separate_views) {
      strcpy(alt_file_name, filename);
      sprintf(alt_file_name+strlen(alt_file_name)-4, "_%d.yuv", view);
      fn = alt_file_name;
    } else
      fn = (char *)filename;

    /* open output file for writing, can be disabled with define.
     * If file open fails -> exit */
    if(strcmp(filename, "none") != 0) {
      *fout = fopen(fn, "wb");
      if(*fout == NULL) {
        DEBUG_PRINT(("UNABLE TO OPEN OUTPUT FILE\n"));
        return;
      }
    }
#ifdef _ENABLE_2ND_CHROMA
    if (TBGetDecCh8PixIleavOutput(&tb_cfg) && !mono_chrome) {
      fchroma2 = fopen("out_ch8pix.yuv", "wb");
      if(fchroma2 == NULL) {
        DEBUG_PRINT(("UNABLE TO OPEN OUTPUT FILE\n"));
        return;
      }
    }
#endif
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
  if(tiled_mode && convert_tiled_output) {
    u32 eff_height = (pic_height + 15) & (~15);
    raster_scan = (u8*)malloc(pic_width*eff_height*3/2);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                     data, raster_scan, pic_width,
                     eff_height );
    if(!mono_chrome)
      TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                       data+pic_width*eff_height,
                       raster_scan+pic_width*eff_height, pic_width, eff_height/2 );
    data = raster_scan;
  } else if ( convert_to_frame_dpb && (dpb_mode != DEC_DPB_FRAME)) {
    u32 eff_height = (pic_height + 15) & (~15);
    raster_scan = (u8*)malloc(pic_width*eff_height*3/2);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBFieldDpbToFrameDpb( convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                          data, raster_scan, mono_chrome, pic_width, eff_height );

    data = raster_scan;
  }

  if(mono_chrome) {
    if(grey_chroma_size != (pic_size / 2)) {
      if(grey_chroma != NULL)
        free(grey_chroma);

      grey_chroma = (char *) malloc(pic_size / 2);
      if(grey_chroma == NULL) {
        DEBUG_PRINT(("UNABLE TO ALLOCATE GREYSCALE CHROMA BUFFER\n"));
        if(raster_scan)
          free(raster_scan);
        return;
      }
      grey_chroma_size = pic_size / 2;
      memset(grey_chroma, 128, grey_chroma_size);
    }
  }

  if(*fout == NULL || data == NULL) {
    return;
  }

#ifndef ASIC_TRACE_SUPPORT
  if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
    if(pic_big_endian_size != pic_size) {
      if(pic_big_endian != NULL)
        free(pic_big_endian);

      pic_big_endian = (char *) malloc(pic_size);
      if(pic_big_endian == NULL) {
        DEBUG_PRINT(("MALLOC FAILED @ %s %d", __FILE__, __LINE__));
        if(raster_scan)
          free(raster_scan);
        return;
      }

      pic_big_endian_size = pic_size;
    }

    memcpy(pic_big_endian, data, pic_size);
    TbChangeEndianess(pic_big_endian, pic_size);
    data = pic_big_endian;
  }
#endif

  if(md5sum) {
    TBWriteFrameMD5Sum(*fout, data, pic_size, frame_number);
  } else {
    /* this presumes output has system endianess */
    if(planar_output && !mono_chrome) {
      tmp = pic_size * 2 / 3;
      fwrite(data, 1, tmp, *fout);
      for(i = 0; i < tmp / 4; i++)
        fwrite(data + tmp + i * 2, 1, 1, *fout);
      for(i = 0; i < tmp / 4; i++)
        fwrite(data + tmp + 1 + i * 2, 1, 1, *fout);
    } else { /* semi-planar */
      fwrite(data, 1, pic_size, *fout);
      if(mono_chrome) {
        fwrite(grey_chroma, 1, grey_chroma_size, *fout);
      }
    }
  }

#ifdef _ENABLE_2ND_CHROMA
  if (TBGetDecCh8PixIleavOutput(&tb_cfg) && !mono_chrome) {
    u8 *p_ch;
    u32 tmp;
    H264DecRet ret;

    ret = H264DecNextChPicture(dec_inst, &p_ch, &tmp);
    ASSERT(ret == H264DEC_PIC_RDY);
#ifndef ASIC_TRACE_SUPPORT
    if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
      if(pic_big_endian_size != pic_size/3) {
        if(pic_big_endian != NULL)
          free(pic_big_endian);

        pic_big_endian = (char *) malloc(pic_size/3);
        if(pic_big_endian == NULL) {
          DEBUG_PRINT(("MALLOC FAILED @ %s %d", __FILE__, __LINE__));
          if(raster_scan)
            free(raster_scan);
          return;
        }

        pic_big_endian_size = pic_size/3;
      }

      memcpy(pic_big_endian, p_ch, pic_size/3);
      TbChangeEndianess(pic_big_endian, pic_size/3);
      p_ch = pic_big_endian;
    }
#endif

    if(md5sum)
      TBWriteFrameMD5Sum(fchroma2, p_ch, pic_size/3, frame_number);
    else
      fwrite(p_ch, 1, pic_size/3, fchroma2);
  }
#endif

  if(raster_scan)
    free(raster_scan);


}

/*------------------------------------------------------------------------------

    Function name: NextPacket

        Returns the packet size in bytes

------------------------------------------------------------------------------*/
u32 NextPacket(u8 ** p_strm)

{
  u32 length;

  if (enable_mvc) {
    /* libav does not support MVC */
    length = NextNALFromFile(finput, *p_strm, stream_mem->size);
  } else {
    length = libav_read_frame(*p_strm, stream_mem->size);
  }

  if (length)
    DEBUG_PRINT(("NextPacket = %d\n", length));

  return length;
}

/*------------------------------------------------------------------------------

    Function name: CropPicture

    Purpose:
        Perform cropping for picture. Input picture p_in_image with dimensions
        frame_width x frame_height is cropped with p_crop_params and the resulting
        picture is stored in p_out_image.

------------------------------------------------------------------------------*/
u32 CropPicture(u8 * p_out_image, u8 * p_in_image,
                u32 frame_width, u32 frame_height, H264CropParams * p_crop_params,
                u32 mono_chrome) {

  u32 i, j;
  u32 out_width, out_height;
  u8 *p_out, *p_in;

  if(p_out_image == NULL || p_in_image == NULL || p_crop_params == NULL ||
      !frame_width || !frame_height) {
    return (1);
  }

  if(((p_crop_params->crop_left_offset + p_crop_params->crop_out_width) >
      frame_width) ||
      ((p_crop_params->crop_top_offset + p_crop_params->crop_out_height) >
       frame_height)) {
    return (1);
  }

  out_width = p_crop_params->crop_out_width;
  out_height = p_crop_params->crop_out_height;

  /* Calculate starting pointer for luma */
  p_in = p_in_image + p_crop_params->crop_top_offset * frame_width +
         p_crop_params->crop_left_offset;
  p_out = p_out_image;

  /* Copy luma pixel values */
  for(i = out_height; i; i--) {
    for(j = out_width; j; j--) {
      *p_out++ = *p_in++;
    }
    p_in += frame_width - out_width;
  }

#if 0   /* planar */
  out_width >>= 1;
  out_height >>= 1;

  /* Calculate starting pointer for cb */
  p_in = p_in_image + frame_width * frame_height +
         p_crop_params->crop_top_offset * frame_width / 4 +
         p_crop_params->crop_left_offset / 2;

  /* Copy cb pixel values */
  for(i = out_height; i; i--) {
    for(j = out_width; j; j--) {
      *p_out++ = *p_in++;
    }
    p_in += frame_width / 2 - out_width;
  }

  /* Calculate starting pointer for cr */
  p_in = p_in_image + 5 * frame_width * frame_height / 4 +
         p_crop_params->crop_top_offset * frame_width / 4 +
         p_crop_params->crop_left_offset / 2;

  /* Copy cr pixel values */
  for(i = out_height; i; i--) {
    for(j = out_width; j; j--) {
      *p_out++ = *p_in++;
    }
    p_in += frame_width / 2 - out_width;
  }
#else /* semiplanar */

  if(mono_chrome)
    return 0;

  out_height >>= 1;

  /* Calculate starting pointer for chroma */
  p_in = p_in_image + frame_width * frame_height +
         (p_crop_params->crop_top_offset * frame_width / 2 +
          p_crop_params->crop_left_offset);

  /* Copy chroma pixel values */
  for(i = out_height; i; i--) {
    for(j = out_width; j; j -= 2) {
      *p_out++ = *p_in++;
      *p_out++ = *p_in++;
    }
    p_in += (frame_width - out_width);
  }

#endif

  return (0);
}

/*------------------------------------------------------------------------------

    Function name:  H264DecTrace

    Purpose:
        Example implementation of H264DecTrace function. Prototype of this
        function is given in H264DecApi.h. This implementation appends
        trace messages to file named 'dec_api.trc'.

------------------------------------------------------------------------------*/
void H264DecTrace(const char *string) {
  FILE *fp;

#if 0
  fp = fopen("dec_api.trc", "at");
#else
  fp = stderr;
#endif

  if(!fp)
    return;

  fprintf(fp, "%s", string);

  if(fp != stderr)
    fclose(fp);
}

/*------------------------------------------------------------------------------

    Function name:  bsdDecodeReturn

    Purpose: Print out decoder return value

------------------------------------------------------------------------------*/
static void printDecodeReturn(i32 retval) {

  DEBUG_PRINT(("TB: H264MCDecDecode returned: "));
  switch (retval) {

  case H264DEC_OK:
    DEBUG_PRINT(("H264DEC_OK\n"));
    break;
  case H264DEC_NONREF_PIC_SKIPPED:
    DEBUG_PRINT(("H264DEC_NONREF_PIC_SKIPPED\n"));
    break;
  case H264DEC_STRM_PROCESSED:
    DEBUG_PRINT(("H264DEC_STRM_PROCESSED\n"));
    break;
  case H264DEC_PIC_RDY:
    DEBUG_PRINT(("H264DEC_PIC_RDY\n"));
    break;
  case H264DEC_PIC_DECODED:
    DEBUG_PRINT(("H264DEC_PIC_DECODED\n"));
    break;
  case H264DEC_PENDING_FLUSH:
    DEBUG_PRINT(("H264DEC_PENDING_FLUSH\n"));
    break;
  case H264DEC_ADVANCED_TOOLS:
    DEBUG_PRINT(("H264DEC_ADVANCED_TOOLS\n"));
    break;
  case H264DEC_HDRS_RDY:
    DEBUG_PRINT(("H264DEC_HDRS_RDY\n"));
    break;
  case H264DEC_STREAM_NOT_SUPPORTED:
    DEBUG_PRINT(("H264DEC_STREAM_NOT_SUPPORTED\n"));
    break;
  case H264DEC_DWL_ERROR:
    DEBUG_PRINT(("H264DEC_DWL_ERROR\n"));
    break;
  case H264DEC_STRM_ERROR:
    DEBUG_PRINT(("H264DEC_STRM_ERROR\n"));
    break;
  case H264DEC_HW_TIMEOUT:
    DEBUG_PRINT(("H264DEC_HW_TIMEOUT\n"));
    break;
  default:
    DEBUG_PRINT(("Other %d\n", retval));
    break;
  }
}

/*------------------------------------------------------------------------------

    Function name:            printH264MCPicCodingType

    Functional description:   Print out picture coding type value

------------------------------------------------------------------------------*/
void printH264MCPicCodingType(u32 *pic_type) {
  printf("Coding type ");
  switch (pic_type[0]) {
  case DEC_PIC_TYPE_I:
    printf("[I:");
    break;
  case DEC_PIC_TYPE_P:
    printf("[P:");
    break;
  case DEC_PIC_TYPE_B:
    printf("[B:");
    break;
  default:
    printf("[Other %d:", pic_type[0]);
    break;
  }

  switch (pic_type[1]) {
  case DEC_PIC_TYPE_I:
    printf("I]");
    break;
  case DEC_PIC_TYPE_P:
    printf("P]");
    break;
  case DEC_PIC_TYPE_B:
    printf("B]");
    break;
  default:
    printf("Other %d]", pic_type[1]);
    break;
  }
}
