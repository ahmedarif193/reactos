/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/kdbg/kdb_shim.c
 * PURPOSE:         ARM64 KDBG implementation providing crash output with
 *                  stack traces, register dumps, and module information
 *
 * This file provides ARM64-specific implementations of KDBG functions.
 * Unlike x86/AMD64 which use EFlags and hardware debug registers,
 * ARM64 uses CPSR/PSTATE and a different debug model.
 */

#include <ntoskrnl.h>
#include <kdbg/kdb.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* Provide globals expected by kdbg.c and other code */
volatile PCHAR KdbInitFileBuffer = NULL;
PEPROCESS KdbCurrentProcess = NULL;
PETHREAD KdbCurrentThread = NULL;
LONG KdbLastBreakPointNr = -1;
ULONG KdbNumSingleSteps = 0;
BOOLEAN KdbSingleStepOver = FALSE;
PKDB_KTRAP_FRAME KdbCurrentTrapFrame = NULL;
const CSTRING KdbPromptStr = RTL_CONSTANT_STRING("kdb:> ");

/* PRIVATE FUNCTIONS *********************************************************/

/* Forward declarations */
VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);
NTSTATUS
KdbpSafeReadMemory(
    OUT PVOID Dest,
    IN PVOID Src,
    IN ULONG Bytes);

/* Simple print helpers using KiArm64BootStageLog which works during exceptions */
VOID
KdbPuts(
    _In_ PCSTR String)
{
    /* Use KiArm64BootStageLog for reliability during exception handling */
    if (String)
        KiArm64BootStageLog(String);
}

VOID
__cdecl
KdbPrintf(
    _In_ PCSTR Format,
    ...)
{
    CHAR Buffer[256];
    va_list ap;
    va_start(ap, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, ap);
    Buffer[sizeof(Buffer) - 1] = '\0';
    va_end(ap);
    KiArm64BootStageLog(Buffer);
}

/* Print with paging support - for ARM64 just print directly */
VOID
KdbpPrint(
    _In_ PSTR Format,
    _In_ ...)
{
    CHAR Buffer[256];
    va_list ap;
    va_start(ap, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, ap);
    Buffer[sizeof(Buffer) - 1] = '\0';
    va_end(ap);
    KiArm64BootStageLog(Buffer);
}

VOID
KdbpPrintUnicodeString(
    _In_ PCUNICODE_STRING String)
{
    CHAR Buffer[128];
    ULONG i, Len;
    if (!String || !String->Buffer)
        return;
    /* Convert to ASCII and print */
    Len = String->Length / sizeof(WCHAR);
    if (Len >= sizeof(Buffer))
        Len = sizeof(Buffer) - 1;
    for (i = 0; i < Len; i++)
        Buffer[i] = (CHAR)(String->Buffer[i] & 0x7F);
    Buffer[Len] = '\0';
    KiArm64BootStageLog(Buffer);
}

/* PUBLIC FUNCTIONS **********************************************************/

/**
 * @brief Initialize KDBG for ARM64
 */
NTSTATUS
NTAPI
KdbInitialize(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    UNREFERENCED_PARAMETER(DispatchTable);
    UNREFERENCED_PARAMETER(BootPhase);

    /* ARM64 KDBG is always ready - no special initialization needed */
    return STATUS_SUCCESS;
}

/**
 * @brief Parse hex number from string
 */
BOOLEAN
NTAPI
KdbpGetHexNumber(
    _In_ PCHAR pszNum,
    _Out_ ULONG_PTR *pulValue)
{
    NTSTATUS Status;
    ANSI_STRING AnsiString;
    ULONG Value;

    if (!pszNum || !pulValue)
        return FALSE;

    RtlInitAnsiString(&AnsiString, pszNum);

    /* RtlCharToInteger expects decimal by default; we need hex */
    Status = RtlCharToInteger(pszNum, 16, &Value);
    if (!NT_SUCCESS(Status))
        return FALSE;

    *pulValue = (ULONG_PTR)Value;
    return TRUE;
}

/**
 * @brief Get command history entry (stub - no history for ARM64 shim)
 */
PCSTR
KdbGetHistoryEntry(
    _Inout_ PLONG NextIndex,
    _In_ BOOLEAN Next)
{
    UNREFERENCED_PARAMETER(NextIndex);
    UNREFERENCED_PARAMETER(Next);
    return NULL;
}

/**
 * @brief Check if a pointer looks like a valid kernel address
 */
