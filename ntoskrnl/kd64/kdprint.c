/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/kd64/kdprint.c
 * PURPOSE:         KD64 Trap Handler Routines
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Stefan Ginsberg (stefan.ginsberg@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KD_PRINT_MAX_BYTES 512

/*
 * ARM64 Monotonic Timestamp Support
 *
 * On ARM64, the following issues can cause non-monotonic timestamps:
 * 1. Multiple CPUs reading cntpct_el0 concurrently without synchronization
 * 2. ARM64's relaxed memory model allowing out-of-order execution
 * 3. Different code paths (KdpDprintf vs KdpPrint) using different time sources
 *
 * This implementation provides:
 * - A spinlock-protected timestamp acquisition function
 * - A global "last timestamp" to ensure strict monotonicity
 * - Proper memory barriers for ARM64
 * - Unified timestamp source (performance counter) for all debug output
 */

/* Global state for monotonic timestamp generation */
static KSPIN_LOCK KdpTimestampSpinLock;
static volatile ULONGLONG KdpLastTimestampMicroseconds = 0;
static BOOLEAN KdpTimestampSpinLockInitialized = FALSE;

/* Forward declarations */
KIRQL NTAPI KdpAcquireLock(_In_ PKSPIN_LOCK SpinLock);
VOID NTAPI KdpReleaseLock(_In_ PKSPIN_LOCK SpinLock, _In_ KIRQL OldIrql);

/**
 * @brief Acquires a monotonically increasing timestamp in microseconds.
 *
 * This function ensures that timestamps are always monotonically increasing,
 * even when called concurrently from multiple CPUs. On ARM64, this is critical
 * because:
 * - The performance counter can be read out of order due to speculative execution
 * - Multiple CPUs may read similar values that get printed in non-chronological order
 * - The relaxed memory model requires explicit synchronization
 *
 * @return Monotonically increasing timestamp in microseconds since boot.
 */
/**
 * @brief Read the raw counter value directly (IRQL-safe).
 *
 * On x86/x64: Reads TSC via RDTSC instruction.
 * On ARM64: Reads cntpct_el0 (physical counter).
 *
 * This is safe at any IRQL and does not call into HAL.
 */
static ULONGLONG
KdpReadRawCounter(VOID)
{
#if defined(_M_AMD64) || defined(_M_IX86)
    return __rdtsc();
#elif defined(_M_ARM64)
    ULONGLONG Counter;
    __asm__ __volatile__("isb" ::: "memory");
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (Counter));
    return Counter;
#else
    /* Fallback: use HAL performance counter if available */
    LARGE_INTEGER Counter = KeQueryPerformanceCounter(NULL);
    return (ULONGLONG)Counter.QuadPart;
#endif
}

/**
 * @brief Get counter frequency for the current architecture.
 *
 * Prefers the bootloader-provided frequency for timestamp continuity.
 * Falls back to HAL or architecture-specific methods if not available.
 */
static ULONGLONG
KdpGetCounterFrequency(VOID)
{
    /* Prefer bootloader-provided frequency for continuity */
    if (KdpBootloaderCounterFrequency != 0)
    {
        return KdpBootloaderCounterFrequency;
    }

#if defined(_M_ARM64)
    /* ARM64: Read cntfrq_el0 directly */
    {
        ULONGLONG Freq;
        __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r" (Freq));
        if (Freq != 0)
            return Freq;
        /* Fallback to common ARM64 frequency */
        return 24000000ULL;
    }
#else
    /* x86/x64: Use HAL frequency as fallback */
    if (KdPerformanceCounterRate.QuadPart != 0)
    {
        return (ULONGLONG)KdPerformanceCounterRate.QuadPart;
    }
    /* Very early fallback: assume ~2GHz */
    return 2000000000ULL;
#endif
}

