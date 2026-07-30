/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Process-owned device linkage for process-wide paging work
 */

#ifndef _DXGKRNL_PROCESS_DEVICE_CORE_H_
#define _DXGKRNL_PROCESS_DEVICE_CORE_H_

/*
 * The caller owns the lock protecting the list head for every operation below.
 * TryReference must acquire a real lifetime reference before returning TRUE.
 * The selected miniport handle is immutable while that reference is held.
 */
typedef BOOLEAN
(*PDXGK_PROCESS_DEVICE_TRY_REFERENCE)(
    _In_ PVOID Device,
    _In_opt_ PVOID Context);

typedef struct _DXGK_PROCESS_DEVICE_LINK
{
    LIST_ENTRY Entry;
    PVOID Device;
    HANDLE MiniportDevice;
} DXGK_PROCESS_DEVICE_LINK, *PDXGK_PROCESS_DEVICE_LINK;

FORCEINLINE
VOID
DxgkProcessDeviceLinkInitialize(
    _Out_ PDXGK_PROCESS_DEVICE_LINK Link,
    _In_ PVOID Device)
{
    InitializeListHead(&Link->Entry);
    Link->Device = Device;
    Link->MiniportDevice = NULL;
}

FORCEINLINE
BOOLEAN
DxgkProcessDeviceLinkAttach(
    _Inout_ PLIST_ENTRY ListHead,
    _Inout_ PDXGK_PROCESS_DEVICE_LINK Link,
    _In_ HANDLE MiniportDevice)
{
    if (ListHead == NULL ||
        Link == NULL ||
        Link->Device == NULL ||
        !IsListEmpty(&Link->Entry))
    {
        return FALSE;
    }

    Link->MiniportDevice = MiniportDevice;
    InsertTailList(ListHead, &Link->Entry);
    return TRUE;
}

FORCEINLINE
BOOLEAN
DxgkProcessDeviceLinkDetach(
    _Inout_ PDXGK_PROCESS_DEVICE_LINK Link)
{
    if (Link == NULL || IsListEmpty(&Link->Entry))
        return FALSE;

    RemoveEntryList(&Link->Entry);
    InitializeListHead(&Link->Entry);
    Link->MiniportDevice = NULL;
    return TRUE;
}

FORCEINLINE
BOOLEAN
DxgkProcessDeviceTryReference(
    _In_ PLIST_ENTRY ListHead,
    _In_ PDXGK_PROCESS_DEVICE_TRY_REFERENCE TryReference,
    _In_opt_ PVOID Context,
    _Out_ PVOID *OutDevice,
    _Out_ PHANDLE OutMiniportDevice)
{
    PLIST_ENTRY Entry;

    if (OutDevice == NULL || OutMiniportDevice == NULL)
        return FALSE;
    *OutDevice = NULL;
    *OutMiniportDevice = NULL;

    if (ListHead == NULL || TryReference == NULL)
        return FALSE;

    for (Entry = ListHead->Flink;
         Entry != ListHead;
         Entry = Entry->Flink)
    {
        PDXGK_PROCESS_DEVICE_LINK Link =
            CONTAINING_RECORD(Entry, DXGK_PROCESS_DEVICE_LINK, Entry);

        if (Link->Device == NULL ||
            Link->MiniportDevice == NULL ||
            !TryReference(Link->Device, Context))
        {
            continue;
        }

        *OutDevice = Link->Device;
        *OutMiniportDevice = Link->MiniportDevice;
        return TRUE;
    }

    return FALSE;
}

#endif /* _DXGKRNL_PROCESS_DEVICE_CORE_H_ */
