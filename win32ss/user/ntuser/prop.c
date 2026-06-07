/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Window properties
 * FILE:             win32ss/user/ntuser/prop.c
 * PROGRAMER:        Casper S. Hornstrup (chorns@users.sourceforge.net)
 */

#include <win32k.h>
DBG_DEFAULT_CHANNEL(UserProp);

/* STATIC FUNCTIONS **********************************************************/

PPROPERTY
FASTCALL
IntGetProp(
    _In_ PWND Window,
    _In_ ATOM Atom,
    _In_ BOOLEAN SystemProp)
{
    PLIST_ENTRY ListEntry;
    PPROPERTY Property;
    UINT i;
    WORD SystemFlag = SystemProp ? PROPERTY_FLAG_SYSTEM : 0;

    NT_ASSERT(UserIsEntered());
    ListEntry = Window->PropListHead.Flink;

    for (i = 0; i < Window->PropListItems; i++)
    {
        Property = CONTAINING_RECORD(ListEntry, PROPERTY, PropListEntry);
        ListEntry = ListEntry->Flink;

        if (Property->Atom == Atom &&
            (Property->fs & PROPERTY_FLAG_SYSTEM) == SystemFlag)
        {
            return Property;
        }
    }
    NT_ASSERT(ListEntry == &Window->PropListHead);
    return NULL;
}

HANDLE
FASTCALL
UserGetProp(
    _In_ PWND Window,
    _In_ ATOM Atom,
    _In_ BOOLEAN SystemProp)
{
    PDESKTOP pdesk = Window->head.rpdesk;
    PPROPERTY Prop;
    HANDLE Data = NULL;

    NT_ASSERT(UserIsEntered());
    /* The window's property list is serialized by its desktop lock (read side
     * shared); read Data while still holding it so a concurrent remover cannot
     * free the property under us. */
    if (pdesk) UserEnterDesktopShared(pdesk);
    Prop = IntGetProp(Window, Atom, SystemProp);
    if (Prop) Data = Prop->Data;
    if (pdesk) UserLeaveDesktop(pdesk);
    return Data;
}

_Success_(return)
HANDLE
FASTCALL
UserRemoveProp(
    _In_ PWND Window,
    _In_ ATOM Atom,
    _In_ BOOLEAN SystemProp)
{
    PDESKTOP pdesk = Window->head.rpdesk;
    PPROPERTY Prop;
    HANDLE Data = NULL;

    NT_ASSERT(UserIsEntered());
    /* Property-list writer: serialize on the window's desktop lock (exclusive). */
    if (pdesk) UserEnterDesktopExclusive(pdesk);
    Prop = IntGetProp(Window, Atom, SystemProp);
    if (Prop != NULL)
    {
        Data = Prop->Data;
        RemoveEntryList(&Prop->PropListEntry);
        UserHeapFree(Prop);
        Window->PropListItems--;
    }
    if (pdesk) UserLeaveDesktop(pdesk);
    return Data;
}

_Success_(return)
BOOL
FASTCALL
UserSetProp(
    _In_ PWND Window,
    _In_ ATOM Atom,
    _In_ HANDLE Data,
    _In_ BOOLEAN SystemProp)
{
    PDESKTOP pdesk = Window->head.rpdesk;
    PPROPERTY Prop;
    BOOL Ret = FALSE;

    NT_ASSERT(UserIsEntered());
    /* Property-list writer: serialize on the window's desktop lock (exclusive). */
    if (pdesk) UserEnterDesktopExclusive(pdesk);
    Prop = IntGetProp(Window, Atom, SystemProp);
    if (Prop == NULL)
    {
        Prop = UserHeapAlloc(sizeof(PROPERTY));
        if (Prop == NULL)
        {
            goto Exit;
        }
        Prop->Atom = Atom;
        Prop->fs = SystemProp ? PROPERTY_FLAG_SYSTEM : 0;
        InsertTailList(&Window->PropListHead, &Prop->PropListEntry);
        Window->PropListItems++;
    }

    Prop->Data = Data;
    Ret = TRUE;

Exit:
    if (pdesk) UserLeaveDesktop(pdesk);
    return Ret;
}

