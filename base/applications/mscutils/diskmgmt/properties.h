/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/properties.h
 * PURPOSE:     Read-only properties helpers for the Disk Management UI
 */

#ifndef DISKMGMT_PROPERTIES_H
#define DISKMGMT_PROPERTIES_H

#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

BOOL
DmBuildDiskPropertiesString(
    _In_ const DM_DISK *Disk,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

BOOL
DmBuildVolumePropertiesString(
    _In_ const DM_VOLUME *Volume,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

BOOL
DmShowDiskProperties(
    _In_opt_ HWND hWndOwner,
    _In_ const DM_DISK *Disk);

BOOL
DmShowVolumeProperties(
    _In_opt_ HWND hWndOwner,
    _In_ const DM_VOLUME *Volume);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_PROPERTIES_H */
