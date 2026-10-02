/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Entry point of the little-endian loader started by stage0
 */

#include <freeldr.h>
#include <stdarg.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/*
 * Entered through the image entry descriptor with MSR[LE] set, r3 = the
 * stage0 handoff record and r1 = the stage0-provided loader stack.
 */
VOID
OfwEntry(_In_ PPPC_STAGE0_INFO Info)
{
    PCSTR CmdLine = "";

    if (!OfwInitialize(Info))
    {
        for (;;)
            NOTHING;
    }

    OfwConsWriteString("\nReactOS FreeLoader for PowerPC (Open Firmware)\n");

    OfwDetectMachine();
    MachInit(CmdLine);

    /* Load the default settings from the command-line */
    LoadSettings(CmdLine);

    /* Debugger pre-initialization */
    DebugInit(BootMgrInfo.DebugString);

    /* UI pre-initialization */
    if (!UiInitialize(FALSE))
    {
        UiMessageBoxCritical("Unable to initialize UI.");
        goto Quit;
    }

    /* Initialize memory manager */
    if (!MmInitializeMemoryManager())
    {
        UiMessageBoxCritical("Unable to initialize memory manager.");
        goto Quit;
    }

    /* Initialize I/O subsystem */
    FsInit();

    /* Initialize the module list */
    if (!PeLdrInitializeModuleList())
    {
        UiMessageBoxCritical("Unable to initialize module list.");
        goto Quit;
    }

    if (!MachInitializeBootDevices())
    {
        UiMessageBoxCritical("Error when detecting hardware.");
        goto Quit;
    }

    RunLoader();

Quit:
    Reboot();
}

#ifdef KeGetCurrentIrql
#undef KeGetCurrentIrql
#endif

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

/*
 * ramdisk.c probes this optional FAT write-back hook through a weak
 * reference. A PowerPC direct call binds to the "..FatFlushCache" code
 * entry, which the weak attribute does not cover, so provide the no-op.
 */
VOID
FatFlushCache(VOID)
{
}

DECLSPEC_NORETURN
VOID
FrLdrBugCheckWithMessage(
    ULONG BugCode,
    PCHAR File,
    ULONG Line,
    PCSTR Format,
    ...)
{
    CHAR Detail[320];
    CHAR Message[512];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Detail, sizeof(Detail), Format, Arguments);
    va_end(Arguments);

    RtlStringCbPrintfA(Message, sizeof(Message), "FreeLdr bug check %lu at %s:%lu\n%s", BugCode, File ? File : "<unknown>", Line, Detail);
    ERR("%s\n", Message);
    if (OfwAvailable)
        UiMessageBoxCritical(Message);
    for (;;)
        NOTHING;
}

VOID
FrLdrCheckCpuCompatibility(VOID)
{
    /* Every 32-bit PowerPC with a hashed MMU runs the NT kernel. */
}

DECLSPEC_NORETURN
VOID __cdecl Reboot(VOID)
{
    WARN("Something has gone wrong - returning to Open Firmware\n");
    if (OfwAvailable)
        OfwExit();
    for (;;)
        NOTHING;
}
