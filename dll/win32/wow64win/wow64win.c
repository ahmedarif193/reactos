/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            dll/win32/wow64win/wow64win.c
 * PURPOSE:         WOW64 Win32k (USER32/GDI32) System Call Thunking Layer
 * PROGRAMMERS:     (c) 2025 Ahmed ARIF (arif.ing@outlook.com)
 *
 * OVERVIEW:
 * --------
 * wow64win.dll is responsible for translating 32-bit win32k system calls
 * (NtUser*, NtGdi*) into their 64-bit equivalents. This includes:
 *
 * 1. Window Station and Desktop Operations
 *    - NtUserOpenWindowStation / NtUserCreateWindowStation
 *    - NtUserCreateDesktop / NtUserOpenDesktop / NtUserCloseDesktop
 *    - NtUserSwitchDesktop / NtUserSetThreadDesktop
 *
 * 2. Message Loop Operations
 *    - NtUserGetMessage / NtUserPeekMessage
 *    - NtUserDispatchMessage / NtUserTranslateMessage
 *    - NtUserWaitMessage / NtUserMsgWaitForMultipleObjectsEx
 *
 * 3. Window Creation and Management
 *    - NtUserCreateWindowEx / NtUserDestroyWindow
 *    - NtUserShowWindow / NtUserSetWindowPos
 *    - NtUserGetDC / NtUserReleaseDC
 *
 * 4. Structure Conversions
 *    - MSG32 <-> MSG64
 *    - CREATESTRUCT32 <-> CREATESTRUCT64
 *    - WINDOWPOS32 <-> WINDOWPOS64
 *    - Handle table and shared memory translations
 *
 * ARCHITECTURE:
 * ------------
 * - Win32k syscalls use a different service table (KeServiceDescriptorTableShadow)
 * - WOW64 threads transition from 32-bit compat mode to 64-bit kernel mode
 * - This DLL intercepts the syscall, converts structures, and calls 64-bit win32k
 * - Return values and out-parameters are converted back to 32-bit format
 *
 * INTEGRATION:
 * -----------
 * - Loaded by wow64.dll during GUI thread initialization
 * - Registered via Wow64LdrpLoadWow64Win during PsConvertToGuiThread
 * - Exports Wow64Win* functions for message dispatch and structure conversion
 *
 * COMPATIBILITY:
 * -------------
 * This implementation follows the Windows WOW64 architecture where:
 * - wow64.dll handles NT* syscalls (kernel32/ntdll layer)
 * - wow64win.dll handles NtUser/NtGdi syscalls (user32/gdi32 layer)
 * - wow64cpu.dll handles context switches and register state
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <stdio.h>
#include <string.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

/* ===================================================================
 * Debug Tracing
 * ================================================================ */

static VOID
Wow64WinReportStub(
    _In_z_ LPCSTR FunctionName)
{
#if DBG
    CHAR Buffer[128];

    if (FunctionName)
    {
        _snprintf(Buffer, sizeof(Buffer), "wow64win.dll stub: %s\n", FunctionName);
        Buffer[sizeof(Buffer) - 1] = '\0';
        OutputDebugStringA(Buffer);
    }
    else
    {
        OutputDebugStringA("wow64win.dll stub hit\n");
    }
#else
    UNREFERENCED_PARAMETER(FunctionName);
#endif
}

static VOID
Wow64WinDebugTrace(
    _In_z_ LPCSTR Message)
{
#if DBG
    CHAR Buffer[256];
    _snprintf(Buffer, sizeof(Buffer), "wow64win: %s\n", Message);
    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
#else
    UNREFERENCED_PARAMETER(Message);
#endif
}

/* ===================================================================
 * 32-bit Structure Definitions
 * ================================================================ */

/*
 * 32-bit MSG structure layout
 * Must match the Windows WOW64 MSG32 layout exactly
 */
