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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#ifndef MPEG2DEC_EXTERNAL_ALLOC_DISABLE
#include <fcntl.h>
#include <sys/mman.h>
#endif

#include "dwl.h"
#include "mpeg2decapi.h"
#ifdef USE_EFENCE
#include "efence.h"
#endif

#include "mpeg2hwd_container.h"
#include "regdrv_g1.h"

#ifdef PP_PIPELINE_ENABLED
#include "ppapi.h"
#include "pptestbench.h"
#endif

#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

#include "tb_cfg.h"
#include "tb_tiled.h"

#ifdef MD5SUM
#include "tb_md5.h"
#endif

#include "tb_sw_performance.h"
#include "tb_stream_corrupt.h"

/*#define DISABLE_WHOLE_STREAM_BUFFERING 0*/
#define DEFAULT -1
#define VOP_START_CODE 0xb6010000

/* Size of stream buffer */
#define STREAMBUFFER_BLOCKSIZE 0xfffff
#define MPEG2_WHOLE_STREAM_SAFETY_LIMIT (10*10*1024)
#define MAX_BUFFERS 32

#define MPEG2_FRAME_BUFFER_SIZE ((1280 * 720 * 3) / 2)  /* 720p frame */
#define MPEG2_NUM_BUFFERS 3 /* number of output buffers for ext alloc */
#define ASSERT(expr) assert(expr)

/* Function prototypes */

void printTimeCode(Mpeg2DecTime * timecode);
static u32 readDecodeUnit(FILE * fp, u8 * frame_buffer);
static void Error(char *format, ...);
void decRet(Mpeg2DecRet ret);
void decNextPictureRet(Mpeg2DecRet ret);
void printMpeg2Version(void);
i32 AllocatePicBuffers(Mpeg2DecLinearMem * buffer, DecContainer * container);
void printMpeg2PicCodingType(u32 pic_type);

void decsw_performance(void) {
}

void WriteOutput(char *filename, char *filename_tiled, u8 * data,
                 u32 frame_number, u32 width, u32 height, u32 interlaced,
                 u32 top, u32 first_field, u32 output_picture_endian,
                 Mpeg2DecPicture DecPicture, u32 coded_height, u32 tiled_mode);

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 frame_number, Mpeg2DecPicture DecPicture,
                    Mpeg2DecInst decoder);
#endif

/* stream start address */
u8 *byte_strm_start;
u32 b_frames;

/* stream used in SW decode */
u32 trace_used_stream = 0;
u32 previous_used = 0;

/* SW/SW testing, read stream trace file */
FILE *f_stream_trace = NULL;

/* output file */
FILE *fout = NULL;
FILE *f_tiled_output = NULL;
static u32 StartCode;

i32 strm_rew = 0;
u32 length;
u32 write_output = 1;
u32 crop_output = 0;
u8 disable_resync = 0;
u8 strm_end = 0;
u8 *stream_stop = NULL;

u32 stream_size = 0;
u32 stop_decoding = 0;
u32 stream_truncate = 0;
u32 stream_packet_loss = 0;
u32 hdrs_rdy = 0;
u32 pic_rdy = 0;
u32 stream_header_corrupt = 0;
struct TBCfg tb_cfg;

/* Give stream to decode as one chunk */
u32 whole_stream_mode = 0;
u32 cumulative_error_mbs = 0;

u32 planar_output = 0;
u32 b_frames;
u32 interlaced_field = 0;

u32 use_peek_output = 0;
u32 skip_non_reference = 0;

u32 pic_display_number = 0;
u32 frame_number = 0;

u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 dpb_mode = DEC_DPB_FRAME;
u32 convert_to_frame_dpb = 0;
u32 convert_tiled_output = 0;

FILE *findex = NULL;
u32 save_index = 0;
u32 use_index = 0;
off64_t cur_index = 0;
off64_t next_index = 0;
u32 ds_ratio_x, ds_ratio_y;

#if defined(ASIC_TRACE_SUPPORT) || defined(SYSTEM_VERIFICATION)
extern u32 use_mpeg2_idct;
#endif

#ifdef ASIC_TRACE_SUPPORT
extern u32 hw_dec_pic_count;
extern u32 g_hw_ver;
#endif

#ifdef MPEG2_EVALUATION
extern u32 g_hw_ver;
#endif

Mpeg2DecInst decoder;
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

u32 buffer_release_flag = 1;

u32 res_changed = 0;

