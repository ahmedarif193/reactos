# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

@ varargs DbgPrint(str) ChpeDbgPrint
@ varargs DbgPrintEx(long long str) ChpeDbgPrintEx
@ cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
@ stdcall ChpeDispatchExceptionNative(ptr ptr)
@ stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
@ stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
@ stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
@ stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeNtAllocateVirtualMemory
@ stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeNtAllocateVirtualMemoryEx
@ stdcall NtAlertThreadByThreadId(long) ChpeNtAlertThreadByThreadId
@ stdcall NtContinue(ptr long) ChpeNtContinue
@ stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
@ stdcall NtFlushInstructionCache(long ptr long) ChpeNtFlushInstructionCache
@ stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeNtFreeVirtualMemory
@ stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
@ stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeNtMapViewOfSection
@ stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
@ stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeNtProtectVirtualMemory
@ stdcall NtQuerySystemInformation(long ptr long ptr) ChpeNtQuerySystemInformation
@ stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
@ stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeNtReadFile
@ stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
@ stdcall NtTerminateProcess(long long) ChpeNtTerminateProcess
@ stdcall NtTerminateThread(long long) ChpeNtTerminateThread
@ stdcall NtUnmapViewOfSection(long ptr) ChpeNtUnmapViewOfSection
@ stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
@ stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeNtWaitForAlertByThreadId
@ stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeNtWriteVirtualMemory
@ stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
@ stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
@ stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
@ stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
@ stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
@ stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
@ stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContext
@ stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
@ stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
@ stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
@ cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
@ stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
@ stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
@ stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
@ stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
@ stdcall RtlFlsFree(long) ChpeRtlFlsFree
@ stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
@ stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
@ stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
@ stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
@ stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
@ stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
@ stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
@ stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
@ cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
@ stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
@ stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
@ stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
@ stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
@ stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
@ stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
@ stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
@ stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
@ stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
@ stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
@ stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
@ stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
@ stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
@ stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
@ stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
@ stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
@ stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
@ stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
@ stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
@ stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
@ stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
@ stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
@ stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
@ stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
@ stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
@ stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
@ stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
@ stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
@ stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
@ stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
@ stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
@ stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
@ varargs _snprintf(ptr long str) ChpeSnprintf
@ varargs _snwprintf(ptr long wstr) ChpeSnwprintf
@ varargs _swprintf(ptr wstr) ChpeSwprintf
@ varargs sprintf(ptr str) ChpeSprintf
@ varargs swprintf(ptr wstr) ChpeSwprintf
