/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Boundary descriptors and private namespaces
 */

#include "precomp.h"

#define CHILD_NAMESPACE_OPENED 0x1
#define CHILD_NAMESPACE_DENIED 0x2
#define CHILD_OBJECT_MISSING 0x4
#define CHILD_GLOBAL_LEAK 0x8
#define CHILD_BOUNDARY_FAILED 0x10
#define CHILD_ISOLATED_CREATE_FAILED 0x20
#define CHILD_ISOLATION_MISSING 0x40
#define CHILD_ISOLATION_PREFIX 0x80

#define BOUNDARY_NAME L"SbxPrivateBoundary"
#define LOW_BOUNDARY_NAME L"SbxPrivateBoundaryLow"
#define ALIAS_NAME L"SbxAlias"
#define OPEN_ALIAS_NAME L"SbxAliasOpen"
#define OBJECT_NAME L"SbxAlias\\sbx_ns_event"
#define RAW_OBJECT_NAME L"sbx_ns_event"

static HANDLE
MakeBoundary(PCWSTR Name, PSID Sid, PSID Label)
{
    HANDLE Boundary = CreateBoundaryDescriptorW(Name, 0);

    if (!Boundary)
        return NULL;
    if (Sid && !AddSIDToBoundaryDescriptor(&Boundary, Sid))
    {
        DeleteBoundaryDescriptor(Boundary);
        return NULL;
    }
    if (Label && !AddIntegrityLabelToBoundaryDescriptor(&Boundary, Label))
    {
        DeleteBoundaryDescriptor(Boundary);
        return NULL;
    }
    return Boundary;
}

static DWORD
RunChild(BOOL ExpectAccess)
{
    DWORD Failures = 0;
    HANDLE Boundary, Namespace, Event;
    PSID Label = SbxLabelSid(SECURITY_MANDATORY_LOW_RID);

    Boundary = MakeBoundary(ExpectAccess ? LOW_BOUNDARY_NAME : BOUNDARY_NAME, NULL, ExpectAccess ? NULL : Label);
    if (!Boundary)
    {
        SbxChildFail(&Failures, CHILD_BOUNDARY_FAILED, "CreateBoundaryDescriptor", GetLastError());
        if (Label) FreeSid(Label);
        return Failures;
    }

    Namespace = OpenPrivateNamespaceW(Boundary, ALIAS_NAME);
    if (!ExpectAccess)
    {
        if (Namespace)
        {
            SbxChildFail(&Failures, CHILD_NAMESPACE_OPENED, "namespace opened below its integrity boundary", 0);
            ClosePrivateNamespace(Namespace, 0);
        }
    }
    else if (!Namespace)
    {
        SbxChildFail(&Failures, CHILD_NAMESPACE_DENIED, "OpenPrivateNamespace", GetLastError());
    }
    else
    {
        Event = OpenEventW(SYNCHRONIZE, FALSE, OBJECT_NAME);
        if (!Event)
            SbxChildFail(&Failures, CHILD_OBJECT_MISSING, "namespace object not reachable through the alias", GetLastError());
        else
            CloseHandle(Event);
        Event = OpenEventW(EVENT_MODIFY_STATE, FALSE, OBJECT_NAME);
        if (Event)
        {
            SbxChildFail(&Failures, CHILD_GLOBAL_LEAK, "low integrity child modified a medium namespace object", 0);
            CloseHandle(Event);
        }
        Event = OpenEventW(SYNCHRONIZE, FALSE, RAW_OBJECT_NAME);
        if (Event)
        {
            SbxChildFail(&Failures, CHILD_GLOBAL_LEAK, "namespace object visible in the global namespace", 0);
            CloseHandle(Event);
        }
        ClosePrivateNamespace(Namespace, 0);
    }

    DeleteBoundaryDescriptor(Boundary);
    if (Label) FreeSid(Label);
    return Failures;
}

