/*
    Compatibility <intrin.h> header for GCC/Clang on ARM64 Windows.
*/

#ifndef KJK_INTRIN_ARM64_H_
#define KJK_INTRIN_ARM64_H_

#ifndef __GNUC__
#error Unsupported compiler
#endif

#define _ReturnAddress() (__builtin_return_address(0))
#define _AddressOfReturnAddress() (__builtin_frame_address(0))

#if !HAS_BUILTIN(__break)
__INTRIN_INLINE void __break(int value)
{
    __asm__ __volatile__("brk %0" : : "i"(value));
}
#endif

#if !HAS_BUILTIN(_ReadWriteBarrier)
__INTRIN_INLINE void _ReadWriteBarrier(void)
{
    __asm__ __volatile__("" : : : "memory");
}
#endif

#define _ReadBarrier _ReadWriteBarrier
#define _WriteBarrier _ReadWriteBarrier

#if !HAS_BUILTIN(_disable)
__INTRIN_INLINE void _disable(void)
{
    __asm__ __volatile__("msr daifset, #0x2\n\tisb" : : : "memory");
}
#endif

#if !HAS_BUILTIN(_enable)
__INTRIN_INLINE void _enable(void)
{
    __asm__ __volatile__("msr daifclr, #0x2\n\tisb" : : : "memory");
}
#endif

#if !HAS_BUILTIN(_byteswap_ushort)
__INTRIN_INLINE unsigned short _byteswap_ushort(unsigned short value)
{
    return __builtin_bswap16(value);
}
#endif

#if !HAS_BUILTIN(_byteswap_ulong)
__INTRIN_INLINE unsigned long __cdecl _byteswap_ulong(unsigned long value)
{
    return __builtin_bswap32(value);
}
#endif

#if !HAS_BUILTIN(_byteswap_uint64)
__INTRIN_INLINE unsigned __int64 __cdecl _byteswap_uint64(unsigned __int64 value)
{
    return __builtin_bswap64(value);
}
#endif

#if !HAS_BUILTIN(_BitScanForward)
__INTRIN_INLINE unsigned char _BitScanForward(unsigned long *Index, unsigned long Mask)
{
    if (!Mask)
        return 0;

    *Index = __builtin_ctzl(Mask);
    return 1;
}
#endif

#if !HAS_BUILTIN(_BitScanReverse)
__INTRIN_INLINE unsigned char _BitScanReverse(unsigned long *Index, unsigned long Mask)
{
    if (!Mask)
        return 0;

    *Index = (sizeof(Mask) * 8 - 1) - __builtin_clzl(Mask);
    return 1;
}
#endif

#if !HAS_BUILTIN(_BitScanForward64)
__INTRIN_INLINE unsigned char _BitScanForward64(unsigned long *Index, unsigned long long Mask)
{
    if (!Mask)
        return 0;

    *Index = __builtin_ctzll(Mask);
    return 1;
}
#endif

