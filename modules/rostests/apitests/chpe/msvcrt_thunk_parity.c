/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Portable ARM64EC MSVCRT call-transition parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <stddef.h>
#include <stdio.h>

typedef void *(__cdecl *PMEMMOVE)(void *Destination, const void *Source, size_t Length);

int
main(void)
{
    HMODULE Msvcrt;
    PMEMMOVE Memmove;
    CHAR MoveRight[16] = "0123456789";
    CHAR MoveLeft[16] = "0123456789";
    CHAR NoMove[8] = "stable";

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("CHPE_MSVCRT_THUNK_BEGIN\n");

    Msvcrt = LoadLibraryW(L"msvcrt.dll");
    Memmove = Msvcrt ? (PMEMMOVE)GetProcAddress(Msvcrt, "memmove") : NULL;
    if (!Memmove)
    {
        printf("CHPE_MSVCRT_THUNK_FAIL setup module=%p memmove=%p error=%lu\n",
               Msvcrt, Memmove, GetLastError());
        return 1;
    }

    if (Memmove(MoveRight + 2, MoveRight, 8) != MoveRight + 2 ||
        lstrcmpA(MoveRight, "0101234567") != 0 ||
        Memmove(MoveLeft, MoveLeft + 2, 9) != MoveLeft ||
        lstrcmpA(MoveLeft, "23456789") != 0 ||
        Memmove(NoMove + 1, NoMove, 0) != NoMove + 1 ||
        lstrcmpA(NoMove, "stable") != 0)
    {
        printf("CHPE_MSVCRT_THUNK_FAIL call memmove=%p right=%s left=%s zero=%s\n",
               Memmove, MoveRight, MoveLeft, NoMove);
        return 2;
    }

    printf("CHPE_MSVCRT_THUNK_PASS memmove=%p right=%s left=%s zero=%s\n",
           Memmove, MoveRight, MoveLeft, NoMove);
    return 0;
}
