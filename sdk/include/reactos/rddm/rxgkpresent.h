/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Private full-WDDM shadow-scanout extension
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 */

#pragma once

#include <dispmprt.h>

/*
 * Full WDDM and KMDOD intentionally use different Windows initialization
 * tables.  A full miniport therefore cannot publish
 * DxgkDdiPresentDisplayOnly in DRIVER_INITIALIZATION_DATA.  ReactOS queries
 * this optional interface through the standard DxgkDdiQueryInterface slot
 * when its compatibility CDD needs to publish the CPU-mapped shadow surface.
 * The Windows callback-table layout remains byte-for-byte unchanged.
 */
#define RXGK_SHADOW_PRESENT_INTERFACE_VERSION_1 1

#define RXGK_SHADOW_PRESENT_INTERFACE_GUID_INIT \
    { 0x4f6371cf, 0x9890, 0x480b, \
      { 0xbd, 0x97, 0x2d, 0x8b, 0x6f, 0x82, 0x5e, 0xa2 } }

typedef
NTSTATUS
(APIENTRY *PRXGKDDI_PRESENT_SHADOW)(
    _In_ PVOID Context,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *Present);

typedef struct _RXGK_SHADOW_PRESENT_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PRXGKDDI_PRESENT_SHADOW Present;
} RXGK_SHADOW_PRESENT_INTERFACE, *PRXGK_SHADOW_PRESENT_INTERFACE;

#ifdef _WIN64
C_ASSERT(sizeof(RXGK_SHADOW_PRESENT_INTERFACE) == 0x28);
#else
C_ASSERT(sizeof(RXGK_SHADOW_PRESENT_INTERFACE) == 0x14);
#endif
