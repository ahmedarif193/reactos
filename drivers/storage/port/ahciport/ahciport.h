/*
 * PROJECT:        ReactOS Kernel
 * LICENSE:        GNU GPLv2 only as published by the Free Software Foundation
 * PURPOSE:        AHCI Miniport (SCSIPORT implementation)
 */

#pragma once

#include <ntddk.h>
#include <ntddscsi.h>

#ifdef AHCI_USE_STORPORT
#include <storport.h>
#ifndef SCSI_ADSENSE_NO_SENSE
#define SCSI_ADSENSE_NO_SENSE 0x00
#endif
#ifndef SERVICE_ACTION_READ_CAPACITY16
#define SERVICE_ACTION_READ_CAPACITY16 0x10
#endif
typedef STOR_PHYSICAL_ADDRESS SCSI_PHYSICAL_ADDRESS;
typedef struct _SCSI_SUPPORTED_CONTROL_TYPE_LIST {
    ULONG MaxControlType;
    BOOLEAN SupportedTypeList[1];
} SCSI_SUPPORTED_CONTROL_TYPE_LIST, *PSCSI_SUPPORTED_CONTROL_TYPE_LIST;
#else
#include <scsi.h>
#include <scsiwmi.h>
#include <srb.h>
#endif

#include "ahciport_compat.h"

#ifndef AHCI_ENABLE_TRACE
#define AHCI_ENABLE_TRACE 0
#endif

#if AHCI_ENABLE_TRACE
#define AHCI_TRACE(fmt, ...) \
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, \
               "ahciport: " fmt "\n", ##__VA_ARGS__))
#else
#define AHCI_TRACE(fmt, ...) (void)0
#endif

#if AHCI_ENABLE_TRACE
#define AHCI_TRACE_PRINT(...) DbgPrint(__VA_ARGS__)
#else
#define AHCI_TRACE_PRINT(...) (void)0
#endif

#define AHCI_WARN(fmt, ...) \
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, \
               "ahciport: " fmt "\n", ##__VA_ARGS__))

#define AHCI_ERROR(fmt, ...) \
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, \
               "ahciport: " fmt "\n", ##__VA_ARGS__))

#ifdef AHCI_USE_STORPORT
#define AHCI_READ_REG32(Adapter, RegPtr) StorPortReadRegisterUlong((Adapter), (PULONG)(RegPtr))
#define AHCI_WRITE_REG32(Adapter, RegPtr, Value) StorPortWriteRegisterUlong((Adapter), (PULONG)(RegPtr), (Value))
#else
#define AHCI_READ_REG32(Adapter, RegPtr) ScsiPortReadRegisterUlong((PULONG)(RegPtr))
#define AHCI_WRITE_REG32(Adapter, RegPtr, Value) ScsiPortWriteRegisterUlong((PULONG)(RegPtr), (Value))
#endif

/* Basic tags and constants */
#define AHCI_TAG 'IHCA'
#define AHCI_MAX_PORTS 32
#define AHCI_MAX_PRDT_ENTRIES 32
#define AHCI_CMD_TABLE_ALLOC_SIZE \
    (sizeof(AHCI_CMD_TABLE) + ((AHCI_MAX_PRDT_ENTRIES - 1) * sizeof(AHCI_PRDT_ENTRY)))

/* Minimal AHCI HBA register layout */
typedef struct _AHCI_HBA_MEM {
    volatile ULONG CAP;        /* 0x00: HBA Capabilities */
    volatile ULONG GHC;        /* 0x04: Global Host Control */
    volatile ULONG IS;         /* 0x08: Interrupt Status */
    volatile ULONG PI;         /* 0x0C: Ports Implemented */
    volatile ULONG VS;         /* 0x10: Version */
    volatile ULONG CCC_CTL;    /* 0x14 */
    volatile ULONG CCC_PORTS;  /* 0x18 */
    volatile ULONG EM_LOC;     /* 0x1C */
    volatile ULONG EM_CTL;     /* 0x20 */
    volatile ULONG CAP2;       /* 0x24 */
    volatile ULONG BOHC;       /* 0x28 */
} AHCI_HBA_MEM, *PAHCI_HBA_MEM;

/* Per-port register offsets (base at 0x100 + port*0x80) */
#define AHCI_PORT_REG_BASE      0x100
#define AHCI_PORT_STRIDE        0x80
#define AHCI_PxCLB              0x00
#define AHCI_PxCLBU             0x04
#define AHCI_PxFB               0x08
#define AHCI_PxFBU              0x0C
#define AHCI_PxIS               0x10
#define AHCI_PxIE               0x14
#define AHCI_PxCMD              0x18
#define AHCI_PxTFD              0x20
#define AHCI_PxSIG              0x24
#define AHCI_PxSSTS             0x28
#define AHCI_PxSCTL             0x2C
#define AHCI_PxSERR             0x30
#define AHCI_PxSACT             0x34
#define AHCI_PxCI               0x38
#define AHCI_PxSNTF             0x3C

