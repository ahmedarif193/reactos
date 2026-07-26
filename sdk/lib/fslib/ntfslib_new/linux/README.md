# ntfslib_new Linux/FUSE test driver

This directory builds the existing `ntfslib_new` parser natively on Linux and
exposes it as a read-only FUSE 3 filesystem. It also provides image-inspection
and focused mutation commands that do not require FUSE privileges.
This is a dedicated Linux test build and is not added to ReactOS's top-level
CMake targets.

The mounted frontend is intentionally read-only and single-threaded. Normal
inspection opens the image read-only. Explicit mutation probes request write
access only for one operation; use them only on disposable test images because
LFS logging and recovery are not implemented. This is a bring-up/test driver,
not a production replacement for `ntfs3` or `ntfs-3g`.

## Build

Install a C++ compiler, CMake, pkg-config, and the FUSE 3 development package.
For Debian/Ubuntu:

```sh
sudo apt install g++ cmake ninja-build pkg-config libfuse3-dev
cmake -S sdk/lib/fslib/ntfslib_new/linux \
      -B build/ntfslib-linux \
      -G Ninja
cmake --build build/ntfslib-linux
```

With GNU C++, the inspection executable links the compiler support runtime
statically by default. This removes loader-dominated startup from mountless
core measurements; set `-DNTFSLIB_STATIC_CXX_RUNTIME=OFF` to disable it.

If FUSE development files are absent, the same build still produces
`ntfslib_fuse` with all direct image commands; only mounting is disabled.

## Inspect an image without mounting

```sh
build/ntfslib-linux/fuse/ntfslib_fuse --probe disk.ntfs
build/ntfslib-linux/fuse/ntfslib_fuse \
    --volume-bitmap disk.ntfs 13 64
build/ntfslib-linux/fuse/ntfslib_fuse --volume-data disk.ntfs
build/ntfslib-linux/fuse/ntfslib_fuse \
    --mft-record disk.ntfs 0xffff000000000017 >record.bin
build/ntfslib-linux/fuse/ntfslib_fuse \
    --streams disk.ntfs /path/to/file
build/ntfslib-linux/fuse/ntfslib_fuse --list disk.ntfs /
build/ntfslib-linux/fuse/ntfslib_fuse --list-info disk.ntfs /
build/ntfslib-linux/fuse/ntfslib_fuse --cat disk.ntfs /path/to/file
build/ntfslib-linux/fuse/ntfslib_fuse \
    --cat-range disk.ntfs /path/to/file 123 70001
build/ntfslib-linux/fuse/ntfslib_fuse --readlink disk.ntfs /path/to/link
build/ntfslib-linux/fuse/ntfslib_fuse --reparse disk.ntfs /path/to/link
build/ntfslib-linux/fuse/ntfslib_fuse --ea disk.ntfs /path/to/file
build/ntfslib-linux/fuse/ntfslib_fuse \
    --security disk.ntfs /path/to/file
build/ntfslib-linux/fuse/ntfslib_fuse --basic disk.ntfs /path/to/file
build/ntfslib-linux/fuse/ntfslib_fuse \
    --allocated-ranges disk.ntfs /path/to/file 0 1048576
build/ntfslib-linux/fuse/ntfslib_fuse \
    --retrieval-pointers disk.ntfs /path/to/file 0

# Writable probes for disposable images:
build/ntfslib-linux/fuse/ntfslib_fuse --set-label disk.ntfs TEST_LABEL
build/ntfslib-linux/fuse/ntfslib_fuse \
    --set-basic disk.ntfs /path/to/file \
    126256467060000000 - - - 0x23
build/ntfslib-linux/fuse/ntfslib_fuse \
    --set-reparse disk.ntfs /path/to/file reparse-buffer.bin
build/ntfslib-linux/fuse/ntfslib_fuse \
    --delete-reparse disk.ntfs /path/to/file delete-buffer.bin
build/ntfslib-linux/fuse/ntfslib_fuse \
    --set-ea disk.ntfs /path/to/file 0x80 AUTHOR host-value.bin
build/ntfslib-linux/fuse/ntfslib_fuse \
    --remove-ea disk.ntfs /path/to/file AUTHOR
build/ntfslib-linux/fuse/ntfslib_fuse \
    --delete-external-backing disk.ntfs /path/to/file
build/ntfslib-linux/fuse/ntfslib_fuse \
    --write disk.ntfs /path/to/file 4096 host-patch.bin
build/ntfslib-linux/fuse/ntfslib_fuse \
    --truncate disk.ntfs /path/to/file 8192
build/ntfslib-linux/fuse/ntfslib_fuse \
    --allocate disk.ntfs /path/to/file 1048576
build/ntfslib-linux/fuse/ntfslib_fuse \
    --set-sparse disk.ntfs /path/to/file 1
build/ntfslib-linux/fuse/ntfslib_fuse \
    --zero-data disk.ntfs /path/to/file 65536 131072
```

