/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Minimal emulated TLS support for Clang builds
 * COPYRIGHT:   Copyright 2025 ReactOS contributors
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#define WIN32_NO_STATUS
#include <windef.h>
#include <winnt.h>
#include <intrin.h>
#include <ndk/rtlfuncs.h>
#include <ndk/psfuncs.h>
#undef WIN32_NO_STATUS
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <ntstatus.h>

/*
 * Local raise helper to avoid pulling external RtlRaiseStatus symbol
 * into every consumer of libcntpr. This keeps linkage simple across
 * user-mode and kernel-mode targets, and still halts execution on
 * fatal conditions (e.g., out-of-memory during TLS setup).
 */
#if defined(_MSC_VER)
#  include <intrin.h>
static __forceinline void EmuTlsRaise(NTSTATUS Status)
{
    (void)Status;
    __fastfail(7); /* FAST_FAIL_FATAL_APP_EXIT */
}
#else
static __attribute__((noreturn)) inline void EmuTlsRaise(NTSTATUS Status)
{
    (void)Status;
    __builtin_trap();
}
#endif

typedef struct __emutls_control
{
    uintptr_t size;
    uintptr_t align;
    union
    {
        uintptr_t index;
        void *address;
    } object;
    void *value;
} __emutls_control;

typedef struct _EMUTLS_THREAD_ENTRY
{
    struct _EMUTLS_THREAD_ENTRY *Next;
    PVOID ThreadKey;
    uintptr_t Capacity;
    void **Slots;
} EMUTLS_THREAD_ENTRY, *PEMUTLS_THREAD_ENTRY;

static volatile LONG EmuTlsInitState;
static volatile LONG EmuTlsLockState;
static PEMUTLS_THREAD_ENTRY EmuTlsThreadList;
static uintptr_t EmuTlsObjectCount;
static HANDLE EmuTlsHeapHandle;

static __inline VOID
EmuTlsYield(VOID)
{
    YieldProcessor();
}

static VOID
EmuTlsAcquireLock(VOID)
{
    while (InterlockedCompareExchange(&EmuTlsLockState, 1, 0) != 0)
    {
        EmuTlsYield();
    }
}

static VOID
EmuTlsReleaseLock(VOID)
{
    InterlockedExchange(&EmuTlsLockState, 0);
}

static VOID
EmuTlsEnsureInitialized(VOID)
{
    LONG State;

    State = InterlockedCompareExchange(&EmuTlsInitState, 1, 0);
    if (State == 0)
    {
        EmuTlsThreadList = NULL;
        EmuTlsObjectCount = 0;
        EmuTlsHeapHandle = RtlCreateHeap(HEAP_GROWABLE, NULL, 0, 0, NULL, NULL);
        if (EmuTlsHeapHandle == NULL)
            EmuTlsRaise(STATUS_NO_MEMORY);
        InterlockedExchange(&EmuTlsInitState, 2);
    }
    else
    {
        while (EmuTlsInitState != 2)
        {
            EmuTlsYield();
        }
    }
}

static __inline PVOID
EmuTlsCurrentThreadKey(VOID)
{
    return NtCurrentTeb();
}

static __inline PVOID
EmuTlsAllocateZero(SIZE_T Size)
{
    PVOID Block;

    Block = RtlAllocateHeap(EmuTlsHeapHandle, 0, Size);
    if (Block == NULL)
        EmuTlsRaise(STATUS_NO_MEMORY);

    RtlZeroMemory(Block, Size);
    return Block;
}

