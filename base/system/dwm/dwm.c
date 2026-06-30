/*
 * PROJECT:     ReactOS Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM service entry point, D3DKMT device initialization
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Overview
 * --------
 * dwm.exe is a user-mode service process that acts as the display
 * compositor for ReactOS.  It is the functional equivalent of
 * Windows Vista+'s Desktop Window Manager.
 *
 * Service lifecycle:
 *   1. SCM starts dwm.exe
 *   2. main() calls StartServiceCtrlDispatcher
 *   3. DwmServiceMain() is invoked by the SCM
 *   4. DwmInitD3dkmt() opens the WDDM adapter and creates a D3DKMT device
 *   5. DwmCompositorInit() allocates the full-screen composition buffer
 *   6. DwmCompositorRun() enters the composition loop
 *   7. On SERVICE_CONTROL_STOP, the loop exits and everything is torn down
 *
 * D3DKMT adapter open
 * -------------------
 * We use D3DKMTOpenAdapterFromGdiDisplayName with "\\\\.\\DISPLAY1"
 * which is the standard GDI display name for the primary monitor.
 * This routes through gdi32 -> win32k -> dxgkrnl and returns an
 * adapter handle plus VidPN source ID.  From there we create a
 * D3DKMT device which gives us the ability to allocate surfaces
 * and call D3DKMTPresent.
 */

#include "dwm.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwm);

/* ========================================================================
 * Global DWM context -- single instance per process
 * ====================================================================== */
static DWM_CONTEXT g_DwmContext;

/* ========================================================================
 * Service control handler
 * ====================================================================== */

/*
 * DwmServiceCtrlHandler
 *
 * Called by the SCM when a control code is sent to the DWM service.
 * We handle STOP (initiates graceful shutdown) and INTERROGATE
 * (reports current status).
 */
VOID WINAPI
DwmServiceCtrlHandler(
    _In_ DWORD dwControl)
{
    switch (dwControl)
    {
        case SERVICE_CONTROL_STOP:
            TRACE("DwmServiceCtrlHandler: SERVICE_CONTROL_STOP received\n");
            g_DwmContext.ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_DwmContext.ServiceStatus.dwWaitHint = 5000;
            SetServiceStatus(g_DwmContext.hServiceStatus,
                             &g_DwmContext.ServiceStatus);

            /* Signal the composition loop to exit */
            g_DwmContext.ShutdownRequested = TRUE;
            if (g_DwmContext.hStopEvent)
                SetEvent(g_DwmContext.hStopEvent);
            break;

        case SERVICE_CONTROL_INTERROGATE:
            /* Just report current status */
            SetServiceStatus(g_DwmContext.hServiceStatus,
                             &g_DwmContext.ServiceStatus);
            break;

        default:
            break;
    }
}

/* ========================================================================
 * D3DKMT initialization
 *
 * Opens the primary WDDM adapter and creates a D3DKMT device.
 * This is the user-mode equivalent of what the real Windows DWM does
 * when it calls D3DKMTOpenAdapterFromHdc / D3DKMTCreateDevice.
 * ====================================================================== */

BOOL
DwmInitD3dkmt(
    _Inout_ PDWM_CONTEXT pCtx)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_CREATEDEVICE CreateDevice;
    NTSTATUS Status;

    TRACE("DwmInitD3dkmt: Opening primary display adapter\n");

    /*
     * Step 1: Open the primary display adapter.
     *
     * "\\\\.\\DISPLAY1" is the standard GDI device name for the primary
     * monitor.  D3DKMTOpenAdapterFromGdiDisplayName resolves this to the
     * WDDM adapter handle through win32k and dxgkrnl.
     */
    ZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    wcscpy(OpenAdapter.DeviceName, L"\\\\.\\DISPLAY1");

    Status = D3DKMTOpenAdapterFromGdiDisplayName(&OpenAdapter);
    if (Status != STATUS_SUCCESS)
    {
        ERR("DwmInitD3dkmt: D3DKMTOpenAdapterFromGdiDisplayName failed "
            "with status 0x%08lX\n", Status);
        return FALSE;
    }

    pCtx->D3dkmt.hAdapter = OpenAdapter.hAdapter;
    pCtx->D3dkmt.AdapterLuid = OpenAdapter.AdapterLuid;
    pCtx->D3dkmt.VidPnSourceId = OpenAdapter.VidPnSourceId;

    TRACE("DwmInitD3dkmt: Adapter opened: hAdapter=0x%X LUID=%08X:%08X "
          "VidPnSrc=%u\n",
          OpenAdapter.hAdapter,
          OpenAdapter.AdapterLuid.HighPart,
          OpenAdapter.AdapterLuid.LowPart,
          OpenAdapter.VidPnSourceId);

    /*
     * Step 2: Create a D3DKMT device on the adapter.
     *
     * The device handle is needed for D3DKMTCreateAllocation and
     * D3DKMTPresent.  We request VSync notification (RequestVSync)
     * so present can synchronize with the display refresh.
     */
    ZeroMemory(&CreateDevice, sizeof(CreateDevice));
    CreateDevice.hAdapter = pCtx->D3dkmt.hAdapter;
    CreateDevice.Flags.LegacyMode = 0;
    CreateDevice.Flags.RequestVSync = 1;

    Status = D3DKMTCreateDevice(&CreateDevice);
    if (Status != STATUS_SUCCESS)
    {
        D3DKMT_CLOSEADAPTER CloseAdapter;

        ERR("DwmInitD3dkmt: D3DKMTCreateDevice failed with status 0x%08lX\n",
            Status);

        /* Clean up the adapter handle on failure */
        CloseAdapter.hAdapter = pCtx->D3dkmt.hAdapter;
        D3DKMTCloseAdapter(&CloseAdapter);
        pCtx->D3dkmt.hAdapter = 0;
        return FALSE;
    }

    pCtx->D3dkmt.hDevice = CreateDevice.hDevice;
    pCtx->D3dkmt.Initialized = TRUE;

    TRACE("DwmInitD3dkmt: Device created: hDevice=0x%X\n",
          CreateDevice.hDevice);

    return TRUE;
}

