/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel Streaming
 * FILE:            drivers/wdm/audio/legacy/wdmaud/deviface.c
 * PURPOSE:         System Audio graph builder
 * PROGRAMMER:      Andrew Greenwood
 *                  Johannes Anderwald
 */

#include <ntifs.h>
#include "wdmaud.h"

#define NDEBUG
#include <debug.h>

const GUID KSPROPSETID_Sysaudio                 = {0xCBE3FAA0L, 0xCC75, 0x11D0, {0xB4, 0x65, 0x00, 0x00, 0x1A, 0x18, 0x18, 0xE6}};

#if defined(_WIN64)

/* 32-bit wire layouts used by wdmaud.drv under WOW64. */
typedef struct _WDMAUD_KSSTREAM_HEADER32
{
    ULONG Size;
    ULONG TypeSpecificFlags;
    KSTIME PresentationTime;
    LONGLONG Duration;
    ULONG FrameExtent;
    ULONG DataUsed;
    ULONG Data;
    ULONG OptionsFlags;
} WDMAUD_KSSTREAM_HEADER32, *PWDMAUD_KSSTREAM_HEADER32;

typedef union _WDMAUD_DEVICE_INFO32_UNION
{
    UCHAR Data[280];
    ULONGLONG Alignment;
} WDMAUD_DEVICE_INFO32_UNION;

typedef struct _WDMAUD_DEVICE_INFO32
{
    WDMAUD_KSSTREAM_HEADER32 Header;
    SOUND_DEVICE_TYPE DeviceType;
    ULONG DeviceIndex;
    ULONG hDevice;
    ULONG DeviceCount;
    ULONG Flags;
    WDMAUD_DEVICE_INFO32_UNION u;
} WDMAUD_DEVICE_INFO32, *PWDMAUD_DEVICE_INFO32;

typedef struct _WDMAUD_MIXERLINEW32
{
    ULONG cbStruct;
    ULONG dwDestination;
    ULONG dwSource;
    ULONG dwLineID;
    ULONG fdwLine;
    ULONG dwUser;
    ULONG dwComponentType;
    ULONG cChannels;
    ULONG cConnections;
    ULONG cControls;
    WCHAR szShortName[MIXER_SHORT_NAME_CHARS];
    WCHAR szName[MIXER_LONG_NAME_CHARS];
    struct
    {
        ULONG dwType;
        ULONG dwDeviceID;
        USHORT wMid;
        USHORT wPid;
        MMVERSION vDriverVersion;
        WCHAR szPname[MAXPNAMELEN];
    } Target;
} WDMAUD_MIXERLINEW32, *PWDMAUD_MIXERLINEW32;

typedef struct _WDMAUD_MIXERLINECONTROLSW32
{
    ULONG cbStruct;
    ULONG dwLineID;
    ULONG dwControl;
    ULONG cControls;
    ULONG cbmxctrl;
    ULONG pamxctrl;
} WDMAUD_MIXERLINECONTROLSW32, *PWDMAUD_MIXERLINECONTROLSW32;

typedef struct _WDMAUD_MIXERCONTROLDETAILS32
{
    ULONG cbStruct;
    ULONG dwControlID;
    ULONG cChannels;
    ULONG OwnerOrMultipleItems;
    ULONG cbDetails;
    ULONG paDetails;
} WDMAUD_MIXERCONTROLDETAILS32, *PWDMAUD_MIXERCONTROLDETAILS32;

C_ASSERT(sizeof(WDMAUD_KSSTREAM_HEADER32) == 48);
C_ASSERT(FIELD_OFFSET(WDMAUD_DEVICE_INFO32, u) == 72);
C_ASSERT(sizeof(WDMAUD_DEVICE_INFO32) == 352);
C_ASSERT(sizeof(WDMAUD_MIXERLINEW32) == 280);
C_ASSERT(sizeof(WDMAUD_MIXERLINECONTROLSW32) == 24);
C_ASSERT(sizeof(WDMAUD_MIXERCONTROLDETAILS32) == 24);

static VOID
WdmAudMixerLine32ToNative(
    _Out_ LPMIXERLINEW Target,
    _In_ const WDMAUD_MIXERLINEW32 *Source)
{
    RtlZeroMemory(Target, sizeof(*Target));
    Target->cbStruct = Source->cbStruct;
    Target->dwDestination = Source->dwDestination;
    Target->dwSource = Source->dwSource;
    Target->dwLineID = Source->dwLineID;
    Target->fdwLine = Source->fdwLine;
    Target->dwUser = Source->dwUser;
    Target->dwComponentType = Source->dwComponentType;
    Target->cChannels = Source->cChannels;
    Target->cConnections = Source->cConnections;
    Target->cControls = Source->cControls;
    RtlCopyMemory(Target->szShortName, Source->szShortName, sizeof(Source->szShortName));
    RtlCopyMemory(Target->szName, Source->szName, sizeof(Source->szName));
    Target->Target.dwType = Source->Target.dwType;
    Target->Target.dwDeviceID = Source->Target.dwDeviceID;
    Target->Target.wMid = Source->Target.wMid;
    Target->Target.wPid = Source->Target.wPid;
    Target->Target.vDriverVersion = Source->Target.vDriverVersion;
    RtlCopyMemory(Target->Target.szPname,
                  Source->Target.szPname,
                  sizeof(Source->Target.szPname));
}

