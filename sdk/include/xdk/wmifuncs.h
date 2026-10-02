/******************************************************************************
 *                          WMI Library Support Functions                     *
 ******************************************************************************/

#ifdef RUN_WPP
#if (NTDDI_VERSION >= NTDDI_WINXP)
_IRQL_requires_max_(HIGH_LEVEL)
NTKERNELAPI
NTSTATUS
__cdecl
WmiTraceMessage(
  _In_ TRACEHANDLE LoggerHandle,
  _In_ ULONG MessageFlags,
  _In_ LPGUID MessageGuid,
  _In_ USHORT MessageNumber,
  ...);
#endif
#endif /* RUN_WPP */

#if (NTDDI_VERSION >= NTDDI_WINXP)

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
WmiQueryTraceInformation(
  _In_ TRACE_INFORMATION_CLASS TraceInformationClass,
  _Out_writes_bytes_(TraceInformationLength) PVOID TraceInformation,
  _In_ ULONG TraceInformationLength,
  _Out_opt_ PULONG RequiredLength,
  _In_opt_ PVOID Buffer);

#if 0
/* FIXME: Get va_list from where? */
_IRQL_requires_max_(HIGH_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
WmiTraceMessageVa(
  _In_ TRACEHANDLE LoggerHandle,
  _In_ ULONG MessageFlags,
  _In_ LPGUID MessageGuid,
  _In_ USHORT MessageNumber,
  _In_ va_list MessageArgList);
#endif

#endif /* (NTDDI_VERSION >= NTDDI_WINXP) */

#ifndef TRACE_INFORMATION_CLASS_DEFINE

#if (NTDDI_VERSION >= NTDDI_WINXP)
_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
WmiQueryTraceInformation(
  _In_ TRACE_INFORMATION_CLASS TraceInformationClass,
  _Out_writes_bytes_(TraceInformationLength) PVOID TraceInformation,
  _In_ ULONG TraceInformationLength,
  _Out_opt_ PULONG RequiredLength,
  _In_opt_ PVOID Buffer);
#endif

#define TRACE_INFORMATION_CLASS_DEFINE

#endif /* TRACE_INFOPRMATION_CLASS_DEFINE */

#if (NTDDI_VERSION >= NTDDI_VISTA)

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwRegister(
  _In_ LPCGUID ProviderId,
  _In_opt_ PETWENABLECALLBACK EnableCallback,
  _In_opt_ PVOID CallbackContext,
  _Out_ PREGHANDLE RegHandle);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwUnregister(
  _In_ REGHANDLE RegHandle);

_IRQL_requires_max_(HIGH_LEVEL)
BOOLEAN
NTKERNELAPI
NTAPI
EtwEventEnabled(
  _In_ REGHANDLE RegHandle,
  _In_ PCEVENT_DESCRIPTOR EventDescriptor);

_IRQL_requires_max_(HIGH_LEVEL)
BOOLEAN
NTKERNELAPI
NTAPI
EtwProviderEnabled(
  _In_ REGHANDLE RegHandle,
  _In_ UCHAR Level,
  _In_ ULONGLONG Keyword);

_When_(ControlCode==EVENT_ACTIVITY_CTRL_CREATE_ID, _IRQL_requires_max_(HIGH_LEVEL))
_When_(ControlCode!=EVENT_ACTIVITY_CTRL_CREATE_ID, _IRQL_requires_max_(APC_LEVEL))
NTSTATUS
NTKERNELAPI
NTAPI
EtwActivityIdControl(
  _In_ ULONG ControlCode,
  _Inout_updates_bytes_(sizeof(GUID)) LPGUID ActivityId);

_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
NTKRNLVISTAAPI
NTAPI
EtwWrite(
  _In_ REGHANDLE RegHandle,
  _In_ PCEVENT_DESCRIPTOR EventDescriptor,
  _In_opt_ LPCGUID ActivityId,
  _In_ ULONG UserDataCount,
  _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData);

_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
NTKERNELAPI
NTAPI
EtwWriteTransfer(
  _In_ REGHANDLE RegHandle,
  _In_ PCEVENT_DESCRIPTOR EventDescriptor,
  _In_opt_ LPCGUID ActivityId,
  _In_opt_ LPCGUID RelatedActivityId,
  _In_ ULONG UserDataCount,
  _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData);

_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
NTKERNELAPI
NTAPI
EtwWriteString(
  _In_ REGHANDLE RegHandle,
  _In_ UCHAR Level,
  _In_ ULONGLONG Keyword,
  _In_opt_ LPCGUID ActivityId,
  _In_ PCWSTR String);

#endif /* (NTDDI_VERSION >= NTDDI_VISTA) */

#if (NTDDI_VERSION >= NTDDI_WIN7)
_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
NTKERNELAPI
NTAPI
EtwWriteEx(
  _In_ REGHANDLE RegHandle,
  _In_ PCEVENT_DESCRIPTOR EventDescriptor,
  _In_ ULONG64 Filter,
  _In_ ULONG Flags,
  _In_opt_ LPCGUID ActivityId,
  _In_opt_ LPCGUID RelatedActivityId,
  _In_ ULONG UserDataCount,
  _In_reads_opt_(UserDataCount) PEVENT_DATA_DESCRIPTOR UserData);
#endif

#if (NTDDI_VERSION >= NTDDI_WIN7)
_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
PcwRegister(
    _Outptr_ PPCW_REGISTRATION *Registration,
    _In_ PPCW_REGISTRATION_INFORMATION Info
    );
#endif

#if (NTDDI_VERSION >= NTDDI_WIN7)
_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
PcwUnregister(
    _In_ PPCW_REGISTRATION Registration
    );
#endif

#if (NTDDI_VERSION >= NTDDI_WIN7)
_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
PcwCreateInstance(
    _Outptr_ PPCW_INSTANCE *Instance,
    _In_ PPCW_REGISTRATION Registration,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG Count,
    _In_reads_(Count) PPCW_DATA Data
    );
#endif

#if (NTDDI_VERSION >= NTDDI_WIN7)
_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
PcwCloseInstance(
    _In_ PPCW_INSTANCE Instance
    );
#endif

#if (NTDDI_VERSION >= NTDDI_WIN7)
_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
PcwAddInstance(
    _In_ PPCW_BUFFER Buffer,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG Id,
    _In_ ULONG Count,
    _In_reads_(Count) PPCW_DATA Data
    );
#endif