#if !HAS_BUILTIN(_BitScanReverse64)
__INTRIN_INLINE unsigned char _BitScanReverse64(unsigned long *Index, unsigned long long Mask)
{
    if (!Mask)
        return 0;

    *Index = 63 - __builtin_clzll(Mask);
    return 1;
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange)
__INTRIN_INLINE long _InterlockedCompareExchange(volatile long *Destination, long Exchange, long Comparand)
{
    return __sync_val_compare_and_swap(Destination, Comparand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange16)
__INTRIN_INLINE short _InterlockedCompareExchange16(volatile short *Destination, short Exchange, short Comparand)
{
    return __sync_val_compare_and_swap(Destination, Comparand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange8)
__INTRIN_INLINE char _InterlockedCompareExchange8(volatile char *Destination, char Exchange, char Comparand)
{
    return __sync_val_compare_and_swap(Destination, Comparand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange64)
__INTRIN_INLINE long long _InterlockedCompareExchange64(volatile long long *Destination, long long Exchange, long long Comparand)
{
    return __sync_val_compare_and_swap(Destination, Comparand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchangePointer)
__INTRIN_INLINE void *_InterlockedCompareExchangePointer(void *volatile *Destination, void *Exchange, void *Comparand)
{
    return __sync_val_compare_and_swap(Destination, Comparand, Exchange);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchange)
__INTRIN_INLINE long _InterlockedExchange(volatile long *Target, long Value)
{
    return __sync_lock_test_and_set(Target, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchange16)
__INTRIN_INLINE short _InterlockedExchange16(volatile short *Target, short Value)
{
    return __sync_lock_test_and_set(Target, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchange8)
__INTRIN_INLINE char _InterlockedExchange8(volatile char *Target, char Value)
{
    return __sync_lock_test_and_set(Target, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchange64)
__INTRIN_INLINE long long _InterlockedExchange64(volatile long long *Target, long long Value)
{
    return __sync_lock_test_and_set(Target, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangePointer)
__INTRIN_INLINE void *_InterlockedExchangePointer(void *volatile *Target, void *Value)
{
    return __sync_lock_test_and_set(Target, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangeAdd)
__INTRIN_INLINE long _InterlockedExchangeAdd(volatile long *Addend, long Value)
{
    return __sync_fetch_and_add(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangeAdd16)
__INTRIN_INLINE short _InterlockedExchangeAdd16(volatile short *Addend, short Value)
{
    return __sync_fetch_and_add(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangeAdd8)
__INTRIN_INLINE char _InterlockedExchangeAdd8(volatile char *Addend, char Value)
{
    return __sync_fetch_and_add(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedExchangeAdd64)
__INTRIN_INLINE long long _InterlockedExchangeAdd64(volatile long long *Addend, long long Value)
{
    return __sync_fetch_and_add(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedAdd)
__INTRIN_INLINE long _InterlockedAdd(volatile long *Addend, long Value)
{
    return __sync_add_and_fetch(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedAdd64)
__INTRIN_INLINE long long _InterlockedAdd64(volatile long long *Addend, long long Value)
{
    return __sync_add_and_fetch(Addend, Value);
}
#endif

#if !HAS_BUILTIN(_InterlockedAnd)
__INTRIN_INLINE long _InterlockedAnd(volatile long *Value, long Mask)
{
    return __sync_fetch_and_and(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedAnd16)
__INTRIN_INLINE short _InterlockedAnd16(volatile short *Value, short Mask)
{
    return __sync_fetch_and_and(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedAnd8)
__INTRIN_INLINE char _InterlockedAnd8(volatile char *Value, char Mask)
{
    return __sync_fetch_and_and(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedAnd64)
__INTRIN_INLINE long long _InterlockedAnd64(volatile long long *Value, long long Mask)
{
    return __sync_fetch_and_and(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedOr)
__INTRIN_INLINE long _InterlockedOr(volatile long *Value, long Mask)
{
    return __sync_fetch_and_or(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedOr16)
__INTRIN_INLINE short _InterlockedOr16(volatile short *Value, short Mask)
{
    return __sync_fetch_and_or(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedOr8)
__INTRIN_INLINE char _InterlockedOr8(volatile char *Value, char Mask)
{
    return __sync_fetch_and_or(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedOr64)
__INTRIN_INLINE long long _InterlockedOr64(volatile long long *Value, long long Mask)
{
    return __sync_fetch_and_or(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedXor)
__INTRIN_INLINE long _InterlockedXor(volatile long *Value, long Mask)
{
    return __sync_fetch_and_xor(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedXor16)
__INTRIN_INLINE short _InterlockedXor16(volatile short *Value, short Mask)
{
    return __sync_fetch_and_xor(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedXor8)
__INTRIN_INLINE char _InterlockedXor8(volatile char *Value, char Mask)
{
    return __sync_fetch_and_xor(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedXor64)
__INTRIN_INLINE long long _InterlockedXor64(volatile long long *Value, long long Mask)
{
    return __sync_fetch_and_xor(Value, Mask);
}
#endif

#if !HAS_BUILTIN(_InterlockedIncrement)
__INTRIN_INLINE long _InterlockedIncrement(volatile long *Addend)
{
    return __sync_add_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedIncrement16)
__INTRIN_INLINE short _InterlockedIncrement16(volatile short *Addend)
{
    return __sync_add_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedIncrement64)
__INTRIN_INLINE long long _InterlockedIncrement64(volatile long long *Addend)
{
    return __sync_add_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedDecrement)
__INTRIN_INLINE long _InterlockedDecrement(volatile long *Addend)
{
    return __sync_sub_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedDecrement16)
__INTRIN_INLINE short _InterlockedDecrement16(volatile short *Addend)
{
    return __sync_sub_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedDecrement64)
__INTRIN_INLINE long long _InterlockedDecrement64(volatile long long *Addend)
{
    return __sync_sub_and_fetch(Addend, 1);
}
#endif

#if !HAS_BUILTIN(_InterlockedCompareExchange128)
__INTRIN_INLINE unsigned char _InterlockedCompareExchange128(volatile __int64 *Destination, __int64 ExchangeHigh, __int64 ExchangeLow, __int64 *ComparandResult)
{
    __int64 xchg[2] = { ExchangeLow, ExchangeHigh };
    __uint128_t expected = *(__uint128_t *)ComparandResult;
    __uint128_t desired = *(__uint128_t *)xchg;
    __uint128_t old = __sync_val_compare_and_swap((__uint128_t *)Destination, expected, desired);
    if (old == expected)
        return 1;
    *(__uint128_t *)ComparandResult = old;
    return 0;
}
#endif

#if !HAS_BUILTIN(_mm_pause)
__INTRIN_INLINE void _mm_pause(void)
{
    __asm__ __volatile__("yield" : : : "memory");
}
#endif

#if !HAS_BUILTIN(__nop)
__INTRIN_INLINE void __nop(void)
{
    __asm__ __volatile__("nop");
}
#endif

/*** Bit manipulation ***/
#if !HAS_BUILTIN(_bittest)
__INTRIN_INLINE unsigned char _bittest(const long *a, long b)
{
    return (a[b / (sizeof(long) * 8)] >> (b % (sizeof(long) * 8))) & 1;
}
#endif

#if !HAS_BUILTIN(_bittestandset)
__INTRIN_INLINE unsigned char _bittestandset(long *a, long b)
{
    long bit = 1L << (b % (sizeof(long) * 8));
    long *ptr = &a[b / (sizeof(long) * 8)];
    unsigned char retval = (*ptr >> (b % (sizeof(long) * 8))) & 1;
    *ptr |= bit;
    return retval;
}
#endif

#if !HAS_BUILTIN(_bittestandreset)
__INTRIN_INLINE unsigned char _bittestandreset(long *a, long b)
{
    long bit = 1L << (b % (sizeof(long) * 8));
    long *ptr = &a[b / (sizeof(long) * 8)];
    unsigned char retval = (*ptr >> (b % (sizeof(long) * 8))) & 1;
    *ptr &= ~bit;
    return retval;
}
#endif

#if !HAS_BUILTIN(_bittestandcomplement)
__INTRIN_INLINE unsigned char _bittestandcomplement(long *a, long b)
{
    long bit = 1L << (b % (sizeof(long) * 8));
    long *ptr = &a[b / (sizeof(long) * 8)];
    unsigned char retval = (*ptr >> (b % (sizeof(long) * 8))) & 1;
    *ptr ^= bit;
    return retval;
}
#endif

#if !HAS_BUILTIN(_bittest64)
__INTRIN_INLINE unsigned char _bittest64(const long long *a, long long b)
{
    return (a[b / 64] >> (b % 64)) & 1;
}
#endif

#if !HAS_BUILTIN(_bittestandset64)
__INTRIN_INLINE unsigned char _bittestandset64(long long *a, long long b)
{
    long long bit = 1LL << (b % 64);
    long long *ptr = &a[b / 64];
    unsigned char retval = (*ptr >> (b % 64)) & 1;
    *ptr |= bit;
    return retval;
}
#endif

#if !HAS_BUILTIN(_bittestandreset64)
__INTRIN_INLINE unsigned char _bittestandreset64(long long *a, long long b)
{
    long long bit = 1LL << (b % 64);
    long long *ptr = &a[b / 64];
    unsigned char retval = (*ptr >> (b % 64)) & 1;
    *ptr &= ~bit;
    return retval;
}
#endif

#if !HAS_BUILTIN(_bittestandcomplement64)
__INTRIN_INLINE unsigned char _bittestandcomplement64(long long *a, long long b)
{
    long long bit = 1LL << (b % 64);
    long long *ptr = &a[b / 64];
    unsigned char retval = (*ptr >> (b % 64)) & 1;
    *ptr ^= bit;
    return retval;
}
#endif

/*** Interlocked bit test ***/
#if !HAS_BUILTIN(_interlockedbittestandreset)
__INTRIN_INLINE unsigned char _interlockedbittestandreset(volatile long *a, long b)
{
    unsigned int bit = b & 31;
    long mask = 1L << bit;
    long old = __sync_fetch_and_and(a, ~mask);
    return (unsigned char)((old >> bit) & 1);
}
#endif

#if !HAS_BUILTIN(_interlockedbittestandset)
__INTRIN_INLINE unsigned char _interlockedbittestandset(volatile long *a, long b)
{
    unsigned int bit = b & 31;
    long mask = 1L << bit;
    long old = __sync_fetch_and_or(a, mask);
    return (unsigned char)((old >> bit) & 1);
}
#endif

#if !HAS_BUILTIN(_interlockedbittestandreset64)
__INTRIN_INLINE unsigned char _interlockedbittestandreset64(volatile long long *a, long long b)
{
    unsigned int bit = b & 63;
    long long mask = 1LL << bit;
    long long old = __sync_fetch_and_and(a, ~mask);
    return (unsigned char)((old >> bit) & 1);
}
#endif

#if !HAS_BUILTIN(_interlockedbittestandset64)
__INTRIN_INLINE unsigned char _interlockedbittestandset64(volatile long long *a, long long b)
{
    unsigned int bit = b & 63;
    long long mask = 1LL << bit;
    long long old = __sync_fetch_and_or(a, mask);
    return (unsigned char)((old >> bit) & 1);
}
#endif

/*** Rotates ***/
#if !HAS_BUILTIN(_rotl8)
__INTRIN_INLINE unsigned char __cdecl _rotl8(unsigned char value, unsigned char shift)
{
    shift &= 7;
    if (!shift)
        return value;
    return (value << shift) | (value >> (8 - shift));
}
#endif

#if !HAS_BUILTIN(_rotl16)
__INTRIN_INLINE unsigned short __cdecl _rotl16(unsigned short value, unsigned char shift)
{
    shift &= 15;
    if (!shift)
        return value;
    return (value << shift) | (value >> (16 - shift));
}
#endif

#if !HAS_BUILTIN(_rotl)
__INTRIN_INLINE unsigned int __cdecl _rotl(unsigned int value, int shift)
{
    shift &= 31;
    if (!shift)
        return value;
    return (value << shift) | (value >> (32 - shift));
}
#endif

#if !HAS_BUILTIN(_rotl64)
__INTRIN_INLINE unsigned long long _rotl64(unsigned long long value, int shift)
{
    shift &= 63;
    if (!shift)
        return value;
    return (value << shift) | (value >> (64 - shift));
}
#endif

#if !HAS_BUILTIN(_rotr8)
__INTRIN_INLINE unsigned char __cdecl _rotr8(unsigned char value, unsigned char shift)
{
    shift &= 7;
    if (!shift)
        return value;
    return (value >> shift) | (value << (8 - shift));
}
#endif

#if !HAS_BUILTIN(_rotr16)
__INTRIN_INLINE unsigned short __cdecl _rotr16(unsigned short value, unsigned char shift)
{
    shift &= 15;
    if (!shift)
        return value;
    return (value >> shift) | (value << (16 - shift));
}
#endif

#if !HAS_BUILTIN(_rotr)
__INTRIN_INLINE unsigned int __cdecl _rotr(unsigned int value, int shift)
{
    shift &= 31;
    if (!shift)
        return value;
    return (value >> shift) | (value << (32 - shift));
}
#endif

#if !HAS_BUILTIN(_rotr64)
__INTRIN_INLINE unsigned long long _rotr64(unsigned long long value, int shift)
{
    shift &= 63;
    if (!shift)
        return value;
    return (value >> shift) | (value << (64 - shift));
}
#endif

#if !HAS_BUILTIN(_lrotl)
__INTRIN_INLINE unsigned long __cdecl _lrotl(unsigned long value, int shift)
{
    shift &= 31;
    if (!shift)
        return value;
    return (value << shift) | (value >> (32 - shift));
}
#endif

#if !HAS_BUILTIN(_lrotr)
__INTRIN_INLINE unsigned long __cdecl _lrotr(unsigned long value, int shift)
{
    shift &= 31;
    if (!shift)
        return value;
    return (value >> shift) | (value << (32 - shift));
}
#endif

/*** 64-bit shifts ***/
#if !HAS_BUILTIN(__ll_lshift)
__INTRIN_INLINE unsigned long long __ll_lshift(unsigned long long Mask, int Bit)
{
    return Mask << (Bit & 0x3F);
}
#endif

#if !HAS_BUILTIN(__ll_rshift)
__INTRIN_INLINE long long __ll_rshift(long long Mask, int Bit)
{
    return Mask >> (Bit & 0x3F);
}
#endif

#if !HAS_BUILTIN(__ull_rshift)
__INTRIN_INLINE unsigned long long __ull_rshift(unsigned long long Mask, int Bit)
{
    return Mask >> (Bit & 0x3F);
}
#endif

/*** Leading zero count and population count ***/
#if !HAS_BUILTIN(__lzcnt)
__INTRIN_INLINE unsigned int __lzcnt(unsigned int value)
{
    if (!value)
        return 32;
    return __builtin_clz(value);
}
#endif

#if !HAS_BUILTIN(__lzcnt16)
__INTRIN_INLINE unsigned short __lzcnt16(unsigned short value)
{
    if (!value)
        return 16;
    return (unsigned short)(__builtin_clz((unsigned int)value) - 16);
}
#endif

#if !HAS_BUILTIN(__lzcnt64)
__INTRIN_INLINE unsigned long long __lzcnt64(unsigned long long value)
{
    if (!value)
        return 64;
    return __builtin_clzll(value);
}
#endif

#if !HAS_BUILTIN(__popcnt)
__INTRIN_INLINE unsigned int __popcnt(unsigned int value)
{
    return __builtin_popcount(value);
}
#endif

#if !HAS_BUILTIN(__popcnt16)
__INTRIN_INLINE unsigned short __popcnt16(unsigned short value)
{
    return __builtin_popcount(value);
}
#endif

#if !HAS_BUILTIN(__popcnt64)
__INTRIN_INLINE unsigned long long __popcnt64(unsigned long long value)
{
    return __builtin_popcountll(value);
}
#endif

/*** 64-bit math ***/
#if !HAS_BUILTIN(__mulh)
__INTRIN_INLINE long long __mulh(long long a, long long b)
{
    return ((__int128)a * (__int128)b) >> 64;
}
#endif

#if !HAS_BUILTIN(__umulh)
__INTRIN_INLINE unsigned long long __umulh(unsigned long long a, unsigned long long b)
{
    return ((unsigned __int128)a * (unsigned __int128)b) >> 64;
}
#endif

#if !HAS_BUILTIN(_abs64)
__INTRIN_INLINE long long __cdecl _abs64(long long value)
{
    return (value >= 0) ? value : -value;
}
#endif

#endif