static VOID
WdmAudMixerLineNativeTo32(
    _Out_ PWDMAUD_MIXERLINEW32 Target,
    _In_ const MIXERLINEW *Source)
{
    RtlZeroMemory(Target, sizeof(*Target));
    Target->cbStruct = Source->cbStruct;
    Target->dwDestination = Source->dwDestination;
    Target->dwSource = Source->dwSource;
    Target->dwLineID = Source->dwLineID;
    Target->fdwLine = Source->fdwLine;
    Target->dwUser = (ULONG)Source->dwUser;
    Target->dwComponentType = Source->dwComponentType;
    Target->cChannels = Source->cChannels;
    Target->cConnections = Source->cConnections;
    Target->cControls = Source->cControls;
    RtlCopyMemory(Target->szShortName, Source->szShortName, sizeof(Target->szShortName));
    RtlCopyMemory(Target->szName, Source->szName, sizeof(Target->szName));
    Target->Target.dwType = Source->Target.dwType;
    Target->Target.dwDeviceID = Source->Target.dwDeviceID;
    Target->Target.wMid = Source->Target.wMid;
    Target->Target.wPid = Source->Target.wPid;
    Target->Target.vDriverVersion = Source->Target.vDriverVersion;
    RtlCopyMemory(Target->Target.szPname,
                  Source->Target.szPname,
                  sizeof(Target->Target.szPname));
}

static VOID
WdmAudMixerLineControls32ToNative(
    _Out_ LPMIXERLINECONTROLSW Target,
    _In_ const WDMAUD_MIXERLINECONTROLSW32 *Source)
{
    RtlZeroMemory(Target, sizeof(*Target));
    RtlCopyMemory(Target, Source, FIELD_OFFSET(WDMAUD_MIXERLINECONTROLSW32, pamxctrl));
    Target->pamxctrl = (LPMIXERCONTROLW)(ULONG_PTR)Source->pamxctrl;
}

static VOID
WdmAudMixerLineControlsNativeTo32(
    _Out_ PWDMAUD_MIXERLINECONTROLSW32 Target,
    _In_ const MIXERLINECONTROLSW *Source)
{
    RtlZeroMemory(Target, sizeof(*Target));
    RtlCopyMemory(Target, Source, FIELD_OFFSET(WDMAUD_MIXERLINECONTROLSW32, pamxctrl));
    Target->pamxctrl = (ULONG)(ULONG_PTR)Source->pamxctrl;
}

static VOID
WdmAudMixerControlDetails32ToNative(
    _Out_ LPMIXERCONTROLDETAILS Target,
    _In_ const WDMAUD_MIXERCONTROLDETAILS32 *Source)
{
    RtlZeroMemory(Target, sizeof(*Target));
    Target->cbStruct = Source->cbStruct;
    Target->dwControlID = Source->dwControlID;
    Target->cChannels = Source->cChannels;
    Target->hwndOwner = (HWND)LongToHandle((LONG)Source->OwnerOrMultipleItems);
    Target->cbDetails = Source->cbDetails;
    Target->paDetails = (PVOID)(ULONG_PTR)Source->paDetails;
}

static VOID
WdmAudMixerControlDetailsNativeTo32(
    _Out_ PWDMAUD_MIXERCONTROLDETAILS32 Target,
    _In_ const MIXERCONTROLDETAILS *Source)
{
    RtlZeroMemory(Target, sizeof(*Target));
    Target->cbStruct = Source->cbStruct;
    Target->dwControlID = Source->dwControlID;
    Target->cChannels = Source->cChannels;
    Target->OwnerOrMultipleItems = HandleToUlong(Source->hwndOwner);
    Target->cbDetails = Source->cbDetails;
    Target->paDetails = (ULONG)(ULONG_PTR)Source->paDetails;
}

