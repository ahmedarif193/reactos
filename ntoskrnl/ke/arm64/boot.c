/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/boot.c
 * PURPOSE:         Kernel entry stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <ntstrsafe.h>
#include <reactos/drivers/acpi/acpi.h>
#define NDEBUG
#include <debug.h>
#include <arm64pl011.h>

typedef struct _ARM64_EARLY_GPRS
{
    UINT64 X0;
    UINT64 X1;
    UINT64 X2;
    UINT64 X3;
    UINT64 Sp;
} ARM64_EARLY_GPRS, *PARM64_EARLY_GPRS;

DECLSPEC_NORETURN VOID NTAPI
KiInitializeSystem(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
KiArm64EarlyVectorHandler(_In_ UINT64 VectorId,
                          _In_ UINT64 ExceptionSyndrome,
                          _In_ UINT64 FaultAddress,
                          _In_opt_ PARM64_EARLY_GPRS Registers);

#define ARM64_SYSTEM_RANGE_BASE     0xFFFF800000000000ULL
#define ARM64_KERNEL_IMAGE_BASE     0xFFFFF80000000000ULL
#define ARM64_PHYS_MAP_BASE         0xFFFFFC0000000000ULL
#define ARM64_KSEG0_BASE            ARM64_KERNEL_IMAGE_BASE
/* Memory attribute indices in MAIR_EL1 */
#define ARM64_MEM_ATTR_DEVICE_nGnRnE 0x1ULL
#define ARM64_MEM_ATTR_NORMAL_NC     0x2ULL
#define ARM64_MEM_ATTR_NORMAL_WC     0x3ULL
#define ARM64_MEM_ATTR_NORMAL_WB     0x4ULL
#define ARM64_NT_MAIR_VALUE          0x444400FF444400FFULL
#define ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT 2
#define ARM64_PTE_BLOCK_INNER_SHARE (3ULL << 8)
#define ARM64_PTE_BLOCK_AF          (1ULL << 10)
#define ARM64_PTE_BLOCK_PXN         (1ULL << 53)
#define ARM64_PTE_BLOCK_UXN         (1ULL << 54)
#define ARM64_PTE_TABLE_ATTRS       (ARM64_PTE_TYPE_TABLE | \
                                      (ARM64_MEM_ATTR_NORMAL_WB << ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT) | \
                                      ARM64_PTE_BLOCK_INNER_SHARE | \
                                      ARM64_PTE_BLOCK_AF)
#define ARM64_IDENTITY_DEFAULT_ATTRS (ARM64_PTE_TYPE_BLOCK | \
                                      (ARM64_MEM_ATTR_NORMAL_WB << ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT) | \
                                      ARM64_PTE_BLOCK_INNER_SHARE | \
                                      ARM64_PTE_BLOCK_AF | \
                                      ARM64_PTE_BLOCK_UXN | \
                                      ARM64_PTE_BLOCK_PXN)
#define ARM64_IDENTITY_MIN_BYTES    (512ULL << 20) /* 512 MB */
#define ARM64_L1_BLOCK_SHIFT        30
#define ARM64_L2_BLOCK_SHIFT        21
#define ARM64_L2_BLOCK_SIZE         (1ULL << ARM64_L2_BLOCK_SHIFT)
#define ARM64_L1_MAX_ENTRIES        512

#define ARM64_IDENTITY_L0_ENTRIES   512
#define ARM64_IDENTITY_L1_ENTRIES   512
#define ARM64_IDENTITY_L2_ENTRIES   (512 * 512)
C_ASSERT(ARM64_PTE_TABLE_ATTRS == 0x713ULL);

/* Backing storage for identity tables; pointers are aligned at runtime. */
static UINT64 KiArm64IdentityL0Backing[ARM64_IDENTITY_L0_ENTRIES +
                                       (PAGE_SIZE / sizeof(UINT64))];
static UINT64 KiArm64IdentityL1Backing[ARM64_IDENTITY_L1_ENTRIES +
                                       (PAGE_SIZE / sizeof(UINT64))];
static UINT64 KiArm64IdentityL2Backing[ARM64_IDENTITY_L2_ENTRIES +
                                       (PAGE_SIZE / sizeof(UINT64))];

extern const UINT64 KiArm64EarlyVectorTable[];

