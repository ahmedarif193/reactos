/*
 * PROJECT:         ReactOS Win32 Base API
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            dll/win32/kernel32/client/synch.c
 * PURPOSE:         Wrappers for the NT Wait Implementation
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/
#include <k32.h>

#define NDEBUG
#include <debug.h>

#undef InterlockedIncrement
#undef InterlockedDecrement
#undef InterlockedExchange
#undef InterlockedExchangeAdd
#undef InterlockedCompareExchange

/* Forward declaration to avoid prototype gating; implemented in kernel32 */
#ifndef _REACTOS_K32_GETTICKCOUNT64_DECL
#define _REACTOS_K32_GETTICKCOUNT64_DECL
ULONGLONG WINAPI GetTickCount64(VOID);
#endif

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
LONG
WINAPI
InterlockedIncrement(IN OUT LONG volatile *lpAddend)
{
    return _InterlockedIncrement(lpAddend);
}

/*
 * @implemented
 */
LONG
WINAPI
InterlockedDecrement(IN OUT LONG volatile *lpAddend)
{
    return _InterlockedDecrement(lpAddend);
}

/*
 * @implemented
 */
LONG
WINAPI
InterlockedExchange(IN OUT LONG volatile *Target,
                    IN LONG Value)
{
    return _InterlockedExchange(Target, Value);
}

/*
 * @implemented
 */
LONG
WINAPI
InterlockedExchangeAdd(IN OUT LONG volatile *Addend,
                       IN LONG Value)
{
    return _InterlockedExchangeAdd(Addend, Value);
}

/*
 * @implemented
 */
LONG
WINAPI
InterlockedCompareExchange(IN OUT LONG volatile *Destination,
                           IN LONG Exchange,
                           IN LONG Comperand)
{
    return _InterlockedCompareExchange(Destination, Exchange, Comperand);
}

/*
 * @implemented
 */
DWORD
WINAPI
WaitForSingleObject(IN HANDLE hHandle,
                    IN DWORD dwMilliseconds)
{
    /* Call the extended API */
    return WaitForSingleObjectEx(hHandle, dwMilliseconds, FALSE);
}

/*
 * @implemented
 */
DWORD
WINAPI
WaitForSingleObjectEx(IN HANDLE hHandle,
                      IN DWORD dwMilliseconds,
                      IN BOOL bAlertable)
{
    PLARGE_INTEGER TimePtr;
    LARGE_INTEGER Time;
    NTSTATUS Status;
    RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME ActCtx;

    /* APCs must execute with the default activation context */
    if (bAlertable)
    {
        /* Setup the frame */
        RtlZeroMemory(&ActCtx, sizeof(ActCtx));
        ActCtx.Size = sizeof(ActCtx);
        ActCtx.Format = RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER;
        RtlActivateActivationContextUnsafeFast(&ActCtx, NULL);
    }

    /* Get real handle */
    hHandle = TranslateStdHandle(hHandle);

    /* Check for console handle */
    if ((IsConsoleHandle(hHandle)) && (VerifyConsoleIoHandle(hHandle)))
    {
        /* Get the real wait handle */
        hHandle = GetConsoleInputWaitHandle();
    }

    /* Convert the timeout */
    TimePtr = BaseFormatTimeOut(&Time, dwMilliseconds);

    /* Start wait loop */
    do
    {
        /* Do the wait */
        Status = NtWaitForSingleObject(hHandle, (BOOLEAN)bAlertable, TimePtr);
        if (!NT_SUCCESS(Status))
        {
            /* The wait failed */
            BaseSetLastNTError(Status);
            Status = WAIT_FAILED;
        }
    } while ((Status == STATUS_ALERTED) && (bAlertable));

    /* Cleanup the activation context */
    if (bAlertable) RtlDeactivateActivationContextUnsafeFast(&ActCtx);

    /* Return wait status */
    return Status;
}

/*
 * @implemented
 */
DWORD
WINAPI
WaitForMultipleObjects(IN DWORD nCount,
                       IN CONST HANDLE *lpHandles,
                       IN BOOL bWaitAll,
                       IN DWORD dwMilliseconds)
{
    /* Call the extended API */
    return WaitForMultipleObjectsEx(nCount,
                                    lpHandles,
                                    bWaitAll,
                                    dwMilliseconds,
                                    FALSE);
}