/* ========================================================================
 * D3DKMT shutdown
 * ====================================================================== */

VOID
DwmShutdownD3dkmt(
    _Inout_ PDWM_CONTEXT pCtx)
{
    NTSTATUS Status;

    if (!pCtx->D3dkmt.Initialized)
        return;

    TRACE("DwmShutdownD3dkmt: Tearing down D3DKMT state\n");

    /* Destroy the D3DKMT device */
    if (pCtx->D3dkmt.hDevice != 0)
    {
        D3DKMT_DESTROYDEVICE DestroyDevice;
        DestroyDevice.hDevice = pCtx->D3dkmt.hDevice;

        Status = D3DKMTDestroyDevice(&DestroyDevice);
        if (Status != STATUS_SUCCESS)
        {
            ERR("DwmShutdownD3dkmt: D3DKMTDestroyDevice failed 0x%08lX\n",
                Status);
        }
        pCtx->D3dkmt.hDevice = 0;
    }

    /* Close the adapter */
    if (pCtx->D3dkmt.hAdapter != 0)
    {
        D3DKMT_CLOSEADAPTER CloseAdapter;
        CloseAdapter.hAdapter = pCtx->D3dkmt.hAdapter;

        Status = D3DKMTCloseAdapter(&CloseAdapter);
        if (Status != STATUS_SUCCESS)
        {
            ERR("DwmShutdownD3dkmt: D3DKMTCloseAdapter failed 0x%08lX\n",
                Status);
        }
        pCtx->D3dkmt.hAdapter = 0;
    }

    pCtx->D3dkmt.Initialized = FALSE;
    TRACE("DwmShutdownD3dkmt: Done\n");
}

/* ========================================================================
 * Query screen dimensions from the adapter's display mode list
 *
 * Falls back to GetSystemMetrics if the D3DKMT query fails.
 * ====================================================================== */

static VOID
DwmQueryScreenDimensions(
    _Inout_ PDWM_CONTEXT pCtx)
{
    /*
     * For Phase 1, use GetSystemMetrics to determine the screen size.
     * This is reliable and matches what GDI reports for the primary
     * display.  Phase 2 can use D3DKMTGetDisplayModeList for the
     * current mode's exact pixel dimensions.
     */
    pCtx->ScreenWidth = (UINT)GetSystemMetrics(SM_CXSCREEN);
    pCtx->ScreenHeight = (UINT)GetSystemMetrics(SM_CYSCREEN);

    /* Sanity check -- avoid zero-dimension composition buffers */
    if (pCtx->ScreenWidth == 0 || pCtx->ScreenHeight == 0)
    {
        WARN("DwmQueryScreenDimensions: GetSystemMetrics returned zero; "
             "using 1024x768 fallback\n");
        pCtx->ScreenWidth = 1024;
        pCtx->ScreenHeight = 768;
    }

    TRACE("DwmQueryScreenDimensions: %ux%u\n",
          pCtx->ScreenWidth, pCtx->ScreenHeight);
}

/* ========================================================================
 * Report service status helper
 * ====================================================================== */

static VOID
DwmReportStatus(
    _Inout_ PDWM_CONTEXT pCtx,
    _In_ DWORD dwCurrentState,
    _In_ DWORD dwWin32ExitCode,
    _In_ DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    pCtx->ServiceStatus.dwCurrentState = dwCurrentState;
    pCtx->ServiceStatus.dwWin32ExitCode = dwWin32ExitCode;
    pCtx->ServiceStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        pCtx->ServiceStatus.dwControlsAccepted = 0;
    else
        pCtx->ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if (dwCurrentState == SERVICE_RUNNING || dwCurrentState == SERVICE_STOPPED)
        pCtx->ServiceStatus.dwCheckPoint = 0;
    else
        pCtx->ServiceStatus.dwCheckPoint = dwCheckPoint++;

    SetServiceStatus(pCtx->hServiceStatus, &pCtx->ServiceStatus);
}

/* ========================================================================
 * DwmServiceMain -- SCM entry point
 * ====================================================================== */

