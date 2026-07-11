/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     wlansvc control device (\Device\Nwifi) and IOCTL dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

/* Handle returned by NdisRegisterDeviceEx (for teardown). */
static NDIS_HANDLE gNwifiControlDeviceHandle = NULL;

/* ===========================================================================
 *  Asynchronous notification channel
 *
 *  wlansvc keeps one IOCTL_NWIFI_GET_NOTIFICATION pending and re-issues it on
 *  completion; events with no IRP pending are queued in a small ring so none
 *  is lost between re-issues.
 * ===========================================================================
 */

/* Complete the IRP with one notification payload. */
static
VOID
NwifiCompleteNotifyIrp(
    _In_ PIRP Irp,
    _In_ PNWIFI_NOTIFICATION Notif)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG OutLen = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (OutLen >= sizeof(NWIFI_NOTIFICATION) && Irp->AssociatedIrp.SystemBuffer != NULL)
    {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Notif, sizeof(NWIFI_NOTIFICATION));
        Irp->IoStatus.Information = sizeof(NWIFI_NOTIFICATION);
        Irp->IoStatus.Status = STATUS_SUCCESS;
    }
    else
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
    }

    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

VOID
NwifiQueueNotification(
    _In_ ULONG InterfaceIndex,
    _In_ NWIFI_NOTIFICATION_CODE Code,
    _In_ NDIS_STATUS StatusCode)
{
    KIRQL OldIrql;
    PIRP Irp = NULL;
    NWIFI_NOTIFICATION Notif;

    RtlZeroMemory(&Notif, sizeof(Notif));
    Notif.Size = sizeof(NWIFI_NOTIFICATION);
    Notif.InterfaceIndex = InterfaceIndex;
    Notif.Code = Code;
    Notif.StatusCode = StatusCode;

    KeAcquireSpinLock(&gNwifi.NotifyLock, &OldIrql);

    {
        /*
         * IoSetCancelRoutine returning non-NULL means we won the race and own
         * this IRP; NULL means a concurrent cancel claimed it and will
         * remove+complete it, so leave it on the list and keep looking.
         */
        PLIST_ENTRY Entry = gNwifi.NotifyIrpQueue.Flink;
        while (Entry != &gNwifi.NotifyIrpQueue)
        {
            PIRP Candidate = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
            PLIST_ENTRY Next = Entry->Flink;

            if (IoSetCancelRoutine(Candidate, NULL) != NULL)
            {
                RemoveEntryList(Entry);
                Irp = Candidate;
                break;
            }
            Entry = Next;
        }
    }

    if (Irp == NULL)
    {
        /* No waiter: ring-buffer the event (drop the oldest on overflow). */
        gNwifi.NotifyRing[gNwifi.NotifyHead] = Notif;
        gNwifi.NotifyHead = (gNwifi.NotifyHead + 1) % NWIFI_NOTIFY_RING;
        if (gNwifi.NotifyCount < NWIFI_NOTIFY_RING)
        {
            gNwifi.NotifyCount++;
        }
        else
        {
            /* Overwrote the tail; advance it. */
            gNwifi.NotifyTail = (gNwifi.NotifyTail + 1) % NWIFI_NOTIFY_RING;
        }
    }

    KeReleaseSpinLock(&gNwifi.NotifyLock, OldIrql);

    if (Irp != NULL)
    {
        NwifiCompleteNotifyIrp(Irp, &Notif);
    }
}

/* Cancel routine for a pended notify IRP. */
static
VOID
NTAPI
NwifiCancelNotifyIrp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&gNwifi.NotifyLock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&gNwifi.NotifyLock, OldIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