/*
 * @implemented
 */
DWORD
WINAPI
WaitForMultipleObjectsEx(IN DWORD nCount,
                         IN CONST HANDLE *lpHandles,
                         IN BOOL bWaitAll,
                         IN DWORD dwMilliseconds,
                         IN BOOL bAlertable)
{
    PLARGE_INTEGER TimePtr;
    LARGE_INTEGER Time;
    PHANDLE HandleBuffer;
    HANDLE Handle[8];
    DWORD i;
    NTSTATUS Status;
    RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME ActCtx;

    /* APCs must execute with the default activation context */
    if (bAlertable)
    {
        /* Setup the frame */
        RtlZeroMemory(&ActCtx, sizeof(ActCtx));
        ActCtx.Size = sizeof(ActCtx);
        ActCtx.Format = RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER;
        RtlActivateActivationContextUnsafeFast(&ActCtx, NULL);
    }

    /* Check if we have more handles then we locally optimize */
    if (nCount > 8)
    {
        /* Allocate a buffer for them */
        HandleBuffer = RtlAllocateHeap(RtlGetProcessHeap(),
                                       0,
                                       nCount * sizeof(HANDLE));
        if (!HandleBuffer)
        {
            /* No buffer, fail the wait */
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            if (bAlertable) RtlDeactivateActivationContextUnsafeFast(&ActCtx);
            return WAIT_FAILED;
        }
    }
    else
    {
        /* Otherwise, use our local buffer */
        HandleBuffer = Handle;
    }

    /* Copy the handles into our buffer and loop them all */
    RtlCopyMemory(HandleBuffer, (LPVOID)lpHandles, nCount * sizeof(HANDLE));
    for (i = 0; i < nCount; i++)
    {
        /* Check what kind of handle this is */
        HandleBuffer[i] = TranslateStdHandle(HandleBuffer[i]);

        /* Check for console handle */
        if ((IsConsoleHandle(HandleBuffer[i])) &&
            (VerifyConsoleIoHandle(HandleBuffer[i])))
        {
            /* Get the real wait handle */
            HandleBuffer[i] = GetConsoleInputWaitHandle();
        }
    }

    /* Convert the timeout */
    TimePtr = BaseFormatTimeOut(&Time, dwMilliseconds);

    /* Start wait loop */
    do
    {
        /* Do the wait */
        Status = NtWaitForMultipleObjects(nCount,
                                          HandleBuffer,
                                          bWaitAll ? WaitAll : WaitAny,
                                          (BOOLEAN)bAlertable,
                                          TimePtr);
        if (!NT_SUCCESS(Status))
        {
            /* Wait failed */
            BaseSetLastNTError(Status);
            Status = WAIT_FAILED;
        }
    } while ((Status == STATUS_ALERTED) && (bAlertable));

    /* Check if we didn't use our local buffer */
    if (HandleBuffer != Handle)
    {
        /* Free the allocated one */
        RtlFreeHeap(RtlGetProcessHeap(), 0, HandleBuffer);
    }

    /* Cleanup the activation context */
    if (bAlertable) RtlDeactivateActivationContextUnsafeFast(&ActCtx);

    /* Return wait status */
    return Status;
}

/* === Windows 8+ WaitOnAddress/WakeByAddress* true semantics using Keyed Events === */


typedef NTSTATUS (NTAPI *PFN_NT_CREATE_KEYED_EVENT)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, ULONG);
typedef NTSTATUS (NTAPI *PFN_NT_WAIT_FOR_KEYED_EVENT)(HANDLE, PVOID, BOOLEAN, PLARGE_INTEGER);
typedef NTSTATUS (NTAPI *PFN_NT_RELEASE_KEYED_EVENT)(HANDLE, PVOID, BOOLEAN, PLARGE_INTEGER);

static volatile LONG g_WoaInit = 0; /* 0=not init, 1=ok, -1=failed */
static HANDLE g_hKeyedEvent = NULL;
static PFN_NT_WAIT_FOR_KEYED_EVENT   g_pNtWaitForKeyedEvent = NULL;
static PFN_NT_RELEASE_KEYED_EVENT   g_pNtReleaseKeyedEvent = NULL;

