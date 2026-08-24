/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort main driver functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"

#define NDEBUG
#include <debug.h>

#define NDEBUG_USBPORT_CORE
#define NDEBUG_USBPORT_INTERRUPT
#define NDEBUG_USBPORT_TIMER
#include "usbdebug.h"

#if DBG
#ifndef USBPORT_DBG_BOUNCE_TRACE
#define USBPORT_DBG_BOUNCE_TRACE 0
#endif
#ifndef USBPORT_DBG_TIMER_TRACE
#define USBPORT_DBG_TIMER_TRACE 0
#endif
#if USBPORT_DBG_BOUNCE_TRACE
#define USBPORT_BOUNCE_TRACE DPRINT_CORE
#else
#define USBPORT_BOUNCE_TRACE(...) do { } while (0)
#endif
#if USBPORT_DBG_TIMER_TRACE
#define USBPORT_TIMER_TRACE DPRINT_CORE
#else
#define USBPORT_TIMER_TRACE(...) do { } while (0)
#endif
#else
#define USBPORT_BOUNCE_TRACE(...) do { } while (0)
#define USBPORT_TIMER_TRACE(...) do { } while (0)
#endif

LIST_ENTRY USBPORT_MiniPortDrivers = {NULL, NULL};
LIST_ENTRY USBPORT_USB1FdoList = {NULL, NULL};
LIST_ENTRY USBPORT_USB2FdoList = {NULL, NULL};

KSPIN_LOCK USBPORT_SpinLock;
BOOLEAN USBPORT_Initialized = FALSE;

static volatile LONG USBPORT_DuplicateDoneTransferCount = 0;

#define USBPORT_MAP_CALLBACK_NONE 0
#define USBPORT_MAP_CALLBACK_DONE 1
#define USBPORT_MAP_CALLBACK_PENDING 2

static
VOID
USBPORT_CleanupTransferOnBadUrb(IN PUSBPORT_TRANSFER Transfer,
                                IN USBD_STATUS TransferStatus);

VOID
USBPORT_ReferenceRootHubCallbackData(IN PUSBPORT_ROOT_HUB_CALLBACK_DATA CallbackData)
{
    if (!CallbackData)
        return;

    InterlockedIncrement(&CallbackData->RefCount);
}

VOID
USBPORT_DereferenceRootHubCallbackData(IN PUSBPORT_ROOT_HUB_CALLBACK_DATA CallbackData)
{
    if (!CallbackData)
        return;

    if (InterlockedDecrement(&CallbackData->RefCount) == 0)
    {
        ExFreePoolWithTag(CallbackData, USB_PORT_TAG);
    }
}

VOID
USBPORT_StopControllerTimer(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    BOOLEAN CancelTimer = FALSE;
    KIRQL OldIrql;

    if (!FdoExtension)
        return;

    KeAcquireSpinLock(&FdoExtension->TimerFlagsSpinLock, &OldIrql);

    if (FdoExtension->TimerFlags & USBPORT_TMFLAG_TIMER_QUEUED)
    {
        FdoExtension->TimerFlags &= ~USBPORT_TMFLAG_TIMER_QUEUED;
        CancelTimer = TRUE;
    }

    KeReleaseSpinLock(&FdoExtension->TimerFlagsSpinLock, OldIrql);

    if (CancelTimer)
    {
        /* A successful cancel proves that the timer cannot queue its DPC.
         * Otherwise, remove a DPC which expired but has not started yet. */
        if (KeCancelTimer(&FdoExtension->TimerObject) || KeRemoveQueueDpc(&FdoExtension->TimerDpc))
        {
            return;
        }

        /* The DPC was already dequeued and may be running. It observes the
         * cleared flag under TimerFlagsSpinLock and signals after its final
         * access to the embedded timer state. */
        KeWaitForSingleObject(&FdoExtension->TimerDpcEvent, Executive, KernelMode, FALSE, NULL);
    }
}

static
BOOLEAN
USBPORT_MdlNeedsBounce(IN PMDL Mdl,
                       IN SIZE_T TransferLength)
{
    PFN_NUMBER *PfnArray;
    ULONG PageCount;
    ULONG Index;

    if (!Mdl || TransferLength == 0)
        return FALSE;

    PfnArray = MmGetMdlPfnArray(Mdl);
    if (!PfnArray)
        return FALSE;

    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl),
                                               TransferLength);

    for (Index = 0; Index < PageCount; Index++)
    {
        ULONGLONG Physical = ((ULONGLONG)PfnArray[Index]) << PAGE_SHIFT;

        if ((Physical >> 32) != 0)
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
USBPORT_UhciNeedsPacketAlignedBounce(IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
                                     IN PUSBPORT_TRANSFER Transfer,
                                     IN PMDL Mdl,
                                     IN SIZE_T TransferLength)
{
    PUSBPORT_ENDPOINT Endpoint;
    ULONG MaxPacketSize;
    ULONG_PTR Offset;
    SIZE_T FirstSegmentLength;

    if (!FdoExtension ||
        !FdoExtension->MiniPortInterface ||
        FdoExtension->MiniPortInterface->Packet.MiniPortVersion != USB_MINIPORT_VERSION_UHCI)
    {
        return FALSE;
    }

    Endpoint = Transfer->Endpoint;
    if (!Endpoint ||
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        return FALSE;
    }

    MaxPacketSize = Endpoint->EndpointProperties.TotalMaxPacketSize;
    if (MaxPacketSize == 0 || TransferLength <= MaxPacketSize)
        return FALSE;

    Offset = (ULONG_PTR)MmGetMdlVirtualAddress(Mdl) & (PAGE_SIZE - 1);
    FirstSegmentLength = PAGE_SIZE - Offset;

    if (FirstSegmentLength >= TransferLength)
        return FALSE;

    return (FirstSegmentLength % MaxPacketSize) != 0 ||
           (PAGE_SIZE % MaxPacketSize) != 0;
}

NTSTATUS
USBPORT_SetupTransferBounceBuffer(IN PDEVICE_OBJECT FdoDevice,
                                  IN PUSBPORT_TRANSFER Transfer,
                                  IN PURB Urb)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    SIZE_T TransferLength = Transfer->TransferParameters.TransferBufferLength;
    PMDL OriginalMdl = Transfer->TransferBufferMDL;
    BOOLEAN NeedsBounce;
    PVOID OriginalVa;
    PUSBPORT_COMMON_BUFFER_HEADER CommonBuffer;
    PMDL BounceMdl;

    if (!OriginalMdl || TransferLength == 0)
        return STATUS_SUCCESS;

    /*
     * Root-hub transfers are serviced in software and never reach the host
     * controller's DMA engine.
     */
    if (Transfer->Endpoint &&
        (Transfer->Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0))
    {
        return STATUS_SUCCESS;
    }

    /*
     * On amd64/q35, nonpaged allocations routinely land above 4GB. xHCI
     * controllers are expected to handle 64-bit DMA, so avoid forcing bounce
     * buffering (and copy-back) for xHCI miniports.
     */
    FdoExtension = FdoDevice ? FdoDevice->DeviceExtension : NULL;
    if (FdoExtension &&
        FdoExtension->MiniPortInterface &&
        FdoExtension->MiniPortInterface->Packet.MiniPortVersion == USB_MINIPORT_VERSION_XHCI)
    {
        return STATUS_SUCCESS;
    }

    NeedsBounce = USBPORT_MdlNeedsBounce(OriginalMdl, TransferLength);
    if (!NeedsBounce)
    {
        NeedsBounce = USBPORT_UhciNeedsPacketAlignedBounce(FdoExtension,
                                                           Transfer,
                                                           OriginalMdl,
                                                           TransferLength);
    }

    if (!NeedsBounce)
        return STATUS_SUCCESS;

    OriginalVa = MmGetSystemAddressForMdlSafe(OriginalMdl, NormalPagePriority);
    if (!OriginalVa)
        return STATUS_INSUFFICIENT_RESOURCES;

    CommonBuffer = USBPORT_AllocateCommonBuffer(FdoDevice, TransferLength);
    if (!CommonBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    BounceMdl = IoAllocateMdl((PVOID)CommonBuffer->VirtualAddress,
                              (ULONG)TransferLength,
                              FALSE,
                              FALSE,
                              NULL);
    if (!BounceMdl)
    {
        USBPORT_FreeCommonBuffer(FdoDevice, CommonBuffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MmBuildMdlForNonPagedPool(BounceMdl);

    Transfer->BounceBuffer = CommonBuffer;
    Transfer->BounceBufferLength = TransferLength;
    Transfer->BounceOriginalVa = OriginalVa;
    Transfer->BounceOriginalMdl = OriginalMdl;
    Transfer->BounceOriginalBuffer = Urb->UrbControlTransfer.TransferBuffer;
    Transfer->BounceMdl = BounceMdl;

    Transfer->TransferBufferMDL = BounceMdl;
    Urb->UrbControlTransfer.TransferBufferMDL = BounceMdl;
    Urb->UrbControlTransfer.TransferBuffer = (PVOID)CommonBuffer->VirtualAddress;

    if (Transfer->Direction == USBPORT_DMA_DIRECTION_TO_DEVICE)
    {
        RtlCopyMemory((PVOID)CommonBuffer->VirtualAddress,
                      OriginalVa,
                      TransferLength);
    }
    else
    {
        RtlZeroMemory((PVOID)CommonBuffer->VirtualAddress, TransferLength);
    }

    Transfer->Flags |= TRANSFER_FLAG_BOUNCE;

    USBPORT_BOUNCE_TRACE("USBPORT: using bounce buffer %p PA=0x%08lx len=%lu (transfer=%p)\n",
                         (PVOID)CommonBuffer->VirtualAddress,
                         CommonBuffer->PhysicalAddress,
                         (ULONG)TransferLength,
                         Transfer);

    return STATUS_SUCCESS;
}

PDEVICE_OBJECT
NTAPI
USBPORT_FindUSB2Controller(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_DEVICE_EXTENSION USB2FdoExtension;
    KIRQL OldIrql;
    PLIST_ENTRY USB2FdoEntry;
    PDEVICE_OBJECT USB2FdoDevice = NULL;

    DPRINT("USBPORT_FindUSB2Controller: FdoDevice - %p\n", FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&USBPORT_SpinLock, &OldIrql);

    USB2FdoEntry = USBPORT_USB2FdoList.Flink;

    while (USB2FdoEntry && USB2FdoEntry != &USBPORT_USB2FdoList)
    {
        USB2FdoExtension = CONTAINING_RECORD(USB2FdoEntry,
                                             USBPORT_DEVICE_EXTENSION,
                                             ControllerLink);

        if (USB2FdoExtension->BusNumber == FdoExtension->BusNumber &&
            USB2FdoExtension->PciDeviceNumber == FdoExtension->PciDeviceNumber)
        {
            USB2FdoDevice = USB2FdoExtension->CommonExtension.SelfDevice;
            break;
        }

        USB2FdoEntry = USB2FdoEntry->Flink;
    }

    KeReleaseSpinLock(&USBPORT_SpinLock, OldIrql);

    return USB2FdoDevice;
}

VOID
NTAPI
USBPORT_AddUSB1Fdo(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;

    DPRINT("USBPORT_AddUSB1Fdo: FdoDevice - %p\n", FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;
    FdoExtension->Flags |= USBPORT_FLAG_REGISTERED_FDO;

    ExInterlockedInsertTailList(&USBPORT_USB1FdoList,
                                &FdoExtension->ControllerLink,
                                &USBPORT_SpinLock);
}

VOID
NTAPI
USBPORT_AddUSB2Fdo(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;

    DPRINT("USBPORT_AddUSB2Fdo: FdoDevice - %p\n", FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;
    FdoExtension->Flags |= USBPORT_FLAG_REGISTERED_FDO;

    ExInterlockedInsertTailList(&USBPORT_USB2FdoList,
                                &FdoExtension->ControllerLink,
                                &USBPORT_SpinLock);
}

VOID
NTAPI
USBPORT_RemoveUSBxFdo(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    KIRQL OldIrql;

    DPRINT("USBPORT_RemoveUSBxFdo: FdoDevice - %p\n", FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&USBPORT_SpinLock, &OldIrql);
    RemoveEntryList(&FdoExtension->ControllerLink);
    KeReleaseSpinLock(&USBPORT_SpinLock, OldIrql);

    FdoExtension->Flags &= ~USBPORT_FLAG_REGISTERED_FDO;

    FdoExtension->ControllerLink.Flink = NULL;
    FdoExtension->ControllerLink.Blink = NULL;
}

BOOLEAN
NTAPI
USBPORT_IsCompanionFdoExtension(IN PDEVICE_OBJECT USB2FdoDevice,
                                IN PUSBPORT_DEVICE_EXTENSION USB1FdoExtension)
{
    PUSBPORT_DEVICE_EXTENSION USB2FdoExtension;

    DPRINT("USBPORT_IsCompanionFdoExtension: USB2Fdo - %p, USB1FdoExtension - %p\n",
           USB2FdoDevice,
           USB1FdoExtension);

    USB2FdoExtension = USB2FdoDevice->DeviceExtension;

    return USB2FdoExtension->BusNumber == USB1FdoExtension->BusNumber &&
           USB2FdoExtension->PciDeviceNumber == USB1FdoExtension->PciDeviceNumber;
}

static
LONG
USBPORT_CompareCompanionExtensions(IN PUSBPORT_DEVICE_EXTENSION Left,
                                   IN PUSBPORT_DEVICE_EXTENSION Right)
{
    if (Left->BusNumber != Right->BusNumber)
        return (Left->BusNumber > Right->BusNumber) ? 1 : -1;

    if (Left->PciDeviceNumber != Right->PciDeviceNumber)
        return (Left->PciDeviceNumber > Right->PciDeviceNumber) ? 1 : -1;

    if (Left->PciFunctionNumber != Right->PciFunctionNumber)
        return (Left->PciFunctionNumber > Right->PciFunctionNumber) ? 1 : -1;

    return 0;
}

PDEVICE_RELATIONS
NTAPI
USBPORT_FindCompanionControllers(IN PDEVICE_OBJECT USB2FdoDevice,
                                 IN BOOLEAN IsObRefer,
                                 IN BOOLEAN IsFDOsReturned)
{
    PLIST_ENTRY USB1FdoList;
    PUSBPORT_DEVICE_EXTENSION USB1FdoExtension;
    ULONG NumControllers = 0;
    PDEVICE_OBJECT * Entry;
    PDEVICE_RELATIONS ControllersList = NULL;
    KIRQL OldIrql;

    DPRINT("USBPORT_FindCompanionControllers: USB2Fdo - %p, IsObRefer - %x, IsFDOs - %x\n",
           USB2FdoDevice,
           IsObRefer,
           IsFDOsReturned);

    KeAcquireSpinLock(&USBPORT_SpinLock, &OldIrql);

    USB1FdoList = USBPORT_USB1FdoList.Flink;

    while (USB1FdoList && USB1FdoList != &USBPORT_USB1FdoList)
    {
        USB1FdoExtension = CONTAINING_RECORD(USB1FdoList,
                                             USBPORT_DEVICE_EXTENSION,
                                             ControllerLink);

        if (USB1FdoExtension->Flags & USBPORT_FLAG_COMPANION_HC &&
            USBPORT_IsCompanionFdoExtension(USB2FdoDevice, USB1FdoExtension))
        {
            ++NumControllers;
        }

        USB1FdoList = USB1FdoExtension->ControllerLink.Flink;
    }

    DPRINT("USBPORT_FindCompanionControllers: NumControllers - %x\n",
           NumControllers);

    if (!NumControllers)
    {
        goto Exit;
    }

    ControllersList = ExAllocatePoolWithTag(NonPagedPool,
                                            NumControllers * sizeof(DEVICE_RELATIONS),
                                            USB_PORT_TAG);

    if (!ControllersList)
    {
        goto Exit;
    }

    RtlZeroMemory(ControllersList, NumControllers * sizeof(DEVICE_RELATIONS));

    ControllersList->Count = NumControllers;

    USB1FdoList = USBPORT_USB1FdoList.Flink;

    Entry = &ControllersList->Objects[0];

    if (NumControllers)
    {
        PUSBPORT_DEVICE_EXTENSION *Matched;
        ULONG MatchedIndex = 0;
        ULONG ix, jx;

        Matched = ExAllocatePoolWithTag(NonPagedPool,
                                        NumControllers * sizeof(PUSBPORT_DEVICE_EXTENSION),
                                        USB_PORT_TAG);
        if (!Matched)
        {
            ExFreePoolWithTag(ControllersList, USB_PORT_TAG);
            ControllersList = NULL;
            goto Exit;
        }

        USB1FdoList = USBPORT_USB1FdoList.Flink;
        while (USB1FdoList && USB1FdoList != &USBPORT_USB1FdoList)
        {
            USB1FdoExtension = CONTAINING_RECORD(USB1FdoList,
                                                 USBPORT_DEVICE_EXTENSION,
                                                 ControllerLink);

            if (USB1FdoExtension->Flags & USBPORT_FLAG_COMPANION_HC &&
                USBPORT_IsCompanionFdoExtension(USB2FdoDevice, USB1FdoExtension))
            {
                Matched[MatchedIndex++] = USB1FdoExtension;
            }

            USB1FdoList = USB1FdoExtension->ControllerLink.Flink;
        }

        for (ix = 0; ix < MatchedIndex; ++ix)
        {
            for (jx = ix + 1; jx < MatchedIndex; ++jx)
            {
                if (USBPORT_CompareCompanionExtensions(Matched[ix], Matched[jx]) > 0)
                {
                    PUSBPORT_DEVICE_EXTENSION Temp = Matched[ix];
                    Matched[ix] = Matched[jx];
                    Matched[jx] = Temp;
                }
            }
        }

        for (ix = 0; ix < MatchedIndex; ++ix)
        {
            PDEVICE_OBJECT TargetObject;

            if (IsFDOsReturned)
            {
                TargetObject = Matched[ix]->CommonExtension.SelfDevice;
            }
            else
            {
                TargetObject = Matched[ix]->CommonExtension.LowerPdoDevice;
            }

            if (IsObRefer && TargetObject)
            {
                ObReferenceObject(TargetObject);
            }

            *Entry = TargetObject;
            ++Entry;
        }

        ExFreePoolWithTag(Matched, USB_PORT_TAG);
    }

Exit:

    KeReleaseSpinLock(&USBPORT_SpinLock, OldIrql);

    return ControllersList;
}

MPSTATUS
NTAPI
USBPORT_NtStatusToMpStatus(NTSTATUS NtStatus)
{
    DPRINT("USBPORT_NtStatusToMpStatus: NtStatus - %x\n", NtStatus);

    if (NtStatus == STATUS_SUCCESS)
    {
        return MP_STATUS_SUCCESS;
    }
    else
    {
        return MP_STATUS_UNSUCCESSFUL;
    }
}

NTSTATUS
NTAPI
USBPORT_SetRegistryKeyValue(IN PDEVICE_OBJECT DeviceObject,
                            IN BOOL UseDriverKey,
                            IN ULONG Type,
                            IN PCWSTR ValueNameString,
                            IN PVOID Data,
                            IN ULONG DataSize)
{
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    NTSTATUS Status;

    DPRINT("USBPORT_SetRegistryKeyValue: ValueNameString - %S\n",
           ValueNameString);

    if (UseDriverKey)
    {
        Status = IoOpenDeviceRegistryKey(DeviceObject,
                                         PLUGPLAY_REGKEY_DRIVER,
                                         STANDARD_RIGHTS_ALL,
                                         &KeyHandle);
    }
    else
    {
        Status = IoOpenDeviceRegistryKey(DeviceObject,
                                         PLUGPLAY_REGKEY_DEVICE,
                                         STANDARD_RIGHTS_ALL,
                                         &KeyHandle);
    }

    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, ValueNameString);

        Status = ZwSetValueKey(KeyHandle,
                               &ValueName,
                               0,
                               Type,
                               Data,
                               DataSize);

        ZwClose(KeyHandle);
    }

    return Status;
}

NTSTATUS
NTAPI
USBPORT_GetRegistryKeyValueFullInfo(IN PDEVICE_OBJECT FdoDevice,
                                    IN PDEVICE_OBJECT PdoDevice,
                                    IN BOOL UseDriverKey,
                                    IN PCWSTR SourceString,
                                    IN ULONG LengthStr,
                                    IN PVOID Buffer,
                                    IN ULONG BufferLength)
{
    NTSTATUS Status;
    PKEY_VALUE_FULL_INFORMATION KeyValue;
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    ULONG LengthKey;

    DPRINT("USBPORT_GetRegistryKeyValue: UseDriverKey - %x, SourceString - %S, LengthStr - %x, Buffer - %p, BufferLength - %x\n",
           UseDriverKey,
           SourceString,
           LengthStr,
           Buffer,
           BufferLength);

    if (UseDriverKey)
    {
        Status = IoOpenDeviceRegistryKey(PdoDevice,
                                         PLUGPLAY_REGKEY_DRIVER,
                                         STANDARD_RIGHTS_ALL,
                                         &KeyHandle);
    }
    else
    {
        Status = IoOpenDeviceRegistryKey(PdoDevice,
                                         PLUGPLAY_REGKEY_DEVICE,
                                         STANDARD_RIGHTS_ALL,
                                         &KeyHandle);
    }

    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&ValueName, SourceString);

        LengthKey = sizeof(KEY_VALUE_FULL_INFORMATION) +
                    LengthStr +
                    BufferLength;

        KeyValue = ExAllocatePoolWithTag(PagedPool,
                                         LengthKey,
                                         USB_PORT_TAG);

        if (KeyValue)
        {
            RtlZeroMemory(KeyValue, LengthKey);

            Status = ZwQueryValueKey(KeyHandle,
                                     &ValueName,
                                     KeyValueFullInformation,
                                     KeyValue,
                                     LengthKey,
                                     &LengthKey);

            if (NT_SUCCESS(Status))
            {
                RtlCopyMemory(Buffer,
                              (PUCHAR)KeyValue + KeyValue->DataOffset,
                              BufferLength);
            }

            ExFreePoolWithTag(KeyValue, USB_PORT_TAG);
        }

        ZwClose(KeyHandle);
    }

    return Status;
}

MPSTATUS
NTAPI
USBPORT_GetMiniportRegistryKeyValue(IN PVOID MiniPortExtension,
                                    IN BOOL UseDriverKey,
                                    IN PCWSTR SourceString,
                                    IN SIZE_T LengthStr,
                                    IN PVOID Buffer,
                                    IN SIZE_T BufferLength)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT FdoDevice;
    NTSTATUS Status;

    DPRINT("USBPORT_GetMiniportRegistryKeyValue: MiniPortExtension - %p, UseDriverKey - %x, SourceString - %S, LengthStr - %x, Buffer - %p, BufferLength - %x\n",
           MiniPortExtension,
           UseDriverKey,
           SourceString,
           LengthStr,
           Buffer,
           BufferLength);

    FdoExtension = (PUSBPORT_DEVICE_EXTENSION)((ULONG_PTR)MiniPortExtension -
                                               sizeof(USBPORT_DEVICE_EXTENSION));

    FdoDevice = FdoExtension->CommonExtension.SelfDevice;

    Status = USBPORT_GetRegistryKeyValueFullInfo(FdoDevice,
                                                 FdoExtension->CommonExtension.LowerPdoDevice,
                                                 UseDriverKey,
                                                 SourceString,
                                                 LengthStr,
                                                 Buffer,
                                                 BufferLength);

    return USBPORT_NtStatusToMpStatus(Status);
}

