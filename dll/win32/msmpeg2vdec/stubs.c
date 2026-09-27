/*
 * PROJECT:     ReactOS Media Foundation decoders
 * FILE:        dll/win32/msmpeg2vdec/stubs.c
 * PURPOSE:     Entry points of a build without FFmpeg
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <windef.h>
#include <winbase.h>
#include <objbase.h>

/* Without FFmpeg there is no decoder to create or to register. */

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}
