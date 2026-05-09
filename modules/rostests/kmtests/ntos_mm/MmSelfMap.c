/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 self-map invariants test
 */

#include <kmt_test.h>

#ifdef _M_ARM64

VOID Test_MmSelfMap(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#define ARM64_TEST_PTE_TYPE_MASK    0x0000000000000003ULL
#define ARM64_TEST_PTE_TYPE_TABLE   0x0000000000000003ULL
#define ARM64_TEST_PTE_TYPE_PAGE    0x0000000000000003ULL
#define ARM64_TEST_PTE_ADDR_MASK    0x0000FFFFFFFFF000ULL
#define ARM64_TEST_PTE_VALID        0x0000000000000001ULL
#define ARM64_TEST_PTE_AF           0x0000000000000400ULL
#define ARM64_TEST_PTE_NG           0x0000000000000800ULL
#define ARM64_TEST_PTE_PXN          0x0020000000000000ULL
#define ARM64_TEST_PTE_UXN          0x0040000000000000ULL

#define ARM64_TEST_AP_SHIFT         6
#define ARM64_TEST_SH_SHIFT         8
#define ARM64_TEST_AP_KERNEL_RW     0ULL
#define ARM64_TEST_AP_READ_ONLY     2ULL
#define ARM64_TEST_SH_INNER         3ULL

#define ARM64_TEST_PTI_SHIFT        12
#define ARM64_TEST_PDI_SHIFT        21
#define ARM64_TEST_PPI_SHIFT        30
#define ARM64_TEST_PXI_SHIFT        39
#define ARM64_TEST_INDEX_MASK       0x1FFULL
#define ARM64_TEST_LEVEL_PXE        0
#define ARM64_TEST_LEVEL_PPE        1
#define ARM64_TEST_LEVEL_PDE        2
#define ARM64_TEST_LEVEL_PTE        3

/*
 * These constants describe the NT-style ARM64 self-map layout ReactOS now
 * exposes. The executable contract still discovers the recursive slot
 * dynamically and derives all addresses from that slot; the constants are
 * reference probes so the log shows when ReactOS drifts from the Win11 dump.
 */
#define ARM64_TEST_PXE_SELFMAP_INDEX 457ULL
#define ARM64_TEST_NO_SELFMAP_INDEX  ((ULONGLONG)-1)

#define ARM64_TEST_PXE_BASE         0xFFFFE4F2793C9000ULL
#define ARM64_TEST_PXE_SELFMAP      0xFFFFE4F2793C9E48ULL
#define ARM64_TEST_PPE_BASE         0xFFFFE4F279200000ULL
#define ARM64_TEST_PDE_BASE         0xFFFFE4F240000000ULL
#define ARM64_TEST_PTE_BASE         0xFFFFE48000000000ULL
#define ARM64_TEST_KSEG0_BASE       0xFFFF800000000000ULL
#define ARM64_TEST_L2_SPAN          (1ULL << ARM64_TEST_PDI_SHIFT)

#define ARM64_TEST_LEGACY_PXE_SELFMAP_INDEX 493ULL
#define ARM64_TEST_LEGACY_PXE_BASE         0xFFFFF6FB7DBED000ULL
#define ARM64_TEST_LEGACY_PPE_BASE         0xFFFFF6FB7DA00000ULL
#define ARM64_TEST_LEGACY_PDE_BASE         0xFFFFF6FB40000000ULL
#define ARM64_TEST_LEGACY_PTE_BASE         0xFFFFF68000000000ULL

#define ARM64_TEST_WIN11_HIGHEST_USER       0x00007FFFFFFEFFFFULL
#define ARM64_TEST_WIN11_USER_PROBE         0x00007FFFFFFF0000ULL
#define ARM64_TEST_WIN11_KUSER_SHARED_DATA  0xFFFFF78000000000ULL
#define ARM64_TEST_OBSERVED_KSEG0_FAULT_LOW  0xFFFF8000427BE000ULL
#define ARM64_TEST_OBSERVED_KSEG0_FAULT_HIGH 0xFFFF8000428E2000ULL

#define ARM64_TEST_POOL_TAG         'fSmM'

static volatile ULONG Arm64SelfMapWritableProbe = 0x13572468;

static
ULONGLONG
Arm64ReadTtbr0(VOID)
{
    ULONGLONG Value;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Value));
    return Value;
}

static
ULONGLONG
Arm64ReadTtbr1(VOID)
{
    ULONGLONG Value;

    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Value));
    return Value;
}

static
ULONGLONG
Arm64ReadTcr(VOID)
{
    ULONGLONG Value;

    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(Value));
    return Value;
}

static
ULONGLONG
Arm64ReadMair(VOID)
{
    ULONGLONG Value;

    __asm__ __volatile__("mrs %0, mair_el1" : "=r"(Value));
    return Value;
}

static
ULONGLONG
Arm64ReadSctlr(VOID)
{
    ULONGLONG Value;

    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Value));
    return Value;
}

static
ULONG
Arm64TcrPhysicalBits(
    _In_ ULONGLONG Tcr)
{
    switch ((Tcr >> 32) & 7)
    {
        case 0: return 32;
        case 1: return 36;
        case 2: return 40;
        case 3: return 42;
        case 4: return 44;
        case 5: return 48;
        case 6: return 52;
        default: return 0;
    }
}

static
PCSTR
Arm64TcrTg0(
    _In_ ULONGLONG Tcr)
{
    switch ((Tcr >> 14) & 3)
    {
        case 0: return "4K";
        case 1: return "64K";
        case 2: return "16K";
        default: return "reserved";
    }
}

static
PCSTR
Arm64TcrTg1(
    _In_ ULONGLONG Tcr)
{
    switch ((Tcr >> 30) & 3)
    {
        case 1: return "16K";
        case 2: return "4K";
        case 3: return "64K";
        default: return "reserved";
    }
}

static
ULONGLONG
Arm64SignExtend48(
    _In_ ULONGLONG Address)
{
    if (Address & (1ULL << 47))
        Address |= 0xFFFF000000000000ULL;

    return Address;
}

static
NTSTATUS
ProbeReadableRange(
    _In_ ULONG_PTR Address,
    _In_ SIZE_T Size)
{
    ULONG_PTR EndAddress;

    if (Size == 0)
        return STATUS_SUCCESS;

    if (Address > MAXULONG_PTR - (Size - 1))
        return STATUS_ACCESS_VIOLATION;

    EndAddress = Address + Size - 1;
    if (!MmIsAddressValid((PVOID)Address) ||
        !MmIsAddressValid((PVOID)EndAddress))
    {
        return STATUS_ACCESS_VIOLATION;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
ProbeUlong(
    _In_ volatile ULONG *Address,
    _Out_ PULONG Value)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG LocalValue = 0;

    Status = ProbeReadableRange((ULONG_PTR)Address, sizeof(*Address));
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        LocalValue = *Address;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
        *Value = LocalValue;

    return Status;
}

static
NTSTATUS
ProbeUlong64(
    _In_ volatile ULONGLONG *Address,
    _Out_ PULONGLONG Value)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONGLONG LocalValue = 0;

    Status = ProbeReadableRange((ULONG_PTR)Address, sizeof(*Address));
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        LocalValue = *Address;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
        *Value = LocalValue;

    return Status;
}

static
BOOLEAN
IsReactOS(VOID)
{
    ULONG Marker;
    NTSTATUS Status;

    Status = ProbeUlong((volatile ULONG *)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)),
                        &Marker);

    return NT_SUCCESS(Status) && Marker == 0x8eac705;
}

static
ULONGLONG
Arm64AddressToPde(
    _In_ PVOID Address)
{
    ULONGLONG Offset = ((ULONGLONG)Address >> (ARM64_TEST_PDI_SHIFT - 3)) &
                       (0x7FFFFFFULL << 3);

    return ARM64_TEST_PDE_BASE + Offset;
}

static
BOOLEAN
Arm64IsTableDescriptor(
    _In_ ULONGLONG Entry)
{
    return (Entry & ARM64_TEST_PTE_TYPE_MASK) == ARM64_TEST_PTE_TYPE_TABLE;
}

static
BOOLEAN
Arm64IsPageDescriptor(
    _In_ ULONGLONG Entry)
{
    return (Entry & ARM64_TEST_PTE_TYPE_MASK) == ARM64_TEST_PTE_TYPE_PAGE;
}

static
ULONGLONG
Arm64PfnFromEntry(
    _In_ ULONGLONG Entry)
{
    return (Entry & ARM64_TEST_PTE_ADDR_MASK) >> PAGE_SHIFT;
}

static
PVOID
Arm64PageBase(
    _In_ PVOID Address)
{
    return (PVOID)((ULONG_PTR)Address & ~(ULONG_PTR)(PAGE_SIZE - 1));
}

static
PVOID
Arm64Kseg0AliasForPhysicalAddress(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG_PTR PageOffset)
{
    ULONGLONG PhysicalPage = (ULONGLONG)PhysicalAddress.QuadPart &
                             ARM64_TEST_PTE_ADDR_MASK;

    return (PVOID)(ARM64_TEST_KSEG0_BASE | PhysicalPage |
                   (PageOffset & (PAGE_SIZE - 1)));
}

static
ULONGLONG
Arm64DescriptorAp(
    _In_ ULONGLONG Entry)
{
    return (Entry >> ARM64_TEST_AP_SHIFT) & 3;
}

static
ULONGLONG
Arm64DescriptorShareability(
    _In_ ULONGLONG Entry)
{
    return (Entry >> ARM64_TEST_SH_SHIFT) & 3;
}

static
BOOLEAN
Arm64DescriptorHasFlag(
    _In_ ULONGLONG Entry,
    _In_ ULONGLONG Flag)
{
    return (Entry & Flag) != 0;
}

static
VOID
TraceDescriptorFields(
    _In_z_ PCSTR Name,
    _In_ ULONGLONG Entry)
{
    dump_trace("[arm64][MmSelfMap] %s raw=0x%I64x valid=%I64u type=%I64u addr=0x%I64x\n",
          Name,
          Entry,
          Entry & 1,
          Entry & ARM64_TEST_PTE_TYPE_MASK,
          Entry & ARM64_TEST_PTE_ADDR_MASK);

    dump_trace("[arm64][MmSelfMap] %s attr=%I64u ns=%I64u ap=%I64u sh=%I64u af=%I64u ng=%I64u cont=%I64u pxn=%I64u uxn=%I64u\n",
          Name,
          (Entry >> 2) & 7,
          (Entry >> 5) & 1,
          (Entry >> 6) & 3,
          (Entry >> 8) & 3,
          (Entry >> 10) & 1,
          (Entry >> 11) & 1,
          (Entry >> 52) & 1,
          (Entry >> 53) & 1,
          (Entry >> 54) & 1);
}

