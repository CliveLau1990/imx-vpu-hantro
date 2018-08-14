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

#include "trace.h"

extern u32 pic_number;
extern u32 test_data_format;  /* 0->trc, 1->hex */

struct TraceMpeg2DecTools trace_mpeg2_dec_tools;
struct TraceH264DecTools trace_h264_dec_tools;
struct TraceJpegDecTools trace_jpeg_dec_tools;
struct TraceMpeg4DecTools trace_mpeg4_dec_tools;
struct TraceVc1DecTools trace_vc1_dec_tools;
struct TraceRvDecTools trace_rv_dec_tools;
u32 trace_ref_buff_hit_rate;

extern FILE *trace_acdcd_out;
extern FILE *trace_acdcd_out_data;
extern FILE *trace_decoded_mvs;
extern FILE *trace_rlc;
extern FILE *trace_mb_ctrl;
extern FILE *trace_mb_ctrl_hex;
extern FILE *trace_picture_ctrl_dec;
extern FILE *trace_picture_ctrl_pp;
extern FILE *trace_picture_ctrl_dec_tiled;
extern FILE *trace_picture_ctrl_pp_tiled;
extern FILE *trace_motion_vectors;
extern FILE *trace_motion_vectors_hex;
extern FILE *trace_dir_mode_mvs;
extern FILE *trace_final_mvs;
extern FILE *trace_dir_mode_mvs_hex;
extern FILE *trace_separ_dc;
extern FILE *trace_separ_dc_hex;
extern FILE *trace_recon;
extern FILE *trace_scd_out_data;
extern FILE *trace_bs;
extern FILE *trace_bit_plane_ctrl;
extern FILE *trace_transd_first_round;
extern FILE *trace_inter_out_data;
extern FILE *trace_overlap_smooth;
extern FILE *trace_stream;
extern FILE *trace_stream_ctrl;
extern FILE *trace_sw_reg_access;
extern FILE *trace_sw_reg_access_tiled;
extern FILE *trace_qtables;
extern FILE *trace_intra4x4_modes;
extern FILE *trace_intra4x4_modes_hex;
extern FILE *trace_dct_out_data;
extern FILE *trace_inter_ref_y;
extern FILE *trace_inter_ref_y1;
extern FILE *trace_inter_ref_cb;
extern FILE *trace_inter_ref_cb1;
extern FILE *trace_inter_ref_cr;
extern FILE *trace_inter_ref_cr1;
extern FILE *trace_overfill;
extern FILE *trace_overfill1;
extern FILE *trace_intra_pred;
extern FILE *trace_h264_pic_id_map;
extern FILE *trace_residual;
extern FILE *trace_iq;
extern FILE *trace_jpeg_tables;
extern FILE *trace_out;
extern FILE *trace_out_tiled;
extern FILE *trace_vc1_filtering_ctrl;
extern FILE *trace_stream_txt;
extern FILE *trace_cabac_table[4];
extern FILE *trace_cabac_bin;
extern FILE *trace_cabac_ctx;
extern FILE *trace_neighbour_mv;
extern FILE *trace_ic_sets;
extern FILE *trace_above_mb_mem;
extern FILE *trace_pic_ord_cnts;
extern FILE *trace_scaling_lists;
extern FILE *trace_ref_pic_list;
extern FILE *trace_implicit_weights;
extern FILE *trace_intra_filtered_pels;
extern FILE *trace_motion_vectors_fixed;
extern FILE *trace_decoded_mvs_fixed;
extern FILE *trace_dir_mv_fetch;
extern FILE *trace_scaling_out;
extern FILE *trace_pjpeg_coeffs;
extern FILE *trace_pjpeg_coeffs_hex;
extern FILE *trace_inter_out_y;
extern FILE *trace_inter_out_y1;
extern FILE *trace_inter_out_cb;
extern FILE *trace_inter_out_cb1;
extern FILE *trace_inter_out_cr;
extern FILE *trace_inter_out_cr1;
extern FILE *trace_filter_internal;
extern FILE *trace_out2nd_ch;

extern FILE *trace_pp_ablend1;
extern FILE *trace_pp_ablend2;
extern FILE *trace_pp_in;
extern FILE *trace_pp_in_tiled4x4;
extern FILE *trace_ctrl_pp_in_tiled4x4;
extern FILE *trace_dscale_pp_out;
extern FILE *trace_ctrl_dscale_pp_out;
extern FILE *trace_pp_in_tiled;
extern FILE *trace_pp_in_bot;
extern FILE *trace_pp_out;
extern FILE *trace_pp_background;
extern FILE *trace_pp_deint_in;
extern FILE *trace_pp_deint_out_y;
extern FILE *trace_pp_deint_out_cb;
extern FILE *trace_pp_deint_out_cr;
extern FILE *trace_pp_crop;
extern FILE *trace_pp_rotation;
extern FILE *trace_pp_scaling_kernel;
extern FILE *trace_pp_scaling_r;
extern FILE *trace_pp_scaling_g;
extern FILE *trace_pp_scaling_b;
extern FILE *trace_pp_color_conv_r;
extern FILE *trace_pp_color_conv_g;
extern FILE *trace_pp_color_conv_b;
extern FILE *trace_pp_weights;

extern FILE *trace_ref_bufferd_pic_ctrl;
extern FILE *trace_ref_bufferd_ctrl;
extern FILE *trace_ref_bufferd_pic_y;
extern FILE *trace_ref_bufferd_pic_cb_cr;
extern FILE *trace_custom_idct;
extern FILE *trace_busload;
extern FILE *trace_variance;

extern FILE *trace_slice_sizes;
extern FILE *trace_rv_mvd_bits;
extern FILE *trace_segmentation;
extern FILE *trace_boolcoder[9];
extern FILE *trace_bc_stream[9];
extern FILE *trace_bc_stream_ctrl[9];
extern FILE *trace_prob_tables;
extern FILE *trace_vp78_above_cbf;
extern FILE *trace_vp78_mv_weight;

