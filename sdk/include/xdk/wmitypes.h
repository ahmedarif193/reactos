/******************************************************************************
 *                          WMI Library Support Types                         *
 ******************************************************************************/

#ifdef RUN_WPP
#include <evntrace.h>
#include <stdarg.h>
#endif

#ifndef _TRACEHANDLE_DEFINED
#define _TRACEHANDLE_DEFINED
typedef ULONG64 TRACEHANDLE, *PTRACEHANDLE;
#endif

#ifndef TRACE_INFORMATION_CLASS_DEFINE

typedef struct _ETW_TRACE_SESSION_SETTINGS {
  ULONG Version;
  ULONG BufferSize;
  ULONG MinimumBuffers;
  ULONG MaximumBuffers;
  ULONG LoggerMode;
  ULONG FlushTimer;
  ULONG FlushThreshold;
  ULONG ClockType;
} ETW_TRACE_SESSION_SETTINGS, *PETW_TRACE_SESSION_SETTINGS;

typedef enum _TRACE_INFORMATION_CLASS {
  TraceIdClass,
  TraceHandleClass,
  TraceEnableFlagsClass,
  TraceEnableLevelClass,
  GlobalLoggerHandleClass,
  EventLoggerHandleClass,
  AllLoggerHandlesClass,
  TraceHandleByNameClass,
  LoggerEventsLostClass,
  TraceSessionSettingsClass,
  LoggerEventsLoggedClass,
  MaxTraceInformationClass
} TRACE_INFORMATION_CLASS;

#endif /* TRACE_INFORMATION_CLASS_DEFINE */

#ifndef _ETW_KM_
#define _ETW_KM_
#endif

#include <evntprov.h>

_IRQL_requires_same_
typedef VOID
(NTAPI *PETWENABLECALLBACK)(
  _In_ LPCGUID SourceId,
  _In_ ULONG ControlCode,
  _In_ UCHAR Level,
  _In_ ULONGLONG MatchAnyKeyword,
  _In_ ULONGLONG MatchAllKeyword,
  _In_opt_ PEVENT_FILTER_DESCRIPTOR FilterData,
  _Inout_opt_ PVOID CallbackContext);

#define EVENT_WRITE_FLAG_NO_FAULTING             0x00000001

#define PCW_VERSION_1 0x0100
#if (NTDDI_VERSION >= NTDDI_WIN10_MN)
#define PCW_VERSION_2 0x0200
#endif

#if (NTDDI_VERSION >= NTDDI_WIN10_FE)
#define PCW_CURRENT_VERSION PCW_VERSION_2
#else
#define PCW_CURRENT_VERSION PCW_VERSION_1
#endif

typedef struct _PCW_INSTANCE *PPCW_INSTANCE;
typedef struct _PCW_REGISTRATION *PPCW_REGISTRATION;
typedef struct _PCW_BUFFER *PPCW_BUFFER;

typedef struct _PCW_COUNTER_DESCRIPTOR {
    USHORT Id;
    USHORT StructIndex;
    USHORT Offset;
    USHORT Size;
} PCW_COUNTER_DESCRIPTOR, *PPCW_COUNTER_DESCRIPTOR;

typedef struct _PCW_DATA {
    _In_reads_bytes_(Size) const VOID *Data;
    _In_ ULONG Size;
} PCW_DATA, *PPCW_DATA;

typedef struct _PCW_COUNTER_INFORMATION {
    ULONG64 CounterMask;
    PCUNICODE_STRING InstanceMask;
} PCW_COUNTER_INFORMATION, *PPCW_COUNTER_INFORMATION;

typedef struct _PCW_MASK_INFORMATION {
    ULONG64 CounterMask;
    PCUNICODE_STRING InstanceMask;
    ULONG InstanceId;
    BOOLEAN CollectMultiple;
    PPCW_BUFFER Buffer;
    PKEVENT CancelEvent;
} PCW_MASK_INFORMATION, *PPCW_MASK_INFORMATION;

typedef union _PCW_CALLBACK_INFORMATION {
    PCW_COUNTER_INFORMATION AddCounter;
    PCW_COUNTER_INFORMATION RemoveCounter;
    PCW_MASK_INFORMATION EnumerateInstances;
    PCW_MASK_INFORMATION CollectData;
} PCW_CALLBACK_INFORMATION, *PPCW_CALLBACK_INFORMATION;

typedef enum _PCW_CALLBACK_TYPE {
    PcwCallbackAddCounter = 0,
    PcwCallbackRemoveCounter,
    PcwCallbackEnumerateInstances,
    PcwCallbackCollectData,
} PCW_CALLBACK_TYPE, *PPCW_CALLBACK_TYPE;

typedef
NTSTATUS NTAPI
PCW_CALLBACK(
    _In_ PCW_CALLBACK_TYPE Type,
    _In_ PPCW_CALLBACK_INFORMATION Info,
    _In_opt_ PVOID Context
    );

typedef PCW_CALLBACK *PPCW_CALLBACK;

typedef enum PCW_REGISTRATION_FLAGS {
    PcwRegistrationNone = 0,
#if (NTDDI_VERSION >= NTDDI_WIN10_MN)
    PcwRegistrationSiloNeutral = 0x0001,
#endif
} PCW_REGISTRATION_FLAGS;

typedef struct _PCW_REGISTRATION_INFORMATION {
    _In_ ULONG Version;
    _In_ PCUNICODE_STRING Name;
    _In_ ULONG CounterCount;
    _In_reads_(CounterCount) PPCW_COUNTER_DESCRIPTOR Counters;
    _In_opt_ PPCW_CALLBACK Callback;
    _In_opt_ PVOID CallbackContext;
#if (NTDDI_VERSION >= NTDDI_WIN10_MN)
    PCW_REGISTRATION_FLAGS Flags;
#endif
} PCW_REGISTRATION_INFORMATION, *PPCW_REGISTRATION_INFORMATION;

