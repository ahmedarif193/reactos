/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User Interface Privilege Isolation between integrity levels
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define WM_SBX_BLOCKED (WM_USER + 1)
#define WM_SBX_WINDOW_ALLOWED (WM_USER + 2)
#define WM_SBX_PROCESS_ALLOWED (WM_USER + 3)
#define WM_SBX_WINDOW_DISALLOWED (WM_USER + 4)
#define WM_SBX_THREAD (WM_USER + 5)
#define WM_SBX_DONE (WM_USER + 6)

#define CHILD_BLOCKED_POSTED 0x01
#define CHILD_BLOCKED_WRONG_ERROR 0x02
#define CHILD_NULL_BLOCKED 0x04
#define CHILD_GETTEXT_BLOCKED 0x08
#define CHILD_WINDOW_ALLOW_BLOCKED 0x10
#define CHILD_PROCESS_ALLOW_BLOCKED 0x20
#define CHILD_WINDOW_DISALLOW_POSTED 0x40
#define CHILD_THREAD_POSTED 0x80
#define CHILD_NOTIFY_POSTED 0x100
#define CHILD_SEND_BLOCKED_SUCCEEDED 0x200
#define CHILD_NO_WINDOW 0x400
#define HELPER_CHILD_NO_WINDOW 0x04000000

static HWND gWindow;
static DWORD gReceived[8];
static BOOL gDone;

static BOOL
IsUipiPolicyEnabled(void)
{
    HKEY Key;
    DWORD EnableLua = 1, Size = sizeof(EnableLua), Type;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                      0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
    {
        return TRUE;
    }
    if (RegQueryValueExW(Key, L"EnableLUA", NULL, &Type,
                         (PBYTE)&EnableLua, &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD || Size != sizeof(EnableLua))
    {
        EnableLua = 1;
    }
    RegCloseKey(Key);
    return EnableLua != 0;
}

static HANDLE
CreateBrowserIntegrityToken(DWORD Rid)
{
    HANDLE Base = NULL, Token = NULL;
    PTOKEN_GROUPS Groups = NULL;
    PTOKEN_USER User = NULL;
    PSID_AND_ATTRIBUTES Restricting = NULL;
    DWORD Length = 0, Count = 0, Index;

    Base = SbxOpenToken(TOKEN_ALL_ACCESS);
    if (!Base)
        goto Cleanup;
    GetTokenInformation(Base, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Groups ||
        !GetTokenInformation(Base, TokenGroups, Groups, Length, &Length))
        goto Cleanup;
    GetTokenInformation(Base, TokenUser, NULL, 0, &Length);
    User = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!User ||
        !GetTokenInformation(Base, TokenUser, User, Length, &Length))
        goto Cleanup;
    Restricting = HeapAlloc(GetProcessHeap(), 0,
                            (Groups->GroupCount + 1) * sizeof(*Restricting));
    if (!Restricting)
        goto Cleanup;
    Restricting[Count].Sid = User->User.Sid;
    Restricting[Count++].Attributes = 0;
    for (Index = 0; Index < Groups->GroupCount; Index++)
    {
        if (Groups->Groups[Index].Attributes &
            (SE_GROUP_INTEGRITY | SE_GROUP_USE_FOR_DENY_ONLY))
            continue;
        Restricting[Count].Sid = Groups->Groups[Index].Sid;
        Restricting[Count++].Attributes = 0;
    }
    if (!CreateRestrictedToken(Base,
                               DISABLE_MAX_PRIVILEGE | SANDBOX_INERT,
                               0, NULL, 0, NULL,
                               Count, Restricting, &Token) ||
        !SbxSetTokenIntegrity(Token, Rid))
    {
        if (Token) CloseHandle(Token);
        Token = NULL;
    }

Cleanup:
    if (Restricting) HeapFree(GetProcessHeap(), 0, Restricting);
    if (User) HeapFree(GetProcessHeap(), 0, User);
    if (Groups) HeapFree(GetProcessHeap(), 0, Groups);
    if (Base) CloseHandle(Base);
    return Token;
}

static LRESULT CALLBACK
TestWindowProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg >= WM_SBX_BLOCKED && Msg <= WM_SBX_DONE)
    {
        gReceived[Msg - WM_SBX_BLOCKED]++;
        if (Msg == WM_SBX_DONE) gDone = TRUE;
        return 0;
    }
    return DefWindowProcW(hwnd, Msg, wParam, lParam);
}