static BOOLEAN
KdbpIsValidKernelPointer(
    _In_ PVOID Pointer)
{
    ULONG_PTR Addr = (ULONG_PTR)Pointer;

    /* ARM64 kernel addresses are in the high VA range (0xFFFF8000...) */
    if (Addr < 0xFFFF000000000000ULL)
        return FALSE;

    /* NULL pointer check */
    if (Addr == 0)
        return FALSE;

    return TRUE;
}

/**
 * @brief Find module containing an address
 */
BOOLEAN
KdbpSymFindModule(
    IN PVOID Address OPTIONAL,
    IN INT Index OPTIONAL,
    OUT PLDR_DATA_TABLE_ENTRY *pLdrEntry)
{
    PLIST_ENTRY ListHead, NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    INT CurrentIndex = 0;
    ULONG IterCount = 0;
    const ULONG MaxIter = 100; /* Safety limit */

    if (!pLdrEntry)
        return FALSE;

    *pLdrEntry = NULL;

    /* Check if PsLoadedModuleList is valid */
    ListHead = &PsLoadedModuleList;
    if (!KdbpIsValidKernelPointer(ListHead) ||
        !KdbpIsValidKernelPointer(ListHead->Flink))
    {
        return FALSE;
    }

    NextEntry = ListHead->Flink;

    while (NextEntry != ListHead && IterCount < MaxIter)
    {
        /* Validate the pointer before dereferencing */
        if (!KdbpIsValidKernelPointer(NextEntry))
            break;

        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* Validate LdrEntry pointer */
        if (!KdbpIsValidKernelPointer(LdrEntry) ||
            !KdbpIsValidKernelPointer(LdrEntry->DllBase))
        {
            NextEntry = NextEntry->Flink;
            IterCount++;
            continue;
        }

        if (Address)
        {
            /* Check if address is within this module */
            ULONG_PTR Base = (ULONG_PTR)LdrEntry->DllBase;
            ULONG_PTR End = Base + LdrEntry->SizeOfImage;

            if ((ULONG_PTR)Address >= Base && (ULONG_PTR)Address < End)
            {
                *pLdrEntry = LdrEntry;
                return TRUE;
            }
        }
        else if (Index >= 0)
        {
            /* Find by index */
            if (CurrentIndex == Index)
            {
                *pLdrEntry = LdrEntry;
                return TRUE;
            }
            CurrentIndex++;
        }

        NextEntry = NextEntry->Flink;
        IterCount++;
    }

    return FALSE;
}

/**
 * @brief Process symbol load/unload for a module (stub)
 */
VOID
KdbSymProcessSymbols(
    _Inout_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ BOOLEAN Load)
{
    /* For ARM64 shim, we don't process debug symbols */
    UNREFERENCED_PARAMETER(LdrEntry);
    UNREFERENCED_PARAMETER(Load);
}

/**
 * @brief Print module information for an address
 */
static VOID
KdbpPrintAddressInModule(
    _In_ PVOID Address)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    /* Don't attempt module lookup for obviously invalid addresses */
    if (!KdbpIsValidKernelPointer(Address))
        return;

    if (KdbpSymFindModule(Address, -1, &LdrEntry))
    {
        /* Validate the UNICODE_STRING before using it */
        if (LdrEntry->BaseDllName.Buffer != NULL &&
            KdbpIsValidKernelPointer(LdrEntry->BaseDllName.Buffer) &&
            LdrEntry->BaseDllName.Length > 0 &&
            LdrEntry->BaseDllName.Length <= LdrEntry->BaseDllName.MaximumLength)
        {
            ULONG_PTR Offset = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry->DllBase;
            KdbPrintf("<%wZ+0x%lx>", &LdrEntry->BaseDllName, (ULONG)Offset);
        }
    }
}

/**
 * @brief Print all ARM64 registers from context
 */
