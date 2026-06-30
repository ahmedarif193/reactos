/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Miscellaneous D3DKMT user-mode surface tests:
 *              output duplication, GPU clock calibration, stable power state,
 *              process scheduling-priority class, in-process context priority,
 *              trim / budget-change notifications, InvalidateCache,
 *              QueryStatistics and QueryVideoMemoryInfo budget edge cases.
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * This file pins to WDDM 2.0 (DXGKDDI_INTERFACE_VERSION == 0x5023, set by the
 * test CMakeLists). Entry points whose PFND3DKMT_* typedef only exists at a
 * higher interface level are therefore NOT referenced here, because the typedef
 * is #if'd out and the file would not compile:
 *
 *   - Full-screen-exclusive block: D3DKMTSetFSEBlock / D3DKMTQueryFSEBlock
 *       (PFND3DKMT_SETFSEBLOCK / _QUERYFSEBLOCK are WDDM 2.1, gated out at 0x5023)
 *   - Protected sessions: D3DKMTCreateProtectedSession / DestroyProtectedSession /
 *       QueryProtectedSessionStatus / QueryProtectedSessionInfoFromNtHandle /
 *       OpenProtectedSessionFromNtHandle  (all WDDM 2.3, gated out at 0x5023)
 *   - D3DKMTGetProcessDeviceRemovalSupport (WDDM 2.3, gated out at 0x5023)
 *
 * The entry points are still resolved dynamically via GetProcAddress, so the
 * same binary degrades gracefully (skip) on hosts that lack a given export. The
 * portable contract checked for every covered function is "a NULL / bad-handle
 * argument is refused" (STATUS_INVALID_PARAMETER, another error status, or an
 * access violation in the user-mode thunk). Positive paths that need a render
 * device, a real GPU engine, or elevation are attempted and skip()ped on
 * failure -- success is never asserted (Win11-green rule).
 *
 * Reference: Microsoft "D3DKMTCreateOutputDupl" & companions,
 *            "D3DKMTQueryClockCalibration", "D3DKMTSetStablePowerState",
 *            "D3DKMTSetProcessSchedulingPriorityClass" /
 *            "D3DKMTGetProcessSchedulingPriorityClass",
 *            "D3DKMTSetContextInProcessSchedulingPriority",
 *            "D3DKMTRegisterTrimNotification" /
 *            "D3DKMTRegisterBudgetChangeNotification",
 *            "D3DKMTInvalidateCache", "D3DKMTQueryStatistics",
 *            "D3DKMTQueryVideoMemoryInfo".
 */

#include "precomp.h"

/* Deliberately invalid handle values used for the bad-handle contract. */
#define MISC_BAD_ADAPTER    ((D3DKMT_HANDLE)0xDEAD0001)
#define MISC_BAD_DEVICE     ((D3DKMT_HANDLE)0xDEAD0002)
#define MISC_BAD_CONTEXT    ((D3DKMT_HANDLE)0xDEAD0003)
#define MISC_BAD_ALLOCATION ((D3DKMT_HANDLE)0xDEAD0004)
#define MISC_BAD_NTHANDLE   ((HANDLE)(ULONG_PTR)0xDEAD0010)

/*
 * Call CallExpr (which must yield an NTSTATUS) under SEH and assert that the
 * call was refused -- i.e. it either returned a non-success status or raised an
 * access violation in the thunk. Only a STATUS_SUCCESS return is a bug. This is
 * the multi-argument / bad-handle analogue of EXPECT_NULL_REJECTED.
 */
#define MISC_EXPECT_REFUSED(CallExpr, Name)                                    \
do {                                                                           \
    NTSTATUS _st = STATUS_SUCCESS;                                             \
    BOOL _faulted = FALSE;                                                     \
    _SEH2_TRY { _st = (CallExpr); }                                           \
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { _faulted = TRUE; }              \
    _SEH2_END;                                                                 \
    ok(_faulted || !NT_SUCCESS(_st),                                          \
       Name " must be refused (error status or fault), got 0x%08lX%s\n",      \
       (long)_st, _faulted ? " (faulted)" : "");                             \
} while (0)

/*
 * Like the above but only traces the outcome -- used where the contract is
 * genuinely undefined (e.g. unregistering an unknown notification handle, which
 * an implementation is free to treat as an idempotent no-op).
 */
#define MISC_TRACE_RESULT(CallExpr, Name)                                      \
do {                                                                           \
    NTSTATUS _st = STATUS_SUCCESS;                                             \
    BOOL _faulted = FALSE;                                                     \
    _SEH2_TRY { _st = (CallExpr); }                                           \
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { _faulted = TRUE; }              \
    _SEH2_END;                                                                 \
    trace(Name " returned 0x%08lX%s\n",                                       \
          (long)_st, _faulted ? " (faulted)" : "");                          \
} while (0)

