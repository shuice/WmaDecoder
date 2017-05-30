#ifndef _OS_SUPPORT_H
#define _OS_SUPPORT_H

#include "Wma_Decoder.h"
/**
 * @file os_support.h
 * miscellaneous OS support macros and functions.
 *
 * - usleep() (Win32, BeOS, OS/2)
 * - floatf() (OS/2)
 * - strcasecmp() (OS/2)
 */

#ifdef __MINGW32__
#  undef DATADIR /* clashes with /usr/include/w32api/objidl.h */
__declspec(dllimport) void __stdcall Sleep(unsigned long dwMilliseconds);
// #  include <windows.h>
#  define usleep(t)    Sleep((t) / 1000)
#endif

#ifdef __BEOS__
#  ifndef usleep
#    include <OS.h>
#    define usleep(t)  snooze((bigtime_t)(t))
#  endif
#endif

//#if defined(CONFIG_OS2)
//#include <stdlib.h>
//static inline int usleep(unsigned int t) { return _sleep2(t / 1000); }
//static inline int strcasecmp(const char* s1, const char* s2) { return stricmp(s1,s2); }
//#endif

#include <stdlib.h>


namespace WMADECODER_NAMESPACE
{




}
#endif /* _OS_SUPPORT_H */
