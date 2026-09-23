/*
 * COPYRIGHT:            See COPYING in the top level directory
 * PROJECT:              ReactOS kernel
 * FILE:                 lib/opengl32/icdload.c
 * PURPOSE:              OpenGL32 lib, ICD dll loader
 */

#include "opengl32.h"

#include <d3dkmthk.h>
#include <reactos/dwmframe.h>
#include <winreg.h>

WINE_DEFAULT_DEBUG_CHANNEL(opengl32);

/* based off https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/frontends/wgl/gldrv.h */
typedef struct
{
    ULONG Version;                    /*!< Driver interface version */
    ULONG DriverVersion;              /*!< Driver version */
    WCHAR DriverName[MAX_PATH + 1];   /*!< Driver name */
} Drv_Opengl_Info, *pDrv_Opengl_Info;

#ifndef OPENGL_GETINFO_DRVNAME
#define OPENGL_GETINFO_DRVNAME 0
#endif

typedef enum
{
    OGL_CD_NOT_QUERIED,
    OGL_CD_NONE,
    OGL_CD_ROSSWI,
    OGL_CD_CUSTOM_ICD
} CUSTOM_DRIVER_STATE;

static CRITICAL_SECTION icdload_cs = {NULL, -1, 0, 0, 0, 0};
static struct ICD_Data* ICD_Data_List = NULL;
static DWORD IcdLoadingThreadId;
static const WCHAR OpenGLDrivers_Key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers";
static const WCHAR CustomDrivers_Key[] = L"SOFTWARE\\ReactOS\\OpenGL";
static Drv_Opengl_Info CustomDrvInfo;
static CUSTOM_DRIVER_STATE CustomDriverState = OGL_CD_NOT_QUERIED;

static BOOL
IntGetWddmIcdInfo(
    HDC hdc,
    Drv_Opengl_Info *DrvInfo,
    WCHAR DllName[MAX_PATH],
    DWORD *Flags,
    LUID *AdapterLuid);

static void APIENTRY wglSetCurrentValue(PVOID value)
{
    IntSetCurrentICDPrivate(value);
}

static PVOID APIENTRY wglGetCurrentValue()
{
    return IntGetCurrentICDPrivate();
}

static DHGLRC APIENTRY wglGetDHGLRC(HGLRC hglrc)
{
    struct wgl_context* context = get_context(hglrc);

    if (!context)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return context->dhglrc;
}

typedef HRESULT (WINAPI *PFN_DWM_DX_GET_WINDOW_SHARED_SURFACE)(
    HWND, LUID, HMONITOR, DWORD, UINT *, HANDLE *, ULONGLONG *);
typedef HRESULT (WINAPI *PFN_DWM_DX_UPDATE_WINDOW_SHARED_SURFACE)(
    HWND, ULONGLONG, DWORD, HMONITOR, const RECT *);

static INIT_ONCE DwmDxInitOnce = INIT_ONCE_STATIC_INIT;
static PFN_DWM_DX_GET_WINDOW_SHARED_SURFACE DwmDxGetWindowSharedSurface;
static PFN_DWM_DX_UPDATE_WINDOW_SHARED_SURFACE DwmDxUpdateWindowSharedSurface;
static LONG DwmDxPresentFailureLogged;

typedef struct _WGL_ASYNC_PRESENT
{
    LIST_ENTRY Entry;
    HWND Window;
    HANDLE CompletionEvent;
    ULONGLONG UpdateId;
    DWORD Flags;
    RECT UpdateRect;
} WGL_ASYNC_PRESENT, *PWGL_ASYNC_PRESENT;

/*
 * Publishing a presented frame means waiting for the ICD's completion event
 * and then handing the surface to the compositor.  A thread-pool item per
 * frame both cost a kernel event plus a dispatch on every SwapBuffers and
 * left the publish order to the pool, which may run two frames' callbacks
 * concurrently and hand the compositor an older update last.  One owning
 * thread drains a FIFO instead, and its records keep their events, so a
 * steady stream of frames allocates nothing and publishes strictly in order.
 */
#define WGL_PUBLISH_QUEUE_LIMIT 8
#define WGL_PUBLISH_QUEUE_STALL_MS 5000

typedef struct _WGL_PUBLISH_QUEUE
{
    CRITICAL_SECTION Lock;
    LIST_ENTRY Pending;
    LIST_ENTRY Spare;
    HANDLE Wake;
    HANDLE Slots;
    HANDLE Thread;
    HMODULE Module;
    BOOL Usable;
} WGL_PUBLISH_QUEUE;

static WGL_PUBLISH_QUEUE DwmDxPublishQueue;
static INIT_ONCE DwmDxPublishOnce = INIT_ONCE_STATIC_INIT;

static VOID
IntReportDwmDxPresentFailure(const char *Stage, HRESULT Result)
{
    if (InterlockedCompareExchange(&DwmDxPresentFailureLogged, 1, 0) == 0)
    {
        ERR("DWM shared-surface present failed at %s, hr=%#lx, error=%lu\n",
            Stage, (ULONG)Result, GetLastError());
    }
}

/* Completes one record and keeps its event for the next frame. */
static BOOL
IntPublishDwmDxPresentRecycle(PWGL_ASYNC_PRESENT Present)
{
    HRESULT Result;
    BOOL Succeeded;

    if (WaitForSingleObject(Present->CompletionEvent, INFINITE) != WAIT_OBJECT_0)
    {
        IntReportDwmDxPresentFailure("completion_wait",
                                     HRESULT_FROM_WIN32(GetLastError()));
        Result = E_FAIL;
    }
    else
    {
        Result = DwmDxUpdateWindowSharedSurface(Present->Window,
                                                 Present->UpdateId,
                                                 Present->Flags,
                                                 NULL,
                                                 &Present->UpdateRect);
        if (FAILED(Result))
            IntReportDwmDxPresentFailure("publish", Result);
    }
    if (FAILED(Result))
    {
        (void)DwmDxUpdateWindowSharedSurface(Present->Window,
                                             Present->UpdateId,
                                             DWM_DX_UPDATE_CANCEL,
                                             NULL,
                                             NULL);
    }
    Succeeded = SUCCEEDED(Result);

    EnterCriticalSection(&DwmDxPublishQueue.Lock);
    InsertHeadList(&DwmDxPublishQueue.Spare, &Present->Entry);
    LeaveCriticalSection(&DwmDxPublishQueue.Lock);
    ReleaseSemaphore(DwmDxPublishQueue.Slots, 1, NULL);
    return Succeeded;
}

