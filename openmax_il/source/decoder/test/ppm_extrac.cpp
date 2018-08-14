/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
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

#include <string>
#include <cstdlib>
#include <cstdio>

using namespace std;

int main(int argc, const char* argv[])
{
    int width  = atoi(argv[2]);
    int height = atoi(argv[3]);
    string str_raw = argv[1];
    string str_ppm = argv[1];
    str_ppm.append(".ppm");

    unsigned int pixel;
    
    FILE* raw = fopen(str_raw.c_str(), "r+b");
    FILE* ppm = fopen(str_ppm.c_str(), "w+b");
    
    fprintf(ppm, "P6\n");
    fprintf(ppm, "# foobar\n");
    fprintf(ppm, "%d %d\n", width, height);
    fprintf(ppm, "255\n");

    for (int i=0; i<width*height; ++i)
    {
        fread(&pixel, 4, 1, raw);
        fputc(pixel >> 16, ppm);
        fputc((pixel >> 8) & 0xFF, ppm);
        fputc(pixel & 0xFF, ppm);
        
    }
    printf("read  %d bytes\n", width * height * 4);
    printf("wrote %d bytes\n", width * height * 3);
    fclose(raw);
    fclose(ppm);
    return 0;
}