/* No-op notification callbacks matching the canonical PFND3DKMT_* signatures. */
static VOID APIENTRY
MiscTrimCallback(_Inout_ D3DKMT_TRIMNOTIFICATION *pNotification)
{
    (void)pNotification;
}

static VOID APIENTRY
MiscBudgetCallback(_In_ D3DKMT_BUDGETCHANGENOTIFICATION *pNotification)
{
    (void)pNotification;
}

/* =====================================================================
 *  Output duplication (Desktop Duplication kernel surface, WDDM 1.2+)
 * ===================================================================== */

static void Test_CreateOutputDupl_NullArg(void)
{
    LOADFN(PFND3DKMT_CREATEOUTPUTDUPL, p, "D3DKMTCreateOutputDupl");
    EXPECT_NULL_REJECTED(p, "D3DKMTCreateOutputDupl");
}

static void Test_CreateOutputDupl_BadHandle(void)
{
    D3DKMT_CREATE_OUTPUTDUPL cod;
    LOADFN(PFND3DKMT_CREATEOUTPUTDUPL, p, "D3DKMTCreateOutputDupl");

    memset(&cod, 0, sizeof(cod));
    cod.hAdapter = MISC_BAD_ADAPTER;
    cod.VidPnSourceId = 0;
    cod.KeyedMutexCount = 0;        /* pre-create check form */
    MISC_EXPECT_REFUSED(p(&cod), "D3DKMTCreateOutputDupl(bad adapter)");
}

static void Test_DestroyOutputDupl_NullArg(void)
{
    LOADFN(PFND3DKMT_DESTROYOUTPUTDUPL, p, "D3DKMTDestroyOutputDupl");
    EXPECT_NULL_REJECTED(p, "D3DKMTDestroyOutputDupl");
}

static void Test_DestroyOutputDupl_BadHandle(void)
{
    D3DKMT_DESTROY_OUTPUTDUPL dod;
    LOADFN(PFND3DKMT_DESTROYOUTPUTDUPL, p, "D3DKMTDestroyOutputDupl");

    memset(&dod, 0, sizeof(dod));
    dod.hAdapter = MISC_BAD_ADAPTER;
    dod.VidPnSourceId = 0;
    dod.bDestroyAllContexts = TRUE;
    MISC_EXPECT_REFUSED(p(&dod), "D3DKMTDestroyOutputDupl(bad adapter)");
}

static void Test_OutputDuplGetFrameInfo_NullArg(void)
{
    LOADFN(PFND3DKMT_OUTPUTDUPLGETFRAMEINFO, p, "D3DKMTOutputDuplGetFrameInfo");
    EXPECT_NULL_REJECTED(p, "D3DKMTOutputDuplGetFrameInfo");
}

static void Test_OutputDuplGetFrameInfo_BadHandle(void)
{
    D3DKMT_OUTPUTDUPL_GET_FRAMEINFO fi;
    LOADFN(PFND3DKMT_OUTPUTDUPLGETFRAMEINFO, p, "D3DKMTOutputDuplGetFrameInfo");

    memset(&fi, 0, sizeof(fi));
    fi.hAdapter = MISC_BAD_ADAPTER;
    fi.VidPnSourceId = 0;
    MISC_EXPECT_REFUSED(p(&fi), "D3DKMTOutputDuplGetFrameInfo(bad adapter)");
}

static void Test_OutputDuplGetMetaData_NullArg(void)
{
    LOADFN(PFND3DKMT_OUTPUTDUPLGETMETADATA, p, "D3DKMTOutputDuplGetMetaData");
    EXPECT_NULL_REJECTED(p, "D3DKMTOutputDuplGetMetaData");
}

static void Test_OutputDuplGetMetaData_BadHandle(void)
{
    D3DKMT_OUTPUTDUPL_METADATA md;
    LOADFN(PFND3DKMT_OUTPUTDUPLGETMETADATA, p, "D3DKMTOutputDuplGetMetaData");

    memset(&md, 0, sizeof(md));
    md.hAdapter = MISC_BAD_ADAPTER;
    md.VidPnSourceId = 0;
    md.Type = D3DKMT_OUTPUTDUPL_METADATATYPE_DIRTY_RECTS;
    md.BufferSizeSupplied = 0;
    md.pBuffer = NULL;
    MISC_EXPECT_REFUSED(p(&md), "D3DKMTOutputDuplGetMetaData(bad adapter)");
}

