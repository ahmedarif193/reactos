/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
/* Regression tests run as normally loaded processes on the native port. */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ndk/ntndk.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <intrin.h>
#include <dbghelp.h>

ULONG RunSehTests(VOID);
ULONG RunSmpTests(VOID);
int RunCxxTests(VOID);
static ULONG Checks, Failures;
#define CHECK(e) do { ++Checks; if (!(e)) { ++Failures; \
    DbgPrint("RISCVTEST: FAIL line=%lu error=%lu\n", (ULONG)__LINE__, GetLastError()); } } while (0)

static LONG WINAPI UnexpectedHandler(PEXCEPTION_POINTERS Info)
{
    DbgPrint("RISCVTEST: FAIL fast-fail invoked an application handler\n");
    ExitProcess(77);
    return EXCEPTION_CONTINUE_SEARCH;
}

static VOID FastFailChild(VOID)
{
    AddVectoredExceptionHandler(1, UnexpectedHandler);
    __try { __fastfail(FAST_FAIL_INVALID_ARG); }
    __except(EXCEPTION_EXECUTE_HANDLER) { ExitProcess(78); }
}

static VOID CheckChild(PCSTR Mode, DWORD ExpectedExit)
{
    CHAR Path[MAX_PATH], Command[MAX_PATH + 64];
    STARTUPINFOA Startup = {sizeof(Startup)};
    PROCESS_INFORMATION Process;
    DWORD Code = STILL_ACTIVE, Wait;
    BOOL Created;

    GetModuleFileNameA(NULL, Path, sizeof(Path));
    sprintf(Command, "\"%s\" %s", Path, Mode);
    Created = CreateProcessA(NULL, Command, NULL, NULL, FALSE, 0, NULL, NULL,
                             &Startup, &Process);
    CHECK(Created);
    if (!Created) return;
    Wait = WaitForSingleObject(Process.hProcess, 30000);
    CHECK(Wait == WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0) TerminateProcess(Process.hProcess, 79);
    CHECK(GetExitCodeProcess(Process.hProcess, &Code));
    CHECK(Code == ExpectedExit);
    DbgPrint("RISCVTEST: child %s exit=%lx expected=%lx\n", Mode, Code, ExpectedExit);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
}

static DWORD WINAPI Worker(PVOID Event)
{
    return WaitForSingleObject(Event, 30000) == WAIT_OBJECT_0 ? 0 : 1;
}

static VOID ContextTests(VOID)
{
    HANDLE Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE Thread = CreateThread(NULL, 0, Worker, Event, CREATE_SUSPENDED, NULL);
    CONTEXT Original = {0}, Changed = {0};
    CHECK(Event != NULL && Thread != NULL);
    if (Thread)
    {
        Original.ContextFlags = CONTEXT_FULL;
        CHECK(GetThreadContext(Thread, &Original));
        CHECK(Original.Pc != 0 && Original.Sp != 0 && !(Original.Sp & 15));
        Changed = Original;
        Changed.S3 = 0x123456789abcdef0ULL;
        CHECK(SetThreadContext(Thread, &Changed));
        ZeroMemory(&Changed, sizeof(Changed));
        Changed.ContextFlags = CONTEXT_FULL;
        CHECK(GetThreadContext(Thread, &Changed));
        CHECK(Changed.S3 == 0x123456789abcdef0ULL);
        CHECK(SetThreadContext(Thread, &Original));
        CHECK(!GetThreadContext(Thread, (PCONTEXT)(ULONG_PTR)0x42));
        Changed.ContextFlags = 0;
        CHECK(!GetThreadContext(Thread, &Changed));
        SetEvent(Event);
        CHECK(ResumeThread(Thread) != MAXDWORD);
        CHECK(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0);
        CloseHandle(Thread);
    }
    if (Event) CloseHandle(Event);
}

static VOID KernelTests(VOID)
{
    SC_HANDLE Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    SC_HANDLE Service;
    SERVICE_STATUS Status;
    WCHAR Path[MAX_PATH];
    CHECK(Manager != NULL);
    if (!Manager) return;
    GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    wcscat(Path, L"\\drivers\\riscv64_porttest.sys");
    Service = CreateServiceW(Manager, L"riscv64_porttest", L"RISC-V regression tests",
                             SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                             SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                             Path, NULL, NULL, NULL, NULL, NULL);
    CHECK(Service != NULL);
    if (Service)
    {
        CHECK(StartServiceW(Service, 0, NULL));
        CHECK(ControlService(Service, SERVICE_CONTROL_STOP, &Status));
        CHECK(DeleteService(Service));
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);
}

__declspec(noinline) static ULONG Capture(VOID)
{
    PVOID Frames[12];
    return RtlWalkFrameChain(Frames, ARRAYSIZE(Frames), 0);
}