static
VOID
TraceAddressIndices(
    _In_z_ PCSTR Name,
    _In_ PVOID Address)
{
    dump_trace("[arm64][MmSelfMap] va %s=%p l0=%I64u l1=%I64u l2=%I64u l3=%I64u pageOffset=0x%I64x\n",
          Name,
          Address,
          ((ULONGLONG)Address >> ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK,
          ((ULONGLONG)Address >> ARM64_TEST_PPI_SHIFT) & ARM64_TEST_INDEX_MASK,
          ((ULONGLONG)Address >> ARM64_TEST_PDI_SHIFT) & ARM64_TEST_INDEX_MASK,
          ((ULONGLONG)Address >> ARM64_TEST_PTI_SHIFT) & ARM64_TEST_INDEX_MASK,
          (ULONGLONG)Address & (PAGE_SIZE - 1));
}

static
ULONGLONG
Arm64SelfMapPteBase(
    _In_ ULONGLONG SelfMapIndex)
{
    return Arm64SignExtend48(SelfMapIndex << ARM64_TEST_PXI_SHIFT);
}

static
ULONGLONG
Arm64SelfMapPdeBase(
    _In_ ULONGLONG SelfMapIndex)
{
    return Arm64SignExtend48((SelfMapIndex << ARM64_TEST_PXI_SHIFT) |
                             (SelfMapIndex << ARM64_TEST_PPI_SHIFT));
}

static
ULONGLONG
Arm64SelfMapPpeBase(
    _In_ ULONGLONG SelfMapIndex)
{
    return Arm64SignExtend48((SelfMapIndex << ARM64_TEST_PXI_SHIFT) |
                             (SelfMapIndex << ARM64_TEST_PPI_SHIFT) |
                             (SelfMapIndex << ARM64_TEST_PDI_SHIFT));
}

static
ULONGLONG
Arm64SelfMapPxeBase(
    _In_ ULONGLONG SelfMapIndex)
{
    return Arm64SignExtend48((SelfMapIndex << ARM64_TEST_PXI_SHIFT) |
                             (SelfMapIndex << ARM64_TEST_PPI_SHIFT) |
                             (SelfMapIndex << ARM64_TEST_PDI_SHIFT) |
                             (SelfMapIndex << ARM64_TEST_PTI_SHIFT));
}

static
ULONGLONG
Arm64SelfMapAddressToPxe(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address)
{
    ULONGLONG Offset = ((ULONGLONG)Address >> (ARM64_TEST_PXI_SHIFT - 3)) &
                       (ARM64_TEST_INDEX_MASK << 3);

    return Arm64SelfMapPxeBase(SelfMapIndex) + Offset;
}

static
ULONGLONG
Arm64SelfMapAddressToPpe(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address)
{
    ULONGLONG Offset = ((ULONGLONG)Address >> (ARM64_TEST_PPI_SHIFT - 3)) &
                       (0x3FFFFULL << 3);

    return Arm64SelfMapPpeBase(SelfMapIndex) + Offset;
}

static
ULONGLONG
Arm64SelfMapAddressToPde(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address)
{
    ULONGLONG Offset = ((ULONGLONG)Address >> (ARM64_TEST_PDI_SHIFT - 3)) &
                       (0x7FFFFFFULL << 3);

    return Arm64SelfMapPdeBase(SelfMapIndex) + Offset;
}

static
ULONGLONG
Arm64SelfMapAddressToPte(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address)
{
    ULONGLONG Offset = ((ULONGLONG)Address >> (ARM64_TEST_PTI_SHIFT - 3)) &
                       (0xFFFFFFFFFULL << 3);

    return Arm64SelfMapPteBase(SelfMapIndex) + Offset;
}

static
ULONGLONG
Arm64SelfMapAddressForLevel(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_ ULONG Level)
{
    switch (Level)
    {
        case ARM64_TEST_LEVEL_PXE:
            return Arm64SelfMapAddressToPxe(SelfMapIndex, Address);

        case ARM64_TEST_LEVEL_PPE:
            return Arm64SelfMapAddressToPpe(SelfMapIndex, Address);

        case ARM64_TEST_LEVEL_PDE:
            return Arm64SelfMapAddressToPde(SelfMapIndex, Address);

        default:
            return Arm64SelfMapAddressToPte(SelfMapIndex, Address);
    }
}

static
NTSTATUS
ProbeSelfMapLevel(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_ ULONG Level,
    _Out_ PULONGLONG EntryAddress,
    _Out_ PULONGLONG Entry)
{
    *EntryAddress = Arm64SelfMapAddressForLevel(SelfMapIndex, Address, Level);
    return ProbeUlong64((volatile ULONGLONG *)*EntryAddress, Entry);
}

static
VOID
TestSelfMapDiscovery(
    _In_ ULONGLONG SelfMapIndex)
{
    ok(SelfMapIndex != ARM64_TEST_NO_SELFMAP_INDEX,
       "No TTBR1-root recursive self-map slot was discovered\n");
    if (SelfMapIndex == ARM64_TEST_NO_SELFMAP_INDEX)
        return;

    ok(SelfMapIndex >= 256 && SelfMapIndex <= ARM64_TEST_INDEX_MASK,
       "Self-map index %I64u is outside the kernel half\n",
       SelfMapIndex);
}

static
VOID
TestSelectedSelfMapRanges(
    _In_ ULONGLONG SelfMapIndex)
{
    ULONGLONG PteBase = Arm64SelfMapPteBase(SelfMapIndex);
    ULONGLONG PdeBase = Arm64SelfMapPdeBase(SelfMapIndex);
    ULONGLONG PpeBase = Arm64SelfMapPpeBase(SelfMapIndex);
    ULONGLONG PxeBase = Arm64SelfMapPxeBase(SelfMapIndex);
    ULONGLONG SelfEntry = PxeBase + SelfMapIndex * sizeof(ULONGLONG);

    ok_eq_ulonglong(((PteBase >> ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PdeBase >> ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PdeBase >> ARM64_TEST_PPI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PpeBase >> ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PpeBase >> ARM64_TEST_PPI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PpeBase >> ARM64_TEST_PDI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PxeBase >> ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PxeBase >> ARM64_TEST_PPI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PxeBase >> ARM64_TEST_PDI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);
    ok_eq_ulonglong(((PxeBase >> ARM64_TEST_PTI_SHIFT) & ARM64_TEST_INDEX_MASK),
                    SelfMapIndex);

    ok(SelfEntry >= PxeBase && SelfEntry < PxeBase + PAGE_SIZE,
       "Self-map entry %p is outside PXE page %p-%p\n",
       (PVOID)SelfEntry,
       (PVOID)PxeBase,
       (PVOID)(PxeBase + PAGE_SIZE - 1));
}

static
VOID
TestSelectedSelfMapRoot(
    _In_ ULONGLONG SelfMapIndex)
{
    ULONGLONG Ttbr1;
    ULONGLONG SelfMapEntryAddress;
    ULONGLONG SelfMapEntry;
    ULONGLONG ExpectedPfn;
    ULONGLONG SelfMapPfn;
    NTSTATUS Status;

    Ttbr1 = Arm64ReadTtbr1();
    SelfMapEntryAddress = Arm64SelfMapPxeBase(SelfMapIndex) +
                          SelfMapIndex * sizeof(ULONGLONG);

    Status = ProbeUlong64((volatile ULONGLONG *)SelfMapEntryAddress,
                          &SelfMapEntry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok(Arm64IsTableDescriptor(SelfMapEntry),
       "Selected self-map PXE is not a table descriptor: 0x%I64x\n",
       SelfMapEntry);

    ExpectedPfn = (Ttbr1 & ARM64_TEST_PTE_ADDR_MASK) >> PAGE_SHIFT;
    SelfMapPfn = Arm64PfnFromEntry(SelfMapEntry);
    ok_eq_ulonglong(SelfMapPfn, ExpectedPfn);
}

static
BOOLEAN
TestDynamicSelfMapEntry(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name,
    _In_z_ PCSTR LevelName,
    _In_ ULONGLONG EntryAddress,
    _In_ BOOLEAN Leaf,
    _Out_ PULONGLONG EntryValue)
{
    NTSTATUS Status;
    ULONGLONG Entry = 0;

    Status = ProbeUlong64((volatile ULONGLONG *)EntryAddress, &Entry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;

    dump_trace("[arm64][MmSelfMap] check %s %s index=%I64u va=%p entryAddr=%p entry=0x%I64x pfn=0x%I64x\n",
          Name,
          LevelName,
          SelfMapIndex,
          Address,
          (PVOID)EntryAddress,
          Entry,
          Arm64PfnFromEntry(Entry));

    if (Leaf)
    {
        ok(Arm64IsPageDescriptor(Entry),
           "%s %s is not a page descriptor: 0x%I64x\n",
           Name,
           LevelName,
           Entry);
    }
    else
    {
        ok(Arm64IsTableDescriptor(Entry),
           "%s %s is not a table descriptor: 0x%I64x\n",
           Name,
           LevelName,
           Entry);
    }

    *EntryValue = Entry;
    return TRUE;
}

static
VOID
TestDynamicSelfMapMappedAddress(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name,
    _In_ BOOLEAN CheckPhysicalAddress)
{
    ULONGLONG PxeAddress = Arm64SelfMapAddressToPxe(SelfMapIndex, Address);
    ULONGLONG PpeAddress = Arm64SelfMapAddressToPpe(SelfMapIndex, Address);
    ULONGLONG PdeAddress = Arm64SelfMapAddressToPde(SelfMapIndex, Address);
    ULONGLONG PteAddress = Arm64SelfMapAddressToPte(SelfMapIndex, Address);
    ULONGLONG PxeBase = Arm64SelfMapPxeBase(SelfMapIndex);
    ULONGLONG PpeBase = Arm64SelfMapPpeBase(SelfMapIndex);
    ULONGLONG PdeBase = Arm64SelfMapPdeBase(SelfMapIndex);
    ULONGLONG PteBase = Arm64SelfMapPteBase(SelfMapIndex);
    ULONGLONG Entry;
    ULONGLONG PteEntry = 0;
    BOOLEAN HavePte;

    ok(PxeAddress >= PxeBase && PxeAddress < PxeBase + PAGE_SIZE,
       "%s PXE address %p is outside selected PXE page\n",
       Name,
       (PVOID)PxeAddress);
    ok(PpeAddress >= PpeBase && PpeAddress < PpeBase + (1ULL << ARM64_TEST_PDI_SHIFT),
       "%s PPE address %p is outside selected PPE range\n",
       Name,
       (PVOID)PpeAddress);
    ok(PdeAddress >= PdeBase && PdeAddress < PdeBase + (1ULL << ARM64_TEST_PPI_SHIFT),
       "%s PDE address %p is outside selected PDE range\n",
       Name,
       (PVOID)PdeAddress);
    ok(PteAddress >= PteBase && PteAddress < PteBase + (1ULL << ARM64_TEST_PXI_SHIFT),
       "%s PTE address %p is outside selected PTE range\n",
       Name,
       (PVOID)PteAddress);

    TestDynamicSelfMapEntry(SelfMapIndex,
                            Address,
                            Name,
                            "PXE",
                            PxeAddress,
                            FALSE,
                            &Entry);
    TestDynamicSelfMapEntry(SelfMapIndex,
                            Address,
                            Name,
                            "PPE",
                            PpeAddress,
                            FALSE,
                            &Entry);
    TestDynamicSelfMapEntry(SelfMapIndex,
                            Address,
                            Name,
                            "PDE",
                            PdeAddress,
                            FALSE,
                            &Entry);
    HavePte = TestDynamicSelfMapEntry(SelfMapIndex,
                                      Address,
                                      Name,
                                      "PTE",
                                      PteAddress,
                                      TRUE,
                                      &PteEntry);

    if (HavePte && CheckPhysicalAddress)
    {
        PHYSICAL_ADDRESS PhysicalAddress = MmGetPhysicalAddress(Address);
        ULONGLONG ExpectedPfn = (ULONGLONG)PhysicalAddress.QuadPart >> PAGE_SHIFT;
        ULONGLONG PtePfn = Arm64PfnFromEntry(PteEntry);

        ok_eq_ulonglong(PtePfn, ExpectedPfn);
    }
}

static
BOOLEAN
ReadDynamicPte(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name,
    _Out_ PULONGLONG PteEntry)
{
    ULONGLONG EntryAddress;
    NTSTATUS Status;

    Status = ProbeSelfMapLevel(SelfMapIndex,
                               Address,
                               ARM64_TEST_LEVEL_PTE,
                               &EntryAddress,
                               PteEntry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return FALSE;

    dump_trace("[arm64][MmSelfMap] leaf %s va=%p pte=%p entry=0x%I64x pfn=0x%I64x\n",
          Name,
          Address,
          (PVOID)EntryAddress,
          *PteEntry,
          Arm64PfnFromEntry(*PteEntry));

    return TRUE;
}

static
VOID
TestWin11TableDescriptorPolicy(
    _In_z_ PCSTR Name,
    _In_ ULONGLONG Entry)
{
    ok(Arm64IsTableDescriptor(Entry),
       "%s table descriptor is not valid: 0x%I64x\n",
       Name,
       Entry);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_VALID),
       "%s table descriptor missing valid bit: 0x%I64x\n",
       Name,
       Entry);
    ok_eq_ulonglong(Arm64DescriptorAp(Entry), ARM64_TEST_AP_KERNEL_RW);
    ok_eq_ulonglong(Arm64DescriptorShareability(Entry), ARM64_TEST_SH_INNER);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_AF),
       "%s table descriptor missing access flag: 0x%I64x\n",
       Name,
       Entry);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_NG),
       "%s table descriptor missing NT nG/table bit: 0x%I64x\n",
       Name,
       Entry);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_PXN),
       "%s table descriptor missing PXNTable: 0x%I64x\n",
       Name,
       Entry);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_UXN),
       "%s table descriptor missing UXNTable: 0x%I64x\n",
       Name,
       Entry);
}

