/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     Kernel-Mode Test Suite kernel-mode test list
 */

#include <kmt_test.h>

KMT_TESTFUNC Test_CmSecurity;
KMT_TESTFUNC Test_Example;
KMT_TESTFUNC Test_ExCallback;
KMT_TESTFUNC Test_ExDoubleList;
KMT_TESTFUNC Test_ExFastMutex;
KMT_TESTFUNC Test_ExHardError;
KMT_TESTFUNC Test_ExHardErrorInteractive;
KMT_TESTFUNC Test_ExInterlocked;
KMT_TESTFUNC Test_ExPools;
KMT_TESTFUNC Test_ExCallbackExtra;
KMT_TESTFUNC Test_ExFastMutexExtra;
KMT_TESTFUNC Test_ExLookaside;
KMT_TESTFUNC Test_ExPoolExtra;
KMT_TESTFUNC Test_ExResourceExtra;
KMT_TESTFUNC Test_ExTimerExtra;
KMT_TESTFUNC Test_ExTimeZone;
KMT_TESTFUNC Test_IoCancelKM;
KMT_TESTFUNC Test_PsProcessInfo;
KMT_TESTFUNC Test_RtlStringSupportKM;
#ifdef _M_ARM64
KMT_TESTFUNC Test_Win11NewKM;
KMT_TESTFUNC Test_MmWin11KM;
#endif
KMT_TESTFUNC Test_ExResource;
KMT_TESTFUNC Test_ExRundown;
KMT_TESTFUNC Test_ExSequencedList;
KMT_TESTFUNC Test_ExSingleList;
KMT_TESTFUNC Test_ExTimer;
KMT_TESTFUNC Test_ExUuid;
KMT_TESTFUNC Test_FsRtlDissect;
KMT_TESTFUNC Test_FsRtlExpression;
KMT_TESTFUNC Test_FsRtlLegal;
KMT_TESTFUNC Test_FsRtlMcb;
KMT_TESTFUNC Test_FsRtlRemoveDotsFromPath;
KMT_TESTFUNC Test_FsRtlTunnel;
#if defined(_M_IX86) || defined(_M_AMD64)
KMT_TESTFUNC Test_HalPortIo;
#endif
KMT_TESTFUNC Test_HalSystemInfo;
KMT_TESTFUNC Test_IoCreateFile;
KMT_TESTFUNC Test_IoDeviceInterface;
KMT_TESTFUNC Test_IoEvent;
KMT_TESTFUNC Test_IoFilesystem;
KMT_TESTFUNC Test_IoInterrupt;
KMT_TESTFUNC Test_IoIrp;
KMT_TESTFUNC Test_ExWorkItem;
KMT_TESTFUNC Test_IoMdl;
#if defined(_M_IX86) || defined(_M_AMD64)
KMT_TESTFUNC Test_IoTimerKM;
#endif
KMT_TESTFUNC Test_IoVolume;
KMT_TESTFUNC Test_KdSystemDebugControl;
KMT_TESTFUNC Test_KeApc;
KMT_TESTFUNC Test_KeApcInject;
#ifdef _M_ARM64
KMT_TESTFUNC Test_HalArm64Layout;
KMT_TESTFUNC Test_HalArm64Stage1;
KMT_TESTFUNC Test_HalArm64Stage2;
KMT_TESTFUNC Test_HalArm64Stage3;
KMT_TESTFUNC Test_HalArm64Stage4;
KMT_TESTFUNC Test_HalArm64Stage5;
KMT_TESTFUNC Test_KdArm64Layout;
KMT_TESTFUNC Test_KeArm64;
KMT_TESTFUNC Test_KeArm64Dispatcher;
KMT_TESTFUNC Test_KeArm64DpcIpi;
KMT_TESTFUNC Test_KeArm64Frames;
KMT_TESTFUNC Test_KeArm64Intrinsics;
KMT_TESTFUNC Test_KeArm64IpiBroadcast;
KMT_TESTFUNC Test_KeArm64Irql;
KMT_TESTFUNC Test_KeArm64LoaderCache;
KMT_TESTFUNC Test_KeArm64PcrPrcb;
KMT_TESTFUNC Test_KeArm64Smp;
KMT_TESTFUNC Test_KeArm64SmpChurn;
KMT_TESTFUNC Test_KeArm64SpinLock;
KMT_TESTFUNC Test_KeArm64ThreadProcess;
KMT_TESTFUNC Test_RtlArm64UnwindLayout;
KMT_TESTFUNC Test_KeArm64SubNodeSched;
KMT_TESTFUNC Test_KeArm64Smt;
KMT_TESTFUNC Test_KeArm64Numa;
#endif
KMT_TESTFUNC Test_KeDeviceQueue;
KMT_TESTFUNC Test_KeDpc;
KMT_TESTFUNC Test_KeEvent;
KMT_TESTFUNC Test_KeFloatPointState;
KMT_TESTFUNC Test_KeGuardedMutex;
KMT_TESTFUNC Test_CmKeyKM;
KMT_TESTFUNC Test_EtwRegisterKM;
KMT_TESTFUNC Test_IoBuildIoctlKM;
KMT_TESTFUNC Test_IoCsqKM;
KMT_TESTFUNC Test_IoNullDeviceKM;
KMT_TESTFUNC Test_ZwDuplicateKM;
KMT_TESTFUNC Test_ZwFileKM;
KMT_TESTFUNC Test_KeIpiKM;
KMT_TESTFUNC Test_KeIrql;
KMT_TESTFUNC Test_KeMutex;
KMT_TESTFUNC Test_KeAffinityKM;
KMT_TESTFUNC Test_KeBugCheckCbKM;
KMT_TESTFUNC Test_KePcr;
KMT_TESTFUNC Test_RtlImageKM;
KMT_TESTFUNC Test_RtlRandomKM;
KMT_TESTFUNC Test_ZwSystemInfoKM;
KMT_TESTFUNC Test_KeQueue;
KMT_TESTFUNC Test_KeSemaphore;
KMT_TESTFUNC Test_KeProcessor;
KMT_TESTFUNC Test_KeSpinLock;
KMT_TESTFUNC Test_KeThreadedDpc;
KMT_TESTFUNC Test_KeTime;
KMT_TESTFUNC Test_KeTimer2KM;
KMT_TESTFUNC Test_MmSecureKM;
KMT_TESTFUNC Test_KeTimer;
KMT_TESTFUNC Test_KeWaitMultiple;
KMT_TESTFUNC Test_KernelType;
KMT_TESTFUNC Test_MmAllocateContiguousNode;
KMT_TESTFUNC Test_MmMdl;
KMT_TESTFUNC Test_MmSection;
KMT_TESTFUNC Test_MmReservedMapping;
KMT_TESTFUNC Test_MmSelfMap;
KMT_TESTFUNC Test_NpfsConnect;
KMT_TESTFUNC Test_NpfsCreate;
KMT_TESTFUNC Test_NpfsFileInfo;
KMT_TESTFUNC Test_NpfsReadWrite;
KMT_TESTFUNC Test_NpfsVolumeInfo;
KMT_TESTFUNC Test_ObHandle;
KMT_TESTFUNC Test_ObQuery;
KMT_TESTFUNC Test_ObReference;
KMT_TESTFUNC Test_ObSecurity;
KMT_TESTFUNC Test_ObSymbolicLink;
KMT_TESTFUNC Test_ObType;
KMT_TESTFUNC Test_ObTypeClean;
KMT_TESTFUNC Test_ObTypeNoClean;
KMT_TESTFUNC Test_ObTypes;
KMT_TESTFUNC Test_PsNotify;
KMT_TESTFUNC Test_PsQuota;
KMT_TESTFUNC Test_PsSystemThread;
KMT_TESTFUNC Test_SeAccessCheckKM;
KMT_TESTFUNC Test_SeInheritance;
KMT_TESTFUNC Test_SeLogonSession;
KMT_TESTFUNC Test_SeQueryInfoToken;
KMT_TESTFUNC Test_SeTokenFiltering;
KMT_TESTFUNC Test_RtlAvlTree;
KMT_TESTFUNC Test_RtlCaptureContext;
KMT_TESTFUNC Test_RtlException;
KMT_TESTFUNC Test_RtlGetVersion;
KMT_TESTFUNC Test_RtlIntSafe;
KMT_TESTFUNC Test_RtlIsValidOemCharacter;
KMT_TESTFUNC Test_MmMapReserve;
KMT_TESTFUNC Test_MmPhysical;
KMT_TESTFUNC Test_MmPrefetchPages;
KMT_TESTFUNC Test_IoStackKM;
KMT_TESTFUNC Test_KeCriticalRegionKM;
KMT_TESTFUNC Test_ObOpenByPointer;
KMT_TESTFUNC Test_ObSecurityDescKM;
KMT_TESTFUNC Test_PsNotifyKM;
KMT_TESTFUNC Test_RtlBitmapKM;
KMT_TESTFUNC Test_RtlGuidKM;
KMT_TESTFUNC Test_RtlHashTableKM;
KMT_TESTFUNC Test_RtlTimeKM;
KMT_TESTFUNC Test_RtlGenericTableKM;
KMT_TESTFUNC Test_RtlMemory;
KMT_TESTFUNC Test_RtlRangeList;
KMT_TESTFUNC Test_RtlRegistry;
KMT_TESTFUNC Test_RtlSplayTree;
KMT_TESTFUNC Test_RtlStack;
KMT_TESTFUNC Test_RtlStrSafe;
KMT_TESTFUNC Test_RtlUnicodeString;
KMT_TESTFUNC Test_ZwAllocateVirtualMemory;
KMT_TESTFUNC Test_ZwCreateSection;
KMT_TESTFUNC Test_ZwMapViewOfSection;
KMT_TESTFUNC Test_ZwWaitForMultipleObjects;

