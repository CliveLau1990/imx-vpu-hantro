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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "basetype.h"
#include "dwl.h"
#include "dwlthread.h"
#include "fifo.h"
#include "regdrv_g1.h"
#include "tb_cfg.h"
#include "testparams.h"
#include "vp8bufferalloc.h"
#include "vp8decapi.h"
#include "vp8filereader.h"
#include "vp8hwd_container.h"
#include "vp8writeoutput.h"

#define DEFAULT_FIFO_CAPACITY   (MAX_ASIC_CORES+1)
#define DEFAULT_OUTPUT_FILENAME "out.yuv"
#define VP8_MAX_STREAM_SIZE     DEC_X170_MAX_STREAM>>4
/* Multicore decoding functionality */
/* States to control the test bench */
typedef enum test_state_ {
  STATE_DOWN = 0,      /* Nothing running, no resources allocated. */
  STATE_INITIALIZING,  /* Allocating resources in input thread. */
  STATE_STREAMING,     /* Input reading&decoding, output handling pictures. */
  STATE_END_OF_STREAM, /* Input waiting, outputs still pending. */
  STATE_TEARING_DOWN,  /* Input waiting, in middle of tear-down process. */
  STATE_OUTPUT_DOWN    /* Output thread has been torn down. */
} test_state;

/* Generic function declarations. */
typedef i32 read_stream_func(VP8DecInput*);   /* Input reading. */
typedef i32 write_pic_func(VP8DecPicture);    /* Output writing. */

typedef struct shared_data_ {
  FifoInst input_fifo_;    /* Handle to the fifo instance. */
  VP8DecInst dec_inst_;     /* Decoder instance. */
  void *dwl;                /* DWL instance. */
  pthread_t output_thread_; /* Handle to the output thread. */
  test_state state_;        /* State of the test bench */
  u8 stream_buffer_refresh; /* Flag to tell whether to refresh strmbuffer. */
  VP8DecInput dec_input_;   /* Decoder input. */
  i32 output_status_;       /* Status value for output thread */
  write_pic_func* write_pic_; /* Function pointer to output writing. */
} shared_data;

typedef struct decoder_cb_data_ {
  FifoInst input_fifo_;      /* Handle to the fifo instance. */
  struct DWLLinearMem stream_mem_; /* Information about the linear mem. */
} decoder_cb_data;

typedef i32 buffer_alloc_func(shared_data*);          /* User buffer allocation */
typedef void buffer_free_func(shared_data*);          /* User buffer free */

typedef struct decoder_utils_ {
  read_stream_func* read_stream;
  write_pic_func* write_pic;
  buffer_alloc_func* alloc_buffers;
  buffer_free_func* free_buffers;
} decoder_utils;

/* Input thread functionality. */
static int vp8_multicore_decode(decoder_utils* decoder_utils); /* Input loop. */
static int input_on_initializing(shared_data* shared_data);
static int input_on_streaming(shared_data* shared_data,
                              decoder_utils* decoder_utils);
static int input_on_end_of_stream(shared_data* shared_data);
static int input_on_output_down(shared_data* shared_data,
                                decoder_utils* decoder_utils);
static void vp8_stream_consumed_cb(u8* consumed_stream_buffer,
                                   void* user_data); /* Callback from dec. */

/* Output thread functionality. */
static void* vp8_output_thread(void* arg); /* Output loop. */

/* I/O functionality required by tests. */
i32 alloc_user_buffers(shared_data* shared_data); /* Allocate user buffers */
void free_user_buffers(shared_data* shared_data);
i32 alloc_user_null(shared_data* shared_data);
void free_user_null(shared_data* shared_data);
i32 read_vp8_stream(VP8DecInput* input); /* Input reading. */
i32 write_pic_null(VP8DecPicture pic); /* Output writing. */
i32 write_pic(VP8DecPicture pic); /* Output writing. */

/* Parameter helpers. */
static void print_header(void);
static void print_usage(char* executable);
static void setup_default_params(test_params* params);
static void params_resolve_overlapping(struct TBCfg* tbcfg, test_params* params);
static int parse_params(int argc, char* argv[], test_params* params);
static int hwconfig_override(VP8DecInst dec_inst,  struct TBCfg* tbcfg);

/* Helpers to protect the state. */
static test_state get_state(shared_data* data);
static void set_state(shared_data* data, test_state state);

/* Input reader and output writer instance. */
reader_inst g_reader_inst;
output_inst g_output_inst;
useralloc_inst g_useralloc_inst;

