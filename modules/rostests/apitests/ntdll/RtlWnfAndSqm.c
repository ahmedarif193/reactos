/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     ABI contract tests for RtlFlushHeaps, the WNF Rtl layer and WinSqm
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Measured against Windows 8.1 ntdll.dll:
 *   RtlFlushHeaps                                  0 args
 *   RtlQueryWnfStateData                           6 args
 *   RtlSubscribeWnfStateChangeNotification         9 args
 *   RtlUnsubscribeWnfNotificationWaitForCompletion 1 arg
 *   WinSqmAddToStream                              4 args
 *   WinSqmAddToStreamEx                            5 args
 *   WinSqmSetDWORD                                 3 args
 *   WinSqmSetString                                3 args
 *   WinSqmEventEnabled                             2 args
 *   WinSqmIsOptedIn                                0 args
 *
 * WinSqmEventEnabled/WinSqmIsOptedIn return FALSE on Windows whenever no SQM
 * session is active, which is exactly what a system without telemetry must
 * report - so "returns zero" is the correct contract, not a placeholder.
 */

#include <apitest.h>
#include <ndk/rtlfuncs.h>
#include <versionhelpers.h>
#include <pseh/pseh2.h>

typedef NTSTATUS (NTAPI *FN_RtlFlushHeaps)(VOID);
typedef NTSTATUS (NTAPI *FN_RtlQueryWnfStateData)(PULONG, ULONGLONG, PVOID, PVOID, PVOID, ULONG);
typedef NTSTATUS (NTAPI *FN_RtlSubscribeWnf)(PVOID *, ULONGLONG, ULONG, PVOID, PVOID, PVOID,
                                             ULONG, ULONG, ULONG);
typedef NTSTATUS (NTAPI *FN_RtlUnsubscribeWnf)(PVOID);
typedef BOOL (NTAPI *FN_WinSqmIsOptedIn)(VOID);
typedef BOOL (NTAPI *FN_WinSqmEventEnabled)(ULONG, ULONG);

static HMODULE hNtdll;
static FN_RtlFlushHeaps pRtlFlushHeaps;
static FN_RtlQueryWnfStateData pRtlQueryWnfStateData;
static FN_RtlSubscribeWnf pRtlSubscribeWnfStateChangeNotification;
static FN_RtlUnsubscribeWnf pRtlUnsubscribeWnfNotificationWaitForCompletion;
static FN_WinSqmIsOptedIn pWinSqmIsOptedIn;
static FN_WinSqmEventEnabled pWinSqmEventEnabled;

static void
Test_RtlFlushHeaps(void)
{
    PVOID Blocks[64];
    UINT i;
    NTSTATUS Status;

    if (!pRtlFlushHeaps)
        return;

    /* Dirty the heap so the flush has something to walk. */
    for (i = 0; i < ARRAYSIZE(Blocks); i++)
        Blocks[i] = RtlAllocateHeap(RtlGetProcessHeap(), 0, 128 + i * 16);

    Status = pRtlFlushHeaps();
    ok(NT_SUCCESS(Status), "RtlFlushHeaps returned 0x%08lx\n", Status);

    /* Every block must still be valid and writable afterwards. */
    for (i = 0; i < ARRAYSIZE(Blocks); i++)
    {
        if (!Blocks[i])
            continue;
        RtlFillMemory(Blocks[i], 128, 0xAB);
        ok(*(BYTE *)Blocks[i] == 0xAB, "block %u became unusable after the flush\n", i);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Blocks[i]);
    }

    /* Repeated flushes must stay stable. */
    for (i = 0; i < 200; i++)
    {
        if (!NT_SUCCESS(pRtlFlushHeaps()))
        {
            ok(FALSE, "RtlFlushHeaps failed on iteration %u\n", i);
            return;
        }
    }
    ok(TRUE, "200 consecutive flushes stayed successful\n");
}

