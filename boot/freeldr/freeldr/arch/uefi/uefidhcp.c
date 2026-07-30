/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DHCPv4 configuration for UEFI network boot
 */

#include <uefildr.h>
#include <Dhcp4.h>
#include <Ip4Config2.h>
#include <ServiceBinding.h>

#include "uefinetp.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER      3
#define DHCP_OPTION_PAD         0
#define DHCP_OPTION_END         255
#define DHCP_MAGIC_COOKIE       0x63538263

typedef struct _UEFI_DHCP_SESSION
{
    EFI_SERVICE_BINDING_PROTOCOL *Binding;
    EFI_HANDLE ChildHandle;
    EFI_DHCP4_PROTOCOL *Protocol;
} UEFI_DHCP_SESSION;

static EFI_GUID EfiDhcp4ServiceBindingGuid = EFI_DHCP4_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_GUID EfiDhcp4Guid = EFI_DHCP4_PROTOCOL_GUID;
static EFI_GUID EfiIp4Config2Guid = EFI_IP4_CONFIG2_PROTOCOL_GUID;

static BOOLEAN
UefiIpv4IsZero(
    _In_ const EFI_IPv4_ADDRESS *Address)
{
    return Address->Addr[0] == 0 &&
           Address->Addr[1] == 0 &&
           Address->Addr[2] == 0 &&
           Address->Addr[3] == 0;
}

static EFI_STATUS
UefiWaitForIp4Data(
    _In_ EFI_IP4_CONFIG2_PROTOCOL *Ip4Config,
    _In_ EFI_IP4_CONFIG2_DATA_TYPE DataType)
{
    EFI_STATUS Status = EFI_NOT_READY;
    UINTN Attempt;
    UINTN DataSize;

    for (Attempt = 0; Attempt < 25; Attempt++)
    {
        DataSize = 0;
        Status = Ip4Config->GetData(
            Ip4Config, DataType, &DataSize, NULL);
        if (Status == EFI_BUFFER_TOO_SMALL || !EFI_ERROR(Status))
            return EFI_SUCCESS;
        if (Status != EFI_NOT_READY)
            return Status;

        GlobalSystemTable->BootServices->Stall(200000);
    }

    return Status;
}

static EFI_STATUS
UefiSetIp4Data(
    _In_ EFI_IP4_CONFIG2_PROTOCOL *Ip4Config,
    _In_ EFI_IP4_CONFIG2_DATA_TYPE DataType,
    _In_ UINTN DataSize,
    _In_ VOID *Data)
{
    EFI_STATUS Status = EFI_NOT_READY;
    UINTN Attempt;

    for (Attempt = 0; Attempt < 3; Attempt++)
    {
        Status = Ip4Config->SetData(
            Ip4Config, DataType, DataSize, Data);
        if (!EFI_ERROR(Status))
            return EFI_SUCCESS;

        if (Status == EFI_NOT_READY)
            return UefiWaitForIp4Data(Ip4Config, DataType);

        if (Status != EFI_ACCESS_DENIED)
            return Status;

        GlobalSystemTable->BootServices->Stall(200000);
    }

    return Status;
}

static EFI_STATUS
UefiConfigureIp4(
    _In_ PUEFI_NET_CONTEXT Context,
    _In_ BOOLEAN SetGateway)
{
    EFI_STATUS Status;
    EFI_IP4_CONFIG2_PROTOCOL *Ip4Config = NULL;
    EFI_IP4_CONFIG2_POLICY Policy;

    Status = UefiNetGetProtocol(
        Context->ControllerHandle,
        &EfiIp4Config2Guid,
        (VOID **)&Ip4Config,
        NULL);
    if (EFI_ERROR(Status) || !Ip4Config)
        return Status;

    if (!SetGateway)
    {
        Policy = Ip4Config2PolicyStatic;
        Status = UefiSetIp4Data(
            Ip4Config,
            Ip4Config2DataTypePolicy,
            sizeof(Policy),
            &Policy);
        return Status;
    }

    if (UefiIpv4IsZero(&Context->Gateway))
    {
        return EFI_SUCCESS;
    }

    Status = UefiSetIp4Data(
        Ip4Config,
        Ip4Config2DataTypeGateway,
        sizeof(Context->Gateway),
        &Context->Gateway);
    return Status;
}

static BOOLEAN
UefiDhcpFindOption(
    _In_ EFI_DHCP4_PACKET *Packet,
    _In_ UINT8 RequestedCode,
    _Out_writes_bytes_(BufferSize) UINT8 *Buffer,
    _In_ UINTN BufferSize)
{
    UINT8 *Option;
    UINT8 *PacketEnd;
    UINT8 *AllocationEnd;

    if (!Packet ||
        !Buffer ||
        BufferSize == 0 ||
        Packet->Size < sizeof(Packet->Size) + sizeof(Packet->Length) ||
        Packet->Length >
            Packet->Size - sizeof(Packet->Size) - sizeof(Packet->Length) ||
        Packet->Dhcp4.Magik != DHCP_MAGIC_COOKIE)
    {
        return FALSE;
    }

    Option = Packet->Dhcp4.Option;
    PacketEnd = (UINT8 *)&Packet->Dhcp4 + Packet->Length;
    AllocationEnd = (UINT8 *)Packet + Packet->Size;
    if (PacketEnd > AllocationEnd || Option >= PacketEnd)
        return FALSE;

    while (Option < PacketEnd)
    {
        UINT8 Code = *Option++;
        UINT8 Length;

        if (Code == DHCP_OPTION_PAD)
            continue;
        if (Code == DHCP_OPTION_END || Option == PacketEnd)
            break;

        Length = *Option++;
        if ((UINTN)(PacketEnd - Option) < Length)
            break;

        if (Code == RequestedCode)
        {
            if (Length < BufferSize)
                return FALSE;
            RtlCopyMemory(Buffer, Option, BufferSize);
            return TRUE;
        }

        Option += Length;
    }

    return FALSE;
}