VOID WINAPI
DwmServiceMain(
    _In_ DWORD dwArgc,
    _In_ LPWSTR *lpszArgv)
{
    UNREFERENCED_PARAMETER(dwArgc);
    UNREFERENCED_PARAMETER(lpszArgv);

    TRACE("DwmServiceMain: Entry\n");

    /* Initialize the global context */
    ZeroMemory(&g_DwmContext, sizeof(g_DwmContext));

    /* Register the service control handler */
    g_DwmContext.hServiceStatus = RegisterServiceCtrlHandlerW(
        DWM_SERVICE_NAME, DwmServiceCtrlHandler);

    if (!g_DwmContext.hServiceStatus)
    {
        ERR("DwmServiceMain: RegisterServiceCtrlHandler failed (error %lu)\n",
            GetLastError());
        return;
    }

    /* Set initial service status */
    g_DwmContext.ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_DwmContext.ServiceStatus.dwServiceSpecificExitCode = 0;
    DwmReportStatus(&g_DwmContext, SERVICE_START_PENDING, NO_ERROR, 10000);

    /* Create the stop event (manual reset, initially non-signaled) */
    g_DwmContext.hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_DwmContext.hStopEvent)
    {
        ERR("DwmServiceMain: CreateEvent failed (error %lu)\n",
            GetLastError());
        DwmReportStatus(&g_DwmContext, SERVICE_STOPPED,
                        GetLastError(), 0);
        return;
    }

    /* Initialize the window list critical section */
    InitializeCriticalSection(&g_DwmContext.csWindowList);

    /* Get our session ID */
    ProcessIdToSessionId(GetCurrentProcessId(), &g_DwmContext.SessionId);
    TRACE("DwmServiceMain: Session ID = %lu\n", g_DwmContext.SessionId);

    /*
     * Step 1: Read configuration from registry.
     */
    DwmReadConfig(&g_DwmContext);
    DwmReportStatus(&g_DwmContext, SERVICE_START_PENDING, NO_ERROR, 10000);

    /*
     * Step 2: Create named events for session coordination.
     */
    if (!DwmCreateNamedEvents(&g_DwmContext))
    {
        ERR("DwmServiceMain: Failed to create DWM named events\n");
        DeleteCriticalSection(&g_DwmContext.csWindowList);
        CloseHandle(g_DwmContext.hStopEvent);
        g_DwmContext.hStopEvent = NULL;
        DwmReportStatus(&g_DwmContext, SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    /*
     * Step 3: Initialize D3DKMT -- open adapter and create device.
     */
    if (!DwmInitD3dkmt(&g_DwmContext))
    {
        ERR("DwmServiceMain: D3DKMT init failed; composition unavailable\n");
        DwmDestroyNamedEvents(&g_DwmContext);
        DeleteCriticalSection(&g_DwmContext.csWindowList);
        CloseHandle(g_DwmContext.hStopEvent);
        g_DwmContext.hStopEvent = NULL;
        DwmReportStatus(&g_DwmContext, SERVICE_STOPPED, ERROR_NOT_SUPPORTED, 0);
        return;
    }

    /*
     * Step 4: Elevate GPU scheduling priority.
     */
    DwmSetGpuPriority(&g_DwmContext);

    /*
     * Step 5: Query screen dimensions and initialize the compositor.
     */
    DwmQueryScreenDimensions(&g_DwmContext);
    DwmReportStatus(&g_DwmContext, SERVICE_START_PENDING, NO_ERROR, 5000);

    if (!DwmCompositorInit(&g_DwmContext))
    {
        ERR("DwmServiceMain: Compositor init failed\n");
        DwmShutdownD3dkmt(&g_DwmContext);
        DwmDestroyNamedEvents(&g_DwmContext);
        DeleteCriticalSection(&g_DwmContext.csWindowList);
        CloseHandle(g_DwmContext.hStopEvent);
        g_DwmContext.hStopEvent = NULL;
        DwmReportStatus(&g_DwmContext, SERVICE_STOPPED, ERROR_NOT_ENOUGH_MEMORY, 0);
        return;
    }

    /*
     * Step 6: Register window classes and create the DWM windows.
     */
    if (!DwmRegisterWindowClasses(&g_DwmContext) ||
        !DwmCreateMainWindow(&g_DwmContext))
    {
        ERR("DwmServiceMain: Failed to initialize DWM windows\n");
        DwmDestroyMainWindow(&g_DwmContext);
        DwmUnregisterWindowClasses(&g_DwmContext);
        DwmCompositorShutdown(&g_DwmContext);
        DwmShutdownD3dkmt(&g_DwmContext);
        DwmDestroyNamedEvents(&g_DwmContext);
        DeleteCriticalSection(&g_DwmContext.csWindowList);
        CloseHandle(g_DwmContext.hStopEvent);
        g_DwmContext.hStopEvent = NULL;
        DwmReportStatus(&g_DwmContext, SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    /*
     * Step 7: Create the LPC API port for client communication.
     */
    if (!DwmCreateLpcPort(&g_DwmContext))
    {
        ERR("DwmServiceMain: Failed to create DWM LPC port\n");
        DwmDestroyMainWindow(&g_DwmContext);
        DwmUnregisterWindowClasses(&g_DwmContext);
        DwmCompositorShutdown(&g_DwmContext);
        DwmShutdownD3dkmt(&g_DwmContext);
        DwmDestroyNamedEvents(&g_DwmContext);
        DeleteCriticalSection(&g_DwmContext.csWindowList);
        CloseHandle(g_DwmContext.hStopEvent);
        g_DwmContext.hStopEvent = NULL;
        DwmReportStatus(&g_DwmContext, SERVICE_STOPPED, GetLastError(), 0);
        return;
    }

    /*
     * Step 8: Signal the startup event so clients know DWM is ready.
     */
    if (g_DwmContext.hStartupEvent)
    {
        SetEvent(g_DwmContext.hStartupEvent);
        TRACE("DwmServiceMain: Startup event signaled\n");
    }

    /*
     * Step 9: Report SERVICE_RUNNING and enter the message loop.
     */
    DwmReportStatus(&g_DwmContext, SERVICE_RUNNING, NO_ERROR, 0);
    TRACE("DwmServiceMain: Service running\n");

    DwmMessageLoop(&g_DwmContext);

    /*
     * Step 10: Shutdown -- clean up everything in reverse order.
     */
    TRACE("DwmServiceMain: Shutting down\n");

    DwmDestroyLpcPort(&g_DwmContext);
    DwmDestroyMainWindow(&g_DwmContext);
    DwmUnregisterWindowClasses(&g_DwmContext);
    DwmCompositorShutdown(&g_DwmContext);
    DwmShutdownD3dkmt(&g_DwmContext);
    DwmDestroyNamedEvents(&g_DwmContext);

    DeleteCriticalSection(&g_DwmContext.csWindowList);

    if (g_DwmContext.hInstanceMutex)
    {
        ReleaseMutex(g_DwmContext.hInstanceMutex);
        CloseHandle(g_DwmContext.hInstanceMutex);
        g_DwmContext.hInstanceMutex = NULL;
    }

    if (g_DwmContext.hStopEvent)
    {
        CloseHandle(g_DwmContext.hStopEvent);
        g_DwmContext.hStopEvent = NULL;
    }

    DwmReportStatus(&g_DwmContext, SERVICE_STOPPED, NO_ERROR, 0);
    TRACE("DwmServiceMain: Service stopped\n");
}

/* ========================================================================
 * Registry configuration
 *
 * Reads DWM settings from HKLM\Software\Microsoft\Windows\DWM.
 * Missing values get sensible defaults.
 * ====================================================================== */

static DWORD
DwmRegReadDword(
    _In_ HKEY hKey,
    _In_ LPCWSTR pszValue,
    _In_ DWORD dwDefault)
{
    DWORD dwType, dwData, cbData = sizeof(dwData);
    LONG lResult;

    lResult = RegQueryValueExW(hKey, pszValue, NULL, &dwType,
                               (LPBYTE)&dwData, &cbData);
    if (lResult == ERROR_SUCCESS && dwType == REG_DWORD)
        return dwData;

    return dwDefault;
}

VOID
DwmReadConfig(
    _Inout_ PDWM_CONTEXT pCtx)
{
    HKEY hKey;
    LONG lResult;

    TRACE("DwmReadConfig: Reading configuration\n");

    /* Defaults */
    pCtx->Config.CompositionEnabled = TRUE;
    pCtx->Config.ColorizationColor = 0x6B74B8FC;  /* Default Aero blue */
    pCtx->Config.ColorizationColorBalance = 8;
    pCtx->Config.ColorizationBlurBalance = 49;
    pCtx->Config.ColorizationGlassReflectionIntensity = 50;

    lResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, DWM_REGISTRY_PATH,
                            0, KEY_READ, &hKey);
    if (lResult != ERROR_SUCCESS)
    {
        TRACE("DwmReadConfig: Registry key not found, using defaults\n");
        return;
    }

    pCtx->Config.CompositionEnabled =
        DwmRegReadDword(hKey, L"Composition", 1) != 0;
    pCtx->Config.DisallowComposition =
        DwmRegReadDword(hKey, L"DisallowComposition", 0) != 0;
    pCtx->Config.DisallowAnimations =
        DwmRegReadDword(hKey, L"DisallowAnimations", 0) != 0;
    pCtx->Config.ColorizationColor =
        DwmRegReadDword(hKey, L"ColorizationColor", 0x6B74B8FC);
    pCtx->Config.ColorizationColorBalance =
        DwmRegReadDword(hKey, L"ColorizationColorBalance", 8);
    pCtx->Config.ColorizationAfterglow =
        DwmRegReadDword(hKey, L"ColorizationAfterglow", 0x6B74B8FC);
    pCtx->Config.ColorizationAfterglowBalance =
        DwmRegReadDword(hKey, L"ColorizationAfterglowBalance", 43);
    pCtx->Config.ColorizationBlurBalance =
        DwmRegReadDword(hKey, L"ColorizationBlurBalance", 49);
    pCtx->Config.ColorizationGlassReflectionIntensity =
        DwmRegReadDword(hKey, L"ColorizationGlassReflectionIntensity", 50);
    pCtx->Config.ColorizationOpaqueBlend =
        DwmRegReadDword(hKey, L"ColorizationOpaqueBlend", 0) != 0;
    pCtx->Config.DisableLockingMemory =
        DwmRegReadDword(hKey, L"DisableLockingMemory", 0) != 0;
    pCtx->Config.UseDPIScaling =
        DwmRegReadDword(hKey, L"UseDPIScaling", 0) != 0;

    RegCloseKey(hKey);

    TRACE("DwmReadConfig: Composition=%d Color=0x%08lX\n",
          pCtx->Config.CompositionEnabled,
          pCtx->Config.ColorizationColor);
}

/* ========================================================================
 * Named events
 *
 * DWM creates session-scoped named events for client synchronization:
 *   - DwmStartupEvent: signaled when DWM is initialized and ready
 *   - DwmComposedEvent_<PID>: signaled after each composition cycle
 * ====================================================================== */

BOOL
DwmCreateNamedEvents(
    _Inout_ PDWM_CONTEXT pCtx)
{
    WCHAR szEventName[128];
    SECURITY_ATTRIBUTES sa;
    BOOL CreatedAny = FALSE;

    TRACE("DwmCreateNamedEvents: Creating events\n");

    /* Security attributes: allow all access so client processes can open */
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    /* DwmStartupEvent -- clients wait on this to know DWM is ready */
    _snwprintf(szEventName, ARRAYSIZE(szEventName),
               L"Global\\DwmStartupEvent_%lu", pCtx->SessionId);
    pCtx->hStartupEvent = CreateEventW(&sa, TRUE, FALSE, szEventName);
    if (!pCtx->hStartupEvent)
    {
        WARN("DwmCreateNamedEvents: CreateEvent(Startup) failed %lu\n",
             GetLastError());
    }
    else
    {
        CreatedAny = TRUE;
    }

    /* DwmComposedEvent -- signaled each frame for DwmFlush callers */
    _snwprintf(szEventName, ARRAYSIZE(szEventName),
               L"Global\\DwmComposedEvent_%lu", GetCurrentProcessId());
    pCtx->hComposedEvent = CreateEventW(&sa, FALSE, FALSE, szEventName);
    if (!pCtx->hComposedEvent)
    {
        WARN("DwmCreateNamedEvents: CreateEvent(Composed) failed %lu\n",
             GetLastError());
    }
    else
    {
        CreatedAny = TRUE;
    }

    return CreatedAny;
}

VOID
DwmDestroyNamedEvents(
    _Inout_ PDWM_CONTEXT pCtx)
{
    if (pCtx->hStartupEvent)
    {
        CloseHandle(pCtx->hStartupEvent);
        pCtx->hStartupEvent = NULL;
    }
    if (pCtx->hComposedEvent)
    {
        CloseHandle(pCtx->hComposedEvent);
        pCtx->hComposedEvent = NULL;
    }
}

/* ========================================================================
 * LPC API port
 *
 * DWM creates a waitable LPC port that clients (via dwmapi.dll)
 * connect to for DWM service requests (thumbnails, blur, timing, etc.).
 *
 * Port name pattern: \BaseNamedObjects\Dwm-SSSS-ApiPort-PPPP
 * where SSSS = session ID, PPPP = process ID (both as 4-digit hex).
 *
 * The LPC server runs in a dedicated thread, processing incoming
 * requests in a NtReplyWaitReceivePort loop.
 * ====================================================================== */

static DWORD WINAPI
DwmLpcServerThread(
    _In_ LPVOID lpParameter)
{
    PDWM_CONTEXT pCtx = (PDWM_CONTEXT)lpParameter;
    /* LPC message buffer -- max size per the DWM protocol */
    BYTE MsgBuffer[sizeof(PORT_MESSAGE) + DWM_LPC_MAX_MSG_LENGTH];
    PPORT_MESSAGE pMsg = (PPORT_MESSAGE)MsgBuffer;
    NTSTATUS Status;
    HANDLE hConnectedPort = NULL;

    TRACE("DwmLpcServerThread: Starting LPC server on %ls\n",
          pCtx->Lpc.PortName);

    while (pCtx->Lpc.Running)
    {
        /* Wait for the next LPC message (connect request or data) */
        Status = NtReplyWaitReceivePort(
            pCtx->Lpc.hServerPort,
            NULL,          /* PortContext (not used) */
            NULL,          /* ReplyMessage (NULL = just wait) */
            pMsg);         /* ReceiveMessage */

        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_PORT_CLOSED ||
                Status == STATUS_INVALID_HANDLE)
            {
                /* Port was closed -- shutdown */
                TRACE("DwmLpcServerThread: Port closed, exiting\n");
                break;
            }
            WARN("DwmLpcServerThread: NtReplyWaitReceivePort = 0x%08lX\n",
                 Status);
            continue;
        }

        /*
         * Check message type.  LPC_CONNECTION_REQUEST means a new
         * client (dwmapi.dll) is connecting.
         */
        if (pMsg->u2.s2.Type == LPC_CONNECTION_REQUEST)
        {
            TRACE("DwmLpcServerThread: Connection request\n");

            /* Accept the connection */
            Status = NtAcceptConnectPort(
                &hConnectedPort,
                NULL,   /* PortContext */
                pMsg,
                TRUE,   /* AcceptConnection */
                NULL,   /* ServerView */
                NULL);  /* ClientView */

            if (NT_SUCCESS(Status))
            {
                NtCompleteConnectPort(hConnectedPort);
                TRACE("DwmLpcServerThread: Client connected\n");
            }
            else
            {
                WARN("DwmLpcServerThread: AcceptConnectPort = 0x%08lX\n",
                     Status);
            }
        }
        else if (pMsg->u2.s2.Type == LPC_REQUEST)
        {
            HRESULT ReplyStatus = DWM_E_COMPOSITIONDISABLED;

            /*
             * Client request.  Phase 1 has no request dispatcher, so report
             * composition-disabled explicitly instead of pretending the
             * operation completed.
             *
             * dwmapi.dll uses tags:
             *   0x40xxxxxx = fire-and-forget (no reply needed)
             *   0x80xxxxxx = request-reply (must send response)
             */
            TRACE("DwmLpcServerThread: Request, DataLength=%u\n",
                  pMsg->u1.s1.DataLength);

            if (pMsg->u1.s1.DataLength > DWM_LPC_MAX_MSG_LENGTH)
                ReplyStatus = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);

            pMsg->u1.s1.DataLength = sizeof(HRESULT);
            pMsg->u1.s1.TotalLength = sizeof(PORT_MESSAGE) + sizeof(HRESULT);
            *(HRESULT*)((PBYTE)pMsg + sizeof(PORT_MESSAGE)) = ReplyStatus;

            Status = NtReplyPort(pCtx->Lpc.hServerPort, pMsg);
            if (!NT_SUCCESS(Status))
            {
                WARN("DwmLpcServerThread: NtReplyPort = 0x%08lX\n", Status);
            }
        }
        else if (pMsg->u2.s2.Type == LPC_PORT_CLOSED ||
                 pMsg->u2.s2.Type == LPC_CLIENT_DIED)
        {
            TRACE("DwmLpcServerThread: Client disconnected (type=%u)\n",
                  pMsg->u2.s2.Type);
        }
    }

    TRACE("DwmLpcServerThread: Exiting\n");
    return 0;
}

