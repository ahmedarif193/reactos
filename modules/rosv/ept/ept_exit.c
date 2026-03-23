/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     EPT violation and misconfiguration exit handlers
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 *
 * EPT violations on MMIO regions trigger instruction decode + device dispatch.
 * The decoder determines which register, access size, and direction (R/W),
 * then routes to the appropriate device emulation (virtio-blk, etc.).
 */

#include <rosv/rosv.h>
#include <rosv/ept.h>
#include <rosv/vm.h>
#include <rosv/vmx.h>
#include <rosv/virtio_blk.h>
#include <rosv/virtio_net.h>
#include <rosv/virtio_console.h>
#include <rosv/device.h>
#include <rosv/insn_decode.h>

#define NDEBUG
#include <debug.h>

/* ---- EPT violation exit qualification bits ------------------------------ */

#define EPT_QUAL_READ               (1ULL << 0)
#define EPT_QUAL_WRITE              (1ULL << 1)
#define EPT_QUAL_EXECUTE            (1ULL << 2)
#define EPT_QUAL_READABLE           (1ULL << 3)
#define EPT_QUAL_WRITABLE           (1ULL << 4)
#define EPT_QUAL_EXECUTABLE         (1ULL << 5)
#define EPT_QUAL_GPA_VALID          (1ULL << 7)
#define EPT_QUAL_LINEAR_VALID       (1ULL << 8)

/* ---- MMIO regions ------------------------------------------------------- */

#define LAPIC_BASE                  0xFEE00000ULL
#define LAPIC_SIZE                  0x1000ULL
#define IOAPIC_BASE                 0xFEC00000ULL
#define IOAPIC_SIZE                 0x1000ULL

/* ---- MMIO region identification ----------------------------------------- */

typedef enum _MMIO_REGION_TYPE {
    MmioRegionUnknown = 0,
    MmioRegionLapic,
    MmioRegionIoapic,
    MmioRegionVgaHole,
    MmioRegionVirtioBlk,
    MmioRegionVirtioNet,
    MmioRegionVirtioCon,
} MMIO_REGION_TYPE;

static
MMIO_REGION_TYPE
RosvEptIdentifyMmioRegion(
    _In_ ULONG64 GuestPhysicalAddress)
{
    /* Virtio-blk MMIO region */
    if (GuestPhysicalAddress >= ROSV_VIRTIO_BLK_MMIO_BASE &&
        GuestPhysicalAddress < ROSV_VIRTIO_BLK_MMIO_BASE + ROSV_VIRTIO_BLK_MMIO_SIZE)
    {
        return MmioRegionVirtioBlk;
    }

    /* Virtio-net MMIO region */
    if (GuestPhysicalAddress >= ROSV_VIRTIO_NET_MMIO_BASE &&
        GuestPhysicalAddress < ROSV_VIRTIO_NET_MMIO_BASE + ROSV_VIRTIO_NET_MMIO_SIZE)
    {
        return MmioRegionVirtioNet;
    }

    /* Virtio-console MMIO region */
    if (GuestPhysicalAddress >= ROSV_VIRTIO_CON_MMIO_BASE &&
        GuestPhysicalAddress < ROSV_VIRTIO_CON_MMIO_BASE + ROSV_VIRTIO_CON_MMIO_SIZE)
    {
        return MmioRegionVirtioCon;
    }

    /* Local APIC */
    if (GuestPhysicalAddress >= LAPIC_BASE &&
        GuestPhysicalAddress < LAPIC_BASE + LAPIC_SIZE)
    {
        return MmioRegionLapic;
    }

    /* I/O APIC */
    if (GuestPhysicalAddress >= IOAPIC_BASE &&
        GuestPhysicalAddress < IOAPIC_BASE + IOAPIC_SIZE)
    {
        return MmioRegionIoapic;
    }

    /* VGA/ROM/BIOS hole: 0xA0000 - 0xFFFFF */
    if (GuestPhysicalAddress >= 0xA0000ULL &&
        GuestPhysicalAddress < 0x100000ULL)
    {
        return MmioRegionVgaHole;
    }

    return MmioRegionUnknown;
}

static
const char *
RosvMmioRegionName(
    _In_ MMIO_REGION_TYPE Type)
{
    switch (Type)
    {
        case MmioRegionLapic:     return "LAPIC";
        case MmioRegionIoapic:    return "IOAPIC";
        case MmioRegionVgaHole:   return "VGA/ROM";
        case MmioRegionVirtioBlk: return "virtio-blk";
        case MmioRegionVirtioNet: return "virtio-net";
        case MmioRegionVirtioCon: return "virtio-con";
        default:                  return "unknown";
    }
}

/* ---- GVA→GPA translation cache (avoids 4-level page walks) -------------- */

/*
 * Small direct-mapped cache for GVA page → GPA page translations.
 * MMIO instruction decode hits the same few RIPs repeatedly (virtio driver
 * register access routines), so even a tiny cache has a very high hit rate.
 * Invalidated on CR3 writes (guest context switch).
 */
#define ROSV_GVA_CACHE_SHIFT    12
#define ROSV_GVA_CACHE_SIZE     16  /* Must be power of 2 */
#define ROSV_GVA_CACHE_MASK     (ROSV_GVA_CACHE_SIZE - 1)

typedef struct _ROSV_GVA_CACHE_ENTRY {
    ULONG64 GvaPage;       /* GVA with low 12 bits cleared */
    ULONG64 GpaPage;       /* Corresponding GPA page base */
    ULONG64 Cr3;           /* CR3 value when this entry was cached */
    BOOLEAN Valid;
} ROSV_GVA_CACHE_ENTRY;

static ROSV_GVA_CACHE_ENTRY g_GvaCache[ROSV_GVA_CACHE_SIZE];

VOID
RosvEptGvaCacheInvalidate(VOID)
{
    ULONG i;
    for (i = 0; i < ROSV_GVA_CACHE_SIZE; i++)
        g_GvaCache[i].Valid = FALSE;
}

static
BOOLEAN
RosvGvaCacheLookup(
    _In_ ULONG64 Gva,
    _In_ ULONG64 Cr3,
    _Out_ PULONG64 Gpa)
{
    ULONG Index = (ULONG)((Gva >> ROSV_GVA_CACHE_SHIFT) & ROSV_GVA_CACHE_MASK);
    ROSV_GVA_CACHE_ENTRY *Entry = &g_GvaCache[Index];

    if (Entry->Valid &&
        Entry->Cr3 == Cr3 &&
        Entry->GvaPage == (Gva & ~0xFFFULL))
    {
        *Gpa = Entry->GpaPage | (Gva & 0xFFFULL);
        return TRUE;
    }
    return FALSE;
}

