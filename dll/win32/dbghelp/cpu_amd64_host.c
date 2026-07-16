/*
 * PROJECT:     ReactOS host dbghelp
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Minimal AMD64 CPU description for host symbol conversion
 */

#include "dbghelp_private.h"

static unsigned
amd64_map_dwarf_register(unsigned regno, const struct module* module, BOOL eh_frame)
{
    (void)module;
    (void)eh_frame;

    if (regno >= 17 && regno <= 24)
        return CV_AMD64_XMM0 + regno - 17;
    if (regno >= 25 && regno <= 32)
        return CV_AMD64_XMM8 + regno - 25;
    if (regno >= 33 && regno <= 40)
        return CV_AMD64_ST0 + regno - 33;

    switch (regno)
    {
    case  0: return CV_AMD64_RAX;
    case  1: return CV_AMD64_RDX;
    case  2: return CV_AMD64_RCX;
    case  3: return CV_AMD64_RBX;
    case  4: return CV_AMD64_RSI;
    case  5: return CV_AMD64_RDI;
    case  6: return CV_AMD64_RBP;
    case  7: return CV_AMD64_RSP;
    case  8: return CV_AMD64_R8;
    case  9: return CV_AMD64_R9;
    case 10: return CV_AMD64_R10;
    case 11: return CV_AMD64_R11;
    case 12: return CV_AMD64_R12;
    case 13: return CV_AMD64_R13;
    case 14: return CV_AMD64_R14;
    case 15: return CV_AMD64_R15;
    case 16: return CV_AMD64_RIP;
    case 49: return CV_AMD64_EFLAGS;
    case 50: return CV_AMD64_ES;
    case 51: return CV_AMD64_CS;
    case 52: return CV_AMD64_SS;
    case 53: return CV_AMD64_DS;
    case 54: return CV_AMD64_FS;
    case 55: return CV_AMD64_GS;
    case 62: return CV_AMD64_TR;
    case 63: return CV_AMD64_LDTR;
    case 64: return CV_AMD64_MXCSR;
    case 65: return CV_AMD64_CTRL;
    case 66: return CV_AMD64_STAT;
    default:
        return 0;
    }
}

static void*
amd64_fetch_context_reg(union ctx* ctx, unsigned regno, unsigned* size)
{
    (void)ctx;
    (void)regno;
    (void)size;
    return NULL;
}

static const char*
amd64_fetch_regname(unsigned regno)
{
    switch (regno)
    {
    case CV_AMD64_RAX: return "rax";
    case CV_AMD64_RDX: return "rdx";
    case CV_AMD64_RCX: return "rcx";
    case CV_AMD64_RBX: return "rbx";
    case CV_AMD64_RSI: return "rsi";
    case CV_AMD64_RDI: return "rdi";
    case CV_AMD64_RBP: return "rbp";
    case CV_AMD64_RSP: return "rsp";
    case CV_AMD64_R8:  return "r8";
    case CV_AMD64_R9:  return "r9";
    case CV_AMD64_R10: return "r10";
    case CV_AMD64_R11: return "r11";
    case CV_AMD64_R12: return "r12";
    case CV_AMD64_R13: return "r13";
    case CV_AMD64_R14: return "r14";
    case CV_AMD64_R15: return "r15";
    case CV_AMD64_RIP: return "rip";
    default:            return "unknown";
    }
}

DECLSPEC_HIDDEN struct cpu cpu_x86_64 =
{
    IMAGE_FILE_MACHINE_AMD64,
    8,
    CV_AMD64_RSP,
    NULL,
    NULL,
    NULL,
    amd64_map_dwarf_register,
    amd64_fetch_context_reg,
    amd64_fetch_regname,
    NULL,
    NULL,
};