extern FILE *trace_rlc_unpacked;
extern FILE *trace_adv_pre_fetch;
extern FILE *trace_huffman_ctx;
extern FILE *trace_huffman;
extern FILE *trace_prob1;
extern FILE *trace_prob2;
extern FILE *trace_zig_zag;
extern FILE *trace_nearest;
extern FILE *trace_inter_filtered_y;
extern FILE *trace_inter_filtered_cb;
extern FILE *trace_inter_filtered_cr;


static FILE *trace_sequence_ctrl;
static FILE *trace_sequence_ctrl_pp;
FILE *trace_decoding_tools;

/* Trace files for G1 (temporarily) */

/*------------------------------------------------------------------------------
 *   Function : openTraceFiles
 * ---------------------------------------------------------------------------*/
u32 openTraceFiles(void) {

  char trace_string[80];
  FILE *trace_cfg;
  u32 i;
  char tmp_string[80];

  trace_cfg = fopen("trace.cfg", "r");
  if(!trace_cfg) {
    return (1);
  }

  while(fscanf(trace_cfg, "%s\n", trace_string) != EOF) {
    if(!strcmp(trace_string, "toplevel") && !trace_sequence_ctrl) {
      for (i = 0 ; i < 9 ; ++i) {
        sprintf(tmp_string, "stream_%d.trc", i+1 );
        trace_bc_stream[i] = fopen(tmp_string, "w");
        sprintf(tmp_string, "stream_control_%d.trc", i+1 );
        trace_bc_stream_ctrl[i] = fopen(tmp_string, "w");
      }
      trace_prob_tables = fopen("boolcoder_prob.trc", "w");
      trace_sequence_ctrl = fopen("sequence_ctrl.trc", "w");
      trace_picture_ctrl_dec = fopen("picture_ctrl_dec.trc", "w");
      trace_picture_ctrl_dec_tiled = fopen("picture_ctrl_dec_tiled.trc", "w");
      trace_jpeg_tables = fopen("jpeg_tables.trc", "w");
      trace_stream = fopen("stream.trc", "w");
      trace_stream_ctrl = fopen("stream_control.trc", "w");
      trace_out = fopen("decoder_out.trc", "w");
      trace_out_tiled = fopen("decoder_out_tiled.trc", "w");
      trace_separ_dc = fopen("dc_separate_coeffs.trc", "w");
      trace_separ_dc_hex = fopen("dc_separate_coeffs.hex", "w");
      trace_bit_plane_ctrl = fopen("vc1_bitplane_ctrl.trc", "w");
      trace_dir_mode_mvs = fopen("direct_mode_mvs.trc", "w");
      trace_dir_mode_mvs_hex = fopen("direct_mode_mvs.hex", "w");
      trace_qtables = fopen("qtables.trc", "w");
      trace_sw_reg_access = fopen("swreg_accesses.trc", "w");
      trace_sw_reg_access_tiled = fopen("swreg_accesses_tiled.trc", "w");
      trace_busload = fopen("busload.trc", "w");

      trace_cabac_table[0] = fopen("cabac_table_intra.trc", "w");
      trace_cabac_table[1] = fopen("cabac_table_inter0.trc", "w");
      trace_cabac_table[2] = fopen("cabac_table_inter1.trc", "w");
      trace_cabac_table[3] = fopen("cabac_table_inter2.trc", "w");
      trace_segmentation = fopen("segmentation.trc", "w");

      /*required if sw is performing entropy decoding */
      trace_mb_ctrl = fopen("mbcontrol.trc", "w");
      trace_mb_ctrl_hex = fopen("mbcontrol.hex", "w");
      trace_motion_vectors = fopen("motion_vectors.trc", "w");
      trace_motion_vectors_hex = fopen("motion_vectors.hex", "w");
      trace_intra4x4_modes = fopen("intra4x4_modes.trc", "w");
      trace_intra4x4_modes_hex = fopen("intra4x4_modes.hex", "w");
      trace_rlc = fopen("rlc.trc", "w");
      trace_rlc_unpacked = fopen("rlc_unpacked.trc", "w");
      trace_vp78_mv_weight = fopen("vp78_context_weight.trc", "w" );

      trace_ref_bufferd_pic_ctrl = fopen("refbufferd_picctrl.trc", "w");
      trace_ref_bufferd_ctrl = fopen("refbufferd_ctrl.trc", "w");

      trace_pjpeg_coeffs = fopen("prog_jpeg_coefficients.trc", "w");
      trace_pjpeg_coeffs_hex = fopen("prog_jpeg_coefficients.hex", "w");

      trace_pic_ord_cnts = fopen("picord_counts.trc", "w");
      trace_scaling_lists = fopen("scaling_lists.trc", "w");

      trace_slice_sizes = fopen("slice_sizes.trc", "w");;

      trace_rv_mvd_bits = fopen("mvd_flags.trc", "w");;
      trace_huffman = fopen("huffman.trc", "w");
      trace_prob1 = fopen("boolcoder_1.trc", "w");
      trace_prob2 = fopen("boolcoder_2.trc", "w");

      trace_out2nd_ch = fopen("decoder_out_ch_8pix.trc", "w");

      if((trace_sequence_ctrl == NULL) || (trace_picture_ctrl_dec == NULL) ||
          (trace_jpeg_tables == NULL) || (trace_stream == NULL) ||
          (trace_stream_ctrl == NULL) || (trace_out == NULL) ||
          /*(trace_out_tiled == NULL) || */(trace_separ_dc == NULL) ||
          (trace_bit_plane_ctrl == NULL) || (trace_dir_mode_mvs == NULL) ||
          (trace_qtables == NULL) || (trace_sw_reg_access == NULL) ||
          (trace_mb_ctrl == NULL) || (trace_motion_vectors == NULL) ||
          (trace_intra4x4_modes == NULL) || (trace_rlc == NULL) ||
          (trace_ref_bufferd_pic_ctrl == NULL) ||
          (trace_ref_bufferd_ctrl == NULL) ||
          (trace_pjpeg_coeffs == NULL) || (trace_pjpeg_coeffs_hex == NULL) ||
          (trace_pic_ord_cnts == NULL) || (trace_scaling_lists == NULL) ||
          (trace_slice_sizes == NULL) || (trace_rv_mvd_bits == NULL) ||
          (trace_out2nd_ch == NULL))

      {
        return (0);
      }
    }

    if(!strcmp(trace_string, "all") && !trace_acdcd_out) {
      for (i = 0 ; i < 9 ; ++i) {
        sprintf(tmp_string, "boolcoder_%d_ctx.trc", i+1 );
        trace_boolcoder[i] = fopen(tmp_string, "w");
      }
      trace_acdcd_out = fopen("acdcd_out.trc", "w");
      trace_acdcd_out_data = fopen("acdcd_outdata.trc", "w");
      trace_bs = fopen("bs.trc", "w");
      trace_dct_out_data = fopen("dct_outdata.trc", "w");
      trace_decoded_mvs = fopen("decoded_mvs.trc", "w");
      trace_final_mvs = fopen("final_mvs.trc", "w");
      trace_inter_ref_y = fopen("inter_reference_y.trc", "w");
      trace_inter_ref_y1 = fopen("inter_reference1_y.trc", "w");
      trace_inter_ref_cb = fopen("inter_reference_cb.trc", "w");
      trace_inter_ref_cb1 = fopen("inter_reference1_cb.trc", "w");
      trace_inter_ref_cr = fopen("inter_reference_cr.trc", "w");
      trace_inter_ref_cr1 = fopen("inter_reference1_cr.trc", "w");
      trace_overfill = fopen("inter_overfill.trc", "w");
      trace_overfill1 = fopen("inter_overfill1.trc", "w");
      trace_inter_out_data = fopen("inter_outdata.trc", "w");
      trace_intra_pred = fopen("intra_predicted.trc", "w");
      trace_recon = fopen("reconstructed.trc", "w");
      trace_scd_out_data = fopen("scd_outdata.trc", "w");
      trace_transd_first_round = fopen("transd_1rnd.trc", "w");
      trace_h264_pic_id_map = fopen("h264_picid_map.trc", "w");
      trace_residual = fopen("residual.trc", "w");
      trace_iq = fopen("inverse_quant.trc", "w");
      trace_overlap_smooth = fopen("overlap_smoothed.trc", "w");
      trace_vc1_filtering_ctrl = fopen("vc1_filtering_ctrl.trc", "w");
      trace_stream_txt = fopen("stream.txt", "w");
      trace_neighbour_mv = fopen("neighbour_mvs.trc", "w");
      trace_ic_sets = fopen("intensity_sets.trc", "w");
      trace_above_mb_mem = fopen("above_mb_ctrl_sram.trc", "w");
      trace_ref_pic_list = fopen("ref_pic_list.trc", "w");
      trace_implicit_weights = fopen("implicit_weights.trc", "w");
      trace_intra_filtered_pels = fopen("intra_filtered_pxls.trc", "w");
      trace_motion_vectors_fixed = fopen("motion_vectors_fixed.trc", "w");
      trace_decoded_mvs_fixed = fopen("decoded_mvs_fixed.trc", "w");
      trace_dir_mv_fetch = fopen("h264_dirmv_fetch.trc", "w");
      trace_scaling_out = fopen("h264_scaling_out.trc", "w");
      trace_custom_idct = fopen("custom_idct.trc", "w");
      trace_inter_out_y = fopen("inter_interpolated_y.trc", "w");
      trace_inter_out_y1 = fopen("inter_interpolated1_y.trc", "w");
      trace_inter_out_cb = fopen("inter_interpolated_cb.trc", "w");
      trace_inter_out_cb1 = fopen("inter_interpolated1_cb.trc", "w");
      trace_inter_out_cr = fopen("inter_interpolated_cr.trc", "w");
      trace_inter_out_cr1 = fopen("inter_interpolated1_cr.trc", "w");
      trace_filter_internal = fopen("rv_filter.trc", "w");
      trace_variance = fopen("inter_variance.trc", "w");
      trace_huffman_ctx = fopen("huffman_ctx.trc", "w");
      trace_zig_zag = fopen("zigzag.trc", "w");
      trace_nearest = fopen("nearest.trc", "w" );
      trace_inter_filtered_y = fopen("inter_vp6_filtered_ref_y.trc","w");
      trace_inter_filtered_cb = fopen("inter_vp6_filtered_ref_cb.trc","w");
      trace_inter_filtered_cr = fopen("inter_vp6_filtered_ref_cr.trc","w");

      trace_cabac_bin = fopen("cabac_bin.trc", "w");
      trace_ref_bufferd_pic_y = fopen("refbufferd_buffil_y.trc", "w");
      trace_adv_pre_fetch = fopen("advanced_prefetch.trc", "w" );
      trace_ref_bufferd_pic_cb_cr = fopen("refbufferd_buffil_c.trc", "w");

      trace_vp78_above_cbf = fopen("vp78_above_cbf.trc", "w");

      if((trace_acdcd_out == NULL) || (trace_acdcd_out_data == NULL) ||
          (trace_bs == NULL) || (trace_dct_out_data == NULL) ||
          (trace_decoded_mvs == NULL) || (trace_inter_ref_y == NULL) ||
          (trace_inter_ref_y1 == NULL) || (trace_inter_ref_cb == NULL) ||
          (trace_inter_ref_cb1 == NULL) || (trace_inter_ref_cr == NULL) ||
          (trace_inter_ref_cr1 == NULL) || (trace_overfill == NULL) ||
          (trace_overfill1 == NULL) || (trace_inter_out_data == NULL) ||
          (trace_intra_pred == NULL) || (trace_recon == NULL) ||
          (trace_scd_out_data == NULL) || (trace_transd_first_round == NULL) ||
          (trace_h264_pic_id_map == NULL) || (trace_residual == NULL) ||
          (trace_iq == NULL) || (trace_overlap_smooth == NULL) ||
          (trace_vc1_filtering_ctrl == NULL) || (trace_stream_txt == NULL) ||
          (trace_final_mvs == NULL) || (trace_neighbour_mv == NULL) ||
          (trace_ic_sets == NULL) || (trace_above_mb_mem == NULL) ||
          (trace_ref_pic_list == NULL) || (trace_implicit_weights == NULL) ||
          (trace_intra_filtered_pels == NULL) ||
          (trace_motion_vectors_fixed == NULL) ||
          (trace_custom_idct == NULL) ||
          (trace_cabac_bin == NULL) ||
          (trace_decoded_mvs_fixed == NULL) || (trace_dir_mv_fetch == NULL) ||
          (trace_inter_out_y == NULL) || (trace_inter_out_y1 == NULL) ||
          (trace_inter_out_cb == NULL) || (trace_inter_out_cb1 == NULL) ||
          (trace_inter_out_cr == NULL) || (trace_inter_out_cr1 == NULL) ||
          (trace_ref_bufferd_pic_y == NULL) || (trace_ref_bufferd_pic_cb_cr == NULL) ||
          (trace_filter_internal == NULL)) {
        return (1);
      }
    }
    if(!strcmp(trace_string, "fpga") && !trace_picture_ctrl_dec) {
      trace_picture_ctrl_dec = fopen("picture_ctrl_dec.trc", "w");
      trace_picture_ctrl_pp = fopen("picture_ctrl_pp.trc", "w");
      trace_picture_ctrl_dec_tiled = fopen("picture_ctrl_dec_tiled.trc", "w");
      trace_picture_ctrl_pp_tiled = fopen("picture_ctrl_pp_tiled.trc", "w");
      trace_busload = fopen("busload.trc", "w");


      if(trace_picture_ctrl_dec == NULL)
        return (1);
    }
    if(!strcmp(trace_string, "decoding_tools")) {
      /* MPEG2 decoding tools trace */
      memset(&trace_mpeg2_dec_tools, 0,
             sizeof(struct TraceMpeg2DecTools));
#if 0
      /* by default MPEG-1 is decoded and MPEG-2 is signaled */
      trace_mpeg2_dec_tools.decoding_mode = TRACE_MPEG1;

      /* MPEG-1 is always progressive */
      trace_mpeg2_dec_tools.sequence_type.interlaced = 0;
      trace_mpeg2_dec_tools.sequence_type.progressive = 1;
#endif
      /* H.264 decoding tools trace */
      memset(&trace_h264_dec_tools, 0,
             sizeof(struct TraceH264DecTools));
#if 0
      trace_h264_dec_tools.decoding_mode = TRACE_H264;
#endif

      /* JPEG decoding tools trace */
      memset(&trace_jpeg_dec_tools, 0,
             sizeof(struct TraceJpegDecTools));
#if 0
      trace_jpeg_dec_tools.decoding_mode = TRACE_JPEG;
#endif

      /* MPEG4 decoding tools trace */
      memset(&trace_mpeg4_dec_tools, 0,
             sizeof(struct TraceMpeg4DecTools));
#if 0
      trace_mpeg4_dec_tools.decoding_mode = TRACE_MPEG4;
#endif

      /* VC1 decoding tools trace */
      memset(&trace_vc1_dec_tools, 0,
             sizeof(struct TraceVc1DecTools));
#if 0
      trace_vc1_dec_tools.decoding_mode = TRACE_VC1;
#endif

      memset(&trace_rv_dec_tools, 0,
             sizeof(struct TraceRvDecTools));

      trace_decoding_tools = fopen("decoding_tools.trc", "w");

      if(trace_decoding_tools == NULL)
        return (1);
    }
    if(!strcmp(trace_string, "pp_toplevel")) {
      trace_sequence_ctrl_pp = fopen("sequence_ctrl_pp.trc", "w");
      trace_picture_ctrl_pp = fopen("picture_ctrl_pp.trc", "w");
      trace_picture_ctrl_pp_tiled = fopen("picture_ctrl_pp_tiled.trc", "w");
      trace_pp_in = fopen("pp_in.trc", "w");
      trace_pp_in_tiled = fopen("pp_in_tiled.trc", "w");
      trace_pp_in_bot = fopen("pp_in_bot.trc", "w");
      trace_pp_out = fopen("pp_out.trc", "w");
      trace_pp_background = fopen("pp_background.trc", "w");
      trace_pp_ablend1 = fopen("pp_ablend1.trc", "w");
      trace_pp_ablend2 = fopen("pp_ablend2.trc", "w");
      trace_pp_in_tiled4x4 = fopen("ppin_tiled4x4.bin", "wb");
      trace_ctrl_pp_in_tiled4x4 = fopen("ppin_ctrl_tiled4x4.bin", "wb");
      trace_dscale_pp_out = fopen("ppout_downscale.bin", "wb");
      trace_ctrl_dscale_pp_out = fopen("ppout_ctrl_downscale.bin", "wb");
      if(trace_sequence_ctrl_pp == NULL ||
          trace_picture_ctrl_pp == NULL ||
          trace_pp_in == NULL ||
          trace_pp_in_bot == NULL ||
          trace_pp_out == NULL ||
          trace_pp_background == NULL ||
          trace_pp_ablend1 == NULL || trace_pp_ablend2 == NULL ||
          trace_pp_in_tiled4x4 == NULL || trace_ctrl_pp_in_tiled4x4 == NULL ||
          trace_dscale_pp_out == NULL || trace_ctrl_dscale_pp_out == NULL)
        return (1);
    }
    if(!strcmp(trace_string, "pp_all")) {
      trace_pp_deint_in = fopen("pp_deint_in.trc", "w");
      trace_pp_deint_out_y = fopen("pp_deint_out_y.trc", "w");
      trace_pp_deint_out_cb = fopen("pp_deint_out_cb.trc", "w");
      trace_pp_deint_out_cr = fopen("pp_deint_out_cr.trc", "w");
      trace_pp_crop = fopen("pp_crop.trc", "w");;
      trace_pp_rotation = fopen("pp_rotation.trc", "w");;
      trace_pp_scaling_kernel = fopen("pp_scaling_kernel.trc", "w");;
      trace_pp_scaling_r = fopen("pp_scaling_r.trc", "w");;
      trace_pp_scaling_g = fopen("pp_scaling_g.trc", "w");;
      trace_pp_scaling_b = fopen("pp_scaling_b.trc", "w");;
      trace_pp_color_conv_r = fopen("pp_colorconv_r.trc", "w");;
      trace_pp_color_conv_g = fopen("pp_colorconv_g.trc", "w");;
      trace_pp_color_conv_b = fopen("pp_colorconv_b.trc", "w");;
      trace_pp_weights = fopen("pp_weights.trc", "w");;
      if(trace_pp_deint_in == NULL ||
          trace_pp_deint_out_y == NULL ||
          trace_pp_deint_out_cb == NULL ||
          trace_pp_deint_out_cr == NULL ||
          trace_pp_crop == NULL ||
          trace_pp_rotation == NULL ||
          trace_pp_scaling_kernel == NULL ||
          trace_pp_scaling_r == NULL ||
          trace_pp_scaling_g == NULL ||
          trace_pp_scaling_b == NULL ||
          trace_pp_color_conv_r == NULL ||
          trace_pp_color_conv_g == NULL ||
          trace_pp_color_conv_b == NULL || trace_pp_weights == NULL)
        return (1);
    }
  }
  return (1);
}

