#ifndef KJK_INTRIN_ARM64_H_
#define KJK_INTRIN_ARM64_H_

#ifndef __GNUC__
#error Unsupported compiler
#endif

#include <stdint.h>

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#define _ReturnAddress() (__builtin_return_address(0))
#define _ReadWriteBarrier() __sync_synchronize()

#if !__has_builtin(__debugbreak)
__INTRIN_INLINE void __cdecl __debugbreak(void)
{
    __asm__ __volatile__("brk #0xf000" ::: "memory");
}
#else
void __cdecl __debugbreak(void);
#endif

#if !__has_builtin(__yield)
__INTRIN_INLINE void __yield(void)
{
    __asm__ __volatile__("yield");
}
#else
void __yield(void);
#endif

#if !__has_builtin(__break)
__INTRIN_INLINE void __break(unsigned int value)
{
    (void)value;
    __asm__ __volatile__("brk #0xf000");
}
#else
void __break(int);
#endif

#if !__has_builtin(_BitScanForward64)
__INTRIN_INLINE unsigned char _BitScanForward64(unsigned long *Index, unsigned long long Mask)
{
    if (!Mask)
        return 0;
    *Index = (unsigned long)__builtin_ctzll(Mask);
    return 1;
}
#else
unsigned char _BitScanForward64(unsigned long *, unsigned long long);
#endif

#if !__has_builtin(_BitScanReverse64)
__INTRIN_INLINE unsigned char _BitScanReverse64(unsigned long *Index, unsigned long long Mask)
{
    if (!Mask)
        return 0;
    *Index = (unsigned long)(63 - __builtin_clzll(Mask));
    return 1;
}
#else
unsigned char _BitScanReverse64(unsigned long *, unsigned long long);
#endif

#if !__has_builtin(_BitScanForward)
__INTRIN_INLINE unsigned char _BitScanForward(unsigned long *Index, unsigned long Mask)
{
    if (!Mask)
        return 0;
    *Index = (unsigned long)__builtin_ctzl(Mask);
    return 1;
}
#else
unsigned char _BitScanForward(unsigned long *, unsigned long);
#endif

#if !__has_builtin(_BitScanReverse)
__INTRIN_INLINE unsigned char _BitScanReverse(unsigned long *Index, unsigned long Mask)
{
    if (!Mask)
        return 0;
    *Index = (unsigned long)(31 - __builtin_clzl(Mask));
    return 1;
}
#else
unsigned char _BitScanReverse(unsigned long *, unsigned long);
#endif

#if !__has_builtin(_interlockedbittestandset)
__INTRIN_INLINE unsigned char _interlockedbittestandset(volatile long *Base, long Bit)
{
    long mask = 1L << (Bit & 31);
    long previous = __atomic_fetch_or(Base, mask, __ATOMIC_SEQ_CST);
    return (previous & mask) != 0;
}
#else
unsigned char _interlockedbittestandset(volatile long *, long);
#endif

#if !__has_builtin(_interlockedbittestandreset)
__INTRIN_INLINE unsigned char _interlockedbittestandreset(volatile long *Base, long Bit)
{
    long mask = 1L << (Bit & 31);
    long previous = __atomic_fetch_and(Base, ~mask, __ATOMIC_SEQ_CST);
    return (previous & mask) != 0;
}
#else
unsigned char _interlockedbittestandreset(volatile long *, long);
#endif

#if !__has_builtin(_interlockedbittestandset64)
__INTRIN_INLINE unsigned char _interlockedbittestandset64(volatile long long *Base, long long Bit)
{
    long long mask = 1LL << (Bit & 63);
    volatile long long *word = &Base[Bit / 64];
    long long previous = __atomic_fetch_or(word, mask, __ATOMIC_SEQ_CST);
    return (previous & mask) != 0;
}
#else
unsigned char _interlockedbittestandset64(volatile long long *, long long);
#endif

#if !__has_builtin(_interlockedbittestandreset64)
__INTRIN_INLINE unsigned char _interlockedbittestandreset64(volatile long long *Base, long long Bit)
{
    long long mask = 1LL << (Bit & 63);
    volatile long long *word = &Base[Bit / 64];
    long long previous = __atomic_fetch_and(word, ~mask, __ATOMIC_SEQ_CST);
    return (previous & mask) != 0;
}
#else
unsigned char _interlockedbittestandreset64(volatile long long *, long long);
#endif

#if !__has_builtin(_InterlockedAdd64)
__INTRIN_INLINE long long _InterlockedAdd64(volatile long long *Addend, long long Value)
{
    return __atomic_add_fetch(Addend, Value, __ATOMIC_SEQ_CST);
}
#else
long long _InterlockedAdd64(volatile long long *, long long);
#endif

