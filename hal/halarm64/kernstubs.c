#include <ntddk.h>

VOID NTAPI HalSweepIcache(VOID) { __asm__ volatile("ic iallu\n\tdsb ish\n\tisb" ::: "memory"); }
VOID NTAPI HalSweepDcache(VOID) { __asm__ volatile("dsb ish" ::: "memory"); }
VOID NTAPI HalSetGicPriorityMask(KIRQL P) { (VOID)P; }
KIRQL NTAPI HalGetInterruptSource(VOID) { return 0; }
VOID NTAPI HalpGetInterruptTargetInformation(PVOID p) { (VOID)p; }
VOID NTAPI HalpGetMessageRoutingInfo(PVOID p) { (VOID)p; }
NTSTATUS NTAPI IopReserveIrqVectors(ULONG c, ULONG a, PKINTERRUPT *i) { (VOID)c;(VOID)a;(VOID)i; return 0; }

#undef READ_PORT_UCHAR
#undef WRITE_PORT_UCHAR
UCHAR NTAPI READ_PORT_UCHAR(PUCHAR p) { return *(volatile UCHAR*)p; }
VOID NTAPI WRITE_PORT_UCHAR(PUCHAR p, UCHAR v) { *(volatile UCHAR*)p=v; }

VOID NTAPI ScsiPortWritePortUchar(PUCHAR p, UCHAR v) { WRITE_PORT_UCHAR(p,v); }
UCHAR NTAPI ScsiPortReadPortUchar(PUCHAR p) { return READ_PORT_UCHAR(p); }
VOID NTAPI VideoPortWritePortUchar(PUCHAR p, UCHAR v) { WRITE_PORT_UCHAR(p,v); }
UCHAR NTAPI VideoPortReadPortUchar(PUCHAR p) { return READ_PORT_UCHAR(p); }
PHYSICAL_ADDRESS NTAPI ScsiPortConvertUlongToPhysicalAddress(ULONG_PTR a) { PHYSICAL_ADDRESS pa; pa.QuadPart=a; return pa; }
VOID NTAPI VideoPortQuerySystemTime(PLARGE_INTEGER t) { t->QuadPart = 0; }

/* Additional HAL exports */
#undef READ_PORT_USHORT
#undef READ_PORT_ULONG
#undef WRITE_PORT_USHORT
#undef WRITE_PORT_ULONG
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

USHORT NTAPI READ_PORT_USHORT(PUSHORT p) { return *(volatile USHORT*)p; }
ULONG NTAPI READ_PORT_ULONG(PULONG p) { return *(volatile ULONG*)p; }
VOID NTAPI WRITE_PORT_USHORT(PUSHORT p, USHORT v) { *(volatile USHORT*)p=v; }
VOID NTAPI WRITE_PORT_ULONG(PULONG p, ULONG v) { *(volatile ULONG*)p=v; }

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