/*------------------------------------------------------------------------------
 *   Function : closeTraceFiles
 * ---------------------------------------------------------------------------*/
void closeTraceFiles(void) {
  u32 i;
  for( i = 0 ; i < 9 ; ++i ) {
    if(trace_boolcoder[i])
      fclose(trace_boolcoder[i]);
    if(trace_bc_stream[i])
      fclose(trace_bc_stream[i]);
    if(trace_bc_stream_ctrl[i])
      fclose(trace_bc_stream_ctrl[i]);
  }
  if(trace_acdcd_out)
    fclose(trace_acdcd_out);
  if(trace_acdcd_out_data)
    fclose(trace_acdcd_out_data);
  if(trace_decoded_mvs)
    fclose(trace_decoded_mvs);
  if(trace_rlc)
    fclose(trace_rlc);
  if(trace_rlc_unpacked)
    fclose(trace_rlc_unpacked);
  if(trace_vp78_mv_weight)
    fclose(trace_vp78_mv_weight);
  if(trace_mb_ctrl)
    fclose(trace_mb_ctrl);
  if(trace_picture_ctrl_dec)
    fclose(trace_picture_ctrl_dec);
  if(trace_picture_ctrl_pp)
    fclose(trace_picture_ctrl_pp);
  if(trace_motion_vectors)
    fclose(trace_motion_vectors);
  if(trace_dir_mode_mvs)
    fclose(trace_dir_mode_mvs);
  if(trace_final_mvs)
    fclose(trace_final_mvs);
  if(trace_separ_dc)
    fclose(trace_separ_dc);
  if(trace_recon)
    fclose(trace_recon);
  if(trace_scd_out_data)
    fclose(trace_scd_out_data);
  if(trace_bs)
    fclose(trace_bs);
  if(trace_bit_plane_ctrl)
    fclose(trace_bit_plane_ctrl);
  if(trace_transd_first_round)
    fclose(trace_transd_first_round);
  if(trace_inter_out_data)
    fclose(trace_inter_out_data);
  if(trace_overlap_smooth)
    fclose(trace_overlap_smooth);
  if(trace_stream)
    fclose(trace_stream);
  if(trace_stream_ctrl)
    fclose(trace_stream_ctrl);
  if(trace_sw_reg_access)
    fclose(trace_sw_reg_access);
  if(trace_qtables)
    fclose(trace_qtables);
  if(trace_intra4x4_modes)
    fclose(trace_intra4x4_modes);
  if(trace_dct_out_data)
    fclose(trace_dct_out_data);
  if(trace_inter_ref_y)
    fclose(trace_inter_ref_y);
  if(trace_inter_ref_y1)
    fclose(trace_inter_ref_y1);
  if(trace_inter_ref_cb)
    fclose(trace_inter_ref_cb);
  if(trace_inter_ref_cb1)
    fclose(trace_inter_ref_cb1);
  if(trace_inter_ref_cr)
    fclose(trace_inter_ref_cr);
  if(trace_inter_ref_cr1)
    fclose(trace_inter_ref_cr1);
  if(trace_overfill)
    fclose(trace_overfill);
  if(trace_overfill1)
    fclose(trace_overfill1);
  if(trace_intra_pred)
    fclose(trace_intra_pred);
  if(trace_h264_pic_id_map)
    fclose(trace_h264_pic_id_map);
  if(trace_residual)
    fclose(trace_residual);
  if(trace_iq)
    fclose(trace_iq);
  if(trace_sequence_ctrl)
    fclose(trace_sequence_ctrl);
  if(trace_jpeg_tables)
    fclose(trace_jpeg_tables);
  if(trace_out)
    fclose(trace_out);
  if(trace_out_tiled)
    fclose(trace_out_tiled);
  if(trace_vc1_filtering_ctrl)
    fclose(trace_vc1_filtering_ctrl);
  if(trace_decoding_tools)
    fclose(trace_decoding_tools);
  if(trace_pp_weights)
    fclose(trace_pp_weights);
  if(trace_pp_color_conv_r)
    fclose(trace_pp_color_conv_r);
  if(trace_pp_color_conv_g)
    fclose(trace_pp_color_conv_g);
  if(trace_pp_color_conv_b)
    fclose(trace_pp_color_conv_b);
  if(trace_pp_scaling_r)
    fclose(trace_pp_scaling_r);
  if(trace_pp_scaling_g)
    fclose(trace_pp_scaling_g);
  if(trace_pp_scaling_b)
    fclose(trace_pp_scaling_b);
  if(trace_pp_scaling_kernel)
    fclose(trace_pp_scaling_kernel);
  if(trace_pp_rotation)
    fclose(trace_pp_rotation);
  if(trace_pp_crop)
    fclose(trace_pp_crop);
  if(trace_pp_deint_in)
    fclose(trace_pp_deint_in);
  if(trace_pp_deint_out_y)
    fclose(trace_pp_deint_out_y);
  if(trace_pp_deint_out_cb)
    fclose(trace_pp_deint_out_cb);
  if(trace_pp_deint_out_cr)
    fclose(trace_pp_deint_out_cr);
  if(trace_pp_background)
    fclose(trace_pp_background);
  if(trace_pp_in)
    fclose(trace_pp_in);
  if(trace_pp_in_bot)
    fclose(trace_pp_in_bot);
  if(trace_pp_out)
    fclose(trace_pp_out);
  if(trace_pp_ablend1)
    fclose(trace_pp_ablend1);
  if(trace_pp_ablend2)
    fclose(trace_pp_ablend2);
  if(trace_cabac_table[0])
    fclose(trace_cabac_table[0]);
  if(trace_cabac_table[1])
    fclose(trace_cabac_table[1]);
  if(trace_cabac_table[2])
    fclose(trace_cabac_table[2]);
  if(trace_cabac_table[3])
    fclose(trace_cabac_table[3]);
  if(trace_cabac_bin)
    fclose(trace_cabac_bin);
  if(trace_cabac_ctx)
    fclose(trace_cabac_ctx);
  if(trace_neighbour_mv)
    fclose(trace_neighbour_mv);
  if(trace_ic_sets)
    fclose(trace_ic_sets);
  if(trace_ref_bufferd_pic_ctrl)
    fclose(trace_ref_bufferd_pic_ctrl);
  if(trace_ref_bufferd_ctrl)
    fclose(trace_ref_bufferd_ctrl);
  if(trace_ref_bufferd_pic_y)
    fclose(trace_ref_bufferd_pic_y);
  if(trace_ref_bufferd_pic_cb_cr)
    fclose(trace_ref_bufferd_pic_cb_cr);
  if(trace_scaling_out)
    fclose(trace_scaling_out);
  if(trace_custom_idct)
    fclose(trace_custom_idct);
  if(trace_inter_out_y)
    fclose(trace_inter_out_y);
  if(trace_inter_out_y1)
    fclose(trace_inter_out_y1);
  if(trace_inter_out_cb)
    fclose(trace_inter_out_cb);
  if(trace_inter_out_cb1)
    fclose(trace_inter_out_cb1);
  if(trace_inter_out_cr)
    fclose(trace_inter_out_cr);
  if(trace_inter_out_cr1)
    fclose(trace_inter_out_cr1);
  if(trace_filter_internal)
    fclose(trace_filter_internal);

  if (trace_slice_sizes)
    fclose(trace_slice_sizes);
  if (trace_scaling_lists)
    fclose(trace_scaling_lists);
  if (trace_pic_ord_cnts)
    fclose(trace_pic_ord_cnts);
  if (trace_pjpeg_coeffs)
    fclose(trace_pjpeg_coeffs);
  if (trace_pjpeg_coeffs_hex)
    fclose(trace_pjpeg_coeffs_hex);
  if (trace_rv_mvd_bits)
    fclose(trace_rv_mvd_bits);
  if (trace_out2nd_ch)
    fclose(trace_out2nd_ch);
  if (trace_busload)
    fclose(trace_busload);
  if (trace_segmentation)
    fclose(trace_segmentation);
  if (trace_vp78_above_cbf)
    fclose(trace_vp78_above_cbf);
}

