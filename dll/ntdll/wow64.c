/*
 * NT WoW64 process support
 *
 * Copyright 2018 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/* Synced with Wine commit e8781e7c8d0. ReactOS-specific changes are marked below. */

#include <ntdll.h>
#define __WINE_WINNT_EXCEPTION_REGISTRATION_RECORD
#include <wine/winnt.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTL_THREAD_DESCRIPTOR_INFORMATION
{
    ULONG Selector;
    LDT_ENTRY Entry;
} RTL_THREAD_DESCRIPTOR_INFORMATION;

NTSTATUS WINAPI RtlWow64GetCurrentCpuArea(USHORT *machine, void **context, void **context_ex);

static USHORT
RtlpNativeMachine(VOID)
{
#if defined(_M_AMD64)
    return IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    return IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_IX86)
    return IMAGE_FILE_MACHINE_I386;
#elif defined(_M_ARM)
    return IMAGE_FILE_MACHINE_ARMNT;
#else
    return IMAGE_FILE_MACHINE_UNKNOWN;
#endif
}

USHORT
WINAPI
RtlWow64GetCurrentMachine(VOID)
{
    USHORT machine = RtlpNativeMachine();

#ifdef _WIN64
    if (NtCurrentTeb()->WowTebOffset)
        RtlWow64GetCurrentCpuArea(&machine, NULL, NULL);
#endif
    return machine;
}

NTSTATUS
WINAPI
RtlWow64GetProcessMachines(HANDLE process, USHORT *current_ret, USHORT *native_ret)
{
    SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION machines[8];
    USHORT current = 0, native = 0;
    NTSTATUS status;
    ULONG i;

    status = NtQuerySystemInformationEx(SystemSupportedProcessorArchitectures, &process, sizeof(process), machines, sizeof(machines), NULL);
    if (status)
        return status;

    for (i = 0; machines[i].Machine; ++i)
    {
        if (machines[i].Native)
            native = machines[i].Machine;
        else if (machines[i].Process)
            current = machines[i].Machine;
    }

    if (current_ret)
        *current_ret = current;
    if (native_ret)
        *native_ret = native;
    return status;
}

NTSTATUS
WINAPI
RtlWow64GetSharedInfoProcess(HANDLE process, BOOLEAN *is_wow64, WOW64INFO *info)
{
    PEB32 *peb32;
    NTSTATUS status;

    status = NtQueryInformationProcess(process, ProcessWow64Information, &peb32, sizeof(peb32), NULL);
    if (status)
        return status;
    if (peb32)
        status = NtReadVirtualMemory(process, peb32 + 1, info, sizeof(*info), NULL);
    *is_wow64 = !!peb32;
    return status;
}

NTSTATUS
WINAPI
RtlWow64IsWowGuestMachineSupported(USHORT machine, BOOLEAN *supported)
{
    SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION machines[8];
    HANDLE process = NULL;
    NTSTATUS status;
    ULONG i;

    status = NtQuerySystemInformationEx(SystemSupportedProcessorArchitectures, &process, sizeof(process), machines, sizeof(machines), NULL);
    if (status)
        return status;

    *supported = FALSE;
    for (i = 0; machines[i].Machine; ++i)
    {
        if (!machines[i].Native && machine == machines[i].Machine)
            *supported = TRUE;
    }
    return status;
}

#if defined(_WIN64) && (!defined(__REACTOS__) || !defined(_M_ARM64))