static
VOID
RosvGvaCacheInsert(
    _In_ ULONG64 Gva,
    _In_ ULONG64 Gpa,
    _In_ ULONG64 Cr3)
{
    ULONG Index = (ULONG)((Gva >> ROSV_GVA_CACHE_SHIFT) & ROSV_GVA_CACHE_MASK);
    ROSV_GVA_CACHE_ENTRY *Entry = &g_GvaCache[Index];

    Entry->GvaPage = Gva & ~0xFFFULL;
    Entry->GpaPage = Gpa & ~0xFFFULL;
    Entry->Cr3 = Cr3;
    Entry->Valid = TRUE;
}

/* ---- Guest page table walk ---------------------------------------------- */

/*
 * Walk the guest's 4-level page table (CR3 → PML4 → PDPT → PD → PT)
 * to translate a guest virtual address (GVA) to a guest physical address (GPA).
 *
 * Handles 4KB, 2MB, and 1GB page sizes.  LA57 (5-level paging) is
 * stripped from CPUID so only 4-level walks are needed.
 *
 * Uses a small direct-mapped cache to avoid the expensive 4-level walk
 * on repeated accesses to the same page (common for MMIO instruction decode).
 */
static
BOOLEAN
RosvGvaToGpa(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 Gva,
    _Out_ PULONG64 Gpa)
{
    ULONG64 Cr3;
    ULONG64 Entry;
    ULONG64 TableGpa;
    NTSTATUS Status;
    ULONG Index;

    *Gpa = 0;

    /* Read CR3 (page table root) — mask off PCID/flags bits */
    Cr3 = RosvVmcsRead(VMCS_GUEST_CR3) & ~0xFFFULL;

    /* Fast path: check GVA cache before doing the expensive page walk */
    if (RosvGvaCacheLookup(Gva, Cr3, Gpa))
        return TRUE;

    /* PML4 */
    Index = (ULONG)((Gva >> 39) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, Cr3 + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    /* PDPT */
    TableGpa = Entry & 0x000FFFFFFFFFF000ULL;
    Index = (ULONG)((Gva >> 30) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, TableGpa + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    if (Entry & 0x80) /* 1GB huge page */
    {
        *Gpa = (Entry & 0x000FFFFFC0000000ULL) | (Gva & 0x3FFFFFFFULL);
        RosvGvaCacheInsert(Gva, *Gpa, Cr3);
        return TRUE;
    }

    /* PD */
    TableGpa = Entry & 0x000FFFFFFFFFF000ULL;
    Index = (ULONG)((Gva >> 21) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, TableGpa + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    if (Entry & 0x80) /* 2MB large page */
    {
        *Gpa = (Entry & 0x000FFFFFFFE00000ULL) | (Gva & 0x1FFFFFULL);
        RosvGvaCacheInsert(Gva, *Gpa, Cr3);
        return TRUE;
    }

    /* PT */
    TableGpa = Entry & 0x000FFFFFFFFFF000ULL;
    Index = (ULONG)((Gva >> 12) & 0x1FF);
    Status = RosvMemoryCopyFromGpa(Vm, &Entry, TableGpa + Index * 8, sizeof(Entry));
    if (!NT_SUCCESS(Status) || !(Entry & 1))
        return FALSE;

    *Gpa = (Entry & 0x000FFFFFFFFFF000ULL) | (Gva & 0xFFFULL);
    RosvGvaCacheInsert(Gva, *Gpa, Cr3);
    return TRUE;
}

/* ---- Instruction fetch from guest memory -------------------------------- */

/*
 * Fetch instruction bytes from guest memory for the faulting RIP.
 *
 * Walks the guest page tables (CR3-based) to translate GVA → GPA,
 * then reads instruction bytes from guest physical memory.
 */
static
BOOLEAN
RosvFetchGuestInstructionBytes(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestRip,
    _In_ ULONG InstrLen,
    _Out_writes_(InstrLen) UCHAR *Buffer)
{
    ULONG64 InstrGpa;
    NTSTATUS Status;

    if (!RosvGvaToGpa(Vm, GuestRip, &InstrGpa))
    {
        ROSV_ERR("MMIO: Page table walk failed for RIP=0x%llX (CR3=0x%llX)",
                 GuestRip, RosvVmcsRead(VMCS_GUEST_CR3));
        return FALSE;
    }

    /* Check GPA is within guest RAM */
    if (InstrGpa >= Vm->GuestRamSize)
    {
        ROSV_ERR("MMIO: Cannot fetch instruction - GPA=0x%llX out of range (RIP=0x%llX, RamSize=0x%llX)",
                 InstrGpa, GuestRip, Vm->GuestRamSize);
        return FALSE;
    }

    /* Check if instruction crosses page boundary */
    {
        ULONG PageOffset = (ULONG)(GuestRip & 0xFFF);
        if (PageOffset + InstrLen > PAGE_SIZE)
        {
            /* Instruction straddles page boundary - read in two parts */
            ULONG FirstPartLen = PAGE_SIZE - PageOffset;
            ULONG SecondPartLen = InstrLen - FirstPartLen;
            ULONG64 SecondGva = (GuestRip & ~0xFFFULL) + PAGE_SIZE;
            ULONG64 SecondGpa;

            /* Read first part from current page */
            Status = RosvMemoryCopyFromGpa(Vm, Buffer, InstrGpa, FirstPartLen);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("Failed to read first part of cross-page instruction");
                return FALSE;
            }

            /* Translate and read second part */
            if (!RosvGvaToGpa(Vm, SecondGva, &SecondGpa))
            {
                ROSV_ERR("Failed to translate second page for cross-page instruction");
                return FALSE;
            }

            Status = RosvMemoryCopyFromGpa(Vm, Buffer + FirstPartLen, SecondGpa, SecondPartLen);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("Failed to read second part of cross-page instruction");
                return FALSE;
            }

            return TRUE;
        }
    }

    Status = RosvMemoryCopyFromGpa(Vm, Buffer, InstrGpa, InstrLen);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("MMIO: Cannot fetch instruction - CopyFromGpa failed for GPA=0x%llX (RIP=0x%llX) Status=0x%08X",
                 InstrGpa, GuestRip, Status);
        return FALSE;
    }

    return TRUE;
}

/* ---- IOAPIC MMIO dispatch ----------------------------------------------- */

#define ROSV_IOAPIC_IOREGSEL_OFFSET 0x00
#define ROSV_IOAPIC_IOWIN_OFFSET    0x10