/*------------------------------------------------------------------------------
 *   Function : trace_SequenceCtrl
 * ---------------------------------------------------------------------------*/
void trace_SequenceCtrl(u32 nmb_of_pics, u32 b_frames) {
  if(!trace_sequence_ctrl)
    return;

  fprintf(trace_sequence_ctrl, "%d\tAmount of pictures in the sequence\n",
          nmb_of_pics);
  fprintf(trace_sequence_ctrl, "%d\tSequence includes B frames", b_frames);
}

void trace_SequenceCtrlPp(u32 nmb_of_pics) {
  if(!trace_sequence_ctrl_pp)
    return;

  fprintf(trace_sequence_ctrl_pp, "%d\tAmount of pictures in the testcase\n",
          nmb_of_pics);

}

static void traceDecodingMode(FILE * trc, enum TraceDecodingMode dec_mode) {
  switch (dec_mode) {
  case TRACE_H263:
    fprintf(trc, "# H.263\n");
    break;
  case TRACE_H264:
    fprintf(trc, "# H.264\n");
    break;
  case TRACE_MPEG1:
    fprintf(trc, "# MPEG-1\n");
    break;
  case TRACE_MPEG2:
    fprintf(trc, "# MPEG-2\n");
    break;
  case TRACE_MPEG4:
    fprintf(trc, "# MPEG-4\n");
    break;
  case TRACE_VC1:
    fprintf(trc, "# VC-1\n");
    break;
  case TRACE_JPEG:
    fprintf(trc, "# JPEG\n");
    break;
  default:
    fprintf(trc, "# UNKNOWN\n");
    break;
  }
}