static VOID
WdmAudDeviceInfo32ToNative(
    _Out_ PWDMAUD_DEVICE_INFO Target,
    _In_ const WDMAUD_DEVICE_INFO32 *Source,
    _In_ ULONG IoControlCode)
{
    const ULONG *Values = (const ULONG *)Source->u.Data;

    RtlZeroMemory(Target, sizeof(*Target));
    Target->Header.Size = Source->Header.Size;
    Target->Header.TypeSpecificFlags = Source->Header.TypeSpecificFlags;
    Target->Header.PresentationTime = Source->Header.PresentationTime;
    Target->Header.Duration = Source->Header.Duration;
    Target->Header.FrameExtent = Source->Header.FrameExtent;
    Target->Header.DataUsed = Source->Header.DataUsed;
    Target->Header.Data = (PVOID)(ULONG_PTR)Source->Header.Data;
    Target->Header.OptionsFlags = Source->Header.OptionsFlags;
    Target->DeviceType = Source->DeviceType;
    Target->DeviceIndex = Source->DeviceIndex;
    Target->hDevice = LongToHandle((LONG)Source->hDevice);
    Target->DeviceCount = Source->DeviceCount;
    Target->Flags = Source->Flags;
    RtlCopyMemory(&Target->u, Source->u.Data, sizeof(Source->u.Data));

    switch (IoControlCode)
    {
        case IOCTL_OPEN_WDMAUD:
            if (Source->DeviceType == MIXER_DEVICE_TYPE)
                Target->u.hNotifyEvent = LongToHandle((LONG)Values[0]);
            break;
        case IOCTL_GETLINEINFO:
            WdmAudMixerLine32ToNative(&Target->u.MixLine,
                                      (const WDMAUD_MIXERLINEW32 *)Source->u.Data);
            break;
        case IOCTL_GETLINECONTROLS:
            WdmAudMixerLineControls32ToNative(&Target->u.MixControls,
                                              (const WDMAUD_MIXERLINECONTROLSW32 *)Source->u.Data);
            break;
        case IOCTL_SETCONTROLDETAILS:
        case IOCTL_GETCONTROLDETAILS:
            WdmAudMixerControlDetails32ToNative(&Target->u.MixDetails,
                                                (const WDMAUD_MIXERCONTROLDETAILS32 *)Source->u.Data);
            break;
        case IOCTL_QUERYDEVICEINTERFACESTRING:
            Target->u.Interface.DeviceInterfaceString = (LPWSTR)(ULONG_PTR)Values[0];
            Target->u.Interface.DeviceInterfaceStringSize = Values[1];
            break;
    }
}

static VOID
WdmAudDeviceInfoNativeTo32(
    _Out_ PWDMAUD_DEVICE_INFO32 Target,
    _In_ const WDMAUD_DEVICE_INFO *Source,
    _In_ ULONG IoControlCode)
{
    ULONG *Values;

    RtlZeroMemory(Target, sizeof(*Target));
    Target->Header.Size = Source->Header.Size;
    Target->Header.TypeSpecificFlags = Source->Header.TypeSpecificFlags;
    Target->Header.PresentationTime = Source->Header.PresentationTime;
    Target->Header.Duration = Source->Header.Duration;
    Target->Header.FrameExtent = Source->Header.FrameExtent;
    Target->Header.DataUsed = Source->Header.DataUsed;
    Target->Header.Data = (ULONG)(ULONG_PTR)Source->Header.Data;
    Target->Header.OptionsFlags = Source->Header.OptionsFlags;
    Target->DeviceType = Source->DeviceType;
    Target->DeviceIndex = (ULONG)Source->DeviceIndex;
    Target->hDevice = HandleToUlong(Source->hDevice);
    Target->DeviceCount = Source->DeviceCount;
    Target->Flags = Source->Flags;
    RtlCopyMemory(Target->u.Data, &Source->u, sizeof(Target->u.Data));
    Values = (ULONG *)Target->u.Data;

    switch (IoControlCode)
    {
        case IOCTL_OPEN_WDMAUD:
            if (Source->DeviceType == MIXER_DEVICE_TYPE)
                Values[0] = HandleToUlong(Source->u.hNotifyEvent);
            break;
        case IOCTL_GETLINEINFO:
            WdmAudMixerLineNativeTo32((PWDMAUD_MIXERLINEW32)Target->u.Data,
                                      &Source->u.MixLine);
            break;
        case IOCTL_GETLINECONTROLS:
            WdmAudMixerLineControlsNativeTo32((PWDMAUD_MIXERLINECONTROLSW32)Target->u.Data,
                                              &Source->u.MixControls);
            break;
        case IOCTL_SETCONTROLDETAILS:
        case IOCTL_GETCONTROLDETAILS:
            WdmAudMixerControlDetailsNativeTo32((PWDMAUD_MIXERCONTROLDETAILS32)Target->u.Data,
                                                &Source->u.MixDetails);
            break;
        case IOCTL_QUERYDEVICEINTERFACESTRING:
            Values[0] = (ULONG)(ULONG_PTR)Source->u.Interface.DeviceInterfaceString;
            Values[1] = Source->u.Interface.DeviceInterfaceStringSize;
            break;
        case IOCTL_GET_MIXER_EVENT:
            Values[0] = HandleToUlong(Source->u.MixerEvent.hMixer);
            Values[1] = Source->u.MixerEvent.NotificationType;
            Values[2] = Source->u.MixerEvent.Value;
            break;
        case IOCTL_DUPLICATE_WDMAUD_PIN:
            Values[0] = HandleToUlong(Source->u.hUserDevice);
            break;
    }
}

#endif /* _WIN64 */

