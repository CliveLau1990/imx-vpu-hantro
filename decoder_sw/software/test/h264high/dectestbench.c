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

#ifdef PP_PIPELINE_ENABLED
#include "ppapi.h"
#include "pptestbench.h"
#endif

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

void WriteOutput(char *filename, char *filename_tiled, u8 * data, u32 pic_size,
                 u32 pic_width, u32 pic_height, u32 frame_number, u32 mono_chrome,
                 u32 view, u32 tiled_mode);
u32 NextPacket(u8 ** p_strm);
u32 NextPacketFromFile(u8 ** p_strm);
u32 CropPicture(u8 * p_out_image, u8 * p_in_image,
                u32 frame_width, u32 frame_height, H264CropParams * p_crop_params,
                u32 mono_chrome);
static void printDecodeReturn(i32 retval);
void printH264PicCodingType(u32 *pic_type);

void SetMissingField2Const( u8 *output_picture,
                            u32 pic_width,
                            u32 pic_height,
                            u32 monochrome,
                            u32 top_field);

#ifdef PP_PIPELINE_ENABLED
static void HandlePpOutput(u32 pic_display_number, u32 view_id);
#endif

u32 fillBuffer(const u8 *stream);
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
/*u32 tiled_output = 0;*/

/* flag to enable md5sum output */
u32 md5sum = 0;

u32 tiled_output = DEC_REF_FRM_RASTER_SCAN;
u32 dpb_mode = DEC_DPB_FRAME;
u32 convert_tiled_output = 0;

u32 use_peek_output = 0;
u32 enable_mvc = 0;
u32 mvc_separate_views = 0;
u32 skip_non_reference = 0;
u32 convert_to_frame_dpb = 0;

/* variables for indexing */
u32 save_index = 0;
u32 use_index = 0;
off64_t cur_index = 0;
off64_t next_index = 0;
/* indicate when we save index */
u8 save_flag = 0;

u32 b_frames;

char *grey_chroma = NULL;
size_t grey_chroma_size = 0;

char *pic_big_endian = NULL;
size_t pic_big_endian_size = 0;

u32 ds_ratio_x, ds_ratio_y;
u32 pp_enabled = 0;

#ifdef ASIC_TRACE_SUPPORT
extern u32 hw_dec_pic_count;
extern u32 g_hw_ver;
extern u32 h264_high_support;
#endif

#ifdef H264_EVALUATION
extern u32 g_hw_ver;
extern u32 h264_high_support;
#endif

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

