/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware detection routines
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>
#include "../vidfb.h"
#if defined(_M_IX86) || defined(_M_AMD64)
#include <arch/pc/pcbios.h>
#endif
#include <reactos/arc/loaderblk.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

/* GLOBALS *******************************************************************/

extern EFI_SYSTEM_TABLE * GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* From uefivid.c */
extern ULONG_PTR VramAddress;
extern ULONG VramSize;
extern PCM_FRAMEBUF_DEVICE_DATA FrameBufferData;

#ifndef TAG_HW_RESOURCE_LIST
#define TAG_HW_RESOURCE_LIST    'lRwH'
#endif

BOOLEAN AcpiPresent = FALSE;
static EFI_EVENT IdleTimerEvent = NULL;

/* FUNCTIONS *****************************************************************/

PVOID
UefiGetSmbiosEpsPointer(VOID)
{
    EFI_GUID Smbios3Guid = SMBIOS3_TABLE_GUID;
    UINTN Index;

    if (!GlobalSystemTable)
        return NULL;

    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &Smbios3Guid, sizeof(EFI_GUID)))
        {
            PSMBIOS3_ENTRY_POINT Entry3 = (PSMBIOS3_ENTRY_POINT)Entry->VendorTable;

            if (Entry3 &&
                (memcmp(Entry3->Anchor, "_SM3_", sizeof(Entry3->Anchor)) == 0) &&
                (Entry3->TableAddress != 0))
            {
                return Entry->VendorTable;
            }
        }
    }

    return NULL;
}

#if defined(_M_ARM) || defined(_M_ARM64) || defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)

static
const CHAR*
UefiSmbiosGetString(
    _In_ PSMBIOS_HEADER Header,
    _In_ UCHAR StringIndex)
{
    const CHAR *String;
    UCHAR Index;

    if ((StringIndex == 0) || (Header->Length < sizeof(SMBIOS_HEADER)))
        return NULL;

    String = (const CHAR*)((UINTN)Header + Header->Length);

    for (Index = 1; Index < StringIndex; ++Index)
    {
        ULONG Guard = 0;

        while ((String[0] != ANSI_NULL) && (++Guard < 1024))
            ++String;

        if ((Guard >= 1024) || (String[1] == ANSI_NULL))
            return NULL;

        ++String;
    }

    return (String[0] != ANSI_NULL) ? String : NULL;
}

static
PSMBIOS_HEADER
UefiSmbiosNextStructure(
    _In_ PSMBIOS_HEADER Header)
{
    const CHAR *String;
    ULONG Guard = 0;

    if (Header->Length < sizeof(SMBIOS_HEADER))
        return NULL;

    String = (const CHAR*)((UINTN)Header + Header->Length);

    while (((String[0] != ANSI_NULL) || (String[1] != ANSI_NULL)) &&
           (++Guard < 4096))
    {
        ++String;
    }

    if (Guard >= 4096)
        return NULL;

    return (PSMBIOS_HEADER)(String + 2);
}

static
PSMBIOS_HEADER
UefiGetSmbiosTable(VOID)
{
    EFI_GUID Smbios3Guid = SMBIOS3_TABLE_GUID;
    EFI_GUID SmbiosGuid = SMBIOS_TABLE_GUID;
    UINTN Index;

    if (!GlobalSystemTable)
        return NULL;

    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &Smbios3Guid, sizeof(EFI_GUID)))
        {
            PSMBIOS3_ENTRY_POINT Entry3 = (PSMBIOS3_ENTRY_POINT)Entry->VendorTable;

            if (Entry3 &&
                (memcmp(Entry3->Anchor, "_SM3_", sizeof(Entry3->Anchor)) == 0) &&
                (Entry3->TableAddress != 0))
            {
                return (PSMBIOS_HEADER)(UINTN)Entry3->TableAddress;
            }
        }

        if (!memcmp(&Entry->VendorGuid, &SmbiosGuid, sizeof(EFI_GUID)))
        {
            PSMBIOS_ENTRY_POINT Entry2 = (PSMBIOS_ENTRY_POINT)Entry->VendorTable;

            if (Entry2 &&
                (memcmp(Entry2->Anchor, "_SM_", sizeof(Entry2->Anchor)) == 0) &&
                (Entry2->TableAddress != 0))
            {
                return (PSMBIOS_HEADER)(UINTN)Entry2->TableAddress;
            }
        }
    }

    return NULL;
}