static DWORD WINAPI
IntPublishDwmDxQueueThread(PVOID Parameter)
{
    (void)Parameter;
    for (;;)
    {
        PWGL_ASYNC_PRESENT Present = NULL;
        PLIST_ENTRY Entry;

        EnterCriticalSection(&DwmDxPublishQueue.Lock);
        if (!IsListEmpty(&DwmDxPublishQueue.Pending))
        {
            Entry = RemoveHeadList(&DwmDxPublishQueue.Pending);
            Present = CONTAINING_RECORD(Entry, WGL_ASYNC_PRESENT, Entry);
        }
        LeaveCriticalSection(&DwmDxPublishQueue.Lock);

        if (Present == NULL)
        {
            (void)WaitForSingleObject(DwmDxPublishQueue.Wake, INFINITE);
            continue;
        }
        (void)IntPublishDwmDxPresentRecycle(Present);
    }
    return 0;
}

static BOOL CALLBACK
IntInitDwmDxPublishQueue(PINIT_ONCE Once, PVOID Parameter, PVOID *Context)
{
    DWORD ThreadId;

    (void)Once; (void)Parameter; (void)Context;
    InitializeListHead(&DwmDxPublishQueue.Pending);
    InitializeListHead(&DwmDxPublishQueue.Spare);
    if (!InitializeCriticalSectionAndSpinCount(&DwmDxPublishQueue.Lock, 4000))
        return TRUE;
    DwmDxPublishQueue.Wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    DwmDxPublishQueue.Slots = CreateSemaphoreW(NULL, WGL_PUBLISH_QUEUE_LIMIT,
                                               WGL_PUBLISH_QUEUE_LIMIT, NULL);
    if (DwmDxPublishQueue.Wake == NULL || DwmDxPublishQueue.Slots == NULL)
        goto Failed;
    /* The process-wide worker can outlive the application's last OpenGL
     * context and FreeLibrary call. Keep its code and queue storage loaded
     * for its lifetime, including when it is asleep with no pending frames. */
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (LPCWSTR)IntPublishDwmDxQueueThread,
                            &DwmDxPublishQueue.Module))
        goto Failed;
    DwmDxPublishQueue.Thread = CreateThread(NULL, 0, IntPublishDwmDxQueueThread,
                                            NULL, 0, &ThreadId);
    if (DwmDxPublishQueue.Thread == NULL)
        goto Failed;
    DwmDxPublishQueue.Usable = TRUE;
    return TRUE;

Failed:
    if (DwmDxPublishQueue.Module != NULL)
        FreeLibrary(DwmDxPublishQueue.Module);
    DwmDxPublishQueue.Module = NULL;
    if (DwmDxPublishQueue.Wake != NULL)
        CloseHandle(DwmDxPublishQueue.Wake);
    if (DwmDxPublishQueue.Slots != NULL)
        CloseHandle(DwmDxPublishQueue.Slots);
    DwmDxPublishQueue.Wake = NULL;
    DwmDxPublishQueue.Slots = NULL;
    DeleteCriticalSection(&DwmDxPublishQueue.Lock);
    return TRUE;
}

/*
 * Hands back a record whose completion event is already created and reset.
 * Returns NULL when the queue is unusable or already holds the frames the
 * compositor has yet to consume, so the caller publishes inline and the
 * producer feels the backpressure instead of growing this list.
 */
static VOID IntReleaseDwmDxPublishRecord(PWGL_ASYNC_PRESENT Present);

static PWGL_ASYNC_PRESENT
IntAcquireDwmDxPublishRecord(void)
{
    PWGL_ASYNC_PRESENT Present = NULL;
    PLIST_ENTRY Entry;

    (void)InitOnceExecuteOnce(&DwmDxPublishOnce, IntInitDwmDxPublishQueue,
                              NULL, NULL);
    if (!DwmDxPublishQueue.Usable)
        return NULL;

    /* Waiting here throttles the producer to the frames the compositor can
     * still take. Publishing inline instead would overtake everything this
     * queue already holds. */
    if (WaitForSingleObject(DwmDxPublishQueue.Slots,
                            WGL_PUBLISH_QUEUE_STALL_MS) != WAIT_OBJECT_0)
        return NULL;

    EnterCriticalSection(&DwmDxPublishQueue.Lock);
    if (!IsListEmpty(&DwmDxPublishQueue.Spare))
    {
        Entry = RemoveHeadList(&DwmDxPublishQueue.Spare);
        Present = CONTAINING_RECORD(Entry, WGL_ASYNC_PRESENT, Entry);
    }
    LeaveCriticalSection(&DwmDxPublishQueue.Lock);

    if (Present != NULL)
    {
        if (ResetEvent(Present->CompletionEvent))
            return Present;
        IntReleaseDwmDxPublishRecord(Present);
        return NULL;
    }

    Present = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Present));
    if (Present != NULL)
    {
        Present->CompletionEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (Present->CompletionEvent != NULL)
            return Present;
        HeapFree(GetProcessHeap(), 0, Present);
    }
    ReleaseSemaphore(DwmDxPublishQueue.Slots, 1, NULL);
    return NULL;
}

static VOID
IntReleaseDwmDxPublishRecord(PWGL_ASYNC_PRESENT Present)
{
    EnterCriticalSection(&DwmDxPublishQueue.Lock);
    InsertHeadList(&DwmDxPublishQueue.Spare, &Present->Entry);
    LeaveCriticalSection(&DwmDxPublishQueue.Lock);
    ReleaseSemaphore(DwmDxPublishQueue.Slots, 1, NULL);
}

