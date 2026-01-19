/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Hardware specific functions
 * COPYRIGHT:   2018 Mark Jansen (mark.jansen@reactos.org)
 *              2019 Victor Perevertkin (victor.perevertkin@reactos.org)
 *              2024 ReactOS Team - Modernization for 82574L PCIe support
 *              2026 Ahmed ARIF (arif.ing@outlook.com) - NDIS 6.x port
 */

#include "e1000.h"

#include <debug.h>


static USHORT SupportedDevices[] =
{
    /* 8254x Family adapters. Not all of them are tested */
    0x1000,     // Intel 82542
    0x1001,     // Intel 82543GC Fiber
    0x1004,     // Intel 82543GC Copper
    0x1008,     // Intel 82544EI Copper
    0x1009,     // Intel 82544EI Fiber
    0x100A,     // Intel 82540EM
    0x100C,     // Intel 82544GC Copper
    0x100D,     // Intel 82544GC LOM (LAN on Motherboard)
    0x100E,     // Intel 82540EM
    0x100F,     // Intel 82545EM Copper
    0x1010,     // Intel 82546EB Copper
    0x1011,     // Intel 82545EM Fiber
    0x1012,     // Intel 82546EB Fiber
    0x1013,     // Intel 82541EI
    0x1014,     // Intel 82541EI LOM
    0x1015,     // Intel 82540EM LOM
    0x1016,     // Intel 82540EP LOM
    0x1017,     // Intel 82540EP
    0x1018,     // Intel 82541EI Mobile
    0x1019,     // Intel 82547EI
    0x101A,     // Intel 82547EI Mobile
    0x101D,     // Intel 82546EB Quad Copper
    0x101E,     // Intel 82540EP LP (Low profile)
    0x1026,     // Intel 82545GM Copper
    0x1027,     // Intel 82545GM Fiber
    0x1028,     // Intel 82545GM SerDes
    0x1075,     // Intel 82547GI
    0x1076,     // Intel 82541GI
    0x1077,     // Intel 82541GI Mobile
    0x1078,     // Intel 82541ER
    0x1079,     // Intel 82546GB Copper
    0x107A,     // Intel 82546GB Fiber
    0x107B,     // Intel 82546GB SerDes
    0x107C,     // Intel 82541PI
    0x108A,     // Intel 82546GB PCI-E
    0x1099,     // Intel 82546GB Quad Copper
    0x10B5,     // Intel 82546GB Quad Copper KSP3
    0x10D3,     // Intel 82574L Gigabit Network Connection
};

