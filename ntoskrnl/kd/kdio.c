/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kd/kdio.c
 * PURPOSE:         NT Kernel Debugger Input/Output Functions
 *
 * PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/buildno.h>
#include "kd.h"
#include "kdterminal.h"
#ifdef KDBG
#include "../kdbg/kdb.h"
#endif
#include <cportlib/uartinfo.h>

#define NDEBUG
#include <debug.h>

#undef KdSendPacket
#undef KdReceivePacket

/* GLOBALS *******************************************************************/

#define KdpBufferSize  (1024 * 512)
static volatile BOOLEAN KdpLoggingEnabled = FALSE;
static CHAR KdpDebugBuffer[KdpBufferSize];
static volatile ULONG KdpCurrentPosition = 0;
static volatile ULONG KdpFreeBytes = 0;
static KSPIN_LOCK KdpDebugLogSpinLock;
static KEVENT KdpLoggerThreadEvent;
static KDPC KdpLoggerWakeDpc;
static HANDLE KdpLogFileHandle;
ANSI_STRING KdpLogFileName = RTL_CONSTANT_STRING("\\SystemRoot\\debug.log");

static KSPIN_LOCK KdpSerialSpinLock;
ULONG  SerialPortNumber = DEFAULT_DEBUG_PORT;
CPPORT SerialPortInfo   = {0, DEFAULT_DEBUG_BAUD_RATE, 0};

#define KdpScreenLineLengthDefault 80
static CHAR KdpScreenLineBuffer[KdpScreenLineLengthDefault + 1] = "";
static ULONG KdpScreenLineBufferPos = 0, KdpScreenLineLength = 0;
static BOOLEAN KdpScreenInitialized = FALSE;

KDP_DEBUG_MODE KdpDebugMode;
LIST_ENTRY KdProviders = {&KdProviders, &KdProviders};
KD_DISPATCH_TABLE DispatchTable[KdMax] = {0};

PKDP_INIT_ROUTINE InitRoutines[KdMax] =
{
    KdpScreenInit,
    KdpSerialInit,
    KdpDebugLogInit,
#ifdef KDBG // See kdb_cli.c
    KdpKdbgInit
#endif
};

/* LOCKING FUNCTIONS *********************************************************/

BOOLEAN
NTAPI
KdbpAcquireLock(
    _In_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    /* A frozen processor may hold the lock, so debugger and bugcheck paths
     * must not wait for a lock that can no longer be released. */
    if (KdEnteredDebugger || KeBugCheckActive)
    {
        KeRaiseIrql(HIGH_LEVEL, OldIrql);
        return KeTryToAcquireSpinLockAtDpcLevel(SpinLock);
    }

    /* Acquire the spinlock without waiting at raised IRQL */
    while (TRUE)
    {
        /* Loop until the spinlock becomes available */
        while (!KeTestSpinLock(SpinLock));

        /* Spinlock is free, raise IRQL to high level */
        KeRaiseIrql(HIGH_LEVEL, OldIrql);

        /* Try to get the spinlock */
        if (KeTryToAcquireSpinLockAtDpcLevel(SpinLock))
            break;

        /* Someone else got the spinlock, lower IRQL back */
        KeLowerIrql(*OldIrql);
    }

    return TRUE;
}

VOID
NTAPI
KdbpReleaseLock(
    _In_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql,
    _In_ BOOLEAN LockAcquired)
{
    /* Release the spinlock if it was acquired */
    if (LockAcquired)
    {
        KiReleaseSpinLock(SpinLock);
        // KeReleaseSpinLockFromDpcLevel(SpinLock);
    }

    /* Restore the old IRQL */
    KeLowerIrql(OldIrql);
}

/* FILE DEBUG LOG FUNCTIONS **************************************************/

static VOID
NTAPI
KdpLoggerWakeDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeSetEvent(&KdpLoggerThreadEvent, IO_NO_INCREMENT, FALSE);
}

