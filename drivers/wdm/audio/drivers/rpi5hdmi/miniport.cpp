/*
 * PROJECT:         ReactOS Raspberry Pi 5 HDMI Audio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         WaveRT and topology miniports
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "private.h"

static const ULONG Rpi5HdmiTopologyPinCount = 2;

static KSDATARANGE_AUDIO Rpi5HdmiPcmDataRange =
{
    {
        sizeof(KSDATARANGE_AUDIO),
        0,
        RPI5HDMI_BLOCK_ALIGN,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    RPI5HDMI_CHANNELS,
    RPI5HDMI_BITS_PER_SAMPLE,
    RPI5HDMI_BITS_PER_SAMPLE,
    RPI5HDMI_SAMPLE_RATE,
    RPI5HDMI_SAMPLE_RATE
};

static PKSDATARANGE Rpi5HdmiPcmDataRanges[] =
{
    reinterpret_cast<PKSDATARANGE>(&Rpi5HdmiPcmDataRange)
};

static KSDATARANGE Rpi5HdmiBridgeDataRange =
{
    sizeof(KSDATARANGE),
    0,
    0,
    0,
    STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
    STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
    STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
};

static PKSDATARANGE Rpi5HdmiBridgeDataRanges[] =
{
    &Rpi5HdmiBridgeDataRange
};

/*
 * Emits the KSPROPERTY_TYPE_BASICSUPPORT reply shared by the topology property
 * handlers. Returns STATUS_MORE_ENTRIES when the caller's buffer is large
 * enough for a members list, so the caller can append its own.
 */
