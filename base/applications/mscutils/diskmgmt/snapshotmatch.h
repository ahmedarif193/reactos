/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pure snapshot matching helpers shared by diskmgmt and tests.
 */

#ifndef DISKMGMT_SNAPSHOTMATCH_H
#define DISKMGMT_SNAPSHOTMATCH_H

#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

VOID
DmSnapshotMatchVolumesToRegions(
    _Inout_ PDM_SNAPSHOT Snapshot);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_SNAPSHOTMATCH_H */