#if !__has_builtin(_InterlockedAnd64)
__INTRIN_INLINE long long _InterlockedAnd64(volatile long long *Destination, long long Value)
{
    unsigned long long oldValue;
    unsigned long long newValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %0, [%3]\n"      /* oldValue = *Destination (acquire) */
        "and %1, %0, %4\n"      /* newValue = oldValue & Value       */
        "stlxr %w2, %1, [%3]\n" /* attempt store (release)           */
        "cbnz %w2, 1b\n"        /* retry on failure                  */
        : "=&r"(oldValue), "=&r"(newValue), "=&r"(status)
        : "r"(Destination), "r"((unsigned long long)Value)
        : "memory");

    /* Windows _InterlockedAnd64 returns the new value */
    return (long long)newValue;
}
#else
long long _InterlockedAnd64(volatile long long *, long long);
#endif

#if !__has_builtin(_InterlockedIncrement64)
__INTRIN_INLINE long long _InterlockedIncrement64(volatile long long *Addend)
{
    return __atomic_add_fetch(Addend, 1, __ATOMIC_SEQ_CST);
}
#else
long long _InterlockedIncrement64(volatile long long *);
#endif

#if !__has_builtin(_InterlockedDecrement64)
__INTRIN_INLINE long long _InterlockedDecrement64(volatile long long *Addend)
{
    return __atomic_add_fetch(Addend, -1, __ATOMIC_SEQ_CST);
}
#else
long long _InterlockedDecrement64(volatile long long *);
#endif

/* 32-bit decrement for ARM64 – used via InterlockedDecrement.
 * Implemented with ldaxr/stlxr to provide full seq_cst semantics. */
#if !__has_builtin(_InterlockedDecrement)
__INTRIN_INLINE long _InterlockedDecrement(volatile long *Addend)
{
    unsigned int oldValue;
    unsigned int newValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %w0, [%3]\n"      /* oldValue = *Addend (acquire) */
        "sub %w1, %w0, #1\n"     /* newValue = oldValue - 1      */
        "stlxr %w2, %w1, [%3]\n" /* attempt store (release)      */
        "cbnz %w2, 1b\n"        /* retry on failure             */
        : "=&r"(oldValue), "=&r"(newValue), "=&r"(status)
        : "r"(Addend)
        : "memory");

    /* Windows _InterlockedDecrement returns the new value */
    return (long)newValue;
}
#else
long _InterlockedDecrement(volatile long *);
#endif

/* 32-bit increment for ARM64 – fallback only if the compiler lacks a builtin */
#if !__has_builtin(_InterlockedIncrement)
__INTRIN_INLINE long _InterlockedIncrement(volatile long *Addend)
{
    unsigned int oldValue;
    unsigned int newValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %w0, [%3]\n"      /* oldValue = *Addend (acquire) */
        "add %w1, %w0, #1\n"     /* newValue = oldValue + 1      */
        "stlxr %w2, %w1, [%3]\n" /* attempt store (release)      */
        "cbnz %w2, 1b\n"        /* retry on failure             */
        : "=&r"(oldValue), "=&r"(newValue), "=&r"(status)
        : "r"(Addend)
        : "memory");

    /* Windows _InterlockedIncrement returns the new value */
    return (long)newValue;
}
#else
long _InterlockedIncrement(volatile long *);
#endif
#if !__has_builtin(_InterlockedExchangeAdd64)
__INTRIN_INLINE long long _InterlockedExchangeAdd64(volatile long long *Addend, long long Value)
{
    unsigned long long oldValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %0, [%2]\n"      /* oldValue = *Addend (acquire)  */
        "add %0, %0, %3\n"      /* new = oldValue + Value        */
        "stlxr %w1, %0, [%2]\n" /* attempt store (release)       */
        "cbnz %w1, 1b\n"        /* retry on failure              */
        : "=&r"(oldValue), "=&r"(status)
        : "r"(Addend), "r"((unsigned long long)Value)
        : "memory");

    /* Windows _InterlockedExchangeAdd64 returns the original value */
    return (long long)(oldValue - (unsigned long long)Value);
}
#else
long long _InterlockedExchangeAdd64(volatile long long *, long long);
#endif

#if !__has_builtin(_InterlockedOr64)
__INTRIN_INLINE long long _InterlockedOr64(volatile long long *Destination, long long Value)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_SEQ_CST);
}
#else
long long _InterlockedOr64(volatile long long *, long long);
#endif

#if !__has_builtin(_InterlockedOr)
__INTRIN_INLINE long _InterlockedOr(volatile long *Destination, long Value)
{
    return __atomic_fetch_or(Destination, Value, __ATOMIC_SEQ_CST);
}
#else
long _InterlockedOr(volatile long *, long);
#endif

