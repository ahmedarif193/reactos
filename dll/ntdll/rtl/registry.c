/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
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
