/*
 * PROJECT:     ReactOS Kernel
 * PURPOSE:     SMP boot and runtime diagnostics.
 *
 * All output is gated at runtime by SmpDbgEnabled, set when the /SMPDIAG boot
 * option is present.
 */

#ifndef _REACTOS_SMPDBG_H
#define _REACTOS_SMPDBG_H

#define SMPDBG_MAXCPU 8
#define SMPDBG_SOURCE_UNKNOWN ((ULONG)-1)

#define SMPDBG_SCHED_REPLACE_STANDBY 1
#define SMPDBG_SCHED_PREEMPT_CURRENT 2
#define SMPDBG_SCHED_IDLE_REQUEST    3
#define SMPDBG_SCHED_PRIORITY        4
#define SMPDBG_SCHED_AFFINITY        5
#define SMPDBG_SCHED_RESCHEDULE      6
#define SMPDBG_SCHED_STANDBY_REPAIR  7

#define SMPDBG_BALANCE_WAKE_PLACEMENT 1
#define SMPDBG_BALANCE_IDLE           2
#define SMPDBG_BALANCE_QUANTUM        3
#define SMPDBG_BALANCE_PERIODIC       4

#ifdef __cplusplus
extern "C" {
#endif

extern BOOLEAN SmpDbgEnabled; /* set by the /SMPDIAG boot option */

#ifdef _WIN64

/* Scheduler, DPC and IPI recorders and the reporter/watchdog (ke/smpdbg.c). */
VOID NTAPI SmpDbgRuntimeTick(ULONG Cpu);
VOID NTAPI SmpDbgQuantumRequest(ULONG Cpu);
VOID NTAPI SmpDbgDispatchInterrupt(ULONG Cpu);
VOID NTAPI SmpDbgQuantumEnd(ULONG Cpu);
VOID NTAPI SmpDbgIpi(ULONG Cpu);
VOID NTAPI SmpDbgRemoteDpc(ULONG Cpu, ULONG SourceCpu);
VOID NTAPI SmpDbgSchedulerIpi(ULONG Cpu, PVOID Thread, ULONG Cause);
VOID NTAPI SmpDbgQueuedDpcIpi(ULONG Cpu);
VOID NTAPI SmpDbgTbFlushIpi(ULONG Cpu);
VOID NTAPI SmpDbgGenericCallIpi(ULONG Cpu);
VOID NTAPI SmpDbgBalanceEvent(ULONG TargetCpu, ULONG SourceCpu, ULONG Reason);
VOID NTAPI SmpDbgStandbySteal(ULONG TargetCpu);
VOID NTAPI SmpDbgCycleCharge(ULONG Cpu, ULONG64 Cycles, BOOLEAN Reset);
VOID NTAPI SmpDbgStartWatchdog(VOID);

#else

/* The recorders are only built for 64-bit kernels. */
FORCEINLINE VOID SmpDbgRuntimeTick(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgQuantumRequest(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgDispatchInterrupt(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgQuantumEnd(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgIpi(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgRemoteDpc(ULONG Cpu, ULONG SourceCpu)
{ UNREFERENCED_PARAMETER(Cpu); UNREFERENCED_PARAMETER(SourceCpu); }
FORCEINLINE VOID SmpDbgSchedulerIpi(ULONG Cpu, PVOID Thread, ULONG Cause)
{ UNREFERENCED_PARAMETER(Cpu); UNREFERENCED_PARAMETER(Thread); UNREFERENCED_PARAMETER(Cause); }
FORCEINLINE VOID SmpDbgQueuedDpcIpi(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgTbFlushIpi(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgGenericCallIpi(ULONG Cpu) { UNREFERENCED_PARAMETER(Cpu); }
FORCEINLINE VOID SmpDbgBalanceEvent(ULONG TargetCpu, ULONG SourceCpu, ULONG Reason)
{ UNREFERENCED_PARAMETER(TargetCpu); UNREFERENCED_PARAMETER(SourceCpu); UNREFERENCED_PARAMETER(Reason); }
FORCEINLINE VOID SmpDbgStandbySteal(ULONG TargetCpu) { UNREFERENCED_PARAMETER(TargetCpu); }
FORCEINLINE VOID SmpDbgCycleCharge(ULONG Cpu, ULONG64 Cycles, BOOLEAN Reset)
{ UNREFERENCED_PARAMETER(Cpu); UNREFERENCED_PARAMETER(Cycles); UNREFERENCED_PARAMETER(Reset); }
FORCEINLINE VOID SmpDbgStartWatchdog(VOID) {}

#endif /* _WIN64 */

#if defined(_M_ARM64)

/* Generic timer and GIC recorders (ke/arm64/smpdbg.c), also used by the HAL. */
VOID NTAPI SmpDbgTimerBegin(ULONG Cpu, ULONG IntId);
VOID NTAPI SmpDbgTimerEoi(ULONG Cpu, ULONG IntId);
VOID NTAPI SmpDbgTimerReject(ULONG Cpu, ULONG IntId);
VOID NTAPI SmpDbgTimerTick(ULONG Cpu);
VOID NTAPI SmpDbgPark(ULONG Cpu);
VOID NTAPI SmpDbgWake(ULONG Cpu);
VOID NTAPI SmpDbgHeartbeat(ULONG Cpu);
VOID NTAPI SmpDbgCntv(ULONG Cpu, ULONG Ctl, LONG Tval, ULONG Pmr);
VOID NTAPI SmpDbgGic(ULONG Cpu, ULONG Prio, ULONG En, ULONG Pend, ULONG Act);
ULONG NTAPI SmpDbgGetTick(ULONG Cpu);
VOID NTAPI SmpDbgDumpTimers(ULONG StrandMask);

/* HAL reader (exported from hal): live GIC priority/enable/pending/active. */
VOID NTAPI HalArm64DbgGicState(ULONG IntId, ULONG *Prio, ULONG *Enable, ULONG *Pending, ULONG *Active);

#define SMPDBG_TIMER_BEGIN(c, i)  SmpDbgTimerBegin((c), (i))
#define SMPDBG_TIMER_EOI(c, i)    SmpDbgTimerEoi((c), (i))
#define SMPDBG_TIMER_REJECT(c, i) SmpDbgTimerReject((c), (i))

#else

#define SMPDBG_TIMER_BEGIN(c, i)
#define SMPDBG_TIMER_EOI(c, i)
#define SMPDBG_TIMER_REJECT(c, i)

#endif

#ifdef __cplusplus
}
#endif

#endif /* _REACTOS_SMPDBG_H */
