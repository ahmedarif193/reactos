/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */

/* Real native process: compiler-produced RVUW, kernel hardware delivery,
 * NTDLL dispatch/unwind, and an observable process exit status. */
#include <rtl.h>
#include "riscv64/unwind.h"
#undef __try
#undef __except
#undef __finally
#undef _exception_info

static ULONG Checks, Failures, Used, Order[32];
static BOOLEAN Collide;
static ULONG_PTR ResumePc;
#define CHECK(e) do { Checks++; if (!(e)) { Failures++; \
    DbgPrint("SEH: FAIL line=%lu\n", (ULONG)__LINE__); } } while (0)
static VOID Log(ULONG Value) { if (Used < 32) Order[Used++] = Value; }

__declspec(noinline) static VOID Fault(VOID)
{
    *(volatile ULONG *)(ULONG_PTR)0x42 = 0x1234;
}

static LONG Filter(PEXCEPTION_POINTERS Info, LONG Local, LONG Decision)
{
    CHECK(Info->ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION);
    CHECK(Info->ExceptionRecord->NumberParameters == 2);
    CHECK(Info->ExceptionRecord->ExceptionInformation[0] == 1);
    CHECK(Info->ExceptionRecord->ExceptionInformation[1] == 0x42);
    CHECK(Local == 37);
    Log(Decision == EXCEPTION_CONTINUE_SEARCH ? 1 : 2);
    DbgPrint("SEH: filter decision=%ld local=%ld pc=%p\n", Decision, Local,
             Info->ExceptionRecord->ExceptionAddress);
    return Decision;
}

__declspec(noinline) static VOID Cleanup(BOOLEAN Abnormal)
{
    CHECK(Abnormal);
    Log(3);
    DbgPrint("SEH: finally abnormal=%u\n", Abnormal);
    if (Collide) { Collide = FALSE; Fault(); CHECK(FALSE); }
}

__declspec(noinline) static VOID Selected(VOID)
{
    volatile LONG Local = 37;
    __try {
        __try {
            __try { Fault(); CHECK(FALSE); }
            __finally { Cleanup(_abnormal_termination()); }
        } __except(Filter(_exception_info(), Local, EXCEPTION_CONTINUE_SEARCH)) {
            CHECK(FALSE);
        }
    } __except(Filter(_exception_info(), Local, EXCEPTION_EXECUTE_HANDLER)) {
        Log(4);
        CHECK(Local == 37);
        DbgPrint("SEH: selected landing pad\n");
    }
}

static LONG ContinueFilter(PEXCEPTION_POINTERS Info)
{
    CHECK(Info->ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION);
    Info->ContextRecord->Pc = ResumePc;
    return EXCEPTION_CONTINUE_EXECUTION;
}

__declspec(noinline) static VOID Continued(VOID)
{
    __try {
        ResumePc = (ULONG_PTR)&&Resume;
        *(volatile ULONG *)(ULONG_PTR)0x42 = 1;
Resume:
        Log(5);
    } __except(ContinueFilter(_exception_info())) { CHECK(FALSE); }
}

static VOID RejectMalformed(VOID)
{
    CONTEXT Context, Before;
    ULONG64 Image;
    PRUNTIME_FUNCTION Entry;
    RUNTIME_FUNCTION Bad;
    PVOID Inaccessible = NULL;
    SIZE_T Length = 4096;
    NTSTATUS Status;
    RtlCaptureContext(&Context);
    Entry = RtlLookupFunctionEntry(Context.Pc, &Image, NULL);
    CHECK(Entry != NULL);
    if (!Entry) return;
    Before = Context;
    Context.Sp = 0;
    Status = RtlVirtualUnwind2(0, Image, Context.Pc, Entry, &Context,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
    CHECK(Status == STATUS_BAD_STACK && Context.Sp == 0 && Context.Pc == Before.Pc);
    Context = Before;
    Bad = *Entry;
    Bad.UnwindData = 0xfffffffc;
    Status = RtlVirtualUnwind2(0, Image, Context.Pc, &Bad, &Context,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
    CHECK(Status == STATUS_BAD_FUNCTION_TABLE &&
          RtlCompareMemory(&Context, &Before, sizeof(Context)) == sizeof(Context));
    CHECK(NtAllocateVirtualMemory(NtCurrentProcess(), &Inaccessible, 0, &Length,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS) == STATUS_SUCCESS);
    if (Inaccessible) {
        Status = RtlVirtualUnwind2(0, Image, Context.Pc, Inaccessible, &Context,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0);
        CHECK(Status == STATUS_BAD_FUNCTION_TABLE &&
              RtlCompareMemory(&Context, &Before, sizeof(Context)) == sizeof(Context));
        Length = 0;
        CHECK(NtFreeVirtualMemory(NtCurrentProcess(), &Inaccessible, &Length, MEM_RELEASE) == STATUS_SUCCESS);
    }
}

static VOID SoftwareFirstChance(VOID)
{
    EXCEPTION_RECORD Record = {0};
    CONTEXT Context;
    volatile LONG Landing = 0;
    Record.ExceptionCode = 0xe1234567;
    __try {
        RtlCaptureContext(&Context);
        Context.Pc = (ULONG_PTR)&&Raised;
        Context.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
        NtRaiseException(&Record, &Context, TRUE);
        CHECK(FALSE);
Raised:
        Landing = 1;
        CHECK(FALSE);
    } __except(EXCEPTION_EXECUTE_HANDLER) { Landing = 2; }
    CHECK(Landing == 2);
}

ULONG RunSehTests(VOID)
{
    ULONG i;
    EXCEPTION_RECORD Record = {0};
    CONTEXT Context;
    RejectMalformed();
    SoftwareFirstChance();
    Selected();
    CHECK(Used == 4 && Order[0] == 1 && Order[1] == 2 && Order[2] == 3 && Order[3] == 4);
    Used = 0;
    Collide = TRUE;
    Selected();
    CHECK(!Collide && Used == 6 && Order[0] == 1 && Order[1] == 2 &&
          Order[2] == 3 && Order[3] == 1 && Order[4] == 2 && Order[5] == 4);
    for (i = 0; i < Used; i++) DbgPrint("SEH: cleanup-order[%lu]=%lu\n", i, Order[i]);
    Used = 0;
    Continued();
    CHECK(Used == 1 && Order[0] == 5);
    __try { Log(6); }
    __finally { CHECK(!_abnormal_termination()); Log(7); }
    CHECK(Used == 3 && Order[1] == 6 && Order[2] == 7);
    CHECK(NtRaiseException(NULL, NULL, TRUE) == STATUS_ACCESS_VIOLATION);
    RtlCaptureContext(&Context);
    Record.NumberParameters = EXCEPTION_MAXIMUM_PARAMETERS + 1;
    CHECK(NtRaiseException(&Record, &Context, TRUE) == STATUS_INVALID_PARAMETER);
    DbgPrint("SEH: checks=%lu failures=%lu\n", Checks, Failures);
    return Failures;
}
