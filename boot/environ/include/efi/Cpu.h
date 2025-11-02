/*
 * PROJECT:     ReactOS FreeLoader UEFI Support
 * LICENSE:     BSD-2-Clause (same as upstream EDK2 headers)
 * PURPOSE:     Minimal definitions for the EFI CPU Architectural Protocol
 */

#pragma once

#include <Uefi.h>

/* GUID for the EFI CPU Architectural Protocol. */
#define EFI_CPU_ARCH_PROTOCOL_GUID \
    { 0x26baccb1, 0x6f42, 0x11d4, { 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

struct _EFI_CPU_ARCH_PROTOCOL;

/* Signature of the SetMemoryAttributes method. */
typedef EFI_STATUS (EFIAPI *EFI_CPU_SET_MEMORY_ATTRIBUTES)(
    struct _EFI_CPU_ARCH_PROTOCOL *This,
    EFI_PHYSICAL_ADDRESS BaseAddress,
    UINT64 Length,
    UINT64 Attributes);

/*
 * Minimal representation of EFI_CPU_ARCH_PROTOCOL.
 * Only members accessed by FreeLoader are modelled explicitly.
 */
typedef struct _EFI_CPU_ARCH_PROTOCOL
{
    VOID *FlushDataCache;            /* EFI_CPU_FLUSH_DATA_CACHE */
    VOID *EnableInterrupt;           /* EFI_CPU_ENABLE_INTERRUPT */
    VOID *DisableInterrupt;          /* EFI_CPU_DISABLE_INTERRUPT */
    VOID *GetInterruptState;         /* EFI_CPU_GET_INTERRUPT_STATE */
    VOID *Init;                      /* EFI_CPU_INIT */
    VOID *RegisterInterruptHandler;  /* EFI_CPU_REGISTER_INTERRUPT_HANDLER */
    VOID *GetTimerValue;             /* EFI_CPU_GET_TIMER_VALUE */
    EFI_CPU_SET_MEMORY_ATTRIBUTES SetMemoryAttributes;
    UINT32 NumberOfTimers;
    UINT32 DmaBufferAlignment;
} EFI_CPU_ARCH_PROTOCOL;