static void *AddBufferThread(void *arg) {
  usleep(100000);
  while(add_buffer_thread_run) {
    pthread_mutex_lock(&ext_buffer_contro);
    if(add_extra_flag && num_buffers < MAX_BUFFERS) {
      struct DWLLinearMem mem;
      i32 dwl_ret;
      if (pp_enabled)
        DWLMallocLinear(dwl_inst, buffer_size, &mem);
      else
        DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
      if(dwl_ret == DWL_OK) {
        Mpeg2DecRet rv = Mpeg2DecAddBuffer(decoder, &mem);
        if(rv != MPEG2DEC_OK && rv != MPEG2DEC_WAITING_FOR_BUFFER) {
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
  printf("Releasing %d external frame buffers\n", num_buffers);
  pthread_mutex_lock(&ext_buffer_contro);
  for(i=0; i<num_buffers; i++) {
    printf("Freeing buffer %p\n", ext_buffers[i].virtual_address);
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
char out_file_name[256] = "";
char out_file_name_tiled[256] = "out_tiled.yuv";
Mpeg2DecInfo Decinfo;
u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
pthread_t output_thread;
pthread_t release_thread;
int output_thread_run = 0;

sem_t buf_release_sem;
Mpeg2DecPicture buf_list[100] = {0};
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
      Mpeg2DecPictureConsumed(decoder, &buf_list[list_pop_index]);
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
            dwl_ret = DWLMallocLinear(dwl_inst, buffer_size, &mem);
          else
            dwl_ret = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
          if(dwl_ret == DWL_OK) {
            Mpeg2DecRet rv = Mpeg2DecAddBuffer(decoder, &mem);
            if(rv != MPEG2DEC_OK && rv != MPEG2DEC_WAITING_FOR_BUFFER) {
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
    if(last_pic_flag &&  buf_status[list_pop_index] == 0)
      break;
    usleep(5000);
  }
  return NULL;
}


/* Output thread entry point. */
static void* mpeg2_output_thread(void* arg) {
  Mpeg2DecPicture DecPic;
  u32 pic_display_number = 1;
  u32 pic_size = 0;
  static u32 tmp_id = 0xFFFFFFFF;
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
    Mpeg2DecRet ret;
    ret = Mpeg2DecNextPicture(decoder, &DecPic, 0);
    if(ret == MPEG2DEC_PIC_RDY) {
      if(!use_peek_output) {
        /* print result */
        decNextPictureRet(ret);
        /* printf info */
        printf("PIC %d, %s", DecPic.pic_id,
               DecPic.key_picture ? "key picture,    " :
               "non key picture,");

        if(DecPic.field_picture) {
          /* pic coding type */
          if(DecPic.top_field)
            printMpeg2PicCodingType(DecPic.pic_coding_type[0]);
          else
            printMpeg2PicCodingType(DecPic.pic_coding_type[1]);
          printf(" %s ", DecPic.top_field ?
                 "top field.   " : "bottom field.");
        } else {
          printMpeg2PicCodingType(DecPic.pic_coding_type[0]);
          printf(" frame picture. ");
        }

        printTimeCode(&(DecPic.time_code));
        if(DecPic.number_of_err_mbs) {
          printf(", %d/%d error mbs\n",
                 DecPic.number_of_err_mbs,
                 (DecPic.frame_width >> 4) *
                 (DecPic.frame_height >> 4));
          cumulative_error_mbs += DecPic.number_of_err_mbs;
        }

        /* Write output picture to file */
        image_data = (u8 *) DecPic.output_picture;

        pic_size = DecPic.frame_width * DecPic.frame_height * 3 / 2;

        printf("DecPic.first_field %d\n", DecPic.first_field);
        WriteOutput(out_file_name, out_file_name_tiled, image_data,
                    pic_display_number - 1,
                    ((DecPic.frame_width + 15) & ~15),
                    ((DecPic.frame_height + 15) & ~15),
                    DecPic.interlaced,
                    DecPic.top_field, DecPic.first_field,
                    output_picture_endian, DecPic,
                    DecPic.coded_height,
                    DecPic.output_format);
      }
      if((tmp_id == DecPic.pic_id && DecPic.interlaced)
          || !DecPic.interlaced) {
        /* Push output buffer into buf_list and wait to be consumed */
        buf_list[list_push_index] = DecPic;
        buf_status[list_push_index] = 1;
        list_push_index++;
        if(list_push_index == 100)
          list_push_index = 0;

        sem_post(&buf_release_sem);

      }

      tmp_id = DecPic.pic_id;

      pic_display_number++;
    }

    else if(ret == MPEG2DEC_END_OF_STREAM) {
      last_pic_flag = 1;
#ifdef USE_EXTERNAL_BUFFER
      add_buffer_thread_run = 0;
#endif
      break;
    }
//        usleep(50000);
  }
  return NULL;
}
#endif



int main(int argc, char **argv) {
  FILE *f_tbcfg;
  u8 *p_strm_data = 0;
  u32 max_num_frames;   /* todo! */
  u32 num_frame_buffers = 0;
  u8 *image_data;
  u32 pic_size = 0;

  u32 i, stream_len = 0;
  u32 outp_byte_size;
  u32 vp_num = 0;
  u32 allocate_buffers = 0;    /* Allocate buffers in test bench */
  u32 buffer_selector = 0; /* Which frame buffer is in use */
  u32 disable_picture_freeze = 0;
  u32 rlc_mode = 0;
  struct DecDownscaleCfg dscale_cfg;

#ifdef ASIC_TRACE_SUPPORT
  u32 tmp = 0;
#endif
  /*
   * Decoder API structures
   */
#ifdef USE_EXTERNAL_BUFFER
  u32 prev_width = 0, prev_height = 0;
  u32 min_buffer_num = 0;
  Mpeg2DecBufferInfo hbuf;
  Mpeg2DecRet rv;
  memset(ext_buffers, 0, sizeof(ext_buffers));
  pthread_mutex_init(&ext_buffer_contro, NULL);
  struct DWLInitParam dwl_init;
#endif
  Mpeg2DecRet ret;
  Mpeg2DecRet info_ret;
  Mpeg2DecInput DecIn;
  Mpeg2DecOutput DecOut;
  Mpeg2DecPicture DecPic;
  struct DWLLinearMem stream_mem;
  u32 pic_id = 0;
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  char out_file_name[256] = "";
  char out_file_name_tiled[256] = "out_tiled.yuv";
  Mpeg2DecInfo Decinfo;
  u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
#endif
  char out_names[256] = "out_1.yuv";

  FILE *f_in = NULL;
  FILE *f_config = NULL;
  FILE *f_pip_mask = NULL;
  Mpeg2DecLinearMem pic_buffer[MPEG2_NUM_BUFFERS] = { 0 };

  u32 clock_gating = DEC_X170_INTERNAL_CLOCK_GATING;
  u32 data_discard = DEC_X170_DATA_DISCARD_ENABLE;
  u32 latency_comp = DEC_X170_LATENCY_COMPENSATION;
  u32 bus_burst_length = DEC_X170_BUS_BURST_LENGTH;
  u32 asic_service_priority = DEC_X170_ASIC_SERVICE_PRIORITY;
  u32 output_format = DEC_X170_OUTPUT_FORMAT;
  u32 service_merge_disable = DEC_X170_SERVICE_MERGE_DISABLE;

#ifdef PP_PIPELINE_ENABLED
  PPApiVersion pp_ver;
  PPBuild pp_build;
#endif

  u32 seed_rnd = 0;
  u32 stream_bit_swap = 0;
  i32 corrupted_bytes = 0;


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

  outp_byte_size = 0;

  INIT_SW_PERFORMANCE;

#ifdef ASIC_TRACE_SUPPORT
  g_hw_ver = 8190; /* default to 8190 mode */
#endif

#ifdef MPEG2_EVALUATION_8170
  g_hw_ver = 8170;
#elif MPEG2_EVALUATION_8190
  g_hw_ver = 8190;
#elif MPEG2_EVALUATION_9170
  g_hw_ver = 9170;
#elif MPEG2_EVALUATION_9190
  g_hw_ver = 9190;
#elif MPEG2_EVALUATION_G1
  g_hw_ver = 10000;
#endif

#ifndef PP_PIPELINE_ENABLED
  if(argc < 2) {

    printf("\n8170 MPEG-2 Decoder Testbench\n\n");
    printf("USAGE:\n%s [options] stream.mpeg2\n", argv[0]);
    printf("-Ooutfile write output to \"outfile\" (default out.yuv)\n");
    printf("-Nn to decode only first n frames of the stream\n");
    printf("-X to not to write output picture\n");
    printf("-Bn to use n frame buffers in decoder\n");
    printf("-E use tiled reference frame format.\n");
    printf("-G convert tiled output pictures to raster scan\n");
    printf("-Sfile.hex stream control trace file\n");
#if defined(ASIC_TRACE_SUPPORT) || defined(SYSTEM_VERIFICATION)
    printf("-R use reference decoder IDCT (sw/sw integration only)\n");
#endif
    printf("-W whole stream mode - give stream to decoder in one chunk\n");
    printf("-I save index file\n");
    printf
    ("-T write tiled output (out_tiled.yuv) by converting raster scan output\n");
    printf("-Y Write output as Interlaced Fields (instead of Frames).\n");
    printf
    ("-C crop output picture to real picture dimensions (only planar)\n");
    printf("-Q Skip decoding non-reference pictures.\n");
    printf("-Z output pictures using Mpeg2DecPeek() function\n");
    printf("--separate-fields-in-dpb DPB stores interlaced content"\
           " as fields (default: frames)\n");
    printf("--output-frame-dpb Convert output to frame mode even if"\
           " field DPB mode used\n");
#ifdef USE_EXTERNAL_BUFFER
    printf("-A add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
    printf("-a add extra external buffer in ouput thread\n");
#endif
#endif
    printMpeg2Version();
    exit(100);
  }

  remove("pp_out.yuv");
  max_num_frames = 0;
  for(i = 1; i < argc - 1; i++) {
    if(strncmp(argv[i], "-O", 2) == 0) {
      strcpy(out_file_name, argv[i] + 2);
    } else if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_frames = atoi(argv[i] + 2);
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if (strncmp(argv[i], "-G", 2) == 0)
      convert_tiled_output = 1;
    else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
      if(num_frame_buffers > MAX_BUFFERS)
        num_frame_buffers = MAX_BUFFERS;
    } else if(strncmp(argv[i], "-X", 2) == 0) {
      write_output = 0;
    } else if(strncmp(argv[i], "-S", 2) == 0) {
      f_stream_trace = fopen((argv[i] + 2), "r");
    } else if(strncmp(argv[i], "-P", 2) == 0) {
      planar_output = 1;
    } else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    }
#if defined(ASIC_TRACE_SUPPORT) || defined(SYSTEM_VERIFICATION)
    else if(strncmp(argv[i], "-R", 2) == 0) {
      use_mpeg2_idct = 1;
    }
#endif
    else if(strncmp(argv[i], "-W", 2) == 0) {
      whole_stream_mode = 1;
    } else if(strcmp(argv[i], "-Y") == 0) {
      interlaced_field = 1;
    } else if(strcmp(argv[i], "-C") == 0) {
      crop_output = 1;
    } else if(strcmp(argv[i], "-Z") == 0) {
      use_peek_output = 1;
    } else if(strcmp(argv[i], "-I") == 0) {
      save_index = 1;
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
    else if (strncmp(argv[i], "-d", 2) == 0) {
      if (strlen(argv[i]) == 3 &&
          (argv[i][2] >= '1' && argv[i][2] <= '3')) {
        ds_ratio_x = ds_ratio_y = argv[i][2] - '0';
      } else if ((strlen(argv[i]) == 5) &&
                 ((argv[i][2] >= '1' && argv[i][2] <= '3') &&
                  (argv[i][4] >= '1' && argv[i][2] <= '4') &&
                  argv[i][3] == ':')) {
        ds_ratio_x = argv[i][2] - '0';
        ds_ratio_y = argv[i][4] - '0';
      } else {
        printf("Illegal parameter: %s\n", argv[i]);
        return 1;
      }
    } else {
      printf("UNKNOWN PARAMETER: %s\n", argv[i]);
      return 1;
    }
  }

  printMpeg2Version();
  /* open data file */
  f_in = fopen(argv[argc - 1], "rb");
  if(f_in == NULL) {
    printf("Unable to open input file %s\n", argv[argc - 1]);
    exit(100);
  }
#else
  if(argc < 3) {
    printf("\nMpeg-2 Decoder PP Pipelined Testbench\n\n");
    printf("USAGE:\n%s [options] stream.mpeg2 pp.cfg\n", argv[0]);
    printf("-Nn to decode only first n vops of the stream\n");
    printf("-E use tiled reference frame format.\n");
    printf("-Bn to use n frame buffers in decoder\n");
    printf("-X to not to write output picture\n");
    printf("-W whole stream mode - give stream to decoder in one chunk\n");
    printf("-I save index file\n");
    printf("-Q Skip decoding non-reference pictures.\n");
    printf("--separate-fields-in-dpb DPB stores interlaced content"\
           " as fields (default: frames)\n");
#ifdef USE_EXTERNAL_BUFFER
    printf("-A add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
    printf("-a add extra external buffer in output thread\n");
#endif
#endif
    exit(100);
  }

  remove("pp_out.yuv");
  max_num_frames = 0;
  /* read cmdl parameters */
  for(i = 1; i < argc - 2; i++) {
    if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_frames = atoi(argv[i] + 2);
    } else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
      if(num_frame_buffers > MAX_BUFFERS)
        num_frame_buffers = MAX_BUFFERS;
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if(strncmp(argv[i], "-X", 2) == 0) {
      write_output = 0;
    } else if(strncmp(argv[i], "-W", 2) == 0) {
      whole_stream_mode = 1;
    } else if(strcmp(argv[i], "-I") == 0) {
      save_index = 1;
    } else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    } else if(strcmp(argv[i], "--separate-fields-in-dpb") == 0) {
      dpb_mode = DEC_DPB_INTERLACED_FIELD;
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
    else if (strncmp(argv[i], "-d", 2) == 0) {
      if (strlen(argv[i]) == 3 &&
          (argv[i][2] >= '1' && argv[i][2] <= '3')) {
        ds_ratio_x = ds_ratio_y = argv[i][2] - '0';
      } else if (strlen(argv[i] == 5) &&
                 ((argv[i][2] >= '1' && argv[i][2] <= '3') &&
                  (argv[i][4] >= '1' && argv[i][2] <= '4') &&
                  argv[i][3] == ':')) {
        ds_ratio_x = argv[i][2] - '0';
        ds_ratio_y = argv[i][4] - '0';
      } else {
        printf("Illegal parameter: %s\n", argv[i]);
        return 1;
      }
    } else {
      fprintf(stdout, "UNKNOWN PARAMETER: %s\n", argv[i]);
      return 1;
    }
  }

  printMpeg2Version();
  /* open data file */
  f_in = fopen(argv[argc - 2], "rb");
  if(f_in == NULL) {
    printf("Unable to open input file %s\n", argv[argc - 2]);
    exit(100);
  }

  /* Print API and build version numbers */
  pp_ver = PPGetAPIVersion();
  pp_build = PPGetBuild();

  /* Version */
  fprintf(stdout,
          "\n8170 PP API v%d.%d - SW build: %d - HW build: %x\n",
          pp_ver.major, pp_ver.minor, pp_build.sw_build, pp_build.hw_build);
#endif

  /* open index file for saving */
  if(save_index) {
    findex = fopen("stream.cfg", "w");
    if(findex == NULL) {
      printf("UNABLE TO OPEN INDEX FILE\n");
      return -1;
    }
  } else {
    findex = fopen("stream.cfg", "r");
    if(findex != NULL) {
      use_index = 1;
    }
  }

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
  clock_gating = TBGetDecClockGating(&tb_cfg);
  data_discard = TBGetDecDataDiscard(&tb_cfg);
  latency_comp = tb_cfg.dec_params.latency_compensation;
  output_picture_endian = TBGetDecOutputPictureEndian(&tb_cfg);
  bus_burst_length = tb_cfg.dec_params.bus_burst_length;
  asic_service_priority = tb_cfg.dec_params.asic_service_priority;
  output_format = TBGetDecOutputFormat(&tb_cfg);
  service_merge_disable = TBGetDecServiceMergeDisable(&tb_cfg);
  /*#if MD5SUM
      output_picture_endian = DEC_X170_LITTLE_ENDIAN;
      printf("Decoder Output Picture Endian forced to %d\n",
             output_picture_endian);
  #endif*/
  printf("Decoder Clock Gating %d\n", clock_gating);
  printf("Decoder Data Discard %d\n", data_discard);
  printf("Decoder Latency Compensation %d\n", latency_comp);
  printf("Decoder Output Picture Endian %d\n", output_picture_endian);
  printf("Decoder Bus Burst Length %d\n", bus_burst_length);
  printf("Decoder Asic Service Priority %d\n", asic_service_priority);
  printf("Decoder Output Format %d\n", output_format);

  seed_rnd = tb_cfg.tb_params.seed_rnd;
  stream_header_corrupt = TBGetTBStreamHeaderCorrupt(&tb_cfg);
  /* if headers are to be corrupted
    -> do not wait the picture to finalize before starting stream corruption */
  if (stream_header_corrupt)
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
  disable_resync = TBGetTBPacketByPacket(&tb_cfg);
  printf("TB Slice by slice %d\n", disable_resync);
  printf("TB Seed Rnd %d\n", seed_rnd);
  printf("TB Stream Truncate %d\n", stream_truncate);
  printf("TB Stream Header Corrupt %d\n", stream_header_corrupt);
  printf("TB Stream Bit Swap %d; odds %s\n",
         stream_bit_swap, tb_cfg.tb_params.stream_bit_swap);
  printf("TB Stream Packet Loss %d; odds %s\n",
         stream_packet_loss, tb_cfg.tb_params.stream_packet_loss);

  /* allocate memory for stream buffer. if unsuccessful -> exit */
  stream_mem.virtual_address = NULL;
  stream_mem.bus_address = 0;
  stream_mem.mem_type = DWL_MEM_TYPE_CPU;

  length = STREAMBUFFER_BLOCKSIZE;

  rewind(f_in);

  TBInitializeRandom(seed_rnd);

  /* check size of the input file -> length of the stream in bytes */
  fseek(f_in, 0L, SEEK_END);
  stream_size = (u32) ftell(f_in);
  rewind(f_in);

  /* sets the stream length to random value */
  if(stream_truncate && !disable_resync) {
    printf("stream_size %d\n", stream_size);
    ret = TBRandomizeU32(&stream_size);
    if(ret != 0) {
      printf("RANDOM STREAM ERROR FAILED\n");
      return -1;
    }
    printf("Randomized stream_size %d\n", stream_size);
  }

#ifdef ASIC_TRACE_SUPPORT
  tmp = openTraceFiles();
  if(!tmp) {
    printf("UNABLE TO OPEN TRACE FILES(S)\n");
  }
#endif

#ifdef USE_EXTERNAL_BUFFER
  dwl_init.client_type = DWL_CLIENT_TYPE_MPEG2_DEC;

  dwl_inst = DWLInit(&dwl_init);

  if(dwl_inst == NULL) {
    fprintf(stdout, ("ERROR: DWL Init failed"));
    goto end2;
  }
#endif
  dscale_cfg.down_scale_x = ds_ratio_x;
  dscale_cfg.down_scale_y = ds_ratio_y;

  /* Initialize the decoder */
  START_SW_PERFORMANCE;
  decsw_performance();
  ret = Mpeg2DecInit(&decoder,
#ifdef USE_EXTERNAL_BUFFER
                     dwl_inst,
#endif
                     TBGetDecErrorConcealment( &tb_cfg ),
                     num_frame_buffers,
                     tiled_output |
                     (dpb_mode == DEC_DPB_INTERLACED_FIELD ? DEC_DPB_ALLOW_FIELD_ORDERING : 0), 0, 0, &dscale_cfg);

  END_SW_PERFORMANCE;
  decsw_performance();

#ifdef PP_PIPELINE_ENABLED
  /* Initialize the post processer. If unsuccessful -> exit */
  if(pp_startup
      (argv[argc - 1], decoder, PP_PIPELINED_DEC_TYPE_MPEG2, &tb_cfg) != 0) {
    fprintf(stdout, "PP INITIALIZATION FAILED\n");
    goto end2;
  }
#ifdef USE_EXTERNAL_BUFFER
  if(pp_update_config
      (decoder, PP_PIPELINED_DEC_TYPE_MPEG2, &tb_cfg) == CFG_UPDATE_FAIL) {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
    goto end2;
  }
#endif
#endif

  if(ret != MPEG2DEC_OK) {
    printf("Could not initialize decoder\n");
    goto end2;
  }

  /* Set ref buffer test mode */
  ((DecContainer *) decoder)->ref_buffer_ctrl.test_function = TBRefbuTestMode;

  if(DWLMallocLinear(((DecContainer *) decoder)->dwl,
                     STREAMBUFFER_BLOCKSIZE, &stream_mem) != DWL_OK) {
    printf(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
    goto end2;
  }

  /* allocate output buffers if necessary */
  if(allocate_buffers) {
    if(AllocatePicBuffers(pic_buffer, (DecContainer *) decoder))
      goto end2;
  }
  byte_strm_start = (u8 *) stream_mem.virtual_address;

  DecIn.skip_non_reference = skip_non_reference;
  DecIn.stream = byte_strm_start;
  DecIn.stream_bus_address = stream_mem.bus_address;

  if(byte_strm_start == NULL) {
    printf(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
    goto end2;
  }

  stream_stop = byte_strm_start + length;
  /* NOTE: The registers should not be used outside decoder SW for other
   * than compile time setting test purposes */
  SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  /*
  SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_DEC_OUT_TILED_E,
                 output_format);
                 */
  SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_DEC_OUT_ENDIAN,
                 output_picture_endian);
  SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_DEC_MAX_BURST,
                 bus_burst_length);
  if ((DWLReadAsicID(DWL_CLIENT_TYPE_MPEG2_DEC) >> 16) == 0x8170U) {
    SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_PRIORITY_MODE,
                   asic_service_priority);
  }
  SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_DEC_DATA_DISC_E,
                 data_discard);
  SetDecRegister(((DecContainer *) decoder)->mpeg2_regs, HWIF_SERV_MERGE_DIS,
                 service_merge_disable);

  /* Read what kind of stream is coming */
  START_SW_PERFORMANCE;
  decsw_performance();
  info_ret = Mpeg2DecGetInfo(decoder, &Decinfo);
  END_SW_PERFORMANCE;
  decsw_performance();
  if(info_ret) {
    decRet(info_ret);
  }

  p_strm_data = (u8 *) DecIn.stream;

  /* Read sequence headers */
  stream_len = readDecodeUnit(f_in, p_strm_data);

  i = StartCode;
  /* decrease 4 because previous function call
   * read the first sequence start code */

  stream_len -= 4;
  DecIn.data_len = stream_len;
  DecOut.data_left = 0;

  printf("Start decoding\n");
  do {
    printf("DecIn.data_len %d\n", DecIn.data_len);
    DecIn.pic_id = pic_id;
    if(ret != MPEG2DEC_STRM_PROCESSED &&
        ret != MPEG2DEC_BUF_EMPTY &&
        ret != MPEG2DEC_NO_DECODING_BUFFER &&
        ret != MPEG2DEC_NONREF_PIC_SKIPPED )
      printf("\nStarting to decode picture ID %d\n", pic_id);

    /* If enabled, break the stream */
    if(stream_bit_swap) {
      if((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt) {
        /* Picture needs to be ready before corrupting next picture */
        if(pic_rdy && corrupted_bytes <= 0) {
          ret = TBRandomizeBitSwapInStream(DecIn.stream,
                                           DecIn.data_len,
                                           tb_cfg.tb_params.
                                           stream_bit_swap);
          if(ret != 0) {
            printf("RANDOM STREAM ERROR FAILED\n");
            goto end2;
          }

          corrupted_bytes = DecIn.data_len;
          printf("corrupted_bytes %d\n", corrupted_bytes);
        }
      }
    }

    assert(DecOut.data_left == DecIn.data_len || !DecOut.data_left);

    START_SW_PERFORMANCE;
    decsw_performance();
    ret = Mpeg2DecDecode(decoder, &DecIn, &DecOut);
    END_SW_PERFORMANCE;
    decsw_performance();

    decRet(ret);

    /*
     * Choose what to do now based on the decoder return value
     */

    switch (ret) {

    case MPEG2DEC_HDRS_RDY:

      /* Set a flag to indicate that headers are ready */
      hdrs_rdy = 1;
      TBSetRefbuMemModel( &tb_cfg,
                          ((DecContainer *) decoder)->mpeg2_regs,
                          &((DecContainer *) decoder)->ref_buffer_ctrl );

      /* Read what kind of stream is coming */
      START_SW_PERFORMANCE;
      decsw_performance();
      info_ret = Mpeg2DecGetInfo(decoder, &Decinfo);
      END_SW_PERFORMANCE;
      decsw_performance();
      if(info_ret) {
        decRet(info_ret);
      }
      outp_byte_size =
        (Decinfo.frame_width * Decinfo.frame_height * 3) >> 1;

      if (Decinfo.interlaced_sequence)
        printf("INTERLACED SEQUENCE\n");
#ifdef USE_EXTERNAL_BUFFER
      if(Decinfo.pic_buff_size != min_buffer_num ||
          (Decinfo.frame_width * Decinfo.frame_height > prev_width * prev_height)) {
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
      prev_width = Decinfo.frame_width;
      prev_height = Decinfo.frame_height;
      min_buffer_num = Decinfo.pic_buff_size;
#endif
      //dpb_mode = Decinfo.dpb_mode;

      /* If -O option not used, generate default file name */
      if(out_file_name[0] == 0) {
        if(planar_output && crop_output)
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  Decinfo.coded_width, Decinfo.coded_height);
        else
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  Decinfo.frame_width, Decinfo.frame_height);
      }

      if(!frame_number) {
        /*disable_resync = 0; */
        if(Decinfo.stream_format == MPEG2)
          printf("MPEG-2 stream\n");
        else
          printf("MPEG-1 stream\n");

        printf("Profile and level %d\n",
               Decinfo.profile_and_level_indication);
        switch (Decinfo.display_aspect_ratio) {
        case MPEG2DEC_1_1:
          printf("Display Aspect ratio 1:1\n");
          break;
        case MPEG2DEC_4_3:
          printf("Display Aspect ratio 4:3\n");
          break;
        case MPEG2DEC_16_9:
          printf("Display Aspect ratio 16:9\n");
          break;
        case MPEG2DEC_2_21_1:
          printf("Display Aspect ratio 2.21:1\n");
          break;
        }
        printf("Output format %s\n",
               Decinfo.output_format == MPEG2DEC_SEMIPLANAR_YUV420
               ? "MPEG2DEC_SEMIPLANAR_YUV420" :
               "MPEG2DEC_TILED_YUV420");
      }

      printf("DecOut.data_left %d \n", DecOut.data_left);
      if(DecOut.data_left) {
        corrupted_bytes -= (DecIn.data_len - DecOut.data_left);
        DecIn.data_len = DecOut.data_left;
        DecIn.stream = DecOut.strm_curr_pos;
        DecIn.stream_bus_address = DecOut.strm_curr_bus_address;
      } else {
        *(u32 *) p_strm_data = StartCode;
        DecIn.stream = (u8 *) p_strm_data;
        DecIn.stream_bus_address = stream_mem.bus_address;

        if(strm_end) {
          /* stream ended */
          stream_len = 0;
          DecIn.stream = NULL;
        } else {
          /*u32 streamPacketLossTmp = stream_packet_loss;

          if(!pic_rdy)
              stream_packet_loss = 0;*/
          stream_len = readDecodeUnit(f_in, p_strm_data + 4);
          /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;

        corrupted_bytes = 0;

      }

#ifdef PP_PIPELINE_ENABLED
      pp_set_input_interlaced(Decinfo.interlaced_sequence);
#ifndef USE_EXTERNAL_BUFFER
      if(pp_update_config
          (decoder, PP_PIPELINED_DEC_TYPE_MPEG2, &tb_cfg) == CFG_UPDATE_FAIL) {
        fprintf(stdout, "PP CONFIG LOAD FAILED\n");
        goto end2;
      }
#endif
      /*
                  DecIn.enableDeblock = pp_mpeg4_filter_used();
                  if(DecIn.enableDeblock == 1)
                      printf("Deblocking filter enabled\n");
                  else if(DecIn.enableDeblock == 0)
                      printf("Deblocking filter disabled\n");
                      */
      /* If unspecified at cmd line, use minimum # of buffers, otherwise
       * use specified amount. */
      if(num_frame_buffers == 0)
        pp_number_of_buffers(Decinfo.multi_buff_pp_size);
      else
        pp_number_of_buffers(num_frame_buffers);
#endif
      break;

#ifdef USE_EXTERNAL_BUFFER
    case MPEG2DEC_WAITING_FOR_BUFFER:
      rv = Mpeg2DecGetBufferInfo(decoder, &hbuf);
      printf("MREG2DecGetBufferInfo ret %d\n", rv);
      printf("buf_to_free %p, next_buf_size %d, buf_num %d\n",
             (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num);
      if (hbuf.buf_to_free.virtual_address != NULL && res_changed) {
        add_extra_flag = 0;
        ReleaseExtBuffers();
        buffer_release_flag = 1;
        num_buffers = 0;
        res_changed = 0;
      }
      if(buffer_release_flag && hbuf.next_buf_size) {
        /* Only add minimum required buffers at first. */
        //extra_buffer_num = hbuf.buf_num - min_buffer_num;
        buffer_size = hbuf.next_buf_size;
        struct DWLLinearMem mem;
        mem.mem_type = DWL_MEM_TYPE_CPU;
        for(i=0; i<hbuf.buf_num; i++) {
          if (pp_enabled)
            DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
          else
            DWLMallocRefFrm(dwl_inst, hbuf.next_buf_size, &mem);
          rv = Mpeg2DecAddBuffer(decoder, &mem);
          printf("Mpeg2DecAddBuffer ret %d\n", rv);
          if(rv != MPEG2DEC_OK && rv != MPEG2DEC_WAITING_FOR_BUFFER) {
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
      break;
#endif

    case MPEG2DEC_PIC_DECODED:
      /* Picture is ready */
      pic_rdy = 1;
      pic_id++;
#if 0
      if (pic_id == 10) {
        info_ret = Mpeg2DecAbort(decoder);
        info_ret = Mpeg2DecAbortAfter(decoder);
        pic_id = 0;
        rewind(f_in);

        /* Read sequence headers */
        stream_len = readDecodeUnit(f_in, p_strm_data);

        i = StartCode;
        /* decrease 4 because previous function call
         * read the first sequence start code */

        stream_len -= 4;
        DecIn.data_len = stream_len;
        DecIn.stream = byte_strm_start;
        DecIn.stream_bus_address = stream_mem.bus_address;
        DecOut.data_left = 0;
        break;
      }
#endif

      /* Read what kind of stream is coming */
      info_ret = Mpeg2DecGetInfo(decoder, &Decinfo);
      if(info_ret) {
        decRet(info_ret);
      }
#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
      if (!output_thread_run) {
        output_thread_run = 1;
        sem_init(&buf_release_sem, 0, 0);
        pthread_create(&output_thread, NULL, mpeg2_output_thread, NULL);
        pthread_create(&release_thread, NULL, buf_release_thread, NULL);
      }
#endif

      if (use_peek_output &&
          Mpeg2DecPeek(decoder, &DecPic) == MPEG2DEC_PIC_RDY) {
        pic_display_number++;
        printf("DECPIC %d, %s", DecPic.pic_id,
               DecPic.key_picture ? "key picture,    " :
               "non key picture,");

        /* pic coding type */
        printMpeg2PicCodingType(DecPic.pic_coding_type[0]);

        image_data = (u8 *) DecPic.output_picture;

        pic_size = DecPic.frame_width * DecPic.frame_height * 3 / 2;

        WriteOutput(out_file_name, out_file_name_tiled, image_data,
                    pic_display_number - 1,
                    ((Decinfo.frame_width + 15) & ~15),
                    ((Decinfo.frame_height + 15) & ~15),
                    Decinfo.interlaced_sequence,
                    DecPic.top_field, DecPic.first_field,
                    output_picture_endian, DecPic,
                    Decinfo.coded_height,
                    DecPic.output_format);
      }

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      /* loop to get all pictures/fields out from decoder */
      do {
        START_SW_PERFORMANCE;
        decsw_performance();
        info_ret = Mpeg2DecNextPicture(decoder, &DecPic, 0);
        END_SW_PERFORMANCE;
        decsw_performance();

        if (!use_peek_output) {
          /* print result */
          decNextPictureRet(info_ret);

          /* Increment display number for every displayed picture */
          pic_display_number++;

          if(info_ret == MPEG2DEC_PIC_RDY) {
            /* printf info */
            printf("PIC %d, %s", DecPic.pic_id,
                   DecPic.key_picture ? "key picture,    " :
                   "non key picture,");

            if(DecPic.field_picture) {
              /* pic coding type */
              if(DecPic.top_field)
                printMpeg2PicCodingType(DecPic.pic_coding_type[0]);
              else
                printMpeg2PicCodingType(DecPic.pic_coding_type[1]);
              printf(" %s ", DecPic.top_field ?
                     "top field.   " : "bottom field.");
            } else {
              printMpeg2PicCodingType(DecPic.pic_coding_type[0]);
              printf(" frame picture. ");
            }

            printTimeCode(&(DecPic.time_code));
            if(DecPic.number_of_err_mbs) {
              printf(", %d/%d error mbs\n",
                     DecPic.number_of_err_mbs,
                     (DecPic.frame_width >> 4) *
                     (DecPic.frame_height >> 4));
              cumulative_error_mbs += DecPic.number_of_err_mbs;
            }

            /* Write output picture to file */
            image_data = (u8 *) DecPic.output_picture;

            pic_size = DecPic.frame_width * DecPic.frame_height * 3 / 2;

#ifndef PP_PIPELINE_ENABLED
            printf("DecPic.first_field %d\n", DecPic.first_field);
            WriteOutput(out_file_name, out_file_name_tiled, image_data,
                        pic_display_number - 1,
                        ((Decinfo.frame_width + 15) & ~15),
                        ((Decinfo.frame_height + 15) & ~15),
                        Decinfo.interlaced_sequence,
                        DecPic.top_field, DecPic.first_field,
                        output_picture_endian, DecPic,
                        Decinfo.coded_height,
                        DecPic.output_format);
#else

            HandlePpOutput(frame_number - 1, DecPic, decoder);
#endif
          }
        }
      } while(info_ret == MPEG2DEC_PIC_RDY);
#endif

      frame_number++;
      vp_num = 0;

#ifdef USE_EXTERNAL_BUFFER
      if(use_extra_buffers && !add_buffer_thread_run) {
        add_buffer_thread_run = 1;
        pthread_create(&add_buffer_thread, NULL, AddBufferThread, NULL);
      }
#endif
      printf("DecOut.data_left %d \n", DecOut.data_left);
      if(DecOut.data_left) {
        corrupted_bytes -= (DecIn.data_len - DecOut.data_left);
        DecIn.data_len = DecOut.data_left;
        DecIn.stream = DecOut.strm_curr_pos;
        DecIn.stream_bus_address = DecOut.strm_curr_bus_address;
      } else {

        *(u32 *) p_strm_data = StartCode;
        DecIn.stream = (u8 *) p_strm_data;
        DecIn.stream_bus_address = stream_mem.bus_address;

        if(strm_end) {
          stream_len = 0;
          DecIn.stream = NULL;
        } else {
          /*u32 streamPacketLossTmp = stream_packet_loss;

          if(!pic_rdy)
              stream_packet_loss = 0;*/
          stream_len = readDecodeUnit(f_in, p_strm_data + 4);
          /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;

        corrupted_bytes = 0;

      }

      if(max_num_frames && (frame_number >= max_num_frames)) {
        printf("\n\nMax num of pictures reached\n\n");
        DecIn.data_len = 0;
        goto end2;
      }

      break;

    case MPEG2DEC_STRM_PROCESSED:
    case MPEG2DEC_BUF_EMPTY:
    case MPEG2DEC_NONREF_PIC_SKIPPED:
      fprintf(stdout,
              "TB: Frame Number: %u, pic: %d\n", vp_num++, frame_number);
    /* Used to indicate that picture decoding needs to
     * finalized prior to corrupting next picture */
#ifdef GET_FREE_BUFFER_NON_BLOCK
    case MPEG2DEC_NO_DECODING_BUFFER:
    /* Just for simulation: if no buffer, sleep 0.5 second and try decoding again. */
#endif

    /* fallthrough */

    case MPEG2DEC_OK:

      /* Read what kind of stream is coming */
      START_SW_PERFORMANCE;
      decsw_performance();
      info_ret = Mpeg2DecGetInfo(decoder, &Decinfo);
      END_SW_PERFORMANCE;
      decsw_performance();

      if(info_ret) {
        decRet(info_ret);
      }

      /*
       **  Write output picture to the file
       */

      /*
       *    Read next decode unit. Because readDecodeUnit
       *   reads VOP start code in previous
       *   function call, Insert this start code
       *   in to first word
       *   of stream buffer, and increase
       *   stream buffer pointer by 4 in
       *   the function call.
       */

      printf("DecOut.data_left %d \n", DecOut.data_left);
      if(DecOut.data_left) {
        corrupted_bytes -= (DecIn.data_len - DecOut.data_left);
        DecIn.data_len = DecOut.data_left;
        DecIn.stream = DecOut.strm_curr_pos;
        DecIn.stream_bus_address = DecOut.strm_curr_bus_address;
      } else {

        *(u32 *) p_strm_data = StartCode;
        DecIn.stream = (u8 *) p_strm_data;
        DecIn.stream_bus_address = stream_mem.bus_address;

        if(strm_end) {
          stream_len = 0;
          DecIn.stream = NULL;
        } else {
          /*u32 streamPacketLossTmp = stream_packet_loss;

          if(!pic_rdy)
              stream_packet_loss = 0;*/
          stream_len = readDecodeUnit(f_in, p_strm_data + 4);
          /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;

        corrupted_bytes = 0;

      }

      break;

    case MPEG2DEC_PARAM_ERROR:
      printf("INCORRECT STREAM PARAMS\n");
      goto end2;
      break;

    case MPEG2DEC_STRM_ERROR:
      printf("STREAM ERROR\n");

      printf("DecOut.data_left %d \n", DecOut.data_left);
      if(DecOut.data_left) {
        corrupted_bytes -= (DecIn.data_len - DecOut.data_left);
        DecIn.data_len = DecOut.data_left;
        DecIn.stream = DecOut.strm_curr_pos;
        DecIn.stream_bus_address = DecOut.strm_curr_bus_address;
      } else {

        *(u32 *) p_strm_data = StartCode;
        DecIn.stream = (u8 *) p_strm_data;
        DecIn.stream_bus_address = stream_mem.bus_address;

        if(strm_end) {
          stream_len = 0;
          DecIn.stream = NULL;
        } else {
          /*u32 streamPacketLossTmp = stream_packet_loss;

          if(!pic_rdy)
              stream_packet_loss = 0;*/
          stream_len = readDecodeUnit(f_in, p_strm_data + 4);
          /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;

      }
      break;

    default:
      decRet(ret);
      goto end2;
    }
    /*
     * While there is stream
     */

  } while(DecIn.data_len > 0);
end2:

  /* Output buffered images also... */
  START_SW_PERFORMANCE;
  decsw_performance();
#ifdef USE_EXTERNAL_BUFFER
  add_buffer_thread_run = 0;
#endif

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  while(!use_peek_output &&
        Mpeg2DecNextPicture(decoder, &DecPic, 1) == MPEG2DEC_PIC_RDY) {
    /* Increment display number for every displayed picture */
    pic_display_number++;

#ifndef PP_PIPELINE_ENABLED
    /* Write output picture to file */
    image_data = (u8 *) DecPic.output_picture;

    pic_size = DecPic.frame_width * DecPic.frame_height * 3 / 2;

    WriteOutput(out_file_name, out_file_name_tiled, image_data,
                pic_display_number - 1,
                ((Decinfo.frame_width + 15) & ~15),
                ((Decinfo.frame_height + 15) & ~15),
                Decinfo.interlaced_sequence,
                DecPic.top_field, DecPic.first_field,
                output_picture_endian, DecPic, Decinfo.coded_height,
                DecPic.output_format);
#else
    HandlePpOutput(frame_number - 1, DecPic, decoder);
#endif
  }

#else
  if(output_thread_run)
    Mpeg2DecEndOfStream(decoder, 1);
  if(output_thread)
    pthread_join(output_thread, NULL);
  if(release_thread)
    pthread_join(release_thread, NULL);
#endif

  END_SW_PERFORMANCE;
  decsw_performance();

  START_SW_PERFORMANCE;
  decsw_performance();
  Mpeg2DecGetInfo(decoder, &Decinfo);
  END_SW_PERFORMANCE;
  decsw_performance();

  /*
   * Release the decoder
   */
#ifdef PP_PIPELINE_ENABLED
  pp_close();
#endif
  if(stream_mem.virtual_address)
    DWLFreeLinear(((DecContainer *) decoder)->dwl, &stream_mem);

  START_SW_PERFORMANCE;
  decsw_performance();
#ifdef USE_EXTERNAL_BUFFER
  ReleaseExtBuffers();
  pthread_mutex_destroy(&ext_buffer_contro);
#endif
  Mpeg2DecRelease(decoder);
#ifdef USE_EXTERNAL_BUFFER
  DWLRelease(dwl_inst);
#endif
  END_SW_PERFORMANCE;
  decsw_performance();

  if(Decinfo.frame_width < 1921)
    printf("\nWidth %d Height %d\n", Decinfo.frame_width,
           Decinfo.frame_height);
  if(cumulative_error_mbs) {
    printf("Cumulative errors: %d/%d macroblocks, ",
           cumulative_error_mbs,
           (Decinfo.frame_width >> 4) * (Decinfo.frame_height >> 4) *
           frame_number);
  }
  printf("decoded %d pictures\n", frame_number);

  if(f_in)
    fclose(f_in);

  if(fout)
    fclose(fout);

  if(f_tiled_output)
    fclose(f_tiled_output);

#ifdef ASIC_TRACE_SUPPORT
  trace_SequenceCtrl(hw_dec_pic_count, b_frames);
  trace_RefbufferHitrate();
  trace_MPEG2DecodingTools();
  closeTraceFiles();
#endif

  if(save_index || use_index) {
    fclose(findex);
  }

  /* Calculate the output size and print it  */
  fout = fopen(out_file_name, "rb");
  if(NULL == fout) {
    stream_len = 0;
  } else {
    fseek(fout, 0L, SEEK_END);
    stream_len = (u32) ftell(fout);
    fclose(fout);
  }

#ifndef PP_PIPELINE_ENABLED
  printf("output size %d\n", stream_len);
#endif

  FINALIZE_SW_PERFORMANCE;

  if(cumulative_error_mbs || !frame_number) {
    printf("ERRORS FOUND\n");
    return (1);
  } else
    return (0);

}

/*------------------------------------------------------------------------------
        readDecodeUnit
        Description : search pic start code and read one decode unit at a time
------------------------------------------------------------------------------*/
static u32 readDecodeUnit(FILE * fp, u8 * frame_buffer) {

  u32 idx = 0, VopStart = 0;
  u8 temp;
  u8 next_packet = 0;
  int ret;

  StartCode = 0;

  if(stop_decoding) {
    printf("Truncated stream size reached -> stop decoding\n");
    return 0;
  }

  /* If enabled, loose the packets (skip this packet first though) */
  if(stream_packet_loss && disable_resync) {
    u32 ret = 0;

    ret =
      TBRandomizePacketLoss(tb_cfg.tb_params.stream_packet_loss, &next_packet);
    if(ret != 0) {
      printf("RANDOM STREAM ERROR FAILED\n");
      return 0;
    }
  }

  if(use_index) {
    u32 amount = 0;

    /* get next index */
#ifdef USE_64BIT_ENV
    ret = fscanf(findex, "%lu", &next_index);
#else
    ret = fscanf(findex, "%llu", &next_index);
#endif
    amount = next_index - cur_index;

    /* read data */
    idx = fread(frame_buffer, 1, amount, fp);

    VopStart = 1;
    StartCode = ((frame_buffer[idx - 1] << 24) |
                 (frame_buffer[idx - 2] << 16) |
                 (frame_buffer[idx - 3] << 8) |
                 frame_buffer[idx - 4]);

    /* end of stream */
    if(next_index == stream_size) {
      strm_end = 1;
      idx += 4;
    }

    cur_index = next_index;
  } else {
    while(!VopStart) {

      ret = fread(&temp, sizeof(u8), 1, fp);

      if(feof(fp)) {

        fprintf(stdout, "TB: End of stream noticed in readDecodeUnit\n");
        strm_end = 1;
        idx += 4;
        break;
      }
      /* Reading the whole stream at once must be limited to buffer size */
      if((idx > (length - MPEG2_WHOLE_STREAM_SAFETY_LIMIT)) &&
          whole_stream_mode) {

        whole_stream_mode = 0;

      }

      frame_buffer[idx] = temp;

      if(idx >= (frame_buffer == byte_strm_start ? 4 : 3)) {
        if(!whole_stream_mode) {
          if(disable_resync) {
            /*-----------------------------------
                Slice by slice
            -----------------------------------*/
            if((frame_buffer[idx - 3] == 0x00) &&
                (frame_buffer[idx - 2] == 0x00) &&
                (frame_buffer[idx - 1] == 0x01)) {
              if(frame_buffer[idx] > 0x00) {
                if(frame_buffer[idx] <= 0xAF) {
                  VopStart = 1;
                  StartCode = ((frame_buffer[idx] << 24) |
                               (frame_buffer[idx - 1] << 16) |
                               (frame_buffer[idx - 2] << 8) |
                               frame_buffer[idx - 3]);
                  /*printf("SLICE FOUND\n");*/
                }
              } else if(frame_buffer[idx] == 0x00) {
                VopStart = 1;
                StartCode = ((frame_buffer[idx] << 24) |
                             (frame_buffer[idx - 1] << 16) |
                             (frame_buffer[idx - 2] << 8) |
                             frame_buffer[idx - 3]);
                /* MPEG2 start code found */
              }
            }
          } else {
            /*-----------------------------------
                MPEG2 Start code
            -----------------------------------*/
            if(((frame_buffer[idx - 3] == 0x00) &&
                (frame_buffer[idx - 2] == 0x00) &&
                (((frame_buffer[idx - 1] == 0x01) &&
                  (frame_buffer[idx] == 0x00))))) {
              VopStart = 1;
              StartCode = ((frame_buffer[idx] << 24) |
                           (frame_buffer[idx - 1] << 16) |
                           (frame_buffer[idx - 2] << 8) |
                           frame_buffer[idx - 3]);
              /* MPEG2 start code found */
            }
          }
        }
      }
      if(idx >= length) {
        fprintf(stdout, "idx = %d,lenght = %d \n", idx, length);
        fprintf(stdout, "TB: Out Of Stream Buffer\n");
        break;
      }
      if(idx > strm_rew + 128) {
        idx -= strm_rew;
      }
      idx++;
      /* stop reading if truncated stream size is reached */
      if(stream_truncate && !disable_resync) {
        if(previous_used + idx >= stream_size) {
          printf("Stream truncated at %d bytes\n", previous_used + idx);
          stop_decoding = 1;   /* next call return 0 size -> exit decoding main loop */
          break;
        }
      }
    }
  }

  if(save_index && !use_index) {
#ifdef USE_64BIT_ENV
    fprintf(findex, "%lu\n", ftello64(fp));
#else
    fprintf(findex, "%llu\n", ftello64(fp));
#endif
  }

  trace_used_stream = previous_used;
  previous_used += idx;

  /* If we skip this packet */
  if(pic_rdy && next_packet && ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt)) {
    /* Get the next packet */
    printf("Packet Loss\n");
    return readDecodeUnit(fp, frame_buffer);
  } else {
    /*printf("READ DECODE UNIT %d\n", idx); */
    printf("No Packet Loss\n");
    if (disable_resync && pic_rdy && stream_truncate
        && ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt)) {
      i32 ret;
      printf("Original packet size %d\n", idx);
      ret = TBRandomizeU32(&idx);
      if(ret != 0) {
        printf("RANDOM STREAM ERROR FAILED\n");
        stop_decoding = 1;   /* next call return 0 size -> exit decoding main loop */
        return 0;
      }
      printf("Randomized packet size %d\n", idx);
    }
    return (idx);
  }
}

/*------------------------------------------------------------------------------
        printTimeCode
        Description : Print out time code
------------------------------------------------------------------------------*/

void printTimeCode(Mpeg2DecTime * timecode) {

  fprintf(stdout, "hours %u, "
          "minutes %u, "
          "seconds %u, "
          "time_pictures %u \n",
          timecode->hours,
          timecode->minutes, timecode->seconds, timecode->pictures);
}

/*------------------------------------------------------------------------------
        decRet
        Description : Print out Decoder return values
------------------------------------------------------------------------------*/

void decRet(Mpeg2DecRet ret) {

  printf("Decode result: ");

  switch (ret) {
  case MPEG2DEC_OK:
    printf("MPEG2DEC_OK\n");
    break;
  case MPEG2DEC_STRM_PROCESSED:
    printf("MPEG2DEC_STRM_PROCESSED\n");
    break;
  case MPEG2DEC_BUF_EMPTY:
    printf("MPEG2DEC_BUF_EMPTY\n");
    break;
  case MPEG2DEC_NO_DECODING_BUFFER:
    printf("MPEG2DEC_NO_DECODING_BUFFER\n");
    break;
  case MPEG2DEC_NONREF_PIC_SKIPPED:
    printf("MPEG2DEC_NONREF_PIC_SKIPPED\n");
    break;
  case MPEG2DEC_PIC_RDY:
    printf("MPEG2DEC_PIC_RDY\n");
    break;
  case MPEG2DEC_HDRS_RDY:
    printf("MPEG2DEC_HDRS_RDY\n");
    break;
  case MPEG2DEC_PIC_DECODED:
    printf("MPEG2DEC_PIC_DECODED\n");
    break;
  case MPEG2DEC_PARAM_ERROR:
    printf("MPEG2DEC_PARAM_ERROR\n");
    break;
  case MPEG2DEC_STRM_ERROR:
    printf("MPEG2DEC_STRM_ERROR\n");
    break;
  case MPEG2DEC_NOT_INITIALIZED:
    printf("MPEG2DEC_NOT_INITIALIZED\n");
    break;
  case MPEG2DEC_MEMFAIL:
    printf("MPEG2DEC_MEMFAIL\n");
    break;
  case MPEG2DEC_DWL_ERROR:
    printf("MPEG2DEC_DWL_ERROR\n");
    break;
  case MPEG2DEC_HW_BUS_ERROR:
    printf("MPEG2DEC_HW_BUS_ERROR\n");
    break;
  case MPEG2DEC_SYSTEM_ERROR:
    printf("MPEG2DEC_SYSTEM_ERROR\n");
    break;
  case MPEG2DEC_HW_TIMEOUT:
    printf("MPEG2DEC_HW_TIMEOUT\n");
    break;
  case MPEG2DEC_HDRS_NOT_RDY:
    printf("MPEG2DEC_HDRS_NOT_RDY\n");
    break;
  case MPEG2DEC_STREAM_NOT_SUPPORTED:
    printf("MPEG2DEC_STREAM_NOT_SUPPORTED\n");
    break;
  default:
    printf("Other %d\n", ret);
    break;
  }
}

/*------------------------------------------------------------------------------
        decNextPictureRet
        Description : Print out NextPicture return values
------------------------------------------------------------------------------*/
void decNextPictureRet(Mpeg2DecRet ret) {
  printf("next picture returns: ");

  decRet(ret);
}

/*------------------------------------------------------------------------------

    Function name:            printMpeg2PicCodingType

    Functional description:   Print out picture coding type value

------------------------------------------------------------------------------*/
void printMpeg2PicCodingType(u32 pic_type) {
  switch (pic_type) {
  case DEC_PIC_TYPE_I:
    printf(" DEC_PIC_TYPE_I,");
    break;
  case DEC_PIC_TYPE_P:
    printf(" DEC_PIC_TYPE_P,");
    break;
  case DEC_PIC_TYPE_B:
    printf(" DEC_PIC_TYPE_B,");
    break;
  case DEC_PIC_TYPE_D:
    printf(" DEC_PIC_TYPE_D,");
    break;
  default:
    printf("Other %d\n", pic_type);
    break;
  }
}


/*------------------------------------------------------------------------------
        printMpeg2Version
        Description : Print out decoder version info
------------------------------------------------------------------------------*/

void printMpeg2Version(void) {

  Mpeg2DecApiVersion dec_version;
  Mpeg2DecBuild dec_build;

  /*
   * Get decoder version info
   */

  dec_version = Mpeg2DecGetAPIVersion();
  printf("\nApi version:  %d.%d, ", dec_version.major, dec_version.minor);

  dec_build = Mpeg2DecGetBuild();
  printf("sw build nbr: %d, hw build nbr: %x\n\n",
         dec_build.sw_build, dec_build.hw_build);

}

/*------------------------------------------------------------------------------

    Function name: allocatePicBuffers

    Functional description: Allocates frame buffers

    Inputs:     Mpeg2DecLinearMem * buffer       pointers stored here

    Outputs:    NONE

    Returns:    nonzero if err

------------------------------------------------------------------------------*/

i32 AllocatePicBuffers(Mpeg2DecLinearMem * buffer, DecContainer * container) {

  u32 offset = (MPEG2_FRAME_BUFFER_SIZE + 0xFFF) & ~(0xFFF);
  u32 i = 0;

#ifndef MPEG2DEC_EXTERNAL_ALLOC_DISABLE

  if(DWLMallocRefFrm(((DecContainer *) container)->dwl,
                     offset * MPEG2_NUM_BUFFERS,
                     (struct DWLLinearMem *) buffer) != DWL_OK) {
    printf(("UNABLE TO ALLOCATE OUTPUT BUFFER MEMORY\n"));
    return 1;
  }

  buffer[1].virtual_address = buffer[0].virtual_address + offset / 4;
  buffer[1].bus_address = buffer[0].bus_address + offset;

  buffer[2].virtual_address = buffer[1].virtual_address + offset / 4;
  buffer[2].bus_address = buffer[1].bus_address + offset;

  for(i = 0; i < MPEG2_NUM_BUFFERS; i++) {
    printf("buff %d vir %p bus %lx\n", i,
           buffer[i].virtual_address, buffer[i].bus_address);
  }

#endif
  return 0;
}

/*------------------------------------------------------------------------------

 Function name:  WriteOutput

  Purpose:
  Write picture pointed by data to file. Size of the
  picture in pixels is indicated by pic_size.

------------------------------------------------------------------------------*/
void WriteOutput(char *filename, char *filename_tiled, u8 * data,
                 u32 frame_number, u32 width, u32 height, u32 interlaced,
                 u32 top, u32 first_field, u32 output_picture_endian,
                 Mpeg2DecPicture DecPicture, u32 coded_height,
                 u32 tiled_mode ) {
  u32 tmp, i, j;
  u32 pic_size;
  u8 *p, *ptmp;
  u32 skip_last_row = 0;
  static u32 frame_id = 0;
  u8 *raster_scan = NULL;
  enum DecDpbMode dpb_mode = DecPicture.dpb_mode;

  if(!write_output) {
    return;
  }

  pic_size = width * height * 3 / 2;

  /* Convert back to raster scan format if decoder outputs
   * tiled format */
  if(tiled_mode && convert_tiled_output) {
    raster_scan = (u8*)malloc(pic_size);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                     data, raster_scan, width, height );
    TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                     data+width*height, raster_scan+width*height, width, height/2 );
    data = raster_scan;
  } else if (convert_to_frame_dpb && (dpb_mode != DEC_DPB_FRAME) ) {

    raster_scan = (u8*)malloc(pic_size);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBFieldDpbToFrameDpb( 1, data, raster_scan, 0, width, height );

    data = raster_scan;
  }

  if(interlaced && coded_height <= (height - 16)) {
    height -= 16;
    skip_last_row = 1;
  }

  /* fout is global file pointer */
  if(fout == NULL) {
    /* open output file for writing, can be disabled with define.
     * If file open fails -> exit */
    if(strcmp(filename, "none") != 0) {
      fout = fopen(filename, "wb");
      if(fout == NULL) {
        printf("UNABLE TO OPEN OUTPUT FILE\n");
        if(raster_scan)
          free(raster_scan);
        return;
      }
    }
  }

#if 0
  if(f_tiled_output == NULL && tiled_output) {
    /* open output file for writing, can be disabled with define.
     * If file open fails -> exit */
    if(strcmp(filename_tiled, "none") != 0) {
      f_tiled_output = fopen(filename_tiled, "wb");
      if(f_tiled_output == NULL) {
        printf("UNABLE TO OPEN TILED OUTPUT FILE\n");
        if(raster_scan)
          free(raster_scan);
        return;
      }
    }
  }
#endif

  if(fout && data) {
    if(interlaced && interlaced_field) {
      /* start of top field */
      p = data;
      /* start of bottom field */
      if(!top)
        p += width;
      else {
        if(raster_scan)
          free(raster_scan);
        return; /* TODO! use "return" ==> match to reference model */
      }

      if(planar_output) {
        /* luma */
        for(i = 0; i < height / 2; i++, p += (width * 2))
          fwrite(p, 1, width, fout);
        if(skip_last_row)
          p += 16 * width;
        ptmp = p;
        /* cb */
        for(i = 0; i < height / 4; i++, p += (width * 2))
          for(j = 0; j < width >> 1; j++)
            fwrite(p + j * 2, 1, 1, fout);
        /* cr */
        for(i = 0; i < height / 4; i++, ptmp += (width * 2))
          for(j = 0; j < width >> 1; j++)
            fwrite(ptmp + 1 + j * 2, 1, 1, fout);
      } else {
        /* luma */
        for(i = 0; i < height / 2; i++, p += (width * 2))
          fwrite(p, 1, width, fout);
        if(skip_last_row)
          p += 16 * width;
        /* chroma */
        for(i = 0; i < height / 4; i++, p += (width * 2))
          fwrite(p, 1, width, fout);
      }
    } else { /* progressive */
#ifndef ASIC_TRACE_SUPPORT
      u8 *pic_copy = NULL;
#endif
      /*printf("first_field %d\n");*/

      if(interlaced && !interlaced_field && first_field)
        return;

      /*
       * printf("PIC %d, %s", DecPicture.pic_id,
       * DecPicture.
       * key_picture ? "key picture" : "non key picture");
       * if(DecPicture.field_picture)
       * {
       * printf(", %s Field",
       * DecPicture.top_field ? "top" : "bottom");
       * }
       * printf(" bPicture %d\n", DecPicture.bPicture);
       * printf("number_of_err_mbs %d\n", DecPicture.number_of_err_mbs); */

      if((DecPicture.field_picture && !first_field) ||
          !DecPicture.field_picture) {
        printf("Output picture %d\n", frame_id);
        /* Decoder without pp does not write out fields but a
         * frame containing both fields */
        /* PP output is written field by field */

#ifndef ASIC_TRACE_SUPPORT
        if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
          pic_copy = (u8 *) malloc(pic_size);
          if(NULL == pic_copy) {
            printf("MALLOC FAILED @ %s %d", __FILE__, __LINE__);
            if(raster_scan)
              free(raster_scan);
            return;
          }
          memcpy(pic_copy, data, pic_size);
          TbChangeEndianess(pic_copy, pic_size);
          data = pic_copy;
        }
#endif

#ifdef MD5SUM
        TBWriteFrameMD5Sum(fout, data, pic_size, frame_id);

        /* tiled */
#if 0
        if(tiled_output) {
          /* round up to next multiple of 16 */
          u32 tmp_width = (width + 15) & ~15;
          u32 tmp_height = (height + 15) & ~15;

          TbWriteTiledOutput(f_tiled_output, data, tmp_width >> 4,
                             tmp_height >> 4, frame_id,
                             1 /* write md5sum */ ,
                             1 /* semi-planar data */ );
        }
#endif
#else

        /* this presumes output has system endianess */
        if(planar_output) {
          if(!crop_output) {
            tmp = width * height;
            fwrite(data, 1, tmp, fout);
            p = skip_last_row ? data + tmp + 16 * width : data + tmp;
            for(i = 0; i < tmp / 4; i++)
              fwrite(p + i * 2, 1, 1, fout);
            for(i = 0; i < tmp / 4; i++)
              fwrite(p + 1 + i * 2, 1, 1, fout);
          } else { /* cropped output */
            tmp = width * height;
            p = data;
            for(i = 0; i < DecPicture.coded_height; i++) {
              fwrite(p, 1, DecPicture.coded_width, fout);
              p += DecPicture.frame_width;
            }
            p = data + tmp;
            for(i = 0; i < DecPicture.coded_height / 2; i++) {
              for(j = 0; j < DecPicture.coded_width / 2; j++)
                fwrite(p + 2 * j, 1, 1, fout);
              p += DecPicture.frame_width;
            }
            p = data + tmp + 1;
            for(i = 0; i < DecPicture.coded_height / 2; i++) {
              for(j = 0; j < DecPicture.coded_width / 2; j++)
                fwrite(p + 2 * j, 1, 1, fout);
              p += DecPicture.frame_width;
            }
          }
        } else if(!skip_last_row) /* semi-planar */
          fwrite(data, 1, pic_size, fout);
        else {
          tmp = width * height;
          fwrite(data, 1, tmp, fout);
          fwrite(data + tmp + 16 * width, 1, tmp / 2, fout);
        }

        /* tiled */
#if 0
        if(tiled_output) {
          /* round up to next multiple of 16 */
          u32 tmp_width = (width + 15) & ~15;
          u32 tmp_height = (height + 15) & ~15;

          TbWriteTiledOutput(f_tiled_output, data, tmp_width >> 4,
                             tmp_height >> 4, frame_id,
                             0 /* write md5sum */ ,
                             1 /* semi-planar data */ );
        }
#endif
#endif
#ifndef ASIC_TRACE_SUPPORT
        if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
          free(pic_copy);
        }
#endif
        frame_id++;
      }
    }
  }

  if(raster_scan)
    free(raster_scan);

}

/*------------------------------------------------------------------------------

    Function name:  WriteOutputLittleEndian

     Purpose:
     Write picture pointed by data to file. Size of the
     picture in pixels is indicated by pic_size. The data
     is converted to little endian.

------------------------------------------------------------------------------*/
void WriteOutputLittleEndian(u8 * data, u32 pic_size) {
  u32 chunks = 0;
  u32 i = 0;
  u32 word = 0;

  chunks = pic_size / 4;
  for(i = 0; i < chunks; ++i) {
    word = data[0];
    word <<= 8;
    word |= data[1];
    word <<= 8;
    word |= data[2];
    word <<= 8;
    word |= data[3];
    fwrite(&word, 4, 1, fout);
    data += 4;
  }

  if(pic_size % 4 == 0) {
    return;
  } else if(pic_size % 4 == 1) {
    word = data[0];
    word <<= 24;
    fwrite(&word, 1, 1, fout);
  } else if(pic_size % 4 == 2) {
    word = data[0];
    word <<= 8;
    word |= data[1];
    word <<= 16;
    fwrite(&word, 2, 1, fout);
  } else if(pic_size % 4 == 3) {
    word = data[0];
    word <<= 8;
    word |= data[1];
    word <<= 8;
    word |= data[2];
    word <<= 8;
    fwrite(&word, 3, 1, fout);
  }
}

/*------------------------------------------------------------------------------

    Function name:  Mpeg2DecTrace

    Purpose:
        Example implementation of Mpeg2DecTrace function. Prototype of this
        function is given in mpeg2decapi.h. This implementation appends
        trace messages to file named 'dec_api.trc'.

------------------------------------------------------------------------------*/
void Mpeg2DecTrace(const char *string) {
  FILE *fp;

  fp = fopen("dec_api.trc", "at");

  if(!fp)
    return;

  fwrite(string, 1, strlen(string), fp);
  fwrite("\n", 1, 1, fp);

  fclose(fp);
}

/*------------------------------------------------------------------------------

    Function name: writeOutputPicture

    Functional description: write output picture

    Inputs: vop number, picture description struct, instance

    Outputs:

    Returns:    MP4DecRet

------------------------------------------------------------------------------*/

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 vop_number, Mpeg2DecPicture DecPicture, Mpeg2DecInst decoder) {
  PPResult res;

  res = pp_check_combined_status();

  if(res == PP_OK) {
    pp_write_output(vop_number, DecPicture.field_picture,
                    DecPicture.top_field);
    pp_read_blend_components(((DecContainer *) decoder)->pp_instance);
  }
  if(pp_update_config
      (decoder, PP_PIPELINED_DEC_TYPE_MPEG2, &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
  }

}
#endif