NTSTATUS
WINAPI
RtlWow64GetCpuAreaInfo(WOW64_CPURESERVED *cpu, ULONG reserved, WOW64_CPU_AREA_INFO *info)
{
    static const struct
    {
        ULONG machine;
        ULONG align;
        ULONG size;
        ULONG offset;
        ULONG flag;
    } data[] =
    {
#define ENTRY(machine, type, flag) { machine, TYPE_ALIGNMENT(type), sizeof(type), FIELD_OFFSET(type, ContextFlags), flag },
        ENTRY(IMAGE_FILE_MACHINE_I386, I386_CONTEXT, CONTEXT_i386)
        ENTRY(IMAGE_FILE_MACHINE_AMD64, AMD64_CONTEXT, CONTEXT_AMD64)
        ENTRY(IMAGE_FILE_MACHINE_ARMNT, ARM_CONTEXT, CONTEXT_ARM)
        ENTRY(IMAGE_FILE_MACHINE_ARM64, ARM64_NT_CONTEXT, CONTEXT_ARM64)
#undef ENTRY
    };
    ULONG i;

    UNREFERENCED_PARAMETER(reserved);

    if (!cpu || !info)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < RTL_NUMBER_OF(data); ++i)
    {
#define ALIGN_POINTER(ptr, align) ((void *)(((ULONG_PTR)(ptr) + (align) - 1) & ~((ULONG_PTR)(align) - 1)))
        if (data[i].machine != cpu->Machine)
            continue;
        info->Context = ALIGN_POINTER(cpu + 1, data[i].align);
        info->ContextEx = ALIGN_POINTER((char *)info->Context + data[i].size, sizeof(void *));
        info->ContextFlagsLocation = (char *)info->Context + data[i].offset;
        info->ContextFlag = data[i].flag;
        info->CpuReserved = cpu;
        info->Machine = data[i].machine;
        info->Reserved = 0;
        info->Unknown = 0;
        return STATUS_SUCCESS;
#undef ALIGN_POINTER
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
WINAPI
RtlWow64GetCurrentCpuArea(USHORT *machine, void **context, void **context_ex)
{
    WOW64_CPU_AREA_INFO info;
    NTSTATUS status;

    status = RtlWow64GetCpuAreaInfo(NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED], 0, &info);
    if (!status)
    {
        if (machine)
            *machine = info.Machine;
        if (context)
            *context = info.Context;
        if (context_ex)
            *context_ex = *(void **)info.ContextEx;
    }
    return status;
}

NTSTATUS
WINAPI
RtlWow64GetThreadContext(HANDLE handle, WOW64_CONTEXT *context)
{
    return NtQueryInformationThread(handle, ThreadWow64Context, context, sizeof(*context), NULL);
}

NTSTATUS
WINAPI
RtlWow64SetThreadContext(HANDLE handle, WOW64_CONTEXT *context)
{
    return NtSetInformationThread(handle, ThreadWow64Context, context, sizeof(*context));
}

NTSTATUS
WINAPI
RtlWow64GetThreadSelectorEntry(HANDLE handle,
                               RTL_THREAD_DESCRIPTOR_INFORMATION *info,
                               ULONG size,
                               ULONG *retlen)
{
    ULONG selector;
    WOW64_CONTEXT context = { WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_SEGMENTS };
    LDT_ENTRY entry = { 0 };

    if (size != sizeof(*info))
        return STATUS_INFO_LENGTH_MISMATCH;

    if (RtlWow64GetThreadContext(handle, &context))
    {
        context.SegCs = 0x23;
        __asm__("movw %%fs,%0" : "=m" (context.SegFs));
        __asm__("movw %%ss,%0" : "=m" (context.SegSs));
    }

    selector = info->Selector | 3;
    if (selector == 0x03)
        goto done;

    if (selector & 0x04)
        return NtQueryInformationThread(handle, ThreadDescriptorTableEntry, info, size, NULL);

    entry.HighWord.Bits.Dpl = 3;
    entry.HighWord.Bits.Pres = 1;
    entry.HighWord.Bits.Default_Big = 1;
    if (selector == context.SegCs)
    {
        entry.LimitLow = 0xffff;
        entry.HighWord.Bits.LimitHi = 0xf;
        entry.HighWord.Bits.Type = 0x1b;
        entry.HighWord.Bits.Granularity = 1;
    }
    else if (selector == context.SegSs)
    {
        entry.LimitLow = 0xffff;
        entry.HighWord.Bits.LimitHi = 0xf;
        entry.HighWord.Bits.Type = 0x13;
        entry.HighWord.Bits.Granularity = 1;
    }
    else if (selector == context.SegFs)
    {
        THREAD_BASIC_INFORMATION basic;

        entry.LimitLow = 0xfff;
        entry.HighWord.Bits.Type = 0x13;
        if (!NtQueryInformationThread(handle, ThreadBasicInformation, &basic, sizeof(basic), NULL))
        {
            ULONG address = (ULONG_PTR)basic.TebBaseAddress + 0x2000;
            entry.BaseLow = address;
            entry.HighWord.Bytes.BaseMid = address >> 16;
            entry.HighWord.Bytes.BaseHi = address >> 24;
        }
    }
    else
    {
        return STATUS_UNSUCCESSFUL;
    }

done:
    info->Entry = entry;
    if (retlen)
        *retlen = sizeof(entry);
    return STATUS_SUCCESS;
}

static BOOLEAN
RtlpIsCurrentProcess(HANDLE process)
{
    return process == NtCurrentProcess() || !NtCompareObjects(process, NtCurrentProcess());
}

BOOLEAN
WINAPI
RtlIsCurrentProcess(HANDLE process)
{
    return RtlpIsCurrentProcess(process);
}

