/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halx86/acpi/halacpi.c
 * PURPOSE:         HAL ACPI Code
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <halacpi.h>
#include <ntifs.h>
#include <stdarg.h>
#define NDEBUG
#include <debug.h>

extern VOID HalpPciLogEcamCoverage(VOID);

NTSYSAPI NTSTATUS NTAPI NtShutdownSystem(_In_ SHUTDOWN_ACTION Action);

/* GLOBALS ********************************************************************/

LIST_ENTRY HalpAcpiTableCacheList;
FAST_MUTEX HalpAcpiTableCacheLock;

BOOLEAN HalpProcessedACPIPhase0;
BOOLEAN HalpPhysicalMemoryMayAppearAbove4GB;

FADT HalpFixedAcpiDescTable;
PDEBUG_PORT_TABLE HalpDebugPortTable;
PACPI_SRAT HalpAcpiSrat;
PBOOT_TABLE HalpSimpleBootFlagTable;

PHYSICAL_ADDRESS HalpMaxHotPlugMemoryAddress;
PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
PHARDWARE_PTE HalpPteForFlush;
PVOID HalpVirtAddrForFlush;
PVOID HalpLowStub;

PACPI_BIOS_MULTI_NODE HalpAcpiMultiNode;
PHYSICAL_ADDRESS HalpAcpiRsdpAddress;

LIST_ENTRY HalpAcpiTableMatchList;

ULONG HalpInvalidAcpiTable;

PHALP_ACPI_MCFG HalpAcpiMcfgTable;
PHALP_ACPI_MCFG_ALLOCATION HalpAcpiMcfgAllocations;
ULONG HalpAcpiMcfgAllocationCount;
volatile LONG HalpAcpiEcamCoverageFlags;
BOOLEAN HalpAcpiEcamDisabled;

static
LONG
HalpAcpiRecordEcamEvent(
    _In_ ULONG Flag,
    _In_opt_z_ PCSTR Message)
{
    LONG PreviousFlags;

    PreviousFlags = InterlockedOr(&HalpAcpiEcamCoverageFlags, Flag);
    if (Message && !(PreviousFlags & Flag))
    {
        DPRINT1("%s\n", Message);
    }

    return PreviousFlags;
}

#define HALP_ACPI_OVERRIDE_ALIGNMENT   8
#define ACPI_GAS_SYSTEM_MEMORY         0
#define ACPI_GAS_SYSTEM_IO             1
#define ACPI_PM1_STATUS_POWER_BUTTON   0x0100
#ifndef ACPI_FADT_POWER_BUTTON
#define ACPI_FADT_POWER_BUTTON         (1 << 4)
#endif

ULONG HalpPicVectorRedirect[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

PHYSICAL_ADDRESS HalpFacsPhysicalAddress;
GEN_ADDR HalpPmTimerBlock;
BOOLEAN HalpPmTimerBlockValid;
BOOLEAN HalpPmTimerInitialized;
BOOLEAN HalpPmTimerMemoryMapped;
volatile ULONG *HalpPmTimerRegister;
PVOID HalpPmTimerMappingBase;
PFN_COUNT HalpPmTimerMappingPages;
ULONG HalpPmTimerPort;
ULONG HalpPmTimerMask;
ULONG HalpPmTimerBitShift;
ULONG HalpAcpiPmTimerFrequency = 3579545UL;
ULONG HalpAppliedAcpiOverrides;
GEN_ADDR HalpPm1EventBlocks[2];
GEN_ADDR HalpPm1ControlBlocks[2];
GEN_ADDR HalpPm2ControlBlock;
GEN_ADDR HalpGeneralPurposeBlocks[2];
BOOLEAN HalpPm1EventBlockValid[2];
BOOLEAN HalpPm1ControlBlockValid[2];
BOOLEAN HalpPm2ControlBlockValid;
BOOLEAN HalpGeneralPurposeBlockValid[2];
BOOLEAN HalpAcpiOverrideAttempted;
BOOLEAN HalpPowerButtonShutdownInitiated;

static
VOID
HalpAcpiInstallOverrideTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDESCRIPTION_HEADER TableHeader);

static
VOID
HalpAppendFormatA(
    _Inout_updates_z_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_z_ PCSTR Format,
    ...);

static
VOID
HalpAcpiApplyLoaderOverrides(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_reads_bytes_(OverrideSize) PVOID OverrideBuffer,
    _In_ ULONG OverrideSize);

static
PFN_COUNT
HalpAcpiPagesForRange(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONG Length);

static
PHYSICAL_ADDRESS
HalpAcpiSelectFadtPointer(
    _In_ ULONG LegacyPointer,
    _In_ PHYSICAL_ADDRESS ExtendedPointer);

static
BOOLEAN
HalpAcpiGasValid(
    _In_ const GEN_ADDR *Address);

static
VOID
HalpAcpiInitializePmTimerBlock(
    _In_ PFADT Fadt);

static
BOOLEAN
HalpAcpiInitializeGenericBlock(
    _In_opt_ const GEN_ADDR *Extended,
    _In_ ULONG LegacyAddress,
    _In_ UCHAR Length,
    _In_ UCHAR DefaultBitWidth,
    _Out_ GEN_ADDR *TargetGas);

static
VOID
HalpAcpiInitializePmIoBlocks(
    _In_ PFADT Fadt);

static
BOOLEAN
HalpAcpiReadRegister(
    _In_ const GEN_ADDR *Gas,
    _Out_ ULONG *Value);

static
BOOLEAN
HalpAcpiWriteRegister(
    _In_ const GEN_ADDR *Gas,
    _In_ ULONG Value);

static
ULONG
HalpAcpiGetRegisterByteWidth(
    _In_ const GEN_ADDR *Gas)
{
    ULONG Bytes;

    if (!Gas)
    {
        return sizeof(ULONG);
    }

    Bytes = (Gas->BitWidth + 7) >> 3;
    if (Bytes < sizeof(ULONG))
    {
        Bytes = sizeof(ULONG);
    }

    return Bytes;
}

/* This determines the HAL type */
BOOLEAN HalDisableFirmwareMapper = TRUE;
PWCHAR HalHardwareIdString = L"acpipic_up";
PWCHAR HalName = L"ACPI Compatible Eisa/Isa HAL";

/* PRIVATE FUNCTIONS **********************************************************/

PDESCRIPTION_HEADER
NTAPI
HalpAcpiGetCachedTable(IN ULONG Signature)
{
    PLIST_ENTRY ListHead, NextEntry;
    PACPI_CACHED_TABLE CachedTable;

    /* Loop cached tables */
    ListHead = &HalpAcpiTableCacheList;
    NextEntry = ListHead->Flink;
    while (NextEntry != ListHead)
    {
        /* Get the table */
        CachedTable = CONTAINING_RECORD(NextEntry, ACPI_CACHED_TABLE, Links);

        /* Compare signatures */
        if (CachedTable->Header.Signature == Signature) return &CachedTable->Header;

        /* Keep going */
        NextEntry = NextEntry->Flink;
    }

    /* Nothing found */
    return NULL;
}

VOID
NTAPI
HalpAcpiCacheTable(IN PDESCRIPTION_HEADER TableHeader)
{
    PACPI_CACHED_TABLE CachedTable;

    /* Get the cached table and link it */
    CachedTable = CONTAINING_RECORD(TableHeader, ACPI_CACHED_TABLE, Header);
    InsertTailList(&HalpAcpiTableCacheList, &CachedTable->Links);
}

PVOID
NTAPI
HalpAcpiCopyBiosTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN PDESCRIPTION_HEADER TableHeader)
{
    ULONG Size;
    PFN_COUNT PageCount;
    PHYSICAL_ADDRESS PhysAddress;
    PACPI_CACHED_TABLE CachedTable;
    PDESCRIPTION_HEADER CopiedTable;

    /* Size we'll need for the cached table */
    Size = TableHeader->Length + FIELD_OFFSET(ACPI_CACHED_TABLE, Header);
    if (LoaderBlock)
    {
        /* Phase 0: Convert to pages and use the HAL heap */
        PageCount = BYTES_TO_PAGES(Size);
        PhysAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                       0x1000000,
                                                       PageCount,
                                                       FALSE);
        if (PhysAddress.QuadPart)
        {
            /* Map it */
            CachedTable = HalpMapPhysicalMemory64(PhysAddress, PageCount);
        }
        else
        {
            /* No memory, so nothing to map */
            CachedTable = NULL;
        }
    }
    else
    {
        /* Use Mm pool */
        CachedTable = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_HAL);
    }

    /* Do we have the cached table? */
    if (CachedTable)
    {
        /* Copy the data */
        CopiedTable = &CachedTable->Header;
        RtlCopyMemory(CopiedTable, TableHeader, TableHeader->Length);
    }
    else
    {
        /* Nothing to return */
        CopiedTable = NULL;
    }

    /* Return the table */
    return CopiedTable;
}

static
VOID
HalpAcpiInstallOverrideTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDESCRIPTION_HEADER TableHeader)
{
    PACPI_CACHED_TABLE CachedTable;
    PDESCRIPTION_HEADER CachedHeader;

    CachedHeader = HalpAcpiCopyBiosTable(LoaderBlock, TableHeader);
    if (!CachedHeader)
    {
        DPRINT1("HAL: Failed to cache ACPI override for %c%c%c%c\n",
                TableHeader->Signature & 0xFF,
                (TableHeader->Signature >> 8) & 0xFF,
                (TableHeader->Signature >> 16) & 0xFF,
                (TableHeader->Signature >> 24) & 0xFF);
        return;
    }

    CachedTable = CONTAINING_RECORD(CachedHeader, ACPI_CACHED_TABLE, Header);

    /* Prepend so override supersedes firmware copy */
    InsertHeadList(&HalpAcpiTableCacheList, &CachedTable->Links);

    ++HalpAppliedAcpiOverrides;

    DPRINT1("HAL: ACPI override applied for %c%c%c%c (len %lu)\n",
            TableHeader->Signature & 0xFF,
            (TableHeader->Signature >> 8) & 0xFF,
            (TableHeader->Signature >> 16) & 0xFF,
            (TableHeader->Signature >> 24) & 0xFF,
            TableHeader->Length);
}

