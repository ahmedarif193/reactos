/*
 * ntdll.dll Event Tracing Functions
 */

#include <ntdll.h>

#include <wmistr.h>
#include <evntrace.h>

#define NDEBUG
#include <debug.h>

#define FIXME DPRINT1

typedef struct _ETW_CLASSIC_PROVIDER
{
    LIST_ENTRY Entry;
    TRACEHANDLE Handle;
    GUID ControlGuid;
    WMIDPREQUEST Callback;
    PVOID Context;
    GUID ClassGuids[ANYSIZE_ARRAY];
} ETW_CLASSIC_PROVIDER;

static RTL_SRWLOCK EtwpClassicLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY EtwpClassicProviders = { &EtwpClassicProviders, &EtwpClassicProviders };
static TRACEHANDLE EtwpNextClassicHandle;

static ULONG
EtwpRegisterClassicProvider(WMIDPREQUEST Callback, PVOID Context, LPCGUID ControlGuid,
                            ULONG GuidCount, PTRACE_GUID_REGISTRATION TraceGuidReg,
                            PTRACEHANDLE RegistrationHandle)
{
    ETW_CLASSIC_PROVIDER *Provider;
    ULONG i;

    if (!Callback || !ControlGuid || !RegistrationHandle || (GuidCount && !TraceGuidReg))
        return ERROR_INVALID_PARAMETER;
    for (i = 0; i < GuidCount; ++i)
        if (!TraceGuidReg[i].Guid) return ERROR_INVALID_PARAMETER;

    if ((ULONG_PTR)GuidCount > (MAXULONG_PTR - FIELD_OFFSET(ETW_CLASSIC_PROVIDER, ClassGuids)) / sizeof(GUID))
        return ERROR_NOT_ENOUGH_MEMORY;
    Provider = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY,
                              FIELD_OFFSET(ETW_CLASSIC_PROVIDER, ClassGuids) + (SIZE_T)GuidCount * sizeof(GUID));
    if (!Provider) return ERROR_NOT_ENOUGH_MEMORY;
    Provider->ControlGuid = *ControlGuid;
    Provider->Callback = Callback;
    Provider->Context = Context;
    for (i = 0; i < GuidCount; ++i)
        Provider->ClassGuids[i] = *TraceGuidReg[i].Guid;

    RtlAcquireSRWLockExclusive(&EtwpClassicLock);
    Provider->Handle = ++EtwpNextClassicHandle;
    InsertTailList(&EtwpClassicProviders, &Provider->Entry);
    RtlReleaseSRWLockExclusive(&EtwpClassicLock);

    for (i = 0; i < GuidCount; ++i)
        TraceGuidReg[i].RegHandle = &Provider->ClassGuids[i];
    *RegistrationHandle = Provider->Handle;
    /* No trace controller is active, so no enable callback is delivered. */
    return ERROR_SUCCESS;
}

/*
 * @unimplemented
 */
ULONG CDECL
EtwTraceMessage(
    TRACEHANDLE  SessionHandle,
    ULONG        MessageFlags,
    LPCGUID      MessageGuid,
    USHORT       MessageNumber,
    ...)
{
    FIXME("TraceMessage()\n");
    return ERROR_SUCCESS;
}

TRACEHANDLE
NTAPI
EtwGetTraceLoggerHandle(
    PVOID Buffer
)
{
    FIXME("EtwGetTraceLoggerHandle stub()\n");
    return (TRACEHANDLE)-1;
}


