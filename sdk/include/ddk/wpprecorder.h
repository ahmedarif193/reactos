/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WPP recorder (inflight trace recorder) interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WPPRECORDER_H_
#define _WPPRECORDER_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_KERNEL_MODE)
#if ((NTDDI_VERSION >= NTDDI_WIN8) || defined (__WPP_RECORDER_DOWNLEVEL__))

typedef enum _WPP_RECORDER_TRI_STATE {
    WppRecorderFalse      = FALSE,
    WppRecorderTrue       = TRUE,
    WppRecorderUseDefault = 2,
} WPP_RECORDER_TRI_STATE, *PWPP_RECORDER_TRI_STATE;

DECLARE_HANDLE(RECORDER_LOG);

DECLARE_HANDLE(WPP_RECORDER_COUNTER);

#define RECORDER_LOG_IDENTIFIER_MAX_CHARS       16

#define RECORDER_LOG_DEFAULT_BUFFER_SIZE        1024

#define RECORDER_LOG_DEFAULT_ERR_PARTITION_SIZE 200

typedef struct _WPP_TRIAGE_INFO {
    ULONG WppAutoLogHeaderSize;
    ULONG WppDriverContextOffset;
    ULONG WppAutoLogHeaderSizeOffset;
    ULONG WppSizeOfAutoLogHeaderSizeField;
    ULONG WppDriverContextSize;
} WPP_TRIAGE_INFO, *PWPP_TRIAGE_INFO;

typedef struct _RECORDER_CONFIGURE_PARAMS {
    ULONG Size;
    BOOLEAN CreateDefaultLog;
#if (NTDDI_VERSION >= NTDDI_WIN10_NI) || defined (__WPP_RECORDER_DOWNLEVEL__)
    WPP_RECORDER_TRI_STATE   UseTimeStamp;
    WPP_RECORDER_TRI_STATE   PreciseTimeStamp;
#endif
} RECORDER_CONFIGURE_PARAMS, *PRECORDER_CONFIGURE_PARAMS;

FORCEINLINE
VOID
RECORDER_CONFIGURE_PARAMS_INIT(
    _Out_
        PRECORDER_CONFIGURE_PARAMS  Params
    )
{
    Params->Size = sizeof(*Params);
    Params->CreateDefaultLog = TRUE;
#if (NTDDI_VERSION >= NTDDI_WIN10_NI) || defined (__WPP_RECORDER_DOWNLEVEL__)
    Params->UseTimeStamp     = WppRecorderUseDefault;
    Params->PreciseTimeStamp = WppRecorderUseDefault;
#endif
}

__drv_maxIRQL(PASSIVE_LEVEL)
VOID
imp_WppRecorderConfigure(
    _In_
        PVOID                       WppCb,
    _In_
        CONST RECORDER_CONFIGURE_PARAMS* ConfigureParams
    );

#define WppRecorderConfigure(ConfigureParams) \
    imp_WppRecorderConfigure(WPP_CB, ConfigureParams)

typedef struct _RECORDER_LOG_CREATE_PARAMS {
    ULONG       Size;
    ULONG       LogTag;
    ULONG       TotalBufferSize;
    ULONG       ErrorPartitionSize;
    ULONG_PTR   LogIdentifierAppendValue;
    BOOLEAN     LogIdentifierAppendValueSet;
    ULONG       LogIdentifierSize;
    _Field_size_(LogIdentifierSize)
    CHAR        LogIdentifier[RECORDER_LOG_IDENTIFIER_MAX_CHARS];
#if (NTDDI_VERSION >= NTDDI_WIN10_NI) || defined (__WPP_RECORDER_DOWNLEVEL__)
    WPP_RECORDER_TRI_STATE   UseTimeStamp;
    WPP_RECORDER_TRI_STATE   PreciseTimeStamp;
#endif
} RECORDER_LOG_CREATE_PARAMS, *PRECORDER_LOG_CREATE_PARAMS;