static NTSTATUS
Rpi5HdmiBasicSupport(
    PPCPROPERTY_REQUEST PropertyRequest,
    ULONG AccessFlags,
    ULONG PropertyType,
    ULONG DescriptionSize,
    ULONG MembersListCount)
{
    PKSPROPERTY_DESCRIPTION Description;
    ULONG ValueSize = PropertyRequest->ValueSize;

    if (ValueSize < sizeof(ULONG))
    {
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (ValueSize < sizeof(KSPROPERTY_DESCRIPTION))
    {
        *static_cast<PULONG>(PropertyRequest->Value) = AccessFlags;
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    Description = static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);
    RtlZeroMemory(Description, sizeof(*Description));
    Description->AccessFlags = AccessFlags;
    Description->DescriptionSize = DescriptionSize;
    if (PropertyType == VT_ILLEGAL)
    {
        Description->PropTypeSet.Set = GUID_NULL;
        Description->PropTypeSet.Id = 0;
    }
    else
    {
        Description->PropTypeSet.Set = KSPROPTYPESETID_General;
        Description->PropTypeSet.Id = PropertyType;
    }
    Description->MembersListCount = MembersListCount;
    PropertyRequest->ValueSize = sizeof(KSPROPERTY_DESCRIPTION);

    return ValueSize < DescriptionSize ? STATUS_SUCCESS : STATUS_MORE_ENTRIES;
}

static NTSTATUS NTAPI
Rpi5HdmiVolumePropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) +
                                  sizeof(KSPROPERTY_MEMBERSHEADER) +
                                  RPI5HDMI_CHANNELS * sizeof(KSPROPERTY_STEPPING_LONG);
    CRpi5HdmiTopology *Topology;
    CRpi5HdmiAdapter *Adapter;
    ULONG Channel;

    if (!PropertyRequest || PropertyRequest->Node != 0 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        NTSTATUS Status = Rpi5HdmiBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
            VT_I4,
            DescriptionSize,
            1);

        if (Status != STATUS_MORE_ENTRIES)
            return Status;

        PKSPROPERTY_DESCRIPTION Description =
            static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);

        PKSPROPERTY_MEMBERSHEADER Members =
            reinterpret_cast<PKSPROPERTY_MEMBERSHEADER>(Description + 1);
        Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
        Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
        Members->MembersCount = RPI5HDMI_CHANNELS;
        Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL;

        PKSPROPERTY_STEPPING_LONG Range =
            reinterpret_cast<PKSPROPERTY_STEPPING_LONG>(Members + 1);
        for (ULONG Index = 0; Index < RPI5HDMI_CHANNELS; ++Index)
        {
            Range[Index].SteppingDelta = RPI5HDMI_VOLUME_STEP;
            Range[Index].Bounds.SignedMinimum = RPI5HDMI_VOLUME_MINIMUM;
            Range[Index].Bounds.SignedMaximum = RPI5HDMI_VOLUME_MAXIMUM;
        }
        PropertyRequest->ValueSize = DescriptionSize;
        return STATUS_SUCCESS;
    }

    if (PropertyRequest->InstanceSize < sizeof(LONG) ||
        PropertyRequest->ValueSize < sizeof(LONG))
    {
        PropertyRequest->ValueSize = sizeof(LONG);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Channel = *static_cast<PULONG>(PropertyRequest->Instance);
    Topology = static_cast<CRpi5HdmiTopology *>(
        static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget));
    Adapter = Topology->GetAdapter();

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        NTSTATUS Status = Adapter->GetVolume(Channel, static_cast<PLONG>(PropertyRequest->Value));
        if (NT_SUCCESS(Status))
            PropertyRequest->ValueSize = sizeof(LONG);
        return Status;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
        return Adapter->SetVolume(Channel, *static_cast<PLONG>(PropertyRequest->Value));

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS NTAPI
Rpi5HdmiMutePropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) +
                                  sizeof(KSPROPERTY_MEMBERSHEADER) +
                                  RPI5HDMI_CHANNELS * sizeof(KSPROPERTY_STEPPING_LONG);
    CRpi5HdmiTopology *Topology;
    CRpi5HdmiAdapter *Adapter;

    if (!PropertyRequest || PropertyRequest->Node != 1 || !PropertyRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        NTSTATUS Status = Rpi5HdmiBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
            VT_BOOL,
            DescriptionSize,
            1);

        if (Status != STATUS_MORE_ENTRIES)
            return Status;

        PKSPROPERTY_DESCRIPTION Description =
            static_cast<PKSPROPERTY_DESCRIPTION>(PropertyRequest->Value);
        PKSPROPERTY_MEMBERSHEADER Members =
            reinterpret_cast<PKSPROPERTY_MEMBERSHEADER>(Description + 1);
        Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
        Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
        Members->MembersCount = RPI5HDMI_CHANNELS;
        Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_MULTICHANNEL |
                         KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_UNIFORM;

        PKSPROPERTY_STEPPING_LONG Range =
            reinterpret_cast<PKSPROPERTY_STEPPING_LONG>(Members + 1);
        for (ULONG Index = 0; Index < RPI5HDMI_CHANNELS; ++Index)
        {
            Range[Index].SteppingDelta = 1;
            Range[Index].Bounds.SignedMinimum = FALSE;
            Range[Index].Bounds.SignedMaximum = TRUE;
        }

        PropertyRequest->ValueSize = DescriptionSize;
        return STATUS_SUCCESS;
    }

    if (PropertyRequest->ValueSize < sizeof(BOOL))
    {
        PropertyRequest->ValueSize = sizeof(BOOL);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Topology = static_cast<CRpi5HdmiTopology *>(
        static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget));
    Adapter = Topology->GetAdapter();

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        *static_cast<PBOOL>(PropertyRequest->Value) = Adapter->GetMute();
        PropertyRequest->ValueSize = sizeof(BOOL);
        return STATUS_SUCCESS;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        Adapter->SetMute(*static_cast<PBOOL>(PropertyRequest->Value));
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

