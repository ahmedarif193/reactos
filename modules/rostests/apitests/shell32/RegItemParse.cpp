/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests parsing shell parsing names of registered and unknown CLSIDs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"

static void
TestParse(PCWSTR Name, BOOL Expected)
{
    PIDLIST_ABSOLUTE pidl = NULL;
    HRESULT hr;

    hr = SHParseDisplayName(Name, NULL, &pidl, 0, NULL);
    if (Expected)
    {
        ok(hr == S_OK, "%s: SHParseDisplayName returned 0x%lx\n", wine_dbgstr_w(Name), hr);
        ok(pidl != NULL, "%s: no PIDL\n", wine_dbgstr_w(Name));
    }
    else
    {
        ok(FAILED(hr), "%s: SHParseDisplayName returned 0x%lx\n", wine_dbgstr_w(Name), hr);
        ok(pidl == NULL, "%s: unexpected PIDL\n", wine_dbgstr_w(Name));
    }
    if (pidl)
        ILFree(pidl);
}

START_TEST(RegItemParse)
{
    HRESULT hr = CoInitialize(NULL);

    TestParse(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", TRUE);
    TestParse(L"::{645FF040-5081-101B-9F08-00AA002F954E}", TRUE);
    TestParse(L"::{8A4D5E6B-2C1F-4B7A-9E3D-6F5A4C3B2A19}", FALSE);

    if (SUCCEEDED(hr))
        CoUninitialize();
}