static
BOOLEAN
UefiIsUsableSmbiosString(
    _In_opt_ PCSTR String)
{
    if (!String || (String[0] == ANSI_NULL))
        return FALSE;

    if (!strcmp(String, "To Be Filled By O.E.M.") ||
        !strcmp(String, "Default string") ||
        !strcmp(String, "System Product Name") ||
        !strcmp(String, "System manufacturer") ||
        !strcmp(String, "Not Specified"))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
UefiGetSmbiosSystemIdentifier(
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize)
{
    PSMBIOS_HEADER Header;
    ULONG Count;

    Header = UefiGetSmbiosTable();
    if (!Header)
        return FALSE;

    for (Count = 0; Count < 256 && Header && Header->Type != 127; ++Count)
    {
        if (Header->Type == 1)
        {
            PSMBIOS_SYSTEM_INFO SystemInfo = (PSMBIOS_SYSTEM_INFO)Header;
            PCSTR Manufacturer;
            PCSTR ProductName;

            if (Header->Length < FIELD_OFFSET(SMBIOS_SYSTEM_INFO, Version))
                return FALSE;

            Manufacturer = UefiSmbiosGetString(Header, SystemInfo->Manufacturer);
            ProductName = UefiSmbiosGetString(Header, SystemInfo->ProductName);

            if (UefiIsUsableSmbiosString(Manufacturer) &&
                UefiIsUsableSmbiosString(ProductName))
            {
                if (strstr(ProductName, Manufacturer))
                    return NT_SUCCESS(RtlStringCbCopyA(Buffer, BufferSize, ProductName));

                return NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                                     BufferSize,
                                                     "%s %s",
                                                     Manufacturer,
                                                     ProductName));
            }

            if (UefiIsUsableSmbiosString(ProductName))
                return NT_SUCCESS(RtlStringCbCopyA(Buffer, BufferSize, ProductName));

            if (UefiIsUsableSmbiosString(Manufacturer))
                return NT_SUCCESS(RtlStringCbCopyA(Buffer, BufferSize, Manufacturer));

            return FALSE;
        }

        Header = UefiSmbiosNextStructure(Header);
    }

    return FALSE;
}

#endif

VOID
StallExecutionProcessor(ULONG Microseconds)
{
    GlobalSystemTable->BootServices->Stall(Microseconds);
}

VOID
UefiHwIdle(VOID)
{
    UINTN Index;
    EFI_STATUS Status;
    EFI_BOOT_SERVICES *BootServices = GlobalSystemTable->BootServices;

    /* Keep one timer event around and arm it each idle tick */
    if (IdleTimerEvent == NULL)
    {
        Status = BootServices->CreateEvent(EVT_TIMER,
                                           TPL_APPLICATION,
                                           NULL,
                                           NULL,
                                           &IdleTimerEvent);
        if (EFI_ERROR(Status))
        {
            StallExecutionProcessor(10000); /* 10 ms fallback */
            return;
        }
    }

    /* Set a 10ms (100,000 * 100ns) relative timer */
    Status = BootServices->SetTimer(IdleTimerEvent, TimerRelative, 100000);
    if (!EFI_ERROR(Status))
        Status = BootServices->WaitForEvent(1, &IdleTimerEvent, &Index);
    if (EFI_ERROR(Status))
        StallExecutionProcessor(10000); /* 10 ms fallback */
}

BOOLEAN IsAcpiPresent(VOID)
{
    return AcpiPresent;
}

static
PRSDP_DESCRIPTOR
FindAcpiBios(VOID)
{
    UINTN i;
    RSDP_DESCRIPTOR* rsdp = NULL;
    EFI_GUID acpi2_guid = EFI_ACPI_20_TABLE_GUID;

    for (i = 0; i < GlobalSystemTable->NumberOfTableEntries; i++)
    {
        if (!memcmp(&GlobalSystemTable->ConfigurationTable[i].VendorGuid,
                    &acpi2_guid, sizeof(acpi2_guid)))
        {
            rsdp = (RSDP_DESCRIPTOR*)GlobalSystemTable->ConfigurationTable[i].VendorTable;
            break;
        }
    }

    return rsdp;
}

