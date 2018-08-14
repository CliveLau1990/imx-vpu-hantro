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
#include "h264_pp_pipeline.h"
#include "h264_pp_multibuffer.h"
#include "h264hwd_container.h"
#include "h264hwd_debug.h"
#include "h264hwd_util.h"

#ifndef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          do{}while(0)
#else
#undef TRACE_PP_CTRL
#define TRACE_PP_CTRL(...)          printf(__VA_ARGS__)
#endif

/*------------------------------------------------------------------------------
    Function name   : h264RegisterPP
    Description     :
    Return type     : i32
    Argument        : const void * dec_inst
    Argument        : const void  *pp_inst
    Argument        : (*PPRun)(const void *)
    Argument        : void (*PPEndCallback)(const void *)
------------------------------------------------------------------------------*/
i32 h264RegisterPP(const void *dec_inst, const void *pp_inst,
                   void (*PPDecStart) (const void *, const DecPpInterface *),
                   void (*PPDecWaitEnd) (const void *),
                   void (*PPConfigQuery) (const void *, DecPpQuery *),
                   void (*PPDisplayIndex) (const void *, u32)) {
  decContainer_t *dec_cont;

  dec_cont = (decContainer_t *) dec_inst;

  if(dec_inst == NULL || dec_cont->pp.pp_instance != NULL ||
      pp_inst == NULL || PPDecStart == NULL || PPDecWaitEnd == NULL
      || PPConfigQuery == NULL) {
    TRACE_PP_CTRL("h264RegisterPP: Invalid parameter\n");
    return -1;
  }

  if(dec_cont->asic_running) {
    TRACE_PP_CTRL("h264RegisterPP: Illegal action, asic_running\n");
    return -2;
  }

  if(dec_cont->b_mc) {
    TRACE_PP_CTRL("h264RegisterPP: Illegal action, multicore enabled\n");
    return -3;
  }

  dec_cont->pp.pp_instance = pp_inst;
  dec_cont->pp.PPConfigQuery = PPConfigQuery;
  dec_cont->pp.PPDecStart = PPDecStart;
  dec_cont->pp.PPDecWaitEnd = PPDecWaitEnd;
  dec_cont->pp.PPNextDisplayId = PPDisplayIndex;

  dec_cont->pp.dec_pp_if.pp_status = DECPP_IDLE;

  dec_cont->pp.pp_info.multi_buffer = 0;
  h264PpMultiInit(dec_cont, 0);

  dec_cont->storage.pp_used = 1;

  TRACE_PP_CTRL("h264RegisterPP: Connected to PP instance 0x%08x\n",
                (size_t) pp_inst);

  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : h264UnregisterPP
    Description     :
    Return type     : i32
    Argument        : const void * dec_inst
    Argument        : const void  *pp_inst
------------------------------------------------------------------------------*/
i32 h264UnregisterPP(const void *dec_inst, const void *pp_inst) {
  decContainer_t *dec_cont;

  dec_cont = (decContainer_t *) dec_inst;

  ASSERT(dec_inst != NULL && pp_inst == dec_cont->pp.pp_instance);

  if(pp_inst != dec_cont->pp.pp_instance) {
    TRACE_PP_CTRL("h264UnregisterPP: Invalid parameter\n");
    return -1;
  }

  if(dec_cont->asic_running) {
    TRACE_PP_CTRL("h264UnregisterPP: Illegal action, asic_running\n");
    return -2;
  }

  dec_cont->pp.pp_instance = NULL;
  dec_cont->pp.PPConfigQuery = NULL;
  dec_cont->pp.PPDecStart = NULL;
  dec_cont->pp.PPDecWaitEnd = NULL;

  TRACE_PP_CTRL("h264UnregisterPP: Disconnected from PP instance 0x%08x\n",
                (size_t) pp_inst);

  return 0;
}

/*------------------------------------------------------------------------------
    Function name   : h264PreparePpRun
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
------------------------------------------------------------------------------*/
void h264PreparePpRun(decContainer_t * dec_cont) {
  const storage_t *storage = &dec_cont->storage;
  DecPpInterface *dec_pp_if = &dec_cont->pp.dec_pp_if;
  const dpbStorage_t *dpb = storage->dpbs[storage->out_view];
  u32 multi_view;

  /* decoding second field of field coded picture -> queue current picture
   * for post processing */
  if (dec_pp_if->pp_status == DECPP_PIC_NOT_FINISHED &&
      !storage->second_field &&
      dec_cont->pp.pp_info.multi_buffer) {
    dec_cont->pp.queued_pic_to_pp[storage->view] =
      storage->dpb->current_out->data;
  }

  /* PP not connected or still running (not waited when first field of frame
   * finished */
  if(dec_cont->pp.pp_instance == NULL ||
      dec_pp_if->pp_status == DECPP_PIC_NOT_FINISHED) {
    return;
  }

  if(dpb->no_reordering && (!dpb->num_out || dpb->delayed_out)) {
    TRACE_PP_CTRL
    ("h264PreparePpRun: No reordering and no output => PP no run\n");
    /* decoder could create fake pictures in DPB, which will never go to output.
     * this is the case when new access unit detetcted but no valid slice decoded.
     */

    return;
  }

  if(dec_cont->pp.pp_info.multi_buffer == 0) {  /* legacy single buffer mode */
    /* in some situations, like field decoding,
     * the dpb->num_out is not reset, but, dpb->num_out == dpb->outIndex */
    if(dpb->num_out != 0) { /* we have output */
      TRACE_PP_CTRL
      ("h264PreparePpRun: output picture => PP could run!\n");

      if(dpb->out_buf[dpb->out_index_r].data == storage->curr_image->data) {
        /* we could have pipeline */
        TRACE_PP_CTRL("h264PreparePpRun: decode == output\n");

        if(storage->active_sps->frame_mbs_only_flag == 0 &&
            storage->second_field) {
          TRACE_PP_CTRL
          ("h264PreparePpRun: first field only! Do not run PP, yet!\n");
          return;
        }

        if(storage->curr_image->pic_struct != FRAME) {
          u32 opposit_field = storage->curr_image->pic_struct ^ 1;

          if(dpb->current_out->status[opposit_field] != EMPTY) {
            dec_pp_if->use_pipeline = 0;
            TRACE_PP_CTRL
            ("h264PreparePpRun: second field of frame! Pipeline cannot be used!\n");
          }
        } else if(!storage->active_sps->mb_adaptive_frame_field_flag) {
          dec_pp_if->use_pipeline =
            dec_cont->pp.pp_info.pipeline_accepted & 1;
        } else
          dec_pp_if->use_pipeline = 0;

        if(dec_pp_if->use_pipeline) {
          TRACE_PP_CTRL
          ("h264PreparePpRun: pipeline=ON => PP will be running\n");
          dec_pp_if->pp_status = DECPP_RUNNING;

          if(!storage->prev_nal_unit->nal_ref_idc) {
            dec_cont->asic_buff->disable_out_writing = 1;
            TRACE_PP_CTRL
            ("h264PreparePpRun: Not reference => Disable decoder output writing\n");
          }
        } else {
          TRACE_PP_CTRL
          ("h264PreparePpRun: pipeline=OFF => PP has to run after DEC\n");
        }
      } else {
        dec_pp_if->pp_status = DECPP_RUNNING;
        dec_pp_if->use_pipeline = 0;

        TRACE_PP_CTRL
        ("h264PreparePpRun: decode != output => pipeline=OFF => PP run in parallel\n");
      }

    } else {
      TRACE_PP_CTRL
      ("h264PreparePpRun: no output picture => PP no run!\n");
    }

    return;
  }

  /* post process picture of current view. If decoding base view and only
   * stereo view  picture available for post processing -> pp
   *  (parallel dec+pp case) (needed for field coded pictures to start
   *  pp for stereo view when starting to decode second field of base view,
   *  base view pp'd while decoding first fields of base and stereo view) */
  multi_view = storage->view;
  if (storage->view == 0 &&
      dec_cont->pp.queued_pic_to_pp[storage->view] == NULL &&
      dec_cont->pp.queued_pic_to_pp[!storage->view] != NULL)
    multi_view = !storage->view;
  dpb = dec_cont->storage.dpbs[storage->view];

  /* multibuffer mode */
  TRACE_PP_CTRL("h264PreparePpRun: MULTIBUFFER!\n");
  if(dec_cont->pp.queued_pic_to_pp[multi_view] == NULL &&
      storage->active_sps->frame_mbs_only_flag == 0 && storage->second_field) {
    TRACE_PP_CTRL
    ("h264PreparePpRun: no queued picture and first field only! Do not run PP, yet!\n");
    return;
  }

  if(dec_cont->pp.queued_pic_to_pp[multi_view] == NULL &&
      storage->current_marked == HANTRO_FALSE) {
    TRACE_PP_CTRL
    ("h264PreparePpRun: no queued picture and current pic NOT for display! Do not run PP!\n");
    return;
  }

  if(storage->curr_image->pic_struct != FRAME) {

    TRACE_PP_CTRL("h264PreparePpRun: 2 Fields! Pipeline cannot be used!\n");

    /* we shall not have EMPTY output marked for display, missing fields are acceptable */
    ASSERT((dec_cont->pp.queued_pic_to_pp[multi_view] != NULL) ||
           (dpb->current_out->status[0] != EMPTY) ||
           (dpb->current_out->status[1] != EMPTY));

    dec_pp_if->use_pipeline = 0;
  } else if(!storage->active_sps->mb_adaptive_frame_field_flag) {
    dec_pp_if->use_pipeline = dec_cont->pp.pp_info.pipeline_accepted & 1;
  } else
    dec_pp_if->use_pipeline = 0;

  if(dec_pp_if->use_pipeline) {
    dec_pp_if->pp_status = DECPP_RUNNING;
    TRACE_PP_CTRL
    ("h264PreparePpRun: pipeline=ON => PP will be running\n");
  } else {
    if(dec_cont->pp.queued_pic_to_pp[multi_view] != NULL) {
      dec_pp_if->pp_status = DECPP_RUNNING;
      TRACE_PP_CTRL
      ("h264PreparePpRun: pipeline=OFF, queued picture => PP run in parallel\n");
    } else {
      TRACE_PP_CTRL
      ("h264PreparePpRun: pipeline=OFF, no queued picture => PP no run\n");
      /* do not process pictures added to DPB but not intended for display */
      if(dpb->no_reordering ||
          dpb->current_out->to_be_displayed == HANTRO_TRUE) {
        if(!storage->second_field) { /* do not queue first field */
          dec_cont->pp.queued_pic_to_pp[storage->view] = dpb->current_out->data;
        }
      }
    }
  }
}

/*------------------------------------------------------------------------------
    Function name   : h264PpMultiAddPic
    Description     : Add a new picture to the PP processed table (frist free place).
                      Return the tabel position where added.
    Return type     : u32
    Argument        : decContainer_t * dec_cont
    Argument        : const struct DWLLinearMem * data
------------------------------------------------------------------------------*/
u32 h264PpMultiAddPic(decContainer_t * dec_cont, const struct DWLLinearMem * data) {
  u32 next_free_id;

  ASSERT(dec_cont->pp.pp_info.multi_buffer != 0);

  for(next_free_id = 0; next_free_id <= dec_cont->pp.multi_max_id; next_free_id++) {
    if(dec_cont->pp.sent_pic_to_pp[next_free_id] == NULL) {
      break;
    }
  }

  ASSERT(next_free_id <= dec_cont->pp.multi_max_id);

  if(next_free_id <= dec_cont->pp.multi_max_id)
    dec_cont->pp.sent_pic_to_pp[next_free_id] = data;

  return next_free_id;
}

/*------------------------------------------------------------------------------
    Function name   : h264PpMultiRemovePic
    Description     : Reomove a picture form the PP processed list.
                      Return the position which was emptied.
    Return type     : u32
    Argument        : decContainer_t * dec_cont
    Argument        : const struct DWLLinearMem * data
------------------------------------------------------------------------------*/
u32 h264PpMultiRemovePic(decContainer_t * dec_cont, const struct DWLLinearMem * data) {
  u32 pic_id;

  ASSERT(dec_cont->pp.pp_info.multi_buffer != 0);

  for(pic_id = 0; pic_id <= dec_cont->pp.multi_max_id; pic_id++) {
    if(dec_cont->pp.sent_pic_to_pp[pic_id] == data) {
      break;
    }
  }

  ASSERT(pic_id <= dec_cont->pp.multi_max_id);

  if(pic_id <= dec_cont->pp.multi_max_id)
    dec_cont->pp.sent_pic_to_pp[pic_id] = NULL;

  return pic_id;
}

/*------------------------------------------------------------------------------
    Function name   : h264PpMultiFindPic
    Description     : Find a picture in the PP processed list. If found, return
                      the position. If not found, return an value bigger than
                      the max.
    Return type     : u32
    Argument        : decContainer_t * dec_cont
    Argument        : const struct DWLLinearMem * data
------------------------------------------------------------------------------*/
u32 h264PpMultiFindPic(decContainer_t * dec_cont, const struct DWLLinearMem * data) {
  u32 pic_id;

  ASSERT(dec_cont->pp.pp_info.multi_buffer != 0);

  for(pic_id = 0; pic_id <= dec_cont->pp.multi_max_id; pic_id++) {
    if(dec_cont->pp.sent_pic_to_pp[pic_id] == data) {
      break;
    }
  }

  return pic_id;
}

/*------------------------------------------------------------------------------
    Function name   : h264PpMultiInit
    Description     : Initialize the PP processed list.
    Return type     : void
    Argument        : decContainer_t * dec_cont
    Argument        : u32 max_buff_id - max ID in use (buffs = (max_buff_id + 1))
------------------------------------------------------------------------------*/
void h264PpMultiInit(decContainer_t * dec_cont, u32 max_buff_id) {
  u32 i;
  const u32 buffs =
    sizeof(dec_cont->pp.sent_pic_to_pp) / sizeof(*dec_cont->pp.sent_pic_to_pp);

  ASSERT(max_buff_id < buffs);

  dec_cont->pp.queued_pic_to_pp[0] = NULL;
  dec_cont->pp.queued_pic_to_pp[1] = NULL;
  dec_cont->pp.multi_max_id = max_buff_id;
  for(i = 0; i < buffs; i++) {
    dec_cont->pp.sent_pic_to_pp[i] = NULL;
  }
}

/*------------------------------------------------------------------------------
    Function name   : h264PpMultiMvc
    Description     :
    Return type     : void
    Argument        : decContainer_t * dec_cont
    Argument        : u32 max_buff_id - max ID in use (buffs = (max_buff_id + 1))
------------------------------------------------------------------------------*/
void h264PpMultiMvc(decContainer_t * dec_cont, u32 max_buff_id) {

  ASSERT(max_buff_id < (sizeof(dec_cont->pp.sent_pic_to_pp) / sizeof(*dec_cont->pp.sent_pic_to_pp)));

  dec_cont->pp.multi_max_id = max_buff_id;

}

u32 h264UseDisplaySmoothing(const void *dec_inst) {

  decContainer_t *dec_cont = (decContainer_t*)dec_inst;
  if (dec_cont->storage.use_smoothing)
    return HANTRO_TRUE;
  else
    return HANTRO_FALSE;

}
