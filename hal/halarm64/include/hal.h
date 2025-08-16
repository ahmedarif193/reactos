/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     HAL Header for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#pragma once

/* C Headers */
#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/haltypes.h>
#include <ndk/ketypes.h>

/* ARM64 specific definitions */
#define HALP_GIC_DISTRIBUTOR_BASE  0x08000000
#define HALP_GIC_CPU_INTERFACE_BASE 0x08010000
#define HALP_GIC_REDISTRIBUTOR_BASE 0x080A0000

/* GICv3 definitions */
typedef struct _GIC_DISTRIBUTOR
{
    ULONG Control;          // 0x000
    ULONG TypeRegister;     // 0x004
    ULONG DistIdent;        // 0x008
    ULONG Reserved0[29];    // 0x00C-0x07C
    ULONG SetEnable[32];    // 0x100-0x17C
    ULONG ClearEnable[32];  // 0x180-0x1FC
    ULONG SetPending[32];   // 0x200-0x27C
    ULONG ClearPending[32]; // 0x280-0x2FC
    ULONG Active[32];       // 0x300-0x37C
    ULONG Reserved1[32];    // 0x380-0x3FC
    ULONG Priority[255];    // 0x400-0x7FC
    ULONG Reserved2;        // 0x7FC
    ULONG Target[255];      // 0x800-0xBFC
    ULONG Reserved3;        // 0xBFC
    ULONG Config[64];       // 0xC00-0xCFC
    ULONG Reserved4[64];    // 0xD00-0xDFC
    ULONG SoftwareInt;      // 0xF00
} GIC_DISTRIBUTOR, *PGIC_DISTRIBUTOR;

typedef struct _GIC_CPU_INTERFACE
{
    ULONG Control;          // 0x00
    ULONG PriorityMask;     // 0x04
    ULONG BinaryPoint;      // 0x08
    ULONG IntAck;           // 0x0C
    ULONG EndOfInt;         // 0x10
    ULONG RunningPriority;  // 0x14
    ULONG HighestPending;   // 0x18
} GIC_CPU_INTERFACE, *PGIC_CPU_INTERFACE;

/* HAL private functions */
VOID NTAPI HalpInitializeGIC(VOID);
VOID NTAPI HalpInitializeTimer(VOID);
VOID NTAPI HalpInitializeInterrupts(VOID);
VOID NTAPI HalpMapPhysicalMemory(VOID);

/* Global HAL data */
extern PGIC_DISTRIBUTOR HalpGicDistributor;
extern PGIC_CPU_INTERFACE HalpGicCpuInterface;

/* EOF */