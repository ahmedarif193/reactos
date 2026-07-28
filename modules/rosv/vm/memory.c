/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Guest physical memory allocator and EPT setup
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 *
 * Uses MDL-based allocation (MmAllocatePagesForMdlEx) to avoid
 * consuming kernel virtual address space for guest RAM. EPT mappings
 * are set up directly from the MDL PFN array. Kernel VA mappings are
 * created only for explicit access paths (for example GpaToHva).
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>

/* ---- Guest physical memory layout --------------------------------------- */

#define GUEST_LOW_MEM_BASE      0x00000ULL
#define GUEST_LOW_MEM_SIZE      0xA0000ULL      /* 640KB conventional RAM */
#define GUEST_HOLE_BASE         0xA0000ULL      /* VGA/ROM/BIOS hole */
#define GUEST_HOLE_END          0x100000ULL     /* 1MB mark */
#define GUEST_EXT_MEM_BASE      0x100000ULL     /* Extended memory starts at 1MB */
#define LAPIC_MMIO_GPA          0xFEE00000ULL
#define IOAPIC_MMIO_GPA         0xFEC00000ULL

/* ---- Allocation chunk size ---------------------------------------------- */

#define ALLOC_CHUNK_SIZE        (2ULL * 1024 * 1024) /* 2MB chunks */
#define MAX_ALLOC_CHUNKS        16384 /* 4GB guest with 256KB fallback chunks */

#pragma pack(push, 1)
typedef struct _ROSV_MP_FLOATING_POINTER {
    UCHAR Signature[4];
    ULONG ConfigTableAddress;
    UCHAR Length16Bytes;
    UCHAR SpecRevision;
    UCHAR Checksum;
    UCHAR Feature1;
    UCHAR Feature2;
    UCHAR Feature3;
    UCHAR Feature4;
    UCHAR Feature5;
} ROSV_MP_FLOATING_POINTER;

typedef struct _ROSV_MP_CONFIG_TABLE_HEADER {
    UCHAR Signature[4];
    USHORT BaseTableLength;
    UCHAR SpecRevision;
    UCHAR Checksum;
    UCHAR OemId[8];
    UCHAR ProductId[12];
    ULONG OemTablePointer;
    USHORT OemTableSize;
    USHORT EntryCount;
    ULONG LapicAddress;
    USHORT ExtendedTableLength;
    UCHAR ExtendedTableChecksum;
    UCHAR Reserved;
} ROSV_MP_CONFIG_TABLE_HEADER;

typedef struct _ROSV_MP_PROCESSOR_ENTRY {
    UCHAR EntryType;
    UCHAR LocalApicId;
    UCHAR LocalApicVersion;
    UCHAR CpuFlags;
    ULONG CpuSignature;
    ULONG FeatureFlags;
    ULONG Reserved0;
    ULONG Reserved1;
} ROSV_MP_PROCESSOR_ENTRY;

typedef struct _ROSV_MP_BUS_ENTRY {
    UCHAR EntryType;
    UCHAR BusId;
    UCHAR BusType[6];
} ROSV_MP_BUS_ENTRY;

typedef struct _ROSV_MP_IOAPIC_ENTRY {
    UCHAR EntryType;
    UCHAR IoApicId;
    UCHAR IoApicVersion;
    UCHAR IoApicFlags;
    ULONG IoApicAddress;
} ROSV_MP_IOAPIC_ENTRY;

typedef struct _ROSV_MP_INT_ASSIGN_ENTRY {
    UCHAR EntryType;
    UCHAR InterruptType;
    USHORT Flags;
    UCHAR SourceBusId;
    UCHAR SourceBusIrq;
    UCHAR DestIoApicId;
    UCHAR DestIoApicIntin;
} ROSV_MP_INT_ASSIGN_ENTRY;

typedef struct _ROSV_MP_LOCAL_INT_ASSIGN_ENTRY {
    UCHAR EntryType;
    UCHAR InterruptType;
    USHORT Flags;
    UCHAR SourceBusId;
    UCHAR SourceBusIrq;
    UCHAR DestLapicId;
    UCHAR DestLapicLint;
} ROSV_MP_LOCAL_INT_ASSIGN_ENTRY;

typedef struct _ROSV_ACPI_RSDP {
    UCHAR Signature[8];
    UCHAR Checksum;
    UCHAR OemId[6];
    UCHAR Revision;
    ULONG RsdtAddress;
    ULONG Length;
    ULONG64 XsdtAddress;
    UCHAR ExtendedChecksum;
    UCHAR Reserved[3];
} ROSV_ACPI_RSDP;

typedef struct _ROSV_ACPI_SDT_HEADER {
    UCHAR Signature[4];
    ULONG Length;
    UCHAR Revision;
    UCHAR Checksum;
    UCHAR OemId[6];
    UCHAR OemTableId[8];
    ULONG OemRevision;
    ULONG CreatorId;
    ULONG CreatorRevision;
} ROSV_ACPI_SDT_HEADER;

typedef struct _ROSV_ACPI_RSDT {
    ROSV_ACPI_SDT_HEADER Header;
    ULONG Entry[1];
} ROSV_ACPI_RSDT;

typedef struct _ROSV_ACPI_XSDT {
    ROSV_ACPI_SDT_HEADER Header;
    ULONG64 Entry[1];
} ROSV_ACPI_XSDT;

typedef struct _ROSV_ACPI_MADT {
    ROSV_ACPI_SDT_HEADER Header;
    ULONG LapicAddress;
    ULONG Flags;
} ROSV_ACPI_MADT;

typedef struct _ROSV_ACPI_MADT_LOCAL_APIC {
    UCHAR Type;
    UCHAR Length;
    UCHAR AcpiProcessorUid;
    UCHAR ApicId;
    ULONG Flags;
} ROSV_ACPI_MADT_LOCAL_APIC;

typedef struct _ROSV_ACPI_MADT_IO_APIC {
    UCHAR Type;
    UCHAR Length;
    UCHAR IoApicId;
    UCHAR Reserved;
    ULONG IoApicAddress;
    ULONG GlobalSystemInterruptBase;
} ROSV_ACPI_MADT_IO_APIC;

typedef struct _ROSV_ACPI_MADT_INTERRUPT_OVERRIDE {
    UCHAR Type;
    UCHAR Length;
    UCHAR Bus;
    UCHAR SourceIrq;
    ULONG GlobalSystemInterrupt;
    USHORT Flags;
} ROSV_ACPI_MADT_INTERRUPT_OVERRIDE;

typedef struct _ROSV_ACPI_MADT_LAPIC_NMI {
    UCHAR Type;
    UCHAR Length;
    UCHAR AcpiProcessorUid;
    USHORT Flags;
    UCHAR LapicLint;
} ROSV_ACPI_MADT_LAPIC_NMI;
#pragma pack(pop)

/* ---- Chunk tracking ----------------------------------------------------- */

typedef struct _ROSV_ALLOC_CHUNK {
    PMDL Mdl;                       /* MDL for allocated pages (no VA) */
    PVOID MappedVa;                 /* Temporary kernel VA, NULL when unmapped */
    ULONG64 GuestPhysicalAddress;
    ULONG64 SizeInBytes;
} ROSV_ALLOC_CHUNK;

static ROSV_ALLOC_CHUNK g_AllocChunks[MAX_ALLOC_CHUNKS];
static ULONG g_AllocChunkCount = 0;