#define ROSV_LAPIC_ID_OFFSET        0x020
#define ROSV_LAPIC_VERSION_OFFSET   0x030
#define ROSV_LAPIC_TPR_OFFSET       0x080
#define ROSV_LAPIC_EOI_OFFSET       0x0B0
#define ROSV_LAPIC_LDR_OFFSET       0x0D0
#define ROSV_LAPIC_DFR_OFFSET       0x0E0
#define ROSV_LAPIC_SVR_OFFSET       0x0F0
#define ROSV_LAPIC_ESR_OFFSET       0x280
#define ROSV_LAPIC_LVT_TIMER_OFFSET 0x320
#define ROSV_LAPIC_TIMER_INIT_OFFSET 0x380
#define ROSV_LAPIC_TIMER_CURR_OFFSET 0x390
#define ROSV_LAPIC_TIMER_DIV_OFFSET  0x3E0
#define ROSV_LAPIC_TIMER_PERIODIC   (1U << 17)
#define ROSV_LAPIC_TIMER_BASE_HZ    100000000ULL

static
ULONG64
RosvLapicReadQpc(
    VOID)
{
    LARGE_INTEGER Counter;
    Counter = KeQueryPerformanceCounter(NULL);
    return (ULONG64)Counter.QuadPart;
}

static
ULONG64
RosvLapicReadQpcFrequency(
    VOID)
{
    LARGE_INTEGER Frequency;

    /* Always query fresh frequency to avoid latching a transient invalid value. */
    KeQueryPerformanceCounter(&Frequency);
    if (Frequency.QuadPart <= 0)
        return 0;
    return (ULONG64)Frequency.QuadPart;
}

static
ULONG
RosvLapicTimerDivideValue(
    _In_ ULONG DivideConfig)
{
    switch (((DivideConfig >> 1) & 0x4) | (DivideConfig & 0x3))
    {
        case 0: return 2;
        case 1: return 4;
        case 2: return 8;
        case 3: return 16;
        case 4: return 32;
        case 5: return 64;
        case 6: return 128;
        case 7: return 1;
        default: return 2;
    }
}

static
ULONG64
RosvLapicTimerElapsedTicks(
    _In_ PROSV_VM Vm)
{
    ULONG64 FrequencyQpc;
    ULONG64 NowQpc;
    ULONG64 DeltaQpc;
    ULONG Divisor;
    ULONG64 ElapsedTimerTicks;

    if (Vm->Lapic.TimerInitialCount == 0 || Vm->Lapic.TimerStartQpc == 0)
        return 0;

    FrequencyQpc = RosvLapicReadQpcFrequency();
    if (FrequencyQpc == 0)
        return 0;

    NowQpc = RosvLapicReadQpc();
    if (NowQpc <= Vm->Lapic.TimerStartQpc)
        return 0;

    DeltaQpc = NowQpc - Vm->Lapic.TimerStartQpc;
    Divisor = RosvLapicTimerDivideValue(Vm->Lapic.TimerDivideConfig);

    /*
     * Avoid ULONG64 overflow: DeltaQpc * BASE_HZ can exceed 2^64 after ~53s
     * when FrequencyQpc ~3.5 GHz.  Split into whole-seconds and remainder.
     */
    {
        ULONG64 WholeSec = DeltaQpc / FrequencyQpc;
        ULONG64 RemQpc   = DeltaQpc % FrequencyQpc;

        ElapsedTimerTicks = WholeSec * ROSV_LAPIC_TIMER_BASE_HZ
                          + (RemQpc * ROSV_LAPIC_TIMER_BASE_HZ) / FrequencyQpc;
    }
    return ElapsedTimerTicks / (ULONG64)Divisor;
}

static
ULONG64
RosvLapicTimerExpiredPeriods(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 ElapsedTicks)
{
    ULONG Init = Vm->Lapic.TimerInitialCount;

    if (Init == 0)
        return 0;

    if (Vm->Lapic.LvtTimer & ROSV_LAPIC_TIMER_PERIODIC)
        return ElapsedTicks / (ULONG64)Init;

    return (ElapsedTicks >= (ULONG64)Init) ? 1ULL : 0ULL;
}

static
VOID
RosvLapicRefreshTimerCurrentCount(
    _Inout_ PROSV_VM Vm)
{
    ULONG Init = Vm->Lapic.TimerInitialCount;
    ULONG64 ElapsedTicks;
    ULONG64 Remainder;

    if (Init == 0 || Vm->Lapic.TimerStartQpc == 0)
    {
        Vm->Lapic.TimerCurrentCount = 0;
        return;
    }

    ElapsedTicks = RosvLapicTimerElapsedTicks(Vm);
    if (Vm->Lapic.LvtTimer & ROSV_LAPIC_TIMER_PERIODIC)
    {
        Remainder = ElapsedTicks % (ULONG64)Init;
        if (Remainder == 0 && ElapsedTicks != 0)
            Vm->Lapic.TimerCurrentCount = Init;
        else
            Vm->Lapic.TimerCurrentCount = Init - (ULONG)Remainder;
        return;
    }

    if (ElapsedTicks >= (ULONG64)Init)
        Vm->Lapic.TimerCurrentCount = 0;
    else
        Vm->Lapic.TimerCurrentCount = Init - (ULONG)ElapsedTicks;
}

static
ULONG64
RosvMmioMaskForSize(
    _In_ UCHAR Size)
{
    switch (Size)
    {
        case 1: return 0xFFULL;
        case 2: return 0xFFFFULL;
        case 4: return 0xFFFFFFFFULL;
        case 8: return 0xFFFFFFFFFFFFFFFFULL;
        default: return 0;
    }
}

static
ULONG
RosvIoApicReadSelectedRegister(
    _In_ PROSV_VM Vm)
{
    ULONG Reg = Vm->IoApic.IoRegSel & 0xFF;
    ULONG Entry;

    switch (Reg)
    {
        case 0x00: /* IOAPICID */
            return (Vm->IoApic.Id & 0x0F) << 24;

        case 0x01: /* IOAPICVER */
            return ((ROSV_IOAPIC_REDIRECTION_ENTRIES - 1) << 16) | 0x11;

        case 0x02: /* IOAPICARB */
            return (Vm->IoApic.Id & 0x0F) << 24;

        default:
            if (Reg >= 0x10 && Reg < (0x10 + ROSV_IOAPIC_REDIRECTION_ENTRIES * 2))
            {
                Entry = (Reg - 0x10) >> 1;
                if ((Reg & 1) == 0)
                    return Vm->IoApic.RedirectionLow[Entry];
                return Vm->IoApic.RedirectionHigh[Entry];
            }
            return 0;
    }
}

