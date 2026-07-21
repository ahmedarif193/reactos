/*
 * Shared WoW64 user-mode definitions
 *
 * Copyright 2021 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef _REACTOS_WOW64_SHARED_H_
#define _REACTOS_WOW64_SHARED_H_

/* Reserved TEB64 TLS slots used by the WoW64 runtime. */
#define WOW64_TLS_CPURESERVED      1
#define WOW64_TLS_TEMPLIST         3
#define WOW64_TLS_USERCALLBACKDATA 5
#define WOW64_TLS_APCLIST          7
#define WOW64_TLS_FILESYSREDIR     8
#define WOW64_TLS_WOW64INFO        10
#define WOW64_TLS_MAX_NUMBER       19

#if defined(_NTOSKRNL_) && !defined(WOW64_CONTEXT_i386)
#define WOW64_CONTEXT_i386 0x00010000
#define WOW64_CONTEXT_CONTROL (WOW64_CONTEXT_i386 | 0x00000001L)
#define WOW64_CONTEXT_INTEGER (WOW64_CONTEXT_i386 | 0x00000002L)
#define WOW64_CONTEXT_SEGMENTS (WOW64_CONTEXT_i386 | 0x00000004L)
#define WOW64_CONTEXT_FLOATING_POINT (WOW64_CONTEXT_i386 | 0x00000008L)
#define WOW64_CONTEXT_DEBUG_REGISTERS (WOW64_CONTEXT_i386 | 0x00000010L)
#define WOW64_CONTEXT_EXTENDED_REGISTERS (WOW64_CONTEXT_i386 | 0x00000020L)
#define WOW64_CONTEXT_FULL (WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER | WOW64_CONTEXT_SEGMENTS)
#define WOW64_CONTEXT_ALL (WOW64_CONTEXT_FULL | WOW64_CONTEXT_FLOATING_POINT | WOW64_CONTEXT_DEBUG_REGISTERS | WOW64_CONTEXT_EXTENDED_REGISTERS)
#define WOW64_MAXIMUM_SUPPORTED_EXTENSION 512

typedef struct _WOW64_FLOATING_SAVE_AREA
{
    ULONG ControlWord;
    ULONG StatusWord;
    ULONG TagWord;
    ULONG ErrorOffset;
    ULONG ErrorSelector;
    ULONG DataOffset;
    ULONG DataSelector;
    UCHAR RegisterArea[80];
    ULONG Cr0NpxState;
} WOW64_FLOATING_SAVE_AREA;

#include <pshpack4.h>
typedef struct _WOW64_CONTEXT
{
    ULONG ContextFlags;
    ULONG Dr0;
    ULONG Dr1;
    ULONG Dr2;
    ULONG Dr3;
    ULONG Dr6;
    ULONG Dr7;
    WOW64_FLOATING_SAVE_AREA FloatSave;
    ULONG SegGs;
    ULONG SegFs;
    ULONG SegEs;
    ULONG SegDs;
    ULONG Edi;
    ULONG Esi;
    ULONG Ebx;
    ULONG Edx;
    ULONG Ecx;
    ULONG Eax;
    ULONG Ebp;
    ULONG Eip;
    ULONG SegCs;
    ULONG EFlags;
    ULONG Esp;
    ULONG SegSs;
    UCHAR ExtendedRegisters[WOW64_MAXIMUM_SUPPORTED_EXTENSION];
} WOW64_CONTEXT;
#include <poppack.h>
#endif

/* Undocumented layout of WOW64INFO.CrossProcessWorkList. */
typedef struct
{
    ULONG     next;
    ULONG     id;
    ULONGLONG addr;
    ULONGLONG size;
    ULONG     args[4];
} CROSS_PROCESS_WORK_ENTRY;

typedef union
{
    struct
    {
        ULONG first;
        ULONG counter;
    };
    volatile LONGLONG hdr;
} CROSS_PROCESS_WORK_HDR;

typedef struct
{
    CROSS_PROCESS_WORK_HDR   free_list;
    CROSS_PROCESS_WORK_HDR   work_list;
    ULONGLONG                unknown[4];
    CROSS_PROCESS_WORK_ENTRY entries[1];
} CROSS_PROCESS_WORK_LIST;

typedef enum
{
    CrossProcessPreVirtualAlloc    = 0,
    CrossProcessPostVirtualAlloc   = 1,
    CrossProcessPreVirtualFree     = 2,
    CrossProcessPostVirtualFree    = 3,
    CrossProcessPreVirtualProtect  = 4,
    CrossProcessPostVirtualProtect = 5,
    CrossProcessFlushCache         = 6,
    CrossProcessFlushCacheHeavy    = 7,
    CrossProcessMemoryWrite        = 8,
} CROSS_PROCESS_NOTIFICATION;

#define CROSS_PROCESS_LIST_FLUSH 0x80000000
#define CROSS_PROCESS_LIST_ENTRY(list,pos) \
    ((CROSS_PROCESS_WORK_ENTRY *)((char *)(list) + ((pos) & ~CROSS_PROCESS_LIST_FLUSH)))

