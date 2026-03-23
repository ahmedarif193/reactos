/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     EPT table management - init, destroy, map, unmap, lookup
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/ept.h>
#include <rosv/vmx.h>

/* ---- Constants ---------------------------------------------------------- */

#define EPT_PAGE_SIZE           4096ULL
#define EPT_LARGE_PAGE_SIZE     (2ULL * 1024 * 1024)    /* 2MB */
#define EPT_PFN_MASK            0x000FFFFFFFFFF000ULL    /* Bits 12-51 */

/* ---- GPA index extraction ----------------------------------------------- */

#define EPT_PML4_INDEX(gpa)     (((gpa) >> 39) & 0x1FF)
#define EPT_PDPT_INDEX(gpa)     (((gpa) >> 30) & 0x1FF)
#define EPT_PD_INDEX(gpa)       (((gpa) >> 21) & 0x1FF)
#define EPT_PT_INDEX(gpa)       (((gpa) >> 12) & 0x1FF)

/* ---- Internal helpers --------------------------------------------------- */

/**
 * Allocate a zeroed, 4KB-aligned page for an EPT page table.
 * Tracks the allocation in the EPT context's page table list.
 */
static PEPT_PTE
RosvEptAllocatePageTable(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _Out_ PHYSICAL_ADDRESS *PhysAddr)
{
    PVOID VirtAddr;
    PHYSICAL_ADDRESS MaxAddr;
    PEPT_PAGE_TABLE Tracker;

    MaxAddr.QuadPart = -1LL; /* No upper bound */

    VirtAddr = MmAllocateContiguousMemory(EPT_PAGE_SIZE, MaxAddr);
    if (!VirtAddr)
    {
        ROSV_ERR("EPT: Failed to allocate page table (count=%u)", EptContext->PageTableCount);
        PhysAddr->QuadPart = 0;
        return NULL;
    }
    RtlZeroMemory(VirtAddr, EPT_PAGE_SIZE);
    *PhysAddr = MmGetPhysicalAddress(VirtAddr);

    /* Allocate tracker node */
    Tracker = (PEPT_PAGE_TABLE)ExAllocatePoolWithTag(NonPagedPool,
                                                      sizeof(EPT_PAGE_TABLE),
                                                      ROSV_DRIVER_TAG);
    if (!Tracker)
    {
        ROSV_ERR("EPT: Failed to allocate page table tracker");
        MmFreeContiguousMemory(VirtAddr);
        PhysAddr->QuadPart = 0;
        return NULL;
    }
    Tracker->VirtualAddress = VirtAddr;
    Tracker->PhysicalAddress = *PhysAddr;
    InsertTailList(&EptContext->PageTableList, &Tracker->ListEntry);
    EptContext->PageTableCount++;

    return (PEPT_PTE)VirtAddr;
}

/**
 * Get the virtual address of the page table pointed to by an EPT entry.
 * Walks the tracker list to find the VA for the given PFN.
 */
static PEPT_PTE
RosvEptGetSubTable(
    _In_ PROSV_EPT_CONTEXT EptContext,
    _In_ PEPT_PTE Entry)
{
    PHYSICAL_ADDRESS PhysAddr;
    PLIST_ENTRY ListEntry;
    PEPT_PAGE_TABLE Tracker;

    PhysAddr.QuadPart = (LONGLONG)(Entry->PageFrameNumber << 12);

    for (ListEntry = EptContext->PageTableList.Flink;
         ListEntry != &EptContext->PageTableList;
         ListEntry = ListEntry->Flink)
    {
        Tracker = CONTAINING_RECORD(ListEntry, EPT_PAGE_TABLE, ListEntry);
        if (Tracker->PhysicalAddress.QuadPart == PhysAddr.QuadPart)
        {
            return (PEPT_PTE)Tracker->VirtualAddress;
        }
    }

    ROSV_ERR("EPT: No tracker found for PA=0x%llX", PhysAddr.QuadPart);
    return NULL;
}

/**
 * Walk/create EPT tables down to the desired level.
 * Returns a pointer to the entry at the final level.
 * If LargePage is TRUE, stops at PD level (for 2MB pages).
 */
