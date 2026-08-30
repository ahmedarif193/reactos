/*
 * PROJECT:         ReactOS Raspberry Pi 5 HDMI Audio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         BCM2712 HDMI MAI and DMA40 hardware support
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "private.h"

#define HDMI_AUDIO_CHANNEL_MAP 0x0a4
#define HDMI_AUDIO_CONFIG 0x0a8
#define HDMI_AUDIO_PACKET_CONFIG 0x0c0
#define HDMI_INFOFRAME_CONFIG 0x0c4
#define HDMI_INFOFRAME_STATUS 0x0cc
#define HDMI_CRP_CONFIG 0x0d0
#define HDMI_CTS_0 0x0d4
#define HDMI_CTS_1 0x0d8
#define HDMI_SCHEDULER_CONTROL 0x0e8
#define HDMI_MISC_CONTROL 0x114
#define HDMI_DEEP_COLOR_CONFIG_1 0x18c
#define HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE (1u << 1)
#define HDMI_SCHEDULER_CONTROL_MODE_HDMI (1u << 0)
#define HDMI_MISC_CONTROL_PIXEL_REP_MASK 0x0fu
#define HDMI_DEEP_COLOR_CONFIG_1_COLOR_DEPTH_MASK 0x0fu
#define HDMI_COLOR_DEPTH_24BPP 4u

#define HDMI_INFOFRAME_AUDIO_ENABLE (1u << 4)
#define HDMI_INFOFRAME_RAM_ENABLE (1u << 16)
#define HDMI_AUDIO_INFOFRAME_TYPE 0x84
#define HDMI_AUDIO_INFOFRAME_VERSION 1
#define HDMI_AUDIO_INFOFRAME_PAYLOAD_SIZE 10
#define HDMI_INFOFRAME_HEADER_SIZE 4
#define HDMI_PACKET_STRIDE 36

#define HDMI_MAI_CONTROL 0x010
#define HDMI_MAI_THRESHOLD 0x014
#define HDMI_MAI_FORMAT 0x018
#define HDMI_MAI_DATA 0x01c
#define HDMI_MAI_SAMPLE 0x020
#define HDMI_MAI_CONTROL_RESET (1u << 0)
#define HDMI_MAI_CONTROL_OVERFLOW_CLEAR (1u << 1)
#define HDMI_MAI_CONTROL_UNDERFLOW_CLEAR (1u << 2)
#define HDMI_MAI_CONTROL_ENABLE (1u << 3)
#define HDMI_MAI_CONTROL_CHANNEL_COUNT(Value) (((Value) & 0xfu) << 4)
#define HDMI_MAI_CONTROL_FLUSH (1u << 9)
#define HDMI_MAI_CONTROL_WHOLE_SAMPLE (1u << 12)
#define HDMI_MAI_CONTROL_CHANNEL_ALIGN (1u << 13)
#define HDMI_MAI_CONTROL_DATA_LATE_CLEAR (1u << 15)
#define HDMI_MAI_CONTROL_RESET_STATE \
    (HDMI_MAI_CONTROL_RESET | HDMI_MAI_CONTROL_FLUSH | \
     HDMI_MAI_CONTROL_DATA_LATE_CLEAR | \
     HDMI_MAI_CONTROL_UNDERFLOW_CLEAR | \
     HDMI_MAI_CONTROL_OVERFLOW_CLEAR)
#define HDMI_MAI_CONTROL_STREAM_STATE \
    (HDMI_MAI_CONTROL_CHANNEL_COUNT(RPI5HDMI_CHANNELS) | \
     HDMI_MAI_CONTROL_WHOLE_SAMPLE | HDMI_MAI_CONTROL_CHANNEL_ALIGN | \
     HDMI_MAI_CONTROL_ENABLE)
#define HDMI_MAI_FORMAT_AUDIO_PCM (2u << 16)
#define HDMI_MAI_FORMAT_SAMPLE_RATE_48000 (9u << 8)
#define HDMI_MAI_CONFIG_FORMAT_REVERSE (1u << 27)
#define HDMI_MAI_CONFIG_BIT_REVERSE (1u << 26)
#define HDMI_MAI_CONFIG_CHANNEL_MASK(Value) ((Value) & 0xffu)
#define HDMI_AUDIO_PACKET_ZERO_SAMPLE_FLAT (1u << 29)
#define HDMI_AUDIO_PACKET_ZERO_INACTIVE (1u << 24)
#define HDMI_AUDIO_PACKET_B_FRAME_ID(Value) (((Value) & 0xfu) << 10)
#define HDMI_AUDIO_PACKET_CEA_MASK(Value) ((Value) & 0xffu)
#define HDMI_CRP_EXTERNAL_CTS_ENABLE (1u << 24)
#define RPI5_HDMI_MAI_THRESHOLD ((0x10u << 23) | (0x10u << 15) | \
                                 (0x1cu << 7) | 0x1cu)
#define RPI5_HDMI_MAI_SAMPLE_108MHZ_48KHZ (2250u << 8)
#define RPI5_HDMI_ACR_N_48KHZ 6144u
#define RPI5_HDMI_PIXEL_CLOCK_MIN 1000000u
#define RPI5_HDMI_PIXEL_CLOCK_MAX 600000000u
#define HDMI0_VIDEO_CONTROL 0x044
#define HDMI_VIDEO_CONTROL_ENABLE (1u << 31)

#define HDMI_TX_PHY_PLL_VCOCLK_DIV 0x02c
#define HDMI_TX_PHY_PLL_VCOCLK_DIV_ENABLE (1u << 10)
#define HDMI_TX_PHY_PLL_VCOCLK_DIV_MASK 0x3ffu
#define HDMI_RM_OFFSET 0x018
#define HDMI_RM_OFFSET_ONLY (1u << 31)
#define HDMI_RM_OFFSET_MASK 0x7fffffffu
#define RPI5_HDMI_OSCILLATOR_FREQUENCY 54000000u
#define RPI5_HDMI_RM_FRACTION_BITS 21u
#define RPI5_HDMI_VCO_CLOCK_MULTIPLIER 10u

#define DVP_MISC_CONFIG 0x008
#define DVP_HDMI0_AUDIO_CLOCK_DISABLE (1u << 3)

#define DMA40_CONTROL_STATUS 0x000
#define DMA40_CONTROL_BLOCK 0x004
#define DMA40_DEBUG 0x00c
#define DMA40_SOURCE_ADDRESS 0x014
#define DMA40_SOURCE_INFORMATION 0x018

#define DMA40_CS_ACTIVE (1u << 0)
#define DMA40_CS_INTERRUPT (1u << 2)
#define DMA40_CS_TRANSACTIONS (1u << 25)
#define DMA40_CS_PROT ((1u << 8) | (1u << 9))
#define DMA40_CS_QOS(Value) (((Value) & 0x1fu) << 16)
#define DMA40_CS_PANIC_QOS(Value) (((Value) & 0x1fu) << 20)
#define DMA40_CS_START (DMA40_CS_ACTIVE | DMA40_CS_PROT | \
                        DMA40_CS_QOS(10) | DMA40_CS_PANIC_QOS(15))
#define DMA40_DEBUG_RESET (1u << 23)

#define DMA40_TI_INTERRUPT (1u << 0)
#define DMA40_TI_WAIT_RESPONSE (1u << 2)
#define DMA40_TI_PERIPHERAL_MAP(Value) (((Value) & 0x1fu) << 9)
#define DMA40_TI_DESTINATION_DREQ (1u << 15)
#define DMA40_TI_HDMI_AUDIO(RequestLine) \
    (DMA40_TI_INTERRUPT | DMA40_TI_WAIT_RESPONSE | \
     DMA40_TI_PERIPHERAL_MAP(RequestLine) | DMA40_TI_DESTINATION_DREQ)
#define DMA40_ADDRESS_BURST_LENGTH(Value) (((Value) & 0xfu) << 8)
#define DMA40_ADDRESS_INCREMENT (1u << 12)
#define DMA40_ADDRESS_SIZE_128 (2u << 13)

#define IEC958_PREAMBLE_Z 0x08u
#define IEC958_PREAMBLE_X 0x02u
#define IEC958_PREAMBLE_Y 0x04u

static ULONG
ReadRegister(PVOID Base, ULONG Offset)
{
    return READ_REGISTER_ULONG(reinterpret_cast<PULONG>(reinterpret_cast<PUCHAR>(Base) + Offset));
}

static BOOLEAN
Rpi5HdmiGetPixelClock(
    PVOID CoreRegisters,
    PVOID PhyRegisters,
    PVOID RateManagerRegisters,
    PULONG PixelClock)
{
    ULONG DeepColorConfig;
    ULONG Divider;
    ULONG DividerRegister;
    ULONG MiscControl;
    ULONG RateManagerOffset;
    ULONG RateManagerRegister;
    ULONG Scale;
    ULONGLONG PixelClock64;

    DeepColorConfig = ReadRegister(CoreRegisters, HDMI_DEEP_COLOR_CONFIG_1);
    MiscControl = ReadRegister(CoreRegisters, HDMI_MISC_CONTROL);
    DividerRegister = ReadRegister(PhyRegisters, HDMI_TX_PHY_PLL_VCOCLK_DIV);
    RateManagerRegister = ReadRegister(RateManagerRegisters, HDMI_RM_OFFSET);
    /* Supported RGB8 modes use zero (implicit) or four (explicit 24-bit). */
    DeepColorConfig &= HDMI_DEEP_COLOR_CONFIG_1_COLOR_DEPTH_MASK;
    if ((DeepColorConfig && DeepColorConfig != HDMI_COLOR_DEPTH_24BPP) ||
        (MiscControl & HDMI_MISC_CONTROL_PIXEL_REP_MASK))
    {
        return FALSE;
    }

    Divider = DividerRegister & HDMI_TX_PHY_PLL_VCOCLK_DIV_MASK;
    if (!(DividerRegister & HDMI_TX_PHY_PLL_VCOCLK_DIV_ENABLE) || !Divider)
        return FALSE;

    RateManagerOffset = RateManagerRegister & HDMI_RM_OFFSET_MASK;
    if (!(RateManagerRegister & HDMI_RM_OFFSET_ONLY) || !RateManagerOffset)
        return FALSE;

    Scale = Divider * RPI5_HDMI_VCO_CLOCK_MULTIPLIER;
    /* Undo the VC6 rate-manager fixed-point encoding and PHY VCO divider. */
    PixelClock64 = static_cast<ULONGLONG>(RateManagerOffset) *
                   RPI5_HDMI_OSCILLATOR_FREQUENCY;
    PixelClock64 = (PixelClock64 +
                    (1u << (RPI5_HDMI_RM_FRACTION_BITS - 1))) >>
                   RPI5_HDMI_RM_FRACTION_BITS;
    PixelClock64 = (PixelClock64 + Scale / 2) / Scale;
    if (PixelClock64 < RPI5_HDMI_PIXEL_CLOCK_MIN ||
        PixelClock64 > RPI5_HDMI_PIXEL_CLOCK_MAX)
    {
        return FALSE;
    }

    *PixelClock = static_cast<ULONG>(PixelClock64);
    return TRUE;
}

