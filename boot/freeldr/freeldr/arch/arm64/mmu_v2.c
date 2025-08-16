/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Memory Management Unit Support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>

/* Debug support */
#include <debug.h>

/* Page table definitions */
#ifndef PAGE_SIZE
#define PAGE_SIZE           4096
#endif
#ifndef PAGE_MASK
#define PAGE_MASK           (~(PAGE_SIZE - 1))
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT          12
#endif

/* Translation table descriptors */
#define TT_TYPE_MASK        0x3
#define TT_TYPE_INVALID     0x0
#define TT_TYPE_BLOCK       0x1
#define TT_TYPE_TABLE       0x3
#define TT_TYPE_PAGE        0x3

/* Access permissions */
#define TT_AP_RW_EL1        (0x0 << 6)
#define TT_AP_RW_ALL        (0x1 << 6)
#define TT_AP_RO_EL1        (0x2 << 6)
#define TT_AP_RO_ALL        (0x3 << 6)

/* Memory attributes */
#define TT_ATTR_DEVICE      (0x0 << 2)
#define TT_ATTR_NORMAL_NC   (0x1 << 2)
#define TT_ATTR_NORMAL_WT   (0x2 << 2)
#define TT_ATTR_NORMAL_WB   (0x3 << 2)

/* Other flags */
#define TT_AF               (1 << 10)  /* Access flag */
#define TT_nG               (1 << 11)  /* Not global */
#define TT_PXN              (1ULL << 53)  /* Privileged execute never */
#define TT_UXN              (1ULL << 54)  /* User execute never */

/* Level sizes */
#define L0_SIZE             (1ULL << 39)
#define L1_SIZE             (1ULL << 30)
#define L2_SIZE             (1ULL << 21)
#define L3_SIZE             (1ULL << 12)

/* Number of entries per level */
#define TT_ENTRIES          512

/* Translation table entry pointer */
typedef ULONG64 *PPTE_ARM64;

/* Global page tables */
/* Page tables aligned to page size */
static __attribute__((aligned(4096))) ULONG64 PageTableL0[TT_ENTRIES];
static __attribute__((aligned(4096))) ULONG64 PageTableL1[TT_ENTRIES];
static __attribute__((aligned(4096))) ULONG64 PageTableL2[TT_ENTRIES];
static __attribute__((aligned(4096))) ULONG64 PageTableL3[TT_ENTRIES];

/* TCR_EL1 configuration */
#define TCR_T0SZ(x)         ((x) & 0x3F)
#define TCR_IRGN0_WB_RA_WA  (1ULL << 8)
#define TCR_ORGN0_WB_RA_WA  (1ULL << 10)
#define TCR_SH0_INNER       (3ULL << 12)
#define TCR_TG0_4K          (0ULL << 14)
#define TCR_IPS_48BIT       (5ULL << 32)

/* MAIR_EL1 attributes */
#define MAIR_ATTR_DEVICE    0x00
#define MAIR_ATTR_NORMAL_NC 0x44
#define MAIR_ATTR_NORMAL_WT 0xBB
#define MAIR_ATTR_NORMAL_WB 0xFF

