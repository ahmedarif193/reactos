#include <ntoskrnl.h>

extern volatile KIRQL KeArm64CurrentIrql;

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    return 0;
}

ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber)
{
    if (ProcNumber)
    {
        ProcNumber->Group = 0;
        ProcNumber->Number = 0;
        ProcNumber->Reserved = 0;
    }

    return 0;
}

KIRQL
FASTCALL
KfRaiseIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KeArm64CurrentIrql;
    KeArm64CurrentIrql = NewIrql;
    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(
    _In_ _IRQL_restores_ KIRQL NewIrql)
{
    KeArm64CurrentIrql = NewIrql;
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}
