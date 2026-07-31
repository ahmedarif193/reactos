/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60adapter.c
 * PURPOSE:     NDIS 6 LOGICAL_ADAPTER lifecycle.
 *
 *              Allocates a LOGICAL_ADAPTER (the same struct the legacy
 *              5.x library uses) plus a NDIS6_ADAPTER_EXT for the
 *              NDIS 6 specific state. Inserts on the global
 *              AdapterListHead so MiniLocateDevice() finds it for
 *              NdisOpenAdapter callers.
 *
 *              Calls the driver's MiniportInitializeEx with a freshly
 *              built NDIS_MINIPORT_INIT_PARAMETERS, then waits for the
 *              halt callback path to tear everything down.
 *
 *              Created on the dev-nt6-1 branch by the NDIS 5↔6 bridge
 *              work that lets e1000e move real packets.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"
#include <ntstrsafe.h>

#define NDIS6_ADAPTER_TAG  'aDNn'  /* "nNDa" */

static VOID
Ndis6ResetMiniportAttributes(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    ASSERT(Adapter != NULL);
    Ext = NDIS6_EXT(Adapter);
    ASSERT(Ext != NULL);

    if (Ext->GeneralAttrs.SupportedOidList != NULL)
    {
        ExFreePoolWithTag(Ext->GeneralAttrs.SupportedOidList,
                          NDIS6_ATTR_TAG);
    }
    if (Ext->OffloadDefaultPtr != NULL)
        ExFreePoolWithTag(Ext->OffloadDefaultPtr, NDIS6_ATTR_TAG);
    if (Ext->OffloadHwPtr != NULL)
        ExFreePoolWithTag(Ext->OffloadHwPtr, NDIS6_ATTR_TAG);
    if (Ext->TcpOffloadDefaultPtr != NULL)
        ExFreePoolWithTag(Ext->TcpOffloadDefaultPtr, NDIS6_ATTR_TAG);
    if (Ext->TcpOffloadHwPtr != NULL)
        ExFreePoolWithTag(Ext->TcpOffloadHwPtr, NDIS6_ATTR_TAG);

    RtlZeroMemory(&Ext->RegistrationAttrs, sizeof(Ext->RegistrationAttrs));
    RtlZeroMemory(&Ext->GeneralAttrs, sizeof(Ext->GeneralAttrs));
    Ext->RegistrationAttrsValid = FALSE;
    Ext->GeneralAttrsValid = FALSE;
    Ext->OffloadDefaultPtr = NULL;
    Ext->OffloadHwPtr = NULL;
    Ext->TcpOffloadDefaultPtr = NULL;
    Ext->TcpOffloadHwPtr = NULL;
    Ext->OffloadValid = FALSE;
    Ext->MiniportAdapterContext = NULL;

    RtlZeroMemory(&Adapter->Address, sizeof(Adapter->Address));
    Adapter->AddressLength = 0;
}

/* ============================================================================
 *  Ndis6ReadExportName — read "\Device\{NetCfgInstanceId}" from the
 *  device's Class\<GUID>\<Instance>\Linkage key in the registry.
 *
 *  Ported from the legacy NDIS 5 NdisIAddDevice (miniport.c:2500-2588).
 *  Having the same adapter name as the legacy path is mandatory because
 *  protocol drivers like tcpip.sys call NdisOpenAdapter with the GUID-
 *  based name they read from their own Linkage\Bind registry list, and
 *  MiniLocateDevice does an exact string compare.
 * ============================================================================ */

static NTSTATUS
Ndis6ReadExportName(
    _In_  PDEVICE_OBJECT   Pdo,
    _Out_ PUNICODE_STRING  ExportName)
{
    static const WCHAR ClassKeyName[]   = L"Class\\";
    static const WCHAR LinkageKeyName[] = L"\\Linkage";
    WCHAR* LinkageKeyBuffer;
    ULONG  DriverKeyLength = 0;
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];
    NTSTATUS Status;

    RtlInitUnicodeString(ExportName, NULL);

    Status = IoGetDeviceProperty(Pdo, DevicePropertyDriverKeyName,
                                 0, NULL, &DriverKeyLength);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW &&
        Status != STATUS_SUCCESS)
    {
        return Status;
    }

    LinkageKeyBuffer = (WCHAR*)ExAllocatePoolWithTag(
        PagedPool,
        DriverKeyLength + sizeof(ClassKeyName) + sizeof(LinkageKeyName),
        NDIS6_ADAPTER_TAG);
    if (LinkageKeyBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoGetDeviceProperty(
        Pdo, DevicePropertyDriverKeyName, DriverKeyLength,
        LinkageKeyBuffer + ((sizeof(ClassKeyName) - sizeof(WCHAR)) / sizeof(WCHAR)),
        &DriverKeyLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(LinkageKeyBuffer, NDIS6_ADAPTER_TAG);
        return Status;
    }

    /* Prefix "Class\" */
    RtlCopyMemory(LinkageKeyBuffer, ClassKeyName,
                  sizeof(ClassKeyName) - sizeof(WCHAR));
    /* Append "\Linkage" after the driver key */
    RtlCopyMemory(
        LinkageKeyBuffer + ((sizeof(ClassKeyName) - sizeof(WCHAR) +
                             DriverKeyLength - sizeof(WCHAR)) / sizeof(WCHAR)),
        LinkageKeyName, sizeof(LinkageKeyName));

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags        = RTL_QUERY_REGISTRY_REQUIRED | RTL_QUERY_REGISTRY_DIRECT;
    QueryTable[0].Name         = L"Export";
    QueryTable[0].EntryContext = ExportName;

    Status = RtlQueryRegistryValues(RTL_REGISTRY_CONTROL, LinkageKeyBuffer,
                                    QueryTable, NULL, NULL);
    ExFreePoolWithTag(LinkageKeyBuffer, NDIS6_ADAPTER_TAG);
    return Status;
}

static VOID
Ndis6FreeAdapterName(_Inout_ PUNICODE_STRING Name)
{
    if (Name->Buffer)
    {
        RtlFreeUnicodeString(Name);
        Name->Buffer = NULL;
    }
}

static HANDLE
Ndis6ImOpenKey(_In_ PCWSTR Path)
{
    UNICODE_STRING KeyPath;
    OBJECT_ATTRIBUTES Oa;
    HANDLE Key = NULL;
    ULONG Disp;

    RtlInitUnicodeString(&KeyPath, Path);
    InitializeObjectAttributes(&Oa, &KeyPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateKey(&Key, KEY_ALL_ACCESS, &Oa, 0, NULL,
                                REG_OPTION_NON_VOLATILE, &Disp)))
        return NULL;
    return Key;
}

