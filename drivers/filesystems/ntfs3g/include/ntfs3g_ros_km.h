/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NTFS-3G kernel host interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntifs.h>
#include <ntfs3g_ros.h>

typedef NTFS3G_ROS_VOLUME NTFS3G_ROS_KM_VOLUME;
typedef NTFS3G_ROS_KM_VOLUME *PNTFS3G_ROS_KM_VOLUME;

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
Ntfs3gRosInitializeKernelLibrary(void);

void
Ntfs3gRosUninitializeKernelLibrary(void);

NTSTATUS
Ntfs3gRosMountDevice(PDEVICE_OBJECT DeviceObject,
                     int ReadOnly,
                     PNTFS3G_ROS_KM_VOLUME *Volume);

NTSTATUS
Ntfs3gRosUnmountDevice(PNTFS3G_ROS_KM_VOLUME Volume);

NTSTATUS
Ntfs3gRosStatusFromError(int Error);

NTSTATUS
Ntfs3gRosOpenUnicodeFile(PNTFS3G_ROS_KM_VOLUME Volume,
                         PCUNICODE_STRING Path,
                         NTFS3G_ROS_FILE **File);

#ifdef __cplusplus
}
#endif