NTSTATUS
NTAPI
USBPORT_GetSetConfigSpaceData(IN PDEVICE_OBJECT FdoDevice,
                              IN BOOLEAN IsReadData,
                              IN PVOID Buffer,
                              IN ULONG Offset,
                              IN ULONG Length)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    ULONG BytesReadWrite;

    DPRINT("USBPORT_GetSetConfigSpaceData ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    /* No PCI parent (e.g. an ACPI-enumerated controller) means there is no
     * config space and BusInterface is not populated. Fail gracefully instead
     * of dereferencing a NULL GetBusData/SetBusData and bugchecking. */
    if (!FdoExtension->BusInterface.GetBusData ||
        !FdoExtension->BusInterface.SetBusData)
    {
        if (IsReadData)
            RtlZeroMemory(Buffer, Length);
        return STATUS_NOT_SUPPORTED;
    }

    BytesReadWrite = Length;

    if (IsReadData)
    {
        RtlZeroMemory(Buffer, Length);

        BytesReadWrite = (*FdoExtension->BusInterface.GetBusData)
                          (FdoExtension->BusInterface.Context,
                           PCI_WHICHSPACE_CONFIG,
                           Buffer,
                           Offset,
                           Length);
    }
    else
    {
        BytesReadWrite = (*FdoExtension->BusInterface.SetBusData)
                          (FdoExtension->BusInterface.Context,
                           PCI_WHICHSPACE_CONFIG,
                           Buffer,
                           Offset,
                           Length);
    }

    if (BytesReadWrite == Length)
    {
        return STATUS_SUCCESS;
    }

    return STATUS_UNSUCCESSFUL;
}

MPSTATUS
NTAPI
USBPORT_ReadWriteConfigSpace(IN PVOID MiniPortExtension,
                             IN BOOLEAN IsReadData,
                             IN PVOID Buffer,
                             IN ULONG Offset,
                             IN ULONG Length)
{
    NTSTATUS Status;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT FdoDevice;

    DPRINT("USBPORT_ReadWriteConfigSpace: ...\n");

    //FdoExtension->MiniPortExt = (PVOID)((ULONG_PTR)FdoExtension + sizeof(USBPORT_DEVICE_EXTENSION));
    FdoExtension = (PUSBPORT_DEVICE_EXTENSION)((ULONG_PTR)MiniPortExtension -
                                               sizeof(USBPORT_DEVICE_EXTENSION));

    FdoDevice = FdoExtension->CommonExtension.SelfDevice;

    Status = USBPORT_GetSetConfigSpaceData(FdoDevice,
                                           IsReadData,
                                           Buffer,
                                           Offset,
                                           Length);

    return USBPORT_NtStatusToMpStatus(Status);
}

NTSTATUS
NTAPI
USBPORT_USBDStatusToNtStatus(IN PURB Urb,
                             IN USBD_STATUS USBDStatus)
{
    NTSTATUS Status;

    if (USBD_ERROR(USBDStatus))
    {
        DPRINT_CORE("USBPORT_USBDStatusToNtStatus: Urb - %p, USBDStatus - %x\n",
                Urb,
                USBDStatus);
    }

    if (Urb)
        Urb->UrbHeader.Status = USBDStatus;

    switch (USBDStatus)
    {
        case USBD_STATUS_SUCCESS:
            Status = STATUS_SUCCESS;
            break;

        case USBD_STATUS_INSUFFICIENT_RESOURCES:
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;

        case USBD_STATUS_DEVICE_GONE:
            Status = STATUS_DEVICE_NOT_CONNECTED;
            break;

        case USBD_STATUS_CANCELED:
            Status = STATUS_CANCELLED;
            break;

        case USBD_STATUS_NOT_SUPPORTED:
            Status = STATUS_NOT_SUPPORTED;
            break;

        case USBD_STATUS_INVALID_URB_FUNCTION:
        case USBD_STATUS_INVALID_PARAMETER:
        case USBD_STATUS_INVALID_PIPE_HANDLE:
        case USBD_STATUS_BAD_START_FRAME:
            Status = STATUS_INVALID_PARAMETER;
            break;

        default:
            if (USBD_ERROR(USBDStatus))
                Status = STATUS_UNSUCCESSFUL;
            else
                Status = STATUS_SUCCESS;

            break;
    }

    return Status;
}

NTSTATUS
NTAPI
USBPORT_Wait(IN PVOID MiniPortExtension,
             IN ULONG Milliseconds)
{
    LARGE_INTEGER Interval = {{0, 0}};

    DPRINT("USBPORT_Wait: Milliseconds - %x\n", Milliseconds);
    Interval.QuadPart -= 10000 * Milliseconds + (KeQueryTimeIncrement() - 1);
    return KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

VOID
NTAPI
USBPORT_MiniportInterrupts(IN PDEVICE_OBJECT FdoDevice,
                           IN BOOLEAN IsEnable)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    BOOLEAN IsLock;
    KIRQL OldIrql;

    DPRINT_INT("USBPORT_MiniportInterrupts: IsEnable - %p\n", IsEnable);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    IsLock = (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_NOT_LOCK_INT) == 0;

    if (IsLock)
        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

    if (IsEnable)
    {
        FdoExtension->Flags |= USBPORT_FLAG_INTERRUPT_ENABLED;
        Packet->EnableInterrupts(FdoExtension->MiniPortExt);
    }
    else
    {
        Packet->DisableInterrupts(FdoExtension->MiniPortExt);
        FdoExtension->Flags &= ~USBPORT_FLAG_INTERRUPT_ENABLED;
    }

    if (IsLock)
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
}

static BOOLEAN
USBPORT_AcquireIsrDpcRundown(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    LONG State;

    for (;;)
    {
        State = InterlockedCompareExchange(&FdoExtension->IsrDpcRundownState, 0, 0);
        if (State < 0)
            return FALSE;

        ASSERT(State != MAXLONG);
        if (InterlockedCompareExchange(&FdoExtension->IsrDpcRundownState,
                                       State + 1,
                                       State) == State)
        {
            return TRUE;
        }
    }
}

static VOID
USBPORT_ReleaseIsrDpcRundown(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    LONG State;

    State = InterlockedDecrement(&FdoExtension->IsrDpcRundownState);
    /* A failed queue can release at DIRQL. Stop rechecks the state after
     * KeSynchronizeExecution and publishes idle from a wait-safe IRQL. */
    if (State == USBPORT_ISR_DPC_RUNDOWN_STOPPING &&
        KeGetCurrentIrql() <= DISPATCH_LEVEL)
    {
        KeSetEvent(&FdoExtension->IsrDpcRundownEvent, IO_NO_INCREMENT, FALSE);
    }
}

static BOOLEAN
USBPORT_QueueIsrDpc(IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
                    IN PVOID SystemArgument1,
                    IN PVOID SystemArgument2)
{
    if (!USBPORT_AcquireIsrDpcRundown(FdoExtension))
        return FALSE;

    if (KeInsertQueueDpc(&FdoExtension->IsrDpc, SystemArgument1, SystemArgument2))
        return TRUE;

    USBPORT_ReleaseIsrDpcRundown(FdoExtension);
    return FALSE;
}

static BOOLEAN NTAPI
USBPORT_IsrDpcSynchronizeRoutine(IN PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return TRUE;
}

VOID
USBPORT_StartIsrDpcRundown(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    ASSERT(InterlockedCompareExchange(&FdoExtension->IsrDpcRundownState, 0, 0) ==
           USBPORT_ISR_DPC_RUNDOWN_STOPPING);
    KeClearEvent(&FdoExtension->IsrDpcRundownEvent);
    InterlockedExchange(&FdoExtension->IsrDpcRundownState, 0);
}

VOID
USBPORT_StopIsrDpcRundown(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    LONG State;
    ULONG MessageIndex;

    for (;;)
    {
        State = InterlockedCompareExchange(&FdoExtension->IsrDpcRundownState, 0, 0);
        if (State < 0)
            break;

        if (InterlockedCompareExchange(&FdoExtension->IsrDpcRundownState,
                                       State | USBPORT_ISR_DPC_RUNDOWN_STOPPING,
                                       State) == State)
        {
            break;
        }
    }

    if (FdoExtension->InterruptMessageInfo)
    {
        for (MessageIndex = 0; MessageIndex < FdoExtension->InterruptMessageInfo->MessageCount; MessageIndex++)
        {
            PKINTERRUPT InterruptObject = FdoExtension->InterruptMessageInfo->MessageInfo[MessageIndex].InterruptObject;

            if (InterruptObject)
                KeSynchronizeExecution(InterruptObject, USBPORT_IsrDpcSynchronizeRoutine, FdoExtension);
        }
    }
    else if (FdoExtension->InterruptObject)
    {
        KeSynchronizeExecution(FdoExtension->InterruptObject, USBPORT_IsrDpcSynchronizeRoutine, FdoExtension);
    }

    if (KeRemoveQueueDpc(&FdoExtension->IsrDpc))
        USBPORT_ReleaseIsrDpcRundown(FdoExtension);

    if (InterlockedCompareExchange(&FdoExtension->IsrDpcRundownState, 0, 0) ==
        USBPORT_ISR_DPC_RUNDOWN_STOPPING)
    {
        KeSetEvent(&FdoExtension->IsrDpcRundownEvent, IO_NO_INCREMENT, FALSE);
    }

    KeWaitForSingleObject(&FdoExtension->IsrDpcRundownEvent, Executive, KernelMode, FALSE, NULL);
    ASSERT(InterlockedCompareExchange(&FdoExtension->IsrDpcRundownState, 0, 0) ==
           USBPORT_ISR_DPC_RUNDOWN_STOPPING);
}

VOID
NTAPI
USBPORT_SoftInterruptDpc(IN PRKDPC Dpc,
                         IN PVOID DeferredContext,
                         IN PVOID SystemArgument1,
                         IN PVOID SystemArgument2)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;

    DPRINT_INT("USBPORT_SoftInterruptDpc: ...\n");

    FdoDevice = DeferredContext;
    FdoExtension = FdoDevice->DeviceExtension;

    if (!USBPORT_QueueIsrDpc(FdoExtension, NULL, (PVOID)1))
    {
        InterlockedDecrement(&FdoExtension->IsrDpcCounter);
    }
}

VOID
NTAPI
USBPORT_SoftInterrupt(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    LARGE_INTEGER DueTime = {{0, 0}};

    DPRINT_INT("USBPORT_SoftInterrupt: ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeInitializeTimer(&FdoExtension->TimerSoftInterrupt);

    KeInitializeDpc(&FdoExtension->SoftInterruptDpc,
                    USBPORT_SoftInterruptDpc,
                    FdoDevice);

    DueTime.QuadPart -= 10000 + (KeQueryTimeIncrement() - 1);

    KeSetTimer(&FdoExtension->TimerSoftInterrupt,
               DueTime,
               &FdoExtension->SoftInterruptDpc);
}

VOID
NTAPI
USBPORT_InvalidateControllerHandler(IN PDEVICE_OBJECT FdoDevice,
                                    IN ULONG Type)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;

    DPRINT_CORE("USBPORT_InvalidateControllerHandler: Invalidate Type - %x\n",
                Type);

    FdoExtension = FdoDevice->DeviceExtension;

    switch (Type)
    {
        case USBPORT_INVALIDATE_CONTROLLER_RESET:
            DPRINT_CORE("USBPORT_InvalidateControllerHandler: INVALIDATE_CONTROLLER_RESET\n");
            USBPORT_SignalTransportChange(FdoExtension,
                                          USB_REGISTER_FOR_TRANSPORT_LATENCY_CHANGE |
                                          USB_REGISTER_FOR_TRANSPORT_BANDWIDTH_CHANGE);
            USBPORT_InvalidateTimeSyncGeneration(FdoExtension);
            break;

        case USBPORT_INVALIDATE_CONTROLLER_SURPRISE_REMOVE:
            DPRINT_CORE("USBPORT_InvalidateControllerHandler: INVALIDATE_CONTROLLER_SURPRISE_REMOVE\n");
            USBPORT_SignalTransportChange(FdoExtension,
                                          USB_REGISTER_FOR_TRANSPORT_LATENCY_CHANGE |
                                          USB_REGISTER_FOR_TRANSPORT_BANDWIDTH_CHANGE);
            USBPORT_InvalidateTimeSyncGeneration(FdoExtension);
            break;

        case USBPORT_INVALIDATE_CONTROLLER_SOFT_INTERRUPT:
            if (InterlockedIncrement(&FdoExtension->IsrDpcCounter))
            {
                InterlockedDecrement(&FdoExtension->IsrDpcCounter);
            }
            else
            {
                USBPORT_SoftInterrupt(FdoDevice);
            }
            break;
    }
}

ULONG
NTAPI
USBPORT_InvalidateController(IN PVOID MiniPortExtension,
                             IN ULONG Type)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT FdoDevice;

    DPRINT("USBPORT_InvalidateController: Invalidate Type - %x\n", Type);

    //FdoExtension->MiniPortExt = (PVOID)((ULONG_PTR)FdoExtension + sizeof(USBPORT_DEVICE_EXTENSION));
    FdoExtension = (PUSBPORT_DEVICE_EXTENSION)((ULONG_PTR)MiniPortExtension -
                                               sizeof(USBPORT_DEVICE_EXTENSION));
    FdoDevice = FdoExtension->CommonExtension.SelfDevice;

    USBPORT_InvalidateControllerHandler(FdoDevice, Type);

    return 0;
}

ULONG
NTAPI
USBPORT_NotifyDoubleBuffer(IN PVOID MiniPortExtension,
                           IN PVOID MiniPortTransfer,
                           IN PVOID Buffer,
                           IN SIZE_T Length)
{
    PUSBPORT_MINIPORT_TRANSFER_HEADER Header;
    PUSBPORT_TRANSFER Transfer;
    PMDL OriginalMdl;
    PVOID OriginalVa;
    SIZE_T RequestedLength;
    SIZE_T MaxCopy;
    SIZE_T Copied;

    if (!MiniPortTransfer || !Buffer || !Length)
        return 0;

    Header = USBPORT_GetMiniportTransferHeader(MiniPortTransfer);
    Transfer = Header->Transfer;

    if (!Transfer || Transfer->MiniportTransfer != MiniPortTransfer)
        return 0;

    if (Transfer->FdoDevice)
    {
        PUSBPORT_DEVICE_EXTENSION FdoExtension;

        FdoExtension = Transfer->FdoDevice->DeviceExtension;
        if (FdoExtension && FdoExtension->MiniPortExt != MiniPortExtension)
            return 0;
    }

    if (!Transfer->TransferBufferMDL)
        return 0;

    if (Transfer->Direction != USBPORT_DMA_DIRECTION_FROM_DEVICE)
        return 0;

    RequestedLength = Transfer->TransferParameters.TransferBufferLength;
    if (!RequestedLength)
        return 0;

    OriginalMdl = Transfer->TransferBufferMDL;
    OriginalVa = MmGetSystemAddressForMdlSafe(OriginalMdl, NormalPagePriority);
    if (!OriginalVa)
        return 0;

    MaxCopy = MmGetMdlByteCount(OriginalMdl);
    Copied = Length;
    if (Copied > RequestedLength)
        Copied = RequestedLength;
    if (Copied > MaxCopy)
        Copied = MaxCopy;

    RtlCopyMemory(OriginalVa, Buffer, Copied);

    return (ULONG)Copied;
}

VOID
NTAPI
USBPORT_ResetEndpointIdle(
    _In_ PUSBPORT_ENDPOINT Endpoint)
{
    if (!Endpoint)
        return;

    InterlockedAnd((PLONG)&Endpoint->Flags, ~(LONG)ENDPOINT_FLAG_IDLE);
}

VOID
NTAPI
USBPORT_WorkerRequestDpc(IN PRKDPC Dpc,
                         IN PVOID DeferredContext,
                         IN PVOID SystemArgument1,
                         IN PVOID SystemArgument2)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;

    DPRINT("USBPORT_WorkerRequestDpc: ...\n");

    FdoDevice = DeferredContext;
    FdoExtension = FdoDevice->DeviceExtension;

    if (!InterlockedIncrement(&FdoExtension->IsrDpcHandlerCounter))
    {
        USBPORT_DpcHandler(FdoDevice);
    }

    InterlockedDecrement(&FdoExtension->IsrDpcHandlerCounter);
}

VOID
NTAPI
USBPORT_DoneTransfer(IN PUSBPORT_TRANSFER Transfer)
{
    PUSBPORT_ENDPOINT          Endpoint;
    PDEVICE_OBJECT             FdoDevice;
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    PURB                       Urb;
    PIRP                       Irp;
    KIRQL                      CancelIrql;
    KIRQL                      OldIrql;

    DPRINT_CORE("USBPORT_DoneTransfer: Transfer - %p\n", Transfer);

    Endpoint = Transfer->Endpoint;
    FdoDevice = Transfer->FdoDevice;
    if (!FdoDevice && Endpoint)
        FdoDevice = Endpoint->FdoDevice;
    if (!FdoDevice)
    {
        DPRINT1("USBPORT_DoneTransfer: missing FdoDevice Transfer=%p\n", Transfer);
        USBPORT_CleanupTransferOnBadUrb(Transfer, Transfer->USBDStatus);
        return;
    }
    FdoExtension = FdoDevice->DeviceExtension;

    Urb = Transfer->Urb;
    Irp = Transfer->Irp;

if (!Urb)
    {
        DPRINT1("USBPORT_DoneTransfer: invalid Urb=%p Transfer=%p\n", Urb, Transfer);
        USBPORT_CleanupTransferOnBadUrb(Transfer, Transfer->USBDStatus);
        return;
    }

    DPRINT("USBPORT_DoneTransfer: Transfer=%p Endpoint=%p Urb=%p Irp=%p USBDStatus=0x%x CompLen=%lu\n",
           Transfer, Endpoint, Urb, Irp, Transfer->USBDStatus, Transfer->CompletedTransferLen);

    KeAcquireSpinLock(&FdoExtension->FlushTransferSpinLock, &OldIrql);

    if (Irp)
    {
        IoAcquireCancelSpinLock(&CancelIrql);
        IoSetCancelRoutine(Irp, NULL);
        IoReleaseCancelSpinLock(CancelIrql);

        USBPORT_RemoveActiveTransferIrp(FdoDevice, Irp);
    }

    KeReleaseSpinLock(&FdoExtension->FlushTransferSpinLock, OldIrql);

    USBPORT_USBDStatusToNtStatus(Transfer->Urb, Transfer->USBDStatus);
    USBPORT_CompleteTransfer(Urb, Urb->UrbHeader.Status);

    DPRINT_CORE("USBPORT_DoneTransfer: exit\n");
}

/* Limit per-DPC iteration count to prevent DPC watchdog timeout.
   HID completion callbacks synchronously re-submit transfers through
   the full USB stack, so each iteration can take ~40ms with debug serial. */
#define USBPORT_MAX_DONE_TRANSFERS_PER_DPC 16