static BOOL WoaEnsureInit(void)
{
    LONG state = g_WoaInit;
    if (state == 1) return TRUE;
    if (state == -1) return FALSE;
    if (InterlockedCompareExchange(&g_WoaInit, 1, 0) == 0)
    {
        HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
        if (!hNt) { g_WoaInit = -1; return FALSE; }
        PFN_NT_CREATE_KEYED_EVENT pNtCreateKeyedEvent = (PFN_NT_CREATE_KEYED_EVENT)GetProcAddress(hNt, "NtCreateKeyedEvent");
        g_pNtWaitForKeyedEvent    = (PFN_NT_WAIT_FOR_KEYED_EVENT)   GetProcAddress(hNt, "NtWaitForKeyedEvent");
        g_pNtReleaseKeyedEvent    = (PFN_NT_RELEASE_KEYED_EVENT)    GetProcAddress(hNt, "NtReleaseKeyedEvent");
        if (!pNtCreateKeyedEvent || !g_pNtWaitForKeyedEvent || !g_pNtReleaseKeyedEvent)
        { g_WoaInit = -1; return FALSE; }
        NTSTATUS st = pNtCreateKeyedEvent(&g_hKeyedEvent, GENERIC_READ | GENERIC_WRITE, NULL, 0);
        if (!NT_SUCCESS(st)) { g_hKeyedEvent = NULL; g_WoaInit = -1; return FALSE; }
        g_WoaInit = 1;
        return TRUE;
    }
    /* Another thread is initializing; spin briefly */
    while ((state = g_WoaInit) == 0) Sleep(0);
    return (state == 1);
}

static __inline BOOL WoaKeyedOk(void)
{
    return (g_hKeyedEvent != NULL) && g_pNtWaitForKeyedEvent && g_pNtReleaseKeyedEvent;
}

static __inline BOOL WoaSizeOk(SIZE_T s)
{
    return (s == 1 || s == 2 || s == 4 || s == 8);
}

static __inline BOOL WoaAligned(const volatile VOID* p, SIZE_T s)
{
    return (((ULONG_PTR)p) & (s - 1)) == 0;
}

/* Ensure visibility around the compare. Writers should use Interlocked/atomics. */
#if defined(_MSC_VER)
  #ifndef MemoryBarrier
    #include <intrin.h>
    #define MemoryBarrier() _ReadWriteBarrier()
  #endif
#else
  #ifndef MemoryBarrier
    #define MemoryBarrier() __sync_synchronize()
  #endif
#endif
static __inline BOOL BytesEqual(volatile const VOID* a, const VOID* b, SIZE_T n)
{
    MemoryBarrier(); /* acquire */
    BOOL eq = (RtlCompareMemory((const VOID*)a, b, n) == n);
    MemoryBarrier(); /* release */
    return eq;
}

/* Fallback polling loop if keyed events are unavailable */
static BOOL WoaPoll(volatile VOID* Address, const VOID* CompareAddress, SIZE_T AddressSize, DWORD dwMilliseconds)
{
    if (dwMilliseconds == 0)
    {
        if (BytesEqual(Address, CompareAddress, AddressSize))
        { SetLastError(WAIT_TIMEOUT); return FALSE; }
        return TRUE;
    }

    ULONGLONG deadline = (dwMilliseconds == INFINITE) ? 0 : (GetTickCount64() + dwMilliseconds);

    for (;;)
    {
        if (!BytesEqual(Address, CompareAddress, AddressSize))
            return TRUE;

        if (dwMilliseconds != INFINITE)
        {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline)
            { SetLastError(WAIT_TIMEOUT); return FALSE; }
            Sleep(1);
        }
        else
        {
            Sleep(1);
        }
    }
}

