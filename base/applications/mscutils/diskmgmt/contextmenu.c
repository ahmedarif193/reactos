/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        base/applications/mscutils/diskmgmt/contextmenu.c
 * PURPOSE:     Popup menu helpers for Disk Management.
 */

#include "precomp.h"
#include "contextmenu.h"

typedef struct _DM_MENU_COMMAND
{
    UINT CmdId;
    UINT Flags;
    PCWSTR Text;
} DM_MENU_COMMAND, *PDM_MENU_COMMAND;

static BOOL DmIsPartitionRegion(_In_opt_ const DM_REGION *Region);
static BOOL DmIsFreeRegion(_In_opt_ const DM_REGION *Region);
static BOOL DmCanRunCommand(_In_ UINT CommandId,
                            _In_opt_ const DM_DISK *Disk,
                            _In_opt_ const DM_REGION *Region,
                            _In_opt_ const DM_VOLUME *Volume);
static UINT DmGetCommandFlags(_In_ DM_CONTEXT_MENU_KIND Kind,
                              _In_ UINT CommandId,
                              _In_opt_ const DM_DISK *Disk,
                              _In_opt_ const DM_REGION *Region,
                              _In_opt_ const DM_VOLUME *Volume);
static VOID DmAppendCommandIfVisible(_In_ HMENU Menu,
                                     _In_ DM_CONTEXT_MENU_KIND Kind,
                                     _In_ UINT CommandId,
                                     _In_ PCWSTR Text,
                                     _In_opt_ const DM_DISK *Disk,
                                     _In_opt_ const DM_REGION *Region,
                                     _In_opt_ const DM_VOLUME *Volume);
static VOID DmAppendSeparatorIfNeeded(_In_ HMENU Menu);
static PCWSTR DmGetCreateMenuText(_In_opt_ const DM_DISK *Disk,
                                  _In_opt_ const DM_REGION *Region);
static PCWSTR DmGetDeleteMenuText(_In_opt_ const DM_VOLUME *Volume);

static VOID
DmAppendCommandIfVisible(
    _In_ HMENU Menu,
    _In_ DM_CONTEXT_MENU_KIND Kind,
    _In_ UINT CommandId,
    _In_ PCWSTR Text,
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region,
    _In_opt_ const DM_VOLUME *Volume)
{
    UINT Flags;

    Flags = DmGetCommandFlags(Kind, CommandId, Disk, Region, Volume);
    if (Flags == 0)
        return;

    AppendMenuW(Menu, Flags, CommandId, Text);
}

static VOID
DmAppendSeparatorIfNeeded(
    _In_ HMENU Menu)
{
    AppendMenuW(Menu,
                MF_SEPARATOR,
                0,
                NULL);
}

static PCWSTR
DmGetCreateMenuText(
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region)
{
    if (Region != NULL && Region->Type == DmRegionFree)
    {
        if (Region->IsLogical)
            return L"Create &Logical Drive...";

        if (Disk != NULL && Disk->PartitionStyle == PARTITION_STYLE_GPT)
            return L"Create &Volume...";
    }

    return L"Cre&ate Partition...";
}

static PCWSTR
DmGetDeleteMenuText(
    _In_opt_ const DM_VOLUME *Volume)
{
    return (Volume != NULL) ? L"De&lete Volume" : L"Delete &Partition";
}

static BOOL
DmIsPartitionRegion(
    _In_opt_ const DM_REGION *Region)
{
    return (Region != NULL && Region->Type == DmRegionPartition);
}

static BOOL
DmIsFreeRegion(
    _In_opt_ const DM_REGION *Region)
{
    return (Region != NULL && Region->Type == DmRegionFree);
}