static UINT64 *KiArm64IdentityL0;
static UINT64 *KiArm64IdentityL1;
static UINT64 (*KiArm64IdentityL2)[512];
static BOOLEAN KiArm64IdentityMapActive;

typedef struct _ARM64_BOOT_CONTEXT
{
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    BOOLEAN MmuEnabled;
    UINT64 SctlrEl1;
    UINT64 TcrEl1;
    UINT64 Ttbr0El1;
    UINT64 Ttbr1El1;
    UINT64 MairEl1;
} ARM64_BOOT_CONTEXT, *PARM64_BOOT_CONTEXT;

/*
 * Boot stack (CPU0): Win11 ARM64 advertises a 0x80000 stack reserve
 * for the loader-provided initial kernel stack. Keep this bootstrap
 * stack separate from the normal per-thread kernel stack size.
 */
#define ARM64_P0_BOOT_STACK_SIZE 0x80000
#define ARM64_P0_BOOT_STACK_ENTRY_RESERVE PAGE_SIZE

UCHAR DECLSPEC_ALIGN(PAGE_SIZE) KiArm64P0BootStackData[ARM64_P0_BOOT_STACK_SIZE] = {0};
PVOID KiArm64P0BootStackLimit = &KiArm64P0BootStackData[0];
PVOID KiArm64P0BootStack = &KiArm64P0BootStackData[ARM64_P0_BOOT_STACK_SIZE];

/* Assembly helper that switches SP then branches into the C wrapper */
DECLSPEC_NORETURN VOID KiArm64SwitchToBootStack(ULONG_PTR InitialStack,
                                                PLOADER_PARAMETER_BLOCK LoaderBlock);
CODE_SEG("INIT") DECLSPEC_NORETURN VOID NTAPI KiArm64SystemStartupBootStack(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
static UINT64
KiArm64VirtualToPhysical(_In_ UINT64 Virtual);

CODE_SEG("INIT")
static UINT64
KiArm64AlignUp(_In_ UINT64 Value,
               _In_ UINT64 Alignment);

CODE_SEG("INIT")
static BOOLEAN
KiArm64IsMappableMemoryType(_In_ TYPE_OF_MEMORY MemoryType);

CODE_SEG("INIT")
static VOID
KiArm64EnsureL1Entry(_In_ UINT64 Index);

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityRange(_In_ UINT64 PhysicalStart,
                        _In_ UINT64 PhysicalEnd,
                        _In_ UINT64 Attributes);

CODE_SEG("INIT")
static DECLSPEC_NORETURN VOID
KiArm64FatalHalt(VOID);

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityAcpiRange(_In_ UINT64 PhysicalStart,
                            _In_ ULONG Length)
{
    UINT64 Start;
    UINT64 End;

    if (Length == 0)
        Length = sizeof(DESCRIPTION_HEADER);

    Start = PhysicalStart & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
    End = (PhysicalStart + (UINT64)Length + ARM64_L2_BLOCK_SIZE - 1ULL) &
          ~(ARM64_L2_BLOCK_SIZE - 1ULL);
    if (End <= Start)
        End = Start + ARM64_L2_BLOCK_SIZE;

    KiArm64MapIdentityRange(Start, End, ARM64_IDENTITY_DEFAULT_ATTRS);
    if (KiArm64IdentityMapActive)
    {
        __asm__ __volatile__("dsb ishst" ::: "memory");
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("isb");
    }
}

CODE_SEG("INIT")
static BOOLEAN
KiArm64IsMappableMemoryType(_In_ TYPE_OF_MEMORY MemoryType)
{
    switch (MemoryType)
    {
        case LoaderFree:
        case LoaderLoadedProgram:
        case LoaderFirmwareTemporary:
        case LoaderFirmwarePermanent:
        case LoaderOsloaderHeap:
        case LoaderOsloaderStack:
        case LoaderSystemCode:
        case LoaderHalCode:
        case LoaderBootDriver:
        case LoaderConsoleInDriver:
        case LoaderConsoleOutDriver:
        case LoaderStartupDpcStack:
        case LoaderStartupKernelStack:
        case LoaderStartupPanicStack:
        case LoaderStartupPcrPage:
        case LoaderStartupPdrPage:
        case LoaderRegistryData:
        case LoaderMemoryData:
        case LoaderNlsData:
        case LoaderSpecialMemory:
        case LoaderBBTMemory:
        case LoaderXIPRom:
        case LoaderHALCachedMemory:
        case LoaderLargePageFiller:
        case LoaderErrorLogMemory:
            return TRUE;
        default:
            return FALSE;
    }
}

CODE_SEG("INIT")
static UINT64
KiArm64ReadUnalignedU64(_In_ const VOID *Source)
{
    UINT64 Value;
    RtlCopyMemory(&Value, Source, sizeof(Value));
    return Value;
}

CODE_SEG("INIT")
static BOOLEAN
KiArm64FindAcpiNode(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                    _Outptr_result_maybenull_ PACPI_BIOS_MULTI_NODE *AcpiNode)
{
    PCONFIGURATION_COMPONENT_DATA ComponentEntry;
    PCONFIGURATION_COMPONENT_DATA Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PACPI_BIOS_MULTI_NODE NodeData;

    *AcpiNode = NULL;

    if (!KiArm64IdentityMapActive)
    {
        return FALSE;
    }

    if (!LoaderBlock || !LoaderBlock->ConfigurationRoot)
    {
        return FALSE;
    }

    ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                  AdapterClass,
                                                  MultiFunctionAdapter,
                                                  0,
                                                  &Next);
    while (ComponentEntry)
    {
        if (ComponentEntry->ComponentEntry.Identifier &&
            !_stricmp(ComponentEntry->ComponentEntry.Identifier, "ACPI BIOS"))
        {
            break;
        }

        Next = ComponentEntry;
        ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                      AdapterClass,
                                                      MultiFunctionAdapter,
                                                      NULL,
                                                      &Next);
    }

    if (!ComponentEntry || !ComponentEntry->ConfigurationData)
    {
        return FALSE;
    }

    ResourceList = ComponentEntry->ConfigurationData;
    NodeData = (PACPI_BIOS_MULTI_NODE)(ResourceList + 1);
    if (!NodeData)
    {
        return FALSE;
    }

    *AcpiNode = NodeData;
    return TRUE;
}