static
VOID
RosvIoApicWriteSelectedRegister(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG Value)
{
    ULONG Reg = Vm->IoApic.IoRegSel & 0xFF;
    ULONG Entry;

    switch (Reg)
    {
        case 0x00: /* IOAPICID */
            Vm->IoApic.Id = (Value >> 24) & 0x0F;
            return;

        case 0x01: /* IOAPICVER (read-only) */
        case 0x02: /* IOAPICARB (read-only) */
            return;

        default:
            break;
    }

    if (Reg < 0x10 || Reg >= (0x10 + ROSV_IOAPIC_REDIRECTION_ENTRIES * 2))
        return;

    Entry = (Reg - 0x10) >> 1;
    if ((Reg & 1) == 0)
    {
        ULONG OldLow = Vm->IoApic.RedirectionLow[Entry];
        Vm->IoApic.RedirectionLow[Entry] = Value;
        if (OldLow != Value)
        {
            ROSV_TRACE("IOAPIC: RTE[%u].low = 0x%08X (vec=%u dm=%u mask=%u trig=%u)",
                       Entry,
                       Value,
                       Value & 0xFF,
                       (Value >> 8) & 0x7,
                       (Value >> 16) & 0x1,
                       (Value >> 15) & 0x1);
        }
    }
    else
    {
        ULONG NewHigh = Value & 0xFF000000;
        ULONG OldHigh = Vm->IoApic.RedirectionHigh[Entry];
        /* Destination APIC ID lives in bits [31:24]. */
        Vm->IoApic.RedirectionHigh[Entry] = NewHigh;
        if (OldHigh != NewHigh)
        {
            ROSV_TRACE("IOAPIC: RTE[%u].high = 0x%08X (dest_apic=%u)",
                       Entry,
                       NewHigh,
                       (NewHigh >> 24) & 0xFF);
        }
    }
}

static
BOOLEAN
RosvIoApicMmioRead(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ UCHAR Size,
    _Out_ PULONG64 Value)
{
    ULONG Offset = (ULONG)(GuestPhysicalAddress - IOAPIC_BASE);
    ULONG AlignedOffset = Offset & ~3U;
    ULONG Shift = (Offset & 3U) * 8U;
    ULONG64 Mask = RosvMmioMaskForSize(Size);
    ULONG RegValue;

    if (Mask == 0 || Size > 4)
        return FALSE;

    if (AlignedOffset == ROSV_IOAPIC_IOREGSEL_OFFSET)
    {
        RegValue = Vm->IoApic.IoRegSel;
    }
    else if (AlignedOffset == ROSV_IOAPIC_IOWIN_OFFSET)
    {
        RegValue = RosvIoApicReadSelectedRegister(Vm);
    }
    else
    {
        RegValue = 0;
    }

    *Value = (RegValue >> Shift) & Mask;
    return TRUE;
}

static
BOOLEAN
RosvIoApicMmioWrite(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ UCHAR Size,
    _In_ ULONG64 Value)
{
    ULONG Offset = (ULONG)(GuestPhysicalAddress - IOAPIC_BASE);
    ULONG AlignedOffset = Offset & ~3U;
    ULONG Shift = (Offset & 3U) * 8U;
    ULONG64 Mask = RosvMmioMaskForSize(Size);
    ULONG Current;
    ULONG NewValue;

    if (Mask == 0 || Size > 4)
        return FALSE;

    if (AlignedOffset == ROSV_IOAPIC_IOREGSEL_OFFSET)
    {
        Current = Vm->IoApic.IoRegSel;
    }
    else if (AlignedOffset == ROSV_IOAPIC_IOWIN_OFFSET)
    {
        Current = RosvIoApicReadSelectedRegister(Vm);
    }
    else
    {
        return TRUE;
    }

    NewValue = (Current & ~((ULONG)Mask << Shift)) |
               (((ULONG)Value & (ULONG)Mask) << Shift);

    if (AlignedOffset == ROSV_IOAPIC_IOREGSEL_OFFSET)
    {
        Vm->IoApic.IoRegSel = NewValue;
    }
    else
    {
        RosvIoApicWriteSelectedRegister(Vm, NewValue);
    }

    return TRUE;
}

static
DECLSPEC_NOINLINE
BOOLEAN
RosvHandleIoApicMmio(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress)
{
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);
    ULONG InstrLen = (ULONG)RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
    UCHAR InsnBuf[MMIO_INSN_MAX_BYTES];
    MMIO_INSN_INFO InsnInfo;
    ULONG64 Value;
    BOOLEAN Ok;

    if (InstrLen == 0 || InstrLen > MMIO_INSN_MAX_BYTES)
    {
        ROSV_ERR("MMIO IOAPIC: Invalid instruction length %u at RIP=0x%llX",
                 InstrLen, GuestRip);
        return FALSE;
    }

    if (!RosvFetchGuestInstructionBytes(Vm, GuestRip, InstrLen, InsnBuf))
        return FALSE;

    if (!RosvMmioDecodeInstruction(InsnBuf, InstrLen, &InsnInfo))
    {
        ROSV_ERR("MMIO IOAPIC: Instruction decode failed at RIP=0x%llX GPA=0x%llX",
                 GuestRip, GuestPhysicalAddress);
        return FALSE;
    }

    if (InsnInfo.AccessType == MmioAccessWrite)
    {
        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            Value = RosvGetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex);
            if (InsnInfo.AccessSize < 8)
                Value &= RosvMmioMaskForSize(InsnInfo.AccessSize);
        }
        else
        {
            Value = InsnInfo.ImmediateValue;
        }

        Ok = RosvIoApicMmioWrite(Vm, GuestPhysicalAddress, InsnInfo.AccessSize, Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO IOAPIC: Write failed GPA=0x%llX size=%u value=0x%llX",
                     GuestPhysicalAddress, InsnInfo.AccessSize, Value);
            return FALSE;
        }
    }
    else
    {
        Value = 0;
        Ok = RosvIoApicMmioRead(Vm, GuestPhysicalAddress, InsnInfo.AccessSize, &Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO IOAPIC: Read failed GPA=0x%llX size=%u",
                     GuestPhysicalAddress, InsnInfo.AccessSize);
            return FALSE;
        }

        if (InsnInfo.RegIndex == (ULONG)-1)
        {
            ROSV_ERR("MMIO IOAPIC: Read with no destination register");
            return FALSE;
        }

        if (InsnInfo.AccessSize == 4)
            Value &= 0xFFFFFFFF;

        RosvSetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex, Value);
    }

    RosvVmcsWrite(VMCS_GUEST_RIP, GuestRip + InstrLen);
    return TRUE;
}

/* ---- LAPIC MMIO dispatch ------------------------------------------------ */

