/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 bug check support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#include <freeldr.h>
#include <debug.h>

/* Stub implementation for AMD64 */
VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PSTR Format,
    ...)
{
    va_list argptr;
    char Buffer[256];
    
    /* Print the bug check header */
    DbgPrint("\n*** FREELDR BUG CHECK ***\n");
    DbgPrint("Code: 0x%08lx\n", BugCode);
    DbgPrint("File: %s\n", File);
    DbgPrint("Line: %lu\n", Line);
    
    /* Format and print the message */
    if (Format)
    {
        va_start(argptr, Format);
        vsprintf(Buffer, Format, argptr);
        va_end(argptr);
        DbgPrint("Message: %s\n", Buffer);
    }
    
    /* Hang the system */
    DbgPrint("\nSystem halted!\n");
    for (;;)
    {
        __asm__ __volatile__("hlt");
    }
}