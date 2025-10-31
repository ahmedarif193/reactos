/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/include/vt.h
 * PURPOSE:         Virtual Terminal processing stubs
 */

#pragma once

#include "conio.h"

VOID
NTAPI
ConDrvVtInitializeBuffer(PTEXTMODE_SCREEN_BUFFER ScreenBuffer);

VOID
NTAPI
ConDrvVtInvalidateBufferRgb(PTEXTMODE_SCREEN_BUFFER ScreenBuffer);

NTSTATUS
NTAPI
ConDrvVtWriteConsole(PCONSOLE Console,
                     PTEXTMODE_SCREEN_BUFFER ScreenBuffer,
                     PCWSTR Buffer,
                     ULONG Length,
                     PULONG NumCharsProcessed,
                     PBOOLEAN Handled);
