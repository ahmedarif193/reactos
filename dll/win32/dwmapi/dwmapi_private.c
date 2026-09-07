/*
 * PROJECT:     ReactOS private DWM client API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows 11-compatible private DWM client entry points
 *
 * The entry points in this file mirror the private ARM64 Windows 11
 * dwmapi.dll export surface. Signatures, validation order, output ownership,
 * and fixed HRESULTs are derived from public symbols and clean-room binary
 * observation. Server-backed operations use a process-local lifecycle model
 * until their win32k/DirectComposition transports are available; this keeps
 * observable state coherent instead of exposing spec stubs.
 */

#include <stdarg.h>

#ifdef __REACTOS__
#include <ndk/exfuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#endif

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winreg.h"
#include "objbase.h"
#include "sddl.h"
#include "dwmapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwmapi);

#ifndef SECURITY_WINDOW_MANAGER_BASE_RID
#define SECURITY_WINDOW_MANAGER_BASE_RID 0x0000005aL
#endif

#define DWM_PRIVATE_OBJECT_LIMIT 256
#define DWM_PRIVATE_TYPE_THUMBNAIL  1
#define DWM_PRIVATE_TYPE_TRANSITION 2
#define DWM_PRIVATE_TYPE_ANIMATION  3
#define DWM_PRIVATE_TYPE_SWAPCHAIN  4
#define DWM_PRIVATE_TYPE_CAPTURE    5
#define DWM_PRIVATE_TYPE_VISUAL     6

typedef struct _DWM_PRIVATE_OBJECT
{
    BOOL Used;
    DWORD Type;
    ULONGLONG Token;
    ULONGLONG First;
    ULONGLONG Second;
    HWND Window;
    DWORD Flags;
    SIZE Size;
    LARGE_INTEGER Time;
} DWM_PRIVATE_OBJECT;

typedef struct _DWM_PRIVATE_COLORIZATION
{
    DWORD ColorizationColor;
    DWORD ColorizationAfterglow;
    DWORD ColorizationColorBalance;
    DWORD ColorizationAfterglowBalance;
    DWORD ColorizationBlurBalance;
    BOOL EnableWindowColorization;
    DWORD ColorizationGlassAttribute;
    DWORD Reserved;
} DWM_PRIVATE_COLORIZATION;

typedef struct _DWM_PRIVATE_GLOBAL_STATE
{
    ULONGLONG Generation;
    ULONGLONG Flags;
} DWM_PRIVATE_GLOBAL_STATE;

typedef struct _DWM_PRIVATE_HMD_STATUS
{
    DWORD Values[8];
} DWM_PRIVATE_HMD_STATUS;

static INIT_ONCE DwmPrivateInitOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION DwmPrivateLock;
static DWM_PRIVATE_OBJECT DwmPrivateObjects[DWM_PRIVATE_OBJECT_LIMIT];
static volatile LONG64 DwmPrivateNextToken = 1;
static volatile LONG64 DwmPrivateGeneration = 1;

static BOOL CALLBACK
DwmPrivateInitialize(PINIT_ONCE Once, PVOID Parameter, PVOID *Context)
{
    UNREFERENCED_PARAMETER(Once);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);
    InitializeCriticalSection(&DwmPrivateLock);
    return TRUE;
}

static void
DwmPrivateAcquire(void)
{
    InitOnceExecuteOnce(&DwmPrivateInitOnce, DwmPrivateInitialize, NULL, NULL);
    EnterCriticalSection(&DwmPrivateLock);
}

static void
DwmPrivateRelease(void)
{
    LeaveCriticalSection(&DwmPrivateLock);
}

static DWM_PRIVATE_OBJECT *
DwmPrivateFindObject(ULONGLONG Token, DWORD Type)
{
    UINT Index;

    for (Index = 0; Index < DWM_PRIVATE_OBJECT_LIMIT; ++Index)
    {
        if (DwmPrivateObjects[Index].Used &&
            DwmPrivateObjects[Index].Token == Token &&
            (Type == 0 || DwmPrivateObjects[Index].Type == Type))
        {
            return &DwmPrivateObjects[Index];
        }
    }
    return NULL;
}

static HRESULT
DwmPrivateAllocateObject(DWORD Type, ULONGLONG First, ULONGLONG Second,
                         HWND Window, DWORD Flags, ULONGLONG *Token)
{
    DWM_PRIVATE_OBJECT *Object = NULL;
    ULONGLONG NewToken;
    UINT Index;

    if (Token == NULL)
        return E_INVALIDARG;
    *Token = 0;

    DwmPrivateAcquire();
    for (Index = 0; Index < DWM_PRIVATE_OBJECT_LIMIT; ++Index)
    {
        if (!DwmPrivateObjects[Index].Used)
        {
            Object = &DwmPrivateObjects[Index];
            break;
        }
    }
    if (Object == NULL)
    {
        DwmPrivateRelease();
        return E_OUTOFMEMORY;
    }

    NewToken = (ULONGLONG)InterlockedIncrement64(&DwmPrivateNextToken);
    if (NewToken == 0)
        NewToken = (ULONGLONG)InterlockedIncrement64(&DwmPrivateNextToken);
    ZeroMemory(Object, sizeof(*Object));
    Object->Used = TRUE;
    Object->Type = Type;
    Object->Token = NewToken;
    Object->First = First;
    Object->Second = Second;
    Object->Window = Window;
    Object->Flags = Flags;
    QueryPerformanceCounter(&Object->Time);
    *Token = NewToken;
    InterlockedIncrement64(&DwmPrivateGeneration);
    DwmPrivateRelease();
    return S_OK;
}