NTSTATUS
WdmAudControlOpen(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        return WdmAudControlOpenMixer(DeviceObject, Irp, DeviceInfo, ClientInfo);
    }

    if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
    {
        return WdmAudControlOpenWave(DeviceObject, Irp, DeviceInfo, ClientInfo);
    }

    if (DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE || DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE)
    {
        return WdmAudControlOpenMidi(DeviceObject, Irp, DeviceInfo, ClientInfo);
    }


    return SetIrpIoStatus(Irp, STATUS_NOT_SUPPORTED, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
WdmAudControlDeviceType(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    ULONG Result = 0;

    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        Result = WdmAudGetMixerDeviceCount();
    }
    else if (DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        Result = WdmAudGetWaveOutDeviceCount();
    }
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE)
    {
        Result = WdmAudGetWaveInDeviceCount();
    }
    else if (DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE)
    {
        Result = WdmAudGetMidiInDeviceCount();
    }
    else if (DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        Result = WdmAudGetMidiOutDeviceCount();
    }


    /* store result count */
    DeviceInfo->DeviceCount = Result;

    DPRINT("WdmAudControlDeviceType Devices %u\n", DeviceInfo->DeviceCount);
    return SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
WdmAudControlDeviceState(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    KSPROPERTY Property;
    KSSTATE State;
    NTSTATUS Status;
    ULONG BytesReturned;
    PFILE_OBJECT FileObject;

    DPRINT("WdmAudControlDeviceState\n");

    Status = ObReferenceObjectByHandle(DeviceInfo->hDevice, GENERIC_READ | GENERIC_WRITE, *IoFileObjectType, KernelMode, (PVOID*)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Error: invalid device handle provided %p Type %x\n", DeviceInfo->hDevice, DeviceInfo->DeviceType);
        return SetIrpIoStatus(Irp, STATUS_UNSUCCESSFUL, 0);
    }

    Property.Set = KSPROPSETID_Connection;
    Property.Id = KSPROPERTY_CONNECTION_STATE;
    Property.Flags = KSPROPERTY_TYPE_SET;

    State = DeviceInfo->u.State;

    Status = KsSynchronousIoControlDevice(FileObject, KernelMode, IOCTL_KS_PROPERTY, (PVOID)&Property, sizeof(KSPROPERTY), (PVOID)&State, sizeof(KSSTATE), &BytesReturned);

    ObDereferenceObject(FileObject);

    DPRINT("WdmAudControlDeviceState Status %x\n", Status);
    return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
WdmAudCapabilities(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    PWDMAUD_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    DPRINT("WdmAudCapabilities entered\n");

    DeviceExtension = (PWDMAUD_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (DeviceInfo->DeviceType == MIXER_DEVICE_TYPE)
    {
        Status = WdmAudMixerCapabilities(DeviceObject, DeviceInfo, ClientInfo, DeviceExtension);
    }
    else if (DeviceInfo->DeviceType == WAVE_IN_DEVICE_TYPE || DeviceInfo->DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        Status = WdmAudWaveCapabilities(DeviceObject, DeviceInfo, ClientInfo, DeviceExtension);
    }
    else if (DeviceInfo->DeviceType == MIDI_IN_DEVICE_TYPE || DeviceInfo->DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        Status = WdmAudMidiCapabilities(DeviceObject, DeviceInfo, ClientInfo, DeviceExtension);
    }

    return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
NTAPI
WdmAudIoctlClose(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    ULONG Index;

    for(Index = 0; Index < ClientInfo->NumPins; Index++)
    {
        if (ClientInfo->hPins[Index].Handle == DeviceInfo->hDevice && ClientInfo->hPins[Index].Type != MIXER_DEVICE_TYPE)
        {
            DPRINT1("Closing device %p\n", DeviceInfo->hDevice);
            ZwClose(DeviceInfo->hDevice);
            ClientInfo->hPins[Index].Handle = NULL;
            SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO));
            return STATUS_SUCCESS;
        }
        else if (ClientInfo->hPins[Index].Handle == DeviceInfo->hDevice && ClientInfo->hPins[Index].Type == MIXER_DEVICE_TYPE)
        {
            DPRINT1("Closing mixer %p\n", DeviceInfo->hDevice);
            return WdmAudControlCloseMixer(Irp, ClientInfo, Index);
        }
    }

    SetIrpIoStatus(Irp, STATUS_INVALID_PARAMETER, sizeof(WDMAUD_DEVICE_INFO));
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
NTAPI
WdmAudFrameSize(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo,
    IN  PWDMAUD_CLIENT ClientInfo)
{
    PFILE_OBJECT FileObject;
    KSPROPERTY Property;
    ULONG BytesReturned;
    KSALLOCATOR_FRAMING Framing;
    NTSTATUS Status;

    /* Get sysaudio pin file object */
    Status = ObReferenceObjectByHandle(DeviceInfo->hDevice, GENERIC_WRITE, *IoFileObjectType, KernelMode, (PVOID*)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Invalid buffer handle %p\n", DeviceInfo->hDevice);
        return SetIrpIoStatus(Irp, Status, 0);
    }

    /* Setup get framing request */
    Property.Id = KSPROPERTY_CONNECTION_ALLOCATORFRAMING;
    Property.Flags = KSPROPERTY_TYPE_GET;
    Property.Set = KSPROPSETID_Connection;

    Status = KsSynchronousIoControlDevice(FileObject, KernelMode, IOCTL_KS_PROPERTY, (PVOID)&Property, sizeof(KSPROPERTY), (PVOID)&Framing, sizeof(KSALLOCATOR_FRAMING), &BytesReturned);
    /* Did we succeed */
    if (NT_SUCCESS(Status))
    {
        /* Store framesize */
        DeviceInfo->u.FrameSize = Framing.FrameSize;
    }

    /* Release file object */
    ObDereferenceObject(FileObject);

    return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));

}

