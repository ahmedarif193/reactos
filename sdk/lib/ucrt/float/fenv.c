//
// fenv.c
//
//      Copyright (c) ReactOS. Licensed under the MIT license.
//
// Floating point environment functions.
//
// The MSVC fenv_t encoding packs the abstract state of each FPU unit into
// _Fe_ctl/_Fe_stat by duplicating the abstract bits at a per-unit shift:
// x87 flags at <<16, SSE flags at <<24 (rounding at <<14 / <<22), ARM plain.
// The generic core below works on abstract per-unit values (exception masks
// 0x3f, rounding 0x300, unit-specific extra bits) and only the hardware
// accessors are per-architecture.
//
#include <fenv.h>
#include <float.h>

#define FE_DENORMAL 0x20
#define FENV_FLAG_MASK (FE_ALL_EXCEPT | FE_DENORMAL)

#define FENV_X_24 0x2000
#define FENV_X_53 0x1000
#define FENV_X_AFFINE 0x4000
#define FENV_X_EXTRA (FENV_X_24 | FENV_X_53 | FENV_X_AFFINE)
#define FENV_DN_FLUSH 0x400
#define FENV_DN_FLUSH_SAVE 0x800
#define FENV_DN_EXTRA (FENV_DN_FLUSH | FENV_DN_FLUSH_SAVE)

/* ========================================================================
 * Hardware layer: fenv_hw_get/fenv_hw_set exchange per-unit abstract
 * control (masks | rounding | extra) and status (flags), fenv_hw_reset
 * restores the default environment.
 * ====================================================================== */

#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__) || \
    defined(_M_ARM) || defined(__arm__)

#define FENV_UNITS 1

static const struct { unsigned char flag_shift, rc_shift; unsigned short extra; unsigned long cw_mask; }
fenv_unit[FENV_UNITS] =
{
    { 0, 0, FENV_DN_FLUSH, _MCW_EM | _MCW_RC | _MCW_DN },
};

#define FPSCR_FLAG_MASK 0x0000009F
#define FPSCR_TRAP_MASK 0x00009F00
#define FPSCR_RMODE_MASK 0x00C00000
#define FPSCR_RMODE_RP 0x00400000
#define FPSCR_RMODE_RM 0x00800000
#define FPSCR_RMODE_RZ 0x00C00000
#define FPSCR_FZ 0x01000000
#define FPSCR_MANAGED (FPSCR_FLAG_MASK | FPSCR_TRAP_MASK | FPSCR_RMODE_MASK | FPSCR_FZ)

#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__aarch64__)

#define ARM64_FPCR 0x5A20
#define ARM64_FPSR 0x5A21

static unsigned int __getfpenv(void)
{
#ifdef _MSC_VER
    return (unsigned int)_ReadStatusReg(ARM64_FPCR) |
           ((unsigned int)_ReadStatusReg(ARM64_FPSR) & FPSCR_FLAG_MASK);
#else
    unsigned long long fpcr, fpsr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
    return (unsigned int)fpcr | ((unsigned int)fpsr & FPSCR_FLAG_MASK);
#endif
}

static void __setfpenv(unsigned int env)
{
#ifdef _MSC_VER
    _WriteStatusReg(ARM64_FPCR,
        (_ReadStatusReg(ARM64_FPCR) & ~(__int64)(FPSCR_MANAGED & ~FPSCR_FLAG_MASK)) |
        (env & (FPSCR_MANAGED & ~FPSCR_FLAG_MASK)));
    _WriteStatusReg(ARM64_FPSR,
        (_ReadStatusReg(ARM64_FPSR) & ~(__int64)FPSCR_FLAG_MASK) |
        (env & FPSCR_FLAG_MASK));
#else
    unsigned long long fpcr, fpsr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
    fpcr = (fpcr & ~(unsigned long long)(FPSCR_MANAGED & ~FPSCR_FLAG_MASK)) |
           (env & (FPSCR_MANAGED & ~FPSCR_FLAG_MASK));
    fpsr = (fpsr & ~(unsigned long long)FPSCR_FLAG_MASK) | (env & FPSCR_FLAG_MASK);
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
    __asm__ __volatile__("msr fpsr, %0" :: "r"(fpsr));
#endif
}

#else

static unsigned int __getfpenv(void)
{
#ifdef _MSC_VER
    return _MoveFromCoprocessor(10, 7, 1, 0, 0);
#else
    unsigned int fpscr;
    __asm__ __volatile__("vmrs %0, fpscr" : "=r"(fpscr));
    return fpscr;
#endif
}

