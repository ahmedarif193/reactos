/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/boot.c
 * PURPOSE:         Kernel entry stubs for ARM64
 */

#include <ntoskrnl.h>
#include <ntstrsafe.h>
#include <reactos/drivers/acpi/acpi.h>
#define NDEBUG
#include <debug.h>

/*
 * PL011 UART early debug helpers for pre-KD initialization tracing.
 * Base address is QEMU virt machine's PL011 UART at 0x09000000.
 * The UART must be identity-mapped before use; the bootloader sets this up.
 */
#define KI_ARM64_PL011_BASE     0x09000000UL
#define KI_ARM64_PL011_VA       (0xFFFF800000000000ULL + KI_ARM64_PL011_BASE)
#define KI_ARM64_PL011_FR_TXFF  (1U << 5)

CODE_SEG("INIT")
static inline VOID
KiArm64RawPutc(char Ch)
{
    volatile ULONG *Uart = (volatile ULONG *)KI_ARM64_PL011_VA;
    while (Uart[0x18 / sizeof(ULONG)] & KI_ARM64_PL011_FR_TXFF) {}
    Uart[0] = (ULONG)Ch;
}

CODE_SEG("INIT")
static inline VOID
KiArm64RawPuts(const char *Str)
{
    while (*Str)
    {
        if (*Str == '\n')
            KiArm64RawPutc('\r');
        KiArm64RawPutc(*Str++);
    }
}

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

#define ARM64_KSEG0_BASE            0xFFFF800000000000ULL
/* Memory attribute indices in MAIR_EL1 */
#define ARM64_MEM_ATTR_DEVICE_nGnRnE 0x0ULL
#define ARM64_MEM_ATTR_NORMAL_NC     0x1ULL
#define ARM64_MEM_ATTR_NORMAL_WC     0x2ULL
#define ARM64_MEM_ATTR_NORMAL_WB     0x4ULL
#define ARM64_PTE_TYPE_BLOCK        0x1ULL
#define ARM64_PTE_TYPE_TABLE        0x3ULL
#define ARM64_PTE_BLOCK_ATTR_INDEX_SHIFT 2
#define ARM64_PTE_BLOCK_INNER_SHARE (3ULL << 8)
#define ARM64_PTE_BLOCK_AF          (1ULL << 10)
#define ARM64_PTE_BLOCK_PXN         (1ULL << 53)
#define ARM64_PTE_BLOCK_UXN         (1ULL << 54)
#define ARM64_PTE_TABLE_NSTABLE     (1ULL << 63)
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
#define ARM64_GICR_STRIDE_DEFAULT   0x20000ULL
#define ARM64_GICR_MAX_MAP_BYTES    (64ULL << 20)

#define ARM64_IDENTITY_L0_ENTRIES   512
#define ARM64_IDENTITY_L1_ENTRIES   512
#define ARM64_IDENTITY_L2_ENTRIES   (512 * 512)

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

#define ACPI_MADT_TYPE_GENERIC_INTERRUPT      0x0B
#define ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR    0x0C
#define ACPI_MADT_TYPE_GENERIC_MSI_FRAME      0x0D
#define ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR  0x0E
#define ACPI_MADT_TYPE_GENERIC_TRANSLATOR     0x0F

typedef struct _ACPI_SUBTABLE_HEADER
{
    UCHAR Type;
    UCHAR Length;
} ACPI_SUBTABLE_HEADER, *PACPI_SUBTABLE_HEADER;

#include <pshpack1.h>
typedef struct _ACPI_MADT
{
    DESCRIPTION_HEADER Header;
    ULONG LocalApicAddress;
    ULONG Flags;
} ACPI_MADT, *PACPI_MADT;

typedef struct _ACPI_MADT_GENERIC_DISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG GicId;
    ULONGLONG BaseAddress;
    ULONG SystemVectorBase;
    UCHAR GicVersion;
    UCHAR Reserved2[3];
} ACPI_MADT_GENERIC_DISTRIBUTOR, *PACPI_MADT_GENERIC_DISTRIBUTOR;