static PEPT_PTE
RosvEptWalkCreate(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ BOOLEAN LargePage)
{
    PEPT_PTE Table;
    PEPT_PTE Entry;
    PHYSICAL_ADDRESS NewTablePhys;
    PEPT_PTE NewTable;
    ULONG Indices[4];
    ULONG Levels;
    ULONG i;

    Indices[0] = (ULONG)EPT_PML4_INDEX(GuestPhysicalAddress);
    Indices[1] = (ULONG)EPT_PDPT_INDEX(GuestPhysicalAddress);
    Indices[2] = (ULONG)EPT_PD_INDEX(GuestPhysicalAddress);
    Indices[3] = (ULONG)EPT_PT_INDEX(GuestPhysicalAddress);

    /* For 2MB large pages, we stop at PD (3 levels: PML4->PDPT->PD) */
    /* For 4KB pages, we walk all 4 levels: PML4->PDPT->PD->PT */
    Levels = LargePage ? 3 : 4;

    Table = EptContext->Pml4;
    ROSV_ASSERT(Table != NULL, "PML4 must be initialized");

    for (i = 0; i < Levels - 1; i++)
    {
        Entry = &Table[Indices[i]];

        if (!Entry->Read && !Entry->Write && !Entry->Execute)
        {
            /* Entry is empty - allocate a new sub-table */
            NewTable = RosvEptAllocatePageTable(EptContext, &NewTablePhys);
            if (!NewTable)
            {
                ROSV_ERR("EPT: Failed to allocate sub-table at level %u for GPA=0x%llX",
                         i, GuestPhysicalAddress);
                return NULL;
            }

            Entry->Value = 0;
            Entry->Read = 1;
            Entry->Write = 1;
            Entry->Execute = 1;
            Entry->PageFrameNumber = (ULONG64)(NewTablePhys.QuadPart >> 12);
        }

        /* Descend into the sub-table */
        Table = RosvEptGetSubTable(EptContext, Entry);
        if (!Table)
        {
            ROSV_ERR("EPT: Failed to resolve sub-table at level %u for GPA=0x%llX",
                     i, GuestPhysicalAddress);
            return NULL;
        }
    }

    /* Return the entry at the final level */
    return &Table[Indices[Levels - 1]];
}

/**
 * Walk EPT tables (read-only) to find the leaf entry for a GPA.
 * Returns NULL if the GPA is unmapped at any level.
 */
static PEPT_PTE
RosvEptWalkLookup(
    _In_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress)
{
    PEPT_PTE Table;
    PEPT_PTE Entry;

    if (!EptContext->Pml4)
    {
        ROSV_ERR("EPT: Lookup with NULL PML4");
        return NULL;
    }

    /* PML4 */
    Table = EptContext->Pml4;
    Entry = &Table[EPT_PML4_INDEX(GuestPhysicalAddress)];
    if (!Entry->Read && !Entry->Write && !Entry->Execute)
        return NULL;

    /* PDPT */
    Table = RosvEptGetSubTable(EptContext, Entry);
    if (!Table)
        return NULL;
    Entry = &Table[EPT_PDPT_INDEX(GuestPhysicalAddress)];
    if (!Entry->Read && !Entry->Write && !Entry->Execute)
        return NULL;
    /* 1GB large page check */
    if (Entry->LargePage)
        return Entry;

    /* PD */
    Table = RosvEptGetSubTable(EptContext, Entry);
    if (!Table)
        return NULL;
    Entry = &Table[EPT_PD_INDEX(GuestPhysicalAddress)];
    if (!Entry->Read && !Entry->Write && !Entry->Execute)
        return NULL;
    /* 2MB large page check */
    if (Entry->LargePage)
        return Entry;

    /* PT */
    Table = RosvEptGetSubTable(EptContext, Entry);
    if (!Table)
        return NULL;
    Entry = &Table[EPT_PT_INDEX(GuestPhysicalAddress)];
    if (!Entry->Read && !Entry->Write && !Entry->Execute)
        return NULL;

    return Entry;
}

/* ---- Public API --------------------------------------------------------- */