static DWORD
RunChild(HWND Target, DWORD TargetThread, BOOL ExpectBlocked)
{
    DWORD Failures = 0;
    DWORD_PTR Result;
    BOOL Posted;

    if (!IsWindow(Target))
    {
        SbxChildFail(&Failures, CHILD_NO_WINDOW, "target window invalid", GetLastError());
        return Failures;
    }

    SetLastError(0);
    Posted = PostMessageW(Target, WM_SBX_BLOCKED, 0, 0);
    if (ExpectBlocked)
    {
        if (Posted) SbxChildFail(&Failures, CHILD_BLOCKED_POSTED, "blocked message posted", 0);
        else if (GetLastError() != ERROR_ACCESS_DENIED) SbxChildFail(&Failures, CHILD_BLOCKED_WRONG_ERROR, "blocked message error", GetLastError());
    }
    else if (!Posted)
        SbxChildFail(&Failures, CHILD_BLOCKED_POSTED, "same-level message not posted", GetLastError());

    if (!PostMessageW(Target, WM_NULL, 0, 0))
        SbxChildFail(&Failures, CHILD_NULL_BLOCKED, "WM_NULL blocked", GetLastError());

    SetLastError(0);
    if (!SendMessageTimeoutW(Target, WM_GETTEXTLENGTH, 0, 0, SMTO_NORMAL, 5000, &Result))
        SbxChildFail(&Failures, CHILD_GETTEXT_BLOCKED, "WM_GETTEXTLENGTH blocked", GetLastError());

    SetLastError(0);
    if (SendMessageTimeoutW(Target, WM_SBX_BLOCKED, 0, 0, SMTO_NORMAL, 5000, &Result) && ExpectBlocked)
        SbxChildFail(&Failures, CHILD_SEND_BLOCKED_SUCCEEDED, "SendMessageTimeout of blocked message succeeded", 0);

    if (!PostMessageW(Target, WM_SBX_WINDOW_ALLOWED, 0, 0))
        SbxChildFail(&Failures, CHILD_WINDOW_ALLOW_BLOCKED, "window-allowed message blocked", GetLastError());
    if (!PostMessageW(Target, WM_SBX_PROCESS_ALLOWED, 0, 0))
        SbxChildFail(&Failures, CHILD_PROCESS_ALLOW_BLOCKED, "process-allowed message blocked", GetLastError());

    Posted = PostMessageW(Target, WM_SBX_WINDOW_DISALLOWED, 0, 0);
    if (ExpectBlocked && Posted)
        SbxChildFail(&Failures, CHILD_WINDOW_DISALLOW_POSTED, "window-disallowed message posted", 0);
    if (!ExpectBlocked && !Posted)
        SbxChildFail(&Failures, CHILD_WINDOW_DISALLOW_POSTED, "same-level window-disallowed not posted", GetLastError());

    Posted = PostThreadMessageW(TargetThread, WM_SBX_THREAD, 0, 0);
    if (!Posted)
        SbxChildFail(&Failures, CHILD_THREAD_POSTED,
                     "PostThreadMessage failed", GetLastError());

    Posted = SendNotifyMessageW(Target, WM_SBX_BLOCKED, 0, 0);
    if (ExpectBlocked && Posted)
        SbxChildFail(&Failures, CHILD_NOTIFY_POSTED, "SendNotifyMessage of blocked message succeeded", 0);

    PostMessageW(Target, WM_SBX_DONE, 0, 0);
    return Failures;
}

static void
PumpUntilDone(HANDLE Process)
{
    MSG Msg;
    DWORD Start = GetTickCount();

    gDone = FALSE;
    while (!gDone && GetTickCount() - Start < SBX_CHILD_TIMEOUT_MS)
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            if (Msg.hwnd == NULL && Msg.message >= WM_SBX_BLOCKED && Msg.message <= WM_SBX_DONE)
                gReceived[Msg.message - WM_SBX_BLOCKED]++;
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        if (WaitForSingleObject(Process, 50) == WAIT_OBJECT_0)
        {
            while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
            {
                if (Msg.hwnd == NULL && Msg.message >= WM_SBX_BLOCKED && Msg.message <= WM_SBX_DONE)
                    gReceived[Msg.message - WM_SBX_BLOCKED]++;
                DispatchMessageW(&Msg);
            }
            break;
        }
    }
}

