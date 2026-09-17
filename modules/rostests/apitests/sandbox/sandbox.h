#ifndef _SANDBOX_APITEST_SANDBOX_H_
#define _SANDBOX_APITEST_SANDBOX_H_

#define SBX_CHILD_TIMEOUT_MS 30000
#define SBX_LABEL_LOW_SDDL L"S:(ML;;NW;;;LW)"
#define SBX_LABEL_LOW_NR_SDDL L"S:(ML;;NWNR;;;LW)"

/* Creation-policy word 2 values used by current Chromium.  The ReactOS SDK is
 * intentionally usable with older consumers, so keep local fallbacks in the
 * browser parity tests until the public header is brought up to date. */
#ifndef PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_INDIRECT_BRANCH_PREDICTION_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_INDIRECT_BRANCH_PREDICTION_ALWAYS_ON (0x00000001ULL << 16)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_ALWAYS_OFF
#define PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_ALWAYS_OFF (0x00000002ULL << 28)
#define PROCESS_CREATION_MITIGATION_POLICY2_CET_USER_SHADOW_STACKS_STRICT_MODE (0x00000003ULL << 28)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY2_CET_DYNAMIC_APIS_OUT_OF_PROC_ONLY_ALWAYS_OFF
#define PROCESS_CREATION_MITIGATION_POLICY2_CET_DYNAMIC_APIS_OUT_OF_PROC_ONLY_ALWAYS_OFF (0x00000002ULL << 48)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_CORE_SHARING_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY2_RESTRICT_CORE_SHARING_ALWAYS_ON (0x00000001ULL << 52)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY2_FSCTL_SYSTEM_CALL_DISABLE_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY2_FSCTL_SYSTEM_CALL_DISABLE_ALWAYS_ON (0x00000001ULL << 56)
#endif

#define SBX_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY ((PROCESS_MITIGATION_POLICY)14)
#define SBX_PROCESS_USER_SHADOW_STACK_POLICY      ((PROCESS_MITIGATION_POLICY)15)
#define SBX_PROCESS_SEHOP_POLICY                  ((PROCESS_MITIGATION_POLICY)18)

typedef NTSTATUS (NTAPI *PFN_NtCreateLowBoxToken)(PHANDLE, HANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PSID, ULONG, PSID_AND_ATTRIBUTES, ULONG, HANDLE*);

static inline PSID
SbxCreateUniqueSid(VOID)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    LARGE_INTEGER Counter;
    PSID Sid = NULL;

    QueryPerformanceCounter(&Counter);
    AllocateAndInitializeSid(&NtAuthority, 4, 111,
                             GetCurrentProcessId() ^ Counter.LowPart,
                             GetTickCount() ^ Counter.HighPart,
                             GetCurrentThreadId() * 2654435761u + Counter.LowPart,
                             0, 0, 0, 0, &Sid);
    return Sid;
}

static inline BOOL
SbxAddDefaultDaclSid(HANDLE Token, PSID Sid, DWORD Access)
{
    PTOKEN_DEFAULT_DACL Old = NULL;
    TOKEN_DEFAULT_DACL New;
    EXPLICIT_ACCESSW Entry;
    PACL Acl = NULL;
    DWORD Length = 0;
    BOOL Success = FALSE;

    GetTokenInformation(Token, TokenDefaultDacl, NULL, 0, &Length);
    if (Length)
    {
        Old = HeapAlloc(GetProcessHeap(), 0, Length);
        if (Old && !GetTokenInformation(Token, TokenDefaultDacl, Old, Length, &Length))
            Old->DefaultDacl = NULL;
    }
    ZeroMemory(&Entry, sizeof(Entry));
    Entry.grfAccessPermissions = Access;
    Entry.grfAccessMode = GRANT_ACCESS;
    Entry.grfInheritance = NO_INHERITANCE;
    Entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    Entry.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    Entry.Trustee.ptstrName = (LPWSTR)Sid;
    if (SetEntriesInAclW(1, &Entry, Old ? Old->DefaultDacl : NULL, &Acl) == ERROR_SUCCESS)
    {
        New.DefaultDacl = Acl;
        Success = SetTokenInformation(Token, TokenDefaultDacl, &New, sizeof(New));
        LocalFree(Acl);
    }
    if (Old) HeapFree(GetProcessHeap(), 0, Old);
    return Success;
}