BOOL
DwmCreateLpcPort(
    _Inout_ PDWM_CONTEXT pCtx)
{
    UNICODE_STRING PortName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    /* Build the port name */
    _snwprintf(pCtx->Lpc.PortName, ARRAYSIZE(pCtx->Lpc.PortName),
               DWM_PORT_NAME_FMT,
               (USHORT)pCtx->SessionId,
               (USHORT)(GetCurrentProcessId() & 0xFFFF));

    TRACE("DwmCreateLpcPort: Creating port %ls\n", pCtx->Lpc.PortName);

    RtlInitUnicodeString(&PortName, pCtx->Lpc.PortName);
    ZeroMemory(&ObjectAttributes, sizeof(ObjectAttributes));
    ObjectAttributes.Length = sizeof(ObjectAttributes);
    ObjectAttributes.ObjectName = &PortName;

    Status = NtCreateWaitablePort(
        &pCtx->Lpc.hServerPort,
        &ObjectAttributes,
        DWM_LPC_MAX_CONNECT_INFO,
        DWM_LPC_MAX_MSG_LENGTH,
        0);  /* MaxPoolUsage */

    if (!NT_SUCCESS(Status))
    {
        WARN("DwmCreateLpcPort: NtCreateWaitablePort = 0x%08lX\n", Status);
        pCtx->Lpc.hServerPort = NULL;
        return FALSE;
    }

    /* Start the LPC server thread */
    pCtx->Lpc.Running = TRUE;
    pCtx->Lpc.hServerThread = CreateThread(
        NULL, 0, DwmLpcServerThread, pCtx, 0, NULL);

    if (!pCtx->Lpc.hServerThread)
    {
        WARN("DwmCreateLpcPort: CreateThread failed %lu\n", GetLastError());
        NtClose(pCtx->Lpc.hServerPort);
        pCtx->Lpc.hServerPort = NULL;
        pCtx->Lpc.Running = FALSE;
        return FALSE;
    }

    TRACE("DwmCreateLpcPort: LPC server started\n");
    return TRUE;
}