static VOID
WriteRegister(PVOID Base, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG(reinterpret_cast<PULONG>(reinterpret_cast<PUCHAR>(Base) + Offset), Value);
}

/* Even parity over subframe bits 4..30. XOR-folded rather than bit-walked:
   this runs once per sample from the DMA completion DPC. */
static ULONG
Rpi5HdmiIec958Parity(ULONG Subframe)
{
    Subframe = (Subframe >> 4) & 0x07ffffffu;
    Subframe ^= Subframe >> 16;
    Subframe ^= Subframe >> 8;
    Subframe ^= Subframe >> 4;
    Subframe ^= Subframe >> 2;
    Subframe ^= Subframe >> 1;

    return Subframe & 1;
}

CRpi5HdmiAdapter::CRpi5HdmiAdapter()
    : m_DeviceObject(NULL),
      m_CoreRegisters(NULL),
      m_CoreRegistersLength(0),
      m_PacketRegisters(NULL),
      m_PacketRegistersLength(0),
      m_HdRegisters(NULL),
      m_HdRegistersLength(0),
      m_DmaRegisters(NULL),
      m_DmaRegistersLength(0),
      m_DmaRequestLine(0),
      m_DvpRegisters(NULL),
      m_DvpRegistersLength(0),
      m_PhyRegisters(NULL),
      m_PhyRegistersLength(0),
      m_RateManagerRegisters(NULL),
      m_RateManagerRegistersLength(0),
      m_AudioClockOwned(FALSE),
      m_InterruptSync(NULL),
      m_PendingInterrupts(0),
      m_Running(0),
      m_StreamOpen(0),
      m_Mute(FALSE),
      m_AudioBufferMdl(NULL),
      m_AudioBuffer(NULL),
      m_AudioBufferSize(0),
      m_NotificationCount(0),
      m_PeriodBytes(0),
      m_ShadowBuffer(NULL),
      m_ShadowBufferSize(0),
      m_ControlBlocks(NULL),
      m_ControlBlocksSize(0),
      m_HalfIndex(0),
      m_Iec958FrameCounter(0),
      m_PendingConversionPeriod(-1),
      m_NotificationEvent(NULL)
{
    for (ULONG Channel = 0; Channel < RPI5HDMI_CHANNELS; ++Channel)
    {
        m_VolumeLevel[Channel] = RPI5HDMI_VOLUME_MAXIMUM;
        m_VolumeGain[Channel] = 0x10000;
    }
    m_HdPhysicalAddress.QuadPart = 0;
    m_ShadowPhysicalAddress.QuadPart = 0;
    m_ControlBlocksPhysicalAddress.QuadPart = 0;
    KeInitializeDpc(&m_Dpc, DpcRoutine, this);
    KeInitializeSpinLock(&m_EventLock);
}

