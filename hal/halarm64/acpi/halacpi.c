/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm64/acpi/halacpi.c
 * PURPOSE:         HAL ACPI Code
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <halacpi_arm64.h>
#include <halpcie.h>
#include <ntifs.h>
#include <ndk/extypes.h>
#include <ndk/kefuncs.h>
#include <reactos/hal/acpi_cstate.h>
#include <stdarg.h>
#include <ntstrsafe.h>
#define NDEBUG
#include <debug.h>

extern VOID HalpPciLogEcamCoverage(VOID);

NTSYSAPI NTSTATUS NTAPI NtShutdownSystem(_In_ SHUTDOWN_ACTION Action);

#define HALP_ARM64_KSEG0_BASE 0xFFFF800000000000ULL
#define HALP_ARM64_PHYS_MAP_BASE 0xFFFFFC0000000000ULL
#define HALP_ARM64_PHYS_ADDR_MASK 0x0000FFFFFFFFFFFFULL

/* GLOBALS ********************************************************************/

PHALP_ACPI_MCFG_ALLOCATION HalpAcpiMcfgAllocations;
ULONG HalpAcpiMcfgAllocationCount;
#define HALP_ACPI_MAX_MCFG_ALLOCATIONS 32
static HALP_ACPI_MCFG_ALLOCATION HalpAcpiMcfgAllocationStorage[HALP_ACPI_MAX_MCFG_ALLOCATIONS];
volatile LONG HalpAcpiEcamCoverageFlags;

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

static
BOOLEAN
HalpPciVendorIdLooksSane(
    _In_ ULONG VendorDword,
    _In_ USHORT VendorWord)
{
    USHORT Candidate;

    Candidate = (USHORT)(VendorDword & 0xFFFF);
    if ((VendorWord != 0) && (VendorWord != PCI_INVALID_VENDORID))
    {
        Candidate = VendorWord;
    }

    if ((Candidate == 0) || (Candidate == PCI_INVALID_VENDORID))
    {
        return FALSE;
    }

    if ((Candidate & 0xFF00) == 0xFF00)
    {
        return FALSE;
    }

    return TRUE;
}

static __inline ULONGLONG
HalpAcpiEcamOffsetInAllocation(
    _In_ const HALP_ACPI_MCFG_ALLOCATION *Allocation,
    _In_ UCHAR BusNumber,
    _In_ UCHAR DeviceNumber,
    _In_ UCHAR FunctionNumber,
    _In_ ULONG RegisterOffset)
{
    ULONGLONG Offset;

    Offset = ((ULONGLONG)(BusNumber - Allocation->StartBusNumber) << 20);
    Offset += ((ULONGLONG)DeviceNumber << 15);
    Offset += ((ULONGLONG)FunctionNumber << 12);
    Offset += RegisterOffset;

    return Offset;
}

static
VOID
HalpAcpiReadEcamBytes(
    _In_ const HALP_ACPI_MCFG_ALLOCATION *Allocation,
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PVOID tempMapping;
    PUCHAR deviceBase;
    ULONGLONG deviceOffset;
    ULONGLONG physAddr;
    PHYSICAL_ADDRESS phys;
    ULONG currentOffset;
    ULONG remaining;
    PUCHAR out;

    deviceOffset = HalpAcpiEcamOffsetInAllocation(Allocation,
                                                  (UCHAR)BusNumber,
                                                  Slot.u.bits.DeviceNumber,
                                                  Slot.u.bits.FunctionNumber,
                                                  0);

    physAddr = Allocation->BaseAddress + deviceOffset;
    phys.QuadPart = physAddr & ~((ULONGLONG)PAGE_SIZE - 1);

    tempMapping = MmMapIoSpace(phys, PAGE_SIZE, MmNonCached);
    if (!tempMapping)
    {
        RtlFillMemory(Buffer, Length, 0xFF);
        return;
    }

    deviceBase = (PUCHAR)tempMapping + (ULONG)(physAddr - phys.QuadPart);

    currentOffset = Offset;
    remaining = Length;
    out = Buffer;

    while (remaining)
    {
        ULONG alignedOffset;
        ULONG byteInDword;
        ULONG toCopy;
        ULONG dword;
        volatile ULONG *reg;

        alignedOffset = currentOffset & ~3u;
        byteInDword = currentOffset & 3u;
        toCopy = 4 - byteInDword;
        if (toCopy > remaining)
        {
            toCopy = remaining;
        }

        reg = (volatile ULONG *)(deviceBase + alignedOffset);
        dword = READ_REGISTER_ULONG((PULONG)reg);

        RtlCopyMemory(out, ((PUCHAR)&dword) + byteInDword, toCopy);

        currentOffset += toCopy;
        out += toCopy;
        remaining -= toCopy;
    }

    MmUnmapIoSpace(tempMapping, PAGE_SIZE);
}

