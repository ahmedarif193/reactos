/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/kdbg/kdb_shim.c
 * PURPOSE:         Minimal KDBG shim for ARM64 to log exception banners
 */

#include <ntoskrnl.h>
#include <kdbg/kdb.h>

#define NDEBUG
#include <debug.h>

/* Simple print helpers mapping to KD */
VOID KdbPuts(_In_ PCSTR s) { KdpDprintf("%s", s); }
VOID __cdecl KdbPrintf(_In_ PCSTR fmt, ...) {
    va_list ap; va_start(ap, fmt);
    _vsnprintf(KdpMessageBuffer, sizeof(KdpMessageBuffer), fmt, ap);
    va_end(ap);
    KdpDprintf("%s", KdpMessageBuffer);
}

/* Stubs used by various KDBG-aware helpers when KDBG is enabled */
NTSTATUS NTAPI
KdbInitialize(_In_ PKD_DISPATCH_TABLE DispatchTable,
              _In_ ULONG BootPhase)
{
    UNREFERENCED_PARAMETER(DispatchTable);
    UNREFERENCED_PARAMETER(BootPhase);
    return STATUS_SUCCESS;
}

VOID
KdbpPrint(_In_ PSTR Format, _In_ ...)
{
    va_list ap; va_start(ap, Format);
    _vsnprintf(KdpMessageBuffer, sizeof(KdpMessageBuffer), Format, ap);
    va_end(ap);
    KdpDprintf("%s", KdpMessageBuffer);
}

VOID
KdbpPrintUnicodeString(_In_ PCUNICODE_STRING String)
{
    if (!String || !String->Buffer) return;
    /* Print as best-effort: convert to ANSI? For now, length only */
    KdpDprintf("<UNICODE_STRING len=%hu>\n", String->Length);
}

BOOLEAN NTAPI
KdbpGetHexNumber(_In_ PCHAR pszNum,
                 _Out_ ULONG_PTR *pulValue)
{
    ULONG value32;
    if (!pszNum || !pulValue) return FALSE;
    if (NT_SUCCESS(RtlCharToInteger(pszNum, 16, &value32)))
    {
        *pulValue = (ULONG_PTR)value32;
        return TRUE;
    }
    return FALSE;
}

PCSTR
KdbGetHistoryEntry(_Inout_ PLONG NextIndex,
                   _In_ BOOLEAN Next)
{
    UNREFERENCED_PARAMETER(NextIndex);
    UNREFERENCED_PARAMETER(Next);
    return NULL;
}

/* Provide a prompt constant expected by kd/kdio */
const CSTRING KdbPromptStr = RTL_CONSTANT_STRING("kdb:> ");

/* Minimal banner on exception; no interactive CLI */
KD_CONTINUE_TYPE
KdbEnterDebuggerException(IN PEXCEPTION_RECORD64 ExceptionRecord,
                          IN KPROCESSOR_MODE PreviousMode,
                          IN OUT PCONTEXT Context,
                          IN BOOLEAN FirstChance)
{
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(Context);

    /* Emit parity-like banner */
    KdbPrintf("\nEntered debugger on %s-chance exception (Exception Code: 0x%08lx)\n",
              FirstChance ? "first" : "last",
              (ULONG)ExceptionRecord->ExceptionCode);

    if ((ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION) &&
        (ExceptionRecord->NumberParameters >= 2))
    {
        PVOID FaultVa = (PVOID)(ULONG_PTR)ExceptionRecord->ExceptionInformation[1];
        KdbPrintf("Memory at 0x%p could not be accessed\n", FaultVa);
    }

    /* Log the PC if present in Context */
    if (Context && (Context->ContextFlags & CONTEXT_ARM64))
    {
        KdbPrintf("PC=%p SP=%p CPSR=0x%08lx\n",
                  (PVOID)(ULONG_PTR)Context->Pc,
                  (PVOID)(ULONG_PTR)Context->Sp,
                  (ULONG)Context->Cpsr);
    }

    /* Minimal stack backtrace using ARM64 unwind metadata */
    if (Context && (Context->ContextFlags & CONTEXT_ARM64))
    {
        CONTEXT Ctx = *Context; /* local working copy */
        ULONG64 ControlPc = Ctx.Pc;
        ULONG frames = 0;
        /* Declarations per RTL */
        PRUNTIME_FUNCTION FunctionEntry;
        ULONG64 ImageBase;
        PVOID HandlerData = NULL;
        ULONG64 EstablisherFrame = 0;
        /* UNW_FLAG_NHANDLER is 0 on ARM64 */
#ifndef UNW_FLAG_NHANDLER
#define UNW_FLAG_NHANDLER 0x0
#endif

        KdbPrintf("Backtrace:\n");
        while (frames < 8 && ControlPc)
        {
            FunctionEntry = RtlLookupFunctionEntry((ULONG_PTR)ControlPc,
                                                   (PULONG_PTR)&ImageBase,
                                                   NULL);
            KdbPrintf("  #%lu %p\n", frames, (PVOID)(ULONG_PTR)ControlPc);
            if (!FunctionEntry)
            {
                /* No unwind info; stop */
                break;
            }

            RtlVirtualUnwind(UNW_FLAG_NHANDLER,
                             ImageBase,
                             ControlPc,
                             FunctionEntry,
                             &Ctx,
                             &HandlerData,
                             &EstablisherFrame,
                             NULL);
            ControlPc = Ctx.Pc;
            frames++;
        }
    }

    /* Non-interactive shim: just continue letting KD/SEH handle it */
    return kdContinue;
}