VOID
DwmDestroyLpcPort(
    _Inout_ PDWM_CONTEXT pCtx)
{
    if (!pCtx->Lpc.Running)
        return;

    TRACE("DwmDestroyLpcPort: Shutting down LPC server\n");

    pCtx->Lpc.Running = FALSE;

    /* Closing the port handle unblocks NtReplyWaitReceivePort */
    if (pCtx->Lpc.hServerPort)
    {
        NtClose(pCtx->Lpc.hServerPort);
        pCtx->Lpc.hServerPort = NULL;
    }

    /* Wait for the server thread to exit */
    if (pCtx->Lpc.hServerThread)
    {
        WaitForSingleObject(pCtx->Lpc.hServerThread, 5000);
        CloseHandle(pCtx->Lpc.hServerThread);
        pCtx->Lpc.hServerThread = NULL;
    }
}

/* ========================================================================
 * GPU scheduling priority
 *
 * DWM sets its process to HIGH GPU scheduling priority so its
 * composition work preempts normal application GPU work.
 * ====================================================================== */

VOID
DwmSetGpuPriority(
    _Inout_ PDWM_CONTEXT pCtx)
{
    NTSTATUS Status;

    TRACE("DwmSetGpuPriority: Setting HIGH GPU priority\n");

    Status = D3DKMTSetProcessSchedulingPriorityClass(
        GetCurrentProcess(),
        D3DKMT_SCHEDULINGPRIORITYCLASS_HIGH);

    if (NT_SUCCESS(Status))
    {
        pCtx->GpuPrioritySet = TRUE;
        TRACE("DwmSetGpuPriority: GPU priority set to HIGH\n");
    }
    else
    {
        WARN("DwmSetGpuPriority: D3DKMTSetProcessSchedulingPriorityClass "
             "failed 0x%08lX\n", Status);
    }
}