FORCEINLINE
VOID
RECORDER_LOG_CREATE_PARAMS_INIT(
    _Out_
        PRECORDER_LOG_CREATE_PARAMS Params,
    _In_opt_
        PCSTR                       LogIdentifier
    )
{
    Params->Size = sizeof(*Params);
    Params->TotalBufferSize = RECORDER_LOG_DEFAULT_BUFFER_SIZE;
    Params->ErrorPartitionSize = RECORDER_LOG_DEFAULT_ERR_PARTITION_SIZE;
    Params->LogIdentifier[0] = '\0';
    Params->LogIdentifierSize = sizeof(Params->LogIdentifier);
    Params->LogIdentifierAppendValue = 0;
    Params->LogIdentifierAppendValueSet = FALSE;
    Params->LogTag = 0;
    if (LogIdentifier != NULL) {
        RtlStringCchCopyA(Params->LogIdentifier,
                          ARRAYSIZE(Params->LogIdentifier),
                          LogIdentifier);
    }
#if (NTDDI_VERSION >= NTDDI_WIN10_NI) || defined (__WPP_RECORDER_DOWNLEVEL__)
    Params->UseTimeStamp     = WppRecorderUseDefault;
    Params->PreciseTimeStamp = WppRecorderUseDefault;
#endif
}

__drv_maxIRQL(DISPATCH_LEVEL)
NTSTATUS
imp_WppRecorderLogCreate(
    _In_
        PVOID                       WppCb,
    _In_
        CONST RECORDER_LOG_CREATE_PARAMS* CreateParams,
    _Out_
        RECORDER_LOG *              RecorderLog
    );

#define WppRecorderLogCreate(CreateParams, RecorderLog) \
    imp_WppRecorderLogCreate(WPP_CB, CreateParams, RecorderLog)

__drv_maxIRQL(DISPATCH_LEVEL)
VOID
imp_WppRecorderLogDelete(
    _In_
       PVOID                        WppCb,
    _In_
        RECORDER_LOG                RecorderLog
    );

#define WppRecorderLogDelete(RecorderLog) \
    imp_WppRecorderLogDelete(WPP_CB, RecorderLog)

__drv_maxIRQL(DISPATCH_LEVEL)
VOID
imp_WppRecorderLogSetIdentifier(
    _In_
       PVOID                        WppCb,
    _In_
        RECORDER_LOG                RecorderLog,
    _In_
        PCSTR                       LogIdentifier
    );

#define WppRecorderLogSetIdentifier(RecorderLog, LogIdentifier) \
    imp_WppRecorderLogSetIdentifier(WPP_CB, RecorderLog, LogIdentifier)

__drv_maxIRQL(DISPATCH_LEVEL)
RECORDER_LOG
imp_WppRecorderLogGetDefault(
    _In_
        PVOID                       WppCb
    );

#define WppRecorderLogGetDefault() \
    imp_WppRecorderLogGetDefault(WPP_CB)

__drv_maxIRQL(DISPATCH_LEVEL)
BOOLEAN
imp_WppRecorderIsDefaultLogAvailable(
    _In_
        PVOID                       WppCb
    );

#define WppRecorderIsDefaultLogAvailable() \
    imp_WppRecorderIsDefaultLogAvailable(WPP_CB)

__drv_maxIRQL(DISPATCH_LEVEL)
WPP_RECORDER_COUNTER
imp_WppRecorderGetCounterHandle(
    _In_
        PVOID                       WppCb
    );

#define WppRecorderGetCounterHandle() \
    imp_WppRecorderGetCounterHandle(WPP_CB)

__drv_maxIRQL(DISPATCH_LEVEL)
NTSTATUS
imp_WppRecorderLinkCounters(
    _In_
        PVOID                       WppCb,
    _In_
        WPP_RECORDER_COUNTER        CounterOwner
    );

#define WppRecorderLinkCounters(CounterOwner) \
    imp_WppRecorderLinkCounters(WPP_CB, CounterOwner)

