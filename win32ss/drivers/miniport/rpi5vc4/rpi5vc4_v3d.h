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

BOOLEAN
NTAPI
Rpi5V3dInterrupt(
    _Inout_ PVOID HwDeviceExtension);

VOID
Rpi5V3dInitializeInterrupts(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

VP_STATUS
Rpi5V3dQuery(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PRPI5VC4_V3D_INFO Info);

VP_STATUS
Rpi5V3dRunSelfTest(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PRPI5VC4_V3D_SELFTEST Result);

VP_STATUS
Rpi5V3dRenderClear(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PRPI5VC4_V3D_CLEAR_REQUEST Request,
    _Out_writes_bytes_(ResultBufferLength) PRPI5VC4_V3D_CLEAR_RESULT Result,
    _In_ ULONG ResultBufferLength,
    _Out_ PULONG BytesReturned);

VP_STATUS
Rpi5V3dRenderTriangle(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PRPI5VC4_V3D_TRIANGLE_REQUEST Request,
    _Out_writes_bytes_(ResultBufferLength) PRPI5VC4_V3D_TRIANGLE_RESULT Result,
    _In_ ULONG ResultBufferLength,
    _Out_ PULONG BytesReturned);

VP_STATUS
Rpi5V3dRenderBatch(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestBufferLength) PRPI5VC4_V3D_BATCH_REQUEST Request,
    _In_ ULONG RequestBufferLength,
    _Out_writes_bytes_(ResultBufferLength) PRPI5VC4_V3D_BATCH_RESULT Result,
    _In_ ULONG ResultBufferLength,
    _Out_ PULONG BytesReturned);

VP_STATUS
Rpi5V3dUploadTexture(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestBufferLength)
        PRPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST Request,
    _In_ ULONG RequestBufferLength,
    _Out_ PRPI5VC4_V3D_TEXTURE_UPLOAD_RESULT Result,
    _Out_ PULONG BytesReturned);

VP_STATUS
Rpi5V3dPresentGdi(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestBufferLength)
        PRPI5VC4_GDI_PRESENT_REQUEST Request,
    _In_ ULONG RequestBufferLength,
    _Out_ PRPI5VC4_GDI_PRESENT_RESULT Result,
    _Out_ PULONG BytesReturned);

VP_STATUS
Rpi5V3dRenderGraph(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(RequestBufferLength)
        PRPI5VC4_V3D_RENDER_GRAPH_REQUEST Request,
    _In_ ULONG RequestBufferLength,
    _Out_ PRPI5VC4_V3D_RENDER_GRAPH_RESULT Result,
    _Out_ PULONG BytesReturned);

VP_STATUS
Rpi5V3dReadGraph(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ PRPI5VC4_V3D_READ_GRAPH_REQUEST Request,
    _Out_writes_bytes_(ResultBufferLength)
        PRPI5VC4_V3D_READ_GRAPH_RESULT Result,
    _In_ ULONG ResultBufferLength,
    _Out_ PULONG BytesReturned);

#endif /* _RPI5VC4_V3D_H_ */
