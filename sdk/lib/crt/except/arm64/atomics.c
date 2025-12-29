/*
 * PROJECT: ReactOS CRT (ARM64)
 * PURPOSE: Minimal GCC AArch64 atomic helper thunks used by MinGW CRT
 */

static inline int
atomic_cmpxchg_char(volatile char *destination, char *expected, char exchange, int success_order, int failure_order)
{
    return __atomic_compare_exchange_n(destination, expected, exchange, 0, success_order, failure_order);
}

static inline int
atomic_cmpxchg_short(volatile short *destination, short *expected, short exchange, int success_order, int failure_order)
{
    return __atomic_compare_exchange_n(destination, expected, exchange, 0, success_order, failure_order);
}

static inline int
atomic_cmpxchg_long(volatile long *destination, long *expected, long exchange, int success_order, int failure_order)
{
    return __atomic_compare_exchange_n(destination, expected, exchange, 0, success_order, failure_order);
}

static inline int
atomic_cmpxchg_longlong(volatile long long *destination, long long *expected, long long exchange, int success_order, int failure_order)
{
    return __atomic_compare_exchange_n(destination, expected, exchange, 0, success_order, failure_order);
}

char __aarch64_cas1_sync(char comperand, char exchange, volatile char *destination)
{
    char current = comperand;
    atomic_cmpxchg_char(destination, &current, exchange, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return current;
}

short __aarch64_cas2_sync(short comperand, short exchange, volatile short *destination)
{
    short current = comperand;
    atomic_cmpxchg_short(destination, &current, exchange, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return current;
}

long __aarch64_cas4_sync(long comperand, long exchange, volatile long *destination)
{
    long current = comperand;
    atomic_cmpxchg_long(destination, &current, exchange, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return current;
}

long long __aarch64_cas8_sync(long long comperand, long long exchange, volatile long long *destination)
{
    long long current = comperand;
    atomic_cmpxchg_longlong(destination, &current, exchange, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return current;
}

long __aarch64_ldadd4_sync(long value, volatile long *destination)
{
    return __atomic_fetch_add(destination, value, __ATOMIC_SEQ_CST);
}

short __aarch64_ldadd2_sync(short value, volatile short *destination)
{
    return __atomic_fetch_add(destination, value, __ATOMIC_SEQ_CST);
}

long long __aarch64_ldadd8_sync(long long value, volatile long long *destination)
{
    return __atomic_fetch_add(destination, value, __ATOMIC_SEQ_CST);
}

long __aarch64_swp4_sync(long value, volatile long *destination)
{
    return __atomic_exchange_n(destination, value, __ATOMIC_SEQ_CST);
}

long long __aarch64_swp8_sync(long long value, volatile long long *destination)
{
    return __atomic_exchange_n(destination, value, __ATOMIC_SEQ_CST);
}

long __aarch64_swp4_acq_rel(long value, volatile long *destination)
{
    return __atomic_exchange_n(destination, value, __ATOMIC_ACQ_REL);
}

long long __aarch64_swp8_acq_rel(long long value, volatile long long *destination)
{
    return __atomic_exchange_n(destination, value, __ATOMIC_ACQ_REL);
}

long __aarch64_ldset4_sync(long value, volatile long *destination)
{
    return __atomic_fetch_or(destination, value, __ATOMIC_SEQ_CST);
}

long __aarch64_ldclr4_sync(long value, volatile long *destination)
{
    return __atomic_fetch_and(destination, ~value, __ATOMIC_SEQ_CST);
}

long __aarch64_ldset4_acq_rel(long value, volatile long *destination)
{
    return __atomic_fetch_or(destination, value, __ATOMIC_ACQ_REL);
}

long __aarch64_ldclr4_acq_rel(long value, volatile long *destination)
{
    return __atomic_fetch_and(destination, ~value, __ATOMIC_ACQ_REL);
}

long long __aarch64_ldset8_acq_rel(long long value, volatile long long *destination)
{
    return __atomic_fetch_or(destination, value, __ATOMIC_ACQ_REL);
}

long long __aarch64_ldclr8_acq_rel(long long value, volatile long long *destination)
{
    return __atomic_fetch_and(destination, ~value, __ATOMIC_ACQ_REL);
}

char __aarch64_cas1_acq_rel(char comperand, char exchange, volatile char *destination)
{
    char current = comperand;
    atomic_cmpxchg_char(destination, &current, exchange, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return current;
}

short __aarch64_cas2_acq_rel(short comperand, short exchange, volatile short *destination)
{
    short current = comperand;
    atomic_cmpxchg_short(destination, &current, exchange, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return current;
}

long __aarch64_cas4_acq_rel(long comperand, long exchange, volatile long *destination)
{
    long current = comperand;
    atomic_cmpxchg_long(destination, &current, exchange, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return current;
}

long long __aarch64_cas8_acq_rel(long long comperand, long long exchange, volatile long long *destination)
{
    long long current = comperand;
    atomic_cmpxchg_longlong(destination, &current, exchange, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return current;
}

short __aarch64_ldadd2_acq_rel(short value, volatile short *destination)
{
    return __atomic_fetch_add(destination, value, __ATOMIC_ACQ_REL);
}

long __aarch64_ldadd4_acq_rel(long value, volatile long *destination)
{
    return __atomic_fetch_add(destination, value, __ATOMIC_ACQ_REL);
}

long long __aarch64_ldadd8_acq_rel(long long value, volatile long long *destination)
{
    return __atomic_fetch_add(destination, value, __ATOMIC_ACQ_REL);
}

long __aarch64_ldadd4_acq(long value, volatile long *destination)
{
    return __atomic_fetch_add(destination, value, __ATOMIC_ACQUIRE);
}

long __aarch64_ldset4_acq(long value, volatile long *destination)
{
    return __atomic_fetch_or(destination, value, __ATOMIC_ACQUIRE);
}

long __aarch64_ldclr4_acq(long value, volatile long *destination)
{
    return __atomic_fetch_and(destination, ~value, __ATOMIC_ACQUIRE);
}

long long __aarch64_ldset8_acq(long long value, volatile long long *destination)
{
    return __atomic_fetch_or(destination, value, __ATOMIC_ACQUIRE);
}

long long __aarch64_ldclr8_acq(long long value, volatile long long *destination)
{
    return __atomic_fetch_and(destination, ~value, __ATOMIC_ACQUIRE);
}

long __aarch64_swp4_acq(long value, volatile long *destination)
{
    return __atomic_exchange_n(destination, value, __ATOMIC_ACQUIRE);
}

long long __aarch64_swp8_acq(long long value, volatile long long *destination)
{
    return __atomic_exchange_n(destination, value, __ATOMIC_ACQUIRE);
}