#if !__has_builtin(_InterlockedAnd)
__INTRIN_INLINE long _InterlockedAnd(volatile long *Destination, long Value)
{
    return __atomic_fetch_and(Destination, Value, __ATOMIC_SEQ_CST);
}
#else
long _InterlockedAnd(volatile long *, long);
#endif

#if !__has_builtin(_InterlockedCompareExchange64)
__INTRIN_INLINE long long _InterlockedCompareExchange64(volatile long long * const Destination,
                                                        const long long Exchange,
                                                        const long long Comparand)
{
    long long Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                0,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    return Expected;
}
#else
long long _InterlockedCompareExchange64(volatile long long *, long long, long long);
#endif

/* 32-bit CAS for ARM64 – used by guarded mutexes, resources, etc. */
#if !__has_builtin(_InterlockedCompareExchange)
__INTRIN_INLINE long _InterlockedCompareExchange(volatile long * const Destination,
                                                 const long Exchange,
                                                 const long Comparand)
{
    long Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                0,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    /* Return the original value */
    return Expected;
}
#else
long _InterlockedCompareExchange(volatile long *, long, long);
#endif

#if !__has_builtin(_InterlockedCompareExchange8)
__INTRIN_INLINE char _InterlockedCompareExchange8(volatile char * const Destination,
                                                  const char Exchange,
                                                  const char Comparand)
{
    char Expected = Comparand;
    __atomic_compare_exchange_n(Destination,
                                &Expected,
                                Exchange,
                                0,
                                __ATOMIC_SEQ_CST,
                                __ATOMIC_SEQ_CST);
    /* Return the original value */
    return Expected;
}
#else
char _InterlockedCompareExchange8(volatile char *, char, char);
#endif

#if !__has_builtin(_InterlockedExchange64)
__INTRIN_INLINE long long _InterlockedExchange64(volatile long long *Destination, long long Value)
{
    unsigned long long oldValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %0, [%2]\n"
        "stlxr %w1, %3, [%2]\n"
        "cbnz %w1, 1b\n"
        : "=&r"(oldValue), "=&r"(status)
        : "r"(Destination), "r"(Value)
        : "memory");

    return (long long)oldValue;
}
#else
long long _InterlockedExchange64(volatile long long *, long long);
#endif

/* 32-bit ExchangeAdd for ARM64 – used by _InterlockedIncrement/_Decrement. */
#if !__has_builtin(_InterlockedExchangeAdd)
__INTRIN_INLINE long _InterlockedExchangeAdd(volatile long *Addend, long Value)
{
    unsigned int oldValue;
    unsigned int newValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %w0, [%3]\n"      /* oldValue = *Addend (acquire)  */
        "add %w1, %w0, %w4\n"    /* newValue = oldValue + Value   */
        "stlxr %w2, %w1, [%3]\n" /* attempt store (release)       */
        "cbnz %w2, 1b\n"        /* retry on failure              */
        : "=&r"(oldValue), "=&r"(newValue), "=&r"(status)
        : "r"(Addend), "r"(Value)
        : "memory");

    /* Windows _InterlockedExchangeAdd returns the original value */
    return (long)oldValue;
}
#else
long _InterlockedExchangeAdd(volatile long *, long);
#endif

#if !__has_builtin(_InterlockedExchange)
__INTRIN_INLINE long _InterlockedExchange(volatile long *Destination, long Value)
{
    unsigned int oldValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %w0, [%2]\n"
        "stlxr %w1, %w3, [%2]\n"
        "cbnz %w1, 1b\n"
        : "=&r"(oldValue), "=&r"(status)
        : "r"(Destination), "r"(Value)
        : "memory");

    return (long)oldValue;
}
#else
long _InterlockedExchange(volatile long *, long);
#endif

#if !__has_builtin(_InterlockedExchange16)
__INTRIN_INLINE short _InterlockedExchange16(volatile short *Destination, short Value)
{
    unsigned int oldValue;
    unsigned int status;
    unsigned int newValue = (unsigned short)Value;

    __asm__ __volatile__(
        "1:\n"
        "ldaxrh %w0, [%2]\n"
        "stlxrh %w1, %w3, [%2]\n"
        "cbnz %w1, 1b\n"
        : "=&r"(oldValue), "=&r"(status)
        : "r"(Destination), "r"(newValue)
        : "memory");

    return (short)oldValue;
}
#else
short _InterlockedExchange16(volatile short *, short);
#endif

#if !__has_builtin(_InterlockedExchange8)
__INTRIN_INLINE char _InterlockedExchange8(volatile char *Destination, char Value)
{
    unsigned int oldValue;
    unsigned int status;
    unsigned int newValue = (unsigned char)Value;

    __asm__ __volatile__(
        "1:\n"
        "ldaxrb %w0, [%2]\n"
        "stlxrb %w1, %w3, [%2]\n"
        "cbnz %w1, 1b\n"
        : "=&r"(oldValue), "=&r"(status)
        : "r"(Destination), "r"(newValue)
        : "memory");

    return (char)oldValue;
}
#else
char _InterlockedExchange8(volatile char *, char);
#endif