static
VOID
HalpAcpiWriteEcamBytes(
    _In_ const HALP_ACPI_MCFG_ALLOCATION *Allocation,
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PVOID tempMapping;
    PUCHAR deviceBase;
    ULONGLONG deviceOffset;
    ULONGLONG physAddr;
    PHYSICAL_ADDRESS phys;
    ULONG currentOffset;
    ULONG remaining;
    PUCHAR in;

    deviceOffset = HalpAcpiEcamOffsetInAllocation(Allocation,
                                                  (UCHAR)BusNumber,
                                                  Slot.u.bits.DeviceNumber,
                                                  Slot.u.bits.FunctionNumber,
                                                  0);

    physAddr = Allocation->BaseAddress + deviceOffset;
    phys.QuadPart = physAddr & ~((ULONGLONG)PAGE_SIZE - 1);

    tempMapping = MmMapIoSpace(phys, PAGE_SIZE, MmNonCached);
    if (!tempMapping)
    {
        return;
    }

    deviceBase = (PUCHAR)tempMapping + (ULONG)(physAddr - phys.QuadPart);

    currentOffset = Offset;
    remaining = Length;
    in = Buffer;

    while (remaining)
    {
        ULONG alignedOffset;
        ULONG byteInDword;
        ULONG toWrite;
        volatile ULONG *reg;
        ULONG dword;

        alignedOffset = currentOffset & ~3u;
        byteInDword = currentOffset & 3u;
        toWrite = 4 - byteInDword;
        if (toWrite > remaining)
        {
            toWrite = remaining;
        }

        reg = (volatile ULONG *)(deviceBase + alignedOffset);

        if ((toWrite == 4) && (byteInDword == 0))
        {
            RtlCopyMemory(&dword, in, sizeof(ULONG));
            WRITE_REGISTER_ULONG((PULONG)reg, dword);
        }
        else
        {
            dword = READ_REGISTER_ULONG((PULONG)reg);
            RtlCopyMemory(((PUCHAR)&dword) + byteInDword, in, toWrite);
            WRITE_REGISTER_ULONG((PULONG)reg, dword);
        }

        currentOffset += toWrite;
        in += toWrite;
        remaining -= toWrite;
    }

    MmUnmapIoSpace(tempMapping, PAGE_SIZE);
}

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
GEN_ADDR HalpPm1EventBlocks[2];
GEN_ADDR HalpPm1ControlBlocks[2];
GEN_ADDR HalpPm2ControlBlock;
GEN_ADDR HalpGeneralPurposeBlocks[2];
BOOLEAN HalpPm1EventBlockValid[2];
BOOLEAN HalpPm1ControlBlockValid[2];
BOOLEAN HalpPm2ControlBlockValid;
BOOLEAN HalpGeneralPurposeBlockValid[2];
BOOLEAN HalpPowerButtonShutdownInitiated;

static
VOID
HalpAppendFormatA(
    _Inout_updates_z_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_z_ PCSTR Format,
    ...);

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

BOOLEAN
HalpAcpiReadRegister(
    _In_ const GEN_ADDR *Gas,
    _Out_ ULONG *Value);

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
PWCHAR HalHardwareIdString = L"ACPIARM64";
PWCHAR HalName = L"ACPI Compatible Eisa/Isa HAL";

/* PRIVATE FUNCTIONS **********************************************************/

PVOID
NTAPI
HalpAcpiMapCachedTable(IN PHYSICAL_ADDRESS PhysicalAddress,
                       IN PFN_COUNT PageCount)
{
    PVOID CachedTable;

    CachedTable = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);

    /*
     * Keep Phase 0 ACPI cache nodes in the stable private physical
     * alias. FFFF8000... is the public system range, not a direct map.
     */
    if ((CachedTable != NULL) &&
        ((ULONG_PTR)CachedTable < HALP_ARM64_KSEG0_BASE))
    {
        CachedTable = (PVOID)(ULONG_PTR)(HALP_ARM64_PHYS_MAP_BASE |
                                         (PhysicalAddress.QuadPart & HALP_ARM64_PHYS_ADDR_MASK));
    }

    return CachedTable;
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

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    PFADT Fadt;
    ULONG TableLength;

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

                    PHALP_ACPI_MCFG_ALLOCATION SourceAllocations;
                    ULONG CopyCount;

                    SourceAllocations =
                        (PHALP_ACPI_MCFG_ALLOCATION)((PUCHAR)Mcfg + sizeof(*Mcfg));
                    CopyCount = min(EntryCount, HALP_ACPI_MAX_MCFG_ALLOCATIONS);

                    RtlCopyMemory(HalpAcpiMcfgAllocationStorage,
                                  SourceAllocations,
                                  CopyCount * sizeof(HALP_ACPI_MCFG_ALLOCATION));

                    HalpAcpiMcfgAllocations = HalpAcpiMcfgAllocationStorage;
                    HalpAcpiMcfgAllocationCount = CopyCount;

                    if (EntryCount > HALP_ACPI_MAX_MCFG_ALLOCATIONS)
                    {
                        DPRINT1("HAL: ACPI MCFG has %lu allocations; using first %lu\n",
                                EntryCount,
                                CopyCount);
                    }

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

    /* Ensure legacy PCI handlers are ready when MMCONFIG is unavailable */
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

    /* Don't do this again */
    HalpProcessedACPIPhase0 = TRUE;

    HalpAcpiDiscoverArm64Tables(LoaderBlock);

    /* Setup the boot table */
    HalpInitBootTable(LoaderBlock);

    /* Log some ACPI data */
    HalpAcpiLogTables(Fadt);

    /*
     * Identify the ACPI HAL flavor more accurately based on architecture.
     * This is user-visible via HalReportResourceUsage().
     */
    /* ARM64 always uses GIC (Generic Interrupt Controller) */
    HalName = L"ACPI ARM64-based PC";
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
    NTSTATUS Status;
    va_list Args;

    if (BufferSize == 0)
        return;

    Buffer[BufferSize - 1] = '\0';
    Length = strlen(Buffer);
    if (Length >= BufferSize - 1)
        return;

    va_start(Args, Format);
    Status = RtlStringCbVPrintfA(Buffer + Length, BufferSize - Length, Format, Args);
    va_end(Args);

    if (!NT_SUCCESS(Status))
        Buffer[BufferSize - 1] = '\0';
}