static VOID
NTAPI
KdpLoggerThread(PVOID Context)
{
    ULONG beg, end, num;
    IO_STATUS_BLOCK Iosb;

    ASSERT(ExGetPreviousMode() == KernelMode);

    KdpLoggingEnabled = TRUE;

    while (TRUE)
    {
        KeWaitForSingleObject(&KdpLoggerThreadEvent, Executive, KernelMode, FALSE, NULL);

        /* Bug */
        /* Keep KdpCurrentPosition and KdpFreeBytes values in local
         * variables to avoid their possible change from Producer part,
         * KdpPrintToLogFile function
         */
        end = KdpCurrentPosition;
        num = KdpFreeBytes;

        /* Now securely calculate values, based on local variables */
        beg = (end + num) % KdpBufferSize;
        num = KdpBufferSize - num;

        /* Nothing to do? */
        if (num == 0)
            continue;

        if (end > beg)
        {
            NtWriteFile(KdpLogFileHandle, NULL, NULL, NULL, &Iosb,
                        KdpDebugBuffer + beg, num, NULL, NULL);
        }
        else
        {
            NtWriteFile(KdpLogFileHandle, NULL, NULL, NULL, &Iosb,
                        KdpDebugBuffer + beg, KdpBufferSize - beg, NULL, NULL);

            NtWriteFile(KdpLogFileHandle, NULL, NULL, NULL, &Iosb,
                        KdpDebugBuffer, end, NULL, NULL);
        }

        (VOID)InterlockedExchangeAddUL(&KdpFreeBytes, num);
    }
}

static VOID
NTAPI
KdpPrintToLogFile(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    BOOLEAN LockAcquired;
    KIRQL OldIrql;
    ULONG beg, end, num;

    /* Acquire the printing spinlock without waiting at raised IRQL */
    LockAcquired = KdbpAcquireLock(&KdpDebugLogSpinLock, &OldIrql);
    if (!LockAcquired)
    {
        KdbpReleaseLock(&KdpDebugLogSpinLock, OldIrql, LockAcquired);
        return;
    }

    beg = KdpCurrentPosition;
    num = min(Length, KdpFreeBytes);
    if (num != 0)
    {
        end = (beg + num) % KdpBufferSize;
        KdpCurrentPosition = end;
        KdpFreeBytes -= num;

        if (end > beg)
        {
            RtlCopyMemory(KdpDebugBuffer + beg, String, num);
        }
        else
        {
            RtlCopyMemory(KdpDebugBuffer + beg, String, KdpBufferSize - beg);
            RtlCopyMemory(KdpDebugBuffer, String + KdpBufferSize - beg, end);
        }
    }

    /* Release the spinlock */
    KdbpReleaseLock(&KdpDebugLogSpinLock, OldIrql, LockAcquired);

    /* Signal the logger thread, deferring the wake-up when KD runs at HIGH_LEVEL. */
    if (KdpLoggingEnabled)
    {
        if (OldIrql <= DISPATCH_LEVEL)
            KeSetEvent(&KdpLoggerThreadEvent, IO_NO_INCREMENT, FALSE);
        else
            KeInsertQueueDpc(&KdpLoggerWakeDpc, NULL, NULL);
    }
}

NTSTATUS
NTAPI
KdpDebugLogInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!KdpDebugMode.File)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        /* The kernel cannot allocate pool this early, but file logging must
         * retain the banner and hardware inventory printed before Phase 1. */
        KeInitializeSpinLock(&KdpDebugLogSpinLock);
        KdpFreeBytes = KdpBufferSize;

        /* Write out the functions that we support for now */
        DispatchTable->KdpPrintRoutine = KdpPrintToLogFile;

        /* Register for BootPhase 1 initialization and as a Provider */
        DispatchTable->KdpInitRoutine = KdpDebugLogInit;
        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }
    else if (BootPhase == 1)
    {
        /* Register for later BootPhase 2 reinitialization */
        DispatchTable->KdpInitRoutine = KdpDebugLogInit;

        /* Announce ourselves */
        HalDisplayString("   File log debugging enabled\r\n");
    }
    else if (BootPhase >= 2)
    {
        UNICODE_STRING FileName;
        OBJECT_ATTRIBUTES ObjectAttributes;
        IO_STATUS_BLOCK Iosb;
        HANDLE ThreadHandle;
        KPRIORITY Priority;

        /* If we have already successfully opened the log file, bail out */
        if (KdpLogFileHandle != NULL)
            return STATUS_SUCCESS;

        /* Setup the log name */
        Status = RtlAnsiStringToUnicodeString(&FileName, &KdpLogFileName, TRUE);
        if (!NT_SUCCESS(Status))
            goto Failure;

        InitializeObjectAttributes(&ObjectAttributes,
                                   &FileName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);

        /* Start a fresh log so this boot's phase-0 output begins at byte zero. */
        Status = ZwCreateFile(&KdpLogFileHandle,
                              FILE_WRITE_DATA | SYNCHRONIZE,
                              &ObjectAttributes,
                              &Iosb,
                              NULL,
                              FILE_ATTRIBUTE_NORMAL,
                              FILE_SHARE_READ,
                              FILE_OVERWRITE_IF,
                              FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                                FILE_SEQUENTIAL_ONLY | FILE_WRITE_THROUGH,
                              NULL,
                              0);

        RtlFreeUnicodeString(&FileName);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to open log file: 0x%08lx\n", Status);

            /* The final retry is issued after the I/O manager replaces the ARC-form \SystemRoot link. */
            if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
                Status == STATUS_OBJECT_PATH_NOT_FOUND)
            {
                if (BootPhase < KDP_BOOT_PHASE_SYSTEM_ROOT)
                {
                    DispatchTable->KdpInitRoutine = KdpDebugLogInit;
                    return Status;
                }
            }
            goto Failure;
        }

        KeInitializeEvent(&KdpLoggerThreadEvent, SynchronizationEvent, TRUE);
        KeInitializeDpc(&KdpLoggerWakeDpc, KdpLoggerWakeDpcRoutine, NULL);

        /* Create the logger thread */
        Status = PsCreateSystemThread(&ThreadHandle,
                                      THREAD_ALL_ACCESS,
                                      NULL,
                                      NULL,
                                      NULL,
                                      KdpLoggerThread,
                                      NULL);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to create log file thread: 0x%08lx\n", Status);
            ZwClose(KdpLogFileHandle);
            KdpLogFileHandle = NULL;
            goto Failure;
        }

        Priority = HIGH_PRIORITY;
        ZwSetInformationThread(ThreadHandle,
                               ThreadPriority,
                               &Priority,
                               sizeof(Priority));

        ZwClose(ThreadHandle);
        return Status;