static VOID
EmuTlsEnsureCapacity(
    _Inout_ PEMUTLS_THREAD_ENTRY Entry,
    _In_ uintptr_t Required)
{
    uintptr_t NewCapacity;
    SIZE_T OldSize;
    SIZE_T NewSize;
    void **NewSlots;

    if (Required <= Entry->Capacity)
        return;

    NewCapacity = Entry->Capacity ? Entry->Capacity : 4;
    while (NewCapacity < Required)
    {
        if (NewCapacity > (UINTPTR_MAX / 2))
        {
            NewCapacity = Required;
            break;
        }

        NewCapacity *= 2;
    }

    OldSize = Entry->Capacity * sizeof(void *);
    NewSize = NewCapacity * sizeof(void *);
    NewSlots = EmuTlsAllocateZero(NewSize);

    if (Entry->Slots != NULL && OldSize != 0)
    {
        RtlCopyMemory(NewSlots, Entry->Slots, OldSize);
        RtlFreeHeap(EmuTlsHeapHandle, 0, Entry->Slots);
    }

    Entry->Slots = NewSlots;
    Entry->Capacity = NewCapacity;
}

static VOID *
EmuTlsAllocateObject(__emutls_control *Control)
{
    SIZE_T Alignment;
    SIZE_T Size;
    PUCHAR Base;

    Size = (SIZE_T)Control->size;
    Alignment = (SIZE_T)Control->align;
    if (Alignment < sizeof(void *))
        Alignment = sizeof(void *);

    if ((Alignment & (Alignment - 1)) != 0)
        EmuTlsRaise(STATUS_NO_MEMORY);

    Base = EmuTlsAllocateZero(Size + Alignment);
    if (((ULONG_PTR)Base & (Alignment - 1)) != 0)
        Base = (PUCHAR)(((ULONG_PTR)Base + Alignment - 1) & ~(ULONG_PTR)(Alignment - 1));

    if (Control->value != NULL)
        RtlCopyMemory(Base, Control->value, Size);

    return Base;
}

static PEMUTLS_THREAD_ENTRY
EmuTlsFindThreadEntry(PVOID ThreadKey)
{
    PEMUTLS_THREAD_ENTRY Entry = EmuTlsThreadList;

    while (Entry != NULL)
    {
        if (Entry->ThreadKey == ThreadKey)
            return Entry;

        Entry = Entry->Next;
    }

    return NULL;
}

static PEMUTLS_THREAD_ENTRY
EmuTlsCreateThreadEntry(PVOID ThreadKey)
{
    PEMUTLS_THREAD_ENTRY Entry;

    Entry = EmuTlsAllocateZero(sizeof(*Entry));
    Entry->ThreadKey = ThreadKey;
    Entry->Next = EmuTlsThreadList;
    EmuTlsThreadList = Entry;
    return Entry;
}

static uintptr_t
EmuTlsGetIndex(__emutls_control *Control)
{
    uintptr_t Index;

    Index = Control->object.index;
    if (Index == 0)
    {
        EmuTlsAcquireLock();

        Index = Control->object.index;
        if (Index == 0)
        {
            EmuTlsObjectCount++;
            Index = EmuTlsObjectCount;
            Control->object.index = Index;
        }

        EmuTlsReleaseLock();
    }

    return Index;
}

void *
__cdecl
__emutls_get_address(void *ControlPtr)
{
    __emutls_control *Control;
    uintptr_t Index;
    PVOID ThreadKey;
    PEMUTLS_THREAD_ENTRY ThreadEntry;
    void **Slot;
    void *Result;

    Control = (__emutls_control *)ControlPtr;

    EmuTlsEnsureInitialized();

    Index = EmuTlsGetIndex(Control);
    ThreadKey = EmuTlsCurrentThreadKey();

    EmuTlsAcquireLock();

    ThreadEntry = EmuTlsFindThreadEntry(ThreadKey);
    if (ThreadEntry == NULL)
        ThreadEntry = EmuTlsCreateThreadEntry(ThreadKey);

    EmuTlsEnsureCapacity(ThreadEntry, Index);
    Slot = &ThreadEntry->Slots[Index - 1];

    if (*Slot == NULL)
        *Slot = EmuTlsAllocateObject(Control);

    Result = *Slot;

    EmuTlsReleaseLock();
    return Result;
}