/* Pend (or immediately satisfy from the ring) a GET_NOTIFICATION IRP. */
static
NTSTATUS
NwifiPendNotifyIrp(
    _In_ PIRP Irp)
{
    KIRQL OldIrql;
    NWIFI_NOTIFICATION Pending;
    BOOLEAN HavePending = FALSE;

    KeAcquireSpinLock(&gNwifi.NotifyLock, &OldIrql);

    if (gNwifi.NotifyCount != 0)
    {
        /* Deliver a buffered event immediately. */
        Pending = gNwifi.NotifyRing[gNwifi.NotifyTail];
        gNwifi.NotifyTail = (gNwifi.NotifyTail + 1) % NWIFI_NOTIFY_RING;
        gNwifi.NotifyCount--;
        HavePending = TRUE;
    }
    else
    {
        /* Queue the IRP and arm cancellation. */
        IoSetCancelRoutine(Irp, NwifiCancelNotifyIrp);
        if (Irp->Cancel)
        {
            /* Cancel arrived before we set the routine: complete cancelled if
             * we still own the routine. */
            if (IoSetCancelRoutine(Irp, NULL) != NULL)
            {
                KeReleaseSpinLock(&gNwifi.NotifyLock, OldIrql);
                Irp->IoStatus.Status = STATUS_CANCELLED;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_CANCELLED;
            }
        }
        IoMarkIrpPending(Irp);
        InsertTailList(&gNwifi.NotifyIrpQueue, &Irp->Tail.Overlay.ListEntry);
    }

    KeReleaseSpinLock(&gNwifi.NotifyLock, OldIrql);

    if (HavePending)
    {
        NwifiCompleteNotifyIrp(Irp, &Pending);
        return STATUS_SUCCESS;
    }
    return STATUS_PENDING;
}

/* ===========================================================================
 *  Interface enumeration
 * ===========================================================================
 */