static
VOID
TestWin11LeafDescriptorPolicy(
    _In_z_ PCSTR Name,
    _In_ ULONGLONG Entry)
{
    ok(Arm64IsPageDescriptor(Entry),
       "%s leaf descriptor is not valid: 0x%I64x\n",
       Name,
       Entry);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_VALID),
       "%s leaf descriptor missing valid bit: 0x%I64x\n",
       Name,
       Entry);
    ok_eq_ulonglong(Arm64DescriptorShareability(Entry), ARM64_TEST_SH_INNER);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_AF),
       "%s leaf descriptor missing access flag: 0x%I64x\n",
       Name,
       Entry);
    ok(!Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_NG),
       "%s Win11 ARM64 kernel leaf unexpectedly has nG set: 0x%I64x\n",
       Name,
       Entry);
    ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_UXN),
       "%s Win11 ARM64 kernel leaf missing UXN: 0x%I64x\n",
       Name,
       Entry);
}

static
VOID
TestLeafDescriptorStableAcrossWrite(
    _In_z_ PCSTR Name,
    _In_ ULONGLONG Before,
    _In_ ULONGLONG After)
{
    dump_trace("[arm64][MmSelfMap] write-stable %s before=0x%I64x after=0x%I64x\n",
          Name,
          Before,
          After);

    ok_eq_ulonglong(Arm64PfnFromEntry(After), Arm64PfnFromEntry(Before));
    ok_eq_ulonglong(Arm64DescriptorAp(After), Arm64DescriptorAp(Before));
    ok_eq_ulonglong(Arm64DescriptorShareability(After),
                    Arm64DescriptorShareability(Before));
    ok_eq_ulonglong(!!Arm64DescriptorHasFlag(After, ARM64_TEST_PTE_VALID),
                    !!Arm64DescriptorHasFlag(Before, ARM64_TEST_PTE_VALID));
    ok_eq_ulonglong(!!Arm64DescriptorHasFlag(After, ARM64_TEST_PTE_AF),
                    !!Arm64DescriptorHasFlag(Before, ARM64_TEST_PTE_AF));
    ok_eq_ulonglong(!!Arm64DescriptorHasFlag(After, ARM64_TEST_PTE_PXN),
                    !!Arm64DescriptorHasFlag(Before, ARM64_TEST_PTE_PXN));
    ok_eq_ulonglong(!!Arm64DescriptorHasFlag(After, ARM64_TEST_PTE_UXN),
                    !!Arm64DescriptorHasFlag(Before, ARM64_TEST_PTE_UXN));
}

static
VOID
TestWritableByteLeafMutationContract(
    _In_ ULONGLONG SelfMapIndex,
    _Inout_ volatile UCHAR *Address,
    _In_z_ PCSTR Name)
{
    UCHAR OldValue;
    UCHAR NewValue;
    ULONGLONG Before;
    ULONGLONG After;

    if (!ReadDynamicPte(SelfMapIndex, (PVOID)Address, Name, &Before))
        return;

    TestWin11LeafDescriptorPolicy(Name, Before);
    ok_eq_ulonglong(Arm64DescriptorAp(Before), ARM64_TEST_AP_KERNEL_RW);
    ok(Arm64DescriptorHasFlag(Before, ARM64_TEST_PTE_PXN),
       "%s writable leaf is executable before write: 0x%I64x\n",
       Name,
       Before);

    OldValue = *Address;
    NewValue = (UCHAR)(OldValue ^ 0x5a);
    *Address = NewValue;
    KeMemoryBarrier();
    ok_eq_ulong((ULONG)*Address, (ULONG)NewValue);

    if (ReadDynamicPte(SelfMapIndex, (PVOID)Address, Name, &After))
        TestLeafDescriptorStableAcrossWrite(Name, Before, After);

    *Address = OldValue;
    KeMemoryBarrier();
}

static
VOID
TestPhysicalAddressOffsetContract(
    _In_ PVOID Address,
    _In_z_ PCSTR Name)
{
    PHYSICAL_ADDRESS PhysicalAddress;

    PhysicalAddress = MmGetPhysicalAddress(Address);
    dump_trace("[arm64][MmSelfMap] pa-offset %s va=%p pa=0x%I64x\n",
          Name,
          Address,
          (ULONGLONG)PhysicalAddress.QuadPart);

    ok_eq_hex64((ULONGLONG)PhysicalAddress.QuadPart & (PAGE_SIZE - 1),
                (ULONGLONG)(ULONG_PTR)Address & (PAGE_SIZE - 1));
}

static
VOID
TestMappedPageSpanContract(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PUCHAR Page,
    _In_z_ PCSTR Name)
{
    PUCHAR PageBase = Arm64PageBase(Page);
    PUCHAR PageTail = PageBase + PAGE_SIZE - 1;
    PUCHAR NextPage = PageBase + PAGE_SIZE;
    ULONGLONG FirstPteAddress;
    ULONGLONG TailPteAddress;
    ULONGLONG NextPteAddress;
    ULONGLONG FirstEntry;
    ULONGLONG TailEntry;
    ULONGLONG NextEntry;
    PHYSICAL_ADDRESS NextPhysical;

    FirstPteAddress = Arm64SelfMapAddressToPte(SelfMapIndex, PageBase);
    TailPteAddress = Arm64SelfMapAddressToPte(SelfMapIndex, PageTail);
    NextPteAddress = Arm64SelfMapAddressToPte(SelfMapIndex, NextPage);

    dump_trace("[arm64][MmSelfMap] page-span %s base=%p tail=%p next=%p pte=%p/%p/%p\n",
          Name,
          PageBase,
          PageTail,
          NextPage,
          (PVOID)FirstPteAddress,
          (PVOID)TailPteAddress,
          (PVOID)NextPteAddress);

    ok_eq_hex64(TailPteAddress, FirstPteAddress);
    ok_eq_hex64(NextPteAddress - FirstPteAddress, (ULONGLONG)sizeof(ULONGLONG));

    TestPhysicalAddressOffsetContract(PageBase, Name);
    TestPhysicalAddressOffsetContract(PageTail, Name);
    TestPhysicalAddressOffsetContract(NextPage, Name);

    if (ReadDynamicPte(SelfMapIndex, PageBase, Name, &FirstEntry) &&
        ReadDynamicPte(SelfMapIndex, PageTail, Name, &TailEntry))
    {
        TestWin11LeafDescriptorPolicy(Name, FirstEntry);
        TestWin11LeafDescriptorPolicy(Name, TailEntry);
        ok_eq_ulonglong(Arm64PfnFromEntry(TailEntry),
                        Arm64PfnFromEntry(FirstEntry));
    }

    if (ReadDynamicPte(SelfMapIndex, NextPage, Name, &NextEntry))
    {
        NextPhysical = MmGetPhysicalAddress(NextPage);
        TestWin11LeafDescriptorPolicy(Name, NextEntry);
        ok_eq_ulonglong(Arm64PfnFromEntry(NextEntry),
                        (ULONGLONG)NextPhysical.QuadPart >> PAGE_SHIFT);
    }
}