static ULONG PacketFilterToMask(ULONG PacketFilter)
{
    ULONG FilterMask = 0;

    if (PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST)
    {
        /* Multicast Promiscuous Enabled */
        FilterMask |= E1000_RCTL_MPE;
    }
    if (PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
    {
        /* Unicast Promiscuous Enabled */
        FilterMask |= E1000_RCTL_UPE;
        /* Multicast Promiscuous Enabled */
        FilterMask |= E1000_RCTL_MPE;
    }
    if (PacketFilter & NDIS_PACKET_TYPE_MAC_FRAME)
    {
        /* Pass MAC Control Frames */
        FilterMask |= E1000_RCTL_PMCF;
    }
    if (PacketFilter & NDIS_PACKET_TYPE_BROADCAST)
    {
        /* Broadcast Accept Mode */
        FilterMask |= E1000_RCTL_BAM;
    }

    return FilterMask;
}

static ULONG RcvBufAllocationSize(E1000_RCVBUF_SIZE BufSize)
{
    static ULONG PredefSizes[4] = {
        2048, 1024, 512, 256,
    };
    ULONG Size;

    Size = PredefSizes[BufSize & E1000_RCVBUF_INDEXMASK];
    if (BufSize & E1000_RCVBUF_RESERVED)
    {
        ASSERT(BufSize != 2048);
        Size *= 16;
    }
    return Size;
}

static ULONG RcvBufRegisterMask(E1000_RCVBUF_SIZE BufSize)
{
    ULONG Mask = 0;

    Mask |= BufSize & E1000_RCVBUF_INDEXMASK;
    Mask <<= E1000_RCTL_BSIZE_SHIFT;
    if (BufSize & E1000_RCVBUF_RESERVED)
        Mask |= E1000_RCTL_BSEX;

    return Mask;
}

#if 0
/* This function works, but the driver does not use PHY register access right now */
static BOOLEAN E1000ReadMdic(IN PE1000_ADAPTER Adapter, IN ULONG Address, USHORT *Result)
{
    ULONG ResultAddress;
    ULONG Mdic;
    UINT n;

    ASSERT(Address <= MAX_PHY_REG_ADDRESS)

    Mdic = (Address << E1000_MDIC_REGADD_SHIFT);
    Mdic |= (E1000_MDIC_PHYADD_GIGABIT << E1000_MDIC_PHYADD_SHIFT);
    Mdic |= E1000_MDIC_OP_READ;
    E1000WriteUlong(Adapter, E1000_REG_MDIC, Mdic);

    for (n = 0; n < MAX_PHY_READ_ATTEMPTS; n++)
    {
        NdisStallExecution(50);
        E1000ReadUlong(Adapter, E1000_REG_MDIC, &Mdic);
        if (Mdic & E1000_MDIC_R)
            break;
    }
    if (!(Mdic & E1000_MDIC_R))
    {
        NDIS_DbgPrint(MIN_TRACE, ("MDI Read incomplete\n"));
        return FALSE;
    }
    if (Mdic & E1000_MDIC_E)
    {
        NDIS_DbgPrint(MIN_TRACE, ("MDI Read error\n"));
        return FALSE;
    }

    ResultAddress = (Mdic >> E1000_MDIC_REGADD_SHIFT) & MAX_PHY_REG_ADDRESS;

    if (ResultAddress!= Address)
    {
        /* Add locking? */
        NDIS_DbgPrint(MIN_TRACE, ("MDI Read got wrong address (%d instead of %d)\n",
                                  ResultAddress, Address));
        return FALSE;
    }
    *Result = (USHORT) Mdic;
    return TRUE;
}
#endif


/*
 * E1000ReadEeprom - Read a word from EEPROM/NVM
 *
 * The 82574L and other PCIe NICs use a different EERD register format:
 *   - Legacy 8254x (PCI): DONE at bit 4, address shifted by 8
 *   - 82574L (PCIe):      DONE at bit 1, address shifted by 2
 */
static BOOLEAN E1000ReadEeprom(IN PE1000_ADAPTER Adapter, IN UCHAR Address, USHORT *Result)
{
    ULONG Value;
    ULONG DoneBit;
    ULONG AddrShift;
    UINT n;

    /* Select the correct bit positions based on device type */
    if (Adapter->IsPCIe)
    {
        /* 82574L and newer PCIe devices */
        DoneBit = E1000_EERD_DONE_PCIE;
        AddrShift = E1000_EERD_ADDR_SHIFT_PCIE;
    }
    else
    {
        /* Legacy 8254x PCI devices */
        DoneBit = E1000_EERD_DONE;
        AddrShift = E1000_EERD_ADDR_SHIFT;
    }

    /* Issue the read command */
    E1000WriteUlong(Adapter, E1000_REG_EERD, E1000_EERD_START | ((ULONG)Address << AddrShift));

    /* Poll for completion */
    for (n = 0; n < MAX_EEPROM_READ_ATTEMPTS; ++n)
    {
        NdisStallExecution(5);

        E1000ReadUlong(Adapter, E1000_REG_EERD, &Value);

        if (Value & DoneBit)
            break;
    }

    if (!(Value & DoneBit))
    {
        NDIS_DbgPrint(MIN_TRACE, ("EEPROM Read incomplete (Addr=%u, Value=0x%08x, IsPCIe=%d)\n",
                                  Address, Value, Adapter->IsPCIe));
        return FALSE;
    }

    *Result = (USHORT)(Value >> E1000_EERD_DATA_SHIFT);
    return TRUE;
}

BOOLEAN E1000ValidateNvmChecksum(IN PE1000_ADAPTER Adapter)
{
    USHORT Checksum = 0, Data;
    UINT n;

    /* 5.6.35 Checksum Word Calculation (Word 3Fh) */
    for (n = 0; n <= E1000_NVM_REG_CHECKSUM; n++)
    {
        if (!E1000ReadEeprom(Adapter, n, &Data))
        {
            return FALSE;
        }
        Checksum += Data;
    }

    if (Checksum != NVM_MAGIC_SUM)
    {
        NDIS_DbgPrint(MIN_TRACE, ("EEPROM has an invalid checksum of 0x%x\n", (ULONG)Checksum));
        return FALSE;
    }

    return TRUE;
}


/*
 * List of PCIe device IDs (82574L and similar)
 * These use the new EERD format with DONE at bit 1
 */
static USHORT PCIeDevices[] =
{
    0x10D3,     // Intel 82574L Gigabit Network Connection
    0x10F6,     // Intel 82574L Gigabit Network Connection
    0x150C,     // Intel 82583V Gigabit Network Connection
};

/*
 * E1000DetectPCIeDevice - Detect whether this is a PCIe device
 *
 * Sets Adapter->IsPCIe based on device ID. This must be called
 * early before EEPROM access since PCIe devices use different
 * EERD register bit positions.
 */
static VOID E1000DetectPCIeDevice(IN PE1000_ADAPTER Adapter)
{
    UINT n;

    /* Check if this is a known PCIe device */
    Adapter->IsPCIe = FALSE;
    for (n = 0; n < ARRAYSIZE(PCIeDevices); ++n)
    {
        if (PCIeDevices[n] == Adapter->DeviceId)
        {
            Adapter->IsPCIe = TRUE;
            break;
        }
    }

    NDIS_DbgPrint(MID_TRACE, ("Device 0x%04x: IsPCIe=%d\n",
                              Adapter->DeviceId, Adapter->IsPCIe));
}

/*
 * E1000DetectFlashNvm - Detect if NVM is flash-based
 *
 * Must be called after IoBase is mapped. Checks the EECD register
 * to determine if flash hardware is present.
 */
static VOID E1000DetectFlashNvm(IN PE1000_ADAPTER Adapter)
{
    ULONG Eecd;

    Adapter->HasFlash = FALSE;

    if (Adapter->IsPCIe && Adapter->IoBase != NULL)
    {
        E1000ReadUlong(Adapter, E1000_REG_EECD, &Eecd);
        /*
         * Check bits 15-16 of EECD to determine if flash is present
         * If both bits are set (0x3), then flash hardware is present
         */
        if (((Eecd >> E1000_EECD_FLASH_DETECTED_SHIFT) & 0x3) == 0x3)
        {
            Adapter->HasFlash = TRUE;
        }

        NDIS_DbgPrint(MID_TRACE, ("EECD=0x%08x, HasFlash=%d\n", Eecd, Adapter->HasFlash));
    }
}

/*
 * E1000ReadDeviceSerialNumber - Read device serial number from EEPROM/PCIe config
 *
 * The 82574L stores a device serial number that can be used for unique
 * device identification.
 */
static VOID E1000ReadDeviceSerialNumber(IN PE1000_ADAPTER Adapter)
{
    USHORT Word0, Word1, Word2, Word3;

    Adapter->DeviceSerialNumber.Valid = FALSE;

    if (!Adapter->IsPCIe)
        return;

    /* Try to read serial number from EEPROM */
    if (E1000ReadEeprom(Adapter, E1000_EEPROM_DSN_LOW, &Word0) &&
        E1000ReadEeprom(Adapter, E1000_EEPROM_DSN_LOW + 1, &Word1) &&
        E1000ReadEeprom(Adapter, E1000_EEPROM_DSN_MID, &Word2) &&
        E1000ReadEeprom(Adapter, E1000_EEPROM_DSN_MID + 1, &Word3))
    {
        /* Store as 8-byte array (little-endian format) */
        Adapter->DeviceSerialNumber.Serial[0] = (UCHAR)(Word0 & 0xFF);
        Adapter->DeviceSerialNumber.Serial[1] = (UCHAR)(Word0 >> 8);
        Adapter->DeviceSerialNumber.Serial[2] = (UCHAR)(Word1 & 0xFF);
        Adapter->DeviceSerialNumber.Serial[3] = (UCHAR)(Word1 >> 8);
        Adapter->DeviceSerialNumber.Serial[4] = (UCHAR)(Word2 & 0xFF);
        Adapter->DeviceSerialNumber.Serial[5] = (UCHAR)(Word2 >> 8);
        Adapter->DeviceSerialNumber.Serial[6] = (UCHAR)(Word3 & 0xFF);
        Adapter->DeviceSerialNumber.Serial[7] = (UCHAR)(Word3 >> 8);

        /* Check if all zeros (invalid) */
        if (Word0 != 0 || Word1 != 0 || Word2 != 0 || Word3 != 0)
        {
            Adapter->DeviceSerialNumber.Valid = TRUE;
            NDIS_DbgPrint(MID_TRACE, ("Device Serial: %02X%02X%02X%02X%02X%02X%02X%02X\n",
                Adapter->DeviceSerialNumber.Serial[7],
                Adapter->DeviceSerialNumber.Serial[6],
                Adapter->DeviceSerialNumber.Serial[5],
                Adapter->DeviceSerialNumber.Serial[4],
                Adapter->DeviceSerialNumber.Serial[3],
                Adapter->DeviceSerialNumber.Serial[2],
                Adapter->DeviceSerialNumber.Serial[1],
                Adapter->DeviceSerialNumber.Serial[0]));
        }
    }
}


/* ============================================================================
 * NDIS 5.1 Legacy Functions
 *
 * These functions are only used for NDIS 5.1 builds. For NDIS 6.x builds,
 * use the E1000* wrapper functions defined at the end of this file.
 * ============================================================================ */

#ifdef NDIS51_MINIPORT

BOOLEAN
NTAPI
NICRecognizeHardware(
    IN PE1000_ADAPTER Adapter)
{
    UINT n;
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_INIT_DBG(("[INIT] NICRecognizeHardware: VendorID=0x%04x DeviceID=0x%04x\n",
                    Adapter->VendorID, Adapter->DeviceID));

    if (Adapter->VendorID != HW_VENDOR_INTEL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unknown vendor: 0x%x\n", Adapter->VendorID));
        E1000_INIT_DBG(("[INIT] FAILED: Unknown vendor 0x%04x (expected 0x%04x)\n",
                        Adapter->VendorID, HW_VENDOR_INTEL));
        return FALSE;
    }

    for (n = 0; n < ARRAYSIZE(SupportedDevices); ++n)
    {
        if (SupportedDevices[n] == Adapter->DeviceID)
        {
            E1000_INIT_DBG(("[INIT] Device recognized: Intel NIC DeviceID=0x%04x (index %u)\n",
                            Adapter->DeviceID, n));
            /* Detect device type (PCIe vs PCI) early - this is needed
             * before EEPROM access since PCIe uses different EERD bits */
            E1000DetectPCIeDevice(Adapter);
            E1000_HW_DBG(("[HW] Device type: %s\n", Adapter->IsPCIe ? "PCIe" : "PCI"));
            return TRUE;
        }
    }

    NDIS_DbgPrint(MIN_TRACE, ("Unknown device: 0x%x\n", Adapter->DeviceID));
    E1000_INIT_DBG(("[INIT] FAILED: Unknown DeviceID 0x%04x not in supported list\n",
                    Adapter->DeviceID));

    return FALSE;
}

NDIS_STATUS
NTAPI
NICInitializeAdapterResources(
    IN PE1000_ADAPTER Adapter,
    IN PNDIS_RESOURCE_LIST ResourceList)
{
    UINT n;
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_INIT_DBG(("[INIT] NICInitializeAdapterResources: parsing %u resources\n",
                    ResourceList->Count));

    DbgPrint("E1000: NICInitializeAdapterResources: parsing %u resources\n", ResourceList->Count);

    for (n = 0; n < ResourceList->Count; n++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR ResourceDescriptor = ResourceList->PartialDescriptors + n;

        DbgPrint("E1000: Resource[%u]: Type=%u Flags=0x%x ShareDisposition=%u\n",
                 n, ResourceDescriptor->Type, ResourceDescriptor->Flags,
                 ResourceDescriptor->ShareDisposition);

        switch (ResourceDescriptor->Type)
        {
        case CmResourceTypePort:
            ASSERT(Adapter->IoPortAddress == 0);
            ASSERT(ResourceDescriptor->u.Port.Start.HighPart == 0);

            Adapter->IoPortAddress = ResourceDescriptor->u.Port.Start.LowPart;
            Adapter->IoPortLength = ResourceDescriptor->u.Port.Length;

            DbgPrint("E1000: I/O Port: Start=0x%x Length=%u\n",
                     Adapter->IoPortAddress, Adapter->IoPortLength);
            NDIS_DbgPrint(MID_TRACE, ("I/O port range is %p to %p\n",
                                      Adapter->IoPortAddress,
                                      Adapter->IoPortAddress + Adapter->IoPortLength));
            break;
        case CmResourceTypeInterrupt:
            /*
             * Parse interrupt resource descriptor.
             *
             * The format differs based on whether this is an MSI/MSI-X resource:
             *
             * Legacy interrupts (Flags without CM_RESOURCE_INTERRUPT_MESSAGE):
             *   u.Interrupt.Level   = IRQL/IRQ level
             *   u.Interrupt.Vector  = System interrupt vector
             *   u.Interrupt.Affinity = Processor affinity
             *
             * MSI/MSI-X interrupts (Flags with CM_RESOURCE_INTERRUPT_MESSAGE):
             *   u.MessageInterrupt.Raw.Reserved    = Reserved
             *   u.MessageInterrupt.Raw.MessageCount = Number of messages allocated
             *   u.MessageInterrupt.Raw.Vector      = First vector in the range
             *   u.MessageInterrupt.Raw.Affinity    = Processor affinity
             *
             * The Translated form (after HalGetInterruptVector) has:
             *   u.MessageInterrupt.Translated.Level    = IRQL
             *   u.MessageInterrupt.Translated.Vector   = System vector
             *   u.MessageInterrupt.Translated.Affinity = Affinity
             */

            if (ResourceDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
            {
                /*
                 * MSI/MSI-X interrupt resource.
                 * The resource manager provides us with the raw allocation.
                 */
                Adapter->InterruptVector = ResourceDescriptor->u.MessageInterrupt.Raw.Vector;
                Adapter->InterruptLevel = ResourceDescriptor->u.MessageInterrupt.Raw.Vector;
                Adapter->InterruptShared = (ResourceDescriptor->ShareDisposition == CmResourceShareShared);
                Adapter->InterruptFlags = ResourceDescriptor->Flags;
                Adapter->MsixVectorCount = ResourceDescriptor->u.MessageInterrupt.Raw.MessageCount;
                if (Adapter->MsixVectorCount == 0)
                    Adapter->MsixVectorCount = 1;

                Adapter->InterruptMode = E1000_INTERRUPT_MODE_MSI;

                DbgPrint("E1000: MSI Interrupt Resource:\n");
                DbgPrint("E1000:   Raw.Reserved=%u\n", ResourceDescriptor->u.MessageInterrupt.Raw.Reserved);
                DbgPrint("E1000:   Raw.MessageCount=%u\n", ResourceDescriptor->u.MessageInterrupt.Raw.MessageCount);
                DbgPrint("E1000:   Raw.Vector=%u (0x%x)\n",
                         ResourceDescriptor->u.MessageInterrupt.Raw.Vector,
                         ResourceDescriptor->u.MessageInterrupt.Raw.Vector);
                DbgPrint("E1000:   Raw.Affinity=0x%Ix\n",
                         (ULONG_PTR)ResourceDescriptor->u.MessageInterrupt.Raw.Affinity);
                DbgPrint("E1000:   Flags=0x%x ShareDisposition=%u\n",
                         ResourceDescriptor->Flags, ResourceDescriptor->ShareDisposition);
                DbgPrint("E1000:   Interpreted: Vector=%u Level=%u MessageCount=%u Shared=%d\n",
                         Adapter->InterruptVector, Adapter->InterruptLevel,
                         Adapter->MsixVectorCount, Adapter->InterruptShared);

                NDIS_DbgPrint(MID_TRACE, ("MSI/MSI-X interrupt resource: Vector=%u MessageCount=%u Flags=0x%x\n",
                                          Adapter->InterruptVector, Adapter->MsixVectorCount,
                                          ResourceDescriptor->Flags));
            }
            else
            {
                /*
                 * Legacy interrupt resource.
                 */
                ASSERT(Adapter->InterruptVector == 0);
                ASSERT(Adapter->InterruptLevel == 0);

                Adapter->InterruptVector = ResourceDescriptor->u.Interrupt.Vector;
                Adapter->InterruptLevel = ResourceDescriptor->u.Interrupt.Level;
                Adapter->InterruptShared = (ResourceDescriptor->ShareDisposition == CmResourceShareShared);
                Adapter->InterruptFlags = ResourceDescriptor->Flags;
                Adapter->InterruptMode = E1000_INTERRUPT_MODE_LEGACY;
                Adapter->MsixVectorCount = 0;

                DbgPrint("E1000: Legacy Interrupt Resource:\n");
                DbgPrint("E1000:   Level=%u (0x%x)\n",
                         ResourceDescriptor->u.Interrupt.Level,
                         ResourceDescriptor->u.Interrupt.Level);
                DbgPrint("E1000:   Vector=%u (0x%x)\n",
                         ResourceDescriptor->u.Interrupt.Vector,
                         ResourceDescriptor->u.Interrupt.Vector);
                DbgPrint("E1000:   Affinity=0x%Ix\n",
                         (ULONG_PTR)ResourceDescriptor->u.Interrupt.Affinity);
                DbgPrint("E1000:   Flags=0x%x ShareDisposition=%u\n",
                         ResourceDescriptor->Flags, ResourceDescriptor->ShareDisposition);

                NDIS_DbgPrint(MID_TRACE, ("Legacy interrupt resource: Vector=%u Level=%u Flags=0x%x\n",
                                          Adapter->InterruptVector, Adapter->InterruptLevel,
                                          ResourceDescriptor->Flags));
            }

            NDIS_DbgPrint(MID_TRACE, ("IRQ: Vector=%u Level=%u Shared=%d Flags=0x%x Mode=%s\n",
                                      Adapter->InterruptVector,
                                      Adapter->InterruptLevel,
                                      Adapter->InterruptShared,
                                      Adapter->InterruptFlags,
                                      (Adapter->InterruptMode == E1000_INTERRUPT_MODE_LEGACY) ? "Legacy" :
                                      (Adapter->InterruptMode == E1000_INTERRUPT_MODE_MSI) ? "MSI" : "MSI-X"));
            break;
        case CmResourceTypeMemory:
            /*
             * Internal registers and memories (including PHY).
             * The 82574L has multiple memory BARs:
             *   BAR0: 128KB - Main registers (this is what we want)
             *   BAR1: 128KB - Flash memory (for 82574L only)
             *   BAR2: 16KB - MSI-X tables (for 82574L)
             * We only want the first 128KB BAR for the main registers.
             */
            DbgPrint("E1000: Memory Resource: Start=0x%I64x Length=%u (0x%x)\n",
                     ResourceDescriptor->u.Memory.Start.QuadPart,
                     ResourceDescriptor->u.Memory.Length,
                     ResourceDescriptor->u.Memory.Length);

            if (ResourceDescriptor->u.Memory.Length == (128 * 1024) &&
                Adapter->IoAddress.QuadPart == 0)
            {
                Adapter->IoAddress.QuadPart = ResourceDescriptor->u.Memory.Start.QuadPart;
                Adapter->IoLength = ResourceDescriptor->u.Memory.Length;
                DbgPrint("E1000:   -> Main register BAR (128KB)\n");
                NDIS_DbgPrint(MID_TRACE, ("Memory range is %I64x to %I64x\n",
                                          Adapter->IoAddress.QuadPart,
                                          Adapter->IoAddress.QuadPart + Adapter->IoLength));
            }
            else if (ResourceDescriptor->u.Memory.Length == (16 * 1024))
            {
                /* MSI-X BAR - record it for later use */
                Adapter->MsixAddress.QuadPart = ResourceDescriptor->u.Memory.Start.QuadPart;
                Adapter->MsixLength = ResourceDescriptor->u.Memory.Length;
                DbgPrint("E1000:   -> MSI-X table BAR (16KB)\n");
                NDIS_DbgPrint(MID_TRACE, ("MSI-X table at %I64x (size %u)\n",
                                          Adapter->MsixAddress.QuadPart,
                                          Adapter->MsixLength));
            }
            else
            {
                DbgPrint("E1000:   -> Skipping (unknown BAR type)\n");
                NDIS_DbgPrint(MID_TRACE, ("Skipping memory region at %I64x (size %u)\n",
                                          ResourceDescriptor->u.Memory.Start.QuadPart,
                                          ResourceDescriptor->u.Memory.Length));
            }
            break;

        default:
            NDIS_DbgPrint(MIN_TRACE, ("Unrecognized resource type: 0x%x\n", ResourceDescriptor->Type));
            break;
        }
    }

    if (Adapter->IoAddress.QuadPart == 0 || Adapter->IoPortAddress == 0 || Adapter->InterruptVector == 0)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Adapter didn't receive enough resources\n"));
        E1000_INIT_DBG(("[INIT] FAILED: Missing resources - IoAddr=%I64x IoPort=0x%x IntVec=%u\n",
                        Adapter->IoAddress.QuadPart, Adapter->IoPortAddress, Adapter->InterruptVector));
        return NDIS_STATUS_RESOURCES;
    }

    E1000_INIT_DBG(("[INIT] Resources acquired: IoAddr=0x%I64x IoPort=0x%x IRQ=%u\n",
                    Adapter->IoAddress.QuadPart, Adapter->IoPortAddress, Adapter->InterruptVector));

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICAllocateIoResources(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    ULONG AllocationSize;
    UINT n;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_INIT_DBG(("[INIT] NICAllocateIoResources: Allocating hardware resources\n"));

    /* Register I/O port range */
    E1000_HW_DBG(("[HW] Registering I/O port range: 0x%x len=%u\n",
                  Adapter->IoPortAddress, Adapter->IoPortLength));
    Status = NdisMRegisterIoPortRange((PVOID*)&Adapter->IoPort,
                                      Adapter->AdapterHandle,
                                      Adapter->IoPortAddress,
                                      Adapter->IoPortLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to register IO port range (0x%x)\n", Status));
        E1000_INIT_DBG(("[INIT] FAILED: NdisMRegisterIoPortRange returned 0x%08x\n", Status));
        return NDIS_STATUS_RESOURCES;
    }
    E1000_HW_DBG(("[HW] I/O ports registered at VA=%p\n", Adapter->IoPort));

    /* Map memory-mapped I/O space */
    E1000_HW_DBG(("[HW] Mapping MMIO space: PA=0x%I64x len=%u\n",
                  Adapter->IoAddress.QuadPart, Adapter->IoLength));
    Status = NdisMMapIoSpace((PVOID*)&Adapter->IoBase,
                             Adapter->AdapterHandle,
                             Adapter->IoAddress,
                             Adapter->IoLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to map IO space (0x%x)\n", Status));
        E1000_INIT_DBG(("[INIT] FAILED: NdisMMapIoSpace returned 0x%08x\n", Status));
        return NDIS_STATUS_RESOURCES;
    }
    E1000_HW_DBG(("[HW] MMIO space mapped at VA=%p\n", Adapter->IoBase));

    /*
     * Map MSI-X BAR if present (for 82574L).
     *
     * The 82574L has a dedicated 16KB BAR for MSI-X tables and PBA (Pending Bit Array).
     * This BAR is required for MSI-X operation but is NOT required for legacy interrupts.
     *
     * If the BAR is present but we cannot map it, we fall back to legacy interrupts.
     * If the BAR is not present in the resource list, we're probably running with
     * MSI/MSI-X disabled at the hardware level (which is common in QEMU).
     */
    if (Adapter->MsixAddress.QuadPart != 0 && Adapter->MsixLength > 0)
    {
        E1000_INIT_DBG(("[INIT] MSI-X BAR detected: PA=0x%I64x Size=%u bytes\n",
                        Adapter->MsixAddress.QuadPart, Adapter->MsixLength));

        Status = NdisMMapIoSpace((PVOID*)&Adapter->MsixBase,
                                 Adapter->AdapterHandle,
                                 Adapter->MsixAddress,
                                 Adapter->MsixLength);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            /* MSI-X mapping failure is not fatal - we'll fall back to legacy interrupts */
            NDIS_DbgPrint(MIN_TRACE, ("Unable to map MSI-X space (0x%x), will use legacy interrupts\n", Status));
            E1000_INIT_DBG(("[INIT] MSI-X BAR mapping failed - using legacy interrupts\n"));
            Adapter->MsixBase = NULL;
        }
        else
        {
            ULONG TableEntry;
            NDIS_DbgPrint(MID_TRACE, ("MSI-X BAR mapped at %p\n", Adapter->MsixBase));
            E1000_INIT_DBG(("[INIT] MSI-X BAR mapped: VA=%p\n", Adapter->MsixBase));

            /*
             * Dump MSI-X table contents for debugging.
             * The table format is defined in PCI Local Bus Spec 3.0:
             * - Each entry is 16 bytes (4 DWORDs):
             *   Offset 0: Message Address Low (32-bit)
             *   Offset 4: Message Address High (32-bit)
             *   Offset 8: Message Data (32-bit, only low 16 bits used)
             *   Offset 12: Vector Control (32-bit, bit 0 = masked)
             *
             * 82574L has 5 MSI-X vectors (entries 0-4).
             */
            DbgPrint("E1000: MSI-X Table contents:\n");
            for (TableEntry = 0; TableEntry < 5; TableEntry++)
            {
                volatile PULONG Entry = (volatile PULONG)(Adapter->MsixBase + (TableEntry * 16));
                ULONG MsgAddrLo = Entry[0];
                ULONG MsgAddrHi = Entry[1];
                ULONG MsgData = Entry[2];
                ULONG VecCtrl = Entry[3];

                DbgPrint("E1000:   [%u] AddrLo=0x%08x AddrHi=0x%08x Data=0x%08x Ctrl=0x%08x %s\n",
                         TableEntry, MsgAddrLo, MsgAddrHi, MsgData, VecCtrl,
                         (VecCtrl & 1) ? "(masked)" : "(active)");
            }
        }
    }
    else
    {
        E1000_INIT_DBG(("[INIT] No MSI-X BAR present - will use legacy interrupts\n"));
        Adapter->MsixBase = NULL;
    }

    /* Note: Spin locks are allocated in MiniportInitialize, not here */

    /* Allocate transmit descriptors - increased to 256 */
    E1000_RING_DBG(("[RING] Allocating TX descriptor ring: %u descriptors, %u bytes\n",
                    NUM_TRANSMIT_DESCRIPTORS,
                    (ULONG)(sizeof(E1000_TRANSMIT_DESCRIPTOR) * NUM_TRANSMIT_DESCRIPTORS)));
    NdisMAllocateSharedMemory(Adapter->AdapterHandle,
                              sizeof(E1000_TRANSMIT_DESCRIPTOR) * NUM_TRANSMIT_DESCRIPTORS,
                              FALSE,
                              (PVOID*)&Adapter->TransmitDescriptors,
                              &Adapter->TransmitDescriptorsPa);
    if (Adapter->TransmitDescriptors == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to allocate transmit descriptors\n"));
        E1000_INIT_DBG(("[INIT] FAILED: TX descriptor allocation failed\n"));
        return NDIS_STATUS_RESOURCES;
    }

    NDIS_DbgPrint(MID_TRACE, ("Allocated %d TX descriptors at VA=%p PA=%I64x\n",
                              NUM_TRANSMIT_DESCRIPTORS,
                              Adapter->TransmitDescriptors,
                              Adapter->TransmitDescriptorsPa.QuadPart));
    E1000_RING_DBG(("[RING] TX ring allocated: VA=%p PA=0x%I64x\n",
                    Adapter->TransmitDescriptors, Adapter->TransmitDescriptorsPa.QuadPart));

    for (n = 0; n < NUM_TRANSMIT_DESCRIPTORS; ++n)
    {
        PE1000_TRANSMIT_DESCRIPTOR Descriptor = Adapter->TransmitDescriptors + n;
        Descriptor->Address = 0;
        Descriptor->Length = 0;
    }

    /* Allocate receive descriptors - increased to 256 */
    E1000_RING_DBG(("[RING] Allocating RX descriptor ring: %u descriptors, %u bytes\n",
                    NUM_RECEIVE_DESCRIPTORS,
                    (ULONG)(sizeof(E1000_RECEIVE_DESCRIPTOR) * NUM_RECEIVE_DESCRIPTORS)));
    NdisMAllocateSharedMemory(Adapter->AdapterHandle,
                              sizeof(E1000_RECEIVE_DESCRIPTOR) * NUM_RECEIVE_DESCRIPTORS,
                              FALSE,
                              (PVOID*)&Adapter->ReceiveDescriptors,
                              &Adapter->ReceiveDescriptorsPa);
    if (Adapter->ReceiveDescriptors == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to allocate receive descriptors\n"));
        E1000_INIT_DBG(("[INIT] FAILED: RX descriptor allocation failed\n"));
        return NDIS_STATUS_RESOURCES;
    }

    NDIS_DbgPrint(MID_TRACE, ("Allocated %d RX descriptors at VA=%p PA=%I64x\n",
                              NUM_RECEIVE_DESCRIPTORS,
                              Adapter->ReceiveDescriptors,
                              Adapter->ReceiveDescriptorsPa.QuadPart));
    E1000_RING_DBG(("[RING] RX ring allocated: VA=%p PA=0x%I64x\n",
                    Adapter->ReceiveDescriptors, Adapter->ReceiveDescriptorsPa.QuadPart));

    AllocationSize = RcvBufAllocationSize(Adapter->ReceiveBufferType);
    ASSERT(Adapter->ReceiveBufferEntrySize == 0 || Adapter->ReceiveBufferEntrySize == AllocationSize);
    Adapter->ReceiveBufferEntrySize = AllocationSize;

    E1000_RING_DBG(("[RING] Allocating RX buffer pool: %u entries x %u bytes = %u bytes total\n",
                    NUM_RECEIVE_DESCRIPTORS, Adapter->ReceiveBufferEntrySize,
                    Adapter->ReceiveBufferEntrySize * NUM_RECEIVE_DESCRIPTORS));
    NdisMAllocateSharedMemory(Adapter->AdapterHandle,
                              Adapter->ReceiveBufferEntrySize * NUM_RECEIVE_DESCRIPTORS,
                              FALSE,
                              (PVOID*)&Adapter->ReceiveBuffer,
                              &Adapter->ReceiveBufferPa);

    if (Adapter->ReceiveBuffer == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to allocate receive buffer\n"));
        E1000_INIT_DBG(("[INIT] FAILED: RX buffer allocation failed\n"));
        return NDIS_STATUS_RESOURCES;
    }
    E1000_RING_DBG(("[RING] RX buffer pool allocated: VA=%p PA=0x%I64x\n",
                    Adapter->ReceiveBuffer, Adapter->ReceiveBufferPa.QuadPart));

    for (n = 0; n < NUM_RECEIVE_DESCRIPTORS; ++n)
    {
        PE1000_RECEIVE_DESCRIPTOR Descriptor = Adapter->ReceiveDescriptors + n;

        RtlZeroMemory(Descriptor, sizeof(*Descriptor));
        Descriptor->Address = Adapter->ReceiveBufferPa.QuadPart + n * Adapter->ReceiveBufferEntrySize;
    }

    E1000_INIT_DBG(("[INIT] NICAllocateIoResources: SUCCESS - all resources allocated\n"));
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICRegisterInterrupts(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    NDIS_INTERRUPT_MODE InterruptType;
    ULONG InterruptVector;
    ULONG InterruptLevel;
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    /*
     * Extensive MSI/MSI-X debug logging
     */
    DbgPrint("E1000: NICRegisterInterrupts entry\n");
    DbgPrint("E1000:   InterruptVector=%u (0x%x)\n", Adapter->InterruptVector, Adapter->InterruptVector);
    DbgPrint("E1000:   InterruptLevel=%u (0x%x)\n", Adapter->InterruptLevel, Adapter->InterruptLevel);
    DbgPrint("E1000:   InterruptShared=%d\n", Adapter->InterruptShared);
    DbgPrint("E1000:   InterruptFlags=0x%x\n", Adapter->InterruptFlags);
    DbgPrint("E1000:   PciInterruptLine=%u\n", Adapter->PciInterruptLine);
    DbgPrint("E1000:   InterruptMode=%d (%s)\n", Adapter->InterruptMode,
             (Adapter->InterruptMode == E1000_INTERRUPT_MODE_LEGACY) ? "Legacy" :
             (Adapter->InterruptMode == E1000_INTERRUPT_MODE_MSI) ? "MSI" : "MSI-X");
    DbgPrint("E1000:   MsixVectorCount=%u\n", Adapter->MsixVectorCount);
    DbgPrint("E1000:   MsixAddress=0x%I64x MsixLength=%u MsixBase=%p\n",
             Adapter->MsixAddress.QuadPart, Adapter->MsixLength, Adapter->MsixBase);
    DbgPrint("E1000:   CM_RESOURCE_INTERRUPT_MESSAGE=%s\n",
             (Adapter->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE) ? "YES" : "NO");
    DbgPrint("E1000:   CM_RESOURCE_INTERRUPT_LATCHED=%s\n",
             (Adapter->InterruptFlags & CM_RESOURCE_INTERRUPT_LATCHED) ? "YES" : "NO");

    E1000_INT_DBG(("[INT] NICRegisterInterrupts: vector=%u level=%u shared=%d flags=0x%x\n",
                   Adapter->InterruptVector, Adapter->InterruptLevel,
                   Adapter->InterruptShared, Adapter->InterruptFlags));

    /*
     * Determine interrupt type based on flags from the resource descriptor.
     *
     * CM_RESOURCE_INTERRUPT_LATCHED (0x01) - Edge-triggered interrupt
     * CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE (0x00) - Level-triggered interrupt
     * CM_RESOURCE_INTERRUPT_MESSAGE (0x02) - MSI/MSI-X message-signaled interrupt
     *
     * For PCI devices with MSI/MSI-X:
     * - The Vector/Level from the resource descriptor are the allocated MSI vectors
     * - MSI interrupts are EDGE-TRIGGERED (latched), not level-sensitive
     * - The resource manager has already allocated vectors through the IRQ arbiter
     * - The PCI driver has already programmed the MSI Message Address/Data
     *
     * For legacy PCI interrupts (INTx):
     * - Flags without CM_RESOURCE_INTERRUPT_MESSAGE
     * - Level-sensitive mode for PCI devices
     */

    Adapter->MsixEnabled = FALSE;
    Adapter->MsiEnabled = FALSE;

    if (Adapter->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE)
    {
        /*
         * MSI/MSI-X resources detected.
         *
         * The Vector and Level values from the resource descriptor are the
         * MSI vector numbers allocated by the IRQ arbiter. The PCI driver
         * has already programmed the device's MSI capability structure with
         * the appropriate Message Address and Message Data.
         *
         * MSI interrupts are EDGE-TRIGGERED (NdisInterruptLatched).
         * They are typically not shared (exclusive to this device).
         */
        InterruptVector = Adapter->InterruptVector;
        InterruptLevel = Adapter->InterruptLevel;

        /*
         * MSI/MSI-X interrupts are edge-triggered.
         * However, on ARM64 with GIC, the HAL may have translated these to
         * level-sensitive SPIs. Check the LATCHED flag to determine the
         * actual trigger mode.
         */
        if (Adapter->InterruptFlags & CM_RESOURCE_INTERRUPT_LATCHED)
        {
            InterruptType = NdisInterruptLatched;
            DbgPrint("E1000: MSI mode: edge-triggered (latched)\n");
        }
        else
        {
            InterruptType = NdisInterruptLevelSensitive;
            DbgPrint("E1000: MSI mode: level-sensitive (translated by HAL)\n");
        }

        /*
         * Determine if this is MSI or MSI-X based on the number of vectors
         * and whether we have an MSI-X BAR mapped.
         */
        if (Adapter->MsixVectorCount > 1 && Adapter->MsixBase != NULL)
        {
            Adapter->InterruptMode = E1000_INTERRUPT_MODE_MSIX;
            Adapter->MsixEnabled = TRUE;
            DbgPrint("E1000: Using MSI-X mode with %u vectors\n", Adapter->MsixVectorCount);
        }
        else
        {
            Adapter->InterruptMode = E1000_INTERRUPT_MODE_MSI;
            Adapter->MsiEnabled = TRUE;
            DbgPrint("E1000: Using MSI mode (single vector)\n");
        }

        /*
         * MSI/MSI-X interrupts are device-exclusive (not shared)
         * unless the descriptor says otherwise.
         */
        DbgPrint("E1000: MSI interrupt registration: Vector=%u Level=%u Type=%s Shared=%d\n",
                 InterruptVector, InterruptLevel,
                 (InterruptType == NdisInterruptLatched) ? "Latched" : "Level",
                 Adapter->InterruptShared);

        E1000_INT_DBG(("[INT] MSI/MSI-X mode: Vector=%u Level=%u Type=%s\n",
                       InterruptVector, InterruptLevel,
                       (InterruptType == NdisInterruptLatched) ? "Latched" : "Level"));
    }
    else
    {
        /*
         * Legacy interrupt mode.
         * Use the Vector and Level as provided, and determine edge/level
         * based on the LATCHED flag.
         */
        InterruptVector = Adapter->InterruptVector;
        InterruptLevel = Adapter->InterruptLevel;
        Adapter->InterruptMode = E1000_INTERRUPT_MODE_LEGACY;

        if (Adapter->InterruptFlags & CM_RESOURCE_INTERRUPT_LATCHED)
        {
            InterruptType = NdisInterruptLatched;
            DbgPrint("E1000: Legacy mode: edge-triggered (latched)\n");
            E1000_INT_DBG(("[INT] Using legacy latched (edge-triggered) interrupt mode\n"));
        }
        else
        {
            InterruptType = NdisInterruptLevelSensitive;
            DbgPrint("E1000: Legacy mode: level-sensitive\n");
            E1000_INT_DBG(("[INT] Using legacy level-sensitive interrupt mode\n"));
        }

        DbgPrint("E1000: Legacy interrupt registration: Vector=%u Level=%u Type=%s Shared=%d\n",
                 InterruptVector, InterruptLevel,
                 (InterruptType == NdisInterruptLatched) ? "Latched" : "Level",
                 Adapter->InterruptShared);
    }

    E1000_INT_DBG(("[INT] Registering interrupt: Vector=%u Level=%u Shared=%d Type=%s\n",
                   InterruptVector, InterruptLevel, Adapter->InterruptShared,
                   (InterruptType == NdisInterruptLatched) ? "Latched" : "Level"));

    DbgPrint("E1000: Calling NdisMRegisterInterrupt(Vector=%u, Level=%u, Shared=%d, Mode=%d)\n",
             InterruptVector, InterruptLevel, Adapter->InterruptShared, InterruptType);

    Status = NdisMRegisterInterrupt(&Adapter->Interrupt,
                                    Adapter->AdapterHandle,
                                    InterruptVector,
                                    InterruptLevel,
                                    TRUE, // We always want ISR calls
                                    Adapter->InterruptShared,
                                    InterruptType);

    DbgPrint("E1000: NdisMRegisterInterrupt returned 0x%08x\n", Status);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        Adapter->InterruptRegistered = TRUE;

        DbgPrint("E1000: Interrupt registered successfully\n");
        DbgPrint("E1000:   Final mode: %s\n",
                 (Adapter->InterruptMode == E1000_INTERRUPT_MODE_LEGACY) ? "Legacy" :
                 (Adapter->InterruptMode == E1000_INTERRUPT_MODE_MSI) ? "MSI" : "MSI-X");

        NDIS_DbgPrint(MID_TRACE, ("Registered interrupt: vector=%u level=%u shared=%d type=%s mode=%s\n",
                                  InterruptVector, InterruptLevel,
                                  Adapter->InterruptShared,
                                  (InterruptType == NdisInterruptLatched) ? "latched" : "level-sensitive",
                                  (Adapter->InterruptMode == E1000_INTERRUPT_MODE_MSI) ? "MSI" :
                                  (Adapter->InterruptMode == E1000_INTERRUPT_MODE_MSIX) ? "MSI-X" : "Legacy"));
        E1000_INT_DBG(("[INT] Interrupt registered successfully\n"));
    }
    else
    {
        DbgPrint("E1000: FAILED to register interrupt: Status=0x%08x\n", Status);
        DbgPrint("E1000:   Vector=%u Level=%u Shared=%d Flags=0x%x\n",
                 InterruptVector, InterruptLevel,
                 Adapter->InterruptShared, Adapter->InterruptFlags);

        NDIS_DbgPrint(MIN_TRACE, ("Failed to register interrupt: Status=0x%x Vector=%u Level=%u\n",
                                  Status, InterruptVector, InterruptLevel));
        E1000_INIT_DBG(("[INIT] FAILED: NdisMRegisterInterrupt returned 0x%08x\n", Status));
        E1000_INIT_DBG(("[INIT] Interrupt params: Vector=%u Level=%u Shared=%d Flags=0x%x\n",
                        InterruptVector, InterruptLevel,
                        Adapter->InterruptShared, Adapter->InterruptFlags));
    }

    return Status;
}

NDIS_STATUS
NTAPI
NICUnregisterInterrupts(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    if (Adapter->InterruptRegistered)
    {
        E1000_INT_DBG(("[INT] Unregistering interrupt\n"));
        NdisMDeregisterInterrupt(&Adapter->Interrupt);
        Adapter->InterruptRegistered = FALSE;
        E1000_INT_DBG(("[INT] Interrupt unregistered successfully\n"));
    }
    else
    {
        E1000_INT_DBG(("[INT] NICUnregisterInterrupts: No interrupt was registered\n"));
    }

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICReleaseIoResources(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_INIT_DBG(("[INIT] NICReleaseIoResources: Releasing hardware resources\n"));

    if (Adapter->ReceiveDescriptors != NULL)
    {
        E1000_RING_DBG(("[RING] Freeing RX descriptor ring\n"));
        /* Disassociate our shared buffer before freeing it to avoid NIC-induced memory corruption */
        if (Adapter->IoBase)
        {
            E1000WriteUlong(Adapter, E1000_REG_RDH, 0);
            E1000WriteUlong(Adapter, E1000_REG_RDT, 0);
        }

        NdisMFreeSharedMemory(Adapter->AdapterHandle,
                              sizeof(E1000_RECEIVE_DESCRIPTOR) * NUM_RECEIVE_DESCRIPTORS,
                              FALSE,
                              Adapter->ReceiveDescriptors,
                              Adapter->ReceiveDescriptorsPa);

        Adapter->ReceiveDescriptors = NULL;
    }

    if (Adapter->ReceiveBuffer != NULL)
    {
        E1000_RING_DBG(("[RING] Freeing RX buffer pool\n"));
        NdisMFreeSharedMemory(Adapter->AdapterHandle,
                              Adapter->ReceiveBufferEntrySize * NUM_RECEIVE_DESCRIPTORS,
                              FALSE,
                              (PVOID)Adapter->ReceiveBuffer,
                              Adapter->ReceiveBufferPa);

        Adapter->ReceiveBuffer = NULL;
        Adapter->ReceiveBufferEntrySize = 0;
    }


    if (Adapter->TransmitDescriptors != NULL)
    {
        E1000_RING_DBG(("[RING] Freeing TX descriptor ring\n"));
        /* Disassociate our shared buffer before freeing it to avoid NIC-induced memory corruption */
        if (Adapter->IoBase)
        {
            E1000WriteUlong(Adapter, E1000_REG_TDH, 0);
            E1000WriteUlong(Adapter, E1000_REG_TDT, 0);
        }

        NdisMFreeSharedMemory(Adapter->AdapterHandle,
                              sizeof(E1000_TRANSMIT_DESCRIPTOR) * NUM_TRANSMIT_DESCRIPTORS,
                              FALSE,
                              Adapter->TransmitDescriptors,
                              Adapter->TransmitDescriptorsPa);

        Adapter->TransmitDescriptors = NULL;
    }

    /* Note: Spin locks are freed in MiniportHalt, not here */

    if (Adapter->MsixBase)
    {
        E1000_HW_DBG(("[HW] Unmapping MSI-X BAR\n"));
        NdisMUnmapIoSpace(Adapter->AdapterHandle, Adapter->MsixBase, Adapter->MsixLength);
        Adapter->MsixBase = NULL;
    }

    if (Adapter->IoPort)
    {
        E1000_HW_DBG(("[HW] Deregistering I/O port range\n"));
        NdisMDeregisterIoPortRange(Adapter->AdapterHandle,
                                   Adapter->IoPortAddress,
                                   Adapter->IoPortLength,
                                   (PVOID)Adapter->IoPort);
    }

    if (Adapter->IoBase)
    {
        E1000_HW_DBG(("[HW] Unmapping MMIO space\n"));
        NdisMUnmapIoSpace(Adapter->AdapterHandle, (PVOID)Adapter->IoBase, Adapter->IoLength);
    }

    E1000_INIT_DBG(("[INIT] NICReleaseIoResources: All resources released\n"));

    return NDIS_STATUS_SUCCESS;
}


NDIS_STATUS
NTAPI
NICPowerOn(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_POWER_DBG(("[POWER] NICPowerOn: Powering on adapter\n"));
    E1000_STAT_INC32(PowerTransitions);

    Status = NICSoftReset(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        E1000_POWER_DBG(("[POWER] FAILED: Soft reset failed with status 0x%08x\n", Status));
        return Status;
    }
    E1000_POWER_DBG(("[POWER] Soft reset completed successfully\n"));

    /* Now that IoBase is available, detect if NVM is flash-based */
    E1000DetectFlashNvm(Adapter);

    E1000_HW_DBG(("[HW] Validating NVM checksum\n"));
    if (!E1000ValidateNvmChecksum(Adapter))
    {
        E1000_INIT_DBG(("[INIT] FAILED: NVM checksum validation failed\n"));
        return NDIS_STATUS_INVALID_DATA;
    }
    E1000_HW_DBG(("[HW] NVM checksum valid\n"));

    /* Read device serial number (for PCIe devices) */
    E1000ReadDeviceSerialNumber(Adapter);

    /* Initialize power state */
    Adapter->CurrentPowerState = E1000PowerStateD0;
    Adapter->NdisPowerState = NdisDeviceStateD0;

    E1000_POWER_DBG(("[POWER] NICPowerOn: Adapter powered on, state=D0\n"));

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICSoftReset(
    IN PE1000_ADAPTER Adapter)
{
    ULONG Value, ResetAttempts;
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_HW_DBG(("[HW] NICSoftReset: Beginning hardware reset sequence\n"));
    E1000_STAT_INC32(ResetCount);

    E1000_HW_DBG(("[HW] Disabling interrupts and TX/RX\n"));
    NICDisableInterrupts(Adapter);
    E1000WriteUlong(Adapter, E1000_REG_RCTL, 0);
    E1000WriteUlong(Adapter, E1000_REG_TCTL, 0);

    E1000ReadUlong(Adapter, E1000_REG_CTRL, &Value);
    E1000_HW_DBG(("[HW] Current CTRL=0x%08x, issuing reset via I/O port\n", Value));
    /* Write this using IO port, some devices cannot ack this otherwise */
    E1000WriteIoUlong(Adapter, E1000_REG_CTRL, Value | E1000_CTRL_RST);


    for (ResetAttempts = 0; ResetAttempts < MAX_RESET_ATTEMPTS; ResetAttempts++)
    {
        /* Wait 1us after reset (according to manual) */
        NdisStallExecution(1);
        E1000ReadUlong(Adapter, E1000_REG_CTRL, &Value);

        if (!(Value & E1000_CTRL_RST))
        {
            NDIS_DbgPrint(MAX_TRACE, ("Device is back (%u)\n", ResetAttempts));
            E1000_HW_DBG(("[HW] Reset complete after %u attempts\n", ResetAttempts + 1));

            NICDisableInterrupts(Adapter);
            /* Clear out interrupts (the register is cleared upon read) */
            E1000ReadUlong(Adapter, E1000_REG_ICR, &Value);
            E1000_HW_DBG(("[HW] Cleared pending interrupts: ICR=0x%08x\n", Value));

            E1000ReadUlong(Adapter, E1000_REG_CTRL, &Value);
            Value &= ~(E1000_CTRL_LRST|E1000_CTRL_VME);
            Value |= (E1000_CTRL_ASDE|E1000_CTRL_SLU);
            E1000WriteUlong(Adapter, E1000_REG_CTRL, Value);
            E1000_HW_DBG(("[HW] Set CTRL=0x%08x (ASDE|SLU enabled, LRST|VME cleared)\n", Value));

            return NDIS_STATUS_SUCCESS;
        }
    }

    NDIS_DbgPrint(MIN_TRACE, ("Device did not recover\n"));
    E1000_HW_DBG(("[HW] FAILED: Reset timed out after %u attempts\n", MAX_RESET_ATTEMPTS));
    return NDIS_STATUS_FAILURE;
}


/*
 * NICConfigureIVAR - Configure interrupt vector allocation registers
 *
 * For 82574L and other PCIe devices, configures the IVAR registers to map
 * interrupt causes to vectors. When using legacy interrupts, all causes
 * are mapped to vector 0.
 */
static VOID NICConfigureIVAR(IN PE1000_ADAPTER Adapter)
{
    if (!Adapter->IsPCIe)
        return;

    if (Adapter->InterruptMode == E1000_INTERRUPT_MODE_MSIX && Adapter->MsixVectorCount >= 5)
    {
        /*
         * MSI-X mode with 5 vectors:
         * Vector 0: Rx Queue 0
         * Vector 1: Rx Queue 1
         * Vector 2: Tx Queue 0
         * Vector 3: Tx Queue 1
         * Vector 4: Other (Link status, etc.)
         */
        ULONG Ivar0 = 0;
        Ivar0 |= (E1000_IVAR_VALID | E1000_MSIX_VECTOR_RXQ0) << E1000_IVAR_RXQ0_SHIFT;
        Ivar0 |= (E1000_IVAR_VALID | E1000_MSIX_VECTOR_TXQ0) << E1000_IVAR_TXQ0_SHIFT;
        Ivar0 |= (E1000_IVAR_VALID | E1000_MSIX_VECTOR_RXQ1) << E1000_IVAR_RXQ1_SHIFT;
        Ivar0 |= (E1000_IVAR_VALID | E1000_MSIX_VECTOR_TXQ1) << E1000_IVAR_TXQ1_SHIFT;
        E1000WriteUlong(Adapter, E1000_IVAR0, Ivar0);

        ULONG IvarMisc = (E1000_IVAR_VALID | E1000_MSIX_VECTOR_OTHER) << E1000_IVAR_MISC_OTHER_SHIFT;
        E1000WriteUlong(Adapter, E1000_IVAR_MISC, IvarMisc);

        NDIS_DbgPrint(MID_TRACE, ("Configured MSI-X IVAR: IVAR0=0x%08x, IVAR_MISC=0x%08x\n",
                                  Ivar0, IvarMisc));
    }
    else
    {
        /*
         * Legacy or MSI mode: All causes mapped to vector 0
         * IVAR0 (0x1700):
         * Bit 7: Rx Queue 0 Valid
         * Bits 0-2: Rx Queue 0 Vector (0)
         * Bit 15: Tx Queue 0 Valid
         * Bits 8-10: Tx Queue 0 Vector (0)
         * Value: 0x80 | 0x8000 = 0x8080
         */
        E1000WriteUlong(Adapter, E1000_IVAR0, 0x8080);

        /*
         * IVAR_MISC (0x1740):
         * Bit 7: Other Cause Valid
         * Bits 0-2: Other Cause Vector (0)
         * Value: 0x80
         */
        E1000WriteUlong(Adapter, E1000_IVAR_MISC, 0x80);

        NDIS_DbgPrint(MID_TRACE, ("Configured legacy IVAR for PCIe device\n"));
    }
}


/*
 * NICConfigureInterruptThrottling - Configure interrupt coalescing
 *
 * Sets up interrupt throttling registers for optimal performance.
 * The 82574L supports per-vector interrupt throttling with MSI-X.
 */
static VOID NICConfigureInterruptThrottling(IN PE1000_ADAPTER Adapter)
{
    ULONG ItrValue;

    /* Default interrupt throttle rate */
    ItrValue = DEFAULT_ITR;

    /* Store for adaptive moderation */
    Adapter->InterruptModeration.CurrentItr = ItrValue;
    Adapter->InterruptModeration.AdaptiveEnabled = TRUE;
    Adapter->InterruptModeration.PacketsSinceLastAdjust = 0;
    Adapter->InterruptModeration.BytesSinceLastAdjust = 0;

    /* Set the global ITR register */
    E1000WriteUlong(Adapter, E1000_REG_ITR, ItrValue);

    if (Adapter->IsPCIe)
    {
        /* For 82574L, also set the extended interrupt throttle registers */
        E1000WriteUlong(Adapter, E1000_REG_EITR0, ItrValue);
        E1000WriteUlong(Adapter, E1000_REG_EITR1, ItrValue);
        E1000WriteUlong(Adapter, E1000_REG_EITR2, ItrValue);
        E1000WriteUlong(Adapter, E1000_REG_EITR3, ItrValue);
        E1000WriteUlong(Adapter, E1000_REG_EITR4, ItrValue);
    }

    NDIS_DbgPrint(MID_TRACE, ("Interrupt throttling configured: ITR=%u (~%u int/sec)\n",
                              ItrValue, ItrValue ? (1000000000 / (ItrValue * 256)) : 0));
}


NDIS_STATUS
NTAPI
NICEnableTxRx(
    IN PE1000_ADAPTER Adapter)
{
    ULONG Value;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_INIT_DBG(("[INIT] NICEnableTxRx: Configuring TX/RX engines\n"));

    /*
     * Set a default packet filter if none has been configured.
     * This ensures the adapter can receive packets immediately after init.
     *
     * The default filter accepts:
     * - NDIS_PACKET_TYPE_DIRECTED: Packets addressed to our MAC
     * - NDIS_PACKET_TYPE_BROADCAST: Broadcast packets (needed for ARP, DHCP)
     *
     * Protocols will later set the appropriate filter via OID_GEN_CURRENT_PACKET_FILTER,
     * but having a default ensures basic connectivity even before protocols bind.
     */
    if (Adapter->PacketFilter == 0)
    {
        Adapter->PacketFilter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_BROADCAST;
        NDIS_DbgPrint(MID_TRACE, ("Set default packet filter: 0x%08x (DIRECTED|BROADCAST)\n",
                                  Adapter->PacketFilter));
        E1000_INIT_DBG(("[INIT] Default packet filter set: 0x%08x\n", Adapter->PacketFilter));
    }

    /*
     * For PCIe devices (82574L, 82583V), configure IVAR registers to map
     * all interrupt causes to the appropriate vectors.
     */
    NICConfigureIVAR(Adapter);

    /* Configure interrupt throttling */
    NICConfigureInterruptThrottling(Adapter);

    NDIS_DbgPrint(MID_TRACE, ("Setting up transmit.\n"));
    E1000_RING_DBG(("[RING] Configuring TX descriptor ring\n"));

    /* Make sure the thing is disabled first. */
    E1000WriteUlong(Adapter, E1000_REG_TCTL, 0);

    /* Transmit descriptor ring buffer */
    E1000WriteUlong(Adapter, E1000_REG_TDBAH, Adapter->TransmitDescriptorsPa.HighPart);
    E1000WriteUlong(Adapter, E1000_REG_TDBAL, Adapter->TransmitDescriptorsPa.LowPart);

    /* Transmit descriptor buffer size */
    E1000WriteUlong(Adapter, E1000_REG_TDLEN, sizeof(E1000_TRANSMIT_DESCRIPTOR) * NUM_TRANSMIT_DESCRIPTORS);

    /* Transmit descriptor tail / head */
    E1000WriteUlong(Adapter, E1000_REG_TDH, 0);
    E1000WriteUlong(Adapter, E1000_REG_TDT, 0);
    Adapter->CurrentTxDesc = 0;
    Adapter->LastTxDesc = 0;
    Adapter->TxFull = FALSE;

    /* Set up interrupt timers */
    E1000WriteUlong(Adapter, E1000_REG_TADV, 96); // value is in 1.024 of usec
    E1000WriteUlong(Adapter, E1000_REG_TIDV, 16);

    /* Configure TX descriptor control for better performance */
    Value = (8 << E1000_TXDCTL_PTHRESH_SHIFT) |  /* Prefetch threshold */
            (1 << E1000_TXDCTL_HTHRESH_SHIFT) |  /* Host threshold */
            (1 << E1000_TXDCTL_WTHRESH_SHIFT) |  /* Write back threshold */
            E1000_TXDCTL_GRAN;                    /* Descriptor granularity */
    E1000WriteUlong(Adapter, E1000_REG_TXDCTL, Value);

    /* Enable transmitter with collision threshold and collision distance */
    Value = E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT_DEF | E1000_TCTL_COLD_DEF;
    E1000WriteUlong(Adapter, E1000_REG_TCTL, Value);

    E1000WriteUlong(Adapter, E1000_REG_TIPG, E1000_TIPG_IPGT_DEF | E1000_TIPG_IPGR1_DEF | E1000_TIPG_IPGR2_DEF);

    NDIS_DbgPrint(MID_TRACE, ("Setting up receive.\n"));
    E1000_RING_DBG(("[RING] Configuring RX descriptor ring\n"));

    /* Make sure the thing is disabled first. */
    E1000WriteUlong(Adapter, E1000_REG_RCTL, 0);

    /* Receive descriptor ring buffer */
    E1000WriteUlong(Adapter, E1000_REG_RDBAH, Adapter->ReceiveDescriptorsPa.HighPart);
    E1000WriteUlong(Adapter, E1000_REG_RDBAL, Adapter->ReceiveDescriptorsPa.LowPart);

    /* Receive descriptor buffer size */
    E1000WriteUlong(Adapter, E1000_REG_RDLEN, sizeof(E1000_RECEIVE_DESCRIPTOR) * NUM_RECEIVE_DESCRIPTORS);

    /* Receive descriptor tail / head */
    E1000WriteUlong(Adapter, E1000_REG_RDH, 0);
    E1000WriteUlong(Adapter, E1000_REG_RDT, NUM_RECEIVE_DESCRIPTORS - 1);

    /* Set up interrupt timers */
    E1000WriteUlong(Adapter, E1000_REG_RADV, 96);
    E1000WriteUlong(Adapter, E1000_REG_RDTR, 16);

    /* Enable checksum offload */
    E1000_CSUM_DBG(("[CSUM] Initializing checksum offload\n"));
    NICInitializeChecksumOffload(Adapter);
    if (Adapter->ChecksumOffload.RxIpChecksum || Adapter->ChecksumOffload.RxTcpChecksum)
    {
        NICEnableChecksumOffload(Adapter, TRUE, TRUE);
    }

    /* Some defaults */
    Value = E1000_RCTL_SECRC | E1000_RCTL_EN;

    /* Receive buffer size */
    Value |= RcvBufRegisterMask(Adapter->ReceiveBufferType);

    /* Add our current packet filter */
    Value |= PacketFilterToMask(Adapter->PacketFilter);

    E1000WriteUlong(Adapter, E1000_REG_RCTL, Value);
    E1000_HW_DBG(("[HW] RCTL=0x%08x (RX enabled with filter mask)\n", Value));

    NDIS_DbgPrint(MID_TRACE, ("TX/RX enabled. TX descriptors=%d, RX descriptors=%d\n",
                              NUM_TRANSMIT_DESCRIPTORS, NUM_RECEIVE_DESCRIPTORS));
    E1000_INIT_DBG(("[INIT] NICEnableTxRx: TX/RX engines enabled, %d TX desc, %d RX desc\n",
                    NUM_TRANSMIT_DESCRIPTORS, NUM_RECEIVE_DESCRIPTORS));

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICDisableTxRx(
    IN PE1000_ADAPTER Adapter)
{
    ULONG Value;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_INIT_DBG(("[INIT] NICDisableTxRx: Disabling TX/RX engines\n"));

    E1000ReadUlong(Adapter, E1000_REG_TCTL, &Value);
    Value &= ~E1000_TCTL_EN;
    E1000WriteUlong(Adapter, E1000_REG_TCTL, Value);
    E1000_HW_DBG(("[HW] TX disabled: TCTL=0x%08x\n", Value));

    E1000ReadUlong(Adapter, E1000_REG_RCTL, &Value);
    Value &= ~E1000_RCTL_EN;
    E1000WriteUlong(Adapter, E1000_REG_RCTL, Value);
    E1000_HW_DBG(("[HW] RX disabled: RCTL=0x%08x\n", Value));

    E1000_INIT_DBG(("[INIT] NICDisableTxRx: TX/RX engines disabled\n"));

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICGetPermanentMacAddress(
    IN PE1000_ADAPTER Adapter,
    OUT PUCHAR MacAddress)
{
    USHORT AddrWord;
    UINT n;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    /* Should we read from RAL/RAH first? */
    for (n = 0; n < (IEEE_802_ADDR_LENGTH / 2); ++n)
    {
        if (!E1000ReadEeprom(Adapter, (UCHAR)n, &AddrWord))
            return NDIS_STATUS_FAILURE;
        Adapter->PermanentMacAddress[n * 2 + 0] = AddrWord & 0xff;
        Adapter->PermanentMacAddress[n * 2 + 1] = (AddrWord >> 8) & 0xff;
    }

#if 0
    NDIS_DbgPrint(MIN_TRACE, ("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                              Adapter->PermanentMacAddress[0],
                              Adapter->PermanentMacAddress[1],
                              Adapter->PermanentMacAddress[2],
                              Adapter->PermanentMacAddress[3],
                              Adapter->PermanentMacAddress[4],
                              Adapter->PermanentMacAddress[5]));
#endif
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICUpdateMulticastList(
    IN PE1000_ADAPTER Adapter)
{
    UINT n;
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    // FIXME: Use 'Adapter->MulticastListSize'? Check the datasheet
    for (n = 0; n < MAXIMUM_MULTICAST_ADDRESSES; ++n)
    {
        ULONG Ral = *(ULONG *)Adapter->MulticastList[n].MacAddress;
        ULONG Rah = *(USHORT *)&Adapter->MulticastList[n].MacAddress[4];

        if (Rah || Ral)
        {
            Rah |= E1000_RAH_AV;

            E1000WriteUlong(Adapter, E1000_REG_RAL + (8*n), Ral);
            E1000WriteUlong(Adapter, E1000_REG_RAH + (8*n), Rah);
        }
        else
        {
            E1000WriteUlong(Adapter, E1000_REG_RAH + (8*n), 0);
            E1000WriteUlong(Adapter, E1000_REG_RAL + (8*n), 0);
        }
    }

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICApplyPacketFilter(
    IN PE1000_ADAPTER Adapter)
{
    ULONG FilterMask;

    E1000ReadUlong(Adapter, E1000_REG_RCTL, &FilterMask);

    FilterMask &= ~E1000_RCTL_FILTER_BITS;
    FilterMask |= PacketFilterToMask(Adapter->PacketFilter);
    E1000WriteUlong(Adapter, E1000_REG_RCTL, FilterMask);

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NICUpdateLinkStatus(
    IN PE1000_ADAPTER Adapter)
{
    ULONG DeviceStatus;
    SIZE_T SpeedIndex;
    NDIS_MEDIA_STATE OldMediaState;
    ULONG OldLinkSpeed;
    static ULONG SpeedValues[] = { 10, 100, 1000, 1000 };

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    /* Save old state for change detection */
    OldMediaState = Adapter->MediaState;
    OldLinkSpeed = Adapter->LinkSpeedMbps;

    E1000ReadUlong(Adapter, E1000_REG_STATUS, &DeviceStatus);
    Adapter->MediaState = (DeviceStatus & E1000_STATUS_LU) ? NdisMediaStateConnected : NdisMediaStateDisconnected;
    SpeedIndex = (DeviceStatus & E1000_STATUS_SPEEDMASK) >> E1000_STATUS_SPEEDSHIFT;
    Adapter->LinkSpeedMbps = SpeedValues[SpeedIndex];

    /* Log link state changes */
    if (OldMediaState != Adapter->MediaState || OldLinkSpeed != Adapter->LinkSpeedMbps)
    {
        E1000_LINK_DBG(("[LINK] Status changed: %s -> %s, Speed: %u -> %u Mbps (STATUS=0x%08x)\n",
                        (OldMediaState == NdisMediaStateConnected) ? "Connected" : "Disconnected",
                        (Adapter->MediaState == NdisMediaStateConnected) ? "Connected" : "Disconnected",
                        OldLinkSpeed, Adapter->LinkSpeedMbps, DeviceStatus));
        E1000_STAT_INC32(LinkInterrupts);
    }
}


/* ============================================================================
 * Checksum Offload Implementation
 * ============================================================================ */

VOID
NTAPI
NICInitializeChecksumOffload(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_CSUM_DBG(("[CSUM] NICInitializeChecksumOffload: Initializing offload capabilities\n"));

    /*
     * The E1000 family supports two modes of TX checksum offload:
     *
     * 1. Legacy descriptor mode: Uses E1000_TDESC_CMD_IC flag with ChecksumOffset
     *    and ChecksumStartField in the legacy TX descriptor. This provides basic
     *    checksum insertion but requires the driver to calculate the pseudo-header
     *    checksum for TCP/UDP.
     *
     * 2. Context descriptor mode (82574L and newer): Uses extended descriptors
     *    with DEXT=1 and DTYP=context. This provides full TCP/IP checksum offload
     *    without requiring driver-calculated pseudo-headers.
     *
     * For now, we enable RX checksum offload (which is straightforward via RXCSUM)
     * and TX checksum offload using the legacy descriptor method. The legacy method
     * works on all E1000 variants including 82574L.
     *
     * Note: For full performance with TSO (TCP Segmentation Offload), context
     * descriptors would be required, but that's a more complex feature.
     */

    /* TX offload capabilities - enable for IPv4 TCP/UDP using legacy descriptors */
    Adapter->ChecksumOffload.TxIpChecksum = TRUE;    /* IPv4 header checksum */
    Adapter->ChecksumOffload.TxTcpChecksum = TRUE;   /* TCP checksum */
    Adapter->ChecksumOffload.TxUdpChecksum = TRUE;   /* UDP checksum */

    /* RX offload capabilities - fully supported via RXCSUM register */
    Adapter->ChecksumOffload.RxIpChecksum = TRUE;
    Adapter->ChecksumOffload.RxTcpChecksum = TRUE;
    Adapter->ChecksumOffload.RxUdpChecksum = TRUE;

    /* Will be enabled in NICEnableChecksumOffload */
    Adapter->ChecksumOffload.TxChecksumEnabled = FALSE;
    Adapter->ChecksumOffload.RxChecksumEnabled = FALSE;

    NDIS_DbgPrint(MID_TRACE, ("Checksum offload capabilities initialized\n"));
    E1000_CSUM_DBG(("[CSUM] Capabilities: TX(IP=%d TCP=%d UDP=%d) RX(IP=%d TCP=%d UDP=%d)\n",
                    Adapter->ChecksumOffload.TxIpChecksum,
                    Adapter->ChecksumOffload.TxTcpChecksum,
                    Adapter->ChecksumOffload.TxUdpChecksum,
                    Adapter->ChecksumOffload.RxIpChecksum,
                    Adapter->ChecksumOffload.RxTcpChecksum,
                    Adapter->ChecksumOffload.RxUdpChecksum));
}

NDIS_STATUS
NTAPI
NICEnableChecksumOffload(
    IN PE1000_ADAPTER Adapter,
    IN BOOLEAN EnableTx,
    IN BOOLEAN EnableRx)
{
    ULONG RxcsumValue = 0;

    NDIS_DbgPrint(MAX_TRACE, ("Called (EnableTx=%d, EnableRx=%d).\n", EnableTx, EnableRx));

    E1000_CSUM_DBG(("[CSUM] NICEnableChecksumOffload: TX=%s RX=%s\n",
                    EnableTx ? "ENABLE" : "DISABLE",
                    EnableRx ? "ENABLE" : "DISABLE"));

    if (EnableRx)
    {
        /* Enable IP checksum offload */
        RxcsumValue |= E1000_RXCSUM_IPOFL;

        /* Enable TCP/UDP checksum offload */
        RxcsumValue |= E1000_RXCSUM_TUOFL;

        /* Set packet checksum start (after Ethernet header) */
        RxcsumValue |= (E1000_CSUM_IP_START & E1000_RXCSUM_PCSS_MASK);

        Adapter->ChecksumOffload.RxChecksumEnabled = TRUE;
        E1000_CSUM_DBG(("[CSUM] RX checksum offload enabled: IPOFL|TUOFL\n"));
    }
    else
    {
        Adapter->ChecksumOffload.RxChecksumEnabled = FALSE;
        E1000_CSUM_DBG(("[CSUM] RX checksum offload disabled\n"));
    }

    E1000WriteUlong(Adapter, E1000_REG_RXCSUM, RxcsumValue);
    E1000_HW_DBG(("[HW] RXCSUM=0x%08x\n", RxcsumValue));

    /*
     * TX checksum offload using legacy descriptors.
     *
     * The E1000 hardware can compute and insert checksums when:
     * - E1000_TDESC_CMD_IC (Insert Checksum) bit is set in descriptor
     * - ChecksumOffset specifies where to insert the checksum
     * - ChecksumStartField specifies where to start checksum calculation
     *
     * This is the legacy method and works on all E1000 variants.
     * Note: For TCP/UDP, the driver must pre-compute the pseudo-header checksum.
     * This is typically handled by the protocol stack before the packet reaches
     * the miniport driver.
     */
    if (EnableTx)
    {
        Adapter->ChecksumOffload.TxChecksumEnabled = TRUE;
        E1000_CSUM_DBG(("[CSUM] TX checksum offload enabled (legacy descriptor mode)\n"));
    }
    else
    {
        Adapter->ChecksumOffload.TxChecksumEnabled = FALSE;
        E1000_CSUM_DBG(("[CSUM] TX checksum offload disabled\n"));
    }

    NDIS_DbgPrint(MID_TRACE, ("Checksum offload: RXCSUM=0x%08x, TxEnabled=%d, RxEnabled=%d\n",
                              RxcsumValue,
                              Adapter->ChecksumOffload.TxChecksumEnabled,
                              Adapter->ChecksumOffload.RxChecksumEnabled));

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICDisableChecksumOffload(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_CSUM_DBG(("[CSUM] NICDisableChecksumOffload: Disabling all checksum offload\n"));

    /* Disable all checksum offload */
    E1000WriteUlong(Adapter, E1000_REG_RXCSUM, 0);
    E1000_HW_DBG(("[HW] RXCSUM=0x0 (disabled)\n"));

    Adapter->ChecksumOffload.TxChecksumEnabled = FALSE;
    Adapter->ChecksumOffload.RxChecksumEnabled = FALSE;

    E1000_CSUM_DBG(("[CSUM] Checksum offload disabled\n"));

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * Power Management Implementation
 * ============================================================================ */

NDIS_STATUS
NTAPI
NICSetPowerState(
    IN PE1000_ADAPTER Adapter,
    IN NDIS_DEVICE_POWER_STATE PowerState)
{
    NDIS_DEVICE_POWER_STATE OldPowerState = Adapter->NdisPowerState;

    NDIS_DbgPrint(MAX_TRACE, ("Called (PowerState=%d).\n", PowerState));

    E1000_POWER_DBG(("[POWER] NICSetPowerState: D%d -> D%d\n",
                     OldPowerState - NdisDeviceStateD0,
                     PowerState - NdisDeviceStateD0));
    E1000_STAT_INC32(PowerTransitions);

    switch (PowerState)
    {
    case NdisDeviceStateD0:
        /* Transition to full power */
        E1000_POWER_DBG(("[POWER] Transitioning to D0 (full power)\n"));
        if (Adapter->NdisPowerState != NdisDeviceStateD0)
        {
            E1000_POWER_DBG(("[POWER] Restoring device state from D%d\n",
                             OldPowerState - NdisDeviceStateD0));
            /* Restore device state */
            NICRestoreDeviceState(Adapter);

            /* Re-enable TX/RX */
            NICEnableTxRx(Adapter);

            /* Re-enable interrupts */
            NICApplyInterruptMask(Adapter);
            E1000_POWER_DBG(("[POWER] Device state restored, TX/RX and interrupts enabled\n"));
        }
        Adapter->CurrentPowerState = E1000PowerStateD0;
        break;

    case NdisDeviceStateD1:
    case NdisDeviceStateD2:
        /* Light/medium sleep - device maintains some state */
        E1000_POWER_DBG(("[POWER] Transitioning to D%d (light/medium sleep)\n",
                         PowerState - NdisDeviceStateD0));
        if (Adapter->NdisPowerState == NdisDeviceStateD0)
        {
            /* Disable interrupts */
            NICDisableInterrupts(Adapter);

            /* Disable TX/RX */
            NICDisableTxRx(Adapter);

            /* Save device state */
            NICSaveDeviceState(Adapter);
            E1000_POWER_DBG(("[POWER] Device state saved, TX/RX and interrupts disabled\n"));
        }
        Adapter->CurrentPowerState = (PowerState == NdisDeviceStateD1) ?
                                     E1000PowerStateD1 : E1000PowerStateD2;
        break;

    case NdisDeviceStateD3:
        /* Lowest power state - device may lose context */
        E1000_POWER_DBG(("[POWER] Transitioning to D3 (lowest power)\n"));
        if (Adapter->NdisPowerState == NdisDeviceStateD0)
        {
            /* Disable interrupts */
            NICDisableInterrupts(Adapter);

            /* Disable TX/RX */
            NICDisableTxRx(Adapter);

            /* Save device state */
            NICSaveDeviceState(Adapter);

            /* Configure wake-up filters if enabled */
            if (Adapter->WakeOnMagicPacket || Adapter->WakeOnLinkChange)
            {
                ULONG Wufc = 0;
                if (Adapter->WakeOnMagicPacket)
                    Wufc |= E1000_WUFC_MAG;
                if (Adapter->WakeOnLinkChange)
                    Wufc |= E1000_WUFC_LNKC;
                E1000WriteUlong(Adapter, E1000_REG_WUFC, Wufc);

                /* Enable PME */
                E1000WriteUlong(Adapter, E1000_REG_WUC, E1000_WUC_PME_EN);
                E1000_POWER_DBG(("[POWER] Wake-on-LAN configured: WUFC=0x%08x (Magic=%d Link=%d)\n",
                                 Wufc, Adapter->WakeOnMagicPacket, Adapter->WakeOnLinkChange));
            }
            E1000_POWER_DBG(("[POWER] Device state saved, entering D3 sleep\n"));
        }
        Adapter->CurrentPowerState = E1000PowerStateD3;
        break;

    default:
        E1000_POWER_DBG(("[POWER] FAILED: Invalid power state %d\n", PowerState));
        return NDIS_STATUS_INVALID_DATA;
    }

    Adapter->NdisPowerState = PowerState;
    NDIS_DbgPrint(MID_TRACE, ("Power state changed to D%d\n",
                              PowerState - NdisDeviceStateD0));
    E1000_POWER_DBG(("[POWER] Power state transition complete: D%d\n",
                     PowerState - NdisDeviceStateD0));

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NICQueryPowerState(
    IN PE1000_ADAPTER Adapter,
    IN NDIS_DEVICE_POWER_STATE PowerState)
{
    NDIS_DbgPrint(MAX_TRACE, ("Called (PowerState=%d).\n", PowerState));

    /* We support all power states */
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NICSaveDeviceState(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_POWER_DBG(("[POWER] NICSaveDeviceState: Saving adapter state\n"));

    /* Save any state needed for power transitions */
    /* The descriptor ring state is already tracked in the adapter structure */

    E1000_POWER_DBG(("[POWER] Device state saved (TX: current=%u last=%u, RX buffer=%p)\n",
                     Adapter->CurrentTxDesc, Adapter->LastTxDesc, Adapter->ReceiveBuffer));
}

VOID
NTAPI
NICRestoreDeviceState(
    IN PE1000_ADAPTER Adapter)
{
    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    E1000_POWER_DBG(("[POWER] NICRestoreDeviceState: Restoring adapter state\n"));

    /* Clear wake-up status */
    E1000WriteUlong(Adapter, E1000_REG_WUC, 0);
    E1000WriteUlong(Adapter, E1000_REG_WUFC, 0);
    E1000_HW_DBG(("[HW] Cleared wake-up status registers\n"));

    /* Perform soft reset to restore device */
    E1000_POWER_DBG(("[POWER] Performing soft reset\n"));
    NICSoftReset(Adapter);

    /* Restore MAC address */
    E1000_POWER_DBG(("[POWER] Restoring multicast list\n"));
    NICUpdateMulticastList(Adapter);

    E1000_POWER_DBG(("[POWER] Device state restore complete\n"));
}


/* ============================================================================
 * Statistics Implementation
 * ============================================================================ */

VOID
NTAPI
NICUpdateStatistics(
    IN PE1000_ADAPTER Adapter)
{
    ULONG Low, High;
    ULONG64 TxBytesDelta, RxBytesDelta;
    ULONG TxPacketsDelta, RxPacketsDelta;

    /* Read and accumulate statistics from hardware registers */

    /* Good Packets Transmitted */
    E1000ReadUlong(Adapter, E1000_REG_GPTC, &Low);
    TxPacketsDelta = Low;
    Adapter->Statistics.TxPackets += Low;

    /* Good Packets Received */
    E1000ReadUlong(Adapter, E1000_REG_GPRC, &Low);
    RxPacketsDelta = Low;
    Adapter->Statistics.RxPackets += Low;

    /* Good Octets Transmitted */
    E1000ReadUlong(Adapter, E1000_REG_GOTCL, &Low);
    E1000ReadUlong(Adapter, E1000_REG_GOTCH, &High);
    TxBytesDelta = ((ULONG64)High << 32) | Low;
    Adapter->Statistics.TxBytes += TxBytesDelta;

    /* Good Octets Received */
    E1000ReadUlong(Adapter, E1000_REG_GORCL, &Low);
    E1000ReadUlong(Adapter, E1000_REG_GORCH, &High);
    RxBytesDelta = ((ULONG64)High << 32) | Low;
    Adapter->Statistics.RxBytes += RxBytesDelta;

    /* Receive No Buffers */
    E1000ReadUlong(Adapter, E1000_REG_RNBC, &Low);
    Adapter->Statistics.RxNoBuffer += Low;

    /* CRC Errors */
    E1000ReadUlong(Adapter, E1000_REG_CRCERRS, &Low);
    Adapter->Statistics.RxCrcErrors += Low;

    /* Alignment Errors */
    E1000ReadUlong(Adapter, E1000_REG_ALGNERRC, &Low);
    Adapter->Statistics.RxAlignErrors += Low;

    /* Collision Count */
    E1000ReadUlong(Adapter, E1000_REG_COLC, &Low);
    Adapter->Statistics.TxCollisions += Low;

    /* Total Transmit Errors (approximation) */
    E1000ReadUlong(Adapter, E1000_REG_ECOL, &Low);  /* Excessive collisions */
    Adapter->Statistics.TxErrors += Low;
    E1000ReadUlong(Adapter, E1000_REG_LATECOL, &Low);  /* Late collisions */
    Adapter->Statistics.TxErrors += Low;

    /* Total Receive Errors */
    E1000ReadUlong(Adapter, E1000_REG_RXERRC, &Low);
    Adapter->Statistics.RxErrors += Low;

    /* Log statistics if there's any activity (rate-limited to avoid spam) */
    if (TxPacketsDelta > 0 || RxPacketsDelta > 0 ||
        Adapter->Statistics.RxCrcErrors > 0 || Adapter->Statistics.TxErrors > 0)
    {
        E1000_STATS_DBG(("[STATS] Delta: TX %u pkts/%I64u bytes, RX %u pkts/%I64u bytes\n",
                         TxPacketsDelta, TxBytesDelta, RxPacketsDelta, RxBytesDelta));
        E1000_STATS_DBG(("[STATS] Totals: TX %I64u/%I64u, RX %I64u/%I64u, Errors: TX=%I64u RX=%I64u CRC=%I64u\n",
                         Adapter->Statistics.TxPackets, Adapter->Statistics.TxBytes,
                         Adapter->Statistics.RxPackets, Adapter->Statistics.RxBytes,
                         Adapter->Statistics.TxErrors, Adapter->Statistics.RxErrors,
                         Adapter->Statistics.RxCrcErrors));
    }
}

#endif /* NDIS51_MINIPORT */


/* ============================================================================
 * NDIS 6.x Wrapper Functions
 *
 * These wrapper functions provide the interface expected by init.c and other
 * NDIS 6.x source files.
 * ============================================================================ */

#if !defined(NDIS51_MINIPORT)

/*
 * E1000RecognizeHardware - Verify this is a supported E1000 device
 *
 * Wrapper for NICRecognizeHardware for NDIS 6.x compatibility.
 */
BOOLEAN
E1000RecognizeHardware(
    _In_ PE1000_ADAPTER Adapter
    )
{
    UINT n;

    DbgPrint("E1000: E1000RecognizeHardware - VendorId=0x%04x DeviceId=0x%04x\n",
             Adapter->VendorId, Adapter->DeviceId);

    if (Adapter->VendorId != HW_VENDOR_INTEL)
    {
        DbgPrint("E1000: Unknown vendor: 0x%04x (expected 0x8086)\n", Adapter->VendorId);
        return FALSE;
    }

    for (n = 0; n < ARRAYSIZE(SupportedDevices); ++n)
    {
        if (SupportedDevices[n] == Adapter->DeviceId)
        {
            DbgPrint("E1000: Device recognized: Intel NIC DeviceId=0x%04x\n", Adapter->DeviceId);

            /* Detect device type (PCIe vs PCI) - needed for EEPROM access */
            Adapter->IsPCIe = FALSE;
            {
                UINT i;
                for (i = 0; i < ARRAYSIZE(PCIeDevices); ++i)
                {
                    if (PCIeDevices[i] == Adapter->DeviceId)
                    {
                        Adapter->IsPCIe = TRUE;
                        break;
                    }
                }
            }

            DbgPrint("E1000: Device type: %s\n", Adapter->IsPCIe ? "PCIe" : "PCI");
            return TRUE;
        }
    }

    DbgPrint("E1000: Unknown device: 0x%04x not in supported list\n", Adapter->DeviceId);
    return FALSE;
}


/*
 * E1000ResetHardware - Reset the E1000 hardware
 *
 * Performs a complete hardware reset sequence for NDIS 6.x.
 */
NDIS_STATUS
E1000ResetHardware(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG Value, ResetAttempts;

    DbgPrint("E1000: E1000ResetHardware - Beginning hardware reset\n");

    if (Adapter->IoBase == NULL)
    {
        DbgPrint("E1000: ResetHardware - IoBase not mapped\n");
        return NDIS_STATUS_HARD_ERRORS;
    }

    /* Disable interrupts */
    E1000_WRITE_REG(Adapter, E1000_REG_IMC, 0xFFFFFFFF);

    /* Disable RX and TX */
    E1000_WRITE_REG(Adapter, E1000_REG_RCTL, 0);
    E1000_WRITE_REG(Adapter, E1000_REG_TCTL, 0);

    /* Issue software reset */
    Value = E1000_READ_REG(Adapter, E1000_REG_CTRL);
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value | E1000_CTRL_RST);

    /* Wait for reset to complete */
    for (ResetAttempts = 0; ResetAttempts < MAX_RESET_ATTEMPTS; ResetAttempts++)
    {
        NdisStallExecution(1);
        Value = E1000_READ_REG(Adapter, E1000_REG_CTRL);

        if (!(Value & E1000_CTRL_RST))
        {
            DbgPrint("E1000: Reset complete after %u attempts\n", ResetAttempts + 1);

            /* Clear pending interrupts */
            Value = E1000_READ_REG(Adapter, E1000_REG_ICR);

            /* Configure control register */
            Value = E1000_READ_REG(Adapter, E1000_REG_CTRL);
            Value &= ~(E1000_CTRL_LRST | E1000_CTRL_VME);
            Value |= (E1000_CTRL_ASDE | E1000_CTRL_SLU);
            E1000_WRITE_REG(Adapter, E1000_REG_CTRL, Value);

            /* Initialize power state */
            Adapter->CurrentPowerState = E1000PowerStateD0;
            Adapter->NdisPowerState = NdisDeviceStateD0;

            return NDIS_STATUS_SUCCESS;
        }
    }

    DbgPrint("E1000: Reset timed out after %u attempts\n", MAX_RESET_ATTEMPTS);
    return NDIS_STATUS_HARD_ERRORS;
}


/*
 * E1000SetupLink - Configure the link
 *
 * Sets up the PHY and link parameters for auto-negotiation.
 */
NDIS_STATUS
E1000SetupLink(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG CtrlValue;
    ULONG TctlValue;
    ULONG RctlValue;

    DbgPrint("E1000: E1000SetupLink - Configuring link\n");

    if (Adapter->IoBase == NULL)
    {
        return NDIS_STATUS_HARD_ERRORS;
    }

    /* Configure for auto-negotiation */
    CtrlValue = E1000_READ_REG(Adapter, E1000_REG_CTRL);
    CtrlValue |= E1000_CTRL_ASDE | E1000_CTRL_SLU;
    CtrlValue &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPLX);
    E1000_WRITE_REG(Adapter, E1000_REG_CTRL, CtrlValue);

    /* Enable transmitter */
    TctlValue = E1000_TCTL_EN |          /* Enable TX */
                E1000_TCTL_PSP |         /* Pad short packets */
                E1000_TCTL_CT_IEEE |     /* Collision threshold */
                E1000_TCTL_COLD_FD;      /* Collision distance (full duplex) */
    E1000_WRITE_REG(Adapter, E1000_REG_TCTL, TctlValue);

    /* Configure IPG (Inter-Packet Gap) */
    E1000_WRITE_REG(Adapter, E1000_REG_TIPG,
                    E1000_TIPG_IPGT_DEF | E1000_TIPG_IPGR1_DEF | E1000_TIPG_IPGR2_DEF);

    /* Enable receiver with default filter */
    RctlValue = E1000_RCTL_EN |          /* Enable RX */
                E1000_RCTL_SECRC |       /* Strip CRC */
                E1000_RCTL_BAM |         /* Accept broadcast */
                (E1000_RCTL_BSIZE_2048 << E1000_RCTL_BSIZE_SHIFT);
    E1000_WRITE_REG(Adapter, E1000_REG_RCTL, RctlValue);

    /* Update link status */
    E1000UpdateLinkStatus(Adapter);

    Adapter->AutoNegotiate = TRUE;

    DbgPrint("E1000: Link setup complete - MediaState=%d, Speed=%I64u\n",
             Adapter->MediaState, Adapter->LinkSpeed);

    return NDIS_STATUS_SUCCESS;
}


/*
 * E1000UpdateLinkStatus - Update link state from hardware
 *
 * Reads the device status register to update link state.
 */
VOID
E1000UpdateLinkStatus(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG DeviceStatus;
    SIZE_T SpeedIndex;
    NDIS_MEDIA_CONNECT_STATE OldMediaState;
    static ULONG64 SpeedValues[] = {
        E1000_LINK_SPEED_10MBPS,
        E1000_LINK_SPEED_100MBPS,
        E1000_LINK_SPEED_1GBPS,
        E1000_LINK_SPEED_1GBPS
    };

    if (Adapter->IoBase == NULL)
    {
        return;
    }

    OldMediaState = Adapter->MediaState;

    DeviceStatus = E1000_READ_REG(Adapter, E1000_REG_STATUS);

    /* Link status */
    if (DeviceStatus & E1000_STATUS_LU)
    {
        Adapter->MediaState = MediaConnectStateConnected;
        InterlockedOr(&Adapter->Flags, E1000_FLAG_LINK_UP);
    }
    else
    {
        Adapter->MediaState = MediaConnectStateDisconnected;
        InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_LINK_UP);
    }

    /* Link speed */
    SpeedIndex = (DeviceStatus & E1000_STATUS_SPEEDMASK) >> E1000_STATUS_SPEEDSHIFT;
    Adapter->LinkSpeed = SpeedValues[SpeedIndex];

    /* Duplex mode */
    Adapter->FullDuplex = (DeviceStatus & E1000_STATUS_FD) ? TRUE : FALSE;

    /* Log and indicate link state changes */
    if (OldMediaState != Adapter->MediaState)
    {
        NDIS_STATUS_INDICATION StatusIndication;
        NDIS_LINK_STATE LinkState;

        DbgPrint("E1000: Link state changed: %s, Speed=%I64u bps, Duplex=%s\n",
                 (Adapter->MediaState == MediaConnectStateConnected) ? "Connected" : "Disconnected",
                 Adapter->LinkSpeed,
                 Adapter->FullDuplex ? "Full" : "Half");

        /* Indicate link state change to NDIS */
        if (Adapter->MiniportAdapterHandle != NULL)
        {
            /* Initialize link state structure */
            RtlZeroMemory(&LinkState, sizeof(LinkState));
            LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
            LinkState.MediaConnectState = Adapter->MediaState;
            LinkState.MediaDuplexState = Adapter->FullDuplex ? MediaDuplexStateFull : MediaDuplexStateHalf;
            LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

            /* Initialize status indication */
            RtlZeroMemory(&StatusIndication, sizeof(StatusIndication));
            StatusIndication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
            StatusIndication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
            StatusIndication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
            StatusIndication.SourceHandle = Adapter->MiniportAdapterHandle;
            StatusIndication.StatusCode = NDIS_STATUS_LINK_STATE;
            StatusIndication.StatusBuffer = &LinkState;
            StatusIndication.StatusBufferSize = sizeof(LinkState);

            DbgPrint("E1000: Indicating link state to NDIS - Handle=%p, State=%d\n",
                     Adapter->MiniportAdapterHandle, Adapter->MediaState);

            NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &StatusIndication);
        }
    }
}


/*
 * E1000UpdateStatistics - Read statistics from hardware
 *
 * Wrapper for NICUpdateStatistics for NDIS 6.x.
 */
VOID
E1000UpdateStatistics(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG Low, High;
    ULONG64 BytesDelta;

    if (Adapter->IoBase == NULL)
    {
        return;
    }

    /* Good Packets Transmitted */
    Low = E1000_READ_REG(Adapter, E1000_REG_GPTC);
    Adapter->Statistics.TxPackets += Low;

    /* Good Packets Received */
    Low = E1000_READ_REG(Adapter, E1000_REG_GPRC);
    Adapter->Statistics.RxPackets += Low;

    /* Good Octets Transmitted */
    Low = E1000_READ_REG(Adapter, E1000_REG_GOTCL);
    High = E1000_READ_REG(Adapter, E1000_REG_GOTCH);
    BytesDelta = ((ULONG64)High << 32) | Low;
    Adapter->Statistics.TxBytes += BytesDelta;

    /* Good Octets Received */
    Low = E1000_READ_REG(Adapter, E1000_REG_GORCL);
    High = E1000_READ_REG(Adapter, E1000_REG_GORCH);
    BytesDelta = ((ULONG64)High << 32) | Low;
    Adapter->Statistics.RxBytes += BytesDelta;

    /* Receive No Buffers */
    Low = E1000_READ_REG(Adapter, E1000_REG_RNBC);
    Adapter->Statistics.RxNoBuffer += Low;

    /* CRC Errors */
    Low = E1000_READ_REG(Adapter, E1000_REG_CRCERRS);
    Adapter->Statistics.RxCrcErrors += Low;

    /* Total Receive Errors */
    Low = E1000_READ_REG(Adapter, E1000_REG_RXERRC);
    Adapter->Statistics.RxErrors += Low;

    /* Excessive Collisions */
    Low = E1000_READ_REG(Adapter, E1000_REG_ECOL);
    Adapter->Statistics.TxErrors += Low;

    /* Late Collisions */
    Low = E1000_READ_REG(Adapter, E1000_REG_LATECOL);
    Adapter->Statistics.TxErrors += Low;
}


/*
 * E1000SetPacketFilter - Configure receive packet filter
 *
 * Updates the RCTL register to reflect the requested packet filter.
 */
NDIS_STATUS
E1000SetPacketFilter(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG PacketFilter
    )
{
    ULONG RctlValue;

    DbgPrint("E1000: E1000SetPacketFilter - Filter=0x%08x\n", PacketFilter);

    if (Adapter->IoBase == NULL)
    {
        return NDIS_STATUS_HARD_ERRORS;
    }

    /* Read current RCTL */
    RctlValue = E1000_READ_REG(Adapter, E1000_REG_RCTL);

    /* Clear filter bits */
    RctlValue &= ~E1000_RCTL_FILTER_BITS;

    /* Apply new filter */
    if (PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
    {
        RctlValue |= E1000_RCTL_UPE | E1000_RCTL_MPE;
    }
    if (PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST)
    {
        RctlValue |= E1000_RCTL_MPE;
    }
    if (PacketFilter & NDIS_PACKET_TYPE_BROADCAST)
    {
        RctlValue |= E1000_RCTL_BAM;
    }

    E1000_WRITE_REG(Adapter, E1000_REG_RCTL, RctlValue);

    return NDIS_STATUS_SUCCESS;
}


/*
 * E1000SetMulticastList - Configure multicast address list
 *
 * Programs the multicast address table (MTA) and RAR entries.
 */
NDIS_STATUS
E1000SetMulticastList(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PUCHAR MulticastList,
    _In_ ULONG MulticastCount
    )
{
    ULONG i;
    PUCHAR Address;

    DbgPrint("E1000: E1000SetMulticastList - Count=%u\n", MulticastCount);

    if (Adapter->IoBase == NULL)
    {
        return NDIS_STATUS_HARD_ERRORS;
    }

    if (MulticastCount > E1000_MAX_MULTICAST)
    {
        return NDIS_STATUS_MULTICAST_FULL;
    }

    /* Program multicast addresses into RAR entries (starting at index 1) */
    for (i = 0; i < MulticastCount && i < 15; i++)
    {
        Address = MulticastList + (i * ETH_LENGTH_OF_ADDRESS);

        ULONG RalValue = ((ULONG)Address[0]) |
                         ((ULONG)Address[1] << 8) |
                         ((ULONG)Address[2] << 16) |
                         ((ULONG)Address[3] << 24);

        ULONG RahValue = ((ULONG)Address[4]) |
                         ((ULONG)Address[5] << 8) |
                         E1000_RAH_AV;

        E1000_WRITE_REG(Adapter, E1000_REG_RAL + ((i + 1) * 8), RalValue);
        E1000_WRITE_REG(Adapter, E1000_REG_RAH + ((i + 1) * 8), RahValue);
    }

    /* Clear unused RAR entries */
    for (; i < 15; i++)
    {
        E1000_WRITE_REG(Adapter, E1000_REG_RAL + ((i + 1) * 8), 0);
        E1000_WRITE_REG(Adapter, E1000_REG_RAH + ((i + 1) * 8), 0);
    }

    return NDIS_STATUS_SUCCESS;
}

#endif /* !NDIS51_MINIPORT */