static VOID
Ndis6ImSetSz(_In_ HANDLE Key, _In_ PCWSTR Name, _In_ ULONG Type, _In_ PCWSTR Value)
{
    UNICODE_STRING ValName;
    SIZE_T Chars = wcslen(Value) + 1;
    RtlInitUnicodeString(&ValName, Name);
    if (Type == REG_MULTI_SZ)
        Chars += 1;
    ZwSetValueKey(Key, &ValName, 0, Type, (PVOID)Value,
                  (ULONG)(Chars * sizeof(WCHAR)));
}

static VOID
Ndis6ImAppendBind(_In_ PCWSTR Device)
{
    HANDLE Key;
    UNICODE_STRING ValName;
    NTSTATUS Status;
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    ULONG Len = 0, NewLen, DevBytes = (ULONG)((wcslen(Device) + 1) * sizeof(WCHAR));
    PWCHAR Buf, p;

    Key = Ndis6ImOpenKey(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Linkage");
    if (Key == NULL)
        return;

    RtlInitUnicodeString(&ValName, L"Bind");
    ZwQueryValueKey(Key, &ValName, KeyValuePartialInformation, NULL, 0, &Len);

    NewLen = (Len ? Len : FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data)) + DevBytes + sizeof(WCHAR);
    Info = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, NewLen, NDIS6_ADAPTER_TAG);
    if (Info == NULL) { ZwClose(Key); return; }

    Buf = (PWCHAR)Info->Data;
    if (Len)
    {
        Status = ZwQueryValueKey(Key, &ValName, KeyValuePartialInformation, Info, Len, &Len);
        if (!NT_SUCCESS(Status)) { ExFreePoolWithTag(Info, NDIS6_ADAPTER_TAG); ZwClose(Key); return; }
        for (p = Buf; *p; p += wcslen(p) + 1)
        {
            if (_wcsicmp(p, Device) == 0)
            {
                ExFreePoolWithTag(Info, NDIS6_ADAPTER_TAG);
                ZwClose(Key);
                return;
            }
        }
        RtlCopyMemory(p, Device, DevBytes);
        p[wcslen(Device) + 1] = L'\0';
        NewLen = (ULONG)(((PUCHAR)(p + wcslen(Device) + 2)) - (PUCHAR)Buf);
    }
    else
    {
        RtlCopyMemory(Buf, Device, DevBytes);
        Buf[wcslen(Device) + 1] = L'\0';
        NewLen = DevBytes + sizeof(WCHAR);
    }

    ZwSetValueKey(Key, &ValName, 0, REG_MULTI_SZ, Buf, NewLen);
    ExFreePoolWithTag(Info, NDIS6_ADAPTER_TAG);
    ZwClose(Key);
}

VOID
Ndis6ImSeedTcpipRegistry(_In_ PCUNICODE_STRING DeviceName)
{
    static const WCHAR ClassBase[] =
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\";
    static const WCHAR IfBase[] =
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\";
    static LONG InstanceSeq = 0x4000;
    WCHAR Guid[64];
    WCHAR Path[300];
    HANDLE Key;
    USHORT i, gstart = 0;
    ULONG One = 1, glen;

    for (i = (USHORT)(DeviceName->Length / sizeof(WCHAR)); i > 0; i--)
    {
        if (DeviceName->Buffer[i - 1] == L'\\') { gstart = i; break; }
    }
    glen = (DeviceName->Length / sizeof(WCHAR)) - gstart;
    if (glen == 0 || glen >= RTL_NUMBER_OF(Guid))
        return;
    RtlCopyMemory(Guid, &DeviceName->Buffer[gstart], glen * sizeof(WCHAR));
    Guid[glen] = L'\0';

    RtlStringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%ls%04X", ClassBase,
                        InterlockedIncrement(&InstanceSeq));
    Key = Ndis6ImOpenKey(Path);
    if (Key != NULL)
    {
        Ndis6ImSetSz(Key, L"DriverDesc", REG_SZ, L"ReactOS NDIS Virtual Miniport");
        Ndis6ImSetSz(Key, L"NetCfgInstanceId", REG_SZ, Guid);
        ZwClose(Key);
    }
    RtlStringCchCatW(Path, RTL_NUMBER_OF(Path), L"\\Linkage");
    Key = Ndis6ImOpenKey(Path);
    if (Key != NULL)
    {
        Ndis6ImSetSz(Key, L"RootDevice", REG_SZ, Guid);
        Ndis6ImSetSz(Key, L"Export", REG_SZ, DeviceName->Buffer);
        Ndis6ImSetSz(Key, L"UpperBind", REG_SZ, L"Tcpip");
        ZwClose(Key);
    }

    RtlStringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%ls%ls", IfBase, Guid);
    Key = Ndis6ImOpenKey(Path);
    if (Key != NULL)
    {
        UNICODE_STRING ValName;
        RtlInitUnicodeString(&ValName, L"EnableDHCP");
        ZwSetValueKey(Key, &ValName, 0, REG_DWORD, &One, sizeof(One));
        Ndis6ImSetSz(Key, L"IPAddress", REG_MULTI_SZ, L"0.0.0.0");
        Ndis6ImSetSz(Key, L"SubnetMask", REG_MULTI_SZ, L"0.0.0.0");
        Ndis6ImSetSz(Key, L"DefaultGateway", REG_MULTI_SZ, L"");
        ZwClose(Key);
    }

    Ndis6ImAppendBind(DeviceName->Buffer);
    DbgPrint("NDIS6: seeded TCPIP registry for %wZ\n", DeviceName);
}

/* ============================================================================
 *  Ndis6CreateLogicalAdapter
 *
 *  Called from Ndis6AddDevice (60driver.c) when PnP enumerates a device
 *  the driver claims. Builds the LOGICAL_ADAPTER + extension and inserts
 *  on AdapterListHead. Does NOT call the driver's InitializeHandlerEx —
 *  that happens at IRP_MN_START_DEVICE time after PnP has assigned
 *  resources.
 * ============================================================================ */

