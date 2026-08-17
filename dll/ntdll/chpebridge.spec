# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

@ varargs DbgPrint(str) ChpeDbgPrint
@ varargs DbgPrintEx(long long str) ChpeDbgPrintEx
@ stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
@ stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
@ stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContext
@ stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
@ stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
@ stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
@ stdcall RtlFlsFree(long) ChpeRtlFlsFree
@ stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
@ stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
@ stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
@ stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
@ stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
@ stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
@ stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
@ stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
@ stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
@ stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
@ varargs _snprintf(ptr long str) ChpeSnprintf
@ varargs _snwprintf(ptr long wstr) ChpeSnwprintf
@ varargs _swprintf(ptr wstr) ChpeSwprintf
@ varargs sprintf(ptr str) ChpeSprintf
@ varargs swprintf(ptr wstr) ChpeSwprintf
