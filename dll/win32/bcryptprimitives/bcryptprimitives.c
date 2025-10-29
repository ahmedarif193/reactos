/*
 * Minimal bcryptprimitives.dll stub for ReactOS
 * Provides ProcessPrng to satisfy UCRT/minGW runtime dependencies.
 */

#include <windef.h>
#include <winbase.h>

/*
 * ProcessPrng: On Windows this seeds or services the process PRNG in UCRT
 * using system entropy providers. Our minimal stub is a no-op; consumers that
 * actually need randomness should call BCryptGenRandom or RtlGenRandom.
 */
VOID WINAPI ProcessPrng(VOID)
{
    /* no-op */
}