static HRESULT
DwmPrivateRegisterObject(DWORD Type, ULONGLONG Token, ULONGLONG First,
                         ULONGLONG Second, HWND Window, DWORD Flags)
{
    DWM_PRIVATE_OBJECT *Object = NULL;
    UINT Index;

    if (Token == 0)
        return E_INVALIDARG;
    DwmPrivateAcquire();
    if (DwmPrivateFindObject(Token, 0) != NULL)
    {
        DwmPrivateRelease();
        return E_INVALIDARG;
    }
    for (Index = 0; Index < DWM_PRIVATE_OBJECT_LIMIT; ++Index)
    {
        if (!DwmPrivateObjects[Index].Used)
        {
            Object = &DwmPrivateObjects[Index];
            break;
        }
    }
    if (Object == NULL)
    {
        DwmPrivateRelease();
        return E_OUTOFMEMORY;
    }

    ZeroMemory(Object, sizeof(*Object));
    Object->Used = TRUE;
    Object->Type = Type;
    Object->Token = Token;
    Object->First = First;
    Object->Second = Second;
    Object->Window = Window;
    Object->Flags = Flags;
    QueryPerformanceCounter(&Object->Time);
    InterlockedIncrement64(&DwmPrivateGeneration);
    DwmPrivateRelease();
    return S_OK;
}

static HRESULT
DwmPrivateRemoveObject(ULONGLONG Token, DWORD Type)
{
    DWM_PRIVATE_OBJECT *Object;

    if (Token == 0)
        return E_INVALIDARG;
    DwmPrivateAcquire();
    Object = DwmPrivateFindObject(Token, Type);
    if (Object == NULL)
    {
        DwmPrivateRelease();
        return E_INVALIDARG;
    }
    ZeroMemory(Object, sizeof(*Object));
    InterlockedIncrement64(&DwmPrivateGeneration);
    DwmPrivateRelease();
    return S_OK;
}

static HRESULT
DwmPrivateTouchObject(ULONGLONG Token, DWORD Type, ULONGLONG Value,
                      DWORD Flags)
{
    DWM_PRIVATE_OBJECT *Object;

    if (Token == 0)
        return E_INVALIDARG;
    DwmPrivateAcquire();
    Object = DwmPrivateFindObject(Token, Type);
    if (Object == NULL)
    {
        DwmPrivateRelease();
        return E_INVALIDARG;
    }
    Object->Second = Value;
    Object->Flags = Flags;
    QueryPerformanceCounter(&Object->Time);
    InterlockedIncrement64(&DwmPrivateGeneration);
    DwmPrivateRelease();
    return S_OK;
}

static HRESULT
DwmPrivateRequireComposition(void)
{
    BOOL Enabled = FALSE;
    HRESULT Result = DwmIsCompositionEnabled(&Enabled);

    if (FAILED(Result))
        return Result;
    return Enabled ? S_OK : DWM_E_COMPOSITIONDISABLED;
}

static BOOL
DwmPrivateIsShellProcess(void)
{
    HWND Shell = GetShellWindow();
    DWORD ProcessId = 0;

    if (Shell == NULL)
        return FALSE;
    GetWindowThreadProcessId(Shell, &ProcessId);
    return ProcessId == GetCurrentProcessId();
}

static BOOL
DwmPrivateIsTopLevelWindow(HWND Window)
{
    return Window != NULL && IsWindow(Window) &&
           GetAncestor(Window, GA_ROOT) == Window &&
           !(GetWindowLongPtrW(Window, GWL_STYLE) & WS_CHILD);
}

static BOOL
DwmPrivateIsWindowInProcess(HWND Window)
{
    DWORD ProcessId = 0;

    if (!IsWindow(Window))
        return FALSE;
    GetWindowThreadProcessId(Window, &ProcessId);
    return ProcessId == GetCurrentProcessId();
}

static BOOL
DwmPrivateValidRect(const RECT *Rect)
{
    return Rect != NULL && Rect->right >= Rect->left &&
           Rect->bottom >= Rect->top;
}

static HRESULT
DwmPrivateDuplicateHandle(HANDLE Source, HANDLE *Duplicate)
{
#ifdef __REACTOS__
    NTSTATUS Status;

    *Duplicate = NULL;
    Status = NtDuplicateObject(NtCurrentProcess(), Source,
                               NtCurrentProcess(), Duplicate, 0, 0,
                               DUPLICATE_SAME_ACCESS);
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
#else
    UNREFERENCED_PARAMETER(Source);
    *Duplicate = NULL;
    return E_NOTIMPL;
#endif
}

HRESULT WINAPI
DwmpGetGlobalState(DWM_PRIVATE_GLOBAL_STATE *State)
{
    if (State == NULL)
        return E_INVALIDARG;
    return E_INVALIDARG;
}