static void Test_OutputDuplGetPointerShapeData_NullArg(void)
{
    LOADFN(PFND3DKMT_OUTPUTDUPLGETPOINTERSHAPEDATA, p,
           "D3DKMTOutputDuplGetPointerShapeData");
    EXPECT_NULL_REJECTED(p, "D3DKMTOutputDuplGetPointerShapeData");
}

static void Test_OutputDuplGetPointerShapeData_BadHandle(void)
{
    D3DKMT_OUTPUTDUPL_GET_POINTER_SHAPE_DATA ps;
    LOADFN(PFND3DKMT_OUTPUTDUPLGETPOINTERSHAPEDATA, p,
           "D3DKMTOutputDuplGetPointerShapeData");

    memset(&ps, 0, sizeof(ps));
    ps.hAdapter = MISC_BAD_ADAPTER;
    ps.VidPnSourceId = 0;
    ps.BufferSizeSupplied = 0;
    ps.pShapeBuffer = NULL;
    MISC_EXPECT_REFUSED(p(&ps),
                        "D3DKMTOutputDuplGetPointerShapeData(bad adapter)");
}

static void Test_OutputDuplReleaseFrame_NullArg(void)
{
    LOADFN(PFND3DKMT_OUTPUTDUPLRELEASEFRAME, p, "D3DKMTOutputDuplReleaseFrame");
    EXPECT_NULL_REJECTED(p, "D3DKMTOutputDuplReleaseFrame");
}

static void Test_OutputDuplReleaseFrame_BadHandle(void)
{
    D3DKMT_OUTPUTDUPL_RELEASE_FRAME rf;
    LOADFN(PFND3DKMT_OUTPUTDUPLRELEASEFRAME, p, "D3DKMTOutputDuplReleaseFrame");

    memset(&rf, 0, sizeof(rf));
    rf.hAdapter = MISC_BAD_ADAPTER;
    rf.VidPnSourceId = 0;
    MISC_EXPECT_REFUSED(p(&rf), "D3DKMTOutputDuplReleaseFrame(bad adapter)");
}

static void Test_OutputDuplPresent_NullArg(void)
{
    LOADFN(PFND3DKMT_OUTPUTDUPLPRESENT, p, "D3DKMTOutputDuplPresent");
    EXPECT_NULL_REJECTED(p, "D3DKMTOutputDuplPresent");
}

static void Test_OutputDuplPresent_BadHandle(void)
{
    D3DKMT_OUTPUTDUPLPRESENT odp;
    LOADFN(PFND3DKMT_OUTPUTDUPLPRESENT, p, "D3DKMTOutputDuplPresent");

    memset(&odp, 0, sizeof(odp));
    odp.hContext = MISC_BAD_CONTEXT;        /* this entry point keys off a context */
    odp.hSource = MISC_BAD_ALLOCATION;
    odp.VidPnSourceId = 0;
    odp.BroadcastContextCount = 0;
    MISC_EXPECT_REFUSED(p(&odp), "D3DKMTOutputDuplPresent(bad context)");
}

/* =====================================================================
 *  GPU clock calibration
 * ===================================================================== */

static void Test_QueryClockCalibration_NullArg(void)
{
    LOADFN(PFND3DKMT_QUERYCLOCKCALIBRATION, p, "D3DKMTQueryClockCalibration");
    EXPECT_NULL_REJECTED(p, "D3DKMTQueryClockCalibration");
}

static void Test_QueryClockCalibration_BadHandle(void)
{
    D3DKMT_QUERYCLOCKCALIBRATION qcc;
    LOADFN(PFND3DKMT_QUERYCLOCKCALIBRATION, p, "D3DKMTQueryClockCalibration");

    memset(&qcc, 0, sizeof(qcc));
    qcc.hAdapter = MISC_BAD_ADAPTER;
    qcc.NodeOrdinal = 0;
    qcc.PhysicalAdapterIndex = 0;
    MISC_EXPECT_REFUSED(p(&qcc), "D3DKMTQueryClockCalibration(bad adapter)");
}