static
VOID
TestMappedHierarchyDescriptorPolicy(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name)
{
    static const ULONG Levels[] =
    {
        ARM64_TEST_LEVEL_PXE,
        ARM64_TEST_LEVEL_PPE,
        ARM64_TEST_LEVEL_PDE
    };
    static const PCSTR LevelNames[] =
    {
        "PXE",
        "PPE",
        "PDE"
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Levels); i++)
    {
        ULONGLONG EntryAddress;
        ULONGLONG Entry = 0;
        NTSTATUS Status;

        Status = ProbeSelfMapLevel(SelfMapIndex,
                                   Address,
                                   Levels[i],
                                   &EntryAddress,
                                   &Entry);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            continue;

        dump_trace("[arm64][MmSelfMap] table-policy %s %s entryAddr=%p entry=0x%I64x\n",
              Name,
              LevelNames[i],
              (PVOID)EntryAddress,
              Entry);
        TestWin11TableDescriptorPolicy(LevelNames[i], Entry);
    }
}

static
VOID
TestNtArm64TranslationRegisterContract(VOID)
{
    ULONGLONG Tcr = Arm64ReadTcr();
    ULONGLONG Ttbr0 = Arm64ReadTtbr0();
    ULONGLONG Ttbr1 = Arm64ReadTtbr1();

    ok_eq_ulong((ULONG)PAGE_SIZE, 0x1000);
    ok_eq_ulong((ULONG)PAGE_SHIFT, 12);
    ok_eq_ulonglong(64 - (Tcr & 0x3f), 47ULL);
    ok_eq_ulonglong(64 - ((Tcr >> 16) & 0x3f), 47ULL);
    ok_eq_ulonglong(Arm64TcrPhysicalBits(Tcr), 48ULL);
    ok_eq_ulonglong((Tcr >> 14) & 3, 0ULL);
    ok_eq_ulonglong((Tcr >> 30) & 3, 2ULL);
    ok_eq_ulonglong((Tcr >> 7) & 1, 0ULL);
    ok_eq_ulonglong((Tcr >> 23) & 1, 0ULL);
    ok_eq_ulonglong((Tcr >> 22) & 1, 1ULL);
    ok_eq_ulonglong((Tcr >> 37) & 1, 0ULL);
    ok_eq_ulonglong((Tcr >> 38) & 1, 0ULL);
    ok_eq_ulonglong((Tcr >> 39) & 1, 0ULL);
    ok_eq_ulonglong((Tcr >> 40) & 1, 0ULL);

    ok_eq_hex64(Ttbr0 & ARM64_TEST_PTE_ADDR_MASK,
                Ttbr1 & ARM64_TEST_PTE_ADDR_MASK);
    ok_eq_ulonglong(Ttbr0 >> 48, Ttbr1 >> 48);
    ok_eq_hex64((Ttbr1 & ARM64_TEST_PTE_ADDR_MASK) & (PAGE_SIZE - 1), 0ULL);
}

static
VOID
TestNtArm64MemoryAttributeContract(VOID)
{
    ULONGLONG Mair = Arm64ReadMair();
    ULONGLONG Sctlr = Arm64ReadSctlr();

    ok_eq_hex64(Mair, 0x444400ff444400ffULL);
    ok_eq_ulonglong(Sctlr & 1, 1ULL);
    ok_eq_ulonglong((Sctlr >> 1) & 1, 0ULL);
    ok_eq_ulonglong((Sctlr >> 2) & 1, 1ULL);
    ok_eq_ulonglong((Sctlr >> 3) & 1, 1ULL);
    ok_eq_ulonglong((Sctlr >> 4) & 1, 1ULL);
    ok_eq_ulonglong((Sctlr >> 12) & 1, 1ULL);
    ok_eq_ulonglong((Sctlr >> 19) & 1, 0ULL);
    ok_eq_ulonglong((Sctlr >> 25) & 1, 0ULL);
}

static
VOID
TestNtArm64MemoryGlobalContract(VOID)
{
    ok_eq_hex64((ULONGLONG)(ULONG_PTR)MmHighestUserAddress,
                ARM64_TEST_WIN11_HIGHEST_USER);
    ok_eq_hex64((ULONGLONG)(ULONG_PTR)MmSystemRangeStart,
                ARM64_TEST_KSEG0_BASE);
    ok_eq_hex64((ULONGLONG)MmUserProbeAddress,
                ARM64_TEST_WIN11_USER_PROBE);
    ok_eq_hex64((ULONGLONG)(ULONG_PTR)KI_USER_SHARED_DATA,
                ARM64_TEST_WIN11_KUSER_SHARED_DATA);

    ok_eq_ulonglong(((ULONGLONG)(ULONG_PTR)MmHighestUserAddress >>
                     ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK,
                    255ULL);
    ok_eq_ulonglong(((ULONGLONG)(ULONG_PTR)MmSystemRangeStart >>
                     ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK,
                    256ULL);
    ok_eq_ulonglong(((ULONGLONG)(ULONG_PTR)KI_USER_SHARED_DATA >>
                     ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK,
                    495ULL);
}

static
VOID
TestSelfMapCandidateUniqueness(
    _In_ ULONGLONG SelfMapIndex)
{
    ULONGLONG Ttbr1 = Arm64ReadTtbr1();
    ULONGLONG RootPfn = (Ttbr1 & ARM64_TEST_PTE_ADDR_MASK) >> PAGE_SHIFT;
    ULONGLONG MatchingIndex = ARM64_TEST_NO_SELFMAP_INDEX;
    ULONG Matching = 0;
    ULONGLONG Index;

    for (Index = 256; Index <= ARM64_TEST_INDEX_MASK; Index++)
    {
        ULONGLONG EntryAddress = Arm64SelfMapPxeBase(Index) +
                                 Index * sizeof(ULONGLONG);
        ULONGLONG Entry = 0;
        NTSTATUS Status;

        Status = ProbeUlong64((volatile ULONGLONG *)EntryAddress, &Entry);
        if (!NT_SUCCESS(Status))
            continue;

        if (Arm64IsTableDescriptor(Entry) && Arm64PfnFromEntry(Entry) == RootPfn)
        {
            Matching++;
            MatchingIndex = Index;
        }
    }

    ok_eq_ulong(Matching, 1);
    ok_eq_ulonglong(MatchingIndex, SelfMapIndex);
}

static
VOID
TestSelfMapArithmeticContract(
    _In_ ULONGLONG SelfMapIndex)
{
    ULONG_PTR CodePage = (ULONG_PTR)Test_MmSelfMap & ~(PAGE_SIZE - 1);
    ULONG_PTR DataPage = (ULONG_PTR)&KmtDriverObject & ~(PAGE_SIZE - 1);
    ULONGLONG ExpectedPteDelta;

    ok(Arm64SelfMapPteBase(SelfMapIndex) <
       Arm64SelfMapPdeBase(SelfMapIndex),
       "PTE base is not below PDE base\n");
    ok(Arm64SelfMapPdeBase(SelfMapIndex) <
       Arm64SelfMapPpeBase(SelfMapIndex),
       "PDE base is not below PPE base\n");
    ok(Arm64SelfMapPpeBase(SelfMapIndex) <
       Arm64SelfMapPxeBase(SelfMapIndex),
       "PPE base is not below PXE base\n");

    ok_eq_hex64(Arm64SelfMapAddressToPte(SelfMapIndex, NULL),
                Arm64SelfMapPteBase(SelfMapIndex));
    ok_eq_hex64(Arm64SelfMapAddressToPde(SelfMapIndex, NULL),
                Arm64SelfMapPdeBase(SelfMapIndex));
    ok_eq_hex64(Arm64SelfMapAddressToPpe(SelfMapIndex, NULL),
                Arm64SelfMapPpeBase(SelfMapIndex));
    ok_eq_hex64(Arm64SelfMapAddressToPxe(SelfMapIndex, NULL),
                Arm64SelfMapPxeBase(SelfMapIndex));

    ok(DataPage >= CodePage,
       "test data page %p is below code page %p\n",
       (PVOID)DataPage,
       (PVOID)CodePage);
    ExpectedPteDelta = ((ULONGLONG)((DataPage - CodePage) >> PAGE_SHIFT)) *
                       sizeof(ULONGLONG);
    ok_eq_hex64(Arm64SelfMapAddressToPte(SelfMapIndex, (PVOID)DataPage) -
                Arm64SelfMapAddressToPte(SelfMapIndex, (PVOID)CodePage),
                ExpectedPteDelta);
}

static
VOID
TestInvalidAtL0SelfMapAddress(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name)
{
    static const ULONG LowerLevels[] =
    {
        ARM64_TEST_LEVEL_PPE,
        ARM64_TEST_LEVEL_PDE,
        ARM64_TEST_LEVEL_PTE
    };
    ULONGLONG EntryAddress;
    ULONGLONG Entry = 0;
    NTSTATUS Status;
    ULONG i;

    Status = ProbeSelfMapLevel(SelfMapIndex,
                               Address,
                               ARM64_TEST_LEVEL_PXE,
                               &EntryAddress,
                               &Entry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        dump_trace("[arm64][MmSelfMap] invalid-l0 %s pxe=%p entry=0x%I64x\n",
              Name,
              (PVOID)EntryAddress,
              Entry);
        ok_eq_hex64(Entry, 0ULL);
    }

    for (i = 0; i < RTL_NUMBER_OF(LowerLevels); i++)
    {
        Status = ProbeSelfMapLevel(SelfMapIndex,
                                   Address,
                                   LowerLevels[i],
                                   &EntryAddress,
                                   &Entry);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    }
}

static
VOID
TestInvalidAtL1SelfMapAddress(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name)
{
    static const ULONG LowerLevels[] =
    {
        ARM64_TEST_LEVEL_PDE,
        ARM64_TEST_LEVEL_PTE
    };
    ULONGLONG EntryAddress;
    ULONGLONG Entry = 0;
    NTSTATUS Status;
    ULONG i;

    Status = ProbeSelfMapLevel(SelfMapIndex,
                               Address,
                               ARM64_TEST_LEVEL_PXE,
                               &EntryAddress,
                               &Entry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        dump_trace("[arm64][MmSelfMap] invalid-l1 %s PXE=%p entry=0x%I64x\n",
              Name,
              (PVOID)EntryAddress,
              Entry);
        TestWin11TableDescriptorPolicy("PXE", Entry);
    }

    Status = ProbeSelfMapLevel(SelfMapIndex,
                               Address,
                               ARM64_TEST_LEVEL_PPE,
                               &EntryAddress,
                               &Entry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        dump_trace("[arm64][MmSelfMap] invalid-l1 %s PPE=%p entry=0x%I64x\n",
              Name,
              (PVOID)EntryAddress,
              Entry);
        ok_eq_hex64(Entry, 0ULL);
    }

    for (i = 0; i < RTL_NUMBER_OF(LowerLevels); i++)
    {
        Status = ProbeSelfMapLevel(SelfMapIndex,
                                   Address,
                                   LowerLevels[i],
                                   &EntryAddress,
                                   &Entry);
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    }
}

static
VOID
TestInvalidUserBoundaryContract(
    _In_ ULONGLONG SelfMapIndex)
{
    PVOID UserAfterHighest = (PVOID)((ULONG_PTR)MmHighestUserAddress + 1);
    PVOID SystemBeforeStart = (PVOID)((ULONG_PTR)MmSystemRangeStart - PAGE_SIZE);

    TestInvalidAtL1SelfMapAddress(SelfMapIndex, NULL, "NULL");
    TestInvalidAtL1SelfMapAddress(SelfMapIndex,
                                  MmHighestUserAddress,
                                  "MmHighestUserAddress");
    TestInvalidAtL1SelfMapAddress(SelfMapIndex,
                                  UserAfterHighest,
                                  "UserAfterHighest");
    TestInvalidAtL1SelfMapAddress(SelfMapIndex,
                                  (PVOID)(ULONG_PTR)MmUserProbeAddress,
                                  "MmUserProbeAddress");
    TestInvalidAtL1SelfMapAddress(SelfMapIndex,
                                  SystemBeforeStart,
                                  "SystemBeforeStart");
}

static
VOID
TestSystemRangeStartInvalidContract(
    _In_ ULONGLONG SelfMapIndex)
{
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  MmSystemRangeStart,
                                  "MmSystemRangeStart");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)ARM64_TEST_KSEG0_BASE,
                                  "KSEG0");
}

