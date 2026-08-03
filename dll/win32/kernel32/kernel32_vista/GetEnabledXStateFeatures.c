#include "k32_vista.h"

/* Taken from Wine 11.14 kernelbase/memory.c. */

#if defined(_AMD64_) || defined(_X86_)

NTSYSAPI
ULONG64
NTAPI
RtlGetEnabledExtendedFeatures(
    _In_ ULONG64 FeatureMask);

/***********************************************************************
 *             GetEnabledXStateFeatures   (kernelbase.@)
 */
DWORD64 WINAPI GetEnabledXStateFeatures(void)
{
    return RtlGetEnabledExtendedFeatures(~(ULONG64)0);
}

#endif
