/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kdbg/kdb.c
 * PURPOSE:         Kernel Debugger
 *
 * PROGRAMMERS:     Gregor Anich
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "kdb.h"

/* TYPES *********************************************************************/

/* DEFINES *******************************************************************/

#define KDB_STACK_SIZE                   (4096*3)
#if defined(_M_AMD64) || defined(_M_ARM64)
#define KDB_STACK_ALIGN                 16
#ifdef _M_AMD64
#define KDB_STACK_RESERVE               (5 * sizeof(PVOID)) /* Home space + return address */
#else
#define KDB_STACK_RESERVE               0
#endif
#else
#define KDB_STACK_ALIGN                 4
#define KDB_STACK_RESERVE               sizeof(ULONG) /* Return address */
#endif
#define KDB_MAXIMUM_BREAKPOINT_COUNT     256
#define KDB_MAXIMUM_HW_BREAKPOINT_COUNT  4
#define KDB_MAXIMUM_SW_BREAKPOINT_COUNT  256

#if defined(_M_ARM64)
#define KDB_ARM64_PSTATE_D               (1UL << 9)
#define KDB_ARM64_WCR_ENABLE             (1UL << 0)
#define KDB_ARM64_WCR_PAC_EL0_EL1        (3UL << 1)
#define KDB_ARM64_WCR_LSC_LOAD           (1UL << 3)
#define KDB_ARM64_WCR_LSC_STORE          (2UL << 3)
#define KDB_ARM64_WCR_LSC_BOTH           (3UL << 3)
#define KDB_ARM64_WCR_BAS_SHIFT          5
#endif

#define __STRING(x) #x
#define _STRING(x) __STRING(x)

/* GLOBALS *******************************************************************/

static LONG KdbEntryCount = 0;
static DECLSPEC_ALIGN(KDB_STACK_ALIGN) CHAR KdbStack[KDB_STACK_SIZE];

static ULONG KdbBreakPointCount = 0;  /* Number of used breakpoints in the array */
static KDB_BREAKPOINT KdbBreakPoints[KDB_MAXIMUM_BREAKPOINT_COUNT] = {{0}};  /* Breakpoint array */
static ULONG KdbSwBreakPointCount = 0;  /* Number of enabled software breakpoints */
static ULONG KdbHwBreakPointCount = 0;  /* Number of enabled hardware breakpoints */
static PKDB_BREAKPOINT KdbSwBreakPoints[KDB_MAXIMUM_SW_BREAKPOINT_COUNT]; /* Enabled software breakpoints, orderless */
static PKDB_BREAKPOINT KdbHwBreakPoints[KDB_MAXIMUM_HW_BREAKPOINT_COUNT]; /* Enabled hardware breakpoints, orderless */
static PKDB_BREAKPOINT KdbBreakPointToReenable = NULL; /* Set to a breakpoint struct when single stepping after
                                                          a software breakpoint was hit, to reenable it */
static BOOLEAN KdbpEvenThoughWeHaveABreakPointToReenableWeAlsoHaveARealSingleStep;
LONG KdbLastBreakPointNr = -1;  /* Index of the breakpoint which cause KDB to be entered */
ULONG KdbNumSingleSteps = 0; /* How many single steps to do */
BOOLEAN KdbSingleStepOver = FALSE; /* Whether to step over calls/reps. */
static BOOLEAN KdbEnteredOnSingleStep = FALSE; /* Set to true when KDB was entered because of single step */
PEPROCESS KdbCurrentProcess = NULL;  /* The current process context in which KDB runs */
PEPROCESS KdbOriginalProcess = NULL; /* The process in whichs context KDB was intered */
PETHREAD KdbCurrentThread = NULL;  /* The current thread context in which KDB runs */
PETHREAD KdbOriginalThread = NULL; /* The thread in whichs context KDB was entered */
EXCEPTION_RECORD64 KdbCurrentExceptionRecord = { 0 };
BOOLEAN KdbCurrentExceptionRecordValid = FALSE;
PKDB_KTRAP_FRAME KdbCurrentTrapFrame = NULL; /* Pointer to the current trapframe */
static KDB_KTRAP_FRAME KdbTrapFrame = { 0 };  /* The trapframe which was passed to KdbEnterDebuggerException */
static KDB_KTRAP_FRAME KdbThreadTrapFrame = { 0 }; /* The trapframe of the current thread (KdbCurrentThread) */
static KAPC_STATE KdbApcState;
extern BOOLEAN KdbpBugCheckRequested;

/* Array of conditions when to enter KDB */
static KDB_ENTER_CONDITION KdbEnterConditions[][2] =
{
    /* First chance       Last chance */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 0: Zero divide */
    { KdbEnterFromKmode,  KdbDoNotEnter },       /* 1: Debug trap */
    { KdbDoNotEnter,      KdbEnterAlways },      /* 2: NMI */
    { KdbEnterFromKmode,  KdbDoNotEnter },       /* 3: INT3 */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 4: Overflow */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 5: BOUND range exceeded */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 6: Invalid opcode */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 7: No math coprocessor fault */
    { KdbEnterAlways,     KdbEnterAlways },      /* 8: Double Fault */
    { KdbEnterAlways,     KdbEnterAlways },      /* 9: Unknown(9) */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 10: Invalid TSS */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 11: Segment Not Present */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 12: Stack fault */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 13: General protection fault */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 14: Page fault */
    { KdbEnterAlways,     KdbEnterAlways },      /* 15: Reserved (15) */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 16: FPU fault */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 17: Alignment Check */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 18: Machine Check */
    { KdbDoNotEnter,      KdbEnterFromKmode },   /* 19: SIMD fault */
    { KdbEnterFromKmode,  KdbDoNotEnter },       /* 20: Assertion failure */
    { KdbDoNotEnter,      KdbEnterFromKmode }    /* Last entry: used for unknown exceptions */
};

/* Exception descriptions */
static const CHAR *ExceptionNrToString[] =
{
    "Divide Error",
    "Debug Trap",
    "NMI",
    "Breakpoint",
    "Overflow",
    "BOUND range exceeded",
    "Invalid Opcode",
    "No Math Coprocessor",
    "Double Fault",
    "Unknown(9)",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection",
    "Page Fault",
    "Reserved(15)",
    "Math Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Fault",
    "Assertion Failure"
};

/* FUNCTIONS *****************************************************************/

BOOLEAN
KdbpKdbTrapFrameFromKernelStack(
    PVOID KernelStack,
    PKDB_KTRAP_FRAME KdbTrapFrame)
{
    RtlZeroMemory(KdbTrapFrame, sizeof(KDB_KTRAP_FRAME));
    if (KernelStack == NULL)
        return FALSE;
#ifdef _M_IX86
    {
        ULONG_PTR Stack[8];

        if (!NT_SUCCESS(KdbpSafeReadMemory(Stack, KernelStack, sizeof(Stack))))
            return FALSE;
        KdbTrapFrame->Ebp = Stack[3];
        KdbTrapFrame->Edi = Stack[4];
        KdbTrapFrame->Esi = Stack[5];
        KdbTrapFrame->Ebx = Stack[6];
        KdbTrapFrame->Eip = Stack[7];
        KdbTrapFrame->Esp = (ULONG)((PULONG_PTR)KernelStack + RTL_NUMBER_OF(Stack));
    }
    KdbTrapFrame->SegSs = KGDT_R0_DATA;
    KdbTrapFrame->SegCs = KGDT_R0_CODE;
    KdbTrapFrame->SegDs = KGDT_R0_DATA;
    KdbTrapFrame->SegEs = KGDT_R0_DATA;
    KdbTrapFrame->SegGs = KGDT_R0_DATA;
#elif defined(_M_AMD64)
    {
        KSWITCH_FRAME SwitchFrame;
        KEXCEPTION_FRAME ExceptionFrame;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&SwitchFrame, KernelStack, sizeof(SwitchFrame))) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&ExceptionFrame, (PKSWITCH_FRAME)KernelStack + 1, sizeof(ExceptionFrame))))
        {
            return FALSE;
        }

        KdbTrapFrame->Rbx = ExceptionFrame.Rbx;
        KdbTrapFrame->Rbp = ExceptionFrame.Rbp;
        KdbTrapFrame->Rsi = ExceptionFrame.Rsi;
        KdbTrapFrame->Rdi = ExceptionFrame.Rdi;
        KdbTrapFrame->R12 = ExceptionFrame.R12;
        KdbTrapFrame->R13 = ExceptionFrame.R13;
        KdbTrapFrame->R14 = ExceptionFrame.R14;
        KdbTrapFrame->R15 = ExceptionFrame.R15;
        KdbTrapFrame->Rip = SwitchFrame.Return;
        KdbTrapFrame->Rsp = (ULONG_PTR)((PKSWITCH_FRAME)KernelStack + 1);
        KdbTrapFrame->SegCs = KGDT64_R0_CODE;
        KdbTrapFrame->SegSs = KGDT64_R0_DATA;
        KdbTrapFrame->SegDs = KGDT64_R0_DATA;
        KdbTrapFrame->SegEs = KGDT64_R0_DATA;
        KdbTrapFrame->SegFs = KGDT64_R0_DATA;
        KdbTrapFrame->SegGs = KGDT64_R0_DATA;
    }
#elif defined(_M_ARM64)
    {
        KSWITCH_FRAME SwitchFrame;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&SwitchFrame, KernelStack, sizeof(SwitchFrame))))
        {
            return FALSE;
        }

        KdbTrapFrame->X19 = SwitchFrame.X19;
        KdbTrapFrame->X20 = SwitchFrame.X20;
        KdbTrapFrame->X21 = SwitchFrame.X21;
        KdbTrapFrame->X22 = SwitchFrame.X22;
        KdbTrapFrame->X23 = SwitchFrame.X23;
        KdbTrapFrame->X24 = SwitchFrame.X24;
        KdbTrapFrame->X25 = SwitchFrame.X25;
        KdbTrapFrame->X26 = SwitchFrame.X26;
        KdbTrapFrame->X27 = SwitchFrame.X27;
        KdbTrapFrame->X28 = SwitchFrame.X28;
        KdbTrapFrame->Fp = SwitchFrame.Fp;
        KdbTrapFrame->Lr = SwitchFrame.ReturnAddress;
        KdbTrapFrame->Pc = SwitchFrame.ReturnAddress;
        KdbTrapFrame->Sp = (ULONG_PTR)((PKSWITCH_FRAME)KernelStack + 1);
        KdbTrapFrame->Cpsr = 0x5;
    }