/* Hack globals to carry around data for model. */
struct TBCfg tb_cfg;
u32 b_frames=0;

#define DEBUG_PRINT(str) printf str

int main(int argc, char* argv[]) {
  test_params params;
  u32 tmp;
  decoder_utils dec_utils;
  FILE * f_tbcfg;

  print_header();
  setup_default_params(&params);
  if (argc < 2) {
    print_usage(argv[0]);
    return 0;
  }
  if (parse_params(argc, argv, &params)) {
    return 1;
  }

  TBSetDefaultCfg(&tb_cfg); /* Set up the struct TBCfg hack. */
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

  params_resolve_overlapping(&tb_cfg, &params);

  /* Create the reader and output. */
  g_reader_inst = rdr_open(params.in_file_name_);
  if (g_reader_inst == NULL)
    return 1;
  g_output_inst = output_open(params.out_file_name_, &params);
  if (g_output_inst == NULL)
    return 1;

  /* Create user buffer allocator if necessary */
  if (params.user_allocated_buffers_) {
    g_useralloc_inst = useralloc_open(&params);
    if (g_useralloc_inst == NULL)
      return 1;
  }

#ifdef ASIC_TRACE_SUPPORT
  tmp = openTraceFiles();
  if (!tmp) {
    DEBUG_PRINT(("UNABLE TO OPEN TRACE FILE(S)\n"));
  }
#endif

  /* Setup decoder utility functions */
  dec_utils.read_stream = read_vp8_stream;
  dec_utils.write_pic = write_pic;
  if (params.user_allocated_buffers_) {
    dec_utils.alloc_buffers = alloc_user_buffers;
    dec_utils.free_buffers = free_user_buffers;
  } else {
    dec_utils.alloc_buffers = alloc_user_null;
    dec_utils.free_buffers = free_user_null;
  }
  /* Run the input loop. */
  vp8_multicore_decode(&dec_utils);

  /* Close the reader and output. */
  rdr_close(g_reader_inst);
  output_close(g_output_inst);

  if (params.user_allocated_buffers_) {
    useralloc_close(g_useralloc_inst);
  }
  return 0;
}

static int vp8_multicore_decode(decoder_utils* decoder_utils) {
  shared_data shared_data;
  set_state(&shared_data, STATE_INITIALIZING);
  shared_data.write_pic_ = decoder_utils->write_pic;
  /* Start the actual decoding loop. */
  while (1) {
    switch (get_state(&shared_data)) {
    case STATE_INITIALIZING:
      if(input_on_initializing(&shared_data)) {
        /* If init fails do not continue. */
        return 0;
      }
      break;
    case STATE_STREAMING:
      input_on_streaming(&shared_data, decoder_utils);
      break;
    case STATE_END_OF_STREAM:
      input_on_end_of_stream(&shared_data);
      break;
    case STATE_OUTPUT_DOWN:
      input_on_output_down(&shared_data, decoder_utils);
      return 0;
    case STATE_TEARING_DOWN:
    case STATE_DOWN:
    default:
      fprintf(stderr, "Input thread in invalid state.");
      assert(0);
      return -1;
    }
  }
  fprintf(stderr, "ERROR: Something went horribly wrong with input thread.");
  assert(0);
  return -1;
}

static void vp8_stream_consumed_cb(u8* consumed_stream_buffer,
                                   void* user_data) {
  /* This function could be called in signal handler context. */
  assert(user_data);
  decoder_cb_data* callback_data = (decoder_cb_data*)user_data;
  FifoObject stream_buffer = callback_data;
  /* Recycle the input buffer to the input buffer queue. */
  FifoPush(callback_data->input_fifo_, stream_buffer, FIFO_EXCEPTION_DISABLE);
}

