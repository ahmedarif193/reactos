$if (_WDMDDK_)
/** Kernel definitions for ARM64 **/

/* Interrupt request levels */
#define PASSIVE_LEVEL           0
#define LOW_LEVEL               0
#define APC_LEVEL               1
#define DISPATCH_LEVEL          2
#define CLOCK_LEVEL             13
#define IPI_LEVEL               14
#define DRS_LEVEL               14
#define POWER_LEVEL             14
#define PROFILE_LEVEL           15
#define HIGH_LEVEL              15

#define KI_USER_SHARED_DATA     0xFFFF800000000000ULL
#define SharedUserData          ((KUSER_SHARED_DATA * const)KI_USER_SHARED_DATA)

#define PAGE_SIZE               0x1000
#define PAGE_SHIFT              12L

#define PAUSE_PROCESSOR YieldProcessor();

/* FIXME: Based on AMD64 but needed to compile apps */
#define KERNEL_STACK_SIZE                   12288
#define KERNEL_LARGE_STACK_SIZE             61440
#define KERNEL_LARGE_STACK_COMMIT KERNEL_STACK_SIZE
/* FIXME End */

#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

NTSYSAPI
PKTHREAD
NTAPI
KeGetCurrentThread(VOID);

FORCEINLINE
ULONG
KeGetCurrentProcessorNumber(VOID)
{
    /* ARM64: Read processor number from PCR */
    /* The PCR is stored in TPIDR_EL1 */
    /* Use void pointer to avoid forward declaration issues */
    PVOID Pcr;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    /* PCR->CurrentPrcb is at offset 0x180, PRCB->Number is at offset 2 */
    return *((PUSHORT)((PUCHAR)Pcr + 0x180 + 2));
}

/* ARM64 IRQL management */
NTKERNELAPI
KIRQL
NTAPI
KeGetCurrentIrql(VOID);

NTKERNELAPI
VOID
NTAPI
KeLowerIrql(
    _In_ KIRQL NewIrql);

NTKERNELAPI
VOID
NTAPI
KeRaiseIrql(
    _In_ KIRQL NewIrql,
    _Out_ PKIRQL OldIrql);

NTKERNELAPI
KIRQL
FASTCALL
KfRaiseIrql(
    _In_ KIRQL NewIrql);

NTKERNELAPI
VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql);

#define KeRaiseIrqlToDpcLevel() KfRaiseIrql(DISPATCH_LEVEL)
#define KeRaiseIrqlToSynchLevel() KfRaiseIrql(SYNCH_LEVEL)

NTKERNELAPI
BOOLEAN
NTAPI
KeDisableInterrupts(VOID);

NTKERNELAPI
VOID
NTAPI
KeRestoreInterrupts(
    _In_ BOOLEAN Enable);

#define DbgRaiseAssertionFailure() __break(0xf001)

$endif (_WDMDDK_)
$if (_NTDDK_)

#define ARM64_MAX_BREAKPOINTS 8
#define ARM64_MAX_WATCHPOINTS 2

typedef union NEON128 {
    struct {
        ULONGLONG Low;
        LONGLONG High;
    } DUMMYSTRUCTNAME;
    double D[2];
    float  S[4];
    USHORT H[8];
    UCHAR  B[16];
} NEON128, *PNEON128;
typedef NEON128 NEON128, *PNEON128;

/* Context flags for ARM64 */
#define CONTEXT_CONTROL         0x00000001
#define CONTEXT_INTEGER         0x00000002
#define CONTEXT_FLOATING_POINT  0x00000004
#define CONTEXT_DEBUG_REGISTERS 0x00000008
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)

typedef struct _CONTEXT {

    //
    // Control flags.
    //

    ULONG ContextFlags;

    //
    // Integer registers
    //

    ULONG Cpsr;
    union {
        struct {
            ULONG64 X0;
            ULONG64 X1;
            ULONG64 X2;
            ULONG64 X3;
            ULONG64 X4;
            ULONG64 X5;
            ULONG64 X6;
            ULONG64 X7;
            ULONG64 X8;
            ULONG64 X9;
            ULONG64 X10;
            ULONG64 X11;
            ULONG64 X12;
            ULONG64 X13;
            ULONG64 X14;
            ULONG64 X15;
            ULONG64 X16;
            ULONG64 X17;
            ULONG64 X18;
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
        } DUMMYSTRUCTNAME;
        ULONG64 X[31];
    } DUMMYUNIONNAME;

    ULONG64 Sp;
    ULONG64 Pc;

    //
    // Floating Point/NEON Registers
    //

    NEON128 V[32];
    ULONG Fpcr;
    ULONG Fpsr;

    //
    // Debug registers
    //

    ULONG Bcr[ARM64_MAX_BREAKPOINTS];
    ULONG64 Bvr[ARM64_MAX_BREAKPOINTS];
    ULONG Wcr[ARM64_MAX_WATCHPOINTS];
    ULONG64 Wvr[ARM64_MAX_WATCHPOINTS];

} CONTEXT, *PCONTEXT;

/* Timer functions */
VOID
NTAPI
KeQueryTickCount(OUT PLARGE_INTEGER CurrentCount);

/* HAL functions needed for ARM64 */
/* FIXME TODO: These should be moved to proper HAL headers */
NTSTATUS
NTAPI
HalAllocateAdapterChannel(
  _In_ PADAPTER_OBJECT AdapterObject,
  _In_ PWAIT_CONTEXT_BLOCK Wcb,
  _In_ ULONG NumberOfMapRegisters,
  _In_ PDRIVER_CONTROL ExecutionRoutine);

/* Additional ARM64 kernel functions */
/* FIXME TODO: These functions use types not yet defined in NTDDK scope
 * They should be moved to a location where the types are available
 */
#if 0
FORCEINLINE
ULONG
KeGetContextSwitches(IN PKPRCB Prcb)
{
    return Prcb->KeContextSwitches;
}

/* Trap frame functions */
ULONG_PTR
NTAPI
KeGetTrapFramePc(IN PKTRAP_FRAME TrapFrame);

BOOLEAN
NTAPI
KiUserTrap(IN PKTRAP_FRAME TrapFrame);

PKTRAP_FRAME
NTAPI
KiGetLinkedTrapFrame(IN PKTRAP_FRAME TrapFrame);

VOID
NTAPI
KiExceptionExit(IN PKTRAP_FRAME TrapFrame,
                IN PKEXCEPTION_FRAME ExceptionFrame);

/* HAL functions */
NTSTATUS
NTAPI
HalAllocateAdapterChannel(IN PADAPTER_OBJECT AdapterObject,
                          IN PWAIT_CONTEXT_BLOCK Wcb,
                          IN ULONG NumberOfMapRegisters,
                          IN PDRIVER_CONTROL ExecutionRoutine);

/* ARM64 has cache-coherent DMA, so KeFlushIoBuffers is a no-op */
#define KeFlushIoBuffers(Mdl, ReadOp, DmaOp) ((void)0)
#endif

$endif