__drv_maxIRQL(DISPATCH_LEVEL)
NTSTATUS
imp_WppRecorderGetTriageInfo(
    _In_
        PVOID                       WppCb,
    _Out_
        PWPP_TRIAGE_INFO            WppTriageInfo
    );

#define WppRecorderGetTriageInfo(WppTriageInfo) \
    imp_WppRecorderGetTriageInfo(WPP_CB, WppTriageInfo)

__drv_maxIRQL(HIGH_LEVEL)
NTSTATUS
imp_WppRecorderDumpLiveDriverData(
    _In_
        PVOID                WppCb,
    _Out_ __deref_ecount(*OutBufferLength)
        PVOID              * OutBuffer,
    _Out_
        PULONG               OutBufferLength,
    _Out_
        LPGUID               Guid
    );

#define WppRecorderDumpLiveDriverData(OutBuffer, OutBufferLength, Guid) \
    imp_WppRecorderDumpLiveDriverData(WPP_CB, OutBuffer, OutBufferLength, Guid)

__drv_maxIRQL(HIGH_LEVEL)
NTSTATUS
imp_WppRecorderLogDumpLiveData(
    _In_
        PVOID                WppCb,
    _In_
        RECORDER_LOG         RecorderLog,
    _Out_ __deref_ecount(*OutBufferLength)
        PVOID              * OutBuffer,
    _Out_
        PULONG               OutBufferLength,
    _Out_
        LPGUID               Guid
    );

#define WppRecorderLogDumpLiveData(RecorderLog, OutBuffer, OutBufferLength, Guid) \
    imp_WppRecorderLogDumpLiveData(WPP_CB, RecorderLog, OutBuffer, OutBufferLength, Guid)

#define WPP_RECORDER_IFRLOG_LEVEL_FLAGS_ARGS(ifr, lvl, flags) ifr, lvl, WPP_BIT_ ## flags

#define WPP_RECORDER_IFRLOG_LEVEL_FLAGS_FILTER(ifr, lvl, flags) \
    (lvl < TRACE_LEVEL_VERBOSE || WPP_CONTROL(WPP_BIT_ ## flags).AutoLogVerboseEnabled)

#define WPP_RECORDER_LEVEL_FLAGS_IFRLOG_ARGS(lvl, flags, ifr) \
    WPP_RECORDER_IFRLOG_LEVEL_FLAGS_ARGS    (ifr, lvl,   flags)

#define WPP_RECORDER_LEVEL_FLAGS_IFRLOG_FILTER(lvl, flags, ifr) \
    WPP_RECORDER_IFRLOG_LEVEL_FLAGS_FILTER    (ifr, lvl,   flags)

#define WPP_RECORDER_LEVEL_IFRLOG_FLAGS_ARGS(lvl, ifr, flags) \
    WPP_RECORDER_IFRLOG_LEVEL_FLAGS_ARGS    (ifr, lvl, flags)

#define WPP_RECORDER_LEVEL_IFRLOG_FLAGS_FILTER(lvl, ifr, flags) \
    WPP_RECORDER_IFRLOG_LEVEL_FLAGS_FILTER    (ifr, lvl, flags)

#endif
#endif
#if !defined(_KERNEL_MODE)
#if ((NTDDI_VERSION >= NTDDI_WIN8))

DECLARE_HANDLE(RECORDER_LOG);

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN10_CU))

__drv_maxIRQL(HIGH_LEVEL)
NTSTATUS
imp_WppRecorderLogDumpLiveData(
    _In_
        PVOID                WppCb,
    _In_opt_
        RECORDER_LOG         RecorderLog,
    _Out_ __deref_ecount(*OutBufferLength)
        PVOID              * OutBuffer,
    _Out_
        PULONG               OutBufferLength,
    _Out_
        LPGUID               Guid
    );

#define WppRecorderLogDumpLiveData(RecorderLog, OutBuffer, OutBufferLength, Guid) \
    imp_WppRecorderLogDumpLiveData(WPP_CB, RecorderLog, OutBuffer, OutBufferLength, Guid)

#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