Pass `--show-metadata` before the command to include reserved NTFS metadata
records in directory listings. `--list-info` adds each index entry's allocated
size, packed-EA size, and reparse tag to the normal type, logical-size, and
name fields; it is used to verify duplicated parent `$FILE_NAME` metadata
after growth.
`--reparse` writes the complete validated raw value to standard output.
`--set-reparse` consumes a complete Microsoft or third-party reparse buffer;
`--delete-reparse` consumes its matching zero-data deletion envelope.
These commands exercise the same shared mutation API as the ReactOS reparse
FSCTLs.
`--ea` prints each validated native NTFS EA as its `NEED_EA` marker, ASCII
name, value length, and hexadecimal value. `--set-ea` reads an opaque value
from a host file and accepts flags `0` or `0x80`; `--remove-ea` deletes the
name case-insensitively. These are direct shared-core probes, not a private
FUSE xattr format.
`--security` writes the complete validated self-relative security descriptor
to standard output. `--basic` prints the four raw NT system timestamps and
file attributes. `--set-basic` accepts creation, access, write, change, and
attribute values in that order; `-` preserves a field.
`--write` modifies an existing `$DATA` stream and may extend it, including
resident-to-nonresident promotion, cluster allocation, mapping-pair growth,
zero-filled gaps, and parent-index size synchronization. Existing sparse
streams wholly described by the base MFT record allocate only the holes
intersected by the write and retain all untouched sparse ranges. The command
does not create an absent unnamed stream, but it does create a missing named
stream when its minimal attribute record fits in the base MFT record. If the
file already has an `$ATTRIBUTE_LIST`, the core can instead allocate an
extension MFT record, insert the ordered list entry, and place either a
resident or immediately promoted nonresident stream there. A full base record
can create its initial complete list by relocating a resident named stream or
legacy per-file security descriptor. `--truncate`
changes the logical size in either direction, releases whole tail clusters
when shrinking, and preserves initialized-size zero semantics when growing.
Direct `--cat`, `--write`, `--truncate`, and `--allocate` paths accept native
ADS syntax such as `/file:stream` or `/file:stream:$DATA`. `--allocate`
reserves or releases rounded whole clusters without changing EOF, except that
an allocation request below EOF truncates the stream as NT file-allocation
semantics require. `--set-sparse` marks a stream without allocating or
materializes every hole when clearing sparse state. `--zero-data` uses an
exclusive end offset, does not extend EOF, zeroes partial sparse units, and
releases fully covered units. `--allocated-ranges` reports the coalesced
logical ranges containing physical data within the requested byte range.
`--retrieval-pointers` reports VCN/LCN extents, using `-1` for sparse ranges,
and rounds a starting VCN back to the containing extent. `--volume-bitmap`
prints the byte-aligned starting LCN, remaining valid cluster count, and the
requested prefix of literal `$Bitmap` bytes. `--volume-data` prints the exact
64-bit boot geometry, free clusters, file-record geometry, MFT locations and
valid length, and NTFS version. `--mft-record` ignores the input reference's
sequence component, enumerates downward to the first in-use ordinal, writes
the validated fixup-applied record to standard output, and reports the
returned ordinal and length on standard error.