static void* vp8_output_thread(void* arg) {
  assert(arg);
  do {
    shared_data* shared_data = arg;
    VP8DecRet rv;
    VP8DecPicture pic;
    switch (get_state(shared_data)) {
    case STATE_STREAMING:
    case STATE_END_OF_STREAM:
      /* Function call blocks until picture is actually ready. */
      rv = VP8DecMCNextPicture(shared_data->dec_inst_, &pic);
      switch (rv) {
      case VP8DEC_OK:
      case VP8DEC_PIC_RDY:
        shared_data->output_status_ =
          shared_data->write_pic_(pic);
        /* Return the picture buffer to the decoder. */
        VP8DecMCPictureConsumed(shared_data->dec_inst_, &pic);
        if (shared_data->output_status_) {
          fprintf(stderr, "ERROR: output writing failed.");
          set_state(shared_data, STATE_END_OF_STREAM);
        }
        break;
      case VP8DEC_END_OF_STREAM:
        set_state(shared_data, STATE_TEARING_DOWN);
        break;
      default:
        /* If this happens, we're already screwed. */
        fprintf(stderr,
                "ERROR: unhandled condition for NextPicture.");
        set_state(shared_data, STATE_OUTPUT_DOWN);
        shared_data->output_status_ = -1;
        break;
      }
      break;

    case STATE_TEARING_DOWN:
      set_state(shared_data, STATE_OUTPUT_DOWN);
      pthread_exit(NULL);
      break;

    case STATE_INITIALIZING:
    case STATE_OUTPUT_DOWN:
    case STATE_DOWN:
    default:
      fprintf(stderr, "Output thread active in invalid state.");
      assert(0);
      pthread_exit(NULL);
      break;
    }
  } while (1);

  fprintf(stderr, "Program error in output thread.");
  assert(0);
  return NULL;
}

i32 alloc_user_buffers(shared_data* shared_data) {
  return useralloc_alloc(g_useralloc_inst, shared_data->dec_inst_, shared_data->dwl);
}
void free_user_buffers(shared_data* shared_data) {
  useralloc_free(g_useralloc_inst, shared_data->dec_inst_, shared_data->dwl);

}

i32 alloc_user_null(shared_data* shared_data) {
  return 0;
}

void free_user_null(shared_data* shared_data) {
  return;
}

i32 read_vp8_stream(VP8DecInput* input) {
  return rdr_read_frame(g_reader_inst, input->stream, VP8_MAX_STREAM_SIZE,
                        &input->data_len, 1);
}

i32 write_pic(VP8DecPicture pic) {
  DEBUG_PRINT(("WRITING PICTURE %d\n", pic.pic_id));

  return output_write_pic(g_output_inst,
                          (unsigned char *) pic.p_output_frame,
                          (unsigned char *) pic.p_output_frame_c,
                          pic.frame_width, pic.frame_height,
                          pic.coded_width, pic.coded_height,
                          0,
                          pic.output_format,
                          pic.luma_stride,
                          pic.chroma_stride,
                          pic.pic_id);

}

i32 write_pic_null(VP8DecPicture pic) {
  return 0;
}

static int input_on_initializing(shared_data* shared_data) {
  i32 i;
  i32 num_of_stream_bufs = DEFAULT_FIFO_CAPACITY;
  VP8DecFormat dec_format;
  VP8DecMCConfig config;
  VP8DecRet rv;

  struct DWLInitParam dwl_init;
  dwl_init.client_type = DWL_CLIENT_TYPE_VP8_DEC;

  shared_data->dwl = (void *)DWLInit(&dwl_init);

  if(shared_data->dwl == NULL) {
    return -1;
  }

  /* Allocate initial stream buffers and put them to fifo queue */
  if (FifoInit(DEFAULT_FIFO_CAPACITY, &shared_data->input_fifo_))
    return -1;

  if(rdr_identify_format(g_reader_inst) != BITSTREAM_VP8) {
    return -1;
  }
  config.stream_consumed_callback = vp8_stream_consumed_cb;
  /* Initialize the decoder in multicore mode. */
  if (VP8DecMCInit(&shared_data->dec_inst_, shared_data->dwl, &config) != VP8DEC_OK) {
    FifoRelease(shared_data->input_fifo_);
    set_state(shared_data, STATE_TEARING_DOWN);
    return -1;
  }

  /* Create the stream buffers and push them into the FIFO. */
  for (i = 0; i < num_of_stream_bufs; i++) {
    FifoObject stream_buffer;
    struct DWLLinearMem stream_mem;
    stream_buffer = malloc(sizeof(decoder_cb_data));
    stream_buffer->input_fifo_ = shared_data->input_fifo_;
    if (DWLMallocLinear(
          ((VP8DecContainer_t *)shared_data->dec_inst_)->dwl,
          VP8_MAX_STREAM_SIZE,
          &stream_buffer->stream_mem_) != DWL_OK) {
      fprintf(stderr,"UNABLE TO ALLOCATE STREAM BUFFER MEM\n");
      set_state(shared_data, STATE_TEARING_DOWN);
      return -1;
    }
    FifoPush(shared_data->input_fifo_, stream_buffer, FIFO_EXCEPTION_DISABLE);
  }

  /* Internal testing feature: Override HW configuration parameters */
  hwconfig_override(shared_data->dec_inst_, &tb_cfg);

  /* Start the output handling thread. */
  shared_data->stream_buffer_refresh = 1;
  set_state(shared_data, STATE_STREAMING);
  pthread_create(&shared_data->output_thread_, NULL, vp8_output_thread,
                 shared_data);
  return 0;
}

