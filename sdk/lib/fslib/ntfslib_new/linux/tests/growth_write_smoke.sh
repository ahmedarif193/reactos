#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfsfix=$5

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-growth.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/growth.ntfs"
resident_original="$workdir/resident-original.bin"
resident_patch="$workdir/resident-patch.bin"
resident_expected="$workdir/resident-expected.bin"
nonresident_original="$workdir/nonresident-original.bin"
nonresident_patch="$workdir/nonresident-patch.bin"
nonresident_expected="$workdir/nonresident-expected.bin"

dd if=/dev/zero bs=127 count=1 status=none |
    tr '\000' R >"$resident_original"
dd if=/dev/zero bs=8192 count=1 status=none |
    tr '\000' P >"$resident_patch"
cp "$resident_original" "$resident_expected"
dd if="$resident_patch" of="$resident_expected" bs=1 seek=127 \
    conv=notrunc status=none

dd if=/dev/zero bs=65536 count=1 status=none |
    tr '\000' A >"$nonresident_original"
dd if=/dev/zero bs=12288 count=1 status=none |
    tr '\000' B >"$nonresident_patch"
cp "$nonresident_original" "$nonresident_expected"
truncate -s 70000 "$nonresident_expected"
dd if="$nonresident_patch" of="$nonresident_expected" bs=1 seek=70000 \
    conv=notrunc status=none

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L GROWTH_TEST "$image"
"$ntfscp" -f -q "$image" "$resident_original" /resident.bin
"$ntfscp" -f -q "$image" "$nonresident_original" /nonresident.bin

cluster_size=$("$driver" --probe "$image" |
    awk '/^cluster size:/ { print $3 }')
free_before=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')

# Force resident-to-nonresident promotion.
"$driver" --write "$image" /resident.bin 127 "$resident_patch"
"$driver" --cat "$image" /resident.bin | cmp - "$resident_expected"
"$ntfscat" "$image" /resident.bin | cmp - "$resident_expected"

# Extend an existing nonresident stream and verify that the gap reads as zero.
"$driver" --write "$image" /nonresident.bin 70000 "$nonresident_patch"
"$driver" --cat "$image" /nonresident.bin | cmp - "$nonresident_expected"
"$ntfscat" "$image" /nonresident.bin | cmp - "$nonresident_expected"

free_after=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
resident_clusters=$(((127 + 8192 + cluster_size - 1) / cluster_size))
old_nonresident_clusters=$(((65536 + cluster_size - 1) / cluster_size))
new_nonresident_clusters=$(((70000 + 12288 + cluster_size - 1) / cluster_size))
expected_used=$((resident_clusters +
    new_nonresident_clusters - old_nonresident_clusters))
test $((free_before - free_after)) -eq "$expected_used"

# Verify the duplicated sizes in the parent's $I30 index, not only the
# attributes stored in each child's MFT record.
list_info=$("$driver" --list-info "$image" /)
resident_info=$(printf '%s\n' "$list_info" |
    awk '$4 == "resident.bin" { print $2 " " $3 }')
nonresident_info=$(printf '%s\n' "$list_info" |
    awk '$4 == "nonresident.bin" { print $2 " " $3 }')
test "$resident_info" = "$((127 + 8192)) $((resident_clusters * cluster_size))"
test "$nonresident_info" = \
    "$((70000 + 12288)) $((new_nonresident_clusters * cluster_size))"

"$ntfsfix" -n "$image" >/dev/null
