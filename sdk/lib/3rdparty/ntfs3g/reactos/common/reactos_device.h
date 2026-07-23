/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared NTFS-3G device bridge
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "device.h"
#include "ntfs3g_ros.h"

struct ntfs_device *
Ntfs3gRosCreateDevice(void *Context,
                     const NTFS3G_ROS_DEVICE_OPERATIONS *Operations,
                     uint64_t Length,
                     uint32_t SectorSize);

void
Ntfs3gRosDestroyDevice(struct ntfs_device *Device);