#if !__has_builtin(_InterlockedExchangePointer)
__INTRIN_INLINE void * _InterlockedExchangePointer(void * volatile *Destination, void *Value)
{
    return (void *)_InterlockedExchange64((volatile long long *)Destination, (long long)Value);
}
#else
void *_InterlockedExchangePointer(void * volatile *, void *);
#endif

#if !__has_builtin(_InterlockedCompareExchangePointer)
__INTRIN_INLINE void * _InterlockedCompareExchangePointer(void * volatile *Destination,
                                                          void *Exchange,
                                                          void *Comperand)
{
    void *oldValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxr %0, [%2]\n"
        "cmp %0, %3\n"
        "b.ne 2f\n"
        "stlxr %w1, %4, [%2]\n"
        "cbnz %w1, 1b\n"
        "2:\n"
        : "=&r"(oldValue), "=&r"(status)
        : "r"(Destination), "r"(Comperand), "r"(Exchange)
        : "memory", "cc");

    return oldValue;
}
#else
void *_InterlockedCompareExchangePointer(void * volatile *, void *, void *);
#endif

#if !__has_builtin(_InterlockedOr16)
__INTRIN_INLINE short _InterlockedOr16(volatile short *Destination, short Value)
{
    unsigned int oldValue;
    unsigned int status;
    unsigned int newValue;
    unsigned int mask = (unsigned short)Value;

    __asm__ __volatile__(
        "1:\n"
        "ldaxrh %w0, [%3]\n"
        "orr %w2, %w0, %w4\n"
        "stlxrh %w1, %w2, [%3]\n"
        "cbnz %w1, 1b\n"
        : "=&r"(oldValue), "=&r"(status), "=&r"(newValue)
        : "r"(Destination), "r"(mask)
        : "memory");

    return (short)oldValue;
}
#else
short _InterlockedOr16(volatile short *, short);
#endif

#if !__has_builtin(_InterlockedIncrement16)
__INTRIN_INLINE short _InterlockedIncrement16(volatile short *Addend)
{
    unsigned int oldValue;
    unsigned int newValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxrh %w0, [%3]\n"      /* oldValue = *Addend (acquire) */
        "add %w1, %w0, #1\n"      /* newValue = oldValue + 1      */
        "stlxrh %w2, %w1, [%3]\n" /* attempt store (release)      */
        "cbnz %w2, 1b\n"          /* retry on failure             */
        : "=&r"(oldValue), "=&r"(newValue), "=&r"(status)
        : "r"(Addend)
        : "memory");

    /* Windows _InterlockedIncrement16 returns the new value */
    return (short)newValue;
}
#else
short _InterlockedIncrement16(volatile short *);
#endif

#if !__has_builtin(_InterlockedDecrement16)
__INTRIN_INLINE short _InterlockedDecrement16(volatile short *Addend)
{
    unsigned int oldValue;
    unsigned int newValue;
    unsigned int status;

    __asm__ __volatile__(
        "1:\n"
        "ldaxrh %w0, [%3]\n"      /* oldValue = *Addend (acquire) */
        "sub %w1, %w0, #1\n"      /* newValue = oldValue - 1      */
        "stlxrh %w2, %w1, [%3]\n" /* attempt store (release)      */
        "cbnz %w2, 1b\n"          /* retry on failure             */
        : "=&r"(oldValue), "=&r"(newValue), "=&r"(status)
        : "r"(Addend)
        : "memory");

    /* Windows _InterlockedDecrement16 returns the new value */
    return (short)newValue;
}
#else
short _InterlockedDecrement16(volatile short *);
#endif

#if !__has_builtin(_rotr8)
__INTRIN_INLINE unsigned char __cdecl _rotr8(unsigned char value, unsigned char shift)
{
    unsigned char amount = (unsigned char)(shift & 7);
    if (amount == 0)
        return value;
    return (unsigned char)((value >> amount) | (value << (unsigned char)(8 - amount)));
}
#else
unsigned char __cdecl _rotr8(unsigned char, unsigned char);
#endif

__INTRIN_INLINE unsigned short _byteswap_ushort(unsigned short value)
{
    return (unsigned short)__builtin_bswap16(value);
}

__INTRIN_INLINE unsigned long _byteswap_ulong(unsigned long value)
{
    return __builtin_bswap32(value);
}

__INTRIN_INLINE unsigned __int64 _byteswap_uint64(unsigned __int64 value)
{
    return __builtin_bswap64(value);
}

#endif /* KJK_INTRIN_ARM64_H_ */
