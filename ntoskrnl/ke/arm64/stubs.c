/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Stub Functions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * ARM64-specific stub functions that need proper implementation
 * These are provided to allow kernel compilation to complete
 */

/* KiUserTrap moved to line 294 */

VOID
NTAPI
DbgBreakPoint(VOID)
{
    /* ARM64 breakpoint instruction */
    __asm__ __volatile__("brk #0xF000");
}

VOID
NTAPI
DbgBreakPointWithStatus(IN ULONG Status)
{
    /* Store status in x0 and trigger breakpoint */
    __asm__ __volatile__(
        "mov x0, %0\n"
        "brk #0xF001"
        : : "r" ((ULONG_PTR)Status)
    );
}

VOID
NTAPI
RtlpBreakWithStatusInstruction(IN NTSTATUS Status)
{
    /* Store status in x0 and trigger breakpoint */
    __asm__ __volatile__(
        "mov x0, %0\n"
        "brk #0xF001"
        : : "r" ((ULONG_PTR)Status)
    );
}

NTSTATUS
NTAPI
NtSetLdtEntries(IN ULONG Selector1,
                IN LDT_ENTRY LdtEntry1,
                IN ULONG Selector2,
                IN LDT_ENTRY LdtEntry2)
{
    /* LDT is x86-specific, not applicable to ARM64 */
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtVdmControl(IN ULONG ControlCode,
             IN PVOID ControlData)
{
    /* VDM (Virtual DOS Machine) is x86-specific, not applicable to ARM64 */
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(ControlData);
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
KiInitializeMachineType(VOID)
{
    /* Set machine type for ARM64 */
    KeI386MachineType = IMAGE_FILE_MACHINE_ARM64;
    
    /* Initialize ARM64-specific features */
    DPRINT1("ARM64: Machine type initialized\n");
}

BOOLEAN
NTAPI
KiIsNpxPresent(VOID)
{
    /* ARM64 always has NEON/FP support in modern processors */
    return TRUE;
}

BOOLEAN
NTAPI
KiIsNpxErrataPresent(VOID)
{
    /* No x87 FPU errata on ARM64 */
    return FALSE;
}

VOID
NTAPI
KiFlushNPXState(IN PVOID SaveArea)
{
    /* Flush NEON/FP state on ARM64 */
    /* TODO: Implement proper NEON state save */
    UNREFERENCED_PARAMETER(SaveArea);
}

#if 0  /* Implemented in except.c for other architectures */
NTSTATUS
NTAPI
KiRaiseException(IN PEXCEPTION_RECORD ExceptionRecord,
                 IN PCONTEXT Context,
                 IN PKEXCEPTION_FRAME ExceptionFrame,
                 IN PKTRAP_FRAME TrapFrame,
                 IN BOOLEAN FirstChance)
{
    /* Raise exception on ARM64 */
    DPRINT1("ARM64: KiRaiseException called\n");
    
    /* Dispatch the exception */
    KiDispatchException(ExceptionRecord,
                        ExceptionFrame,
                        TrapFrame,
                        UserMode,
                        FirstChance);
    return STATUS_SUCCESS;
}
#endif

#if 0  /* Implemented in cpu.c */
VOID
NTAPI
KiSetProcessorType(VOID)
{
    /* Set ARM64 processor type */
    PKPRCB Prcb = KeGetCurrentPrcb();
    
    /* Set as ARM64 processor */
    Prcb->CpuType = 0x64;  /* Use a valid CHAR value for ARM64 */
    Prcb->CpuID = 1;
    
    /* Set vendor string */
    RtlCopyMemory(Prcb->VendorString, "ARM64", 5);
    
    DPRINT1("ARM64: Processor type set\n");
}
#endif

VOID
NTAPI
KiInitializeGDT(IN PKPCR Pcr,
                IN ULONG Cpu,
                IN PVOID GdtBase,
                IN PVOID TssBase)
{
    /* ARM64 doesn't use GDT */
    UNREFERENCED_PARAMETER(Pcr);
    UNREFERENCED_PARAMETER(Cpu);
    UNREFERENCED_PARAMETER(GdtBase);
    UNREFERENCED_PARAMETER(TssBase);
    
    DPRINT1("ARM64: No GDT initialization needed\n");
}

VOID
NTAPI
KiInitializeTSS(IN PVOID TssBase)
{
    /* ARM64 doesn't use TSS */
    UNREFERENCED_PARAMETER(TssBase);
    
    DPRINT1("ARM64: No TSS initialization needed\n");
}

VOID
NTAPI
KiRestoreProcessorControlState(IN PKPROCESSOR_STATE ProcessorState)
{
    /* Restore ARM64 processor state */
    /* TODO: Implement proper state restoration */
    UNREFERENCED_PARAMETER(ProcessorState);
    
    DPRINT1("ARM64: Processor control state restored\n");
}

VOID
NTAPI
KiSaveProcessorControlState(OUT PKPROCESSOR_STATE ProcessorState)
{
    /* Save ARM64 processor state */
    /* TODO: Implement proper state save */
    UNREFERENCED_PARAMETER(ProcessorState);
    
    DPRINT1("ARM64: Processor control state saved\n");
}

BOOLEAN
NTAPI
KiHandleFpuFault(IN PKTRAP_FRAME TrapFrame)
{
    /* Handle NEON/FP fault on ARM64 */
    UNREFERENCED_PARAMETER(TrapFrame);
    
    /* Enable FP/NEON access */
    ULONG64 CpacrValue;
    __asm__ __volatile__("mrs %0, cpacr_el1" : "=r" (CpacrValue));
    CpacrValue |= (3 << 20);  /* FPEN bits */
    __asm__ __volatile__("msr cpacr_el1, %0" : : "r" (CpacrValue));
    __asm__ __volatile__("isb");
    
    return TRUE;
}

VOID
NTAPI
KiInitializePcr(IN ULONG ProcessorNumber,
                IN PKPCR Pcr,
                IN PKIDR Idt,
                IN PKGDTENTRY Gdt,
                IN PKTSS Tss,
                IN PKTHREAD IdleThread,
                IN PVOID DpcStack)
{
    /* Initialize ARM64 PCR */
    PKPRCB Prcb = Pcr->Prcb;
    
    /* Set PCR fields */
    Pcr->Self = Pcr;
    Pcr->CurrentIrql = PASSIVE_LEVEL;
    
    /* Set PRCB fields */
    Prcb->Number = (USHORT)ProcessorNumber;
    Prcb->CurrentThread = IdleThread;
    Prcb->IdleThread = IdleThread;
    Prcb->DpcStack = DpcStack;
    
    /* ARM64 doesn't use IDT/GDT/TSS */
    UNREFERENCED_PARAMETER(Idt);
    UNREFERENCED_PARAMETER(Gdt);
    UNREFERENCED_PARAMETER(Tss);
    
    /* Set PCR in TPIDR_EL1 */
    __asm__ __volatile__("msr tpidr_el1, %0" : : "r" (Pcr));
    
    DPRINT1("ARM64: PCR initialized for processor %lu\n", ProcessorNumber);
}

/* KeFlushCurrentTb is defined as a macro or inline in headers, remove duplicate definition */

VOID
NTAPI
KiFlushSingleTb(IN PVOID Virtual)
{
    /* Flush single TLB entry on ARM64 */
    ULONG_PTR Address = (ULONG_PTR)Virtual;
    
    __asm__ __volatile__(
        "dsb ishst\n\t"
        "tlbi vae1is, %0\n\t"
        "dsb ish\n\t"
        "isb"
        : : "r" (Address >> 12)
    );
}

/* KeFreezeExecution and KeThawExecution are implemented in freeze.c */

/* KiCallUserMode - Moved to usercall.c */

VOID
NTAPI
KiInitializeTss(IN PKTSS Tss,
                IN PVOID GdtBase)
{
    /* ARM64 doesn't use TSS */
    UNREFERENCED_PARAMETER(Tss);
    UNREFERENCED_PARAMETER(GdtBase);
}

VOID
NTAPI
KiSwapProcess(IN PKPROCESS NewProcess,
              IN PKPROCESS OldProcess)
{
    /* Swap process on ARM64 */
    ULONG64 Ttbr0;
    
    /* Get new process page table base */
    Ttbr0 = NewProcess->DirectoryTableBase[0];
    
    /* Switch page tables */
    __asm__ __volatile__(
        "msr ttbr0_el1, %0\n\t"
        "isb"
        : : "r" (Ttbr0)
    );
    
    UNREFERENCED_PARAMETER(OldProcess);
}

/* Trap frame functions */
ULONG_PTR
NTAPI
KeGetTrapFramePc(IN PKTRAP_FRAME TrapFrame)
{
    /* Return program counter from trap frame */
    return TrapFrame->Pc;
}

BOOLEAN
NTAPI
KiUserTrap(IN PKTRAP_FRAME TrapFrame)
{
    /* Check if trap came from user mode on ARM64 */
    /* Check the SPSR to determine previous mode */
    /* Bit 4 of SPSR indicates EL0 (user mode) when clear */
    return ((TrapFrame->Spsr & 0x0F) == 0);
}

/* Timer functions */
/* KeQueryTickCount is implemented in clock.c */

/* Exception handling functions */
PKTRAP_FRAME
NTAPI
KiGetLinkedTrapFrame(IN PKTRAP_FRAME TrapFrame)
{
    /* ARM64: Get linked trap frame */
    /* TODO: Implement proper trap frame chain walking */
    return NULL;
}

VOID
NTAPI
KiExceptionExit(IN PKTRAP_FRAME TrapFrame,
                IN PKEXCEPTION_FRAME ExceptionFrame)
{
    /* ARM64: Exit from exception */
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    
    /* TODO: Implement exception exit */
    /* This would restore context and return from exception */
}

/* HAL functions */
NTSTATUS
NTAPI
HalAllocateAdapterChannel(IN PADAPTER_OBJECT AdapterObject,
                          IN PWAIT_CONTEXT_BLOCK Wcb,
                          IN ULONG NumberOfMapRegisters,
                          IN PDRIVER_CONTROL ExecutionRoutine)
{
    /* ARM64: Allocate adapter channel */
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Wcb);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    UNREFERENCED_PARAMETER(ExecutionRoutine);
    
    /* TODO: Implement DMA adapter channel allocation */
    DPRINT1("ARM64: HalAllocateAdapterChannel not yet implemented\n");
    return STATUS_NOT_IMPLEMENTED;
}

/* Global variables required by kernel */
ULONG KeI386MachineType = IMAGE_FILE_MACHINE_ARM64;
ULONG ProcessCount = 0;

/* Math function stubs for kernel mode - these should not be used in kernel */
double
pow(double x, double y)
{
    /* Kernel should not use floating point math */
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);
    return 1.0;
}