CRpi5HdmiAdapter::~CRpi5HdmiAdapter()
{
    Stop();
    if (m_InterruptSync)
    {
        m_InterruptSync->Disconnect();
        m_InterruptSync->Release();
        m_InterruptSync = NULL;
    }
    UnmapResources();
}

NTSTATUS
NTAPI
CRpi5HdmiAdapter::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown))
    {
        *Interface = static_cast<PUNKNOWN>(this);
        AddRef();
        return STATUS_SUCCESS;
    }
    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
CRpi5HdmiAdapter::Initialize(PDEVICE_OBJECT DeviceObject, PRESOURCELIST ResourceList)
{
    NTSTATUS Status;

    if (!DeviceObject || !ResourceList)
        return STATUS_INVALID_PARAMETER;

    m_DeviceObject = DeviceObject;
    Status = MapResources(ResourceList);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PcNewInterruptSync(&m_InterruptSync, NULL, ResourceList, 0, InterruptSyncModeNormal);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = m_InterruptSync->RegisterServiceRoutine(InterruptService, this, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    return m_InterruptSync->Connect();
}

/* Single definition of the register block order shared by map and unmap. */
VOID
CRpi5HdmiAdapter::CollectRegisterBlocks(PVOID **Mappings, PULONG *Lengths)
{
    Mappings[0] = &m_CoreRegisters;
    Mappings[1] = &m_PacketRegisters;
    Mappings[2] = &m_HdRegisters;
    Mappings[3] = &m_DmaRegisters;
    Mappings[4] = &m_DvpRegisters;
    Mappings[5] = &m_PhyRegisters;
    Mappings[6] = &m_RateManagerRegisters;

    Lengths[0] = &m_CoreRegistersLength;
    Lengths[1] = &m_PacketRegistersLength;
    Lengths[2] = &m_HdRegistersLength;
    Lengths[3] = &m_DmaRegistersLength;
    Lengths[4] = &m_DvpRegistersLength;
    Lengths[5] = &m_PhyRegistersLength;
    Lengths[6] = &m_RateManagerRegistersLength;
}

NTSTATUS
CRpi5HdmiAdapter::MapResources(PRESOURCELIST ResourceList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PVOID *Mappings[RPI5HDMI_REGISTER_BLOCK_COUNT];
    PULONG Lengths[RPI5HDMI_REGISTER_BLOCK_COUNT];
    const ULONG MinimumLengths[] = {0x300, 0x200, 0x100, 0x100,
                                    0x10, 0x300, 0x80};

    CollectRegisterBlocks(Mappings, Lengths);

    if (ResourceList->NumberOfMemories() < RTL_NUMBER_OF(Mappings) ||
        ResourceList->NumberOfInterrupts() < 1 ||
        ResourceList->NumberOfDmas() != 1)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Only the request line and transfer width are consumed here; the channel
       register window arrives as its own memory resource, so the firmware is
       free to place HDMI audio on any DMA40 channel. */
    Descriptor = ResourceList->FindTranslatedDma(0);
    if (!Descriptor ||
        !(Descriptor->Flags & CM_RESOURCE_DMA_V3) ||
        Descriptor->u.DmaV3.RequestLine > 31 ||
        Descriptor->u.DmaV3.TransferWidth != Width32Bits)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    m_DmaRequestLine = Descriptor->u.DmaV3.RequestLine;

    for (ULONG Index = 0; Index < RTL_NUMBER_OF(Mappings); ++Index)
    {
        Descriptor = ResourceList->FindTranslatedMemory(Index);
        if (!Descriptor || Descriptor->u.Memory.Length < MinimumLengths[Index])
        {
            UnmapResources();
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        *Lengths[Index] = Descriptor->u.Memory.Length;
        *Mappings[Index] = MmMapIoSpace(Descriptor->u.Memory.Start, Descriptor->u.Memory.Length, MmNonCached);
        if (!*Mappings[Index])
        {
            UnmapResources();
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (Index == 2)
            m_HdPhysicalAddress = Descriptor->u.Memory.Start;
    }

    return STATUS_SUCCESS;
}

VOID
CRpi5HdmiAdapter::UnmapResources()
{
    PVOID *Mappings[RPI5HDMI_REGISTER_BLOCK_COUNT];
    PULONG Lengths[RPI5HDMI_REGISTER_BLOCK_COUNT];

    CollectRegisterBlocks(Mappings, Lengths);

    /* Release in reverse mapping order. */
    for (ULONG Index = RTL_NUMBER_OF(Mappings); Index-- > 0;)
    {
        if (*Mappings[Index])
        {
            MmUnmapIoSpace(*Mappings[Index], *Lengths[Index]);
            *Mappings[Index] = NULL;
        }
    }

    m_DmaRequestLine = 0;
}

BOOLEAN
CRpi5HdmiAdapter::EnableAudioClock()
{
    ULONG Value;

    if (!m_DvpRegisters)
        return FALSE;

    Value = ReadRegister(m_DvpRegisters, DVP_MISC_CONFIG);
    m_AudioClockOwned = (Value & DVP_HDMI0_AUDIO_CLOCK_DISABLE) != 0;
    if (m_AudioClockOwned)
    {
        WriteRegister(m_DvpRegisters, DVP_MISC_CONFIG,
                      Value & ~DVP_HDMI0_AUDIO_CLOCK_DISABLE);
    }

    KeMemoryBarrier();
    Value = ReadRegister(m_DvpRegisters, DVP_MISC_CONFIG);
    return (Value & DVP_HDMI0_AUDIO_CLOCK_DISABLE) == 0;
}

VOID
CRpi5HdmiAdapter::DisableAudioClock()
{
    ULONG Value;

    if (!m_DvpRegisters || !m_AudioClockOwned)
        return;

    Value = ReadRegister(m_DvpRegisters, DVP_MISC_CONFIG);
    WriteRegister(m_DvpRegisters, DVP_MISC_CONFIG,
                  Value | DVP_HDMI0_AUDIO_CLOCK_DISABLE);
    m_AudioClockOwned = FALSE;
}

BOOLEAN
CRpi5HdmiAdapter::ClaimStream()
{
    return InterlockedCompareExchange(&m_StreamOpen, 1, 0) == 0;
}

VOID
CRpi5HdmiAdapter::ReleaseStream()
{
    InterlockedExchange(&m_StreamOpen, 0);
}

NTSTATUS
CRpi5HdmiAdapter::GetVolume(ULONG Channel, PLONG Level)
{
    if (Channel >= RPI5HDMI_CHANNELS || !Level)
        return STATUS_INVALID_PARAMETER;

    *Level = InterlockedCompareExchange(&m_VolumeLevel[Channel], 0, 0);
    return STATUS_SUCCESS;
}

NTSTATUS
CRpi5HdmiAdapter::SetVolume(ULONG Channel, LONG Level)
{
    ULONG AttenuationSteps;
    LONG QuantizedLevel;
    LONG Gain;

    if (Channel >= RPI5HDMI_CHANNELS)
        return STATUS_INVALID_PARAMETER;

    if (Level > RPI5HDMI_VOLUME_MAXIMUM)
        Level = RPI5HDMI_VOLUME_MAXIMUM;
    else if (Level < RPI5HDMI_VOLUME_MINIMUM)
        Level = RPI5HDMI_VOLUME_MINIMUM;

    AttenuationSteps = (static_cast<ULONG>(-Level) + RPI5HDMI_VOLUME_STEP / 2) /
                       RPI5HDMI_VOLUME_STEP;
    QuantizedLevel = -static_cast<LONG>(AttenuationSteps * RPI5HDMI_VOLUME_STEP);
    Gain = 0x10000 >> AttenuationSteps;

    InterlockedExchange(&m_VolumeGain[Channel], Gain);
    InterlockedExchange(&m_VolumeLevel[Channel], QuantizedLevel);
    return STATUS_SUCCESS;
}

BOOLEAN
CRpi5HdmiAdapter::GetMute()
{
    return InterlockedCompareExchange(&m_Mute, 0, 0) != 0;
}

VOID
CRpi5HdmiAdapter::SetMute(BOOLEAN Mute)
{
    InterlockedExchange(&m_Mute, Mute ? TRUE : FALSE);
}

NTSTATUS
CRpi5HdmiAdapter::AllocateBuffer(
    PPORTWAVERTSTREAM PortStream,
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS BoundaryAddress;

    if (!PortStream || !AudioBufferMdl || !ActualSize || !OffsetFromFirstPage || !CacheType)
        return STATUS_INVALID_PARAMETER;
    if (m_AudioBufferMdl)
        return STATUS_DEVICE_BUSY;
    if (NotificationCount < 2 || NotificationCount > 64 ||
        RequestedSize < NotificationCount * RPI5HDMI_BLOCK_ALIGN ||
        RequestedSize > RPI5HDMI_MAX_BUFFER_SIZE ||
        RequestedSize % NotificationCount != 0 ||
        (RequestedSize / NotificationCount) % RPI5HDMI_BLOCK_ALIGN != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = 0xffffffffULL;
    BoundaryAddress.QuadPart = 0;

    m_AudioBufferMdl = PortStream->AllocatePagesForMdl(HighAddress, RequestedSize);
    if (!m_AudioBufferMdl)
        return STATUS_INSUFFICIENT_RESOURCES;

    m_AudioBuffer = PortStream->MapAllocatedPages(m_AudioBufferMdl, MmCached);
    if (!m_AudioBuffer)
        goto Failure;

    m_AudioBufferSize = RequestedSize;
    m_NotificationCount = NotificationCount;
    m_PeriodBytes = RequestedSize / NotificationCount;
    m_ShadowBufferSize = RequestedSize * 2;
    m_ShadowBuffer = MmAllocateContiguousMemorySpecifyCache(
        m_ShadowBufferSize, LowAddress, HighAddress, BoundaryAddress, MmNonCached);
    if (!m_ShadowBuffer)
        goto Failure;
    m_ShadowPhysicalAddress = MmGetPhysicalAddress(m_ShadowBuffer);

    m_ControlBlocksSize = NotificationCount * 2 * sizeof(RPI5HDMI_DMA_CONTROL_BLOCK);
    m_ControlBlocks = reinterpret_cast<PRPI5HDMI_DMA_CONTROL_BLOCK>(MmAllocateContiguousMemorySpecifyCache(
        m_ControlBlocksSize, LowAddress, HighAddress, BoundaryAddress, MmNonCached));
    if (!m_ControlBlocks)
        goto Failure;
    m_ControlBlocksPhysicalAddress = MmGetPhysicalAddress(m_ControlBlocks);

    RtlZeroMemory(m_AudioBuffer, RequestedSize);
    RtlZeroMemory(m_ShadowBuffer, m_ShadowBufferSize);
    RtlZeroMemory(m_ControlBlocks, m_ControlBlocksSize);
    BuildControlBlocks();

    *AudioBufferMdl = m_AudioBufferMdl;
    *ActualSize = RequestedSize;
    *OffsetFromFirstPage = 0;
    *CacheType = MmCached;
    return STATUS_SUCCESS;

Failure:
    if (m_ControlBlocks)
    {
        MmFreeContiguousMemorySpecifyCache(m_ControlBlocks, m_ControlBlocksSize, MmNonCached);
        m_ControlBlocks = NULL;
    }
    if (m_ShadowBuffer)
    {
        MmFreeContiguousMemorySpecifyCache(m_ShadowBuffer, m_ShadowBufferSize, MmNonCached);
        m_ShadowBuffer = NULL;
    }
    if (m_AudioBuffer)
    {
        PortStream->UnmapAllocatedPages(m_AudioBuffer, m_AudioBufferMdl);
        m_AudioBuffer = NULL;
    }
    PortStream->FreePagesFromMdl(m_AudioBufferMdl);
    m_AudioBufferMdl = NULL;
    m_AudioBufferSize = 0;
    m_NotificationCount = 0;
    m_PeriodBytes = 0;
    m_ShadowBufferSize = 0;
    m_ControlBlocksSize = 0;
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID
CRpi5HdmiAdapter::FreeBuffer(PPORTWAVERTSTREAM PortStream, PMDL AudioBufferMdl)
{
    Stop();
    if (!PortStream || !m_AudioBufferMdl || AudioBufferMdl != m_AudioBufferMdl)
        return;

    if (m_ControlBlocks)
        MmFreeContiguousMemorySpecifyCache(m_ControlBlocks, m_ControlBlocksSize, MmNonCached);
    if (m_ShadowBuffer)
        MmFreeContiguousMemorySpecifyCache(m_ShadowBuffer, m_ShadowBufferSize, MmNonCached);
    if (m_AudioBuffer)
        PortStream->UnmapAllocatedPages(m_AudioBuffer, m_AudioBufferMdl);
    PortStream->FreePagesFromMdl(m_AudioBufferMdl);

    m_ControlBlocks = NULL;
    m_ControlBlocksSize = 0;
    m_ControlBlocksPhysicalAddress.QuadPart = 0;
    m_ShadowBuffer = NULL;
    m_ShadowBufferSize = 0;
    m_ShadowPhysicalAddress.QuadPart = 0;
    m_AudioBuffer = NULL;
    m_AudioBufferMdl = NULL;
    m_AudioBufferSize = 0;
    m_NotificationCount = 0;
    m_PeriodBytes = 0;
}

VOID
CRpi5HdmiAdapter::BuildControlBlocks()
{
    ULONGLONG DestinationAddress = m_HdPhysicalAddress.QuadPart + HDMI_MAI_DATA;
    ULONG HalfCount = m_NotificationCount * 2;
    ULONG HalfShadowBytes = m_PeriodBytes;

    for (ULONG Index = 0; Index < HalfCount; ++Index)
    {
        ULONGLONG SourceAddress = m_ShadowPhysicalAddress.QuadPart +
                                  static_cast<ULONGLONG>(Index) * HalfShadowBytes;
        ULONGLONG NextAddress = m_ControlBlocksPhysicalAddress.QuadPart +
                                static_cast<ULONGLONG>((Index + 1) % HalfCount) *
                                    sizeof(RPI5HDMI_DMA_CONTROL_BLOCK);
        PRPI5HDMI_DMA_CONTROL_BLOCK ControlBlock = &m_ControlBlocks[Index];

        ControlBlock->TransferInformation = DMA40_TI_HDMI_AUDIO(m_DmaRequestLine);
        ControlBlock->SourceAddress = static_cast<ULONG>(SourceAddress);
        ControlBlock->SourceInformation = static_cast<ULONG>(SourceAddress >> 32) |
                                          DMA40_ADDRESS_BURST_LENGTH(3) |
                                          DMA40_ADDRESS_INCREMENT |
                                          DMA40_ADDRESS_SIZE_128;
        ControlBlock->DestinationAddress = static_cast<ULONG>(DestinationAddress);
        ControlBlock->DestinationInformation = static_cast<ULONG>(DestinationAddress >> 32) |
                                               DMA40_ADDRESS_BURST_LENGTH(3);
        ControlBlock->TransferLength = HalfShadowBytes;
        ControlBlock->NextControlBlock = static_cast<ULONG>(NextAddress >> 5);
    }
    KeMemoryBarrier();
}

VOID
CRpi5HdmiAdapter::ConvertPeriod(ULONG Period)
{
    /*
     * Match alsa-lib's vc4-hdmi.conf and pcm_iec958.c at
     * a48bbb2322fefc7eff9dca061517e935e98cb6a5. The fixed format is
     * consumer PCM, 48 kHz, 16-bit, with Z/X/Y preambles and even parity.
     */
    static const UCHAR ChannelStatus[24] = {0x04, 0x82, 0x00, 0x02, 0x02};
    PSHORT Input;
    PULONG Output;
    ULONG SampleCount;
    ULONG FrameCounter;
    LONG Gain[RPI5HDMI_CHANNELS];

    if (!m_AudioBuffer || !m_ShadowBuffer || Period >= m_NotificationCount)
        return;

    Input = reinterpret_cast<PSHORT>(reinterpret_cast<PUCHAR>(m_AudioBuffer) + Period * m_PeriodBytes);
    Output = reinterpret_cast<PULONG>(reinterpret_cast<PUCHAR>(m_ShadowBuffer) + Period * m_PeriodBytes * 2);
    SampleCount = m_PeriodBytes / sizeof(USHORT);
    FrameCounter = m_Iec958FrameCounter;

    if (GetMute())
    {
        RtlZeroMemory(Gain, sizeof(Gain));
    }
    else
    {
        for (ULONG Channel = 0; Channel < RPI5HDMI_CHANNELS; ++Channel)
            Gain[Channel] = InterlockedCompareExchange(&m_VolumeGain[Channel], 0, 0);
    }

    for (ULONG Index = 0; Index < SampleCount; ++Index)
    {
        ULONG Channel = Index % RPI5HDMI_CHANNELS;
        ULONG FrameIndex = FrameCounter;
        ULONG ByteIndex = FrameIndex >> 3;
        ULONG BitIndex = FrameIndex & 7;
        ULONG Subframe;
        LONG Sample;

        Sample = static_cast<LONG>((static_cast<LONGLONG>(Input[Index]) *
                                    Gain[Channel]) >> 16);
        Subframe = static_cast<ULONG>(static_cast<USHORT>(static_cast<SHORT>(Sample))) << 12;
        if (ChannelStatus[ByteIndex] & (1u << BitIndex))
            Subframe |= 1u << 30;
        if (Rpi5HdmiIec958Parity(Subframe))
            Subframe |= 1u << 31;

        if (Channel)
            Subframe |= IEC958_PREAMBLE_Y;
        else if (!FrameIndex)
            Subframe |= IEC958_PREAMBLE_Z;
        else
            Subframe |= IEC958_PREAMBLE_X;

        Output[Index] = Subframe;
        if (Channel == RPI5HDMI_CHANNELS - 1)
            FrameCounter = (FrameCounter + 1) % 192;
    }
    m_Iec958FrameCounter = FrameCounter;
    KeMemoryBarrier();
}

NTSTATUS
CRpi5HdmiAdapter::ProgramHdmiAudio()
{
    UCHAR InfoFrame[HDMI_PACKET_STRIDE] = {0};
    ULONGLONG Cts64;
    ULONG Cts;
    ULONG InfoFrameConfig;
    ULONG InfoFrameOffset = (HDMI_AUDIO_INFOFRAME_TYPE - 0x80) *
                            HDMI_PACKET_STRIDE;
    ULONG InfoFrameEnd = InfoFrameOffset + HDMI_PACKET_STRIDE;
    ULONG InfoFrameLength = HDMI_INFOFRAME_HEADER_SIZE +
                            HDMI_AUDIO_INFOFRAME_PAYLOAD_SIZE;
    ULONG PixelClock;
    UCHAR Checksum = 0;

    if (!Rpi5HdmiGetPixelClock(m_CoreRegisters,
                               m_PhyRegisters,
                               m_RateManagerRegisters,
                               &PixelClock))
    {
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    Cts64 = static_cast<ULONGLONG>(PixelClock) *
            RPI5_HDMI_ACR_N_48KHZ /
            (128u * RPI5HDMI_SAMPLE_RATE);
    if (!Cts64 || Cts64 > 0xfffffu)
        return STATUS_DEVICE_HARDWARE_ERROR;
    Cts = static_cast<ULONG>(Cts64);

    WriteRegister(m_HdRegisters,
                  HDMI_MAI_CONTROL,
                  HDMI_MAI_CONTROL_RESET_STATE);

    /*
     * Keep the prepare sequence identical to vc4_hdmi_audio_prepare() in
     * Linux vc4_hdmi.c at 95d9c0c7f20ab1b49ac88773a6138b16d2b8f061.
     */
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_SAMPLE,
                  RPI5_HDMI_MAI_SAMPLE_108MHZ_48KHZ);
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_CONTROL,
                  HDMI_MAI_CONTROL_STREAM_STATE);
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_FORMAT,
                  HDMI_MAI_FORMAT_AUDIO_PCM |
                      HDMI_MAI_FORMAT_SAMPLE_RATE_48000);
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_THRESHOLD,
                  RPI5_HDMI_MAI_THRESHOLD);
    WriteRegister(m_CoreRegisters,
                  HDMI_AUDIO_CONFIG,
                  HDMI_MAI_CONFIG_BIT_REVERSE |
                      HDMI_MAI_CONFIG_FORMAT_REVERSE |
                      HDMI_MAI_CONFIG_CHANNEL_MASK(0x3));
    WriteRegister(m_CoreRegisters, HDMI_AUDIO_CHANNEL_MAP, 0x00000010);
    WriteRegister(m_CoreRegisters,
                  HDMI_AUDIO_PACKET_CONFIG,
                  HDMI_AUDIO_PACKET_ZERO_SAMPLE_FLAT |
                      HDMI_AUDIO_PACKET_ZERO_INACTIVE |
                      HDMI_AUDIO_PACKET_B_FRAME_ID(0x8) |
                      HDMI_AUDIO_PACKET_CEA_MASK(0x3));
    WriteRegister(m_CoreRegisters,
                  HDMI_CRP_CONFIG,
                  HDMI_CRP_EXTERNAL_CTS_ENABLE |
                      RPI5_HDMI_ACR_N_48KHZ);
    WriteRegister(m_CoreRegisters, HDMI_CTS_0, Cts);
    WriteRegister(m_CoreRegisters, HDMI_CTS_1, Cts);

    InfoFrameConfig = ReadRegister(m_CoreRegisters, HDMI_INFOFRAME_CONFIG);
    WriteRegister(m_CoreRegisters,
                  HDMI_INFOFRAME_CONFIG,
                  (InfoFrameConfig | HDMI_INFOFRAME_RAM_ENABLE) &
                      ~HDMI_INFOFRAME_AUDIO_ENABLE);
    ULONG Retry;

    for (Retry = 0; Retry < 1000; ++Retry)
    {
        if (!(ReadRegister(m_CoreRegisters, HDMI_INFOFRAME_STATUS) & HDMI_INFOFRAME_AUDIO_ENABLE))
            break;
        KeStallExecutionProcessor(100);
    }

    if (Retry == 1000)
        return STATUS_IO_TIMEOUT;

    /*
     * Match Linux hdmi_audio_infoframe_pack_only() and
     * vc4_hdmi_write_infoframe(): HB0..HB2, checksum, then the CEA payload,
     * stored by the VC4 packet RAM as groups of three and four bytes.
     */
    InfoFrame[0] = HDMI_AUDIO_INFOFRAME_TYPE;
    InfoFrame[1] = HDMI_AUDIO_INFOFRAME_VERSION;
    InfoFrame[2] = HDMI_AUDIO_INFOFRAME_PAYLOAD_SIZE;
    InfoFrame[4] = RPI5HDMI_CHANNELS - 1;
    for (ULONG Index = 0; Index < InfoFrameLength; ++Index)
        Checksum = static_cast<UCHAR>(Checksum + InfoFrame[Index]);
    InfoFrame[3] = static_cast<UCHAR>(0 - Checksum);

    for (ULONG Index = 0; Index < InfoFrameLength; Index += 7)
    {
        WriteRegister(m_PacketRegisters,
                      InfoFrameOffset,
                      static_cast<ULONG>(InfoFrame[Index]) |
                          (static_cast<ULONG>(InfoFrame[Index + 1]) << 8) |
                          (static_cast<ULONG>(InfoFrame[Index + 2]) << 16));
        InfoFrameOffset += sizeof(ULONG);
        WriteRegister(m_PacketRegisters,
                      InfoFrameOffset,
                      static_cast<ULONG>(InfoFrame[Index + 3]) |
                          (static_cast<ULONG>(InfoFrame[Index + 4]) << 8) |
                          (static_cast<ULONG>(InfoFrame[Index + 5]) << 16) |
                          (static_cast<ULONG>(InfoFrame[Index + 6]) << 24));
        InfoFrameOffset += sizeof(ULONG);
    }
    while (InfoFrameOffset < InfoFrameEnd)
    {
        WriteRegister(m_PacketRegisters, InfoFrameOffset, 0);
        InfoFrameOffset += sizeof(ULONG);
    }
    WriteRegister(m_CoreRegisters,
                  HDMI_INFOFRAME_CONFIG,
                  InfoFrameConfig | HDMI_INFOFRAME_RAM_ENABLE |
                      HDMI_INFOFRAME_AUDIO_ENABLE);
    for (Retry = 0; Retry < 1000; ++Retry)
    {
        if (ReadRegister(m_CoreRegisters, HDMI_INFOFRAME_STATUS) & HDMI_INFOFRAME_AUDIO_ENABLE)
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(100);
    }

    WriteRegister(m_CoreRegisters, HDMI_INFOFRAME_CONFIG, InfoFrameConfig | HDMI_INFOFRAME_RAM_ENABLE);
    return STATUS_IO_TIMEOUT;
}

VOID
CRpi5HdmiAdapter::DisableHdmiAudio()
{
    ULONG Value;

    if (!m_CoreRegisters || !m_HdRegisters)
        return;

    /* vc4_hdmi_audio_shutdown(), followed by vc4_hdmi_audio_reset(). */
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_CONTROL,
                  HDMI_MAI_CONTROL_DATA_LATE_CLEAR |
                      HDMI_MAI_CONTROL_UNDERFLOW_CLEAR |
                      HDMI_MAI_CONTROL_OVERFLOW_CLEAR);
    Value = ReadRegister(m_CoreRegisters, HDMI_INFOFRAME_CONFIG);
    WriteRegister(m_CoreRegisters,
                  HDMI_INFOFRAME_CONFIG,
                  Value & ~HDMI_INFOFRAME_AUDIO_ENABLE);
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_CONTROL,
                  HDMI_MAI_CONTROL_RESET);
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_CONTROL,
                  HDMI_MAI_CONTROL_OVERFLOW_CLEAR);
    WriteRegister(m_HdRegisters,
                  HDMI_MAI_CONTROL,
                  HDMI_MAI_CONTROL_FLUSH);
}

