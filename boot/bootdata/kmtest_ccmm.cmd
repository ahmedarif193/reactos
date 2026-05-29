@echo off

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: START

rem --- Ke (Kernel) tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeApc
%SystemRoot%\bin\kmtest_.exe KeApc
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeApc

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeDeviceQueue
%SystemRoot%\bin\kmtest_.exe KeDeviceQueue
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeDeviceQueue

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeDpc
%SystemRoot%\bin\kmtest_.exe KeDpc
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeDpc

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeEvent
%SystemRoot%\bin\kmtest_.exe KeEvent
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeEvent

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeFloatPointState
%SystemRoot%\bin\kmtest_.exe KeFloatPointState
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeFloatPointState

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeGuardedMutex
%SystemRoot%\bin\kmtest_.exe KeGuardedMutex
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeGuardedMutex

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeIrql
%SystemRoot%\bin\kmtest_.exe KeIrql
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeIrql

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeMutex
%SystemRoot%\bin\kmtest_.exe KeMutex
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeMutex

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeProcessor (SKIPPED)
rem KeProcessor takes 32+ seconds of stall, zero checks — disabled intentionally
rem %SystemRoot%\bin\kmtest_.exe KeProcessor
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeProcessor (SKIPPED)

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeSpinLock
%SystemRoot%\bin\kmtest_.exe KeSpinLock
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeSpinLock

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeTimer
%SystemRoot%\bin\kmtest_.exe KeTimer
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeTimer

rem --- Ex (Executive) tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExCallback
%SystemRoot%\bin\kmtest_.exe ExCallback
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExCallback

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExDoubleList
%SystemRoot%\bin\kmtest_.exe ExDoubleList
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExDoubleList

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExFastMutex
%SystemRoot%\bin\kmtest_.exe ExFastMutex
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExFastMutex

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExHardError
%SystemRoot%\bin\kmtest_.exe ExHardError
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExHardError

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExHardErrorInteractive (SKIPPED)
rem ExHardErrorInteractive pops up a GUI dialog — disabled for automated testing
rem %SystemRoot%\bin\kmtest_.exe ExHardErrorInteractive
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExHardErrorInteractive (SKIPPED)

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExInterlocked
%SystemRoot%\bin\kmtest_.exe ExInterlocked
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExInterlocked

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExPools
%SystemRoot%\bin\kmtest_.exe ExPools
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExPools

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExResource
%SystemRoot%\bin\kmtest_.exe ExResource
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExResource

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExSequencedList
%SystemRoot%\bin\kmtest_.exe ExSequencedList
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExSequencedList

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExSingleList
%SystemRoot%\bin\kmtest_.exe ExSingleList
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExSingleList

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExTimer
%SystemRoot%\bin\kmtest_.exe ExTimer
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExTimer

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ExUuid
%SystemRoot%\bin\kmtest_.exe ExUuid
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ExUuid

rem --- Ob (Object Manager) tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObHandle
%SystemRoot%\bin\kmtest_.exe ObHandle
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObHandle

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObQuery
%SystemRoot%\bin\kmtest_.exe ObQuery
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObQuery

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObReference
%SystemRoot%\bin\kmtest_.exe ObReference
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObReference

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObSecurity
%SystemRoot%\bin\kmtest_.exe ObSecurity
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObSecurity

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObSymbolicLink
%SystemRoot%\bin\kmtest_.exe ObSymbolicLink
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObSymbolicLink

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObType
%SystemRoot%\bin\kmtest_.exe ObType
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObType

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObTypeClean
%SystemRoot%\bin\kmtest_.exe ObTypeClean
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObTypeClean

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObTypeNoClean
%SystemRoot%\bin\kmtest_.exe ObTypeNoClean
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObTypeNoClean

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ObTypes
%SystemRoot%\bin\kmtest_.exe ObTypes
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ObTypes

rem --- Ps (Process Structure) tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN PsNotify
%SystemRoot%\bin\kmtest_.exe PsNotify
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END PsNotify

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN PsQuota
%SystemRoot%\bin\kmtest_.exe PsQuota
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END PsQuota