static
VOID
HalpAcpiApplyLoaderOverrides(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_reads_bytes_(OverrideSize) PVOID OverrideBuffer,
    _In_ ULONG OverrideSize)
{
    PUCHAR Current;
    PUCHAR End;
    ULONG TableIndex = 0;

    if (!OverrideBuffer || !OverrideSize)
    {
        return;
    }

    HalpAcpiOverrideAttempted = TRUE;

    Current = OverrideBuffer;
    End = Current + OverrideSize;

    while ((SIZE_T)(End - Current) >= sizeof(DESCRIPTION_HEADER))
    {
        PDESCRIPTION_HEADER TableHeader;
        ULONG TableLength;
        SIZE_T Advance;

        TableHeader = (PDESCRIPTION_HEADER)Current;
        TableLength = TableHeader->Length;

        if ((TableLength < sizeof(DESCRIPTION_HEADER)) ||
            (TableLength > (ULONG)(End - Current)))
        {
            DPRINT1("HAL: ACPI override #%lu has invalid length (%lu)\n",
                    TableIndex,
                    TableLength);
            break;
        }

        if (TableLength == 0)
        {
            DPRINT1("HAL: ACPI override #%lu has zero length\n", TableIndex);
            break;
        }

        HalpAcpiInstallOverrideTable(LoaderBlock, TableHeader);

        Advance = TableLength;
        Advance = (Advance + (HALP_ACPI_OVERRIDE_ALIGNMENT - 1)) &
                  ~(SIZE_T)(HALP_ACPI_OVERRIDE_ALIGNMENT - 1);

        Current += Advance;
        ++TableIndex;
    }

    if ((SIZE_T)(End - Current) > 0)
    {
        ULONG Remaining;

        Remaining = (ULONG)(End - Current);
        DPRINT1("HAL: ACPI override data leftover (%lu bytes)\n", Remaining);
    }

    if (LoaderBlock && HalpAppliedAcpiOverrides)
    {
        DPRINT1("HAL: ACPI override tables installed: %lu\n",
                HalpAppliedAcpiOverrides);
    }
}

static
PFN_COUNT
HalpAcpiPagesForRange(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONG Length)
{
    ULONGLONG Offset;
    ULONGLONG TotalBytes;
    ULONGLONG Pages;

    Offset = BaseAddress.QuadPart & (PAGE_SIZE - 1);
    TotalBytes = Offset + Length;
    Pages = (TotalBytes + PAGE_SIZE - 1) >> PAGE_SHIFT;

    if (!Pages)
    {
        Pages = 1;
    }

    return (PFN_COUNT)Pages;
}

static
PHYSICAL_ADDRESS
HalpAcpiSelectFadtPointer(
    _In_ ULONG LegacyPointer,
    _In_ PHYSICAL_ADDRESS ExtendedPointer)
{
    PHYSICAL_ADDRESS Address;

    if (ExtendedPointer.QuadPart)
    {
        Address = ExtendedPointer;
    }
    else
    {
        Address.QuadPart = 0;
        Address.LowPart = LegacyPointer;
    }

    return Address;
}

static
BOOLEAN
HalpAcpiGasValid(
    _In_ const GEN_ADDR *Address)
{
    if (!Address)
    {
        return FALSE;
    }

    if (Address->Address.QuadPart == 0)
    {
        return FALSE;
    }

    if ((Address->AddressSpaceID != ACPI_GAS_SYSTEM_MEMORY) &&
        (Address->AddressSpaceID != ACPI_GAS_SYSTEM_IO))
    {
        return FALSE;
    }

    return TRUE;
}

static
VOID
HalpAcpiInitializePmTimerBlock(
    _In_ PFADT Fadt)
{
    UCHAR DefaultWidth;

    DefaultWidth = HalpFixedAcpiDescTable.pm_tmr_len ?
                   (UCHAR)(HalpFixedAcpiDescTable.pm_tmr_len * 8) :
                   24;

    HalpPmTimerBlockValid = HalpAcpiInitializeGenericBlock(&Fadt->x_pm_tmr_blk,
                                                           Fadt->pm_tmr_blk_io_port,
                                                           HalpFixedAcpiDescTable.pm_tmr_len,
                                                           DefaultWidth,
                                                           &HalpPmTimerBlock);

    if (!HalpPmTimerBlockValid)
    {
        RtlZeroMemory(&HalpPmTimerBlock, sizeof(HalpPmTimerBlock));
    }
}

static
BOOLEAN
HalpAcpiInitializeGenericBlock(
    _In_opt_ const GEN_ADDR *Extended,
    _In_ ULONG LegacyAddress,
    _In_ UCHAR Length,
    _In_ UCHAR DefaultBitWidth,
    _Out_ GEN_ADDR *TargetGas)
{
    GEN_ADDR Gas;
    UCHAR BitWidth;

    RtlZeroMemory(&Gas, sizeof(Gas));

    BitWidth = Length ? (UCHAR)(Length * 8) : DefaultBitWidth;
    if (BitWidth == 0)
    {
        BitWidth = DefaultBitWidth;
    }
    if (BitWidth == 0)
    {
        BitWidth = 8;
    }

    if (Extended && HalpAcpiGasValid(Extended))
    {
        Gas = *Extended;
        if (Gas.BitWidth == 0)
        {
            Gas.BitWidth = BitWidth;
        }
    }
    else if (LegacyAddress)
    {
        Gas.AddressSpaceID = ACPI_GAS_SYSTEM_IO;
        Gas.BitWidth = BitWidth;
        Gas.BitOffset = 0;
        Gas.Reserved = 0;
        Gas.Address.QuadPart = LegacyAddress;
    }
    else
    {
        RtlZeroMemory(TargetGas, sizeof(*TargetGas));
        return FALSE;
    }

    if (Gas.BitWidth > 255)
    {
        Gas.BitWidth = 255;
    }

    *TargetGas = Gas;
    return HalpAcpiGasValid(TargetGas);
}

static
VOID
HalpAcpiInitializePmIoBlocks(
    _In_ PFADT Fadt)
{
    HalpPm1EventBlockValid[0] = HalpAcpiInitializeGenericBlock(&Fadt->x_pm1a_evt_blk,
                                                               Fadt->pm1a_evt_blk_io_port,
                                                               HalpFixedAcpiDescTable.pm1_evt_len,
                                                               16,
                                                               &HalpPm1EventBlocks[0]);

    HalpPm1EventBlockValid[1] = HalpAcpiInitializeGenericBlock(&Fadt->x_pm1b_evt_blk,
                                                               Fadt->pm1b_evt_blk_io_port,
                                                               HalpFixedAcpiDescTable.pm1_evt_len,
                                                               16,
                                                               &HalpPm1EventBlocks[1]);

    HalpPm1ControlBlockValid[0] = HalpAcpiInitializeGenericBlock(&Fadt->x_pm1a_ctrl_blk,
                                                                 Fadt->pm1a_ctrl_blk_io_port,
                                                                 HalpFixedAcpiDescTable.pm1_ctrl_len,
                                                                 16,
                                                                 &HalpPm1ControlBlocks[0]);

    HalpPm1ControlBlockValid[1] = HalpAcpiInitializeGenericBlock(&Fadt->x_pm1b_ctrl_blk,
                                                                 Fadt->pm1b_ctrl_blk_io_port,
                                                                 HalpFixedAcpiDescTable.pm1_ctrl_len,
                                                                 16,
                                                                 &HalpPm1ControlBlocks[1]);

    HalpPm2ControlBlockValid = HalpAcpiInitializeGenericBlock(&Fadt->x_pm2_ctrl_blk,
                                                              Fadt->pm2_ctrl_blk_io_port,
                                                              HalpFixedAcpiDescTable.pm2_ctrl_len,
                                                              8,
                                                              &HalpPm2ControlBlock);

    HalpGeneralPurposeBlockValid[0] = HalpAcpiInitializeGenericBlock(&Fadt->x_gp0_blk,
                                                                     Fadt->gp0_blk_io_port,
                                                                     HalpFixedAcpiDescTable.gp0_blk_len,
                                                                     8,
                                                                     &HalpGeneralPurposeBlocks[0]);

    HalpGeneralPurposeBlockValid[1] = HalpAcpiInitializeGenericBlock(&Fadt->x_gp1_blk,
                                                                     Fadt->gp1_blk_io_port,
                                                                     HalpFixedAcpiDescTable.gp1_blk_len,
                                                                     8,
                                                                     &HalpGeneralPurposeBlocks[1]);
}

static
BOOLEAN
HalpAcpiReadRegister(
    _In_ const GEN_ADDR *Gas,
    _Out_ ULONG *Value)
{
    ULONG Bytes;

    if (!Gas || !Value || !HalpAcpiGasValid(Gas))
    {
        return FALSE;
    }

    Bytes = (Gas->BitWidth + 7) >> 3;
    if (!Bytes)
    {
        Bytes = 1;
    }

    switch (Gas->AddressSpaceID)
    {
        case ACPI_GAS_SYSTEM_IO:
        {
            ULONG_PTR Port = (ULONG_PTR)Gas->Address.LowPart;

            switch (Bytes)
            {
                case 1:
                    *Value = READ_PORT_UCHAR((PUCHAR)Port);
                    return TRUE;
                case 2:
                    *Value = READ_PORT_USHORT((PUSHORT)Port);
                    return TRUE;
                case 4:
                    *Value = READ_PORT_ULONG((PULONG)Port);
                    return TRUE;
                default:
                    return FALSE;
            }
        }

        case ACPI_GAS_SYSTEM_MEMORY:
        {
            PHYSICAL_ADDRESS BaseAddress;
            PFN_COUNT Pages;
            PVOID Mapping;
            ULONG Offset;
            volatile PUCHAR Pointer;

            BaseAddress.QuadPart = Gas->Address.QuadPart & ~((ULONGLONG)PAGE_SIZE - 1);
            Pages = HalpAcpiPagesForRange(Gas->Address, Bytes);
            Mapping = HalpMapPhysicalMemory64(BaseAddress, Pages);
            if (!Mapping)
            {
                return FALSE;
            }

            Offset = (ULONG)(Gas->Address.QuadPart - BaseAddress.QuadPart);
            Pointer = (volatile PUCHAR)Mapping + Offset;

            switch (Bytes)
            {
                case 1:
                    *Value = READ_REGISTER_UCHAR((PUCHAR)Pointer);
                    break;
                case 2:
                    *Value = READ_REGISTER_USHORT((PUSHORT)Pointer);
                    break;
                case 4:
                    *Value = READ_REGISTER_ULONG((PULONG)Pointer);
                    break;
                default:
                    HalpUnmapVirtualAddress(Mapping, Pages);
                    return FALSE;
            }

            HalpUnmapVirtualAddress(Mapping, Pages);
            return TRUE;
        }

        default:
            return FALSE;
    }
}

