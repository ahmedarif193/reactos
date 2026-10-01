/*
 * PROJECT:     LiberNT Automatic Testing Utility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test categories selected with the /g option
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

struct CATEGORY_RULE
{
    const char* Program;
    const char* Test;
    const char* Category;
};

static const char UnsortedCategory[] = "other:unsorted";

static const CATEGORY_RULE Rules[] =
{
    { "ntdll_winetest", "exception", "core:exceptions" },
    { "ntdll_winetest", "unwind", "core:exceptions" },
    { "ntdll_winetest", "wow64", "core:wow64" },
    { "ntdll_winetest", "virtual", "core:memory" },
    { "ntdll_winetest", "sync", "core:threads" },
    { "ntdll_winetest", "thread", "core:threads" },
    { "ntdll_winetest", "threadpool", "core:threads" },
    { "ntdll_winetest", "om", "core:syscalls" },
    { "ntdll_winetest", "file", "core:syscalls" },
    { "ntdll_winetest", "pipe", "core:syscalls" },
    { "ntdll_winetest", "port", "core:syscalls" },
    { "ntdll_winetest", "reg", "core:syscalls" },
    { "ntdll_winetest", "info", "core:syscalls" },
    { "ntdll_winetest", "change", "core:syscalls" },
    { "ntdll_winetest", "directory", "core:syscalls" },
    { "ntdll_winetest", "*", "core:rtl" },
    { "ntdll_apitest", "RtlUnwind", "core:exceptions" },
    { "ntdll_apitest", "RtlCaptureContext", "core:exceptions" },
    { "ntdll_apitest", "RtlVirtualUnwind", "core:exceptions" },
    { "ntdll_apitest", "RtlVirtualUnwindChainedHandler", "core:exceptions" },
    { "ntdll_apitest", "UserModeException", "core:exceptions" },
    { "ntdll_apitest", "RtlUnhandledExceptionFilter", "core:exceptions" },
    { "ntdll_apitest", "StackOverflow", "core:exceptions" },
    { "ntdll_apitest", "NtContinue", "core:exceptions" },
    { "ntdll_apitest", "setjmp", "core:exceptions" },
    { "ntdll_apitest", "DebugRegisters", "core:exceptions" },
    { "ntdll_apitest", "wow64_startup", "core:wow64" },
    { "ntdll_apitest", "wow64_native_process", "core:wow64" },
    { "ntdll_apitest", "arm64_chpe", "core:wow64" },
    { "ntdll_apitest", "Ldr*", "core:loader" },
    { "ntdll_apitest", "DllLoadNotification", "core:loader" },
    { "ntdll_apitest", "load_notifications", "core:loader" },
    { "ntdll_apitest", "RtlImage*", "core:loader" },
    { "ntdll_apitest", "RtlComputePrivatizedDllName_U", "core:loader" },
    { "ntdll_apitest", "RtlDosApplyFileIsolationRedirection_Ustr", "core:loader" },
    { "ntdll_apitest", "RtlGetUnloadEventTrace", "core:loader" },
    { "ntdll_apitest", "NtAllocateVirtualMemory", "core:memory" },
    { "ntdll_apitest", "NtFreeVirtualMemory", "core:memory" },
    { "ntdll_apitest", "NtProtectVirtualMemory", "core:memory" },
    { "ntdll_apitest", "NtMapViewOfSection*", "core:memory" },
    { "ntdll_apitest", "NtCreateSection", "core:memory" },
    { "ntdll_apitest", "NtQuerySection", "core:memory" },
    { "ntdll_apitest", "MemoryAccounting", "core:memory" },
    { "ntdll_apitest", "RtlAllocateHeap", "core:memory" },
    { "ntdll_apitest", "RtlReAllocateHeap", "core:memory" },
    { "ntdll_apitest", "RtlMultipleAllocateHeap", "core:memory" },
    { "ntdll_apitest", "RtlHeapFreeCoalesce", "core:memory" },
    { "ntdll_apitest", "RtlGetProcessHeaps", "core:memory" },
    { "ntdll_apitest", "RtlCopyMappedMemory", "core:memory" },
    { "ntdll_apitest", "RtlCriticalSection", "core:threads" },
    { "ntdll_apitest", "RtlSRWLock", "core:threads" },
    { "ntdll_apitest", "RtlConditionVariable", "core:threads" },
    { "ntdll_apitest", "RtlWaitOnAddress*", "core:threads" },
    { "ntdll_apitest", "NtCreateThread*", "core:threads" },
    { "ntdll_apitest", "NtMutant", "core:threads" },
    { "ntdll_apitest", "TimerResolution", "core:threads" },
    { "ntdll_apitest", "_*", "core:crt" },
    { "ntdll_apitest", "mbstowcs", "core:crt" },
    { "ntdll_apitest", "sprintf", "core:crt" },
    { "ntdll_apitest", "strcpy", "core:crt" },
    { "ntdll_apitest", "strlen", "core:crt" },
    { "ntdll_apitest", "strtoul", "core:crt" },
    { "ntdll_apitest", "wcstoul", "core:crt" },
    { "ntdll_apitest", "wcstombs", "core:crt" },
    { "ntdll_apitest", "memmove", "core:crt" },
    { "ntdll_apitest", "Nt*", "core:syscalls" },
    { "ntdll_apitest", "SyscallStub", "core:syscalls" },
    { "ntdll_apitest", "*", "core:rtl" },
    { "kernel32_winetest", "loader", "core:loader" },
    { "kernel32_winetest", "module", "core:loader" },
    { "kernel32_winetest", "actctx", "core:loader" },
    { "kernel32_winetest", "resource", "core:loader" },
    { "kernel32_winetest", "debugger", "core:debug" },
    { "kernel32_winetest", "heap", "core:memory" },
    { "kernel32_winetest", "virtual", "core:memory" },
    { "kernel32_winetest", "sync", "core:threads" },
    { "kernel32_winetest", "thread", "core:threads" },
    { "kernel32_winetest", "fiber", "core:threads" },
    { "kernel32_winetest", "timer", "core:threads" },
    { "kernel32_winetest", "process", "core:process" },
    { "kernel32_winetest", "toolhelp", "core:process" },
    { "kernel32_winetest", "environ", "core:process" },
    { "kernel32_winetest", "version", "core:process" },
    { "kernel32_winetest", "profile", "core:process" },
    { "kernel32_winetest", "power", "core:process" },
    { "kernel32_winetest", "file", "core:files" },
    { "kernel32_winetest", "directory", "core:files" },
    { "kernel32_winetest", "drive", "core:files" },
    { "kernel32_winetest", "volume", "core:files" },
    { "kernel32_winetest", "path", "core:files" },
    { "kernel32_winetest", "change", "core:files" },
    { "kernel32_winetest", "mailslot", "core:files" },
    { "kernel32_winetest", "pipe", "core:files" },
    { "kernel32_winetest", "comm", "core:files" },
    { "kernel32_winetest", "console", "core:console" },
    { "kernel32_winetest", "locale", "core:locale" },
    { "kernel32_winetest", "codepage", "core:locale" },
    { "kernel32_winetest", "format_msg", "core:locale" },
    { "kernel32_winetest", "time", "core:locale" },
    { "kernel32_winetest", "*", "core:rtl" },
    { "kernel32_apitest", "SetUnhandledExceptionFilter", "core:exceptions" },
    { "kernel32_apitest", "SetThreadStackGuarantee", "core:exceptions" },
    { "kernel32_apitest", "GetCurrentThreadStackLimits", "core:exceptions" },
    { "kernel32_apitest", "WerRegisterFile", "core:exceptions" },
    { "kernel32_apitest", "Wow64GetThreadContext", "core:wow64" },
    { "kernel32_apitest", "GetSystemWow64Directory", "core:wow64" },
    { "kernel32_apitest", "Arm64ThreadContext", "core:wow64" },
    { "kernel32_apitest", "XState", "core:wow64" },
    { "kernel32_apitest", "LoadLibraryExW", "core:loader" },
    { "kernel32_apitest", "DefaultActCtx", "core:loader" },
    { "kernel32_apitest", "ActCtxWithXmlNamespaces", "core:loader" },
    { "kernel32_apitest", "FindActCtxSectionStringW", "core:loader" },
    { "kernel32_apitest", "GetModuleFileName", "core:loader" },
    { "kernel32_apitest", "ApiSetCoreSynch", "core:loader" },
    { "kernel32_apitest", "SystemImageCatalog", "core:loader" },
    { "kernel32_apitest", "SharedMemory*", "core:memory" },
    { "kernel32_apitest", "InitOnce", "core:threads" },
    { "kernel32_apitest", "FLS", "core:threads" },
    { "kernel32_apitest", "QueueUserAPC", "core:threads" },
    { "kernel32_apitest", "SuspendThread", "core:threads" },
    { "kernel32_apitest", "SetWaitableTimerEx", "core:threads" },
    { "kernel32_apitest", "interlck", "core:threads" },
    { "kernel32_apitest", "IoCompletion", "core:threads" },
    { "kernel32_apitest", "ThreadPowerThrottling", "core:threads" },
    { "kernel32_apitest", "ConsoleCP", "core:console" },
    { "kernel32_apitest", "ConsoleProcessInheritance", "core:console" },
    { "kernel32_apitest", "ConsoleVirtualTerminal", "core:console" },
    { "kernel32_apitest", "ResizePseudoConsole", "core:console" },
    { "kernel32_apitest", "SetConsoleWindowInfo", "core:console" },
    { "kernel32_apitest", "CreateProcess", "core:process" },
    { "kernel32_apitest", "JobObject*", "core:process" },
    { "kernel32_apitest", "TerminateProcess", "core:process" },
    { "kernel32_apitest", "QueryProcessCycleTime", "core:process" },
    { "kernel32_apitest", "ProcessPowerThrottling", "core:process" },
    { "kernel32_apitest", "StdHandleInheritance", "core:process" },
    { "kernel32_apitest", "GetEnvironmentVariable", "core:process" },
    { "kernel32_apitest", "SandboxLaunch", "core:process" },
    { "kernel32_apitest", "GetNumaNodeProcessorMaskEx", "core:process" },
    { "kernel32_apitest", "FindPackagesByPackageFamily", "core:process" },
    { "kernel32_apitest", "GetPackageFamilyName", "core:process" },
    { "kernel32_apitest", "PixeloramaCompat", "core:process" },
    { "kernel32_apitest", "GetOsSafeBootMode", "core:process" },
    { "kernel32_apitest", "SystemFirmware", "core:process" },
    { "kernel32_apitest", "UEFIFirmware", "core:process" },
    { "kernel32_apitest", "GetComputerNameEx", "core:process" },
    { "kernel32_apitest", "SetComputerNameExW", "core:process" },
    { "kernel32_apitest", "EnumSystemCodePages", "core:locale" },
    { "kernel32_apitest", "FormatMessage", "core:locale" },
    { "kernel32_apitest", "GetCPInfo", "core:locale" },
    { "kernel32_apitest", "GetLocaleInfo", "core:locale" },
    { "kernel32_apitest", "IsDBCSLeadByteEx", "core:locale" },
    { "kernel32_apitest", "JapaneseCalendar", "core:locale" },
    { "kernel32_apitest", "LCMapString", "core:locale" },
    { "kernel32_apitest", "LocaleNameToLCID", "core:locale" },
    { "kernel32_apitest", "MultiByteToWideChar", "core:locale" },
    { "kernel32_apitest", "WideCharToMultiByte", "core:locale" },
    { "kernel32_apitest", "ProcessPreferredUILanguages", "core:locale" },
    { "kernel32_apitest", "lstrcpynW", "core:locale" },
    { "kernel32_apitest", "lstrlen", "core:locale" },
    { "kernel32_apitest", "*", "core:files" },
    { "msvcrt_winetest", "cpp", "core:exceptions" },
    { "msvcrt_winetest", "*", "core:crt" },
    { "msvcrt_apitest", "setjmp", "core:exceptions" },
    { "msvcrt_apitest", "*", "core:crt" },
    { "compiler_apitest", "ms-seh", "core:exceptions" },
    { "compiler_apitest", "pseh", "core:exceptions" },
    { "compiler_apitest", "pseh_cpp", "core:exceptions" },
    { "compiler_apitest", "*", "core:crt" },
    { "kmtest", "Dxgk*", "graphics:wddm" },
    { "kmtest", "Dxgmms2*", "graphics:wddm" },
    { "kmtest", "SoftGpu*", "graphics:wddm" },
    { "kmtest", "CddDisplay", "graphics:wddm" },
    { "kmtest", "Rpi5Vc4*", "graphics:wddm" },
    { "kmtest", "ExWddm*", "graphics:wddm" },
    { "kmtest", "MmWddm*", "graphics:wddm" },
    { "kmtest", "PsWddm*", "graphics:wddm" },
    { "kmtest", "KeWddm*", "graphics:wddm" },
    { "kmtest", "InbvVirtualFrameBuffer", "graphics:wddm" },
    { "kmtest", "KeArm64*", "kernel:arch" },
    { "kmtest", "HalArm64*", "kernel:arch" },
    { "kmtest", "KdArm64*", "kernel:arch" },
    { "kmtest", "RtlArm64*", "kernel:arch" },
    { "kmtest", "Mm*", "kernel:mm" },
    { "kmtest", "ZwAllocateVirtualMemory", "kernel:mm" },
    { "kmtest", "ZwCreateSection", "kernel:mm" },
    { "kmtest", "ZwMapViewOfSection", "kernel:mm" },
    { "kmtest", "NtCreateSection", "kernel:mm" },
    { "kmtest", "NtUserPhysicalPages", "kernel:mm" },
    { "kmtest", "Example", "kernel:misc" },
    { "kmtest", "Win11NewKM", "kernel:misc" },
    { "kmtest", "KernelType", "kernel:misc" },
    { "kmtest", "Cc*", "kernel:cc" },
    { "kmtest", "Ke*", "kernel:ke" },
    { "kmtest", "Ex*", "kernel:ex" },
    { "kmtest", "EtwRegister*", "kernel:ex" },
    { "kmtest", "Hal*", "kernel:hal" },
    { "kmtest", "FsRtl*", "kernel:fs" },
    { "kmtest", "Npfs*", "kernel:fs" },
    { "kmtest", "FltMgr*", "kernel:fs" },
    { "kmtest", "FileAttributes", "kernel:fs" },
    { "kmtest", "FindFile", "kernel:fs" },
    { "kmtest", "Io*", "kernel:io" },
    { "kmtest", "HidPDescription", "kernel:io" },
    { "kmtest", "Ob*", "kernel:ob" },
    { "kmtest", "Ps*", "kernel:ps" },
    { "kmtest", "NtProcessClone", "kernel:ps" },
    { "kmtest", "Se*", "kernel:se" },
    { "kmtest", "KsecBcrypt", "kernel:se" },
    { "kmtest", "Cm*", "kernel:cm" },
    { "kmtest", "Po*", "kernel:po" },
    { "kmtest", "Kd*", "kernel:kd" },
    { "kmtest", "NtSystemDebugControl", "kernel:kd" },
    { "kmtest", "Ndis*", "kernel:net" },
    { "kmtest", "TcpIp*", "kernel:net" },
    { "kmtest", "Rtl*", "kernel:rtl" },
    { "kmtest", "Zw*", "kernel:zw" },
    { "kmtest", "*", "kernel:misc" },
    { "msacm32_winetest", "*", "audio:codecs" },
    { "mmdevapi_apitest", "*", "audio:coreaudio" },
    { "mmdevapi_winetest", "*", "audio:coreaudio" },
    { "dmusic_winetest", "*", "audio:dsound" },
    { "dsound_winetest", "*", "audio:dsound" },
    { "mmixer_test", "*", "audio:winmm" },
    { "winmm_winetest", "*", "audio:winmm" },
    { "atl100_winetest", "*", "com:atl" },
    { "atl80_winetest", "*", "com:atl" },
    { "atl_apitest", "*", "com:atl" },
    { "atl_winetest", "*", "com:atl" },
    { "com_apitest", "*", "com:ole" },
    { "combase_winetest", "*", "com:ole" },
    { "comcat_winetest", "*", "com:ole" },
    { "interop_unittest", "*", "com:ole" },
    { "ole32_apitest", "*", "com:ole" },
    { "ole32_winetest", "*", "com:ole" },
    { "oleaut32_winetest", "*", "com:ole" },
    { "oledlg_winetest", "*", "com:ole" },
    { "wine.combase.test", "*", "com:ole" },
    { "rpcrt4_winetest", "*", "com:rpc" },
    { "crtdll_apitest", "*", "core:crt" },
    { "msvcirt_winetest", "*", "core:crt" },
    { "msvcrtd_winetest", "*", "core:crt" },
    { "pathcch_dyn1_unittest", "*", "core:crt" },
    { "pathcch_dyn2_unittest", "*", "core:crt" },
    { "pathcch_static_unittest", "*", "core:crt" },
    { "ucrtbase_apitest", "*", "core:crt" },
    { "ucrtbase_winetest", "*", "core:crt" },
    { "dbgeng_winetest", "*", "core:debug" },
    { "dbghelp_apitest", "*", "core:debug" },
    { "dbghelp_winetest", "*", "core:debug" },
    { "faultrep_winetest", "*", "core:exceptions" },
    { "unwindptrs_apitest", "*", "core:exceptions" },
    { "tunneltest", "*", "core:files" },
    { "apisets_apitest", "*", "core:loader" },
    { "delayimp_globalhook_apitest", "*", "core:loader" },
    { "delayimp_nohook_apitest", "*", "core:loader" },
    { "delayimp_runtimehook_apitest", "*", "core:loader" },
    { "dllexport_test", "*", "core:loader" },
    { "fusion_winetest", "*", "core:loader" },
    { "imagehlp_winetest", "*", "core:loader" },
    { "loadconfig_apitest", "*", "core:loader" },
    { "managedloader_apitest", "*", "core:loader" },
    { "mscoree_winetest", "*", "core:loader" },
    { "pefile_apitest", "*", "core:loader" },
    { "runtimeexports_apitest", "*", "core:loader" },
    { "sdk_apitest", "*", "core:loader" },
    { "sxs_winetest", "*", "core:loader" },
    { "localeparent_apitest", "*", "core:locale" },
    { "mlang_winetest", "*", "core:locale" },
    { "nlsfind_apitest", "*", "core:locale" },
    { "nlssortkey_apitest", "*", "core:locale" },
    { "psapi_apitest", "*", "core:process" },
    { "psapi_winetest", "*", "core:process" },
    { "rtl_unittest", "*", "core:rtl" },
    { "alpc_apitest", "*", "core:syscalls" },
    { "umkm_apitest", "*", "core:syscalls" },
    { "odbc32_winetest", "*", "data:odbc" },
    { "odbccp32_winetest", "*", "data:odbc" },
    { "isapnp_unittest", "*", "devices:other" },
    { "portabledevicetypes_winetest", "*", "devices:other" },
    { "sensorsapi_winetest", "*", "devices:other" },
    { "serialui_winetest", "*", "devices:other" },
    { "twain_32_winetest", "*", "devices:other" },
    { "compstui_winetest", "*", "devices:printing" },
    { "localspl_apitest", "*", "devices:printing" },
    { "localspl_winetest", "*", "devices:printing" },
    { "localui_winetest", "*", "devices:printing" },
    { "spoolss_apitest", "*", "devices:printing" },
    { "spoolss_winetest", "*", "devices:printing" },
    { "winprint_apitest", "*", "devices:printing" },
    { "winspool_apitest", "*", "devices:printing" },
    { "winspool_winetest", "*", "devices:printing" },
    { "d2d1_winetest", "*", "graphics:2d" },
    { "windowscodecs_winetest", "*", "graphics:2d" },
    { "windowscodecsext_winetest", "*", "graphics:2d" },
    { "d3d10_1_winetest", "*", "graphics:direct3d" },
    { "d3d10_winetest", "*", "graphics:direct3d" },
    { "d3d10core_winetest", "*", "graphics:direct3d" },
    { "d3d11_winetest", "*", "graphics:direct3d" },
    { "d3d8_winetest", "*", "graphics:direct3d" },
    { "d3d9_winetest", "*", "graphics:direct3d" },
    { "d3dcompiler_43_winetest", "*", "graphics:direct3d" },
    { "d3dcompiler_47_winetest", "*", "graphics:direct3d" },
    { "d3dkmt_apitest", "*", "graphics:direct3d" },
    { "d3drm_winetest", "*", "graphics:direct3d" },
    { "d3dx9_35_winetest", "*", "graphics:direct3d" },
    { "d3dx9_36_winetest", "*", "graphics:direct3d" },
    { "d3dx9_42_winetest", "*", "graphics:direct3d" },
    { "d3dx9_43_winetest", "*", "graphics:direct3d" },
    { "d3dxof_winetest", "*", "graphics:direct3d" },
    { "ddraw_winetest", "*", "graphics:direct3d" },
    { "dxdiagn_winetest", "*", "graphics:direct3d" },
    { "dxgi_apitest", "*", "graphics:direct3d" },
    { "dxgi_winetest", "*", "graphics:direct3d" },
    { "dcomp_apitest", "*", "graphics:dwm" },
    { "dwmapi_winetest", "*", "graphics:dwm" },
    { "dwmstack_apitest", "*", "graphics:dwm" },
    { "dciman32_apitest", "*", "graphics:gdi" },
    { "gdi32_apitest", "*", "graphics:gdi" },
    { "gdi32_winetest", "*", "graphics:gdi" },
    { "gdiplus_winetest", "*", "graphics:gdi" },
    { "mscms_winetest", "*", "graphics:gdi" },
    { "wing32_winetest", "*", "graphics:gdi" },
    { "opengl32_apitest", "*", "graphics:opengl" },
    { "opengl32_winetest", "*", "graphics:opengl" },
    { "dwrite_winetest", "*", "graphics:text" },
    { "fontext_apitest", "*", "graphics:text" },
    { "t2embed_winetest", "*", "graphics:text" },
    { "usp10_winetest", "*", "graphics:text" },
    { "win32u_apitest", "*", "graphics:win32k" },
    { "win32u_winetest", "*", "graphics:win32k" },
    { "amstream_winetest", "*", "media:directshow" },
    { "devenum_winetest", "*", "media:directshow" },
    { "msdmo_winetest", "*", "media:directshow" },
    { "qasf_winetest", "*", "media:directshow" },
    { "qcap_winetest", "*", "media:directshow" },
    { "qedit_winetest", "*", "media:directshow" },
    { "quartz_winetest", "*", "media:directshow" },
    { "dxva2_winetest", "*", "media:mf" },
    { "evr_winetest", "*", "media:mf" },
    { "mf_winetest", "*", "media:mf" },
    { "mfmediaengine_winetest", "*", "media:mf" },
    { "mfplat_winetest", "*", "media:mf" },
    { "mfplay_winetest", "*", "media:mf" },
    { "mfreadwrite_winetest", "*", "media:mf" },
    { "mfsrcsnk_winetest", "*", "media:mf" },
    { "avifil32_winetest", "*", "media:vfw" },
    { "iccvid_winetest", "*", "media:vfw" },
    { "msrle32_winetest", "*", "media:vfw" },
    { "msvfw32_winetest", "*", "media:vfw" },
    { "wmvcore_winetest", "*", "media:vfw" },
    { "activeds_winetest", "*", "network:directory" },
    { "dplayx_winetest", "*", "network:directory" },
    { "mpr_winetest", "*", "network:directory" },
    { "netapi32_apitest", "*", "network:directory" },
    { "netapi32_winetest", "*", "network:directory" },
    { "ntdsapi_winetest", "*", "network:directory" },
    { "rasapi32_winetest", "*", "network:directory" },
    { "tapi32_winetest", "*", "network:directory" },
    { "wldap32_winetest", "*", "network:directory" },
    { "jsproxy_winetest", "*", "network:http" },
    { "qmgr_winetest", "*", "network:http" },
    { "urlmon_winetest", "*", "network:http" },
    { "webservices_winetest", "*", "network:http" },
    { "winhttp_apitest", "*", "network:http" },
    { "winhttp_winetest", "*", "network:http" },
    { "wininet_apitest", "*", "network:http" },
    { "wininet_winetest", "*", "network:http" },
    { "wsdapi_winetest", "*", "network:http" },
    { "bluetoothapis_winetest", "*", "network:ip" },
    { "dnsapi_apitest", "*", "network:ip" },
    { "dnsapi_winetest", "*", "network:ip" },
    { "hnetcfg_winetest", "*", "network:ip" },
    { "inetmib1_winetest", "*", "network:ip" },
    { "iphlpapi_apitest", "*", "network:ip" },
    { "iphlpapi_winetest", "*", "network:ip" },
    { "netprofm_winetest", "*", "network:ip" },
    { "snmpapi_winetest", "*", "network:ip" },
    { "wlanapi_apitest", "*", "network:ip" },
    { "afd_apitest", "*", "network:sockets" },
    { "nsi_winetest", "*", "network:sockets" },
    { "tcpip_drvtest", "*", "network:sockets" },
    { "ws2_32_apitest", "*", "network:sockets" },
    { "ws2_32_winetest", "*", "network:sockets" },
    { "bcrypt_winetest", "*", "security:crypto" },
    { "crypt32_apitest", "*", "security:crypto" },
    { "crypt32_winetest", "*", "security:crypto" },
    { "cryptnet_winetest", "*", "security:crypto" },
    { "cryptowinrt_winetest", "*", "security:crypto" },
    { "cryptui_winetest", "*", "security:crypto" },
    { "dssenh_winetest", "*", "security:crypto" },
    { "ncrypt_winetest", "*", "security:crypto" },
    { "pstorec_winetest", "*", "security:crypto" },
    { "rsaenh_winetest", "*", "security:crypto" },
    { "schannel_winetest", "*", "security:crypto" },
    { "secur32_winetest", "*", "security:crypto" },
    { "winscard_winetest", "*", "security:crypto" },
    { "wintrust_winetest", "*", "security:crypto" },
    { "advapi32_apitest", "*", "security:tokens" },
    { "advapi32_winetest", "*", "security:tokens" },
    { "credui_winetest", "*", "security:tokens" },
    { "msgina_apitest", "*", "security:tokens" },
    { "sandbox_apitest", "*", "security:tokens" },
    { "userenv_apitest", "*", "security:tokens" },
    { "userenv_winetest", "*", "security:tokens" },
    { "cmd_apitest", "*", "shell:cmd" },
    { "cmd_rostest", "*", "shell:cmd" },
    { "cmd_winetest", "*", "shell:cmd" },
    { "xcopy_winetest", "*", "shell:cmd" },
    { "browseui_apitest", "*", "shell:explorer" },
    { "browseui_winetest", "*", "shell:explorer" },
    { "fanlaunch_apitest", "*", "shell:explorer" },
    { "folder_execute_apitest", "*", "shell:explorer" },
    { "netshell_apitest", "*", "shell:explorer" },
    { "notificationtest", "*", "shell:explorer" },
    { "shdocvw_apitest", "*", "shell:explorer" },
    { "shdocvw_winetest", "*", "shell:explorer" },
    { "taskmgr11_apitest", "*", "shell:explorer" },
    { "zipfldr_apitest", "*", "shell:explorer" },
    { "propsys_apitest", "*", "shell:shell32" },
    { "propsys_winetest", "*", "shell:shell32" },
    { "shcore_apitest", "*", "shell:shell32" },
    { "shcore_winetest", "*", "shell:shell32" },
    { "shell32_apitest", "*", "shell:shell32" },
    { "shell32_winetest", "*", "shell:shell32" },
    { "shlwapi_apitest", "*", "shell:shell32" },
    { "shlwapi_winetest", "*", "shell:shell32" },
    { "apphelp_apitest", "*", "system:compat" },
    { "appshim_apitest", "*", "system:compat" },
    { "cfg_apitest", "*", "system:compat" },
    { "utildll_apitest", "*", "system:compat" },
    { "version_apitest", "*", "system:compat" },
    { "version_winetest", "*", "system:compat" },
    { "reg_winetest", "*", "system:registry" },
    { "regedit_winetest", "*", "system:registry" },
    { "evtlogtest", "*", "system:services" },
    { "mstask_winetest", "*", "system:services" },
    { "powrprof_apitest", "*", "system:services" },
    { "psmtest", "*", "system:services" },
    { "schedsvc_winetest", "*", "system:services" },
    { "schtasks_winetest", "*", "system:services" },
    { "services_winetest", "*", "system:services" },
    { "sfc_apitest", "*", "system:services" },
    { "taskschd_winetest", "*", "system:services" },
    { "wtsapi32_winetest", "*", "system:services" },
    { "advpack_apitest", "*", "system:setup" },
    { "advpack_winetest", "*", "system:setup" },
    { "cabinet_winetest", "*", "system:setup" },
    { "cfgmgr32_winetest", "*", "system:setup" },
    { "lz32_winetest", "*", "system:setup" },
    { "msi_winetest", "*", "system:setup" },
    { "mspatcha_winetest", "*", "system:setup" },
    { "msptcha_apitest", "*", "system:setup" },
    { "netcfgx_winetest", "*", "system:setup" },
    { "newdev_apitest", "*", "system:setup" },
    { "newdev_unittest", "*", "system:setup" },
    { "setupapi_apitest", "*", "system:setup" },
    { "setupapi_winetest", "*", "system:setup" },
    { "setuplib_unittest", "*", "system:setup" },
    { "sti_winetest", "*", "system:setup" },
    { "acpi_apitest", "*", "system:storage" },
    { "fltlib_apitest", "*", "system:storage" },
    { "mountmgr_apitest", "*", "system:storage" },
    { "partmgr_apitest", "*", "system:storage" },
    { "parttest", "*", "system:storage" },
    { "pdh_winetest", "*", "system:wmi" },
    { "wbemdisp_winetest", "*", "system:wmi" },
    { "wbemprox_winetest", "*", "system:wmi" },
    { "wmiutils_winetest", "*", "system:wmi" },
    { "directmanipulation_winetest", "*", "ui:accessibility" },
    { "oleacc_winetest", "*", "ui:accessibility" },
    { "uiautomationcore_winetest", "*", "ui:accessibility" },
    { "buttonvistest", "*", "ui:controls" },
    { "comctl32_apitest", "*", "ui:controls" },
    { "comctl32_winetest", "*", "ui:controls" },
    { "comdlg32_winetest", "*", "ui:controls" },
    { "msftedit_winetest", "*", "ui:controls" },
    { "riched20_apitest", "*", "ui:controls" },
    { "riched20_winetest", "*", "ui:controls" },
    { "riched32_winetest", "*", "ui:controls" },
    { "dinput_winetest", "*", "ui:input" },
    { "hid_winetest", "*", "ui:input" },
    { "imm32_apitest", "*", "ui:input" },
    { "imm32_winetest", "*", "ui:input" },
    { "msctf_winetest", "*", "ui:input" },
    { "xinput1_3_winetest", "*", "ui:input" },
    { "uxtheme_apitest", "*", "ui:theme" },
    { "uxtheme_winetest", "*", "ui:theme" },
    { "user32_apitest", "*", "ui:user32" },
    { "user32_dynamic_apitest", "*", "ui:user32" },
    { "user32_winetest", "*", "ui:user32" },
    { "hlink_winetest", "*", "web:html" },
    { "ieframe_winetest", "*", "web:html" },
    { "iertutil_winetest", "*", "web:html" },
    { "itss_winetest", "*", "web:html" },
    { "mshtml_winetest", "*", "web:html" },
    { "inetcomm_winetest", "*", "web:mail" },
    { "mapi32_winetest", "*", "web:mail" },
    { "jscript_winetest", "*", "web:script" },
    { "scrrun_winetest", "*", "web:script" },
    { "vbscript_winetest", "*", "web:script" },
    { "wscript_apitest", "*", "web:script" },
    { "wscript_winetest", "*", "web:script" },
    { "wshom_winetest", "*", "web:script" },
    { "msxml3_winetest", "*", "web:xml" },
    { "msxml4_winetest", "*", "web:xml" },
    { "msxml6_winetest", "*", "web:xml" },
    { "opcservices_winetest", "*", "web:xml" },
    { "xmllite_winetest", "*", "web:xml" },
    { "windows_applicationmodel_winetest", "*", "winrt:api" },
    { "windows_devices_bluetooth_winetest", "*", "winrt:api" },
    { "windows_devices_enumeration_winetest", "*", "winrt:api" },
    { "windows_devices_radios_winetest", "*", "winrt:api" },
    { "windows_devices_usb_winetest", "*", "winrt:api" },
    { "windows_gaming_input_winetest", "*", "winrt:api" },
    { "windows_gaming_ui_gamebar_winetest", "*", "winrt:api" },
    { "windows_globalization_winetest", "*", "winrt:api" },
    { "windows_graphics_winetest", "*", "winrt:api" },
    { "windows_media_devices_winetest", "*", "winrt:api" },
    { "windows_media_mediacontrol_winetest", "*", "winrt:api" },
    { "windows_media_playback_backgroundmediaplayer_winetest", "*", "winrt:api" },
    { "windows_media_playback_mediaplayer_winetest", "*", "winrt:api" },
    { "windows_media_speech_winetest", "*", "winrt:api" },
    { "windows_media_winetest", "*", "winrt:api" },
    { "windows_networking_connectivity_winetest", "*", "winrt:api" },
    { "windows_networking_hostname_winetest", "*", "winrt:api" },
    { "windows_perception_stub_winetest", "*", "winrt:api" },
    { "windows_security_authentication_onlineid_winetest", "*", "winrt:api" },
    { "windows_security_credentials_ui_userconsentverifier_winetest", "*", "winrt:api" },
    { "windows_storage_applicationdata_winetest", "*", "winrt:api" },
    { "windows_storage_winetest", "*", "winrt:api" },
    { "windows_system_profile_systemid_winetest", "*", "winrt:api" },
    { "windows_system_profile_systemmanufacturers_winetest", "*", "winrt:api" },
    { "windows_ui_core_textinput_winetest", "*", "winrt:api" },
    { "windows_ui_winetest", "*", "winrt:api" },
    { "windows_ui_xaml_winetest", "*", "winrt:api" },
    { "windows_web_winetest", "*", "winrt:api" },
    { "rtworkq_winetest", "*", "winrt:runtime" },
    { "threadpoolwinrt_winetest", "*", "winrt:runtime" },
    { "wintypes_winetest", "*", "winrt:runtime" },
};

static bool
MatchesPattern(const char* Pattern, const string& Name)
{
    size_t Length = strlen(Pattern);

    if (Length && Pattern[Length - 1] == '*')
        return Name.compare(0, Length - 1, Pattern, Length - 1) == 0;

    return Name == Pattern;
}

static bool
IsSelected(const vector<string>& Selected, const char* Category)
{
    for (size_t i = 0; i < Selected.size(); i++)
    {
        const string& Name = Selected[i];

        if (Name == Category)
            return true;

        if (Name.find(':') == string::npos && !strncmp(Category, Name.c_str(), Name.length()) && Category[Name.length()] == ':')
            return true;
    }

    return false;
}

static void
AddSorted(vector<string>& Names, const string& Name)
{
    size_t i = 0;

    while (i < Names.size() && Names[i] < Name)
        i++;

    if (i == Names.size() || Names[i] != Name)
        Names.insert(Names.begin() + i, Name);
}

const char*
CCategories::Find(const string& Program, const string& Test)
{
    for (size_t i = 0; i < _countof(Rules); i++)
    {
        if (!_stricmp(Rules[i].Program, Program.c_str()) && MatchesPattern(Rules[i].Test, Test))
            return Rules[i].Category;
    }

    return UnsortedCategory;
}

bool
CCategories::IsKnown(const string& Name)
{
    vector<string> Selected(1, Name);

    if (IsSelected(Selected, UnsortedCategory))
        return true;

    for (size_t i = 0; i < _countof(Rules); i++)
    {
        if (IsSelected(Selected, Rules[i].Category))
            return true;
    }

    return false;
}

bool
CCategories::WantsProgram(const vector<string>& Selected, const string& Program)
{
    bool Known = false;

    if (Selected.empty())
        return true;

    for (size_t i = 0; i < _countof(Rules); i++)
    {
        if (_stricmp(Rules[i].Program, Program.c_str()))
            continue;

        if (IsSelected(Selected, Rules[i].Category))
            return true;

        Known = true;
    }

    return !Known && IsSelected(Selected, UnsortedCategory);
}

bool
CCategories::WantsTest(const vector<string>& Selected, const string& Program, const string& Test)
{
    return Selected.empty() || IsSelected(Selected, Find(Program, Test));
}

string
CCategories::Describe()
{
    vector<string> Names;
    stringstream ss;
    string Current;

    AddSorted(Names, UnsortedCategory);
    for (size_t i = 0; i < _countof(Rules); i++)
        AddSorted(Names, Rules[i].Category);

    ss << "Test categories for the /g option, each followed by its sub-categories:" << endl;
    for (size_t i = 0; i < Names.size(); i++)
    {
        size_t Colon = Names[i].find(':');
        string Category = Names[i].substr(0, Colon);

        if (Category != Current)
        {
            if (!Current.empty())
                ss << endl;
            ss << "  " << Category << ":";
            Current = Category;
        }

        ss << " " << Names[i].substr(Colon + 1);
    }
    ss << endl;

    return ss.str();
}