static
ULONG
RosvLapicReadRegister(
    _In_ PROSV_VM Vm,
    _In_ ULONG Offset)
{
    switch (Offset)
    {
        case ROSV_LAPIC_ID_OFFSET:
            return (Vm->Lapic.Id & 0xFF) << 24;

        case ROSV_LAPIC_VERSION_OFFSET:
            return Vm->Lapic.Version;

        case ROSV_LAPIC_TPR_OFFSET:
            return Vm->Lapic.Tpr & 0xFF;

        case ROSV_LAPIC_LDR_OFFSET:
            return Vm->Lapic.Ldr;

        case ROSV_LAPIC_DFR_OFFSET:
            return Vm->Lapic.Dfr;

        case ROSV_LAPIC_SVR_OFFSET:
            return Vm->Lapic.Svr;

        case ROSV_LAPIC_ESR_OFFSET:
            return Vm->Lapic.ErrorStatus;

        case ROSV_LAPIC_LVT_TIMER_OFFSET:
            return Vm->Lapic.LvtTimer;

        case ROSV_LAPIC_TIMER_INIT_OFFSET:
            return Vm->Lapic.TimerInitialCount;

        case ROSV_LAPIC_TIMER_CURR_OFFSET:
            RosvLapicRefreshTimerCurrentCount(Vm);
            return Vm->Lapic.TimerCurrentCount;

        case ROSV_LAPIC_TIMER_DIV_OFFSET:
            return Vm->Lapic.TimerDivideConfig;

        default:
            return 0;
    }
}

static
VOID
RosvLapicWriteRegister(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    switch (Offset)
    {
        case ROSV_LAPIC_ID_OFFSET:
            Vm->Lapic.Id = (Value >> 24) & 0xFF;
            return;

        case ROSV_LAPIC_VERSION_OFFSET:
            return; /* read-only */

        case ROSV_LAPIC_TPR_OFFSET:
            Vm->Lapic.Tpr = Value & 0xFF;
            return;

        case ROSV_LAPIC_EOI_OFFSET:
            /*
             * Re-arm device lines that remain asserted after guest EOI.
             * TODO: model per-INTIN in-service/remote-IRR state so
             * reassertion follows trigger-mode semantics exactly.
             *
             * Also clear PIC ISR bits so that device IRQs routed through
             * the PIC fallback path (when IOAPIC route is unavailable)
             * can be re-injected.  The guest in APIC mode sends EOI only
             * to the LAPIC, never to the PIC.
             */
            RosvPicClearAllIsr();
            RosvVirtioBlkOnGuestEoi(&Vm->VirtioBlk);
            RosvVirtioNetOnGuestEoi(&Vm->VirtioNet);
            return;

        case ROSV_LAPIC_LDR_OFFSET:
            Vm->Lapic.Ldr = Value & 0xFF000000;
            return;

        case ROSV_LAPIC_DFR_OFFSET:
            Vm->Lapic.Dfr = Value;
            return;

        case ROSV_LAPIC_SVR_OFFSET:
            if (Vm->Lapic.Svr != Value)
            {
                ROSV_TRACE("LAPIC: SVR = 0x%08X (enabled=%u spurious_vec=%u)",
                           Value,
                           (Value >> 8) & 0x1,
                           Value & 0xFF);
            }
            Vm->Lapic.Svr = Value;
            return;

        case ROSV_LAPIC_ESR_OFFSET:
            Vm->Lapic.ErrorStatus = Value;
            return;

        case ROSV_LAPIC_LVT_TIMER_OFFSET:
            if (Vm->Lapic.LvtTimer != Value)
            {
                ROSV_TRACE("LAPIC: LVT_TIMER = 0x%08X (vec=%u mask=%u periodic=%u)",
                           Value,
                           Value & 0xFF,
                           (Value >> 16) & 0x1,
                           (Value >> 17) & 0x1);
            }
            Vm->Lapic.LvtTimer = Value;
            if (Vm->Lapic.TimerInitialCount != 0)
            {
                RosvLapicRefreshTimerCurrentCount(Vm);
                if (Value & (1U << 16))
                {
                    ULONG64 ElapsedTicks = RosvLapicTimerElapsedTicks(Vm);
                    Vm->Lapic.TimerDeliveredPeriods =
                        RosvLapicTimerExpiredPeriods(Vm, ElapsedTicks);
                }
            }
            return;

        case ROSV_LAPIC_TIMER_INIT_OFFSET:
            if (Vm->Lapic.TimerInitialCount != Value)
            {
                ROSV_TRACE("LAPIC: TIMER_INIT = 0x%08X", Value);
            }
            Vm->Lapic.TimerInitialCount = Value;
            Vm->Lapic.TimerCurrentCount = Value;
            Vm->Lapic.TimerStartQpc = RosvLapicReadQpc();
            Vm->Lapic.TimerDeliveredPeriods = 0;
            return;

        case ROSV_LAPIC_TIMER_CURR_OFFSET:
            return; /* read-only */

        case ROSV_LAPIC_TIMER_DIV_OFFSET:
            if (Vm->Lapic.TimerDivideConfig != Value)
            {
                ROSV_TRACE("LAPIC: TIMER_DIV = 0x%08X (divide=%u)",
                           Value,
                           RosvLapicTimerDivideValue(Value));
            }
            Vm->Lapic.TimerDivideConfig = Value;
            if (Vm->Lapic.TimerInitialCount != 0)
            {
                Vm->Lapic.TimerStartQpc = RosvLapicReadQpc();
                Vm->Lapic.TimerCurrentCount = Vm->Lapic.TimerInitialCount;
                Vm->Lapic.TimerDeliveredPeriods = 0;
            }
            return;

        default:
            return;
    }
}

static
BOOLEAN
RosvLapicMmioRead(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ UCHAR Size,
    _Out_ PULONG64 Value)
{
    ULONG Offset = (ULONG)(GuestPhysicalAddress - LAPIC_BASE);
    ULONG AlignedOffset = Offset & ~3U;
    ULONG Shift = (Offset & 3U) * 8U;
    ULONG64 Mask = RosvMmioMaskForSize(Size);
    ULONG RegValue;

    if (Mask == 0 || Size > 4)
        return FALSE;

    RegValue = RosvLapicReadRegister(Vm, AlignedOffset);
    *Value = (RegValue >> Shift) & Mask;
    return TRUE;
}

static
BOOLEAN
RosvLapicMmioWrite(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ UCHAR Size,
    _In_ ULONG64 Value)
{
    ULONG Offset = (ULONG)(GuestPhysicalAddress - LAPIC_BASE);
    ULONG AlignedOffset = Offset & ~3U;
    ULONG Shift = (Offset & 3U) * 8U;
    ULONG64 Mask = RosvMmioMaskForSize(Size);
    ULONG Current;
    ULONG NewValue;

    if (Mask == 0 || Size > 4)
        return FALSE;

    if (AlignedOffset == ROSV_LAPIC_EOI_OFFSET)
    {
        RosvLapicWriteRegister(Vm, ROSV_LAPIC_EOI_OFFSET, (ULONG)Value);
        return TRUE;
    }

    Current = RosvLapicReadRegister(Vm, AlignedOffset);
    NewValue = (Current & ~((ULONG)Mask << Shift)) |
               (((ULONG)Value & (ULONG)Mask) << Shift);

    RosvLapicWriteRegister(Vm, AlignedOffset, NewValue);
    return TRUE;
}