VOID
NTAPI
USBPORT_FlushDoneTransfers(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PLIST_ENTRY DoneTransferList;
    PUSBPORT_TRANSFER Transfer;
    PUSBPORT_ENDPOINT Endpoint;
    ULONG TransferCount = 0;
    KIRQL OldIrql;
    BOOLEAN IsHasTransfers;
    ULONG Iterations = 0;

    DPRINT_CORE("USBPORT_FlushDoneTransfers: ...\n");

    FdoExtension = FdoDevice->DeviceExtension;
    DoneTransferList = &FdoExtension->DoneTransferList;

    while (TRUE)
    {
        KeAcquireSpinLock(&FdoExtension->DoneTransferSpinLock, &OldIrql);

        if (IsListEmpty(DoneTransferList))
            break;

        /* Yield after N transfers to prevent DPC timeout */
        if (Iterations >= USBPORT_MAX_DONE_TRANSFERS_PER_DPC)
        {
            KeReleaseSpinLock(&FdoExtension->DoneTransferSpinLock, OldIrql);
            KeInsertQueueDpc(&FdoExtension->TransferFlushDpc, NULL, NULL);
            return;
        }

        Transfer = CONTAINING_RECORD(DoneTransferList->Flink,
                                     USBPORT_TRANSFER,
                                     DoneLink);

        RemoveHeadList(DoneTransferList);
        /* NULL out DoneLink after removal for defensive hygiene, mirroring
         * what is done for TransferLink below. This prevents accidental
         * double-removal if the transfer is erroneously touched again. */
        Transfer->DoneLink.Flink = NULL;
        Transfer->DoneLink.Blink = NULL;
        KeReleaseSpinLock(&FdoExtension->DoneTransferSpinLock, OldIrql);

        Iterations++;

        if (Transfer)
        {
            Endpoint = Transfer->Endpoint;

            /*
             * Remove from TransferList under EndpointSpinLock. The transfer
             * was left on TransferList by QueueDoneTransfer to avoid a race
             * with concurrent TransferList iterators (AbortEndpoint,
             * DmaEndpointPaused). Now that we're in the DPC context and
             * about to complete, we can safely remove it.
             */
            if (Endpoint)
            {
                KeAcquireSpinLockAtDpcLevel(&Endpoint->EndpointSpinLock);
                if (Transfer->TransferLink.Flink != NULL &&
                    Transfer->TransferLink.Blink != NULL)
                {
                    RemoveEntryList(&Transfer->TransferLink);
                    Transfer->TransferLink.Flink = NULL;
                    Transfer->TransferLink.Blink = NULL;
                }
                KeReleaseSpinLockFromDpcLevel(&Endpoint->EndpointSpinLock);
            }
            else
            {
                if (Transfer->TransferLink.Flink != NULL &&
                    Transfer->TransferLink.Blink != NULL)
                {
                    RemoveEntryList(&Transfer->TransferLink);
                    Transfer->TransferLink.Flink = NULL;
                    Transfer->TransferLink.Blink = NULL;
                }
            }

            if ((Transfer->Flags & TRANSFER_FLAG_SPLITED))
            {
                USBPORT_DoneSplitTransfer(Transfer);
            }
            else
            {
                USBPORT_DoneTransfer(Transfer);
            }

            TransferCount = 0;
            IsHasTransfers = USBPORT_EndpointHasQueuedTransfers(FdoDevice,
                                                                Endpoint,
                                                                &TransferCount);

            if (IsHasTransfers && !TransferCount)
            {
                /*
                 * The completion callback queued a new transfer on the
                 * PendingTransferList but no transfer is on the active
                 * TransferList yet.  Try to dispatch it directly.
                 * FlushPendingTransfers has its own reentrancy guard so
                 * this is safe even if called from a nested context.
                 *
                 * We also schedule a WorkerDPC as a fallback, but when
                 * called from within DpcHandler the WorkerDPC may be
                 * suppressed by the IsrDpcHandlerCounter check.  The
                 * direct FlushPendingTransfers call handles that case.
                 */
                USBPORT_FlushPendingTransfers(Endpoint);
                USBPORT_InvalidateEndpointHandler(FdoDevice,
                                                  Endpoint,
                                                  INVALIDATE_ENDPOINT_WORKER_DPC);
            }
        }
    }

    KeReleaseSpinLock(&FdoExtension->DoneTransferSpinLock, OldIrql);
}


VOID
NTAPI
USBPORT_TransferFlushDpc(IN PRKDPC Dpc,
                         IN PVOID DeferredContext,
                         IN PVOID SystemArgument1,
                         IN PVOID SystemArgument2)
{
    PDEVICE_OBJECT FdoDevice;

    DPRINT_CORE("USBPORT_TransferFlushDpc: ...\n");
    FdoDevice = DeferredContext;
    USBPORT_FlushDoneTransfers(FdoDevice);
}

BOOLEAN
NTAPI
USBPORT_QueueDoneTransfer(IN PUSBPORT_TRANSFER Transfer,
                          IN USBD_STATUS USBDStatus,
                          IN BOOLEAN CallerHoldsEndpointLock)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    PUSBPORT_ENDPOINT Endpoint;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    ULONG EndpointAddress = 0;
    ULONG DeviceAddress = 0;
    ULONG PortNumber = 0;
    LONG DuplicateCount;
    PVOID Caller;

    DPRINT_CORE("USBPORT_QueueDoneTransfer: Transfer - %p, USBDStatus - %p\n",
                Transfer,
                USBDStatus);

    if (InterlockedBitTestAndSet((PLONG)&Transfer->Flags, TRANSFER_FLAG_COMPLETED_BIT))
    {
        Endpoint = Transfer->Endpoint;
        DeviceHandle = Endpoint ? Endpoint->DeviceHandle : NULL;
        EndpointAddress = Endpoint ? Endpoint->EndpointProperties.EndpointAddress : 0;
        if (DeviceHandle)
        {
            DeviceAddress = DeviceHandle->DeviceAddress;
            PortNumber = DeviceHandle->PortNumber;
        }
        else if (Endpoint)
        {
            DeviceAddress = Endpoint->EndpointProperties.DeviceAddress;
            PortNumber = Endpoint->EndpointProperties.PortNumber;
        }

        DuplicateCount = InterlockedIncrement(&USBPORT_DuplicateDoneTransferCount);
        Caller = USBPORT_RETURN_ADDRESS();

        DPRINT1("USBPORT_QueueDoneTransfer: duplicate completion #%ld "
                "(Transfer=%p Endpoint=%p Fdo=%p Irp=%p Urb=%p "
                "Status=0x%08lx FirstStatus=0x%08lx Flags=0x%08lx Caller=%p "
                "CallerHoldsEndpointLock=%u DevAddr=%lu Port=%lu EpAddr=0x%02lx "
                "EpStateLast=%lu EpStateNext=%lu EpFlags=0x%08lx EpLock=%ld)\n",
                DuplicateCount,
                Transfer,
                Endpoint,
                Transfer->FdoDevice,
                Transfer->Irp,
                Transfer->Urb,
                (ULONG)USBDStatus,
                (ULONG)Transfer->USBDStatus,
                Transfer->Flags,
                Caller,
                CallerHoldsEndpointLock ? 1u : 0u,
                DeviceAddress,
                PortNumber,
                EndpointAddress,
                Endpoint ? Endpoint->StateLast : 0,
                Endpoint ? Endpoint->StateNext : 0,
                Endpoint ? Endpoint->Flags : 0,
                Endpoint ? Endpoint->LockCounter : 0);
        return FALSE;
    }

    FdoDevice = Transfer->FdoDevice;
    if (!FdoDevice && Transfer->Endpoint)
        FdoDevice = Transfer->Endpoint->FdoDevice;
    if (!FdoDevice)
    {
        DPRINT1("USBPORT_QueueDoneTransfer: missing FdoDevice Transfer=%p\n",
                Transfer);
        return FALSE;
    }
    FdoExtension = FdoDevice->DeviceExtension;

    /*
     * Do NOT remove from TransferList here. This function can be called
     * from contexts where EndpointSpinLock is already held (e.g., from
     * SubmitTransfer via synchronous miniport completion, or from
     * MapTransfer under EndpointSpinLock). Doing RemoveEntryList without
     * EndpointSpinLock races with TransferList iterators on other CPUs
     * (AbortEndpoint, DmaEndpointPaused/Active), causing list corruption.
     *
     * Instead, the transfer stays on TransferList and is also linked onto
     * DoneTransferList via the separate DoneLink. The DoneTransfer path
     * (FlushDoneTransfers -> DoneTransfer) removes from TransferList
     * under EndpointSpinLock before completing.
     */
    Transfer->USBDStatus = USBDStatus;

    ExInterlockedInsertTailList(&FdoExtension->DoneTransferList,
                                &Transfer->DoneLink,
                                &FdoExtension->DoneTransferSpinLock);

    DPRINT("USBPORT_QueueDoneTransfer: queued Transfer=%p Endpoint=%p USBDStatus=0x%x\n",
           Transfer, Transfer->Endpoint, USBDStatus);

    return KeInsertQueueDpc(&FdoExtension->TransferFlushDpc, NULL, NULL);
}

VOID
NTAPI
USBPORT_DpcHandler(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_ENDPOINT Endpoint;
    PLIST_ENTRY Entry;
    LIST_ENTRY List;
    LONG LockCounter;

    DPRINT_CORE("USBPORT_DpcHandler: ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    InitializeListHead(&List);

    KeAcquireSpinLockAtDpcLevel(&FdoExtension->EndpointListSpinLock);
    Entry = FdoExtension->EndpointList.Flink;

    while (Entry && Entry != &FdoExtension->EndpointList)
    {
        Endpoint = CONTAINING_RECORD(Entry,
                                     USBPORT_ENDPOINT,
                                     EndpointLink);

        LockCounter = InterlockedIncrement(&Endpoint->LockCounter);

        if (USBPORT_GetEndpointState(Endpoint) != USBPORT_ENDPOINT_ACTIVE ||
            LockCounter ||
            Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0)
        {
            InterlockedDecrement(&Endpoint->LockCounter);
        }
        else
        {
            InsertTailList(&List, &Endpoint->DispatchLink);

            if (Endpoint->WorkerLink.Flink && Endpoint->WorkerLink.Blink)
            {
                RemoveEntryList(&Endpoint->WorkerLink);

                Endpoint->WorkerLink.Flink = NULL;
                Endpoint->WorkerLink.Blink = NULL;
            }
        }

        Entry = Endpoint->EndpointLink.Flink;
    }

    KeReleaseSpinLockFromDpcLevel(&FdoExtension->EndpointListSpinLock);

    while (!IsListEmpty(&List))
    {
        Endpoint = CONTAINING_RECORD(List.Flink,
                                     USBPORT_ENDPOINT,
                                     DispatchLink);

        RemoveEntryList(List.Flink);
        Endpoint->DispatchLink.Flink = NULL;
        Endpoint->DispatchLink.Blink = NULL;

        USBPORT_EndpointWorker(Endpoint, TRUE);
        USBPORT_FlushPendingTransfers(Endpoint);
    }

    KeAcquireSpinLockAtDpcLevel(&FdoExtension->EndpointListSpinLock);

    if (!IsListEmpty(&FdoExtension->WorkerList))
    {
        USBPORT_SignalWorkerThread(FdoDevice);
    }

    KeReleaseSpinLockFromDpcLevel(&FdoExtension->EndpointListSpinLock);

    USBPORT_FlushDoneTransfers(FdoDevice);
}

VOID
NTAPI
USBPORT_IsrDpcHandler(IN PDEVICE_OBJECT FdoDevice,
                      IN BOOLEAN IsDpcHandler)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_ENDPOINT Endpoint;
    PLIST_ENTRY List;
    ULONG FrameNumber;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    DPRINT_CORE("USBPORT_IsrDpcHandler: IsDpcHandler - %x\n", IsDpcHandler);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (InterlockedIncrement(&FdoExtension->IsrDpcHandlerCounter))
    {
        USBPORT_QueueIsrDpc(FdoExtension, NULL, NULL);
        InterlockedDecrement(&FdoExtension->IsrDpcHandlerCounter);
        return;
    }

    for (List = ExInterlockedRemoveHeadList(&FdoExtension->EpStateChangeList,
                                            &FdoExtension->EpStateChangeSpinLock);
         List != NULL;
         List = ExInterlockedRemoveHeadList(&FdoExtension->EpStateChangeList,
                                            &FdoExtension->EpStateChangeSpinLock))
    {
        Endpoint = CONTAINING_RECORD(List,
                                     USBPORT_ENDPOINT,
                                     StateChangeLink);

        DPRINT_CORE("USBPORT_IsrDpcHandler: Endpoint - %p\n", Endpoint);

        KeAcquireSpinLockAtDpcLevel(&Endpoint->EndpointSpinLock);

        KeAcquireSpinLockAtDpcLevel(&FdoExtension->MiniportSpinLock);
        FrameNumber = Packet->Get32BitFrameNumber(FdoExtension->MiniPortExt);
        KeReleaseSpinLockFromDpcLevel(&FdoExtension->MiniportSpinLock);

        if (FrameNumber <= Endpoint->FrameNumber &&
            !(Endpoint->Flags & ENDPOINT_FLAG_NUKE))
        {
            KeReleaseSpinLockFromDpcLevel(&Endpoint->EndpointSpinLock);

            ExInterlockedInsertHeadList(&FdoExtension->EpStateChangeList,
                                        &Endpoint->StateChangeLink,
                                        &FdoExtension->EpStateChangeSpinLock);

            KeAcquireSpinLockAtDpcLevel(&FdoExtension->MiniportSpinLock);
            Packet->InterruptNextSOF(FdoExtension->MiniPortExt);
            KeReleaseSpinLockFromDpcLevel(&FdoExtension->MiniportSpinLock);

            break;
        }

        KeReleaseSpinLockFromDpcLevel(&Endpoint->EndpointSpinLock);

        KeAcquireSpinLockAtDpcLevel(&Endpoint->StateChangeSpinLock);
        Endpoint->StateLast = Endpoint->StateNext;
        KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);

        DPRINT_CORE("USBPORT_IsrDpcHandler: Endpoint->StateLast - %x\n",
                    Endpoint->StateLast);

        if (IsDpcHandler)
        {
            USBPORT_InvalidateEndpointHandler(FdoDevice,
                                              Endpoint,
                                              INVALIDATE_ENDPOINT_ONLY);
        }
        else
        {
            USBPORT_InvalidateEndpointHandler(FdoDevice,
                                              Endpoint,
                                              INVALIDATE_ENDPOINT_WORKER_THREAD);
        }
    }

    if (IsDpcHandler)
    {
        USBPORT_DpcHandler(FdoDevice);
    }

    InterlockedDecrement(&FdoExtension->IsrDpcHandlerCounter);
}

VOID
NTAPI
USBPORT_IsrDpc(IN PRKDPC Dpc,
               IN PVOID DeferredContext,
               IN PVOID SystemArgument1,
               IN PVOID SystemArgument2)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    BOOLEAN InterruptEnable;

    DPRINT_INT("USBPORT_IsrDpc: DeferredContext - %p, SystemArgument2 - %p\n",
               DeferredContext,
               SystemArgument2);

    FdoDevice = DeferredContext;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (SystemArgument2)
    {
        InterlockedDecrement(&FdoExtension->IsrDpcCounter);
    }

    KeAcquireSpinLockAtDpcLevel(&FdoExtension->MiniportInterruptsSpinLock);
    InterruptEnable = (FdoExtension->Flags & USBPORT_FLAG_INTERRUPT_ENABLED) ==
                       USBPORT_FLAG_INTERRUPT_ENABLED;

    Packet->InterruptDpc(FdoExtension->MiniPortExt, InterruptEnable);

    KeReleaseSpinLockFromDpcLevel(&FdoExtension->MiniportInterruptsSpinLock);

    if (FdoExtension->Flags & USBPORT_FLAG_HC_SUSPEND &&
        FdoExtension->TimerFlags & USBPORT_TMFLAG_WAKE)
    {
        USBPORT_CompletePdoWaitWake(FdoDevice);
    }
    else
    {
        USBPORT_IsrDpcHandler(FdoDevice, TRUE);
    }

    DPRINT_INT("USBPORT_IsrDpc: exit\n");
    USBPORT_ReleaseIsrDpcRundown(FdoExtension);
}

BOOLEAN
NTAPI
USBPORT_InterruptService(IN PKINTERRUPT Interrupt,
                         IN PVOID ServiceContext)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    BOOLEAN Result = FALSE;

    FdoDevice = ServiceContext;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    DPRINT_INT("USBPORT_InterruptService: FdoExtension[%p]->Flags - %08X\n",
           FdoExtension,
           FdoExtension->Flags);

    if (FdoExtension->Flags & USBPORT_FLAG_INTERRUPT_ENABLED &&
        FdoExtension->MiniPortFlags & USBPORT_MPFLAG_INTERRUPTS_ENABLED)
    {
        Result = Packet->InterruptService(FdoExtension->MiniPortExt);

        if (Result)
        {
            /*
             * Level-triggered controllers can keep the interrupt line asserted
             * until the miniport DPC drains the hardware source, so mask their
             * interrupt source before queuing the DPC. Keep MSI/MSI-X enabled:
             * masking an edge-triggered source can consume a later event during
             * the ISR-to-DPC window without generating a replacement edge.
             */
            if (!FdoExtension->MessageInterruptsEnabled)
                Packet->DisableInterrupts(FdoExtension->MiniPortExt);
            USBPORT_QueueIsrDpc(FdoExtension, NULL, NULL);
        }
    }

    DPRINT_INT("USBPORT_InterruptService: return - %x\n", Result);

    return Result;
}

BOOLEAN
NTAPI
USBPORT_MessageInterruptService(IN PKINTERRUPT Interrupt,
                                IN PVOID ServiceContext,
                                IN ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);
    return USBPORT_InterruptService(Interrupt, ServiceContext);
}