BOOL WINAPI
WaitOnAddress(IN volatile VOID* Address,
              IN CONST VOID* CompareAddress,
              IN SIZE_T AddressSize,
              IN DWORD dwMilliseconds)
{
    if (!WoaSizeOk(AddressSize) || !WoaAligned(Address, AddressSize))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!WoaEnsureInit() || !WoaKeyedOk())
        return WoaPoll(Address, CompareAddress, AddressSize, dwMilliseconds);

    if (dwMilliseconds == 0)
    {
        if (BytesEqual(Address, CompareAddress, AddressSize))
        { SetLastError(WAIT_TIMEOUT); return FALSE; }
        return TRUE;
    }

    /* Track absolute deadline to recompute remaining timeout across spurious wakes */
    ULONGLONG deadline = (dwMilliseconds == INFINITE) ? 0 : (GetTickCount64() + dwMilliseconds);

    if (!BytesEqual(Address, CompareAddress, AddressSize))
        return TRUE;

    for (;;)
    {
        if (!BytesEqual(Address, CompareAddress, AddressSize))
            return TRUE;

        LARGE_INTEGER liTimeout, *pTimeout = NULL;
        if (dwMilliseconds != INFINITE)
        {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline)
            { SetLastError(WAIT_TIMEOUT); return FALSE; }
            ULONGLONG remain_ms = deadline - now;
            liTimeout.QuadPart = -(LONGLONG)remain_ms * 10000LL; /* relative, 100ns */
            pTimeout = &liTimeout;
        }

        NTSTATUS st = g_pNtWaitForKeyedEvent(g_hKeyedEvent, (PVOID)Address, FALSE, pTimeout);
        if (st == STATUS_TIMEOUT)
        { SetLastError(WAIT_TIMEOUT); return FALSE; }
        if (!NT_SUCCESS(st))
        { SetLastError(RtlNtStatusToDosError(st)); return FALSE; }

        if (!BytesEqual(Address, CompareAddress, AddressSize))
            return TRUE;
        /* else spurious/already same: loop; remaining time recomputed each pass */
    }
}

VOID WINAPI
WakeByAddressSingle(IN PVOID Address)
{
    if (!WoaEnsureInit() || !WoaKeyedOk()) return; /* polling fallback: nothing to do */

    LARGE_INTEGER zero; zero.QuadPart = 0;
    (void)g_pNtReleaseKeyedEvent(g_hKeyedEvent, Address, FALSE, &zero);
}

VOID WINAPI
WakeByAddressAll(IN PVOID Address)
{
    if (!WoaEnsureInit() || !WoaKeyedOk()) return;

    LARGE_INTEGER zero; zero.QuadPart = 0;
    /* Drain ready waiters without blocking; cap iterations to avoid pathologies */
    for (int i = 0; i < 4096; ++i)
    {
        NTSTATUS st = g_pNtReleaseKeyedEvent(g_hKeyedEvent, Address, FALSE, &zero);
        DWORD err = RtlNtStatusToDosError(st);
        if (err == WAIT_TIMEOUT) break; /* no waiter ready at this instant */
        if (!NT_SUCCESS(st)) break;      /* best-effort */
    }
}

/* Optional tidy-up called from DllMain on process detach */
VOID
BaseSyncCleanup(VOID)
{
    if (g_hKeyedEvent)
    {
        CloseHandle(g_hKeyedEvent);
        g_hKeyedEvent = NULL;
    }
}

/*
 * @implemented
 */
DWORD
WINAPI
SignalObjectAndWait(IN HANDLE hObjectToSignal,
                    IN HANDLE hObjectToWaitOn,
                    IN DWORD dwMilliseconds,
                    IN BOOL bAlertable)
{
    PLARGE_INTEGER TimePtr;
    LARGE_INTEGER Time;
    NTSTATUS Status;
    RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME ActCtx;

    /* APCs must execute with the default activation context */
    if (bAlertable)
    {
        /* Setup the frame */
        RtlZeroMemory(&ActCtx, sizeof(ActCtx));
        ActCtx.Size = sizeof(ActCtx);
        ActCtx.Format = RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER;
        RtlActivateActivationContextUnsafeFast(&ActCtx, NULL);
    }

    /* Get real handle */
    hObjectToWaitOn = TranslateStdHandle(hObjectToWaitOn);

    /* Check for console handle */
    if ((IsConsoleHandle(hObjectToWaitOn)) &&
        (VerifyConsoleIoHandle(hObjectToWaitOn)))
    {
        /* Get the real wait handle */
        hObjectToWaitOn = GetConsoleInputWaitHandle();
    }

    /* Convert the timeout */
    TimePtr = BaseFormatTimeOut(&Time, dwMilliseconds);

    /* Start wait loop */
    do
    {
        /* Do the wait */
        Status = NtSignalAndWaitForSingleObject(hObjectToSignal,
                                                hObjectToWaitOn,
                                                (BOOLEAN)bAlertable,
                                                TimePtr);
        if (!NT_SUCCESS(Status))
        {
            /* The wait failed */
            BaseSetLastNTError(Status);
            Status = WAIT_FAILED;
        }
    } while ((Status == STATUS_ALERTED) && (bAlertable));

    /* Cleanup the activation context */
    if (bAlertable) RtlDeactivateActivationContextUnsafeFast(&ActCtx);

    /* Return wait status */
    return Status;
}

