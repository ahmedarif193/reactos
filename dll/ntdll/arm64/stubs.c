/*
 * PROJECT:     ReactOS NTDLL ARM64
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 stubs until full ntdll arm64 is implemented
 */

#include <ntdllp.h>

void _local_unwind2(void) {}
void _global_unwind2(void) {}
int _except_handler2(void) { return 1; }
int _except_handler3(void) { return 1; }

/*
 * ExpInterlockedPopEntrySList* entry points.
 * These are the ABI entry points for the kernel's SList fault/retry handling.
 * Currently reference-counted by the ntdll export table and matched against the
 * ARM64 trap handler (KiHandleKernelSListFaultArm64).  Passing through to the
 * RTL inline implementation which uses _InterlockedCompareExchange128.
 */
PSLIST_ENTRY NTAPI ExpInterlockedPopEntrySList(PSLIST_HEADER H)
{
    return RtlInterlockedPopEntrySList(H);
}

PSLIST_ENTRY NTAPI ExpInterlockedPopEntrySListEnd(PSLIST_HEADER H)
{
    return RtlInterlockedPopEntrySList(H);
}

PSLIST_ENTRY NTAPI ExpInterlockedPopEntrySListFault(PSLIST_HEADER H)
{
    return RtlInterlockedPopEntrySList(H);
}

PSLIST_ENTRY NTAPI ExpInterlockedPopEntrySListResume(PSLIST_HEADER H)
{
    return RtlInterlockedPopEntrySList(H);
}
