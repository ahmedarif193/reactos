/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Private module state for dxgmms2.sys
 */

#ifndef _DXGMMS2_PRIVATE_H_
#define _DXGMMS2_PRIVATE_H_

#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>
#include <reactos/debug.h>
#include <reactos/drivers/directx/dxgmms2.h>
#include "timeline_core.h"
#include "context_stream_core.h"

#define DXGMMS2_REGISTRATION_TAG 'R2mG'
#define DXGMMS2_ADAPTER_TAG      'A2mG'
#define DXGMMS2_REGISTRATION_SIGNATURE 'R2MG'
#define DXGMMS2_ADAPTER_SIGNATURE      'A2MG'

typedef enum _DXGMMS2_ADAPTER_STATE
{
    Dxgmms2AdapterCreated = 0,
    Dxgmms2AdapterStarted = 1,
    Dxgmms2AdapterStopping = 2,
    Dxgmms2AdapterStopped = 3,
    Dxgmms2AdapterDestroying = 4
} DXGMMS2_ADAPTER_STATE;

typedef struct _DXGMMS2_REGISTRATION_CONTEXT
{
    ULONG Signature;
    DXGMMS2_REGISTRATION_HANDLE PublicHandle;
    DXGMMS2_DXGKRNL_INTERFACE_V1 ClientInterface;
    KMUTEX AdapterListMutex;
    LIST_ENTRY AdapterListHead;
    ULONG AdapterCount;
    volatile LONG Unregistering;
} DXGMMS2_REGISTRATION_CONTEXT, *PDXGMMS2_REGISTRATION_CONTEXT;

typedef struct _DXGMMS2_ADAPTER_CONTEXT
{
    ULONG Signature;
    PDXGMMS2_REGISTRATION_CONTEXT Registration;
    LIST_ENTRY RegistrationEntry;
    BOOLEAN RegistrationLinked;
    DXGMMS2_ADAPTER_HANDLE PublicHandle;
    PVOID AdapterCookie;
    ULONG AdapterFlags;
    KMUTEX StateMutex;
    volatile LONG State;
    volatile LONG DestroyClaimed;
    EX_RUNDOWN_REF RundownRef;
    volatile LONG RundownStarted;
    ULONG MiniportDdiVersion;
    ULONG RequestedWddmVersion;
    ULONG NodeCount;
    ULONG SegmentCount;
    ULONG SchedulingCaps;
    ULONGLONG EnabledSubsystems;
    ULONG HighestCompleteWddmVersion;
    ULONG StopReason;
    DXGMMS2_TIMELINE_CONTEXT Timeline;
    DXGMMS2_CONTEXT_STREAM_MANAGER ContextStreamManager;
} DXGMMS2_ADAPTER_CONTEXT, *PDXGMMS2_ADAPTER_CONTEXT;

extern KMUTEX Dxgmms2GlobalMutex;
extern volatile LONG Dxgmms2InitializationState;
extern volatile LONGLONG Dxgmms2NextPublicHandle;
extern PDXGMMS2_REGISTRATION_CONTEXT Dxgmms2ActiveRegistration;

NTSTATUS Dxgmms2EnsureInitialized(VOID);
VOID Dxgmms2AcquireMutex(_Inout_ PKMUTEX Mutex);
VOID Dxgmms2ReleaseMutex(_Inout_ PKMUTEX Mutex);

NTSTATUS NTAPI Dxgmms2CreateAdapter(_In_ DXGMMS2_REGISTRATION_HANDLE Registration, _In_ const DXGMMS2_CREATE_ADAPTER_INFO_V1 *Info, _Out_ DXGMMS2_ADAPTER_HANDLE *Adapter);
NTSTATUS NTAPI Dxgmms2StartAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_START_ADAPTER_INFO_V1 *Info, _Inout_ DXGMMS2_START_ADAPTER_RESULT_V1 *Result);
NTSTATUS NTAPI Dxgmms2BeginStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_STOP_ADAPTER_INFO_V1 *Info);
NTSTATUS NTAPI Dxgmms2CompleteStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_STOP_ADAPTER_INFO_V1 *Info);
NTSTATUS NTAPI Dxgmms2DestroyAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter);
NTSTATUS NTAPI Dxgmms2QuerySchedulerTimelineInterface(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 *TimelineInterface);
NTSTATUS NTAPI Dxgmms2QueryContextStreamInterface(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *ContextStreamInterface);
PDXGMMS2_ADAPTER_CONTEXT Dxgmms2ReferenceAdapterContext(_In_ DXGMMS2_ADAPTER_HANDLE Adapter);
VOID Dxgmms2DereferenceAdapterContext(_In_ PDXGMMS2_ADAPTER_CONTEXT Context);

#endif /* _DXGMMS2_PRIVATE_H_ */