NTSTATUS
NTAPI
WdmAudGetDeviceInterface(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo)
{
    NTSTATUS Status;
    LPWSTR Device;
    ULONG Size, Length;

    /* get device interface string input length */
    Size = DeviceInfo->u.Interface.DeviceInterfaceStringSize;

   /* get mixer info */
   Status = WdmAudGetPnpNameByIndexAndType(DeviceInfo->DeviceIndex, DeviceInfo->DeviceType, &Device);

   /* check for success */
   if (!NT_SUCCESS(Status))
   {
        /* invalid device id */
        return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));
   }

   /* calculate length */
   Length = (wcslen(Device)+1) * sizeof(WCHAR);

    if (!Size)
    {
        /* store device interface size */
        DeviceInfo->u.Interface.DeviceInterfaceStringSize = Length;
    }
    else if (Size < Length)
    {
        /* buffer too small */
        DeviceInfo->u.Interface.DeviceInterfaceStringSize = Length;
        FreeItem(Device);
        return SetIrpIoStatus(Irp, STATUS_BUFFER_OVERFLOW, sizeof(WDMAUD_DEVICE_INFO));
    }
    else
    {
        _SEH2_TRY
        {
            ProbeForWrite(DeviceInfo->u.Interface.DeviceInterfaceString, Length, sizeof(WCHAR));
            RtlMoveMemory(DeviceInfo->u.Interface.DeviceInterfaceString, Device, Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            FreeItem(Device);
            return SetIrpIoStatus(Irp, _SEH2_GetExceptionCode(), 0);
        }
        _SEH2_END;
    }

    FreeItem(Device);
    return SetIrpIoStatus(Irp, STATUS_SUCCESS, sizeof(WDMAUD_DEVICE_INFO));
}

NTSTATUS
NTAPI
WdmAudResetStream(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp,
    IN  PWDMAUD_DEVICE_INFO DeviceInfo)
{
    KSRESET ResetStream;
    NTSTATUS Status;
    ULONG BytesReturned;
    PFILE_OBJECT FileObject;

    DPRINT("WdmAudResetStream\n");

    Status = ObReferenceObjectByHandle(DeviceInfo->hDevice, GENERIC_READ | GENERIC_WRITE, *IoFileObjectType, KernelMode, (PVOID*)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Error: invalid device handle provided %p Type %x\n", DeviceInfo->hDevice, DeviceInfo->DeviceType);
        return SetIrpIoStatus(Irp, STATUS_UNSUCCESSFUL, 0);
    }

    ResetStream = DeviceInfo->u.ResetStream;
    ASSERT(ResetStream == KSRESET_BEGIN || ResetStream == KSRESET_END);

    Status = KsSynchronousIoControlDevice(FileObject, KernelMode, IOCTL_KS_RESET_STATE, (PVOID)&ResetStream, sizeof(KSRESET), NULL, 0, &BytesReturned);

    ObDereferenceObject(FileObject);

    DPRINT("WdmAudResetStream Status %x\n", Status);
    return SetIrpIoStatus(Irp, Status, sizeof(WDMAUD_DEVICE_INFO));
}

static
NTSTATUS
WdmAudDuplicatePinHandle(
    IN PIRP Irp,
    IN PWDMAUD_DEVICE_INFO DeviceInfo,
    IN PWDMAUD_CLIENT ClientInfo)
{
    OBJECT_HANDLE_INFORMATION HandleInformation;
    PFILE_OBJECT FileObject;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < ClientInfo->NumPins; ++Index)
    {
        if (ClientInfo->hPins[Index].Handle == DeviceInfo->hDevice &&
            ClientInfo->hPins[Index].Type != MIXER_DEVICE_TYPE)
        {
            break;
        }
    }

    if (Index == ClientInfo->NumPins)
        return SetIrpIoStatus(Irp, STATUS_INVALID_HANDLE, 0);

    Status = ObReferenceObjectByHandle(DeviceInfo->hDevice,
                                       0,
                                       *IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&FileObject,
                                       &HandleInformation);
    if (!NT_SUCCESS(Status))
        return SetIrpIoStatus(Irp, Status, 0);

    Status = ObOpenObjectByPointer(FileObject,
                                   0,
                                   NULL,
                                   HandleInformation.GrantedAccess,
                                   *IoFileObjectType,
                                   KernelMode,
                                   &DeviceInfo->u.hUserDevice);
    ObDereferenceObject(FileObject);

    return SetIrpIoStatus(Irp,
                          Status,
                          NT_SUCCESS(Status) ? sizeof(WDMAUD_DEVICE_INFO) : 0);
}