template <typename Description>
static NTSTATUS
Rpi5HdmiPrepareJackDescription(
    PPCPROPERTY_REQUEST PropertyRequest,
    ULONG PinId,
    PKSMULTIPLE_ITEM *MultipleItem,
    Description **JackDescription)
{
    ULONG DescriptionCount;
    ULONG RequiredSize;

    if (PinId >= Rpi5HdmiTopologyPinCount)
        return STATUS_INVALID_PARAMETER;

    DescriptionCount = PinId == 1 ? 1 : 0;
    RequiredSize = sizeof(KSMULTIPLE_ITEM) +
                   DescriptionCount * sizeof(Description);

    if (!PropertyRequest->ValueSize)
    {
        PropertyRequest->ValueSize = RequiredSize;
        return STATUS_BUFFER_OVERFLOW;
    }

    if (PropertyRequest->ValueSize < RequiredSize)
    {
        PropertyRequest->ValueSize = RequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    *MultipleItem = static_cast<PKSMULTIPLE_ITEM>(PropertyRequest->Value);
    (*MultipleItem)->Size = RequiredSize;
    (*MultipleItem)->Count = DescriptionCount;
    *JackDescription = DescriptionCount
        ? reinterpret_cast<Description *>(*MultipleItem + 1)
        : NULL;
    PropertyRequest->ValueSize = RequiredSize;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
Rpi5HdmiJackDescriptionPropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    CRpi5HdmiTopology *Topology;
    PKSMULTIPLE_ITEM MultipleItem;
    PKSJACK_DESCRIPTION JackDescription;
    ULONG PinId;
    NTSTATUS Status;

    if (!PropertyRequest || !PropertyRequest->MajorTarget ||
        !PropertyRequest->Instance ||
        PropertyRequest->InstanceSize < sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    PinId = *static_cast<PULONG>(PropertyRequest->Instance);
    if (PinId >= Rpi5HdmiTopologyPinCount)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        return Rpi5HdmiBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET,
            VT_ILLEGAL,
            sizeof(KSPROPERTY_DESCRIPTION),
            0);
    }
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;

    Status = Rpi5HdmiPrepareJackDescription(
        PropertyRequest, PinId, &MultipleItem, &JackDescription);
    if (!NT_SUCCESS(Status) || !JackDescription)
        return Status;

    Topology = static_cast<CRpi5HdmiTopology *>(
        static_cast<PMINIPORTTOPOLOGY>(PropertyRequest->MajorTarget));
    RtlZeroMemory(JackDescription, sizeof(*JackDescription));
    JackDescription->ConnectionType = eConnTypeOtherDigital;
    JackDescription->GeoLocation = eGeoLocHDMI;
    JackDescription->GenLocation = eGenLocPrimaryBox;
    JackDescription->PortConnection = ePortConnJack;
    JackDescription->IsConnected = Topology->GetAdapter()->IsSinkConnected();
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
Rpi5HdmiJackDescription2PropertyHandler(PPCPROPERTY_REQUEST PropertyRequest)
{
    PKSMULTIPLE_ITEM MultipleItem;
    PKSJACK_DESCRIPTION2 JackDescription;
    ULONG PinId;
    NTSTATUS Status;

    if (!PropertyRequest || !PropertyRequest->MajorTarget ||
        !PropertyRequest->Instance ||
        PropertyRequest->InstanceSize < sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    PinId = *static_cast<PULONG>(PropertyRequest->Instance);
    if (PinId >= Rpi5HdmiTopologyPinCount)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        return Rpi5HdmiBasicSupport(
            PropertyRequest,
            KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET,
            VT_ILLEGAL,
            sizeof(KSPROPERTY_DESCRIPTION),
            0);
    }
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;

    Status = Rpi5HdmiPrepareJackDescription(
        PropertyRequest, PinId, &MultipleItem, &JackDescription);
    if (!NT_SUCCESS(Status) || !JackDescription)
        return Status;

    RtlZeroMemory(JackDescription, sizeof(*JackDescription));
    JackDescription->JackCapabilities =
        JACKDESC2_PRESENCE_DETECT_CAPABILITY;
    return STATUS_SUCCESS;
}

static PCPROPERTY_ITEM Rpi5HdmiVolumeProperties[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_VOLUMELEVEL,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        Rpi5HdmiVolumePropertyHandler
    }
};

static PCPROPERTY_ITEM Rpi5HdmiMuteProperties[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_MUTE,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        Rpi5HdmiMutePropertyHandler
    }
};

static PCPROPERTY_ITEM Rpi5HdmiJackProperties[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        Rpi5HdmiJackDescriptionPropertyHandler
    },
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION2,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        Rpi5HdmiJackDescription2PropertyHandler
    }
};

