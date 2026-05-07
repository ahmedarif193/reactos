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

unsigned __int64 __ull_rshift(unsigned __int64 value, int shift)
{ return value >> shift; }

unsigned __int64 __ll_rshift(unsigned __int64 value, int shift)
{ return (unsigned __int64)((long long)value >> shift); }

unsigned __int64 __ll_lshift(unsigned __int64 value, int shift)
{ return value << shift; }

double cos(double x) { (VOID)x; return 1.0; }
double fabs(double x) { return (x < 0) ? -x : x; }
double sin(double x) { (VOID)x; return 0.0; }

PSLIST_ENTRY ExpInterlockedPopEntrySListEnd(PSLIST_HEADER H) { (VOID)H; return NULL; }
PSLIST_ENTRY ExpInterlockedPopEntrySListFault(PSLIST_HEADER H) { (VOID)H; return NULL; }
PSLIST_ENTRY ExpInterlockedPopEntrySListResume(PSLIST_HEADER H) { (VOID)H; return NULL; }
