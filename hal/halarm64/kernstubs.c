/*
 * PROJECT:         ReactOS HAL (ARM64)
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/hal/msi.h>

NTSTATUS NTAPI HalGetInterruptTargetInformation(PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation);
NTSTATUS NTAPI HalGetMessageRoutingInfo(PHAL_MESSAGE_ROUTING_INFO RoutingInfo);

NTSTATUS NTAPI HalpGetInterruptTargetInformation(PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    return HalGetInterruptTargetInformation(TargetInformation);
}

NTSTATUS NTAPI HalpGetMessageRoutingInfo(PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    return HalGetMessageRoutingInfo(RoutingInfo);
}
NTSTATUS NTAPI IopReserveIrqVectors(ULONG c, ULONG a, PKINTERRUPT *i) { (VOID)c;(VOID)a;(VOID)i; return 0; }

VOID NTAPI ScsiPortWritePortUchar(PUCHAR p, UCHAR v) { WRITE_PORT_UCHAR(p,v); }
UCHAR NTAPI ScsiPortReadPortUchar(PUCHAR p) { return READ_PORT_UCHAR(p); }
VOID NTAPI VideoPortWritePortUchar(PUCHAR p, UCHAR v) { WRITE_PORT_UCHAR(p,v); }
UCHAR NTAPI VideoPortReadPortUchar(PUCHAR p) { return READ_PORT_UCHAR(p); }
PHYSICAL_ADDRESS NTAPI ScsiPortConvertUlongToPhysicalAddress(ULONG_PTR a) { PHYSICAL_ADDRESS pa; pa.QuadPart=a; return pa; }
VOID NTAPI VideoPortQuerySystemTime(PLARGE_INTEGER t) { t->QuadPart = 0; }

/* Additional HAL exports */
#undef READ_PORT_BUFFER_UCHAR
#undef READ_PORT_BUFFER_USHORT
#undef READ_PORT_BUFFER_ULONG
#undef WRITE_PORT_BUFFER_UCHAR
#undef WRITE_PORT_BUFFER_USHORT
#undef WRITE_PORT_BUFFER_ULONG
#undef READ_REGISTER_UCHAR
#undef READ_REGISTER_USHORT
#undef READ_REGISTER_ULONG
#undef WRITE_REGISTER_UCHAR
#undef WRITE_REGISTER_USHORT
#undef WRITE_REGISTER_ULONG

VOID NTAPI ScsiPortWritePortUlong(PULONG p, ULONG v) { WRITE_PORT_ULONG(p,v); }
VOID NTAPI ScsiPortWritePortUshort(PUSHORT p, USHORT v) { WRITE_PORT_USHORT(p,v); }
ULONG NTAPI ScsiPortReadPortUlong(PULONG p) { return READ_PORT_ULONG(p); }
USHORT NTAPI ScsiPortReadPortUshort(PUSHORT p) { return READ_PORT_USHORT(p); }
VOID NTAPI ScsiPortWritePortBufferUlong(PULONG p, PULONG b, ULONG n) { ULONG i; for(i=0;i<n;i++) ScsiPortWritePortUlong(p,b[i]); }
VOID NTAPI ScsiPortWritePortBufferUshort(PUSHORT p, PUSHORT b, ULONG n) { ULONG i; for(i=0;i<n;i++) ScsiPortWritePortUshort(p,b[i]); }
VOID NTAPI ScsiPortReadPortBufferUlong(PULONG p, PULONG b, ULONG n) { ULONG i; for(i=0;i<n;i++) b[i]=ScsiPortReadPortUlong(p); }
VOID NTAPI ScsiPortReadPortBufferUshort(PUSHORT p, PUSHORT b, ULONG n) { ULONG i; for(i=0;i<n;i++) b[i]=ScsiPortReadPortUshort(p); }

VOID NTAPI ScsiPortReadRegisterUchar(PUCHAR r, PUCHAR v) { *v = *(volatile UCHAR*)r; }
VOID NTAPI ScsiPortReadRegisterUlong(PULONG r, PULONG v) { *v = *(volatile ULONG*)r; }
VOID NTAPI ScsiPortReadRegisterUshort(PUSHORT r, PUSHORT v) { *v = *(volatile USHORT*)r; }
VOID NTAPI ScsiPortWriteRegisterUchar(PUCHAR r, UCHAR v) { *(volatile UCHAR*)r = v; }
VOID NTAPI ScsiPortWriteRegisterUlong(PULONG r, ULONG v) { *(volatile ULONG*)r = v; }
VOID NTAPI ScsiPortWriteRegisterUshort(PUSHORT r, USHORT v) { *(volatile USHORT*)r = v; }

VOID NTAPI VideoPortReadPortUshort(PUSHORT p, PUSHORT v) { *v = *(volatile USHORT*)p; }
VOID NTAPI VideoPortReadPortUlong(PULONG p, PULONG v) { *v = *(volatile ULONG*)p; }
VOID NTAPI VideoPortWritePortUshort(PUSHORT p, USHORT v) { *(volatile USHORT*)p = v; }
VOID NTAPI VideoPortWritePortUlong(PULONG p, ULONG v) { *(volatile ULONG*)p = v; }
VOID NTAPI VideoPortReadRegisterUshort(PUSHORT r, PUSHORT v) { *v = *(volatile USHORT*)r; }
VOID NTAPI VideoPortWriteRegisterUshort(PUSHORT r, USHORT v) { *(volatile USHORT*)r = v; }
