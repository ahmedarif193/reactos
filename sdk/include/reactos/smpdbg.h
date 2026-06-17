/*
 * PROJECT:     ReactOS Kernel
 * PURPOSE:     ARM64 SMP boot diagnostics (smpdbg).
 *
 * Per-CPU counters/state live in ke/arm64/smpdbg.c and are recorded from both
 * ntoskrnl and the HAL (which imports the recorder exports). All output is gated
 * at runtime by SmpDbgEnabled, set when the /SMPDIAG boot option is present.
 */

#ifndef _REACTOS_SMPDBG_H
#define _REACTOS_SMPDBG_H

#define SMPDBG_MAXCPU 8

#if defined(_M_ARM64)

#ifdef __cplusplus
extern "C" {
#endif

extern BOOLEAN SmpDbgEnabled; /* set by the /SMPDIAG boot option */

/* HAL-side recorders (exported from ntoskrnl, imported by the HAL). */
VOID NTAPI SmpDbgTimerBegin(ULONG Cpu, ULONG IntId);
VOID NTAPI SmpDbgTimerEoi(ULONG Cpu, ULONG IntId);
VOID NTAPI SmpDbgTimerReject(ULONG Cpu, ULONG IntId);

/* Kernel-side recorders / dump. */
VOID NTAPI SmpDbgTimerTick(ULONG Cpu);
VOID NTAPI SmpDbgCntv(ULONG Cpu, ULONG Ctl, LONG Tval, ULONG Pmr);
VOID NTAPI SmpDbgGic(ULONG Cpu, ULONG Prio, ULONG En, ULONG Pend, ULONG Act);
ULONG NTAPI SmpDbgGetTick(ULONG Cpu);
VOID NTAPI SmpDbgDumpTimers(ULONG StrandMask);

/* HAL reader (exported from hal): live GIC priority/enable/pending/active. */
VOID NTAPI HalArm64DbgGicState(ULONG IntId, ULONG *Prio, ULONG *Enable, ULONG *Pending, ULONG *Active);

#ifdef __cplusplus
}
#endif

#define SMPDBG_TIMER_BEGIN(c, i)  SmpDbgTimerBegin((c), (i))
#define SMPDBG_TIMER_EOI(c, i)    SmpDbgTimerEoi((c), (i))
#define SMPDBG_TIMER_REJECT(c, i) SmpDbgTimerReject((c), (i))

#else /* !_M_ARM64 */

#define SMPDBG_TIMER_BEGIN(c, i)
#define SMPDBG_TIMER_EOI(c, i)
#define SMPDBG_TIMER_REJECT(c, i)

#endif

#endif /* _REACTOS_SMPDBG_H */
