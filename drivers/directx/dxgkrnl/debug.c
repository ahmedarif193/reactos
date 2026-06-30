/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Debug and tracing helpers implementation
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 */

#include "dxgkrnl_private.h"

/*
 * DxgkDebugInit
 *
 * Called from DriverEntry.  Performs any one-time debug/trace
 * initialisation needed by the driver.  Currently a no-op; retained
 * for future extension (e.g. WPP tracing initialisation).
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
NTAPI
DxgkDebugInit(VOID)
{
    DXGKRNL_TRACE("DxgkDebugInit: dxgkrnl debug layer ready\n");
}
