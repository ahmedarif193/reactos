/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows 11 public D3DKMT validation-status parity tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#ifdef D3DKMT_PUBLIC27_STANDALONE

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;

static unsigned int StandaloneFailures;

#define PARITY_OK(Condition, ...)                                             \
    do                                                                       \
    {                                                                        \
        if (!(Condition))                                                    \
        {                                                                    \
            ++StandaloneFailures;                                            \
            printf("FAIL: ");                                                \
            printf(__VA_ARGS__);                                             \
        }                                                                    \
    } while (0)

static FARPROC
LoadD3DKMTProc(const char *Name)
{
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");

    if (Gdi32 == NULL)
        Gdi32 = LoadLibraryW(L"gdi32.dll");
    return Gdi32 != NULL ? GetProcAddress(Gdi32, Name) : NULL;
}

#else

#include "precomp.h"
#define PARITY_OK ok

#endif

#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008L)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#ifndef STATUS_GRAPHICS_VAIL_STATE_CHANGED
#define STATUS_GRAPHICS_VAIL_STATE_CHANGED ((NTSTATUS)0xC01E0011L)
#endif

typedef NTSTATUS (APIENTRY *PFN_D3DKMT_PUBLIC_ONE_POINTER)(void *Data);

typedef struct _D3DKMT_PUBLIC_STATUS_TEST
{
    const char *Name;
    NTSTATUS ZeroStatus;
    NTSTATUS PatternStatus;
} D3DKMT_PUBLIC_STATUS_TEST;

static const D3DKMT_PUBLIC_STATUS_TEST PublicStatusTests[] =
{
    { "D3DKMTAdjustFullscreenGamma", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTConfigureSharedResource", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTConnectDoorbell", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTCreateDoorbell", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTCreateNativeFence", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTCreateProtectedSession", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTDestroyDoorbell", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTDestroyProtectedSession", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTFlushHeapTransitions", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTGetNativeFenceLogDetail", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTGetPostCompositionCaps", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTGetProcessDeviceRemovalSupport", STATUS_INVALID_HANDLE, STATUS_INVALID_HANDLE },
    { "D3DKMTMarkDeviceAsError", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTNotifyWorkSubmission", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTOpenKeyedMutexFromNtHandle", STATUS_INVALID_HANDLE, STATUS_INVALID_HANDLE },
    { "D3DKMTOpenNativeFenceFromNtHandle", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTOpenProtectedSessionFromNtHandle", STATUS_INVALID_HANDLE, STATUS_INVALID_HANDLE },
    { "D3DKMTOutputDuplPresentToHwQueue", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTPresentRedirected", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTQueryProcessOfferInfo", STATUS_INVALID_PARAMETER, STATUS_INVALID_HANDLE },
    { "D3DKMTQueryProtectedSessionInfoFromNtHandle", STATUS_INVALID_HANDLE, STATUS_INVALID_HANDLE },
    { "D3DKMTQueryProtectedSessionStatus", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTQueryRemoteVidPnSourceFromGdiDisplayName", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTRegisterVailProcess", STATUS_GRAPHICS_VAIL_STATE_CHANGED, STATUS_GRAPHICS_VAIL_STATE_CHANGED },
    { "D3DKMTSetHwProtectionTeardownRecovery", STATUS_SUCCESS, STATUS_SUCCESS },
    { "D3DKMTSetVidPnSourceHwProtection", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER },
    { "D3DKMTTrimProcessCommitment", STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER }
};

static void
TestPublicStatusCase(const D3DKMT_PUBLIC_STATUS_TEST *Test)
{
    union
    {
        ULONGLONG Alignment;
        unsigned char Bytes[1024];
    } Data, Before;
    PFN_D3DKMT_PUBLIC_ONE_POINTER Function;
    NTSTATUS Status;

    Function = (PFN_D3DKMT_PUBLIC_ONE_POINTER)LoadD3DKMTProc(Test->Name);
    PARITY_OK(Function != NULL, "%s is not exported by gdi32.dll\n", Test->Name);
    if (Function == NULL)
        return;

    Status = Function(NULL);
    PARITY_OK(Status == STATUS_INVALID_PARAMETER, "%s(NULL) returned 0x%08lx, expected 0x%08lx\n", Test->Name, (unsigned long)Status, (unsigned long)STATUS_INVALID_PARAMETER);

    memset(Data.Bytes, 0, sizeof(Data.Bytes));
    Before = Data;
    Status = Function(Data.Bytes);
    PARITY_OK(Status == Test->ZeroStatus, "%s(zero) returned 0x%08lx, expected 0x%08lx\n", Test->Name, (unsigned long)Status, (unsigned long)Test->ZeroStatus);
    PARITY_OK(memcmp(Data.Bytes, Before.Bytes, sizeof(Data.Bytes)) == 0, "%s(zero) modified its input buffer\n", Test->Name);

    memset(Data.Bytes, 0xa5, sizeof(Data.Bytes));
    Before = Data;
    Status = Function(Data.Bytes);
    PARITY_OK(Status == Test->PatternStatus, "%s(pattern) returned 0x%08lx, expected 0x%08lx\n", Test->Name, (unsigned long)Status, (unsigned long)Test->PatternStatus);
    PARITY_OK(memcmp(Data.Bytes, Before.Bytes, sizeof(Data.Bytes)) == 0, "%s(pattern) modified its input buffer\n", Test->Name);
}

static void
RunPublicStatusTests(void)
{
    unsigned int Index;

    for (Index = 0; Index < sizeof(PublicStatusTests) / sizeof(PublicStatusTests[0]); ++Index)
        TestPublicStatusCase(&PublicStatusTests[Index]);
}

#ifdef D3DKMT_PUBLIC27_STANDALONE

int
main(void)
{
    RunPublicStatusTests();
    printf("D3DKMT_PUBLIC27_DONE tests=%u failures=%u\n", (unsigned int)(sizeof(PublicStatusTests) / sizeof(PublicStatusTests[0])), StandaloneFailures);
    return StandaloneFailures == 0 ? 0 : 1;
}

#else

START_TEST(public27)
{
    RunPublicStatusTests();
}

#endif