typedef struct _MSG32
{
    ULONG hwnd;        /* 32-bit window handle */
    ULONG message;     /* message identifier */
    ULONG wParam;      /* word parameter */
    ULONG lParam;      /* long parameter */
    ULONG time;        /* message timestamp */
    LONG  x;           /* cursor X coordinate */
    LONG  y;           /* cursor Y coordinate */
} MSG32, *PMSG32;

/*
 * 32-bit CREATESTRUCT layout
 * Used during window creation (WM_CREATE)
 */
typedef struct _CREATESTRUCT32
{
    ULONG lpCreateParams;  /* 32-bit pointer to creation parameters */
    ULONG hInstance;       /* 32-bit instance handle */
    ULONG hMenu;           /* 32-bit menu handle */
    ULONG hwndParent;      /* 32-bit parent window handle */
    LONG  cy;              /* window height */
    LONG  cx;              /* window width */
    LONG  y;               /* window Y position */
    LONG  x;               /* window X position */
    LONG  style;           /* window style */
    ULONG lpszName;        /* 32-bit pointer to window name */
    ULONG lpszClass;       /* 32-bit pointer to class name */
    ULONG dwExStyle;       /* extended window style */
} CREATESTRUCT32, *PCCREATESTRUCT32;

/*
 * 32-bit WINDOWPOS structure
 * Used for window positioning (WM_WINDOWPOSCHANGING/CHANGED)
 */
typedef struct _WINDOWPOS32
{
    ULONG hwnd;            /* 32-bit window handle */
    ULONG hwndInsertAfter; /* 32-bit Z-order handle */
    LONG  x;               /* new X position */
    LONG  y;               /* new Y position */
    LONG  cx;              /* new width */
    LONG  cy;              /* new height */
    ULONG flags;           /* positioning flags (SWP_*) */
} WINDOWPOS32, *PWINDOWPOS32;

/* ===================================================================
 * Structure Conversion Helpers
 * ================================================================ */

#if defined(__GNUC__)
#define WOW64WIN_UNUSED __attribute__((unused))
#else
#define WOW64WIN_UNUSED
#endif

/*
 * Convert a 32-bit MSG structure to 64-bit MSG
 * Used when returning messages from Get/PeekMessage
 */
static VOID WOW64WIN_UNUSED
Wow64WinConvertMsg32To64(
    _In_ PMSG32 Msg32,
    _Out_ PMSG Msg64)
{
    if (!Msg32 || !Msg64)
    {
        return;
    }

    Msg64->hwnd = (HWND)(ULONG_PTR)Msg32->hwnd;
    Msg64->message = Msg32->message;
    Msg64->wParam = (WPARAM)Msg32->wParam;
    Msg64->lParam = (LPARAM)Msg32->lParam;
    Msg64->time = Msg32->time;
    Msg64->pt.x = Msg32->x;
    Msg64->pt.y = Msg32->y;
}

/*
 * Convert a 64-bit MSG structure to 32-bit MSG
 * Used when delivering messages back to 32-bit code
 */
static VOID WOW64WIN_UNUSED
Wow64WinConvertMsg64To32(
    _In_ PMSG Msg64,
    _Out_ PMSG32 Msg32)
{
    if (!Msg64 || !Msg32)
    {
        return;
    }

    Msg32->hwnd = (ULONG)(ULONG_PTR)Msg64->hwnd;
    Msg32->message = Msg64->message;
    Msg32->wParam = (ULONG)Msg64->wParam;
    Msg32->lParam = (ULONG)Msg64->lParam;
    Msg32->time = Msg64->time;
    Msg32->x = Msg64->pt.x;
    Msg32->y = Msg64->pt.y;
}

/*
 * Convert a 32-bit CREATESTRUCT to 64-bit CREATESTRUCT
 * Used during window creation syscall thunking
 */