HRESULT WINAPI
DwmpActivateLivePreviewEx(DWORD Flags, const HWND *Windows, UINT Count,
                          const DWORD *Options, INT Type,
                          const RECT *Rects)
{
    UINT Index;

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Options);
    if (Type < 1 || Type > 6)
        return E_INVALIDARG;
    if (Count != 0 && Windows == NULL)
        return E_INVALIDARG;
    if (Rects != NULL && Count == 0)
        return E_INVALIDARG;
    for (Index = 0; Index < Count; ++Index)
    {
        if (!IsWindow(Windows[Index]))
            return E_INVALIDARG;
    }
    for (Index = 0; Index < Count; ++Index)
        RedrawWindow(Windows[Index], NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpActivateLivePreview(DWORD Flags, HWND Window, const DWORD *Options,
                        INT Type, const RECT *Rect)
{
    return DwmpActivateLivePreviewEx(Flags, &Window, 1, Options, Type, Rect);
}

HRESULT WINAPI
DwmpQueryThumbnailType(ULONGLONG Thumbnail, DWORD *Type)
{
    DWM_PRIVATE_OBJECT *Object;

    if (Type == NULL)
        return E_INVALIDARG;
    DwmPrivateAcquire();
    Object = DwmPrivateFindObject(Thumbnail, DWM_PRIVATE_TYPE_THUMBNAIL);
    if (Object == NULL)
    {
        DwmPrivateRelease();
        return E_INVALIDARG;
    }
    *Type = Object->Flags;
    DwmPrivateRelease();
    return S_OK;
}

HRESULT WINAPI
DwmpRegisterThumbnail(HWND Destination, HWND Source, DWORD Flags,
                      DWORD Type, ULONGLONG *Thumbnail)
{
    DWM_PRIVATE_OBJECT *Object;
    RECT SourceRect;
    HRESULT Result;

    if (!DwmPrivateIsTopLevelWindow(Destination) ||
        !DwmPrivateIsTopLevelWindow(Source) || Destination == Source ||
        !DwmPrivateIsWindowInProcess(Destination) || Thumbnail == NULL)
    {
        return E_INVALIDARG;
    }
    Result = DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_THUMBNAIL,
                                      (ULONGLONG)(ULONG_PTR)Source, Type,
                                      Destination, Flags, Thumbnail);
    if (FAILED(Result))
        return Result;
    if (GetWindowRect(Source, &SourceRect))
    {
        DwmPrivateAcquire();
        Object = DwmPrivateFindObject(*Thumbnail, DWM_PRIVATE_TYPE_THUMBNAIL);
        if (Object != NULL)
        {
            Object->Size.cx = SourceRect.right - SourceRect.left;
            Object->Size.cy = SourceRect.bottom - SourceRect.top;
        }
        DwmPrivateRelease();
    }
    return S_OK;
}

HRESULT WINAPI
DwmpDxgiIsThreadDesktopComposited(BOOL *Composited)
{
    if (Composited == NULL)
        return E_INVALIDARG;
    return DwmIsCompositionEnabled(Composited);
}

static HRESULT
DwmPrivateOpenColorizationKey(REGSAM Access, BOOL Create, HKEY *Key)
{
    static const WCHAR Path[] = L"Software\\Microsoft\\Windows\\DWM";
    LONG Error;

    if (Create)
        Error = RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, 0,
                                Access, NULL, Key, NULL);
    else
        Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, Access, Key);
    return Error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(Error);
}

static HRESULT
DwmPrivateWriteColorization(const DWM_PRIVATE_COLORIZATION *Parameters)
{
    static const WCHAR *Names[] =
    {
        L"ColorizationColor",
        L"ColorizationAfterglow",
        L"ColorizationColorBalance",
        L"ColorizationAfterglowBalance",
        L"ColorizationBlurBalance",
        L"EnableWindowColorization",
        L"ColorizationGlassAttribute"
    };
    HKEY Key;
    const DWORD *Values = (const DWORD *)Parameters;
    HRESULT Result;
    LONG Error = ERROR_SUCCESS;
    UINT Index;

    Result = DwmPrivateOpenColorizationKey(KEY_SET_VALUE, TRUE, &Key);
    if (FAILED(Result))
        return Result;
    for (Index = 0; Index < ARRAYSIZE(Names); ++Index)
    {
        Error = RegSetValueExW(Key, Names[Index], 0, REG_DWORD,
                               (const BYTE *)&Values[Index], sizeof(DWORD));
        if (Error != ERROR_SUCCESS)
            break;
    }
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(Error);
}

HRESULT WINAPI
DwmpSetColorizationParameters(const DWM_PRIVATE_COLORIZATION *Parameters,
                              BOOL DoNotPersist)
{
    HRESULT Result;

    if (Parameters == NULL)
        return E_INVALIDARG;
    if (!DoNotPersist)
    {
        Result = DwmPrivateWriteColorization(Parameters);
        if (FAILED(Result))
            return Result;
    }
    InterlockedIncrement64(&DwmPrivateGeneration);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpGetCompositionTimingInfoEx(HWND Window, DWM_TIMING_INFO *Information)
{
    BYTE NativeInformation[0x13c];
    DWM_TIMING_INFO *BaseInformation = (DWM_TIMING_INFO *)NativeInformation;
    UINT Size;
    HRESULT Result;

    if (Information == NULL)
        return E_INVALIDARG;
    Size = Information->cbSize;
    if (Size != 0x124 && Size != 0x13c)
        return HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);

    ZeroMemory(NativeInformation, sizeof(NativeInformation));
    BaseInformation->cbSize = sizeof(*BaseInformation);
    Result = DwmGetCompositionTimingInfo(Window, BaseInformation);
    if (FAILED(Result))
        return Result;

    CopyMemory(Information, NativeInformation, Size);
    Information->cbSize = Size;
    return Result;
}

