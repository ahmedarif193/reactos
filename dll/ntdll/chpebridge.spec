# PROJECT:     ReactOS ARM64EC runtime
# PURPOSE:     Native NTDLL bridge exports for emulated AMD64 imports
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

37 stdcall DbgBreakPoint() ChpeDbgBreakPoint
38 varargs DbgPrint(str) ChpeDbgPrint
39 varargs DbgPrintEx(long long str) ChpeDbgPrintEx
58 stdcall -version=0x600+ EtwEventActivityIdControl(long ptr) ChpeEtwEventActivityIdControl
61 stdcall -version=0x600+ EtwEventRegister(ptr ptr ptr ptr) ChpeEtwEventRegister
63 stdcall -version=0x600+ EtwEventUnregister(int64) ChpeEtwEventUnregister
64 stdcall -version=0x600+ EtwEventWrite(int64 ptr long ptr) ChpeEtwEventWrite
70 stdcall -version=0x600+ EtwEventWriteTransfer(int64 ptr ptr ptr long ptr) ChpeEtwEventWriteTransfer
1838 cdecl __C_specific_handler(ptr long ptr ptr) ChpeCSpecificHandler
1970 stdcall ChpeDispatchExceptionNative(ptr ptr)
104 stdcall KiUserExceptionDispatcher(ptr ptr) ChpeKiUserExceptionDispatcher
107 stdcall LdrAccessResource(ptr ptr ptr ptr) ChpeLdrAccessResource
109 stdcall LdrAddRefDll(long ptr) ChpeLdrAddRefDll
111 stdcall LdrEnumResources(ptr ptr long ptr ptr) ChpeLdrEnumResources
113 stdcall LdrFindEntryForAddress(ptr ptr) ChpeLdrFindEntryForAddress
114 stdcall LdrFindResourceDirectory_U(ptr ptr long ptr) ChpeLdrFindResourceDirectory_U
116 stdcall LdrFindResource_U(ptr ptr long ptr) ChpeLdrFindResource_U
118 stdcall LdrGetDllHandle(wstr ptr ptr ptr) ChpeLdrGetDllHandle
119 stdcall LdrGetDllHandleEx(long wstr ptr ptr ptr) ChpeLdrGetDllHandleEx
123 stdcall LdrGetProcedureAddress(ptr ptr long ptr) ChpeLdrGetProcedureAddress
130 stdcall LdrLoadDll(wstr long ptr ptr) ChpeLdrLoadDll
139 stdcall LdrQueryProcessModuleInformation(ptr long ptr) ChpeLdrQueryProcessModuleInformation
142 stdcall -version=0x602+ LdrResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) ChpeLdrResolveDelayLoadedAPI
143 stdcall -version=0x602+ LdrResolveDelayLoadsFromDll(ptr str long) ChpeLdrResolveDelayLoadsFromDll
156 stdcall LdrUnloadDll(ptr) ChpeLdrUnloadDll
186 stdcall NtAdjustPrivilegesToken(long long ptr long ptr ptr) ChpeNtAdjustPrivilegesToken
195 stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ChpeNtAllocateVirtualMemory
196 stdcall NtAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeNtAllocateVirtualMemoryEx
190 stdcall NtAlertThreadByThreadId(long) ChpeNtAlertThreadByThreadId
230 stdcall NtClose(long) ChpeNtClose
241 stdcall NtContinue(ptr long) ChpeNtContinue
242 stdcall -version=0xA00+ NtContinueEx(ptr ptr) ChpeNtContinueEx
249 stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ChpeNtCreateFile
261 stdcall NtCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeNtCreateNamedPipeFile
296 stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeNtDeviceIoControlFile
314 stdcall -version=0x600+ NtFlushProcessWriteBuffers() ChpeNtFlushProcessWriteBuffers
312 stdcall NtFlushInstructionCache(long ptr long) ChpeNtFlushInstructionCache
318 stdcall NtFreeVirtualMemory(long ptr ptr long) ChpeNtFreeVirtualMemory
322 stdcall NtGetContextThread(long ptr) ChpeNtGetContextThread
328 stdcall -version=0x600+ NtGetNextThread(ptr ptr long long long ptr) ChpeNtGetNextThread
357 stdcall NtMapViewOfSection(long long ptr long ptr ptr ptr long long long) ChpeNtMapViewOfSection
358 stdcall NtMapViewOfSectionEx(long long ptr ptr ptr long long ptr long) ChpeNtMapViewOfSectionEx
363 stdcall NtNotifyChangeKey(ptr ptr ptr ptr ptr long long ptr long long) ChpeNtNotifyChangeKey
364 stdcall NtNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeNtNotifyChangeMultipleKeys
369 stdcall NtOpenFile(ptr long ptr ptr long long) ChpeNtOpenFile
404 stdcall NtProtectVirtualMemory(long ptr ptr long ptr) ChpeNtProtectVirtualMemory
412 stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeNtQueryDirectoryFile
422 stdcall NtQueryInformationFile(long ptr ptr long long) ChpeNtQueryInformationFile
435 stdcall NtQueryKey(long long ptr long ptr) ChpeNtQueryKey
256 stdcall NtCreateKey(ptr long ptr long ptr long ptr) ChpeNtCreateKey
427 stdcall NtQueryInformationThread(long long ptr long ptr) ChpeNtQueryInformationThread
425 stdcall NtQueryInformationProcess(ptr long ptr long ptr) ChpeNtQueryInformationProcess
876 stdcall RtlFormatCurrentUserKeyPath(ptr) ChpeRtlFormatCurrentUserKeyPath
887 stdcall RtlFreeUnicodeString(ptr) ChpeRtlFreeUnicodeString
372 stdcall NtOpenKey(ptr long ptr) ChpeNtOpenKey
826 stdcall NtOpenKeyEx(ptr long ptr long) ChpeNtOpenKeyEx
456 stdcall NtQueryValueKey(long ptr long ptr long ptr) ChpeNtQueryValueKey
553 stdcall NtSetValueKey(long ptr long long ptr long) ChpeNtSetValueKey
290 stdcall NtDeleteKey(long) ChpeNtDeleteKey
293 stdcall NtDeleteValueKey(long ptr) ChpeNtDeleteValueKey
302 stdcall NtEnumerateKey(long long long ptr long ptr) ChpeNtEnumerateKey
305 stdcall NtEnumerateValueKey(long long long ptr long ptr) ChpeNtEnumerateValueKey
313 stdcall NtFlushKey(long) ChpeNtFlushKey
251 stdcall NtCreateWaitCompletionPacket(ptr long ptr) ChpeNtCreateWaitCompletionPacket
252 stdcall NtAssociateWaitCompletionPacket(long long long ptr ptr long long ptr) ChpeNtAssociateWaitCompletionPacket
253 stdcall NtCancelWaitCompletionPacket(long long) ChpeNtCancelWaitCompletionPacket
458 stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ChpeNtQueryVolumeInformationFile
439 stdcall NtQueryObject(long long long long long) ChpeNtQueryObject
451 stdcall NtQuerySystemInformation(long ptr long ptr) ChpeNtQuerySystemInformation
921 stdcall RtlGetNativeSystemInformation(long ptr long ptr) ChpeRtlGetNativeSystemInformation
457 stdcall NtQueryVirtualMemory(long ptr long ptr long ptr) ChpeNtQueryVirtualMemory
464 stdcall NtRaiseException(ptr ptr long) ChpeNtRaiseException
466 stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeNtReadFile
470 stdcall NtReadVirtualMemory(long ptr ptr long ptr) ChpeNtReadVirtualMemory
511 stdcall NtSetContextThread(long ptr) ChpeNtSetContextThread
562 stdcall NtSuspendProcess(ptr) ChpeNtSuspendProcess
563 stdcall NtSuspendThread(ptr ptr) ChpeNtSuspendThread
500 stdcall NtResumeThread(ptr ptr) ChpeNtResumeThread
566 stdcall NtTerminateProcess(long long) ChpeNtTerminateProcess
567 stdcall NtTerminateThread(long long) ChpeNtTerminateThread
580 stdcall NtUnmapViewOfSection(long ptr) ChpeNtUnmapViewOfSection
581 stdcall NtUnmapViewOfSectionEx(long ptr long) ChpeNtUnmapViewOfSectionEx
587 stdcall NtWaitForAlertByThreadId(ptr ptr) ChpeNtWaitForAlertByThreadId
595 stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeNtWriteFile
598 stdcall NtWriteVirtualMemory(long ptr ptr long ptr) ChpeNtWriteVirtualMemory
633 stdcall RtlAddFunctionTable(ptr long long) ChpeRtlAddFunctionTable
634 stdcall RtlAddGrowableFunctionTable(ptr ptr long long long long) ChpeRtlAddGrowableFunctionTable
639 stdcall RtlAddVectoredContinueHandler(long ptr) ChpeRtlAddVectoredContinueHandler
640 stdcall RtlAddVectoredExceptionHandler(long ptr) ChpeRtlAddVectoredExceptionHandler
646 stdcall RtlAllocateHeap(ptr long ptr) ChpeRtlAllocateHeap
898 stdcall RtlGetCurrentPeb() ChpeRtlGetCurrentPeb
928 stdcall RtlGetProcessHeaps(long ptr) ChpeRtlGetProcessHeaps
1108 stdcall RtlQueryEnvironmentVariable(ptr ptr long ptr long ptr) ChpeRtlQueryEnvironmentVariable
611 stdcall RtlAcquirePrivilege(ptr long long ptr) ChpeRtlAcquirePrivilege
614 stdcall RtlAcquireSRWLockExclusive(ptr) ChpeRtlAcquireSRWLockExclusive
615 stdcall RtlAcquireSRWLockShared(ptr) ChpeRtlAcquireSRWLockShared
668 stdcall RtlCaptureContext(ptr) ChpeRtlCaptureContextX64
669 stdcall RtlCaptureStackBackTrace(long long ptr ptr) ChpeRtlCaptureStackBackTrace
684 stdcall RtlCompareMemory(ptr ptr long) ChpeRtlCompareMemory
687 stdcall RtlCompareUnicodeString(ptr ptr long) ChpeRtlCompareUnicodeString
714 stdcall RtlCopyUnicodeString(ptr ptr) ChpeRtlCopyUnicodeString
753 stdcall RtlDecodePointer(ptr) ChpeRtlDecodePointer
754 stdcall RtlDecodeSystemPointer(ptr) ChpeRtlDecodeSystemPointer
764 stdcall RtlDeleteCriticalSection(ptr) ChpeRtlDeleteCriticalSection
767 cdecl RtlDeleteFunctionTable(ptr) ChpeRtlDeleteFunctionTable
768 stdcall RtlDeleteGrowableFunctionTable(ptr) ChpeRtlDeleteGrowableFunctionTable
808 stdcall RtlEnterCriticalSection(ptr) ChpeRtlEnterCriticalSection
806 stdcall RtlEncodePointer(ptr) ChpeRtlEncodePointer
807 stdcall RtlEncodeSystemPointer(ptr) ChpeRtlEncodeSystemPointer
843 stdcall RtlExitUserThread(long) ChpeRtlExitUserThread
849 stdcall RtlFillMemory(ptr long long) ChpeRtlFillMemory
871 stdcall RtlFlsAlloc(ptr ptr) ChpeRtlFlsAlloc
872 stdcall RtlFlsFree(long) ChpeRtlFlsFree
873 stdcall RtlFlsGetValue(long ptr) ChpeRtlFlsGetValue
874 stdcall RtlFlsSetValue(long ptr) ChpeRtlFlsSetValue
882 stdcall RtlFreeHeap(long long long) ChpeRtlFreeHeap
912 stdcall RtlGetFunctionTableListHead() ChpeRtlGetFunctionTableListHead
915 stdcall RtlGetLastNtStatus() ChpeRtlGetLastNtStatus
916 stdcall RtlGetLastWin32Error() ChpeRtlGetLastWin32Error
899 stdcall RtlGetCurrentProcessorNumber() ChpeRtlGetCurrentProcessorNumber
900 stdcall -version=0x601+ RtlGetCurrentProcessorNumberEx(ptr) ChpeRtlGetCurrentProcessorNumberEx
929 stdcall -version=0x600+ RtlGetProductInfo(long long long long ptr) ChpeRtlGetProductInfo
942 stdcall RtlGetVersion(ptr) ChpeRtlGetVersion
993 stdcall RtlGrowFunctionTable(ptr long) ChpeRtlGrowFunctionTable
966 stdcall RtlInitUnicodeString(ptr wstr) ChpeRtlInitUnicodeString
971 stdcall -version=0x600+ RtlInitializeConditionVariable(ptr) ChpeRtlInitializeConditionVariable
973 stdcall RtlInitializeCriticalSection(ptr) ChpeRtlInitializeCriticalSection
984 stdcall RtlInitializeSListHead(ptr) ChpeRtlInitializeSListHead
985 stdcall -version=0x600+ RtlInitializeSRWLock(ptr) ChpeRtlInitializeSRWLock
992 cdecl RtlInstallFunctionTableCallback(double double long ptr ptr ptr) ChpeRtlInstallFunctionTableCallback
997 stdcall RtlInterlockedFlushSList(ptr) ChpeRtlInterlockedFlushSList
998 stdcall RtlInterlockedPopEntrySList(ptr) ChpeRtlInterlockedPopEntrySList
999 stdcall RtlInterlockedPushEntrySList(ptr ptr) ChpeRtlInterlockedPushEntrySList
1000 stdcall RtlInterlockedPushListSList(ptr ptr ptr long) ChpeRtlInterlockedPushListSList
1001 stdcall -version=0x602+ RtlInterlockedPushListSListEx(ptr ptr ptr long) ChpeRtlInterlockedPushListSListEx
1030 stdcall RtlIsProcessorFeaturePresent(long) ChpeRtlIsProcessorFeaturePresent
1039 stdcall RtlLeaveCriticalSection(ptr) ChpeRtlLeaveCriticalSection
1061 stdcall RtlLookupFunctionEntry(long ptr ptr) ChpeRtlLookupFunctionEntry
1062 stdcall RtlLookupFunctionTable(int64 ptr ptr) ChpeRtlLookupFunctionTable
1052 stdcall -version=0xA00+ RtlLogUnexpectedCodepath(ptr) ChpeRtlLogUnexpectedCodepath
1066 stdcall RtlMoveMemory(ptr ptr long) ChpeRtlMoveMemory
1080 stdcall RtlNtStatusToDosError(long) ChpeRtlNtStatusToDosError
1093 stdcall RtlPcToFileHeader(ptr ptr) ChpeRtlPcToFileHeader
1105 stdcall RtlQueryDepthSList(ptr) ChpeRtlQueryDepthSList
1127 stdcall -norelay RtlRaiseException(ptr) ChpeRtlRaiseException
1128 stdcall RtlRaiseStatus(long) ChpeRtlRaiseStatus
1129 stdcall RtlRandom(ptr) ChpeRtlRandom
1131 stdcall RtlReAllocateHeap(long long ptr long) ChpeRtlReAllocateHeap
1143 stdcall RtlReleasePrivilege(ptr) ChpeRtlReleasePrivilege
1146 stdcall RtlReleaseSRWLockExclusive(ptr) ChpeRtlReleaseSRWLockExclusive
1147 stdcall RtlReleaseSRWLockShared(ptr) ChpeRtlReleaseSRWLockShared
1150 stdcall RtlRemoveVectoredContinueHandler(ptr) ChpeRtlRemoveVectoredContinueHandler
1151 stdcall RtlRemoveVectoredExceptionHandler(ptr) ChpeRtlRemoveVectoredExceptionHandler
1156 stdcall RtlRestoreContext(ptr ptr) ChpeRtlRestoreContext
1157 stdcall RtlRestoreLastWin32Error(long) ChpeRtlRestoreLastWin32Error
1164 stdcall -version=0x600+ RtlRunOnceExecuteOnce(ptr ptr ptr ptr) ChpeRtlRunOnceExecuteOnce
1165 stdcall -version=0x600+ RtlRunOnceInitialize(ptr) ChpeRtlRunOnceInitialize
1215 stdcall RtlSizeHeap(long long ptr) ChpeRtlSizeHeap
1177 stdcall RtlSetCriticalSectionSpinCount(ptr long) ChpeRtlSetCriticalSectionSpinCount
1190 stdcall RtlSetLastWin32Error(long) ChpeRtlSetLastWin32Error
1242 stdcall RtlTryAcquireSRWLockExclusive(ptr) ChpeRtlTryAcquireSRWLockExclusive
1243 stdcall RtlTryAcquireSRWLockShared(ptr) ChpeRtlTryAcquireSRWLockShared
1244 stdcall RtlTryEnterCriticalSection(ptr) ChpeRtlTryEnterCriticalSection
1248 stdcall -version=0x601+ RtlUTF8ToUnicodeN(ptr long ptr str long) ChpeRtlUTF8ToUnicodeN
1266 stdcall RtlUnwind(ptr ptr ptr ptr) ChpeRtlUnwind
1267 stdcall RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ChpeRtlUnwindEx
1291 stdcall RtlVirtualUnwind(long int64 int64 ptr ptr ptr ptr ptr) ChpeRtlVirtualUnwind
1292 stdcall -version=0x602+ RtlWaitOnAddress(ptr ptr long ptr) ChpeRtlWaitOnAddress
1294 stdcall -version=0x602+ RtlWakeAddressAll(ptr) ChpeRtlWakeAddressAll
1295 stdcall -version=0x602+ RtlWakeAddressSingle(ptr) ChpeRtlWakeAddressSingle
1293 stdcall RtlWakeAllConditionVariable(ptr) ChpeRtlWakeAllConditionVariable
1296 stdcall RtlWakeConditionVariable(ptr) ChpeRtlWakeConditionVariable
1319 stdcall RtlZeroMemory(ptr long) ChpeRtlZeroMemory
1373 stdcall -version=0x600+ TpCallbackLeaveCriticalSectionOnCompletion(ptr ptr) ChpeTpCallbackLeaveCriticalSectionOnCompletion
1375 stdcall -version=0x600+ TpCallbackReleaseMutexOnCompletion(ptr ptr) ChpeTpCallbackReleaseMutexOnCompletion
1376 stdcall -version=0x600+ TpCallbackReleaseSemaphoreOnCompletion(ptr ptr long) ChpeTpCallbackReleaseSemaphoreOnCompletion
1379 stdcall -version=0x600+ TpCallbackSetEventOnCompletion(ptr ptr) ChpeTpCallbackSetEventOnCompletion
1380 stdcall -version=0x600+ TpCallbackUnloadDllOnCompletion(ptr ptr) ChpeTpCallbackUnloadDllOnCompletion
1381 stdcall -version=0x600+ TpCancelAsyncIoOperation(ptr) ChpeTpCancelAsyncIoOperation
1386 stdcall -version=0x600+ TpDisassociateCallback(ptr) ChpeTpDisassociateCallback
1387 stdcall -version=0x600+ TpIsTimerSet(ptr) ChpeTpIsTimerSet
1388 stdcall -version=0x600+ TpPostWork(ptr) ChpeTpPostWork
1391 stdcall -version=0x600+ TpReleaseCleanupGroup(ptr) ChpeTpReleaseCleanupGroup
1392 stdcall -version=0x600+ TpReleaseCleanupGroupMembers(ptr long ptr) ChpeTpReleaseCleanupGroupMembers
1393 stdcall -version=0x600+ TpReleaseIoCompletion(ptr) ChpeTpReleaseIoCompletion
1394 stdcall -version=0x600+ TpReleasePool(ptr) ChpeTpReleasePool
1395 stdcall TpReleaseTimer(ptr) ChpeTpReleaseTimer
1396 stdcall TpReleaseWait(ptr) ChpeTpReleaseWait
1397 stdcall -version=0x600+ TpReleaseWork(ptr) ChpeTpReleaseWork
1398 stdcall -version=0x600+ TpSetPoolMaxThreads(ptr long) ChpeTpSetPoolMaxThreads
1399 stdcall -version=0x600+ TpSetPoolMinThreads(ptr long) ChpeTpSetPoolMinThreads
1401 stdcall TpSetTimer(ptr ptr long long) ChpeTpSetTimer
1402 stdcall -version=0x602+ TpSetTimerEx(ptr ptr long long) ChpeTpSetTimerEx
1403 stdcall TpSetWait(ptr long ptr) ChpeTpSetWait
1404 stdcall -version=0x602+ TpSetWaitEx(ptr long ptr ptr) ChpeTpSetWaitEx
1406 stdcall -version=0x600+ TpStartAsyncIoOperation(ptr) ChpeTpStartAsyncIoOperation
1408 stdcall -version=0x600+ TpWaitForIoCompletion(ptr long) ChpeTpWaitForIoCompletion
1409 stdcall TpWaitForTimer(ptr long) ChpeTpWaitForTimer
1410 stdcall -version=0x600+ TpWaitForWait(ptr long) ChpeTpWaitForWait
1411 stdcall -version=0x600+ TpWaitForWork(ptr long) ChpeTpWaitForWork
1412 stdcall -ret64 VerSetConditionMask(double long long) ChpeVerSetConditionMask
1859 varargs _snprintf(ptr long str) ChpeSnprintf
1860 varargs _snwprintf(ptr long wstr) ChpeSnwprintf
1867 varargs _swprintf(ptr wstr) ChpeSwprintf
1922 varargs sprintf(ptr str) ChpeSprintf
1943 varargs swprintf(ptr wstr) ChpeSwprintf
1839 cdecl __chkstk() ChpeChkStk
1852 cdecl _local_unwind(ptr ptr) ChpeLocalUnwind
1916 cdecl memcpy(ptr ptr long) ChpeMemcpy
1971 stdcall ChpeEmulationDispatch(ptr)
1309 stdcall RtlWow64GetThreadSelectorEntry(ptr ptr long ptr) ChpeRtlWow64GetThreadSelectorEntry

273 stdcall NtCreateThread(ptr long ptr long ptr ptr ptr long) ChpeNtCreateThread
321 stdcall NtFsControlFile(long long ptr ptr ptr long ptr long ptr long) ChpeNtFsControlFile
550 stdcall NtSetTimer(long ptr ptr ptr long long ptr) ChpeNtSetTimer
744 stdcall RtlCreateUserThread(long ptr long long ptr ptr ptr ptr ptr) ChpeRtlCreateUserThread
756 stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ChpeRtlDecompressFragment
890 stdcall RtlGenerate8dot3Name(ptr ptr long ptr) ChpeRtlGenerate8dot3Name
918 stdcall RtlGetLengthWithoutLastFullDosOrNtPathElement(long ptr ptr) ChpeRtlGetLengthWithoutLastFullDosOrNtPathElement
938 stdcall RtlGetUnloadEventTrace() ChpeRtlGetUnloadEventTrace
1126 stdcall RtlQueueWorkItem(ptr ptr long) ChpeRtlQueueWorkItem
1139 stdcall RtlRegisterWait(ptr ptr ptr ptr long long) ChpeRtlRegisterWait
1321 stdcall RtlpApplyLengthFunction(long long ptr ptr) ChpeRtlpApplyLengthFunction
1949 stdcall vDbgPrintEx(long long str ptr) ChpevDbgPrintEx
1950 stdcall vDbgPrintExWithPrefix(str long long str ptr) ChpevDbgPrintExWithPrefix
825 stdcall LdrGetDllFullName(ptr ptr) ChpeLdrGetDllFullName
860 stdcall RtlFindExportedRoutineByName(ptr str) ChpeRtlFindExportedRoutineByName
828 stdcall RtlIsEcCode(ptr) ChpeRtlIsEcCode
836 stdcall RtlLocateExtendedFeature(ptr long ptr) ChpeRtlLocateExtendedFeature
837 stdcall RtlLocateExtendedFeature2(ptr long ptr ptr) ChpeRtlLocateExtendedFeature2
838 stdcall RtlQueryPerformanceCounter(ptr) ChpeRtlQueryPerformanceCounter
839 stdcall RtlQueryPerformanceFrequency(ptr) ChpeRtlQueryPerformanceFrequency
840 stdcall RtlQuerySystemTime(ptr) ChpeRtlQuerySystemTime
841 stdcall RtlSystemTimeToTimeFields(ptr ptr) ChpeRtlSystemTimeToTimeFields
40 varargs DbgPrintReturnControlC(str) ChpeDbgPrintReturnControlC
54 stdcall DbgUserBreakPoint() ChpeDbgUserBreakPoint
86 varargs EtwTraceMessage(int64 long ptr long) ChpeEtwTraceMessage
1282 stdcall RtlUserThreadStart(long long) ChpeRtlUserThreadStart
1924 varargs sscanf(str str) ChpeSscanf
1968 cdecl ChpeVsscanf(str str ptr) ChpeAutoVsscanf
1969 stdcall ChpeVDbgPrintReturnControlC(str ptr) ChpeAutoVDbgPrintReturnControlC
1851 cdecl _lfind(ptr ptr ptr long ptr) ChpeLfind
1888 cdecl bsearch(ptr ptr long long ptr) ChpeBsearch
1920 cdecl qsort(ptr long long ptr) ChpeQsort
1857 cdecl _setjmp(ptr ptr) ChpeSetJmpX64
1858 cdecl _setjmpex(ptr ptr) ChpeSetJmpX64
95 stdcall ExpInterlockedPopEntrySListEnd() ChpeUnsupportedKernelEntry
97 stdcall ExpInterlockedPopEntrySListFault() ChpeUnsupportedKernelEntry
99 stdcall ExpInterlockedPopEntrySListResume() ChpeUnsupportedKernelEntry
102 stdcall KiUserApcDispatcher(ptr ptr ptr ptr) ChpeUnsupportedApcEntry
106 stdcall KiUserEmulationDispatcher(ptr) ChpeUnsupportedEmulationEntry

