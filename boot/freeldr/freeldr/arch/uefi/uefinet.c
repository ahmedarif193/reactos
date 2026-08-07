/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI network stack bring-up for HTTP boot
 */

#include <uefildr.h>
#include <Dhcp4.h>
#include <Hash2.h>
#include <Http.h>
#include <Ip4Config2.h>
#include <LoadedImage.h>
#include <NetworkInterfaceIdentifier.h>
#include <ServiceBinding.h>
#include <SimpleFileSystem.h>
#include <SimpleNetwork.h>

#include "uefinetp.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define NET_STAGE_NII   0x0001
#define NET_STAGE_SNP   0x0002
#define NET_STAGE_MNP   0x0004
#define NET_STAGE_ARP   0x0008
#define NET_STAGE_IP4   0x0010
#define NET_STAGE_DHCP  0x0020
#define NET_STAGE_UDP4  0x0040
#define NET_STAGE_TCP4  0x0080
#define NET_STAGE_HTTP  0x0100
#define NET_STAGE_IP4_CONFIG2 0x0200
#define NET_STAGE_HASH2 0x0400
#define NET_STAGE_HASH2_SERVICE_BINDING 0x0800

#define NET_REQUIRED_STAGES \
    (NET_STAGE_SNP | NET_STAGE_MNP | NET_STAGE_ARP | NET_STAGE_IP4 | \
     NET_STAGE_IP4_CONFIG2 | NET_STAGE_DHCP | NET_STAGE_UDP4 | \
     NET_STAGE_TCP4 | NET_STAGE_HTTP)

typedef enum _UEFI_NETWORK_DRIVER_PHASE
{
    UefiNetworkDriverBase,
    UefiNetworkDriverMnp,
    UefiNetworkDriverArp,
    UefiNetworkDriverUpper
} UEFI_NETWORK_DRIVER_PHASE;

typedef struct _UEFI_NETWORK_DRIVER
{
    CHAR16 *Path;
    BOOLEAN NicDriver;
    BOOLEAN HttpDriver;
    UEFI_NETWORK_DRIVER_PHASE Phase;
} UEFI_NETWORK_DRIVER;

static EFI_GUID EfiDhcp4ServiceBindingGuid = EFI_DHCP4_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_GUID EfiHash2Guid = EFI_HASH2_PROTOCOL_GUID;
static EFI_GUID EfiHash2ServiceBindingGuid = EFI_HASH2_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_GUID EfiHttpServiceBindingGuid = EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_GUID EfiIp4Config2Guid = EFI_IP4_CONFIG2_PROTOCOL_GUID;
static EFI_GUID EfiLoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID EfiNetworkInterfaceIdentifierGuid =
    EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_GUID;
static EFI_GUID EfiNetworkInterfaceIdentifier31Guid =
    EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_GUID_31;
static EFI_GUID EfiSimpleFileSystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID EfiSimpleNetworkGuid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
static EFI_GUID EfiFileInfoGuid = EFI_FILE_INFO_ID;

static EFI_GUID EfiRngGuid =
    {0x3152bca5, 0xeade, 0x433d,
     {0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44}};

static EFI_GUID EfiManagedNetworkServiceBindingGuid =
    {0xf36ff770, 0xa7e1, 0x42cf,
     {0x9e, 0xd2, 0x56, 0xf0, 0xf2, 0x71, 0xf4, 0x4c}};
static EFI_GUID EfiManagedNetworkGuid =
    {0x7ab33a91, 0xace5, 0x4326,
     {0xb5, 0x72, 0xe7, 0xee, 0x33, 0xd3, 0x9f, 0x16}};

/* EFI_MANAGED_NETWORK_CONFIG_DATA (UEFI spec 25.1); no vendored header. */
typedef struct _UEFI_MNP_CONFIG_DATA
{
    UINT32 ReceivedQueueTimeoutValue;
    UINT32 TransmitQueueTimeoutValue;
    UINT16 ProtocolTypeFilter;
    BOOLEAN EnableUnicastReceive;
    BOOLEAN EnableMulticastReceive;
    BOOLEAN EnableBroadcastReceive;
    BOOLEAN EnablePromiscuousReceive;
    BOOLEAN FlushQueuesOnReset;
    BOOLEAN EnableReceiveTimestamps;
    BOOLEAN DisableBackgroundPolling;
} UEFI_MNP_CONFIG_DATA;

