/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL Unicode search services
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

PWCHAR
NTAPI
RtlFindUnicodeSubstring(
    _In_ PCUNICODE_STRING String,
    _In_ PCUNICODE_STRING SubString,
    _In_ BOOLEAN CaseInsensitive)
{
    USHORT Offset;
    UNICODE_STRING Candidate;

    if ((String == NULL) || (SubString == NULL) || (String->Buffer == NULL) || (SubString->Buffer == NULL) || (SubString->Length > String->Length))
        return NULL;

    Candidate.Length = SubString->Length;
    Candidate.MaximumLength = SubString->Length;
    for (Offset = 0; Offset <= String->Length - SubString->Length; Offset += sizeof(WCHAR))
    {
        Candidate.Buffer = (PWCHAR)((PUCHAR)String->Buffer + Offset);
        if (RtlEqualUnicodeString(&Candidate, SubString, CaseInsensitive))
            return Candidate.Buffer;
    }

    return NULL;
}