static void
RunScenario(HANDLE Token, BOOL ExpectBlocked, BOOL Filtered, DWORD ExpectedExit,
            const char *Tag)
{
    WCHAR Application[MAX_PATH], Extra[80], *FileName;
    PROCESS_INFORMATION Info;
    HANDLE ChildToken;
    DWORD ExitCode, ExpectedRid, UiAccess, Length;

    ZeroMemory(gReceived, sizeof(gReceived));
    if (!GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)))
    {
        ok(0, "%s: executable path failed %lu\n", Tag, GetLastError());
        return;
    }
    FileName = wcsrchr(Application, L'\\');
    if (!FileName || FAILED(StringCchCopyW(FileName + 1,
                                           ARRAYSIZE(Application) - (FileName + 1 - Application),
                                           L"sandbox_helper.exe")))
    {
        ok(0, "%s: helper path failed\n", Tag);
        return;
    }
    StringCchPrintfW(Extra, ARRAYSIZE(Extra), L"%Iu %lu %u %u",
                     (SIZE_T)(ULONG_PTR)gWindow, GetCurrentThreadId(),
                     ExpectBlocked, Filtered);
    if (!SbxSpawnChildExecutable(Application, NULL, "Uipi", "uipi", Extra,
                                 Token, NULL, CREATE_NO_WINDOW | CREATE_SUSPENDED, &Info))
    {
        ok(0, "%s: CreateProcess failed %lu\n", Tag, GetLastError());
        return;
    }
    if (Token)
    {
        ExpectedRid = SbxGetTokenIntegrity(Token);
        ChildToken = NULL;
        ok(OpenProcessToken(Info.hProcess, TOKEN_QUERY, &ChildToken),
           "%s: OpenProcessToken(child) failed %lu\n", Tag, GetLastError());
        if (ChildToken)
        {
            ok(SbxGetTokenIntegrity(ChildToken) == ExpectedRid,
               "%s: requested/actual integrity 0x%lx/0x%lx\n", Tag,
               ExpectedRid, SbxGetTokenIntegrity(ChildToken));
            UiAccess = 0xdeadbeef;
            Length = 0;
            ok(GetTokenInformation(ChildToken, TokenUIAccess, &UiAccess,
                                   sizeof(UiAccess), &Length) && !UiAccess,
               "%s: child TokenUIAccess %lu (%lu)\n", Tag, UiAccess,
               GetLastError());
            CloseHandle(ChildToken);
        }
    }
    ok(ResumeThread(Info.hThread) == 1,
       "%s: ResumeThread failed %lu\n", Tag, GetLastError());
    PumpUntilDone(Info.hProcess);
    ExitCode = SbxWaitChild(&Info);
    ok(ExitCode == ExpectedExit, "%s: child exit 0x%lx, expected 0x%lx\n",
       Tag, ExitCode, ExpectedExit);

    if (ExpectedExit != 0)
    {
        ok(gReceived[0] == 0 && gReceived[1] == 0 && gReceived[2] == 0 &&
           gReceived[3] == 0 && gReceived[4] == 0 && gReceived[5] == 0,
           "%s: a message escaped desktop initialization denial\n", Tag);
        return;
    }

    if (Filtered)
    {
        ok(gReceived[WM_SBX_WINDOW_ALLOWED - WM_SBX_BLOCKED] == 1, "%s: window-allowed received %lu\n", Tag, gReceived[1]);
        ok(gReceived[WM_SBX_PROCESS_ALLOWED - WM_SBX_BLOCKED] == 1, "%s: process-allowed received %lu\n", Tag, gReceived[2]);
        ok(gReceived[WM_SBX_DONE - WM_SBX_BLOCKED] == 1, "%s: done received %lu\n", Tag, gReceived[5]);
    }
    if (ExpectBlocked)
    {
        ok(gReceived[0] == 0, "%s: blocked message delivered %lu times\n", Tag, gReceived[0]);
        if (Filtered)
            ok(gReceived[WM_SBX_WINDOW_DISALLOWED - WM_SBX_BLOCKED] == 0, "%s: window-disallowed delivered\n", Tag);
    }
    else
    {
        ok(gReceived[0] >= 1, "%s: same-level message not delivered\n", Tag);
    }
    ok(gReceived[WM_SBX_THREAD - WM_SBX_BLOCKED] == 1,
       "%s: thread message delivery count %lu\n", Tag,
       gReceived[WM_SBX_THREAD - WM_SBX_BLOCKED]);
}

static void
SetupFilterApi(void)
{
    CHANGEFILTERSTRUCT Filter = { sizeof(Filter), 0 };

    ok(ChangeWindowMessageFilterEx(gWindow, WM_SBX_WINDOW_ALLOWED, MSGFLT_ALLOW, &Filter), "ChangeWindowMessageFilterEx(allow) failed %lu\n", GetLastError());
    ok(Filter.ExtStatus == MSGFLTINFO_NONE, "first allow ExtStatus %lu\n", Filter.ExtStatus);
    ok(ChangeWindowMessageFilterEx(gWindow, WM_SBX_WINDOW_ALLOWED, MSGFLT_ALLOW, &Filter), "second allow failed %lu\n", GetLastError());
    ok(Filter.ExtStatus == MSGFLTINFO_NONE, "second allow ExtStatus %lu\n", Filter.ExtStatus);
    ok(ChangeWindowMessageFilter(WM_SBX_PROCESS_ALLOWED, MSGFLT_ADD), "ChangeWindowMessageFilter(add) failed %lu\n", GetLastError());
    ok(ChangeWindowMessageFilterEx(gWindow, WM_SBX_PROCESS_ALLOWED, MSGFLT_ALLOW, &Filter), "allow over process filter failed %lu\n", GetLastError());
    ok(Filter.ExtStatus == MSGFLTINFO_NONE, "process-allowed ExtStatus %lu\n", Filter.ExtStatus);
    ok(ChangeWindowMessageFilterEx(gWindow, WM_SBX_WINDOW_DISALLOWED, MSGFLT_DISALLOW, &Filter), "disallow failed %lu\n", GetLastError());
}