CODE_SEG("INIT")
static PRSDP
KiArm64FindRsdp(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_BIOS_MULTI_NODE NodeData;

    if (!KiArm64FindAcpiNode(LoaderBlock, &NodeData))
        return NULL;

    if (!NodeData->RsdpAddress.QuadPart)
    {
        return NULL;
    }

    KiArm64MapIdentityAcpiRange(NodeData->RsdpAddress.QuadPart, sizeof(RSDP));
    PRSDP Rsdp = (PRSDP)(ULONG_PTR)NodeData->RsdpAddress.QuadPart;
    if (!Rsdp || (Rsdp->Signature != RSDP_SIGNATURE))
        return NULL;

    return Rsdp;
}

/*
 * KiArm64MapAllAcpiTables
 *
 * Enumerates all ACPI tables referenced by the XSDT/RSDT and ensures each
 * table is fully identity-mapped. This is critical for the HAL's ACPI
 * initialization which uses HalpMapPhysicalMemory64 in identity-mapping
 * mode before Mm is available.
 *
 * Without this, tables like FACP (FADT) would only have their headers
 * mapped during early ACPI table probing, causing page faults when the
 * HAL tries to read full table contents.
 */
CODE_SEG("INIT")
static VOID
KiArm64MapAllAcpiTables(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRSDP Rsdp;
    PACPI_BIOS_MULTI_NODE AcpiNode = NULL;
    UINT64 RootPa = 0;
    PDESCRIPTION_HEADER Root;
    ULONG Count;
    ULONG Index;

    if (!KiArm64IdentityMapActive)
    {
        return;
    }

    Rsdp = KiArm64FindRsdp(LoaderBlock);
    if (Rsdp)
    {
        if ((Rsdp->Revision >= 2) && (Rsdp->XsdtAddress.QuadPart != 0))
            RootPa = Rsdp->XsdtAddress.QuadPart;
        else
            RootPa = (UINT64)Rsdp->RsdtAddress;
    }

    if (!RootPa &&
        KiArm64FindAcpiNode(LoaderBlock, &AcpiNode) &&
        AcpiNode)
    {
        RootPa = AcpiNode->RsdtAddress.QuadPart;
    }

    if (!RootPa)
    {
        return;
    }

    KiArm64MapIdentityAcpiRange(RootPa, sizeof(DESCRIPTION_HEADER));
    Root = (PDESCRIPTION_HEADER)(ULONG_PTR)RootPa;

    if ((Root->Signature != XSDT_SIGNATURE) &&
        (Root->Signature != RSDT_SIGNATURE))
    {
        return;
    }

    if (Root->Signature == XSDT_SIGNATURE)
    {
        UINT64 XsdtPa = RootPa;
        PXSDT Xsdt;
        ULONG Length;

        /* Map XSDT header first */
        KiArm64MapIdentityAcpiRange(XsdtPa, sizeof(DESCRIPTION_HEADER));
        Xsdt = (PXSDT)(ULONG_PTR)XsdtPa;
        Length = Xsdt->Header.Length;

        if ((Xsdt->Header.Signature != XSDT_SIGNATURE) ||
            (Length < sizeof(DESCRIPTION_HEADER)))
        {
            return;
        }

        /* Map full XSDT */
        KiArm64MapIdentityAcpiRange(XsdtPa, Length);

        /* Enumerate and map all tables */
        Count = (Length - sizeof(DESCRIPTION_HEADER)) / sizeof(PHYSICAL_ADDRESS);

        for (Index = 0; Index < Count; ++Index)
        {
            ULONGLONG TablePa = KiArm64ReadUnalignedU64(&Xsdt->Tables[Index]);
            PDESCRIPTION_HEADER Header;

            if (TablePa == 0)
                continue;

            /* Map table header to read its length */
            KiArm64MapIdentityAcpiRange(TablePa, sizeof(DESCRIPTION_HEADER));
            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)TablePa;

            /* Now map the entire table */
            if (Header->Length > sizeof(DESCRIPTION_HEADER))
            {
                KiArm64MapIdentityAcpiRange(TablePa, Header->Length);
            }
        }
    }
    else
    {
        UINT64 RsdtPa = RootPa;
        PRSDT Rsdt;
        ULONG Length;

        /* Map RSDT header first */
        KiArm64MapIdentityAcpiRange(RsdtPa, sizeof(DESCRIPTION_HEADER));
        Rsdt = (PRSDT)(ULONG_PTR)RsdtPa;
        Length = Rsdt->Header.Length;

        if ((Rsdt->Header.Signature != RSDT_SIGNATURE) ||
            (Length < sizeof(DESCRIPTION_HEADER)))
        {
            return;
        }

        /* Map full RSDT */
        KiArm64MapIdentityAcpiRange(RsdtPa, Length);

        /* Enumerate and map all tables */
        Count = (Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONG);
        for (Index = 0; Index < Count; ++Index)
        {
            ULONG TablePa = Rsdt->Tables[Index];
            PDESCRIPTION_HEADER Header;

            if (TablePa == 0)
                continue;

            /* Map table header to read its length */
            KiArm64MapIdentityAcpiRange(TablePa, sizeof(DESCRIPTION_HEADER));
            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)TablePa;

            /* Now map the entire table */
            if (Header->Length > sizeof(DESCRIPTION_HEADER))
            {
                KiArm64MapIdentityAcpiRange(TablePa, Header->Length);
            }
        }
    }
}