static void __setfpenv(unsigned int env)
{
    unsigned int fpscr = (__getfpenv() & ~FPSCR_MANAGED) | (env & FPSCR_MANAGED);
#ifdef _MSC_VER
    _MoveToCoprocessor(fpscr, 10, 7, 1, 0, 0);
#else
    __asm__ __volatile__("vmsr fpscr, %0" :: "r"(fpscr));
#endif
}

#endif

static unsigned int flags_from_bits(unsigned int bits)
{
    unsigned int flags = 0;
    if (bits & 0x01) flags |= FE_INVALID;
    if (bits & 0x02) flags |= FE_DIVBYZERO;
    if (bits & 0x04) flags |= FE_OVERFLOW;
    if (bits & 0x08) flags |= FE_UNDERFLOW;
    if (bits & 0x10) flags |= FE_INEXACT;
    if (bits & 0x80) flags |= FE_DENORMAL;
    return flags;
}

static unsigned int bits_from_flags(unsigned int flags)
{
    unsigned int bits = 0;
    if (flags & FE_INVALID) bits |= 0x01;
    if (flags & FE_DIVBYZERO) bits |= 0x02;
    if (flags & FE_OVERFLOW) bits |= 0x04;
    if (flags & FE_UNDERFLOW) bits |= 0x08;
    if (flags & FE_INEXACT) bits |= 0x10;
    if (flags & FE_DENORMAL) bits |= 0x80;
    return bits;
}

static void fenv_hw_get(unsigned int* ctl, unsigned int* stat)
{
    unsigned int env = __getfpenv();

    ctl[0] = flags_from_bits(~(env >> 8) & 0x9F);
    switch (env & FPSCR_RMODE_MASK)
    {
        case FPSCR_RMODE_RP: ctl[0] |= FE_UPWARD; break;
        case FPSCR_RMODE_RM: ctl[0] |= FE_DOWNWARD; break;
        case FPSCR_RMODE_RZ: ctl[0] |= FE_TOWARDZERO; break;
    }
    if (env & FPSCR_FZ) ctl[0] |= FENV_DN_FLUSH;
    stat[0] = flags_from_bits(env & FPSCR_FLAG_MASK);
}

static void fenv_hw_set(const unsigned int* ctl, const unsigned int* stat)
{
    unsigned int env;

    env = (~bits_from_flags(ctl[0]) & 0x9F) << 8;
    switch (ctl[0] & FE_ROUND_MASK)
    {
        case FE_UPWARD: env |= FPSCR_RMODE_RP; break;
        case FE_DOWNWARD: env |= FPSCR_RMODE_RM; break;
        case FE_TOWARDZERO: env |= FPSCR_RMODE_RZ; break;
    }
    if (ctl[0] & FENV_DN_FLUSH) env |= FPSCR_FZ;
    env |= bits_from_flags(stat[0]);
    __setfpenv(env);
}

static void fenv_hw_reset(void)
{
    __setfpenv(0);
}

#elif defined(_M_AMD64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)

#ifdef _MSC_VER
#include <xmmintrin.h>
#endif

#define MXCSR_FLAG_MASK 0x003F
#define MXCSR_DAZ 0x0040
#define MXCSR_MASK_MASK 0x1F80
#define MXCSR_RC_MASK 0x6000
#define MXCSR_RC_DOWN 0x2000
#define MXCSR_RC_UP 0x4000
#define MXCSR_FZ 0x8000
#define MXCSR_DEFAULT 0x1F80

static unsigned int flags_from_bits(unsigned int bits)
{
    unsigned int flags = 0;
    if (bits & 0x01) flags |= FE_INVALID;
    if (bits & 0x02) flags |= FE_DENORMAL;
    if (bits & 0x04) flags |= FE_DIVBYZERO;
    if (bits & 0x08) flags |= FE_OVERFLOW;
    if (bits & 0x10) flags |= FE_UNDERFLOW;
    if (bits & 0x20) flags |= FE_INEXACT;
    return flags;
}

static unsigned int bits_from_flags(unsigned int flags)
{
    unsigned int bits = 0;
    if (flags & FE_INVALID) bits |= 0x01;
    if (flags & FE_DENORMAL) bits |= 0x02;
    if (flags & FE_DIVBYZERO) bits |= 0x04;
    if (flags & FE_OVERFLOW) bits |= 0x08;
    if (flags & FE_UNDERFLOW) bits |= 0x10;
    if (flags & FE_INEXACT) bits |= 0x20;
    return bits;
}