/* Publishes in submission order behind whatever this queue already holds. */
static VOID
IntQueueDwmDxPublishRecord(PWGL_ASYNC_PRESENT Present)
{
    EnterCriticalSection(&DwmDxPublishQueue.Lock);
    InsertTailList(&DwmDxPublishQueue.Pending, &Present->Entry);
    LeaveCriticalSection(&DwmDxPublishQueue.Lock);
    SetEvent(DwmDxPublishQueue.Wake);
}

static NTSTATUS
IntOpenAdapterFromWindowMonitor(
    HDC hdc,
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *OpenAdapter)
{
    MONITORINFOEXW MonitorInfo;
    HMONITOR Monitor;
    HWND Window;

    Window = WindowFromDC(hdc);
    if (Window == NULL)
        return STATUS_INVALID_HANDLE;

    Monitor = MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST);
    if (Monitor == NULL)
        return STATUS_NOT_FOUND;

    RtlZeroMemory(&MonitorInfo, sizeof(MonitorInfo));
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    if (!GetMonitorInfoW(Monitor, (MONITORINFO *)&MonitorInfo) ||
        MonitorInfo.szDevice[0] == UNICODE_NULL)
    {
        return STATUS_NOT_FOUND;
    }

    RtlZeroMemory(OpenAdapter, sizeof(*OpenAdapter));
    lstrcpynW(OpenAdapter->DeviceName, MonitorInfo.szDevice,
              ARRAYSIZE(OpenAdapter->DeviceName));
    return D3DKMTOpenAdapterFromGdiDisplayName(OpenAdapter);
}

static VOID
IntCloseAdapter(D3DKMT_HANDLE Adapter)
{
    D3DKMT_CLOSEADAPTER CloseAdapter;

    if (Adapter == 0)
        return;

    RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
    CloseAdapter.hAdapter = Adapter;
    (void)D3DKMTCloseAdapter(&CloseAdapter);
}

static BOOL CALLBACK
IntLoadDwmDxCallbacks(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context)
{
    HMODULE Module;

    UNREFERENCED_PARAMETER(InitOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    Module = LoadLibraryW(L"dwmapi.dll");
    if (Module == NULL)
        return TRUE;

    DwmDxGetWindowSharedSurface =
        (PFN_DWM_DX_GET_WINDOW_SHARED_SURFACE)
            GetProcAddress(Module, (LPCSTR)(ULONG_PTR)100);
    DwmDxUpdateWindowSharedSurface =
        (PFN_DWM_DX_UPDATE_WINDOW_SHARED_SURFACE)
            GetProcAddress(Module, (LPCSTR)(ULONG_PTR)101);
    return TRUE;
}

static VOID APIENTRY
wglGetAdapterLuid(HDC hdc, LUID *AdapterLuid)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenDisplayAdapter;
    D3DKMT_OPENADAPTERFROMHDC OpenAdapter;
    Drv_Opengl_Info DrvInfo;
    WCHAR DllName[MAX_PATH];
    DWORD Flags;
    NTSTATUS Status;

    if (AdapterLuid == NULL)
        return;
    AdapterLuid->LowPart = 0;
    AdapterLuid->HighPart = 0;
    if (hdc == NULL)
        return;

    /* The ICD calls this on every present; answer from the identity captured
     * when the ICD was bound to this DC instead of re-opening the adapter. */
    {
        struct wgl_dc_data *DcData = IntGetDcData(hdc);

        if (DcData != NULL && DcData->icd_data != NULL &&
            DcData->AdapterLuidValid)
        {
            *AdapterLuid = DcData->AdapterLuid;
            return;
        }
    }

    RtlZeroMemory(&DrvInfo, sizeof(DrvInfo));
    RtlZeroMemory(DllName, sizeof(DllName));
    if (IntGetWddmIcdInfo(hdc, &DrvInfo, DllName, &Flags, AdapterLuid))
        return;

    Status = IntOpenAdapterFromWindowMonitor(hdc, &OpenDisplayAdapter);
    if (NT_SUCCESS(Status) && OpenDisplayAdapter.hAdapter != 0)
    {
        *AdapterLuid = OpenDisplayAdapter.AdapterLuid;
        IntCloseAdapter(OpenDisplayAdapter.hAdapter);
        return;
    }

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    OpenAdapter.hDc = hdc;
    if (NT_SUCCESS(D3DKMTOpenAdapterFromHdc(&OpenAdapter)) &&
        OpenAdapter.hAdapter != 0)
    {
        *AdapterLuid = OpenAdapter.AdapterLuid;
        IntCloseAdapter(OpenAdapter.hAdapter);
        return;
    }

}

static BOOL
IntQueryWddmOpenGlInfo(
    D3DKMT_HANDLE Adapter,
    D3DKMT_OPENGLINFO *OpenGlInfo)
{
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    NTSTATUS Status;

    if (Adapter == 0 || OpenGlInfo == NULL)
        return FALSE;

    RtlZeroMemory(OpenGlInfo, sizeof(*OpenGlInfo));
    RtlZeroMemory(&QueryInfo, sizeof(QueryInfo));
    QueryInfo.hAdapter = Adapter;
    QueryInfo.Type = KMTQAITYPE_UMOPENGLINFO;
    QueryInfo.pPrivateDriverData = OpenGlInfo;
    QueryInfo.PrivateDriverDataSize = sizeof(*OpenGlInfo);
    Status = D3DKMTQueryAdapterInfo(&QueryInfo);
    OpenGlInfo->UmdOpenGlIcdFileName[
        ARRAYSIZE(OpenGlInfo->UmdOpenGlIcdFileName) - 1] = UNICODE_NULL;
    return NT_SUCCESS(Status) && OpenGlInfo->UmdOpenGlIcdFileName[0] != UNICODE_NULL;
}