CODE_SEG("INIT")
static BOOLEAN
KiArm64IsRangeMappedByDescriptors(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                                  _In_ UINT64 Start,
                                  _In_ UINT64 End);

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityDeviceBlock(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                              _In_ UINT64 Base,
                              _In_ UINT64 Attributes)
{
    UINT64 Start;
    BOOLEAN DescriptorMapped;

    if (Base == 0)
        return;

    Start = Base & ~(ARM64_L2_BLOCK_SIZE - 1ULL);

    DescriptorMapped = KiArm64IsRangeMappedByDescriptors(LoaderBlock,
                                                         Start,
                                                         Start + ARM64_L2_BLOCK_SIZE);
    if (DescriptorMapped)
    {
        return;
    }

    KiArm64MapIdentityRange(Start, Start + ARM64_L2_BLOCK_SIZE, Attributes);
}

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityDeviceRange(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                              _In_ UINT64 Base,
                              _In_ UINT64 Length,
                              _In_ UINT64 Attributes)
{
    UINT64 Start;
    UINT64 End;

    if (Base == 0 || Length == 0)
        return;

    Start = Base & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
    End = (Base + Length + ARM64_L2_BLOCK_SIZE - 1ULL) & ~(ARM64_L2_BLOCK_SIZE - 1ULL);

    for (UINT64 Cursor = Start; Cursor < End; Cursor += ARM64_L2_BLOCK_SIZE)
    {
        KiArm64MapIdentityDeviceBlock(LoaderBlock, Cursor, Attributes);
    }
}

