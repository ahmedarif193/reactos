/*
 * PROJECT:     ReactOS early desktop splash
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Shared build-time names for the experimental splash handoff
 */

#pragma once

#define REACTOS_EARLY_SPLASH_FILENAME L"BlueHorizon.png"
#define REACTOS_EARLY_SPLASH_RELATIVE_PATH L"\\Web\\Wallpaper\\" REACTOS_EARLY_SPLASH_FILENAME
#define REACTOS_EARLY_SPLASH_READY_EVENT L"ReactOS_EarlySplashDesktopReady"
#define REACTOS_EARLY_SPLASH_READY_TIMEOUT_MS 30000

/* ReactOS-private SystemParametersInfo action; accepted only from Winlogon. */
#define REACTOS_SPI_SET_EARLY_WALLPAPER 0x80000001u
