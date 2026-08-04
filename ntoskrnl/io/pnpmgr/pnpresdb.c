/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     In-memory PnP resource database
 * COPYRIGHT:   Copyright ReactOS contributors
 */

#include <ntoskrnl.h>
#include <reactos/drivers/cmreslist.h>
#define NDEBUG
#include <debug.h>

typedef enum _PI_RES_CLASS
{
    PiResClassMemory = 0,
    PiResClassPort,
    PiResClassInterrupt,
    PiResClassDma,
    PiResClassBusNumber,
    PiResClassMax
} PI_RES_CLASS;

typedef struct _PI_RES_ENTRY
{
    LIST_ENTRY ListEntry;
    PDEVICE_NODE Owner;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Desc;
} PI_RES_ENTRY, *PPI_RES_ENTRY;

typedef struct _PI_RES_BUCKET
{
    LIST_ENTRY List;
    KSPIN_LOCK Lock;
} PI_RES_BUCKET;

static PI_RES_BUCKET IopResBuckets[PiResClassMax];
static BOOLEAN IopResDbReady = FALSE;
static volatile LONG IopResDbSeedState = 0;
static KEVENT IopResDbSeededEvent;

static VOID IopResDbSeedFromRegistry(VOID);

VOID
IopResDbInitialize(VOID)
{
    ULONG i;
    for (i = 0; i < PiResClassMax; i++)
    {
        InitializeListHead(&IopResBuckets[i].List);
        KeInitializeSpinLock(&IopResBuckets[i].Lock);
    }
    KeInitializeEvent(&IopResDbSeededEvent, NotificationEvent, FALSE);
    IopResDbReady = TRUE;
}

VOID
IopResDbEnsureSeeded(VOID)
{
    if (InterlockedCompareExchange(&IopResDbSeedState, 1, 0) == 0)
    {
        IopResDbSeedFromRegistry();
        InterlockedExchange(&IopResDbSeedState, 2);
        KeSetEvent(&IopResDbSeededEvent, IO_NO_INCREMENT, FALSE);
    }
    else if (IopResDbSeedState != 2)
    {
        /* Another thread is seeding; wait so we never query a partial database */
        KeWaitForSingleObject(&IopResDbSeededEvent, Executive, KernelMode, FALSE, NULL);
    }
}

static
BOOLEAN
IopResClassFromType(_In_ UCHAR Type, _Out_ PI_RES_CLASS *Class)
{
    switch (Type)
    {
        case CmResourceTypeMemory:      *Class = PiResClassMemory; return TRUE;
        case CmResourceTypePort:        *Class = PiResClassPort; return TRUE;
        case CmResourceTypeInterrupt:   *Class = PiResClassInterrupt; return TRUE;
        case CmResourceTypeDma:         *Class = PiResClassDma; return TRUE;
        case CmResourceTypeBusNumber:   *Class = PiResClassBusNumber; return TRUE;
        default:                        return FALSE;
    }
}

/* Pairwise overlap test, mirroring IopCheckResourceDescriptor (pnpres.c). */
static
BOOLEAN
IopResDbIsAncestorOrSelf(_In_ PDEVICE_NODE Ancestor, _In_ PDEVICE_NODE DeviceNode)
{
    while (DeviceNode)
    {
        if (DeviceNode == Ancestor)
            return TRUE;
        DeviceNode = DeviceNode->Parent;
    }
    return FALSE;
}

