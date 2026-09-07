#include "precomp.h"
#include <reactos/wow64shared.h>

#ifdef _M_IX86
typedef NTSTATUS (NTAPI *GET_LOCALE_MAPPING)(PVOID *, PLCID, PLARGE_INTEGER);
static DWORD WorkerTlsIndex;
static HANDLE WorkerStartEvent;

static __declspec(noinline) ULONG ProbeStack(ULONG Seed)
{
    volatile UCHAR Buffer[32768];
    ULONG Index, Sum = 0;

    for (Index = 0; Index < sizeof(Buffer); Index += PAGE_SIZE)
        Buffer[Index] = (UCHAR)(Seed + Index / PAGE_SIZE);
    for (Index = 0; Index < sizeof(Buffer); Index += PAGE_SIZE)
        Sum += Buffer[Index];
    return Sum;
}

static DWORD WINAPI StackWorker(PVOID Parameter)
{
    ULONG Seed = (ULONG)(ULONG_PTR)Parameter;
    ULONG_PTR OldLimit = (ULONG_PTR)NtCurrentTeb()->NtTib.StackLimit;

    if (WaitForSingleObject(WorkerStartEvent, 10000) != WAIT_OBJECT_0) return 5;
    if (!TlsSetValue(WorkerTlsIndex, Parameter)) return 1;
    if (ProbeStack(Seed) != Seed * 8 + 28) return 2;
    if ((ULONG_PTR)NtCurrentTeb()->NtTib.StackLimit >= OldLimit) return 3;
    if (TlsGetValue(WorkerTlsIndex) != Parameter) return 4;
    return 0;
}

static VOID TestThreadedStackGrowth(VOID)
{
    HANDLE Threads[16];
    DWORD Count = 0, Index, Result, ExitCode;

    WorkerTlsIndex = TlsAlloc();
    ok(WorkerTlsIndex != TLS_OUT_OF_INDEXES, "TLS allocation failed\n");
    if (WorkerTlsIndex == TLS_OUT_OF_INDEXES) return;
    WorkerStartEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(WorkerStartEvent != NULL, "Worker start event creation failed\n");
    if (!WorkerStartEvent)
    {
        TlsFree(WorkerTlsIndex);
        return;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Threads); ++Index)
    {
        HANDLE Thread = CreateThread(NULL, 128 * 1024, StackWorker, ULongToPtr(Index + 1),
                                     STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        ok(Thread != NULL, "Worker %lu creation failed: %lu\n", Index, GetLastError());
        if (Thread) Threads[Count++] = Thread;
    }
    ok(SetEvent(WorkerStartEvent), "Cannot release workers\n");
    if (Count)
    {
        Result = WaitForMultipleObjects(Count, Threads, TRUE, 10000);
        ok_hex(Result, WAIT_OBJECT_0);
        for (Index = 0; Index < Count; ++Index)
        {
            ExitCode = STILL_ACTIVE;
            ok(GetExitCodeThread(Threads[Index], &ExitCode), "Cannot query worker %lu\n", Index);
            ok_hex(ExitCode, 0);
            CloseHandle(Threads[Index]);
        }
        if (Result != WAIT_OBJECT_0) return;
    }
    ok(TlsFree(WorkerTlsIndex), "TLS release failed\n");
    CloseHandle(WorkerStartEvent);
}