/* ========================================================================
 * Window classes and main window
 *
 * DWM registers window classes and creates:
 *   - "DWMWindow": The main DWM overlay window (full-screen)
 *   - "DWM Notification Window": For internal DWM notifications
 * ====================================================================== */

static LRESULT CALLBACK
DwmMainWndProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
            TRACE("DwmMainWndProc: WM_CREATE\n");
            return 0;

        case WM_DESTROY:
            TRACE("DwmMainWndProc: WM_DESTROY\n");
            PostQuitMessage(0);
            return 0;

        case WM_DISPLAYCHANGE:
            TRACE("DwmMainWndProc: WM_DISPLAYCHANGE %ux%u %ubpp\n",
                  LOWORD(lParam), HIWORD(lParam), (UINT)wParam);
            /* Reconfigure compositor for new display mode */
            return 0;

        case WM_POWERBROADCAST:
            TRACE("DwmMainWndProc: WM_POWERBROADCAST %Iu\n", wParam);
            return TRUE;

        default:
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

static LRESULT CALLBACK
DwmNotifyWndProc(
    _In_ HWND hWnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

BOOL
DwmRegisterWindowClasses(
    _Inout_ PDWM_CONTEXT pCtx)
{
    WNDCLASSEXW wc;

    TRACE("DwmRegisterWindowClasses\n");

    /* Register "DWMWindow" class */
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DwmMainWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = DWM_WINDOW_CLASS;

    pCtx->WndState.atomMainClass = RegisterClassExW(&wc);
    if (!pCtx->WndState.atomMainClass)
    {
        WARN("DwmRegisterWindowClasses: RegisterClassExW(DWMWindow) "
             "failed %lu\n", GetLastError());
    }

    /* Register "DWM Notification Window" class */
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DwmNotifyWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = DWM_NOTIFICATION_CLASS;

    pCtx->WndState.atomNotifyClass = RegisterClassExW(&wc);
    if (!pCtx->WndState.atomNotifyClass)
    {
        WARN("DwmRegisterWindowClasses: RegisterClassExW(Notify) "
             "failed %lu\n", GetLastError());
    }

    /* Register custom window message for redirection hints */
    pCtx->WndState.WM_DWM_REDIR_CHANGED =
        RegisterWindowMessageW(L"DwmRedirectionEnvironmentChangedHint");

    return (pCtx->WndState.atomMainClass != 0);
}

VOID
DwmUnregisterWindowClasses(
    _Inout_ PDWM_CONTEXT pCtx)
{
    HINSTANCE hInst = GetModuleHandleW(NULL);

    if (pCtx->WndState.atomMainClass)
    {
        UnregisterClassW(DWM_WINDOW_CLASS, hInst);
        pCtx->WndState.atomMainClass = 0;
    }
    if (pCtx->WndState.atomNotifyClass)
    {
        UnregisterClassW(DWM_NOTIFICATION_CLASS, hInst);
        pCtx->WndState.atomNotifyClass = 0;
    }
}

BOOL
DwmCreateMainWindow(
    _Inout_ PDWM_CONTEXT pCtx)
{
    TRACE("DwmCreateMainWindow\n");

    if (!pCtx->WndState.atomMainClass)
        return FALSE;

    /*
     * Create the main DWM window.  This is a message-only window in
     * Phase 1 (not visible).  In a full DWM, this would be a full-screen
     * layered window used as the composition target.
     */
    pCtx->WndState.hMainWindow = CreateWindowExW(
        0,                          /* dwExStyle */
        DWM_WINDOW_CLASS,           /* lpClassName */
        L"Desktop Window Manager",  /* lpWindowName */
        WS_POPUP,                   /* dwStyle */
        0, 0, 0, 0,                /* x, y, w, h (message-only) */
        HWND_MESSAGE,              /* hWndParent (message-only parent) */
        NULL,                      /* hMenu */
        GetModuleHandleW(NULL),    /* hInstance */
        NULL);                     /* lpParam */

    if (!pCtx->WndState.hMainWindow)
    {
        WARN("DwmCreateMainWindow: CreateWindowExW failed %lu\n",
             GetLastError());
        return FALSE;
    }

    /* Create notification window */
    if (pCtx->WndState.atomNotifyClass)
    {
        pCtx->WndState.hNotifyWindow = CreateWindowExW(
            0,
            DWM_NOTIFICATION_CLASS,
            L"DWM Notification",
            WS_POPUP,
            0, 0, 0, 0,
            HWND_MESSAGE,
            NULL,
            GetModuleHandleW(NULL),
            NULL);
    }

    TRACE("DwmCreateMainWindow: Main=%p Notify=%p\n",
          pCtx->WndState.hMainWindow,
          pCtx->WndState.hNotifyWindow);

    return TRUE;
}

VOID
DwmDestroyMainWindow(
    _Inout_ PDWM_CONTEXT pCtx)
{
    if (pCtx->WndState.hNotifyWindow)
    {
        DestroyWindow(pCtx->WndState.hNotifyWindow);
        pCtx->WndState.hNotifyWindow = NULL;
    }
    if (pCtx->WndState.hMainWindow)
    {
        DestroyWindow(pCtx->WndState.hMainWindow);
        pCtx->WndState.hMainWindow = NULL;
    }
}

/* ========================================================================
 * Message-based main loop
 *
 * This replaces the simple timer-based DwmCompositorRun loop with a
 * proper MsgWaitForMultipleObjectsEx loop, matching the Windows DWM
 * architecture.  The loop waits on:
 *   - Window messages (for the DWM window and notifications)
 *   - Stop event (for service shutdown)
 *   - Composition timer (for frame pacing)
 * ====================================================================== */

VOID
DwmMessageLoop(
    _Inout_ PDWM_CONTEXT pCtx)
{
    HANDLE WaitHandles[2];
    DWORD HandleCount;
    DWORD WaitResult;
    MSG msg;

    TRACE("DwmMessageLoop: Entering message loop (%ux%u)\n",
          pCtx->ScreenWidth, pCtx->ScreenHeight);

    WaitHandles[0] = pCtx->hStopEvent;
    HandleCount = 1;

    while (!pCtx->ShutdownRequested)
    {
        /*
         * MsgWaitForMultipleObjectsEx is the heart of the DWM main loop.
         * It combines waiting for kernel objects (stop event, VSync)
         * with the Windows message pump.
         */
        WaitResult = MsgWaitForMultipleObjectsEx(
            HandleCount,
            WaitHandles,
            DWM_COMPOSE_INTERVAL_MS,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);

        if (WaitResult == WAIT_OBJECT_0)
        {
            /* Stop event signaled -- exit */
            TRACE("DwmMessageLoop: Stop event signaled\n");
            break;
        }

        /* Process any pending window messages */
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                TRACE("DwmMessageLoop: WM_QUIT received\n");
                pCtx->ShutdownRequested = TRUE;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (pCtx->ShutdownRequested)
            break;

        /*
         * Compose and present a frame.
         * This is the same path as the old DwmCompositorRun loop,
         * but now integrated with the message pump.
         */
        DwmUpdateWindowList(pCtx);
        DwmComposeSingleFrame(pCtx);

        /* Signal the composed event for DwmFlush callers */
        if (pCtx->hComposedEvent)
            SetEvent(pCtx->hComposedEvent);
    }

    TRACE("DwmMessageLoop: Exited (composed=%I64u, presented=%I64u)\n",
          pCtx->FramesComposed, pCtx->FramesPresented);
}

/* ========================================================================
 * main() -- process entry point
 *
 * dwm.exe is started by the SCM as a service.  main() just registers
 * the service dispatch table and blocks until the service exits.
 * ====================================================================== */

int WINAPI
wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    SERVICE_TABLE_ENTRYW ServiceTable[] =
    {
        { DWM_SERVICE_NAME, DwmServiceMain },
        { NULL, NULL }
    };

    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    TRACE("dwm.exe: Starting service dispatcher\n");

    if (!StartServiceCtrlDispatcherW(ServiceTable))
    {
        ERR("dwm.exe: StartServiceCtrlDispatcher failed (error %lu)\n",
            GetLastError());

        /*
         * If we were not started by the SCM (e.g. run manually from
         * command line for debugging), run the compositor directly.
         */
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
        {
            TRACE("dwm.exe: Not started by SCM -- running standalone "
                  "for debugging\n");

            ZeroMemory(&g_DwmContext, sizeof(g_DwmContext));
            g_DwmContext.hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            InitializeCriticalSection(&g_DwmContext.csWindowList);
            ProcessIdToSessionId(GetCurrentProcessId(),
                                 &g_DwmContext.SessionId);

            DwmReadConfig(&g_DwmContext);
            DwmCreateNamedEvents(&g_DwmContext);

            if (DwmInitD3dkmt(&g_DwmContext))
            {
                DwmSetGpuPriority(&g_DwmContext);
                DwmQueryScreenDimensions(&g_DwmContext);

                if (DwmCompositorInit(&g_DwmContext))
                {
                    DwmRegisterWindowClasses(&g_DwmContext);
                    DwmCreateMainWindow(&g_DwmContext);
                    DwmCreateLpcPort(&g_DwmContext);

                    if (g_DwmContext.hStartupEvent)
                        SetEvent(g_DwmContext.hStartupEvent);

                    TRACE("dwm.exe: Standalone compositor running\n");
                    DwmMessageLoop(&g_DwmContext);

                    DwmDestroyLpcPort(&g_DwmContext);
                    DwmDestroyMainWindow(&g_DwmContext);
                    DwmUnregisterWindowClasses(&g_DwmContext);
                }
            }

            DwmCompositorShutdown(&g_DwmContext);
            DwmShutdownD3dkmt(&g_DwmContext);
            DwmDestroyNamedEvents(&g_DwmContext);
            DeleteCriticalSection(&g_DwmContext.csWindowList);

            if (g_DwmContext.hStopEvent)
                CloseHandle(g_DwmContext.hStopEvent);
        }

        return 1;
    }

    return 0;
}
