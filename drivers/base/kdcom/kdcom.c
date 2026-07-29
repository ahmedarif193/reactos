/*
 * COPYRIGHT:       GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            drivers/base/kdcom/kdcom.c
 * PURPOSE:         COM port functions for the kernel debugger.
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include "kddll.h"

#include <arc/arc.h>
#include <stdlib.h>
#include <ndk/halfuncs.h>
#include <ndk/inbvfuncs.h>

#include <cportlib/cportlib.h>
#include <cportlib/uartinfo.h>
#include <reactos/drivers/bootvid/bootvid.h>

/* GLOBALS ********************************************************************/

CPPORT KdComPort;
BOOLEAN KdpScreenMode;
#ifdef KDDEBUG
CPPORT KdDebugComPort;
#endif

/* LOCALS *********************************************************************/

#define KDP_SCREEN_LINE_LENGTH_DEFAULT 80
#define KDP_SCREEN_LINE_LENGTH_MAXIMUM 2048
static CHAR KdpScreenLineBuffer[KDP_SCREEN_LINE_LENGTH_MAXIMUM + 1];
static ULONG KdpScreenLineBufferPosition;
static ULONG KdpScreenLineLength;
static ULONG KdpScreenLineCapacity = KDP_SCREEN_LINE_LENGTH_DEFAULT;

/* DEBUGGING ******************************************************************/