static VOID
UefiDhcpClose(
    _Inout_ UEFI_DHCP_SESSION *Session)
{
    if (Session->Protocol)
    {
        Session->Protocol->Stop(Session->Protocol);
        Session->Protocol->Configure(Session->Protocol, NULL);
    }

    if (Session->Binding && Session->ChildHandle)
    {
        Session->Binding->DestroyChild(
            Session->Binding, Session->ChildHandle);
    }

    RtlZeroMemory(Session, sizeof(*Session));
}

BOOLEAN
UefiDhcpAcquire(
    _Inout_ PUEFI_NET_CONTEXT Context)
{
    EFI_STATUS Status;
    UEFI_DHCP_SESSION Session;
    EFI_DHCP4_CONFIG_DATA Config;
    EFI_DHCP4_MODE_DATA Mode;
    UINT32 RetryTimeouts[] = {4, 8, 16, 32};
    UINT8 OptionValue[4];
    BOOLEAN Success = FALSE;

    RtlZeroMemory(&Session, sizeof(Session));
    RtlZeroMemory(&Config, sizeof(Config));
    RtlZeroMemory(&Mode, sizeof(Mode));

    Status = UefiConfigureIp4(Context, FALSE);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: cannot select static IP4 policy (Status %llx)\n",
              (unsigned long long)Status);
        return FALSE;
    }

    Status = UefiNetGetProtocol(
        Context->ControllerHandle,
        &EfiDhcp4ServiceBindingGuid,
        (VOID **)&Session.Binding,
        NULL);
    if (EFI_ERROR(Status) || !Session.Binding)
        goto Cleanup;

    Status = Session.Binding->CreateChild(
        Session.Binding, &Session.ChildHandle);
    if (EFI_ERROR(Status) || !Session.ChildHandle)
        goto Cleanup;

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Session.ChildHandle,
        &EfiDhcp4Guid,
        (VOID **)&Session.Protocol);
    if (EFI_ERROR(Status) || !Session.Protocol)
        goto Cleanup;

    Config.DiscoverTryCount = RTL_NUMBER_OF(RetryTimeouts);
    Config.DiscoverTimeout = RetryTimeouts;
    Config.RequestTryCount = RTL_NUMBER_OF(RetryTimeouts);
    Config.RequestTimeout = RetryTimeouts;

    Status = Session.Protocol->Configure(Session.Protocol, &Config);
    if (EFI_ERROR(Status))
        goto Cleanup;

    TRACE("UEFI HttpBoot: requesting DHCP4 lease\n");
    Status = Session.Protocol->Start(Session.Protocol, NULL);
    if (EFI_ERROR(Status))
        goto Cleanup;

    Status = Session.Protocol->GetModeData(Session.Protocol, &Mode);
    if (EFI_ERROR(Status))
        goto Cleanup;
    if (Mode.State != Dhcp4Bound)
        goto Cleanup;

    RtlCopyMemory(
        &Context->LocalAddress,
        &Mode.ClientAddress,
        sizeof(Context->LocalAddress));
    RtlCopyMemory(
        &Context->SubnetMask,
        &Mode.SubnetMask,
        sizeof(Context->SubnetMask));
    RtlCopyMemory(
        &Context->Gateway,
        &Mode.RouterAddress,
        sizeof(Context->Gateway));

    if (Mode.ReplyPacket)
    {
        if (!UefiIpv4IsZero(&Mode.ReplyPacket->Dhcp4.Header.YourAddr))
        {
            RtlCopyMemory(
                &Context->LocalAddress,
                &Mode.ReplyPacket->Dhcp4.Header.YourAddr,
                sizeof(Context->LocalAddress));
        }

        if (UefiDhcpFindOption(
                Mode.ReplyPacket,
                DHCP_OPTION_SUBNET_MASK,
                OptionValue,
                sizeof(OptionValue)))
        {
            RtlCopyMemory(
                &Context->SubnetMask,
                OptionValue,
                sizeof(Context->SubnetMask));
        }

        if (UefiDhcpFindOption(
                Mode.ReplyPacket,
                DHCP_OPTION_ROUTER,
                OptionValue,
                sizeof(OptionValue)))
        {
            RtlCopyMemory(
                &Context->Gateway,
                OptionValue,
                sizeof(Context->Gateway));
        }
    }

    if (UefiIpv4IsZero(&Context->LocalAddress))
        goto Cleanup;

    RtlZeroMemory(&Config, sizeof(Config));
    Status = Session.Protocol->Configure(Session.Protocol, &Config);
    if (EFI_ERROR(Status))
        goto Cleanup;

    Status = UefiConfigureIp4(Context, TRUE);
    if (EFI_ERROR(Status))
        goto Cleanup;

    TRACE("UEFI HttpBoot: DHCP4 IP=%u.%u.%u.%u mask=%u.%u.%u.%u gateway=%u.%u.%u.%u\n",
          Context->LocalAddress.Addr[0],
          Context->LocalAddress.Addr[1],
          Context->LocalAddress.Addr[2],
          Context->LocalAddress.Addr[3],
          Context->SubnetMask.Addr[0],
          Context->SubnetMask.Addr[1],
          Context->SubnetMask.Addr[2],
          Context->SubnetMask.Addr[3],
          Context->Gateway.Addr[0],
          Context->Gateway.Addr[1],
          Context->Gateway.Addr[2],
          Context->Gateway.Addr[3]);

    Success = TRUE;

Cleanup:
    UefiDhcpClose(&Session);
    return Success;
}
