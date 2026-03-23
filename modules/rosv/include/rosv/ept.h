/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     EPT (Extended Page Tables) structures and prototypes
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#pragma once

#include <rosv/rosv.h>

/* ---- EPT memory types --------------------------------------------------- */

#define EPT_MEMORY_TYPE_UC      0
#define EPT_MEMORY_TYPE_WC      1
#define EPT_MEMORY_TYPE_WT      4
#define EPT_MEMORY_TYPE_WP      5
#define EPT_MEMORY_TYPE_WB      6

/* ---- EPT access rights -------------------------------------------------- */

#define EPT_READ                (1ULL << 0)
#define EPT_WRITE               (1ULL << 1)
#define EPT_EXECUTE             (1ULL << 2)
#define EPT_RWX                 (EPT_READ | EPT_WRITE | EPT_EXECUTE)

/* ---- EPT page table entry (generic for all levels) ---------------------- */

typedef union _EPT_PTE {
    ULONG64 Value;
    struct {
        ULONG64 Read : 1;           /* Bit 0 */
        ULONG64 Write : 1;          /* Bit 1 */
        ULONG64 Execute : 1;        /* Bit 2 */
        ULONG64 MemoryType : 3;     /* Bits 3-5 (leaf only) */
        ULONG64 IgnorePat : 1;      /* Bit 6 (leaf only) */
        ULONG64 LargePage : 1;      /* Bit 7 (PDE/PDPTE only) */
        ULONG64 Accessed : 1;       /* Bit 8 */
        ULONG64 Dirty : 1;          /* Bit 9 (leaf only) */
        ULONG64 ExecuteUser : 1;    /* Bit 10 */
        ULONG64 Reserved1 : 1;      /* Bit 11 */
        ULONG64 PageFrameNumber : 40; /* Bits 12-51 */
        ULONG64 Reserved2 : 11;     /* Bits 52-62 */
        ULONG64 SuppressVe : 1;     /* Bit 63 */
    };
} EPT_PTE, *PEPT_PTE;

/* All levels use the same entry format */
typedef EPT_PTE EPT_PML4E, *PEPT_PML4E;
typedef EPT_PTE EPT_PDPTE, *PEPT_PDPTE;
typedef EPT_PTE EPT_PDE, *PEPT_PDE;

/* ---- EPTP (EPT Pointer) ------------------------------------------------- */

typedef union _EPT_POINTER {
    ULONG64 Value;
    struct {
        ULONG64 MemoryType : 3;     /* Bits 0-2: 0=UC, 6=WB */
        ULONG64 PageWalkLength : 3; /* Bits 3-5: must be 3 (4-level) */
        ULONG64 AccessedDirty : 1;  /* Bit 6: enable A/D bits */
        ULONG64 Reserved1 : 5;      /* Bits 7-11 */
        ULONG64 Pml4Address : 40;   /* Bits 12-51: PML4 physical addr >> 12 */
        ULONG64 Reserved2 : 12;     /* Bits 52-63 */
    };
} EPT_POINTER, *PEPT_POINTER;

/* ---- EPT page table tracking node --------------------------------------- */

typedef struct _EPT_PAGE_TABLE {
    LIST_ENTRY ListEntry;           /* Link into allocation list */
    PVOID VirtualAddress;           /* Virtual address of 4KB page table */
    PHYSICAL_ADDRESS PhysicalAddress; /* Physical address */
} EPT_PAGE_TABLE, *PEPT_PAGE_TABLE;

/* ---- EPT context (per-VM) ----------------------------------------------- */

struct _ROSV_EPT_CONTEXT {
    PEPT_PML4E Pml4;                /* PML4 table (virtual) */
    PHYSICAL_ADDRESS Pml4Physical;  /* PML4 table (physical) */
    EPT_POINTER Eptp;               /* Cached EPTP value */
    LIST_ENTRY PageTableList;       /* All allocated page tables */
    ULONG PageTableCount;           /* Number of allocated page tables */
    KSPIN_LOCK Lock;                /* Protects page table modifications */
};

/* ---- Function prototypes (ept/) ----------------------------------------- */

/* ept.c - EPT table management */
NTSTATUS
RosvEptInitialize(
    _Out_ PROSV_EPT_CONTEXT EptContext);

VOID
RosvEptDestroy(
    _Inout_ PROSV_EPT_CONTEXT EptContext);

NTSTATUS
RosvEptMapPage(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 HostPhysicalAddress,
    _In_ ULONG64 AccessFlags,
    _In_ ULONG MemoryType);

NTSTATUS
RosvEptMapRange(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalBase,
    _In_ ULONG64 HostPhysicalBase,
    _In_ ULONG64 SizeInBytes,
    _In_ ULONG64 AccessFlags,
    _In_ ULONG MemoryType);

NTSTATUS
RosvEptUnmapPage(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress);

PEPT_PTE
RosvEptLookup(
    _In_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress);

/* ept_exit.c - EPT violation handler */
BOOLEAN
RosvEptHandleViolation(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 ExitQualification);

BOOLEAN
RosvEptHandleMisconfiguration(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress);

/* Invalidate the GVA→GPA translation cache (call on CR3 writes) */
VOID
RosvEptGvaCacheInvalidate(VOID);

/* INVEPT types */
#define INVEPT_SINGLE_CONTEXT   1
#define INVEPT_ALL_CONTEXT      2

/* RosvAsmInvept declared in vmx.h */