ULONGLONG
NTAPI
KdpAcquireMonotonicTimestamp(VOID)
{
    ULONGLONG Microseconds = 0;
    ULONGLONG LastTimestamp;
    ULONGLONG CurrentCounter;
    ULONGLONG Frequency;
    ULONGLONG Delta;
    KIRQL OldIrql;
    BOOLEAN UseLock;

    /*
     * Check if spinlock is initialized. During very early boot, the spinlock
     * may not be initialized yet. In that case, we fall back to unsynchronized
     * access which is acceptable since early boot is typically single-threaded.
     */
    UseLock = KdpTimestampSpinLockInitialized;

    /*
     * ARM64-specific: Issue an ISB (Instruction Synchronization Barrier) before
     * reading the performance counter to ensure all prior instructions have
     * completed. This prevents speculative reads of the counter from returning
     * stale or out-of-order values.
     */
#if defined(_M_ARM64)
    __asm__ __volatile__("isb" ::: "memory");
#endif

    /*
     * Use direct counter reads with bootloader-calibrated frequency.
     * This ensures timestamp continuity between FreeLDR and the kernel
     * by using the same time base (TSC on x86/x64, cntpct on ARM64).
     *
     * The calculation is:
     *   elapsed_us = ((current_counter - base_counter) * 1000000) / frequency
     *   total_us = bootloader_end_time + elapsed_us
     */
    Frequency = KdpGetCounterFrequency();
    CurrentCounter = KdpReadRawCounter();

    if (Frequency != 0 && KdpBootloaderEndTimeCounter != 0)
    {
        /* Calculate time elapsed since bootloader handed off to kernel */
        if (CurrentCounter >= KdpBootloaderEndTimeCounter)
            Delta = CurrentCounter - KdpBootloaderEndTimeCounter;
        else
            Delta = 0; /* Counter wrapped or invalid state */

        /* Convert to microseconds: (delta * 1000000) / frequency */
        Microseconds = (Delta * 1000000ULL) / Frequency;

        /* Add bootloader's EndTime to get continuous timestamp since boot */
        Microseconds += KdpTimeStampOffsetMicroseconds;
    }
    else if (Frequency != 0)
    {
        /*
         * Bootloader data not available (KdpBootloaderEndTimeCounter == 0).
         * Fall back to old behavior using HAL performance counter baseline.
         */
        if (KdpInitialPerformanceCounter.QuadPart != 0)
        {
            Delta = CurrentCounter - (ULONGLONG)KdpInitialPerformanceCounter.QuadPart;
            Microseconds = (Delta * 1000000ULL) / Frequency;
            Microseconds += KdpTimeStampOffsetMicroseconds;
        }
        else
        {
            /* Very early boot - just use raw counter with assumed frequency */
            Microseconds = (CurrentCounter * 1000000ULL) / Frequency;
        }
    }
    else
    {
        /*
         * Fallback when no frequency is available.
         * Use KeQueryInterruptTime if available.
         */
        ULONGLONG InterruptTime = KeQueryInterruptTime();
        if (InterruptTime != 0)
        {
            /* Convert from 100ns units to microseconds */
            Microseconds = InterruptTime / 10ULL;
            Microseconds += KdpTimeStampOffsetMicroseconds;
        }
        else
        {
            /* Absolute fallback: return last + 1 to maintain monotonicity */
            Microseconds = KdpLastTimestampMicroseconds + 1;
        }
    }

    /*
     * Ensure monotonicity: the timestamp must be strictly greater than the
     * last recorded timestamp. This handles cases where:
     * - Two CPUs read the counter at nearly the same time
     * - Counter wraps around (extremely unlikely but possible)
     * - Clock adjustments or drift between reads
     */
    if (UseLock)
    {
        /* Acquire lock at HIGH_LEVEL to prevent preemption */
        OldIrql = KdpAcquireLock(&KdpTimestampSpinLock);

        /*
         * ARM64-specific: Memory barrier to ensure we see the latest value
         * of KdpLastTimestampMicroseconds after acquiring the lock.
         */
#if defined(_M_ARM64)
        __asm__ __volatile__("dmb ish" ::: "memory");
#endif

        LastTimestamp = KdpLastTimestampMicroseconds;

        /* Ensure strict monotonicity */
        if (Microseconds <= LastTimestamp)
        {
            Microseconds = LastTimestamp + 1;
        }

        /* Update the global last timestamp */
        KdpLastTimestampMicroseconds = Microseconds;

        /*
         * ARM64-specific: Memory barrier to ensure the updated timestamp
         * is visible to other CPUs before we release the lock.
         */
#if defined(_M_ARM64)
        __asm__ __volatile__("dmb ish" ::: "memory");
#endif

        KdpReleaseLock(&KdpTimestampSpinLock, OldIrql);
    }
    else
    {
        /*
         * No lock available (early boot). Use atomic compare-exchange to
         * try to maintain monotonicity without full synchronization.
         */
        do
        {
            LastTimestamp = KdpLastTimestampMicroseconds;
            if (Microseconds <= LastTimestamp)
            {
                Microseconds = LastTimestamp + 1;
            }
        } while (InterlockedCompareExchange64(
                    (volatile LONGLONG*)&KdpLastTimestampMicroseconds,
                    (LONGLONG)Microseconds,
                    (LONGLONG)LastTimestamp) != (LONGLONG)LastTimestamp);
    }

    return Microseconds;
}