static void
TestBoundaryDescriptors(void)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    HANDLE Boundary;
    PSID World = NULL, Label;

    SetLastError(0xdeadbeef);
    ok(CreateBoundaryDescriptorW(NULL, 0) == NULL && GetLastError() == 0xdeadbeef,
       "CreateBoundaryDescriptor(NULL) %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(CreateBoundaryDescriptorW(BOUNDARY_NAME, 0x80) == NULL && GetLastError() == 0xdeadbeef,
       "CreateBoundaryDescriptor(bad flags) %lu\n", GetLastError());

    Boundary = CreateBoundaryDescriptorW(BOUNDARY_NAME, 0);
    ok(Boundary != NULL, "CreateBoundaryDescriptor failed %lu\n", GetLastError());
    if (!Boundary) return;

    ok(AllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &World),
       "world SID allocation failed %lu\n", GetLastError());
    ok(AddSIDToBoundaryDescriptor(&Boundary, World), "AddSIDToBoundaryDescriptor failed %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!AddSIDToBoundaryDescriptor(&Boundary, NULL), "AddSIDToBoundaryDescriptor(NULL) succeeded\n");

    Label = SbxLabelSid(SECURITY_MANDATORY_LOW_RID);
    ok(Label != NULL, "label SID allocation failed\n");
    ok(AddIntegrityLabelToBoundaryDescriptor(&Boundary, Label), "AddIntegrityLabelToBoundaryDescriptor failed %lu\n", GetLastError());
    ok(!AddIntegrityLabelToBoundaryDescriptor(&Boundary, Label), "second integrity label accepted\n");

    SetLastError(0xdeadbeef);
    ok(!AddIntegrityLabelToBoundaryDescriptor(&Boundary, World), "non-label SID accepted as an integrity label\n");

    DeleteBoundaryDescriptor(Boundary);
    if (World) FreeSid(World);
    if (Label) FreeSid(Label);
}

