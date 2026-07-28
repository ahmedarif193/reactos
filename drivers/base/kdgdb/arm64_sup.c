/*
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:         GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            drivers/base/kdgdb/arm64_sup.c
 * PURPOSE:         ARM64 register description for the GDB stub.
 */

#include "kdgdb.h"

const CHAR gdb_target_xml[] =
    "<?xml version=\"1.0\"?><!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
    "<target version=\"1.0\"><architecture>aarch64</architecture>"
    "<feature name=\"org.gnu.gdb.aarch64.core\">"
    "<reg name=\"x0\" bitsize=\"64\" type=\"int64\"/><reg name=\"x1\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x2\" bitsize=\"64\" type=\"int64\"/><reg name=\"x3\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x4\" bitsize=\"64\" type=\"int64\"/><reg name=\"x5\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x6\" bitsize=\"64\" type=\"int64\"/><reg name=\"x7\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x8\" bitsize=\"64\" type=\"int64\"/><reg name=\"x9\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x10\" bitsize=\"64\" type=\"int64\"/><reg name=\"x11\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x12\" bitsize=\"64\" type=\"int64\"/><reg name=\"x13\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x14\" bitsize=\"64\" type=\"int64\"/><reg name=\"x15\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x16\" bitsize=\"64\" type=\"int64\"/><reg name=\"x17\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x18\" bitsize=\"64\" type=\"int64\"/><reg name=\"x19\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x20\" bitsize=\"64\" type=\"int64\"/><reg name=\"x21\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x22\" bitsize=\"64\" type=\"int64\"/><reg name=\"x23\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x24\" bitsize=\"64\" type=\"int64\"/><reg name=\"x25\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x26\" bitsize=\"64\" type=\"int64\"/><reg name=\"x27\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x28\" bitsize=\"64\" type=\"int64\"/>"
    "<reg name=\"x29\" bitsize=\"64\" type=\"data_ptr\"/><reg name=\"x30\" bitsize=\"64\" type=\"code_ptr\"/>"
    "<reg name=\"sp\" bitsize=\"64\" type=\"data_ptr\"/><reg name=\"pc\" bitsize=\"64\" type=\"code_ptr\"/>"
    "<reg name=\"cpsr\" bitsize=\"32\" type=\"uint32\"/>"
    "</feature><feature name=\"org.gnu.gdb.aarch64.fpu\">"
    "<reg name=\"v0\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v1\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v2\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v3\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v4\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v5\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v6\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v7\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v8\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v9\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v10\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v11\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v12\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v13\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v14\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v15\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v16\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v17\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v18\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v19\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v20\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v21\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v22\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v23\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v24\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v25\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v26\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v27\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v28\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v29\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"v30\" bitsize=\"128\" type=\"uint128\"/><reg name=\"v31\" bitsize=\"128\" type=\"uint128\"/>"
    "<reg name=\"fpsr\" bitsize=\"32\" type=\"uint32\"/><reg name=\"fpcr\" bitsize=\"32\" type=\"uint32\"/>"
    "</feature></target>";
const SIZE_T gdb_target_xml_length = sizeof(gdb_target_xml) - 1;

/* Must match the order of gdb_target_xml above */
enum reg_name
{
    X0, X1, X2, X3, X4, X5, X6, X7,
    X8, X9, X10, X11, X12, X13, X14, X15,
    X16, X17, X18, X19, X20, X21, X22, X23,
    X24, X25, X26, X27, X28, X29, X30,
    SP,
    PC,
    CPSR,
    V0, V1, V2, V3, V4, V5, V6, V7,
    V8, V9, V10, V11, V12, V13, V14, V15,
    V16, V17, V18, V19, V20, V21, V22, V23,
    V24, V25, V26, V27, V28, V29, V30, V31,
    FPSR, FPCR
};

const UCHAR gdb_reg_size[] =
{
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8,
    8,
    8,
    4,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16,
    4, 4
};
const ULONG gdb_reg_count = RTL_NUMBER_OF(gdb_reg_size);

/*
 * Every ARM64 register is stored in the CONTEXT at exactly the width GDB
 * expects, so each one can be handed out by address and ScalarValue is unused.
 */
const void*
gdb_ctx_to_reg(
    _In_ CONTEXT* ctx,
    _In_ ULONG Register,
    _Out_ PULONG ScalarValue)
{
    enum reg_name name = (enum reg_name)Register;

    UNREFERENCED_PARAMETER(ScalarValue);

    if (name >= X0 && name <= X30)
        return &ctx->X[name - X0];

    if (name >= V0 && name <= V31)
        return &ctx->V[name - V0];

    switch (name)
    {
        case SP: return &ctx->Sp;
        case PC: return &ctx->Pc;
        case CPSR: return &ctx->Cpsr;
        case FPSR: return &ctx->Fpsr;
        case FPCR: return &ctx->Fpcr;
        default: return NULL;
    }
}

BOOLEAN
gdb_set_ctx_reg(
    _Inout_ CONTEXT* Context,
    _In_ ULONG Register,
    _In_reads_bytes_(Size) const UCHAR* Value,
    _In_ SIZE_T Size)
{
    ULONG ScalarValue;
    PVOID Storage;

    if (Register >= gdb_reg_count || Size != gdb_reg_size[Register])
        return FALSE;

    Storage = (PVOID)gdb_ctx_to_reg(Context, Register, &ScalarValue);
    if (Storage == NULL)
        return FALSE;

    RtlCopyMemory(Storage, Value, Size);
    return TRUE;
}

const void*
gdb_thread_to_reg(
    _In_ PETHREAD Thread,
    _In_ ULONG Register)
{
    enum reg_name reg_name = (enum reg_name)Register;
    static const void* NullValue = NULL;

    if (!Thread->Tcb.InitialStack)
    {
        /* Terminated thread? */
        switch (reg_name)
        {
            case SP:
            case X29:
            case PC:
                KDDBGPRINT("Returning NULL for register %d.\n", reg_name);
                return &NullValue;
            default:
                return NULL;
        }
    }

    /*
     * The thread is switched out. Only its stack pointer can be recovered
     * without decoding the switch frame; the rest is reported unavailable.
     */
    if (reg_name == SP)
        return &Thread->Tcb.KernelStack;

    return NULL;
}
