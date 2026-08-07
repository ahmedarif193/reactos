/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DiscardVirtualMemory
 * COPYRIGHT:   Adapted from Wine dlls/kernelbase/memory.c
 */

#include "k32_vista.h"

DWORD
WINAPI
DiscardVirtualMemory(
    _Inout_ PVOID VirtualAddress,
    _In_ SIZE_T Size)
{
    NTSTATUS Status;
    PVOID BaseAddress = VirtualAddress;
    SIZE_T RegionSize = Size;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &RegionSize, MEM_RESET, PAGE_NOACCESS);
    return RtlNtStatusToDosError(Status);
}