static
BOOLEAN
IopResDbConflict(_In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR a, _In_opt_ PDEVICE_NODE aOwner, _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR b, _In_opt_ PDEVICE_NODE bOwner)
{
    if (a->Type != b->Type)
        return FALSE;
    if (a->ShareDisposition == CmResourceShareShared && b->ShareDisposition == CmResourceShareShared)
        return FALSE;

    switch (a->Type)
    {
        case CmResourceTypeMemory:
        {
            UINT64 aStart = (UINT64)a->u.Memory.Start.QuadPart, aEnd = aStart + a->u.Memory.Length;
            UINT64 bStart = (UINT64)b->u.Memory.Start.QuadPart, bEnd = bStart + b->u.Memory.Length;
            if (!(aStart < bEnd && bStart < aEnd))
                return FALSE;
            if ((a->Flags & CM_RESOURCE_MEMORY_WINDOW_DECODE) && aStart <= bStart && aEnd >= bEnd)
            {
                return aOwner && bOwner ? !IopResDbIsAncestorOrSelf(aOwner, bOwner) : FALSE;
            }
            if ((b->Flags & CM_RESOURCE_MEMORY_WINDOW_DECODE) && bStart <= aStart && bEnd >= aEnd)
            {
                return aOwner && bOwner ? !IopResDbIsAncestorOrSelf(bOwner, aOwner) : FALSE;
            }
            return TRUE;
        }
        case CmResourceTypePort:
        {
            UINT64 aStart = (UINT64)a->u.Port.Start.QuadPart, aEnd = aStart + a->u.Port.Length;
            UINT64 bStart = (UINT64)b->u.Port.Start.QuadPart, bEnd = bStart + b->u.Port.Length;
            if (!(aStart < bEnd && bStart < aEnd))
                return FALSE;
            if ((a->Flags & CM_RESOURCE_PORT_WINDOW_DECODE) && aStart <= bStart && aEnd >= bEnd)
            {
                return aOwner && bOwner ? !IopResDbIsAncestorOrSelf(aOwner, bOwner) : FALSE;
            }
            if ((b->Flags & CM_RESOURCE_PORT_WINDOW_DECODE) && bStart <= aStart && bEnd >= aEnd)
            {
                return aOwner && bOwner ? !IopResDbIsAncestorOrSelf(bOwner, aOwner) : FALSE;
            }
            return TRUE;
        }
        case CmResourceTypeInterrupt:
            return (a->u.Interrupt.Vector == b->u.Interrupt.Vector);
        case CmResourceTypeBusNumber:
        {
            UINT32 aStart = a->u.BusNumber.Start, aEnd = aStart + a->u.BusNumber.Length;
            UINT32 bStart = b->u.BusNumber.Start, bEnd = bStart + b->u.BusNumber.Length;
            if (!(aStart < bEnd && bStart < aEnd))
                return FALSE;
            if ((a->ShareDisposition == CmResourceShareShared &&
                 aStart < bStart && aEnd >= bEnd) ||
                (b->ShareDisposition == CmResourceShareShared &&
                 bStart < aStart && bEnd >= aEnd))
            {
                return FALSE;
            }
            return TRUE;
        }
        case CmResourceTypeDma:
            return (a->u.Dma.Channel == b->u.Dma.Channel);
        default:
            return FALSE;
    }
}

/* Conflicts are logged but still recorded (non-enforcing). */
NTSTATUS
IopResDbReserve(_In_ PDEVICE_NODE DeviceNode, _In_opt_ PCM_RESOURCE_LIST ResourceList, _Out_opt_ PBOOLEAN Conflict)
{
    PCM_FULL_RESOURCE_DESCRIPTOR full;
    ULONG i, j;

    if (Conflict)
        *Conflict = FALSE;
    if (!IopResDbReady || ResourceList == NULL || ResourceList->Count == 0)
        return STATUS_SUCCESS;

    full = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++)
    {
        PCM_PARTIAL_RESOURCE_LIST partial = &full->PartialResourceList;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &partial->PartialDescriptors[0];

        for (j = 0; j < partial->Count; j++, desc = CmiGetNextPartialDescriptor(desc))
        {
            PI_RES_CLASS cls;
            PPI_RES_ENTRY entry, existing;
            PLIST_ENTRY le;
            KIRQL irql;

            if (!IopResClassFromType(desc->Type, &cls))
                continue;

            entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*entry), TAG_IO);
            if (entry == NULL)
                continue;
            entry->Owner = DeviceNode;
            entry->Desc = *desc;

            KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
            for (le = IopResBuckets[cls].List.Flink; le != &IopResBuckets[cls].List; le = le->Flink)
            {
                existing = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
                if (existing->Owner == DeviceNode)
                    continue;
                if (IopResDbConflict(desc, DeviceNode, &existing->Desc, existing->Owner))
                {
                    if (Conflict)
                        *Conflict = TRUE;
                    if (DeviceNode)
                        DPRINT("PnP: resource conflict (type %u) involving %wZ\n", desc->Type, &DeviceNode->InstancePath);
                    break;
                }
            }
            InsertTailList(&IopResBuckets[cls].List, &entry->ListEntry);
            KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);
        }

        full = CmiGetNextResourceDescriptor(full);
    }

    return STATUS_SUCCESS;
}

