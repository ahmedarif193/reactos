@ stdcall AcquireSRWLockExclusive(ptr) kernel32.AcquireSRWLockExclusive
@ stdcall AcquireSRWLockShared(ptr) kernel32.AcquireSRWLockShared
@ stdcall InitOnceBeginInitialize(ptr long ptr ptr) kernel32.InitOnceBeginInitialize
@ stdcall InitOnceComplete(ptr long ptr) kernel32.InitOnceComplete
@ stdcall InitOnceExecuteOnce(ptr ptr ptr ptr) kernel32.InitOnceExecuteOnce
@ stdcall InitOnceInitialize(ptr) kernel32.InitOnceInitialize
@ stdcall InitializeConditionVariable(ptr) kernel32.InitializeConditionVariable
@ stdcall InitializeCriticalSectionEx(ptr long long) kernel32.InitializeCriticalSectionEx
@ stdcall InitializeSRWLock(ptr) kernel32.InitializeSRWLock
@ stdcall ReleaseSRWLockExclusive(ptr) kernel32.ReleaseSRWLockExclusive
@ stdcall ReleaseSRWLockShared(ptr) kernel32.ReleaseSRWLockShared
@ stdcall Sleep(long) kernel32.Sleep
@ stdcall SleepConditionVariableCS(ptr ptr long) kernel32.SleepConditionVariableCS
@ stdcall SleepConditionVariableSRW(ptr ptr long long) kernel32.SleepConditionVariableSRW
@ stdcall TryAcquireSRWLockExclusive(ptr) kernel32.TryAcquireSRWLockExclusive
@ stdcall TryAcquireSRWLockShared(ptr) kernel32.TryAcquireSRWLockShared
@ stdcall WaitOnAddress(ptr ptr long long) kernel32.WaitOnAddress
@ stdcall WakeByAddressAll(ptr) kernel32.WakeByAddressAll
@ stdcall WakeByAddressSingle(ptr) kernel32.WakeByAddressSingle
@ stdcall WakeConditionVariable(ptr) kernel32.WakeConditionVariable
@ stdcall WakeAllConditionVariable(ptr) kernel32.WakeAllConditionVariable