static int input_on_streaming(shared_data* shared_data,
                              decoder_utils* decoder_utils) {
  FifoObject stream_buffer = NULL;
  VP8DecRet rv;
  VP8DecOutput output;
  /* If needed, refresh the input stream buffer. */
  if (shared_data->stream_buffer_refresh) {
    /* Fifo pop blocks until available or queue released. */
    FifoPop(shared_data->input_fifo_, &stream_buffer, FIFO_EXCEPTION_DISABLE);
    /* Read data to stream buffer. */
    memset(&shared_data->dec_input_, 0, sizeof(shared_data->dec_input_));
    shared_data->dec_input_.stream =
      (u8*)stream_buffer->stream_mem_.virtual_address;
    shared_data->dec_input_.stream_bus_address =
      stream_buffer->stream_mem_.bus_address;
    /* Trick to recycle buffer and fifo to the callback. */
    shared_data->dec_input_.p_user_data = stream_buffer;
    switch (decoder_utils->read_stream(&shared_data->dec_input_)) {
    case 0:
      shared_data->stream_buffer_refresh = 0;
      break;
    /* TODO(vmr): Remember to check eos and set it. */
    default:
      /* Return the unused buffer. */
      FifoPush(shared_data->input_fifo_, stream_buffer, FIFO_EXCEPTION_DISABLE);
      set_state(shared_data, STATE_END_OF_STREAM);
      return 0;
    }
  }
  /* Decode the contents of the input stream buffer. */
  switch (rv = VP8DecMCDecode(shared_data->dec_inst_, &shared_data->dec_input_,
                              &output)) {
  case VP8DEC_HDRS_RDY:
    if(decoder_utils->alloc_buffers(shared_data)) {
      fprintf(stderr, "Error in custom buffer allocation\n");
      /* Return the stream buffer if allocation fails. */
      if(stream_buffer != NULL) {
        FifoPush(shared_data->input_fifo_, stream_buffer, FIFO_EXCEPTION_DISABLE);
      }
      set_state(shared_data, STATE_END_OF_STREAM);
    }
    break;
  case VP8DEC_STRM_PROCESSED:
  case VP8DEC_PIC_DECODED:
  case VP8DEC_SLICE_RDY:
  case VP8DEC_OK:
    /* Everything is good, keep on going. */
    shared_data->stream_buffer_refresh = 1;
    break;
  case VP8DEC_STREAM_NOT_SUPPORTED:
    /* Probably encountered faulty bitstream. */
    fprintf(stderr, "Missing headers or unsupported stream(?)\n");
    set_state(shared_data, STATE_END_OF_STREAM);
    break;
  case VP8DEC_MEMFAIL:
    /* Might be low on memory or something worse, anyway we
     * need to stop */
    fprintf(stderr,
            "VP8DecDecode VP8DEC_MEMFAIL. Low on memory?\n");
    set_state(shared_data, STATE_END_OF_STREAM);
    break;
  case VP8DEC_PARAM_ERROR:
  case VP8DEC_NOT_INITIALIZED:
    /* these are errors when the decoder cannot possibly call
     * stream consumed callback and therefore we push the buffer
     * into the empty queue here. */
    FifoPush(shared_data->input_fifo_, stream_buffer, FIFO_EXCEPTION_DISABLE);
  case VP8DEC_INITFAIL:
  case VP8DEC_HW_RESERVED:
  case VP8DEC_HW_TIMEOUT:
  case VP8DEC_HW_BUS_ERROR:
  case VP8DEC_SYSTEM_ERROR:
  case VP8DEC_DWL_ERROR:
  case VP8DEC_STRM_ERROR:
    /* This is bad stuff and something in the system is
     * fundamentally flawed. Can't do much so just stop. */
    fprintf(stderr, "VP8DecDecode fatal failure %i\n", rv);
    set_state(shared_data, STATE_END_OF_STREAM);
    break;
  case VP8DEC_EVALUATION_LIMIT_EXCEEDED:
    fprintf(stderr, "Decoder evaluation limit reached\n");
    /* Return the stream buffer. */
    set_state(shared_data, STATE_END_OF_STREAM);
    break;
  case VP8DEC_PIC_RDY:
  default:
    fprintf(stderr, "UNKNOWN ERROR!\n");
    /* Return the stream buffer. */
    FifoPush(shared_data->input_fifo_, stream_buffer, FIFO_EXCEPTION_DISABLE);
    set_state(shared_data, STATE_END_OF_STREAM);
    break;
  }
  return 0;
}