Failure:
        KdpFreeBytes = 0;
        KdpDebugMode.File = FALSE;
        RemoveEntryList(&DispatchTable->KdProvidersList);
    }

    return Status;
}

/* SERIAL FUNCTIONS **********************************************************/

static VOID
NTAPI
KdpSerialPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    BOOLEAN LockAcquired;
    PCCH pch = String;
    PCCH End = String + Length;
    PCCH Run;
    KIRQL OldIrql;

    /* Acquire the printing spinlock without waiting at raised IRQL */
    LockAcquired = KdbpAcquireLock(&KdpSerialSpinLock, &OldIrql);

    /* Output unlocked rather than deadlock if the lock is unavailable.
     * Send the longest newline-free run as one buffer write, so transports
     * with per-transfer command cost pay it per run instead of per byte. */
    while (pch < End && *pch)
    {
        Run = pch;
        while (pch < End && *pch && *pch != '\n') ++pch;
        if (pch > Run)
        {
            KdPortPutBufferEx(&SerialPortInfo, Run, (ULONG)(pch - Run));
        }
        if (pch < End && *pch == '\n')
        {
            KdPortPutBufferEx(&SerialPortInfo, "\r\n", 2);
            ++pch;
        }
    }

    /* Release the spinlock */
    KdbpReleaseLock(&KdpSerialSpinLock, OldIrql, LockAcquired);
}

NTSTATUS
NTAPI
KdpSerialInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    if (!KdpDebugMode.Serial)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        /* Write out the functions that we support for now */
        DispatchTable->KdpPrintRoutine = KdpSerialPrint;

        /* Initialize the Port */
        if (!KdPortInitializeEx(&SerialPortInfo, SerialPortNumber))
        {
            KdpDebugMode.Serial = FALSE;
            return STATUS_DEVICE_DOES_NOT_EXIST;
        }
        KdComPortInUse = SerialPortInfo.Address;

        /* Initialize spinlock */
        KeInitializeSpinLock(&KdpSerialSpinLock);

        /* Register for BootPhase 1 initialization and as a Provider */
        DispatchTable->KdpInitRoutine = KdpSerialInit;
        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }
    else if (BootPhase == 1)
    {
        /* Announce ourselves */
        HalDisplayString("   Serial debugging enabled\r\n");
    }

    return STATUS_SUCCESS;
}

/* SCREEN FUNCTIONS **********************************************************/