static
NTSTATUS
NwifiIoctlEnumInterfaces(
    _Out_writes_bytes_(OutLen) PVOID Out,
    _In_ ULONG OutLen,
    _Out_ PULONG Information)
{
    PNWIFI_INTERFACE_LIST List = (PNWIFI_INTERFACE_LIST)Out;
    PLIST_ENTRY Entry;
    ULONG Count = 0;
    ULONG Needed;

    *Information = 0;

    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    for (Entry = gNwifi.AdapterList.Flink;
         Entry != &gNwifi.AdapterList;
         Entry = Entry->Flink)
    {
        Count++;
    }

    Needed = FIELD_OFFSET(NWIFI_INTERFACE_LIST, Item) +
             (Count == 0 ? 1 : Count) * sizeof(NWIFI_INTERFACE_REF);

    if (OutLen < Needed)
    {
        NdisReleaseSpinLock(&gNwifi.AdapterLock);
        *Information = Needed;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(List, FIELD_OFFSET(NWIFI_INTERFACE_LIST, Item));
    List->Size = Needed;
    List->NumberOfItems = Count;

    Count = 0;
    for (Entry = gNwifi.AdapterList.Flink;
         Entry != &gNwifi.AdapterList;
         Entry = Entry->Flink)
    {
        PNWIFI_ADAPTER Adapter = CONTAINING_RECORD(Entry, NWIFI_ADAPTER, Link);
        PNWIFI_INTERFACE_REF Ref = &List->Item[Count++];

        RtlZeroMemory(Ref, sizeof(*Ref));
        Ref->Size = sizeof(NWIFI_INTERFACE_REF);
        Ref->InterfaceIndex = Adapter->InterfaceIndex;
        Ref->UpperLuid = 0;     /* TODO: NET_LUID correlation */
        RtlCopyMemory(Ref->MacAddress, Adapter->MacAddress, IEEE80211_ADDR_LEN);
    }
    NdisReleaseSpinLock(&gNwifi.AdapterLock);

    *Information = Needed;
    return STATUS_SUCCESS;
}

/* Map an MSM/NDIS status to an NTSTATUS for the IOCTL return. */
static
NTSTATUS
NwifiMapNdisStatus(
    _In_ NDIS_STATUS Status)
{
    switch (Status)
    {
        case NDIS_STATUS_SUCCESS:           return STATUS_SUCCESS;
        case NDIS_STATUS_PENDING:           return STATUS_SUCCESS;
        case NDIS_STATUS_BUFFER_TOO_SHORT:  return STATUS_BUFFER_TOO_SMALL;
        case NDIS_STATUS_INVALID_LENGTH:    return STATUS_INVALID_BUFFER_SIZE;
        case NDIS_STATUS_INVALID_DATA:      return STATUS_INVALID_PARAMETER;
        case NDIS_STATUS_RESOURCES:         return STATUS_INSUFFICIENT_RESOURCES;
        case NDIS_STATUS_NOT_SUPPORTED:     return STATUS_NOT_SUPPORTED;
        default:                            return STATUS_UNSUCCESSFUL;
    }
}

/* ===========================================================================
 *  IRP_MJ_DEVICE_CONTROL dispatch
 * ===========================================================================
 */
static
NTSTATUS
NTAPI
NwifiDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG Ioctl = IrpSp->Parameters.DeviceIoControl.IoControlCode;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG InLen = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutLen = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    NDIS_STATUS NdisStatus;
    PNWIFI_ADAPTER Adapter;
    PNWIFI_MSM Msm;
    ULONG Info = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (Ioctl)
    {
        case IOCTL_NWIFI_ENUM_INTERFACES:
            Status = NwifiIoctlEnumInterfaces(Buffer, OutLen, &Info);
            break;

        case IOCTL_NWIFI_SCAN:
        {
            PNWIFI_SCAN_REQUEST Req = (PNWIFI_SCAN_REQUEST)Buffer;
            if (InLen < sizeof(NWIFI_SCAN_REQUEST))
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            Adapter = NwifiFindAdapterByIndex(Req->InterfaceIndex);
            if (Adapter == NULL || Adapter->MsmContext == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }
            Msm = (PNWIFI_MSM)Adapter->MsmContext;
            NdisStatus = NwifiMsmStartScan(Msm, Req->BssType, &Req->Ssid);
            Status = NwifiMapNdisStatus(NdisStatus);
            break;
        }

        case IOCTL_NWIFI_GET_BSS_LIST:
        {
            PNWIFI_BSS_LIST ReqHdr = (PNWIFI_BSS_LIST)Buffer;
            ULONG Index;
            if (InLen < sizeof(ULONG) && OutLen < sizeof(NWIFI_BSS_LIST))
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            /* The interface index is carried in the (in==out buffered) request
             * header's InterfaceIndex field. */
            Index = (InLen >= sizeof(NWIFI_BSS_LIST)) ? ReqHdr->InterfaceIndex
                  : (InLen >= sizeof(ULONG)) ? *(PULONG)Buffer : 0;
            Adapter = NwifiFindAdapterByIndex(Index);
            if (Adapter == NULL || Adapter->MsmContext == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }
            Msm = (PNWIFI_MSM)Adapter->MsmContext;
            NdisStatus = NwifiMsmGetBssList(Msm, Buffer, OutLen, &Info);
            Status = NwifiMapNdisStatus(NdisStatus);
            break;
        }

        case IOCTL_NWIFI_CONNECT:
        {
            PNWIFI_CONNECT_REQUEST Req = (PNWIFI_CONNECT_REQUEST)Buffer;
            NWIFI_CONNECT_PARAMS Params;
            if (InLen < sizeof(NWIFI_CONNECT_REQUEST))
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            Adapter = NwifiFindAdapterByIndex(Req->InterfaceIndex);
            if (Adapter == NULL || Adapter->MsmContext == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }
            Msm = (PNWIFI_MSM)Adapter->MsmContext;

            RtlZeroMemory(&Params, sizeof(Params));
            Params.Ssid = Req->Ssid;
            RtlCopyMemory(Params.DesiredBssid, Req->DesiredBssid, IEEE80211_ADDR_LEN);
            Params.HaveBssid = FALSE;
            {
                ULONG k;
                for (k = 0; k < IEEE80211_ADDR_LEN; k++)
                {
                    if (Req->DesiredBssid[k] != 0) { Params.HaveBssid = TRUE; break; }
                }
            }
            Params.BssType = Req->BssType;
            Params.AuthAlgorithm = Req->AuthAlgorithm;
            Params.UnicastCipher = Req->UnicastCipher;
            Params.MulticastCipher = Req->MulticastCipher;
            /* RSNA / WPA-PSK auth => run the supplicant. */
            Params.Secure = (Req->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK ||
                             Req->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA ||
                             Req->AuthAlgorithm == DOT11_AUTH_ALGO_WPA_PSK ||
                             Req->AuthAlgorithm == DOT11_AUTH_ALGO_WPA);

            NdisStatus = NwifiMsmConnect(Msm, &Params);
            Status = NwifiMapNdisStatus(NdisStatus);
            break;
        }

        case IOCTL_NWIFI_DISCONNECT:
        {
            PNWIFI_INTERFACE_REF Ref = (PNWIFI_INTERFACE_REF)Buffer;
            if (InLen < sizeof(NWIFI_INTERFACE_REF))
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            Adapter = NwifiFindAdapterByIndex(Ref->InterfaceIndex);
            if (Adapter == NULL || Adapter->MsmContext == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }
            Msm = (PNWIFI_MSM)Adapter->MsmContext;
            NdisStatus = NwifiMsmDisconnect(Msm);
            Status = NwifiMapNdisStatus(NdisStatus);
            break;
        }

        case IOCTL_NWIFI_QUERY_STATE:
        {
            PNWIFI_INTERFACE_REF Ref = (PNWIFI_INTERFACE_REF)Buffer;
            ULONG Index;
            if (OutLen < sizeof(NWIFI_LINK_STATE))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Info = sizeof(NWIFI_LINK_STATE);
                break;
            }
            Index = (InLen >= sizeof(NWIFI_INTERFACE_REF)) ? Ref->InterfaceIndex
                  : (InLen >= sizeof(ULONG)) ? *(PULONG)Buffer : 0;
            Adapter = NwifiFindAdapterByIndex(Index);
            if (Adapter == NULL || Adapter->MsmContext == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }
            Msm = (PNWIFI_MSM)Adapter->MsmContext;
            NwifiMsmQueryState(Msm, (PNWIFI_LINK_STATE)Buffer);
            Info = sizeof(NWIFI_LINK_STATE);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_NWIFI_SET_KEY:
        {
            PNWIFI_SET_KEY Key = (PNWIFI_SET_KEY)Buffer;
            if (InLen < sizeof(NWIFI_SET_KEY))
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            Adapter = NwifiFindAdapterByIndex(Key->InterfaceIndex);
            if (Adapter == NULL || Adapter->MsmContext == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }
            Msm = (PNWIFI_MSM)Adapter->MsmContext;
            NdisStatus = NwifiMsmSetKey(Msm, Key);
            Status = NwifiMapNdisStatus(NdisStatus);
            break;
        }

        case IOCTL_NWIFI_GET_NOTIFICATION:
            if (OutLen < sizeof(NWIFI_NOTIFICATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Info = sizeof(NWIFI_NOTIFICATION);
                break;
            }
            /* Pends (or completes immediately from the ring).  Either way the
             * IRP is handled here; do not fall through to the common
             * completion below. */
            return NwifiPendNotifyIrp(Irp);

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* IRP_MJ_CREATE / IRP_MJ_CLOSE. */
static
NTSTATUS
NTAPI
NwifiDispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);

    /* The control device drives the radio (connect/key install); restrict opens to
     * SYSTEM-level callers (wlansvc runs as LocalSystem) since NDIS creates it with no ACL. */
    if (IoStack->MajorFunction == IRP_MJ_CREATE && Irp->RequestorMode == UserMode)
    {
        LUID TcbPrivilege;
        TcbPrivilege.LowPart = SE_TCB_PRIVILEGE;
        TcbPrivilege.HighPart = 0;
        if (!SeSinglePrivilegeCheck(TcbPrivilege, UserMode))
        {
            DPRINT1("NWIFI: control-device open denied (caller lacks SeTcbPrivilege)\n");
            Status = STATUS_ACCESS_DENIED;
        }
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* Complete every pended notify IRP as STATUS_CANCELLED.  An IRP is only
 * claimed once its cancel-routine race is won (IoSetCancelRoutine non-NULL);
 * IRPs owned by a concurrent cancel are left for that routine to complete. */
static
VOID
NwifiDrainNotifyQueue(VOID)
{
    KIRQL OldIrql;
    LIST_ENTRY Cancelled;
    PLIST_ENTRY Entry;

    InitializeListHead(&Cancelled);

    KeAcquireSpinLock(&gNwifi.NotifyLock, &OldIrql);
    Entry = gNwifi.NotifyIrpQueue.Flink;
    while (Entry != &gNwifi.NotifyIrpQueue)
    {
        PIRP Pended = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        PLIST_ENTRY Next = Entry->Flink;

        if (IoSetCancelRoutine(Pended, NULL) != NULL)
        {
            RemoveEntryList(Entry);
            InsertTailList(&Cancelled, Entry);
        }
        Entry = Next;
    }
    KeReleaseSpinLock(&gNwifi.NotifyLock, OldIrql);

    while (!IsListEmpty(&Cancelled))
    {
        PIRP Pended;
        Entry = RemoveHeadList(&Cancelled);
        Pended = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        Pended->IoStatus.Status = STATUS_CANCELLED;
        Pended->IoStatus.Information = 0;
        IoCompleteRequest(Pended, IO_NO_INCREMENT);
    }
}

static
NTSTATUS
NTAPI
NwifiDispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    /* The handle is closing: fail every pended notify IRP we can claim. */
    NwifiDrainNotifyQueue();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ===========================================================================
 *  Control device create / delete (called from DriverEntry / Unload)
 * ===========================================================================
 */
NDIS_STATUS
NwifiCreateControlDevice(VOID)
{
    NDIS_DEVICE_OBJECT_ATTRIBUTES Attr;
    PDRIVER_DISPATCH Dispatch[IRP_MJ_MAXIMUM_FUNCTION + 1];
    NDIS_STRING DeviceName = RTL_CONSTANT_STRING(NWIFI_CONTROL_DEVICE_NAME);
    NDIS_STRING SymbolicName = RTL_CONSTANT_STRING(NWIFI_CONTROL_SYMBOLIC_NAME);
    PDEVICE_OBJECT DeviceObject = NULL;
    NDIS_STATUS Status;

    RtlZeroMemory(Dispatch, sizeof(Dispatch));
    Dispatch[IRP_MJ_CREATE]         = NwifiDispatchCreateClose;
    Dispatch[IRP_MJ_CLOSE]          = NwifiDispatchCreateClose;
    Dispatch[IRP_MJ_CLEANUP]        = NwifiDispatchCleanup;
    Dispatch[IRP_MJ_DEVICE_CONTROL] = NwifiDispatchDeviceControl;

    RtlZeroMemory(&Attr, sizeof(Attr));
    Attr.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Attr.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
    Attr.Header.Size = sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES);
    Attr.DeviceName = &DeviceName;
    Attr.SymbolicName = &SymbolicName;
    Attr.MajorFunctions = Dispatch;
    Attr.ExtensionSize = 0;
    Attr.DefaultSDDLString = NULL;
    Attr.DeviceClassGuid = NULL;

    Status = NdisRegisterDeviceEx(gNwifi.MiniportDriverHandle,
                                  &Attr,
                                  &DeviceObject,
                                  &gNwifiControlDeviceHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: NdisRegisterDeviceEx failed 0x%08X\n", Status);
        return Status;
    }

    gNwifi.ControlDeviceObject = DeviceObject;
    DPRINT1("NWIFI: control device \\Device\\Nwifi created\n");
    return NDIS_STATUS_SUCCESS;
}

VOID
NwifiDeleteControlDevice(VOID)
{
    /* Fail any still-pending notify IRPs before destroying the device. */
    NwifiDrainNotifyQueue();

    if (gNwifiControlDeviceHandle != NULL)
    {
        NdisDeregisterDeviceEx(gNwifiControlDeviceHandle);
        gNwifiControlDeviceHandle = NULL;
    }
    gNwifi.ControlDeviceObject = NULL;
}