VOID
NTAPI
USBPORT_SignalWorkerThread(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    KIRQL OldIrql;

    DPRINT_CORE("USBPORT_SignalWorkerThread ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->WorkerThreadEventSpinLock, &OldIrql);
    KeSetEvent(&FdoExtension->WorkerThreadEvent, EVENT_INCREMENT, FALSE);
    KeReleaseSpinLock(&FdoExtension->WorkerThreadEventSpinLock, OldIrql);
}

typedef struct _USBPORT_TT_CLEAR_CONTEXT
{
    LIST_ENTRY Link;
    PIO_WORKITEM WorkItem;
    PUSBPORT_ENDPOINT Endpoint;
    PUSBPORT_DEVICE_HANDLE HubDeviceHandle;
} USBPORT_TT_CLEAR_CONTEXT, *PUSBPORT_TT_CLEAR_CONTEXT;

static BOOLEAN
USBPORT_AcquireTtClearRundown(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    PUSBPORT_TT_CLEAR_RUNDOWN Rundown;
    LONG State;

    Rundown = FdoExtension->Aux.TtClearRundown;
    if (!Rundown)
        return FALSE;

    for (;;)
    {
        State = InterlockedCompareExchange(&Rundown->State, 0, 0);
        if (State < 0)
            return FALSE;

        ASSERT(State != MAXLONG);
        if (InterlockedCompareExchange(&Rundown->State,
                                       State + 1,
                                       State) == State)
        {
            return TRUE;
        }
    }
}

static VOID
USBPORT_ReleaseTtClearRundown(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    PUSBPORT_TT_CLEAR_RUNDOWN Rundown;

    Rundown = FdoExtension->Aux.TtClearRundown;
    ASSERT(Rundown != NULL);

    if (InterlockedDecrement(&Rundown->State) ==
        USBPORT_TT_CLEAR_RUNDOWN_STOPPING)
    {
        KeSetEvent(&Rundown->Event, IO_NO_INCREMENT, FALSE);
    }
}

VOID
USBPORT_StopTtClearRundown(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    PUSBPORT_TT_CLEAR_RUNDOWN Rundown;
    LONG State;

    Rundown = FdoExtension->Aux.TtClearRundown;
    if (!Rundown)
        return;

    for (;;)
    {
        State = InterlockedCompareExchange(&Rundown->State, 0, 0);
        if (State < 0)
            break;

        if (InterlockedCompareExchange(&Rundown->State,
                                       State | USBPORT_TT_CLEAR_RUNDOWN_STOPPING,
                                       State) == State)
        {
            break;
        }
    }

    if (InterlockedCompareExchange(&Rundown->State, 0, 0) ==
        USBPORT_TT_CLEAR_RUNDOWN_STOPPING)
    {
        KeSetEvent(&Rundown->Event, IO_NO_INCREMENT, FALSE);
    }

    KeWaitForSingleObject(&Rundown->Event,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
}

static PUSBPORT_DEVICE_HANDLE
USBPORT_GetTtHubDeviceHandle(IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_HANDLE HubDeviceHandle;
    PUSB2_TT_EXTENSION TtExtension;

    TtExtension = Endpoint->TtExtension;
    if (!Endpoint->DeviceHandle || !TtExtension)
        return NULL;

    for (HubDeviceHandle = Endpoint->DeviceHandle->HubDeviceHandle;
         HubDeviceHandle && !HubDeviceHandle->IsRootHub;
         HubDeviceHandle = HubDeviceHandle->HubDeviceHandle)
    {
        if (HubDeviceHandle->DeviceAddress == TtExtension->DeviceAddress)
            return HubDeviceHandle;
    }

    return NULL;
}

static NTSTATUS
USBPORT_SendClearTtBuffer(IN PDEVICE_OBJECT FdoDevice,
                          IN PUSBPORT_TT_CLEAR_CONTEXT Context)
{
    PUSBPORT_DEVICE_HANDLE HubDeviceHandle;
    PUSB2_TT_EXTENSION TtExtension;
    PUSBPORT_ENDPOINT Endpoint;
    PUSBPORT_ENDPOINT_PROPERTIES Properties;
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    NTSTATUS Status;
    USHORT DeviceInfo;

    Endpoint = Context->Endpoint;
    TtExtension = Endpoint->TtExtension;
    HubDeviceHandle = Context->HubDeviceHandle;
    if (!TtExtension || !HubDeviceHandle || !TtExtension->TtNumber)
        return STATUS_DEVICE_NOT_CONNECTED;

    if (TtExtension->Flags & USB2_TT_EXTENSION_FLAG_DELETED)
        return STATUS_DEVICE_NOT_CONNECTED;

    if (HubDeviceHandle->Flags & DEVICE_HANDLE_FLAG_REMOVED)
        return STATUS_DEVICE_NOT_CONNECTED;

    Properties = &Endpoint->EndpointProperties;
    DeviceInfo = Properties->EndpointAddress & 0x0F;
    DeviceInfo |= (Properties->DeviceAddress & 0x7F) << 4;
    DeviceInfo |= (Properties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL ?
                   USB_ENDPOINT_TYPE_CONTROL : USB_ENDPOINT_TYPE_BULK) << 11;
    if (USB_ENDPOINT_DIRECTION_IN(Properties->EndpointAddress))
        DeviceInfo |= 0x8000;

    RtlZeroMemory(&SetupPacket, sizeof(SetupPacket));
    SetupPacket.bmRequestType.Recipient = BMREQUEST_TO_OTHER;
    SetupPacket.bmRequestType.Type = BMREQUEST_CLASS;
    SetupPacket.bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
    SetupPacket.bRequest = USB_REQUEST_CLEAR_TT_BUFFER;
    SetupPacket.wIndex.W = TtExtension->TtNumber;

    /* A control endpoint can leave state in either TT direction. */
    if (Properties->TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
    {
        SetupPacket.wValue.W = DeviceInfo ^ 0x8000;
        Status = USBPORT_SendSetupPacket(HubDeviceHandle,
                                         FdoDevice,
                                         &SetupPacket,
                                         NULL,
                                         0,
                                         NULL,
                                         NULL);
        if (!NT_SUCCESS(Status))
            goto Exit;
    }

    SetupPacket.wValue.W = DeviceInfo;
    Status = USBPORT_SendSetupPacket(HubDeviceHandle,
                                     FdoDevice,
                                     &SetupPacket,
                                     NULL,
                                     0,
                                     NULL,
                                     NULL);

Exit:
    return Status;
}

static VOID
NTAPI
USBPORT_TtClearWorker(IN PDEVICE_OBJECT FdoDevice,
                      IN PVOID ContextPointer)
{
    PUSBPORT_TT_CLEAR_CONTEXT Context;
    PUSBPORT_TT_CLEAR_CONTEXT WorkerContext = ContextPointer;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_TT_CLEAR_RUNDOWN Rundown;
    PUSBPORT_ENDPOINT Endpoint;
    PIO_WORKITEM WorkerItem;
    PLIST_ENTRY Entry;
    ULONG EndpointStatus;
    NTSTATUS Status;
    KIRQL OldIrql;

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;
    Rundown = FdoExtension->Aux.TtClearRundown;
    ASSERT(Rundown != NULL);
    WorkerItem = WorkerContext->WorkItem;
    WorkerContext->WorkItem = NULL;

    for (;;)
    {
        KeAcquireSpinLock(&Rundown->QueueLock, &OldIrql);
        if (IsListEmpty(&Rundown->Queue))
        {
            Rundown->WorkerActive = FALSE;
            KeReleaseSpinLock(&Rundown->QueueLock, OldIrql);
            break;
        }

        Entry = RemoveHeadList(&Rundown->Queue);
        KeReleaseSpinLock(&Rundown->QueueLock, OldIrql);
        Context = CONTAINING_RECORD(Entry, USBPORT_TT_CLEAR_CONTEXT, Link);
        Endpoint = Context->Endpoint;

        /* Only the first context's work item runs this queue. */
        if (Context->WorkItem)
            IoFreeWorkItem(Context->WorkItem);

        Status = USBPORT_SendClearTtBuffer(FdoDevice, Context);
        if (!NT_SUCCESS(Status) && Status != STATUS_DEVICE_NOT_CONNECTED)
        {
            DPRINT1("USBPORT: CLEAR_TT_BUFFER failed endpoint=%p hub=%u tt=%u status=%08lx\n",
                    Endpoint,
                    Endpoint->EndpointProperties.TtHubAddr,
                    Endpoint->TtExtension ? Endpoint->TtExtension->TtNumber : 0,
                    Status);
        }

        /* Release the HCD queue even if the hub request failed. */
        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
        EndpointStatus = Packet->GetEndpointStatus(FdoExtension->MiniPortExt,
                                                   Endpoint + 1);
        if (EndpointStatus & USBPORT_ENDPOINT_TT_BUFFER_DIRTY)
        {
            Packet->SetEndpointStatus(FdoExtension->MiniPortExt,
                                      Endpoint + 1,
                                      USBPORT_ENDPOINT_RUN);
        }
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

        InterlockedDecrement(&Context->HubDeviceHandle->DeviceHandleLock);
        USBPORT_InvalidateEndpointHandler(FdoDevice,
                                          Endpoint,
                                          INVALIDATE_ENDPOINT_WORKER_THREAD);
        InterlockedDecrement(&Endpoint->LockCounter);
        ExFreePoolWithTag(Context, USB_PORT_TAG);
        USBPORT_ReleaseTtClearRundown(FdoExtension);
    }

    IoFreeWorkItem(WorkerItem);
    /* Keep rundown storage alive until the queue worker has fully returned. */
    USBPORT_ReleaseTtClearRundown(FdoExtension);
}

BOOLEAN
NTAPI
USBPORT_QueueTtClear(IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_TT_CLEAR_CONTEXT Context;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_DEVICE_HANDLE HubDeviceHandle;
    PUSBPORT_TT_CLEAR_RUNDOWN Rundown;
    PDEVICE_OBJECT FdoDevice;
    BOOLEAN QueueWorker = FALSE;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    FdoDevice = Endpoint->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    if (!USBPORT_AcquireTtClearRundown(FdoExtension))
        return FALSE;
    Rundown = FdoExtension->Aux.TtClearRundown;

    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Context),
                                    USB_PORT_TAG);
    if (!Context)
    {
        USBPORT_ReleaseTtClearRundown(FdoExtension);
        return FALSE;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    Context->WorkItem = IoAllocateWorkItem(FdoDevice);
    if (!Context->WorkItem)
    {
        ExFreePoolWithTag(Context, USB_PORT_TAG);
        USBPORT_ReleaseTtClearRundown(FdoExtension);
        return FALSE;
    }

    /* Parent-handle detachment and final free use this same lock. */
    KeAcquireSpinLockAtDpcLevel(&FdoExtension->DeviceHandleSpinLock);
    HubDeviceHandle = USBPORT_GetTtHubDeviceHandle(Endpoint);
    if (HubDeviceHandle &&
        !(HubDeviceHandle->Flags & DEVICE_HANDLE_FLAG_REMOVED))
    {
        if (InterlockedIncrement(&HubDeviceHandle->DeviceHandleLock) > 0)
        {
            Context->HubDeviceHandle = HubDeviceHandle;
        }
        else
        {
            InterlockedDecrement(&HubDeviceHandle->DeviceHandleLock);
        }
    }
    KeReleaseSpinLockFromDpcLevel(&FdoExtension->DeviceHandleSpinLock);

    if (!Context->HubDeviceHandle)
    {
        IoFreeWorkItem(Context->WorkItem);
        ExFreePoolWithTag(Context, USB_PORT_TAG);
        USBPORT_ReleaseTtClearRundown(FdoExtension);
        return FALSE;
    }

    Context->Endpoint = Endpoint;
    KeAcquireSpinLockAtDpcLevel(&Rundown->QueueLock);
    InsertTailList(&Rundown->Queue, &Context->Link);
    if (!Rundown->WorkerActive)
    {
        Rundown->WorkerActive = TRUE;
        InterlockedIncrement(&Rundown->State);
        QueueWorker = TRUE;
    }
    KeReleaseSpinLockFromDpcLevel(&Rundown->QueueLock);

    if (QueueWorker)
    {
        IoQueueWorkItem(Context->WorkItem,
                        USBPORT_TtClearWorker,
                        DelayedWorkQueue,
                        Context);
    }
    return TRUE;
}

VOID
NTAPI
USBPORT_WorkerThreadHandler(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PLIST_ENTRY workerList;
    KIRQL OldIrql;
    PUSBPORT_ENDPOINT Endpoint;
    LIST_ENTRY list;
    BOOLEAN Result;

    DPRINT_CORE("USBPORT_WorkerThreadHandler: ...\n");

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

    if (!(FdoExtension->Flags & USBPORT_FLAG_HC_SUSPEND))
    {
        Packet->CheckController(FdoExtension->MiniPortExt);
    }

    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

    InitializeListHead(&list);

    USBPORT_FlushAllEndpoints(FdoDevice);

    while (TRUE)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        KeAcquireSpinLockAtDpcLevel(&FdoExtension->EndpointListSpinLock);

        workerList = &FdoExtension->WorkerList;

        if (IsListEmpty(workerList))
            break;

        Endpoint = CONTAINING_RECORD(workerList->Flink,
                                     USBPORT_ENDPOINT,
                                     WorkerLink);

        DPRINT_CORE("USBPORT_WorkerThreadHandler: Endpoint - %p\n", Endpoint);

        RemoveHeadList(workerList);
        Endpoint->WorkerLink.Blink = NULL;
        Endpoint->WorkerLink.Flink = NULL;

        KeReleaseSpinLockFromDpcLevel(&FdoExtension->EndpointListSpinLock);

        Result = USBPORT_EndpointWorker(Endpoint, FALSE);
        KeAcquireSpinLockAtDpcLevel(&FdoExtension->EndpointListSpinLock);

        if (Result)
        {
            if (Endpoint->FlushAbortLink.Flink == NULL ||
                Endpoint->FlushAbortLink.Blink == NULL)
            {
                InsertTailList(&list, &Endpoint->FlushAbortLink);
            }
        }

        while (!IsListEmpty(&list))
        {
            Endpoint = CONTAINING_RECORD(list.Flink,
                                         USBPORT_ENDPOINT,
                                         FlushAbortLink);

            RemoveHeadList(&list);

            Endpoint->FlushAbortLink.Flink = NULL;
            Endpoint->FlushAbortLink.Blink = NULL;

            if (Endpoint->WorkerLink.Flink == NULL ||
                Endpoint->WorkerLink.Blink == NULL)
            {
                InsertTailList(&FdoExtension->WorkerList,
                               &Endpoint->WorkerLink);

                USBPORT_SignalWorkerThread(FdoDevice);
            }
        }

        KeReleaseSpinLockFromDpcLevel(&FdoExtension->EndpointListSpinLock);
        KeLowerIrql(OldIrql);
    }

    KeReleaseSpinLockFromDpcLevel(&FdoExtension->EndpointListSpinLock);
    KeLowerIrql(OldIrql);

    USBPORT_FlushClosedEndpointList(FdoDevice);
}

VOID
NTAPI
USBPORT_DoRootHubCallback(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_ROOT_HUB_CALLBACK_DATA CallbackData;
    PRH_INIT_CALLBACK RootHubInitCallback;
    PVOID RootHubInitContext;
    PVOID Caller;
    ULONG Sequence;
    ULONGLONG Timestamp;
    KIRQL OldIrql;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT PdoDevice;

    FdoExtension = FdoDevice->DeviceExtension;

    DPRINT("USBPORT_DoRootHubCallback: FdoDevice - %p\n", FdoDevice);

    CallbackData = FdoExtension->RootHubCallbackData;

    if (!CallbackData)
    {
        DPRINT_CORE("USBPORT_DoRootHubCallback: no callback data (Fdo=%p)\n",
                FdoDevice);
        return;
    }

    KeAcquireSpinLock(&CallbackData->Lock, &OldIrql);
    RootHubInitContext = CallbackData->Context;
    RootHubInitCallback = CallbackData->Callback;
    Caller = CallbackData->Caller;
    Sequence = CallbackData->Sequence;
    Timestamp = CallbackData->Timestamp;
    CallbackData->Callback = NULL;
    CallbackData->Context = NULL;
    CallbackData->Caller = NULL;
    CallbackData->Timestamp = 0;
    KeReleaseSpinLock(&CallbackData->Lock, OldIrql);

    PdoDevice = FdoExtension->RootHubPdo;
    PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);

    if (PdoExtension)
    {
        PdoExtension->RootHubInitCallback = RootHubInitCallback;
        PdoExtension->RootHubInitContext = RootHubInitContext;
    }

    if (RootHubInitCallback)
    {
        if (!USBPORT_IsKernelPointer((PVOID)RootHubInitCallback))
        {
            DPRINT_CORE("USBPORT_DoRootHubCallback: invalid callback pointer %p (pdo=%p seq=%lu caller=%p ctx=%p)\n",
                    RootHubInitCallback,
                    PdoDevice,
                    Sequence,
                    Caller,
                    RootHubInitContext);
            DPRINT1("USBPORT_DoRootHubCallback: invalid callback data\n");
            return;
        }

        DPRINT_CORE("USBPORT_DoRootHubCallback: calling %p ctx=%p seq=%lu caller=%p ts=%llu\n",
                RootHubInitCallback,
                RootHubInitContext,
                Sequence,
                Caller,
                (unsigned long long)Timestamp);

        RootHubInitCallback(RootHubInitContext);
    }

    DPRINT("USBPORT_DoRootHubCallback: exit\n");
}

VOID
NTAPI
USBPORT_SynchronizeRootHubCallback(IN PDEVICE_OBJECT FdoDevice,
                                   IN PDEVICE_OBJECT Usb2FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_DEVICE_EXTENSION Usb2FdoExtension;
    PDEVICE_RELATIONS CompanionControllersList;
    PUSBPORT_DEVICE_EXTENSION CompanionFdoExtension;
    PDEVICE_OBJECT * Entry;
    ULONG ix;

    DPRINT("USBPORT_SynchronizeRootHubCallback: FdoDevice - %p, Usb2FdoDevice - %p\n",
           FdoDevice,
           Usb2FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (Usb2FdoDevice == NULL &&
        !(Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2))
    {
        /* Not Companion USB11 Controller */
        USBPORT_DoRootHubCallback(FdoDevice);

        FdoExtension->Flags &= ~USBPORT_FLAG_RH_INIT_CALLBACK;
        InterlockedCompareExchange(&FdoExtension->RHInitCallBackLock, 0, 1);

        DPRINT("USBPORT_SynchronizeRootHubCallback: exit\n");
        return;
    }

    /* USB2 or Companion USB11 */

    DPRINT("USBPORT_SynchronizeRootHubCallback: FdoExtension->Flags - %p\n",
           FdoExtension->Flags);

    if (!(FdoExtension->Flags & USBPORT_FLAG_COMPANION_HC))
    {
        KeWaitForSingleObject(&FdoExtension->ControllerSemaphore,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);

        FdoExtension->Flags |= USBPORT_FLAG_PWR_AND_CHIRP_LOCK;

        if (!(FdoExtension->Flags & (USBPORT_FLAG_HC_SUSPEND |
                                     USBPORT_FLAG_POWER_AND_CHIRP_OK)))
        {
            USBPORT_RootHubPowerAndChirpAllCcPorts(FdoDevice);
            FdoExtension->Flags |= USBPORT_FLAG_POWER_AND_CHIRP_OK;
        }

        FdoExtension->Flags &= ~USBPORT_FLAG_PWR_AND_CHIRP_LOCK;

        KeReleaseSemaphore(&FdoExtension->ControllerSemaphore,
                           LOW_REALTIME_PRIORITY,
                           1,
                           FALSE);

        CompanionControllersList = USBPORT_FindCompanionControllers(FdoDevice,
                                                                    FALSE,
                                                                    TRUE);

        if (CompanionControllersList)
        {
            Entry = &CompanionControllersList->Objects[0];

            for (ix = 0; ix < CompanionControllersList->Count; ++ix)
            {
                CompanionFdoExtension = ((*Entry)->DeviceExtension);

                InterlockedCompareExchange(&CompanionFdoExtension->RHInitCallBackLock,
                                           0,
                                           1);

                ++Entry;
            }

            ExFreePoolWithTag(CompanionControllersList, USB_PORT_TAG);
        }

        USBPORT_DoRootHubCallback(FdoDevice);

        FdoExtension->Flags &= ~USBPORT_FLAG_RH_INIT_CALLBACK;
        InterlockedCompareExchange(&FdoExtension->RHInitCallBackLock, 0, 1);
    }
    else
    {
        Usb2FdoExtension = Usb2FdoDevice->DeviceExtension;

        USBPORT_Wait(FdoDevice, 50);

        while (FdoExtension->RHInitCallBackLock)
        {
            USBPORT_Wait(FdoDevice, 10);

            Usb2FdoExtension->Flags |= USBPORT_FLAG_RH_INIT_CALLBACK;
            USBPORT_SignalWorkerThread(Usb2FdoDevice);
        }

        USBPORT_DoRootHubCallback(FdoDevice);

        FdoExtension->Flags &= ~USBPORT_FLAG_RH_INIT_CALLBACK;
    }

    DPRINT("USBPORT_SynchronizeRootHubCallback: exit\n");
}

VOID
NTAPI
USBPORT_WorkerThread(IN PVOID StartContext)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    LARGE_INTEGER OldTime;
    LARGE_INTEGER NewTime;
    KIRQL OldIrql;

    DPRINT_CORE("USBPORT_WorkerThread ...\n");

    FdoDevice = StartContext;
    FdoExtension = FdoDevice->DeviceExtension;

    FdoExtension->WorkerThread = KeGetCurrentThread();

    do
    {
        KeQuerySystemTime(&OldTime);

        KeWaitForSingleObject(&FdoExtension->WorkerThreadEvent,
                              Suspended,
                              KernelMode,
                              FALSE,
                              NULL);

        if (FdoExtension->Flags & USBPORT_FLAG_WORKER_THREAD_EXIT)
        {
            break;
        }

        KeQuerySystemTime(&NewTime);

        KeAcquireSpinLock(&FdoExtension->WorkerThreadEventSpinLock, &OldIrql);
        KeClearEvent(&FdoExtension->WorkerThreadEvent);
        KeReleaseSpinLock(&FdoExtension->WorkerThreadEventSpinLock, OldIrql);
        DPRINT_CORE("USBPORT_WorkerThread: run\n");

        if (FdoExtension->MiniPortFlags & USBPORT_MPFLAG_INTERRUPTS_ENABLED)
        {
            USBPORT_DoSetPowerD0(FdoDevice);

            if (FdoExtension->Flags & USBPORT_FLAG_RH_INIT_CALLBACK)
            {
                PDEVICE_OBJECT USB2FdoDevice = NULL;

                USB2FdoDevice = USBPORT_FindUSB2Controller(FdoDevice);
                USBPORT_SynchronizeRootHubCallback(FdoDevice, USB2FdoDevice);
            }
        }

        USBPORT_WorkerThreadHandler(FdoDevice);
    }
    while (!(FdoExtension->Flags & USBPORT_FLAG_WORKER_THREAD_ON));

    PsTerminateSystemThread(0);
}

