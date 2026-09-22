/* ReactOS-specific USER syscall compatibility definitions. */

#ifndef __WOW64WIN_REACTOS_USER_H
#define __WOW64WIN_REACTOS_USER_H

#define ROS_WOW64_FNID_NUM 31

W32KAPI BOOL WINAPI NtUserGetImeInfoEx(void *info, UINT search_type);
W32KAPI BOOL WINAPI NtUserSetImeInfoEx(const void *info);

typedef struct
{
    ULONG Length;
    ULONG MaximumLength;
    ULONG Buffer;
} ROS_LARGE_STRING32;

typedef struct
{
    ULONG Length;
    ULONG MaximumLength;
    PVOID Buffer;
} ROS_LARGE_STRING64;

typedef HWND (WINAPI *ROS_NTUSER_CREATE_WINDOW_EX)(DWORD, PVOID, PVOID, PVOID, DWORD, INT, INT, INT, INT, HWND, HMENU, HINSTANCE, PVOID, DWORD, PVOID);
typedef NTSTATUS (WINAPI *ROS_NTUSER_BUILD_HWND_LIST)(HDESK, HWND, BOOLEAN, BOOLEAN, ULONG, ULONG, HWND *, ULONG *);

typedef struct
{
    ULONG maxMsgs;
    ULONG abMsgs;
} ROS_WNDMSG32;

typedef struct
{
    ULONG maxMsgs;
    ULONG Reserved;
    ULONG64 abMsgs;
} ROS_WNDMSG64;

typedef struct
{
    ULONG psi;
    ULONG aheList;
    ULONG pDispInfo;
    ULONG ulSharedDelta;
    ROS_WNDMSG32 awmControl[ROS_WOW64_FNID_NUM];
    ROS_WNDMSG32 DefWindowMsgs;
    ROS_WNDMSG32 DefWindowSpecMsgs;
} ROS_SHAREDINFO32;

typedef struct
{
    ULONG64 psi;
    ULONG64 aheList;
    ULONG64 pDispInfo;
    ULONG64 ulSharedDelta;
    ROS_WNDMSG64 awmControl[ROS_WOW64_FNID_NUM];
    ROS_WNDMSG64 DefWindowMsgs;
    ROS_WNDMSG64 DefWindowSpecMsgs;
} ROS_SHAREDINFO64;

typedef struct
{
    ULONG ulVersion;
    ULONG ulCurrentVersion;
    ULONG dwDispatchCount;
    ROS_SHAREDINFO32 siClient;
} ROS_USERCONNECT32;

typedef struct
{
    ULONG ulVersion;
    ULONG ulCurrentVersion;
    ULONG dwDispatchCount;
    ULONG Reserved;
    ROS_SHAREDINFO64 siClient;
} ROS_USERCONNECT64;

#define ROS_PFNCLIENT_COUNT 23
#define ROS_PFNCLIENTWORKER_COUNT 11
#define ROS_SERVERPROC_COUNT 7

typedef struct
{
    ULONG Functions[ROS_PFNCLIENT_COUNT];
} ROS_PFNCLIENT32;

typedef struct
{
    ULONG_PTR Functions[ROS_PFNCLIENT_COUNT];
} ROS_PFNCLIENT64;

typedef struct
{
    ULONG Functions[ROS_PFNCLIENTWORKER_COUNT];
} ROS_PFNCLIENTWORKER32;

typedef struct
{
    ULONG_PTR Functions[ROS_PFNCLIENTWORKER_COUNT];
} ROS_PFNCLIENTWORKER64;

typedef struct
{
    DWORD dwSRVIFlags;
    ULONG_PTR cHandleEntries;
    ULONG_PTR mpFnidPfn[ROS_WOW64_FNID_NUM];
    ULONG_PTR aStoCidPfn[ROS_SERVERPROC_COUNT];
    USHORT mpFnid_serverCBWndProc[ROS_WOW64_FNID_NUM];
    ROS_PFNCLIENT64 apfnClientA;
    ROS_PFNCLIENT64 apfnClientW;
    ROS_PFNCLIENTWORKER64 apfnClientWorker;
} ROS_SERVERINFO64_PFN;

typedef struct
{
    ULONG pszClientAnsiMenuName;
    ULONG pwszClientUnicodeMenuName;
    ULONG pusMenuName;
} ROS_CLSMENUNAME32;

typedef struct
{
    ULONG64 pszClientAnsiMenuName;
    ULONG64 pwszClientUnicodeMenuName;
    ULONG64 pusMenuName;
} ROS_CLSMENUNAME64;

