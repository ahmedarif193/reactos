/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndissys.h
 * PURPOSE:     NDIS library definitions
 * NOTES:       Spin lock acquire order:
 *                - Miniport list lock
 *                - Adapter list lock
 */
#ifndef __NDISSYS_H
#define __NDISSYS_H

#include <ndis.h>

#include "debug.h"
#include "miniport.h"
#include "protocol.h"
#include "efilter.h"
#include "buffer.h"

/* Exported functions */
#ifndef EXPORT
#define EXPORT NTAPI
#endif

/* the version of NDIS we claim to be.
 *
 * dev-nt6-1: bumped from 0x00050001 (NDIS 5.1) to 0x00060014 (NDIS 6.20)
 * for the NT 6.1 (Windows 7) target. The encoding is
 *   ((Major << 16) | (Minor & 0xFFFF))
 * so 0x0006_0014 = NDIS 6.20.
 *
 * NdisGetVersion() returns this value, and NdisReadConfiguration with the
 * "NdisVersion" key reports it back to legacy NDIS 5 protocols. Legacy
 * protocols that check NdisGetVersion() expect a value >= some minimum;
 * 6.20 satisfies all known checks. */
#define NDIS_VERSION 0x00060014

#define NDIS_TAG 'SIDN' // "NDIS"

#define MIN(value1, value2) \
    ((value1 < value2)? value1 : value2)

#define MAX(value1, value2) \
    ((value1 > value2)? value1 : value2)

#define RTL_CONSTANT_LARGE_INTEGER(quad_part) {{(quad_part), (quad_part) >> 32}}

#define ExInterlockedRemoveEntryList(_List,_Lock) \
 { KIRQL OldIrql; \
   KeAcquireSpinLock(_Lock, &OldIrql); \
   RemoveEntryList(_List); \
   KeReleaseSpinLock(_Lock, OldIrql); \
 }

/* missing protypes */
VOID
NTAPI
ExGetCurrentProcessorCounts(
   PULONG IdleTime,
   PULONG KernelAndUserTime,
   PULONG ProcessorNumber);

VOID
NTAPI
ExGetCurrentProcessorCpuUsage(
    PULONG CpuUsage);

/* portability fixes */
#ifdef _M_AMD64
#define KfReleaseSpinLock KeReleaseSpinLock
#define KefAcquireSpinLockAtDpcLevel KeAcquireSpinLockAtDpcLevel
#define KefReleaseSpinLockFromDpcLevel KeReleaseSpinLockFromDpcLevel
#endif

#endif /* __NDISSYS_H */
