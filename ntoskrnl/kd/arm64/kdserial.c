/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Serial Port Kernel Debugger Support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* ARM64 typically uses PL011 UART on development boards */
#define PL011_UART_BASE     0x09000000  /* Default for QEMU virt */
#define PL011_DR            0x00        /* Data Register */
#define PL011_FR            0x18        /* Flag Register */
#define PL011_FR_TXFF       0x20        /* Transmit FIFO Full */
#define PL011_FR_RXFE       0x10        /* Receive FIFO Empty */

PUCHAR KdComPortInUse = (PUCHAR)PL011_UART_BASE;

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
KdPortInitializeEx(IN PCPPORT PortInformation,
                   IN ULONG ComPortNumber)
{
    /* Initialize ARM64 serial port (PL011 UART) */
    /* The UART is usually already initialized by UEFI firmware */
    
    /* Store the port address */
    KdComPortInUse = (PUCHAR)PL011_UART_BASE;
    
    /* TODO: Proper initialization if needed */
    
    DPRINT1("ARM64 KD Serial Port initialized at 0x%p\n", KdComPortInUse);
    
    return TRUE;
}

BOOLEAN
NTAPI
KdPortGetByteEx(IN PCPPORT PortInformation,
                OUT PUCHAR ByteReceived)
{
    ULONG Flags;
    
    /* Check if data is available */
    Flags = *(volatile ULONG*)(KdComPortInUse + PL011_FR);
    if (Flags & PL011_FR_RXFE)
    {
        /* Receive FIFO is empty */
        return FALSE;
    }
    
    /* Read the byte */
    *ByteReceived = *(volatile UCHAR*)(KdComPortInUse + PL011_DR);
    return TRUE;
}

VOID
NTAPI
KdPortPutByteEx(IN PCPPORT PortInformation,
                IN UCHAR ByteToSend)
{
    ULONG Flags;
    
    /* Wait until transmit FIFO is not full */
    do
    {
        Flags = *(volatile ULONG*)(KdComPortInUse + PL011_FR);
    } while (Flags & PL011_FR_TXFF);
    
    /* Send the byte */
    *(volatile UCHAR*)(KdComPortInUse + PL011_DR) = ByteToSend;
}