BOOLEAN
NTAPI
HalpAcpiQueryPowerButton(VOID)
{
    ULONG Index;
    ULONG Value;
    BOOLEAN Pressed = FALSE;

    /* If ACPI handles power button via a control method device, nothing to do */
    if (HalpFixedAcpiDescTable.flags & ACPI_FADT_POWER_BUTTON)
    {
        return FALSE;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(HalpPm1EventBlocks); Index++)
    {
        if (!HalpPm1EventBlockValid[Index])
        {
            continue;
        }

        if (!HalpAcpiReadRegister(&HalpPm1EventBlocks[Index], &Value))
        {
            continue;
        }

        if (Value & ACPI_PM1_STATUS_POWER_BUTTON)
        {
            HalpAcpiWriteRegister(&HalpPm1EventBlocks[Index], ACPI_PM1_STATUS_POWER_BUTTON);
            Pressed = TRUE;
        }
    }

    if (Pressed && !HalpPowerButtonShutdownInitiated)
    {
        NTSTATUS Status;

        HalpPowerButtonShutdownInitiated = TRUE;
        DPRINT1("HAL: Initiating shutdown sequence after power button press.\n");

        Status = NtShutdownSystem(ShutdownPowerOff);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("HAL: NtShutdownSystem failed with status 0x%08lx\n", Status);
            HalpPowerButtonShutdownInitiated = FALSE;
        }
    }

    return Pressed;
}

static
BOOLEAN
HalpAcpiWriteRegister(
    _In_ const GEN_ADDR *Gas,
    _In_ ULONG Value)
{
    ULONG Bytes;

    if (!Gas || !HalpAcpiGasValid(Gas))
    {
        return FALSE;
    }

    Bytes = (Gas->BitWidth + 7) >> 3;
    if (!Bytes)
    {
        Bytes = 1;
    }

    switch (Gas->AddressSpaceID)
    {
        case ACPI_GAS_SYSTEM_IO:
        {
            ULONG_PTR Port = (ULONG_PTR)Gas->Address.LowPart;

            switch (Bytes)
            {
                case 1:
                    WRITE_PORT_UCHAR((PUCHAR)Port, (UCHAR)Value);
                    return TRUE;
                case 2:
                    WRITE_PORT_USHORT((PUSHORT)Port, (USHORT)Value);
                    return TRUE;
                case 4:
                    WRITE_PORT_ULONG((PULONG)Port, Value);
                    return TRUE;
                default:
                    return FALSE;
            }
        }

        case ACPI_GAS_SYSTEM_MEMORY:
        {
            PHYSICAL_ADDRESS BaseAddress;
            PFN_COUNT Pages;
            PVOID Mapping;
            ULONG Offset;
            volatile PUCHAR Pointer;

            BaseAddress.QuadPart = Gas->Address.QuadPart & ~((ULONGLONG)PAGE_SIZE - 1);
            Pages = HalpAcpiPagesForRange(Gas->Address, Bytes);
            Mapping = HalpMapPhysicalMemory64(BaseAddress, Pages);
            if (!Mapping)
            {
                return FALSE;
            }

            Offset = (ULONG)(Gas->Address.QuadPart - BaseAddress.QuadPart);
            Pointer = (volatile PUCHAR)Mapping + Offset;

            switch (Bytes)
            {
                case 1:
                    WRITE_REGISTER_UCHAR((PUCHAR)Pointer, (UCHAR)Value);
                    break;
                case 2:
                    WRITE_REGISTER_USHORT((PUSHORT)Pointer, (USHORT)Value);
                    break;
                case 4:
                    WRITE_REGISTER_ULONG((PULONG)Pointer, Value);
                    break;
                default:
                    HalpUnmapVirtualAddress(Mapping, Pages);
                    return FALSE;
            }

            HalpUnmapVirtualAddress(Mapping, Pages);
            return TRUE;
        }

        default:
            return FALSE;
    }
}

PVOID
NTAPI
HalpAcpiGetTableFromBios(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                         IN ULONG Signature)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PXSDT Xsdt;
    PRSDT Rsdt;
    PFADT Fadt;
    PDESCRIPTION_HEADER Header = NULL;
    ULONG TableLength;
    CHAR CheckSum = 0;
    ULONG Offset;
    ULONG EntryCount, CurrentEntry;
    PCHAR CurrentByte;
    PFN_COUNT PageCount;

    /* Should not query the RSDT/XSDT by itself */
    if ((Signature == RSDT_SIGNATURE) || (Signature == XSDT_SIGNATURE)) return NULL;

    /* Special case request for DSDT, because the FADT points to it */
    if (Signature == DSDT_SIGNATURE)
    {
        /* Grab the FADT */
        Fadt = HalpAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
        if (Fadt)
        {
            /* Grab the DSDT address and assume 2 pages */
            PhysicalAddress = HalpAcpiSelectFadtPointer(Fadt->dsdt, Fadt->x_dsdt);
            if (!PhysicalAddress.QuadPart)
            {
                DPRINT1("HAL: FADT missing DSDT address.\n");
                return NULL;
            }
            /* Map it */
            if (LoaderBlock)
            {
                /* Phase 0, use HAL heap */
                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2u);
            }
            else
            {
                /* Phase 1, use Mm */
                Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, 0);
            }

            /* Fail if we couldn't map it */
            if (!Header)
            {
                DPRINT1("HAL: Failed to map ACPI table.\n");
                return NULL;
            }

            /* Validate the signature */
            if (Header->Signature != DSDT_SIGNATURE)
            {
                /* Fail and unmap */
                if (LoaderBlock)
                {
                    /* Using HAL heap */
                    HalpUnmapVirtualAddress(Header, 2);
                }
                else
                {
                    /* Using Mm */
                    MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
                }

                /* Didn't find anything */
                return NULL;
            }
        }
        else
        {
            /* Couldn't find it */
            return NULL;
        }
    }
    else
    {
        /* To find tables, we need the RSDT */
        Rsdt = HalpAcpiGetTable(LoaderBlock, RSDT_SIGNATURE);
        if (Rsdt)
        {
            /* Won't be using the XSDT */
            Xsdt = NULL;
        }
        else
        {
            /* Only other choice is to use the XSDT */
            Xsdt = HalpAcpiGetTable(LoaderBlock, XSDT_SIGNATURE);
            if (!Xsdt) return NULL;

            /* Won't be using the RSDT */
            Rsdt = NULL;
        }

        /* Smallest RSDT/XSDT is one without table entries */
        Offset = FIELD_OFFSET(RSDT, Tables);
        if (Xsdt)
        {
            /* Figure out total size of table and the offset */
            TableLength = Xsdt->Header.Length;
            if (TableLength < Offset) Offset = Xsdt->Header.Length;

            /* The entries are each 64-bits, so count them */
            EntryCount = (TableLength - Offset) / sizeof(PHYSICAL_ADDRESS);
        }
        else
        {
            /* Figure out total size of table and the offset */
            TableLength = Rsdt->Header.Length;
            if (TableLength < Offset) Offset = Rsdt->Header.Length;

            /* The entries are each 32-bits, so count them */
            EntryCount = (TableLength - Offset) / sizeof(ULONG);
        }

        /* Start at the beginning of the array and loop it */
        for (CurrentEntry = 0; CurrentEntry < EntryCount; CurrentEntry++)
        {
            /* Are we using the XSDT? */
            if (!Xsdt)
            {
                /* Read the 32-bit physical address */
                PhysicalAddress.LowPart = Rsdt->Tables[CurrentEntry];
                PhysicalAddress.HighPart = 0;
            }
            else
            {
                /* Read the 64-bit physical address */
                PhysicalAddress = Xsdt->Tables[CurrentEntry];
            }

            /* Had we already mapped a table? */
            if (Header)
            {
                /* Yes, unmap it */
                if (LoaderBlock)
                {
                    /* Using HAL heap */
                    HalpUnmapVirtualAddress(Header, 2);
                }
                else
                {
                    /* Using Mm */
                    MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
                }
            }

            /* Now map this table */
            if (!LoaderBlock)
            {
                /* Phase 1: Use HAL heap */
                Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, MmNonCached);
            }
            else
            {
                /* Phase 0: Use Mm */
                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
            }

            /* Check if we mapped it */
            if (!Header)
            {
                /* Game over */
                DPRINT1("HAL: Failed to map ACPI table.\n");
                return NULL;
            }

            /* We found it, break out */
            DPRINT("Found ACPI table %c%c%c%c at 0x%p\n",
                    Header->Signature & 0xFF,
                    (Header->Signature & 0xFF00) >> 8,
                    (Header->Signature & 0xFF0000) >> 16,
                    (Header->Signature & 0xFF000000) >> 24,
                    Header);
            if (Header->Signature == Signature) break;
        }

        /* Did we end up here back at the last entry? */
        if (CurrentEntry == EntryCount)
        {
            /* Yes, unmap the last table we processed */
            if (LoaderBlock)
            {
                /* Using HAL heap */
                HalpUnmapVirtualAddress(Header, 2);
            }
            else
            {
                /* Using Mm */
                MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
            }

            /* Didn't find anything */
            return NULL;
        }
    }

    /* Past this point, we assume something was found */
    ASSERT(Header);

    /* How many pages do we need? */
    PageCount = BYTES_TO_PAGES(Header->Length);
    if (PageCount != 2)
    {
        /* We assumed two, but this is not the case, free the current mapping */
        if (LoaderBlock)
        {
            /* Using HAL heap */
            HalpUnmapVirtualAddress(Header, 2);
        }
        else
        {
            /* Using Mm */
            MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
        }

        /* Now map this table using its correct size */
        if (!LoaderBlock)
        {
            /* Phase 1: Use HAL heap */
            Header = MmMapIoSpace(PhysicalAddress, PageCount << PAGE_SHIFT, MmNonCached);
        }
        else
        {
            /* Phase 0: Use Mm */
            Header = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
        }
    }

    /* Fail if the remapped failed */
    if (!Header) return NULL;

    /* All tables in ACPI 3.0 other than the FACP should have correct checksum */
    if ((Header->Signature != FADT_SIGNATURE) || (Header->Revision > 2))
    {
        /* Go to the end of the table */
        CheckSum = 0;
        CurrentByte = (PCHAR)Header + Header->Length;
        while (CurrentByte-- != (PCHAR)Header)
        {
            /* Add this byte */
            CheckSum += *CurrentByte;
        }

        /* The correct checksum is always 0, anything else is illegal */
        if (CheckSum)
        {
            HalpInvalidAcpiTable = Header->Signature;
            DPRINT1("Checksum failed on ACPI table %c%c%c%c\n",
                    (Signature & 0xFF),
                    (Signature & 0xFF00) >> 8,
                    (Signature & 0xFF0000) >> 16,
                    (Signature & 0xFF000000) >> 24);
        }
    }

    /* Return the table */
    return Header;
}

