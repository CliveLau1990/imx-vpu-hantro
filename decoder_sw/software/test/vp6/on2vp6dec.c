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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>

#include "vp6decapi.h"

#ifdef PP_PIPELINE_ENABLED
#include "ppapi.h"
#include "pptestbench.h"
#endif

#include "tb_cfg.h"
#include "tb_tiled.h"
#include "vp6hwd_container.h"
#include "tb_stream_corrupt.h"

#include "regdrv_g1.h"

#ifdef MD5SUM
#include "md5.h"
#endif

typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned char uchar;

/* Size of stream buffer */
#define STREAMBUFFER_BLOCKSIZE 2*2097151
struct DWLLinearMem stream_mem;

u32 asic_trace_enabled; /* control flag for trace generation */
#ifdef ASIC_TRACE_SUPPORT
extern u32 asic_trace_enabled; /* control flag for trace generation */
/* Stuff to enable ref buffer model support */
//#include "../../../system/models/g1hw/ref_bufferd.h"
/* Ref buffer support stuff ends */
#endif

#ifdef VP6_EVALUATION
/* Stuff to enable ref buffer model support */
#include "../../../system/models/g1hw/ref_bufferd.h"
/* Ref buffer support stuff ends */
#endif

#ifdef VP6_EVALUATION
extern u32 g_hw_ver;
#endif

/* for tracing */
u32 b_frames=0;
void printVp6PicCodingType(u32 pic_type);

#define DEBUG_PRINT(...) printf(__VA_ARGS__)

#define MAX_BUFFERS 16

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 pic_number, VP6DecPicture  DecPicture,
                    VP6DecInst decoder);
#endif

const char *const short_options = "HO:N:m_mt_pab:EGZ";

addr_t input_stream_bus_address = 0;

u32 clock_gating = DEC_X170_INTERNAL_CLOCK_GATING;
u32 data_discard = DEC_X170_DATA_DISCARD_ENABLE;
u32 latency_comp = DEC_X170_LATENCY_COMPENSATION;
u32 output_picture_endian = DEC_X170_OUTPUT_PICTURE_ENDIAN;
u32 bus_burst_length = DEC_X170_BUS_BURST_LENGTH;
u32 asic_service_priority = DEC_X170_ASIC_SERVICE_PRIORITY;
u32 output_format = DEC_X170_OUTPUT_FORMAT;
u32 service_merge_disable = DEC_X170_SERVICE_MERGE_DISABLE;
u32 ErrorConcealment = DEC_EC_PICTURE_FREEZE;

u32 pic_big_endian_size = 0;
u8* pic_big_endian = NULL;

u32 seed_rnd = 0;
u32 stream_bit_swap = 0;
i32 corrupted_bytes = 0;  /*  */
u32 stream_truncate = 0;
u32 stream_packet_loss = 0;
u32 stream_header_corrupt = 0;
u32 hdrs_rdy = 0;
u32 pic_rdy = 0;
u32 num_buffers = 3;
u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 convert_tiled_output = 0;

u32 use_peek_output = 0;