#ifdef KDDEBUG
#include <stdio.h>
ULONG KdpDbgPrint(const char *Format, ...)
{
    va_list ap;
    int Length;
    char* ptr;
    CHAR Buffer[512];

    va_start(ap, Format);
    Length = _vsnprintf(Buffer, sizeof(Buffer), Format, ap);
    va_end(ap);

    /* Check if we went past the buffer */
    if (Length == -1)
    {
        /* Terminate it if we went over-board */
        Buffer[sizeof(Buffer) - 1] = '\n';

        /* Put maximum */
        Length = sizeof(Buffer);
    }

    ptr = Buffer;
    while (Length--)
    {
        if (*ptr == '\n')
            CpPutByte(&KdDebugComPort, '\r');

        CpPutByte(&KdDebugComPort, *ptr++);
    }

    return 0;
}
#endif

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
KdD0Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdD3Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdSave(IN BOOLEAN SleepTransition)
{
    /* Nothing to do on COM ports */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdRestore(IN BOOLEAN SleepTransition)
{
    /* Nothing to do on COM ports */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdpPortInitialize(
    _In_ PUCHAR PortAddress,
    _In_ ULONG BaudRate)
{
    NTSTATUS Status;

    Status = CpInitialize(&KdComPort, PortAddress, BaudRate);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;

    KdComPortInUse = KdComPort.Address;
    return STATUS_SUCCESS;
}

VOID
NTAPI
KdpScreenPrint(
    _In_reads_bytes_(Length) PCCH String,
    _In_ ULONG Length)
{
    PCCH Character = String;

    while ((Character < String + Length) && *Character)
    {
        if (*Character == '\b')
        {
            if (KdpScreenLineLength > 0)
            {
                KdpScreenLineBuffer[--KdpScreenLineLength] = ANSI_NULL;
                KdpScreenLineBufferPosition = KdpScreenLineLength;
                HalDisplayString("\r");
                HalDisplayString(KdpScreenLineBuffer);
            }
        }
        else
        {
            KdpScreenLineBuffer[KdpScreenLineLength++] = *Character;
            KdpScreenLineBuffer[KdpScreenLineLength] = ANSI_NULL;
        }

        if ((*Character == '\n') ||
            (KdpScreenLineLength >= KdpScreenLineCapacity))
        {
            if (KdpScreenLineBufferPosition != KdpScreenLineLength)
            {
                HalDisplayString(KdpScreenLineBuffer + KdpScreenLineBufferPosition);
            }

            KdpScreenLineBuffer[0] = ANSI_NULL;
            KdpScreenLineLength = 0;
            KdpScreenLineBufferPosition = 0;
        }

        ++Character;
    }

    if (KdpScreenLineBufferPosition != KdpScreenLineLength)
    {
        HalDisplayString(KdpScreenLineBuffer + KdpScreenLineBufferPosition);
        KdpScreenLineBufferPosition = KdpScreenLineLength;
    }
}

/******************************************************************************
 * \name KdDebuggerInitialize0
 * \brief Phase 0 initialization.
 * \param [opt] LoaderBlock Pointer to the Loader parameter block. Can be NULL.
 * \return Status
 */
NTSTATUS
NTAPI
KdDebuggerInitialize0(IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
#define CONST_STR_LEN(x) (sizeof(x)/sizeof(x[0]) - 1)

    ULONG ComPortNumber   = DEFAULT_DEBUG_PORT;
    ULONG ComPortBaudRate = DEFAULT_DEBUG_BAUD_RATE;
    PUCHAR ComPortAddress = NULL;

    PSTR CommandLine, PortString, BaudString;
    ULONG Value;

    /* Check if we have a LoaderBlock */
    if (LoaderBlock)
    {
        /* Get the Command Line */
        CommandLine = LoaderBlock->LoadOptions;

        /* Upcase it */
        _strupr(CommandLine);

        /* Get the port and baud rate */
        PortString = strstr(CommandLine, "DEBUGPORT");
        BaudString = strstr(CommandLine, "BAUDRATE");

        /* Check if we got the DEBUGPORT parameter */
        if (PortString)
        {
            /* Move past the actual string and any spaces */
            PortString += CONST_STR_LEN("DEBUGPORT");
            while (*PortString == ' ') ++PortString;
            /* Skip the equals sign */
            if (*PortString) ++PortString;

            if (_strnicmp(PortString, "SCREEN", CONST_STR_LEN("SCREEN")) == 0)
            {
                KdpScreenMode = TRUE;
            }
            else if (_strnicmp(PortString, "COM", CONST_STR_LEN("COM")) != 0)
            {
                return STATUS_INVALID_PARAMETER;
            }

            /* Check for a valid serial port */
            if (!KdpScreenMode)
            {
                PortString += CONST_STR_LEN("COM");
                if (*PortString != ':')
                {
                    Value = (ULONG)atol(PortString);
                    if (Value > MAX_COM_PORTS)
                        return STATUS_INVALID_PARAMETER;
                    // if (Value > 0 && Value <= MAX_COM_PORTS)
                    /* Set the port to use */
                    ComPortNumber = Value;
                }
                else
                {
                    /* Retrieve and set its address */
                    Value = strtoul(PortString + 1, NULL, 0);
                    if (Value)
                    {
                        ComPortNumber = 0;
                        ComPortAddress = UlongToPtr(Value);
                    }
                }
            }
        }

        /* Check if we got a baud rate */
        if (BaudString)
        {
            /* Move past the actual string and any spaces */
            BaudString += CONST_STR_LEN("BAUDRATE");
            while (*BaudString == ' ') ++BaudString;

            /* Make sure we have a rate */
            if (*BaudString)
            {
                /* Read and set it */
                Value = (ULONG)atol(BaudString + 1);
                if (Value) ComPortBaudRate = Value;
            }
        }
    }

    if (KdpScreenMode)
        return STATUS_SUCCESS;

    if (!ComPortAddress)
        ComPortAddress = UlongToPtr(BaseArray[ComPortNumber]);

#ifdef KDDEBUG
    /*
     * Try to find a free COM port and use it as the KD debugging port.
     * NOTE: Inspired by freeldr/comm/rs232.c, Rs232PortInitialize(...)
     */
    {
    /*
     * Enumerate COM ports from the last to the first one, and stop
     * when we find a valid port. If we reach the first list element
     * (the undefined COM port), no valid port was found.
     */
    PUCHAR Address = NULL;
    ULONG ComPort;
    for (ComPort = MAX_COM_PORTS; ComPort > 0; ComPort--)
    {
        /* Check if the port exist; skip the KD port */
        Address = UlongToPtr(BaseArray[ComPort]);
        if ((Address != ComPortAddress) && CpDoesPortExist(Address))
            break;
    }
    if (ComPort != 0 && Address != NULL)
        CpInitialize(&KdDebugComPort, Address, DEFAULT_BAUD_RATE);
    }
#endif

    /* Initialize the port */
    return KdpPortInitialize(ComPortAddress, ComPortBaudRate);
}

/******************************************************************************
 * \name KdDebuggerInitialize1
 * \brief Phase 1 initialization.
 * \param [opt] LoaderBlock Pointer to the Loader parameter block. Can be NULL.
 * \return Status
 */
NTSTATUS
NTAPI
KdDebuggerInitialize1(IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    VID_DISPLAY_INFO DisplayInfo;

    UNREFERENCED_PARAMETER(LoaderBlock);

    if (KdpScreenMode && InbvIsBootDriverInstalled())
    {
        InbvQueryDisplayInfo(&DisplayInfo);
        if (DisplayInfo.CharacterWidth)
        {
            KdpScreenLineCapacity = min(DisplayInfo.Width / DisplayInfo.CharacterWidth, KDP_SCREEN_LINE_LENGTH_MAXIMUM);
        }
        if (!KdpScreenLineCapacity)
            KdpScreenLineCapacity = KDP_SCREEN_LINE_LENGTH_DEFAULT;

        InbvAcquireDisplayOwnership();
        InbvResetDisplay();
        InbvSolidColorFill(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1, BV_COLOR_BLACK);
        InbvSetTextColor(BV_COLOR_WHITE);
        InbvInstallDisplayStringFilter(NULL);
        InbvEnableDisplayString(TRUE);
        InbvSetScrollRegion(0, 0, DisplayInfo.Width - 1, DisplayInfo.Height - 1);
        HalDisplayString("   Screen debugging enabled\r\n");
    }

    return STATUS_SUCCESS;
}


VOID
NTAPI
KdpSendByte(IN UCHAR Byte)
{
    /* Send the byte */
    CpPutByte(&KdComPort, Byte);
}

KDP_STATUS
NTAPI
KdpPollByte(OUT PUCHAR OutByte)
{
    USHORT Status;

    /* Poll the byte */
    Status = CpGetByte(&KdComPort, OutByte, FALSE, FALSE);
    switch (Status)
    {
        case CP_GET_SUCCESS:
            return KDP_PACKET_RECEIVED;

        case CP_GET_NODATA:
            return KDP_PACKET_TIMEOUT;

        case CP_GET_ERROR:
        default:
            return KDP_PACKET_RESEND;
    }
}

KDP_STATUS
NTAPI
KdpReceiveByte(OUT PUCHAR OutByte)
{
    USHORT Status;

    /* Get the byte */
    Status = CpGetByte(&KdComPort, OutByte, TRUE, FALSE);
    switch (Status)
    {
        case CP_GET_SUCCESS:
            return KDP_PACKET_RECEIVED;

        case CP_GET_NODATA:
            return KDP_PACKET_TIMEOUT;

        case CP_GET_ERROR:
        default:
            return KDP_PACKET_RESEND;
    }
}

KDP_STATUS
NTAPI
KdpPollBreakIn(VOID)
{
    KDP_STATUS KdStatus;
    UCHAR Byte;

    KdStatus = KdpPollByte(&Byte);
    if ((KdStatus == KDP_PACKET_RECEIVED) && (Byte == BREAKIN_PACKET_BYTE))
    {
        return KDP_PACKET_RECEIVED;
    }
    return KDP_PACKET_TIMEOUT;
}

/* EOF */
