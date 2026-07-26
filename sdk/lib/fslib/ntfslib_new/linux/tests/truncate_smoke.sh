#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfsfix=$5

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-truncate.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/truncate.ntfs"
resident_original="$workdir/resident-original.bin"
resident_shrunk="$workdir/resident-shrunk.bin"
resident_expected="$workdir/resident-expected.bin"
nonresident_original="$workdir/nonresident-original.bin"
nonresident_shrunk="$workdir/nonresident-shrunk.bin"
nonresident_regrown="$workdir/nonresident-regrown.bin"
prealloc_original="$workdir/prealloc-original.bin"
prealloc_truncated="$workdir/prealloc-truncated.bin"
empty="$workdir/empty.bin"

dd if=/dev/zero bs=127 count=1 status=none |
    tr '\000' R >"$resident_original"
dd if="$resident_original" of="$resident_shrunk" \
    bs=1 count=31 status=none
cp "$resident_shrunk" "$resident_expected"
truncate -s 9000 "$resident_expected"

dd if=/dev/zero bs=65536 count=1 status=none |
    tr '\000' A >"$nonresident_original"
dd if="$nonresident_original" of="$nonresident_shrunk" \
    bs=1 count=5000 status=none
cp "$nonresident_shrunk" "$nonresident_regrown"
truncate -s 12000 "$nonresident_regrown"
dd if=/dev/zero bs=63 count=1 status=none |
    tr '\000' Q >"$prealloc_original"
dd if="$prealloc_original" of="$prealloc_truncated" \
    bs=1 count=20 status=none
truncate -s 0 "$empty"

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L TRUNCATE_TEST "$image"
"$ntfscp" -f -q "$image" "$resident_original" /resident.bin
"$ntfscp" -f -q "$image" "$nonresident_original" /nonresident.bin
"$ntfscp" -f -q "$image" "$prealloc_original" /prealloc.bin

cluster_size=$("$driver" --probe "$image" |
    awk '/^cluster size:/ { print $3 }')
free_before=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
resident_clusters=$(((9000 + cluster_size - 1) / cluster_size))
old_nonresident_clusters=$(((65536 + cluster_size - 1) / cluster_size))

# Shrink resident data, then grow it beyond the MFT record without writing
# payload bytes. The new logical tail must read as zero.
"$driver" --truncate "$image" /resident.bin 31
"$driver" --cat "$image" /resident.bin | cmp - "$resident_shrunk"
"$driver" --truncate "$image" /resident.bin 9000
"$driver" --cat "$image" /resident.bin | cmp - "$resident_expected"
"$ntfscat" "$image" /resident.bin | cmp - "$resident_expected"

# Shorten mapping pairs and release whole tail clusters, then truncate to zero
# and convert the empty stream back to resident form.
"$driver" --truncate "$image" /nonresident.bin 5000
"$driver" --cat "$image" /nonresident.bin | cmp - "$nonresident_shrunk"
"$ntfscat" "$image" /nonresident.bin | cmp - "$nonresident_shrunk"
"$driver" --truncate "$image" /nonresident.bin 12000
"$driver" --cat "$image" /nonresident.bin | cmp - "$nonresident_regrown"
"$ntfscat" "$image" /nonresident.bin | cmp - "$nonresident_regrown"
"$driver" --truncate "$image" /nonresident.bin 0
"$driver" --cat "$image" /nonresident.bin | cmp - "$empty"
"$ntfscat" "$image" /nonresident.bin | cmp - "$empty"

# Reserve clusters without changing EOF, then release excess preallocation.
"$driver" --allocate "$image" /prealloc.bin 16384
free_preallocated=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test $((free_before - free_preallocated)) -eq \
    $((resident_clusters + 4 - old_nonresident_clusters))
"$driver" --cat "$image" /prealloc.bin | cmp - "$prealloc_original"
"$ntfscat" "$image" /prealloc.bin | cmp - "$prealloc_original"
prealloc_info=$("$driver" --list-info "$image" / |
    awk '$4 == "prealloc.bin" { print $2 " " $3 }')
test "$prealloc_info" = "63 16384"

"$driver" --allocate "$image" /prealloc.bin 4096
"$driver" --allocate "$image" /prealloc.bin 20
"$driver" --cat "$image" /prealloc.bin | cmp - "$prealloc_truncated"
"$ntfscat" "$image" /prealloc.bin | cmp - "$prealloc_truncated"

free_after=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test $((free_after - free_before)) -eq \
    $((old_nonresident_clusters - resident_clusters - 1))

list_info=$("$driver" --list-info "$image" /)
resident_info=$(printf '%s\n' "$list_info" |
    awk '$4 == "resident.bin" { print $2 " " $3 }')
nonresident_info=$(printf '%s\n' "$list_info" |
    awk '$4 == "nonresident.bin" { print $2 " " $3 }')
prealloc_info=$(printf '%s\n' "$list_info" |
    awk '$4 == "prealloc.bin" { print $2 " " $3 }')
test "$resident_info" = \
    "9000 $((resident_clusters * cluster_size))"
test "$nonresident_info" = "0 0"
test "$prealloc_info" = "20 $cluster_size"

"$ntfsfix" -n "$image" >/dev/null