PDESCRIPTION_HEADER
UefiFindAcpiTable(
    _In_ ULONG Signature)
{
    UINTN Index, Count;
    PRSDP_DESCRIPTOR Rsdp;

    Rsdp = FindAcpiBios();
    if (Rsdp == NULL)
        return NULL;

    if ((Rsdp->revision > 0) && (Rsdp->xsdt_physical_address != 0))
    {
        PXSDT Xsdt = (PXSDT)(ULONG_PTR)Rsdp->xsdt_physical_address;

        if ((Xsdt != NULL) && (Xsdt->Header.Length >= sizeof(Xsdt->Header)))
        {
            Count = (Xsdt->Header.Length - sizeof(Xsdt->Header)) / sizeof(Xsdt->Tables[0]);
            for (Index = 0; Index < Count; ++Index)
            {
                PDESCRIPTION_HEADER Header =
                    (PDESCRIPTION_HEADER)(ULONG_PTR)Xsdt->Tables[Index].QuadPart;

                if ((Header != NULL) && (Header->Signature == Signature))
                    return Header;
            }
        }
    }

    if (Rsdp->rsdt_physical_address != 0)
    {
        PRSDT Rsdt = (PRSDT)(ULONG_PTR)Rsdp->rsdt_physical_address;

        if ((Rsdt != NULL) && (Rsdt->Header.Length >= sizeof(Rsdt->Header)))
        {
            Count = (Rsdt->Header.Length - sizeof(Rsdt->Header)) / sizeof(Rsdt->Tables[0]);
            for (Index = 0; Index < Count; ++Index)
            {
                PDESCRIPTION_HEADER Header =
                    (PDESCRIPTION_HEADER)(ULONG_PTR)Rsdt->Tables[Index];

                if ((Header != NULL) && (Header->Signature == Signature))
                    return Header;
            }
        }
    }

    return NULL;
}

VOID
DetectAcpiBios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber)
{
    PCONFIGURATION_COMPONENT_DATA BiosKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PRSDP_DESCRIPTOR Rsdp;
    PACPI_BIOS_MULTI_NODE AcpiBiosData;
    ULONG TableSize, Size;

    Rsdp = FindAcpiBios();

    if (Rsdp)
    {
        /* Set up the flag in the loader block */
        AcpiPresent = TRUE;

        /* Calculate the table size */
        TableSize = sizeof(ACPI_BIOS_MULTI_NODE);

        /* Set 'Configuration Data' value */
        Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors[1]) + TableSize;
        PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
        if (PartialResourceList == NULL)
        {
            ERR("Failed to allocate resource descriptor\n");
            return;
        }

        RtlZeroMemory(PartialResourceList, Size);
        PartialResourceList->Version = 0;
        PartialResourceList->Revision = 0;
        PartialResourceList->Count = 1;

        PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
        PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
        PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
        PartialDescriptor->u.DeviceSpecificData.DataSize = TableSize;

        /* Fill the table */
        AcpiBiosData = (PACPI_BIOS_MULTI_NODE)(PartialDescriptor + 1);

        if (Rsdp->revision > 0)
        {
            TRACE("ACPI >1.0, using XSDT address\n");
            AcpiBiosData->RsdtAddress.QuadPart = Rsdp->xsdt_physical_address;
        }
        else
        {
            TRACE("ACPI 1.0, using RSDT address\n");
            AcpiBiosData->RsdtAddress.LowPart = Rsdp->rsdt_physical_address;
        }

        AcpiBiosData->Count = 0;

        TRACE("RSDT %p, data size %x\n", Rsdp->rsdt_physical_address, TableSize);

        /* Create new bus key */
        FldrCreateComponentKey(SystemKey,
                               AdapterClass,
                               MultiFunctionAdapter,
                               0x0,
                               0x0,
                               0xFFFFFFFF,
                               "ACPI BIOS",
                               PartialResourceList,
                               Size,
                               &BiosKey);

        /* Increment bus number */
        (*BusNumber)++;
    }
}