typedef struct _ACPI_MADT_GENERIC_INTERRUPT
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG CpuInterfaceNumber;
    ULONG AcpiProcessorUid;
    ULONG Flags;
    ULONG ParkingProtocolVersion;
    ULONG PerformanceInterrupt;
    ULONGLONG ParkedAddress;
    ULONGLONG BaseAddress;
    ULONGLONG GicvBaseAddress;
    ULONGLONG GichBaseAddress;
    ULONG VgicMaintenanceInterrupt;
    ULONG Reserved2;
    ULONGLONG GicrBaseAddress;
    ULONGLONG Mpidr;
} ACPI_MADT_GENERIC_INTERRUPT, *PACPI_MADT_GENERIC_INTERRUPT;

typedef struct _ACPI_MADT_GENERIC_REDISTRIBUTOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONGLONG BaseAddress;
    ULONG Length;
    ULONG Reserved2;
} ACPI_MADT_GENERIC_REDISTRIBUTOR, *PACPI_MADT_GENERIC_REDISTRIBUTOR;

typedef struct _ACPI_MADT_GENERIC_MSI_FRAME
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG MsiFrameId;
    ULONGLONG BaseAddress;
    ULONG Flags;
    USHORT SpiCount;
    USHORT SpiBase;
} ACPI_MADT_GENERIC_MSI_FRAME, *PACPI_MADT_GENERIC_MSI_FRAME;

typedef struct _ACPI_MADT_GENERIC_TRANSLATOR
{
    ACPI_SUBTABLE_HEADER Header;
    USHORT Reserved;
    ULONG TranslationId;
    ULONGLONG BaseAddress;
    ULONG Reserved2;
} ACPI_MADT_GENERIC_TRANSLATOR, *PACPI_MADT_GENERIC_TRANSLATOR;
#include <poppack.h>

typedef struct _ARM64_EARLY_TRAP_STATE
{
    ARM64_EARLY_GPRS Registers;
    UINT64 VectorId;
    UINT64 ExceptionSyndrome;
    UINT64 FaultAddress;
    UINT64 Elr;
    UINT64 Spsr;
} ARM64_EARLY_TRAP_STATE, *PARM64_EARLY_TRAP_STATE;

static ARM64_EARLY_TRAP_STATE KiArm64LastTrapState;
static BOOLEAN KiArm64TrapStateValid = FALSE;

static const PCSTR KiArm64EsrClassNames[64] =
{
    [0x00] = "Unknown",
    [0x01] = "WFI/WFE trap",
    [0x03] = "CP15 RT trap",
    [0x04] = "CP15 R trap",
    [0x05] = "CP15 W trap",
    [0x07] = "FP/SIMD access",
    [0x08] = "MCRR/MRRC trap",
    [0x0C] = "SVE access",
    [0x11] = "SVC in AArch32",
    [0x12] = "HVC in AArch32",
    [0x13] = "SMC in AArch32",
    [0x15] = "SVC in AArch64",
    [0x16] = "HVC in AArch64",
    [0x17] = "SMC in AArch64",
    [0x18] = "MSR/MRS trap",
    [0x1C] = "Instruction abort (lower EL)",
    [0x1D] = "Instruction abort (same EL)",
    [0x20] = "Data abort (lower EL)",
    [0x21] = "Data abort (same EL)",
    [0x22] = "SP alignment fault",
    [0x24] = "FP exception",
    [0x26] = "FP exception (AArch64)",
    [0x2F] = "SError interrupt",
};

static const PCSTR KiArm64VectorNames[16] =
{
    [0]  = "Sync SP0",
    [1]  = "IRQ SP0",
    [2]  = "FIQ SP0",
    [3]  = "SError SP0",
    [4]  = "Sync SPx",
    [5]  = "IRQ SPx",
    [6]  = "FIQ SPx",
    [7]  = "SError SPx",
    [8]  = "Sync lower A64",
    [9]  = "IRQ lower A64",
    [10] = "FIQ lower A64",
    [11] = "SError lower A64",
    [12] = "Sync lower A32",
    [13] = "IRQ lower A32",
    [14] = "FIQ lower A32",
    [15] = "SError lower A32",
};

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

/* Boot stack (CPU0): mirror amd64 boot stack handoff behavior */
UCHAR DECLSPEC_ALIGN(16) KiArm64P0BootStackData[KERNEL_STACK_SIZE] = {0};
PVOID KiArm64P0BootStack = &KiArm64P0BootStackData[KERNEL_STACK_SIZE];

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

