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

#include "dwl.h"
#include "basetype.h"
#include "h264hwd_slice_group_map.h"
#include "h264hwd_cfg.h"
#include "h264hwd_pic_param_set.h"
#include "h264hwd_util.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static void DecodeInterleavedMap(
  u32 *map,
  u32 num_slice_groups,
  u32 *run_length,
  u32 pic_size);

static void DecodeDispersedMap(
  u32 *map,
  u32 num_slice_groups,
  u32 pic_width,
  u32 pic_height);

static void DecodeForegroundLeftOverMap(
  u32 *map,
  u32 num_slice_groups,
  u32 *top_left,
  u32 *bottom_right,
  u32 pic_width,
  u32 pic_height);

static void DecodeBoxOutMap(
  u32 *map,
  u32 slice_group_change_direction_flag,
  u32 units_in_slice_group0,
  u32 pic_width,
  u32 pic_height);

static void DecodeRasterScanMap(
  u32 *map,
  u32 slice_group_change_direction_flag,
  u32 size_of_upper_left_group,
  u32 pic_size);

static void DecodeWipeMap(
  u32 *map,
  u32 slice_group_change_direction_flag,
  u32 size_of_upper_left_group,
  u32 pic_width,
  u32 pic_height);

/*------------------------------------------------------------------------------

    Function: DecodeInterleavedMap

        Functional description:
            Function to decode interleaved slice group map type, i.e. slice
            group map type 0.

        Inputs:
            map             pointer to the map
            num_slice_groups  number of slice groups
            run_length       run_length[] values for each slice group
            pic_size         picture size in macroblocks

        Outputs:
            map             slice group map is stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void DecodeInterleavedMap(
  u32 *map,
  u32 num_slice_groups,
  u32 *run_length,
  u32 pic_size) {

  /* Variables */

  u32 i,j, group;

  /* Code */

  ASSERT(map);
  ASSERT(num_slice_groups >= 1 && num_slice_groups <= MAX_NUM_SLICE_GROUPS);
  ASSERT(run_length);

  i = 0;

  do {
    for (group = 0; group < num_slice_groups && i < pic_size;
         i += run_length[group++]) {
      ASSERT(run_length[group] <= pic_size);
      for (j = 0; j < run_length[group] && i + j < pic_size; j++)
        map[i+j] = group;
    }
  } while (i < pic_size);


}

/*------------------------------------------------------------------------------

    Function: DecodeDispersedMap

        Functional description:
            Function to decode dispersed slice group map type, i.e. slice
            group map type 1.

        Inputs:
            map               pointer to the map
            num_slice_groups    number of slice groups
            pic_width          picture width in macroblocks
            pic_height         picture height in macroblocks

        Outputs:
            map               slice group map is stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void DecodeDispersedMap(
  u32 *map,
  u32 num_slice_groups,
  u32 pic_width,
  u32 pic_height) {

  /* Variables */

  u32 i, pic_size;

  /* Code */

  ASSERT(map);
  ASSERT(num_slice_groups >= 1 && num_slice_groups <= MAX_NUM_SLICE_GROUPS);
  ASSERT(pic_width);
  ASSERT(pic_height);

  pic_size = pic_width * pic_height;

  for (i = 0; i < pic_size; i++)
    map[i] = ((i % pic_width) + (((i / pic_width) * num_slice_groups) >> 1)) %
             num_slice_groups;


}

/*------------------------------------------------------------------------------

    Function: DecodeForegroundLeftOverMap

        Functional description:
            Function to decode foreground with left-over slice group map type,
            i.e. slice group map type 2.

        Inputs:
            map               pointer to the map
            num_slice_groups    number of slice groups
            top_left           top_left[] values
            bottom_right       bottom_right[] values
            pic_width          picture width in macroblocks
            pic_height         picture height in macroblocks

        Outputs:
            map               slice group map is stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void DecodeForegroundLeftOverMap(
  u32 *map,
  u32 num_slice_groups,
  u32 *top_left,
  u32 *bottom_right,
  u32 pic_width,
  u32 pic_height) {

  /* Variables */

  u32 i,y,x,y_top_left,y_bottom_right,x_top_left,x_bottom_right, pic_size;
  u32 group;

  /* Code */

  ASSERT(map);
  ASSERT(num_slice_groups >= 1 && num_slice_groups <= MAX_NUM_SLICE_GROUPS);
  ASSERT(top_left);
  ASSERT(bottom_right);
  ASSERT(pic_width);
  ASSERT(pic_height);

  pic_size = pic_width * pic_height;

  for (i = 0; i < pic_size; i++)
    map[i] = num_slice_groups - 1;

  for (group = num_slice_groups - 1; group--; ) {
    ASSERT( top_left[group] <= bottom_right[group] &&
            bottom_right[group] < pic_size );
    y_top_left = top_left[group] / pic_width;
    x_top_left = top_left[group] % pic_width;
    y_bottom_right = bottom_right[group] / pic_width;
    x_bottom_right = bottom_right[group] % pic_width;
    ASSERT(x_top_left <= x_bottom_right);

    for (y = y_top_left; y <= y_bottom_right; y++)
      for (x = x_top_left; x <= x_bottom_right; x++)
        map[ y * pic_width + x ] = group;
  }


}

