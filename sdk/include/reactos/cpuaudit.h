#pragma once
/* Temporary opt-in Pi 5 capture ABI; not an NT system-information class. */
#define CPU_AUDIT_CLASS 0x43505541u
#define CPU_AUDIT_VERSION 2
#define CPU_AUDIT_CPUS 4
#define CPU_AUDIT_EVENTS 512
#define CPU_AUDIT_IRQS 1024
#define CPU_AUDIT_RING 8192
#define CPU_AUDIT_START 0
#define CPU_AUDIT_DRAIN 1
#define CPU_AUDIT_STOP 2
#define CPU_AUDIT_INTERRUPTS 3
#define CPU_AUDIT_SAMPLE 1
#define CPU_AUDIT_SWITCH 2
#define CPU_AUDIT_READY 3
#define CPU_AUDIT_ASID 4
#define CPU_AUDIT_TLB 5
#define CPU_AUDIT_TIMES 6
#define CPU_AUDIT_CONFIG 7
#define CPU_AUDIT_MARK 8
#define CPU_AUDIT_PLACE 9

typedef struct _CPU_AUDIT_EVENT {
    ULONGLONG Tick, Pc, Tid;
    ULONG Kind, Cpu, Pid, OtherPid, OtherTid, State, Reason, Aux;
    ULONGLONG Values[6];
} CPU_AUDIT_EVENT;
typedef struct _CPU_AUDIT_IRQ {
    ULONGLONG Count, DispatchTicks, EnvelopeTicks, MaximumTicks, HandlerCalls;
} CPU_AUDIT_IRQ;
typedef struct _CPU_AUDIT_PACKET {
    ULONG Version, Command, Pid, Cpu, Count, Dropped, Errors, Active;
    ULONGLONG Frequency;
    union { CPU_AUDIT_EVENT Events[CPU_AUDIT_EVENTS]; CPU_AUDIT_IRQ Irqs[CPU_AUDIT_IRQS]; } Data;
} CPU_AUDIT_PACKET;
#ifdef _NTOSKRNL_
extern volatile LONG KiCpuAuditActive;
extern CPU_AUDIT_IRQ KiCpuAuditIrqs[CPU_AUDIT_CPUS][CPU_AUDIT_IRQS];
ULONGLONG KiCpuAuditClock(VOID);
VOID KiCpuAuditTick(ULONG_PTR Pc);
VOID KiCpuAuditProcessTimes(VOID);
VOID KiCpuAuditSwitch(PKTHREAD Old, PKTHREAD New);
VOID KiCpuAuditReady(PKTHREAD Thread);
VOID KiCpuAuditPlace(PKTHREAD Thread, ULONG Processor, KAFFINITY IdleRequest);
VOID KiCpuAuditEvent(ULONG Kind, ULONG_PTR Pc, ULONGLONG A, ULONGLONG B);
NTSTATUS KiCpuAuditControl(PVOID Buffer, ULONG Length, KPROCESSOR_MODE Mode);
#endif
