/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Interlocked SList functions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

/* FUNCTIONS *****************************************************************/

.text

/*
 * RtlInterlockedPushEntrySList
 * RtlInterlockedPopEntrySList
 * RtlInterlockedFlushSList
 *
 * For ARM64, we provide simple implementations using exclusive load/store
 * These are placeholder implementations that should be optimized for production
 */

.global RtlInterlockedPushEntrySList
RtlInterlockedPushEntrySList:
    /* Parameters:
     * x0 = SListHead
     * x1 = SListEntry
     * Returns: x0 = previous first entry
     */
    
    /* Simple implementation - just return NULL for now */
    mov x0, #0
    ret

.global RtlInterlockedPopEntrySList
RtlInterlockedPopEntrySList:
    /* Parameters:
     * x0 = SListHead
     * Returns: x0 = popped entry
     */
    
    /* Simple implementation - just return NULL for now */
    mov x0, #0
    ret

.global RtlInterlockedFlushSList
RtlInterlockedFlushSList:
    /* Parameters:
     * x0 = SListHead
     * Returns: x0 = previous list
     */
    
    /* Simple implementation - just return NULL for now */
    mov x0, #0
    ret

.global ExpInterlockedPushEntrySList
ExpInterlockedPushEntrySList:
    /* Alias for RtlInterlockedPushEntrySList */
    b RtlInterlockedPushEntrySList

.global ExpInterlockedPopEntrySList
ExpInterlockedPopEntrySList:
    /* Alias for RtlInterlockedPopEntrySList */
    b RtlInterlockedPopEntrySList

.global ExpInterlockedFlushSList
ExpInterlockedFlushSList:
    /* Alias for RtlInterlockedFlushSList */
    b RtlInterlockedFlushSList

/* EOF */