static BOOL
IntFindWddmRenderIcd(
    D3DKMT_OPENGLINFO *OpenGlInfo,
    LUID *AdapterLuid)
{
    D3DKMT_ENUMADAPTERS2 Enumeration;
    D3DKMT_ADAPTERINFO *Adapters = NULL;
    D3DKMT_QUERYADAPTERINFO QueryInfo;
    D3DKMT_ADAPTERTYPE AdapterType;
    D3DKMT_OPENGLINFO CandidateInfo;
    ULONG AdapterCount;
    ULONG FoundCount = 0;
    ULONG Index;
    BOOL Found = FALSE;
    NTSTATUS Status;

    RtlZeroMemory(&Enumeration, sizeof(Enumeration));
    Status = D3DKMTEnumAdapters2(&Enumeration);
    if (!NT_SUCCESS(Status) || Enumeration.NumAdapters == 0)
        return FALSE;

    AdapterCount = Enumeration.NumAdapters;
    if ((SIZE_T)AdapterCount > ~(SIZE_T)0 / sizeof(*Adapters))
        return FALSE;

    Adapters = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         (SIZE_T)AdapterCount * sizeof(*Adapters));
    if (Adapters == NULL)
        return FALSE;

    Enumeration.NumAdapters = AdapterCount;
    Enumeration.pAdapters = Adapters;
    Status = D3DKMTEnumAdapters2(&Enumeration);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (Enumeration.NumAdapters < AdapterCount)
        AdapterCount = Enumeration.NumAdapters;

    for (Index = 0; Index < AdapterCount; ++Index)
    {
        BOOL TypeKnown;

        RtlZeroMemory(&AdapterType, sizeof(AdapterType));
        RtlZeroMemory(&QueryInfo, sizeof(QueryInfo));
        QueryInfo.hAdapter = Adapters[Index].hAdapter;
        QueryInfo.Type = KMTQAITYPE_ADAPTERTYPE;
        QueryInfo.pPrivateDriverData = &AdapterType;
        QueryInfo.PrivateDriverDataSize = sizeof(AdapterType);
        TypeKnown = NT_SUCCESS(D3DKMTQueryAdapterInfo(&QueryInfo));
        if (TypeKnown &&
            (!AdapterType.RenderSupported || AdapterType.SoftwareDevice))
        {
            continue;
        }

        if (!IntQueryWddmOpenGlInfo(Adapters[Index].hAdapter, &CandidateInfo))
            continue;

        /*
         * More than one render adapter can publish an ICD once every GPU in
         * the machine starts.  None of them owns the display (this search
         * only runs when the display adapter has no ICD), so there is no
         * better tie-break than enumeration order, which follows adapter
         * start order; refusing to pick would turn every multi-GPU machine
         * into a software renderer.
         */
        ++FoundCount;
        if (FoundCount == 1)
        {
            *OpenGlInfo = CandidateInfo;
            if (AdapterLuid != NULL)
                *AdapterLuid = Adapters[Index].AdapterLuid;
        }
    }

    Found = FoundCount != 0;

Cleanup:
    for (Index = 0; Index < AdapterCount; ++Index)
        IntCloseAdapter(Adapters[Index].hAdapter);
    HeapFree(GetProcessHeap(), 0, Adapters);
    return Found;
}

static BOOL APIENTRY
wglPresentBuffersDirect(HDC hdc,
                        struct ICD_Data *IcdData,
                        const WGL_PRESENTBUFFERS_CB *CallbackData)
{
    WGL_PRESENTBUFFERS PresentData;

    RtlZeroMemory(&PresentData, sizeof(PresentData));
    PresentData.AdapterLuid = CallbackData->AdapterLuid;
    PresentData.PrivateData = CallbackData->PrivateData;

    return IcdData->DrvPresentBuffers(hdc, &PresentData);
}

