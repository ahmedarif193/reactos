/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Architecture Definitions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#pragma once

/* ARM64 specific definitions */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#ifndef MM_PAGE_SIZE
#define MM_PAGE_SIZE 4096
#endif

/* FreeLoader memory layout for ARM64 */
#define FREELDR_BASE       0x0001F000
#define FREELDR_PE_BASE    0x0001F000
#define MAX_FREELDR_PE_SIZE 0xFFFFFF

/* LOADER_PARAMETER_BLOCK ARM64 extension */
typedef struct _LOADER_BLOCK_ARM64
{
    ULONG_PTR KernelStack;
    ULONG_PTR PanicStack;
    ULONG_PTR InterruptStack;
    ULONG FirstLevelDcacheSize;
    ULONG FirstLevelDcacheFillSize;
    ULONG FirstLevelIcacheSize;
    ULONG FirstLevelIcacheFillSize;
    ULONG SecondLevelDcacheSize;
    ULONG SecondLevelDcacheFillSize;
    ULONG SecondLevelIcacheSize;
    ULONG SecondLevelIcacheFillSize;
} LOADER_BLOCK_ARM64, *PLOADER_BLOCK_ARM64;

/* Page flags for ARM64 - avoid redefining if already defined */
#ifndef PAGE_READWRITE
#define PAGE_READWRITE  0x01
#endif
#ifndef PAGE_EXECUTE
#define PAGE_EXECUTE    0x02
#endif
#ifndef PAGE_NOCACHE
#define PAGE_NOCACHE    0x04
#endif

/* These structures are defined elsewhere, just skip them */

/* Assembly helper functions */
extern VOID InvalidateICache(VOID);
extern VOID InvalidateDCache(VOID);
extern VOID EnableCaches(VOID);
extern VOID DisableCaches(VOID);
extern VOID MmuInit(VOID);
extern VOID MmuEnable(VOID);
extern VOID MmuDisable(VOID);
extern VOID TrapInit(VOID);
extern VOID TimerInit(VOID);

/* Debug output function is declared in debug.h */

/* Timer functions */
extern VOID StallExecutionProcessor(ULONG Microseconds);

/* Boot functions */
extern VOID Reboot(VOID);

/* Boot drive/partition variables - extern for ARM64 */
#ifndef FrldrBootDrive
extern UCHAR FrldrBootDrive;
#endif
#ifndef FrldrBootPartition
extern ULONG FrldrBootPartition;
#endif