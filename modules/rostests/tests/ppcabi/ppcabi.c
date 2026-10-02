/*
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Native Windows NT PowerPC compiler/ABI probe.
 * No ReactOS runtime is linked: exception dispatch, callbacks and CRT calls
 * are supplied by the operating system being tested.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef ABI_LOG
#define ABI_LOG "ppcabi.log"
#endif
#define TEST_EXCEPTION 0xe0425050u
#define NOINLINE __declspec(noinline)

__declspec(dllimport) LONG WINAPI NtContinue(PCONTEXT Context, BOOLEAN TestAlert);

static HANDLE Log;
static unsigned Checks, Failures;
static volatile unsigned Trace, FinallyCount, CallbackCount;
static volatile unsigned NtContinueVisits;
static volatile ULONG_PTR FaultAddress;
static volatile ULONG_PTR NtContinueCallerToc, NtContinuePreCallToc;
static CONTEXT NtContinueContext;

static void text(const char *s)
{
    DWORD length = 0, written;
    while (s[length]) ++length;
    if (Log != INVALID_HANDLE_VALUE)
    {
        WriteFile(Log, s, length, &written, NULL);
        FlushFileBuffers(Log);
    }
    OutputDebugStringA(s);
}

static void number(unsigned value)
{
    char s[11];
    unsigned i;
    s[0] = '0'; s[1] = 'x'; s[10] = 0;
    for (i = 0; i != 8; ++i)
        s[2+i] = "0123456789abcdef"[(value >> (28-i*4)) & 15];
    text(s);
}

static void check(int condition, const char *name)
{
    ++Checks;
    if (!condition) ++Failures;
    text(condition ? "PASS " : "FAIL ");
    text(name); text("\r\n");
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *p)
{
    text("UNHANDLED exception="); number(p->ExceptionRecord->ExceptionCode);
    text(" pc="); number(p->ContextRecord->Iar);
    text(" sp="); number(p->ContextRecord->Gpr1); text("\r\n");
    ExitProcess(99);
    return EXCEPTION_EXECUTE_HANDLER;
}

static NOINLINE void raise_test(void)
{
    ULONG_PTR args[2] = {0x12345678, 0xabcdef01};
    RaiseException(TEST_EXCEPTION, 0, 2, args);
}

static NOINLINE int inspect(EXCEPTION_POINTERS *p, unsigned local)
{
    check(p->ExceptionRecord->ExceptionCode == TEST_EXCEPTION, "filter exception code");
    check(p->ExceptionRecord->NumberParameters == 2 &&
          p->ExceptionRecord->ExceptionInformation[0] == 0x12345678 &&
          p->ExceptionRecord->ExceptionInformation[1] == 0xabcdef01,
          "RaiseException argument array");
    check(p->ContextRecord->Iar != 0 && (p->ContextRecord->Gpr1 & 15) == 0,
          "NT4 CONTEXT control registers and stack alignment");
    check(local == 0x87654321, "filter parent local and TOC recovery");
    return EXCEPTION_EXECUTE_HANDLER;
}

static NOINLINE void test_raise(void)
{
    volatile unsigned local = 0x87654321;
    int caught = 0;
    __try { raise_test(); }
    __except (inspect(GetExceptionInformation(), local))
    {
        caught = GetExceptionCode() == TEST_EXCEPTION;
    }
    check(caught, "NT4 dispatch to compiler-generated except body");
    check(local == 0x87654321, "local survives exception unwind");
}

static NOINLINE int access_filter(EXCEPTION_POINTERS *p)
{
    check(p->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION,
          "hardware access violation code");
    check(p->ExceptionRecord->NumberParameters >= 2 &&
          p->ExceptionRecord->ExceptionInformation[0] == 1 &&
          p->ExceptionRecord->ExceptionInformation[1] == FaultAddress,
          "hardware write fault information");
    return EXCEPTION_EXECUTE_HANDLER;
}

static NOINLINE void test_access(void)
{
    int caught = 0;
    __try { *(volatile unsigned *)FaultAddress = 123; }
    __except (access_filter(GetExceptionInformation())) { caught = 1; }
    check(caught, "asynchronous fault covered by SEH scope");
}

static NOINLINE void unwind_leaf(void)
{
    volatile unsigned local = 17;
    __try { raise_test(); }
    __finally
    {
        check(AbnormalTermination(), "abnormal finally flag");
        check(local == 17, "unwind finally parent local");
        Trace = Trace * 10 + 1;
    }
}

static NOINLINE int return_from_try(void)
{
    __try { return 42; }
    __finally
    {
        /* Unlike __leave, return is abnormal termination of the protected
         * block even when there is no exception (Win32 AbnormalTermination). */
        check(AbnormalTermination(), "return finally is abnormal");
        ++FinallyCount;
    }
}

