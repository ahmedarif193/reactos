/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI Serial I/O support
 */

#include <uefildr.h>

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern volatile BOOLEAN BootServicesExitedFlag;

static EFI_GUID EfiSerialIoProtocolGuid = EFI_SERIAL_IO_PROTOCOL_GUID;
static EFI_SERIAL_IO_PROTOCOL* UefiSerial = NULL;
static BOOLEAN UefiSerialTried = FALSE;

BOOLEAN
UefiSerialInitialize(VOID)
{
    EFI_STATUS Status;

    if (BootServicesExitedFlag)
        return FALSE;

    if (UefiSerial != NULL)
        return TRUE;

    if (UefiSerialTried)
        return FALSE;

    UefiSerialTried = TRUE;

    if ((GlobalSystemTable == NULL) || (GlobalSystemTable->BootServices == NULL))
        return FALSE;

    Status = GlobalSystemTable->BootServices->LocateProtocol(&EfiSerialIoProtocolGuid,
                                                             NULL,
                                                             (VOID**)&UefiSerial);
    if ((Status != EFI_SUCCESS) || (UefiSerial == NULL) || (UefiSerial->Write == NULL))
    {
        UefiSerial = NULL;
        return FALSE;
    }

    if (UefiSerial->SetAttributes != NULL)
    {
        UefiSerial->SetAttributes(UefiSerial,
                                  115200,
                                  1,
                                  1000000,
                                  NoParity,
                                  8,
                                  OneStopBit);
    }

    if (UefiSerial->SetControl != NULL)
    {
        UefiSerial->SetControl(UefiSerial,
                               EFI_SERIAL_DATA_TERMINAL_READY |
                               EFI_SERIAL_REQUEST_TO_SEND);
    }

    return TRUE;
}

VOID
UefiSerialPutChar(
    _In_ UCHAR Character)
{
    UINTN Size = 1;

    if ((UefiSerial == NULL) || BootServicesExitedFlag)
        return;

    UefiSerial->Write(UefiSerial, &Size, &Character);
}

VOID
UefiSerialDisableFirmware(VOID)
{
    UefiSerial = NULL;
}
