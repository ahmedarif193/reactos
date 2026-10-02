/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC context and table-based unwind types
 */

/* The Windows NT PowerPC CONTEXT layout (NT 3.51/4.0 ntppc.h). */
#ifndef _PPC_CONTEXT_DEFINED
#define _PPC_CONTEXT_DEFINED

#define CONTEXT_CONTROL         0x00000001L
#define CONTEXT_FLOATING_POINT  0x00000002L
#define CONTEXT_INTEGER         0x00000004L
#define CONTEXT_DEBUG_REGISTERS 0x00000008L
#define CONTEXT_FULL            (CONTEXT_CONTROL | CONTEXT_FLOATING_POINT | CONTEXT_INTEGER)
#define CONTEXT_ALL             (CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS)
#define CONTEXT_UNWOUND_TO_CALL 0x20000000

/* ExceptionInformation[0] values for STATUS_ACCESS_VIOLATION. */
#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

typedef struct DECLSPEC_ALIGN(8) _CONTEXT
{
    double Fpr0;
    double Fpr1;
    double Fpr2;
    double Fpr3;
    double Fpr4;
    double Fpr5;
    double Fpr6;
    double Fpr7;
    double Fpr8;
    double Fpr9;
    double Fpr10;
    double Fpr11;
    double Fpr12;
    double Fpr13;
    double Fpr14;
    double Fpr15;
    double Fpr16;
    double Fpr17;
    double Fpr18;
    double Fpr19;
    double Fpr20;
    double Fpr21;
    double Fpr22;
    double Fpr23;
    double Fpr24;
    double Fpr25;
    double Fpr26;
    double Fpr27;
    double Fpr28;
    double Fpr29;
    double Fpr30;
    double Fpr31;
    double Fpscr;
    ULONG Gpr0;
    ULONG Gpr1;
    ULONG Gpr2;
    ULONG Gpr3;
    ULONG Gpr4;
    ULONG Gpr5;
    ULONG Gpr6;
    ULONG Gpr7;
    ULONG Gpr8;
    ULONG Gpr9;
    ULONG Gpr10;
    ULONG Gpr11;
    ULONG Gpr12;
    ULONG Gpr13;
    ULONG Gpr14;
    ULONG Gpr15;
    ULONG Gpr16;
    ULONG Gpr17;
    ULONG Gpr18;
    ULONG Gpr19;
    ULONG Gpr20;
    ULONG Gpr21;
    ULONG Gpr22;
    ULONG Gpr23;
    ULONG Gpr24;
    ULONG Gpr25;
    ULONG Gpr26;
    ULONG Gpr27;
    ULONG Gpr28;
    ULONG Gpr29;
    ULONG Gpr30;
    ULONG Gpr31;
    ULONG Cr;
    ULONG Xer;
    ULONG Msr;
    ULONG Iar;
    ULONG Lr;
    ULONG Ctr;
    ULONG ContextFlags;
    ULONG Fill[3];
    ULONG Dr0;
    ULONG Dr1;
    ULONG Dr2;
    ULONG Dr3;
    ULONG Dr4;
    ULONG Dr5;
    ULONG Dr6;
    ULONG Dr7;
} CONTEXT, *PCONTEXT;

C_ASSERT(FIELD_OFFSET(CONTEXT, Fpscr) == 0x100);
C_ASSERT(FIELD_OFFSET(CONTEXT, Gpr0) == 0x108);
C_ASSERT(FIELD_OFFSET(CONTEXT, Cr) == 0x188);
C_ASSERT(FIELD_OFFSET(CONTEXT, Iar) == 0x194);
C_ASSERT(FIELD_OFFSET(CONTEXT, ContextFlags) == 0x1A0);
C_ASSERT(sizeof(CONTEXT) == 0x1D0);

#endif /* _PPC_CONTEXT_DEFINED */

#ifndef _PPC_UNWIND_TYPES_DEFINED
#define _PPC_UNWIND_TYPES_DEFINED

struct _EXCEPTION_RECORD;
struct _CONTEXT;

/* Function table entry. All five words are virtual addresses (the linker
 * emits base relocations for them). The unwinder decodes the prologue that
 * ends at PrologEndAddress; there are no unwind codes. */
typedef struct _IMAGE_PPC_RUNTIME_FUNCTION_ENTRY
{
    ULONG BeginAddress;
    ULONG EndAddress;
    PEXCEPTION_ROUTINE ExceptionHandler;
    PVOID HandlerData;
    ULONG PrologEndAddress;
} IMAGE_PPC_RUNTIME_FUNCTION_ENTRY, *PIMAGE_PPC_RUNTIME_FUNCTION_ENTRY;

typedef IMAGE_PPC_RUNTIME_FUNCTION_ENTRY RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

#ifndef _APISETRTLSUPPORT_
#define _APISETRTLSUPPORT_

#define UNWIND_HISTORY_TABLE_SIZE 12

typedef struct _UNWIND_HISTORY_TABLE_ENTRY
{
    ULONG_PTR ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
} UNWIND_HISTORY_TABLE_ENTRY, *PUNWIND_HISTORY_TABLE_ENTRY;