/* GPA->HVA translation cache: index of last successfully looked-up chunk.
 * Exploits temporal locality — consecutive GPA accesses typically hit the
 * same 2MB chunk (virtqueue descriptors, packet buffers, etc.). */
static volatile LONG g_LastUsedChunkIndex = -1;

/* ---- Internal helpers --------------------------------------------------- */

static
UCHAR
RosvMemoryComputeChecksum(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG i;
    UCHAR Sum = 0;

    for (i = 0; i < Length; i++)
        Sum = (UCHAR)(Sum + Data[i]);

    return (UCHAR)(0 - Sum);
}

static
VOID
RosvMemoryInitializeAcpiHeader(
    _Out_ ROSV_ACPI_SDT_HEADER *Header,
    _In_reads_(4) const CHAR Signature[4],
    _In_ ULONG Length,
    _In_ UCHAR Revision)
{
    RtlZeroMemory(Header, sizeof(*Header));
    RtlCopyMemory(Header->Signature, Signature, 4);
    Header->Length = Length;
    Header->Revision = Revision;
    RtlCopyMemory(Header->OemId, "ROSV  ", 6);
    RtlCopyMemory(Header->OemTableId, "ROSVACPI", 8);
    Header->OemRevision = 1;
    Header->CreatorId = 0x56534F52; /* "ROSV" little-endian */
    Header->CreatorRevision = 1;
}

static
VOID
RosvMemoryInitializeMpTable(
    _Inout_ PROSV_VM Vm)
{
    ROSV_MP_FLOATING_POINTER *MpFp;
    ROSV_MP_CONFIG_TABLE_HEADER *Cfg;
    PUCHAR Cursor;
    USHORT EntryCount = 0;
    UCHAR Irq;
    ULONG BaseLength;

    MpFp = (ROSV_MP_FLOATING_POINTER *)RosvMemoryGpaToHva(Vm, ROSV_MP_FLOATING_PTR_GPA);
    Cfg = (ROSV_MP_CONFIG_TABLE_HEADER *)RosvMemoryGpaToHva(Vm, ROSV_MP_CONFIG_TABLE_GPA);
    if (MpFp == NULL || Cfg == NULL)
    {
        ROSV_WARN("Memory: Unable to place MP tables (MPF=%p, MPCFG=%p)", MpFp, Cfg);
        return;
    }

    RtlZeroMemory(Cfg, 0x200);
    RtlCopyMemory(Cfg->Signature, "PCMP", 4);
    Cfg->SpecRevision = 4;
    RtlCopyMemory(Cfg->OemId, "ROSV    ", 8);
    RtlCopyMemory(Cfg->ProductId, "ROSVVM      ", 12);
    Cfg->LapicAddress = (ULONG)LAPIC_MMIO_GPA;

    Cursor = (PUCHAR)(Cfg + 1);

    {
        ROSV_MP_PROCESSOR_ENTRY *Cpu = (ROSV_MP_PROCESSOR_ENTRY *)Cursor;
        RtlZeroMemory(Cpu, sizeof(*Cpu));
        Cpu->EntryType = 0;
        Cpu->LocalApicId = 0;
        Cpu->LocalApicVersion = 0x14;
        Cpu->CpuFlags = 0x03;      /* enabled + BSP */
        Cpu->FeatureFlags = 0x00000201; /* FPU + APIC */
        Cursor += sizeof(*Cpu);
        EntryCount++;
    }

    {
        ROSV_MP_BUS_ENTRY *Bus = (ROSV_MP_BUS_ENTRY *)Cursor;
        RtlZeroMemory(Bus, sizeof(*Bus));
        Bus->EntryType = 1;
        Bus->BusId = 0;
        RtlCopyMemory(Bus->BusType, "ISA   ", 6);
        Cursor += sizeof(*Bus);
        EntryCount++;
    }

    {
        ROSV_MP_IOAPIC_ENTRY *IoApic = (ROSV_MP_IOAPIC_ENTRY *)Cursor;
        RtlZeroMemory(IoApic, sizeof(*IoApic));
        IoApic->EntryType = 2;
        IoApic->IoApicId = 1;
        IoApic->IoApicVersion = 0x11;
        IoApic->IoApicFlags = 1;  /* enabled */
        IoApic->IoApicAddress = (ULONG)IOAPIC_MMIO_GPA;
        Cursor += sizeof(*IoApic);
        EntryCount++;
    }

    for (Irq = 0; Irq < 16; Irq++)
    {
        ROSV_MP_INT_ASSIGN_ENTRY *IoInt = (ROSV_MP_INT_ASSIGN_ENTRY *)Cursor;
        UCHAR Intin;

        RtlZeroMemory(IoInt, sizeof(*IoInt));
        IoInt->EntryType = 3;
        IoInt->InterruptType = 0; /* INT */
        IoInt->SourceBusId = 0;   /* ISA */
        IoInt->SourceBusIrq = Irq;
        IoInt->DestIoApicId = 1;
        /*
         * PC/APIC compatibility routing:
         * legacy ISA IRQ0 (PIT) arrives on IOAPIC INTIN2.
         */
        Intin = (Irq == 0) ? 2 : Irq;
        IoInt->DestIoApicIntin = Intin;
        Cursor += sizeof(*IoInt);
        EntryCount++;
    }

    {
        ROSV_MP_LOCAL_INT_ASSIGN_ENTRY *Lint = (ROSV_MP_LOCAL_INT_ASSIGN_ENTRY *)Cursor;
        RtlZeroMemory(Lint, sizeof(*Lint));
        Lint->EntryType = 4;
        Lint->InterruptType = 3; /* ExtINT */
        Lint->SourceBusId = 0;
        Lint->SourceBusIrq = 0;
        Lint->DestLapicId = 0xFF; /* all LAPICs */
        Lint->DestLapicLint = 0;
        Cursor += sizeof(*Lint);
        EntryCount++;
    }

    {
        ROSV_MP_LOCAL_INT_ASSIGN_ENTRY *Lint = (ROSV_MP_LOCAL_INT_ASSIGN_ENTRY *)Cursor;
        RtlZeroMemory(Lint, sizeof(*Lint));
        Lint->EntryType = 4;
        Lint->InterruptType = 1; /* NMI */
        Lint->SourceBusId = 0;
        Lint->SourceBusIrq = 0;
        Lint->DestLapicId = 0xFF;
        Lint->DestLapicLint = 1;
        Cursor += sizeof(*Lint);
        EntryCount++;
    }

    BaseLength = (ULONG)(Cursor - (PUCHAR)Cfg);
    Cfg->BaseTableLength = (USHORT)BaseLength;
    Cfg->EntryCount = EntryCount;
    Cfg->Checksum = 0;
    Cfg->Checksum = RosvMemoryComputeChecksum((const UCHAR *)Cfg, BaseLength);

    RtlZeroMemory(MpFp, sizeof(*MpFp));
    RtlCopyMemory(MpFp->Signature, "_MP_", 4);
    MpFp->ConfigTableAddress = (ULONG)ROSV_MP_CONFIG_TABLE_GPA;
    MpFp->Length16Bytes = 1;
    MpFp->SpecRevision = 4;
    MpFp->Checksum = 0;
    MpFp->Checksum = RosvMemoryComputeChecksum((const UCHAR *)MpFp, sizeof(*MpFp));

    ROSV_TRACE("Memory: MP tables published at GPA 0x%llX/0x%llX (entries=%u, len=%u)",
               ROSV_MP_FLOATING_PTR_GPA, ROSV_MP_CONFIG_TABLE_GPA, EntryCount, BaseLength);
}