static void
TestNamespaceLifetime(void)
{
    HANDLE Boundary, LookupBoundary, Namespace, Second, Opened, Event, Raw;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    PSID Random = NULL, World = NULL;
    DWORD Error;

    ok(AllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID,
                                0, 0, 0, 0, 0, 0, 0, &World),
       "world SID allocation failed %lu\n", GetLastError());
    if (!World) return;
    Boundary = MakeBoundary(BOUNDARY_NAME, World, NULL);
    ok(Boundary != NULL, "boundary creation failed %lu\n", GetLastError());
    if (!Boundary) { FreeSid(World); return; }

    SetLastError(0xdeadbeef);
    ok(OpenPrivateNamespaceW(Boundary, ALIAS_NAME) == NULL && GetLastError() == ERROR_PATH_NOT_FOUND,
       "opening a namespace that does not exist: %lu\n", GetLastError());

    Namespace = CreatePrivateNamespaceW(NULL, Boundary, ALIAS_NAME);
    ok(Namespace != NULL, "CreatePrivateNamespace failed %lu\n", GetLastError());
    if (!Namespace) { DeleteBoundaryDescriptor(Boundary); FreeSid(World); return; }

    LookupBoundary = MakeBoundary(BOUNDARY_NAME, World, NULL);
    ok(LookupBoundary != NULL, "duplicate boundary creation failed %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Second = LookupBoundary ? CreatePrivateNamespaceW(NULL, LookupBoundary, ALIAS_NAME) : NULL;
    Error = GetLastError();
    ok(Second == NULL && Error == ERROR_ALREADY_EXISTS, "duplicate namespace: %p %lu\n", Second, Error);
    if (Second) ClosePrivateNamespace(Second, 0);
    if (LookupBoundary) DeleteBoundaryDescriptor(LookupBoundary);

    LookupBoundary = MakeBoundary(BOUNDARY_NAME, World, NULL);
    ok(LookupBoundary != NULL, "open boundary creation failed %lu\n", GetLastError());
    Opened = LookupBoundary ? OpenPrivateNamespaceW(LookupBoundary, OPEN_ALIAS_NAME) : NULL;
    ok(Opened != NULL, "OpenPrivateNamespace failed %lu\n", GetLastError());
    if (Opened) ClosePrivateNamespace(Opened, 0);
    if (LookupBoundary) DeleteBoundaryDescriptor(LookupBoundary);

    Event = CreateEventW(NULL, TRUE, FALSE, OBJECT_NAME);
    ok(Event != NULL, "creating an object through the alias failed %lu\n", GetLastError());
    Raw = OpenEventW(EVENT_MODIFY_STATE, FALSE, RAW_OBJECT_NAME);
    ok(Raw == NULL, "namespace object is visible in the global namespace\n");
    if (Raw) CloseHandle(Raw);
    Raw = OpenEventW(EVENT_MODIFY_STATE, FALSE, OBJECT_NAME);
    ok(Raw != NULL, "reopening through the alias failed %lu\n", GetLastError());
    if (Raw) CloseHandle(Raw);
    if (Event) CloseHandle(Event);

    ok(ClosePrivateNamespace(Namespace, PRIVATE_NAMESPACE_FLAG_DESTROY), "ClosePrivateNamespace failed %lu\n", GetLastError());

    LookupBoundary = MakeBoundary(BOUNDARY_NAME, World, NULL);
    ok(LookupBoundary != NULL, "post-destroy boundary creation failed %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Opened = LookupBoundary ? OpenPrivateNamespaceW(LookupBoundary, OPEN_ALIAS_NAME) : NULL;
    ok(Opened == NULL && GetLastError() == ERROR_PATH_NOT_FOUND, "namespace still present after destroy: %p %lu\n", Opened, GetLastError());
    if (Opened) ClosePrivateNamespace(Opened, 0);
    if (LookupBoundary) DeleteBoundaryDescriptor(LookupBoundary);

    DeleteBoundaryDescriptor(Boundary);

    ok(AllocateAndInitializeSid(&NtAuthority, 4, 111, 0x77777777, 0x66666666, 0x55555555, 0, 0, 0, 0, &Random),
       "random SID allocation failed %lu\n", GetLastError());
    Boundary = MakeBoundary(BOUNDARY_NAME, Random, NULL);
    ok(Boundary != NULL, "boundary with a foreign SID failed %lu\n", GetLastError());
    if (Boundary)
    {
        SetLastError(0xdeadbeef);
        Namespace = CreatePrivateNamespaceW(NULL, Boundary, ALIAS_NAME);
        Error = GetLastError();
        ok(Namespace == NULL && Error == ERROR_ACCESS_DENIED,
           "namespace created outside its SID boundary: %p %lu\n", Namespace, Error);
        if (Namespace) ClosePrivateNamespace(Namespace, PRIVATE_NAMESPACE_FLAG_DESTROY);
        DeleteBoundaryDescriptor(Boundary);
    }
    if (Random) FreeSid(Random);
    FreeSid(World);
}

static void
TestBoundaryEnforcement(BOOL LowBoundary)
{
    HANDLE Boundary, Namespace = NULL, Event = NULL, Token = NULL;
    PSID Label = SbxLabelSid(SECURITY_MANDATORY_MEDIUM_RID);
    PROCESS_INFORMATION Info;
    DWORD ExitCode;

    Boundary = MakeBoundary(LowBoundary ? LOW_BOUNDARY_NAME : BOUNDARY_NAME, NULL, LowBoundary ? NULL : Label);
    ok(Boundary != NULL, "boundary creation failed %lu\n", GetLastError());
    if (!Boundary) { if (Label) FreeSid(Label); return; }

    Namespace = CreatePrivateNamespaceW(NULL, Boundary, ALIAS_NAME);
    ok(Namespace != NULL, "CreatePrivateNamespace failed %lu\n", GetLastError());
    if (!Namespace) goto Cleanup;

    Event = CreateEventW(NULL, TRUE, FALSE, OBJECT_NAME);
    ok(Event != NULL, "namespace object creation failed %lu\n", GetLastError());

    Token = SbxCreateIntegrityToken(SECURITY_MANDATORY_LOW_RID, TokenPrimary);
    ok(Token != NULL, "low integrity token failed %lu\n", GetLastError());
    if (!Token) goto Cleanup;

    ok(SbxSpawnChild("PrivateNamespace", LowBoundary ? "low-allowed" : "low-denied", NULL, Token, NULL, CREATE_NO_WINDOW, &Info),
       "child spawn failed %lu\n", GetLastError());
    if (Info.hProcess)
    {
        ExitCode = SbxWaitChild(&Info);
        ok(ExitCode == 0, "%s child failed with 0x%lx\n", LowBoundary ? "low-boundary" : "medium-boundary", ExitCode);
    }

Cleanup:
    if (Token) CloseHandle(Token);
    if (Event) CloseHandle(Event);
    if (Namespace) ClosePrivateNamespace(Namespace, PRIVATE_NAMESPACE_FLAG_DESTROY);
    DeleteBoundaryDescriptor(Boundary);
    if (Label) FreeSid(Label);
}

#define ISOLATION_PREFIX L"SbxIso"
#define ISOLATED_EVENT L"sbx_bno_event"

typedef struct _SBX_PROC_THREAD_BNOISOLATION_ATTRIBUTE
{
    BOOL IsolationEnabled;
    WCHAR IsolationPrefix[0x88];
} SBX_PROC_THREAD_BNOISOLATION_ATTRIBUTE;

#define SBX_PROC_THREAD_ATTRIBUTE_BNO_ISOLATION \
    ProcThreadAttributeValue(19, FALSE, TRUE, FALSE)

static BOOL
QueryObjectPath(HANDLE Handle, LPWSTR Buffer, ULONG Length)
{
    UCHAR NameBuffer[1024];
    POBJECT_NAME_INFORMATION Name = (POBJECT_NAME_INFORMATION)NameBuffer;
    ULONG Returned;

    if (!NT_SUCCESS(NtQueryObject(Handle, ObjectNameInformation, NameBuffer, sizeof(NameBuffer), &Returned)))
        return FALSE;
    if (Name->Name.Length / sizeof(WCHAR) >= Length)
        return FALSE;
    RtlCopyMemory(Buffer, Name->Name.Buffer, Name->Name.Length);
    Buffer[Name->Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
    return TRUE;
}

static DWORD
RunIsolatedChild(void)
{
    DWORD Failures = 0;
    UCHAR Buffer[sizeof(TOKEN_BNO_ISOLATION_INFORMATION) + 256 * sizeof(WCHAR)];
    PTOKEN_BNO_ISOLATION_INFORMATION Isolation = (PTOKEN_BNO_ISOLATION_INFORMATION)Buffer;
    HANDLE Token = SbxOpenToken(TOKEN_QUERY);
    HANDLE Event;
    DWORD Length;

    if (!Token || !GetTokenInformation(Token, TokenBnoIsolation, Buffer, sizeof(Buffer), &Length) ||
        !Isolation->IsolationEnabled)
    {
        SbxChildFail(&Failures, CHILD_ISOLATION_MISSING, "TokenBnoIsolation", GetLastError());
    }
    else if (wcscmp(Isolation->IsolationPrefix, ISOLATION_PREFIX) != 0)
    {
        SbxChildFail(&Failures, CHILD_ISOLATION_PREFIX, "isolation prefix mismatch", 0);
    }
    if (Token) CloseHandle(Token);

    Event = CreateEventW(NULL, TRUE, FALSE, ISOLATED_EVENT);
    if (!Event)
        SbxChildFail(&Failures, CHILD_ISOLATED_CREATE_FAILED, "CreateEvent under isolation", GetLastError());
    else
    {
        SetEvent(Event);
        Sleep(1500);
        CloseHandle(Event);
    }
    return Failures;
}

static BOOL
SpawnBnoIsolatedChild(HANDLE Token, PPROCESS_INFORMATION Info)
{
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH * 2];
    SBX_PROC_THREAD_BNOISOLATION_ATTRIBUTE Bno;
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    STARTUPINFOEXW Startup;
    SIZE_T Size = 0;
    BOOL Success;

    ZeroMemory(Info, sizeof(*Info));
    if (!GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)))
        return FALSE;
    if (FAILED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                                L"\"%s\" PrivateNamespace child isolated", Application)))
        return FALSE;

    ZeroMemory(&Bno, sizeof(Bno));
    Bno.IsolationEnabled = TRUE;
    if (FAILED(StringCchCopyW(Bno.IsolationPrefix,
                              ARRAYSIZE(Bno.IsolationPrefix), ISOLATION_PREFIX)))
        return FALSE;

    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes ||
        !InitializeProcThreadAttributeList(Attributes, 1, 0, &Size))
    {
        if (Attributes) HeapFree(GetProcessHeap(), 0, Attributes);
        return FALSE;
    }
    Success = UpdateProcThreadAttribute(Attributes, 0,
                                        SBX_PROC_THREAD_ATTRIBUTE_BNO_ISOLATION,
                                        &Bno, sizeof(Bno), NULL, NULL);
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.StartupInfo.cb = sizeof(Startup);
    Startup.lpAttributeList = Attributes;
    if (Success)
        Success = CreateProcessAsUserW(Token, Application, CommandLine,
                                       NULL, NULL, FALSE,
                                       CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                                       EXTENDED_STARTUPINFO_PRESENT,
                                       NULL, NULL, &Startup.StartupInfo, Info);
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
    if (!Success)
        return FALSE;

    if (ResumeThread(Info->hThread) == (DWORD)-1)
    {
        TerminateProcess(Info->hProcess, GetLastError());
        CloseHandle(Info->hThread);
        CloseHandle(Info->hProcess);
        ZeroMemory(Info, sizeof(*Info));
        return FALSE;
    }
    return TRUE;
}

