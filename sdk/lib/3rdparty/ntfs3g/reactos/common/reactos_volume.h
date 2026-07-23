/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private shared NTFS-3G volume layout
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stdint.h>

#include "volume.h"

struct _NTFS3G_ROS_VOLUME
{
    ntfs_volume *Native;
    uint64_t DeviceLength;
    uint64_t FreeClusterCount;
    uint64_t SerialNumber;
};