CODE_SEG("INIT")
static VOID
KiArm64MapEarlyDeviceRanges(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                            _In_ UINT64 Attributes)
{
    ULONG Count;

    if (!LoaderBlock)
    {
        return;
    }

    Count = LoaderBlock->u.Arm64.EarlyDeviceRangeCount;
    if (Count > ARM64_LOADER_MAX_EARLY_DEVICE_RANGES)
    {
        Count = ARM64_LOADER_MAX_EARLY_DEVICE_RANGES;
    }

    for (ULONG Index = 0; Index < Count; ++Index)
    {
        PARM64_LOADER_EARLY_DEVICE_RANGE Range;

        Range = &LoaderBlock->u.Arm64.EarlyDeviceRanges[Index];
        KiArm64MapIdentityDeviceRange(LoaderBlock,
                                      Range->BaseAddress,
                                      Range->Length,
                                      Attributes);
    }
}

CODE_SEG("INIT")
static BOOLEAN
KiArm64IsRangeMappedByDescriptors(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                                  _In_ UINT64 Start,
                                  _In_ UINT64 End)
{
    if (!LoaderBlock || (End <= Start))
        return FALSE;

    for (PLIST_ENTRY Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
            CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

        if (!KiArm64IsMappableMemoryType(Descriptor->MemoryType))
            continue;

        UINT64 RangeStart = (UINT64)Descriptor->BasePage << PAGE_SHIFT;
        UINT64 RangeEnd = RangeStart + ((UINT64)Descriptor->PageCount << PAGE_SHIFT);

        if ((Start < RangeEnd) && (End > RangeStart))
            return TRUE;
    }

    return FALSE;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadSctlrEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadTcrEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadTtbr0El1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadTtbr1El1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static __inline__ UINT64
KiArm64ReadMairEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, mair_el1" : "=r"(Value));
    return Value;
}

CODE_SEG("INIT")
static VOID
KiArm64CaptureMmuState(_Out_ PARM64_BOOT_CONTEXT BootContext)
{
    BootContext->SctlrEl1 = KiArm64ReadSctlrEl1();
    BootContext->TcrEl1 = KiArm64ReadTcrEl1();
    BootContext->Ttbr0El1 = KiArm64ReadTtbr0El1();
    BootContext->Ttbr1El1 = KiArm64ReadTtbr1El1();
    BootContext->MairEl1 = KiArm64ReadMairEl1();
    BootContext->MmuEnabled = (BootContext->SctlrEl1 & 1ULL) != 0;
}

CODE_SEG("INIT")
static VOID
KiArm64EnsureL1Entry(_In_ UINT64 Index)
{
    if (Index >= ARM64_L1_MAX_ENTRIES)
        return;

    if ((KiArm64IdentityL1[Index] & ARM64_PTE_TYPE_TABLE) == 0)
    {
        RtlZeroMemory(KiArm64IdentityL2[Index], sizeof(KiArm64IdentityL2[Index]));

        UINT64 L2Physical = KiArm64VirtualToPhysical((ULONG_PTR)&KiArm64IdentityL2[Index][0]);
        KiArm64IdentityL1[Index] = (L2Physical & ~((UINT64)PAGE_SIZE - 1ULL)) |
                                   ARM64_PTE_TABLE_ATTRS;
    }
}

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityRange(_In_ UINT64 PhysicalStart,
                        _In_ UINT64 PhysicalEnd,
                        _In_ UINT64 Attributes)
{
    const UINT64 PhysicalLimit = 512ULL << ARM64_L1_BLOCK_SHIFT;

    if (PhysicalStart >= PhysicalLimit)
        return;

    if (PhysicalEnd > PhysicalLimit)
        PhysicalEnd = PhysicalLimit;

    if (PhysicalEnd <= PhysicalStart)
        return;

    PhysicalStart &= ~(ARM64_L2_BLOCK_SIZE - 1ULL);
    PhysicalEnd = KiArm64AlignUp(PhysicalEnd, ARM64_L2_BLOCK_SIZE);

    while (PhysicalStart < PhysicalEnd)
    {
        UINT64 L1Index = PhysicalStart >> ARM64_L1_BLOCK_SHIFT;
        UINT64 L2Index = (PhysicalStart >> ARM64_L2_BLOCK_SHIFT) & 0x1FFULL;

        KiArm64EnsureL1Entry(L1Index);
        KiArm64IdentityL2[L1Index][L2Index] = PhysicalStart | Attributes;

        PhysicalStart += ARM64_L2_BLOCK_SIZE;
    }
}

