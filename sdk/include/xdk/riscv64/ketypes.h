/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS experimental RV64 context, not a Microsoft-defined Windows ABI.
 * Keep user-visible register state separate from supervisor trap state.
 * See docs/riscv64/abi-reference.md before changing this layout. */
#ifndef _RISCV64_CONTEXT_DEFINED
#define _RISCV64_CONTEXT_DEFINED

/* Port-private discriminator; no Windows RISC-V assignment is claimed. */
#define CONTEXT_RISCV64         0x00800000L
#define CONTEXT_CONTROL        (CONTEXT_RISCV64 | 0x00000001L)
#define CONTEXT_INTEGER        (CONTEXT_RISCV64 | 0x00000002L)
#define CONTEXT_FLOATING_POINT (CONTEXT_RISCV64 | 0x00000004L)
#define CONTEXT_FULL           (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#define CONTEXT_ALL            CONTEXT_FULL
#define CONTEXT_UNWOUND_TO_CALL 0x20000000

/* ExceptionInformation[0] values for STATUS_ACCESS_VIOLATION. */
#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

typedef struct DECLSPEC_ALIGN(16) _CONTEXT
{
    ULONG ContextFlags;
    ULONG Fcsr;
    ULONG64 Pc;
    union
    {
        ULONG64 X[32];
        struct
        {
            ULONG64 Zero;
            ULONG64 Ra;
            ULONG64 Sp;
            ULONG64 Gp;
            ULONG64 Tp;
            ULONG64 T0;
            ULONG64 T1;
            ULONG64 T2;
            ULONG64 S0;
            ULONG64 S1;
            ULONG64 A0;
            ULONG64 A1;
            ULONG64 A2;
            ULONG64 A3;
            ULONG64 A4;
            ULONG64 A5;
            ULONG64 A6;
            ULONG64 A7;
            ULONG64 S2;
            ULONG64 S3;
            ULONG64 S4;
            ULONG64 S5;
            ULONG64 S6;
            ULONG64 S7;
            ULONG64 S8;
            ULONG64 S9;
            ULONG64 S10;
            ULONG64 S11;
            ULONG64 T3;
            ULONG64 T4;
            ULONG64 T5;
            ULONG64 T6;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    /* Raw RV64D register bits, not C floating-point values. No vector state. */
    ULONG64 F[32];
} CONTEXT, *PCONTEXT;

C_ASSERT(sizeof(CONTEXT) == 0x210);
C_ASSERT(__alignof(CONTEXT) == 16);
C_ASSERT(FIELD_OFFSET(CONTEXT, Pc) == 0x008);
#if defined(NONAMELESSUNION)
C_ASSERT(FIELD_OFFSET(CONTEXT, u.X) == 0x010);
#else
C_ASSERT(FIELD_OFFSET(CONTEXT, X) == 0x010);
#endif
C_ASSERT(FIELD_OFFSET(CONTEXT, F) == 0x110);

#endif /* _RISCV64_CONTEXT_DEFINED */

/* ReactOS-private RVUW version 1 declarations. These types describe the
 * selected RISC-V64 PE unwind contract; they do not make the runtime usable.
 * See docs/riscv64/seh-unwind.md before changing any value or layout. */
#ifndef _RISCV64_UNWIND_TYPES_DEFINED
#define _RISCV64_UNWIND_TYPES_DEFINED

#define RISCV64_UNWIND_MAGIC                  0x57555652UL
#define RISCV64_UNWIND_VERSION                1
#define RISCV64_UNWIND_INFO_V1_SIZE           32

#define RISCV64_UNW_FLAG_NHANDLER             0x00000000UL
#define RISCV64_UNW_FLAG_EHANDLER             0x00000001UL
#define RISCV64_UNW_FLAG_UHANDLER             0x00000002UL
#define RISCV64_UNW_FLAG_CHAININFO            0x00000004UL

#define RISCV64_UNW_STATE_INTEGER             0x0001
#define RISCV64_UNW_STATE_FLOATING            0x0002
#define RISCV64_UNW_STATE_VECTOR              0x0004

#define RISCV64_UNW_OP_MASK                   0x0000003FUL
#define RISCV64_UNW_REG_MASK                  0x000007C0UL
#define RISCV64_UNW_REG_SHIFT                 6
#define RISCV64_UNW_OPERAND_SHIFT             11

#define RISCV64_UNW_OP_SET_CFA                0x01
#define RISCV64_UNW_OP_SAVE_GPR               0x02
#define RISCV64_UNW_OP_SAME_GPR               0x03
#define RISCV64_UNW_OP_GPR_FROM_GPR           0x04
#define RISCV64_UNW_OP_SAVE_FPR               0x05
#define RISCV64_UNW_OP_SAME_FPR               0x06
#define RISCV64_UNW_OP_FPR_FROM_FPR           0x07
#define RISCV64_UNW_OP_SAVE_FCSR              0x08
#define RISCV64_UNW_OP_SAME_FCSR              0x09
#define RISCV64_UNW_OP_SET_CFA_LARGE          0x10
#define RISCV64_UNW_OP_SAVE_GPR_LARGE         0x11
#define RISCV64_UNW_OP_SAVE_FPR_LARGE         0x12
#define RISCV64_UNW_OP_SAVE_FCSR_LARGE        0x13

typedef struct _IMAGE_RISCV64_RUNTIME_FUNCTION_ENTRY
{
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG UnwindData;
} IMAGE_RISCV64_RUNTIME_FUNCTION_ENTRY, *PIMAGE_RISCV64_RUNTIME_FUNCTION_ENTRY;

typedef IMAGE_RISCV64_RUNTIME_FUNCTION_ENTRY RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

#ifndef _APISETRTLSUPPORT_
#define _APISETRTLSUPPORT_

#define UNWIND_HISTORY_TABLE_SIZE 12

typedef struct _UNWIND_HISTORY_TABLE_ENTRY
{
    ULONG64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
} UNWIND_HISTORY_TABLE_ENTRY, *PUNWIND_HISTORY_TABLE_ENTRY;

typedef struct _UNWIND_HISTORY_TABLE
{
    ULONG Count;
    UCHAR LocalHint;
    UCHAR GlobalHint;
    UCHAR Search;
    UCHAR Once;
    ULONG64 LowAddress;
    ULONG64 HighAddress;
    UNWIND_HISTORY_TABLE_ENTRY Entry[UNWIND_HISTORY_TABLE_SIZE];
} UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;

#endif /* _APISETRTLSUPPORT_ */

typedef struct _RISCV64_UNWIND_INFO_V1
{
    ULONG Magic;
    USHORT Version;
    USHORT HeaderSize;
    ULONG RecordSize;
    ULONG Flags;
    ULONG PrologEndOffset;
    LONG EstablisherFrameOffset;
    USHORT PrologCodeSlots;
    USHORT EpilogScopeCount;
    USHORT EpilogCodeSlots;
    USHORT RequiredState;
} RISCV64_UNWIND_INFO_V1, *PRISCV64_UNWIND_INFO_V1;

typedef struct _RISCV64_UNWIND_CODE_V1
{
    ULONG CodeOffset;
    ULONG OpInfo;
} RISCV64_UNWIND_CODE_V1, *PRISCV64_UNWIND_CODE_V1;

typedef struct _RISCV64_UNWIND_EPILOG_SCOPE_V1
{
    ULONG StartOffset;
    ULONG EndOffset;
    USHORT FirstCodeSlot;
    USHORT CodeSlots;
    ULONG Reserved;
} RISCV64_UNWIND_EPILOG_SCOPE_V1, *PRISCV64_UNWIND_EPILOG_SCOPE_V1;

typedef struct _RISCV64_UNWIND_HANDLER_V1
{
    ULONG ExceptionHandlerRva;
    ULONG HandlerDataRva;
} RISCV64_UNWIND_HANDLER_V1, *PRISCV64_UNWIND_HANDLER_V1;

typedef struct _RISCV64_SCOPE_RECORD
{
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG HandlerAddress;
    ULONG JumpTarget;
} RISCV64_SCOPE_RECORD, *PRISCV64_SCOPE_RECORD;

typedef struct _RISCV64_SCOPE_TABLE
{
    ULONG Count;
    RISCV64_SCOPE_RECORD ScopeRecord[1];
} RISCV64_SCOPE_TABLE, *PRISCV64_SCOPE_TABLE;

typedef RISCV64_SCOPE_TABLE SCOPE_TABLE, *PSCOPE_TABLE;

#define UNW_FLAG_NHANDLER RISCV64_UNW_FLAG_NHANDLER
#define UNW_FLAG_EHANDLER RISCV64_UNW_FLAG_EHANDLER
#define UNW_FLAG_UHANDLER RISCV64_UNW_FLAG_UHANDLER
#define UNW_FLAG_CHAININFO RISCV64_UNW_FLAG_CHAININFO
#define RUNTIME_FUNCTION_INDIRECT 0x1

typedef struct _KNONVOLATILE_CONTEXT_POINTERS
{
    PULONG64 Ra;
    PULONG64 S0;
    PULONG64 S1;
    PULONG64 S2;
    PULONG64 S3;
    PULONG64 S4;
    PULONG64 S5;
    PULONG64 S6;
    PULONG64 S7;
    PULONG64 S8;
    PULONG64 S9;
    PULONG64 S10;
    PULONG64 S11;
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

#define KNONVOLATILE_CONTEXT_POINTERS_DEFINED

#define RISCV64_NONVOLATILE_CONTEXT_VERSION 1

typedef struct _RISCV64_NONVOLATILE_CONTEXT_V1
{
    ULONG Version;
    ULONG Size;
    ULONG64 S[12];
} RISCV64_NONVOLATILE_CONTEXT_V1, *PRISCV64_NONVOLATILE_CONTEXT_V1;

typedef struct _DISPATCHER_CONTEXT
{
    ULONG64 ControlPc;
    ULONG64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 EstablisherFrame;
    ULONG64 TargetPc;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    struct _UNWIND_HISTORY_TABLE *HistoryTable;
    ULONG ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    UCHAR Reserved[3];
    UCHAR *NonVolatileRegisters;
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

/* The table-based unwind entry points are part of the common 64-bit NT ABI.
 * Keep them visible from winnt.h, as they are for AMD64 and ARM64. */
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
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Inout_opt_ PUNWIND_HISTORY_TABLE HistoryTable);

NTSYSAPI
PEXCEPTION_ROUTINE
NTAPI
RtlVirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ struct _CONTEXT *ContextRecord,
    _Out_opt_ PVOID *HandlerData,
    _Out_ PULONG64 EstablisherFrame,
    _Inout_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers);

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

C_ASSERT(sizeof(IMAGE_RISCV64_RUNTIME_FUNCTION_ENTRY) == 12);
C_ASSERT(sizeof(UNWIND_HISTORY_TABLE_ENTRY) == 16);
C_ASSERT(sizeof(UNWIND_HISTORY_TABLE) == 216);
C_ASSERT(sizeof(RISCV64_UNWIND_INFO_V1) == RISCV64_UNWIND_INFO_V1_SIZE);
C_ASSERT(sizeof(RISCV64_UNWIND_CODE_V1) == 8);
C_ASSERT(sizeof(RISCV64_UNWIND_EPILOG_SCOPE_V1) == 16);
C_ASSERT(sizeof(RISCV64_UNWIND_HANDLER_V1) == 8);
C_ASSERT(sizeof(RISCV64_SCOPE_RECORD) == 16);
C_ASSERT(sizeof(KNONVOLATILE_CONTEXT_POINTERS) == 13 * sizeof(PVOID));
C_ASSERT(sizeof(RISCV64_NONVOLATILE_CONTEXT_V1) == 104);
C_ASSERT(FIELD_OFFSET(DISPATCHER_CONTEXT, NonVolatileRegisters) == 80);
C_ASSERT(sizeof(DISPATCHER_CONTEXT) == 88);

#endif /* _RISCV64_UNWIND_TYPES_DEFINED */