static BOOL APIENTRY
wglPresentBuffers(HDC hdc, WGL_PRESENTBUFFERS_CB *CallbackData)
{
    struct ICD_Data *IcdData;
    PWGL_ASYNC_PRESENT AsyncPresent = NULL;
    WGL_PRESENTBUFFERS PresentData;
    WGL_PRESENTBUFFERS2 PresentData2;
    HWND Window;
    POINT ClientOrigin = {0, 0};
    RECT ClientRect;
    RECT WindowRect;
    RECT UpdateRect;
    HANDLE SharedSurface = NULL;
    ULONGLONG UpdateId = 0;
    UINT Format = 0;
    HRESULT Result;

    if (hdc == NULL || CallbackData == NULL ||
        (CallbackData->Version != 2 && CallbackData->Version != 3) ||
        CallbackData->SyncType > 1)
    {
        return FALSE;
    }

    Window = WindowFromDC(hdc);
    if (Window == NULL)
        return FALSE;

    /* The ICD update is relative to the window surface, including its
     * non-client area. The shared surface contains only client pixels. */
    if (!ClientToScreen(Window, &ClientOrigin) || !GetWindowRect(Window, &WindowRect))
        return FALSE;
    UpdateRect = CallbackData->UpdateRect;
    OffsetRect(&UpdateRect, WindowRect.left - ClientOrigin.x, WindowRect.top - ClientOrigin.y);

    /* The ICD presents on every frame; use the ICD already bound to this DC
     * instead of re-running ICD discovery (adapter open/query/close). */
    {
        struct wgl_dc_data *DcData = IntGetDcData(hdc);

        IcdData = (DcData != NULL && DcData->icd_data != NULL) ?
            DcData->icd_data : IntGetIcdData(hdc, NULL, NULL);
    }
    if (IcdData == NULL || IcdData->DrvPresentBuffers == NULL)
    {
        IntReportDwmDxPresentFailure("icd_callback", E_NOINTERFACE);
        return FALSE;
    }

    /* An empty client has no shared allocation, and the compositor never
     * redirects an invisible window (including message-only windows). Still
     * call the ICD so it can finish the present and update its framebuffer. */
    if (!GetClientRect(Window, &ClientRect))
        return FALSE;
    if (IsIconic(Window) || IsRectEmpty(&ClientRect) || !IsWindowVisible(Window))
        return wglPresentBuffersDirect(hdc, IcdData, CallbackData);

    /* DWM's registered output swapchain is already the completed desktop.
     * Keep it in its render allocation so win32k can promote the ensuing KMT
     * present to a direct scanout flip. */
    if (GetPropW(Window, DWM_PROP_GPU_OUTPUT) != NULL)
        return wglPresentBuffersDirect(hdc, IcdData, CallbackData);

    (void)InitOnceExecuteOnce(&DwmDxInitOnce, IntLoadDwmDxCallbacks,
                              NULL, NULL);
    if (DwmDxGetWindowSharedSurface == NULL ||
        DwmDxUpdateWindowSharedSurface == NULL)
    {
        IntReportDwmDxPresentFailure("load_callbacks", E_NOINTERFACE);
        return wglPresentBuffersDirect(hdc, IcdData, CallbackData);
    }

    Result = DwmDxGetWindowSharedSurface(Window,
                                         CallbackData->AdapterLuid,
                                         NULL,
                                         CallbackData->SyncType,
                                         &Format,
                                         &SharedSurface,
                                         &UpdateId);
    if (Result == S_FALSE)
        return TRUE; /* compositor intentionally dropped this immediate frame */
    if (FAILED(Result))
    {
        IntReportDwmDxPresentFailure("get_surface", Result);
        if (Result == HRESULT_FROM_WIN32(ERROR_NOT_READY) ||
            Result == DWM_E_COMPOSITIONDISABLED)
        {
            return wglPresentBuffersDirect(hdc, IcdData, CallbackData);
        }
        return FALSE;
    }
    if (SharedSurface == NULL || UpdateId == 0)
    {
        IntReportDwmDxPresentFailure("get_surface_contract", E_UNEXPECTED);
        return FALSE;
    }

    RtlZeroMemory(&PresentData, sizeof(PresentData));
    PresentData.hSurface = SharedSurface;
    PresentData.AdapterLuid = CallbackData->AdapterLuid;
    PresentData.PresentToken = UpdateId;
    PresentData.PrivateData = CallbackData->PrivateData;
    if (CallbackData->Version >= 3 && IcdData->DrvPresentBuffers2 != NULL)
    {
        /* NULL only means this frame publishes inline, which is correct but
         * slower; it is not a presentation failure. */
        AsyncPresent = IntAcquireDwmDxPublishRecord();
        if (AsyncPresent == NULL && DwmDxPublishQueue.Usable)
        {
            /* The queue is alive but has not drained. Dropping this frame
             * keeps the published order intact; an inline publish would not. */
            IntReportDwmDxPresentFailure("publish_congested", E_PENDING);
            goto Cancel;
        }
        if (AsyncPresent != NULL)
        {
            AsyncPresent->Window = Window;
            AsyncPresent->UpdateId = UpdateId;
            AsyncPresent->Flags = CallbackData->SyncType;
            AsyncPresent->UpdateRect = UpdateRect;
        }
    }

    if (AsyncPresent != NULL)
    {
        RtlZeroMemory(&PresentData2, sizeof(PresentData2));
        PresentData2.Size = sizeof(PresentData2);
        PresentData2.Version = WGL_PRESENTBUFFERS2_VERSION;
        PresentData2.hSurface = PresentData.hSurface;
        PresentData2.AdapterLuid = PresentData.AdapterLuid;
        PresentData2.PresentToken = PresentData.PresentToken;
        PresentData2.PrivateData = PresentData.PrivateData;
        PresentData2.CompletionEvent = AsyncPresent->CompletionEvent;
        Result = IcdData->DrvPresentBuffers2(hdc, &PresentData2) ? S_OK : E_FAIL;
    }
    else
    {
        Result = IcdData->DrvPresentBuffers(hdc, &PresentData) ? S_OK : E_FAIL;
    }
    if (FAILED(Result))
    {
        IntReportDwmDxPresentFailure("icd_present", E_FAIL);
        goto Cancel;
    }
    if (AsyncPresent != NULL)
    {
        IntQueueDwmDxPublishRecord(AsyncPresent);
        return TRUE;
    }

    Result = DwmDxUpdateWindowSharedSurface(Window,
                                             UpdateId,
                                             CallbackData->SyncType,
                                             NULL,
                                             &UpdateRect);
    if (SUCCEEDED(Result))
        return TRUE;

    IntReportDwmDxPresentFailure("publish", Result);

Cancel:
    /* The record never reached the queue, so its event stays for reuse. */
    if (AsyncPresent != NULL)
        IntReleaseDwmDxPublishRecord(AsyncPresent);
    (void)DwmDxUpdateWindowSharedSurface(Window,
                                         UpdateId,
                                         DWM_DX_UPDATE_CANCEL,
                                         NULL,
                                         NULL);
    return FALSE;
}

/*
 * WDDM display miniports publish their OpenGL ICD through the adapter's
 * software key.  Querying it through D3DKMT keeps the choice tied to the HDC
 * instead of applying a process-wide registry override, which is important
 * when a hardware miniport fails and BasicDisplay owns the desktop instead.
 */
