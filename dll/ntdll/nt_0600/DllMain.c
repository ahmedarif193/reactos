#include "ntdll_vista.h"

NTSTATUS
NTAPI
RtlpInitializeLocaleTable(VOID);

BOOL
WINAPI
DllMain(HANDLE hDll,
        DWORD dwReason,
        LPVOID lpReserved)
{
    NTSTATUS Status;

    if (dwReason == DLL_PROCESS_ATTACH)
    {
        LdrDisableThreadCalloutsForDll(hDll);
        Status = RtlpInitializeLocaleTable();
        if (!NT_SUCCESS(Status))
            return FALSE;
    }
    return TRUE;
}