/**
 * @brief Initializes the timestamp spinlock.
 *
 * This must be called during kernel initialization before multi-threaded
 * debug output begins. It is safe to call multiple times.
 */
VOID
NTAPI
KdpInitializeTimestampLock(VOID)
{
    if (!KdpTimestampSpinLockInitialized)
    {
        KeInitializeSpinLock(&KdpTimestampSpinLock);
        KdpTimestampSpinLockInitialized = TRUE;
    }
}

/* FUNCTIONS *****************************************************************/

KIRQL
NTAPI
KdpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql;

    /* Acquire the spinlock without waiting at raised IRQL */
    while (TRUE)
    {
        /* Loop until the spinlock becomes available */
        while (!KeTestSpinLock(SpinLock));

        /* Spinlock is free, raise IRQL to high level */
        KeRaiseIrql(HIGH_LEVEL, &OldIrql);

        /* Try to get the spinlock */
        if (KeTryToAcquireSpinLockAtDpcLevel(SpinLock))
            break;

        /* Someone else got the spinlock, lower IRQL back */
        KeLowerIrql(OldIrql);
    }

    return OldIrql;
}

VOID
NTAPI
KdpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    /* Release the spinlock */
    KiReleaseSpinLock(SpinLock);
    // KeReleaseSpinLockFromDpcLevel(SpinLock);

    /* Restore the old IRQL */
    KeLowerIrql(OldIrql);
}

VOID
NTAPI
KdLogDbgPrint(
    _In_ PSTRING String)
{
    SIZE_T Length, Remaining;
    KIRQL OldIrql;

    /* If the string is empty, bail out */
    if (!String->Buffer || (String->Length == 0))
        return;

    /* If no log buffer available, bail out */
    if (!KdPrintCircularBuffer /*|| (KdPrintBufferSize == 0)*/)
        return;

    /* Acquire the log spinlock without waiting at raised IRQL */
    OldIrql = KdpAcquireLock(&KdpPrintSpinLock);

    Length = min(String->Length, KdPrintBufferSize);
    Remaining = KdPrintCircularBuffer + KdPrintBufferSize - KdPrintWritePointer;

    if (Length < Remaining)
    {
        KdpMoveMemory(KdPrintWritePointer, String->Buffer, Length);
        KdPrintWritePointer += Length;
    }
    else
    {
        KdpMoveMemory(KdPrintWritePointer, String->Buffer, Remaining);
        Length -= Remaining;
        if (Length > 0)
            KdpMoveMemory(KdPrintCircularBuffer, String->Buffer + Remaining, Length);

        KdPrintWritePointer = KdPrintCircularBuffer + Length;

        /* Got a rollover, update count (handle wrapping, must always be >= 1) */
        ++KdPrintRolloverCount;
        if (KdPrintRolloverCount == 0)
            ++KdPrintRolloverCount;
    }

    /* Release the spinlock */
    KdpReleaseLock(&KdpPrintSpinLock, OldIrql);
}