static VOID
KdbpPrintArm64Registers(
    _In_ PCONTEXT Context)
{
    /* Validate Context pointer before accessing its fields */
    if (!Context || !KdbpIsValidKernelPointer(Context))
    {
        KdbPrintf("\n=== ARM64 Register Dump ===\n");
        KdbPrintf("  (Context not available)\n");
        return;
    }

    if (!(Context->ContextFlags & CONTEXT_ARM64))
    {
        KdbPrintf("\n=== ARM64 Register Dump ===\n");
        KdbPrintf("  (Invalid context flags: 0x%lx)\n", Context->ContextFlags);
        return;
    }

    KdbPrintf("\n=== ARM64 Register Dump ===\n");

    /* Program Counter and Stack Pointer */
    KdbPrintf("   PC   0x%016llx  ", (ULONGLONG)Context->Pc);
    KdbpPrintAddressInModule((PVOID)Context->Pc);
    KdbPrintf("\n");
    KdbPrintf("   SP   0x%016llx     LR   0x%016llx  ",
              (ULONGLONG)Context->Sp, (ULONGLONG)Context->Lr);
    KdbpPrintAddressInModule((PVOID)Context->Lr);
    KdbPrintf("\n");
    KdbPrintf("   FP   0x%016llx\n", (ULONGLONG)Context->Fp);

    /* General purpose registers X0-X28 */
    KdbPrintf("   X0   0x%016llx     X1   0x%016llx\n",
              (ULONGLONG)Context->X0, (ULONGLONG)Context->X1);
    KdbPrintf("   X2   0x%016llx     X3   0x%016llx\n",
              (ULONGLONG)Context->X2, (ULONGLONG)Context->X3);
    KdbPrintf("   X4   0x%016llx     X5   0x%016llx\n",
              (ULONGLONG)Context->X4, (ULONGLONG)Context->X5);
    KdbPrintf("   X6   0x%016llx     X7   0x%016llx\n",
              (ULONGLONG)Context->X6, (ULONGLONG)Context->X7);
    KdbPrintf("   X8   0x%016llx     X9   0x%016llx\n",
              (ULONGLONG)Context->X8, (ULONGLONG)Context->X9);
    KdbPrintf("  X10   0x%016llx    X11   0x%016llx\n",
              (ULONGLONG)Context->X10, (ULONGLONG)Context->X11);
    KdbPrintf("  X12   0x%016llx    X13   0x%016llx\n",
              (ULONGLONG)Context->X12, (ULONGLONG)Context->X13);
    KdbPrintf("  X14   0x%016llx    X15   0x%016llx\n",
              (ULONGLONG)Context->X14, (ULONGLONG)Context->X15);
    KdbPrintf("  X16   0x%016llx    X17   0x%016llx\n",
              (ULONGLONG)Context->X16, (ULONGLONG)Context->X17);
    KdbPrintf("  X18   0x%016llx    X19   0x%016llx\n",
              (ULONGLONG)Context->X18, (ULONGLONG)Context->X19);
    KdbPrintf("  X20   0x%016llx    X21   0x%016llx\n",
              (ULONGLONG)Context->X20, (ULONGLONG)Context->X21);
    KdbPrintf("  X22   0x%016llx    X23   0x%016llx\n",
              (ULONGLONG)Context->X22, (ULONGLONG)Context->X23);
    KdbPrintf("  X24   0x%016llx    X25   0x%016llx\n",
              (ULONGLONG)Context->X24, (ULONGLONG)Context->X25);
    KdbPrintf("  X26   0x%016llx    X27   0x%016llx\n",
              (ULONGLONG)Context->X26, (ULONGLONG)Context->X27);
    KdbPrintf("  X28   0x%016llx\n", (ULONGLONG)Context->X28);

    /* CPSR/PSTATE with decoded flags */
    KdbPrintf("\n  CPSR  0x%08x", Context->Cpsr);
    if (Context->Cpsr & (1 << 31)) KdbPrintf(" N");  /* Negative */
    if (Context->Cpsr & (1 << 30)) KdbPrintf(" Z");  /* Zero */
    if (Context->Cpsr & (1 << 29)) KdbPrintf(" C");  /* Carry */
    if (Context->Cpsr & (1 << 28)) KdbPrintf(" V");  /* Overflow */
    if (Context->Cpsr & (1 << 9))  KdbPrintf(" E");  /* Endianness */
    if (Context->Cpsr & (1 << 8))  KdbPrintf(" A");  /* SError mask */
    if (Context->Cpsr & (1 << 7))  KdbPrintf(" I");  /* IRQ mask */
    if (Context->Cpsr & (1 << 6))  KdbPrintf(" F");  /* FIQ mask */
    KdbPrintf(" EL%d", (Context->Cpsr >> 2) & 3);
    if (Context->Cpsr & (1 << 0))
        KdbPrintf(" SP_ELx");
    else
        KdbPrintf(" SP_EL0");
    KdbPrintf("\n");
}

/**
 * @brief Print stack backtrace using ARM64 unwind information
 */
