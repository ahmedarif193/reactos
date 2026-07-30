/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI HTTP download and ramdisk integration
 */

#include <uefildr.h>
#include <Http.h>
#include <LoadedImage.h>
#include <ServiceBinding.h>

#include "uefinetp.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define HTTP_URL_MAX_CHARS      1024
#define HTTP_HOST_MAX_CHARS     256
#define HTTP_BODY_CHUNK_SIZE    (4 * 1024 * 1024)
#define HTTP_LOG_INTERVAL_SECONDS 5
#define HTTP_OPERATION_TIMEOUT  (30ULL * 10000000ULL)
#define HTTP_SECONDS_PER_DAY    (24 * 60 * 60)
typedef struct _UEFI_HTTP_SESSION
{
    EFI_SERVICE_BINDING_PROTOCOL *Binding;
    EFI_HANDLE ChildHandle;
    EFI_HTTP_PROTOCOL *Protocol;
    EFI_EVENT RequestEvent;
    EFI_EVENT ResponseEvent;
    EFI_EVENT TimerEvent;
    volatile BOOLEAN RequestDone;
    volatile BOOLEAN ResponseDone;
    EFI_HTTP_MESSAGE RequestMessage;
    EFI_HTTP_MESSAGE ResponseMessage;
    EFI_HTTP_TOKEN RequestToken;
    EFI_HTTP_TOKEN ResponseToken;
} UEFI_HTTP_SESSION;

static EFI_GUID EfiHttpServiceBindingGuid = EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_GUID EfiHttpGuid = EFI_HTTP_PROTOCOL_GUID;
static EFI_GUID EfiLoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID EfiTcp4ServiceBindingGuid =
    {0x00720665, 0x67eb, 0x4a99,
     {0xba, 0xf7, 0xd3, 0xc3, 0x3a, 0x1c, 0x7c, 0xc9}};

static EFI_HANDLE
UefiHttpFindFunctionOwner(
    _In_ const VOID *Address)
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles = NULL;
    EFI_HANDLE Owner = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
    UINTN AddressValue = (UINTN)Address;
    UINTN BaseValue;
    UINTN HandleCount = 0;
    UINTN Index;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol,
        &EfiLoadedImageGuid,
        NULL,
        &HandleCount,
        &Handles);
    if (EFI_ERROR(Status))
        return NULL;

    for (Index = 0; Index < HandleCount; Index++)
    {
        LoadedImage = NULL;
        Status = GlobalSystemTable->BootServices->HandleProtocol(
            Handles[Index],
            &EfiLoadedImageGuid,
            (VOID **)&LoadedImage);
        if (EFI_ERROR(Status) || !LoadedImage)
            continue;

        BaseValue = (UINTN)LoadedImage->ImageBase;
        if (AddressValue >= BaseValue &&
            (UINT64)(AddressValue - BaseValue) < LoadedImage->ImageSize)
        {
            Owner = Handles[Index];
            break;
        }
    }

    GlobalSystemTable->BootServices->FreePool(Handles);
    return Owner;
}

static ULONG
UefiHttpGetSecondsOfDay(VOID)
{
    TIMEINFO *TimeInfo = ArcGetTime();

    return ((TimeInfo->Hour * 60) + TimeInfo->Minute) * 60 +
           TimeInfo->Second;
}

static ULONG
UefiHttpElapsedSeconds(
    _In_ ULONG StartSeconds,
    _In_ ULONG EndSeconds)
{
    if (EndSeconds >= StartSeconds)
        return EndSeconds - StartSeconds;

    return HTTP_SECONDS_PER_DAY - StartSeconds + EndSeconds;
}

static EFI_HANDLE
UefiHttpProbeProviderImage(
    _In_ EFI_HANDLE Controller)
{
    EFI_STATUS Status;
    EFI_SERVICE_BINDING_PROTOCOL *Binding = NULL;
    EFI_HANDLE ChildHandle = NULL;
    EFI_HANDLE Owner = NULL;
    EFI_HTTP_PROTOCOL *Protocol = NULL;

    Status = UefiNetGetProtocol(
        Controller, &EfiHttpServiceBindingGuid, (VOID **)&Binding, NULL);
    if (EFI_ERROR(Status) || !Binding)
    {
        return NULL;
    }

    Status = Binding->CreateChild(Binding, &ChildHandle);
    if (EFI_ERROR(Status) || !ChildHandle)
    {
        return NULL;
    }

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        ChildHandle, &EfiHttpGuid, (VOID **)&Protocol);
    if (!EFI_ERROR(Status) && Protocol)
    {
        Owner = UefiHttpFindFunctionOwner(
            (const VOID *)(UINTN)Protocol->Request);
    }

    Binding->DestroyChild(Binding, ChildHandle);
    return Owner;
}

