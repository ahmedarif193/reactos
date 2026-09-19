#include <ntoskrnl.h>
#include <reactos/cpuaudit.h>

volatile LONG KiCpuAuditActive;
CPU_AUDIT_IRQ KiCpuAuditIrqs[CPU_AUDIT_CPUS][CPU_AUDIT_IRQS];
static volatile LONG AuditBusy;
static ULONG AuditPid;
static HANDLE AuditOwner;
static ULONGLONG AuditFrequency;
static ULONG EventTypes[6] = {0x11, 0x08, 0x03, 0x17, 0x05, 0x24};
typedef struct _AUDIT_CPU {
    volatile ULONG Producer, Consumer;
    ULONG Dropped, Error, Owned;
    ULONGLONG Pmcr, Selector, IntEnable, Types[6], Counters[6], Last[6], LastTick;
    CPU_AUDIT_EVENT Ring[CPU_AUDIT_RING];
} AUDIT_CPU;
static AUDIT_CPU AuditCpus[CPU_AUDIT_CPUS];

ULONGLONG KiCpuAuditClock(VOID)
{
    ULONGLONG Value;
    __asm__ __volatile__("isb; mrs %0, cntpct_el0" : "=r"(Value) :: "memory");
    return Value;
}
static ULONG AuditThreadPid(PKTHREAD Thread)
{
    return HandleToUlong(PsGetProcessId((PEPROCESS)Thread->ApcState.Process));
}
static VOID AuditRecord(CPU_AUDIT_EVENT *Event)
{
    AUDIT_CPU *Cpu = &AuditCpus[Event->Cpu];
    ULONG Producer = Cpu->Producer;
    if (Producer - __atomic_load_n(&Cpu->Consumer, __ATOMIC_ACQUIRE) >= CPU_AUDIT_RING)
    { Cpu->Dropped++; return; }
    Cpu->Ring[Producer % CPU_AUDIT_RING] = *Event;
    __atomic_store_n(&Cpu->Producer, Producer + 1, __ATOMIC_RELEASE);
}
static VOID AuditReadCounters(CPU_AUDIT_EVENT *Event)
{
    AUDIT_CPU *Cpu = &AuditCpus[Event->Cpu];
    ULONG i;
    if (!Cpu->Owned) return;
    for (i = 0; i < 6; ++i)
    {
        ULONGLONG Value;
        __asm__ __volatile__("msr pmselr_el0, %0; isb" :: "r"((ULONGLONG)i) : "memory");
        __asm__ __volatile__("mrs %0, pmxevcntr_el0" : "=r"(Value));
        Event->Values[i] = (ULONG)(Value - Cpu->Last[i]);
        Cpu->Last[i] = Value;
    }
}
static VOID AuditCapture(ULONG Kind, PKTHREAD Old, PKTHREAD New, ULONG_PTR Pc)
{
    CPU_AUDIT_EVENT Event = {0};
    ULONGLONG Daif;
    ULONG Cpu;
    __asm__ __volatile__("mrs %0, daif; msr daifset, #3" : "=r"(Daif) :: "memory");
    Cpu = KeGetCurrentProcessorNumber();
    if (!KiCpuAuditActive || Cpu >= CPU_AUDIT_CPUS) goto Done;
    Event.Tick = KiCpuAuditClock();
    Event.Cpu = Cpu; Event.Kind = Kind; Event.Pc = Pc;
    Event.Tid = (ULONG_PTR)PsGetThreadId((PETHREAD)Old);
    Event.Pid = AuditThreadPid(Old); Event.State = Old->State; Event.Reason = Old->WaitReason;
    if (New) { Event.OtherTid = HandleToUlong(PsGetThreadId((PETHREAD)New)); Event.OtherPid = AuditThreadPid(New); }
    AuditReadCounters(&Event);
    Event.Aux = (ULONG)(KiCpuAuditClock() - Event.Tick);
    AuditCpus[Cpu].LastTick = Event.Tick;
    AuditRecord(&Event);
Done:
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
}
VOID KiCpuAuditTick(ULONG_PTR Pc)
{
    ULONG Cpu;
    if (!KiCpuAuditActive) return;
    Cpu = KeGetCurrentProcessorNumber();
    if (Cpu < CPU_AUDIT_CPUS && KiCpuAuditClock() - AuditCpus[Cpu].LastTick >= AuditFrequency / 100)
        AuditCapture(CPU_AUDIT_SAMPLE, KeGetCurrentThread(), NULL, Pc);
}
VOID KiCpuAuditSwitch(PKTHREAD Old, PKTHREAD New)
{
    if (KiCpuAuditActive) AuditCapture(CPU_AUDIT_SWITCH, Old, New, 0);
}
static VOID AuditReady(PKTHREAD Thread, ULONG Kind, ULONG Processor, KAFFINITY IdleRequest)
{
    CPU_AUDIT_EVENT Event = {0};
    ULONGLONG Daif;
    if (!KiCpuAuditActive || AuditThreadPid(Thread) != AuditPid) return;
    __asm__ __volatile__("mrs %0, daif; msr daifset, #3" : "=r"(Daif) :: "memory");
    Event.Cpu = KeGetCurrentProcessorNumber();
    if (KiCpuAuditActive && Event.Cpu < CPU_AUDIT_CPUS)
    {
        Event.Tick = KiCpuAuditClock(); Event.Kind = Kind;
        Event.Pc = (ULONG_PTR)Thread; Event.State = Thread->NextProcessor;
        Event.Reason = Thread->IdealProcessor; Event.Aux = Processor;
        Event.Values[0] = Thread->Priority; Event.Values[1] = IdleRequest;
        Event.Pid = AuditPid; Event.Tid = (ULONG_PTR)PsGetThreadId((PETHREAD)Thread);
        AuditRecord(&Event);
    }
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
}
VOID KiCpuAuditReady(PKTHREAD Thread)
{
    AuditReady(Thread, CPU_AUDIT_READY, MAXULONG, 0);
}
VOID KiCpuAuditPlace(PKTHREAD Thread, ULONG Processor, KAFFINITY IdleRequest)
{
    AuditReady(Thread, CPU_AUDIT_PLACE, Processor, IdleRequest);
}
VOID KiCpuAuditEvent(ULONG Kind, ULONG_PTR Pc, ULONGLONG A, ULONGLONG B)
{
    CPU_AUDIT_EVENT Event = {0};
    ULONGLONG Daif;
    if (!KiCpuAuditActive) return;
    __asm__ __volatile__("mrs %0, daif; msr daifset, #3" : "=r"(Daif) :: "memory");
    Event.Cpu = KeGetCurrentProcessorNumber();
    if (KiCpuAuditActive && Event.Cpu < CPU_AUDIT_CPUS)
    {
        Event.Tick = KiCpuAuditClock(); Event.Kind = Kind; Event.Pc = Pc;
        Event.Tid = (ULONG_PTR)PsGetThreadId(PsGetCurrentThread());
        Event.Pid = HandleToUlong(PsGetCurrentProcessId());
        if (Kind != CPU_AUDIT_TIMES || Event.Pid == AuditPid)
        {
            Event.Values[0] = A; Event.Values[1] = B;
            AuditRecord(&Event);
        }
    }
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
}
VOID KiCpuAuditProcessTimes(VOID)
{
    CPU_AUDIT_EVENT Event = {0};
    PKTRAP_FRAME Frame;
    ULONG_PTR Fp;
    ULONGLONG Daif;
    ULONG i;
    if (!KiCpuAuditActive || HandleToUlong(PsGetCurrentProcessId()) != AuditPid) return;
    Frame = KeGetCurrentThread()->TrapFrame;
    if (!Frame || ExGetPreviousMode() != UserMode) return;
    Event.Tick = KiCpuAuditClock(); Event.Kind = CPU_AUDIT_TIMES;
    Event.Pid = AuditPid; Event.Tid = (ULONG_PTR)PsGetThreadId(PsGetCurrentThread());
    Event.Pc = Frame->Lr; Fp = Frame->Fp;
    _SEH2_TRY
    {
        for (i = 0; i < 6; ++i)
        {
            ULONG_PTR *Pair = (ULONG_PTR *)Fp, Next;
            ProbeForRead(Pair, 16, 16);
            Next = Pair[0]; Event.Values[i] = Pair[1];
            if (Next <= Fp || Next - Fp > 0x100000) break;
            Fp = Next;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Event.Aux = 1; }
    _SEH2_END;
    __asm__ __volatile__("mrs %0, daif; msr daifset, #3" : "=r"(Daif) :: "memory");
    Event.Cpu = KeGetCurrentProcessorNumber();
    if (KiCpuAuditActive && Event.Cpu < CPU_AUDIT_CPUS) AuditRecord(&Event);
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
}
static ULONG_PTR NTAPI AuditPmu(ULONG_PTR Operation)
{
    ULONG Number = KeGetCurrentProcessorNumber(), i;
    AUDIT_CPU *Cpu;
    ULONGLONG Enabled, Pmcr, Pmceid, Pmceid1, Dfr;
    CPU_AUDIT_EVENT Event = {0};
    if (Number >= CPU_AUDIT_CPUS) return 0;
    Cpu = &AuditCpus[Number];
    if (Operation == CPU_AUDIT_START)
    {
        __asm__ __volatile__("mrs %0, id_aa64dfr0_el1" : "=r"(Dfr));
        if (((Dfr >> 8) & 15) == 0 || ((Dfr >> 8) & 15) == 15) { Cpu->Error = 1; return 0; }
        __asm__ __volatile__("mrs %0, pmcr_el0" : "=r"(Pmcr));
        __asm__ __volatile__("mrs %0, pmcntenset_el0" : "=r"(Enabled));
        __asm__ __volatile__("mrs %0, pmceid0_el0" : "=r"(Pmceid));
        __asm__ __volatile__("mrs %0, pmceid1_el0" : "=r"(Pmceid1));
        Pmceid = (ULONG)Pmceid | ((ULONGLONG)(ULONG)Pmceid1 << 32);
        if (((Pmcr >> 11) & 31) < 6 || (Enabled & 0x7fffffff)) { Cpu->Error = 2; return 0; }
        Cpu->Pmcr = Pmcr;
        __asm__ __volatile__("mrs %0, pmselr_el0" : "=r"(Cpu->Selector));
        __asm__ __volatile__("mrs %0, pmintenset_el1" : "=r"(Cpu->IntEnable));
        __asm__ __volatile__("msr pmintenclr_el1, %0" :: "r"(63ULL) : "memory");
        for (i = 0; i < 6; ++i)
        {
            ULONGLONG Type = EventTypes[i] | (i ? (1ULL << 31) : 0);
            if (!(Pmceid & (1ULL << EventTypes[i]))) Cpu->Error |= 1UL << (8 + i);
            __asm__ __volatile__("msr pmselr_el0, %0; isb" :: "r"((ULONGLONG)i) : "memory");
            __asm__ __volatile__("mrs %0, pmxevtyper_el0" : "=r"(Cpu->Types[i]));
            __asm__ __volatile__("mrs %0, pmxevcntr_el0" : "=r"(Cpu->Counters[i]));
            __asm__ __volatile__("msr pmxevtyper_el0, %0; msr pmxevcntr_el0, xzr" :: "r"(Type) : "memory");
            Cpu->Last[i] = 0;
        }
        __asm__ __volatile__("msr pmovsclr_el0, %0; msr pmcntenset_el0, %0" :: "r"(63ULL) : "memory");
        Pmcr = (Pmcr & ~6ULL) | 1;
        __asm__ __volatile__("msr pmcr_el0, %0; isb" :: "r"(Pmcr) : "memory");
        Cpu->Owned = 1;
        Cpu->LastTick = KiCpuAuditClock();
        Event.Tick = Cpu->LastTick; Event.Kind = CPU_AUDIT_CONFIG; Event.Cpu = Number;
        Event.Values[0] = Pmcr; Event.Values[1] = Enabled; Event.Values[2] = Pmceid;
        Event.Values[3] = EventTypes[5];
        Event.Aux = Cpu->Error;
        AuditRecord(&Event);
    }
    else if (Cpu->Owned)
    {
        Event.Tick = KiCpuAuditClock(); Event.Kind = CPU_AUDIT_SAMPLE; Event.Cpu = Number;
        Event.Pid = HandleToUlong(PsGetCurrentProcessId()); Event.Tid = (ULONG_PTR)PsGetThreadId(PsGetCurrentThread());
        AuditReadCounters(&Event); AuditRecord(&Event);
        __asm__ __volatile__("msr pmcntenclr_el0, %0; isb" :: "r"(63ULL) : "memory");
        for (i = 0; i < 6; ++i)
        {
            __asm__ __volatile__("msr pmselr_el0, %0; isb" :: "r"((ULONGLONG)i) : "memory");
            __asm__ __volatile__("msr pmxevtyper_el0, %0" :: "r"(Cpu->Types[i]) : "memory");
            __asm__ __volatile__("msr pmxevcntr_el0, %0" :: "r"(Cpu->Counters[i]) : "memory");
        }
        __asm__ __volatile__("msr pmovsclr_el0, %0; msr pmintenclr_el1, %0" :: "r"(63ULL) : "memory");
        __asm__ __volatile__("msr pmintenset_el1, %0" :: "r"(Cpu->IntEnable & 63) : "memory");
        __asm__ __volatile__("msr pmselr_el0, %0; msr pmcr_el0, %1; isb" :: "r"(Cpu->Selector), "r"(Cpu->Pmcr & ~6ULL) : "memory");
        Cpu->Owned = 0;
    }
    return 0;
}
NTSTATUS KiCpuAuditControl(PVOID Buffer, ULONG Length, KPROCESSOR_MODE Mode)
{
    CPU_AUDIT_PACKET *Request = Buffer, *Packet;
    ULONG Command, Number, Limit, Consumer, Producer;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Held = FALSE;
    if (Length != sizeof(*Request)) return STATUS_INFO_LENGTH_MISMATCH;
    if (!SeSinglePrivilegeCheck(SeDebugPrivilege, Mode)) return STATUS_PRIVILEGE_NOT_HELD;
    Packet = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Packet), 'aUpC');
    if (!Packet) return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY
    {
        if (Mode != KernelMode) ProbeForWrite(Buffer, Length, sizeof(ULONG));
        RtlCopyMemory(Packet, Request, sizeof(*Packet));
        if (Packet->Version != CPU_AUDIT_VERSION) { Status = STATUS_REVISION_MISMATCH; _SEH2_LEAVE; }
        if (InterlockedCompareExchange(&AuditBusy, 1, 0)) { Status = STATUS_DEVICE_BUSY; _SEH2_LEAVE; }
        Held = TRUE;
        Command = Packet->Command;
        if (Command == CPU_AUDIT_START)
        {
            if (KiCpuAuditActive || (ULONG)KeNumberProcessors != CPU_AUDIT_CPUS)
                Status = STATUS_DEVICE_BUSY;
            else
            {
                RtlZeroMemory(AuditCpus, sizeof(AuditCpus));
                AuditPid = Packet->Pid; AuditOwner = PsGetCurrentProcessId();
                __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(AuditFrequency));
                KeIpiGenericCall(AuditPmu, CPU_AUDIT_START);
                InterlockedExchange(&KiCpuAuditActive, 1);
            }
        }
        else if (AuditOwner != PsGetCurrentProcessId()) Status = STATUS_ACCESS_DENIED;
        else if (Command == CPU_AUDIT_STOP)
        {
            InterlockedExchange(&KiCpuAuditActive, 0);
            KeIpiGenericCall(AuditPmu, CPU_AUDIT_STOP);
        }
        else if (Command != CPU_AUDIT_DRAIN && Command != CPU_AUDIT_INTERRUPTS)
            Status = STATUS_INVALID_PARAMETER;
        Packet->Count = Packet->Dropped = Packet->Errors = 0;
        Packet->Active = KiCpuAuditActive; Packet->Frequency = AuditFrequency;
        if (NT_SUCCESS(Status) && Command == CPU_AUDIT_INTERRUPTS)
        {
            if (Packet->Cpu >= CPU_AUDIT_CPUS) Status = STATUS_INVALID_PARAMETER;
            else { RtlCopyMemory(Packet->Data.Irqs, KiCpuAuditIrqs[Packet->Cpu], sizeof(Packet->Data.Irqs)); Packet->Count = CPU_AUDIT_IRQS; }
        }
        else if (NT_SUCCESS(Status))
        {
            for (Number = 0; Number < CPU_AUDIT_CPUS; ++Number)
            {
                AUDIT_CPU *Cpu = &AuditCpus[Number];
                Packet->Dropped += Cpu->Dropped; Packet->Errors |= Cpu->Error;
                Consumer = Cpu->Consumer;
                Producer = __atomic_load_n(&Cpu->Producer, __ATOMIC_ACQUIRE);
                Limit = min(Producer - Consumer, CPU_AUDIT_EVENTS - Packet->Count);
                while (Limit--) Packet->Data.Events[Packet->Count++] = Cpu->Ring[Consumer++ % CPU_AUDIT_RING];
                __atomic_store_n(&Cpu->Consumer, Consumer, __ATOMIC_RELEASE);
            }
        }
        InterlockedExchange(&AuditBusy, 0); Held = FALSE;
        RtlCopyMemory(Request, Packet, sizeof(*Packet));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Status = _SEH2_GetExceptionCode(); }
    _SEH2_END;
    if (Held) InterlockedExchange(&AuditBusy, 0);
    ExFreePoolWithTag(Packet, 'aUpC');
    return Status;
}