static VOID TestSelfModifyingCode(VOID)
{
    static const UCHAR Template[] = {0xb8, 1, 0, 0, 0, 0xc3};
    volatile UCHAR *Code = VirtualAlloc(NULL, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    ULONG (__cdecl *Function)(VOID) = (PVOID)Code;
    ULONG Index;

    ok(Code != NULL, "Executable allocation failed: %lu\n", GetLastError());
    if (!Code) return;
    memcpy((PVOID)Code, Template, sizeof(Template));
    ok(FlushInstructionCache(GetCurrentProcess(), (PVOID)Code, sizeof(Template)), "Initial cache flush failed\n");
    for (Index = 1; Index <= 32; ++Index)
    {
        Code[1] = (UCHAR)Index;
        ok_hex(Function(), Index);
    }
    ok(VirtualFree((PVOID)Code, 0, MEM_RELEASE), "Executable allocation release failed\n");
}

static VOID WaitForNativeInspection(VOID)
{
    char **Arguments;
    HANDLE Ready, Continue;

    if (winetest_get_mainargs(&Arguments) != 4) return;
    Ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, Arguments[2]);
    Continue = OpenEventA(SYNCHRONIZE, FALSE, Arguments[3]);
    ok(Ready && Continue, "Cannot open native inspection events: %lu\n", GetLastError());
    if (Ready && Continue)
    {
        ok(SetEvent(Ready), "Cannot signal native inspector\n");
        ok_hex(WaitForSingleObject(Continue, 30000), WAIT_OBJECT_0);
    }
    if (Continue) CloseHandle(Continue);
    if (Ready) CloseHandle(Ready);
}

static VOID TestDeadFlagMemoryProbe(VOID)
{
    PVOID Page = VirtualAlloc(NULL, PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
    volatile NTSTATUS Exception = STATUS_SUCCESS;

    ok(Page != NULL, "Guard-page allocation failed: %lu\n", GetLastError());
    if (!Page) return;
    _SEH2_TRY
    {
        __asm__ __volatile__("testl %0, (%0)\n\txorl %%eax, %%eax"
                             : : "r"(Page) : "eax", "cc", "memory");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Exception = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_hex(Exception, STATUS_GUARD_PAGE_VIOLATION);
    ok(VirtualFree(Page, 0, MEM_RELEASE), "Guard-page release failed\n");
}

static VOID TestSectionUnmap(VOID)
{
    HANDLE Section;
    LARGE_INTEGER Size;
    SIZE_T ViewSize = 0;
    PVOID BaseAddress = NULL;
    NTSTATUS Status;

    Size.QuadPart = PAGE_SIZE;
    Status = NtCreateSection(&Section, SECTION_ALL_ACCESS, NULL, &Size,
                             PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = NtMapViewOfSection(Section, NtCurrentProcess(), &BaseAddress,
                                0, 0, NULL, &ViewSize, ViewUnmap, 0, PAGE_READWRITE);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtUnmapViewOfSection(NtCurrentProcess(), BaseAddress);
        ok_hex(Status, STATUS_SUCCESS);
    }
    Status = NtClose(Section);
    ok_hex(Status, STATUS_SUCCESS);
}

#elif defined(_WIN64)
static VOID TestNativeTlsBitmap(VOID)
{
    char **Arguments;
    char ReadyName[80], ContinueName[80], CommandLine[MAX_PATH + 256];
    STARTUPINFOA Startup = {sizeof(Startup)};
    PROCESS_INFORMATION Process;
    PROCESS_BASIC_INFORMATION Info;
    HANDLE Ready, Continue, WaitHandles[2];
    SIZE_T BytesRead = 0;
    ULONG Bitmap = 0;
    ULONG ReservedMask = (1u << WOW64_TLS_MAX_NUMBER) - 1;
    DWORD Result, ExitCode;
    BOOL Created;
    NTSTATUS Status;

    if (winetest_get_mainargs(&Arguments) != 3)
    {
        skip("Pass the path to the x86 ntdll_apitest executable\n");
        return;
    }
    StringCbPrintfA(ReadyName, sizeof(ReadyName), "Local\\wow64_tls_%lu_ready", GetCurrentProcessId());
    StringCbPrintfA(ContinueName, sizeof(ContinueName), "Local\\wow64_tls_%lu_continue", GetCurrentProcessId());
    Ready = CreateEventA(NULL, TRUE, FALSE, ReadyName);
    Continue = CreateEventA(NULL, TRUE, FALSE, ContinueName);
    ok(Ready && Continue, "Cannot create native inspection events\n");
    if (!Ready || !Continue) goto Done;
    if (FAILED(StringCbPrintfA(CommandLine, sizeof(CommandLine), "\"%s\" wow64_startup %s %s",
                               Arguments[2], ReadyName, ContinueName)))
    {
        ok(0, "Child command line is too long\n");
        goto Done;
    }
    Created = CreateProcessA(Arguments[2], CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                             NULL, NULL, &Startup, &Process);
    ok(Created, "Cannot start x86 test: %lu\n", GetLastError());
    if (!Created) goto Done;
    WaitHandles[0] = Ready;
    WaitHandles[1] = Process.hProcess;
    Result = WaitForMultipleObjects(2, WaitHandles, FALSE, 30000);
    ok_hex(Result, WAIT_OBJECT_0);
    if (Result == WAIT_OBJECT_0)
    {
        Status = NtQueryInformationProcess(Process.hProcess, ProcessBasicInformation, &Info, sizeof(Info), NULL);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = NtReadVirtualMemory(Process.hProcess, (PUCHAR)Info.PebBaseAddress + FIELD_OFFSET(PEB, TlsBitmapBits),
                                         &Bitmap, sizeof(Bitmap), &BytesRead);
            ok_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok(BytesRead == sizeof(Bitmap), "Incomplete native bitmap read\n");
                ok((Bitmap & ReservedMask) == ReservedMask, "Native reserved TLS bitmap: %08lx\n", Bitmap);
            }
        }
    }
    SetEvent(Continue);
    Result = WaitForSingleObject(Process.hProcess, 30000);
    ok_hex(Result, WAIT_OBJECT_0);
    if (Result == WAIT_OBJECT_0)
    {
        ExitCode = STILL_ACTIVE;
        ok(GetExitCodeProcess(Process.hProcess, &ExitCode), "Cannot query child exit\n");
        ok_hex(ExitCode, 0);
    }
    else TerminateProcess(Process.hProcess, 1);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
Done:
    if (Continue) CloseHandle(Continue);
    if (Ready) CloseHandle(Ready);
}
#endif

