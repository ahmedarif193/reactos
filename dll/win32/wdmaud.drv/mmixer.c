/*
 * PROJECT:     ReactOS Sound System
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/win32/wdmaud.drv/mmixer.c
 *
 * PURPOSE:     WDM Audio Mixer API (User-mode part)
 * PROGRAMMERS: Johannes Anderwald
 */

#include "wdmaud.h"

#include <winreg.h>
#include <setupapi.h>
#include <mmixer.h>
#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>
#include <ndk/iofuncs.h>
#define NDEBUG
#include <debug.h>
#include <mmebuddy_debug.h>

BOOL MMixerLibraryInitialized = FALSE;

DWORD
WINAPI
RTStreamingThreadProc(
    LPVOID Parameter);

DWORD
WINAPI
RTStreamingCompletionThreadProc(
    IN  PVOID Parameter);

static
VOID
WdmAudCloseHandle(
    _Inout_ PHANDLE Handle)
{
    if (*Handle)
    {
        CloseHandle(*Handle);
        *Handle = NULL;
    }
}

static
VOID
WdmAudCloseRTStreamingEvents(
    _Inout_ PSOUND_DEVICE_INSTANCE Instance)
{
    WdmAudCloseHandle(&Instance->hNotifyRTStreamingEvent);
    WdmAudCloseHandle(&Instance->hNotifyRTStreamingStopEvent);
    WdmAudCloseHandle(&Instance->hNotifyRTStreamingCompletionEvent);
    WdmAudCloseHandle(&Instance->hNotifyRTStreamingCompletionReadyEvent);
    WdmAudCloseHandle(&Instance->hNotifyRTStreamingCompletionFinishEvent);
    WdmAudCloseHandle(&Instance->hNotifyRTStreamingCompletionStopEvent);
}

static
VOID
WdmAudStopRTStreamingThreads(
    _Inout_ PSOUND_DEVICE_INSTANCE Instance)
{
    if (Instance->hRTStreamingThread)
        SetEvent(Instance->hNotifyRTStreamingStopEvent);
    if (Instance->hRTStreamingCompletionThread)
        SetEvent(Instance->hNotifyRTStreamingCompletionStopEvent);

    if (Instance->hRTStreamingThread)
        WaitForSingleObject(Instance->hRTStreamingThread, INFINITE);
    if (Instance->hRTStreamingCompletionThread)
        WaitForSingleObject(Instance->hRTStreamingCompletionThread, INFINITE);

    WdmAudCloseHandle(&Instance->hRTStreamingThread);
    WdmAudCloseHandle(&Instance->hRTStreamingCompletionThread);
}

static
BOOL
WdmAudStartRTStreamingThreads(
    _Inout_ PSOUND_DEVICE_INSTANCE Instance)
{
    if (!Instance->hRTStreamingThread)
    {
        ResetEvent(Instance->hNotifyRTStreamingStopEvent);
        Instance->hRTStreamingThread =
            CreateThread(NULL, 0, RTStreamingThreadProc, Instance, 0, NULL);
    }

    if (!Instance->hRTStreamingCompletionThread)
    {
        ResetEvent(Instance->hNotifyRTStreamingCompletionStopEvent);
        Instance->hRTStreamingCompletionThread =
            CreateThread(NULL, 0, RTStreamingCompletionThreadProc, Instance, 0, NULL);
    }

    if (Instance->hRTStreamingThread &&
        Instance->hRTStreamingCompletionThread)
    {
        return TRUE;
    }

    WdmAudStopRTStreamingThreads(Instance);
    return FALSE;
}

PVOID Alloc(ULONG NumBytes);
MIXER_STATUS Close(HANDLE hDevice);
VOID Free(PVOID Block);
VOID Copy(PVOID Src, PVOID Dst, ULONG NumBytes);
MIXER_STATUS Open(IN LPWSTR DevicePath, OUT PHANDLE hDevice);
MIXER_STATUS Control(IN HANDLE hMixer, IN ULONG dwIoControlCode, IN PVOID lpInBuffer, IN ULONG nInBufferSize, OUT PVOID lpOutBuffer, ULONG nOutBufferSize, PULONG lpBytesReturned);
MIXER_STATUS Enum(IN  PVOID EnumContext, IN  ULONG DeviceIndex, OUT LPWSTR * DeviceName, OUT PHANDLE OutHandle, OUT PHANDLE OutKey);
MIXER_STATUS OpenKey(IN HANDLE hKey, IN LPWSTR SubKey, IN ULONG DesiredAccess, OUT PHANDLE OutKey);
MIXER_STATUS CloseKey(IN HANDLE hKey);
MIXER_STATUS QueryKeyValue(IN HANDLE hKey, IN LPWSTR KeyName, OUT PVOID * ResultBuffer, OUT PULONG ResultLength, OUT PULONG KeyType);
PVOID AllocEventData(IN ULONG ExtraSize);
VOID FreeEventData(IN PVOID EventData);

MIXER_CONTEXT MixerContext =
{
    sizeof(MIXER_CONTEXT),
    NULL,
    Alloc,
    Control,
    Free,
    Open,
    Close,
    Copy,
    OpenKey,
    QueryKeyValue,
    CloseKey,
    AllocEventData,
    FreeEventData
};

GUID CategoryGuid = {STATIC_KSCATEGORY_AUDIO};

MIXER_STATUS
QueryKeyValue(
    IN HANDLE hKey,
    IN LPWSTR KeyName,
    OUT PVOID * ResultBuffer,
    OUT PULONG ResultLength,
    OUT PULONG KeyType)
{
    if (RegQueryValueExW((HKEY)hKey, KeyName, NULL, KeyType, NULL, ResultLength) == ERROR_FILE_NOT_FOUND)
        return MM_STATUS_UNSUCCESSFUL;

    *ResultBuffer = HeapAlloc(GetProcessHeap(), 0, *ResultLength);
    if (*ResultBuffer == NULL)
        return MM_STATUS_NO_MEMORY;

    if (RegQueryValueExW((HKEY)hKey, KeyName, NULL, KeyType, *ResultBuffer, ResultLength) != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, *ResultBuffer);
        return MM_STATUS_UNSUCCESSFUL;
    }
    return MM_STATUS_SUCCESS;
}

MIXER_STATUS
OpenKey(
    IN HANDLE hKey,
    IN LPWSTR SubKey,
    IN ULONG DesiredAccess,
    OUT PHANDLE OutKey)
{
    if (RegOpenKeyExW((HKEY)hKey, SubKey, 0, DesiredAccess, (PHKEY)OutKey) == ERROR_SUCCESS)
        return MM_STATUS_SUCCESS;

    return MM_STATUS_UNSUCCESSFUL;
}

MIXER_STATUS
CloseKey(
    IN HANDLE hKey)
{
    RegCloseKey((HKEY)hKey);
    return MM_STATUS_SUCCESS;
}


PVOID Alloc(ULONG NumBytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NumBytes);
}

MIXER_STATUS
Close(HANDLE hDevice)
{
    if (CloseHandle(hDevice))
        return MM_STATUS_SUCCESS;
    else
        return MM_STATUS_UNSUCCESSFUL;
}

VOID
Free(PVOID Block)
{
    HeapFree(GetProcessHeap(), 0, Block);
}

VOID
Copy(PVOID Src, PVOID Dst, ULONG NumBytes)
{
    RtlMoveMemory(Src, Dst, NumBytes);
}

