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

static BOOLEAN Rpi3WppRecorderLevelEnabled;
#define WPP_RECORDER_LEVEL_FILTER(_Flag) Rpi3WppRecorderLevelEnabled

typedef struct _RECORDER_CONFIGURE_PARAMS
{
    ULONG Size;
    BOOLEAN CreateDefaultLog;
} RECORDER_CONFIGURE_PARAMS, *PRECORDER_CONFIGURE_PARAMS;

typedef PVOID RECORDER_LOG;

typedef struct _RECORDER_LOG_CREATE_PARAMS
{
    ULONG Size;
    PCSTR LogIdentifier;
    ULONG TotalBufferSize;
    ULONG ErrorPartitionSize;
} RECORDER_LOG_CREATE_PARAMS, *PRECORDER_LOG_CREATE_PARAMS;

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

FORCEINLINE
VOID
RECORDER_LOG_CREATE_PARAMS_INIT(
    _Out_ PRECORDER_LOG_CREATE_PARAMS Params,
    _In_opt_ PCSTR LogIdentifier)
{
    RtlZeroMemory(Params, sizeof(*Params));
    Params->Size = sizeof(*Params);
    Params->LogIdentifier = LogIdentifier;
}

FORCEINLINE
RECORDER_LOG
WppRecorderLogGetDefault(VOID)
{
    return (RECORDER_LOG)(ULONG_PTR)1;
}

FORCEINLINE
NTSTATUS
WppRecorderLogCreate(
    _In_ PRECORDER_LOG_CREATE_PARAMS Params,
    _Out_ RECORDER_LOG *Log)
{
    UNREFERENCED_PARAMETER(Params);
    *Log = WppRecorderLogGetDefault();
    return STATUS_SUCCESS;
}

FORCEINLINE
VOID
WppRecorderLogDelete(
    _In_opt_ RECORDER_LOG Log)
{
    UNREFERENCED_PARAMETER(Log);
}