static BOOL
DmCanRunCommand(
    _In_ UINT CommandId,
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region,
    _In_opt_ const DM_VOLUME *Volume)
{
    DM_ACTION_CONTEXT ActionContext;

    ZeroMemory(&ActionContext, sizeof(ActionContext));
    ActionContext.Disk = (PDM_DISK)Disk;
    ActionContext.Region = (PDM_REGION)Region;
    ActionContext.Volume = (PDM_VOLUME)Volume;

    if (ActionContext.Volume != NULL)
    {
        if (ActionContext.Disk == NULL)
            ActionContext.Disk = ActionContext.Volume->Disk;

        if (ActionContext.Region == NULL)
            ActionContext.Region = ActionContext.Volume->Region;
    }

    return DmActionIsAvailable(&ActionContext, CommandId);
}

static UINT
DmGetCommandFlags(
    _In_ DM_CONTEXT_MENU_KIND Kind,
    _In_ UINT CommandId,
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region,
    _In_opt_ const DM_VOLUME *Volume)
{
    switch (CommandId)
    {
        case IDM_ACTION_REFRESH:
            return MF_STRING;

        case IDM_ACTION_RESCAN:
            return (Kind == DmContextMenuBackground || Kind == DmContextMenuDisk) ? MF_STRING : 0;

        case IDM_ACTION_INITIALIZE_DISK:
            return (Kind == DmContextMenuDisk && DmCanRunCommand(CommandId, Disk, Region, Volume)) ? MF_STRING : (Kind == DmContextMenuDisk ? (MF_STRING | MF_GRAYED) : 0);

        case IDM_ACTION_CONVERT_GPT:
        case IDM_ACTION_CONVERT_MBR:
            return (Kind == DmContextMenuDisk && DmCanRunCommand(CommandId, Disk, Region, Volume)) ? MF_STRING : (Kind == DmContextMenuDisk ? (MF_STRING | MF_GRAYED) : 0);

        case IDM_ACTION_CREATE_PARTITION:
            return (Kind == DmContextMenuUnallocated && DmCanRunCommand(CommandId, Disk, Region, Volume)) ? MF_STRING : 0;

        case IDM_ACTION_DELETE_VOLUME:
            if (Kind == DmContextMenuPartition)
            {
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            }

            if (Kind == DmContextMenuVolume)
            {
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            }

            return 0;

        case IDM_ACTION_EXTEND_VOLUME:
            if (Kind == DmContextMenuPartition)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            if (Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_SHRINK_VOLUME:
            if (Kind == DmContextMenuPartition)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            if (Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_FORMAT:
            if (Kind == DmContextMenuPartition)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            if (Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_ASSIGN_LETTER:
            if (Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_REMOVE_LETTER:
            if (Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_CHANGE_MOUNT_PATH:
            if (Kind == DmContextMenuPartition || Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_REMOVE_MOUNT_PATH:
            if (Kind == DmContextMenuPartition || Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_ONLINE:
            return (Kind == DmContextMenuDisk && DmCanRunCommand(CommandId, Disk, Region, Volume)) ? MF_STRING : (Kind == DmContextMenuDisk ? (MF_STRING | MF_GRAYED) : 0);

        case IDM_ACTION_OFFLINE:
            return (Kind == DmContextMenuDisk && DmCanRunCommand(CommandId, Disk, Region, Volume)) ? MF_STRING : (Kind == DmContextMenuDisk ? (MF_STRING | MF_GRAYED) : 0);

        case IDM_ACTION_SET_READ_ONLY:
            return (Kind == DmContextMenuDisk && DmCanRunCommand(CommandId, Disk, Region, Volume)) ? MF_STRING : (Kind == DmContextMenuDisk ? (MF_STRING | MF_GRAYED) : 0);

        case IDM_ACTION_CLEAR_READ_ONLY:
            return (Kind == DmContextMenuDisk && DmCanRunCommand(CommandId, Disk, Region, Volume)) ? MF_STRING : (Kind == DmContextMenuDisk ? (MF_STRING | MF_GRAYED) : 0);

        case IDM_ACTION_MARK_ACTIVE:
            if (Kind == DmContextMenuPartition)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            if (Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_MARK_INACTIVE:
            if (Kind == DmContextMenuPartition)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            if (Kind == DmContextMenuVolume)
                return DmCanRunCommand(CommandId, Disk, Region, Volume) ? MF_STRING : (MF_STRING | MF_GRAYED);
            return 0;

        case IDM_ACTION_PROPERTIES:
            return (Kind == DmContextMenuBackground || Kind == DmContextMenuUnallocated) ? 0 : MF_STRING;

        case IDM_HELP_ABOUT:
            return (Kind == DmContextMenuBackground) ? MF_STRING : 0;

        default:
            return 0;
    }
}

DM_CONTEXT_MENU_KIND
DmGetContextMenuKind(
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region,
    _In_opt_ const DM_VOLUME *Volume)
{
    if (Volume != NULL)
    {
        return DmContextMenuVolume;
    }

    if (DmIsPartitionRegion(Region))
    {
        return DmContextMenuPartition;
    }

    if (DmIsFreeRegion(Region))
    {
        return DmContextMenuUnallocated;
    }

    if (Disk != NULL)
    {
        return DmContextMenuDisk;
    }

    return DmContextMenuBackground;
}

static HMENU
DmBuildBackgroundMenu(VOID)
{
    HMENU Menu;

    Menu = CreatePopupMenu();
    if (Menu == NULL)
        return NULL;

    DmAppendCommandIfVisible(Menu, DmContextMenuBackground, IDM_ACTION_REFRESH, L"&Refresh", NULL, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuBackground, IDM_ACTION_RESCAN, L"Res&can Disks", NULL, NULL, NULL);
    DmAppendSeparatorIfNeeded(Menu);
    DmAppendCommandIfVisible(Menu, DmContextMenuBackground, IDM_HELP_ABOUT, L"&About Disk Management", NULL, NULL, NULL);

    return Menu;
}

static HMENU
DmBuildDiskMenu(
    _In_opt_ const DM_DISK *Disk)
{
    HMENU Menu;

    Menu = CreatePopupMenu();
    if (Menu == NULL)
        return NULL;

    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_REFRESH, L"&Refresh", Disk, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_RESCAN, L"Res&can Disks", Disk, NULL, NULL);
    DmAppendSeparatorIfNeeded(Menu);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_INITIALIZE_DISK, L"&Initialize Disk...", Disk, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_CONVERT_GPT, L"Convert to &GPT Disk", Disk, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_CONVERT_MBR, L"Convert to &MBR Disk", Disk, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_ONLINE, L"&Online", Disk, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_OFFLINE, L"O&ffline", Disk, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_SET_READ_ONLY, L"Set &Read-only", Disk, NULL, NULL);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_CLEAR_READ_ONLY, L"C&lear Read-only", Disk, NULL, NULL);
    DmAppendSeparatorIfNeeded(Menu);
    DmAppendCommandIfVisible(Menu, DmContextMenuDisk, IDM_ACTION_PROPERTIES, L"&Properties", Disk, NULL, NULL);
    return Menu;
}

static HMENU
DmBuildPartitionMenu(
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region)
{
    HMENU Menu;
    BOOL IsFreeRegion;
    const DM_VOLUME *Volume;

    Menu = CreatePopupMenu();
    if (Menu == NULL)
        return NULL;

    IsFreeRegion = DmIsFreeRegion(Region);
    Volume = (!IsFreeRegion && Region != NULL) ? Region->Volume : NULL;
    DmAppendCommandIfVisible(Menu, IsFreeRegion ? DmContextMenuUnallocated : DmContextMenuPartition, IDM_ACTION_REFRESH, L"&Refresh", Disk, Region, NULL);
    DmAppendSeparatorIfNeeded(Menu);
    DmAppendCommandIfVisible(Menu,
                             IsFreeRegion ? DmContextMenuUnallocated : DmContextMenuPartition,
                             IsFreeRegion ? IDM_ACTION_CREATE_PARTITION : IDM_ACTION_DELETE_VOLUME,
                             IsFreeRegion ? DmGetCreateMenuText(Disk, Region) : DmGetDeleteMenuText(Volume),
                             Disk,
                             Region,
                             Volume);
    if (!IsFreeRegion)
    {
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_EXTEND_VOLUME, L"E&xtend Volume...", Disk, Region, Volume);
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_SHRINK_VOLUME, L"Sh&rink Volume...", Disk, Region, Volume);
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_FORMAT, L"&Format...", Disk, Region, Volume);
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_CHANGE_MOUNT_PATH, L"Change Mount &Path...", Disk, Region, Volume);
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_REMOVE_MOUNT_PATH, L"Remove Mount Pat&h", Disk, Region, Volume);
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_MARK_ACTIVE, L"Mark Partition &Active", Disk, Region, NULL);
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_MARK_INACTIVE, L"Mark Partition &Inactive", Disk, Region, NULL);
        DmAppendSeparatorIfNeeded(Menu);
        DmAppendCommandIfVisible(Menu, DmContextMenuPartition, IDM_ACTION_PROPERTIES, L"&Properties", Disk, Region, NULL);
    }
    return Menu;
}