PVOID
NTAPI
HalpAcpiGetTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                 IN ULONG Signature)
{
    PFN_COUNT PageCount;
    PDESCRIPTION_HEADER TableAddress, BiosCopy;

    /* See if we have a cached table? */
    TableAddress = HalpAcpiGetCachedTable(Signature);
    if (!TableAddress)
    {
        /* No cache, search the BIOS */
        TableAddress = HalpAcpiGetTableFromBios(LoaderBlock, Signature);
        if (TableAddress)
        {
            /* Found it, copy it into our own memory */
            BiosCopy = HalpAcpiCopyBiosTable(LoaderBlock, TableAddress);

            /* Get the pages, and unmap the BIOS copy */
            PageCount = BYTES_TO_PAGES(TableAddress->Length);
            if (LoaderBlock)
            {
                /* Phase 0, use the HAL heap */
                HalpUnmapVirtualAddress(TableAddress, PageCount);
            }
            else
            {
                /* Phase 1, use Mm */
                MmUnmapIoSpace(TableAddress, PageCount << PAGE_SHIFT);
            }

            /* Cache the bios copy */
            TableAddress = BiosCopy;
            if (BiosCopy) HalpAcpiCacheTable(BiosCopy);
        }
    }

    /* Return the table */
    return TableAddress;
}

PVOID
NTAPI
HalAcpiGetTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                IN ULONG Signature)
{
    PDESCRIPTION_HEADER TableHeader;

    /* Is this phase0 */
    if (LoaderBlock)
    {
        /* Initialize the cache first */
        if (!NT_SUCCESS(HalpAcpiTableCacheInit(LoaderBlock))) return NULL;
    }
    else
    {
        /* Lock the cache */
        ExAcquireFastMutex(&HalpAcpiTableCacheLock);
    }

    /* Get the table */
    TableHeader = HalpAcpiGetTable(LoaderBlock, Signature);

    /* Release the lock in phase 1 */
    if (!LoaderBlock) ExReleaseFastMutex(&HalpAcpiTableCacheLock);

    /* Return the table */
    return TableHeader;
}

VOID
NTAPI
HalpNumaInitializeStaticConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_SRAT SratTable;

    /* Get the SRAT, bail out if it doesn't exist */
    SratTable = HalAcpiGetTable(LoaderBlock, SRAT_SIGNATURE);
    HalpAcpiSrat = SratTable;
    if (!SratTable) return;
}

VOID
NTAPI
HalpGetHotPlugMemoryInfo(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_SRAT SratTable;

    /* Get the SRAT, bail out if it doesn't exist */
    SratTable = HalAcpiGetTable(LoaderBlock, SRAT_SIGNATURE);
    HalpAcpiSrat = SratTable;
    if (!SratTable) return;
}

VOID
NTAPI
HalpDynamicSystemResourceConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* For this HAL, it means to get hot plug memory information */
    HalpGetHotPlugMemoryInfo(LoaderBlock);
}

VOID
NTAPI
HalpAcpiDetectMachineSpecificActions(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                                     IN PFADT DescriptionTable)
{
    /* Does this HAL specify something? */
    if (HalpAcpiTableMatchList.Flink)
    {
        /* Great, but we don't support it */
        DPRINT1("WARNING: Your HAL has specific ACPI hacks to apply!\n");
    }
}

VOID
NTAPI
HalpInitBootTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PBOOT_TABLE BootTable;

    /* Get the boot table */
    BootTable = HalAcpiGetTable(LoaderBlock, BOOT_SIGNATURE);
    HalpSimpleBootFlagTable = BootTable;

    /* Validate it */
    if ((BootTable) &&
        (BootTable->Header.Length >= sizeof(BOOT_TABLE)) &&
        (BootTable->CMOSIndex >= 9))
    {
        DPRINT1("ACPI Boot table found, but not supported!\n");
    }
    else
    {
        /* Invalid or doesn't exist, ignore it */
        HalpSimpleBootFlagTable = 0;
    }

    /* Install the end of boot handler */
//    HalEndOfBoot = HalpEndOfBoot;
}