static
VOID
TestLegacyReactOSSelfMapConstantsContract(
    _In_ ULONGLONG SelfMapIndex)
{
    ok(SelfMapIndex != ARM64_TEST_LEGACY_PXE_SELFMAP_INDEX,
       "Win11 ARM64 self-map used the legacy ReactOS fixed slot %I64u\n",
       ARM64_TEST_LEGACY_PXE_SELFMAP_INDEX);
    if (SelfMapIndex == ARM64_TEST_LEGACY_PXE_SELFMAP_INDEX)
        return;

    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)ARM64_TEST_LEGACY_PTE_BASE,
                                  "legacy PTE_BASE");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)ARM64_TEST_LEGACY_PDE_BASE,
                                  "legacy PDE_BASE");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)ARM64_TEST_LEGACY_PPE_BASE,
                                  "legacy PPE_BASE");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)ARM64_TEST_LEGACY_PXE_BASE,
                                  "legacy PXE_BASE");
}

static
VOID
TestKernelLeafProtectionContract(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID StackPage)
{
    ULONGLONG Entry;

    if (ReadDynamicPte(SelfMapIndex, (PVOID)Test_MmSelfMap, "code", &Entry))
    {
        TestWin11LeafDescriptorPolicy("code PTE", Entry);
        ok_eq_ulonglong(Arm64DescriptorAp(Entry), ARM64_TEST_AP_READ_ONLY);
        ok(!Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_PXN),
           "code PTE is PXN: 0x%I64x\n",
           Entry);
    }

    if (ReadDynamicPte(SelfMapIndex, (PVOID)&KmtDriverObject, "data", &Entry))
    {
        TestWin11LeafDescriptorPolicy("data PTE", Entry);
        ok_eq_ulonglong(Arm64DescriptorAp(Entry), ARM64_TEST_AP_KERNEL_RW);
        ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_PXN),
           "data PTE is executable: 0x%I64x\n",
           Entry);
    }

    if (ReadDynamicPte(SelfMapIndex, StackPage, "stack", &Entry))
    {
        TestWin11LeafDescriptorPolicy("stack PTE", Entry);
        ok_eq_ulonglong(Arm64DescriptorAp(Entry), ARM64_TEST_AP_KERNEL_RW);
        ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_PXN),
           "stack PTE is executable: 0x%I64x\n",
           Entry);
    }
}

static
VOID
TestKUserSharedDataContract(
    _In_ ULONGLONG SelfMapIndex)
{
    ULONGLONG Entry;

    TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                    (PVOID)KI_USER_SHARED_DATA,
                                    "KI_USER_SHARED_DATA",
                                    TRUE);
    TestMappedHierarchyDescriptorPolicy(SelfMapIndex,
                                        (PVOID)KI_USER_SHARED_DATA,
                                        "KI_USER_SHARED_DATA");

    if (ReadDynamicPte(SelfMapIndex,
                       (PVOID)KI_USER_SHARED_DATA,
                       "KI_USER_SHARED_DATA",
                       &Entry))
    {
        TestWin11LeafDescriptorPolicy("KI_USER_SHARED_DATA PTE", Entry);
        ok_eq_ulonglong(Arm64DescriptorAp(Entry), ARM64_TEST_AP_READ_ONLY);
        ok(Arm64DescriptorHasFlag(Entry, ARM64_TEST_PTE_PXN),
           "KI_USER_SHARED_DATA PTE is executable: 0x%I64x\n",
           Entry);
    }
}

static
VOID
TestPoolLeafDescriptorContract(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name)
{
    ULONGLONG Entry;

    TestMappedHierarchyDescriptorPolicy(SelfMapIndex, Address, Name);

    if (ReadDynamicPte(SelfMapIndex, Address, Name, &Entry))
    {
        TestWin11LeafDescriptorPolicy(Name, Entry);
        ok_eq_ulonglong(Arm64DescriptorAp(Entry), ARM64_TEST_AP_KERNEL_RW);
    }
}

static
VOID
TestNtKseg0Contract(
    _In_ ULONGLONG SelfMapIndex)
{
    ULONGLONG PxeAddress;
    ULONGLONG PxeEntry = 0;
    NTSTATUS Status;

    ok_eq_ulonglong((ARM64_TEST_KSEG0_BASE >> ARM64_TEST_PXI_SHIFT) &
                    ARM64_TEST_INDEX_MASK,
                    256ULL);

    PxeAddress = Arm64SelfMapAddressToPxe(SelfMapIndex,
                                          (PVOID)ARM64_TEST_KSEG0_BASE);
    Status = ProbeUlong64((volatile ULONGLONG *)PxeAddress, &PxeEntry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    dump_trace("[arm64][MmSelfMap] NT KSEG0 contract base=%p pxeAddr=%p entry=0x%I64x\n",
          (PVOID)ARM64_TEST_KSEG0_BASE,
          (PVOID)PxeAddress,
          PxeEntry);
    TraceDescriptorFields("KSEG0 PXE", PxeEntry);

    ok_eq_hex64(PxeEntry, 0ULL);
}

static
VOID
TestKseg0AliasInvalidForMappedAddress(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID Kseg0Alias;

    PhysicalAddress = MmGetPhysicalAddress(Address);
    Kseg0Alias = Arm64Kseg0AliasForPhysicalAddress(
                     PhysicalAddress,
                     (ULONG_PTR)Address & (PAGE_SIZE - 1));

    dump_trace("[arm64][MmSelfMap] kseg0-alias %s va=%p pa=0x%I64x alias=%p\n",
          Name,
          Address,
          (ULONGLONG)PhysicalAddress.QuadPart,
          Kseg0Alias);
    TraceAddressIndices(Name, Address);
    TraceAddressIndices("kseg0 alias", Kseg0Alias);

    TestInvalidAtL0SelfMapAddress(SelfMapIndex, Kseg0Alias, Name);
}

static
VOID
TestObservedKseg0FaultAddressContract(
    _In_ ULONGLONG SelfMapIndex)
{
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)(ARM64_TEST_OBSERVED_KSEG0_FAULT_LOW - PAGE_SIZE),
                                  "observed KSEG0 fault low previous");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)ARM64_TEST_OBSERVED_KSEG0_FAULT_LOW,
                                  "observed KSEG0 fault low");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)(ARM64_TEST_OBSERVED_KSEG0_FAULT_LOW + PAGE_SIZE),
                                  "observed KSEG0 fault low next");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)(ARM64_TEST_OBSERVED_KSEG0_FAULT_HIGH - PAGE_SIZE),
                                  "observed KSEG0 fault high previous");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)ARM64_TEST_OBSERVED_KSEG0_FAULT_HIGH,
                                  "observed KSEG0 fault high");
    TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                  (PVOID)(ARM64_TEST_OBSERVED_KSEG0_FAULT_HIGH + PAGE_SIZE),
                                  "observed KSEG0 fault high next");
}

static
VOID
TestKseg0WindowInvalidContract(
    _In_ ULONGLONG SelfMapIndex)
{
    static const ULONGLONG Addresses[] =
    {
        ARM64_TEST_KSEG0_BASE,
        ARM64_TEST_KSEG0_BASE + PAGE_SIZE,
        ARM64_TEST_KSEG0_BASE + ARM64_TEST_L2_SPAN - PAGE_SIZE,
        ARM64_TEST_KSEG0_BASE + ARM64_TEST_L2_SPAN,
        ARM64_TEST_KSEG0_BASE + (1ULL << ARM64_TEST_PPI_SHIFT) - PAGE_SIZE,
        ARM64_TEST_KSEG0_BASE + (1ULL << ARM64_TEST_PPI_SHIFT),
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Addresses); i++)
    {
        dump_trace("[arm64][MmSelfMap] kseg0-window address=%p\n",
              (PVOID)Addresses[i]);
        TraceAddressIndices("KSEG0 invalid window", (PVOID)Addresses[i]);
        TestInvalidAtL0SelfMapAddress(SelfMapIndex,
                                      (PVOID)Addresses[i],
                                      "KSEG0 invalid window");
    }
}