static BOOL
IntGetWddmIcdInfo(
    HDC hdc,
    Drv_Opengl_Info *DrvInfo,
    WCHAR DllName[MAX_PATH],
    DWORD *Flags,
    LUID *AdapterLuid)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenDisplayAdapter;
    D3DKMT_OPENADAPTERFROMHDC OpenAdapter;
    D3DKMT_OPENGLINFO OpenGlInfo;
    D3DKMT_HANDLE Adapter = 0;
    LUID SelectedLuid;
    BOOL Found = FALSE;
    NTSTATUS Status = STATUS_NOT_FOUND;

    if (!hdc || !DrvInfo || !DllName || !Flags)
        return FALSE;

    SelectedLuid.LowPart = 0;
    SelectedLuid.HighPart = 0;

    memset(&OpenAdapter, 0, sizeof(OpenAdapter));
    OpenAdapter.hDc = hdc;
    Status = D3DKMTOpenAdapterFromHdc(&OpenAdapter);
    if (NT_SUCCESS(Status) && OpenAdapter.hAdapter != 0)
    {
        Adapter = OpenAdapter.hAdapter;
        SelectedLuid = OpenAdapter.AdapterLuid;
    }

    Found = IntQueryWddmOpenGlInfo(Adapter, &OpenGlInfo);
    IntCloseAdapter(Adapter);

    /* A redirected window DC is backed by a DIB, not by the scan-out PDEV.
     * Resolve that window's monitor when the HDC-selected adapter does not
     * publish an ICD, preserving the normal HDC path for direct display DCs. */
    if (!Found)
    {
        Status = IntOpenAdapterFromWindowMonitor(hdc, &OpenDisplayAdapter);
        if (NT_SUCCESS(Status) && OpenDisplayAdapter.hAdapter != 0)
        {
            SelectedLuid = OpenDisplayAdapter.AdapterLuid;
            Found = IntQueryWddmOpenGlInfo(OpenDisplayAdapter.hAdapter,
                                           &OpenGlInfo);
            IntCloseAdapter(OpenDisplayAdapter.hAdapter);
        }
    }

    /* If BasicDisplay owns the scan-out PDEV, the HDC and monitor both resolve
     * to that software adapter.  A separately started hardware render adapter
     * can still publish the ICD that should service windowed OpenGL. */
    if (!Found)
        Found = IntFindWddmRenderIcd(&OpenGlInfo, &SelectedLuid);

    if (!Found)
        return FALSE;

    lstrcpynW(DllName, OpenGlInfo.UmdOpenGlIcdFileName, MAX_PATH);
    DrvInfo->Version = OpenGlInfo.Version;
    /* WDDM's query returns one ICD interface version.  The legacy loader's
     * validation callback consumes that value through DriverVersion. */
    DrvInfo->DriverVersion = OpenGlInfo.Version;
    lstrcpynW(DrvInfo->DriverName, OpenGlInfo.UmdOpenGlIcdFileName,
              ARRAYSIZE(DrvInfo->DriverName));
    *Flags = OpenGlInfo.Flags;
    if (AdapterLuid != NULL)
        *AdapterLuid = SelectedLuid;
    return TRUE;
}

/* GDI entry points (win32k) */
extern INT APIENTRY GdiDescribePixelFormat(HDC hdc, INT ipfd, UINT cjpfd, PPIXELFORMATDESCRIPTOR ppfd);
extern BOOL APIENTRY GdiSetPixelFormat(HDC hdc, INT ipfd);
extern BOOL APIENTRY GdiSwapBuffers(HDC hdc);