VOID
CRpi5HdmiAdapter::ResetDma()
{
    ULONG ControlStatus;

    if (!m_DmaRegisters)
        return;

    if (ReadRegister(m_DmaRegisters, DMA40_CONTROL_BLOCK))
    {
        ControlStatus = ReadRegister(m_DmaRegisters, DMA40_CONTROL_STATUS);
        WriteRegister(m_DmaRegisters, DMA40_CONTROL_STATUS,
                      ControlStatus & ~DMA40_CS_ACTIVE);
        for (ULONG Retry = 0; Retry < 1000; ++Retry)
        {
            if (!(ReadRegister(m_DmaRegisters, DMA40_CONTROL_STATUS) &
                  DMA40_CS_TRANSACTIONS))
            {
                break;
            }
            KeStallExecutionProcessor(1);
        }
    }

    WriteRegister(m_DmaRegisters, DMA40_CONTROL_STATUS, DMA40_CS_PROT);
    WriteRegister(m_DmaRegisters, DMA40_DEBUG,
                  ReadRegister(m_DmaRegisters, DMA40_DEBUG) | DMA40_DEBUG_RESET);
}

NTSTATUS
NTAPI
CRpi5HdmiAdapter::StartSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    CRpi5HdmiAdapter *Adapter = static_cast<CRpi5HdmiAdapter *>(Context);
    UNREFERENCED_PARAMETER(InterruptSync);

    Adapter->ResetDma();
    Adapter->m_HalfIndex = 0;
    Adapter->m_PendingConversionPeriod = -1;
    InterlockedExchange(&Adapter->m_PendingInterrupts, 0);
    InterlockedExchange(&Adapter->m_Running, 1);
    KeMemoryBarrier();
    WriteRegister(
        Adapter->m_DmaRegisters,
        DMA40_CONTROL_BLOCK,
        static_cast<ULONG>(Adapter->m_ControlBlocksPhysicalAddress.QuadPart >> 5));
    WriteRegister(Adapter->m_DmaRegisters, DMA40_CONTROL_STATUS, DMA40_CS_START);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CRpi5HdmiAdapter::StopSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    CRpi5HdmiAdapter *Adapter = static_cast<CRpi5HdmiAdapter *>(Context);
    UNREFERENCED_PARAMETER(InterruptSync);

    InterlockedExchange(&Adapter->m_Running, 0);
    Adapter->ResetDma();
    return STATUS_SUCCESS;
}