static VOID
KdbpPrintStackTrace(
    _In_ PCONTEXT Context,
    _In_ ULONG MaxFrames)
{
    CONTEXT Ctx;
    ULONG64 ControlPc;
    ULONG64 PreviousPc;
    ULONG Frames = 0;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 ImageBase;
    PVOID HandlerData = NULL;
    ULONG64 EstablisherFrame = 0;
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    KdbPrintf("\n=== Stack Trace ===\n");

    /* Validate Context pointer */
    if (!Context || !KdbpIsValidKernelPointer(Context))
    {
        KdbPrintf("  (Context not available)\n");
        return;
    }

    if (!(Context->ContextFlags & CONTEXT_ARM64))
    {
        KdbPrintf("  (Invalid context)\n");
        return;
    }

    /* Make a working copy of the context */
    Ctx = *Context;
    ControlPc = Ctx.Pc;
    PreviousPc = 0;

    while (Frames < MaxFrames && ControlPc)
    {
        /* Detect infinite loop by checking if PC hasn't changed */
        if (ControlPc == PreviousPc)
        {
            break;
        }

        /* Print frame number and address */
        KdbPrintf("  #%02lu  0x%016llx  ", Frames, (ULONGLONG)ControlPc);

        /* Try to find and print module information */
        __try
        {
            if (KdbpSymFindModule((PVOID)ControlPc, -1, &LdrEntry))
            {
                ULONG_PTR Offset = (ULONG_PTR)ControlPc - (ULONG_PTR)LdrEntry->DllBase;
                KdbPrintf("%wZ+0x%lx", &LdrEntry->BaseDllName, (ULONG)Offset);
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Module lookup faulted - skip it */
        }
        KdbPrintf("\n");

        /* Save current PC before attempting unwind */
        PreviousPc = ControlPc;

        /* Look up function entry for unwinding */
        FunctionEntry = RtlLookupFunctionEntry((ULONG_PTR)ControlPc,
                                               (PULONG_PTR)&ImageBase,
                                               NULL);
        if (!FunctionEntry)
        {
            /* No unwind info - try frame pointer based unwinding */
            if (Ctx.Fp != 0 && Ctx.Fp != Ctx.Sp)
            {
                /* ARM64 frame: [FP] = previous FP, [FP+8] = return address */
                ULONG64 *FramePtr = (ULONG64 *)Ctx.Fp;
                NTSTATUS Status;
                ULONG64 NewFp = 0, NewLr = 0;

                Status = KdbpSafeReadMemory(&NewFp, FramePtr, sizeof(ULONG64));
                if (NT_SUCCESS(Status))
                {
                    Status = KdbpSafeReadMemory(&NewLr, FramePtr + 1, sizeof(ULONG64));
                }

                if (NT_SUCCESS(Status) && NewLr != 0)
                {
                    Ctx.Fp = NewFp;
                    ControlPc = NewLr;
                    Frames++;
                    continue;
                }
            }
            /* Cannot unwind further */
            break;
        }

        /* Use RtlVirtualUnwind to get the next frame */
        RtlVirtualUnwind(0, /* UNW_FLAG_NHANDLER */
                         ImageBase,
                         ControlPc,
                         FunctionEntry,
                         &Ctx,
                         &HandlerData,
                         &EstablisherFrame,
                         NULL);

        ControlPc = Ctx.Pc;
        Frames++;
    }

    if (Frames == 0)
    {
        KdbPrintf("  (Unable to unwind stack)\n");
    }
}

/**
 * @brief Print loaded modules list
 */
static VOID
KdbpPrintModules(VOID)
{
    PLIST_ENTRY ListHead, NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG Count = 0;
    const ULONG MaxIter = 30;

    KdbPrintf("\n=== Loaded Modules ===\n");

    ListHead = &PsLoadedModuleList;

    /* Check if list is valid */
    if (!KdbpIsValidKernelPointer(ListHead) ||
        !KdbpIsValidKernelPointer(ListHead->Flink))
    {
        KdbPrintf("  (Module list not available)\n");
        return;
    }

    NextEntry = ListHead->Flink;

    while (NextEntry != ListHead && Count < MaxIter)
    {
        /* Validate the entry pointer */
        if (!KdbpIsValidKernelPointer(NextEntry))
            break;

        LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        /* Validate LdrEntry and its fields */
        if (KdbpIsValidKernelPointer(LdrEntry) &&
            KdbpIsValidKernelPointer(LdrEntry->DllBase) &&
            LdrEntry->BaseDllName.Buffer != NULL &&
            KdbpIsValidKernelPointer(LdrEntry->BaseDllName.Buffer) &&
            LdrEntry->BaseDllName.Length > 0 &&
            LdrEntry->BaseDllName.Length <= LdrEntry->BaseDllName.MaximumLength)
        {
            KdbPrintf("  %wZ @ 0x%p - 0x%p (0x%lx bytes)\n",
                      &LdrEntry->BaseDllName,
                      LdrEntry->DllBase,
                      (PUCHAR)LdrEntry->DllBase + LdrEntry->SizeOfImage,
                      LdrEntry->SizeOfImage);
        }

        NextEntry = NextEntry->Flink;
        Count++;
    }

    if (Count == 0)
    {
        KdbPrintf("  (No modules loaded yet)\n");
    }
}

/* Guard against recursive KDBG entry */
static volatile LONG KdbEntered = 0;

/**
 * @brief Main debugger entry point for ARM64
 *
 * This is called when an exception occurs. It prints comprehensive
 * crash information including registers, stack trace, and module info.
 */
KD_CONTINUE_TYPE
KdbEnterDebuggerException(
    IN PEXCEPTION_RECORD64 ExceptionRecord,
    IN KPROCESSOR_MODE PreviousMode,
    IN OUT PCONTEXT Context,
    IN BOOLEAN FirstChance)
{
    UNREFERENCED_PARAMETER(PreviousMode);

    /* Guard against nested entry - this can happen if printing causes another fault */
    if (InterlockedCompareExchange(&KdbEntered, 1, 0) != 0)
    {
        /* Already in KDBG - don't recurse */
        return kdContinue;
    }

    /* Save current context for other KDBG functions */
    /* Note: KdbCurrentTrapFrame expects a trap frame, but we only have a CONTEXT.
     * Set to NULL to avoid type confusion - KDBG on ARM64 doesn't use trap frames. */
    KdbCurrentTrapFrame = NULL;
    KdbCurrentProcess = PsGetCurrentProcess();
    KdbCurrentThread = PsGetCurrentThread();

    /* Print exception banner */
    KdbPrintf("\n");
    KdbPrintf("******************************************************************************\n");
    KdbPrintf("*                                                                            *\n");
    KdbPrintf("*  ARM64 Kernel Debugger - %s-chance Exception                           *\n",
              FirstChance ? "First" : "Last ");
    KdbPrintf("*                                                                            *\n");
    KdbPrintf("******************************************************************************\n");

    /* Print exception information */
    if (ExceptionRecord)
    {
        KdbPrintf("\nException Code:    0x%08lx", (ULONG)ExceptionRecord->ExceptionCode);

        /* Decode common exception codes */
        switch (ExceptionRecord->ExceptionCode)
        {
            case STATUS_ACCESS_VIOLATION:
                KdbPrintf(" (ACCESS_VIOLATION)");
                break;
            case STATUS_BREAKPOINT:
                KdbPrintf(" (BREAKPOINT)");
                break;
            case STATUS_SINGLE_STEP:
                KdbPrintf(" (SINGLE_STEP)");
                break;
            case STATUS_INTEGER_DIVIDE_BY_ZERO:
                KdbPrintf(" (DIVIDE_BY_ZERO)");
                break;
            case STATUS_ILLEGAL_INSTRUCTION:
                KdbPrintf(" (ILLEGAL_INSTRUCTION)");
                break;
            case STATUS_ASSERTION_FAILURE:
                KdbPrintf(" (ASSERTION_FAILURE)");
                break;
            case STATUS_IN_PAGE_ERROR:
                KdbPrintf(" (IN_PAGE_ERROR)");
                break;
            default:
                break;
        }
        KdbPrintf("\n");

        KdbPrintf("Exception Flags:   0x%08lx\n", ExceptionRecord->ExceptionFlags);
        KdbPrintf("Exception Address: 0x%016llx  ",
                  (ULONGLONG)ExceptionRecord->ExceptionAddress);
        KdbpPrintAddressInModule((PVOID)(ULONG_PTR)ExceptionRecord->ExceptionAddress);
        KdbPrintf("\n");

        /* Print access violation details */
        if ((ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION ||
             ExceptionRecord->ExceptionCode == STATUS_IN_PAGE_ERROR) &&
            ExceptionRecord->NumberParameters >= 2)
        {
            PCSTR AccessType;
            switch (ExceptionRecord->ExceptionInformation[0])
            {
                case 0: AccessType = "read"; break;
                case 1: AccessType = "write"; break;
                case 8: AccessType = "execute"; break;
                default: AccessType = "unknown"; break;
            }
            KdbPrintf("\nAttempted to %s address 0x%016llx\n",
                      AccessType,
                      (ULONGLONG)ExceptionRecord->ExceptionInformation[1]);
        }
    }

    /* Print register dump */
    KdbpPrintArm64Registers(Context);

    /* Print stack trace */
    KdbpPrintStackTrace(Context, 16);

    /* Print loaded modules (useful for crash analysis) */
    KdbpPrintModules();

    KdbPrintf("\n******************************************************************************\n\n");

    /* Clear saved context */
    KdbCurrentTrapFrame = NULL;

    /* Release the guard */
    InterlockedExchange(&KdbEntered, 0);

    /* Continue - let the system handle the exception */
    return kdContinue;
}

/* Stub functions that may be referenced but aren't needed for ARM64 shim */

VOID
KdbpGetCommandLineSettings(
    _In_ PCSTR p1)
{
    UNREFERENCED_PARAMETER(p1);
}

NTSTATUS
KdbpSafeReadMemory(
    OUT PVOID Dest,
    IN PVOID Src,
    IN ULONG Bytes)
{
    /* Simple implementation - could add exception handling */
    __try
    {
        RtlCopyMemory(Dest, Src, Bytes);
        return STATUS_SUCCESS;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode();
    }
}

NTSTATUS
KdbpSafeWriteMemory(
    OUT PVOID Dest,
    IN PVOID Src,
    IN ULONG Bytes)
{
    __try
    {
        RtlCopyMemory(Dest, Src, Bytes);
        return STATUS_SUCCESS;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return GetExceptionCode();
    }
}

/* KdbpGetInstLength and KdbpDisassemble are provided by arm64-dis.c */

/**
 * @brief Register/deregister CLI callback (stub - no CLI for ARM64 shim)
 */
BOOLEAN
NTAPI
KdbRegisterCliCallback(
    PVOID Callback,
    BOOLEAN Deregister)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Deregister);
    /* ARM64 shim doesn't support CLI callbacks */
    return FALSE;
}