static NOINLINE void nested_finally_exit(unsigned kind)
{
    unsigned count = 0;
    __try
    {
        __try
        {
            if (kind == 1) goto outside;
            if (kind == 2) goto inside;
            __leave;
        }
        __finally
        {
            check(!!AbnormalTermination() == (kind != 0),
                  "nested inner finally exit classification");
            ++count;
        }
    inside:;
    }
    __finally
    {
        check(!!AbnormalTermination() == (kind == 1),
              "nested outer finally exit classification");
        ++count;
    }
outside:
    check(count == 2, "nested control exit executes both finalizers once");
}

static NOINLINE void aligned_finally_leaf(int unwind)
{
    __declspec(align(64)) volatile unsigned local = 17;
    __try
    {
        if (unwind) raise_test();
    }
    __finally
    {
        /* Exercise both integer and floating callee saves in the funclet. */
        __asm__ volatile ("" ::: "r29", "f14");
        check(local == 17, "realigned finally accesses parent local");
        check(!!AbnormalTermination() == !!unwind, "realigned finally exit classification");
        ++local;
    }
    check(local == 18, "realigned normal finally updates parent local");
}

static NOINLINE void aligned_finally_caller(unsigned padding)
{
    volatile unsigned char *space = __builtin_alloca(padding);
    int caught = 0;
    space[0] = 0x5a;
    aligned_finally_leaf(0);
    __try { aligned_finally_leaf(1); }
    __except (GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER :
                                                  EXCEPTION_CONTINUE_SEARCH)
    { caught = 1; }
    check(caught, "unwind through realigned frame reaches caller");
    check(space[0] == 0x5a, "realigned funclet preserves caller stack");
}

static NOINLINE void clobber_fp_and_raise(int hardware)
{
    const double values[3] = {-2.5, -22.5, -31.5};
    /* The compiler must save these nonvolatile registers in this frame.
     * An exception abandons its epilogue, so RTL must restore the saves. */
    __asm__ volatile ("lfd 14,0(%0)\n\tlfd 22,8(%0)\n\tlfd 31,16(%0)"
                      : : "b"(values), "m"(values) : "f14", "f22", "f31");
    if (hardware) *(volatile unsigned *)FaultAddress = 123;
    else raise_test();
}

static NOINLINE void test_unwind_fp(int hardware)
{
    const double before[3] = {14.25, 22.25, 31.25};
    double after[3];
    DWORD caught = 0;

    __asm__ volatile ("lfd 14,0(%0)\n\tlfd 22,8(%0)\n\tlfd 31,16(%0)"
                      : : "b"(before), "m"(before) : "f14", "f22", "f31");
    __try { clobber_fp_and_raise(hardware); }
    __except (EXCEPTION_EXECUTE_HANDLER) { caught = GetExceptionCode(); }
    __asm__ volatile ("stfd 14,0(%1)\n\tstfd 22,8(%1)\n\tstfd 31,16(%1)"
                      : "=m"(after) : "b"(after));

    check(caught == (hardware ? EXCEPTION_ACCESS_VIOLATION : TEST_EXCEPTION),
          "register preservation test reaches exception destination");
    check(after[0] == before[0], "SEH unwind preserves nonvolatile f14");
    check(after[1] == before[1], "SEH unwind preserves nonvolatile f22");
    check(after[2] == before[2], "SEH unwind preserves nonvolatile f31");
}