NTSTATUS
NTAPI
HalpAcpiFindRsdtPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                       OUT PACPI_BIOS_MULTI_NODE* AcpiMultiNode)
{
    PCONFIGURATION_COMPONENT_DATA ComponentEntry;
    PCONFIGURATION_COMPONENT_DATA Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PACPI_BIOS_MULTI_NODE NodeData;
    SIZE_T NodeLength;
    PFN_COUNT PageCount;
    PVOID MappedAddress;
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Did we already do this once? */
    if (HalpAcpiMultiNode)
    {
        /* Return what we know */
        *AcpiMultiNode = HalpAcpiMultiNode;
        return STATUS_SUCCESS;
    }

    /* Assume failure */
    *AcpiMultiNode = NULL;

    /* Find the multi function adapter key */
    ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                  AdapterClass,
                                                  MultiFunctionAdapter,
                                                  0,
                                                  &Next);
    while (ComponentEntry)
    {
        /* Find the ACPI BIOS key */
        if (!_stricmp(ComponentEntry->ComponentEntry.Identifier, "ACPI BIOS"))
        {
            /* Found it */
            break;
        }

        /* Keep searching */
        Next = ComponentEntry;
        ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                      AdapterClass,
                                                      MultiFunctionAdapter,
                                                      NULL,
                                                      &Next);
    }

    /* Make sure we found it */
    if (!ComponentEntry)
    {
        DPRINT1("**** HalpAcpiFindRsdtPhase0: did NOT find RSDT\n");
        return STATUS_NOT_FOUND;
    }

    /* The configuration data is a resource list, and the BIOS node follows */
    ResourceList = ComponentEntry->ConfigurationData;
    NodeData = (PACPI_BIOS_MULTI_NODE)(ResourceList + 1);

    /* How many E820 memory entries are there? */
    NodeLength = sizeof(ACPI_BIOS_MULTI_NODE) +
                 (NodeData->Count - 1) * sizeof(ACPI_E820_ENTRY);

    /* Convert to pages */
    PageCount = (PFN_COUNT)BYTES_TO_PAGES(NodeLength);

    /* Allocate the memory */
    PhysicalAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                       0x1000000,
                                                       PageCount,
                                                       FALSE);
    if (PhysicalAddress.QuadPart)
    {
        /* Map it if the allocation worked */
        MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
    }
    else
    {
        /* Otherwise we'll have to fail */
        MappedAddress = NULL;
    }

    /* Save the multi node, bail out if we didn't find it */
    HalpAcpiMultiNode = MappedAddress;
    if (!MappedAddress) return STATUS_INSUFFICIENT_RESOURCES;

    /* Copy the multi-node data */
    RtlCopyMemory(MappedAddress, NodeData, NodeLength);
    HalpAcpiRsdpAddress = HalpAcpiMultiNode->RsdpAddress;

    /* Return the data */
    *AcpiMultiNode = HalpAcpiMultiNode;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpAcpiTableCacheInit(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_BIOS_MULTI_NODE AcpiMultiNode;
    NTSTATUS Status = STATUS_SUCCESS;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID MappedAddress;
    PFN_COUNT TablePages;
    PRSDT Rsdt;
    PLOADER_PARAMETER_EXTENSION LoaderExtension;

    /* Only initialize once */
    if (HalpAcpiTableCacheList.Flink) return Status;

    /* Setup the lock and table */
    ExInitializeFastMutex(&HalpAcpiTableCacheLock);
    InitializeListHead(&HalpAcpiTableCacheList);

    /* Find the RSDT */
    Status = HalpAcpiFindRsdtPhase0(LoaderBlock, &AcpiMultiNode);
    if (!NT_SUCCESS(Status)) return Status;

    PhysicalAddress.QuadPart = AcpiMultiNode->RsdtAddress.QuadPart;

    /* Map the RSDT */
    if (LoaderBlock)
    {
        /* Phase0: Use HAL Heap to map the RSDT, we assume it's about 2 pages */
        MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, 2);
    }
    else
    {
        /* Use an I/O map */
        MappedAddress = MmMapIoSpace(PhysicalAddress, PAGE_SIZE * 2, MmNonCached);
    }

    /* Get the RSDT */
    Rsdt = MappedAddress;
    if (!MappedAddress)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Failed to map RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Validate it */
    if ((Rsdt->Header.Signature != RSDT_SIGNATURE) &&
        (Rsdt->Header.Signature != XSDT_SIGNATURE))
    {
        /* Very bad: crash */
        HalDisplayString("Bad RSDT pointer\r\n");
        KeBugCheckEx(MISMATCHED_HAL, 4, __LINE__, 0, 0);
    }

    /* We assumed two pages -- do we need less or more? */
    TablePages = HalpAcpiPagesForRange(PhysicalAddress,
                                       Rsdt->Header.Length);
    if (TablePages != 2)
    {
        /* Are we in phase 0 or 1? */
        if (!LoaderBlock)
        {
            /* Unmap the old table, remap the new one, using Mm I/O space */
            MmUnmapIoSpace(MappedAddress, 2 * PAGE_SIZE);
            MappedAddress = MmMapIoSpace(PhysicalAddress,
                                         TablePages << PAGE_SHIFT,
                                         MmNonCached);
        }
        else
        {
            /* Unmap the old table, remap the new one, using HAL heap */
            HalpUnmapVirtualAddress(MappedAddress, 2);
            MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, TablePages);
        }

        /* Get the remapped table */
        Rsdt = MappedAddress;
        if (!MappedAddress)
        {
            /* Fail, no memory */
            DPRINT1("HAL: Couldn't remap RSDT\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /* Now take the BIOS copy and make our own local copy */
    Rsdt = HalpAcpiCopyBiosTable(LoaderBlock, &Rsdt->Header);
    if (!Rsdt)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Couldn't remap RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Get rid of the BIOS mapping */
    if (LoaderBlock)
    {
        /* Use HAL heap */
        HalpUnmapVirtualAddress(MappedAddress, TablePages);

        LoaderExtension = LoaderBlock->Extension;
    }
    else
    {
        /* Use Mm */
        MmUnmapIoSpace(MappedAddress, TablePages << PAGE_SHIFT);

        LoaderExtension = NULL;
    }

    /* Cache the RSDT */
    HalpAcpiCacheTable(&Rsdt->Header);

    /* Check for compatible loader block extension */
    if (LoaderExtension && (LoaderExtension->Size >= 0x58))
    {
        /* Compatible loader: did it provide an ACPI table override? */
        if ((LoaderExtension->AcpiTable) && (LoaderExtension->AcpiTableSize))
        {
            HalpAcpiApplyLoaderOverrides(LoaderBlock,
                                         LoaderExtension->AcpiTable,
                                         LoaderExtension->AcpiTableSize);
        }
    }

    /* Done */
    return Status;
}

VOID
NTAPI
HaliAcpiTimerInit(IN ULONG TimerPort,
                  IN ULONG TimerValExt)
{
    PHYSICAL_ADDRESS BaseAddress;
    PVOID Mapping;
    PFN_COUNT MappingPages;
    ULONG RegisterBytes;
    ULONG Offset;
    ULONG Width;
    ULONG LocalTimerPort;
    BOOLEAN TimerExtended;
    BOOLEAN AttemptMemory;

    PAGED_CODE();

    if (HalpPmTimerInitialized)
    {
        return;
    }

    HalpPmTimerMemoryMapped = FALSE;
    HalpPmTimerRegister = NULL;
    HalpPmTimerMappingBase = NULL;
    HalpPmTimerMappingPages = 0;
    HalpPmTimerPort = 0;
    HalpPmTimerBitShift = 0;

    LocalTimerPort = TimerPort;
    TimerExtended = (TimerValExt != 0);
    AttemptMemory = FALSE;

    if (!LocalTimerPort)
    {
        TimerExtended = (HalpFixedAcpiDescTable.flags & ACPI_TMR_VAL_EXT) != 0;

        if (HalpPmTimerBlockValid)
        {
            HalpPmTimerBitShift = HalpPmTimerBlock.BitOffset;

            if (HalpPmTimerBlock.AddressSpaceID == ACPI_GAS_SYSTEM_IO)
            {
                LocalTimerPort = (ULONG)HalpPmTimerBlock.Address.LowPart;
            }
            else if (HalpPmTimerBlock.AddressSpaceID == ACPI_GAS_SYSTEM_MEMORY)
            {
                AttemptMemory = TRUE;
            }
        }
    }

    if (!LocalTimerPort && !AttemptMemory)
    {
        LocalTimerPort = HalpFixedAcpiDescTable.pm_tmr_blk_io_port;
        HalpPmTimerBitShift = 0;
    }

    if (AttemptMemory)
    {
        RegisterBytes = HalpAcpiGetRegisterByteWidth(&HalpPmTimerBlock);
        BaseAddress.QuadPart = HalpPmTimerBlock.Address.QuadPart & ~((ULONGLONG)PAGE_SIZE - 1);
        Offset = (ULONG)(HalpPmTimerBlock.Address.QuadPart - BaseAddress.QuadPart);
        MappingPages = HalpAcpiPagesForRange(HalpPmTimerBlock.Address, RegisterBytes);

        Mapping = HalpMapPhysicalMemory64(BaseAddress, MappingPages);
        if (Mapping)
        {
            HalpPmTimerMappingBase = Mapping;
            HalpPmTimerMappingPages = MappingPages;
            HalpPmTimerRegister = (volatile ULONG *)((volatile PUCHAR)Mapping + Offset);
            HalpPmTimerMemoryMapped = TRUE;
            DPRINT1("ACPI Timer mapped at 0x%llx (EXT: %u)\n",
                    (unsigned long long)HalpPmTimerBlock.Address.QuadPart,
                    TimerExtended ? 1U : 0U);
        }
        else
        {
            DPRINT1("HAL: Failed to map ACPI PM timer at 0x%llx -- falling back to I/O port\n",
                    (unsigned long long)HalpPmTimerBlock.Address.QuadPart);
            LocalTimerPort = HalpFixedAcpiDescTable.pm_tmr_blk_io_port;
            HalpPmTimerBitShift = 0;
        }
    }

    if (!HalpPmTimerMemoryMapped)
    {
        if (LocalTimerPort)
        {
            HalpPmTimerPort = LocalTimerPort;
            DPRINT1("ACPI Timer port at: %lXh (EXT: %u)\n",
                    HalpPmTimerPort,
                    TimerExtended ? 1U : 0U);
        }
        else
        {
            DPRINT1("ACPI Timer resource unavailable (EXT: %u)\n", TimerExtended ? 1U : 0U);
            return;
        }
    }

    Width = TimerExtended ? 32 : 24;
    if (HalpPmTimerBlockValid && HalpPmTimerBlock.BitWidth)
    {
        Width = HalpPmTimerBlock.BitWidth;
    }
    if (Width > 32)
    {
        Width = 32;
    }
    else if (Width == 0)
    {
        Width = 24;
    }

    if (Width == 32)
    {
        HalpPmTimerMask = 0xFFFFFFFFUL;
    }
    else
    {
        HalpPmTimerMask = (1UL << Width) - 1;
    }

    HalpPmTimerInitialized = TRUE;
}

ULONG
NTAPI
HalpAcpiTimerRead(VOID)
{
    ULONG Value;

    if (!HalpPmTimerInitialized)
    {
        return 0;
    }

    if (HalpPmTimerMemoryMapped && HalpPmTimerRegister)
    {
    Value = READ_REGISTER_ULONG((PULONG)HalpPmTimerRegister);
    }
    else
    {
        Value = READ_PORT_ULONG((PULONG)(ULONG_PTR)HalpPmTimerPort);
    }

    if (HalpPmTimerBitShift)
    {
        Value >>= HalpPmTimerBitShift;
    }

    return Value & HalpPmTimerMask;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    PFADT Fadt;
    ULONG TableLength;
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Only do this once */
    if (HalpProcessedACPIPhase0) return STATUS_SUCCESS;

    /* Setup the ACPI table cache */
    Status = HalpAcpiTableCacheInit(LoaderBlock);
    if (!NT_SUCCESS(Status)) return Status;

    /* Grab the FADT */
    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (!Fadt)
    {
        /* Fail */
        DPRINT1("HAL: Didn't find the FACP\n");
        return STATUS_NOT_FOUND;
    }

    /* Assume typical size, otherwise whatever the descriptor table says */
    TableLength = sizeof(FADT);
    if (Fadt->Header.Length < sizeof(FADT)) TableLength = Fadt->Header.Length;

    /* Copy it in the HAL static buffer */
    RtlCopyMemory(&HalpFixedAcpiDescTable, Fadt, TableLength);

    /* Resolve key ACPI pointers that may reside above 4GB */
    HalpFacsPhysicalAddress = HalpAcpiSelectFadtPointer(Fadt->facs,
                                                       Fadt->x_firmware_ctrl);
    HalpAcpiInitializePmTimerBlock(Fadt);
    HalpAcpiInitializePmIoBlocks(Fadt);

    /* Anything special this HAL needs to do? */
    HalpAcpiDetectMachineSpecificActions(LoaderBlock, &HalpFixedAcpiDescTable);

    /* Get the debug table for KD */
    HalpDebugPortTable = HalAcpiGetTable(LoaderBlock, DBGP_SIGNATURE);

    /* Cache the PCI Express MMCONFIG information if present */
    {
        PHALP_ACPI_MCFG Mcfg;

        HalpAcpiMcfgTable = NULL;
        HalpAcpiMcfgAllocations = NULL;
        HalpAcpiMcfgAllocationCount = 0;

        Mcfg = HalAcpiGetTable(LoaderBlock, MCFG_SIGNATURE);
        if (Mcfg)
        {
            ULONG EntryBytes;
            ULONG EntryCount;
            ULONG Remainder;

            if (Mcfg->Header.Length < sizeof(*Mcfg))
            {
                DPRINT1("HAL: ACPI MCFG length %lu is smaller than header\n",
                        Mcfg->Header.Length);
            }
            else
            {
                EntryBytes = Mcfg->Header.Length - sizeof(*Mcfg);
                EntryCount = EntryBytes / sizeof(HALP_ACPI_MCFG_ALLOCATION);
                Remainder = EntryBytes % sizeof(HALP_ACPI_MCFG_ALLOCATION);

                if (Remainder != 0)
                {
                    DPRINT1("HAL: ACPI MCFG length %lu leaves %lu leftover bytes\n",
                            Mcfg->Header.Length,
                            Remainder);
                }

                if (EntryCount != 0)
                {
                    ULONG Index;

                    HalpAcpiMcfgTable = Mcfg;
                    HalpAcpiMcfgAllocations =
                        (PHALP_ACPI_MCFG_ALLOCATION)((PUCHAR)Mcfg + sizeof(*Mcfg));
                    HalpAcpiMcfgAllocationCount = EntryCount;

                    for (Index = 0; Index < EntryCount; ++Index)
                    {
                        const HALP_ACPI_MCFG_ALLOCATION *Allocation =
                            &HalpAcpiMcfgAllocations[Index];

                        DPRINT1("HAL: ACPI MCFG[%lu] Segment %u Buses %u-%u Base %I64x\n",
                                Index,
                                Allocation->PciSegment,
                                Allocation->StartBusNumber,
                                Allocation->EndBusNumber,
                                Allocation->BaseAddress);
                    }
                }
                else
                {
                    DPRINT1("HAL: ACPI MCFG present but contains no allocations\n");
                }
            }
        }
    }

    /* Initialize NUMA through the SRAT */
    HalpNumaInitializeStaticConfiguration(LoaderBlock);

    /* Initialize hotplug through the SRAT */
    HalpDynamicSystemResourceConfiguration(LoaderBlock);
    if (HalpAcpiSrat)
    {
        DPRINT1("Your machine has a SRAT, but NUMA/HotPlug are not supported!\n");
    }

    /* Can there be memory higher than 4GB? */
    if (HalpMaxHotPlugMemoryAddress.HighPart >= 1)
    {
        /* We'll need this for DMA later */
        HalpPhysicalMemoryMayAppearAbove4GB = TRUE;
    }

    /* Setup the ACPI timer */
    HaliAcpiTimerInit(0, 0);

    /* Do we have a low stub address yet? */
    if (!HalpLowStubPhysicalAddress.QuadPart)
    {
        /* Allocate it */
        HalpLowStubPhysicalAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                                      0x100000,
                                                                      HALP_LOW_STUB_SIZE_IN_PAGES,
                                                                      FALSE);
        if (HalpLowStubPhysicalAddress.QuadPart)
        {
            /* Map it */
            HalpLowStub = HalpMapPhysicalMemory64(HalpLowStubPhysicalAddress, HALP_LOW_STUB_SIZE_IN_PAGES);
        }
    }

    /* Grab a page for flushes */
    PhysicalAddress.QuadPart = 0x100000;
    HalpVirtAddrForFlush = HalpMapPhysicalMemory64(PhysicalAddress, 1);
    HalpPteForFlush = HalAddressToPte(HalpVirtAddrForFlush);

    /* Don't do this again */
    HalpProcessedACPIPhase0 = TRUE;

    /* Setup the boot table */
    HalpInitBootTable(LoaderBlock);

    if (HalpAcpiOverrideAttempted)
    {
        if (HalpAppliedAcpiOverrides)
        {
            DPRINT1("HAL: Applied %lu ACPI override table(s).\n",
                    HalpAppliedAcpiOverrides);
        }
        else
        {
            DPRINT1("HAL: ACPI override tables were supplied but none matched.\n");
        }
    }

    /* Log some ACPI data */
    {
        PLIST_ENTRY NextEntry;
        PCSTR AcpiVersion = NULL;

        /* Find the ACPI version (range) out */
        // v1.0+: Revision is major version.
        // v5.1+: minor_revision is minor version.
        // v6.4+: errata bits are errata version.
        switch (Fadt->Header.Revision)
        {
            case 0: // Should not happen.
                AcpiVersion = "Unknown_0";
                break;
            case 1:
                AcpiVersion = "1.0-1.0b";
                break;
            case 2: // Should not happen.
                AcpiVersion = "Unknown_2";
                break;
            case 3:
                AcpiVersion = "1.5-2.0_C";
                break;
            case 4:
                AcpiVersion = "3.0-4.0_A";
                break;
            case 5:
                if (Fadt->minor_revision == 0)
                    AcpiVersion = "5.0-5.0_B";
                else if (Fadt->minor_revision == 1)
                    AcpiVersion = "5.1-5.1_B";
                break;
            case 6:
                if (Fadt->minor_revision == 0)
                    AcpiVersion = "6.0-6.0_A";
                else if (Fadt->minor_revision == 1)
                    AcpiVersion = "6.1-6.1_A";
                else if (Fadt->minor_revision == 2)
                    AcpiVersion = "6.2-6.2_B";
                else if (Fadt->minor_revision == 3)
                    AcpiVersion = "6.3-6.3_A";
                else if ((Fadt->minor_revision & 0x0F) == 0x04)
                {
                    if ((Fadt->minor_revision & 0xF0) == 0x00)
                        AcpiVersion = "6.4";
                    else if ((Fadt->minor_revision & 0xF0) == 0x10)
                        AcpiVersion = "6.4_A";
                }
                else if (Fadt->minor_revision == 5) // v6.5_A too is documented as errata=0.
                    AcpiVersion = "6.5-6.6";
                break;
        }

        /* Print the ACPI version */
        {
            CHAR Message[256];

            Message[0] = '\0';
            HalpAppendFormatA(Message, sizeof(Message), "ACPI v");
            if (AcpiVersion == NULL)
            {
                // Unknown past values, or newer than v6.6 (documented as 6.5).
                HalpAppendFormatA(Message,
                                  sizeof(Message),
                                  "Unknown_%u_%u",
                                  Fadt->Header.Revision,
                                  Fadt->minor_revision);
            }
            else
            {
                HalpAppendFormatA(Message, sizeof(Message), "%s", AcpiVersion);
            }
            HalpAppendFormatA(Message, sizeof(Message), " detected. Tables:");

            /* List cached tables */
            for (NextEntry = HalpAcpiTableCacheList.Flink;
                 NextEntry != &HalpAcpiTableCacheList;
                 NextEntry = NextEntry->Flink)
            {
                PACPI_CACHED_TABLE CachedTable = CONTAINING_RECORD(NextEntry, ACPI_CACHED_TABLE, Links);

                HalpAppendFormatA(Message,
                                  sizeof(Message),
                                  " [%c%c%c%c]",
                                  CachedTable->Header.Signature & 0x000000FF,
                                 (CachedTable->Header.Signature & 0x0000FF00) >>  8,
                                 (CachedTable->Header.Signature & 0x00FF0000) >> 16,
                                 (CachedTable->Header.Signature & 0xFF000000) >> 24);
            }

            DPRINT1("%s\n", Message);
        }
    }

    /* Return success */
    return STATUS_SUCCESS;
}

