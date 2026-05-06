/*
 * PROJECT:     ReactOS NTDLL ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 stubs until full ntdll arm64 is implemented
 */

#include <ntdllp.h>

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

unsigned __int64 __ull_rshift(unsigned __int64 value, int shift)
{
    return value >> shift;
}

EXCEPTION_DISPOSITION __cdecl __C_specific_handler(
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



RUNTIME_FUNCTION *RtlLookupFunctionEntry(unsigned long long PC, unsigned long long *Base, void *History) { if(Base)*Base=0; (void)History; return 0; }
PEXCEPTION_ROUTINE RtlVirtualUnwind(ULONG Type, unsigned long long Base, unsigned long long PC, RUNTIME_FUNCTION *Entry, CONTEXT *Ctx, void **Data, unsigned long long *Frame, void *Ptrs) { (void)Type;(void)Base;(void)PC;(void)Entry;(void)Ctx;(void)Data;(void)Frame;(void)Ptrs; return 0; }
void RtlUnwindEx(void *Frame, void *IP, EXCEPTION_RECORD *Rec, void *Ret, CONTEXT *Ctx, void *History) { (void)Frame;(void)IP;(void)Rec;(void)Ret;(void)Ctx;(void)History; }
void RtlRestoreContext(CONTEXT *Ctx, EXCEPTION_RECORD *Rec) { (void)Ctx;(void)Rec; }
void RtlAddFunctionTable(RUNTIME_FUNCTION *Tbl, ULONG N, unsigned long long Base) { (void)Tbl;(void)N;(void)Base; }
void RtlDeleteFunctionTable(RUNTIME_FUNCTION *Tbl) { (void)Tbl; }
RUNTIME_FUNCTION *RtlLookupFunctionTable(unsigned long long Base, unsigned long long *Len, ULONG *Unused) { if(Len)*Len=0;(void)Unused;(void)Base; return 0; }
void _local_unwind(void) {}

PSLIST_ENTRY ExpInterlockedPopEntrySListEnd(PSLIST_HEADER H) { (void)H; return 0; }
PSLIST_ENTRY ExpInterlockedPopEntrySListFault(PSLIST_HEADER H) { (void)H; return 0; }
PSLIST_ENTRY ExpInterlockedPopEntrySListResume(PSLIST_HEADER H) { (void)H; return 0; }
