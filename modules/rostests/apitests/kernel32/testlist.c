
#define STANDALONE
#include <apitest.h>

extern void func_ActCtxWithXmlNamespaces(void);
extern void func_ApiSetCoreSynch(void);
extern void func_Arm64ThreadContext(void);
extern void func_ConsoleCP(void);
extern void func_ConsoleProcessInheritance(void);
extern void func_ConsoleVirtualTerminal(void);
extern void func_CreateProcess(void);
extern void func_DefaultActCtx(void);
extern void func_DeviceIoControl(void);
extern void func_dosdev(void);
extern void func_EnumSystemCodePages(void);
extern void func_FindActCtxSectionStringW(void);
extern void func_FindFiles(void);
extern void func_FindPackagesByPackageFamily(void);
extern void func_FLS(void);
extern void func_FormatMessage(void);
extern void func_GetComputerNameEx(void);
extern void func_GetCPInfo(void);
extern void func_GetCurrentDirectory(void);
extern void func_GetCurrentThreadStackLimits(void);
extern void func_GetDriveType(void);
extern void func_GetEnvironmentVariable(void);
extern void func_GetFinalPathNameByHandle(void);
extern void func_GetLocaleInfo(void);
extern void func_GetModuleFileName(void);
extern void func_GetNumaNodeProcessorMaskEx(void);
extern void func_GetOsSafeBootMode(void);
extern void func_GetPackageFamilyName(void);
extern void func_GetSystemWow64Directory(void);
extern void func_GetTempFileName(void);
extern void func_GetVolumeInformation(void);
extern void func_InitOnce(void);
extern void func_interlck(void);
extern void func_IoCompletion(void);
extern void func_IsDBCSLeadByteEx(void);
extern void func_JapaneseCalendar(void);
extern void func_JobObject(void);
extern void func_JobObjectUI(void);
extern void func_LCMapString(void);
extern void func_LoadLibraryExW(void);
extern void func_LockFileEx(void);
extern void func_SandboxLaunch(void);
extern void func_SystemImageCatalog(void);
extern void func_SharedMemorySecurity(void);
extern void func_SharedMemoryTransfer(void);
extern void func_StdHandleInheritance(void);
extern void func_LocaleNameToLCID(void);
extern void func_lstrcpynW(void);
extern void func_lstrlen(void);
extern void func_MailslotRead(void);
extern void func_MultiByteToWideChar(void);
extern void func_Pipes(void);
extern void func_PixeloramaCompat(void);
extern void func_ProcessPreferredUILanguages(void);
extern void func_PrivMoveFileIdentityW(void);
extern void func_QueueUserAPC(void);
extern void func_QueryProcessCycleTime(void);
extern void func_ResizePseudoConsole(void);
extern void func_SetComputerNameExW(void);
extern void func_SetConsoleWindowInfo(void);
extern void func_SetCurrentDirectory(void);
extern void func_SetFileAllocationInfo(void);
extern void func_SetThreadStackGuarantee(void);
extern void func_SetUnhandledExceptionFilter(void);
extern void func_SetWaitableTimerEx(void);
extern void func_SuspendThread(void);
extern void func_SystemFirmware(void);
extern void func_TerminateProcess(void);
extern void func_ThreadPowerThrottling(void);
extern void func_ProcessPowerThrottling(void);
extern void func_TunnelCache(void);
extern void func_UEFIFirmware(void);
extern void func_WerRegisterFile(void);
extern void func_WideCharToMultiByte(void);
extern void func_Wow64GetThreadContext(void);
extern void func_WriteFileSeekBack(void);