static NTSTATUS NTAPI
Rpi5HdmiJackEventHandler(PPCEVENT_REQUEST EventRequest)
{
    CRpi5HdmiTopology *Topology;

    if (!EventRequest || !EventRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    switch (EventRequest->Verb)
    {
        case PCEVENT_VERB_ADD:
            if (!EventRequest->EventEntry)
                return STATUS_INVALID_PARAMETER;
            Topology = static_cast<CRpi5HdmiTopology *>(
                static_cast<PMINIPORTTOPOLOGY>(EventRequest->MajorTarget));
            Topology->AddEventToEventList(EventRequest->EventEntry);
            return STATUS_SUCCESS;

        case PCEVENT_VERB_REMOVE:
        case PCEVENT_VERB_SUPPORT:
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static PCEVENT_ITEM Rpi5HdmiJackEvents[] =
{
    {
        &KSEVENTSETID_PinCapsChange,
        KSEVENT_PINCAPS_JACKINFOCHANGE,
        PCEVENT_ITEM_FLAG_ENABLE | PCEVENT_ITEM_FLAG_BASICSUPPORT,
        Rpi5HdmiJackEventHandler
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(Rpi5HdmiVolumeAutomation, Rpi5HdmiVolumeProperties);
DEFINE_PCAUTOMATION_TABLE_PROP(Rpi5HdmiMuteAutomation, Rpi5HdmiMuteProperties);
DEFINE_PCAUTOMATION_TABLE_PROP_EVENT(
    Rpi5HdmiJackAutomation,
    Rpi5HdmiJackProperties,
    Rpi5HdmiJackEvents);

static PCPIN_DESCRIPTOR Rpi5HdmiWavePins[] =
{
    {
        1,
        1,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(Rpi5HdmiPcmDataRanges),
            Rpi5HdmiPcmDataRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(Rpi5HdmiBridgeDataRanges),
            Rpi5HdmiBridgeDataRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    }
};

static PCNODE_DESCRIPTOR Rpi5HdmiWaveNodes[] =
{
    {0, NULL, &KSNODETYPE_DAC, NULL}
};

static PCCONNECTION_DESCRIPTOR Rpi5HdmiWaveConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, PCFILTER_NODE, 1}
};

PCFILTER_DESCRIPTOR Rpi5HdmiWaveFilterDescriptor =
{
    0,
    NULL,
    sizeof(PCPIN_DESCRIPTOR),
    RTL_NUMBER_OF(Rpi5HdmiWavePins),
    Rpi5HdmiWavePins,
    sizeof(PCNODE_DESCRIPTOR),
    RTL_NUMBER_OF(Rpi5HdmiWaveNodes),
    Rpi5HdmiWaveNodes,
    RTL_NUMBER_OF(Rpi5HdmiWaveConnections),
    Rpi5HdmiWaveConnections,
    0,
    NULL
};

static PCPIN_DESCRIPTOR Rpi5HdmiTopologyPins[] =
{
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(Rpi5HdmiBridgeDataRanges),
            Rpi5HdmiBridgeDataRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    {
        0,
        0,
        0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            RTL_NUMBER_OF(Rpi5HdmiBridgeDataRanges),
            Rpi5HdmiBridgeDataRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_SPDIF_INTERFACE,
            NULL,
            0
        }
    }
};

static PCCONNECTION_DESCRIPTOR Rpi5HdmiTopologyConnections[] =
{
    {PCFILTER_NODE, 0, 0, 1},
    {0, 0, 1, 1},
    {1, 0, PCFILTER_NODE, 1}
};

static PCNODE_DESCRIPTOR Rpi5HdmiTopologyNodes[] =
{
    {0, &Rpi5HdmiVolumeAutomation, &KSNODETYPE_VOLUME, &KSAUDFNAME_MASTER_VOLUME},
    {0, &Rpi5HdmiMuteAutomation, &KSNODETYPE_MUTE, &KSAUDFNAME_MASTER_MUTE}
};

PCFILTER_DESCRIPTOR Rpi5HdmiTopologyFilterDescriptor =
{
    0,
    &Rpi5HdmiJackAutomation,
    sizeof(PCPIN_DESCRIPTOR),
    RTL_NUMBER_OF(Rpi5HdmiTopologyPins),
    Rpi5HdmiTopologyPins,
    sizeof(PCNODE_DESCRIPTOR),
    RTL_NUMBER_OF(Rpi5HdmiTopologyNodes),
    Rpi5HdmiTopologyNodes,
    RTL_NUMBER_OF(Rpi5HdmiTopologyConnections),
    Rpi5HdmiTopologyConnections,
    0,
    NULL
};

BOOLEAN
Rpi5HdmiIsFormatSupported(PKSDATAFORMAT DataFormat)
{
    PKSDATAFORMAT_WAVEFORMATEX WaveFormat;
    PWAVEFORMATEXTENSIBLE Extensible;

    if (!DataFormat || DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX))
        return FALSE;

    if (!IsEqualGUIDAligned(DataFormat->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataFormat->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataFormat->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
    {
        return FALSE;
    }

    WaveFormat = reinterpret_cast<PKSDATAFORMAT_WAVEFORMATEX>(DataFormat);
    if (WaveFormat->WaveFormatEx.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (DataFormat->FormatSize !=
                sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE) ||
            WaveFormat->WaveFormatEx.cbSize !=
                sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            return FALSE;
        }

        Extensible = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(
            &WaveFormat->WaveFormatEx);
        if (!IsEqualGUIDAligned(Extensible->SubFormat,
                                KSDATAFORMAT_SUBTYPE_PCM) ||
            Extensible->Samples.wValidBitsPerSample !=
                RPI5HDMI_BITS_PER_SAMPLE)
        {
            return FALSE;
        }
    }
    else if (WaveFormat->WaveFormatEx.wFormatTag != WAVE_FORMAT_PCM ||
             DataFormat->FormatSize != sizeof(KSDATAFORMAT_WAVEFORMATEX) ||
             WaveFormat->WaveFormatEx.cbSize)
    {
        return FALSE;
    }

    return WaveFormat->WaveFormatEx.nChannels == RPI5HDMI_CHANNELS &&
           WaveFormat->WaveFormatEx.nSamplesPerSec == RPI5HDMI_SAMPLE_RATE &&
           WaveFormat->WaveFormatEx.nAvgBytesPerSec ==
               RPI5HDMI_SAMPLE_RATE * RPI5HDMI_BLOCK_ALIGN &&
           WaveFormat->WaveFormatEx.wBitsPerSample == RPI5HDMI_BITS_PER_SAMPLE &&
           WaveFormat->WaveFormatEx.nBlockAlign == RPI5HDMI_BLOCK_ALIGN &&
           DataFormat->SampleSize == RPI5HDMI_BLOCK_ALIGN;
}

CRpi5HdmiTopology::CRpi5HdmiTopology(CRpi5HdmiAdapter *Adapter)
    : m_Adapter(Adapter), m_PortEvents(NULL)
{
    m_Adapter->AddRef();
}

CRpi5HdmiTopology::~CRpi5HdmiTopology()
{
    if (m_PortEvents)
    {
        m_PortEvents->Release();
        m_PortEvents = NULL;
    }
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CRpi5HdmiTopology::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniport) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportTopology))
    {
        *Interface = static_cast<PMINIPORTTOPOLOGY>(this);
        AddRef();
        return STATUS_SUCCESS;
    }

    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
