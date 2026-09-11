/*
 * PROJECT:         ReactOS HDAudio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Property handlers
 * COPYRIGHT:       Copyright 2025-2026 Oleg Dubinskiy <oleg.dubinskiy@reactos.org>
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

// FIXME: halfplemented

NTSTATUS
NTAPI
PropertyHandler_JackDescription(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG JackDescriptionSize = sizeof(KSMULTIPLE_ITEM) + sizeof(KSJACK_DESCRIPTION);

    if (PropertyRequest->ValueSize == 0)
    {
        PropertyRequest->ValueSize = JackDescriptionSize;
        return STATUS_BUFFER_OVERFLOW;
    }

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        if (PropertyRequest->ValueSize < sizeof(ULONG))
        {
            PropertyRequest->ValueSize = sizeof(ULONG);
            return STATUS_BUFFER_TOO_SMALL;
        }

        PULONG AccessFlags = (PULONG)PropertyRequest->Value;
        *AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET;
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;

    if (PropertyRequest->ValueSize < JackDescriptionSize)
    {
        PropertyRequest->ValueSize = JackDescriptionSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportTopology *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportTopology, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    PIN_CONFIGURATION_DEFAULT PinConfiguration;
    Status = Node->GetPinConfigurationDefault(Node->GetStartNodeId(), &PinConfiguration);
    if (NT_SUCCESS(Status))
    {
        PKSMULTIPLE_ITEM MultipleItem = (PKSMULTIPLE_ITEM)PropertyRequest->Value;
        MultipleItem->Size = JackDescriptionSize;
        MultipleItem->Count = 1;
        PKSJACK_DESCRIPTION JackDescription = (PKSJACK_DESCRIPTION)(MultipleItem + 1);
        JackDescription->ChannelMapping = KSAUDIO_SPEAKER_STEREO; // FIXME
        JackDescription->Color = PinConfiguration.Color;
        JackDescription->ConnectionType = (EPcxConnectionType)PinConfiguration.ConnectionType;
        JackDescription->GeoLocation = (EPcxGeoLocation)PinConfiguration.Location;
        JackDescription->GenLocation = (EPcxGenLocation)(PinConfiguration.Location << 4);
        JackDescription->PortConnection = (EPxcPortConnection)PinConfiguration.PortConnectivity;
        JackDescription->IsConnected = TRUE;
        PropertyRequest->ValueSize = JackDescriptionSize;
    }

    Miniport->Release();
    return Status;
}

NTSTATUS
NTAPI
PropertyHandler_ChannelConfig(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(KSAUDIO_CHANNEL_CONFIG))
        return STATUS_BUFFER_TOO_SMALL;

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportWaveRT *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportWaveRT, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    PKSAUDIO_CHANNEL_CONFIG ChannelConfig = (PKSAUDIO_CHANNEL_CONFIG)PropertyRequest->Value;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UNIMPLEMENTED;
        ChannelConfig->ActiveSpeakerPositions = KSAUDIO_SPEAKER_7POINT1;
        Miniport->Release();
        return STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UNIMPLEMENTED;
        Miniport->Release();
        return STATUS_SUCCESS;
    }
    Miniport->Release();
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
PropertyHandler_SpeakerGeometry(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(LONG))
        return STATUS_BUFFER_TOO_SMALL;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UNIMPLEMENTED;
        *(PLONG)PropertyRequest->Value = -1;
        PropertyRequest->ValueSize = sizeof(LONG);
        return STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UNIMPLEMENTED;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
HdaGetMasterAmps(
    IN CFunctionGroupNode *Node,
    OUT PULONG *Nids,
    OUT PULONG Count,
    OUT PAMPLIFIER_CAPABILITIES Caps)
{
    PULONG Nodes = NULL;
    ULONG Total = 0, Found = 0, Index;
    AMPLIFIER_CAPABILITIES Probe;
    PNODE_CONTEXT Context;
    NTSTATUS Status;

    Status = Node->GetNodesWithType(0x00, &Total, &Nodes);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Total; Index++)
    {
        Context = Node->FindNodeId(Nodes[Index]);
        if (Context && Context->Digital)
            continue;
        if (!NT_SUCCESS(Node->GetAmplifierDetails(Nodes[Index], 0, &Probe)))
            continue;
        if (!Found)
            *Caps = Probe;
        Nodes[Found++] = Nodes[Index];
    }

    if (!Found)
    {
        if (Nodes)
            ExFreePoolWithTag(Nodes, TAG_HDAUDIO);
        return STATUS_NOT_SUPPORTED;
    }

    *Nids = Nodes;
    *Count = Found;
    return STATUS_SUCCESS;
}

static
LONG
HdaGetRequestChannel(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Instance && PropertyRequest->InstanceSize >= sizeof(LONG))
        return *(PLONG)PropertyRequest->Instance;
    return -1;
}

static
UCHAR
HdaGainFromLevel(
    IN LONG Level,
    IN PAMPLIFIER_CAPABILITIES Caps)
{
    LONG Step = ((LONG)Caps->Steps + 1) * 0x4000;
    LONG Gain = (Level >= 0 ? Level + Step / 2 : Level - Step / 2) / Step + (LONG)Caps->Offset;

    if (Gain < 0)
        Gain = 0;
    if (Gain > (LONG)Caps->NumSteps)
        Gain = Caps->NumSteps;
    return (UCHAR)Gain;
}

static
NTSTATUS
HdaOpenSettingsKey(
    IN CMiniportTopology *Miniport,
    OUT PREGISTRYKEY *SettingsKey)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Settings");
    PPORTTOPOLOGY Port = Miniport->GetPort();
    PREGISTRYKEY DriverKey;
    NTSTATUS Status;

    if (!Port)
        return STATUS_DEVICE_NOT_READY;

    Status = Port->NewRegistryKey(&DriverKey, NULL, DriverRegistryKey, KEY_ALL_ACCESS, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DriverKey->NewSubKey(SettingsKey, NULL, KEY_ALL_ACCESS, &Name, REG_OPTION_NON_VOLATILE, NULL);
    DriverKey->Release();
    return Status;
}

static
VOID
HdaSaveSetting(
    IN CMiniportTopology *Miniport,
    IN PCWSTR ValueName,
    IN ULONG Value)
{
    UNICODE_STRING Name;
    PREGISTRYKEY Key;

    if (!NT_SUCCESS(HdaOpenSettingsKey(Miniport, &Key)))
        return;

    RtlInitUnicodeString(&Name, ValueName);
    Key->SetValueKey(&Name, REG_DWORD, &Value, sizeof(Value));
    Key->Release();
}

static
BOOLEAN
HdaQuerySetting(
    IN PREGISTRYKEY Key,
    IN PCWSTR ValueName,
    OUT PULONG Value)
{
    ULONG Buffer[(sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 2 * sizeof(ULONG) - 1) / sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING Name;
    ULONG Length;

    RtlInitUnicodeString(&Name, ValueName);
    if (!NT_SUCCESS(Key->QueryValueKey(&Name, KeyValuePartialInformation, Info, sizeof(Buffer), &Length)) ||
        Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
    {
        return FALSE;
    }

    *Value = *(PULONG)Info->Data;
    return TRUE;
}

VOID
HDAUDIO_RestoreMasterSettings(
    IN CMiniportTopology *Miniport)
{
    CFunctionGroupNode *Node = Miniport->GetNode();
    AMPLIFIER_CAPABILITIES Caps;
    PREGISTRYKEY Key;
    PULONG Nids = NULL;
    ULONG Count = 0, Index, Right, Level = 0, Mute = 0;
    BOOLEAN HaveLevel, HaveMute;

    if (!Node || !NT_SUCCESS(HdaOpenSettingsKey(Miniport, &Key)))
        return;

    HaveLevel = HdaQuerySetting(Key, L"MasterLevel", &Level);
    HaveMute = HdaQuerySetting(Key, L"MasterMute", &Mute);
    Key->Release();

    if ((!HaveLevel && !HaveMute) || !NT_SUCCESS(HdaGetMasterAmps(Node, &Nids, &Count, &Caps)))
        return;

    for (Index = 0; Index < Count; Index++)
    {
        for (Right = 0; Right < 2; Right++)
        {
            UCHAR CurrentMute, Gain;

            if (!NT_SUCCESS(Node->GetAmplifierGainMute(Nids[Index], 0, Right, &CurrentMute, &Gain)))
                continue;
            if (HaveLevel && Caps.NumSteps)
                Gain = HdaGainFromLevel((LONG)Level, &Caps);
            if (HaveMute && Caps.MuteCapable)
                CurrentMute = Mute ? 1 : 0;
            Node->SetAmplifierGainMute(Nids[Index], 0, Right, CurrentMute, Gain);
        }
    }

    ExFreePoolWithTag(Nids, TAG_HDAUDIO);
}

NTSTATUS
NTAPI
PropertyHandler_Volume(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(LONG))
        return STATUS_BUFFER_TOO_SMALL;

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportTopology *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportTopology, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    PULONG Nids = NULL;
    ULONG Count = 0, Index, Right;
    AMPLIFIER_CAPABILITIES Caps;
    Status = HdaGetMasterAmps(Node, &Nids, &Count, &Caps);
    if (!NT_SUCCESS(Status) || !Caps.NumSteps)
    {
        if (NT_SUCCESS(Status))
            ExFreePoolWithTag(Nids, TAG_HDAUDIO);
        Miniport->Release();
        return STATUS_NOT_SUPPORTED;
    }

    LONG Step = ((LONG)Caps.Steps + 1) * 0x4000;
    LONG Channel = HdaGetRequestChannel(PropertyRequest);

    if (Channel > 0 || Channel < -1)
    {
        ExFreePoolWithTag(Nids, TAG_HDAUDIO);
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        if (PropertyRequest->ValueSize >= sizeof(KSPROPERTY_DESCRIPTION))
        {
            PKSPROPERTY_DESCRIPTION Desc = (PKSPROPERTY_DESCRIPTION)PropertyRequest->Value;

            Desc->AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
            Desc->DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) +
                                    sizeof(KSPROPERTY_STEPPING_LONG);
            Desc->PropTypeSet.Set = KSPROPTYPESETID_General;
            Desc->PropTypeSet.Id = VT_I4;
            Desc->PropTypeSet.Flags = 0;
            Desc->MembersListCount = 1;
            Desc->Reserved = 0;

            if (PropertyRequest->ValueSize >= Desc->DescriptionSize)
            {
                PKSPROPERTY_MEMBERSHEADER Members = (PKSPROPERTY_MEMBERSHEADER)(Desc + 1);
                PKSPROPERTY_STEPPING_LONG Range = (PKSPROPERTY_STEPPING_LONG)(Members + 1);

                Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
                Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
                Members->MembersCount = 1;
                Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_UNIFORM;
                Range->SteppingDelta = Step;
                Range->Reserved = 0;
                Range->Bounds.SignedMinimum = -(LONG)Caps.Offset * Step;
                Range->Bounds.SignedMaximum = ((LONG)Caps.NumSteps - (LONG)Caps.Offset) * Step;
                PropertyRequest->ValueSize = Desc->DescriptionSize;
            }
            else
            {
                PropertyRequest->ValueSize = sizeof(KSPROPERTY_DESCRIPTION);
            }
        }
        else
        {
            *(PULONG)PropertyRequest->Value = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
            PropertyRequest->ValueSize = sizeof(ULONG);
        }
        Status = STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UCHAR Mute, Gain;

        Status = Node->GetAmplifierGainMute(Nids[0], 0, 0, &Mute, &Gain);
        if (NT_SUCCESS(Status))
        {
            *(PLONG)PropertyRequest->Value = ((LONG)Gain - (LONG)Caps.Offset) * Step;
            PropertyRequest->ValueSize = sizeof(LONG);
        }
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UCHAR Gain = HdaGainFromLevel(*(PLONG)PropertyRequest->Value, &Caps);

        Status = STATUS_SUCCESS;
        for (Index = 0; Index < Count; Index++)
        {
            for (Right = 0; Right < 2; Right++)
            {
                UCHAR Mute, Current;

                if (!NT_SUCCESS(Node->GetAmplifierGainMute(Nids[Index], 0, Right, &Mute, &Current)))
                    Mute = 0;
                Status = Node->SetAmplifierGainMute(Nids[Index], 0, Right, Mute, Gain);
            }
        }
        if (NT_SUCCESS(Status))
            HdaSaveSetting(Miniport, L"MasterLevel", (ULONG)(((LONG)Gain - (LONG)Caps.Offset) * Step));
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    ExFreePoolWithTag(Nids, TAG_HDAUDIO);
    Miniport->Release();
    return Status;
}

NTSTATUS
NTAPI
PropertyHandler_Mute(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(BOOL))
        return STATUS_BUFFER_TOO_SMALL;

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportTopology *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportTopology, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    PULONG Nids = NULL;
    ULONG Count = 0, Index, Right;
    AMPLIFIER_CAPABILITIES Caps;
    Status = HdaGetMasterAmps(Node, &Nids, &Count, &Caps);
    if (!NT_SUCCESS(Status) || !Caps.MuteCapable)
    {
        if (NT_SUCCESS(Status))
            ExFreePoolWithTag(Nids, TAG_HDAUDIO);
        Miniport->Release();
        return STATUS_NOT_SUPPORTED;
    }

    LONG Channel = HdaGetRequestChannel(PropertyRequest);

    if (Channel > 0 || Channel < -1)
    {
        ExFreePoolWithTag(Nids, TAG_HDAUDIO);
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        *(PULONG)PropertyRequest->Value = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
        PropertyRequest->ValueSize = sizeof(ULONG);
        Status = STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UCHAR Mute, Gain;

        Status = Node->GetAmplifierGainMute(Nids[0], 0, 0, &Mute, &Gain);
        if (NT_SUCCESS(Status))
        {
            *(PBOOL)PropertyRequest->Value = Mute ? TRUE : FALSE;
            PropertyRequest->ValueSize = sizeof(BOOL);
        }
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UCHAR Mute = *(PBOOL)PropertyRequest->Value ? 1 : 0;

        Status = STATUS_SUCCESS;
        for (Index = 0; Index < Count; Index++)
        {
            for (Right = 0; Right < 2; Right++)
            {
                UCHAR Current, Gain;

                if (!NT_SUCCESS(Node->GetAmplifierGainMute(Nids[Index], 0, Right, &Current, &Gain)))
                    Gain = Caps.Offset;
                Status = Node->SetAmplifierGainMute(Nids[Index], 0, Right, Mute, Gain);
            }
        }
        if (NT_SUCCESS(Status))
            HdaSaveSetting(Miniport, L"MasterMute", Mute);
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    ExFreePoolWithTag(Nids, TAG_HDAUDIO);
    Miniport->Release();
    return Status;
}

NTSTATUS
NTAPI
EventHandler_Volume(IN PPCEVENT_REQUEST EventRequest)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}