BOOLEAN
KdpScreenAcquire(VOID)
{
    BOOLEAN DisplayAcquired = FALSE;
    BOOLEAN InitializeDisplay;

    if (InbvIsBootDriverInstalled())
    {
        DisplayAcquired = (InbvGetDisplayState() != INBV_DISPLAY_STATE_OWNED);
        InitializeDisplay = DisplayAcquired || !KdpScreenInitialized;

        /* Acquire ownership and initialize a new screen session */
        InbvAcquireDisplayOwnership();
        if (InitializeDisplay)
        {
            InbvResetDisplay();
            InbvSolidColorFill(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1, BV_COLOR_BLACK);
        }

        InbvSetTextColor(BV_COLOR_WHITE);
        InbvInstallDisplayStringFilter(NULL);
        InbvEnableDisplayString(TRUE);
        if (InitializeDisplay)
            InbvSetScrollRegion(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);

        KdpScreenInitialized = TRUE;
    }

    return DisplayAcquired;
}

// extern VOID NTAPI InbvSetDisplayOwnership(IN BOOLEAN DisplayOwned);

VOID
KdpScreenRelease(
    _In_ BOOLEAN DisplayAcquired)
{
    if (DisplayAcquired &&
        InbvIsBootDriverInstalled() &&
        InbvCheckDisplayOwnership())
    {
        /* Release the display */
        // InbvSetDisplayOwnership(FALSE);
        InbvNotifyDisplayOwnershipLost(NULL);
    }
}

static VOID
NTAPI
KdpScreenPrint(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    PCCH pch = String;

    while (pch < String + Length && *pch)
    {
        if (*pch == '\b')
        {
            /* HalDisplayString does not support '\b'. Workaround it and use '\r' */
            if (KdpScreenLineLength > 0)
            {
                /* Remove last character from buffer */
                KdpScreenLineBuffer[--KdpScreenLineLength] = '\0';
                KdpScreenLineBufferPos = KdpScreenLineLength;

                /* Clear row and print line again */
                HalDisplayString("\r");
                HalDisplayString(KdpScreenLineBuffer);
            }
        }
        else
        {
            KdpScreenLineBuffer[KdpScreenLineLength++] = *pch;
            KdpScreenLineBuffer[KdpScreenLineLength] = '\0';
        }

        if (*pch == '\n' || KdpScreenLineLength == KdpScreenLineLengthDefault)
        {
            /* Print buffered characters */
            if (KdpScreenLineBufferPos != KdpScreenLineLength)
                HalDisplayString(KdpScreenLineBuffer + KdpScreenLineBufferPos);

            /* Clear line buffer */
            KdpScreenLineBuffer[0] = '\0';
            KdpScreenLineLength = KdpScreenLineBufferPos = 0;
        }

        ++pch;
    }

    /* Print buffered characters */
    if (KdpScreenLineBufferPos != KdpScreenLineLength)
    {
        HalDisplayString(KdpScreenLineBuffer + KdpScreenLineBufferPos);
        KdpScreenLineBufferPos = KdpScreenLineLength;
    }
}

NTSTATUS
NTAPI
KdpScreenInit(
    _In_ PKD_DISPATCH_TABLE DispatchTable,
    _In_ ULONG BootPhase)
{
    if (!KdpDebugMode.Screen)
        return STATUS_PORT_DISCONNECTED;

    if (BootPhase == 0)
    {
        /* Write out the functions that we support for now */
        DispatchTable->KdpPrintRoutine = KdpScreenPrint;

        /* Register for BootPhase 1 initialization and as a Provider */
        DispatchTable->KdpInitRoutine = KdpScreenInit;
        InsertTailList(&KdProviders, &DispatchTable->KdProvidersList);
    }
    else if (BootPhase == 1)
    {
        /* Take control of the display */
        (VOID)KdpScreenAcquire();

        /* Announce ourselves */
        HalDisplayString("   Screen debugging enabled\r\n");
    }

    return STATUS_SUCCESS;
}


/* GENERAL FUNCTIONS *********************************************************/

static VOID
KdIoPrintString(
    _In_ PCCH String,
    _In_ ULONG Length)
{
    PLIST_ENTRY CurrentEntry;
    PKD_DISPATCH_TABLE CurrentTable;

    /* Call the registered providers */
    for (CurrentEntry = KdProviders.Flink;
         CurrentEntry != &KdProviders;
         CurrentEntry = CurrentEntry->Flink)
    {
        CurrentTable = CONTAINING_RECORD(CurrentEntry,
                                         KD_DISPATCH_TABLE,
                                         KdProvidersList);

        CurrentTable->KdpPrintRoutine(String, Length);
    }
}

VOID
KdIoPuts(
    _In_ PCSTR String)
{
    KdIoPrintString(String, (ULONG)strlen(String));
}