NTSTATUS
NTAPI
USBPORT_CreateWorkerThread(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    NTSTATUS Status;

    DPRINT("USBPORT_CreateWorkerThread ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    FdoExtension->Flags &= ~USBPORT_FLAG_WORKER_THREAD_ON;

    KeInitializeEvent(&FdoExtension->WorkerThreadEvent,
                      NotificationEvent,
                      FALSE);

    Status = PsCreateSystemThread(&FdoExtension->WorkerThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  USBPORT_WorkerThread,
                                  (PVOID)FdoDevice);

    return Status;
}

VOID
NTAPI
USBPORT_StopWorkerThread(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    NTSTATUS Status;
    HANDLE ThreadHandle;

    DPRINT("USBPORT_StopWorkerThread ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    /* A TT-clear control transfer can still need the USBPORT worker. */
    USBPORT_StopTtClearRundown(FdoExtension);

    ThreadHandle = FdoExtension->WorkerThreadHandle;

    if (!ThreadHandle)
    {
        return;
    }

    FdoExtension->Flags |= USBPORT_FLAG_WORKER_THREAD_EXIT;
    USBPORT_SignalWorkerThread(FdoDevice);
    Status = ZwWaitForSingleObject(ThreadHandle, FALSE, NULL);
#if DBG
    NT_ASSERT(Status == STATUS_SUCCESS);
#endif
    ZwClose(ThreadHandle);
    FdoExtension->WorkerThreadHandle = NULL;
    FdoExtension->WorkerThread = NULL;
    FdoExtension->Flags &= ~USBPORT_FLAG_WORKER_THREAD_EXIT;
}

VOID
NTAPI
USBPORT_SynchronizeControllersStart(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT USB2FdoDevice = NULL;
    PUSBPORT_DEVICE_EXTENSION USB2FdoExtension;
    BOOLEAN IsOn;
    PUSBPORT_ROOT_HUB_CALLBACK_DATA CallbackData;
    PRH_INIT_CALLBACK PendingCallback;
    PVOID PendingContext;
    PVOID PendingCaller;
    ULONG PendingSequence;
    ULONGLONG PendingTimestamp;
    KIRQL CallbackIrql;

    // DPRINT_CORE("USBPORT_SynchronizeControllersStart: FdoDevice - %p\n", FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;

    PdoDevice = FdoExtension->RootHubPdo;

    if (!PdoDevice)
    {
        DPRINT_CORE("USBPORT_SynchronizeControllersStart: FdoDevice is null - return\n");
        return;
    }

    PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);
    CallbackData = FdoExtension->RootHubCallbackData;

    if (!PdoExtension)
    {
        DPRINT_CORE("USBPORT_SynchronizeControllersStart: no RootHub extension (pdo=%p fdo=%p)\n",
                PdoDevice,
                FdoDevice);
        return;
    }

    if ((LONG_PTR)PdoExtension >= 0)
    {
        DPRINT_CORE("USBPORT_SynchronizeControllersStart: invalid RootHub extension pointer %p (pdo=%p)\n",
                PdoExtension,
                PdoDevice);
        DPRINT1("USBPORT_SynchronizeControllersStart: invalid PDO extension\n");
        return;
    }

    if (!CallbackData)
    {
        DPRINT_CORE("USBPORT_SynchronizeControllersStart: missing callback data (fdo=%p)\n",
                FdoDevice);
        return;
    }

    KeAcquireSpinLock(&CallbackData->Lock, &CallbackIrql);
    PendingCallback = CallbackData->Callback;
    PendingContext = CallbackData->Context;
    PendingCaller = CallbackData->Caller;
    PendingSequence = CallbackData->Sequence;
    PendingTimestamp = CallbackData->Timestamp;
    KeReleaseSpinLock(&CallbackData->Lock, CallbackIrql);

    if (!PendingCallback || (FdoExtension->Flags & USBPORT_FLAG_RH_INIT_CALLBACK))
    {
        /* No callback scheduled yet: clear the flag so the timer stops spamming,
           and wait for the hub to register a callback before retrying. */
        if (FdoExtension->Flags & USBPORT_FLAG_RH_INIT_CALLBACK)
        {
            FdoExtension->Flags &= ~USBPORT_FLAG_RH_INIT_CALLBACK;
            DPRINT_CORE("USBPORT_SynchronizeControllersStart: PendingCallback is null - suppressing retry\n");
        }
        return;
    }

    FdoExtension->Flags &= ~USBPORT_FLAG_RH_STOPPED;

    DPRINT_CORE("USBPORT_SynchronizeControllersStart: Flags - %p\n",
            FdoExtension->Flags);

    /* Extra diagnostics to catch bad pointers on DPC path */
#if DBG
    DPRINT_CORE("USBPORT_SynchronizeControllersStart: MiniPortExt=%p RootHubPdo=%p RootHubInitCb=%p\n",
            FdoExtension->MiniPortExt,
            PdoDevice,
            PendingCallback);
    DPRINT_CORE("USBPORT_SynchronizeControllersStart: rh seq=%lu caller=%p ts=%llu ctx=%p\n",
            PendingSequence,
            PendingCaller,
            (unsigned long long)PendingTimestamp,
            PendingContext);
#endif

    if (FdoExtension->Flags & USBPORT_FLAG_COMPANION_HC)
    {
        IsOn = FALSE;

        USB2FdoDevice = USBPORT_FindUSB2Controller(FdoDevice);

        DPRINT_CORE("USBPORT_SynchronizeControllersStart: USB2FdoDevice - %p\n",
                USB2FdoDevice);

        if (USB2FdoDevice)
        {
            USB2FdoExtension = USB2FdoDevice->DeviceExtension;

            if (USB2FdoExtension->CommonExtension.PnpStateFlags &
                USBPORT_PNP_STATE_STARTED)
            {
                IsOn = TRUE;
            }
        }

        if (!(FdoExtension->Flags & USBPORT_FLAG_NO_HACTION))
        {
            goto Start;
        }

        USB2FdoDevice = NULL;
    }

    IsOn = TRUE;

  Start:

    if (IsOn &&
        !InterlockedCompareExchange(&FdoExtension->RHInitCallBackLock, 1, 0))
    {
        FdoExtension->Flags |= USBPORT_FLAG_RH_INIT_CALLBACK;
        USBPORT_SignalWorkerThread(FdoDevice);

        if (USB2FdoDevice)
        {
            USB2FdoExtension = USB2FdoDevice->DeviceExtension;

            USB2FdoExtension->Flags |= USBPORT_FLAG_RH_INIT_CALLBACK;
            USBPORT_SignalWorkerThread(USB2FdoDevice);
        }
    }

    DPRINT_TIMER("USBPORT_SynchronizeControllersStart: exit\n");
}

VOID
NTAPI
USBPORT_TimerDpc(IN PRKDPC Dpc,
                 IN PVOID DeferredContext,
                 IN PVOID SystemArgument1,
                 IN PVOID SystemArgument2)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    LARGE_INTEGER DueTime = {{0, 0}};
    ULONG TimerFlags;
    PTIMER_WORK_QUEUE_ITEM IdleQueueItem;
    BOOLEAN TimerStopped = FALSE;
    KIRQL OldIrql;
    KIRQL TimerOldIrql;

    USBPORT_TIMER_TRACE("USBPORT_TimerDpc: Dpc - %p, DeferredContext - %p\n",
                        Dpc,
                        DeferredContext);

    FdoDevice = DeferredContext;
    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->TimerFlagsSpinLock, &TimerOldIrql);

    TimerFlags = FdoExtension->TimerFlags;

    if (!(TimerFlags & USBPORT_TMFLAG_TIMER_QUEUED))
    {
        TimerStopped = TRUE;
        goto Exit;
    }

    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (FdoExtension->Flags & USBPORT_FLAG_RH_STOPPED)
    {
        USBPORT_TIMER_TRACE("USBPORT_TimerDpc: root hub stopped, skipping DPC\n");
        goto Exit;
    }

    USBPORT_TIMER_TRACE("USBPORT_TimerDpc: Flags - %p, TimerFlags - %p\n",
                        FdoExtension->Flags,
                        TimerFlags);

    if (FdoExtension->Flags & USBPORT_FLAG_HC_SUSPEND &&
        FdoExtension->Flags & USBPORT_FLAG_HC_WAKE_SUPPORT &&
        !(TimerFlags & USBPORT_TMFLAG_HC_RESUME))
    {
        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
        USBPORT_TIMER_TRACE("USBPORT_TimerDpc: calling PollController (MiniPortExt=%p)\n",
                            FdoExtension->MiniPortExt);
        Packet->PollController(FdoExtension->MiniPortExt);
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
    }

    {
        PUSBPORT_RHDEVICE_EXTENSION RhExt = USBPORT_GetRootHubExtension(FdoExtension);
        USBPORT_TIMER_TRACE("USBPORT_TimerDpc: RootHubPdo=%p RootHubExt=%p\n",
                            FdoExtension->RootHubPdo,
                            RhExt);
        UNREFERENCED_PARAMETER(RhExt);
    }

    USBPORT_SynchronizeControllersStart(FdoDevice);

    if (TimerFlags & USBPORT_TMFLAG_HC_SUSPENDED)
    {
        USBPORT_BadRequestFlush(FdoDevice);
        goto Exit;
    }

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

    if (!(FdoExtension->Flags & USBPORT_FLAG_HC_SUSPEND))
    {
        USBPORT_TIMER_TRACE("USBPORT_TimerDpc: calling CheckController (MiniPortExt=%p)\n",
                            FdoExtension->MiniPortExt);
        Packet->CheckController(FdoExtension->MiniPortExt);
    }

    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

    if (FdoExtension->Flags & USBPORT_FLAG_HC_POLLING)
    {
        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
    USBPORT_TIMER_TRACE("USBPORT_TimerDpc: calling PollController (MiniPortExt=%p) [polling]\n",
                        FdoExtension->MiniPortExt);
        Packet->PollController(FdoExtension->MiniPortExt);
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
    }

    USBPORT_IsrDpcHandler(FdoDevice, FALSE);

    DPRINT_TIMER("USBPORT_TimerDpc: USBPORT_TimeoutAllEndpoints UNIMPLEMENTED.\n");
    //USBPORT_TimeoutAllEndpoints(FdoDevice);
    DPRINT_TIMER("USBPORT_TimerDpc: USBPORT_CheckIdleEndpoints UNIMPLEMENTED.\n");
    //USBPORT_CheckIdleEndpoints(FdoDevice);

    USBPORT_BadRequestFlush(FdoDevice);

    if (FdoExtension->IdleLockCounter > -1 &&
        !(TimerFlags & USBPORT_TMFLAG_IDLE_QUEUEITEM_ON))
    {
        IdleQueueItem = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(TIMER_WORK_QUEUE_ITEM),
                                              USB_PORT_TAG);

        DPRINT("USBPORT_TimerDpc: IdleLockCounter - %x, IdleQueueItem - %p\n",
               FdoExtension->IdleLockCounter,
               IdleQueueItem);

        if (IdleQueueItem)
        {
            RtlZeroMemory(IdleQueueItem, sizeof(TIMER_WORK_QUEUE_ITEM));

            IdleQueueItem->WqItem.List.Flink = NULL;
            IdleQueueItem->WqItem.WorkerRoutine = USBPORT_DoIdleNotificationCallback;
            IdleQueueItem->WqItem.Parameter = IdleQueueItem;

            IdleQueueItem->FdoDevice = FdoDevice;
            IdleQueueItem->Context = 0;

            FdoExtension->TimerFlags |= USBPORT_TMFLAG_IDLE_QUEUEITEM_ON;

            ExQueueWorkItem(&IdleQueueItem->WqItem, CriticalWorkQueue);
        }
    }

Exit:
    /* Rearm while holding the same lock used by StopControllerTimer. This
     * makes the live flag, the timer state, and stop's cancel decision one
     * ordered transition instead of rearming from a stale snapshot. */
    if (FdoExtension->TimerFlags & USBPORT_TMFLAG_TIMER_QUEUED)
    {
        DueTime.QuadPart -= FdoExtension->TimerValue * 10000 +
                            (KeQueryTimeIncrement() - 1);

        KeSetTimer(&FdoExtension->TimerObject,
                   DueTime,
                   &FdoExtension->TimerDpc);
    }
    else
    {
        TimerStopped = TRUE;
    }

    KeReleaseSpinLock(&FdoExtension->TimerFlagsSpinLock, TimerOldIrql);

    if (TimerStopped)
    {
        KeSetEvent(&FdoExtension->TimerDpcEvent, IO_NO_INCREMENT, FALSE);
    }

    DPRINT_TIMER("USBPORT_TimerDpc: exit\n");
}

BOOLEAN
NTAPI
USBPORT_StartTimer(IN PDEVICE_OBJECT FdoDevice,
                   IN ULONG Time)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    LARGE_INTEGER DueTime = {{0, 0}};
    ULONG TimeIncrement;
    BOOLEAN Result;
    KIRQL OldIrql;

    DPRINT_TIMER("USBPORT_StartTimer: FdoDevice - %p, Time - %x\n",
           FdoDevice,
           Time);

    FdoExtension = FdoDevice->DeviceExtension;

    TimeIncrement = KeQueryTimeIncrement();

    KeInitializeTimer(&FdoExtension->TimerObject);
    KeInitializeDpc(&FdoExtension->TimerDpc, USBPORT_TimerDpc, FdoDevice);

    DueTime.QuadPart -= 10000 * Time + (TimeIncrement - 1);

    KeAcquireSpinLock(&FdoExtension->TimerFlagsSpinLock, &OldIrql);
    FdoExtension->TimerFlags |= USBPORT_TMFLAG_TIMER_QUEUED;
    FdoExtension->TimerValue = Time;
    KeClearEvent(&FdoExtension->TimerDpcEvent);

    Result = KeSetTimer(&FdoExtension->TimerObject,
                        DueTime,
                        &FdoExtension->TimerDpc);
    KeReleaseSpinLock(&FdoExtension->TimerFlagsSpinLock, OldIrql);

    return Result;
}

PUSBPORT_COMMON_BUFFER_HEADER
NTAPI
USBPORT_AllocateCommonBuffer(IN PDEVICE_OBJECT FdoDevice,
                             IN SIZE_T BufferLength)
{
    PUSBPORT_COMMON_BUFFER_HEADER HeaderBuffer = NULL;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDMA_ADAPTER DmaAdapter;
    PDMA_OPERATIONS DmaOperations;
    SIZE_T HeaderSize;
    ULONG Length = 0;
    ULONG LengthPadded;
    PHYSICAL_ADDRESS LogicalAddress;
    ULONG_PTR BaseVA;
    ULONG_PTR StartBufferVA;
    ULONGLONG StartBufferPA;

    DPRINT("USBPORT_AllocateCommonBuffer: FdoDevice - %p, BufferLength - %p\n",
           FdoDevice,
           BufferLength);

    if (BufferLength == 0)
        goto Exit;

    FdoExtension = FdoDevice->DeviceExtension;

    DmaAdapter = FdoExtension->DmaAdapter;
    if (!DmaAdapter || !DmaAdapter->DmaOperations ||
        !DmaAdapter->DmaOperations->AllocateCommonBuffer)
    {
        DPRINT1("USBPORT_AllocateCommonBuffer: missing DMA adapter/ops\n");
        goto Exit;
    }

    DmaOperations = DmaAdapter->DmaOperations;

    HeaderSize = sizeof(USBPORT_COMMON_BUFFER_HEADER);
    Length = ROUND_TO_PAGES(BufferLength + HeaderSize);
    LengthPadded = Length - (BufferLength + HeaderSize);

    BaseVA = (ULONG_PTR)DmaOperations->AllocateCommonBuffer(DmaAdapter,
                                                            Length,
                                                            &LogicalAddress,
                                                            TRUE);

    if (!BaseVA)
        goto Exit;

    StartBufferVA = BaseVA & ~(PAGE_SIZE - 1);
    StartBufferPA = LogicalAddress.QuadPart & ~((ULONGLONG)PAGE_SIZE - 1ULL);

    HeaderBuffer = (PUSBPORT_COMMON_BUFFER_HEADER)(StartBufferVA +
                                                   BufferLength +
                                                   LengthPadded);

    HeaderBuffer->Length = Length;
    HeaderBuffer->BaseVA = BaseVA;
    HeaderBuffer->LogicalAddress = LogicalAddress;

    HeaderBuffer->BufferLength = BufferLength + LengthPadded;
    HeaderBuffer->VirtualAddress = StartBufferVA;
    HeaderBuffer->PhysicalAddress = StartBufferPA;

    RtlZeroMemory((PVOID)StartBufferVA, BufferLength + LengthPadded);

Exit:
    return HeaderBuffer;
}

VOID
NTAPI
USBPORT_FreeCommonBuffer(IN PDEVICE_OBJECT FdoDevice,
                         IN PUSBPORT_COMMON_BUFFER_HEADER HeaderBuffer)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDMA_ADAPTER DmaAdapter;
    PDMA_OPERATIONS DmaOperations;

    DPRINT("USBPORT_FreeCommonBuffer: ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    DmaAdapter = FdoExtension->DmaAdapter;
    DmaOperations = DmaAdapter->DmaOperations;

    DmaOperations->FreeCommonBuffer(FdoExtension->DmaAdapter,
                                    HeaderBuffer->Length,
                                    HeaderBuffer->LogicalAddress,
                                    (PVOID)HeaderBuffer->VirtualAddress,
                                    TRUE);
}

PUSBPORT_MINIPORT_INTERFACE
NTAPI
USBPORT_FindMiniPort(IN PDRIVER_OBJECT DriverObject)
{
    KIRQL OldIrql;
    PLIST_ENTRY List;
    PUSBPORT_MINIPORT_INTERFACE MiniPortInterface;
    BOOLEAN IsFound = FALSE;

    DPRINT("USBPORT_FindMiniPort: ...\n");

    KeAcquireSpinLock(&USBPORT_SpinLock, &OldIrql);

    for (List = USBPORT_MiniPortDrivers.Flink;
         List != &USBPORT_MiniPortDrivers;
         List = List->Flink)
    {
        MiniPortInterface = CONTAINING_RECORD(List,
                                              USBPORT_MINIPORT_INTERFACE,
                                              DriverLink);

        if (MiniPortInterface->DriverObject == DriverObject)
        {
            DPRINT("USBPORT_FindMiniPort: find MiniPortInterface - %p\n",
                   MiniPortInterface);

            IsFound = TRUE;
            break;
        }
    }

    KeReleaseSpinLock(&USBPORT_SpinLock, OldIrql);

    if (IsFound)
        return MiniPortInterface;
    else
        return NULL;

}

NTSTATUS
NTAPI
USBPORT_AddDevice(IN PDRIVER_OBJECT DriverObject,
                  IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;
    PUSBPORT_MINIPORT_INTERFACE MiniPortInterface;
    ULONG DeviceNumber = 0;
    WCHAR CharDeviceName[64];
    UNICODE_STRING DeviceName;
    PDEVICE_OBJECT DeviceObject;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_COMMON_DEVICE_EXTENSION FdoCommonExtension;
    PDEVICE_OBJECT LowerDevice;
    ULONG Length;

    DPRINT("USBPORT_AddDevice: DriverObject - %p, PhysicalDeviceObject - %p\n",
           DriverObject,
           PhysicalDeviceObject);

    MiniPortInterface = USBPORT_FindMiniPort(DriverObject);

    if (!MiniPortInterface)
    {
        DPRINT("USBPORT_AddDevice: USBPORT_FindMiniPort not found MiniPortInterface\n");
        return STATUS_UNSUCCESSFUL;
    }

    while (TRUE)
    {
        /* Construct device name */
        RtlStringCbPrintfW(CharDeviceName,
                           sizeof(CharDeviceName),
                           L"\\Device\\USBFDO-%d",
                           DeviceNumber);

        RtlInitUnicodeString(&DeviceName, CharDeviceName);

        ASSERT(MiniPortInterface->Packet.MiniPortExtensionSize <=
               MAXULONG - sizeof(USBPORT_DEVICE_EXTENSION) - sizeof(USB2_HC_EXTENSION));
        Length = (ULONG)(sizeof(USBPORT_DEVICE_EXTENSION) +
                         MiniPortInterface->Packet.MiniPortExtensionSize +
                         sizeof(USB2_HC_EXTENSION));

        /* Create device */
        Status = IoCreateDevice(DriverObject,
                                Length,
                                &DeviceName,
                                FILE_DEVICE_CONTROLLER,
                                0,
                                FALSE,
                                &DeviceObject);

        /* Check for success */
        if (NT_SUCCESS(Status)) break;

        /* Is there a device object with that same name */
        if ((Status == STATUS_OBJECT_NAME_EXISTS) ||
            (Status == STATUS_OBJECT_NAME_COLLISION))
        {
            /* Try the next name */
            DeviceNumber++;
            continue;
        }

        /* Bail out on other errors */
        if (!NT_SUCCESS(Status))
        {
            DPRINT_CORE("USBPORT_AddDevice: failed to create %wZ, Status %x\n",
                    &DeviceName,
                    Status);

            return Status;
        }
    }

    DPRINT("USBPORT_AddDevice: created device %p <%wZ>, Status %x\n",
           DeviceObject,
           &DeviceName,
           Status);

    FdoExtension = DeviceObject->DeviceExtension;
    FdoCommonExtension = &FdoExtension->CommonExtension;

    RtlZeroMemory(FdoExtension, sizeof(USBPORT_DEVICE_EXTENSION));

    FdoCommonExtension->SelfDevice = DeviceObject;
    FdoCommonExtension->LowerPdoDevice = PhysicalDeviceObject;
    FdoCommonExtension->IsPDO = FALSE;

    LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject,
                                              PhysicalDeviceObject);

    FdoCommonExtension->LowerDevice = LowerDevice;

    FdoCommonExtension->DevicePowerState = PowerDeviceD3;

    FdoExtension->MiniPortExt = (PVOID)((ULONG_PTR)FdoExtension +
                                        sizeof(USBPORT_DEVICE_EXTENSION));

    if (MiniPortInterface->Packet.MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
    {
        FdoExtension->Usb2Extension =
        (PUSB2_HC_EXTENSION)((ULONG_PTR)FdoExtension->MiniPortExt +
                             MiniPortInterface->Packet.MiniPortExtensionSize);

        DPRINT("USBPORT_AddDevice: Usb2Extension - %p\n",
               FdoExtension->Usb2Extension);

        USB2_InitController(FdoExtension->Usb2Extension);
    }
    else
    {
        FdoExtension->Usb2Extension = NULL;
    }

    FdoExtension->MiniPortInterface = MiniPortInterface;
    FdoExtension->FdoNameNumber = DeviceNumber;

    KeInitializeSemaphore(&FdoExtension->DeviceSemaphore, 1, 1);
    KeInitializeSemaphore(&FdoExtension->ControllerSemaphore, 1, 1);

    InitializeListHead(&FdoExtension->EndpointList);
    InitializeListHead(&FdoExtension->DoneTransferList);
    InitializeListHead(&FdoExtension->WorkerList);
    InitializeListHead(&FdoExtension->EpStateChangeList);
    InitializeListHead(&FdoExtension->MapTransferList);
    FdoExtension->MapTransferBusy = 0;
    FdoExtension->MapTransferCallbackState = USBPORT_MAP_CALLBACK_NONE;
    InitializeListHead(&FdoExtension->DeviceHandleList);
    InitializeListHead(&FdoExtension->IdleIrpList);
    InitializeListHead(&FdoExtension->BadRequestList);
    InitializeListHead(&FdoExtension->EndpointClosedList);
    InitializeListHead(&FdoExtension->Aux.TransportRegistrationList);
    InitializeListHead(&FdoExtension->Aux.TimeSyncTrackingList);
    FdoExtension->Aux.NextTimeSyncId = 1;

    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return Status;
}

VOID
NTAPI
USBPORT_Unload(IN PDRIVER_OBJECT DriverObject)
{
    PUSBPORT_MINIPORT_INTERFACE MiniPortInterface;
    KIRQL OldIrql;

    MiniPortInterface = USBPORT_FindMiniPort(DriverObject);

    if (!MiniPortInterface)
    {
        DPRINT("USBPORT_Unload: CRITICAL ERROR!!! Not found MiniPortInterface\\n");
        KeBugCheckEx(BUGCODE_USB_DRIVER, 1, 0, 0, 0);
    }

    if (MiniPortInterface->DriverUnload)
    {
        MiniPortInterface->DriverUnload(DriverObject);
    }

    KeAcquireSpinLock(&USBPORT_SpinLock, &OldIrql);
    RemoveEntryList(&MiniPortInterface->DriverLink);
    KeReleaseSpinLock(&USBPORT_SpinLock, OldIrql);

    ExFreePoolWithTag(MiniPortInterface, USB_PORT_TAG);
}

