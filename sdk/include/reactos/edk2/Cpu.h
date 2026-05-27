/*
 * PROJECT:     ReactOS UEFI Support
 * LICENSE:     BSD-2-Clause (same as upstream EDK2 headers)
 * PURPOSE:     EFI CPU Architectural Protocol definitions
 */

#pragma once

#include <Uefi.h>

#define EFI_CPU_ARCH_PROTOCOL_GUID \
    { 0x26baccb1, 0x6f42, 0x11d4, { 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

struct _EFI_CPU_ARCH_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_CPU_SET_MEMORY_ATTRIBUTES)(
    struct _EFI_CPU_ARCH_PROTOCOL *This,
    EFI_PHYSICAL_ADDRESS BaseAddress,
    UINT64 Length,
    UINT64 Attributes);

typedef struct _EFI_CPU_ARCH_PROTOCOL
{
    VOID *FlushDataCache;
    VOID *EnableInterrupt;
    VOID *DisableInterrupt;
    VOID *GetInterruptState;
    VOID *Init;
    VOID *RegisterInterruptHandler;
    VOID *GetTimerValue;
    EFI_CPU_SET_MEMORY_ATTRIBUTES SetMemoryAttributes;
    UINT32 NumberOfTimers;
    UINT32 DmaBufferAlignment;
} EFI_CPU_ARCH_PROTOCOL;
