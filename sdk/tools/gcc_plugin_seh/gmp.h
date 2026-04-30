/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     BSD Zero Clause License (https://spdx.org/licenses/0BSD)
 * PURPOSE:     Minimal GMP declarations required by GCC plugin headers
 */

#ifndef REACTOS_GCC_PLUGIN_SEH_GMP_H
#define REACTOS_GCC_PLUGIN_SEH_GMP_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
typedef unsigned long long mp_limb_t;
#else
typedef unsigned long mp_limb_t;
#endif

typedef long mp_size_t;
typedef unsigned long mp_bitcnt_t;

typedef struct
{
    int _mp_alloc;
    int _mp_size;
    mp_limb_t *_mp_d;
} __mpz_struct;

typedef __mpz_struct mpz_t[1];
typedef __mpz_struct *mpz_ptr;
typedef const __mpz_struct *mpz_srcptr;

void mpz_init(mpz_ptr);
void mpz_clear(mpz_ptr);

#ifdef __cplusplus
}
#endif

#endif /* REACTOS_GCC_PLUGIN_SEH_GMP_H */