/*
 * @implemented
 */
HANDLE
WINAPI
CreateWaitableTimerW(IN LPSECURITY_ATTRIBUTES lpTimerAttributes OPTIONAL,
                     IN BOOL bManualReset,
                     IN LPCWSTR lpTimerName OPTIONAL)
{
    CreateNtObjectFromWin32Api(WaitableTimer, Timer, TIMER_ALL_ACCESS,
                               lpTimerAttributes,
                               lpTimerName,
                               bManualReset ? NotificationTimer : SynchronizationTimer);
}

/*
 * @implemented
 */
HANDLE
WINAPI
CreateWaitableTimerA(IN LPSECURITY_ATTRIBUTES lpTimerAttributes OPTIONAL,
                     IN BOOL bManualReset,
                     IN LPCSTR lpTimerName OPTIONAL)
{
    ConvertWin32AnsiObjectApiToUnicodeApi(WaitableTimer, lpTimerName, lpTimerAttributes, bManualReset);
}

/*
 * @implemented
 */
HANDLE
WINAPI
OpenWaitableTimerW(IN DWORD dwDesiredAccess,
                   IN BOOL bInheritHandle,
                   IN LPCWSTR lpTimerName)
{
    OpenNtObjectFromWin32Api(Timer, dwDesiredAccess, bInheritHandle, lpTimerName);
}

/*
 * @implemented
 */
HANDLE
WINAPI
OpenWaitableTimerA(IN DWORD dwDesiredAccess,
                   IN BOOL bInheritHandle,
                   IN LPCSTR lpTimerName)
{
    ConvertOpenWin32AnsiObjectApiToUnicodeApi(WaitableTimer, dwDesiredAccess, bInheritHandle, lpTimerName);
}

/*
 * @implemented
 */
