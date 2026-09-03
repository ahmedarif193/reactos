/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for the ntdll memcpy and memmove exports
 */

#include "precomp.h"

typedef void *(__cdecl *PMEMMOVE)(void *, const void *, size_t);

static const SIZE_T Lengths[] =
{
    0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33,
    63, 64, 65, 127, 128, 129, 191, 192
};

static VOID
TestCopyFunction(const char *Name, PMEMMOVE Function, BOOL AllowOverlap)
{
    UCHAR Actual[256], Expected[256], Temporary[192];
    ULONG Destination, Source, LengthIndex, Index;
    ULONG DataFailures = 0, ReturnFailures = 0;

    if (Function == NULL)
    {
        skip("ntdll does not export %s\n", Name);
        return;
    }

    for (Destination = 0; Destination < 32; ++Destination)
    {
        for (Source = 0; Source < 32; ++Source)
        {
            for (LengthIndex = 0; LengthIndex < _countof(Lengths); ++LengthIndex)
            {
                SIZE_T Length = Lengths[LengthIndex];
                void *Result;

                if (Destination + Length > sizeof(Actual) ||
                    Source + Length > sizeof(Actual))
                {
                    continue;
                }
                if (!AllowOverlap && Length != 0 &&
                    Destination < Source + Length &&
                    Source < Destination + Length)
                {
                    continue;
                }

                for (Index = 0; Index < sizeof(Actual); ++Index)
                {
                    Actual[Index] = Expected[Index] =
                        (UCHAR)(Index * 37u + Destination * 11u + Source);
                }
                for (Index = 0; Index < Length; ++Index)
                    Temporary[Index] = Expected[Source + Index];
                for (Index = 0; Index < Length; ++Index)
                    Expected[Destination + Index] = Temporary[Index];

                Result = Function(&Actual[Destination], &Actual[Source], Length);
                if (Result != &Actual[Destination])
                    ++ReturnFailures;
                for (Index = 0; Index < sizeof(Actual); ++Index)
                {
                    if (Actual[Index] != Expected[Index])
                    {
                        ++DataFailures;
                        if (DataFailures <= 4)
                        {
                            trace("%s mismatch: dst=%lu src=%lu length=%Iu index=%lu expected=%02x actual=%02x\n",
                                  Name, Destination, Source, Length, Index,
                                  Expected[Index], Actual[Index]);
                        }
                        break;
                    }
                }
            }
        }
    }

    ok(DataFailures == 0, "%s had %lu data failures\n", Name, DataFailures);
    ok(ReturnFailures == 0, "%s had %lu return-value failures\n",
       Name, ReturnFailures);
}

START_TEST(memmove)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    ok(Ntdll != NULL, "GetModuleHandleW failed: %lu\n", GetLastError());
    if (Ntdll == NULL)
        return;

    TestCopyFunction("memmove", (PMEMMOVE)GetProcAddress(Ntdll, "memmove"), TRUE);
    TestCopyFunction("memcpy", (PMEMMOVE)GetProcAddress(Ntdll, "memcpy"), FALSE);
}
