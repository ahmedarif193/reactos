# ReactOS ARM64 heap corruption — root-cause + fix task

File: `sdk/lib/rtl/heap.c`. Target: ReactOS ARM64 (this fork), debug build. Repro: loading SwiftShader `vk_swiftshader.dll` (42 MB C++ ICD, heavy ucrtbase malloc/free/calloc over the serialized process heap at base `0x40120000`). Repro is on `-smp 1` (single core) — NOT an SMP/memory-ordering bug. ~100% within ~11 s of boot.

## Symptom
Assert `*FreeSize == NextEntry->PreviousSize` in `RtlpCoalesceFreeBlocks` (the forward-coalesce back-link check). A live BUSY block's 16-byte header gets fully **zeroed**, so a neighbor freed later forward-coalesces into what it thinks is a free block and the PreviousSize link is inconsistent. On runs that dodge the assert: user-mode translation faults (DFSC=0x7, page unmapped) on heap-metadata addresses from both the app and ntdll's free-list walker → a decommitted page under a live pointer.

## Instrumentation already in heap.c (all under `#if DBG`, keyed to heap `0x40120000` / sentinel addr `0x401403C8`, process filtered to the image whose name contains "vulkan1")
- `RtlpHeapSentinelCheck` (alloc/free/realloc in+out): watches user-data qword `0x401403C8`, prints when it becomes 0, with a 16-slot ring of the last blocks whose range covered it (`RtlpHeapSentinelCover` on every alloc/free/realloc).
- `RtlpHeapSentinelModule`: resolves an RA to a loaded module+offset via PEB loader list.
- Coalesce mismatch dump + self-heal at both `RtlpCoalesceFreeBlocks` back-link checks.
- DECOMMIT dump in `RtlpDeCommitFreeBlock` (prints range, FreeEntry, Size param, `FreeEntry->Size`, **`FreeEntry->Flags`**, PreviousSize, and whether the last-committed branch was taken; then walks entries inside the doomed range).
- COMMIT dump in `RtlpFindAndCommitPages`.
Leave this instrumentation in place while debugging; it's how we see the state. Strip it only in the final cleanup commit.

## KEY EVIDENCE (captured from serial)

1. **`RtlpDeCommitFreeBlock` is sometimes called with a BUSY block.** Most DECOMMIT lines show `flags=00`; several show **`flags=01` = HEAP_ENTRY_BUSY**:
```
DECOMMIT [40140000..40141000) free=4013FFF0 szParam=19b szField=19b flags=01 prev=f  lastBranch=0
DECOMMIT [40140000..40142000) free=4013F000 szParam=39a szField=39a flags=01 prev=0  lastBranch=0
DECOMMIT [40140000..40142000) free=4013F610 szParam=302 szField=302 flags=01 prev=4  lastBranch=0   <-- the corrupting one
```
`szField` == `FreeEntry->Size`, so Size field is intact; only `Flags` still has BUSY set.

2. **The corrupting sequence** (sentinel ring, oldest→newest):
```
ring[13] alloc-nd entry=4013F610 units=302 flags=01 ra=ucrtbase!_calloc_base+0x80
ring[14] free     entry=4013F610 units=302 flags=01 ra=ucrtbase!_free_base+0x28
ring[15] alloc    entry=401403C0 units=5   flags=01 ra=ucrtbase!_malloc_base+0x4c
--> then: sentinel zeroed (header at 401403C0 is now 0000 0000 0000 0000)
```
`free(4013F610)` (12 KB, size 0x302) triggers the `flags=01` DECOMMIT of `[40140000..40142000)`. Block `401403C0` (allocated right after) lives INSIDE that decommit range. Page `0x40140000` is being **commit/decommit thrashed** continuously (dozens of `COMMIT addr=40140000 ... DECOMMIT [40140000..` pairs in the log).

3. Writers are plain `_malloc_base`/`_free_base`/`_calloc_base` (NOT `_aligned_malloc`) — so ucrt aligned-header math is NOT involved.

## HYPOTHESIS (verify or refute against source — don't assume I'm right)
`RtlFreeHeap`'s no-coalesce path passes the freed block to `RtlpDeCommitFreeBlock` (and/or `RtlpInsertFreeBlock`) with `HEAP_ENTRY_BUSY` still set (only the coalesce path clears it, by adopting the free neighbor's flags — hence flags=00 when coalescing happened, flags=01 when it didn't). `RtlpDeCommitFreeBlock`'s guard-entry / remainder-reinsert / UCR bookkeeping may then be wrong for a BUSY-flagged input (e.g. `FreeEntry->Flags &= ~HEAP_ENTRY_LAST_ENTRY` leaves BUSY set on a reinserted "free" remainder; a remainder block carrying BUSY is treated as in-use by a later coalesce and its size/links go stale), so a page still holding — or about to be reused for — a live allocation (`401403C0`) gets decommitted/recommitted zero-filled, or a stale oversized free entry overlaps the new small allocation.

## YOUR TASK
1. Trace `RtlFreeHeap` precisely: does the block passed to `RtlpDeCommitFreeBlock` / `RtlpInsertFreeBlock` still have `HEAP_ENTRY_BUSY` set in the no-coalesce path? Quote the exact lines. Compare to how the coalesce path clears it.
2. Determine whether `RtlpDeCommitFreeBlock` and the remainder-reinsert paths are correct when the input has BUSY set, and whether the guard-entry `PreviousSize`/`Size` bookkeeping for the trailing remainder (the entry AFTER `DecommitEnd`) updates the *following committed entry*'s PreviousSize. Cross-check against Windows/Wine NT heap behavior if you know it.
3. Also check: after decommit re-inserts remainders, is there any path where the SAME physical page can be handed out by `RtlpFindAndCommitPages` (COMMIT) while a free-list entry or a neighbor's forward link still references an entry inside it.
4. Propose a **minimal, source-level fix** (most likely: clear `HEAP_ENTRY_BUSY` on the block before decommit/insert in `RtlFreeHeap`, OR fix the remainder/guard bookkeeping in `RtlpDeCommitFreeBlock`). It must not regress x86/x64 (this heap.c is shared) and must keep Win11/NT parity — no invented fields, no behavioral hacks. Explain WHY it fixes the zeroing, tied to the evidence above.

Do the analysis first and show me the exact lines + your reasoning BEFORE editing. I will review every diff and build/test it myself in QEMU. Do not run builds or QEMU yourself.