NTSTATUS
RosvEptInitialize(
    _Out_ PROSV_EPT_CONTEXT EptContext)
{
    PHYSICAL_ADDRESS Pml4Phys;
    PHYSICAL_ADDRESS MaxAddr;

    ROSV_ASSERT(EptContext != NULL, "EptContext must not be NULL");

    RtlZeroMemory(EptContext, sizeof(ROSV_EPT_CONTEXT));
    InitializeListHead(&EptContext->PageTableList);
    KeInitializeSpinLock(&EptContext->Lock);

    MaxAddr.QuadPart = -1LL;
    EptContext->Pml4 = (PEPT_PML4E)MmAllocateContiguousMemory(EPT_PAGE_SIZE, MaxAddr);
    if (!EptContext->Pml4)
    {
        ROSV_ERR("EPT: Failed to allocate PML4 table");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(EptContext->Pml4, EPT_PAGE_SIZE);

    Pml4Phys = MmGetPhysicalAddress(EptContext->Pml4);
    EptContext->Pml4Physical = Pml4Phys;

    /* Build EPTP: WB memory type (6) | page walk length 3 (4-level) | PML4 address */
    EptContext->Eptp.Value = 0;
    EptContext->Eptp.MemoryType = EPT_MEMORY_TYPE_WB;
    EptContext->Eptp.PageWalkLength = 3; /* 4-level walk minus 1 */
    EptContext->Eptp.AccessedDirty = 0;  /* Disable A/D bits for simplicity */
    EptContext->Eptp.Pml4Address = (ULONG64)(Pml4Phys.QuadPart >> 12);

    ROSV_TRACE("EPT: Initialized PML4 VA=%p PA=0x%llX EPTP=0x%llX",
               EptContext->Pml4, Pml4Phys.QuadPart, EptContext->Eptp.Value);

    return STATUS_SUCCESS;
}

VOID
RosvEptDestroy(
    _Inout_ PROSV_EPT_CONTEXT EptContext)
{
    PLIST_ENTRY ListEntry;
    PEPT_PAGE_TABLE Tracker;
    ULONG FreedCount = 0;

    ROSV_ASSERT(EptContext != NULL, "EptContext must not be NULL");

    /* Free all tracked page tables */
    while (!IsListEmpty(&EptContext->PageTableList))
    {
        ListEntry = RemoveHeadList(&EptContext->PageTableList);
        Tracker = CONTAINING_RECORD(ListEntry, EPT_PAGE_TABLE, ListEntry);

        MmFreeContiguousMemory(Tracker->VirtualAddress);
        ExFreePoolWithTag(Tracker, ROSV_DRIVER_TAG);
        FreedCount++;
    }

    /* Free the PML4 table itself */
    if (EptContext->Pml4)
    {
        MmFreeContiguousMemory(EptContext->Pml4);
    }

    ROSV_TRACE("EPT: Destroyed context, freed %u page tables + PML4", FreedCount);

    RtlZeroMemory(EptContext, sizeof(ROSV_EPT_CONTEXT));
}

NTSTATUS
RosvEptMapPage(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 HostPhysicalAddress,
    _In_ ULONG64 AccessFlags,
    _In_ ULONG MemoryType)
{
    PEPT_PTE Entry;
    BOOLEAN IsLargePage;

    ROSV_ASSERT(EptContext != NULL, "EptContext must not be NULL");
    ROSV_ASSERT(EptContext->Pml4 != NULL, "PML4 must be initialized");
    /*
     * Keep this helper VMX-agnostic.
     * It is used during ROSV_IOCTL_SET_MEMORY before VMXON, so INVEPT here
     * would trigger #UD on hosts that are not yet in VMX operation.
     */

    /* Determine if this is a 2MB large page mapping */
    IsLargePage = ((GuestPhysicalAddress & (EPT_LARGE_PAGE_SIZE - 1)) == 0) &&
                  ((HostPhysicalAddress & (EPT_LARGE_PAGE_SIZE - 1)) == 0);

    if (!IsLargePage)
    {
        /* 4KB page - must be page-aligned */
        ROSV_ASSERT((GuestPhysicalAddress & (EPT_PAGE_SIZE - 1)) == 0,
                    "GPA must be 4KB aligned for 4KB mapping");
        ROSV_ASSERT((HostPhysicalAddress & (EPT_PAGE_SIZE - 1)) == 0,
                    "HPA must be 4KB aligned for 4KB mapping");
    }

    Entry = RosvEptWalkCreate(EptContext, GuestPhysicalAddress, FALSE);
    if (!Entry)
    {
        ROSV_ERR("EPT: MapPage failed to walk/create tables for GPA=0x%llX", GuestPhysicalAddress);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Check if already mapped */
    if (Entry->Read || Entry->Write || Entry->Execute)
    {
        ROSV_WARN("EPT: GPA=0x%llX already mapped (existing PFN=0x%llX), overwriting",
                  GuestPhysicalAddress, Entry->PageFrameNumber);
    }

    /* Set the leaf entry */
    Entry->Value = 0;
    Entry->Read = (AccessFlags & EPT_READ) ? 1 : 0;
    Entry->Write = (AccessFlags & EPT_WRITE) ? 1 : 0;
    Entry->Execute = (AccessFlags & EPT_EXECUTE) ? 1 : 0;
    Entry->MemoryType = MemoryType;
    Entry->IgnorePat = 1; /* Use EPT memory type, ignore guest PAT */
    Entry->PageFrameNumber = HostPhysicalAddress >> 12;

    return STATUS_SUCCESS;
}

NTSTATUS
RosvEptMapRange(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalBase,
    _In_ ULONG64 HostPhysicalBase,
    _In_ ULONG64 SizeInBytes,
    _In_ ULONG64 AccessFlags,
    _In_ ULONG MemoryType)
{
    ULONG64 Offset;
    ULONG64 GpaPage, HpaPage;
    NTSTATUS Status;
    ULONG PageCount = 0;

    ROSV_ASSERT(EptContext != NULL, "EptContext must not be NULL");
    ROSV_ASSERT((GuestPhysicalBase & (EPT_PAGE_SIZE - 1)) == 0, "GPA base must be page-aligned");
    ROSV_ASSERT((HostPhysicalBase & (EPT_PAGE_SIZE - 1)) == 0, "HPA base must be page-aligned");
    ROSV_ASSERT((SizeInBytes & (EPT_PAGE_SIZE - 1)) == 0, "Size must be page-aligned");

    for (Offset = 0; Offset < SizeInBytes; Offset += EPT_PAGE_SIZE)
    {
        GpaPage = GuestPhysicalBase + Offset;
        HpaPage = HostPhysicalBase + Offset;

        Status = RosvEptMapPage(EptContext, GpaPage, HpaPage, AccessFlags, MemoryType);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("EPT: MapRange failed at offset 0x%llX (GPA=0x%llX), mapped %u pages before failure",
                     Offset, GpaPage, PageCount);
            return Status;
        }
        PageCount++;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
RosvEptUnmapPage(
    _Inout_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress)
{
    PEPT_PTE Entry;

    ROSV_ASSERT(EptContext != NULL, "EptContext must not be NULL");

    Entry = RosvEptWalkLookup(EptContext, GuestPhysicalAddress);
    if (!Entry)
    {
        ROSV_WARN("EPT: UnmapPage GPA=0x%llX not found (already unmapped?)", GuestPhysicalAddress);
        return STATUS_NOT_FOUND;
    }

    ROSV_TRACE("EPT: Unmap GPA=0x%llX (was HPA PFN=0x%llX)",
               GuestPhysicalAddress, Entry->PageFrameNumber);

    Entry->Value = 0;

    return STATUS_SUCCESS;
}

PEPT_PTE
RosvEptLookup(
    _In_ PROSV_EPT_CONTEXT EptContext,
    _In_ ULONG64 GuestPhysicalAddress)
{
    PEPT_PTE Entry;

    ROSV_ASSERT(EptContext != NULL, "EptContext must not be NULL");

    Entry = RosvEptWalkLookup(EptContext, GuestPhysicalAddress);
    if (!Entry)
    {
        ROSV_TRACE("EPT: Lookup GPA=0x%llX -> not mapped", GuestPhysicalAddress);
    }

    return Entry;
}
