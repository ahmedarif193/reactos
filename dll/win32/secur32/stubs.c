#include "precomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(secur32);

SECURITY_STATUS
SEC_ENTRY
DeleteSecurityPackageA(LPSTR pszPackageName)
{
    FIXME("stub\n");
    return STATUS_UNSUCCESSFUL;
}

SECURITY_STATUS
SEC_ENTRY
DeleteSecurityPackageW(LPWSTR pszPackageName)
{
    FIXME("stub\n");
    return STATUS_UNSUCCESSFUL;
}

SECURITY_STATUS
SEC_ENTRY
AddSecurityPackageA(LPSTR pszPackageName, PSECURITY_PACKAGE_OPTIONS pOptions)
{
    FIXME("stub\n");
    return STATUS_UNSUCCESSFUL;
}

SECURITY_STATUS
SEC_ENTRY
AddSecurityPackageW(LPWSTR pszPackageName, PSECURITY_PACKAGE_OPTIONS pOptions)
{
    FIXME("stub\n");
    return STATUS_UNSUCCESSFUL;
}

SECURITY_STATUS
SEC_ENTRY
GetSecurityUserInfo(
    PLUID LogonId,
    ULONG Flags,
    PSecurityUserData *UserInformation)
{
    FIXME("stub\n");
    return STATUS_UNSUCCESSFUL;
}