static void tracePicCodingType(FILE * trc, char *name,
                               struct TracePicCodingType * pic_coding_type) {
  fprintf(trc, "%d    I-%s\n", pic_coding_type->i_coded, name);
  fprintf(trc, "%d    P-%s\n", pic_coding_type->p_coded, name);
  fprintf(trc, "%d    B-%s\n", pic_coding_type->b_coded, name);
}

static void traceSequenceType(FILE * trc, struct TraceSequenceType * sequence_type) {
  fprintf(trc, "%d    Interlaced sequence type\n", sequence_type->interlaced);
  fprintf(trc, "%d    Progressive sequence type\n",
          sequence_type->progressive);
}

void trace_RefbufferHitrate() {
  printf("Refbuffer hit percentage: %d%%\n", trace_ref_buff_hit_rate);
}

void trace_MPEG2DecodingTools() {
  if(!trace_decoding_tools)
    return;

  traceDecodingMode(trace_decoding_tools,
                    trace_mpeg2_dec_tools.decoding_mode);

  tracePicCodingType(trace_decoding_tools, "Picture",
                     &trace_mpeg2_dec_tools.pic_coding_type);

  fprintf(trace_decoding_tools, "%d    D-Picture\n",
          trace_mpeg2_dec_tools.d_coded);

  traceSequenceType(trace_decoding_tools,
                    &trace_mpeg2_dec_tools.sequence_type);
}

