/*
 * PROJECT:        ReactOS Kernel
 * LICENSE:        GNU GPLv2 only as published by the Free Software Foundation
 * PURPOSE:        AHCI Miniport (SCSIPORT skeleton)
 */

#pragma once

#include <ntddk.h>
#include <scsi.h>
#include <scsiwmi.h>
#include <srb.h>

/* Basic tags and constants */
#define AHCI_TAG 'IHCA'

/* Minimal AHCI HBA register layout */
typedef struct _AHCI_HBA_MEM {
    volatile ULONG CAP;   /* 0x00: HBA Capabilities */
    volatile ULONG GHC;   /* 0x04: Global Host Control */
    volatile ULONG IS;    /* 0x08: Interrupt Status */
    volatile ULONG PI;    /* 0x0C: Ports Implemented */
    volatile ULONG VS;    /* 0x10: Version */
} AHCI_HBA_MEM, *PAHCI_HBA_MEM;

/* Per-port register offsets (base at 0x100 + port*0x80) */
#define AHCI_PORT_STRIDE        0x80
#define AHCI_PORT_REG_BASE      0x100
#define AHCI_PxCLB              0x00
#define AHCI_PxCLBU             0x04
#define AHCI_PxFB               0x08
#define AHCI_PxFBU              0x0C
#define AHCI_PxIS               0x10
#define AHCI_PxIE               0x14
#define AHCI_PxCMD              0x18
#define AHCI_PxSSTS             0x28
#define AHCI_PxSERR             0x30

/* GHC bits */
#define AHCI_GHC_IE             0x00000002

/* Command header flags (simplified) */
#define AHCI_CMDH_CFL_MASK      0x1F    /* Command FIS length in DWORDs */
#define AHCI_CMDH_W             (1 << 6)
#define AHCI_CMDH_P             (1 << 5)
#define AHCI_CMDH_R             (1 << 3)

/* Simplified AHCI command header and PRDT entry (doc: AHCI spec 1.3.1) */
typedef struct _AHCI_CMD_HEADER {
    USHORT Flags;        /* bits: CFL, A, W, P, R, B, C, PMPort */
    USHORT PRDTL;        /* PRDT length (# entries) */
    ULONG  PRDBC;        /* PRDT byte count */
    ULONG  CTBA;         /* Command table base (low) */
    ULONG  CTBAU;        /* Command table base (high) */
    ULONG  Reserved[4];
} AHCI_CMD_HEADER, *PAHCI_CMD_HEADER;

typedef struct _AHCI_PRDT_ENTRY {
    ULONG DBA;           /* Data base address (low) */
    ULONG DBAU;          /* Data base address (high) */
    ULONG Reserved;      /* reserved */
    ULONG DBC_I;         /* bits 0..21: byte count-1; bit31: interrupt */
} AHCI_PRDT_ENTRY, *PAHCI_PRDT_ENTRY;

/* Minimal command table layout */
typedef struct _AHCI_CMD_TABLE {
    UCHAR CFIS[64];
    UCHAR ACMD[16];
    UCHAR Reserved[48];
    AHCI_PRDT_ENTRY PRDT[1]; /* variable length */
} AHCI_CMD_TABLE, *PAHCI_CMD_TABLE;

typedef struct _AHCI_ADAPTER_EXTENSION {
    PAHCI_HBA_MEM AbBase;     /* Mapped ABAR base */
    ULONG Cap;
    ULONG Version;
    ULONG PortsImplemented;
    UCHAR PortCount;
    /* Non-cached buffers */
    PVOID NonCachedBase;
    ULONG NonCachedBytes;
    PVOID CommandList[32];
    PVOID ReceivedFIS[32];
} AHCI_ADAPTER_EXTENSION, *PAHCI_ADAPTER_EXTENSION;

static __inline BOOLEAN
AhciIsPortDevicePresent(_In_ PAHCI_ADAPTER_EXTENSION Adapter, _In_ ULONG Port)
{
    volatile ULONG *pxssts;
    ULONG ssts;
    if (Adapter == NULL || Adapter->AbBase == NULL) return FALSE;
    pxssts = (volatile ULONG *)((PUCHAR)Adapter->AbBase + AHCI_PORT_REG_BASE + Port * AHCI_PORT_STRIDE + AHCI_PxSSTS);
    ssts = *pxssts;
    /* DET: bits 0..3; 3 = device present, phy communication established */
    return ((ssts & 0x0F) == 0x03);
}

#define AHCI_ALIGN_UP(Value, Align)   (((Value) + ((Align) - 1)) & ~((Align) - 1))


typedef struct _AHCI_SRB_EXTENSION {
    ULONG Placeholder;
} AHCI_SRB_EXTENSION, *PAHCI_SRB_EXTENSION;
