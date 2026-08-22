# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

@ stdcall DbgBreakPoint() ChpeDbgBreakPoint
@ varargs DbgPrint(str) ChpeDbgPrint
@ varargs DbgPrintEx(long long str) ChpeDbgPrintEx
@ cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
@ stdcall ChpeDispatchExceptionNative(ptr ptr)
@ stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
@ stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
@ stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
@ stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
@ stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
@ stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeNtAllocateVirtualMemory
@ stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeNtAllocateVirtualMemoryEx
@ stdcall NtAlertThreadByThreadId(long) ChpeNtAlertThreadByThreadId
@ stdcall NtClose(long) ChpeNtClose
@ stdcall NtContinue(ptr long) ChpeNtContinue
@ stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
@ stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeNtFlushProcessWriteBuffers
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
@ stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
@ stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
@ stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
@ stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
@ stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
@ stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContext
@ stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
@ stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
@ stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
@ stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
@ stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
@ cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
@ stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
@ stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
@ stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
@ stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
@ stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
@ stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
@ stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
@ stdcall RtlFlsFree(long) ChpeRtlFlsFree
@ stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
@ stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
@ stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
@ stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
@ stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
@ stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
@ stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
@ stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
@ stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
@ stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
@ stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
@ stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
@ stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
@ cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
@ stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
@ stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
@ stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
@ stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
@ stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
@ stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
@ stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
@ stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
@ stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
@ stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
@ stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
@ stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
@ stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
@ stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
@ stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
@ stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
@ stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
@ stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
@ stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
@ stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
@ stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
@ stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
@ stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
@ stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
@ stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
@ stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
@ stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
@ stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
@ stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
@ stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
@ stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
@ stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
@ stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
@ stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
@ stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
@ stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
@ stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
@ stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
@ stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
@ stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
@ stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
@ stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
@ stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
@ stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
@ stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
@ stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
@ stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
@ stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
@ stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
@ stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
@ stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
@ stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
@ stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
@ stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
@ stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
@ stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
@ stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
@ stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
@ stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
@ stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
@ stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
@ stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
@ stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
@ stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
@ stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
@ stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
@ varargs _snprintf(ptr long str) ChpeSnprintf
@ varargs _snwprintf(ptr long wstr) ChpeSnwprintf
@ varargs _swprintf(ptr wstr) ChpeSwprintf
@ varargs sprintf(ptr str) ChpeSprintf
@ varargs swprintf(ptr wstr) ChpeSwprintf
@ cdecl __chkstk() ChpeChkStk
@ cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
@ cdecl memcpy(ptr ptr long) ChpeMemcpy