static void
TestInvalidFilterApi(void)
{
    CHANGEFILTERSTRUCT Filter = { sizeof(Filter), 0 };

    SetLastError(0xdeadbeef);
    ok(!ChangeWindowMessageFilterEx(gWindow, WM_USER + 103, MSGFLT_RESET, &Filter) &&
       GetLastError() == ERROR_INVALID_PARAMETER,
       "native window reset behavior %lu\n", GetLastError());
    ok(!ChangeWindowMessageFilterEx(gWindow, WM_USER + 101, 7, NULL), "invalid action accepted\n");
    ok(ChangeWindowMessageFilter(WM_USER + 100, MSGFLT_RESET),
       "legacy MSGFLT_RESET failed %lu\n", GetLastError());
    Filter.cbSize = 1;
    ok(!ChangeWindowMessageFilterEx(gWindow, WM_USER + 102, MSGFLT_ALLOW, &Filter), "bad cbSize accepted\n");
}

START_TEST(Uipi)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);
    WNDCLASSW Class;
    HANDLE BaseToken, LowToken, UntrustedToken;
    DWORD ParentRid;
    BOOL UipiEnabled;

    if (Count >= 6 && !strcmp(Arguments[2], "child"))
    {
        HWND Target = (HWND)(ULONG_PTR)_strtoui64(Arguments[4], NULL, 10);
        DWORD Thread = strtoul(Arguments[5], NULL, 10);
        ExitProcess(RunChild(Target, Thread, !strcmp(Arguments[3], "low")));
    }

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = TestWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"SbxUipiWindow";
    ok(RegisterClassW(&Class) != 0, "RegisterClass failed %lu\n", GetLastError());
    gWindow = CreateWindowExW(0, L"SbxUipiWindow", L"sbx", WS_OVERLAPPED, 0, 0, 50, 50, NULL, NULL, Class.hInstance, NULL);
    ok(gWindow != NULL, "CreateWindow failed %lu\n", GetLastError());
    if (!gWindow) return;

    BaseToken = SbxOpenToken(TOKEN_QUERY);
    ParentRid = BaseToken ? SbxGetTokenIntegrity(BaseToken) : MAXDWORD;
    ok(BaseToken != NULL && ParentRid > SECURITY_MANDATORY_LOW_RID,
       "parent integrity 0x%lx (%lu)\n", ParentRid, GetLastError());
    if (BaseToken) CloseHandle(BaseToken);

    UipiEnabled = IsUipiPolicyEnabled();
    trace("EnableLUA/UIPI policy is %s\n", UipiEnabled ? "enabled" : "disabled");

    RunScenario(NULL, FALSE, FALSE, 0, "medium baseline child");

    LowToken = CreateBrowserIntegrityToken(SECURITY_MANDATORY_LOW_RID);
    ok(LowToken != NULL, "low token failed %lu\n", GetLastError());
    if (LowToken)
    {
        ok(IsTokenRestricted(LowToken), "low browser token is not restricted\n");
        RunScenario(LowToken, UipiEnabled, FALSE, 0, "low baseline child");
    }

    SetupFilterApi();
    ok(ChangeWindowMessageFilterEx(gWindow, WM_SBX_DONE, MSGFLT_ALLOW, NULL), "allow done failed %lu\n", GetLastError());
    if (LowToken)
    {
        RunScenario(LowToken, UipiEnabled, TRUE, 0, "low filtered child");
        CloseHandle(LowToken);
    }

    UntrustedToken = CreateBrowserIntegrityToken(SECURITY_MANDATORY_UNTRUSTED_RID);
    ok(UntrustedToken != NULL, "untrusted token failed %lu\n", GetLastError());
    if (UntrustedToken)
    {
        RunScenario(UntrustedToken, TRUE, FALSE,
                    HELPER_CHILD_NO_WINDOW | ERROR_DLL_INIT_FAILED,
                    "untrusted default-desktop child");
        CloseHandle(UntrustedToken);
    }

    TestInvalidFilterApi();

    DestroyWindow(gWindow);
    UnregisterClassW(L"SbxUipiWindow", Class.hInstance);
}
