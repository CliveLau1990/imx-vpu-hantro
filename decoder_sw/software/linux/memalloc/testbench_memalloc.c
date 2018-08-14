/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <assert.h>

#include "basetype.h"
#include "memalloc.h"

int main(void) {

  i32 memdev_fd = -1;
  i32 memdev_fd_2 = -1;
  i32 memdev_map = -1;
  i32 i = 0, u = 0;
  u32 *virtual1 = MAP_FAILED;
  u32 *virtual2 = MAP_FAILED;

  u32 pgsize = getpagesize();

  const char *memdev = "/tmp/dev/memalloc";
  const char *memmap = "/dev/mem";

  u32 bus_address1 = 0, bus_address2 = 0;
  u32 size = 4096;

  memdev_fd = open(memdev, O_RDWR);
  if (memdev_fd == -1) {
    printf("Failed to open dev: %s\n", memdev);
    goto end1;
  }

  memdev_fd_2 = open(memdev, O_RDWR);
  if (memdev_fd_2 == -1) {
    printf("Failed to open dev: %s\n", memdev);
    goto end1;
  }

  memdev_map = open(memmap, O_RDWR);
  if (memdev_map == -1) {
    printf("Failed to open dev: %s\n", memmap);
    goto end1;
  }

  size = (size + pgsize) & (~(pgsize - 1));

  printf("Hard Reset\n", size);
  /*ioctl(memdev_fd, MEMALLOC_IOCHARDRESET, 0);*/

  for (i = 0; i < 100; i++) {

    ioctl(memdev_fd, MEMALLOC_IOCXGETBUFFER, &bus_address1);
    printf("bus_address1 0x%08x\n", bus_address1);

    ioctl(memdev_fd_2, MEMALLOC_IOCXGETBUFFER, &bus_address2);
    printf("bus_address2 0x%08x\n", bus_address2);

    /* test write stuff in the mem are*/

    if (bus_address1) {

      virtual1 = (u32 *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                             memdev_map, bus_address1);

      printf("Virtual1 %08x\n", virtual1);
    }
    if (virtual1 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        *virtual1 = i + 1;
      }
    } else {
      printf("map failed\n");
    }

    /* test write stuff in the mem are*/

    if (bus_address2) {

      virtual2 = (u32 *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                             memdev_map, bus_address2);

      printf("Virtual2 0x%08x\n", virtual2);
    }
    if (virtual2 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        *virtual2 = i + 2;
      }
    } else {
      printf("map failed\n");
    }

    if (virtual1 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        if (*virtual1 != i + 1) {
          printf("MISMATCH1!\n");
          break;
        }
      }
    }

    if (virtual2 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        if (*virtual2 != i + 2) {
          printf("MISMATCH2!\n");
          break;
        }
      }
    }

    if ((i % 30)) {
      printf("release %d ", size);
      ioctl(memdev_fd, MEMALLOC_IOCSFREEBUFFER, &bus_address1);
      printf("address:\t\t0x%08x\n", bus_address1);
    }

    printf("release %d ", size);
    /*ioctl(memdev_fd_2, MEMALLOC_IOCSFREEBUFFER, &bus_address2);
    printf("address:\t\t0x%08x\n", bus_address2);
    virtual1 = MAP_FAILED;
    virtual2 = MAP_FAILED;*/

    if (!(i % 30)) {
      close(memdev_fd);
      memdev_fd = open(memdev, O_RDWR);
      if (memdev_fd == -1) {
        printf("Failed to open dev: %s\n", memdev);
        goto end1;
      }
    }
    close(memdev_fd_2);
    memdev_fd_2 = open(memdev, O_RDWR);
    if (memdev_fd_2 == -1) {
      printf("Failed to open dev: %s\n", memdev);
      goto end1;
    }
  }

end1:

  close(memdev_fd);
  close(memdev_fd_2);
  close(memdev_map);

  return 0;
}
