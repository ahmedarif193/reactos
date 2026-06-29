/*
 * PROJECT:     ReactOS display stack API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 */

#ifndef _DISPLAY_APITEST_PRECOMP_H_
#define _DISPLAY_APITEST_PRECOMP_H_

#include <apitest.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <d3dkmthk.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef MAX_ENUM_ADAPTERS
#define MAX_ENUM_ADAPTERS 16
typedef struct _D3DKMT_ADAPTERINFO
{
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;
    ULONG NumOfSources;
    BOOL bPrecisePresentRegionsPreferred;
} D3DKMT_ADAPTERINFO;

typedef struct _D3DKMT_ENUMADAPTERS
{
    ULONG NumAdapters;
    D3DKMT_ADAPTERINFO Adapters[MAX_ENUM_ADAPTERS];
} D3DKMT_ENUMADAPTERS;
#endif

typedef NTSTATUS (APIENTRY *PFN_D3DKMTEnumAdapters)(D3DKMT_ENUMADAPTERS *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTOpenAdapterFromGdiDisplayName)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *);
typedef NTSTATUS (APIENTRY *PFN_D3DKMTCloseAdapter)(const D3DKMT_CLOSEADAPTER *);
typedef HRESULT (WINAPI *PFN_DwmIsCompositionEnabled)(BOOL *);

static inline FARPROC
LoadDisplayProc(_In_z_ LPCWSTR DllName, _In_z_ LPCSTR ProcName)
{
    HMODULE Module = GetModuleHandleW(DllName);
    if (!Module)
        Module = LoadLibraryW(DllName);
    if (!Module)
        return NULL;
    return GetProcAddress(Module, ProcName);
}

#endif /* _DISPLAY_APITEST_PRECOMP_H_ */