/*
 * The board firmware ships its own HttpDxe built with
 * PcdAllowHttpConnections=FALSE: its EFI_HTTP_PROTOCOL.Request() rejects any
 * plain http:// URL with EFI_ACCESS_DENIED before touching the wire. When the
 * firmware copy wins the driver-binding race for the NIC, HTTP boot can never
 * work, so rebind the controller to the permissive HttpDxe loaded from the
 * boot volume.
 */
static VOID
UefiHttpEnsureOwnProvider(
    _In_ PUEFI_NET_CONTEXT Context)
{
    EFI_STATUS Status;
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *OpenInfo = NULL;
    EFI_HANDLE OurImage;
    EFI_HANDLE Provider;
    EFI_HANDLE DriverList[2];
    UINTN OpenInfoCount = 0;
    UINTN Index;

    OurImage = UefiNetGetHttpDriverImage();
    Provider = UefiHttpProbeProviderImage(Context->ControllerHandle);
    if (!OurImage || !Provider || Provider == OurImage)
        return;

    TRACE("UEFI HttpBoot: foreign HTTP driver %p owns the NIC; rebinding to %p\n",
          Provider,
          OurImage);

    /*
     * The foreign HTTP driver holds the TCP4 service binding BY_DRIVER on
     * this controller. Ask the handle database which agents actually hold
     * it and disconnect those, rather than guessing the agent from the
     * image's driver-binding handles (firmware cores record agents that do
     * not match that guess).
     */
    Status = GlobalSystemTable->BootServices->OpenProtocolInformation(
        Context->ControllerHandle,
        &EfiTcp4ServiceBindingGuid,
        &OpenInfo,
        &OpenInfoCount);
    if (EFI_ERROR(Status) || !OpenInfo)
        return;

    for (Index = 0; Index < OpenInfoCount; Index++)
    {
        if (!(OpenInfo[Index].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER))
            continue;

        GlobalSystemTable->BootServices->DisconnectController(
            Context->ControllerHandle, OpenInfo[Index].AgentHandle, NULL);
    }
    GlobalSystemTable->BootServices->FreePool(OpenInfo);

    /*
     * Reconnect with our HttpDxe image first in the priority list. If it
     * cannot start, ConnectController falls back to the remaining drivers,
     * restoring the previous (firmware) provider.
     */
    DriverList[0] = OurImage;
    DriverList[1] = NULL;
    GlobalSystemTable->BootServices->ConnectController(
        Context->ControllerHandle, DriverList, NULL, FALSE);

    Provider = UefiHttpProbeProviderImage(Context->ControllerHandle);
    TRACE("UEFI HttpBoot: HTTP provider after rebind %p (%s)\n",
          Provider,
          (Provider == OurImage) ? "own driver" : "still foreign");
}

static VOID EFIAPI
UefiHttpNotify(
    _In_ EFI_EVENT Event,
    _In_ VOID *Context)
{
    volatile BOOLEAN *Done = Context;

    *Done = TRUE;
}

static VOID
UefiHttpFreeHeaders(
    _Inout_ EFI_HTTP_MESSAGE *Message)
{
    UINTN Index;

    if (!Message->Headers)
        return;

    for (Index = 0; Index < Message->HeaderCount; Index++)
    {
        if (Message->Headers[Index].FieldName)
        {
            GlobalSystemTable->BootServices->FreePool(
                Message->Headers[Index].FieldName);
        }
        if (Message->Headers[Index].FieldValue)
        {
            GlobalSystemTable->BootServices->FreePool(
                Message->Headers[Index].FieldValue);
        }
    }

    GlobalSystemTable->BootServices->FreePool(Message->Headers);
    Message->Headers = NULL;
    Message->HeaderCount = 0;
}