static NDIS_STATUS
Ndis6InitializeLogicalAdapter(
    _In_ PNDIS6_DRIVER_BLOCK DriverBlock,
    _In_ PDEVICE_OBJECT Pdo,
    _In_ PDEVICE_OBJECT Fdo,
    _In_opt_ PDEVICE_OBJECT NextDeviceObject,
    _In_ PUNICODE_STRING AdapterName,
    _In_ BOOLEAN IsWdfManaged,
    _In_opt_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT  Ext;
    KIRQL               OldIrql;
    extern LIST_ENTRY   AdapterListHead;
    extern KSPIN_LOCK   AdapterListLock;

    if (DriverBlock == NULL || Pdo == NULL || Fdo == NULL ||
        AdapterName == NULL || Adapter == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->IsNdis6 = TRUE;

    /* Allocate the NDIS 6 extension. */
    Ext = (PNDIS6_ADAPTER_EXT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_ADAPTER_EXT), NDIS6_ADAPTER_TAG);
    if (Ext == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Ext, sizeof(*Ext));
    Ext->Adapter                = Adapter;
    Ext->DriverBlock            = DriverBlock;
    Ext->IsWdfManaged           = IsWdfManaged;
    Ext->PhysicalDeviceObject   = Pdo;
    Ext->FunctionalDeviceObject = Fdo;
    Ext->MiniportAdapterContext = MiniportAdapterContext;
    KeInitializeSpinLock(&Ext->WdfReferenceLock);
    KeInitializeEvent(&Ext->WdfReferenceDrainEvent, NotificationEvent, TRUE);
    KeInitializeSpinLock(&Ext->IsrLock);
    Ext->InterruptRundownState = NDIS6_INTERRUPT_RUNDOWN_STOPPING;
    KeInitializeEvent(&Ext->InterruptDrainEvent, NotificationEvent, TRUE);

    /* Phase 3 TX thunk: in-flight wrapper NBL list. */
    KeInitializeSpinLock(&Ext->TxLookupLock);
    InitializeListHead(&Ext->InFlightNblsTx);
    /* A1: drain event — notification, manually reset when a send lands on
     * the in-flight list, set when the count decrements. HaltEx waits
     * for count == 0 with a timeout. */
    Ext->TxInFlightCount = 0;
    KeInitializeEvent(&Ext->TxDrainEvent, NotificationEvent, TRUE);
    Ext->TxAccepting = FALSE;

    /* A4: Pause/Restart state machine — starts paused after InitializeEx
     * and transitions to running when a protocol opens the adapter. */
    Ext->PauseState    = NDIS6_PAUSE_STATE_PAUSED;
    Ext->PauseStatus   = NDIS_STATUS_SUCCESS;
    Ext->RestartStatus = NDIS_STATUS_SUCCESS;
    KeInitializeEvent(&Ext->PauseEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Ext->RestartEvent, SynchronizationEvent, FALSE);

    /* Phase 3 OID thunk: legacy-NDIS5-request waiter list. */
    KeInitializeSpinLock(&Ext->OidWaiterLock);
    InitializeListHead(&Ext->OidWaiters);

    /* Phase 7B: per-adapter NDIS 6 filter module list. */
    KeInitializeSpinLock(&Ext->FilterModuleListLock);
    InitializeListHead(&Ext->FilterModuleList);

    /* Per-adapter native NDIS 6 protocol binding list. Populated by
     * NdisOpenAdapterEx, walked by the native send/receive datapath. */
    KeInitializeSpinLock(&Ext->ProtocolBindingListLock);
    InitializeListHead(&Ext->ProtocolBindingList);

    /* Phase 3 TX wrapper NBL pool — allocate using the bridge's own
     * NdisAllocateNetBufferListPool. fAllocateNetBuffer=TRUE so each NBL
     * comes with an embedded NB; we wrap the caller's MDL chain into the
     * embedded NB instead of allocating a backing data buffer. */
    {
        NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
        NdisZeroMemory(&PoolParams, sizeof(PoolParams));
        PoolParams.Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
        PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        PoolParams.Header.Size     = sizeof(NET_BUFFER_LIST_POOL_PARAMETERS);
        PoolParams.ProtocolId      = NDIS_PROTOCOL_ID_DEFAULT;
        PoolParams.fAllocateNetBuffer = TRUE;
        PoolParams.ContextSize     = 0;
        PoolParams.PoolTag         = 'TxNb';
        PoolParams.DataSize        = 0;
        Ext->TxWrapperNblPool = NdisAllocateNetBufferListPool(NULL, &PoolParams);
    }

    /* Phase 3 RX legacy packet/buffer pools — used to wrap incoming NBs
     * as legacy NDIS_PACKETs for tcpip's ProtocolReceivePacketHandler.
     * Sized to e1000e's typical RX batch (64 NBs per DPC budget). */
    {
        NDIS_STATUS PoolStatus;
        NdisAllocateBufferPool(&PoolStatus, &Ext->RxLegacyBufferPool, 64);
        DbgPrint("NDIS6-INIT: NdisAllocateBufferPool -> 0x%08lx, handle=%p\n",
                 (ULONG)PoolStatus, Ext->RxLegacyBufferPool);
        if (PoolStatus != NDIS_STATUS_SUCCESS)
            Ext->RxLegacyBufferPool = NULL;

        NdisAllocatePacketPool(&PoolStatus, &Ext->RxLegacyPacketPool, 64,
                               sizeof(PVOID) * 4);
        DbgPrint("NDIS6-INIT: NdisAllocatePacketPool -> 0x%08lx, handle=%p\n",
                 (ULONG)PoolStatus, Ext->RxLegacyPacketPool);
        if (PoolStatus != NDIS_STATUS_SUCCESS)
            Ext->RxLegacyPacketPool = NULL;
    }

    Adapter->Ndis6Context = Ext;

    /* Initialize the protocol list and the lock that legacy receive code
     * walks. The mininport_block lock is used by MiniIndicateData(). */
    InitializeListHead(&Adapter->ProtocolListHead);
    KeInitializeSpinLock(&Adapter->NdisMiniportBlock.Lock);

    /* Stash the synthesized name in the miniport block so legacy code
     * (MiniLocateDevice) can match it. */
    Adapter->NdisMiniportBlock.MiniportName = *AdapterName;

    /* Populate the device-object pointers in the legacy NdisMiniportBlock
     * so the existing NdisMGetDeviceProperty implementation in
     * miniport.c:3186 returns the right values for NDIS 6 adapters. */
    Adapter->NdisMiniportBlock.PhysicalDeviceObject = Pdo;
    Adapter->NdisMiniportBlock.DeviceObject         = Fdo;
    /* BusType / BusNumber: sniff the PDO's owning driver name to pick a
     * sensible default. PCI miniports (e1000e, virtio-net 6.x, etc.) sit
     * on \Driver\PCI; USB miniports (usbrndis) sit on \Driver\USBHUB.
     * Anything else falls through to PCIBus which is the safest default
     * for the legacy NdisMMapIoSpace fallback path. */
    Adapter->NdisMiniportBlock.BusType    = NdisInterfacePci;
    Adapter->NdisMiniportBlock.BusNumber  = 0;
    if (Pdo != NULL && Pdo->DriverObject != NULL)
    {
        PUNICODE_STRING DrvName = &Pdo->DriverObject->DriverName;
        if (DrvName->Buffer != NULL && DrvName->Length >= sizeof(L"\\Driver\\USB"))
        {
            /* Case-insensitive prefix match for USBHUB / USBPORT / USB. */
            if ((DrvName->Buffer[8]  == L'U' || DrvName->Buffer[8]  == L'u') &&
                (DrvName->Buffer[9]  == L'S' || DrvName->Buffer[9]  == L's') &&
                (DrvName->Buffer[10] == L'B' || DrvName->Buffer[10] == L'b'))
            {
                Adapter->NdisMiniportBlock.BusType = NdisInterfaceUSB;
            }
        }
    }

    if (IsWdfManaged)
    {
        /* WDF already owns and attached this FDO. This is a borrowed lower
         * stack pointer used only by NdisMGetDeviceProperty. */
        Adapter->NdisMiniportBlock.NextDeviceObject = NextDeviceObject;
    }
    else
    {
        /* Attach to the device stack so we receive subsequent PnP IRPs.
         * Use IoAttachDeviceToDeviceStack which is the canonical pattern. */
        Adapter->NdisMiniportBlock.NextDeviceObject =
            IoAttachDeviceToDeviceStack(Fdo, Pdo);
        if (Adapter->NdisMiniportBlock.NextDeviceObject == NULL)
        {
            if (Ext->TxWrapperNblPool)
                NdisFreeNetBufferListPool(Ext->TxWrapperNblPool);
            if (Ext->RxLegacyPacketPool)
                NdisFreePacketPool(Ext->RxLegacyPacketPool);
            if (Ext->RxLegacyBufferPool)
                NdisFreeBufferPool(Ext->RxLegacyBufferPool);
            ExFreePoolWithTag(Ext, NDIS6_ADAPTER_TAG);
            Adapter->Ndis6Context = NULL;
            Adapter->IsNdis6 = FALSE;
            return NDIS_STATUS_RESOURCES;
        }

        Fdo->Flags |= DO_DIRECT_IO | DO_POWER_PAGABLE;
        Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    }

    /* Insert into the global adapter list so legacy NdisOpenAdapter
     * (which calls MiniLocateDevice) can find this adapter by name. */
    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    InsertTailList(&AdapterListHead, &Adapter->ListEntry);
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
Ndis6CreateLogicalAdapter(
    _In_  PNDIS6_DRIVER_BLOCK   DriverBlock,
    _In_  PDEVICE_OBJECT        Pdo,
    _Out_ PLOGICAL_ADAPTER*     AdapterOut)
{
    PLOGICAL_ADAPTER Adapter;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;
    UNICODE_STRING AdapterName;

    if (DriverBlock == NULL || Pdo == NULL || AdapterOut == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *AdapterOut = NULL;

    /* Read \Device\{NetCfgInstanceId} from the registry so protocol
     * drivers (tcpip.sys) can find us by the same GUID name they
     * stored in their own Linkage\Bind list. */
    Status = Ndis6ReadExportName(Pdo, &AdapterName);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Create an FDO whose DeviceExtension is our LOGICAL_ADAPTER.
     * The device object IS named — it's \Device\{GUID} — so that
     * ObReferenceObjectByName in the legacy NdisOpenAdapter path
     * can open it in addition to MiniLocateDevice finding it. */
    Status = IoCreateDevice(
        DriverBlock->DriverObject,
        sizeof(LOGICAL_ADAPTER),
        &AdapterName,
        FILE_DEVICE_PHYSICAL_NETCARD,
        0,
        FALSE,
        &Fdo);
    if (!NT_SUCCESS(Status))
    {
        Ndis6FreeAdapterName(&AdapterName);
        return Status;
    }

    Adapter = (PLOGICAL_ADAPTER)Fdo->DeviceExtension;
    Status = Ndis6InitializeLogicalAdapter(DriverBlock,
                                           Pdo,
                                           Fdo,
                                           NULL,
                                           &AdapterName,
                                           FALSE,
                                           NULL,
                                           Adapter);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(Fdo);
        Ndis6FreeAdapterName(&AdapterName);
        return Status;
    }

    *AdapterOut = Adapter;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
Ndis6CreateWdfLogicalAdapter(
    _In_ PNDIS6_DRIVER_BLOCK DriverBlock,
    _In_ PNDIS6_WDF_ADD_DEVICE_INFO AddDeviceInfo,
    _Out_ PLOGICAL_ADAPTER* AdapterOut)
{
    PNDIS6_WDF_CX_DRIVER CxDriver;
    PLOGICAL_ADAPTER Adapter = NULL;
    PDEVICE_OBJECT Fdo;
    PDEVICE_OBJECT NextDeviceObject;
    UNICODE_STRING AdapterName;
    WCHAR AssignedNameBuffer[260];
    NTSTATUS Status;

    if (DriverBlock == NULL || AddDeviceInfo == NULL || AdapterOut == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *AdapterOut = NULL;
    CxDriver = DriverBlock->WdfCxDriver;
    if (!DriverBlock->IsWdfManaged || CxDriver == NULL ||
        CxDriver->Signature != NDIS6_WDF_CX_SIGNATURE ||
        CxDriver->Characteristics.EvtCxAllocateMiniportBlock == NULL ||
        CxDriver->Characteristics.EvtCxGetDeviceObject == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Status = CxDriver->Characteristics.EvtCxAllocateMiniportBlock(
        AddDeviceInfo->MiniportAdapterContext,
        sizeof(LOGICAL_ADAPTER),
        (PVOID*)&Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Fdo = CxDriver->Characteristics.EvtCxGetDeviceObject(
        AddDeviceInfo->MiniportAdapterContext);
    NextDeviceObject = CxDriver->Characteristics.EvtCxGetNextDeviceObject
        ? CxDriver->Characteristics.EvtCxGetNextDeviceObject(
              AddDeviceInfo->MiniportAdapterContext)
        : NULL;

    /* Prefer the NDIS installation's \Device\{NetCfgInstanceId} name. If
     * ReactOS setup did not create Linkage yet, use NetAdapterCx's WDF FDO
     * name and make an RTL-owned copy with the same lifetime semantics. */
    Status = Ndis6ReadExportName(AddDeviceInfo->PhysicalDeviceObject,
                                 &AdapterName);
    if (!NT_SUCCESS(Status) &&
        CxDriver->Characteristics.EvtCxGetAssignedFdoName != NULL)
    {
        UNICODE_STRING AssignedName;

        AssignedName.Buffer = AssignedNameBuffer;
        AssignedName.Length = 0;
        AssignedName.MaximumLength = sizeof(AssignedNameBuffer);
        Status = CxDriver->Characteristics.EvtCxGetAssignedFdoName(
            AddDeviceInfo->MiniportAdapterContext,
            &AssignedName);
        if (NT_SUCCESS(Status))
        {
            if (AssignedName.Length + sizeof(WCHAR) >
                AssignedName.MaximumLength)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                AssignedName.Buffer[AssignedName.Length / sizeof(WCHAR)] = L'\0';
                Status = RtlCreateUnicodeString(&AdapterName,
                                                AssignedName.Buffer)
                    ? STATUS_SUCCESS
                    : STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }

    if (!NT_SUCCESS(Status))
        return Status;

    Status = Ndis6InitializeLogicalAdapter(
        DriverBlock,
        AddDeviceInfo->PhysicalDeviceObject,
        Fdo,
        NextDeviceObject,
        &AdapterName,
        TRUE,
        AddDeviceInfo->MiniportAdapterContext,
        Adapter);
    if (!NT_SUCCESS(Status))
    {
        Ndis6FreeAdapterName(&AdapterName);
        return Status;
    }

    *AdapterOut = Adapter;
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  Ndis6CallMiniportInitializeEx
 *
 *  Builds an NDIS_MINIPORT_INIT_PARAMETERS from the saved driver
 *  characteristics + the resources extracted in IRP_MN_START_DEVICE,
 *  and calls the driver's InitializeHandlerEx. The driver runs to
 *  completion and (if everything works) calls NdisMSetMiniportAttributes
 *  several times before returning, which populates Ext->RegistrationAttrs
 *  and Ext->GeneralAttrs.
 * ============================================================================ */

NDIS_STATUS
Ndis6CallMiniportInitializeEx(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    NDIS_MINIPORT_INIT_PARAMETERS  Params;
    PNDIS6_ADAPTER_EXT             Ext;
    NDIS_STATUS                    Status;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Ext->DriverBlock->Characteristics.InitializeHandlerEx == NULL)
        return NDIS_STATUS_BAD_CHARACTERISTICS;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = NDIS_MINIPORT_INIT_PARAMETERS_REVISION_1;
    Params.Header.Size     = sizeof(Params);

    Params.Flags                     = 0;
    /* AllocatedResources is a PNDIS_RESOURCE_LIST which is really
     * PCM_PARTIAL_RESOURCE_LIST — the inner partial list, NOT the outer
     * CM_RESOURCE_LIST. Skip one level of indirection from what PnP
     * handed us. e1000e walks ResourceList->PartialDescriptors[] and
     * expects the real CmResourceTypeMemory/Port/Interrupt descriptors. */
    if (Ext->AllocatedResourcesTranslated &&
        Ext->AllocatedResourcesTranslated->Count > 0)
    {
        Params.AllocatedResources = (PNDIS_RESOURCE_LIST)
            &Ext->AllocatedResourcesTranslated->List[0].PartialResourceList;
    }
    Params.IMDeviceInstanceContext   = NULL;
    Params.MiniportAddDeviceContext  = NULL;
    Params.IfIndex                   = 0;
    Params.NetLuid.Value             = 0;
    Params.DefaultPortAuthStates     = NULL;
    Params.PciDeviceCustomProperties = NULL;

    /* Call the driver. The handle we hand it is the LOGICAL_ADAPTER
     * pointer, which the driver round-trips back to us via every
     * subsequent NdisM* call. */
    DbgPrint("NDIS6: InitializeEx → driver (adapter=%p pdo=%p fdo=%p next=%p)\n", Adapter, Adapter->NdisMiniportBlock.PhysicalDeviceObject, Adapter->NdisMiniportBlock.DeviceObject, Adapter->NdisMiniportBlock.NextDeviceObject);
    Status = Ext->DriverBlock->Characteristics.InitializeHandlerEx(
        (NDIS_HANDLE)Adapter,
        Ext->DriverBlock->MiniportDriverContext,
        &Params);
    DbgPrint("NDIS6: InitializeEx returned 0x%08lx (ctx=%p)\n", (ULONG)Status, Ext->MiniportAdapterContext);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        /* A failed InitializeEx owns its driver-side unwind and must not be
         * followed by HaltEx. Discard attributes published before failure so
         * a later START begins with an empty NDIS instance. */
        Ndis6ResetMiniportAttributes(Adapter);
    }

    return Status;
}

/* ============================================================================
 *  Ndis6CallMiniportHaltEx — symmetric tear-down
 * ============================================================================ */

VOID
Ndis6CallMiniportHaltEx(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PNDIS6_ADAPTER_EXT Ext;
    LARGE_INTEGER      Timeout;
    NTSTATUS           WaitStatus;
    LONG               StartingCount;
    LONG               RemainingCount;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
        return;

    /* A4: Pause the driver first so it stops accepting new sends. This
     * gives the TX DPC a chance to drain what's already in flight and
     * prevents new NBLs from landing on InFlightNblsTx while we wait. */
    (VOID)Ndis6CallMiniportPauseEx(Adapter);

    /* A1: drain in-flight TX wrapper NBLs before the driver tears down.
     * NdisMSendNetBufferListsComplete (60thunk_tx.c) decrements
     * TxInFlightCount and signals TxDrainEvent when it hits zero. We
     * wait up to 5 seconds for the count to drain, then push through
     * regardless — an NBL leak is preferable to hanging REMOVE_DEVICE.
     *
     * The common case is count == 0 because the stack has already
     * stopped sending by the time PnP sends REMOVE; we just sample
     * the event and return immediately. The wait only blocks when a
     * send is genuinely in flight on the miniport's TX DPC. */
    StartingCount = Ext->TxInFlightCount;
    if (StartingCount > 0)
    {
        DbgPrint("NDIS6: HaltEx draining %ld in-flight TX NBLs\n", StartingCount);

        /* Clear the event so we wait for the NEXT decrement. We own
         * the event setter side (TerminalSendComplete), so clearing
         * here can't race the setter in a problematic way — if the
         * count is about to hit zero we'll see it in the re-sample
         * after the wait. */
        KeClearEvent(&Ext->TxDrainEvent);

        /* If the count was already zero when the setter side raced
         * our clear, the sampling after the clear catches it. */
        if (Ext->TxInFlightCount == 0)
        {
            KeSetEvent(&Ext->TxDrainEvent, IO_NO_INCREMENT, FALSE);
        }
        else
        {
            /* 5-second absolute timeout (negative = relative 100-ns units). */
            Timeout.QuadPart = -50000000LL;
            WaitStatus = KeWaitForSingleObject(&Ext->TxDrainEvent,
                                               Executive, KernelMode,
                                               FALSE, &Timeout);
            RemainingCount = Ext->TxInFlightCount;
            if (WaitStatus == STATUS_TIMEOUT && RemainingCount > 0)
            {
                DbgPrint("NDIS6: HaltEx drain TIMEOUT, %ld NBLs still in flight — leaking\n",
                         RemainingCount);
                /* Fall through. The send-completion path will still
                 * try to call MiniSendComplete on an adapter whose
                 * extension may be gone. Ndis6FilterTerminalSendComplete
                 * checks for NULL Ext and drops the call, so this just
                 * leaks the wrapper NBL and the legacy protocol's
                 * SendCompleteHandler never fires for those packets. */
            }
        }
    }

    if (Ext->DriverBlock->Characteristics.HaltHandlerEx != NULL &&
        Ext->MiniportAdapterContext != NULL)
    {
        Ext->DriverBlock->Characteristics.HaltHandlerEx(
            Ext->MiniportAdapterContext, HaltAction);
    }

    /* Registration, general, and offload attributes belong to one
     * InitializeEx/HaltEx cycle. AddDevice state and interface identity stay
     * with the devnode so STOP followed by START can initialize it again. */
    Ndis6ResetMiniportAttributes(Adapter);
}

/* ============================================================================
 *  A4: Pause/Restart state machine
 *
 *  NDIS 6 miniports transition through running → pausing → paused →
 *  restarting → running. Pause stops the driver's send/receive; Restart
 *  wakes them back up. We call Pause before filter attach/detach and
 *  before Halt; Restart after attach and after init. Drivers may return
 *  PENDING from PauseHandler/RestartHandler and call NdisMPauseComplete /
 *  NdisMRestartComplete when the transition is done; we wait on the
 *  respective event in that case.
 * ============================================================================ */

static VOID
Ndis6SetTransmitAccepting(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ BOOLEAN Accepting)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Ext->TxLookupLock, &OldIrql);
    Ext->TxAccepting = Accepting;
    KeReleaseSpinLock(&Ext->TxLookupLock, OldIrql);
}

NDIS_STATUS
Ndis6CallMiniportPauseEx(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT           Ext;
    NDIS_MINIPORT_PAUSE_PARAMETERS PauseParams;
    NDIS_STATUS                  Status;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.PauseHandler == NULL ||
        Ext->MiniportAdapterContext == NULL)
    {
        /* Driver has no pause handler — treat as already paused. Many
         * simple miniports don't need pause semantics and fall through. */
        Ndis6SetTransmitAccepting(Ext, FALSE);
        Ext->PauseState = NDIS6_PAUSE_STATE_PAUSED;
        return NDIS_STATUS_SUCCESS;
    }

    if (Ext->PauseState == NDIS6_PAUSE_STATE_PAUSED ||
        Ext->PauseState == NDIS6_PAUSE_STATE_PAUSING)
    {
        return NDIS_STATUS_SUCCESS;
    }

    RtlZeroMemory(&PauseParams, sizeof(PauseParams));
    PauseParams.Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
    PauseParams.Header.Revision = NDIS_MINIPORT_PAUSE_PARAMETERS_REVISION_1;
    PauseParams.Header.Size     = sizeof(PauseParams);
    PauseParams.Flags           = 0;
    PauseParams.PauseReason     = 0;

    Ndis6SetTransmitAccepting(Ext, FALSE);
    Ext->PauseState  = NDIS6_PAUSE_STATE_PAUSING;
    Ext->PauseStatus = NDIS_STATUS_PENDING;
    KeClearEvent(&Ext->PauseEvent);

    DbgPrint("NDIS6: Pause → driver\n");
    Status = Ext->DriverBlock->Characteristics.PauseHandler(
        Ext->MiniportAdapterContext, &PauseParams);
    DbgPrint("NDIS6: PauseHandler returned 0x%08lx\n", (ULONG)Status);

    if (Status == NDIS_STATUS_PENDING)
    {
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = -50000000LL;  /* 5 seconds */
        KeWaitForSingleObject(&Ext->PauseEvent, Executive, KernelMode,
                              FALSE, &Timeout);
        Status = Ext->PauseStatus;
    }

    if (NT_SUCCESS(Status))
        Ext->PauseState = NDIS6_PAUSE_STATE_PAUSED;
    else
    {
        Ext->PauseState = NDIS6_PAUSE_STATE_RUNNING;  /* stay in running on fail */
        Ndis6SetTransmitAccepting(Ext, TRUE);
    }

    return Status;
}

NDIS_STATUS
Ndis6CallMiniportRestartEx(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT                 Ext;
    NDIS_MINIPORT_RESTART_PARAMETERS   RestartParams;
    NDIS_STATUS                        Status;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.RestartHandler == NULL ||
        Ext->MiniportAdapterContext == NULL)
    {
        Ext->PauseState = NDIS6_PAUSE_STATE_RUNNING;
        Ndis6SetTransmitAccepting(Ext, TRUE);
        return NDIS_STATUS_SUCCESS;
    }

    if (Ext->PauseState == NDIS6_PAUSE_STATE_RUNNING)
    {
        Ndis6SetTransmitAccepting(Ext, TRUE);
        return NDIS_STATUS_SUCCESS;
    }

    if (Ext->PauseState == NDIS6_PAUSE_STATE_RESTARTING)
    {
        return NDIS_STATUS_SUCCESS;
    }

    RtlZeroMemory(&RestartParams, sizeof(RestartParams));
    RestartParams.Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
    RestartParams.Header.Revision = NDIS_MINIPORT_RESTART_PARAMETERS_REVISION_1;
    RestartParams.Header.Size     = sizeof(RestartParams);
    RestartParams.RestartAttributes = NULL;
    RestartParams.Flags             = 0;

    Ext->PauseState    = NDIS6_PAUSE_STATE_RESTARTING;
    Ext->RestartStatus = NDIS_STATUS_PENDING;
    KeClearEvent(&Ext->RestartEvent);

    DbgPrint("NDIS6: Restart → driver\n");
    Status = Ext->DriverBlock->Characteristics.RestartHandler(
        Ext->MiniportAdapterContext, &RestartParams);
    DbgPrint("NDIS6: RestartHandler returned 0x%08lx\n", (ULONG)Status);

    if (Status == NDIS_STATUS_PENDING)
    {
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = -50000000LL;  /* 5 seconds */
        KeWaitForSingleObject(&Ext->RestartEvent, Executive, KernelMode,
                              FALSE, &Timeout);
        Status = Ext->RestartStatus;
    }

    if (NT_SUCCESS(Status))
    {
        Ext->PauseState = NDIS6_PAUSE_STATE_RUNNING;
        Ndis6SetTransmitAccepting(Ext, TRUE);
    }
    else
    {
        Ext->PauseState = NDIS6_PAUSE_STATE_PAUSED;  /* stay paused on fail */
        Ndis6SetTransmitAccepting(Ext, FALSE);
    }

    return Status;
}

/* ============================================================================
 *  NdisMPauseComplete / NdisMRestartComplete — driver-side callbacks
 *
 *  A driver returning PENDING from its PauseHandler or RestartHandler must
 *  call these when the transition finishes. We record the status and set
 *  the event the wait-side is blocked on.
 * ============================================================================ */

VOID
NTAPI
NdisMPauseComplete(
    _In_ NDIS_HANDLE NdisMiniportHandle)
{
    PLOGICAL_ADAPTER    Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT  Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    Ext->PauseStatus = NDIS_STATUS_SUCCESS;
    KeSetEvent(&Ext->PauseEvent, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
NdisMRestartComplete(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_STATUS Status)
{
    PLOGICAL_ADAPTER    Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT  Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    Ext->RestartStatus = Status;
    KeSetEvent(&Ext->RestartEvent, IO_NO_INCREMENT, FALSE);
}

/* ============================================================================
 *  Ndis6DestroyLogicalAdapter — full tear-down (called from REMOVE_DEVICE)
 * ============================================================================ */

VOID
Ndis6DestroyLogicalAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PDEVICE_OBJECT     Fdo;
    BOOLEAN            IsWdfManaged;
    KIRQL              OldIrql;
    extern LIST_ENTRY  AdapterListHead;
    extern KSPIN_LOCK  AdapterListLock;

    if (Adapter == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Fdo = Ext ? Ext->FunctionalDeviceObject : NULL;
    IsWdfManaged = Ext ? Ext->IsWdfManaged : FALSE;

    /* Phase 7B: detach any NDIS 6 filter modules attached to this adapter
     * BEFORE we tear anything else down — filters may try to issue OID
     * requests during DetachHandler and need the adapter still wired. */
    {
        extern VOID Ndis6DetachFiltersFromAdapter(PLOGICAL_ADAPTER);
        Ndis6DetachFiltersFromAdapter(Adapter);
    }

    /* Detach from the device stack BEFORE we free anything. */
    if (!IsWdfManaged && Adapter->NdisMiniportBlock.NextDeviceObject)
    {
        IoDetachDevice(Adapter->NdisMiniportBlock.NextDeviceObject);
        Adapter->NdisMiniportBlock.NextDeviceObject = NULL;
    }
    else if (IsWdfManaged)
    {
        Adapter->NdisMiniportBlock.NextDeviceObject = NULL;
    }

    /* Remove from the global adapter list so no new bind requests
     * can find this adapter. */
    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    if (Adapter->ListEntry.Flink && Adapter->ListEntry.Blink &&
        Adapter->ListEntry.Flink != &Adapter->ListEntry)
    {
        RemoveEntryList(&Adapter->ListEntry);
        Adapter->ListEntry.Flink = NULL;
        Adapter->ListEntry.Blink = NULL;
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    /* Free DMA, interrupt, and other resources we may have set up
     * during MiniportInitializeEx. */
    if (Ext)
    {
        Ndis6IoFreeDmaAdapter(Ext);
        if (Ext->InterruptObject)
        {
            IoDisconnectInterrupt(Ext->InterruptObject);
            Ext->InterruptObject = NULL;
        }

        /* Phase 3 thunk pools. */
        if (Ext->TxWrapperNblPool)
        {
            NdisFreeNetBufferListPool(Ext->TxWrapperNblPool);
            Ext->TxWrapperNblPool = NULL;
        }
        if (Ext->RxLegacyPacketPool)
        {
            NdisFreePacketPool(Ext->RxLegacyPacketPool);
            Ext->RxLegacyPacketPool = NULL;
        }
        if (Ext->RxLegacyBufferPool)
        {
            NdisFreeBufferPool(Ext->RxLegacyBufferPool);
            Ext->RxLegacyBufferPool = NULL;
        }

        Ndis6ResetMiniportAttributes(Adapter);
    }

    Ndis6FreeAdapterName(&Adapter->NdisMiniportBlock.MiniportName);

    /* Sever the LOGICAL_ADAPTER → Ext link before freeing Ext, so any
     * stale reference goes through a NULL check rather than touching
     * freed memory. */
    Adapter->Ndis6Context = NULL;
    Adapter->IsNdis6 = FALSE;

    if (Ext)
        ExFreePoolWithTag(Ext, NDIS6_ADAPTER_TAG);

    /* Finally delete the FDO. After this Adapter (which lives inside
     * Fdo->DeviceExtension) is gone — do NOT touch it again. */
    if (!IsWdfManaged && Fdo)
        IoDeleteDevice(Fdo);
}

/* ============================================================================
 *  Ndis6CreateImInstance
 *
 *  Like Ndis6CreateLogicalAdapter, but for an intermediate driver's upper
 *  (virtual) miniport: there is no PnP PDO and no lower device stack. The
 *  FDO doubles as its own "PDO" so NdisAllocateIoWorkItem and
 *  NdisMGetDeviceProperty keep working.
 * ============================================================================ */
NDIS_STATUS
Ndis6CreateImInstance(
    _In_  PNDIS6_DRIVER_BLOCK DriverBlock,
    _In_  PCUNICODE_STRING    DeviceName,
    _In_  NDIS_HANDLE         DeviceContext,
    _Out_ PLOGICAL_ADAPTER*   AdapterOut)
{
    PLOGICAL_ADAPTER    Adapter;
    PNDIS6_ADAPTER_EXT  Ext;
    PDEVICE_OBJECT      Fdo;
    NTSTATUS            Status;
    UNICODE_STRING      AdapterName;
    KIRQL               OldIrql;
    extern LIST_ENTRY   AdapterListHead;
    extern KSPIN_LOCK   AdapterListLock;

    if (DriverBlock == NULL || DeviceName == NULL || DeviceName->Buffer == NULL ||
        AdapterOut == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *AdapterOut = NULL;

    /* Duplicate the supplied (NUL-terminated) device name for the FDO. */
    if (!RtlCreateUnicodeString(&AdapterName, DeviceName->Buffer))
        return NDIS_STATUS_RESOURCES;

    Status = IoCreateDevice(DriverBlock->DriverObject, sizeof(LOGICAL_ADAPTER),
                            &AdapterName, FILE_DEVICE_PHYSICAL_NETCARD, 0, FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        Ndis6FreeAdapterName(&AdapterName);
        return Status;
    }

    Adapter = (PLOGICAL_ADAPTER)Fdo->DeviceExtension;
    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->IsNdis6 = TRUE;

    Ext = (PNDIS6_ADAPTER_EXT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_ADAPTER_EXT), NDIS6_ADAPTER_TAG);
    if (Ext == NULL)
    {
        IoDeleteDevice(Fdo);
        Ndis6FreeAdapterName(&AdapterName);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Ext, sizeof(*Ext));
    Ext->Adapter                = Adapter;
    Ext->DriverBlock            = DriverBlock;
    Ext->PhysicalDeviceObject   = Fdo;   /* self-PDO: no bus underneath */
    Ext->FunctionalDeviceObject = Fdo;
    KeInitializeSpinLock(&Ext->IsrLock);
    Ext->InterruptRundownState = NDIS6_INTERRUPT_RUNDOWN_STOPPING;
    KeInitializeEvent(&Ext->InterruptDrainEvent, NotificationEvent, TRUE);
    KeInitializeSpinLock(&Ext->TxLookupLock);
    InitializeListHead(&Ext->InFlightNblsTx);
    Ext->TxInFlightCount = 0;
    KeInitializeEvent(&Ext->TxDrainEvent, NotificationEvent, TRUE);
    Ext->TxAccepting = FALSE;
    Ext->PauseState    = NDIS6_PAUSE_STATE_PAUSED;
    Ext->PauseStatus   = NDIS_STATUS_SUCCESS;
    Ext->RestartStatus = NDIS_STATUS_SUCCESS;
    KeInitializeEvent(&Ext->PauseEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Ext->RestartEvent, SynchronizationEvent, FALSE);
    KeInitializeSpinLock(&Ext->OidWaiterLock);
    InitializeListHead(&Ext->OidWaiters);
    KeInitializeSpinLock(&Ext->FilterModuleListLock);
    InitializeListHead(&Ext->FilterModuleList);
    KeInitializeSpinLock(&Ext->ProtocolBindingListLock);
    InitializeListHead(&Ext->ProtocolBindingList);

    {
        NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
        NdisZeroMemory(&PoolParams, sizeof(PoolParams));
        PoolParams.Header.Type        = NDIS_OBJECT_TYPE_DEFAULT;
        PoolParams.Header.Revision    = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        PoolParams.Header.Size        = sizeof(NET_BUFFER_LIST_POOL_PARAMETERS);
        PoolParams.ProtocolId         = NDIS_PROTOCOL_ID_DEFAULT;
        PoolParams.fAllocateNetBuffer = TRUE;
        PoolParams.ContextSize        = 0;
        PoolParams.PoolTag            = 'TxNb';
        PoolParams.DataSize           = 0;
        Ext->TxWrapperNblPool = NdisAllocateNetBufferListPool(NULL, &PoolParams);
    }
    {
        NDIS_STATUS PoolStatus;
        NdisAllocateBufferPool(&PoolStatus, &Ext->RxLegacyBufferPool, 64);
        if (PoolStatus != NDIS_STATUS_SUCCESS)
            Ext->RxLegacyBufferPool = NULL;
        NdisAllocatePacketPool(&PoolStatus, &Ext->RxLegacyPacketPool, 64,
                               sizeof(PVOID) * 4);
        if (PoolStatus != NDIS_STATUS_SUCCESS)
            Ext->RxLegacyPacketPool = NULL;
    }

    Adapter->Ndis6Context = Ext;
    InitializeListHead(&Adapter->ProtocolListHead);
    KeInitializeSpinLock(&Adapter->NdisMiniportBlock.Lock);
    Adapter->NdisMiniportBlock.MiniportName          = AdapterName;
    Adapter->NdisMiniportBlock.PhysicalDeviceObject  = Fdo;
    Adapter->NdisMiniportBlock.DeviceObject          = Fdo;
    Adapter->NdisMiniportBlock.BusType               = NdisInterfaceInternal;
    Adapter->NdisMiniportBlock.BusNumber             = 0;
    /* The driver-private context recovered via NdisIMGetDeviceContext inside
     * the miniport's InitializeHandlerEx. */
    Adapter->NdisMiniportBlock.DeviceContext         = DeviceContext;
    /* No lower device stack to forward PnP/Power IRPs to. */
    Adapter->NdisMiniportBlock.NextDeviceObject      = NULL;

    Fdo->Flags |= DO_DIRECT_IO | DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    InsertTailList(&AdapterListHead, &Adapter->ListEntry);
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    *AdapterOut = Adapter;
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  Ndis6InitializeImDeviceInstance
 *
 *  The worker behind NdisIMInitializeDeviceInstanceEx: create the IM upper
 *  miniport instance, run the driver's InitializeHandlerEx, and (on success)
 *  fan out a bind notification to the registered protocols — the same sequence
 *  the PnP START path performs for a normal miniport.
 * ============================================================================ */
NDIS_STATUS
Ndis6InitializeImDeviceInstance(
    _In_ PNDIS6_DRIVER_BLOCK DriverBlock,
    _In_ PCUNICODE_STRING    DeviceName,
    _In_ NDIS_HANDLE         DeviceContext)
{
    PLOGICAL_ADAPTER   Adapter = NULL;
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS        Status;
    extern VOID Ndis6BindAllProtocolsToAdapter(PLOGICAL_ADAPTER);

    Status = Ndis6CreateImInstance(DriverBlock, DeviceName, DeviceContext, &Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Ndis6ImSeedTcpipRegistry(DeviceName);

    Status = Ndis6CallMiniportInitializeEx(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Ndis6DestroyLogicalAdapter(Adapter);
        return Status;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext != NULL)
        Ext->Initialized = TRUE;

    Ndis6CallMiniportRestartEx(Adapter);

    /* Bind every registered native NDIS 6 protocol; legacy NDIS 5 protocols
     * bind separately via their own Linkage\Bind list. */
    Ndis6BindAllProtocolsToAdapter(Adapter);

    {
        extern VOID ndisRebindAllProtocols(VOID);
        ndisRebindAllProtocols();
    }

    return NDIS_STATUS_SUCCESS;
}

/* EOF */
