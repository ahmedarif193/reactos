/*
 * PROJECT:     LiberNT Boot Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Boot manager settings and power actions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>

VOID
OptionMenuReboot(VOID)
{
    PCSTR Items[] = {"Restart", "Back"};
    ULONG Selected;
    if (UiDisplayMenu("Restart this device?", NULL, Items, RTL_NUMBER_OF(Items),
                      1, -1, &Selected, TRUE, NULL, NULL) && Selected == 0)
        Reboot();
}

VOID
FreeLdrSetupMenu(OperatingSystemItem* OperatingSystem)
{
    PCSTR Items[3];
    ULONG Count, Selected = 0, FirmwareIndex, RestartIndex, BackIndex;
    UiDrawStatusText("");
    for (;;)
    {
        Count = 0;
        FirmwareIndex = MAXULONG;
#ifdef UEFIBOOT
        if (UefiFirmwareSetupSupported())
        {
            FirmwareIndex = Count;
            Items[Count++] = "UEFI firmware settings";
        }
#endif
        RestartIndex = Count;
        Items[Count++] = "Restart";
        BackIndex = Count;
        Items[Count++] = "Back";
        if (!UiDisplayMenu("Settings", NULL, Items, Count, Selected, -1,
                           &Selected, TRUE, NULL, NULL) || Selected == BackIndex)
            return;
        if (Selected == FirmwareIndex)
        {
#ifdef UEFIBOOT
            UefiBootToFirmware();
#endif
        }
        else if (Selected == RestartIndex)
            OptionMenuReboot();
    }
}

VOID
DisplayBootTimeOptions(OperatingSystemItem* OperatingSystem)
{
    CHAR Status[520];
    Status[0] = 0;
    if (OperatingSystem->AdvBootOptsDesc[0])
        RtlStringCbPrintfA(Status, sizeof(Status), "%s: %s",
                           OperatingSystem->LoadIdentifier, OperatingSystem->AdvBootOptsDesc);
    UiDrawStatusText(Status);
}
