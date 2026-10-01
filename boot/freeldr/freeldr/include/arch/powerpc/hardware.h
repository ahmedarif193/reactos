/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC (little-endian) loader architecture contract
 */

#pragma once

/* PaToVa and VaToPa address the KSEG0 BAT window. */
#ifdef KSEG0_BASE
#undef KSEG0_BASE
#endif
#define KSEG0_BASE PPC_LOADER_KSEG0_BASE

#define TAG_HW_RESOURCE_LIST 'lRwH'
#define TAG_HW_DISK_CONTEXT  'cDwH'

#define PPC_PAGE_SHIFT 12
#define PPC_PAGE_SIZE  (1UL << PPC_PAGE_SHIFT)

/* MSR bits used by the loader. */
#define PPC_MSR_EE  0x00008000UL
#define PPC_MSR_PR  0x00004000UL
#define PPC_MSR_FP  0x00002000UL
#define PPC_MSR_ME  0x00001000UL
#define PPC_MSR_IP  0x00000040UL
#define PPC_MSR_IR  0x00000020UL
#define PPC_MSR_DR  0x00000010UL
#define PPC_MSR_ILE 0x00010000UL
#define PPC_MSR_RI  0x00000002UL
#define PPC_MSR_LE  0x00000001UL

#ifndef __ASM__

#include <arch/powerpc/ofw.h>

VOID __cdecl Reboot(VOID);

VOID
StallExecutionProcessor(
    _In_ ULONG Microseconds);

UCHAR
DriveMapGetBiosDriveNumber(
    _In_z_ PCSTR DeviceName);

BOOLEAN
PpcLoaderSetupSucceeded(VOID);

BOOLEAN
PpcFinalizePageTables(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

DECLSPEC_NORETURN
VOID
PpcJumpToKernel(
    _In_ ULONG_PTR KernelEntry,
    _In_ ULONG_PTR LoaderBlock,
    _In_ ULONG_PTR KernelStack);

/* Physical window of the machine I/O space as seen while Open Firmware
 * still owns the MMU (1:1). */
ULONG
PpcGetIoBase(VOID);

VOID
PpcFlushCacheRange(
    _In_ PVOID Address,
    _In_ SIZE_T Length);

#endif /* !__ASM__ */
