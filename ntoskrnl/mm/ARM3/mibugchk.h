/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/mibugchk.h
 * PURPOSE:         ARM3 internal bugcheck subcodes and helpers
 */

#pragma once

/*
 * Internal MEMORY_MANAGEMENT bugcheck subcodes reserved for ARM64 bring-up
 * invariants in ARM3.
 */
#define MI_BUGCHECK_MM_PFN_PROTOTYPE_TRANSITION      0x8888
#define MI_BUGCHECK_MM_PTE_PAGE_TABLE_INVALID        0x61940
#define MI_BUGCHECK_MM_UNLOCK_PAGES_PFN_CORRUPT      0x1236
#define MI_BUGCHECK_MM_DELETEPTE_FORK_CORRUPTION     0x400
#define MI_BUGCHECK_MM_DELETEPTE_PFN_PTE_MISMATCH    0x401

#define MI_BUGCHECK_MM_ARM64_VA_USER_GE_SYSTEM       0xA6400301
#define MI_BUGCHECK_MM_ARM64_VA_SYSTEM_NOT_UPPER     0xA6400302
#define MI_BUGCHECK_MM_ARM64_VA_USER_NOT_LOWER       0xA6400303
#define MI_BUGCHECK_MM_ARM64_VA_USERPROBE_ABOVE_USER 0xA6400304

#define MI_BUGCHECK_ARM64_PFN_SHARECOUNT_INVALID  0xA6400401
#define MI_BUGCHECK_ARM64_ROOT_SHARE_UNDERFLOW    0xA6400402
#define MI_BUGCHECK_ARM64_L1_PARENT_MISMATCH      0xA6400403
#define MI_BUGCHECK_ARM64_ROOT_SHARE_FINAL_MISMATCH 0xA6400404

#define MI_BUGCHECK_ARM64_WS_WALK_FAILED      0xA6400101
#define MI_BUGCHECK_ARM64_WS_PFN_MISMATCH     0xA6400102
#define MI_BUGCHECK_ARM64_WS_ROOT_MISMATCH    0xA6400103
#define MI_BUGCHECK_ARM64_WS_HYPER_MISMATCH   0xA6400104
#define MI_BUGCHECK_ARM64_PFN_INIT_STATE      0xA6400105
#define MI_BUGCHECK_ARM64_WS_PFN_MISSING      0xA6400106

#define MI_BUGCHECK_MM(SubCode, Param1, Param2, Param3)                     \
    KeBugCheckEx(MEMORY_MANAGEMENT,                                          \
                 (ULONG_PTR)(SubCode),                                       \
                 (ULONG_PTR)(Param1),                                        \
                 (ULONG_PTR)(Param2),                                        \
                 (ULONG_PTR)(Param3))
