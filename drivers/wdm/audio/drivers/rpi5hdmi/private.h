/*
 * PROJECT:         ReactOS Raspberry Pi 5 HDMI Audio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Internal driver definitions
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <ntddk.h>
#include <ks.h>
#include <portcls.h>
#include <ksmedia.h>

#define TAG_RPI5HDMI '5iPR'
#define RPI5HDMI_SAMPLE_RATE 48000
#define RPI5HDMI_CHANNELS 2
#define RPI5HDMI_BITS_PER_SAMPLE 16
#define RPI5HDMI_BLOCK_ALIGN 4
#define RPI5HDMI_MAX_BUFFER_SIZE (256 * 1024)
#define RPI5HDMI_VOLUME_MINIMUM (-96 * 0x10000)
#define RPI5HDMI_VOLUME_MAXIMUM 0
#define RPI5HDMI_VOLUME_STEP (6 * 0x10000)

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag);

template <typename Interface> class CUnknownImpl : public Interface
{
  private:
    volatile LONG m_RefCount;

  protected:
    CUnknownImpl() : m_RefCount(0)
    {
    }

    virtual ~CUnknownImpl()
    {
    }

  public:
    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        ULONG RefCount = InterlockedDecrement(&m_RefCount);
        if (!RefCount)
            delete this;
        return RefCount;
    }
};

typedef struct DECLSPEC_ALIGN(32) _RPI5HDMI_DMA_CONTROL_BLOCK
{
    ULONG TransferInformation;
    ULONG SourceAddress;
    ULONG SourceInformation;
    ULONG DestinationAddress;
    ULONG DestinationInformation;
    ULONG TransferLength;
    ULONG NextControlBlock;
    ULONG Reserved;
} RPI5HDMI_DMA_CONTROL_BLOCK, *PRPI5HDMI_DMA_CONTROL_BLOCK;

class CRpi5HdmiAdapter : public CUnknownImpl<IUnknown>
{
  public:
    CRpi5HdmiAdapter();
    virtual ~CRpi5HdmiAdapter();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);

    NTSTATUS Initialize(PDEVICE_OBJECT DeviceObject, PRESOURCELIST ResourceList);
    VOID Stop();

    BOOLEAN ClaimStream();
    VOID ReleaseStream();

    NTSTATUS AllocateBuffer(
        PPORTWAVERTSTREAM PortStream,
        ULONG NotificationCount,
        ULONG RequestedSize,
        PMDL *AudioBufferMdl,
        ULONG *ActualSize,
        ULONG *OffsetFromFirstPage,
        MEMORY_CACHING_TYPE *CacheType);
    VOID FreeBuffer(PPORTWAVERTSTREAM PortStream, PMDL AudioBufferMdl);
    NTSTATUS SetStreamState(KSSTATE State);
    NTSTATUS GetPosition(PKSAUDIO_POSITION Position);
    NTSTATUS RegisterNotificationEvent(PKEVENT NotificationEvent);
    NTSTATUS UnregisterNotificationEvent(PKEVENT NotificationEvent);
    NTSTATUS GetVolume(ULONG Channel, PLONG Level);
    NTSTATUS SetVolume(ULONG Channel, LONG Level);
    BOOLEAN GetMute();
    VOID SetMute(BOOLEAN Mute);

  private:
    static NTSTATUS NTAPI InterruptService(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI StartSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI StopSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static VOID NTAPI DpcRoutine(PRKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);

    NTSTATUS MapResources(PRESOURCELIST ResourceList);
    VOID UnmapResources();
    BOOLEAN EnableAudioClock();
    VOID DisableAudioClock();
    NTSTATUS ProgramHdmiAudio();
    VOID DisableHdmiAudio();
    VOID ResetDma();
    VOID BuildControlBlocks();
    VOID ConvertPeriod(ULONG Period);
    VOID ProcessInterrupts();

    PDEVICE_OBJECT m_DeviceObject;
    PVOID m_CoreRegisters;
    ULONG m_CoreRegistersLength;
    PVOID m_PacketRegisters;
    ULONG m_PacketRegistersLength;
    PVOID m_HdRegisters;
    ULONG m_HdRegistersLength;
    PVOID m_DmaRegisters;
    ULONG m_DmaRegistersLength;
    PVOID m_DvpRegisters;
    ULONG m_DvpRegistersLength;
    BOOLEAN m_AudioClockOwned;
    PHYSICAL_ADDRESS m_HdPhysicalAddress;

    PINTERRUPTSYNC m_InterruptSync;
    KDPC m_Dpc;
    KSPIN_LOCK m_EventLock;
    volatile LONG m_PendingInterrupts;
    volatile LONG m_Running;
    volatile LONG m_StreamOpen;
    volatile LONG m_VolumeLevel[RPI5HDMI_CHANNELS];
    volatile LONG m_VolumeGain[RPI5HDMI_CHANNELS];
    volatile LONG m_Mute;

    PMDL m_AudioBufferMdl;
    PVOID m_AudioBuffer;
    ULONG m_AudioBufferSize;
    ULONG m_NotificationCount;
    ULONG m_PeriodBytes;
    PVOID m_ShadowBuffer;
    ULONG m_ShadowBufferSize;
    PHYSICAL_ADDRESS m_ShadowPhysicalAddress;
    PRPI5HDMI_DMA_CONTROL_BLOCK m_ControlBlocks;
    ULONG m_ControlBlocksSize;
    PHYSICAL_ADDRESS m_ControlBlocksPhysicalAddress;
    ULONG m_HalfIndex;
    ULONG m_Iec958FrameCounter;
    LONG m_PendingConversionPeriod;
    PKEVENT m_NotificationEvent;
};

class CRpi5HdmiTopology : public CUnknownImpl<IMiniportTopology>
{
  public:
    explicit CRpi5HdmiTopology(CRpi5HdmiAdapter *Adapter);
    virtual ~CRpi5HdmiTopology();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportTopology;

    CRpi5HdmiAdapter *GetAdapter() const
    {
        return m_Adapter;
    }

  private:
    CRpi5HdmiAdapter *m_Adapter;
};

class CRpi5HdmiWave : public CUnknownImpl<IMiniportWaveRT>
{
  public:
    explicit CRpi5HdmiWave(CRpi5HdmiAdapter *Adapter);
    virtual ~CRpi5HdmiWave();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRT;

  private:
    CRpi5HdmiAdapter *m_Adapter;
};

class CRpi5HdmiStream : public CUnknownImpl<IMiniportWaveRTStreamNotification>
{
  public:
    CRpi5HdmiStream(CRpi5HdmiAdapter *Adapter, PPORTWAVERTSTREAM PortStream);
    virtual ~CRpi5HdmiStream();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRTStreamNotification;

  private:
    CRpi5HdmiAdapter *m_Adapter;
    PPORTWAVERTSTREAM m_PortStream;
    PMDL m_AudioBufferMdl;
    KSSTATE m_State;
};

typedef struct _RPI5HDMI_DEVICE_EXTENSION
{
    ULONG_PTR PortClassReserved[64];
    CRpi5HdmiAdapter *Adapter;
} RPI5HDMI_DEVICE_EXTENSION, *PRPI5HDMI_DEVICE_EXTENSION;

extern PCFILTER_DESCRIPTOR Rpi5HdmiWaveFilterDescriptor;
extern PCFILTER_DESCRIPTOR Rpi5HdmiTopologyFilterDescriptor;

BOOLEAN
Rpi5HdmiIsFormatSupported(PKSDATAFORMAT DataFormat);

NTSTATUS
Rpi5HdmiCreateTopology(PUNKNOWN *Unknown, CRpi5HdmiAdapter *Adapter);

NTSTATUS
Rpi5HdmiCreateWave(PUNKNOWN *Unknown, CRpi5HdmiAdapter *Adapter);
