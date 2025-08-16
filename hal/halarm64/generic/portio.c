/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Port I/O Functions for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

/* FUNCTIONS *****************************************************************/

/*
 * ARM64 doesn't have direct port I/O like x86.
 * These functions map to memory-mapped I/O regions.
 * For now, we provide stubs that can be implemented for specific hardware.
 */

UCHAR
NTAPI
READ_PORT_UCHAR(IN PUCHAR Port)
{
    /* ARM64: Memory-mapped I/O */
    return *(volatile UCHAR *)Port;
}

USHORT
NTAPI
READ_PORT_USHORT(IN PUSHORT Port)
{
    /* ARM64: Memory-mapped I/O */
    return *(volatile USHORT *)Port;
}

ULONG
NTAPI
READ_PORT_ULONG(IN PULONG Port)
{
    /* ARM64: Memory-mapped I/O */
    return *(volatile ULONG *)Port;
}

VOID
NTAPI
WRITE_PORT_UCHAR(IN PUCHAR Port,
                 IN UCHAR Value)
{
    /* ARM64: Memory-mapped I/O */
    *(volatile UCHAR *)Port = Value;
}

VOID
NTAPI
WRITE_PORT_USHORT(IN PUSHORT Port,
                  IN USHORT Value)
{
    /* ARM64: Memory-mapped I/O */
    *(volatile USHORT *)Port = Value;
}

VOID
NTAPI
WRITE_PORT_ULONG(IN PULONG Port,
                 IN ULONG Value)
{
    /* ARM64: Memory-mapped I/O */
    *(volatile ULONG *)Port = Value;
}

VOID
NTAPI
READ_PORT_BUFFER_UCHAR(IN PUCHAR Port,
                       OUT PUCHAR Buffer,
                       IN ULONG Count)
{
    ULONG i;
    
    for (i = 0; i < Count; i++)
    {
        Buffer[i] = READ_PORT_UCHAR(Port);
    }
}

VOID
NTAPI
READ_PORT_BUFFER_USHORT(IN PUSHORT Port,
                        OUT PUSHORT Buffer,
                        IN ULONG Count)
{
    ULONG i;
    
    for (i = 0; i < Count; i++)
    {
        Buffer[i] = READ_PORT_USHORT(Port);
    }
}

VOID
NTAPI
READ_PORT_BUFFER_ULONG(IN PULONG Port,
                       OUT PULONG Buffer,
                       IN ULONG Count)
{
    ULONG i;
    
    for (i = 0; i < Count; i++)
    {
        Buffer[i] = READ_PORT_ULONG(Port);
    }
}

VOID
NTAPI
WRITE_PORT_BUFFER_UCHAR(IN PUCHAR Port,
                        IN PUCHAR Buffer,
                        IN ULONG Count)
{
    ULONG i;
    
    for (i = 0; i < Count; i++)
    {
        WRITE_PORT_UCHAR(Port, Buffer[i]);
    }
}

VOID
NTAPI
WRITE_PORT_BUFFER_USHORT(IN PUSHORT Port,
                         IN PUSHORT Buffer,
                         IN ULONG Count)
{
    ULONG i;
    
    for (i = 0; i < Count; i++)
    {
        WRITE_PORT_USHORT(Port, Buffer[i]);
    }
}

VOID
NTAPI
WRITE_PORT_BUFFER_ULONG(IN PULONG Port,
                        IN PULONG Buffer,
                        IN ULONG Count)
{
    ULONG i;
    
    for (i = 0; i < Count; i++)
    {
        WRITE_PORT_ULONG(Port, Buffer[i]);
    }
}

/* EOF */