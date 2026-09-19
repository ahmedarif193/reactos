/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 loader architecture contract
 */

#pragma once

#define RISCV64_PAGE_SHIFT             RISCV64_LOADER_PAGE_SHIFT
#define RISCV64_PAGE_SIZE              RISCV64_LOADER_PAGE_SIZE
#define RISCV64_SV39_MODE              RISCV64_LOADER_SATP_MODE_SV39
#define RISCV64_DIRECT_MAP_BASE        RISCV64_LOADER_DIRECT_MAP_BASE
#define RISCV64_MAX_PHYSICAL_ADDRESS   RISCV64_LOADER_PHYSICAL_LIMIT

/* PaToVa and VaToPa address the KSEG0 window. */
#ifdef KSEG0_BASE
#undef KSEG0_BASE
#endif
#define KSEG0_BASE RISCV64_LOADER_KSEG0_BASE

/* Kernel view of the shared user data page, as in the kernel headers. */
#ifndef KI_USER_SHARED_DATA
#define KI_USER_SHARED_DATA 0xFFFFFFFFFFFE0000ULL
#endif

#ifndef __ASM__

/* Device windows (the early console) the loader maps into the direct map. */
#define RISCV64_MAX_EARLY_DEVICE_RANGES 8

typedef struct _RISCV64_EARLY_DEVICE_RANGE
{
    ULONGLONG BaseAddress;
    ULONGLONG Length;
} RISCV64_EARLY_DEVICE_RANGE, *PRISCV64_EARLY_DEVICE_RANGE;

VOID __cdecl Reboot(VOID);

VOID
StallExecutionProcessor(
    _In_ ULONG Microseconds);

UCHAR
DriveMapGetBiosDriveNumber(
    _In_z_ PCSTR DeviceName);

BOOLEAN
RiscvLoaderSetupSucceeded(VOID);

BOOLEAN
RiscvFinalizePageTables(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

DECLSPEC_NORETURN
VOID
RiscvJumpToKernel(
    _In_ ULONG_PTR KernelEntry,
    _In_ ULONG_PTR LoaderBlock,
    _In_ ULONG_PTR KernelStack);

#endif /* __ASM__ */