static VOID
UefiHttpClose(
    _Inout_ UEFI_HTTP_SESSION *Session)
{
    if (Session->Protocol)
    {
        Session->Protocol->Cancel(Session->Protocol, NULL);
        Session->Protocol->Configure(Session->Protocol, NULL);
    }

    UefiHttpFreeHeaders(&Session->ResponseMessage);

    if (Session->RequestEvent)
    {
        GlobalSystemTable->BootServices->CloseEvent(
            Session->RequestEvent);
    }
    if (Session->ResponseEvent)
    {
        GlobalSystemTable->BootServices->CloseEvent(
            Session->ResponseEvent);
    }
    if (Session->TimerEvent)
    {
        GlobalSystemTable->BootServices->CloseEvent(
            Session->TimerEvent);
    }

    if (Session->Binding && Session->ChildHandle)
    {
        Session->Binding->DestroyChild(
            Session->Binding, Session->ChildHandle);
    }

    RtlZeroMemory(Session, sizeof(*Session));
}

static BOOLEAN
UefiHttpOpen(
    _In_ PUEFI_NET_CONTEXT Context,
    _Out_ UEFI_HTTP_SESSION *Session)
{
    EFI_STATUS Status;
    EFI_HTTP_CONFIG_DATA Config;
    EFI_HTTPv4_ACCESS_POINT AccessPoint;

    RtlZeroMemory(Session, sizeof(*Session));
    RtlZeroMemory(&Config, sizeof(Config));
    RtlZeroMemory(&AccessPoint, sizeof(AccessPoint));

    Status = UefiNetGetProtocol(
        Context->ControllerHandle,
        &EfiHttpServiceBindingGuid,
        (VOID **)&Session->Binding,
        NULL);
    if (EFI_ERROR(Status) || !Session->Binding)
        goto Failure;

    Status = Session->Binding->CreateChild(
        Session->Binding, &Session->ChildHandle);
    if (EFI_ERROR(Status) || !Session->ChildHandle)
        goto Failure;

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Session->ChildHandle,
        &EfiHttpGuid,
        (VOID **)&Session->Protocol);
    if (EFI_ERROR(Status) || !Session->Protocol)
        goto Failure;

    Config.HttpVersion = HttpVersion11;
    Config.TimeOutMillisec = 30000;
    Config.LocalAddressIsIPv6 = FALSE;
    Config.AccessPoint.IPv4Node = &AccessPoint;
    AccessPoint.UseDefaultAddress = FALSE;
    RtlCopyMemory(
        &AccessPoint.LocalAddress,
        &Context->LocalAddress,
        sizeof(AccessPoint.LocalAddress));
    RtlCopyMemory(
        &AccessPoint.LocalSubnet,
        &Context->SubnetMask,
        sizeof(AccessPoint.LocalSubnet));

    Status = Session->Protocol->Configure(Session->Protocol, &Config);
    if (EFI_ERROR(Status))
        goto Failure;

    Status = GlobalSystemTable->BootServices->CreateEvent(
        EVT_NOTIFY_SIGNAL,
        TPL_CALLBACK,
        UefiHttpNotify,
        (VOID *)&Session->RequestDone,
        &Session->RequestEvent);
    if (EFI_ERROR(Status))
        goto Failure;

    Status = GlobalSystemTable->BootServices->CreateEvent(
        EVT_NOTIFY_SIGNAL,
        TPL_CALLBACK,
        UefiHttpNotify,
        (VOID *)&Session->ResponseDone,
        &Session->ResponseEvent);
    if (EFI_ERROR(Status))
        goto Failure;

    Status = GlobalSystemTable->BootServices->CreateEvent(
        EVT_TIMER,
        TPL_CALLBACK,
        NULL,
        NULL,
        &Session->TimerEvent);
    if (EFI_ERROR(Status))
        goto Failure;

    Session->RequestToken.Event = Session->RequestEvent;
    Session->RequestToken.Message = &Session->RequestMessage;
    Session->ResponseToken.Event = Session->ResponseEvent;
    Session->ResponseToken.Message = &Session->ResponseMessage;
    return TRUE;

Failure:
    UefiHttpClose(Session);
    return FALSE;
}

