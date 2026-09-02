/*
 * PROJECT:     ReactOS softgpu user-mode display driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Empty optional backend for the generic linear WDDM UMD
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>

#include "softgpuum_backend.h"

static CONST SOFTGPUUM_BACKEND SoftGpuUmNoBackend;

CONST SOFTGPUUM_BACKEND *APIENTRY
SoftGpuUmGetBackend(VOID)
{
    return &SoftGpuUmNoBackend;
}