static void Test_QueryClockCalibration_RealAdapter(void)
{
    D3DKMT_QUERYCLOCKCALIBRATION qcc;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_QUERYCLOCKCALIBRATION, p, "D3DKMTQueryClockCalibration");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    memset(&qcc, 0, sizeof(qcc));
    qcc.hAdapter = hAdapter;
    qcc.NodeOrdinal = 0;
    qcc.PhysicalAdapterIndex = 0;

    Status = p(&qcc);
    if (!NT_SUCCESS(Status))
    {
        /* Needs a real GPU scheduling node; software/display-only adapters
         * legitimately do not implement it. */
        skip("QueryClockCalibration not supported on this adapter (0x%08lX)\n",
             (long)Status);
        CloseAdapter(hAdapter);
        return;
    }

    trace("ClockCalibration node0: GpuFrequency=%llu GpuClock=%llu CpuClock=%llu\n",
          (unsigned long long)qcc.ClockData.GpuFrequency,
          (unsigned long long)qcc.ClockData.GpuClockCounter,
          (unsigned long long)qcc.ClockData.CpuClockCounter);

    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  Stable power state (per the typedef this takes a single struct pointer
 *  carrying { hAdapter, Enabled }, not separate arguments)
 * ===================================================================== */

static void Test_SetStablePowerState_NullArg(void)
{
    LOADFN(PFND3DKMT_SETSTABLEPOWERSTATE, p, "D3DKMTSetStablePowerState");
    EXPECT_NULL_REJECTED(p, "D3DKMTSetStablePowerState");
}

static void Test_SetStablePowerState_BadHandle(void)
{
    D3DKMT_SETSTABLEPOWERSTATE sps;
    LOADFN(PFND3DKMT_SETSTABLEPOWERSTATE, p, "D3DKMTSetStablePowerState");

    memset(&sps, 0, sizeof(sps));
    sps.hAdapter = MISC_BAD_ADAPTER;
    sps.Enabled = TRUE;
    MISC_EXPECT_REFUSED(p(&sps), "D3DKMTSetStablePowerState(bad adapter)");
}

static void Test_SetStablePowerState_RealAdapter(void)
{
    D3DKMT_SETSTABLEPOWERSTATE sps;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_SETSTABLEPOWERSTATE, p, "D3DKMTSetStablePowerState");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    /* Stable power state requires the machine to be in developer / debug mode
     * and a real GPU; on a normal Win11 box this is refused. Attempt, never
     * assert success, and try to restore the state on the way out. */
    memset(&sps, 0, sizeof(sps));
    sps.hAdapter = hAdapter;
    sps.Enabled = TRUE;
    Status = p(&sps);
    if (!NT_SUCCESS(Status))
    {
        skip("SetStablePowerState(TRUE) not permitted on this host (0x%08lX)\n",
             (long)Status);
        CloseAdapter(hAdapter);
        return;
    }

    trace("SetStablePowerState(TRUE) succeeded; restoring\n");
    sps.Enabled = FALSE;
    Status = p(&sps);
    trace("SetStablePowerState(FALSE) returned 0x%08lX\n", (long)Status);

    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  Process scheduling priority class (multi-argument entry points:
 *  HANDLE hProcess + D3DKMT_SCHEDULINGPRIORITYCLASS [*])
 * ===================================================================== */

static void Test_GetProcessSchedulingPriorityClass_Current(void)
{
    D3DKMT_SCHEDULINGPRIORITYCLASS cls = D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_GETPROCESSSCHEDULINGPRIORITYCLASS, p,
           "D3DKMTGetProcessSchedulingPriorityClass");

    Status = p(GetCurrentProcess(), &cls);
    if (!NT_SUCCESS(Status))
    {
        skip("GetProcessSchedulingPriorityClass(current) failed (0x%08lX)\n",
             (long)Status);
        return;
    }

    trace("Current process GPU scheduling priority class = %d\n", (int)cls);
    ok(cls <= D3DKMT_SCHEDULINGPRIORITYCLASS_REALTIME,
       "Scheduling priority class %d out of the documented range\n", (int)cls);
}

static void Test_GetProcessSchedulingPriorityClass_NullOut(void)
{
    LOADFN(PFND3DKMT_GETPROCESSSCHEDULINGPRIORITYCLASS, p,
           "D3DKMTGetProcessSchedulingPriorityClass");
    MISC_EXPECT_REFUSED(p(GetCurrentProcess(), NULL),
                        "D3DKMTGetProcessSchedulingPriorityClass(NULL out)");
}

static void Test_GetProcessSchedulingPriorityClass_BadHandle(void)
{
    D3DKMT_SCHEDULINGPRIORITYCLASS cls = D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL;
    LOADFN(PFND3DKMT_GETPROCESSSCHEDULINGPRIORITYCLASS, p,
           "D3DKMTGetProcessSchedulingPriorityClass");
    MISC_EXPECT_REFUSED(p(MISC_BAD_NTHANDLE, &cls),
                        "D3DKMTGetProcessSchedulingPriorityClass(bad handle)");
    (void)cls;
}

