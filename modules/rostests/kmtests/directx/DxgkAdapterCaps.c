/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Reported WDDM version and capability gating
 *
 * The version a client sees decides which APIs it will call.  Claiming more
 * than the weakest participant implements is how an app is told a feature
 * works and then finds it does not.
 */

#include <kmt_test.h>
#include "object_core.h"

static VOID InitInput(_Out_ PDXGK_CAPS_INPUT Input)
{
    RtlZeroMemory(Input, sizeof(*Input));
    Input->MiniportDeclaredVersion = DXGK_CAPS_CORE_VERSION_WDDM_3_0;
    Input->OsCompletedVersion = DXGK_CAPS_CORE_VERSION_WDDM_3_0;
    Input->ProviderCompletedVersion = DXGK_CAPS_CORE_VERSION_WDDM_3_0;
    Input->HasRenderCallbacks = TRUE;
}

static VOID TestVersionIsTheMinimum(VOID)
{
    DXGK_CAPS_INPUT Input;

    InitInput(&Input);
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_VERSION_WDDM_3_0); }

    /* Any one ceiling lowers the reported version on its own. */
    InitInput(&Input);
    Input.OsCompletedVersion = DXGK_CAPS_CORE_VERSION_WDDM_1_3;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_VERSION_WDDM_1_3); }

    InitInput(&Input);
    Input.ProviderCompletedVersion = DXGK_CAPS_CORE_VERSION_WDDM_2_0;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_VERSION_WDDM_2_0); }

    InitInput(&Input);
    Input.MiniportDeclaredVersion = DXGK_CAPS_CORE_VERSION_WDDM_2_0;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_VERSION_WDDM_2_0); }

    /* The lowest of several wins, not the last one examined. */
    InitInput(&Input);
    Input.MiniportDeclaredVersion = DXGK_CAPS_CORE_VERSION_WDDM_2_9;
    Input.OsCompletedVersion = DXGK_CAPS_CORE_VERSION_WDDM_1_3;
    Input.ProviderCompletedVersion = DXGK_CAPS_CORE_VERSION_WDDM_2_0;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_VERSION_WDDM_1_3); }

    /* A miniport below every ceiling keeps its own lower number. */
    InitInput(&Input);
    Input.MiniportDeclaredVersion = DXGK_CAPS_CORE_VERSION_WDDM_1_3;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_VERSION_WDDM_1_3); }

    /* Zero means "no opinion" and must not clamp the result to zero. */
    InitInput(&Input);
    Input.OsCompletedVersion = 0;
    Input.ProviderCompletedVersion = 0;
    { ULONG Observed = DxgkCapsCoreReportedVersion(&Input); ok_eq_ulong(Observed, (ULONG)DXGK_CAPS_CORE_VERSION_WDDM_3_0); }
}

static VOID TestRenderSupport(VOID)
{
    DXGK_CAPS_INPUT Input;

    InitInput(&Input);
    ok_bool_true(DxgkCapsCoreRenderSupported(&Input), "full adapter renders");

    /* A display-only adapter never renders, whatever version it declares. */
    InitInput(&Input);
    Input.DisplayOnly = TRUE;
    ok_bool_false(DxgkCapsCoreRenderSupported(&Input), "display-only cannot render");

    /*
     * Render support is a statement about the callbacks a submission actually
     * travels through, not about the version a miniport declares.
     */
    InitInput(&Input);
    Input.HasRenderCallbacks = FALSE;
    ok_bool_false(DxgkCapsCoreRenderSupported(&Input), "no render callbacks, no render");
}

static VOID TestFeatureGating(VOID)
{
    DXGK_CAPS_INPUT Input;

    InitInput(&Input);
    Input.OsCompletedVersion = DXGK_CAPS_CORE_VERSION_WDDM_1_3;

    /* Everything above the reported version must read as unavailable, so a
     * client does not call an API the stack cannot honour. */
    ok_bool_true(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_VERSION_WDDM_1_3), "at the cap");
    ok_bool_false(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_VERSION_WDDM_2_0), "above the cap");
    ok_bool_false(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_VERSION_WDDM_3_0), "far above the cap");

    InitInput(&Input);
    ok_bool_true(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_VERSION_WDDM_2_0), "below the cap");
    ok_bool_true(DxgkCapsCoreFeatureAvailable(&Input, DXGK_CAPS_CORE_VERSION_WDDM_3_0), "at the cap");
}

START_TEST(DxgkAdapterCaps)
{
    TestVersionIsTheMinimum();
    TestRenderSupport();
    TestFeatureGating();
}

/* EOF */