static unsigned int sse_ctl_from_csr(unsigned int csr)
{
    unsigned int ctl = flags_from_bits(csr >> 7);

    if (csr & MXCSR_RC_DOWN) ctl |= FE_DOWNWARD;
    if (csr & MXCSR_RC_UP) ctl |= FE_UPWARD;
    if ((csr & (MXCSR_DAZ | MXCSR_FZ)) == (MXCSR_DAZ | MXCSR_FZ))
        ctl |= FENV_DN_FLUSH;
    else if (csr & MXCSR_DAZ)
        ctl |= FENV_DN_FLUSH_SAVE;
    else if (csr & MXCSR_FZ)
        ctl |= FENV_DN_FLUSH | FENV_DN_FLUSH_SAVE;
    return ctl;
}

static unsigned int csr_from_sse_ctl(unsigned int ctl, unsigned int flags)
{
    unsigned int csr = bits_from_flags(ctl) << 7 | bits_from_flags(flags);

    if (ctl & FE_DOWNWARD) csr |= MXCSR_RC_DOWN;
    if (ctl & FE_UPWARD) csr |= MXCSR_RC_UP;
    switch (ctl & FENV_DN_EXTRA)
    {
        case FENV_DN_FLUSH: csr |= MXCSR_DAZ | MXCSR_FZ; break;
        case FENV_DN_FLUSH_SAVE: csr |= MXCSR_DAZ; break;
        case FENV_DN_FLUSH | FENV_DN_FLUSH_SAVE: csr |= MXCSR_FZ; break;
    }
    return csr;
}

static unsigned int __getcsr(void)
{
#ifdef _MSC_VER
    return _mm_getcsr();
#else
    unsigned int csr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(csr));
    return csr;
#endif
}

static void __setcsr(unsigned int csr)
{
#ifdef _MSC_VER
    _mm_setcsr(csr);
#else
    __asm__ __volatile__("ldmxcsr %0" :: "m"(csr));
#endif
}

#if defined(_M_IX86) || defined(__i386__)

#define FENV_UNITS 2

static const struct { unsigned char flag_shift, rc_shift; unsigned short extra; unsigned long cw_mask; }
fenv_unit[FENV_UNITS] =
{
    { 16, 14, FENV_X_EXTRA, _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC },
    { 24, 22, FENV_DN_EXTRA, _MCW_EM | _MCW_RC | _MCW_DN },
};

#define X87_FLAG_MASK 0x003F
#define X87_RC_MASK 0x0C00
#define X87_PC_MASK 0x0300
#define X87_PC_24 0x0000
#define X87_PC_53 0x0200
#define X87_PC_64 0x0300
#define X87_IC 0x1000
#define X87_CW_DEFAULT 0x027F

#define PF_XMMI64_INSTRUCTIONS_AVAILABLE 10
int __stdcall IsProcessorFeaturePresent(unsigned int);

static int sse2_supported = -1;

static int has_sse2(void)
{
    if (sse2_supported < 0)
        sse2_supported = IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE);
    return sse2_supported;
}

static unsigned short __getcw(void)
{
    unsigned short cw;
#ifdef _MSC_VER
    __asm { fnstcw cw }
#else
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
#endif
    return cw;
}

static void __setcw(unsigned short cw)
{
#ifdef _MSC_VER
    __asm { fldcw cw }
#else
    __asm__ __volatile__("fldcw %0" :: "m"(cw));
#endif
}

static unsigned short __getsw(void)
{
    unsigned short sw;
#ifdef _MSC_VER
    __asm { fnstsw sw }
#else
    __asm__ __volatile__("fnstsw %0" : "=m"(sw));
#endif
    return sw;
}

static void __setsw(unsigned short sw)
{
    unsigned int x87env[7];
#ifdef _MSC_VER
    __asm { fnstenv x87env }
#else
    __asm__ __volatile__("fnstenv %0" : "=m"(x87env));
#endif
    x87env[1] = (x87env[1] & ~(unsigned int)X87_FLAG_MASK) | (sw & X87_FLAG_MASK);
#ifdef _MSC_VER
    __asm { fldenv x87env }
#else
    __asm__ __volatile__("fldenv %0" :: "m"(x87env));
#endif
}

