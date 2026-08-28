/*
 * PROJECT:         ReactOS Raspberry Pi 5 HDMI Audio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         WaveRT render stream
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "private.h"

CRpi5HdmiStream::CRpi5HdmiStream(CRpi5HdmiAdapter *Adapter, PPORTWAVERTSTREAM PortStream)
    : m_Adapter(Adapter), m_PortStream(PortStream), m_AudioBufferMdl(NULL), m_State(KSSTATE_STOP)
{
    m_Adapter->AddRef();
    m_PortStream->AddRef();
}

CRpi5HdmiStream::~CRpi5HdmiStream()
{
    m_Adapter->SetStreamState(KSSTATE_STOP);
    if (m_AudioBufferMdl)
        m_Adapter->FreeBuffer(m_PortStream, m_AudioBufferMdl);
    m_PortStream->Release();
    m_Adapter->ReleaseStream();
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CRpi5HdmiStream::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRTStream) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRTStreamNotification))
    {
        *Interface = static_cast<PMINIPORTWAVERTSTREAMNOTIFICATION>(this);
        AddRef();
        return STATUS_SUCCESS;
    }

    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
NTAPI
CRpi5HdmiStream::SetFormat(PKSDATAFORMAT DataFormat)
{
    if (m_State == KSSTATE_RUN)
        return STATUS_INVALID_DEVICE_STATE;
    return Rpi5HdmiIsFormatSupported(DataFormat) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CRpi5HdmiStream::SetState(KSSTATE State)
{
    NTSTATUS Status;

    if (State < KSSTATE_STOP || State > KSSTATE_RUN)
        return STATUS_INVALID_PARAMETER;

    Status = m_Adapter->SetStreamState(State);
    if (NT_SUCCESS(Status))
        m_State = State;
    return Status;
}

NTSTATUS
NTAPI
CRpi5HdmiStream::GetPosition(PKSAUDIO_POSITION Position)
{
    return m_Adapter->GetPosition(Position);
}

NTSTATUS
NTAPI
CRpi5HdmiStream::AllocateAudioBuffer(
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    UNREFERENCED_PARAMETER(RequestedSize);
    UNREFERENCED_PARAMETER(AudioBufferMdl);
    UNREFERENCED_PARAMETER(ActualSize);
    UNREFERENCED_PARAMETER(OffsetFromFirstPage);
    UNREFERENCED_PARAMETER(CacheType);
    return STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
CRpi5HdmiStream::FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(AudioBufferMdl);
    UNREFERENCED_PARAMETER(BufferSize);
}

VOID
NTAPI
CRpi5HdmiStream::GetHWLatency(PKSRTAUDIO_HWLATENCY HardwareLatency)
{
    if (!HardwareLatency)
        return;
    HardwareLatency->FifoSize = 64;
    HardwareLatency->ChipsetDelay = 0;
    HardwareLatency->CodecDelay = 0;
}

NTSTATUS
NTAPI
CRpi5HdmiStream::GetPositionRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CRpi5HdmiStream::GetClockRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CRpi5HdmiStream::AllocateBufferWithNotification(
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    NTSTATUS Status;

    if (m_AudioBufferMdl)
        return STATUS_DEVICE_BUSY;

    Status = m_Adapter->AllocateBuffer(
        m_PortStream,
        NotificationCount,
        RequestedSize,
        AudioBufferMdl,
        ActualSize,
        OffsetFromFirstPage,
        CacheType);
    if (NT_SUCCESS(Status))
        m_AudioBufferMdl = *AudioBufferMdl;
    return Status;
}

VOID
NTAPI
CRpi5HdmiStream::FreeBufferWithNotification(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(BufferSize);

    if (m_AudioBufferMdl && AudioBufferMdl == m_AudioBufferMdl)
    {
        m_Adapter->FreeBuffer(m_PortStream, m_AudioBufferMdl);
        m_AudioBufferMdl = NULL;
    }
}

NTSTATUS
NTAPI
CRpi5HdmiStream::RegisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->RegisterNotificationEvent(NotificationEvent);
}

NTSTATUS
NTAPI
CRpi5HdmiStream::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->UnregisterNotificationEvent(NotificationEvent);
}
