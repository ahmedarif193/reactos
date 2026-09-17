/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 loader architecture contract
 */

#pragma once

#define RISCV64_PAGE_SHIFT             RISCV64_LOADER_PAGE_SHIFT
#define RISCV64_PAGE_SIZE              RISCV64_LOADER_PAGE_SIZE
#define RISCV64_SV39_MODE              RISCV64_LOADER_SATP_MODE_SV39
#define RISCV64_PHYSICAL_MAP_BASE      RISCV64_LOADER_DIRECT_MAP_BASE
#define RISCV64_PHYSICAL_MAP_SIZE      RISCV64_LOADER_DIRECT_MAP_SIZE
#define RISCV64_MAX_PHYSICAL_ADDRESS   RISCV64_PHYSICAL_MAP_SIZE

#ifdef KSEG0_BASE
#undef KSEG0_BASE
#endif
#define KSEG0_BASE RISCV64_PHYSICAL_MAP_BASE

/* Fixed user address; the kernel writable view is supplied separately. */
#ifndef KI_USER_SHARED_DATA
#define KI_USER_SHARED_DATA 0x000000007FFE0000ULL
#endif

#ifndef __ASM__

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

VOID
RiscvLoaderZeroSharedUserData(VOID);

DECLSPEC_NORETURN
VOID
RiscvJumpToKernel(
    _In_ ULONG_PTR KernelEntry,
    _In_ ULONG_PTR LoaderBlock,
    _In_ ULONG_PTR KernelStack);

#endif /* __ASM__ */