static inline BOOL
SbxIsChild(char **Arguments, int Count, const char *Mode)
{
    return Count >= 4 && !strcmp(Arguments[2], "child") && !strcmp(Arguments[3], Mode);
}

static inline HANDLE
SbxOpenToken(DWORD Access)
{
    HANDLE Token = NULL;
    OpenProcessToken(GetCurrentProcess(), Access, &Token);
    return Token;
}

static inline PSID
SbxLabelSid(DWORD Rid)
{
    SID_IDENTIFIER_AUTHORITY Authority = {SECURITY_MANDATORY_LABEL_AUTHORITY};
    PSID Sid = NULL;
    AllocateAndInitializeSid(&Authority, 1, Rid, 0, 0, 0, 0, 0, 0, 0, &Sid);
    return Sid;
}

static inline BOOL
SbxSetTokenIntegrity(HANDLE Token, DWORD Rid)
{
    TOKEN_MANDATORY_LABEL Label;
    BOOL Result;

    Label.Label.Sid = SbxLabelSid(Rid);
    Label.Label.Attributes = SE_GROUP_INTEGRITY;
    if (!Label.Label.Sid) return FALSE;
    Result = SetTokenInformation(Token, TokenIntegrityLevel, &Label, sizeof(Label));
    FreeSid(Label.Label.Sid);
    return Result;
}

static inline DWORD
SbxGetTokenIntegrity(HANDLE Token)
{
    UCHAR Buffer[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    DWORD Length;
    PTOKEN_MANDATORY_LABEL Label = (PTOKEN_MANDATORY_LABEL)Buffer;

    if (!GetTokenInformation(Token, TokenIntegrityLevel, Buffer, sizeof(Buffer), &Length))
        return 0xFFFFFFFF;
    return *GetSidSubAuthority(Label->Label.Sid, 0);
}

static inline HANDLE
SbxCreateIntegrityToken(DWORD Rid, TOKEN_TYPE Type)
{
    HANDLE Base, Token = NULL;

    Base = SbxOpenToken(TOKEN_ALL_ACCESS);
    if (!Base) return NULL;
    if (!DuplicateTokenEx(Base, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, Type, &Token))
        Token = NULL;
    CloseHandle(Base);
    if (Token) AdjustTokenPrivileges(Token, TRUE, NULL, 0, NULL, NULL);
    if (Token && !SbxSetTokenIntegrity(Token, Rid))
    {
        CloseHandle(Token);
        Token = NULL;
    }
    return Token;
}

static inline PSECURITY_DESCRIPTOR
SbxSdFromSddl(PCWSTR Sddl)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, &Sd, NULL))
        return NULL;
    return Sd;
}

static inline DWORD
SbxSetLabel(HANDLE Handle, SE_OBJECT_TYPE Type, DWORD Rid, DWORD Policy)
{
    UCHAR AclBuffer[sizeof(ACL) + sizeof(SYSTEM_MANDATORY_LABEL_ACE) + SECURITY_MAX_SID_SIZE];
    PACL Sacl = (PACL)AclBuffer;
    PSID Sid = SbxLabelSid(Rid);
    DWORD Error;

    if (!Sid) return ERROR_NOT_ENOUGH_MEMORY;
    InitializeAcl(Sacl, sizeof(AclBuffer), ACL_REVISION);
    if (!AddMandatoryAce(Sacl, ACL_REVISION, 0, Policy, Sid))
    {
        Error = GetLastError();
        FreeSid(Sid);
        return Error;
    }
    Error = SetSecurityInfo(Handle, Type, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, Sacl);
    FreeSid(Sid);
    return Error;
}

