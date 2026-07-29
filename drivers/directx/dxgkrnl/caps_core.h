/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM level normalization and capability gating
 */

#ifndef _DXGK_CAPS_CORE_H_
#define _DXGK_CAPS_CORE_H_

#include <ntddk.h>

/*
 * D3DKMT_DRIVERVERSION values. Keep these separate from the raw
 * DXGKDDI_INTERFACE_VERSION selector: a selector identifies a particular DDI
 * revision (for example, both 0x5022 and 0x5023 are WDDM 2.0), while these
 * values identify the feature level exposed to KMT clients.
 */
#define DXGK_CAPS_CORE_LEVEL_WDDM_1_0   1000UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_1_1   1105UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_1_2   1200UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_1_3   1300UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_0   2000UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_1   2100UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_2   2200UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_3   2300UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_4   2400UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_5   2500UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_6   2600UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_7   2700UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_8   2800UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_2_9   2900UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_3_0   3000UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_3_1   3100UL
#define DXGK_CAPS_CORE_LEVEL_WDDM_3_2   3200UL

typedef struct _DXGK_CAPS_INPUT
{
    ULONG MiniportDeclaredLevel;
    ULONG OsCompletedLevel;
    ULONG ProviderCompletedLevel;
    ULONG ConfiguredLevel;
    BOOLEAN DisplayOnly;
    BOOLEAN HasRenderCallbacks;
} DXGK_CAPS_INPUT, *PDXGK_CAPS_INPUT;

/* The reported version is the lowest of every ceiling: claiming more than the
 * weakest participant implements is how a client is told a feature works and
 * then finds it does not. */
ULONG DxgkCapsCoreReportedVersion(_In_ const DXGK_CAPS_INPUT *Input);
/*
 * Convert a known kernel DDI selector to its WDDM feature level. Unknown
 * revisions return zero: sharing a high selector family is not sufficient
 * proof that two revisions expose the same initialization-table prefix.
 * Historical official revisions, including the Windows 10 10240 0x5022
 * WDDM 2.0 selector, are listed explicitly.
 */
ULONG DxgkCapsCoreInterfaceVersionToLevel(_In_ ULONG InterfaceVersion);
BOOLEAN DxgkCapsCoreInterfaceVersionAtLeast(
    _In_ ULONG InterfaceVersion,
    _In_ ULONG MinimumLevel);
BOOLEAN DxgkCapsCoreInterfaceVersionInRange(
    _In_ ULONG InterfaceVersion,
    _In_ ULONG MinimumLevel,
    _In_ ULONG MaximumLevel);
BOOLEAN DxgkCapsCoreInterfaceVersionPermitted(
    _In_ ULONG InterfaceVersion,
    _In_ ULONG ConfiguredLevel);
BOOLEAN DxgkCapsCoreRenderSupported(_In_ const DXGK_CAPS_INPUT *Input);
BOOLEAN DxgkCapsCoreFeatureAvailable(_In_ const DXGK_CAPS_INPUT *Input, _In_ ULONG RequiredVersion);
/*
 * Some contracts are valid only for a bounded range. A zero MaximumVersion
 * means that the contract remains valid at all later levels.
 */
BOOLEAN DxgkCapsCoreFeatureAvailableInRange(
    _In_ const DXGK_CAPS_INPUT *Input,
    _In_ ULONG MinimumVersion,
    _In_ ULONG MaximumVersion);

#endif /* _DXGK_CAPS_CORE_H_ */