VOID
WINAPI
RtlOpenCrossProcessEmulatorWorkConnection(HANDLE process, HANDLE *section, void **address)
{
    WOW64INFO wow64info;
    BOOLEAN is_wow64;
    HANDLE remote_handle = NULL;
    SIZE_T size = 0;

    *address = NULL;
    *section = NULL;

    if (RtlpIsCurrentProcess(process))
        return;
    if (RtlWow64GetSharedInfoProcess(process, &is_wow64, &wow64info))
        return;
    if (is_wow64)
        remote_handle = (HANDLE)(ULONG_PTR)wow64info.SectionHandle;
    if (!remote_handle)
        return;

    if (NtDuplicateObject(process, remote_handle, NtCurrentProcess(), section, 0, 0, DUPLICATE_SAME_ACCESS))
        return;

    if (!NtMapViewOfSection(*section, NtCurrentProcess(), address, 0, 0, NULL, &size, ViewShare, 0, PAGE_READWRITE))
        return;

    NtClose(*section);
    *section = NULL;
}

CROSS_PROCESS_WORK_ENTRY *
WINAPI
RtlWow64PopAllCrossProcessWorkFromWorkList(CROSS_PROCESS_WORK_HDR *list, BOOLEAN *flush)
{
    CROSS_PROCESS_WORK_HDR previous, next_header;
    ULONG position, previous_position = 0;

    do
    {
        previous.hdr = list->hdr;
        if (!previous.first)
            break;
        next_header.first = 0;
        next_header.counter = previous.counter + 1;
    } while (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) != previous.hdr);

    *flush = (previous.first & CROSS_PROCESS_LIST_FLUSH) != 0;
    position = previous.first & ~CROSS_PROCESS_LIST_FLUSH;
    if (!position)
        return NULL;

    for (;;)
    {
        CROSS_PROCESS_WORK_ENTRY *entry = CROSS_PROCESS_LIST_ENTRY(list, position);
        ULONG next = entry->next;

        entry->next = previous_position;
        if (!next)
            return entry;
        previous_position = position;
        position = next;
    }
}

CROSS_PROCESS_WORK_ENTRY *
WINAPI
RtlWow64PopCrossProcessWorkFromFreeList(CROSS_PROCESS_WORK_HDR *list)
{
    CROSS_PROCESS_WORK_ENTRY *entry;
    CROSS_PROCESS_WORK_HDR previous, next_header;

    do
    {
        previous.hdr = list->hdr;
        if (!previous.first)
            return NULL;
        entry = CROSS_PROCESS_LIST_ENTRY(list, previous.first);
        next_header.first = entry->next;
        next_header.counter = previous.counter + 1;
    } while (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) != previous.hdr);

    entry->next = 0;
    return entry;
}

BOOLEAN
WINAPI
RtlWow64PushCrossProcessWorkOntoFreeList(CROSS_PROCESS_WORK_HDR *list, CROSS_PROCESS_WORK_ENTRY *entry)
{
    CROSS_PROCESS_WORK_HDR previous, next_header;

    do
    {
        previous.hdr = list->hdr;
        entry->next = previous.first;
        next_header.first = (ULONG)((char *)entry - (char *)list);
        next_header.counter = previous.counter + 1;
    } while (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) != previous.hdr);

    return TRUE;
}

BOOLEAN
WINAPI
RtlWow64PushCrossProcessWorkOntoWorkList(CROSS_PROCESS_WORK_HDR *list,
                                         CROSS_PROCESS_WORK_ENTRY *entry,
                                         void **unknown)
{
    CROSS_PROCESS_WORK_HDR previous, next_header;

    *unknown = NULL;
    do
    {
        previous.hdr = list->hdr;
        entry->next = previous.first;
        next_header.first = (ULONG)((char *)entry - (char *)list) |
                            (previous.first & CROSS_PROCESS_LIST_FLUSH);
        next_header.counter = previous.counter + 1;
    } while (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) != previous.hdr);

    return TRUE;
}

BOOLEAN
WINAPI
RtlWow64RequestCrossProcessHeavyFlush(CROSS_PROCESS_WORK_HDR *list)
{
    CROSS_PROCESS_WORK_HDR previous, next_header;

    do
    {
        previous.hdr = list->hdr;
        next_header.first = previous.first | CROSS_PROCESS_LIST_FLUSH;
        next_header.counter = previous.counter + 1;
    } while (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) != previous.hdr);

    return TRUE;
}

#endif /* _WIN64 */