static int input_on_end_of_stream(shared_data* shared_data) {
  if(VP8DecMCEndOfStream(shared_data->dec_inst_) != VP8DEC_OK) {
    fprintf(stderr, "VP8DecMCEndOfStream returned unexpected failure\n");
  }

  /* We're done, wait for the output to finish it's job. */
  pthread_join(shared_data->output_thread_, NULL);
  if (shared_data->output_status_) {
    fprintf(stderr, "Output thread returned fail status (%u)\n",
            shared_data->output_status_);
  }
  return 0;
}

static int input_on_output_down(shared_data* shared_data,
                                decoder_utils* decoder_utils) {
  i32 i;
  for (i = 0; i < DEFAULT_FIFO_CAPACITY; i++) {
    FifoObject stream_buffer;
    FifoPop(shared_data->input_fifo_, &stream_buffer, FIFO_EXCEPTION_DISABLE);
    DWLFreeLinear(((VP8DecContainer_t *)shared_data->dec_inst_)->dwl,
                  &stream_buffer->stream_mem_);
    free(stream_buffer);
  }

  decoder_utils->free_buffers(shared_data);

  /* Release the allocated resources. */
  FifoRelease(shared_data->input_fifo_);
  set_state(shared_data, STATE_DOWN);
  VP8DecRelease(shared_data->dec_inst_);
  return 0;
}

static void print_header(void) {
  VP8DecApiVersion version = VP8DecGetAPIVersion();
  VP8DecBuild build = VP8DecGetBuild();
  printf("Multicore VP8 decoder\n");
  printf("API version %i.%i, ", version.major, version.minor);
  printf("SW build %u, ", build.sw_build);
  printf("HW build %u\n", build.hw_build);
}

static void print_usage(char* executable) {
  printf("Usage: %s [options] <file>\n", executable);
  printf("\t-a (or -Xa) user allocates picture buffers, alternate allocation order. (--user-allocated-buffers-alt)\n");
  printf("\t-cn Chrominance stride. (--chroma-stride)\n");
  printf("\t-C display cropped image. (--display-cropped)\n");
  printf("\t-D Disable output writing (--disable-write)\n");
  printf("\t-E use tiled reference frame format. (--tiled-output)\n");
  printf("\t-F Enable frame picture writing, filled black. (--frame-write)\n");
  printf("\t-I use interleaved frame buffers (requires stride mode and "\
         "user allocated buffers(--interleaved-buffers\n");
  printf("\t-ln Luminance stride. (--luma-stride)\n");
  printf("\t-Nn forces decoding to stop after n pictures. (--decode-n-pictures)\n");
  printf("\t-Ooutfile write output to \"outfile\" (default out.yuv). (--output-file <outfile>)\n");
  printf("\t-M write output as MD5 sum. (--md5)\n");
  printf("\t-X user allocates picture buffers. (--user-allocated-buffers)\n");
}

static void setup_default_params(test_params* params) {
  memset(params, 0, sizeof(test_params));
  params->out_file_name_ = DEFAULT_OUTPUT_FILENAME;
}

static void params_resolve_overlapping(struct TBCfg* tbcfg, test_params* params) {
  /* Override decoder allocation with tbcfg value */
  if (TBGetDecMemoryAllocation(&tb_cfg) &&
      params->user_allocated_buffers_ == VP8DEC_DECODER_ALLOC) {
    params->user_allocated_buffers_ = VP8DEC_EXTERNAL_ALLOC;
  }

  if (params->luma_stride_ || params->chroma_stride_) {
    if (params->user_allocated_buffers_ == VP8DEC_DECODER_ALLOC) {
      params->user_allocated_buffers_ = VP8DEC_EXTERNAL_ALLOC;
    }

    if (params->md5_) {
      params->frame_picture_ = 1;
    }
  }
  /* MD5 sum unsupported for external mem allocation */
  if (params->interleaved_buffers_ ||
      params->user_allocated_buffers_ != VP8DEC_DECODER_ALLOC) {
    params->md5_ = 0;
  }
}