/* Global Host Control bits */
#define AHCI_GHC_HR             0x00000001
#define AHCI_GHC_IE             0x00000002
#define AHCI_GHC_AE             0x80000000

/* PxCMD bits */
#define AHCI_PxCMD_ST           0x00000001
#define AHCI_PxCMD_SUD          0x00000002
#define AHCI_PxCMD_POD          0x00000004
#define AHCI_PxCMD_CLO          0x00000008
#define AHCI_PxCMD_FRE          0x00000010
#define AHCI_PxCMD_FR           0x00004000
#define AHCI_PxCMD_CR           0x00008000

/* PxSSTS values */
#define AHCI_PxSSTS_DET_MASK    0x0000000F
#define AHCI_PxSSTS_DET_PRESENT 0x00000003
#define AHCI_PxSSTS_SPD_MASK    0x000000F0
#define AHCI_PxSSTS_IPM_MASK    0x00000F00
#define AHCI_PxSSTS_IPM_ACTIVE  0x00000100

/* PxSCTL DET values */
#define AHCI_PxSCTL_DET_MASK    0x0000000F
#define AHCI_PxSCTL_DET_NONE    0x00000000
#define AHCI_PxSCTL_DET_RESET   0x00000001

/* PxIS status bits */
#define AHCI_PxIS_TFES          0x40000000
#define AHCI_PxIS_HBFS          0x08000000
#define AHCI_PxIS_HBDS          0x04000000
#define AHCI_PxIS_IFS           0x01000000
#define AHCI_PxIS_DS            0x00000002
#define AHCI_PxIS_DHRS          0x00000001

/* Taskfile bits */
#define AHCI_TFD_STS_BSY        0x80
#define AHCI_TFD_STS_DRQ        0x08
#define AHCI_TFD_STS_ERR        0x01

/* Command header flags */
#define AHCI_CMDH_CFL_MASK      0x1F    /* Command FIS length in DWORDs */
#define AHCI_CMDH_A             (1 << 5) /* ATAPI */
#define AHCI_CMDH_W             (1 << 6)
#define AHCI_CMDH_P             (1 << 7)
#define AHCI_CMDH_R             (1 << 8)
#define AHCI_CMDH_B             (1 << 9)
#define AHCI_CMDH_C             (1 << 10)

/* FIS types */
#define AHCI_FIS_TYPE_REG_H2D   0x27

/* Simplified AHCI command header and PRDT entry (spec 1.3.1) */
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

typedef struct _AHCI_PORT_CONTEXT {
    BOOLEAN Present;
    BOOLEAN Atapi;
    BOOLEAN Busy;
    ULONG Signature;
    ULONG SectorSize;
    ULONGLONG SectorCount;
    BOOLEAN SenseValid;
    SENSE_DATA SenseData;
    PAHCI_CMD_HEADER CommandList;
    SCSI_PHYSICAL_ADDRESS CommandListPhys;
    PAHCI_CMD_TABLE CommandTable;
    SCSI_PHYSICAL_ADDRESS CommandTablePhys;
    PVOID ReceivedFis;
    SCSI_PHYSICAL_ADDRESS ReceivedFisPhys;
    PVOID IdentifyBuffer;
    SCSI_PHYSICAL_ADDRESS IdentifyBufferPhys;
} AHCI_PORT_CONTEXT, *PAHCI_PORT_CONTEXT;

typedef struct _AHCI_ADAPTER_EXTENSION {
    PAHCI_HBA_MEM AbBase;     /* Mapped ABAR base */
    ULONG Cap;
    ULONG Version;
    ULONG PortsImplemented;
    UCHAR PortCount;
    PVOID NonCachedBase;
    ULONG NonCachedBytes;
    AHCI_PORT_CONTEXT Ports[AHCI_MAX_PORTS];
} AHCI_ADAPTER_EXTENSION, *PAHCI_ADAPTER_EXTENSION;

static __inline BOOLEAN
AhciIsPortDevicePresent(_In_ PAHCI_ADAPTER_EXTENSION Adapter, _In_ ULONG Port)
{
    volatile ULONG *pxssts;
    ULONG ssts;

    if (Adapter == NULL || Adapter->AbBase == NULL)
        return FALSE;

    pxssts = (volatile ULONG *)((PUCHAR)Adapter->AbBase + AHCI_PORT_REG_BASE + Port * AHCI_PORT_STRIDE + AHCI_PxSSTS);
    ssts = *pxssts;
    return ((ssts & AHCI_PxSSTS_DET_MASK) == AHCI_PxSSTS_DET_PRESENT);
}

#define AHCI_ALIGN_UP(Value, Align)   (((Value) + ((Align) - 1)) & ~((Align) - 1))


typedef struct _AHCI_SRB_EXTENSION {
    ULONG Placeholder;
} AHCI_SRB_EXTENSION, *PAHCI_SRB_EXTENSION;