static VOID
DetectDisplayController(
    _In_ PCONFIGURATION_COMPONENT_DATA BusKey)
{
    PCONFIGURATION_COMPONENT_DATA ControllerKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PCM_FRAMEBUF_DEVICE_DATA FramebufData;
    ULONG Size;

    if (!VramAddress || (VramSize == 0) || !FrameBufferData)
        return;

    Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors[2]) + sizeof(*FramebufData);
    PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
    if (PartialResourceList == NULL)
    {
        ERR("Failed to allocate resource descriptor\n");
        return;
    }

    /* Initialize resource descriptor */
    RtlZeroMemory(PartialResourceList, Size);
    PartialResourceList->Version  = 1;
    PartialResourceList->Revision = 2;
    PartialResourceList->Count = 2;

    /* Set Memory */
    PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
    PartialDescriptor->Type = CmResourceTypeMemory;
    PartialDescriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    PartialDescriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    PartialDescriptor->u.Memory.Start.QuadPart = VramAddress;
    PartialDescriptor->u.Memory.Length = VramSize;

    /* Set framebuffer-specific data */
    PartialDescriptor = &PartialResourceList->PartialDescriptors[1];
    PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
    PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
    PartialDescriptor->Flags = 0;
    PartialDescriptor->u.DeviceSpecificData.DataSize = sizeof(*FramebufData);

    /* Get pointer to framebuffer-specific data */
    FramebufData = (PCM_FRAMEBUF_DEVICE_DATA)(PartialDescriptor + 1);
    RtlCopyMemory(FramebufData, FrameBufferData, sizeof(*FrameBufferData));
    FramebufData->Version  = 1;
    FramebufData->Revision = 3;
    FramebufData->VideoClock = 0; // FIXME: Use EDID

    FldrCreateComponentKey(BusKey,
                           ControllerClass,
                           DisplayController,
                           Output | ConsoleOut,
                           0,
                           0xFFFFFFFF,
                           "UEFI GOP Framebuffer",
                           PartialResourceList,
                           Size,
                           &ControllerKey);

    // NOTE: Don't add a MonitorPeripheral for now.
    // We should use EDID data for it.
}

static
VOID
DetectInternal(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber)
{
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCONFIGURATION_COMPONENT_DATA BusKey;
    ULONG Size;

    /* Set 'Configuration Data' value */
    Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors);
    PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
    if (PartialResourceList == NULL)
    {
        ERR("Failed to allocate resource descriptor\n");
        return;
    }

    /* Initialize resource descriptor */
    RtlZeroMemory(PartialResourceList, Size);
    PartialResourceList->Version  = 1;
    PartialResourceList->Revision = 1;
    PartialResourceList->Count = 0;

    /* Create new bus key */
    FldrCreateComponentKey(SystemKey,
                           AdapterClass,
                           MultiFunctionAdapter,
                           0,
                           0,
                           0xFFFFFFFF,
                           "UEFI Internal",
                           PartialResourceList,
                           Size,
                           &BusKey);

    /* Increment bus number */
    (*BusNumber)++;

    /* Detect devices that do not belong to "standard" buses */
    DetectDisplayController(BusKey);

    /* FIXME: Detect more devices */
}

PCONFIGURATION_COMPONENT_DATA
UefiHwDetect(
    _In_opt_ PCSTR Options)
{
    PCONFIGURATION_COMPONENT_DATA SystemKey;
    ULONG BusNumber = 0;

    TRACE("DetectHardware()\n");

    /* Create the 'System' key */
#if defined(_M_IX86) || defined(_M_AMD64)
    FldrCreateSystemKey(&SystemKey, "AT/AT COMPATIBLE");
#elif defined(_M_IA64)
    FldrCreateSystemKey(&SystemKey, "Intel Itanium processor family");
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)
    {
        CHAR SystemIdentifier[128];

        RtlStringCbCopyA(SystemIdentifier,
                         sizeof(SystemIdentifier),
                         "ARM processor family");
        UefiGetSmbiosSystemIdentifier(SystemIdentifier, sizeof(SystemIdentifier));
        FldrCreateSystemKey(&SystemKey, SystemIdentifier);
    }
#else
    #error Please define a system key for your architecture
#endif

    /* Detect buses */
    DetectInternal(SystemKey, &BusNumber);
    // TODO: DetectPciBios
    DetectAcpiBios(SystemKey, &BusNumber);

    TRACE("DetectHardware() Done\n");
    return SystemKey;
}