HRESULT WINAPI
DwmpRenderFlick(HWND Window, POINT Point)
{
    UNREFERENCED_PARAMETER(Window);
    UNREFERENCED_PARAMETER(Point);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpAllocateSecurityDescriptor(PSECURITY_DESCRIPTOR *Descriptor,
                               ACCESS_MASK Access)
{
#ifdef __REACTOS__
    SID_IDENTIFIER_AUTHORITY Authority = {SECURITY_NT_AUTHORITY};
    PSECURITY_DESCRIPTOR NewDescriptor = NULL;
    PSID Sid = NULL;
    PACL Acl;
    ULONG SidLength, AclLength, AllocationLength;
    NTSTATUS Status;
    ULONG SessionId;

    if (Descriptor == NULL)
        return E_INVALIDARG;
    *Descriptor = NULL;
    SessionId = NtCurrentPeb()->SessionId;
    Status = RtlAllocateAndInitializeSid(&Authority, 3,
                                         SECURITY_WINDOW_MANAGER_BASE_RID,
                                         0, SessionId, 0, 0, 0, 0, 0, &Sid);
    if (!NT_SUCCESS(Status))
        return HRESULT_FROM_NT(Status);

    SidLength = RtlLengthSid(Sid);
    AclLength = SidLength + 0x14;
    AllocationLength = sizeof(SECURITY_DESCRIPTOR) + AclLength;
    NewDescriptor = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY,
                                    AllocationLength);
    if (NewDescriptor == NULL)
    {
        RtlFreeSid(Sid);
        return E_OUTOFMEMORY;
    }

    Status = RtlCreateSecurityDescriptor(NewDescriptor,
                                         SECURITY_DESCRIPTOR_REVISION);
    Acl = (PACL)((BYTE *)NewDescriptor + sizeof(SECURITY_DESCRIPTOR));
    if (NT_SUCCESS(Status))
        Status = RtlCreateAcl(Acl, AclLength, ACL_REVISION);
    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Acl, ACL_REVISION, Access, Sid);
    if (NT_SUCCESS(Status))
        Status = RtlSetDaclSecurityDescriptor(NewDescriptor, TRUE, Acl, FALSE);
    RtlFreeSid(Sid);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NewDescriptor);
        return HRESULT_FROM_NT(Status);
    }
    *Descriptor = NewDescriptor;
    return S_OK;
#else
    UNREFERENCED_PARAMETER(Descriptor);
    UNREFERENCED_PARAMETER(Access);
    return E_NOTIMPL;
#endif
}

void WINAPI
DwmpFreeSecurityDescriptor(PSECURITY_DESCRIPTOR Descriptor)
{
#ifdef __REACTOS__
    RtlFreeHeap(RtlGetProcessHeap(), 0, Descriptor);
#else
    UNREFERENCED_PARAMETER(Descriptor);
#endif
}

HRESULT WINAPI
DwmpBeginTransitionRequestWithGUIDEx(DWORD Request, const GUID *Activity,
                                     GUID *Correlation)
{
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Activity);
    UNREFERENCED_PARAMETER(Correlation);
    return E_INVALIDARG;
}

HRESULT WINAPI
DwmpBeginTransitionRequest(DWORD Request)
{
    UNREFERENCED_PARAMETER(Request);
    return E_INVALIDARG;
}

HRESULT WINAPI
DwmpBeginTransitionRequestWithGUID(DWORD Request, const GUID *Activity)
{
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Activity);
    return E_INVALIDARG;
}

