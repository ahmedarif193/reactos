#pragma once

/* -----------------------------------------------------------------------
 * taskmgrv2.h — Top-level globals and helpers shared by all modules.
 * ----------------------------------------------------------------------- */

extern HINSTANCE g_hInst;
extern HWND      g_hMainWnd;
extern HANDLE    g_hMutex;

int  TmgrV2_Run(HINSTANCE hInst, LPWSTR lpCmdLine, int nCmdShow);
void TmgrV2_LoadSettings(void);
void TmgrV2_SaveSettings(void);

/* Last-resort error popup that doesn't depend on the shell window. */
void TmgrV2_FatalError(LPCWSTR title, LPCWSTR fmt, ...);
