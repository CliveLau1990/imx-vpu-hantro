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

#include "vc1decapi.h"
#include "dwl.h"
#include "trace.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef USE_EFENCE
#include "efence.h"
#endif

#include "vc1hwd_container.h"
#include "regdrv_g1.h"
#include "tb_cfg.h"
#include "tb_tiled.h"

#ifdef PP_PIPELINE_ENABLED
#include "pptestbench.h"
#include "ppapi.h"
#endif

#ifdef MD5SUM
#include "tb_md5.h"
#endif

#include "tb_sw_performance.h"
#include "tb_stream_corrupt.h"

#ifdef ASIC_TRACE_SUPPORT
#include "trace.h"
#endif

/* Define RCV format metadata max size. Standard specifies 44 bytes, add one
 * to support some non-compliant streams */
#define RCV_METADATA_MAX_SIZE   (44+1)
#define MAX_BUFFERS 16

void VC1DecTrace(const char *string) {
  printf("%s\n", string);
}

/*------------------------------------------------------------------------------
Module defines
------------------------------------------------------------------------------*/

#define VC1_MAX_STREAM_SIZE  DEC_X170_MAX_STREAM>>1

/* Debug prints */
#define DEBUG_PRINT(argv) printf argv

/*void decsw_performance(void)  __attribute__((noinline));*/
void decsw_performance(void);

void WriteOutput(char *filename, char *filename_tiled, u8 * data, u32 frame_number,
                 u32 width, u32 height, u32 interlaced, u32 top, u32 first_field,
                 u32 tiled_mode );
u32 NextPacket(u8 ** p_strm);
u32 CropPicture(u8 *p_out_image, u8 *p_in_image,
                u32 pic_width, u32 pic_height, u32 out_width, u32 out_height );

void FramePicture( u8 *p_in, i32 in_width, i32 in_height,
                   i32 in_frame_width, i32 in_frame_height,
                   u8 *p_out, i32 out_width, i32 out_height );
u32 fillBuffer(u8 *stream);

/* Global variables for stream handling */
u32 rcv_v2;
u32 rcv_metadata_size = 0;
u8 *stream_stop = NULL;
FILE *foutput = NULL;
FILE *f_tiled_output = NULL;
FILE *finput;

i32 DecodeRCV(u8 *stream, u32 strm_len, VC1DecMetaData *meta_data);
u32 DecodeFrameLayerData(u8 *stream);
static void vc1DecPrintReturnValue(VC1DecRet ret);
static u32 GetNextDuSize(const u8* stream, const u8* stream_start,
                         u32 strm_len, u32 *skipped_bytes);
void printVc1PicCodingType(u32 *pic_type);

/* stream start address */
u8 *byte_strm_start;

u32 enable_frame_picture = 0;
u32 number_of_written_frames = 0;
u32 num_frame_buffers = 0;
/* index */
u32 save_index = 0;
u32 use_index = 0;
FILE *f_index = NULL;

off64_t cur_index = 0;
off64_t next_index = 0;
off64_t last_stream_pos = 0;
u32 ds_ratio_x, ds_ratio_y;

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
u32 interlaced_field = 0;
u32 stream_packet_loss = 0;
u32 stream_truncate = 0;
u32 stream_header_corrupt = 0;
u32 hdrs_rdy = 0;
u32 pic_rdy = 0;
u32 slice_ud_in_packet = 0;
u32 use_peek_output = 0;
u32 skip_non_reference = 0;
u32 slice_mode = 0;
u32 field_output = 0;

u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 dpb_mode = DEC_DPB_FRAME;
u32 dump_dpb_contents = 1;
u32 convert_tiled_output = 0;
u32 convert_to_frame_dpb = 0;

struct TBCfg tb_cfg;
/* for tracing */
u32 b_frames=0;
#ifdef ASIC_TRACE_SUPPORT
extern u32 hw_dec_pic_count;
extern u32 g_hw_ver;
#endif

#ifdef VC1_EVALUATION
extern u32 g_hw_ver;
#endif

VC1DecInst dec_inst;
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
u32 res_changed = 0;
/* Fixme: this value should be set based on option "-d" when invoking testbench. */
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
        VC1DecRet rv = VC1DecAddBuffer(dec_inst, &mem);
        if( rv != VC1DEC_OK && rv != VC1DEC_WAITING_FOR_BUFFER) {
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
    //DEBUG_PRINT(("DWLFreeLinear ret %d\n", rv));
  }
  pthread_mutex_unlock(&ext_buffer_contro);
}
#endif

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
VC1DecInfo info;
VC1DecMetaData meta_data;
char out_file_name[256] = "";
char out_file_name_tiled[256] = "out_tiled.yuv";
u32 crop_display = 0;
u8 *tmp_image = NULL;
u32 coded_pic_width = 0;
u32 coded_pic_height = 0;
u32 end_of_stream = 0;
pthread_t output_thread;
pthread_t release_thread;
int output_thread_run = 0;

sem_t buf_release_sem;
VC1DecPicture buf_list[100];
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
      VC1DecPictureConsumed(dec_inst, &buf_list[list_pop_index]);
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
            VC1DecRet rv = VC1DecAddBuffer(dec_inst, &mem);
            if( rv != VC1DEC_OK && rv != VC1DEC_WAITING_FOR_BUFFER) {
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
    usleep(50000);
  }
  return NULL;
}

/* Output thread entry point. */
static void* vc1_output_thread(void* arg) {
  VC1DecPicture dec_picture;
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
    VC1DecRet ret;
    u32 tmp;

    ret = VC1DecNextPicture(dec_inst, &dec_picture, 0);
    if(ret == VC1DEC_PIC_RDY) {
      if(!use_peek_output) {
        DEBUG_PRINT(("VC1DecNextPicture returned: "));
        vc1DecPrintReturnValue(ret);

        /* Increment display number for every displayed picture */
        pic_display_number++;

        DEBUG_PRINT(("OUTPUT PICTURE ID \t%d; SIZE %dx%d; ERR MBs %d; %s, ",
                     dec_picture.pic_id, dec_picture.coded_width,
                     dec_picture.coded_height, dec_picture.number_of_err_mbs,
                     dec_picture.key_picture ? "(KEYFRAME)" : ""));

        if (dec_picture.interlaced && dec_picture.field_picture)
          DEBUG_PRINT(("Interlaced field %s, ", dec_picture.top_field ? "(Top)" : "(Bottom)"));
        else if (dec_picture.interlaced && !dec_picture.field_picture)
          DEBUG_PRINT(("Interlaced frame, "));
        else
          DEBUG_PRINT(("Progressive, "));

        /* pic coding type */
        printVc1PicCodingType(dec_picture.pic_coding_type);

        DEBUG_PRINT(("\n\n"));

        if (coded_pic_width == 0) {
          coded_pic_width = dec_picture.coded_width;
          coded_pic_height = dec_picture.coded_height;
        }

        /* Write output picture to file */
        image_data = (u8 *) dec_picture.output_picture;

        if (crop_display &&
            ( dec_picture.frame_width > dec_picture.coded_width ||
              dec_picture.frame_height > dec_picture.coded_height ) ) {
          pic_size = dec_picture.coded_width *
                     dec_picture.coded_height*3/2;
          tmp = CropPicture(tmp_image, image_data,
                            dec_picture.frame_width, dec_picture.frame_height,
                            dec_picture.coded_width, dec_picture.coded_height );
          if (tmp)
            return NULL;
          WriteOutput(out_file_name, out_file_name_tiled, tmp_image,
                      pic_display_number - 1, dec_picture.coded_width,
                      dec_picture.coded_height,
                      info.interlaced_sequence,
                      dec_picture.top_field, dec_picture.first_field,
                      dec_picture.output_format);
        } else {
          pic_size = dec_picture.frame_width *
                     dec_picture.frame_height*3/2;

          if( (enable_frame_picture
               && ( dec_picture.coded_width !=
                    meta_data.max_coded_width ||
                    dec_picture.coded_height !=
                    meta_data.max_coded_height ) ) ) {
            FramePicture( image_data,
                          dec_picture.coded_width,
                          dec_picture.coded_height,
                          dec_picture.frame_width,
                          dec_picture.frame_height,
                          tmp_image,
                          info.max_coded_width,
                          info.max_coded_height );
            WriteOutput(out_file_name, out_file_name_tiled,
                        tmp_image, pic_display_number - 1,
                        info.max_coded_width,
                        info.max_coded_height,
                        info.interlaced_sequence,
                        dec_picture.top_field,
                        dec_picture.first_field,
                        dec_picture.output_format);
          } else {
            WriteOutput(out_file_name, out_file_name_tiled,
                        image_data, pic_display_number - 1,
                        ( ( dec_picture.coded_width + 15 ) & ~15 ),
                        ( ( dec_picture.coded_height + 15 ) & ~15 ),
                        info.interlaced_sequence,
                        dec_picture.top_field,
                        dec_picture.first_field,
                        dec_picture.output_format);
          }

        }

      }
      if((!dec_picture.first_field && dec_picture.interlaced) ||
         (!dec_picture.interlaced && !info.interlaced_sequence) ||
         (!dec_picture.interlaced && info.interlaced_sequence && !dec_picture.first_field))
      {
        /* Push output buffer into buf_list and wait to be consumed */
        buf_list[list_push_index] = dec_picture;
        buf_status[list_push_index] = 1;
        list_push_index++;
        if(list_push_index == 100)
          list_push_index = 0;

        sem_post(&buf_release_sem);

      }

      pic_display_number++;
    }

    else if(ret == VC1DEC_END_OF_STREAM) {
      last_pic_flag = 1;
      break;
    }
  }
  return NULL;
}



#endif

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
  u32 strm_len = 0;
  u32 pic_size = 0;
  u32 max_pic_size;
  VC1DecRet ret;
  VC1DecRet tmpret;
  VC1DecInput dec_input;
  VC1DecOutput dec_output;
  VC1DecPicture dec_picture;
  struct DWLLinearMem stream_mem;
  DWLHwConfig hw_config;
  u8* tmp_strm = 0;
  u32 pic_id = 0;
  u32 new_headers = 0;

  u32 pic_decode_number = 0;
  u32 pic_display_number = 0;
  u32 num_errors = 0;
  u32 disable_output_reordering = 0;
  int ra;

  FILE *f_tbcfg;
  struct DecDownscaleCfg dscale_cfg;

  u32 seed_rnd = 0;
  u32 stream_bit_swap = 0;
  u32 advanced = 0;
  u32 skipped_bytes = 0;
  off64_t stream_pos = 0; /* For advanced profile long streams */
#ifdef USE_EXTERNAL_BUFFER
  u32 prev_width = 0, prev_height = 0;
  u32 min_buffer_num = 0;
  VC1DecBufferInfo hbuf;
  VC1DecRet rv;
  memset(ext_buffers, 0, sizeof(ext_buffers));
  pthread_mutex_init(&ext_buffer_contro, NULL);
  struct DWLInitParam dwl_init;
#endif

#ifdef PP_PIPELINE_ENABLED
  PPApiVersion pp_ver;
  PPBuild pp_build;
#endif

#ifdef ASIC_TRACE_SUPPORT
  g_hw_ver = 8190; /* default to 8190 mode */
#endif

#ifdef VC1_EVALUATION_8170
  g_hw_ver = 8170;