VOID
__cdecl
KdIoPrintf(
    _In_ PCSTR Format,
    ...)
{
    va_list ap;
    ULONG Length;
    CHAR Buffer[512];

    /* Format the string */
    va_start(ap, Format);
    Length = (ULONG)_vsnprintf(Buffer,
                               sizeof(Buffer),
                               Format,
                               ap);
    va_end(ap);

    /* Send it to the display providers */
    KdIoPrintString(Buffer, Length);
}

#ifdef KDBG
extern const CSTRING KdbPromptStr;
#endif

VOID
NTAPI
KdSendPacket(
    _In_ ULONG PacketType,
    _In_ PSTRING MessageHeader,
    _In_opt_ PSTRING MessageData,
    _Inout_ PKD_CONTEXT Context)
{
    PDBGKD_DEBUG_IO DebugIo;

    if (PacketType == PACKET_TYPE_KD_STATE_CHANGE32 ||
        PacketType == PACKET_TYPE_KD_STATE_CHANGE64)
    {
        PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange = (PDBGKD_ANY_WAIT_STATE_CHANGE)MessageHeader->Buffer;

        if (WaitStateChange->NewState == DbgKdLoadSymbolsStateChange)
            return; // Ignore: invoked anytime a new module is loaded.

        /* We should not get there, unless an exception has been raised */
        if (WaitStateChange->NewState == DbgKdExceptionStateChange)
        {
            PEXCEPTION_RECORD64 ExceptionRecord = &WaitStateChange->u.Exception.ExceptionRecord;

            /*
             * Claim the debugger to be present, so that KdpSendWaitContinue()
             * can call back KdReceivePacket(PACKET_TYPE_KD_STATE_MANIPULATE),
             * which, in turn, informs KD that the exception cannot be handled.
             */
            KD_DEBUGGER_NOT_PRESENT = FALSE;
            MmWriteableSharedUserData->KdDebuggerEnabled |= 0x00000002;

            KdIoPrintf("%s: Got exception 0x%08lx @ 0x%p, Flags 0x%08x, %s - Info[0]: 0x%p\n",
                       __FUNCTION__,
                       ExceptionRecord->ExceptionCode,
                       (PVOID)(ULONG_PTR)ExceptionRecord->ExceptionAddress,
                       ExceptionRecord->ExceptionFlags,
                       WaitStateChange->u.Exception.FirstChance ? "FirstChance" : "LastChance",
                       ExceptionRecord->ExceptionInformation[0]);
#if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM) || defined(_M_ARM64)
extern VOID NTAPI RtlpBreakWithStatusInstruction(VOID);
            if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
                ((PVOID)(ULONG_PTR)ExceptionRecord->ExceptionAddress == (PVOID)RtlpBreakWithStatusInstruction))
            {
                PCONTEXT ContextRecord = &KeGetCurrentPrcb()->ProcessorState.ContextFrame;
                ULONG Status =
#if defined(_M_IX86)
                    ContextRecord->Eax;
#elif defined(_M_AMD64)
                    (ULONG)ContextRecord->Rcx;
#elif defined(_M_ARM)
                    ContextRecord->R0;
#else // defined(_M_ARM64)
                    (ULONG)ContextRecord->X0;
#endif
                KdIoPrintf("STATUS_BREAKPOINT Status 0x%08lx\n", Status);
            }
// #else
// #error Unknown architecture
#endif
            return;
        }

        KdIoPrintf("%s: PACKET_TYPE_KD_STATE_CHANGE32/64 NewState %d is UNIMPLEMENTED\n",
                   __FUNCTION__, WaitStateChange->NewState);
        return;
    }
    else
    if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        KdIoPrintf("%s: PACKET_TYPE_KD_STATE_MANIPULATE for ApiNumber %lu\n",
                   __FUNCTION__, ManipulateState->ApiNumber);
        return;
    }

    if (PacketType != PACKET_TYPE_KD_DEBUG_IO)
    {
        KdIoPrintf("%s: PacketType %d is UNIMPLEMENTED\n", __FUNCTION__, PacketType);
        return;
    }

    DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;

    /* Validate API call */
    if (MessageHeader->Length != sizeof(DBGKD_DEBUG_IO))
        return;
    if ((DebugIo->ApiNumber != DbgKdPrintStringApi) &&
        (DebugIo->ApiNumber != DbgKdGetStringApi))
    {
        return;
    }
    if (!MessageData)
        return;

    /* NOTE: MessageData->Length should be equal to
     * DebugIo.u.PrintString.LengthOfString, or to
     * DebugIo.u.GetString.LengthOfPromptString */

    if (!KdpDebugMode.Value)
        return;

    /* Print the string proper */
    KdIoPrintString(MessageData->Buffer, MessageData->Length);
}