static HMENU
DmBuildVolumeMenu(
    _In_opt_ const DM_VOLUME *Volume)
{
    HMENU Menu;

    Menu = CreatePopupMenu();
    if (Menu == NULL)
        return NULL;

    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_REFRESH, L"&Refresh", NULL, NULL, Volume);
    DmAppendSeparatorIfNeeded(Menu);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_DELETE_VOLUME, L"De&lete Volume", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_EXTEND_VOLUME, L"E&xtend Volume...", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_SHRINK_VOLUME, L"Sh&rink Volume...", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_FORMAT, L"&Format...", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_ASSIGN_LETTER, L"Assi&gn Drive Letter", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_REMOVE_LETTER, L"&Remove Drive Letter", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_CHANGE_MOUNT_PATH, L"Change Mount &Path...", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_REMOVE_MOUNT_PATH, L"Remove Mount Pat&h", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_MARK_ACTIVE, L"Mark Partition &Active", NULL, NULL, Volume);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_MARK_INACTIVE, L"Mark Partition &Inactive", NULL, NULL, Volume);
    DmAppendSeparatorIfNeeded(Menu);
    DmAppendCommandIfVisible(Menu, DmContextMenuVolume, IDM_ACTION_PROPERTIES, L"&Properties", NULL, NULL, Volume);
    return Menu;
}

