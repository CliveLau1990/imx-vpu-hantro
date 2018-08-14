How to enable decoder log in software in android platform ?
1. modify Android.mk to set ENABLE_HANTRO_DEBUG_LOG to true
2. add log tag and redefine macro in the source file which you want to print message through android adb log system.
2.1 Here is an example for Hevcdecapi.c
add them at line 58 of Hevcdecapi.c

#include "util.h"
#include "dbgtrace.h"

#undef LOG_TAG
#define LOG_TAG "hevcdecapi"
#undef DEBUG_PRINT
#define DEBUG_PRINT(args) DBGT_PDEBUG args


2.2 in Mp4decapi.c you need the redifine MP4_API_TRC, the log pring macro is different from Hevcdecapi.c

#include "util.h"
#include "dbgtrace.h"

#undef LOG_TAG
#define LOG_TAG "mp4decapi"
#undef MP4_API_TRC
#define MP4_API_TRC DBGT_PDEBUG

be careful on the log print format. Two types exist in the files and you need to choose one.

3. if you want to use vpu test from hantro, enable it in Android.mk
