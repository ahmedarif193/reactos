/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT shared-surface consumer declarations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <windows.h>
#include <reactos/dwmframe.h>

const BYTE *DwmDxGetSurfaceSnapshot(const DWM_WIN *Window);
void DwmDxAcknowledgeSurface(const DWM_WIN *Window);
void DwmDxSweepSurfaces(ULONG FrameSequence);