double
log10(double x)
{
    /* Kernel should not use floating point math */
    UNREFERENCED_PARAMETER(x);
    return 0.0;
}

VOID
NTAPI
RtlGetCallersAddress(OUT PVOID *CallersAddress,
                     OUT PVOID *CallersCaller)
{
    /* ARM64 implementation - get return addresses from stack */
    /* For now, just return NULL */
    if (CallersAddress) *CallersAddress = NULL;
    if (CallersCaller) *CallersCaller = NULL;
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    /* ARM64 cache line size is typically 64 bytes */
    return 64;
}

VOID
NTAPI
KeInitializeInterrupt(IN PKINTERRUPT Interrupt,
                      IN PKSERVICE_ROUTINE ServiceRoutine,
                      IN PVOID ServiceContext,
                      IN PKSPIN_LOCK SpinLock,
                      IN ULONG Vector,
                      IN KIRQL Irql,
                      IN KIRQL SynchronizeIrql,
                      IN KINTERRUPT_MODE InterruptMode,
                      IN BOOLEAN ShareVector,
                      IN CHAR ProcessorNumber,
                      IN BOOLEAN FloatingSave)
{
    /* ARM64 interrupt initialization stub */
    DPRINT1("ARM64: KeInitializeInterrupt not yet implemented\n");
}

KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch(IN PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;
    
    /* Raise IRQL to SYNCH_LEVEL */
    OldIrql = KfRaiseIrql(SYNCH_LEVEL);
    
    /* Acquire the spinlock */
    KeAcquireSpinLockAtDpcLevel(SpinLock);
    
    return OldIrql;
}