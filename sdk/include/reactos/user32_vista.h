#ifndef _REACTOS_USER32_VISTA_H_
#define _REACTOS_USER32_VISTA_H_

#pragma once

/* Declarations exported by the ReactOS user32_vista compatibility module. */
#if (WINVER < 0x0605)
WINUSERAPI UINT WINAPI GetDpiForSystem(VOID);
WINUSERAPI UINT WINAPI GetDpiForWindow(_In_ HWND hwnd);
WINUSERAPI BOOL WINAPI SetProcessDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT context);
WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI SetThreadDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT context);
#endif

#endif /* _REACTOS_USER32_VISTA_H_ */