/*------------------------------------------------------------------------------

    Function: DecodeBoxOutMap

        Functional description:
            Function to decode box-out slice group map type, i.e. slice group
            map type 3.

        Inputs:
            map                               pointer to the map
            slice_group_change_direction_flag     slice_group_change_direction_flag
            units_in_slice_group0                mbs on slice group 0
            pic_width                          picture width in macroblocks
            pic_height                         picture height in macroblocks

        Outputs:
            map                               slice group map is stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void DecodeBoxOutMap(
  u32 *map,
  u32 slice_group_change_direction_flag,
  u32 units_in_slice_group0,
  u32 pic_width,
  u32 pic_height) {

  /* Variables */

  u32 i, k, pic_size;
  i32 x, y, x_dir, y_dir, left_bound, top_bound, right_bound, bottom_bound;
  u32 map_unit_vacant;

  /* Code */

  ASSERT(map);
  ASSERT(pic_width);
  ASSERT(pic_height);

  pic_size = pic_width * pic_height;
  ASSERT(units_in_slice_group0 <= pic_size);

  for (i = 0; i < pic_size; i++)
    map[i] = 1;

  x = (pic_width - (u32)slice_group_change_direction_flag) >> 1;
  y = (pic_height - (u32)slice_group_change_direction_flag) >> 1;

  left_bound = x;
  top_bound = y;

  right_bound = x;
  bottom_bound = y;

  x_dir = (i32)slice_group_change_direction_flag - 1;
  y_dir = (i32)slice_group_change_direction_flag;

  for (k = 0; k < units_in_slice_group0; k += map_unit_vacant ? 1 : 0) {
    map_unit_vacant = (map[ (u32)y * pic_width + (u32)x ] == 1) ? HANTRO_TRUE : HANTRO_FALSE;

    if (map_unit_vacant)
      map[ (u32)y * pic_width + (u32)x ] = 0;

    if (x_dir == -1 && x == left_bound) {
      left_bound = MAX(left_bound - 1, 0);
      x = left_bound;
      x_dir = 0;
      y_dir = 2 * (i32)slice_group_change_direction_flag - 1;
    } else if (x_dir == 1 && x == right_bound) {
      right_bound = MIN(right_bound + 1, (i32)pic_width - 1);
      x = right_bound;
      x_dir = 0;
      y_dir = 1 - 2 * (i32)slice_group_change_direction_flag;
    } else if (y_dir == -1 && y == top_bound) {
      top_bound = MAX(top_bound - 1, 0);
      y = top_bound;
      x_dir = 1 - 2 * (i32)slice_group_change_direction_flag;
      y_dir = 0;
    } else if (y_dir == 1 && y == bottom_bound) {
      bottom_bound = MIN(bottom_bound + 1, (i32)pic_height - 1);
      y = bottom_bound;
      x_dir = 2 * (i32)slice_group_change_direction_flag - 1;
      y_dir = 0;
    } else {
      x += x_dir;
      y += y_dir;
    }
  }


}

/*------------------------------------------------------------------------------

    Function: DecodeRasterScanMap

        Functional description:
            Function to decode raster scan slice group map type, i.e. slice
            group map type 4.

        Inputs:
            map                               pointer to the map
            slice_group_change_direction_flag     slice_group_change_direction_flag
            size_of_upper_left_group              mbs in upperLeftGroup
            pic_size                           picture size in macroblocks

        Outputs:
            map                               slice group map is stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void DecodeRasterScanMap(
  u32 *map,
  u32 slice_group_change_direction_flag,
  u32 size_of_upper_left_group,
  u32 pic_size) {

  /* Variables */

  u32 i;

  /* Code */

  ASSERT(map);
  ASSERT(pic_size);
  ASSERT(size_of_upper_left_group <= pic_size);

  for (i = 0; i < pic_size; i++)
    if (i < size_of_upper_left_group)
      map[i] = (u32)slice_group_change_direction_flag;
    else
      map[i] = 1 - (u32)slice_group_change_direction_flag;


}

