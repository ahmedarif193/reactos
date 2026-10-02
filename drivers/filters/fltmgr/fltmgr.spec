
 @ stdcall FltRegisterFilter(ptr ptr ptr)
 @ stdcall FltUnregisterFilter(ptr)
 @ stdcall FltStartFiltering(ptr)
 @ stdcall FltBuildDefaultSecurityDescriptor(ptr long)
 @ stdcall FltFreeSecurityDescriptor(ptr)
 @ stdcall FltGetDiskDeviceObject(ptr ptr)
 @ stdcall FltGetVolumeProperties(ptr ptr long ptr)
 @ stdcall FltObjectDereference(ptr)
 @ stdcall FltSendMessage(ptr ptr ptr long ptr ptr ptr)
 @ stdcall FltEnumerateVolumes(ptr ptr long ptr)
 @ stdcall FltGetFileNameInformationUnsafe(ptr ptr long ptr)
 @ stdcall FltCloseClientPort(ptr ptr)
 @ stdcall FltClose(ptr)
 @ stdcall FltCreateFileEx(ptr ptr ptr ptr long ptr ptr ptr long long long long ptr long long)
 @ stdcall FltCreateFile(ptr ptr ptr long ptr ptr ptr long long long long ptr long long)
 @ stdcall FltDetachVolume(ptr ptr ptr)
 @ stdcall FltGetVolumeName(ptr ptr ptr)
 @ stdcall FltGetFileNameInformation(ptr long ptr)
 @ stdcall FltCreateCommunicationPort(ptr ptr ptr ptr ptr ptr ptr long)
 @ stdcall FltCloseCommunicationPort(ptr)
 @ stdcall FltAttachVolume(ptr ptr ptr ptr)
 @ stdcall FltGetDestinationFileNameInformation(ptr ptr ptr ptr long long ptr)
 @ stdcall FltReleaseFileNameInformation(ptr)
 @ stdcall FltAcknowledgeEcp(ptr ptr)
 @ stdcall FltAcquirePushLockExclusive(ptr)
 @ stdcall FltAcquirePushLockExclusiveEx(ptr long)
 @ stdcall FltAcquirePushLockShared(ptr)
 @ stdcall FltAcquirePushLockSharedEx(ptr long)
 @ stdcall FltAcquireResourceExclusive(ptr)
 @ stdcall FltAcquireResourceShared(ptr)
 @ stub FltAddOpenReparseEntry
 @ stub FltAdjustDeviceStackSizeForIoRedirection
 @ stdcall FltAllocateCallbackData(ptr ptr ptr)
 @ stdcall FltAllocateCallbackDataEx(ptr ptr long ptr)
 @ stdcall FltAllocateContext(ptr long ptr long ptr)
 @ stdcall FltAllocateDeferredIoWorkItem()
 @ stdcall FltAllocateExtraCreateParameter(ptr ptr long long ptr long ptr)
 @ stdcall FltAllocateExtraCreateParameterFromLookasideList(ptr ptr long long ptr ptr ptr)
 @ stdcall FltAllocateExtraCreateParameterList(ptr long ptr)
 @ stub FltAllocateFileLock
 @ stdcall FltAllocateGenericWorkItem()
 @ stdcall FltAllocatePoolAlignedWithTag(ptr long ptr long)
 @ stub FltApplyPriorityInfoThread
 @ stdcall FltAttachVolumeAtAltitude(ptr ptr ptr ptr ptr)
 @ stdcall FltCancelFileOpen(ptr ptr)
 @ stdcall FltCancelIo(ptr)
 @ stdcall FltCancellableWaitForMultipleObjects(long ptr long ptr ptr ptr)
 @ stdcall FltCancellableWaitForSingleObject(ptr ptr ptr)
 @ stdcall FltCbdqDisable(ptr)
 @ stdcall FltCbdqEnable(ptr)
 @ stdcall FltCbdqInitialize(ptr ptr ptr ptr ptr ptr ptr ptr)
 @ stdcall FltCbdqInsertIo(ptr ptr ptr ptr)
 @ stdcall FltCbdqRemoveIo(ptr ptr)
 @ stdcall FltCbdqRemoveNextIo(ptr ptr)
 @ stdcall FltCheckAndGrowNameControl(ptr long)
 @ stub FltCheckLockForReadAccess
 @ stub FltCheckLockForWriteAccess
 @ stub FltCheckOplock
 @ stub FltCheckOplockEx
 @ stdcall FltClearCallbackDataDirty(ptr)
 @ stdcall FltClearCancelCompletion(ptr)
 @ stdcall FltCloseSectionForDataScan(ptr)
 @ stdcall FltCommitComplete(ptr ptr ptr)
 @ stdcall FltCommitFinalizeComplete(ptr ptr ptr)
 @ stdcall FltCompareInstanceAltitudes(ptr ptr)
 @ stdcall FltCompletePendedPostOperation(ptr)
 @ stdcall FltCompletePendedPreOperation(ptr long ptr)
 @ stub FltCopyOpenReparseList
 @ stdcall FltCreateFileEx2(ptr ptr ptr ptr long ptr ptr ptr long long long long ptr long long ptr)
 @ stub FltCreateMailslotFile
 @ stub FltCreateNamedPipeFile
 @ stdcall FltCreateSectionForDataScan(ptr ptr ptr long ptr ptr long long long ptr ptr ptr)
 @ stdcall FltCreateSystemVolumeInformationFolder(ptr)
 @ stub FltCurrentBatchOplock
 @ stub FltCurrentOplock
 @ stub FltCurrentOplockH
 @ stdcall FltDecodeParameters(ptr ptr ptr ptr ptr)
 @ stdcall FltDeleteContext(ptr)
 @ stdcall FltDeleteExtraCreateParameterLookasideList(ptr ptr long)
 @ stdcall FltDeleteFileContext(ptr ptr ptr)
 @ stdcall FltDeleteInstanceContext(ptr ptr)
 @ stdcall FltDeletePushLock(ptr)
 @ stdcall FltDeleteStreamContext(ptr ptr ptr)
 @ stdcall FltDeleteStreamHandleContext(ptr ptr ptr)
 @ stdcall FltDeleteTransactionContext(ptr ptr ptr)
 @ stdcall FltDeleteVolumeContext(ptr ptr ptr)
 @ stdcall FltDeviceIoControlFile(ptr ptr long ptr long ptr long ptr)
 @ stdcall FltDoCompletionProcessingWhenSafe(ptr ptr ptr long ptr ptr)
 @ stdcall FltEnlistInTransaction(ptr ptr ptr long)
 @ stdcall FltEnumerateFilterInformation(long long ptr long ptr)
 @ stdcall FltEnumerateFilters(ptr long ptr)
 @ stdcall FltEnumerateInstanceInformationByDeviceObject(ptr long long ptr long ptr)
 @ stdcall FltEnumerateInstanceInformationByFilter(ptr long long ptr long ptr)
 @ stdcall FltEnumerateInstanceInformationByVolume(ptr long long ptr long ptr)
 @ stdcall FltEnumerateInstanceInformationByVolumeName(ptr long long ptr long ptr)
 @ stdcall FltEnumerateInstances(ptr ptr ptr long ptr)
 @ stdcall FltEnumerateVolumeInformation(ptr long long ptr long ptr)
 @ stub FltFastIoMdlRead
 @ stub FltFastIoMdlReadComplete
 @ stub FltFastIoMdlWriteComplete
 @ stub FltFastIoPrepareMdlWrite
 @ stdcall FltFindExtraCreateParameter(ptr ptr ptr ptr ptr)
 @ stdcall FltFlushBuffers(ptr ptr)
 @ stub FltFlushBuffers2
 @ stdcall FltFreeCallbackData(ptr)
 @ stdcall FltFreeDeferredIoWorkItem(ptr)
 @ stdcall FltFreeExtraCreateParameter(ptr ptr)
 @ stdcall FltFreeExtraCreateParameterList(ptr ptr)
 @ stub FltFreeFileLock
 @ stdcall FltFreeGenericWorkItem(ptr)
 @ stub FltFreeOpenReparseList
 @ stdcall FltFreePoolAlignedWithTag(ptr ptr long)
 @ stdcall FltFsControlFile(ptr ptr long ptr long ptr long ptr)
 @ stub FltGetActivityIdCallbackData
 @ stdcall FltGetBottomInstance(ptr ptr)
 @ stdcall FltGetContexts(ptr long ptr)
 @ stdcall FltGetContextsEx(ptr long ptr ptr)
 @ stub FltGetCopyInformationFromCallbackData
 @ stdcall FltGetDeviceObject(ptr ptr)
 @ stdcall FltGetEcpListFromCallbackData(ptr ptr ptr)
 @ stdcall FltGetFileContext(ptr ptr ptr)
 @ stdcall FltGetFileSystemType(ptr ptr)
 @ stdcall FltGetFilterFromInstance(ptr ptr)
 @ stdcall FltGetFilterFromName(ptr ptr)
 @ stdcall FltGetFilterInformation(ptr long ptr long ptr)
 @ stub FltGetFsZeroingOffset
 @ stdcall FltGetInstanceContext(ptr ptr)
 @ stdcall FltGetInstanceInformation(ptr long ptr long ptr)
 @ stub FltGetIoAttributionHandleFromCallbackData
 @ stub FltGetIoCacheIntention
 @ stub FltGetIoPriorityHint
 @ stub FltGetIoPriorityHintFromCallbackData
 @ stub FltGetIoPriorityHintFromFileObject
 @ stub FltGetIoPriorityHintFromThread
 @ stdcall FltGetIrpName(long)
 @ stdcall FltGetLowerInstance(ptr ptr)
 @ stdcall FltGetNewSystemBufferAddress(ptr)
 @ stdcall FltGetNextExtraCreateParameter(ptr ptr ptr ptr ptr ptr)
 @ stdcall FltGetRequestorProcess(ptr)
 @ stdcall FltGetRequestorProcessId(ptr)
 @ stdcall FltGetRequestorProcessIdEx(ptr)
 @ stdcall FltGetRequestorSessionId(ptr ptr)
 @ stdcall FltGetRoutineAddress(ptr)
 @ stdcall FltGetSectionContext(ptr ptr ptr)
 @ stdcall FltGetStreamContext(ptr ptr ptr)
 @ stdcall FltGetStreamHandleContext(ptr ptr ptr)
 @ fastcall FltGetSwappedBufferMdlAddress(ptr)
 @ stdcall FltGetTopInstance(ptr ptr)
 @ stdcall FltGetTransactionContext(ptr ptr ptr)
 @ stdcall FltGetTunneledName(ptr ptr ptr)
 @ stdcall FltGetUpperInstance(ptr ptr)
 @ stdcall FltGetVolumeContext(ptr ptr ptr)
 @ stdcall FltGetVolumeFromDeviceObject(ptr ptr ptr)
 @ stdcall FltGetVolumeFromFileObject(ptr ptr ptr)
 @ stdcall FltGetVolumeFromInstance(ptr ptr)
 @ stdcall FltGetVolumeFromName(ptr ptr ptr)
 @ stdcall FltGetVolumeGuidName(ptr ptr ptr)
 @ stdcall FltGetVolumeInformation(ptr long ptr long ptr)
 @ stdcall FltGetVolumeInstanceFromName(ptr ptr ptr ptr)
 @ stdcall FltInitExtraCreateParameterLookasideList(ptr ptr long ptr long)
 @ stub FltInitializeFileLock
 @ stub FltInitializeOplock
 @ stdcall FltInitializePushLock(ptr)
 @ stdcall FltInsertExtraCreateParameter(ptr ptr ptr)
 @ stdcall FltIs32bitProcess(ptr)
 @ stdcall FltIsCallbackDataDirty(ptr)
 @ stdcall FltIsDirectory(ptr ptr ptr)
 @ stdcall FltIsEcpAcknowledged(ptr ptr)
 @ stdcall FltIsEcpFromUserMode(ptr ptr)
 @ stdcall FltIsFltMgrVolumeDeviceObject(ptr)
 @ stdcall FltIsIoCanceled(ptr)
 @ stub FltIsIoRedirectionAllowed
 @ stub FltIsIoRedirectionAllowedForOperation
 @ stdcall FltIsOperationSynchronous(ptr)
 @ stdcall FltIsVolumeSnapshot(ptr ptr)
 @ stdcall FltIsVolumeWritable(ptr ptr)
 @ stdcall FltLoadFilter(ptr)
 @ stdcall FltLockUserBuffer(ptr)
 @ stub FltMupGetProviderInfoFromFileObject
 @ stub FltNotifyFilterChangeDirectory
 @ stdcall FltObjectReference(ptr)
 @ stub FltOpenVolume
 @ stub FltOplockBreakH
 @ stub FltOplockBreakToNone
 @ stub FltOplockBreakToNoneEx
 @ stub FltOplockFsctrl
 @ stub FltOplockFsctrlEx
 @ stub FltOplockIsFastIoPossible
 @ stub FltOplockIsSharedRequest
 @ stub FltOplockKeysEqual
 @ stdcall FltParseFileName(ptr ptr ptr ptr)
 @ stdcall FltParseFileNameInformation(ptr)
 @ stdcall FltPerformAsynchronousIo(ptr ptr ptr)
 @ stdcall FltPerformSynchronousIo(ptr)
 @ stdcall FltPrePrepareComplete(ptr ptr ptr)
 @ stdcall FltPrepareComplete(ptr ptr ptr)
 @ stdcall FltPrepareToReuseEcp(ptr ptr)
 @ stub FltProcessFileLock
 @ stub FltPropagateActivityIdToThread
 @ stub FltPropagateIrpExtension
 @ stdcall FltPurgeFileNameInformationCache(ptr ptr)
 @ stdcall FltQueryDirectoryFile(ptr ptr ptr long long long ptr long ptr)
 @ stdcall FltQueryDirectoryFileEx(ptr ptr ptr long long long ptr ptr)
 @ stdcall FltQueryEaFile(ptr ptr ptr long long ptr long ptr long ptr)
 @ stub FltQueryInformationByName
 @ stdcall FltQueryInformationFile(ptr ptr ptr long long ptr)
 @ stub FltQueryQuotaInformationFile
 @ stdcall FltQuerySecurityObject(ptr ptr long ptr long ptr)
 @ stub FltQueryVolumeInformation
 @ stdcall FltQueryVolumeInformationFile(ptr ptr ptr long long ptr)
 @ stdcall FltQueueDeferredIoWorkItem(ptr ptr ptr long ptr)
 @ stdcall FltQueueGenericWorkItem(ptr ptr ptr long ptr)
 @ stdcall FltReadFile(ptr ptr ptr long ptr long ptr ptr ptr)
 @ stdcall FltReadFileEx(ptr ptr ptr long ptr long ptr ptr ptr ptr ptr)
 @ stdcall FltReferenceContext(ptr)
 @ stdcall FltReferenceFileNameInformation(ptr)
 @ stdcall FltRegisterForDataScan(ptr)
 @ stub FltReissueSynchronousIo
 @ stdcall FltReleaseContext(ptr)
 @ stdcall FltReleaseContexts(ptr)
 @ stdcall FltReleaseContextsEx(ptr ptr)
 @ stdcall FltReleasePushLock(ptr)
 @ stdcall FltReleasePushLockEx(ptr long)
 @ stdcall FltReleaseResource(ptr)
 @ stdcall FltRemoveExtraCreateParameter(ptr ptr ptr ptr ptr)
 @ stub FltRemoveOpenReparseEntry
 @ stub FltRequestFileInfoOnCreateCompletion
 @ stdcall FltRequestOperationStatusCallback(ptr ptr ptr)
 @ stub FltRequestSecurityInfoOnCreateCompletion
 @ fastcall FltRetainSwappedBufferMdlAddress(ptr)
 @ stub FltRetrieveFileInfoOnCreateCompletion
 @ stub FltRetrieveFileInfoOnCreateCompletionEx
 @ stub FltRetrieveIoPriorityInfo
 @ stdcall FltReuseCallbackData(ptr)
 @ stdcall FltRollbackComplete(ptr ptr ptr)
 @ stdcall FltRollbackEnlistment(ptr ptr ptr)
 @ stub FltSetActivityIdCallbackData
 @ stdcall FltSetCallbackDataDirty(ptr)
 @ stdcall FltSetCancelCompletion(ptr ptr)
 @ stdcall FltSetEaFile(ptr ptr ptr long)
 @ stdcall FltSetEcpListIntoCallbackData(ptr ptr ptr)
 @ stdcall FltSetFileContext(ptr ptr long ptr ptr)
 @ stub FltSetFsZeroingOffset
 @ stub FltSetFsZeroingOffsetRequired
 @ stdcall FltSetInformationFile(ptr ptr ptr long long)
 @ stdcall FltSetInstanceContext(ptr long ptr ptr)
 @ stub FltSetIoCacheIntention
 @ stub FltSetIoPriorityHintIntoCallbackData
 @ stub FltSetIoPriorityHintIntoFileObject
 @ stub FltSetIoPriorityHintIntoThread
 @ stub FltSetQuotaInformationFile
 @ stdcall FltSetSecurityObject(ptr ptr long ptr)
 @ stdcall FltSetStreamContext(ptr ptr long ptr ptr)
 @ stdcall FltSetStreamHandleContext(ptr ptr long ptr ptr)
 @ stdcall FltSetTransactionContext(ptr ptr long ptr ptr)
 @ stdcall FltSetVolumeContext(ptr long ptr ptr)
 @ stub FltSetVolumeInformation
 @ stdcall FltSupportsFileContexts(ptr)
 @ stdcall FltSupportsFileContextsEx(ptr ptr)
 @ stdcall FltSupportsStreamContexts(ptr)
 @ stdcall FltSupportsStreamHandleContexts(ptr)
 @ stub FltTagFile
 @ stub FltTagFileEx
 @ stub FltUninitializeFileLock
 @ stub FltUninitializeOplock
 @ stdcall FltUnloadFilter(ptr)
 @ stub FltUntagFile
 @ stub FltVetoBypassIo
 @ stdcall FltWriteFile(ptr ptr ptr long ptr long ptr ptr ptr)
 @ stdcall FltWriteFileEx(ptr ptr ptr long ptr long ptr ptr ptr ptr ptr)
 @ stub FltpTraceRedirectedFileIo
