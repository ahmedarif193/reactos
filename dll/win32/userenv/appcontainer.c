/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     AppContainer profile functions
 * COPYRIGHT:   Adapted from Wine dlls/userenv/userenv_main.c
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

HRESULT
WINAPI
DeriveAppContainerSidFromAppContainerName(PCWSTR pszAppContainerName,
                                          PSID *ppsidAppContainerSid)
{
    DPRINT1("DeriveAppContainerSidFromAppContainerName(%S %p) not supported\n",
            pszAppContainerName, ppsidAppContainerSid);

    if (ppsidAppContainerSid == NULL)
        return E_INVALIDARG;

    *ppsidAppContainerSid = NULL;

    return E_NOTIMPL;
}

HRESULT
WINAPI
CreateAppContainerProfile(PCWSTR pszAppContainerName,
                          PCWSTR pszDisplayName,
                          PCWSTR pszDescription,
                          PSID_AND_ATTRIBUTES pCapabilities,
                          DWORD dwCapabilityCount,
                          PSID *ppSidAppContainerSid)
{
    DPRINT1("CreateAppContainerProfile(%S %S %S %p %lu %p) not supported\n",
            pszAppContainerName, pszDisplayName, pszDescription,
            pCapabilities, dwCapabilityCount, ppSidAppContainerSid);

    if (ppSidAppContainerSid != NULL)
        *ppSidAppContainerSid = NULL;

    return E_NOTIMPL;
}