typedef struct _WOW64_CPURESERVED
{
    USHORT Flags;
    USHORT Machine;
    /* CONTEXT context */
    /* CONTEXT_EX *context_ex */
} WOW64_CPURESERVED, *PWOW64_CPURESERVED;

#define WOW64_CPURESERVED_FLAG_RESET_STATE 1

#if defined(WOW64_CONTEXT_i386)
typedef struct _WOW64_CPU_INIT
{
    WOW64_CPURESERVED Cpu;
    WOW64_CONTEXT Context;
    PVOID ContextEx;
} WOW64_CPU_INIT, *PWOW64_CPU_INIT;

C_ASSERT(FIELD_OFFSET(WOW64_CPU_INIT, Context) == sizeof(WOW64_CPURESERVED));
C_ASSERT(FIELD_OFFSET(WOW64_CPU_INIT, ContextEx) == ((sizeof(WOW64_CPURESERVED) + sizeof(WOW64_CONTEXT) + sizeof(PVOID) - 1) & ~(sizeof(PVOID) - 1)));
#endif

typedef struct _WOW64_UNICODE_STRING
{
    USHORT Length;
    USHORT MaximumLength;
    ULONG Buffer;
} WOW64_UNICODE_STRING, *PWOW64_UNICODE_STRING;

typedef struct _WOW64_CURDIR
{
    WOW64_UNICODE_STRING DosPath;
    ULONG Handle;
} WOW64_CURDIR;

typedef struct _WOW64_DRIVE_LETTER_CURDIR
{
    USHORT Flags;
    USHORT Length;
    ULONG TimeStamp;
    WOW64_UNICODE_STRING DosPath;
} WOW64_DRIVE_LETTER_CURDIR;

#define WOW64_MAX_DRIVE_LETTERS 32

typedef struct _WOW64_USER_PROCESS_PARAMETERS
{
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    ULONG ConsoleHandle;
    ULONG ConsoleFlags;
    ULONG StandardInput;
    ULONG StandardOutput;
    ULONG StandardError;
    WOW64_CURDIR CurrentDirectory;
    WOW64_UNICODE_STRING DllPath;
    WOW64_UNICODE_STRING ImagePathName;
    WOW64_UNICODE_STRING CommandLine;
    ULONG Environment;
    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG FillAttribute;
    ULONG WindowFlags;
    ULONG ShowWindowFlags;
    WOW64_UNICODE_STRING WindowTitle;
    WOW64_UNICODE_STRING DesktopInfo;
    WOW64_UNICODE_STRING ShellInfo;
    WOW64_UNICODE_STRING RuntimeData;
    WOW64_DRIVE_LETTER_CURDIR CurrentDirectories[WOW64_MAX_DRIVE_LETTERS];
    ULONG EnvironmentSize;
    ULONG EnvironmentVersion;
} WOW64_USER_PROCESS_PARAMETERS, *PWOW64_USER_PROCESS_PARAMETERS;

typedef struct _WOW64_CPU_AREA_INFO
{
    void              *Context;
    void              *ContextEx;
    void              *ContextFlagsLocation;
    WOW64_CPURESERVED *CpuReserved;
    ULONG              ContextFlag;
    USHORT             Machine;
    USHORT             Reserved;
    ULONG64            Unknown;
} WOW64_CPU_AREA_INFO, *PWOW64_CPU_AREA_INFO;

typedef struct _WOW64INFO
{
    ULONG     NativeSystemPageSize;
    ULONG     CpuFlags;
    ULONG     Wow64ExecuteFlags;
    ULONG     unknown;
    ULONGLONG SectionHandle;
    ULONGLONG CrossProcessWorkList;
    USHORT    NativeMachineType;
    USHORT    EmulatedMachineType;
} WOW64INFO;

C_ASSERT(sizeof(WOW64INFO) == 40);

/* Undocumented Windows 10 layout shared by native and guest ntdll. */
typedef struct _SYSTEM_DLL_INIT_BLOCK
{
    ULONG   version;
    ULONG   unknown1[3];
    ULONG64 unknown2;
    ULONG64 pLdrInitializeThunk;
    ULONG64 pKiUserExceptionDispatcher;
    ULONG64 pKiUserApcDispatcher;
    ULONG64 pKiUserCallbackDispatcher;
    ULONG64 pRtlUserThreadStart;
    ULONG64 pRtlpQueryProcessDebugInformationRemote;
    ULONG64 ntdll_handle;
    ULONG64 pLdrSystemDllInitBlock;
    ULONG64 pRtlpFreezeTimeBias;
} SYSTEM_DLL_INIT_BLOCK, *PSYSTEM_DLL_INIT_BLOCK;

#define WOW64_CPUFLAGS_MSFT64   0x01
#define WOW64_CPUFLAGS_SOFTWARE 0x02

#endif /* _REACTOS_WOW64_SHARED_H_ */