void trace_H264DecodingTools() {
  if(!trace_decoding_tools)
    return;

  traceDecodingMode(trace_decoding_tools,
                    trace_h264_dec_tools.decoding_mode);

  tracePicCodingType(trace_decoding_tools, "Slice",
                     &trace_h264_dec_tools.pic_coding_type);

  fprintf(trace_decoding_tools, "%d    Multiple slices per picture\n",
          trace_h264_dec_tools.multiple_slices_per_picture);

  fprintf(trace_decoding_tools, "%d    Baseline profile\n",
          trace_h264_dec_tools.profile_type.baseline);

  fprintf(trace_decoding_tools, "%d    Main profile\n",
          trace_h264_dec_tools.profile_type.main);

  fprintf(trace_decoding_tools, "%d    High profile\n",
          trace_h264_dec_tools.profile_type.high);

  fprintf(trace_decoding_tools, "%d    I-PCM MB type\n",
          trace_h264_dec_tools.ipcm);

  fprintf(trace_decoding_tools, "%d    Direct mode MB type\n",
          trace_h264_dec_tools.direct_mode);

  fprintf(trace_decoding_tools, "%d    Monochrome\n",
          trace_h264_dec_tools.monochrome);

  fprintf(trace_decoding_tools, "%d    8x8 transform\n",
          trace_h264_dec_tools.transform8x8);

  fprintf(trace_decoding_tools, "%d    8x8 intra prediction\n",
          trace_h264_dec_tools.intra_prediction8x8);

  fprintf(trace_decoding_tools, "%d    Scaling list present\n",
          trace_h264_dec_tools.scaling_list_present);

  fprintf(trace_decoding_tools, "%d    Scaling list present (SPS)\n",
          trace_h264_dec_tools.scaling_matrix_present_type.seq);

  fprintf(trace_decoding_tools, "%d    Scaling list present (PPS)\n",
          trace_h264_dec_tools.scaling_matrix_present_type.pic);

  fprintf(trace_decoding_tools,
          "%d    Weighted prediction, explicit, P slice\n",
          trace_h264_dec_tools.weighted_prediction_type.explicit);

  fprintf(trace_decoding_tools,
          "%d    Weighted prediction, explicit, B slice\n",
          trace_h264_dec_tools.weighted_prediction_type.explicit_b);

  fprintf(trace_decoding_tools, "%d    Weighted prediction, implicit\n",
          trace_h264_dec_tools.weighted_prediction_type.implicit);

  fprintf(trace_decoding_tools, "%d    In-loop filter control present\n",
          trace_h264_dec_tools.loop_filter);

  fprintf(trace_decoding_tools,
          "%d    Disable Loop filter in slice header\n",
          trace_h264_dec_tools.loop_filter_dis);

  fprintf(trace_decoding_tools, "%d    CAVLC entropy coding\n",
          trace_h264_dec_tools.entropy_coding.cavlc);

  fprintf(trace_decoding_tools, "%d    CABAC entropy coding\n",
          trace_h264_dec_tools.entropy_coding.cabac);

  if(trace_h264_dec_tools.seq_type.ilaced == 0)
    trace_h264_dec_tools.seq_type.prog = 1;
  fprintf(trace_decoding_tools, "%d    Progressive sequence type\n",
          trace_h264_dec_tools.seq_type.prog);

  fprintf(trace_decoding_tools, "%d    Interlace sequence type\n",
          trace_h264_dec_tools.seq_type.ilaced);

  fprintf(trace_decoding_tools, "%d    PicAff\n",
          trace_h264_dec_tools.interlace_type.pic_aff);

  fprintf(trace_decoding_tools, "%d    MbAff\n",
          trace_h264_dec_tools.interlace_type.mb_aff);

  fprintf(trace_decoding_tools, "%d    NAL unit stream\n",
          trace_h264_dec_tools.stream_mode.nal_unit_strm);

  fprintf(trace_decoding_tools, "%d    Byte stream\n",
          trace_h264_dec_tools.stream_mode.byte_strm);

  fprintf(trace_decoding_tools, "%d    More than 1 slice groups (FMO)\n",
          trace_h264_dec_tools.slice_groups);

  fprintf(trace_decoding_tools, "%d    Arbitrary slice order (ASO)\n",
          trace_h264_dec_tools.arbitrary_slice_order);

  fprintf(trace_decoding_tools, "%d    Redundant slices\n",
          trace_h264_dec_tools.redundant_slices);

  fprintf(trace_decoding_tools, "%d    Image cropping\n",
          trace_h264_dec_tools.image_cropping);

  fprintf(trace_decoding_tools, "%d    Error stream\n",
          trace_h264_dec_tools.error);
}

