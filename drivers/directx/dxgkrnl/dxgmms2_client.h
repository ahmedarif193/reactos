/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgkrnl-side client for the typed dxgmms2 provider ABI v3 boundary
 */

#ifndef _DXGKRNL_DXGMMS2_CLIENT_H_
#define _DXGKRNL_DXGMMS2_CLIENT_H_

#include <reactos/drivers/directx/dxgmms2.h>

NTSTATUS DxgkpMms2Initialize(VOID);
NTSTATUS DxgkpMms2Uninitialize(VOID);
NTSTATUS DxgkpMms2CreateAdapter(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG AdapterFlags, _Out_ DXGMMS2_ADAPTER_HANDLE *Mms2Adapter);
NTSTATUS DxgkpMms2StartAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _In_ ULONG MiniportDdiVersion, _In_ ULONG RequestedWddmVersion, _In_ ULONG NodeCount, _In_ ULONG SegmentCount, _In_ ULONG AdapterFlags, _In_ ULONG SchedulingCaps, _Out_ PULONGLONG EnabledSubsystems, _Out_ PULONG HighestCompleteWddmVersion, _Out_ PBOOLEAN ProviderStarted);
NTSTATUS DxgkpMms2BeginStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _In_ DXGMMS2_STOP_REASON Reason);
NTSTATUS DxgkpMms2CompleteStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _In_ DXGMMS2_STOP_REASON Reason);
NTSTATUS DxgkpMms2DestroyAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter);
NTSTATUS DxgkpMms2QuerySchedulerTimeline(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _Out_ DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 *TimelineInterface);
NTSTATUS DxgkpMms2QueryContextStreamInterface(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _Out_ DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *ContextStreamInterface);

#endif /* _DXGKRNL_DXGMMS2_CLIENT_H_ */
