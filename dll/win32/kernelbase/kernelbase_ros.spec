@ stdcall AccessCheck(ptr long long ptr ptr ptr ptr ptr) kernelbase.AccessCheck
@ stdcall AccessCheckAndAuditAlarmW(wstr ptr wstr wstr ptr long ptr long ptr ptr ptr) kernelbase.AccessCheckAndAuditAlarmW
@ stdcall AccessCheckByType(ptr ptr long long ptr long ptr ptr ptr ptr ptr) kernelbase.AccessCheckByType
@ stdcall AccessCheckByTypeResultList(ptr ptr long long ptr long ptr ptr ptr ptr ptr) kernelbase.AccessCheckByTypeResultList
#@ stub AccessCheckByTypeAndAuditAlarmW
#@ stub AccessCheckByTypeResultList
#@ stub AccessCheckByTypeResultListAndAuditAlarmByHandleW
#@ stub AccessCheckByTypeResultListAndAuditAlarmW
#@ stdcall AcquireSRWLockExclusive(ptr) ntdll.RtlAcquireSRWLockExclusive
#@ stdcall AcquireSRWLockShared(ptr) ntdll.RtlAcquireSRWLockShared
# #@ stub AcquireStateLock
#@ stdcall ActivateActCtx(ptr ptr)
@ stdcall AddAccessAllowedAce(ptr long long ptr) kernelbase.AddAccessAllowedAce
@ stdcall AddAccessAllowedAceEx(ptr long long long ptr) kernelbase.AddAccessAllowedAceEx
@ stdcall AddAccessAllowedObjectAce(ptr long long long ptr ptr ptr) kernelbase.AddAccessAllowedObjectAce
@ stdcall AddAccessDeniedAce(ptr long long ptr) kernelbase.AddAccessDeniedAce
@ stdcall AddAccessDeniedAceEx(ptr long long long ptr) kernelbase.AddAccessDeniedAceEx
@ stdcall AddAccessDeniedObjectAce(ptr long long long ptr ptr ptr) kernelbase.AddAccessDeniedObjectAce
@ stdcall AddAce(ptr long long ptr long) kernelbase.AddAce
@ stdcall AddAuditAccessAce(ptr long long ptr long long) kernelbase.AddAuditAccessAce
@ stdcall AddAuditAccessAceEx(ptr long long long ptr long long) kernelbase.AddAuditAccessAceEx
@ stdcall AddAuditAccessObjectAce(ptr long long long ptr ptr ptr long long) kernelbase.AddAuditAccessObjectAce
@ stdcall AddConsoleAliasA(str str str) kernelbase.AddConsoleAliasA
@ stdcall AddConsoleAliasW(wstr wstr wstr) kernelbase.AddConsoleAliasW
#@ stdcall AddDllDirectory(wstr)
@ stdcall AddMandatoryAce(ptr long long long ptr) kernelbase.AddMandatoryAce
#@ stdcall AddRefActCtx(ptr)
@ stdcall AddResourceAttributeAce(ptr long long long ptr ptr ptr) kernelbase.AddResourceAttributeAce
@ stdcall AddSIDToBoundaryDescriptor(ptr ptr) kernelbase.AddSIDToBoundaryDescriptor
@ stdcall AddIntegrityLabelToBoundaryDescriptor(ptr ptr) kernelbase.AddIntegrityLabelToBoundaryDescriptor
@ stdcall AddScopedPolicyIDAce(ptr long long long ptr) kernelbase.AddScopedPolicyIDAce
#@ stdcall AddVectoredContinueHandler(long ptr) ntdll.RtlAddVectoredContinueHandler
#@ stdcall AddVectoredExceptionHandler(long ptr) ntdll.RtlAddVectoredExceptionHandler
@ stdcall AdjustTokenGroups(long long ptr long ptr ptr) kernelbase.AdjustTokenGroups
@ stdcall AdjustTokenPrivileges(long long ptr long ptr ptr) kernelbase.AdjustTokenPrivileges
@ stdcall AllocConsole() kernelbase.AllocConsole
@ stdcall AllocateAndInitializeSid(ptr long long long long long long long long long ptr) kernelbase.AllocateAndInitializeSid
@ stdcall AllocateLocallyUniqueId(ptr) kernelbase.AllocateLocallyUniqueId
#@ stdcall AllocateUserPhysicalPages(long ptr ptr)
#@ stdcall AllocateUserPhysicalPagesNuma(long ptr ptr long)
# #@ stub AppContainerDeriveSidFromMoniker
@ stdcall AppContainerFreeMemory(ptr) kernelbase.AppContainerFreeMemory
# #@ stub AppContainerLookupDisplayNameMrtReference
@ stdcall AppContainerLookupMoniker(ptr ptr) kernelbase.AppContainerLookupMoniker
@ stdcall AppContainerRegisterSid(ptr wstr wstr) kernelbase.AppContainerRegisterSid
@ stdcall AppContainerUnregisterSid(ptr) kernelbase.AppContainerUnregisterSid
# #@ stub AppPolicyGetClrCompat
# #@ stub AppPolicyGetCreateFileAccess
# #@ stub AppPolicyGetLifecycleManagement
#@ stdcall AppPolicyGetMediaFoundationCodecLoading(ptr ptr)
#@ stdcall AppPolicyGetProcessTerminationMethod(ptr ptr)
#@ stdcall AppPolicyGetShowDeveloperDiagnostic(ptr ptr)
#@ stdcall AppPolicyGetThreadInitializationType(ptr ptr)
#@ stdcall AppPolicyGetWindowingModel(ptr ptr)
# #@ stub AppXFreeMemory
# #@ stub AppXGetApplicationData
# #@ stub AppXGetDevelopmentMode
# #@ stub AppXGetOSMaxVersionTested
# #@ stub AppXGetOSMinVersion
# #@ stub AppXGetPackageCapabilities
# #@ stub AppXGetPackageSid
# #@ stub AppXLookupDisplayName
# #@ stub AppXLookupMoniker
# #@ stub AppXPostSuccessExtension
# #@ stub AppXPreCreationExtension
# #@ stub AppXReleaseAppXContext
# #@ stub AppXUpdatePackageCapabilities
# #@ stub ApplicationUserModelIdFromProductId
@ stdcall AreAllAccessesGranted(long long) kernelbase.AreAllAccessesGranted
@ stdcall AreAnyAccessesGranted(long long) kernelbase.AreAnyAccessesGranted
#@ stdcall AreFileApisANSI()
# #@ stub AreThereVisibleLogoffScriptsInternal
# #@ stub AreThereVisibleShutdownScriptsInternal
@ stdcall AttachConsole(long) kernelbase.AttachConsole
#@ stub BaseCheckAppcompatCache
# #@ stub BaseCheckAppcompatCacheEx
#@ stub BaseCleanupAppcompatCacheSupport
#@ stub BaseDllFreeResourceId
#@ stub BaseDllMapResourceIdW
#@ stub BaseDumpAppcompatCache
#@ stdcall BaseFlushAppcompatCache()
# #@ stub BaseFormatObjectAttributes
# #@ stub BaseFreeAppCompatDataForProcess
#@ stdcall BaseGetNamedObjectDirectory(ptr)
#@ stub BaseGetProcessDllPath
#@ stub BaseGetProcessExePath
#@ stub BaseInitAppcompatCacheSupport
#@ stub BaseInvalidateDllSearchPathCache
#@ stub BaseInvalidateProcessSearchPathCache
# #@ stub BaseIsAppcompatInfrastructureDisabled
# #@ stub BaseMarkFileForDelete
# #@ stub BaseReadAppCompatDataForProcess
#@ stub BaseReleaseProcessDllPath
#@ stub BaseReleaseProcessExePath
#@ stub BaseUpdateAppcompatCache
@ stdcall BasepAdjustObjectAttributesForPrivateNamespace(ptr) kernelbase.BasepAdjustObjectAttributesForPrivateNamespace
# #@ stub BasepCopyFileCallback
# #@ stub BasepCopyFileExW
# #@ stub BasepNotifyTrackingService
#@ stdcall Beep(long long)
#@ stub BemCopyReference
#@ stub BemCreateContractFrom
#@ stub BemCreateReference
#@ stub BemFreeContract
#@ stub BemFreeReference
# #@ stub CLOSE_LOCAL_HANDLE_INTERNAL
#@ stdcall CallNamedPipeW(wstr ptr long ptr long ptr long)
#@ stdcall CallbackMayRunLong(ptr)
#@ stdcall CancelIo(long)
#@ stdcall CancelIoEx(long ptr)
@ stdcall CancelSynchronousIo(long) kernelbase.CancelSynchronousIo
@ stdcall CancelThreadpoolIo(ptr) kernelbase.CancelThreadpoolIo
#@ stdcall CancelWaitableTimer(long)
# #@ stub CeipIsOptedIn
#@ stdcall ChangeTimerQueueTimer(ptr ptr long long)
#@ stdcall CharLowerA(str)
#@ stdcall CharLowerBuffA(str long)
#@ stdcall CharLowerBuffW(wstr long)
#@ stdcall CharLowerW(wstr)
#@ stdcall CharNextA(str)
#@ stdcall CharNextExA(long str long)
#@ stdcall CharNextW(wstr)
#@ stdcall CharPrevA(str str)
#@ stdcall CharPrevExA(long str str long)
#@ stdcall CharPrevW(wstr wstr)
#@ stdcall CharUpperA(str)
#@ stdcall CharUpperBuffA(str long)
#@ stdcall CharUpperBuffW(wstr long)
#@ stdcall CharUpperW(wstr)
# #@ stub CheckAllowDecryptedRemoteDestinationPolicy
#@ stub CheckGroupPolicyEnabled
# #@ stub CheckIfStateChangeNotificationExists
#@ stdcall CheckRemoteDebuggerPresent(long ptr)
@ stdcall CheckTokenCapability(ptr ptr ptr) kernelbase.CheckTokenCapability
@ stdcall CheckTokenMembership(long ptr ptr) kernelbase.CheckTokenMembership
@ stdcall CheckTokenMembershipEx(ptr ptr long ptr) kernelbase.CheckTokenMembershipEx
@ stdcall ChrCmpIA(long long) kernelbase.ChrCmpIA
@ stdcall ChrCmpIW(long long) kernelbase.ChrCmpIW
#@ stdcall ClearCommBreak(long)
#@ stdcall ClearCommError(long ptr ptr)
# #@ stub CloseGlobalizationUserSettingsKey
#@ stdcall CloseHandle(long)
# #@ stub ClosePackageInfo
@ stdcall ClosePrivateNamespace(ptr long) kernelbase.ClosePrivateNamespace
@ stdcall ClosePseudoConsole(ptr) kernelbase.ClosePseudoConsole
# #@ stub CloseState
# #@ stub CloseStateAtom
# #@ stub CloseStateChangeNotification
# #@ stub CloseStateContainer
# #@ stub CloseStateLock
@ stdcall CloseThreadpool(ptr) kernelbase.CloseThreadpool
@ stdcall CloseThreadpoolCleanupGroup(ptr) kernelbase.CloseThreadpoolCleanupGroup
@ stdcall CloseThreadpoolCleanupGroupMembers(ptr long ptr) kernelbase.CloseThreadpoolCleanupGroupMembers
@ stdcall CloseThreadpoolIo(ptr) kernelbase.CloseThreadpoolIo
@ stdcall CloseThreadpoolTimer(ptr) kernelbase.CloseThreadpoolTimer
@ stdcall CloseThreadpoolWait(ptr) kernelbase.CloseThreadpoolWait
@ stdcall CloseThreadpoolWork(ptr) kernelbase.CloseThreadpoolWork
# #@ stub CommitStateAtom
#@ stdcall CompareFileTime(ptr ptr)
#@ stdcall CompareObjectHandles(ptr ptr)
@ stdcall CompareStringA(long long str long str long) kernelbase.CompareStringA
@ stdcall CompareStringEx(wstr long wstr long wstr long ptr ptr long) kernelbase.CompareStringEx
@ stdcall CompareStringOrdinal(wstr long wstr long long) kernelbase.CompareStringOrdinal
@ stdcall CompareStringW(long long wstr long wstr long) kernelbase.CompareStringW
#@ stdcall ConnectNamedPipe(long ptr)
#@ stdcall ContinueDebugEvent(long long long)
#@ stdcall ConvertDefaultLocale(long)
#@ stdcall ConvertFiberToThread()
#@ stdcall ConvertThreadToFiber(ptr)
#@ stdcall ConvertThreadToFiberEx(ptr long)
@ stdcall ConvertToAutoInheritPrivateObjectSecurity(ptr ptr ptr ptr long ptr) kernelbase.ConvertToAutoInheritPrivateObjectSecurity
#@ stdcall CopyContext(ptr long ptr)
#@ stdcall CopyFile2(wstr wstr ptr)
#@ stdcall CopyFileExW(wstr wstr ptr ptr ptr long)
#@ stdcall CopyFileW(wstr wstr long)
#@ stdcall -arch=x86_64 CopyMemoryNonTemporal(ptr ptr long) ntdll.RtlCopyMemoryNonTemporal
@ stdcall CopySid(long ptr ptr) kernelbase.CopySid
# #@ stub CouldMultiUserAppsBehaviorBePossibleForPackage
#@ stdcall CreateActCtxW(ptr)
# #@ stub CreateAppContainerToken
@ stdcall CreateBoundaryDescriptorW(wstr long) kernelbase.CreateBoundaryDescriptorW
@ stdcall CreateConsoleScreenBuffer(long long ptr long ptr) kernelbase.CreateConsoleScreenBuffer
#@ stdcall CreateDirectoryA(str ptr)
#@ stdcall CreateDirectoryExW(wstr wstr ptr)
#@ stdcall CreateDirectoryW(wstr ptr)
# #@ stub CreateEnclave
#@ stdcall CreateEventA(ptr long long str)
@ stdcall CreateEventExA(ptr str long long) kernelbase.CreateEventExA
@ stdcall CreateEventExW(ptr wstr long long) kernelbase.CreateEventExW
#@ stdcall CreateEventW(ptr long long wstr)
#@ stdcall CreateFiber(long ptr ptr)
#@ stdcall CreateFiberEx(long long long ptr ptr)
#@ stdcall CreateFile2(wstr long long long ptr)
#@ stdcall CreateFileA(str long long ptr long long long)
#@ stdcall CreateFileMappingFromApp(long ptr long int64 wstr)
#@ stdcall CreateFileMappingNumaW(long ptr long long long wstr long)
#@ stdcall CreateFileMappingW(long ptr long long long wstr)
#@ stdcall CreateFileW(wstr long long ptr long long long)
#@ stdcall CreateHardLinkA(str str ptr)
#@ stdcall CreateHardLinkW(wstr wstr ptr)
#@ stdcall CreateIoCompletionPort(long long long long)
#@ stdcall CreateMemoryResourceNotification(long)
#@ stdcall CreateMutexA(ptr long str)
@ stdcall CreateMutexExA(ptr str long long) kernelbase.CreateMutexExA
@ stdcall CreateMutexExW(ptr wstr long long) kernelbase.CreateMutexExW
#@ stdcall CreateMutexW(ptr long wstr)
#@ stdcall CreateNamedPipeW(wstr long long long long long long ptr)
#@ stdcall CreatePipe(ptr ptr ptr long)
@ stdcall CreatePrivateNamespaceW(ptr ptr wstr) kernelbase.CreatePrivateNamespaceW
@ stdcall CreatePrivateObjectSecurity(ptr ptr ptr long long ptr) kernelbase.CreatePrivateObjectSecurity
@ stdcall CreatePrivateObjectSecurityEx(ptr ptr ptr ptr long long long ptr) kernelbase.CreatePrivateObjectSecurityEx
@ stdcall CreatePrivateObjectSecurityWithMultipleInheritance(ptr ptr ptr ptr long long long long ptr) kernelbase.CreatePrivateObjectSecurityWithMultipleInheritance
@ stdcall CreateProcessA(str str ptr ptr long long ptr str ptr ptr) kernelbase.CreateProcessA
#@ stdcall CreateProcessAsUserA(long str str ptr ptr long long ptr str ptr ptr)
@ stdcall CreateProcessAsUserW(ptr wstr wstr ptr ptr long long ptr wstr ptr ptr) kernelbase.CreateProcessAsUserW
#@ stdcall CreateProcessInternalA(long str str ptr ptr long long ptr str ptr ptr ptr)
#@ stdcall CreateProcessInternalW(long wstr wstr ptr ptr long long ptr wstr ptr ptr ptr)
@ stdcall CreateProcessW(wstr wstr ptr ptr long long ptr wstr ptr ptr) kernelbase.CreateProcessW
@ stdcall CreatePseudoConsole(long long long long ptr) kernelbase.CreatePseudoConsole
@ stdcall CreateRemoteThread(long ptr long ptr long long ptr) kernelbase.CreateRemoteThread
@ stdcall CreateRemoteThreadEx(long ptr long ptr long long ptr ptr) kernelbase.CreateRemoteThreadEx
@ stdcall CreateRestrictedToken(long long long ptr long ptr long ptr ptr) kernelbase.CreateRestrictedToken
#@ stdcall CreateSemaphoreExW(ptr long long wstr long long)
#@ stdcall CreateSemaphoreW(ptr long long wstr)
# #@ stub CreateStateAtom
# #@ stub CreateStateChangeNotification
# #@ stub CreateStateContainer
# #@ stub CreateStateLock
# #@ stub CreateStateSubcontainer
#@ stdcall CreateSymbolicLinkW(wstr wstr long)
@ stdcall CreateThread(ptr long ptr long long ptr) kernelbase.CreateThread
@ stdcall CreateThreadpool(ptr) kernelbase.CreateThreadpool
@ stdcall CreateThreadpoolCleanupGroup() kernelbase.CreateThreadpoolCleanupGroup
@ stdcall CreateThreadpoolIo(ptr ptr ptr ptr) kernelbase.CreateThreadpoolIo
@ stdcall CreateThreadpoolTimer(ptr ptr ptr) kernelbase.CreateThreadpoolTimer
@ stdcall CreateThreadpoolWait(ptr ptr ptr) kernelbase.CreateThreadpoolWait
@ stdcall CreateThreadpoolWork(ptr ptr ptr) kernelbase.CreateThreadpoolWork
#@ stdcall CreateTimerQueue()
#@ stdcall CreateTimerQueueTimer(ptr long ptr ptr long long long)
#@ stdcall CreateWaitableTimerExW(ptr wstr long long)
#@ stdcall CreateWaitableTimerW(ptr long wstr)
@ stdcall CreateWellKnownSid(long ptr ptr ptr) kernelbase.CreateWellKnownSid
@ stdcall CtrlRoutine(ptr) kernelbase.CtrlRoutine
# #@ stub CveEventWrite
#@ stdcall DeactivateActCtx(long long)
#@ stdcall DebugActiveProcess(long)
#@ stdcall DebugActiveProcessStop(long)
#@ stdcall DebugBreak()
#@ stdcall DecodePointer(ptr) ntdll.RtlDecodePointer
# #@ stub DecodeRemotePointer
#@ stdcall DecodeSystemPointer(ptr) ntdll.RtlDecodeSystemPointer
#@ stdcall DefineDosDeviceW(long wstr wstr)
#@ stdcall DelayLoadFailureHook(str str)
# #@ stub DelayLoadFailureHookLookup
@ stdcall DeriveCapabilitySidsFromName(ptr ptr ptr ptr ptr) kernelbase.DeriveCapabilitySidsFromName
@ stdcall DeleteAce(ptr long) kernelbase.DeleteAce
@ stdcall DeleteBoundaryDescriptor(ptr) kernelbase.DeleteBoundaryDescriptor
#@ stdcall DeleteCriticalSection(ptr) ntdll.RtlDeleteCriticalSection
#@ stdcall DeleteFiber(ptr)
#@ stdcall DeleteFileA(str)
#@ stdcall DeleteFileW(wstr)
@ stdcall -version=0x600+ DeleteProcThreadAttributeList(ptr) kernelbase.DeleteProcThreadAttributeList
# #@ stub DeleteStateAtomValue
# #@ stub DeleteStateContainer
# #@ stub DeleteStateContainerValue
# #@ stub DeleteSynchronizationBarrier
#@ stdcall DeleteTimerQueueEx(long long)
#@ stdcall DeleteTimerQueueTimer(long long long)
#@ stdcall DeleteVolumeMountPointW(wstr)
@ stdcall DestroyPrivateObjectSecurity(ptr) kernelbase.DestroyPrivateObjectSecurity
#@ stdcall DeviceIoControl(long long ptr long ptr long ptr ptr)
@ stdcall DisablePredefinedHandleTableInternal(long) kernelbase.DisablePredefinedHandleTableInternal
#@ stdcall DisableThreadLibraryCalls(long)
#@ stdcall DisassociateCurrentThreadFromCallback(ptr) ntdll.TpDisassociateCallback
#@ stdcall DiscardVirtualMemory(ptr long)
#@ stdcall DisconnectNamedPipe(long)
@ stdcall DnsHostnameToComputerNameExW(wstr ptr ptr) kernelbase.DnsHostnameToComputerNameExW
# #@ stub DsBindWithSpnExW
# #@ stub DsCrackNamesW
# #@ stub DsFreeDomainControllerInfoW
# #@ stub DsFreeNameResultW
# #@ stub DsFreeNgcKey
# #@ stub DsFreePasswordCredentials
# #@ stub DsGetDomainControllerInfoW
# #@ stub DsMakePasswordCredentialsW
# #@ stub DsReadNgcKeyW
# #@ stub DsUnBindW
# #@ stub DsWriteNgcKeyW
#@ stdcall DuplicateHandle(long long long ptr long long long)
# #@ stub DuplicateStateContainerHandle
@ stdcall DuplicateToken(long long ptr) kernelbase.DuplicateToken
@ stdcall DuplicateTokenEx(long long ptr long long ptr) kernelbase.DuplicateTokenEx
#@ stdcall EmptyWorkingSet(long)
#@ stdcall EncodePointer(ptr) ntdll.RtlEncodePointer
# #@ stub EncodeRemotePointer
#@ stdcall EncodeSystemPointer(ptr) ntdll.RtlEncodeSystemPointer
# #@ stub EnterCriticalPolicySectionInternal
#@ stdcall EnterCriticalSection(ptr) ntdll.RtlEnterCriticalSection
# #@ stub EnterSynchronizationBarrier
#@ stdcall EnumCalendarInfoExEx(ptr wstr long wstr long long)
#@ stdcall EnumCalendarInfoExW(ptr long long long)
#@ stdcall EnumCalendarInfoW(ptr long long long)
#@ stdcall EnumDateFormatsExEx(ptr wstr long long)
#@ stdcall EnumDateFormatsExW(ptr long long)
#@ stdcall EnumDateFormatsW(ptr long long)
#@ stdcall EnumDeviceDrivers(ptr long ptr)
#@ stdcall EnumDynamicTimeZoneInformation(long ptr)
#@ stdcall EnumLanguageGroupLocalesW(ptr long long ptr)
#@ stdcall EnumPageFilesA(ptr ptr)
#@ stdcall EnumPageFilesW(ptr ptr)
#@ stdcall EnumProcessModules(long ptr long ptr)
#@ stdcall EnumProcessModulesEx(long ptr long ptr long)
#@ stdcall EnumProcesses(ptr long ptr)
#@ stdcall EnumResourceLanguagesExA(long str str ptr long long long)
#@ stdcall EnumResourceLanguagesExW(long wstr wstr ptr long long long)
#@ stdcall EnumResourceNamesExA(long str ptr long long long)
#@ stdcall EnumResourceNamesExW(long wstr ptr long long long)
#@ stdcall EnumResourceNamesW(long wstr ptr long)
#@ stdcall EnumResourceTypesExA(long ptr long long long)
#@ stdcall EnumResourceTypesExW(long ptr long long long)
#@ stdcall EnumSystemCodePagesW(ptr long)
#@ stdcall EnumSystemFirmwareTables(long ptr long)
@ stdcall EnumSystemGeoID(long long ptr) kernelbase.EnumSystemGeoID
#@ stdcall EnumSystemLanguageGroupsW(ptr long ptr)
@ stdcall EnumSystemLocalesA(ptr long) kernelbase.EnumSystemLocalesA
@ stdcall EnumSystemLocalesEx(ptr long long ptr) kernelbase.EnumSystemLocalesEx
@ stdcall EnumSystemLocalesW(ptr long) kernelbase.EnumSystemLocalesW
#@ stdcall EnumTimeFormatsEx(ptr wstr long long)
#@ stdcall EnumTimeFormatsW(ptr long long)
#@ stdcall EnumUILanguagesW(ptr long long)
# #@ stub EnumerateStateAtomValues
# #@ stub EnumerateStateContainerItems
@ stdcall EqualDomainSid(ptr ptr ptr) kernelbase.EqualDomainSid
@ stdcall EqualPrefixSid(ptr ptr) kernelbase.EqualPrefixSid
@ stdcall EqualSid(ptr ptr) kernelbase.EqualSid
#@ stdcall EscapeCommFunction(long long)
@ stdcall EventActivityIdControl(long ptr) kernelbase.EventActivityIdControl
@ stdcall EventEnabled(int64 ptr) kernelbase.EventEnabled
@ stdcall EventProviderEnabled(int64 long int64) kernelbase.EventProviderEnabled
@ stdcall EventRegister(ptr ptr ptr ptr) kernelbase.EventRegister
@ stdcall EventSetInformation(int64 long ptr long) kernelbase.EventSetInformation
@ stdcall EventUnregister(int64) kernelbase.EventUnregister
@ stdcall EventWrite(int64 ptr long ptr) kernelbase.EventWrite
@ stdcall EventWriteEx(int64 ptr int64 long ptr ptr long ptr) kernelbase.EventWriteEx
@ stdcall EventWriteString(int64 long int64 wstr) kernelbase.EventWriteString
@ stdcall EventWriteTransfer(int64 ptr ptr ptr long ptr) kernelbase.EventWriteTransfer
@ stdcall ExitProcess(long) kernelbase.ExitProcess
@ stdcall ExitThread(long) kernelbase.ExitThread
#@ stdcall ExpandEnvironmentStringsA(str ptr long)
#@ stdcall ExpandEnvironmentStringsW(wstr ptr long)
@ stdcall ExpungeConsoleCommandHistoryA(long) kernelbase.ExpungeConsoleCommandHistoryA
@ stdcall ExpungeConsoleCommandHistoryW(long) kernelbase.ExpungeConsoleCommandHistoryW
#@ stdcall FatalAppExitA(long str)
#@ stdcall FatalAppExitW(long wstr)
#@ stdcall FileTimeToLocalFileTime(ptr ptr)
#@ stdcall FileTimeToSystemTime(ptr ptr)
@ stdcall FillConsoleOutputAttribute(long long long long ptr) kernelbase.FillConsoleOutputAttribute
@ stdcall FillConsoleOutputCharacterA(long long long long ptr) kernelbase.FillConsoleOutputCharacterA
@ stdcall FillConsoleOutputCharacterW(long long long long ptr) kernelbase.FillConsoleOutputCharacterW
#@ stdcall FindActCtxSectionGuid(long ptr long ptr ptr)
#@ stdcall FindActCtxSectionStringW(long ptr long wstr ptr)
#@ stdcall FindClose(long)
#@ stdcall FindCloseChangeNotification(long)
#@ stdcall FindFirstChangeNotificationA(str long long)
#@ stdcall FindFirstChangeNotificationW(wstr long long)
#@ stdcall FindFirstFileA(str ptr)
#@ stdcall FindFirstFileExA(str long ptr long ptr long)
#@ stdcall FindFirstFileExW(wstr long ptr long ptr long)
#@ stdcall FindFirstFileNameW(wstr long ptr ptr)
#@ stdcall FindFirstFileW(wstr ptr)
@ stdcall FindFirstFreeAce(ptr ptr) kernelbase.FindFirstFreeAce
#@ stdcall FindFirstStreamW(wstr long ptr long)
#@ stdcall FindFirstVolumeW(ptr long)
#@ stdcall FindNLSString(long long wstr long wstr long ptr)
@ stdcall FindNLSStringEx(wstr long wstr long wstr long ptr ptr ptr long) kernelbase.FindNLSStringEx
#@ stdcall FindNextChangeNotification(long)
#@ stdcall FindNextFileA(long ptr)
# #@ stub FindNextFileNameW
#@ stdcall FindNextFileW(long ptr)
#@ stdcall FindNextStreamW(long ptr)
#@ stdcall FindNextVolumeW(long ptr long)
@ stdcall FindPackagesByPackageFamily(wstr long ptr ptr ptr ptr ptr) kernelbase.FindPackagesByPackageFamily
#@ stdcall FindResourceExW(long wstr wstr long)
#@ stdcall FindResourceW(long wstr wstr)
@ stdcall FindStringOrdinal(long wstr long wstr long long) kernelbase.FindStringOrdinal
#@ stdcall FindVolumeClose(ptr)
#@ stdcall FlsAlloc(ptr)
#@ stdcall FlsFree(long)
#@ stdcall FlsGetValue(long)
#@ stdcall FlsSetValue(long ptr)
@ stdcall FlushConsoleInputBuffer(long) kernelbase.FlushConsoleInputBuffer
#@ stdcall FlushFileBuffers(long)
#@ stdcall FlushInstructionCache(long long long)
@ stdcall -version=0x600+ FlushProcessWriteBuffers() kernelbase.FlushProcessWriteBuffers
#@ stdcall FlushViewOfFile(ptr long)
#@ stdcall FoldStringW(long wstr long ptr long)
# #@ stub ForceSyncFgPolicyInternal
# #@ stub FormatApplicationUserModelId
#@ stdcall FormatMessageA(long ptr long long ptr long ptr)
#@ stdcall FormatMessageW(long ptr long long ptr long ptr)
@ stdcall FreeConsole() kernelbase.FreeConsole
#@ stdcall FreeEnvironmentStringsA(ptr) FreeEnvironmentStringsW
#@ stdcall FreeEnvironmentStringsW(ptr)
# #@ stub FreeGPOListInternalA
# #@ stub FreeGPOListInternalW
#@ stdcall FreeLibrary(long)
#@ stdcall FreeLibraryAndExitThread(long long)
#@ stdcall FreeLibraryWhenCallbackReturns(ptr ptr) ntdll.TpCallbackUnloadDllOnCompletion
#@ stdcall FreeResource(long)
@ stdcall FreeSid(ptr) kernelbase.FreeSid
#@ stdcall FreeUserPhysicalPages(long ptr ptr)
@ stdcall GenerateConsoleCtrlEvent(long long) kernelbase.GenerateConsoleCtrlEvent
# #@ stub GenerateGPNotificationInternal
#@ stdcall GetACP()
#@ stdcall GetAcceptLanguagesA(ptr ptr)
#@ stdcall GetAcceptLanguagesW(ptr ptr)
@ stdcall GetAce(ptr long ptr) kernelbase.GetAce
@ stdcall GetAclInformation(ptr ptr long long) kernelbase.GetAclInformation
# #@ stub GetAdjustObjectAttributesForPrivateNamespaceRoutine
# #@ stub GetAlternatePackageRoots
@ stdcall GetAppContainerAce(ptr long ptr ptr) kernelbase.GetAppContainerAce
@ stdcall GetAppContainerNamedObjectPath(ptr ptr long ptr ptr) kernelbase.GetAppContainerNamedObjectPath
# #@ stub GetAppDataFolder
# #@ stub GetAppModelVersion
# #@ stub GetApplicationRecoveryCallback
#@ stdcall GetApplicationRestartSettings(long ptr ptr ptr)
# #@ stub GetApplicationUserModelId
@ stdcall GetApplicationUserModelIdFromToken(ptr ptr ptr) kernelbase.GetApplicationUserModelIdFromToken
# #@ stub GetAppliedGPOListInternalA
# #@ stub GetAppliedGPOListInternalW
#@ stub GetCPFileNameFromRegistry
#@ stub GetCPHashNode
#@ stdcall GetCPInfo(long ptr)
#@ stdcall GetCPInfoExW(long long ptr)
# #@ stub GetCachedSigningLevel
#@ stub GetCalendar
#@ stdcall GetCalendarInfoEx(wstr long ptr long ptr long ptr)
#@ stdcall GetCalendarInfoW(long long long ptr long ptr)
#@ stdcall GetCommConfig(long ptr ptr)
#@ stdcall GetCommMask(long ptr)
#@ stdcall GetCommModemStatus(long ptr)
#@ stdcall GetCommProperties(long ptr)
#@ stdcall GetCommState(long ptr)
#@ stdcall GetCommTimeouts(long ptr)
#@ stdcall GetCommandLineA()
#@ stdcall GetCommandLineW()
#@ stdcall GetCompressedFileSizeA(str ptr)
#@ stdcall GetCompressedFileSizeW(wstr ptr)
@ stdcall GetComputerNameExA(long ptr ptr) kernelbase.GetComputerNameExA
@ stdcall GetComputerNameExW(long ptr ptr) kernelbase.GetComputerNameExW
@ stdcall GetConsoleAliasA(str str long str) kernelbase.GetConsoleAliasA
##@ stub GetConsoleAliasExesA
@ stdcall GetConsoleAliasExesLengthA() kernelbase.GetConsoleAliasExesLengthA
@ stdcall GetConsoleAliasExesLengthW() kernelbase.GetConsoleAliasExesLengthW
##@ stub GetConsoleAliasExesW
@ stdcall GetConsoleAliasW(wstr ptr long wstr) kernelbase.GetConsoleAliasW
##@ stub GetConsoleAliasesA
@ stdcall GetConsoleAliasesLengthA(str) kernelbase.GetConsoleAliasesLengthA
@ stdcall GetConsoleAliasesLengthW(wstr) kernelbase.GetConsoleAliasesLengthW
##@ stub GetConsoleAliasesW
@ stdcall GetConsoleCP() kernelbase.GetConsoleCP
@ stdcall GetConsoleCommandHistoryA(long long long) kernelbase.GetConsoleCommandHistoryA
@ stdcall GetConsoleCommandHistoryLengthA(long) kernelbase.GetConsoleCommandHistoryLengthA
@ stdcall GetConsoleCommandHistoryLengthW(long) kernelbase.GetConsoleCommandHistoryLengthW
@ stdcall GetConsoleCommandHistoryW(long long long) kernelbase.GetConsoleCommandHistoryW
@ stdcall GetConsoleCursorInfo(long ptr) kernelbase.GetConsoleCursorInfo
@ stdcall GetConsoleDisplayMode(ptr) kernelbase.GetConsoleDisplayMode
@ stdcall GetConsoleFontSize(long long) kernelbase.GetConsoleFontSize
@ stdcall GetConsoleInputExeNameA(long ptr) kernelbase.GetConsoleInputExeNameA
@ stdcall GetConsoleInputExeNameW(long ptr) kernelbase.GetConsoleInputExeNameW
@ stdcall GetConsoleMode(long ptr) kernelbase.GetConsoleMode
#@ stdcall GetConsoleOriginalTitleA(ptr long)
#@ stdcall GetConsoleOriginalTitleW(ptr long)
@ stdcall GetConsoleOutputCP() kernelbase.GetConsoleOutputCP
@ stdcall GetConsoleProcessList(ptr long) kernelbase.GetConsoleProcessList
@ stdcall GetConsoleScreenBufferInfo(long ptr) kernelbase.GetConsoleScreenBufferInfo
#@ stdcall GetConsoleScreenBufferInfoEx(long ptr)
@ stdcall GetConsoleTitleA(ptr long) kernelbase.GetConsoleTitleA
@ stdcall GetConsoleTitleW(ptr long) kernelbase.GetConsoleTitleW
@ stdcall GetConsoleWindow() kernelbase.GetConsoleWindow
#@ stdcall GetCurrencyFormatEx(wstr long wstr ptr ptr long)
#@ stdcall GetCurrencyFormatW(long long wstr ptr ptr long)
#@ stdcall GetCurrentActCtx(ptr)
@ stdcall GetCurrentApplicationUserModelId(ptr ptr) kernelbase.GetCurrentApplicationUserModelId
@ stdcall GetCurrentConsoleFont(long long ptr) kernelbase.GetCurrentConsoleFont
#@ stdcall GetCurrentConsoleFontEx(long long ptr)
#@ stdcall GetCurrentDirectoryA(long ptr)
#@ stdcall GetCurrentDirectoryW(long ptr)
# #@ stub GetCurrentPackageApplicationContext
# #@ stub GetCurrentPackageApplicationResourcesContext
# #@ stub GetCurrentPackageContext
#@ stdcall GetCurrentPackageFamilyName(ptr ptr)
#@ stdcall GetCurrentPackageFullName(ptr ptr)
#@ stdcall GetCurrentPackageId(ptr ptr)
#@ stdcall GetCurrentPackageInfo(long ptr ptr ptr)
#@ stdcall GetCurrentPackagePath(ptr ptr)
# #@ stub GetCurrentPackageResourcesContext
# #@ stub GetCurrentPackageSecurityContext
@ stdcall -norelay GetCurrentProcess() kernelbase.GetCurrentProcess
@ stdcall -norelay GetCurrentProcessId() kernelbase.GetCurrentProcessId
#@ stdcall GetCurrentProcessorNumber() ntdll.NtGetCurrentProcessorNumber
#@ stdcall GetCurrentProcessorNumberEx(ptr) ntdll.RtlGetCurrentProcessorNumberEx
# #@ stub GetCurrentTargetPlatformContext
@ stdcall -norelay GetCurrentThread() kernelbase.GetCurrentThread
@ stdcall -norelay GetCurrentThreadId() kernelbase.GetCurrentThreadId
@ stdcall GetCurrentThreadStackLimits(ptr ptr) kernelbase.GetCurrentThreadStackLimits
#@ stdcall GetDateFormatA(long long ptr str ptr long)
#@ stdcall GetDateFormatEx(wstr long ptr wstr ptr long wstr)
#@ stdcall GetDateFormatW(long long ptr wstr ptr long)
#@ stdcall GetDeviceDriverBaseNameA(ptr ptr long)
#@ stdcall GetDeviceDriverBaseNameW(ptr ptr long)
#@ stdcall GetDeviceDriverFileNameA(ptr ptr long)
#@ stdcall GetDeviceDriverFileNameW(ptr ptr long)
#@ stdcall GetDiskFreeSpaceA(str ptr ptr ptr ptr)
#@ stdcall GetDiskFreeSpaceExA(str ptr ptr ptr)
#@ stdcall GetDiskFreeSpaceExW(wstr ptr ptr ptr)
#@ stdcall GetDiskFreeSpaceW(wstr ptr ptr ptr ptr)
#@ stdcall GetDriveTypeA(str)
#@ stdcall GetDriveTypeW(wstr)
# #@ stub GetDurationFormatEx
#@ stdcall GetDynamicTimeZoneInformation(ptr)
#@ stdcall GetDynamicTimeZoneInformationEffectiveYears(ptr ptr ptr)
# #@ stub GetEffectivePackageStatusForUser
# #@ stub GetEightBitStringToUnicodeSizeRoutine
# #@ stub GetEightBitStringToUnicodeStringRoutine
#@ stdcall -ret64 -arch=i386,x86_64 GetEnabledXStateFeatures()
#@ stdcall GetEnvironmentStrings() GetEnvironmentStringsA
#@ stdcall GetEnvironmentStringsA()
#@ stdcall GetEnvironmentStringsW()
#@ stdcall GetEnvironmentVariableA(str ptr long)
#@ stdcall GetEnvironmentVariableW(wstr ptr long)
#@ stub GetEraNameCountedString
#@ stdcall GetErrorMode()
@ stdcall GetExitCodeProcess(long ptr) kernelbase.GetExitCodeProcess
@ stdcall GetExitCodeThread(long ptr) kernelbase.GetExitCodeThread
#@ stub GetFallbackDisplayName
#@ stdcall GetFileAttributesA(str)
#@ stdcall GetFileAttributesExA(str long ptr)
#@ stdcall GetFileAttributesExW(wstr long ptr)
#@ stdcall GetFileAttributesW(wstr)
#@ stdcall GetFileInformationByHandle(long ptr)
#@ stdcall GetFileInformationByHandleEx(long long ptr long)
#@ stdcall GetFileMUIInfo(long wstr ptr ptr)
#@ stdcall GetFileMUIPath(long wstr wstr ptr ptr ptr ptr)
@ stdcall GetFileSecurityW(wstr long ptr long ptr) kernelbase.GetFileSecurityW
#@ stdcall GetFileSize(long ptr)
#@ stdcall GetFileSizeEx(long ptr)
#@ stdcall GetFileTime(long ptr ptr ptr)
#@ stdcall GetFileType(long)
@ stdcall GetFileVersionInfoA(str long long ptr) kernelbase.GetFileVersionInfoA
# #@ stub GetFileVersionInfoByHandle
@ stdcall GetFileVersionInfoExA(long str long long ptr) kernelbase.GetFileVersionInfoExA
@ stdcall GetFileVersionInfoExW(long wstr long long ptr) kernelbase.GetFileVersionInfoExW
@ stdcall GetFileVersionInfoSizeA(str ptr) kernelbase.GetFileVersionInfoSizeA
@ stdcall GetFileVersionInfoSizeExA(long str ptr) kernelbase.GetFileVersionInfoSizeExA
@ stdcall GetFileVersionInfoSizeExW(long wstr ptr) kernelbase.GetFileVersionInfoSizeExW
@ stdcall GetFileVersionInfoSizeW(wstr ptr) kernelbase.GetFileVersionInfoSizeW
@ stdcall GetFileVersionInfoW(wstr long long ptr) kernelbase.GetFileVersionInfoW
#@ stdcall GetFinalPathNameByHandleA(long ptr long long)
#@ stdcall GetFinalPathNameByHandleW(long ptr long long)
#@ stdcall GetFullPathNameA(str long ptr ptr)
#@ stdcall GetFullPathNameW(wstr long ptr ptr)
# #@ stub GetGPOListInternalA
# #@ stub GetGPOListInternalW
@ stdcall GetGeoInfoW(long long ptr long long) kernelbase.GetGeoInfoW
@ stdcall GetGeoInfoEx(ptr long ptr long) kernelbase.GetGeoInfoEx
#@ stdcall GetHandleInformation(long ptr)
# #@ stub GetHivePath
@ stdcall GetIntegratedDisplaySize(ptr) kernelbase.GetIntegratedDisplaySize
# #@ stub GetIsEdpEnabled
@ stdcall GetKernelObjectSecurity(long long ptr long ptr) kernelbase.GetKernelObjectSecurity
#@ stdcall GetLargePageMinimum()
@ stdcall GetLargestConsoleWindowSize(long) kernelbase.GetLargestConsoleWindowSize
#@ stdcall GetLastError() kernelbase_GetLastError
@ stdcall GetLengthSid(ptr) kernelbase.GetLengthSid
#@ stdcall GetLocalTime(ptr)
#@ stdcall GetLocaleInfoA(long long ptr long)
#@ stdcall GetLocaleInfoEx(wstr long ptr long)
#@ stub GetLocaleInfoHelper
#@ stdcall GetLocaleInfoW(long long ptr long)
#@ stdcall GetLogicalDriveStringsW(long ptr)
#@ stdcall GetLogicalDrives()
#@ stdcall GetLogicalProcessorInformation(ptr ptr)
#@ stdcall GetLogicalProcessorInformationEx(long ptr ptr)
#@ stdcall GetLongPathNameA(str ptr long)
#@ stdcall GetLongPathNameW(wstr ptr long)
#@ stdcall GetMappedFileNameA(long ptr ptr long)
#@ stdcall GetMappedFileNameW(long ptr ptr long)
# #@ stub GetMemoryErrorHandlingCapabilities
#@ stdcall GetModuleBaseNameA(long long ptr long)
#@ stdcall GetModuleBaseNameW(long long ptr long)
#@ stdcall GetModuleFileNameA(long ptr long)
#@ stdcall GetModuleFileNameExA(long long ptr long)
#@ stdcall GetModuleFileNameExW(long long ptr long)
#@ stdcall GetModuleFileNameW(long ptr long)
#@ stdcall GetModuleHandleA(str)
#@ stdcall GetModuleHandleExA(long ptr ptr)
#@ stdcall GetModuleHandleExW(long ptr ptr)
#@ stdcall GetModuleHandleW(wstr)
#@ stdcall GetModuleInformation(long long ptr long)
@ stdcall GetNLSVersion(long long ptr) kernelbase.GetNLSVersion
@ stdcall GetNLSVersionEx(long wstr ptr) kernelbase.GetNLSVersionEx
#@ stub GetNamedLocaleHashNode
#@ stub GetNamedPipeAttribute
#@ stub GetNamedPipeClientComputerNameW
#@ stdcall GetNamedPipeHandleStateW(long ptr ptr ptr ptr ptr long)
#@ stdcall GetNamedPipeInfo(long ptr ptr ptr ptr)
#@ stdcall GetNativeSystemInfo(ptr)
# #@ stub GetNextFgPolicyRefreshInfoInternal
#@ stdcall GetNumaHighestNodeNumber(ptr)
#@ stdcall GetNumaNodeProcessorMaskEx(long ptr)
#@ stdcall GetNumaProximityNodeEx(long ptr)
#@ stdcall GetNumberFormatEx(wstr long wstr ptr ptr long)
#@ stdcall GetNumberFormatW(long long wstr ptr ptr long)
@ stdcall GetNumberOfConsoleInputEvents(long ptr) kernelbase.GetNumberOfConsoleInputEvents
@ stdcall GetNumberOfConsoleMouseButtons(ptr) kernelbase.GetNumberOfConsoleMouseButtons
#@ stdcall GetOEMCP()
# #@ stub GetOsManufacturingMode
# #@ stub GetOsSafeBootMode
#@ stdcall GetOverlappedResult(long ptr ptr long)
#@ stdcall GetOverlappedResultEx(long ptr ptr long long)
# #@ stub GetPackageApplicationContext
# #@ stub GetPackageApplicationIds
# #@ stub GetPackageApplicationProperty
# #@ stub GetPackageApplicationPropertyString
# #@ stub GetPackageApplicationResourcesContext
# #@ stub GetPackageContext
@ stdcall GetPackageFamilyName(long ptr ptr) kernelbase.GetPackageFamilyName
@ stdcall GetPackageFamilyNameFromToken(ptr ptr ptr) kernelbase.GetPackageFamilyNameFromToken
#@ stdcall GetPackageFullName(long ptr ptr)
@ stdcall GetPackageFullNameFromToken(ptr ptr ptr) kernelbase.GetPackageFullNameFromToken
# #@ stub GetPackageId
# #@ stub GetPackageInfo
# #@ stub GetPackageInstallTime
# #@ stub GetPackageOSMaxVersionTested
# #@ stub GetPackagePath
#@ stdcall GetPackagePathByFullName(wstr ptr wstr)
# #@ stub GetPackagePathOnVolume
# #@ stub GetPackageProperty
# #@ stub GetPackagePropertyString
# #@ stub GetPackageResourcesContext
# #@ stub GetPackageResourcesProperty
# #@ stub GetPackageSecurityContext
# #@ stub GetPackageSecurityProperty
# #@ stub GetPackageStatus
# #@ stub GetPackageStatusForUser
# #@ stub GetPackageTargetPlatformProperty
# #@ stub GetPackageVolumeSisPath
#@ stdcall GetPackagesByPackageFamily(wstr ptr ptr ptr ptr)
#@ stdcall GetPerformanceInfo(ptr long)
#@ stdcall GetPhysicallyInstalledSystemMemory(ptr)
# #@ stub GetPreviousFgPolicyRefreshInfoInternal
@ stdcall GetPriorityClass(long) kernelbase.GetPriorityClass
@ stdcall GetPrivateObjectSecurity(ptr long ptr long ptr) kernelbase.GetPrivateObjectSecurity
#@ stdcall GetProcAddress(long str)
# #@ stub GetProcAddressForCaller
# #@ stub GetProcessDefaultCpuSets
#@ stdcall GetProcessGroupAffinity(long ptr ptr)
#@ stdcall GetProcessHandleCount(long ptr)
#@ stdcall -norelay GetProcessHeap() kernelbase_GetProcessHeap
#@ stdcall -import GetProcessHeaps(long ptr) RtlGetProcessHeaps
@ stdcall GetProcessId(long) kernelbase.GetProcessId
@ stdcall GetProcessIdOfThread(ptr) kernelbase.GetProcessIdOfThread
#@ stdcall GetProcessImageFileNameA(long ptr long)
#@ stdcall GetProcessImageFileNameW(long ptr long)
@ stdcall GetProcessInformation(long long ptr long) kernelbase.GetProcessInformation
#@ stdcall GetProcessMemoryInfo(long ptr long)
@ stdcall GetProcessMitigationPolicy(long long ptr long) kernelbase.GetProcessMitigationPolicy
@ stdcall GetProcessPreferredUILanguages(long ptr ptr ptr) kernelbase.GetProcessPreferredUILanguages
#@ stdcall GetProcessPriorityBoost(long ptr)
#@ stdcall GetProcessShutdownParameters(ptr ptr)
@ stdcall GetProcessTimes(long ptr ptr ptr ptr) kernelbase.GetProcessTimes
@ stdcall GetProcessVersion(long) kernelbase.GetProcessVersion
#@ stdcall GetProcessWorkingSetSizeEx(long ptr ptr ptr)
# #@ stub GetProcessorSystemCycleTime
#@ stdcall GetProductInfo(long long long long ptr)
#@ stub GetPtrCalData
#@ stub GetPtrCalDataArray
# #@ stub GetPublisherCacheFolder
# #@ stub GetPublisherRootFolder
#@ stdcall GetQueuedCompletionStatus(long ptr ptr ptr long)
#@ stdcall GetQueuedCompletionStatusEx(ptr ptr long ptr long long)
# #@ stub GetRegistryExtensionFlags
# #@ stub GetRoamingLastObservedChangeTime
@ stdcall GetSecurityDescriptorControl(ptr ptr ptr) kernelbase.GetSecurityDescriptorControl
@ stdcall GetSecurityDescriptorDacl(ptr ptr ptr ptr) kernelbase.GetSecurityDescriptorDacl
@ stdcall GetSecurityDescriptorGroup(ptr ptr ptr) kernelbase.GetSecurityDescriptorGroup
@ stdcall GetSecurityDescriptorLength(ptr) kernelbase.GetSecurityDescriptorLength
@ stdcall GetSecurityDescriptorOwner(ptr ptr ptr) kernelbase.GetSecurityDescriptorOwner
#@ stub GetSecurityDescriptorRMControl
@ stdcall GetSecurityDescriptorSacl(ptr ptr ptr ptr) kernelbase.GetSecurityDescriptorSacl
# #@ stub GetSerializedAtomBytes
# #@ stub GetSharedLocalFolder
#@ stdcall GetShortPathNameW(wstr ptr long)
@ stdcall GetSidIdentifierAuthority(ptr) kernelbase.GetSidIdentifierAuthority
@ stdcall GetSidLengthRequired(long) kernelbase.GetSidLengthRequired
@ stdcall GetSidSubAuthority(ptr long) kernelbase.GetSidSubAuthority
@ stdcall GetSidSubAuthorityCount(ptr) kernelbase.GetSidSubAuthorityCount
# #@ stub GetStagedPackageOrigin
# #@ stub GetStagedPackagePathByFullName
@ stdcall GetStartupInfoW(ptr) kernelbase.GetStartupInfoW
# #@ stub GetStateContainerDepth
# #@ stub GetStateFolder
# #@ stub GetStateRootFolder
# #@ stub GetStateRootFolderBase
# #@ stub GetStateSettingsFolder
# #@ stub GetStateVersion
@ stdcall GetStdHandle(long) kernelbase.GetStdHandle
# #@ stub GetStringScripts
#@ stub GetStringTableEntry
@ stdcall GetStringTypeA(long long str long ptr) kernelbase.GetStringTypeA
@ stdcall GetStringTypeExW(long long wstr long ptr) kernelbase.GetStringTypeExW
@ stdcall GetStringTypeW(long wstr long ptr) kernelbase.GetStringTypeW
# #@ stub GetSystemAppDataFolder
# #@ stub GetSystemAppDataKey
@ stdcall GetSystemCpuSetInformation(ptr long ptr ptr long) kernelbase.GetSystemCpuSetInformation
#@ stdcall GetSystemDefaultLCID()
#@ stdcall GetSystemDefaultLangID()
#@ stdcall GetSystemDefaultLocaleName(ptr long)
#@ stdcall GetSystemDefaultUILanguage()
#@ stdcall GetSystemDirectoryA(ptr long)
#@ stdcall GetSystemDirectoryW(ptr long)
#@ stdcall GetSystemFileCacheSize(ptr ptr ptr)
#@ stdcall GetSystemFirmwareTable(long long ptr long)
#@ stdcall GetSystemInfo(ptr)
# #@ stub GetSystemMetadataPath
# #@ stub GetSystemMetadataPathForPackage
# #@ stub GetSystemMetadataPathForPackageFamily
@ stdcall GetSystemPreferredUILanguages(long ptr ptr ptr) kernelbase.GetSystemPreferredUILanguages
# #@ stub GetSystemStateRootFolder
#@ stdcall GetSystemTime(ptr)
#@ stdcall GetSystemTimeAdjustment(ptr ptr ptr)
#@ stdcall GetSystemTimeAsFileTime(ptr)
#@ stdcall GetSystemTimePreciseAsFileTime(ptr)
#@ stdcall GetSystemTimes(ptr ptr ptr)
#@ stdcall GetSystemWindowsDirectoryA(ptr long)
#@ stdcall GetSystemWindowsDirectoryW(ptr long)
#@ stdcall GetSystemWow64Directory2A(ptr long long)
#@ stdcall GetSystemWow64Directory2W(ptr long long)
#@ stdcall GetSystemWow64DirectoryA(ptr long)
#@ stdcall GetSystemWow64DirectoryW(ptr long)
# #@ stub GetTargetPlatformContext
#@ stdcall GetTempFileNameA(str str long ptr)
#@ stdcall GetTempFileNameW(wstr wstr long ptr)
#@ stdcall GetTempPath2W(long ptr)
#@ stdcall GetTempPath2A(long ptr)
#@ stdcall GetTempPathA(long ptr)
#@ stdcall GetTempPathW(long ptr)
#@ stdcall GetThreadContext(long ptr)
@ stdcall GetThreadDescription(ptr ptr) kernelbase.GetThreadDescription
@ stdcall GetThreadErrorMode() kernelbase.GetThreadErrorMode
#@ stdcall GetThreadGroupAffinity(long ptr)
#@ stdcall GetThreadIOPendingFlag(long ptr)
@ stdcall GetThreadId(ptr) kernelbase.GetThreadId
@ stdcall GetThreadIdealProcessorEx(long ptr) kernelbase.GetThreadIdealProcessorEx
@ stdcall GetThreadInformation(long long ptr long) kernelbase.GetThreadInformation
#@ stdcall GetThreadLocale()
@ stdcall GetThreadPreferredUILanguages(long ptr ptr ptr) kernelbase.GetThreadPreferredUILanguages
@ stdcall GetThreadPriority(long) kernelbase.GetThreadPriority
@ stdcall GetThreadPriorityBoost(long ptr) kernelbase.GetThreadPriorityBoost
# #@ stub GetThreadSelectedCpuSets
#@ stdcall GetThreadTimes(long ptr ptr ptr ptr)
#@ stdcall GetThreadUILanguage()
#@ stdcall GetTickCount()
#@ stdcall -ret64 GetTickCount64()
#@ stdcall GetTimeFormatA(long long ptr str ptr long)
#@ stdcall GetTimeFormatEx(wstr long ptr wstr ptr long)
#@ stdcall GetTimeFormatW(long long ptr wstr ptr long)
#@ stdcall GetTimeZoneInformation(ptr)
#@ stdcall GetTimeZoneInformationForYear(long ptr ptr)
@ stdcall GetTokenInformation(long long ptr long ptr) kernelbase.GetTokenInformation
#@ stdcall GetTraceEnableFlags(int64) ntdll.EtwGetTraceEnableFlags
#@ stdcall GetTraceEnableLevel(int64) ntdll.EtwGetTraceEnableLevel
#@ stdcall -ret64 GetTraceLoggerHandle(ptr) ntdll.EtwGetTraceLoggerHandle
#@ stub GetUILanguageInfo
# #@ stub GetUnicodeStringToEightBitSizeRoutine
# #@ stub GetUnicodeStringToEightBitStringRoutine
@ stdcall GetUserDefaultGeoName(ptr long) kernelbase.GetUserDefaultGeoName
#@ stdcall GetUserDefaultLCID()
#@ stdcall GetUserDefaultLangID()
#@ stdcall GetUserDefaultLocaleName(ptr long)
#@ stdcall GetUserDefaultUILanguage()
@ stdcall GetUserGeoID(long) kernelbase.GetUserGeoID
#@ stub GetUserInfo
#@ stub GetUserInfoWord
# #@ stub GetUserOverrideString
# #@ stub GetUserOverrideWord
@ stdcall GetUserPreferredUILanguages(long ptr ptr ptr) kernelbase.GetUserPreferredUILanguages
#@ stdcall GetVersion()
#@ stdcall GetVersionExA(ptr)
#@ stdcall GetVersionExW(ptr)
#@ stdcall GetVolumeInformationA(str ptr long ptr ptr ptr ptr long)
@ stdcall GetVolumeInformationByHandleW(ptr ptr long ptr ptr ptr ptr long) kernelbase.GetVolumeInformationByHandleW
#@ stdcall GetVolumeInformationW(wstr ptr long ptr ptr ptr ptr long)
#@ stdcall GetVolumeNameForVolumeMountPointW(wstr ptr long)
#@ stdcall GetVolumePathNameW(wstr ptr long)
#@ stdcall GetVolumePathNamesForVolumeNameW(wstr ptr long ptr)
@ stdcall GetWindowsAccountDomainSid(ptr ptr ptr) kernelbase.GetWindowsAccountDomainSid
#@ stdcall GetWindowsDirectoryA(ptr long)
#@ stdcall GetWindowsDirectoryW(ptr long)
#@ stdcall GetWriteWatch(long ptr long ptr ptr ptr)
#@ stdcall GetWsChanges(long ptr long)
#@ stdcall GetWsChangesEx(long ptr ptr)
@ stdcall -arch=x86_64,arm64ec GetXStateFeaturesMask(ptr ptr) kernelbase.GetXStateFeaturesMask
#@ stdcall GlobalAlloc(long long)
#@ stdcall GlobalFree(long)
#@ stdcall GlobalMemoryStatusEx(ptr)
# #@ stub GuardCheckLongJumpTarget
# #@ stub HasPolicyForegroundProcessingCompletedInternal
@ stdcall HashData(ptr long ptr long) kernelbase.HashData
#@ stdcall HeapAlloc(long long long) ntdll.RtlAllocateHeap
#@ stdcall HeapCompact(long long)
#@ stdcall HeapCreate(long long long)
#@ stdcall HeapDestroy(long)
#@ stdcall HeapFree(long long ptr) ntdll.RtlFreeHeap
#@ stdcall HeapLock(long)
#@ stdcall HeapQueryInformation(long long ptr long ptr)
#@ stdcall HeapReAlloc(long long ptr long) ntdll.RtlReAllocateHeap
#@ stdcall HeapSetInformation(ptr long ptr long)
#@ stdcall HeapSize(long long ptr) ntdll.RtlSizeHeap
#@ stub HeapSummary
#@ stdcall HeapUnlock(long)
#@ stdcall HeapValidate(long long ptr)
#@ stdcall HeapWalk(long ptr)
#@ stdcall IdnToAscii(long wstr long ptr long)
#@ stdcall IdnToNameprepUnicode(long wstr long ptr long)
#@ stdcall IdnToUnicode(long wstr long ptr long)
@ stdcall ImpersonateAnonymousToken(long) kernelbase.ImpersonateAnonymousToken
@ stdcall ImpersonateLoggedOnUser(long) kernelbase.ImpersonateLoggedOnUser
@ stdcall ImpersonateNamedPipeClient(long) kernelbase.ImpersonateNamedPipeClient
@ stdcall ImpersonateSelf(long) kernelbase.ImpersonateSelf
# #@ stub IncrementPackageStatusVersion
@ stdcall InitOnceBeginInitialize(ptr long ptr ptr) kernelbase.InitOnceBeginInitialize
@ stdcall InitOnceComplete(ptr long ptr) kernelbase.InitOnceComplete
@ stdcall InitOnceExecuteOnce(ptr ptr ptr ptr) kernelbase.InitOnceExecuteOnce
#@ stdcall InitOnceInitialize(ptr) ntdll.RtlRunOnceInitialize
@ stdcall InitializeAcl(ptr long long) kernelbase.InitializeAcl
@ stdcall InitializeConditionVariable(ptr) kernelbase.InitializeConditionVariable
@ stdcall -arch=win64 InitializeContext(ptr long ptr ptr) kernelbase.InitializeContext
@ stdcall -arch=win64 InitializeContext2(ptr long ptr ptr int64) kernelbase.InitializeContext2
#@ stdcall InitializeCriticalSection(ptr) ntdll.RtlInitializeCriticalSection
#@ stdcall InitializeCriticalSectionAndSpinCount(ptr long)
#@ stdcall InitializeCriticalSectionEx(ptr long long)
# #@ stub InitializeEnclave
@ stdcall -version=0x600+ InitializeProcThreadAttributeList(ptr long long ptr) kernelbase.InitializeProcThreadAttributeList
#@ stdcall InitializeProcessForWsWatch(long)
#@ stdcall InitializeSListHead(ptr) ntdll.RtlInitializeSListHead
#@ stdcall InitializeSRWLock(ptr) ntdll.RtlInitializeSRWLock
@ stdcall InitializeSecurityDescriptor(ptr long) kernelbase.InitializeSecurityDescriptor
@ stdcall InitializeSid(ptr ptr long) kernelbase.InitializeSid
# #@ stub InitializeSynchronizationBarrier
# #@ stub InstallELAMCertificateInfo
#@ stdcall -arch=i386 InterlockedCompareExchange(ptr long long)
#@ stdcall -arch=i386 -ret64 InterlockedCompareExchange64(ptr int64 int64) ntdll.RtlInterlockedCompareExchange64
#@ stdcall -arch=i386 InterlockedDecrement(ptr)
#@ stdcall -arch=i386 InterlockedExchange(ptr long)
#@ stdcall -arch=i386 InterlockedExchangeAdd(ptr long )
#@ stdcall InterlockedFlushSList(ptr) ntdll.RtlInterlockedFlushSList
#@ stdcall -arch=i386 InterlockedIncrement(ptr)
#@ stdcall InterlockedPopEntrySList(ptr) ntdll.RtlInterlockedPopEntrySList
#@ stdcall InterlockedPushEntrySList(ptr ptr) ntdll.RtlInterlockedPushEntrySList
#@ stdcall -fastcall InterlockedPushListSList(ptr ptr ptr long) ntdll.RtlInterlockedPushListSList
#@ stdcall InterlockedPushListSListEx(ptr ptr ptr long) ntdll.RtlInterlockedPushListSListEx
#@ stub InternalLcidToName
#@ stdcall Internal_EnumCalendarInfo(ptr long long long long long long long)
#@ stdcall Internal_EnumDateFormats(ptr long long long long long long)
#@ stdcall Internal_EnumLanguageGroupLocales(ptr long long ptr long)
#@ stdcall Internal_EnumSystemCodePages(ptr long long)
#@ stdcall Internal_EnumSystemLanguageGroups(ptr long ptr long)
#@ stub Internal_EnumSystemLocales
#@ stdcall Internal_EnumTimeFormats(ptr long long long long long)
#@ stdcall Internal_EnumUILanguages(ptr long long long)
# #@ stub InternetTimeFromSystemTimeA
# #@ stub InternetTimeFromSystemTimeW
# #@ stub InternetTimeToSystemTimeA
# #@ stub InternetTimeToSystemTimeW
# #@ stub InvalidateAppModelVersionCache
#@ stub InvalidateTzSpecificCache
#@ stdcall IsApiSetImplemented(str)
#@ stdcall IsCharAlphaA(long)
#@ stdcall IsCharAlphaNumericA(long)
#@ stdcall IsCharAlphaNumericW(long)
#@ stdcall IsCharAlphaW(long)
#@ stdcall IsCharBlankW(long)
#@ stdcall IsCharCntrlW(long)
#@ stdcall IsCharDigitW(long)
#@ stdcall IsCharLowerA(long)
#@ stdcall IsCharLowerW(long)
#@ stdcall IsCharPunctW(long)
#@ stdcall IsCharSpaceA(long)
#@ stdcall IsCharSpaceW(long)
#@ stdcall IsCharUpperA(long)
#@ stdcall IsCharUpperW(long)
#@ stdcall IsCharXDigitW(long)
#@ stdcall IsDBCSLeadByte(long)
#@ stdcall IsDBCSLeadByteEx(long long)
#@ stdcall IsDebuggerPresent()
# #@ stub IsDeveloperModeEnabled
# #@ stub IsDeveloperModePolicyApplied
# #@ stub IsEnclaveTypeSupported
# #@ stub IsGlobalizationUserSettingsKeyRedirected
@ stdcall IsInternetESCEnabled() kernelbase.IsInternetESCEnabled
@ stdcall IsNLSDefinedString(long long ptr wstr long) kernelbase.IsNLSDefinedString
@ stdcall IsNormalizedString(long wstr long) kernelbase.IsNormalizedString
# #@ stub IsProcessCritical
@ stdcall IsProcessInJob(long long ptr) kernelbase.IsProcessInJob
#@ stdcall IsProcessorFeaturePresent(long)
# #@ stub IsSideloadingEnabled
# #@ stub IsSideloadingPolicyApplied
# #@ stub IsSyncForegroundPolicyRefresh
#@ stdcall IsThreadAFiber()
@ stdcall IsThreadpoolTimerSet(ptr) kernelbase.IsThreadpoolTimerSet
# #@ stub IsTimeZoneRedirectionEnabled
@ stdcall IsTokenRestricted(long) kernelbase.IsTokenRestricted
@ stdcall IsValidAcl(ptr) kernelbase.IsValidAcl
#@ stdcall IsValidCodePage(long)
#@ stdcall IsValidLanguageGroup(long long)
#@ stdcall IsValidLocale(long long)
@ stdcall IsValidLocaleName(wstr) kernelbase.IsValidLocaleName
@ stdcall IsValidNLSVersion(long wstr ptr) kernelbase.IsValidNLSVersion
#@ stub IsValidRelativeSecurityDescriptor
@ stdcall IsValidSecurityDescriptor(ptr) kernelbase.IsValidSecurityDescriptor
@ stdcall IsValidSid(ptr) kernelbase.IsValidSid
@ stdcall IsWellKnownSid(ptr long) kernelbase.IsWellKnownSid
#@ stdcall IsWow64Process(ptr ptr)
#@ stdcall IsWow64Process2(ptr ptr ptr)
#@ stdcall K32EmptyWorkingSet(long) EmptyWorkingSet
#@ stdcall K32EnumDeviceDrivers(ptr long ptr) EnumDeviceDrivers
#@ stdcall K32EnumPageFilesA(ptr ptr) EnumPageFilesA
#@ stdcall K32EnumPageFilesW(ptr ptr) EnumPageFilesW
#@ stdcall K32EnumProcessModules(long ptr long ptr) EnumProcessModules
#@ stdcall K32EnumProcessModulesEx(long ptr long ptr long) EnumProcessModulesEx
#@ stdcall K32EnumProcesses(ptr long ptr) EnumProcesses
#@ stdcall K32GetDeviceDriverBaseNameA(ptr ptr long) GetDeviceDriverBaseNameA
#@ stdcall K32GetDeviceDriverBaseNameW(ptr ptr long) GetDeviceDriverBaseNameW
#@ stdcall K32GetDeviceDriverFileNameA(ptr ptr long) GetDeviceDriverFileNameA
#@ stdcall K32GetDeviceDriverFileNameW(ptr ptr long) GetDeviceDriverFileNameW
#@ stdcall K32GetMappedFileNameA(long ptr ptr long) GetMappedFileNameA
#@ stdcall K32GetMappedFileNameW(long ptr ptr long) GetMappedFileNameW
#@ stdcall K32GetModuleBaseNameA(long long ptr long) GetModuleBaseNameA
#@ stdcall K32GetModuleBaseNameW(long long ptr long) GetModuleBaseNameW
#@ stdcall K32GetModuleFileNameExA(long long ptr long) GetModuleFileNameExA
#@ stdcall K32GetModuleFileNameExW(long long ptr long) GetModuleFileNameExW
#@ stdcall K32GetModuleInformation(long long ptr long) GetModuleInformation
#@ stdcall K32GetPerformanceInfo(ptr long) GetPerformanceInfo
#@ stdcall K32GetProcessImageFileNameA(long ptr long) GetProcessImageFileNameA
#@ stdcall K32GetProcessImageFileNameW(long ptr long) GetProcessImageFileNameW
#@ stdcall K32GetProcessMemoryInfo(long ptr long) GetProcessMemoryInfo
#@ stdcall K32GetWsChanges(long ptr long) GetWsChanges
#@ stdcall K32GetWsChangesEx(long ptr ptr) GetWsChangesEx
#@ stdcall K32InitializeProcessForWsWatch(long) InitializeProcessForWsWatch
#@ stdcall K32QueryWorkingSet(long ptr long) QueryWorkingSet
#@ stdcall K32QueryWorkingSetEx(long ptr long) QueryWorkingSetEx
#@ stdcall KernelBaseGetGlobalData()
@ stdcall LCIDToLocaleName(long ptr long long) kernelbase.LCIDToLocaleName
@ stdcall LCMapStringA(long long str long ptr long) kernelbase.LCMapStringA
@ stdcall LCMapStringEx(wstr long wstr long ptr long ptr ptr long) kernelbase.LCMapStringEx
@ stdcall LCMapStringW(long long wstr long ptr long) kernelbase.LCMapStringW
# #@ stub LeaveCriticalPolicySectionInternal
#@ stdcall LeaveCriticalSection(ptr) ntdll.RtlLeaveCriticalSection
#@ stdcall LeaveCriticalSectionWhenCallbackReturns(ptr ptr) ntdll.TpCallbackLeaveCriticalSectionOnCompletion
#@ stdcall LoadAppInitDlls()
# #@ stub LoadEnclaveData
#@ stdcall LoadLibraryA(str)
#@ stdcall LoadLibraryExA( str long long)
#@ stdcall LoadLibraryExW(wstr long long)
#@ stdcall LoadLibraryW(wstr)
#@ stdcall LoadPackagedLibrary(wstr long)
#@ stdcall LoadResource(long long)
#@ stdcall LoadStringA(long long ptr long)
#@ stub LoadStringBaseExW
#@ stub LoadStringByReference
#@ stdcall LoadStringW(long long ptr long)
#@ stdcall LocalAlloc(long long)
#@ stdcall LocalFileTimeToFileTime(ptr ptr)
#@ stdcall LocalFree(long)
#@ stdcall LocalLock(long)
#@ stdcall LocalReAlloc(long long long)
#@ stdcall LocalUnlock(long)
@ stdcall LocaleNameToLCID(wstr long) kernelbase.LocaleNameToLCID
@ stdcall -arch=x86_64,arm64ec LocateXStateFeature(ptr long ptr) kernelbase.LocateXStateFeature
#@ stdcall LockFile(long long long long long)
#@ stdcall LockFileEx(long long long long long ptr)
#@ stdcall LockResource(long)
@ stdcall MakeAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) kernelbase.MakeAbsoluteSD
#@ stub MakeAbsoluteSD2
@ stdcall MakeSelfRelativeSD(ptr ptr ptr) kernelbase.MakeSelfRelativeSD
@ stdcall MapGenericMask(ptr ptr) kernelbase.MapGenericMask
# #@ stub MapPredefinedHandleInternal
#@ stdcall MapUserPhysicalPages(ptr long ptr)
#@ stdcall MapViewOfFile(long long long long long)
#@ stdcall MapViewOfFile3(long long ptr int64 long long long ptr long)
#@ stdcall MapViewOfFileEx(long long long long long ptr)
#@ stdcall MapViewOfFileExNuma(long long long long long ptr long)
#@ stdcall MapViewOfFileFromApp(long long int64 long)
#@ stdcall MoveFileExW(wstr wstr long)
# #@ stub MoveFileWithProgressTransactedW
#@ stdcall MoveFileWithProgressW(wstr wstr ptr ptr long)
#@ stdcall MulDiv(long long long)
#@ stdcall MultiByteToWideChar(long long str long ptr long)
# #@ stub NamedPipeEventEnum
# #@ stub NamedPipeEventSelect
#@ stdcall NeedCurrentDirectoryForExePathA(str)
#@ stdcall NeedCurrentDirectoryForExePathW(wstr)
#@ stub NlsCheckPolicy
#@ stub NlsDispatchAnsiEnumProc
#@ stub NlsEventDataDescCreate
#@ stub NlsGetACPFromLocale
#@ stub NlsGetCacheUpdateCount
#@ stub NlsIsUserDefaultLocale
#@ stub NlsUpdateLocale
#@ stub NlsUpdateSystemLocale
#@ stdcall NlsValidateLocale(ptr long)
#@ stub NlsWriteEtwEvent
@ stdcall NormalizeString(long wstr long ptr long) kernelbase.NormalizeString
#@ stub NotifyMountMgr
#@ stub NotifyRedirectedStringChange
@ stdcall ObjectCloseAuditAlarmW(wstr ptr long) kernelbase.ObjectCloseAuditAlarmW
@ stdcall ObjectDeleteAuditAlarmW(wstr ptr long) kernelbase.ObjectDeleteAuditAlarmW
@ stdcall ObjectOpenAuditAlarmW(wstr ptr wstr wstr ptr long long long ptr long long ptr) kernelbase.ObjectOpenAuditAlarmW
@ stdcall ObjectPrivilegeAuditAlarmW(wstr ptr long long ptr long) kernelbase.ObjectPrivilegeAuditAlarmW
# #@ stub OfferVirtualMemory
#@ stdcall OpenEventA(long long str)
#@ stdcall OpenEventW(long long wstr)
#@ stdcall OpenFileById(long ptr long long ptr long)
#@ stdcall OpenFileMappingFromApp(long long wstr)
#@ stdcall OpenFileMappingW(long long wstr)
# #@ stub OpenGlobalizationUserSettingsKey
#@ stdcall OpenMutexW(long long wstr)
# #@ stub OpenPackageInfoByFullName
# #@ stub OpenPackageInfoByFullNameForUser
@ stdcall OpenPrivateNamespaceW(ptr wstr) kernelbase.OpenPrivateNamespaceW
#@ stdcall OpenProcess(long long long)
@ stdcall OpenProcessToken(long long ptr) kernelbase.OpenProcessToken
#@ stub OpenRegKey
#@ stdcall OpenSemaphoreW(long long wstr)
# #@ stub OpenState
# #@ stub OpenStateAtom
# #@ stub OpenStateExplicit
# #@ stub OpenStateExplicitForUserSid
# #@ stub OpenStateExplicitForUserSidString
@ stdcall OpenThread(long long long) kernelbase.OpenThread
@ stdcall OpenThreadToken(long long long ptr) kernelbase.OpenThreadToken
#@ stdcall OpenWaitableTimerW(long long wstr)
#@ stdcall OutputDebugStringA(str)
#@ stdcall OutputDebugStringW(wstr)
# #@ stub OverrideRoamingDataModificationTimesInRange
# #@ stub PackageFamilyNameFromFullName
# #@ stub PackageFamilyNameFromId
# #@ stub PackageFamilyNameFromProductId
# #@ stub PackageFullNameFromId
# #@ stub PackageFullNameFromProductId
#@ stdcall PackageIdFromFullName(wstr long ptr ptr)
# #@ stub PackageIdFromProductId
# #@ stub PackageNameAndPublisherIdFromFamilyName
# #@ stub PackageRelativeApplicationIdFromProductId
# #@ stub PackageSidFromFamilyName
# #@ stub PackageSidFromProductId
# #@ stub ParseApplicationUserModelId
@ stdcall ParseURLA(str ptr) kernelbase.ParseURLA
@ stdcall ParseURLW(wstr ptr) kernelbase.ParseURLW
@ stdcall PathAddBackslashA(str) kernelbase.PathAddBackslashA
@ stdcall PathAddBackslashW(wstr) kernelbase.PathAddBackslashW
@ stdcall PathAddExtensionA(str str) kernelbase.PathAddExtensionA
@ stdcall PathAddExtensionW(wstr wstr) kernelbase.PathAddExtensionW
@ stdcall PathAllocCanonicalize(wstr long ptr) kernelbase.PathAllocCanonicalize
@ stdcall PathAllocCombine(wstr wstr long ptr) kernelbase.PathAllocCombine
@ stdcall PathAppendA(str str) kernelbase.PathAppendA
@ stdcall PathAppendW(wstr wstr) kernelbase.PathAppendW
@ stdcall PathCanonicalizeA(ptr str) kernelbase.PathCanonicalizeA
@ stdcall PathCanonicalizeW(ptr wstr) kernelbase.PathCanonicalizeW
@ stdcall PathCchAddBackslash(wstr long) kernelbase.PathCchAddBackslash
@ stdcall PathCchAddBackslashEx(wstr long ptr ptr) kernelbase.PathCchAddBackslashEx
@ stdcall PathCchAddExtension(wstr long wstr) kernelbase.PathCchAddExtension
@ stdcall PathCchAppend(wstr long wstr) kernelbase.PathCchAppend
@ stdcall PathCchAppendEx(wstr long wstr long) kernelbase.PathCchAppendEx
@ stdcall PathCchCanonicalize(ptr long wstr) kernelbase.PathCchCanonicalize
@ stdcall PathCchCanonicalizeEx(ptr long wstr long) kernelbase.PathCchCanonicalizeEx
@ stdcall PathCchCombine(ptr long wstr wstr) kernelbase.PathCchCombine
@ stdcall PathCchCombineEx(ptr long wstr wstr long) kernelbase.PathCchCombineEx
@ stdcall PathCchFindExtension(wstr long ptr) kernelbase.PathCchFindExtension
@ stdcall PathCchIsRoot(wstr) kernelbase.PathCchIsRoot
@ stdcall PathCchRemoveBackslash(wstr long) kernelbase.PathCchRemoveBackslash
@ stdcall PathCchRemoveBackslashEx(wstr long ptr ptr) kernelbase.PathCchRemoveBackslashEx
@ stdcall PathCchRemoveExtension(wstr long) kernelbase.PathCchRemoveExtension
@ stdcall PathCchRemoveFileSpec(wstr long) kernelbase.PathCchRemoveFileSpec
@ stdcall PathCchRenameExtension(wstr long wstr) kernelbase.PathCchRenameExtension
@ stdcall PathCchSkipRoot(wstr ptr) kernelbase.PathCchSkipRoot
@ stdcall PathCchStripPrefix(wstr long) kernelbase.PathCchStripPrefix
@ stdcall PathCchStripToRoot(wstr long) kernelbase.PathCchStripToRoot
@ stdcall PathCombineA(ptr str str) kernelbase.PathCombineA
@ stdcall PathCombineW(ptr wstr wstr) kernelbase.PathCombineW
@ stdcall PathCommonPrefixA(str str ptr) kernelbase.PathCommonPrefixA
@ stdcall PathCommonPrefixW(wstr wstr ptr) kernelbase.PathCommonPrefixW
@ stdcall PathCreateFromUrlA(str ptr ptr long) kernelbase.PathCreateFromUrlA
@ stdcall PathCreateFromUrlAlloc(wstr ptr long) kernelbase.PathCreateFromUrlAlloc
@ stdcall PathCreateFromUrlW(wstr ptr ptr long) kernelbase.PathCreateFromUrlW
@ stdcall PathFileExistsA(str) kernelbase.PathFileExistsA
@ stdcall PathFileExistsW(wstr) kernelbase.PathFileExistsW
@ stdcall PathFindExtensionA(str) kernelbase.PathFindExtensionA
@ stdcall PathFindExtensionW(wstr) kernelbase.PathFindExtensionW
@ stdcall PathFindFileNameA(str) kernelbase.PathFindFileNameA
@ stdcall PathFindFileNameW(wstr) kernelbase.PathFindFileNameW
@ stdcall PathFindNextComponentA(str) kernelbase.PathFindNextComponentA
@ stdcall PathFindNextComponentW(wstr) kernelbase.PathFindNextComponentW
@ stdcall PathGetArgsA(str) kernelbase.PathGetArgsA
@ stdcall PathGetArgsW(wstr) kernelbase.PathGetArgsW
@ stdcall PathGetCharTypeA(long) kernelbase.PathGetCharTypeA
@ stdcall PathGetCharTypeW(long) kernelbase.PathGetCharTypeW
@ stdcall PathGetDriveNumberA(str) kernelbase.PathGetDriveNumberA
@ stdcall PathGetDriveNumberW(wstr) kernelbase.PathGetDriveNumberW
@ stdcall PathIsFileSpecA(str) kernelbase.PathIsFileSpecA
@ stdcall PathIsFileSpecW(wstr) kernelbase.PathIsFileSpecW
@ stdcall PathIsLFNFileSpecA(str) kernelbase.PathIsLFNFileSpecA
@ stdcall PathIsLFNFileSpecW(wstr) kernelbase.PathIsLFNFileSpecW
@ stdcall PathIsPrefixA(str str) kernelbase.PathIsPrefixA
@ stdcall PathIsPrefixW(wstr wstr) kernelbase.PathIsPrefixW
@ stdcall PathIsRelativeA(str) kernelbase.PathIsRelativeA
@ stdcall PathIsRelativeW(wstr) kernelbase.PathIsRelativeW
@ stdcall PathIsRootA(str) kernelbase.PathIsRootA
@ stdcall PathIsRootW(wstr) kernelbase.PathIsRootW
@ stdcall PathIsSameRootA(str str) kernelbase.PathIsSameRootA
@ stdcall PathIsSameRootW(wstr wstr) kernelbase.PathIsSameRootW
@ stdcall PathIsUNCA(str) kernelbase.PathIsUNCA
@ stdcall PathIsUNCEx(wstr ptr) kernelbase.PathIsUNCEx
@ stdcall PathIsUNCServerA(str) kernelbase.PathIsUNCServerA
@ stdcall PathIsUNCServerShareA(str) kernelbase.PathIsUNCServerShareA
@ stdcall PathIsUNCServerShareW(wstr) kernelbase.PathIsUNCServerShareW
@ stdcall PathIsUNCServerW(wstr) kernelbase.PathIsUNCServerW
@ stdcall PathIsUNCW(wstr) kernelbase.PathIsUNCW
@ stdcall PathIsURLA(str) kernelbase.PathIsURLA
@ stdcall PathIsURLW(wstr) kernelbase.PathIsURLW
@ stdcall PathIsValidCharA(long long) kernelbase.PathIsValidCharA
@ stdcall PathIsValidCharW(long long) kernelbase.PathIsValidCharW
@ stdcall PathMatchSpecA(str str) kernelbase.PathMatchSpecA
@ stdcall PathMatchSpecExA(str str long) kernelbase.PathMatchSpecExA
@ stdcall PathMatchSpecExW(wstr wstr long) kernelbase.PathMatchSpecExW
@ stdcall PathMatchSpecW(wstr wstr) kernelbase.PathMatchSpecW
@ stdcall PathParseIconLocationA(str) kernelbase.PathParseIconLocationA
@ stdcall PathParseIconLocationW(wstr) kernelbase.PathParseIconLocationW
@ stdcall PathQuoteSpacesA(str) kernelbase.PathQuoteSpacesA
@ stdcall PathQuoteSpacesW(wstr) kernelbase.PathQuoteSpacesW
@ stdcall PathRelativePathToA(ptr str long str long) kernelbase.PathRelativePathToA
@ stdcall PathRelativePathToW(ptr wstr long wstr long) kernelbase.PathRelativePathToW
@ stdcall PathRemoveBackslashA(str) kernelbase.PathRemoveBackslashA
@ stdcall PathRemoveBackslashW(wstr) kernelbase.PathRemoveBackslashW
@ stdcall PathRemoveBlanksA(str) kernelbase.PathRemoveBlanksA
@ stdcall PathRemoveBlanksW(wstr) kernelbase.PathRemoveBlanksW
@ stdcall PathRemoveExtensionA(str) kernelbase.PathRemoveExtensionA
@ stdcall PathRemoveExtensionW(wstr) kernelbase.PathRemoveExtensionW
@ stdcall PathRemoveFileSpecA(str) kernelbase.PathRemoveFileSpecA
@ stdcall PathRemoveFileSpecW(wstr) kernelbase.PathRemoveFileSpecW
@ stdcall PathRenameExtensionA(str str) kernelbase.PathRenameExtensionA
@ stdcall PathRenameExtensionW(wstr wstr) kernelbase.PathRenameExtensionW
@ stdcall PathSearchAndQualifyA(str ptr long) kernelbase.PathSearchAndQualifyA
@ stdcall PathSearchAndQualifyW(wstr ptr long) kernelbase.PathSearchAndQualifyW
@ stdcall PathSkipRootA(str) kernelbase.PathSkipRootA
@ stdcall PathSkipRootW(wstr) kernelbase.PathSkipRootW
@ stdcall PathStripPathA(str) kernelbase.PathStripPathA
@ stdcall PathStripPathW(wstr) kernelbase.PathStripPathW
@ stdcall PathStripToRootA(str) kernelbase.PathStripToRootA
@ stdcall PathStripToRootW(wstr) kernelbase.PathStripToRootW
@ stdcall PathUnExpandEnvStringsA(str ptr long) kernelbase.PathUnExpandEnvStringsA
@ stdcall PathUnExpandEnvStringsW(wstr ptr long) kernelbase.PathUnExpandEnvStringsW
@ stdcall PathUnquoteSpacesA(str) kernelbase.PathUnquoteSpacesA
@ stdcall PathUnquoteSpacesW(wstr) kernelbase.PathUnquoteSpacesW
# #@ stub PcwAddQueryItem
# #@ stub PcwClearCounterSetSecurity
# #@ stub PcwCollectData
# #@ stub PcwCompleteNotification
# #@ stub PcwCreateNotifier
# #@ stub PcwCreateQuery
# #@ stub PcwDisconnectCounterSet
# #@ stub PcwEnumerateInstances
# #@ stub PcwIsNotifierAlive
# #@ stub PcwQueryCounterSetSecurity
# #@ stub PcwReadNotificationData
# #@ stub PcwRegisterCounterSet
# #@ stub PcwRemoveQueryItem
# #@ stub PcwSendNotification
# #@ stub PcwSendStatelessNotification
# #@ stub PcwSetCounterSetSecurity
# #@ stub PcwSetQueryItemUserData
@ stdcall PeekConsoleInputA(ptr ptr long ptr) kernelbase.PeekConsoleInputA
@ stdcall PeekConsoleInputW(ptr ptr long ptr) kernelbase.PeekConsoleInputW
#@ stdcall PeekNamedPipe(long ptr long ptr ptr ptr)
@ stdcall PerfCreateInstance(long ptr wstr long) kernelbase.PerfCreateInstance
# #@ stub PerfDecrementULongCounterValue
# #@ stub PerfDecrementULongLongCounterValue
@ stdcall PerfDeleteInstance(long ptr) kernelbase.PerfDeleteInstance
# #@ stub PerfIncrementULongCounterValue
# #@ stub PerfIncrementULongLongCounterValue
# #@ stub PerfQueryInstance
@ stdcall PerfSetCounterRefValue(long ptr long ptr) kernelbase.PerfSetCounterRefValue
@ stdcall PerfSetCounterSetInfo(long ptr long) kernelbase.PerfSetCounterSetInfo
@ stdcall PerfSetULongCounterValue(long ptr long long) kernelbase.PerfSetULongCounterValue
@ stdcall PerfSetULongLongCounterValue(long ptr long int64) kernelbase.PerfSetULongLongCounterValue
@ stdcall PerfStartProvider(ptr ptr ptr) kernelbase.PerfStartProvider
@ stdcall PerfStartProviderEx(ptr ptr ptr) kernelbase.PerfStartProviderEx
@ stdcall PerfStopProvider(long) kernelbase.PerfStopProvider
# #@ stub PoolPerAppKeyStateInternal
#@ stdcall PostQueuedCompletionStatus(long long ptr ptr)
#@ stdcall PrefetchVirtualMemory(ptr ptr ptr long)
#@ stub PrivCopyFileExW
@ stdcall PrivilegeCheck(ptr ptr ptr) kernelbase.PrivilegeCheck
@ stdcall PrivilegedServiceAuditAlarmW(wstr wstr long ptr long) kernelbase.PrivilegedServiceAuditAlarmW
@ stdcall ProcessIdToSessionId(long ptr) kernelbase.ProcessIdToSessionId
# #@ stub ProductIdFromPackageFamilyName
# #@ stub PsmCreateKey
# #@ stub PsmCreateKeyWithDynamicId
# #@ stub PsmEqualApplication
# #@ stub PsmEqualPackage
# #@ stub PsmGetApplicationNameFromKey
# #@ stub PsmGetKeyFromProcess
# #@ stub PsmGetKeyFromToken
# #@ stub PsmGetPackageFullNameFromKey
# #@ stub PsmIsChildKey
# #@ stub PsmIsDynamicKey
# #@ stub PsmIsValidKey
@ stdcall PssCaptureSnapshot(ptr long long ptr) kernelbase.PssCaptureSnapshot
# #@ stub PssDuplicateSnapshot
@ stdcall PssFreeSnapshot(ptr ptr) kernelbase.PssFreeSnapshot
@ stdcall PssQuerySnapshot(ptr long ptr long) kernelbase.PssQuerySnapshot
# #@ stub PssWalkMarkerCreate
# #@ stub PssWalkMarkerFree
# #@ stub PssWalkMarkerGetPosition
# #@ stub PssWalkMarkerSeekToBeginning
# #@ stub PssWalkMarkerSetPosition
# #@ stub PssWalkSnapshot
# #@ stub PublishStateChangeNotification
#@ stdcall PulseEvent(long)
#@ stdcall PurgeComm(long long)
#@ stdcall QISearch(ptr ptr ptr ptr)
#@ stdcall QueryActCtxSettingsW(long ptr wstr wstr ptr long ptr)
#@ stdcall QueryActCtxW(long ptr ptr long ptr long ptr)
#@ stdcall QueryDepthSList(ptr) ntdll.RtlQueryDepthSList
#@ stdcall QueryDosDeviceW(wstr ptr long)
#@ stdcall QueryFullProcessImageNameA(ptr long ptr ptr)
#@ stdcall QueryFullProcessImageNameW(ptr long ptr ptr)
#@ stdcall QueryIoRingCapabilities(ptr)
#@ stdcall QueryIdleProcessorCycleTime(ptr ptr)
#@ stdcall QueryIdleProcessorCycleTimeEx(long ptr ptr)
#@ stdcall QueryInterruptTime(ptr)
#@ stdcall QueryInterruptTimePrecise(ptr)
#@ stdcall QueryMemoryResourceNotification(ptr ptr)
# #@ stub QueryOptionalDelayLoadedAPI
#@ stdcall QueryPerformanceCounter(ptr) ntdll.RtlQueryPerformanceCounter
#@ stdcall QueryPerformanceFrequency(ptr) ntdll.RtlQueryPerformanceFrequency
@ stdcall QueryProcessAffinityUpdateMode(ptr ptr) kernelbase.QueryProcessAffinityUpdateMode
#@ stdcall QueryProcessCycleTime(long ptr)
# #@ stub QueryProtectedPolicy
#@ stub QuerySecurityAccessMask
# #@ stub QueryStateAtomValueInfo
# #@ stub QueryStateContainerCreatedNew
# #@ stub QueryStateContainerItemInfo
#@ stdcall QueryThreadCycleTime(long ptr)
@ stdcall QueryThreadpoolStackInformation(ptr ptr) kernelbase.QueryThreadpoolStackInformation
#@ stdcall QueryUnbiasedInterruptTime(ptr) ntdll.RtlQueryUnbiasedInterruptTime
#@ stdcall QueryUnbiasedInterruptTimePrecise(ptr)
#@ stdcall QueryVirtualMemoryInformation(long ptr long ptr long ptr)
#@ stdcall QueryWorkingSet(long ptr long)
#@ stdcall QueryWorkingSetEx(long ptr long)
@ stdcall QueueUserAPC(ptr long long) kernelbase.QueueUserAPC
#@ stdcall QueueUserWorkItem(ptr ptr long)
# #@ stub QuirkGetData
# #@ stub QuirkGetData2
#@ stdcall QuirkIsEnabled(ptr)
# #@ stub QuirkIsEnabled2
#@ stdcall QuirkIsEnabled3(ptr ptr)
# #@ stub QuirkIsEnabledForPackage
# #@ stub QuirkIsEnabledForPackage2
# #@ stub QuirkIsEnabledForPackage3
# #@ stub QuirkIsEnabledForPackage4
# #@ stub QuirkIsEnabledForProcess
#@ stdcall RaiseException(long long long ptr)
@ stdcall RaiseFailFastException(ptr ptr long) kernelbase.RaiseFailFastException
#@ stdcall ReOpenFile(ptr long long long)
@ stdcall ReadConsoleA(long ptr long ptr ptr) kernelbase.ReadConsoleA
@ stdcall ReadConsoleInputA(long ptr long ptr) kernelbase.ReadConsoleInputA
#@ stub ReadConsoleInputExA
#@ stub ReadConsoleInputExW
@ stdcall ReadConsoleInputW(long ptr long ptr) kernelbase.ReadConsoleInputW
@ stdcall ReadConsoleOutputA(long ptr long long ptr) kernelbase.ReadConsoleOutputA
@ stdcall ReadConsoleOutputAttribute(long ptr long long ptr) kernelbase.ReadConsoleOutputAttribute
@ stdcall ReadConsoleOutputCharacterA(long ptr long long ptr) kernelbase.ReadConsoleOutputCharacterA
@ stdcall ReadConsoleOutputCharacterW(long ptr long long ptr) kernelbase.ReadConsoleOutputCharacterW
@ stdcall ReadConsoleOutputW(long ptr long long ptr) kernelbase.ReadConsoleOutputW
@ stdcall ReadConsoleW(long ptr long ptr ptr) kernelbase.ReadConsoleW
#@ stdcall ReadDirectoryChangesW(long ptr long long long ptr ptr ptr)
#@ stdcall ReadFile(long ptr long ptr ptr)
#@ stdcall ReadFileEx(long ptr long ptr ptr)
#@ stdcall ReadFileScatter(long ptr long ptr ptr)
#@ stdcall ReadProcessMemory(long ptr ptr long ptr)
# #@ stub ReadStateAtomValue
# #@ stub ReadStateContainerValue
# #@ stub ReclaimVirtualMemory
# #@ stub RefreshPolicyExInternal
# #@ stub RefreshPolicyInternal
@ stdcall RegCloseKey(long) kernelbase.RegCloseKey
@ stdcall RegCopyTreeW(long wstr long) kernelbase.RegCopyTreeW
@ stdcall RegCreateKeyExA(long str long ptr long long ptr ptr ptr) kernelbase.RegCreateKeyExA
# #@ stub RegCreateKeyExInternalA
# #@ stub RegCreateKeyExInternalW
@ stdcall RegCreateKeyExW(long wstr long ptr long long ptr ptr ptr) kernelbase.RegCreateKeyExW
@ stdcall RegDeleteKeyExA(long str long long) kernelbase.RegDeleteKeyExA
# #@ stub RegDeleteKeyExInternalA
# #@ stub RegDeleteKeyExInternalW
@ stdcall RegDeleteKeyExW(long wstr long long) kernelbase.RegDeleteKeyExW
@ stdcall RegDeleteKeyValueA(long str str) kernelbase.RegDeleteKeyValueA
@ stdcall RegDeleteKeyValueW(long wstr wstr) kernelbase.RegDeleteKeyValueW
@ stdcall RegDeleteTreeA(long str) kernelbase.RegDeleteTreeA
@ stdcall RegDeleteTreeW(long wstr) kernelbase.RegDeleteTreeW
@ stdcall RegDeleteValueA(long str) kernelbase.RegDeleteValueA
@ stdcall RegDeleteValueW(long wstr) kernelbase.RegDeleteValueW
# #@ stub RegDisablePredefinedCacheEx
@ stdcall RegEnumKeyExA(long long ptr ptr ptr ptr ptr ptr) kernelbase.RegEnumKeyExA
@ stdcall RegEnumKeyExW(long long ptr ptr ptr ptr ptr ptr) kernelbase.RegEnumKeyExW
@ stdcall RegEnumValueA(long long ptr ptr ptr ptr ptr ptr) kernelbase.RegEnumValueA
@ stdcall RegEnumValueW(long long ptr ptr ptr ptr ptr ptr) kernelbase.RegEnumValueW
@ stdcall RegFlushKey(long) kernelbase.RegFlushKey
@ stdcall RegGetKeySecurity(long long ptr ptr) kernelbase.RegGetKeySecurity
@ stdcall RegGetValueA(long str str long ptr ptr ptr) kernelbase.RegGetValueA
@ stdcall RegGetValueW(long wstr wstr long ptr ptr ptr) kernelbase.RegGetValueW
# #@ stub RegKrnGetAppKeyEventAddressInternal
# #@ stub RegKrnGetAppKeyLoaded
# #@ stub RegKrnGetClassesEnumTableAddressInternal
# #@ stub RegKrnGetHKEY_ClassesRootAddress
# #@ stub RegKrnGetTermsrvRegistryExtensionFlags
# #@ stub RegKrnResetAppKeyLoaded
# #@ stub RegKrnSetDllHasThreadStateGlobal
# #@ stub RegKrnSetTermsrvRegistryExtensionFlags
@ stdcall RegLoadAppKeyA(str ptr long long long) kernelbase.RegLoadAppKeyA
@ stdcall RegLoadAppKeyW(wstr ptr long long long) kernelbase.RegLoadAppKeyW
@ stdcall RegLoadKeyA(long str str) kernelbase.RegLoadKeyA
@ stdcall RegLoadKeyW(long wstr wstr) kernelbase.RegLoadKeyW
@ stdcall RegLoadMUIStringA(long str str long ptr long str) kernelbase.RegLoadMUIStringA
@ stdcall RegLoadMUIStringW(long wstr wstr long ptr long wstr) kernelbase.RegLoadMUIStringW
@ stdcall RegNotifyChangeKeyValue(long long long long long) kernelbase.RegNotifyChangeKeyValue
@ stdcall RegOpenCurrentUser(long ptr) kernelbase.RegOpenCurrentUser
@ stdcall RegOpenKeyExA(long str long long ptr) kernelbase.RegOpenKeyExA
# #@ stub RegOpenKeyExInternalA
# #@ stub RegOpenKeyExInternalW
@ stdcall RegOpenKeyExW(long wstr long long ptr) kernelbase.RegOpenKeyExW
@ stdcall RegOpenUserClassesRoot(ptr long long ptr) kernelbase.RegOpenUserClassesRoot
@ stdcall RegQueryInfoKeyA(long ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) kernelbase.RegQueryInfoKeyA
@ stdcall RegQueryInfoKeyW(long ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) kernelbase.RegQueryInfoKeyW
@ stdcall RegQueryValueExA(long str ptr ptr ptr ptr) kernelbase.RegQueryValueExA
@ stdcall RegQueryValueExW(long wstr ptr ptr ptr ptr) kernelbase.RegQueryValueExW
@ stdcall RegRestoreKeyA(long str long) kernelbase.RegRestoreKeyA
@ stdcall RegRestoreKeyW(long wstr long) kernelbase.RegRestoreKeyW
@ stdcall RegSaveKeyExA(long str ptr long) kernelbase.RegSaveKeyExA
@ stdcall RegSaveKeyExW(long wstr ptr long) kernelbase.RegSaveKeyExW
@ stdcall RegSetKeySecurity(long long ptr) kernelbase.RegSetKeySecurity
@ stdcall RegSetKeyValueA(long str str long ptr long) kernelbase.RegSetKeyValueA
@ stdcall RegSetKeyValueW(long wstr wstr long ptr long) kernelbase.RegSetKeyValueW
@ stdcall RegSetValueExA(long str long long ptr long) kernelbase.RegSetValueExA
@ stdcall RegSetValueExW(long wstr long long ptr long) kernelbase.RegSetValueExW
@ stdcall RegUnLoadKeyA(long str) kernelbase.RegUnLoadKeyA
@ stdcall RegUnLoadKeyW(long wstr) kernelbase.RegUnLoadKeyW
# #@ stub RegisterBadMemoryNotification
# #@ stub RegisterGPNotificationInternal
# #@ stub RegisterStateChangeNotification
# #@ stub RegisterStateLock
#@ stdcall RegisterTraceGuidsW(ptr ptr ptr long ptr wstr wstr ptr) ntdll.EtwRegisterTraceGuidsW
#@ stdcall RegisterWaitForSingleObjectEx(long ptr ptr long long)
#@ stdcall ReleaseActCtx(ptr)
#@ stdcall ReleaseMutex(long)
#@ stdcall ReleaseMutexWhenCallbackReturns(ptr long) ntdll.TpCallbackReleaseMutexOnCompletion
#@ stdcall ReleaseSRWLockExclusive(ptr) ntdll.RtlReleaseSRWLockExclusive
#@ stdcall ReleaseSRWLockShared(ptr) ntdll.RtlReleaseSRWLockShared
#@ stdcall ReleaseSemaphore(long long ptr)
#@ stdcall ReleaseSemaphoreWhenCallbackReturns(ptr long long) ntdll.TpCallbackReleaseSemaphoreOnCompletion
# #@ stub ReleaseStateLock
@ stdcall RemapPredefinedHandleInternal(long long) kernelbase.RemapPredefinedHandleInternal
#@ stdcall RemoveDirectoryA(str)
#@ stdcall RemoveDirectoryW(wstr)
#@ stdcall RemoveDllDirectory(ptr)
# #@ stub RemovePackageStatus
# #@ stub RemovePackageStatusForUser
#@ stdcall RemoveVectoredContinueHandler(ptr) ntdll.RtlRemoveVectoredContinueHandler
#@ stdcall RemoveVectoredExceptionHandler(ptr) ntdll.RtlRemoveVectoredExceptionHandler
# #@ stub ReplaceFileExInternal
#@ stdcall ReplaceFileW(wstr wstr wstr long ptr ptr)
#@ stdcall ResetEvent(long)
# #@ stub ResetState
#@ stdcall ResetWriteWatch(ptr long)
@ stdcall ResizePseudoConsole(ptr long) kernelbase.ResizePseudoConsole
#@ stdcall -import ResolveDelayLoadedAPI(ptr ptr ptr ptr ptr long) LdrResolveDelayLoadedAPI
# #@ stub ResolveDelayLoadsFromDll
@ stdcall ResolveLocaleName(wstr ptr long) kernelbase.ResolveLocaleName
#@ stdcall RestoreLastError(long) ntdll.RtlRestoreLastWin32Error
@ stdcall ResumeThread(long) kernelbase.ResumeThread
@ stdcall RevertToSelf() kernelbase.RevertToSelf
# #@ stub RsopLoggingEnabledInternal
# #@ stub SHCoCreateInstance
#@ stdcall SHExpandEnvironmentStringsA(str ptr long) ExpandEnvironmentStringsA
#@ stdcall SHExpandEnvironmentStringsW(wstr ptr long) ExpandEnvironmentStringsW
@ stdcall SHLoadIndirectString(wstr ptr long ptr) kernelbase.SHLoadIndirectString
# #@ stub SHLoadIndirectStringInternal
@ stdcall SHRegCloseUSKey(ptr) kernelbase.SHRegCloseUSKey
@ stdcall SHRegCreateUSKeyA(str long long ptr long) kernelbase.SHRegCreateUSKeyA
@ stdcall SHRegCreateUSKeyW(wstr long long ptr long) kernelbase.SHRegCreateUSKeyW
@ stdcall SHRegDeleteEmptyUSKeyA(long str long) kernelbase.SHRegDeleteEmptyUSKeyA
@ stdcall SHRegDeleteEmptyUSKeyW(long wstr long) kernelbase.SHRegDeleteEmptyUSKeyW
@ stdcall SHRegDeleteUSValueA(long str long) kernelbase.SHRegDeleteUSValueA
@ stdcall SHRegDeleteUSValueW(long wstr long) kernelbase.SHRegDeleteUSValueW
@ stdcall SHRegEnumUSKeyA(long long str ptr long) kernelbase.SHRegEnumUSKeyA
@ stdcall SHRegEnumUSKeyW(long long wstr ptr long) kernelbase.SHRegEnumUSKeyW
@ stdcall SHRegEnumUSValueA(long long ptr ptr ptr ptr ptr long) kernelbase.SHRegEnumUSValueA
@ stdcall SHRegEnumUSValueW(long long ptr ptr ptr ptr ptr long) kernelbase.SHRegEnumUSValueW
@ stdcall SHRegGetBoolUSValueA(str str long long) kernelbase.SHRegGetBoolUSValueA
@ stdcall SHRegGetBoolUSValueW(wstr wstr long long) kernelbase.SHRegGetBoolUSValueW
@ stdcall SHRegGetUSValueA(str str ptr ptr ptr long ptr long) kernelbase.SHRegGetUSValueA
@ stdcall SHRegGetUSValueW(wstr wstr ptr ptr ptr long ptr long) kernelbase.SHRegGetUSValueW
@ stdcall SHRegOpenUSKeyA(str long long ptr long) kernelbase.SHRegOpenUSKeyA
@ stdcall SHRegOpenUSKeyW(wstr long long ptr long) kernelbase.SHRegOpenUSKeyW
@ stdcall SHRegQueryInfoUSKeyA(long ptr ptr ptr ptr long) kernelbase.SHRegQueryInfoUSKeyA
@ stdcall SHRegQueryInfoUSKeyW(long ptr ptr ptr ptr long) kernelbase.SHRegQueryInfoUSKeyW
@ stdcall SHRegQueryUSValueA(long str ptr ptr ptr long ptr long) kernelbase.SHRegQueryUSValueA
@ stdcall SHRegQueryUSValueW(long wstr ptr ptr ptr long ptr long) kernelbase.SHRegQueryUSValueW
@ stdcall SHRegSetUSValueA(str str long ptr long long) kernelbase.SHRegSetUSValueA
@ stdcall SHRegSetUSValueW(wstr wstr long ptr long long) kernelbase.SHRegSetUSValueW
@ stdcall SHRegWriteUSValueA(long str long ptr long long) kernelbase.SHRegWriteUSValueA
@ stdcall SHRegWriteUSValueW(long wstr long ptr long long) kernelbase.SHRegWriteUSValueW
@ stdcall SHTruncateString(str long) kernelbase.SHTruncateString
# #@ stub SaveAlternatePackageRootPath
# #@ stub SaveStateRootFolderPath
@ stdcall ScrollConsoleScreenBufferA(long ptr ptr ptr ptr) kernelbase.ScrollConsoleScreenBufferA
@ stdcall ScrollConsoleScreenBufferW(long ptr ptr ptr ptr) kernelbase.ScrollConsoleScreenBufferW
#@ stdcall SearchPathA(str str str long ptr ptr)
#@ stdcall SearchPathW(wstr wstr wstr long ptr ptr)
@ stdcall SetAclInformation(ptr ptr long long) kernelbase.SetAclInformation
@ stdcall SetCachedSigningLevel(ptr long long long) kernelbase.SetCachedSigningLevel
#@ stdcall SetCalendarInfoW(long long long wstr)
# #@ stub SetClientDynamicTimeZoneInformation
# #@ stub SetClientTimeZoneInformation
#@ stdcall SetCommBreak(long)
#@ stdcall SetCommConfig(long ptr long)
#@ stdcall SetCommMask(long long)
#@ stdcall SetCommState(long ptr)
#@ stdcall SetCommTimeouts(long ptr)
@ stdcall SetComputerNameA(str) kernelbase.SetComputerNameA
# #@ stub SetComputerNameEx2W
@ stdcall SetComputerNameExA(long str) kernelbase.SetComputerNameExA
@ stdcall SetComputerNameExW(long wstr) kernelbase.SetComputerNameExW
@ stdcall SetComputerNameW(wstr) kernelbase.SetComputerNameW
@ stdcall SetConsoleActiveScreenBuffer(long) kernelbase.SetConsoleActiveScreenBuffer
@ stdcall SetConsoleCP(long) kernelbase.SetConsoleCP
@ stdcall SetConsoleCtrlHandler(ptr long) kernelbase.SetConsoleCtrlHandler
@ stdcall SetConsoleCursorInfo(long ptr) kernelbase.SetConsoleCursorInfo
@ stdcall SetConsoleCursorPosition(long long) kernelbase.SetConsoleCursorPosition
@ stdcall SetConsoleDisplayMode(long long ptr) kernelbase.SetConsoleDisplayMode
@ stdcall SetConsoleInputExeNameA(ptr) kernelbase.SetConsoleInputExeNameA
@ stdcall SetConsoleInputExeNameW(ptr) kernelbase.SetConsoleInputExeNameW
@ stdcall SetConsoleMode(long long) kernelbase.SetConsoleMode
@ stdcall SetConsoleOutputCP(long) kernelbase.SetConsoleOutputCP
#@ stdcall SetConsoleScreenBufferInfoEx(long ptr)
@ stdcall SetConsoleScreenBufferSize(long long) kernelbase.SetConsoleScreenBufferSize
@ stdcall SetConsoleTextAttribute(long long) kernelbase.SetConsoleTextAttribute
@ stdcall SetConsoleTitleA(str) kernelbase.SetConsoleTitleA
@ stdcall SetConsoleTitleW(wstr) kernelbase.SetConsoleTitleW
@ stdcall SetConsoleWindowInfo(long long ptr) kernelbase.SetConsoleWindowInfo
#@ stdcall SetCriticalSectionSpinCount(ptr long) ntdll.RtlSetCriticalSectionSpinCount
#@ stdcall SetCurrentConsoleFontEx(long long ptr)
#@ stdcall SetCurrentDirectoryA(str)
#@ stdcall SetCurrentDirectoryW(wstr)
#@ stdcall SetDefaultDllDirectories(long)
# #@ stub SetDynamicTimeZoneInformation
#@ stdcall SetEndOfFile(long)
#@ stdcall SetEnvironmentStringsA(str)
#@ stdcall SetEnvironmentStringsW(wstr)
#@ stdcall SetEnvironmentVariableA(str str)
#@ stdcall SetEnvironmentVariableW(wstr wstr)
#@ stdcall SetErrorMode(long)
#@ stdcall SetEvent(long)
#@ stdcall SetEventWhenCallbackReturns(ptr long) ntdll.TpCallbackSetEventOnCompletion
#@ stdcall SetFileApisToANSI()
#@ stdcall SetFileApisToOEM()
#@ stdcall SetFileAttributesA(str long)
#@ stdcall SetFileAttributesW(wstr long)
#@ stdcall SetFileInformationByHandle(long long ptr long)
# #@ stub SetFileIoOverlappedRange
#@ stdcall SetFilePointer(long long ptr long)
#@ stdcall SetFilePointerEx(long int64 ptr long)
@ stdcall SetFileSecurityW(wstr long ptr) kernelbase.SetFileSecurityW
#@ stdcall SetFileTime(long ptr ptr ptr)
#@ stdcall SetFileValidData(ptr int64)
#@ stdcall SetHandleCount(long)
#@ stdcall SetHandleInformation(long long long)
# #@ stub SetIsDeveloperModeEnabled
# #@ stub SetIsSideloadingEnabled
@ stdcall SetKernelObjectSecurity(long long ptr) kernelbase.SetKernelObjectSecurity
#@ stub SetLastConsoleEventActive
#@ stdcall SetLastError(long) ntdll.RtlSetLastWin32Error
#@ stdcall SetLocalTime(ptr)
#@ stdcall SetLocaleInfoW(long long wstr)
#@ stdcall SetNamedPipeHandleState(long ptr ptr ptr)
@ stdcall SetPriorityClass(long long) kernelbase.SetPriorityClass
@ stdcall SetPrivateObjectSecurity(long ptr ptr ptr long) kernelbase.SetPrivateObjectSecurity
@ stdcall SetPrivateObjectSecurityEx(long ptr ptr long ptr long) kernelbase.SetPrivateObjectSecurityEx
@ stdcall SetProcessAffinityUpdateMode(ptr long) kernelbase.SetProcessAffinityUpdateMode
#@ stdcall SetProcessDefaultCpuSets(ptr ptr long)
#@ stdcall SetProcessGroupAffinity(long ptr ptr)
@ stdcall SetProcessInformation(long long ptr long) kernelbase.SetProcessInformation
@ stdcall SetProcessMitigationPolicy(long ptr long) kernelbase.SetProcessMitigationPolicy
@ stdcall SetProcessPreferredUILanguages(long ptr ptr) kernelbase.SetProcessPreferredUILanguages
#@ stdcall SetProcessPriorityBoost(long long)
@ stdcall SetProcessShutdownParameters(long long) kernelbase.SetProcessShutdownParameters
@ stdcall SetProcessValidCallTargets(long ptr long long ptr) kernelbase.SetProcessValidCallTargets
#@ stdcall SetProcessWorkingSetSizeEx(long long long long)
# #@ stub SetProtectedPolicy
# #@ stub SetRoamingLastObservedChangeTime
#@ stub SetSecurityAccessMask
@ stdcall SetSecurityDescriptorControl(ptr long long) kernelbase.SetSecurityDescriptorControl
@ stdcall SetSecurityDescriptorDacl(ptr long ptr long) kernelbase.SetSecurityDescriptorDacl
@ stdcall SetSecurityDescriptorGroup(ptr ptr long) kernelbase.SetSecurityDescriptorGroup
@ stdcall SetSecurityDescriptorOwner(ptr ptr long) kernelbase.SetSecurityDescriptorOwner
#@ stub SetSecurityDescriptorRMControl
@ stdcall SetSecurityDescriptorSacl(ptr long ptr long) kernelbase.SetSecurityDescriptorSacl
# #@ stub SetStateVersion
@ stdcall SetStdHandle(long long) kernelbase.SetStdHandle
#@ stdcall SetStdHandleEx(long long ptr)
#@ stdcall SetSystemFileCacheSize(long long long)
#@ stdcall SetSystemTime(ptr)
#@ stdcall SetSystemTimeAdjustment(long long)
#@ stdcall SetThreadContext(long ptr)
@ stdcall SetThreadDescription(ptr wstr) kernelbase.SetThreadDescription
@ stdcall SetThreadErrorMode(long ptr) kernelbase.SetThreadErrorMode
#@ stdcall SetThreadGroupAffinity(long ptr ptr)
#@ stdcall SetThreadIdealProcessor(long long)
@ stdcall SetThreadIdealProcessorEx(long ptr ptr) kernelbase.SetThreadIdealProcessorEx
@ stdcall SetThreadInformation(long long ptr long) kernelbase.SetThreadInformation
#@ stdcall SetThreadLocale(long)
@ stdcall SetThreadPreferredUILanguages(long ptr ptr) kernelbase.SetThreadPreferredUILanguages
@ stdcall SetThreadPriority(long long) kernelbase.SetThreadPriority
@ stdcall SetThreadPriorityBoost(long long) kernelbase.SetThreadPriorityBoost
#@ stdcall SetThreadSelectedCpuSets(ptr ptr long)
@ stdcall SetThreadStackGuarantee(ptr) kernelbase.SetThreadStackGuarantee
@ stdcall SetThreadToken(ptr ptr) kernelbase.SetThreadToken
#@ stdcall SetThreadUILanguage(long)
@ stdcall SetThreadpoolStackInformation(ptr ptr) kernelbase.SetThreadpoolStackInformation
@ stdcall SetThreadpoolThreadMaximum(ptr long) kernelbase.SetThreadpoolThreadMaximum
@ stdcall SetThreadpoolThreadMinimum(ptr long) kernelbase.SetThreadpoolThreadMinimum
@ stdcall SetThreadpoolTimer(ptr ptr long long) kernelbase.SetThreadpoolTimer
# #@ stub SetThreadpoolTimerEx
@ stdcall SetThreadpoolWait(ptr long ptr) kernelbase.SetThreadpoolWait
# #@ stub SetThreadpoolWaitEx
#@ stdcall SetTimeZoneInformation(ptr)
@ stdcall SetTokenInformation(long long ptr long) kernelbase.SetTokenInformation
#@ stdcall SetUnhandledExceptionFilter(ptr)
@ stdcall SetUserGeoID(long) kernelbase.SetUserGeoID
@ stdcall SetUserGeoName(wstr) kernelbase.SetUserGeoName
#@ stdcall SetWaitableTimer(long ptr long ptr ptr long)
@ stdcall SetWaitableTimerEx(long ptr long ptr ptr ptr long) kernelbase.SetWaitableTimerEx
@ stdcall -arch=x86_64,arm64ec SetXStateFeaturesMask(ptr int64) kernelbase.SetXStateFeaturesMask
#@ stdcall SetupComm(long long long)
# #@ stub SharedLocalIsEnabled
#@ stdcall SignalObjectAndWait(long long long long)
#@ stdcall SizeofResource(long long)
@ stdcall Sleep(long) kernelbase.Sleep
@ stdcall SleepConditionVariableCS(ptr ptr long) kernelbase.SleepConditionVariableCS
@ stdcall SleepConditionVariableSRW(ptr ptr long long) kernelbase.SleepConditionVariableSRW
@ stdcall SleepEx(long long) kernelbase.SleepEx
#@ stub SpecialMBToWC
@ stdcall StartThreadpoolIo(ptr) kernelbase.StartThreadpoolIo
# #@ stub StmAlignSize
# #@ stub StmAllocateFlat
# #@ stub StmCoalesceChunks
# #@ stub StmDeinitialize
# #@ stub StmInitialize
# #@ stub StmReduceSize
# #@ stub StmReserve
# #@ stub StmWrite
@ stdcall StrCSpnA(str str) kernelbase.StrCSpnA
@ stdcall StrCSpnIA(str str) kernelbase.StrCSpnIA
@ stdcall StrCSpnIW(wstr wstr) kernelbase.StrCSpnIW
@ stdcall StrCSpnW(wstr wstr) kernelbase.StrCSpnW
@ stdcall StrCatBuffA(str str long) kernelbase.StrCatBuffA
@ stdcall StrCatBuffW(wstr wstr long) kernelbase.StrCatBuffW
@ stdcall StrCatChainW(ptr long long wstr) kernelbase.StrCatChainW
@ stdcall StrChrA(str long) kernelbase.StrChrA
#@ stub StrChrA_MB
@ stdcall StrChrIA(str long) kernelbase.StrChrIA
@ stdcall StrChrIW(wstr long) kernelbase.StrChrIW
#@ stub StrChrNIW
@ stdcall StrChrNW(wstr long long) kernelbase.StrChrNW
@ stdcall StrChrW(wstr long) kernelbase.StrChrW
@ stdcall StrCmpCA(str str) kernelbase.StrCmpCA
@ stdcall StrCmpCW(wstr wstr) kernelbase.StrCmpCW
@ stdcall StrCmpICA(str str) kernelbase.StrCmpICA
@ stdcall StrCmpICW(wstr wstr) kernelbase.StrCmpICW
@ stdcall StrCmpIW(wstr wstr) kernelbase.StrCmpIW
@ stdcall StrCmpLogicalW(wstr wstr) kernelbase.StrCmpLogicalW
@ stdcall StrCmpNA(str str long) kernelbase.StrCmpNA
@ stdcall StrCmpNCA(str str long) kernelbase.StrCmpNCA
@ stdcall StrCmpNCW(wstr wstr long) kernelbase.StrCmpNCW
@ stdcall StrCmpNIA(str str long) kernelbase.StrCmpNIA
@ stdcall StrCmpNICA(str str long) kernelbase.StrCmpNICA
@ stdcall StrCmpNICW(wstr wstr long) kernelbase.StrCmpNICW
@ stdcall StrCmpNIW(wstr wstr long) kernelbase.StrCmpNIW
@ stdcall StrCmpNW(wstr wstr long) kernelbase.StrCmpNW
@ stdcall StrCmpW(wstr wstr) kernelbase.StrCmpW
@ stdcall StrCpyNW(ptr wstr long) kernelbase.StrCpyNW
@ stdcall StrCpyNXA(ptr str long) kernelbase.StrCpyNXA
@ stdcall StrCpyNXW(ptr wstr long) kernelbase.StrCpyNXW
@ stdcall StrDupA(str) kernelbase.StrDupA
@ stdcall StrDupW(wstr) kernelbase.StrDupW
@ stdcall StrIsIntlEqualA(long str str long) kernelbase.StrIsIntlEqualA
@ stdcall StrIsIntlEqualW(long wstr wstr long) kernelbase.StrIsIntlEqualW
@ stdcall StrPBrkA(str str) kernelbase.StrPBrkA
@ stdcall StrPBrkW(wstr wstr) kernelbase.StrPBrkW
@ stdcall StrRChrA(str str long) kernelbase.StrRChrA
@ stdcall StrRChrIA(str str long) kernelbase.StrRChrIA
@ stdcall StrRChrIW(wstr wstr long) kernelbase.StrRChrIW
@ stdcall StrRChrW(wstr wstr long) kernelbase.StrRChrW
@ stdcall StrRStrIA(str str str) kernelbase.StrRStrIA
@ stdcall StrRStrIW(wstr wstr wstr) kernelbase.StrRStrIW
@ stdcall StrSpnA(str str) kernelbase.StrSpnA
@ stdcall StrSpnW(wstr wstr) kernelbase.StrSpnW
@ stdcall StrStrA(str str) kernelbase.StrStrA
@ stdcall StrStrIA(str str) kernelbase.StrStrIA
@ stdcall StrStrIW(wstr wstr) kernelbase.StrStrIW
@ stdcall StrStrNIW(wstr wstr long) kernelbase.StrStrNIW
@ stdcall StrStrNW(wstr wstr long) kernelbase.StrStrNW
@ stdcall StrStrW(wstr wstr) kernelbase.StrStrW
@ stdcall StrToInt64ExA(str long ptr) kernelbase.StrToInt64ExA
@ stdcall StrToInt64ExW(wstr long ptr) kernelbase.StrToInt64ExW
@ stdcall StrToIntA(str) kernelbase.StrToIntA
@ stdcall StrToIntExA(str long ptr) kernelbase.StrToIntExA
@ stdcall StrToIntExW(wstr long ptr) kernelbase.StrToIntExW
@ stdcall StrToIntW(wstr) kernelbase.StrToIntW
@ stdcall StrTrimA(str str) kernelbase.StrTrimA
@ stdcall StrTrimW(wstr wstr) kernelbase.StrTrimW
@ stdcall SubmitThreadpoolWork(ptr) kernelbase.SubmitThreadpoolWork
# #@ stub SubscribeEdpEnabledStateChange
# #@ stub SubscribeStateChangeNotification
@ stdcall SuspendThread(long) kernelbase.SuspendThread
#@ stdcall SwitchToFiber(ptr)
@ stdcall SwitchToThread() kernelbase.SwitchToThread
#@ stdcall SystemTimeToFileTime(ptr ptr)
#@ stdcall SystemTimeToTzSpecificLocalTime(ptr ptr ptr)
#@ stub SystemTimeToTzSpecificLocalTimeEx
@ stdcall TerminateProcess(ptr long) kernelbase.TerminateProcess
# #@ stub TerminateProcessOnMemoryExhaustion
@ stdcall TerminateThread(ptr long) kernelbase.TerminateThread
@ stdcall TlsAlloc() kernelbase.TlsAlloc
@ stdcall TlsFree(long) kernelbase.TlsFree
@ stdcall -norelay TlsGetValue(long) kernelbase.TlsGetValue
@ stdcall -norelay TlsSetValue(long ptr) kernelbase.TlsSetValue
#@ stdcall TraceEvent(int64 ptr) ntdll.EtwLogTraceEvent
#@ varargs TraceMessage(int64 long ptr long) ntdll.EtwTraceMessage
#@ stdcall TraceMessageVa(int64 long ptr long ptr) ntdll.EtwTraceMessageVa
#@ stdcall TransactNamedPipe(long ptr long ptr long ptr ptr)
#@ stdcall TransmitCommChar(long long)
#@ stdcall TryAcquireSRWLockExclusive(ptr) ntdll.RtlTryAcquireSRWLockExclusive
#@ stdcall TryAcquireSRWLockShared(ptr) ntdll.RtlTryAcquireSRWLockShared
#@ stdcall TryEnterCriticalSection(ptr) ntdll.RtlTryEnterCriticalSection
@ stdcall TrySubmitThreadpoolCallback(ptr ptr ptr) kernelbase.TrySubmitThreadpoolCallback
#@ stdcall TzSpecificLocalTimeToSystemTime(ptr ptr ptr)
#@ stub TzSpecificLocalTimeToSystemTimeEx
#@ stdcall UnhandledExceptionFilter(ptr)
#@ stdcall UnlockFile(long long long long long)
#@ stdcall UnlockFileEx(long long long long ptr)
#@ stdcall UnmapViewOfFile(ptr)
#@ stdcall UnmapViewOfFile2(long ptr long)
#@ stdcall UnmapViewOfFileEx(ptr long)
# #@ stub UnregisterBadMemoryNotification
# #@ stub UnregisterGPNotificationInternal
# #@ stub UnregisterStateChangeNotification
# #@ stub UnregisterStateLock
#@ stdcall UnregisterTraceGuids(int64) ntdll.EtwUnregisterTraceGuids
#@ stdcall UnregisterWaitEx(long long)
# #@ stub UnsubscribeEdpEnabledStateChange
# #@ stub UnsubscribeStateChangeNotification
# #@ stub UpdatePackageStatus
# #@ stub UpdatePackageStatusForUser
@ stdcall -version=0x600+ UpdateProcThreadAttribute(ptr long ptr ptr ptr ptr ptr) kernelbase.UpdateProcThreadAttribute
@ stdcall UrlApplySchemeA(str ptr ptr long) kernelbase.UrlApplySchemeA
@ stdcall UrlApplySchemeW(wstr ptr ptr long) kernelbase.UrlApplySchemeW
@ stdcall UrlCanonicalizeA(str ptr ptr long) kernelbase.UrlCanonicalizeA
@ stdcall UrlCanonicalizeW(wstr ptr ptr long) kernelbase.UrlCanonicalizeW
@ stdcall UrlCombineA(str str ptr ptr long) kernelbase.UrlCombineA
@ stdcall UrlCombineW(wstr wstr ptr ptr long) kernelbase.UrlCombineW
@ stdcall UrlCompareA(str str long) kernelbase.UrlCompareA
@ stdcall UrlCompareW(wstr wstr long) kernelbase.UrlCompareW
@ stdcall UrlCreateFromPathA(str ptr ptr long) kernelbase.UrlCreateFromPathA
@ stdcall UrlCreateFromPathW(wstr ptr ptr long) kernelbase.UrlCreateFromPathW
@ stdcall UrlEscapeA(str ptr ptr long) kernelbase.UrlEscapeA
@ stdcall UrlEscapeW(wstr ptr ptr long) kernelbase.UrlEscapeW
@ stdcall UrlFixupW(wstr wstr long) kernelbase.UrlFixupW
@ stdcall UrlGetLocationA(str) kernelbase.UrlGetLocationA
@ stdcall UrlGetLocationW(wstr) kernelbase.UrlGetLocationW
@ stdcall UrlGetPartA(str ptr ptr long long) kernelbase.UrlGetPartA
@ stdcall UrlGetPartW(wstr ptr ptr long long) kernelbase.UrlGetPartW
@ stdcall UrlHashA(str ptr long) kernelbase.UrlHashA
@ stdcall UrlHashW(wstr ptr long) kernelbase.UrlHashW
@ stdcall UrlIsA(str long) kernelbase.UrlIsA
@ stdcall UrlIsNoHistoryA(str) kernelbase.UrlIsNoHistoryA
@ stdcall UrlIsNoHistoryW(wstr) kernelbase.UrlIsNoHistoryW
@ stdcall UrlIsOpaqueA(str) kernelbase.UrlIsOpaqueA
@ stdcall UrlIsOpaqueW(wstr) kernelbase.UrlIsOpaqueW
@ stdcall UrlIsW(wstr long) kernelbase.UrlIsW
@ stdcall UrlUnescapeA(str ptr ptr long) kernelbase.UrlUnescapeA
@ stdcall UrlUnescapeW(wstr ptr ptr long) kernelbase.UrlUnescapeW
@ stdcall VerFindFileA(long str str str ptr ptr ptr ptr) kernelbase.VerFindFileA
@ stdcall VerFindFileW(long wstr wstr wstr ptr ptr ptr ptr) kernelbase.VerFindFileW
#@ stdcall VerLanguageNameA(long str long)
#@ stdcall VerLanguageNameW(long wstr long)
@ stdcall VerQueryValueA(ptr str ptr ptr) kernelbase.VerQueryValueA
@ stdcall VerQueryValueW(ptr wstr ptr ptr) kernelbase.VerQueryValueW
#@ stdcall -ret64 VerSetConditionMask(long long long long) ntdll.VerSetConditionMask
# #@ stub VerifyApplicationUserModelId
# #@ stub VerifyPackageFamilyName
# #@ stub VerifyPackageFullName
# #@ stub VerifyPackageId
# #@ stub VerifyPackageRelativeApplicationId
# #@ stub VerifyScripts
@ stdcall VirtualAlloc2(long ptr long long long ptr long) kernelbase.VirtualAlloc2
#@ stdcall VirtualAlloc2FromApp(long ptr long long long ptr long)
#@ stdcall VirtualAlloc(ptr long long long)
#@ stdcall VirtualAllocEx(long ptr long long long)
#@ stdcall VirtualAllocExNuma(long ptr long long long long)
#@ stdcall VirtualAllocFromApp(ptr long long long)
#@ stdcall VirtualFree(ptr long long)
#@ stdcall VirtualFreeEx(long ptr long long)
#@ stdcall VirtualLock(ptr long)
#@ stdcall VirtualProtect(ptr long long ptr)
#@ stdcall VirtualProtectEx(long ptr long long ptr)
# #@ stub VirtualProtectFromApp
#@ stdcall VirtualQuery(ptr ptr long)
#@ stdcall VirtualQueryEx(long ptr ptr long)
#@ stdcall VirtualUnlock(ptr long)
# #@ stub WTSGetServiceSessionId
# #@ stub WTSIsServerContainer
#@ stdcall WaitCommEvent(long ptr ptr)
#@ stdcall WaitForDebugEvent(ptr long)
#@ stdcall WaitForDebugEventEx(ptr long)
# #@ stub WaitForMachinePolicyForegroundProcessingInternal
#@ stdcall WaitForMultipleObjects(long ptr long long)
#@ stdcall WaitForMultipleObjectsEx(long ptr long long long)
#@ stdcall WaitForSingleObject(long long)
#@ stdcall WaitForSingleObjectEx(long long long)
@ stdcall WaitForThreadpoolIoCallbacks(ptr long) kernelbase.WaitForThreadpoolIoCallbacks
@ stdcall WaitForThreadpoolTimerCallbacks(ptr long) kernelbase.WaitForThreadpoolTimerCallbacks
@ stdcall WaitForThreadpoolWaitCallbacks(ptr long) kernelbase.WaitForThreadpoolWaitCallbacks
@ stdcall WaitForThreadpoolWorkCallbacks(ptr long) kernelbase.WaitForThreadpoolWorkCallbacks
# #@ stub WaitForUserPolicyForegroundProcessingInternal
#@ stdcall WaitNamedPipeW(wstr long)
@ stdcall WaitOnAddress(ptr ptr long long) kernelbase.WaitOnAddress
@ stdcall WakeAllConditionVariable(ptr) kernelbase.WakeAllConditionVariable
@ stdcall WakeByAddressAll(ptr) kernelbase.WakeByAddressAll
@ stdcall WakeByAddressSingle(ptr) kernelbase.WakeByAddressSingle
@ stdcall WakeConditionVariable(ptr) kernelbase.WakeConditionVariable
@ stdcall WerGetFlags(ptr ptr) kernelbase.WerGetFlags
@ stdcall WerRegisterFile(wstr long long) kernelbase.WerRegisterFile
@ stdcall WerRegisterMemoryBlock(ptr long) kernelbase.WerRegisterMemoryBlock
@ stdcall WerRegisterRuntimeExceptionModule(wstr ptr) kernelbase.WerRegisterRuntimeExceptionModule
@ stdcall WerSetFlags(long) kernelbase.WerSetFlags
@ stdcall WerUnregisterFile(wstr) kernelbase.WerUnregisterFile
@ stdcall WerUnregisterMemoryBlock(ptr) kernelbase.WerUnregisterMemoryBlock
@ stdcall WerUnregisterRuntimeExceptionModule(wstr ptr) kernelbase.WerUnregisterRuntimeExceptionModule
# #@ stub WerpNotifyLoadStringResource
# #@ stub WerpNotifyUseStringResource
#@ stdcall WideCharToMultiByte(long long wstr long ptr long ptr ptr)
#@ stdcall Wow64DisableWow64FsRedirection(ptr)
#@ stdcall Wow64EnableWow64FsRedirection(long) kernelbase_Wow64EnableWow64FsRedirection
@ stdcall Wow64GetThreadContext(long ptr) kernelbase.Wow64GetThreadContext
#@ stdcall Wow64RevertWow64FsRedirection(ptr)
@ stdcall Wow64SetThreadContext(long ptr) kernelbase.Wow64SetThreadContext
# #@ stub Wow64SetThreadDefaultGuestMachine
# #@ stub Wow64SuspendThread
# #@ stub -arch=i386 Wow64Transition
@ stdcall WriteConsoleA(long ptr long ptr ptr) kernelbase.WriteConsoleA
@ stdcall WriteConsoleInputA(long ptr long ptr) kernelbase.WriteConsoleInputA
@ stdcall WriteConsoleInputW(long ptr long ptr) kernelbase.WriteConsoleInputW
@ stdcall WriteConsoleOutputA(long ptr long long ptr) kernelbase.WriteConsoleOutputA
@ stdcall WriteConsoleOutputAttribute(long ptr long long ptr) kernelbase.WriteConsoleOutputAttribute
@ stdcall WriteConsoleOutputCharacterA(long ptr long long ptr) kernelbase.WriteConsoleOutputCharacterA
@ stdcall WriteConsoleOutputCharacterW(long ptr long long ptr) kernelbase.WriteConsoleOutputCharacterW
@ stdcall WriteConsoleOutputW(long ptr long long ptr) kernelbase.WriteConsoleOutputW
@ stdcall WriteConsoleW(long ptr long ptr ptr) kernelbase.WriteConsoleW
#@ stdcall WriteFile(long ptr long ptr ptr)
#@ stdcall WriteFileEx(long ptr long ptr ptr)
#@ stdcall WriteFileGather(long ptr long ptr ptr)
#@ stdcall WriteProcessMemory(long ptr ptr long ptr)
# #@ stub WriteStateAtomValue
# #@ stub WriteStateContainerValue
#@ stdcall ZombifyActCtx(ptr)
# #@ stub _AddMUIStringToCache
# #@ stub _GetMUIStringFromCache
# #@ stub _OpenMuiStringCache
#@ stdcall -arch=!i386 -private __C_specific_handler(ptr long ptr ptr) ntdll.__C_specific_handler
#@ cdecl -arch=!i386 -norelay __chkstk() ntdll.__chkstk
# #@ stub __dllonexit3
#@ stub __misaligned_access
# #@ stub __wgetmainargs
# #@ stub _amsg_exit
# #@ stub _c_exit
# #@ stub _cexit
# #@ stub _exit
# #@ stub _initterm
# #@ stub _initterm_e
# #@ stub _invalid_parameter
#@ stdcall -arch=x86_64 -private _local_unwind(ptr ptr) ntdll._local_unwind
# #@ stub _onexit
# #@ stub _purecall
# #@ stub _time64
# #@ stub atexit
# #@ stub exit
# #@ stub hgets
# #@ stub hwprintf
@ stdcall lstrcmp(str str) kernelbase.lstrcmp
@ stdcall lstrcmpA(str str) kernelbase.lstrcmpA
@ stdcall lstrcmpW(wstr wstr) kernelbase.lstrcmpW
@ stdcall lstrcmpi(str str) kernelbase.lstrcmpi
@ stdcall lstrcmpiA(str str) kernelbase.lstrcmpiA
@ stdcall lstrcmpiW(wstr wstr) kernelbase.lstrcmpiW
#@ stdcall lstrcpyn(ptr str long) KERNELBASE_lstrcpynA
#@ stdcall lstrcpynA(ptr str long) KERNELBASE_lstrcpynA
#@ stdcall lstrcpynW(ptr wstr long) KERNELBASE_lstrcpynW
#@ stdcall lstrlen(str) KERNELBASE_lstrlenA
#@ stdcall lstrlenA(str) KERNELBASE_lstrlenA
#@ stdcall lstrlenW(wstr) KERNELBASE_lstrlenW
# #@ stub time
# #@ stub wprintf