const KMT_TEST TestList[] =
{
    { "CmSecurity",                         Test_CmSecurity },
    { "ExCallback",                         Test_ExCallback },
    { "ExDoubleList",                       Test_ExDoubleList },
    { "ExFastMutex",                        Test_ExFastMutex },
    { "ExHardError",                        Test_ExHardError },
    { "ExHardErrorInteractive",             Test_ExHardErrorInteractive },
    { "ExInterlocked",                      Test_ExInterlocked },
    { "ExPools",                            Test_ExPools },
    { "ExLookaside",                        Test_ExLookaside },
    { "ExCallbackExtra",                    Test_ExCallbackExtra },
    { "ExFastMutexExtra",                   Test_ExFastMutexExtra },
    { "ExPoolExtra",                        Test_ExPoolExtra },
    { "ExResourceExtra",                    Test_ExResourceExtra },
    { "ExTimerExtra",                       Test_ExTimerExtra },
    { "ExTimeZone",                         Test_ExTimeZone },
    { "ExResource",                         Test_ExResource },
    { "ExRundown",                          Test_ExRundown },
    { "ExSequencedList",                    Test_ExSequencedList },
    { "ExSingleList",                       Test_ExSingleList },
    { "ExTimer",                            Test_ExTimer },
    { "ExUuid",                             Test_ExUuid },
    { "Example",                            Test_Example },
    { "FsRtlDissect",                       Test_FsRtlDissect },
    { "FsRtlExpression",                    Test_FsRtlExpression },
    { "FsRtlLegal",                         Test_FsRtlLegal },
    { "FsRtlMcb",                           Test_FsRtlMcb },
    { "FsRtlRemoveDotsFromPath",            Test_FsRtlRemoveDotsFromPath },
    { "FsRtlTunnel",                        Test_FsRtlTunnel },
#if defined(_M_IX86) || defined(_M_AMD64)
#if defined(_M_IX86) || defined(_M_AMD64)
    { "HalPortIo",                          Test_HalPortIo },
#endif
#endif
    { "HalSystemInfo",                      Test_HalSystemInfo },
    { "IoCreateFile",                       Test_IoCreateFile },
    { "IoDeviceInterface",                  Test_IoDeviceInterface },
    { "IoEvent",                            Test_IoEvent },
    { "IoFilesystem",                       Test_IoFilesystem },
    { "IoInterrupt",                        Test_IoInterrupt },
    { "IoIrp",                              Test_IoIrp },
    { "ExWorkItem",                         Test_ExWorkItem },
    { "IoCancelKM",                         Test_IoCancelKM },
    { "CmKeyKM",                            Test_CmKeyKM },
    { "EtwRegisterKM",                      Test_EtwRegisterKM },
    { "IoBuildIoctlKM",                     Test_IoBuildIoctlKM },
    { "IoCsqKM",                            Test_IoCsqKM },
    { "IoNullDeviceKM",                     Test_IoNullDeviceKM },
    { "IoMdl",                              Test_IoMdl },
    { "IoVolume",                           Test_IoVolume },
    { "KdSystemDebugControl",               Test_KdSystemDebugControl },
    { "KeApc",                              Test_KeApc },
    { "KeApcInject",                        Test_KeApcInject },
#ifdef _M_ARM64
    { "HalArm64Layout",                     Test_HalArm64Layout },
    { "HalArm64Stage1",                     Test_HalArm64Stage1 },
    { "HalArm64Stage2",                     Test_HalArm64Stage2 },
    { "HalArm64Stage3",                     Test_HalArm64Stage3 },
    { "HalArm64Stage4",                     Test_HalArm64Stage4 },
    { "HalArm64Stage5",                     Test_HalArm64Stage5 },
    { "KdArm64Layout",                      Test_KdArm64Layout },
    { "KeArm64",                            Test_KeArm64 },
    { "KeArm64Dispatcher",                  Test_KeArm64Dispatcher },
    { "KeArm64DpcIpi",                      Test_KeArm64DpcIpi },
    { "KeArm64Frames",                      Test_KeArm64Frames },
    { "KeArm64Intrinsics",                  Test_KeArm64Intrinsics },
    { "KeArm64Irql",                        Test_KeArm64Irql },
    { "KeArm64LoaderCache",                 Test_KeArm64LoaderCache },
    { "KeArm64PcrPrcb",                     Test_KeArm64PcrPrcb },
    { "KeArm64IpiBroadcast",                Test_KeArm64IpiBroadcast },
    { "KeArm64Smp",                         Test_KeArm64Smp },
    { "KeArm64SmpChurn",                    Test_KeArm64SmpChurn },
    { "KeArm64SpinLock",                    Test_KeArm64SpinLock },
    { "KeArm64ThreadProcess",               Test_KeArm64ThreadProcess },
    { "RtlArm64UnwindLayout",               Test_RtlArm64UnwindLayout },
    { "KeArm64SubNodeSched",                Test_KeArm64SubNodeSched },
    { "KeArm64Smt",                         Test_KeArm64Smt },
    { "KeArm64Numa",                        Test_KeArm64Numa },
#endif
#if defined(_M_IX86) || defined(_M_AMD64)
    { "IoStackKM",                          Test_IoStackKM },
    { "IoTimerKM",                          Test_IoTimerKM },
#endif
    { "KeAffinityKM",                       Test_KeAffinityKM },
    { "KeBugCheckCbKM",                     Test_KeBugCheckCbKM },
    { "KeCriticalRegionKM",                 Test_KeCriticalRegionKM },
    { "KeDeviceQueue",                      Test_KeDeviceQueue },
    { "KeDpc",                              Test_KeDpc },
    { "KeEvent",                            Test_KeEvent },
    { "KeFloatPointState",                  Test_KeFloatPointState },
    { "KeGuardedMutex",                     Test_KeGuardedMutex },
    { "KeIrql",                             Test_KeIrql },
    { "KeIpiKM",                            Test_KeIpiKM },
    { "KeMutex",                            Test_KeMutex },
    { "KePcr",                              Test_KePcr },
    { "KeQueue",                            Test_KeQueue },
    { "KeProcessor",                        Test_KeProcessor },
    { "KeSemaphore",                        Test_KeSemaphore },
    { "KeSpinLock",                         Test_KeSpinLock },
    { "KeThreadedDpc",                      Test_KeThreadedDpc },
    { "KeTime",                             Test_KeTime },
    { "KeTimer",                            Test_KeTimer },
    { "KeTimer2KM",                         Test_KeTimer2KM },
    { "KeWaitMultiple",                     Test_KeWaitMultiple },
    { "KernelType",                         Test_KernelType },
    { "MmAllocateContiguousNode",           Test_MmAllocateContiguousNode },
    { "MmMdl",                              Test_MmMdl },
    { "MmSecureKM",                         Test_MmSecureKM },
    { "MmSection",                          Test_MmSection },
    { "MmMapReserve",                       Test_MmMapReserve },
    { "MmPhysical",                         Test_MmPhysical },
    { "MmPrefetchPages",                    Test_MmPrefetchPages },
    { "MmReservedMapping",                  Test_MmReservedMapping },
    { "MmSelfMap",                          Test_MmSelfMap },
    { "NpfsConnect",                        Test_NpfsConnect },
    { "NpfsCreate",                         Test_NpfsCreate },
    { "NpfsFileInfo",                       Test_NpfsFileInfo },
    { "NpfsReadWrite",                      Test_NpfsReadWrite },
    { "NpfsVolumeInfo",                     Test_NpfsVolumeInfo },
    { "ObHandle",                           Test_ObHandle },
    { "ObOpenByPointer",                    Test_ObOpenByPointer },
    { "ObQuery",                            Test_ObQuery },
    { "ObReference",                        Test_ObReference },
    { "ObSecurity",                         Test_ObSecurity },
    { "ObSecurityDescKM",                   Test_ObSecurityDescKM },
    { "ObSymbolicLink",                     Test_ObSymbolicLink },
    { "ObType",                             Test_ObType },
    { "ObTypeClean",                        Test_ObTypeClean },
    { "ObTypeNoClean",                      Test_ObTypeNoClean },
    { "ObTypes",                            Test_ObTypes },
    { "PsNotify",                           Test_PsNotify },
    { "PsQuota",                            Test_PsQuota },
    { "SeAccessCheckKM",                    Test_SeAccessCheckKM },
    { "RtlAvlTreeKM",                       Test_RtlAvlTree },
    { "RtlExceptionKM",                     Test_RtlException },
    { "RtlGetVersion",                      Test_RtlGetVersion },
    { "RtlIntSafeKM",                       Test_RtlIntSafe },
    { "RtlIsValidOemCharacter",             Test_RtlIsValidOemCharacter },
    { "PsProcessInfo",                      Test_PsProcessInfo },
    { "PsNotifyKM",                         Test_PsNotifyKM },
    { "PsSystemThread",                     Test_PsSystemThread },
    { "RtlBitmapKM",                        Test_RtlBitmapKM },
    { "RtlGenericTableKM",                  Test_RtlGenericTableKM },
    { "RtlGuidKM",                          Test_RtlGuidKM },
    { "RtlImageKM",                         Test_RtlImageKM },
    { "RtlRandomKM",                        Test_RtlRandomKM },
    { "ZwDuplicateKM",                      Test_ZwDuplicateKM },
    { "ZwFileKM",                           Test_ZwFileKM },
    { "ZwSystemInfoKM",                     Test_ZwSystemInfoKM },
    { "RtlHashTableKM",                     Test_RtlHashTableKM },
    { "RtlMemoryKM",                        Test_RtlMemory },
    { "RtlRangeList",                       Test_RtlRangeList },
    { "RtlRegistryKM",                      Test_RtlRegistry },
    { "RtlTimeKM",                          Test_RtlTimeKM },
    { "RtlStringSupportKM",                 Test_RtlStringSupportKM },
#ifdef _M_ARM64
    { "Win11NewKM",                         Test_Win11NewKM },
    { "MmWin11KM",                          Test_MmWin11KM },
#endif
    { "RtlSplayTreeKM",                     Test_RtlSplayTree },
    { "RtlStackKM",                         Test_RtlStack },
    { "RtlStrSafeKM",                       Test_RtlStrSafe },
    { "RtlUnicodeStringKM",                 Test_RtlUnicodeString },
    { "SeInheritance",                      Test_SeInheritance },
    { "SeLogonSession",                     Test_SeLogonSession },
    { "SeQueryInfoToken",                   Test_SeQueryInfoToken },
    { "SeTokenFiltering",                   Test_SeTokenFiltering },
    { "ZwAllocateVirtualMemory",            Test_ZwAllocateVirtualMemory },
    { "ZwCreateSection",                    Test_ZwCreateSection },
    { "ZwMapViewOfSection",                 Test_ZwMapViewOfSection },
    { "ZwWaitForMultipleObjects",           Test_ZwWaitForMultipleObjects},
#ifdef _M_AMD64
    { "RtlCaptureContextKM",                Test_RtlCaptureContext },
#endif
    { NULL,                                 NULL }
};