DECLSPEC_NORETURN
CODE_SEG("INIT")
static VOID
KiArm64FatalHalt(VOID)
{
    __asm__ __volatile__("msr daifset, #0xf" ::: "memory");
    for (;;) {
        __asm__ __volatile__("wfi" ::: "memory");
    }
}

CODE_SEG("INIT")
 VOID
KiArm64EarlyVectorHandler(_In_ UINT64 VectorId,
                          _In_ UINT64 ExceptionSyndrome,
                          _In_ UINT64 FaultAddress,
                          _In_opt_ PARM64_EARLY_GPRS Registers)
{
    UNREFERENCED_PARAMETER(VectorId);
    UNREFERENCED_PARAMETER(ExceptionSyndrome);
    UNREFERENCED_PARAMETER(FaultAddress);
    UNREFERENCED_PARAMETER(Registers);

    /*
     * If final vectors are installed, return and let the permanent
     * vector path handle this exception/interrupt. Only halt when
     * still in early bring-up before KeInitExceptions has run.
     */
    if (KiArm64FinalVectorsInstalled)
    {
        return;
    }

    /* Early boot only: halt so the log stays visible */
    KiArm64FatalHalt();
}

CODE_SEG("INIT")
static UINT64
KiArm64VirtualToPhysical(_In_ UINT64 Virtual)
{
    if (Virtual >= ARM64_PHYS_MAP_BASE)
        return Virtual - ARM64_PHYS_MAP_BASE;

    if (Virtual >= ARM64_KERNEL_IMAGE_BASE)
        return Virtual - ARM64_KERNEL_IMAGE_BASE;

    if (Virtual >= ARM64_SYSTEM_RANGE_BASE)
        return Virtual - ARM64_SYSTEM_RANGE_BASE;

    return Virtual;
}

CODE_SEG("INIT")
static UINT64
KiArm64EnsureMairNtLayout(_In_ UINT64 CurrentMair)
{
    if (CurrentMair != ARM64_NT_MAIR_VALUE)
    {
        __asm__ __volatile__("msr mair_el1, %0" :: "r"(ARM64_NT_MAIR_VALUE));
        __asm__ __volatile__("isb");
    }

    return ARM64_NT_MAIR_VALUE;
}

CODE_SEG("INIT")
static UINT64
KiArm64AlignUp(_In_ UINT64 Value,
               _In_ UINT64 Alignment)
{
    return (Value + (Alignment - 1)) & ~(Alignment - 1);
}

CODE_SEG("INIT")
static VOID
KiArm64InitIdentityMapStorage(VOID)
{
    if (KiArm64IdentityL0 && KiArm64IdentityL1 && KiArm64IdentityL2)
        return;

    KiArm64IdentityL0 = (UINT64 *)KiArm64AlignUp((UINT64)KiArm64IdentityL0Backing,
                                                 PAGE_SIZE);
    KiArm64IdentityL1 = (UINT64 *)KiArm64AlignUp((UINT64)KiArm64IdentityL1Backing,
                                                 PAGE_SIZE);
    KiArm64IdentityL2 = (UINT64 (*)[512])KiArm64AlignUp((UINT64)KiArm64IdentityL2Backing,
                                                        PAGE_SIZE);

    ASSERT(((ULONG_PTR)KiArm64IdentityL0 & (PAGE_SIZE - 1)) == 0);
    ASSERT(((ULONG_PTR)KiArm64IdentityL1 & (PAGE_SIZE - 1)) == 0);
    ASSERT(((ULONG_PTR)KiArm64IdentityL2 & (PAGE_SIZE - 1)) == 0);
}

