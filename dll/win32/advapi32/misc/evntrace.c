/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     advapi32.dll Event tracing stubs
 * COPYRIGHT:   Copyright 2017 Mark Jansen (mark.jansen@reactos.org)
 */

#include <advapi32.h>
#include <wmistr.h>
#include <evntrace.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);


TRACEHANDLE
WINAPI
OpenTraceA(IN PEVENT_TRACE_LOGFILEA Logfile)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_PROCESSTRACE_HANDLE;
}

TRACEHANDLE
WINAPI
OpenTraceW(IN PEVENT_TRACE_LOGFILEW Logfile)
{
    UNIMPLEMENTED;
    SetLastError(ERROR_ACCESS_DENIED);
    return INVALID_PROCESSTRACE_HANDLE;
}

ULONG
WINAPI
ProcessTrace(IN PTRACEHANDLE HandleArray,
             IN ULONG HandleCount,
             IN LPFILETIME StartTime,
             IN LPFILETIME EndTime)
{
    UNIMPLEMENTED;
    return ERROR_NOACCESS;
}

ULONG
WINAPI
ControlTraceA(IN TRACEHANDLE TraceHandle,
              IN LPCSTR InstanceName,
              IN PEVENT_TRACE_PROPERTIES Properties,
              IN ULONG ControlCode)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
ControlTraceW(IN TRACEHANDLE TraceHandle,
              IN LPCWSTR InstanceName,
              IN PEVENT_TRACE_PROPERTIES Properties,
              IN ULONG ControlCode)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
EnableTrace(IN ULONG Enable,
            IN ULONG EnableFlag,
            IN ULONG EnableLevel,
            IN LPCGUID ControlGuid,
            IN TRACEHANDLE TraceHandle)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
EnableTraceEx(IN LPCGUID ProviderId,
              IN LPCGUID SourceId,
              IN TRACEHANDLE TraceHandle,
              IN ULONG IsEnabled,
              IN UCHAR Level,
              IN ULONGLONG MatchAnyKeyword,
              IN ULONGLONG MatchAllKeyword,
              IN ULONG EnableProperty,
              IN PEVENT_FILTER_DESCRIPTOR EnableFilterDesc)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
EnableTraceEx2(IN TRACEHANDLE TraceHandle,
               IN LPCGUID ProviderId,
               IN ULONG ControlCode,
               IN UCHAR Level,
               IN ULONGLONG MatchAnyKeyword,
               IN ULONGLONG MatchAllKeyword,
               IN ULONG Timeout,
               IN PENABLE_TRACE_PARAMETERS EnableParameters)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
GetTraceEnableFlags(IN TRACEHANDLE TraceHandle)
{
    UNIMPLEMENTED;
    return 0;
}

UCHAR
WINAPI
GetTraceEnableLevel(IN TRACEHANDLE TraceHandle)
{
    UNIMPLEMENTED;
    return 0;
}

TRACEHANDLE
WINAPI
GetTraceLoggerHandle(IN PVOID Buffer)
{
    UNIMPLEMENTED;
    return INVALID_PROCESSTRACE_HANDLE;
}

ULONG
WINAPI
RegisterTraceGuidsA(IN WMIDPREQUEST RequestAddress,
                    IN PVOID RequestContext,
                    IN LPCGUID ControlGuid,
                    IN ULONG GuidCount,
                    IN PTRACE_GUID_REGISTRATION TraceGuidReg,
                    IN LPCSTR MofImagePath,
                    IN LPCSTR MofResourceName,
                    IN PTRACEHANDLE RegistrationHandle)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
RegisterTraceGuidsW(IN WMIDPREQUEST RequestAddress,
                    IN PVOID RequestContext,
                    IN LPCGUID ControlGuid,
                    IN ULONG GuidCount,
                    IN PTRACE_GUID_REGISTRATION TraceGuidReg,
                    IN LPCWSTR MofImagePath,
                    IN LPCWSTR MofResourceName,
                    IN PTRACEHANDLE RegistrationHandle)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
TraceEventInstance(IN TRACEHANDLE TraceHandle,
                   IN PEVENT_INSTANCE_HEADER EventTrace,
                   IN PEVENT_INSTANCE_INFO pInstInfo,
                   IN PEVENT_INSTANCE_INFO pParentInstInfo)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

ULONG
WINAPI
UnregisterTraceGuids(IN TRACEHANDLE RegistrationHandle)
{
    UNIMPLEMENTED;
    return ERROR_NOT_SUPPORTED;
}

