/*
 * ARM64 Interlocked Functions Stubs
 * These functions provide interlocked operations for ARM64
 */

#include <windef.h>

/* _InterlockedAdd64 - Atomic add for 64-bit values
 * TODO: Implement with proper ARM64 atomics
 */
__int64 _InterlockedAdd64(__int64 volatile * _Addend, __int64 _Value)
{
    /* Simple non-atomic implementation for now */
    /* TODO: Use ARM64 LDADD instruction or equivalent */
    __int64 old = *_Addend;
    *_Addend = old + _Value;
    return old;
}

__int64 _InterlockedAdd64_acq(__int64 volatile * _Addend, __int64 _Value)
{
    return _InterlockedAdd64(_Addend, _Value);
}

__int64 _InterlockedAdd64_nf(__int64 volatile * _Addend, __int64 _Value)
{
    return _InterlockedAdd64(_Addend, _Value);
}

__int64 _InterlockedAdd64_rel(__int64 volatile * _Addend, __int64 _Value)
{
    return _InterlockedAdd64(_Addend, _Value);
}

/* _InterlockedAdd - Atomic add for 32-bit values */
long _InterlockedAdd(long volatile * _Addend, long _Value)
{
    /* Simple non-atomic implementation for now */
    /* TODO: Use ARM64 LDADD instruction or equivalent */
    long old = *_Addend;
    *_Addend = old + _Value;
    return old;
}

long _InterlockedAdd_acq(long volatile * _Addend, long _Value)
{
    return _InterlockedAdd(_Addend, _Value);
}

long _InterlockedAdd_nf(long volatile * _Addend, long _Value)
{
    return _InterlockedAdd(_Addend, _Value);
}

long _InterlockedAdd_rel(long volatile * _Addend, long _Value)
{
    return _InterlockedAdd(_Addend, _Value);
}