static
VOID
HalpAppendFormatA(
    _Inout_updates_z_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_z_ PCSTR Format,
    ...)
{
    SIZE_T Length;
    INT Written;
    va_list Args;

    if (BufferSize == 0)
        return;

    Buffer[BufferSize - 1] = '\0';
    Length = strlen(Buffer);
    if (Length >= BufferSize - 1)
        return;

    va_start(Args, Format);
    Written = _vsnprintf(Buffer + Length, BufferSize - Length, Format, Args);
    va_end(Args);

    if (Written < 0)
        Buffer[BufferSize - 1] = '\0';
}

/* Helper function to show PCI BAR size */
CODE_SEG("INIT")
static VOID
ShowSize(ULONG Size, PCHAR Buffer, SIZE_T BufferSize)
{
    if (!Size) return;

    HalpAppendFormatA(Buffer, BufferSize, " [size=");
    if (Size < 1024)
    {
        HalpAppendFormatA(Buffer, BufferSize, "%d", (int)Size);
    }
    else if (Size < 1048576)
    {
        HalpAppendFormatA(Buffer, BufferSize, "%dK", (int)(Size / 1024));
    }
    else if (Size < 0x80000000)
    {
        HalpAppendFormatA(Buffer, BufferSize, "%dM", (int)(Size / 1048576));
    }
    else
    {
        HalpAppendFormatA(Buffer, BufferSize, "%d", Size);
    }
    HalpAppendFormatA(Buffer, BufferSize, "]");
}

BOOLEAN
NTAPI
HalpAcpiAccessConfigEcam(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PHALP_ACPI_MCFG_ALLOCATION Allocation;
    ULONGLONG FunctionBase;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG BytesLeft;
    ULONG CurrentOffset;
    PUCHAR BufferPtr;
    BOOLEAN ForceLegacy;

    if (HalpAcpiEcamDisabled)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL,
            "HAL: PCI Express MMCONFIG access requested after ECAM was globally disabled; using legacy configuration space.");
        return FALSE;
    }

    if (!HalpAcpiMcfgAllocations || !HalpAcpiMcfgAllocationCount)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_NO_TABLE,
            "HAL: PCI Express MMCONFIG unavailable because the ACPI MCFG table is missing or empty.");
        return FALSE;
    }

    if (BusNumber > 0xFF)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_BUS_TOO_HIGH,
            "HAL: PCI Express MMCONFIG request exceeded the maximum bus number (0xFF); using legacy configuration space.");
        return FALSE;
    }

    if (Offset >= 0x1000)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_OFFSET_TOO_HIGH,
            "HAL: PCI Express MMCONFIG offset went past the 4KB configuration window.");
        return FALSE;
    }

    if (Length == 0)
    {
        HalpAcpiRecordEcamEvent(HALP_ACPI_ECAM_COVERAGE_ZERO_LENGTH, NULL);
        return TRUE;
    }

    if ((ULONGLONG)Offset + Length > 0x1000)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_RANGE_OVERRUN,
            "HAL: PCI Express MMCONFIG request crossed a 4KB boundary; reverting to legacy configuration space.");
        return FALSE;
    }

    if (Segment == HALP_ACPI_SEGMENT_ANY)
    {
        HalpAcpiRecordEcamEvent(HALP_ACPI_ECAM_COVERAGE_SEGMENT_ANY, NULL);
    }

    Allocation = HalpAcpiGetMcfgAllocation(Segment, (UCHAR)BusNumber);
    if (!Allocation)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_NO_ALLOCATION,
            "HAL: PCI Express MMCONFIG has no allocation that covers the requested bus; using legacy configuration space.");
        return FALSE;
    }

    FunctionBase = Allocation->BaseAddress;
    FunctionBase += ((ULONGLONG)(BusNumber - Allocation->StartBusNumber) << 20);
    FunctionBase += ((ULONGLONG)Slot.u.bits.DeviceNumber << 15);
    FunctionBase += ((ULONGLONG)Slot.u.bits.FunctionNumber << 12);

    BufferPtr = Buffer;
    CurrentOffset = Offset;
    BytesLeft = Length;
    ForceLegacy = FALSE;

    while (BytesLeft)
    {
        ULONG Chunk;
        ULONG PageOffset;
        PVOID Mapping;
        PHYSICAL_ADDRESS PageBase;

        PhysicalAddress.QuadPart = FunctionBase + CurrentOffset;
        PageBase.QuadPart = PhysicalAddress.QuadPart & ~((ULONGLONG)PAGE_SIZE - 1);
        PageOffset = (ULONG)(PhysicalAddress.QuadPart - PageBase.QuadPart);
        Chunk = min(BytesLeft, PAGE_SIZE - PageOffset);

        Mapping = MmMapIoSpace(PageBase, PAGE_SIZE, MmNonCached);
        if (!Mapping)
        {
            HalpAcpiRecordEcamEvent(
                HALP_ACPI_ECAM_COVERAGE_MAP_FAILURE,
                "HAL: Failed to map a PCI Express MMCONFIG page; using legacy configuration space instead.");
            return FALSE;
        }

        if (Write)
        {
            RtlCopyMemory((PUCHAR)Mapping + PageOffset, BufferPtr, Chunk);
        }
        else
        {
            RtlCopyMemory(BufferPtr, (PUCHAR)Mapping + PageOffset, Chunk);

            if (!ForceLegacy &&
                BusNumber == 0 &&
                Slot.u.AsULONG == 0 &&
                Offset == 0 &&
                PageOffset == 0 &&
                Chunk >= sizeof(ULONG))
            {
                ULONG Value;

                Value = *(UNALIGNED PULONG)BufferPtr;
                if ((Value & 0xFFFF) == 0xFFFF)
                {
                    ForceLegacy = TRUE;
                }
            }
        }

        MmUnmapIoSpace(Mapping, PAGE_SIZE);

        BufferPtr += Chunk;
        CurrentOffset += Chunk;
        BytesLeft -= Chunk;
    }

    if (ForceLegacy)
    {
        LONG PreviousFlags;
        BOOLEAN FirstVendorFailure;

        PreviousFlags = HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_VENDOR_ALL_ONES,
            NULL);
        FirstVendorFailure = !(PreviousFlags & HALP_ACPI_ECAM_COVERAGE_VENDOR_ALL_ONES);

        if (Segment != HALP_ACPI_SEGMENT_ANY)
        {
            if (!HalpAcpiEcamDisabled)
            {
                HalpAcpiEcamDisabled = TRUE;
                HalpAcpiRecordEcamEvent(
                    HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL,
                    "HAL: MMCONFIG read of 00:00.0 vendor returned 0xFFFF; disabling ECAM access.");
            }
        }
        else if (FirstVendorFailure)
        {
            DPRINT1("HAL: MMCONFIG read of 00:00.0 vendor returned 0xFFFF during bootstrap; deferring ECAM disable.\n");
        }

        return FALSE;
    }

    HalpAcpiRecordEcamEvent(HALP_ACPI_ECAM_COVERAGE_USED, NULL);
    return TRUE;
}