static inline DWORD
SbxQueryLabelRid(HANDLE Handle, SE_OBJECT_TYPE Type, PDWORD Policy)
{
    PSECURITY_DESCRIPTOR Sd = NULL;
    PACL Sacl = NULL;
    DWORD Rid = 0xFFFFFFFF;
    DWORD Index;

    if (Policy) *Policy = 0;
    if (GetSecurityInfo(Handle, Type, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, &Sacl, &Sd) != ERROR_SUCCESS)
        return Rid;
    if (Sacl)
    {
        for (Index = 0; Index < Sacl->AceCount; Index++)
        {
            PSYSTEM_MANDATORY_LABEL_ACE Ace;
            if (!GetAce(Sacl, Index, (PVOID*)&Ace)) break;
            if (Ace->Header.AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;
            Rid = *GetSidSubAuthority(&Ace->SidStart, 0);
            if (Policy) *Policy = Ace->Mask;
            break;
        }
    }
    LocalFree(Sd);
    return Rid;
}

static inline BOOL
SbxSpawnChildExecutable(PCWSTR Application, PCWSTR CurrentDirectory, PCSTR Test, PCSTR Mode, PCWSTR Extra, HANDLE Token,
                        LPPROC_THREAD_ATTRIBUTE_LIST Attributes, DWORD Flags, PPROCESS_INFORMATION Info)
{
    WCHAR CommandLine[MAX_PATH * 2];
    STARTUPINFOEXW Startup;
    BOOL Result;

    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" %S child %S %s",
                     Application, Test, Mode, Extra ? Extra : L"");
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.StartupInfo.cb = Attributes ? sizeof(Startup) : sizeof(Startup.StartupInfo);
    Startup.lpAttributeList = Attributes;
    ZeroMemory(Info, sizeof(*Info));
    Flags |= CREATE_UNICODE_ENVIRONMENT;
    if (Attributes) Flags |= EXTENDED_STARTUPINFO_PRESENT;
    if (Token)
        Result = CreateProcessAsUserW(Token, Application, CommandLine, NULL, NULL, FALSE, Flags, NULL, CurrentDirectory, &Startup.StartupInfo, Info);
    else
        Result = CreateProcessW(Application, CommandLine, NULL, NULL, FALSE, Flags, NULL, CurrentDirectory, &Startup.StartupInfo, Info);
    return Result;
}

static inline BOOL
SbxSpawnChild(PCSTR Test, PCSTR Mode, PCWSTR Extra, HANDLE Token, LPPROC_THREAD_ATTRIBUTE_LIST Attributes, DWORD Flags, PPROCESS_INFORMATION Info)
{
    WCHAR Application[MAX_PATH];

    if (!GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)))
        return FALSE;
    return SbxSpawnChildExecutable(Application, NULL, Test, Mode, Extra, Token, Attributes, Flags, Info);
}

static inline DWORD
SbxWaitChild(PPROCESS_INFORMATION Info)
{
    DWORD ExitCode = 0xFFFFFFFF;

    if (WaitForSingleObject(Info->hProcess, SBX_CHILD_TIMEOUT_MS) != WAIT_OBJECT_0)
    {
        TerminateProcess(Info->hProcess, 0xDEAD);
        WaitForSingleObject(Info->hProcess, 5000);
        ExitCode = 0xDEAD;
    }
    else
    {
        GetExitCodeProcess(Info->hProcess, &ExitCode);
    }
    CloseHandle(Info->hThread);
    CloseHandle(Info->hProcess);
    return ExitCode;
}

static inline void
SbxChildFail(DWORD *Failures, DWORD Bit, const char *What, DWORD Error)
{
    char Text[256];
    HANDLE Stream;
    DWORD Written;

    *Failures |= Bit;
    StringCchPrintfA(Text, ARRAYSIZE(Text), "sandbox child: bit 0x%08lx: %s (error %lu)\r\n",
                     Bit, What, Error);
    OutputDebugStringA(Text);
    Stream = GetStdHandle(STD_ERROR_HANDLE);
    if (Stream && Stream != INVALID_HANDLE_VALUE)
    {
        WriteFile(Stream, Text, (DWORD)strlen(Text), &Written, NULL);
        FlushFileBuffers(Stream);
    }
}

#endif
