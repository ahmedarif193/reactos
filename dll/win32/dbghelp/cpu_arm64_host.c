/*
 * PROJECT:     ReactOS host dbghelp
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Minimal ARM64 CPU description for host symbol conversion
 */

#include "dbghelp_private.h"

static unsigned
arm64_map_dwarf_register(unsigned regno, const struct module* module, BOOL eh_frame)
{
    (void)module;
    (void)eh_frame;

    if (regno <= 30)
        return CV_ARM64_X0 + regno;
    if (regno == 31)
        return CV_ARM64_SP;
    return CV_ARM64_NOREG;
}

static void*
arm64_fetch_context_reg(union ctx* ctx, unsigned regno, unsigned* size)
{
    (void)ctx;
    (void)regno;
    (void)size;
    return NULL;
}

static const char*
arm64_fetch_regname(unsigned regno)
{
    static const char* const IntegerRegisters[] =
    {
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "fp", "lr"
    };

    if (regno >= CV_ARM64_X0 && regno <= CV_ARM64_X0 + 30)
        return IntegerRegisters[regno - CV_ARM64_X0];

    switch (regno)
    {
    case CV_ARM64_SP:     return "sp";
    case CV_ARM64_PC:     return "pc";
    case CV_ARM64_PSTATE: return "cpsr";
    default:              return "unknown";
    }
}

DECLSPEC_HIDDEN struct cpu cpu_arm64 =
{
    IMAGE_FILE_MACHINE_ARM64,
    8,
    CV_ARM64_X0 + 29,
    NULL,
    NULL,
    NULL,
    arm64_map_dwarf_register,
    arm64_fetch_context_reg,
    arm64_fetch_regname,
    NULL,
    NULL,
};
