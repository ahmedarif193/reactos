/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Command/action dispatch helpers.
 */

#ifndef DISKMGMT_ACTIONS_H
#define DISKMGMT_ACTIONS_H

#include <windef.h>

#include "snapshot.h"
#include "resource.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DM_ACTION_CONTEXT
{
    HWND hWnd;
    HWND hStatusBar;
    PDM_SNAPSHOT Snapshot;
    PDM_DISK Disk;
    PDM_VOLUME Volume;
    PDM_REGION Region;
} DM_ACTION_CONTEXT, *PDM_ACTION_CONTEXT;

typedef struct _DM_ACTION_DESCRIPTOR
{
    UINT CommandId;
    UINT VerbStringId;
    UINT StatusStringId;
} DM_ACTION_DESCRIPTOR, *PDM_ACTION_DESCRIPTOR;

const DM_ACTION_DESCRIPTOR *
DmActionGetDescriptor(
    _In_ UINT CommandId);

UINT
DmActionGetVerbStringId(
    _In_ UINT CommandId);

UINT
DmActionGetStatusStringId(
    _In_ UINT CommandId);

BOOL
DmActionIsAvailable(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId);

BOOL
DmActionMutatesSnapshot(
    _In_ UINT CommandId);

BOOL
DmActionExecute(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId);

VOID
DmActionBuildVerbText(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmActionShowStubMessage(
    _In_ const DM_ACTION_CONTEXT *Context,
    _In_ UINT CommandId);

BOOL
DmActionHasPendingCreate(VOID);

BOOL
DmActionProcessPendingCreate(
    _In_opt_ HWND hWnd,
    _In_opt_ HWND hStatusBar);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_ACTIONS_H */