__declspec(noinline) static VOID StackTests(ULONG Size)
{
    volatile UCHAR *Buffer = _alloca(Size);
    ULONG Index;
    for (Index = 0; Index < Size; Index += 4096) Buffer[Index] = (UCHAR)(Index >> 12);
    Buffer[Size - 1] = 0x5a;
    CHECK(Buffer[0] == 0 && Buffer[Size - 1] == 0x5a);
    CHECK(Capture() >= 3);
}

static DWORD64 CALLBACK ModuleBase(HANDLE Process, DWORD64 Pc)
{
    PVOID Base = NULL;
    UNREFERENCED_PARAMETER(Process);
    RtlPcToFileHeader((PVOID)(ULONG_PTR)Pc, &Base);
    return (ULONG_PTR)Base;
}

__declspec(noinline) static VOID DbgHelpTests(VOID)
{
    CONTEXT Context;
    STACKFRAME64 Frame = {0};
    ULONG Count = 0;
    RtlCaptureContext(&Context);
    Frame.AddrPC.Offset = Context.Pc;
    Frame.AddrStack.Offset = Context.Sp;
    Frame.AddrPC.Mode = Frame.AddrStack.Mode = AddrModeFlat;
    while (Count < 32 && StackWalk64(IMAGE_FILE_MACHINE_RISCV64,
                                    GetCurrentProcess(), GetCurrentThread(),
                                    &Frame, &Context, NULL, NULL, ModuleBase, NULL))
        ++Count;
    DbgPrint("RISCVTEST: DbgHelp frames=%lu\n", Count);
    CHECK(Count >= 3 && Count < 32);
}

static VOID ProfileTests(VOID)
{
    DWORD_PTR Affinity = SetThreadAffinityMask(GetCurrentThread(), 1);
    HANDLE Profile = NULL;
    ULONG OldInterval, Interval, Size, Buckets, Index, Samples = 0;
    ULONG *Buffer;
    DWORD Start;
    PVOID Base = GetModuleHandleW(NULL);
    PIMAGE_NT_HEADERS Nt = RtlImageNtHeader(Base);
    NTSTATUS Status;
    volatile ULONG Work = 0;
    Size = Nt->OptionalHeader.SizeOfImage;
    Buckets = (Size + 4095) >> 12;
    Buffer = VirtualAlloc(NULL, Buckets * sizeof(ULONG), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(Buffer != NULL);
    if (!Buffer) { SetThreadAffinityMask(GetCurrentThread(), Affinity); return; }
    NtQueryIntervalProfile(ProfileTime, &OldInterval);
    CHECK(NT_SUCCESS(NtSetIntervalProfile(100000, ProfileTime)));
    CHECK(NT_SUCCESS(NtQueryIntervalProfile(ProfileTime, &Interval)) && Interval >= 100000);
    /* Profiling our own process does not require the system-wide privilege. */
    Status = NtCreateProfile(&Profile, GetCurrentProcess(), Base, Size, 12,
                             Buffer, Buckets * sizeof(ULONG), ProfileTime, 1);
    CHECK(NT_SUCCESS(Status));
    if (NT_SUCCESS(Status))
    {
        Status = NtStartProfile(Profile);
        CHECK(NT_SUCCESS(Status));
        if (NT_SUCCESS(Status))
        {
            Start = GetTickCount();
            do { for (Index = 0; Index < 4096; ++Index) Work += Index; }
            while (GetTickCount() - Start < 500);
            CHECK(NT_SUCCESS(NtStopProfile(Profile)));
            for (Index = 0; Index < Buckets; ++Index) Samples += Buffer[Index];
            DbgPrint("RISCVTEST: profile samples=%lu\n", Samples);
            CHECK(Samples > 0);
        }
        NtClose(Profile);
    }
    NtSetIntervalProfile(OldInterval, ProfileTime);
    VirtualFree(Buffer, 0, MEM_RELEASE);
    SetThreadAffinityMask(GetCurrentThread(), Affinity);
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc > 1 && !strcmp(argv[1], "--fastfail")) FastFailChild();
    if (argc > 1 && !strcmp(argv[1], "--noncontinuable"))
    {
        __try { RaiseException(0xe1234567, EXCEPTION_NONCONTINUABLE, 0, NULL); }
        __except(EXCEPTION_CONTINUE_EXECUTION) { ExitProcess(78); }
        ExitProcess(80);
    }
    DbgPrint("RISCVTEST: BEGIN\n");
    Failures += RunSehTests();
    Failures += RunCxxTests();
    /* DbgPrompt computes strlen before entering the kernel. Use a valid
     * prompt and invalid output buffer to exercise the kernel probe. */
    CHECK(DbgPrompt("", (PCH)(ULONG_PTR)0x42, 16) == 0);
    ContextTests();
    StackTests(65536);
    DbgHelpTests();
    ProfileTests();
    KernelTests();
    Failures += RunSmpTests();
    CheckChild("--fastfail", 0xc0000409);
    CheckChild("--noncontinuable", 0xc0000025);
    DbgPrint("RISCVTEST: END checks=%lu failures=%lu\n", Checks, Failures);
    return Failures ? 1 : 0;
}
