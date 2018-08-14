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
#ifndef MP4DEC_EXTERNAL_ALLOC_DISABLE
#include <fcntl.h>
#include <sys/mman.h>
#endif

#include "dwl.h"
#include "mp4decapi.h"
#ifdef USE_EFENCE
#include "efence.h"
#endif

#include "mp4dechwd_container.h"
#include "regdrv_g1.h"

#ifdef PP_PIPELINE_ENABLED
#include "ppapi.h"
#include "pptestbench.h"
#endif

#include "tb_cfg.h"
#include "tb_tiled.h"

#ifdef MD5SUM
#include "tb_md5.h"
#endif

#include "tb_sw_performance.h"
#include "tb_stream_corrupt.h"

#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

#define DEFAULT -1
#define VOP_START_CODE 0xb6010000
#define MAX_BUFFERS 32

/* Size of stream buffer */
#define STREAMBUFFER_BLOCKSIZE 2*2097151
#define MP4_WHOLE_STREAM_SAFETY_LIMIT (10*10*1024)

/* local function prototypes */

void printTimeCode(MP4DecTime * timecode);
static u32 readDecodeUnit(FILE * fp, u8 * frame_buffer, void *dec_inst);
static void Error(char *format, ...);
void decRet(MP4DecRet ret);
void printMP4Version(void);
void GetUserData(MP4DecInst dec_inst,
                 MP4DecInput DecIn, MP4DecUserDataType type);

static MP4DecRet writeOutputPicture(char *filename, char *filename_tiled,
                                    MP4DecInst decoder, u32 outp_byte_size,
                                    u32 vop_number, u32 output_picture_endian,
                                    MP4DecRet ret, u32 end,
                                    u32 frame_width, u32 frame_height);
void printMpeg4PicCodingType(u32 pic_type);

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 vop_number, MP4DecPicture DecPicture,
                    MP4DecInst decoder);
#endif

void decsw_performance(void);
u32 MP4GetResyncLength(MP4DecInst dec_inst, u8 * p_strm);

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
u8 disable_h263 = 0;
u32 crop_output = 0;
u8 disable_resync = 1;
u8 strm_end = 0;
u8 *stream_stop = NULL;
u32 strm_offset = 4;

u32 stream_size = 0;
u32 stop_decoding = 0;
u32 stream_truncate = 0;
u32 stream_packet_loss = 0;
u32 pic_rdy = 0;
u32 hdrs_rdy = 0;
u32 stream_header_corrupt = 0;
struct TBCfg tb_cfg;

/* Give stream to decode as one chunk */
u32 whole_stream_mode = 0;
u32 cumulative_error_mbs = 0;

u32 use_peek_output = 0;
u32 skip_non_reference = 0;

u32 planar_output = 0;
u32 second_field = 1;         /* for field pictures, flag that
                              * whole frame ready after this field */
u32 custom_width = 0;
u32 custom_height = 0;
u32 custom_dimensions = 0; /* Implicates that raw bitstream does not carry
                           * frame dimensions */
u32 no_start_codes = 0;     /* If raw bitstream doesn't carry startcodes */

MP4DecStrmFmt strm_fmt = MP4DEC_MPEG4;
u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 convert_tiled_output = 0;
u32 dpb_mode = DEC_DPB_FRAME;
u32 convert_to_frame_dpb = 0;

FILE *findex = NULL;
u32 save_index = 0;
u32 use_index = 0;
off64_t cur_index = 0;
off64_t next_index = 0;
u32 ds_ratio_x, ds_ratio_y;

#if defined(ASIC_TRACE_SUPPORT) || defined(SYSTEM_VERIFICATION)
extern u32 use_reference_idct;
#endif

#ifdef ASIC_TRACE_SUPPORT
extern u32 hw_dec_pic_count;
extern u32 g_hw_ver;
#endif

#ifdef MPEG4_EVALUATION
extern u32 g_hw_ver;
#endif

MP4DecInst decoder;
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
u32 res_changed = 0;
u32 pp_enabled = 0;

