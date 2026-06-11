/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 unwind / SEH layout
 *
 * Validates IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY (= RUNTIME_FUNCTION),
 * RUNTIME_FUNCTION_INDIRECT, DISPATCHER_CONTEXT layout, scope-table
 * shape and unwind-history-table fields used by the SEH unwinder.
 */

#include <kmt_test.h>

/*
 * The ARM64 ntddk.h chain does not pull in DISPATCHER_CONTEXT / RUNTIME_FUNCTION
 * or the UNWIND_HISTORY_TABLE typedefs (those live under #ifdef _M_AMD64 in the
 * NDK and in xdk/winnt.h for user-mode). Define what we need locally so the
 * layout test can validate the ARM64 .pdata / .xdata / SEH ABI without
 * depending on the include-chain choices.
 */
#ifdef _M_ARM64

typedef struct _ARM64_RUNTIME_FN_ENTRY {
    ULONG BeginAddress;
    union {
        ULONG UnwindData;
        struct {
            ULONG Flag : 2;
            ULONG FunctionLength : 11;
            ULONG RegF : 3;
            ULONG RegI : 4;
            ULONG H : 1;
            ULONG CR : 2;
            ULONG FrameSize : 9;
        };
    };
} ARM64_RUNTIME_FN_ENTRY;

typedef union _ARM64_RUNTIME_FN_XDATA {
    ULONG HeaderData;
    struct {
        ULONG FunctionLength : 18;
        ULONG Version : 2;
        ULONG ExceptionDataPresent : 1;
        ULONG EpilogInHeader : 1;
        ULONG EpilogCount : 5;
        ULONG CodeWords : 5;
    };
} ARM64_RUNTIME_FN_XDATA;

typedef struct _ARM64_UNWIND_HISTORY_TABLE_ENTRY
{
    ULONG64 ImageBase;
    ARM64_RUNTIME_FN_ENTRY *FunctionEntry;
} ARM64_UNWIND_HISTORY_TABLE_ENTRY;

#define ARM64_UNWIND_HISTORY_TABLE_SIZE 12

typedef struct _ARM64_UNWIND_HISTORY_TABLE
{
    ULONG Count;
    UCHAR LocalHint;
    UCHAR GlobalHint;
    UCHAR Search;
    UCHAR Once;
    ULONG64 LowAddress;
    ULONG64 HighAddress;
    ARM64_UNWIND_HISTORY_TABLE_ENTRY Entry[ARM64_UNWIND_HISTORY_TABLE_SIZE];
} ARM64_UNWIND_HISTORY_TABLE;

typedef struct _ARM64_DISPATCHER_CONTEXT {
    ULONG_PTR ControlPc;
    ULONG_PTR ImageBase;
    ARM64_RUNTIME_FN_ENTRY *FunctionEntry;
    ULONG_PTR EstablisherFrame;
    ULONG_PTR TargetPc;
    void *ContextRecord;
    void *LanguageHandler;
    void *HandlerData;
    ARM64_UNWIND_HISTORY_TABLE *HistoryTable;
    ULONG ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    PUCHAR NonVolatileRegisters;
} ARM64_DISPATCHER_CONTEXT;

#endif /* _M_ARM64 */

VOID Test_RtlArm64UnwindLayout(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

/* Compile-time sanity. */
C_ASSERT(sizeof(ARM64_RUNTIME_FN_ENTRY) == 8);
C_ASSERT(sizeof(ARM64_RUNTIME_FN_XDATA) == 4);
C_ASSERT(sizeof(MACHINE_FRAME) == 0x10);
C_ASSERT(sizeof(ARM64_UNWIND_HISTORY_TABLE_ENTRY) == 0x10);

static VOID RtlArm64UnwindLayoutCheck(VOID)
{
    ARM64_RUNTIME_FN_ENTRY Rf = { 0 };
    ARM64_DISPATCHER_CONTEXT Ctx = { 0 };
    ARM64_UNWIND_HISTORY_TABLE Uht = { 0 };
    ARM64_RUNTIME_FN_XDATA Xdata = { 0 };

    /* ARM64 .pdata entry: 8 bytes total (BeginAddress + UnwindData). */
    ok_eq_size(sizeof(ARM64_RUNTIME_FN_ENTRY), (SIZE_T)8);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_RUNTIME_FN_ENTRY, BeginAddress),
                    0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_RUNTIME_FN_ENTRY, UnwindData),
                    4ULL);

    /* Packed bitfields share the UnwindData word. */
    Rf.UnwindData = 0;
    Rf.Flag = 3;
    ok((Rf.UnwindData & 0x3) == 3,
       "Flag bitfield mis-aligned: 0x%x\n", Rf.UnwindData);

    /* XDATA header: HeaderData word and bitfields. */
    ok_eq_size(sizeof(ARM64_RUNTIME_FN_XDATA), (SIZE_T)4);
    Xdata.FunctionLength = 1;
    ok((Xdata.HeaderData & 0x3FFFF) == 1,
       "XDATA FunctionLength misaligned: 0x%x\n", Xdata.HeaderData);

    /* ARM64_DISPATCHER_CONTEXT field offsets. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, ControlPc),
                    0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, ImageBase),
                    8ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, FunctionEntry),
                    0x10ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, EstablisherFrame),
                    0x18ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, TargetPc),
                    0x20ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, ContextRecord),
                    0x28ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, LanguageHandler),
                    0x30ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, HandlerData),
                    0x38ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, HistoryTable),
                    0x40ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_DISPATCHER_CONTEXT, ScopeIndex),
                    0x48ULL);

    /* Plain-old-data; exercise it. */
    Ctx.ControlPc = 0xDEAD;
    Ctx.ImageBase = 0xBEEF;
    Ctx.ScopeIndex = 7;
    ok_eq_ulongptr(Ctx.ControlPc, (ULONG_PTR)0xDEAD);
    ok_eq_ulongptr(Ctx.ImageBase, (ULONG_PTR)0xBEEF);
    ok_eq_ulong(Ctx.ScopeIndex, 7u);

    /* UNWIND_HISTORY_TABLE field offsets. */
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_UNWIND_HISTORY_TABLE, Count), 0ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_UNWIND_HISTORY_TABLE, LocalHint), 4ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_UNWIND_HISTORY_TABLE, GlobalHint), 5ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_UNWIND_HISTORY_TABLE, Search), 6ULL);
    ok_eq_ulonglong((ULONGLONG)FIELD_OFFSET(ARM64_UNWIND_HISTORY_TABLE, Once), 7ULL);

    Uht.Count = 3;
    Uht.LocalHint = 1;
    Uht.GlobalHint = 2;
    Uht.Search = 4;
    Uht.Once = 8;
    ok_eq_ulong(Uht.Count, 3u);
    ok_eq_uint(Uht.LocalHint, 1);
    ok_eq_uint(Uht.GlobalHint, 2);
    ok_eq_uint(Uht.Search, 4);
    ok_eq_uint(Uht.Once, 8);

    dump_trace("[arm64][RtlArm64UnwindLayout] sizeof(DISPATCHER_CONTEXT)=%Iu\n",
               (SIZE_T)sizeof(ARM64_DISPATCHER_CONTEXT));
}

#endif /* _M_ARM64 */

START_TEST(RtlArm64UnwindLayout)
{
#ifndef _M_ARM64
    skip(FALSE, "RtlArm64UnwindLayout is ARM64-only\n");
#else
    dump_trace("[arm64][RtlArm64UnwindLayout] enter\n");
    RtlArm64UnwindLayoutCheck();
#endif
}