static
VOID
RosvMemoryInitializeAcpiTables(
    _Inout_ PROSV_VM Vm)
{
    ROSV_ACPI_RSDP *Rsdp;
    ROSV_ACPI_RSDT *Rsdt;
    ROSV_ACPI_XSDT *Xsdt;
    ROSV_ACPI_MADT *Madt;
    PUCHAR Cursor;
    ULONG Length;

    Rsdp = (ROSV_ACPI_RSDP *)RosvMemoryGpaToHva(Vm, ROSV_ACPI_RSDP_GPA);
    Xsdt = (ROSV_ACPI_XSDT *)RosvMemoryGpaToHva(Vm, ROSV_ACPI_XSDT_GPA);
    Rsdt = (ROSV_ACPI_RSDT *)RosvMemoryGpaToHva(Vm, ROSV_ACPI_RSDT_GPA);
    Madt = (ROSV_ACPI_MADT *)RosvMemoryGpaToHva(Vm, ROSV_ACPI_MADT_GPA);

    if (Rsdp == NULL || Xsdt == NULL || Rsdt == NULL || Madt == NULL)
    {
        ROSV_WARN("Memory: Unable to publish ACPI tables (RSDP=%p XSDT=%p RSDT=%p MADT=%p)",
                  Rsdp, Xsdt, Rsdt, Madt);
        return;
    }

    /* TODO: publish FADT/FACS/DSDT so ACPICA can fully enable ACPI mode (PM timer, SCI routing). */

    RtlZeroMemory(Rsdp, 0x100);
    RtlZeroMemory(Xsdt, 0x100);
    RtlZeroMemory(Rsdt, 0x100);
    RtlZeroMemory(Madt, 0x200);

    RosvMemoryInitializeAcpiHeader(&Madt->Header, "APIC", sizeof(*Madt), 1);
    Madt->LapicAddress = (ULONG)LAPIC_MMIO_GPA;
    /* APIC-only topology: no dual-8259 compatibility path. */
    Madt->Flags = 0;
    Cursor = (PUCHAR)(Madt + 1);

    {
        ROSV_ACPI_MADT_LOCAL_APIC *Lapic = (ROSV_ACPI_MADT_LOCAL_APIC *)Cursor;
        Lapic->Type = 0;
        Lapic->Length = sizeof(*Lapic);
        Lapic->AcpiProcessorUid = 0;
        Lapic->ApicId = 0;
        Lapic->Flags = 1; /* enabled */
        Cursor += sizeof(*Lapic);
    }

    {
        ROSV_ACPI_MADT_IO_APIC *IoApic = (ROSV_ACPI_MADT_IO_APIC *)Cursor;
        IoApic->Type = 1;
        IoApic->Length = sizeof(*IoApic);
        IoApic->IoApicId = 1;
        IoApic->Reserved = 0;
        IoApic->IoApicAddress = (ULONG)IOAPIC_MMIO_GPA;
        IoApic->GlobalSystemInterruptBase = 0;
        Cursor += sizeof(*IoApic);
    }

    {
        ROSV_ACPI_MADT_INTERRUPT_OVERRIDE *Iso =
            (ROSV_ACPI_MADT_INTERRUPT_OVERRIDE *)Cursor;
        Iso->Type = 2;
        Iso->Length = sizeof(*Iso);
        Iso->Bus = 0;          /* ISA */
        Iso->SourceIrq = 0;    /* PIT IRQ0 */
        Iso->GlobalSystemInterrupt = 2;
        Iso->Flags = 0;        /* bus default polarity/trigger */
        Cursor += sizeof(*Iso);
    }

    {
        ROSV_ACPI_MADT_LAPIC_NMI *Nmi = (ROSV_ACPI_MADT_LAPIC_NMI *)Cursor;
        Nmi->Type = 4;
        Nmi->Length = sizeof(*Nmi);
        Nmi->AcpiProcessorUid = 0xFF; /* all processors */
        Nmi->Flags = 0;
        Nmi->LapicLint = 1;
        Cursor += sizeof(*Nmi);
    }

    Madt->Header.Length = (ULONG)(Cursor - (PUCHAR)Madt);
    Madt->Header.Checksum = 0;
    Madt->Header.Checksum =
        RosvMemoryComputeChecksum((const UCHAR *)Madt, Madt->Header.Length);

    RosvMemoryInitializeAcpiHeader(&Xsdt->Header, "XSDT", sizeof(*Xsdt), 1);
    Xsdt->Entry[0] = ROSV_ACPI_MADT_GPA;
    Xsdt->Header.Checksum = 0;
    Xsdt->Header.Checksum =
        RosvMemoryComputeChecksum((const UCHAR *)Xsdt, Xsdt->Header.Length);

    RosvMemoryInitializeAcpiHeader(&Rsdt->Header, "RSDT", sizeof(*Rsdt), 1);
    Rsdt->Entry[0] = (ULONG)ROSV_ACPI_MADT_GPA;
    Rsdt->Header.Checksum = 0;
    Rsdt->Header.Checksum =
        RosvMemoryComputeChecksum((const UCHAR *)Rsdt, Rsdt->Header.Length);

    RtlZeroMemory(Rsdp, sizeof(*Rsdp));
    RtlCopyMemory(Rsdp->Signature, "RSD PTR ", 8);
    RtlCopyMemory(Rsdp->OemId, "ROSV  ", 6);
    Rsdp->Revision = 2;
    Rsdp->RsdtAddress = (ULONG)ROSV_ACPI_RSDT_GPA;
    Rsdp->Length = sizeof(*Rsdp);
    Rsdp->XsdtAddress = ROSV_ACPI_XSDT_GPA;
    Rsdp->Checksum = 0;
    Rsdp->Checksum = RosvMemoryComputeChecksum((const UCHAR *)Rsdp, 20);
    Rsdp->ExtendedChecksum = 0;
    Length = Rsdp->Length;
    Rsdp->ExtendedChecksum =
        RosvMemoryComputeChecksum((const UCHAR *)Rsdp, Length);

    ROSV_TRACE("Memory: ACPI tables published: RSDP=0x%llX RSDT=0x%llX XSDT=0x%llX MADT=0x%llX",
               ROSV_ACPI_RSDP_GPA, ROSV_ACPI_RSDT_GPA, ROSV_ACPI_XSDT_GPA, ROSV_ACPI_MADT_GPA);
}

/**
 * Allocate physical pages via MDL, EPT-map them, and track for freeing.
 * No kernel VA is consumed until explicitly mapped via GpaToHva.
 */