static NTSTATUS
WdmAudDeviceControlDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _Inout_ PWDMAUD_DEVICE_INFO DeviceInfo,
    _In_ PWDMAUD_CLIENT ClientInfo,
    _In_ ULONG IoControlCode)
{
    switch (IoControlCode)
    {
        case IOCTL_OPEN_WDMAUD:
            return WdmAudControlOpen(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETNUMDEVS_TYPE:
            return WdmAudControlDeviceType(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_SETDEVICE_STATE:
            return WdmAudControlDeviceState(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETCAPABILITIES:
            return WdmAudCapabilities(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_CLOSE_WDMAUD:
            return WdmAudIoctlClose(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETFRAMESIZE:
            return WdmAudFrameSize(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETLINEINFO:
            return WdmAudGetLineInfo(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETLINECONTROLS:
            return WdmAudGetLineControls(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_SETCONTROLDETAILS:
            return WdmAudSetControlDetails(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETCONTROLDETAILS:
            return WdmAudGetControlDetails(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_QUERYDEVICEINTERFACESTRING:
            return WdmAudGetDeviceInterface(DeviceObject, Irp, DeviceInfo);
        case IOCTL_GET_MIXER_EVENT:
            return WdmAudGetMixerEvent(DeviceObject, Irp, DeviceInfo, ClientInfo);
        case IOCTL_RESET_STREAM:
            return WdmAudResetStream(DeviceObject, Irp, DeviceInfo);
        case IOCTL_DUPLICATE_WDMAUD_PIN:
            return WdmAudDuplicatePinHandle(Irp, DeviceInfo, ClientInfo);
        case IOCTL_GETPREFERRED_WAVE_FORMAT:
            return WdmAudGetPreferredWaveFormat(Irp, DeviceInfo);
        case IOCTL_GETWAVEMIXERID:
            return WdmAudGetWaveMixerId(Irp, DeviceInfo);
        case IOCTL_GETENDPOINT_STATE:
            return WdmAudGetEndpointState(Irp, DeviceInfo);
        case IOCTL_GETPOS:
            return WdmAudGetPosition(DeviceObject, Irp, DeviceInfo);
        case IOCTL_GETDEVID:
        case IOCTL_GETVOLUME:
        case IOCTL_SETVOLUME:

           DPRINT1("Unhandled %x\n", IoControlCode);
           break;
    }

    return SetIrpIoStatus(Irp, STATUS_NOT_IMPLEMENTED, 0);
}

NTSTATUS
NTAPI
WdmAudDeviceControl(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PWDMAUD_DEVICE_INFO DeviceInfo;
    PWDMAUD_CLIENT ClientInfo;
    ULONG IoControlCode;

#if defined(_WIN64)
    WDMAUD_DEVICE_INFO NativeDeviceInfo;
    PWDMAUD_DEVICE_INFO32 DeviceInfo32;
    PVOID OriginalSystemBuffer;
    PVOID OriginalDriverContext;
    ULONG OriginalInputLength, OriginalOutputLength;
    ULONG Information;
    NTSTATUS Status;
    BOOLEAN IsWow64;
#endif

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IoControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;

    DPRINT("WdmAudDeviceControl entered\n");

#if defined(_WIN64)
    IsWow64 = IoIs32bitProcess(Irp);
    if (IoStack->Parameters.DeviceIoControl.InputBufferLength <
        (IsWow64 ? sizeof(WDMAUD_DEVICE_INFO32) : sizeof(WDMAUD_DEVICE_INFO)))
#else
    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(WDMAUD_DEVICE_INFO))
#endif
    {
        /* invalid parameter */
        DPRINT1("Input buffer too small size %u expected %u\n",
                IoStack->Parameters.DeviceIoControl.InputBufferLength,
#if defined(_WIN64)
                IsWow64 ? sizeof(WDMAUD_DEVICE_INFO32) : sizeof(WDMAUD_DEVICE_INFO));
#else
                sizeof(WDMAUD_DEVICE_INFO));
#endif
        return SetIrpIoStatus(Irp, STATUS_INVALID_PARAMETER, 0);
    }

    if (!IoStack->FileObject || !IoStack->FileObject->FsContext)
    {
        /* file object parameter */
        DPRINT1("Error: file object is not attached\n");
        return SetIrpIoStatus(Irp, STATUS_UNSUCCESSFUL, 0);
    }
    ClientInfo = (PWDMAUD_CLIENT)IoStack->FileObject->FsContext;

#if defined(_WIN64)
    if (IsWow64)
    {
        DeviceInfo32 = (PWDMAUD_DEVICE_INFO32)Irp->AssociatedIrp.SystemBuffer;
        WdmAudDeviceInfo32ToNative(&NativeDeviceInfo, DeviceInfo32, IoControlCode);
        DeviceInfo = &NativeDeviceInfo;
    }
    else
#endif
    {
        DeviceInfo = (PWDMAUD_DEVICE_INFO)Irp->AssociatedIrp.SystemBuffer;
    }

    if (DeviceInfo->DeviceType < MIN_SOUND_DEVICE_TYPE ||
        DeviceInfo->DeviceType > MAX_SOUND_DEVICE_TYPE)
    {
        /* invalid parameter */
        DPRINT1("Error: device type not set\n");
        return SetIrpIoStatus(Irp, STATUS_INVALID_PARAMETER, 0);
    }

#if defined(_WIN64)
    if (IsWow64)
    {
        /* Let the native handlers operate on a native-sized temporary buffer. */
        OriginalSystemBuffer = Irp->AssociatedIrp.SystemBuffer;
        OriginalDriverContext = Irp->Tail.Overlay.DriverContext[3];
        OriginalInputLength = IoStack->Parameters.DeviceIoControl.InputBufferLength;
        OriginalOutputLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

        Irp->AssociatedIrp.SystemBuffer = &NativeDeviceInfo;
        Irp->Tail.Overlay.DriverContext[3] = WDMAUD_DEFER_IRP_COMPLETION;
        IoStack->Parameters.DeviceIoControl.InputBufferLength = sizeof(NativeDeviceInfo);
        IoStack->Parameters.DeviceIoControl.OutputBufferLength = sizeof(NativeDeviceInfo);

        Status = WdmAudDeviceControlDispatch(DeviceObject,
                                             Irp,
                                             &NativeDeviceInfo,
                                             ClientInfo,
                                             IoControlCode);
        Information = Irp->IoStatus.Information;

        Irp->AssociatedIrp.SystemBuffer = OriginalSystemBuffer;
        Irp->Tail.Overlay.DriverContext[3] = OriginalDriverContext;
        IoStack->Parameters.DeviceIoControl.InputBufferLength = OriginalInputLength;
        IoStack->Parameters.DeviceIoControl.OutputBufferLength = OriginalOutputLength;

        if (Information != 0)
        {
            WdmAudDeviceInfoNativeTo32(DeviceInfo32,
                                       &NativeDeviceInfo,
                                       IoControlCode);
            Information = sizeof(*DeviceInfo32);
        }

        return SetIrpIoStatus(Irp, Status, Information);
    }
#endif

    return WdmAudDeviceControlDispatch(DeviceObject,
                                       Irp,
                                       DeviceInfo,
                                       ClientInfo,
                                       IoControlCode);
}

NTSTATUS
NTAPI
IoCompletion (
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Ctx)
{
    PKSSTREAM_HEADER Header;
    PMDL Mdl, NextMdl;
    PWDMAUD_COMPLETION_CONTEXT Context = (PWDMAUD_COMPLETION_CONTEXT)Ctx;

#if defined(_WIN64)
    PWDMAUD_DEVICE_INFO32 DeviceInfo32;
#endif

    /* get stream header */
    Header = (PKSSTREAM_HEADER)Irp->AssociatedIrp.SystemBuffer;

    /* sanity check */
    ASSERT(Header);

    /* time to free all allocated mdls */
    Mdl = Irp->MdlAddress;

    while(Mdl)
    {
        /* get next mdl */
        NextMdl = Mdl->Next;

        /* unlock pages */
        MmUnlockPages(Mdl);

        /* grab next mdl */
        Mdl = NextMdl;
    }
    //IoFreeMdl(Mdl);

#if defined(_WIN64)
    if (Context->IsWow64)
    {
        DeviceInfo32 = (PWDMAUD_DEVICE_INFO32)
            MmGetSystemAddressForMdlSafe(Context->Mdl, NormalPagePriority);
        if (DeviceInfo32 != NULL)
        {
            WdmAudDeviceInfoNativeTo32(DeviceInfo32,
                                       (PWDMAUD_DEVICE_INFO)Header,
                                       0);
            DeviceInfo32->Header.Size = sizeof(*DeviceInfo32);
        }

        Irp->AssociatedIrp.SystemBuffer = Context->OriginalSystemBuffer;
        FreeItem(Header);
    }
#endif

    /* clear mdl list */
    Irp->MdlAddress = Context->Mdl;



    DPRINT("IoCompletion Irp %p IoStatus %lx Information %lx\n", Irp, Irp->IoStatus.Status, Irp->IoStatus.Information);

    if (!NT_SUCCESS(Irp->IoStatus.Status))
    {
        /* failed */
        Irp->IoStatus.Information = 0;
    }

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    /* dereference file object */
    ObDereferenceObject(Context->FileObject);

    /* free context */
    FreeItem(Context);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
WdmAudReadWrite(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PIRP Irp)
{
    NTSTATUS Status;
    PWDMAUD_DEVICE_INFO DeviceInfo;
    PFILE_OBJECT FileObject;
    PIO_STACK_LOCATION IoStack;
    ULONG Length;
    PMDL Mdl;
    BOOLEAN Read = TRUE;
    PWDMAUD_COMPLETION_CONTEXT Context;

#if defined(_WIN64)
    BOOLEAN IsWow64;
    PWDMAUD_DEVICE_INFO32 DeviceInfo32;
    PWDMAUD_DEVICE_INFO NativeDeviceInfo;
#endif

    /* allocate completion context */
    Context = AllocateItem(NonPagedPool, sizeof(WDMAUD_COMPLETION_CONTEXT));

    if (!Context)
    {
        /* not enough memory */
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        /* done */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* get current irp stack location */
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    /* store the input buffer in UserBuffer - as KsProbeStreamIrp operates on IRP_MJ_DEVICE_CONTROL */
    Irp->UserBuffer = MmGetMdlVirtualAddress(Irp->MdlAddress);

    /* sanity check */
    ASSERT(Irp->UserBuffer);

    /* get the length of the request length */
    Length = IoStack->Parameters.Write.Length;

    /* setup context */
    Context->Length = Length;
    Context->Function = (IoStack->MajorFunction == IRP_MJ_WRITE ? IOCTL_KS_WRITE_STREAM : IOCTL_KS_READ_STREAM);
    Context->Mdl = Irp->MdlAddress;
    Context->OriginalSystemBuffer = Irp->AssociatedIrp.SystemBuffer;

    /* store mdl address */
    Mdl = Irp->MdlAddress;

#if defined(_WIN64)
    IsWow64 = IoIs32bitProcess(Irp);
    Context->IsWow64 = IsWow64;
    if (IsWow64)
    {
        if (Length < sizeof(WDMAUD_DEVICE_INFO32))
        {
            FreeItem(Context);
            return SetIrpIoStatus(Irp, STATUS_INVALID_BUFFER_SIZE, 0);
        }

        DeviceInfo32 = (PWDMAUD_DEVICE_INFO32)
            MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (DeviceInfo32 == NULL)
        {
            FreeItem(Context);
            return SetIrpIoStatus(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        }

        NativeDeviceInfo = AllocateItem(NonPagedPool, sizeof(*NativeDeviceInfo));
        if (NativeDeviceInfo == NULL)
        {
            FreeItem(Context);
            return SetIrpIoStatus(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        }

        WdmAudDeviceInfo32ToNative(NativeDeviceInfo, DeviceInfo32, 0);
        NativeDeviceInfo->Header.Size = sizeof(*NativeDeviceInfo);
        Irp->AssociatedIrp.SystemBuffer = NativeDeviceInfo;
        Length = sizeof(*NativeDeviceInfo);
    }
#endif

    /* store outputbuffer length */
    IoStack->Parameters.DeviceIoControl.OutputBufferLength = Length;

    /* remove mdladdress as KsProbeStreamIrp will interpret it as an already probed audio buffer */
    Irp->MdlAddress = NULL;

    if (IoStack->MajorFunction == IRP_MJ_WRITE)
    {
        /* probe the write stream irp */
        Read = FALSE;
        Status = KsProbeStreamIrp(Irp, KSPROBE_STREAMWRITE | KSPROBE_ALLOCATEMDL | KSPROBE_PROBEANDLOCK, Length);
    }
    else
    {
        /* probe the read stream irp */
        Status = KsProbeStreamIrp(Irp, KSPROBE_STREAMREAD | KSPROBE_ALLOCATEMDL | KSPROBE_PROBEANDLOCK, Length);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KsProbeStreamIrp failed with Status %x Cancel %u\n", Status, Irp->Cancel);
        Irp->MdlAddress = Mdl;
#if defined(_WIN64)
        if (Context->IsWow64)
        {
            FreeItem(Irp->AssociatedIrp.SystemBuffer);
            Irp->AssociatedIrp.SystemBuffer = Context->OriginalSystemBuffer;
        }
#endif
        FreeItem(Context);
        return SetIrpIoStatus(Irp, Status, 0);
    }

    /* get device info */
    DeviceInfo = (PWDMAUD_DEVICE_INFO)Irp->AssociatedIrp.SystemBuffer;
    ASSERT(DeviceInfo);

    /* now get sysaudio file object */
    Status = ObReferenceObjectByHandle(DeviceInfo->hDevice, GENERIC_WRITE, *IoFileObjectType, KernelMode, (PVOID*)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Invalid pin handle %p\n", DeviceInfo->hDevice);
        Irp->MdlAddress = Mdl;
#if defined(_WIN64)
        if (Context->IsWow64)
        {
            FreeItem(Irp->AssociatedIrp.SystemBuffer);
            Irp->AssociatedIrp.SystemBuffer = Context->OriginalSystemBuffer;
        }
#endif
        FreeItem(Context);
        return SetIrpIoStatus(Irp, Status, 0);
    }

    /* store file object whose reference is released in the completion callback */
    Context->FileObject = FileObject;

    /* skip current irp stack location */
    IoSkipCurrentIrpStackLocation(Irp);

    /* get next stack location */
    IoStack = IoGetNextIrpStackLocation(Irp);

    /* prepare stack location */
    IoStack->FileObject = FileObject;
    IoStack->Parameters.Write.Length = Length;
    IoStack->MajorFunction = IRP_MJ_WRITE;
    IoStack->Parameters.DeviceIoControl.IoControlCode = (Read ? IOCTL_KS_READ_STREAM : IOCTL_KS_WRITE_STREAM);
    IoSetCompletionRoutine(Irp, IoCompletion, (PVOID)Context, TRUE, TRUE, TRUE);

    /* mark irp as pending */
//    IoMarkIrpPending(Irp);
    /* call the driver */
    Status = IoCallDriver(IoGetRelatedDeviceObject(FileObject), Irp);
    return Status;
}
