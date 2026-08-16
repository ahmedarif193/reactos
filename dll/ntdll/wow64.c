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

#ifdef _WIN64
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
#endif

static BOOLEAN
RtlpIsCurrentProcess(HANDLE process)
{
    return process == NtCurrentProcess() || !NtCompareObjects(NtCurrentProcess(), process);
}

BOOLEAN
WINAPI
RtlIsCurrentProcess(HANDLE process)
{
    return RtlpIsCurrentProcess(process);
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

#endif /* _WIN64 && !(ReactOS ARM64) */

#if defined(_WIN64)

#define CROSS_PROCESS_WORK_LIST_ALIGNMENT 0x1000
#define CROSS_PROCESS_WORK_LIST_SIZE      0x4000

static VOID
RtlpValidateCrossProcessWorkEntry(CROSS_PROCESS_WORK_HDR *list, CROSS_PROCESS_WORK_ENTRY *entry)
{
    ULONG_PTR list_base = (ULONG_PTR)list & ~(CROSS_PROCESS_WORK_LIST_ALIGNMENT - 1);
    ULONG_PTR list_end = list_base + CROSS_PROCESS_WORK_LIST_SIZE;
    ULONG_PTR entry_address = (ULONG_PTR)entry;
    ULONG_PTR entry_end = entry_address + sizeof(*entry);

    if (list_end <= list_base || entry_end <= entry_address || entry_address < list_base || entry_end > list_end)
        RtlRaiseStatus(STATUS_INVALID_PARAMETER);
}

static __inline LONGLONG
RtlpReadCrossProcessWorkHeader(CROSS_PROCESS_WORK_HDR *list)
{
#if defined(__GNUC__)
    return __atomic_load_n(&list->hdr, __ATOMIC_ACQUIRE);
#else
    return ReadAcquire64(&list->hdr);
#endif
}

VOID
WINAPI
RtlOpenCrossProcessEmulatorWorkConnection(HANDLE process, HANDLE *section, void **address)
{
    WOW64INFO wow64info;
    BOOLEAN is_wow64;
    HANDLE remote_handle = NULL;
    HANDLE local_section = NULL;
    PVOID local_address = NULL;
    SIZE_T size = 0;

    *address = NULL;
    *section = NULL;

    if (RtlpIsCurrentProcess(process))
        return;
    if (!RtlWow64GetSharedInfoProcess(process, &is_wow64, &wow64info) && is_wow64 && (wow64info.CpuFlags & WOW64_CPUFLAGS_SOFTWARE))
        remote_handle = (HANDLE)(ULONG_PTR)wow64info.SectionHandle;

#if defined(__REACTOS__) && defined(_M_ARM64)
    if (!remote_handle)
    {
        PROCESS_BASIC_INFORMATION process_info;
        CHPEV2_PROCESS_INFO64 chpe_info;
        PVOID remote_info = NULL;

        C_ASSERT(FIELD_OFFSET(PEB, ChpeV2ProcessInfo) == 0x338);

        if (!NtQueryInformationProcess(process, ProcessBasicInformation, &process_info, sizeof(process_info), NULL) && !NtReadVirtualMemory(process, &process_info.PebBaseAddress->ChpeV2ProcessInfo, &remote_info, sizeof(remote_info), NULL) && remote_info && !NtReadVirtualMemory(process, remote_info, &chpe_info, sizeof(chpe_info), NULL))
            remote_handle = (HANDLE)(ULONG_PTR)chpe_info.SectionHandle;
    }
#endif

    if (!remote_handle)
        return;

    if (NtDuplicateObject(process, remote_handle, NtCurrentProcess(), &local_section, 0, 0, DUPLICATE_SAME_ACCESS | DUPLICATE_SAME_ATTRIBUTES))
        goto failed;

    if (!NtMapViewOfSection(local_section, NtCurrentProcess(), &local_address, 0, 0, NULL, &size, ViewUnmap, MEM_TOP_DOWN, PAGE_READWRITE))
    {
        *section = local_section;
        *address = local_address;
        return;
    }

failed:
    if (local_address)
        NtUnmapViewOfSection(NtCurrentProcess(), local_address);
    if (local_section)
        NtClose(local_section);
}

CROSS_PROCESS_WORK_ENTRY *
WINAPI
RtlWow64PopAllCrossProcessWorkFromWorkList(CROSS_PROCESS_WORK_HDR *list, BOOLEAN *flush)
{
    CROSS_PROCESS_WORK_HDR previous, next_header;
    CROSS_PROCESS_WORK_ENTRY *entry;
    BOOLEAN flush_requested;
    ULONG position, previous_position;

    do
    {
        previous.hdr = RtlpReadCrossProcessWorkHeader(list);
        position = previous.first & ~CROSS_PROCESS_LIST_FLUSH;
        flush_requested = (previous.first & CROSS_PROCESS_LIST_FLUSH) != 0;
        if (!position && !flush_requested)
            break;
        next_header.first = 0;
        next_header.counter = previous.counter + 1;
    } while (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) != previous.hdr);

    *flush = flush_requested;
    if (!position)
        return NULL;
    entry = (CROSS_PROCESS_WORK_ENTRY *)((char *)list + position);
    if (flush_requested)
        return entry;

    previous_position = 0;
    for (;;)
    {
        ULONG next;

        RtlpValidateCrossProcessWorkEntry(list, entry);
        next = entry->next;
        entry->next = previous_position;
        if (!next)
            return entry;
        previous_position = position;
        position = next;
        entry = (CROSS_PROCESS_WORK_ENTRY *)((char *)list + position);
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
        previous.hdr = RtlpReadCrossProcessWorkHeader(list);
        if (!(previous.first & ~CROSS_PROCESS_LIST_FLUSH))
            return NULL;
        entry = CROSS_PROCESS_LIST_ENTRY(list, previous.first);
        RtlpValidateCrossProcessWorkEntry(list, entry);
        next_header.first = (previous.first & CROSS_PROCESS_LIST_FLUSH) | (entry->next & ~CROSS_PROCESS_LIST_FLUSH);
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
    ULONG position;

    RtlpValidateCrossProcessWorkEntry(list, entry);
    position = (ULONG)((char *)entry - (char *)list);

    do
    {
        previous.hdr = RtlpReadCrossProcessWorkHeader(list);
        entry->next = previous.first & ~CROSS_PROCESS_LIST_FLUSH;
        next_header.first = (previous.first & CROSS_PROCESS_LIST_FLUSH) | position;
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
    CROSS_PROCESS_WORK_ENTRY saved_entry, *previous_entry, *returned_entry;
    BOOLEAN coalesced, saved = FALSE;
    ULONG position;
    ULONGLONG entry_end, previous_end;

    RtlpValidateCrossProcessWorkEntry(list, entry);
    position = (ULONG)((char *)entry - (char *)list);

    do
    {
        previous.hdr = RtlpReadCrossProcessWorkHeader(list);
        coalesced = FALSE;

        if (previous.first & CROSS_PROCESS_LIST_FLUSH)
        {
            returned_entry = entry;
            next_header.first = previous.first;
        }
        else
        {
            returned_entry = NULL;
            previous_entry = NULL;
            entry_end = entry->addr + entry->size;

            if (entry->id == CrossProcessMemoryWrite && (previous.first & ~CROSS_PROCESS_LIST_FLUSH) && entry_end >= entry->addr)
            {
                previous_entry = CROSS_PROCESS_LIST_ENTRY(list, previous.first);
                RtlpValidateCrossProcessWorkEntry(list, previous_entry);
                previous_end = previous_entry->addr + previous_entry->size;
                if (previous_entry->id == CrossProcessMemoryWrite && previous_end >= previous_entry->addr && entry->addr == previous_end)
                {
                    if (!saved)
                    {
                        saved_entry = *entry;
                        saved = TRUE;
                    }
                    entry->next = previous_entry->next;
                    entry->addr = previous_entry->addr;
                    entry->size = entry_end - previous_entry->addr;
                    returned_entry = previous_entry;
                    coalesced = TRUE;
                }
            }

            if (!coalesced)
                entry->next = previous.first & ~CROSS_PROCESS_LIST_FLUSH;

            next_header.first = position;
        }

        next_header.counter = previous.counter + 1;

        if (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) == previous.hdr)
            break;

        if (coalesced)
            *entry = saved_entry;
    } while (TRUE);

    *unknown = returned_entry;
    return TRUE;
}

BOOLEAN
WINAPI
RtlWow64RequestCrossProcessHeavyFlush(CROSS_PROCESS_WORK_HDR *list)
{
    CROSS_PROCESS_WORK_HDR previous, next_header;

    do
    {
        previous.hdr = RtlpReadCrossProcessWorkHeader(list);
        next_header.first = previous.first | CROSS_PROCESS_LIST_FLUSH;
        next_header.counter = previous.counter + 1;
    } while (InterlockedCompareExchange64(&list->hdr, next_header.hdr, previous.hdr) != previous.hdr);

    return TRUE;
}

#endif /* _WIN64 */
