#include "k32_vista.h"

/***********************************************************************
 *           GetProcessGroupAffinity   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetProcessGroupAffinity( HANDLE process, USHORT *count, USHORT *array )
{
    ULONG length = 0;
    NTSTATUS status;

    if (!count)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }

    status = NtQueryInformationProcess(process, ProcessGroupInformation, array, *count * sizeof(*array), &length);
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) *count = (USHORT)(length / sizeof(*array));
    if (!NT_SUCCESS(status))
    {
        BaseSetLastNTError(status);
        return FALSE;
    }

    return TRUE;
}