#elif VC1_EVALUATION_8190
  g_hw_ver = 8190;
#elif VC1_EVALUATION_9170
  g_hw_ver = 9170;
#elif VC1_EVALUATION_9190
  g_hw_ver = 9190;
#elif VC1_EVALUATION_G1
  g_hw_ver = 10000;
#endif
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  char out_file_name[256] = "";
  char out_file_name_tiled[256] = "out_tiled.yuv";

  u32 coded_pic_width = 0;
  u32 coded_pic_height = 0;
  u32 crop_display = 0;
  u8 *tmp_image = NULL;
  VC1DecInfo info;
  VC1DecMetaData meta_data;
#endif
  u32 long_stream = 0;
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

  INIT_SW_PERFORMANCE;

  {
    VC1DecApiVersion dec_api;
    VC1DecBuild dec_build;

    /* Print API version number */
    dec_api = VC1DecGetAPIVersion();
    dec_build = VC1DecGetBuild();
    DWLReadAsicConfig(&hw_config,DWL_CLIENT_TYPE_VC1_DEC);
    DEBUG_PRINT((
                  "\n8170 VC-1 Decoder API v%d.%d - SW build: %d - HW build: %x\n",
                  dec_api.major, dec_api.minor, dec_build.sw_build, dec_build.hw_build));
    DEBUG_PRINT((
                  "HW Supports video decoding up to %d pixels,\n",
                  hw_config.max_dec_pic_width));

    if(hw_config.pp_support)
      DEBUG_PRINT((
                    "Maximum Post-processor output size %d pixels\n\n",
                    hw_config.max_pp_out_pic_width));
    else
      DEBUG_PRINT(("Post-Processor not supported\n\n"));
  }

#ifndef PP_PIPELINE_ENABLED
  /* Check that enough command line arguments given, if not -> print usage
  * information out */
  if(argc < 2) {
    DEBUG_PRINT(("Usage: %s [options] file.rcv\n", argv[0]));
    DEBUG_PRINT(("\t-Nn forces decoding to stop after n pictures\n"));
    DEBUG_PRINT(("\t-Ooutfile write output to \"outfile\" (default out_wxxxhyyy.yuv)\n"));
    DEBUG_PRINT(("\t-X Disable output file writing\n"));
    DEBUG_PRINT(("\t-C display cropped image (default decoded image)\n"));
    DEBUG_PRINT(("\t-Sfile.hex stream control trace file\n"));
    DEBUG_PRINT(("\t-L enable support for long streams.\n"));
    DEBUG_PRINT(("\t-P write planar output.\n"));
    DEBUG_PRINT(("\t-Bn to use n frame buffers in decoder\n"));
    DEBUG_PRINT(("\t-I save index file\n"));
    DEBUG_PRINT(("\t-E use tiled reference frame format.\n"));
    DEBUG_PRINT(("\t-G convert tiled output pictures to raster scan\n"));
    DEBUG_PRINT(("\t-Y Write output as Interlaced Fields (instead of Frames).\n"));
    DEBUG_PRINT(("\t-F Enable frame picture writing in multiresolutin output.\n"));
    DEBUG_PRINT(("\t-Q Skip decoding non-reference pictures.\n"));
    DEBUG_PRINT(("\t-Z output pictures using VC1DecPeek() function\n"));
    DEBUG_PRINT(("\t--separate-fields-in-dpb DPB stores interlaced content"\
                 " as fields (default: frames)\n"));
    DEBUG_PRINT(("\t--output-frame-dpb Convert output to frame mode even if"\
                 " field DPB mode used\n"));
#ifdef USE_EXTERNAL_BUFFER
    DEBUG_PRINT(("\t-A add extra external buffer randomly\n"));
#ifdef USE_OUTPUT_RELEASE
    DEBUG_PRINT(("\t-a allocate extra external buffer in output thread\n"));
#endif
#endif
    return 0;
  }
  /* set stream pointers to null */
  stream_mem.virtual_address = NULL;
  stream_mem.bus_address = 0;

  /* read command line arguments */
  for(i = 1; i < (u32) (argc - 1); i++) {
    if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_pics = (u32) atoi(argv[i] + 2);
    } else if(strncmp(argv[i], "-O", 2) == 0) {
      strcpy(out_file_name, argv[i] + 2);
    } else if(strcmp(argv[i], "-X") == 0) {
      disable_output_writing = 1;
    } else if(strcmp(argv[i], "-C") == 0) {
      crop_display = 1;
    } else if(strncmp(argv[i], "-S", 2) == 0) {
      f_stream_trace = fopen(argv[argc - 2], "r");
    } else if(strcmp(argv[i], "-L") == 0) {
      long_stream = 1;
    } else if(strcmp(argv[i], "-P") == 0) {
      planar_output = 1;
      enable_frame_picture = 1;
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if (strncmp(argv[i], "-G", 2) == 0)
      convert_tiled_output = 1;
    else if(strcmp(argv[i], "-Y") == 0) {
      interlaced_field = 1;
    } else if(strcmp(argv[i], "-F") == 0) {
      enable_frame_picture = 1;
    } else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
    } else if(strcmp(argv[i], "-Z") == 0) {
      use_peek_output = 1;
    } else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
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
      DEBUG_PRINT(("UNKNOWN PARAMETER: %s\n", argv[i]));
      return 1;
    }

  }

  /* open input file for reading, file name given by user. If file open
  * fails -> exit */
  finput = fopen(argv[argc - 1], "rb");
  if(finput == NULL) {
    DEBUG_PRINT(("UNABLE TO OPEN INPUT FILE: %s\n", argv[argc - 1]));
    return -1;
  }
#else
  /* Print API and build version numbers */
  pp_ver = PPGetAPIVersion ();
  pp_build = PPGetBuild ();

  /* set stream pointers to null */
  stream_mem.virtual_address = NULL;
  stream_mem.bus_address = 0;
  stream_mem.mem_type = DWL_MEM_TYPE_CPU;

  /* Version */
  DEBUG_PRINT( (
                 "\nX170 PP API v%d.%d - SW build: %d - HW build: %x\n",
                 pp_ver.major, pp_ver.minor, pp_build.sw_build, pp_build.hw_build));

  /* Check that enough command line arguments given, if not -> print usage
   * information out */
  if(argc < 3) {
    DEBUG_PRINT(("Usage: %s [-Nn] [-X] [-L] file.rcv pp.cfg\n", argv[0]));
    DEBUG_PRINT(("\t-Nn forces decoding to stop after n pictures\n"));
    DEBUG_PRINT(("\t-X disable output file writing\n"));
    DEBUG_PRINT(("\t-Bn to use n frame buffers in decoder\n"));
    DEBUG_PRINT(("\t-L enable support for long streams.\n"));
    DEBUG_PRINT(("\t-I save index file\n"));
    DEBUG_PRINT(("\t-E use tiled reference frame format.\n"));
    DEBUG_PRINT(("\t-Q Skip decoding non-reference pictures.\n"));
    DEBUG_PRINT(("\t--separate-fields-in-dpb DPB stores interlaced content"\
                 " as fields (default: frames)\n"));
#ifdef USE_EXTERNAL_BUFFER
    DEBUG_PRINT(("\t-A add extra external buffer randomly\n"));
#ifdef USE_OUTPUT_RELEASE
    DEBUG_PRINT(("\t-a allocate extra external buffer in output thread\n"));
#endif
#endif
    return 0;
  }

  remove("pp_out.yuv");

  /* read command line arguments */
  for(i = 1; i < (u32) (argc - 2); i++) {
    if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_pics = (u32) atoi(argv[i] + 2);
    } else if(strcmp(argv[i], "-X") == 0) {
      disable_output_writing = 1;
    } else if(strcmp(argv[i], "-L") == 0) {
      long_stream = 1;
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if(strncmp(argv[i], "-B", 2) == 0) {
      num_frame_buffers = atoi(argv[i] + 2);
      if(num_frame_buffers > 16)
        num_frame_buffers = 16;
    } else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    } else if(strcmp(argv[i], "-I") == 0) {
      save_index = 1;
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
      DEBUG_PRINT(("UNKNOWN PARAMETER: %s\n", argv[i]));
      return -1;
    }
  }

  /* open data file */
  finput = fopen(argv[argc - 2], "rb");
  if(finput == NULL) {
    DEBUG_PRINT(("UNABLE TO OPEN INPUT FILE: %s\n", argv[argc - 2]));
    return -1;
  }
#endif
#ifdef ASIC_TRACE_SUPPORT
  tmp = openTraceFiles();
  if (!tmp) {
    DEBUG_PRINT(("UNABLE TO OPEN TRACE FILE(S)\n"));
  }
