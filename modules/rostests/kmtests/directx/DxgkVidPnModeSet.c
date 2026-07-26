/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     VidPN mode sets and mode pinning
 *
 * A mode with a zero refresh denominator divides by zero the moment anything
 * computes a rate; a second pin silently changes the committed mode.
 */

#include <kmt_test.h>
#include "display_core.h"

static VOID InitMode(_Out_ PDXGK_VIDPN_MODE Mode, _In_ ULONG Width, _In_ ULONG Height)
{
    RtlZeroMemory(Mode, sizeof(*Mode));
    Mode->Width = Width;
    Mode->Height = Height;
    Mode->RefreshRateNumerator = 60;
    Mode->RefreshRateDenominator = 1;
    Mode->BitsPerPixel = 32;
}

static VOID TestModeValidation(VOID)
{
    DXGK_VIDPN_MODE Mode;

    InitMode(&Mode, 1920, 1080);
    { NTSTATUS Observed = DxgkVidPnCoreModeValid(&Mode); ok_eq_hex(Observed, STATUS_SUCCESS); }

    InitMode(&Mode, 0, 1080);
    { NTSTATUS Observed = DxgkVidPnCoreModeValid(&Mode); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    InitMode(&Mode, 1920, 0);
    { NTSTATUS Observed = DxgkVidPnCoreModeValid(&Mode); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    InitMode(&Mode, 1920, 1080);
    Mode.BitsPerPixel = 0;
    { NTSTATUS Observed = DxgkVidPnCoreModeValid(&Mode); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* A zero denominator divides by zero the moment anything computes Hz. */
    InitMode(&Mode, 1920, 1080);
    Mode.RefreshRateDenominator = 0;
    { NTSTATUS Observed = DxgkVidPnCoreModeValid(&Mode); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* A zero numerator is a mode that never scans out. */
    InitMode(&Mode, 1920, 1080);
    Mode.RefreshRateNumerator = 0;
    { NTSTATUS Observed = DxgkVidPnCoreModeValid(&Mode); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Fractional rates such as 59.94Hz are legitimate. */
    InitMode(&Mode, 1920, 1080);
    Mode.RefreshRateNumerator = 60000;
    Mode.RefreshRateDenominator = 1001;
    { NTSTATUS Observed = DxgkVidPnCoreModeValid(&Mode); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID TestModeSet(VOID)
{
    DXGK_VIDPN_MODESET ModeSet;
    DXGK_VIDPN_MODE Mode;
    ULONG Added;

    DxgkVidPnCoreModeSetInitialize(&ModeSet);
    ok_eq_ulong(ModeSet.ModeCount, 0UL);
    ok_eq_ulong(ModeSet.PinnedIndex, (ULONG)DXGK_VIDPN_CORE_NO_PIN);

    InitMode(&Mode, 1920, 1080);
    { NTSTATUS Observed = DxgkVidPnCoreAddMode(&ModeSet, &Mode); ok_eq_hex(Observed, STATUS_SUCCESS); }
    InitMode(&Mode, 1280, 720);
    { NTSTATUS Observed = DxgkVidPnCoreAddMode(&ModeSet, &Mode); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(ModeSet.ModeCount, 2UL);

    /* An invalid mode must never enter the set: everything downstream trusts
     * that enumeration only yields usable modes. */
    InitMode(&Mode, 640, 480);
    Mode.RefreshRateDenominator = 0;
    { NTSTATUS Observed = DxgkVidPnCoreAddMode(&ModeSet, &Mode); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulong(ModeSet.ModeCount, 2UL);

    DxgkVidPnCoreModeSetInitialize(&ModeSet);
    for (Added = 0; Added < DXGK_VIDPN_CORE_MAX_MODES; ++Added)
    {
        InitMode(&Mode, 640 + Added, 480);
        if (!NT_SUCCESS(DxgkVidPnCoreAddMode(&ModeSet, &Mode)))
            break;
    }
    ok_eq_ulong(Added, (ULONG)DXGK_VIDPN_CORE_MAX_MODES);
    InitMode(&Mode, 800, 600);
    { NTSTATUS Observed = DxgkVidPnCoreAddMode(&ModeSet, &Mode); ok_eq_hex(Observed, STATUS_INSUFFICIENT_RESOURCES); }
}

static VOID TestPinning(VOID)
{
    DXGK_VIDPN_MODESET ModeSet;
    DXGK_VIDPN_MODE Mode;
    DXGK_VIDPN_MODE Pinned;

    DxgkVidPnCoreModeSetInitialize(&ModeSet);
    InitMode(&Mode, 1920, 1080);
    { NTSTATUS Observed = DxgkVidPnCoreAddMode(&ModeSet, &Mode); ok_eq_hex(Observed, STATUS_SUCCESS); }
    InitMode(&Mode, 1280, 720);
    { NTSTATUS Observed = DxgkVidPnCoreAddMode(&ModeSet, &Mode); ok_eq_hex(Observed, STATUS_SUCCESS); }

    ok_bool_false(DxgkVidPnCoreGetPinnedMode(&ModeSet, &Pinned), "nothing pinned yet");

    { NTSTATUS Observed = DxgkVidPnCorePinMode(&ModeSet, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkVidPnCoreGetPinnedMode(&ModeSet, &Pinned), "pinned mode readable");
    ok_eq_ulong(Pinned.Width, 1280UL);
    ok_eq_ulong(Pinned.Height, 720UL);

    /* Pinning over an existing pin would silently change the committed mode
     * out from under whoever pinned it. */
    { NTSTATUS Observed = DxgkVidPnCorePinMode(&ModeSet, 0); ok_eq_hex(Observed, STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET); }
    ok_bool_true(DxgkVidPnCoreGetPinnedMode(&ModeSet, &Pinned), "still the original pin");
    ok_eq_ulong(Pinned.Width, 1280UL);

    { NTSTATUS Observed = DxgkVidPnCoreUnpinMode(&ModeSet); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkVidPnCoreGetPinnedMode(&ModeSet, &Pinned), "unpinned");
    { NTSTATUS Observed = DxgkVidPnCoreUnpinMode(&ModeSet); ok_eq_hex(Observed, STATUS_GRAPHICS_MODE_NOT_PINNED); }

    { NTSTATUS Observed = DxgkVidPnCorePinMode(&ModeSet, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkVidPnCoreGetPinnedMode(&ModeSet, &Pinned), "repinned");
    ok_eq_ulong(Pinned.Width, 1920UL);

    { NTSTATUS Observed = DxgkVidPnCoreUnpinMode(&ModeSet); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkVidPnCorePinMode(&ModeSet, 2); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkVidPnCorePinMode(&ModeSet, 0xFFFFFFFFUL); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

START_TEST(DxgkVidPnModeSet)
{
    TestModeValidation();
    TestModeSet();
    TestPinning();
}

/* EOF */
