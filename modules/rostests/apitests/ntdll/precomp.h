#ifndef _NTDLL_APITEST_PRECOMP_H_
#define _NTDLL_APITEST_PRECOMP_H_

#include <stdio.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <apitest.h>
#include <apitest_guard.h>
#include <ndk/ntndk.h>
#include <strsafe.h>

#ifndef _PS_PROTECTION_DEFINED
#define _PS_PROTECTION_DEFINED
typedef union _PS_PROTECTION
{
    UCHAR Level;
    struct
    {
        UCHAR Type  : 3;
        UCHAR Audit : 1;
        UCHAR Signer: 4;
    } DUMMYSTRUCTNAME;
} PS_PROTECTION, *PPS_PROTECTION;
#endif

/* probelib.c */
typedef enum _ALIGNMENT_PROBE_MODE
{
    QUERY,
    SET
} ALIGNMENT_PROBE_MODE;

VOID
QuerySetProcessValidator(
    _In_ ALIGNMENT_PROBE_MODE ValidationMode,
    _In_ ULONG InfoClassIndex,
    _In_ PVOID InfoPointer,
    _In_ ULONG InfoLength,
    _In_ NTSTATUS ExpectedStatus);

VOID
QuerySetThreadValidator(
    _In_ ALIGNMENT_PROBE_MODE ValidationMode,
    _In_ ULONG InfoClassIndex,
    _In_ PVOID InfoPointer,
    _In_ ULONG InfoLength,
    _In_ NTSTATUS ExpectedStatus);

void
SetupLocale(
    _In_ ULONG AnsiCode,
    _In_ ULONG OemCode,
    _In_ ULONG Unicode);

#endif /* _NTDLL_APITEST_PRECOMP_H_ */