CODE_SEG("INIT")
static VOID
KiArm64InstallEarlyExceptionVectors(VOID)
{
    __asm__ __volatile__("msr vbar_el1, %0" :: "r"((ULONG_PTR)&KiArm64EarlyVectorTable));
    __asm__ __volatile__("isb");
}

CODE_SEG("INIT")
static VOID
KiArm64EnsureIdentityMapping(_Inout_ PARM64_BOOT_CONTEXT BootContext)
{
    UINT64 HighestPhysical = 0;
    UINT64 BlockAttributes;
    UINT64 TablePhysical;
    UINT64 Ttbr0Physical;
    UINT64 UpdatedMair;
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    UINT64 ExtraAddresses[16] = {0};
    ULONG ExtraCount = 0;

    LoaderBlock = BootContext->LoaderBlock;

    if (LoaderBlock)
    {
        UINT64 Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->KernelStack);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->Prcb);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->Thread);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)LoaderBlock);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->u.Arm64.PanicStack);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->u.Arm64.InterruptStack);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical(LoaderBlock->u.Arm64.PcrPage);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        for (PLIST_ENTRY Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
             Entry != &LoaderBlock->MemoryDescriptorListHead;
             Entry = Entry->Flink)
        {
            PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
                CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

            UINT64 RangeStart = (UINT64)Descriptor->BasePage << PAGE_SHIFT;
            UINT64 RangeEnd = RangeStart + ((UINT64)Descriptor->PageCount << PAGE_SHIFT);

            if (RangeEnd > HighestPhysical)
                HighestPhysical = RangeEnd;
        }
    }

    {
        UINT64 Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)KiSystemStartup);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL0);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;

        Candidate = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL1);
        if (Candidate > 0 && ExtraCount < RTL_NUMBER_OF(ExtraAddresses))
            ExtraAddresses[ExtraCount++] = Candidate;
        if (Candidate > HighestPhysical) HighestPhysical = Candidate;
    }

    KiArm64InitIdentityMapStorage();

    RtlZeroMemory(KiArm64IdentityL0, ARM64_IDENTITY_L0_ENTRIES * sizeof(UINT64));
    RtlZeroMemory(KiArm64IdentityL1, ARM64_IDENTITY_L1_ENTRIES * sizeof(UINT64));
    RtlZeroMemory(KiArm64IdentityL2,
                  ARM64_IDENTITY_L2_ENTRIES * sizeof(UINT64));

    BlockAttributes = ARM64_PTE_TYPE_BLOCK |
                      (ARM64_MEM_ATTR_NORMAL_WB << ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT) |
                      ARM64_PTE_BLOCK_INNER_SHARE |
                      ARM64_PTE_BLOCK_AF |
                      ARM64_PTE_BLOCK_UXN |
                      ARM64_PTE_BLOCK_PXN;

    TablePhysical = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL1);
    KiArm64IdentityL0[0] = (TablePhysical & ~((UINT64)PAGE_SIZE - 1ULL)) |
                           ARM64_PTE_TABLE_ATTRS;

    if (LoaderBlock)
    {
        for (PLIST_ENTRY Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
             Entry != &LoaderBlock->MemoryDescriptorListHead;
             Entry = Entry->Flink)
        {
            PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
                CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

            if (!KiArm64IsMappableMemoryType(Descriptor->MemoryType))
                continue;

            UINT64 RangeStart = (UINT64)Descriptor->BasePage << PAGE_SHIFT;
            UINT64 RangeEnd = RangeStart + ((UINT64)Descriptor->PageCount << PAGE_SHIFT);

            if (RangeEnd > RangeStart)
                KiArm64MapIdentityRange(RangeStart, RangeEnd, BlockAttributes);
        }
    }

    for (ULONG Index = 0; Index < RTL_NUMBER_OF(ExtraAddresses); ++Index)
    {
        UINT64 Address = ExtraAddresses[Index];
        if (Address == 0)
            continue;

        UINT64 Start = Address & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
        UINT64 End = Start + ARM64_L2_BLOCK_SIZE;
        KiArm64MapIdentityRange(Start, End, BlockAttributes);
    }

    if ((KiArm64IdentityL1[0] & ARM64_PTE_TYPE_TABLE) == 0)
    {
        KiArm64MapIdentityRange(0, ARM64_IDENTITY_MIN_BYTES, BlockAttributes);
    }

    /* Program the Win11 ARM64-visible MAIR layout. */
    UpdatedMair = KiArm64EnsureMairNtLayout(BootContext->MairEl1);
    BootContext->MairEl1 = UpdatedMair;

    UINT64 DeviceBlockAttrs = ARM64_PTE_TYPE_BLOCK |
                              (ARM64_MEM_ATTR_DEVICE_nGnRnE << ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT) |
                              ARM64_PTE_BLOCK_AF |
                              ARM64_PTE_BLOCK_UXN |
                              ARM64_PTE_BLOCK_PXN;

    Ttbr0Physical = KiArm64VirtualToPhysical((ULONG_PTR)KiArm64IdentityL0);

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(Ttbr0Physical));
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb");


    BootContext->Ttbr0El1 = Ttbr0Physical;
    BootContext->MmuEnabled = TRUE;
    KiArm64IdentityMapActive = TRUE;

    KiArm64MapEarlyDeviceRanges(LoaderBlock, DeviceBlockAttrs);

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb");


    /*
     * Map ALL ACPI tables from the XSDT/RSDT so that the HAL can access
     * them during ACPI phase 0 initialization. The HAL's HalpMapPhysicalMemory64
     * uses identity mapping before Mm is initialized, so all ACPI table pages
     * must be pre-mapped in the identity page tables.
     */
    KiArm64MapAllAcpiTables(LoaderBlock);

    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb");

}