## Mount through FUSE

```sh
mkdir -p /tmp/ntfslib-mnt
build/ntfslib-linux/fuse/ntfslib_fuse \
    disk.ntfs /tmp/ntfslib-mnt -f

# In another terminal:
ls -la /tmp/ntfslib-mnt
fusermount3 -u /tmp/ntfslib-mnt
```

The frontend adds FUSE's `-s` and `-o ro` options automatically. Because the
backing image is immutable for the lifetime of the mount, it also enables
kernel page caching and extended entry/attribute cache lifetimes. A block
device can be supplied instead of an image when the invoking user has read
access.

## Smoke test

With `mkntfs` and `ntfscp` installed, CTest creates a temporary 64 MiB NTFS
image and verifies probe, UTF-8/case-insensitive root enumeration, and
byte-for-byte reading of resident, nonresident, and empty files:

```sh
ctest --test-dir build/ntfslib-linux --output-on-failure
```

The smoke test also creates an uninitialized sparse stream and a 1,000-entry
directory. The latter forces `$INDEX_ROOT` into an extension MFT record and
checks full multi-level `$I30` traversal. Separate fresh-image tests exercise
resident label growth/shrink and a nonresident write crossing cluster
boundaries, compare the results with NTFS-3G tools, and run `ntfsfix -n`.
Growth fixtures also force resident promotion, extend nonresident data across
a zero-filled gap, verify exact `$Bitmap` consumption and parent `$I30` sizes,
and repeat promotion/extension for named streams. Additional fixtures compare
the resulting alternate streams with `ntfscat`, including newly inserted
resident and nonresident streams. The same compact ADS fixture forces NTFS-3G
to create an `$ATTRIBUTE_LIST` and data-stream attributes in extension MFT
records, then checks all 30 enumerated stream names and sizes. It also modifies
and resizes a resident extension stream, promotes a resident stream retained
in the base record without changing its existing list identity, patches an
already-allocated nonresident extension stream, forces base- and
extension-owned mapping-pair continuations, has NTFS-3G fragment an 8 KiB list
across two physical runs, then grows and copy-on-write compacts it into one
12 KiB run. Literal `$Bitmap` and retrieval-pointer checks prove that old
clusters are freed or legitimately reused. The same case crosses an
MFT-record allocation boundary on its first attempt and verifies base-record
timestamps. A focused size-change fixture checks
resident shrink/promotion, nonresident shrink/regrowth/zero demotion, exact
cluster release, independent preallocation, and parent-index sizes. Security
fixtures then compare legacy per-file and centralized `$Secure` descriptors
with `ntfscat` and `ntfssecaudit`. A basic-information fixture verifies raw
readback plus independent `ntfsinfo` interpretation of all four timestamps
and user-settable attributes. The same focused fixture verifies that data
writes advance last-write/change while allocation-only changes advance only
change time. One native-EA fixture covers set, case-insensitive replacement,
resident promotion, `NEED_EA`, last-entry deletion, exact cluster release, and
parent packed-size synchronization; `ntfscat`, `ntfsinfo`, and `ntfsfix`
independently inspect the result. A native-reparse fixture similarly covers
resident symlink replacement, mismatched tags and GUIDs, EA conflicts, large
nonresident values, exact allocation recovery, and independent raw/metadata
inspection.
A WOF fixture independently compresses a mixed-pattern stream with libwim and
checks transparent reads of FILE-provider XPRESS4K, XPRESS8K, XPRESS16K, and
LZX32K data. Every format covers both compressed and raw chunks, an unaligned
range read, exact output bytes, and rejection of an out-of-range chunk table.
It also verifies automatic materialization before a content write, explicit
external-backing deletion, exact cluster reclamation, removal of the reparse
point and `WofCompressedData` stream, independent `ntfscat`/`ntfsinfo`
interpretation, and non-mutation when the backing table is corrupt.
A sparse fixture lets NTFS-3G independently create a zero-allocation stream,
then checks unaligned writes, EOF shrink/regrowth, ordinary/sparse header
transitions, exact allocated-range queries, sparse marking and clearing,
partial-unit zeroing, full-unit hole punching, and all-hole conversion. It
verifies exact logical bytes, cluster deltas, virtual/physical sizes, mapping
flags, and volume consistency through `ntfscat`, `ntfsinfo`, and `ntfsfix`.
It also matches retrieval pointers with `ntfscluster` and volume-bitmap bytes
with NTFS-3G's independent raw metadata-stream reader. The main image fixture
also matches volume geometry/free space with boot-sector and `ntfsinfo`
values, derives downward MFT enumeration from `$MFT::$BITMAP`, and compares
fixup-applied records against `ntfscat` while checking their physical
update-sequence trailers.

