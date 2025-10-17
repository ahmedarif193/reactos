#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

PUNICODE_STRING
K32VistaAnsiToStaticUnicode(LPCSTR Name)
{
    PUNICODE_STRING StaticString;
    ANSI_STRING AnsiString;
    NTSTATUS Status;

    if (!Name)
        return NULL;

    StaticString = &NtCurrentTeb()->StaticUnicodeString;

    Status = RtlInitAnsiStringEx(&AnsiString, Name);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_BUFFER_OVERFLOW)
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
        else
            BaseSetLastNTError(Status);
        return NULL;
    }

    Status = RtlAnsiStringToUnicodeString(StaticString, &AnsiString, FALSE);
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_BUFFER_OVERFLOW)
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
        else
            BaseSetLastNTError(Status);
        return NULL;
    }

    return StaticString;
}