/* Retrieves the ICD data (driver version + relevant DLL entry points) for a device context */
struct ICD_Data* IntGetIcdData(
    HDC hdc,
    LUID *AdapterLuid,
    BOOL *AdapterLuidValid)
{
    int ret;
    DWORD dwInput, dwValueType, Version, DriverVersion, Flags;
    Drv_Opengl_Info DrvInfo;
    pDrv_Opengl_Info pDrvInfo;
    struct ICD_Data* data;
    DWORD CurrentThreadId;
    HKEY OglKey = NULL;
    HKEY DrvKey = NULL, CustomKey = NULL;
    WCHAR DllName[MAX_PATH];
    WCHAR WddmDllName[MAX_PATH];
    DWORD WddmFlags = 0;
    BOOL WddmIcd = FALSE;
    LUID WddmLuid = {0, 0};
    BOOL (WINAPI *DrvValidateVersion)(DWORD);
    void (WINAPI *DrvSetCallbackProcs)(int nProcs, PROC* pProcs);

    if (AdapterLuid != NULL)
        memset(AdapterLuid, 0, sizeof(*AdapterLuid));
    if (AdapterLuidValid != NULL)
        *AdapterLuidValid = FALSE;

    /* The following code is ReactOS specific and allows us to easily load an arbitrary ICD:
     * It checks HKCU\Software\ReactOS\OpenGL for a custom ICD and will always load it
     * no matter what driver the DC is associated with. It can also force using the
     * built-in Software Implementation*/
    if(CustomDriverState == OGL_CD_NOT_QUERIED)
    {
        /* Only do this once so there's not any significant performance penalty */
        CustomDriverState = OGL_CD_NONE;
        memset(&CustomDrvInfo, 0, sizeof(Drv_Opengl_Info));

        ret = RegOpenKeyExW(HKEY_CURRENT_USER, CustomDrivers_Key, 0, KEY_READ, &CustomKey);
        if(ret != ERROR_SUCCESS)
            goto custom_end;

        dwInput = sizeof(CustomDrvInfo.DriverName);
        ret = RegQueryValueExW(CustomKey, L"", 0, &dwValueType, (LPBYTE)CustomDrvInfo.DriverName, &dwInput);
        RegCloseKey(CustomKey);

        if((ret != ERROR_SUCCESS) || (dwValueType != REG_SZ) || !wcslen(CustomDrvInfo.DriverName))
            goto custom_end;

        if(!_wcsicmp(CustomDrvInfo.DriverName, L"ReactOS Software Implementation"))
        {
            /* Always announce the fact that we're forcing ROSSWI */
            ERR("Forcing ReactOS Software Implementation\n");
            CustomDriverState = OGL_CD_ROSSWI;
            return NULL;
        }

        ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, OpenGLDrivers_Key, 0, KEY_READ, &OglKey);
        if(ret != ERROR_SUCCESS)
            goto custom_end;

        ret = RegOpenKeyExW(OglKey, CustomDrvInfo.DriverName, 0, KEY_READ, &OglKey);
        if(ret != ERROR_SUCCESS)
            goto custom_end;

        dwInput = sizeof(CustomDrvInfo.Version);
        ret = RegQueryValueExW(OglKey, L"Version", 0, &dwValueType, (LPBYTE)&CustomDrvInfo.Version, &dwInput);
        if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            goto custom_end;

        dwInput = sizeof(DriverVersion);
        ret = RegQueryValueExW(OglKey, L"DriverVersion", 0, &dwValueType, (LPBYTE)&CustomDrvInfo.DriverVersion, &dwInput);
        CustomDriverState = OGL_CD_CUSTOM_ICD;

        /* Always announce the fact that we're overriding the default driver */
        ERR("Overriding the default OGL ICD with %S\n", CustomDrvInfo.DriverName);

custom_end:
        if(OglKey)
            RegCloseKey(OglKey);
        if(CustomKey)
            RegCloseKey(CustomKey);
    }

    /* If there's a custom ICD or ROSSWI was requested use it, otherwise proceed as usual */
    if(CustomDriverState == OGL_CD_CUSTOM_ICD)
    {
        pDrvInfo = &CustomDrvInfo;
    }
    else if(CustomDriverState == OGL_CD_ROSSWI)
    {
        return NULL;
    }
    else
    {
        memset(&DrvInfo, 0, sizeof(DrvInfo));
        memset(WddmDllName, 0, sizeof(WddmDllName));
        WddmIcd = IntGetWddmIcdInfo(hdc, &DrvInfo, WddmDllName, &WddmFlags,
                                    &WddmLuid);
        if (WddmIcd)
        {
            if (AdapterLuid != NULL)
                *AdapterLuid = WddmLuid;
            if (AdapterLuidValid != NULL)
                *AdapterLuidValid = TRUE;
        }
        if (!WddmIcd)
        {
            /* XPDM ICD discovery through the display driver's escape. */
            dwInput = OPENGL_GETINFO;
            ret = ExtEscape(hdc, QUERYESCSUPPORT, sizeof(DWORD), (LPCSTR)&dwInput, 0, NULL);

            if (ret > 0)
            {
                /* Query for the ICD DLL name and version */
                dwInput = OPENGL_GETINFO_DRVNAME;
                ret = ExtEscape(hdc, OPENGL_GETINFO, sizeof(DWORD), (LPCSTR)&dwInput, sizeof(DrvInfo), (LPSTR)&DrvInfo);
                if (ret <= 0)
                {
                    ERR("Driver claims to support OPENGL_GETINFO escape code, but doesn't. ret: %X\n", ret);
                    return NULL;
                }
            }
            else
            {
                /* Windows also supports a registered software ICD when the
                 * display provides none. Use the normal registry validation
                 * and loading path; an absent or unusable MSOGL registration
                 * still falls back to the built-in software implementation. */
                DrvInfo.Version = 2;
                DrvInfo.DriverVersion = 1;
                wcscpy(DrvInfo.DriverName, L"MSOGL");
            }
        }

        pDrvInfo = &DrvInfo;
    }

    /* Protect the list while we are loading*/
    EnterCriticalSection(&icdload_cs);

    CurrentThreadId = GetCurrentThreadId();
    if (IcdLoadingThreadId == CurrentThreadId)
    {
        /* A vendor ICD may initialize DXGI from DrvValidateVersion.  ReactOS's
         * DXGI backend probes WGL, which must use its software bootstrap path
         * until the outer ICD has finished publishing a complete dispatch
         * table. */
        LeaveCriticalSection(&icdload_cs);
        return NULL;
    }

    /* Search for it in the list of already loaded modules */
    data = ICD_Data_List;
    while(data)
    {
        if(!_wcsicmp(data->DriverName, pDrvInfo->DriverName))
        {
            /* Found it */
            TRACE("Found already loaded %p.\n", data);
            LeaveCriticalSection(&icdload_cs);
            return data;
        }
        data = data->next;
    }

    if (WddmIcd)
    {
        lstrcpynW(DllName, WddmDllName, ARRAYSIZE(DllName));
        Version = DriverVersion = pDrvInfo->Version;
        Flags = WddmFlags;
        TRACE("WDDM ICD is %S, Version %lx, Flags %lx.\n",
              DllName, Version, Flags);
    }
    else
    {
        /* It was still not loaded, look for it in the legacy ICD registry. */
        ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, OpenGLDrivers_Key, 0, KEY_READ, &OglKey);
        if(ret != ERROR_SUCCESS)
        {
            ERR("Failed to open the OpenGLDrivers key.\n");
            goto end;
        }
        ret = RegOpenKeyExW(OglKey, pDrvInfo->DriverName, 0, KEY_READ, &DrvKey);
        if(ret != ERROR_SUCCESS)
        {
            /* Some driver installer just provide the DLL name, like the Matrox G400 */
            TRACE("No driver subkey for %S, trying to get DLL name directly.\n", pDrvInfo->DriverName);
            dwInput = sizeof(DllName);
            ret = RegQueryValueExW(OglKey, pDrvInfo->DriverName, 0, &dwValueType, (LPBYTE)DllName, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_SZ))
            {
                ERR("Unable to get ICD DLL name!\n");
                RegCloseKey(OglKey);
                goto end;
            }
            Version = DriverVersion = Flags = 0;
            TRACE("DLL name is %S.\n", DllName);
        }
        else
        {
            /* The driver have a subkey for the ICD */
            TRACE("Querying details from registry for %S.\n", pDrvInfo->DriverName);
            dwInput = sizeof(DllName);
            ret = RegQueryValueExW(DrvKey, L"Dll", 0, &dwValueType, (LPBYTE)DllName, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_SZ))
            {
                ERR("Unable to get ICD DLL name!.\n");
                RegCloseKey(DrvKey);
                RegCloseKey(OglKey);
                goto end;
            }

            dwInput = sizeof(Version);
            ret = RegQueryValueExW(DrvKey, L"Version", 0, &dwValueType, (LPBYTE)&Version, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            {
                WARN("No version in driver subkey\n");
            }
            else if(Version != pDrvInfo->Version)
            {
                ERR("Version mismatch between registry (%lu) and display driver (%lu).\n", Version, pDrvInfo->Version);
                RegCloseKey(DrvKey);
                RegCloseKey(OglKey);
                goto end;
            }

            dwInput = sizeof(DriverVersion);
            ret = RegQueryValueExW(DrvKey, L"DriverVersion", 0, &dwValueType, (LPBYTE)&DriverVersion, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            {
                WARN("No driver version in driver subkey\n");
            }
            else if(DriverVersion != pDrvInfo->DriverVersion)
            {
                ERR("Driver version mismatch between registry (%lu) and display driver (%lu).\n", DriverVersion, pDrvInfo->DriverVersion);
                RegCloseKey(DrvKey);
                RegCloseKey(OglKey);
                goto end;
            }

            dwInput = sizeof(Flags);
            ret = RegQueryValueExW(DrvKey, L"Flags", 0, &dwValueType, (LPBYTE)&Flags, &dwInput);
            if((ret != ERROR_SUCCESS) || (dwValueType != REG_DWORD))
            {
                WARN("No driver version in driver subkey\n");
                Flags = 0;
            }

            /* We're done */
            RegCloseKey(DrvKey);
            TRACE("DLL name is %S, Version %lx, DriverVersion %lx, Flags %lx.\n", DllName, Version, DriverVersion, Flags);
        }
        /* No need for this anymore */
        RegCloseKey(OglKey);
    }

    IcdLoadingThreadId = CurrentThreadId;

    /* So far so good, allocate data */
    data = HeapAlloc(GetProcessHeap(), 0, sizeof(*data));
    if(!data)
    {
        ERR("Unable to allocate ICD data!\n");
        goto end;
    }
    data->ProcTable = NULL;
    data->MakeContextCurrentARB = NULL;

    /* Load the library */
    data->hModule = LoadLibraryW(DllName);
    if(!data->hModule)
    {
        ERR("Could not load the ICD DLL: %S.\n", DllName);
        HeapFree(GetProcessHeap(), 0, data);
        data = NULL;
        goto end;
    }

    /*
     * Validate version, if needed.
     * Some drivers (at least VBOX), initialize stuff upon this call.
     */
    DrvValidateVersion = (void*)GetProcAddress(data->hModule, "DrvValidateVersion");
    if(DrvValidateVersion)
    {
        if(!DrvValidateVersion(pDrvInfo->DriverVersion))
        {
            ERR("DrvValidateVersion failed!.\n");
            goto fail;
        }
    }

    /* Pass the callbacks */
    DrvSetCallbackProcs = (void*)GetProcAddress(data->hModule, "DrvSetCallbackProcs");
    if(DrvSetCallbackProcs)
    {
        PROC callbacks[] = {
            (PROC)wglSetCurrentValue,
            (PROC)wglGetCurrentValue,
            (PROC)wglGetDHGLRC,
            NULL,
            (PROC)wglPresentBuffers,
            (PROC)wglGetAdapterLuid};
        DrvSetCallbackProcs(ARRAYSIZE(callbacks), callbacks);
    }

    /* Get the DLL exports */