void trace_JpegDecodingTools() {
  if(!trace_decoding_tools)
    return;

  traceDecodingMode(trace_decoding_tools,
                    trace_jpeg_dec_tools.decoding_mode);

  fprintf(trace_decoding_tools, "%d     YCbCr 4:2:0 sampling\n",
          trace_jpeg_dec_tools.sampling_4_2_0);

  fprintf(trace_decoding_tools, "%d     YCbCr 4:2:2 sampling\n",
          trace_jpeg_dec_tools.sampling_4_2_2);

  fprintf(trace_decoding_tools, "%d     YCbCr 4:0:0 sampling\n",
          trace_jpeg_dec_tools.sampling_4_0_0);

  fprintf(trace_decoding_tools, "%d     YCbCr 4:4:0 sampling\n",
          trace_jpeg_dec_tools.sampling_4_4_0);

  fprintf(trace_decoding_tools, "%d     YCbCr 4:1:1 sampling\n",
          trace_jpeg_dec_tools.sampling_4_1_1);

  fprintf(trace_decoding_tools, "%d     YCbCr 4:4:4 sampling\n",
          trace_jpeg_dec_tools.sampling_4_4_4);

  fprintf(trace_decoding_tools, "%d     JPEG compressed thumbnail\n",
          trace_jpeg_dec_tools.thumbnail);

  fprintf(trace_decoding_tools, "%d     Progressive decoding\n",
          trace_jpeg_dec_tools.progressive);
}