static void fenv_hw_get(unsigned int* ctl, unsigned int* stat)
{
    unsigned int cw = __getcw();
    unsigned int csr;

    ctl[0] = flags_from_bits(cw & X87_FLAG_MASK);
    ctl[0] |= ((cw & X87_RC_MASK) >> 10) << 8;
    switch (cw & X87_PC_MASK)
    {
        case X87_PC_24: ctl[0] |= FENV_X_24; break;
        case X87_PC_53: ctl[0] |= FENV_X_53; break;
    }
    if (cw & X87_IC) ctl[0] |= FENV_X_AFFINE;
    stat[0] = flags_from_bits(__getsw() & X87_FLAG_MASK);

    if (has_sse2())
    {
        csr = __getcsr();
        ctl[1] = sse_ctl_from_csr(csr);
        stat[1] = flags_from_bits(csr & MXCSR_FLAG_MASK);
    }
    else
    {
        ctl[1] = 0;
        stat[1] = 0;
    }
}

static void fenv_hw_set(const unsigned int* ctl, const unsigned int* stat)
{
    unsigned int cw;

    cw = bits_from_flags(ctl[0]);
    cw |= ((ctl[0] & FE_ROUND_MASK) >> 8) << 10;
    switch (ctl[0] & (FENV_X_24 | FENV_X_53))
    {
        case FENV_X_24: cw |= X87_PC_24; break;
        case FENV_X_53: cw |= X87_PC_53; break;
        default: cw |= X87_PC_64; break;
    }
    if (ctl[0] & FENV_X_AFFINE) cw |= X87_IC;
    __setcw((unsigned short)cw);
    __setsw((unsigned short)bits_from_flags(stat[0]));

    if (has_sse2())
        __setcsr(csr_from_sse_ctl(ctl[1], stat[1]));
}

static void fenv_hw_reset(void)
{
#ifdef _MSC_VER
    __asm { fninit }
#else
    __asm__ __volatile__("fninit");
#endif
    __setcw(X87_CW_DEFAULT);
    if (has_sse2())
        __setcsr(MXCSR_DEFAULT);
}

#else

#define FENV_UNITS 1

static const struct { unsigned char flag_shift, rc_shift; unsigned short extra; unsigned long cw_mask; }
fenv_unit[FENV_UNITS] =
{
    { 24, 22, FENV_DN_EXTRA, _MCW_EM | _MCW_RC | _MCW_DN },
};

static void fenv_hw_get(unsigned int* ctl, unsigned int* stat)
{
    unsigned int csr = __getcsr();

    ctl[0] = sse_ctl_from_csr(csr);
    stat[0] = flags_from_bits(csr & MXCSR_FLAG_MASK);
}

static void fenv_hw_set(const unsigned int* ctl, const unsigned int* stat)
{
    __setcsr(csr_from_sse_ctl(ctl[0], stat[0]));
}

static void fenv_hw_reset(void)
{
    __setcsr(MXCSR_DEFAULT);
}

#endif

#else
#error Unsupported architecture
#endif

/* ========================================================================
 * Generic core
 * ====================================================================== */

static unsigned long fenv_dup(unsigned int value, unsigned int shift)
{
    return (unsigned long)value << shift | value;
}

static unsigned long fenv_encode_flags(unsigned int flags, int unit)
{
    return fenv_dup(flags, fenv_unit[unit].flag_shift);
}

static unsigned long fenv_encode_ctl(unsigned int ctl, int unit)
{
    return fenv_dup(ctl & FENV_FLAG_MASK, fenv_unit[unit].flag_shift) |
           fenv_dup(ctl & FE_ROUND_MASK, fenv_unit[unit].rc_shift) |
           (ctl & fenv_unit[unit].extra);
}

static unsigned int fenv_decode_flags(unsigned long enc, int unit)
{
    unsigned int flags = 0, bit;

    for (bit = FE_INEXACT; bit <= FE_DENORMAL; bit <<= 1)
    {
        if ((enc & fenv_encode_flags(bit, unit)) == fenv_encode_flags(bit, unit))
            flags |= bit;
    }
    return flags;
}

static unsigned int fenv_decode_ctl(unsigned long enc, int unit)
{
    unsigned int ctl = fenv_decode_flags(enc, unit);
    unsigned int rc;

    for (rc = FE_DOWNWARD; rc <= FE_UPWARD; rc <<= 1)
    {
        if ((enc & fenv_dup(rc, fenv_unit[unit].rc_shift)) ==
            fenv_dup(rc, fenv_unit[unit].rc_shift))
        {
            ctl |= rc;
        }
    }
    return ctl | (enc & fenv_unit[unit].extra);
}

