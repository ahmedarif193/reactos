/*
 * PROJECT:     LiberNT Boot Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Startup settings and diagnostic boot options
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include "ntldropts.h"

enum SAFEBOOT_MODE BootOptionChoice = NO_OPTION;
LOGICAL BootFlags;
UCHAR HALAutoDetectMode;

static ULONG StartupProfile;

static const struct
{
    PCSTR Name;
    PCSTR Options;
    PCSTR Remove;
} StartupProfiles[] =
{
    {"Boot normally", NULL, NULL},
    {"Boot with serial debugging", "DEBUG DEBUGPORT=COM1 BAUDRATE=115200", "/NODEBUG/DEBUGPORT=/BAUDRATE="},
    {"Boot with file logging", "DEBUG DEBUGPORT=FILE", "/NODEBUG/DEBUGPORT="},
    {"Boot with screen debugging", "DEBUG DEBUGPORT=SCREEN SOS", "/NODEBUG/DEBUGPORT="},
    {"Boot with one processor", "NUMPROC=1", "/NUMPROC=/ONECPU"},
    {"Boot with processor diagnostics", "DEBUG DEBUGPORT=COM1 BAUDRATE=115200 SMPDIAG", "/NODEBUG/DEBUGPORT=/BAUDRATE=/NUMPROC=/ONECPU"}
};

static PCSTR StartupModes[] =
{
    "Default",
    "Safe mode",
    "Safe mode with networking",
    "Safe mode with command prompt"
};

VOID
PrepareBootOptions(OperatingSystemItem* OperatingSystem)
{
    BootOptionChoice = OperatingSystem->StartupMode;
    BootFlags = OperatingSystem->StartupFlags;
    StartupProfile = OperatingSystem->StartupProfile < RTL_NUMBER_OF(StartupProfiles) ?
                     OperatingSystem->StartupProfile : 0;
}

static VOID
StartupDescribe(OperatingSystemItem* OperatingSystem)
{
    PCHAR Text = OperatingSystem->AdvBootOptsDesc;
    SIZE_T Size = sizeof(OperatingSystem->AdvBootOptsDesc);
    Text[0] = 0;
    if (!OperatingSystem->StartupMode && !OperatingSystem->StartupFlags)
        return;
    RtlStringCbCopyA(Text, Size, StartupModes[OperatingSystem->StartupMode]);
    if (OperatingSystem->StartupFlags & BOOT_LOGGING)
        RtlStringCbCatA(Text, Size, " | Boot log");
    if (OperatingSystem->StartupFlags & BOOT_DEBUGGING)
        RtlStringCbCatA(Text, Size, " | Debugging");
    if (OperatingSystem->StartupFlags & BOOT_VGA_MODE)
        RtlStringCbCatA(Text, Size, " | Basic video");
}

static VOID
StartupRecoveryOptions(OperatingSystemItem* OperatingSystem)
{
    PCSTR Items[5];
    CHAR ModeText[100];
    ULONG Count, Selected = 0;
    ULONG ResetIndex, BackIndex;
    if (!OperatingSystem)
        return;
    for (;;)
    {
        RtlStringCbPrintfA(ModeText, sizeof(ModeText), "Recovery mode: %s",
                           StartupModes[OperatingSystem->StartupMode]);
        Items[0] = ModeText;
        Items[1] = OperatingSystem->StartupFlags & BOOT_VGA_MODE ? "Use basic display: Enabled" : "Use basic display: Default";
        Items[2] = OperatingSystem->StartupFlags & BOOT_LOGGING ? "Record driver loading: Enabled" : "Record driver loading: Default";
        Count = 3;
        ResetIndex = MAXULONG;
        if (OperatingSystem->StartupMode || OperatingSystem->StartupFlags)
        {
            ResetIndex = Count;
            Items[Count++] = "Reset changes";
        }
        BackIndex = Count;
        Items[Count++] = "Back";
        StartupDescribe(OperatingSystem);
        UiDrawStatusText(OperatingSystem->AdvBootOptsDesc);
        if (!UiDisplayMenu("Recovery options", OperatingSystem->LoadIdentifier,
                           Items, Count, Selected, -1, &Selected, TRUE, NULL, NULL) ||
            Selected == BackIndex)
            return;
        if (Selected == 0)
            OperatingSystem->StartupMode = (OperatingSystem->StartupMode + 1) % RTL_NUMBER_OF(StartupModes);
        else if (Selected == 1)
            OperatingSystem->StartupFlags ^= BOOT_VGA_MODE;
        else if (Selected == 2)
            OperatingSystem->StartupFlags ^= BOOT_LOGGING;
        else if (Selected == ResetIndex)
        {
            OperatingSystem->StartupMode = NO_OPTION;
            OperatingSystem->StartupFlags = 0;
        }
    }
}

VOID
MenuNTOptions(OperatingSystemItem* OperatingSystem)
{
    PCSTR Items[RTL_NUMBER_OF(StartupProfiles) + 2];
    ULONG I, Selected = 0;
    if (!OperatingSystem)
        return;
    for (I = 0; I < RTL_NUMBER_OF(StartupProfiles); ++I)
        Items[I] = StartupProfiles[I].Name;
    Items[I++] = "Recovery options";
    Items[I] = "Back";
    for (;;)
    {
        UiDrawStatusText(OperatingSystem->AdvBootOptsDesc);
        if (!UiDisplayMenu("Startup options", OperatingSystem->LoadIdentifier,
                           Items, RTL_NUMBER_OF(Items), Selected, -1, &Selected,
                           TRUE, NULL, NULL) || Selected == RTL_NUMBER_OF(StartupProfiles) + 1)
            return;
        if (Selected == RTL_NUMBER_OF(StartupProfiles))
        {
            StartupRecoveryOptions(OperatingSystem);
            continue;
        }
        OperatingSystem->StartupProfile = Selected;
        LoadOperatingSystem(OperatingSystem);
        OperatingSystem->StartupProfile = 0;
        return;
    }
}

VOID
AppendBootTimeOptions(PSTR BootOptions, SIZE_T BootOptionsSize)
{
    static const PCSTR Modes[] =
    {
        NULL,
        "SAFEBOOT:MINIMAL SOS NOGUIBOOT",
        "SAFEBOOT:NETWORK SOS NOGUIBOOT",
        "SAFEBOOT:MINIMAL(ALTERNATESHELL) SOS NOGUIBOOT"
    };
    PCSTR Add[2] = {NULL, NULL};
    PCSTR Remove[2] = {NULL, NULL};
    if (BootOptionsSize < sizeof(CHAR))
        return;
    if (StartupProfile)
    {
        Add[0] = StartupProfiles[StartupProfile].Options;
        Remove[0] = StartupProfiles[StartupProfile].Remove;
        NtLdrUpdateOptions(BootOptions, BootOptionsSize, TRUE, Add, Remove);
    }
    if (BootOptionChoice > NO_OPTION && BootOptionChoice < RTL_NUMBER_OF(Modes))
    {
        Add[0] = Modes[BootOptionChoice];
        Remove[0] = "/SAFEBOOT/SAFEBOOT:";
        NtLdrUpdateOptions(BootOptions, BootOptionsSize, TRUE, Add, Remove);
    }
    if ((BootFlags & BOOT_LOGGING) || BootOptionChoice != NO_OPTION)
        NtLdrAddOptions(BootOptions, BootOptionsSize, TRUE, "BOOTLOG");
    if (BootFlags & BOOT_VGA_MODE)
        NtLdrAddOptions(BootOptions, BootOptionsSize, TRUE, "BASEVIDEO");
    if (BootFlags & BOOT_DEBUGGING)
    {
        Add[0] = "DEBUG";
        Remove[0] = "NODEBUG";
        NtLdrUpdateOptions(BootOptions, BootOptionsSize, TRUE, Add, Remove);
    }
}