MIXER_STATUS
Open(
    IN LPWSTR DevicePath,
    OUT PHANDLE hDevice)
{
     DevicePath[1] = L'\\';
    *hDevice = CreateFileW(DevicePath,
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED,
                           NULL);
    if (*hDevice == INVALID_HANDLE_VALUE)
    {
        return MM_STATUS_UNSUCCESSFUL;
    }

    return MM_STATUS_SUCCESS;
}

MIXER_STATUS
Control(
    IN HANDLE hMixer,
    IN ULONG dwIoControlCode,
    IN PVOID lpInBuffer,
    IN ULONG nInBufferSize,
    OUT PVOID lpOutBuffer,
    ULONG nOutBufferSize,
    PULONG lpBytesReturned)
{
    OVERLAPPED Overlapped;
    BOOL IoResult;
    DWORD Transferred = 0;
    DWORD Error;

    /* Overlapped I/O is done here - this is used for waiting for completion */
    ZeroMemory(&Overlapped, sizeof(OVERLAPPED));
    Overlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if ( ! Overlapped.hEvent )
        return MM_STATUS_NO_MEMORY;

    /* Talk to the device */
    IoResult = DeviceIoControl(hMixer,
                               dwIoControlCode,
                               lpInBuffer,
                               nInBufferSize,
                               lpOutBuffer,
                               nOutBufferSize,
                               &Transferred,
                               &Overlapped);
    Error = IoResult ? ERROR_SUCCESS : GetLastError();
    if (!IoResult)
    {
        if (Error == ERROR_IO_PENDING)
        {
            IoResult = GetOverlappedResult(hMixer,
                                           &Overlapped,
                                           &Transferred,
                                           TRUE);
            if (!IoResult)
                Error = GetLastError();
        }

        if (!IoResult)
        {
            CloseHandle(Overlapped.hEvent);

            if (Error == ERROR_MORE_DATA || Error == ERROR_INSUFFICIENT_BUFFER)
            {
                if (lpBytesReturned)
                    *lpBytesReturned = Transferred;
                return MM_STATUS_MORE_ENTRIES;
            }

            return MM_STATUS_UNSUCCESSFUL;
        }
    }

    CloseHandle(Overlapped.hEvent);
    if (lpBytesReturned)
        *lpBytesReturned = Transferred;
    return MM_STATUS_SUCCESS;
}

MIXER_STATUS
Enum(
    IN  PVOID EnumContext,
    IN  ULONG DeviceIndex,
    OUT LPWSTR * DeviceName,
    OUT PHANDLE OutHandle,
    OUT PHANDLE OutKey)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    SP_DEVINFO_DATA DeviceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DetailData;
    BOOL Result;
    DWORD Length;
    MIXER_STATUS Status;

    //printf("Enum EnumContext %p DeviceIndex %lu OutHandle %p\n", EnumContext, DeviceIndex, OutHandle);

    InterfaceData.cbSize = sizeof(InterfaceData);
    InterfaceData.Reserved = 0;

    Result = SetupDiEnumDeviceInterfaces(EnumContext,
                                NULL,
                                &CategoryGuid,
                                DeviceIndex,
                                &InterfaceData);

    if (!Result)
    {
        if (GetLastError() == ERROR_NO_MORE_ITEMS)
        {
            return MM_STATUS_NO_MORE_DEVICES;
        }
        return MM_STATUS_UNSUCCESSFUL;
    }

    Length = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + MAX_PATH * sizeof(WCHAR);
    DetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(),
                                                             0,
                                                             Length);
    DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    DeviceData.cbSize = sizeof(DeviceData);
    DeviceData.Reserved = 0;

    Result = SetupDiGetDeviceInterfaceDetailW(EnumContext,
                                    &InterfaceData,
                                    DetailData,
                                    Length,
                                    NULL,
                                    &DeviceData);

    if (!Result)
    {
        DPRINT("SetupDiGetDeviceInterfaceDetailW failed with %lu\n", GetLastError());
        return MM_STATUS_UNSUCCESSFUL;
    }


    *OutKey = SetupDiOpenDeviceInterfaceRegKey(EnumContext, &InterfaceData, 0, KEY_READ);
     if ((HKEY)*OutKey == INVALID_HANDLE_VALUE)
     {
        HeapFree(GetProcessHeap(), 0, DetailData);
        return MM_STATUS_UNSUCCESSFUL;
    }

    Status = Open(DetailData->DevicePath, OutHandle);

    if (Status != MM_STATUS_SUCCESS)
    {
        RegCloseKey((HKEY)*OutKey);
        HeapFree(GetProcessHeap(), 0, DetailData);
        return Status;
    }

    *DeviceName = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, (wcslen(DetailData->DevicePath)+1) * sizeof(WCHAR));
    if (*DeviceName == NULL)
    {
        CloseHandle(*OutHandle);
        RegCloseKey((HKEY)*OutKey);
        HeapFree(GetProcessHeap(), 0, DetailData);
        return MM_STATUS_NO_MEMORY;
    }
    wcscpy(*DeviceName, DetailData->DevicePath);
    HeapFree(GetProcessHeap(), 0, DetailData);

    return Status;
}

PVOID
AllocEventData(
    IN ULONG ExtraSize)
{
    PKSEVENTDATA Data = (PKSEVENTDATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(KSEVENTDATA) + ExtraSize);
    if (!Data)
        return NULL;

    Data->EventHandle.Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!Data->EventHandle.Event)
    {
        HeapFree(GetProcessHeap(), 0, Data);
        return NULL;
    }

    Data->NotificationType = KSEVENTF_EVENT_HANDLE;
    return Data;
}

VOID
FreeEventData(IN PVOID EventData)
{
    PKSEVENTDATA Data = (PKSEVENTDATA)EventData;

    CloseHandle(Data->EventHandle.Event);
    HeapFree(GetProcessHeap(), 0, Data);
}