HMENU
DmCreateContextMenu(
    _In_opt_ const DM_SNAPSHOT *Snapshot,
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region,
    _In_opt_ const DM_VOLUME *Volume)
{
    DM_CONTEXT_MENU_KIND Kind;

    UNREFERENCED_PARAMETER(Snapshot);

    Kind = DmGetContextMenuKind(Disk, Region, Volume);
    switch (Kind)
    {
        case DmContextMenuBackground:
            return DmBuildBackgroundMenu();

        case DmContextMenuDisk:
            return DmBuildDiskMenu(Disk);

        case DmContextMenuPartition:
        case DmContextMenuUnallocated:
            return DmBuildPartitionMenu(Disk, Region);

        case DmContextMenuVolume:
            return DmBuildVolumeMenu(Volume);

        default:
            return NULL;
    }
}

VOID
DmDestroyContextMenu(
    _In_opt_ HMENU Menu)
{
    if (Menu != NULL)
    {
        DestroyMenu(Menu);
    }
}

UINT
DmGetContextMenuDefaultCommand(
    _In_ DM_CONTEXT_MENU_KIND Kind)
{
    switch (Kind)
    {
        case DmContextMenuBackground:
            return IDM_ACTION_REFRESH;

        case DmContextMenuDisk:
            return IDM_ACTION_PROPERTIES;

        case DmContextMenuPartition:
            return IDM_ACTION_PROPERTIES;

        case DmContextMenuUnallocated:
            return IDM_ACTION_CREATE_PARTITION;

        case DmContextMenuVolume:
            return IDM_ACTION_PROPERTIES;

        default:
            return 0;
    }
}
