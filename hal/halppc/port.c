/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PReP port I/O through the loader's I/O window
 *
 * The PReP I/O space (ISA and PCI ports) appears at physical 0x80000000; the
 * loader maps it cache-inhibited and guarded at IsaIoVirtualBase. The
 * processor runs little-endian, so port values need no byte swapping.
 */

/* Export real functions; do not select an architecture's inline macros. */
#define NO_PORT_MACROS
#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

#define HALP_PPC_PORT(Type, Port) ((volatile Type *)(HalpPpcIoBase + ((ULONG_PTR)(Port) & 0xFFFFFF)))

static __inline__ VOID HalpPpcIoBarrier(VOID) { __asm__ __volatile__("eieio" ::: "memory"); }

UCHAR NTAPI READ_PORT_UCHAR(PUCHAR Port) { UCHAR Value = *HALP_PPC_PORT(UCHAR, Port); HalpPpcIoBarrier(); return Value; }
USHORT NTAPI READ_PORT_USHORT(PUSHORT Port) { USHORT Value = *HALP_PPC_PORT(USHORT, Port); HalpPpcIoBarrier(); return Value; }
ULONG NTAPI READ_PORT_ULONG(PULONG Port) { ULONG Value = *HALP_PPC_PORT(ULONG, Port); HalpPpcIoBarrier(); return Value; }
VOID NTAPI WRITE_PORT_UCHAR(PUCHAR Port, UCHAR Value) { *HALP_PPC_PORT(UCHAR, Port) = Value; HalpPpcIoBarrier(); }
VOID NTAPI WRITE_PORT_USHORT(PUSHORT Port, USHORT Value) { *HALP_PPC_PORT(USHORT, Port) = Value; HalpPpcIoBarrier(); }
VOID NTAPI WRITE_PORT_ULONG(PULONG Port, ULONG Value) { *HALP_PPC_PORT(ULONG, Port) = Value; HalpPpcIoBarrier(); }

VOID NTAPI READ_PORT_BUFFER_UCHAR(PUCHAR Port, PUCHAR Buffer, ULONG Count) { while (Count--) *Buffer++ = READ_PORT_UCHAR(Port); }
VOID NTAPI READ_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count) { while (Count--) *Buffer++ = READ_PORT_USHORT(Port); }
VOID NTAPI READ_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count) { while (Count--) *Buffer++ = READ_PORT_ULONG(Port); }
VOID NTAPI WRITE_PORT_BUFFER_UCHAR(PUCHAR Port, PUCHAR Buffer, ULONG Count) { while (Count--) WRITE_PORT_UCHAR(Port, *Buffer++); }
VOID NTAPI WRITE_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count) { while (Count--) WRITE_PORT_USHORT(Port, *Buffer++); }
VOID NTAPI WRITE_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count) { while (Count--) WRITE_PORT_ULONG(Port, *Buffer++); }