/* Initialize MMU */
VOID
MmuInit(VOID)
{
    ULONG64 TCR, MAIR;
    ULONG i;
    
    printf("Initializing ARM64 MMU\n");
    
    /* Clear all page tables */
    RtlZeroMemory(PageTableL0, sizeof(PageTableL0));
    RtlZeroMemory(PageTableL1, sizeof(PageTableL1));
    RtlZeroMemory(PageTableL2, sizeof(PageTableL2));
    RtlZeroMemory(PageTableL3, sizeof(PageTableL3));
    
    /* Setup initial identity mapping for first 4GB */
    /* L0 points to L1 */
    PageTableL0[0] = ((ULONG64)PageTableL1) | TT_TYPE_TABLE;
    
    /* L1 entries - map first 4 1GB blocks */
    for (i = 0; i < 4; i++)
    {
        /* Create 1GB block mappings */
        PageTableL1[i] = (i * L1_SIZE) | TT_TYPE_BLOCK | TT_AP_RW_EL1 | 
                        TT_ATTR_NORMAL_WB | TT_AF;
    }
    
    /* Configure MAIR_EL1 (Memory Attribute Indirection Register) */
    MAIR = (MAIR_ATTR_DEVICE << 0) |     /* Index 0: Device memory */
           (MAIR_ATTR_NORMAL_NC << 8) |  /* Index 1: Normal non-cacheable */
           (MAIR_ATTR_NORMAL_WT << 16) | /* Index 2: Normal write-through */
           (MAIR_ATTR_NORMAL_WB << 24);  /* Index 3: Normal write-back */
    /* TODO: Set MAIR_EL1 register in assembly */
    
    /* Configure TCR_EL1 (Translation Control Register) */
    TCR = TCR_T0SZ(16) |           /* 48-bit virtual addresses */
          TCR_TG0_4K |              /* 4KB granule */
          TCR_SH0_INNER |           /* Inner shareable */
          TCR_ORGN0_WB_RA_WA |      /* Outer write-back read-allocate write-allocate */
          TCR_IRGN0_WB_RA_WA |      /* Inner write-back read-allocate write-allocate */
          TCR_IPS_48BIT;            /* 48-bit intermediate physical address */
    /* TODO: Set TCR_EL1 register in assembly */
    
    /* TODO: Set translation table base register in assembly */
    
    printf("MMU initialized with identity mapping for first 4GB\n");
}

/* Enable MMU */
VOID
MmuEnable(VOID)
{
    ULONG64 SCTLR;
    
    printf("Enabling ARM64 MMU\n");
    
    /* TODO: Enable MMU via assembly code */
    /* This involves setting SCTLR_EL1 register bits */
    SCTLR = (1 << 0) | (1 << 2) | (1 << 12); /* M, C, I bits */
    
    printf("MMU enabled\n");
}

/* Disable MMU */
VOID
MmuDisable(VOID)
{
    ULONG64 SCTLR;
    
    printf("Disabling ARM64 MMU\n");
    
    /* TODO: Disable MMU via assembly code */
    /* This involves clearing SCTLR_EL1 register bits */
    SCTLR = 0; /* Clear M, C, I bits */
    
    printf("MMU disabled\n");
}

/* Map a physical address to virtual address */
BOOLEAN
MmuMapPage(ULONG64 VirtualAddress, ULONG64 PhysicalAddress, ULONG Flags)
{
    ULONG L0Index, L1Index, L2Index, L3Index;
    ULONG64 Descriptor;
    
    /* Calculate indices */
    L0Index = (VirtualAddress >> 39) & 0x1FF;
    L1Index = (VirtualAddress >> 30) & 0x1FF;
    L2Index = (VirtualAddress >> 21) & 0x1FF;
    L3Index = (VirtualAddress >> 12) & 0x1FF;
    
    /* For now, only support mappings in first 512GB (L0[0]) */
    if (L0Index != 0)
    {
        printf("ERROR: Mapping beyond 512GB not supported yet\n");
        return FALSE;
    }
    
    /* Build descriptor */
    Descriptor = PhysicalAddress | TT_TYPE_PAGE | TT_AF;
    
    /* Set access permissions based on flags */
    if (Flags & PAGE_READWRITE)
        Descriptor |= TT_AP_RW_EL1;
    else
        Descriptor |= TT_AP_RO_EL1;
    
    /* Set memory attributes */
    if (Flags & PAGE_NOCACHE)
        Descriptor |= TT_ATTR_DEVICE;
    else
        Descriptor |= TT_ATTR_NORMAL_WB;
    
    /* Set execute permissions */
    if (!(Flags & PAGE_EXECUTE))
        Descriptor |= TT_PXN | TT_UXN;
    
    /* For simplicity, use L1 block mappings for now */
    if ((VirtualAddress & (L1_SIZE - 1)) == 0 && 
        (PhysicalAddress & (L1_SIZE - 1)) == 0)
    {
        /* 1GB block mapping */
        PageTableL1[L1Index] = (PhysicalAddress & ~(L1_SIZE - 1)) | 
                               TT_TYPE_BLOCK | TT_AF | TT_AP_RW_EL1 | 
                               TT_ATTR_NORMAL_WB;
        
        /* TODO: Invalidate TLB for this address */
        /* This should be done via assembly helper */
        
        return TRUE;
    }
    
    /* TODO: Implement finer-grained page mappings */
    printf("ERROR: Fine-grained mappings not implemented yet\n");
    return FALSE;
}