VOID
NTAPI
USBPORT_MiniportCompleteTransfer(IN PVOID MiniPortExtension,
                                 IN PVOID MiniPortEndpoint,
                                 IN PVOID TransferParameters,
                                 IN USBD_STATUS USBDStatus,
                                 IN ULONG TransferLength)
{
    PUSBPORT_TRANSFER Transfer;
    PUSBPORT_TRANSFER ParentTransfer;
    PUSBPORT_TRANSFER SplitTransfer;
    PUSBPORT_DUMP_CONTEXT DumpContext;
    PLIST_ENTRY SplitHead;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    DPRINT_CORE("USBPORT_MiniportCompleteTransfer: USBDStatus - %x, TransferLength - %x\n",
                USBDStatus,
                TransferLength);

    /*
     * Defense-in-depth: validate the TransferParameters pointer before
     * deriving the USBPORT_TRANSFER via CONTAINING_RECORD. A miniport
     * bug or race condition (e.g., deferred completion after device
     * removal freed the USBPORT_TRANSFER) could pass a stale or invalid
     * pointer here. Without this check, CONTAINING_RECORD would compute
     * a garbage Transfer pointer, and accessing Transfer->Flags or
     * Transfer->Urb would crash with a page fault.
     */
    if (!TransferParameters)
    {
        DPRINT1("USBPORT_MiniportCompleteTransfer: invalid TransferParameters=%p USBDStatus=%x\n",
                TransferParameters, USBDStatus);
        return;
    }

    Transfer = CONTAINING_RECORD(TransferParameters,
                                 USBPORT_TRANSFER,
                                 TransferParameters);

    if (Transfer->Flags & TRANSFER_FLAG_DUMP)
    {
        DumpContext = Transfer->DumpContext;
        if (DumpContext != NULL)
        {
            DumpContext->TransferStatus = USBDStatus;
            DumpContext->CompletedLength = TransferLength;
            KeMemoryBarrier();
            InterlockedExchange(&DumpContext->Completed, 1);
        }
        return;
    }

    /*
     * Once the bugcheck owner has entered USB dump mode, normal transfer
     * owners can no longer run. Let the miniport retire their descriptors,
     * but never queue a completion that depends on frozen DPC/worker state.
     */
    DumpContext = ((PUSBPORT_DEVICE_EXTENSION)Transfer->FdoDevice->DeviceExtension)->Aux.DumpContext;
    if ((DumpContext != NULL) && DumpContext->DumpMode)
        return;

    Transfer->CompletedTransferLen = TransferLength;

    if (((Transfer->Flags & TRANSFER_FLAG_SPLITED) == 0) ||
        TransferLength >= Transfer->TransferParameters.TransferBufferLength)
    {
        goto Exit;
    }

    ParentTransfer = Transfer->ParentTransfer;

    KeAcquireSpinLock(&ParentTransfer->TransferSpinLock, &OldIrql);

    if (IsListEmpty(&ParentTransfer->SplitTransfersList))
    {
        goto Exit;
    }

    SplitHead = &ParentTransfer->SplitTransfersList;
    Entry = SplitHead->Flink;

    while (Entry && !IsListEmpty(SplitHead))
    {
        SplitTransfer = CONTAINING_RECORD(Entry,
                                          USBPORT_TRANSFER,
                                          SplitLink);

        if (!(SplitTransfer->Flags & TRANSFER_FLAG_SUBMITED))
        {
            DPRINT_CORE("USBPORT_MiniportCompleteTransfer: SplitTransfer->Flags - %X\n",
                    SplitTransfer->Flags);
            //Add TRANSFER_FLAG_xxx
        }

        Entry = Entry->Flink;
    }

    KeReleaseSpinLock(&ParentTransfer->TransferSpinLock, OldIrql);

Exit:
    USBPORT_QueueDoneTransfer(Transfer, USBDStatus, FALSE);
}

VOID
NTAPI
USBPORT_AsyncTimerDpc(IN PRKDPC Dpc,
                      IN PVOID DeferredContext,
                      IN PVOID SystemArgument1,
                      IN PVOID SystemArgument2)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_ASYNC_CALLBACK_DATA AsyncCallbackData;

    DPRINT("USBPORT_AsyncTimerDpc: ...\n");

    AsyncCallbackData = DeferredContext;
    FdoDevice = AsyncCallbackData->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;

    (*AsyncCallbackData->CallbackFunction)(FdoExtension->MiniPortExt,
                                           &AsyncCallbackData->CallbackContext);

    ExFreePoolWithTag(AsyncCallbackData, USB_PORT_TAG);
}

ULONG
NTAPI
USBPORT_RequestAsyncCallback(IN PVOID MiniPortExtension,
                             IN ULONG TimerValue,
                             IN PVOID Buffer,
                             IN SIZE_T Length,
                             IN ASYNC_TIMER_CALLBACK * Callback)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_ASYNC_CALLBACK_DATA AsyncCallbackData;
    LARGE_INTEGER DueTime = {{0, 0}};

    DPRINT("USBPORT_RequestAsyncCallback: ...\n");

    FdoExtension = (PUSBPORT_DEVICE_EXTENSION)((ULONG_PTR)MiniPortExtension -
                                               sizeof(USBPORT_DEVICE_EXTENSION));

    FdoDevice = FdoExtension->CommonExtension.SelfDevice;

    AsyncCallbackData = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(USBPORT_ASYNC_CALLBACK_DATA) + Length,
                                              USB_PORT_TAG);

    if (!AsyncCallbackData)
    {
        DPRINT_CORE("USBPORT_RequestAsyncCallback: Not allocated AsyncCallbackData!\n");
        return 0;
    }

    RtlZeroMemory(AsyncCallbackData,
                  sizeof(USBPORT_ASYNC_CALLBACK_DATA) + Length);

    if (Length)
    {
        RtlCopyMemory(&AsyncCallbackData->CallbackContext, Buffer, Length);
    }

    AsyncCallbackData->FdoDevice = FdoDevice;
    AsyncCallbackData->CallbackFunction = Callback;

    KeInitializeTimer(&AsyncCallbackData->AsyncTimer);

    KeInitializeDpc(&AsyncCallbackData->AsyncTimerDpc,
                    USBPORT_AsyncTimerDpc,
                    AsyncCallbackData);

    DueTime.QuadPart -= (KeQueryTimeIncrement() - 1) + 10000 * TimerValue;

    KeSetTimer(&AsyncCallbackData->AsyncTimer,
               DueTime,
               &AsyncCallbackData->AsyncTimerDpc);

    return 0;
}

PVOID
NTAPI
USBPORT_GetMappedVirtualAddress(IN ULONG PhysicalAddress,
                                IN PVOID MiniPortExtension,
                                IN PVOID MiniPortEndpoint)
{
    PUSBPORT_COMMON_BUFFER_HEADER HeaderBuffer;
    PUSBPORT_ENDPOINT Endpoint;
    ULONG Offset;
    ULONG_PTR VirtualAddress;

    DPRINT_CORE("USBPORT_GetMappedVirtualAddress: phys=%08lx MPext=%p MPep=%p\n",
            PhysicalAddress,
            MiniPortExtension,
            MiniPortEndpoint);

    Endpoint = (PUSBPORT_ENDPOINT)((ULONG_PTR)MiniPortEndpoint -
                                   sizeof(USBPORT_ENDPOINT));

    if (!Endpoint)
    {
        DPRINT1("USBPORT_GetMappedVirtualAddress: NULL Endpoint\n");
        return NULL;
    }

    HeaderBuffer = Endpoint->HeaderBuffer;
    if (!HeaderBuffer)
    {
        DPRINT_CORE("USBPORT_GetMappedVirtualAddress: NULL HeaderBuffer (EP=%p)\n", Endpoint);
        return NULL;
    }

    /* Compute offset within the common buffer and validate bounds */
    if ((ULONGLONG)PhysicalAddress < HeaderBuffer->PhysicalAddress)
    {
        DPRINT_CORE("USBPORT_GetMappedVirtualAddress: phys < base (phys=%llx base=%llx)\n",
                (unsigned long long)PhysicalAddress,
                (unsigned long long)HeaderBuffer->PhysicalAddress);
        return (PVOID)HeaderBuffer->VirtualAddress;
    }

    Offset = PhysicalAddress - (ULONG)HeaderBuffer->PhysicalAddress;
    if ((ULONGLONG)Offset >= HeaderBuffer->BufferLength)
    {
        DPRINT_CORE("USBPORT_GetMappedVirtualAddress: offset OOB (off=%llx len=%Ix)\n",
                (unsigned long long)Offset,
                HeaderBuffer->BufferLength);
        return (PVOID)HeaderBuffer->VirtualAddress;
    }

    VirtualAddress = HeaderBuffer->VirtualAddress + (ULONG_PTR)Offset;
    DPRINT_CORE("USBPORT_GetMappedVirtualAddress: -> VA=%p (base=%p off=%lx)\n",
            (PVOID)VirtualAddress,
            (PVOID)HeaderBuffer->VirtualAddress,
            Offset);

    return (PVOID)VirtualAddress;
}

ULONG
NTAPI
USBPORT_InvalidateEndpoint(IN PVOID MiniPortExtension,
                           IN PVOID MiniPortEndpoint)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_ENDPOINT Endpoint;

    DPRINT_CORE("USBPORT_InvalidateEndpoint: ...\n");

    FdoExtension = (PUSBPORT_DEVICE_EXTENSION)((ULONG_PTR)MiniPortExtension -
                                               sizeof(USBPORT_DEVICE_EXTENSION));

    FdoDevice = FdoExtension->CommonExtension.SelfDevice;

    if (!MiniPortEndpoint)
    {
        USBPORT_InvalidateEndpointHandler(FdoDevice,
                                          NULL,
                                          INVALIDATE_ENDPOINT_ONLY);
        return 0;
    }

    Endpoint = (PUSBPORT_ENDPOINT)((ULONG_PTR)MiniPortEndpoint -
                                   sizeof(USBPORT_ENDPOINT));

    USBPORT_InvalidateEndpointHandler(FdoDevice,
                                      Endpoint,
                                      INVALIDATE_ENDPOINT_ONLY);

    return 0;
}

/*
 * Interrupt Transfer Reuse Implementation
 *
 * For interrupt endpoints (HID devices like mice/keyboards), the same transfer
 * structure can be reused across multiple poll cycles. This eliminates the
 * overhead of allocating and freeing transfer structures ~60+ times per second.
 *
 * Pattern: alloc once -> submit -> complete -> resubmit -> complete -> ... -> free on close
 */

BOOLEAN
NTAPI
USBPORT_IsInterruptTransferReusable(IN PUSBPORT_TRANSFER Transfer)
{
    PUSBPORT_ENDPOINT Endpoint;
    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties;

    if (!Transfer || !Transfer->Endpoint)
        return FALSE;

    Endpoint = Transfer->Endpoint;
    EndpointProperties = &Endpoint->EndpointProperties;

    /* Only interrupt IN endpoints can reuse transfers */
    if (EndpointProperties->TransferType != USBPORT_TRANSFER_TYPE_INTERRUPT)
        return FALSE;

    /* Only IN direction (device to host) - polling for input */
    if (Transfer->Direction != USBPORT_DMA_DIRECTION_FROM_DEVICE)
        return FALSE;

    /* Don't reuse if canceled, aborted, or device gone */
    if (Transfer->Flags & (TRANSFER_FLAG_CANCELED | TRANSFER_FLAG_ABORTED |
                           TRANSFER_FLAG_DEVICE_GONE))
        return FALSE;

    /* Don't reuse split or ISO transfers */
    if (Transfer->Flags & (TRANSFER_FLAG_SPLITED | TRANSFER_FLAG_ISO |
                           TRANSFER_FLAG_PARENT))
        return FALSE;

    /* Don't reuse bounce-buffered transfers (complex cleanup) */
    if (Transfer->Flags & TRANSFER_FLAG_BOUNCE)
        return FALSE;

    /* Don't reuse root hub endpoints */
    if (Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0)
        return FALSE;

    /* Endpoint must be healthy and not being closed */
    if (Endpoint->Flags & (ENDPOINT_FLAG_NUKE | ENDPOINT_FLAG_ABORTING |
                           ENDPOINT_FLAG_CLOSED))
        return FALSE;

    return TRUE;
}

VOID
NTAPI
USBPORT_ResetTransferForResubmit(IN PUSBPORT_TRANSFER Transfer)
{
    /*
     * Reset the transfer structure for resubmission.
     * Keep: Endpoint, Urb, buffer pointers, direction, period
     * Reset: Flags (completion-related), status, completed length
     */

    /* Clear completion and submission flags, keep REUSABLE */
    Transfer->Flags &= ~(TRANSFER_FLAG_COMPLETED | TRANSFER_FLAG_SUBMITED |
                         TRANSFER_FLAG_DMA_MAPPED | TRANSFER_FLAG_HIGH_SPEED);
    Transfer->Flags |= TRANSFER_FLAG_REUSABLE;

    /* Reset status */
    Transfer->USBDStatus = USBD_STATUS_SUCCESS;
    Transfer->CompletedTransferLen = 0;

    /* Reset DMA mapping state */
    Transfer->MapRegisterBase = NULL;
    Transfer->NumberOfMapRegisters = 0;

    /* Reset link pointers */
    Transfer->TransferLink.Flink = NULL;
    Transfer->TransferLink.Blink = NULL;

    /* Reset time tracking */
    Transfer->Time.QuadPart = 0;
}

VOID
NTAPI
USBPORT_ResubmitInterruptTransfer(IN PUSBPORT_TRANSFER Transfer)
{
    PUSBPORT_ENDPOINT Endpoint;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    KIRQL OldIrql;

    if (!Transfer || !Transfer->Endpoint)
        return;

    Endpoint = Transfer->Endpoint;
    FdoDevice = Endpoint->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;

    DPRINT_CORE("USBPORT_ResubmitInterruptTransfer: Transfer - %p, Endpoint - %p\n",
                Transfer, Endpoint);

    /* Reset the transfer for resubmission */
    USBPORT_ResetTransferForResubmit(Transfer);

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &OldIrql);

    /* Check endpoint is still healthy */
    if (Endpoint->Flags & (ENDPOINT_FLAG_NUKE | ENDPOINT_FLAG_ABORTING |
                           ENDPOINT_FLAG_CLOSED))
    {
        /* Endpoint is being torn down, don't resubmit */
        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, OldIrql);

        /* Clear reusable flag and mark for normal completion/free */
        Transfer->Flags &= ~TRANSFER_FLAG_REUSABLE;
        InterlockedExchange(&Endpoint->ReusableTransferInFlight, 0);
        Endpoint->ReusableTransfer = NULL;

        /* Free the transfer */
        ExFreePoolWithTag(Transfer, USB_PORT_TAG);
        return;
    }

    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, OldIrql);

    /*
     * Queue straight to the map list; USBPORT_MapTransfer links the mapped
     * transfer into Endpoint->TransferList itself. Parking it on the
     * endpoint list first meant unlinking it again under the map lock,
     * mutating Endpoint->TransferList under the wrong lock.
     */
    KeAcquireSpinLock(&FdoExtension->MapTransferSpinLock, &OldIrql);
    InsertTailList(&FdoExtension->MapTransferList, &Transfer->TransferLink);
    KeReleaseSpinLock(&FdoExtension->MapTransferSpinLock, OldIrql);

    /* Flush the map transfer list */
    USBPORT_FlushMapTransfers(FdoDevice);

    /* Trigger endpoint worker to submit the transfer */
    USBPORT_InvalidateEndpointHandler(FdoDevice,
                                      Endpoint,
                                      INVALIDATE_ENDPOINT_WORKER_DPC);
}

VOID
NTAPI
USBPORT_FreeReusableTransfer(IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_TRANSFER Transfer;
    KIRQL OldIrql;

    if (!Endpoint)
        return;

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &OldIrql);

    Transfer = Endpoint->ReusableTransfer;
    Endpoint->ReusableTransfer = NULL;
    InterlockedExchange(&Endpoint->ReusableTransferInFlight, 0);

    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, OldIrql);

    if (Transfer)
    {
        DPRINT_CORE("USBPORT_FreeReusableTransfer: Freeing Transfer - %p\n", Transfer);
        ExFreePoolWithTag(Transfer, USB_PORT_TAG);
    }
}

static
VOID
USBPORT_CleanupTransferOnBadUrb(IN PUSBPORT_TRANSFER Transfer,
                                IN USBD_STATUS TransferStatus)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDMA_OPERATIONS DmaOperations;
    PMDL Mdl;
    ULONG_PTR CurrentVa;
    SIZE_T TransferLength;
    BOOLEAN WriteToDevice;
    BOOLEAN IsFlushSuccess;
    KIRQL OldIrql;

    if (!Transfer)
        return;

    /* Best-effort cleanup without touching the URB */
    if (Transfer->Flags & TRANSFER_FLAG_DMA_MAPPED)
    {
        FdoDevice = Transfer->FdoDevice;
        if (FdoDevice)
        {
            FdoExtension = FdoDevice->DeviceExtension;
            DmaOperations = FdoExtension->DmaAdapter->DmaOperations;

            Mdl = Transfer->TransferBufferMDL;
            if (Mdl)
            {
                WriteToDevice = Transfer->Direction == USBPORT_DMA_DIRECTION_TO_DEVICE;
                CurrentVa = (ULONG_PTR)MmGetMdlVirtualAddress(Mdl);
                TransferLength = Transfer->CompletedTransferLen;
                if (!WriteToDevice &&
                    TransferLength == 0 &&
                    Transfer->TransferParameters.TransferBufferLength != 0)
                {
                    TransferLength = Transfer->TransferParameters.TransferBufferLength;
                }

                IsFlushSuccess = DmaOperations->FlushAdapterBuffers(FdoExtension->DmaAdapter,
                                                                    Mdl,
                                                                    Transfer->MapRegisterBase,
                                                                    (PVOID)CurrentVa,
                                                                    TransferLength,
                                                                    WriteToDevice);

                if (!IsFlushSuccess)
                {
                    DPRINT1("USBPORT_CleanupTransferOnBadUrb: FlushAdapterBuffers failed\n");
                }
            }

            KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
            DmaOperations->FreeMapRegisters(FdoExtension->DmaAdapter,
                                            Transfer->MapRegisterBase,
                                            Transfer->NumberOfMapRegisters);
            KeLowerIrql(OldIrql);
        }
    }

    if (Transfer->Flags & TRANSFER_FLAG_BOUNCE)
    {
        if (Transfer->BounceBuffer)
        {
            PDEVICE_OBJECT BounceFdo = Transfer->FdoDevice;
            if (BounceFdo)
                USBPORT_FreeCommonBuffer(BounceFdo, Transfer->BounceBuffer);
            Transfer->BounceBuffer = NULL;
        }

        if (Transfer->BounceMdl)
        {
            IoFreeMdl(Transfer->BounceMdl);
            Transfer->BounceMdl = NULL;
        }
    }

    if (Transfer->Flags & TRANSFER_FLAG_ALLOCATED_MDL)
    {
        if (Transfer->TransferBufferMDL &&
            Transfer->TransferBufferMDL != NULL)
        {
            IoFreeMdl(Transfer->TransferBufferMDL);
        }
        Transfer->Flags &= ~TRANSFER_FLAG_ALLOCATED_MDL;
    }

    DPRINT1("USBPORT_CleanupTransferOnBadUrb: dropping transfer %p status=%x Urb=%p\n",
            Transfer, TransferStatus, Transfer->Urb);

    ExFreePoolWithTag(Transfer, USB_PORT_TAG);
}

VOID
NTAPI
USBPORT_CompleteTransferSafe(IN PUSBPORT_TRANSFER Transfer,
                             IN USBD_STATUS TransferStatus)
{
    PURB Urb;

    if (!Transfer)
        return;

    Urb = Transfer->Urb;
if (!Urb)
    {
        USBPORT_CleanupTransferOnBadUrb(Transfer, TransferStatus);
        return;
    }

    USBPORT_CompleteTransfer(Urb, TransferStatus);
}