/* Leading entry points of EFI_MANAGED_NETWORK_PROTOCOL; the rest is unused. */
typedef struct _UEFI_MNP_VIEW UEFI_MNP_VIEW;
struct _UEFI_MNP_VIEW
{
    VOID *GetModeData;
    EFI_STATUS (EFIAPI *Configure)(
        UEFI_MNP_VIEW *This,
        UEFI_MNP_CONFIG_DATA *ConfigData);
};
static EFI_GUID EfiArpServiceBindingGuid =
    {0xf44c00ee, 0x1f2c, 0x4a00,
     {0xaa, 0x09, 0x1c, 0x9f, 0x3e, 0x08, 0x00, 0xa3}};
static EFI_GUID EfiIp4ServiceBindingGuid =
    {0xc51711e7, 0xb4bf, 0x404a,
     {0xbf, 0xb8, 0x0a, 0x04, 0x8e, 0xf1, 0xff, 0xe4}};
static EFI_GUID EfiUdp4ServiceBindingGuid =
    {0x83f01464, 0x99bd, 0x45e5,
     {0xb3, 0x83, 0xaf, 0x63, 0x05, 0xd8, 0xe9, 0xe6}};
static EFI_GUID EfiTcp4ServiceBindingGuid =
    {0x00720665, 0x67eb, 0x4a99,
     {0xba, 0xf7, 0xd3, 0xc3, 0x3a, 0x1c, 0x7c, 0xc9}};

/*
 * Bind the stack in stages. ARP is the first consumer that configures MNP,
 * which may reinitialize SNP, so DHCP, IP, and HTTP must not bind until the
 * board-specific MAC repair has run after ARP.
 *
 * Only the layers the board firmware lacks are shipped. The LattePanda Mu
 * firmware has no network stack at all, while the Raspberry Pi 5 firmware
 * carries everything except a driver for its own NIC (and an HTTP driver
 * that allows plain http:// URLs).
 */
static const UEFI_NETWORK_DRIVER NetworkDrivers[] =
{
#if defined(_M_ARM64)
    {L"\\EFI\\BOOT\\drivers\\Rp1GemDxe.efi", TRUE, FALSE, UefiNetworkDriverBase},
    {L"\\EFI\\BOOT\\drivers\\HttpDxe.efi", FALSE, TRUE, UefiNetworkDriverUpper},
#else
    {L"\\EFI\\BOOT\\drivers\\DpcDxe.efi", FALSE, FALSE, UefiNetworkDriverBase},
    {L"\\EFI\\BOOT\\drivers\\RngDxe.efi", FALSE, FALSE, UefiNetworkDriverBase},
    {L"\\EFI\\BOOT\\drivers\\Hash2DxeCrypto.efi", FALSE, FALSE, UefiNetworkDriverBase},
    {L"\\EFI\\BOOT\\drivers\\RtkUndiDxe.efi", TRUE, FALSE, UefiNetworkDriverBase},
    {L"\\EFI\\BOOT\\drivers\\SnpDxe.efi", TRUE, FALSE, UefiNetworkDriverBase},
    {L"\\EFI\\BOOT\\drivers\\MnpDxe.efi", FALSE, FALSE, UefiNetworkDriverMnp},
    {L"\\EFI\\BOOT\\drivers\\ArpDxe.efi", FALSE, FALSE, UefiNetworkDriverArp},
    {L"\\EFI\\BOOT\\drivers\\Ip4Dxe.efi", FALSE, FALSE, UefiNetworkDriverUpper},
    {L"\\EFI\\BOOT\\drivers\\Udp4Dxe.efi", FALSE, FALSE, UefiNetworkDriverUpper},
    {L"\\EFI\\BOOT\\drivers\\Dhcp4Dxe.efi", FALSE, FALSE, UefiNetworkDriverUpper},
    {L"\\EFI\\BOOT\\drivers\\TcpDxe.efi", FALSE, FALSE, UefiNetworkDriverUpper},
    {L"\\EFI\\BOOT\\drivers\\DnsDxe.efi", FALSE, FALSE, UefiNetworkDriverUpper},
    {L"\\EFI\\BOOT\\drivers\\HttpUtilitiesDxe.efi", FALSE, FALSE, UefiNetworkDriverUpper},
    {L"\\EFI\\BOOT\\drivers\\HttpDxe.efi", FALSE, TRUE, UefiNetworkDriverUpper},
#endif
};

