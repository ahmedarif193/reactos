/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Memory Management Definitions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#pragma once

//
// ARM64 Memory Layout
// Based on standard AArch64 virtual memory layout
//

// System memory starts at the kernel base address
// ARM64 typically uses 0xFFFF000000000000 as kernel base
#define MM_SYSTEM_RANGE_START MmSystemRangeStart

// User space range (lower half of address space)
#define MM_HIGHEST_USER_ADDRESS MmHighestUserAddress
#define MM_USER_PROBE_ADDRESS MmUserProbeAddress

// Kernel segments
#define MM_KSEG0_BASE       MM_SYSTEM_RANGE_START
#define MM_KSEG1_BASE       MM_SYSTEM_RANGE_START
#define MM_KSEG2_BASE       MM_SYSTEM_RANGE_START

// External declarations
extern PVOID MmSystemRangeStart;
extern PVOID MmHighestUserAddress;
extern ULONG64 MmUserProbeAddress;

/* EOF */