static int fenv_decode(unsigned long ctl_enc, unsigned long stat_enc,
                       unsigned int* ctl, unsigned int* stat)
{
    unsigned long c = 0, s = 0;
    int unit;

    for (unit = 0; unit < FENV_UNITS; unit++)
    {
        ctl[unit] = fenv_decode_ctl(ctl_enc, unit);
        stat[unit] = fenv_decode_flags(stat_enc, unit);
        c |= fenv_encode_ctl(ctl[unit], unit);
        s |= fenv_encode_flags(stat[unit], unit);
    }
    return c == ctl_enc && s == stat_enc;
}

int __cdecl fegetenv(fenv_t* env)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    int unit;

    fenv_hw_get(ctl, stat);
    env->_Fe_ctl = 0;
    env->_Fe_stat = 0;
    for (unit = 0; unit < FENV_UNITS; unit++)
    {
        env->_Fe_ctl |= fenv_encode_ctl(ctl[unit], unit);
        env->_Fe_stat |= fenv_encode_flags(stat[unit], unit);
    }
    return 0;
}

int __cdecl fesetenv(fenv_t const* env)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];

    if (!env->_Fe_ctl && !env->_Fe_stat)
    {
        _fpreset();
        return 0;
    }

    if (!fenv_decode(env->_Fe_ctl, env->_Fe_stat, ctl, stat))
        return 1;

    fenv_hw_set(ctl, stat);
    return 0;
}

int __cdecl fegetround(void)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];

    fenv_hw_get(ctl, stat);
    return ctl[0] & FE_ROUND_MASK;
}

int __cdecl fesetround(int round)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    int unit;

    if (round & ~FE_ROUND_MASK)
        return 1;

    fenv_hw_get(ctl, stat);
    for (unit = 0; unit < FENV_UNITS; unit++)
        ctl[unit] = (ctl[unit] & ~FE_ROUND_MASK) | round;
    fenv_hw_set(ctl, stat);
    return 0;
}

int __cdecl __fpe_flt_rounds(void)
{
    switch (fegetround())
    {
        case FE_TOWARDZERO: return 0;
        case FE_TONEAREST: return 1;
        case FE_UPWARD: return 2;
    }
    return 3;
}

int __cdecl fetestexcept(int flags)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    unsigned int all = 0;
    int unit;

    fenv_hw_get(ctl, stat);
    for (unit = 0; unit < FENV_UNITS; unit++)
        all |= stat[unit];
    return all & flags;
}

int __cdecl feclearexcept(int flags)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    int unit;

    flags &= FENV_FLAG_MASK;
    fenv_hw_get(ctl, stat);
    for (unit = 0; unit < FENV_UNITS; unit++)
        stat[unit] &= ~flags;
    fenv_hw_set(ctl, stat);
    return 0;
}

int __cdecl fegetexceptflag(fexcept_t* status, int excepts)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    int unit;

    fenv_hw_get(ctl, stat);
    *status = 0;
    for (unit = 0; unit < FENV_UNITS; unit++)
        *status |= fenv_encode_flags(stat[unit] & excepts, unit);
    return 0;
}

int __cdecl fesetexceptflag(fexcept_t const* status, int excepts)
{
    fenv_t env;
    unsigned long mask = 0;
    int unit;

    excepts &= FENV_FLAG_MASK;
    if (!excepts)
        return 0;

    for (unit = 0; unit < FENV_UNITS; unit++)
        mask |= fenv_encode_flags(excepts, unit);
    fegetenv(&env);
    env._Fe_stat &= ~mask;
    env._Fe_stat |= *status & mask;
    return fesetenv(&env);
}

int __cdecl feholdexcept(fenv_t* env)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    int unit;

    fegetenv(env);
    fenv_hw_get(ctl, stat);
    for (unit = 0; unit < FENV_UNITS; unit++)
    {
        ctl[unit] |= FENV_FLAG_MASK;
        stat[unit] = 0;
    }
    fenv_hw_set(ctl, stat);
    return 0;
}

void __cdecl _fpreset(void)
{
    fenv_hw_reset();
}