static void Test_SetProcessSchedulingPriorityClass_RoundTrip(void)
{
    D3DKMT_SCHEDULINGPRIORITYCLASS cls = D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL;
    NTSTATUS Status;
    PFND3DKMT_GETPROCESSSCHEDULINGPRIORITYCLASS pGet;

    LOADFN(PFND3DKMT_SETPROCESSSCHEDULINGPRIORITYCLASS, pSet,
           "D3DKMTSetProcessSchedulingPriorityClass");

    /* Read the current class first so we set back exactly what was there. */
    pGet = (PFND3DKMT_GETPROCESSSCHEDULINGPRIORITYCLASS)
           LoadD3DKMTProc("D3DKMTGetProcessSchedulingPriorityClass");
    if (pGet && !NT_SUCCESS(pGet(GetCurrentProcess(), &cls)))
        cls = D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL;

    /* Setting the class requires SeIncreaseBasePriorityPrivilege; a normal
     * (non-elevated) test process is refused. Attempt, never assert success. */
    Status = pSet(GetCurrentProcess(), cls);
    if (!NT_SUCCESS(Status))
    {
        skip("SetProcessSchedulingPriorityClass not permitted (0x%08lX)\n",
             (long)Status);
        return;
    }
    trace("SetProcessSchedulingPriorityClass(current, %d) succeeded\n", (int)cls);
}

static void Test_SetProcessSchedulingPriorityClass_BadHandle(void)
{
    LOADFN(PFND3DKMT_SETPROCESSSCHEDULINGPRIORITYCLASS, p,
           "D3DKMTSetProcessSchedulingPriorityClass");
    MISC_EXPECT_REFUSED(p(MISC_BAD_NTHANDLE,
                          D3DKMT_SCHEDULINGPRIORITYCLASS_NORMAL),
                        "D3DKMTSetProcessSchedulingPriorityClass(bad handle)");
}

static void Test_SetProcessSchedulingPriorityClass_InvalidClass(void)
{
    LOADFN(PFND3DKMT_SETPROCESSSCHEDULINGPRIORITYCLASS, p,
           "D3DKMTSetProcessSchedulingPriorityClass");
    /* An out-of-range class value must be rejected regardless of privilege. */
    MISC_EXPECT_REFUSED(p(GetCurrentProcess(),
                          (D3DKMT_SCHEDULINGPRIORITYCLASS)0x7FFFFFFF),
                        "D3DKMTSetProcessSchedulingPriorityClass(bad class)");
}

/* =====================================================================
 *  In-process context scheduling priority
 * ===================================================================== */

static void Test_SetContextInProcessSchedulingPriority_NullArg(void)
{
    LOADFN(PFND3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY, p,
           "D3DKMTSetContextInProcessSchedulingPriority");
    EXPECT_NULL_REJECTED(p, "D3DKMTSetContextInProcessSchedulingPriority");
}

static void Test_SetContextInProcessSchedulingPriority_BadHandle(void)
{
    D3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY cip;
    LOADFN(PFND3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY, p,
           "D3DKMTSetContextInProcessSchedulingPriority");

    memset(&cip, 0, sizeof(cip));
    cip.hContext = MISC_BAD_CONTEXT;
    cip.Priority = 0;
    MISC_EXPECT_REFUSED(p(&cip),
                        "D3DKMTSetContextInProcessSchedulingPriority(bad context)");
}

/* =====================================================================
 *  Trim notifications
 * ===================================================================== */

static void Test_RegisterTrimNotification_NullArg(void)
{
    LOADFN(PFND3DKMT_REGISTERTRIMNOTIFICATION, p,
           "D3DKMTRegisterTrimNotification");
    EXPECT_NULL_REJECTED(p, "D3DKMTRegisterTrimNotification");
}

static void Test_RegisterTrimNotification_BadDevice(void)
{
    D3DKMT_REGISTERTRIMNOTIFICATION reg;
    LOADFN(PFND3DKMT_REGISTERTRIMNOTIFICATION, p,
           "D3DKMTRegisterTrimNotification");

    memset(&reg, 0, sizeof(reg));
    reg.hDevice = MISC_BAD_DEVICE;
    reg.Callback = MiscTrimCallback;
    reg.Context = NULL;
    /* Win11's RegisterTrimNotification registers a process-wide trim callback and
     * does NOT validate the device handle -- it returns STATUS_SUCCESS even for a
     * bogus device. Accept any result (trace); a leaked registration is harmless
     * in this short-lived test process. */
    {
        NTSTATUS _st = p(&reg);
        trace("RegisterTrimNotification(bad device) -> 0x%08lx\n", (unsigned long)_st);
    }
}

