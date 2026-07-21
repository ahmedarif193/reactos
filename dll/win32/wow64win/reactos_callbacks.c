/*
 * ReactOS WoW64 user callback thunks
 *
 * This file is included by user.c so that the ReactOS-specific callback ABI
 * can reuse Wine's message packing helpers without exporting private symbols.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

enum ros_user_callback
{
    ROS_USER_CALLBACK_WINDOWPROC,
    ROS_USER_CALLBACK_SENDASYNCPROC,
    ROS_USER_CALLBACK_LOADSYSMENUTEMPLATE,
    ROS_USER_CALLBACK_LOADDEFAULTCURSORS,
    ROS_USER_CALLBACK_HOOKPROC,
    ROS_USER_CALLBACK_EVENTPROC,
    ROS_USER_CALLBACK_LOADMENU,
    ROS_USER_CALLBACK_CLIENTTHREADSTARTUP,
    ROS_USER_CALLBACK_CLIENTLOADLIBRARY,
    ROS_USER_CALLBACK_GETCHARSETINFO,
    ROS_USER_CALLBACK_COPYIMAGE,
    ROS_USER_CALLBACK_SETWNDICONS,
    ROS_USER_CALLBACK_DELIVERUSERAPC,
    ROS_USER_CALLBACK_DDEPOST,
    ROS_USER_CALLBACK_DDEGET,
    ROS_USER_CALLBACK_SETOBM,
    ROS_USER_CALLBACK_LPK,
    ROS_USER_CALLBACK_UMPD,
    ROS_USER_CALLBACK_IMMPROCESSKEY,
    ROS_USER_CALLBACK_IMMLOADLAYOUT,
    ROS_USER_CALLBACK_COUNT
};

typedef struct
{
    ULONG Proc;
    BOOL IsAnsiProc;
    ULONG Wnd;
    UINT Msg;
    ULONG wParam;
    LONG lParam;
    INT lParamBufferSize;
    LONG Result;
} ROS_WINDOWPROC_CALLBACK_ARGUMENTS32;

typedef struct
{
    WNDPROC Proc;
    BOOL IsAnsiProc;
    HWND Wnd;
    UINT Msg;
    WPARAM wParam;
    LPARAM lParam;
    INT lParamBufferSize;
    LRESULT Result;
} ROS_WINDOWPROC_CALLBACK_ARGUMENTS64;

typedef struct
{
    ULONG Callback;
    ULONG Wnd;
    UINT Msg;
    ULONG Context;
    LONG Result;
} ROS_SENDASYNCPROC_CALLBACK_ARGUMENTS32;

typedef struct
{
    SENDASYNCPROC Callback;
    HWND Wnd;
    UINT Msg;
    ULONG_PTR Context;
    LRESULT Result;
} ROS_SENDASYNCPROC_CALLBACK_ARGUMENTS64;

typedef struct
{
    INT HookId;
    INT Code;
    ULONG wParam;
    LONG lParam;
    ULONG Proc;
    INT Mod;
    ULONG offPfn;
    BOOLEAN Ansi;
    BYTE Padding[3];
    LONG Result;
    UINT lParamSize;
    WCHAR ModuleName[512];
} ROS_HOOKPROC_CALLBACK_ARGUMENTS32;

typedef struct
{
    INT HookId;
    INT Code;
    WPARAM wParam;
    LPARAM lParam;
    HOOKPROC Proc;
    INT Mod;
    ULONG_PTR offPfn;
    BOOLEAN Ansi;
    LRESULT Result;
    UINT lParamSize;
    WCHAR ModuleName[512];
} ROS_HOOKPROC_CALLBACK_ARGUMENTS64;

typedef struct
{
    CREATESTRUCT32 Cs;
    ULONG WndInsertAfter;
} ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS32;

typedef struct
{
    CREATESTRUCTW Cs;
    HWND WndInsertAfter;
} ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS64;

typedef struct
{
    ROS_HOOKPROC_CALLBACK_ARGUMENTS32 hpca;
    CWPSTRUCT32 cwps;
    ULONG Extra[4];
} ROS_CWP_STRUCT32;

typedef struct
{
    ROS_HOOKPROC_CALLBACK_ARGUMENTS64 hpca;
    CWPSTRUCT cwps;
    BYTE *Extra[4];
} ROS_CWP_STRUCT64;

typedef struct
{
    ROS_HOOKPROC_CALLBACK_ARGUMENTS32 hpca;
    CWPRETSTRUCT32 cwprs;
    ULONG Extra[4];
} ROS_CWPR_STRUCT32;

typedef struct
{
    ROS_HOOKPROC_CALLBACK_ARGUMENTS64 hpca;
    CWPRETSTRUCT cwprs;
    BYTE *Extra[4];
} ROS_CWPR_STRUCT64;

typedef struct
{
    POINT pt;
    ULONG hwnd;
    UINT wHitTestCode;
    ULONG dwExtraInfo;
} ROS_MOUSEHOOKSTRUCT32;

typedef struct
{
    ULONG hook;
    DWORD event;
    ULONG hwnd;
    LONG idObject;
    LONG idChild;
    DWORD dwEventThread;
    DWORD dwmsEventTime;
    ULONG Proc;
    LONG Mod;
    ULONG offPfn;
} ROS_EVENTPROC_CALLBACK_ARGUMENTS32;

typedef struct
{
    HWINEVENTHOOK hook;
    DWORD event;
    HWND hwnd;
    LONG idObject;
    LONG idChild;
    DWORD dwEventThread;
    DWORD dwmsEventTime;
    WINEVENTPROC Proc;
    INT_PTR Mod;
    ULONG_PTR offPfn;
} ROS_EVENTPROC_CALLBACK_ARGUMENTS64;

typedef struct
{
    ULONG hModule;
    ULONG InterSource;
    WCHAR MenuName[1];
} ROS_LOADMENU_CALLBACK_ARGUMENTS32;

typedef struct
{
    HINSTANCE hModule;
    LPCWSTR InterSource;
    WCHAR MenuName[1];
} ROS_LOADMENU_CALLBACK_ARGUMENTS64;

typedef struct
{
    UNICODE_STRING32 strLibraryName;
    UNICODE_STRING32 strInitFuncName;
    BOOL Unload;
    BOOL ApiHook;
} ROS_CLIENT_LOAD_LIBRARY_ARGUMENTS32;

typedef struct
{
    UNICODE_STRING strLibraryName;
    UNICODE_STRING strInitFuncName;
    BOOL Unload;
    BOOL ApiHook;
} ROS_CLIENT_LOAD_LIBRARY_ARGUMENTS64;

typedef struct
{
    ULONG hImage;
    UINT uType;
    INT cxDesired;
    INT cyDesired;
    UINT fuFlags;
} ROS_COPYIMAGE_CALLBACK_ARGUMENTS32;

typedef struct
{
    HANDLE hImage;
    UINT uType;
    INT cxDesired;
    INT cyDesired;
    UINT fuFlags;
} ROS_COPYIMAGE_CALLBACK_ARGUMENTS64;

typedef struct
{
    ULONG hIconSample;
    ULONG hIconHand;
    ULONG hIconQuestion;
    ULONG hIconBang;
    ULONG hIconNote;
    ULONG hIconWindows;
    ULONG hIconSmWindows;
} ROS_SETWNDICONS_CALLBACK_ARGUMENTS32;

typedef struct
{
    HICON hIconSample;
    HICON hIconHand;
    HICON hIconQuestion;
    HICON hIconBang;
    HICON hIconNote;
    HICON hIconWindows;
    HICON hIconSmWindows;
} ROS_SETWNDICONS_CALLBACK_ARGUMENTS64;

typedef struct
{
    INT Type;
    MSG32 Msg;
    INT size;
    ULONG pvData;
    BYTE buffer[1];
} ROS_DDEPOSTGET_CALLBACK_ARGUMENTS32;

typedef struct
{
    INT Type;
    MSG Msg;
    INT size;
    PVOID pvData;
    BYTE buffer[1];
} ROS_DDEPOSTGET_CALLBACK_ARGUMENTS64;

typedef struct
{
    ULONG lpString;
    ULONG hdc;
    INT x;
    INT y;
    UINT flags;
    RECT rect;
    UINT count;
    BOOL bRect;
} ROS_LPK_CALLBACK_ARGUMENTS32;

typedef struct
{
    LPWSTR lpString;
    HDC hdc;
    INT x;
    INT y;
    UINT flags;
    RECT rect;
    UINT count;
    BOOL bRect;
} ROS_LPK_CALLBACK_ARGUMENTS64;

typedef struct
{
    ULONG hWnd;
    ULONG hKL;
    UINT vKey;
    LONG lParam;
    DWORD dwHotKeyID;
} ROS_IMMPROCESSKEY_CALLBACK_ARGUMENTS32;

typedef struct
{
    HWND hWnd;
    HKL hKL;
    UINT vKey;
    LPARAM lParam;
    DWORD dwHotKeyID;
} ROS_IMMPROCESSKEY_CALLBACK_ARGUMENTS64;

typedef struct
{
    ULONG hkl;
    IMEINFO ImeInfo;
    WCHAR wszUIClass[16];
    ULONG fdwInitConvMode;
    INT fInitOpen;
    INT fLoadFlag;
    DWORD dwProdVersion;
    DWORD dwImeWinVersion;
    WCHAR wszImeDescription[50];
    WCHAR wszImeFile[80];
    ULONG Flags;
} ROS_IMEINFOEX32;

typedef struct
{
    HKL hkl;
    IMEINFO ImeInfo;
    WCHAR wszUIClass[16];
    ULONG fdwInitConvMode;
    INT fInitOpen;
    INT fLoadFlag;
    DWORD dwProdVersion;
    DWORD dwImeWinVersion;
    WCHAR wszImeDescription[50];
    WCHAR wszImeFile[80];
    ULONG Flags;
} ROS_IMEINFOEX64;

typedef struct
{
    BOOL ret;
    ROS_IMEINFOEX32 iiex;
} ROS_IMMLOADLAYOUT_CALLBACK_OUTPUT32;

typedef struct
{
    BOOL ret;
    ROS_IMEINFOEX64 iiex;
} ROS_IMMLOADLAYOUT_CALLBACK_OUTPUT64;

C_ASSERT(sizeof(ROS_WINDOWPROC_CALLBACK_ARGUMENTS32) == 32);
C_ASSERT(sizeof(ROS_WINDOWPROC_CALLBACK_ARGUMENTS64) == 64);
C_ASSERT(sizeof(ROS_SENDASYNCPROC_CALLBACK_ARGUMENTS32) == 20);
C_ASSERT(sizeof(ROS_SENDASYNCPROC_CALLBACK_ARGUMENTS64) == 40);
C_ASSERT(sizeof(ROS_HOOKPROC_CALLBACK_ARGUMENTS32) == 1064);
C_ASSERT(sizeof(ROS_HOOKPROC_CALLBACK_ARGUMENTS64) == 1096);
C_ASSERT(sizeof(ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS32) == 52);
C_ASSERT(sizeof(ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS64) == 88);
C_ASSERT(sizeof(ROS_CWP_STRUCT32) == 1096);
C_ASSERT(sizeof(ROS_CWP_STRUCT64) == 1160);
C_ASSERT(sizeof(ROS_CWPR_STRUCT32) == 1100);
C_ASSERT(sizeof(ROS_CWPR_STRUCT64) == 1168);
C_ASSERT(sizeof(ROS_MOUSEHOOKSTRUCT32) == 20);
C_ASSERT(sizeof(ROS_EVENTPROC_CALLBACK_ARGUMENTS32) == 40);
C_ASSERT(sizeof(ROS_EVENTPROC_CALLBACK_ARGUMENTS64) == 64);
C_ASSERT(sizeof(ROS_LOADMENU_CALLBACK_ARGUMENTS32) == 12);
C_ASSERT(sizeof(ROS_LOADMENU_CALLBACK_ARGUMENTS64) == 24);
C_ASSERT(sizeof(ROS_CLIENT_LOAD_LIBRARY_ARGUMENTS32) == 24);
C_ASSERT(sizeof(ROS_CLIENT_LOAD_LIBRARY_ARGUMENTS64) == 40);
C_ASSERT(sizeof(ROS_COPYIMAGE_CALLBACK_ARGUMENTS32) == 20);
C_ASSERT(sizeof(ROS_COPYIMAGE_CALLBACK_ARGUMENTS64) == 24);
C_ASSERT(sizeof(ROS_SETWNDICONS_CALLBACK_ARGUMENTS32) == 28);
C_ASSERT(sizeof(ROS_SETWNDICONS_CALLBACK_ARGUMENTS64) == 56);
C_ASSERT(sizeof(ROS_DDEPOSTGET_CALLBACK_ARGUMENTS32) == 44);
C_ASSERT(sizeof(ROS_DDEPOSTGET_CALLBACK_ARGUMENTS64) == 80);
C_ASSERT(sizeof(ROS_LPK_CALLBACK_ARGUMENTS32) == 44);
C_ASSERT(sizeof(ROS_LPK_CALLBACK_ARGUMENTS64) == 56);
C_ASSERT(sizeof(ROS_IMMPROCESSKEY_CALLBACK_ARGUMENTS32) == 20);
C_ASSERT(sizeof(ROS_IMMPROCESSKEY_CALLBACK_ARGUMENTS64) == 40);
C_ASSERT(sizeof(ROS_IMEINFOEX32) == 348);
C_ASSERT(sizeof(ROS_IMEINFOEX64) == 352);
C_ASSERT(sizeof(ROS_IMMLOADLAYOUT_CALLBACK_OUTPUT32) == 352);
C_ASSERT(sizeof(ROS_IMMLOADLAYOUT_CALLBACK_OUTPUT64) == 360);
C_ASSERT(ROS_USER_CALLBACK_COUNT == 20);

static NTSTATUS ros_callback_bad_length(void)
{
    return NtCallbackReturn(NULL, 0, STATUS_INFO_LENGTH_MISMATCH);
}

static NTSTATUS ros_callback_dispatch(ULONG id, void *args, ULONG len)
{
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    NTSTATUS status;

    status = Wow64KiUserCallbackDispatcher(id, args, len, &ret_ptr, &ret_len);
    return NtCallbackReturn(ret_ptr, ret_len, status);
}

static BOOL ros_wow64_map_window_proc(WNDPROC proc, BOOL ansi, ULONG *proc32)
{
    const ROS_PFNCLIENT64 *native_procs;
    const ROS_PFNCLIENT32 *wow64_procs;
    ULONG_PTR address = (ULONG_PTR)proc;
    UINT i;

    if (address <= MAXDWORD)
    {
        *proc32 = (ULONG)address;
        return TRUE;
    }
    if (!ros_server_info || !ros_client_procs_initialized) return FALSE;

    native_procs = ansi ? &ros_server_info->apfnClientA : &ros_server_info->apfnClientW;
    wow64_procs = ansi ? &ros_client_procs_a : &ros_client_procs_w;
    for (i = 0; i < ROS_PFNCLIENT_COUNT; ++i)
    {
        if (native_procs->Functions[i] == address)
        {
            *proc32 = wow64_procs->Functions[i];
            return TRUE;
        }
    }
    for (i = 0; i < ROS_SERVERPROC_COUNT; ++i)
    {
        if (ros_server_info->aStoCidPfn[i] == address)
        {
            *proc32 = wow64_procs->Functions[i];
            return TRUE;
        }
    }
    return FALSE;
}

static NTSTATUS WINAPI ros_wow64_window_proc(void *arg, ULONG size)
{
    ROS_WINDOWPROC_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_WINDOWPROC_CALLBACK_ARGUMENTS32 *args32;
    ULONG len32 = sizeof(*args32);
    ULONG buffer_size = 0;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    NTSTATUS status;

    if (size < sizeof(*args64)) return ros_callback_bad_length();
    if (args64->lParamBufferSize >= 0)
    {
        buffer_size = args64->lParamBufferSize;
        if (size != sizeof(*args64) + buffer_size) return ros_callback_bad_length();
    }
    else if (size != sizeof(*args64)) return ros_callback_bad_length();
    if (!(args32 = Wow64AllocateTemp(sizeof(*args32) + buffer_size + 64))) return NtCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    if (!ros_wow64_map_window_proc(args64->Proc, args64->IsAnsiProc, &args32->Proc)) return NtCallbackReturn(NULL, 0, STATUS_INVALID_ADDRESS);
    args32->IsAnsiProc = args64->IsAnsiProc;
    args32->Wnd = HandleToUlong(args64->Wnd);
    args32->Msg = args64->Msg;
    args32->wParam = args64->wParam;
    args32->lParam = args64->lParam;
    args32->lParamBufferSize = args64->lParamBufferSize;
    args32->Result = args64->Result;
    if (args64->lParamBufferSize >= 0)
    {
        buffer_size = packed_message_64to32(args64->Msg, args64->wParam, args64 + 1, args32 + 1, buffer_size);
        args32->lParamBufferSize = buffer_size;
        len32 += buffer_size;
    }
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_WINDOWPROC, args32, len32, &ret_ptr, &ret_len);
    if (status || ret_len < sizeof(*args32)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    args64->Result = ((ROS_WINDOWPROC_CALLBACK_ARGUMENTS32 *)ret_ptr)->Result;
    if (args64->lParamBufferSize >= 0) packed_result_32to64(args64->Msg, args64->wParam, (ROS_WINDOWPROC_CALLBACK_ARGUMENTS32 *)ret_ptr + 1, ret_len - sizeof(*args32), args64 + 1);
    return NtCallbackReturn(args64, size, status);
}

static NTSTATUS WINAPI ros_wow64_send_async_proc(void *arg, ULONG size)
{
    const ROS_SENDASYNCPROC_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_SENDASYNCPROC_CALLBACK_ARGUMENTS32 args32;

    if (size != sizeof(*args64)) return ros_callback_bad_length();
    args32.Callback = PtrToUlong(args64->Callback);
    args32.Wnd = HandleToUlong(args64->Wnd);
    args32.Msg = args64->Msg;
    args32.Context = args64->Context;
    args32.Result = args64->Result;
    return ros_callback_dispatch(ROS_USER_CALLBACK_SENDASYNCPROC, &args32, sizeof(args32));
}

static NTSTATUS WINAPI ros_wow64_load_sysmenu_template(void *arg, ULONG size)
{
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    LRESULT result;
    NTSTATUS status;

    if (size) return ros_callback_bad_length();
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_LOADSYSMENUTEMPLATE, arg, 0, &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(LONG)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    result = (LRESULT)LongToHandle(*(LONG *)ret_ptr);
    return NtCallbackReturn(&result, sizeof(result), status);
}

static NTSTATUS WINAPI ros_wow64_load_default_cursors(void *arg, ULONG size)
{
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    HCURSOR result;
    NTSTATUS status;

    if (size != sizeof(BOOL)) return ros_callback_bad_length();
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_LOADDEFAULTCURSORS, arg, size, &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(ULONG)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    result = LongToHandle(*(LONG *)ret_ptr);
    return NtCallbackReturn(&result, sizeof(result), status);
}

static void ros_hook_common_64to32(const ROS_HOOKPROC_CALLBACK_ARGUMENTS64 *args64, ROS_HOOKPROC_CALLBACK_ARGUMENTS32 *args32)
{
    memset(args32, 0, sizeof(*args32));
    args32->HookId = args64->HookId;
    args32->Code = args64->Code;
    args32->wParam = args64->wParam;
    args32->lParam = args64->lParam;
    args32->Proc = PtrToUlong(args64->Proc);
    args32->Mod = args64->Mod;
    args32->offPfn = args64->offPfn;
    args32->Ansi = args64->Ansi;
    args32->Result = args64->Result;
    args32->lParamSize = args64->lParamSize;
    memcpy(args32->ModuleName, args64->ModuleName, sizeof(args32->ModuleName));
}

static ULONG ros_hook_extra_64to32(const ROS_HOOKPROC_CALLBACK_ARGUMENTS64 *args64, const void *extra64, ULONG extra_size64, void *extra32)
{
    switch (args64->HookId)
    {
    case WH_CBT:
        switch (args64->Code)
        {
        case HCBT_CREATEWND:
            if (extra_size64 >= sizeof(ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS64))
            {
                const ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS64 *create64 = extra64;
                ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS32 *create32 = extra32;
                createstruct_64to32(&create64->Cs, &create32->Cs);
                create32->WndInsertAfter = HandleToUlong(create64->WndInsertAfter);
                return sizeof(*create32);
            }
            break;
        case HCBT_MOVESIZE:
            if (extra_size64 >= sizeof(RECT))
            {
                memcpy(extra32, extra64, sizeof(RECT));
                return sizeof(RECT);
            }
            break;
        case HCBT_ACTIVATE:
            if (extra_size64 >= sizeof(CBTACTIVATESTRUCT))
            {
                const CBTACTIVATESTRUCT *activate64 = extra64;
                CBTACTIVATESTRUCT32 *activate32 = extra32;
                activate32->fMouse = activate64->fMouse;
                activate32->hWndActive = HandleToUlong(activate64->hWndActive);
                return sizeof(*activate32);
            }
            break;
        case HCBT_CLICKSKIPPED:
            if (extra_size64 >= sizeof(MOUSEHOOKSTRUCT))
            {
                const MOUSEHOOKSTRUCT *mouse64 = extra64;
                ROS_MOUSEHOOKSTRUCT32 *mouse32 = extra32;
                mouse32->pt = mouse64->pt;
                mouse32->hwnd = HandleToUlong(mouse64->hwnd);
                mouse32->wHitTestCode = mouse64->wHitTestCode;
                mouse32->dwExtraInfo = mouse64->dwExtraInfo;
                return sizeof(*mouse32);
            }
            break;
        }
        break;
    case WH_MOUSE:
        if (extra_size64 >= sizeof(MOUSEHOOKSTRUCT))
        {
            const MOUSEHOOKSTRUCT *mouse64 = extra64;
            ROS_MOUSEHOOKSTRUCT32 *mouse32 = extra32;
            mouse32->pt = mouse64->pt;
            mouse32->hwnd = HandleToUlong(mouse64->hwnd);
            mouse32->wHitTestCode = mouse64->wHitTestCode;
            mouse32->dwExtraInfo = mouse64->dwExtraInfo;
            return sizeof(*mouse32);
        }
        break;
    default:
        return hook_lparam_64to32(args64->HookId, args64->Code, extra64, extra_size64, extra32);
    }
    return 0;
}

static NTSTATUS WINAPI ros_wow64_hook_proc(void *arg, ULONG size)
{
    ROS_HOOKPROC_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_HOOKPROC_CALLBACK_ARGUMENTS32 *args32;
    ULONG size32;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    NTSTATUS status;

    if (size < sizeof(*args64)) return ros_callback_bad_length();
    if (!(args32 = Wow64AllocateTemp(size + sizeof(*args32)))) return NtCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    ros_hook_common_64to32(args64, args32);
    size32 = sizeof(*args32);

    if (args64->HookId == WH_CALLWNDPROC)
    {
        const ROS_CWP_STRUCT64 *cwp64 = arg;
        ROS_CWP_STRUCT32 *cwp32 = (ROS_CWP_STRUCT32 *)args32;
        ULONG packed_size = 0;

        if (size < sizeof(*cwp64)) return ros_callback_bad_length();
        cwp32->cwps.lParam = cwp64->cwps.lParam;
        cwp32->cwps.wParam = cwp64->cwps.wParam;
        cwp32->cwps.message = cwp64->cwps.message;
        cwp32->cwps.hwnd = HandleToUlong(cwp64->cwps.hwnd);
        memset(cwp32->Extra, 0, sizeof(cwp32->Extra));
        if (args64->lParamSize) packed_size = packed_message_64to32(cwp64->cwps.message, cwp64->cwps.wParam, cwp64->Extra, cwp32->Extra, args64->lParamSize);
        cwp32->hpca.lParamSize = packed_size;
        size32 = sizeof(*cwp32) + packed_size;
    }
    else if (args64->HookId == WH_CALLWNDPROCRET)
    {
        const ROS_CWPR_STRUCT64 *cwpr64 = arg;
        ROS_CWPR_STRUCT32 *cwpr32 = (ROS_CWPR_STRUCT32 *)args32;
        ULONG packed_size = 0;

        if (size < sizeof(*cwpr64)) return ros_callback_bad_length();
        cwpr32->cwprs.lResult = cwpr64->cwprs.lResult;
        cwpr32->cwprs.lParam = cwpr64->cwprs.lParam;
        cwpr32->cwprs.wParam = cwpr64->cwprs.wParam;
        cwpr32->cwprs.message = cwpr64->cwprs.message;
        cwpr32->cwprs.hwnd = HandleToUlong(cwpr64->cwprs.hwnd);
        memset(cwpr32->Extra, 0, sizeof(cwpr32->Extra));
        if (args64->lParamSize) packed_size = packed_message_64to32(cwpr64->cwprs.message, cwpr64->cwprs.wParam, cwpr64->Extra, cwpr32->Extra, args64->lParamSize);
        cwpr32->hpca.lParamSize = packed_size;
        size32 = sizeof(*cwpr32) + packed_size;
    }
    else if (size > sizeof(*args64))
    {
        ULONG extra_size = ros_hook_extra_64to32(args64, args64 + 1, size - sizeof(*args64), args32 + 1);
        args32->lParam = sizeof(*args32);
        size32 += extra_size;
    }

    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_HOOKPROC, args32, size32, &ret_ptr, &ret_len);
    if (status || ret_len < sizeof(*args32)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    args64->Result = ((ROS_HOOKPROC_CALLBACK_ARGUMENTS32 *)ret_ptr)->Result;

    if (args64->HookId == WH_CBT && args64->Code == HCBT_CREATEWND && size >= sizeof(*args64) + sizeof(ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS64) && ret_len >= sizeof(*args32) + sizeof(ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS32))
    {
        ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS64 *create64 = (ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS64 *)(args64 + 1);
        const ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS32 *create32 = (const ROS_HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS32 *)((const ROS_HOOKPROC_CALLBACK_ARGUMENTS32 *)ret_ptr + 1);
        create64->WndInsertAfter = LongToHandle(create32->WndInsertAfter);
        create64->Cs.x = create32->Cs.x;
        create64->Cs.y = create32->Cs.y;
        create64->Cs.cx = create32->Cs.cx;
        create64->Cs.cy = create32->Cs.cy;
    }
    else if (args64->HookId == WH_CBT && args64->Code == HCBT_MOVESIZE && size >= sizeof(*args64) + sizeof(RECT) && ret_len >= sizeof(*args32) + sizeof(RECT)) memcpy(args64 + 1, (ROS_HOOKPROC_CALLBACK_ARGUMENTS32 *)ret_ptr + 1, sizeof(RECT));
    else if (args64->HookId == WH_GETMESSAGE && size >= sizeof(*args64) + sizeof(MSG) && ret_len >= sizeof(*args32) + sizeof(MSG32)) msg_32to64((MSG *)(args64 + 1), (const MSG32 *)((ROS_HOOKPROC_CALLBACK_ARGUMENTS32 *)ret_ptr + 1));

    return NtCallbackReturn(args64, size, status);
}

static NTSTATUS WINAPI ros_wow64_event_proc(void *arg, ULONG size)
{
    const ROS_EVENTPROC_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_EVENTPROC_CALLBACK_ARGUMENTS32 args32;

    if (size != sizeof(*args64)) return ros_callback_bad_length();
    args32.hook = HandleToUlong(args64->hook);
    args32.event = args64->event;
    args32.hwnd = HandleToUlong(args64->hwnd);
    args32.idObject = args64->idObject;
    args32.idChild = args64->idChild;
    args32.dwEventThread = args64->dwEventThread;
    args32.dwmsEventTime = args64->dwmsEventTime;
    args32.Proc = PtrToUlong(args64->Proc);
    args32.Mod = args64->Mod;
    args32.offPfn = args64->offPfn;
    return ros_callback_dispatch(ROS_USER_CALLBACK_EVENTPROC, &args32, sizeof(args32));
}

static NTSTATUS WINAPI ros_wow64_load_menu(void *arg, ULONG size)
{
    const ROS_LOADMENU_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_LOADMENU_CALLBACK_ARGUMENTS32 *args32;
    TEB *teb = NtCurrentTeb();
    TEB32 *teb32 = (TEB32 *)((char *)teb + teb->WowTebOffset);
    PEB32 *peb32 = ULongToPtr(teb32->Peb);
    ULONG *callbacks32 = ULongToPtr(peb32->KernelCallbackTable);
    MEMORY_BASIC_INFORMATION memory_info;
    SIZE_T memory_info_size;
    void *module32 = NULL;
    ULONG len32 = sizeof(*args32);
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    HMENU result;
    NTSTATUS status;

    if (size < sizeof(*args64)) return ros_callback_bad_length();
    if (!(args32 = Wow64AllocateTemp(size))) return NtCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    memset(args32, 0, size);
    if ((ULONG_PTR)args64->hModule >> 32 && !NtQueryVirtualMemory(NtCurrentProcess(), callbacks32, MemoryBasicInformation, &memory_info, sizeof(memory_info), &memory_info_size)) module32 = memory_info.AllocationBase;
    args32->hModule = module32 ? PtrToUlong(module32) : HandleToUlong(args64->hModule);
    args32->InterSource = PtrToUlong(args64->InterSource);
    if (!args64->InterSource)
    {
        ULONG name_size = size - FIELD_OFFSET(ROS_LOADMENU_CALLBACK_ARGUMENTS64, MenuName);
        memcpy(args32->MenuName, args64->MenuName, name_size);
        len32 = FIELD_OFFSET(ROS_LOADMENU_CALLBACK_ARGUMENTS32, MenuName) + name_size;
    }
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_LOADMENU, args32, len32, &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(ULONG)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    result = LongToHandle(*(LONG *)ret_ptr);
    return NtCallbackReturn(&result, sizeof(result), status);
}

static NTSTATUS WINAPI ros_wow64_client_thread_startup(void *arg, ULONG size)
{
    if (size) return ros_callback_bad_length();
    return ros_callback_dispatch(ROS_USER_CALLBACK_CLIENTTHREADSTARTUP, arg, 0);
}

static ULONG ros_copy_relative_unicode_string32(UNICODE_STRING32 *dst, const UNICODE_STRING *src, const void *base64, BYTE *base32, ULONG offset32)
{
    dst->Length = src->Length;
    dst->MaximumLength = src->MaximumLength;
    dst->Buffer = 0;
    if (!src->Buffer) return offset32;
    dst->Buffer = offset32;
    memcpy(base32 + offset32, (const BYTE *)base64 + (ULONG_PTR)src->Buffer, src->MaximumLength);
    return offset32 + src->MaximumLength;
}

static NTSTATUS WINAPI ros_wow64_client_load_library(void *arg, ULONG size)
{
    const ROS_CLIENT_LOAD_LIBRARY_ARGUMENTS64 *args64 = arg;
    ROS_CLIENT_LOAD_LIBRARY_ARGUMENTS32 *args32;
    ULONG len32 = sizeof(*args32);
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    ULONG_PTR result = 0;
    NTSTATUS status;

    if (size < sizeof(*args64)) return ros_callback_bad_length();
    if (!(args32 = Wow64AllocateTemp(size))) return NtCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    memset(args32, 0, size);
    len32 = ros_copy_relative_unicode_string32(&args32->strLibraryName, &args64->strLibraryName, args64, (BYTE *)args32, len32);
    len32 = ros_copy_relative_unicode_string32(&args32->strInitFuncName, &args64->strInitFuncName, args64, (BYTE *)args32, len32);
    args32->Unload = args64->Unload;
    args32->ApiHook = args64->ApiHook;
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_CLIENTLOADLIBRARY, args32, len32, &ret_ptr, &ret_len);
    if (status || ret_len < sizeof(BOOL)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    result = *(BOOL *)ret_ptr;
    return NtCallbackReturn(&result, sizeof(result), status);
}

static NTSTATUS WINAPI ros_wow64_get_charset_info(void *arg, ULONG size)
{
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    NTSTATUS status;

    if (size != sizeof(LCID) + sizeof(CHARSETINFO)) return ros_callback_bad_length();
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_GETCHARSETINFO, arg, size, &ret_ptr, &ret_len);
    if (status || ret_len != size) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    memcpy(arg, ret_ptr, size);
    return NtCallbackReturn(arg, size, status);
}

static NTSTATUS WINAPI ros_wow64_copy_image(void *arg, ULONG size)
{
    const ROS_COPYIMAGE_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_COPYIMAGE_CALLBACK_ARGUMENTS32 args32;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    HANDLE result;
    NTSTATUS status;

    if (size != sizeof(*args64)) return ros_callback_bad_length();
    args32.hImage = HandleToUlong(args64->hImage);
    args32.uType = args64->uType;
    args32.cxDesired = args64->cxDesired;
    args32.cyDesired = args64->cyDesired;
    args32.fuFlags = args64->fuFlags;
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_COPYIMAGE, &args32, sizeof(args32), &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(ULONG)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    result = LongToHandle(*(LONG *)ret_ptr);
    return NtCallbackReturn(&result, sizeof(result), status);
}

static void ros_icons_64to32(const ROS_SETWNDICONS_CALLBACK_ARGUMENTS64 *args64, ROS_SETWNDICONS_CALLBACK_ARGUMENTS32 *args32)
{
    args32->hIconSample = HandleToUlong(args64->hIconSample);
    args32->hIconHand = HandleToUlong(args64->hIconHand);
    args32->hIconQuestion = HandleToUlong(args64->hIconQuestion);
    args32->hIconBang = HandleToUlong(args64->hIconBang);
    args32->hIconNote = HandleToUlong(args64->hIconNote);
    args32->hIconWindows = HandleToUlong(args64->hIconWindows);
    args32->hIconSmWindows = HandleToUlong(args64->hIconSmWindows);
}

static void ros_icons_32to64(const ROS_SETWNDICONS_CALLBACK_ARGUMENTS32 *args32, ROS_SETWNDICONS_CALLBACK_ARGUMENTS64 *args64)
{
    args64->hIconSample = LongToHandle(args32->hIconSample);
    args64->hIconHand = LongToHandle(args32->hIconHand);
    args64->hIconQuestion = LongToHandle(args32->hIconQuestion);
    args64->hIconBang = LongToHandle(args32->hIconBang);
    args64->hIconNote = LongToHandle(args32->hIconNote);
    args64->hIconWindows = LongToHandle(args32->hIconWindows);
    args64->hIconSmWindows = LongToHandle(args32->hIconSmWindows);
}

static NTSTATUS WINAPI ros_wow64_set_wnd_icons(void *arg, ULONG size)
{
    ROS_SETWNDICONS_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_SETWNDICONS_CALLBACK_ARGUMENTS32 args32;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    NTSTATUS status;

    if (size != sizeof(*args64)) return ros_callback_bad_length();
    ros_icons_64to32(args64, &args32);
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_SETWNDICONS, &args32, sizeof(args32), &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(args32)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    ros_icons_32to64(ret_ptr, args64);
    return NtCallbackReturn(args64, size, status);
}

static NTSTATUS WINAPI ros_wow64_deliver_user_apc(void *arg, ULONG size)
{
    if (size) return ros_callback_bad_length();
    return ros_callback_dispatch(ROS_USER_CALLBACK_DELIVERUSERAPC, arg, 0);
}

static NTSTATUS ros_wow64_dde_callback(ULONG id, void *arg, ULONG size)
{
    ROS_DDEPOSTGET_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_DDEPOSTGET_CALLBACK_ARGUMENTS32 *args32;
    ROS_DDEPOSTGET_CALLBACK_ARGUMENTS32 *ret32;
    ULONG buffer_size;
    ULONG len32;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    NTSTATUS status;

    if (size < sizeof(*args64)) return ros_callback_bad_length();
    buffer_size = args64->size > 0 ? args64->size : 0;
    if (buffer_size > size - FIELD_OFFSET(ROS_DDEPOSTGET_CALLBACK_ARGUMENTS64, buffer)) return ros_callback_bad_length();
    len32 = sizeof(*args32) + buffer_size;
    if (!(args32 = Wow64AllocateTemp(len32))) return NtCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    memset(args32, 0, len32);
    args32->Type = args64->Type;
    msg_64to32(&args64->Msg, &args32->Msg);
    args32->size = args64->size;
    args32->pvData = PtrToUlong(args64->pvData);
    if (buffer_size) memcpy(args32->buffer, args64->buffer, buffer_size);
    status = Wow64KiUserCallbackDispatcher(id, args32, len32, &ret_ptr, &ret_len);
    if (status || ret_len < sizeof(*ret32)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    ret32 = ret_ptr;
    args64->Type = ret32->Type;
    msg_32to64(&args64->Msg, &ret32->Msg);
    args64->size = ret32->size;
    args64->pvData = ULongToPtr(ret32->pvData);
    if (buffer_size && ret_len >= sizeof(*ret32) + buffer_size) memcpy(args64->buffer, ret32->buffer, buffer_size);
    return NtCallbackReturn(args64, size, status);
}

static NTSTATUS WINAPI ros_wow64_dde_post(void *arg, ULONG size)
{
    return ros_wow64_dde_callback(ROS_USER_CALLBACK_DDEPOST, arg, size);
}

static NTSTATUS WINAPI ros_wow64_dde_get(void *arg, ULONG size)
{
    return ros_wow64_dde_callback(ROS_USER_CALLBACK_DDEGET, arg, size);
}

static NTSTATUS WINAPI ros_wow64_set_obm(void *arg, ULONG size)
{
    if (size != 93 * 4 * sizeof(INT)) return ros_callback_bad_length();
    return ros_callback_dispatch(ROS_USER_CALLBACK_SETOBM, arg, size);
}

static NTSTATUS WINAPI ros_wow64_lpk(void *arg, ULONG size)
{
    const ROS_LPK_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_LPK_CALLBACK_ARGUMENTS32 *args32;
    ULONG string_size;
    ULONG len32;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    BOOL result;
    NTSTATUS status;

    if (size < sizeof(*args64)) return ros_callback_bad_length();
    string_size = size - sizeof(*args64);
    len32 = sizeof(*args32) + string_size;
    if (!(args32 = Wow64AllocateTemp(len32))) return NtCallbackReturn(NULL, 0, STATUS_NO_MEMORY);
    args32->lpString = sizeof(*args32);
    args32->hdc = HandleToUlong(args64->hdc);
    args32->x = args64->x;
    args32->y = args64->y;
    args32->flags = args64->flags;
    args32->rect = args64->rect;
    args32->count = args64->count;
    args32->bRect = args64->bRect;
    if (string_size) memcpy(args32 + 1, args64 + 1, string_size);
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_LPK, args32, len32, &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(BOOL)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    result = *(BOOL *)ret_ptr;
    return NtCallbackReturn(&result, sizeof(result), status);
}

static NTSTATUS WINAPI ros_wow64_umpd(void *arg, ULONG size)
{
    UNREFERENCED_PARAMETER(arg);
    UNREFERENCED_PARAMETER(size);
    return NtCallbackReturn(NULL, 0, STATUS_NOT_SUPPORTED);
}

static NTSTATUS WINAPI ros_wow64_imm_process_key(void *arg, ULONG size)
{
    const ROS_IMMPROCESSKEY_CALLBACK_ARGUMENTS64 *args64 = arg;
    ROS_IMMPROCESSKEY_CALLBACK_ARGUMENTS32 args32;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    DWORD result;
    NTSTATUS status;

    if (size != sizeof(*args64)) return ros_callback_bad_length();
    args32.hWnd = HandleToUlong(args64->hWnd);
    args32.hKL = HandleToUlong(args64->hKL);
    args32.vKey = args64->vKey;
    args32.lParam = args64->lParam;
    args32.dwHotKeyID = args64->dwHotKeyID;
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_IMMPROCESSKEY, &args32, sizeof(args32), &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(result)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    result = *(DWORD *)ret_ptr;
    return NtCallbackReturn(&result, sizeof(result), status);
}

static NTSTATUS WINAPI ros_wow64_imm_load_layout(void *arg, ULONG size)
{
    const HKL *layout64 = arg;
    ULONG layout32;
    ROS_IMMLOADLAYOUT_CALLBACK_OUTPUT32 *ret32;
    ROS_IMMLOADLAYOUT_CALLBACK_OUTPUT64 result64;
    void *ret_ptr = NULL;
    ULONG ret_len = 0;
    NTSTATUS status;

    if (size != sizeof(*layout64)) return ros_callback_bad_length();
    layout32 = HandleToUlong(*layout64);
    status = Wow64KiUserCallbackDispatcher(ROS_USER_CALLBACK_IMMLOADLAYOUT, &layout32, sizeof(layout32), &ret_ptr, &ret_len);
    if (status || ret_len != sizeof(*ret32)) return NtCallbackReturn(NULL, 0, status ? status : STATUS_INFO_LENGTH_MISMATCH);
    ret32 = ret_ptr;
    memset(&result64, 0, sizeof(result64));
    result64.ret = ret32->ret;
    result64.iiex.hkl = LongToHandle(ret32->iiex.hkl);
    memcpy(&result64.iiex.ImeInfo, &ret32->iiex.ImeInfo, sizeof(result64.iiex) - FIELD_OFFSET(ROS_IMEINFOEX64, ImeInfo));
    return NtCallbackReturn(&result64, sizeof(result64), status);
}

ntuser_callback user_callbacks[ROS_USER_CALLBACK_COUNT] =
{
    ros_wow64_window_proc,
    ros_wow64_send_async_proc,
    ros_wow64_load_sysmenu_template,
    ros_wow64_load_default_cursors,
    ros_wow64_hook_proc,
    ros_wow64_event_proc,
    ros_wow64_load_menu,
    ros_wow64_client_thread_startup,
    ros_wow64_client_load_library,
    ros_wow64_get_charset_info,
    ros_wow64_copy_image,
    ros_wow64_set_wnd_icons,
    ros_wow64_deliver_user_apc,
    ros_wow64_dde_post,
    ros_wow64_dde_get,
    ros_wow64_set_obm,
    ros_wow64_lpk,
    ros_wow64_umpd,
    ros_wow64_imm_process_key,
    ros_wow64_imm_load_layout
};