/* Symbol-related stubs */
BOOLEAN
KdbSymInit(
    _In_ ULONG BootPhase)
{
    UNREFERENCED_PARAMETER(BootPhase);
    return TRUE;
}

BOOLEAN
KdbSymPrintAddress(
    IN PVOID Address,
    IN PCONTEXT Context)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    UNREFERENCED_PARAMETER(Context);

    if (KdbpSymFindModule(Address, -1, &LdrEntry))
    {
        ULONG_PTR Offset = (ULONG_PTR)Address - (ULONG_PTR)LdrEntry->DllBase;
        KdbPrintf("%wZ+0x%lx", &LdrEntry->BaseDllName, (ULONG)Offset);
        return TRUE;
    }
    return FALSE;
}

/* KDB print functions for kd subsystem */
VOID
KdbPrintString(
    _In_ const CSTRING* Output)
{
    if (Output && Output->Buffer)
    {
        KdbPutsN(Output->Buffer, Output->Length);
    }
}

VOID
KdbPutsN(
    _In_ PCCH String,
    _In_ USHORT Length)
{
    CHAR Buffer[256];
    USHORT CopyLen = (Length < sizeof(Buffer) - 1) ? Length : sizeof(Buffer) - 1;
    RtlCopyMemory(Buffer, String, CopyLen);
    Buffer[CopyLen] = '\0';
    KiArm64BootStageLog(Buffer);
}

USHORT
KdbPromptString(
    _In_ const CSTRING* PromptString,
    _Inout_ PSTRING ResponseString)
{
    /* ARM64 shim doesn't support interactive prompts */
    UNREFERENCED_PARAMETER(PromptString);
    if (ResponseString)
    {
        ResponseString->Length = 0;
    }
    return 0;
}

SIZE_T
KdbPrompt(
    _In_ PCSTR Prompt,
    _Out_ PCHAR Buffer,
    _In_ SIZE_T Size)
{
    /* ARM64 shim doesn't support interactive prompts */
    UNREFERENCED_PARAMETER(Prompt);
    if (Buffer && Size > 0)
    {
        Buffer[0] = '\0';
    }
    return 0;
}

/* EOF */