START_TEST(wow64_startup)
{
#ifdef _M_IX86
    GET_LOCALE_MAPPING GetLocaleMapping;
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PVOID Mapping = NULL, CachedMapping = NULL;
    LCID Locale = 0, CachedLocale = 0;
    LARGE_INTEGER CasingSize = {{0}};
    MEMORY_BASIC_INFORMATION MemoryInfo;
    NTSTATUS Status;

    if (!NtCurrentTeb()->WOW32Reserved)
    {
        skip("Requires an x86 process under WoW64\n");
        return;
    }
    WaitForNativeInspection();
    TestSectionUnmap();

    GetLocaleMapping = (GET_LOCALE_MAPPING)GetProcAddress(Ntdll, "RtlGetLocaleFileMappingAddress");
    ok(GetLocaleMapping != NULL, "Missing locale mapping API\n");
    if (!GetLocaleMapping) return;
    Status = GetLocaleMapping(&Mapping, &Locale, &CasingSize);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(Mapping != NULL, "Locale mapping is NULL\n");
    Status = NtQueryVirtualMemory(NtCurrentProcess(), Mapping, MemoryBasicInformation,
                                 &MemoryInfo, sizeof(MemoryInfo), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) ok_hex(MemoryInfo.State, MEM_COMMIT);
    Status = GetLocaleMapping(&CachedMapping, &CachedLocale, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    ok_ptr(CachedMapping, Mapping);
    ok_hex(CachedLocale, Locale);
    TestDeadFlagMemoryProbe();
    TestThreadedStackGrowth();
    TestSelfModifyingCode();
#elif defined(_WIN64)
    TestNativeTlsBitmap();
#else
    skip("Requires an x86 build\n");
#endif
}
