/*
 * ARM64 Architecture Stubs and Compatibility Definitions
 * This file provides temporary stubs and definitions for ARM64 port
 */

#ifndef _ARM64_STUBS_H
#define _ARM64_STUBS_H

#ifdef __cplusplus
extern "C" {
#endif

/* CONTEXT structure compatibility definitions for ARM64 */
#define SegCs       ContextFlags    /* No segment registers on ARM64 */
#define SegSs       ContextFlags
#define Rip         Pc              /* Use ARM64 PC register */
#define Rsp         Sp              /* Use ARM64 SP register */
#define Rax         X0              /* Map x86-64 registers to ARM64 */
#define Rbx         X1
#define Rcx         X2
#define Rdx         X3
#define Rsi         X4
#define Rdi         X5
#define Rbp         Fp              /* Frame pointer */
#define Dr0         Bvr[0]          /* Debug registers - use breakpoint registers */
#define Dr1         Bvr[1]
#define Dr2         Bvr[2]
#define Dr3         Bvr[3]
#define Dr6         Wcr[0]          /* Use watchpoint control register */
#define Dr7         Wcr[1]
#define Cr0         Sctlr           /* Control registers */
#define Cr2         Far
#define Cr3         Ttbr0
#define Cr4         Tcr

/* x86-specific types that don't exist on ARM64 */
typedef struct _KDESCRIPTOR {
    USHORT Pad[3];
    USHORT Limit;
    PVOID Base;
} KDESCRIPTOR, *PKDESCRIPTOR;

typedef struct _KGDTENTRY {
    USHORT LimitLow;
    USHORT BaseLow;
    union {
        struct {
            UCHAR BaseMid;
            UCHAR Flags1;
            UCHAR Flags2;
            UCHAR BaseHi;
        } Bytes;
        struct {
            ULONG BaseMid : 8;
            ULONG Type : 5;
            ULONG Dpl : 2;
            ULONG Pres : 1;
            ULONG LimitHi : 4;
            ULONG Sys : 1;
            ULONG Reserved_0 : 1;
            ULONG Default_Big : 1;
            ULONG Granularity : 1;
            ULONG BaseHi : 8;
        } Bits;
    };
} KGDTENTRY, *PKGDTENTRY;

typedef struct _KTSS {
    ULONG Reserved;
} KTSS, *PKTSS;

typedef struct _KIDR {
    ULONG Reserved;
} KIDR, *PKIDR;

/* Thread/Process structures */
typedef struct _KSWITCHFRAME {
    ULONG64 X19;
    ULONG64 X20;
    ULONG64 X21;
    ULONG64 X22;
    ULONG64 X23;
    ULONG64 X24;
    ULONG64 X25;
    ULONG64 X26;
    ULONG64 X27;
    ULONG64 X28;
    ULONG64 Fp;
    ULONG64 Lr;
} KSWITCHFRAME, *PKSWITCHFRAME;

/* ARM64-specific type definitions */
typedef struct _ARM64_KTRAP_FRAME {
    struct _KTRAP_FRAME Base;
    ULONG64 X[30];  /* General purpose registers */
} ARM64_KTRAP_FRAME, *PKARM64_KTRAP_FRAME;

/* Memory management function prototypes */
FORCEINLINE
PVOID
MmAllocateMemoryWithType(
    IN SIZE_T NumberOfBytes,
    IN TYPE_OF_MEMORY MemoryType)
{
    /* Stub implementation - should be properly implemented */
    return NULL;
}

FORCEINLINE
VOID
MiFlushTlb(
    IN PVOID Address,
    IN ULONG Count)
{
    /* ARM64 TLB flush implementation */
    __asm__ __volatile__("dsb sy");
    __asm__ __volatile__("tlbi vaae1is, %0" : : "r" ((ULONG_PTR)Address >> 12));
    __asm__ __volatile__("dsb sy");
    __asm__ __volatile__("isb");
}

FORCEINLINE
VOID
KeInvalidateTlbEntry(
    IN PVOID Address)
{
    MiFlushTlb(Address, 1);
}

FORCEINLINE
VOID
KeSweepICache(
    IN PVOID BaseAddress,
    IN SIZE_T Length)
{
    /* ARM64 instruction cache flush */
    __asm__ __volatile__("ic iallu");
    __asm__ __volatile__("dsb sy");
    __asm__ __volatile__("isb");
}

/* Exception/Interrupt handling */
FORCEINLINE
BOOLEAN
KeGetTrapFrameInterruptState(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Check if interrupts were enabled in SPSR */
    return !(TrapFrame->Spsr & 0x80);  /* Check I bit */
}

FORCEINLINE
PKEXCEPTION_FRAME
KeGetExceptionFrame(
    IN PKTHREAD Thread)
{
    /* ARM64 doesn't use exception frames the same way */
    return NULL;
}

FORCEINLINE
PKTRAP_FRAME
KeGetTrapFrame(
    IN PKTHREAD Thread)
{
    return (PKTRAP_FRAME)Thread->TrapFrame;
}

FORCEINLINE
ULONG_PTR
KeGetContextReturnRegister(
    IN PCONTEXT Context)
{
    return Context->X0;  /* Return value is in X0 on ARM64 */
}

FORCEINLINE
KPROCESSOR_MODE
KiGetPreviousMode(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Check EL0/EL1 in SPSR */
    return ((TrapFrame->Spsr & 0xF) == 0) ? UserMode : KernelMode;
}

/* Floating point state */
FORCEINLINE
VOID
KiFlushNPXState(
    IN PVOID SaveArea)
{
    /* ARM64 NEON/FP state flush - stub */
}

/* Instruction fetch detection */
#define MI_IS_INSTRUCTION_FETCH(FaultCode) ((FaultCode) & 0x4)
#define MI_IS_PAGE_EXECUTABLE(Pte) (!((Pte)->u.Hard.UserNoExecute))

/* Subsection PTE support */
#define MI_MAKE_SUBSECTION_PTE(NewPte, Subsection) \
    do { \
        (NewPte)->u.Long = 0; \
        (NewPte)->u.Subsect.Prototype = 1; \
        (NewPte)->u.Subsect.SubsectionAddress = (ULONG_PTR)(Subsection); \
    } while(0)

FORCEINLINE
PVOID
MiSubsectionPteToSubsection(
    IN PMMPTE Pte)
{
    return (PVOID)(Pte->u.Subsect.SubsectionAddress);
}

/* Additional stubs for missing functions */
/* Note: KiAcquireApcLock and KiReleaseApcLock are defined in ke_x.h */

FORCEINLINE
VOID
KiRequestThreadScheduling(VOID)
{
    /* Request reschedule - stub */
}

/* KiInitializeDpc is defined in ke/dpc.c - not a stub */

FORCEINLINE
VOID
KiInitializeGIC(VOID)
{
    /* Initialize Generic Interrupt Controller - stub */
}

FORCEINLINE
VOID
KiEnableInterrupt(
    IN ULONG Vector)
{
    /* Enable interrupt in GIC - stub */
}

FORCEINLINE
VOID
KiDisableInterrupt(
    IN ULONG Vector)
{
    /* Disable interrupt in GIC - stub */
}

FORCEINLINE
VOID
KiGenerateSGI(
    IN ULONG SgiId)
{
    /* Generate Software Generated Interrupt - stub */
}

FORCEINLINE
NTSTATUS
KiHandleBreakpoint(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Handle breakpoint exception - stub */
    return STATUS_SUCCESS;
}

FORCEINLINE
NTSTATUS
KiHandleWatchpoint(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG64 FaultAddress)
{
    /* Handle watchpoint exception - stub */
    return STATUS_SUCCESS;
}

FORCEINLINE
NTSTATUS
KiHandleFloatingPointException(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Handle floating point exception - stub */
    return STATUS_SUCCESS;
}

FORCEINLINE
VOID
KiInitializePerformanceMonitoring(VOID)
{
    /* Initialize performance monitoring - stub */
}

FORCEINLINE
VOID
KiInitializeDebugRegisters(VOID)
{
    /* Initialize debug registers - stub */
}

FORCEINLINE
NTSTATUS
KiCallSystemService(
    IN PVOID ServiceRoutine,
    IN PVOID Arguments,
    IN ULONG ArgumentCount)
{
    /* Call system service - stub */
    return STATUS_NOT_IMPLEMENTED;
}

/* Kernel global variable stubs */
extern ULONG KeI386MachineType;

/* x86 instruction emulation stubs */
FORCEINLINE
VOID
__sidt(PKDESCRIPTOR Descriptor)
{
    /* No IDT on ARM64 - stub */
    Descriptor->Limit = 0;
    Descriptor->Base = NULL;
}

/* Debug offset definitions for ARM64 */
#define KPCR_INITIAL_STACK_OFFSET     0x08
#define KPCR_STACK_LIMIT_OFFSET       0x10
#define KPRCB_PCR_PAGE_OFFSET         0x18

/* Additional type definitions */
typedef __int128 ULONG128;

#ifdef __cplusplus
}
#endif

#endif /* _ARM64_STUBS_H */