BOOLEAN
NTAPI
KdpPrintString(
    _In_ PSTRING Output)
{
    STRING Data, Header;
    DBGKD_DEBUG_IO DebugIo;
    USHORT Length;

    /* Copy the string */
    KdpMoveMemory(KdpMessageBuffer,
                  Output->Buffer,
                  Output->Length);

    /* Make sure we don't exceed the KD Packet size */
    Length = Output->Length;
    if ((sizeof(DBGKD_DEBUG_IO) + Length) > PACKET_MAX_SIZE)
    {
        /* Normalize length */
        Length = PACKET_MAX_SIZE - sizeof(DBGKD_DEBUG_IO);
    }

    /* Build the packet header */
    DebugIo.ApiNumber = DbgKdPrintStringApi;
    DebugIo.ProcessorLevel = (USHORT)KeProcessorLevel;
    DebugIo.Processor = KeGetCurrentPrcb()->Number;
    DebugIo.u.PrintString.LengthOfString = Length;
    Header.Length = sizeof(DBGKD_DEBUG_IO);
    Header.Buffer = (PCHAR)&DebugIo;

    /* Build the data */
    Data.Length = Length;
    Data.Buffer = KdpMessageBuffer;

    /* Send the packet */
    KdSendPacket(PACKET_TYPE_KD_DEBUG_IO, &Header, &Data, &KdpContext);

    /* Check if the user pressed CTRL+C */
    return KdpPollBreakInWithPortLock();
}

BOOLEAN
NTAPI
KdpPromptString(
    _In_ PSTRING PromptString,
    _In_ PSTRING ResponseString)
{
    STRING Data, Header;
    DBGKD_DEBUG_IO DebugIo;
    ULONG Length;
    KDSTATUS Status;

    /* Copy the string to the message buffer */
    KdpMoveMemory(KdpMessageBuffer,
                  PromptString->Buffer,
                  PromptString->Length);

    /* Make sure we don't exceed the KD Packet size */
    Length = PromptString->Length;
    if ((sizeof(DBGKD_DEBUG_IO) + Length) > PACKET_MAX_SIZE)
    {
        /* Normalize length */
        Length = PACKET_MAX_SIZE - sizeof(DBGKD_DEBUG_IO);
    }

    /* Build the packet header */
    DebugIo.ApiNumber = DbgKdGetStringApi;
    DebugIo.ProcessorLevel = (USHORT)KeProcessorLevel;
    DebugIo.Processor = KeGetCurrentPrcb()->Number;
    DebugIo.u.GetString.LengthOfPromptString = Length;
    DebugIo.u.GetString.LengthOfStringRead = ResponseString->MaximumLength;
    Header.Length = sizeof(DBGKD_DEBUG_IO);
    Header.Buffer = (PCHAR)&DebugIo;

    /* Build the data */
    Data.Length = Length;
    Data.Buffer = KdpMessageBuffer;

    /* Send the packet */
    KdSendPacket(PACKET_TYPE_KD_DEBUG_IO, &Header, &Data, &KdpContext);

    /* Set the maximum lengths for the receive */
    Header.MaximumLength = sizeof(DBGKD_DEBUG_IO);
    Data.MaximumLength = sizeof(KdpMessageBuffer);

    /* Enter receive loop */
    do
    {
        /* Get our reply */
        Status = KdReceivePacket(PACKET_TYPE_KD_DEBUG_IO,
                                 &Header,
                                 &Data,
                                 &Length,
                                 &KdpContext);

        /* Return TRUE if we need to resend */
        if (Status == KdPacketNeedsResend) return TRUE;

    /* Loop until we succeed */
    } while (Status != KdPacketReceived);

    /* Don't copy back a larger response than there is room for */
    Length = min(Length,
                 ResponseString->MaximumLength);

    /* Copy back the string and return the length */
    KdpMoveMemory(ResponseString->Buffer,
                  KdpMessageBuffer,
                  Length);
    ResponseString->Length = (USHORT)Length;

    /* Success; we don't need to resend */
    return FALSE;
}

