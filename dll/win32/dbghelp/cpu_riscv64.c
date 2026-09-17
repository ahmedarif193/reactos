/*
 * PROJECT:     ReactOS DbgHelp
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 CPU description without an unwind implementation
 */

#include "dbghelp_private.h"
#include "winerror.h"

static BOOL
riscv64_get_addr(HANDLE Thread, const CONTEXT *Context,
                 enum cpu_addr AddressKind, ADDRESS64 *Address)
{
    (void)Thread;

    if (!Context || !Address)
        return FALSE;

    Address->Mode = AddrModeFlat;
    Address->Segment = 0;

    switch (AddressKind)
    {
        case cpu_addr_pc:
            Address->Offset = Context->Pc;
            return TRUE;

        case cpu_addr_stack:
            Address->Offset = Context->Sp;
            return TRUE;

        case cpu_addr_frame:
            /* s0 is optional and RVUW does not define a frame chain. */
            break;
    }

    Address->Mode = -1;
    return FALSE;
}

static BOOL
riscv64_stack_walk(struct cpu_stack_walk *Walk, STACKFRAME64 *Frame,
                   union ctx *Context)
{
    (void)Walk;
    (void)Frame;
    (void)Context;

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

static unsigned
riscv64_map_dwarf_register(unsigned Register, const struct module *Module,
                           BOOL EhFrame)
{
    (void)Register;
    (void)Module;
    (void)EhFrame;

    /* The selected CodeView definitions assign no RISC-V register numbers. */
    return CV_REG_NONE;
}

static void *
riscv64_fetch_context_register(union ctx *Context, unsigned Register,
                               unsigned *Size)
{
    (void)Context;
    (void)Register;

    if (Size)
        *Size = 0;
    return NULL;
}

static const char *
riscv64_fetch_register_name(unsigned Register)
{
    (void)Register;
    return "unavailable";
}

static BOOL
riscv64_fetch_minidump_thread(struct dump_context *Dump, unsigned Index,
                              unsigned Flags, const CONTEXT *Context)
{
    (void)Dump;
    (void)Index;
    (void)Flags;
    (void)Context;
    return FALSE;
}

static BOOL
riscv64_fetch_minidump_module(struct dump_context *Dump, unsigned Index,
                              unsigned Flags)
{
    (void)Dump;
    (void)Index;
    (void)Flags;
    return FALSE;
}

struct cpu cpu_riscv64 =
{
    IMAGE_FILE_MACHINE_RISCV64,
    8,
    CV_REG_NONE,
    riscv64_get_addr,
    riscv64_stack_walk,
    NULL,
    riscv64_map_dwarf_register,
    riscv64_fetch_context_register,
    riscv64_fetch_register_name,
    riscv64_fetch_minidump_thread,
    riscv64_fetch_minidump_module,
};