VOID
FASTCALL
UserRemoveWindowProps(
    _In_ PWND Window)
{
    PLIST_ENTRY ListEntry;
    PPROPERTY Property;

    NT_ASSERT(UserIsEnteredExclusive());
    while (!IsListEmpty(&Window->PropListHead))
    {
        ListEntry = Window->PropListHead.Flink;
        Property = CONTAINING_RECORD(ListEntry, PROPERTY, PropListEntry);
        RemoveEntryList(&Property->PropListEntry);
        UserHeapFree(Property);
        Window->PropListItems--;
    }
    return;
}

/* FUNCTIONS *****************************************************************/

NTSTATUS
APIENTRY
NtUserBuildPropList(
    _In_ HWND hWnd,
    _Out_writes_bytes_to_opt_(BufferSize, *Count * sizeof(PROPLISTITEM)) LPVOID Buffer,
    _In_ DWORD BufferSize,
    _Out_opt_ DWORD *Count)
{
    PWND Window;
    PPROPERTY Property;
    PLIST_ENTRY ListEntry;
    PROPLISTITEM listitem = { 0 }, *li;
    NTSTATUS Status;
    DWORD Cnt = 0;
    PDESKTOP pdesk = NULL;

    TRACE("Enter NtUserBuildPropList\n");
    UserEnterShared();

    Window = UserGetWindowObject(hWnd);
    if (Window == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Exit;
    }

    /* Serialize the property-list read on the window's desktop lock (shared). */
    pdesk = Window->head.rpdesk;
    if (pdesk) UserEnterDesktopShared(pdesk);

    if (Buffer)
    {
        if (!BufferSize || (BufferSize % sizeof(PROPLISTITEM) != 0))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Exit;
        }

        /* Copy list */
        li = (PROPLISTITEM *)Buffer;
        ListEntry = Window->PropListHead.Flink;
        while ((BufferSize >= sizeof(PROPLISTITEM)) &&
               (ListEntry != &Window->PropListHead))
        {
            Property = CONTAINING_RECORD(ListEntry, PROPERTY, PropListEntry);
            ListEntry = ListEntry->Flink;
            if (!(Property->fs & PROPERTY_FLAG_SYSTEM))
            {
                listitem.Atom = Property->Atom;
                listitem.Data = Property->Data;

                Status = MmCopyToCaller(li, &listitem, sizeof(PROPLISTITEM));
                if (!NT_SUCCESS(Status))
                {
                    goto Exit;
                }

                BufferSize -= sizeof(PROPLISTITEM);
                Cnt++;
                li++;
            }
        }

    }
    else
    {
        /* FIXME: This counts user and system props */
        Cnt = Window->PropListItems * sizeof(PROPLISTITEM);
    }

    if (Count)
    {
        Status = MmCopyToCaller(Count, &Cnt, sizeof(DWORD));
        if (!NT_SUCCESS(Status))
        {
            goto Exit;
        }
    }

    Status = STATUS_SUCCESS;

Exit:
    if (pdesk) UserLeaveDesktop(pdesk);
    TRACE("Leave NtUserBuildPropList, ret=%lx\n", Status);
    UserLeave();

    return Status;
}

HANDLE
APIENTRY
NtUserRemoveProp(
    _In_ HWND hWnd,
    _In_ ATOM Atom)
{
    PWND Window;
    HANDLE Data = NULL;

    TRACE("Enter NtUserRemoveProp\n");
    UserEnterShared();

    Window = UserGetWindowObject(hWnd);
    if (Window == NULL)
    {
        goto Exit;
    }

    Data = UserRemoveProp(Window, Atom, FALSE);

Exit:
    TRACE("Leave NtUserRemoveProp, ret=%p\n", Data);
    UserLeave();

    return Data;
}

BOOL
APIENTRY
NtUserSetProp(
    _In_ HWND hWnd,
    _In_ ATOM Atom,
    _In_ HANDLE Data)
{
    PWND Window;
    BOOL Ret;

    TRACE("Enter NtUserSetProp\n");
    UserEnterShared();

    Window = UserGetWindowObject(hWnd);
    if (Window == NULL)
    {
        Ret = FALSE;
        goto Exit;
    }

    Ret = UserSetProp(Window, Atom, Data, FALSE);

Exit:
    TRACE("Leave NtUserSetProp, ret=%i\n", Ret);
    UserLeave();

    return Ret;
}

/* EOF */