BOOL
WdmAudInitUserModeMixer()
{
    HDEVINFO DeviceHandle;
    MIXER_STATUS Status;

    if (MMixerLibraryInitialized)
    {
        /* Library is already initialized */
        return TRUE;
    }


    /* Create a device list */
    DeviceHandle = SetupDiGetClassDevs(&CategoryGuid,
                                       NULL,
                                       NULL,
                                       DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    if (DeviceHandle == INVALID_HANDLE_VALUE)
    {
        /* Failed to create a device list */
        return FALSE;
    }


    /* Initialize the mixer library */
    Status = MMixerInitialize(&MixerContext, Enum, (PVOID)DeviceHandle);

    /* Free device list */
    SetupDiDestroyDeviceInfoList(DeviceHandle);

    if (Status != MM_STATUS_SUCCESS)
    {
        /* Failed to initialize mixer library */
        DPRINT1("Failed to initialize mixer library with %x\n", Status);
        return FALSE;
    }

    /* Library is now initialized */
    MMixerLibraryInitialized = TRUE;

    /* Completed successfully */
    return TRUE;
}

MMRESULT
WdmAudCleanupByMMixer()
{
    /* TODO */
    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudGetMixerCapabilities(
    IN ULONG DeviceId,
    LPMIXERCAPSW Capabilities)
{
    if (MMixerGetCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_BADDEVICEID;
}

MMRESULT
WdmAudGetLineInfo(
    IN HANDLE hMixer,
    IN DWORD MixerId,
    IN LPMIXERLINEW MixLine,
    IN ULONG Flags)
{
    if (MMixerGetLineInfo(&MixerContext, hMixer, MixerId, Flags, MixLine)  == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudGetLineControls(
    IN HANDLE hMixer,
    IN DWORD MixerId,
    IN LPMIXERLINECONTROLSW MixControls,
    IN ULONG Flags)
{
    if (MMixerGetLineControls(&MixerContext, hMixer, MixerId, Flags, MixControls) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudSetControlDetails(
    IN HANDLE hMixer,
    IN DWORD MixerId,
    IN LPMIXERCONTROLDETAILS MixDetails,
    IN ULONG Flags)
{
    if (MMixerSetControlDetails(&MixerContext, hMixer, MixerId, Flags, MixDetails) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;

}

MMRESULT
WdmAudGetControlDetails(
    IN HANDLE hMixer,
    IN DWORD MixerId,
    IN LPMIXERCONTROLDETAILS MixDetails,
    IN ULONG Flags)
{
    if (MMixerGetControlDetails(&MixerContext, hMixer, MixerId, Flags, MixDetails) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudGetWaveOutCapabilities(
    IN ULONG DeviceId,
    LPWAVEOUTCAPSW Capabilities)
{
    if (MMixerWaveOutCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;

}

MMRESULT
WdmAudGetWaveInCapabilities(
    IN ULONG DeviceId,
    LPWAVEINCAPSW Capabilities)
{
    if (MMixerWaveInCapabilities(&MixerContext, DeviceId, Capabilities) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudQueryWaveFormatSupportByMMixer(
    IN  PSOUND_DEVICE SoundDevice,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize)
{
    MMDEVICE_TYPE DeviceType;
    PVOID Identifier;
    MMRESULT Result;
    MIXER_STATUS Status;

    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    if (!MMSUCCESS(Result) ||
        (DeviceType != WAVE_IN_DEVICE_TYPE &&
         DeviceType != WAVE_OUT_DEVICE_TYPE))
    {
        return MMSYSERR_BADDEVICEID;
    }

    Result = GetSoundDeviceIdentifier(SoundDevice, &Identifier);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    Status = MMixerQueryWaveFormatSupport(&MixerContext,
                                          PtrToUlong(Identifier),
                                          DeviceType == WAVE_IN_DEVICE_TYPE,
                                          WaveFormat,
                                          WaveFormatSize);
    if (Status == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;
    if (Status == MM_STATUS_NO_MEMORY)
        return MMSYSERR_NOMEM;

    return WAVERR_BADFORMAT;
}

MMRESULT
WdmAudSetWaveDeviceFormatByMMixer(
    IN  PSOUND_DEVICE_INSTANCE Instance,
    IN  DWORD DeviceId,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize)
{
    MMDEVICE_TYPE DeviceType;
    PSOUND_DEVICE SoundDevice;
    MMRESULT Result;
    BOOL bWaveIn;
    BOOL EventRegistered = FALSE;

    DPRINT("SetWaveDeviceFormatByMMixer\n");
    Result = GetSoundDeviceFromInstance(Instance, &SoundDevice);

    if ( ! MMSUCCESS(Result) )
    {
        return TranslateInternalMmResult(Result);
    }

    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    ASSERT( Result == MMSYSERR_NOERROR );

    bWaveIn = (DeviceType == WAVE_IN_DEVICE_TYPE ? TRUE : FALSE);

    if (MMixerOpenWave(&MixerContext, DeviceId, bWaveIn, WaveFormat, NULL, NULL, &Instance->Handle) == MM_STATUS_SUCCESS)
    {
        MIXER_STATUS MixerStatus = MMixerInitializeRTStreamingBuffer(
            &MixerContext,
            Instance->Handle,
            PAGE_SIZE * 8,
            2,
            &Instance->RTStreamingBuffer,
            &Instance->RTStreamingBufferLength);
        if (MixerStatus == MM_STATUS_SUCCESS)
        {
            DPRINT("RTStreamingBuffer %p Length %u\n",
            Instance->RTStreamingBuffer,
            Instance->RTStreamingBufferLength);

            /* Clear buffer */
            RtlZeroMemory(Instance->RTStreamingBuffer, Instance->RTStreamingBufferLength);
            /* Set offset */
            Instance->RTStreamingBufferOffset = 0;
            Instance->hNotifyRTStreamingEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            Instance->hNotifyRTStreamingStopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            Instance->hNotifyRTStreamingCompletionEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            Instance->hNotifyRTStreamingCompletionReadyEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
            Instance->hNotifyRTStreamingCompletionFinishEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            Instance->hNotifyRTStreamingCompletionStopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            if (Instance->hNotifyRTStreamingEvent == NULL ||
                Instance->hNotifyRTStreamingStopEvent == NULL ||
                Instance->hNotifyRTStreamingCompletionEvent == NULL ||
                Instance->hNotifyRTStreamingCompletionReadyEvent == NULL ||
                Instance->hNotifyRTStreamingCompletionFinishEvent == NULL ||
                Instance->hNotifyRTStreamingCompletionStopEvent == NULL
            )
            {
                DPRINT1("Failed to create event with %x", GetLastError());
                goto FailedRTStreamingSetup;
            }
            MixerStatus = MMixerRegisterRTStreamingEvent(&MixerContext, Instance->Handle, Instance->hNotifyRTStreamingEvent);
            if (MixerStatus != MM_STATUS_SUCCESS)
                goto FailedRTStreamingSetup;

            EventRegistered = TRUE;
            Instance->RTStreamingShadowBuffer = (PUCHAR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Instance->RTStreamingBufferLength);
            if (Instance->RTStreamingShadowBuffer == NULL)
            {
                DPRINT1("Failed to create shadow buffer with %x\n", GetLastError());
                goto FailedRTStreamingSetup;
            }

            Instance->RTStreamingShadowBufferLength = Instance->RTStreamingBufferLength;
            Instance->RTStreamingNotificationCount = 2;
            Instance->RTStreamingShadowBufferBytesUsed = 0;
            Instance->RTStreamingShadowBufferReadOffset = 0;
            Instance->RTStreamingShadowBufferWriteOffset = 0;
            Instance->RTStreamingBufferBytesWritten = 0;
            Instance->RTStreamingNextPacketNumber = 0;
            Instance->RTStreamingPacketError = 0;
            Instance->RTStreamingUnderrunCount = 0;
            Instance->RTStreamingUnderrunBytes = 0;
            Instance->LegacyStreaming = FALSE;
            Instance->RTStreamingEnabled = TRUE;
        }
        else
        {
            Instance->LegacyStreaming = TRUE;
        }

        if (DeviceType == WAVE_OUT_DEVICE_TYPE && !Instance->RTStreamingEnabled)
        {
            MMixerSetWaveStatus(&MixerContext, Instance->Handle, KSSTATE_ACQUIRE);
            MMixerSetWaveStatus(&MixerContext, Instance->Handle, KSSTATE_PAUSE);
            MMixerSetWaveStatus(&MixerContext, Instance->Handle, KSSTATE_RUN);
        }
        return MMSYSERR_NOERROR;

FailedRTStreamingSetup:
        if (EventRegistered)
        {
            MMixerUnregisterRTStreamingEvent(&MixerContext,
                                             Instance->Handle,
                                             Instance->hNotifyRTStreamingEvent);
        }
        WdmAudCloseRTStreamingEvents(Instance);
        HeapFree(GetProcessHeap(), 0, Instance->RTStreamingShadowBuffer);
        Instance->RTStreamingShadowBuffer = NULL;
        Instance->RTStreamingShadowBufferLength = 0;
        Instance->RTStreamingBuffer = NULL;
        Instance->RTStreamingBufferLength = 0;
        Instance->RTStreamingNotificationCount = 0;
        return MMSYSERR_ERROR;
    }
    return MMSYSERR_ERROR;
}


MMRESULT
WdmAudGetCapabilitiesByMMixer(
    IN  PSOUND_DEVICE SoundDevice,
    IN  DWORD DeviceId,
    OUT PVOID Capabilities,
    IN  DWORD CapabilitiesSize)
{
    MMDEVICE_TYPE DeviceType;
    MMRESULT Result;

    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    SND_ASSERT( Result == MMSYSERR_NOERROR );

    if (DeviceType == MIXER_DEVICE_TYPE)
    {
        return WdmAudGetMixerCapabilities(DeviceId, (LPMIXERCAPSW)Capabilities);
    }
    else if (DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        return WdmAudGetWaveOutCapabilities(DeviceId, (LPWAVEOUTCAPSW)Capabilities);
    }
    else if (DeviceType == WAVE_IN_DEVICE_TYPE)
    {
        return WdmAudGetWaveInCapabilities(DeviceId, (LPWAVEINCAPSW)Capabilities);
    }
    else
    {
        /* Not supported */
        return MMSYSERR_ERROR;
    }
}

MMRESULT
WdmAudOpenSoundDeviceByMMixer(
    IN  struct _SOUND_DEVICE* SoundDevice,
    OUT PVOID* Handle)
{
    if (WdmAudInitUserModeMixer())
        return MMSYSERR_NOERROR;
    else
        return MMSYSERR_ERROR;
}

MMRESULT
WdmAudCloseSoundDeviceByMMixer(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance,
    IN  PVOID Handle)
{
    MMDEVICE_TYPE DeviceType;
    PSOUND_DEVICE SoundDevice;
    MMRESULT Result;

    DPRINT("CloseSoundDeviceByMMixer\n");

    Result = GetSoundDeviceFromInstance(SoundDeviceInstance, &SoundDevice);

    if ( ! MMSUCCESS(Result) )
    {
        return TranslateInternalMmResult(Result);
    }

    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    SND_ASSERT( Result == MMSYSERR_NOERROR );

    if (DeviceType == MIXER_DEVICE_TYPE)
    {
        /* No op */
        return MMSYSERR_NOERROR;
    }
    else if (DeviceType == WAVE_IN_DEVICE_TYPE || DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        /* Make sure the pin is stopped */
        MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
        MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
        MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_STOP);
        if (SoundDeviceInstance->RTStreamingEnabled)
        {
            SoundDeviceInstance->RTStreamingEnabled = FALSE;
            WdmAudStopRTStreamingThreads(SoundDeviceInstance);

            DPRINT("closing device handling\n");
            SoundDeviceInstance->RTStreamingBuffer = NULL;
            SoundDeviceInstance->RTStreamingBufferLength = 0;
            SoundDeviceInstance->RTStreamingNotificationCount = 0;
            SoundDeviceInstance->RTStreamingBufferOffset = 0;
            MMixerUnregisterRTStreamingEvent(&MixerContext,
                                             SoundDeviceInstance->Handle,
                                             SoundDeviceInstance->hNotifyRTStreamingEvent);
            HeapFree(GetProcessHeap(), 0, SoundDeviceInstance->RTStreamingShadowBuffer);
            SoundDeviceInstance->RTStreamingShadowBuffer = NULL;
            SoundDeviceInstance->RTStreamingShadowBufferLength = 0;
            WdmAudCloseRTStreamingEvents(SoundDeviceInstance);
        }
        CloseHandle(Handle);
        return MMSYSERR_NOERROR;
    }

    /* Midi is not supported */
    return MMSYSERR_ERROR;
}

MMRESULT
WdmAudGetNumWdmDevsByMMixer(
    IN  MMDEVICE_TYPE DeviceType,
    OUT DWORD* DeviceCount)
{
    switch(DeviceType)
    {
        case MIXER_DEVICE_TYPE:
            *DeviceCount = MMixerGetCount(&MixerContext);
            break;
        case WAVE_OUT_DEVICE_TYPE:
            *DeviceCount = MMixerGetWaveOutCount(&MixerContext);
            break;
        case WAVE_IN_DEVICE_TYPE:
            *DeviceCount = MMixerGetWaveInCount(&MixerContext);
            break;
        default:
            *DeviceCount = 0;
    }
    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudQueryMixerInfoByMMixer(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance,
    IN DWORD MixerId,
    IN UINT uMsg,
    IN LPVOID Parameter,
    IN DWORD Flags)
{
    LPMIXERLINEW MixLine;
    LPMIXERLINECONTROLSW MixControls;
    LPMIXERCONTROLDETAILS MixDetails;
    HANDLE hMixer = NULL;

    MixLine = (LPMIXERLINEW)Parameter;
    MixControls = (LPMIXERLINECONTROLSW)Parameter;
    MixDetails = (LPMIXERCONTROLDETAILS)Parameter;

    /* FIXME param checks */

    if (SoundDeviceInstance)
    {
        hMixer = SoundDeviceInstance->Handle;
    }

    switch(uMsg)
    {
        case MXDM_GETLINEINFO:
            return WdmAudGetLineInfo(hMixer, MixerId, MixLine, Flags);
        case MXDM_GETLINECONTROLS:
            return WdmAudGetLineControls(hMixer, MixerId, MixControls, Flags);
        case MXDM_SETCONTROLDETAILS:
            return WdmAudSetControlDetails(hMixer, MixerId, MixDetails, Flags);
        case MXDM_GETCONTROLDETAILS:
            return WdmAudGetControlDetails(hMixer, MixerId, MixDetails, Flags);
        default:
            DPRINT1("MixerId %lu, uMsg %lu, Parameter %p, Flags %lu\n", MixerId, uMsg, Parameter, Flags);
            SND_ASSERT(0);
            return MMSYSERR_NOTSUPPORTED;
    }
}

MMRESULT
WdmAudGetDeviceInterfaceStringByMMixer(
    IN  MMDEVICE_TYPE DeviceType,
    IN  DWORD DeviceId,
    IN  LPWSTR Interface,
    IN  DWORD  InterfaceLength,
    OUT  DWORD * InterfaceSize)
{
    /* FIXME */
    return MMSYSERR_NOTSUPPORTED;
}

VOID
CALLBACK
MixerEventCallback(
    IN PVOID MixerEventContext,
    IN HANDLE hMixer,
    IN ULONG NotificationType,
    IN ULONG Value)
{
    PSOUND_DEVICE_INSTANCE Instance = (PSOUND_DEVICE_INSTANCE)MixerEventContext;

    DriverCallback(Instance->WinMM.ClientCallback,
                   HIWORD(Instance->WinMM.Flags),
                   Instance->WinMM.Handle,
                   NotificationType,
                   Instance->WinMM.ClientCallbackInstanceData,
                   (DWORD_PTR)Value,
                   0);
}

MMRESULT
WdmAudSetMixerDeviceFormatByMMixer(
    IN  PSOUND_DEVICE_INSTANCE Instance,
    IN  DWORD DeviceId,
    IN  PWAVEFORMATEX WaveFormat,
    IN  DWORD WaveFormatSize)
{
    if (MMixerOpen(&MixerContext, DeviceId, (PVOID)Instance, MixerEventCallback, &Instance->Handle) == MM_STATUS_SUCCESS)
        return MMSYSERR_NOERROR;

    return MMSYSERR_BADDEVICEID;
}

MMRESULT
WdmAudSetWaveStateByMMixer(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance,
    IN BOOL bStart)
{
    MMDEVICE_TYPE DeviceType;
    PSOUND_DEVICE SoundDevice;
    MIXER_STATUS MixerStatus;
    MMRESULT Result;

    DPRINT("WdmAuSetWaveState bStart %x\n", bStart);

    Result = GetSoundDeviceFromInstance(SoundDeviceInstance, &SoundDevice);
    SND_ASSERT( Result == MMSYSERR_NOERROR );

    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    SND_ASSERT( Result == MMSYSERR_NOERROR );

    if (DeviceType == WAVE_IN_DEVICE_TYPE || DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        if (bStart)
        {
            MixerStatus = MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
            if (MixerStatus == MM_STATUS_SUCCESS)
                MixerStatus = MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
            if (MixerStatus == MM_STATUS_SUCCESS)
                MixerStatus = MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_RUN);
            if (MixerStatus != MM_STATUS_SUCCESS)
                return MMSYSERR_ERROR;

            if (SoundDeviceInstance->RTStreamingEnabled)
            {
                if (DeviceType == WAVE_OUT_DEVICE_TYPE)
                {
                    ULONG PacketNumber;

                    if (!SoundDeviceInstance->RTStreamingNotificationCount)
                        return MMSYSERR_ERROR;

                    MemoryBarrier();
                    InterlockedExchange(
                        &SoundDeviceInstance->RTStreamingNextPacketNumber,
                        SoundDeviceInstance->RTStreamingNotificationCount);
                    InterlockedExchange(&SoundDeviceInstance->RTStreamingPacketError, 0);

                    for (PacketNumber = 0;
                         PacketNumber < SoundDeviceInstance->RTStreamingNotificationCount;
                         ++PacketNumber)
                    {
                        MixerStatus = MMixerSetRTStreamingWritePacket(
                            &MixerContext,
                            SoundDeviceInstance->Handle,
                            PacketNumber,
                            0,
                            0);
                        if (MixerStatus != MM_STATUS_SUCCESS)
                        {
                            InterlockedExchange(&SoundDeviceInstance->RTStreamingPacketError, 1);
                            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
                            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
                            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_STOP);
                            return MMSYSERR_ERROR;
                        }
                    }
                }

                if (!WdmAudStartRTStreamingThreads(SoundDeviceInstance))
                {
                    InterlockedExchange(&SoundDeviceInstance->RTStreamingPacketError, 1);
                    MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
                    MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
                    MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_STOP);
                    return MMSYSERR_NOMEM;
                }

                SoundDeviceInstance->bStarted = TRUE;
            }
        }
        else
        {
            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_STOP);
            if (SoundDeviceInstance->RTStreamingEnabled)
                SoundDeviceInstance->bStarted = FALSE;
        }
    }
    else if (DeviceType == MIDI_IN_DEVICE_TYPE || DeviceType == MIDI_OUT_DEVICE_TYPE)
    {
        if (bStart)
        {
            MMixerSetMidiStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
            MMixerSetMidiStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
            MMixerSetMidiStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_RUN);
        }
        else
        {
            MMixerSetMidiStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
            MMixerSetMidiStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
            MMixerSetMidiStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_STOP);
        }
    }

    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudResetStreamByMMixer(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance,
    IN  MMDEVICE_TYPE DeviceType,
    IN  BOOLEAN bStartReset)
{
    MIXER_STATUS Status;

    DPRINT("WdmaudResetStream bStartReset %x\n", bStartReset);
    if (SoundDeviceInstance->RTStreamingEnabled)
    {
        if (bStartReset)
        {
            SoundDeviceInstance->ResetInProgress = TRUE;
            SoundDeviceInstance->bStarted = FALSE;
            WdmAudStopRTStreamingThreads(SoundDeviceInstance);

            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_PAUSE);
            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_ACQUIRE);
            MMixerSetWaveStatus(&MixerContext, SoundDeviceInstance->Handle, KSSTATE_STOP);

            SoundDeviceInstance->RTStreamingBufferOffset = 0;
            SoundDeviceInstance->RTStreamingShadowBufferReadOffset = 0;
            SoundDeviceInstance->RTStreamingShadowBufferWriteOffset = 0;
            InterlockedExchange(&SoundDeviceInstance->RTStreamingBufferBytesWritten, 0);
            InterlockedExchange(&SoundDeviceInstance->RTStreamingShadowBufferBytesUsed, 0);
            InterlockedExchange(&SoundDeviceInstance->RTStreamingNextPacketNumber, 0);
            InterlockedExchange(&SoundDeviceInstance->RTStreamingPacketError, 0);
            InterlockedExchange(&SoundDeviceInstance->RTStreamingUnderrunCount, 0);
            InterlockedExchange(&SoundDeviceInstance->RTStreamingUnderrunBytes, 0);
            RtlZeroMemory(SoundDeviceInstance->RTStreamingBuffer,
                          SoundDeviceInstance->RTStreamingBufferLength);
            RtlZeroMemory(SoundDeviceInstance->RTStreamingShadowBuffer,
                          SoundDeviceInstance->RTStreamingShadowBufferLength);
            MemoryBarrier();
        }
        else
        {
            SoundDeviceInstance->ResetInProgress = FALSE;
        }
        return MMSYSERR_NOERROR;
    }

    if (DeviceType == WAVE_IN_DEVICE_TYPE || DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        Status = MMixerSetWaveResetState(&MixerContext, SoundDeviceInstance->Handle, bStartReset);
        DPRINT("WdmaudResetStream Result %x\n", Status);
        if (Status == MM_STATUS_SUCCESS)
        {
            /* Completed successfully */
            return MMSYSERR_NOERROR;
        }
    }
    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
WdmAudGetWavePositionByMMixer(
    IN  struct _SOUND_DEVICE_INSTANCE* SoundDeviceInstance,
    IN  MMTIME* Time)
{
    PSOUND_DEVICE SoundDevice;
    MMDEVICE_TYPE DeviceType;
    MIXER_STATUS Status;
    MMRESULT Result;
    DWORD Position;

    DPRINT("GetWavePosition\n");

    Result = GetSoundDeviceFromInstance(SoundDeviceInstance, &SoundDevice);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    SND_ASSERT(Result == MMSYSERR_NOERROR);

    if (DeviceType == WAVE_IN_DEVICE_TYPE || DeviceType == WAVE_OUT_DEVICE_TYPE)
    {
        if (SoundDeviceInstance->RTStreamingEnabled)
        {
            /* Store position */
            Time->wType = TIME_BYTES;
            Time->u.cb = SoundDeviceInstance->RTStreamingBufferBytesWritten;

            /* Completed successfully */
            return MMSYSERR_NOERROR;
        }

        Status = MMixerGetWavePosition(&MixerContext, SoundDeviceInstance->Handle, &Position);
        if (Status == MM_STATUS_SUCCESS)
        {
            /* Store position */
            Time->wType = TIME_BYTES;
            Time->u.cb = Position;

            /* Completed successfully */
            return MMSYSERR_NOERROR;
        }
    }
    return MMSYSERR_NOTSUPPORTED;
}

MMRESULT
WdmAudGetVolumeByMMixer(
    _In_ PSOUND_DEVICE_INSTANCE SoundDeviceInstance,
    _In_ DWORD DeviceId,
    _Out_ PDWORD pdwVolume)
{
    MMRESULT Result;
    MIXERLINE MixLine;
    MIXERCONTROL MixControl;
    MIXERLINECONTROLS MixLineControls;
    MIXERCONTROLDETAILS MixControlDetails;
    PMIXERCONTROLDETAILS_UNSIGNED MixControlDetailsU;
    DWORD ChannelCount;

    MixLine.cbStruct = sizeof(MixLine);
    MixLine.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

    /* Get line info */
    Result = WdmAudGetLineInfo(SoundDeviceInstance->Handle,
                               DeviceId,
                               &MixLine,
                               MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_COMPONENTTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    MixLineControls.cbStruct = sizeof(MixLineControls);
    MixLineControls.dwLineID = MixLine.dwLineID;
    MixLineControls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    MixLineControls.cControls = 1;
    MixLineControls.cbmxctrl = sizeof(MixControl);
    MixLineControls.pamxctrl = &MixControl;

    /* Get line controls */
    Result = WdmAudGetLineControls(SoundDeviceInstance->Handle,
                                   DeviceId,
                                   &MixLineControls,
                                   MIXER_OBJECTF_MIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    ChannelCount = (MixControl.fdwControl & MIXERCONTROL_CONTROLF_UNIFORM) ?
                   1 : MixLine.cChannels;
    if (!ChannelCount || ChannelCount > MAXDWORD / sizeof(*MixControlDetailsU))
        return MMSYSERR_ERROR;

    MixControlDetailsU = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                   ChannelCount * sizeof(*MixControlDetailsU));
    if (!MixControlDetailsU)
        return MMSYSERR_NOMEM;

    MixControlDetails.cbStruct = sizeof(MixControlDetails);
    MixControlDetails.dwControlID = MixControl.dwControlID;
    MixControlDetails.cChannels = ChannelCount;
    MixControlDetails.cMultipleItems = 0;
    MixControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
    MixControlDetails.paDetails = MixControlDetailsU;

    /* Get volume control details */
    Result = WdmAudGetControlDetails(SoundDeviceInstance->Handle,
                                     DeviceId,
                                     &MixControlDetails,
                                     MIXER_OBJECTF_MIXER);
    if (MMSUCCESS(Result))
    {
        *pdwVolume = MAKELONG(LOWORD(MixControlDetailsU[0].dwValue),
                              LOWORD(MixControlDetailsU[ChannelCount > 1 ? 1 : 0].dwValue));
    }

    HeapFree(GetProcessHeap(), 0, MixControlDetailsU);

    return Result;
}

MMRESULT
WdmAudSetVolumeByMMixer(
    _In_ PSOUND_DEVICE_INSTANCE SoundDeviceInstance,
    _In_ DWORD DeviceId,
    _In_ DWORD dwVolume)
{
    MMRESULT Result;
    MIXERLINE MixLine;
    MIXERCONTROL MixControl;
    MIXERLINECONTROLS MixLineControls;
    MIXERCONTROLDETAILS MixControlDetails;
    PMIXERCONTROLDETAILS_UNSIGNED MixControlDetailsU;
    DWORD ChannelCount;
    DWORD Channel;

    MixLine.cbStruct = sizeof(MixLine);
    MixLine.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

    /* Get line info */
    Result = WdmAudGetLineInfo(SoundDeviceInstance->Handle,
                               DeviceId,
                               &MixLine,
                               MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_COMPONENTTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    MixLineControls.cbStruct = sizeof(MixLineControls);
    MixLineControls.dwLineID = MixLine.dwLineID;
    MixLineControls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    MixLineControls.cControls = 1;
    MixLineControls.cbmxctrl = sizeof(MixControl);
    MixLineControls.pamxctrl = &MixControl;

    /* Get line controls */
    Result = WdmAudGetLineControls(SoundDeviceInstance->Handle,
                                   DeviceId,
                                   &MixLineControls,
                                   MIXER_OBJECTF_MIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE);
    if (!MMSUCCESS(Result))
        return TranslateInternalMmResult(Result);

    ChannelCount = (MixControl.fdwControl & MIXERCONTROL_CONTROLF_UNIFORM) ?
                   1 : MixLine.cChannels;
    if (!ChannelCount || ChannelCount > MAXDWORD / sizeof(*MixControlDetailsU))
        return MMSYSERR_ERROR;

    MixControlDetailsU = HeapAlloc(GetProcessHeap(), 0,
                                   ChannelCount * sizeof(*MixControlDetailsU));
    if (!MixControlDetailsU)
        return MMSYSERR_NOMEM;

    for (Channel = 0; Channel < ChannelCount; ++Channel)
    {
        MixControlDetailsU[Channel].dwValue =
            Channel ? HIWORD(dwVolume) : LOWORD(dwVolume);
    }

    MixControlDetails.cbStruct = sizeof(MixControlDetails);
    MixControlDetails.dwControlID = MixControl.dwControlID;
    MixControlDetails.cChannels = ChannelCount;
    MixControlDetails.cMultipleItems = 0;
    MixControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
    MixControlDetails.paDetails = MixControlDetailsU;

    /* Set volume control details */
    Result = WdmAudSetControlDetails(SoundDeviceInstance->Handle,
                                     DeviceId,
                                     &MixControlDetails,
                                     MIXER_OBJECTF_MIXER);
    HeapFree(GetProcessHeap(), 0, MixControlDetailsU);
    return Result;
}

static
VOID WINAPI
CommitWaveBufferApc(PVOID ApcContext,
           PIO_STATUS_BLOCK IoStatusBlock,
           ULONG Reserved)
{
    DWORD ErrorCode;
    PSOUND_OVERLAPPED Overlap;
    KSSTREAM_HEADER* lpHeader;

    UNREFERENCED_PARAMETER(ApcContext);
    UNREFERENCED_PARAMETER(Reserved);

    Overlap = (PSOUND_OVERLAPPED)IoStatusBlock;
    lpHeader = Overlap->CompletionContext;
    ErrorCode = RtlNtStatusToDosError(IoStatusBlock->Status);

    /* Call mmebuddy overlap routine */
    CompleteIO(ErrorCode, lpHeader->DataUsed, Overlap);
    HeapFree(GetProcessHeap(), 0, lpHeader);
}

static DWORD
RTStreamingRingWrite(
    PSOUND_DEVICE_INSTANCE SoundDeviceInstance,
    const UCHAR *Source,
    DWORD Length)
{
    ULONG Available;
    ULONG BytesCopied;
    ULONG FirstPart;
    ULONG WriteOffset;
    LONG BytesUsed;

    BytesUsed = InterlockedCompareExchange(
        &SoundDeviceInstance->RTStreamingShadowBufferBytesUsed, 0, 0);
    if (BytesUsed < 0 ||
        (ULONG)BytesUsed > SoundDeviceInstance->RTStreamingShadowBufferLength)
    {
        return 0;
    }

    Available = SoundDeviceInstance->RTStreamingShadowBufferLength - BytesUsed;
    BytesCopied = min(Length, Available);
    if (!BytesCopied)
        return 0;

    WriteOffset = SoundDeviceInstance->RTStreamingShadowBufferWriteOffset;
    FirstPart = min(BytesCopied,
                    SoundDeviceInstance->RTStreamingShadowBufferLength - WriteOffset);
    RtlCopyMemory(&SoundDeviceInstance->RTStreamingShadowBuffer[WriteOffset],
                  Source,
                  FirstPart);
    if (BytesCopied != FirstPart)
    {
        RtlCopyMemory(SoundDeviceInstance->RTStreamingShadowBuffer,
                      Source + FirstPart,
                      BytesCopied - FirstPart);
    }

    SoundDeviceInstance->RTStreamingShadowBufferWriteOffset =
        (WriteOffset + BytesCopied) % SoundDeviceInstance->RTStreamingShadowBufferLength;
    InterlockedExchangeAdd(&SoundDeviceInstance->RTStreamingShadowBufferBytesUsed,
                           BytesCopied);
    return BytesCopied;
}

static DWORD
RTStreamingRingRead(
    PSOUND_DEVICE_INSTANCE SoundDeviceInstance,
    UCHAR *Destination,
    DWORD Length)
{
    ULONG BytesCopied;
    ULONG FirstPart;
    ULONG ReadOffset;
    LONG BytesUsed;

    BytesUsed = InterlockedCompareExchange(
        &SoundDeviceInstance->RTStreamingShadowBufferBytesUsed, 0, 0);
    if (BytesUsed <= 0 ||
        (ULONG)BytesUsed > SoundDeviceInstance->RTStreamingShadowBufferLength)
    {
        return 0;
    }

    BytesCopied = min(Length, (ULONG)BytesUsed);
    ReadOffset = SoundDeviceInstance->RTStreamingShadowBufferReadOffset;
    FirstPart = min(BytesCopied,
                    SoundDeviceInstance->RTStreamingShadowBufferLength - ReadOffset);
    RtlCopyMemory(Destination,
                  &SoundDeviceInstance->RTStreamingShadowBuffer[ReadOffset],
                  FirstPart);
    if (BytesCopied != FirstPart)
    {
        RtlCopyMemory(Destination + FirstPart,
                      SoundDeviceInstance->RTStreamingShadowBuffer,
                      BytesCopied - FirstPart);
    }

    SoundDeviceInstance->RTStreamingShadowBufferReadOffset =
        (ReadOffset + BytesCopied) % SoundDeviceInstance->RTStreamingShadowBufferLength;
    InterlockedExchangeAdd(&SoundDeviceInstance->RTStreamingShadowBufferBytesUsed,
                           -((LONG)BytesCopied));
    return BytesCopied;
}

DWORD
WINAPI
RTStreamingThreadProc(
    LPVOID Parameter)
{
    DWORD WaitStatus;
    PVOID WaitObjects[2];
    PSOUND_DEVICE SoundDevice;
    MMDEVICE_TYPE DeviceType;
    MMRESULT Result;

    PSOUND_DEVICE_INSTANCE SoundDeviceInstance;

    SoundDeviceInstance = (PSOUND_DEVICE_INSTANCE)Parameter;

    Result = GetSoundDeviceFromInstance(SoundDeviceInstance, &SoundDevice);
    if ( ! MMSUCCESS(Result) )
    {
        return TranslateInternalMmResult(Result);
    }
    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    SND_ASSERT( Result == MMSYSERR_NOERROR );

    WaitObjects[0] = (PVOID)SoundDeviceInstance->hNotifyRTStreamingStopEvent;
    WaitObjects[1] = (PVOID)SoundDeviceInstance->hNotifyRTStreamingEvent;

    DPRINT("RTStreamingThreadProc entered %p\n", SoundDeviceInstance);
    SoundDeviceInstance->RTStreamingStarted = TRUE;
    while (SoundDeviceInstance->RTStreamingEnabled)
    {
        WaitStatus = WaitForMultipleObjects(2, WaitObjects, FALSE, INFINITE);
        if (WaitStatus == WAIT_OBJECT_0)
        {
            DPRINT("RTStreamingThreadProc StopEvent\n");
            break;
        }
        else if (WaitStatus == WAIT_OBJECT_0 + 1)
        {
            DWORD Length;
            DWORD BytesCopied;

            if (!SoundDeviceInstance->RTStreamingNotificationCount)
            {
                InterlockedExchange(&SoundDeviceInstance->RTStreamingPacketError, 1);
                break;
            }

            Length = SoundDeviceInstance->RTStreamingBufferLength /
                SoundDeviceInstance->RTStreamingNotificationCount;
            if (DeviceType == WAVE_OUT_DEVICE_TYPE)
            {
                LONG PacketNumber;
                ULONG PacketIndex;
                MIXER_STATUS MixerStatus;
                UCHAR *PeriodBuffer =
                    SoundDeviceInstance->RTStreamingBuffer;

                PacketNumber = InterlockedIncrement(
                    &SoundDeviceInstance->RTStreamingNextPacketNumber) - 1;
                PacketIndex = (ULONG)PacketNumber %
                    SoundDeviceInstance->RTStreamingNotificationCount;
                PeriodBuffer += PacketIndex * Length;

                BytesCopied = RTStreamingRingRead(SoundDeviceInstance,
                                                  PeriodBuffer,
                                                  Length);
                if (BytesCopied < Length)
                {
                    RtlZeroMemory(PeriodBuffer + BytesCopied,
                                  Length - BytesCopied);
                    InterlockedIncrement(&SoundDeviceInstance->RTStreamingUnderrunCount);
                    InterlockedExchangeAdd(&SoundDeviceInstance->RTStreamingUnderrunBytes,
                                           Length - BytesCopied);
                }
                InterlockedExchangeAdd(&SoundDeviceInstance->RTStreamingBufferBytesWritten,
                                       BytesCopied);
                SoundDeviceInstance->RTStreamingBufferOffset =
                    ((PacketIndex + 1) * Length) %
                    SoundDeviceInstance->RTStreamingBufferLength;

                MemoryBarrier();
                MixerStatus = MMixerSetRTStreamingWritePacket(
                    &MixerContext,
                    SoundDeviceInstance->Handle,
                    (ULONG)PacketNumber,
                    0,
                    0);
                if (MixerStatus != MM_STATUS_SUCCESS)
                {
                    InterlockedExchange(&SoundDeviceInstance->RTStreamingPacketError, 1);
                    break;
                }
            }
            else if (DeviceType == WAVE_IN_DEVICE_TYPE)
            {
                RTStreamingRingWrite(
                    SoundDeviceInstance,
                    &SoundDeviceInstance->RTStreamingBuffer[SoundDeviceInstance->RTStreamingBufferOffset],
                    Length);
                SoundDeviceInstance->RTStreamingBufferOffset =
                    (SoundDeviceInstance->RTStreamingBufferOffset + Length) %
                    SoundDeviceInstance->RTStreamingBufferLength;
            }
        }
    }
    RtlZeroMemory(SoundDeviceInstance->RTStreamingBuffer, SoundDeviceInstance->RTStreamingBufferLength);
    DPRINT("Exiting thread\n");
    SoundDeviceInstance->RTStreamingStarted = FALSE;
    return 0;
}

typedef struct
{
    PSOUND_OVERLAPPED Overlap;
    LPSOUND_OVERLAPPED_COMPLETION_ROUTINE CompletionRoutine;
    DWORD Status;
    DWORD BytesTransferred;
} COMPLETION_CONTEXT, *PCOMPLETION_CONTEXT;

DWORD
WINAPI
RTStreamingCompletionThreadProc(
    IN  PVOID Parameter)
{
    DWORD WaitStatus;
    PVOID WaitObjects[2];
    PSOUND_DEVICE_INSTANCE SoundDeviceInstance;
    PCOMPLETION_CONTEXT Context;

    SoundDeviceInstance = (PSOUND_DEVICE_INSTANCE)Parameter;

    WaitObjects[0] = (PVOID)SoundDeviceInstance->hNotifyRTStreamingCompletionStopEvent;
    WaitObjects[1] = (PVOID)SoundDeviceInstance->hNotifyRTStreamingCompletionEvent;

    DPRINT("RTStreamingCompletionThreadProc entered %p\n", SoundDeviceInstance);
    while (SoundDeviceInstance->RTStreamingEnabled)
    {
        SoundDeviceInstance->RTStreamingCompletionStarted = TRUE;
        WaitStatus = WaitForMultipleObjects(2, WaitObjects, FALSE, INFINITE);
        if (WaitStatus == WAIT_OBJECT_0)
        {
            DPRINT("RTStreamingCompletionThreadProc StopEvent\n");
            break;
        }
        else if (WaitStatus == WAIT_OBJECT_0 + 1)
        {
            Context = (PCOMPLETION_CONTEXT)SoundDeviceInstance->RTStreamingCompletionContext;
            ASSERT(Context);
            SoundDeviceInstance->RTStreamingCompletionContext = NULL;
            SetEvent(SoundDeviceInstance->hNotifyRTStreamingCompletionReadyEvent);
            Context->CompletionRoutine(Context->Status, Context->BytesTransferred, Context->Overlap);
            SetEvent(SoundDeviceInstance->hNotifyRTStreamingCompletionFinishEvent);
            FreeMemory(Context);
        }
    }
    DPRINT("Exiting completion thread\n");
    SoundDeviceInstance->RTStreamingCompletionStarted = FALSE;
    return MMSYSERR_NOERROR;
}

MMRESULT
WdmAudCommitWaveBufferByMMixer(
    IN  PSOUND_DEVICE_INSTANCE SoundDeviceInstance,
    IN  PVOID OffsetPtr,
    IN  DWORD Length,
    IN  PSOUND_OVERLAPPED Overlap,
    IN  LPSOUND_OVERLAPPED_COMPLETION_ROUTINE CompletionRoutine)
{
    PSOUND_DEVICE SoundDevice;
    MMDEVICE_TYPE DeviceType;
    MMRESULT Result;
    ULONG IoCtl;
    KSSTREAM_HEADER* lpHeader;
    NTSTATUS Status;
    PCOMPLETION_CONTEXT Context;

    Result = GetSoundDeviceFromInstance(SoundDeviceInstance, &SoundDevice);

    if ( ! MMSUCCESS(Result) )
    {
        return TranslateInternalMmResult(Result);
    }
    Result = GetSoundDeviceType(SoundDevice, &DeviceType);
    SND_ASSERT( Result == MMSYSERR_NOERROR );

    if (SoundDeviceInstance->RTStreamingEnabled)
    {
        Context = AllocateMemory(sizeof(*Context));
        if (!Context)
            return MMSYSERR_NOMEM;

        Status = STATUS_SUCCESS;
        DWORD Offset = 0;

        if (DeviceType == WAVE_OUT_DEVICE_TYPE &&
            !SoundDeviceInstance->bStarted)
        {
            DWORD PrefillLength = min(Length,
                                      SoundDeviceInstance->RTStreamingBufferLength);

            RtlCopyMemory(SoundDeviceInstance->RTStreamingBuffer,
                          OffsetPtr,
                          PrefillLength);
            if (PrefillLength < SoundDeviceInstance->RTStreamingBufferLength)
            {
                RtlZeroMemory(SoundDeviceInstance->RTStreamingBuffer + PrefillLength,
                              SoundDeviceInstance->RTStreamingBufferLength - PrefillLength);
            }

            SoundDeviceInstance->RTStreamingBufferOffset = 0;
            InterlockedExchangeAdd(&SoundDeviceInstance->RTStreamingBufferBytesWritten,
                                   PrefillLength);
            Offset = PrefillLength;
            MemoryBarrier();

            Result = WdmAudSetWaveStateByMMixer(SoundDeviceInstance, TRUE);
            if (Result != MMSYSERR_NOERROR)
                Status = STATUS_UNSUCCESSFUL;
        }

        while(NT_SUCCESS(Status) && Offset < Length)
        {
            if (SoundDeviceInstance->ResetInProgress ||
                !SoundDeviceInstance->RTStreamingEnabled ||
                SoundDeviceInstance->bClosed ||
                InterlockedCompareExchange(
                    &SoundDeviceInstance->RTStreamingPacketError, 0, 0))
            {
                Status = STATUS_CANCELLED;
                break;
            }
            if (DeviceType == WAVE_OUT_DEVICE_TYPE)
            {
                DWORD BytesCopied = RTStreamingRingWrite(
                    SoundDeviceInstance,
                    &((PUCHAR)OffsetPtr)[Offset],
                    Length - Offset);
                Offset += BytesCopied;
                if (Offset < Length)
                {
                    DPRINT("Waiting...\n");
                    Sleep(1);
                }
            }
            else if (DeviceType == WAVE_IN_DEVICE_TYPE)
            {
                DWORD BytesCopied;

                while(InterlockedCompareExchange(
                          &SoundDeviceInstance->RTStreamingShadowBufferBytesUsed,
                          0,
                          0) == 0)
                {
                    DPRINT("Waiting...\n");
                    Sleep(1);
                }

                BytesCopied = RTStreamingRingRead(
                    SoundDeviceInstance,
                    &((PUCHAR)OffsetPtr)[Offset],
                    Length - Offset);
                Offset += BytesCopied;
            }
        }
        Context->BytesTransferred = Offset;
        Context->Status = Status;
        Context->CompletionRoutine = CompletionRoutine;
        Context->Overlap = Overlap;
        WaitForSingleObject(SoundDeviceInstance->hNotifyRTStreamingCompletionReadyEvent, INFINITE);
        SoundDeviceInstance->RTStreamingCompletionContext = Context;
        SetEvent(SoundDeviceInstance->hNotifyRTStreamingCompletionEvent);
        WaitForSingleObject(SoundDeviceInstance->hNotifyRTStreamingCompletionFinishEvent, INFINITE);
        return MMSYSERR_NOERROR;
    }
    else if (SoundDeviceInstance->LegacyStreaming)
    {
        lpHeader = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(KSSTREAM_HEADER));
        if (!lpHeader)
        {
            /* No memory */
            return MMSYSERR_NOMEM;
        }

        /* Setup stream packet */
        lpHeader->Size = sizeof(KSSTREAM_HEADER);
        lpHeader->PresentationTime.Numerator = 1;
        lpHeader->PresentationTime.Denominator = 1;
        lpHeader->Data = OffsetPtr;
        lpHeader->FrameExtent = Length;
        Overlap->CompletionContext = lpHeader;
        Overlap->OriginalCompletionRoutine = CompletionRoutine;
        IoCtl = (DeviceType == WAVE_OUT_DEVICE_TYPE ? IOCTL_KS_WRITE_STREAM : IOCTL_KS_READ_STREAM);

        if (DeviceType == WAVE_OUT_DEVICE_TYPE)
        {
            lpHeader->DataUsed = Length;
        }
        Status = NtDeviceIoControlFile(
            SoundDeviceInstance->Handle, NULL, CommitWaveBufferApc, NULL, (PIO_STATUS_BLOCK)Overlap, IoCtl, NULL, 0,
            lpHeader, sizeof(KSSTREAM_HEADER));

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("NtDeviceIoControlFile() failed with status %08lx\n", Status);
            Overlap->CompletionContext = NULL;
            Overlap->OriginalCompletionRoutine = NULL;
            HeapFree(GetProcessHeap(), 0, lpHeader);
            return MMSYSERR_ERROR;
        }

        return MMSYSERR_NOERROR;
    }
    else
    {
        ASSERT(FALSE);
    }
    return MMSYSERR_NOTSUPPORTED;
}