static
DECLSPEC_NOINLINE
BOOLEAN
RosvHandleLapicMmio(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress)
{
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);
    ULONG InstrLen = (ULONG)RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
    UCHAR InsnBuf[MMIO_INSN_MAX_BYTES];
    MMIO_INSN_INFO InsnInfo;
    ULONG64 Value;
    BOOLEAN Ok;

    if (InstrLen == 0 || InstrLen > MMIO_INSN_MAX_BYTES)
    {
        ROSV_ERR("MMIO LAPIC: Invalid instruction length %u at RIP=0x%llX",
                 InstrLen, GuestRip);
        return FALSE;
    }

    if (!RosvFetchGuestInstructionBytes(Vm, GuestRip, InstrLen, InsnBuf))
        return FALSE;

    if (!RosvMmioDecodeInstruction(InsnBuf, InstrLen, &InsnInfo))
    {
        ROSV_ERR("MMIO LAPIC: Instruction decode failed at RIP=0x%llX GPA=0x%llX",
                 GuestRip, GuestPhysicalAddress);
        return FALSE;
    }

    if (InsnInfo.AccessType == MmioAccessWrite)
    {
        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            Value = RosvGetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex);
            if (InsnInfo.AccessSize < 8)
                Value &= RosvMmioMaskForSize(InsnInfo.AccessSize);
        }
        else
        {
            Value = InsnInfo.ImmediateValue;
        }

        Ok = RosvLapicMmioWrite(Vm, GuestPhysicalAddress, InsnInfo.AccessSize, Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO LAPIC: Write failed GPA=0x%llX size=%u value=0x%llX",
                     GuestPhysicalAddress, InsnInfo.AccessSize, Value);
            return FALSE;
        }
    }
    else
    {
        Value = 0;
        Ok = RosvLapicMmioRead(Vm, GuestPhysicalAddress, InsnInfo.AccessSize, &Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO LAPIC: Read failed GPA=0x%llX size=%u",
                     GuestPhysicalAddress, InsnInfo.AccessSize);
            return FALSE;
        }

        if (InsnInfo.RegIndex == (ULONG)-1)
        {
            ROSV_ERR("MMIO LAPIC: Read with no destination register");
            return FALSE;
        }

        if (InsnInfo.AccessSize == 4)
            Value &= 0xFFFFFFFF;

        RosvSetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex, Value);
    }

    RosvVmcsWrite(VMCS_GUEST_RIP, GuestRip + InstrLen);
    return TRUE;
}

/* ---- Virtio-blk MMIO dispatch ------------------------------------------- */

/*
 * Handle an EPT violation on the virtio-blk MMIO region.
 * Decodes the faulting instruction to determine register, size, direction,
 * then dispatches to the virtio-blk device emulation.
 * Advances guest RIP past the emulated instruction on success.
 */
static
DECLSPEC_NOINLINE
BOOLEAN
RosvHandleVirtioBlkMmio(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 ExitQualification)
{
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);
    ULONG InstrLen = (ULONG)RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
    UCHAR InsnBuf[MMIO_INSN_MAX_BYTES];
    MMIO_INSN_INFO InsnInfo;
    BOOLEAN IsWrite = (ExitQualification & EPT_QUAL_WRITE) != 0;
    ULONG64 Value;
    BOOLEAN Ok;

    /* Sanity check instruction length */
    if (InstrLen == 0 || InstrLen > MMIO_INSN_MAX_BYTES)
    {
        ROSV_ERR("MMIO virtio-blk: Invalid instruction length %u at RIP=0x%llX GPA=0x%llX",
                 InstrLen, GuestRip, GuestPhysicalAddress);
        return FALSE;
    }

    /* Fetch instruction bytes from guest memory */
    if (!RosvFetchGuestInstructionBytes(Vm, GuestRip, InstrLen, InsnBuf))
    {
        ROSV_ERR("MMIO virtio-blk: Failed to fetch instruction at RIP=0x%llX (len=%u)",
                 GuestRip, InstrLen);
        return FALSE;
    }

    /* Decode the instruction */
    if (!RosvMmioDecodeInstruction(InsnBuf, InstrLen, &InsnInfo))
    {
        ROSV_ERR("MMIO virtio-blk: Instruction decode failed at RIP=0x%llX GPA=0x%llX",
                 GuestRip, GuestPhysicalAddress);
        ROSV_ERR("  Instruction bytes: %02X %02X %02X %02X %02X %02X",
                 InsnBuf[0], InstrLen > 1 ? InsnBuf[1] : 0,
                 InstrLen > 2 ? InsnBuf[2] : 0, InstrLen > 3 ? InsnBuf[3] : 0,
                 InstrLen > 4 ? InsnBuf[4] : 0, InstrLen > 5 ? InsnBuf[5] : 0);
        return FALSE;
    }

    /* Validate decoded direction matches EPT qualification */
    if (IsWrite && InsnInfo.AccessType != MmioAccessWrite)
    {
        ROSV_ERR("MMIO virtio-blk: EPT says WRITE but decoder says READ at RIP=0x%llX GPA=0x%llX",
                 GuestRip, GuestPhysicalAddress);
        /* Trust the decoder - the EPT qualification bit 1 indicates the guest attempted
         * a write, but for MOV r, [mem] the memory side is a read. EPT qualification
         * bit 0 (read) should be set instead. Log but continue with decoder result. */
    }

    if (InsnInfo.AccessType == MmioAccessWrite)
    {
        /* MMIO Write: get value from guest register or immediate */
        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            Value = RosvGetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex);
            /* Mask to access size */
            switch (InsnInfo.AccessSize)
            {
                case 1: Value &= 0xFF; break;
                case 2: Value &= 0xFFFF; break;
                case 4: Value &= 0xFFFFFFFF; break;
                /* 8: full 64-bit value */
            }
        }
        else
        {
            /* Immediate value */
            Value = InsnInfo.ImmediateValue;
        }

        Ok = RosvVirtioBlkMmioWrite(&Vm->VirtioBlk, GuestPhysicalAddress,
                                     InsnInfo.AccessSize, Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO virtio-blk: Write handler failed for GPA=0x%llX size=%u value=0x%llX",
                     GuestPhysicalAddress, InsnInfo.AccessSize, Value);
            return FALSE;
        }
    }
    else
    {
        /* MMIO Read: get value from device, write to guest register */
        Value = 0;
        Ok = RosvVirtioBlkMmioRead(&Vm->VirtioBlk, GuestPhysicalAddress,
                                    InsnInfo.AccessSize, &Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO virtio-blk: Read handler failed for GPA=0x%llX size=%u",
                     GuestPhysicalAddress, InsnInfo.AccessSize);
            return FALSE;
        }

        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            /* For 32-bit reads on x86-64, zero-extend to 64 bits (Intel behavior) */
            if (InsnInfo.AccessSize == 4)
                Value &= 0xFFFFFFFF;

            RosvSetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex, Value);
        }
        else
        {
            ROSV_ERR("MMIO virtio-blk: Read with no destination register at RIP=0x%llX",
                     GuestRip);
            return FALSE;
        }
    }

    /* Advance guest RIP past the emulated instruction */
    RosvVmcsWrite(VMCS_GUEST_RIP, GuestRip + InstrLen);
    return TRUE;
}