#endif

    /* FIXME: what about the other registers??? */
    return KeGetContextPc((PCONTEXT)KdbTrapFrame) != 0;
}

#if defined(_M_ARM64)
static VOID
KdbpArm64ProgramWatchpoint(IN ULONG Slot, IN ULONG64 Address, IN ULONG Control)
{
    ULONG Processor;

    KdbTrapFrame.Wvr[Slot] = Address;
    KdbTrapFrame.Wcr[Slot] = Control;
    for (Processor = 0; Processor < KeNumberProcessors; Processor++)
    {
        if (KiProcessorBlock[Processor] == NULL)
            continue;

        KiProcessorBlock[Processor]->ProcessorState.SpecialRegisters.KernelWvr[Slot] = Address;
        KiProcessorBlock[Processor]->ProcessorState.SpecialRegisters.KernelWcr[Slot] = Control;
    }
}

static BOOLEAN
KdbpArm64WatchpointSlotInUse(IN ULONG Slot)
{
    ULONG Index;

    if (KdbTrapFrame.Wcr[Slot] & KDB_ARM64_WCR_ENABLE)
        return TRUE;

    for (Index = 0; Index < KeNumberProcessors; Index++)
    {
        if (KiProcessorBlock[Index] != NULL &&
            (KiProcessorBlock[Index]->ProcessorState.SpecialRegisters.KernelWcr[Slot] & KDB_ARM64_WCR_ENABLE))
        {
            return TRUE;
        }
    }

    return FALSE;
}
#endif

/*!\brief Overwrites the instruction at \a Address with \a NewInst and stores
 *        the old instruction in *OldInst.
 *
 * \param Process  Process in which's context to overwrite the instruction.
 * \param Address  Address at which to overwrite the instruction.
 * \param NewInst  New instruction (written to \a Address)
 * \param OldInst  Old instruction (read from \a Address)
 *
 * \returns NTSTATUS
 */
static NTSTATUS
KdbpOverwriteInstruction(
    IN  PEPROCESS Process,
    IN  ULONG_PTR Address,
    IN  KD_BREAKPOINT_TYPE NewInst,
    OUT KD_BREAKPOINT_TYPE *OldInst  OPTIONAL)
{
    NTSTATUS Status;
    ULONG Protect;
    PEPROCESS CurrentProcess = PsGetCurrentProcess();
    KAPC_STATE ApcState;

    /* Get the protection for the address. */
    Protect = MmGetPageProtect(Process, (PVOID)PAGE_ROUND_DOWN(Address));

    /* Return if that page isn't present. */
    if (Protect & PAGE_NOACCESS)
    {
        return STATUS_MEMORY_NOT_ALLOCATED;
    }

    /* Attach to the process */
    if (CurrentProcess != Process)
    {
        KeStackAttachProcess(&Process->Pcb, &ApcState);
    }

    /* Make the page writeable if it is read only. */
    if (Protect & (PAGE_READONLY|PAGE_EXECUTE|PAGE_EXECUTE_READ))
    {
        MmSetPageProtect(Process, (PVOID)PAGE_ROUND_DOWN(Address),
                         (Protect & ~(PAGE_READONLY|PAGE_EXECUTE|PAGE_EXECUTE_READ)) | PAGE_READWRITE);
    }

    /* Copy the old instruction back to the caller. */
    if (OldInst)
    {
        Status = KdbpSafeReadMemory(OldInst, (PVOID)Address, sizeof(*OldInst));
        if (!NT_SUCCESS(Status))
        {
            if (Protect & (PAGE_READONLY|PAGE_EXECUTE|PAGE_EXECUTE_READ))
            {
                MmSetPageProtect(Process, (PVOID)PAGE_ROUND_DOWN(Address), Protect);
            }

            /* Detach from process */
            if (CurrentProcess != Process)
            {
                KeUnstackDetachProcess(&ApcState);
            }

            return Status;
        }
    }

    /* Copy the new instruction in its place. */
    Status = KdbpSafeWriteMemory((PVOID)Address, &NewInst, sizeof(NewInst));
#if defined(_M_ARM64)
    if (NT_SUCCESS(Status))
    {
        /* Ranged, SMP-broadcast maintenance for exactly the patched instruction */
        KeSweepICache((PVOID)Address, sizeof(NewInst));
    }
#endif

    /* Restore the page protection. */
    if (Protect & (PAGE_READONLY|PAGE_EXECUTE|PAGE_EXECUTE_READ))
    {
        MmSetPageProtect(Process, (PVOID)PAGE_ROUND_DOWN(Address), Protect);
    }

    /* Detach from process */
    if (CurrentProcess != Process)
    {
        KeUnstackDetachProcess(&ApcState);
    }

    return Status;
}

/*!\brief Checks whether the given instruction can be single stepped or has to be
 *        stepped over using a temporary breakpoint.
 *
 * \retval TRUE   Instruction is a call.
 * \retval FALSE  Instruction is not a call.
 */
BOOLEAN
KdbpShouldStepOverInstruction(
    ULONG_PTR Eip)
{
#if defined(_M_ARM64)
    UNREFERENCED_PARAMETER(Eip);
    return FALSE;
#else
    UCHAR Mem[3];
    ULONG i = 0;

    if (!NT_SUCCESS(KdbpSafeReadMemory(Mem, (PVOID)Eip, sizeof (Mem))))
    {
        KdbPrintf("Couldn't access memory at 0x%p\n", Eip);
        return FALSE;
    }

    /* Check if the current instruction is a call. */
    while ((i < sizeof (Mem)) && (Mem[i] == 0x66 || Mem[i] == 0x67))
        i++;

    if (i == sizeof (Mem))
        return FALSE;

    if (Mem[i] == 0xE8 || Mem[i] == 0x9A || Mem[i] == 0xF2 || Mem[i] == 0xF3 ||
        (((i + 1) < sizeof (Mem)) && Mem[i] == 0xFF && (Mem[i+1] & 0x38) == 0x10))
    {
        return TRUE;
    }

    return FALSE;
#endif
}

/*!\brief Steps over an instruction
 *
 * If the given instruction should be stepped over, this function inserts a
 * temporary breakpoint after the instruction and returns TRUE, otherwise it
 * returns FALSE.
 *
 * \retval TRUE   Temporary breakpoint set after instruction.
 * \retval FALSE  No breakpoint was set.
 */
BOOLEAN
KdbpStepOverInstruction(
    ULONG_PTR Eip)
{
    LONG InstLen;

    if (!KdbpShouldStepOverInstruction(Eip))
        return FALSE;

    InstLen = KdbpGetInstLength(Eip);
    if (InstLen < 1)
        return FALSE;

    if (!NT_SUCCESS(KdbpInsertBreakPoint(Eip + InstLen, KdbBreakPointTemporary, 0, 0, NULL, FALSE, NULL)))
        return FALSE;

    return TRUE;
}

/*!\brief Steps into an instruction (interrupts)
 *
 * If the given instruction should be stepped into, this function inserts a
 * temporary breakpoint at the target instruction and returns TRUE, otherwise it
 * returns FALSE.
 *
 * \retval TRUE   Temporary breakpoint set at target instruction.
 * \retval FALSE  No breakpoint was set.
 */
BOOLEAN
KdbpStepIntoInstruction(
    ULONG_PTR Eip)
{
#if defined(_M_ARM64)
    UNREFERENCED_PARAMETER(Eip);
    return FALSE;
#else
    KDESCRIPTOR Idtr = {0};
    UCHAR Mem[2];
    INT IntVect;
    ULONG IntDesc[2];
    ULONG_PTR TargetEip;

    /* Read memory */
    if (!NT_SUCCESS(KdbpSafeReadMemory(Mem, (PVOID)Eip, sizeof (Mem))))
    {
        // KdbPrintf("Couldn't access memory at 0x%p\n", Eip);
        return FALSE;
    }

    /* Check for INT instruction */
    /* FIXME: Check for iret */
    if (Mem[0] == 0xcc)
        IntVect = 3;
    else if (Mem[0] == 0xcd)
        IntVect = Mem[1];
    else if (Mem[0] == 0xce && KdbCurrentTrapFrame->EFlags & (1<<11)) /* 1 << 11 is the overflow flag */
        IntVect = 4;
    else
        return FALSE;

    if (IntVect < 32) /* We should be informed about interrupts < 32 by the kernel, no need to breakpoint them */
    {
        return FALSE;
    }

    /* Read the interrupt descriptor table register  */
    __sidt(&Idtr.Limit);
    if (IntVect >= (Idtr.Limit + 1) / 8)
    {
        // KdbPrintf("IDT does not contain interrupt vector %d.\n", IntVect);
        return TRUE;
    }

    /* Get the interrupt descriptor */
    if (!NT_SUCCESS(KdbpSafeReadMemory(IntDesc, (PVOID)((ULONG_PTR)Idtr.Base + (IntVect * 8)), sizeof(IntDesc))))
    {
        // KdbPrintf("Couldn't access memory at 0x%p\n", (ULONG_PTR)Idtr.Base + (IntVect * 8));
        return FALSE;
    }

    /* Check descriptor and get target eip (16 bit interrupt/trap gates not supported) */
    if ((IntDesc[1] & (1 << 15)) == 0) /* not present */
    {
        return FALSE;
    }
    if ((IntDesc[1] & 0x1f00) == 0x0500) /* Task gate */
    {
        /* FIXME: Task gates not supported */
        return FALSE;
    }
    else if (((IntDesc[1] & 0x1fe0) == 0x0e00) || /* 32 bit Interrupt gate */
             ((IntDesc[1] & 0x1fe0) == 0x0f00))   /* 32 bit Trap gate */
    {
        /* FIXME: Should the segment selector of the interrupt gate be checked? */
        TargetEip = (IntDesc[1] & 0xffff0000) | (IntDesc[0] & 0x0000ffff);
    }
    else
    {
        return FALSE;
    }

    /* Insert breakpoint */
    if (!NT_SUCCESS(KdbpInsertBreakPoint(TargetEip, KdbBreakPointTemporary, 0, 0, NULL, FALSE, NULL)))
        return FALSE;

    return TRUE;
#endif
}

/*!\brief Gets the number of the next breakpoint >= Start.
 *
 * \param Start   Breakpoint number to start searching at. -1 if no more breakpoints are found.
 *
 * \returns Breakpoint number (-1 if no more breakpoints are found)
 */
LONG
KdbpGetNextBreakPointNr(
    IN ULONG Start  OPTIONAL)
{
    for (; Start < RTL_NUMBER_OF(KdbBreakPoints); Start++)
    {
        if (KdbBreakPoints[Start].Type != KdbBreakPointNone)
            return Start;
    }

    return -1;
}