const struct test winetest_testlist[] =
{
    { "SetFileAllocationInfo", func_SetFileAllocationInfo },
    { "ActCtxWithXmlNamespaces",     func_ActCtxWithXmlNamespaces },
    { "ApiSetCoreSynch",             func_ApiSetCoreSynch },
    { "Arm64ThreadContext",          func_Arm64ThreadContext },
    { "ConsoleCP",                   func_ConsoleCP },
    { "ConsoleProcessInheritance",   func_ConsoleProcessInheritance },
    { "ConsoleVirtualTerminal",      func_ConsoleVirtualTerminal },
    { "CreateProcess",               func_CreateProcess },
    { "DefaultActCtx",               func_DefaultActCtx },
    { "DeviceIoControl",             func_DeviceIoControl },
    { "dosdev",                      func_dosdev },
    { "EnumSystemCodePages",         func_EnumSystemCodePages },
    { "FindActCtxSectionStringW",    func_FindActCtxSectionStringW },
    { "FindFiles",                   func_FindFiles },
    { "FindPackagesByPackageFamily", func_FindPackagesByPackageFamily },
    { "FLS",                         func_FLS },
    { "FormatMessage",               func_FormatMessage },
    { "GetComputerNameEx",           func_GetComputerNameEx },
    { "GetCPInfo",                   func_GetCPInfo },
    { "GetCurrentDirectory",         func_GetCurrentDirectory },
    { "GetCurrentThreadStackLimits", func_GetCurrentThreadStackLimits },
    { "GetDriveType",                func_GetDriveType },
    { "GetEnvironmentVariable",      func_GetEnvironmentVariable },
    { "GetFinalPathNameByHandle",    func_GetFinalPathNameByHandle },
    { "GetLocaleInfo",               func_GetLocaleInfo },
    { "GetModuleFileName",           func_GetModuleFileName },
    { "GetNumaNodeProcessorMaskEx",  func_GetNumaNodeProcessorMaskEx },
    { "GetOsSafeBootMode",           func_GetOsSafeBootMode },
    { "GetPackageFamilyName",        func_GetPackageFamilyName },
    { "GetSystemWow64Directory",     func_GetSystemWow64Directory },
    { "GetTempFileName",             func_GetTempFileName },
    { "GetVolumeInformation",        func_GetVolumeInformation },
    { "InitOnce",                    func_InitOnce },
    { "interlck",                    func_interlck },
    { "IoCompletion",                func_IoCompletion },
    { "IsDBCSLeadByteEx",            func_IsDBCSLeadByteEx },
    { "JapaneseCalendar",            func_JapaneseCalendar },
    { "JobObject",                   func_JobObject },
    { "JobObjectUI",                 func_JobObjectUI },
    { "LCMapString",                 func_LCMapString },
    { "LoadLibraryExW",              func_LoadLibraryExW },
    { "LockFileEx",                  func_LockFileEx },
    { "SystemImageCatalog",            func_SystemImageCatalog },
    { "SandboxLaunch",               func_SandboxLaunch },
    { "SharedMemorySecurity",        func_SharedMemorySecurity },
    { "SharedMemoryTransfer",        func_SharedMemoryTransfer },
    { "StdHandleInheritance",        func_StdHandleInheritance },
    { "LocaleNameToLCID",            func_LocaleNameToLCID },
    { "lstrcpynW",                   func_lstrcpynW },
    { "lstrlen",                     func_lstrlen },
    { "MailslotRead",                func_MailslotRead },
    { "MultiByteToWideChar",         func_MultiByteToWideChar },
    { "Pipes",                       func_Pipes },
    { "PixeloramaCompat",            func_PixeloramaCompat },
    { "ProcessPreferredUILanguages", func_ProcessPreferredUILanguages },
    { "PrivMoveFileIdentityW",       func_PrivMoveFileIdentityW },
    { "QueueUserAPC",                func_QueueUserAPC },
    { "QueryProcessCycleTime",       func_QueryProcessCycleTime },
    { "ResizePseudoConsole",         func_ResizePseudoConsole },
    { "SetComputerNameExW",          func_SetComputerNameExW },
    { "SetConsoleWindowInfo",        func_SetConsoleWindowInfo },
    { "SetCurrentDirectory",         func_SetCurrentDirectory },
    { "SetThreadStackGuarantee",     func_SetThreadStackGuarantee },
    { "SetUnhandledExceptionFilter", func_SetUnhandledExceptionFilter },
    { "SetWaitableTimerEx",          func_SetWaitableTimerEx },
    { "SuspendThread",               func_SuspendThread },
    { "SystemFirmware",              func_SystemFirmware },
    { "TerminateProcess",            func_TerminateProcess },
    { "ThreadPowerThrottling",       func_ThreadPowerThrottling },
    { "ProcessPowerThrottling",      func_ProcessPowerThrottling },
    { "TunnelCache",                 func_TunnelCache },
    { "UEFIFirmware",                func_UEFIFirmware },
    { "WerRegisterFile",             func_WerRegisterFile },
    { "WideCharToMultiByte",         func_WideCharToMultiByte },
    { "Wow64GetThreadContext",       func_Wow64GetThreadContext },
    { "WriteFileSeekBack",           func_WriteFileSeekBack },
    { 0, 0 }
};
