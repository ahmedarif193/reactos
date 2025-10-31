/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/winsrv/consrv/conoutput.h
 * PURPOSE:         Console Output functions
 * PROGRAMMERS:     Jeffrey Morlan
 *                  Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

#define ConSrvGetTextModeBuffer(ProcessData, Handle, Ptr, Access, LockConsole)  \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), NULL,  \
                    (Access), (LockConsole), TEXTMODE_BUFFER)
#define ConSrvGetTextModeBufferAndHandleEntry(ProcessData, Handle, Ptr, Entry, Access, LockConsole) \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), (Entry),                   \
                    (Access), (LockConsole), TEXTMODE_BUFFER)

#define ConSrvGetGraphicsBuffer(ProcessData, Handle, Ptr, Access, LockConsole)  \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), NULL,  \
                    (Access), (LockConsole), GRAPHICS_BUFFER)
#define ConSrvGetGraphicsBufferAndHandleEntry(ProcessData, Handle, Ptr, Entry, Access, LockConsole) \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), (Entry),                   \
                    (Access), (LockConsole), GRAPHICS_BUFFER)

#define ConSrvGetScreenBuffer(ProcessData, Handle, Ptr, Access, LockConsole)    \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), NULL,  \
                    (Access), (LockConsole), SCREEN_BUFFER)
#define ConSrvGetScreenBufferAndHandleEntry(ProcessData, Handle, Ptr, Entry, Access, LockConsole)   \
    ConSrvGetObject((ProcessData), (Handle), (PCONSOLE_IO_OBJECT*)(Ptr), (Entry),                   \
                    (Access), (LockConsole), SCREEN_BUFFER)

#define ConSrvReleaseScreenBuffer(Buff, IsConsoleLocked)    \
    ConSrvReleaseObject(&(Buff)->Header, (IsConsoleLocked))

NTSTATUS ConDrvCreateScreenBuffer(OUT PCONSOLE_SCREEN_BUFFER* Buffer,
                                  IN PCONSOLE Console,
                                  IN HANDLE ProcessHandle OPTIONAL,
                                  IN ULONG BufferType,
                                  IN PVOID ScreenBufferInfo);
VOID NTAPI ConDrvDeleteScreenBuffer(PCONSOLE_SCREEN_BUFFER Buffer);
NTSTATUS NTAPI ConDrvSetConsoleActiveScreenBuffer(IN PCONSOLE Console,
                                                  IN PCONSOLE_SCREEN_BUFFER Buffer);
NTSTATUS NTAPI ConDrvSetConsoleCursorInfo(IN PCONSOLE Console,
                                          IN PTEXTMODE_SCREEN_BUFFER Buffer,
                                          IN PCONSOLE_CURSOR_INFO CursorInfo);
NTSTATUS NTAPI ConDrvSetConsoleCursorPosition(IN PCONSOLE Console,
                                              IN PTEXTMODE_SCREEN_BUFFER Buffer,
                                              IN PCOORD Position);
NTSTATUS NTAPI ConDrvSetConsoleScreenBufferSize(IN PCONSOLE Console,
                                                IN PTEXTMODE_SCREEN_BUFFER Buffer,
                                                IN PCOORD Size);
NTSTATUS NTAPI ConDrvSetConsoleWindowInfo(IN PCONSOLE Console,
                                          IN PTEXTMODE_SCREEN_BUFFER Buffer,
                                          IN BOOLEAN Absolute,
                                          IN PSMALL_RECT WindowRect);
// VOID ConioSetActiveScreenBuffer(PCONSOLE_SCREEN_BUFFER Buffer);

PCONSOLE_SCREEN_BUFFER
ConDrvGetActiveScreenBuffer(IN PCONSOLE Console);

/* EOF */
