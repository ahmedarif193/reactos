/*
 * UEFI Debug Output for ARM64
 * Provides RS232-compatible interface using UEFI Console Services
 *
 * NOTE: This file is disabled in favor of uefiserial.c which provides
 * proper serial-only output for debug messages
 */

#include <freeldr.h>

#if 0 /* Disabled - using uefiserial.c instead */
#if (defined(_M_ARM64) || defined(__aarch64__)) && defined(UEFIBOOT)

#include <uefildr.h>
#include <debug.h>

extern EFI_SYSTEM_TABLE* GlobalSystemTable;

/* RS232 compatibility functions for ARM64 UEFI */

BOOLEAN Rs232PortInitialize(IN ULONG ComPort, IN ULONG BaudRate)
{
    /* On UEFI ARM64, we use the UEFI console for debug output */
    /* Always return success as UEFI console is always available */
    (void)ComPort;
    (void)BaudRate;
    return TRUE;
}

BOOLEAN Rs232PortGetByte(PUCHAR ByteReceived)
{
    /* Not implemented for UEFI - we only output debug messages */
    (void)ByteReceived;
    return FALSE;
}

BOOLEAN Rs232PortPollByte(PUCHAR ByteReceived)
{
    /* Not implemented for UEFI - we only output debug messages */
    (void)ByteReceived;
    return FALSE;
}

VOID Rs232PortPutByte(UCHAR ByteToSend)
{
    CHAR16 WideChar[2];

    /* If UEFI console is available, output the character */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
    {
        /* Convert byte to wide character */
        WideChar[0] = (CHAR16)ByteToSend;
        WideChar[1] = 0;

        /* Special handling for newline */
        if (ByteToSend == '\n')
        {
            WideChar[0] = L'\r';
            GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
            WideChar[0] = L'\n';
        }

        /* Output the character */
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, WideChar);
    }
}

BOOLEAN Rs232PortInUse(PUCHAR Base)
{
    /* Not applicable for UEFI */
    (void)Base;
    return FALSE;
}

#endif /* ARM64 && UEFIBOOT */
#endif /* Disabled */