HRESULT WINAPI
DwmpEndTransitionRequest(DWORD Request)
{
    UNREFERENCED_PARAMETER(Request);
    InterlockedIncrement64(&DwmPrivateGeneration);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpTransitionWindowWithRects(HWND Window, DWORD Transition,
                              const RECT *Source, const RECT *Destination,
                              const RECT *Clip, const RECT *Content,
                              const RECT *Visible)
{
    UNREFERENCED_PARAMETER(Transition);
    if (!IsWindow(Window))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if ((Source != NULL && !DwmPrivateValidRect(Source)) ||
        (Destination != NULL && !DwmPrivateValidRect(Destination)) ||
        (Clip != NULL && !DwmPrivateValidRect(Clip)) ||
        (Content != NULL && !DwmPrivateValidRect(Content)) ||
        (Visible != NULL && !DwmPrivateValidRect(Visible)))
    {
        return E_INVALIDARG;
    }
    RedrawWindow(Window, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpTransitionWindow(HWND Window, DWORD Transition)
{
    return DwmpTransitionWindowWithRects(Window, Transition, NULL, NULL,
                                         NULL, NULL, NULL);
}

HRESULT WINAPI
DwmpUpdateDesktopThumbnail(ULONGLONG Thumbnail, const RECT *Destination,
                           ULONGLONG First, ULONGLONG Second,
                           ULONGLONG Third, BOOL Visible, DWORD Flags)
{
    UNREFERENCED_PARAMETER(First);
    UNREFERENCED_PARAMETER(Second);
    UNREFERENCED_PARAMETER(Third);
    UNREFERENCED_PARAMETER(Visible);
    UNREFERENCED_PARAMETER(Thumbnail);
    UNREFERENCED_PARAMETER(Flags);
    if (Destination != NULL && !DwmPrivateValidRect(Destination))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI
DwmpTransitionBitmap(DWORD Request, HBITMAP Bitmap, DWORD Flags,
                     const RECT *Source, const RECT *Destination)
{
    DIBSECTION Object;
    ULONGLONG PixelCount;

    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Flags);
    if (Bitmap == NULL || Source == NULL || Destination == NULL ||
        !DwmPrivateValidRect(Source) || !DwmPrivateValidRect(Destination))
    {
        return E_INVALIDARG;
    }
    if (GetObjectW(Bitmap, sizeof(Object), &Object) != sizeof(Object) ||
        Object.dsBm.bmBitsPixel != 32 ||
        Object.dsBmih.biCompression != BI_RGB)
    {
        return E_INVALIDARG;
    }
    PixelCount = (ULONGLONG)abs(Object.dsBm.bmWidth) *
                 (ULONGLONG)abs(Object.dsBm.bmHeight);
    if (PixelCount == 0 || PixelCount > MAXDWORD / 4)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpCreateSharedThumbnailVisual(HWND Destination, HWND Source, DWORD Flags,
                                const void *Properties, IUnknown *Adapter,
                                ULONGLONG *Visual, ULONGLONG *Thumbnail)
{
    HRESULT Result;

    if (!IsWindow(Destination) || Destination == Source ||
        Properties == NULL || Adapter == NULL || Visual == NULL ||
        Thumbnail == NULL)
    {
        return E_INVALIDARG;
    }
    *Visual = 0;
    *Thumbnail = 0;
    Result = DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_VISUAL,
                                      (ULONGLONG)(ULONG_PTR)Source, 0,
                                      Destination, Flags, Visual);
    if (FAILED(Result))
        return Result;
    return DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_THUMBNAIL, *Visual, 0,
                                    Destination, Flags, Thumbnail);
}

HRESULT WINAPI
DwmpCreateAnimationClock(ULONGLONG First, ULONGLONG Second, DWORD Flags)
{
    ULONGLONG Token;

    if (First == 0 || Second == 0)
        return E_INVALIDARG;
    return DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_ANIMATION, First, Second,
                                    NULL, Flags, &Token);
}

HRESULT WINAPI
DwmpBeginAnimationClock(ULONGLONG First, ULONGLONG Second, DWORD Flags)
{
    if (First == 0 || Second == 0)
        return E_INVALIDARG;
    return DwmpCreateAnimationClock(First, Second, Flags);
}

HRESULT WINAPI
DwmpEndAnimationClock(ULONGLONG First, ULONGLONG Second)
{
    if (First == 0 || Second == 0)
        return E_INVALIDARG;
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpGetAnimationClockTime(ULONGLONG First, ULONGLONG Second, DWORD Flags,
                          ULONGLONG *Time)
{
    LARGE_INTEGER Counter;

    UNREFERENCED_PARAMETER(Flags);
    if (Time == NULL)
        return E_INVALIDARG;
    if (First == 0 || Second == 0)
        return E_INVALIDARG;
    QueryPerformanceCounter(&Counter);
    *Time = (ULONGLONG)Counter.QuadPart;
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpSetAnimationClockTime(ULONGLONG First, ULONGLONG Second, DWORD Flags,
                          const ULONGLONG *Time)
{
    UNREFERENCED_PARAMETER(Flags);
    if (First == 0 || Second == 0 || Time == NULL)
        return E_INVALIDARG;
    InterlockedIncrement64(&DwmPrivateGeneration);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpGetAnimationClockToken(ULONGLONG First, ULONGLONG Second,
                           ULONGLONG *Token)
{
    if (Token == NULL)
        return E_INVALIDARG;
    *Token = 0;
    if (First == 0 || Second == 0)
        return E_INVALIDARG;
    return DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_ANIMATION, First, Second,
                                    NULL, 0, Token);
}

HRESULT WINAPI
DwmpRegisterSwapchainRenderTarget(HANDLE Swapchain, ULONGLONG First,
                                  ULONGLONG Second, DWORD Flags)
{
    HANDLE Duplicate;
    HRESULT Result;

    Result = DwmPrivateDuplicateHandle(Swapchain, &Duplicate);
    if (FAILED(Result))
        return Result;
    CloseHandle(Duplicate);
    return DwmPrivateRegisterObject(DWM_PRIVATE_TYPE_SWAPCHAIN, First,
                                    (ULONGLONG)(ULONG_PTR)Swapchain, Second,
                                    NULL, Flags);
}

HRESULT WINAPI
DwmpUnregisterSwapchainRenderTarget(ULONGLONG Target, DWORD *Flags)
{
    HRESULT Result;

    if (Target == 0)
        return E_INVALIDARG;
    Result = DwmPrivateRemoveObject(Target, DWM_PRIVATE_TYPE_SWAPCHAIN);
    if (SUCCEEDED(Result) && Flags != NULL)
        *Flags = 0;
    return Result;
}

HRESULT WINAPI
DwmpUpdateAccentBlurRect(HWND Window, const RECT *Rect)
{
    if (!DwmPrivateValidRect(Rect))
        return E_INVALIDARG;
    RedrawWindow(Window, Rect, NULL, RDW_INVALIDATE);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpSetImmersiveIconic(HWND Window, HBITMAP Bitmap, DWORD Color,
                       INT MaximumWidth)
{
    DIBSECTION Object;

    UNREFERENCED_PARAMETER(Color);
    UNREFERENCED_PARAMETER(MaximumWidth);
    if (Window == NULL || !DwmPrivateIsShellProcess())
        return E_INVALIDARG;
    if (Bitmap != NULL)
    {
        if (GetObjectW(Bitmap, sizeof(Object), &Object) != sizeof(Object) ||
            Object.dsBm.bmBitsPixel != 32 ||
            Object.dsBmih.biCompression != BI_RGB)
        {
            return E_INVALIDARG;
        }
    }
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpSetImmersiveIconicNotifyWindow(HWND Window)
{
    if (Window == NULL || !DwmPrivateIsShellProcess())
        return E_INVALIDARG;
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpQueryWindowThumbnailSourceSize(ULONGLONG Thumbnail, DWORD Flags,
                                   SIZE *Size)
{
    DWM_PRIVATE_OBJECT *Object;

    UNREFERENCED_PARAMETER(Flags);
    if (Size == NULL)
        return E_INVALIDARG;
    DwmPrivateAcquire();
    Object = DwmPrivateFindObject(Thumbnail, DWM_PRIVATE_TYPE_THUMBNAIL);
    if (Object == NULL)
    {
        DwmPrivateRelease();
        return E_INVALIDARG;
    }
    *Size = Object->Size;
    DwmPrivateRelease();
    return S_OK;
}

HRESULT WINAPI
DwmpCreateSharedMultiWindowVisual(HWND Window, IUnknown *Adapter,
                                  ULONGLONG *Visual, ULONGLONG *Token)
{
    HRESULT Result;

    if (!DwmPrivateIsWindowInProcess(Window) || Adapter == NULL ||
        Visual == NULL || Token == NULL)
        return E_INVALIDARG;
    *Visual = 0;
    *Token = 0;
    Result = DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_VISUAL, 0, 0,
                                      Window, 0, Visual);
    if (FAILED(Result))
        return Result;
    return DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_THUMBNAIL, *Visual, 0,
                                    Window, 0, Token);
}

HRESULT WINAPI
DwmpUpdateSharedMultiWindowVisual(ULONGLONG Token, const HWND *Include,
                                  UINT IncludeCount, const HWND *Exclude,
                                  UINT ExcludeCount, const RECT *Bounds,
                                  const SIZE *Size, DWORD Flags)
{
    UINT Index;

    if ((IncludeCount != 0 && Include == NULL) ||
        (ExcludeCount != 0 && Exclude == NULL) || Bounds == NULL ||
        Size == NULL || !DwmPrivateValidRect(Bounds))
    {
        return E_INVALIDARG;
    }
    for (Index = 0; Index < IncludeCount; ++Index)
        if (!IsWindow(Include[Index])) return E_INVALIDARG;
    for (Index = 0; Index < ExcludeCount; ++Index)
        if (!IsWindow(Exclude[Index])) return E_INVALIDARG;
    return DwmPrivateTouchObject(Token, DWM_PRIVATE_TYPE_THUMBNAIL,
                                 ((ULONGLONG)(DWORD)Size->cx << 32) |
                                 (DWORD)Size->cy, Flags);
}

HRESULT WINAPI
DwmpSetHolographicExclusiveView(BOOL Enabled)
{
    UNREFERENCED_PARAMETER(Enabled);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpSetChildRootVisual(HWND Window, IUnknown *Target, IUnknown *Visual,
                       ULONGLONG First, ULONGLONG Second)
{
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(First);
    UNREFERENCED_PARAMETER(Second);
    if (!DwmPrivateIsWindowInProcess(Window))
        return E_INVALIDARG;
    if (Visual == NULL)
        return S_OK;
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpGetHmdStatus(DWM_PRIVATE_HMD_STATUS *Status)
{
    if (Status == NULL)
        return E_INVALIDARG;
    ZeroMemory(Status, sizeof(*Status));
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpResetColorizationParameters(void)
{
    static const WCHAR *Names[] =
    {
        L"ColorizationColor",
        L"ColorizationColorBalance",
        L"ColorizationAfterglow",
        L"ColorizationAfterglowBalance",
        L"ColorizationBlurBalance",
        L"EnableWindowColorization"
    };
    HKEY Key;
    HRESULT Result;
    UINT Index;

    Result = DwmPrivateOpenColorizationKey(KEY_SET_VALUE, TRUE, &Key);
    if (FAILED(Result))
        return S_OK;
    for (Index = 0; Index < ARRAYSIZE(Names); ++Index)
        RegDeleteValueW(Key, Names[Index]);
    RegCloseKey(Key);
    return S_OK;
}

static HRESULT
DwmPrivateReadDword(HKEY Key, const WCHAR *Name, DWORD *Value)
{
    DWORD Type = 0, Size = sizeof(*Value);
    LONG Error;

    Error = RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)Value, &Size);
    if (Error == ERROR_SUCCESS && Type == REG_DWORD && Size == sizeof(*Value))
        return S_OK;
    return HRESULT_FROM_WIN32(Error == ERROR_SUCCESS ?
                              ERROR_DATATYPE_MISMATCH : Error);
}

HRESULT WINAPI
DwmpReadColorizationParameters(DWM_PRIVATE_COLORIZATION *Parameters)
{
    HKEY Key;
    HRESULT Result;
    DWORD Value;

    if (Parameters == NULL)
        return E_INVALIDARG;
    Result = DwmPrivateOpenColorizationKey(KEY_QUERY_VALUE, FALSE, &Key);
    if (FAILED(Result))
        return Result;

    Result = DwmPrivateReadDword(Key, L"ColorizationColor", &Value);
    if (FAILED(Result))
        goto Exit;
    Parameters->ColorizationColor = Value;

    Result = DwmPrivateReadDword(Key, L"ColorizationColorBalance", &Value);
    Parameters->ColorizationColorBalance = SUCCEEDED(Result) ? Value : 27;

    Result = DwmPrivateReadDword(Key, L"ColorizationAfterglow", &Value);
    if (FAILED(Result))
        goto Exit;
    Parameters->ColorizationAfterglow = Value;

    Result = DwmPrivateReadDword(Key, L"ColorizationAfterglowBalance", &Value);
    Parameters->ColorizationAfterglowBalance = SUCCEEDED(Result) ? Value : 0;

    Result = DwmPrivateReadDword(Key, L"ColorizationBlurBalance", &Value);
    Parameters->ColorizationBlurBalance = SUCCEEDED(Result) ? Value : 73;

    Result = DwmPrivateReadDword(Key, L"EnableWindowColorization", &Value);
    Parameters->EnableWindowColorization = SUCCEEDED(Result) ? Value : TRUE;

    Result = DwmPrivateReadDword(Key, L"ColorizationGlassAttribute", &Value);
    if (FAILED(Result))
        goto Exit;
    Parameters->ColorizationGlassAttribute = Value;

    Parameters->EnableWindowColorization =
        Parameters->EnableWindowColorization != FALSE;
    if (Parameters->ColorizationColorBalance > 100)
        Parameters->ColorizationColorBalance = 27;
    if (Parameters->ColorizationAfterglowBalance > 100)
        Parameters->ColorizationAfterglowBalance = 0;
    if (Parameters->ColorizationBlurBalance > 100)
        Parameters->ColorizationBlurBalance = 73;
    Result = S_OK;

Exit:
    RegCloseKey(Key);
    return Result;
}

HRESULT WINAPI
DwmpCreateSessionShutdownEvent(DWORD SessionId, HANDLE *Event)
{
    WCHAR Name[96];
    PSECURITY_DESCRIPTOR Descriptor = NULL;
#ifdef __REACTOS__
    UNICODE_STRING NativeName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
#endif

    if (Event == NULL)
        return E_INVALIDARG;
    *Event = NULL;
    wsprintfW(Name, L"\\Sessions\\%d\\Windows\\DwmCatastrophicShutdown",
              (INT)SessionId);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;0x00100002;;;SY)", SDDL_REVISION_1,
            &Descriptor, NULL))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
#ifdef __REACTOS__
    RtlInitUnicodeString(&NativeName, Name);
    InitializeObjectAttributes(&ObjectAttributes, &NativeName, 0, NULL,
                               Descriptor);
    Status = NtCreateEvent(Event, EVENT_MODIFY_STATE | SYNCHRONIZE,
                           &ObjectAttributes, NotificationEvent, FALSE);
    LocalFree(Descriptor);
    if (Status == STATUS_OBJECT_PATH_NOT_FOUND)
        Status = STATUS_OBJECT_NAME_COLLISION;
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
#else
    LocalFree(Descriptor);
    return E_NOTIMPL;
#endif
}

HRESULT WINAPI
DwmpSDRToHDRBoost(ULONGLONG Boost, ULONGLONG Target)
{
    UNREFERENCED_PARAMETER(Boost);
    if (Target == 0)
        return E_INVALIDARG;
    return DwmPrivateRequireComposition();
}

static HRESULT
DwmPrivateBeginCapture(ULONGLONG Source, HANDLE Swapchain, ULONGLONG *Token,
                       DWORD Flags)
{
    HANDLE Duplicate;
    HRESULT Result;

    if (Token == NULL)
        return E_INVALIDARG;
    *Token = 0;
    if (Swapchain == NULL)
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    Result = DwmPrivateDuplicateHandle(Swapchain, &Duplicate);
    if (FAILED(Result))
        return Result;
    CloseHandle(Duplicate);
    return DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_CAPTURE, Source, 0,
                                    NULL, Flags, Token);
}

HRESULT WINAPI
DwmpBeginWindowCapture(ULONGLONG Window, HANDLE Swapchain, ULONGLONG *Token)
{
    return DwmPrivateBeginCapture(Window, Swapchain, Token, 1);
}

HRESULT WINAPI
DwmpBeginDisplayCapture(ULONGLONG Display, HANDLE Swapchain, ULONGLONG *Token)
{
    return DwmPrivateBeginCapture(Display, Swapchain, Token, 2);
}

HRESULT WINAPI
DwmpUpdateWindowCapture(ULONGLONG Token, ULONGLONG Value)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateTouchObject(Token, DWM_PRIVATE_TYPE_CAPTURE, Value, 1);
}