ULONG
NTAPI
EtwTraceEvent(
    TRACEHANDLE SessionHandle,
    PEVENT_TRACE_HEADER EventTrace
)
{
    FIXME("EtwTraceEvent stub()\n");

    if (!SessionHandle || !EventTrace)
    {
        /* invalid parameters */
        return ERROR_INVALID_PARAMETER;
    }

    if (EventTrace->Size != sizeof(EVENT_TRACE_HEADER))
    {
        /* invalid parameter */
        return ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
}

ULONG
NTAPI
EtwGetTraceEnableFlags(
    TRACEHANDLE TraceHandle
)
{
    /* There are currently no enabled trace sessions. */
    return 0;
}

UCHAR
NTAPI
EtwGetTraceEnableLevel(
    TRACEHANDLE TraceHandle
)
{
    return 0;
}

ULONG
NTAPI
EtwUnregisterTraceGuids(
    TRACEHANDLE RegistrationHandle
)
{
    PLIST_ENTRY Entry;
    ETW_CLASSIC_PROVIDER *Provider;

    RtlAcquireSRWLockExclusive(&EtwpClassicLock);
    for (Entry = EtwpClassicProviders.Flink; Entry != &EtwpClassicProviders; Entry = Entry->Flink)
    {
        Provider = CONTAINING_RECORD(Entry, ETW_CLASSIC_PROVIDER, Entry);
        if (Provider->Handle == RegistrationHandle)
        {
            RemoveEntryList(Entry);
            RtlReleaseSRWLockExclusive(&EtwpClassicLock);
            RtlFreeHeap(RtlGetProcessHeap(), 0, Provider);
            return ERROR_SUCCESS;
        }
    }
    RtlReleaseSRWLockExclusive(&EtwpClassicLock);
    return ERROR_INVALID_HANDLE;
}

ULONG
NTAPI
EtwRegisterTraceGuidsA(
    WMIDPREQUEST RequestAddress,
    PVOID RequestContext,
    LPCGUID ControlGuid,
    ULONG GuidCount,
    PTRACE_GUID_REGISTRATION TraceGuidReg,
    LPCSTR MofImagePath,
    LPCSTR MofResourceName,
    PTRACEHANDLE RegistrationHandle
)
{
    return EtwpRegisterClassicProvider(RequestAddress, RequestContext, ControlGuid,
                                       GuidCount, TraceGuidReg, RegistrationHandle);
}

ULONG
NTAPI
EtwRegisterTraceGuidsW(
    WMIDPREQUEST RequestAddress,
    PVOID RequestContext,
    LPCGUID ControlGuid,
    ULONG GuidCount,
    PTRACE_GUID_REGISTRATION TraceGuidReg,
    LPCWSTR MofImagePath,
    LPCWSTR MofResourceName,
    PTRACEHANDLE RegistrationHandle
)
{
    return EtwpRegisterClassicProvider(RequestAddress, RequestContext, ControlGuid,
                                       GuidCount, TraceGuidReg, RegistrationHandle);
}

ULONG WINAPI EtwStartTraceW( PTRACEHANDLE pSessionHandle, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    FIXME("(%p, %s, %p) stub\n", pSessionHandle, SessionName, Properties);
    if (pSessionHandle) *pSessionHandle = 0xcafe4242;
    return ERROR_SUCCESS;
}

ULONG WINAPI EtwStartTraceA( PTRACEHANDLE pSessionHandle, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    FIXME("(%p, %s, %p) stub\n", pSessionHandle, SessionName, Properties);
    if (pSessionHandle) *pSessionHandle = 0xcafe4242;
    return ERROR_SUCCESS;
}

/******************************************************************************
 * EtwControlTraceW [NTDLL.@]
 *
 * Control a givel event trace session
 *
 */
ULONG WINAPI EtwControlTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties, ULONG control )
{
    FIXME("(%I64x, %s, %p, %d) stub\n", hSession, SessionName, Properties, control);
    return ERROR_SUCCESS;
}

/******************************************************************************
 * EtwControlTraceA [NTDLL.@]
 *
 * See ControlTraceW.
 *
 */
ULONG WINAPI EtwControlTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties, ULONG control )
{
    FIXME("(%I64x, %s, %p, %d) stub\n", hSession, SessionName, Properties, control);
    return ERROR_SUCCESS;
}

/******************************************************************************
 * EtwEnableTrace [NTDLL.@]
 */
ULONG WINAPI EtwEnableTrace( ULONG enable, ULONG flag, ULONG level, LPCGUID guid, TRACEHANDLE hSession )
{
    FIXME("(%d, 0x%x, %d, %p, %I64x): stub\n", enable, flag, level,
            guid, hSession);

    return ERROR_SUCCESS;
}

/******************************************************************************
 * EtwQueryAllTracesW [NTDLL.@]
 *
 * Query information for started event trace sessions
 *
 */
ULONG WINAPI EtwQueryAllTracesW( PEVENT_TRACE_PROPERTIES * parray, ULONG arraycount, PULONG psessioncount )
{
    FIXME("(%p, %d, %p) stub\n", parray, arraycount, psessioncount);

    if (psessioncount) *psessioncount = 0;
    return ERROR_SUCCESS;
}

/******************************************************************************
 * QueryAllTracesA [NTDLL.@]
 *
 * See EtwQueryAllTracesA.
 */
ULONG WINAPI EtwQueryAllTracesA( PEVENT_TRACE_PROPERTIES * parray, ULONG arraycount, PULONG psessioncount )
{
    FIXME("(%p, %d, %p) stub\n", parray, arraycount, psessioncount);

    if (psessioncount) *psessioncount = 0;
    return ERROR_SUCCESS;
}

/******************************************************************************
 * EtwFlushTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwFlushTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_FLUSH );
}

/******************************************************************************
 * EtwFlushTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwFlushTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_FLUSH );
}

/******************************************************************************
 * EtwQueryTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwQueryTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_QUERY );
}

/******************************************************************************
 * EtwQueryTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwQueryTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_QUERY );
}

/******************************************************************************
 * EtwStopTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwStopTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_STOP );
}

/******************************************************************************
 * EtwStopTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwStopTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_STOP );
}

/******************************************************************************
 * EtwUpdateTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwUpdateTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_UPDATE );
}

/******************************************************************************
 * EtwUpdateTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwUpdateTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_UPDATE );
}

/* EOF */