VOID
NTAPI
KdpCommandString(IN PSTRING NameString,
                 IN PSTRING CommandString,
                 IN KPROCESSOR_MODE PreviousMode,
                 IN PCONTEXT ContextRecord,
                 IN PKTRAP_FRAME TrapFrame,
                 IN PKEXCEPTION_FRAME ExceptionFrame)
{
    BOOLEAN Enable;
    PKPRCB Prcb = KeGetCurrentPrcb();

    /* Check if we need to do anything */
    if ((PreviousMode != KernelMode) || (KdDebuggerNotPresent)) return;

    /* Enter the debugger */
    Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);

    /* Save the CPU Control State and save the context */
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    KdpMoveMemory(&Prcb->ProcessorState.ContextFrame,
                  ContextRecord,
                  sizeof(CONTEXT));

    /* Send the command string to the debugger */
    KdpReportCommandStringStateChange(NameString,
                                      CommandString,
                                      &Prcb->ProcessorState.ContextFrame);

    /* Restore the processor state */
    KdpMoveMemory(ContextRecord,
                  &Prcb->ProcessorState.ContextFrame,
                  sizeof(CONTEXT));
    KiRestoreProcessorControlState(&Prcb->ProcessorState);

    /* Exit the debugger and return */
    KdExitDebugger(Enable);
}

VOID
NTAPI
KdpSymbol(IN PSTRING DllPath,
          IN PKD_SYMBOLS_INFO SymbolInfo,
          IN BOOLEAN Unload,
          IN KPROCESSOR_MODE PreviousMode,
          IN PCONTEXT ContextRecord,
          IN PKTRAP_FRAME TrapFrame,
          IN PKEXCEPTION_FRAME ExceptionFrame)
{
    BOOLEAN Enable;
    PKPRCB Prcb = KeGetCurrentPrcb();

    /* Check if we need to do anything */
    if ((PreviousMode != KernelMode) || (KdDebuggerNotPresent)) return;

    /* Enter the debugger */
    Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);

    /* Save the CPU Control State and save the context */
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    KdpMoveMemory(&Prcb->ProcessorState.ContextFrame,
                  ContextRecord,
                  sizeof(CONTEXT));

    /* Report the new state */
    KdpReportLoadSymbolsStateChange(DllPath,
                                    SymbolInfo,
                                    Unload,
                                    &Prcb->ProcessorState.ContextFrame);

    /* Restore the processor state */
    KdpMoveMemory(ContextRecord,
                  &Prcb->ProcessorState.ContextFrame,
                  sizeof(CONTEXT));
    KiRestoreProcessorControlState(&Prcb->ProcessorState);

    /* Exit the debugger and return */
    KdExitDebugger(Enable);
}

