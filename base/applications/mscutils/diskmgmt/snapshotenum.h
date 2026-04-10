/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pure disk-enumeration parsing helpers shared by diskmgmt and tests.
 */

#ifndef DISKMGMT_SNAPSHOTENUM_H
#define DISKMGMT_SNAPSHOTENUM_H

#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

BOOL
DmSnapshotEnumParsePhysicalDriveNumber(
    _In_ PCWSTR Name,
    _Out_ PULONG DiskNumber);

BOOL
DmSnapshotEnumParseDiskNumbers(
    _In_ PCWSTR DeviceList,
    _Outptr_result_buffer_maybenull_(*DiskCount) PULONG *DiskNumbers,
    _Out_ PULONG DiskCount);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_SNAPSHOTENUM_H */