static EFI_STATUS
UefiHttpWait(
    _In_ UEFI_HTTP_SESSION *Session,
    _Inout_ EFI_HTTP_TOKEN *Token,
    _Inout_ volatile BOOLEAN *Done)
{
    EFI_STATUS Status;

    Status = GlobalSystemTable->BootServices->SetTimer(
        Session->TimerEvent,
        TimerRelative,
        HTTP_OPERATION_TIMEOUT);
    if (EFI_ERROR(Status))
        return Status;

    while (!*Done)
    {
        Status = GlobalSystemTable->BootServices->CheckEvent(
            Session->TimerEvent);
        if (Status == EFI_SUCCESS)
        {
            Session->Protocol->Cancel(Session->Protocol, Token);
            GlobalSystemTable->BootServices->SetTimer(
                Session->TimerEvent, TimerCancel, 0);
            return EFI_TIMEOUT;
        }
        if (Status != EFI_NOT_READY)
        {
            Session->Protocol->Cancel(Session->Protocol, Token);
            GlobalSystemTable->BootServices->SetTimer(
                Session->TimerEvent, TimerCancel, 0);
            return Status;
        }

        Status = Session->Protocol->Poll(Session->Protocol);
        if (EFI_ERROR(Status) && Status != EFI_NOT_READY)
        {
            Session->Protocol->Cancel(Session->Protocol, Token);
            GlobalSystemTable->BootServices->SetTimer(
                Session->TimerEvent, TimerCancel, 0);
            return Status;
        }
    }

    GlobalSystemTable->BootServices->SetTimer(
        Session->TimerEvent, TimerCancel, 0);
    return Token->Status;
}

static EFI_STATUS
UefiHttpSendRequest(
    _Inout_ UEFI_HTTP_SESSION *Session,
    _In_ CHAR16 *Url,
    _In_ CHAR8 *Host)
{
    EFI_STATUS Status;
    EFI_HTTP_REQUEST_DATA Request;
    EFI_HTTP_HEADER Headers[2];

    RtlZeroMemory(&Request, sizeof(Request));
    RtlZeroMemory(Headers, sizeof(Headers));
    RtlZeroMemory(&Session->RequestMessage, sizeof(Session->RequestMessage));

    Request.Method = HttpMethodGet;
    Request.Url = Url;
    Headers[0].FieldName = (CHAR8 *)"Host";
    Headers[0].FieldValue = Host;
    Headers[1].FieldName = (CHAR8 *)"Connection";
    Headers[1].FieldValue = (CHAR8 *)"close";

    Session->RequestMessage.Data.Request = &Request;
    Session->RequestMessage.HeaderCount = RTL_NUMBER_OF(Headers);
    Session->RequestMessage.Headers = Headers;
    Session->RequestDone = FALSE;
    Session->RequestToken.Status = EFI_NOT_READY;

    Status = Session->Protocol->Request(
        Session->Protocol, &Session->RequestToken);
    if (EFI_ERROR(Status))
        return Status;

    Status = UefiHttpWait(
        Session, &Session->RequestToken, &Session->RequestDone);
    return Status;
}

static EFI_STATUS
UefiHttpReceive(
    _Inout_ UEFI_HTTP_SESSION *Session,
    _In_opt_ EFI_HTTP_RESPONSE_DATA *Response,
    _Out_writes_bytes_opt_(BufferSize) VOID *Buffer,
    _In_ UINTN BufferSize,
    _Out_ UINTN *BytesReceived)
{
    EFI_STATUS Status;

    *BytesReceived = 0;
    UefiHttpFreeHeaders(&Session->ResponseMessage);
    RtlZeroMemory(&Session->ResponseMessage, sizeof(Session->ResponseMessage));

    Session->ResponseMessage.Data.Response = Response;
    Session->ResponseMessage.Body = Buffer;
    Session->ResponseMessage.BodyLength = BufferSize;
    Session->ResponseDone = FALSE;
    Session->ResponseToken.Status = EFI_NOT_READY;

    Status = Session->Protocol->Response(
        Session->Protocol, &Session->ResponseToken);
    if (EFI_ERROR(Status))
        return Status;

    Status = UefiHttpWait(
        Session, &Session->ResponseToken, &Session->ResponseDone);
    *BytesReceived = Session->ResponseMessage.BodyLength;
    return Status;
}