static unsigned int cw_from_ctl(unsigned int ctl)
{
    unsigned int cw = (ctl & FE_ALL_EXCEPT) | (ctl & FE_ROUND_MASK);

    if (ctl & FE_DENORMAL) cw |= _EM_DENORMAL;
    switch (ctl & (FENV_X_24 | FENV_X_53))
    {
        case FENV_X_24: cw |= _PC_24; break;
        case FENV_X_53: cw |= _PC_53; break;
    }
    if (ctl & FENV_X_AFFINE) cw |= _IC_AFFINE;
    switch (ctl & FENV_DN_EXTRA)
    {
        case FENV_DN_FLUSH: cw |= _DN_FLUSH; break;
        case FENV_DN_FLUSH_SAVE: cw |= _DN_FLUSH_OPERANDS_SAVE_RESULTS; break;
        case FENV_DN_FLUSH | FENV_DN_FLUSH_SAVE: cw |= _DN_SAVE_OPERANDS_FLUSH_RESULTS; break;
    }
    return cw;
}

static unsigned int ctl_from_cw(unsigned int cw)
{
    unsigned int ctl = (cw & FE_ALL_EXCEPT) | (cw & FE_ROUND_MASK);

    if (cw & _EM_DENORMAL) ctl |= FE_DENORMAL;
    switch (cw & _MCW_PC)
    {
        case _PC_24: ctl |= FENV_X_24; break;
        case _PC_53: ctl |= FENV_X_53; break;
    }
    if (cw & _IC_AFFINE) ctl |= FENV_X_AFFINE;
    switch (cw & _MCW_DN)
    {
        case _DN_FLUSH: ctl |= FENV_DN_FLUSH; break;
        case _DN_FLUSH_OPERANDS_SAVE_RESULTS: ctl |= FENV_DN_FLUSH_SAVE; break;
        case _DN_SAVE_OPERANDS_FLUSH_RESULTS: ctl |= FENV_DN_FLUSH | FENV_DN_FLUSH_SAVE; break;
    }
    return ctl;
}

static unsigned int sw_from_stat(unsigned int stat)
{
    unsigned int sw = stat & FE_ALL_EXCEPT;

    if (stat & FE_DENORMAL) sw |= _SW_DENORMAL;
    return sw;
}

static unsigned int apply_cw(unsigned int ctl, unsigned int newval,
                             unsigned int mask, int unit)
{
    unsigned int m = mask & fenv_unit[unit].cw_mask;

    return ctl_from_cw((cw_from_ctl(ctl) & ~m) | (newval & m));
}

unsigned int __cdecl _control87(unsigned int newval, unsigned int mask)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    int unit;

    fenv_hw_get(ctl, stat);
    if (mask)
    {
        for (unit = 0; unit < FENV_UNITS; unit++)
            ctl[unit] = apply_cw(ctl[unit], newval, mask, unit);
        fenv_hw_set(ctl, stat);
        fenv_hw_get(ctl, stat);
    }
    return cw_from_ctl(ctl[0]);
}

unsigned int __cdecl _controlfp(unsigned int newval, unsigned int mask)
{
    return _control87(newval, mask & ~_EM_DENORMAL);
}

unsigned int __cdecl _statusfp(void)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    unsigned int all = 0;
    int unit;

    fenv_hw_get(ctl, stat);
    for (unit = 0; unit < FENV_UNITS; unit++)
        all |= sw_from_stat(stat[unit]);
    return all;
}

unsigned int __cdecl _clearfp(void)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];
    unsigned int all = 0;
    int unit;

    fenv_hw_get(ctl, stat);
    for (unit = 0; unit < FENV_UNITS; unit++)
    {
        all |= sw_from_stat(stat[unit]);
        stat[unit] = 0;
    }
    fenv_hw_set(ctl, stat);
    return all;
}

#if FENV_UNITS > 1

int __cdecl __control87_2(unsigned int newval, unsigned int mask,
                          unsigned int* x86_cw, unsigned int* sse_cw)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];

    fenv_hw_get(ctl, stat);
    if (mask)
    {
        if (x86_cw) ctl[0] = apply_cw(ctl[0], newval, mask, 0);
        if (sse_cw) ctl[1] = apply_cw(ctl[1], newval, mask, 1);
        fenv_hw_set(ctl, stat);
        fenv_hw_get(ctl, stat);
    }
    if (x86_cw) *x86_cw = cw_from_ctl(ctl[0]);
    if (sse_cw) *sse_cw = cw_from_ctl(ctl[1]);
    return 1;
}

void __cdecl _statusfp2(unsigned int* x86_sw, unsigned int* sse_sw)
{
    unsigned int ctl[FENV_UNITS], stat[FENV_UNITS];

    fenv_hw_get(ctl, stat);
    if (x86_sw) *x86_sw = sw_from_stat(stat[0]);
    if (sse_sw) *sse_sw = sw_from_stat(stat[1]);
}

#endif
