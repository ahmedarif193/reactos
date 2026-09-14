#include "precomp.h"
#include <reactos/wow64shared.h>

#ifdef _M_IX86
#define EXPLICIT_64BIT
#include <ndk/peb_teb.h>
#undef EXPLICIT_64BIT

typedef NTSTATUS (NTAPI *GET_LOCALE_MAPPING)(PVOID *, PLCID, PLARGE_INTEGER);
static DWORD WorkerTlsIndex;
static HANDLE WorkerStartEvent;

static DWORD WINAPI SelectorWorker(PVOID Parameter)
{
    return 0;
}

static VOID TestThreadSelectors(VOID)
{
    typedef BOOL (WINAPI *QUERY_MACHINES)(HANDLE, PUSHORT, PUSHORT);
    QUERY_MACHINES QueryMachines;
    USHORT ProcessMachine, NativeMachine;
    CONTEXT Context = {0};
    HANDLE Thread;
    BOOL Success;
    ULONG Index, Selectors[3];

    /* GetNativeSystemInfo can report the emulated architecture on Windows ARM64. */
    QueryMachines = (QUERY_MACHINES)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    if (!QueryMachines)
    {
        skip("IsWow64Process2 unavailable\n");
        return;
    }
    Success = QueryMachines(GetCurrentProcess(), &ProcessMachine, &NativeMachine);
    ok(Success, "Machine query failed: %lu\n", GetLastError());
    if (!Success) return;
    if (NativeMachine != IMAGE_FILE_MACHINE_ARM64)
    {
        skip("ARM64-specific WoW64 selector layout\n");
        return;
    }

    Thread = CreateThread(NULL, 0, SelectorWorker, NULL, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "Suspended thread creation failed: %lu\n", GetLastError());
    if (!Thread) return;

    Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_SEGMENTS;
    Success = GetThreadContext(Thread, &Context);
    ok(Success, "Context query failed: %lu\n", GetLastError());
    if (Success)
    {
        ok_hex(Context.SegCs, 0x1b);
        ok_hex(Context.SegSs, 0x23);
        ok_hex(Context.SegFs, 0x3b);
        ok_hex(Context.SegDs, 0x23);
        ok_hex(Context.SegEs, 0x23);
        ok_hex(Context.SegGs, 0x23);
        Selectors[0] = Context.SegCs;
        Selectors[1] = Context.SegSs;
        Selectors[2] = Context.SegFs;
        for (Index = 0; Index < RTL_NUMBER_OF(Selectors); ++Index)
        {
            LDT_ENTRY Entry;
            Success = GetThreadSelectorEntry(Thread, Selectors[Index], &Entry);
            ok(Success, "Selector %lx query failed: %lu\n", Selectors[Index], GetLastError());
            if (Success) ok_hex(Entry.HighWord.Bits.Type, Index ? 0x13 : 0x1b);
        }
    }

    ok(ResumeThread(Thread) == 1, "Cannot resume selector worker: %lu\n", GetLastError());
    ok_hex(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
    CloseHandle(Thread);
}

static VOID TestNativePointerFields(VOID)
{
    TEB64 *NativeTeb = UlongToPtr(NtCurrentTeb()->GdiBatchCount);
    PEB64 *NativePeb;
    PVOID ImageBase = NtCurrentPeb()->ImageBaseAddress;
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(ImageBase);
    PVOID StartAddress = NULL;
    NTSTATUS Status;

    Status = NtQueryInformationThread(NtCurrentThread(), ThreadQuerySetWin32StartAddress,
                                     &StartAddress, sizeof(StartAddress), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status) && Headers)
        ok_ptr(StartAddress, (PUCHAR)ImageBase + Headers->OptionalHeader.AddressOfEntryPoint);

    ok(NativeTeb != NULL, "Missing native TEB backlink\n");
    if (!NativeTeb) return;
    ok(NativeTeb->NtTib.ExceptionList == PtrToUlong(NtCurrentTeb()), "Native TIB backlink is %I64x\n", NativeTeb->NtTib.ExceptionList);
    ok(NativeTeb->ProcessEnvironmentBlock && NativeTeb->ProcessEnvironmentBlock <= MAXULONG,
       "Native PEB is not x86-addressable: %I64x\n", NativeTeb->ProcessEnvironmentBlock);
    if (!NativeTeb->ProcessEnvironmentBlock || NativeTeb->ProcessEnvironmentBlock > MAXULONG) return;
    NativePeb = UlongToPtr((ULONG)NativeTeb->ProcessEnvironmentBlock);
    ok(NativePeb->AnsiCodePageData == PtrToUlong(NtCurrentPeb()->AnsiCodePageData), "Different native/x86 ANSI table\n");
    ok(NativePeb->OemCodePageData == PtrToUlong(NtCurrentPeb()->OemCodePageData), "Different native/x86 OEM table\n");
    ok(NativePeb->UnicodeCaseTableData == PtrToUlong(NtCurrentPeb()->UnicodeCaseTableData), "Different native/x86 case table\n");
}