/* Helper function to show PCI BAR size */
CODE_SEG("INIT")
static VOID
ShowSize(ULONGLONG Size, PCHAR Buffer, SIZE_T BufferSize)
{
    if (!Size) return;

    HalpAppendFormatA(Buffer, BufferSize, " [size=");
    if (Size < 1024)
    {
        HalpAppendFormatA(Buffer, BufferSize, "%I64u", Size);
    }
    else if (Size < 1048576)
    {
        HalpAppendFormatA(Buffer, BufferSize, "%I64uK", (Size / 1024));
    }
    else if (Size < 0x80000000ULL)
    {
        HalpAppendFormatA(Buffer, BufferSize, "%I64uM", (Size / 1048576));
    }
    else
    {
        HalpAppendFormatA(Buffer, BufferSize, "%I64u", Size);
    }
    HalpAppendFormatA(Buffer, BufferSize, "]");
}

static PHALP_ACPI_MCFG_ALLOCATION
HalpAcpiFindMcfgAllocation(
    _In_ USHORT Segment,
    _In_ UCHAR BusNumber
    );

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
    ULONGLONG BusCount;
    ULONGLONG WindowLength;
    ULONGLONG AccessOffset;

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

    Allocation = HalpAcpiFindMcfgAllocation(Segment, (UCHAR)BusNumber);
    if (!Allocation)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_NO_ALLOCATION,
            "HAL: PCI Express MMCONFIG has no allocation that covers the requested bus; using legacy configuration space.");
        return FALSE;
    }

    //
    // Single-bus allocations expose only BDF xx:00.0 (firmware single-function
    // ECAM quirk); report any other device/function absent. Multi-bus unaffected.
    //
    if (Allocation->StartBusNumber == Allocation->EndBusNumber &&
        (Slot.u.bits.DeviceNumber != 0 || Slot.u.bits.FunctionNumber != 0))
    {
        if (!Write)
        {
            RtlFillMemory(Buffer, Length, 0xFF);
        }
        return TRUE;
    }

    BusCount = (ULONGLONG)(Allocation->EndBusNumber - Allocation->StartBusNumber + 1);
    WindowLength = BusCount << 20;
    if (!BusCount || !WindowLength)
    {
        HalpAcpiRecordEcamEvent(HALP_ACPI_ECAM_COVERAGE_ZERO_LENGTH, NULL);
        return FALSE;
    }

    AccessOffset = HalpAcpiEcamOffsetInAllocation(Allocation,
                                                  (UCHAR)BusNumber,
                                                  Slot.u.bits.DeviceNumber,
                                                  Slot.u.bits.FunctionNumber,
                                                  Offset);

    if ((AccessOffset + Length) > WindowLength)
    {
        HalpAcpiRecordEcamEvent(
            HALP_ACPI_ECAM_COVERAGE_RANGE_OVERRUN,
            "HAL: PCI Express MMCONFIG request exceeded the allocation window; using legacy configuration space.");
        return FALSE;
    }

    if (Write)
    {
        HalpAcpiWriteEcamBytes(Allocation,
                               BusNumber,
                               Slot,
                               Buffer,
                               Offset,
                               Length);
        HalpAcpiRecordEcamEvent(HALP_ACPI_ECAM_COVERAGE_USED, NULL);
        return TRUE;
    }

    HalpAcpiReadEcamBytes(Allocation,
                          BusNumber,
                          Slot,
                          Offset,
                          Buffer,
                          Length);

    if (Allocation->PciSegment == 0 &&
        Offset == 0 &&
        Length >= sizeof(USHORT) &&
        (UCHAR)BusNumber == Allocation->StartBusNumber &&
        Slot.u.AsULONG == 0)
    {
        USHORT Vendor = *(UNALIGNED PUSHORT)Buffer;
        if (Vendor == 0xFFFF || Vendor == 0x0000)
        {
            DPRINT1("HAL: PCI ECAM anchor vendor all-ones/zero seg=%u bus=%02lu\n",
                    Allocation->PciSegment,
                    BusNumber);
            return FALSE;
        }
    }

    HalpAcpiRecordEcamEvent(HALP_ACPI_ECAM_COVERAGE_USED, NULL);
    return TRUE;
}

