#include <win32k.h>

/*
 * @implemented
 * https://learn.microsoft.com/en-us/windows/win32/api/winddi/nf-winddi-enggetlasterror
 */
ULONG
APIENTRY
EngGetLastError(VOID)
{
    PTEB pTeb = NtCurrentTeb();
    return (pTeb ? pTeb->LastErrorValue : ERROR_SUCCESS);
}

/*
 * @implemented
 * https://learn.microsoft.com/en-us/windows/win32/api/winddi/nf-winddi-engsetlasterror
 */
VOID
APIENTRY
EngSetLastError(_In_ ULONG iError)
{
    PTEB pTeb = NtCurrentTeb();
    if (pTeb)
    {
        pTeb->LastErrorValue = iError;
#ifdef _WIN64
        if (PsGetProcessWow64Process(PsGetCurrentProcess()))
        {
            PTEB32 pTeb32 = (PTEB32)((PUCHAR)pTeb + ROUND_TO_PAGES(sizeof(TEB)));
            pTeb32->LastErrorValue = iError;
        }
#endif
    }
}

VOID
FASTCALL
SetLastNtError(_In_ NTSTATUS Status)
{
    EngSetLastError(RtlNtStatusToDosError(Status));
}