VOID
NTAPI
USBPORT_CompleteTransfer(IN PURB Urb,
                         IN USBD_STATUS TransferStatus)
{
    struct _URB_CONTROL_TRANSFER *UrbTransfer;
    PUSBPORT_TRANSFER Transfer;
    NTSTATUS Status;
    PIRP Irp;
    KIRQL OldIrql;
    PRKEVENT Event;
    BOOLEAN WriteToDevice;
    BOOLEAN IsFlushSuccess;
    PMDL Mdl;
    ULONG_PTR CurrentVa;
    SIZE_T TransferLength;
    PUSBPORT_ENDPOINT Endpoint;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDMA_OPERATIONS DmaOperations;

    DPRINT("USBPORT_CompleteTransfer: Urb - %p, TransferStatus - %X\n",
           Urb,
           TransferStatus);

if (!Urb)
    {
        DPRINT1("USBPORT_CompleteTransfer: invalid Urb=%p Status=%X\n",
                Urb, TransferStatus);
        return;
    }

    UrbTransfer = &Urb->UrbControlTransfer;
    Transfer = (PUSBPORT_TRANSFER)InterlockedExchangePointer(
        (PVOID *)&UrbTransfer->hca.Reserved8[0],
        NULL);
    if (!Transfer)
    {
        DPRINT1("USBPORT_CompleteTransfer: duplicate or missing transfer (Urb=%p Status=%X)\n",
                Urb,
                TransferStatus);
        return;
    }

    Transfer->USBDStatus = TransferStatus;
    Status = USBPORT_USBDStatusToNtStatus(Urb, TransferStatus);

    /* If the miniport reported success but zero length on an IN control transfer,
     * fall back to the requested length so cached data from a bounce buffer still
     * gets flushed back to the caller. */
    if ((Urb->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER) &&
        (Transfer->Direction == USBPORT_DMA_DIRECTION_FROM_DEVICE) &&
        NT_SUCCESS(Status) &&
        Transfer->CompletedTransferLen == 0 &&
        Transfer->TransferParameters.TransferBufferLength != 0)
    {
        Transfer->CompletedTransferLen = Transfer->TransferParameters.TransferBufferLength;
    }

    UrbTransfer->TransferBufferLength = Transfer->CompletedTransferLen;

    if (Transfer->Flags & TRANSFER_FLAG_DMA_MAPPED)
    {
        Endpoint = Transfer->Endpoint;
        FdoDevice = Transfer->FdoDevice;
        if (!FdoDevice && Endpoint)
        {
            FdoDevice = Endpoint->FdoDevice;
        }
        if (!FdoDevice)
        {
            DPRINT1("USBPORT_CompleteTransfer: missing FdoDevice for Transfer=%p\n",
                    Transfer);
            goto SkipDmaCleanup;
        }
        FdoExtension = FdoDevice->DeviceExtension;
        DmaOperations = FdoExtension->DmaAdapter->DmaOperations;

        WriteToDevice = Transfer->Direction == USBPORT_DMA_DIRECTION_TO_DEVICE;
        Mdl = UrbTransfer->TransferBufferMDL;
        CurrentVa = (ULONG_PTR)MmGetMdlVirtualAddress(Mdl);
        TransferLength = UrbTransfer->TransferBufferLength;
        if (!WriteToDevice &&
            TransferLength == 0 &&
            Transfer->TransferParameters.TransferBufferLength != 0)
        {
            TransferLength = Transfer->TransferParameters.TransferBufferLength;
        }

        IsFlushSuccess = DmaOperations->FlushAdapterBuffers(FdoExtension->DmaAdapter,
                                                            Mdl,
                                                            Transfer->MapRegisterBase,
                                                            (PVOID)CurrentVa,
                                                            TransferLength,
                                                            WriteToDevice);

        if (!IsFlushSuccess)
        {
            DPRINT1("USBPORT_CompleteTransfer: FlushAdapterBuffers failed\n");
        }

        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

        DmaOperations->FreeMapRegisters(FdoExtension->DmaAdapter,
                                        Transfer->MapRegisterBase,
                                        Transfer->NumberOfMapRegisters);

        KeLowerIrql(OldIrql);
    }
SkipDmaCleanup:

    if (Transfer->Flags & TRANSFER_FLAG_BOUNCE)
    {
        SIZE_T BytesToCopy = Transfer->CompletedTransferLen;

        if (Transfer->Direction == USBPORT_DMA_DIRECTION_FROM_DEVICE &&
            Transfer->BounceBuffer &&
            Transfer->BounceOriginalVa)
        {
            if (BytesToCopy == 0 && Transfer->TransferParameters.TransferBufferLength)
                BytesToCopy = Transfer->TransferParameters.TransferBufferLength;
            if (BytesToCopy > Transfer->BounceBufferLength)
                BytesToCopy = Transfer->BounceBufferLength;

            if (BytesToCopy)
            {
                RtlCopyMemory(Transfer->BounceOriginalVa,
                              (PVOID)Transfer->BounceBuffer->VirtualAddress,
                              BytesToCopy);
            }
        }

        if (Transfer->BounceBuffer)
        {
            PDEVICE_OBJECT BounceFdo = Transfer->FdoDevice;
            if (!BounceFdo && Transfer->Endpoint)
                BounceFdo = Transfer->Endpoint->FdoDevice;
            USBPORT_FreeCommonBuffer(BounceFdo, Transfer->BounceBuffer);
            Transfer->BounceBuffer = NULL;
        }

        if (Transfer->BounceMdl)
        {
            IoFreeMdl(Transfer->BounceMdl);
            Transfer->BounceMdl = NULL;
        }

        Urb->UrbControlTransfer.TransferBufferMDL = Transfer->BounceOriginalMdl;
        Urb->UrbControlTransfer.TransferBuffer = Transfer->BounceOriginalBuffer;
        Transfer->TransferBufferMDL = Transfer->BounceOriginalMdl;
        Transfer->BounceOriginalMdl = NULL;
        Transfer->BounceOriginalBuffer = NULL;
        Transfer->BounceOriginalVa = NULL;
        Transfer->BounceBufferLength = 0;
        Transfer->Flags &= ~TRANSFER_FLAG_BOUNCE;
    }

    if (Urb->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER)
    {
        PUSB_DEFAULT_PIPE_SETUP_PACKET SetupPkt =
            (PUSB_DEFAULT_PIPE_SETUP_PACKET)&UrbTransfer->SetupPacket[0];
        if (SetupPkt->bRequest == USB_REQUEST_GET_DESCRIPTOR &&
            SetupPkt->wValue.HiByte == USB_CONFIGURATION_DESCRIPTOR_TYPE)
        {
            PUCHAR Buf = UrbTransfer->TransferBuffer;
            ULONG Len = (ULONG)UrbTransfer->TransferBufferLength;

            if (Buf && Len >= 6)
            {
                DPRINT("USBPORT_CompleteTransfer: CFG DESC len=%lu first=%02x %02x %02x %02x %02x %02x\n",
                       Len,
                       Buf[0], Buf[1], Buf[2], Buf[3], Buf[4], Buf[5]);
            }
            else
            {
                DPRINT("USBPORT_CompleteTransfer: CFG DESC len=%lu buf=%p\n",
                       Len, Buf);
            }
        }
    }

    if (Urb->UrbHeader.UsbdFlags & USBD_FLAG_ALLOCATED_MDL)
    {
        IoFreeMdl(Transfer->TransferBufferMDL);
        Urb->UrbHeader.UsbdFlags &= ~USBD_FLAG_ALLOCATED_MDL;
        Transfer->Flags &= ~TRANSFER_FLAG_ALLOCATED_MDL;
    }

    Urb->UrbHeader.UsbdFlags &= ~USBD_FLAG_ALLOCATED_TRANSFER;

    Irp = Transfer->Irp;

    if (Irp)
    {
        if (!NT_SUCCESS(Status))
        {
            //DbgBreakPoint();
            DPRINT_CORE("USBPORT_CompleteTransfer: Irp - %p complete with Status - %lx\n",
                    Irp,
                    Status);

            USBPORT_DumpingURB(Urb);
        }

        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;

        DPRINT("USBPORT_CompleteTransfer: IoCompleteRequest Irp=%p Status=0x%lx USBD=0x%x Len=%lu\n",
               Irp, Status, TransferStatus, UrbTransfer->TransferBufferLength);

        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        KeLowerIrql(OldIrql);
    }

    Event = Transfer->Event;

    if (Event)
    {
        KeSetEvent(Event, EVENT_INCREMENT, FALSE);
    }

    /*
     * For successful interrupt IN transfers, cache the transfer structure
     * in the endpoint for reuse instead of freeing it. This eliminates
     * the alloc/free overhead for HID device polling (~60+ times/sec).
     */
    Endpoint = Transfer->Endpoint;
    if (NT_SUCCESS(Status) &&
        USBPORT_IsInterruptTransferReusable(Transfer) &&
        Endpoint->ReusableTransfer == NULL)
    {
        KIRQL CacheIrql;

        KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &CacheIrql);

        /*
         * Never cache a transfer that is still linked somewhere, or reuse
         * would reset live list links.
         */
        if (Endpoint->ReusableTransfer == NULL &&
            Transfer->TransferLink.Flink == NULL &&
            Transfer->TransferLink.Blink == NULL &&
            !(Endpoint->Flags & (ENDPOINT_FLAG_NUKE | ENDPOINT_FLAG_ABORTING |
                                  ENDPOINT_FLAG_CLOSED)))
        {
            /* Cache for reuse - clear the in-flight marker first */
            InterlockedExchange(&Endpoint->ReusableTransferInFlight, 0);

            /* Mark as reusable and cache */
            Transfer->Flags |= TRANSFER_FLAG_REUSABLE;
            Transfer->Flags &= ~TRANSFER_FLAG_COMPLETED;

            /* Clear per-request fields to avoid stale references */
            Transfer->Irp = NULL;
            Transfer->Urb = NULL;
            Transfer->Event = NULL;
            Transfer->DoneLink.Flink = NULL;
            Transfer->DoneLink.Blink = NULL;

            Endpoint->ReusableTransfer = Transfer;
            Transfer = NULL; /* Prevent free below */

            DPRINT_CORE("USBPORT_CompleteTransfer: Cached Transfer for reuse on Endpoint - %p\n",
                        Endpoint);
        }

        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, CacheIrql);
    }

    if (Transfer)
    {
        /* Clear the reusable in-flight flag if we're freeing.
         * Only access Endpoint when the transfer succeeded - on failure
         * (DEVICE_GONE, CANCELED) the endpoint may already be freed,
         * and Transfer->Endpoint would be a stale pointer. */
        if (NT_SUCCESS(Status) && Endpoint &&
            (Transfer->Flags & TRANSFER_FLAG_REUSABLE))
        {
            InterlockedExchange(&Endpoint->ReusableTransferInFlight, 0);
        }

        ExFreePoolWithTag(Transfer, USB_PORT_TAG);
    }

    DPRINT_CORE("USBPORT_CompleteTransfer: exit\n");
}

IO_ALLOCATION_ACTION
NTAPI
USBPORT_MapTransfer(IN PDEVICE_OBJECT FdoDevice,
                    IN PIRP Irp,
                    IN PVOID MapRegisterBase,
                    IN PVOID Context)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDMA_ADAPTER DmaAdapter;
    PUSBPORT_TRANSFER Transfer;
    PURB Urb;
    PUSBPORT_ENDPOINT Endpoint;
    PMDL Mdl;
    ULONG_PTR CurrentVa;
    PUSBPORT_SCATTER_GATHER_LIST sgList;
    SIZE_T CurrentLength;
    ULONG ix;
    BOOLEAN WriteToDevice;
    PHYSICAL_ADDRESS PhAddr = {{0, 0}};
    PHYSICAL_ADDRESS PhAddress = {{0, 0}};
    ULONG TransferLength;
    SIZE_T SgCurrentLength;
    SIZE_T ElementLength;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    PDMA_OPERATIONS DmaOperations;
    USBD_STATUS USBDStatus;
    LIST_ENTRY List;
    PUSBPORT_TRANSFER transfer;
    LONG CallbackState;

    DPRINT_CORE("USBPORT_MapTransfer: ...\n");

    FdoExtension = FdoDevice->DeviceExtension;
    DmaAdapter = FdoExtension->DmaAdapter;
    DmaOperations = DmaAdapter->DmaOperations;

    Transfer = Context;

    Urb = Transfer->Urb;
    Endpoint = Transfer->Endpoint;
    TransferLength = Transfer->TransferParameters.TransferBufferLength;

    Mdl = Urb->UrbControlTransfer.TransferBufferMDL;
    CurrentVa = (ULONG_PTR)MmGetMdlVirtualAddress(Mdl);

    if (Endpoint &&
        (Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK ||
         TransferLength >= 64))
    {
        DPRINT("USBPORT_MapTransfer: Transfer=%p Ep=%p Len=%lu Flags=0x%lx Mdl=%p VA=%p\n",
               Transfer,
               Endpoint,
               TransferLength,
               Transfer->TransferParameters.TransferFlags,
               Mdl,
               (PVOID)CurrentVa);
    }

    sgList = &Transfer->SgList;

    sgList->Flags = 0;
    sgList->CurrentVa = CurrentVa;
    sgList->MappedSystemVa = MmGetSystemAddressForMdlSafe(Mdl,
                                                          NormalPagePriority);
    Transfer->MapRegisterBase = MapRegisterBase;

    ix = 0;
    CurrentLength = 0;

    do
    {
        WriteToDevice = Transfer->Direction == USBPORT_DMA_DIRECTION_TO_DEVICE;
        ASSERT(Transfer->Direction != 0);

        PhAddress = DmaOperations->MapTransfer(DmaAdapter,
                                               Mdl,
                                               MapRegisterBase,
                                               (PVOID)CurrentVa,
                                               &TransferLength,
                                               WriteToDevice);

        DPRINT_CORE("USBPORT_MapTransfer: PhAddress.LowPart - %p, PhAddress.HighPart - %x, TransferLength - %x\n",
               PhAddress.LowPart,
               PhAddress.HighPart,
               TransferLength);

        SgCurrentLength = TransferLength;

        if (Transfer->Flags & TRANSFER_FLAG_BOUNCE)
        {
            sgList->SgElement[ix].SgPhysicalAddress = PhAddress;
            sgList->SgElement[ix].SgTransferLength = SgCurrentLength;
            sgList->SgElement[ix].SgOffset = CurrentLength;

            PhAddress.QuadPart += SgCurrentLength;
            SgCurrentLength = 0;

            ++ix;
        }
        else
        {
            do
            {
                ElementLength = PAGE_SIZE - (PhAddress.LowPart & (PAGE_SIZE - 1));

                if (ElementLength > SgCurrentLength)
                    ElementLength = SgCurrentLength;

                DPRINT_CORE("USBPORT_MapTransfer: PhAddress.LowPart - %p, HighPart - %x, ElementLength - %x\n",
                       PhAddress.LowPart,
                       PhAddress.HighPart,
                       ElementLength);

                sgList->SgElement[ix].SgPhysicalAddress = PhAddress;
                sgList->SgElement[ix].SgTransferLength = ElementLength;
                sgList->SgElement[ix].SgOffset = CurrentLength +
                                                 (TransferLength - SgCurrentLength);

                PhAddress.QuadPart += ElementLength;
                SgCurrentLength -= ElementLength;

                ++ix;
            }
            while (SgCurrentLength);
        }

        if (PhAddr.QuadPart == PhAddress.QuadPart)
        {
            DPRINT1("USBPORT_MapTransfer: PhAddr == PhAddress\n");
            break;
        }

        PhAddr = PhAddress;

        CurrentLength += TransferLength;
        CurrentVa += TransferLength;

        TransferLength = Transfer->TransferParameters.TransferBufferLength -
                         CurrentLength;
    }
    while (CurrentLength != Transfer->TransferParameters.TransferBufferLength);

    sgList->SgElementCount = ix;

    if (Endpoint &&
        (Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK ||
         Transfer->TransferParameters.TransferBufferLength >= 64))
    {
        DPRINT("USBPORT_MapTransfer: SG count=%lu TotalLen=%lu\n",
               sgList->SgElementCount,
               Transfer->TransferParameters.TransferBufferLength);
    }

    if (Endpoint->EndpointProperties.DeviceSpeed == UsbHighSpeed ||
        Endpoint->EndpointProperties.DeviceSpeed == UsbSuperSpeed)
    {
        Transfer->Flags |= TRANSFER_FLAG_HIGH_SPEED;
    }

    Transfer->Flags |= TRANSFER_FLAG_DMA_MAPPED;

    if ((Transfer->Flags & TRANSFER_FLAG_ISO) == 0)
    {
        KeAcquireSpinLock(&Endpoint->EndpointSpinLock,
                          &Endpoint->EndpointOldIrql);

        USBPORT_SplitTransfer(FdoDevice, Endpoint, Transfer, &List);

        while (!IsListEmpty(&List))
        {
            transfer = CONTAINING_RECORD(List.Flink,
                                         USBPORT_TRANSFER,
                                         TransferLink);

            RemoveHeadList(&List);
            InsertTailList(&Endpoint->TransferList, &transfer->TransferLink);
        }

        KeReleaseSpinLock(&Endpoint->EndpointSpinLock,
                          Endpoint->EndpointOldIrql);
    }
    else
    {
        USBDStatus = USBPORT_InitializeIsoTransfer(FdoDevice,
                                                   &Urb->UrbIsochronousTransfer,
                                                   Transfer);

        if (USBDStatus != USBD_STATUS_SUCCESS)
        {
            KeAcquireSpinLock(&Endpoint->EndpointSpinLock,
                              &Endpoint->EndpointOldIrql);

            USBPORT_QueueDoneTransfer(Transfer, USBDStatus, TRUE);

            KeReleaseSpinLock(&Endpoint->EndpointSpinLock,
                              Endpoint->EndpointOldIrql);
        }
        else
        {
            KeAcquireSpinLock(&Endpoint->EndpointSpinLock,
                              &Endpoint->EndpointOldIrql);

            InsertTailList(&Endpoint->TransferList, &Transfer->TransferLink);

            KeReleaseSpinLock(&Endpoint->EndpointSpinLock,
                              Endpoint->EndpointOldIrql);
        }
    }

    DeviceHandle = Urb->UrbHeader.UsbdDeviceHandle;
    InterlockedDecrement(&DeviceHandle->DeviceHandleLock);

    if (USBPORT_EndpointWorker(Endpoint, 0))
    {
        USBPORT_InvalidateEndpointHandler(FdoDevice,
                                          Endpoint,
                                          INVALIDATE_ENDPOINT_WORKER_THREAD);
    }

    CallbackState = InterlockedExchange(&FdoExtension->MapTransferCallbackState,
                                        USBPORT_MAP_CALLBACK_DONE);
    if (CallbackState == USBPORT_MAP_CALLBACK_PENDING)
    {
        /* This processor cannot run the DPC until the callback returns. */
        KeInsertQueueDpc(&FdoExtension->MapTransferDpc, NULL, NULL);
    }

    return DeallocateObjectKeepRegisters;
}

static
NTSTATUS
USBPORT_SubmitMapTransfer(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PLIST_ENTRY MapTransferList;
    PUSBPORT_TRANSFER Transfer;
    ULONG NumMapRegisters;
    PMDL Mdl;
    SIZE_T TransferBufferLength;
    ULONG_PTR VirtualAddr;
    NTSTATUS Status;
    NTSTATUS FlushStatus = STATUS_SUCCESS;
    LONG CallbackState;
    PDMA_OPERATIONS DmaOperations;

    FdoExtension = FdoDevice->DeviceExtension;
    DmaOperations = FdoExtension->DmaAdapter->DmaOperations;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    while (TRUE)
    {
        KeAcquireSpinLockAtDpcLevel(&FdoExtension->MapTransferSpinLock);

        MapTransferList = &FdoExtension->MapTransferList;

        if (IsListEmpty(&FdoExtension->MapTransferList))
        {
            /* Do not let an enqueuer miss the transition back to idle. */
            InterlockedExchange(&FdoExtension->MapTransferBusy, 0);
            KeReleaseSpinLockFromDpcLevel(&FdoExtension->MapTransferSpinLock);
            return FlushStatus;
        }

        Transfer = CONTAINING_RECORD(MapTransferList->Flink,
                                     USBPORT_TRANSFER,
                                     TransferLink);

        RemoveHeadList(MapTransferList);
        Transfer->TransferLink.Flink = NULL;
        Transfer->TransferLink.Blink = NULL;

        KeReleaseSpinLockFromDpcLevel(&FdoExtension->MapTransferSpinLock);

        Mdl = Transfer->Urb->UrbControlTransfer.TransferBufferMDL;
        TransferBufferLength = Transfer->TransferParameters.TransferBufferLength;
        VirtualAddr = (ULONG_PTR)MmGetMdlVirtualAddress(Mdl);

        if (Transfer->Endpoint &&
            (Transfer->Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK ||
             TransferBufferLength >= 64))
        {
            DPRINT("USBPORT_FlushMapTransfers: Transfer=%p Ep=%p Len=%lu Mdl=%p VA=%p\n",
                   Transfer,
                   Transfer->Endpoint,
                   TransferBufferLength,
                   Mdl,
                   (PVOID)VirtualAddr);
        }

        NumMapRegisters = ADDRESS_AND_SIZE_TO_SPAN_PAGES(VirtualAddr,
                                                         TransferBufferLength);

        Transfer->NumberOfMapRegisters = NumMapRegisters;

        Status = DmaOperations->AllocateAdapterChannel(FdoExtension->DmaAdapter,
                                                       FdoDevice,
                                                       NumMapRegisters,
                                                       USBPORT_MapTransfer,
                                                       Transfer);

        if (Transfer->Endpoint &&
            (Transfer->Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK ||
             TransferBufferLength >= 64))
        {
            DPRINT("USBPORT_FlushMapTransfers: AllocateAdapterChannel Status=0x%lx\n",
                   Status);
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("USBPORT_FlushMapTransfers: AllocateAdapterChannel failed (0x%lx)\n", Status);
            if (NT_SUCCESS(FlushStatus))
                FlushStatus = Status;

            USBPORT_QueueDoneTransfer(Transfer,
                                      USBD_STATUS_INSUFFICIENT_RESOURCES,
                                      TRUE);
            continue;
        }

        /* Keep draining if the callback completed before the call returned. */
        CallbackState = InterlockedCompareExchange(&FdoExtension->MapTransferCallbackState,
                                                   USBPORT_MAP_CALLBACK_PENDING,
                                                   USBPORT_MAP_CALLBACK_NONE);
        if (CallbackState == USBPORT_MAP_CALLBACK_DONE)
        {
            InterlockedExchange(&FdoExtension->MapTransferCallbackState,
                                USBPORT_MAP_CALLBACK_NONE);
            continue;
        }

        return FlushStatus;
    }
}

