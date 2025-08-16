/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 SList Support Functions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <rtl.h>

/* FUNCTIONS *****************************************************************/

/*
 * ARM64 implementation of 128-bit compare exchange
 * This is a simplified implementation using LDXP/STXP instructions
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
unsigned char 
_InterlockedCompareExchange128(
    volatile long long *Destination,
    long long ExchangeHigh,
    long long ExchangeLow,
    long long *ComparandResult)
{
    long long CurrentHigh, CurrentLow;
    unsigned char result = 0;
    
    /* Use ARM64 LDXP/STXP for 128-bit atomic operations */
    __asm__ __volatile__(
        "1:\n\t"
        "ldxp %[low], %[high], [%[dest]]\n\t"      /* Load exclusive pair */
        "cmp %[low], %[comp_low]\n\t"              /* Compare low part */
        "ccmp %[high], %[comp_high], #0, eq\n\t"   /* Compare high if low equal */
        "b.ne 2f\n\t"                               /* Branch if not equal */
        "stxp %w[result], %[new_low], %[new_high], [%[dest]]\n\t" /* Store exclusive pair */
        "cbnz %w[result], 1b\n\t"                  /* Retry if store failed */
        "mov %w[result], #1\n\t"                    /* Set success */
        "b 3f\n\t"
        "2:\n\t"
        "clrex\n\t"                                 /* Clear exclusive */
        "mov %w[result], #0\n\t"                    /* Set failure */
        "3:\n\t"
        : [result] "=&r" (result),
          [low] "=&r" (CurrentLow),
          [high] "=&r" (CurrentHigh)
        : [dest] "r" (Destination),
          [comp_low] "r" (ComparandResult[0]),
          [comp_high] "r" (ComparandResult[1]),
          [new_low] "r" (ExchangeLow),
          [new_high] "r" (ExchangeHigh)
        : "cc", "memory"
    );
    
    /* Store current values in ComparandResult */
    ComparandResult[0] = CurrentLow;
    ComparandResult[1] = CurrentHigh;
    
    return result;
}
#pragma GCC diagnostic pop

/* EOF */