static
VOID
TestKseg0AliasInvalidContract(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID StackPage,
    _In_opt_ PVOID PoolPage)
{
    TestNtKseg0Contract(SelfMapIndex);
    TestKseg0WindowInvalidContract(SelfMapIndex);
    TestObservedKseg0FaultAddressContract(SelfMapIndex);
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          Arm64PageBase((PVOID)Test_MmSelfMap),
                                          "code KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          (PVOID)((ULONG_PTR)Test_MmSelfMap + 0x10),
                                          "code offset KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          Arm64PageBase((PVOID)&KmtDriverObject),
                                          "data KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          (PVOID)((ULONG_PTR)&KmtDriverObject + sizeof(PVOID) - 1),
                                          "data offset KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          StackPage,
                                          "stack KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          (PUCHAR)StackPage + PAGE_SIZE - 1,
                                          "stack tail KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          (PVOID)KI_USER_SHARED_DATA,
                                          "KUSER_SHARED_DATA KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          (PVOID)(KI_USER_SHARED_DATA + PAGE_SIZE - sizeof(ULONG)),
                                          "KUSER_SHARED_DATA tail KSEG0 alias");

    if (PoolPage != NULL)
    {
        TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                              PoolPage,
                                              "nonpaged-pool KSEG0 alias");
        TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                              (PUCHAR)PoolPage + PAGE_SIZE,
                                              "nonpaged-pool second KSEG0 alias");
        TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                              (PUCHAR)PoolPage + PAGE_SIZE - 1,
                                              "nonpaged-pool tail KSEG0 alias");
    }
}

static
VOID
TestDynamicPagedPoolUpperLevelAllocation(
    _In_ ULONGLONG SelfMapIndex)
{
    SIZE_T Size = (SIZE_T)(ARM64_TEST_L2_SPAN + 2 * PAGE_SIZE);
    PUCHAR Allocation;
    PUCHAR BoundaryPage;
    PUCHAR LastPage;
    ULONGLONG FirstPde;
    ULONGLONG BoundaryPde;
    ULONGLONG LastPde;
    ULONGLONG BoundaryPte;
    ULONGLONG LastPte;

    Allocation = ExAllocatePoolWithTag(PagedPool, Size, ARM64_TEST_POOL_TAG);
    ok(Allocation != NULL, "PagedPool expansion allocation failed\n");
    if (skip(Allocation != NULL, "No paged-pool expansion allocation\n"))
        return;

    ok(((ULONG_PTR)Allocation & (PAGE_SIZE - 1)) == 0,
       "PagedPool expansion allocation %p is not page-aligned\n",
       Allocation);

    BoundaryPage = Allocation + ARM64_TEST_L2_SPAN;
    LastPage = BoundaryPage + PAGE_SIZE;

    /*
     * Span an L2/PDE boundary with real pages. This is the NT-visible ABI
     * shape ReactOS needs to follow: page-table hierarchy entries are readable
     * through the dynamic recursive slot and leaf PFNs agree with the memory
     * manager's physical translation.
     */
    RtlFillMemory(Allocation, PAGE_SIZE, 0x5a);
    RtlFillMemory(BoundaryPage, PAGE_SIZE, 0xa5);
    RtlFillMemory(LastPage, PAGE_SIZE, 0xc3);

    FirstPde = Arm64SelfMapAddressToPde(SelfMapIndex, Allocation);
    BoundaryPde = Arm64SelfMapAddressToPde(SelfMapIndex, BoundaryPage);
    LastPde = Arm64SelfMapAddressToPde(SelfMapIndex, LastPage);

    ok(FirstPde != BoundaryPde,
       "PagedPool allocation did not cross an L2/PDE boundary: %p -> %p\n",
       Allocation,
       BoundaryPage);
    ok_eq_hex64(BoundaryPde - FirstPde, (ULONGLONG)sizeof(ULONGLONG));
    ok_eq_hex64(LastPde - BoundaryPde, 0ULL);

    BoundaryPte = Arm64SelfMapAddressToPte(SelfMapIndex, BoundaryPage);
    LastPte = Arm64SelfMapAddressToPte(SelfMapIndex, LastPage);
    ok_eq_hex64(LastPte - BoundaryPte, (ULONGLONG)sizeof(ULONGLONG));

    TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                    Allocation,
                                    "paged-pool first",
                                    TRUE);
    TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                    BoundaryPage,
                                    "paged-pool boundary",
                                    TRUE);
    TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                    LastPage,
                                    "paged-pool last",
                                    TRUE);
    TestPoolLeafDescriptorContract(SelfMapIndex,
                                   Allocation,
                                   "paged-pool first");
    TestPoolLeafDescriptorContract(SelfMapIndex,
                                   BoundaryPage,
                                   "paged-pool boundary");
    TestPoolLeafDescriptorContract(SelfMapIndex,
                                   LastPage,
                                   "paged-pool last");
    TestWritableByteLeafMutationContract(SelfMapIndex,
                                         Allocation,
                                         "paged-pool first writable");
    TestWritableByteLeafMutationContract(SelfMapIndex,
                                         BoundaryPage - 1,
                                         "paged-pool pre-boundary writable");
    TestWritableByteLeafMutationContract(SelfMapIndex,
                                         BoundaryPage,
                                         "paged-pool boundary writable");
    TestWritableByteLeafMutationContract(SelfMapIndex,
                                         LastPage,
                                         "paged-pool last writable");
    TestMappedPageSpanContract(SelfMapIndex,
                               Allocation,
                               "paged-pool first span");
    TestMappedPageSpanContract(SelfMapIndex,
                               BoundaryPage - PAGE_SIZE,
                               "paged-pool pre-boundary span");
    TestMappedPageSpanContract(SelfMapIndex,
                               BoundaryPage,
                               "paged-pool boundary span");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          Allocation,
                                          "paged-pool first KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          BoundaryPage - 1,
                                          "paged-pool pre-boundary KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          BoundaryPage,
                                          "paged-pool boundary KSEG0 alias");
    TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                          LastPage,
                                          "paged-pool last KSEG0 alias");

    ExFreePoolWithTag(Allocation, ARM64_TEST_POOL_TAG);
}

static
VOID
TraceProbeEntry(
    _In_z_ PCSTR Name,
    _In_ ULONGLONG Address)
{
    NTSTATUS Status;
    ULONGLONG Entry;

    Status = ProbeUlong64((volatile ULONGLONG *)Address, &Entry);
    if (NT_SUCCESS(Status))
    {
        dump_trace("[arm64][MmSelfMap] %s addr=%p entry=0x%I64x pfn=0x%I64x\n",
              Name,
              (PVOID)Address,
              Entry,
              Arm64PfnFromEntry(Entry));
        TraceDescriptorFields(Name, Entry);
    }
    else
    {
        dump_trace("[arm64][MmSelfMap] %s addr=%p status=0x%08lx\n",
              Name,
              (PVOID)Address,
              Status);
    }
}

static
VOID
TraceSelfMapProbeForAddress(
    _In_ ULONGLONG SelfMapIndex,
    _In_ PVOID Address,
    _In_z_ PCSTR Name)
{
    ULONGLONG Pxe;
    ULONGLONG Ppe;
    ULONGLONG Pde;
    ULONGLONG Pte;

    Pxe = Arm64SelfMapAddressToPxe(SelfMapIndex, Address);
    Ppe = Arm64SelfMapAddressToPpe(SelfMapIndex, Address);
    Pde = Arm64SelfMapAddressToPde(SelfMapIndex, Address);
    Pte = Arm64SelfMapAddressToPte(SelfMapIndex, Address);

    dump_trace("[arm64][MmSelfMap] sample %s va=%p index=%I64u l0=%I64u l1=%I64u l2=%I64u l3=%I64u pxe=%p ppe=%p pde=%p pte=%p\n",
          Name,
          Address,
          SelfMapIndex,
          ((ULONGLONG)Address >> ARM64_TEST_PXI_SHIFT) & ARM64_TEST_INDEX_MASK,
          ((ULONGLONG)Address >> ARM64_TEST_PPI_SHIFT) & ARM64_TEST_INDEX_MASK,
          ((ULONGLONG)Address >> ARM64_TEST_PDI_SHIFT) & ARM64_TEST_INDEX_MASK,
          ((ULONGLONG)Address >> ARM64_TEST_PTI_SHIFT) & ARM64_TEST_INDEX_MASK,
          (PVOID)Pxe,
          (PVOID)Ppe,
          (PVOID)Pde,
          (PVOID)Pte);

    TraceProbeEntry("PXE", Pxe);
    TraceProbeEntry("PPE", Ppe);
    TraceProbeEntry("PDE", Pde);
    TraceProbeEntry("PTE", Pte);
}

static
ULONGLONG
TraceSelfMapCandidates(VOID)
{
    ULONGLONG Ttbr1;
    ULONGLONG RootPfn;
    ULONGLONG BestIndex = ARM64_TEST_NO_SELFMAP_INDEX;
    ULONG Readable = 0;
    ULONG Matching = 0;
    ULONGLONG Index;

    Ttbr1 = Arm64ReadTtbr1();
    RootPfn = (Ttbr1 & ARM64_TEST_PTE_ADDR_MASK) >> PAGE_SHIFT;

    for (Index = 256; Index <= ARM64_TEST_INDEX_MASK; Index++)
    {
        ULONGLONG PxeBase = Arm64SelfMapPxeBase(Index);
        ULONGLONG SelfEntryAddress = PxeBase + Index * sizeof(ULONGLONG);
        ULONGLONG Entry;
        NTSTATUS Status;

        Status = ProbeUlong64((volatile ULONGLONG *)SelfEntryAddress, &Entry);
        if (!NT_SUCCESS(Status))
            continue;

        Readable++;
        if (Arm64IsTableDescriptor(Entry) && Arm64PfnFromEntry(Entry) == RootPfn)
        {
            Matching++;
            if (BestIndex == ARM64_TEST_NO_SELFMAP_INDEX)
                BestIndex = Index;

            dump_trace("[arm64][MmSelfMap] self-map candidate index=%I64u pxeBase=%p pteBase=%p pdeBase=%p ppeBase=%p entry=0x%I64x rootPfn=0x%I64x\n",
                  Index,
                  (PVOID)PxeBase,
                  (PVOID)Arm64SelfMapPteBase(Index),
                  (PVOID)Arm64SelfMapPdeBase(Index),
                  (PVOID)Arm64SelfMapPpeBase(Index),
                  Entry,
                  RootPfn);
        }
    }

    dump_trace("[arm64][MmSelfMap] self-map scan readable=%lu matchingRoot=%lu selected=%I64u\n",
          Readable,
          Matching,
          BestIndex);

    return BestIndex;
}