/* Check one candidate descriptor against the database; returns the conflicting entry. */
BOOLEAN
IopResDbCheckDescriptor(_In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate, _In_opt_ PDEVICE_NODE CandidateOwner, _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Conflicting)
{
    PI_RES_CLASS cls;
    PLIST_ENTRY le;
    KIRQL irql;
    BOOLEAN found = FALSE;

    if (!IopResDbReady || !IopResClassFromType(Candidate->Type, &cls))
        return FALSE;

    KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
    for (le = IopResBuckets[cls].List.Flink; le != &IopResBuckets[cls].List; le = le->Flink)
    {
        PPI_RES_ENTRY entry = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
        if (IopResDbConflict(Candidate, CandidateOwner, &entry->Desc, entry->Owner))
        {
            if (Conflicting)
                *Conflicting = entry->Desc;
            found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);
    return found;
}

/* Check every descriptor in a list against the database. */
BOOLEAN
IopResDbCheckList(_In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    PCM_FULL_RESOURCE_DESCRIPTOR full;
    ULONG i, j;

    if (!IopResDbReady || ResourceList == NULL || ResourceList->Count == 0)
        return FALSE;

    full = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++)
    {
        PCM_PARTIAL_RESOURCE_LIST partial = &full->PartialResourceList;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &partial->PartialDescriptors[0];

        for (j = 0; j < partial->Count; j++, desc = CmiGetNextPartialDescriptor(desc))
        {
            if (IopResDbCheckDescriptor(desc, NULL, NULL))
                return TRUE;
        }

        full = CmiGetNextResourceDescriptor(full);
    }

    return FALSE;
}

VOID
IopResDbRelease(_In_ PDEVICE_NODE DeviceNode)
{
    ULONG cls;

    if (!IopResDbReady)
        return;

    for (cls = 0; cls < PiResClassMax; cls++)
    {
        PLIST_ENTRY le;
        KIRQL irql;

        KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
        le = IopResBuckets[cls].List.Flink;
        while (le != &IopResBuckets[cls].List)
        {
            PPI_RES_ENTRY entry = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
            le = le->Flink;
            if (entry->Owner == DeviceNode)
            {
                RemoveEntryList(&entry->ListEntry);
                ExFreePoolWithTag(entry, TAG_IO);
            }
        }
        KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);
    }
}