static BOOLEAN
UefiParseDecimalSize(
    _In_ const CHAR8 *Text,
    _Out_ UINTN *Value)
{
    UINTN Result = 0;
    BOOLEAN HaveDigit = FALSE;

    while (*Text == ' ' || *Text == '\t')
        Text++;

    while (*Text >= '0' && *Text <= '9')
    {
        UINTN Digit = (UINTN)(*Text - '0');

        if (Result > (((UINTN)-1) - Digit) / 10)
            return FALSE;
        Result = Result * 10 + Digit;
        HaveDigit = TRUE;
        Text++;
    }

    while (*Text == ' ' || *Text == '\t')
        Text++;

    if (!HaveDigit || *Text != '\0')
        return FALSE;

    *Value = Result;
    return TRUE;
}

static BOOLEAN
UefiHttpGetContentLength(
    _In_ EFI_HTTP_MESSAGE *Message,
    _Out_ UINTN *ContentLength)
{
    UINTN Index;

    for (Index = 0; Index < Message->HeaderCount; Index++)
    {
        EFI_HTTP_HEADER *Header = &Message->Headers[Index];

        if (Header->FieldName &&
            Header->FieldValue &&
            _stricmp((PCSTR)Header->FieldName, "Content-Length") == 0)
        {
            return UefiParseDecimalSize(
                Header->FieldValue, ContentLength);
        }
    }

    return FALSE;
}

static BOOLEAN
UefiConvertUrl(
    _In_ PCSTR Url,
    _Out_writes_(HTTP_URL_MAX_CHARS) CHAR16 *WideUrl,
    _Out_writes_(HTTP_HOST_MAX_CHARS) CHAR8 *Host)
{
    PCSTR Authority;
    PCSTR End;
    UINTN UrlLength;
    UINTN HostLength;
    UINTN Index;

    if (_strnicmp(Url, "http://", 7) != 0)
    {
        return FALSE;
    }

    UrlLength = strlen(Url);
    if (UrlLength == 0 || UrlLength >= HTTP_URL_MAX_CHARS)
    {
        return FALSE;
    }

    Authority = Url + 7;
    End = Authority;
    while (*End && *End != '/' && *End != '?' && *End != '#')
        End++;

    HostLength = End - Authority;
    if (HostLength == 0 || HostLength >= HTTP_HOST_MAX_CHARS)
    {
        return FALSE;
    }

    RtlCopyMemory(Host, Authority, HostLength);
    Host[HostLength] = '\0';

    for (Index = 0; Index <= UrlLength; Index++)
        WideUrl[Index] = (CHAR16)(UINT8)Url[Index];

    return TRUE;
}

