/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Statically linked SRW lock and condition variable surface for llvm-mingw runtimes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * libc++ references the Win7 SRW lock APIs (directly and through dllimport slots) and the Vista condition
 * variable APIs (through dllimport slots). Bind them to the RTL implementation linked statically from
 * rtl_vista (sdk/lib/rtl/srw.c and condvar.c, which waits through the same static SRW functions) instead of
 * exporting them from kernel32_vista.dll: modules get one self-contained, consistent synchronization
 * implementation (ReactOS' lock layout is not Windows-compatible, mixing implementations on the same lock
 * would be fatal) and no import that real Windows cannot satisfy.
 *
 * No SDK headers here, they would declare these names dllimport.
 */

#if defined(__i386__)
#define SYNC_STDCALL __attribute__((__stdcall__))
#else
#define SYNC_STDCALL
#endif

typedef void *PSRWLOCK_VOID;
typedef long long SYNC_LONGLONG;

SYNC_STDCALL void RtlInitializeSRWLock(PSRWLOCK_VOID);
SYNC_STDCALL void RtlAcquireSRWLockExclusive(PSRWLOCK_VOID);
SYNC_STDCALL void RtlAcquireSRWLockShared(PSRWLOCK_VOID);
SYNC_STDCALL void RtlReleaseSRWLockExclusive(PSRWLOCK_VOID);
SYNC_STDCALL void RtlReleaseSRWLockShared(PSRWLOCK_VOID);
SYNC_STDCALL unsigned char RtlTryAcquireSRWLockExclusive(PSRWLOCK_VOID);
SYNC_STDCALL unsigned char RtlTryAcquireSRWLockShared(PSRWLOCK_VOID);
SYNC_STDCALL void RtlWakeConditionVariable(PSRWLOCK_VOID);
SYNC_STDCALL void RtlWakeAllConditionVariable(PSRWLOCK_VOID);
SYNC_STDCALL long RtlSleepConditionVariableSRW(PSRWLOCK_VOID, PSRWLOCK_VOID, SYNC_LONGLONG *, unsigned long);
SYNC_STDCALL unsigned long RtlNtStatusToDosError(long);
SYNC_STDCALL void SetLastError(unsigned long);

/* The kernel32 names, for direct calls */

SYNC_STDCALL void InitializeSRWLock(PSRWLOCK_VOID SRWLock)
{
    RtlInitializeSRWLock(SRWLock);
}

SYNC_STDCALL void AcquireSRWLockExclusive(PSRWLOCK_VOID SRWLock)
{
    RtlAcquireSRWLockExclusive(SRWLock);
}

SYNC_STDCALL void AcquireSRWLockShared(PSRWLOCK_VOID SRWLock)
{
    RtlAcquireSRWLockShared(SRWLock);
}

SYNC_STDCALL void ReleaseSRWLockExclusive(PSRWLOCK_VOID SRWLock)
{
    RtlReleaseSRWLockExclusive(SRWLock);
}

SYNC_STDCALL void ReleaseSRWLockShared(PSRWLOCK_VOID SRWLock)
{
    RtlReleaseSRWLockShared(SRWLock);
}

SYNC_STDCALL unsigned char TryAcquireSRWLockExclusive(PSRWLOCK_VOID SRWLock)
{
    return RtlTryAcquireSRWLockExclusive(SRWLock);
}

SYNC_STDCALL unsigned char TryAcquireSRWLockShared(PSRWLOCK_VOID SRWLock)
{
    return RtlTryAcquireSRWLockShared(SRWLock);
}

SYNC_STDCALL void WakeConditionVariable(PSRWLOCK_VOID ConditionVariable)
{
    RtlWakeConditionVariable(ConditionVariable);
}

SYNC_STDCALL void WakeAllConditionVariable(PSRWLOCK_VOID ConditionVariable)
{
    RtlWakeAllConditionVariable(ConditionVariable);
}

SYNC_STDCALL int SleepConditionVariableSRW(PSRWLOCK_VOID ConditionVariable, PSRWLOCK_VOID SRWLock,
                                           unsigned long Timeout, unsigned long Flags)
{
    long Status;
    SYNC_LONGLONG Time;
    SYNC_LONGLONG *TimePtr;

    if (Timeout == 0xFFFFFFFFul) /* INFINITE */
    {
        TimePtr = 0;
    }
    else
    {
        Time = (SYNC_LONGLONG)Timeout * -10000; /* relative time, 100ns units */
        TimePtr = &Time;
    }

    Status = RtlSleepConditionVariableSRW(ConditionVariable, SRWLock, TimePtr, Flags);
    if (Status < 0 || Status == 0x102) /* !NT_SUCCESS(Status) || STATUS_TIMEOUT */
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return 0;
    }
    return 1;
}

/* The dllimport slots, bound directly to the RTL functions where the signatures match */

#include "imp_alias.h"

IMP_ALIAS_STDCALL(InitializeSRWLock, 4, RtlInitializeSRWLock);
IMP_ALIAS_STDCALL(AcquireSRWLockExclusive, 4, RtlAcquireSRWLockExclusive);
IMP_ALIAS_STDCALL(AcquireSRWLockShared, 4, RtlAcquireSRWLockShared);
IMP_ALIAS_STDCALL(ReleaseSRWLockExclusive, 4, RtlReleaseSRWLockExclusive);
IMP_ALIAS_STDCALL(ReleaseSRWLockShared, 4, RtlReleaseSRWLockShared);
IMP_ALIAS_STDCALL(TryAcquireSRWLockExclusive, 4, RtlTryAcquireSRWLockExclusive);
IMP_ALIAS_STDCALL(TryAcquireSRWLockShared, 4, RtlTryAcquireSRWLockShared);
IMP_ALIAS_STDCALL(WakeConditionVariable, 4, RtlWakeConditionVariable);
IMP_ALIAS_STDCALL(WakeAllConditionVariable, 4, RtlWakeAllConditionVariable);
IMP_ALIAS_STDCALL(SleepConditionVariableSRW, 16, SleepConditionVariableSRW);