static BOOLEAN NetworkDriversAttempted;
static BOOLEAN UpperNetworkDriversReleased;
static EFI_HANDLE NetworkDriverImages[RTL_NUMBER_OF(NetworkDrivers)];
static BOOLEAN NetworkDriversStarted[RTL_NUMBER_OF(NetworkDrivers)];

static UINT32
UefiNetFnv1a(
    _In_reads_bytes_(Size) const VOID *Buffer,
    _In_ UINTN Size)
{
    const UINT8 *Bytes = Buffer;
    UINT32 Hash = 2166136261U;
    UINTN Index;

    for (Index = 0; Index < Size; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 16777619U;
    }

    return Hash;
}

static UINTN
UefiCountProtocol(
    _In_ EFI_GUID *ProtocolGuid,
    _Out_opt_ EFI_HANDLE *FirstHandle)
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles = NULL;
    UINTN HandleCount = 0;

    if (FirstHandle)
        *FirstHandle = NULL;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, ProtocolGuid, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status))
        HandleCount = 0;

    if (FirstHandle && HandleCount != 0)
        *FirstHandle = Handles[0];

    if (Handles)
        GlobalSystemTable->BootServices->FreePool(Handles);

    return HandleCount;
}

EFI_STATUS
UefiNetGetProtocol(
    _In_opt_ EFI_HANDLE PreferredHandle,
    _In_ EFI_GUID *ProtocolGuid,
    _Out_ VOID **Protocol,
    _Out_opt_ EFI_HANDLE *ProtocolHandle)
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles = NULL;
    UINTN HandleCount = 0;

    *Protocol = NULL;
    if (ProtocolHandle)
        *ProtocolHandle = NULL;


    if (PreferredHandle)
    {
        Status = GlobalSystemTable->BootServices->HandleProtocol(
            PreferredHandle, ProtocolGuid, Protocol);
        if (!EFI_ERROR(Status) && *Protocol)
        {
            if (ProtocolHandle)
                *ProtocolHandle = PreferredHandle;
            return EFI_SUCCESS;
        }

    }

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, ProtocolGuid, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status) || HandleCount == 0)
    {
        if (Handles)
            GlobalSystemTable->BootServices->FreePool(Handles);
        return EFI_ERROR(Status) ? Status : EFI_NOT_FOUND;
    }

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Handles[0], ProtocolGuid, Protocol);
    if (!EFI_ERROR(Status) && ProtocolHandle)
        *ProtocolHandle = Handles[0];


    GlobalSystemTable->BootServices->FreePool(Handles);
    return Status;
}

static UINT32
UefiGetNetworkStageMask(
    _Out_opt_ EFI_HANDLE *HttpController)
{
    UINT32 Mask = 0;

    if (UefiCountProtocol(&EfiNetworkInterfaceIdentifierGuid, NULL) ||
        UefiCountProtocol(&EfiNetworkInterfaceIdentifier31Guid, NULL))
    {
        Mask |= NET_STAGE_NII;
    }
    if (UefiCountProtocol(&EfiSimpleNetworkGuid, NULL))
        Mask |= NET_STAGE_SNP;
    if (UefiCountProtocol(&EfiManagedNetworkServiceBindingGuid, NULL))
        Mask |= NET_STAGE_MNP;
    if (UefiCountProtocol(&EfiArpServiceBindingGuid, NULL))
        Mask |= NET_STAGE_ARP;
    if (UefiCountProtocol(&EfiIp4ServiceBindingGuid, NULL))
        Mask |= NET_STAGE_IP4;
    if (UefiCountProtocol(&EfiIp4Config2Guid, NULL))
        Mask |= NET_STAGE_IP4_CONFIG2;
    if (UefiCountProtocol(&EfiDhcp4ServiceBindingGuid, NULL))
        Mask |= NET_STAGE_DHCP;
    if (UefiCountProtocol(&EfiUdp4ServiceBindingGuid, NULL))
        Mask |= NET_STAGE_UDP4;
    if (UefiCountProtocol(&EfiHash2Guid, NULL))
        Mask |= NET_STAGE_HASH2;
    if (UefiCountProtocol(&EfiHash2ServiceBindingGuid, NULL))
        Mask |= NET_STAGE_HASH2_SERVICE_BINDING;
    if (UefiCountProtocol(&EfiTcp4ServiceBindingGuid, NULL))
        Mask |= NET_STAGE_TCP4;
    if (UefiCountProtocol(&EfiHttpServiceBindingGuid, HttpController))
        Mask |= NET_STAGE_HTTP;

    return Mask;
}

