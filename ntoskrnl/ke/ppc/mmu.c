/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC hashed page table, TLB and address spaces
 *
 * The hashed page table caches the software page tables (it is filled by the
 * real-mode reload in trap.S). Its entries are big-endian, so with
 * little-endian data accesses every word is byte-swapped. Changing or removing a software entry must
 * remove the matching hashed entry and the TLB entry. User segments 0-7 carry
 * per-process VSIDs, supervisor segments 8-15 the fixed PPC_KERNEL_VSID
 * values, so entries of other address spaces never match and a process
 * switch reloads only the user segment registers.
 */

#include <ntoskrnl.h>

#define KI_PPC_HPTE_VALID  0x80000000UL
#define KI_PPC_HPTE_HASH   0x00000040UL
#define KI_PPC_TLB_CLASSES 64

/* NVS can trim an inactive process without attaching to it. Its range
 * invalidation interface supplies a VA, not the owning address space.
 * Remember that inactive VSIDs may therefore contain stale HPTEs. */
static BOOLEAN KiPpcAddressSpacesStale;

typedef struct _KI_PPC_HPTE
{
    ULONG Word0;
    ULONG Word1;
} KI_PPC_HPTE, *PKI_PPC_HPTE;

static
PKI_PPC_HPTE
KiPpcHashGroup(
    _In_ PKPCR Pcr,
    _In_ ULONG Hash)
{
    ULONG Offset = (Hash & Pcr->HashTableMask) << 6;

    return (PKI_PPC_HPTE)(PPC_LOADER_KSEG0_BASE + Pcr->HashTable + Offset);
}

static
VOID
KiPpcTlbInvalidateAllClasses(VOID)
{
    ULONG Index;

    __asm__ __volatile__("sync" ::: "memory");
    for (Index = 0; Index < KI_PPC_TLB_CLASSES; Index++)
        __asm__ __volatile__("tlbie %0" :: "r"(Index << PAGE_SHIFT) : "memory");
    __asm__ __volatile__("sync" ::: "memory");
}

VOID
NTAPI
KiPpcFlushTranslation(_In_ PVOID Address)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();
    ULONG Ea = (ULONG_PTR)Address & ~(PAGE_SIZE - 1);
    ULONG Segment, Vsid, Api, Hash, Pass, Index;

    if (Ea < PPC_LOADER_KSEG0_BASE)
        KiPpcAddressSpacesStale = TRUE;

    __asm__ __volatile__("mfsrin %0, %1" : "=r"(Segment) : "r"(Ea));
    Vsid = Segment & PPC_SR_VSID_MASK;
    Api = (Ea >> 22) & 0x3F;
    Hash = (Vsid & 0x7FFFF) ^ ((Ea >> PAGE_SHIFT) & 0xFFFF);

    for (Pass = 0; Pass < 2; Pass++)
    {
        PKI_PPC_HPTE Group = KiPpcHashGroup(Pcr, Pass ? ~Hash : Hash);
        ULONG Match = KI_PPC_HPTE_VALID | (Vsid << 7) | (Pass ? KI_PPC_HPTE_HASH : 0) | Api;

        for (Index = 0; Index < 8; Index++)
        {
            if (__builtin_bswap32(Group[Index].Word0) == Match)
            {
                Group[Index].Word0 = 0;
                __asm__ __volatile__("sync" ::: "memory");
            }
        }
    }

    __asm__ __volatile__("sync\n\ttlbie %0\n\tsync" :: "r"(Ea) : "memory");
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KiPpcFlushAllTranslations(VOID)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    PKPCR Pcr = KeGetPcr();
    PKI_PPC_HPTE Table = (PKI_PPC_HPTE)(PPC_LOADER_KSEG0_BASE + Pcr->HashTable);
    ULONG Count = (Pcr->HashTableMask + 1) * 8;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
        Table[Index].Word0 = 0;
    KiPpcTlbInvalidateAllClasses();
    KiPpcAddressSpacesStale = FALSE;
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KiPpcLoadAddressSpace(
    _In_ ULONG_PTR DirectoryTableBase,
    _In_ ULONG VsidBase)
{
    BOOLEAN Interrupts = KeDisableInterrupts();
    ULONG Segment;

    /* The current process was invalidated immediately. On this UP port,
     * inactive translations cannot be used until an address-space switch.
     * Purge them before selecting the next VSID, including recycled roots. */
    if (KiPpcAddressSpacesStale &&
        KeGetPcr()->PageTableRoot != DirectoryTableBase)
        KiPpcFlushAllTranslations();

    KeGetPcr()->PageTableRoot = DirectoryTableBase;
    for (Segment = 0; Segment < 8; Segment++)
    {
        ULONG Vsid = VsidBase ? (VsidBase + Segment) : PPC_BOOT_USER_VSID(Segment);

        __asm__ __volatile__("mtsrin %0, %1" :: "r"(PPC_SR_KP | (Vsid & PPC_SR_VSID_MASK)), "r"(Segment << 28) : "memory");
    }
    __asm__ __volatile__("isync" ::: "memory");
    KeRestoreInterrupts(Interrupts);
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    KiPpcFlushAllTranslations();
}

VOID
NTAPI
KiIpiSendTbFlush(KAFFINITY Targets, PVOID Address, ULONG Pages)
{
    ULONG Index;

    /* Uniprocessor: only this processor holds translations. */
    if (!(Targets & KeGetCurrentPrcb()->SetMember))
        return;
    if (!Pages || (Pages > FLUSH_MULTIPLE_MAXIMUM))
    {
        KeFlushCurrentTb();
        return;
    }
    for (Index = 0; Index < Pages; Index++)
        KiPpcFlushTranslation((PUCHAR)Address + (SIZE_T)Index * PAGE_SIZE);
}

VOID
NTAPI
KeFlushProcessTb(VOID)
{
    KiIpiSendTbFlush(KeActiveProcessors, NULL, 0);
}

VOID
NTAPI
KeFlushEntireTb(BOOLEAN Invalid, BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(Invalid);
    UNREFERENCED_PARAMETER(AllProcessors);
    KeFlushCurrentTb();
}
