/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/labels.h
 * PURPOSE:     Type and status label helpers for the Disk Management UI
 */

#ifndef DISKMGMT_LABELS_H
#define DISKMGMT_LABELS_H

#include <windef.h>
#include <winioctl.h>
#include <ntddstor.h>
#include <guiddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _DM_HEALTH_STATE
{
    DmHealthUnknown = 0,
    DmHealthHealthy,
    DmHealthSick,
    DmHealthUnavailable
} DM_HEALTH_STATE, *PDM_HEALTH_STATE;

typedef enum _DM_ACCESS_STATE
{
    DmAccessUnknown = 0,
    DmAccessOnline,
    DmAccessOffline,
    DmAccessNoMedia
} DM_ACCESS_STATE, *PDM_ACCESS_STATE;

VOID
DmGetPartitionStyleLabel(
    _In_ PARTITION_STYLE Style,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetBusTypeLabel(
    _In_ STORAGE_BUS_TYPE BusType,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetMbrRoleLabel(
    _In_ BOOLEAN IsLogical,
    _In_ BOOLEAN IsContainer,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetMbrTypeLabel(
    _In_ UCHAR PartitionType,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetGptTypeLabel(
    _In_ const GUID *PartitionType,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetHealthLabel(
    _In_ DM_HEALTH_STATE Health,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetAccessStateLabel(
    _In_ DM_ACCESS_STATE State,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetYesNoLabel(
    _In_ BOOL Value,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

VOID
DmGetBootSystemLabel(
    _In_ BOOL IsBoot,
    _In_ BOOL IsSystem,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_LABELS_H */
