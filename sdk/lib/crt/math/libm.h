//
// libm.h
//
//      Partly imported from musl libc
//
// Support header for musl math code.
//
// SPDX-License-Identifier: MIT
//

#pragma once

#include <stdint.h>
#include <float.h>
#include <math.h>
#include <errno.h>

// Define _STATIC_ASSERT for compatibility with legacy CRT headers
#ifndef _STATIC_ASSERT
#define _STATIC_ASSERT(expr) extern char (*__static_assert__(void)) [(expr) ? 1 : -1]
#endif

// Make sure the sizes are correct
_STATIC_ASSERT(sizeof(float) == 4);
_STATIC_ASSERT(sizeof(double) == 8);
_STATIC_ASSERT(sizeof(long double) == 8);

_Check_return_ int __cdecl _isnanf(_In_ float _X);
double __cdecl __acrt_math_error(int type, const char *name, double arg1, double arg2, double retval);
double __cdecl log1p(double);
float __cdecl log1pf(float);
double __cdecl scalbn(double, int);
#define math_error __acrt_math_error
#undef isnan
#define isnan _isnan
#define isnanf _isnanf

// musl has this in math.h
static __inline uint32_t __FLOAT_BITS(float __f)
{
	union {float __f; uint32_t __i;} __u;
	__u.__f = __f;
	return __u.__i;
}

static __inline float __FLOAT_FROM_BITS(uint32_t __i)
{
	union {float __f; uint32_t __i;} __u;
	__u.__i = __i;
	return __u.__f;
}

static __inline uint64_t __DOUBLE_BITS(double __f)
{
	union {double __f; uint64_t __i;} __u;
	__u.__f = __f;
	return __u.__i;
}

static __inline double __DOUBLE_FROM_BITS(uint64_t __i)
{
	union {double __f; uint64_t __i;} __u;
	__u.__i = __i;
	return __u.__f;
}

#define GET_HIGH_WORD(hi, value) ((hi) = __DOUBLE_BITS(value) >> 32)
#define SET_LOW_WORD(value, lo) ((value) = __DOUBLE_FROM_BITS((__DOUBLE_BITS(value) & 0xffffffff00000000ULL) | (uint32_t)(lo)))
#define GET_FLOAT_WORD(word, value) ((word) = __FLOAT_BITS(value))
#define SET_FLOAT_WORD(value, word) ((value) = __FLOAT_FROM_BITS((uint32_t)(word)))

#undef signbit
#define signbit(x) \
	((sizeof(x) == sizeof(float)) ? (int)(__FLOAT_BITS(x)>>31) : \
	                                (int)(__DOUBLE_BITS(x)>>63))

static inline void fp_force_evalf(float x)
{
	volatile float y;
	y = x;
    (void)y;
}

static inline void fp_force_eval(double x)
{
	volatile double y;
	y = x;
    (void)y;
}

static inline float fp_barrierf(float x)
{
	volatile float y = x;
	return y;
}

static inline double fp_barrier(double x)
{
	volatile double y = x;
	return y;
}

#define FORCE_EVAL(x) do {                    \
	if (sizeof(x) == sizeof(float)) {         \
		fp_force_evalf(x);                    \
	} else {                                  \
		fp_force_eval(x);                     \
	}                                         \
} while(0)