static void Test_UnregisterTrimNotification_NullArg(void)
{
    LOADFN(PFND3DKMT_UNREGISTERTRIMNOTIFICATION, p,
           "D3DKMTUnregisterTrimNotification");
    EXPECT_NULL_REJECTED(p, "D3DKMTUnregisterTrimNotification");
}

static void Test_UnregisterTrimNotification_BadHandle(void)
{
    D3DKMT_UNREGISTERTRIMNOTIFICATION unreg;
    LOADFN(PFND3DKMT_UNREGISTERTRIMNOTIFICATION, p,
           "D3DKMTUnregisterTrimNotification");

    /* Unregistering an unknown handle has no documented contract; just exercise
     * the path under SEH and trace the result. */
    memset(&unreg, 0, sizeof(unreg));
    unreg.Handle = (VOID *)MISC_BAD_NTHANDLE;
    MISC_TRACE_RESULT(p(&unreg),
                      "D3DKMTUnregisterTrimNotification(bad handle)");
}

static void Test_TrimNotification_Lifecycle(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_REGISTERTRIMNOTIFICATION reg;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_REGISTERTRIMNOTIFICATION, pReg,
           "D3DKMTRegisterTrimNotification");
    LOADFN(PFND3DKMT_UNREGISTERTRIMNOTIFICATION, pUnreg,
           "D3DKMTUnregisterTrimNotification");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed (display-only adapter?)\n");
                    CloseAdapter(hAdapter); return; }

    memset(&reg, 0, sizeof(reg));
    reg.hDevice = hDevice;
    reg.Callback = MiscTrimCallback;
    reg.Context = NULL;

    Status = pReg(&reg);
    if (!NT_SUCCESS(Status))
    {
        skip("RegisterTrimNotification not supported on this device (0x%08lX)\n",
             (long)Status);
        goto cleanup;
    }

    ok(reg.Handle != NULL,
       "RegisterTrimNotification should return a non-NULL handle\n");

    {
        D3DKMT_UNREGISTERTRIMNOTIFICATION unreg;
        memset(&unreg, 0, sizeof(unreg));
        unreg.Handle = reg.Handle;
        Status = pUnreg(&unreg);
        ok(NT_SUCCESS(Status),
           "UnregisterTrimNotification(valid handle) failed 0x%08lX\n",
           (long)Status);
    }

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  Budget-change notifications
 * ===================================================================== */

static void Test_RegisterBudgetChangeNotification_NullArg(void)
{
    LOADFN(PFND3DKMT_REGISTERBUDGETCHANGENOTIFICATION, p,
           "D3DKMTRegisterBudgetChangeNotification");
    EXPECT_NULL_REJECTED(p, "D3DKMTRegisterBudgetChangeNotification");
}

static void Test_RegisterBudgetChangeNotification_BadDevice(void)
{
    D3DKMT_REGISTERBUDGETCHANGENOTIFICATION reg;
    LOADFN(PFND3DKMT_REGISTERBUDGETCHANGENOTIFICATION, p,
           "D3DKMTRegisterBudgetChangeNotification");

    memset(&reg, 0, sizeof(reg));
    reg.hDevice = MISC_BAD_DEVICE;
    reg.Callback = MiscBudgetCallback;
    reg.Context = NULL;
    MISC_EXPECT_REFUSED(p(&reg),
                        "D3DKMTRegisterBudgetChangeNotification(bad device)");
}

static void Test_UnregisterBudgetChangeNotification_NullArg(void)
{
    LOADFN(PFND3DKMT_UNREGISTERBUDGETCHANGENOTIFICATION, p,
           "D3DKMTUnregisterBudgetChangeNotification");
    EXPECT_NULL_REJECTED(p, "D3DKMTUnregisterBudgetChangeNotification");
}

static void Test_UnregisterBudgetChangeNotification_BadHandle(void)
{
    D3DKMT_UNREGISTERBUDGETCHANGENOTIFICATION unreg;
    LOADFN(PFND3DKMT_UNREGISTERBUDGETCHANGENOTIFICATION, p,
           "D3DKMTUnregisterBudgetChangeNotification");

    memset(&unreg, 0, sizeof(unreg));
    unreg.Handle = (VOID *)MISC_BAD_NTHANDLE;
    MISC_TRACE_RESULT(p(&unreg),
                      "D3DKMTUnregisterBudgetChangeNotification(bad handle)");
}

