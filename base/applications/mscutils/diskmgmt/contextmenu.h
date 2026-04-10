/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        base/applications/mscutils/diskmgmt/contextmenu.h
 * PURPOSE:     Popup menu helpers for Disk Management.
 */

#ifndef DISKMGMT_CONTEXTMENU_H
#define DISKMGMT_CONTEXTMENU_H

#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _DM_CONTEXT_MENU_KIND
{
    DmContextMenuNone = 0,
    DmContextMenuBackground,
    DmContextMenuDisk,
    DmContextMenuPartition,
    DmContextMenuUnallocated,
    DmContextMenuVolume
} DM_CONTEXT_MENU_KIND;

HMENU
DmCreateContextMenu(
    _In_opt_ const DM_SNAPSHOT *Snapshot,
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region,
    _In_opt_ const DM_VOLUME *Volume);

VOID
DmDestroyContextMenu(
    _In_opt_ HMENU Menu);

UINT
DmGetContextMenuDefaultCommand(
    _In_ DM_CONTEXT_MENU_KIND Kind);

DM_CONTEXT_MENU_KIND
DmGetContextMenuKind(
    _In_opt_ const DM_DISK *Disk,
    _In_opt_ const DM_REGION *Region,
    _In_opt_ const DM_VOLUME *Volume);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_CONTEXTMENU_H */