static PHALP_ACPI_MCFG_ALLOCATION
HalpAcpiFindMcfgAllocation(
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

ULONG
NTAPI
HalpKdReadPciConfig(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PCI_SLOT_NUMBER Slot;

    if (Buffer == NULL || Length == 0 || BusNumber > 0xFF || Offset >= 0x1000 || (ULONGLONG)Offset + Length > 0x1000)
        return 0;

    Slot.u.AsULONG = SlotNumber;
    if (Slot.u.bits.DeviceNumber >= PCI_MAX_DEVICES || Slot.u.bits.FunctionNumber >= PCI_MAX_FUNCTION)
        return 0;

    /* FIXME: No ECAM window is mapped for use from the debugger */
    return MAXULONG;
}

/*
 * ARM64-specific PCI configuration space access functions.
 * ARM64 uses only ECAM (PCIe memory-mapped configuration) - there is no
 * legacy I/O port-based PCI configuration mechanism on ARM64.
 */

/* ARM64 stub for PCI stubs initialization - no legacy PCI on ARM64 */
CODE_SEG("INIT")
static
VOID
NTAPI
HalpInitializePciStubsArm64(VOID)
{
    /* ARM64 uses memory-mapped PCI config space only; no legacy PCI initialization needed */
    DPRINT("HAL: ARM64 PCI stubs initialized (ECAM-only mode)\n");
}

/* ARM64 stub for NMI crash flag - not applicable on ARM64 */
CODE_SEG("INIT")
static
VOID
NTAPI
HalpGetNMICrashFlagArm64(VOID)
{
    /* NMI crash flag is x86-specific; ARM64 uses different mechanisms */
}

/* ARM64-specific Phase0 PCI config read using ECAM only */
CODE_SEG("INIT")
static
ULONG
HalpPhase0GetPciDataByOffsetArm64(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER PciSlot,
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    /*
     * ARM64 has no legacy PCI configuration mechanism (CF8/CFC ports).
     * All PCI configuration space access must use a firmware-published
     * memory-mapped config-space base (MCFG or ACPI root-bridge _CBA).
     */
    if (Length == 0)
    {
        return 0;
    }

    /* The early diagnostic scan is restricted to QEMU's segment zero. */
    if (HalpArm64AccessPciConfigSpace(FALSE,
                                      0,
                                      Bus,
                                      PciSlot,
                                      Buffer,
                                      Offset,
                                      Length))
    {
        return Length;
    }

    /* ECAM access failed - fill with 0xFF (device not present) */
    RtlFillMemory(Buffer, Length, 0xFF);
    return Length;
}

/* ARM64-specific Phase0 PCI config write using ECAM only */
CODE_SEG("INIT")
static
ULONG
HalpPhase0SetPciDataByOffsetArm64(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER PciSlot,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    /*
     * ARM64 has no legacy PCI configuration mechanism (CF8/CFC ports).
     * All PCI configuration space access must use a firmware-published
     * memory-mapped config-space base (MCFG or ACPI root-bridge _CBA).
     */
    if (Length == 0)
    {
        return 0;
    }

    /* Use the same explicit segment as the diagnostic reads. */
    if (HalpArm64AccessPciConfigSpace(TRUE,
                                      0,
                                      Bus,
                                      PciSlot,
                                      Buffer,
                                      Offset,
                                      Length))
    {
        return Length;
    }

    /* ECAM access failed */
    return 0;
}

/*
 * ARM64 GSI diagnostic structures - ARM64 uses GIC for interrupt routing,
 * not the x86 GSI (Global System Interrupt) model. Provide stubs.
 */
typedef struct _HALP_PCI_GSI_DIAG
{
    BOOLEAN FromFirmware;
    USHORT Segment;
    UCHAR Bus;
    UCHAR Device;
    UCHAR Function;
    UCHAR Pin;
} HALP_PCI_GSI_DIAG, *PHALP_PCI_GSI_DIAG;

static __inline
BOOLEAN
HalpPciDescribeGsi(
    _In_ ULONG Gsi,
    _Out_ PHALP_PCI_GSI_DIAG Diag)
{
    /* ARM64 uses GIC, not GSI - always return FALSE */
    UNREFERENCED_PARAMETER(Gsi);
    if (Diag)
    {
        RtlZeroMemory(Diag, sizeof(*Diag));
    }
    return FALSE;
}

/* Macros to use ARM64-specific functions */
#define HalpInitializePciStubs() HalpInitializePciStubsArm64()
#define HalpGetNMICrashFlag() HalpGetNMICrashFlagArm64()
#define HalpPhase0GetPciDataByOffset HalpPhase0GetPciDataByOffsetArm64
#define HalpPhase0SetPciDataByOffset HalpPhase0SetPciDataByOffsetArm64

/* These includes provide the PCI device/vendor lookup tables */
#define NEWLINE "\n"
#include "pci_classes.h"
#include "pci_vendors.h"

CODE_SEG("INIT")
static
VOID
NTAPI
HalpDebugPciDumpCapabilitiesAcpi(
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER PciSlot,
    _In_ UCHAR HeaderType,
    _In_ PPCI_COMMON_CONFIG PciData)
{
    PCI_CAPABILITIES_HEADER Header;
    UCHAR CapabilityPointer;
    ULONG GuardCount;
    ULONG Visited[8];
    BOOLEAN IsPcie;

    IsPcie = FALSE;
    RtlZeroMemory(Visited, sizeof(Visited));

    if (!(PciData->Status & PCI_STATUS_CAPABILITIES_LIST))
    {
        return;
    }

    switch (HeaderType)
    {
        case PCI_DEVICE_TYPE:
            CapabilityPointer = PciData->u.type0.CapabilitiesPtr;
            break;

        case PCI_BRIDGE_TYPE:
            CapabilityPointer = PciData->u.type1.CapabilitiesPtr;
            break;

        case PCI_CARDBUS_BRIDGE_TYPE:
            CapabilityPointer = PciData->u.type2.CapabilitiesPtr;
            break;

        default:
            CapabilityPointer = 0;
            break;
    }

    GuardCount = 0;
    while (CapabilityPointer >= 0x40 &&
           GuardCount++ < 48)
    {
        ULONG Index;
        ULONG Mask;

        CapabilityPointer &= (UCHAR)~0x3;

        Index = CapabilityPointer / 4;
        Mask = 1u << (Index & 31);
        if (Visited[Index >> 5] & Mask)
        {
            break;
        }
        Visited[Index >> 5] |= Mask;

        RtlZeroMemory(&Header, sizeof(Header));
        HalpPhase0GetPciDataByOffset(BusNumber,
                                     PciSlot,
                                     &Header,
                                     CapabilityPointer,
                                     sizeof(Header));

        if (Header.CapabilityID == 0 ||
            Header.CapabilityID == 0xFF ||
            Header.Next == CapabilityPointer)
        {
            break;
        }

        switch (Header.CapabilityID)
        {
            case PCI_CAPABILITY_ID_POWER_MANAGEMENT:
            {
                USHORT Pmc;

                Pmc = 0;
                HalpPhase0GetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &Pmc,
                                             (ULONG)CapabilityPointer + 2,
                                             sizeof(Pmc));
                DbgPrint("\tCapabilities: [%02x] Power Management version %u\n",
                         CapabilityPointer,
                         (ULONG)(Pmc & 0x7));
                break;
            }

            case PCI_CAPABILITY_ID_MSI:
            {
                USHORT Control;
                UCHAR MultipleCapable;
                UCHAR MultipleEnabled;

                Control = 0;
                HalpPhase0GetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &Control,
                                             (ULONG)CapabilityPointer + 2,
                                             sizeof(Control));

                MultipleCapable = (UCHAR)((Control >> 1) & 0x7);
                MultipleEnabled = (UCHAR)((Control >> 4) & 0x7);
                if (MultipleEnabled > MultipleCapable)
                {
                    MultipleEnabled = MultipleCapable;
                }

                DbgPrint("\tCapabilities: [%02x] MSI: Enable%c Count=%lu/%lu Maskable%c 64bit%c\n",
                         CapabilityPointer,
                         (Control & 0x0001) ? '+' : '-',
                         1ul << MultipleEnabled,
                         1ul << MultipleCapable,
                         (Control & 0x0100) ? '+' : '-',
                         (Control & 0x0080) ? '+' : '-');
                break;
            }

            case PCI_CAPABILITY_ID_PCI_EXPRESS:
            {
                USHORT PcieCap;
                UCHAR DeviceType;
                UCHAR MessageNumber;

                IsPcie = TRUE;

                PcieCap = 0;
                HalpPhase0GetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &PcieCap,
                                             (ULONG)CapabilityPointer + 2,
                                             sizeof(PcieCap));

                DeviceType = (UCHAR)((PcieCap >> 4) & 0xF);
                MessageNumber = (UCHAR)((PcieCap >> 9) & 0x1F);

                DbgPrint("\tCapabilities: [%02x] Express %s, MSI %02u\n",
                         CapabilityPointer,
                         HalpPciExpressDeviceTypeName(DeviceType),
                         (ULONG)MessageNumber);
                break;
            }

            case PCI_CAPABILITY_ID_MSIX:
            {
                USHORT Control;
                ULONG TableSize;

                Control = 0;
                HalpPhase0GetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &Control,
                                             (ULONG)CapabilityPointer + 2,
                                             sizeof(Control));

                TableSize = (ULONG)(Control & 0x07FF) + 1;

                DbgPrint("\tCapabilities: [%02x] MSI-X: Enable%c Count=%lu Masked%c\n",
                         CapabilityPointer,
                         (Control & 0x8000) ? '+' : '-',
                         TableSize,
                         (Control & 0x4000) ? '+' : '-');
                break;
            }

            default:
                DbgPrint("\tCapabilities: [%02x] Capability ID %02x\n",
                         CapabilityPointer,
                         (ULONG)Header.CapabilityID);
                break;
        }

        CapabilityPointer = Header.Next;
    }

    if (!IsPcie)
    {
        return;
    }

    {
        USHORT Offset;
        ULONG ExtVisited[32];

        Offset = 0x100;
        RtlZeroMemory(ExtVisited, sizeof(ExtVisited));
        GuardCount = 0;

        while (Offset >= 0x100 &&
               Offset < 0x1000 &&
               GuardCount++ < 64)
        {
            ULONG ExtHeader;
            USHORT CapabilityId;
            USHORT NextOffset;
            ULONG Index;
            ULONG Mask;

            Index = Offset / 4;
            Mask = 1u << (Index & 31);
            if (ExtVisited[Index >> 5] & Mask)
            {
                break;
            }
            ExtVisited[Index >> 5] |= Mask;

            ExtHeader = 0;
            HalpPhase0GetPciDataByOffset(BusNumber,
                                         PciSlot,
                                         &ExtHeader,
                                         Offset,
                                         sizeof(ExtHeader));

            if (ExtHeader == 0 || ExtHeader == 0xFFFFFFFF)
            {
                break;
            }

            CapabilityId = (USHORT)(ExtHeader & 0xFFFF);
            if (CapabilityId == 0 || CapabilityId == 0xFFFF)
            {
                break;
            }

            NextOffset = (USHORT)((ExtHeader >> 20) & 0xFFF);

            switch (CapabilityId)
            {
                case 0x0001:
                    DbgPrint("\tCapabilities: [%03x] Advanced Error Reporting\n", Offset);
                    break;

                case 0x0003:
                {
                    ULONG SerialLow;
                    ULONG SerialHigh;
                    ULONGLONG Serial;
                    UCHAR B0, B1, B2, B3, B4, B5, B6, B7;

                    SerialLow = 0;
                    SerialHigh = 0;
                    HalpPhase0GetPciDataByOffset(BusNumber,
                                                 PciSlot,
                                                 &SerialLow,
                                                 Offset + 4,
                                                 sizeof(SerialLow));
                    HalpPhase0GetPciDataByOffset(BusNumber,
                                                 PciSlot,
                                                 &SerialHigh,
                                                 Offset + 8,
                                                 sizeof(SerialHigh));

                    Serial = ((ULONGLONG)SerialHigh << 32) | SerialLow;
                    B0 = (UCHAR)(Serial >> 56);
                    B1 = (UCHAR)(Serial >> 48);
                    B2 = (UCHAR)(Serial >> 40);
                    B3 = (UCHAR)(Serial >> 32);
                    B4 = (UCHAR)(Serial >> 24);
                    B5 = (UCHAR)(Serial >> 16);
                    B6 = (UCHAR)(Serial >> 8);
                    B7 = (UCHAR)(Serial);

                    DbgPrint("\tCapabilities: [%03x] Device Serial Number %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n",
                             Offset,
                             (ULONG)B0,
                             (ULONG)B1,
                             (ULONG)B2,
                             (ULONG)B3,
                             (ULONG)B4,
                             (ULONG)B5,
                             (ULONG)B6,
                             (ULONG)B7);
                    break;
                }

                default:
                    DbgPrint("\tCapabilities: [%03x] Extended Capability ID %04x\n",
                             Offset,
                             (ULONG)CapabilityId);
                    break;
            }

            if (NextOffset == 0 || NextOffset == Offset || (NextOffset & 0x3))
            {
                break;
            }

            Offset = NextOffset;
        }
    }
}

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
    BOOLEAN HasSubsystemIds;
    ULONG b;
    ULONG OriginalBar, PciBar;

    HeaderType = (PciData->HeaderType & ~PCI_MULTIFUNCTION);
    HasSubsystemIds = TRUE;

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
                if ((PciData->u.type0.SubVendorID == 0) &&
                    (PciData->u.type0.SubSystemID == 0))
                {
                    HasSubsystemIds = FALSE;
                }

                /* Isolate the subvendor and subsystem name */
                if (HasSubsystemIds)
                {
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
        if (HasSubsystemIds)
        {
            DbgPrint("\tSubsystem: %s [%04x:%04x]\n",
                     bSubVendorName,
                     PciData->u.type0.SubVendorID,
                     PciData->u.type0.SubSystemID);
        }
        else
        {
            DbgPrint("\tSubsystem: (not provided)\n");
        }
    }

    /* Print out and decode flags */
    {
        CHAR FlagsLine[256];
        HALP_PCI_GSI_DIAG GsiDiag;
        BOOLEAN HasGsi = FALSE;

        FlagsLine[0] = '\0';
        HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), "\tFlags:");
        if (PciData->Command & PCI_ENABLE_BUS_MASTER) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " bus master,");
        if (PciData->Status & PCI_STATUS_66MHZ_CAPABLE) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " 66MHz,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x000) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " fast devsel,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x200) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " medium devsel,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x400) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " slow devsel,");
        if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x600) HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " unknown devsel,");
        HalpAppendFormatA(FlagsLine, sizeof(FlagsLine), " latency %d", PciData->LatencyTimer);

        if ((HeaderType == PCI_DEVICE_TYPE) &&
            (PciData->u.type0.InterruptLine != 0) &&
            (PciData->u.type0.InterruptLine != 0xFF) &&
            HalpPciDescribeGsi((ULONG)PciData->u.type0.InterruptLine, &GsiDiag))
        {
            HasGsi = TRUE;
        }

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

        if (HasGsi)
        {
            DbgPrint("\tInterrupt: line %02u -> GSI %u%s (seg %u bus %u dev %u fn %u pin IN%c)\n",
                     PciData->u.type0.InterruptLine,
                     (ULONG)PciData->u.type0.InterruptLine,
                     GsiDiag.FromFirmware ? " from firmware" : "",
                     (ULONG)GsiDiag.Segment,
                     (ULONG)GsiDiag.Bus,
                     (ULONG)GsiDiag.Device,
                     (ULONG)GsiDiag.Function,
                     (GsiDiag.Pin >= 1 && GsiDiag.Pin <= 4) ? ('A' + GsiDiag.Pin - 1) : '?');
        }
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
    for (b = 0; b < (HeaderType == PCI_DEVICE_TYPE ? PCI_TYPE0_ADDRESSES : PCI_TYPE1_ADDRESSES); b++)
    {
        ULONGLONG BaseAddress;
        ULONGLONG Mask;
        ULONGLONG BarSize;
        BOOLEAN IsIo;
        BOOLEAN Is64Bit;
        BOOLEAN Prefetch;
        ULONG Offset;
        ULONG OriginalLow;
        ULONG OriginalHigh;
        ULONG MaskLow;
        ULONG MaskHigh;

        if (HeaderType == PCI_CARDBUS_BRIDGE_TYPE)
            break;

        OriginalLow = PciData->u.type0.BaseAddresses[b];
        if (OriginalLow == 0)
            continue;

        Offset = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[b]);
        OriginalBar = OriginalLow;

        PciBar = 0xFFFFFFFF;
        HalpPhase0SetPciDataByOffset(BusNumber, PciSlot, &PciBar, Offset, sizeof(ULONG));
        HalpPhase0GetPciDataByOffset(BusNumber, PciSlot, &PciBar, Offset, sizeof(ULONG));
        MaskLow = PciBar;

        HalpPhase0SetPciDataByOffset(BusNumber, PciSlot, &OriginalBar, Offset, sizeof(ULONG));

        IsIo = ((OriginalLow & PCI_ADDRESS_IO_SPACE) != 0);
        Is64Bit = FALSE;
        Prefetch = FALSE;
        OriginalHigh = 0;
        MaskHigh = 0;

        if (!IsIo)
        {
            Prefetch = ((OriginalLow & PCI_ADDRESS_MEMORY_PREFETCHABLE) != 0);
            if (((OriginalLow & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT) &&
                (b + 1 < (HeaderType == PCI_DEVICE_TYPE ? PCI_TYPE0_ADDRESSES : PCI_TYPE1_ADDRESSES)))
            {
                ULONG ProbeHigh = 0xFFFFFFFF;

                Is64Bit = TRUE;
                OriginalHigh = PciData->u.type0.BaseAddresses[b + 1];

                HalpPhase0SetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &ProbeHigh,
                                             Offset + sizeof(ULONG),
                                             sizeof(ULONG));
                HalpPhase0GetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &ProbeHigh,
                                             Offset + sizeof(ULONG),
                                             sizeof(ULONG));
                MaskHigh = ProbeHigh;

                HalpPhase0SetPciDataByOffset(BusNumber,
                                             PciSlot,
                                             &OriginalHigh,
                                             Offset + sizeof(ULONG),
                                             sizeof(ULONG));
            }
        }

        if (IsIo)
        {
            BaseAddress = (ULONGLONG)(OriginalLow & PCI_ADDRESS_IO_ADDRESS_MASK);
            Mask = (ULONGLONG)(MaskLow & PCI_ADDRESS_IO_ADDRESS_MASK);
            BarSize = Mask ? (Mask & (0ULL - Mask)) : 0;

            {
                CHAR BarLine[256];

                BarLine[0] = '\0';
                HalpAppendFormatA(BarLine,
                                  sizeof(BarLine),
                                  "\tI/O ports at %04lx",
                                  (ULONG)BaseAddress);
                ShowSize(BarSize, BarLine, sizeof(BarLine));
                DbgPrint("%s\n", BarLine);
            }
        }
        else
        {
            BaseAddress = (ULONGLONG)(OriginalLow & PCI_ADDRESS_MEMORY_ADDRESS_MASK);
            if (Is64Bit)
            {
                BaseAddress |= ((ULONGLONG)OriginalHigh << 32);
                Mask = ((ULONGLONG)MaskHigh << 32) | MaskLow;
            }
            else
            {
                Mask = (ULONGLONG)MaskLow;
            }

            Mask &= 0xFFFFFFFFFFFFFFF0ULL;
            BarSize = Mask ? (Mask & (0ULL - Mask)) : 0;

            {
                CHAR BarLine[256];

                BarLine[0] = '\0';
                if (Is64Bit)
                {
                    HalpAppendFormatA(BarLine,
                                      sizeof(BarLine),
                                      "\tMemory at %I64x (64-bit, %sprefetchable)",
                                      BaseAddress,
                                      Prefetch ? "" : "non-");
                }
                else
                {
                    HalpAppendFormatA(BarLine,
                                      sizeof(BarLine),
                                      "\tMemory at %08lx (32-bit, %sprefetchable)",
                                      (ULONG)BaseAddress,
                                      Prefetch ? "" : "non-");
                }
                ShowSize(BarSize, BarLine, sizeof(BarLine));
                DbgPrint("%s\n", BarLine);
            }
        }

        if (Is64Bit)
            b++;
    }

    if (HeaderType == PCI_DEVICE_TYPE && PciData->u.type0.ROMBaseAddress)
    {
        ULONGLONG RomSize;
        ULONGLONG Mask;
        ULONG Offset;
        ULONG MaskValue;
        CHAR RomLine[256];

        OriginalBar = PciData->u.type0.ROMBaseAddress;
        Offset = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.ROMBaseAddress);

        PciBar = 0xFFFFFFFE;
        HalpPhase0SetPciDataByOffset(BusNumber, PciSlot, &PciBar, Offset, sizeof(ULONG));
        MaskValue = 0;
        HalpPhase0GetPciDataByOffset(BusNumber, PciSlot, &MaskValue, Offset, sizeof(ULONG));
        HalpPhase0SetPciDataByOffset(BusNumber, PciSlot, &OriginalBar, Offset, sizeof(ULONG));

        Mask = (ULONGLONG)(MaskValue & PCI_ADDRESS_ROM_ADDRESS_MASK);
        RomSize = Mask ? (Mask & (0ULL - Mask)) : 0;

        RomLine[0] = '\0';
        HalpAppendFormatA(RomLine,
                          sizeof(RomLine),
                          "\tExpansion ROM at %08lx [%s]",
                          OriginalBar & PCI_ADDRESS_ROM_ADDRESS_MASK,
                          (OriginalBar & PCI_ROMADDRESS_ENABLED) ? "enabled" : "disabled");
        ShowSize(RomSize, RomLine, sizeof(RomLine));
        DbgPrint("%s\n", RomLine);
    }

		    HalpDebugPciDumpCapabilitiesAcpi(BusNumber, PciSlot, HeaderType, PciData);
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
    USHORT VendorWord;

    /* Setup the PCI stub support */
    HalpInitializePciStubs();

    /* Set the NMI crash flag */
    HalpGetNMICrashFlag();

    /*
     * Several ARM64 platforms describe overlapping bus-number ranges in
     * separate MCFG segments. This early diagnostic pass runs before the
     * ACPI PCI roots have selected their _SEG/_STA state, so probing an
     * arbitrary segment can manufacture devices from unrelated MMIO data.
     * QEMU's virt machine has a known, active segment-zero root. Retain the
     * detailed diagnostic scan there when MCFG describes a single window;
     * other platforms must wait for segment-aware ACPI/PnP discovery.
     */
    if (!HaliGetCachedAcpiTable(FADT_SIGNATURE, "BOCHS", "BXPC") ||
        !HalpAcpiMcfgAllocations || HalpAcpiMcfgAllocationCount != 1 ||
        HalpAcpiMcfgAllocations[0].PciSegment != 0 ||
        HalpAcpiMcfgAllocations[0].StartBusNumber != 0)
    {
        DbgPrint("HAL: Early PCI hardware dump deferred to ACPI/PnP discovery.\n");
        return;
    }

    /* Print PCI bus enumeration header */
    DbgPrint("\n====== PCI BUS HARDWARE DETECTION (ACPI HAL) =======\n\n");

    if (!HalpArm64HasPciConfigSpaceBackend())
    {
        HalpPciLogEcamCoverage();
        DbgPrint("\n====== END PCI BUS DETECTION =======\n\n");
        return;
    }

    /* Enumerate all PCI buses */
    for (BusNumber = 0; BusNumber < 256; BusNumber++)
    {
        BOOLEAN BusHadAnyDevice = FALSE;

        if (BusNumber > HalpAcpiMcfgAllocations[0].EndBusNumber)
            break;

        /* Try to read from bus - if it fails, still try all slots for bus 0 */
        PciSlot.u.AsULONG = 0;
        VendorId = 0xFFFFFFFF;
        VendorWord = 0xFFFF;
        HalpPhase0GetPciDataByOffset(BusNumber,
                                     PciSlot,
                                     &VendorId,
                                     0,
                                     sizeof(VendorId));

        if (!HalpPciVendorIdLooksSane(VendorId, (USHORT)(VendorId & 0xFFFF)))
        {
            HalpPhase0GetPciDataByOffset(BusNumber,
                                         PciSlot,
                                         &VendorWord,
                                         0,
                                         sizeof(VendorWord));

            if (HalpPciVendorIdLooksSane(0xFFFFFFFF, VendorWord))
            {
                VendorId = VendorWord;
            }
        }

        if (!HalpPciVendorIdLooksSane(VendorId, (USHORT)VendorId))
        {
            DPRINT("HAL: No PCI device responding on bus %u (vendor=0x%08lx/0x%04x); stopping scan\n",
                   BusNumber,
                   VendorId,
                   VendorWord);
            /* If not bus 0, assume no more buses and stop */
            if (BusNumber != 0)
            {
                break;
            }
            /* For bus 0, continue scanning all devices/functions to see if anyone responds */
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
		        VendorId = 0xFFFFFFFF;
		        VendorWord = 0xFFFF;
		        HalpPhase0GetPciDataByOffset(BusNumber,
		                                     PciSlot,
		                                     &VendorId,
		                                     0,
		                                     sizeof(VendorId));

			        /* Check if device exists */
			        {
			            BOOLEAN VendorSane;

				        if (!HalpPciVendorIdLooksSane(VendorId, (USHORT)(VendorId & 0xFFFF)))
				        {
				            HalpPhase0GetPciDataByOffset(BusNumber,
				                                         PciSlot,
				                                         &VendorWord,
				                                         0,
				                                         sizeof(VendorWord));
				            if (!HalpPciVendorIdLooksSane(0xFFFFFFFF, VendorWord))
				            {
				                continue;
				            }

				            VendorId = VendorWord;
				            DPRINT1("HAL: Legacy config probe resolved via WORD read for %02u:%02u.%u (vendor=0x%04x)\n",
				                    BusNumber,
				                    DeviceNumber,
				                    FunctionNumber,
				                    VendorWord);
				        }
				        else
				        {
				            /* Any valid response means the bus decodes */
				        }
				        VendorSane = HalpPciVendorIdLooksSane(VendorId, (USHORT)VendorId);
				        if (!VendorSane)
				        {
				            continue;
				        }
			        }
		        BusHadAnyDevice = TRUE;
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

        if ((BusNumber == 0) && !BusHadAnyDevice)
        {
            DbgPrint("ERROR: Cannot detect PCI Bus 0!\n");
        }
    }

    HalpPciLogEcamCoverage();
    DbgPrint("\n====== END PCI BUS DETECTION =======\n\n");
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

    HalpInitDma();

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