BOOL
WINAPI
SetWaitableTimer(IN HANDLE hTimer,
                 IN const LARGE_INTEGER *pDueTime,
                 IN LONG lPeriod,
                 IN PTIMERAPCROUTINE pfnCompletionRoutine OPTIONAL,
                 IN OPTIONAL LPVOID lpArgToCompletionRoutine,
                 IN BOOL fResume)
{
    NTSTATUS Status;

    /* Set the timer */
    Status = NtSetTimer(hTimer,
                        (PLARGE_INTEGER)pDueTime,
                        (PTIMER_APC_ROUTINE)pfnCompletionRoutine,
                        lpArgToCompletionRoutine,
                        (BOOLEAN)fResume,
                        lPeriod,
                        NULL);
    if (NT_SUCCESS(Status)) return TRUE;

    /* If we got here, then we failed */
    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
WINAPI
CancelWaitableTimer(IN HANDLE hTimer)
{
    NTSTATUS Status;

    /* Cancel the timer */
    Status = NtCancelTimer(hTimer, NULL);
    if (NT_SUCCESS(Status)) return TRUE;

    /* If we got here, then we failed */
    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateSemaphoreA(IN LPSECURITY_ATTRIBUTES lpSemaphoreAttributes  OPTIONAL,
                 IN LONG lInitialCount,
                 IN LONG lMaximumCount,
                 IN LPCSTR lpName  OPTIONAL)
{
    ConvertWin32AnsiObjectApiToUnicodeApi(Semaphore, lpName, lpSemaphoreAttributes, lInitialCount, lMaximumCount);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateSemaphoreW(IN LPSECURITY_ATTRIBUTES lpSemaphoreAttributes  OPTIONAL,
                 IN LONG lInitialCount,
                 IN LONG lMaximumCount,
                 IN LPCWSTR lpName  OPTIONAL)
{
    CreateNtObjectFromWin32Api(Semaphore, Semaphore, SEMAPHORE_ALL_ACCESS,
                               lpSemaphoreAttributes,
                               lpName,
                               lInitialCount,
                               lMaximumCount);
}

HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateSemaphoreExW(IN LPSECURITY_ATTRIBUTES lpSemaphoreAttributes  OPTIONAL,
                   IN LONG lInitialCount,
                   IN LONG lMaximumCount,
                   IN LPCWSTR lpName  OPTIONAL,
                   IN DWORD dwFlags OPTIONAL,
                   IN DWORD dwDesiredAccess OPTIONAL)
{
    CreateNtObjectFromWin32Api(Semaphore, Semaphore, dwDesiredAccess,
                               lpSemaphoreAttributes,
                               lpName,
                               lInitialCount,
                               lMaximumCount);
}

HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateSemaphoreExA(IN LPSECURITY_ATTRIBUTES lpSemaphoreAttributes OPTIONAL,
                   IN LONG lInitialCount,
                   IN LONG lMaximumCount,
                   IN LPCSTR lpName  OPTIONAL,
                   IN DWORD dwFlags OPTIONAL,
                   IN DWORD dwDesiredAccess OPTIONAL)
{
    ConvertAnsiToUnicodePrologue

    if (!lpName)
    {
        return CreateSemaphoreExW(lpSemaphoreAttributes,
                                  lInitialCount,
                                  lMaximumCount,
                                  NULL,
                                  dwFlags,
                                  dwDesiredAccess);
    }

    ConvertAnsiToUnicodeBody(lpName)

    if (NT_SUCCESS(Status))
    {
        return CreateSemaphoreExW(lpSemaphoreAttributes,
                                  lInitialCount,
                                  lMaximumCount,
                                  UnicodeCache->Buffer,
                                  dwFlags,
                                  dwDesiredAccess);
    }

    ConvertAnsiToUnicodeEpilogue
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenSemaphoreA(IN DWORD dwDesiredAccess,
               IN BOOL bInheritHandle,
               IN LPCSTR lpName)
{
    ConvertOpenWin32AnsiObjectApiToUnicodeApi(Semaphore, dwDesiredAccess, bInheritHandle, lpName);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenSemaphoreW(IN DWORD dwDesiredAccess,
               IN BOOL bInheritHandle,
               IN LPCWSTR lpName)
{
    OpenNtObjectFromWin32Api(Semaphore, dwDesiredAccess, bInheritHandle, lpName);
}

/*
 * @implemented
 */
BOOL
WINAPI
DECLSPEC_HOTPATCH
ReleaseSemaphore(IN HANDLE hSemaphore,
                 IN LONG lReleaseCount,
                 IN LPLONG lpPreviousCount)
{
    NTSTATUS Status;

    /* Release the semaphore */
    Status = NtReleaseSemaphore(hSemaphore, lReleaseCount, lpPreviousCount);
    if (NT_SUCCESS(Status)) return TRUE;

    /* If we got here, then we failed */
    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateMutexA(IN LPSECURITY_ATTRIBUTES lpMutexAttributes  OPTIONAL,
             IN BOOL bInitialOwner,
             IN LPCSTR lpName  OPTIONAL)
{
    ConvertWin32AnsiObjectApiToUnicodeApi(Mutex, lpName, lpMutexAttributes, bInitialOwner);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateMutexW(IN LPSECURITY_ATTRIBUTES lpMutexAttributes  OPTIONAL,
             IN BOOL bInitialOwner,
             IN LPCWSTR lpName  OPTIONAL)
{
    CreateNtObjectFromWin32Api(Mutex, Mutant, MUTEX_ALL_ACCESS,
                               lpMutexAttributes,
                               lpName,
                               bInitialOwner);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenMutexA(IN DWORD dwDesiredAccess,
           IN BOOL bInheritHandle,
           IN LPCSTR lpName)
{
    ConvertOpenWin32AnsiObjectApiToUnicodeApi(Mutex, dwDesiredAccess, bInheritHandle, lpName);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenMutexW(IN DWORD dwDesiredAccess,
           IN BOOL bInheritHandle,
           IN LPCWSTR lpName)
{
    OpenNtObjectFromWin32Api(Mutant, dwDesiredAccess, bInheritHandle, lpName);
}

/*
 * @implemented
 */
BOOL
WINAPI
DECLSPEC_HOTPATCH
ReleaseMutex(IN HANDLE hMutex)
{
    NTSTATUS Status;

    /* Release the mutant */
    Status = NtReleaseMutant(hMutex, NULL);
    if (NT_SUCCESS(Status)) return TRUE;

    /* If we got here, then we failed */
    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateEventA(IN LPSECURITY_ATTRIBUTES lpEventAttributes  OPTIONAL,
             IN BOOL bManualReset,
             IN BOOL bInitialState,
             IN LPCSTR lpName OPTIONAL)
{
    ConvertWin32AnsiObjectApiToUnicodeApi(Event, lpName, lpEventAttributes, bManualReset, bInitialState);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
CreateEventW(IN LPSECURITY_ATTRIBUTES lpEventAttributes  OPTIONAL,
             IN BOOL bManualReset,
             IN BOOL bInitialState,
             IN LPCWSTR lpName  OPTIONAL)
{
    CreateNtObjectFromWin32Api(Event, Event, EVENT_ALL_ACCESS,
                               lpEventAttributes,
                               lpName,
                               bManualReset ? NotificationEvent : SynchronizationEvent,
                               bInitialState);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenEventA(IN DWORD dwDesiredAccess,
           IN BOOL bInheritHandle,
           IN LPCSTR lpName)
{
    ConvertOpenWin32AnsiObjectApiToUnicodeApi(Event, dwDesiredAccess, bInheritHandle, lpName);
}

/*
 * @implemented
 */
HANDLE
WINAPI
DECLSPEC_HOTPATCH
OpenEventW(IN DWORD dwDesiredAccess,
           IN BOOL bInheritHandle,
           IN LPCWSTR lpName)
{
    OpenNtObjectFromWin32Api(Event, dwDesiredAccess, bInheritHandle, lpName);
}

/*
 * @implemented
 */
BOOL
WINAPI
DECLSPEC_HOTPATCH
PulseEvent(IN HANDLE hEvent)
{
    NTSTATUS Status;

    /* Pulse the event */
    Status = NtPulseEvent(hEvent, NULL);
    if (NT_SUCCESS(Status)) return TRUE;

    /* If we got here, then we failed */
    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
WINAPI
DECLSPEC_HOTPATCH
ResetEvent(IN HANDLE hEvent)
{
    NTSTATUS Status;

    /* Clear the event */
    Status = NtResetEvent(hEvent, NULL);
    if (NT_SUCCESS(Status)) return TRUE;

    /* If we got here, then we failed */
    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
WINAPI
DECLSPEC_HOTPATCH
SetEvent(IN HANDLE hEvent)
{
    NTSTATUS Status;

    /* Set the event */
    Status = NtSetEvent(hEvent, NULL);
    if (NT_SUCCESS(Status)) return TRUE;

    /* If we got here, then we failed */
    BaseSetLastNTError(Status);
    return FALSE;
}

/*
 * @implemented
 */
VOID
WINAPI
InitializeCriticalSection(OUT LPCRITICAL_SECTION lpCriticalSection)
{
    NTSTATUS Status;

    /* Initialize the critical section and raise an exception if we failed */
    Status = RtlInitializeCriticalSection((PVOID)lpCriticalSection);
    if (!NT_SUCCESS(Status)) RtlRaiseStatus(Status);
}

/*
 * @implemented
 */
BOOL
WINAPI
InitializeCriticalSectionAndSpinCount(OUT LPCRITICAL_SECTION lpCriticalSection,
                                      IN DWORD dwSpinCount)
{
    NTSTATUS Status;

    /* Initialize the critical section */
    Status = RtlInitializeCriticalSectionAndSpinCount((PVOID)lpCriticalSection,
                                                      dwSpinCount);
    if (!NT_SUCCESS(Status))
    {
        /* Set failure code */
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* Success */
    return TRUE;
}

/*
 * @implemented
 */
VOID
WINAPI
DECLSPEC_HOTPATCH
Sleep(IN DWORD dwMilliseconds)
{
    /* Call the new API */
    SleepEx(dwMilliseconds, FALSE);
}


/*
 * @implemented
 */
DWORD
WINAPI
SleepEx(IN DWORD dwMilliseconds,
        IN BOOL bAlertable)
{
    LARGE_INTEGER Time;
    PLARGE_INTEGER TimePtr;
    NTSTATUS errCode;
    RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME ActCtx;

    /* APCs must execute with the default activation context */
    if (bAlertable)
    {
        /* Setup the frame */
        RtlZeroMemory(&ActCtx, sizeof(ActCtx));
        ActCtx.Size = sizeof(ActCtx);
        ActCtx.Format = RTL_CALLER_ALLOCATED_ACTIVATION_CONTEXT_STACK_FRAME_FORMAT_WHISTLER;
        RtlActivateActivationContextUnsafeFast(&ActCtx, NULL);
    }

    /* Convert the timeout */
    TimePtr = BaseFormatTimeOut(&Time, dwMilliseconds);
    if (!TimePtr)
    {
        /* Turn an infinite wait into a really long wait */
        Time.LowPart = 0;
        Time.HighPart = 0x80000000;
        TimePtr = &Time;
    }

    /* Loop the delay while APCs are alerting us */
    do
    {
        /* Do the delay */
        errCode = NtDelayExecution((BOOLEAN)bAlertable, TimePtr);
    }
    while ((bAlertable) && (errCode == STATUS_ALERTED));

    /* Cleanup the activation context */
    if (bAlertable) RtlDeactivateActivationContextUnsafeFast(&ActCtx);

    /* Return the correct code */
    return (errCode == STATUS_USER_APC) ? WAIT_IO_COMPLETION : 0;
}

/*
 * @implemented
 */
BOOL
WINAPI
RegisterWaitForSingleObject(OUT PHANDLE phNewWaitObject,
                            IN HANDLE hObject,
                            IN WAITORTIMERCALLBACK Callback,
                            IN PVOID Context,
                            IN ULONG dwMilliseconds,
                            IN ULONG dwFlags)
{
    NTSTATUS Status;

    /* Get real handle */
    hObject = TranslateStdHandle(hObject);

    /* Check for console handle */
    if ((IsConsoleHandle(hObject)) && (VerifyConsoleIoHandle(hObject)))
    {
        /* Get the real wait handle */
        hObject = GetConsoleInputWaitHandle();
    }

    /* Register the wait now */
    Status = RtlRegisterWait(phNewWaitObject,
                             hObject,
                             Callback,
                             Context,
                             dwMilliseconds,
                             dwFlags);
    if (!NT_SUCCESS(Status))
    {
        /* Return failure */
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* All good */
    return TRUE;
}

/*
 * @implemented
 */
HANDLE
WINAPI
RegisterWaitForSingleObjectEx(IN HANDLE hObject,
                              IN WAITORTIMERCALLBACK Callback,
                              IN PVOID Context,
                              IN ULONG dwMilliseconds,
                              IN ULONG dwFlags)
{
    NTSTATUS Status;
    HANDLE hNewWaitObject;

    /* Get real handle */
    hObject = TranslateStdHandle(hObject);

    /* Check for console handle */
    if ((IsConsoleHandle(hObject)) && (VerifyConsoleIoHandle(hObject)))
    {
        /* Get the real wait handle */
        hObject = GetConsoleInputWaitHandle();
    }

    /* Register the wait */
    Status = RtlRegisterWait(&hNewWaitObject,
                             hObject,
                             Callback,
                             Context,
                             dwMilliseconds,
                             dwFlags);
    if (!NT_SUCCESS(Status))
    {
        /* Return failure */
        BaseSetLastNTError(Status);
        return NULL;
    }

    /* Return the object */
    return hNewWaitObject;
}

/*
 * @implemented
 */
BOOL
WINAPI
UnregisterWait(IN HANDLE WaitHandle)
{
    NTSTATUS Status;

    /* Check for invalid handle */
    if (!WaitHandle)
    {
        /* Fail */
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    /* Deregister the wait and check status */
    Status = RtlDeregisterWaitEx(WaitHandle, NULL);
    if (!(NT_SUCCESS(Status)) || (Status == STATUS_PENDING))
    {
        /* Failure or non-blocking call */
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* All good */
    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
UnregisterWaitEx(IN HANDLE WaitHandle,
                 IN HANDLE CompletionEvent)
{
    NTSTATUS Status;

    /* Check for invalid handle */
    if (!WaitHandle)
    {
        /* Fail */
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    /* Deregister the wait and check status */
    Status = RtlDeregisterWaitEx(WaitHandle, CompletionEvent);
    if (!(NT_SUCCESS(Status)) ||
        ((CompletionEvent != INVALID_HANDLE_VALUE) && (Status == STATUS_PENDING)))
    {
        /* Failure or non-blocking call */
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* All good */
    return TRUE;
}

/* EOF */