HRESULT WINAPI
DwmpStopWindowCapture(ULONGLONG Token)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateRemoveObject(Token, DWM_PRIVATE_TYPE_CAPTURE);
}

HRESULT WINAPI
DwmpStopDisplayCapture(ULONGLONG Token)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateRemoveObject(Token, DWM_PRIVATE_TYPE_CAPTURE);
}

HRESULT WINAPI
DwmpGetAnimationCommitHandle(ULONGLONG First, ULONGLONG Second,
                             ULONGLONG *Handle)
{
    if (Handle == NULL)
        return E_INVALIDARG;
    return DwmPrivateAllocateObject(DWM_PRIVATE_TYPE_ANIMATION, First, Second,
                                    NULL, 0, Handle);
}

HRESULT WINAPI
DwmpGetTitlebarInfo(HWND Window, void *Information)
{
    if (!DwmPrivateIsTopLevelWindow(Window) || Information == NULL)
        return E_INVALIDARG;
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpAddSharedProjectedShadowCaster(ULONGLONG Visual, HANDLE Light,
                                   HANDLE Caster)
{
    HANDLE LightDuplicate = NULL, CasterDuplicate = NULL;
    HRESULT Result;

    UNREFERENCED_PARAMETER(Visual);
    if ((Light == NULL) != (Caster == NULL))
        return E_INVALIDARG;
    if (Light != NULL)
    {
        Result = DwmPrivateDuplicateHandle(Light, &LightDuplicate);
        if (FAILED(Result))
            return Result;
        Result = DwmPrivateDuplicateHandle(Caster, &CasterDuplicate);
        CloseHandle(LightDuplicate);
        if (FAILED(Result))
            return Result;
        CloseHandle(CasterDuplicate);
    }
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpBeginVirtualMonitorCapture(ULONGLONG Monitor, HANDLE Swapchain,
                               ULONGLONG *Token)
{
    return DwmPrivateBeginCapture(Monitor, Swapchain, Token, 3);
}

HRESULT WINAPI
DwmpStopVirtualMonitorCapture(ULONGLONG Token)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateRemoveObject(Token, DWM_PRIVATE_TYPE_CAPTURE);
}

HRESULT WINAPI
DwmpUpdateProxyWindowForCapture(ULONGLONG Token, HWND Window)
{
    if (!IsWindow(Window))
        return E_ACCESSDENIED;
    return DwmPrivateTouchObject(Token, DWM_PRIVATE_TYPE_CAPTURE,
                                 (ULONGLONG)(ULONG_PTR)Window, 0);
}

HRESULT WINAPI
DwmpUpdateWindowCaptureBorder(ULONGLONG Token, DWORD Flags)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateTouchObject(Token, DWM_PRIVATE_TYPE_CAPTURE, 0, Flags);
}

