/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM feature-query result negotiation
 */

#ifndef _DXGK_FEATURE_QUERY_CORE_H_
#define _DXGK_FEATURE_QUERY_CORE_H_

#include <ntddk.h>

/*
 * WDK 10.0.26100 DXGK_ISFEATUREENABLED_RESULT bit assignments.  Keep this
 * core independent of the WDDM 3.2 public type so lower-level builds and
 * kmtests can exercise the negotiation without exposing the newer ABI.
 */
#define DXGK_FEATURE_QUERY_RESULT_ENABLED                   0x0001U
#define DXGK_FEATURE_QUERY_RESULT_KNOWN                     0x0002U
#define DXGK_FEATURE_QUERY_RESULT_SUPPORTED_BY_DRIVER       0x0004U
#define DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG       0x0008U
#define DXGK_FEATURE_QUERY_RESULT_VALID_MASK                0x000FU

typedef struct _DXGK_FEATURE_QUERY_CORE_INPUT
{
    BOOLEAN KnownFeature;
    BOOLEAN RequiresDriverSupport;

    BOOLEAN OsSupported;
    BOOLEAN OsSupportedOnCurrentConfig;
    USHORT OsMinVersion;
    USHORT OsMaxVersion;

    BOOLEAN DriverResponseValid;
    BOOLEAN SupportedByDriver;
    BOOLEAN SupportedOnCurrentConfig;
    USHORT DriverMinVersion;
    USHORT DriverMaxVersion;
} DXGK_FEATURE_QUERY_CORE_INPUT, *PDXGK_FEATURE_QUERY_CORE_INPUT;

typedef struct _DXGK_FEATURE_QUERY_CORE_RESULT
{
    USHORT Version;
    USHORT Value;
} DXGK_FEATURE_QUERY_CORE_RESULT, *PDXGK_FEATURE_QUERY_CORE_RESULT;

/*
 * Returns FALSE only when a party claims support with a malformed version
 * range.  A missing driver response and two non-overlapping valid ranges are
 * ordinary disabled results, not malformed contracts.
 */
BOOLEAN
DxgkFeatureQueryCoreEvaluate(
    _In_ const DXGK_FEATURE_QUERY_CORE_INPUT *Input,
    _Out_ DXGK_FEATURE_QUERY_CORE_RESULT *Result);

#endif /* _DXGK_FEATURE_QUERY_CORE_H_ */