/*!\brief Returns information of the specified breakpoint.
 *
 * \param BreakPointNr         Number of the breakpoint to return information of.
 * \param Address              Receives the address of the breakpoint.
 * \param Type                 Receives the type of the breakpoint (hardware or software)
 * \param Size                 Size - for memory breakpoints.
 * \param AccessType           Access type - for hardware breakpoints.
 * \param DebugReg             Debug register - for enabled hardware breakpoints.
 * \param Enabled              Whether the breakpoint is enabled or not.
 * \param Process              The owning process of the breakpoint.
 * \param ConditionExpression  The expression which was given as condition for the bp.
 *
 * \returns NULL on failure, pointer to a KDB_BREAKPOINT struct on success.
 */
BOOLEAN
KdbpGetBreakPointInfo(
    IN  ULONG BreakPointNr,
    OUT ULONG_PTR *Address  OPTIONAL,
    OUT KDB_BREAKPOINT_TYPE *Type  OPTIONAL,
    OUT UCHAR *Size  OPTIONAL,
    OUT KDB_ACCESS_TYPE *AccessType  OPTIONAL,
    OUT UCHAR *DebugReg  OPTIONAL,
    OUT BOOLEAN *Enabled  OPTIONAL,
    OUT BOOLEAN *Global  OPTIONAL,
    OUT PEPROCESS *Process  OPTIONAL,
    OUT PCHAR *ConditionExpression  OPTIONAL)
{
    PKDB_BREAKPOINT bp;

    if (BreakPointNr >= RTL_NUMBER_OF(KdbBreakPoints) ||
        KdbBreakPoints[BreakPointNr].Type == KdbBreakPointNone)
    {
        return FALSE;
    }

    bp = KdbBreakPoints + BreakPointNr;
    if (Address)
        *Address = bp->Address;

    if (Type)
        *Type = bp->Type;

    if (bp->Type == KdbBreakPointHardware)
    {
        if (Size)
            *Size = bp->Data.Hw.Size;

        if (AccessType)
            *AccessType = bp->Data.Hw.AccessType;

        if (DebugReg && bp->Enabled)
            *DebugReg = bp->Data.Hw.DebugReg;
    }

    if (Enabled)
        *Enabled = bp->Enabled;

    if (Global)
        *Global = bp->Global;

    if (Process)
        *Process = bp->Process;

    if (ConditionExpression)
        *ConditionExpression = bp->ConditionExpression;

    return TRUE;
}

/*!\brief Inserts a breakpoint into the breakpoint array.
 *
 * The \a Process of the breakpoint is set to \a KdbCurrentProcess
 *
 * \param Address              Address at which to set the breakpoint.
 * \param Type                 Type of breakpoint (hardware or software)
 * \param Size                 Size of breakpoint (for hardware/memory breakpoints)
 * \param AccessType           Access type (for hardware breakpoins)
 * \param ConditionExpression  Expression which must evaluate to true for conditional breakpoints.
 * \param Global               Wether the breakpoint is global or local to a process.
 * \param BreakPointNumber     Receives the breakpoint number on success
 *
 * \returns NTSTATUS
 */
NTSTATUS
KdbpInsertBreakPoint(
    IN  ULONG_PTR Address,
    IN  KDB_BREAKPOINT_TYPE Type,
    IN  UCHAR Size  OPTIONAL,
    IN  KDB_ACCESS_TYPE AccessType  OPTIONAL,
    IN  PCHAR ConditionExpression  OPTIONAL,
    IN  BOOLEAN Global,
    OUT PLONG BreakPointNr  OPTIONAL)
{
    LONG_PTR i;
    PVOID Condition;
    PCHAR ConditionExpressionDup;
    LONG ErrOffset;
    CHAR ErrMsg[128];

    ASSERT(Type != KdbBreakPointNone);

    if (Type == KdbBreakPointHardware)
    {
        if (Size != 1 && Size != 2 && Size != 4 && Size != 8)
        {
            KdbPrintf("Unsupported hardware breakpoint size %u.\n", Size);
            return STATUS_UNSUCCESSFUL;
        }

        if (AccessType == KdbAccessExec && Size != 1)
        {
            KdbPuts("Size must be 1 for execution breakpoints.\n");
            return STATUS_UNSUCCESSFUL;
        }

#if defined(_M_ARM64)
        if (AccessType != KdbAccessExec && ((Address & 7) + Size > 8 || Address > MAXULONG_PTR - (Size - 1)))
        {
            KdbPuts("ARM64 watchpoints may not wrap or cross an eight-byte boundary.\n");
            return STATUS_UNSUCCESSFUL;
        }
#else
#if !defined(_M_AMD64)
        if (Size == 8)
        {
            KdbPuts("Eight-byte hardware breakpoints require AMD64.\n");
            return STATUS_UNSUCCESSFUL;
        }
#endif
        if ((Address % Size) != 0)
        {
            KdbPrintf("Address (0x%p) must be aligned to a multiple of the size (%d)\n", (PVOID)Address, Size);
            return STATUS_UNSUCCESSFUL;
        }
#endif
    }

    if (KdbBreakPointCount == KDB_MAXIMUM_BREAKPOINT_COUNT)
    {
        return STATUS_UNSUCCESSFUL;
    }

    /* Parse condition expression string and duplicate it */
    if (ConditionExpression)
    {
        Condition = KdbpRpnParseExpression(ConditionExpression, &ErrOffset, ErrMsg);
        if (!Condition)
        {
            if (ErrOffset >= 0)
                KdbPrintf("Couldn't parse expression: %s at character %d\n", ErrMsg, ErrOffset);
            else
                KdbPrintf("Couldn't parse expression: %s", ErrMsg);

            return STATUS_UNSUCCESSFUL;
        }

        i = strlen(ConditionExpression) + 1;
        ConditionExpressionDup = ExAllocatePoolWithTag(NonPagedPool, i, TAG_KDBG);
        RtlCopyMemory(ConditionExpressionDup, ConditionExpression, i);
    }
    else
    {
        Condition = NULL;
        ConditionExpressionDup = NULL;
    }

    /* Find unused breakpoint */
    if (Type == KdbBreakPointTemporary)
    {
        for (i = RTL_NUMBER_OF(KdbBreakPoints) - 1; i >= 0; i--)
        {
            if (KdbBreakPoints[i].Type == KdbBreakPointNone)
                break;
        }
    }
    else
    {
        for (i = 0; i < (LONG)RTL_NUMBER_OF(KdbBreakPoints); i++)
        {
            if (KdbBreakPoints[i].Type == KdbBreakPointNone)
                break;
        }
    }

    ASSERT(i < (LONG)RTL_NUMBER_OF(KdbBreakPoints));

    /* Set the breakpoint */
    ASSERT(KdbCurrentProcess);
    KdbBreakPoints[i].Type = Type;
    KdbBreakPoints[i].Address = Address;
    KdbBreakPoints[i].Enabled = FALSE;
    KdbBreakPoints[i].Global = Global;
    KdbBreakPoints[i].Process = KdbCurrentProcess;
    KdbBreakPoints[i].ConditionExpression = ConditionExpressionDup;
    KdbBreakPoints[i].Condition = Condition;

    if (Type == KdbBreakPointHardware)
    {
        KdbBreakPoints[i].Data.Hw.Size = Size;
        KdbBreakPoints[i].Data.Hw.AccessType = AccessType;
    }

    KdbBreakPointCount++;

    if (Type != KdbBreakPointTemporary)
        KdbPrintf("Breakpoint %d inserted.\n", i);

    /* Try to enable the breakpoint */
    KdbpEnableBreakPoint(i, NULL);

    /* Return the breakpoint number */
    if (BreakPointNr)
        *BreakPointNr = i;

    return STATUS_SUCCESS;
}

/*!\brief Deletes a breakpoint
 *
 * \param BreakPointNr  Number of the breakpoint to delete. Can be -1
 * \param BreakPoint    Breakpoint to delete. Can be NULL.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure (invalid breakpoint number)
 */
BOOLEAN
KdbpDeleteBreakPoint(
    IN LONG BreakPointNr  OPTIONAL,
    IN OUT PKDB_BREAKPOINT BreakPoint  OPTIONAL)
{
    if (BreakPointNr < 0)
    {
        ASSERT(BreakPoint);
        BreakPointNr = BreakPoint - KdbBreakPoints;
    }

    if (BreakPointNr < 0 || BreakPointNr >= KDB_MAXIMUM_BREAKPOINT_COUNT)
    {
        KdbPrintf("Invalid breakpoint: %d\n", BreakPointNr);
        return FALSE;
    }

    if (!BreakPoint)
    {
        BreakPoint = KdbBreakPoints + BreakPointNr;
    }

    if (BreakPoint->Type == KdbBreakPointNone)
    {
        KdbPrintf("Invalid breakpoint: %d\n", BreakPointNr);
        return FALSE;
    }

    if (BreakPoint->Enabled && !KdbpDisableBreakPoint(-1, BreakPoint))
        return FALSE;

    if (BreakPoint->Type != KdbBreakPointTemporary)
        KdbPrintf("Breakpoint %d deleted.\n", BreakPointNr);

    BreakPoint->Type = KdbBreakPointNone;
    KdbBreakPointCount--;

    return TRUE;
}

/*!\brief Checks if the breakpoint was set by the debugger
 *
 * Tries to find a breakpoint in the breakpoint array which caused
 * the debug exception to happen.
 *
 * \param ExpNr      Exception Number (1 or 3)
 * \param TrapFrame  Exception trapframe
 *
 * \returns Breakpoint number, -1 on error.
 */
