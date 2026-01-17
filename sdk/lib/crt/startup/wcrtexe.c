/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */

#define WPRFLAG
#define UNICODE
#define _UNICODE
#define mainCRTStartup wmainCRTStartup
#define WinMainCRTStartup wWinMainCRTStartup
#define mingw_pcinit mingw_pwcinit
#define mingw_pcppinit mingw_pwcppinit
#define __mingw_winmain_hInstance __mingw_winmain_hInstanceW
#define __mingw_winmain_lpCmdLine __mingw_winmain_lpCmdLineW
#define __mingw_winmain_nShowCmd __mingw_winmain_nShowCmdW

#include "crtexe.c"