NTSTATUS
CRpi5HdmiAdapter::SetStreamState(KSSTATE State)
{
    NTSTATUS Status;
    ULONG SchedulerControl;
    ULONG VideoControl;

    if (State != KSSTATE_RUN)
    {
        Stop();
        return STATUS_SUCCESS;
    }
    if (!m_AudioBufferMdl || !m_ControlBlocks || !m_InterruptSync)
        return STATUS_INVALID_DEVICE_STATE;
    if (InterlockedCompareExchange(&m_Running, 1, 1))
        return STATUS_SUCCESS;

    SchedulerControl = ReadRegister(m_CoreRegisters, HDMI_SCHEDULER_CONTROL);
    VideoControl = ReadRegister(m_HdRegisters, HDMI0_VIDEO_CONTROL);
    if ((SchedulerControl &
         (HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE |
          HDMI_SCHEDULER_CONTROL_MODE_HDMI)) !=
            (HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE |
             HDMI_SCHEDULER_CONTROL_MODE_HDMI) ||
        !(VideoControl & HDMI_VIDEO_CONTROL_ENABLE))
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    m_Iec958FrameCounter = 0;
    for (ULONG Period = 0; Period < m_NotificationCount; ++Period)
        ConvertPeriod(Period);
    if (!EnableAudioClock())
        return STATUS_DEVICE_HARDWARE_ERROR;
    Status = ProgramHdmiAudio();
    if (!NT_SUCCESS(Status))
    {
        DisableHdmiAudio();
        DisableAudioClock();
        return Status;
    }

    Status = m_InterruptSync->CallSynchronizedRoutine(StartSynchronized, this);
    if (!NT_SUCCESS(Status))
    {
        DisableHdmiAudio();
        DisableAudioClock();
    }
    return Status;
}