NTSTATUS
NTAPI
USBPORT_FlushMapTransfers(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    KIRQL OldIrql;
    NTSTATUS Status;

    DPRINT_CORE("USBPORT_FlushMapTransfers: ...\n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);

    if (InterlockedCompareExchange(&FdoExtension->MapTransferBusy, 1, 0))
    {
        KeLowerIrql(OldIrql);
        return STATUS_SUCCESS;
    }

    Status = USBPORT_SubmitMapTransfer(FdoDevice);

    KeLowerIrql(OldIrql);
    return Status;
}

VOID
NTAPI
USBPORT_MapTransferDpc(IN PRKDPC Dpc,
                       IN PVOID DeferredContext,
                       IN PVOID SystemArgument1,
                       IN PVOID SystemArgument2)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT FdoDevice = DeferredContext;

    FdoExtension = FdoDevice->DeviceExtension;
    InterlockedExchange(&FdoExtension->MapTransferCallbackState,
                        USBPORT_MAP_CALLBACK_NONE);
    USBPORT_SubmitMapTransfer(FdoDevice);
}

USBD_STATUS
NTAPI
USBPORT_AllocateTransfer(IN PDEVICE_OBJECT FdoDevice,
                         IN PURB Urb,
                         IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                         IN PIRP Irp,
                         IN PRKEVENT Event)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    SIZE_T TransferLength;
    PMDL Mdl;
    ULONG_PTR VirtualAddr;
    ULONG PagesNeed = 0;
    SIZE_T PortTransferLength;
    SIZE_T FullTransferLength;
    PUSBPORT_TRANSFER Transfer;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    USBD_STATUS USBDStatus;
    SIZE_T IsoBlockLen = 0;
    PUSBPORT_ENDPOINT Endpoint;
    KIRQL OldIrql;
    BOOLEAN ReusingTransfer = FALSE;

    DPRINT_CORE("USBPORT_AllocateTransfer: FdoDevice - %p, Urb - %p, DeviceHandle - %p, Irp - %p, Event - %p\n",
           FdoDevice,
           Urb,
           DeviceHandle,
           Irp,
           Event);

    FdoExtension = FdoDevice->DeviceExtension;

    switch (Urb->UrbHeader.Function)
    {
        case URB_FUNCTION_ISOCH_TRANSFER:
            TransferLength = Urb->UrbIsochronousTransfer.TransferBufferLength;
            PipeHandle = Urb->UrbIsochronousTransfer.PipeHandle;
            Mdl = Urb->UrbIsochronousTransfer.TransferBufferMDL;
            break;

        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        case URB_FUNCTION_CONTROL_TRANSFER:
        default:
            TransferLength = Urb->UrbControlTransfer.TransferBufferLength;
            PipeHandle = Urb->UrbControlTransfer.PipeHandle;
            Mdl = Urb->UrbControlTransfer.TransferBufferMDL;
            break;
    }

    Endpoint = PipeHandle->Endpoint;

    /*
     * Try to reuse a cached interrupt transfer structure.
     * This optimization eliminates alloc/free overhead for HID polling.
     */
    if (Urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER &&
        Endpoint &&
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT &&
        Endpoint->ReusableTransfer != NULL &&
        !InterlockedCompareExchange(&Endpoint->ReusableTransferInFlight, 1, 0))
    {
        KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &OldIrql);

        Transfer = Endpoint->ReusableTransfer;
        if (Transfer &&
            Transfer->FullTransferLength >= (sizeof(USBPORT_TRANSFER) +
                sizeof(USBPORT_MINIPORT_TRANSFER_HEADER) +
                FdoExtension->MiniPortInterface->Packet.MiniPortTransferSize) &&
            Transfer->TransferParameters.TransferBufferLength == TransferLength)
        {
            /* Reuse the cached transfer */
            Endpoint->ReusableTransfer = NULL;
            ReusingTransfer = TRUE;

            DPRINT_CORE("USBPORT_AllocateTransfer: Reusing Transfer - %p for Endpoint - %p\n",
                        Transfer, Endpoint);
        }
        else
        {
            /* Cannot reuse (size mismatch or NULL), release the lock */
            InterlockedExchange(&Endpoint->ReusableTransferInFlight, 0);
            Transfer = NULL;
        }

        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, OldIrql);
    }

    if (ReusingTransfer)
    {
        /* Reset the transfer for new use */
        Transfer->Flags &= ~(TRANSFER_FLAG_COMPLETED | TRANSFER_FLAG_SUBMITED |
                             TRANSFER_FLAG_DMA_MAPPED | TRANSFER_FLAG_HIGH_SPEED |
                             TRANSFER_FLAG_CANCELED | TRANSFER_FLAG_ABORTED |
                             TRANSFER_FLAG_ALLOCATED_MDL);
        Transfer->Flags |= TRANSFER_FLAG_REUSABLE;
        Transfer->USBDStatus = USBD_STATUS_SUCCESS;
        Transfer->CompletedTransferLen = 0;
        Transfer->MapRegisterBase = NULL;
        Transfer->NumberOfMapRegisters = 0;
        Transfer->TransferLink.Flink = NULL;
        Transfer->TransferLink.Blink = NULL;
        Transfer->DoneLink.Flink = NULL;
        Transfer->DoneLink.Blink = NULL;
        Transfer->Time.QuadPart = 0;

        /* Update per-request fields */
        Transfer->Irp = Irp;
        Transfer->Urb = Urb;
        Transfer->Event = Event;
        Transfer->FdoDevice = FdoDevice;
        Transfer->TransferBufferMDL = Mdl;
        Transfer->TransferParameters.TransferBufferLength = TransferLength;
        Transfer->TransferParameters.TransferFlags = Urb->UrbControlTransfer.TransferFlags;
        Transfer->TransferParameters.Reserved2 = 0;
        if (PipeHandle &&
            PipeHandle->StreamId != 0 &&
            Endpoint &&
            Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK &&
            Endpoint->EndpointProperties.DeviceSpeed == UsbSuperSpeed)
        {
            Transfer->TransferParameters.Reserved2 = PipeHandle->StreamId;
        }

        if (Urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)
            Transfer->Direction = USBPORT_DMA_DIRECTION_FROM_DEVICE;
        else
            Transfer->Direction = USBPORT_DMA_DIRECTION_TO_DEVICE;

        if (Urb->UrbHeader.UsbdFlags & USBD_FLAG_ALLOCATED_MDL)
            Transfer->Flags |= TRANSFER_FLAG_ALLOCATED_MDL;

        Urb->UrbControlTransfer.hca.Reserved8[0] = Transfer;
        Urb->UrbHeader.UsbdFlags |= USBD_FLAG_ALLOCATED_TRANSFER;

        return USBD_STATUS_SUCCESS;
    }

    /* Standard allocation path */
    if (TransferLength && Mdl)
    {
        VirtualAddr = (ULONG_PTR)MmGetMdlVirtualAddress(Mdl);

        PagesNeed = ADDRESS_AND_SIZE_TO_SPAN_PAGES(VirtualAddr,
                                                   TransferLength);
        if (PagesNeed > 0)
        {
            PagesNeed--;
        }
    }

    if (Urb->UrbHeader.Function == URB_FUNCTION_ISOCH_TRANSFER)
    {
        IsoBlockLen = USBPORT_ISO_BLOCK_SIZE(
            Urb->UrbIsochronousTransfer.NumberOfPackets);
    }

    PortTransferLength = sizeof(USBPORT_TRANSFER) +
                         PagesNeed * sizeof(USBPORT_SCATTER_GATHER_ELEMENT) +
                         IsoBlockLen;

    FullTransferLength = PortTransferLength +
                         sizeof(USBPORT_MINIPORT_TRANSFER_HEADER) +
                         FdoExtension->MiniPortInterface->Packet.MiniPortTransferSize;

    Transfer = ExAllocatePoolWithTag(NonPagedPool,
                                     FullTransferLength,
                                     USB_PORT_TAG);

    if (!Transfer)
    {
        DPRINT_CORE("USBPORT_AllocateTransfer: Transfer not allocated!\n");
        return USBD_STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Transfer, FullTransferLength);

    Transfer->Irp = Irp;
    Transfer->Urb = Urb;
    Transfer->Endpoint = PipeHandle->Endpoint;
    Transfer->FdoDevice = FdoDevice;
    Transfer->Event = Event;
    Transfer->PortTransferLength = PortTransferLength;
    Transfer->FullTransferLength = FullTransferLength;
    Transfer->IsoBlockPtr = NULL;
    Transfer->Period = 0;
    Transfer->ParentTransfer = Transfer;
    Transfer->BounceBuffer = NULL;
    Transfer->BounceOriginalVa = NULL;
    Transfer->BounceBufferLength = 0;
    Transfer->BounceMdl = NULL;
    Transfer->BounceOriginalMdl = NULL;
    Transfer->BounceOriginalBuffer = NULL;

    switch (Urb->UrbHeader.Function)
    {
        case URB_FUNCTION_ISOCH_TRANSFER:
            Transfer->TransferBufferMDL =
                Urb->UrbIsochronousTransfer.TransferBufferMDL;
            Transfer->TransferParameters.TransferBufferLength = TransferLength;
            Transfer->TransferParameters.TransferFlags =
                Urb->UrbIsochronousTransfer.TransferFlags;
            Transfer->TransferParameters.Reserved2 = 0;

            if (Urb->UrbIsochronousTransfer.TransferFlags &
                USBD_TRANSFER_DIRECTION_IN)
            {
                Transfer->Direction = USBPORT_DMA_DIRECTION_FROM_DEVICE;
            }
            else
            {
                Transfer->Direction = USBPORT_DMA_DIRECTION_TO_DEVICE;
            }
            break;

        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        case URB_FUNCTION_CONTROL_TRANSFER:
        default:
            Transfer->TransferBufferMDL =
                Urb->UrbControlTransfer.TransferBufferMDL;
            Transfer->TransferParameters.TransferBufferLength = TransferLength;
            Transfer->TransferParameters.TransferFlags =
                Urb->UrbControlTransfer.TransferFlags;
            Transfer->TransferParameters.Reserved2 = 0;
            if (PipeHandle &&
                PipeHandle->StreamId != 0 &&
                Endpoint &&
                Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK &&
                Endpoint->EndpointProperties.DeviceSpeed == UsbSuperSpeed)
            {
                Transfer->TransferParameters.Reserved2 = PipeHandle->StreamId;
            }

            if (Urb->UrbControlTransfer.TransferFlags &
                USBD_TRANSFER_DIRECTION_IN)
            {
                Transfer->Direction = USBPORT_DMA_DIRECTION_FROM_DEVICE;
            }
            else
            {
                Transfer->Direction = USBPORT_DMA_DIRECTION_TO_DEVICE;
            }
            break;
    }

    if (Urb->UrbHeader.UsbdFlags & USBD_FLAG_ALLOCATED_MDL)
        Transfer->Flags |= TRANSFER_FLAG_ALLOCATED_MDL;

    if (IsoBlockLen)
    {
        Transfer->IsoBlockPtr = (PVOID)((ULONG_PTR)Transfer +
                                 PortTransferLength - IsoBlockLen);

        Transfer->Period = PipeHandle->Endpoint->EndpointProperties.Period;
        Transfer->Flags |= TRANSFER_FLAG_ISO;
    }

    USBPORT_InitializeMiniportTransfer(Transfer);

    KeInitializeSpinLock(&Transfer->TransferSpinLock);

    Urb->UrbControlTransfer.hca.Reserved8[0] = Transfer;
    Urb->UrbHeader.UsbdFlags |= USBD_FLAG_ALLOCATED_TRANSFER;

    if (TransferLength &&
        Transfer->TransferBufferMDL &&
        !(Transfer->Flags & TRANSFER_FLAG_ISO))
    {
        NTSTATUS BounceStatus;

        BounceStatus = USBPORT_SetupTransferBounceBuffer(FdoDevice,
                                                         Transfer,
                                                         Urb);

        if (!NT_SUCCESS(BounceStatus))
        {
            Urb->UrbControlTransfer.hca.Reserved8[0] = NULL;
            Urb->UrbHeader.UsbdFlags &= ~USBD_FLAG_ALLOCATED_TRANSFER;
            ExFreePoolWithTag(Transfer, USB_PORT_TAG);
            return USBD_STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    USBDStatus = USBD_STATUS_SUCCESS;

    DPRINT_CORE("USBPORT_AllocateTransfer: return USBDStatus - %x\n",
                USBDStatus);

    return USBDStatus;
}

NTSTATUS
NTAPI
USBPORT_Dispatch(IN PDEVICE_OBJECT DeviceObject,
                 IN PIRP Irp)
{
    PUSBPORT_COMMON_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status = STATUS_SUCCESS;

    DeviceExtension = DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceExtension->PnpStateFlags & USBPORT_PNP_STATE_FAILED)
    {
        DPRINT1("USBPORT_Dispatch: device in failed state, rejecting IRP\n");
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    switch (IoStack->MajorFunction)
    {
        case IRP_MJ_DEVICE_CONTROL:
            if (DeviceExtension->IsPDO)
            {
                DPRINT("USBPORT_Dispatch: PDO IRP_MJ_DEVICE_CONTROL. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_PdoDeviceControl(DeviceObject, Irp);
            }
            else
            {
                DPRINT("USBPORT_Dispatch: FDO IRP_MJ_DEVICE_CONTROL. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_FdoDeviceControl(DeviceObject, Irp);
            }

            break;

        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
            if (DeviceExtension->IsPDO)
            {
                DPRINT("USBPORT_Dispatch: PDO IRP_MJ_INTERNAL_DEVICE_CONTROL. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_PdoInternalDeviceControl(DeviceObject, Irp);
            }
            else
            {
                DPRINT("USBPORT_Dispatch: FDO IRP_MJ_INTERNAL_DEVICE_CONTROL. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_FdoInternalDeviceControl(DeviceObject, Irp);
            }

            break;

        case IRP_MJ_POWER:
            if (DeviceExtension->IsPDO)
            {
                DPRINT("USBPORT_Dispatch: PDO IRP_MJ_POWER. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_PdoPower(DeviceObject, Irp);
            }
            else
            {
                DPRINT("USBPORT_Dispatch: FDO IRP_MJ_POWER. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_FdoPower(DeviceObject, Irp);
            }

            break;

        case IRP_MJ_SYSTEM_CONTROL:
            if (DeviceExtension->IsPDO)
            {
                DPRINT("USBPORT_Dispatch: PDO IRP_MJ_SYSTEM_CONTROL. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = Irp->IoStatus.Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
            }
            else
            {
                DPRINT("USBPORT_Dispatch: FDO IRP_MJ_SYSTEM_CONTROL. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            }

            break;

        case IRP_MJ_PNP:
            if (DeviceExtension->IsPDO)
            {
                DPRINT("USBPORT_Dispatch: PDO IRP_MJ_PNP. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_PdoPnP(DeviceObject, Irp);
            }
            else
            {
                DPRINT("USBPORT_Dispatch: FDO IRP_MJ_PNP. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);

                Status = USBPORT_FdoPnP(DeviceObject, Irp);
            }

            break;

        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
            DPRINT("USBPORT_Dispatch: IRP_MJ_CREATE | IRP_MJ_CLOSE\n");
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            break;

        default:
            if (DeviceExtension->IsPDO)
            {
                DPRINT("USBPORT_Dispatch: PDO unhandled IRP_MJ_???. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);
            }
            else
            {
                DPRINT("USBPORT_Dispatch: FDO unhandled IRP_MJ_???. Major - %d, Minor - %d\n",
                       IoStack->MajorFunction,
                       IoStack->MinorFunction);
            }

            Status = STATUS_INVALID_DEVICE_REQUEST;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            break;
    }

    DPRINT("USBPORT_Dispatch: Status - %x\n", Status);
    return Status;
}

ULONG
NTAPI
USBPORT_GetHciMn(VOID)
{
    return USBPORT_HCI_MN;
}

NTSTATUS
NTAPI
USBPORT_RegisterUSBPortDriver(IN PDRIVER_OBJECT DriverObject,
                              IN ULONG Version,
                              IN PUSBPORT_REGISTRATION_PACKET RegPacket)
{
    PUSBPORT_MINIPORT_INTERFACE MiniPortInterface;

    DPRINT("USBPORT_RegisterUSBPortDriver: DriverObject - %p, Version - %p, RegPacket - %p\n",
           DriverObject,
           Version,
           RegPacket);

    DPRINT("USBPORT_RegisterUSBPortDriver: sizeof(USBPORT_MINIPORT_INTERFACE) - %x\n",
           sizeof(USBPORT_MINIPORT_INTERFACE));

    DPRINT("USBPORT_RegisterUSBPortDriver: sizeof(USBPORT_DEVICE_EXTENSION)   - %x\n",
           sizeof(USBPORT_DEVICE_EXTENSION));

    if (Version < USB10_MINIPORT_INTERFACE_VERSION)
    {
        return STATUS_UNSUCCESSFUL;
    }

    if (!USBPORT_Initialized)
    {
        InitializeListHead(&USBPORT_MiniPortDrivers);
        InitializeListHead(&USBPORT_USB1FdoList);
        InitializeListHead(&USBPORT_USB2FdoList);

        KeInitializeSpinLock(&USBPORT_SpinLock);
        USBPORT_Initialized = TRUE;
    }

    MiniPortInterface = ExAllocatePoolWithTag(NonPagedPool,
                                              sizeof(USBPORT_MINIPORT_INTERFACE),
                                              USB_PORT_TAG);
    if (!MiniPortInterface)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(MiniPortInterface, sizeof(USBPORT_MINIPORT_INTERFACE));

    MiniPortInterface->DriverObject = DriverObject;
    MiniPortInterface->DriverUnload = DriverObject->DriverUnload;
    MiniPortInterface->Version = Version;

    ExInterlockedInsertTailList(&USBPORT_MiniPortDrivers,
                                &MiniPortInterface->DriverLink,
                                &USBPORT_SpinLock);

    DriverObject->DriverExtension->AddDevice = USBPORT_AddDevice;
    DriverObject->DriverUnload = USBPORT_Unload;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = USBPORT_Dispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = USBPORT_Dispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = USBPORT_Dispatch;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = USBPORT_Dispatch;
    DriverObject->MajorFunction[IRP_MJ_PNP] = USBPORT_Dispatch;
    DriverObject->MajorFunction[IRP_MJ_POWER] = USBPORT_Dispatch;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = USBPORT_Dispatch;

    RegPacket->UsbPortDbgPrint = USBPORT_DbgPrint;
    RegPacket->UsbPortTestDebugBreak = USBPORT_TestDebugBreak;
    RegPacket->UsbPortAssertFailure = USBPORT_AssertFailure;
    RegPacket->UsbPortGetMiniportRegistryKeyValue = USBPORT_GetMiniportRegistryKeyValue;
    RegPacket->UsbPortInvalidateRootHub = USBPORT_InvalidateRootHub;
    RegPacket->UsbPortInvalidateEndpoint = USBPORT_InvalidateEndpoint;
    RegPacket->UsbPortCompleteTransfer = USBPORT_MiniportCompleteTransfer;
    RegPacket->UsbPortCompleteIsoTransfer = USBPORT_CompleteIsoTransfer;
    RegPacket->UsbPortLogEntry = USBPORT_LogEntry;
    RegPacket->UsbPortGetMappedVirtualAddress = USBPORT_GetMappedVirtualAddress;
    RegPacket->UsbPortRequestAsyncCallback = USBPORT_RequestAsyncCallback;
    RegPacket->UsbPortReadWriteConfigSpace = USBPORT_ReadWriteConfigSpace;
    RegPacket->UsbPortWait = USBPORT_Wait;
    RegPacket->UsbPortInvalidateController = USBPORT_InvalidateController;
    RegPacket->UsbPortBugCheck = USBPORT_BugCheck;
    RegPacket->UsbPortNotifyDoubleBuffer = USBPORT_NotifyDoubleBuffer;

#if DBG
    /*
     * For interface versions that support message interrupts and the full
     * common-buffer/async callback helpers (USB 2.0 and later), assert that
     * the registration packet is wired up as expected. This catches cases
     * where a miniport was built against a mismatched USBPORT header.
     */
    if (Version >= USB20_MINIPORT_INTERFACE_VERSION)
    {
        ASSERT(RegPacket->UsbPortDbgPrint != NULL);
        ASSERT(RegPacket->UsbPortGetMiniportRegistryKeyValue != NULL);
        ASSERT(RegPacket->UsbPortInvalidateRootHub != NULL);
        ASSERT(RegPacket->UsbPortInvalidateEndpoint != NULL);
        ASSERT(RegPacket->UsbPortCompleteTransfer != NULL);
        ASSERT(RegPacket->UsbPortCompleteIsoTransfer != NULL);
        ASSERT(RegPacket->UsbPortLogEntry != NULL);
        ASSERT(RegPacket->UsbPortGetMappedVirtualAddress != NULL);
        ASSERT(RegPacket->UsbPortRequestAsyncCallback != NULL);
        ASSERT(RegPacket->UsbPortReadWriteConfigSpace != NULL);
        ASSERT(RegPacket->UsbPortWait != NULL);
        ASSERT(RegPacket->UsbPortInvalidateController != NULL);
        ASSERT(RegPacket->UsbPortBugCheck != NULL);
        ASSERT(RegPacket->UsbPortNotifyDoubleBuffer != NULL);
    }
#endif

    RtlCopyMemory(&MiniPortInterface->Packet,
                  RegPacket,
                  sizeof(USBPORT_REGISTRATION_PACKET));

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(IN PDRIVER_OBJECT DriverObject,
            IN PUNICODE_STRING RegistryPath)
{
    return STATUS_SUCCESS;
}
#if DBG
#ifndef USBPORT_DBG_BOUNCE_TRACE
#define USBPORT_DBG_BOUNCE_TRACE 0
#endif
#if USBPORT_DBG_BOUNCE_TRACE
#define USBPORT_BOUNCE_TRACE DPRINT_CORE
#else
#define USBPORT_BOUNCE_TRACE(...) do { } while (0)
#endif
#else
#define USBPORT_BOUNCE_TRACE(...) do { } while (0)
#endif