static VOID
UefiTraceNetworkStages(
    _In_ UINT32 Mask)
{
    TRACE("UEFI Network: SNP=%s MNP=%s ARP=%s IP4=%s IP4CFG=%s DHCP4=%s UDP4=%s TCP4=%s HTTP=%s\n",
          (Mask & NET_STAGE_SNP) ? "yes" : "no",
          (Mask & NET_STAGE_MNP) ? "yes" : "no",
          (Mask & NET_STAGE_ARP) ? "yes" : "no",
          (Mask & NET_STAGE_IP4) ? "yes" : "no",
          (Mask & NET_STAGE_IP4_CONFIG2) ? "yes" : "no",
          (Mask & NET_STAGE_DHCP) ? "yes" : "no",
          (Mask & NET_STAGE_UDP4) ? "yes" : "no",
          (Mask & NET_STAGE_TCP4) ? "yes" : "no",
          (Mask & NET_STAGE_HTTP) ? "yes" : "no");
}

static VOID
UefiConnectAllControllers(
    _In_ BOOLEAN Recursive)
{
    EFI_STATUS Status;
    EFI_HANDLE *Handles = NULL;
    UINTN HandleCount = 0;
    UINTN Index;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        AllHandles, NULL, NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status))
        return;

    for (Index = 0; Index < HandleCount; Index++)
    {
        GlobalSystemTable->BootServices->ConnectController(
            Handles[Index], NULL, NULL, Recursive);
    }

    GlobalSystemTable->BootServices->FreePool(Handles);
}

static BOOLEAN
UefiLoadDxeImage(
    _In_ EFI_FILE_PROTOCOL *Root,
    _In_ CHAR16 *Path,
    _Out_ EFI_HANDLE *ImageHandle)
{
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *Info = NULL;
    VOID *ImageBuffer = NULL;
    UINTN InfoSize = 0;
    UINTN ImageSize = 0;
    BOOLEAN Loaded = FALSE;

    *ImageHandle = NULL;

    Status = Root->Open(Root, &File, Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
        goto Cleanup;

    Status = File->GetInfo(File, &EfiFileInfoGuid, &InfoSize, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL || InfoSize < sizeof(*Info))
        goto Cleanup;

    Status = GlobalSystemTable->BootServices->AllocatePool(
        EfiLoaderData, InfoSize, (VOID **)&Info);
    if (EFI_ERROR(Status))
        goto Cleanup;

    Status = File->GetInfo(File, &EfiFileInfoGuid, &InfoSize, Info);
    if (EFI_ERROR(Status))
        goto Cleanup;

    ImageSize = (UINTN)Info->FileSize;
    if (ImageSize == 0 || (UINT64)ImageSize != Info->FileSize)
        goto Cleanup;

    Status = GlobalSystemTable->BootServices->AllocatePool(
        EfiLoaderData, ImageSize, &ImageBuffer);
    if (EFI_ERROR(Status))
        goto Cleanup;

    Status = File->Read(File, &ImageSize, ImageBuffer);
    if (EFI_ERROR(Status) || ImageSize != (UINTN)Info->FileSize)
        goto Cleanup;

    Status = GlobalSystemTable->BootServices->LoadImage(
        FALSE,
        GlobalImageHandle,
        NULL,
        ImageBuffer,
        ImageSize,
        ImageHandle);
    Loaded = !EFI_ERROR(Status) && *ImageHandle != NULL;

Cleanup:
    if (!Loaded)
    {
        *ImageHandle = NULL;
    }
    if (ImageBuffer)
        GlobalSystemTable->BootServices->FreePool(ImageBuffer);
    if (Info)
        GlobalSystemTable->BootServices->FreePool(Info);
    if (File)
        File->Close(File);

    return Loaded;
}

static VOID
UefiStartNetworkDrivers(
    _In_ UEFI_NETWORK_DRIVER_PHASE Phase)
{
    EFI_STATUS Status;
    UINTN Index;

    for (Index = 0; Index < RTL_NUMBER_OF(NetworkDrivers); Index++)
    {
        if (!NetworkDriverImages[Index] ||
            NetworkDriversStarted[Index] ||
            NetworkDrivers[Index].Phase != Phase)
        {
            continue;
        }

        TRACE("UEFI Network: starting %S\n", NetworkDrivers[Index].Path);
        Status = GlobalSystemTable->BootServices->StartImage(
            NetworkDriverImages[Index], NULL, NULL);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI Network: %S failed to start (Status %llx)\n",
                  NetworkDrivers[Index].Path,
                  (unsigned long long)Status);
            GlobalSystemTable->BootServices->UnloadImage(
                NetworkDriverImages[Index]);
            NetworkDriverImages[Index] = NULL;
            continue;
        }

        TRACE("UEFI Network: started %S\n", NetworkDrivers[Index].Path);
        NetworkDriversStarted[Index] = TRUE;
    }
}

