/*
 * PROJECT:     ReactOS C++ runtime library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows threading primitives required by the LLVM C++ ABI
 * COPYRIGHT:   Copyright 2026 ReactOS RISC-V64 contributors
 *              Copyright 2026 Ahmed ARIF
 */

#define WIN32_LEAN_AND_MEAN
#include <windef.h>
#include <winbase.h>

namespace std
{
inline namespace __1
{

typedef void *__libcpp_mutex_t;
typedef void *__libcpp_condvar_t;

static_assert(sizeof(__libcpp_mutex_t) == sizeof(SRWLOCK),
              "libc++ mutex must match SRWLOCK");
static_assert(alignof(__libcpp_mutex_t) == alignof(SRWLOCK),
              "libc++ mutex alignment must match SRWLOCK");
static_assert(sizeof(__libcpp_condvar_t) == sizeof(CONDITION_VARIABLE),
              "libc++ condition variable must match CONDITION_VARIABLE");
static_assert(alignof(__libcpp_condvar_t) == alignof(CONDITION_VARIABLE),
              "libc++ condition-variable alignment must match Windows");

int __libcpp_mutex_lock(__libcpp_mutex_t *Mutex)
{
    AcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(Mutex));
    return 0;
}

int __libcpp_mutex_unlock(__libcpp_mutex_t *Mutex)
{
    ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(Mutex));
    return 0;
}

int __libcpp_condvar_wait(__libcpp_condvar_t *Condition,
                          __libcpp_mutex_t *Mutex)
{
    if (SleepConditionVariableSRW(
            reinterpret_cast<PCONDITION_VARIABLE>(Condition),
            reinterpret_cast<PSRWLOCK>(Mutex),
            INFINITE,
            0))
    {
        return 0;
    }

    return static_cast<int>(GetLastError());
}

int __libcpp_condvar_broadcast(__libcpp_condvar_t *Condition)
{
    WakeAllConditionVariable(
        reinterpret_cast<PCONDITION_VARIABLE>(Condition));
    return 0;
}

} // namespace __1
} // namespace std