#endif
  if(save_index) {
    f_index = fopen("stream.cfg", "w");
    if(f_index == NULL) {
      DEBUG_PRINT(("UNABLE TO OPEN INDEX FILE: \"stream.cfg\"\n"));
      return -1;
    }
  } else {
    f_index = fopen("stream.cfg", "r");
    if(f_index != NULL) {
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
    if (TBParseConfig("tb.cfg", TBReadParam, &tb_cfg) == TB_FALSE)
      return -1;
    if (TBCheckCfg(&tb_cfg) != 0)
      return -1;
  }
  /*TBPrintCfg(&tb_cfg);*/

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
      printf("Decoder Output Picture Endian forced to %d\n", output_picture_endian);
  #endif*/

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
    -> do not wait the picture to finalize before starting stream corruption */
  if (stream_header_corrupt)
    pic_rdy = 1;
  stream_truncate = TBGetTBStreamTruncate(&tb_cfg);
  slice_mode = TBGetTBPacketByPacket(&tb_cfg);
  slice_ud_in_packet = TBGetTBSliceUdInPacket(&tb_cfg);
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
  DEBUG_PRINT(("TB Packet By Packet %d\n", slice_mode));
  DEBUG_PRINT(("TB Slice User Data In Packet %d\n", slice_ud_in_packet));
  DEBUG_PRINT(("TB Stream Bit Swap %d; odds %s\n",
               stream_bit_swap, tb_cfg.tb_params.stream_bit_swap));
  DEBUG_PRINT(("TB Stream Packet Loss %d; odds %s\n",
               stream_packet_loss, tb_cfg.tb_params.stream_packet_loss));

  tmp_strm = (u8*) malloc(100);
  if (NULL == tmp_strm) {
    DEBUG_PRINT(("MALLOC FAILED\n"));
    return -1;
  }

  /* read metadata (max size (5+4)*4 bytes,
   * DecodeRCV checks that size is at
   * least RCV_METADATA_MAX_SIZE bytes) */
  ra = fread(tmp_strm, sizeof(u8), RCV_METADATA_MAX_SIZE, finput);
  rewind(finput);

  /* Advanced profile if startcode prefix found */
  if ( (tmp_strm[0] == 0x00) && (tmp_strm[1] == 0x00) && (tmp_strm[2] == 0x01) )
    advanced = 1;

  /* reset metadata structure */
  DWLmemset(&meta_data, 0, sizeof(meta_data));

  if (!advanced) {
    /* decode row coded video header (coded metadata). DecodeRCV function reads
    * image dimensions from struct A, struct C information is parsed by
    * VC1DecUnpackMetaData function */
    tmp = DecodeRCV(tmp_strm, RCV_METADATA_MAX_SIZE, &meta_data);
    if (tmp != 0) {
      DEBUG_PRINT(("DECODING RCV FAILED\n"));
      free(tmp_strm);
      return -1;
    }
  } else {
    meta_data.profile = 8;
  }

  TBInitializeRandom(seed_rnd);

  if (!advanced && stream_header_corrupt && stream_bit_swap) {
    DEBUG_PRINT(("meta_data[0] %x\n", *(tmp_strm+8)));
    DEBUG_PRINT(("meta_data[1] %x\n", *(tmp_strm+9)));
    DEBUG_PRINT(("meta_data[2] %x\n", *(tmp_strm+10)));
    DEBUG_PRINT(("meta_data[3] %x\n", *(tmp_strm+11)));
    tmp = TBRandomizeBitSwapInStream(tmp_strm+8, 4, tb_cfg.tb_params.stream_bit_swap);
    if (tmp != 0) {
      DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
      free(tmp_strm);
      return -1;
    }
    DEBUG_PRINT(("Randomized meta_data[0] %x\n", *(tmp_strm+8)));
    DEBUG_PRINT(("Randomized meta_data[1] %x\n", *(tmp_strm+9)));
    DEBUG_PRINT(("Randomized meta_data[2] %x\n", *(tmp_strm+10)));
    DEBUG_PRINT(("Randomized meta_data[3] %x\n", *(tmp_strm+11)));
  }

  if (!advanced) {
    decsw_performance();
    START_SW_PERFORMANCE
    tmp = VC1DecUnpackMetaData(tmp_strm+8, 4, &meta_data);
    END_SW_PERFORMANCE
    decsw_performance();
    if (tmp != VC1DEC_OK) {
      DEBUG_PRINT(("UNPACKING META DATA FAILED\n"));
      free(tmp_strm);
      return -1;
    }
    DEBUG_PRINT(("meta_data.vs_transform %d\n", meta_data.vs_transform));
    DEBUG_PRINT(("meta_data.overlap %d\n", meta_data.overlap));
    DEBUG_PRINT(("meta_data.sync_marker %d\n", meta_data.sync_marker));
    DEBUG_PRINT(("meta_data.quantizer %d\n", meta_data.quantizer));
    DEBUG_PRINT(("meta_data.frame_interp %d\n", meta_data.frame_interp));
    DEBUG_PRINT(("meta_data.max_bframes %d\n", meta_data.max_bframes));
    DEBUG_PRINT(("meta_data.fast_uv_mc %d\n", meta_data.fast_uv_mc));
    DEBUG_PRINT(("meta_data.extended_mv %d\n", meta_data.extended_mv));
    DEBUG_PRINT(("meta_data.multi_res %d\n", meta_data.multi_res));
    DEBUG_PRINT(("meta_data.range_red %d\n", meta_data.range_red));
    DEBUG_PRINT(("meta_data.dquant %d\n", meta_data.dquant));
    DEBUG_PRINT(("meta_data.loop_filter %d\n", meta_data.loop_filter));
    DEBUG_PRINT(("meta_data.profile %d\n", meta_data.profile));

    hdrs_rdy = 1;
  }
  if (!advanced && stream_header_corrupt) {
    u32 rnd_value;
    /* randomize picture width */
    DEBUG_PRINT(("meta_data.max_coded_width %d\n", meta_data.max_coded_width));
    rnd_value = 1920 + 48;
    tmp = TBRandomizeU32(&rnd_value);
    if (tmp != 0) {
      DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
      free(tmp_strm);
      return -1;
    }
    meta_data.max_coded_width = rnd_value & (~0x15);
    DEBUG_PRINT(("Randomized meta_data.max_coded_width %d\n", meta_data.max_coded_width));

    /* randomize picture height */
    DEBUG_PRINT(("meta_data.max_coded_height %d\n", meta_data.max_coded_height));
    rnd_value = 1920 + 48;
    tmp = TBRandomizeU32(&rnd_value);
    if (tmp != 0) {
      DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
      free(tmp_strm);
      return -1;
    }
    meta_data.max_coded_height = rnd_value & (~0x15);
    DEBUG_PRINT(("Randomized meta_data.max_coded_height %d\n", meta_data.max_coded_height));
  }

#ifdef USE_EXTERNAL_BUFFER
  dwl_init.client_type = DWL_CLIENT_TYPE_VC1_DEC;

  dwl_inst = DWLInit(&dwl_init);

  if(dwl_inst == NULL) {
    fprintf(stdout, ("ERROR: DWL Init failed"));
    goto end;
  }
#endif
  dscale_cfg.down_scale_x = ds_ratio_x;
  dscale_cfg.down_scale_y = ds_ratio_y;

  /* initialize decoder. If unsuccessful -> exit */
  decsw_performance();
  START_SW_PERFORMANCE;
  {
    enum DecDpbFlags flags = 0;
    if( tiled_output )   flags |= DEC_REF_FRM_TILED_DEFAULT;
    if( dpb_mode == DEC_DPB_INTERLACED_FIELD )
      flags |= DEC_DPB_ALLOW_FIELD_ORDERING;
    ret = VC1DecInit(&dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                     dwl_inst,
#endif
                     &meta_data,
                     TBGetDecErrorConcealment( &tb_cfg ),
                     num_frame_buffers,
                     flags, 0, 0, &dscale_cfg);
  }
  END_SW_PERFORMANCE;
  decsw_performance();

  if (ret != VC1DEC_OK) {
    DEBUG_PRINT(("DECODER INITIALIZATION FAILED\n"));
    goto end;
  }

  /* Set ref buffer test mode */
  ((decContainer_t *) dec_inst)->ref_buffer_ctrl.test_function = TBRefbuTestMode;
  TBSetRefbuMemModel( &tb_cfg,
                      ((decContainer_t *) dec_inst)->vc1_regs,
                      &((decContainer_t *) dec_inst)->ref_buffer_ctrl );

  dec_input.skip_non_reference = skip_non_reference;

#ifdef PP_PIPELINE_ENABLED
  /* Initialize the post processor. If unsuccessful -> exit */

  if(pp_startup
      (argv[argc - 1], dec_inst, PP_PIPELINED_DEC_TYPE_VC1, &tb_cfg) != 0) {
    fprintf(stdout, "PP INITIALIZATION FAILED\n");
    goto end;
  }

  if(!advanced) {
    if (pp_update_config
        (dec_inst, PP_PIPELINED_DEC_TYPE_VC1, &tb_cfg) == CFG_UPDATE_FAIL) {
      fprintf(stdout, "PP CONFIG LOAD FAILED\n");
      goto end;
    }

    /* get info */
    decsw_performance();
    VC1DecGetInfo(dec_inst, &info);
    decsw_performance();

    dpb_mode = info.dpb_mode;

    /* If unspecified at cmd line, use minimum # of buffers, otherwise
     * use specified amount. */
    if(num_frame_buffers == 0)
      pp_number_of_buffers(info.multi_buff_pp_size);
    else
      pp_number_of_buffers(num_frame_buffers);
  }
#endif

  if (!long_stream) {
    /* check size of the input file -> length of the stream in bytes */
    fseek(finput, 0L, SEEK_END);
    strm_len = (u32) ftell(finput);
    rewind(finput);

    /* sets the stream length to random value*/
    if (stream_truncate && !slice_mode) {
      DEBUG_PRINT(("strm_len %d\n", strm_len));
      tmp = TBRandomizeU32(&strm_len);
      if (tmp != 0) {
        DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
        goto end;
      }
      DEBUG_PRINT(("Randomized strm_len %d\n", strm_len));
    }

    /* allocate memory for stream buffer. if unsuccessful -> exit */


    if(DWLMallocLinear(((decContainer_t*)dec_inst)->dwl, strm_len, &stream_mem)
        != DWL_OK ) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }

    byte_strm_start = (u8 *) stream_mem.virtual_address;

    if(byte_strm_start == NULL) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }

    /* read input stream from file to buffer and close input file */
    ra = fread(byte_strm_start, sizeof(u8), strm_len, finput);
    fclose(finput);

    /* initialize VC1DecDecode() input structure */
    stream_stop = byte_strm_start + strm_len;

    if (!advanced) {
      /* size of Sequence layer data structure */
      dec_input.stream = byte_strm_start + ( 4 + 4 * rcv_v2 ) *4 + rcv_metadata_size;
      dec_input.stream_size = DecodeFrameLayerData((u8*)dec_input.stream);
      dec_input.stream += 4 + 4 * rcv_v2; /* size of Frame layer data structure */
      dec_input.stream_bus_address = stream_mem.bus_address +
                                     (dec_input.stream - byte_strm_start);
    } else {
      dec_input.stream = byte_strm_start;
      dec_input.stream_size = strm_len;
      dec_input.stream_bus_address = stream_mem.bus_address;
    }
  }
  /* LONG STREAM */
  else {
    if(DWLMallocLinear(((decContainer_t *) dec_inst)->dwl,
                       VC1_MAX_STREAM_SIZE,
                       &stream_mem) != DWL_OK ) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
    byte_strm_start = (u8 *) stream_mem.virtual_address;
    dec_input.stream =  (u8 *) stream_mem.virtual_address;
    dec_input.stream_bus_address = stream_mem.bus_address;

    if (!advanced) {
      /* Read meta data and frame layer data */
      ra = fread(tmp_strm, sizeof(u8), ( 4 + 4 * rcv_v2 ) *4 + 4 + 4 * rcv_v2 + rcv_metadata_size, finput);
      if (ferror(finput)) {
        DEBUG_PRINT(("STREAM READ ERROR\n"));
        goto end;
      }
      if (feof(finput)) {
        DEBUG_PRINT(("END OF STREAM\n"));
        goto end;
      }

      dec_input.stream_size = DecodeFrameLayerData(tmp_strm + ( 4 + 4 * rcv_v2 ) *4 + rcv_metadata_size);
      ra = fread( (u8*)dec_input.stream, sizeof(u8), dec_input.stream_size, finput );
    } else {
      if(use_index) {
        dec_input.stream_size = fillBuffer((u8*)dec_input.stream);
        strm_len = dec_input.stream_size;
      } else {
        dec_input.stream_size =
          fread((u8*)dec_input.stream, sizeof(u8), VC1_MAX_STREAM_SIZE, finput);
        strm_len = dec_input.stream_size;
      }
    }

    if (ferror(finput)) {
      DEBUG_PRINT(("STREAM READ ERROR\n"));
      goto end;
    }
    if (feof(finput)) {
      DEBUG_PRINT(("STREAM WILL END\n"));
      /*goto end;*/
    }
  }

  if (!advanced) {
    /* If -O option not used, generate default file name */
    if (out_file_name[0] == 0)
      sprintf(out_file_name, "out_w%dh%d.yuv",
              meta_data.max_coded_width, meta_data.max_coded_height);

    /* max picture size */
    max_pic_size = (
                     ( ( meta_data.max_coded_width + 15 ) & ~15 ) *
                     ( ( meta_data.max_coded_height + 15 ) & ~15 ) ) * 3 / 2;
  }

  if( (crop_display || meta_data.multi_res) && !advanced ) {
    tmp_image = (u8*)malloc(max_pic_size * sizeof(u8) );
  }

  {
    SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
                   HWIF_DEC_LATENCY, latency_comp);
    SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
                   HWIF_DEC_CLK_GATE_E, clock_gating);
    /*
    SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
        HWIF_DEC_OUT_TILED_E, output_format);
        */
    SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
                   HWIF_DEC_OUT_ENDIAN, output_picture_endian);
    SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
                   HWIF_DEC_MAX_BURST, bus_burst_length);
    if ((DWLReadAsicID(DWL_CLIENT_TYPE_VC1_DEC) >> 16) == 0x8170U) {
      SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
                     HWIF_PRIORITY_MODE, asic_service_priority);
    }
    SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
                   HWIF_DEC_DATA_DISC_E, data_discard);
    SetDecRegister(((decContainer_t *) dec_inst)->vc1_regs,
                   HWIF_SERV_MERGE_DIS, service_merge_disable);
  }

  /* main decoding loop */
  do {
    dec_input.pic_id = pic_id;
    if (ret != VC1DEC_NO_DECODING_BUFFER)
    DEBUG_PRINT(("Starting to decode picture ID %d\n", pic_id));

    /*printf("dec_input.stream_size %d\n", dec_input.stream_size);*/

    if (stream_truncate && pic_rdy && (hdrs_rdy || stream_header_corrupt) && (long_stream || (!long_stream && slice_mode))) {
      i32 ret;
      ret = TBRandomizeU32(&dec_input.stream_size);
      if(ret != 0) {
        DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
        return 0;
      }
      DEBUG_PRINT(("Randomized stream size %d\n", dec_input.stream_size));
    }

    /* If enabled, break the stream*/
    if (stream_bit_swap) {
      if((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt) {
        /*if (pic_rdy && ((pic_decode_number%2 && corrupted_bytes <= 0) == 0))*/
        if (pic_rdy && corrupted_bytes <= 0) {
          tmp = TBRandomizeBitSwapInStream((u8 *)dec_input.stream,
                                           dec_input.stream_size,
                                           tb_cfg.tb_params.stream_bit_swap);
          if (tmp != 0) {
            DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
            goto end;
          }

          corrupted_bytes = dec_input.stream_size;
          printf("corrupted_bytes %d\n", corrupted_bytes);
        }
      }
    }
    /* call API function to perform decoding */
    /* if stream size bigger than decoder can handle, skip the frame */
    /* checking error response for oversized pic done in API testing */
    if(dec_input.stream_size <= VC1_MAX_STREAM_SIZE) {

      decsw_performance();
      START_SW_PERFORMANCE;
      ret = VC1DecDecode(dec_inst, &dec_input, &dec_output);
      END_SW_PERFORMANCE;
      /*DEBUG_PRINT(("dec_output.data_left %d\n", dec_output.data_left));*/
      decsw_performance();
      DEBUG_PRINT(("VC1DecDecode returned: "));
      vc1DecPrintReturnValue(ret);

#if 1
      /* there is some data left */
      /* if simple or main, test bench does not care of dec_output.data_left but rather skips to next picture */
      /* if advanced but long stream mode, stream is read again from file */
      /* if slice mode, test bench does not care of dec_output.data_left but also skips to next slice */
      if(dec_output.data_left && advanced && !long_stream && !slice_mode) {
        printf("dec_output.data_left %d\n", dec_output.data_left);
        corrupted_bytes -= (dec_input.stream_size - dec_output.data_left);
      } else {
        corrupted_bytes = 0;
      }
#endif
    } else {
      ret = VC1DEC_STRM_PROCESSED;
      DEBUG_PRINT(("Oversized stream for picture, ignoring... \n"));
      break;
    }
    switch(ret) {
    case VC1DEC_RESOLUTION_CHANGED:
      TBSetRefbuMemModel( &tb_cfg,
                          ((decContainer_t *) dec_inst)->vc1_regs,
                          &((decContainer_t *) dec_inst)->ref_buffer_ctrl );
      /* get info */
      decsw_performance();
      VC1DecGetInfo(dec_inst, &info);
      decsw_performance();
      DEBUG_PRINT(("RESOLUTION CHANGED\n"));
      DEBUG_PRINT(("New resolution is %dx%d\n",
                   info.coded_width, info.coded_height));

#ifdef PP_PIPELINE_ENABLED
      /* Flush picture buffer before configuring new image size to PP */
      decsw_performance();
      while( VC1DecNextPicture(dec_inst, &dec_picture, 1 ) == VC1DEC_PIC_RDY ) {
        decsw_performance();

        /* Increment display number for every displayed picture */
        pic_display_number++;

        /* write PP output */
        number_of_written_frames++;
        if (!disable_output_writing) {
          pp_write_output( pic_decode_number - 1,
                           field_output,
                           dec_picture.top_field );
        }
        decsw_performance();
      }
      decsw_performance();

      if (pp_change_resolution( ((info.coded_width+7) & ~7),
                                ((info.coded_height+7) & ~7),
                                &tb_cfg) ) {
        DEBUG_PRINT(("PP CONFIG FAILED!!!\n"));
        goto end;
      }
#endif
      break;

    case VC1DEC_HDRS_RDY:
      /* Set a flag to indicate that headers are ready */
#ifdef USE_EXTERNAL_BUFFER
      rv = VC1DecGetBufferInfo(dec_inst, &hbuf);
      printf("VC1DecGetBufferInfo ret %d\n", rv);
      printf("buf_to_free %p, next_buf_size %d, buf_num %d\n",
             (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num);
#endif
      hdrs_rdy = 1;
      new_headers = 1;
      TBSetRefbuMemModel( &tb_cfg,
                          ((decContainer_t *) dec_inst)->vc1_regs,
                          &((decContainer_t *) dec_inst)->ref_buffer_ctrl );

      if( !long_stream ) {
        tmp = (dec_output.p_stream_curr_pos - dec_input.stream);
        stream_pos += tmp;
        dec_input.stream = dec_output.p_stream_curr_pos;
        dec_input.stream_bus_address += tmp;
        if (slice_mode) {
          dec_input.stream_size =
            GetNextDuSize(dec_output.p_stream_curr_pos,
                          byte_strm_start, strm_len, &skipped_bytes);
          dec_input.stream += skipped_bytes;
          dec_input.stream_bus_address += skipped_bytes;
          stream_pos += skipped_bytes;
        } else
          dec_input.stream_size = dec_output.data_left;
      } else { /* LONG STREAM */
        if(use_index) {
          if(dec_output.data_left != 0) {
            dec_input.stream_bus_address += (dec_output.p_stream_curr_pos - dec_input.stream);
            dec_input.stream_size = dec_output.data_left;
            dec_input.stream = dec_output.p_stream_curr_pos;
          } else {
            dec_input.stream_bus_address = stream_mem.bus_address;
            dec_input.stream =  (u8 *) stream_mem.virtual_address;
            dec_input.stream_size = fillBuffer((u8*)dec_input.stream);
          }
        } else {
          tmp = (dec_output.p_stream_curr_pos - dec_input.stream);
          stream_pos += tmp;
          fseeko64( finput, stream_pos, SEEK_SET );
          dec_input.stream_size =
            fread((u8*)dec_input.stream, sizeof(u8), VC1_MAX_STREAM_SIZE, finput);

          if (slice_mode) {
            dec_input.stream_size =
              GetNextDuSize((u8*)dec_input.stream,
                            dec_input.stream, dec_input.stream_size, &skipped_bytes);
            stream_pos += skipped_bytes;
            fseeko64( finput, stream_pos, SEEK_SET );
            if(save_index) {
              if(dec_input.stream[0] == 0 &&
                  dec_input.stream[1] == 0 &&
                  dec_input.stream[2] == 1) {
#ifdef USE_64BIT_ENV
                fprintf(f_index, "%lu\n", stream_pos);
#else
                fprintf(f_index, "%llu\n", stream_pos);
#endif
              }
            }
            dec_input.stream_size =
              fread((u8*)dec_input.stream, sizeof(u8),
                    dec_input.stream_size, finput);
          }
        }
        if (ferror(finput)) {
          DEBUG_PRINT(("STREAM READ ERROR\n"));
          goto end;
        }
        if (feof(finput)) {
          DEBUG_PRINT(("STREAM WILL END\n"));
          /*goto end;*/
        }
      }

      /* get info */
      decsw_performance();
      VC1DecGetInfo(dec_inst, &info);
      decsw_performance();
#ifdef USE_EXTERNAL_BUFFER
      if(info.buf_release_flag) {
        /* Reset buffers added and stop adding extra buffers when a new header comes. */
        if (pp_enabled)
          res_changed = 1;
        else {
          add_extra_flag = 0;
          ReleaseExtBuffers();
          num_buffers = 0;
        }
      }
      prev_width = info.max_coded_width;
      prev_height = info.max_coded_height;
#endif
      dpb_mode = info.dpb_mode;

      if(new_headers) {
        if (info.interlaced_sequence) {
          DEBUG_PRINT(("Interlaced sequence\n"));
          field_output = 1;
        } else
          DEBUG_PRINT(("Progressive sequence\n"));

        DEBUG_PRINT(("Max size %dx%d\n", info.max_coded_width, info.max_coded_height));
        DEBUG_PRINT(("Coded size %dx%d\n", info.coded_width, info.coded_height));
        DEBUG_PRINT(("Output format %s\n",
                     info.output_format == VC1DEC_SEMIPLANAR_YUV420
                     ? "VC1DEC_SEMIPLANAR_YUV420" :
                     "VC1DEC_TILED_YUV420"));
      }
      /* max picture size */
      max_pic_size = (info.max_coded_width * info.max_coded_height * 3)>>1;

      if( (crop_display || enable_frame_picture) && (tmp_image == NULL) )
        tmp_image = (u8*)malloc(max_pic_size * sizeof(u8) );

      /* If -O option not used, generate default file name */
      if (out_file_name[0] == 0) {
        if (!info.interlaced_sequence || !interlaced_field) {
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  info.max_coded_width, info.max_coded_height);
        } else {
          sprintf(out_file_name, "out_w%dh%d.yuv",
                  info.max_coded_width, info.max_coded_height>>1);
        }
      }
#ifdef PP_PIPELINE_ENABLED
      pp_set_input_interlaced(info.interlaced_sequence);
      if (pp_update_config
          (dec_inst, PP_PIPELINED_DEC_TYPE_VC1, &tb_cfg) == CFG_UPDATE_FAIL) {
        fprintf(stdout, "PP CONFIG LOAD FAILED\n");
        goto end;
      }
      /* If unspecified at cmd line, use minimum # of buffers, otherwise
       * use specified amount. */
      if (num_frame_buffers == 0)
        pp_number_of_buffers(info.multi_buff_pp_size);
      else
        pp_number_of_buffers(num_frame_buffers);

      if (pp_deinterlace_used())
        field_output = 0;
#endif
      break;

#ifdef USE_EXTERNAL_BUFFER
    case VC1DEC_WAITING_FOR_BUFFER:
      rv = VC1DecGetBufferInfo(dec_inst, &hbuf);
      DEBUG_PRINT(("VC1DecGetBufferInfo ret %d\n", rv));
      DEBUG_PRINT(("buf_to_free %p, next_buf_size %d, buf_num %d\n",
                   (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num));
      min_buffer_num = hbuf.buf_num;
      if (hbuf.buf_to_free.virtual_address != NULL && res_changed) {
        add_extra_flag = 0;
        ReleaseExtBuffers();
        num_buffers = 0;
        res_changed = 0;
      }

      if(hbuf.next_buf_size) {
        /* Only add minimum required buffers at first. */
        //extra_buffer_num = hbuf.buf_num - min_buffer_num;
        buffer_size = hbuf.next_buf_size;
        struct DWLLinearMem mem;
        mem.mem_type = DWL_MEM_TYPE_CPU;
        i32 dwl_ret;
        for(i=0; i<min_buffer_num; i++) {
          if (pp_enabled)
            dwl_ret = DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
          else
            dwl_ret = DWLMallocRefFrm(dwl_inst, hbuf.next_buf_size, &mem);

          rv = VC1DecAddBuffer(dec_inst, &mem);
          DEBUG_PRINT(("VC1DecAddBuffer ret %d\n", rv));
          if( rv != VC1DEC_OK && rv != VC1DEC_WAITING_FOR_BUFFER) {
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

    case VC1DEC_PIC_DECODED:
      /* Picture is now ready */
      pic_rdy = 1;
      new_headers = 0;
      /* get info */
      decsw_performance();
      VC1DecGetInfo(dec_inst, &info);
      decsw_performance();

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
      if (!output_thread_run) {
        output_thread_run = 1;
        sem_init(&buf_release_sem, 0, 0);
        pthread_create(&output_thread, NULL, vc1_output_thread, NULL);
        pthread_create(&release_thread, NULL, buf_release_thread, NULL);
      }
#endif

#ifdef USE_EXTERNAL_BUFFER
      if(use_extra_buffers && !add_buffer_thread_run) {
        add_buffer_thread_run = 1;
        pthread_create(&add_buffer_thread, NULL, AddBufferThread, NULL);
      }
#endif
      /* Increment decoding number for every decoded picture */
      pic_decode_number++;

      if (!long_stream && !advanced) {
        dec_input.stream += dec_input.stream_size;
        dec_input.stream_bus_address += dec_input.stream_size;
      }
#if 0
      if (pic_decode_number == 10) {
        VC1DecRet tmp_ret = VC1DecAbort(dec_inst);
        tmp_ret = VC1DecAbortAfter(dec_inst);
      }
#endif

      if (use_peek_output &&
          VC1DecPeek(dec_inst, &dec_picture) == VC1DEC_PIC_RDY) {
        pic_display_number++;
        DEBUG_PRINT(("OUTPUT PICTURE ID \t%d; SIZE %dx%d; ERR MBs %d; %s, ",
                     dec_picture.pic_id, dec_picture.coded_width,
                     dec_picture.coded_height, dec_picture.number_of_err_mbs,
                     dec_picture.key_picture ? "(KEYFRAME)" : ""));

        if (dec_picture.interlaced && dec_picture.field_picture)
          DEBUG_PRINT(("Interlaced field %s, ", dec_picture.top_field ? "(Top)" : "(Bottom)"));
        else if (dec_picture.interlaced && !dec_picture.field_picture)
          DEBUG_PRINT(("Interlaced frame, "));
        else
          DEBUG_PRINT(("Progressive, "));

        /* pic coding type */
        printVc1PicCodingType(dec_picture.pic_coding_type);

        DEBUG_PRINT(("; DECODED PIC ID %d\n\n", pic_id));
        if (coded_pic_width == 0) {
          coded_pic_width = dec_picture.coded_width;
          coded_pic_height = dec_picture.coded_height;
        }

        /* Write output picture to file */
        image_data = (u8*)dec_picture.output_picture;

        pic_size = dec_picture.frame_width*dec_picture.frame_height*3/2;

        WriteOutput(out_file_name, out_file_name_tiled, image_data,
                    pic_display_number - 1,
                    ( ( dec_picture.coded_width + 15 ) & ~15 ),
                    ( ( dec_picture.coded_height + 15 ) & ~15 ),
                    info.interlaced_sequence,
                    dec_picture.top_field, dec_picture.first_field,
                    dec_picture.output_format);
      }

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      /* loop to get all pictures/fields out from decoder */
      do {
        decsw_performance();
        START_SW_PERFORMANCE;
        tmpret = VC1DecNextPicture(dec_inst, &dec_picture, 0 );
        END_SW_PERFORMANCE;
        decsw_performance();

        if (!use_peek_output) {
          DEBUG_PRINT(("VC1DecNextPicture returned: "));
          vc1DecPrintReturnValue(tmpret);

          if ( tmpret == VC1DEC_PIC_RDY ) {
            /* Increment display number for every displayed picture */
            pic_display_number++;

            DEBUG_PRINT(("OUTPUT PICTURE ID \t%d; SIZE %dx%d; ERR MBs %d; %s, ",
                         dec_picture.pic_id, dec_picture.coded_width,
                         dec_picture.coded_height, dec_picture.number_of_err_mbs,
                         dec_picture.key_picture ? "(KEYFRAME)" : ""));

            if (dec_picture.interlaced && dec_picture.field_picture)
              DEBUG_PRINT(("Interlaced field %s, ", dec_picture.top_field ? "(Top)" : "(Bottom)"));
            else if (dec_picture.interlaced && !dec_picture.field_picture)
              DEBUG_PRINT(("Interlaced frame, "));
            else
              DEBUG_PRINT(("Progressive, "));

            /* pic coding type */
            printVc1PicCodingType(dec_picture.pic_coding_type);

            DEBUG_PRINT(("; DECODED PIC ID %d\n\n", pic_id));

            if (coded_pic_width == 0) {
              coded_pic_width = dec_picture.coded_width;
              coded_pic_height = dec_picture.coded_height;
            }

            /* Write output picture to file */
            image_data = (u8*)dec_picture.output_picture;

#ifndef PP_PIPELINE_ENABLED

            if (crop_display &&
                ( dec_picture.frame_width > dec_picture.coded_width ||
                  dec_picture.frame_height > dec_picture.coded_height ) ) {
              pic_size = dec_picture.coded_width *
                         dec_picture.coded_height*3/2;
              tmp = CropPicture(tmp_image, image_data,
                                dec_picture.frame_width, dec_picture.frame_height,
                                dec_picture.coded_width, dec_picture.coded_height );
              if (tmp)
                return -1;
              WriteOutput(out_file_name, out_file_name_tiled, tmp_image,
                          pic_display_number - 1, dec_picture.coded_width,
                          dec_picture.coded_height,
                          info.interlaced_sequence,
                          dec_picture.top_field, dec_picture.first_field,
                          dec_picture.output_format);
            } else {
              pic_size = dec_picture.frame_width *
                         dec_picture.frame_height*3/2;

              if( (enable_frame_picture
                   && ( dec_picture.coded_width !=
                        meta_data.max_coded_width ||
                        dec_picture.coded_height !=
                        meta_data.max_coded_height ) ) ) {
                FramePicture( image_data,
                              dec_picture.coded_width,
                              dec_picture.coded_height,
                              dec_picture.frame_width,
                              dec_picture.frame_height,
                              tmp_image,
                              info.max_coded_width,
                              info.max_coded_height );
                WriteOutput(out_file_name, out_file_name_tiled,
                            tmp_image, pic_display_number - 1,
                            info.max_coded_width,
                            info.max_coded_height,
                            info.interlaced_sequence,
                            dec_picture.top_field,
                            dec_picture.first_field,
                            dec_picture.output_format);
              } else {
                WriteOutput(out_file_name, out_file_name_tiled,
                            image_data, pic_display_number - 1,
                            ( ( dec_picture.coded_width + 15 ) & ~15 ),
                            ( ( dec_picture.coded_height + 15 ) & ~15 ),
                            info.interlaced_sequence,
                            dec_picture.top_field,
                            dec_picture.first_field,
                            dec_picture.output_format);
              }

            }
#else

            /* write PP output */
            number_of_written_frames++;
            if (!disable_output_writing) {
              if (pp_check_combined_status() == PP_OK) {
                pp_write_output( pic_decode_number - 1,
                                 field_output,
                                 dec_picture.top_field );
              } else {
                fprintf(stderr, "COMBINED STATUS FAILED\n");
              }

              if (pp_update_config
                  (dec_inst, PP_PIPELINED_DEC_TYPE_VC1, &tb_cfg) == CFG_UPDATE_FAIL) {
                fprintf(stdout, "PP CONFIG LOAD FAILED\n");
              }
            }
#endif
          }
        }
      } while (tmpret == VC1DEC_PIC_RDY);
#endif

      /* Update pic Id */
      pic_id++;

      /* If enough pictures decoded -> force decoding to end
      * by setting that no more stream is available */
      if ((max_num_pics && number_of_written_frames == max_num_pics) ||
          dec_input.stream >= stream_stop && !long_stream)
        dec_input.stream_size = 0;
      else {
        /* Decode frame layer metadata */
        if( !long_stream ) {
          if( !advanced ) {
            dec_input.stream_size =
              DecodeFrameLayerData((u8*)dec_input.stream);
            dec_input.stream += 4 + 4 * rcv_v2;
            if ((dec_input.stream + dec_input.stream_size) > stream_stop) {
              dec_input.stream_size = stream_stop > dec_input.stream ?
                                      stream_stop-dec_input.stream : 0;
            }
            dec_input.stream_bus_address += 4 + 4 * rcv_v2;
          } else {
            tmp = (dec_output.p_stream_curr_pos - dec_input.stream);
            stream_pos += tmp;
            dec_input.stream = dec_output.p_stream_curr_pos;
            dec_input.stream_bus_address += tmp;
            if (slice_mode) {
              dec_input.stream_size =
                GetNextDuSize(dec_output.p_stream_curr_pos,
                              byte_strm_start, strm_len, &skipped_bytes);
              dec_input.stream += skipped_bytes;
              dec_input.stream_bus_address += skipped_bytes;
              stream_pos += skipped_bytes;
            } else
              dec_input.stream_size = dec_output.data_left;
          }
        } else { /* LONG STREAM */
          if (!advanced) {
            ra = fread(tmp_strm, sizeof(u8),  4 + 4 * rcv_v2, finput);
            if (ferror(finput)) {
              DEBUG_PRINT(("STREAM READ ERROR\n"));
              goto end;
            }
            if (feof(finput)) {
              DEBUG_PRINT(("END OF STREAM\n"));
              dec_input.stream_size = 0;
              continue;
            }
            dec_input.stream_size = DecodeFrameLayerData(tmp_strm);
            ra = fread((u8*)dec_input.stream, sizeof(u8), dec_input.stream_size, finput);
          } else {
            if(use_index) {
              if(dec_output.data_left != 0) {
                dec_input.stream_bus_address += (dec_output.p_stream_curr_pos - dec_input.stream);
                dec_input.stream_size = dec_output.data_left;
                dec_input.stream = dec_output.p_stream_curr_pos;
              } else {
                dec_input.stream_bus_address = stream_mem.bus_address;
                dec_input.stream =  (u8 *) stream_mem.virtual_address;
                dec_input.stream_size = fillBuffer((u8*)dec_input.stream);
              }
            } else {
              tmp = (dec_output.p_stream_curr_pos - dec_input.stream);
              stream_pos += tmp;
              fseeko64( finput, stream_pos, SEEK_SET );

              dec_input.stream_size =
                fread((u8*)dec_input.stream, sizeof(u8), VC1_MAX_STREAM_SIZE, finput);

              if(save_index && !slice_mode) {
                if(dec_input.stream[0] == 0 &&
                    dec_input.stream[1] == 0 &&
                    dec_input.stream[2] == 1) {
#ifdef USE_64BIT_ENV
                  fprintf(f_index, "%lu\n", stream_pos);
#else
                  fprintf(f_index, "%llu\n", stream_pos);
#endif
                }
              }


              if (slice_mode) {
                dec_input.stream_size =
                  GetNextDuSize((u8*)dec_input.stream, dec_input.stream,
                                dec_input.stream_size, &skipped_bytes);
                stream_pos += skipped_bytes;
                fseeko64( finput, stream_pos, SEEK_SET );

                if(save_index) {
                  if(dec_input.stream[0] == 0 &&
                      dec_input.stream[1] == 0 &&
                      dec_input.stream[2] == 1) {
#ifdef USE_64BIT_ENV
                    fprintf(f_index, "%lu\n", stream_pos);
#else
                    fprintf(f_index, "%llu\n", stream_pos);
#endif
                  }
                }

                dec_input.stream_size =
                  fread((u8*)dec_input.stream, sizeof(u8),
                        dec_input.stream_size, finput);
              }
            }
          }
          if (ferror(finput)) {
            DEBUG_PRINT(("STREAM READ ERROR\n"));
            goto end;
          }
          if (feof(finput)) {
            DEBUG_PRINT(("STREAM WILL END\n"));
            /*goto end;*/
          }
        }
      }
      break;

    case VC1DEC_STRM_PROCESSED:
    case VC1DEC_NONREF_PIC_SKIPPED:
      /* Used to indicate that picture decoding needs to finalized prior
         to corrupting next picture
      pic_rdy = 0;*/

      /* If enough pictures decoded -> force decoding to end
      * by setting that no more stream is available */
      if ((max_num_pics && number_of_written_frames == max_num_pics) ||
          (dec_input.stream >= stream_stop ) && !long_stream)
        dec_input.stream_size = 0;
      else {
        /* Decode frame layer metadata */
        if( !long_stream ) {
          if( !advanced ) {
            dec_input.stream += dec_input.stream_size;
            dec_input.stream_bus_address += dec_input.stream_size;

            dec_input.stream_size =
              DecodeFrameLayerData((u8*)dec_input.stream);
            dec_input.stream += 4 + 4 * rcv_v2;
            if ((dec_input.stream + dec_input.stream_size) > stream_stop) {
              dec_input.stream_size = stream_stop > dec_input.stream ?
                                      stream_stop-dec_input.stream : 0;
            }
            dec_input.stream_bus_address += 4 + 4 * rcv_v2;
          } else {
            tmp = (dec_output.p_stream_curr_pos - dec_input.stream);
            stream_pos += tmp;
            dec_input.stream = dec_output.p_stream_curr_pos;
            dec_input.stream_bus_address += tmp;
            if (slice_mode) {
              dec_input.stream_size =
                GetNextDuSize(dec_output.p_stream_curr_pos,
                              byte_strm_start, strm_len, &skipped_bytes);
              dec_input.stream += skipped_bytes;
              dec_input.stream_bus_address += skipped_bytes;
              stream_pos += skipped_bytes;
            } else
              dec_input.stream_size = dec_output.data_left;
          }
        } else { /* LONG STREAM */
          if (!advanced) {
            ra = fread(tmp_strm, sizeof(u8),  4 + 4 * rcv_v2, finput);
            if (ferror(finput)) {
              DEBUG_PRINT(("STREAM READ ERROR\n"));
              goto end;
            }
            if (feof(finput)) {
              DEBUG_PRINT(("END OF STREAM\n"));
              dec_input.stream_size = 0;
              continue;
            }
            dec_input.stream_size = DecodeFrameLayerData(tmp_strm);
            ra = fread((u8*)dec_input.stream, sizeof(u8), dec_input.stream_size, finput);
          } else {
            if(use_index) {
              if(dec_output.data_left != 0) {
                dec_input.stream_bus_address += (dec_output.p_stream_curr_pos - dec_input.stream);
                dec_input.stream_size = dec_output.data_left;
                dec_input.stream = dec_output.p_stream_curr_pos;
              } else {
                dec_input.stream_bus_address = stream_mem.bus_address;
                dec_input.stream =  (u8 *) stream_mem.virtual_address;
                dec_input.stream_size = fillBuffer((u8*)dec_input.stream);
              }

            } else {
              tmp = (dec_output.p_stream_curr_pos - dec_input.stream);
              stream_pos += tmp;
              fseeko64( finput, stream_pos, SEEK_SET );

              dec_input.stream_size =
                fread((u8*)dec_input.stream, sizeof(u8),
                      VC1_MAX_STREAM_SIZE, finput);
              if (slice_mode) {
                dec_input.stream_size =
                  GetNextDuSize((u8*)dec_input.stream, dec_input.stream,
                                dec_input.stream_size, &skipped_bytes);
                stream_pos += skipped_bytes;
                fseeko64( finput, stream_pos, SEEK_SET );

                if(save_index) {
                  if(dec_input.stream[0] == 0 &&
                      dec_input.stream[1] == 0 &&
                      dec_input.stream[2] == 1) {
#ifdef USE_64BIT_ENV
                    fprintf(f_index, "%lu\n", stream_pos);
#else
                    fprintf(f_index, "%llu\n", stream_pos);
#endif
                  }
                }

                dec_input.stream_size =
                  fread((u8*)dec_input.stream, sizeof(u8),
                        dec_input.stream_size, finput);
              }
            }
          }
          if (ferror(finput)) {
            DEBUG_PRINT(("STREAM READ ERROR\n"));
            goto end;
          }
          if (feof(finput)) {
            DEBUG_PRINT(("STREAM WILL END\n"));
            /*goto end;*/
          }
        }
      }
      break;

    case VC1DEC_NO_DECODING_BUFFER:
      break;

    case VC1DEC_END_OF_SEQ:
      dec_input.stream_size = 0;
      break;

    default:
      DEBUG_PRINT(("FATAL ERROR: %d\n", ret));
      goto end;
    }
    /* keep decoding until all data from input stream buffer consumed */
  } while(dec_input.stream_size > 0);

  printf("STREAM END ENCOUNTERED\n");
  if(save_index && advanced) {
    tmp = (dec_output.p_stream_curr_pos - dec_input.stream);
    stream_pos += tmp;
#ifdef USE_64BIT_ENV
    fprintf(f_index, "%lu\n", stream_pos);
#else
    fprintf(f_index, "%llu\n", stream_pos);
#endif
  }
  if(save_index || use_index) {
    fclose(f_index);
  }

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  /* Output buffered images also... */
  decsw_performance();
  START_SW_PERFORMANCE;
  while( !use_peek_output &&
         ((max_num_pics && number_of_written_frames < max_num_pics) || !max_num_pics ) &&
         VC1DecNextPicture(dec_inst, &dec_picture, 1 ) == VC1DEC_PIC_RDY ) {
    END_SW_PERFORMANCE;
    decsw_performance();
    /* Increment display number for every displayed picture */
    pic_display_number++;

#if !defined(VC1_EVALUATION_VERSION)
    DEBUG_PRINT(("   BUFFERED %s ID %d SIZE %dx%d;",
                 dec_picture.key_picture ? "(KEYFRAME)" : "", dec_picture.pic_id,
                 dec_picture.coded_width,
                 dec_picture.coded_height));

    if (dec_picture.interlaced && dec_picture.field_picture)
      DEBUG_PRINT((" Interlaced field %s, ", dec_picture.top_field ? "(Top)" : "(Bottom)"));
    else if (dec_picture.interlaced && !dec_picture.field_picture)
      DEBUG_PRINT((" Interlaced frame, "));
    else
      DEBUG_PRINT((" Progressive, "));

    /* pic coding type */
    printVc1PicCodingType(dec_picture.pic_coding_type);
    DEBUG_PRINT(("\n"));
#endif

#ifndef PP_PIPELINE_ENABLED

    /* Write output picture to file */
    image_data = (u8*)dec_picture.output_picture;

    if (crop_display &&
        ( dec_picture.frame_width > dec_picture.coded_width ||
          dec_picture.frame_height > dec_picture.coded_height ) ) {
      pic_size = dec_picture.coded_width*dec_picture.coded_height*3/2;
      tmp = CropPicture(tmp_image, image_data,
                        dec_picture.frame_width, dec_picture.frame_height,
                        dec_picture.coded_width, dec_picture.coded_height );
      if (tmp)
        return -1;
      WriteOutput(out_file_name, out_file_name_tiled, tmp_image,
                  pic_display_number - 1, dec_picture.coded_width,
                  dec_picture.coded_height, info.interlaced_sequence,
                  dec_picture.top_field, dec_picture.first_field,
                  dec_picture.output_format);
    } else {
      pic_size = dec_picture.frame_width*dec_picture.frame_height*3/2;

      if( (enable_frame_picture
           && ( dec_picture.coded_width != meta_data.max_coded_width ||
                dec_picture.coded_height != meta_data.max_coded_height))) {
        FramePicture( image_data, dec_picture.coded_width,
                      dec_picture.coded_height,
                      dec_picture.frame_width,
                      dec_picture.frame_height,
                      tmp_image,
                      info.max_coded_width,
                      info.max_coded_height );
        WriteOutput(out_file_name, out_file_name_tiled, tmp_image,
                    pic_display_number - 1, info.max_coded_width,
                    info.max_coded_height, info.interlaced_sequence,
                    dec_picture.top_field,dec_picture.first_field,
                    dec_picture.output_format);
      } else {
        WriteOutput(out_file_name, out_file_name_tiled, image_data,
                    pic_display_number - 1,
                    ( ( dec_picture.coded_width + 15 ) & ~15 ),
                    ( ( dec_picture.coded_height + 15 ) & ~15 ),
                    info.interlaced_sequence,
                    dec_picture.top_field, dec_picture.first_field,
                    dec_picture.output_format);
      }
    }

#else

    /* write PP output */
    number_of_written_frames++;
    if (!disable_output_writing) {
      if (pp_check_combined_status() == PP_OK) {
        pp_write_output( pic_decode_number - 1,
                         field_output,
                         dec_picture.top_field );
      } else {
        fprintf(stderr, "COMBINED STATUS FAILED\n");
      }

      if (pp_update_config
          (dec_inst, PP_PIPELINED_DEC_TYPE_VC1, &tb_cfg) == CFG_UPDATE_FAIL) {
        fprintf(stdout, "PP CONFIG LOAD FAILED\n");
      }
    }
#endif
    decsw_performance();
    START_SW_PERFORMANCE;
  }
  END_SW_PERFORMANCE;
  decsw_performance();
#endif

end:
#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
  if(output_thread_run)
    VC1DecEndOfStream(dec_inst, 1);
  if(output_thread)
    pthread_join(output_thread, NULL);
  if(release_thread)
    pthread_join(release_thread, NULL);
#endif

#ifdef USE_EXTERNAL_BUFFER
  add_buffer_thread_run = 0;
#endif
  DEBUG_PRINT(("\nWidth %d Height %d\n", coded_pic_width, coded_pic_height));

  if( stream_mem.virtual_address != NULL)
    DWLFreeLinear(((decContainer_t *) dec_inst)->dwl, &stream_mem);


  /* release decoder instance */
#ifdef PP_PIPELINE_ENABLED
  pp_close();
#endif
#ifdef USE_EXTERNAL_BUFFER
  ReleaseExtBuffers();
  pthread_mutex_destroy(&ext_buffer_contro);
#endif
  START_SW_PERFORMANCE;
  VC1DecRelease(dec_inst);
  END_SW_PERFORMANCE;
#ifdef USE_EXTERNAL_BUFFER
  DWLRelease(dwl_inst);
#endif

#ifdef ASIC_TRACE_SUPPORT
  trace_SequenceCtrl(hw_dec_pic_count, b_frames);
  trace_RefbufferHitrate();
  trace_VC1DecodingTools();
  closeTraceFiles();
#endif
  if(foutput)
    fclose(foutput);
  if( f_stream_trace)
    fclose(f_stream_trace);
  if(f_tiled_output)
    fclose(f_tiled_output);
  if (long_stream && finput)
    fclose(finput);

  /* free allocated buffers */
  if (tmp_image)
    free(tmp_image);
  if (tmp_strm)
    free(tmp_strm);

  foutput = fopen(out_file_name, "rb");
  if (NULL == foutput) {
    strm_len = 0;
  } else {
    fseek(foutput, 0L, SEEK_END);
    strm_len = (u32) ftell(foutput);
    fclose(foutput);
  }

  DEBUG_PRINT(("Output file: %s\n", out_file_name));

  DEBUG_PRINT(("OUTPUT_SIZE %d\n", strm_len));
  DEBUG_PRINT(("NUMBER OF WRITTEN FRAMES %d\n", number_of_written_frames));

  FINALIZE_SW_PERFORMANCE;

  DEBUG_PRINT(("DECODING DONE\n"));

  if(num_errors || pic_decode_number == 1) {
    DEBUG_PRINT(("ERRORS FOUND in %d out of %d PICTURES\n",
                 num_errors, pic_decode_number));
    return 1;
  }

  return 0;
}

/*------------------------------------------------------------------------------

 Function name:  GetNextDuSize

  Purpose:
    Get stream slice by slice...

------------------------------------------------------------------------------*/
u32 GetNextDuSize(const u8* stream, const u8* stream_start, u32 strm_len, u32 *skipped_bytes) {
  const u8* p;
  u8 byte;
  u32 zero = 0;
  u32 size = 0;
  u32 total_size = 0;
  u32 sc_prefix = 0;
  u32 next_packet = 0;
  u32 tmp = 0;

  *skipped_bytes = 0;
  strm_len -= (stream - stream_start);
  p = stream;

  if (strm_len < 3)
    return strm_len;

  /* If enabled, loose the packets (skip this packet first though) */
  if (stream_packet_loss) {
    u32 ret = 0;
    ret = TBRandomizePacketLoss(tb_cfg.tb_params.stream_packet_loss, (u8 *)&next_packet);
    if (ret != 0) {
      printf("RANDOM STREAM ERROR FAILED\n");
      return 0;
    }
  }
  if (pic_rdy && next_packet && (hdrs_rdy || stream_header_corrupt)) {
    DEBUG_PRINT(("\nPacket Loss\n"));
    tmp = 1;
  }
  /*else
  {
      DEBUG_PRINT(("\nNo Packet Loss\n"));
  } */

  /* Seek start of the next slice */
  while(1) {
    byte = *p++;
    size++;
    total_size++;

    if (total_size >= strm_len)
      return size;

    if (!byte)
      zero++;
    else if ( (byte == 0x01) && (zero >=2) )
      sc_prefix = 1;
    else if (sc_prefix && (byte>=0x0A && byte<=0x0F) && slice_ud_in_packet) {
      DEBUG_PRINT(("slice_ud_in_packet\n"));
      if (tmp) {
        zero = 0;
        sc_prefix = 0;
        tmp = 0;
      } else {
        *skipped_bytes += (size-4);
        size -= *skipped_bytes;
        zero = 0;
        sc_prefix = 0;

        if (next_packet)
          size = 0;

        break;
      }
    } else if (sc_prefix && ((byte>=0x0A && byte<=0x0F) || (byte>=0x1B && byte<=0x1F)) && !slice_ud_in_packet) {
      DEBUG_PRINT(("No slice_ud_in_packet\n"));
      if (tmp) {
        zero = 0;
        sc_prefix = 0;
        tmp = 0;
      } else {
        *skipped_bytes += (size-4);
        size -= *skipped_bytes;
        zero = 0;
        sc_prefix = 0;

        if (next_packet)
          size = 0;

        break;
      }
    } else {
      zero = 0;
      sc_prefix = 0;
    }
  }
  zero = 0;
  /* Seek end of the next slice */
  while(1) {
    byte = *p++;
    size++;
    total_size++;

    if (total_size >= strm_len)
      return size;

    if (!byte)
      zero++;
    else if ( (byte == 0x01) && (zero >=2) )
      sc_prefix = 1;
    else if (sc_prefix && (byte>=0x0A && byte<=0x0F) && slice_ud_in_packet) {
      DEBUG_PRINT(("slice_ud_in_packet\n"));
      size -= 4;
      break;
    } else if (sc_prefix && ((byte>=0x0A && byte<=0x0F) || (byte>=0x1B && byte<=0x1F)) && !slice_ud_in_packet) {
      DEBUG_PRINT(("No slice_ud_in_packet\n"));
      size -= 4;
      break;
    } else {
      zero = 0;
      sc_prefix = 0;
    }
  }

  /*if (pic_rdy && next_packet && ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt)
  {
      DEBUG_PRINT(("\nPacket Loss\n"));
      return GetNextDuSize(stream + size, stream_start + size, strm_len - size, skipped_bytes);
  }*/
  /*if (pic_rdy && stream_truncate && ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt))
  {
      i32 ret;
      DEBUG_PRINT(("Original packet size %d\n", size));
      ret = TBRandomizeU32(&index);
      if(ret != 0)
      {
          DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
          return 0;
      }
      DEBUG_PRINT(("Randomized packet size %d\n", size));
  }*/
  return size;
}

/*------------------------------------------------------------------------------

 Function name:  WriteOutput

  Purpose:
  Write picture pointed by data to file. Size of the
  picture in pixels is indicated by pic_size.

------------------------------------------------------------------------------*/
void WriteOutput(char *filename, char *filename_tiled, u8 * data, u32 frame_number,
                 u32 width, u32 height, u32 interlaced, u32 top, u32 first_field,
                 u32 tiled_mode) {
  u32 tmp, i, j;
  u32 pic_size;
  u8 *p, *ptmp;
  u8 *raster_scan = NULL;

  if (!interlaced_field && first_field)
    number_of_written_frames++;

  if(disable_output_writing != 0) {
    return;
  }

  pic_size = width * height * 3/2;

  DEBUG_PRINT(("picture %d in display order \n", ++display_order));
  /* foutput is global file pointer */
  if(foutput == NULL) {
    /* open output file for writing, can be disabled with define.
        * If file open fails -> exit */
    if(strcmp(filename, "none") != 0) {
      foutput = fopen(filename, "wb");
      if(foutput == NULL) {
        DEBUG_PRINT(("UNABLE TO OPEN OUTPUT FILE\n"));
        return;
      }
    }
  }


  /* Convert back to raster scan format if decoder outputs
   * tiled format */
  if(tiled_mode && convert_tiled_output) {
    u32 eff_height = (height + 15) & (~15);
    raster_scan = (u8*)malloc(width*eff_height*3/2);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for tiled"
              "-->raster conversion!\n");
      return;
    }

    TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                     data, raster_scan, width, eff_height );
    TBTiledToRaster( tiled_mode, convert_to_frame_dpb ? dpb_mode : DEC_DPB_FRAME,
                     data+width*eff_height,
                     raster_scan+width*height, width, eff_height/2 );
    data = raster_scan;
  } else if(convert_to_frame_dpb && (dpb_mode != DEC_DPB_FRAME)) {
    u32 eff_height = (height + 15) & (~15);
    raster_scan = (u8*)malloc(width*eff_height*3/2);
    if(!raster_scan) {
      fprintf(stderr, "error allocating memory for field"
              "-->frame DPB conversion!\n");
      return;
    }

    TBFieldDpbToFrameDpb( 1, data, raster_scan, 0, width, eff_height );

    data = raster_scan;
  }

#if 0
  if (f_tiled_output == NULL && tiled_output) {
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

  if(foutput && data) {
    /* field output */
    if (interlaced && interlaced_field) {
      /* start of top field */
      p = data;
      /* start of bottom field */
      if (!top)
        p += width;
      if (planar_output) {
        /* luma */
        for (i = 0; i < height/2; i++, p+=(width*2))
          fwrite(p, 1, width, foutput);
        ptmp = p;
        /* cb */
        for (i = 0; i < height/4; i++, p+=(width*2))
          for (j = 0; j < width>>1; j++)
            fwrite(p+j*2, 1, 1, foutput);
        /* cr */
        for (i = 0; i < height/4; i++, ptmp+=(width*2))
          for (j = 0; j < width>>1; j++)
            fwrite(ptmp+1+j*2, 1, 1, foutput);
      } else {
        /* luma */
        for (i = 0; i < height/2; i++, p+=(width*2))
          fwrite(p, 1, width, foutput);
        /* chroma */
        for (i = 0; i < height/4; i++, p+=(width*2)) {
          fwrite(p, 1, width, foutput);
        }
      }
    } else { /* frame output */
#ifndef ASIC_TRACE_SUPPORT
      u8* pic_copy = NULL;
#endif

      if (interlaced && !interlaced_field && first_field)
        return;

#ifndef ASIC_TRACE_SUPPORT
      if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
        pic_copy = (u8*) malloc(pic_size);
        if (NULL == pic_copy) {
          DEBUG_PRINT(("MALLOC FAILED @ %s %d", __FILE__, __LINE__));
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
      TBWriteFrameMD5Sum(foutput, data, pic_size, number_of_written_frames - 1);

      /* tiled */
      if (tiled_output) {
        /* round up to next multiple of 16 */
        u32 tmp_width = (width + 15) & ~15;
        u32 tmp_height = (height + 15) & ~15;

        TbWriteTiledOutput(f_tiled_output, data, tmp_width >> 4, tmp_height >> 4,
                           number_of_written_frames - 1, 1 /* write md5sum */, 1 /* semi-planar data */ );
      }
#else

      /* this presumes output has system endianess */
      if (planar_output) {
        tmp = pic_size * 2 / 3;
        fwrite(data, 1, tmp, foutput);
        for (i = 0; i < tmp/4; i++)
          fwrite(data+tmp+i*2, 1, 1, foutput);
        for (i = 0; i < tmp/4; i++)
          fwrite(data+tmp+1+i*2, 1, 1, foutput);
      } else /* semi-planar */
        fwrite(data, 1, pic_size, foutput);

      /* tiled */
      if (tiled_output) {
        /* round up to next multiple of 16 */
        u32 tmp_width = (width + 15) & ~15;
        u32 tmp_height = (height + 15) & ~15;

        TbWriteTiledOutput(f_tiled_output, data, tmp_width >> 4, tmp_height >> 4,
                           number_of_written_frames - 1, 0 /* not write md5sum */, 1 /* semi-planar data */ );
      }
#endif

#ifndef ASIC_TRACE_SUPPORT
      if(output_picture_endian == DEC_X170_BIG_ENDIAN) {
        free(pic_copy);
      }
#endif
    }
  }

  if(raster_scan)
    free(raster_scan);

}

/*------------------------------------------------------------------------------

    Function name: CropPicture

     Purpose:
     Perform cropping for picture. Input picture p_in_image with dimensions
     pic_width x pic_height is cropped into out_width x out_height and the
     resulting picture is stored in p_out_image.

------------------------------------------------------------------------------*/
u32 CropPicture(u8 *p_out_image, u8 *p_in_image,
                u32 pic_width, u32 pic_height, u32 out_width, u32 out_height ) {

  u32 i, j;
  u8 *p_out, *p_in;

  if (p_out_image == NULL || p_in_image == NULL ||
      !out_width || !out_height ||
      !pic_width || !pic_height) {
    /* just to prevent lint warning, returning non-zero will result in
        * return without freeing the memory */
    free(p_out_image);
    return(1);
  }

  /* Calculate starting pointer for luma */
  p_in = p_in_image;
  p_out = p_out_image;

  /* Copy luma pixel values */
  for (i = out_height; i; i--) {
    for (j = out_width; j; j--) {
      *p_out++ = *p_in++;
    }
    p_in += pic_width - out_width;
  }

  out_width >>= 1;
  out_height >>= 1;

  /* Calculate starting pointer for cb */
  p_in = p_in_image + pic_width*pic_height;

  /* Copy chroma pixel values */
  for (i = out_height; i; i--) {
    for (j = out_width*2; j; j--) {
      *p_out++ = *p_in++;
    }
    p_in += pic_width - out_width*2;
  }

  return (0);
}

/*------------------------------------------------------------------------------

    Function name:  VC1DecTrace

     Purpose:
     Example implementation of H264DecTrace function. Prototype of this
     function is given in H264DecApi.h. This implementation appends
     trace messages to file named 'dec_api.trc'.

------------------------------------------------------------------------------*/
/*void VC1DecTrace(const char *string)
{
    FILE *fp;

    fp = fopen("dec_api.trc", "at");

    if(!fp)
        return;

    fwrite(string, 1, strlen(string), fp);
    fwrite("\n", 1, 1, fp);

    fclose(fp);
}*/

#define SHOW1(p) (p[0]); p+=1;
#define SHOW2(p) (p[0]) | (p[1]<<8); p+=2;
#define SHOW3(p) (p[0]) | (p[1]<<8) | (p[2]<<16); p+=3;
#define SHOW4(p) (p[0]) | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p+=4;

#define    BIT0(tmp)  ((tmp & 1)   >>0);
#define    BIT1(tmp)  ((tmp & 2)   >>1);
#define    BIT2(tmp)  ((tmp & 4)   >>2);
#define    BIT3(tmp)  ((tmp & 8)   >>3);
#define    BIT4(tmp)  ((tmp & 16)  >>4);
#define    BIT5(tmp)  ((tmp & 32)  >>5);
#define    BIT6(tmp)  ((tmp & 64)  >>6);
#define    BIT7(tmp)  ((tmp & 128) >>7);

/*------------------------------------------------------------------------------

    Function name:  DecodeFrameLayerData

     Purpose:
     Decodes initialization frame layer from rcv format.

      Returns:
      Frame size in bytes.

------------------------------------------------------------------------------*/
u32 DecodeFrameLayerData(u8 *stream) {
  u32 tmp = 0;
  u32 time_stamp = 0;
  u32 frame_size = 0;
  u8 *p = stream;

  frame_size = SHOW3(p);
  tmp = SHOW1(p);
  tmp = BIT7(tmp);
  if( rcv_v2 ) {
    time_stamp = SHOW4(p);
    if (tmp == 1)
      DEBUG_PRINT(("INTRA FRAME timestamp: %d size: %d\n",
                   time_stamp, frame_size));
    else
      DEBUG_PRINT(("INTER FRAME timestamp: %d size: %d\n",
                   time_stamp, frame_size));
  } else {
    if (tmp == 1)
      DEBUG_PRINT(("INTRA FRAME size: %d\n", frame_size));
    else
      DEBUG_PRINT(("INTER FRAME size: %d\n", frame_size));
  }

  return frame_size;
}

/*------------------------------------------------------------------------------

    Function name:  DecodeRCV

     Purpose:
     Decodes initialization metadata from rcv format.

------------------------------------------------------------------------------*/
i32 DecodeRCV(u8 *stream, u32 strm_len, VC1DecMetaData *meta_data) {
  u32 tmp1 = 0;
  u32 tmp2 = 0;
  u32 tmp3 = 0;
  u32 profile = 0;
  u8 *p;
  p = stream;

  if (strm_len < 9*4+8)
    return -1;

  tmp1 = SHOW3(p);
  DEBUG_PRINT(("\nNumframes: \t %d\n", tmp1));
  tmp1 = SHOW1(p);
  DEBUG_PRINT(("0xC5: \t\t %0X\n", tmp1));
  if( tmp1 & 0x40 )   rcv_v2 = 1;
  else                rcv_v2 = 0;
  DEBUG_PRINT(("RCV_VERSION: \t\t %d\n", rcv_v2+1 ));

  rcv_metadata_size = SHOW4(p);
  DEBUG_PRINT(("0x04: \t\t %0X\n", rcv_metadata_size));

  /* Decode image dimensions */
  p += rcv_metadata_size;
  tmp1 = SHOW4(p);
  meta_data->max_coded_height = tmp1;
  tmp1 = SHOW4(p);
  meta_data->max_coded_width = tmp1;

  return 0;
}

/*------------------------------------------------------------------------------

    FramePicture

        Create frame of max-coded-width*max-coded-height around output image.
        Useful for system model verification, this way we can directly compare
        our output to the DecRef_* output generated by the reference decoder..

------------------------------------------------------------------------------*/
void FramePicture( u8 *p_in, i32 in_width, i32 in_height,
                   i32 in_frame_width, i32 in_frame_height,
                   u8 *p_out, i32 out_width, i32 out_height ) {

  /* Variables */

  i32 x, y;

  /* Code */

  memset( p_out, 0, out_width*out_height*3/2 );

  /* Luma */
  p_out += out_width * ( out_height - in_height );
  for ( y = 0 ; y < in_height ; ++y ) {
    p_out += ( out_width - in_width );
    for( x = 0 ; x < in_width; ++x )
      *p_out++ = *p_in++;
    p_in += ( in_frame_width - in_width );
  }

  p_in += in_frame_width * ( in_frame_height - in_height );

  in_frame_height /= 2;
  in_frame_width /= 2;
  out_height /= 2;
  out_width /= 2;
  in_height /= 2;
  in_width /= 2;

  /* Chroma */
  p_out += 2 * out_width * ( out_height - in_height );
  for ( y = 0 ; y < in_height ; ++y ) {
    p_out += 2 * ( out_width - in_width );
    for( x = 0 ; x < 2*in_width; ++x )
      *p_out++ = *p_in++;
    p_in += 2 * ( in_frame_width - in_width );
  }

}

void vc1DecPrintReturnValue(VC1DecRet ret) {
  switch(ret) {
  case VC1DEC_OK:
    DEBUG_PRINT(("VC1DEC_OK\n"));
    break;

  case VC1DEC_HDRS_RDY:
    DEBUG_PRINT(("VC1DEC_HDRS_RDY\n"));
    break;

  case VC1DEC_PIC_RDY:
    DEBUG_PRINT(("VC1DEC_PIC_RDY\n"));
    break;

  case VC1DEC_END_OF_SEQ:
    DEBUG_PRINT(("VC1DEC_END_OF_SEQ\n"));
    break;

  case VC1DEC_PIC_DECODED:
    DEBUG_PRINT(("VC1DEC_PIC_DECODED\n"));
    break;

  case VC1DEC_RESOLUTION_CHANGED:
    DEBUG_PRINT(("VC1DEC_RESOLUTION_CHANGED\n"));
    break;

  case VC1DEC_STRM_ERROR:
    DEBUG_PRINT(("VC1DEC_STRM_ERROR\n"));
    break;

  case VC1DEC_STRM_PROCESSED:
    DEBUG_PRINT(("VC1DEC_STRM_PROCESSED\n"));
    break;

  case VC1DEC_NO_DECODING_BUFFER:
    DEBUG_PRINT(("VC1DEC_NO_DECODING_BUFFER\n"));
    break;

  case VC1DEC_PARAM_ERROR:
    DEBUG_PRINT(("VC1DEC_PARAM_ERROR\n"));
    break;

  case VC1DEC_NOT_INITIALIZED:
    DEBUG_PRINT(("VC1DEC_NOT_INITIALIZED\n"));
    break;

  case VC1DEC_MEMFAIL:
    DEBUG_PRINT(("VC1DEC_MEMFAIL\n"));
    break;

  case VC1DEC_INITFAIL:
    DEBUG_PRINT(("VC1DEC_INITFAIL\n"));
    break;

  case VC1DEC_METADATA_FAIL:
    DEBUG_PRINT(("VC1DEC_METADATA_FAIL\n"));
    break;

  case VC1DEC_HW_RESERVED:
    DEBUG_PRINT(("VC1DEC_HW_RESERVED\n"));
    break;

  case VC1DEC_HW_TIMEOUT:
    DEBUG_PRINT(("VC1DEC_HW_TIMEOUT\n"));
    break;

  case VC1DEC_HW_BUS_ERROR:
    DEBUG_PRINT(("VC1DEC_HW_BUS_ERROR\n"));
    break;

  case VC1DEC_SYSTEM_ERROR:
    DEBUG_PRINT(("VC1DEC_SYSTEM_ERROR\n"));
    break;

  case VC1DEC_DWL_ERROR:
    DEBUG_PRINT(("VC1DEC_DWL_ERROR\n"));
    break;

  case VC1DEC_NONREF_PIC_SKIPPED:
    DEBUG_PRINT(("VC1DEC_NONREF_PIC_SKIPPED\n"));
    break;

  default:

    DEBUG_PRINT(("unknown return value!\n"));
  }
}

/*------------------------------------------------------------------------------

    Function name:            printVc1PicCodingType

    Functional description:   Print out picture coding type value

------------------------------------------------------------------------------*/
void printVc1PicCodingType(u32 *pic_type) {
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
  case DEC_PIC_TYPE_BI:
    printf("[BI:");
    break;
  default:
    printf("[Other %d:", *pic_type);
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
  case DEC_PIC_TYPE_BI:
    printf("BI]");
    break;
  default:
    printf("Other %d]", *pic_type);
    break;
  }
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

/*------------------------------------------------------------------------------

    Function name: fillBuffer

------------------------------------------------------------------------------*/
u32 fillBuffer(u8 *stream) {
  u32 amount = 0;
  u32 data_len = 0;
  off64_t pos = ftello64(finput);
  int ret;
  if(cur_index != pos) {
    fseeko64(finput, cur_index, SEEK_SET);
  }
#ifdef USE_64BIT_ENV
  ret = fscanf(f_index, "%lu", &next_index);
#else
  ret = fscanf(f_index, "%llu", &next_index);
#endif
  amount += next_index - cur_index;
  cur_index = next_index;

  /* read data */
  data_len = fread(stream, 1, amount, finput);

  return data_len;
}