static int parse_params(int argc, char* argv[], test_params* params) {
  i32 c;
  i32 option_index = 0;
  static struct option long_options[] = {
    {"user-allocated-buffers-alt", no_argument, 0, 'a'},
    {"chroma-stride", required_argument, 0, 'c'},
    {"display-cropped", no_argument, 0, 'C'},
    {"disable-write", no_argument, 0, 'D'},
    {"tiled-ouput", no_argument, 0, 'E'},
    {"frame-write", no_argument, 0, 'F'},
    {"interleaved-buffers", no_argument, 0, 'I'},
    {"luma-stride", required_argument, 0, 'l'},
    {"md5",  no_argument, 0, 'M'},
    {"decode-n-pictures",  required_argument, 0, 'N'},
    {"output-file",  required_argument, 0, 'O'},
    {"user-allocated-buffers", no_argument, 0, 'X'},
    {0, 0, 0, 0}
  };

  /* read command line arguments */
  while ((c = getopt_long(argc, argv, "ac:CDEFIl:MN:O:X", long_options, &option_index)) != -1) {
    switch (c) {

    case 'a':
      params->user_allocated_buffers_ = VP8DEC_EXTERNAL_ALLOC_ALT;
      break;
    case 'c':
      params->chroma_stride_ = atoi(optarg);
      break;
    case 'C':
      params->display_cropped_ = 1;
      break;
    case 'D':
      params->disable_write_ = 1;
      break;
    case 'E':
      params->tiled_ = 1;
      break;
    case 'F':
      params->frame_picture_ = 1;
      break;
    case 'I':
      params->interleaved_buffers_ = 1;
      break;
    case 'l':
      params->luma_stride_ = atoi(optarg);
      break;
    case 'M':
      params->md5_ = 1;
      break;
    case 'N':
      params->num_of_decoded_pics_ = atoi(optarg);
      break;
    case 'O':
      params->out_file_name_ = optarg;
      break;
    case 'X':
      params->user_allocated_buffers_ = VP8DEC_EXTERNAL_ALLOC;
      break;
    case ':':
      if (optopt == 'O' || optopt == 'N' || optopt == 'l' || optopt == 'c')
        fprintf(stderr, "Option -%c requires an argument.\n",
                optopt);
      return 1;
    case '?':
      if (isprint(optopt))
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
      else
        fprintf(stderr,
                "Unknown option character `\\x%x'.\n",
                optopt);
      return 1;
    default:
      break;
    }
  }
  if (optind >= argc) {
    fprintf(stderr, "Invalid or no input file specified\n");
    return 1;
  }
  params->in_file_name_ = argv[optind];
  return 0;
}

static pthread_mutex_t cs_mutex = PTHREAD_MUTEX_INITIALIZER;

static test_state get_state(shared_data* data) {
  pthread_mutex_lock(&cs_mutex);
  test_state state = data->state_;
  pthread_mutex_unlock(&cs_mutex);
  return state;
}

static void set_state(shared_data* data, test_state state) {
  pthread_mutex_lock(&cs_mutex);
  data->state_ = state;
  pthread_mutex_unlock(&cs_mutex);
}

static int hwconfig_override(VP8DecInst dec_inst,  struct TBCfg* tbcfg) {
  u32 clock_gating = TBGetDecClockGating(&tb_cfg);
  u32 data_discard = TBGetDecDataDiscard(&tb_cfg);
  u32 latency_comp = tb_cfg.dec_params.latency_compensation;
  u32 output_picture_endian = TBGetDecOutputPictureEndian(&tb_cfg);
  u32 bus_burst_length = tb_cfg.dec_params.bus_burst_length;
  u32 asic_service_priority = tb_cfg.dec_params.asic_service_priority;
  u32 output_format = TBGetDecOutputFormat(&tb_cfg);
  u32 service_merge_disable = TBGetDecServiceMergeDisable(&tb_cfg);

  DEBUG_PRINT(("Decoder Clock Gating %d\n", clock_gating));
  DEBUG_PRINT(("Decoder Data Discard %d\n", data_discard));
  DEBUG_PRINT(("Decoder Latency Compensation %d\n", latency_comp));
  DEBUG_PRINT(("Decoder Output Picture Endian %d\n", output_picture_endian));
  DEBUG_PRINT(("Decoder Bus Burst Length %d\n", bus_burst_length));
  DEBUG_PRINT(("Decoder Asic Service Priority %d\n", asic_service_priority));
  DEBUG_PRINT(("Decoder Output Format %d\n", output_format));

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
}