VOID
CRpi5HdmiAdapter::Stop()
{
    if (m_InterruptSync)
        m_InterruptSync->CallSynchronizedRoutine(StopSynchronized, this);
    else
    {
        InterlockedExchange(&m_Running, 0);
        ResetDma();
    }

    KeRemoveQueueDpc(&m_Dpc);
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        KeFlushQueuedDpcs();
    InterlockedExchange(&m_PendingInterrupts, 0);
    DisableHdmiAudio();
    DisableAudioClock();
}

NTSTATUS
NTAPI
CRpi5HdmiAdapter::InterruptService(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    CRpi5HdmiAdapter *Adapter = static_cast<CRpi5HdmiAdapter *>(Context);
    ULONG ControlStatus;
    UNREFERENCED_PARAMETER(InterruptSync);

    ControlStatus = ReadRegister(Adapter->m_DmaRegisters, DMA40_CONTROL_STATUS);
    if (!(ControlStatus & DMA40_CS_INTERRUPT))
        return STATUS_UNSUCCESSFUL;

    if (InterlockedCompareExchange(&Adapter->m_Running, 1, 1))
    {
        WriteRegister(Adapter->m_DmaRegisters, DMA40_CONTROL_STATUS,
                      DMA40_CS_START | DMA40_CS_INTERRUPT);
        InterlockedIncrement(&Adapter->m_PendingInterrupts);
        KeInsertQueueDpc(&Adapter->m_Dpc, NULL, NULL);
    }
    else
    {
        WriteRegister(Adapter->m_DmaRegisters, DMA40_CONTROL_STATUS,
                      DMA40_CS_PROT | DMA40_CS_INTERRUPT);
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
CRpi5HdmiAdapter::DpcRoutine(
    PRKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2)
{
    CRpi5HdmiAdapter *Adapter = static_cast<CRpi5HdmiAdapter *>(DeferredContext);
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    Adapter->ProcessInterrupts();
}

VOID
CRpi5HdmiAdapter::ProcessInterrupts()
{
    LONG InterruptCount;

    while ((InterruptCount = InterlockedExchange(&m_PendingInterrupts, 0)) != 0)
    {
        while (InterruptCount-- > 0 && InterlockedCompareExchange(&m_Running, 1, 1))
        {
            ULONG Period = m_HalfIndex / 2;

            if ((m_HalfIndex & 1) == 0)
            {
                if (m_PendingConversionPeriod >= 0)
                {
                    ConvertPeriod(static_cast<ULONG>(m_PendingConversionPeriod));
                    m_PendingConversionPeriod = -1;
                }
            }
            else
            {
                KIRQL OldIrql;
                m_PendingConversionPeriod = static_cast<LONG>(Period);
                KeAcquireSpinLock(&m_EventLock, &OldIrql);
                if (m_NotificationEvent)
                    KeSetEvent(m_NotificationEvent, IO_NO_INCREMENT, FALSE);
                KeReleaseSpinLock(&m_EventLock, OldIrql);
            }

            m_HalfIndex = (m_HalfIndex + 1) % (m_NotificationCount * 2);
        }
    }
}

NTSTATUS
CRpi5HdmiAdapter::GetPosition(PKSAUDIO_POSITION Position)
{
    ULONGLONG Offset;
    ULONGLONG SourceAddress;

    if (!Position)
        return STATUS_INVALID_PARAMETER;
    if (!m_AudioBufferMdl || !m_AudioBufferSize)
        return STATUS_INVALID_DEVICE_STATE;

    SourceAddress = ReadRegister(m_DmaRegisters, DMA40_SOURCE_ADDRESS);
    SourceAddress |= static_cast<ULONGLONG>(
                         ReadRegister(m_DmaRegisters,
                                      DMA40_SOURCE_INFORMATION) & 0xffu)
                     << 32;
    if (SourceAddress >= static_cast<ULONGLONG>(m_ShadowPhysicalAddress.QuadPart) &&
        SourceAddress < static_cast<ULONGLONG>(m_ShadowPhysicalAddress.QuadPart) +
                            m_ShadowBufferSize)
    {
        Offset = (SourceAddress - m_ShadowPhysicalAddress.QuadPart) / 2;
    }
    else
    {
        Offset = static_cast<ULONGLONG>(m_HalfIndex) * m_PeriodBytes / 2;
    }
    Offset %= m_AudioBufferSize;
    Position->PlayOffset = Offset;
    Position->WriteOffset = Offset;
    return STATUS_SUCCESS;
}

NTSTATUS
CRpi5HdmiAdapter::RegisterNotificationEvent(PKEVENT NotificationEvent)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!NotificationEvent)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&m_EventLock, &OldIrql);
    if (!m_NotificationEvent)
        m_NotificationEvent = NotificationEvent;
    else if (m_NotificationEvent != NotificationEvent)
        Status = STATUS_DEVICE_BUSY;
    KeReleaseSpinLock(&m_EventLock, OldIrql);
    return Status;
}

NTSTATUS
CRpi5HdmiAdapter::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    KeAcquireSpinLock(&m_EventLock, &OldIrql);
    if (m_NotificationEvent == NotificationEvent)
        m_NotificationEvent = NULL;
    else if (m_NotificationEvent)
        Status = STATUS_NOT_FOUND;
    KeReleaseSpinLock(&m_EventLock, OldIrql);
    return Status;
}
