/*
 * PROJECT:     ReactOS DbgHelp
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 stack walking through the shared RVUW decoder
 */

#include "dbghelp_private.h"
#include "winerror.h"
#include "ntstatus.h"
#include "riscv64/unwind.h"

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

static NTSTATUS NTAPI
riscv64_read_memory(PVOID Opaque, ULONG64 Address, PVOID Buffer, SIZE_T Size)
{
    if (Size > MAXDWORD || !sw_read_mem(Opaque, Address, Buffer, (DWORD)Size))
        return STATUS_PARTIAL_COPY;
    return STATUS_SUCCESS;
}

static BOOL
riscv64_next_frame(struct cpu_stack_walk *Walk, CONTEXT *Context)
{
    RTL_RISCV64_UNWIND_VIEW View = {0};
    RTL_RISCV64_UNWIND_RESULT Result;
    IMAGE_DOS_HEADER Dos;
    IMAGE_NT_HEADERS64 Nt;
    IMAGE_DATA_DIRECTORY Directory;
    RUNTIME_FUNCTION Entry, *Found = NULL;
    ULONG First, Limit, Middle;
    ULONG64 Pc = Context->Pc, Base, Rva;
    NTSTATUS Status;

    if (Context->ContextFlags & CONTEXT_UNWOUND_TO_CALL)
    {
        if (Pc < 2) return FALSE;
        Pc -= 2;
    }
    Base = sw_module_base(Walk, Pc);
    if (!Base || Base > Pc ||
        !sw_read_mem(Walk, Base, &Dos, sizeof(Dos)) ||
        Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew < sizeof(Dos) ||
        Dos.e_lfanew > 0x100000 || Base > ~(ULONG64)0 - Dos.e_lfanew ||
        !sw_read_mem(Walk, Base + Dos.e_lfanew, &Nt, sizeof(Nt)) ||
        Nt.Signature != IMAGE_NT_SIGNATURE ||
        Nt.FileHeader.Machine != IMAGE_FILE_MACHINE_RISCV64 ||
        Nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        Nt.FileHeader.SizeOfOptionalHeader < sizeof(Nt.OptionalHeader) ||
        Pc - Base >= Nt.OptionalHeader.SizeOfImage)
        return FALSE;
    View.ImageBase = Base;
    View.ImageSize = Nt.OptionalHeader.SizeOfImage;
    View.StackLow = Context->Sp;
    View.StackHigh = (1ULL << 38) - 1; /* Sv39 user address ceiling. */
    View.ReadMemory = riscv64_read_memory;
    View.ReadContext = Walk;
    if (Nt.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXCEPTION)
    {
        Directory = Nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (Directory.Size % sizeof(Entry) || Directory.VirtualAddress > View.ImageSize ||
            Directory.Size > View.ImageSize - Directory.VirtualAddress)
            return FALSE;
        Rva = Pc - Base;
        First = 0;
        Limit = Directory.Size / sizeof(Entry);
        while (First < Limit)
        {
            Middle = First + (Limit - First) / 2;
            if (!sw_read_mem(Walk, Base + Directory.VirtualAddress + Middle * sizeof(Entry),
                             &Entry, sizeof(Entry)) ||
                Entry.BeginAddress >= Entry.EndAddress || Entry.EndAddress > View.ImageSize)
                return FALSE;
            if (Rva < Entry.BeginAddress) Limit = Middle;
            else if (Rva >= Entry.EndAddress) First = Middle + 1;
            else { Found = &Entry; break; }
        }
    }
    Status = RtlpRiscv64UnwindFrame(UNW_FLAG_NHANDLER, Pc, Found, &View,
                                   Context, &Result, NULL);
    return Status >= 0 && Context->Pc != 0;
}

static BOOL
riscv64_stack_walk(struct cpu_stack_walk *Walk, STACKFRAME64 *Frame,
                   union ctx *Context)
{
    CONTEXT Next;
    if (!Context || Frame->Reserved[0] == 2) return FALSE;
    if (Frame->Reserved[0] && !riscv64_next_frame(Walk, &Context->ctx))
    {
        Frame->Reserved[0] = 2;
        return FALSE;
    }
    Frame->Reserved[0] = 1;
    Frame->AddrPC.Mode = Frame->AddrStack.Mode = Frame->AddrReturn.Mode = AddrModeFlat;
    Frame->AddrFrame.Mode = AddrModeFlat;
    Frame->AddrPC.Offset = Context->ctx.Pc;
    Frame->AddrStack.Offset = Frame->AddrFrame.Offset = Context->ctx.Sp;
    Next = Context->ctx;
    Frame->AddrReturn.Offset = riscv64_next_frame(Walk, &Next) ? Next.Pc : 0;
    Frame->FuncTableEntry = NULL;
    memset(Frame->Params, 0, sizeof(Frame->Params));
    Frame->Far = Frame->Virtual = TRUE;
    return Frame->AddrPC.Offset != 0;
}

/* Internal identifiers only, not claimed CodeView register assignments. */
#define RISCV_DWARF_REGISTER_BASE 0x10000

static unsigned
riscv64_map_dwarf_register(unsigned Register, const struct module *Module,
                           BOOL EhFrame)
{
    (void)Module;
    (void)EhFrame;
    return Register < 64 ? RISCV_DWARF_REGISTER_BASE + Register : CV_REG_NONE;
}

static void *
riscv64_fetch_context_register(union ctx *Context, unsigned Register,
                               unsigned *Size)
{
    if (Size) *Size = 0;
    if (!Context || !Size || Register < RISCV_DWARF_REGISTER_BASE ||
        Register >= RISCV_DWARF_REGISTER_BASE + 64) return NULL;
    Register -= RISCV_DWARF_REGISTER_BASE;
    *Size = sizeof(ULONG64);
    return Register < 32 ? &Context->ctx.X[Register] : &Context->ctx.F[Register - 32];
}

static const char *
riscv64_fetch_register_name(unsigned Register)
{
    static const char *Names[] = {
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3",
        "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6",
        "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11",
        "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19", "f20", "f21",
        "f22", "f23", "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31" };
    return Register >= RISCV_DWARF_REGISTER_BASE && Register < RISCV_DWARF_REGISTER_BASE + 64 ?
        Names[Register - RISCV_DWARF_REGISTER_BASE] : "unavailable";
}

static BOOL
riscv64_fetch_minidump_thread(struct dump_context *Dump, unsigned Index,
                              unsigned Flags, const CONTEXT *Context)
{
    (void)Index;
    if (Context && Context->ContextFlags && (Flags & ThreadWriteInstructionWindow))
    {
        ULONG64 Base = Context->Pc & ~(ULONG64)0xfff;
        /* Keep the instruction window within its mapped page. */
        minidump_add_memory_block(Dump, Base, 0x1000, 0);
    }
    return TRUE;
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
