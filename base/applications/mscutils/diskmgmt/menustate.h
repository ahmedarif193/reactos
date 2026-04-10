/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Menu state and hint helpers.
 */

#ifndef DISKMGMT_MENUSTATE_H
#define DISKMGMT_MENUSTATE_H

#include <windef.h>

#include "actions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _DM_MENU_FOCUS
{
    DmMenuFocusNone = 0,
    DmMenuFocusTop,
    DmMenuFocusBottom,
    DmMenuFocusDiskList
} DM_MENU_FOCUS, *PDM_MENU_FOCUS;

typedef struct _DM_MENU_STATE
{
    DM_ACTION_CONTEXT ActionContext;
    DM_MENU_FOCUS Focus;
    UINT SelectedCommandId;
} DM_MENU_STATE, *PDM_MENU_STATE;

VOID
DmMenuStateInitialize(
    _Out_ PDM_MENU_STATE State);

VOID
DmMenuStateSetActionContext(
    _Inout_ PDM_MENU_STATE State,
    _In_ const DM_ACTION_CONTEXT *Context);

VOID
DmMenuStateSetFocus(
    _Inout_ PDM_MENU_STATE State,
    _In_ DM_MENU_FOCUS Focus,
    _In_ UINT SelectedCommandId);

BOOL
DmMenuStateIsChecked(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId);

BOOL
DmMenuStateIsEnabled(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId);

BOOL
DmMenuStateApplyMenu(
    _In_ const DM_MENU_STATE *State,
    _In_ HMENU hMenu);

UINT
DmMenuStateGetHintStringId(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId);

UINT
DmMenuStateGetStatusStringId(
    _In_ const DM_MENU_STATE *State,
    _In_ UINT CommandId);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_MENUSTATE_H */