static VOID
UefiLoadNetworkDrivers(
    _In_ BOOLEAN IncludeNicDrivers)
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;
    UINTN Index;

    if (NetworkDriversAttempted)
    {
        return;
    }
    NetworkDriversAttempted = TRUE;


    Status = GlobalSystemTable->BootServices->HandleProtocol(
        GlobalImageHandle, &EfiLoadedImageGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage)
        goto Cleanup;

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        LoadedImage->DeviceHandle,
        &EfiSimpleFileSystemGuid,
        (VOID **)&FileSystem);
    if (EFI_ERROR(Status) || !FileSystem)
        goto Cleanup;

    Status = FileSystem->OpenVolume(FileSystem, &Root);
    if (EFI_ERROR(Status) || !Root)
        goto Cleanup;

    for (Index = 0; Index < RTL_NUMBER_OF(NetworkDrivers); Index++)
    {
        if (!IncludeNicDrivers && NetworkDrivers[Index].NicDriver)
            continue;

        UefiLoadDxeImage(
            Root,
            NetworkDrivers[Index].Path,
            &NetworkDriverImages[Index]);
    }

    UefiStartNetworkDrivers(UefiNetworkDriverBase);

Cleanup:
    if (Root)
        Root->Close(Root);

}

static BOOLEAN
UefiNetAnyDriverLoaded(VOID)
{
    UINTN Index;

    for (Index = 0; Index < RTL_NUMBER_OF(NetworkDrivers); Index++)
    {
        if (NetworkDriverImages[Index])
            return TRUE;
    }

    return FALSE;
}