static VOID WOW64WIN_UNUSED
Wow64WinConvertCreateStruct32To64(
    _In_ PCCREATESTRUCT32 Cs32,
    _Out_ LPCREATESTRUCT Cs64)
{
    if (!Cs32 || !Cs64)
    {
        return;
    }

    Cs64->lpCreateParams = (LPVOID)(ULONG_PTR)Cs32->lpCreateParams;
    Cs64->hInstance = (HINSTANCE)(ULONG_PTR)Cs32->hInstance;
    Cs64->hMenu = (HMENU)(ULONG_PTR)Cs32->hMenu;
    Cs64->hwndParent = (HWND)(ULONG_PTR)Cs32->hwndParent;
    Cs64->cy = Cs32->cy;
    Cs64->cx = Cs32->cx;
    Cs64->y = Cs32->y;
    Cs64->x = Cs32->x;
    Cs64->style = Cs32->style;
    Cs64->lpszName = (LPCSTR)(ULONG_PTR)Cs32->lpszName;
    Cs64->lpszClass = (LPCSTR)(ULONG_PTR)Cs32->lpszClass;
    Cs64->dwExStyle = Cs32->dwExStyle;
}

/*
 * Convert a 32-bit WINDOWPOS to 64-bit WINDOWPOS
 * Used for window positioning operations
 */
static VOID WOW64WIN_UNUSED
Wow64WinConvertWindowPos32To64(
    _In_ PWINDOWPOS32 Wp32,
    _Out_ LPWINDOWPOS Wp64)
{
    if (!Wp32 || !Wp64)
    {
        return;
    }

    Wp64->hwnd = (HWND)(ULONG_PTR)Wp32->hwnd;
    Wp64->hwndInsertAfter = (HWND)(ULONG_PTR)Wp32->hwndInsertAfter;
    Wp64->x = Wp32->x;
    Wp64->y = Wp32->y;
    Wp64->cx = Wp32->cx;
    Wp64->cy = Wp32->cy;
    Wp64->flags = Wp32->flags;
}

/* ===================================================================
 * Window Station and Desktop Thunks
 * ================================================================ */

/*
 * Wow64NtUserOpenWindowStation
 *
 * Thunks NtUserOpenWindowStation from 32-bit to 64-bit.
 * Window stations are session-scoped objects that contain desktops.
 *
 * Arguments:
 *   ObjectName32     - 32-bit pointer to UNICODE_STRING naming the window station
 *   DesiredAccess    - ACCESS_MASK for the window station
 *   ObjectAttributes - Object attributes (unused for window stations)
 *
 * Returns:
 *   HWINSTA handle (same size on 32/64-bit, but stored as 32-bit in WOW64)
 */