static void *AddBufferThread(void *arg) {
  usleep(100000);
  while(add_buffer_thread_run) {
    pthread_mutex_lock(&ext_buffer_contro);
    if(add_extra_flag && num_buffers < MAX_BUFFERS) {
      struct DWLLinearMem mem;
      i32 dwl_ret;
      if (pp_enabled)
        dwl_ret = DWLMallocLinear(dwl_inst, buffer_size, &mem);
      else
        dwl_ret = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
      if(dwl_ret == DWL_OK) {
        MP4DecRet rv = MP4DecAddBuffer(decoder, &mem);
        if(rv != MP4DEC_OK && rv != MP4DEC_WAITING_FOR_BUFFER) {
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
  u32 i;
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
MP4DecInfo Decinfo;
u32 end_of_stream = 0;
u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
pthread_t output_thread;
pthread_t release_thread;
int output_thread_run = 0;

sem_t buf_release_sem;
MP4DecPicture buf_list[100];
u32 buf_status[100] = {0};
u32 list_pop_index = 0;
u32 list_push_index = 0;
u32 last_pic_flag = 0;


void WriteOutput(char *filename, char *filename_tiled, u8 *data, u32 pic_size,
                 u32 vop_number, u32 output_picture_endian,
                 u32 frame_width, u32 frame_height,
                 MP4DecPicture DecPicture, u32 tiled_mode);

/* buf release thread entry point. */
static void* buf_release_thread(void* arg) {
  while(1) {
    /* Pop output buffer from buf_list and consume it */
    if(buf_status[list_pop_index]) {
      sem_wait(&buf_release_sem);
      MP4DecPictureConsumed(decoder, &buf_list[list_pop_index]);
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
            MP4DecRet rv = MP4DecAddBuffer(decoder, &mem);
            if(rv != MP4DEC_OK && rv != MP4DEC_WAITING_FOR_BUFFER) {
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
    usleep(10000);
  }
  return NULL;
}


/* Output thread entry point. */
static void* mpeg4_output_thread(void* arg) {
  MP4DecPicture DecPicture;
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
    MP4DecRet ret;
    ret = MP4DecNextPicture(decoder, &DecPicture, 0);
    if(ret == MP4DEC_PIC_RDY) {
      if(!use_peek_output) {
        printf("next picture returns:");
        decRet(ret);

        printf("PIC %d, %s", DecPicture.pic_id,
               DecPicture.
               key_picture ? "key picture,    " : "non key picture,");

        /* pic coding type */
        printMpeg4PicCodingType(DecPicture.pic_coding_type);

        if(DecPicture.field_picture)
          printf(" %s ",
                 DecPicture.top_field ? "top field.   " : "bottom field.");
        else
          printf(" frame picture. ");

        printTimeCode(&(DecPicture.time_code));
        if(DecPicture.nbr_of_err_mbs) {
          printf(", %d/%d error mbs\n",
                 DecPicture.nbr_of_err_mbs,
                 (DecPicture.frame_width >> 4) * (DecPicture.frame_height >> 4));
          cumulative_error_mbs += DecPicture.nbr_of_err_mbs;
        } else {
          printf("\n");

        }
        /* Write output picture to file */
        image_data = (u8 *) DecPicture.output_picture;

        pic_size = DecPicture.frame_width * DecPicture.frame_height * 3 / 2;

        WriteOutput(out_file_name, out_file_name_tiled, image_data,
                    pic_size, pic_display_number - 1,
                    output_picture_endian,
                    ((DecPicture.frame_width + 15) & ~15),
                    ((DecPicture.frame_height + 15) & ~15),
                    DecPicture, DecPicture.output_format);
      }

      if((tmp_id == DecPicture.pic_id && Decinfo.interlaced_sequence)
          || !Decinfo.interlaced_sequence) {
        /* Push output buffer into buf_list and wait to be consumed */
        buf_list[list_push_index] = DecPicture;
        buf_status[list_push_index] = 1;
        list_push_index++;
        if(list_push_index == 100)
          list_push_index = 0;

        sem_post(&buf_release_sem);

      }

      tmp_id = DecPicture.pic_id;

      pic_display_number++;
    }

    else if(ret == MP4DEC_END_OF_STREAM) {
      last_pic_flag = 1;
#ifdef USE_EXTERNAL_BUFFER
      add_buffer_thread_run = 0;
#endif
      break;
    }
  }
  return NULL;
}
#endif

int main(int argc, char **argv) {
  FILE *f_tbcfg;
  u8 *p_strm_data = 0;
  u32 max_num_vops;

  u32 i, tmp = 0, stream_len = 0;
  u32 outp_byte_size;
  u32 time_resolution;
  u32 time_hundreds;
  u32 vop_number = 0, vp_num = 0;
  u32 rlc_mode = 0;
  u32 num_frame_buffers = 0;
  struct DecDownscaleCfg dscale_cfg;

  /*
   * Decoder API structures
   */
#ifdef USE_EXTERNAL_BUFFER
  u32 prev_width = 0, prev_height = 0;
  u32 min_buffer_num = 0;
  MP4DecBufferInfo hbuf;
  MP4DecRet rv;
  memset(ext_buffers, 0, sizeof(ext_buffers));
  pthread_mutex_init(&ext_buffer_contro, NULL);
  struct DWLInitParam dwl_init;
#endif
  MP4DecRet ret;
  MP4DecRet info_ret;
  MP4DecRet tmp_ret;
  MP4DecInput DecIn;
  MP4DecOutput DecOut;
  MP4DecPicture DecPicture;
  struct DWLLinearMem stream_mem;
  u32 pic_id = 0;
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  char out_file_name[256] = "";
  char out_file_name_tiled[256] = "out_tiled.yuv";
  MP4DecInfo Decinfo;
  u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
#else
  u8 *image_data;
  u32 pic_size = 0;
#endif

  FILE *f_in = NULL;
  FILE *f_config = NULL;
  FILE *f_pip_mask = NULL;

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

#ifdef MPEG4_EVALUATION_8170
  g_hw_ver = 8170;
#elif MPEG4_EVALUATION_8190
  g_hw_ver = 8190;
#elif MPEG4_EVALUATION_9170
  g_hw_ver = 9170;
#elif MPEG4_EVALUATION_9190
  g_hw_ver = 9190;
#elif MPEG4_EVALUATION_G1
  g_hw_ver = 10000;
#endif

#ifndef PP_PIPELINE_ENABLED
  if(argc < 2) {

    printf("\nx170 MPEG-4 Decoder Testbench\n\n");
    printf("USAGE:\n%s [options] stream.mpeg4\n", argv[0]);
    printf("-Ooutfile write output to \"outfile\" (default out.yuv)\n");
    printf("-Nn to decode only first n vops of the stream\n");
    printf("-X to not to write output picture\n");
    printf("-Bn to use n frame buffers in decoder\n");
    printf("-Sfile.hex stream control trace file\n");
#if defined(ASIC_TRACE_SUPPORT) || defined(SYSTEM_VERIFICATION)
    printf("-R use reference decoder IDCT (sw/sw integration only)\n");
#endif
    printf("-W whole stream mode - give stream to decoder in one chunk\n");
    printf("-P write planar output\n");
    printf("-I save index file\n");
    printf("-E use tiled reference frame format.\n");
    printf("-G convert tiled output pictures to raster scan\n");
    printf("-F decode Sorenson Spark stream\n");
    printf
    ("-C crop output picture to real picture dimensions (only planar)\n");
    printf("-J decode DivX4 or DivX5 stream\n");
    printf("-D<width>x<height> decode DivX3 stream of resolution width x height\n");
    printf("-Q Skip decoding non-reference pictures.\n");
    printf("-Z output pictures using MP4DecPeek() function\n");
    printf("--separate-fields-in-dpb DPB stores interlaced content"\
           " as fields (default: frames)\n");
    printf("--output-frame-dpb Convert output to frame mode even if"\
           " field DPB mode used\n");
#ifdef USE_EXTERNAL_BUFFER
    printf("-A add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
    printf("-a allocate extra external buffer in output thread\n");
#endif
#endif
    printMP4Version();
    exit(100);
  }

  max_num_vops = 0;
  for(i = 1; i < (u32)argc - 1; i++) {
    if(strncmp(argv[i], "-O", 2) == 0) {
      strcpy(out_file_name, argv[i] + 2);
    } else if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_vops = atoi(argv[i] + 2);
    } else if(strncmp(argv[i], "-X", 2) == 0) {
      write_output = 0;
    } else if(strncmp(argv[i], "-S", 2) == 0) {
      f_stream_trace = fopen((argv[i] + 2), "r");
    } else if(strncmp(argv[i], "-P", 2) == 0) {
      planar_output = 1;
    } else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
      if(num_frame_buffers > MAX_BUFFERS)
        num_frame_buffers = MAX_BUFFERS;
    }
#if defined(ASIC_TRACE_SUPPORT) || defined(SYSTEM_VERIFICATION)
    else if(strncmp(argv[i], "-R", 2) == 0) {
      use_reference_idct = 1;
    }
#endif
    else if(strncmp(argv[i], "-W", 2) == 0) {
      whole_stream_mode = 1;
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if (strncmp(argv[i], "-G", 2) == 0)
      convert_tiled_output = 1;
    else if(strcmp(argv[i], "-F") == 0) {
      strm_fmt = MP4DEC_SORENSON;
    } else if(strcmp(argv[i], "-J") == 0) {
      strm_fmt = MP4DEC_CUSTOM_1;
    } else if(strcmp(argv[i], "-Z") == 0) {
      use_peek_output = 1;
    } else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    } else if(strncmp(argv[i], "-D", 2) == 0) {
      u32 frame_width, frame_height;
      custom_dimensions = 1;
      no_start_codes = 1;
      tmp = sscanf(argv[i]+2, "%dx%d", &custom_width, &custom_height );
      if( tmp != 2 ) {
        printf("MALFORMED WIDTHxHEIGHT: %s\n", argv[i]+2);
        return 1;
      }
      strm_offset = 0;
      frame_width = (custom_width+15)&~15;
      frame_height = (custom_height+15)&~15;
      outp_byte_size =
        (frame_width * frame_height * 3) >> 1;
      /* If -O option not used, generate default file name */
      if(out_file_name[0] == 0) {
        if(planar_output && crop_output)
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  custom_width, custom_height);
        else
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  frame_width, frame_height );
      }
    } else if(strcmp(argv[i], "-C") == 0) {
      crop_output = 1;
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

  printMP4Version();
  /* open data file */
  f_in = fopen(argv[argc - 1], "rb");
  if(f_in == NULL) {
    printf("Unable to open input file %s\n", argv[argc - 1]);
    exit(100);
  }
#else
  if(argc < 3) {
    printf("\nMpeg-4 Decoder PP Pipelined Testbench\n\n");
    printf("USAGE:\n%s [options] stream.mpeg4 pp.cfg\n", argv[0]);
    printf("-Nn to decode only first n vops of the stream\n");
    printf("-X to not to write output picture\n");
    printf("-W whole stream mode - give stream to decoder in one chunk\n");
    printf("-Bn to use n frame buffers in decoder\n");
    printf("-I save index file\n");
    printf("-E use tiled reference frame format.\n");
    printf("-F decode Sorenson Spark stream\n");
    printf("-J decode DivX4 or DivX5 stream\n");
    printf("-D<width>x<height> decode DivX3 stream of resolution width x height\n");
    printf("-Q Skip decoding non-reference pictures.\n");
    printf("--separate-fields-in-dpb DPB stores interlaced content"\
           " as fields (default: frames)\n");
#ifdef USE_EXTERNAL_BUFFER
    printf("-A add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
    printf("-a allocate extra external buffer in output thread\n");
#endif
#endif
    exit(100);
  }

  remove("pp_out.yuv");
  max_num_vops = 0;
  /* read cmdl parameters */
  for(i = 1; i < argc - 2; i++) {
    if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_vops = atoi(argv[i] + 2);
    } else if(strncmp(argv[i], "-X", 2) == 0) {
      write_output = 0;
    } else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
      if(num_frame_buffers > MAX_BUFFERS)
        num_frame_buffers = MAX_BUFFERS;
    } else if(strncmp(argv[i], "-W", 2) == 0) {
      whole_stream_mode = 1;
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if(strcmp(argv[i], "-F") == 0) {
      strm_fmt = MP4DEC_SORENSON;
    } else if(strcmp(argv[i], "-J") == 0) {
      strm_fmt = MP4DEC_CUSTOM_1;
    } else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    } else if(strncmp(argv[i], "-D", 2) == 0) {
      u32 frame_width, frame_height;
      custom_dimensions = 1;
      no_start_codes = 1;
      tmp = sscanf(argv[i]+2, "%dx%d", &custom_width, &custom_height );
      if( tmp != 2 ) {
        printf("MALFORMED WIDTHxHEIGHT: %s\n", argv[i]+2);
        return 1;
      }
      strm_offset = 0;
      frame_width = (custom_width+15)&~15;
      frame_height = (custom_height+15)&~15;
      outp_byte_size =
        (frame_width * frame_height * 3) >> 1;
      /* If -O option not used, generate default file name */
      if(out_file_name[0] == 0) {
        if(planar_output && crop_output)
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  custom_width, custom_height);
        else
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  frame_width, frame_height );
      }
    } else if(strcmp(argv[i], "-I") == 0) {
      save_index = 1;
    } else if(strcmp(argv[i], "--separate-fields-in-dpb") == 0) {
      dpb_mode = DEC_DPB_INTERLACED_FIELD;
    }
#ifdef USE_EXTERNAL_BUFFER
    else if(strcmp(argv[i], "-A") == 0) {
      use_extra_buffers = 1;
    } else if(strcmp(argv[i], "-a") == 0) {
      use_extra_buffers = 0;
      allocate_extra_buffers_in_output = 1;
    }
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

  printMP4Version();
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
          "\nX170 PP API v%d.%d - SW build: %d - HW build: %x\n",
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

  rlc_mode = TBGetDecRlcModeForced(&tb_cfg);
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
  printf("Decoder RLC %d\n", rlc_mode);
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
  disable_resync = !TBGetTBPacketByPacket(&tb_cfg);
  printf("TB Packet by Packet  %d\n", !disable_resync);
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
  if(stream_truncate && disable_resync) {
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
#endif
  if(!tmp) {
    printf("UNABLE TO OPEN TRACE FILES(S)\n");
  }
#ifdef USE_EXTERNAL_BUFFER
  dwl_init.client_type = DWL_CLIENT_TYPE_MPEG4_DEC;

  dwl_inst = DWLInit(&dwl_init);

  if(dwl_inst == NULL) {
    fprintf(stdout, ("ERROR: DWL Init failed"));
    goto end;
  }
#endif
  dscale_cfg.down_scale_x = ds_ratio_x;
  dscale_cfg.down_scale_y = ds_ratio_y;
  /*
   * Initialize the decoder
   */
  START_SW_PERFORMANCE;
  decsw_performance();
  ret = MP4DecInit(&decoder,
#ifdef USE_EXTERNAL_BUFFER
                   dwl_inst,
#endif
                   strm_fmt,
                   TBGetDecErrorConcealment( &tb_cfg ),
                   num_frame_buffers,
                   tiled_output |
                   (dpb_mode == DEC_DPB_INTERLACED_FIELD ? DEC_DPB_ALLOW_FIELD_ORDERING : 0), 0, 0, &dscale_cfg);
  END_SW_PERFORMANCE;
  decsw_performance();

  if(ret != MP4DEC_OK) {
    decoder = NULL;
    printf("Could not initialize decoder\n");
    goto end;
  }

  /* Set ref buffer test mode */
  ((DecContainer *) decoder)->ref_buffer_ctrl.test_function = TBRefbuTestMode;

  /* Check if we have to supply decoder with custom frame dimensions */
  if(custom_dimensions) {
    START_SW_PERFORMANCE;
    decsw_performance();
    MP4DecSetInfo( decoder, custom_width, custom_height );
    END_SW_PERFORMANCE;
    decsw_performance();
    TBSetRefbuMemModel( &tb_cfg,
                        ((DecContainer *) decoder)->mp4_regs,
                        &((DecContainer *) decoder)->ref_buffer_ctrl );
  }

  DecIn.enable_deblock = 0;
#ifdef PP_PIPELINE_ENABLED
  /* Initialize the post processer. If unsuccessful -> exit */
  if(pp_startup
      (argv[argc - 1], decoder, PP_PIPELINED_DEC_TYPE_MPEG4, &tb_cfg) != 0) {
    fprintf(stdout, "PP INITIALIZATION FAILED\n");
    goto end;
  }
#ifdef USE_EXTERNAL_BUFFER
  if(pp_update_config
      (decoder, PP_PIPELINED_DEC_TYPE_MPEG4, &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
    goto end;
  }
#endif
#endif

  if(DWLMallocLinear(((DecContainer *) decoder)->dwl,
                     STREAMBUFFER_BLOCKSIZE, &stream_mem) != DWL_OK) {
    printf(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
    goto end;
  }

  byte_strm_start = (u8 *) stream_mem.virtual_address;

  DecIn.stream = byte_strm_start;
  DecIn.stream_bus_address =  stream_mem.bus_address;


  stream_stop = byte_strm_start + length;
  /* NOTE: The registers should not be used outside decoder SW for other
   * than compile time setting test purposes */
  SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  /*
  SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_DEC_OUT_TILED_E,
                 output_format);
                 */
  SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_DEC_OUT_ENDIAN,
                 output_picture_endian);
  SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_DEC_MAX_BURST,
                 bus_burst_length);
  if ((DWLReadAsicID(DWL_CLIENT_TYPE_MPEG4_DEC) >> 16) == 0x8170U) {
    SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_PRIORITY_MODE,
                   asic_service_priority);
  }
  SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_DEC_DATA_DISC_E,
                 data_discard);
  SetDecRegister(((DecContainer *) decoder)->mp4_regs, HWIF_SERV_MERGE_DIS,
                 service_merge_disable);

  if(rlc_mode) {
    printf("RLC mode forced\n");
    /*Force the decoder into RLC mode */
    ((DecContainer *) decoder)->rlc_mode = 1;
  }

  /* Read what kind of stream is coming */
  START_SW_PERFORMANCE;
  decsw_performance();
  info_ret = MP4DecGetInfo(decoder, &Decinfo);
  END_SW_PERFORMANCE;
  decsw_performance();
  if(info_ret) {
    decRet(info_ret);
  }


#ifdef PP_PIPELINE_ENABLED
  if(custom_dimensions) {

    if(pp_update_config
        (decoder, PP_PIPELINED_DEC_TYPE_MPEG4, &tb_cfg) == CFG_UPDATE_FAIL)

    {
      fprintf(stdout, "PP CONFIG LOAD FAILED\n");
    }
    /* If unspecified at cmd line, use minimum # of buffers, otherwise
     * use specified amount. */
    if(num_frame_buffers == 0)
      pp_number_of_buffers(Decinfo.multi_buff_pp_size);
    else
      pp_number_of_buffers(num_frame_buffers);
  }
#endif
  /*
      fout = fopen(out_file_name, "wb");
      if(fout == NULL)
      {

          printf("Could not open output file\n");
          goto end2;
      }*/
#ifdef ASIC_TRACE_SUPPORT
  if(1 == info_ret)
    trace_mpeg4_dec_tools.short_video = 1;
#endif

  p_strm_data = (u8 *) DecIn.stream;
  DecIn.skip_non_reference = skip_non_reference;

  /* Read vop headers */

  do {
    stream_len = readDecodeUnit(f_in, p_strm_data, decoder);
  } while (!feof(f_in) && stream_len && stream_len <= 4);

  i = StartCode;
  /* decrease 4 because previous function call
   * read the first VOP start code */

  if( !no_start_codes )
    stream_len -= 4;
  DecIn.data_len = stream_len;
  DecOut.data_left = 0;
  printf("Start decoding\n");
  do {
    /*printf("DecIn.data_len %d\n", DecIn.data_len);*/
    DecIn.pic_id = pic_id;
    if(ret != MP4DEC_STRM_PROCESSED &&
        ret != MP4DEC_BUF_EMPTY &&
        ret != MP4DEC_NO_DECODING_BUFFER &&
        ret != MP4DEC_NONREF_PIC_SKIPPED)
      printf("Starting to decode picture ID %d\n", pic_id);

    if(rlc_mode) {
      printf("RLC mode forced \n");
      /*Force the decoder into RLC mode */
      ((DecContainer *) decoder)->rlc_mode = 1;
    }

    /* If enabled, break the stream */
    if(stream_bit_swap) {
      if((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt) {
        /* Picture needs to be ready before corrupting next picture */
        if(pic_rdy && corrupted_bytes <= 0) {
          ret = TBRandomizeBitSwapInStream((u8 *)DecIn.stream,
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
    ret = MP4DecDecode(decoder, &DecIn, &DecOut);
    END_SW_PERFORMANCE;
    decsw_performance();

    decRet(ret);

    /*
     * Choose what to do now based on the decoder return value
     */

    switch (ret) {
    case MP4DEC_HDRS_RDY:
    case MP4DEC_DP_HDRS_RDY:

      /* Set a flag to indicate that headers are ready */
#ifdef USE_EXTERNAL_BUFFER
      rv = MP4DecGetBufferInfo(decoder, &hbuf);
      printf("Mpeg4DecGetBufferInfo ret %d\n", rv);
      printf("buf_to_free %p, next_buf_size %d, buf_num %d\n",
             (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num);
#endif
      hdrs_rdy = 1;
      TBSetRefbuMemModel( &tb_cfg, ((DecContainer *) decoder)->mp4_regs,
                          &((DecContainer *) decoder)->ref_buffer_ctrl );

      /* Read what kind of stream is coming */
      START_SW_PERFORMANCE;
      decsw_performance();
      info_ret = MP4DecGetInfo(decoder, &Decinfo);
      END_SW_PERFORMANCE;
      decsw_performance();
      if(info_ret) {
        decRet(info_ret);
      }

      dpb_mode = Decinfo.dpb_mode;

      outp_byte_size =
        (Decinfo.frame_width * Decinfo.frame_height * 3) >> 1;

      if (Decinfo.interlaced_sequence)
        printf("INTERLACED SEQUENCE\n");

      /* If -O option not used, generate default file name */
      if(out_file_name[0] == 0) {
        if(planar_output && crop_output)
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  Decinfo.coded_width, Decinfo.coded_height);
        else
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  Decinfo.frame_width, Decinfo.frame_height);
      }

      if(!vop_number) {
        /*disable_resync = 1; */
        disable_h263 = !Decinfo.stream_format;
        if(Decinfo.stream_format)
          printf("%s stream\n",
                 Decinfo.stream_format ==
                 1 ? "MPEG-4 short video" : "h.263");
        else
          printf("MPEG-4 stream\n");

        printf("Profile and level %d\n",
               Decinfo.profile_and_level_indication);
        printf("Pixel Aspect ratio %d : %d\n",
               Decinfo.par_width, Decinfo.par_height);
        printf("Output format %s\n",
               Decinfo.output_format == MP4DEC_SEMIPLANAR_YUV420
               ? "MP4DEC_SEMIPLANAR_YUV420" : "MP4DEC_TILED_YUV420");
      } else {
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
        tmp_ret = writeOutputPicture(out_file_name, out_file_name_tiled, decoder,
                                     outp_byte_size,
                                     vop_number,
                                     output_picture_endian,
                                     ret, 0, Decinfo.frame_width,
                                     Decinfo.frame_height);
#else
        MP4DecEndOfStream(decoder, 0);
#endif
      }
      /* get user data if present.
       * Valid for only current stream buffer */
      if(Decinfo.user_data_voslen != 0)
        GetUserData(decoder, DecIn, MP4DEC_USER_DATA_VOS);
      if(Decinfo.user_data_visolen != 0)
        GetUserData(decoder, DecIn, MP4DEC_USER_DATA_VISO);
      if(Decinfo.user_data_vollen != 0)
        GetUserData(decoder, DecIn, MP4DEC_USER_DATA_VOL);
#ifdef USE_EXTERNAL_BUFFER
      if(Decinfo.pic_buff_size != min_buffer_num ||
          (Decinfo.frame_width * Decinfo.frame_height > prev_width * prev_height)) {
        /* Reset buffers added and stop adding extra buffers when a new header comes. */
        if (pp_enabled)
          res_changed = 1;
        else {
          add_extra_flag = 0;
          ReleaseExtBuffers();
          num_buffers = 0;
        }
      }
      prev_width = Decinfo.frame_width;
      prev_height = Decinfo.frame_height;
#endif
      /*printf("DecOut.data_left %d \n", DecOut.data_left);*/
      if(DecOut.data_left && !no_start_codes) {
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
          stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          if(!feof(f_in) && stream_len && stream_len <= 4 &&
              !no_start_codes ) {
            for ( i = 0 ; i < 4 ; ++i )
              p_strm_data[i] = p_strm_data[4+i];
            stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          }
          /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;
        DecOut.data_left = 0;

        corrupted_bytes = 0;

      }

#ifdef PP_PIPELINE_ENABLED
      pp_set_input_interlaced(Decinfo.interlaced_sequence);
#ifndef USE_EXTERNAL_BUFFER
      if(pp_update_config
          (decoder, PP_PIPELINED_DEC_TYPE_MPEG4, &tb_cfg) == CFG_UPDATE_FAIL)

      {
        fprintf(stdout, "PP CONFIG LOAD FAILED\n");
        goto end2;
      }
#endif
      DecIn.enable_deblock = pp_mpeg4_filter_used();
      if(DecIn.enable_deblock == 1)
        printf("Deblocking filter enabled\n");
      else if(DecIn.enable_deblock == 0)
        printf("Deblocking filter disabled\n");

      /* If unspecified at cmd line, use minimum # of buffers, otherwise
       * use specified amount. */
      if(num_frame_buffers == 0)
        pp_number_of_buffers(Decinfo.multi_buff_pp_size);
      else
        pp_number_of_buffers(num_frame_buffers);

#endif
      break;
#ifdef USE_EXTERNAL_BUFFER
    case MP4DEC_WAITING_FOR_BUFFER:
      rv = MP4DecGetBufferInfo(decoder, &hbuf);
      printf("MREG4DecGetBufferInfo ret %d\n", rv);
      printf("buf_to_free %p, next_buf_size %d, buf_num %d\n",
             (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num);
      if (hbuf.buf_to_free.virtual_address != NULL && res_changed) {
        add_extra_flag = 0;
        ReleaseExtBuffers();
        num_buffers = 0;
        res_changed = 0;
      }
      min_buffer_num = hbuf.buf_num;
      if(hbuf.next_buf_size) {
        /* Only add minimum required buffers at first. */
        //extra_buffer_num = hbuf.buf_num - min_buffer_num;
        buffer_size = hbuf.next_buf_size;
        struct DWLLinearMem mem;
        mem.mem_type = DWL_MEM_TYPE_CPU;
        for(i=0; i<min_buffer_num; i++) {
          if (pp_enabled)
            DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
          else
            DWLMallocRefFrm(dwl_inst, hbuf.next_buf_size, &mem);
          rv = MP4DecAddBuffer(decoder, &mem);
          printf("MP4DecAddBuffer ret %d\n", rv);
          if(rv != MP4DEC_OK && rv != MP4DEC_WAITING_FOR_BUFFER) {
            if (pp_enabled)
              DWLFreeLinear(dwl_inst, &mem);
            else
              DWLFreeRefFrm(dwl_inst, &mem);
          } else {
            ext_buffers[i] = mem;
          }
        }
        /* Extra buffers are allowed when minimum required buffers have been added.*/
        num_buffers = min_buffer_num;
        add_extra_flag = 1;
      }
      break;
#endif
    case MP4DEC_PIC_DECODED:
      /* Picture is now ready */
      pic_rdy = 1;
      /* Read what kind of stream is coming */
      pic_id++;
#if 0
      if (pic_id == 10) {
        info_ret = MP4DecAbort(decoder);
        info_ret = MP4DecAbortAfter(decoder);
        pic_id = 0;
        rewind(f_in);
        do {
          stream_len = readDecodeUnit(f_in, p_strm_data, decoder);
        } while (!feof(f_in) && stream_len && stream_len <= 4);

        i = StartCode;
        /* decrease 4 because previous function call
        * read the first VOP start code */

        if( !no_start_codes )
        stream_len -= 4;
        DecIn.data_len = stream_len;
        DecOut.data_left = 0;
        break;
      }
#endif

      START_SW_PERFORMANCE;
      decsw_performance();
      info_ret = MP4DecGetInfo(decoder, &Decinfo);
      END_SW_PERFORMANCE;
      decsw_performance();
      if(info_ret) {
        decRet(info_ret);
      }

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
      if (!output_thread_run) {
        output_thread_run = 1;
        sem_init(&buf_release_sem, 0, 0);
        pthread_create(&output_thread, NULL, mpeg4_output_thread, NULL);
        pthread_create(&release_thread, NULL, buf_release_thread, NULL);
      }

      if (use_peek_output &&
          MP4DecPeek(decoder, &DecPicture) == MP4DEC_PIC_RDY) {

        printf("next picture returns:");
        decRet(ret);

        printf("PIC %d, %s", DecPicture.pic_id,
               DecPicture.
               key_picture ? "key picture,    " : "non key picture,");

        /* pic coding type */
        printMpeg4PicCodingType(DecPicture.pic_coding_type);

        if(DecPicture.field_picture)
          printf(" %s ",
                 DecPicture.top_field ? "top field.   " : "bottom field.");
        else
          printf(" frame picture. ");

        printTimeCode(&(DecPicture.time_code));
        if(DecPicture.nbr_of_err_mbs) {
          printf(", %d/%d error mbs\n",
                 DecPicture.nbr_of_err_mbs,
                 (DecPicture.frame_width >> 4) * (DecPicture.frame_height >> 4));
          cumulative_error_mbs += DecPicture.nbr_of_err_mbs;
        } else {
          printf("\n");

        }

        /* Write output picture to file */
        image_data = (u8 *) DecPicture.output_picture;

        pic_size = DecPicture.frame_width * DecPicture.frame_height * 3 / 2;

        WriteOutput(out_file_name, out_file_name_tiled, image_data,
                    pic_size, vop_number,
                    output_picture_endian,
                    ((Decinfo.frame_width + 15) & ~15),
                    ((Decinfo.frame_height + 15) & ~15),
                    DecPicture, DecPicture.output_format);
      }

#else

      tmp_ret = writeOutputPicture(out_file_name, out_file_name_tiled, decoder,
                                   outp_byte_size,
                                   vop_number,
                                   output_picture_endian,
                                   ret, 0, Decinfo.frame_width,
                                   Decinfo.frame_height);
#endif
      vop_number++;
      vp_num = 0;
#ifdef USE_EXTERNAL_BUFFER
      if(use_extra_buffers && !add_buffer_thread_run) {
        add_buffer_thread_run = 1;
        pthread_create(&add_buffer_thread, NULL, AddBufferThread, NULL);
      }
#endif
      /*printf("DecOut.data_left %d \n", DecOut.data_left);*/
      if(DecOut.data_left && !no_start_codes) {
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

          printf("READ NEXT DECODE UNIT\n");

          if(!pic_rdy)
              stream_packet_loss = 0;*/
          stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          if(!feof(f_in) && stream_len && stream_len <= 4 &&
              !no_start_codes) {
            for ( i = 0 ; i < 4 ; ++i )
              p_strm_data[i] = p_strm_data[4+i];
            stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          }
          /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;
        DecOut.data_left = 0;

        corrupted_bytes = 0;

      }

      if(max_num_vops && (vop_number >= max_num_vops)) {
        printf("\n\nMax num of pictures reached\n\n");
        DecIn.data_len = 0;
        goto end2;
      }

      break;

    case MP4DEC_STRM_PROCESSED:
    case MP4DEC_BUF_EMPTY:
    case MP4DEC_NONREF_PIC_SKIPPED:
      fprintf(stdout,
              "TB: Video packet Number: %u, vop: %d\n", vp_num++,
              vop_number);
    /* Used to indicate that picture decoding needs to
     * finalized prior to corrupting next picture */

    /* fallthrough */

#ifdef GET_FREE_BUFFER_NON_BLOCK
    case MP4DEC_NO_DECODING_BUFFER:
    /* Just for simulation: if no buffer, sleep 0.5 second and try decoding again. */
#endif

    case MP4DEC_OK:

      if((ret == MP4DEC_OK) && (StartCode == VOP_START_CODE)) {
        fprintf(stdout, "\nTb: ...::::!! The decoder missed"
                "a VOP startcode !!::::...\n\n");
      }

      /* Read what kind of stream is coming */
      START_SW_PERFORMANCE;
      decsw_performance();
      info_ret = MP4DecGetInfo(decoder, &Decinfo);
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

      /*printf("DecOut.data_left %d \n", DecOut.data_left);*/
      if(DecOut.data_left && (!no_start_codes || ret == MP4DEC_NO_DECODING_BUFFER)) {
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
          stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          if(!feof(f_in) && stream_len && stream_len <= 4 &&
              !no_start_codes) {
            for ( i = 0 ; i < 4 ; ++i )
              p_strm_data[i] = p_strm_data[4+i];
            stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          }
          /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;
        DecOut.data_left = 0;

        corrupted_bytes = 0;

      }

      break;

    case MP4DEC_VOS_END:
      printf("Video object seq end\n");
      /*DecIn.data_len = 0;*/
      /*printf("DecOut.data_left %d \n", DecOut.data_left);*/
      {

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
          stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          if(!feof(f_in) && stream_len && stream_len <= 4 &&
              !no_start_codes) {
            for ( i = 0 ; i < 4 ; ++i )
              p_strm_data[i] = p_strm_data[4+i];
            stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
          }                    /*stream_packet_loss = streamPacketLossTmp;*/
        }
        DecIn.data_len = stream_len;
        DecOut.data_left = 0;

        corrupted_bytes = 0;
      }

      break;

    case MP4DEC_PARAM_ERROR:
      printf("INCORRECT STREAM PARAMS\n");
      goto end2;
      break;

    case MP4DEC_STRM_ERROR:
      /* Used to indicate that picture decoding needs to
       * finalized prior to corrupting next picture
      pic_rdy = 0;*/
      printf("STREAM ERROR\n");
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
        stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
        if(!feof(f_in) && stream_len && stream_len <= 4 &&
            !no_start_codes) {
          for ( i = 0 ; i < 4 ; ++i )
            p_strm_data[i] = p_strm_data[4+i];
          stream_len = readDecodeUnit(f_in, p_strm_data + strm_offset, decoder);
        }
        /*stream_packet_loss = streamPacketLossTmp;*/
      }
      DecIn.data_len = stream_len;
      DecOut.data_left = 0;
      /*goto end2; */
      break;

    default:
      goto end2;
    }
    /*
     * While there is stream
     */

  } while(DecIn.data_len > 0);
end2:

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  do {
    tmp_ret = writeOutputPicture(out_file_name, out_file_name_tiled,
                                 decoder, outp_byte_size,
                                 vop_number, output_picture_endian, ret, 1,
                                 Decinfo.frame_width, Decinfo.frame_height);
  } while(tmp_ret == MP4DEC_PIC_RDY);
#else
  if(output_thread_run)
    MP4DecEndOfStream(decoder, 1);
  if(output_thread)
    pthread_join(output_thread, NULL);
  if(release_thread)
    pthread_join(release_thread, NULL);
#endif

  START_SW_PERFORMANCE;
  decsw_performance();
  MP4DecGetInfo(decoder, &Decinfo);
  END_SW_PERFORMANCE;
  decsw_performance();

end:
  /*
   * Release the decoder
   */
#ifdef USE_EXTERNAL_BUFFER
  add_buffer_thread_run = 0;
#endif
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
  if (decoder != NULL) MP4DecRelease(decoder);
#ifdef USE_EXTERNAL_BUFFER
  DWLRelease(dwl_inst);
#endif
  END_SW_PERFORMANCE;
  decsw_performance();

  if(Decinfo.frame_width < 1921)
    printf("\nWidth %d Height %d (Cropped %dx%d)\n", Decinfo.frame_width,
           Decinfo.frame_height, Decinfo.coded_width, Decinfo.coded_height);

  if(cumulative_error_mbs) {
    printf("Cumulative errors: %d/%d macroblocks, ",
           cumulative_error_mbs,
           (Decinfo.frame_width >> 4) * (Decinfo.frame_height >> 4) *
           vop_number);
  }
  printf("decoded %d pictures\n", vop_number);

  printf( "Output file: %s\n", out_file_name);

  if(f_in)
    fclose(f_in);

  if(fout)
    fclose(fout);

  if(f_tiled_output)
    fclose(f_tiled_output);

#ifdef ASIC_TRACE_SUPPORT
  trace_SequenceCtrl(hw_dec_pic_count, b_frames);
  trace_RefbufferHitrate();
  trace_MPEG4DecodingTools();
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

  FINALIZE_SW_PERFORMANCE;

  if(cumulative_error_mbs || !vop_number) {
    printf("ERRORS FOUND\n");
    return (1);
  } else
    return (0);
}


/*------------------------------------------------------------------------------
        readDecodeUnitNoSc
        Description : search VOP start code  or video
        packet start code and read one decode unit at a time
------------------------------------------------------------------------------*/
static u32 readDecodeUnitNoSc(FILE * fp, u8 * frame_buffer, void *dec_inst) {
  u8  size_tmp[4];
  u32 size, tmp;
  static off64_t offset = 0;
  static u32 count = 0;
  int ret;

  if(use_index) {
#ifdef USE_64BIT_ENV
    ret = fscanf(findex, "%lu", &next_index);
#else
    ret = fscanf(findex, "%llu", &next_index);
#endif
    if(feof(findex)) {
      strm_end = 1;
      return 0;
    }

    if(ftello64(fp) != next_index) {
      fseek(fp, next_index, SEEK_SET);
    }

    ret = fread(&size_tmp, sizeof(u8), 4, fp);

  } else {
    /* skip "00dc" from frame beginning (may signal video chunk start code).
     * also skip "0000" in case stream contains zero-size packets */
    for(;;) {
      fseeko64( fp, offset, SEEK_SET );
      if (fread( &size_tmp, sizeof(u8), 4, fp ) != 4)
        break;
      if( ( size_tmp[0] == '0' &&
            size_tmp[1] == '0' &&
            size_tmp[2] == 'd' &&
            size_tmp[3] == 'c' ) ||
          ( size_tmp[0] == 0x0 &&
            size_tmp[1] == 0x0 &&
            size_tmp[2] == 0x0 &&
            size_tmp[3] == 0x0 ) ) {
        offset += 4;
        continue;
      }
      break;
    }
  }

  size = (size_tmp[0]) +
         (size_tmp[1] << 8) +
         (size_tmp[2] << 16) +
         (size_tmp[3] << 24);

  if( size == (u32)-1 ) {
    strm_end = 1;
    return 0;
  }

  if(save_index && !use_index) {
#ifdef USE_64BIT_ENV
    fprintf(findex, "%lu\n", offset);
#else
    fprintf(findex, "%llu\n", offset);
#endif
  }

  tmp = fread( frame_buffer, sizeof(u8), size, fp );
  if( size != tmp ) {
    strm_end = 1;
    return 0;
  }

  offset += size + 4;
  return size;

}

/*------------------------------------------------------------------------------
        readDecodeUnit
        Description : search VOP start code  or video
        packet start code and read one decode unit at a time
------------------------------------------------------------------------------*/
static u32 readDecodeUnit(FILE * fp, u8 * frame_buffer, void *dec_inst) {

  u32 idx = 0, VopStart = 0;
  u8 temp;
  u8 next_packet = 0;
  static u32 resync_marker_length = 0;
  int ret;

  if( no_start_codes ) {
    return readDecodeUnitNoSc(fp, frame_buffer, dec_inst);
  }

  StartCode = 0;

  if(stop_decoding) {
    printf("Truncated stream size reached -> stop decoding\n");
    return 0;
  }

  /* If enabled, lose the packets (skip this packet first though) */
  if(stream_packet_loss && !disable_resync) {
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

    /* read index */
#ifdef USE_64BIT_ENV
    ret = fscanf(findex, "%lu", &next_index);
#else
    ret = fscanf(findex, "%llu", &next_index);
#endif
    amount = next_index - cur_index;

    idx = fread(frame_buffer, 1, amount, fp);

    /* start code */
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
      if((idx > (length - MP4_WHOLE_STREAM_SAFETY_LIMIT)) && whole_stream_mode) {

        whole_stream_mode = 0;

      }

      frame_buffer[idx] = temp;

      if(idx >= 3) {

        if(!whole_stream_mode) {

          /*-----------------------------------
              H263 Start code

          -----------------------------------*/
          if((strm_fmt == MP4DEC_SORENSON) && (idx >= 7)) {
            if((frame_buffer[idx - 3] == 0x00) &&
                (frame_buffer[idx - 2] == 0x00) &&
                ((frame_buffer[idx - 1] & 0xF8) == 0x80)) {
              VopStart = 1;
              StartCode = ((frame_buffer[idx] << 24) |
                           (frame_buffer[idx - 1] << 16) |
                           (frame_buffer[idx - 2] << 8) |
                           frame_buffer[idx - 3]);
            }
          } else if(!disable_h263 && (idx >= 7)) {
            if((frame_buffer[idx - 3] == 0x00) &&
                (frame_buffer[idx - 2] == 0x00) &&
                ((frame_buffer[idx - 1] & 0xFC) == 0x80)) {
              VopStart = 1;
              StartCode = ((frame_buffer[idx] << 24) |
                           (frame_buffer[idx - 1] << 16) |
                           (frame_buffer[idx - 2] << 8) |
                           frame_buffer[idx - 3]);
            }
          }
          /*-----------------------------------
              MPEG4 Start code
          -----------------------------------*/
          if(((frame_buffer[idx - 3] == 0x00) &&
              (frame_buffer[idx - 2] == 0x00) &&
              (((frame_buffer[idx - 1] == 0x01) &&
                (frame_buffer[idx] == 0xB6))))) {
            VopStart = 1;
            StartCode = ((frame_buffer[idx] << 24) |
                         (frame_buffer[idx - 1] << 16) |
                         (frame_buffer[idx - 2] << 8) |
                         frame_buffer[idx - 3]);
            /* if MPEG4 start code found,
             * no need to look for H263 start code */
            disable_h263 = 1;
            resync_marker_length = 0;

          }

          /*-----------------------------------
              resync_marker
          -----------------------------------*/

          else if(disable_h263 && !disable_resync) {
            if((frame_buffer[idx - 3] == 0x00) &&
                (frame_buffer[idx - 2] == 0x00) &&
                (frame_buffer[idx - 1] > 0x01)) {

              if(resync_marker_length == 0) {
                resync_marker_length = MP4GetResyncLength(dec_inst,
                                       frame_buffer);
              }
              if((frame_buffer[idx - 1] >> (24 - resync_marker_length))
                  == 0x1) {
                VopStart = 1;
                StartCode = ((frame_buffer[idx] << 24) |
                             (frame_buffer[idx - 1] << 16) |
                             (frame_buffer[idx - 2] << 8) |
                             frame_buffer[idx - 3]);
              }
            }
          }
          /*-----------------------------------
              VOS end code
          -----------------------------------*/
          if(idx >= 7) {
            if(((frame_buffer[idx - 3] == 0x00) &&
                (frame_buffer[idx - 2] == 0x00) &&
                (((frame_buffer[idx - 1] == 0x01) &&
                  (frame_buffer[idx] == 0xB1))))) {

              /* stream end located */
              VopStart = 1;
              StartCode = ((frame_buffer[idx] << 24) |
                           (frame_buffer[idx - 1] << 16) |
                           (frame_buffer[idx - 2] << 8) |
                           frame_buffer[idx - 3]);
            }
          }

        }
      }
      if(idx >= length) {
        fprintf(stdout, "idx = %d,lenght = %d \n", idx, length);
        fprintf(stdout, "TB: Out Of Stream Buffer\n");
        break;
      }
      if(idx > (u32)strm_rew + 128) {
        idx -= strm_rew;
      }
      idx++;
      /* stop reading if truncated stream size is reached */
      if(stream_truncate && disable_resync) {
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
    return readDecodeUnit(fp, frame_buffer, dec_inst);
  } else {
    /*printf("No Packet Loss\n");*/
    if (!disable_resync && pic_rdy && stream_truncate && ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt)) {
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

void printTimeCode(MP4DecTime * timecode) {

  fprintf(stdout, "hours %u, "
          "minutes %u, "
          "seconds %u, "
          "time_incr %u, "
          "time_res %u",
          timecode->hours,
          timecode->minutes,
          timecode->seconds, timecode->time_incr, timecode->time_res);
}

/*------------------------------------------------------------------------------
        decRet
        Description : Print out Decoder return values
------------------------------------------------------------------------------*/

void decRet(MP4DecRet ret) {

  printf("Decode result: ");

  switch (ret) {
  case MP4DEC_OK:
    printf("MP4DEC_OK\n");
    break;
  case MP4DEC_STRM_PROCESSED:
    printf("MP4DEC_STRM_PROCESSED\n");
    break;
#ifdef GET_FREE_BUFFER_NON_BLOCK
  case MP4DEC_NO_DECODING_BUFFER:
    printf("MP4DEC_NO_DECODING_BUFFER\n");
    break;
#endif
  case MP4DEC_BUF_EMPTY:
    printf("MP4DEC_BUF_EMPTY\n");
    break;
  case MP4DEC_NONREF_PIC_SKIPPED:
    printf("MP4DEC_NONREF_PIC_SKIPPED\n");
    break;
  case MP4DEC_PIC_RDY:
    printf("MP4DEC_PIC_RDY\n");
    break;
  case MP4DEC_HDRS_RDY:
    printf("MP4DEC_HDRS_RDY\n");
    break;
  case MP4DEC_DP_HDRS_RDY:
    printf("MP4DEC_DP_HDRS_RDY\n");
    break;
  case MP4DEC_PARAM_ERROR:
    printf("MP4DEC_PARAM_ERROR\n");
    break;
  case MP4DEC_STRM_ERROR:
    printf("MP4DEC_STRM_ERROR\n");
    break;
  case MP4DEC_NOT_INITIALIZED:
    printf("MP4DEC_NOT_INITIALIZED\n");
    break;
  case MP4DEC_MEMFAIL:
    printf("MP4DEC_MEMFAIL\n");
    break;
  case MP4DEC_DWL_ERROR:
    printf("MP4DEC_DWL_ERROR\n");
    break;
  case MP4DEC_HW_BUS_ERROR:
    printf("MP4DEC_HW_BUS_ERROR\n");
    break;
  case MP4DEC_SYSTEM_ERROR:
    printf("MP4DEC_SYSTEM_ERROR\n");
    break;
  case MP4DEC_HW_TIMEOUT:
    printf("MP4DEC_HW_TIMEOUT\n");
    break;
  case MP4DEC_HDRS_NOT_RDY:
    printf("MP4DEC_HDRS_NOT_RDY\n");
    break;
  case MP4DEC_PIC_DECODED:
    printf("MP4DEC_PIC_DECODED\n");
    break;
  case MP4DEC_FORMAT_NOT_SUPPORTED:
    printf("MP4DEC_FORMAT_NOT_SUPPORTED\n");
    break;
  case MP4DEC_STRM_NOT_SUPPORTED:
    printf("MP4DEC_STRM_NOT_SUPPORTED\n");
    break;
  default:
    printf("Other %d\n", ret);
    break;
  }
}

/*------------------------------------------------------------------------------

    Function name:            printMpeg4PicCodingType

    Functional description:   Print out picture coding type value

------------------------------------------------------------------------------*/
void printMpeg4PicCodingType(u32 pic_type) {
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
  default:
    printf("Other %d\n", pic_type);
    break;
  }
}

/*------------------------------------------------------------------------------

    Function name: printMP4Version

    Functional description: Print version info

    Inputs:

    Outputs:    NONE

    Returns:    NONE

------------------------------------------------------------------------------*/
void printMP4Version(void) {

  MP4DecApiVersion dec_version;
  MP4DecBuild dec_build;

  /*
   * Get decoder version info
   */

  dec_version = MP4DecGetAPIVersion();
  printf("\nApi version:  %d.%d, ", dec_version.major, dec_version.minor);

  dec_build = MP4DecGetBuild();
  printf("sw build nbr: %d, hw build nbr: %x\n\n",
         dec_build.sw_build, dec_build.hw_build);

}

/*------------------------------------------------------------------------------

    Function name: GetUserData

    Functional description: This function is used to get user data from the
                            decoder.

    Inputs:     MP4DecInst dec_inst       decoder instance
                MP4DecUserDataType type   user data type to read

    Outputs:    NONE

    Returns:    NONE

------------------------------------------------------------------------------*/
void GetUserData(MP4DecInst dec_inst,
                 MP4DecInput DecIn, MP4DecUserDataType type) {
  u32 tmp;
  MP4DecUserConf user_data_config;
  MP4DecInfo dec_info;
  u8 *data = NULL;
  u32 size = 0;

  /* get info from the decoder */
  tmp = MP4DecGetInfo(dec_inst, &dec_info);
  if(tmp != 0) {
    printf(("ERROR, exiting...\n"));
  }
  switch (type) {
  case MP4DEC_USER_DATA_VOS:
    size = dec_info.user_data_voslen;
    data = (u8 *) calloc(size + 1, sizeof(u8));
    user_data_config.p_user_data_vos = data;
    user_data_config.user_data_vosmax_len = size;
    break;
  case MP4DEC_USER_DATA_VISO:
    size = dec_info.user_data_visolen;
    data = (u8 *) calloc(size + 1, sizeof(u8));
    user_data_config.p_user_data_viso = data;
    user_data_config.user_data_visomax_len = size;
    break;
  case MP4DEC_USER_DATA_VOL:
    size = dec_info.user_data_vollen;
    data = (u8 *) calloc(size + 1, sizeof(u8));
    user_data_config.p_user_data_vol = data;
    user_data_config.user_data_volmax_len = size;
    break;
  case MP4DEC_USER_DATA_GOV:
    size = dec_info.user_data_govlen;
    data = (u8 *) calloc(size + 1, sizeof(u8));
    user_data_config.p_user_data_gov = data;
    user_data_config.user_data_govmax_len = size;

    printf("VOS user data size: %d\n", size);
    break;
  default:
    break;
  }
  user_data_config.user_data_type = type;

  /* get user data */
  tmp = MP4DecGetUserData(dec_inst, &DecIn, &user_data_config);
  if(tmp != 0) {
    printf("ERROR, exiting...\n");
  }

  /* print user data */
  if(type == MP4DEC_USER_DATA_VOS)
    printf("VOS user data: %s\n", data);
  else if(type == MP4DEC_USER_DATA_VISO)
    printf("VISO user data: %s\n", data);
  else if(type == MP4DEC_USER_DATA_VOL)
    printf("VOL user data: %s\n", data);
  else if(type == MP4DEC_USER_DATA_GOV) {
    printf("\nGov user data: %s\n", data);
    fflush(stdout);
  }
  /* free allocated memory */
  if(data)
    free(data);
}

/*------------------------------------------------------------------------------

    Function name: writeOutputPicture

    Functional description: write output picture

    Inputs:

    Outputs:

    Returns:    MP4DecRet

------------------------------------------------------------------------------*/

MP4DecRet writeOutputPicture(char *filename, char *filename_tiled,
                             MP4DecInst decoder, u32 outp_byte_size,
                             u32 vop_number, u32 output_picture_endian,
                             MP4DecRet ret, u32 end,
                             u32 frame_width, u32 frame_height) {
  u8 *p_yuv_out = 0;
#ifdef USE_EXTERNAL_BUFFER
  const u32 *temp_addr = NULL;
#endif
  MP4DecRet tmp_ret;
  MP4DecPicture DecPicture, tmp_picture;
  u32 i, tmp;
  u8 *raster_scan = NULL;

  if (end && use_peek_output)
    return MP4DEC_OK;
  do {
    decsw_performance();
    if (use_peek_output) {
      tmp_ret = MP4DecPeek(decoder, &DecPicture);
      while (MP4DecNextPicture(decoder, &tmp_picture, 0) == MP4DEC_PIC_RDY);
    } else
      tmp_ret = MP4DecNextPicture(decoder, &DecPicture, end ? 1 : 0);
    decsw_performance();

    printf("next picture returns:");
    decRet(tmp_ret);
#ifdef USE_EXTERNAL_BUFFER
    temp_addr = (u32*)(DecPicture.output_picture);
#endif
    /* foutput is global file pointer */
    if(fout == NULL) {
      /* open output file for writing, can be disabled with define.
       * If file open fails -> exit */
      if(strcmp(filename, "none") != 0) {
        fout = fopen(filename, "wb");
        if(fout == NULL) {
          printf("UNABLE TO OPEN OUTPUT FILE\n");
          return MP4DEC_PARAM_ERROR;
        }
      }
    }

    /* Convert back to raster scan format if decoder outputs
     * tiled format */
    if(DecPicture.output_format != DEC_OUT_FRM_RASTER_SCAN && convert_tiled_output) {
      raster_scan = (u8*)malloc(outp_byte_size);
      if(!raster_scan) {
        fprintf(stderr, "error allocating memory for tiled"
                "-->raster conversion!\n");
        return MP4DEC_PARAM_ERROR;
      }

      TBTiledToRaster( DecPicture.output_format, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                       (u8*)DecPicture.output_picture, raster_scan, frame_width, frame_height );
      TBTiledToRaster( DecPicture.output_format, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                       (u8*)DecPicture.output_picture+frame_width*frame_height,
                       raster_scan+frame_width*frame_height, frame_width, frame_height/2 );
      DecPicture.output_picture = raster_scan;
    } else if (convert_to_frame_dpb && (dpb_mode != DEC_DPB_FRAME) ) {

      raster_scan = (u8*)malloc(outp_byte_size);
      if(!raster_scan) {
        fprintf(stderr, "error allocating memory for tiled"
                "-->raster conversion!\n");
        return MP4DEC_PARAM_ERROR;
      }

      TBFieldDpbToFrameDpb( 1, (u8*)DecPicture.output_picture,
                            raster_scan, 0, frame_width, frame_height );

      DecPicture.output_picture = raster_scan;
    }


#if 0
    if(f_tiled_output == NULL && tiled_output) {
      /* open output file for writing, can be disabled with define.
       * If file open fails -> exit */
      if(strcmp(filename_tiled, "none") != 0) {
        f_tiled_output = fopen(filename_tiled, "wb");
        if(f_tiled_output == NULL) {
          printf("UNABLE TO OPEN TILED OUTPUT FILE\n");
          return;
        }
      }
    }
#endif

    if( fout == NULL && f_tiled_output == NULL)
      continue;

    if(tmp_ret == MP4DEC_PIC_RDY) {
      printf("PIC %d, %s", DecPicture.pic_id,
             DecPicture.
             key_picture ? "key picture,    " : "non key picture,");
      /* pic coding type */
      printMpeg4PicCodingType(DecPicture.pic_coding_type);

      if(DecPicture.field_picture)
        printf(" %s ",
               DecPicture.top_field ? "top field.   " : "bottom field.");
      else
        printf(" frame picture. ");

      printTimeCode(&(DecPicture.time_code));
      if(DecPicture.nbr_of_err_mbs) {
        printf(", %d/%d error mbs\n",
               DecPicture.nbr_of_err_mbs,
               (frame_width >> 4) * (frame_height >> 4));
        cumulative_error_mbs += DecPicture.nbr_of_err_mbs;
      } else {
        printf("\n");

      }

      if((DecPicture.field_picture && second_field) ||
          !DecPicture.field_picture) {
        /* Decoder without pp does not write out fields but a
         * frame containing both fields */
        /* PP output is written field by field */
#ifndef PP_PIPELINE_ENABLED
        if(DecPicture.field_picture)
          second_field = 0;
#endif

        fflush(stdout);
        p_yuv_out = (u8 *) DecPicture.output_picture;
        if(write_output) {
#ifndef PP_PIPELINE_ENABLED
#ifndef ASIC_TRACE_SUPPORT
          u8 *pic_copy = NULL;
#endif
          assert(p_yuv_out != 0);
          assert(fout != 0);

#ifndef ASIC_TRACE_SUPPORT
          if(output_picture_endian ==
              DEC_X170_BIG_ENDIAN) {
            pic_copy = (u8 *) malloc(outp_byte_size);
            if(NULL == pic_copy) {
              printf("MALLOC FAILED @ %s %d", __FILE__, __LINE__);
              if(raster_scan)
                free(raster_scan);
              return MP4DEC_PARAM_ERROR;
            }
            memcpy(pic_copy, p_yuv_out, outp_byte_size);
            TbChangeEndianess(pic_copy, outp_byte_size);
            p_yuv_out = pic_copy;
          }
#endif
          /* Write  MD5 checksum instead of the frame */
#ifdef MD5SUM
          TBWriteFrameMD5Sum(fout, p_yuv_out,
                             outp_byte_size, vop_number);

          /* tiled */
#if 0
          if(tiled_output) {
            assert(frame_width % 16 == 0);
            assert(frame_height % 16 == 0);
            TbWriteTiledOutput(f_tiled_output, p_yuv_out,
                               frame_width >> 4, frame_height >> 4,
                               vop_number, 1 /* write md5sum */ ,
                               1 /* semi-planar data */ );
          }
#endif
#else
          if(!planar_output) {
            fwrite(p_yuv_out, 1, outp_byte_size, fout);
            /* tiled */
            if(tiled_output) {
              assert(frame_width % 16 == 0);
              assert(frame_height % 16 == 0);
              TbWriteTiledOutput(f_tiled_output, p_yuv_out,
                                 frame_width >> 4,
                                 frame_height >> 4, vop_number,
                                 0 /* write md5sum */ ,
                                 1 /* semi-planar data */ );
            }
          } else if(!crop_output) {
            tmp = outp_byte_size * 2 / 3;
            fwrite(p_yuv_out, 1, tmp, fout);
            for(i = 0; i < tmp / 4; i++)
              fwrite(p_yuv_out + tmp + i * 2, 1, 1, fout);
            for(i = 0; i < tmp / 4; i++)
              fwrite(p_yuv_out + tmp + 1 + i * 2, 1, 1, fout);
          } else {
            u32 j;
            u8 *p;

            tmp = outp_byte_size * 2 / 3;
            p = p_yuv_out;
            for(i = 0; i < DecPicture.coded_height; i++) {
              fwrite(p, 1, DecPicture.coded_width, fout);
              p += DecPicture.frame_width;
            }
            p = p_yuv_out + tmp;
            for(i = 0; i < DecPicture.coded_height / 2; i++) {
              for(j = 0; j < DecPicture.coded_width / 2; j++)
                fwrite(p + 2 * j, 1, 1, fout);
              p += DecPicture.frame_width;
            }
            p = p_yuv_out + tmp + 1;
            for(i = 0; i < DecPicture.coded_height / 2; i++) {
              for(j = 0; j < DecPicture.coded_width / 2; j++)
                fwrite(p + 2 * j, 1, 1, fout);
              p += DecPicture.frame_width;
            }
          }
#endif

#ifndef ASIC_TRACE_SUPPORT
          if(output_picture_endian ==
              DEC_X170_BIG_ENDIAN) {
            free(pic_copy);
          }
#endif
#else
          HandlePpOutput(vop_number, DecPicture, decoder);

#endif
        }

      } else if(DecPicture.field_picture) {
        second_field = 1;
      }
    }

  } while(!use_peek_output && tmp_ret == MP4DEC_PIC_RDY);

  if(raster_scan)
    free(raster_scan);

  return tmp_ret;
}


#ifdef USE_OUTPUT_RELEASE
/*------------------------------------------------------------------------------

    Function name: WriteOutput

    Functional description: write output picture

    Inputs:

    Outputs:

    Returns:    void

------------------------------------------------------------------------------*/

void WriteOutput(char *filename, char *filename_tiled,
                 u8 *data, u32 pic_size,
                 u32 vop_number, u32 output_picture_endian,
                 u32 frame_width, u32 frame_height,
                 MP4DecPicture DecPicture, u32 tiled_mode) {
  u8 *p_yuv_out = 0;
  MP4DecRet tmp_ret;
  MP4DecPicture tmp_picture;
  u32 i, tmp;
  u8 *raster_scan = NULL;

  if(!write_output) {
    return;
  }

  /* foutput is global file pointer */
  if(fout == NULL) {
    /* open output file for writing, can be disabled with define.
     * If file open fails -> exit */
    if(strcmp(filename, "none") != 0) {
      fout = fopen(filename, "wb");
      if(fout == NULL) {
        printf("UNABLE TO OPEN OUTPUT FILE\n");
        return;
      }
    }
  }

  /* Convert back to raster scan format if decoder outputs tiled format */
  if(tiled_mode && convert_tiled_output) {
    raster_scan = (u8*)malloc(pic_size);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode :
                     DEC_DPB_FRAME, data, raster_scan, frame_width, frame_height );
    TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode :
                     DEC_DPB_FRAME, data+frame_width*frame_height,
                     raster_scan+frame_width*frame_height, frame_width, frame_height/2 );
    data = raster_scan;
  } else if (convert_to_frame_dpb && (dpb_mode != DEC_DPB_FRAME) ) {

    raster_scan = (u8*)malloc(pic_size);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBFieldDpbToFrameDpb( 1, data,
                          raster_scan, 0, frame_width, frame_height );

    data = raster_scan;
  }


#if 0
  if(f_tiled_output == NULL && tiled_output) {
    /* open output file for writing, can be disabled with define.
     * If file open fails -> exit */
    if(strcmp(filename_tiled, "none") != 0) {
      f_tiled_output = fopen(filename_tiled, "wb");
      if(f_tiled_output == NULL) {
        printf("UNABLE TO OPEN TILED OUTPUT FILE\n");
        return;
      }
    }
  }
#endif

  if((DecPicture.field_picture && second_field) ||
      !DecPicture.field_picture) {
    /* Decoder without pp does not write out fields but a
     * frame containing both fields */
    /* PP output is written field by field */
#ifndef PP_PIPELINE_ENABLED
    if(DecPicture.field_picture)
      second_field = 0;
#endif

    fflush(stdout);
    p_yuv_out = data;

#ifndef ASIC_TRACE_SUPPORT
    u8 *pic_copy = NULL;
#endif
    assert(p_yuv_out != 0);
    assert(fout != 0);

#ifndef ASIC_TRACE_SUPPORT
    if(output_picture_endian ==
        DEC_X170_BIG_ENDIAN) {
      pic_copy = (u8 *) malloc(pic_size);
      if(NULL == pic_copy) {
        printf("MALLOC FAILED @ %s %d", __FILE__, __LINE__);
        if(raster_scan)
          free(raster_scan);
        return;
      }
      memcpy(pic_copy, p_yuv_out, pic_size);
      TbChangeEndianess(pic_copy, pic_size);
      p_yuv_out = pic_copy;
    }
#endif
    /* Write  MD5 checksum instead of the frame */
#ifdef MD5SUM
    TBWriteFrameMD5Sum(fout, p_yuv_out,
                       pic_size, vop_number);

    /* tiled */
#if 0
    if(tiled_output) {
      assert(frame_width % 16 == 0);
      assert(frame_height % 16 == 0);
      TbWriteTiledOutput(f_tiled_output, p_yuv_out,
                         frame_width >> 4, frame_height >> 4,
                         vop_number, 1 /* write md5sum */ ,
                         1 /* semi-planar data */ );
    }
#endif
#else
    if(!planar_output) {
      fwrite(p_yuv_out, 1, pic_size, fout);
      /* tiled */
      if(tiled_output) {
        assert(frame_width % 16 == 0);
        assert(frame_height % 16 == 0);
        TbWriteTiledOutput(f_tiled_output, p_yuv_out,
                           frame_width >> 4,
                           frame_height >> 4, vop_number,
                           0 /* write md5sum */ ,
                           1 /* semi-planar data */ );
      }
    } else if(!crop_output) {
      tmp = pic_size * 2 / 3;
      fwrite(p_yuv_out, 1, tmp, fout);
      for(i = 0; i < tmp / 4; i++)
        fwrite(p_yuv_out + tmp + i * 2, 1, 1, fout);
      for(i = 0; i < tmp / 4; i++)
        fwrite(p_yuv_out + tmp + 1 + i * 2, 1, 1, fout);
    } else {
      u32 j;
      u8 *p;

      tmp = pic_size * 2 / 3;
      p = p_yuv_out;
      for(i = 0; i < DecPicture.coded_height; i++) {
        fwrite(p, 1, DecPicture.coded_width, fout);
        p += DecPicture.frame_width;
      }
      p = p_yuv_out + tmp;
      for(i = 0; i < DecPicture.coded_height / 2; i++) {
        for(j = 0; j < DecPicture.coded_width / 2; j++)
          fwrite(p + 2 * j, 1, 1, fout);
        p += DecPicture.frame_width;
      }
      p = p_yuv_out + tmp + 1;
      for(i = 0; i < DecPicture.coded_height / 2; i++) {
        for(j = 0; j < DecPicture.coded_width / 2; j++)
          fwrite(p + 2 * j, 1, 1, fout);
        p += DecPicture.frame_width;
      }
    }
#endif

#ifndef ASIC_TRACE_SUPPORT
    if(output_picture_endian ==
        DEC_X170_BIG_ENDIAN) {
      free(pic_copy);
    }
#endif
  } else if(DecPicture.field_picture) {
    second_field = 1;
  }

  if(raster_scan)
    free(raster_scan);
}
#endif




/*------------------------------------------------------------------------------

    Function name: writeOutputPicture

    Functional description: write output picture

    Inputs: vop number, picture description struct, instance

    Outputs:

    Returns:    MP4DecRet

------------------------------------------------------------------------------*/

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 vop_number, MP4DecPicture DecPicture, MP4DecInst decoder) {
  PPResult res;

  res = pp_check_combined_status();

  if(res == PP_OK) {
    pp_write_output(vop_number, DecPicture.field_picture,
                    DecPicture.top_field);
    pp_read_blend_components(((DecContainer *) decoder)->pp_instance);
  }
  if(pp_update_config
      (decoder, PP_PIPELINED_DEC_TYPE_MPEG4, &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
  }

}
#endif

/*------------------------------------------------------------------------------

    Function name: MP4DecTrace

    Functional description: API trace implementation

    Inputs: string

    Outputs:

    Returns: void

------------------------------------------------------------------------------*/
void MP4DecTrace(const char *string) {
  printf("%s", string);
}

/*------------------------------------------------------------------------------

    Function name: decsw_performance

    Functional description: breakpoint for performance

    Inputs:  void

    Outputs:

    Returns: void

------------------------------------------------------------------------------*/
void decsw_performance(void) {
}