/* KiArm64DescribeEsr is defined in trapdump.c */
#if 0
CODE_SEG("INIT")
static PCSTR
KiArm64DescribeEsr(_In_ UINT64 ExceptionSyndrome)
{
    ULONG Class = (ULONG)((ExceptionSyndrome >> 26) & 0x3FULL);

    if (Class < RTL_NUMBER_OF(KiArm64EsrClassNames) &&
        KiArm64EsrClassNames[Class] != NULL)
    {
        return KiArm64EsrClassNames[Class];
    }

    return "Unknown";
}
#endif

CODE_SEG("INIT")
static VOID
KiArm64MapIdentityRange(_In_ UINT64 PhysicalStart,
                        _In_ UINT64 PhysicalEnd,
                        _In_ UINT64 Attributes);

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
static PRSDP
KiArm64FindRsdp(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PCONFIGURATION_COMPONENT_DATA ComponentEntry;
    PCONFIGURATION_COMPONENT_DATA Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PACPI_BIOS_MULTI_NODE NodeData;

    if (!KiArm64IdentityMapActive)
        return NULL;

    if (!LoaderBlock || !LoaderBlock->ConfigurationRoot)
        return NULL;

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
        return NULL;

    ResourceList = ComponentEntry->ConfigurationData;
    NodeData = (PACPI_BIOS_MULTI_NODE)(ResourceList + 1);
    if (!NodeData || !NodeData->RsdpAddress.QuadPart)
        return NULL;

    KiArm64MapIdentityAcpiRange(NodeData->RsdpAddress.QuadPart, sizeof(RSDP));
    PRSDP Rsdp = (PRSDP)(ULONG_PTR)NodeData->RsdpAddress.QuadPart;
    if (!Rsdp || (Rsdp->Signature != RSDP_SIGNATURE))
        return NULL;

    return Rsdp;
}

CODE_SEG("INIT")
static PDESCRIPTION_HEADER
KiArm64GetAcpiTable(_In_opt_ PRSDP Rsdp,
                    _In_ ULONG Signature)
{
    ULONG Count;
    ULONG Index;

    if (!Rsdp)
        return NULL;

    if (!KiArm64IdentityMapActive)
        return NULL;

    if ((Rsdp->Revision >= 2) && (Rsdp->XsdtAddress.QuadPart != 0))
    {
        UINT64 XsdtPa = Rsdp->XsdtAddress.QuadPart;
        KiArm64MapIdentityAcpiRange(XsdtPa, sizeof(DESCRIPTION_HEADER));
        PXSDT Xsdt = (PXSDT)(ULONG_PTR)XsdtPa;
        ULONG Length = Xsdt->Header.Length;

        if ((Xsdt->Header.Signature != XSDT_SIGNATURE) ||
            (Length < sizeof(DESCRIPTION_HEADER)))
        {
            return NULL;
        }

        KiArm64MapIdentityAcpiRange(XsdtPa, Length);
        Count = (Length - sizeof(DESCRIPTION_HEADER)) / sizeof(PHYSICAL_ADDRESS);
        for (Index = 0; Index < Count; ++Index)
        {
            ULONGLONG TablePa = KiArm64ReadUnalignedU64(&Xsdt->Tables[Index]);
            PDESCRIPTION_HEADER Header;

            if (TablePa == 0)
                continue;

            KiArm64MapIdentityAcpiRange(TablePa, sizeof(DESCRIPTION_HEADER));
            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)TablePa;
            if (Header->Signature == Signature)
            {
                KiArm64MapIdentityAcpiRange(TablePa, Header->Length);
                return Header;
            }
        }
    }
    else if (Rsdp->RsdtAddress != 0)
    {
        UINT64 RsdtPa = (UINT64)Rsdp->RsdtAddress;
        KiArm64MapIdentityAcpiRange(RsdtPa, sizeof(DESCRIPTION_HEADER));
        PRSDT Rsdt = (PRSDT)(ULONG_PTR)RsdtPa;
        ULONG Length = Rsdt->Header.Length;

        if ((Rsdt->Header.Signature != RSDT_SIGNATURE) ||
            (Length < sizeof(DESCRIPTION_HEADER)))
        {
            return NULL;
        }

        KiArm64MapIdentityAcpiRange(RsdtPa, Length);
        Count = (Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONG);
        for (Index = 0; Index < Count; ++Index)
        {
            ULONG TablePa = Rsdt->Tables[Index];
            PDESCRIPTION_HEADER Header;

            if (TablePa == 0)
                continue;

            KiArm64MapIdentityAcpiRange(TablePa, sizeof(DESCRIPTION_HEADER));
            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)TablePa;
            if (Header->Signature == Signature)
            {
                KiArm64MapIdentityAcpiRange(TablePa, Header->Length);
                return Header;
            }
        }
    }

    return NULL;
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
 * mapped during KiArm64GetAcpiTable lookups, causing page faults when the
 * HAL tries to read full table contents.
 */