static LONG
KdbpIsBreakPointOurs(IN NTSTATUS ExceptionCode, IN PCONTEXT Context, IN PEXCEPTION_RECORD64 ExceptionRecord)
{
    ULONG i;
    ASSERT(ExceptionCode == STATUS_SINGLE_STEP || ExceptionCode == STATUS_BREAKPOINT);

    if (ExceptionCode == STATUS_BREAKPOINT) /* Software interrupt */
    {
#if defined(_M_ARM64)
        ULONG_PTR BpPc = KeGetContextPc(Context);
#else
        ULONG_PTR BpPc = KeGetContextPc(Context) - 1; /* Get EIP of INT3 instruction */
#endif
        for (i = 0; i < KdbSwBreakPointCount; i++)
        {
            ASSERT((KdbSwBreakPoints[i]->Type == KdbBreakPointSoftware ||
                   KdbSwBreakPoints[i]->Type == KdbBreakPointTemporary));
            ASSERT(KdbSwBreakPoints[i]->Enabled);

            if (KdbSwBreakPoints[i]->Address == BpPc)
            {
                return KdbSwBreakPoints[i] - KdbBreakPoints;
            }
        }
    }
    else if (ExceptionCode == STATUS_SINGLE_STEP) /* Hardware interrupt */
    {
#if defined(_M_ARM64)
        ULONG Closest = MAXULONG;
        ULONG64 MinimumDistance = MAXULONGLONG;
        ULONG64 Reported;

        if (ExceptionRecord == NULL || ExceptionRecord->NumberParameters == 0)
            return -1;

        Reported = ExceptionRecord->ExceptionInformation[0];
        for (i = 0; i < KdbHwBreakPointCount; i++)
        {
            PKDB_BREAKPOINT BreakPoint = KdbHwBreakPoints[i];
            ULONG64 LastByte;
            ULONG64 Distance;

            ASSERT(BreakPoint->Type == KdbBreakPointHardware && BreakPoint->Enabled);
            if (BreakPoint->Data.Hw.AccessType == KdbAccessExec)
                continue;

            if (ExceptionRecord->NumberParameters > 1)
            {
                BOOLEAN IsWrite = ExceptionRecord->ExceptionInformation[1] != 0;

                if ((IsWrite && BreakPoint->Data.Hw.AccessType == KdbAccessRead) ||
                    (!IsWrite && BreakPoint->Data.Hw.AccessType == KdbAccessWrite))
                {
                    continue;
                }
            }

            LastByte = BreakPoint->Address + BreakPoint->Data.Hw.Size - 1;
            if (Reported < BreakPoint->Address)
                Distance = BreakPoint->Address - Reported;
            else if (Reported > LastByte)
                Distance = Reported - LastByte;
            else
                Distance = 0;

            if (Distance < MinimumDistance)
            {
                MinimumDistance = Distance;
                Closest = i;
            }
        }

        return Closest == MAXULONG ? -1 : KdbHwBreakPoints[Closest] - KdbBreakPoints;
#else
        UCHAR DebugReg;

        for (i = 0; i < KdbHwBreakPointCount; i++)
        {
            ASSERT(KdbHwBreakPoints[i]->Type == KdbBreakPointHardware &&
                   KdbHwBreakPoints[i]->Enabled);
            DebugReg = KdbHwBreakPoints[i]->Data.Hw.DebugReg;

            if ((Context->Dr6 & ((ULONG_PTR)1 << DebugReg)) != 0)
            {
                return KdbHwBreakPoints[i] - KdbBreakPoints;
            }
        }
#endif
    }

    return -1;
}

/*!\brief Enables a breakpoint.
 *
 * \param BreakPointNr  Number of the breakpoint to enable Can be -1.
 * \param BreakPoint    Breakpoint to enable. Can be NULL.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure.
 *
 * \sa KdbpDisableBreakPoint
 */
BOOLEAN
KdbpEnableBreakPoint(
    IN LONG BreakPointNr  OPTIONAL,
    IN OUT PKDB_BREAKPOINT BreakPoint  OPTIONAL)
{
    NTSTATUS Status;
#if !defined(_M_ARM64)
    INT i;
    ULONG ul, Length;
#endif

    if (BreakPointNr < 0)
    {
        ASSERT(BreakPoint);
        BreakPointNr = BreakPoint - KdbBreakPoints;
    }

    if (BreakPointNr < 0 || BreakPointNr >= KDB_MAXIMUM_BREAKPOINT_COUNT)
    {
        KdbPrintf("Invalid breakpoint: %d\n", BreakPointNr);
        return FALSE;
    }

    if (!BreakPoint)
    {
        BreakPoint = KdbBreakPoints + BreakPointNr;
    }

    if (BreakPoint->Type == KdbBreakPointNone)
    {
        KdbPrintf("Invalid breakpoint: %d\n", BreakPointNr);
        return FALSE;
    }

    if (BreakPoint->Enabled)
    {
        KdbPrintf("Breakpoint %d is already enabled.\n", BreakPointNr);
        return TRUE;
    }

    if (BreakPoint->Type == KdbBreakPointSoftware ||
        BreakPoint->Type == KdbBreakPointTemporary)
    {
        if (KdbSwBreakPointCount >= KDB_MAXIMUM_SW_BREAKPOINT_COUNT)
        {
            KdbPrintf("Maximum number of SW breakpoints (%d) used. "
                      "Disable another breakpoint in order to enable this one.\n",
                      KDB_MAXIMUM_SW_BREAKPOINT_COUNT);
            return FALSE;
        }

        Status = KdbpOverwriteInstruction(BreakPoint->Process, BreakPoint->Address,
                                          KD_BREAKPOINT_VALUE,
                                          &BreakPoint->Data.SavedInstruction);
        if (!NT_SUCCESS(Status))
        {
            KdbPrintf("Couldn't access memory at 0x%p\n", BreakPoint->Address);
            return FALSE;
        }

        KdbSwBreakPoints[KdbSwBreakPointCount++] = BreakPoint;
    }
    else
    {
#if defined(_M_ARM64)
        ULONG Control;
        ULONG Slot;

        if (BreakPoint->Data.Hw.AccessType == KdbAccessExec)
        {
            KdbPuts("ARM64 KDB execution hardware breakpoints are not supported.\n");
            return FALSE;
        }

        if (KdbHwBreakPointCount >= KiArm64NumWatchpoints)
        {
            KdbPrintf("Maximum number of ARM64 watchpoints (%lu) already used.\n", KiArm64NumWatchpoints);
            return FALSE;
        }

        for (Slot = 0; Slot < KiArm64NumWatchpoints; Slot++)
        {
            ULONG Index;
            BOOLEAN Used = KdbpArm64WatchpointSlotInUse(Slot);

            for (Index = 0; !Used && Index < KdbHwBreakPointCount; Index++)
                Used = KdbHwBreakPoints[Index]->Data.Hw.DebugReg == Slot;
            if (!Used)
                break;
        }

        if (Slot >= KiArm64NumWatchpoints)
        {
            KdbPuts("No free ARM64 watchpoint register is available.\n");
            return FALSE;
        }

        Control = KDB_ARM64_WCR_ENABLE | KDB_ARM64_WCR_PAC_EL0_EL1;
        switch (BreakPoint->Data.Hw.AccessType)
        {
            case KdbAccessRead: Control |= KDB_ARM64_WCR_LSC_LOAD; break;
            case KdbAccessWrite: Control |= KDB_ARM64_WCR_LSC_STORE; break;
            case KdbAccessReadWrite: Control |= KDB_ARM64_WCR_LSC_BOTH; break;
            default: ASSERT(FALSE); return FALSE;
        }

        Control |= ((((1UL << BreakPoint->Data.Hw.Size) - 1) << (BreakPoint->Address & 7)) << KDB_ARM64_WCR_BAS_SHIFT);
        KdbpArm64ProgramWatchpoint(Slot, BreakPoint->Address & ~7ULL, Control);
        KdbTrapFrame.Cpsr &= ~KDB_ARM64_PSTATE_D;
        BreakPoint->Data.Hw.DebugReg = Slot;
        KdbHwBreakPoints[KdbHwBreakPointCount++] = BreakPoint;
#else
        if (BreakPoint->Data.Hw.AccessType == KdbAccessExec)
            ASSERT(BreakPoint->Data.Hw.Size == 1);

        ASSERT((BreakPoint->Address % BreakPoint->Data.Hw.Size) == 0);

        if (KdbHwBreakPointCount >= KDB_MAXIMUM_HW_BREAKPOINT_COUNT)
        {
            KdbPrintf("Maximum number of HW breakpoints (%d) already used. "
                      "Disable another breakpoint in order to enable this one.\n",
                      KDB_MAXIMUM_HW_BREAKPOINT_COUNT);
            return FALSE;
        }

        /* Find unused hw breakpoint */
        ASSERT(KDB_MAXIMUM_HW_BREAKPOINT_COUNT == 4);
        for (i = 0; i < KDB_MAXIMUM_HW_BREAKPOINT_COUNT; i++)
        {
            if ((KdbTrapFrame.Dr7 & (0x3 << (i * 2))) == 0)
                break;
        }

        ASSERT(i < KDB_MAXIMUM_HW_BREAKPOINT_COUNT);

        /* Set the breakpoint address. */
        switch (i)
        {
            case 0:
                KdbTrapFrame.Dr0 = BreakPoint->Address;
                break;
            case 1:
                KdbTrapFrame.Dr1 = BreakPoint->Address;
                break;
            case 2:
                KdbTrapFrame.Dr2 = BreakPoint->Address;
                break;
            case 3:
                KdbTrapFrame.Dr3 = BreakPoint->Address;
                break;
        }

        /* Enable the global breakpoint */
        KdbTrapFrame.Dr7 |= (0x2 << (i * 2));

        /* Enable the exact match bits. */
        KdbTrapFrame.Dr7 |= 0x00000300;

        /* Clear existing state. */
        KdbTrapFrame.Dr7 &= ~(0xF << (16 + (i * 4)));

        /* Set the breakpoint type. */
        switch (BreakPoint->Data.Hw.AccessType)
        {
            case KdbAccessExec:
                ul = 0;
                break;
            case KdbAccessWrite:
                ul = 1;
                break;
            case KdbAccessRead:
            case KdbAccessReadWrite:
                ul = 3;
                break;
            default:
                ASSERT(0);
                return TRUE;
                break;
        }

        KdbTrapFrame.Dr7 |= (ul << (16 + (i * 4)));

        /* Set the breakpoint length (00=1, 01=2, 11=4, 10=8 bytes). */
        switch (BreakPoint->Data.Hw.Size)
        {
            case 1: Length = 0; break;
            case 2: Length = 1; break;
            case 4: Length = 3; break;
            case 8: Length = 2; break;
            default: ASSERT(FALSE); return FALSE;
        }
        KdbTrapFrame.Dr7 |= (Length << (18 + (i * 4)));

        /* Update KdbCurrentTrapFrame - values are taken from there by the CLI */
        if (&KdbTrapFrame != KdbCurrentTrapFrame)
        {
            KdbCurrentTrapFrame->Dr0 = KdbTrapFrame.Dr0;
            KdbCurrentTrapFrame->Dr1 = KdbTrapFrame.Dr1;
            KdbCurrentTrapFrame->Dr2 = KdbTrapFrame.Dr2;
            KdbCurrentTrapFrame->Dr3 = KdbTrapFrame.Dr3;
            KdbCurrentTrapFrame->Dr6 = KdbTrapFrame.Dr6;
            KdbCurrentTrapFrame->Dr7 = KdbTrapFrame.Dr7;
        }

        BreakPoint->Data.Hw.DebugReg = i;
        KdbHwBreakPoints[KdbHwBreakPointCount++] = BreakPoint;
#endif
    }

    BreakPoint->Enabled = TRUE;
    if (BreakPoint->Type != KdbBreakPointTemporary)
        KdbPrintf("Breakpoint %d enabled.\n", BreakPointNr);

    return TRUE;
}

