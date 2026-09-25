#include <nvs/nt/mint.h>
#include "hardware.h"

typedef struct _MI_I386_BOOT_ALLOCATOR
{
    PLOADER_PARAMETER_BLOCK Loader;
    PMEMORY_ALLOCATION_DESCRIPTOR Last;
} MI_I386_BOOT_ALLOCATOR;

static BOOLEAN
MiI386BootAllocate(MI_I386_BOOT_ALLOCATOR *Allocator, PULONG Frame)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Largest = NULL;
    PMEMORY_ALLOCATION_DESCRIPTOR Reserved = Allocator->Last;
    PLIST_ENTRY Entry;

    for (Entry = Allocator->Loader->MemoryDescriptorListHead.Flink;
         Entry != &Allocator->Loader->MemoryDescriptorListHead; Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);
        if (Descriptor->MemoryType == LoaderFree && Descriptor->PageCount &&
            Descriptor->BasePage < MI_I386_DIRECT_PAGES &&
            Descriptor->PageCount <= MI_I386_DIRECT_PAGES - Descriptor->BasePage &&
            (Largest == NULL || Descriptor->PageCount > Largest->PageCount))
            Largest = Descriptor;
    }
    if (Largest == NULL)
        return FALSE;

    *Frame = (ULONG)(Largest->BasePage + Largest->PageCount - 1);
    if (Reserved == NULL || Reserved->BasePage != *Frame + 1)
    {
        Reserved = MiKmAllocate(sizeof(*Reserved));
        if (Reserved == NULL)
            return FALSE;
        Reserved->MemoryType = LoaderMemoryData;
        Reserved->BasePage = *Frame + 1;
        Reserved->PageCount = 0;
        InsertHeadList(&Allocator->Loader->MemoryDescriptorListHead, &Reserved->ListEntry);
        Allocator->Last = Reserved;
    }
    Reserved->BasePage--;
    Reserved->PageCount++;
    Largest->PageCount--;
    return TRUE;
}

static NTSTATUS
MiI386BootMap(MI_I386_BOOT_ALLOCATOR *Allocator, ULONG Frame)
{
    ULONG Address = MI_I386_DIRECT_BASE + (Frame << PAGE_SHIFT);
    ULONG Index = Address >> 22;
    PMI_PTE Root = (PMI_PTE)(ULONG_PTR)MI_I386_ROOT_VA;
    PMI_PTE Table = (PMI_PTE)(ULONG_PTR)(MI_I386_SELF_BASE + (Index << PAGE_SHIFT));
    PMI_PTE Slot;
    MI_PTE Existing;

    if (!MiArchPteIsValid(MiArchPteRead(&Root[Index])))
    {
        ULONG Child;
        if (!MiI386BootAllocate(Allocator, &Child))
            return STATUS_NO_MEMORY;
        MiArchPteWrite(&Root[Index], MiArchPteMakeTable(Child, 0));
        __invlpg(Table);
        RtlZeroMemory(Table, PAGE_SIZE);
    }
    else if (MiArchPteIsBlock(MiArchPteRead(&Root[Index]), 1))
    {
        return STATUS_CONFLICTING_ADDRESSES;
    }

    Slot = &Table[(Address >> PAGE_SHIFT) & 1023];
    Existing = MiArchPteRead(Slot);
    if (Existing != 0)
    {
        if (!MiArchPteIsValid(Existing) || MiArchPteFrame(Existing) != Frame)
            return STATUS_CONFLICTING_ADDRESSES;
    }
    else if (Frame != 0)
    {
        MiArchPteWrite(Slot, MiArchPteMakeLeaf(Frame, MI_PROT_READWRITE,
                                                MI_LEAF_GLOBAL | MI_LEAF_DIRTY));
        __invlpg((PVOID)(ULONG_PTR)Address);
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
MiInitializeKernelVaLayout(const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    MI_I386_BOOT_ALLOCATOR Allocator = { (PLOADER_PARAMETER_BLOCK)LoaderBlock, NULL };
    PLIST_ENTRY Entry;

    MmSystemRangeStart = (PVOID)(ULONG_PTR)MI_I386_DIRECT_BASE;
    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead; Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR,
                                                                     ListEntry);
        ULONG64 First = Descriptor->BasePage;
        ULONG64 End = First + Descriptor->PageCount;
        ULONG64 Frame;

        if (Descriptor->PageCount == 0 || Descriptor->MemoryType == LoaderBad ||
            Descriptor->MemoryType == LoaderFirmwarePermanent || Descriptor->MemoryType == LoaderSpecialMemory ||
            Descriptor->MemoryType == LoaderHALCachedMemory || Descriptor->MemoryType == LoaderBBTMemory)
            continue;
        if (First >= MI_I386_DIRECT_PAGES || End > MI_I386_DIRECT_PAGES || End < First)
            KeBugCheckEx(MEMORY_MANAGEMENT, 0x444D4150, (ULONG_PTR)First, (ULONG_PTR)End, 0);

        for (Frame = First; Frame < End; Frame++)
        {
            NTSTATUS Status = MiI386BootMap(&Allocator, (ULONG)Frame);
            if (!NT_SUCCESS(Status))
                KeBugCheckEx(MEMORY_MANAGEMENT, 0x444D4150, (ULONG_PTR)Status, (ULONG_PTR)Frame, 0);
        }
    }
    MiI386SetDirectMapReady();
}
