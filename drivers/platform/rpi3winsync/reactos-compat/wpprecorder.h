/*
 * PROJECT:     ReactOS Raspberry Pi 3 Windows driver compatibility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Provide the common WPP recorder surface for synchronized drivers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#define WPP_DEFINE_CONTROL_GUID(_Name, _Guid, _Bits)
#define WPP_DEFINE_BIT(_Name)
#define WPP_INIT_TRACING(_DriverObject, _RegistryPath) do { } while (0)
#define WPP_CLEANUP(_DriverObject) do { } while (0)

typedef struct _RECORDER_CONFIGURE_PARAMS
{
    ULONG Size;
    BOOLEAN CreateDefaultLog;
} RECORDER_CONFIGURE_PARAMS, *PRECORDER_CONFIGURE_PARAMS;

FORCEINLINE
VOID
RECORDER_CONFIGURE_PARAMS_INIT(
    _Out_ PRECORDER_CONFIGURE_PARAMS Params)
{
    Params->Size = sizeof(*Params);
    Params->CreateDefaultLog = TRUE;
}

FORCEINLINE
VOID
WppRecorderConfigure(
    _In_ PRECORDER_CONFIGURE_PARAMS ConfigureParams)
{
    UNREFERENCED_PARAMETER(ConfigureParams);
}