static void Test_BudgetChangeNotification_Lifecycle(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_REGISTERBUDGETCHANGENOTIFICATION reg;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_REGISTERBUDGETCHANGENOTIFICATION, pReg,
           "D3DKMTRegisterBudgetChangeNotification");
    LOADFN(PFND3DKMT_UNREGISTERBUDGETCHANGENOTIFICATION, pUnreg,
           "D3DKMTUnregisterBudgetChangeNotification");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed (display-only adapter?)\n");
                    CloseAdapter(hAdapter); return; }

    memset(&reg, 0, sizeof(reg));
    reg.hDevice = hDevice;
    reg.Callback = MiscBudgetCallback;
    reg.Context = NULL;

    Status = pReg(&reg);
    if (!NT_SUCCESS(Status))
    {
        skip("RegisterBudgetChangeNotification not supported (0x%08lX)\n",
             (long)Status);
        goto cleanup;
    }

    ok(reg.Handle != NULL,
       "RegisterBudgetChangeNotification should return a non-NULL handle\n");

    {
        D3DKMT_UNREGISTERBUDGETCHANGENOTIFICATION unreg;
        memset(&unreg, 0, sizeof(unreg));
        unreg.Handle = reg.Handle;
        Status = pUnreg(&unreg);
        ok(NT_SUCCESS(Status),
           "UnregisterBudgetChangeNotification(valid handle) failed 0x%08lX\n",
           (long)Status);
    }

cleanup:
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* =====================================================================
 *  QueryStatistics (documented "reserved for system use"; NULL only)
 * ===================================================================== */

static void Test_QueryStatistics_NullArg(void)
{
    LOADFN(PFND3DKMT_QUERYSTATISTICS, p, "D3DKMTQueryStatistics");
    EXPECT_NULL_REJECTED(p, "D3DKMTQueryStatistics");
}

/* =====================================================================
 *  InvalidateCache
 * ===================================================================== */

static void Test_InvalidateCache_NullArg(void)
{
    LOADFN(PFND3DKMT_INVALIDATECACHE, p, "D3DKMTInvalidateCache");
    EXPECT_NULL_REJECTED(p, "D3DKMTInvalidateCache");
}

static void Test_InvalidateCache_BadHandle(void)
{
    D3DKMT_INVALIDATECACHE ic;
    LOADFN(PFND3DKMT_INVALIDATECACHE, p, "D3DKMTInvalidateCache");

    memset(&ic, 0, sizeof(ic));
    ic.hDevice = MISC_BAD_DEVICE;
    ic.hAllocation = MISC_BAD_ALLOCATION;
    ic.Offset = 0;
    ic.Length = 0x1000;
    MISC_EXPECT_REFUSED(p(&ic), "D3DKMTInvalidateCache(bad device)");
}

/* =====================================================================
 *  QueryVideoMemoryInfo -- budget edge cases (positive LOCAL path lives in
 *  the separate videomem suite; here we cover the edges)
 * ===================================================================== */

static void Test_QueryVideoMemoryInfo_InvalidSegmentGroup(void)
{
    D3DKMT_QUERYVIDEOMEMORYINFO qvm;
    D3DKMT_HANDLE hAdapter;

    LOADFN(PFND3DKMT_QUERYVIDEOMEMORYINFO, p, "D3DKMTQueryVideoMemoryInfo");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    memset(&qvm, 0, sizeof(qvm));
    qvm.hProcess = NULL;
    qvm.hAdapter = hAdapter;
    qvm.MemorySegmentGroup = (D3DKMT_MEMORY_SEGMENT_GROUP)0x99;   /* invalid */
    MISC_EXPECT_REFUSED(p(&qvm),
                        "D3DKMTQueryVideoMemoryInfo(invalid segment group)");

    CloseAdapter(hAdapter);
}

static void Test_QueryVideoMemoryInfo_NonLocal(void)
{
    D3DKMT_QUERYVIDEOMEMORYINFO qvm;
    D3DKMT_HANDLE hAdapter;
    NTSTATUS Status;

    LOADFN(PFND3DKMT_QUERYVIDEOMEMORYINFO, p, "D3DKMTQueryVideoMemoryInfo");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    memset(&qvm, 0, sizeof(qvm));
    qvm.hProcess = NULL;
    qvm.hAdapter = hAdapter;
    qvm.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL;

    Status = p(&qvm);
    if (!NT_SUCCESS(Status))
    {
        skip("QueryVideoMemoryInfo(NON_LOCAL) not supported (0x%08lX)\n",
             (long)Status);
        CloseAdapter(hAdapter);
        return;
    }

    ok(qvm.CurrentReservation <= qvm.AvailableForReservation ||
       qvm.AvailableForReservation == 0,
       "NON_LOCAL reservation %llu exceeds available-for-reservation %llu\n",
       (unsigned long long)qvm.CurrentReservation,
       (unsigned long long)qvm.AvailableForReservation);

    trace("VideoMemory NON_LOCAL: Budget=%llu CurrentUsage=%llu "
          "Reservation=%llu AvailForRsv=%llu\n",
          (unsigned long long)qvm.Budget,
          (unsigned long long)qvm.CurrentUsage,
          (unsigned long long)qvm.CurrentReservation,
          (unsigned long long)qvm.AvailableForReservation);

    CloseAdapter(hAdapter);
}