static BOOLEAN
UefiHttpReadBody(
    _Inout_ UEFI_HTTP_SESSION *Session,
    _Out_writes_bytes_(ContentLength) VOID *Buffer,
    _In_ UINTN ContentLength)
{
    EFI_STATUS Status;
    UINT8 *Destination = Buffer;
    UINTN TotalReceived = 0;
    UINTN LastSpeedBytes = 0;
    ULONG StartSeconds = UefiHttpGetSecondsOfDay();
    ULONG LastSpeedSeconds = StartSeconds;
    ULONG LastLogSeconds = StartSeconds;
    ULONG CurrentSpeedTenths = 0;
    ULONG AverageSpeedTenths = 0;
    ULONG LastPercent = 0;
    CHAR ProgressText[64];

    UiUpdateProgressBar(0, "ISO download 0% | measuring speed...");

    while (TotalReceived < ContentLength)
    {
        UINTN Remaining = ContentLength - TotalReceived;
        UINTN Requested =
            (Remaining > HTTP_BODY_CHUNK_SIZE) ?
                HTTP_BODY_CHUNK_SIZE : Remaining;
        UINTN Received = 0;

        Status = UefiHttpReceive(
            Session,
            NULL,
            Destination + TotalReceived,
            Requested,
            &Received);
        if (EFI_ERROR(Status) || Received == 0 || Received > Requested)
        {
            TRACE("UEFI HttpBoot: body receive failed at %lu/%lu (Status %llx)\n",
                  (unsigned long)TotalReceived,
                  (unsigned long)ContentLength,
                  (unsigned long long)Status);
            return FALSE;
        }

        /*
         * Tokens complete from data TCP already buffered, so the wait loop
         * never polls. Without this pump only MnpDxe's 10ms background
         * timer drains the NIC ring, which overflows in under a
         * millisecond at line rate and throttles the sender to a few
         * MiB/s. Poll until the receive path runs dry (EFI_SUCCESS means
         * frames were processed) so the ring never overflows and TCP's
         * 2 MiB buffer fills between Response calls; the cap bounds the
         * time spent when the sender keeps the ring busy.
         */
        {
            UINTN PumpRound;

            for (PumpRound = 0; PumpRound < 64; PumpRound++)
            {
                if (Session->Protocol->Poll(Session->Protocol) != EFI_SUCCESS)
                    break;
            }
        }

        TotalReceived += Received;
        {
            ULONG Percent = (ULONG)(((ULONGLONG)TotalReceived * 100ULL) /
                                    (ULONGLONG)ContentLength);
            ULONG NowSeconds = UefiHttpGetSecondsOfDay();
            BOOLEAN UpdateSpeed =
                (NowSeconds != LastSpeedSeconds) ||
                (TotalReceived == ContentLength);

            if (UpdateSpeed)
            {
                ULONG SampleSeconds =
                    UefiHttpElapsedSeconds(LastSpeedSeconds, NowSeconds);
                ULONG TotalSeconds =
                    UefiHttpElapsedSeconds(StartSeconds, NowSeconds);

                if (SampleSeconds != 0)
                {
                    CurrentSpeedTenths =
                        (ULONG)(((ULONGLONG)(TotalReceived - LastSpeedBytes) *
                                 10ULL) /
                                ((1024ULL * 1024ULL) * SampleSeconds));
                    LastSpeedBytes = TotalReceived;
                    LastSpeedSeconds = NowSeconds;
                }

                if (TotalSeconds != 0)
                {
                    AverageSpeedTenths =
                        (ULONG)(((ULONGLONG)TotalReceived * 10ULL) /
                                ((1024ULL * 1024ULL) * TotalSeconds));
                }

                if (CurrentSpeedTenths != 0 || AverageSpeedTenths != 0)
                {
                    RtlStringCbPrintfA(
                        ProgressText,
                        sizeof(ProgressText),
                        "ISO download %lu%% | %lu.%lu MiB/s | avg %lu.%lu",
                        Percent,
                        CurrentSpeedTenths / 10,
                        CurrentSpeedTenths % 10,
                        AverageSpeedTenths / 10,
                        AverageSpeedTenths % 10);
                }
                else
                {
                    RtlStringCbPrintfA(
                        ProgressText,
                        sizeof(ProgressText),
                        "ISO download %lu%% | measuring speed...",
                        Percent);
                }

                UiUpdateProgressBar(Percent, ProgressText);
                LastPercent = Percent;
            }
            else if (Percent != LastPercent)
            {
                UiUpdateProgressBar(Percent, NULL);
                LastPercent = Percent;
            }

            if (UefiHttpElapsedSeconds(LastLogSeconds, NowSeconds) >=
                    HTTP_LOG_INTERVAL_SECONDS ||
                TotalReceived == ContentLength)
            {
                TRACE("UEFI HttpBoot: %lu/%lu MiB (%lu%%, %lu.%lu MiB/s)\n",
                      (unsigned long)(TotalReceived / (1024 * 1024)),
                      (unsigned long)(ContentLength / (1024 * 1024)),
                      Percent,
                      CurrentSpeedTenths / 10,
                      CurrentSpeedTenths % 10);
                LastLogSeconds = NowSeconds;
            }
        }
    }

    return TRUE;
}

