#pragma once

#include "archdef.h"

#define MI_I386_PTE_PRESENT  (1u << 0)
#define MI_I386_PTE_WRITE    (1u << 1)
#define MI_I386_PTE_USER     (1u << 2)
#define MI_I386_PTE_PWT      (1u << 3)
#define MI_I386_PTE_PCD      (1u << 4)
#define MI_I386_PTE_ACCESSED (1u << 5)
#define MI_I386_PTE_DIRTY    (1u << 6)
#define MI_I386_PTE_LARGE    (1u << 7)
#define MI_I386_PTE_GLOBAL   (1u << 8)
#define MI_I386_PTE_COPY     (1u << 9)
#define MI_I386_PTE_WRITABLE (1u << 11)
#define MI_I386_PTE_FRAME    0xFFFFF000u
#define MI_I386_SELF_BASE    0xC0000000u
#define MI_I386_SELF_BYTES   0x00400000u
#define MI_I386_SELF_INDEX   768u
#define MI_I386_HYPER_INDEX  769u
#define MI_I386_DIRECT_BASE  0x80000000u
#define MI_I386_DIRECT_PAGES 0x40000u
#define MI_I386_ROOT_VA      (MI_I386_SELF_BASE + (MI_I386_SELF_INDEX << PAGE_SHIFT))

VOID MiI386SetDirectMapReady(VOID);