PHALP_ACPI_MCFG_ALLOCATION
NTAPI
HalpAcpiGetMcfgAllocation(
    _In_ USHORT Segment,
    _In_ UCHAR BusNumber)
{
    ULONG Index;

    if (!HalpAcpiMcfgAllocations || !HalpAcpiMcfgAllocationCount)
    {
        return NULL;
    }

    for (Index = 0; Index < HalpAcpiMcfgAllocationCount; ++Index)
    {
        PHALP_ACPI_MCFG_ALLOCATION Allocation;

        Allocation = &HalpAcpiMcfgAllocations[Index];
        if ((Segment != HALP_ACPI_SEGMENT_ANY) &&
            (Allocation->PciSegment != Segment))
        {
            continue;
        }

        if (BusNumber < Allocation->StartBusNumber ||
            BusNumber > Allocation->EndBusNumber)
        {
            continue;
        }

        return Allocation;
    }

    return NULL;
}

BOOLEAN
NTAPI
HalpAcpiGetEcamAddress(
    _In_ USHORT Segment,
    _In_ UCHAR BusNumber,
    _In_ UCHAR DeviceNumber,
    _In_ UCHAR FunctionNumber,
    _In_ ULONG RegisterOffset,
    _Out_ PPHYSICAL_ADDRESS Address)
{
    PHALP_ACPI_MCFG_ALLOCATION Allocation;
    ULONGLONG Base;

    if (!Address)
    {
        return FALSE;
    }

    Allocation = HalpAcpiGetMcfgAllocation(Segment, BusNumber);
    if (!Allocation)
    {
        return FALSE;
    }

    if (DeviceNumber >= PCI_MAX_DEVICES ||
        FunctionNumber >= PCI_MAX_FUNCTION ||
        RegisterOffset >= 0x1000)
    {
        return FALSE;
    }

    Base = Allocation->BaseAddress;
    Base += ((ULONGLONG)(BusNumber - Allocation->StartBusNumber) << 20);
    Base += ((ULONGLONG)DeviceNumber << 15);
    Base += ((ULONGLONG)FunctionNumber << 12);
    Base += RegisterOffset;

    Address->QuadPart = Base;
    return TRUE;
}

/* These includes provide the PCI device/vendor lookup tables */
#define NEWLINE "\n"
#include "pci_classes.h"
#include "pci_vendors.h"

/* Enhanced PCI device enumeration with rich output */
CODE_SEG("INIT")
static VOID
HalpDebugPciDumpBusAcpi(
    IN ULONG BusNumber,
    IN PCI_SLOT_NUMBER PciSlot,
    IN PPCI_COMMON_CONFIG PciData)
{
    PCHAR p, ClassName, Boundary, SubClassName, VendorName, ProductName, SubVendorName;
    UCHAR HeaderType;
    ULONG Length;
    CHAR LookupString[16] = "";
    CHAR bSubClassName[64] = "Unknown";
    CHAR bVendorName[64] = "";
    CHAR bProductName[128] = "Unknown device";
    CHAR bSubVendorName[128] = "Unknown";
    ULONG Size, Mem, b;
    ULONG OriginalBar, PciBar;

    HeaderType = (PciData->HeaderType & ~PCI_MULTIFUNCTION);

    /* Isolate the class name */
    sprintf(LookupString, "C %02x  ", PciData->BaseClass);
    ClassName = strstr((PCHAR)ClassTable, LookupString);
    if (ClassName)
    {
        /* Isolate the subclass name */
        ClassName += strlen("C 00  ");
        Boundary = strstr(ClassName, NEWLINE "C ");
        sprintf(LookupString, NEWLINE "\t%02x  ", PciData->SubClass);
        SubClassName = strstr(ClassName, LookupString);
        if (Boundary && SubClassName > Boundary)
        {
            SubClassName = NULL;
        }
        if (!SubClassName)
        {
            SubClassName = ClassName;
        }
        else
        {
            SubClassName += strlen(NEWLINE "\t00  ");
        }
        /* Copy the subclass into our buffer */
        p = strpbrk(SubClassName, NEWLINE);
        if (p)
        {
            Length = p - SubClassName;
            Length = min(Length, sizeof(bSubClassName) - 1);
            strncpy(bSubClassName, SubClassName, Length);
            bSubClassName[Length] = '\0';
        }
    }

    /* Isolate the vendor name */
    sprintf(LookupString, NEWLINE "%04x  ", PciData->VendorID);
    VendorName = strstr((PCHAR)VendorTable, LookupString);
    if (VendorName)
    {
        /* Copy the vendor name into our buffer */
        VendorName += strlen(NEWLINE "0000  ");
        p = strpbrk(VendorName, NEWLINE);
        if (p)
        {
            Length = p - VendorName;
            Length = min(Length, sizeof(bVendorName) - 1);
            strncpy(bVendorName, VendorName, Length);
            bVendorName[Length] = '\0';
            p += strlen(NEWLINE);
            while (*p == '\t' || *p == '#')
            {
                p = strpbrk(p, NEWLINE);
                if (!p) break;
                p += strlen(NEWLINE);
            }
            Boundary = p;

            /* Isolate the product name */
            sprintf(LookupString, "\t%04x  ", PciData->DeviceID);
            ProductName = strstr(VendorName, LookupString);
            if (Boundary && ProductName >= Boundary)
            {
                ProductName = NULL;
            }
            if (ProductName)
            {
                /* Copy the product name into our buffer */
                ProductName += strlen("\t0000  ");
                p = strpbrk(ProductName, NEWLINE);
                if (p)
                {
                    Length = p - ProductName;
                    Length = min(Length, sizeof(bProductName) - 1);
                    strncpy(bProductName, ProductName, Length);
                    bProductName[Length] = '\0';
                    p += strlen(NEWLINE);
                    while ((*p == '\t' && *(p + 1) == '\t') || *p == '#')
                    {
                        p = strpbrk(p, NEWLINE);
                        if (!p) break;
                        p += strlen(NEWLINE);
                    }
                    Boundary = p;
                    SubVendorName = NULL;

                    if (HeaderType == PCI_DEVICE_TYPE)
                    {
                        /* Isolate the subvendor and subsystem name */
                        sprintf(LookupString,
                                "\t\t%04x %04x  ",
                                PciData->u.type0.SubVendorID,
                                PciData->u.type0.SubSystemID);
                        SubVendorName = strstr(ProductName, LookupString);
                        if (Boundary && SubVendorName >= Boundary)
                        {
                            SubVendorName = NULL;
                        }
                    }
                    if (SubVendorName)
                    {
                        /* Copy the subvendor name into our buffer */
                        SubVendorName += strlen("\t\t0000 0000  ");
                        p = strpbrk(SubVendorName, NEWLINE);
                        if (p)
                        {
                            Length = p - SubVendorName;
                            Length = min(Length, sizeof(bSubVendorName) - 1);
                            strncpy(bSubVendorName, SubVendorName, Length);
                            bSubVendorName[Length] = '\0';
                        }
                    }
                }
            }
        }
    }

    /* Print out the device information */
    DbgPrint("%02x:%02x.%x %s [%02x%02x]: %s %s [%04x:%04x] (rev %02x)\n",
             BusNumber,
             PciSlot.u.bits.DeviceNumber,
             PciSlot.u.bits.FunctionNumber,
             bSubClassName,
             PciData->BaseClass,
             PciData->SubClass,
             bVendorName,
             bProductName,
             PciData->VendorID,
             PciData->DeviceID,
             PciData->RevisionID);

    if (HeaderType == PCI_DEVICE_TYPE)
    {
        DbgPrint("\tSubsystem: %s [%04x:%04x]\n",
                 bSubVendorName,
                 PciData->u.type0.SubVendorID,
                 PciData->u.type0.SubSystemID);
    }

    /* Print out and decode flags */
    {
        CHAR FlagsLine[256];

        FlagsLine[0] = '\0';
        HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), "\tFlags:");
        if (PciData->Command & PCI_ENABLE_BUS_MASTER) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " bus master,");
        if (PciData->Status & PCI_STATUS_66MHZ_CAPABLE) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " 66MHz,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x000) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " fast devsel,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x200) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " medium devsel,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x400) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " slow devsel,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x600) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " unknown devsel,");
        HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " latency %d", PciData->LatencyTimer);
        if (PciData->u.type0.InterruptPin != 0 &&
            PciData->u.type0.InterruptLine != 0 &&
            PciData->u.type0.InterruptLine != 0xFF)
        {
            HalpAppendFormatA(FlagsLine,
                              sizeof(FlagsLine),
                              ", IRQ %02d",
                              PciData->u.type0.InterruptLine);
        }
        else if (PciData->u.type0.InterruptPin != 0)
        {
            HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), ", IRQ assignment required");
        }

        DbgPrint("%s\n", FlagsLine);
    }

    if (HeaderType == PCI_BRIDGE_TYPE)
    {
        CHAR BridgeLine[256];

        BridgeLine[0] = '\0';
        HalpAppendFormatA(BridgeLine, sizeof(BridgeLine), "\tBridge:");
        HalpAppendFormatA(BridgeLine,
                          sizeof(BridgeLine),
                          " primary bus %d,",
                          PciData->u.type1.PrimaryBus);
        HalpAppendFormatA(BridgeLine,
                          sizeof(BridgeLine),
                          " secondary bus %d,",
                          PciData->u.type1.SecondaryBus);
        HalpAppendFormatA(BridgeLine,
                          sizeof(BridgeLine),
                          " subordinate bus %d,",
                          PciData->u.type1.SubordinateBus);
        HalpAppendFormatA(BridgeLine,
                          sizeof(BridgeLine),
                          " secondary latency %d",
                          PciData->u.type1.SecondaryLatency);
        DbgPrint("%s\n", BridgeLine);
    }

    /* Scan and display BARs (Base Address Registers) */
    Size = 0;
    for (b = 0; b < (HeaderType == PCI_DEVICE_TYPE ? PCI_TYPE0_ADDRESSES : PCI_TYPE1_ADDRESSES); b++)
    {
        /* Check for a BAR */
        if (HeaderType != PCI_CARDBUS_BRIDGE_TYPE)
            Mem = PciData->u.type0.BaseAddresses[b];
        else
            Mem = 0;
        if (Mem)
        {
            /* Save original BAR value */
            OriginalBar = Mem;
            PciBar = 0xFFFFFFFF;

            /* Write all 1s to determine size */
            HalpPhase0SetPciDataByOffset(BusNumber,
                                         PciSlot,
                                         &PciBar,
                                         FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[b]),
                                         sizeof(ULONG));
            /* Read back the value */
            HalpPhase0GetPciDataByOffset(BusNumber,
                                         PciSlot,
                                         &PciBar,
                                         FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[b]),
                                         sizeof(ULONG));
            /* Restore original value */
            HalpPhase0SetPciDataByOffset(BusNumber,
                                         PciSlot,
                                         &OriginalBar,
                                         FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[b]),
                                         sizeof(ULONG));

            /* Decode the address type */
            if (PciBar & PCI_ADDRESS_IO_SPACE)
            {
                /* Guess the I/O size */
                Size = 1 << 2;
                while (!(PciBar & Size) && (Size)) Size <<= 1;

                /* Print I/O BAR info */
                CHAR BarLine[256];

                BarLine[0] = '\0';
                HalpAppendFormatA(BarLine,
                                  sizeof(BarLine),
                                  "\tI/O ports at %04lx",
                                  Mem & PCI_ADDRESS_IO_ADDRESS_MASK);
                ShowSize(Size, BarLine, sizeof(BarLine));
                DbgPrint("%s\n", BarLine);
            }
            else
            {
                /* Guess the memory size */
                Size = 1 << 4;
                while (!(PciBar & Size) && (Size)) Size <<= 1;

                /* Print Memory BAR info */
                {
                    CHAR BarLine[256];

                    BarLine[0] = '\0';
                    HalpAppendFormatA(BarLine,
                                      sizeof(BarLine),
                                      "\tMemory at %08lx (%d-bit, %sprefetchable)",
                                      Mem & PCI_ADDRESS_MEMORY_ADDRESS_MASK,
                                      (Mem & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_32BIT ? 32 : 64,
                                      (Mem & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? "" : "non-");
                    ShowSize(Size, BarLine, sizeof(BarLine));
                    DbgPrint("%s\n", BarLine);
                }
            }
        }
    }
}