static BOOLEAN
UefiNetUpperDriversBlockedByRng(VOID)
{
    UINTN Index;

    if (UefiCountProtocol(&EfiRngGuid, NULL))
        return FALSE;

    for (Index = 0; Index < RTL_NUMBER_OF(NetworkDrivers); Index++)
    {
        if (NetworkDrivers[Index].Phase == UefiNetworkDriverUpper &&
            !NetworkDrivers[Index].HttpDriver &&
            NetworkDriverImages[Index] &&
            !NetworkDriversStarted[Index])
        {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
UefiGetSimpleNetwork(
    _Out_ EFI_SIMPLE_NETWORK_PROTOCOL **Snp)
{
    EFI_STATUS Status;
    EFI_HANDLE Handle = NULL;
    UINTN Count;

    *Snp = NULL;
    Count = UefiCountProtocol(&EfiSimpleNetworkGuid, &Handle);
    if (!Count)
        return FALSE;

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Handle, &EfiSimpleNetworkGuid, (VOID **)Snp);
    return !EFI_ERROR(Status) && *Snp && (*Snp)->Mode;
}

static BOOLEAN
UefiInitializeSimpleNetwork(
    _Out_ EFI_SIMPLE_NETWORK_PROTOCOL **Snp)
{
    EFI_STATUS Status;
    EFI_SIMPLE_NETWORK_PROTOCOL *Protocol;
    UINTN Index;
    UINTN HardwareAddressSize;

    if (!UefiGetSimpleNetwork(&Protocol))
        return FALSE;


    switch (Protocol->Mode->State)
    {
        case EfiSimpleNetworkStopped:
            Status = Protocol->Start(Protocol);
            if (EFI_ERROR(Status))
                return FALSE;
            /* Fall through. */

        case EfiSimpleNetworkStarted:
            Status = Protocol->Initialize(Protocol, 0, 0);
            if (EFI_ERROR(Status))
                return FALSE;
            break;

        case EfiSimpleNetworkInitialized:
            break;

        default:
            return FALSE;
    }

    UefiLattePandaFixMac(Protocol);

    Status = Protocol->ReceiveFilters(
        Protocol,
        EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
            EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
        0,
        FALSE,
        0,
        NULL);
    if (EFI_ERROR(Status) && Status != EFI_UNSUPPORTED)
    {
        TRACE("UEFI Network: ReceiveFilters failed (Status %llx)\n",
              (unsigned long long)Status);
    }

    HardwareAddressSize = Protocol->Mode->HwAddressSize;
    if (HardwareAddressSize > sizeof(Protocol->Mode->CurrentAddress.Addr))
        HardwareAddressSize = sizeof(Protocol->Mode->CurrentAddress.Addr);

    TRACE("UEFI Network: SNP ready, MAC=");
    for (Index = 0; Index < HardwareAddressSize; Index++)
    {
        TRACE("%02x%s",
              Protocol->Mode->CurrentAddress.Addr[Index],
              (Index + 1 == HardwareAddressSize) ? "" : ":");
    }
    TRACE(" media-detect=%s media=%s\n",
          Protocol->Mode->MediaPresentSupported ? "yes" : "no",
          Protocol->Mode->MediaPresent ? "present" : "absent");

    *Snp = Protocol;
    return TRUE;
}

static EFI_HANDLE MnpAnchorChild;
static BOOLEAN MnpAnchorConfigured;
static BOOLEAN MnpAnchorSnpReset;

/*
 * Keep one freeldr-owned MNP child configured for the whole boot. MnpDxe
 * (re)initializes the NIC when its first child is configured, which resets
 * the RTL8168's programmed MAC to the (all-zero) permanent address, and it
 * shuts the NIC down again when the last child goes away. Configuring this
 * anchor before ArpDxe binds makes that one-time reset happen here, lets
 * the board MAC repair run before ARP snapshots the station address (a
 * zero snapshot poisons every ARP frame and reply, so peers address their
 * TCP traffic to 00:00:00:00:00:00 and the NIC filter drops it), and pins
 * the NIC started so MnpStartSnp never runs again to fail with
 * EFI_ALREADY_STARTED.
 */
static VOID
UefiNetEnsureMnpAnchor(
    _In_opt_ EFI_SIMPLE_NETWORK_PROTOCOL *Snp)
{
    EFI_STATUS Status;
    EFI_SERVICE_BINDING_PROTOCOL *Binding = NULL;
    UEFI_MNP_VIEW *Mnp = NULL;
    UEFI_MNP_CONFIG_DATA Config;
    EFI_HANDLE Controller = NULL;

    if (MnpAnchorConfigured)
        return;

    if (!UefiCountProtocol(&EfiManagedNetworkServiceBindingGuid, &Controller) ||
        !Controller)
    {
        return;
    }

    Status = UefiNetGetProtocol(
        Controller,
        &EfiManagedNetworkServiceBindingGuid,
        (VOID **)&Binding,
        NULL);
    if (EFI_ERROR(Status) || !Binding)
        return;

    if (!MnpAnchorChild)
    {
        Status = Binding->CreateChild(Binding, &MnpAnchorChild);
        if (EFI_ERROR(Status) || !MnpAnchorChild)
        {
            MnpAnchorChild = NULL;
            return;
        }
    }

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        MnpAnchorChild, &EfiManagedNetworkGuid, (VOID **)&Mnp);
    if (EFI_ERROR(Status) || !Mnp)
    {
        return;
    }

    /*
     * MnpStartSnp (run by the first child Configure) does Snp->Start and
     * treats EFI_ALREADY_STARTED as fatal, and MnpDxe does not roll the
     * configured-children count back on failure. FreeLdr initialized the
     * NIC earlier, so put it back into the Stopped state exactly once so
     * that first Configure starts it cleanly instead of erroring.
     */
    if (!MnpAnchorSnpReset && Snp && Snp->Mode &&
        Snp->Mode->State != EfiSimpleNetworkStopped)
    {
        MnpAnchorSnpReset = TRUE;
        if (Snp->Mode->State == EfiSimpleNetworkInitialized)
            Snp->Shutdown(Snp);
        Snp->Stop(Snp);
    }

    RtlZeroMemory(&Config, sizeof(Config));
    Config.EnableUnicastReceive = TRUE;
    Config.EnableBroadcastReceive = TRUE;
    Config.FlushQueuesOnReset = TRUE;
    Status = Mnp->Configure(Mnp, &Config);
    if (!EFI_ERROR(Status))
        MnpAnchorConfigured = TRUE;
}

/*
 * Bind the NIC stack with our HttpDxe as the context-override (highest
 * priority) driver. The firmware ships its own HttpDxe built with plain
 * HTTP disabled; when both compete for the TCP service binding the older
 * firmware driver-binding handle wins the tie, so the permissive driver
 * loaded from the boot volume must be first in line the moment the TCP
 * service binding appears.
 */
static VOID
UefiConnectHttpPriority(VOID)
{
    EFI_HANDLE SnpHandle = NULL;
    EFI_HANDLE DriverList[2];

    DriverList[0] = UefiNetGetHttpDriverImage();
    DriverList[1] = NULL;
    if (!DriverList[0])
        return;

    if (!UefiCountProtocol(&EfiSimpleNetworkGuid, &SnpHandle) || !SnpHandle)
        return;

    GlobalSystemTable->BootServices->ConnectController(
        SnpHandle, DriverList, NULL, TRUE);
}

BOOLEAN
UefiNetForceRebindHttp(
    _Inout_ PUEFI_NET_CONTEXT Context)
{
    EFI_SIMPLE_NETWORK_PROTOCOL *Snp = NULL;
    EFI_HANDLE DriverList[2];

    if (!Context || !Context->ControllerHandle)
        return FALSE;

    DriverList[0] = UefiNetGetHttpDriverImage();
    DriverList[1] = NULL;
    if (!DriverList[0])
        return FALSE;

    /*
     * Last resort: tear the whole NIC driver stack down and rebuild it with
     * our HttpDxe as the context-override driver. This also recovers an SNP
     * wedged outside the Stopped state, which otherwise makes MnpStartSnp
     * fail with EFI_ALREADY_STARTED on the next DHCP attempt.
     */
    GlobalSystemTable->BootServices->DisconnectController(
        Context->ControllerHandle, NULL, NULL);

    /* The teardown destroyed the MNP anchor child along with MnpDxe. */
    MnpAnchorChild = NULL;
    MnpAnchorConfigured = FALSE;
    MnpAnchorSnpReset = FALSE;

    GlobalSystemTable->BootServices->ConnectController(
        Context->ControllerHandle, DriverList, NULL, TRUE);

    /*
     * MnpDxe owns the SNP lifecycle from here on: it stops the NIC at bind
     * and restarts it when the first MNP child is configured. Only refresh
     * the (reinstalled) interface pointer and repair the MAC; starting the
     * NIC here would wedge the next MnpStartSnp with EFI_ALREADY_STARTED.
     */
    if (!UefiGetSimpleNetwork(&Snp))
    {
        return FALSE;
    }

    Context->Snp = Snp;
    UefiNetEnsureMnpAnchor(Snp);
    UefiLattePandaFixMac(Snp);
    return TRUE;
}

BOOLEAN
UefiNetPrepare(
    _Out_ PUEFI_NET_CONTEXT Context)
{
    EFI_HANDLE HttpController = NULL;
    EFI_SIMPLE_NETWORK_PROTOCOL *Snp = NULL;
    UINT32 Mask;
    UINT32 PreviousMask = (UINT32)-1;
    UINTN Attempts = 0;

    if (!Context ||
        !GlobalSystemTable ||
        !GlobalSystemTable->BootServices)
    {
        return FALSE;
    }

    RtlZeroMemory(Context, sizeof(*Context));

    /*
     * Expose PCI I/O handles, apply the board's RTL8168 preparation before
     * UNDI binds, then load only the layers missing from firmware.
     */
    TRACE("UEFI Network: connecting controllers\n");
    UefiConnectAllControllers(FALSE);
    UefiLattePandaPrepareNic();

    Mask = UefiGetNetworkStageMask(NULL);
    TRACE("UEFI Network: firmware provides mask %08lx, need %08lx\n",
          (unsigned long)Mask,
          (unsigned long)NET_REQUIRED_STAGES);
    if ((Mask & NET_REQUIRED_STAGES) != NET_REQUIRED_STAGES)
    {
        UefiLoadNetworkDrivers((Mask & NET_STAGE_SNP) == 0);
        if (!UefiNetAnyDriverLoaded())
        {
            TRACE("UEFI Network: required protocols missing and no boot volume drivers available, giving up\n");
            return FALSE;
        }
    }
    TRACE("UEFI Network: waiting for the protocol stack\n");

    for (;;)
    {
        Mask = UefiGetNetworkStageMask(&HttpController);
        if (Mask != PreviousMask)
        {
            UefiTraceNetworkStages(Mask);
            PreviousMask = Mask;
        }

        if (!Snp && (Mask & NET_STAGE_SNP))
        {
            if (!UefiInitializeSimpleNetwork(&Snp))
                TRACE("UEFI Network: SNP initialization failed\n");
        }

        if (Snp && (Mask & NET_REQUIRED_STAGES) == NET_REQUIRED_STAGES)
        {
            /*
             * Keep the address repaired after the remaining drivers bind.
             * Their services were released only after the ARP transition
             * repair below, so their cached SNP mode data is already valid.
             */
            UefiLattePandaFixMac(Snp);
            Context->ControllerHandle = HttpController;
            Context->Snp = Snp;
            return Context->ControllerHandle != NULL;
        }

        if (Snp && !(Mask & NET_STAGE_MNP))
        {
            UefiStartNetworkDrivers(UefiNetworkDriverMnp);
        }
        else if (Snp && !(Mask & NET_STAGE_ARP))
        {
            /*
             * Take MnpDxe's one-time NIC reset now and repair the MAC so
             * ArpDxe binds with the fixed station address already live.
             */
            UefiNetEnsureMnpAnchor(Snp);
            UefiLattePandaFixMac(Snp);
            UefiStartNetworkDrivers(UefiNetworkDriverArp);
        }
        else if (Snp && !UpperNetworkDriversReleased)
        {
            if (UefiNetUpperDriversBlockedByRng())
            {
                TRACE("UEFI Network: EFI_RNG_PROTOCOL missing, the bundled IP stack cannot start\n");
                return FALSE;
            }
            UefiLattePandaFixMac(Snp);
            UpperNetworkDriversReleased = TRUE;
            UefiStartNetworkDrivers(UefiNetworkDriverUpper);
        }

        if (Snp)
            UefiConnectHttpPriority();
        UefiConnectAllControllers(Snp != NULL);

        if (++Attempts >= UEFI_NETWORK_MAX_WAIT_ATTEMPTS)
        {
            TRACE("UEFI Network: stack incomplete after %lu attempts (mask %08lx snp=%s), giving up\n", (unsigned long)Attempts, (unsigned long)Mask, Snp ? "yes" : "no");
            return FALSE;
        }

        if (Attempts % 10 == 0)
        {
            TRACE("UEFI Network: still waiting, mask %08lx snp=%s\n",
                  (unsigned long)Mask,
                  Snp ? "yes" : "no");
        }

        if (Attempts >= UEFI_NETWORK_MAX_ATTEMPTS)
        {
            TRACE("UEFI Network: no usable stack after %lu attempts (mask %08lx snp=%s), continuing without network\n",
                  (unsigned long)Attempts,
                  (unsigned long)Mask,
                  Snp ? "yes" : "no");
            return FALSE;
        }

        GlobalSystemTable->BootServices->Stall(UEFI_NETWORK_RETRY_DELAY_US);
    }
}

EFI_HANDLE
UefiNetGetHttpDriverImage(VOID)
{
    UINTN Index;

    for (Index = 0; Index < RTL_NUMBER_OF(NetworkDrivers); Index++)
    {
        if (NetworkDrivers[Index].HttpDriver && NetworkDriversStarted[Index])
            return NetworkDriverImages[Index];
    }

    return NULL;
}

BOOLEAN
UefiNetMediaPresent(
    _In_ PUEFI_NET_CONTEXT Context)
{
    EFI_STATUS Status;
    UINT32 InterruptStatus;

    if (!Context || !Context->Snp || !Context->Snp->Mode)
    {
        return FALSE;
    }

    InterruptStatus = 0;
    Status = Context->Snp->GetStatus(
        Context->Snp, &InterruptStatus, NULL);
    if (EFI_ERROR(Status))
        return FALSE;

    if (!Context->Snp->Mode->MediaPresentSupported)
        return TRUE;

    return Context->Snp->Mode->MediaPresent;
}
