/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel Transaction Manager system-call entry points
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
NtCreateTransactionManager(
    _Out_ PHANDLE TmHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PUNICODE_STRING LogFileName,
    _In_opt_ ULONG CreateOptions,
    _In_opt_ ULONG CommitStrength)
{
    UNREFERENCED_PARAMETER(TmHandle);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);
    UNREFERENCED_PARAMETER(LogFileName);
    UNREFERENCED_PARAMETER(CreateOptions);
    UNREFERENCED_PARAMETER(CommitStrength);

    /*
     * COMPONENT_KTM is a process-creation security boundary.  Honor it even
     * while the transaction manager itself is not implemented, so a filtered
     * process observes the same denial as Windows and cannot reach future KTM
     * functionality accidentally.
     */
    if (ReadAcquire(&PsGetCurrentProcess()->ComponentFilter) &
        PSP_COMPONENT_FILTER_KTM)
    {
        return STATUS_ACCESS_DENIED;
    }

    return STATUS_NOT_IMPLEMENTED;
}
