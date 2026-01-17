/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */
#include <stdarg.h>
#include <windef.h>

/* Do the UNICODE prototyping of WinMain.  Be aware that in winbase.h WinMain is a macro
   defined to wWinMain.  */
int WINAPI wWinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPWSTR lpCmdLine,int nShowCmd);

extern HINSTANCE __mingw_winmain_hInstanceW;
extern LPWSTR __mingw_winmain_lpCmdLineW;
extern DWORD __mingw_winmain_nShowCmdW;

int __cdecl wmain (int, wchar_t **, wchar_t **);

/*
 * This wmain() is a bridge for GUI applications that use wmainCRTStartup
 * but have wWinMain instead of wmain. This file is compiled directly into
 * GUI applications that need it.
 */
/*ARGSUSED*/
int __cdecl wmain (int        __UNUSED_PARAM(flags),
	   wchar_t ** __UNUSED_PARAM(cmdline),
	   wchar_t ** __UNUSED_PARAM(inst))
{
  return (int) wWinMain (__mingw_winmain_hInstanceW, NULL,
			__mingw_winmain_lpCmdLineW, __mingw_winmain_nShowCmdW);
}

/*
 * Dummy main() to satisfy the linker when crtexe.c.obj is pulled in
 * due to CRT section dependencies. This function should never be called
 * in a wWinMain application.
 */
/*ARGSUSED*/
int main (int     __UNUSED_PARAM(flags),
	  char ** __UNUSED_PARAM(cmdline),
	  char ** __UNUSED_PARAM(inst))
{
  /* This should never be reached in a wWinMain app */
  return wmain(0, (wchar_t**)0, (wchar_t**)0);
}