## Compare with ntfs-3g

Core comparisons use the mountless commands against `ntfsls` and `ntfscat`,
with names and bytes checked before timing. The current compact result and
exact workload are recorded in `../ROADMAP.md`.

A separate frontend-only runner remains available when FUSE behavior itself
is the subject of a test. It creates one image, mounts it read-only through
both drivers, validates entry and byte equivalence, and measures the mounted
paths:

```sh
sdk/lib/fslib/ntfslib_new/linux/tests/benchmark_fuse.sh \
    build/ntfslib-linux/fuse/ntfslib_fuse \
    build/ntfslib-linux/ntfslib_posix_bench
```

Tune the workload with `NTFSLIB_SMALL_FILE_COUNT`,
`NTFSLIB_LIST_ITERATIONS`, `NTFSLIB_READ_ITERATIONS`, and
`NTFSLIB_READ_SIZE_MIB`. Results are CSV and include the compared entry/byte
counts. Each measured operation gets an untimed warm-up pass, and a partial or
incorrect result fails instead of being reported as fast.

## Current limits

- One backing image/device per process; the older environment contract still
  owns process-global disk state.
- Existing ordinary unnamed and named `$DATA` streams can grow through
  resident promotion and nonresident cluster allocation, and can shrink with
  tail-cluster release or reserve allocation independently of EOF. Missing
  named streams can be inserted in the base record or in a newly allocated
  extension record. The core can create an initial list for a full base when
  it can relocate a resident named stream or legacy security descriptor; an
  uncommon record with neither enough header room nor a safely movable
  attribute still cannot be transformed. Unnamed stream/file creation,
  directory-index growth, and directory mutation remain unimplemented. An
  existing list grows transactionally when publishing stream-mapping
  continuation entries. Fragmented list storage is preferentially relocated
  copy-on-write into one complete run before its base-record mapping pairs
  become a limit; the previous clusters remain owned until the replacement is
  durable, and bounded incremental growth is the fallback when a contiguous
  replacement is unavailable.
- An all-`0xff` `$LogFile` is recognized as NTFS's empty/clean form, but there
  is no journal replay, transaction logging, crash recovery, or safe writable
  mount.
- Growth synchronizes duplicated local and parent-index `$FILE_NAME` sizes,
  but those metadata writes are not crash-atomic until LFS transactions exist.
- Native NTFS/LZNT1-compressed and encrypted stream writes remain unsupported
  even where the corresponding read path exists. Base-record sparse streams
  support bounded writes, EOF changes, exact physical allocation, sparse
  mark/clear, zero-range hole punching, and allocated-range queries. Sparse
  scalar preallocation and `$ATTRIBUTE_LIST` extents remain unsupported. WOF
  FILE-provider files are
  materialized to ordinary `$DATA` before unnamed writes, truncation, or
  allocation changes; their external metadata and old clusters are then
  removed. Explicit basic timestamp and attribute updates work. Ordinary
  data/EOF changes automatically maintain last-write/change, allocation-only
  changes maintain change, and ReactOS writable reads maintain last-access.
  ReactOS also honors per-handle `-1` suppression and `-2` resume state for
  the three automatic timestamp fields.