/*!\brief Disables a breakpoint.
 *
 * \param BreakPointNr  Number of the breakpoint to disable. Can be -1
 * \param BreakPoint    Breakpoint to disable. Can be NULL.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure.
 *
 * \sa KdbpEnableBreakPoint
 */
BOOLEAN
KdbpDisableBreakPoint(
    IN LONG BreakPointNr  OPTIONAL,
    IN OUT PKDB_BREAKPOINT BreakPoint  OPTIONAL)
{
    ULONG i;
    NTSTATUS Status;

    if (BreakPointNr < 0)
    {
        ASSERT(BreakPoint);
        BreakPointNr = BreakPoint - KdbBreakPoints;
    }

    if (BreakPointNr < 0 || BreakPointNr >= KDB_MAXIMUM_BREAKPOINT_COUNT)
    {
        KdbPrintf("Invalid breakpoint: %d\n", BreakPointNr);
        return FALSE;
    }

    if (!BreakPoint)
    {
        BreakPoint = KdbBreakPoints + BreakPointNr;
    }

    if (BreakPoint->Type == KdbBreakPointNone)
    {
        KdbPrintf("Invalid breakpoint: %d\n", BreakPointNr);
        return FALSE;
    }

    if (BreakPoint->Enabled == FALSE)
    {
        KdbPrintf("Breakpoint %d is not enabled.\n", BreakPointNr);
        return TRUE;
    }

    if (BreakPoint->Type == KdbBreakPointSoftware ||
        BreakPoint->Type == KdbBreakPointTemporary)
    {
        ASSERT(KdbSwBreakPointCount > 0);
        Status = KdbpOverwriteInstruction(BreakPoint->Process, BreakPoint->Address,
                                          BreakPoint->Data.SavedInstruction, NULL);

        if (!NT_SUCCESS(Status))
        {
            KdbPuts("Couldn't restore original instruction.\n");
            return FALSE;
        }

        for (i = 0; i < KdbSwBreakPointCount; i++)
        {
            if (KdbSwBreakPoints[i] == BreakPoint)
            {
                KdbSwBreakPoints[i] = KdbSwBreakPoints[--KdbSwBreakPointCount];
                i = -1; /* if the last breakpoint is disabled dont break with i >= KdbSwBreakPointCount */
                break;
            }
        }

        if (i != MAXULONG) /* not found */
            ASSERT(0);
    }
    else
    {
        ASSERT(BreakPoint->Type == KdbBreakPointHardware);

#if defined(_M_ARM64)
        KdbpArm64ProgramWatchpoint(BreakPoint->Data.Hw.DebugReg, 0, 0);

        for (i = 0; i < KdbHwBreakPointCount; i++)
        {
            if (KdbHwBreakPoints[i] == BreakPoint)
            {
                KdbHwBreakPoints[i] = KdbHwBreakPoints[--KdbHwBreakPointCount];
                i = MAXULONG;
                break;
            }
        }

        if (i != MAXULONG)
            ASSERT(FALSE);
#else
        /* Clear the breakpoint. */
        KdbTrapFrame.Dr7 &= ~(0x3 << (BreakPoint->Data.Hw.DebugReg * 2));
        if ((KdbTrapFrame.Dr7 & 0xFF) == 0)
        {
            /* If no breakpoints are enabled then clear the exact match flags. */
            KdbTrapFrame.Dr7 &= 0xFFFFFCFF;
        }

        for (i = 0; i < KdbHwBreakPointCount; i++)
        {
            if (KdbHwBreakPoints[i] == BreakPoint)
            {
                KdbHwBreakPoints[i] = KdbHwBreakPoints[--KdbHwBreakPointCount];
                i = -1; /* if the last breakpoint is disabled dont break with i >= KdbHwBreakPointCount */
                break;
            }
        }

        if (i != MAXULONG) /* not found */
            ASSERT(0);
#endif
    }

    BreakPoint->Enabled = FALSE;
    if (BreakPoint->Type != KdbBreakPointTemporary)
        KdbPrintf("Breakpoint %d disabled.\n", BreakPointNr);

    return TRUE;
}

/*!\brief Gets the first or last chance enter-condition for exception nr. \a ExceptionNr
 *
 * \param ExceptionNr  Number of the exception to get condition of.
 * \param FirstChance  Whether to get first or last chance condition.
 * \param Condition    Receives the condition setting.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure (invalid exception nr)
 */
BOOLEAN
KdbpGetEnterCondition(
    IN LONG ExceptionNr,
    IN BOOLEAN FirstChance,
    OUT KDB_ENTER_CONDITION *Condition)
{
    if (ExceptionNr >= (LONG)RTL_NUMBER_OF(KdbEnterConditions))
        return FALSE;

    *Condition = KdbEnterConditions[ExceptionNr][FirstChance ? 0 : 1];
    return TRUE;
}

/*!\brief Sets the first or last chance enter-condition for exception nr. \a ExceptionNr
 *
 * \param ExceptionNr  Number of the exception to set condition of (-1 for all)
 * \param FirstChance  Whether to set first or last chance condition.
 * \param Condition    The new condition setting.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure (invalid exception nr)
 */
BOOLEAN
KdbpSetEnterCondition(
    IN LONG ExceptionNr,
    IN BOOLEAN FirstChance,
    IN KDB_ENTER_CONDITION Condition)
{
    if (ExceptionNr < 0)
    {
        for (ExceptionNr = 0; ExceptionNr < (LONG)RTL_NUMBER_OF(KdbEnterConditions); ExceptionNr++)
        {
            if (ExceptionNr == 1 || ExceptionNr == 8 ||
                ExceptionNr == 9 || ExceptionNr == 15) /* Reserved exceptions */
            {
                continue;
            }

            KdbEnterConditions[ExceptionNr][FirstChance ? 0 : 1] = Condition;
        }
    }
    else
    {
        if (ExceptionNr >= (LONG)RTL_NUMBER_OF(KdbEnterConditions) ||
            ExceptionNr == 1 || ExceptionNr == 8 || /* Do not allow changing of the debug */
            ExceptionNr == 9 || ExceptionNr == 15)  /* trap or reserved exceptions */
        {
            return FALSE;
        }

        KdbEnterConditions[ExceptionNr][FirstChance ? 0 : 1] = Condition;
    }

    return TRUE;
}

/*!\brief Switches to another thread context
 *
 * \param ThreadId  Id of the thread to switch to.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure (i.e. invalid thread id)
 */
BOOLEAN
KdbpFindProcessById(IN PVOID ProcessId, OUT PEPROCESS *Process)
{
    extern LIST_ENTRY PsActiveProcessHead;
    LIST_ENTRY Head;
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    *Process = NULL;
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Head, &PsActiveProcessHead, sizeof(Head))))
    {
        return FALSE;
    }
    Entry = Head.Flink;
    while (Entry != NULL &&
           Entry != &PsActiveProcessHead &&
           Count++ < 4096)
    {
        LIST_ENTRY Links;
        HANDLE CandidateId;
        PEPROCESS Candidate;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Links, Entry, sizeof(Links))))
            return FALSE;
        Candidate = CONTAINING_RECORD(Entry, EPROCESS, ActiveProcessLinks);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&CandidateId, &Candidate->UniqueProcessId, sizeof(CandidateId))))
        {
            return FALSE;
        }
        if (CandidateId == ProcessId)
        {
            *Process = Candidate;
            return TRUE;
        }
        if (Links.Flink == Entry)
            return FALSE;
        Entry = Links.Flink;
    }
    return FALSE;
}

BOOLEAN
KdbpFindThreadById(IN PVOID ThreadId, OUT PETHREAD *Thread)
{
    extern LIST_ENTRY PsActiveProcessHead;
    LIST_ENTRY Head;
    PLIST_ENTRY ProcessEntry;
    ULONG ProcessCount = 0;
    ULONG ThreadCount = 0;

    *Thread = NULL;
    if (!NT_SUCCESS(KdbpSafeReadMemory(&Head, &PsActiveProcessHead, sizeof(Head))))
    {
        return FALSE;
    }
    ProcessEntry = Head.Flink;
    while (ProcessEntry != NULL &&
           ProcessEntry != &PsActiveProcessHead &&
           ProcessCount++ < 4096 &&
           ThreadCount < 65536)
    {
        LIST_ENTRY ProcessLinks;
        LIST_ENTRY ProcessThreadListHead;
        PEPROCESS Process;
        PLIST_ENTRY ThreadEntry;
        PLIST_ENTRY ThreadListHead;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessLinks, ProcessEntry, sizeof(ProcessLinks))))
        {
            return FALSE;
        }
        Process = CONTAINING_RECORD(ProcessEntry, EPROCESS, ActiveProcessLinks);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessThreadListHead, &Process->ThreadListHead, sizeof(ProcessThreadListHead))))
        {
            return FALSE;
        }

        ThreadEntry = ProcessThreadListHead.Flink;
        ThreadListHead = &Process->ThreadListHead;
        while (ThreadEntry != NULL &&
               ThreadEntry != ThreadListHead &&
               ThreadCount++ < 65536)
        {
            LIST_ENTRY ThreadLinks;
            CLIENT_ID CandidateCid;
            PETHREAD Candidate;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadLinks, ThreadEntry, sizeof(ThreadLinks))))
            {
                return FALSE;
            }
            Candidate = CONTAINING_RECORD(ThreadEntry, ETHREAD, ThreadListEntry);
            if (!NT_SUCCESS(KdbpSafeReadMemory(&CandidateCid, &Candidate->Cid, sizeof(CandidateCid))))
            {
                return FALSE;
            }
            if (CandidateCid.UniqueThread == ThreadId)
            {
                *Thread = Candidate;
                return TRUE;
            }
            if (ThreadLinks.Flink == ThreadEntry)
                return FALSE;
            ThreadEntry = ThreadLinks.Flink;
        }

        if (ProcessLinks.Flink == ProcessEntry)
            return FALSE;
        ProcessEntry = ProcessLinks.Flink;
    }
    return FALSE;
}

