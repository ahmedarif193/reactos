/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot driver callbacks for early launch drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define TAG_IO_BOOT_CALLBACK 'cBoI'

#define PNP_INITIALIZE_UNKNOWN_DRIVERS 0x1
#define PNP_INITIALIZE_BAD_CRITICAL_DRIVERS 0x3
#define PNP_INITIALIZE_BAD_DRIVERS 0x7

typedef struct _IOP_BOOT_DRIVER_CALLBACK
{
    LIST_ENTRY ListEntry;
    EX_RUNDOWN_REF RundownRef;
    PBOOT_DRIVER_CALLBACK_FUNCTION Function;
    PVOID Context;
} IOP_BOOT_DRIVER_CALLBACK, *PIOP_BOOT_DRIVER_CALLBACK;

static LIST_ENTRY IopBootDriverCallbackList = {&IopBootDriverCallbackList, &IopBootDriverCallbackList};
static EX_PUSH_LOCK IopBootDriverCallbackLock;
static BOOLEAN IopBootDriverCallbacksDone;

static
VOID
IopCallBootDriverCallbacks(
    _In_ BDCB_CALLBACK_TYPE Type,
    _Inout_ PVOID Information)
{
    PIOP_BOOT_DRIVER_CALLBACK Callback;
    PIOP_BOOT_DRIVER_CALLBACK *Snapshot;
    PLIST_ENTRY Link;
    ULONG Count = 0, Index;

    if (IsListEmpty(&IopBootDriverCallbackList))
    {
        return;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&IopBootDriverCallbackLock);

    for (Link = IopBootDriverCallbackList.Flink; Link != &IopBootDriverCallbackList; Link = Link->Flink)
    {
        Count++;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPoolNx, Count * sizeof(*Snapshot), TAG_IO_BOOT_CALLBACK);
    Count = 0;
    if (Snapshot != NULL)
    {
        for (Link = IopBootDriverCallbackList.Flink; Link != &IopBootDriverCallbackList; Link = Link->Flink)
        {
            Callback = CONTAINING_RECORD(Link, IOP_BOOT_DRIVER_CALLBACK, ListEntry);
            if (ExAcquireRundownProtection(&Callback->RundownRef))
            {
                Snapshot[Count++] = Callback;
            }
        }
    }

    ExReleasePushLockShared(&IopBootDriverCallbackLock);
    KeLeaveCriticalRegion();

    for (Index = 0; Index < Count; Index++)
    {
        Snapshot[Index]->Function(Snapshot[Index]->Context, Type, Information);
        ExReleaseRundownProtection(&Snapshot[Index]->RundownRef);
    }

    if (Snapshot != NULL)
    {
        ExFreePoolWithTag(Snapshot, TAG_IO_BOOT_CALLBACK);
    }
}

VOID
NTAPI
IopBootDriverStatusUpdate(
    _In_ BDCB_STATUS_UPDATE_TYPE StatusType)
{
    BDCB_STATUS_UPDATE_CONTEXT Context;

    Context.StatusType = StatusType;
    IopCallBootDriverCallbacks(BdCbStatusUpdate, &Context);

    if (StatusType == BdCbStatusPrepareForUnload)
    {
        IopBootDriverCallbacksDone = TRUE;
    }
}

BOOLEAN
NTAPI
IopBootImageAllowed(
    _In_ PCUNICODE_STRING ImageName,
    _In_opt_ PCUNICODE_STRING RegistryPath)
{
    static UNICODE_STRING PolicyKey = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\EarlyLaunch");
    static UNICODE_STRING PolicyValue = RTL_CONSTANT_STRING(L"DriverLoadPolicy");
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION ValueInformation = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    BDCB_IMAGE_INFORMATION Information;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Policy = PNP_INITIALIZE_BAD_CRITICAL_DRIVERS;
    ULONG Length;
    HANDLE Key;

    if (IsListEmpty(&IopBootDriverCallbackList))
    {
        return TRUE;
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Classification = BdCbClassificationUnknownImage;
    Information.ImageName = *ImageName;
    if (RegistryPath != NULL)
    {
        Information.RegistryPath = *RegistryPath;
    }
    IopCallBootDriverCallbacks(BdCbInitializeImage, &Information);

    InitializeObjectAttributes(&ObjectAttributes, &PolicyKey, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(ZwOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes)))
    {
        if (NT_SUCCESS(ZwQueryValueKey(Key, &PolicyValue, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length)) &&
            ValueInformation->Type == REG_DWORD &&
            ValueInformation->DataLength == sizeof(ULONG))
        {
            Policy = *(PULONG)ValueInformation->Data;
        }
        ZwClose(Key);
    }

    switch (Information.Classification)
    {
        case BdCbClassificationKnownGoodImage:
            return TRUE;

        case BdCbClassificationKnownBadImage:
            return (Policy & PNP_INITIALIZE_BAD_DRIVERS) == PNP_INITIALIZE_BAD_DRIVERS;

        case BdCbClassificationKnownBadImageBootCritical:
            return (Policy & PNP_INITIALIZE_BAD_CRITICAL_DRIVERS) == PNP_INITIALIZE_BAD_CRITICAL_DRIVERS;

        default:
            return (Policy & PNP_INITIALIZE_UNKNOWN_DRIVERS) != 0;
    }
}

PVOID
NTAPI
IoRegisterBootDriverCallback(
    _In_ PBOOT_DRIVER_CALLBACK_FUNCTION CallbackFunction,
    _In_opt_ PVOID CallbackContext)
{
    PIOP_BOOT_DRIVER_CALLBACK Callback;
    PAGED_CODE();

    Callback = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Callback), TAG_IO_BOOT_CALLBACK);
    if (Callback == NULL)
    {
        return NULL;
    }

    ExInitializeRundownProtection(&Callback->RundownRef);
    Callback->Function = CallbackFunction;
    Callback->Context = CallbackContext;
    InitializeListHead(&Callback->ListEntry);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&IopBootDriverCallbackLock);
    if (!IopBootDriverCallbacksDone)
    {
        InsertTailList(&IopBootDriverCallbackList, &Callback->ListEntry);
    }
    ExReleasePushLockExclusive(&IopBootDriverCallbackLock);
    KeLeaveCriticalRegion();

    return Callback;
}

VOID
NTAPI
IoUnregisterBootDriverCallback(
    _In_ PVOID CallbackHandle)
{
    PIOP_BOOT_DRIVER_CALLBACK Callback = CallbackHandle;
    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&IopBootDriverCallbackLock);
    RemoveEntryList(&Callback->ListEntry);
    ExReleasePushLockExclusive(&IopBootDriverCallbackLock);
    KeLeaveCriticalRegion();

    ExWaitForRundownProtectionRelease(&Callback->RundownRef);
    ExFreePoolWithTag(Callback, TAG_IO_BOOT_CALLBACK);
}