USHORT
NTAPI
KdpPrompt(
    _In_reads_bytes_(PromptLength) PCHAR PromptString,
    _In_ USHORT PromptLength,
    _Out_writes_bytes_(MaximumResponseLength) PCHAR ResponseString,
    _In_ USHORT MaximumResponseLength,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    STRING PromptBuffer, ResponseBuffer;
    BOOLEAN Enable, Resend;
    PCHAR SafeResponseString;
    CHAR CapturedPrompt[KD_PRINT_MAX_BYTES];
    CHAR SafeResponseBuffer[KD_PRINT_MAX_BYTES];

    /* Normalize the lengths */
    PromptLength = min(PromptLength,
                       sizeof(CapturedPrompt));
    MaximumResponseLength = min(MaximumResponseLength,
                                sizeof(SafeResponseBuffer));

    /* Check if we need to verify the string */
    if (PreviousMode != KernelMode)
    {
        /* Handle user-mode buffers safely */
        _SEH2_TRY
        {
            /* Probe and capture the prompt */
            ProbeForRead(PromptString, PromptLength, 1);
            KdpMoveMemory(CapturedPrompt, PromptString, PromptLength);
            PromptString = CapturedPrompt;

            /* Probe and make room for the response */
            ProbeForWrite(ResponseString, MaximumResponseLength, 1);
            SafeResponseString = SafeResponseBuffer;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Bad string pointer, bail out */
            _SEH2_YIELD(return 0);
        }
        _SEH2_END;
    }
    else
    {
        SafeResponseString = ResponseString;
    }

    /* Setup the prompt and response buffers */
    PromptBuffer.Buffer = PromptString;
    PromptBuffer.Length = PromptBuffer.MaximumLength = PromptLength;
    ResponseBuffer.Buffer = SafeResponseString;
    ResponseBuffer.Length = 0;
    ResponseBuffer.MaximumLength = MaximumResponseLength;

    /* Log the print */
    KdLogDbgPrint(&PromptBuffer);

    /* Enter the debugger */
    Enable = KdEnterDebugger(TrapFrame, ExceptionFrame);

    /* Enter prompt loop */
    do
    {
        /* Send the prompt and receive the response */
        Resend = KdpPromptString(&PromptBuffer, &ResponseBuffer);

    /* Loop while we need to resend */
    } while (Resend);

    /* Exit the debugger */
    KdExitDebugger(Enable);

    /* Copy back the response if required */
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            /* Safely copy back the response to user mode */
            KdpMoveMemory(ResponseString,
                          ResponseBuffer.Buffer,
                          ResponseBuffer.Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* String became invalid after we exited, fail */
            _SEH2_YIELD(return 0);
        }
        _SEH2_END;
    }

    /* Return the number of characters received */
    return ResponseBuffer.Length;
}

static
NTSTATUS
NTAPI
KdpPrintFromUser(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_reads_bytes_(Length) PCHAR String,
    _In_ USHORT Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PBOOLEAN Handled)
{
    CHAR CapturedString[KD_PRINT_MAX_BYTES];

    ASSERT(PreviousMode == UserMode);
    ASSERT(Length <= sizeof(CapturedString));

    /* Capture user-mode buffers */
    _SEH2_TRY
    {
        /* Probe and capture the string */
        ProbeForRead(String, Length, 1);
        KdpMoveMemory(CapturedString, String, Length);
        String = CapturedString;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Bad string pointer, bail out */
        _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
    }
    _SEH2_END;

    /* Now go through the kernel-mode code path */
    return KdpPrint(ComponentId,
                    Level,
                    String,
                    Length,
                    KernelMode,
                    TrapFrame,
                    ExceptionFrame,
                    Handled);
}

