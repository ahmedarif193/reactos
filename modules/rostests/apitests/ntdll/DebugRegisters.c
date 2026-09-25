/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 hardware breakpoints across user/kernel transitions
 */
#include <apitest.h>
#include <winuser.h>
#include <ndk/umtypes.h>

#ifdef _M_AMD64
#define TEST_EXCEPTION 0xe0421640
#define ACTION_WRITE 1
#define ACTION_EXECUTE 2
#define ACTION_QUIT 3
#define ACTION_APC 4
#define ACTION_WINDOW 5

typedef struct
{
    HANDLE Thread, Start, Done;
    DWORD Id;
    volatile LONG Action;
} WATCH_THREAD;

static volatile LONG Watched, Hits, UnexpectedHits, ClearOnHit;
static DWORD WatchedThread;
static ULONG64 LastDr6, LastDr7;
static PVOID LastPc;
static void (*ExecuteProbe)(void);
static HWND (WINAPI *pCreateWindowExW)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static BOOL (WINAPI *pDestroyWindow)(HWND);
static LRESULT (WINAPI *pDefWindowProcW)(HWND, UINT, WPARAM, LPARAM);
static const WCHAR WatchClass[] = L"DebugRegisterCallbackTest";

static VOID CALLBACK
WatchApc(ULONG_PTR Parameter)
{
    Watched = (LONG)Parameter;
}

static LRESULT CALLBACK
WatchWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_CREATE) Watched = 0x12345678;
    return pDefWindowProcW(Window, Message, WParam, LParam);
}