typedef struct _UNWIND_HISTORY_TABLE
{
    ULONG Count;
    UCHAR LocalHint;
    UCHAR GlobalHint;
    UCHAR Search;
    UCHAR Once;
    ULONG_PTR LowAddress;
    ULONG_PTR HighAddress;
    UNWIND_HISTORY_TABLE_ENTRY Entry[UNWIND_HISTORY_TABLE_SIZE];
} UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;

#endif /* _APISETRTLSUPPORT_ */

/* __C_specific_handler scope table. All fields are virtual addresses:
 * HandlerAddress is the code entry of the filter or of the termination
 * funclet (1 for EXCEPTION_EXECUTE_HANDLER), JumpTarget the __except block
 * (0 for a termination handler). Filters and funclets are entered with r2 set
 * to the establisher frame, from which they reload the parent's TOC. */
typedef struct _PPC_SCOPE_RECORD
{
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG HandlerAddress;
    ULONG JumpTarget;
} PPC_SCOPE_RECORD, *PPPC_SCOPE_RECORD;

typedef struct _SCOPE_TABLE_PPC
{
    ULONG Count;
    PPC_SCOPE_RECORD ScopeRecord[1];
} SCOPE_TABLE_PPC, *PSCOPE_TABLE_PPC;

typedef SCOPE_TABLE_PPC SCOPE_TABLE, *PSCOPE_TABLE;

#define UNW_FLAG_NHANDLER 0x0
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#define UNW_FLAG_CHAININFO 0x4
#define RUNTIME_FUNCTION_INDIRECT 0x1

typedef struct _KNONVOLATILE_CONTEXT_POINTERS
{
    PULONGLONG FloatingContext14;
    PULONGLONG FloatingContext15;
    PULONGLONG FloatingContext16;
    PULONGLONG FloatingContext17;
    PULONGLONG FloatingContext18;
    PULONGLONG FloatingContext19;
    PULONGLONG FloatingContext20;
    PULONGLONG FloatingContext21;
    PULONGLONG FloatingContext22;
    PULONGLONG FloatingContext23;
    PULONGLONG FloatingContext24;
    PULONGLONG FloatingContext25;
    PULONGLONG FloatingContext26;
    PULONGLONG FloatingContext27;
    PULONGLONG FloatingContext28;
    PULONGLONG FloatingContext29;
    PULONGLONG FloatingContext30;
    PULONGLONG FloatingContext31;
    PULONG IntegerContext14;
    PULONG IntegerContext15;
    PULONG IntegerContext16;
    PULONG IntegerContext17;
    PULONG IntegerContext18;
    PULONG IntegerContext19;
    PULONG IntegerContext20;
    PULONG IntegerContext21;
    PULONG IntegerContext22;
    PULONG IntegerContext23;
    PULONG IntegerContext24;
    PULONG IntegerContext25;
    PULONG IntegerContext26;
    PULONG IntegerContext27;
    PULONG IntegerContext28;
    PULONG IntegerContext29;
    PULONG IntegerContext30;
    PULONG IntegerContext31;
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

#define KNONVOLATILE_CONTEXT_POINTERS_DEFINED

typedef struct _DISPATCHER_CONTEXT
{
    ULONG_PTR ControlPc;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR EstablisherFrame;
    PCONTEXT ContextRecord;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

struct _EXCEPTION_POINTERS;

typedef LONG
(*PEXCEPTION_FILTER)(
    struct _EXCEPTION_POINTERS *ExceptionPointers,
    PVOID EstablisherFrame);

typedef VOID
(*PTERMINATION_HANDLER)(
    BOOLEAN AbnormalTermination,
    PVOID EstablisherFrame);

NTSYSAPI
VOID
NTAPI
RtlRestoreContext(
    _In_ struct _CONTEXT *ContextRecord,
    _In_opt_ struct _EXCEPTION_RECORD *ExceptionRecord);

NTSYSAPI
PRUNTIME_FUNCTION
NTAPI
RtlLookupFunctionEntry(
    _In_ ULONG_PTR ControlPc);

NTSYSAPI
ULONG
NTAPI
RtlVirtualUnwind(
    _In_ ULONG_PTR ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ struct _CONTEXT *ContextRecord,
    _Out_ PBOOLEAN InFunction,
    _Out_ PULONG EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _In_ ULONG LowStackLimit,
    _In_ ULONG HighStackLimit);

NTSYSAPI
VOID
NTAPI
RtlUnwindEx(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ struct _EXCEPTION_RECORD *ExceptionRecord,
    _In_ PVOID ReturnValue,
    _In_ struct _CONTEXT *ContextRecord,
    _In_opt_ PUNWIND_HISTORY_TABLE HistoryTable);

C_ASSERT(sizeof(IMAGE_PPC_RUNTIME_FUNCTION_ENTRY) == 20);
C_ASSERT(sizeof(PPC_SCOPE_RECORD) == 16);
C_ASSERT(sizeof(KNONVOLATILE_CONTEXT_POINTERS) == 36 * sizeof(PVOID));
C_ASSERT(sizeof(DISPATCHER_CONTEXT) == 16);
C_ASSERT(FIELD_OFFSET(DISPATCHER_CONTEXT, FunctionEntry) == 4);
C_ASSERT(FIELD_OFFSET(DISPATCHER_CONTEXT, EstablisherFrame) == 8);
C_ASSERT(FIELD_OFFSET(DISPATCHER_CONTEXT, ContextRecord) == 12);

#endif /* _PPC_UNWIND_TYPES_DEFINED */