NTAPI
CRpi5HdmiTopology::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &Rpi5HdmiTopologyFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CRpi5HdmiTopology::DataRangeIntersection(
    ULONG PinId,
    PKSDATARANGE DataRange,
    PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength,
    PVOID ResultantFormat,
    PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
CRpi5HdmiTopology::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTTOPOLOGY Port)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    if (!Port)
        return STATUS_INVALID_PARAMETER;

    Status = Port->QueryInterface(
        IID_IPortEvents,
        reinterpret_cast<PVOID *>(&m_PortEvents));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = m_Adapter->RegisterJackEventPort(m_PortEvents);
    if (!NT_SUCCESS(Status))
    {
        m_PortEvents->Release();
        m_PortEvents = NULL;
    }
    return Status;
}

VOID
CRpi5HdmiTopology::AddEventToEventList(PKSEVENT_ENTRY EventEntry)
{
    if (m_PortEvents && EventEntry)
        m_PortEvents->AddEventToEventList(EventEntry);
}

CRpi5HdmiWave::CRpi5HdmiWave(CRpi5HdmiAdapter *Adapter) : m_Adapter(Adapter)
{
    m_Adapter->AddRef();
}

CRpi5HdmiWave::~CRpi5HdmiWave()
{
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CRpi5HdmiWave::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniport) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRT))
    {
        *Interface = static_cast<PMINIPORTWAVERT>(this);
        AddRef();
        return STATUS_SUCCESS;
    }

    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