VP6DecInst dec_inst;
#ifdef USE_EXTERNAL_BUFFER
const void *dwl_inst = NULL;
u32 use_extra_buffers = 0;
#ifdef USE_OUTPUT_RELEASE
u32 allocate_extra_buffers_in_output = 0;
#endif
u32 buffer_size;
u32 external_buf_num;  /* external buffers allocated yet. */
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
    if(add_extra_flag && (external_buf_num < MAX_BUFFERS)) {
      struct DWLLinearMem mem;
      i32 dwl_ret;
      if (pp_enabled)
        dwl_ret = DWLMallocLinear(dwl_inst, buffer_size, &mem);
      else
        dwl_ret = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
      if(dwl_ret == DWL_OK) {
        VP6DecRet rv = VP6DecAddBuffer(dec_inst, &mem);
        if(rv != VP6DEC_OK && rv != VP6DEC_WAITING_FOR_BUFFER) {
          if (pp_enabled)
            DWLFreeLinear(dwl_inst, &mem);
          else
            DWLFreeRefFrm(dwl_inst, &mem);
        } else {
          ext_buffers[external_buf_num++] = mem;
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
  printf("Releasing %d external frame buffers\n", external_buf_num);
  pthread_mutex_lock(&ext_buffer_contro);
  for(i=0; i<external_buf_num; i++) {
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

const struct option long_options[] = {
  {"help", 0, NULL, 'H'},
  {"output", 1, NULL, 'O'},
  {"last_pic", 1, NULL, 'N'},
  {"md5-partial", 0, NULL, 'm'},
  {"md5-total", 0, NULL, 'M'},
  {"trace", 0, NULL, 't'},
  {"planar", 0, NULL, 'P'},
  {"alpha", 0, NULL, 'A'},
  {"buffers", 1, NULL, 'B'},
  {"tiled-mode", 0, NULL, 'E'},
  {"convert-tiled", 0, NULL, 'G'},
  {"peek", 0, NULL, 'Z'},
  {NULL, 0, NULL, 0}
};

typedef struct {
  const char *input;
  const char *output;
  const char *pp_cfg;
  int last_frame;
  int md5;
  int planar;
  int alpha;
  int f,d,s;
} options_s;

struct TBCfg tb_cfg;

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
void writeRawFrame(FILE * fp, unsigned char *buffer, int frame_size, int md5,
                   int planar, int width, int height, int tiled_mode);

pthread_t output_thread;
pthread_t release_thread;
int output_thread_run = 0;
FILE *out_file = NULL;
options_s options;

sem_t buf_release_sem;
VP6DecPicture buf_list[100] = {0};
u32 buf_status[100] = {0};
u32 list_pop_index = 0;
u32 list_push_index = 0;
u32 last_pic_flag = 0;
unsigned int current_video_frame = 0;

/* buf release thread entry point. */
static void* buf_release_thread(void* arg) {
  while(1) {
    /* Pop output buffer from buf_list and consume it */
    if(buf_status[list_pop_index]) {
      sem_wait(&buf_release_sem);
      VP6DecPictureConsumed(dec_inst, &buf_list[list_pop_index]);
      buf_status[list_pop_index] = 0;
      list_pop_index++;
      if(list_pop_index == 100)
        list_pop_index = 0;

#ifdef USE_EXTERNAL_BUFFER
      if(allocate_extra_buffers_in_output) {
        pthread_mutex_lock(&ext_buffer_contro);
        if(add_extra_flag && (external_buf_num < MAX_BUFFERS)) {
          struct DWLLinearMem mem;
          i32 dwl_ret;
          if (pp_enabled)
            dwl_ret = DWLMallocLinear(dwl_inst, buffer_size, &mem);
          else
            dwl_ret = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
          if(dwl_ret == DWL_OK) {
            VP6DecRet rv = VP6DecAddBuffer(dec_inst, &mem);
            if(rv != VP6DEC_OK && rv != VP6DEC_WAITING_FOR_BUFFER) {
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
static void* vp6_output_thread(void* arg) {
  VP6DecPicture dec_pic;
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
    VP6DecRet ret;
    ret = VP6DecNextPicture(dec_inst, &dec_pic, 0);
    if(ret == VP6DEC_PIC_RDY) {
      if(!use_peek_output) {
#ifndef PP_PIPELINE_ENABLED
        writeRawFrame(out_file, (unsigned char *) dec_pic.p_output_frame,
                      dec_pic.frame_width * dec_pic.frame_height * 3 / 2,
                      options.md5, options.planar,
                      dec_pic.frame_width, dec_pic.frame_height,
                      dec_pic.output_format);
#else
        HandlePpOutput(current_video_frame, dec_pic, dec_inst);
#endif
      }

      /* pic coding type */
      printVp6PicCodingType(dec_pic.pic_coding_type);
      /* Push output buffer into buf_list and wait to be consumed */
      buf_list[list_push_index] = dec_pic;
      buf_status[list_push_index] = 1;
      list_push_index++;
      if(list_push_index == 100)
        list_push_index = 0;

      sem_post(&buf_release_sem);

      pic_display_number++;
    }

    else if(ret == VP6DEC_END_OF_STREAM) {
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



void print_usage(const char *prog) {
  fprintf(stdout, "Usage:\n%s [options] <stream.vp6>\n\n", prog);

  fprintf(stdout, "    -H       --help              Print this help.\n");

  fprintf(stdout,
          "    -O[file] --output            Write output to specified file. [out.yuv]\n");

  fprintf(stdout,
          "    -N[n]    --last_frame         Forces decoding to stop after n frames; -1 sets no limit.[-1]\n");
  fprintf(stdout,
          "    -m       --md5-partial       Write MD5 checksum for each picture (YUV not written).\n");
  fprintf(stdout,
          "    -M       --md5-total         Write MD5 checksum of whole sequence (YUV not written).\n");
  fprintf(stdout,
          "    -P       --planar            Write planar output\n");
  fprintf(stdout,
          "    -E       --tiled-mode        Signal decoder to use tiled reference frame format\n");
  fprintf(stdout,
          "    -G       --convert-tiled     Convert tiled mode output pictures into raster scan format\n");
  fprintf(stdout,
          "    -A       --alpha             Stream contains alpha channelt\n");
  fprintf(stdout,
          "    -Bn                          Use n frame buffers in decoder\n");
  fprintf(stdout,
          "    -Z       --peek              Output pictures using VP6DecPeek() function\n");
#ifdef USE_EXTERNAL_BUFFER
  fprintf(stdout,
          "    --AddBuffer                  Add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
  fprintf(stdout,
          "    --addbuffer                  Add extra external buffer in ouput thread randomly\n");
#endif
#endif

  fprintf(stdout, "\n");
}

int getoptions(int argc, char **argv, options_s * opts) {
  int next_opt;

#ifdef ASIC_TRACE_SUPPORT
  asic_trace_enabled = 0;
#endif /* ASIC_TRACE_SUPPORT */

  do {
    next_opt = getopt_long(argc, argv, short_options, long_options, NULL);
    if(next_opt == -1)
      return 0;

    switch (next_opt) {
    case 'H':
      print_usage(argv[0]);
      exit(0);
    case 'O':
      opts->output = optarg;
      break;
    case 'N':
      opts->last_frame = atoi(optarg);
      break;
    case 'm':
      opts->md5 = 1;
      break;
    case 'M':
      opts->md5 = 2;
      break;
    case 'P':
      opts->planar = 1;
      break;
    case 'E':
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
      break;
    case 'G':
      convert_tiled_output = 1;
      break;
    case 'B':
      num_buffers = atoi(optarg);
      break;
    case 'A':
      opts->alpha = 1;
      break;
    case 't':
#ifdef ASIC_TRACE_SUPPORT
      asic_trace_enabled = 2;
#else
      fprintf(stdout, "\nWarning! Trace generation not supported!\n");
#endif
      break;

    case 'Z':
      use_peek_output = 1;
      break;

    default:
      fprintf(stderr, "Unknown option\n");
    }
  } while(1);
}

/*
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/

#define leRushort( P, V) { \
  register ushort v = (uchar) *P++; \
  (V) = v + ( (ushort) (uchar) *P++ << 8); \
}
#define leRushortF( F, V) { \
  char b[2], *p = b; \
  fread( (void *) b, 1, 2, F); \
  leRushort( p, V); \
}

#define leRulong( P, V) { \
  register ulong v = (uchar) *P++; \
  v += (ulong) (uchar) *P++ << 8; \
  v += (ulong) (uchar) *P++ << 16; \
  (V) = v + ( (ulong) (uchar) *P++ << 24); \
}
#define leRulongF( F, V) { \
  char b[4] = {0,0,0,0}, *p = b; \
    V = 0; \
  ret = fread( (void *) b, 1, 4, F); \
  leRulong( p, V); \
}

#if 0
#  define leWchar( P, V)  { * ( (char *) P)++ = (char) (V);}
#else
#  define leWchar( P, V)  { *P++ = (char) (V);}
#endif

#define leWshort( P, V)  { \
  register short v = (V); \
  leWchar( P, v) \
  leWchar( P, v >> 8) \
}
#define leWshortF( F, V) { \
  char b[2], *p = b; \
  leWshort( p, V); \
  fwrite( (void *) b, 1, 2, F); \
}

#define leWlong( P, V)  { \
  register long v = (V); \
  leWchar( P, v) \
  leWchar( P, v >> 8) \
  leWchar( P, v >> 16) \
  leWchar( P, v >> 24) \
}
#define leWlongF( F, V)  { \
  char b[4], *p = b; \
  leWlong( p, V); \
  fwrite( (void *) b, 1, 4, F); \
}

/*
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/

int readCompressedFrame(FILE * fp) {
  ulong frame_size = 0;
  int ret;

  leRulongF(fp, frame_size);

  if( frame_size >= STREAMBUFFER_BLOCKSIZE ) {
    /* too big a frame */
    return 0;
  }

  ret = fread(stream_mem.virtual_address, 1, frame_size, fp);

  return (int) frame_size;
}

/*
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/

void writeRawFrame(FILE * fp, unsigned char *buffer, int frame_size, int md5,
                   int planar, int width, int height, int tiled_mode) {
  u8 *raster_scan = NULL;
#ifdef MD5SUM
  static struct MD5Context ctx;
#endif

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
                     raster_scan, width, height );
    TBTiledToRaster( tiled_mode, DEC_DPB_FRAME, buffer+width*height,
                     raster_scan+width*height, width, height/2 );
    buffer = raster_scan;
  }

#ifndef ASIC_TRACE_SUPPORT
  if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
    if(pic_big_endian_size != frame_size) {
      if(pic_big_endian != NULL)
        free(pic_big_endian);

      pic_big_endian = (u8 *) malloc(frame_size);
      if(pic_big_endian == NULL) {
        DEBUG_PRINT("MALLOC FAILED @ %s %d", __FILE__, __LINE__);
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
#ifdef MD5SUM
  if(md5 == 1) {
    unsigned char digest[16];
    int i = 0;

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
  } else {
  }
#endif

  if (!planar)
    fwrite(buffer, frame_size, 1, fp);
  else {
    u32 i, tmp;
    tmp = frame_size * 2 / 3;
    fwrite(buffer, 1, tmp, fp);
    for(i = 0; i < tmp / 4; i++)
      fwrite(buffer + tmp + i * 2, 1, 1, fp);
    for(i = 0; i < tmp / 4; i++)
      fwrite(buffer + tmp + 1 + i * 2, 1, 1, fp);

    /*
            u32 i, tmp;
            tmp = frame_size*2/3;
            fwrite(buffer, tmp, 1, fp);
            buffer += tmp;
            tmp /= 4;
            for (i = 0; i < tmp; i++)
            {
                fwrite(buffer+i, 1, 1, fp);
                fwrite(buffer+tmp+i, 1, 1, fp);
            }
    */
  }

  if(raster_scan)
    free(raster_scan);
}

/*
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/

int decode_file(const options_s * opts) {
  VP6DecInput input;
  VP6DecOutput output;
  VP6DecPicture dec_pic, tmp_pic;
  VP6DecRet ret;
  u32 tmp;
#ifdef USE_EXTERNAL_BUFFER
  u32 prev_width = 0, prev_height = 0;
  u32 min_buffer_num = 0;
  u32 buffer_release_flag = 1;
  VP6DecBufferInfo hbuf;
  VP6DecRet rv;
  int i;
  memset(ext_buffers, 0, sizeof(ext_buffers));
  pthread_mutex_init(&ext_buffer_contro, NULL);
  struct DWLInitParam dwl_init;
  dwl_init.client_type = DWL_CLIENT_TYPE_VP6_DEC;
#endif

  memset(&dec_pic, 0, sizeof(VP6DecPicture));

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  FILE *out_file = NULL;
#endif
  FILE *input_file = fopen(opts->input, "rb");

  if(input_file == NULL) {
    perror(opts->input);
    return -1;
  }
#ifndef PP_PIPELINE_ENABLED
  out_file = fopen(opts->output, "wb");

  if(out_file == NULL) {
    perror(opts->output);
    return -1;
  }
#endif

#ifdef ASIC_TRACE_SUPPORT
  tmp = openTraceFiles();
  if (!tmp) {
    DEBUG_PRINT(("UNABLE TO OPEN TRACE FILE(S)\n"));
  }
#endif

#ifdef USE_EXTERNAL_BUFFER
  dwl_inst = DWLInit(&dwl_init);
  if(dwl_inst == NULL) {
    DEBUG_PRINT(("H264DecInit# ERROR: DWL Init failed\n"));
    goto end;
  }
#endif

  ret = VP6DecInit(&dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                   dwl_inst,
#endif
                   ErrorConcealment,
                   num_buffers,
                   tiled_output, 0, 0);
  if (ret != VP6DEC_OK) {
    printf("DECODER INITIALIZATION FAILED\n");
    goto end;
  }

  /* Set ref buffer test mode */
  ((VP6DecContainer_t *) dec_inst)->ref_buffer_ctrl.test_function = TBRefbuTestMode;

#ifdef PP_PIPELINE_ENABLED
  /* Initialize the post processer. If unsuccessful -> exit */
  if(pp_startup
      ((char*)opts->pp_cfg, dec_inst, PP_PIPELINED_DEC_TYPE_VP6, &tb_cfg) != 0) {
    fprintf(stdout, "PP INITIALIZATION FAILED\n");
    goto end;
  }

  if(pp_update_config (dec_inst, PP_PIPELINED_DEC_TYPE_VP6, &tb_cfg) ==
      CFG_UPDATE_FAIL) {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
    goto end;
  }
#endif

  SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  /*
  SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_DEC_OUT_TILED_E,
                 output_format);
                 */
  SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_DEC_OUT_ENDIAN,
                 output_picture_endian);
  SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_DEC_MAX_BURST,
                 bus_burst_length);
  if ((DWLReadAsicID(DWL_CLIENT_TYPE_VP6_DEC) >> 16) == 0x8170U) {
    SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_PRIORITY_MODE,
                   asic_service_priority);
  }
  SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_DEC_DATA_DISC_E,
                 data_discard);
  SetDecRegister(((VP6DecContainer_t *) dec_inst)->vp6_regs, HWIF_SERV_MERGE_DIS,
                 service_merge_disable);

  /* allocate memory for stream buffer. if unsuccessful -> exit */
  stream_mem.virtual_address = NULL;
  stream_mem.bus_address = 0;

  if(DWLMallocLinear(((VP6DecContainer_t *) dec_inst)->dwl,
                     STREAMBUFFER_BLOCKSIZE, &stream_mem) != DWL_OK) {
    printf(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
    exit(-1);
  }

  while(!feof(input_file) &&
        current_video_frame < (unsigned int) opts->last_frame) {
    fflush(stdout);

    input.data_len = readCompressedFrame(input_file);
    input.stream = (u8*)stream_mem.virtual_address;
    input.stream_bus_address = (addr_t)stream_mem.bus_address;

    if(input.data_len == 0)
      break;

    if( opts->alpha ) {
      if( input.data_len < 3 )
        break;
      input.data_len -= 3;
      input.stream += 3;
      input.stream_bus_address += 3;
    }

    if(stream_truncate && pic_rdy && (hdrs_rdy || stream_header_corrupt)) {
      i32 ret;

      ret = TBRandomizeU32(&input.data_len);
      if(ret != 0) {
        DEBUG_PRINT("RANDOM STREAM ERROR FAILED\n");
        return 0;
      }
    }

    /* If enabled, break the stream */
    if(stream_bit_swap) {
      if((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt) {
        /* Picture needs to be ready before corrupting next picture */
        if(pic_rdy && corrupted_bytes <= 0) {
          ret =
            TBRandomizeBitSwapInStream((u8 *)input.stream,
                                       input.data_len,
                                       tb_cfg.tb_params.
                                       stream_bit_swap);
          if(ret != 0) {
            DEBUG_PRINT("RANDOM STREAM ERROR FAILED\n");
            goto end;
          }

          corrupted_bytes = input.data_len;
        }
      }
    }

    do {
      ret = VP6DecDecode(dec_inst, &input, &output);
      /* printf("VP6DecDecode retruned: %d\n", ret); */

      if(ret == VP6DEC_HDRS_RDY) {
#ifdef USE_EXTERNAL_BUFFER
        rv = VP6DecGetBufferInfo(dec_inst, &hbuf);
        printf("VP6DecGetBufferInfo ret %d\n", rv);
        printf("buf_to_free %p, next_buf_size %d, buf_num %d\n",
               (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num);
#endif
        VP6DecInfo dec_info;

        TBSetRefbuMemModel( &tb_cfg,
                            ((VP6DecContainer_t *) dec_inst)->vp6_regs,
                            &((VP6DecContainer_t *) dec_inst)->ref_buffer_ctrl );

        hdrs_rdy = 1;

        ret = VP6DecGetInfo(dec_inst, &dec_info);
        if (ret != VP6DEC_OK) {
          DEBUG_PRINT("ERROR in getting stream info!\n");
          goto end;
        }

        fprintf(stdout, "\n"
                "Stream info: vp6_version  = 6.%d\n"
                "             vp6_profile  = %s\n"
                "             coded size  = %d x %d\n"
                "             scaled size = %d x %d\n\n",
                dec_info.vp6_version - 6,
                dec_info.vp6_profile == 0 ? "SIMPLE" : "ADVANCED",
                dec_info.frame_width, dec_info.frame_height,
                dec_info.scaled_width, dec_info.scaled_height);
#ifdef USE_EXTERNAL_BUFFER
        if((dec_info.pic_buff_size != min_buffer_num) ||
            (dec_info.frame_width * dec_info.frame_height != prev_width * prev_height)) {
          /* Reset buffers added and stop adding extra buffers when a new header comes. */
          if (pp_enabled)
            res_changed = 1;
          else {
            add_extra_flag = 0;
            ReleaseExtBuffers();
            buffer_release_flag = 1;
            external_buf_num = 0;
          }
        }
        prev_width = dec_info.frame_width;
        prev_height = dec_info.frame_height;
        min_buffer_num = dec_info.pic_buff_size;
#endif
        ret = VP6DEC_HDRS_RDY;  /* restore */
      }
#ifdef USE_EXTERNAL_BUFFER
      if (ret == VP6DEC_WAITING_FOR_BUFFER) {
        DEBUG_PRINT(("Waiting for frame buffers\n"));
        struct DWLLinearMem mem;

        rv = VP6DecGetBufferInfo(dec_inst, &hbuf);
        printf("VP6DecGetBufferInfo ret %d\n", rv);
        printf("buf_to_free %p, next_buf_size %d, buf_num %d\n",
               (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num);

        if (hbuf.buf_to_free.virtual_address != NULL && res_changed) {
          add_extra_flag = 0;
          ReleaseExtBuffers();
          buffer_release_flag = 1;
          num_buffers = 0;
          res_changed = 0;
        }


        buffer_size = hbuf.next_buf_size;
        if(buffer_release_flag && hbuf.next_buf_size) {
          /* Only add minimum required buffers at first. */
          for(i = 0; i < hbuf.buf_num; i++) {
            if (pp_enabled)
              DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
            else
              DWLMallocRefFrm(dwl_inst, hbuf.next_buf_size, &mem);
            rv = VP6DecAddBuffer(dec_inst, &mem);

            printf("VP6DecAddBuffer ret %d\n", rv);
            if(rv != VP6DEC_OK && rv != VP6DEC_WAITING_FOR_BUFFER) {
              if (pp_enabled)
                DWLFreeLinear(dwl_inst, &mem);
              else
                DWLFreeRefFrm(dwl_inst, &mem);
            } else {
              ext_buffers[i] = mem;
            }
          }
          /* Extra buffers are allowed when minimum required buffers have been added.*/
          external_buf_num = hbuf.buf_num;
          add_extra_flag = 1;
        }
        ret = VP6DEC_HDRS_RDY;
      }
#endif
    } while(ret == VP6DEC_HDRS_RDY || ret == VP6DEC_NO_DECODING_BUFFER);

    if(ret == VP6DEC_PIC_DECODED) {
      pic_rdy = 1;

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
      if (!output_thread_run) {
        output_thread_run = 1;
        sem_init(&buf_release_sem, 0, 0);
        pthread_create(&output_thread, NULL, vp6_output_thread, NULL);
        pthread_create(&release_thread, NULL, buf_release_thread, NULL);
      }
#endif

#ifdef USE_EXTERNAL_BUFFER
      if(use_extra_buffers && !add_buffer_thread_run) {
        add_buffer_thread_run = 1;
        pthread_create(&add_buffer_thread, NULL, AddBufferThread, NULL);
      }
#endif

      if (use_peek_output)
        ret = VP6DecPeek(dec_inst, &dec_pic);

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      else
        ret = VP6DecNextPicture(dec_inst, &dec_pic, 0);
#endif

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      if (ret == VP6DEC_PIC_RDY)
#else
      if (ret == VP6DEC_PIC_RDY && use_peek_output)
#endif
      {
#ifndef PP_PIPELINE_ENABLED
        writeRawFrame(out_file, (unsigned char *) dec_pic.p_output_frame,
                      dec_pic.frame_width * dec_pic.frame_height * 3 / 2,
                      opts->md5, opts->planar,
                      dec_pic.frame_width, dec_pic.frame_height,
                      dec_pic.output_format);
#else
        HandlePpOutput(current_video_frame, dec_pic, dec_inst);
#endif
      }

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      if (use_peek_output)
        ret = VP6DecNextPicture(dec_inst, &tmp_pic, 0);
#endif

      current_video_frame++;
#if 0
      if (current_video_frame == 10) {
        ret = VP6DecAbort(dec_inst);
        ret = VP6DecAbortAfter(dec_inst);
        rewind(input_file);
      }
#endif

      fprintf(stdout, "Picture %d,", current_video_frame);

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      /* pic coding type */
      printVp6PicCodingType(dec_pic.pic_coding_type);
#endif
    }

    corrupted_bytes = 0;
  }

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  /* last picture from the buffer */
  if (!use_peek_output) {
    ret = VP6DecNextPicture(dec_inst, &dec_pic, 1);
    if (ret == VP6DEC_PIC_RDY) {
#ifndef PP_PIPELINE_ENABLED
      writeRawFrame(out_file, (unsigned char *) dec_pic.p_output_frame,
                    dec_pic.frame_width * dec_pic.frame_height * 3 / 2,
                    opts->md5, opts->planar,
                    dec_pic.frame_width, dec_pic.frame_height,
                    dec_pic.output_format);
#else
      HandlePpOutput(current_video_frame, dec_pic, dec_inst);
#endif
    }
  }
#else
  if (output_thread_run)
    VP6DecEndOfStream(dec_inst, 1);
#endif

end:

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
  if(output_thread)
    pthread_join(output_thread, NULL);
  if(release_thread)
    pthread_join(release_thread, NULL);
#endif

#ifdef USE_EXTERNAL_BUFFER
  add_buffer_thread_run = 0;
#endif

  printf("Pictures decoded: %d\n", current_video_frame);

#ifdef PP_PIPELINE_ENABLED
  pp_close();
#endif

  if(stream_mem.virtual_address)
    DWLFreeLinear(((VP6DecContainer_t *) dec_inst)->dwl, &stream_mem);
#ifdef USE_EXTERNAL_BUFFER
  ReleaseExtBuffers();
  pthread_mutex_destroy(&ext_buffer_contro);
#endif
  VP6DecRelease(dec_inst);
#ifdef USE_EXTERNAL_BUFFER
  DWLRelease(dwl_inst);
#endif

  fclose(input_file);
#ifndef PP_PIPELINE_ENABLED
  fclose(out_file);
#endif
  if (pic_big_endian != NULL)
    free(pic_big_endian);

  return 0;
}


int main(int argc, char *argv[]) {
  int i, ret;
  FILE *f_tbcfg;
  const char default_out[] = "out.yuv";
  const char default_pp_out[] = "pp_out.yuv";
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  options_s options;
#endif

  memset(&options, 0, sizeof(options_s));

#ifdef PP_PIPELINE_ENABLED
  PPApiVersion pp_ver;
  PPBuild pp_build;
#endif

#ifdef VP6_EVALUATION_9190
  g_hw_ver = 9190;
#elif VP6_EVALUATION_G1
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

#ifndef PP_PIPELINE_ENABLED
  if(argc < 2) {
    print_usage(argv[0]);
    exit(0);
  }

  options.output = default_out;
  options.input = argv[argc - 1];
  options.md5 = 0;
  options.last_frame = -1;
  options.planar = 0;
#ifndef USE_EXTERNAL_BUFFER
  getoptions(argc - 1, argv, &options);
#else
  for (i = 1; i < (u32)(argc-1); i++) {
    if(strcmp(argv[i], "-H") == 0) {
      print_usage(argv[0]);
      exit(0);
    } else if(strcmp(argv[i], "-O") == 0) {
      options.output = optarg;
    } else if(strcmp(argv[i], "-N") == 0) {
      options.last_frame = atoi(optarg);
    } else if(strcmp(argv[i], "-m") == 0) {
      options.md5 = 1;
    } else if(strcmp(argv[i], "-M") == 0) {
      options.md5 = 2;
    } else if(strcmp(argv[i], "-P") == 0) {
      options.planar = 1;
    } else if(strcmp(argv[i], "-E") == 0) {
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    } else if(strcmp(argv[i], "-G") == 0) {
      convert_tiled_output = 1;
    } else if(strcmp(argv[i], "-B") == 0) {
      num_buffers = atoi(optarg);
    } else if(strcmp(argv[i], "-A") == 0) {
      options.alpha = 1;
    } else if(strcmp(argv[i], "-t") == 0) {
#ifdef ASIC_TRACE_SUPPORT
      asic_trace_enabled = 2;
#else
      fprintf(stdout, "\nWarning! Trace generation not supported!\n");
#endif
    } else if(strcmp(argv[i], "-Z") == 0) {
      use_peek_output = 1;
    }
#ifdef USE_EXTERNAL_BUFFER
    else if(strcmp(argv[i], "--AddBuffer") == 0) {
      use_extra_buffers = 1;
    }
#ifdef USE_OUTPUT_RELEASE
    else if(strcmp(argv[i], "--addbuffer") == 0) {
      use_extra_buffers = 0;
      allocate_extra_buffers_in_output = 1;
    }
#endif
#endif
    else {
      fprintf(stdout, "UNKNOWN PARAMETER: %s\n", argv[i]);
    }
  }
#endif
#else
  if(argc < 3) {
    printf("\nVp6 Decoder PP Pipelined Testbench\n\n");
    printf("USAGE:\n%s [options] stream.vp6 pp.cfg\n", argv[0]);
    printf("-Nn to decode only first n vops of the stream\n");
    printf("-A  stream contains alpha channel\n");
#ifdef USE_EXTERNAL_BUFFER
    printf("--AddBuffer add extra external buffer randomly\n");
#ifdef USE_OUTPUT_RELEASE
    printf("--addbuffer add extra external buffer in output thread\n");
#endif
#endif
    exit(100);
  }

  options.output = default_pp_out;
  options.input = argv[argc - 2];
  options.pp_cfg = argv[argc - 1];
  options.md5 = 0;
  options.last_frame = -1;
  options.planar = 0;
  options.alpha = 0;
  /* read cmdl parameters */
  for(i = 1; i < argc - 2; i++) {
    if(strncmp(argv[i], "-N", 2) == 0) {
      options.last_frame = atoi(argv[i] + 2);
    } else if(strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if(strncmp(argv[i], "-A", 2) == 0) {
      options.alpha = 1;
    }
#ifdef USE_EXTERNAL_BUFFER
    else if(strcmp(argv[i], "--AddBuffer") == 0) {
      use_extra_buffers = 1;
    }
#ifdef USE_OUTPUT_RELEASE
    else if(strcmp(argv[i], "--addbuffer") == 0) {
      use_extra_buffers = 0;
      allocate_extra_buffers_in_output = 1;
    }
#endif
#endif
    else {
      fprintf(stdout, "UNKNOWN PARAMETER: %s\n", argv[i]);
      return 1;
    }
  }

  /* Print API and build version numbers */
  pp_ver = PPGetAPIVersion();
  pp_build = PPGetBuild();

  /* Version */
  fprintf(stdout,
          "\nX170 PP API v%d.%d - SW build: %d - HW build: %x\n",
          pp_ver.major, pp_ver.minor, pp_build.sw_build, pp_build.hw_build);
#endif

  /* check if traces shall be enabled */
#ifdef ASIC_TRACE_SUPPORT
  {
    char trace_string[80];
    FILE *fid = fopen("trace.cfg", "r");
    if (fid) {
      /* all tracing enabled if any of the recognized keywords found */
      while(fscanf(fid, "%s\n", trace_string) != EOF) {
        if (!strcmp(trace_string, "toplevel") ||
            !strcmp(trace_string, "all"))
          asic_trace_enabled = 2;
        else if(!strcmp(trace_string, "fpga") ||
                !strcmp(trace_string, "decoding_tools"))
          if(asic_trace_enabled == 0)
            asic_trace_enabled = 1;
      }
    }
  }
#endif

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

#ifdef ASIC_TRACE_SUPPORT

  /*
      {
          extern refBufferd_t    vp6Refbuffer;
          refBufferd_Reset(&vp6Refbuffer);
      }
      */

#endif /* ASIC_TRACE_SUPPORT */

#ifdef VP6_EVALUATION

  /*
      {
          extern refBufferd_t    vp6Refbuffer;
          refBufferd_Reset(&vp6Refbuffer);
      }
      */

#endif /* VP6_EVALUATION */

  clock_gating = TBGetDecClockGating(&tb_cfg);
  data_discard = TBGetDecDataDiscard(&tb_cfg);
  latency_comp = tb_cfg.dec_params.latency_compensation;
  output_picture_endian = TBGetDecOutputPictureEndian(&tb_cfg);
  bus_burst_length = tb_cfg.dec_params.bus_burst_length;
  asic_service_priority = tb_cfg.dec_params.asic_service_priority;
  output_format = TBGetDecOutputFormat(&tb_cfg);
  service_merge_disable = TBGetDecServiceMergeDisable(&tb_cfg);

  /* ErrorConcealment */
  ErrorConcealment = TBGetDecErrorConcealment(&tb_cfg);

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

  TBInitializeRandom(seed_rnd);

  {
    VP6DecApiVersion dec_api;
    VP6DecBuild dec_build;

    /* Print API version number */
    dec_api = VP6DecGetAPIVersion();
    dec_build = VP6DecGetBuild();
    DEBUG_PRINT
    ("\nG1 VP6 Decoder API v%d.%d - SW build: %d - HW build: %x\n\n",
     dec_api.major, dec_api.minor, dec_build.sw_build, dec_build.hw_build);
  }

  ret = decode_file(&options);

#ifdef ASIC_TRACE_SUPPORT
  {
    extern u32 hw_dec_pic_count;
    trace_SequenceCtrl( hw_dec_pic_count );
    trace_RefbufferHitrate();
  }
#endif /* ASIC_TRACE_SUPPORT */

  return ret;
}

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 vop_number, VP6DecPicture DecPicture, VP6DecInst decoder) {
  PPResult res;

  res = pp_check_combined_status();

  if(res == PP_OK) {
    pp_write_output(vop_number, 0, 0);
    pp_read_blend_components(((VP6DecContainer_t *) decoder)->pp.pp_instance);
  }
  if(pp_update_config
      (decoder, PP_PIPELINED_DEC_TYPE_VP6, &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
  }

}
#endif

/*------------------------------------------------------------------------------

    Function name:            printVp6PicCodingType

    Functional description:   Print out picture coding type value

------------------------------------------------------------------------------*/
void printVp6PicCodingType(u32 pic_type) {
  switch (pic_type) {
  case DEC_PIC_TYPE_I:
    printf(" DEC_PIC_TYPE_I\n");
    break;
  case DEC_PIC_TYPE_P:
    printf(" DEC_PIC_TYPE_P\n");
    break;
  default:
    printf("Other %d\n", pic_type);
    break;
  }
}
