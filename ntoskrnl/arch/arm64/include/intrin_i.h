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
    KeArm64CurrentIrql = NewIrql;
}

#ifdef __cplusplus
}
#endif