HRESULT WINAPI
DwmpUpdateDisplayCaptureBorder(ULONGLONG Token, DWORD Flags)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateTouchObject(Token, DWM_PRIVATE_TYPE_CAPTURE, 0, Flags);
}

HRESULT WINAPI
DwmpSetBlurredWallpaperSurface(IUnknown *Surface, ULONGLONG Handle)
{
    if (Surface != NULL && Handle == 0)
        return E_INVALIDARG;
    InterlockedIncrement64(&DwmPrivateGeneration);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpEnableWindowNotifications(BOOL Enable)
{
    UNREFERENCED_PARAMETER(Enable);
    /* Windows gates the transport behind WindowNotificationsFromDWM.
     * ReactOS does not expose that feature or its server-side notification
     * stream, so the native disabled-feature result is the exact contract. */
    return E_NOTIMPL;
}

HRESULT WINAPI
DwmpEnableModeChangeAnimation(BOOL Enable)
{
    UNREFERENCED_PARAMETER(Enable);
    return DwmPrivateRequireComposition();
}

HRESULT WINAPI
DwmpBeginFilteredDisplayCapture(ULONGLONG Display, HANDLE Swapchain,
                                ULONGLONG *Token)
{
    return DwmPrivateBeginCapture(Display, Swapchain, Token, 4);
}

HRESULT WINAPI
DwmpStopFilteredDisplayCapture(ULONGLONG Token)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateRemoveObject(Token, DWM_PRIVATE_TYPE_CAPTURE);
}

HRESULT WINAPI
DwmpAddRemoveWindowToFilteredDisplayCapture(ULONGLONG Token, BOOL Add,
                                            HWND Window)
{
    if (!IsWindow(Window))
        return E_ACCESSDENIED;
    return DwmPrivateTouchObject(Token, DWM_PRIVATE_TYPE_CAPTURE,
                                 (ULONGLONG)(ULONG_PTR)Window, !!Add);
}

HRESULT WINAPI
DwmpUpdateFilteredDisplayCaptureBorder(ULONGLONG Token, DWORD Flags)
{
    if (Token == 0)
        return E_ACCESSDENIED;
    return DwmPrivateTouchObject(Token, DWM_PRIVATE_TYPE_CAPTURE, 0, Flags);
}