static
VOID
TraceTranslationState(
    _In_ BOOLEAN ReactOS)
{
    ULONGLONG Ttbr0 = Arm64ReadTtbr0();
    ULONGLONG Ttbr1 = Arm64ReadTtbr1();
    ULONGLONG Tcr = Arm64ReadTcr();
    ULONGLONG Mair = Arm64ReadMair();
    ULONGLONG Sctlr = Arm64ReadSctlr();
    ULONG T0sz = (ULONG)(Tcr & 0x3f);
    ULONG T1sz = (ULONG)((Tcr >> 16) & 0x3f);

    dump_trace("[arm64][MmSelfMap] survey reactos=%u nt=0x%lx pageSize=0x%lx pageShift=%ld ptrSize=%Iu cpu=%lu\n",
          ReactOS,
          GetNTVersion(),
          (ULONG)PAGE_SIZE,
          (LONG)PAGE_SHIFT,
          sizeof(PVOID),
          KeGetCurrentProcessorNumber());

    dump_trace("[arm64][MmSelfMap] ttbr0=0x%I64x base=0x%I64x asid=0x%I64x ttbr1=0x%I64x base=0x%I64x asid=0x%I64x\n",
          Ttbr0,
          Ttbr0 & ARM64_TEST_PTE_ADDR_MASK,
          Ttbr0 >> 48,
          Ttbr1,
          Ttbr1 & ARM64_TEST_PTE_ADDR_MASK,
          Ttbr1 >> 48);

    dump_trace("[arm64][MmSelfMap] tcr=0x%I64x t0sz=%lu va0=%lu tg0=%s t1sz=%lu va1=%lu tg1=%s ips=%lu\n",
          Tcr,
          T0sz,
          64 - T0sz,
          Arm64TcrTg0(Tcr),
          T1sz,
          64 - T1sz,
          Arm64TcrTg1(Tcr),
          Arm64TcrPhysicalBits(Tcr));

    dump_trace("[arm64][MmSelfMap] tcr flags epd0=%I64u epd1=%I64u a1=%I64u as=%I64u tbi0=%I64u tbi1=%I64u ha=%I64u hd=%I64u\n",
          (Tcr >> 7) & 1,
          (Tcr >> 23) & 1,
          (Tcr >> 22) & 1,
          (Tcr >> 36) & 1,
          (Tcr >> 37) & 1,
          (Tcr >> 38) & 1,
          (Tcr >> 39) & 1,
          (Tcr >> 40) & 1);

    dump_trace("[arm64][MmSelfMap] mair=0x%I64x attr0=%02I64x attr1=%02I64x attr2=%02I64x attr3=%02I64x\n",
          Mair,
          Mair & 0xff,
          (Mair >> 8) & 0xff,
          (Mair >> 16) & 0xff,
          (Mair >> 24) & 0xff);
    dump_trace("[arm64][MmSelfMap] mair attr4=%02I64x attr5=%02I64x attr6=%02I64x attr7=%02I64x\n",
          (Mair >> 32) & 0xff,
          (Mair >> 40) & 0xff,
          (Mair >> 48) & 0xff,
          (Mair >> 56) & 0xff);

    dump_trace("[arm64][MmSelfMap] sctlr=0x%I64x m=%I64u a=%I64u c=%I64u sa=%I64u sa0=%I64u i=%I64u wxn=%I64u ee=%I64u\n",
          Sctlr,
          Sctlr & 1,
          (Sctlr >> 1) & 1,
          (Sctlr >> 2) & 1,
          (Sctlr >> 3) & 1,
          (Sctlr >> 4) & 1,
          (Sctlr >> 12) & 1,
          (Sctlr >> 19) & 1,
          (Sctlr >> 25) & 1);
}

static
VOID
TraceMemoryGlobals(VOID)
{
    dump_trace("[arm64][MmSelfMap] mm globals highestUser=%p systemRange=%p userProbe=0x%I64x kuserShared=%p\n",
          MmHighestUserAddress,
          MmSystemRangeStart,
          (ULONGLONG)MmUserProbeAddress,
          (PVOID)KI_USER_SHARED_DATA);

    TraceAddressIndices("MmHighestUserAddress", MmHighestUserAddress);
    TraceAddressIndices("MmSystemRangeStart", MmSystemRangeStart);
    TraceAddressIndices("MmUserProbeAddress", (PVOID)(ULONG_PTR)MmUserProbeAddress);
    TraceAddressIndices("KI_USER_SHARED_DATA", (PVOID)KI_USER_SHARED_DATA);
}

static
VOID
TraceSelfMapLayoutForIndex(
    _In_z_ PCSTR Label,
    _In_ ULONGLONG Index)
{
    ULONGLONG PteBase = Arm64SelfMapPteBase(Index);
    ULONGLONG PdeBase = Arm64SelfMapPdeBase(Index);
    ULONGLONG PpeBase = Arm64SelfMapPpeBase(Index);
    ULONGLONG PxeBase = Arm64SelfMapPxeBase(Index);

    dump_trace("[arm64][MmSelfMap] layout %s index=%I64u pte=%p-%p pde=%p-%p\n",
          Label,
          Index,
          (PVOID)PteBase,
          (PVOID)(PteBase + (1ULL << ARM64_TEST_PXI_SHIFT) - 1),
          (PVOID)PdeBase,
          (PVOID)(PdeBase + (1ULL << ARM64_TEST_PPI_SHIFT) - 1));

    dump_trace("[arm64][MmSelfMap] layout %s ppe=%p-%p pxe=%p-%p selfEntry=%p\n",
          Label,
          (PVOID)PpeBase,
          (PVOID)(PpeBase + (1ULL << ARM64_TEST_PDI_SHIFT) - 1),
          (PVOID)PxeBase,
          (PVOID)(PxeBase + PAGE_SIZE - 1),
          (PVOID)(PxeBase + Index * sizeof(ULONGLONG)));

    TraceAddressIndices("layout PTE_BASE", (PVOID)PteBase);
    TraceAddressIndices("layout PDE_BASE", (PVOID)PdeBase);
    TraceAddressIndices("layout PPE_BASE", (PVOID)PpeBase);
    TraceAddressIndices("layout PXE_BASE", (PVOID)PxeBase);
}

static
VOID
TraceRangeBoundary(
    _In_z_ PCSTR Label,
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Top)
{
    ULONGLONG TopEntry = Top + 1 - sizeof(ULONGLONG);

    dump_trace("[arm64][MmSelfMap] boundary %s before=%p base=%p topEntry=%p after=%p\n",
          Label,
          (PVOID)(Base - PAGE_SIZE),
          (PVOID)Base,
          (PVOID)TopEntry,
          (PVOID)(Top + 1));

    TraceProbeEntry("boundary before", Base - PAGE_SIZE);
    TraceProbeEntry("boundary base", Base);
    TraceProbeEntry("boundary top", TopEntry);
    TraceProbeEntry("boundary after", Top + 1);
}

static
VOID
TraceSelfMapBoundaries(
    _In_ ULONGLONG Index)
{
    ULONGLONG PteBase = Arm64SelfMapPteBase(Index);
    ULONGLONG PdeBase = Arm64SelfMapPdeBase(Index);
    ULONGLONG PpeBase = Arm64SelfMapPpeBase(Index);
    ULONGLONG PxeBase = Arm64SelfMapPxeBase(Index);

    TraceRangeBoundary("PTE", PteBase, PteBase + (1ULL << ARM64_TEST_PXI_SHIFT) - 1);
    TraceRangeBoundary("PDE", PdeBase, PdeBase + (1ULL << ARM64_TEST_PPI_SHIFT) - 1);
    TraceRangeBoundary("PPE", PpeBase, PpeBase + (1ULL << ARM64_TEST_PDI_SHIFT) - 1);
    TraceRangeBoundary("PXE", PxeBase, PxeBase + PAGE_SIZE - 1);
}

static
VOID
TracePoolSurvey(
    _In_ ULONGLONG SelfMapIndex)
{
    PVOID NonPagedPage;
    PUCHAR PagedAllocation;
    SIZE_T PagedSize = (SIZE_T)(ARM64_TEST_L2_SPAN + PAGE_SIZE);

    NonPagedPage = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, ARM64_TEST_POOL_TAG);
    dump_trace("[arm64][MmSelfMap] nonpaged alloc=%p size=0x%Ix\n",
          NonPagedPage,
          (SIZE_T)PAGE_SIZE);
    if (NonPagedPage != NULL)
    {
        PHYSICAL_ADDRESS PhysicalAddress;

        RtlFillMemory(NonPagedPage, PAGE_SIZE, 0x69);
        PhysicalAddress = MmGetPhysicalAddress(NonPagedPage);
        dump_trace("[arm64][MmSelfMap] nonpaged pa=0x%I64x pfn=0x%I64x\n",
              (ULONGLONG)PhysicalAddress.QuadPart,
              (ULONGLONG)PhysicalAddress.QuadPart >> PAGE_SHIFT);

        if (SelfMapIndex != ARM64_TEST_NO_SELFMAP_INDEX)
            TraceSelfMapProbeForAddress(SelfMapIndex, NonPagedPage, "nonpaged");

        ExFreePoolWithTag(NonPagedPage, ARM64_TEST_POOL_TAG);
    }

    PagedAllocation = ExAllocatePoolWithTag(PagedPool, PagedSize, ARM64_TEST_POOL_TAG);
    dump_trace("[arm64][MmSelfMap] paged alloc=%p size=0x%Ix l2Span=0x%I64x\n",
          PagedAllocation,
          PagedSize,
          ARM64_TEST_L2_SPAN);
    if (PagedAllocation != NULL)
    {
        PUCHAR BoundaryPage = PagedAllocation + ARM64_TEST_L2_SPAN;
        PUCHAR LastPage = PagedAllocation + PagedSize - PAGE_SIZE;

        RtlFillMemory(PagedAllocation, PAGE_SIZE, 0x5a);
        RtlFillMemory(BoundaryPage, PAGE_SIZE, 0xa5);
        RtlFillMemory(LastPage, PAGE_SIZE, 0xc3);

        dump_trace("[arm64][MmSelfMap] paged first=%p boundary=%p last=%p rospde first=%p boundary=%p\n",
              PagedAllocation,
              BoundaryPage,
              LastPage,
              (PVOID)Arm64AddressToPde(PagedAllocation),
              (PVOID)Arm64AddressToPde(BoundaryPage));

        if (SelfMapIndex != ARM64_TEST_NO_SELFMAP_INDEX)
        {
            TraceSelfMapProbeForAddress(SelfMapIndex, PagedAllocation, "paged first");
            TraceSelfMapProbeForAddress(SelfMapIndex, BoundaryPage, "paged boundary");
            TraceSelfMapProbeForAddress(SelfMapIndex, LastPage, "paged last");
        }

        ExFreePoolWithTag(PagedAllocation, ARM64_TEST_POOL_TAG);
    }
}