/* ---- Virtio-net MMIO dispatch ------------------------------------------- */

/*
 * Handle an EPT violation on the virtio-net MMIO region.
 * Same instruction decode + dispatch pattern as virtio-blk.
 */
static
DECLSPEC_NOINLINE
BOOLEAN
RosvHandleVirtioNetMmio(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 ExitQualification)
{
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);
    ULONG InstrLen = (ULONG)RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
    UCHAR InsnBuf[MMIO_INSN_MAX_BYTES];
    MMIO_INSN_INFO InsnInfo;
    BOOLEAN IsWrite = (ExitQualification & EPT_QUAL_WRITE) != 0;
    ULONG64 Value;
    BOOLEAN Ok;

    if (InstrLen == 0 || InstrLen > MMIO_INSN_MAX_BYTES)
    {
        ROSV_ERR("MMIO virtio-net: Invalid instruction length %u at RIP=0x%llX GPA=0x%llX",
                 InstrLen, GuestRip, GuestPhysicalAddress);
        return FALSE;
    }

    if (!RosvFetchGuestInstructionBytes(Vm, GuestRip, InstrLen, InsnBuf))
    {
        ROSV_ERR("MMIO virtio-net: Failed to fetch instruction at RIP=0x%llX (len=%u)",
                 GuestRip, InstrLen);
        return FALSE;
    }

    if (!RosvMmioDecodeInstruction(InsnBuf, InstrLen, &InsnInfo))
    {
        ROSV_ERR("MMIO virtio-net: Instruction decode failed at RIP=0x%llX GPA=0x%llX",
                 GuestRip, GuestPhysicalAddress);
        ROSV_ERR("  Instruction bytes: %02X %02X %02X %02X %02X %02X",
                 InsnBuf[0], InstrLen > 1 ? InsnBuf[1] : 0,
                 InstrLen > 2 ? InsnBuf[2] : 0, InstrLen > 3 ? InsnBuf[3] : 0,
                 InstrLen > 4 ? InsnBuf[4] : 0, InstrLen > 5 ? InsnBuf[5] : 0);
        return FALSE;
    }

    if (IsWrite && InsnInfo.AccessType != MmioAccessWrite)
    {
        /* Trust the decoder over EPT qualification (same rationale as virtio-blk) */
    }

    if (InsnInfo.AccessType == MmioAccessWrite)
    {
        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            Value = RosvGetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex);
            switch (InsnInfo.AccessSize)
            {
                case 1: Value &= 0xFF; break;
                case 2: Value &= 0xFFFF; break;
                case 4: Value &= 0xFFFFFFFF; break;
            }
        }
        else
        {
            Value = InsnInfo.ImmediateValue;
        }

        Ok = RosvVirtioNetMmioWrite(&Vm->VirtioNet, GuestPhysicalAddress,
                                     InsnInfo.AccessSize, Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO virtio-net: Write handler failed for GPA=0x%llX size=%u value=0x%llX",
                     GuestPhysicalAddress, InsnInfo.AccessSize, Value);
            return FALSE;
        }
    }
    else
    {
        Value = 0;
        Ok = RosvVirtioNetMmioRead(&Vm->VirtioNet, GuestPhysicalAddress,
                                    InsnInfo.AccessSize, &Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO virtio-net: Read handler failed for GPA=0x%llX size=%u",
                     GuestPhysicalAddress, InsnInfo.AccessSize);
            return FALSE;
        }

        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            if (InsnInfo.AccessSize == 4)
                Value &= 0xFFFFFFFF;

            RosvSetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex, Value);
        }
        else
        {
            ROSV_ERR("MMIO virtio-net: Read with no destination register at RIP=0x%llX",
                     GuestRip);
            return FALSE;
        }
    }

    RosvVmcsWrite(VMCS_GUEST_RIP, GuestRip + InstrLen);
    return TRUE;
}

/* ---- Virtio-console MMIO handler ---------------------------------------- */

DECLSPEC_NOINLINE
BOOLEAN
RosvHandleVirtioConMmio(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 ExitQualification)
{
    ULONG64 GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);
    ULONG InstrLen = (ULONG)RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
    UCHAR InsnBuf[MMIO_INSN_MAX_BYTES];
    MMIO_INSN_INFO InsnInfo;
    BOOLEAN IsWrite = (ExitQualification & EPT_QUAL_WRITE) != 0;
    ULONG64 Value;
    BOOLEAN Ok;

    if (InstrLen == 0 || InstrLen > MMIO_INSN_MAX_BYTES)
    {
        ROSV_ERR("MMIO virtio-con: Invalid instruction length %u at RIP=0x%llX GPA=0x%llX",
                 InstrLen, GuestRip, GuestPhysicalAddress);
        return FALSE;
    }

    if (!RosvFetchGuestInstructionBytes(Vm, GuestRip, InstrLen, InsnBuf))
    {
        ROSV_ERR("MMIO virtio-con: Failed to fetch instruction at RIP=0x%llX (len=%u)",
                 GuestRip, InstrLen);
        return FALSE;
    }

    if (!RosvMmioDecodeInstruction(InsnBuf, InstrLen, &InsnInfo))
    {
        ROSV_ERR("MMIO virtio-con: Instruction decode failed at RIP=0x%llX GPA=0x%llX",
                 GuestRip, GuestPhysicalAddress);
        return FALSE;
    }

    if (InsnInfo.AccessType == MmioAccessWrite)
    {
        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            Value = RosvGetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex);
            switch (InsnInfo.AccessSize)
            {
                case 1: Value &= 0xFF; break;
                case 2: Value &= 0xFFFF; break;
                case 4: Value &= 0xFFFFFFFF; break;
            }
        }
        else
        {
            Value = InsnInfo.ImmediateValue;
        }

        Ok = RosvVirtioConMmioWrite(&Vm->VirtioCon, GuestPhysicalAddress,
                                     InsnInfo.AccessSize, Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO virtio-con: Write handler failed for GPA=0x%llX size=%u value=0x%llX",
                     GuestPhysicalAddress, InsnInfo.AccessSize, Value);
            return FALSE;
        }
    }
    else
    {
        Value = 0;
        Ok = RosvVirtioConMmioRead(&Vm->VirtioCon, GuestPhysicalAddress,
                                    InsnInfo.AccessSize, &Value);
        if (!Ok)
        {
            ROSV_ERR("MMIO virtio-con: Read handler failed for GPA=0x%llX size=%u",
                     GuestPhysicalAddress, InsnInfo.AccessSize);
            return FALSE;
        }

        if (InsnInfo.RegIndex != (ULONG)-1)
        {
            if (InsnInfo.AccessSize == 4)
                Value &= 0xFFFFFFFF;

            RosvSetGuestRegByIndex(&Vcpu->GuestRegs, InsnInfo.RegIndex, Value);
        }
        else
        {
            ROSV_ERR("MMIO virtio-con: Read with no destination register at RIP=0x%llX",
                     GuestRip);
            return FALSE;
        }
    }

    RosvVmcsWrite(VMCS_GUEST_RIP, GuestRip + InstrLen);
    return TRUE;
}

