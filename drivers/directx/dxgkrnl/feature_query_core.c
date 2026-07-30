/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM feature-query result negotiation
 */

#include "feature_query_core.h"

static BOOLEAN
DxgkFeatureQueryCoreRangeValid(
    _In_ USHORT Minimum,
    _In_ USHORT Maximum)
{
    return Minimum != 0 && Maximum >= Minimum;
}

BOOLEAN
DxgkFeatureQueryCoreEvaluate(
    _In_ const DXGK_FEATURE_QUERY_CORE_INPUT *Input,
    _Out_ DXGK_FEATURE_QUERY_CORE_RESULT *Result)
{
    USHORT Minimum;
    USHORT Maximum;

    if (Input == NULL || Result == NULL)
        return FALSE;

    Result->Version = 0;
    Result->Value = 0;

    if (!Input->KnownFeature)
        return TRUE;

    Result->Value |= DXGK_FEATURE_QUERY_RESULT_KNOWN;

    if (Input->OsSupported &&
        !DxgkFeatureQueryCoreRangeValid(Input->OsMinVersion,
                                        Input->OsMaxVersion))
    {
        Result->Version = 0;
        Result->Value = 0;
        return FALSE;
    }

    if (Input->RequiresDriverSupport)
    {
        if (Input->DriverResponseValid && Input->SupportedByDriver)
        {
            if (!DxgkFeatureQueryCoreRangeValid(Input->DriverMinVersion,
                                                Input->DriverMaxVersion))
            {
                Result->Version = 0;
                Result->Value = 0;
                return FALSE;
            }

            Result->Value |=
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_BY_DRIVER;
            if (Input->SupportedOnCurrentConfig)
            {
                Result->Value |=
                    DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG;
            }
        }

        if (!Input->OsSupported ||
            !Input->OsSupportedOnCurrentConfig ||
            !Input->DriverResponseValid ||
            !Input->SupportedByDriver ||
            !Input->SupportedOnCurrentConfig)
        {
            return TRUE;
        }

        Minimum = Input->OsMinVersion > Input->DriverMinVersion
                      ? Input->OsMinVersion
                      : Input->DriverMinVersion;
        Maximum = Input->OsMaxVersion < Input->DriverMaxVersion
                      ? Input->OsMaxVersion
                      : Input->DriverMaxVersion;
        if (Maximum < Minimum)
            return TRUE;
    }
    else
    {
        if (Input->OsSupportedOnCurrentConfig)
        {
            Result->Value |=
                DXGK_FEATURE_QUERY_RESULT_SUPPORTED_ON_CONFIG;
        }
        if (!Input->OsSupported || !Input->OsSupportedOnCurrentConfig)
            return TRUE;

        Maximum = Input->OsMaxVersion;
    }

    Result->Version = Maximum;
    Result->Value |= DXGK_FEATURE_QUERY_RESULT_ENABLED;
    return TRUE;
}