static
ULONGLONG
TraceSelfMapSurvey(
    _In_ BOOLEAN ReactOS)
{
    ULONGLONG SelfMapIndex;
    ULONG_PTR StackSample = (ULONG_PTR)&SelfMapIndex;
    PVOID UserAfterHighest = (PVOID)((ULONG_PTR)MmHighestUserAddress + 1);
    PVOID SystemBeforeStart = (PVOID)((ULONG_PTR)MmSystemRangeStart - PAGE_SIZE);

    TraceTranslationState(ReactOS);
    TraceMemoryGlobals();
    TraceAddressIndices("code", (PVOID)Test_MmSelfMap);
    TraceAddressIndices("data", (PVOID)&KmtDriverObject);
    TraceAddressIndices("stack", (PVOID)StackSample);
    TraceAddressIndices("NULL", NULL);
    TraceAddressIndices("UserAfterHighest", UserAfterHighest);
    TraceAddressIndices("SystemBeforeStart", SystemBeforeStart);
    TraceAddressIndices("ARM64_TEST_KSEG0_BASE", (PVOID)ARM64_TEST_KSEG0_BASE);
    TraceSelfMapLayoutForIndex("compiled-contract", ARM64_TEST_PXE_SELFMAP_INDEX);

    SelfMapIndex = TraceSelfMapCandidates();
    if (SelfMapIndex == ARM64_TEST_NO_SELFMAP_INDEX)
    {
        if (!ReactOS)
        {
            dump_trace("[arm64][MmSelfMap] no TTBR1-root recursive slot found on NT\n");
            TracePoolSurvey(SelfMapIndex);
            return SelfMapIndex;
        }

        dump_trace("[arm64][MmSelfMap] no TTBR1-root recursive slot found; tracing compiled contract %I64u as fallback only\n",
              ARM64_TEST_PXE_SELFMAP_INDEX);
        TraceSelfMapLayoutForIndex("compiled-fallback", ARM64_TEST_PXE_SELFMAP_INDEX);
        TraceProbeEntry("compiled fallback self-entry", ARM64_TEST_PXE_SELFMAP);
        TracePoolSurvey(SelfMapIndex);
        return SelfMapIndex;
    }

    TraceSelfMapLayoutForIndex("selected", SelfMapIndex);
    TraceProbeEntry("selected self-entry",
                    Arm64SelfMapPxeBase(SelfMapIndex) +
                    SelfMapIndex * sizeof(ULONGLONG));
    TraceSelfMapBoundaries(SelfMapIndex);
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)Test_MmSelfMap, "code");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)&KmtDriverObject, "data");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)StackSample, "stack");
    TraceSelfMapProbeForAddress(SelfMapIndex, NULL, "NULL");
    TraceSelfMapProbeForAddress(SelfMapIndex, MmHighestUserAddress, "MmHighestUserAddress");
    TraceSelfMapProbeForAddress(SelfMapIndex, UserAfterHighest, "UserAfterHighest");
    TraceSelfMapProbeForAddress(SelfMapIndex, SystemBeforeStart, "SystemBeforeStart");
    TraceSelfMapProbeForAddress(SelfMapIndex, MmSystemRangeStart, "MmSystemRangeStart");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)(ULONG_PTR)MmUserProbeAddress, "MmUserProbeAddress");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)KI_USER_SHARED_DATA, "KI_USER_SHARED_DATA");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)ARM64_TEST_KSEG0_BASE, "ARM64_TEST_KSEG0_BASE");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)ARM64_TEST_PTE_BASE, "ARM64_TEST_PTE_BASE");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)ARM64_TEST_PDE_BASE, "ARM64_TEST_PDE_BASE");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)ARM64_TEST_PPE_BASE, "ARM64_TEST_PPE_BASE");
    TraceSelfMapProbeForAddress(SelfMapIndex, (PVOID)ARM64_TEST_PXE_BASE, "ARM64_TEST_PXE_BASE");

    TracePoolSurvey(SelfMapIndex);
    return SelfMapIndex;
}

START_TEST(MmSelfMap)
{
    PUCHAR PoolPage;
    PVOID StackPage;
    BOOLEAN ReactOS;
    ULONGLONG SelfMapIndex;

    ReactOS = IsReactOS();
    SelfMapIndex = TraceSelfMapSurvey(ReactOS);
    TestNtArm64TranslationRegisterContract();
    TestNtArm64MemoryAttributeContract();
    TestNtArm64MemoryGlobalContract();
    TestSelfMapDiscovery(SelfMapIndex);
    if (SelfMapIndex == ARM64_TEST_NO_SELFMAP_INDEX)
        return;

    TestSelfMapCandidateUniqueness(SelfMapIndex);
    TestSelfMapArithmeticContract(SelfMapIndex);
    TestInvalidUserBoundaryContract(SelfMapIndex);
    TestSystemRangeStartInvalidContract(SelfMapIndex);
    TestLegacyReactOSSelfMapConstantsContract(SelfMapIndex);

    TestSelectedSelfMapRanges(SelfMapIndex);
    TestSelectedSelfMapRoot(SelfMapIndex);

    TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                    (PVOID)Test_MmSelfMap,
                                    "code",
                                    TRUE);
    TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                    (PVOID)&KmtDriverObject,
                                    "data",
                                    TRUE);

    StackPage = (PVOID)((ULONG_PTR)&SelfMapIndex & ~(PAGE_SIZE - 1));
    TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                    StackPage,
                                    "stack",
                                    TRUE);
    TestMappedHierarchyDescriptorPolicy(SelfMapIndex,
                                        (PVOID)Test_MmSelfMap,
                                        "code");
    TestMappedHierarchyDescriptorPolicy(SelfMapIndex,
                                        (PVOID)&KmtDriverObject,
                                        "data");
    TestMappedHierarchyDescriptorPolicy(SelfMapIndex,
                                        StackPage,
                                        "stack");
    TestKernelLeafProtectionContract(SelfMapIndex, StackPage);
    TestWritableByteLeafMutationContract(SelfMapIndex,
                                         (volatile UCHAR *)&Arm64SelfMapWritableProbe,
                                         "global writable data");
    TestKUserSharedDataContract(SelfMapIndex);

    PoolPage = ExAllocatePoolWithTag(NonPagedPool,
                                     2 * PAGE_SIZE,
                                     ARM64_TEST_POOL_TAG);
    ok(PoolPage != NULL, "ExAllocatePoolWithTag failed\n");
    if (!skip(PoolPage != NULL, "No nonpaged pool pages\n"))
    {
        RtlFillMemory(PoolPage, 2 * PAGE_SIZE, 0x5a);
        ok(((ULONG_PTR)PoolPage & (PAGE_SIZE - 1)) == 0,
           "NonPagedPool allocation %p is not page-aligned\n",
           PoolPage);
        ok_eq_hex64(Arm64SelfMapAddressToPte(SelfMapIndex, PoolPage + PAGE_SIZE) -
                    Arm64SelfMapAddressToPte(SelfMapIndex, PoolPage),
                    (ULONGLONG)sizeof(ULONGLONG));

        TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                        PoolPage,
                                        "nonpaged-pool first",
                                        TRUE);
        TestDynamicSelfMapMappedAddress(SelfMapIndex,
                                        PoolPage + PAGE_SIZE,
                                        "nonpaged-pool second",
                                        TRUE);
        TestPoolLeafDescriptorContract(SelfMapIndex,
                                       PoolPage,
                                       "nonpaged-pool first");
        TestPoolLeafDescriptorContract(SelfMapIndex,
                                       PoolPage + PAGE_SIZE,
                                       "nonpaged-pool second");
        TestWritableByteLeafMutationContract(SelfMapIndex,
                                             PoolPage,
                                             "nonpaged-pool first writable");
        TestWritableByteLeafMutationContract(SelfMapIndex,
                                             PoolPage + PAGE_SIZE - 1,
                                             "nonpaged-pool first tail writable");
        TestWritableByteLeafMutationContract(SelfMapIndex,
                                             PoolPage + PAGE_SIZE,
                                             "nonpaged-pool second writable");
        TestMappedPageSpanContract(SelfMapIndex,
                                   PoolPage,
                                   "nonpaged-pool first span");
        TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                              PoolPage,
                                              "nonpaged-pool first KSEG0 alias");
        TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                              PoolPage + PAGE_SIZE - 1,
                                              "nonpaged-pool first tail KSEG0 alias");
        TestKseg0AliasInvalidForMappedAddress(SelfMapIndex,
                                              PoolPage + PAGE_SIZE,
                                              "nonpaged-pool second KSEG0 alias");
        ExFreePoolWithTag(PoolPage, ARM64_TEST_POOL_TAG);
    }

    TestDynamicPagedPoolUpperLevelAllocation(SelfMapIndex);
    TestKseg0AliasInvalidContract(SelfMapIndex, StackPage, NULL);
}

#else

START_TEST(MmSelfMap)
{
    skip(FALSE, "ARM64-only test\n");
}

#endif