@ stdcall GetNumaProcessorNodeEx(ptr ptr) kernelbase.GetNumaProcessorNodeEx
@ stdcall SetConsoleMenuClose(long) kernelbase.SetConsoleMenuClose
@ stdcall SetLastConsoleEventActive() kernelbase.SetLastConsoleEventActive
@ stdcall WriteConsoleInputVDMA(long long long long) kernelbase.WriteConsoleInputVDMA
@ stdcall WriteConsoleInputVDMW(long long long long) kernelbase.WriteConsoleInputVDMW
@ stdcall GetConsoleCursorMode(long ptr ptr) kernelbase.GetConsoleCursorMode
@ stdcall SetConsoleHandleInformation(ptr long long) kernelbase.SetConsoleHandleInformation
@ stdcall GetConsoleAliasesA(str long str) kernelbase.GetConsoleAliasesA
@ stdcall RegisterConsoleIME(ptr ptr) kernelbase.RegisterConsoleIME
@ stdcall GetConsoleInputWaitHandle() kernelbase.GetConsoleInputWaitHandle
@ stdcall GetConsoleFontInfo(long long long ptr) kernelbase.GetConsoleFontInfo
@ stdcall GetConsoleHandleInformation(ptr ptr) kernelbase.GetConsoleHandleInformation
@ stdcall SetConsoleIcon(ptr) kernelbase.SetConsoleIcon
@ stdcall UnregisterConsoleIME() kernelbase.UnregisterConsoleIME
@ stdcall ShowConsoleCursor(long long) kernelbase.ShowConsoleCursor
@ stdcall SetConsolePalette(long long long) kernelbase.SetConsolePalette
@ stdcall OpenConsoleW(wstr long long long) kernelbase.OpenConsoleW
@ stdcall SetConsoleNlsMode(long long) kernelbase.SetConsoleNlsMode
@ stdcall DuplicateConsoleHandle(long long long long) kernelbase.DuplicateConsoleHandle
@ stdcall SetConsoleNumberOfCommandsW(long long) kernelbase.SetConsoleNumberOfCommandsW
@ stdcall GetConsoleAliasesW(wstr long wstr) kernelbase.GetConsoleAliasesW
@ stdcall -version=0x502-0x600 -arch=win64 ConsoleIMERoutine(ptr) kernelbase.ConsoleIMERoutine
@ stdcall SetConsoleCursorMode(long long long) kernelbase.SetConsoleCursorMode
@ stdcall GetConsoleSelectionInfo(ptr) kernelbase.GetConsoleSelectionInfo
@ stdcall SetConsoleKeyShortcuts(long long long long) kernelbase.SetConsoleKeyShortcuts
@ stdcall GetConsoleHardwareState(long long ptr) kernelbase.GetConsoleHardwareState
@ cdecl IntCheckForConsoleFileName(wstr long) kernelbase.IntCheckForConsoleFileName
@ stdcall GetConsoleNlsMode(long ptr) kernelbase.GetConsoleNlsMode
@ stdcall CloseConsoleHandle(long) kernelbase.CloseConsoleHandle
@ stdcall SetConsoleLocalEUDC(long long long long) kernelbase.SetConsoleLocalEUDC
@ stdcall GetConsoleKeyboardLayoutNameA(ptr) kernelbase.GetConsoleKeyboardLayoutNameA
@ stdcall VerifyConsoleIoHandle(long) kernelbase.VerifyConsoleIoHandle
@ stdcall GetConsoleAliasExesA(str long) kernelbase.GetConsoleAliasExesA
@ stdcall GetConsoleKeyboardLayoutNameW(ptr) kernelbase.GetConsoleKeyboardLayoutNameW
@ stdcall ConDllInitialize(long wstr) kernelbase.ConDllInitialize
@ stdcall ReadConsoleInputExA(long ptr long ptr long) kernelbase.ReadConsoleInputExA
@ stdcall InvalidateConsoleDIBits(long long) kernelbase.InvalidateConsoleDIBits
@ stdcall SetConsoleHardwareState(long long long) kernelbase.SetConsoleHardwareState
@ stdcall GetNumberOfConsoleFonts() kernelbase.GetNumberOfConsoleFonts
@ stdcall GetConsoleCharType(long long ptr) kernelbase.GetConsoleCharType
@ stdcall RegisterConsoleOS2(long) kernelbase.RegisterConsoleOS2
@ stdcall ConsoleMenuControl(long long long) kernelbase.ConsoleMenuControl
@ stdcall -version=0x351-0x502 SetConsoleCommandHistoryMode(long) kernelbase.SetConsoleCommandHistoryMode
@ stdcall SetConsoleCursor(long long) kernelbase.SetConsoleCursor
@ stdcall SetConsoleFont(long long) kernelbase.SetConsoleFont
@ stdcall GetConsoleAliasExesW(wstr long) kernelbase.GetConsoleAliasExesW
@ stdcall SetConsoleOS2OemFormat(long) kernelbase.SetConsoleOS2OemFormat
@ stdcall SetConsoleNumberOfCommandsA(long long) kernelbase.SetConsoleNumberOfCommandsA
@ stdcall ReadConsoleInputExW(long ptr long ptr long) kernelbase.ReadConsoleInputExW
@ stdcall SetConsoleMaximumWindowSize(long long) kernelbase.SetConsoleMaximumWindowSize