void trace_MPEG4DecodingTools() {
  if(!trace_decoding_tools)
    return;

  traceDecodingMode(trace_decoding_tools,
                    trace_mpeg4_dec_tools.decoding_mode);

  tracePicCodingType(trace_decoding_tools, "VOP",
                     &trace_mpeg4_dec_tools.pic_coding_type);

  traceSequenceType(trace_decoding_tools,
                    &trace_mpeg4_dec_tools.sequence_type);

  fprintf(trace_decoding_tools, "%d    4-MV\n",
          trace_mpeg4_dec_tools.four_mv);

  fprintf(trace_decoding_tools, "%d    AC/DC prediction\n",
          trace_mpeg4_dec_tools.ac_pred);

  fprintf(trace_decoding_tools, "%d    Data partition\n",
          trace_mpeg4_dec_tools.data_partition);

  fprintf(trace_decoding_tools,
          "%d    Slice resynchronization / Video packets\n",
          trace_mpeg4_dec_tools.resync_marker);

  fprintf(trace_decoding_tools, "%d    Reversible VLC\n",
          trace_mpeg4_dec_tools.reversible_vlc);

  fprintf(trace_decoding_tools, "%d    Header extension code\n",
          trace_mpeg4_dec_tools.hdr_extension_code);

  fprintf(trace_decoding_tools, "%d    Quantisation Method 1\n",
          trace_mpeg4_dec_tools.q_method1);

  fprintf(trace_decoding_tools, "%d    Quantisation Method 2\n",
          trace_mpeg4_dec_tools.q_method2);

  fprintf(trace_decoding_tools, "%d    Half-pel motion compensation\n",
          trace_mpeg4_dec_tools.half_pel);

  fprintf(trace_decoding_tools, "%d    Quarter-pel motion compensation\n",
          trace_mpeg4_dec_tools.quarter_pel);

  fprintf(trace_decoding_tools, "%d    Short video header\n",
          trace_mpeg4_dec_tools.short_video);
}

void trace_VC1DecodingTools() {
  if(!trace_decoding_tools)
    return;

  traceDecodingMode(trace_decoding_tools,
                    trace_vc1_dec_tools.decoding_mode);

  tracePicCodingType(trace_decoding_tools, "frames",
                     &trace_vc1_dec_tools.pic_coding_type);

  traceSequenceType(trace_decoding_tools,
                    &trace_vc1_dec_tools.sequence_type);

  fprintf(trace_decoding_tools, "%d    Variable-sized transform\n",
          trace_vc1_dec_tools.vs_transform);

  fprintf(trace_decoding_tools, "%d    Overlapped transform\n",
          trace_vc1_dec_tools.overlap_transform);

  fprintf(trace_decoding_tools, "%d    4 motion vectors per macroblock\n",
          trace_vc1_dec_tools.four_mv);

  fprintf(trace_decoding_tools,
          "%d    Quarter-pixel motion compensation Y\n",
          trace_vc1_dec_tools.qpel_luma);

  fprintf(trace_decoding_tools,
          "%d    Quarter-pixel motion compensation C\n",
          trace_vc1_dec_tools.qpel_chroma);

  fprintf(trace_decoding_tools, "%d    Range reduction\n",
          trace_vc1_dec_tools.range_reduction);

  fprintf(trace_decoding_tools, "%d    Intensity compensation\n",
          trace_vc1_dec_tools.intensity_compensation);

  fprintf(trace_decoding_tools, "%d    Multi-resolution\n",
          trace_vc1_dec_tools.multi_resolution);

  fprintf(trace_decoding_tools, "%d    Adaptive macroblock quantization\n",
          trace_vc1_dec_tools.adaptive_mblock_quant);

  fprintf(trace_decoding_tools, "%d    In-loop deblock filtering\n",
          trace_vc1_dec_tools.loop_filter);

  fprintf(trace_decoding_tools, "%d    Range mapping\n",
          trace_vc1_dec_tools.range_mapping);

  fprintf(trace_decoding_tools, "%d    Extended motion vectors\n",
          trace_vc1_dec_tools.extended_mv);
}

void trace_RvDecodingTools() {
  if(!trace_decoding_tools)
    return;

  traceDecodingMode(trace_decoding_tools,
                    trace_rv_dec_tools.decoding_mode);

  tracePicCodingType(trace_decoding_tools, "frames",
                     &trace_rv_dec_tools.pic_coding_type);

}