static void
TestBnoIsolation(void)
{
    TOKEN_BNO_ISOLATION_INFORMATION Isolation;
    HANDLE Base = NULL, Token = NULL, Marker = NULL, Named = NULL;
    WCHAR Path[512], Root[384], *Last;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    PROCESS_INFORMATION Info;
    NTSTATUS Status;
    BOOL Spawned;
    DWORD ExitCode, Attempt;

    Base = SbxOpenToken(TOKEN_QUERY | TOKEN_DUPLICATE);
    ok(Base != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Base) return;
    ok(DuplicateTokenEx(Base, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &Token),
       "DuplicateTokenEx failed %lu\n", GetLastError());
    if (!Token) { CloseHandle(Base); return; }

    Isolation.IsolationPrefix = ISOLATION_PREFIX;
    Isolation.IsolationEnabled = TRUE;
    SetLastError(0xdeadbeef);
    ok(!SetTokenInformation(Token, TokenBnoIsolation, &Isolation, sizeof(Isolation)) &&
       GetLastError() == ERROR_INVALID_PARAMETER,
       "SetTokenInformation(TokenBnoIsolation) returned %lu\n", GetLastError());

    Marker = CreateEventW(NULL, TRUE, FALSE, L"sbx_bno_marker");
    ok(Marker != NULL, "marker event failed %lu\n", GetLastError());
    if (!Marker || !QueryObjectPath(Marker, Root, ARRAYSIZE(Root)))
    {
        skip("cannot resolve the named object root\n");
        goto Cleanup;
    }
    Last = wcsrchr(Root, L'\\');
    if (Last) *Last = UNICODE_NULL;

    Spawned = SpawnBnoIsolatedChild(Token, &Info);
    ok(Spawned, "isolated child spawn failed %lu\n", GetLastError());
    if (!Spawned) goto Cleanup;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s\\%s", Root, ISOLATION_PREFIX, ISOLATED_EVENT);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = STATUS_OBJECT_NAME_NOT_FOUND;
    for (Attempt = 0; Attempt < 40 && !NT_SUCCESS(Status); Attempt++)
    {
        Status = NtOpenEvent(&Named, EVENT_ALL_ACCESS, &Attributes);
        if (!NT_SUCCESS(Status)) Sleep(100);
    }
    ok(NT_SUCCESS(Status), "isolated child object not found at %S (0x%lx)\n", Path, Status);
    if (Named) { CloseHandle(Named); Named = NULL; }

    Named = OpenEventW(EVENT_ALL_ACCESS, FALSE, ISOLATED_EVENT);
    ok(Named == NULL, "isolated object is visible in the caller's namespace\n");
    if (Named) CloseHandle(Named);

    ExitCode = SbxWaitChild(&Info);
    ok(ExitCode == 0, "isolated child failed with 0x%lx\n", ExitCode);

Cleanup:
    if (Marker) CloseHandle(Marker);
    if (Token) CloseHandle(Token);
    if (Base) CloseHandle(Base);
}

START_TEST(PrivateNamespace)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    if (SbxIsChild(Arguments, Count, "low-denied")) ExitProcess(RunChild(FALSE));
    if (SbxIsChild(Arguments, Count, "low-allowed")) ExitProcess(RunChild(TRUE));
    if (SbxIsChild(Arguments, Count, "isolated")) ExitProcess(RunIsolatedChild());

    TestBoundaryDescriptors();
    TestNamespaceLifetime();
    TestBoundaryEnforcement(TRUE);
    TestBoundaryEnforcement(FALSE);
    TestBnoIsolation();
}