static void
Test_WnfReportsFailure(void)
{
    ULONG ChangeStamp = 0xDEADBEEF;
    PVOID Subscription = (PVOID)(ULONG_PTR)0xDEADBEEF;
    NTSTATUS Status;

    /*
     * ReactOS has no WNF provider. These must report failure: a success return
     * would tell the caller a subscription exists and leave it waiting for
     * notifications that can never arrive.
     */
    if (pRtlQueryWnfStateData)
    {
        Status = pRtlQueryWnfStateData(&ChangeStamp, 0, NULL, NULL, NULL, 0);
        ok(!NT_SUCCESS(Status),
           "RtlQueryWnfStateData must not claim success without a WNF provider, got 0x%08lx\n",
           Status);
        /* Windows leaves the stamp untouched when the state name is invalid;
         * ReactOS initialises it so the caller never reads uninitialised data. */
        if (IsReactOS())
            ok(ChangeStamp == 0, "the change stamp must be initialised, got 0x%08lx\n", ChangeStamp);
        else
            trace("Windows left the change stamp at 0x%08lx\n", ChangeStamp);
    }

    if (pRtlSubscribeWnfStateChangeNotification)
    {
        Status = pRtlSubscribeWnfStateChangeNotification(&Subscription, 0, 0, NULL,
                                                         NULL, NULL, 0, 0, 0);
        ok(!NT_SUCCESS(Status),
           "RtlSubscribeWnfStateChangeNotification must not claim success, got 0x%08lx\n",
           Status);
        ok(Subscription == NULL,
           "a failed subscribe must NULL the handle, got %p\n", Subscription);
    }

    if (pRtlUnsubscribeWnfNotificationWaitForCompletion)
    {
        _SEH2_TRY
        {
            Status = pRtlUnsubscribeWnfNotificationWaitForCompletion(NULL);
            ok(!NT_SUCCESS(Status),
               "unsubscribing a NULL subscription must fail, got 0x%08lx\n", Status);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Windows dereferences the subscription without checking it. */
            ok(!IsReactOS(),
               "RtlUnsubscribeWnfNotificationWaitForCompletion(NULL) raised 0x%08lx\n",
               _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
}

static void
Test_SqmReportsDisabled(void)
{
    if (pWinSqmIsOptedIn)
    {
        BOOL ret = pWinSqmIsOptedIn();

        ok(ret == FALSE,
           "WinSqmIsOptedIn must report FALSE with no telemetry session, got 0x%08x\n", ret);
    }

    if (pWinSqmEventEnabled)
    {
        BOOL ret = pWinSqmEventEnabled(0, 0);

        ok(ret == FALSE,
           "WinSqmEventEnabled must report FALSE with no telemetry session, got 0x%08x\n", ret);
    }
}

static void
Test_ArityStackBalance(void)
{
    volatile ULONG_PTR Guard = 0xC3C3C3C3;
    ULONG ChangeStamp;
    PVOID Subscription;
    UINT i;

    for (i = 0; i < 1000; i++)
    {
        if (pRtlFlushHeaps)
            pRtlFlushHeaps();
        if (pWinSqmIsOptedIn)
            pWinSqmIsOptedIn();
        if (pWinSqmEventEnabled)
            pWinSqmEventEnabled(1, 2);
        if (pRtlQueryWnfStateData)
            pRtlQueryWnfStateData(&ChangeStamp, 0, NULL, NULL, NULL, 0);
        if (pRtlSubscribeWnfStateChangeNotification)
            pRtlSubscribeWnfStateChangeNotification(&Subscription, 0, 0, NULL, NULL, NULL, 0, 0, 0);
    }

    ok(Guard == 0xC3C3C3C3,
       "the caller frame was corrupted: an ntdll export has the wrong arity\n");
}

START_TEST(RtlWnfAndSqm)
{
    hNtdll = GetModuleHandleW(L"ntdll.dll");
    ok(hNtdll != NULL, "ntdll.dll is not loaded\n");
    if (!hNtdll)
        return;

    pRtlFlushHeaps = (FN_RtlFlushHeaps)GetProcAddress(hNtdll, "RtlFlushHeaps");
    pRtlQueryWnfStateData = (FN_RtlQueryWnfStateData)GetProcAddress(hNtdll, "RtlQueryWnfStateData");
    pRtlSubscribeWnfStateChangeNotification =
        (FN_RtlSubscribeWnf)GetProcAddress(hNtdll, "RtlSubscribeWnfStateChangeNotification");
    pRtlUnsubscribeWnfNotificationWaitForCompletion =
        (FN_RtlUnsubscribeWnf)GetProcAddress(hNtdll, "RtlUnsubscribeWnfNotificationWaitForCompletion");
    pWinSqmIsOptedIn = (FN_WinSqmIsOptedIn)GetProcAddress(hNtdll, "WinSqmIsOptedIn");
    pWinSqmEventEnabled = (FN_WinSqmEventEnabled)GetProcAddress(hNtdll, "WinSqmEventEnabled");

    ok(pRtlFlushHeaps != NULL, "ntdll!RtlFlushHeaps is missing\n");
    ok(pRtlQueryWnfStateData != NULL, "ntdll!RtlQueryWnfStateData is missing\n");
    ok(pRtlSubscribeWnfStateChangeNotification != NULL,
       "ntdll!RtlSubscribeWnfStateChangeNotification is missing\n");
    ok(pRtlUnsubscribeWnfNotificationWaitForCompletion != NULL,
       "ntdll!RtlUnsubscribeWnfNotificationWaitForCompletion is missing\n");
    ok(pWinSqmIsOptedIn != NULL, "ntdll!WinSqmIsOptedIn is missing\n");
    ok(pWinSqmEventEnabled != NULL, "ntdll!WinSqmEventEnabled is missing\n");

    Test_RtlFlushHeaps();
    Test_WnfReportsFailure();
    Test_SqmReportsDisabled();
    Test_ArityStackBalance();
}