CODE_SEG("INIT")
static
VOID
HalpAcpiEnumeratePciBusDebug(VOID)
{
    PCI_COMMON_CONFIG PciConfig;
    PCI_SLOT_NUMBER PciSlot;
    ULONG BusNumber, DeviceNumber, FunctionNumber;
    ULONG VendorId;

    /* Setup the PCI stub support */
    HalpInitializePciStubs();

    /* Set the NMI crash flag */
    HalpGetNMICrashFlag();

    /* Print PCI bus enumeration header */
    DbgPrint("\n====== PCI BUS HARDWARE DETECTION (ACPI HAL) =======\n\n");

    /* Enumerate all PCI buses */
    for (BusNumber = 0; BusNumber < 256; BusNumber++)
    {
        /* Try to read from bus - if it fails, no more buses */
        PciSlot.u.AsULONG = 0;
        HalpPhase0GetPciDataByOffset(BusNumber,
                                     PciSlot,
                                     &VendorId,
                                     0,
                                     sizeof(VendorId));

        /* Check if this bus exists */
        if (VendorId == 0xFFFFFFFF || VendorId == 0)
        {
            /* No device on slot 0, likely no bus */
            if (BusNumber == 0)
            {
                /* Bus 0 must exist */
                DbgPrint("ERROR: Cannot detect PCI Bus 0!\n");
            }
            break;
        }

        /* Enumerate all devices on this bus */
        for (DeviceNumber = 0; DeviceNumber < 32; DeviceNumber++)
        {
            /* Enumerate all functions on this device */
            for (FunctionNumber = 0; FunctionNumber < 8; FunctionNumber++)
            {
                /* Build the PCI slot */
                PciSlot.u.AsULONG = 0;
                PciSlot.u.bits.DeviceNumber = DeviceNumber;
                PciSlot.u.bits.FunctionNumber = FunctionNumber;

                /* Read the vendor ID */
                HalpPhase0GetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &VendorId,
                                             0,
                                             sizeof(VendorId));

                /* Check if device exists */
                if (VendorId == 0xFFFFFFFF || VendorId == 0)
                    continue;

                /* Read full configuration */
                HalpPhase0GetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &PciConfig,
                                             0,
                                             sizeof(PCI_COMMON_CONFIG));

                /* Use the enhanced debug output function */
                HalpDebugPciDumpBusAcpi(BusNumber, PciSlot, &PciConfig);

                /* For function 0, check if this is a multi-function device */
                if (FunctionNumber == 0 && !(PciConfig.HeaderType & PCI_MULTIFUNCTION))
                {
                    /* Single function device, skip other functions */
                    break;
                }
            }
        }
    }

    HalpPciLogEcamCoverage();
    DbgPrint("\n====== END PCI BUS DETECTION =======\n\n");
}

VOID
NTAPI
HalpInitNonBusHandler(VOID)
{
    /* These should be written by the PCI driver later, but we give defaults */
    HalPciTranslateBusAddress = HalpTranslateBusAddress;
    HalPciAssignSlotResources = HalpAssignSlotResources;
    HalFindBusAddressTranslation = HalpFindBusAddressTranslation;
}

CODE_SEG("INIT")
VOID
NTAPI
HalpInitBusHandlers(VOID)
{
    /* On ACPI, we only have a fake PCI bus to worry about */
    HalpInitNonBusHandler();
}

CODE_SEG("INIT")
VOID
NTAPI
HalpBuildAddressMap(VOID)
{
    /* ACPI is magic baby */
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpGetDebugPortTable(VOID)
{
    return ((HalpDebugPortTable) &&
            (HalpDebugPortTable->BaseAddress.AddressSpaceID == 1));
}

CODE_SEG("INIT")
ULONG
NTAPI
HalpIs16BitPortDecodeSupported(VOID)
{
    /* All ACPI systems are at least "EISA" so they support this */
    return CM_RESOURCE_PORT_16_BIT_DECODE;
}

VOID
NTAPI
HalpAcpiDetectResourceListSize(OUT PULONG ListSize)
{
    PAGED_CODE();

    /* One element if there is a SCI */
    *ListSize = HalpFixedAcpiDescTable.sci_int_vector ? 1: 0;
}

NTSTATUS
NTAPI
HalpBuildAcpiResourceList(IN PIO_RESOURCE_REQUIREMENTS_LIST ResourceList)
{
    ULONG Interrupt;
    PAGED_CODE();
    ASSERT(ResourceList != NULL);

    /* Initialize the list */
    ResourceList->BusNumber = -1;
    ResourceList->AlternativeLists = 1;
    ResourceList->InterfaceType = PNPBus;
    ResourceList->List[0].Version = 1;
    ResourceList->List[0].Revision = 1;
    ResourceList->List[0].Count = 0;

    /* Is there a SCI? */
    if (HalpFixedAcpiDescTable.sci_int_vector)
    {
        /* Fill out the entry for it */
        ResourceList->List[0].Descriptors[0].Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        ResourceList->List[0].Descriptors[0].Type = CmResourceTypeInterrupt;
        ResourceList->List[0].Descriptors[0].ShareDisposition = CmResourceShareShared;

        /* Get the interrupt number */
        Interrupt = HalpPicVectorRedirect[HalpFixedAcpiDescTable.sci_int_vector];
        ResourceList->List[0].Descriptors[0].u.Interrupt.MinimumVector = Interrupt;
        ResourceList->List[0].Descriptors[0].u.Interrupt.MaximumVector = Interrupt;

        /* One more */
        ++ResourceList->List[0].Count;
    }

    /* All good */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpQueryAcpiResourceRequirements(OUT PIO_RESOURCE_REQUIREMENTS_LIST *Requirements)
{
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    ULONG Count, ListSize;
    NTSTATUS Status;

    PAGED_CODE();

    /* Get ACPI resources */
    HalpAcpiDetectResourceListSize(&Count);
    DPRINT("Resource count: %lu\n", Count);

    /* Compute size of the list and allocate it */
    ListSize = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
               (Count * sizeof(IO_RESOURCE_DESCRIPTOR));
    DPRINT("Resource list size: %lu\n", ListSize);
    RequirementsList = ExAllocatePoolWithTag(PagedPool, ListSize, TAG_HAL);
    if (RequirementsList)
    {
        /* Initialize it */
        RtlZeroMemory(RequirementsList, ListSize);
        RequirementsList->ListSize = ListSize;

        /* Build it */
        Status = HalpBuildAcpiResourceList(RequirementsList);
        if (NT_SUCCESS(Status))
        {
            /* It worked, return it */
            *Requirements = RequirementsList;

            /* Validate the list */
            ASSERT(RequirementsList->List[0].Count == Count);
        }
        else
        {
            /* Fail */
            ExFreePoolWithTag(RequirementsList, TAG_HAL);
            Status = STATUS_NO_SUCH_DEVICE;
        }
    }
    else
    {
        /* Not enough memory */
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Return the status */
    return Status;
}

/*
 * @implemented
 */
CODE_SEG("INIT")
VOID
NTAPI
HalReportResourceUsage(VOID)
{
    INTERFACE_TYPE InterfaceType;
    UNICODE_STRING HalString;

    /* FIXME: Initialize DMA 64-bit support */

    /* FIXME: Initialize MCA bus */

    /* Initialize PCI bus. */
    HalpAcpiEnumeratePciBusDebug();

    /* What kind of bus is this? */
    switch (HalpBusType)
    {
        /* ISA Machine */
        case MACHINE_TYPE_ISA:
            InterfaceType = Isa;
            break;

        /* EISA Machine */
        case MACHINE_TYPE_EISA:
            InterfaceType = Eisa;
            break;

        /* MCA Machine */
        case MACHINE_TYPE_MCA:
            InterfaceType = MicroChannel;
            break;

        /* Unknown */
        default:
            InterfaceType = Internal;
            break;
    }

    /* Build HAL usage */
    RtlInitUnicodeString(&HalString, HalName);
    HalpReportResourceUsage(&HalString, InterfaceType);

    /* Setup PCI debugging and Hibernation */
    HalpRegisterPciDebuggingDeviceInfo();
}

BOOLEAN
HalpQueryAcpiRootPointer(
    _Out_ PPHYSICAL_ADDRESS Address)
{
    if (Address)
    {
        *Address = HalpAcpiRsdpAddress;
    }

    return (HalpAcpiRsdpAddress.QuadPart != 0);
}

/* EOF */
