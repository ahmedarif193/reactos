/*
 * PROJECT:     ReactOS NTDLL ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 stubs until full ntdll arm64 is implemented
 */

#include <ntdllp.h>

void _local_unwind2(void) {}
void _global_unwind2(void) {}
int _except_handler2(void) { return 1; }
int _except_handler3(void) { return 1; }

unsigned __int64 __ull_rshift(unsigned __int64 value, int shift)
{ return value >> shift; }

unsigned __int64 __ll_rshift(unsigned __int64 value, int shift)
{ return (unsigned __int64)((long long)value >> shift); }

unsigned __int64 __ll_lshift(unsigned __int64 value, int shift)
{ return value << shift; }

EXCEPTION_DISPOSITION
__cdecl
__C_specific_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    struct _DISPATCHER_CONTEXT *DispatcherContext)
{
    (VOID)ExceptionRecord;
    (VOID)EstablisherFrame;
    (VOID)ContextRecord;
    (VOID)DispatcherContext;
    return ExceptionContinueSearch;
}

void _local_unwind(void) {}

VOID NTAPI DbgBreakPoint(VOID) { __asm__ volatile("brk #0"); }
VOID NTAPI DbgBreakPointWithStatus(ULONG Status) { (VOID)Status; __asm__ volatile("brk #0"); }
VOID NTAPI DbgUserBreakPoint(VOID) { __asm__ volatile("brk #0"); }
ULONG NTAPI DebugService(ULONG ServiceClass, PVOID Arg1, PVOID Arg2) { (VOID)ServiceClass; (VOID)Arg1; (VOID)Arg2; return 0; }
ULONG NTAPI DebugService2(ULONG ServiceClass, PVOID Arg1, PVOID Arg2) { (VOID)ServiceClass; (VOID)Arg1; (VOID)Arg2; return 0; }

double cos(double x) { (VOID)x; return 1.0; }
double fabs(double x) { return (x < 0) ? -x : x; }
double sin(double x) { (VOID)x; return 0.0; }

void __cdecl KiUserApcDispatcher(ULONG_PTR Unknown1, ULONG_PTR Unknown2, ULONG_PTR Unknown3,
                                 ULONG_PTR Context, ULONG_PTR SystemArgument1, ULONG_PTR SystemArgument2)
{
    for (;;) { __asm__ volatile("wfi"); }
}

void LdrInitializeThunk(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3, ULONG Unknown4)
{
    for (;;) { __asm__ volatile("wfi"); }
}

PSLIST_ENTRY ExpInterlockedPopEntrySListEnd(PSLIST_HEADER H) { (VOID)H; return NULL; }
PSLIST_ENTRY ExpInterlockedPopEntrySListFault(PSLIST_HEADER H) { (VOID)H; return NULL; }
PSLIST_ENTRY ExpInterlockedPopEntrySListResume(PSLIST_HEADER H) { (VOID)H; return NULL; }