static void *AddBufferThread(void *arg) {
  usleep(100000);
  while(add_buffer_thread_run) {
    pthread_mutex_lock(&ext_buffer_contro);
    if(add_extra_flag && (num_buffers < 34)) {
      struct DWLLinearMem mem;
      i32 dwl_ret;
      mem.mem_type = DWL_MEM_TYPE_DPB;
      if (pp_enabled)
        dwl_ret = DWLMallocLinear(dwl_inst, buffer_size, &mem);
      else
        dwl_ret = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
      if(dwl_ret == DWL_OK) {
        H264DecRet rv = H264DecAddBuffer(dec_inst, &mem) ;
        if(rv != H264DEC_OK && rv != H264DEC_WAITING_FOR_BUFFER) {
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
H264DecInfo dec_info;
char out_file_name[256] = "";
char out_file_name_tiled[256] = "out_tiled.yuv";
u32 pic_size = 0;
u32 crop_display = 0;
u8 *tmp_image = NULL;
u32 num_errors = 0;
pthread_t output_thread;
pthread_t release_thread;
int output_thread_run = 0;

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
      H264DecPictureConsumed(dec_inst, &buf_list[list_pop_index]);
      buf_status[list_pop_index] = 0;
      list_pop_index++;
      if(list_pop_index == 100)
        list_pop_index = 0;
#ifdef USE_EXTERNAL_BUFFER
      if(allocate_extra_buffers_in_output) {
        pthread_mutex_lock(&ext_buffer_contro);
        if(add_extra_flag && (num_buffers < 34)) {
          struct DWLLinearMem mem;
          i32 dwl_ret;
          mem.mem_type = DWL_MEM_TYPE_DPB;
          if (pp_enabled)
            dwl_ret = DWLMallocLinear(dwl_inst, buffer_size, &mem);
          else
            dwl_ret = DWLMallocRefFrm(dwl_inst, buffer_size, &mem);
          if(dwl_ret == DWL_OK) {
            H264DecRet rv = H264DecAddBuffer(dec_inst, &mem) ;
            if(rv != H264DEC_OK && rv != H264DEC_WAITING_FOR_BUFFER) {
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
    /* The last buffer has been consumed */
    if(last_pic_flag &&  buf_status[list_pop_index] == 0)
      break;
    usleep(10000);
  }
  return NULL;
}

/* Output thread entry point. */
static void* h264_output_thread(void* arg) {
  H264DecPicture dec_picture;
  u32 pic_display_number = 1;
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

    ret = H264DecNextPicture(dec_inst, &dec_picture, 0);

    if(ret == H264DEC_PIC_RDY) {
      if(!use_peek_output) {
        DEBUG_PRINT(("PIC %d/%d, view %d, type [%s:%s], ",
                     pic_display_number,
                     dec_picture.pic_id,
                     dec_picture.view_id,
                     dec_picture.is_idr_picture[0] ? "IDR" : "NON-IDR",
                     dec_picture.is_idr_picture[1] ? "IDR" : "NON-IDR"));

        /* pic coding type */
        printH264PicCodingType(dec_picture.pic_coding_type);

        if(dec_picture.nbr_of_err_mbs) {
          DEBUG_PRINT((", concealed %d", dec_picture.nbr_of_err_mbs));
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

        num_errors += dec_picture.nbr_of_err_mbs;

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
                    crop_display ?
                    dec_picture.crop_params.crop_out_height : dec_picture.pic_height,
                    pic_display_number - 1, dec_info.mono_chrome,
                    dec_picture.view_id,
                    dec_picture.output_format);
      }

      /* Push output buffer into buf_list and wait to be consumed */
      buf_list[list_push_index] = dec_picture;
      buf_status[list_push_index] = 1;

      list_push_index++;
      if(list_push_index == 100)
        list_push_index = 0;

      sem_post(&buf_release_sem);

      pic_display_number++;
    } else if(ret == H264DEC_END_OF_STREAM) {
      last_pic_flag = 1;
      DEBUG_PRINT(("END-OF-STREAM received in output thread\n"));
#ifdef USE_EXTERNAL_BUFFER
      add_buffer_thread_run = 0;
#endif
      break;
    }
  }
  return NULL;
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
  /* TODO: Fix reference data before enabling this */
  return;

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
      field_base += pic_width * pic_height / 4;
    }

    memset(field_base, 128, pic_width * pic_height / 4);

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
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  u32 pic_size = 0;
  u8 *tmp_image = NULL;
  H264DecInfo dec_info;
  u32 crop_display = 0;
  char out_file_name[256] = "";
  char out_file_name_tiled[256] = "out_tiled.yuv";
  u32 num_errors = 0;
#endif
  struct DWLLinearMem stream_mem;
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
  u32 pic_display_number = 0;
  u32 disable_output_reordering = 0;
  u32 use_display_smoothing = 0;
  u32 rlc_mode = 0;
  u32 mb_error_concealment = 0;
  u32 force_whole_stream = 0;
  const u8 *ptmpstream = NULL;
  u32 stream_will_end = 0;
  u32 eos = 0;
  int ra;

#ifdef PP_PIPELINE_ENABLED
  PPApiVersion pp_ver;
  PPBuild pp_build;
#endif

  FILE *f_tbcfg;
  u32 seed_rnd = 0;
  u32 stream_bit_swap = 0;
  i32 corrupted_bytes = 0;  /*  */

  struct DecDownscaleCfg dscale_cfg;

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

  INIT_SW_PERFORMANCE;

  {
    H264DecApiVersion dec_api;
    H264DecBuild dec_build;

    /* Print API version number */
    dec_api = H264DecGetAPIVersion();
    dec_build = H264DecGetBuild();
    DEBUG_PRINT(("\nX170 H.264 Decoder API v%d.%d - SW build: %d.%d - HW build: %x\n\n",
                 dec_api.major, dec_api.minor, dec_build.sw_build >>16,
                 dec_build.sw_build & 0xFFFF, dec_build.hw_build));
  }

#ifndef PP_PIPELINE_ENABLED
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
    DEBUG_PRINT(("\t-W disable packetizing even if stream does not fit to buffer.\n"
                 "\t   Only the first 0x1FFFFF bytes of the stream are decoded.\n"));
    DEBUG_PRINT(("\t-E use tiled reference frame format.\n"));
    DEBUG_PRINT(("\t-G convert tiled output pictures to raster scan\n"));
    DEBUG_PRINT(("\t-L enable support for long streams\n"));
    DEBUG_PRINT(("\t-P write planar output\n"));
    DEBUG_PRINT(("\t-I save index file\n"));
    DEBUG_PRINT(("\t-M Enable MVC decoding (use it only with MVC streams)\n"));
    DEBUG_PRINT(("\t-V Write MVC views to separate files\n"));
    DEBUG_PRINT(("\t-Q Skip decoding non-reference pictures.\n"));
    DEBUG_PRINT(("\t-Z output pictures using H264DecPeek() function\n"));
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
#ifdef ASIC_TRACE_SUPPORT
    DEBUG_PRINT(("\t-F force 8170 mode in 8190 HW model (baseline configuration forced)\n"));
    DEBUG_PRINT(("\t-B force Baseline configuration to 8190 HW model\n"));
#endif
    return 0;
  }

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
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if (strncmp(argv[i], "-G", 2) == 0)
      convert_tiled_output = 1;
    else if(strcmp(argv[i], "-R") == 0) {
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
    } else if(strcmp(argv[i], "-I") == 0) {
      save_index = 1;
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
#ifdef ASIC_TRACE_SUPPORT
    else if(strcmp(argv[i], "-F") == 0) {
      g_hw_ver = 8170;
      h264_high_support = 0;
      printf("\n\nForce 8170 mode to HW model!!!\n\n");
    } else if(strcmp(argv[i], "-B") == 0) {
      h264_high_support = 0;
      printf("\n\nForce Baseline configuration to 8190 HW model!!!\n\n");
    }
#endif
    else if(strcmp(argv[i], "-Z") == 0) {
      use_peek_output = 1;
    } else if(strcmp(argv[i], "--md5") == 0) {
      md5sum = 1;
    } else if (strncmp(argv[i], "-d", 2) == 0) {
#ifdef USE_EXTERNAL_BUFFER
      pp_enabled = 1;
#endif
      if (strlen(argv[i]) == 3 &&
          (argv[i][2] == '1' || argv[i][2] == '2' || argv[i][2] == '4' || argv[i][2] == '8')) {
        ds_ratio_x = ds_ratio_y = argv[i][2] - '0';
      } else if (strlen(argv[i]) == 5 &&
                 ((argv[i][2] == '1' || argv[i][2] == '2' || argv[i][2] == '4' || argv[i][2] == '8') &&
                  (argv[i][4] == '1' || argv[i][4] == '2' || argv[i][4] == '4' || argv[i][4] == '8') &&
                  argv[i][3] == ':')) {
        ds_ratio_x = argv[i][2] - '0';
        ds_ratio_y = argv[i][4] - '0';
      } else {
        fprintf(stdout, "Illegal parameter: %s\n", argv[i]);
        fprintf(stdout, "ERROR: Enable down scaler parameter by using: -d[1248][:[1248]]\n");
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
    DEBUG_PRINT(("UNABLE TO OPEN INPUT FILE\n"));
    return -1;
  }

#else
  /* Print API and build version numbers */
  pp_ver = PPGetAPIVersion();
  pp_build = PPGetBuild();

  /* Version */
  fprintf(stdout,
          "\nX170 PP API v%d.%d - SW build: %d - HW build: %x\n\n",
          pp_ver.major, pp_ver.minor, pp_build.sw_build, pp_build.hw_build);

  /* Check that enough command line arguments given, if not -> print usage
   * information out */
  if(argc < 3) {
    DEBUG_PRINT(("Usage: %s [options] file.h264  pp.cfg\n",
                 argv[0]));
    DEBUG_PRINT(("\t-Nn forces decoding to stop after n pictures\n"));
    DEBUG_PRINT(("\t-X Disable output file writing\n"));
    DEBUG_PRINT(("\t--md5 Output frame based md5 checksum\n"));
    DEBUG_PRINT(("\t-R disable DPB output reordering\n"));
    DEBUG_PRINT(("\t-J enable double DPB for smooth display\n"));
    DEBUG_PRINT(("\t-W disable packetizing even if stream does not fit to buffer.\n"
                 "\t   NOTE: Useful only for debug purposes.\n"));
    DEBUG_PRINT(("\t-L enable support for long streams.\n"));
    DEBUG_PRINT(("\t-I save index file\n"));
    DEBUG_PRINT(("\t-E use tiled reference frame format.\n"));
    DEBUG_PRINT(("\t-M Enable MVC decoding (use it only with MVC streams)\n"));
    DEBUG_PRINT(("\t-V Write MVC views to separate files\n"));
    DEBUG_PRINT(("\t-Q Skip decoding non-reference pictures.\n"));
    DEBUG_PRINT(("\t--separate-fields-in-dpb DPB stores interlaced content"\
                 " as fields (default: frames)\n"));
#ifdef USE_EXTERNAL_BUFFER
    DEBUG_PRINT(("\t-A add extra external buffer randomly\n"));
#ifdef USE_OUTPUT_RELEASE
    DEBUG_PRINT(("\t-a allocate extra external buffer in output thread\n"));
#endif
#endif
#ifdef ASIC_TRACE_SUPPORT
    DEBUG_PRINT(("\t-F force 8170 mode in 8190 HW model (baseline configuration forced)\n"));
    DEBUG_PRINT(("\t-B force Baseline configuration to 8190 HW model\n"));
#endif
    return 0;
  }

  /* read command line arguments */
  for(i = 1; i < (u32) (argc - 2); i++) {
    if(strncmp(argv[i], "-N", 2) == 0) {
      max_num_pics = (u32) atoi(argv[i] + 2);
    } else if(strcmp(argv[i], "-X") == 0) {
      disable_output_writing = 1;
    } else if(strcmp(argv[i], "-R") == 0) {
      disable_output_reordering = 1;
    } else if(strcmp(argv[i], "-J") == 0) {
#ifndef USE_EXTERNAL_BUFFER
      use_display_smoothing = 1;
#endif
    } else if (strncmp(argv[i], "-E", 2) == 0)
      tiled_output = DEC_REF_FRM_TILED_DEFAULT;
    else if(strcmp(argv[i], "-W") == 0) {
      force_whole_stream = 1;
    } else if(strcmp(argv[i], "-L") == 0) {
      long_stream = 1;
    } else if(strcmp(argv[i], "-I") == 0) {
      save_index = 1;
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
    else if(strcmp(argv[i], "-Q") == 0) {
      skip_non_reference = 1;
    } else if(strcmp(argv[i], "--separate-fields-in-dpb") == 0) {
      dpb_mode = DEC_DPB_INTERLACED_FIELD;
    }
#ifdef ASIC_TRACE_SUPPORT
    else if(strcmp(argv[i], "-F") == 0) {
      g_hw_ver = 8170;
      h264_high_support = 0;
      printf("\n\nForce 8170 mode to HW model!!!\n\n");
    } else if(strcmp(argv[i], "-B") == 0) {
      h264_high_support = 0;
      printf("\n\nForce Baseline configuration to 8190 HW model!!!\n\n");
    }
#endif
    else if(strcmp(argv[i], "-M") == 0) {
      enable_mvc = 1;
    } else if(strcmp(argv[i], "-V") == 0) {
      mvc_separate_views = 1;
    } else if(strcmp(argv[i], "--md5") == 0) {
      md5sum = 1;
    } else if (strncmp(argv[i], "-d", 2) == 0) {
#ifdef USE_EXTERNAL_BUFFER
      pp_enabled = 1;
#endif
      if (strlen(argv[i]) == 3 &&
          (argv[i][2] == '1' || argv[i][2] == '2' || argv[i][2] == '4' || argv[i][2] == '8')) {
        ds_ratio_x = ds_ratio_y = argv[i][2] - '0';
      } else if (strlen(argv[i]) == 5 &&
                 ((argv[i][2] == '1' || argv[i][2] == '2' || argv[i][2] == '4' || argv[i][2] == '8') &&
                  (argv[i][4] == '1' || argv[i][4] == '2' || argv[i][4] == '4' || argv[i][4] == '8') &&
                  argv[i][3] == ':')) {
        ds_ratio_x = argv[i][2] - '0';
        ds_ratio_y = argv[i][4] - '0';
      } else {
        fprintf(stdout, "Illegal parameter: %s\n", argv[i]);
        fprintf(stdout, "ERROR: Enable down scaler parameter by using: -d[1248][:[1248]]\n");
        return 1;
      }
    } else {
      DEBUG_PRINT(("UNKNOWN PARAMETER: %s\n", argv[i]));
      return 1;
    }
  }

  /* open data file */
  finput = fopen(argv[argc - 2], "rb");
  if(finput == NULL) {
    DEBUG_PRINT(("Unable to open input file %s\n", argv[argc - 2]));
    exit(100);
  }
#endif
  /* open index file for saving */
  if(save_index) {
    findex = fopen("stream.cfg", "w");
    if(findex == NULL) {
      DEBUG_PRINT(("UNABLE TO OPEN INDEX FILE\n"));
      return -1;
    }
  }
  /* try open index file */
  else {
    findex = fopen("stream.cfg", "r");
    if(findex != NULL) {
      use_index = 1;
    }

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

#ifdef ASIC_TRACE_SUPPORT
    if (g_hw_ver != 8170)
      g_hw_ver = tb_cfg.dec_params.hw_version;
#endif
  }
  /*TBPrintCfg(&tb_cfg); */
  mb_error_concealment = 0; /* TBGetDecErrorConcealment(&tb_cfg); */
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
  DEBUG_PRINT(("Decoder Macro Block Error Concealment %d\n",
               mb_error_concealment));
  DEBUG_PRINT(("Decoder RLC %d\n", rlc_mode));
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

  packetize = TBGetTBPacketByPacket(&tb_cfg);
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
  /* initialize decoder. If unsuccessful -> exit */
  START_SW_PERFORMANCE;
  {
    enum DecDpbFlags flags = 0;
    if( tiled_output )   flags |= DEC_REF_FRM_TILED_DEFAULT;
    if( dpb_mode == DEC_DPB_INTERLACED_FIELD )
      flags |= DEC_DPB_ALLOW_FIELD_ORDERING;

    dscale_cfg.down_scale_x = ds_ratio_x;
    dscale_cfg.down_scale_y = ds_ratio_y;
    ret =
      H264DecInit(&dec_inst,
#ifdef USE_EXTERNAL_BUFFER
                  dwl_inst,
#endif
                  disable_output_reordering,
                  TBGetDecErrorConcealment( &tb_cfg ),
                  use_display_smoothing, flags, 0, 0, 0,
                  &dscale_cfg );
  }
  END_SW_PERFORMANCE;
  if(ret != H264DEC_OK) {
    DEBUG_PRINT(("DECODER INITIALIZATION FAILED\n"));
    goto end;
  }

  /* configure decoder to decode both views of MVC stereo high streams */
  if (enable_mvc)
    H264DecSetMvc(dec_inst);

  /* Set ref buffer test mode */
  ((decContainer_t *) dec_inst)->ref_buffer_ctrl.test_function = TBRefbuTestMode;

#ifdef PP_PIPELINE_ENABLED
  /* Initialize the post processer. If unsuccessful -> exit */
  if(pp_startup
      (argv[argc - 1], dec_inst, PP_PIPELINED_DEC_TYPE_H264, &tb_cfg) != 0) {
    DEBUG_PRINT(("PP INITIALIZATION FAILED\n"));
    goto end;
  }

  if(pp_update_config
      (dec_inst, PP_PIPELINED_DEC_TYPE_H264, &tb_cfg) == CFG_UPDATE_FAIL)

  {
    fprintf(stdout, "PP CONFIG LOAD FAILED\n");
    goto end;
  }
#endif

  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_LATENCY,
                 latency_comp);
  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_CLK_GATE_E,
                 clock_gating);
  SetDecRegister(((decContainer_t *) dec_inst)->h264_regs, HWIF_DEC_OUT_TILED_E,
                 output_format);
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

  if(rlc_mode) {
    /*Force the decoder into RLC mode */
    ((decContainer_t *) dec_inst)->force_rlc_mode = 1;
    ((decContainer_t *) dec_inst)->rlc_mode = 1;
    ((decContainer_t *) dec_inst)->try_vlc = 0;
  }

#ifdef _ENABLE_2ND_CHROMA
  if (!TBGetDecCh8PixIleavOutput(&tb_cfg)) {
    ((decContainer_t *) dec_inst)->storage.enable2nd_chroma = 0;
  } else {
    /* cropping not supported when additional chroma output format used */
    crop_display = 0;
  }
#endif

  TBInitializeRandom(seed_rnd);

  /* check size of the input file -> length of the stream in bytes */
  fseek(finput, 0L, SEEK_END);
  strm_len = ftell(finput);
  rewind(finput);

  dec_input.skip_non_reference = skip_non_reference;

  if(!long_stream) {
    /* If the decoder can not handle the whole stream at once, force packet-by-packet mode */
    if(!force_whole_stream) {
      if(strm_len > DEC_X170_MAX_STREAM) {
        packetize = 1;
      }
    } else {
      if(strm_len > DEC_X170_MAX_STREAM) {
        packetize = 0;
        strm_len = DEC_X170_MAX_STREAM;
      }
    }

    /* sets the stream length to random value */
    if(stream_truncate && !packetize && !nal_unit_stream) {
      DEBUG_PRINT(("strm_len %ld\n", strm_len));
      ret = TBRandomizeU32((u32 *)&strm_len);
      if(ret != 0) {
        DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
        goto end;
      }
      DEBUG_PRINT(("Randomized strm_len %ld\n", strm_len));
    }

    /* NOTE: The DWL should not be used outside decoder SW.
     * here we call it because it is the easiest way to get
     * dynamically allocated linear memory
     * */

    /* allocate memory for stream buffer. if unsuccessful -> exit */
#ifndef ADS_PERFORMANCE_SIMULATION
#ifdef USE_EXTERNAL_BUFFER
    stream_mem.mem_type = DWL_MEM_TYPE_CPU;
    if(DWLMallocLinear(dwl_inst,
                       strm_len, &stream_mem) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#else
    stream_mem.mem_type = DWL_MEM_TYPE_CPU;
    if(DWLMallocLinear(((decContainer_t *) dec_inst)->dwl,
                       strm_len, &stream_mem) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#endif
#else
    stream_mem.virtual_address = malloc(strm_len);
    stream_mem.bus_address = (size_t) stream_mem.virtual_address;
#endif

    byte_strm_start = (u8 *) stream_mem.virtual_address;

    if(byte_strm_start == NULL) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }

    /* read input stream from file to buffer and close input file */
    ra = fread(byte_strm_start, 1, strm_len, finput);

    /* initialize H264DecDecode() input structure */
    stream_stop = byte_strm_start + strm_len;
    dec_input.stream = byte_strm_start;
    dec_input.stream_bus_address = (addr_t) stream_mem.bus_address;
    dec_input.data_len = strm_len;
  } else {
#ifndef ADS_PERFORMANCE_SIMULATION
#ifdef USE_EXTERNAL_BUFFER
    stream_mem.mem_type = DWL_MEM_TYPE_CPU;
    if(DWLMallocLinear(dwl_inst,
                       DEC_X170_MAX_STREAM, &stream_mem) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#else
    stream_mem.mem_type = DWL_MEM_TYPE_CPU;
    if(DWLMallocLinear(((decContainer_t *) dec_inst)->dwl,
                       DEC_X170_MAX_STREAM, &stream_mem) != DWL_OK) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#endif

#else
    stream_mem.virtual_address = malloc(DEC_X170_MAX_STREAM);
    stream_mem.bus_address = (size_t) stream_mem.virtual_address;

    if(stream_mem.virtual_address == NULL) {
      DEBUG_PRINT(("UNABLE TO ALLOCATE STREAM BUFFER MEMORY\n"));
      goto end;
    }
#endif

    /* initialize H264DecDecode() input structure */
    dec_input.stream = (u8 *) stream_mem.virtual_address;
    dec_input.stream_bus_address = (addr_t) stream_mem.bus_address;
  }

  if(long_stream && !packetize && !nal_unit_stream) {
    if(use_index == 1) {
      u32 amount = 0;
      cur_index = 0;

      /* read index */
      /* off64_t defined differ between LP32 and LP64*/
#ifdef USE_64BIT_ENV
      ra = fscanf(findex, "%lu", &next_index);
#else
      ra = fscanf(findex, "%llu", &next_index);
#endif
      {
        /* check if last index -> stream end */
        u8 byte[2];
        ra = fread(&byte, 2, 1, findex);
        if(feof(findex)) {
          DEBUG_PRINT(("STREAM WILL END\n"));
          stream_will_end = 1;
        } else {
          fseek(findex, -2, SEEK_CUR);
        }
      }

      amount = next_index - cur_index;
      cur_index = next_index;

      /* read first block */
      dec_input.data_len = fread((u8 *)dec_input.stream, 1, amount, finput);
    } else {
      dec_input.data_len =
        fread((u8 *)dec_input.stream, 1, DEC_X170_MAX_STREAM, finput);
    }
    /*DEBUG_PRINT(("BUFFER READ\n")); */
    if(feof(finput)) {
      DEBUG_PRINT(("STREAM WILL END\n"));
      stream_will_end = 1;
    }
  }

  else {
    if(use_index) {
      if(!nal_unit_stream)
#ifdef USE_64BIT_ENV
        ra = fscanf(findex, "%lu", &cur_index);
#else
        ra = fscanf(findex, "%llu", &cur_index);
#endif
    }

    /* get pointer to next packet and the size of packet
     * (for packetize or nal_unit_stream modes) */
    ptmpstream = dec_input.stream;
    if((tmp = NextPacket((u8 **) (&dec_input.stream))) != 0) {
      dec_input.data_len = tmp;
      dec_input.stream_bus_address += (addr_t) (dec_input.stream - ptmpstream);
    }
  }

  pic_decode_number = pic_display_number = 1;

  /* main decoding loop */
  do {
    save_flag = 1;
    /*DEBUG_PRINT(("dec_input.data_len %d\n", dec_input.data_len));
     * DEBUG_PRINT(("dec_input.stream %d\n", dec_input.stream)); */

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
            TBRandomizeBitSwapInStream((u8 *)dec_input.stream,
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

    /* call API function to perform decoding */
    START_SW_PERFORMANCE;
    ret = H264DecDecode(dec_inst, &dec_input, &dec_output);
    END_SW_PERFORMANCE;
    printDecodeReturn(ret);

    eos = 0;

    switch (ret) {
    case H264DEC_STREAM_NOT_SUPPORTED: {
      DEBUG_PRINT(("ERROR: UNSUPPORTED STREAM!\n"));
      goto end;
    }
    case H264DEC_HDRS_RDY: {
#ifdef DPB_REALLOC_DISABLE
#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      if(hdrs_rdy) {
        DEBUG_PRINT(("Decoding ended, flush the DPB\n"));

        /* if output in display order is preferred, the decoder shall be forced
         * to output pictures remaining in decoded picture buffer. Use function
         * H264DecNextPicture() to obtain next picture in display order. Function
         * is called until no more images are ready for display. Second parameter
         * for the function is set to '1' to indicate that this is end of the
         * stream and all pictures shall be output */
        while(H264DecNextPicture(dec_inst, &dec_picture, 1) == H264DEC_PIC_RDY) {
          if(use_peek_output) {
            /* we peeked all pictures so here we just flush the output */
            continue;
          }

          DEBUG_PRINT(("A PIC %d, type %s", pic_display_number,
                       dec_picture.is_idr_picture ? "IDR, " : "NON-IDR, "));

          /* pic coding type */
          printH264PicCodingType(dec_picture.pic_coding_type);

          if(pic_display_number != dec_picture.pic_id)
            DEBUG_PRINT((", decoded pic %d", dec_picture.pic_id));
          if(dec_picture.nbr_of_err_mbs) {
            DEBUG_PRINT((", concealed %d\n", dec_picture.nbr_of_err_mbs));
          }
          if(dec_picture.interlaced) {
            DEBUG_PRINT((", INTERLACED "));
            if(dec_picture.field_picture) {
              DEBUG_PRINT(("FIELD %s",
                           dec_picture.top_field ? "TOP" : "BOTTOM"));

              SetMissingField2Const((u8*)dec_picture.output_picture,
                                    dec_info.pic_width,
                                    dec_info.pic_height,
                                    dec_info.mono_chrome,
                                    !dec_picture.top_field);
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

          num_errors += dec_picture.nbr_of_err_mbs;

          /* Write output picture to file */
          image_data = (u8 *) dec_picture.output_picture;
          if(crop_display) {
            tmp = CropPicture(tmp_image, image_data,
                              dec_picture.pic_width, dec_picture.pic_height,
                              &dec_picture.crop_params, dec_info.mono_chrome);
            if(tmp) {
              DEBUG_PRINT(("ERROR in cropping!\n"));
              goto end;
            }
          }

#ifndef PP_PIPELINE_ENABLED
          WriteOutput(out_file_name, out_file_name_tiled,
                      crop_display ? tmp_image : image_data, pic_size,
                      crop_display ? dec_picture.crop_params.crop_out_width : dec_picture.pic_width,
                      crop_display ? dec_picture.crop_params.crop_out_height : dec_picture.pic_height,
                      pic_display_number - 1, dec_info.mono_chrome,
                      dec_picture.view_id,
                      dec_picture.output_format);
#else
          if(!disable_output_writing) {
            HandlePpOutput(pic_display_number, dec_picture.view_id);
          }

          pp_read_blend_components(((decContainer_t *) dec_inst)->pp.pp_instance);
          pp_check_combined_status();
          /* load new cfg if needed (handled in pp testbench) */
          if(pp_update_config(dec_inst, PP_PIPELINED_DEC_TYPE_H264, &tb_cfg) == CFG_UPDATE_FAIL) {
            fprintf(stdout, "PP CONFIG LOAD FAILED\n");
          }
#endif
          /* Increment display number for every displayed picture */
          pic_display_number++;
        }
      }
#else
      if(hdrs_rdy) {
        DEBUG_PRINT(("Decoding ended, flush the DPB\n"));
        /* the end of stream is not reached yet */
        H264DecEndOfStream(dec_inst, 0);
      }
#endif
#endif
      save_flag = 0;
      /* Set a flag to indicate that headers are ready */
      hdrs_rdy = 1;
      printf("sizeof(dpb) = %ld\n", sizeof(dpbStorage_t));
      printf("offset of dpbs = %ld\n", (u8 *)((decContainer_t *) dec_inst)->storage.dpbs-(u8 *)&(((decContainer_t *) dec_inst)->storage));
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
      if(dec_info.output_format == H264DEC_TILED_YUV420)
        DEBUG_PRINT(("Output format = H264DEC_TILED_YUV420\n"));
      else if(dec_info.output_format == H264DEC_YUV400)
        DEBUG_PRINT(("Output format = H264DEC_YUV400\n"));
      else
        DEBUG_PRINT(("Output format = H264DEC_SEMIPLANAR_YUV420\n"));
      H264DecConfig dec_cfg;
      dec_cfg.dpb_flags = 0;
      if( tiled_output )   dec_cfg.dpb_flags |= DEC_REF_FRM_TILED_DEFAULT;
    if( dpb_mode == DEC_DPB_INTERLACED_FIELD )
      dec_cfg.dpb_flags  |= DEC_DPB_ALLOW_FIELD_ORDERING;
#ifdef USE_EXTERNAL_BUFFER
      dec_cfg.use_adaptive_buffers = 0;
      dec_cfg.guard_size = 0;
#endif
      dec_cfg.use_secure_mode = 0;
      tmp = H264DecSetInfo(dec_inst, &dec_cfg);
      if (tmp != H264DEC_OK) {
        DEBUG_PRINT(("Invalid pp parameters\n"));
        goto end;
      }
#ifdef USE_EXTERNAL_BUFFER
      if((dec_info.pic_buff_size != min_buffer_num) ||
          (dec_info.pic_width * dec_info.pic_height != prev_width * prev_height)) {
        /* Reset buffers added and stop adding extra buffers when a new header comes. */
        if(pp_enabled)
          res_changed = 1;
        else {
          add_extra_flag = 0;
          ReleaseExtBuffers();
          buffer_release_flag = 1;
          num_buffers = 0;
        }
      }
      prev_width = dec_info.pic_width;
      prev_height = dec_info.pic_height;
      min_buffer_num = dec_info.pic_buff_size;
#endif
      dpb_mode = dec_info.dpb_mode;
#ifdef PP_PIPELINE_ENABLED
      pp_change_resolution( ((dec_info.pic_width+7) & ~7), ((dec_info.pic_height+7) & ~7), &tb_cfg);
      pp_number_of_buffers(dec_info.multi_buff_pp_size);
#endif
      /* release the old temp image buffer if exists */
      if(tmp_image)
        free(tmp_image);

      if(crop_display) {
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
        /* Decoder output frame size in planar YUV 4:2:0 */
        pic_size = dec_info.pic_width * dec_info.pic_height;
        if(!dec_info.mono_chrome)
          pic_size = (3 * pic_size) / 2;
        if (use_peek_output)
          tmp_image = malloc(pic_size);
      }

      DEBUG_PRINT(("video_range %d, matrix_coefficients %d\n",
                   dec_info.video_range, dec_info.matrix_coefficients));

      /* If -O option not used, generate default file name */
      if(out_file_name[0] == 0)
        sprintf(out_file_name, "out.yuv");

      break;
    }
    case H264DEC_ADVANCED_TOOLS: {
      /* ASO/STREAM ERROR was noticed in the stream. The decoder has to
       * reallocate resources */
      assert(dec_output.data_left); /* we should have some data left *//* Used to indicate that picture decoding needs to finalized prior to corrupting next picture */

      /* Used to indicate that picture decoding needs to finalized prior to corrupting next picture
       * pic_rdy = 0; */
      break;
    }
    case H264DEC_PENDING_FLUSH:
      eos = 1;
    case H264DEC_PIC_DECODED:
      /* case H264DEC_FREEZED_PIC_RDY: */
      /* Picture is now ready */
      pic_rdy = 1;
#if 0
      if (pic_decode_number == 10) {
        tmp = H264DecAbort(dec_inst);
        tmp = H264DecAbortAfter(dec_inst);
        rewind(finput);
        dec_input.stream_bus_address = (addr_t) stream_mem.bus_address;
        dec_input.stream = (u8 *) stream_mem.virtual_address;
        dec_input.data_len = strm_len;
        dec_output.strm_curr_pos = dec_input.stream;
        dec_output.data_left =  dec_input.data_len;
      }
#endif

      /*lint -esym(644,tmp_image,pic_size) variable initialized at
       * H264DEC_HDRS_RDY_BUFF_NOT_EMPTY case */

      /* If enough pictures decoded -> force decoding to end
       * by setting that no more stream is available */
      if(pic_decode_number == max_num_pics)
        dec_input.data_len = 0;

      printf("DECODED PICTURE %d\n", pic_decode_number);
      /* Increment decoding number for every decoded picture */
      pic_decode_number++;

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
      if(!output_thread_run) {
        output_thread_run = 1;
        sem_init(&buf_release_sem, 0, 0);
        pthread_create(&output_thread, NULL, h264_output_thread, NULL);
        pthread_create(&release_thread, NULL, buf_release_thread, NULL);
      }
#endif
      if(use_peek_output && ret == H264DEC_PIC_DECODED &&
          H264DecPeek(dec_inst, &dec_picture) == H264DEC_PIC_RDY) {
        static u32 last_field = 0;

        /* pic coding type */
        printH264PicCodingType(dec_picture.pic_coding_type);

        DEBUG_PRINT((", DECPIC %d\n", pic_decode_number));

        /* Write output picture to file. If output consists of separate fields -> store
         * whole frame before writing */
        image_data = (u8 *) dec_picture.output_picture;
        if (dec_picture.field_picture) {
          if (last_field == 0)
            memcpy(tmp_image, image_data, pic_size);
          else {
            u32 i;
            u8 *p_in = image_data, *p_out = tmp_image;
            if (dec_picture.top_field == 0) {
              p_out += dec_picture.pic_width;
              p_in += dec_picture.pic_width;
            }
            tmp = dec_info.mono_chrome ?
                  dec_picture.pic_height / 2 :
                  3 * dec_picture.pic_height / 4;
            for (i = 0; i < tmp; i++) {
              memcpy(p_out, p_in, dec_picture.pic_width);
              p_in += 2*dec_picture.pic_width;
              p_out += 2*dec_picture.pic_width;
            }
          }
          last_field ^= 1;
        } else {
          if (last_field)
            WriteOutput(out_file_name, out_file_name_tiled,
                        tmp_image, pic_size,
                        dec_info.pic_width,
                        dec_info.pic_height,
                        pic_decode_number - 2, dec_info.mono_chrome, 0,
                        dec_picture.output_format);
          last_field = 0;
          memcpy(tmp_image, image_data, pic_size);
        }

        if (!last_field)
          WriteOutput(out_file_name, out_file_name_tiled,
                      tmp_image, pic_size,
                      dec_info.pic_width,
                      dec_info.pic_height,
                      pic_decode_number - 1, dec_info.mono_chrome, 0,
                      dec_picture.output_format);

      }

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
      /* use function H264DecNextPicture() to obtain next picture
       * in display order. Function is called until no more images
       * are ready for display */
      START_SW_PERFORMANCE;
      while (H264DecNextPicture(dec_inst, &dec_picture, eos) ==
             H264DEC_PIC_RDY) {
        END_SW_PERFORMANCE;
        if (!use_peek_output) {
          DEBUG_PRINT(("PIC %d, view %d, type [%s:%s], ", pic_display_number,
                       dec_picture.view_id,
                       dec_picture.is_idr_picture[0] ? "IDR" : "NON-IDR",
                       dec_picture.is_idr_picture[1] ? "IDR" : "NON-IDR"));

          /* pic coding type */
          printH264PicCodingType(dec_picture.pic_coding_type);

          if(pic_display_number != dec_picture.pic_id)
            DEBUG_PRINT((", decoded pic %d", dec_picture.pic_id));

          if(dec_picture.nbr_of_err_mbs) {
            DEBUG_PRINT((", concealed %d", dec_picture.nbr_of_err_mbs));
          }

          if(dec_picture.interlaced) {
            DEBUG_PRINT((", INTERLACED "));
            if(dec_picture.field_picture) {
              DEBUG_PRINT(("FIELD %s",
                           dec_picture.top_field ? "TOP" : "BOTTOM"));

              SetMissingField2Const((u8*)dec_picture.output_picture,
                                    dec_info.pic_width,
                                    dec_info.pic_height,
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

          num_errors += dec_picture.nbr_of_err_mbs;

          /*lint -esym(644,dec_info) always initialized if pictures
           * available for display */

          /* Write output picture to file */
          image_data = (u8 *) dec_picture.output_picture;
          if(crop_display) {
            tmp = CropPicture(tmp_image, image_data,
                              dec_picture.pic_width,
                              dec_picture.pic_height,
                              &dec_picture.crop_params,
                              dec_info.mono_chrome);
            if(tmp) {
              DEBUG_PRINT(("ERROR in cropping!\n"));
              goto end;
            }
          }
#ifndef PP_PIPELINE_ENABLED
          WriteOutput(out_file_name, out_file_name_tiled,
                      crop_display ? tmp_image : image_data,
                      pic_size,
                      crop_display ? dec_picture.crop_params.crop_out_width : dec_picture.pic_width,
                      crop_display ? dec_picture.crop_params.crop_out_height : dec_picture.pic_height,
                      pic_display_number - 1,
                      dec_info.mono_chrome,
                      dec_picture.view_id,
                      dec_picture.output_format);
#else
          if(!disable_output_writing) {
            HandlePpOutput(pic_display_number, dec_picture.view_id);
          }
          pp_check_combined_status();
          pp_read_blend_components(((decContainer_t *) dec_inst)->pp.pp_instance);
          /* load new cfg if needed (handled in pp testbench) */
          if(pp_update_config(dec_inst, PP_PIPELINED_DEC_TYPE_H264, &tb_cfg) == CFG_UPDATE_FAIL) {
            fprintf(stdout, "PP CONFIG LOAD FAILED\n");
          }

#endif
          /* Increment display number for every displayed picture */
          pic_display_number++;
        }

        START_SW_PERFORMANCE;

        if (use_display_smoothing && ret != H264DEC_PENDING_FLUSH)
          break;
      }
      END_SW_PERFORMANCE;
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
    case H264DEC_BUF_EMPTY:
    case H264DEC_NONREF_PIC_SKIPPED:
    case H264DEC_STRM_ERROR:
    case H264DEC_NO_DECODING_BUFFER: {
      /* Used to indicate that picture decoding needs to finalized prior to corrupting next picture
       * pic_rdy = 0; */

      break;
    }
#ifdef USE_EXTERNAL_BUFFER
    case H264DEC_WAITING_FOR_BUFFER: {
      DEBUG_PRINT(("Waiting for frame buffers\n"));
      struct DWLLinearMem mem;

      rv = H264DecGetBufferInfo(dec_inst, &hbuf);
      DEBUG_PRINT(("H264DecGetBufferInfo ret %d\n", rv));
      DEBUG_PRINT(("buf_to_free %p, next_buf_size %d, buf_num %d\n",
                   (void *)hbuf.buf_to_free.virtual_address, hbuf.next_buf_size, hbuf.buf_num));

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
        //extra_buffer_num = hbuf.buf_num - min_buffer_num;
        for(i=0; i<hbuf.buf_num; i++) {
          mem.mem_type = DWL_MEM_TYPE_DPB;
          if (pp_enabled)
            DWLMallocLinear(dwl_inst, hbuf.next_buf_size, &mem);
          else
            DWLMallocRefFrm(dwl_inst, hbuf.next_buf_size, &mem);
          rv = H264DecAddBuffer(dec_inst, &mem);
          DEBUG_PRINT(("H264DecAddBuffer ret %d\n", rv));
          if(rv != H264DEC_OK && rv != H264DEC_WAITING_FOR_BUFFER) {
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
    }
#endif

    case H264DEC_OK:
      /* nothing to do, just call again */
      break;
    case H264DEC_HW_TIMEOUT:
      DEBUG_PRINT(("Timeout\n"));
      goto end;
    default:
      DEBUG_PRINT(("FATAL ERROR: %d\n", ret));
      goto end;
    }

    /* break out of do-while if max_num_pics reached (data_len set to 0) */
    if(dec_input.data_len == 0)
      break;

    if(long_stream && !packetize && !nal_unit_stream) {
      if(stream_will_end) {
        corrupted_bytes -= (dec_input.data_len - dec_output.data_left);
        dec_input.data_len = dec_output.data_left;
        dec_input.stream = dec_output.strm_curr_pos;
        dec_input.stream_bus_address = dec_output.strm_curr_bus_address;
      } else {
        if(use_index == 1) {
          if(dec_output.data_left) {
            dec_input.stream_bus_address += (dec_output.strm_curr_pos - dec_input.stream);
            corrupted_bytes -= (dec_input.data_len - dec_output.data_left);
            dec_input.data_len = dec_output.data_left;
            dec_input.stream = dec_output.strm_curr_pos;
          } else {
            dec_input.stream_bus_address = (addr_t) stream_mem.bus_address;
            dec_input.stream = (u8 *) stream_mem.virtual_address;
            dec_input.data_len = fillBuffer(dec_input.stream);
          }
        } else {
          if(fseek(finput, -(i32)dec_output.data_left, SEEK_CUR) == -1) {
            DEBUG_PRINT(("SEEK FAILED\n"));
            dec_input.data_len = 0;
          } else {
            /* store file index */
            if(save_index && save_flag) {
#ifdef USE_64BIT_ENV
              fprintf(findex, "%lu\n", ftello64(finput));
#else
              fprintf(findex, "%llu\n", ftello64(finput));
#endif
            }

            dec_input.data_len =
              fread((u8 *)dec_input.stream, 1, DEC_X170_MAX_STREAM, finput);
          }
        }

        if(feof(finput)) {
          DEBUG_PRINT(("STREAM WILL END\n"));
          stream_will_end = 1;
        }

        corrupted_bytes = 0;
      }
    }

    else {
      if(dec_output.data_left) {
        dec_input.stream_bus_address += (dec_output.strm_curr_pos - dec_input.stream);
        corrupted_bytes -= (dec_input.data_len - dec_output.data_left);
        dec_input.data_len = dec_output.data_left;
        dec_input.stream = dec_output.strm_curr_pos;
      } else {
        dec_input.stream_bus_address = (addr_t) stream_mem.bus_address;
        dec_input.stream = (u8 *) stream_mem.virtual_address;
        /*u32 streamPacketLossTmp = stream_packet_loss;
         *
         * if(!pic_rdy)
         * {
         * stream_packet_loss = 0;
         * } */
        ptmpstream = dec_input.stream;

        tmp = ftell(finput);
        dec_input.data_len = NextPacket((u8 **) (&dec_input.stream));
        printf("NextPacket = %d at %d\n", dec_input.data_len, tmp);

        dec_input.stream_bus_address +=
          (addr_t) (dec_input.stream - ptmpstream);

        /*stream_packet_loss = streamPacketLossTmp; */

        corrupted_bytes = 0;
      }
    }

    /* keep decoding until all data from input stream buffer consumed
     * and all the decoded/queued frames are ready */
  } while(dec_input.data_len > 0);

  /* store last index */
  if(save_index) {
    off64_t pos = ftello64(finput) - dec_output.data_left;
#ifdef USE_64BIT_ENV
    fprintf(findex, "%lu\n", pos);
#else
    fprintf(findex, "%llu\n", pos);
#endif
  }

  if(use_index || save_index) {
    fclose(findex);
  }

  DEBUG_PRINT(("Decoding ended, flush the DPB\n"));

#if defined(PP_PIPELINE_ENABLED) || !defined(USE_OUTPUT_RELEASE)
  /* if output in display order is preferred, the decoder shall be forced
   * to output pictures remaining in decoded picture buffer. Use function
   * H264DecNextPicture() to obtain next picture in display order. Function
   * is called until no more images are ready for display. Second parameter
   * for the function is set to '1' to indicate that this is end of the
   * stream and all pictures shall be output */
  while(H264DecNextPicture(dec_inst, &dec_picture, 1) == H264DEC_PIC_RDY) {
    if(use_peek_output) {
      /* we peeked all pictures so here we just flush the output */
      continue;
    }

    DEBUG_PRINT(("PIC %d, type [%s:%s], ", pic_display_number,
                 dec_picture.is_idr_picture[0] ? "IDR" : "NON-IDR",
                 dec_picture.is_idr_picture[1] ? "IDR" : "NON-IDR"));

    /* pic coding type */
    printH264PicCodingType(dec_picture.pic_coding_type);

    if(pic_display_number != dec_picture.pic_id)
      DEBUG_PRINT((", decoded pic %d", dec_picture.pic_id));
    if(dec_picture.nbr_of_err_mbs) {
      DEBUG_PRINT((", concealed %d\n", dec_picture.nbr_of_err_mbs));
    }
    if(dec_picture.interlaced) {
      DEBUG_PRINT((", INTERLACED "));
      if(dec_picture.field_picture) {
        DEBUG_PRINT(("FIELD %s",
                     dec_picture.top_field ? "TOP" : "BOTTOM"));

        SetMissingField2Const((u8*)dec_picture.output_picture,
                              dec_info.pic_width,
                              dec_info.pic_height,
                              dec_info.mono_chrome,
                              !dec_picture.top_field);
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

    num_errors += dec_picture.nbr_of_err_mbs;

    /* Write output picture to file */
    image_data = (u8 *) dec_picture.output_picture;
    if(crop_display) {
      tmp = CropPicture(tmp_image, image_data,
                        dec_info.pic_width, dec_info.pic_height,
                        &dec_picture.crop_params, dec_info.mono_chrome);
      if(tmp) {
        DEBUG_PRINT(("ERROR in cropping!\n"));
        goto end;
      }
    }

#ifndef PP_PIPELINE_ENABLED
    WriteOutput(out_file_name, out_file_name_tiled,
                crop_display ? tmp_image : image_data, pic_size,
                crop_display ? dec_picture.crop_params.crop_out_width : dec_picture.pic_width,
                crop_display ? dec_picture.crop_params.crop_out_height : dec_picture.pic_height,
                pic_display_number - 1, dec_info.mono_chrome,
                dec_picture.view_id,
                dec_picture.output_format);
#else
    if(!disable_output_writing) {
      HandlePpOutput(pic_display_number, dec_picture.view_id);
    }

    pp_read_blend_components(((decContainer_t *) dec_inst)->pp.pp_instance);
    pp_check_combined_status();
    /* load new cfg if needed (handled in pp testbench) */
    if(pp_update_config(dec_inst, PP_PIPELINED_DEC_TYPE_H264, &tb_cfg) == CFG_UPDATE_FAIL) {
      fprintf(stdout, "PP CONFIG LOAD FAILED\n");
    }

#endif
    /* Increment display number for every displayed picture */
    pic_display_number++;
  }
#endif

end:

#ifdef USE_EXTERNAL_BUFFER
  add_buffer_thread_run = 0;
#endif

#if !defined(PP_PIPELINE_ENABLED) && defined(USE_OUTPUT_RELEASE)
  H264DecEndOfStream(dec_inst, 1);

  if(output_thread)
    pthread_join(output_thread, NULL);
  if(release_thread)
    pthread_join(release_thread, NULL);
#endif

  byte_strm_start = NULL;
  fflush(stdout);

  if(stream_mem.virtual_address != NULL) {
#ifndef ADS_PERFORMANCE_SIMULATION
    if(dec_inst)
#ifdef USE_EXTERNAL_BUFFER
      DWLFreeLinear(dwl_inst, &stream_mem);
#else
      DWLFreeLinear(((decContainer_t *) dec_inst)->dwl, &stream_mem);
#endif
#else
    free(stream_mem.virtual_address);
#endif
  }


#ifdef PP_PIPELINE_ENABLED
  pp_close();
#endif

  /* release decoder instance */
  START_SW_PERFORMANCE;
  H264DecRelease(dec_inst);
  END_SW_PERFORMANCE;
#ifdef USE_EXTERNAL_BUFFER
  ReleaseExtBuffers();
  pthread_mutex_destroy(&ext_buffer_contro);
  DWLRelease(dwl_inst);
#endif

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

  DEBUG_PRINT(("OUTPUT_SIZE %ld\n", strm_len));

  FINALIZE_SW_PERFORMANCE;

  DEBUG_PRINT(("DECODING DONE\n"));

#ifdef ASIC_TRACE_SUPPORT
  trace_SequenceCtrl(hw_dec_pic_count, 0);
  trace_RefbufferHitrate();
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
void WriteOutput(char *filename, char *filename_tiled, u8 * data, u32 pic_size,
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
      fn = filename;

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

  if (md5sum) {
    TBWriteFrameMD5Sum(*fout, data, pic_size, frame_number);

    /* tiled */
#if 0
    if(tiled_output && !mono_chrome) { /* TODO: does not work for monochrome output */
      assert(pic_width % 16 == 0);
      assert(pic_height % 16 == 0);
      TbWriteTiledOutput(f_tiled_output, data, pic_width >> 4,
                         pic_height >> 4, frame_number,
                         1 /* write md5sum */ ,
                         1 /* semi-planar data */ );
    }
#endif
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

    /* tiled */
#if 0
    if(tiled_output && !mono_chrome) { /* TODO: does not work for monochrome output */
      assert(pic_width % 16 == 0);
      assert(pic_height % 16 == 0);
      TbWriteTiledOutput(f_tiled_output, data, pic_width >> 4,
                         pic_height >> 4, frame_number,
                         0 /* do not write md5sum */ ,
                         1 /* semi-planar data */ );
    }
#endif
  }

#ifdef _ENABLE_2ND_CHROMA
  if (TBGetDecCh8PixIleavOutput(&tb_cfg) && !mono_chrome) {
    u8 *p_ch;
    addr_t tmp;
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

    if (md5sum)
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

    Purpose:
        Get the pointer to start of next packet in input stream. Uses
        global variables 'packetize' and 'nal_unit_stream' to determine the
        decoder input stream mode and 'stream_stop' to determine the end
        of stream. There are three possible stream modes:
            default - the whole stream at once
            packetize - a single NAL-unit with start code prefix
            nal_unit_stream - a single NAL-unit without start code prefix

        p_strm stores pointer to the start of previous decoder input and is
        replaced with pointer to the start of the next decoder input.

        Returns the packet size in bytes

------------------------------------------------------------------------------*/

u32 NextPacket(u8 ** p_strm) {
  u32 index;
  u32 max_index;
  u32 zero_count;
  u8 byte;
  static u32 prev_index = 0;
  static u8 *stream = NULL;
  u8 next_packet = 0;

  /* Next packet is read from file is long stream support is enabled */
  if(long_stream) {
    return NextPacketFromFile(p_strm);
  }

  /* For default stream mode all the stream is in first packet */
  if(!packetize && !nal_unit_stream)
    return 0;

  /* If enabled, loose the packets (skip this packet first though) */
  if(stream_packet_loss) {
    u32 ret = 0;

    ret =
      TBRandomizePacketLoss(tb_cfg.tb_params.stream_packet_loss, &next_packet);
    if(ret != 0) {
      DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
      return 0;
    }
  }

  index = 0;

  if(stream == NULL)
    stream = *p_strm;
  else
    stream += prev_index;

  max_index = (u32) (stream_stop - stream);

  if(stream > stream_stop)
    return (0);

  if(max_index == 0)
    return (0);

  /* leading zeros of first NAL unit */
  do {
    byte = stream[index++];
  } while(byte != 1 && index < max_index);

  /* invalid start code prefix */
  if(index == max_index || index < 3) {
    DEBUG_PRINT(("INVALID BYTE STREAM\n"));
    return 0;
  }

  /* nal_unit_stream is without start code prefix */
  if(nal_unit_stream) {
    stream += index;
    max_index -= index;
    index = 0;
  }

  zero_count = 0;

  /* Search stream for next start code prefix */
  /*lint -e(716) while(1) used consciously */
  while(1) {
    byte = stream[index++];
    if(!byte)
      zero_count++;

    if((byte == 0x01) && (zero_count >= 2)) {
      /* Start code prefix has two zeros
       * Third zero is assumed to be leading zero of next packet
       * Fourth and more zeros are assumed to be trailing zeros of this
       * packet */
      if(zero_count > 3) {
        index -= 4;
        zero_count -= 3;
      } else {
        index -= zero_count + 1;
        zero_count = 0;
      }
      break;
    } else if(byte)
      zero_count = 0;

    if(index == max_index) {
      break;
    }

  }

  /* Store pointer to the beginning of the packet */
  *p_strm = stream;
  prev_index = index;

  /* If we skip this packet */
  if(pic_rdy && next_packet &&
      ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt)) {
    /* Get the next packet */
    DEBUG_PRINT(("Packet Loss\n"));
    return NextPacket(p_strm);
  } else {
    /* nal_unit_stream is without trailing zeros */
    if(nal_unit_stream)
      index -= zero_count;
    /*DEBUG_PRINT(("No Packet Loss\n")); */
    /*if (pic_rdy && stream_truncate && ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt))
     * {
     * i32 ret;
     * DEBUG_PRINT(("Original packet size %d\n", index));
     * ret = TBRandomizeU32(&index);
     * if(ret != 0)
     * {
     * DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
     * return 0;
     * }
     * DEBUG_PRINT(("Randomized packet size %d\n", index));
     * } */
    return (index);
  }
}

/*------------------------------------------------------------------------------

    Function name: NextPacketFromFile

    Purpose:
        Get the pointer to start of next NAL-unit in input stream. Uses global
        variables 'finput' to read the stream and 'packetize' to determine the
        decoder input stream mode.

        nal_unit_stream a single NAL-unit without start code prefix. If not
        enabled, a single NAL-unit with start code prefix

        p_strm stores pointer to the start of previous decoder input and is
        replaced with pointer to the start of the next decoder input.

        Returns the packet size in bytes

------------------------------------------------------------------------------*/
u32 NextPacketFromFile(u8 ** p_strm) {

  u32 index = 0;
  u32 zero_count;
  u8 byte;
  u8 next_packet = 0;
  i32 ret = 0;
  u8 first_read = 1;
  fpos_t strm_pos;
  static u8 *stream = NULL;

  /* store the buffer start address for later use */
  if(stream == NULL)
    stream = *p_strm;
  else
    *p_strm = stream;

  /* If enabled, loose the packets (skip this packet first though) */
  if(stream_packet_loss) {
    u32 ret = 0;

    ret =
      TBRandomizePacketLoss(tb_cfg.tb_params.stream_packet_loss, &next_packet);
    if(ret != 0) {
      DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
      return 0;
    }
  }

  if(fgetpos(finput, &strm_pos)) {
    DEBUG_PRINT(("FILE POSITION GET ERROR\n"));
    return 0;
  }

  if(use_index == 0) {
    /* test end of stream */
    ret = fread(&byte, 1, 1, finput);
    if(ferror(finput)) {
      DEBUG_PRINT(("STREAM READ ERROR\n"));
      return 0;
    }
    if(feof(finput)) {
      DEBUG_PRINT(("END OF STREAM\n"));
      return 0;
    }

    /* leading zeros of first NAL unit */
    do {
      index++;
      /* the byte is already read to test the end of stream */
      if(!first_read) {
        ret = fread(&byte, 1, 1, finput);
        if(ferror(finput)) {
          DEBUG_PRINT(("STREAM READ ERROR\n"));
          return 0;
        }
      } else {
        first_read = 0;
      }
    } while(byte != 1 && !feof(finput));

    /* invalid start code prefix */
    if(feof(finput) || index < 3) {
      DEBUG_PRINT(("INVALID BYTE STREAM\n"));
      return 0;
    }

    /* nal_unit_stream is without start code prefix */
    if(nal_unit_stream) {
      if(fgetpos(finput, &strm_pos)) {
        DEBUG_PRINT(("FILE POSITION GET ERROR\n"));
        return 0;
      }
      index = 0;
    }

    zero_count = 0;

    /* Search stream for next start code prefix */
    /*lint -e(716) while(1) used consciously */
    while(1) {
      /*byte = stream[index++]; */
      index++;
      ret = fread(&byte, 1, 1, finput);
      if(ferror(finput)) {
        DEBUG_PRINT(("FILE ERROR\n"));
        return 0;
      }
      if(!byte)
        zero_count++;

      if((byte == 0x01) && (zero_count >= 2)) {
        /* Start code prefix has two zeros
         * Third zero is assumed to be leading zero of next packet
         * Fourth and more zeros are assumed to be trailing zeros of this
         * packet */
        if(zero_count > 3) {
          index -= 4;
          zero_count -= 3;
        } else {
          index -= zero_count + 1;
          zero_count = 0;
        }
        break;
      } else if(byte)
        zero_count = 0;

      if(feof(finput)) {
        --index;
        break;
      }
    }

    /* Store pointer to the beginning of the packet */
    if(fsetpos(finput, &strm_pos)) {
      DEBUG_PRINT(("FILE POSITION SET ERROR\n"));
      return 0;
    }

    if(save_index) {
#ifdef USE_64BIT_ENV
      fprintf(findex, "%lu\n", strm_pos.__pos);
#else
      fprintf(findex, "%llu\n", strm_pos.__pos);
#endif
      if(nal_unit_stream) {
        /* store amount */
        fprintf(findex, "%u\n", index);
      }
    }

    /* Read the rewind stream */
    ret = fread(*p_strm, 1, index, finput);
    if(feof(finput)) {
      DEBUG_PRINT(("TRYING TO READ STREAM BEYOND STREAM END\n"));
      return 0;
    }
    if(ferror(finput)) {
      DEBUG_PRINT(("FILE ERROR\n"));
      return 0;
    }
  } else {
    u32 amount = 0;
    u32 f_pos = 0;

    if(nal_unit_stream)
#ifdef USE_64BIT_ENV
      ret = fscanf(findex, "%lu", &cur_index);
#else
      ret = fscanf(findex, "%llu", &cur_index);
#endif

    /* check position */
    f_pos = ftell(finput);
    if(f_pos != cur_index) {
      fseeko64(finput, cur_index - f_pos, SEEK_CUR);
    }

    if(nal_unit_stream) {
      ret = fscanf(findex, "%u", &amount);
    } else {
#ifdef USE_64BIT_ENV
      ret = fscanf(findex, "%lu", &next_index);
#else
      ret = fscanf(findex, "%llu", &next_index);
#endif
      amount = next_index - cur_index;
      cur_index = next_index;
    }

    ret = fread(*p_strm, 1, amount, finput);
    index = amount;
  }
  /* If we skip this packet */
  if(pic_rdy && next_packet &&
      ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt)) {
    /* Get the next packet */
    DEBUG_PRINT(("Packet Loss\n"));
    return NextPacket(p_strm);
  } else {
    /*DEBUG_PRINT(("No Packet Loss\n")); */
    /*if (pic_rdy && stream_truncate && ((hdrs_rdy && !stream_header_corrupt) || stream_header_corrupt))
     * {
     * i32 ret;
     * DEBUG_PRINT(("Original packet size %d\n", index));
     * ret = TBRandomizeU32(&index);
     * if(ret != 0)
     * {
     * DEBUG_PRINT(("RANDOM STREAM ERROR FAILED\n"));
     * return 0;
     * }
     * DEBUG_PRINT(("Randomized packet size %d\n", index));
     * } */
    return (index);
  }
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

  DEBUG_PRINT(("TB: H264DecDecode returned: "));
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
  case H264DEC_BUF_EMPTY:
    DEBUG_PRINT(("H264DEC_BUF_EMPTY\n"));
    break;
  case H264DEC_NO_DECODING_BUFFER:
    DEBUG_PRINT(("H264DEC_NO_DECODING_BUFFER\n"));
    break;
  case H264DEC_PIC_RDY:
    DEBUG_PRINT(("H264DEC_PIC_RDY\n"));
    break;
  case H264DEC_PIC_DECODED:
    DEBUG_PRINT(("H264DEC_PIC_DECODED\n"));
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
  case H264DEC_HW_TIMEOUT:
    DEBUG_PRINT(("H264DEC_HW_TIMEOUT\n"));
    break;
  case H264DEC_PENDING_FLUSH:
    DEBUG_PRINT(("H264DEC_PENDING_FLUSH\n"));
    break;
  default:
    DEBUG_PRINT(("Other %d\n", retval));
    break;
  }
}

/*------------------------------------------------------------------------------

    Function name:            printH264PicCodingType

    Functional description:   Print out picture coding type value

------------------------------------------------------------------------------*/
void printH264PicCodingType(u32 *pic_type) {
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

u32 fillBuffer(const u8 *stream) {
  u32 amount = 0;
  u32 data_len = 0;
  int ret;
  if(cur_index != ftell(finput)) {
    fseeko64(finput, cur_index, SEEK_SET);
  }

  /* read next index */
#ifdef USE_64BIT_ENV
  ret = fscanf(findex, "%lu", &next_index);
#else
  ret = fscanf(findex, "%llu", &next_index);
#endif
  amount = next_index - cur_index;
  cur_index = next_index;

  /* read data */
  data_len = fread((u8 *)stream, 1, amount, finput);

  return data_len;
}

#ifdef PP_PIPELINE_ENABLED
void HandlePpOutput(u32 pic_display_number, u32 view_id) {

  static char catcmd0[100];
  static char catcmd1[100];
  static char rmcmd[100];
  const char *fname;

  pp_write_output(pic_display_number - 1, 0, 0);

  if (enable_mvc && mvc_separate_views) {
    if (pic_display_number == 1) {
      /* determine output file names for base and stereo views */
      char out_name[100];
      char ext[10];
      u32 idx;
      fname = pp_get_out_name();
      strcpy(out_name, fname);
      idx = strlen(out_name);
      while(idx && out_name[idx] != '.') idx--;
      strcpy(ext, out_name + idx);
      sprintf(out_name + idx, "_0%s", ext);
      sprintf(rmcmd, "rm -f %s", out_name);
      system(rmcmd);
      sprintf(catcmd0, "cat %s >> %s", fname, out_name);
      strcpy(out_name, fname);
      sprintf(out_name + idx, "_1%s", ext);
      sprintf(rmcmd, "rm -f %s", out_name);
      system(rmcmd);
      sprintf(catcmd1, "cat %s >> %s", fname, out_name);
      sprintf(rmcmd,   "rm %s", fname);
    }

    if (view_id == 0)
      system(catcmd0);
    else
      system(catcmd1);

    system(rmcmd);
  }

}
#endif