NTSTATUS
NTAPI
Wow64NtUserOpenWindowStation(
    _In_ ULONG_PTR ObjectName32,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ObjectAttributes)
{
    UNREFERENCED_PARAMETER(ObjectName32);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(ObjectAttributes);

    /*
     * TODO: Implement window station syscall thunk
     *
     * 1. Convert 32-bit UNICODE_STRING pointer to 64-bit
     * 2. Call NtUserOpenWindowStation (64-bit)
     * 3. Return the HWINSTA handle (no conversion needed - handles are index-based)
     *
     * For now, we block GUI processes until this layer is complete.
     */

    Wow64WinReportStub("Wow64NtUserOpenWindowStation");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserCreateDesktop
 *
 * Thunks NtUserCreateDesktop from 32-bit to 64-bit.
 * Desktops are containers for windows and are children of window stations.
 *
 * Arguments:
 *   DesktopName32 - 32-bit pointer to UNICODE_STRING desktop name
 *   Flags         - Desktop creation flags
 *   DesiredAccess - ACCESS_MASK for the desktop
 *   Attributes32  - 32-bit pointer to attributes structure
 *   SectionName32 - 32-bit pointer to heap section name (usually NULL)
 *
 * Returns:
 *   HDESK handle to the created desktop
 */
NTSTATUS
NTAPI
Wow64NtUserCreateDesktop(
    _In_ ULONG_PTR DesktopName32,
    _In_ ULONG Flags,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG_PTR Attributes32,
    _In_ ULONG_PTR SectionName32)
{
    UNREFERENCED_PARAMETER(DesktopName32);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(Attributes32);
    UNREFERENCED_PARAMETER(SectionName32);

    Wow64WinReportStub("Wow64NtUserCreateDesktop");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserOpenDesktop
 *
 * Opens an existing desktop object for use by the calling thread.
 */
NTSTATUS
NTAPI
Wow64NtUserOpenDesktop(
    _In_ ULONG_PTR DesktopName32,
    _In_ ULONG Flags,
    _In_ ACCESS_MASK DesiredAccess)
{
    UNREFERENCED_PARAMETER(DesktopName32);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(DesiredAccess);

    Wow64WinReportStub("Wow64NtUserOpenDesktop");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserCloseDesktop
 *
 * Closes a desktop handle opened via NtUserCreateDesktop or NtUserOpenDesktop.
 */
NTSTATUS
NTAPI
Wow64NtUserCloseDesktop(
    _In_ HANDLE DesktopHandle)
{
    UNREFERENCED_PARAMETER(DesktopHandle);

    Wow64WinReportStub("Wow64NtUserCloseDesktop");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserSetThreadDesktop
 *
 * Associates a desktop with the calling thread.
 * Required before any windowing operations can occur.
 */
NTSTATUS
NTAPI
Wow64NtUserSetThreadDesktop(
    _In_ HANDLE DesktopHandle)
{
    UNREFERENCED_PARAMETER(DesktopHandle);

    Wow64WinReportStub("Wow64NtUserSetThreadDesktop");
    return STATUS_NOT_IMPLEMENTED;
}

/* ===================================================================
 * Message Loop Thunks
 * ================================================================ */

/*
 * Wow64NtUserGetMessage
 *
 * Retrieves a message from the calling thread's message queue.
 * This is the core of the Windows message loop.
 *
 * Arguments:
 *   Msg32     - 32-bit pointer to MSG32 structure to receive message
 *   Window    - HWND filter (NULL = all windows for this thread)
 *   MsgMin    - Minimum message value to retrieve
 *   MsgMax    - Maximum message value to retrieve
 *
 * Returns:
 *   TRUE if message retrieved (and not WM_QUIT)
 *   FALSE if WM_QUIT received
 *   -1 on error
 */
NTSTATUS
NTAPI
Wow64NtUserGetMessage(
    _In_ ULONG_PTR Msg32,
    _In_ HWND Window,
    _In_ UINT MsgMin,
    _In_ UINT MsgMax)
{
    MSG Msg64;
    PMSG32 pMsg32;
    BOOL Result;

    UNREFERENCED_PARAMETER(Window);
    UNREFERENCED_PARAMETER(MsgMin);
    UNREFERENCED_PARAMETER(MsgMax);

    if (!Msg32)
    {
        return STATUS_INVALID_PARAMETER;
    }

    pMsg32 = (PMSG32)(ULONG_PTR)Msg32;

    /*
     * TODO: Call 64-bit NtUserGetMessage
     *
     * For now, we simulate failure to block GUI apps until layer is complete.
     */

    RtlZeroMemory(&Msg64, sizeof(Msg64));
    Wow64WinConvertMsg64To32(&Msg64, pMsg32);

    Result = FALSE; /* Simulate WM_QUIT */

    Wow64WinReportStub("Wow64NtUserGetMessage");
    return (NTSTATUS)Result;
}

/*
 * Wow64NtUserPeekMessage
 *
 * Non-blocking variant of GetMessage. Checks the message queue without waiting.
 */
NTSTATUS
NTAPI
Wow64NtUserPeekMessage(
    _In_ ULONG_PTR Msg32,
    _In_ HWND Window,
    _In_ UINT MsgMin,
    _In_ UINT MsgMax,
    _In_ UINT RemoveMsg)
{
    UNREFERENCED_PARAMETER(Msg32);
    UNREFERENCED_PARAMETER(Window);
    UNREFERENCED_PARAMETER(MsgMin);
    UNREFERENCED_PARAMETER(MsgMax);
    UNREFERENCED_PARAMETER(RemoveMsg);

    Wow64WinReportStub("Wow64NtUserPeekMessage");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserDispatchMessage
 *
 * Dispatches a message to a window procedure.
 * The window procedure address is retrieved from the window's class.
 */
NTSTATUS
NTAPI
Wow64NtUserDispatchMessage(
    _In_ ULONG_PTR Msg32)
{
    UNREFERENCED_PARAMETER(Msg32);

    Wow64WinReportStub("Wow64NtUserDispatchMessage");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserWaitMessage
 *
 * Yields execution until a message is available in the thread's message queue.
 */
NTSTATUS
NTAPI
Wow64NtUserWaitMessage(VOID)
{
    Wow64WinReportStub("Wow64NtUserWaitMessage");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserMsgWaitForMultipleObjectsEx
 *
 * Waits for messages or kernel objects to become signaled.
 * Combines MsgWaitForMultipleObjects with WaitForMultipleObjectsEx.
 */
NTSTATUS
NTAPI
Wow64NtUserMsgWaitForMultipleObjectsEx(
    _In_ ULONG ObjectCount,
    _In_ ULONG_PTR Handles32,
    _In_ ULONG Timeout,
    _In_ ULONG WakeMask,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(ObjectCount);
    UNREFERENCED_PARAMETER(Handles32);
    UNREFERENCED_PARAMETER(Timeout);
    UNREFERENCED_PARAMETER(WakeMask);
    UNREFERENCED_PARAMETER(Flags);

    Wow64WinReportStub("Wow64NtUserMsgWaitForMultipleObjectsEx");
    return STATUS_NOT_IMPLEMENTED;
}

/* ===================================================================
 * Window Creation and Management Thunks
 * ================================================================ */

/*
 * Wow64NtUserCreateWindowEx
 *
 * Creates a window with extended styles.
 * This is the kernel-mode implementation of CreateWindowEx.
 *
 * Arguments:
 *   ExStyle       - Extended window style (WS_EX_*)
 *   ClassName32   - 32-bit pointer to class name (string or atom)
 *   WindowName32  - 32-bit pointer to window title
 *   Style         - Window style (WS_*)
 *   X, Y, W, H    - Window position and size
 *   ParentWindow  - Parent window handle
 *   Menu          - Menu handle or child window ID
 *   Instance      - Instance handle
 *   Param32       - 32-bit pointer to creation parameter
 *
 * Returns:
 *   HWND of created window, or NULL on failure
 */
NTSTATUS
NTAPI
Wow64NtUserCreateWindowEx(
    _In_ ULONG ExStyle,
    _In_ ULONG_PTR ClassName32,
    _In_ ULONG_PTR WindowName32,
    _In_ ULONG Style,
    _In_ INT X,
    _In_ INT Y,
    _In_ INT Width,
    _In_ INT Height,
    _In_ HWND ParentWindow,
    _In_ HMENU Menu,
    _In_ HINSTANCE Instance,
    _In_ ULONG_PTR Param32)
{
    UNREFERENCED_PARAMETER(ExStyle);
    UNREFERENCED_PARAMETER(ClassName32);
    UNREFERENCED_PARAMETER(WindowName32);
    UNREFERENCED_PARAMETER(Style);
    UNREFERENCED_PARAMETER(X);
    UNREFERENCED_PARAMETER(Y);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(ParentWindow);
    UNREFERENCED_PARAMETER(Menu);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Param32);

    Wow64WinReportStub("Wow64NtUserCreateWindowEx");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserDestroyWindow
 *
 * Destroys a window and its children.
 * Sends WM_DESTROY and WM_NCDESTROY messages before cleanup.
 */
NTSTATUS
NTAPI
Wow64NtUserDestroyWindow(
    _In_ HWND Window)
{
    UNREFERENCED_PARAMETER(Window);

    Wow64WinReportStub("Wow64NtUserDestroyWindow");
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64NtUserShowWindow
 *
 * Controls window visibility and state (show, hide, minimize, maximize).
 */
NTSTATUS
NTAPI
Wow64NtUserShowWindow(
    _In_ HWND Window,
    _In_ INT ShowCommand)
{
    UNREFERENCED_PARAMETER(Window);
    UNREFERENCED_PARAMETER(ShowCommand);

    Wow64WinReportStub("Wow64NtUserShowWindow");
    return STATUS_NOT_IMPLEMENTED;
}

/* ===================================================================
 * Wow64Win Public API
 * ================================================================ */

/*
 * Wow64LdrpLoadWow64Win
 *
 * Called by wow64.dll during GUI thread initialization.
 * Allows lazy loading of wow64win.dll only for GUI processes.
 *
 * Returns:
 *   STATUS_SUCCESS if wow64win.dll is ready for thunking
 *   STATUS_NOT_IMPLEMENTED if GUI layer is not yet functional
 */
NTSTATUS
NTAPI
Wow64LdrpLoadWow64Win(VOID)
{
    Wow64WinDebugTrace("Wow64LdrpLoadWow64Win called - GUI support is not yet implemented");

    /*
     * Return STATUS_NOT_IMPLEMENTED to indicate that GUI processes
     * should be blocked until wow64win.dll is fully functional.
     *
     * When this layer is complete, we'll:
     * 1. Initialize the win32k thunk table
     * 2. Register syscall dispatch handlers
     * 3. Set up shared memory for USER32 handle tables
     * 4. Return STATUS_SUCCESS to enable GUI operations
     */

    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Wow64SystemServiceEx - Win32k Syscall Dispatcher
 *
 * This function is called by wow64cpu.dll when a 32-bit process
 * executes a win32k syscall (NtUser* or NtGdi*).
 *
 * Arguments:
 *   ServiceNumber - Win32k syscall number (extracted from EAX/R10)
 *   Arguments32   - Array of 32-bit arguments from the 32-bit stack
 *
 * Returns:
 *   NTSTATUS from the thunked win32k syscall
 *
 * NOTE: This function is NOT exported in the current stub implementation.
 *       It will be exported once the thunk table is complete.
 */
NTSTATUS
NTAPI
Wow64SystemServiceEx_Win32k(
    _In_ ULONG ServiceNumber,
    _In_ ULONG_PTR *Arguments32)
{
    UNREFERENCED_PARAMETER(ServiceNumber);
    UNREFERENCED_PARAMETER(Arguments32);

    /*
     * TODO: Implement win32k syscall dispatch table
     *
     * This will mirror the wow64.dll syscall dispatcher, but for
     * the win32k service table (KeServiceDescriptorTableShadow).
     *
     * Dispatch flow:
     * 1. Validate ServiceNumber against win32k range
     * 2. Look up thunk function in dispatch table
     * 3. Convert 32-bit arguments to 64-bit format
     * 4. Call 64-bit NtUser/NtGdi syscall
     * 5. Convert result back to 32-bit format
     * 6. Return to wow64cpu.dll for context restoration
     */

    Wow64WinDebugTrace("Wow64SystemServiceEx_Win32k: Win32k syscall dispatching not yet implemented");
    return STATUS_NOT_IMPLEMENTED;
}

/* ===================================================================
 * DLL Entry Point
 * ================================================================ */

BOOL
WINAPI
DllMain(
    HINSTANCE Instance,
    DWORD Reason,
    LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reserved);

    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            Wow64WinDebugTrace("wow64win.dll loaded - GUI thunking layer initialized");
            break;

        case DLL_PROCESS_DETACH:
            Wow64WinDebugTrace("wow64win.dll unloaded");
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            /* No per-thread initialization required */
            break;
    }

    return TRUE;
}