static NTSTATUS
RosvMemoryZeroChunkViaIoSpace(
    _In_ PMDL Mdl,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 SizeInBytes)
{
    PPFN_NUMBER PfnArray;
    ULONG PageCount;
    ULONG i;

    ROSV_ASSERT(Mdl != NULL, "Mdl must not be NULL");
    ROSV_ASSERT((SizeInBytes & (PAGE_SIZE - 1)) == 0, "Chunk size must be page-aligned");

    PfnArray = MmGetMdlPfnArray(Mdl);
    PageCount = (ULONG)(SizeInBytes / PAGE_SIZE);

    for (i = 0; i < PageCount; i++)
    {
        PHYSICAL_ADDRESS PagePa;
        PVOID PageVa;

        PagePa.QuadPart = ((ULONG64)PfnArray[i] << PAGE_SHIFT);
        PageVa = MmMapIoSpace(PagePa, PAGE_SIZE, MmCached);
        if (PageVa == NULL)
        {
            ROSV_ERR("Memory: MmMapIoSpace failed while zeroing chunk GPA=0x%llX page=%u/%u HPA=0x%llX",
                     GuestPhysicalAddress, i, PageCount, PagePa.QuadPart);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(PageVa, PAGE_SIZE);
        MmUnmapIoSpace(PageVa, PAGE_SIZE);
    }

    return STATUS_SUCCESS;
}

static VOID
RosvMemoryRollbackEptMappings(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG PagesMapped)
{
    ULONG i;

    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");

    for (i = 0; i < PagesMapped; i++)
    {
        ULONG64 Gpa = GuestPhysicalAddress + ((ULONG64)i * PAGE_SIZE);
        NTSTATUS Status = RosvEptUnmapPage(&Vm->EptContext, Gpa);
        if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
        {
            ROSV_WARN("Memory: rollback unmap failed GPA=0x%llX, Status=0x%08X", Gpa, Status);
        }
    }
}

static NTSTATUS
RosvMemoryAllocateAndMapChunk(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 SizeInBytes)
{
    PHYSICAL_ADDRESS LowAddr, HighAddr, SkipBytes;
    PMDL Mdl;
    PPFN_NUMBER PfnArray;
    ULONG PageCount, i;
    NTSTATUS Status;

    if ((GuestPhysicalAddress & (PAGE_SIZE - 1)) != 0 ||
        (SizeInBytes & (PAGE_SIZE - 1)) != 0)
    {
        ROSV_ERR("Memory: Chunk is not page aligned (GPA=0x%llX size=0x%llX)",
                 GuestPhysicalAddress, SizeInBytes);
        return STATUS_INVALID_PARAMETER;
    }

    if (g_AllocChunkCount >= MAX_ALLOC_CHUNKS)
    {
        ROSV_ERR("Memory: Chunk table full (%u/%u), cannot allocate GPA=0x%llX size=0x%llX",
                 g_AllocChunkCount, MAX_ALLOC_CHUNKS,
                 GuestPhysicalAddress, SizeInBytes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Allocate physical pages - no kernel VA consumed */
    LowAddr.QuadPart = 0;
    HighAddr.QuadPart = -1LL;   /* No upper bound */
    SkipBytes.QuadPart = 0;     /* No alignment constraint between pages */

    Mdl = MmAllocatePagesForMdlEx(LowAddr, HighAddr, SkipBytes,
                                   (SIZE_T)SizeInBytes,
                                   MmCached, 0);
    if (!Mdl)
    {
        ROSV_ERR("Memory: MmAllocatePagesForMdlEx failed for size=0x%llX (%llu KB)",
                 SizeInBytes, SizeInBytes / 1024);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Verify we got the full amount */
    if (MmGetMdlByteCount(Mdl) < SizeInBytes)
    {
        ROSV_ERR("Memory: MDL only got %lu bytes, requested %llu",
                 MmGetMdlByteCount(Mdl), SizeInBytes);
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Walk PFN array and create EPT mappings page-by-page */
    PfnArray = MmGetMdlPfnArray(Mdl);
    PageCount = (ULONG)(SizeInBytes / PAGE_SIZE);

    for (i = 0; i < PageCount; i++)
    {
        ULONG64 Gpa = GuestPhysicalAddress + ((ULONG64)i * PAGE_SIZE);
        ULONG64 Hpa = (ULONG64)PfnArray[i] << PAGE_SHIFT;

        Status = RosvEptMapPage(&Vm->EptContext, Gpa, Hpa,
                                EPT_RWX, EPT_MEMORY_TYPE_WB);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("Memory: EPT map failed at page %u/%u GPA=0x%llX HPA=0x%llX, Status=0x%08X",
                     i, PageCount, Gpa, Hpa, Status);
            RosvMemoryRollbackEptMappings(Vm, GuestPhysicalAddress, i);
            MmFreePagesFromMdl(Mdl);
            ExFreePool(Mdl);
            return Status;
        }
    }

    Status = RosvMemoryZeroChunkViaIoSpace(Mdl, GuestPhysicalAddress, SizeInBytes);
    if (!NT_SUCCESS(Status))
    {
        RosvMemoryRollbackEptMappings(Vm, GuestPhysicalAddress, PageCount);
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
        return Status;
    }

    /* Track for freeing and later VA mapping */
    g_AllocChunks[g_AllocChunkCount].Mdl = Mdl;
    g_AllocChunks[g_AllocChunkCount].MappedVa = NULL;
    g_AllocChunks[g_AllocChunkCount].GuestPhysicalAddress = GuestPhysicalAddress;
    g_AllocChunks[g_AllocChunkCount].SizeInBytes = SizeInBytes;
    g_AllocChunkCount++;

    return STATUS_SUCCESS;
}

/**
 * Ensure a chunk has a kernel VA mapping. Maps lazily on first access.
 * Returns the base VA of the chunk, or NULL on failure.
 */
static PVOID
RosvMemoryMapChunk(
    _Inout_ ROSV_ALLOC_CHUNK *Chunk)
{
    PVOID ExistingVa;
    PVOID NewVa;

    if (Chunk->MappedVa)
        return Chunk->MappedVa;

    NewVa = MmMapLockedPagesSpecifyCache(
        Chunk->Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    if (!NewVa)
    {
        ROSV_ERR("Memory: MmMapLockedPagesSpecifyCache failed for GPA=0x%llX size=0x%llX",
                 Chunk->GuestPhysicalAddress, Chunk->SizeInBytes);
        return NULL;
    }

    /*
     * Multiple device paths can request the same chunk mapping concurrently.
     * Publish exactly one shared VA and discard any duplicate transient map.
     */
    ExistingVa = InterlockedCompareExchangePointer((PVOID volatile *)&Chunk->MappedVa,
                                                   NewVa,
                                                   NULL);
    if (ExistingVa != NULL)
    {
        MmUnmapLockedPages(NewVa, Chunk->Mdl);
        return ExistingVa;
    }

    return NewVa;
}

static NTSTATUS
RosvMemoryCopyToChunkViaIoSpace(
    _In_ ROSV_ALLOC_CHUNK *Chunk,
    _In_ ULONG64 Offset,
    _In_reads_bytes_(Length) const UCHAR *Source,
    _In_ SIZE_T Length)
{
    PPFN_NUMBER PfnArray;
    SIZE_T Remaining = Length;
    ULONG64 CurrentOffset = Offset;

    ROSV_ASSERT(Chunk != NULL, "Chunk must not be NULL");
    ROSV_ASSERT(Source != NULL, "Source must not be NULL");

    PfnArray = MmGetMdlPfnArray(Chunk->Mdl);
    while (Remaining > 0)
    {
        ULONG PageIndex = (ULONG)(CurrentOffset >> PAGE_SHIFT);
        ULONG PageOffset = (ULONG)(CurrentOffset & (PAGE_SIZE - 1));
        SIZE_T PageBytes = PAGE_SIZE - PageOffset;
        PHYSICAL_ADDRESS PagePa;
        PVOID PageVa;

        if (PageBytes > Remaining)
            PageBytes = Remaining;

        if (CurrentOffset >= Chunk->SizeInBytes)
            return STATUS_INVALID_PARAMETER;

        PagePa.QuadPart = ((ULONG64)PfnArray[PageIndex] << PAGE_SHIFT);
        PageVa = MmMapIoSpace(PagePa, PAGE_SIZE, MmCached);
        if (PageVa == NULL)
        {
            ROSV_ERR("Memory: MmMapIoSpace failed for HPA=0x%llX (GPA base=0x%llX offset=0x%llX)",
                     PagePa.QuadPart,
                     Chunk->GuestPhysicalAddress,
                     CurrentOffset);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory((PUCHAR)PageVa + PageOffset, Source, PageBytes);
        MmUnmapIoSpace(PageVa, PAGE_SIZE);

        Source += PageBytes;
        CurrentOffset += PageBytes;
        Remaining -= PageBytes;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
RosvMemoryCopyFromChunkViaIoSpace(
    _In_ ROSV_ALLOC_CHUNK *Chunk,
    _Out_writes_bytes_(Length) UCHAR *Destination,
    _In_ ULONG64 Offset,
    _In_ SIZE_T Length)
{
    PPFN_NUMBER PfnArray;
    SIZE_T Remaining = Length;
    ULONG64 CurrentOffset = Offset;

    ROSV_ASSERT(Chunk != NULL, "Chunk must not be NULL");
    ROSV_ASSERT(Destination != NULL, "Destination must not be NULL");

    PfnArray = MmGetMdlPfnArray(Chunk->Mdl);
    while (Remaining > 0)
    {
        ULONG PageIndex = (ULONG)(CurrentOffset >> PAGE_SHIFT);
        ULONG PageOffset = (ULONG)(CurrentOffset & (PAGE_SIZE - 1));
        SIZE_T PageBytes = PAGE_SIZE - PageOffset;
        PHYSICAL_ADDRESS PagePa;
        PVOID PageVa;

        if (PageBytes > Remaining)
            PageBytes = Remaining;

        if (CurrentOffset >= Chunk->SizeInBytes)
            return STATUS_INVALID_PARAMETER;

        PagePa.QuadPart = ((ULONG64)PfnArray[PageIndex] << PAGE_SHIFT);
        PageVa = MmMapIoSpace(PagePa, PAGE_SIZE, MmCached);
        if (PageVa == NULL)
        {
            ROSV_ERR("Memory: MmMapIoSpace failed for HPA=0x%llX (GPA base=0x%llX offset=0x%llX)",
                     PagePa.QuadPart,
                     Chunk->GuestPhysicalAddress,
                     CurrentOffset);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(Destination, (PUCHAR)PageVa + PageOffset, PageBytes);
        MmUnmapIoSpace(PageVa, PAGE_SIZE);

        Destination += PageBytes;
        CurrentOffset += PageBytes;
        Remaining -= PageBytes;
    }

    return STATUS_SUCCESS;
}

/* Minimum fallback chunk size. Going below this tends to trigger repeated
 * allocation retries and excessive host memory-manager thrashing. */
#define MIN_FALLBACK_SIZE   (256ULL * 1024)  /* 256KB */

/**
 * Allocate host memory for a guest region and map it into EPT.
 * Allocates in 2MB chunks using MDLs (no kernel VA consumed).
 * On allocation failure, retries with progressively smaller chunk
 * sizes (halving, minimum 256KB). Once a smaller size succeeds,
 * all subsequent chunks use that size (host memory won't grow back).
 */
static NTSTATUS
RosvMemorySetupRegion(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalBase,
    _In_ ULONG64 SizeInBytes)
{
    NTSTATUS Status;
    ULONG64 ChunkSize;
    ULONG64 Offset;
    ULONG64 Remaining;
    ULONG64 TrySize;
    ULONG64 MaxChunkSize = ALLOC_CHUNK_SIZE;
    ULONG ChunksAllocated = 0;
    BOOLEAN FallbackSucceeded;

    ROSV_TRACE("Memory: Setting up region GPA=0x%llX size=0x%llX (%llu MB)",
               GuestPhysicalBase, SizeInBytes, SizeInBytes / (1024 * 1024));

    for (Offset = 0; Offset < SizeInBytes; )
    {
        Remaining = SizeInBytes - Offset;
        ChunkSize = (Remaining >= MaxChunkSize) ? MaxChunkSize : Remaining;

        Status = RosvMemoryAllocateAndMapChunk(Vm, GuestPhysicalBase + Offset, ChunkSize);
        if (NT_SUCCESS(Status))
        {
            Offset += ChunkSize;
            ChunksAllocated++;
            continue;
        }

        /* Allocation failed - try progressively smaller chunks.
         * Once a smaller size works, lock MaxChunkSize to it so we
         * don't keep retrying the larger size (which would just fail again). */
        FallbackSucceeded = FALSE;
        TrySize = ChunkSize / 2;
        if (TrySize > Remaining)
            TrySize = Remaining;
        if (TrySize < MIN_FALLBACK_SIZE && Remaining >= MIN_FALLBACK_SIZE)
            TrySize = MIN_FALLBACK_SIZE;

        while (TrySize >= MIN_FALLBACK_SIZE)
        {
            Status = RosvMemoryAllocateAndMapChunk(Vm, GuestPhysicalBase + Offset, TrySize);
            if (NT_SUCCESS(Status))
            {
                if (TrySize < MaxChunkSize)
                {
                    ROSV_TRACE("Memory: Reduced chunk size from 0x%llX to 0x%llX at offset 0x%llX",
                               MaxChunkSize, TrySize, Offset);
                    MaxChunkSize = TrySize;
                }
                Offset += TrySize;
                ChunksAllocated++;
                FallbackSucceeded = TRUE;
                break;
            }

            if (TrySize == MIN_FALLBACK_SIZE)
                break;

            TrySize /= 2;
            if (TrySize < MIN_FALLBACK_SIZE)
                TrySize = MIN_FALLBACK_SIZE;
            if (TrySize > Remaining)
                TrySize = Remaining;
        }

        /* If even MIN_FALLBACK_SIZE failed, host is truly exhausted */
        if (!FallbackSucceeded)
        {
            ROSV_ERR("Memory: Host exhausted at GPA=0x%llX+0x%llX (%llu/%llu MB allocated)",
                     GuestPhysicalBase, Offset,
                     Offset / (1024 * 1024), SizeInBytes / (1024 * 1024));
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    ROSV_TRACE("Memory: Region GPA=0x%llX allocated in %u chunks",
               GuestPhysicalBase, ChunksAllocated);

    return STATUS_SUCCESS;
}

/* ---- Public API --------------------------------------------------------- */

NTSTATUS
RosvMemoryAllocateGuestRam(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 SizeInBytes)
{
    NTSTATUS Status;
    ULONG64 ExtendedSize;

    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    ROSV_ASSERT(SizeInBytes >= (2 * 1024 * 1024), "Guest RAM must be at least 2MB");

    ROSV_TRACE("Memory: Allocating guest RAM, total size=0x%llX (%llu MB)",
               SizeInBytes, SizeInBytes / (1024 * 1024));

    /* Initialize EPT */
    Status = RosvEptInitialize(&Vm->EptContext);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("Memory: RosvEptInitialize failed, Status=0x%08X", Status);
        return Status;
    }

    /*
     * Map guest physical memory layout:
     *
     * 0x00000 - 0x9FFFF  (640KB):  Low memory (conventional RAM)
     * 0xA0000 - 0xFFFFF  (384KB):  VGA/ROM/BIOS area (RAM-backed firmware data)
     * 0x100000 - end:              Extended memory
     */

    /* Region 1: Low memory (0 - 640KB) */
    ROSV_TRACE("Memory: Setting up low memory (0x0 - 0x%llX, %llu KB)",
               GUEST_LOW_MEM_SIZE, GUEST_LOW_MEM_SIZE / 1024);
    Status = RosvMemorySetupRegion(Vm, GUEST_LOW_MEM_BASE, GUEST_LOW_MEM_SIZE);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("Memory: Failed to setup low memory region, Status=0x%08X", Status);
        goto Cleanup;
    }

    /* Region 1.5: BIOS/ROM hole (0xA0000 - 0xFFFFF)
     * Backed by RAM pages so firmware-era probes do not trap. */
    {
        ULONG64 HoleSize = GUEST_HOLE_END - GUEST_HOLE_BASE;
        ROSV_TRACE("Memory: Setting up BIOS hole (0x%llX - 0x%llX, %llu KB)",
                   GUEST_HOLE_BASE, GUEST_HOLE_END, HoleSize / 1024);
        Status = RosvMemorySetupRegion(Vm, GUEST_HOLE_BASE, HoleSize);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("Memory: Failed to setup BIOS hole region, Status=0x%08X", Status);
            goto Cleanup;
        }

        /* Publish MP floating pointer/config tables for APIC discovery. */
        RosvMemoryInitializeMpTable(Vm);
        /* Publish ACPI RSDP/XSDT/MADT for Linux APIC/IOAPIC discovery. */
        RosvMemoryInitializeAcpiTables(Vm);
    }

    /* Region 2: Extended memory (1MB - end) */
    ExtendedSize = SizeInBytes - GUEST_HOLE_END; /* Total minus the first 1MB */
    if (ExtendedSize > 0)
    {
        ROSV_TRACE("Memory: Setting up extended memory (0x%llX - 0x%llX, %llu MB)",
                   GUEST_EXT_MEM_BASE, GUEST_EXT_MEM_BASE + ExtendedSize,
                   ExtendedSize / (1024 * 1024));
        Status = RosvMemorySetupRegion(Vm, GUEST_EXT_MEM_BASE, ExtendedSize);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("Memory: Failed to setup extended memory region, Status=0x%08X", Status);
            goto Cleanup;
        }
    }

    /*
     * LAPIC/IOAPIC/virtio MMIO ranges are intentionally NOT mapped in EPT.
     * Guest accesses to these GPAs must trap via EPT violation and be handled
     * by MMIO emulation in ept_exit.c.
     *
     * This only works if guest RAM does not extend into the MMIO region.
     */
    if (GUEST_EXT_MEM_BASE + ExtendedSize > 0xFEB00000ULL)
    {
        ROSV_ERR("Memory: Guest RAM (0x%llX bytes) overlaps MMIO regions at 0xFEB00000. "
                 "Maximum supported RAM: %u MB",
                 ExtendedSize, (ULONG)((0xFEB00000ULL - GUEST_EXT_MEM_BASE) / (1024 * 1024)));
        return STATUS_INVALID_PARAMETER;
    }

    Vm->GuestRamSize = SizeInBytes;

    ROSV_TRACE("Memory: Guest RAM setup complete. Total=%llu MB, chunks=%u, "
               "EPT page tables=%u (no kernel VA consumed)",
               SizeInBytes / (1024 * 1024),
               g_AllocChunkCount,
               Vm->EptContext.PageTableCount);
    ROSV_TRACE("Memory: EPTP=0x%llX", Vm->EptContext.Eptp.Value);
    ROSV_TRACE("Memory: LAPIC MMIO at GPA 0x%llX intentionally unmapped for MMIO trapping",
               LAPIC_MMIO_GPA);
    ROSV_TRACE("Memory: IOAPIC MMIO at GPA 0x%llX intentionally unmapped for MMIO trapping",
               IOAPIC_MMIO_GPA);
    ROSV_TRACE("Memory: Virtio-blk MMIO at GPA 0xFEB00000 (4KB) intentionally unmapped for MMIO trapping");
    ROSV_TRACE("Memory: Virtio-net MMIO at GPA 0xFEB01000 (4KB) intentionally unmapped for MMIO trapping");

    return STATUS_SUCCESS;

Cleanup:
    RosvMemoryFreeGuestRam(Vm);
    return Status;
}

VOID
RosvMemoryFreeGuestRam(
    _Inout_ PROSV_VM Vm)
{
    ULONG i;

    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");

    ROSV_TRACE("Memory: Freeing guest RAM (%u chunks)", g_AllocChunkCount);

    /* Free all allocated chunks */
    for (i = 0; i < g_AllocChunkCount; i++)
    {
        if (g_AllocChunks[i].MappedVa)
        {
            MmUnmapLockedPages(g_AllocChunks[i].MappedVa, g_AllocChunks[i].Mdl);
            g_AllocChunks[i].MappedVa = NULL;
        }
        if (g_AllocChunks[i].Mdl)
        {
            MmFreePagesFromMdl(g_AllocChunks[i].Mdl);
            ExFreePool(g_AllocChunks[i].Mdl);
            g_AllocChunks[i].Mdl = NULL;
        }
    }
    g_AllocChunkCount = 0;
    g_LastUsedChunkIndex = -1;

    Vm->MemoryRegionCount = 0;

    /* Destroy EPT */
    RosvEptDestroy(&Vm->EptContext);

    Vm->GuestRam = NULL;
    Vm->GuestRamSize = 0;

    ROSV_TRACE("Memory: Guest RAM freed");
}

/**
 * Release all temporary kernel VA mappings after setup is complete.
 * The physical pages and EPT mappings remain intact for the running VM.
 */
VOID
RosvMemoryUnmapAll(
    _Inout_ PROSV_VM Vm)
{
    ULONG i, Unmapped = 0;

    UNREFERENCED_PARAMETER(Vm);

    for (i = 0; i < g_AllocChunkCount; i++)
    {
        if (g_AllocChunks[i].MappedVa)
        {
            MmUnmapLockedPages(g_AllocChunks[i].MappedVa, g_AllocChunks[i].Mdl);
            g_AllocChunks[i].MappedVa = NULL;
            Unmapped++;
        }
    }

    ROSV_TRACE("Memory: Unmapped %u/%u chunks, kernel VA reclaimed", Unmapped, g_AllocChunkCount);
}

/**
 * Get a temporary kernel VA for a guest physical address.
 * Lazily maps the containing chunk if not already mapped.
 * Call RosvMemoryUnmapAll() after setup to release all VA.
 */
PVOID
RosvMemoryGpaToHva(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress)
{
    ULONG i;
    LONG CachedIdx;
    ULONG64 Offset;
    PVOID ChunkVa;

    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");

    /* Fast path: check the last used chunk first (temporal locality) */
    CachedIdx = g_LastUsedChunkIndex;
    if (CachedIdx >= 0 && (ULONG)CachedIdx < g_AllocChunkCount)
    {
        ROSV_ALLOC_CHUNK *Cached = &g_AllocChunks[CachedIdx];
        if (GuestPhysicalAddress >= Cached->GuestPhysicalAddress &&
            GuestPhysicalAddress < Cached->GuestPhysicalAddress + Cached->SizeInBytes)
        {
            ChunkVa = RosvMemoryMapChunk(Cached);
            if (!ChunkVa)
                return NULL;

            Offset = GuestPhysicalAddress - Cached->GuestPhysicalAddress;
            return (PVOID)((ULONG_PTR)ChunkVa + (ULONG_PTR)Offset);
        }
    }

    /* Slow path: walk the chunk table to find which chunk contains this GPA */
    for (i = 0; i < g_AllocChunkCount; i++)
    {
        if (GuestPhysicalAddress >= g_AllocChunks[i].GuestPhysicalAddress &&
            GuestPhysicalAddress < g_AllocChunks[i].GuestPhysicalAddress + g_AllocChunks[i].SizeInBytes)
        {
            /* Lazily map this chunk into kernel VA */
            ChunkVa = RosvMemoryMapChunk(&g_AllocChunks[i]);
            if (!ChunkVa)
                return NULL;

            /* Update cache for next lookup */
            g_LastUsedChunkIndex = (LONG)i;

            Offset = GuestPhysicalAddress - g_AllocChunks[i].GuestPhysicalAddress;
            return (PVOID)((ULONG_PTR)ChunkVa + (ULONG_PTR)Offset);
        }
    }

    ROSV_ERR("Memory: GpaToHva failed - GPA=0x%llX not in any chunk (%u checked)",
             GuestPhysicalAddress, g_AllocChunkCount);
    return NULL;
}

/**
 * Copy data from a host buffer to guest physical memory.
 * Handles cross-chunk boundaries correctly (the old code didn't).
 *
 * Runtime path uses page-wise IoSpace mappings to avoid long-lived MDL maps.
 */
NTSTATUS
RosvMemoryCopyToGpa(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ const VOID *Source,
    _In_ SIZE_T Length)
{
    const UCHAR *Src = (const UCHAR *)Source;
    SIZE_T Remaining = Length;
    ULONG64 CurrentGpa = GuestPhysicalAddress;

    /* Reject zero-length copies */
    if (Length == 0)
    {
        ROSV_ERR("Memory: CopyToGpa called with Length=0, GPA=0x%llX", GuestPhysicalAddress);
        return STATUS_INVALID_PARAMETER;
    }

    /* Overflow check: GPA + Length must not wrap around */
    if (GuestPhysicalAddress + Length < GuestPhysicalAddress)
    {
        ROSV_ERR("Memory: CopyToGpa address overflow: GPA=0x%llX + Length=0x%zX wraps",
                 GuestPhysicalAddress, Length);
        return STATUS_INVALID_PARAMETER;
    }

    while (Remaining > 0)
    {
        ULONG i;
        LONG CachedIdx;
        SIZE_T CopyLen;
        BOOLEAN Found = FALSE;

        /* Fast path: check last used chunk first */
        CachedIdx = g_LastUsedChunkIndex;
        if (CachedIdx >= 0 && (ULONG)CachedIdx < g_AllocChunkCount &&
            CurrentGpa >= g_AllocChunks[CachedIdx].GuestPhysicalAddress &&
            CurrentGpa < g_AllocChunks[CachedIdx].GuestPhysicalAddress + g_AllocChunks[CachedIdx].SizeInBytes)
        {
            i = (ULONG)CachedIdx;
            Found = TRUE;
        }
        else
        {
            /* Slow path: find the chunk for CurrentGpa */
            for (i = 0; i < g_AllocChunkCount; i++)
            {
                if (CurrentGpa >= g_AllocChunks[i].GuestPhysicalAddress &&
                    CurrentGpa < g_AllocChunks[i].GuestPhysicalAddress + g_AllocChunks[i].SizeInBytes)
                {
                    g_LastUsedChunkIndex = (LONG)i;
                    Found = TRUE;
                    break;
                }
            }
        }

        if (Found)
        {
            ULONG64 Offset = CurrentGpa - g_AllocChunks[i].GuestPhysicalAddress;
            SIZE_T ChunkAvailable = (SIZE_T)(g_AllocChunks[i].SizeInBytes - Offset);
            NTSTATUS CopyStatus;

            CopyLen = (Remaining < ChunkAvailable) ? Remaining : ChunkAvailable;

            /* Assert CopyLen > 0 to prevent infinite loops */
            if (CopyLen == 0)
            {
                ROSV_ERR("Memory: CopyToGpa internal error - CopyLen=0 at GPA=0x%llX "
                         "(Remaining=%zu, ChunkAvailable=%zu)", CurrentGpa, Remaining, ChunkAvailable);
                return STATUS_INTERNAL_ERROR;
            }

            CopyStatus = RosvMemoryCopyToChunkViaIoSpace(&g_AllocChunks[i], Offset, Src, CopyLen);
            if (!NT_SUCCESS(CopyStatus))
            {
                ROSV_ERR("Memory: CopyToGpa failed to map/copy chunk for GPA=0x%llX", CurrentGpa);
                return CopyStatus;
            }
        }

        if (!Found)
        {
            /* Provide a specific diagnostic if the GPA falls in a known MMIO region */
            if (CurrentGpa >= 0xFEB00000ULL && CurrentGpa < 0xFEB01000ULL)
            {
                ROSV_ERR("Memory: CopyToGpa - GPA=0x%llX is in the virtio-blk MMIO hole "
                         "(0xFEB00000-0xFEB01000), not backed by RAM", CurrentGpa);
            }
            else if (CurrentGpa >= 0xFEB01000ULL && CurrentGpa < 0xFEB02000ULL)
            {
                ROSV_ERR("Memory: CopyToGpa - GPA=0x%llX is in the virtio-net MMIO hole "
                         "(0xFEB01000-0xFEB02000), not backed by RAM", CurrentGpa);
            }
            else if (CurrentGpa >= 0xFEC00000ULL && CurrentGpa < 0xFEC01000ULL)
            {
                ROSV_ERR("Memory: CopyToGpa - GPA=0x%llX is in the IOAPIC MMIO region "
                         "(0xFEC00000-0xFEC01000)", CurrentGpa);
            }
            else if (CurrentGpa >= 0xFEE00000ULL && CurrentGpa < 0xFEE01000ULL)
            {
                ROSV_ERR("Memory: CopyToGpa - GPA=0x%llX is in the LAPIC MMIO region "
                         "(0xFEE00000-0xFEE01000)", CurrentGpa);
            }
            else
            {
                ROSV_ERR("Memory: CopyToGpa - GPA=0x%llX not in any chunk (%u checked), "
                         "%llu bytes remaining",
                         CurrentGpa, g_AllocChunkCount, (ULONG64)Remaining);
            }
            return STATUS_INVALID_ADDRESS;
        }

        Src += CopyLen;
        CurrentGpa += CopyLen;
        Remaining -= CopyLen;
    }

    return STATUS_SUCCESS;
}

/**
 * Copy data from guest physical memory to a host buffer.
 * Handles cross-chunk boundaries correctly.
 *
 * See CopyToGpa for mapping policy details.
 */
NTSTATUS
RosvMemoryCopyFromGpa(
    _In_ PROSV_VM Vm,
    _Out_writes_bytes_(Length) VOID *Destination,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ SIZE_T Length)
{
    UCHAR *Dst = (UCHAR *)Destination;
    SIZE_T Remaining = Length;
    ULONG64 CurrentGpa = GuestPhysicalAddress;

    /* Reject zero-length copies */
    if (Length == 0)
    {
        ROSV_ERR("Memory: CopyFromGpa called with Length=0, GPA=0x%llX", GuestPhysicalAddress);
        return STATUS_INVALID_PARAMETER;
    }

    /* Overflow check: GPA + Length must not wrap around */
    if (GuestPhysicalAddress + Length < GuestPhysicalAddress)
    {
        ROSV_ERR("Memory: CopyFromGpa address overflow: GPA=0x%llX + Length=0x%zX wraps",
                 GuestPhysicalAddress, Length);
        return STATUS_INVALID_PARAMETER;
    }

    while (Remaining > 0)
    {
        ULONG i;
        LONG CachedIdx;
        SIZE_T CopyLen;
        BOOLEAN Found = FALSE;

        /* Fast path: check last used chunk first */
        CachedIdx = g_LastUsedChunkIndex;
        if (CachedIdx >= 0 && (ULONG)CachedIdx < g_AllocChunkCount &&
            CurrentGpa >= g_AllocChunks[CachedIdx].GuestPhysicalAddress &&
            CurrentGpa < g_AllocChunks[CachedIdx].GuestPhysicalAddress + g_AllocChunks[CachedIdx].SizeInBytes)
        {
            i = (ULONG)CachedIdx;
            Found = TRUE;
        }
        else
        {
            /* Slow path: find the chunk for CurrentGpa */
            for (i = 0; i < g_AllocChunkCount; i++)
            {
                if (CurrentGpa >= g_AllocChunks[i].GuestPhysicalAddress &&
                    CurrentGpa < g_AllocChunks[i].GuestPhysicalAddress + g_AllocChunks[i].SizeInBytes)
                {
                    g_LastUsedChunkIndex = (LONG)i;
                    Found = TRUE;
                    break;
                }
            }
        }

        if (Found)
        {
            ULONG64 Offset = CurrentGpa - g_AllocChunks[i].GuestPhysicalAddress;
            SIZE_T ChunkAvailable = (SIZE_T)(g_AllocChunks[i].SizeInBytes - Offset);
            NTSTATUS CopyStatus;

            CopyLen = (Remaining < ChunkAvailable) ? Remaining : ChunkAvailable;

            /* Assert CopyLen > 0 to prevent infinite loops */
            if (CopyLen == 0)
            {
                ROSV_ERR("Memory: CopyFromGpa internal error - CopyLen=0 at GPA=0x%llX "
                         "(Remaining=%zu, ChunkAvailable=%zu)", CurrentGpa, Remaining, ChunkAvailable);
                return STATUS_INTERNAL_ERROR;
            }

            CopyStatus = RosvMemoryCopyFromChunkViaIoSpace(&g_AllocChunks[i], Dst, Offset, CopyLen);
            if (!NT_SUCCESS(CopyStatus))
            {
                ROSV_ERR("Memory: CopyFromGpa failed to map/copy chunk for GPA=0x%llX", CurrentGpa);
                return CopyStatus;
            }
        }

        if (!Found)
        {
            /* Provide a specific diagnostic if the GPA falls in a known MMIO region */
            if (CurrentGpa >= 0xFEB00000ULL && CurrentGpa < 0xFEB01000ULL)
            {
                ROSV_ERR("Memory: CopyFromGpa - GPA=0x%llX is in the virtio-blk MMIO hole "
                         "(0xFEB00000-0xFEB01000), not backed by RAM", CurrentGpa);
            }
            else if (CurrentGpa >= 0xFEB01000ULL && CurrentGpa < 0xFEB02000ULL)
            {
                ROSV_ERR("Memory: CopyFromGpa - GPA=0x%llX is in the virtio-net MMIO hole "
                         "(0xFEB01000-0xFEB02000), not backed by RAM", CurrentGpa);
            }
            else if (CurrentGpa >= 0xFEC00000ULL && CurrentGpa < 0xFEC01000ULL)
            {
                ROSV_ERR("Memory: CopyFromGpa - GPA=0x%llX is in the IOAPIC MMIO region "
                         "(0xFEC00000-0xFEC01000)", CurrentGpa);
            }
            else if (CurrentGpa >= 0xFEE00000ULL && CurrentGpa < 0xFEE01000ULL)
            {
                ROSV_ERR("Memory: CopyFromGpa - GPA=0x%llX is in the LAPIC MMIO region "
                         "(0xFEE00000-0xFEE01000)", CurrentGpa);
            }
            else
            {
                ROSV_ERR("Memory: CopyFromGpa - GPA=0x%llX not in any chunk (%u checked), "
                         "%llu bytes remaining",
                         CurrentGpa, g_AllocChunkCount, (ULONG64)Remaining);
            }
            return STATUS_INVALID_ADDRESS;
        }

        Dst += CopyLen;
        CurrentGpa += CopyLen;
        Remaining -= CopyLen;
    }

    return STATUS_SUCCESS;
}

/**
 * Verify that VA-mapped pages match the EPT-mapped (MDL PFN) pages.
 * Scans every page in every mapped chunk and compares MmGetPhysicalAddress
 * with the MDL PFN array. Reports any mismatches.
 */
VOID
RosvMemoryVerifyMappings(
    _In_ PROSV_VM Vm)
{
    ULONG i;
    ULONG TotalPages = 0, Mismatches = 0;

    UNREFERENCED_PARAMETER(Vm);

    for (i = 0; i < g_AllocChunkCount; i++)
    {
        PPFN_NUMBER PfnArray;
        ULONG PageCount, p;

        if (!g_AllocChunks[i].MappedVa)
            continue;

        PfnArray = MmGetMdlPfnArray(g_AllocChunks[i].Mdl);
        PageCount = (ULONG)(g_AllocChunks[i].SizeInBytes / PAGE_SIZE);

        for (p = 0; p < PageCount; p++)
        {
            PVOID PageVa = (PVOID)((ULONG_PTR)g_AllocChunks[i].MappedVa + (ULONG_PTR)p * PAGE_SIZE);
            PHYSICAL_ADDRESS PhysAddr = MmGetPhysicalAddress(PageVa);
            ULONG64 ExpectedHpa = (ULONG64)PfnArray[p] << PAGE_SHIFT;
            ULONG64 ActualHpa = PhysAddr.QuadPart;

            TotalPages++;
            if (ExpectedHpa != ActualHpa)
            {
                ULONG64 Gpa = g_AllocChunks[i].GuestPhysicalAddress + (ULONG64)p * PAGE_SIZE;
                ROSV_ERR("PFN MISMATCH! Chunk[%u] GPA=0x%llX page=%u: MDL_HPA=0x%llX VA_HPA=0x%llX",
                         i, Gpa, p, ExpectedHpa, ActualHpa);
                Mismatches++;
                if (Mismatches >= 10)
                {
                    ROSV_ERR("... (stopping after 10 mismatches)");
                    return;
                }
            }
        }
    }

    ROSV_ERR("PFN verification: %u pages checked, %u mismatches", TotalPages, Mismatches);
}