static void Test_QueryVideoMemoryInfo_HighPhysAdapterIndex(void)
{
    D3DKMT_QUERYVIDEOMEMORYINFO qvm;
    D3DKMT_HANDLE hAdapter;

    LOADFN(PFND3DKMT_QUERYVIDEOMEMORYINFO, p, "D3DKMTQueryVideoMemoryInfo");

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter on \\\\.\\DISPLAY1\n"); return; }

    /* A physical-adapter index well past any plausible LDA chain length. The
     * contract is implementation-specific (some clamp, some reject), so only
     * trace the outcome. */
    memset(&qvm, 0, sizeof(qvm));
    qvm.hProcess = NULL;
    qvm.hAdapter = hAdapter;
    qvm.MemorySegmentGroup = D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL;
    qvm.PhysicalAdapterIndex = 0xFFFF;
    MISC_TRACE_RESULT(p(&qvm),
                      "D3DKMTQueryVideoMemoryInfo(high PhysicalAdapterIndex)");

    CloseAdapter(hAdapter);
}

START_TEST(misc)
{
    /* Output duplication */
    Test_CreateOutputDupl_NullArg();
    Test_CreateOutputDupl_BadHandle();
    Test_DestroyOutputDupl_NullArg();
    Test_DestroyOutputDupl_BadHandle();
    Test_OutputDuplGetFrameInfo_NullArg();
    Test_OutputDuplGetFrameInfo_BadHandle();
    Test_OutputDuplGetMetaData_NullArg();
    Test_OutputDuplGetMetaData_BadHandle();
    Test_OutputDuplGetPointerShapeData_NullArg();
    Test_OutputDuplGetPointerShapeData_BadHandle();
    Test_OutputDuplReleaseFrame_NullArg();
    Test_OutputDuplReleaseFrame_BadHandle();
    Test_OutputDuplPresent_NullArg();
    Test_OutputDuplPresent_BadHandle();

    /* GPU clock calibration */
    Test_QueryClockCalibration_NullArg();
    Test_QueryClockCalibration_BadHandle();
    Test_QueryClockCalibration_RealAdapter();

    /* Stable power state */
    Test_SetStablePowerState_NullArg();
    Test_SetStablePowerState_BadHandle();
    Test_SetStablePowerState_RealAdapter();

    /* Process scheduling priority class (multi-argument) */
    Test_GetProcessSchedulingPriorityClass_Current();
    Test_GetProcessSchedulingPriorityClass_NullOut();
    Test_GetProcessSchedulingPriorityClass_BadHandle();
    Test_SetProcessSchedulingPriorityClass_RoundTrip();
    Test_SetProcessSchedulingPriorityClass_BadHandle();
    Test_SetProcessSchedulingPriorityClass_InvalidClass();

    /* In-process context scheduling priority */
    Test_SetContextInProcessSchedulingPriority_NullArg();
    Test_SetContextInProcessSchedulingPriority_BadHandle();

    /* Trim notifications */
    Test_RegisterTrimNotification_NullArg();
    Test_RegisterTrimNotification_BadDevice();
    Test_UnregisterTrimNotification_NullArg();
    Test_UnregisterTrimNotification_BadHandle();
    Test_TrimNotification_Lifecycle();

    /* Budget-change notifications */
    Test_RegisterBudgetChangeNotification_NullArg();
    Test_RegisterBudgetChangeNotification_BadDevice();
    Test_UnregisterBudgetChangeNotification_NullArg();
    Test_UnregisterBudgetChangeNotification_BadHandle();
    Test_BudgetChangeNotification_Lifecycle();

    /* QueryStatistics (NULL only) */
    Test_QueryStatistics_NullArg();

    /* InvalidateCache */
    Test_InvalidateCache_NullArg();
    Test_InvalidateCache_BadHandle();

    /* QueryVideoMemoryInfo budget edge cases */
    Test_QueryVideoMemoryInfo_InvalidSegmentGroup();
    Test_QueryVideoMemoryInfo_NonLocal();
    Test_QueryVideoMemoryInfo_HighPhysAdapterIndex();
}