static NOINLINE void test_finally(void)
{
    volatile unsigned local = 3;
    __try { ++local; __leave; local = 0; }
    __finally
    {
        check(!AbnormalTermination(), "leave finally is normal");
        local += 5;
    }
    check(local == 9, "leave executes finally once");
    check(return_from_try() == 42 && FinallyCount == 1, "return through finally");
    nested_finally_exit(0);
    nested_finally_exit(1);
    nested_finally_exit(2);
    /* Vary incoming SP modulo 64; the ABI only guarantees 16-byte alignment. */
    aligned_finally_caller(16);
    aligned_finally_caller(32);
    aligned_finally_caller(48);
    aligned_finally_caller(64);
    test_unwind_fp(0);
    test_unwind_fp(1);
    Trace = 0;
    __try
    {
        __try { unwind_leaf(); }
        __finally { Trace = Trace * 10 + 2; }
    }
    __except (GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER :
                                                  EXCEPTION_CONTINUE_SEARCH)
    { Trace = Trace * 10 + 3; }
    check(Trace == 123, "cross-frame nested finally order");
}

static NOINLINE void test_continue(void)
{
    int resumed = 0, caught = 0;
    __try { RaiseException(TEST_EXCEPTION, 0, 0, NULL); resumed = 1; }
    __except (GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_CONTINUE_EXECUTION :
                                                   EXCEPTION_EXECUTE_HANDLER)
    { caught = 1; }
    check(resumed && !caught, "continue execution after RaiseException");
    __try
    {
        __try { raise_test(); }
        __except (EXCEPTION_CONTINUE_SEARCH) { caught = -1; }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { caught = 1; }
    check(caught == 1, "continue search reaches outer scope");
}

/* Exercise the native user-to-kernel context restore separately from the
 * language handler's EXCEPTION_CONTINUE_EXECUTION path above. A resumed call
 * arrives at the instruction after RtlCaptureContext for a second time.
 * NT4's RtlCaptureContext stores its own ntdll TOC in Gpr2; the PPC caller
 * reloads its TOC from the linkage area immediately after that call. */
static NOINLINE void test_ntcontinue(void)
{
    LONG status;
    ULONG_PTR resumed_sp, resumed_toc;

    if (!NtContinueVisits)
    {
        ULONG_PTR caller_toc;
        __asm__ volatile ("mr %0,2" : "=r"(caller_toc));
        NtContinueCallerToc = caller_toc;
    }

    RtlCaptureContext(&NtContinueContext);
    if (++NtContinueVisits == 1)
    {
        int valid = ((NtContinueContext.ContextFlags & CONTEXT_FULL) == CONTEXT_FULL &&
                     NtContinueContext.Iar != 0 && !(NtContinueContext.Iar & 3) &&
                     !(NtContinueContext.Gpr1 & 15) && NtContinueContext.Gpr2 != 0);
        check(valid, "NtContinue captured PPC control and integer context");
        if (!valid) return;
        {
            ULONG_PTR toc;
            __asm__ volatile ("mr %0,2" : "=r"(toc));
            NtContinuePreCallToc = toc;
        }
        status = NtContinue(&NtContinueContext, FALSE);
        text("NtContinue returned status="); number(status); text("\r\n");
        check(0, "NtContinue resumes instead of returning");
        return;
    }
    __asm__ volatile ("mr %0,1\n\tmr %1,2" : "=r"(resumed_sp), "=r"(resumed_toc));
    text("TOC captured="); number(NtContinueContext.Gpr2);
    text(" caller="); number(NtContinueCallerToc);
    text(" pre-call="); number(NtContinuePreCallToc);
    text(" resumed="); number(resumed_toc); text("\r\n");
    check(NtContinueVisits == 2, "NtContinue resumed the captured instruction once");
    check(resumed_sp == NtContinueContext.Gpr1, "NtContinue restored the PPC stack pointer");
    check(NtContinueContext.Gpr2 != NtContinueCallerToc,
          "RtlCaptureContext recorded the ntdll function-descriptor TOC");
    check(NtContinuePreCallToc == NtContinueCallerToc,
          "cross-DLL debug output preserved the PPC caller TOC");
    check(resumed_toc == NtContinueCallerToc,
          "PPC caller TOC restored after NtContinue");
}

static NOINLINE void test_stack(void)
{
    volatile unsigned char bytes[65536];
    unsigned i, sum = 0;
    for (i = 0; i < sizeof(bytes); i += 4096) bytes[i] = (unsigned char)(i / 4096);
    __try { raise_test(); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    { for (i = 0; i < sizeof(bytes); i += 4096) sum += bytes[i]; }
    check(sum == 120, "large frame stack probing and SEH unwind");
}

static DWORD WINAPI thread_entry(void *arg)
{
    int caught = 0;
    __try { raise_test(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { caught = 1; }
    return arg == (void *)0x13579 && caught ? 0x24680 : 0;
}

static void test_thread(void)
{
    DWORD id, result = 0;
    HANDLE h = CreateThread(NULL, 0, thread_entry, (void *)0x13579, 0, &id);
    check(h != NULL, "CreateThread callback descriptor");
    if (!h) return;
    check(WaitForSingleObject(h, 15000) == WAIT_OBJECT_0, "thread callback completes");
    check(GetExitCodeThread(h, &result) && result == 0x24680,
          "NT4 calls our thread entry and dispatches its SEH");
    CloseHandle(h);
}

static int __cdecl compare(const void *a, const void *b)
{
    ++CallbackCount;
    return *(const int *)a - *(const int *)b;
}

static int __cdecl compare_raise(const void *a, const void *b)
{
    (void)a; (void)b;
    raise_test();
    return 0;
}

struct quotient { int quot, rem; };
static void test_crt(void)
{
    typedef void (__cdecl *SORT)(void *, unsigned, unsigned,
                                int (__cdecl *)(const void *, const void *));
    typedef int (__cdecl *FORMAT)(char *, unsigned, const char *, ...);
    typedef struct quotient (__cdecl *DIVIDE)(int, int);
    typedef double (__cdecl *SCALE)(double, int);
    HMODULE crt = LoadLibraryA("msvcrt.dll");
    SORT sort; FORMAT format; DIVIDE divide; SCALE scale;
    int values[5] = {5, 1, 4, 2, 3}, caught = 0, i;
    char buf[128];
    struct quotient q;
    check(crt != NULL, "load operating system MSVCRT");
    if (!crt) return;
    sort = (SORT)GetProcAddress(crt, "qsort");
    format = (FORMAT)GetProcAddress(crt, "_snprintf");
    divide = (DIVIDE)GetProcAddress(crt, "div");
    scale = (SCALE)GetProcAddress(crt, "ldexp");
    check(sort && format && divide && scale, "resolve NT4 CRT entry points");
    if (!(sort && format && divide && scale)) return;
    sort(values, 5, sizeof(values[0]), compare);
    for (i = 0; i != 5; ++i) if (values[i] != i + 1) break;
    check(i == 5 && CallbackCount != 0, "NT4 qsort callback and TOC restoration");
    __try { sort(values, 5, sizeof(values[0]), compare_raise); }
    __except (GetExceptionCode() == TEST_EXCEPTION ? EXCEPTION_EXECUTE_HANDLER :
                                                  EXCEPTION_CONTINUE_SEARCH)
    { caught = 1; }
    check(caught, "unwind from callback through NT4 CRT frames");
    q = divide(-23, 7);
    check(q.quot == -3 && q.rem == -2, "NT4 CRT aggregate return ABI");
    check(scale(1.5, 4) == 24.0, "NT4 CRT mixed floating and integer arguments");
    format(buf, sizeof(buf), "%d %.2f %d %I64x %d %d %d %d %d %d",
           7, 1.25, 9, 0x1122334455667788ULL, 10, 11, 12, 13, 14, 15);
    check(lstrcmpA(buf, "7 1.25 9 1122334455667788 10 11 12 13 14 15") == 0,
          "NT4 variadic FP, 64-bit and stack argument ABI");
    FreeLibrary(crt);
}

unsigned ppc_jump_zero_probe(void);
unsigned ppc_jump_value_probe(void);
unsigned ppc_jump_collided_probe(void);

static void test_jump(void)
{
    check(ppc_jump_zero_probe() == 3,
          "NT4 CRT setjmp/longjmp converts zero to one and retains volatile state");
    check(ppc_jump_value_probe() == 3,
          "NT4 CRT setjmp/longjmp returns nonzero value and retains volatile state");
}

static void test_jumpseh(void)
{
    unsigned Result = ppc_jump_collided_probe();
    check((Result & 1) != 0, "extended setjmp resumes with longjmp value");
    check((Result & 2) != 0, "collided unwind runs exactly the required finally scopes");
    check((Result & 4) != 0, "collided unwind executes each finally exactly once");
}

static void test_ntdll(void)
{
    typedef LARGE_INTEGER (WINAPI *ADD)(LARGE_INTEGER, LARGE_INTEGER);
    typedef LARGE_INTEGER (WINAPI *MUL)(LARGE_INTEGER, LONG);
    ADD add; MUL multiply;
    LARGE_INTEGER a, b, c;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    add = (ADD)GetProcAddress(ntdll, "RtlLargeIntegerAdd");
    multiply = (MUL)GetProcAddress(ntdll, "RtlExtendedIntegerMultiply");
    check(add && multiply, "resolve NT4 large integer entry points");
    if (!(add && multiply)) return;
    a.QuadPart = 0x12345678abcdef01LL; b.QuadPart = 0x00000001432110ffLL;
    c = add(a, b);
    check(c.QuadPart == 0x12345679eeef0000LL, "NT4 large integer arguments and return");
    a.QuadPart = -0x123456789LL;
    c = multiply(a, -7);
    check(c.QuadPart == 0x7f6e5d4bfLL, "NT4 mixed aggregate and scalar arguments");

    {
        /* Deliberately populate unused argument registers to detect the
         * incompatible three-argument lookup ABI used by newer targets. */
        typedef PRUNTIME_FUNCTION (WINAPI *LOOKUP_SPARE_REGISTERS)(ULONG_PTR, PULONG, PVOID);
        LOOKUP_SPARE_REGISTERS lookup = (LOOKUP_SPARE_REGISTERS)
            GetProcAddress(ntdll, "RtlLookupFunctionEntry");
        ULONG guard = 0x13579bdf;
        ULONG_PTR pc = *(const ULONG_PTR *)(void *)test_raise;
        PRUNTIME_FUNCTION entry;
        check(lookup != NULL, "resolve NT4 function lookup");
        if (!lookup) return;
        entry = lookup(pc, &guard, NULL);
        check(guard == 0x13579bdf, "function lookup ignores unused argument registers");
        check(entry && entry->BeginAddress <= pc && pc < entry->EndAddress,
              "function lookup returns absolute PPC code range");
        if (guard == 0x13579bdf)
        {
            check(RtlLookupFunctionEntry(pc) == entry, "one-argument NT4 function lookup");
            check(RtlLookupFunctionEntry(0) == NULL, "function lookup rejects unmapped PC");
        }
    }

    {
        static CONTEXT before;
        PRUNTIME_FUNCTION entry;
        ULONG_PTR pc, sp;
        ULONG flags, next, frame = 0x13579bdf;
        BOOLEAN in_function = 0x7f;

        RtlCaptureContext(&before);
        pc = before.Iar;
        sp = before.Gpr1;
        flags = before.ContextFlags;
        entry = RtlLookupFunctionEntry(pc);
        check(entry != NULL, "NT4 virtual unwind has a function entry");
        if (!entry) return;
        next = RtlVirtualUnwind(pc, entry, &before, &in_function, &frame,
                                NULL, 0, 0xffffffff);
        check(next != 0 && next != pc, "NT4 virtual unwind returns caller PC");
        check(in_function <= 1, "NT4 virtual unwind writes InFunction byte");
        check(frame >= sp && !(frame & 7), "NT4 virtual unwind writes aligned establisher frame");
        check(before.Iar == pc, "NT4 virtual unwind preserves context Iar");
        check(before.ContextFlags == flags, "NT4 virtual unwind preserves context flags");
    }
    {
        static const ULONG leaf_instruction = 0x60000000; /* ori r0,r0,0 */
        static CONTEXT leaf;
        ULONG pc, flags, frame = 0x13579bdf;
        BOOLEAN in_function = 0x7f;

        RtlCaptureContext(&leaf);
        pc = leaf.Iar;
        flags = leaf.ContextFlags;
        leaf.Lr = (ULONG_PTR)&leaf_instruction + sizeof(ULONG);
        check(RtlVirtualUnwind(pc, NULL, &leaf, &in_function, &frame,
                               NULL, 0, 0xffffffff) == (ULONG_PTR)&leaf_instruction,
              "NT4 leaf virtual unwind returns LR minus one instruction");
        check(in_function == 0x7f && frame == 0x13579bdf,
              "NT4 leaf virtual unwind leaves frame outputs untouched");
        check(leaf.Iar == pc && leaf.ContextFlags == flags,
              "NT4 leaf virtual unwind preserves context control fields");
    }
}

struct test_case { const char *name; void (*run)(void); };
static const struct test_case Cases[] = {
#define PPC_ABI_CASE(name) {#name, test_##name},
#include "cases.def"
#undef PPC_ABI_CASE
};

void WINAPI test_entry(void)
{
    static OSVERSIONINFOA version;
    const char *command = GetCommandLineA(), *selected = NULL;
    char filename[80];
    unsigned i, length = 0;
    int quoted = *command == '"';
    if (quoted) ++command;
    while (*command && (quoted ? *command != '"' : *command != ' ')) ++command;
    if (*command == '"') ++command;
    while (*command == ' ') ++command;
    if (*command)
    {
        for (i = 0; i != sizeof(Cases)/sizeof(Cases[0]); ++i)
            if (lstrcmpA(command, Cases[i].name) == 0) selected = Cases[i].name;
        if (!selected) ExitProcess(2);
    }
    while (ABI_LOG[length]) { filename[length] = ABI_LOG[length]; ++length; }
    if (selected)
    {
        length -= 4; /* Replace .log with .<case>.log. */
        filename[length++] = '.';
        for (i = 0; selected[i]; ++i) filename[length++] = selected[i];
        filename[length++] = '.'; filename[length++] = 'l';
        filename[length++] = 'o'; filename[length++] = 'g';
    }
    filename[length] = 0;
    Log = CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(unhandled);
    text("PPC ABI probe: " ABI_LOG "\r\n");
    version.dwOSVersionInfoSize = sizeof(version);
    check(GetVersionExA(&version), "GetVersionExA");
    text("OS major="); number(version.dwMajorVersion);
    text(" minor="); number(version.dwMinorVersion);
    text(" build="); number(version.dwBuildNumber); text("\r\n");
    check(sizeof(void *) == 4 && sizeof(long) == 4 && sizeof(CONTEXT) == 0x1d0,
          "32-bit NT PowerPC public type sizes");
    for (i = 0; i != sizeof(Cases)/sizeof(Cases[0]); ++i)
    {
        if (selected && selected != Cases[i].name) continue;
        text("START "); text(Cases[i].name); text("\r\n");
        Cases[i].run();
    }
    text("COMPLETE checks="); number(Checks);
    text(" failures="); number(Failures); text("\r\n");
    if (Log != INVALID_HANDLE_VALUE) CloseHandle(Log);
    ExitProcess(Failures ? 1 : 0);
}
