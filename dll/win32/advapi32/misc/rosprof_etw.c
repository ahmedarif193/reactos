/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Documented ETW profiling capability facade for RosProf
 *
 * The public RosProf ABI deliberately owns one private stream per device
 * handle.  It has no operation to attach a second process to a session or to
 * enumerate sessions.  Consequently it cannot implement the system-wide ETW
 * controller/consumer contract.  Keep the documented ABI exports available,
 * answer only queries backed by the device, and fail every session operation
 * rather than returning success for a trace that OpenTrace cannot consume.
 */

#include <advapi32.h>
#include <wmistr.h>
#include <evntrace.h>
#include <reactos/rosprof.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);

static ULONG
RosProfEtwOpenDevice(PHANDLE DeviceHandle)
{
    HANDLE Handle;

    Handle = CreateFileW(ROSPROF_WIN32_DEVICE_NAME,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return GetLastError();
    *DeviceHandle = Handle;
    return ERROR_SUCCESS;
}

static ULONG
RosProfEtwQueryCapabilities(PROSPROF_CAPABILITIES_V1 Capabilities)
{
    HANDLE DeviceHandle;
    DWORD BytesReturned;
    ULONG Error;

    Error = RosProfEtwOpenDevice(&DeviceHandle);
    if (Error != ERROR_SUCCESS)
        return Error;
    ZeroMemory(Capabilities, sizeof(*Capabilities));
    if (!DeviceIoControl(DeviceHandle,
                         IOCTL_ROSPROF_QUERY_CAPABILITIES,
                         NULL,
                         0,
                         Capabilities,
                         sizeof(*Capabilities),
                         &BytesReturned,
                         NULL))
    {
        Error = GetLastError();
        CloseHandle(DeviceHandle);
        return Error;
    }
    CloseHandle(DeviceHandle);

    if (BytesReturned < sizeof(*Capabilities) ||
        Capabilities->Header.Size < sizeof(*Capabilities) ||
        Capabilities->Header.MajorVersion != ROSPROF_ABI_VERSION_MAJOR ||
        Capabilities->RecordAlignment != ROSPROF_RECORD_ALIGNMENT)
    {
        return ERROR_INVALID_DATA;
    }
    return ERROR_SUCCESS;
}

static ULONG
RosProfEtwWriteQueryResult(PVOID Buffer,
                           ULONG BufferSize,
                           PULONG ReturnLength,
                           const VOID *Value,
                           ULONG ValueSize)
{
    if (ReturnLength)
        *ReturnLength = ValueSize;
    if (!Buffer || BufferSize < ValueSize)
        return ERROR_INSUFFICIENT_BUFFER;
    CopyMemory(Buffer, Value, ValueSize);
    return ERROR_SUCCESS;
}

static ULONG
RosProfEtwQueryProfileSourceList(PVOID Buffer,
                                 ULONG BufferSize,
                                 PULONG ReturnLength)
{
    static const WCHAR Description[] = L"ReactOS timer sampled profile";
    ROSPROF_CAPABILITIES_V1 Capabilities;
    PPROFILE_SOURCE_INFO SourceInfo;
    ULONG Error, Required;

    Required = FIELD_OFFSET(PROFILE_SOURCE_INFO, Description) +
               sizeof(Description);
    Required = (Required + sizeof(ULONGLONG) - 1) &
               ~(sizeof(ULONGLONG) - 1);
    if (ReturnLength)
        *ReturnLength = Required;
    if (!Buffer || BufferSize < Required)
        return ERROR_INSUFFICIENT_BUFFER;

    Error = RosProfEtwQueryCapabilities(&Capabilities);
    if (Error != ERROR_SUCCESS)
        return Error;
    if (!(Capabilities.Capabilities & ROSPROF_CAP_TIMER_SAMPLE) ||
        !(Capabilities.SupportedSources & ROSPROF_SOURCE_TIMER) ||
        !(Capabilities.SupportedRecordTypes &
          ROSPROF_RECORD_MASK(ROSPROF_RECORD_SAMPLE)))
        return ERROR_NOT_SUPPORTED;

    ZeroMemory(Buffer, Required);
    SourceInfo = Buffer;
    SourceInfo->NextEntryOffset = 0;
    SourceInfo->Source = ProfileTime;
    SourceInfo->MinInterval = Capabilities.MinimumPeriod100ns;
    SourceInfo->MaxInterval = Capabilities.MaximumPeriod100ns;
    CopyMemory(SourceInfo->Description, Description, sizeof(Description));
    return ERROR_SUCCESS;
}

static ULONG
RosProfEtwQueryMaximumPmc(PVOID Buffer,
                          ULONG BufferSize,
                          PULONG ReturnLength)
{
    ROSPROF_CAPABILITIES_V1 Capabilities;
    ULONG Error, Value;

    Error = RosProfEtwQueryCapabilities(&Capabilities);
    if (Error != ERROR_SUCCESS)
        return Error;
    if ((Capabilities.Capabilities & ROSPROF_CAP_PMU_SAMPLE) &&
        (Capabilities.SupportedSources & ROSPROF_SOURCE_PMU) &&
        (Capabilities.SupportedRecordTypes &
         ROSPROF_RECORD_MASK(ROSPROF_RECORD_PMU)))
    {
        Value = Capabilities.MaximumPmuEvents;
    }
    else
    {
        Value = 0;
    }
    return RosProfEtwWriteQueryResult(Buffer,
                                      BufferSize,
                                      ReturnLength,
                                      &Value,
                                      sizeof(Value));
}

ULONG WMIAPI
StartTraceW(PTRACEHANDLE TraceHandle,
            LPCWSTR InstanceName,
            PEVENT_TRACE_PROPERTIES Properties)
{
    UNREFERENCED_PARAMETER(InstanceName);
    UNREFERENCED_PARAMETER(Properties);
    if (!TraceHandle)
        return ERROR_INVALID_PARAMETER;
    *TraceHandle = 0;
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
StartTraceA(PTRACEHANDLE TraceHandle,
            LPCSTR InstanceName,
            PEVENT_TRACE_PROPERTIES Properties)
{
    UNREFERENCED_PARAMETER(InstanceName);
    UNREFERENCED_PARAMETER(Properties);
    if (!TraceHandle)
        return ERROR_INVALID_PARAMETER;
    *TraceHandle = 0;
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
ControlTraceW(TRACEHANDLE TraceHandle,
              LPCWSTR InstanceName,
              PEVENT_TRACE_PROPERTIES Properties,
              ULONG ControlCode)
{
    UNREFERENCED_PARAMETER(TraceHandle);
    UNREFERENCED_PARAMETER(InstanceName);
    UNREFERENCED_PARAMETER(Properties);
    UNREFERENCED_PARAMETER(ControlCode);
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
ControlTraceA(TRACEHANDLE TraceHandle,
              LPCSTR InstanceName,
              PEVENT_TRACE_PROPERTIES Properties,
              ULONG ControlCode)
{
    UNREFERENCED_PARAMETER(TraceHandle);
    UNREFERENCED_PARAMETER(InstanceName);
    UNREFERENCED_PARAMETER(Properties);
    UNREFERENCED_PARAMETER(ControlCode);
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
StopTraceW(TRACEHANDLE TraceHandle,
           LPCWSTR InstanceName,
           PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceW(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_STOP);
}

ULONG WMIAPI
StopTraceA(TRACEHANDLE TraceHandle,
           LPCSTR InstanceName,
           PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceA(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_STOP);
}

ULONG WMIAPI
QueryTraceW(TRACEHANDLE TraceHandle,
            LPCWSTR InstanceName,
            PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceW(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_QUERY);
}

ULONG WMIAPI
QueryTraceA(TRACEHANDLE TraceHandle,
            LPCSTR InstanceName,
            PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceA(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_QUERY);
}

ULONG WMIAPI
UpdateTraceW(TRACEHANDLE TraceHandle,
             LPCWSTR InstanceName,
             PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceW(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_UPDATE);
}

ULONG WMIAPI
UpdateTraceA(TRACEHANDLE TraceHandle,
             LPCSTR InstanceName,
             PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceA(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_UPDATE);
}

ULONG WMIAPI
FlushTraceW(TRACEHANDLE TraceHandle,
            LPCWSTR InstanceName,
            PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceW(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_FLUSH);
}

ULONG WMIAPI
FlushTraceA(TRACEHANDLE TraceHandle,
            LPCSTR InstanceName,
            PEVENT_TRACE_PROPERTIES Properties)
{
    return ControlTraceA(TraceHandle, InstanceName, Properties,
                         EVENT_TRACE_CONTROL_FLUSH);
}

ULONG WMIAPI
QueryAllTracesW(PEVENT_TRACE_PROPERTIES *PropertyArray,
                ULONG PropertyArrayCount,
                PULONG LoggerCount)
{
    UNREFERENCED_PARAMETER(PropertyArray);
    UNREFERENCED_PARAMETER(PropertyArrayCount);
    if (LoggerCount)
        *LoggerCount = 0;
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
QueryAllTracesA(PEVENT_TRACE_PROPERTIES *PropertyArray,
                ULONG PropertyArrayCount,
                PULONG LoggerCount)
{
    UNREFERENCED_PARAMETER(PropertyArray);
    UNREFERENCED_PARAMETER(PropertyArrayCount);
    if (LoggerCount)
        *LoggerCount = 0;
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
TraceSetInformation(TRACEHANDLE SessionHandle,
                    TRACE_INFO_CLASS InformationClass,
                    PVOID TraceInformation,
                    ULONG InformationLength)
{
    UNREFERENCED_PARAMETER(SessionHandle);
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(TraceInformation);
    UNREFERENCED_PARAMETER(InformationLength);
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
TraceQueryInformation(TRACEHANDLE SessionHandle,
                      TRACE_INFO_CLASS InformationClass,
                      PVOID TraceInformation,
                      ULONG InformationLength,
                      PULONG ReturnLength)
{
    if (ReturnLength)
        *ReturnLength = 0;
    if (SessionHandle != 0)
        return ERROR_INVALID_HANDLE;

    switch (InformationClass)
    {
        case TraceProfileSourceListInfo:
            return RosProfEtwQueryProfileSourceList(TraceInformation,
                                                    InformationLength,
                                                    ReturnLength);
        case TraceMaxPmcCounterQuery:
            return RosProfEtwQueryMaximumPmc(TraceInformation,
                                             InformationLength,
                                             ReturnLength);
        default:
            return ERROR_NOT_SUPPORTED;
    }
}

ULONG WMIAPI
EnumerateTraceGuidsEx(TRACE_QUERY_INFO_CLASS InformationClass,
                      PVOID InBuffer,
                      ULONG InBufferSize,
                      PVOID OutBuffer,
                      ULONG OutBufferSize,
                      PULONG ReturnLength)
{
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);

    if (!ReturnLength)
        return ERROR_INVALID_PARAMETER;
    if (InformationClass == TraceProfileSourceListInfo)
        return RosProfEtwQueryProfileSourceList(OutBuffer,
                                                OutBufferSize,
                                                ReturnLength);
    if (InformationClass == TraceMaxPmcCounterQuery)
        return RosProfEtwQueryMaximumPmc(OutBuffer,
                                         OutBufferSize,
                                         ReturnLength);
    *ReturnLength = 0;
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
EnableTrace(ULONG Enable,
            ULONG EnableFlag,
            ULONG EnableLevel,
            LPCGUID ControlGuid,
            TRACEHANDLE TraceHandle)
{
    UNREFERENCED_PARAMETER(Enable);
    UNREFERENCED_PARAMETER(EnableFlag);
    UNREFERENCED_PARAMETER(EnableLevel);
    UNREFERENCED_PARAMETER(ControlGuid);
    UNREFERENCED_PARAMETER(TraceHandle);
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
EnableTraceEx(LPCGUID ProviderId,
              LPCGUID SourceId,
              TRACEHANDLE TraceHandle,
              ULONG IsEnabled,
              UCHAR Level,
              ULONGLONG MatchAnyKeyword,
              ULONGLONG MatchAllKeyword,
              ULONG EnableProperty,
              PEVENT_FILTER_DESCRIPTOR EnableFilterDesc)
{
    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(SourceId);
    UNREFERENCED_PARAMETER(TraceHandle);
    UNREFERENCED_PARAMETER(IsEnabled);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(MatchAnyKeyword);
    UNREFERENCED_PARAMETER(MatchAllKeyword);
    UNREFERENCED_PARAMETER(EnableProperty);
    UNREFERENCED_PARAMETER(EnableFilterDesc);
    return ERROR_NOT_SUPPORTED;
}

ULONG WMIAPI
EnableTraceEx2(TRACEHANDLE TraceHandle,
               LPCGUID ProviderId,
               ULONG ControlCode,
               UCHAR Level,
               ULONGLONG MatchAnyKeyword,
               ULONGLONG MatchAllKeyword,
               ULONG Timeout,
               PENABLE_TRACE_PARAMETERS EnableParameters)
{
    UNREFERENCED_PARAMETER(TraceHandle);
    UNREFERENCED_PARAMETER(ProviderId);
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(MatchAnyKeyword);
    UNREFERENCED_PARAMETER(MatchAllKeyword);
    UNREFERENCED_PARAMETER(Timeout);
    UNREFERENCED_PARAMETER(EnableParameters);
    return ERROR_NOT_SUPPORTED;
}