typedef struct
{
    ULONG lpName;
    ULONG lpModName;
    USHORT rt;
    USHORT dummy;
    ULONG flags;
    SHORT xHotspot;
    SHORT yHotspot;
    ULONG hbmMask;
    ULONG hbmColor;
    ULONG hbmAlpha;
    RECT rcBounds;
    ULONG hbmUserAlpha;
    ULONG bpp;
    ULONG cx;
    ULONG cy;
    UINT cpcur;
    UINT cicur;
    ULONG aspcur;
    ULONG aicur;
    ULONG ajifRate;
    UINT iicur;
} ROS_CURSORDATA32;

typedef struct
{
    ULONG64 lpName;
    ULONG64 lpModName;
    USHORT rt;
    USHORT dummy;
    ULONG flags;
    SHORT xHotspot;
    SHORT yHotspot;
    ULONG64 hbmMask;
    ULONG64 hbmColor;
    ULONG64 hbmAlpha;
    RECT rcBounds;
    ULONG64 hbmUserAlpha;
    ULONG bpp;
    ULONG cx;
    ULONG cy;
    UINT cpcur;
    UINT cicur;
    ULONG64 aspcur;
    ULONG64 aicur;
    ULONG64 ajifRate;
    UINT iicur;
} ROS_CURSORDATA64;

C_ASSERT(sizeof(ROS_WNDMSG32) == 0x08);
C_ASSERT(sizeof(ROS_WNDMSG64) == 0x10);
C_ASSERT(sizeof(ROS_LARGE_STRING32) == 0x0c);
C_ASSERT(sizeof(ROS_LARGE_STRING64) == 0x10);
C_ASSERT(FIELD_OFFSET(ROS_LARGE_STRING32, Buffer) == 0x08);
C_ASSERT(FIELD_OFFSET(ROS_LARGE_STRING64, Buffer) == 0x08);
C_ASSERT(sizeof(ROS_SHAREDINFO32) == 0x118);
C_ASSERT(sizeof(ROS_SHAREDINFO64) == 0x230);
C_ASSERT(sizeof(ROS_USERCONNECT32) == 0x124);
C_ASSERT(sizeof(ROS_USERCONNECT64) == 0x240);
C_ASSERT(sizeof(ROS_PFNCLIENT32) == 0x5c);
C_ASSERT(sizeof(ROS_PFNCLIENT64) == 0xb8);
C_ASSERT(sizeof(ROS_PFNCLIENTWORKER32) == 0x2c);
C_ASSERT(sizeof(ROS_PFNCLIENTWORKER64) == 0x58);
C_ASSERT(FIELD_OFFSET(ROS_SERVERINFO64_PFN, cHandleEntries) == 0x08);
C_ASSERT(FIELD_OFFSET(ROS_SERVERINFO64_PFN, apfnClientA) == 0x180);
C_ASSERT(FIELD_OFFSET(ROS_SERVERINFO64_PFN, apfnClientW) == 0x238);
C_ASSERT(FIELD_OFFSET(ROS_SERVERINFO64_PFN, apfnClientWorker) == 0x2f0);
C_ASSERT(sizeof(ROS_SERVERINFO64_PFN) == 0x348);
C_ASSERT(sizeof(ROS_CLSMENUNAME32) == 0x0c);
C_ASSERT(sizeof(ROS_CLSMENUNAME64) == 0x18);
C_ASSERT(sizeof(ROS_CURSORDATA32) == 0x58);
C_ASSERT(sizeof(ROS_CURSORDATA64) == 0x88);
C_ASSERT(FIELD_OFFSET(ROS_CURSORDATA32, hbmMask) == 0x14);
C_ASSERT(FIELD_OFFSET(ROS_CURSORDATA64, hbmMask) == 0x20);
C_ASSERT(FIELD_OFFSET(ROS_CURSORDATA32, aspcur) == 0x48);
C_ASSERT(FIELD_OFFSET(ROS_CURSORDATA64, aspcur) == 0x68);