NTSTATUS
NTAPI
KdpPrint(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_reads_bytes_(Length) PCHAR String,
    _In_ USHORT Length,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PBOOLEAN Handled)
{
    NTSTATUS Status;
    STRING OutputString;
    CHAR LocalBuffer[KD_PRINT_MAX_BYTES];
    USHORT PrefixLength = 0;
    ULONGLONG Microseconds = 0, Seconds = 0, Fractional = 0;

    if (NtQueryDebugFilterState(ComponentId, Level) == (NTSTATUS)FALSE)
    {
        /* Mask validation failed */
        *Handled = TRUE;
        return STATUS_SUCCESS;
    }

    /* Assume failure */
    *Handled = FALSE;

    /* Normalize the length */
    Length = min(Length, KD_PRINT_MAX_BYTES);

    /* Check if we need to verify the string */
    if (PreviousMode != KernelMode)
    {
        /* This case requires a 512 byte stack buffer.
         * We don't want to use that much stack in the kernel case, but we
         * can't use _alloca due to PSEH. So the buffer exists in this
         * helper function instead.
         */
        return KdpPrintFromUser(ComponentId,
                                Level,
                                String,
                                Length,
                                PreviousMode,
                                TrapFrame,
                                ExceptionFrame,
                                Handled);
    }

    /*
     * Acquire a monotonically increasing timestamp.
     * This uses the centralized timestamp function which ensures:
     * - Proper synchronization across multiple CPUs
     * - ARM64-specific memory barriers
     * - Strict monotonicity even under concurrent access
     */
    Microseconds = KdpAcquireMonotonicTimestamp();

    Seconds = Microseconds / 1000000ULL;
    Fractional = Microseconds % 1000000ULL;
    PrefixLength = (USHORT)_snprintf(LocalBuffer, sizeof(LocalBuffer),
                                     "[%8llu.%06llu] ",
                                     Seconds, Fractional);
    if (PrefixLength >= sizeof(LocalBuffer)) PrefixLength = 0;

    /* Append the original message after the prefix */
    Length = min((USHORT)(sizeof(LocalBuffer) - PrefixLength - 1), Length);
    KdpMoveMemory(LocalBuffer + PrefixLength, String, Length);
    OutputString.Buffer = LocalBuffer;
    OutputString.Length = OutputString.MaximumLength = PrefixLength + Length;

    /* Log the print (prefixed) */
    KdLogDbgPrint(&OutputString);

    /* Check for a debugger */
    if (KdDebuggerNotPresent)
    {
        /* Fail */
        *Handled = TRUE;
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    /*
     * Send the string directly without KdEnterDebugger/KdExitDebugger.
     * The freeze-all-CPUs mechanism is for interactive debugger operations
     * (breakpoints, exceptions, prompts) where system state must be consistent.
     * DbgPrint is fire-and-forget diagnostic output — freezing all CPUs for
     * every print line causes livelock under SMP with many processors.
     * KdpTrap already routes BREAKPOINT_PRINT here without freezing;
     * actual debugger entries (KdpReport, KdpSymbol, KdpPrompt) have their
     * own KdEnterDebugger calls.
     */
    if (KdpPrintString(&OutputString))
    {
        /* User pressed CTRL-C, breakpoint on return */
        Status = STATUS_BREAKPOINT;
    }
    else
    {
        /* String was printed */
        Status = STATUS_SUCCESS;
    }

    *Handled = TRUE;
    return Status;
}

VOID
__cdecl
KdpDprintf(
    _In_ PCSTR Format,
    ...)
{
    STRING String;
    USHORT Length;
    va_list ap;
    CHAR Buffer[512];
    ULONGLONG Microseconds;
    ULONGLONG Seconds;
    ULONGLONG Fractional;
    USHORT TimestampLength = 0;

    /*
     * Acquire a monotonically increasing timestamp.
     * This uses the centralized timestamp function which ensures:
     * - Proper synchronization across multiple CPUs
     * - ARM64-specific memory barriers (ISB before counter read, DMB for ordering)
     * - Strict monotonicity even under concurrent access from multiple CPUs
     *
     * This is critical on ARM64 where:
     * - The cntpct_el0 register can be speculatively read out of order
     * - Multiple CPUs can interleave reads and prints
     * - The relaxed memory model requires explicit barriers
     */
    Microseconds = KdpAcquireMonotonicTimestamp();

    /* Convert to seconds and fractional part */
    Seconds = Microseconds / 1000000ULL;
    Fractional = Microseconds % 1000000ULL;

    /* Format timestamp directly into Buffer */
    TimestampLength = (USHORT)_snprintf(Buffer, sizeof(Buffer),
                                        "[%8llu.%06llu] ",
                                        Seconds, Fractional);

    if (TimestampLength >= sizeof(Buffer))
        TimestampLength = 0;

    /* Format the actual message after the timestamp */
    va_start(ap, Format);
    Length = (USHORT)_vsnprintf(Buffer + TimestampLength,
                                sizeof(Buffer) - TimestampLength,
                                Format,
                                ap);
    va_end(ap);

    /* Calculate total length */
    Length = min(TimestampLength + Length, sizeof(Buffer) - 1);

    /* Set it up */
    String.Buffer = Buffer;
    String.Length = String.MaximumLength = Length;

    /* Send it to the debugger directly */
    KdpPrintString(&String);
}