BOOLEAN
UefiHttpBootDownload(
    _In_ PCSTR Url)
{
    EFI_STATUS Status;
    UEFI_NET_CONTEXT Context;
    UEFI_HTTP_SESSION Session;
    EFI_HTTP_RESPONSE_DATA Response;
    CHAR16 WideUrl[HTTP_URL_MAX_CHARS];
    CHAR8 Host[HTTP_HOST_MAX_CHARS];
    UINTN IgnoredBodyLength;
    UINTN ContentLength;
    PVOID RamDisk;
    BOOLEAN LeaseAcquired = FALSE;
    BOOLEAN ForcedRebind = FALSE;

    if (!Url ||
        !UefiConvertUrl(Url, WideUrl, Host))
    {
        TRACE("UEFI HttpBoot: invalid or unsupported URL '%s'\n",
              Url ? Url : "(null)");
        return FALSE;
    }

    TRACE("UEFI HttpBoot: starting download from %s\n", Url);
    if (!UefiNetPrepare(&Context))
    {
        return FALSE;
    }

    UefiHttpEnsureOwnProvider(&Context);

    for (;;)
    {
        BOOLEAN MediaPresent;

        if (!LeaseAcquired)
        {
            /*
             * Do not gate DHCP on SNP's MediaPresent bit. Some UNDI drivers
             * report it stale until EDK2's DHCP media check reinitializes SNP.
             * Retrying DHCP here waits indefinitely for both link and a lease.
             */
            if (!UefiDhcpAcquire(&Context))
            {
                GlobalSystemTable->BootServices->Stall(
                    UEFI_NETWORK_RETRY_DELAY_US);
                continue;
            }
            LeaseAcquired = TRUE;
        }

        if (!UefiHttpOpen(&Context, &Session))
        {
            GlobalSystemTable->BootServices->Stall(
                UEFI_NETWORK_RETRY_DELAY_US);
            continue;
        }

        Status = UefiHttpSendRequest(&Session, WideUrl, Host);
        if (EFI_ERROR(Status))
        {
            UefiHttpClose(&Session);
            if (Status == EFI_ACCESS_DENIED && !ForcedRebind)
            {
                /*
                 * A plain-HTTP request denied up front means a restrictive
                 * firmware HttpDxe still serves this controller despite the
                 * bind-priority and eviction passes. Rebuild the NIC stack
                 * from scratch with our driver prioritized, once.
                 */
                ForcedRebind = TRUE;
                TRACE("UEFI HttpBoot: request denied; forcing NIC stack rebind\n");
                if (UefiNetForceRebindHttp(&Context))
                {
                    UefiHttpEnsureOwnProvider(&Context);
                    LeaseAcquired = FALSE;
                }
            }
            MediaPresent = UefiNetMediaPresent(&Context);
            if (!MediaPresent)
                LeaseAcquired = FALSE;
            GlobalSystemTable->BootServices->Stall(
                UEFI_NETWORK_RETRY_DELAY_US);
            continue;
        }

        RtlZeroMemory(&Response, sizeof(Response));
        IgnoredBodyLength = 0;
        Status = UefiHttpReceive(
            &Session,
            &Response,
            NULL,
            0,
            &IgnoredBodyLength);
        if ((Status != EFI_SUCCESS && Status != EFI_HTTP_ERROR) ||
            Response.StatusCode != HTTP_STATUS_200_OK ||
            !UefiHttpGetContentLength(
                &Session.ResponseMessage, &ContentLength) ||
            ContentLength == 0)
        {
            TRACE("UEFI HttpBoot: ISO unavailable (Status %llx, HTTP %u); retrying\n",
                  (unsigned long long)Status,
                  (unsigned)Response.StatusCode);
            UefiHttpClose(&Session);
            MediaPresent = UefiNetMediaPresent(&Context);
            if (!MediaPresent)
                LeaseAcquired = FALSE;
            GlobalSystemTable->BootServices->Stall(
                UEFI_NETWORK_RETRY_DELAY_US);
            continue;
        }

        if (ContentLength > MAXULONG)
        {
            TRACE("UEFI HttpBoot: image is too large for the ramdisk interface\n");
            UefiHttpClose(&Session);
            return FALSE;
        }

        TRACE("UEFI HttpBoot: ISO size=%lu bytes (%lu MiB)\n",
              (unsigned long)ContentLength,
              (unsigned long)(ContentLength / (1024 * 1024)));

        RamDisk = MmAllocateMemoryWithType(
            (SIZE_T)ContentLength, LoaderXIPRom);
        if (!RamDisk)
        {
            TRACE("UEFI HttpBoot: cannot allocate %lu-byte ramdisk\n",
                  (unsigned long)ContentLength);
            UefiHttpClose(&Session);
            return FALSE;
        }

        if (!UefiHttpReadBody(
                &Session, RamDisk, ContentLength))
        {
            /*
             * FreeLoader has no matching free operation for LoaderXIPRom.
             * Do not retry into a second allocation after a partial body.
             */
            UefiHttpClose(&Session);
            return FALSE;
        }

        UefiHttpClose(&Session);
        gInitRamDiskBase = RamDisk;
        gInitRamDiskSize = (ULONG)ContentLength;

        TRACE("UEFI HttpBoot: ramdisk ready at %p, size=%lu\n",
              gInitRamDiskBase,
              (unsigned long)gInitRamDiskSize);
        return TRUE;
    }
}