# BEGIN typed bridge wrappers (generate_chpe_bridge.py)
1 stdcall -version=0x600+ RtlFlushHeaps() ChpeAutoRtlFlushHeaps
2 stdcall -version=0x602+ RtlQueryWnfStateData(ptr int64 ptr ptr ptr long) ChpeAutoRtlQueryWnfStateData
3 stdcall -version=0x602+ RtlSubscribeWnfStateChangeNotification(ptr int64 long ptr ptr ptr long long long) ChpeAutoRtlSubscribeWnfStateChangeNotification
4 stdcall -version=0x602+ RtlUnsubscribeWnfNotificationWaitForCompletion(ptr) ChpeAutoRtlUnsubscribeWnfNotificationWaitForCompletion
5 stdcall -version=0x600+ A_SHAFinal(ptr ptr) ChpeAutoA_SHAFinal
6 stdcall -version=0x600+ A_SHAInit(ptr) ChpeAutoA_SHAInit
7 stdcall -version=0x600+ A_SHAUpdate(ptr ptr long) ChpeAutoA_SHAUpdate
8 stdcall -version=0x600+ AlpcAdjustCompletionListConcurrencyCount(ptr long) ChpeAutoAlpcAdjustCompletionListConcurrencyCount
9 stdcall -version=0x600+ AlpcFreeCompletionListMessage(ptr ptr) ChpeAutoAlpcFreeCompletionListMessage
10 stdcall -version=0x600+ AlpcGetCompletionListLastMessageInformation(ptr ptr ptr) ChpeAutoAlpcGetCompletionListLastMessageInformation
11 stdcall -version=0x600+ AlpcGetCompletionListMessageAttributes(ptr ptr) ChpeAutoAlpcGetCompletionListMessageAttributes
12 stdcall -version=0x600+ AlpcGetHeaderSize(long) ChpeAutoAlpcGetHeaderSize
13 stdcall -version=0x600+ AlpcGetMessageAttribute(ptr long) ChpeAutoAlpcGetMessageAttribute
14 stdcall -version=0x600+ AlpcGetMessageFromCompletionList(ptr ptr) ChpeAutoAlpcGetMessageFromCompletionList
15 stdcall -version=0x600+ AlpcGetOutstandingCompletionListMessageCount(ptr) ChpeAutoAlpcGetOutstandingCompletionListMessageCount
16 stdcall -version=0x600+ AlpcInitializeMessageAttribute(long ptr long ptr) ChpeAutoAlpcInitializeMessageAttribute
17 stdcall -version=0x600+ AlpcMaxAllowedMessageLength() ChpeAutoAlpcMaxAllowedMessageLength
18 stdcall -version=0x600+ AlpcRegisterCompletionList(ptr ptr long long long) ChpeAutoAlpcRegisterCompletionList
19 stdcall -version=0x600+ AlpcRegisterCompletionListWorkerThread(ptr) ChpeAutoAlpcRegisterCompletionListWorkerThread
20 stdcall -version=0x600+ AlpcRundownCompletionList(ptr) ChpeAutoAlpcRundownCompletionList
21 stdcall -version=0x600+ AlpcUnregisterCompletionList(ptr) ChpeAutoAlpcUnregisterCompletionList
22 stdcall -version=0x600+ AlpcUnregisterCompletionListWorkerThread(ptr) ChpeAutoAlpcUnregisterCompletionListWorkerThread
23 stdcall CsrAllocateCaptureBuffer(long long) ChpeAutoCsrAllocateCaptureBuffer
24 stdcall CsrAllocateMessagePointer(ptr long ptr) ChpeAutoCsrAllocateMessagePointer
25 stdcall CsrCaptureMessageBuffer(ptr ptr long ptr) ChpeAutoCsrCaptureMessageBuffer
26 stdcall CsrCaptureMessageMultiUnicodeStringsInPlace(ptr long ptr) ChpeAutoCsrCaptureMessageMultiUnicodeStringsInPlace
27 stdcall CsrCaptureMessageString(ptr str long long ptr) ChpeAutoCsrCaptureMessageString
28 stdcall CsrCaptureTimeout(long ptr) ChpeAutoCsrCaptureTimeout
29 stdcall CsrClientCallServer(ptr ptr long long) ChpeAutoCsrClientCallServer
30 stdcall CsrClientConnectToServer(str long ptr ptr ptr) ChpeAutoCsrClientConnectToServer
31 stdcall CsrFreeCaptureBuffer(ptr) ChpeAutoCsrFreeCaptureBuffer
32 stdcall CsrGetProcessId() ChpeAutoCsrGetProcessId
33 stdcall CsrIdentifyAlertableThread() ChpeAutoCsrIdentifyAlertableThread
34 stdcall -version=0x502+ CsrNewThread() ChpeAutoCsrNewThread
35 stdcall CsrSetPriorityClass(ptr ptr) ChpeAutoCsrSetPriorityClass
36 stdcall -version=0x600+ CsrVerifyRegion(ptr long) ChpeStubCsrVerifyRegion
41 stdcall DbgPrompt(ptr ptr long) ChpeAutoDbgPrompt
42 stdcall DbgQueryDebugFilterState(long long) ChpeAutoDbgQueryDebugFilterState
43 stdcall DbgSetDebugFilterState(long long long) ChpeAutoDbgSetDebugFilterState
44 stdcall DbgUiConnectToDbg() ChpeAutoDbgUiConnectToDbg
45 stdcall DbgUiContinue(ptr long) ChpeAutoDbgUiContinue
46 stdcall DbgUiConvertStateChangeStructure(ptr ptr) ChpeAutoDbgUiConvertStateChangeStructure
47 stdcall DbgUiDebugActiveProcess(ptr) ChpeAutoDbgUiDebugActiveProcess
48 stdcall DbgUiGetThreadDebugObject() ChpeAutoDbgUiGetThreadDebugObject
49 stdcall DbgUiIssueRemoteBreakin(ptr) ChpeAutoDbgUiIssueRemoteBreakin
50 stdcall DbgUiRemoteBreakin() ChpeAutoDbgUiRemoteBreakin
51 stdcall DbgUiSetThreadDebugObject(ptr) ChpeAutoDbgUiSetThreadDebugObject
52 stdcall DbgUiStopDebugging(ptr) ChpeAutoDbgUiStopDebugging
53 stdcall DbgUiWaitStateChange(ptr ptr) ChpeAutoDbgUiWaitStateChange
55 stdcall  EtwCreateTraceInstanceId(ptr ptr) ChpeStubEtwCreateTraceInstanceId
56 stdcall -version=0x600+ EtwDeliverDataBlock(long) ChpeStubEtwDeliverDataBlock
57 stdcall -version=0x600+ EtwEnumerateProcessRegGuids(ptr long ptr) ChpeStubEtwEnumerateProcessRegGuids
59 stdcall -version=0x600+ EtwEventEnabled(int64 ptr) ChpeAutoEtwEventEnabled
60 stdcall -version=0x600+ EtwEventProviderEnabled(int64 long int64) ChpeAutoEtwEventProviderEnabled
62 stdcall -version=0x600+ EtwEventSetInformation(int64 long ptr long) ChpeAutoEtwEventSetInformation
65 stdcall -version=0x601+ EtwEventWriteEx(int64 ptr int64 long ptr ptr long ptr) ChpeAutoEtwEventWriteEx
66 stdcall -version=0x600+ EtwEventWriteEndScenario(long ptr long long) ChpeStubEtwEventWriteEndScenario
67 stdcall -version=0x600+ EtwEventWriteFull(long long long long long long long) ChpeStubEtwEventWriteFull
68 stdcall -version=0x600+ EtwEventWriteStartScenario(long ptr long long) ChpeStubEtwEventWriteStartScenario
69 stdcall -version=0x600+ EtwEventWriteString(int64 long int64 wstr) ChpeAutoEtwEventWriteString
71 stdcall EtwGetTraceEnableFlags(double) ChpeAutoEtwGetTraceEnableFlags
72 stdcall EtwGetTraceEnableLevel(double) ChpeAutoEtwGetTraceEnableLevel
73 stdcall EtwGetTraceLoggerHandle(ptr) ChpeAutoEtwGetTraceLoggerHandle
74 stdcall -version=0x600+ EtwLogTraceEvent(long long) ChpeStubEtwLogTraceEvent
75 stdcall -version=0x600+ EtwNotificationRegister(ptr long long long ptr) ChpeStubEtwNotificationRegister
76 stdcall -version=0x600+ EtwNotificationUnregister(long ptr) ChpeStubEtwNotificationUnregister
77 stdcall -version=0x600+ EtwProcessPrivateLoggerRequest(ptr) ChpeStubEtwProcessPrivateLoggerRequest
78 stdcall -version=0x600+ EtwRegister(ptr ptr ptr ptr) ChpeStubEtwRegister
79 stdcall -version=0x600+ EtwRegisterSecurityProvider() ChpeStubEtwRegisterSecurityProvider
80 stdcall EtwRegisterTraceGuidsA(ptr ptr ptr long ptr str str ptr) ChpeAutoEtwRegisterTraceGuidsA
81 stdcall EtwRegisterTraceGuidsW(ptr ptr ptr long ptr wstr wstr ptr) ChpeAutoEtwRegisterTraceGuidsW
82 stdcall -version=0x600+ EtwReplyNotification(long) ChpeStubEtwReplyNotification
83 stdcall -version=0x600+ EtwSendNotification(long long ptr long long) ChpeStubEtwSendNotification
84 stdcall -version=0x600+ EtwSetMark(long long long) ChpeStubEtwSetMark
85 stdcall  EtwTraceEventInstance(double ptr ptr ptr) ChpeStubEtwTraceEventInstance
87 stdcall  EtwTraceMessageVa(int64 long ptr long ptr) ChpeStubEtwTraceMessageVa
88 stdcall -version=0x600+ EtwUnregister(int64) ChpeStubEtwUnregister
89 stdcall EtwUnregisterTraceGuids(double) ChpeAutoEtwUnregisterTraceGuids
90 stdcall -version=0x600+ EtwWrite(int64 ptr ptr long ptr) ChpeStubEtwWrite
91 stdcall -version=0x600+ EtwWriteUMSecurityEvent(ptr long long long) ChpeStubEtwWriteUMSecurityEvent
92 stdcall -version=0x600+ EtwpCreateEtwThread(long long) ChpeStubEtwpCreateEtwThread
93 stdcall -version=0x600+ EtwpGetCpuSpeed(ptr) ChpeStubEtwpGetCpuSpeed
94 stdcall -version=0x600+ EtwpNotificationThread() ChpeStubEtwpNotificationThread
96 stub -version=0x600+ ExpInterlockedPopEntrySListEnd8
98 stub -version=0x600+ ExpInterlockedPopEntrySListFault8
100 stub -version=0x600+ ExpInterlockedPopEntrySListResume8
101 stdcall KiRaiseUserExceptionDispatcher() ChpeAutoKiRaiseUserExceptionDispatcher
103 stdcall KiUserCallbackDispatcher(ptr ptr long) ChpeAutoKiUserCallbackDispatcher
105 stdcall KiUserExceptionDispatcherWorker(ptr ptr) ChpeAutoKiUserExceptionDispatcherWorker
108 stdcall -version=0x600+ LdrAddLoadAsDataTable(ptr wstr long ptr) ChpeStubLdrAddLoadAsDataTable
110 stdcall LdrDisableThreadCalloutsForDll(ptr) ChpeAutoLdrDisableThreadCalloutsForDll
112 stdcall LdrEnumerateLoadedModules(long ptr ptr) ChpeAutoLdrEnumerateLoadedModules
115 stdcall  LdrFindResourceEx_U(ptr ptr ptr ptr ptr) ChpeStubLdrFindResourceEx_U
117 stdcall LdrFlushAlternateResourceModules() ChpeAutoLdrFlushAlternateResourceModules
120 stdcall -version=0x600+ LdrGetFailureData() ChpeStubLdrGetFailureData
121 stdcall -version=0x600+ LdrGetFileNameFromLoadAsDataTable(ptr ptr) ChpeStubLdrGetFileNameFromLoadAsDataTable
122 stdcall -version=0x600+ LdrGetKnownDllSectionHandle(wstr long ptr) ChpeStubLdrGetKnownDllSectionHandle
124 stdcall -version=0x600+ LdrGetProcedureAddressEx(ptr ptr long ptr long) ChpeAutoLdrGetProcedureAddressEx
125 stdcall  LdrHotPatchRoutine(ptr) ChpeStubLdrHotPatchRoutine
126 stdcall LdrInitShimEngineDynamic(ptr) ChpeAutoLdrInitShimEngineDynamic
127 stdcall LdrInitializeThunk(long long long long) ChpeAutoLdrInitializeThunk
128 stdcall LdrLoadAlternateResourceModule(ptr ptr) ChpeAutoLdrLoadAlternateResourceModule
129 stdcall -version=0x600+ LdrLoadAlternateResourceModuleEx(long long ptr ptr long) ChpeStubLdrLoadAlternateResourceModuleEx
131 stdcall LdrLockLoaderLock(long ptr ptr) ChpeAutoLdrLockLoaderLock
132 stdcall LdrOpenImageFileOptionsKey(ptr long ptr) ChpeAutoLdrOpenImageFileOptionsKey
133 stdcall -version=0x600+ LdrProcessInitializationComplete() ChpeStubLdrProcessInitializationComplete
134 stdcall LdrProcessRelocationBlock(ptr long ptr long) ChpeAutoLdrProcessRelocationBlock
135 stdcall LdrQueryImageFileExecutionOptions(ptr str long ptr long ptr) ChpeAutoLdrQueryImageFileExecutionOptions
136 stdcall LdrQueryImageFileExecutionOptionsEx(ptr ptr long ptr long ptr long) ChpeAutoLdrQueryImageFileExecutionOptionsEx
137 stdcall LdrQueryImageFileKeyOption(ptr ptr long ptr long ptr) ChpeAutoLdrQueryImageFileKeyOption
138 stdcall -version=0x600+ LdrQueryModuleServiceTags(ptr ptr ptr) ChpeStubLdrQueryModuleServiceTags
140 stdcall -version=0x600+ LdrRegisterDllNotification(long ptr ptr ptr) ChpeAutoLdrRegisterDllNotification
141 stdcall -version=0x600+ LdrRemoveLoadAsDataTable(ptr ptr ptr long) ChpeStubLdrRemoveLoadAsDataTable
144 stdcall -version=0x600+ LdrResFindResource(ptr long long long ptr ptr ptr ptr long) ChpeAutoLdrResFindResource
145 stdcall -version=0x600+ LdrResFindResourceDirectory(ptr long long ptr ptr ptr long ptr) ChpeAutoLdrResFindResourceDirectory
146 stdcall -version=0x600+ LdrResRelease(ptr ptr long long) ChpeStubLdrResRelease
147 stdcall -version=0x600+ LdrResSearchResource(wstr wstr long long long ptr long long) ChpeStubLdrResSearchResource
148 stdcall LdrSetAppCompatDllRedirectionCallback(long ptr ptr) ChpeAutoLdrSetAppCompatDllRedirectionCallback
149 stdcall LdrSetDllManifestProber(ptr) ChpeAutoLdrSetDllManifestProber
150 stdcall -version=0x600+ LdrSetMUICacheType(long) ChpeStubLdrSetMUICacheType
151 stdcall LdrShutdownProcess() ChpeAutoLdrShutdownProcess
152 stdcall LdrShutdownThread() ChpeAutoLdrShutdownThread
153 extern LdrSystemDllInitBlock ntdll.LdrSystemDllInitBlock
154 stdcall LdrUnloadAlternateResourceModule(ptr) ChpeAutoLdrUnloadAlternateResourceModule
155 stdcall -version=0x600+ LdrUnloadAlternateResourceModuleEx(long long) ChpeStubLdrUnloadAlternateResourceModuleEx
157 stdcall LdrUnlockLoaderLock(long ptr) ChpeAutoLdrUnlockLoaderLock
158 stdcall -version=0x600+ LdrUnregisterDllNotification(ptr) ChpeAutoLdrUnregisterDllNotification
159 stdcall LdrVerifyImageMatchesChecksum(ptr long long long) ChpeAutoLdrVerifyImageMatchesChecksum
160 stdcall -version=0x600+ LdrVerifyImageMatchesChecksumEx(ptr ptr) ChpeStubLdrVerifyImageMatchesChecksumEx
161 stdcall -version=0x600+ LdrpResGetMappingSize(long ptr long long) ChpeStubLdrpResGetMappingSize
162 stdcall -version=0x600+ LdrpResGetRCConfig(long long ptr long long) ChpeStubLdrpResGetRCConfig
163 stdcall -version=0x600+ LdrpResGetResourceDirectory(long long long ptr ptr) ChpeStubLdrpResGetResourceDirectory
164 stdcall -version=0x600+ MD4Final(ptr) ChpeAutoMD4Final
165 stdcall -version=0x600+ MD4Init(ptr) ChpeAutoMD4Init
166 stdcall -version=0x600+ MD4Update(ptr ptr long) ChpeAutoMD4Update
167 stdcall -version=0x600+ MD5Final(ptr) ChpeAutoMD5Final
168 stdcall -version=0x600+ MD5Init(ptr) ChpeAutoMD5Init
169 stdcall -version=0x600+ MD5Update(ptr ptr long) ChpeAutoMD5Update
170 extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
171 extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
172 extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
173 stdcall NtAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoNtAcceptConnectPort
174 stdcall NtAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoNtAccessCheck
175 stdcall NtAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckAndAuditAlarm
176 stdcall NtAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByType
177 stdcall NtAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeAndAuditAlarm
178 stdcall NtAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoNtAccessCheckByTypeResultList
179 stdcall NtAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarm
180 stdcall NtAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoNtAccessCheckByTypeResultListAndAuditAlarmByHandle
181 stdcall -version=0x600+ NtAcquireCMFViewOwnership(ptr ptr long) ChpeStubNtAcquireCMFViewOwnership
182 stdcall NtAddAtom(ptr long ptr) ChpeAutoNtAddAtom
183 stdcall NtAddBootEntry(ptr long) ChpeAutoNtAddBootEntry
184 stdcall NtAddDriverEntry(ptr long) ChpeAutoNtAddDriverEntry
185 stdcall NtAdjustGroupsToken(long long ptr long ptr ptr) ChpeAutoNtAdjustGroupsToken
187 stdcall -version=0xA00+ NtAlertMultipleThreadByThreadId(ptr long ptr ptr) ChpeAutoNtAlertMultipleThreadByThreadId
188 stdcall NtAlertResumeThread(long ptr) ChpeAutoNtAlertResumeThread
189 stdcall NtAlertThread(long) ChpeAutoNtAlertThread
191 stdcall NtAllocateLocallyUniqueId(ptr) ChpeAutoNtAllocateLocallyUniqueId
192 stdcall -version=0x600+ NtAllocateReserveObject(ptr ptr long) ChpeAutoNtAllocateReserveObject
193 stdcall NtAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoNtAllocateUserPhysicalPages
194 stdcall NtAllocateUuids(ptr ptr ptr ptr) ChpeAutoNtAllocateUuids
197 stdcall -version=0x600+ NtAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoNtAlpcAcceptConnectPort
198 stdcall -version=0x600+ NtAlpcCancelMessage(ptr long ptr) ChpeAutoNtAlpcCancelMessage
199 stdcall -version=0x600+ NtAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPort
200 stdcall -version=0x602+ NtAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcConnectPortEx
201 stdcall -version=0x600+ NtAlpcCreatePort(ptr ptr ptr) ChpeAutoNtAlpcCreatePort
202 stdcall -version=0x600+ NtAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoNtAlpcCreatePortSection
203 stdcall -version=0x600+ NtAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoNtAlpcCreateResourceReserve
204 stdcall -version=0x600+ NtAlpcCreateSectionView(ptr long ptr) ChpeAutoNtAlpcCreateSectionView
205 stdcall -version=0x600+ NtAlpcCreateSecurityContext(ptr long ptr) ChpeAutoNtAlpcCreateSecurityContext
206 stdcall -version=0x600+ NtAlpcDeletePortSection(ptr long ptr) ChpeAutoNtAlpcDeletePortSection
207 stdcall -version=0x600+ NtAlpcDeleteResourceReserve(ptr long long) ChpeAutoNtAlpcDeleteResourceReserve
208 stdcall -version=0x600+ NtAlpcDeleteSectionView(ptr long ptr) ChpeAutoNtAlpcDeleteSectionView
209 stdcall -version=0x600+ NtAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoNtAlpcDeleteSecurityContext
210 stdcall -version=0x600+ NtAlpcDisconnectPort(ptr long) ChpeAutoNtAlpcDisconnectPort
211 stdcall -version=0x600+ NtAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoNtAlpcImpersonateClientOfPort
212 stdcall -version=0xA00+ NtAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoNtAlpcImpersonateClientContainerOfPort
213 stdcall -version=0x600+ NtAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderProcess
214 stdcall -version=0x600+ NtAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoNtAlpcOpenSenderThread
215 stdcall -version=0x600+ NtAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformation
216 stdcall -version=0x600+ NtAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoNtAlpcQueryInformationMessage
217 stdcall -version=0x600+ NtAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoNtAlpcRevokeSecurityContext
218 stdcall -version=0x600+ NtAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoNtAlpcSendWaitReceivePort
219 stdcall -version=0x600+ NtAlpcSetInformation(ptr long ptr long) ChpeAutoNtAlpcSetInformation
220 stdcall NtApphelpCacheControl(long ptr) ChpeAutoNtApphelpCacheControl
221 stdcall NtAreMappedFilesTheSame(ptr ptr) ChpeAutoNtAreMappedFilesTheSame
222 stdcall NtAssignProcessToJobObject(long long) ChpeAutoNtAssignProcessToJobObject
223 stdcall NtCallbackReturn(ptr long long) ChpeAutoNtCallbackReturn
224 stdcall NtCancelDeviceWakeupRequest(ptr) ChpeAutoNtCancelDeviceWakeupRequest
225 stdcall NtCancelIoFile(long ptr) ChpeAutoNtCancelIoFile
226 stdcall -version=0x600+ NtCancelIoFileEx(long ptr ptr) ChpeAutoNtCancelIoFileEx
227 stdcall -version=0x600+ NtCancelSynchronousIoFile(long ptr ptr) ChpeAutoNtCancelSynchronousIoFile
228 stdcall NtCancelTimer(long ptr) ChpeAutoNtCancelTimer
229 stdcall NtClearEvent(long) ChpeAutoNtClearEvent
231 stdcall NtCloseObjectAuditAlarm(ptr ptr long) ChpeAutoNtCloseObjectAuditAlarm
232 stdcall -version=0x600+ NtCompareObjects(ptr ptr) ChpeAutoNtCompareObjects
233 stdcall -version=0x600+ NtCommitComplete(ptr ptr) ChpeStubNtCommitComplete
234 stdcall -version=0x600+ NtCommitEnlistment(ptr ptr) ChpeStubNtCommitEnlistment
235 stdcall -version=0x600+ NtCommitTransaction(ptr long) ChpeStubNtCommitTransaction
236 stdcall NtCompactKeys(long ptr) ChpeAutoNtCompactKeys
237 stdcall NtCompareTokens(ptr ptr ptr) ChpeAutoNtCompareTokens
238 stdcall NtCompleteConnectPort(ptr) ChpeAutoNtCompleteConnectPort
239 stdcall NtCompressKey(ptr) ChpeAutoNtCompressKey
240 stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtConnectPort
243 stdcall -version=0xA00+ NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(long ptr ptr ptr) ChpeAutoNtConvertBetweenAuxiliaryCounterAndPerformanceCounter
244 stdcall NtCreateDebugObject(ptr long ptr long) ChpeAutoNtCreateDebugObject
245 stdcall NtCreateDirectoryObject(long long long) ChpeAutoNtCreateDirectoryObject
246 stdcall -version=0x600+ NtCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeStubNtCreateEnlistment
247 stdcall NtCreateEvent(long long long long long) ChpeAutoNtCreateEvent
248 stdcall NtCreateEventPair(ptr long ptr) ChpeAutoNtCreateEventPair
250 stdcall NtCreateIoCompletion(ptr long ptr long) ChpeAutoNtCreateIoCompletion
254 stdcall NtCreateJobObject(ptr long ptr) ChpeAutoNtCreateJobObject
255 stdcall NtCreateJobSet(long ptr long) ChpeAutoNtCreateJobSet
257 stdcall -version=0x600+ NtCreateKeyTransacted(ptr long ptr long ptr long long ptr) ChpeStubNtCreateKeyTransacted
258 stdcall NtCreateKeyedEvent(ptr long ptr long) ChpeAutoNtCreateKeyedEvent
259 stdcall NtCreateMailslotFile(long long long long long long long long) ChpeAutoNtCreateMailslotFile
260 stdcall NtCreateMutant(ptr long ptr long) ChpeAutoNtCreateMutant
262 stdcall NtCreatePagingFile(ptr ptr ptr long) ChpeAutoNtCreatePagingFile
263 stdcall NtCreatePort(ptr ptr long long ptr) ChpeAutoNtCreatePort
264 stdcall -version=0x600+ NtCreatePrivateNamespace(ptr long ptr ptr) ChpeStubNtCreatePrivateNamespace
265 stdcall NtCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoNtCreateProcess
266 stdcall NtCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoNtCreateProcessEx
267 stdcall NtCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoNtCreateProfile
268 stdcall -version=0x600+ NtCreateResourceManager(ptr long ptr ptr ptr long wstr) ChpeStubNtCreateResourceManager
269 stdcall NtCreateSection(ptr long ptr ptr long long ptr) ChpeAutoNtCreateSection
270 stdcall NtCreateSectionEx(ptr long ptr ptr long long ptr ptr long) ChpeAutoNtCreateSectionEx
271 stdcall NtCreateSemaphore(ptr long ptr long long) ChpeAutoNtCreateSemaphore
272 stdcall NtCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoNtCreateSymbolicLinkObject
274 stub -version=0x600+ NtCreateThreadEx
275 stdcall NtCreateTimer(ptr long ptr long) ChpeAutoNtCreateTimer
276 stdcall NtCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtCreateToken
277 stdcall -version=0x600+ NtCreateTransaction(ptr long ptr ptr ptr long long long ptr wstr) ChpeStubNtCreateTransaction
278 stdcall -version=0x600+ NtCreateTransactionManager(ptr long ptr wstr long long) ChpeStubNtCreateTransactionManager
279 stdcall -version=0x600+ NtCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoNtCreateUserProcess
280 stdcall NtCreateWaitablePort(ptr ptr long long long) ChpeAutoNtCreateWaitablePort
281 stdcall -version=0x602+ NtCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoNtCreateWnfStateName
282 stdcall -version=0x600+ NtCreateWorkerFactory(ptr long long long long long long long long long) ChpeStubNtCreateWorkerFactory
283 stdcall NtDebugActiveProcess(ptr ptr) ChpeAutoNtDebugActiveProcess
284 stdcall NtDebugContinue(ptr ptr long) ChpeAutoNtDebugContinue
285 stdcall NtDelayExecution(long ptr) ChpeAutoNtDelayExecution
286 stdcall NtDeleteAtom(long) ChpeAutoNtDeleteAtom
287 stdcall NtDeleteBootEntry(long) ChpeAutoNtDeleteBootEntry
288 stdcall NtDeleteDriverEntry(long) ChpeAutoNtDeleteDriverEntry
289 stdcall NtDeleteFile(ptr) ChpeAutoNtDeleteFile
291 stdcall NtDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoNtDeleteObjectAuditAlarm
292 stdcall -version=0x600+ NtDeletePrivateNamespace(long) ChpeStubNtDeletePrivateNamespace
294 stdcall -version=0x602+ NtDeleteWnfStateData(ptr ptr) ChpeAutoNtDeleteWnfStateData
295 stdcall -version=0x602+ NtDeleteWnfStateName(ptr) ChpeAutoNtDeleteWnfStateName
297 stdcall NtDisplayString(ptr) ChpeAutoNtDisplayString
298 stdcall NtDuplicateObject(long long long ptr long long long) ChpeAutoNtDuplicateObject
299 stdcall NtDuplicateToken(long long long long long long) ChpeAutoNtDuplicateToken
300 stdcall NtEnumerateBootEntries(ptr ptr) ChpeAutoNtEnumerateBootEntries
301 stdcall NtEnumerateDriverEntries(ptr ptr) ChpeAutoNtEnumerateDriverEntries
303 stdcall NtEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoNtEnumerateSystemEnvironmentValuesEx
304 stub -version=0x600+ NtEnumerateTransactionObject
306 stdcall NtExtendSection(ptr ptr) ChpeAutoNtExtendSection
307 stdcall NtFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoNtFilterToken
308 stdcall NtFindAtom(ptr long ptr) ChpeAutoNtFindAtom
309 stdcall NtFlushBuffersFile(long ptr) ChpeAutoNtFlushBuffersFile
310 stdcall NtFlushBuffersFileEx(long long ptr long ptr) ChpeAutoNtFlushBuffersFileEx
311 stdcall -version=0x600+ NtFlushInstallUILanguage(long long) ChpeStubNtFlushInstallUILanguage
315 stdcall NtFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoNtFlushVirtualMemory
316 stdcall NtFlushWriteBuffer() ChpeAutoNtFlushWriteBuffer
317 stdcall NtFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoNtFreeUserPhysicalPages
319 stdcall -version=0x600+ NtFreezeRegistry(long) ChpeStubNtFreezeRegistry
320 stdcall -version=0x600+ NtFreezeTransactions(ptr ptr) ChpeStubNtFreezeTransactions
323 stdcall NtGetCurrentProcessorNumber() ChpeAutoNtGetCurrentProcessorNumber
324 stdcall -version=0xA00+ NtGetCurrentProcessorNumberEx(ptr) ChpeAutoNtGetCurrentProcessorNumberEx
325 stdcall NtGetDevicePowerState(ptr ptr) ChpeAutoNtGetDevicePowerState
326 stdcall -version=0x600+ NtGetMUIRegistryInfo(long ptr ptr) ChpeStubNtGetMUIRegistryInfo
327 stdcall -version=0x600+ NtGetNextProcess(long long long long ptr) ChpeStubNtGetNextProcess
329 stdcall -version=0x600+ NtGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoNtGetNlsSectionPtr
330 stdcall -version=0x600+ NtGetNotificationResourceManager(ptr ptr long ptr ptr long long) ChpeStubNtGetNotificationResourceManager
331 stdcall NtGetPlugPlayEvent(long long ptr long) ChpeAutoNtGetPlugPlayEvent
332 stdcall NtGetTickCount() ChpeAutoNtGetTickCount
333 stdcall NtGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoNtGetWriteWatch
334 stdcall NtImpersonateAnonymousToken(ptr) ChpeAutoNtImpersonateAnonymousToken
335 stdcall NtImpersonateClientOfPort(ptr ptr) ChpeAutoNtImpersonateClientOfPort
336 stdcall NtImpersonateThread(ptr ptr ptr) ChpeAutoNtImpersonateThread
337 stdcall -version=0x600+ NtInitializeNlsFiles(ptr ptr ptr) ChpeAutoNtInitializeNlsFiles
338 stdcall NtInitializeRegistry(long) ChpeAutoNtInitializeRegistry
339 stdcall NtInitiatePowerAction(long long long long) ChpeAutoNtInitiatePowerAction
340 stdcall NtIsProcessInJob(long long) ChpeAutoNtIsProcessInJob
341 stdcall NtIsSystemResumeAutomatic() ChpeAutoNtIsSystemResumeAutomatic
342 stdcall -version=0x600+ NtIsUILanguageComitted() ChpeStubNtIsUILanguageComitted
343 stdcall NtListenPort(ptr ptr) ChpeAutoNtListenPort
344 stdcall NtLoadDriver(ptr) ChpeAutoNtLoadDriver
345 stdcall NtLoadKey2(ptr ptr long) ChpeAutoNtLoadKey2
346 stdcall NtLoadKey(ptr ptr) ChpeAutoNtLoadKey
347 stdcall NtLoadKeyEx(ptr ptr long ptr) ChpeAutoNtLoadKeyEx
348 stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoNtLockFile
349 stdcall NtLockProductActivationKeys(ptr ptr) ChpeAutoNtLockProductActivationKeys
350 stdcall NtLockRegistryKey(ptr) ChpeAutoNtLockRegistryKey
351 stdcall NtLockVirtualMemory(long ptr ptr long) ChpeAutoNtLockVirtualMemory
352 stdcall NtMakePermanentObject(ptr) ChpeAutoNtMakePermanentObject
353 stdcall NtMakeTemporaryObject(long) ChpeAutoNtMakeTemporaryObject
354 stdcall -version=0x600+ NtMapCMFModule(long long ptr ptr ptr ptr) ChpeStubNtMapCMFModule
355 stdcall NtMapUserPhysicalPages(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPages
356 stdcall NtMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoNtMapUserPhysicalPagesScatter
359 stdcall NtModifyBootEntry(ptr) ChpeAutoNtModifyBootEntry
360 stdcall NtModifyDriverEntry(ptr) ChpeAutoNtModifyDriverEntry
361 stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoNtNotifyChangeDirectoryFile
362 stdcall NtNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoNtNotifyChangeDirectoryFileEx
365 stdcall NtOpenDirectoryObject(long long long) ChpeAutoNtOpenDirectoryObject
366 stdcall -version=0x600+ NtOpenEnlistment(ptr long ptr ptr ptr) ChpeStubNtOpenEnlistment
367 stdcall NtOpenEvent(long long long) ChpeAutoNtOpenEvent
368 stdcall NtOpenEventPair(ptr long ptr) ChpeAutoNtOpenEventPair
370 stdcall NtOpenIoCompletion(ptr long ptr) ChpeAutoNtOpenIoCompletion
371 stdcall NtOpenJobObject(ptr long ptr) ChpeAutoNtOpenJobObject
373 stdcall -version=0x600+ NtOpenKeyTransacted(ptr long ptr ptr) ChpeStubNtOpenKeyTransacted
374 stdcall -version=0x600+ NtOpenKeyTransactedEx(ptr long ptr long long) ChpeAutoNtOpenKeyTransactedEx
375 stdcall NtOpenKeyedEvent(ptr long ptr) ChpeAutoNtOpenKeyedEvent
376 stdcall NtOpenMutant(ptr long ptr) ChpeAutoNtOpenMutant
377 stdcall NtOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoNtOpenObjectAuditAlarm
378 stdcall -version=0x600+ NtOpenPrivateNamespace(ptr long ptr ptr) ChpeStubNtOpenPrivateNamespace
379 stdcall NtOpenProcess(ptr long ptr ptr) ChpeAutoNtOpenProcess
380 stdcall NtOpenProcessToken(long long ptr) ChpeAutoNtOpenProcessToken
381 stdcall NtOpenProcessTokenEx(long long long ptr) ChpeAutoNtOpenProcessTokenEx
382 stdcall -version=0x600+ NtOpenResourceManager(ptr long ptr ptr ptr) ChpeStubNtOpenResourceManager
383 stdcall NtOpenSection(ptr long ptr) ChpeAutoNtOpenSection
384 stdcall NtOpenSemaphore(long long ptr) ChpeAutoNtOpenSemaphore
385 stdcall -version=0x600+ NtOpenSession(ptr long ptr) ChpeStubNtOpenSession
386 stdcall NtOpenSymbolicLinkObject(ptr long ptr) ChpeAutoNtOpenSymbolicLinkObject
387 stdcall NtOpenThread(ptr long ptr ptr) ChpeAutoNtOpenThread
388 stdcall NtOpenThreadToken(long long long ptr) ChpeAutoNtOpenThreadToken
389 stdcall NtOpenThreadTokenEx(long long long long ptr) ChpeAutoNtOpenThreadTokenEx
390 stdcall NtOpenTimer(ptr long ptr) ChpeAutoNtOpenTimer
391 stdcall -version=0x600+ NtOpenTransaction(ptr long ptr ptr ptr) ChpeStubNtOpenTransaction
392 stdcall -version=0x600+ NtOpenTransactionManager(ptr long ptr ptr ptr long) ChpeStubNtOpenTransactionManager
393 stdcall NtPlugPlayControl(ptr ptr long) ChpeAutoNtPlugPlayControl
394 stdcall NtPowerInformation(long ptr long ptr long) ChpeAutoNtPowerInformation
395 stdcall -version=0x600+ NtPrePrepareComplete(ptr ptr) ChpeStubNtPrePrepareComplete
396 stdcall -version=0x600+ NtPrePrepareEnlistment(ptr ptr) ChpeStubNtPrePrepareEnlistment
397 stdcall -version=0x600+ NtPrepareComplete(ptr ptr) ChpeStubNtPrepareComplete
398 stdcall -version=0x600+ NtPrepareEnlistment(ptr ptr) ChpeStubNtPrepareEnlistment
399 stdcall NtPrivilegeCheck(ptr ptr ptr) ChpeAutoNtPrivilegeCheck
400 stdcall NtPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoNtPrivilegeObjectAuditAlarm
401 stdcall NtPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoNtPrivilegedServiceAuditAlarm
402 stdcall -version=0x600+ NtPropagationComplete(ptr long long ptr) ChpeStubNtPropagationComplete
403 stdcall -version=0x600+ NtPropagationFailed(ptr long long) ChpeStubNtPropagationFailed
405 stdcall NtPulseEvent(long ptr) ChpeAutoNtPulseEvent
406 stdcall NtQueryAttributesFile(ptr ptr) ChpeAutoNtQueryAttributesFile
407 stdcall NtQueryBootEntryOrder(ptr ptr) ChpeAutoNtQueryBootEntryOrder
408 stdcall NtQueryBootOptions(ptr ptr) ChpeAutoNtQueryBootOptions
409 stdcall NtQueryDebugFilterState(long long) ChpeAutoNtQueryDebugFilterState
410 stdcall NtQueryDefaultLocale(long ptr) ChpeAutoNtQueryDefaultLocale
411 stdcall NtQueryDefaultUILanguage(ptr) ChpeAutoNtQueryDefaultUILanguage
413 stdcall NtQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoNtQueryDirectoryFileEx
414 stdcall NtQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoNtQueryDirectoryObject
415 stdcall NtQueryDriverEntryOrder(ptr ptr) ChpeAutoNtQueryDriverEntryOrder
416 stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoNtQueryEaFile
417 stdcall NtQueryEvent(long long ptr long ptr) ChpeAutoNtQueryEvent
418 stdcall NtQueryFullAttributesFile(ptr ptr) ChpeAutoNtQueryFullAttributesFile
419 stdcall NtQueryInformationAtom(long long ptr long ptr) ChpeAutoNtQueryInformationAtom
420 stdcall NtQueryInformationByName(ptr ptr ptr long long) ChpeAutoNtQueryInformationByName
421 stdcall -version=0x600+ NtQueryInformationEnlistment(ptr long ptr long ptr) ChpeStubNtQueryInformationEnlistment
423 stdcall NtQueryInformationJobObject(ptr long ptr long ptr) ChpeAutoNtQueryInformationJobObject
424 stdcall NtQueryInformationPort(ptr long ptr long ptr) ChpeAutoNtQueryInformationPort
426 stdcall -version=0x600+ NtQueryInformationResourceManager(ptr long ptr long ptr) ChpeStubNtQueryInformationResourceManager
428 stdcall NtQueryInformationToken(ptr long ptr long ptr) ChpeAutoNtQueryInformationToken
429 stdcall -version=0x600+ NtQueryInformationTransaction(ptr long ptr ptr ptr) ChpeStubNtQueryInformationTransaction
430 stdcall -version=0x600+ NtQueryInformationTransactionManager(ptr long ptr long ptr) ChpeStubNtQueryInformationTransactionManager
431 stdcall -version=0x600+ NtQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubNtQueryInformationWorkerFactory
432 stdcall NtQueryInstallUILanguage(ptr) ChpeAutoNtQueryInstallUILanguage
433 stdcall NtQueryIntervalProfile(long ptr) ChpeAutoNtQueryIntervalProfile
434 stdcall NtQueryIoCompletion(long long ptr long ptr) ChpeAutoNtQueryIoCompletion
436 stdcall -version=0x600+ NtQueryLicenseValue(wstr ptr ptr long ptr) ChpeStubNtQueryLicenseValue
437 stdcall NtQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoNtQueryMultipleValueKey
438 stdcall NtQueryMutant(long long ptr long ptr) ChpeAutoNtQueryMutant
440 stdcall NtQueryOpenSubKeys(ptr ptr) ChpeAutoNtQueryOpenSubKeys
441 stdcall NtQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoNtQueryOpenSubKeysEx
442 stdcall NtQueryPerformanceCounter(ptr ptr) ChpeAutoNtQueryPerformanceCounter
443 stdcall NtQueryPortInformationProcess() ChpeAutoNtQueryPortInformationProcess
444 stdcall NtQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoNtQueryQuotaInformationFile
445 stdcall NtQuerySection(long long long long long) ChpeAutoNtQuerySection
446 stdcall NtQuerySecurityObject(long long long long long) ChpeAutoNtQuerySecurityObject
447 stdcall NtQuerySemaphore(long long ptr long ptr) ChpeAutoNtQuerySemaphore
448 stdcall NtQuerySymbolicLinkObject(long ptr ptr) ChpeAutoNtQuerySymbolicLinkObject
449 stdcall NtQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoNtQuerySystemEnvironmentValue
450 stdcall NtQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtQuerySystemEnvironmentValueEx
452 stdcall -version=0x601+ NtQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoNtQuerySystemInformationEx
453 stdcall NtQuerySystemTime(ptr) ChpeAutoNtQuerySystemTime
454 stdcall NtQueryTimer(ptr long ptr long ptr) ChpeAutoNtQueryTimer
455 stdcall NtQueryTimerResolution(long long long) ChpeAutoNtQueryTimerResolution
459 stdcall -version=0x602+ NtQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoNtQueryWnfStateData
460 stdcall -version=0x602+ NtQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoNtQueryWnfStateNameInformation
461 stdcall NtQueueApcThread(long ptr long long long) ChpeAutoNtQueueApcThread
462 stdcall -version=0x601+ NtQueueApcThreadEx(long long ptr long long long) ChpeAutoNtQueueApcThreadEx
463 stdcall -version=0xA00+ NtQueueApcThreadEx2(long long long ptr long long long) ChpeAutoNtQueueApcThreadEx2
465 stdcall NtRaiseHardError(long long long ptr long ptr) ChpeAutoNtRaiseHardError
467 stdcall NtReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtReadFileScatter
468 stdcall -version=0x600+ NtReadOnlyEnlistment(ptr ptr) ChpeStubNtReadOnlyEnlistment
469 stdcall NtReadRequestData(ptr ptr long ptr long ptr) ChpeAutoNtReadRequestData
471 stdcall -version=0x600+ NtRecoverEnlistment(ptr ptr) ChpeStubNtRecoverEnlistment
472 stdcall -version=0x600+ NtRecoverResourceManager(ptr) ChpeStubNtRecoverResourceManager
473 stdcall -version=0x600+ NtRecoverTransactionManager(ptr) ChpeStubNtRecoverTransactionManager
474 stdcall -version=0x600+ NtRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeStubNtRegisterProtocolAddressInformation
475 stdcall NtRegisterThreadTerminatePort(ptr) ChpeAutoNtRegisterThreadTerminatePort
476 stdcall -version=0x600+ NtReleaseCMFViewOwnership() ChpeStubNtReleaseCMFViewOwnership
477 stdcall NtReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoNtReleaseKeyedEvent
478 stdcall NtReleaseMutant(long ptr) ChpeAutoNtReleaseMutant
479 stdcall NtReleaseSemaphore(long long ptr) ChpeAutoNtReleaseSemaphore
480 stdcall -version=0x600+ NtReleaseWorkerFactoryWorker(ptr) ChpeStubNtReleaseWorkerFactoryWorker
481 stdcall NtRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoNtRemoveIoCompletion
482 stdcall -version=0x600+ NtRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoNtRemoveIoCompletionEx
483 stdcall NtRemoveProcessDebug(ptr ptr) ChpeAutoNtRemoveProcessDebug
484 stdcall NtRenameKey(ptr ptr) ChpeAutoNtRenameKey
485 stdcall -version=0x600+ NtRenameTransactionManager(ptr ptr) ChpeStubNtRenameTransactionManager
486 stdcall NtReplaceKey(ptr long ptr) ChpeAutoNtReplaceKey
487 stdcall -version=0x600+ NtReplacePartitionUnit(wstr wstr long) ChpeStubNtReplacePartitionUnit
488 stdcall NtReplyPort(ptr ptr) ChpeAutoNtReplyPort
489 stdcall NtReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePort
490 stdcall NtReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoNtReplyWaitReceivePortEx
491 stdcall NtReplyWaitReplyPort(ptr ptr) ChpeAutoNtReplyWaitReplyPort
492 stdcall NtRequestDeviceWakeup(ptr) ChpeAutoNtRequestDeviceWakeup
493 stdcall NtRequestPort(ptr ptr) ChpeAutoNtRequestPort
494 stdcall NtRequestWaitReplyPort(ptr ptr ptr) ChpeAutoNtRequestWaitReplyPort
495 stdcall NtRequestWakeupLatency(long) ChpeAutoNtRequestWakeupLatency
496 stdcall NtResetEvent(long ptr) ChpeAutoNtResetEvent
497 stdcall NtResetWriteWatch(long ptr long) ChpeAutoNtResetWriteWatch
498 stdcall NtRestoreKey(long long long) ChpeAutoNtRestoreKey
499 stdcall NtResumeProcess(ptr) ChpeAutoNtResumeProcess
501 stdcall -version=0x600+ NtRollbackComplete(ptr ptr) ChpeStubNtRollbackComplete
502 stdcall -version=0x600+ NtRollbackEnlistment(ptr long ptr long) ChpeStubNtRollbackEnlistment
503 stdcall -version=0x600+ NtRollbackTransaction(ptr long) ChpeStubNtRollbackTransaction
504 stdcall -version=0x600+ NtRollforwardTransactionManager(ptr ptr) ChpeStubNtRollforwardTransactionManager
505 stdcall NtSaveKey(long long) ChpeAutoNtSaveKey
506 stdcall NtSaveKeyEx(ptr ptr long) ChpeAutoNtSaveKeyEx
507 stdcall NtSaveMergedKeys(ptr ptr ptr) ChpeAutoNtSaveMergedKeys
508 stdcall NtSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoNtSecureConnectPort
509 stdcall NtSetBootEntryOrder(ptr ptr) ChpeAutoNtSetBootEntryOrder
510 stdcall NtSetBootOptions(ptr long) ChpeAutoNtSetBootOptions
512 stdcall NtSetDebugFilterState(long long long) ChpeAutoNtSetDebugFilterState
513 stdcall NtSetDefaultHardErrorPort(ptr) ChpeAutoNtSetDefaultHardErrorPort
514 stdcall NtSetDefaultLocale(long long) ChpeAutoNtSetDefaultLocale
515 stdcall NtSetDefaultUILanguage(long) ChpeAutoNtSetDefaultUILanguage
516 stdcall NtSetDriverEntryOrder(ptr ptr) ChpeAutoNtSetDriverEntryOrder
517 stdcall NtSetEaFile(long ptr ptr long) ChpeAutoNtSetEaFile
518 stdcall NtSetEvent(long long) ChpeAutoNtSetEvent
519 stdcall NtSetEventBoostPriority(ptr) ChpeAutoNtSetEventBoostPriority
520 stdcall NtSetHighEventPair(ptr) ChpeAutoNtSetHighEventPair
521 stdcall NtSetHighWaitLowEventPair(ptr) ChpeAutoNtSetHighWaitLowEventPair
522 stdcall NtSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoNtSetInformationDebugObject
523 stdcall -version=0x600+ NtSetInformationEnlistment(ptr long ptr long) ChpeStubNtSetInformationEnlistment
524 stdcall NtSetInformationFile(ptr ptr ptr long long) ChpeAutoNtSetInformationFile
525 stdcall NtSetInformationJobObject(ptr long ptr long) ChpeAutoNtSetInformationJobObject
526 stdcall NtSetInformationKey(ptr long ptr long) ChpeAutoNtSetInformationKey
527 stdcall NtSetInformationObject(ptr long ptr long) ChpeAutoNtSetInformationObject
528 stdcall NtSetInformationProcess(ptr long ptr long) ChpeAutoNtSetInformationProcess
529 stdcall -version=0x600+ NtSetInformationResourceManager(ptr long ptr long) ChpeStubNtSetInformationResourceManager
530 stdcall NtSetInformationThread(ptr long ptr long) ChpeAutoNtSetInformationThread
531 stdcall NtSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoNtSetInformationVirtualMemory
532 stdcall NtSetInformationToken(ptr long ptr long) ChpeAutoNtSetInformationToken
533 stdcall -version=0x600+ NtSetInformationTransaction(ptr long ptr long) ChpeStubNtSetInformationTransaction
534 stdcall -version=0x600+ NtSetInformationTransactionManager(ptr long ptr long) ChpeStubNtSetInformationTransactionManager
535 stdcall -version=0x600+ NtSetInformationWorkerFactory(ptr long ptr long) ChpeStubNtSetInformationWorkerFactory
536 stdcall NtSetIntervalProfile(long long) ChpeAutoNtSetIntervalProfile
537 stdcall NtSetIoCompletion(ptr long ptr long long) ChpeAutoNtSetIoCompletion
538 stdcall -version=0x600+ NtSetIoCompletionEx(ptr ptr long long long long) ChpeAutoNtSetIoCompletionEx
539 stdcall NtSetLdtEntries(long int64 long int64) ChpeAutoNtSetLdtEntries
540 stdcall NtSetLowEventPair(ptr) ChpeAutoNtSetLowEventPair
541 stdcall NtSetLowWaitHighEventPair(ptr) ChpeAutoNtSetLowWaitHighEventPair
542 stdcall NtSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoNtSetQuotaInformationFile
543 stdcall NtSetSecurityObject(long long ptr) ChpeAutoNtSetSecurityObject
544 stdcall NtSetSystemEnvironmentValue(ptr ptr) ChpeAutoNtSetSystemEnvironmentValue
545 stdcall NtSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoNtSetSystemEnvironmentValueEx
546 stdcall NtSetSystemInformation(long ptr long) ChpeAutoNtSetSystemInformation
547 stdcall NtSetSystemPowerState(long long long) ChpeAutoNtSetSystemPowerState
548 stdcall NtSetSystemTime(ptr ptr) ChpeAutoNtSetSystemTime
549 stdcall NtSetThreadExecutionState(long ptr) ChpeAutoNtSetThreadExecutionState
551 stdcall NtSetTimerResolution(long long ptr) ChpeAutoNtSetTimerResolution
552 stdcall NtSetUuidSeed(ptr) ChpeAutoNtSetUuidSeed
554 stdcall NtSetVolumeInformationFile(long ptr ptr long long) ChpeAutoNtSetVolumeInformationFile
555 stdcall NtShutdownSystem(long) ChpeAutoNtShutdownSystem
556 stdcall -version=0x600+ NtShutdownWorkerFactory(ptr ptr) ChpeStubNtShutdownWorkerFactory
557 stdcall NtSignalAndWaitForSingleObject(long long long ptr) ChpeAutoNtSignalAndWaitForSingleObject
558 stdcall -version=0x600+ NtSinglePhaseReject(ptr ptr) ChpeStubNtSinglePhaseReject
559 stdcall NtStartProfile(ptr) ChpeAutoNtStartProfile
560 stdcall NtStopProfile(ptr) ChpeAutoNtStopProfile
561 stdcall -version=0x602+ NtSubscribeWnfStateChange(ptr long long ptr) ChpeAutoNtSubscribeWnfStateChange
564 stdcall NtSystemDebugControl(long ptr long ptr long ptr) ChpeAutoNtSystemDebugControl
565 stdcall NtTerminateJobObject(ptr long) ChpeAutoNtTerminateJobObject
568 stdcall NtTestAlert() ChpeAutoNtTestAlert
569 stdcall -version=0x600+ NtThawRegistry() ChpeStubNtThawRegistry
570 stdcall -version=0x600+ NtThawTransactions() ChpeStubNtThawTransactions
571 stdcall -version=0x600+ NtTraceControl(long ptr long ptr long long) ChpeStubNtTraceControl
572 stdcall NtTraceEvent(long long long ptr) ChpeAutoNtTraceEvent
573 stdcall NtTranslateFilePath(ptr long ptr long) ChpeAutoNtTranslateFilePath
574 stdcall NtUnloadDriver(ptr) ChpeAutoNtUnloadDriver
575 stdcall NtUnloadKey2(ptr long) ChpeAutoNtUnloadKey2
576 stdcall NtUnloadKey(long) ChpeAutoNtUnloadKey
577 stdcall NtUnloadKeyEx(ptr ptr) ChpeAutoNtUnloadKeyEx
578 stdcall NtUnlockFile(long ptr ptr ptr ptr) ChpeAutoNtUnlockFile
579 stdcall NtUnlockVirtualMemory(long ptr ptr long) ChpeAutoNtUnlockVirtualMemory
582 stdcall -version=0x602+ NtUnsubscribeWnfStateChange(ptr) ChpeAutoNtUnsubscribeWnfStateChange
583 stdcall -version=0x602+ NtUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoNtUpdateWnfStateData
584 stdcall NtVdmControl(long ptr) ChpeAutoNtVdmControl
585 stdcall NtWaitForDebugEvent(ptr long ptr ptr) ChpeAutoNtWaitForDebugEvent
586 stdcall NtWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoNtWaitForKeyedEvent
588 stdcall NtWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects32
589 stdcall NtWaitForMultipleObjects(long ptr long long ptr) ChpeAutoNtWaitForMultipleObjects
590 stdcall NtWaitForSingleObject(long long long) ChpeAutoNtWaitForSingleObject
591 stub -version=0x600+ NtWaitForWorkViaWorkerFactory
592 stdcall NtWaitHighEventPair(ptr) ChpeAutoNtWaitHighEventPair
593 stdcall NtWaitLowEventPair(ptr) ChpeAutoNtWaitLowEventPair
594 stdcall -version=0x600+ NtWorkerFactoryWorkerReady(long) ChpeStubNtWorkerFactoryWorkerReady
596 stdcall NtWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoNtWriteFileGather
597 stdcall NtWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoNtWriteRequestData
599 stdcall NtYieldExecution() ChpeAutoNtYieldExecution
600 stdcall -version=0x600+ NtdllDefWindowProc_A(long long long long) ChpeStubNtdllDefWindowProc_A
601 stdcall -version=0x600+ NtdllDefWindowProc_W(long long long long) ChpeStubNtdllDefWindowProc_W
602 stdcall -version=0x600+ NtdllDialogWndProc_A(long long long long) ChpeStubNtdllDialogWndProc_A
603 stdcall -version=0x600+ NtdllDialogWndProc_W(long long long long) ChpeStubNtdllDialogWndProc_W
604 stdcall PfxFindPrefix(ptr ptr) ChpeAutoPfxFindPrefix
605 stdcall PfxInitialize(ptr) ChpeAutoPfxInitialize
606 stdcall PfxInsertPrefix(ptr ptr ptr) ChpeAutoPfxInsertPrefix
607 stdcall PfxRemovePrefix(ptr ptr) ChpeAutoPfxRemovePrefix
608 stdcall RtlAbortRXact(ptr) ChpeAutoRtlAbortRXact
609 stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlAbsoluteToSelfRelativeSD
610 stdcall RtlAcquirePebLock() ChpeAutoRtlAcquirePebLock
612 stdcall RtlAcquireResourceExclusive(ptr long) ChpeAutoRtlAcquireResourceExclusive
613 stdcall RtlAcquireResourceShared(ptr long) ChpeAutoRtlAcquireResourceShared
616 stdcall RtlActivateActivationContext(long ptr ptr) ChpeAutoRtlActivateActivationContext
617 stdcall RtlActivateActivationContextEx(long ptr ptr ptr) ChpeAutoRtlActivateActivationContextEx
618 stdcall RtlActivateActivationContextUnsafeFast(ptr ptr) ChpeAutoRtlActivateActivationContextUnsafeFast
619 stdcall RtlAddAccessAllowedAce(ptr long long ptr) ChpeAutoRtlAddAccessAllowedAce
620 stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessAllowedAceEx
621 stdcall RtlAddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessAllowedObjectAce
622 stdcall RtlAddAccessDeniedAce(ptr long long ptr) ChpeAutoRtlAddAccessDeniedAce
623 stdcall RtlAddAccessDeniedAceEx(ptr long long long ptr) ChpeAutoRtlAddAccessDeniedAceEx
624 stdcall RtlAddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) ChpeAutoRtlAddAccessDeniedObjectAce
625 stdcall RtlAddAce(ptr long long ptr long) ChpeAutoRtlAddAce
626 stdcall RtlAddActionToRXact(ptr long ptr long ptr long) ChpeAutoRtlAddActionToRXact
627 stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ChpeAutoRtlAddAtomToAtomTable
628 stdcall RtlAddAttributeActionToRXact(ptr long ptr ptr ptr long ptr long) ChpeAutoRtlAddAttributeActionToRXact
629 stdcall RtlAddAuditAccessAce(ptr long long ptr long long) ChpeAutoRtlAddAuditAccessAce
630 stdcall RtlAddAuditAccessAceEx(ptr long long long ptr long long) ChpeAutoRtlAddAuditAccessAceEx
631 stdcall RtlAddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) ChpeAutoRtlAddAuditAccessObjectAce
632 stdcall  RtlAddCompoundAce(ptr long long long ptr ptr) ChpeStubRtlAddCompoundAce
635 stdcall -version=0x600+ RtlAddMandatoryAce(ptr long long long long ptr) ChpeAutoRtlAddMandatoryAce
636 stdcall RtlAddRefActivationContext(ptr) ChpeAutoRtlAddRefActivationContext
637 stdcall RtlAddRefMemoryStream(ptr) ChpeAutoRtlAddRefMemoryStream
638 stdcall -version=0x600+ RtlAddSIDToBoundaryDescriptor(ptr ptr) ChpeStubRtlAddSIDToBoundaryDescriptor
641 stdcall  RtlAddressInSectionTable(ptr ptr long) ChpeStubRtlAddressInSectionTable
642 stdcall RtlAdjustPrivilege(long long long ptr) ChpeAutoRtlAdjustPrivilege
643 stdcall RtlAllocateActivationContextStack(ptr) ChpeAutoRtlAllocateActivationContextStack
644 stdcall RtlAllocateAndInitializeSid(ptr long long long long long long long long long ptr) ChpeAutoRtlAllocateAndInitializeSid
645 stdcall RtlAllocateHandle(ptr ptr) ChpeAutoRtlAllocateHandle
647 stdcall -version=0x600+ RtlAllocateMemoryBlockLookaside(ptr long ptr) ChpeStubRtlAllocateMemoryBlockLookaside
648 stdcall -version=0x600+ RtlAllocateMemoryZone(long long ptr) ChpeStubRtlAllocateMemoryZone
649 stdcall RtlAnsiCharToUnicodeChar(ptr) ChpeAutoRtlAnsiCharToUnicodeChar
650 stdcall RtlAnsiStringToUnicodeSize(ptr) ChpeAutoRtlAnsiStringToUnicodeSize
651 stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ChpeAutoRtlAnsiStringToUnicodeString
652 stdcall RtlAppendAsciizToString(ptr str) ChpeAutoRtlAppendAsciizToString
653 stdcall  RtlAppendPathElement(ptr ptr ptr) ChpeStubRtlAppendPathElement
654 stdcall RtlAppendStringToString(ptr ptr) ChpeAutoRtlAppendStringToString
655 stdcall RtlAppendUnicodeStringToString(ptr ptr) ChpeAutoRtlAppendUnicodeStringToString
656 stdcall RtlAppendUnicodeToString(ptr wstr) ChpeAutoRtlAppendUnicodeToString
657 stdcall RtlApplicationVerifierStop(ptr str ptr str ptr str ptr str ptr str) ChpeAutoRtlApplicationVerifierStop
658 stdcall RtlApplyRXact(ptr) ChpeAutoRtlApplyRXact
659 stdcall RtlApplyRXactNoFlush(ptr) ChpeAutoRtlApplyRXactNoFlush
660 stdcall RtlAreAllAccessesGranted(long long) ChpeAutoRtlAreAllAccessesGranted
661 stdcall RtlAreAnyAccessesGranted(long long) ChpeAutoRtlAreAnyAccessesGranted
662 stdcall RtlAreBitsClear(ptr long long) ChpeAutoRtlAreBitsClear
663 stdcall RtlAreBitsSet(ptr long long) ChpeAutoRtlAreBitsSet
664 stdcall RtlAssert(ptr ptr long ptr) ChpeAutoRtlAssert
665 stdcall -version=0x600+ RtlBarrier(ptr long) ChpeStubRtlBarrier
666 stdcall -version=0x600+ RtlBarrierForDelete(long long) ChpeStubRtlBarrierForDelete
667 stdcall RtlCancelTimer(ptr ptr) ChpeAutoRtlCancelTimer
670 stdcall RtlCharToInteger(ptr long ptr) ChpeAutoRtlCharToInteger
671 stdcall RtlCheckForOrphanedCriticalSections(ptr) ChpeAutoRtlCheckForOrphanedCriticalSections
672 stdcall RtlCheckRegistryKey(long ptr) ChpeAutoRtlCheckRegistryKey
673 stdcall -version=0x600+ RtlCleanUpTEBLangLists() ChpeStubRtlCleanUpTEBLangLists
674 stdcall RtlClearAllBits(ptr) ChpeAutoRtlClearAllBits
675 stdcall RtlClearBits(ptr long long) ChpeAutoRtlClearBits
676 stdcall RtlCloneMemoryStream(ptr ptr) ChpeAutoRtlCloneMemoryStream
677 stdcall -version=0x600+ RtlCloneUserProcess(long long long long long) ChpeStubRtlCloneUserProcess
678 stdcall -version=0x600+ RtlCmDecodeMemIoResource(ptr ptr) ChpeStubRtlCmDecodeMemIoResource
679 stdcall -version=0x600+ RtlCmEncodeMemIoResource(ptr long long long) ChpeStubRtlCmEncodeMemIoResource
680 stdcall -version=0x600+ RtlCommitDebugInfo(ptr long) ChpeStubRtlCommitDebugInfo
681 stdcall RtlCommitMemoryStream(ptr long) ChpeAutoRtlCommitMemoryStream
682 stdcall RtlCompactHeap(long long) ChpeAutoRtlCompactHeap
683 stdcall -version=0x600+ RtlCompareAltitudes(wstr wstr) ChpeStubRtlCompareAltitudes
685 stdcall RtlCompareMemoryUlong(ptr long long) ChpeAutoRtlCompareMemoryUlong
686 stdcall RtlCompareString(ptr ptr long) ChpeAutoRtlCompareString
688 stdcall -version=0x600+ RtlCompareUnicodeStrings(wstr long wstr long long) ChpeAutoRtlCompareUnicodeStrings
689 stdcall -version=0x600+ RtlCompleteProcessCloning(long) ChpeStubRtlCompleteProcessCloning
690 stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ChpeAutoRtlCompressBuffer
691 stdcall RtlComputeCrc32(long ptr long) ChpeAutoRtlComputeCrc32
692 stdcall RtlComputeImportTableHash(ptr ptr long) ChpeAutoRtlComputeImportTableHash
693 stdcall RtlComputePrivatizedDllName_U(ptr ptr ptr) ChpeAutoRtlComputePrivatizedDllName_U
694 stdcall -version=0x600+ RtlConnectToSm(ptr ptr long ptr) ChpeStubRtlConnectToSm
695 stdcall RtlConsoleMultiByteToUnicodeN(ptr long ptr ptr long ptr) ChpeAutoRtlConsoleMultiByteToUnicodeN
696 stdcall RtlConvertExclusiveToShared(ptr) ChpeAutoRtlConvertExclusiveToShared
697 stdcall -version=0x600+ RtlConvertLCIDToString(long long long ptr long) ChpeAutoRtlConvertLCIDToString
698 stdcall RtlConvertSharedToExclusive(ptr) ChpeAutoRtlConvertSharedToExclusive
699 stdcall RtlConvertSidToUnicodeString(ptr ptr long) ChpeAutoRtlConvertSidToUnicodeString
700 stdcall RtlConvertToAutoInheritSecurityObject(ptr ptr ptr ptr long ptr) ChpeAutoRtlConvertToAutoInheritSecurityObject
701 stdcall RtlConvertUiListToApiList(ptr ptr long) ChpeAutoRtlConvertUiListToApiList
702 stdcall RtlCopyLuid(ptr ptr) ChpeAutoRtlCopyLuid
703 stdcall RtlCopyLuidAndAttributesArray(long ptr ptr) ChpeAutoRtlCopyLuidAndAttributesArray
704 stdcall RtlCopyMappedMemory(ptr ptr long) ChpeAutoRtlCopyMappedMemory
705 stdcall -version=0x600+ RtlCopyExtendedContext(ptr long ptr) ChpeAutoRtlCopyExtendedContext
706 cdecl -version=0x600+ RtlCopyMemory(ptr ptr long) ChpeAutoRtlCopyMemory
707 stdcall -version=0x600+ RtlCopyMemoryNonTemporal(ptr ptr long) ChpeStubRtlCopyMemoryNonTemporal
708 stdcall RtlCopyMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyMemoryStreamTo
709 stdcall RtlCopyOutOfProcessMemoryStreamTo(ptr ptr int64 ptr ptr) ChpeAutoRtlCopyOutOfProcessMemoryStreamTo
710 stdcall RtlCopySecurityDescriptor(ptr ptr) ChpeAutoRtlCopySecurityDescriptor
711 stdcall RtlCopySid(long ptr ptr) ChpeAutoRtlCopySid
712 stdcall RtlCopySidAndAttributesArray(long ptr long ptr ptr ptr ptr) ChpeAutoRtlCopySidAndAttributesArray
713 stdcall RtlCopyString(ptr ptr) ChpeAutoRtlCopyString
715 stdcall RtlCreateAcl(ptr long long) ChpeAutoRtlCreateAcl
716 stdcall RtlCreateActivationContext(long ptr long ptr ptr ptr) ChpeAutoRtlCreateActivationContext
717 stdcall RtlCreateAndSetSD(ptr long ptr ptr ptr) ChpeAutoRtlCreateAndSetSD
718 stdcall RtlContractHashTable(ptr) ChpeAutoRtlContractHashTable
719 stdcall RtlCreateAtomTable(long ptr) ChpeAutoRtlCreateAtomTable
720 stdcall RtlCreateHashTable(ptr long long) ChpeAutoRtlCreateHashTable
721 stdcall RtlCreateBootStatusDataFile() ChpeAutoRtlCreateBootStatusDataFile
722 stdcall -version=0x600+ RtlCreateBoundaryDescriptor(ptr long) ChpeStubRtlCreateBoundaryDescriptor
723 stdcall RtlCreateEnvironment(long ptr) ChpeAutoRtlCreateEnvironment
724 stdcall -version=0x600+ RtlCreateEnvironmentEx(ptr ptr long) ChpeStubRtlCreateEnvironmentEx
725 stdcall RtlCreateHeap(long ptr long long ptr ptr) ChpeAutoRtlCreateHeap
726 stdcall -version=0x600+ RtlCreateMemoryBlockLookaside(ptr long long long long) ChpeStubRtlCreateMemoryBlockLookaside
727 stdcall -version=0x600+ RtlCreateMemoryZone(ptr long long) ChpeStubRtlCreateMemoryZone
728 stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlCreateProcessParameters
729 stdcall -version=0x600+ RtlCreateProcessParametersEx(ptr wstr ptr wstr wstr ptr wstr wstr ptr ptr long) ChpeAutoRtlCreateProcessParametersEx
730 stdcall RtlCreateQueryDebugBuffer(long long) ChpeAutoRtlCreateQueryDebugBuffer
731 stdcall RtlCreateRegistryKey(long wstr) ChpeAutoRtlCreateRegistryKey
732 stdcall RtlCreateSecurityDescriptor(ptr long) ChpeAutoRtlCreateSecurityDescriptor
733 stdcall RtlCreateServiceSid(ptr ptr ptr) ChpeAutoRtlCreateServiceSid
734 stdcall -version=0xA00+ RtlDeriveCapabilitySidsFromName(ptr ptr ptr) ChpeAutoRtlDeriveCapabilitySidsFromName
735 stdcall RtlCreateSystemVolumeInformationFolder(ptr) ChpeAutoRtlCreateSystemVolumeInformationFolder
736 stdcall RtlCreateTagHeap(ptr long wstr wstr) ChpeAutoRtlCreateTagHeap
737 stdcall RtlCreateTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlCreateTimer
738 stdcall RtlCreateTimerQueue(ptr) ChpeAutoRtlCreateTimerQueue
739 stdcall RtlCreateUnicodeString(ptr wstr) ChpeAutoRtlCreateUnicodeString
740 stdcall RtlCreateUnicodeStringFromAsciiz(ptr str) ChpeAutoRtlCreateUnicodeStringFromAsciiz
741 stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlCreateUserProcess
742 stdcall RtlCreateUserSecurityObject(ptr long ptr ptr long ptr ptr) ChpeAutoRtlCreateUserSecurityObject
743 stdcall -version=0x600+ RtlCreateUserStack(long long long long long ptr) ChpeStubRtlCreateUserStack
745 stdcall -version=0x600+ RtlCultureNameToLCID(ptr ptr) ChpeAutoRtlCultureNameToLCID
746 stdcall RtlCustomCPToUnicodeN(ptr wstr long ptr str long) ChpeAutoRtlCustomCPToUnicodeN
747 stdcall RtlCutoverTimeToSystemTime(ptr ptr ptr long) ChpeAutoRtlCutoverTimeToSystemTime
748 stdcall -version=0x600+ RtlDeCommitDebugInfo(long long long) ChpeStubRtlDeCommitDebugInfo
749 stdcall RtlDeNormalizeProcessParams(ptr) ChpeAutoRtlDeNormalizeProcessParams
750 stdcall RtlDeactivateActivationContext(long long) ChpeAutoRtlDeactivateActivationContext
751 stdcall RtlDeactivateActivationContextUnsafeFast(ptr) ChpeAutoRtlDeactivateActivationContextUnsafeFast
752 stdcall  RtlDebugPrintTimes() ChpeStubRtlDebugPrintTimes
755 stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ChpeAutoRtlDecompressBuffer
757 stdcall RtlDefaultNpAcl(ptr) ChpeAutoRtlDefaultNpAcl
758 stdcall RtlDelete(ptr) ChpeAutoRtlDelete
759 stdcall RtlDeleteAce(ptr long) ChpeAutoRtlDeleteAce
760 stdcall RtlDeleteAtomFromAtomTable(ptr long) ChpeAutoRtlDeleteAtomFromAtomTable
761 stdcall RtlDeleteHashTable(ptr) ChpeAutoRtlDeleteHashTable
762 stdcall -version=0x600+ RtlDeleteBarrier(long) ChpeStubRtlDeleteBarrier
763 stdcall -version=0x600+ RtlDeleteBoundaryDescriptor(ptr) ChpeStubRtlDeleteBoundaryDescriptor
765 stdcall RtlDeleteElementGenericTable(ptr ptr) ChpeAutoRtlDeleteElementGenericTable
766 stdcall RtlDeleteElementGenericTableAvl(ptr ptr) ChpeAutoRtlDeleteElementGenericTableAvl
769 stdcall RtlDeleteNoSplay(ptr ptr) ChpeAutoRtlDeleteNoSplay
770 stdcall RtlDeleteRegistryValue(long ptr ptr) ChpeAutoRtlDeleteRegistryValue
771 stdcall RtlDeleteResource(ptr) ChpeAutoRtlDeleteResource
772 stdcall RtlDeleteSecurityObject(ptr) ChpeAutoRtlDeleteSecurityObject
773 stdcall RtlDeleteTimer(ptr ptr ptr) ChpeAutoRtlDeleteTimer
774 stdcall RtlDeleteTimerQueue(ptr) ChpeAutoRtlDeleteTimerQueue
775 stdcall RtlDeleteTimerQueueEx(ptr ptr) ChpeAutoRtlDeleteTimerQueueEx
776 stdcall -version=0x600+ RtlDeregisterSecureMemoryCacheCallback(ptr) ChpeStubRtlDeregisterSecureMemoryCacheCallback
777 stdcall RtlDeregisterWait(ptr) ChpeAutoRtlDeregisterWait
778 stdcall RtlDeregisterWaitEx(ptr ptr) ChpeAutoRtlDeregisterWaitEx
779 stdcall RtlDestroyAtomTable(ptr) ChpeAutoRtlDestroyAtomTable
780 stdcall RtlDestroyEnvironment(ptr) ChpeAutoRtlDestroyEnvironment
781 stdcall RtlDestroyHandleTable(ptr) ChpeAutoRtlDestroyHandleTable
782 stdcall RtlDestroyHeap(long) ChpeAutoRtlDestroyHeap
783 stdcall -version=0x600+ RtlDestroyMemoryBlockLookaside(long) ChpeStubRtlDestroyMemoryBlockLookaside
784 stdcall -version=0x600+ RtlDestroyMemoryZone(long) ChpeStubRtlDestroyMemoryZone
785 stdcall RtlDestroyProcessParameters(ptr) ChpeAutoRtlDestroyProcessParameters
786 stdcall RtlDestroyQueryDebugBuffer(ptr) ChpeAutoRtlDestroyQueryDebugBuffer
787 stdcall RtlDetermineDosPathNameType_U(wstr) ChpeAutoRtlDetermineDosPathNameType_U
788 stdcall RtlDllShutdownInProgress() ChpeAutoRtlDllShutdownInProgress
789 stdcall RtlDnsHostNameToComputerName(ptr ptr long) ChpeAutoRtlDnsHostNameToComputerName
790 stdcall RtlDoesFileExists_U(wstr) ChpeAutoRtlDoesFileExists_U
791 stdcall RtlDosApplyFileIsolationRedirection_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosApplyFileIsolationRedirection_Ustr
792 stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U
793 stdcall RtlDosPathNameToNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToNtPathName_U_WithStatus
794 stdcall RtlDosPathNameToRelativeNtPathName_U(ptr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U
795 stdcall RtlDosPathNameToRelativeNtPathName_U_WithStatus(wstr ptr ptr ptr) ChpeAutoRtlDosPathNameToRelativeNtPathName_U_WithStatus
796 stdcall RtlDosSearchPath_U(wstr wstr wstr long ptr ptr) ChpeAutoRtlDosSearchPath_U
797 stdcall RtlDosSearchPath_Ustr(long ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlDosSearchPath_Ustr
798 stdcall RtlDowncaseUnicodeChar(long) ChpeAutoRtlDowncaseUnicodeChar
799 stdcall RtlDowncaseUnicodeString(ptr ptr long) ChpeAutoRtlDowncaseUnicodeString
800 stdcall RtlDumpResource(ptr) ChpeAutoRtlDumpResource
801 stdcall RtlDuplicateUnicodeString(long ptr ptr) ChpeAutoRtlDuplicateUnicodeString
802 stdcall RtlEmptyAtomTable(ptr long) ChpeAutoRtlEmptyAtomTable
803 stdcall RtlEndEnumerationHashTable(ptr ptr) ChpeAutoRtlEndEnumerationHashTable
804 stdcall RtlEndWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlEndWeakEnumerationHashTable
805 stdcall  RtlEnableEarlyCriticalSectionEventCreation() ChpeStubRtlEnableEarlyCriticalSectionEventCreation
809 stdcall RtlEnumProcessHeaps(ptr ptr) ChpeAutoRtlEnumProcessHeaps
810 stdcall RtlEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlEnumerateEntryHashTable
811 stdcall RtlEnumerateGenericTable(ptr long) ChpeAutoRtlEnumerateGenericTable
812 stdcall RtlEnumerateGenericTableAvl(ptr long) ChpeAutoRtlEnumerateGenericTableAvl
813 stdcall RtlEnumerateGenericTableLikeADirectory(ptr ptr ptr long ptr ptr ptr) ChpeAutoRtlEnumerateGenericTableLikeADirectory
814 stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplaying
815 stdcall RtlEnumerateGenericTableWithoutSplayingAvl(ptr ptr) ChpeAutoRtlEnumerateGenericTableWithoutSplayingAvl
816 stdcall RtlEqualComputerName(ptr ptr) ChpeAutoRtlEqualComputerName
817 stdcall RtlEqualDomainName(ptr ptr) ChpeAutoRtlEqualDomainName
818 stdcall RtlEqualLuid(ptr ptr) ChpeAutoRtlEqualLuid
819 stdcall RtlExpandHashTable(ptr) ChpeAutoRtlExpandHashTable
820 stdcall RtlEqualPrefixSid(ptr ptr) ChpeAutoRtlEqualPrefixSid
821 stdcall RtlEqualSid(long long) ChpeAutoRtlEqualSid
822 stdcall RtlEqualString(ptr ptr long) ChpeAutoRtlEqualString
823 stdcall RtlEqualUnicodeString(ptr ptr long) ChpeAutoRtlEqualUnicodeString
824 stdcall RtlEraseUnicodeString(ptr) ChpeAutoRtlEraseUnicodeString
827 stdcall RtlWow64GetCurrentCpuArea(ptr ptr ptr) ChpeAutoRtlWow64GetCurrentCpuArea
829 stdcall ChpeCanContinueToGuest() ChpeAutoChpeCanContinueToGuest
830 stdcall ChpeContinueToGuest(ptr) ChpeAutoChpeContinueToGuest
831 stdcall ChpeContinueToGuestEx(ptr long) ChpeAutoChpeContinueToGuestEx
832 stdcall ProcessPendingCrossProcessEmulatorWork() ChpeAutoProcessPendingCrossProcessEmulatorWork
833 stdcall ChpeIsProcessorFeaturePresent(long) ChpeAutoChpeIsProcessorFeaturePresent
834 stdcall RtlWow64PopAllCrossProcessWorkFromWorkList(ptr ptr) ChpeAutoRtlWow64PopAllCrossProcessWorkFromWorkList
835 stdcall RtlWow64PushCrossProcessWorkOntoFreeList(ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoFreeList
842 stdcall RtlExitUserProcess(long) ChpeAutoRtlExitUserProcess
844 stdcall -version=0x600+ RtlExpandEnvironmentStrings(long ptr long ptr long ptr) ChpeAutoRtlExpandEnvironmentStrings
845 stdcall RtlExpandEnvironmentStrings_U(ptr ptr ptr ptr) ChpeAutoRtlExpandEnvironmentStrings_U
846 stdcall -version=0x600+ RtlExtendMemoryBlockLookaside(long) ChpeStubRtlExtendMemoryBlockLookaside
847 stdcall -version=0x600+ RtlExtendMemoryZone(long long) ChpeStubRtlExtendMemoryZone
848 stdcall -ret64 RtlExtendedLargeIntegerDivide(double long ptr) ChpeAutoRtlExtendedLargeIntegerDivide
850 stdcall RtlFillMemoryUlong(ptr long long) ChpeAutoRtlFillMemoryUlong
851 stdcall RtlFinalReleaseOutOfProcessMemoryStream(ptr) ChpeAutoRtlFinalReleaseOutOfProcessMemoryStream
852 stdcall -version=0x600+ RtlFindAceByType(long long ptr) ChpeStubRtlFindAceByType
853 stdcall RtlFindActivationContextSectionGuid(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionGuid
854 stdcall RtlFindActivationContextSectionString(long ptr long ptr ptr) ChpeAutoRtlFindActivationContextSectionString
855 stdcall RtlFindCharInUnicodeString(long ptr ptr ptr) ChpeAutoRtlFindCharInUnicodeString
856 stdcall RtlFindClearBits(ptr long long) ChpeAutoRtlFindClearBits
857 stdcall RtlFindClearBitsAndSet(ptr long long) ChpeAutoRtlFindClearBitsAndSet
858 stdcall RtlFindClearRuns(ptr ptr long long) ChpeAutoRtlFindClearRuns
859 stdcall -version=0x600+ RtlFindClosestEncodableLength(long ptr) ChpeStubRtlFindClosestEncodableLength
861 stdcall RtlFindLastBackwardRunClear(ptr long ptr) ChpeAutoRtlFindLastBackwardRunClear
862 stdcall RtlFindLeastSignificantBit(double) ChpeAutoRtlFindLeastSignificantBit
863 stdcall RtlFindLongestRunClear(ptr long) ChpeAutoRtlFindLongestRunClear
864 stdcall RtlFindMessage(long long long long ptr) ChpeAutoRtlFindMessage
865 stdcall RtlFindMostSignificantBit(double) ChpeAutoRtlFindMostSignificantBit
866 stdcall RtlFindNextForwardRunClear(ptr long ptr) ChpeAutoRtlFindNextForwardRunClear
867 stdcall RtlFindSetBits(ptr long long) ChpeAutoRtlFindSetBits
868 stdcall RtlFindSetBitsAndClear(ptr long long) ChpeAutoRtlFindSetBitsAndClear
869 stdcall RtlFirstEntrySList(ptr) ChpeAutoRtlFirstEntrySList
870 stdcall RtlFirstFreeAce(ptr ptr) ChpeAutoRtlFirstFreeAce
875 stdcall RtlFlushSecureMemoryCache(ptr ptr) ChpeAutoRtlFlushSecureMemoryCache
877 stdcall RtlFormatMessage(ptr long long long long ptr ptr long ptr) ChpeAutoRtlFormatMessage
878 stdcall RtlFormatMessageEx(ptr long long long long ptr ptr long ptr long) ChpeAutoRtlFormatMessageEx
879 stdcall RtlFreeActivationContextStack(ptr) ChpeAutoRtlFreeActivationContextStack
880 stdcall RtlFreeAnsiString(long) ChpeAutoRtlFreeAnsiString
881 stdcall RtlFreeHandle(ptr ptr) ChpeAutoRtlFreeHandle
883 stdcall -version=0x600+ RtlFreeMemoryBlockLookaside(long long) ChpeStubRtlFreeMemoryBlockLookaside
884 stdcall RtlFreeOemString(ptr) ChpeAutoRtlFreeOemString
885 stdcall RtlFreeSid(long) ChpeAutoRtlFreeSid
886 stdcall RtlFreeThreadActivationContextStack() ChpeAutoRtlFreeThreadActivationContextStack
888 stdcall -version=0x600+ RtlFreeUserStack(long) ChpeStubRtlFreeUserStack
889 stdcall RtlGUIDFromString(ptr ptr) ChpeAutoRtlGUIDFromString
891 stdcall RtlGetAce(ptr long ptr) ChpeAutoRtlGetAce
892 stdcall RtlGetActiveActivationContext(ptr) ChpeAutoRtlGetActiveActivationContext
893 stdcall RtlGetCallersAddress(ptr ptr) ChpeAutoRtlGetCallersAddress
894 stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ChpeAutoRtlGetCompressionWorkSpaceSize
895 stdcall RtlGetControlSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetControlSecurityDescriptor
896 stdcall RtlGetCriticalSectionRecursionCount(ptr) ChpeAutoRtlGetCriticalSectionRecursionCount
897 stdcall RtlGetCurrentDirectory_U(long ptr) ChpeAutoRtlGetCurrentDirectory_U
901 stdcall -version=0x600+ RtlGetCurrentTransaction() ChpeStubRtlGetCurrentTransaction
902 stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetDaclSecurityDescriptor
903 stdcall -version=0xA00+ RtlGetDeviceFamilyInfoEnum(ptr ptr ptr) ChpeAutoRtlGetDeviceFamilyInfoEnum
904 stdcall RtlGetElementGenericTable(ptr long) ChpeAutoRtlGetElementGenericTable
905 stdcall RtlGetElementGenericTableAvl(ptr long) ChpeAutoRtlGetElementGenericTableAvl
906 stdcall -version=0x600+ RtlGetExtendedContextLength(long ptr) ChpeAutoRtlGetExtendedContextLength
907 stdcall -version=0x600+ RtlGetExtendedContextLength2(long ptr int64) ChpeAutoRtlGetExtendedContextLength2
908 stdcall -version=0x600+ RtlGetFileMUIPath(long long ptr ptr long long ptr) ChpeStubRtlGetFileMUIPath
909 stdcall RtlGetFrame() ChpeAutoRtlGetFrame
910 stdcall RtlGetFullPathName_U(wstr long ptr ptr) ChpeAutoRtlGetFullPathName_U
911 stdcall RtlGetFullPathName_UstrEx(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlGetFullPathName_UstrEx
913 stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetGroupSecurityDescriptor
914 stdcall -version=0x600+ RtlGetIntegerAtom(wstr ptr) ChpeAutoRtlGetIntegerAtom
917 stdcall -version=0x600+ RtlGetLocaleFileMappingAddress(ptr ptr ptr) ChpeAutoRtlGetLocaleFileMappingAddress
919 stdcall RtlGetLengthWithoutTrailingPathSeperators(long ptr ptr) ChpeAutoRtlGetLengthWithoutTrailingPathSeperators
920 stdcall RtlGetLongestNtPathLength() ChpeAutoRtlGetLongestNtPathLength
922 stdcall RtlGetNtGlobalFlags() ChpeAutoRtlGetNtGlobalFlags
923 stdcall RtlGetNtProductType(ptr) ChpeAutoRtlGetNtProductType
924 stdcall RtlGetNtVersionNumbers(ptr ptr ptr) ChpeAutoRtlGetNtVersionNumbers
925 stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ChpeAutoRtlGetOwnerSecurityDescriptor
926 stdcall -version=0x600+ RtlGetParentLocaleName(wstr long long long) ChpeStubRtlGetParentLocaleName
927 stdcall -version=0x600+ RtlGetProcessPreferredUILanguages(long ptr ptr ptr) ChpeAutoRtlGetProcessPreferredUILanguages
930 stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ChpeAutoRtlGetSaclSecurityDescriptor
931 stdcall RtlGetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlGetSecurityDescriptorRMControl
932 stdcall RtlGetSetBootStatusData(ptr long long ptr long long) ChpeAutoRtlGetSetBootStatusData
933 stdcall -version=0x600+ RtlGetSystemPreferredUILanguages(long ptr ptr ptr ptr) ChpeStubRtlGetSystemPreferredUILanguages
934 stdcall RtlGetThreadErrorMode() ChpeAutoRtlGetThreadErrorMode
935 stdcall -version=0x600+ RtlGetThreadLangIdByIndex(long long ptr ptr) ChpeStubRtlGetThreadLangIdByIndex
936 stdcall -version=0x600+ RtlGetThreadPreferredUILanguages(long long ptr ptr) ChpeStubRtlGetThreadPreferredUILanguages
937 stdcall -version=0x600+ RtlGetUILanguageInfo(long ptr long ptr ptr) ChpeStubRtlGetUILanguageInfo
939 stdcall -version=0x600+ RtlGetUnloadEventTraceEx(ptr ptr ptr) ChpeAutoRtlGetUnloadEventTraceEx
940 stdcall RtlGetUserInfoHeap(ptr long ptr ptr ptr) ChpeAutoRtlGetUserInfoHeap
941 stdcall -version=0x600+ RtlGetUserPreferredUILanguages(long long ptr ptr ptr) ChpeAutoRtlGetUserPreferredUILanguages
943 stdcall RtlHashUnicodeString(ptr long long ptr) ChpeAutoRtlHashUnicodeString
944 stdcall -version=0x600+ RtlHeapTrkInitialize(ptr) ChpeStubRtlHeapTrkInitialize
945 stdcall RtlIdentifierAuthoritySid(ptr) ChpeAutoRtlIdentifierAuthoritySid
946 stdcall -version=0x600+ RtlIdnToAscii(long long long long long) ChpeStubRtlIdnToAscii
947 stdcall -version=0x600+ RtlIdnToNameprepUnicode(long long long long long) ChpeStubRtlIdnToNameprepUnicode
948 stdcall -version=0x600+ RtlIdnToUnicode(long long long long long) ChpeStubRtlIdnToUnicode
949 stdcall RtlImageDirectoryEntryToData(ptr long long ptr) ChpeAutoRtlImageDirectoryEntryToData
950 stdcall RtlImageNtHeader(long) ChpeAutoRtlImageNtHeader
951 stdcall RtlImageNtHeaderEx(long ptr double ptr) ChpeAutoRtlImageNtHeaderEx
952 stdcall RtlImageRvaToSection(ptr long long) ChpeAutoRtlImageRvaToSection
953 stdcall RtlImageRvaToVa(ptr long long ptr) ChpeAutoRtlImageRvaToVa
954 stdcall RtlImpersonateSelf(long) ChpeAutoRtlImpersonateSelf
955 stdcall -version=0x600+ RtlImpersonateSelfEx(long long ptr) ChpeStubRtlImpersonateSelfEx
956 stdcall RtlInitAnsiString(ptr str) ChpeAutoRtlInitAnsiString
957 stdcall RtlInitAnsiStringEx(ptr str) ChpeAutoRtlInitAnsiStringEx
958 stdcall -version=0x600+ RtlInitBarrier(long long) ChpeStubRtlInitBarrier
959 stdcall RtlInitCodePageTable(ptr ptr) ChpeAutoRtlInitCodePageTable
960 stdcall RtlInitMemoryStream(ptr) ChpeAutoRtlInitMemoryStream
961 stdcall RtlInitNlsTables(ptr ptr ptr ptr) ChpeAutoRtlInitNlsTables
962 stdcall RtlInitOutOfProcessMemoryStream(ptr) ChpeAutoRtlInitOutOfProcessMemoryStream
963 stdcall RtlInitString(ptr str) ChpeAutoRtlInitString
964 stdcall RtlGetNextEntryHashTable(ptr ptr) ChpeAutoRtlGetNextEntryHashTable
965 stdcall RtlInitEnumerationHashTable(ptr ptr) ChpeAutoRtlInitEnumerationHashTable
967 stdcall RtlInitWeakEnumerationHashTable(ptr ptr) ChpeAutoRtlInitWeakEnumerationHashTable
968 stdcall RtlInitUnicodeStringEx(ptr wstr) ChpeAutoRtlInitUnicodeStringEx
969 stdcall  RtlInitializeAtomPackage(ptr) ChpeStubRtlInitializeAtomPackage
970 stdcall RtlInitializeBitMap(ptr long long) ChpeAutoRtlInitializeBitMap
972 stdcall RtlInitializeContext(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeContext
974 stdcall RtlInitializeCriticalSectionAndSpinCount(ptr long) ChpeAutoRtlInitializeCriticalSectionAndSpinCount
975 stdcall -version=0x600+ RtlInitializeCriticalSectionEx(ptr long long) ChpeAutoRtlInitializeCriticalSectionEx
976 stdcall -version=0x600+ RtlInitializeExtendedContext(ptr long ptr) ChpeAutoRtlInitializeExtendedContext
977 stdcall -version=0x600+ RtlInitializeExtendedContext2(ptr long ptr int64) ChpeAutoRtlInitializeExtendedContext2
978 stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTable
979 stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ChpeAutoRtlInitializeGenericTableAvl
980 stdcall RtlInitializeHandleTable(long long ptr) ChpeAutoRtlInitializeHandleTable
981 stdcall -version=0x600+ RtlInitializeNtUserPfn(wstr long ptr long wstr ptr) ChpeStubRtlInitializeNtUserPfn
982 stdcall RtlInitializeRXact(ptr long ptr) ChpeAutoRtlInitializeRXact
983 stdcall RtlInitializeResource(ptr) ChpeAutoRtlInitializeResource
986 stdcall RtlInitializeSid(ptr ptr long) ChpeAutoRtlInitializeSid
987 stdcall RtlInsertElementGenericTable(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTable
988 stdcall RtlInsertEntryHashTable(ptr ptr long ptr) ChpeAutoRtlInsertEntryHashTable
989 stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ChpeAutoRtlInsertElementGenericTableAvl
990 stdcall RtlInsertElementGenericTableFull(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFull
991 stdcall RtlInsertElementGenericTableFullAvl(ptr ptr long ptr ptr long) ChpeAutoRtlInsertElementGenericTableFullAvl
994 stdcall RtlInt64ToUnicodeString(double long ptr) ChpeAutoRtlInt64ToUnicodeString
995 stdcall RtlIntegerToChar(long long long ptr) ChpeAutoRtlIntegerToChar
996 stdcall RtlIntegerToUnicodeString(long long ptr) ChpeAutoRtlIntegerToUnicodeString
1002 stdcall -version=0x600+ RtlIoDecodeMemIoResource(ptr ptr ptr ptr) ChpeStubRtlIoDecodeMemIoResource
1003 stdcall -version=0x600+ RtlIoEncodeMemIoResource(ptr long long long long long) ChpeStubRtlIoEncodeMemIoResource
1004 stdcall RtlIpv4AddressToStringA(ptr ptr) ChpeAutoRtlIpv4AddressToStringA
1005 stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExA
1006 stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ChpeAutoRtlIpv4AddressToStringExW
1007 stdcall RtlIpv4AddressToStringW(ptr ptr) ChpeAutoRtlIpv4AddressToStringW
1008 stdcall RtlIpv4StringToAddressA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressA
1009 stdcall RtlIpv4StringToAddressExA(str long ptr ptr) ChpeAutoRtlIpv4StringToAddressExA
1010 stdcall RtlIpv4StringToAddressExW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressExW
1011 stdcall RtlIpv4StringToAddressW(wstr long ptr ptr) ChpeAutoRtlIpv4StringToAddressW
1012 stdcall RtlIpv6AddressToStringA(ptr ptr) ChpeAutoRtlIpv6AddressToStringA
1013 stdcall RtlIpv6AddressToStringExA(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExA
1014 stdcall RtlIpv6AddressToStringExW(ptr long long ptr ptr) ChpeAutoRtlIpv6AddressToStringExW
1015 stdcall RtlIpv6AddressToStringW(ptr ptr) ChpeAutoRtlIpv6AddressToStringW
1016 stdcall RtlIpv6StringToAddressA(str ptr ptr) ChpeAutoRtlIpv6StringToAddressA
1017 stdcall RtlIpv6StringToAddressExA(str ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExA
1018 stdcall RtlIpv6StringToAddressExW(wstr ptr ptr ptr) ChpeAutoRtlIpv6StringToAddressExW
1019 stdcall RtlIpv6StringToAddressW(wstr ptr ptr) ChpeAutoRtlIpv6StringToAddressW
1020 stdcall RtlIsActivationContextActive(ptr) ChpeAutoRtlIsActivationContextActive
1021 stdcall RtlIsCriticalSectionLocked(ptr) ChpeAutoRtlIsCriticalSectionLocked
1022 stdcall RtlIsCriticalSectionLockedByThread(ptr) ChpeAutoRtlIsCriticalSectionLockedByThread
1023 stdcall -version=0x600+ RtlIsCurrentProcess(ptr) ChpeAutoRtlIsCurrentProcess
1024 stdcall -version=0x600+ RtlIsCurrentThreadAttachExempt() ChpeStubRtlIsCurrentThreadAttachExempt
1025 stdcall RtlIsDosDeviceName_U(wstr) ChpeAutoRtlIsDosDeviceName_U
1026 stdcall RtlIsGenericTableEmpty(ptr) ChpeAutoRtlIsGenericTableEmpty
1027 stdcall RtlIsGenericTableEmptyAvl(ptr) ChpeAutoRtlIsGenericTableEmptyAvl
1028 stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ChpeAutoRtlIsNameLegalDOS8Dot3
1029 stdcall -version=0x600+ RtlIsNormalizedString(long ptr long ptr) ChpeStubRtlIsNormalizedString
1031 stdcall RtlIsTextUnicode(ptr long ptr) ChpeAutoRtlIsTextUnicode
1032 stdcall RtlIsThreadWithinLoaderCallout() ChpeAutoRtlIsThreadWithinLoaderCallout
1033 stdcall RtlIsValidHandle(ptr ptr) ChpeAutoRtlIsValidHandle
1034 stdcall RtlIsValidIndexHandle(ptr long ptr) ChpeAutoRtlIsValidIndexHandle
1035 stdcall -version=0x600+ RtlIsValidLocaleName(wstr long) ChpeAutoRtlIsValidLocaleName
1036 stdcall -version=0x600+ RtlLCIDToCultureName(long ptr) ChpeAutoRtlLCIDToCultureName
1037 stdcall RtlLargeIntegerToChar(ptr long long ptr) ChpeAutoRtlLargeIntegerToChar
1038 stdcall -version=0x600+ RtlLcidToLocaleName(long ptr long long) ChpeAutoRtlLcidToLocaleName
1040 stdcall RtlLengthRequiredSid(long) ChpeAutoRtlLengthRequiredSid
1041 stdcall RtlLengthSecurityDescriptor(ptr) ChpeAutoRtlLengthSecurityDescriptor
1042 stdcall RtlLengthSid(ptr) ChpeAutoRtlLengthSid
1043 stdcall RtlLocalTimeToSystemTime(ptr ptr) ChpeAutoRtlLocalTimeToSystemTime
1044 stdcall -version=0x600+ RtlLocaleNameToLcid(wstr ptr long) ChpeAutoRtlLocaleNameToLcid
1045 stdcall RtlLockBootStatusData(ptr) ChpeAutoRtlLockBootStatusData
1046 stdcall -version=0x600+ RtlLockCurrentThread() ChpeStubRtlLockCurrentThread
1047 stdcall RtlLockHeap(long) ChpeAutoRtlLockHeap
1048 stdcall -version=0x600+ RtlLockMemoryBlockLookaside(long) ChpeStubRtlLockMemoryBlockLookaside
1049 stdcall RtlLockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlLockMemoryStreamRegion
1050 stdcall -version=0x600+ RtlLockMemoryZone(long) ChpeStubRtlLockMemoryZone
1051 stdcall -version=0x600+ RtlLockModuleSection(long) ChpeStubRtlLockModuleSection
1053 stdcall  RtlLogStackBackTrace() ChpeStubRtlLogStackBackTrace
1054 stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ChpeAutoRtlLookupAtomInAtomTable
1055 stdcall RtlLookupElementGenericTable(ptr ptr) ChpeAutoRtlLookupElementGenericTable
1056 stdcall RtlLookupEntryHashTable(ptr long ptr) ChpeAutoRtlLookupEntryHashTable
1057 stdcall RtlRemoveEntryHashTable(ptr ptr ptr) ChpeAutoRtlRemoveEntryHashTable
1058 stdcall RtlLookupElementGenericTableAvl(ptr ptr) ChpeAutoRtlLookupElementGenericTableAvl
1059 stdcall RtlLookupElementGenericTableFull(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFull
1060 stdcall RtlLookupElementGenericTableFullAvl(ptr ptr ptr long) ChpeAutoRtlLookupElementGenericTableFullAvl
1063 stdcall RtlMakeSelfRelativeSD(ptr ptr ptr) ChpeAutoRtlMakeSelfRelativeSD
1064 stdcall RtlMapGenericMask(long ptr) ChpeAutoRtlMapGenericMask
1065 stdcall RtlMapSecurityErrorToNtStatus(long) ChpeAutoRtlMapSecurityErrorToNtStatus
1067 stdcall RtlMultiAppendUnicodeStringBuffer(ptr long ptr) ChpeAutoRtlMultiAppendUnicodeStringBuffer
1068 stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlMultiByteToUnicodeN
1069 stdcall RtlMultiByteToUnicodeSize(ptr str long) ChpeAutoRtlMultiByteToUnicodeSize
1070 stdcall RtlMultipleAllocateHeap(ptr long ptr long ptr) ChpeAutoRtlMultipleAllocateHeap
1071 stdcall RtlMultipleFreeHeap(ptr long long ptr) ChpeAutoRtlMultipleFreeHeap
1072 stdcall RtlNewInstanceSecurityObject(long long ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoRtlNewInstanceSecurityObject
1073 stdcall RtlNewSecurityGrantedAccess(long ptr ptr ptr ptr ptr) ChpeAutoRtlNewSecurityGrantedAccess
1074 stdcall RtlNewSecurityObject(ptr ptr ptr long ptr ptr) ChpeAutoRtlNewSecurityObject
1075 stdcall RtlNewSecurityObjectEx(ptr ptr ptr ptr long long ptr ptr) ChpeAutoRtlNewSecurityObjectEx
1076 stdcall RtlNewSecurityObjectWithMultipleInheritance(ptr ptr ptr ptr long long long ptr ptr) ChpeAutoRtlNewSecurityObjectWithMultipleInheritance
1077 stdcall RtlNormalizeProcessParams(ptr) ChpeAutoRtlNormalizeProcessParams
1078 stdcall -version=0x600+ RtlNormalizeString(long long long long ptr) ChpeStubRtlNormalizeString
1079 stdcall RtlNtPathNameToDosPathName(long ptr ptr ptr) ChpeAutoRtlNtPathNameToDosPathName
1081 stdcall RtlNtStatusToDosErrorNoTeb(long) ChpeAutoRtlNtStatusToDosErrorNoTeb
1082 stub -version=0x600+ RtlNtdllName
1083 stdcall RtlNumberGenericTableElements(ptr) ChpeAutoRtlNumberGenericTableElements
1084 stdcall RtlNumberGenericTableElementsAvl(ptr) ChpeAutoRtlNumberGenericTableElementsAvl
1085 stdcall RtlNumberOfClearBits(ptr) ChpeAutoRtlNumberOfClearBits
1086 stdcall RtlNumberOfSetBits(ptr) ChpeAutoRtlNumberOfSetBits
1087 stdcall -version=0x600+ RtlNumberOfSetBitsUlongPtr(long) ChpeAutoRtlNumberOfSetBitsUlongPtr
1088 stdcall RtlOemStringToUnicodeSize(ptr) ChpeAutoRtlOemStringToUnicodeSize
1089 stdcall RtlOemStringToUnicodeString(ptr ptr long) ChpeAutoRtlOemStringToUnicodeString
1090 stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ChpeAutoRtlOemToUnicodeN
1091 stdcall RtlOpenCurrentUser(long ptr) ChpeAutoRtlOpenCurrentUser
1092 stdcall -version=0x600+ RtlOwnerAcesPresent(long) ChpeStubRtlOwnerAcesPresent
1094 stdcall RtlPinAtomInAtomTable(ptr long) ChpeAutoRtlPinAtomInAtomTable
1095 stdcall RtlPopFrame(ptr) ChpeAutoRtlPopFrame
1096 stdcall RtlPrefixString(ptr ptr long) ChpeAutoRtlPrefixString
1097 stdcall RtlPrefixUnicodeString(ptr ptr long) ChpeAutoRtlPrefixUnicodeString
1098 stdcall -version=0x600+ RtlPrepareForProcessCloning() ChpeStubRtlPrepareForProcessCloning
1099 stdcall -version=0x600+ RtlProcessFlsData(long long) ChpeStubRtlProcessFlsData
1100 stdcall RtlProtectHeap(ptr long) ChpeAutoRtlProtectHeap
1101 stdcall RtlPushFrame(ptr) ChpeAutoRtlPushFrame
1102 stdcall -version=0x600+ RtlQueryActivationContextApplicationSettings(long ptr wstr wstr ptr ptr ptr) ChpeAutoRtlQueryActivationContextApplicationSettings
1103 stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ChpeAutoRtlQueryAtomInAtomTable
1104 stdcall -version=0x600+ RtlQueryCriticalSectionOwner(ptr) ChpeStubRtlQueryCriticalSectionOwner
1106 stdcall -version=0x600+ RtlQueryDynamicTimeZoneInformation(ptr) ChpeStubRtlQueryDynamicTimeZoneInformation
1107 stdcall -version=0x600+ RtlQueryElevationFlags(ptr) ChpeStubRtlQueryElevationFlags
1109 stdcall RtlQueryEnvironmentVariable_U(ptr ptr ptr) ChpeAutoRtlQueryEnvironmentVariable_U
1110 stdcall RtlQueryHeapInformation(long long ptr long ptr) ChpeAutoRtlQueryHeapInformation
1111 stdcall RtlQueryInformationAcl(ptr ptr long long) ChpeAutoRtlQueryInformationAcl
1112 stdcall RtlQueryInformationActivationContext(long long ptr long ptr long ptr) ChpeAutoRtlQueryInformationActivationContext
1113 stdcall RtlQueryInformationActiveActivationContext(long ptr long ptr) ChpeAutoRtlQueryInformationActiveActivationContext
1114 stdcall RtlQueryInterfaceMemoryStream(ptr ptr ptr) ChpeAutoRtlQueryInterfaceMemoryStream
1115 stdcall -version=0x600+ RtlQueryModuleInformation(ptr long ptr) ChpeStubRtlQueryModuleInformation
1116 stdcall  RtlQueryProcessBackTraceInformation(ptr) ChpeStubRtlQueryProcessBackTraceInformation
1117 stdcall RtlQueryProcessDebugInformation(long long ptr) ChpeAutoRtlQueryProcessDebugInformation
1118 stdcall RtlQueryProcessHeapInformation(ptr) ChpeAutoRtlQueryProcessHeapInformation
1119 stdcall  RtlQueryProcessLockInformation(ptr) ChpeStubRtlQueryProcessLockInformation
1120 stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValues
1121 stdcall RtlQueryRegistryValuesEx(long ptr ptr ptr ptr) ChpeAutoRtlQueryRegistryValuesEx
1122 stdcall RtlQuerySecurityObject(ptr long ptr long ptr) ChpeAutoRtlQuerySecurityObject
1123 stdcall RtlQueryTagHeap(ptr long long long ptr) ChpeAutoRtlQueryTagHeap
1124 stdcall RtlQueryTimeZoneInformation(ptr) ChpeAutoRtlQueryTimeZoneInformation
1125 stdcall RtlQueueApcWow64Thread(ptr ptr ptr ptr ptr) ChpeAutoRtlQueueApcWow64Thread
1130 stdcall RtlRandomEx(ptr) ChpeAutoRtlRandomEx
1132 stdcall RtlReadMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadMemoryStream
1133 stdcall RtlReadOutOfProcessMemoryStream(ptr ptr long ptr) ChpeAutoRtlReadOutOfProcessMemoryStream
1134 stdcall RtlRealPredecessor(ptr) ChpeAutoRtlRealPredecessor
1135 stdcall RtlRealSuccessor(ptr) ChpeAutoRtlRealSuccessor
1136 stdcall RtlRegisterSecureMemoryCacheCallback(ptr) ChpeAutoRtlRegisterSecureMemoryCacheCallback
1137 stdcall RtlRegisterCfgTargetRange(ptr long) ChpeAutoRtlRegisterCfgTargetRange
1138 stdcall -version=0x600+ RtlRegisterThreadWithCsrss() ChpeStubRtlRegisterThreadWithCsrss
1140 stdcall RtlReleaseActivationContext(ptr) ChpeAutoRtlReleaseActivationContext
1141 stdcall RtlReleaseMemoryStream(ptr) ChpeAutoRtlReleaseMemoryStream
1142 stdcall RtlReleasePebLock() ChpeAutoRtlReleasePebLock
1144 stdcall RtlReleaseRelativeName(ptr) ChpeAutoRtlReleaseRelativeName
1145 stdcall RtlReleaseResource(ptr) ChpeAutoRtlReleaseResource
1148 stdcall RtlRemoteCall(ptr ptr ptr long ptr long long) ChpeAutoRtlRemoteCall
1149 stdcall -version=0x600+ RtlRemovePrivileges(ptr ptr long) ChpeAutoRtlRemovePrivileges
1152 stdcall -version=0x600+ RtlReportException(long long long) ChpeStubRtlReportException
1153 stdcall -version=0x600+ RtlResetMemoryBlockLookaside(long) ChpeStubRtlResetMemoryBlockLookaside
1154 stdcall -version=0x600+ RtlResetMemoryZone(long) ChpeStubRtlResetMemoryZone
1155 stdcall RtlResetRtlTranslations(ptr) ChpeAutoRtlResetRtlTranslations
1158 stdcall -version=0x600+ RtlRetrieveNtUserPfn(ptr ptr ptr) ChpeStubRtlRetrieveNtUserPfn
1159 stdcall RtlRevertMemoryStream(ptr) ChpeAutoRtlRevertMemoryStream
1160 stdcall RtlRunDecodeUnicodeString(long ptr) ChpeAutoRtlRunDecodeUnicodeString
1161 stdcall RtlRunEncodeUnicodeString(long ptr) ChpeAutoRtlRunEncodeUnicodeString
1162 stdcall -version=0x600+ RtlRunOnceBeginInitialize(ptr long ptr) ChpeAutoRtlRunOnceBeginInitialize
1163 stdcall -version=0x600+ RtlRunOnceComplete(ptr long ptr) ChpeAutoRtlRunOnceComplete
1166 stdcall RtlSecondsSince1970ToTime(long ptr) ChpeAutoRtlSecondsSince1970ToTime
1167 stdcall RtlSecondsSince1980ToTime(long ptr) ChpeAutoRtlSecondsSince1980ToTime
1168 stdcall RtlSeekMemoryStream(ptr int64 long ptr) ChpeAutoRtlSeekMemoryStream
1169 stdcall RtlSelfRelativeToAbsoluteSD2(ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD2
1170 stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoRtlSelfRelativeToAbsoluteSD
1171 stdcall -version=0x600+ RtlSendMsgToSm(ptr ptr) ChpeStubRtlSendMsgToSm
1172 stdcall RtlSetAllBits(ptr) ChpeAutoRtlSetAllBits
1173 stdcall RtlSetAttributesSecurityDescriptor(ptr long ptr) ChpeAutoRtlSetAttributesSecurityDescriptor
1174 stdcall RtlSetBits(ptr long long) ChpeAutoRtlSetBits
1175 stdcall RtlSetCfgTargetValidity(ptr long long ptr) ChpeAutoRtlSetCfgTargetValidity
1176 stdcall RtlSetControlSecurityDescriptor(ptr long long) ChpeAutoRtlSetControlSecurityDescriptor
1178 stdcall RtlSetCurrentDirectory_U(ptr) ChpeAutoRtlSetCurrentDirectory_U
1179 stdcall RtlSetCurrentEnvironment(wstr ptr) ChpeAutoRtlSetCurrentEnvironment
1180 stdcall -version=0x600+ RtlSetCurrentTransaction(ptr) ChpeStubRtlSetCurrentTransaction
1181 stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetDaclSecurityDescriptor
1182 stdcall -version=0x600+ RtlSetDynamicTimeZoneInformation(long) ChpeStubRtlSetDynamicTimeZoneInformation
1183 stdcall RtlSetEnvironmentStrings(wstr long) ChpeAutoRtlSetEnvironmentStrings
1184 stdcall -version=0x600+ RtlSetEnvironmentVar(ptr ptr long ptr long) ChpeStubRtlSetEnvironmentVar
1185 stdcall RtlSetEnvironmentVariable(ptr ptr ptr) ChpeAutoRtlSetEnvironmentVariable
1186 stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetGroupSecurityDescriptor
1187 stdcall RtlSetHeapInformation(ptr long ptr ptr) ChpeAutoRtlSetHeapInformation
1188 stdcall RtlSetInformationAcl(ptr ptr long long) ChpeAutoRtlSetInformationAcl
1189 stdcall RtlSetIoCompletionCallback(long ptr long) ChpeAutoRtlSetIoCompletionCallback
1191 stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long) ChpeAutoRtlSetLastWin32ErrorAndNtStatusFromNtStatus
1192 stdcall RtlSetMemoryStreamSize(ptr int64) ChpeAutoRtlSetMemoryStreamSize
1193 stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ChpeAutoRtlSetOwnerSecurityDescriptor
1194 stdcall -version=0x600+ RtlSetProcessDebugInformation(ptr long ptr) ChpeStubRtlSetProcessDebugInformation
1195 stdcall -version=0x600+ RtlSetProcessPreferredUILanguages(long ptr ptr) ChpeAutoRtlSetProcessPreferredUILanguages
1196 cdecl RtlSetProcessIsCritical(long ptr long) ChpeAutoRtlSetProcessIsCritical
1197 stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ChpeAutoRtlSetSaclSecurityDescriptor
1198 stdcall RtlSetSecurityDescriptorRMControl(ptr ptr) ChpeAutoRtlSetSecurityDescriptorRMControl
1199 stdcall RtlSetSecurityObject(long ptr ptr ptr ptr) ChpeAutoRtlSetSecurityObject
1200 stdcall RtlSetSecurityObjectEx(long ptr ptr long ptr ptr) ChpeAutoRtlSetSecurityObjectEx
1201 stdcall RtlSetThreadErrorMode(long ptr) ChpeAutoRtlSetThreadErrorMode
1202 cdecl RtlSetThreadIsCritical(long ptr long) ChpeAutoRtlSetThreadIsCritical
1203 stdcall RtlSetThreadPoolStartFunc(ptr ptr) ChpeAutoRtlSetThreadPoolStartFunc
1204 stdcall -version=0x600+ RtlSetThreadPreferredUILanguages(long ptr ptr) ChpeStubRtlSetThreadPreferredUILanguages
1205 stdcall RtlSetTimeZoneInformation(ptr) ChpeAutoRtlSetTimeZoneInformation
1206 stdcall RtlSetTimer(ptr ptr ptr ptr long long long) ChpeAutoRtlSetTimer
1207 stdcall RtlSetUnhandledExceptionFilter(ptr) ChpeAutoRtlSetUnhandledExceptionFilter
1208 stdcall RtlSetUserFlagsHeap(ptr long ptr long long) ChpeAutoRtlSetUserFlagsHeap
1209 stdcall RtlSetUserValueHeap(ptr long ptr ptr) ChpeAutoRtlSetUserValueHeap
1210 stdcall -version=0x600+ RtlSidDominates(long long ptr) ChpeStubRtlSidDominates
1211 stdcall -version=0x600+ RtlSidEqualLevel(long long ptr) ChpeStubRtlSidEqualLevel
1212 stdcall -version=0x600+ RtlSidHashInitialize(ptr long ptr) ChpeStubRtlSidHashInitialize
1213 stdcall -version=0x600+ RtlSidHashLookup(long ptr) ChpeStubRtlSidHashLookup
1214 stdcall -version=0x600+ RtlSidIsHigherLevel(long long ptr) ChpeStubRtlSidIsHigherLevel
1216 stdcall -version=0x600+ RtlSleepConditionVariableCS(ptr ptr ptr) ChpeAutoRtlSleepConditionVariableCS
1217 stdcall -version=0x600+ RtlSleepConditionVariableSRW(ptr ptr ptr long) ChpeAutoRtlSleepConditionVariableSRW
1218 stdcall RtlSplay(ptr) ChpeAutoRtlSplay
1219 stdcall RtlStartRXact(ptr) ChpeAutoRtlStartRXact
1220 stdcall RtlStatMemoryStream(ptr ptr long) ChpeAutoRtlStatMemoryStream
1221 stdcall RtlStringFromGUID(ptr ptr) ChpeAutoRtlStringFromGUID
1222 stdcall RtlSubAuthorityCountSid(ptr) ChpeAutoRtlSubAuthorityCountSid
1223 stdcall RtlSubAuthoritySid(ptr long) ChpeAutoRtlSubAuthoritySid
1224 stdcall RtlSubtreePredecessor(ptr) ChpeAutoRtlSubtreePredecessor
1225 stdcall RtlSubtreeSuccessor(ptr) ChpeAutoRtlSubtreeSuccessor
1226 stdcall RtlSystemTimeToLocalTime(ptr ptr) ChpeAutoRtlSystemTimeToLocalTime
1227 stdcall -version=0x600+ RtlTestBit(ptr long) ChpeAutoRtlTestBit
1228 stdcall RtlTimeFieldsToTime(ptr ptr) ChpeAutoRtlTimeFieldsToTime
1229 stdcall RtlTimeToElapsedTimeFields(long long) ChpeAutoRtlTimeToElapsedTimeFields
1230 stdcall RtlTimeToSecondsSince1970(ptr ptr) ChpeAutoRtlTimeToSecondsSince1970
1231 stdcall RtlTimeToSecondsSince1980(ptr ptr) ChpeAutoRtlTimeToSecondsSince1980
1232 stdcall RtlTimeToTimeFields(long long) ChpeAutoRtlTimeToTimeFields
1233 stdcall RtlTraceDatabaseAdd(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseAdd
1234 stdcall RtlTraceDatabaseCreate(long ptr long long ptr) ChpeAutoRtlTraceDatabaseCreate
1235 stdcall RtlTraceDatabaseDestroy(ptr) ChpeAutoRtlTraceDatabaseDestroy
1236 stdcall RtlTraceDatabaseEnumerate(ptr ptr ptr) ChpeAutoRtlTraceDatabaseEnumerate
1237 stdcall RtlTraceDatabaseFind(ptr long ptr ptr) ChpeAutoRtlTraceDatabaseFind
1238 stdcall RtlTraceDatabaseLock(ptr) ChpeAutoRtlTraceDatabaseLock
1239 stdcall RtlTraceDatabaseUnlock(ptr) ChpeAutoRtlTraceDatabaseUnlock
1240 stdcall RtlTraceDatabaseValidate(ptr) ChpeAutoRtlTraceDatabaseValidate
1241 stdcall -version=0x600+ RtlTryAcquirePebLock() ChpeStubRtlTryAcquirePebLock
1245 stdcall RtlUnhandledExceptionFilter2(ptr long) ChpeAutoRtlUnhandledExceptionFilter2
1246 stdcall RtlUnhandledExceptionFilter(ptr) ChpeAutoRtlUnhandledExceptionFilter
1247 stdcall RtlUnicodeStringToAnsiSize(ptr) ChpeAutoRtlUnicodeStringToAnsiSize
1249 stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUnicodeStringToAnsiString
1250 stdcall RtlUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToCountedOemString
1251 stdcall RtlUnicodeStringToInteger(ptr long ptr) ChpeAutoRtlUnicodeStringToInteger
1252 stdcall RtlUnicodeStringToOemSize(ptr) ChpeAutoRtlUnicodeStringToOemSize
1253 stdcall RtlUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUnicodeStringToOemString
1254 stdcall RtlUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUnicodeToCustomCPN
1255 stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToMultiByteN
1256 stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ChpeAutoRtlUnicodeToMultiByteSize
1257 stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUnicodeToOemN
1258 stdcall RtlUniform(ptr) ChpeAutoRtlUniform
1259 stdcall RtlUnlockBootStatusData(ptr) ChpeAutoRtlUnlockBootStatusData
1260 stdcall -version=0x600+ RtlUnlockCurrentThread() ChpeStubRtlUnlockCurrentThread
1261 stdcall RtlUnlockHeap(long) ChpeAutoRtlUnlockHeap
1262 stdcall -version=0x600+ RtlUnlockMemoryBlockLookaside(long) ChpeStubRtlUnlockMemoryBlockLookaside
1263 stdcall RtlUnlockMemoryStreamRegion(ptr int64 int64 long) ChpeAutoRtlUnlockMemoryStreamRegion
1264 stdcall -version=0x600+ RtlUnlockMemoryZone(long) ChpeStubRtlUnlockMemoryZone
1265 stdcall -version=0x600+ RtlUnlockModuleSection(long) ChpeStubRtlUnlockModuleSection
1268 stdcall RtlUnregisterCfgTargetRange(ptr) ChpeAutoRtlUnregisterCfgTargetRange
1269 stdcall RtlUpcaseUnicodeChar(long) ChpeAutoRtlUpcaseUnicodeChar
1270 stdcall RtlUpcaseUnicodeString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeString
1271 stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToAnsiString
1272 stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToCountedOemString
1273 stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ChpeAutoRtlUpcaseUnicodeStringToOemString
1274 stdcall RtlUpcaseUnicodeToCustomCPN(ptr ptr long ptr wstr long) ChpeAutoRtlUpcaseUnicodeToCustomCPN
1275 stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToMultiByteN
1276 stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ChpeAutoRtlUpcaseUnicodeToOemN
1277 stdcall -version=0x600+ RtlUpdateClonedCriticalSection(long) ChpeStubRtlUpdateClonedCriticalSection
1278 stdcall -version=0x600+ RtlUpdateClonedSRWLock(ptr long) ChpeStubRtlUpdateClonedSRWLock
1279 stdcall RtlUpdateTimer(ptr ptr long long) ChpeAutoRtlUpdateTimer
1280 stdcall RtlUpperChar(long) ChpeAutoRtlUpperChar
1281 stdcall RtlUpperString(ptr ptr) ChpeAutoRtlUpperString
1283 stdcall RtlValidAcl(ptr) ChpeAutoRtlValidAcl
1284 stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ChpeAutoRtlValidRelativeSecurityDescriptor
1285 stdcall RtlValidSecurityDescriptor(ptr) ChpeAutoRtlValidSecurityDescriptor
1286 stdcall RtlValidSid(ptr) ChpeAutoRtlValidSid
1287 stdcall RtlValidateHeap(long long ptr) ChpeAutoRtlValidateHeap
1288 stdcall RtlValidateProcessHeaps() ChpeAutoRtlValidateProcessHeaps
1289 stdcall RtlValidateUnicodeString(long ptr) ChpeAutoRtlValidateUnicodeString
1290 stdcall RtlVerifyVersionInfo(ptr long double) ChpeAutoRtlVerifyVersionInfo
1297 stdcall RtlWalkFrameChain(ptr long long) ChpeAutoRtlWalkFrameChain
1298 stdcall RtlWeaklyEnumerateEntryHashTable(ptr ptr) ChpeAutoRtlWeaklyEnumerateEntryHashTable
1299 stdcall RtlWalkHeap(long ptr) ChpeAutoRtlWalkHeap
1300 stdcall -version=0x600+ RtlWerpReportException(long long ptr long long ptr) ChpeStubRtlWerpReportException
1301 stdcall -version=0x600+ RtlWow64CallFunction64() ChpeStubRtlWow64CallFunction64
1302 stdcall RtlWow64EnableFsRedirection(long) ChpeAutoRtlWow64EnableFsRedirection
1303 stdcall RtlWow64EnableFsRedirectionEx(long ptr) ChpeAutoRtlWow64EnableFsRedirectionEx
1304 stdcall -version=0x600+ RtlOpenCrossProcessEmulatorWorkConnection(ptr ptr ptr) ChpeAutoRtlOpenCrossProcessEmulatorWorkConnection
1305 stdcall -version=0x600+ RtlWow64GetCpuAreaInfo(ptr long ptr) ChpeAutoRtlWow64GetCpuAreaInfo
1306 stdcall -version=0x600+ RtlWow64GetCurrentMachine() ChpeAutoRtlWow64GetCurrentMachine
1307 stdcall -version=0x600+ RtlWow64GetProcessMachines(ptr ptr ptr) ChpeAutoRtlWow64GetProcessMachines
1308 stdcall -version=0x600+ RtlWow64GetThreadContext(ptr ptr) ChpeAutoRtlWow64GetThreadContext
1310 stdcall -version=0x600+ RtlWow64LogMessageInEventLogger(long long long) ChpeStubRtlWow64LogMessageInEventLogger
1311 stdcall -version=0x600+ RtlWow64PopCrossProcessWorkFromFreeList(ptr) ChpeAutoRtlWow64PopCrossProcessWorkFromFreeList
1312 stdcall -version=0x600+ RtlWow64PushCrossProcessWorkOntoWorkList(ptr ptr ptr) ChpeAutoRtlWow64PushCrossProcessWorkOntoWorkList
1313 stdcall -version=0x600+ RtlWow64RequestCrossProcessHeavyFlush(ptr) ChpeAutoRtlWow64RequestCrossProcessHeavyFlush
1314 stdcall -version=0x600+ RtlWow64SetThreadContext(ptr ptr) ChpeAutoRtlWow64SetThreadContext
1315 stdcall -version=0x600+ RtlWow64SuspendThread(long long) ChpeStubRtlWow64SuspendThread
1316 stdcall RtlWriteMemoryStream(ptr ptr long ptr) ChpeAutoRtlWriteMemoryStream
1317 stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ChpeAutoRtlWriteRegistryValue
1318 stdcall RtlZeroHeap(ptr long) ChpeAutoRtlZeroHeap
1320 stdcall RtlZombifyActivationContext(ptr) ChpeAutoRtlZombifyActivationContext
1322 stdcall -version=0x600+ RtlpCheckDynamicTimeZoneInformation(ptr long) ChpeStubRtlpCheckDynamicTimeZoneInformation
1323 stdcall -version=0x600+ RtlpCleanupRegistryKeys() ChpeStubRtlpCleanupRegistryKeys
1324 stdcall -version=0x600+ RtlpConvertCultureNamesToLCIDs(wstr ptr) ChpeStubRtlpConvertCultureNamesToLCIDs
1325 stdcall -version=0x600+ RtlpConvertLCIDsToCultureNames(wstr ptr) ChpeStubRtlpConvertLCIDsToCultureNames
1326 stdcall -version=0x600+ RtlpCreateProcessRegistryInfo(ptr) ChpeStubRtlpCreateProcessRegistryInfo
1327 stdcall RtlpEnsureBufferSize(long ptr long) ChpeAutoRtlpEnsureBufferSize
1328 stdcall -version=0x600+ RtlpGetLCIDFromLangInfoNode(long long ptr) ChpeStubRtlpGetLCIDFromLangInfoNode
1329 stdcall -version=0x600+ RtlpGetNameFromLangInfoNode(long long long) ChpeStubRtlpGetNameFromLangInfoNode
1330 stdcall -version=0x600+ RtlpGetSystemDefaultUILanguage(ptr long) ChpeStubRtlpGetSystemDefaultUILanguage
1331 stdcall -version=0x600+ RtlpGetUserOrMachineUILanguage4NLS(long long ptr) ChpeStubRtlpGetUserOrMachineUILanguage4NLS
1332 stdcall -version=0x600+ RtlpInitializeLangRegistryInfo(ptr) ChpeStubRtlpInitializeLangRegistryInfo
1333 stdcall -version=0x600+ RtlpIsQualifiedLanguage(long ptr long) ChpeStubRtlpIsQualifiedLanguage
1334 stdcall -version=0x600+ RtlpLoadMachineUIByPolicy(ptr long ptr) ChpeStubRtlpLoadMachineUIByPolicy
1335 stdcall -version=0x600+ RtlpLoadUserUIByPolicy(ptr long ptr) ChpeStubRtlpLoadUserUIByPolicy
1336 stdcall -version=0x600+ RtlpMuiFreeLangRegistryInfo(long) ChpeStubRtlpMuiFreeLangRegistryInfo
1337 stdcall -version=0x600+ RtlpMuiRegCreateRegistryInfo() ChpeStubRtlpMuiRegCreateRegistryInfo
1338 stdcall -version=0x600+ RtlpMuiRegFreeRegistryInfo(long long) ChpeStubRtlpMuiRegFreeRegistryInfo
1339 stdcall -version=0x600+ RtlpMuiRegLoadRegistryInfo(long long) ChpeStubRtlpMuiRegLoadRegistryInfo
1340 stdcall RtlpNotOwnerCriticalSection(ptr) ChpeAutoRtlpNotOwnerCriticalSection
1341 stdcall RtlpNtCreateKey(ptr long ptr long ptr ptr) ChpeAutoRtlpNtCreateKey
1342 stdcall RtlpNtEnumerateSubKey(ptr ptr long long) ChpeAutoRtlpNtEnumerateSubKey
1343 stdcall RtlpNtMakeTemporaryKey(ptr) ChpeAutoRtlpNtMakeTemporaryKey
1344 stdcall RtlpNtOpenKey(ptr long ptr long) ChpeAutoRtlpNtOpenKey
1345 stdcall RtlpNtQueryValueKey(ptr ptr ptr ptr long) ChpeAutoRtlpNtQueryValueKey
1346 stdcall RtlpNtSetValueKey(ptr long ptr long) ChpeAutoRtlpNtSetValueKey
1347 stdcall -version=0x600+ RtlpQueryDefaultUILanguage(ptr long) ChpeStubRtlpQueryDefaultUILanguage
1348 stdcall -version=0x600+ RtlpQueryProcessDebugInformationFromWow64(long ptr) ChpeStubRtlpQueryProcessDebugInformationFromWow64
1349 stdcall -version=0x600+ RtlpRefreshCachedUILanguage(wstr long) ChpeStubRtlpRefreshCachedUILanguage
1350 stdcall -version=0x600+ RtlpSetInstallLanguage(long ptr) ChpeStubRtlpSetInstallLanguage
1351 stdcall -version=0x600+ RtlpSetPreferredUILanguages(long ptr ptr) ChpeStubRtlpSetPreferredUILanguages
1352 stdcall RtlpUnWaitCriticalSection(ptr) ChpeAutoRtlpUnWaitCriticalSection
1353 stdcall -version=0x600+ RtlpVerifyAndCommitUILanguageSettings(long) ChpeStubRtlpVerifyAndCommitUILanguageSettings
1354 stdcall RtlpWaitForCriticalSection(ptr) ChpeAutoRtlpWaitForCriticalSection
1355 stdcall RtlxAnsiStringToUnicodeSize(ptr) ChpeAutoRtlxAnsiStringToUnicodeSize
1356 stdcall RtlxOemStringToUnicodeSize(ptr) ChpeAutoRtlxOemStringToUnicodeSize
1357 stdcall RtlxUnicodeStringToAnsiSize(ptr) ChpeAutoRtlxUnicodeStringToAnsiSize
1358 stdcall RtlxUnicodeStringToOemSize(ptr) ChpeAutoRtlxUnicodeStringToOemSize
1359 stdcall -version=0x600+ ShipAssert(long long) ChpeStubShipAssert
1360 stdcall -version=0x600+ ShipAssertGetBufferInfo(ptr ptr) ChpeStubShipAssertGetBufferInfo
1361 stdcall -version=0x600+ ShipAssertMsgA(long long) ChpeStubShipAssertMsgA
1362 stdcall -version=0x600+ ShipAssertMsgW(long long) ChpeStubShipAssertMsgW
1363 stdcall -version=0x600+ TpAllocAlpcCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletion
1364 stdcall -version=0x600+ TpAllocAlpcCompletionEx(ptr ptr ptr ptr ptr) ChpeAutoTpAllocAlpcCompletionEx
1365 stdcall -version=0x600+ TpAllocCleanupGroup(ptr) ChpeAutoTpAllocCleanupGroup
1366 stdcall -version=0x600+ TpAllocIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoTpAllocIoCompletion
1367 stdcall -version=0x600+ TpAllocPool(ptr ptr) ChpeAutoTpAllocPool
1368 stdcall -version=0x600+ TpAllocTimer(ptr ptr ptr ptr) ChpeAutoTpAllocTimer
1369 stdcall -version=0x600+ TpAllocWait(ptr ptr ptr ptr) ChpeAutoTpAllocWait
1370 stdcall -version=0x600+ TpAllocWork(ptr ptr ptr ptr) ChpeAutoTpAllocWork
1371 stdcall -version=0x600+ TpAlpcRegisterCompletionList(ptr) ChpeAutoTpAlpcRegisterCompletionList
1372 stdcall -version=0x600+ TpAlpcUnregisterCompletionList(ptr) ChpeAutoTpAlpcUnregisterCompletionList
1374 stdcall -version=0x600+ TpCallbackMayRunLong(ptr) ChpeAutoTpCallbackMayRunLong
1377 stdcall -version=0x600+ TpCallbackSendAlpcMessageOnCompletion(ptr ptr long ptr) ChpeAutoTpCallbackSendAlpcMessageOnCompletion
1378 stdcall -version=0x600+ TpCallbackSendPendingAlpcMessage(ptr) ChpeAutoTpCallbackSendPendingAlpcMessage
1382 stdcall -version=0x600+ TpCaptureCaller(long) ChpeStubTpCaptureCaller
1383 stdcall -version=0x600+ TpCheckTerminateWorker(ptr) ChpeStubTpCheckTerminateWorker
1384 stdcall -version=0x600+ TpDbgDumpHeapUsage(long ptr long) ChpeStubTpDbgDumpHeapUsage
1385 stdcall -version=0x600+ TpDbgSetLogRoutine() ChpeStubTpDbgSetLogRoutine
1389 stdcall -version=0x601+ TpQueryPoolStackInformation(ptr ptr) ChpeAutoTpQueryPoolStackInformation
1390 stdcall -version=0x600+ TpReleaseAlpcCompletion(ptr) ChpeAutoTpReleaseAlpcCompletion
1400 stdcall -version=0x601+ TpSetPoolStackInformation(ptr ptr) ChpeAutoTpSetPoolStackInformation
1405 stdcall -version=0x600+ TpSimpleTryPost(ptr ptr ptr) ChpeAutoTpSimpleTryPost
1407 stdcall -version=0x600+ TpWaitForAlpcCompletion(ptr) ChpeAutoTpWaitForAlpcCompletion
1413 stdcall -version=0x600+ WerCheckEventEscalation(long ptr) ChpeStubWerCheckEventEscalation
1414 stdcall -version=0x600+ WerReportSQMEvent(long long long) ChpeStubWerReportSQMEvent
1415 stdcall -version=0x600+ WerReportWatsonEvent(long long long long) ChpeStubWerReportWatsonEvent
1416 stdcall -version=0x600+ WinSqmAddToStream(ptr long long long) ChpeStubWinSqmAddToStream
1417 stdcall -version=0x600+ WinSqmAddToStreamEx(ptr long long ptr long) ChpeStubWinSqmAddToStreamEx
1418 stdcall -version=0x600+ WinSqmEndSession(ptr) ChpeStubWinSqmEndSession
1419 stdcall -version=0x600+ WinSqmEventEnabled(long ptr) ChpeStubWinSqmEventEnabled
1420 stdcall -version=0x600+ WinSqmEventWrite(long long long) ChpeStubWinSqmEventWrite
1421 stdcall -version=0x600+ WinSqmIsOptedIn() ChpeStubWinSqmIsOptedIn
1422 stdcall -version=0x600+ WinSqmSetDWORD(ptr long long) ChpeStubWinSqmSetDWORD
1423 stdcall -version=0x600+ WinSqmSetString(ptr long ptr) ChpeStubWinSqmSetString
1424 stdcall -version=0x600+ WinSqmStartSession(ptr) ChpeStubWinSqmStartSession
1425 stdcall ZwAcceptConnectPort(ptr long ptr long long ptr) ChpeAutoZwAcceptConnectPort
1426 stdcall ZwAccessCheck(ptr long long ptr ptr ptr ptr ptr) ChpeAutoZwAccessCheck
1427 stdcall ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckAndAuditAlarm
1428 stdcall ZwAccessCheckByType(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByType
1429 stdcall ZwAccessCheckByTypeAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeAndAuditAlarm
1430 stdcall ZwAccessCheckByTypeResultList(ptr ptr ptr long ptr long ptr ptr long ptr ptr) ChpeAutoZwAccessCheckByTypeResultList
1431 stdcall ZwAccessCheckByTypeResultListAndAuditAlarm(ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarm
1432 stdcall ZwAccessCheckByTypeResultListAndAuditAlarmByHandle(ptr ptr ptr ptr ptr ptr ptr long long long ptr long ptr long ptr ptr ptr) ChpeAutoZwAccessCheckByTypeResultListAndAuditAlarmByHandle
1433 stub -version=0x600+ ZwAcquireCMFViewOwnership
1434 stdcall ZwAddAtom(ptr long ptr) ChpeAutoZwAddAtom
1435 stdcall ZwAddBootEntry(ptr long) ChpeAutoZwAddBootEntry
1436 stdcall ZwAddDriverEntry(ptr long) ChpeAutoZwAddDriverEntry
1437 stdcall ZwAdjustGroupsToken(long long long long long long) ChpeAutoZwAdjustGroupsToken
1438 stdcall ZwAdjustPrivilegesToken(long long long long long long) ChpeAutoZwAdjustPrivilegesToken
1439 stdcall ZwAlertResumeThread(long ptr) ChpeAutoZwAlertResumeThread
1440 stdcall ZwAlertThread(long) ChpeAutoZwAlertThread
1441 stdcall ZwAlertThreadByThreadId(long) ChpeAutoZwAlertThreadByThreadId
1442 stdcall ZwAllocateLocallyUniqueId(ptr) ChpeAutoZwAllocateLocallyUniqueId
1443 stdcall ZwAllocateUserPhysicalPages(ptr ptr ptr) ChpeAutoZwAllocateUserPhysicalPages
1444 stdcall ZwAllocateUuids(ptr ptr ptr ptr) ChpeAutoZwAllocateUuids
1445 stdcall ZwAllocateVirtualMemory(long ptr ptr ptr long long) ChpeAutoZwAllocateVirtualMemory
1446 stdcall ZwAllocateVirtualMemoryEx(long ptr ptr long long ptr long) ChpeAutoZwAllocateVirtualMemoryEx
1447 stdcall -version=0x600+ ZwAlpcAcceptConnectPort(ptr ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwAlpcAcceptConnectPort
1448 stdcall -version=0x600+ ZwAlpcCancelMessage(ptr long ptr) ChpeAutoZwAlpcCancelMessage
1449 stdcall -version=0x600+ ZwAlpcConnectPort(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPort
1450 stdcall -version=0x602+ ZwAlpcConnectPortEx(ptr ptr ptr ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcConnectPortEx
1451 stdcall -version=0x600+ ZwAlpcCreatePort(ptr ptr ptr) ChpeAutoZwAlpcCreatePort
1452 stdcall -version=0x600+ ZwAlpcCreatePortSection(ptr long ptr long ptr ptr) ChpeAutoZwAlpcCreatePortSection
1453 stdcall -version=0x600+ ZwAlpcCreateResourceReserve(ptr long long ptr) ChpeAutoZwAlpcCreateResourceReserve
1454 stdcall -version=0x600+ ZwAlpcCreateSectionView(ptr long ptr) ChpeAutoZwAlpcCreateSectionView
1455 stdcall -version=0x600+ ZwAlpcCreateSecurityContext(ptr long ptr) ChpeAutoZwAlpcCreateSecurityContext
1456 stdcall -version=0x600+ ZwAlpcDeletePortSection(ptr long ptr) ChpeAutoZwAlpcDeletePortSection
1457 stdcall -version=0x600+ ZwAlpcDeleteResourceReserve(ptr long long) ChpeAutoZwAlpcDeleteResourceReserve
1458 stdcall -version=0x600+ ZwAlpcDeleteSectionView(ptr long ptr) ChpeAutoZwAlpcDeleteSectionView
1459 stdcall -version=0x600+ ZwAlpcDeleteSecurityContext(ptr long ptr) ChpeAutoZwAlpcDeleteSecurityContext
1460 stdcall -version=0x600+ ZwAlpcDisconnectPort(ptr long) ChpeAutoZwAlpcDisconnectPort
1461 stdcall -version=0x600+ ZwAlpcImpersonateClientOfPort(ptr ptr ptr) ChpeAutoZwAlpcImpersonateClientOfPort
1462 stdcall -version=0xA00+ ZwAlpcImpersonateClientContainerOfPort(ptr ptr long) ChpeAutoZwAlpcImpersonateClientContainerOfPort
1463 stdcall -version=0x600+ ZwAlpcOpenSenderProcess(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderProcess
1464 stdcall -version=0x600+ ZwAlpcOpenSenderThread(ptr ptr ptr long long ptr) ChpeAutoZwAlpcOpenSenderThread
1465 stdcall -version=0x600+ ZwAlpcQueryInformation(ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformation
1466 stdcall -version=0x600+ ZwAlpcQueryInformationMessage(ptr ptr long ptr long ptr) ChpeAutoZwAlpcQueryInformationMessage
1467 stdcall -version=0x600+ ZwAlpcRevokeSecurityContext(ptr long ptr) ChpeAutoZwAlpcRevokeSecurityContext
1468 stdcall -version=0x600+ ZwAlpcSendWaitReceivePort(ptr long ptr ptr ptr ptr ptr ptr) ChpeAutoZwAlpcSendWaitReceivePort
1469 stdcall -version=0x600+ ZwAlpcSetInformation(ptr long ptr long) ChpeAutoZwAlpcSetInformation
1470 stdcall ZwApphelpCacheControl(long ptr) ChpeAutoZwApphelpCacheControl
1471 stdcall ZwAreMappedFilesTheSame(ptr ptr) ChpeAutoZwAreMappedFilesTheSame
1472 stdcall ZwAssignProcessToJobObject(long long) ChpeAutoZwAssignProcessToJobObject
1473 stdcall ZwCallbackReturn(ptr long long) ChpeAutoZwCallbackReturn
1474 stdcall ZwCancelDeviceWakeupRequest(ptr) ChpeAutoZwCancelDeviceWakeupRequest
1475 stdcall ZwCancelIoFile(long ptr) ChpeAutoZwCancelIoFile
1476 stdcall -version=0x600+ ZwCancelIoFileEx(ptr ptr ptr) ChpeAutoZwCancelIoFileEx
1477 stdcall -version=0x600+ ZwCancelSynchronousIoFile(ptr ptr ptr) ChpeAutoZwCancelSynchronousIoFile
1478 stdcall ZwCancelTimer(long ptr) ChpeAutoZwCancelTimer
1479 stdcall ZwClearEvent(long) ChpeAutoZwClearEvent
1480 stdcall ZwClose(long) ChpeAutoZwClose
1481 stdcall ZwCloseObjectAuditAlarm(ptr ptr long) ChpeAutoZwCloseObjectAuditAlarm
1482 stdcall -version=0x600+ ZwCommitComplete(ptr ptr) ChpeStubZwCommitComplete
1483 stdcall -version=0x600+ ZwCommitEnlistment(ptr ptr) ChpeStubZwCommitEnlistment
1484 stdcall -version=0x600+ ZwCommitTransaction(ptr long) ChpeStubZwCommitTransaction
1485 stdcall ZwCompactKeys(long ptr) ChpeAutoZwCompactKeys
1486 stdcall ZwCompareTokens(ptr ptr ptr) ChpeAutoZwCompareTokens
1487 stdcall ZwCompleteConnectPort(ptr) ChpeAutoZwCompleteConnectPort
1488 stdcall ZwCompressKey(ptr) ChpeAutoZwCompressKey
1489 stdcall ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwConnectPort
1490 stdcall ZwContinue(ptr long) ChpeAutoZwContinue
1491 stdcall ZwCreateDebugObject(ptr long ptr long) ChpeAutoZwCreateDebugObject
1492 stdcall ZwCreateDirectoryObject(long long long) ChpeAutoZwCreateDirectoryObject
1493 stdcall -version=0x600+ ZwCreateEnlistment(ptr long ptr ptr ptr long long ptr) ChpeStubZwCreateEnlistment
1494 stdcall ZwCreateEvent(long long long long long) ChpeAutoZwCreateEvent
1495 stdcall ZwCreateEventPair(ptr long ptr) ChpeAutoZwCreateEventPair
1496 stdcall ZwCreateFile(ptr long ptr ptr long long long ptr long long ptr) ChpeAutoZwCreateFile
1497 stdcall ZwCreateIoCompletion(ptr long ptr long) ChpeAutoZwCreateIoCompletion
1498 stdcall -version=0x602+ ZwCreateWaitCompletionPacket(ptr long ptr) ChpeAutoZwCreateWaitCompletionPacket
1499 stdcall -version=0x602+ ZwAssociateWaitCompletionPacket(ptr ptr ptr ptr ptr long ptr ptr) ChpeAutoZwAssociateWaitCompletionPacket
1500 stdcall -version=0x602+ ZwCancelWaitCompletionPacket(ptr long) ChpeAutoZwCancelWaitCompletionPacket
1501 stdcall ZwCreateJobObject(ptr long ptr) ChpeAutoZwCreateJobObject
1502 stdcall ZwCreateJobSet(long ptr long) ChpeAutoZwCreateJobSet
1503 stdcall ZwCreateKey(ptr long ptr long ptr long long) ChpeAutoZwCreateKey
1504 stdcall -version=0x600+ ZwCreateKeyTransacted(ptr long ptr long ptr long ptr ptr) ChpeStubZwCreateKeyTransacted
1505 stdcall ZwCreateKeyedEvent(ptr long ptr long) ChpeAutoZwCreateKeyedEvent
1506 stdcall ZwCreateMailslotFile(long long long long long long long long) ChpeAutoZwCreateMailslotFile
1507 stdcall ZwCreateMutant(ptr long ptr long) ChpeAutoZwCreateMutant
1508 stdcall ZwCreateNamedPipeFile(ptr long ptr ptr long long long long long long long long long ptr) ChpeAutoZwCreateNamedPipeFile
1509 stdcall ZwCreatePagingFile(ptr ptr ptr long) ChpeAutoZwCreatePagingFile
1510 stdcall ZwCreatePort(ptr ptr long long long) ChpeAutoZwCreatePort
1511 stdcall ZwCreateProcess(ptr long ptr ptr long ptr ptr ptr) ChpeAutoZwCreateProcess
1512 stdcall ZwCreateProcessEx(ptr long ptr ptr long ptr ptr ptr long) ChpeAutoZwCreateProcessEx
1513 stdcall ZwCreateProfile(ptr ptr ptr long long ptr long long long) ChpeAutoZwCreateProfile
1514 stdcall -version=0x600+ ZwCreateResourceManager(ptr long ptr ptr ptr long wstr) ChpeStubZwCreateResourceManager
1515 stdcall ZwCreateSection(ptr long ptr ptr long long long) ChpeAutoZwCreateSection
1516 stdcall ZwCreateSemaphore(ptr long ptr long long) ChpeAutoZwCreateSemaphore
1517 stdcall ZwCreateSymbolicLinkObject(ptr long ptr ptr) ChpeAutoZwCreateSymbolicLinkObject
1518 stdcall ZwCreateThread(ptr long ptr ptr ptr ptr ptr long) ChpeAutoZwCreateThread
1519 stdcall -version=0x600+ ZwCreateThreadEx(ptr long ptr ptr ptr ptr long long long long ptr) ChpeStubZwCreateThreadEx
1520 stdcall ZwCreateTimer(ptr long ptr long) ChpeAutoZwCreateTimer
1521 stdcall ZwCreateToken(ptr long ptr long ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwCreateToken
1522 stdcall -version=0x600+ ZwCreateTransaction(ptr long ptr ptr ptr long long long ptr wstr) ChpeStubZwCreateTransaction
1523 stdcall -version=0x600+ ZwCreateTransactionManager(ptr long ptr ptr long long) ChpeStubZwCreateTransactionManager
1524 stdcall -version=0x600+ ZwCreateUserProcess(ptr ptr long long ptr ptr long long ptr ptr ptr) ChpeAutoZwCreateUserProcess
1525 stdcall ZwCreateWaitablePort(ptr ptr long long long) ChpeAutoZwCreateWaitablePort
1526 stdcall -version=0x602+ ZwCreateWnfStateName(ptr long long long ptr long ptr) ChpeAutoZwCreateWnfStateName
1527 stdcall -version=0x600+ ZwCreateWorkerFactory(ptr long ptr ptr ptr ptr ptr long long long) ChpeStubZwCreateWorkerFactory
1528 stdcall ZwDebugActiveProcess(ptr ptr) ChpeAutoZwDebugActiveProcess
1529 stdcall ZwDebugContinue(ptr ptr long) ChpeAutoZwDebugContinue
1530 stdcall ZwDelayExecution(long ptr) ChpeAutoZwDelayExecution
1531 stdcall ZwDeleteAtom(long) ChpeAutoZwDeleteAtom
1532 stdcall ZwDeleteBootEntry(long) ChpeAutoZwDeleteBootEntry
1533 stdcall ZwDeleteDriverEntry(long) ChpeAutoZwDeleteDriverEntry
1534 stdcall ZwDeleteFile(ptr) ChpeAutoZwDeleteFile
1535 stdcall ZwDeleteKey(long) ChpeAutoZwDeleteKey
1536 stdcall ZwDeleteObjectAuditAlarm(ptr ptr long) ChpeAutoZwDeleteObjectAuditAlarm
1537 stdcall -version=0x600+ ZwDeletePrivateNamespace(ptr) ChpeStubZwDeletePrivateNamespace
1538 stdcall ZwDeleteValueKey(long ptr) ChpeAutoZwDeleteValueKey
1539 stdcall -version=0x602+ ZwDeleteWnfStateData(ptr ptr) ChpeAutoZwDeleteWnfStateData
1540 stdcall -version=0x602+ ZwDeleteWnfStateName(ptr) ChpeAutoZwDeleteWnfStateName
1541 stdcall ZwDeviceIoControlFile(long long long long long long long long long long) ChpeAutoZwDeviceIoControlFile
1542 stdcall ZwDisplayString(ptr) ChpeAutoZwDisplayString
1543 stdcall ZwDuplicateObject(long long long ptr long long long) ChpeAutoZwDuplicateObject
1544 stdcall ZwDuplicateToken(long long long long long long) ChpeAutoZwDuplicateToken
1545 stdcall ZwEnumerateBootEntries(ptr ptr) ChpeAutoZwEnumerateBootEntries
1546 stdcall ZwEnumerateDriverEntries(ptr ptr) ChpeAutoZwEnumerateDriverEntries
1547 stdcall ZwEnumerateKey(long long long ptr long ptr) ChpeAutoZwEnumerateKey
1548 stdcall ZwEnumerateSystemEnvironmentValuesEx(long ptr long) ChpeAutoZwEnumerateSystemEnvironmentValuesEx
1549 stdcall -version=0x600+ ZwEnumerateTransactionObject(ptr long ptr long ptr) ChpeStubZwEnumerateTransactionObject
1550 stdcall ZwEnumerateValueKey(long long long ptr long ptr) ChpeAutoZwEnumerateValueKey
1551 stdcall ZwExtendSection(ptr ptr) ChpeAutoZwExtendSection
1552 stdcall ZwFilterToken(ptr long ptr ptr ptr ptr) ChpeAutoZwFilterToken
1553 stdcall ZwFindAtom(ptr long ptr) ChpeAutoZwFindAtom
1554 stdcall ZwFlushBuffersFile(long ptr) ChpeAutoZwFlushBuffersFile
1555 stdcall ZwFlushBuffersFileEx(long long ptr long ptr) ChpeAutoZwFlushBuffersFileEx
1556 stdcall -version=0x600+ ZwFlushInstallUILanguage(long long) ChpeStubZwFlushInstallUILanguage
1557 stdcall ZwFlushInstructionCache(long ptr long) ChpeAutoZwFlushInstructionCache
1558 stdcall ZwFlushKey(long) ChpeAutoZwFlushKey
1559 stdcall -version=0x600+ ZwFlushProcessWriteBuffers() ChpeAutoZwFlushProcessWriteBuffers
1560 stdcall ZwFlushVirtualMemory(ptr ptr ptr ptr) ChpeAutoZwFlushVirtualMemory
1561 stdcall ZwFlushWriteBuffer() ChpeAutoZwFlushWriteBuffer
1562 stdcall ZwFreeUserPhysicalPages(ptr ptr ptr) ChpeAutoZwFreeUserPhysicalPages
1563 stdcall ZwFreeVirtualMemory(long ptr ptr long) ChpeAutoZwFreeVirtualMemory
1564 stdcall -version=0x600+ ZwFreezeRegistry(long) ChpeStubZwFreezeRegistry
1565 stdcall -version=0x600+ ZwFreezeTransactions(ptr ptr) ChpeStubZwFreezeTransactions
1566 stdcall ZwFsControlFile(long long long long long long long long long long) ChpeAutoZwFsControlFile
1567 stdcall ZwGetContextThread(long ptr) ChpeAutoZwGetContextThread
1568 stdcall ZwGetCurrentProcessorNumber() ChpeAutoZwGetCurrentProcessorNumber
1569 stdcall -version=0xA00+ ZwGetCurrentProcessorNumberEx(ptr) ChpeAutoZwGetCurrentProcessorNumberEx
1570 stdcall ZwGetDevicePowerState(ptr ptr) ChpeAutoZwGetDevicePowerState
1571 stdcall -version=0x600+ ZwGetMUIRegistryInfo(long ptr ptr) ChpeStubZwGetMUIRegistryInfo
1572 stdcall -version=0x600+ ZwGetNextProcess(ptr long long long ptr) ChpeStubZwGetNextProcess
1573 stdcall -version=0x600+ ZwGetNextThread(ptr ptr long long long ptr) ChpeAutoZwGetNextThread
1574 stdcall -version=0x600+ ZwGetNlsSectionPtr(long long ptr ptr ptr) ChpeAutoZwGetNlsSectionPtr
1575 stdcall -version=0x600+ ZwGetNotificationResourceManager(ptr ptr long ptr ptr long ptr) ChpeStubZwGetNotificationResourceManager
1576 stdcall ZwGetPlugPlayEvent(long long ptr long) ChpeAutoZwGetPlugPlayEvent
1577 stdcall ZwGetWriteWatch(long long ptr long ptr ptr ptr) ChpeAutoZwGetWriteWatch
1578 stdcall ZwImpersonateAnonymousToken(ptr) ChpeAutoZwImpersonateAnonymousToken
1579 stdcall ZwImpersonateClientOfPort(ptr ptr) ChpeAutoZwImpersonateClientOfPort
1580 stdcall ZwImpersonateThread(ptr ptr ptr) ChpeAutoZwImpersonateThread
1581 stdcall -version=0x600+ ZwInitializeNlsFiles(ptr ptr ptr ptr) ChpeStubZwInitializeNlsFiles
1582 stdcall ZwInitializeRegistry(long) ChpeAutoZwInitializeRegistry
1583 stdcall ZwInitiatePowerAction(long long long long) ChpeAutoZwInitiatePowerAction
1584 stdcall ZwIsProcessInJob(long long) ChpeAutoZwIsProcessInJob
1585 stdcall ZwIsSystemResumeAutomatic() ChpeAutoZwIsSystemResumeAutomatic
1586 stdcall -version=0x600+ ZwIsUILanguageComitted() ChpeStubZwIsUILanguageComitted
1587 stdcall ZwListenPort(ptr ptr) ChpeAutoZwListenPort
1588 stdcall ZwLoadDriver(ptr) ChpeAutoZwLoadDriver
1589 stdcall ZwLoadKey2(ptr ptr long) ChpeAutoZwLoadKey2
1590 stdcall ZwLoadKey(ptr ptr) ChpeAutoZwLoadKey
1591 stdcall ZwLoadKeyEx(ptr ptr long ptr) ChpeAutoZwLoadKeyEx
1592 stdcall ZwLockFile(long long ptr ptr ptr ptr ptr ptr long long) ChpeAutoZwLockFile
1593 stdcall ZwLockProductActivationKeys(ptr ptr) ChpeAutoZwLockProductActivationKeys
1594 stdcall ZwLockRegistryKey(ptr) ChpeAutoZwLockRegistryKey
1595 stdcall ZwLockVirtualMemory(long ptr ptr long) ChpeAutoZwLockVirtualMemory
1596 stdcall ZwMakePermanentObject(ptr) ChpeAutoZwMakePermanentObject
1597 stdcall ZwMakeTemporaryObject(long) ChpeAutoZwMakeTemporaryObject
1598 stdcall -version=0x600+ ZwMapCMFModule(long long ptr ptr ptr) ChpeStubZwMapCMFModule
1599 stdcall ZwMapUserPhysicalPages(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPages
1600 stdcall ZwMapUserPhysicalPagesScatter(ptr ptr ptr) ChpeAutoZwMapUserPhysicalPagesScatter
1601 stdcall ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ChpeAutoZwMapViewOfSection
1602 stdcall ZwModifyBootEntry(ptr) ChpeAutoZwModifyBootEntry
1603 stdcall ZwModifyDriverEntry(ptr) ChpeAutoZwModifyDriverEntry
1604 stdcall ZwNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ChpeAutoZwNotifyChangeDirectoryFile
1605 stdcall ZwNotifyChangeDirectoryFileEx(long long ptr ptr ptr ptr long long long long) ChpeAutoZwNotifyChangeDirectoryFileEx
1606 stdcall ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeKey
1607 stdcall ZwNotifyChangeMultipleKeys(ptr long ptr ptr ptr ptr ptr long long ptr long long) ChpeAutoZwNotifyChangeMultipleKeys
1608 stdcall ZwOpenDirectoryObject(long long long) ChpeAutoZwOpenDirectoryObject
1609 stdcall -version=0x600+ ZwOpenEnlistment(ptr long ptr ptr ptr) ChpeStubZwOpenEnlistment
1610 stdcall ZwOpenEvent(long long long) ChpeAutoZwOpenEvent
1611 stdcall ZwOpenEventPair(ptr long ptr) ChpeAutoZwOpenEventPair
1612 stdcall ZwOpenFile(ptr long ptr ptr long long) ChpeAutoZwOpenFile
1613 stdcall ZwOpenIoCompletion(ptr long ptr) ChpeAutoZwOpenIoCompletion
1614 stdcall ZwOpenJobObject(ptr long ptr) ChpeAutoZwOpenJobObject
1615 stdcall ZwOpenKey(ptr long ptr) ChpeAutoZwOpenKey
1616 stdcall -version=0x600+ ZwOpenKeyTransacted(ptr long ptr ptr) ChpeStubZwOpenKeyTransacted
1617 stdcall ZwOpenKeyedEvent(ptr long ptr) ChpeAutoZwOpenKeyedEvent
1618 stdcall ZwOpenMutant(ptr long ptr) ChpeAutoZwOpenMutant
1619 stdcall ZwOpenObjectAuditAlarm(ptr ptr ptr ptr ptr ptr long long ptr long long ptr) ChpeAutoZwOpenObjectAuditAlarm
1620 stdcall -version=0x600+ ZwOpenPrivateNamespace(ptr long ptr ptr) ChpeStubZwOpenPrivateNamespace
1621 stdcall ZwOpenProcess(ptr long ptr ptr) ChpeAutoZwOpenProcess
1622 stdcall ZwOpenProcessToken(long long ptr) ChpeAutoZwOpenProcessToken
1623 stdcall ZwOpenProcessTokenEx(long long long ptr) ChpeAutoZwOpenProcessTokenEx
1624 stdcall -version=0x600+ ZwOpenResourceManager(ptr long ptr ptr ptr) ChpeStubZwOpenResourceManager
1625 stdcall ZwOpenSection(ptr long ptr) ChpeAutoZwOpenSection
1626 stdcall ZwOpenSemaphore(long long ptr) ChpeAutoZwOpenSemaphore
1627 stdcall -version=0x600+ ZwOpenSession(ptr long ptr) ChpeStubZwOpenSession
1628 stdcall ZwOpenSymbolicLinkObject(ptr long ptr) ChpeAutoZwOpenSymbolicLinkObject
1629 stdcall ZwOpenThread(ptr long ptr ptr) ChpeAutoZwOpenThread
1630 stdcall ZwOpenThreadToken(long long long ptr) ChpeAutoZwOpenThreadToken
1631 stdcall ZwOpenThreadTokenEx(long long long long ptr) ChpeAutoZwOpenThreadTokenEx
1632 stdcall ZwOpenTimer(ptr long ptr) ChpeAutoZwOpenTimer
1633 stdcall -version=0x600+ ZwOpenTransaction(ptr long ptr ptr ptr) ChpeStubZwOpenTransaction
1634 stdcall -version=0x600+ ZwOpenTransactionManager(ptr long ptr wstr ptr long) ChpeStubZwOpenTransactionManager
1635 stdcall ZwPlugPlayControl(ptr ptr long) ChpeAutoZwPlugPlayControl
1636 stdcall ZwPowerInformation(long ptr long ptr long) ChpeAutoZwPowerInformation
1637 stdcall -version=0x600+ ZwPrePrepareComplete(ptr ptr) ChpeStubZwPrePrepareComplete
1638 stdcall -version=0x600+ ZwPrePrepareEnlistment(ptr ptr) ChpeStubZwPrePrepareEnlistment
1639 stdcall -version=0x600+ ZwPrepareComplete(ptr ptr) ChpeStubZwPrepareComplete
1640 stdcall -version=0x600+ ZwPrepareEnlistment(ptr ptr) ChpeStubZwPrepareEnlistment
1641 stdcall ZwPrivilegeCheck(ptr ptr ptr) ChpeAutoZwPrivilegeCheck
1642 stdcall ZwPrivilegeObjectAuditAlarm(ptr ptr ptr long ptr long) ChpeAutoZwPrivilegeObjectAuditAlarm
1643 stdcall ZwPrivilegedServiceAuditAlarm(ptr ptr ptr ptr long) ChpeAutoZwPrivilegedServiceAuditAlarm
1644 stdcall -version=0x600+ ZwPropagationComplete(ptr long long ptr) ChpeStubZwPropagationComplete
1645 stdcall -version=0x600+ ZwPropagationFailed(ptr long long) ChpeStubZwPropagationFailed
1646 stdcall ZwProtectVirtualMemory(long ptr ptr long ptr) ChpeAutoZwProtectVirtualMemory
1647 stdcall ZwPulseEvent(long ptr) ChpeAutoZwPulseEvent
1648 stdcall ZwQueryAttributesFile(ptr ptr) ChpeAutoZwQueryAttributesFile
1649 stdcall ZwQueryBootEntryOrder(ptr ptr) ChpeAutoZwQueryBootEntryOrder
1650 stdcall ZwQueryBootOptions(ptr ptr) ChpeAutoZwQueryBootOptions
1651 stdcall ZwQueryDebugFilterState(long long) ChpeAutoZwQueryDebugFilterState
1652 stdcall ZwQueryDefaultLocale(long ptr) ChpeAutoZwQueryDefaultLocale
1653 stdcall ZwQueryDefaultUILanguage(ptr) ChpeAutoZwQueryDefaultUILanguage
1654 stdcall ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ChpeAutoZwQueryDirectoryFile
1655 stdcall ZwQueryDirectoryFileEx(long long ptr ptr ptr ptr long long long ptr) ChpeAutoZwQueryDirectoryFileEx
1656 stdcall ZwQueryDirectoryObject(long ptr long long long ptr ptr) ChpeAutoZwQueryDirectoryObject
1657 stdcall ZwQueryDriverEntryOrder(ptr ptr) ChpeAutoZwQueryDriverEntryOrder
1658 stdcall ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ChpeAutoZwQueryEaFile
1659 stdcall ZwQueryEvent(long long ptr long ptr) ChpeAutoZwQueryEvent
1660 stdcall ZwQueryFullAttributesFile(ptr ptr) ChpeAutoZwQueryFullAttributesFile
1661 stdcall ZwQueryInformationAtom(long long ptr long ptr) ChpeAutoZwQueryInformationAtom
1662 stdcall ZwQueryInformationByName(ptr ptr ptr long long) ChpeAutoZwQueryInformationByName
1663 stdcall -version=0x600+ ZwQueryInformationEnlistment(ptr long ptr long ptr) ChpeStubZwQueryInformationEnlistment
1664 stdcall ZwQueryInformationFile(long ptr ptr long long) ChpeAutoZwQueryInformationFile
1665 stdcall ZwQueryInformationJobObject(long long ptr long ptr) ChpeAutoZwQueryInformationJobObject
1666 stdcall ZwQueryInformationPort(ptr long ptr long ptr) ChpeAutoZwQueryInformationPort
1667 stdcall ZwQueryInformationProcess(long long ptr long ptr) ChpeAutoZwQueryInformationProcess
1668 stdcall -version=0x600+ ZwQueryInformationResourceManager(ptr long ptr long ptr) ChpeStubZwQueryInformationResourceManager
1669 stdcall ZwQueryInformationThread(long long ptr long ptr) ChpeAutoZwQueryInformationThread
1670 stdcall ZwQueryInformationToken(long long ptr long ptr) ChpeAutoZwQueryInformationToken
1671 stdcall -version=0x600+ ZwQueryInformationTransaction(ptr long ptr long ptr) ChpeStubZwQueryInformationTransaction
1672 stdcall -version=0x600+ ZwQueryInformationTransactionManager(ptr) ChpeStubZwQueryInformationTransactionManager
1673 stdcall -version=0x600+ ZwQueryInformationWorkerFactory(ptr long ptr long ptr) ChpeStubZwQueryInformationWorkerFactory
1674 stdcall ZwQueryInstallUILanguage(ptr) ChpeAutoZwQueryInstallUILanguage
1675 stdcall ZwQueryIntervalProfile(long ptr) ChpeAutoZwQueryIntervalProfile
1676 stdcall ZwQueryIoCompletion(long long ptr long ptr) ChpeAutoZwQueryIoCompletion
1677 stdcall ZwQueryKey(long long ptr long ptr) ChpeAutoZwQueryKey
1678 stdcall -version=0x600+ ZwQueryLicenseValue(ptr ptr ptr long ptr) ChpeStubZwQueryLicenseValue
1679 stdcall ZwQueryMultipleValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryMultipleValueKey
1680 stdcall ZwQueryMutant(long long ptr long ptr) ChpeAutoZwQueryMutant
1681 stdcall ZwQueryObject(long long long long long) ChpeAutoZwQueryObject
1682 stdcall ZwQueryOpenSubKeys(ptr ptr) ChpeAutoZwQueryOpenSubKeys
1683 stdcall ZwQueryOpenSubKeysEx(ptr long ptr ptr) ChpeAutoZwQueryOpenSubKeysEx
1684 stdcall ZwQueryPerformanceCounter(long long) ChpeAutoZwQueryPerformanceCounter
1685 stdcall ZwQueryPortInformationProcess() ChpeAutoZwQueryPortInformationProcess
1686 stdcall ZwQueryQuotaInformationFile(ptr ptr ptr long long ptr long ptr long) ChpeAutoZwQueryQuotaInformationFile
1687 stdcall ZwQuerySection(long long long long long) ChpeAutoZwQuerySection
1688 stdcall ZwQuerySecurityObject(long long long long long) ChpeAutoZwQuerySecurityObject
1689 stdcall ZwQuerySemaphore(long long long long long) ChpeAutoZwQuerySemaphore
1690 stdcall ZwQuerySymbolicLinkObject(long ptr ptr) ChpeAutoZwQuerySymbolicLinkObject
1691 stdcall ZwQuerySystemEnvironmentValue(ptr ptr long ptr) ChpeAutoZwQuerySystemEnvironmentValue
1692 stdcall ZwQuerySystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwQuerySystemEnvironmentValueEx
1693 stdcall ZwQuerySystemInformation(long long long long) ChpeAutoZwQuerySystemInformation
1694 stdcall -version=0x601+ ZwQuerySystemInformationEx(long ptr long ptr long ptr) ChpeAutoZwQuerySystemInformationEx
1695 stdcall ZwQuerySystemTime(ptr) ChpeAutoZwQuerySystemTime
1696 stdcall ZwQueryTimer(ptr long ptr long ptr) ChpeAutoZwQueryTimer
1697 stdcall ZwQueryTimerResolution(long long long) ChpeAutoZwQueryTimerResolution
1698 stdcall ZwQueryValueKey(long ptr long ptr long ptr) ChpeAutoZwQueryValueKey
1699 stdcall ZwQueryVirtualMemory(long ptr long ptr long ptr) ChpeAutoZwQueryVirtualMemory
1700 stdcall ZwQueryVolumeInformationFile(long ptr ptr long long) ChpeAutoZwQueryVolumeInformationFile
1701 stdcall -version=0x602+ ZwQueryWnfStateData(ptr ptr ptr ptr ptr ptr) ChpeAutoZwQueryWnfStateData
1702 stdcall -version=0x602+ ZwQueryWnfStateNameInformation(ptr long ptr ptr long) ChpeAutoZwQueryWnfStateNameInformation
1703 stdcall ZwQueueApcThread(long ptr long long long) ChpeAutoZwQueueApcThread
1704 stdcall ZwRaiseException(ptr ptr long) ChpeAutoZwRaiseException
1705 stdcall ZwRaiseHardError(long long long ptr long ptr) ChpeAutoZwRaiseHardError
1706 stdcall ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFile
1707 stdcall ZwReadFileScatter(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwReadFileScatter
1708 stdcall -version=0x600+ ZwReadOnlyEnlistment(ptr ptr) ChpeStubZwReadOnlyEnlistment
1709 stdcall ZwReadRequestData(ptr ptr long ptr long ptr) ChpeAutoZwReadRequestData
1710 stdcall ZwReadVirtualMemory(long ptr ptr long ptr) ChpeAutoZwReadVirtualMemory
1711 stdcall -version=0x600+ ZwRecoverEnlistment(ptr ptr) ChpeStubZwRecoverEnlistment
1712 stdcall -version=0x600+ ZwRecoverResourceManager(ptr) ChpeStubZwRecoverResourceManager
1713 stdcall -version=0x600+ ZwRecoverTransactionManager(ptr) ChpeStubZwRecoverTransactionManager
1714 stdcall -version=0x600+ ZwRegisterProtocolAddressInformation(ptr ptr long ptr long) ChpeStubZwRegisterProtocolAddressInformation
1715 stdcall ZwRegisterThreadTerminatePort(ptr) ChpeAutoZwRegisterThreadTerminatePort
1716 stdcall -version=0x600+ ZwReleaseCMFViewOwnership() ChpeStubZwReleaseCMFViewOwnership
1717 stdcall ZwReleaseKeyedEvent(ptr ptr long ptr) ChpeAutoZwReleaseKeyedEvent
1718 stdcall ZwReleaseMutant(long ptr) ChpeAutoZwReleaseMutant
1719 stdcall ZwReleaseSemaphore(long long ptr) ChpeAutoZwReleaseSemaphore
1720 stdcall -version=0x600+ ZwReleaseWorkerFactoryWorker(ptr) ChpeStubZwReleaseWorkerFactoryWorker
1721 stdcall ZwRemoveIoCompletion(ptr ptr ptr ptr ptr) ChpeAutoZwRemoveIoCompletion
1722 stdcall -version=0x600+ ZwRemoveIoCompletionEx(ptr ptr long ptr ptr long) ChpeAutoZwRemoveIoCompletionEx
1723 stdcall ZwRemoveProcessDebug(ptr ptr) ChpeAutoZwRemoveProcessDebug
1724 stdcall ZwRenameKey(ptr ptr) ChpeAutoZwRenameKey
1725 stdcall -version=0x600+ ZwRenameTransactionManager(wstr ptr) ChpeStubZwRenameTransactionManager
1726 stdcall ZwReplaceKey(ptr long ptr) ChpeAutoZwReplaceKey
1727 stdcall -version=0x600+ ZwReplacePartitionUnit(wstr wstr long) ChpeStubZwReplacePartitionUnit
1728 stdcall ZwReplyPort(ptr ptr) ChpeAutoZwReplyPort
1729 stdcall ZwReplyWaitReceivePort(ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePort
1730 stdcall ZwReplyWaitReceivePortEx(ptr ptr ptr ptr ptr) ChpeAutoZwReplyWaitReceivePortEx
1731 stdcall ZwReplyWaitReplyPort(ptr ptr) ChpeAutoZwReplyWaitReplyPort
1732 stdcall ZwRequestDeviceWakeup(ptr) ChpeAutoZwRequestDeviceWakeup
1733 stdcall ZwRequestPort(ptr ptr) ChpeAutoZwRequestPort
1734 stdcall ZwRequestWaitReplyPort(ptr ptr ptr) ChpeAutoZwRequestWaitReplyPort
1735 stdcall ZwRequestWakeupLatency(long) ChpeAutoZwRequestWakeupLatency
1736 stdcall ZwResetEvent(long ptr) ChpeAutoZwResetEvent
1737 stdcall ZwResetWriteWatch(long ptr long) ChpeAutoZwResetWriteWatch
1738 stdcall ZwRestoreKey(long long long) ChpeAutoZwRestoreKey
1739 stdcall ZwResumeProcess(ptr) ChpeAutoZwResumeProcess
1740 stdcall ZwResumeThread(long long) ChpeAutoZwResumeThread
1741 stdcall -version=0x600+ ZwRollbackComplete(ptr ptr) ChpeStubZwRollbackComplete
1742 stdcall -version=0x600+ ZwRollbackEnlistment(ptr ptr) ChpeStubZwRollbackEnlistment
1743 stdcall -version=0x600+ ZwRollbackTransaction(ptr long) ChpeStubZwRollbackTransaction
1744 stdcall -version=0x600+ ZwRollforwardTransactionManager(ptr ptr) ChpeStubZwRollforwardTransactionManager
1745 stdcall ZwSaveKey(long long) ChpeAutoZwSaveKey
1746 stdcall ZwSaveKeyEx(ptr ptr long) ChpeAutoZwSaveKeyEx
1747 stdcall ZwSaveMergedKeys(ptr ptr ptr) ChpeAutoZwSaveMergedKeys
1748 stdcall ZwSecureConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr ptr) ChpeAutoZwSecureConnectPort
1749 stdcall ZwSetBootEntryOrder(ptr ptr) ChpeAutoZwSetBootEntryOrder
1750 stdcall ZwSetBootOptions(ptr long) ChpeAutoZwSetBootOptions
1751 stdcall ZwSetContextThread(long ptr) ChpeAutoZwSetContextThread
1752 stdcall ZwSetDebugFilterState(long long long) ChpeAutoZwSetDebugFilterState
1753 stdcall ZwSetDefaultHardErrorPort(ptr) ChpeAutoZwSetDefaultHardErrorPort
1754 stdcall ZwSetDefaultLocale(long long) ChpeAutoZwSetDefaultLocale
1755 stdcall ZwSetDefaultUILanguage(long) ChpeAutoZwSetDefaultUILanguage
1756 stdcall ZwSetDriverEntryOrder(ptr ptr) ChpeAutoZwSetDriverEntryOrder
1757 stdcall ZwSetEaFile(long ptr ptr long) ChpeAutoZwSetEaFile
1758 stdcall ZwSetEvent(long long) ChpeAutoZwSetEvent
1759 stdcall ZwSetEventBoostPriority(ptr) ChpeAutoZwSetEventBoostPriority
1760 stdcall ZwSetHighEventPair(ptr) ChpeAutoZwSetHighEventPair
1761 stdcall ZwSetHighWaitLowEventPair(ptr) ChpeAutoZwSetHighWaitLowEventPair
1762 stdcall ZwSetInformationDebugObject(ptr long ptr long ptr) ChpeAutoZwSetInformationDebugObject
1763 stdcall -version=0x600+ ZwSetInformationEnlistment(ptr long ptr long) ChpeStubZwSetInformationEnlistment
1764 stdcall ZwSetInformationFile(long long long long long) ChpeAutoZwSetInformationFile
1765 stdcall ZwSetInformationJobObject(long long ptr long) ChpeAutoZwSetInformationJobObject
1766 stdcall ZwSetInformationKey(long long ptr long) ChpeAutoZwSetInformationKey
1767 stdcall ZwSetInformationObject(long long ptr long) ChpeAutoZwSetInformationObject
1768 stdcall ZwSetInformationProcess(long long long long) ChpeAutoZwSetInformationProcess
1769 stdcall -version=0x600+ ZwSetInformationResourceManager(ptr long ptr long) ChpeStubZwSetInformationResourceManager
1770 stdcall ZwSetInformationThread(long long ptr long) ChpeAutoZwSetInformationThread
1771 stdcall ZwSetInformationToken(long long ptr long) ChpeAutoZwSetInformationToken
1772 stdcall -version=0x600+ ZwSetInformationTransaction(ptr long ptr long) ChpeStubZwSetInformationTransaction
1773 stdcall -version=0x600+ ZwSetInformationTransactionManager(ptr long ptr long) ChpeStubZwSetInformationTransactionManager
1774 stdcall ZwSetInformationVirtualMemory(ptr long ptr ptr ptr long) ChpeAutoZwSetInformationVirtualMemory
1775 stdcall -version=0x600+ ZwSetInformationWorkerFactory(ptr long ptr long) ChpeStubZwSetInformationWorkerFactory
1776 stdcall ZwSetIntervalProfile(long long) ChpeAutoZwSetIntervalProfile
1777 stdcall ZwSetIoCompletion(ptr long ptr long long) ChpeAutoZwSetIoCompletion
1778 stdcall ZwSetLdtEntries(long int64 long int64) ChpeAutoZwSetLdtEntries
1779 stdcall ZwSetLowEventPair(ptr) ChpeAutoZwSetLowEventPair
1780 stdcall ZwSetLowWaitHighEventPair(ptr) ChpeAutoZwSetLowWaitHighEventPair
1781 stdcall ZwSetQuotaInformationFile(ptr ptr ptr long) ChpeAutoZwSetQuotaInformationFile
1782 stdcall ZwSetSecurityObject(long long ptr) ChpeAutoZwSetSecurityObject
1783 stdcall ZwSetSystemEnvironmentValue(ptr ptr) ChpeAutoZwSetSystemEnvironmentValue
1784 stdcall ZwSetSystemEnvironmentValueEx(ptr ptr ptr ptr ptr) ChpeAutoZwSetSystemEnvironmentValueEx
1785 stdcall ZwSetSystemInformation(long ptr long) ChpeAutoZwSetSystemInformation
1786 stdcall ZwSetSystemPowerState(long long long) ChpeAutoZwSetSystemPowerState
1787 stdcall ZwSetSystemTime(ptr ptr) ChpeAutoZwSetSystemTime
1788 stdcall ZwSetThreadExecutionState(long ptr) ChpeAutoZwSetThreadExecutionState
1789 stdcall ZwSetTimer(long ptr ptr ptr long long ptr) ChpeAutoZwSetTimer
1790 stdcall ZwSetTimerResolution(long long ptr) ChpeAutoZwSetTimerResolution
1791 stdcall ZwSetUuidSeed(ptr) ChpeAutoZwSetUuidSeed
1792 stdcall ZwSetValueKey(long long long long long long) ChpeAutoZwSetValueKey
1793 stdcall ZwSetVolumeInformationFile(long ptr ptr long long) ChpeAutoZwSetVolumeInformationFile
1794 stdcall ZwShutdownSystem(long) ChpeAutoZwShutdownSystem
1795 stdcall -version=0x600+ ZwShutdownWorkerFactory(ptr ptr) ChpeStubZwShutdownWorkerFactory
1796 stdcall ZwSignalAndWaitForSingleObject(long long long ptr) ChpeAutoZwSignalAndWaitForSingleObject
1797 stdcall -version=0x600+ ZwSinglePhaseReject(ptr ptr) ChpeStubZwSinglePhaseReject
1798 stdcall ZwStartProfile(ptr) ChpeAutoZwStartProfile
1799 stdcall ZwStopProfile(ptr) ChpeAutoZwStopProfile
1800 stdcall -version=0x602+ ZwSubscribeWnfStateChange(ptr long long ptr) ChpeAutoZwSubscribeWnfStateChange
1801 stdcall ZwSuspendProcess(ptr) ChpeAutoZwSuspendProcess
1802 stdcall ZwSuspendThread(long ptr) ChpeAutoZwSuspendThread
1803 stdcall ZwSystemDebugControl(long ptr long ptr long ptr) ChpeAutoZwSystemDebugControl
1804 stdcall ZwTerminateJobObject(ptr long) ChpeAutoZwTerminateJobObject
1805 stdcall ZwTerminateProcess(ptr long) ChpeAutoZwTerminateProcess
1806 stdcall ZwTerminateThread(ptr long) ChpeAutoZwTerminateThread
1807 stdcall ZwTestAlert() ChpeAutoZwTestAlert
1808 stdcall -version=0x600+ ZwThawRegistry() ChpeStubZwThawRegistry
1809 stdcall -version=0x600+ ZwThawTransactions() ChpeStubZwThawTransactions
1810 stdcall -version=0x600+ ZwTraceControl(long ptr long ptr long long) ChpeStubZwTraceControl
1811 stdcall ZwTraceEvent(long long long ptr) ChpeAutoZwTraceEvent
1812 stdcall ZwTranslateFilePath(ptr long ptr long) ChpeAutoZwTranslateFilePath
1813 stdcall ZwUnloadDriver(ptr) ChpeAutoZwUnloadDriver
1814 stdcall ZwUnloadKey2(ptr long) ChpeAutoZwUnloadKey2
1815 stdcall ZwUnloadKey(long) ChpeAutoZwUnloadKey
1816 stdcall ZwUnloadKeyEx(ptr ptr) ChpeAutoZwUnloadKeyEx
1817 stdcall ZwUnlockFile(long ptr ptr ptr ptr) ChpeAutoZwUnlockFile
1818 stdcall ZwUnlockVirtualMemory(long ptr ptr long) ChpeAutoZwUnlockVirtualMemory
1819 stdcall ZwUnmapViewOfSection(long ptr) ChpeAutoZwUnmapViewOfSection
1820 stdcall -version=0x602+ ZwUnsubscribeWnfStateChange(ptr) ChpeAutoZwUnsubscribeWnfStateChange
1821 stdcall -version=0x602+ ZwUpdateWnfStateData(ptr ptr long ptr ptr long long) ChpeAutoZwUpdateWnfStateData
1822 stdcall ZwVdmControl(long ptr) ChpeAutoZwVdmControl
1823 stdcall ZwWaitForDebugEvent(ptr long ptr ptr) ChpeAutoZwWaitForDebugEvent
1824 stdcall ZwWaitForKeyedEvent(ptr ptr long ptr) ChpeAutoZwWaitForKeyedEvent
1825 stdcall ZwWaitForAlertByThreadId(ptr ptr) ChpeAutoZwWaitForAlertByThreadId
1826 stdcall ZwWaitForMultipleObjects32(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects32
1827 stdcall ZwWaitForMultipleObjects(long ptr long long ptr) ChpeAutoZwWaitForMultipleObjects
1828 stdcall ZwWaitForSingleObject(long long long) ChpeAutoZwWaitForSingleObject
1829 stdcall -version=0x600+ ZwWaitForWorkViaWorkerFactory(ptr ptr long ptr ptr) ChpeStubZwWaitForWorkViaWorkerFactory
1830 stdcall ZwWaitHighEventPair(ptr) ChpeAutoZwWaitHighEventPair
1831 stdcall ZwWaitLowEventPair(ptr) ChpeAutoZwWaitLowEventPair
1832 stdcall -version=0x600+ ZwWorkerFactoryWorkerReady(ptr) ChpeStubZwWorkerFactoryWorkerReady
1833 stdcall ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFile
1834 stdcall ZwWriteFileGather(long long ptr ptr ptr ptr long ptr ptr) ChpeAutoZwWriteFileGather
1835 stdcall ZwWriteRequestData(ptr ptr long ptr long ptr) ChpeAutoZwWriteRequestData
1836 stdcall ZwWriteVirtualMemory(long ptr ptr long ptr) ChpeAutoZwWriteVirtualMemory
1837 stdcall ZwYieldExecution() ChpeAutoZwYieldExecution
1840 cdecl __isascii(long) ChpeAuto__isascii
1841 cdecl __iscsym(long) ChpeAuto__iscsym
1842 cdecl __iscsymf(long) ChpeAuto__iscsymf
1843 cdecl -version=0x600+ __misaligned_access() ChpeStub__misaligned_access
1844 cdecl __toascii(long) ChpeAuto__toascii
1845 cdecl -ret64 _atoi64(str) ChpeAuto_atoi64
1846 extern _fltused ntdll._fltused
1847 cdecl _i64toa(double ptr long) ChpeAuto_i64toa
1848 cdecl _i64tow(double ptr long) ChpeAuto_i64tow
1849 cdecl _itoa(long ptr long) ChpeAuto_itoa
1850 cdecl _itow(long ptr long) ChpeAuto_itow
1853 cdecl _ltoa(long ptr long) ChpeAuto_ltoa
1854 cdecl _ltow(long ptr long) ChpeAuto_ltow
1855 cdecl _memccpy(ptr ptr long long) ChpeAuto_memccpy
1856 cdecl _memicmp(str str long) ChpeAuto_memicmp
1861 cdecl _splitpath(str ptr ptr ptr ptr) ChpeAuto_splitpath
1862 cdecl _strcmpi(str str) ChpeAuto_strcmpi
1863 cdecl _stricmp(str str) ChpeAuto_stricmp
1864 cdecl _strlwr(str) ChpeAuto_strlwr
1865 cdecl _strnicmp(str str long) ChpeAuto_strnicmp
1866 cdecl _strupr(str) ChpeAuto_strupr
1868 cdecl _ui64toa(double ptr long) ChpeAuto_ui64toa
1869 cdecl _ui64tow(double ptr long) ChpeAuto_ui64tow
1870 cdecl _ultoa(long ptr long) ChpeAuto_ultoa
1871 cdecl _ultow(long ptr long) ChpeAuto_ultow
1872 cdecl _vscwprintf(wstr ptr) ChpeAuto_vscwprintf
1873 cdecl _vsnprintf(ptr long str ptr) ChpeAuto_vsnprintf
1874 cdecl _vsnwprintf(ptr long wstr ptr) ChpeAuto_vsnwprintf
1875 cdecl -version=0x600+ _vswprintf(ptr wstr ptr) ChpeStub_vswprintf
1876 cdecl _wcsicmp(wstr wstr) ChpeAuto_wcsicmp
1877 cdecl _wcslwr(wstr) ChpeAuto_wcslwr
1878 cdecl _wcsnicmp(wstr wstr long) ChpeAuto_wcsnicmp
1879 cdecl _wcstoui64(wstr ptr long) ChpeAuto_wcstoui64
1880 cdecl _wcsupr(wstr) ChpeAuto_wcsupr
1881 cdecl _wtoi(wstr) ChpeAuto_wtoi
1882 cdecl _wtoi64(wstr) ChpeAuto_wtoi64
1883 cdecl _wtol(wstr) ChpeAuto_wtol
1884 cdecl abs(long) ChpeAutoabs
1885 cdecl atan(double) ChpeAutoatan
1886 cdecl atoi(str) ChpeAutoatoi
1887 cdecl atol(str) ChpeAutoatol
1889 cdecl ceil(double) ChpeAutoceil
1890 cdecl cos(double) ChpeAutocos
1891 cdecl fabs(double) ChpeAutofabs
1892 cdecl floor(double) ChpeAutofloor
1893 cdecl isalnum(long) ChpeAutoisalnum
1894 cdecl isalpha(long) ChpeAutoisalpha
1895 cdecl iscntrl(long) ChpeAutoiscntrl
1896 cdecl isdigit(long) ChpeAutoisdigit
1897 cdecl isgraph(long) ChpeAutoisgraph
1898 cdecl islower(long) ChpeAutoislower
1899 cdecl isprint(long) ChpeAutoisprint
1900 cdecl ispunct(long) ChpeAutoispunct
1901 cdecl isspace(long) ChpeAutoisspace
1902 cdecl isupper(long) ChpeAutoisupper
1903 cdecl iswalpha(long) ChpeAutoiswalpha
1904 cdecl iswctype(long long) ChpeAutoiswctype
1905 cdecl iswdigit(long) ChpeAutoiswdigit
1906 cdecl iswlower(long) ChpeAutoiswlower
1907 cdecl iswspace(long) ChpeAutoiswspace
1908 cdecl iswxdigit(long) ChpeAutoiswxdigit
1909 cdecl isxdigit(long) ChpeAutoisxdigit
1910 cdecl labs(long) ChpeAutolabs
1911 cdecl log(double) ChpeAutolog
1912 cdecl longjmp(ptr) ChpeAutolongjmp
1913 cdecl mbstowcs(ptr str long) ChpeAutombstowcs
1914 cdecl memchr(ptr long long) ChpeAutomemchr
1915 cdecl memcmp(ptr ptr long) ChpeAutomemcmp
1917 cdecl memmove(ptr ptr long) ChpeAutomemmove
1918 cdecl memset(ptr long long) ChpeAutomemset
1919 cdecl pow(double double) ChpeAutopow
1921 cdecl sin(double) ChpeAutosin
1923 cdecl sqrt(double) ChpeAutosqrt
1925 cdecl strcat(str str) ChpeAutostrcat
1926 cdecl strchr(str long) ChpeAutostrchr
1927 cdecl strcmp(str str) ChpeAutostrcmp
1928 cdecl strcpy(ptr str) ChpeAutostrcpy
1929 cdecl -version=0x600+ strcpy_s(ptr long str) ChpeAutostrcpy_s
1930 cdecl -version=0x600+ strcat_s(ptr long str) ChpeAutostrcat_s
1931 cdecl -version=0x600+ strncpy_s(ptr long str long) ChpeAutostrncpy_s
1932 cdecl strcspn(str str) ChpeAutostrcspn
1933 cdecl strlen(str) ChpeAutostrlen
1934 cdecl strncat(str str long) ChpeAutostrncat
1935 cdecl strncmp(str str long) ChpeAutostrncmp
1936 cdecl strncpy(ptr str long) ChpeAutostrncpy
1937 cdecl strpbrk(str str) ChpeAutostrpbrk
1938 cdecl strrchr(str long) ChpeAutostrrchr
1939 cdecl strspn(str str) ChpeAutostrspn
1940 cdecl strstr(str str) ChpeAutostrstr
1941 cdecl strtol(str ptr long) ChpeAutostrtol
1942 cdecl strtoul(str ptr long) ChpeAutostrtoul
1944 cdecl tan(double) ChpeAutotan
1945 cdecl tolower(long) ChpeAutotolower
1946 cdecl toupper(long) ChpeAutotoupper
1947 cdecl towlower(long) ChpeAutotowlower
1948 cdecl towupper(long) ChpeAutotowupper
1951 cdecl vsprintf(ptr str ptr) ChpeAutovsprintf
1952 cdecl wcscat(wstr wstr) ChpeAutowcscat
1953 cdecl wcschr(wstr long) ChpeAutowcschr
1954 cdecl wcscmp(wstr wstr) ChpeAutowcscmp
1955 cdecl wcscpy(ptr wstr) ChpeAutowcscpy
1956 cdecl wcscspn(wstr wstr) ChpeAutowcscspn
1957 cdecl wcslen(wstr) ChpeAutowcslen
1958 cdecl wcsncat(wstr wstr long) ChpeAutowcsncat
1959 cdecl wcsncmp(wstr wstr long) ChpeAutowcsncmp
1960 cdecl wcsncpy(ptr wstr long) ChpeAutowcsncpy
1961 cdecl wcspbrk(wstr wstr) ChpeAutowcspbrk
1962 cdecl wcsrchr(wstr long) ChpeAutowcsrchr
1963 cdecl wcsspn(wstr wstr) ChpeAutowcsspn
1964 cdecl wcsstr(wstr wstr) ChpeAutowcsstr
1965 cdecl wcstol(wstr ptr long) ChpeAutowcstol
1966 cdecl wcstombs(ptr ptr long) ChpeAutowcstombs
1967 cdecl wcstoul(wstr ptr long) ChpeAutowcstoul
# END typed bridge wrappers