W32KAPI NTSTATUS WINAPI NtUserProcessConnect(HANDLE ProcessHandle, PVOID UserConnect, ULONG Size);
W32KAPI ULONG_PTR WINAPI NtUserGetCPD(HWND hwnd, UINT flags, ULONG_PTR proc);
W32KAPI HBRUSH WINAPI NtUserGetControlBrush(HWND hwnd, HDC hdc, UINT ctlMsg);
W32KAPI HBRUSH WINAPI NtUserGetControlColor(HWND hwndParent, HWND hwnd, HDC hdc, UINT ctlMsg);
W32KAPI BOOL WINAPI NtUserDefSetText(HWND hwnd, ROS_LARGE_STRING64 *text);
W32KAPI BOOL WINAPI NtUserGetClassInfo(HINSTANCE instance, UNICODE_STRING *name, WNDCLASSEXW *wc, WCHAR **menu_name, BOOL ansi);
W32KAPI HWND WINAPI NtUserCallHwndOpt(HWND hwnd, DWORD routine);
W32KAPI DWORD WINAPI NtUserCallHwndParamLock(HWND hwnd, DWORD_PTR param, DWORD routine);
W32KAPI HDWP WINAPI NtUserDeferWindowPos(HDWP hdwp, HWND hwnd, HWND after, INT x, INT y, INT cx, INT cy, UINT flags);
W32KAPI BOOL WINAPI NtUserDrawCaption(HWND hwnd, HDC hdc, const RECT *rect, UINT flags);
W32KAPI BOOL WINAPI NtUserDrawAnimatedRects(HWND hwnd, INT id, RECT *from, RECT *to);
W32KAPI DWORD WINAPI NtUserGetListBoxInfo(HWND hwnd);
W32KAPI UINT WINAPI NtUserGetMenuIndex(HMENU menu, HMENU submenu);
W32KAPI DWORD WINAPI NtUserMinMaximize(HWND hwnd, UINT cmd, BOOL hide);
W32KAPI DWORD WINAPI NtUserSetThreadState(DWORD set, DWORD flags);
W32KAPI DWORD WINAPI NtUserGetGuiResources(HANDLE process, DWORD flags);
W32KAPI BOOL WINAPI NtUserUpdatePerUserSystemParameters(DWORD reserved, BOOL enable);
W32KAPI BOOL WINAPI NtUserSetSystemCursor(HCURSOR cursor, DWORD id);
W32KAPI BOOL WINAPI NtUserPaintDesktop(HDC hdc);
W32KAPI BOOL WINAPI NtUserValidateTimerCallback(LPARAM lparam);
W32KAPI BOOL WINAPI NtUserSetImeOwnerWindow(HWND ime, HWND focus);
W32KAPI BOOL WINAPI NtUserLockWorkStation(void);
W32KAPI BOOL WINAPI NtUserBlockInput(BOOL block);
W32KAPI BOOL WINAPI NtUserUnloadKeyboardLayout(HKL hkl);
W32KAPI BOOL WINAPI NtUserGetComboBoxInfo(HWND hwnd, COMBOBOXINFO *info);
W32KAPI BOOL WINAPI NtUserSBGetParms(HWND hwnd, INT bar, void *data, SCROLLINFO *info);
W32KAPI BOOL WINAPI NtUserSetScrollBarInfo(HWND hwnd, LONG id, void *info);
W32KAPI HHOOK WINAPI NtUserSetWindowsHookAW(INT id, HOOKPROC proc, BOOL ansi);
W32KAPI HANDLE WINAPI NtUserConvertMemHandle(void *data, DWORD size);
W32KAPI NTSTATUS WINAPI NtUserCreateLocalMemHandle(HANDLE mem, void *data, DWORD size, DWORD *ret_size);
W32KAPI BOOL WINAPI NtUserGetAltTabInfo(HWND hwnd, INT item, void *info, WCHAR *text, UINT count, BOOL ansi);
W32KAPI BOOL WINAPI NtUserGetImeHotKey(DWORD id, UINT *modifiers, UINT *key, HKL *hkl);
W32KAPI BOOL WINAPI NtUserSetImeHotKey(DWORD id, UINT modifiers, UINT key, HKL hkl, DWORD action);
W32KAPI HKL WINAPI NtUserLoadKeyboardLayoutEx(HANDLE file, DWORD offset, void *tables, HKL old, UNICODE_STRING *klid,
                                              DWORD hkl, UINT flags);

typedef INT (WINAPI *ROS_NTUSER_ENUM_DISPLAY_MONITORS)(HDC, const RECT *, HMONITOR *, RECT *, DWORD);
typedef LONG (WINAPI *ROS_NTUSER_CHANGE_DISPLAY_SETTINGS)(UNICODE_STRING *, DEVMODEW *, DWORD, void *);

static inline PVOID
ros_large_str_32to64(ROS_LARGE_STRING64 *str, const ROS_LARGE_STRING32 *str32)
{
    if ((ULONG_PTR)str32 <= 0xffff) return (PVOID)str32;
    str->Length = str32->Length;
    str->MaximumLength = str32->MaximumLength;
    str->Buffer = ULongToPtr(str32->Buffer);
    return str;
}

#endif /* __WOW64WIN_REACTOS_USER_H */