BOOLEAN
KdbpAttachToThread(
    PVOID ThreadId)
{
    PETHREAD Thread = NULL;
    PEPROCESS Process;
    ETHREAD ThreadSnapshot;

    /* The machine is frozen; avoid the CID-table locks used by PsLookup*. */
    if (!KdbpFindThreadById(ThreadId, &Thread))
    {
        KdbpPrint("Invalid thread id: %p\n", ThreadId);
        return FALSE;
    }
    if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, Thread, sizeof(ThreadSnapshot))))
    {
        KdbpPrint("Cannot read thread %p.\n", Thread);
        return FALSE;
    }
    Process = (PEPROCESS)ThreadSnapshot.ThreadsProcess;
    if (Process == NULL)
    {
        KdbpPrint("Thread %p has no owning process.\n", ThreadId);
        return FALSE;
    }

    if (KeGetCurrentIrql() >= DISPATCH_LEVEL && Process != KdbCurrentProcess)
    {
        KdbpPrint("Cannot attach to a thread in another process at IRQL %lu.\n", KeGetCurrentIrql());
        return FALSE;
    }

    /* Save the current thread's context (if we previously attached to a thread) */
    if (KdbCurrentThread != KdbOriginalThread)
    {
        ASSERT(KdbCurrentTrapFrame == &KdbThreadTrapFrame);
        /* Actually, we can't save the context, there's no guarantee that there was a trap frame */
    }
    else
    {
        ASSERT(KdbCurrentTrapFrame == &KdbTrapFrame);
    }

    /* Switch to the thread's context */
    if (Thread != KdbOriginalThread)
    {
        ULONG Processor;
        BOOLEAN Running = FALSE;
        BOOLEAN Captured = FALSE;

        /* The thread we're attaching to isn't the thread on which we entered
         * kdb. It may be running on a frozen sibling processor, in which case
         * the PRCB processor state is the only stable context. Otherwise use
         * the scheduler switch frame from the saved kernel stack. */
        for (Processor = 0; Processor < (ULONG)(UCHAR)KeNumberProcessors; Processor++)
        {
            PKPRCB Prcb = KiProcessorBlock[Processor];
            PETHREAD ProcessorThread;
            ULONG FrozenState;

            if (Prcb == NULL ||
                !NT_SUCCESS(KdbpSafeReadMemory(&ProcessorThread, &Prcb->CurrentThread, sizeof(ProcessorThread))) ||
                ProcessorThread != Thread)
            {
                continue;
            }

            Running = TRUE;
            if (NT_SUCCESS(KdbpSafeReadMemory(&FrozenState, (PVOID)&Prcb->IpiFrozen, sizeof(FrozenState))) &&
                ((FrozenState & ~IPI_FROZEN_FLAG_ACTIVE) == IPI_FROZEN_STATE_FROZEN) &&
                NT_SUCCESS(KdbpSafeReadMemory(&KdbThreadTrapFrame, &Prcb->ProcessorState.ContextFrame, sizeof(KdbThreadTrapFrame))))
            {
                Captured = TRUE;
            }
            break;
        }

        if (Running && !Captured)
        {
            KdbpPrint("Thread %p is running but has no stable frozen processor context.\n", ThreadId);
            return FALSE;
        }

        if (!Running)
        {
            if (!KdbpKdbTrapFrameFromKernelStack(ThreadSnapshot.Tcb.KernelStack, &KdbThreadTrapFrame))
            {
                KdbpPrint("Thread %p has no readable switched-out context.\n", ThreadId);
                return FALSE;
            }
        }
#if defined(_M_AMD64)
        if (!Running && ThreadSnapshot.Tcb.StateSaveArea != NULL &&
            NT_SUCCESS(KdbpSafeReadMemory(&KdbThreadTrapFrame.FltSave, ThreadSnapshot.Tcb.StateSaveArea, sizeof(KdbThreadTrapFrame.FltSave))))
        {
            KdbThreadTrapFrame.MxCsr = KdbThreadTrapFrame.FltSave.MxCsr;
            KdbThreadTrapFrame.ContextFlags |= CONTEXT_FLOATING_POINT;
        }
#elif defined(_M_ARM64)
        if (!Running && ThreadSnapshot.Tcb.VfpState != NULL)
        {
            KARM64_VFP_STATE VfpState;

            if (NT_SUCCESS(KdbpSafeReadMemory(&VfpState, ThreadSnapshot.Tcb.VfpState, sizeof(VfpState))))
            {
                RtlCopyMemory(KdbThreadTrapFrame.V, VfpState.V, sizeof(KdbThreadTrapFrame.V));
                KdbThreadTrapFrame.Fpcr = VfpState.Fpcr;
                KdbThreadTrapFrame.Fpsr = VfpState.Fpsr;
                KdbThreadTrapFrame.ContextFlags |= CONTEXT_FLOATING_POINT;
            }
        }
#endif
        KdbCurrentTrapFrame = &KdbThreadTrapFrame;
    }
    else /* Switching back to original thread */
    {
        KdbCurrentTrapFrame = &KdbTrapFrame;
    }
    KdbCurrentThread = Thread;

    /* Attach to the thread's process */
    ASSERT(KdbCurrentProcess == PsGetCurrentProcess());
    if (KdbCurrentProcess != Process)
    {
        if (KdbCurrentProcess != KdbOriginalProcess) /* detach from previously attached process */
        {
            KeUnstackDetachProcess(&KdbApcState);
        }

        if (KdbOriginalProcess != Process)
        {
            KeStackAttachProcess(&Process->Pcb, &KdbApcState);
        }

        KdbCurrentProcess = Process;
    }

    return TRUE;
}

/*!\brief Switches to another process/thread context
 *
 * This function switches to the first thread in the specified process.
 *
 * \param ProcessId  Id of the process to switch to.
 *
 * \retval TRUE   Success.
 * \retval FALSE  Failure (i.e. invalid process id)
 */
BOOLEAN
KdbpAttachToProcess(
    PVOID ProcessId)
{
    PEPROCESS Process = NULL;
    PETHREAD Thread;
    EPROCESS ProcessSnapshot;
    ETHREAD ThreadSnapshot;
    PLIST_ENTRY Entry;
    PVOID ThreadId;

    /* The machine is frozen; avoid the CID-table locks used by PsLookup*. */
    if (!KdbpFindProcessById(ProcessId, &Process))
    {
        KdbpPrint("Invalid process id: %p\n", ProcessId);
        return FALSE;
    }

    if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessSnapshot, Process, sizeof(ProcessSnapshot))))
    {
        KdbpPrint("Cannot read process %p.\n", Process);
        return FALSE;
    }

    Entry = ProcessSnapshot.ThreadListHead.Flink;
    if (Entry == &Process->ThreadListHead || Entry == NULL)
    {
        KdbpPrint("No threads in process 0x%p, cannot attach to process!\n", ProcessId);
        return FALSE;
    }

    Thread = CONTAINING_RECORD(Entry, ETHREAD, ThreadListEntry);
    if (!NT_SUCCESS(KdbpSafeReadMemory(&ThreadSnapshot, Thread, sizeof(ThreadSnapshot))))
    {
        KdbpPrint("Cannot read first thread in process %p.\n", ProcessId);
        return FALSE;
    }
    ThreadId = ThreadSnapshot.Cid.UniqueThread;
    return KdbpAttachToThread(ThreadId);
}

/**
 * @brief   Calls the main interactive debugger loop.
 **/
static VOID
KdbpCallMainLoop(VOID)
{
    KdbpCliMainLoop(KdbEnteredOnSingleStep);
}

/**
 * @brief
 * Internal function to enter KDBG and run the specified procedure.
 *
 * Disables interrupts, releases display ownership, ...
 *
 * @param[in]   Procedure
 * The procedure to execute under the KDBG environment.
 * Either execute the main interactive debugger loop (KdbpCallMainLoop)
 * or run the KDBinit file (KdbpCliInterpretInitFile).
 **/
static VOID
KdbpInternalEnter(
    _In_ VOID (*Procedure)(VOID))
{
    PETHREAD Thread;
    PVOID SavedInitialStack, SavedStackBase, SavedKernelStack;
    ULONG SavedStackLimit;
    BOOLEAN DisplayAcquired = FALSE;

    KbdDisableMouse();

    /* Take control of the display */
    if (KdpDebugMode.Screen)
        DisplayAcquired = KdpScreenAcquire();

    /* Call the specified debugger procedure on a different stack */
    Thread = PsGetCurrentThread();
    SavedInitialStack = Thread->Tcb.InitialStack;
    SavedStackBase = Thread->Tcb.StackBase;
    SavedStackLimit = Thread->Tcb.StackLimit;
    SavedKernelStack = Thread->Tcb.KernelStack;
    Thread->Tcb.InitialStack = Thread->Tcb.StackBase = (char*)KdbStack + KDB_STACK_SIZE;
    Thread->Tcb.StackLimit = (ULONG_PTR)KdbStack;
    Thread->Tcb.KernelStack = (char*)KdbStack + KDB_STACK_SIZE;

    KdbpStackSwitchAndCall(KdbStack + KDB_STACK_SIZE - KDB_STACK_RESERVE, Procedure);

    Thread->Tcb.InitialStack = SavedInitialStack;
    Thread->Tcb.StackBase = SavedStackBase;
    Thread->Tcb.StackLimit = SavedStackLimit;
    Thread->Tcb.KernelStack = SavedKernelStack;

    /* Release the display */
    if (KdpDebugMode.Screen)
        KdpScreenRelease(DisplayAcquired);

    KbdEnableMouse();
}

static ULONG
KdbpGetExceptionNumberFromStatus(
    IN NTSTATUS ExceptionCode)
{
    ULONG Ret;

    switch (ExceptionCode)
    {
        case STATUS_INTEGER_DIVIDE_BY_ZERO:
            Ret = 0;
            break;
        case STATUS_SINGLE_STEP:
            Ret = 1;
            break;
        case STATUS_BREAKPOINT:
            Ret = 3;
            break;
        case STATUS_INTEGER_OVERFLOW:
            Ret = 4;
            break;
        case STATUS_ARRAY_BOUNDS_EXCEEDED:
            Ret = 5;
            break;
        case STATUS_ILLEGAL_INSTRUCTION:
            Ret = 6;
            break;
        case STATUS_FLOAT_INVALID_OPERATION:
            Ret = 7;
            break;
        case STATUS_STACK_OVERFLOW:
            Ret = 12;
            break;
        case STATUS_ACCESS_VIOLATION:
            Ret = 14;
            break;
        case STATUS_DATATYPE_MISALIGNMENT:
            Ret = 17;
            break;
        case STATUS_FLOAT_MULTIPLE_TRAPS:
            Ret = 18;
            break;
        case STATUS_ASSERTION_FAILURE:
            Ret = 20;
            break;

        default:
            Ret = RTL_NUMBER_OF(KdbEnterConditions) - 1;
            break;
    }

    return Ret;
}

