/*
 * PROJECT:         ReactOS GCC Compatibility Library
 * LICENSE:         GPL - See COPYING in the top level directory  
 * FILE:            sdk/lib/gcc-compat/rand_s_compat.c
 * PURPOSE:         rand_s compatibility for MinGW libstdc++
 * PROGRAMMERS:     Claude Code (automated fix)
 */

#include <windef.h>

#define EINVAL 22

/* Simple linear congruential generator state */
static unsigned long g_seed = 1;

/*
 * @implemented
 * 
 * Basic rand_s implementation for MinGW libstdc++ compatibility.
 * This provides the symbol that libstdc++ expects.
 */
int __stdcall rand_s(unsigned int *randomValue)
{
    if (!randomValue) return EINVAL;
    g_seed = (g_seed * 1103515245UL + 12345UL) & 0x7FFFFFFFUL;
    *randomValue = g_seed;
    return 0;
}

/* Create the import symbol that libstdc++ looks for */
int (__stdcall *__imp__rand_s)(unsigned int*) = rand_s;