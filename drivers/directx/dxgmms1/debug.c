/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Debug helpers for dxgmms1.sys
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#include "dxgmms1_private.h"

/*
 * DXGMMS1_POOL_TAG_SYMBOL
 *
 * Exporting the pool tag via a named symbol allows kernel debuggers
 * to identify pool allocations as belonging to dxgmms1 when the driver
 * image is loaded.
 */
#if DBG
ULONG DxgMmsPoolTag = DXGMMS1_POOL_TAG;
#endif /* DBG */