rem --- Se (Security) tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN SeInheritance
%SystemRoot%\bin\kmtest_.exe SeInheritance
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END SeInheritance

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN SeLogonSession
%SystemRoot%\bin\kmtest_.exe SeLogonSession
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END SeLogonSession

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN SeQueryInfoToken
%SystemRoot%\bin\kmtest_.exe SeQueryInfoToken
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END SeQueryInfoToken

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN SeTokenFiltering
%SystemRoot%\bin\kmtest_.exe SeTokenFiltering
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END SeTokenFiltering

rem --- FsRtl tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN FsRtlDissect
%SystemRoot%\bin\kmtest_.exe FsRtlDissect
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END FsRtlDissect

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN FsRtlExpression
%SystemRoot%\bin\kmtest_.exe FsRtlExpression
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END FsRtlExpression

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN FsRtlLegal
%SystemRoot%\bin\kmtest_.exe FsRtlLegal
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END FsRtlLegal

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN FsRtlMcb
%SystemRoot%\bin\kmtest_.exe FsRtlMcb
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END FsRtlMcb

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN FsRtlRemoveDotsFromPath
%SystemRoot%\bin\kmtest_.exe FsRtlRemoveDotsFromPath
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END FsRtlRemoveDotsFromPath

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN FsRtlTunnel
%SystemRoot%\bin\kmtest_.exe FsRtlTunnel
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END FsRtlTunnel

rem --- Io tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoCreateFile
%SystemRoot%\bin\kmtest_.exe IoCreateFile
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoCreateFile

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoDeviceInterface
%SystemRoot%\bin\kmtest_.exe IoDeviceInterface
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoDeviceInterface

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoDeviceObject
%SystemRoot%\bin\kmtest_.exe IoDeviceObject
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoDeviceObject

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoEvent
%SystemRoot%\bin\kmtest_.exe IoEvent
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoEvent

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoFilesystem
%SystemRoot%\bin\kmtest_.exe IoFilesystem
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoFilesystem

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoInterrupt
%SystemRoot%\bin\kmtest_.exe IoInterrupt
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoInterrupt

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoIrp
%SystemRoot%\bin\kmtest_.exe IoIrp
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoIrp

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoMdl
%SystemRoot%\bin\kmtest_.exe IoMdl
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoMdl

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoReadWrite
%SystemRoot%\bin\kmtest_.exe IoReadWrite
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoReadWrite

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN IoVolume
%SystemRoot%\bin\kmtest_.exe IoVolume
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END IoVolume

rem --- Additional Ke/Mm/Zw tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KePcr
%SystemRoot%\bin\kmtest_.exe KePcr
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KePcr

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN KeThreadedDpc
%SystemRoot%\bin\kmtest_.exe KeThreadedDpc
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END KeThreadedDpc

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN MmAllocateContiguousNode
%SystemRoot%\bin\kmtest_.exe MmAllocateContiguousNode
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END MmAllocateContiguousNode

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN MmMapLockedPagesSpecifyCache
%SystemRoot%\bin\kmtest_.exe MmMapLockedPagesSpecifyCache
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END MmMapLockedPagesSpecifyCache

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN MmMdl
%SystemRoot%\bin\kmtest_.exe MmMdl
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END MmMdl

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN MmReservedMapping
%SystemRoot%\bin\kmtest_.exe MmReservedMapping
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END MmReservedMapping

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN MmSection
%SystemRoot%\bin\kmtest_.exe MmSection
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END MmSection

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN NtCreateSection
%SystemRoot%\bin\kmtest_.exe NtCreateSection
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END NtCreateSection

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ZwAllocateVirtualMemory
%SystemRoot%\bin\kmtest_.exe ZwAllocateVirtualMemory
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ZwAllocateVirtualMemory

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ZwCreateSection
%SystemRoot%\bin\kmtest_.exe ZwCreateSection
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ZwCreateSection

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ZwMapViewOfSection
%SystemRoot%\bin\kmtest_.exe ZwMapViewOfSection
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ZwMapViewOfSection

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN ZwWaitForMultipleObjects
%SystemRoot%\bin\kmtest_.exe ZwWaitForMultipleObjects
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END ZwWaitForMultipleObjects