DECLSPEC_NORETURN
CODE_SEG("INIT")
VOID
NTAPI
KiSystemStartup(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ARM64_BOOT_CONTEXT BootContext = {0};

    if (LoaderBlock && LoaderBlock->u.Arm64.EarlyUartAddress != 0)
    {
        ARM64_UART_INTERFACE LoaderInterface =
            (ARM64_UART_INTERFACE)LoaderBlock->u.Arm64.EarlyUartInterface;

        EarlyUartBaseAddress = LoaderBlock->u.Arm64.EarlyUartAddress;
        EarlyUartInterface = (LoaderInterface < Arm64UartMax) ?
                             LoaderInterface :
                             Arm64UartUnknown;
        EarlyUartPlatformId = Arm64PlatformGenericAcpi;
        EarlyUartInitialized = TRUE;
    }
    else
    {
        EarlyUartBaseAddress = 0;
        EarlyUartPlatformId = Arm64PlatformUnknown;
        EarlyUartInterface = Arm64UartUnknown;
        EarlyUartInitialized = TRUE;
    }

    /*
     * Install early exception vectors FIRST, before any memory access that
     * could fault. This ensures we get diagnostic output if UART or other
     * early accesses cause translation faults.
     */
    KiArm64InstallEarlyExceptionVectors();

    BootContext.LoaderBlock = LoaderBlock;

    KiArm64CaptureMmuState(&BootContext);
    KiArm64EnsureIdentityMapping(&BootContext);

    /* Switch to a clean boot stack before entering KiInitializeSystem */
    LoaderBlock->KernelStack = (ULONG_PTR)KiArm64P0BootStack;
    if (LoaderBlock->KernelStack < ARM64_KSEG0_BASE)
    {
        LoaderBlock->KernelStack += ARM64_KSEG0_BASE;
    }

    /*
     * Keep KernelStack as the idle-thread stack top, but enter C below the top.
     * KiInitializeThread later builds the idle startup frame below KernelStack.
     * This mirrors amd64, where KiSwitchToBootStack subtracts scratch space
     * before jumping into KiSystemStartupBootStack, keeping live boot frames
     * separate from the idle thread's synthetic startup frame.
     */
    KiArm64SwitchToBootStack(ALIGN_DOWN_BY(LoaderBlock->KernelStack -
                                           ARM64_P0_BOOT_STACK_ENTRY_RESERVE,
                                           16),
                             LoaderBlock);

    /* Not reached */
    KiArm64FatalHalt();
}
CODE_SEG("INIT")
DECLSPEC_NORETURN VOID NTAPI
KiArm64SystemStartupBootStack(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /*
     * ARM64 Boot Stack Initialization
     *
     * This function runs on the clean boot stack before entering the main
     * kernel initialization and then hands off to KiInitializeSystem.
     */
    KiInitializeSystem(LoaderBlock);

    /* Should never return */
    KiArm64FatalHalt();
}