static LONG CALLBACK
WatchHandler(PEXCEPTION_POINTERS Info)
{
    PCONTEXT Context = Info->ContextRecord;
    if (Info->ExceptionRecord->ExceptionCode == TEST_EXCEPTION)
        return EXCEPTION_CONTINUE_EXECUTION;
    if (Info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        !(Context->Dr6 & 1))
        return EXCEPTION_CONTINUE_SEARCH;

    if (GetCurrentThreadId() != WatchedThread)
        InterlockedIncrement(&UnexpectedHits);
    InterlockedIncrement(&Hits);
    LastDr6 = Context->Dr6;
    LastDr7 = Context->Dr7;
    LastPc = Info->ExceptionRecord->ExceptionAddress;
    Context->Dr6 = 0;
    if (ClearOnHit) Context->Dr7 = 0;
    /* Resume an instruction breakpoint without executing it twice. */
    Context->EFlags |= 0x10000;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI
WatchThread(PVOID Parameter)
{
    WATCH_THREAD *Thread = Parameter;
    for (;;)
    {
        if (WaitForSingleObject(Thread->Start, INFINITE) != WAIT_OBJECT_0)
            return 1;
        if (Thread->Action == ACTION_QUIT) return 0;
        Sleep(0);
        RaiseException(TEST_EXCEPTION, 0, 0, NULL);
        if (Thread->Action == ACTION_WRITE)
            Watched = 0x12345678;
        else if (Thread->Action == ACTION_EXECUTE)
            ExecuteProbe();
        else if (Thread->Action == ACTION_APC)
        {
            DWORD Result = SleepEx(1000, TRUE);
            ok(Result == WAIT_IO_COMPLETION, "APC wait returned %lu\n", Result);
            Watched = 0x12345678;
        }
        else if (Thread->Action == ACTION_WINDOW)
        {
            HWND Window = pCreateWindowExW(0, WatchClass, L"", WS_OVERLAPPED,
                                          0, 0, 32, 32, NULL, NULL, GetModuleHandleW(NULL), NULL);
            ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
            if (Window) pDestroyWindow(Window);
            Watched = 0x12345678;
        }
        SetEvent(Thread->Done);
    }
}

static BOOL
DebugContext(WATCH_THREAD *Thread, PCONTEXT Context, BOOL Set)
{
    BOOL Result;
    DWORD Previous = SuspendThread(Thread->Thread);
    ok(Previous != (DWORD)-1, "SuspendThread failed: %lu\n", GetLastError());
    if (Previous == (DWORD)-1) return FALSE;
    Context->ContextFlags = CONTEXT_DEBUG_REGISTERS;
    Result = Set ? SetThreadContext(Thread->Thread, Context) : GetThreadContext(Thread->Thread, Context);
    ok(Result, "%sThreadContext failed: %lu\n", Set ? "Set" : "Get", GetLastError());
    ok(ResumeThread(Thread->Thread) != (DWORD)-1, "ResumeThread failed: %lu\n", GetLastError());
    return Result;
}

static BOOL
RunAction(WATCH_THREAD *Thread, LONG Action)
{
    DWORD Result;
    Thread->Action = Action;
    SetEvent(Thread->Start);
    Result = WaitForSingleObject(Thread->Done, 10000);
    ok(Result == WAIT_OBJECT_0, "Worker action %ld did not complete: %lu\n", Action, Result);
    return Result == WAIT_OBJECT_0;
}
#endif

START_TEST(DebugRegisters)
{
#ifdef _M_AMD64
    WATCH_THREAD Threads[2] = {{0}};
    CONTEXT Context;
    DWORD_PTR ProcessMask, SystemMask, FirstCpu, OtherCpu;
    PVOID Handler;
    HMODULE User32;
    WNDCLASSW Class = {0};
    ATOM ClassAtom = 0;
    ATOM (WINAPI *pRegisterClassW)(const WNDCLASSW *);
    BOOL (WINAPI *pUnregisterClassW)(LPCWSTR, HINSTANCE);
    unsigned int i, Round = 0;
    LONG Before;
    BOOL Running = TRUE;

    Handler = AddVectoredExceptionHandler(1, WatchHandler);
    ok(Handler != NULL, "AddVectoredExceptionHandler failed\n");
    if (!Handler) return;
    ExecuteProbe = (void (*)(void))VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    ok(ExecuteProbe != NULL, "VirtualAlloc failed\n");
    if (!ExecuteProbe) { RemoveVectoredExceptionHandler(Handler); return; }
    *(BYTE *)(ULONG_PTR)ExecuteProbe = 0xc3; /* ret */
    FlushInstructionCache(GetCurrentProcess(), (PVOID)(ULONG_PTR)ExecuteProbe, 1);

    ProcessMask = SystemMask = 1;
    ok(GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask),
       "GetProcessAffinityMask failed\n");
    User32 = LoadLibraryW(L"user32.dll");
    ok(User32 != NULL, "LoadLibrary(user32) failed\n");
    pRegisterClassW = (void *)GetProcAddress(User32, "RegisterClassW");
    pUnregisterClassW = (void *)GetProcAddress(User32, "UnregisterClassW");
    pCreateWindowExW = (void *)GetProcAddress(User32, "CreateWindowExW");
    pDestroyWindow = (void *)GetProcAddress(User32, "DestroyWindow");
    pDefWindowProcW = (void *)GetProcAddress(User32, "DefWindowProcW");
    if (pRegisterClassW && pUnregisterClassW && pCreateWindowExW && pDestroyWindow && pDefWindowProcW)
    {
        Class.lpfnWndProc = WatchWindowProc;
        Class.hInstance = GetModuleHandleW(NULL);
        Class.lpszClassName = WatchClass;
        ClassAtom = pRegisterClassW(&Class);
    }
    ok(ClassAtom != 0, "RegisterClassW failed\n");
    FirstCpu = ProcessMask & -(LONG_PTR)ProcessMask;
    OtherCpu = ProcessMask & ~FirstCpu;
    OtherCpu &= -(LONG_PTR)OtherCpu;
    if (!OtherCpu) OtherCpu = FirstCpu;
    Hits = UnexpectedHits = ClearOnHit = 0;

    for (i = 0; i < 2; ++i)
    {
        Threads[i].Start = CreateEventW(NULL, FALSE, FALSE, NULL);
        Threads[i].Done = CreateEventW(NULL, FALSE, FALSE, NULL);
        Threads[i].Thread = CreateThread(NULL, 0, WatchThread, &Threads[i], CREATE_SUSPENDED, &Threads[i].Id);
        ok(Threads[i].Start && Threads[i].Done && Threads[i].Thread, "Worker %u creation failed\n", i);
        if (!Threads[i].Start || !Threads[i].Done || !Threads[i].Thread)
        {
            if (Threads[i].Thread) ResumeThread(Threads[i].Thread);
            Running = FALSE;
            break;
        }
        ZeroMemory(&Context, sizeof(Context));
        DebugContext(&Threads[i], &Context, FALSE);
        ok(!(Context.Dr0 | Context.Dr1 | Context.Dr2 | Context.Dr3 | Context.Dr6 | Context.Dr7),
           "New thread inherited debug registers: %I64x/%I64x\n", Context.Dr0, Context.Dr7);
        ok(SetThreadAffinityMask(Threads[i].Thread, FirstCpu) != 0, "SetThreadAffinityMask failed\n");
        ResumeThread(Threads[i].Thread);
    }
    if (Running)
    {
        WatchedThread = Threads[0].Id;
        ZeroMemory(&Context, sizeof(Context));
        Context.Dr0 = (ULONG_PTR)&Watched;
        Context.Dr7 = 0xd0001; /* local DWORD write breakpoint */
        Running = DebugContext(&Threads[0], &Context, TRUE);
    }
    for (Round = 0; Running && Round < 16; ++Round)
    {
        /* First share one CPU; then migrate the watched thread each round. */
        if (Round >= 8)
            ok(SetThreadAffinityMask(Threads[0].Thread, Round & 1 ? FirstCpu : OtherCpu) != 0,
               "Migration failed\n");
        Before = Hits;
        Running = RunAction(&Threads[1], ACTION_WRITE);
        ok(Hits == Before, "Watch leaked to another thread: %ld -> %ld\n", Before, Hits);
        if (!Running) break;
        Running = RunAction(&Threads[0], ACTION_WRITE);
        ok(Hits == Before + 1, "Round %u: expected one write trap, got %ld\n", Round, Hits - Before);
        ZeroMemory(&Context, sizeof(Context));
        DebugContext(&Threads[0], &Context, FALSE);
        ok(Context.Dr0 == (ULONG_PTR)&Watched && Context.Dr7 == 0xd0001,
           "Watch lost across syscall/exception: %I64x/%I64x\n", Context.Dr0, Context.Dr7);
    }
    if (Running)
    {
        Before = Hits;
        ok(QueueUserAPC(WatchApc, Threads[0].Thread, 0x12345678), "QueueUserAPC failed\n");
        Running = RunAction(&Threads[0], ACTION_APC);
        ok(Hits == Before + 2, "Watch lost in or after user APC: %ld traps\n", Hits - Before);
    }
    if (Running && ClassAtom)
    {
        Before = Hits;
        Running = RunAction(&Threads[0], ACTION_WINDOW);
        ok(Hits == Before + 2, "Watch lost in or after window callback: %ld traps\n", Hits - Before);
    }
    if (Running)
    {
        ClearOnHit = 1;
        Before = Hits;
        Running = RunAction(&Threads[0], ACTION_WRITE);
        if (Running) Running = RunAction(&Threads[0], ACTION_WRITE);
        ok(Hits == Before + 1, "Clearing DR7 in exception context did not disable watch\n");
        ZeroMemory(&Context, sizeof(Context));
        DebugContext(&Threads[0], &Context, FALSE);
        ok(Context.Dr0 == (ULONG_PTR)&Watched && Context.Dr7 == 0,
           "Disabled slot lost or reenabled: %I64x/%I64x\n", Context.Dr0, Context.Dr7);

        Context.Dr0 = (ULONG_PTR)ExecuteProbe;
        Context.Dr6 = 0;
        Context.Dr7 = 1;
        DebugContext(&Threads[0], &Context, TRUE);
        Before = Hits;
        if (Running) Running = RunAction(&Threads[0], ACTION_EXECUTE);
        ok(Hits == Before + 1 && LastPc == (PVOID)(ULONG_PTR)ExecuteProbe,
           "Instruction breakpoint: hits %ld, pc %p\n", Hits - Before, LastPc);
        ok((LastDr6 & 1) && (LastDr7 & 1), "Missing debug status: %I64x/%I64x\n", LastDr6, LastDr7);

        /* CONTEXT_DEBUG_REGISTERS alone contains no valid SegCs. Privileged
         * addresses and DR7 global/GD/reserved bits must be sanitized. */
        ZeroMemory(&Context, sizeof(Context));
        Context.Dr0 = ~(ULONG64)0;
        Context.Dr1 = ~(ULONG64)0;
        Context.Dr7 = 0x2402;
        DebugContext(&Threads[0], &Context, TRUE);
        ZeroMemory(&Context, sizeof(Context));
        DebugContext(&Threads[0], &Context, FALSE);
        ok(!Context.Dr0 && !Context.Dr1 && !Context.Dr7,
           "Invalid user debug state accepted: %I64x/%I64x/%I64x\n", Context.Dr0, Context.Dr1, Context.Dr7);
    }
    ok(!UnexpectedHits, "Unexpected threads trapped %ld times\n", UnexpectedHits);
    for (i = 0; i < 2; ++i)
    {
        if (Threads[i].Thread)
        {
            ZeroMemory(&Context, sizeof(Context));
            DebugContext(&Threads[i], &Context, TRUE);
            Threads[i].Action = ACTION_QUIT;
            SetEvent(Threads[i].Start);
            /* All observed actions completed before releasing their state. */
            WaitForSingleObject(Threads[i].Thread, INFINITE);
            CloseHandle(Threads[i].Thread);
        }
        if (Threads[i].Start) CloseHandle(Threads[i].Start);
        if (Threads[i].Done) CloseHandle(Threads[i].Done);
    }
    if (ClassAtom) pUnregisterClassW(WatchClass, GetModuleHandleW(NULL));
    if (User32) FreeLibrary(User32);
    VirtualFree((PVOID)(ULONG_PTR)ExecuteProbe, 0, MEM_RELEASE);
    RemoveVectoredExceptionHandler(Handler);
#else
    skip("AMD64 debug-register transition test\n");
#endif
}