rem --- Rtl tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlAvlTree
%SystemRoot%\bin\kmtest_.exe RtlAvlTree
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlAvlTree

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlAvlTreeKM
%SystemRoot%\bin\kmtest_.exe RtlAvlTreeKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlAvlTreeKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlCaptureContextUM
%SystemRoot%\bin\kmtest_.exe RtlCaptureContextUM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlCaptureContextUM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlCaptureContextKM
%SystemRoot%\bin\kmtest_.exe RtlCaptureContextKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlCaptureContextKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlException
%SystemRoot%\bin\kmtest_.exe RtlException
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlException

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlExceptionKM
%SystemRoot%\bin\kmtest_.exe RtlExceptionKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlExceptionKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlGetVersion
%SystemRoot%\bin\kmtest_.exe RtlGetVersion
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlGetVersion

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlIntSafe
%SystemRoot%\bin\kmtest_.exe RtlIntSafe
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlIntSafe

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlIntSafeKM
%SystemRoot%\bin\kmtest_.exe RtlIntSafeKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlIntSafeKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlIsValidOemCharacter
%SystemRoot%\bin\kmtest_.exe RtlIsValidOemCharacter
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlIsValidOemCharacter

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlMemory
%SystemRoot%\bin\kmtest_.exe RtlMemory
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlMemory

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlMemoryKM
%SystemRoot%\bin\kmtest_.exe RtlMemoryKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlMemoryKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlRangeList
%SystemRoot%\bin\kmtest_.exe RtlRangeList
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlRangeList

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlRegistry
%SystemRoot%\bin\kmtest_.exe RtlRegistry
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlRegistry

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlRegistryKM
%SystemRoot%\bin\kmtest_.exe RtlRegistryKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlRegistryKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlSplayTree
%SystemRoot%\bin\kmtest_.exe RtlSplayTree
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlSplayTree

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlSplayTreeKM
%SystemRoot%\bin\kmtest_.exe RtlSplayTreeKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlSplayTreeKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlStack
%SystemRoot%\bin\kmtest_.exe RtlStack
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlStack

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlStackKM
%SystemRoot%\bin\kmtest_.exe RtlStackKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlStackKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlStrSafe
%SystemRoot%\bin\kmtest_.exe RtlStrSafe
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlStrSafe

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlStrSafeKM
%SystemRoot%\bin\kmtest_.exe RtlStrSafeKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlStrSafeKM

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlUnicodeString
%SystemRoot%\bin\kmtest_.exe RtlUnicodeString
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlUnicodeString

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN RtlUnicodeStringKM
%SystemRoot%\bin\kmtest_.exe RtlUnicodeStringKM
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END RtlUnicodeStringKM

rem --- TcpIp tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN TcpIpTdi
%SystemRoot%\bin\kmtest_.exe TcpIpTdi
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END TcpIpTdi

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN TcpIpConnect
%SystemRoot%\bin\kmtest_.exe TcpIpConnect
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END TcpIpConnect

rem --- Cache Manager tests ---
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN CcCopyRead
%SystemRoot%\bin\kmtest_.exe CcCopyRead
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END CcCopyRead

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN CcCopyWrite
%SystemRoot%\bin\kmtest_.exe CcCopyWrite
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END CcCopyWrite

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN CcMapData
%SystemRoot%\bin\kmtest_.exe CcMapData
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END CcMapData

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN CcPinMappedData
%SystemRoot%\bin\kmtest_.exe CcPinMappedData
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END CcPinMappedData

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN CcPinRead
%SystemRoot%\bin\kmtest_.exe CcPinRead
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END CcPinRead

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: BEGIN CcSetFileSizes
%SystemRoot%\bin\kmtest_.exe CcSetFileSizes
%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: END CcSetFileSizes

%SystemRoot%\system32\dbgprint.exe KMTEST-KEEXOBPS: COMPLETE