/* Scan the resource map once, recording every raw allocation under a NULL owner. */
static
VOID
IopResDbSeedFromRegistry(VOID)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING keyName;
    HANDLE mapKey = NULL, classKey = NULL, driverKey = NULL;
    PKEY_BASIC_INFORMATION basic;
    PKEY_VALUE_PARTIAL_INFORMATION value;
    PKEY_VALUE_BASIC_INFORMATION valueName;
    ULONG i1, i2, i3, need;
    NTSTATUS status;

    RtlInitUnicodeString(&keyName, L"\\Registry\\Machine\\HARDWARE\\RESOURCEMAP");
    InitializeObjectAttributes(&oa, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&mapKey, KEY_ENUMERATE_SUB_KEYS, &oa)))
        return;

    for (i1 = 0; ; i1++)
    {
        status = ZwEnumerateKey(mapKey, i1, KeyBasicInformation, NULL, 0, &need);
        if (status == STATUS_NO_MORE_ENTRIES)
            break;
        if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL)
            break;
        basic = ExAllocatePoolWithTag(PagedPool, need, TAG_IO);
        if (basic == NULL)
            break;
        status = ZwEnumerateKey(mapKey, i1, KeyBasicInformation, basic, need, &need);
        if (!NT_SUCCESS(status))
        {
            ExFreePoolWithTag(basic, TAG_IO);
            break;
        }

        keyName.Buffer = basic->Name;
        keyName.MaximumLength = keyName.Length = (USHORT)basic->NameLength;
        InitializeObjectAttributes(&oa, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, mapKey, NULL);
        status = ZwOpenKey(&classKey, KEY_ENUMERATE_SUB_KEYS, &oa);
        ExFreePoolWithTag(basic, TAG_IO);
        if (!NT_SUCCESS(status))
            continue;

        for (i2 = 0; ; i2++)
        {
            status = ZwEnumerateKey(classKey, i2, KeyBasicInformation, NULL, 0, &need);
            if (status == STATUS_NO_MORE_ENTRIES)
                break;
            if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL)
                break;
            basic = ExAllocatePoolWithTag(PagedPool, need, TAG_IO);
            if (basic == NULL)
                break;
            status = ZwEnumerateKey(classKey, i2, KeyBasicInformation, basic, need, &need);
            if (!NT_SUCCESS(status))
            {
                ExFreePoolWithTag(basic, TAG_IO);
                break;
            }

            keyName.Buffer = basic->Name;
            keyName.MaximumLength = keyName.Length = (USHORT)basic->NameLength;
            InitializeObjectAttributes(&oa, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, classKey, NULL);
            status = ZwOpenKey(&driverKey, KEY_QUERY_VALUE, &oa);
            ExFreePoolWithTag(basic, TAG_IO);
            if (!NT_SUCCESS(status))
                continue;

            for (i3 = 0; ; i3++)
            {
                PCM_RESOURCE_LIST list;

                status = ZwEnumerateValueKey(driverKey, i3, KeyValuePartialInformation, NULL, 0, &need);
                if (status == STATUS_NO_MORE_ENTRIES)
                    break;
                if (status != STATUS_BUFFER_TOO_SMALL)
                    break;
                value = ExAllocatePoolWithTag(PagedPool, need, TAG_IO);
                if (value == NULL)
                    break;
                status = ZwEnumerateValueKey(driverKey, i3, KeyValuePartialInformation, value, need, &need);
                if (!NT_SUCCESS(status))
                {
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }

                status = ZwEnumerateValueKey(driverKey, i3, KeyValueBasicInformation, NULL, 0, &need);
                if (status != STATUS_BUFFER_TOO_SMALL)
                {
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }
                valueName = ExAllocatePoolWithTag(PagedPool, need + sizeof(WCHAR), TAG_IO);
                if (valueName == NULL)
                {
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }
                status = ZwEnumerateValueKey(driverKey, i3, KeyValueBasicInformation, valueName, need, &need);
                if (!NT_SUCCESS(status))
                {
                    ExFreePoolWithTag(valueName, TAG_IO);
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }
                valueName->Name[valueName->NameLength / sizeof(WCHAR)] = UNICODE_NULL;

                /* Only raw lists; skip translated copies */
                list = (PCM_RESOURCE_LIST)value->Data;
                if (wcsstr(valueName->Name, L".Translated") == NULL && value->Type == REG_RESOURCE_LIST && IopIsResourceListValid(list, value->DataLength) && list->Count != 0)
                    IopResDbReserve(NULL, list, NULL);

                ExFreePoolWithTag(valueName, TAG_IO);
                ExFreePoolWithTag(value, TAG_IO);
            }

            ZwClose(driverKey);
        }

        ZwClose(classKey);
    }

    ZwClose(mapKey);
}
