/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>

extern HINSTANCE __mingw_winmain_hInstance;
extern LPSTR __mingw_winmain_lpCmdLine;
extern DWORD __mingw_winmain_nShowCmd;

/*
 * This main() is a bridge for GUI applications that use mainCRTStartup
 * but have WinMain instead of main. This file is compiled directly into
 * GUI applications that need it.
 */
/*ARGSUSED*/
int main (int     __UNUSED_PARAM(flags),
	  char ** __UNUSED_PARAM(cmdline),
	  char ** __UNUSED_PARAM(inst))
{
  return (int) WinMain (__mingw_winmain_hInstance, NULL,
			__mingw_winmain_lpCmdLine, __mingw_winmain_nShowCmd);
}

/*
 * Dummy wmain() to satisfy the linker when wcrtexe.c.obj is pulled in
 * due to CRT section dependencies. This function should never be called
 * in a WinMain application.
 */
int __cdecl wmain (int     __UNUSED_PARAM(flags),
	  wchar_t ** __UNUSED_PARAM(cmdline),
	  wchar_t ** __UNUSED_PARAM(inst))
{
  /* This should never be reached in a WinMain app */
  return main(0, (char**)0, (char**)0);
}
