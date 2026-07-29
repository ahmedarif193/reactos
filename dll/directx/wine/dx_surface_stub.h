/*
 * Shared helpers for the temporary DX10/DX11/DXGI surface modules.
 */

#pragma once

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

static inline HRESULT dx_surface_unsupported(void)
{
    return DXGI_ERROR_UNSUPPORTED;
}

static inline void dx_zero_pointer(void **object)
{
    if (object)
        *object = NULL;
}
