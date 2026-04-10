/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Menu state and hint helpers.
 */

#include "precomp.h"
#include "menustate.h"

typedef struct _DM_MENU_ENTRY
{
    UINT CommandId;
    UINT HintStringId;
    UINT StatusStringId;
} DM_MENU_ENTRY;

static const DM_MENU_ENTRY DmMenuEntries[] =
{
    { IDM_FILE_EXIT,               IDS_HINT_FILE_EXIT,               IDS_STATUS_READY },
    { IDM_ACTION_RESCAN,           IDS_HINT_ACTION_RESCAN,           IDS_STATUS_RESCANNING },
    { IDM_ACTION_REFRESH,          IDS_HINT_ACTION_REFRESH,          IDS_STATUS_REFRESHING },
    { IDM_ACTION_INITIALIZE_DISK,   IDS_HINT_ACTION_INITIALIZE_DISK,  IDS_STATUS_INITIALIZING_DISK },
    { IDM_ACTION_CONVERT_GPT,       IDS_HINT_ACTION_CONVERT_GPT,      IDS_STATUS_CONVERTING_GPT },
    { IDM_ACTION_CONVERT_MBR,       IDS_HINT_ACTION_CONVERT_MBR,      IDS_STATUS_CONVERTING_MBR },
    { IDM_ACTION_CREATE_PARTITION,  IDS_HINT_ACTION_CREATE_PARTITION, IDS_STATUS_CREATING_PARTITION },
    { IDM_ACTION_DELETE_VOLUME,     IDS_HINT_ACTION_DELETE_VOLUME,    IDS_STATUS_DELETING_VOLUME },
    { IDM_ACTION_EXTEND_VOLUME,     IDS_HINT_ACTION_EXTEND_VOLUME,    IDS_STATUS_EXTENDING_VOLUME },
    { IDM_ACTION_SHRINK_VOLUME,     IDS_HINT_ACTION_SHRINK_VOLUME,    IDS_STATUS_SHRINKING_VOLUME },
    { IDM_ACTION_FORMAT,            IDS_HINT_ACTION_FORMAT,           IDS_STATUS_FORMATTING_VOLUME },
    { IDM_ACTION_ASSIGN_LETTER,     IDS_HINT_ACTION_ASSIGN_LETTER,    IDS_STATUS_ASSIGNING_LETTER },
    { IDM_ACTION_REMOVE_LETTER,     IDS_HINT_ACTION_REMOVE_LETTER,    IDS_STATUS_REMOVING_LETTER },
    { IDM_ACTION_CHANGE_MOUNT_PATH, IDS_HINT_ACTION_CHANGE_MOUNT_PATH, IDS_STATUS_CHANGING_MOUNT_PATH },
    { IDM_ACTION_REMOVE_MOUNT_PATH, IDS_HINT_ACTION_REMOVE_MOUNT_PATH, IDS_STATUS_REMOVING_MOUNT_PATH },
    { IDM_ACTION_ONLINE,            IDS_HINT_ACTION_ONLINE,           IDS_STATUS_ONLINING_DISK },
    { IDM_ACTION_OFFLINE,           IDS_HINT_ACTION_OFFLINE,          IDS_STATUS_OFFLINING_DISK },
    { IDM_ACTION_SET_READ_ONLY,     IDS_HINT_ACTION_SET_READ_ONLY,    IDS_STATUS_SETTING_READ_ONLY },
    { IDM_ACTION_CLEAR_READ_ONLY,   IDS_HINT_ACTION_CLEAR_READ_ONLY,  IDS_STATUS_CLEARING_READ_ONLY },
    { IDM_ACTION_MARK_ACTIVE,       IDS_HINT_ACTION_MARK_ACTIVE,      IDS_STATUS_MARKING_ACTIVE },
    { IDM_ACTION_MARK_INACTIVE,     IDS_HINT_ACTION_MARK_INACTIVE,    IDS_STATUS_MARKING_INACTIVE },
    { IDM_ACTION_PROPERTIES,        IDS_HINT_ACTION_PROPERTIES,       IDS_STATUS_PROPERTIES },
    { IDM_VIEW_TOP,                IDS_HINT_VIEW_TOP,                IDS_STATUS_READY },
    { IDM_VIEW_BOTTOM,             IDS_HINT_VIEW_BOTTOM,             IDS_STATUS_READY },
    { IDM_VIEW_DISK_LIST,           IDS_HINT_VIEW_DISK_LIST,          IDS_STATUS_READY },
    { IDM_HELP_ABOUT,               IDS_HINT_HELP_ABOUT,              IDS_STATUS_READY }
};