NTAPI
CRpi5HdmiWave::GetDescription(PPCFILTER_DESCRIPTOR *Description)
{
    if (!Description)
        return STATUS_INVALID_PARAMETER;
    *Description = &Rpi5HdmiWaveFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CRpi5HdmiWave::DataRangeIntersection(
    ULONG PinId,
    PKSDATARANGE DataRange,
    PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength,
    PVOID ResultantFormat,
    PULONG ResultantFormatLength)
{
    if (!ResultantFormatLength || PinId != 0 || !DataRange || !MatchingDataRange)
        return STATUS_INVALID_PARAMETER;

    if (!IsEqualGUIDAligned(DataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(DataRange->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(DataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX) ||
        !IsEqualGUIDAligned(MatchingDataRange->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(MatchingDataRange->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !IsEqualGUIDAligned(MatchingDataRange->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
    {
        return STATUS_NO_MATCH;
    }

    if (!Rpi5HdmiIsFormatSupported(reinterpret_cast<PKSDATAFORMAT>(DataRange)))
        return STATUS_NO_MATCH;

    *ResultantFormatLength = DataRange->FormatSize;
    if (!OutputBufferLength || !ResultantFormat)
        return STATUS_BUFFER_OVERFLOW;
    if (OutputBufferLength < DataRange->FormatSize)
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(ResultantFormat, DataRange, DataRange->FormatSize);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CRpi5HdmiWave::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTWAVERT Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    UNREFERENCED_PARAMETER(Port);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CRpi5HdmiWave::NewStream(
    PMINIPORTWAVERTSTREAM *Stream,
    PPORTWAVERTSTREAM PortStream,
    ULONG Pin,
    BOOLEAN Capture,
    PKSDATAFORMAT DataFormat)
{
    CRpi5HdmiStream *NewStream;

    if (!Stream || !PortStream || Pin != 0 || Capture || !Rpi5HdmiIsFormatSupported(DataFormat))
        return STATUS_NOT_SUPPORTED;
    if (!m_Adapter->ClaimStream())
        return STATUS_DEVICE_BUSY;

    NewStream = new (NonPagedPool, TAG_RPI5HDMI) CRpi5HdmiStream(m_Adapter, PortStream);
    if (!NewStream)
    {
        m_Adapter->ReleaseStream();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewStream->AddRef();
    *Stream = static_cast<PMINIPORTWAVERTSTREAM>(NewStream);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CRpi5HdmiWave::GetDeviceDescription(PDEVICE_DESCRIPTION DeviceDescription)
{
    if (!DeviceDescription)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(DeviceDescription, sizeof(*DeviceDescription));
    DeviceDescription->Version = DEVICE_DESCRIPTION_VERSION1;
    DeviceDescription->Master = TRUE;
    DeviceDescription->ScatterGather = FALSE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->InterfaceType = Internal;
    DeviceDescription->MaximumLength = RPI5HDMI_MAX_BUFFER_SIZE;
    return STATUS_SUCCESS;
}

NTSTATUS
Rpi5HdmiCreateTopology(PUNKNOWN *Unknown, CRpi5HdmiAdapter *Adapter)
{
    CRpi5HdmiTopology *Miniport;

    if (!Unknown || !Adapter)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_RPI5HDMI) CRpi5HdmiTopology(Adapter);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTTOPOLOGY>(Miniport);
    return STATUS_SUCCESS;
}

NTSTATUS
Rpi5HdmiCreateWave(PUNKNOWN *Unknown, CRpi5HdmiAdapter *Adapter)
{
    CRpi5HdmiWave *Miniport;

    if (!Unknown || !Adapter)
        return STATUS_INVALID_PARAMETER;
    Miniport = new (NonPagedPool, TAG_RPI5HDMI) CRpi5HdmiWave(Adapter);
    if (!Miniport)
        return STATUS_INSUFFICIENT_RESOURCES;
    Miniport->AddRef();
    *Unknown = static_cast<PMINIPORTWAVERT>(Miniport);
    return STATUS_SUCCESS;
}
