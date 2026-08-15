/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only BCM2712 V3D 7.1 discovery
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _RPI5VC4_V3D_H_
#define _RPI5VC4_V3D_H_

#include "rpi5vc4.h"

VOID
Rpi5V3dProbe(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

VP_STATUS
Rpi5V3dQuery(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PRPI5VC4_V3D_INFO Info);

#endif /* _RPI5VC4_V3D_H_ */