- `$ATTRIBUTE_LIST` lookup validates extension records and merges ordered,
  contiguous nonresident attribute segments into one cached logical run list.
  NTFS requires the mapping pairs for `$ATTRIBUTE_LIST` itself to remain in
  the base MFT record; the core now validates complete base-run coverage
  instead of accepting a recursive, unresolvable extent layout. Continuation
  publication can allocate more list clusters transactionally, compact a
  fragmented list copy-on-write, and releases replacement clusters again if
  the multi-record update aborts.
- Logical `$DATA` stream enumeration is shared by the Linux inspector and
  ReactOS `FileStreamInformation`. It includes named streams stored in
  `$ATTRIBUTE_LIST` extension records, reports physical allocation separately
  from logical size, and validates each referenced record and attribute before
  exposing it. Existing resident extension streams can be modified, resized,
  or promoted. Resident streams retained in the base record can likewise
  promote without changing their existing list entry. Nonresident extension
  streams support in-place writes, allocation growth/shrink, EOF changes, and
  independent preallocation while the rewritten mapping pairs still fit their
  owner record. New named streams can allocate an extension MFT record and
  promote there to nonresident data. Base- and extension-owned streams can
  split oversized mapping pairs into staged extension records, replace those
  continuations on later growth/shrink, and collapse them back into their
  VCN-zero owner, including zero-length resident demotion.
- Retrieval-pointer queries cover ordinary, sparse, compressed,
  `$ATTRIBUTE_LIST`-spanned, ADS, and nonresident directory streams. Volume
  bitmap queries return literal `$Bitmap` state. ReactOS exposes both through
  `FSCTL_GET_RETRIEVAL_POINTERS` and `FSCTL_GET_VOLUME_BITMAP`.
- Exact 64-bit volume geometry and downward, in-use MFT-record enumeration are
  shared core operations. ReactOS exposes them through
  `FSCTL_GET_NTFS_VOLUME_DATA` and `FSCTL_GET_NTFS_FILE_RECORD`; the latter
  returns only records that passed signature, bounds, attribute-layout, and
  update-sequence validation.
- Ordinary NTFS/LZNT1-compressed streams, sparse holes, uninitialized tails,
  and WOF FILE-provider XPRESS4K/8K/16K and LZX32K streams are decoded by the
  shared core. WOF FILE-provider writes restore a regular file first. EFS
  encrypted streams, WOF WIM-provider data, cloud placeholders, and other
  filter-owned payload formats remain unsupported.
- Native reparse buffers are validated and mutable through the shared core,
  including resident/nonresident transitions, tag/GUID replacement rules, and
  duplicated parent metadata. ReactOS serves get/set/delete reparse FSCTLs.
  Symbolic-link and mount-point buffers are exposed as links; other tags remain
  available through the raw API but need tag-specific consumers. ReactOS
  create-time `STATUS_REPARSE` traversal remains unimplemented.
- Native `$EA` / `$EA_INFORMATION` pairs are validated, readable, and mutable
  through the shared core. ReactOS serves query/set EA IRPs and advertises the
  feature. Mutation is copy-on-write for the data pair and releases old runs
  only after the new MFT record is durable, but files needing
  `$ATTRIBUTE_LIST` extent allocation remain unsupported and multi-record
  crash atomicity still depends on future LFS transactions.
- Legacy per-file and NTFS 3.x centralized security descriptors are validated
  and readable through the shared core; ReactOS serves the requested security
  components through `IRP_MJ_QUERY_SECURITY`. Creating descriptors and
  transactionally updating `$Secure` indexes remain unimplemented.
- Exact pathname lookup descends `$I30` lazily and validates only the selected
  child buffers. Full directory enumeration intentionally retains the
  sequential bulk-read tree path.

These limitations mirror the remaining unchecked items in `../ROADMAP.md`.