/* ---- EPT violation handler (public) ------------------------------------- */

BOOLEAN
RosvEptHandleViolation(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG64 ExitQualification)
{
    static ULONG VmMismatchCount;
    BOOLEAN GpaValid = (ExitQualification & EPT_QUAL_GPA_VALID) != 0;
    MMIO_REGION_TYPE Region;

    if (Vcpu == NULL)
    {
        ROSV_ERR("EPT violation: NULL vCPU context");
        return FALSE;
    }

    if (Vm != Vcpu->Vm)
    {
        if (VmMismatchCount < 8 || (VmMismatchCount % 1024) == 0)
        {
            ROSV_WARN("EPT violation: VM pointer mismatch (arg=%p vcpu->Vm=%p) GPA=0x%llX qual=0x%llX",
                      Vm, Vcpu->Vm, GuestPhysicalAddress, ExitQualification);
        }
        VmMismatchCount++;
    }

    Vm = Vcpu->Vm;
    if (Vm == NULL)
    {
        ROSV_ERR("EPT violation: vCPU has no parent VM (GPA=0x%llX qual=0x%llX)",
                 GuestPhysicalAddress, ExitQualification);
        return FALSE;
    }

    /* Identify which MMIO region (if any) this access hits */
    Region = RosvEptIdentifyMmioRegion(GuestPhysicalAddress);

    if (Region != MmioRegionUnknown)
    {

        switch (Region)
        {
            case MmioRegionVirtioBlk:
                return RosvHandleVirtioBlkMmio(Vcpu, Vm,
                                                GuestPhysicalAddress,
                                                ExitQualification);

            case MmioRegionVirtioNet:
                return RosvHandleVirtioNetMmio(Vcpu, Vm,
                                                GuestPhysicalAddress,
                                                ExitQualification);

            case MmioRegionVirtioCon:
                return RosvHandleVirtioConMmio(Vcpu, Vm,
                                                GuestPhysicalAddress,
                                                ExitQualification);

            case MmioRegionIoapic:
                return RosvHandleIoApicMmio(Vcpu, Vm, GuestPhysicalAddress);

            case MmioRegionLapic:
                return RosvHandleLapicMmio(Vcpu, Vm, GuestPhysicalAddress);

            case MmioRegionVgaHole:
                /*
                 * VGA/ROM hole is RAM-backed in EPT. If we trap here,
                 * this is an EPT mapping bug.
                 */
                ROSV_ERR("EPT violation: Unexpected fault on %s at GPA=0x%llX "
                         "(should be backed by RAM pages in EPT)",
                         RosvMmioRegionName(Region), GuestPhysicalAddress);
                Vm->State = RosvVmStateError;
                return FALSE;

            default:
                break;
        }
    }

    /* If GPA is in valid RAM range but unmapped, this is a bug */
    if (GpaValid)
    {
        ROSV_ERR("EPT violation: GPA=0x%llX has valid translation but access denied "
                 "(qual=0x%llX). This indicates an EPT permission mismatch.",
                 GuestPhysicalAddress, ExitQualification);
    }
    else
    {
        ROSV_ERR("EPT violation: GPA=0x%llX is unmapped. Guest accessed memory "
                 "outside configured RAM regions.",
                 GuestPhysicalAddress);
    }

    ROSV_ERR("EPT violation: RIP=0x%llX RSP=0x%llX",
             RosvVmcsRead(VMCS_GUEST_RIP), RosvVmcsRead(VMCS_GUEST_RSP));
    ROSV_ERR("EPT violation: vCPU %u stopping VM due to unhandled EPT violation",
             Vcpu->VcpuId);
    Vm->State = RosvVmStateError;
    return FALSE;
}

BOOLEAN
RosvEptHandleMisconfiguration(
    _Inout_ PROSV_VCPU Vcpu,
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress)
{
    PEPT_PTE Entry;

    if (Vcpu == NULL)
    {
        ROSV_ERR("EPT misconfiguration: NULL vCPU context (GPA=0x%llX)",
                 GuestPhysicalAddress);
        return FALSE;
    }

    Vm = Vcpu->Vm;
    if (Vm == NULL)
    {
        ROSV_ERR("EPT misconfiguration: vCPU has no parent VM (GPA=0x%llX)",
                 GuestPhysicalAddress);
        return FALSE;
    }

    ROSV_ERR("EPT misconfiguration: GPA=0x%llX", GuestPhysicalAddress);

    /*
     * EPT misconfiguration means the EPT entry for this GPA has an invalid
     * combination of bits. This is always a hypervisor bug - the guest
     * cannot cause this by itself.
     *
     * Common causes:
     * - Memory type set on a non-leaf entry
     * - Reserved bits set
     * - Write-only or execute-only without hardware support
     */

    Entry = RosvEptLookup(&Vm->EptContext, GuestPhysicalAddress);
    if (Entry)
    {
        ROSV_ERR("EPT misconfiguration: Entry value=0x%llX (R=%u W=%u X=%u memtype=%u "
                 "large=%u PFN=0x%llX)",
                 Entry->Value,
                 (ULONG)Entry->Read, (ULONG)Entry->Write, (ULONG)Entry->Execute,
                 (ULONG)Entry->MemoryType, (ULONG)Entry->LargePage,
                 Entry->PageFrameNumber);
    }
    else
    {
        ROSV_ERR("EPT misconfiguration: No EPT entry found for GPA=0x%llX "
                 "(lookup failed at some level)", GuestPhysicalAddress);
    }

    ROSV_ERR("EPT misconfiguration is a hypervisor bug - stopping VM (vCPU %u)",
             Vcpu->VcpuId);
    Vm->State = RosvVmStateCrashed;
    return FALSE;
}