/* Arch hooks to save/restore the interrupted flags state around the debugger
 * critical section. ARM64 has no EFLAGS to preserve here: interrupts are
 * masked via _disable() and the trap frame already carries the saved PSTATE. */
#if defined(_M_ARM64)
#define KdbpArchSaveInterruptState()          0
#define KdbpArchRestoreInterruptState(State)  ((VOID)(State))
#else
#define KdbpArchSaveInterruptState()          __readeflags()
#define KdbpArchRestoreInterruptState(State)  __writeeflags(State)
#endif

/*!\brief KDB Exception filter
 *
 * Called by the exception dispatcher.
 *
 * \param ExceptionRecord  Unused.
 * \param PreviousMode     UserMode if the exception was raised from umode, otherwise KernelMode.
 * \param Context          Context, IN/OUT parameter.
 * \param TrapFrame        Exception TrapFrame.
 * \param FirstChance      TRUE when called before exception frames were serached,
 *                         FALSE for the second call.
 *
 * \returns KD_CONTINUE_TYPE
 */
KD_CONTINUE_TYPE
KdbEnterDebuggerException(
    IN PEXCEPTION_RECORD64 ExceptionRecord,
    IN KPROCESSOR_MODE PreviousMode,
    IN PCONTEXT Context,
    IN BOOLEAN FirstChance)
{
    KDB_ENTER_CONDITION EnterCondition;
    KD_CONTINUE_TYPE ContinueType = kdHandleException;
    PKDB_BREAKPOINT BreakPoint;
    ULONG ExpNr;
    ULONGLONG ull;
#if !defined(_M_ARM64)
    BOOLEAN Resume = FALSE;
#endif
    ULONG_PTR OldInterruptState;
    BOOLEAN EnterConditionMet = TRUE;
    KIRQL OldIrql;
    NTSTATUS ExceptionCode;
    VOID (*EntryPoint)(VOID) = KdbpCallMainLoop;
#if defined(_M_ARM64)
    KARM64_VFP_STATE LiveVfpState;
    BOOLEAN LiveVfpStateValid = FALSE;
#endif

    ExceptionCode = (ExceptionRecord ? ExceptionRecord->ExceptionCode : STATUS_BREAKPOINT);

    KdbCurrentProcess = PsGetCurrentProcess();

    /* Set continue type to kdContinue for single steps and breakpoints */
    if (ExceptionCode == STATUS_SINGLE_STEP ||
        ExceptionCode == STATUS_BREAKPOINT ||
        ExceptionCode == STATUS_ASSERTION_FAILURE)
    {
        ContinueType = kdContinue;
    }

    /* Check if we should handle the exception. */
    /* FIXME - won't get all exceptions here :( */
    ExpNr = KdbpGetExceptionNumberFromStatus(ExceptionCode);
    EnterCondition = KdbEnterConditions[ExpNr][FirstChance ? 0 : 1];
    if (EnterCondition == KdbDoNotEnter ||
        (EnterCondition == KdbEnterFromUmode && PreviousMode == KernelMode) ||
        (EnterCondition == KdbEnterFromKmode && PreviousMode != KernelMode))
    {
        EnterConditionMet = FALSE;
    }

    /* If we stopped on one of our breakpoints then let the user know */
    KdbLastBreakPointNr = -1;
    KdbEnteredOnSingleStep = FALSE;

    if (FirstChance && (ExceptionCode == STATUS_SINGLE_STEP || ExceptionCode == STATUS_BREAKPOINT) &&
        (KdbLastBreakPointNr = KdbpIsBreakPointOurs(ExceptionCode, Context, ExceptionRecord)) >= 0)
    {
        BreakPoint = KdbBreakPoints + KdbLastBreakPointNr;

        if (ExceptionCode == STATUS_BREAKPOINT)
        {
            /* ... and restore the original instruction */
            if (!NT_SUCCESS(KdbpOverwriteInstruction(KdbCurrentProcess, BreakPoint->Address,
                                                     BreakPoint->Data.SavedInstruction, NULL)))
            {
                KdbPuts("Couldn't restore original instruction after breakpoint! Cannot continue execution.\n");
                KeBugCheck(0); // FIXME: Proper bugcode!
            }

#if !defined(_M_ARM64)
            /* Also since we are past the int3 now, decrement EIP in the
               TrapFrame. This is only needed because KDBG insists on working
               with the TrapFrame instead of with the Context, as it is supposed
               to do. The context has already EIP point to the int3, since
               KiDispatchException accounts for that. Whatever we do here with
               the TrapFrame does not matter anyway, since KiDispatchException
               will overwrite it with the values from the Context! */
            KeSetContextPc(Context, KeGetContextPc(Context) - 1);
#endif
        }

        if ((BreakPoint->Type == KdbBreakPointHardware) &&
            (BreakPoint->Data.Hw.AccessType == KdbAccessExec))
        {
#if !defined(_M_ARM64)
            Resume = TRUE; /* Set the resume flag when continuing execution */
#endif
        }
        /*
         * When a temporary breakpoint is hit we have to make sure that we are
         * in the same context in which it was set, otherwise it could happen
         * that another process/thread hits it before and it gets deleted.
         */
        else if (BreakPoint->Type == KdbBreakPointTemporary &&
                 BreakPoint->Process == KdbCurrentProcess)
        {
#if !defined(_M_ARM64)
            ASSERT((Context->EFlags & EFLAGS_TF) == 0);

            /* Delete the temporary breakpoint which was used to step over or into the instruction */
            KdbpDeleteBreakPoint(-1, BreakPoint);

            if (--KdbNumSingleSteps > 0)
            {
                if ((KdbSingleStepOver && !KdbpStepOverInstruction(KeGetContextPc(Context))) ||
                    (!KdbSingleStepOver && !KdbpStepIntoInstruction(KeGetContextPc(Context))))
                {
                    Context->EFlags |= EFLAGS_TF;
                }

                goto continue_execution; /* return */
            }

            KdbEnteredOnSingleStep = TRUE;
#endif
        }
        /*
         * If we hit a breakpoint set by the debugger we set the single step flag,
         * ignore the next single step and reenable the breakpoint.
         */
        else if (BreakPoint->Type == KdbBreakPointSoftware ||
                 BreakPoint->Type == KdbBreakPointTemporary)
        {
#if !defined(_M_ARM64)
            ASSERT(ExceptionCode == STATUS_BREAKPOINT);
            Context->EFlags |= EFLAGS_TF;
            KdbBreakPointToReenable = BreakPoint;
#endif
        }

        /* Make sure that the breakpoint should be triggered in this context */
        if (!BreakPoint->Global && BreakPoint->Process != KdbCurrentProcess)
        {
            goto continue_execution; /* return */
        }

        /* Check if the condition for the breakpoint is met. */
        if (BreakPoint->Condition)
        {
            /* Setup the KDB trap frame */
            KdbTrapFrame = *Context;

            ull = 0;
            if (!KdbpRpnEvaluateParsedExpression(BreakPoint->Condition, &KdbTrapFrame, &ull, NULL, NULL))
            {
                /* FIXME: Print warning? */
            }
            else if (ull == 0) /* condition is not met */
            {
                goto continue_execution; /* return */
            }
        }

        if (BreakPoint->Type == KdbBreakPointSoftware)
        {
#if defined(_M_ARM64)
            KdbPrintf("\nEntered debugger on breakpoint #%d: EXEC 0x%p\n",
                      KdbLastBreakPointNr, KeGetContextPc(Context));
#else
            KdbPrintf("\nEntered debugger on breakpoint #%d: EXEC 0x%04x:0x%p\n",
                      KdbLastBreakPointNr, Context->SegCs & 0xffff, KeGetContextPc(Context));
#endif
        }
        else if (BreakPoint->Type == KdbBreakPointHardware)
        {
            KdbPrintf("\nEntered debugger on breakpoint #%d: %s %p\n",
                      KdbLastBreakPointNr,
                      (BreakPoint->Data.Hw.AccessType == KdbAccessRead) ? "READ" :
                      ((BreakPoint->Data.Hw.AccessType == KdbAccessWrite) ? "WRITE" :
                      ((BreakPoint->Data.Hw.AccessType == KdbAccessReadWrite) ? "RDWR" : "EXEC")),
                      (PVOID)BreakPoint->Address);
        }
    }
    else if (ExceptionCode == STATUS_SINGLE_STEP)
    {
#if defined(_M_ARM64)
        if (!EnterConditionMet)
        {
            return kdHandleException;
        }

        KdbPuts("\nEntered debugger on unexpected debug trap!\n");
#else
        /* Silently ignore a debugger initiated single step. */
        if ((Context->Dr6 & 0xf) == 0 && KdbBreakPointToReenable)
        {
            /* FIXME: Make sure that the breakpoint was really hit (check bp->Address vs. tf->Eip) */
            BreakPoint = KdbBreakPointToReenable;
            KdbBreakPointToReenable = NULL;
            ASSERT(BreakPoint->Type == KdbBreakPointSoftware ||
                   BreakPoint->Type == KdbBreakPointTemporary);

            /*
             * Reenable the breakpoint we disabled to execute the breakpointed
             * instruction.
             */
            if (!NT_SUCCESS(KdbpOverwriteInstruction(KdbCurrentProcess, BreakPoint->Address, 0xCC,
                                                     &BreakPoint->Data.SavedInstruction)))
            {
                KdbPrintf("Warning: Couldn't reenable breakpoint %d\n",
                          BreakPoint - KdbBreakPoints);
            }

            /* Unset TF if we are no longer single stepping. */
            if (KdbNumSingleSteps == 0)
                Context->EFlags &= ~EFLAGS_TF;

            if (!KdbpEvenThoughWeHaveABreakPointToReenableWeAlsoHaveARealSingleStep)
            {
                goto continue_execution; /* return */
            }
        }

        /* Quoth the raven, 'Nevermore!' */
        KdbpEvenThoughWeHaveABreakPointToReenableWeAlsoHaveARealSingleStep = FALSE;

        /* Check if we expect a single step */
        if ((Context->Dr6 & 0xf) == 0 && KdbNumSingleSteps > 0)
        {
            /*ASSERT((Context->Eflags & EFLAGS_TF) != 0);*/
            if (--KdbNumSingleSteps > 0)
            {
                if ((KdbSingleStepOver && KdbpStepOverInstruction(KeGetContextPc(Context))) ||
                    (!KdbSingleStepOver && KdbpStepIntoInstruction(KeGetContextPc(Context))))
                {
                    Context->EFlags &= ~EFLAGS_TF;
                }
                else
                {
                    Context->EFlags |= EFLAGS_TF;
                }

                goto continue_execution; /* return */
            }
            else
            {
                Context->EFlags &= ~EFLAGS_TF;
                KdbEnteredOnSingleStep = TRUE;
            }
        }
        else
        {
            if (!EnterConditionMet)
            {
                return kdHandleException;
            }

            KdbPuts("\nEntered debugger on unexpected debug trap!\n");
        }
#endif
    }
    else if (ExceptionCode == STATUS_BREAKPOINT)
    {
        /* Do the condition check and banner display only if we enter
         * from a true code breakpoint. We skip those when running the
         * KDBinit file, because it is done via an artificial breakpoint. */
        if (KdbInitFileBuffer)
        {
            EntryPoint = KdbpCliInterpretInitFile;
            goto EnterKdbg;
        }

        if (!EnterConditionMet)
        {
            return kdHandleException;
        }

#if defined(_M_ARM64)
        KdbPrintf("\nEntered debugger on embedded breakpoint at 0x%p.\n",
                  KeGetContextPc(Context));
#else
        KdbPrintf("\nEntered debugger on embedded INT3 at 0x%04x:0x%p.\n",
                  Context->SegCs & 0xffff, KeGetContextPc(Context));
#endif
EnterKdbg:;
    }
    else
    {
        const CHAR *ExceptionString = (ExpNr < RTL_NUMBER_OF(ExceptionNrToString)) ?
                                      ExceptionNrToString[ExpNr] :
                                      "Unknown/User defined exception";

        if (!EnterConditionMet)
        {
            return ContinueType;
        }

        KdbPrintf("\nEntered debugger on %s-chance exception (Exception Code: 0x%x) (%s)\n",
                  FirstChance ? "first" : "last", ExceptionCode, ExceptionString);

        if (ExceptionCode == STATUS_ACCESS_VIOLATION &&
            ExceptionRecord && ExceptionRecord->NumberParameters > 1)
        {
            ULONG_PTR TrapCr2 = ExceptionRecord->ExceptionInformation[1];
            KdbPrintf("Memory at 0x%p could not be accessed\n", TrapCr2);
            KdbPrintf("Memory access type: %lu\n",
                      (ULONG)ExceptionRecord->ExceptionInformation[0]);
        }
    }

    /* Once we enter the debugger we do not expect any more single steps to happen */
    KdbNumSingleSteps = 0;

    /* Update the current process pointer */
    KdbCurrentProcess = KdbOriginalProcess = PsGetCurrentProcess();
    KdbCurrentThread = KdbOriginalThread = PsGetCurrentThread();
    KdbCurrentTrapFrame = &KdbTrapFrame;

    /* Setup the KDB trap frame */
    KdbTrapFrame = *Context;

    /* Enter critical section */
    OldInterruptState = KdbpArchSaveInterruptState();
    _disable();

    /* HACK: Save the current IRQL and pretend we are at dispatch level */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql > DISPATCH_LEVEL)
        KeLowerIrql(DISPATCH_LEVEL);

    /* Exception inside the debugger? Game over. */
    if (InterlockedIncrement(&KdbEntryCount) > 1)
    {
        KdbpArchRestoreInterruptState(OldInterruptState);
        return kdHandleException;
    }