static VOID TestDebugPrint(VOID)
{
    volatile NTSTATUS Exception = STATUS_SUCCESS;
    ULONG Status = STATUS_UNSUCCESSFUL;

    _SEH2_TRY
    {
        Status = DbgPrint("FEXTEST_DEBUG_PRINT x86 bridge %lu\n", 1ul);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Exception = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_hex(Exception, STATUS_SUCCESS);
    ok_hex(Status, STATUS_SUCCESS);

    Exception = STATUS_SUCCESS;
    _SEH2_TRY
    {
        __debugbreak();
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Exception = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_hex(Exception, STATUS_BREAKPOINT);
}

static VOID TestNativeProcessorInformation(VOID)
{
    static const char *Names[] = {"RtlGetNativeSystemInformation", "NtWow64GetNativeSystemInformation"};
    typedef NTSTATUS (NTAPI *QUERY_NATIVE)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    typedef BOOL (WINAPI *QUERY_MACHINES)(HANDLE, USHORT *, USHORT *);
    QUERY_MACHINES QueryMachines;
    SYSTEM_PROCESSOR_INFORMATION Info;
    ULONG LegacyInfo[4];
    USHORT ProcessMachine, NativeMachine, ExpectedArchitecture;
    ULONG Index, Length;
    NTSTATUS Status;

    QueryMachines = (QUERY_MACHINES)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2");
    if (!QueryMachines)
    {
        skip("IsWow64Process2 is unavailable\n");
        return;
    }
    if (!QueryMachines(GetCurrentProcess(), &ProcessMachine, &NativeMachine))
    {
        ok(FALSE, "IsWow64Process2 failed: %lu\n", GetLastError());
        return;
    }
    ok_hex(ProcessMachine, IMAGE_FILE_MACHINE_I386);
    switch (NativeMachine)
    {
        case IMAGE_FILE_MACHINE_AMD64: ExpectedArchitecture = PROCESSOR_ARCHITECTURE_AMD64; break;
        case IMAGE_FILE_MACHINE_ARM64: ExpectedArchitecture = PROCESSOR_ARCHITECTURE_ARM64; break;
        default:
            ok(FALSE, "Unexpected native WoW64 machine %x\n", NativeMachine);
            return;
    }
    Status = NtQuerySystemInformation(SystemProcessorInformation, &Info, sizeof(Info), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) ok_hex(Info.ProcessorArchitecture, PROCESSOR_ARCHITECTURE_INTEL);

    for (Index = 0; Index < RTL_NUMBER_OF(Names); ++Index)
    {
        QUERY_NATIVE Query = (QUERY_NATIVE)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), Names[Index]);
        ok(Query != NULL, "Missing %s\n", Names[Index]);
        if (!Query) continue;
        memset(&Info, 0xcc, sizeof(Info));
        Length = 0xdeadbeef;
        Status = Query(SystemProcessorInformation, &Info, sizeof(Info), &Length);
        ok_hex(Status, STATUS_SUCCESS);
        ok(Length == sizeof(Info), "%s returned length %lu\n", Names[Index], Length);
        if (NT_SUCCESS(Status)) ok_hex(Info.ProcessorArchitecture, ExpectedArchitecture);
        Status = Query(SystemProcessorInformation, &Info, sizeof(Info), NULL);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ok_hex(Info.ProcessorArchitecture, ExpectedArchitecture);
        Status = Query(SystemProcessorInformation, &Info, sizeof(Info) - 1, &Length);
        ok_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
        memset(LegacyInfo, 0xcc, sizeof(LegacyInfo));
        Length = 0xdeadbeef;
        Status = Query(SystemProcessorInformation, LegacyInfo, 12, &Length);
        ok_hex(Status, STATUS_SUCCESS);
        ok_hex(Length, 12);
        if (NT_SUCCESS(Status)) ok_hex((USHORT)LegacyInfo[0], ExpectedArchitecture);
        ok_hex(LegacyInfo[3], 0xcccccccc);
    }
}

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
            ok((ULONG_PTR)Info.PebBaseAddress <= MAXULONG, "Native WoW64 PEB is not x86-addressable: %p\n", Info.PebBaseAddress);
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
    TestNativePointerFields();
    TestSectionUnmap();
    TestNativeProcessorInformation();
    TestDebugPrint();

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
    TestThreadSelectors();
    TestThreadedStackGrowth();
    TestSelfModifyingCode();
#elif defined(_WIN64)
    TestNativeTlsBitmap();
#else
    skip("Requires an x86 build\n");
#endif
}
