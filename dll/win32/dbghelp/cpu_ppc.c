/*
 * PROJECT:     LiberNT DbgHelp
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC stack walking through the back chain
 *
 * Every non-leaf frame stores the caller's r1 at 0(r1); the caller's LR
 * save slot is 8 bytes into the caller's frame.
 */

#include "dbghelp_private.h"
#include "winerror.h"

static BOOL
ppc_get_addr(HANDLE Thread, const CONTEXT *Context, enum cpu_addr AddressKind, ADDRESS64 *Address)
{
    (void)Thread;

    if (!Context || !Address)
        return FALSE;

    Address->Mode = AddrModeFlat;
    Address->Segment = 0;
    switch (AddressKind)
    {
        case cpu_addr_pc:
            Address->Offset = Context->Iar;
            return TRUE;
        case cpu_addr_stack:
        case cpu_addr_frame:
            Address->Offset = Context->Gpr1;
            return TRUE;
    }
    Address->Mode = -1;
    return FALSE;
}

static BOOL
ppc_next_frame(struct cpu_stack_walk *Walk, CONTEXT *Context)
{
    DWORD Caller, Return;

    if (!Context->Gpr1 || (Context->Gpr1 & 3) || !sw_read_mem(Walk, Context->Gpr1, &Caller, sizeof(Caller)) || (Caller <= Context->Gpr1) || !sw_read_mem(Walk, (DWORD64)Caller + 8, &Return, sizeof(Return)))
        return FALSE;
    Context->Gpr1 = Caller;
    Context->Iar = Return;
    return Return != 0;
}

static BOOL
ppc_stack_walk(struct cpu_stack_walk *Walk, STACKFRAME64 *Frame, union ctx *Context)
{
    CONTEXT Next;

    if (!Context || (Frame->Reserved[0] == 2))
        return FALSE;
    if (Frame->Reserved[0] && !ppc_next_frame(Walk, &Context->ctx))
    {
        Frame->Reserved[0] = 2;
        return FALSE;
    }
    Frame->Reserved[0] = 1;
    Frame->AddrPC.Mode = Frame->AddrStack.Mode = Frame->AddrReturn.Mode = Frame->AddrFrame.Mode = AddrModeFlat;
    Frame->AddrPC.Offset = Context->ctx.Iar;
    Frame->AddrStack.Offset = Frame->AddrFrame.Offset = Context->ctx.Gpr1;
    Next = Context->ctx;
    Frame->AddrReturn.Offset = ppc_next_frame(Walk, &Next) ? Next.Iar : 0;
    Frame->FuncTableEntry = NULL;
    memset(Frame->Params, 0, sizeof(Frame->Params));
    Frame->Far = Frame->Virtual = TRUE;
    return Frame->AddrPC.Offset != 0;
}

/* Internal identifiers only, not claimed CodeView register assignments. */
#define PPC_DWARF_REGISTER_BASE 0x10000

static unsigned
ppc_map_dwarf_register(unsigned Register, const struct module *Module, BOOL EhFrame)
{
    (void)Module;
    (void)EhFrame;
    return Register < 64 ? PPC_DWARF_REGISTER_BASE + Register : CV_REG_NONE;
}

static void *
ppc_fetch_context_register(union ctx *Context, unsigned Register, unsigned *Size)
{
    if (Size)
        *Size = 0;
    if (!Context || !Size || (Register < PPC_DWARF_REGISTER_BASE) || (Register >= PPC_DWARF_REGISTER_BASE + 64))
        return NULL;
    Register -= PPC_DWARF_REGISTER_BASE;
    if (Register < 32)
    {
        *Size = sizeof(DWORD);
        return &(&Context->ctx.Gpr0)[Register];
    }
    *Size = sizeof(double);
    return &(&Context->ctx.Fpr0)[Register - 32];
}

static const char *
ppc_fetch_register_name(unsigned Register)
{
    static char Name[8];

    if ((Register < PPC_DWARF_REGISTER_BASE) || (Register >= PPC_DWARF_REGISTER_BASE + 64))
        return "unavailable";
    Register -= PPC_DWARF_REGISTER_BASE;
    snprintf(Name, sizeof(Name), "%c%u", Register < 32 ? 'r' : 'f', Register & 31);
    return Name;
}

static BOOL
ppc_fetch_minidump_thread(struct dump_context *Dump, unsigned Index, unsigned Flags, const CONTEXT *Context)
{
    (void)Index;
    if (Context && Context->ContextFlags && (Flags & ThreadWriteInstructionWindow))
        minidump_add_memory_block(Dump, Context->Iar & ~(DWORD64)0xfff, 0x1000, 0);
    return TRUE;
}

static BOOL
ppc_fetch_minidump_module(struct dump_context *Dump, unsigned Index, unsigned Flags)
{
    (void)Dump;
    (void)Index;
    (void)Flags;
    return FALSE;
}

struct cpu cpu_ppc =
{
    IMAGE_FILE_MACHINE_POWERPC,
    4,
    CV_REG_NONE,
    ppc_get_addr,
    ppc_stack_walk,
    NULL,
    ppc_map_dwarf_register,
    ppc_fetch_context_register,
    ppc_fetch_register_name,
    ppc_fetch_minidump_thread,
    ppc_fetch_minidump_module,
};
