/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTDLL registry compatibility exports
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtOpenKeyEx(PHANDLE KeyHandle,
            ACCESS_MASK DesiredAccess,
            POBJECT_ATTRIBUTES ObjectAttributes,
            ULONG OpenOptions)
{
    if (OpenOptions & ~REG_OPTION_OPEN_LINK)
        return STATUS_NOT_SUPPORTED;

    return NtOpenKey(KeyHandle, DesiredAccess, ObjectAttributes);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtOpenKeyTransactedEx(PHANDLE KeyHandle,
                      ACCESS_MASK DesiredAccess,
                      POBJECT_ATTRIBUTES ObjectAttributes,
                      ULONG OpenOptions,
                      HANDLE TransactionHandle)
{
    if (TransactionHandle)
        return STATUS_NOT_SUPPORTED;

    return NtOpenKeyEx(KeyHandle, DesiredAccess, ObjectAttributes, OpenOptions);
}