/*------------------------------------------------------------------------------

    Function: DecodeWipeMap

        Functional description:
            Function to decode wipe slice group map type, i.e. slice group map
            type 5.

        Inputs:
            slice_group_change_direction_flag     slice_group_change_direction_flag
            size_of_upper_left_group              mbs in upperLeftGroup
            pic_width                          picture width in macroblocks
            pic_height                         picture height in macroblocks

        Outputs:
            map                               slice group map is stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void DecodeWipeMap(
  u32 *map,
  u32 slice_group_change_direction_flag,
  u32 size_of_upper_left_group,
  u32 pic_width,
  u32 pic_height) {

  /* Variables */

  u32 i,j,k;

  /* Code */

  ASSERT(map);
  ASSERT(pic_width);
  ASSERT(pic_height);
  ASSERT(size_of_upper_left_group <= pic_width * pic_height);

  k = 0;
  for (j = 0; j < pic_width; j++)
    for (i = 0; i < pic_height; i++)
      if (k++ < size_of_upper_left_group)
        map[ i * pic_width + j ] = (u32)slice_group_change_direction_flag;
      else
        map[ i * pic_width + j ] = 1 -
                                   (u32)slice_group_change_direction_flag;


}

/*------------------------------------------------------------------------------

    Function: h264bsdDecodeSliceGroupMap

        Functional description:
            Function to decode macroblock to slice group map. Construction
            of different slice group map types is handled by separate
            functions defined above. See standard for details how slice group
            maps are computed.

        Inputs:
            pps                     active picture parameter set
            slice_group_change_cycle   slice_group_change_cycle
            pic_width                picture width in macroblocks
            pic_height               picture height in macroblocks

        Outputs:
            map                     slice group map is stored here

        Returns:
            none

------------------------------------------------------------------------------*/

void h264bsdDecodeSliceGroupMap(
  u32 *map,
  picParamSet_t *pps,
  u32 slice_group_change_cycle,
  u32 pic_width,
  u32 pic_height) {

  /* Variables */

  u32 i, pic_size, units_in_slice_group0 = 0, size_of_upper_left_group = 0;

  /* Code */

  ASSERT(map);
  ASSERT(pps);
  ASSERT(pic_width);
  ASSERT(pic_height);
  ASSERT(pps->slice_group_map_type < 7);

  pic_size = pic_width * pic_height;

  /* just one slice group -> all macroblocks belong to group 0 */
  if (pps->num_slice_groups == 1) {
    (void) DWLmemset(map, 0, pic_size * sizeof(u32));
    return;
  }

  if (pps->slice_group_map_type > 2 && pps->slice_group_map_type < 6) {
    ASSERT(pps->slice_group_change_rate &&
           pps->slice_group_change_rate <= pic_size);

    units_in_slice_group0 =
      MIN(slice_group_change_cycle * pps->slice_group_change_rate, pic_size);

    if (pps->slice_group_map_type == 4 || pps->slice_group_map_type == 5)
      size_of_upper_left_group = pps->slice_group_change_direction_flag ?
                                 (pic_size - units_in_slice_group0) : units_in_slice_group0;
  }

  switch (pps->slice_group_map_type) {
  case 0:
    DecodeInterleavedMap(map, pps->num_slice_groups,
                         pps->run_length, pic_size);
    break;

  case 1:
    DecodeDispersedMap(map, pps->num_slice_groups, pic_width,
                       pic_height);
    break;

  case 2:
    DecodeForegroundLeftOverMap(map, pps->num_slice_groups,
                                pps->top_left, pps->bottom_right, pic_width, pic_height);
    break;

  case 3:
    DecodeBoxOutMap(map, pps->slice_group_change_direction_flag,
                    units_in_slice_group0, pic_width, pic_height);
    break;

  case 4:
    DecodeRasterScanMap(map,
                        pps->slice_group_change_direction_flag, size_of_upper_left_group,
                        pic_size);
    break;

  case 5:
    DecodeWipeMap(map, pps->slice_group_change_direction_flag,
                  size_of_upper_left_group, pic_width, pic_height);
    break;

  default:
    ASSERT(pps->slice_group_id);
    for (i = 0; i < pic_size; i++) {
      ASSERT(pps->slice_group_id[i] < pps->num_slice_groups);
      map[i] = pps->slice_group_id[i];
    }
    break;
  }

}
