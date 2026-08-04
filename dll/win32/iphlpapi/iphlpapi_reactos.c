/*
 * PROJECT:     ReactOS Networking
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/win32/iphlpapi/iphlpapi_reactos.c
 * PURPOSE:     ReactOS compatibility helpers
 * PROGRAMMERS: Pierre Schweitzer <pierre@reactos.org>
 */

#define IPHLPAPI_DLL_LINKAGE
#include "iphlpapi_private.h"
#include <netioapi.h>

WINE_DEFAULT_DEBUG_CHANNEL(iphlpapi);

#ifdef __REACTOS__
static DWORD get_interface_alias(const GUID *guid, WCHAR *name, ULONG *size)
{
    WCHAR alias[IF_MAX_STRING_SIZE + 1];
    NET_LUID luid;
    DWORD required, status;

    if (!guid || !size) return ERROR_INVALID_PARAMETER;
    if ((status = ConvertInterfaceGuidToLuid(guid, &luid))) return status;
    if ((status = ConvertInterfaceLuidToAlias(&luid, alias, ARRAYSIZE(alias)))) return status;
    required = (wcslen(alias) + 1) * sizeof(WCHAR);
    if (!name || *size < required)
    {
        *size = required;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    CopyMemory(name, alias, required);
    *size = required;
    return ERROR_SUCCESS;
}

DWORD WINAPI NhGetInterfaceNameFromDeviceGuid(const GUID *guid, WCHAR *name, ULONG *size, DWORD unknown4, DWORD unknown5)
{
    UNREFERENCED_PARAMETER(unknown4);
    UNREFERENCED_PARAMETER(unknown5);
    return get_interface_alias(guid, name, size);
}

DWORD WINAPI NhGetInterfaceNameFromGuid(const GUID *guid, WCHAR *name, ULONG *size, DWORD unknown4, DWORD unknown5)
{
    DWORD status;

    UNREFERENCED_PARAMETER(unknown4);
    UNREFERENCED_PARAMETER(unknown5);
    status = get_interface_alias(guid, name, size);
    if (status == ERROR_NOT_FOUND) SetLastError(ERROR_PATH_NOT_FOUND);
    return status;
}

DWORD WINAPI NhGetGuidFromInterfaceName(WCHAR *name, GUID *guid, DWORD unknown3, DWORD unknown4)
{
    NET_LUID luid;
    DWORD status;

    UNREFERENCED_PARAMETER(unknown3);
    UNREFERENCED_PARAMETER(unknown4);
    if (!name || !guid) return ERROR_INVALID_PARAMETER;
    if ((status = ConvertInterfaceAliasToLuid(name, &luid))) return status;
    return ConvertInterfaceLuidToGuid(&luid, guid);
}
#endif