#if defined(_M_AMD64)
    /*
     * Most amd64 trap entries do not populate the debug-register slots in
     * KTRAP_FRAME. CONTEXT_ALL nevertheless asks KeTrapFrameToContext to
     * copy them, which exposes unrelated stack contents as DR0-DR7. KDB is
     * executing on the interrupted processor here, so capture the canonical
     * hardware state before any command reads or edits it.
     */
    KdbTrapFrame.Dr0 = __readdr(0);
    KdbTrapFrame.Dr1 = __readdr(1);
    KdbTrapFrame.Dr2 = __readdr(2);
    KdbTrapFrame.Dr3 = __readdr(3);
    KdbTrapFrame.Dr6 = __readdr(6);
    KdbTrapFrame.Dr7 = __readdr(7);
    KdbTrapFrame.ContextFlags |= CONTEXT_DEBUG_REGISTERS;
#endif

#if defined(_M_ARM64)
    /*
     * Most fatal ARM64 paths deliberately construct only an integer/control
     * CONTEXT. Capture FP/SIMD state directly after recursive-entry rejection,
     * so vregs never reports fabricated zeroes and edits can be restored to
     * the live processor without weakening the fault path's trap conversion.
     */
    RtlZeroMemory(&LiveVfpState, sizeof(LiveVfpState));
    KiSaveVfpState(&LiveVfpState);
    RtlCopyMemory(KdbTrapFrame.V, LiveVfpState.V, sizeof(KdbTrapFrame.V));
    KdbTrapFrame.Fpcr = LiveVfpState.Fpcr;
    KdbTrapFrame.Fpsr = LiveVfpState.Fpsr;
    KdbTrapFrame.ContextFlags |= CONTEXT_FLOATING_POINT;
    LiveVfpStateValid = TRUE;
#endif

    if (ExceptionRecord != NULL)
    {
        KdbCurrentExceptionRecord = *ExceptionRecord;
        KdbCurrentExceptionRecordValid = TRUE;
    }
    else
    {
        RtlZeroMemory(&KdbCurrentExceptionRecord, sizeof(KdbCurrentExceptionRecord));
        KdbCurrentExceptionRecordValid = FALSE;
    }

    /* Enter KDBG proper and run either the main loop or the KDBinit file */
    KdbpInternalEnter(EntryPoint);

    /* Check if we should single step */
#if !defined(_M_ARM64)
    if (KdbNumSingleSteps > 0)
    {
        /* Variable explains itself! */
        KdbpEvenThoughWeHaveABreakPointToReenableWeAlsoHaveARealSingleStep = TRUE;

        if ((KdbSingleStepOver && KdbpStepOverInstruction(KeGetContextPc(KdbCurrentTrapFrame))) ||
            (!KdbSingleStepOver && KdbpStepIntoInstruction(KeGetContextPc(KdbCurrentTrapFrame))))
        {
            ASSERT((KdbCurrentTrapFrame->EFlags & EFLAGS_TF) == 0);
            /*KdbCurrentTrapFrame->EFlags &= ~EFLAGS_TF;*/
        }
        else
        {
            KdbTrapFrame.EFlags |= EFLAGS_TF;
        }
    }
#endif

    /* We can't update the current thread's trapframe 'cause it might not have one */

    /* Detach from attached process */
    if (KdbCurrentProcess != KdbOriginalProcess)
    {
        KeUnstackDetachProcess(&KdbApcState);
    }

#if defined(_M_ARM64)
    if (LiveVfpStateValid)
    {
        RtlCopyMemory(LiveVfpState.V, KdbTrapFrame.V, sizeof(LiveVfpState.V));
        LiveVfpState.Fpcr = KdbTrapFrame.Fpcr;
        LiveVfpState.Fpsr = KdbTrapFrame.Fpsr;
        KiRestoreVfpState(&LiveVfpState);
    }
#endif

    /* Update the exception Context */
    *Context = KdbTrapFrame;

    /* Decrement the entry count */
    InterlockedDecrement(&KdbEntryCount);

    /* HACK: Raise back to old IRQL */
    if (OldIrql > DISPATCH_LEVEL)
        KeRaiseIrql(OldIrql, &OldIrql);

    /* Leave critical section */
    KdbpArchRestoreInterruptState(OldInterruptState);

    /* Check if user requested a bugcheck */
    if (KdbpBugCheckRequested)
    {
        /* Clear the flag and bugcheck the system */
        KdbpBugCheckRequested = FALSE;
        KeBugCheck(MANUALLY_INITIATED_CRASH);
    }

continue_execution:
    /* Clear debug status */
    if (ExceptionCode == STATUS_BREAKPOINT) /* FIXME: Why clear DR6 on INT3? */
    {
#if !defined(_M_ARM64)
        /* Set the RF flag so we don't trigger the same breakpoint again. */
        if (Resume)
        {
            Context->EFlags |= EFLAGS_RF;
        }

        /* Clear dr6 status flags. */
        Context->Dr6 &= ~0x0000e00f;

        if (!(KdbEnteredOnSingleStep && KdbSingleStepOver))
        {
            /* Skip the current instruction */
            KeSetContextPc(Context, KeGetContextPc(Context) + KD_BREAKPOINT_SIZE);
        }
#else
        if (KdbLastBreakPointNr < 0)
        {
            KeSetContextPc(Context, KeGetContextPc(Context) + KD_BREAKPOINT_SIZE);
        }
#endif
    }

    return ContinueType;
}

VOID
KdbpGetCommandLineSettings(
    _In_ PCSTR p1)
{
#define CONST_STR_LEN(x) (sizeof(x)/sizeof(x[0]) - 1)

    while (p1 && *p1)
    {
        /* Skip leading whitespace */
        while (*p1 == ' ') ++p1;

        if (!_strnicmp(p1, "FIRSTCHANCE", CONST_STR_LEN("FIRSTCHANCE")))
        {
            p1 += CONST_STR_LEN("FIRSTCHANCE");
            KdbpSetEnterCondition(-1, TRUE, KdbEnterAlways);
        }

        /* Move on to the next option */
        p1 = strchr(p1, ' ');
    }
}

NTSTATUS
KdbpSafeReadMemory(
    OUT PVOID Dest,
    IN PVOID Src,
    IN ULONG Bytes)
{
    return KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Src,
                               Dest,
                               Bytes,
                               0,
                               MMDBG_COPY_UNSAFE,
                               NULL);
}

NTSTATUS
KdbpSafeWriteMemory(
    OUT PVOID Dest,
    IN PVOID Src,
    IN ULONG Bytes)
{
    return KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Dest,
                               Src,
                               Bytes,
                               0,
                               MMDBG_COPY_UNSAFE | MMDBG_COPY_WRITE,
                               NULL);
}