CODE_SEG("INIT")
static VOID
KiArm64MapAllAcpiTables(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRSDP Rsdp;
    ULONG Count;
    ULONG Index;

    if (!KiArm64IdentityMapActive)
    {
        return;
    }

    Rsdp = KiArm64FindRsdp(LoaderBlock);
    if (!Rsdp)
    {
        return;
    }

    if ((Rsdp->Revision >= 2) && (Rsdp->XsdtAddress.QuadPart != 0))
    {
        UINT64 XsdtPa = Rsdp->XsdtAddress.QuadPart;
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
    else if (Rsdp->RsdtAddress != 0)
    {
        UINT64 RsdtPa = (UINT64)Rsdp->RsdtAddress;
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

    if (Base == 0)
        return;

    Start = Base & ~(ARM64_L2_BLOCK_SIZE - 1ULL);

    if (KiArm64IsRangeMappedByDescriptors(LoaderBlock,
                                          Start,
                                          Start + ARM64_L2_BLOCK_SIZE))
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
static BOOLEAN
KiArm64MapGicMmioFromMadt(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
                          _In_ UINT64 Attributes)
{
    PRSDP Rsdp;
    PACPI_MADT Madt;
    ULONG Offset;
    ULONG TotalLength;
    BOOLEAN Mapped = FALSE;
    BOOLEAN MappedGicd = FALSE;
    BOOLEAN MappedGicc = FALSE;
    BOOLEAN HasGicrEntry = FALSE;

    Rsdp = KiArm64FindRsdp(LoaderBlock);
    if (!Rsdp)
        return FALSE;

    Madt = (PACPI_MADT)KiArm64GetAcpiTable(Rsdp, APIC_SIGNATURE);
    if (!Madt)
        return FALSE;

    TotalLength = Madt->Header.Length;
    if (TotalLength < sizeof(ACPI_MADT))
        return FALSE;

    Offset = sizeof(ACPI_MADT);
    while (Offset + sizeof(ACPI_SUBTABLE_HEADER) <= TotalLength)
    {
        PACPI_SUBTABLE_HEADER Entry = (PACPI_SUBTABLE_HEADER)((ULONG_PTR)Madt + Offset);

        if (Entry->Length < sizeof(ACPI_SUBTABLE_HEADER))
            break;

        if (Offset + Entry->Length > TotalLength)
            break;

        switch (Entry->Type)
        {
            case ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
            {
                if (!MappedGicd && Entry->Length >= sizeof(ACPI_MADT_GENERIC_DISTRIBUTOR))
                {
                    PACPI_MADT_GENERIC_DISTRIBUTOR Dist =
                        (PACPI_MADT_GENERIC_DISTRIBUTOR)Entry;
                    UINT64 Base = KiArm64ReadUnalignedU64(&Dist->BaseAddress);

                    if (Base)
                    {
                        KiArm64MapIdentityDeviceBlock(LoaderBlock, Base, Attributes);
                        Mapped = TRUE;
                        MappedGicd = TRUE;
                    }
                }
                break;
            }
            case ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:
            {
                if (Entry->Length >= sizeof(ACPI_MADT_GENERIC_REDISTRIBUTOR))
                {
                    PACPI_MADT_GENERIC_REDISTRIBUTOR Redist =
                        (PACPI_MADT_GENERIC_REDISTRIBUTOR)Entry;
                    UINT64 Base = KiArm64ReadUnalignedU64(&Redist->BaseAddress);
                    ULONG Length;

                    RtlCopyMemory(&Length, &Redist->Length, sizeof(Length));
                    if (Base)
                    {
                        UINT64 MapLength = Length ? (UINT64)Length :
                            (UINT64)MAXIMUM_PROCESSORS * ARM64_GICR_STRIDE_DEFAULT;

                        if (MapLength > ARM64_GICR_MAX_MAP_BYTES)
                            MapLength = ARM64_GICR_MAX_MAP_BYTES;

                        KiArm64MapIdentityDeviceRange(LoaderBlock, Base, MapLength, Attributes);
                        Mapped = TRUE;
                    }
                    HasGicrEntry = TRUE;
                }
                break;
            }
            case ACPI_MADT_TYPE_GENERIC_TRANSLATOR:
            {
                if (Entry->Length >= sizeof(ACPI_MADT_GENERIC_TRANSLATOR))
                {
                    PACPI_MADT_GENERIC_TRANSLATOR Its =
                        (PACPI_MADT_GENERIC_TRANSLATOR)Entry;
                    UINT64 Base = KiArm64ReadUnalignedU64(&Its->BaseAddress);

                    if (Base)
                    {
                        KiArm64MapIdentityDeviceBlock(LoaderBlock, Base, Attributes);
                        Mapped = TRUE;
                    }
                }
                break;
            }
            case ACPI_MADT_TYPE_GENERIC_MSI_FRAME:
            {
                if (Entry->Length >= sizeof(ACPI_MADT_GENERIC_MSI_FRAME))
                {
                    PACPI_MADT_GENERIC_MSI_FRAME Frame =
                        (PACPI_MADT_GENERIC_MSI_FRAME)Entry;
                    UINT64 Base = KiArm64ReadUnalignedU64(&Frame->BaseAddress);

                    if (Base)
                    {
                        KiArm64MapIdentityDeviceBlock(LoaderBlock, Base, Attributes);
                        Mapped = TRUE;
                    }
                }
                break;
            }
            case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
            {
                if (!MappedGicc && Entry->Length >= sizeof(ACPI_MADT_GENERIC_INTERRUPT))
                {
                    PACPI_MADT_GENERIC_INTERRUPT Gicc =
                        (PACPI_MADT_GENERIC_INTERRUPT)Entry;
                    UINT64 Base = KiArm64ReadUnalignedU64(&Gicc->BaseAddress);
                    UINT64 GicrBase = KiArm64ReadUnalignedU64(&Gicc->GicrBaseAddress);

                    if (Base)
                    {
                        KiArm64MapIdentityDeviceBlock(LoaderBlock, Base, Attributes);
                        Mapped = TRUE;
                        MappedGicc = TRUE;
                    }

                    if (!HasGicrEntry && GicrBase)
                    {
                        KiArm64MapIdentityDeviceBlock(LoaderBlock, GicrBase, Attributes);
                        Mapped = TRUE;
                    }
                }
                break;
            }
            default:
                break;
        }

        Offset += Entry->Length;
    }

    return Mapped;
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
                                   ARM64_PTE_TYPE_TABLE |
                                   ARM64_PTE_TABLE_NSTABLE;
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
    UINT64 Spsr, Elr;
    ARM64_EARLY_GPRS LocalRegisters = {0};

    __asm__ __volatile__("mrs %0, spsr_el1" : "=r"(Spsr));
    __asm__ __volatile__("mrs %0, elr_el1"  : "=r"(Elr));

    if (Registers)
    {
        LocalRegisters = *Registers;
    }

    KiArm64LastTrapState.Registers = LocalRegisters;
    KiArm64LastTrapState.VectorId = VectorId;
    KiArm64LastTrapState.ExceptionSyndrome = ExceptionSyndrome;
    KiArm64LastTrapState.FaultAddress = FaultAddress;
    KiArm64LastTrapState.Elr = Elr;
    KiArm64LastTrapState.Spsr = Spsr;
    KiArm64TrapStateValid = TRUE;


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
    if (Virtual >= ARM64_KSEG0_BASE)
        return Virtual - ARM64_KSEG0_BASE;

    return Virtual;
}

CODE_SEG("INIT")
static UINT64
KiArm64EnsureMairNormalWb(_In_ UINT64 CurrentMair)
{
    const UINT64 AttributeMask = 0xFFULL << (ARM64_MEM_ATTR_NORMAL_WB * 8);

    if ((CurrentMair & AttributeMask) == AttributeMask)
        return CurrentMair;

    UINT64 Updated = (CurrentMair & ~AttributeMask) | (0xFFULL << (ARM64_MEM_ATTR_NORMAL_WB * 8));
    __asm__ __volatile__("msr mair_el1, %0" :: "r"(Updated));
    __asm__ __volatile__("isb");
    return Updated;
}

CODE_SEG("INIT")
static UINT64
KiArm64EnsureMairDeviceNgnrne(_In_ UINT64 CurrentMair)
{
    /* Ensure MAIR attr index 0 encodes Device-nGnRnE (0x00) */
    const UINT64 AttributeMask = 0xFFULL << (ARM64_MEM_ATTR_DEVICE_nGnRnE * 8);
    UINT64 Updated = (CurrentMair & ~AttributeMask) | (0x00ULL << (ARM64_MEM_ATTR_DEVICE_nGnRnE * 8));
    if (Updated != CurrentMair)
    {
        __asm__ __volatile__("msr mair_el1, %0" :: "r"(Updated));
        __asm__ __volatile__("isb");
    }
    return Updated;
}

CODE_SEG("INIT")
static UINT64
KiArm64EnsureMairNormalNc(_In_ UINT64 CurrentMair,
                          _In_ UINT64 AttrIndex)
{
    const UINT64 AttributeMask = 0xFFULL << (AttrIndex * 8);
    UINT64 Updated = (CurrentMair & ~AttributeMask) | (0x44ULL << (AttrIndex * 8));

    if (Updated != CurrentMair)
    {
        __asm__ __volatile__("msr mair_el1, %0" :: "r"(Updated));
        __asm__ __volatile__("isb");
    }

    return Updated;
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
    KiArm64IdentityL0[0] = (TablePhysical & ~((UINT64)PAGE_SIZE - 1ULL)) | ARM64_PTE_TYPE_TABLE;

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

    /* Program MAIR for Normal-WB, Normal-NC, and Device-nGnRnE attributes */
    UpdatedMair = KiArm64EnsureMairNormalWb(BootContext->MairEl1);
    UpdatedMair = KiArm64EnsureMairNormalNc(UpdatedMair, ARM64_MEM_ATTR_NORMAL_NC);
    UpdatedMair = KiArm64EnsureMairNormalNc(UpdatedMair, ARM64_MEM_ATTR_NORMAL_WC);
    UpdatedMair = KiArm64EnsureMairDeviceNgnrne(UpdatedMair);
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

    /* Map GIC MMIO windows based on MADT, fallback to QEMU virt defaults. */
    if (!KiArm64MapGicMmioFromMadt(LoaderBlock, DeviceBlockAttrs))
    {
        /*
         * Map the QEMU virt default GIC regions:
         *   - GICD: 0x08000000
         *   - GICC: 0x08010000
         *   - GICH/GICV: 0x08030000-0x08050000
         */
        const UINT64 GicStart = 0x08000000ULL & ~(ARM64_L2_BLOCK_SIZE - 1ULL);
        const UINT64 GicEnd = GicStart + (8 * ARM64_L2_BLOCK_SIZE);

        KiArm64MapIdentityRange(GicStart, GicEnd, DeviceBlockAttrs);
    }

    /*
     * QEMU GICv3 redistributor region is typically at 0x80a0000 for highmem=off,
     * but at 0x100000000 (4GB) for highmem=on or when using TCG emulation with
     * more than 4GB RAM. Always map the 4GB GICR region to handle both cases.
     * Note: The HAL now detects when ACPI reports highmem GICR addresses that
     * don't match actual hardware and falls back to the lowmem region.
     */
    {
        const UINT64 GicrHighStart = 0x100000000ULL;
        const UINT64 GicrHighEnd = GicrHighStart + (64ULL << 20);  /* 64MB for GICR */
        KiArm64MapIdentityRange(GicrHighStart, GicrHighEnd, DeviceBlockAttrs);
    }

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

    /* CYCLE31: Trace boot path to verify this function is called */
    KiArm64RawPuts("[CYCLE31] KiSystemStartup ENTRY\n");

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
    KiArm64SwitchToBootStack(LoaderBlock->KernelStack, LoaderBlock);

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
    /* CYCLE31: Trace boot path before calling KiInitializeSystem */
    KiArm64RawPuts("[CYCLE31] KiArm64SystemStartupBootStack: about to call KiInitializeSystem\n");
    KiInitializeSystem(LoaderBlock);

    /* Should never return */
    KiArm64FatalHalt();
}
