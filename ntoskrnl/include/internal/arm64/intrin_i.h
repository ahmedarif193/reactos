#pragma once

#include <ndk/arm64/ketypes.h>

#ifdef __cplusplus
extern "C" {
#endif

extern KIRQL KeArm64CurrentIrql;

FORCEINLINE
VOID
KeSetCurrentIrql(
    _In_ KIRQL NewIrql)
{
    PKIPCR Pcr = KeGetPcr();

    if (Pcr != NULL)
    {
        Pcr->CurrentIrql = NewIrql;
        return;
    }

    KeArm64CurrentIrql = NewIrql;
}

#ifdef __cplusplus
}
#endif