KDSTATUS
NTAPI
KdReceivePacket(
    _In_ ULONG PacketType,
    _Out_ PSTRING MessageHeader,
    _Out_ PSTRING MessageData,
    _Out_ PULONG DataLength,
    _Inout_ PKD_CONTEXT Context)
{
#ifdef KDBG
    PDBGKD_DEBUG_IO DebugIo;
    STRING ResponseString;
    CHAR MessageBuffer[512];
#endif

    if (PacketType == PACKET_TYPE_KD_POLL_BREAKIN)
    {
        UCHAR Byte;

        /*
         * KDBG uses the native KD terminal multiplexer rather than kdcom.dll,
         * so it must poll the configured serial provider itself.  Do not take
         * KdpSerialSpinLock here: this runs from the clock interrupt and may
         * have interrupted a debug-print operation that owns that lock.
         */
        if (KdpDebugMode.Serial &&
            (SerialPortInfo.Address != NULL) &&
            KdPortGetByteEx(&SerialPortInfo, &Byte) &&
            (Byte == BREAKIN_PACKET_BYTE))
        {
            return KdPacketReceived;
        }

        return KdPacketTimedOut;
    }

    if (PacketType == PACKET_TYPE_KD_STATE_MANIPULATE)
    {
        PDBGKD_MANIPULATE_STATE64 ManipulateState = (PDBGKD_MANIPULATE_STATE64)MessageHeader->Buffer;
        RtlZeroMemory(MessageHeader->Buffer, MessageHeader->MaximumLength);

        /* The exception (notified via DbgKdExceptionStateChange in
         * KdSendPacket()) cannot be handled: return a failure code */
        ManipulateState->ApiNumber = DbgKdContinueApi;
        ManipulateState->u.Continue.ContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
        return KdPacketReceived;
    }

    if (PacketType != PACKET_TYPE_KD_DEBUG_IO)
    {
        KdIoPrintf("%s: PacketType %d is UNIMPLEMENTED\n", __FUNCTION__, PacketType);
        return KdPacketTimedOut;
    }

#ifdef KDBG
    DebugIo = (PDBGKD_DEBUG_IO)MessageHeader->Buffer;

    /* Validate API call */
    if (MessageHeader->MaximumLength != sizeof(DBGKD_DEBUG_IO))
        return KdPacketNeedsResend;
    if (DebugIo->ApiNumber != DbgKdGetStringApi)
        return KdPacketNeedsResend;

    /* NOTE: We cannot use directly MessageData->Buffer here as it points
     * to the temporary KdpMessageBuffer scratch buffer that is being
     * shared with all the possible I/O KD operations that may happen. */
    ResponseString.Buffer = MessageBuffer;
    ResponseString.Length = 0;
    ResponseString.MaximumLength = min(sizeof(MessageBuffer),
                                       MessageData->MaximumLength);
    ResponseString.MaximumLength = min(ResponseString.MaximumLength,
                                       DebugIo->u.GetString.LengthOfStringRead);

    /* The prompt string has been printed by KdSendPacket; go to
     * new line and print the kdb prompt -- for SYSREG2 support. */
    KdIoPrintString("\n", 1);
    KdIoPuts(KdbPromptStr.Buffer); // Alternatively, use "Input> "

    if (!KdTermSerial)
        KbdDisableMouse();

    /*
     * Read a NULL-terminated line of user input and retrieve its length.
     * Official documentation states that DbgPrompt() includes a terminating
     * newline character but does not NULL-terminate. However, experiments
     * show that this behaviour is left at the discretion of WinDbg itself.
     * WinDbg NULL-terminates the string unless its buffer is too short,
     * in which case the string is simply truncated without NULL-termination.
     */
    ResponseString.Length =
        (USHORT)KdIoReadLine(ResponseString.Buffer,
                             ResponseString.MaximumLength);

    if (!KdTermSerial)
        KbdEnableMouse();

    /* Adjust and return the string length */
    *DataLength = min(ResponseString.Length + sizeof(ANSI_NULL),
                      DebugIo->u.GetString.LengthOfStringRead);
    MessageData->Length = DebugIo->u.GetString.LengthOfStringRead = *DataLength;

    /* Only now we can copy back the data into MessageData->Buffer */
    RtlCopyMemory(MessageData->Buffer, ResponseString.Buffer, *DataLength);
#endif

    return KdPacketReceived;
}

/* EOF */