static const DM_MENU_ENTRY *
DmMenuStateFindEntry(
    _In_ UINT CommandId)
{
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(DmMenuEntries); Index++)
    {
        if (DmMenuEntries[Index].CommandId == CommandId)
            return &DmMenuEntries[Index];
    }

    return NULL;
}

VOID
DmMenuStateInitialize(
    _Out_ PDM_MENU_STATE State)
{
    if (State == NULL)
        return;

    ZeroMemory(State, sizeof(*State));
}

VOID
DmMenuStateSetActionContext(
    _Inout_ PDM_MENU_STATE State,
    _In_ const DM_ACTION_CONTEXT *Context)
{
    if (State == NULL)
        return;

    if (Context != NULL)
    {
        State->ActionContext = *Context;
    }
    else
    {
        ZeroMemory(&State->ActionContext, sizeof(State->ActionContext));
    }
}

VOID
DmMenuStateSetFocus(
    _Inout_ PDM_MENU_STATE State,
    _In_ DM_MENU_FOCUS Focus,
    _In_ UINT SelectedCommandId)
{
    if (State == NULL)
        return;

    State->Focus = Focus;
    State->SelectedCommandId = SelectedCommandId;
}

BOOL
DmMenuStateIsChecked(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId)
{
    if (State == NULL)
        return FALSE;

    switch (CommandId)
    {
        case IDM_VIEW_TOP:
            return State->Focus == DmMenuFocusTop;

        case IDM_VIEW_BOTTOM:
            return State->Focus == DmMenuFocusBottom;

        case IDM_VIEW_DISK_LIST:
            return State->Focus == DmMenuFocusDiskList;

        default:
            return FALSE;
    }
}

BOOL
DmMenuStateIsEnabled(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId)
{
    if (CommandId == IDM_VIEW_TOP ||
        CommandId == IDM_VIEW_BOTTOM ||
        CommandId == IDM_VIEW_DISK_LIST ||
        CommandId == IDM_FILE_EXIT ||
        CommandId == IDM_HELP_ABOUT)
    {
        return TRUE;
    }

    return DmActionIsAvailable(State != NULL ? &State->ActionContext : NULL,
                               CommandId);
}

BOOL
DmMenuStateApplyMenu(
    _In_ const DM_MENU_STATE *State,
    _In_ HMENU hMenu)
{
    UINT Index;

    if (State == NULL || hMenu == NULL)
        return FALSE;

    for (Index = 0; Index < ARRAYSIZE(DmMenuEntries); Index++)
    {
        const DM_MENU_ENTRY *Entry;
        UINT StateFlags;

        Entry = &DmMenuEntries[Index];
        if (DmMenuStateIsEnabled(State, Entry->CommandId))
            StateFlags = MF_ENABLED;
        else
            StateFlags = MF_GRAYED;

        CheckMenuItem(hMenu,
                      Entry->CommandId,
                      MF_BYCOMMAND | (DmMenuStateIsChecked(State, Entry->CommandId) ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(hMenu,
                       Entry->CommandId,
                       MF_BYCOMMAND | StateFlags);
    }

    return TRUE;
}

UINT
DmMenuStateGetHintStringId(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId)
{
    const DM_MENU_ENTRY *Entry;

    UNREFERENCED_PARAMETER(State);

    Entry = DmMenuStateFindEntry(CommandId);
    return (Entry != NULL) ? Entry->HintStringId : IDS_HINT_READY;
}

UINT
DmMenuStateGetStatusStringId(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId)
{
    const DM_MENU_ENTRY *Entry;

    UNREFERENCED_PARAMETER(State);

    Entry = DmMenuStateFindEntry(CommandId);
    return (Entry != NULL) ? Entry->StatusStringId : IDS_STATUS_READY;
}