#define DRV_LOAD(x) do                                  \
{                                                       \
    data->x = (void*)GetProcAddress(data->hModule, #x); \
    if(!data->x) {                                      \
        ERR("%S lacks " #x "!\n", DllName);             \
        goto fail;                                      \
    }                                                   \
} while(0)
    DRV_LOAD(DrvCopyContext);
    DRV_LOAD(DrvCreateContext);
    DRV_LOAD(DrvCreateLayerContext);
    DRV_LOAD(DrvDeleteContext);
    DRV_LOAD(DrvDescribeLayerPlane);
    DRV_LOAD(DrvDescribePixelFormat);
    DRV_LOAD(DrvGetLayerPaletteEntries);
    DRV_LOAD(DrvGetProcAddress);
    DRV_LOAD(DrvReleaseContext);
    DRV_LOAD(DrvRealizeLayerPalette);
    DRV_LOAD(DrvSetContext);
    DRV_LOAD(DrvSetLayerPaletteEntries);
    DRV_LOAD(DrvSetPixelFormat);
    DRV_LOAD(DrvShareLists);
    DRV_LOAD(DrvSwapBuffers);
    DRV_LOAD(DrvSwapLayerBuffers);
    data->DrvPresentBuffers =
        (void *)GetProcAddress(data->hModule, "DrvPresentBuffers");
    data->DrvPresentBuffers2 =
        (void *)GetProcAddress(data->hModule, "DrvPresentBuffers2");
#undef DRV_LOAD

    /* Let's see if GDI should handle this instead of the ICD DLL */
    // FIXME: maybe there is a better way
    if (GdiDescribePixelFormat(hdc, 0, 0, NULL) != 0)
    {
        /* GDI knows what to do with that. Override */
        TRACE("Forwarding WGL calls to win32k!\n");
        data->DrvDescribePixelFormat = GdiDescribePixelFormat;
        data->DrvSetPixelFormat = GdiSetPixelFormat;
        data->DrvSwapBuffers = GdiSwapBuffers;
    }

    /* Copy the DriverName */
    wcscpy(data->DriverName, pDrvInfo->DriverName);

    /* Push the list */
    data->next = ICD_Data_List;
    ICD_Data_List = data;

    TRACE("Returning %p.\n", data);
    TRACE("ICD driver %S (%S) successfully loaded.\n", pDrvInfo->DriverName, DllName);

end:
    /* Unlock and return */
    IcdLoadingThreadId = 0;
    LeaveCriticalSection(&icdload_cs);
    return data;

fail:
    IcdLoadingThreadId = 0;
    LeaveCriticalSection(&icdload_cs);
    FreeLibrary(data->hModule);
    HeapFree(GetProcessHeap(), 0, data);
    return NULL;
}

void IntDeleteAllICDs(void)
{
    struct ICD_Data* data;

    EnterCriticalSection(&icdload_cs);

    while (ICD_Data_List != NULL)
    {
        data = ICD_Data_List;
        ICD_Data_List = data->next;

        FreeLibrary(data->hModule);
        HeapFree(GetProcessHeap(), 0, data);
    }
}
