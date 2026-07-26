#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfs3g=$3
fusermount3=$4
ntfscat=$5
ntfsinfo=$6
ntfsfix=$7
ntfscluster=$8

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-sparse.XXXXXX")
mountpoint="$workdir/mnt"
image="$workdir/sparse.ntfs"
original="$workdir/original.bin"
patch_a="$workdir/patch-a.bin"
patch_b="$workdir/patch-b.bin"
patch_c="$workdir/patch-c.bin"
expected="$workdir/expected.bin"
mounted=0

# Some hosts confine fusermount3 with an AppArmor profile that rejects
# TMPDIR mountpoints; util-linux umount stays available for the fixture.
unmount_fixture()
{
    "$fusermount3" -u "$mountpoint" 2>/dev/null ||
        umount "$mountpoint"
}

cleanup()
{
    if test "$mounted" -eq 1; then
        unmount_fixture >/dev/null 2>&1 || true
    fi
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM

mkdir "$mountpoint"
truncate -s 100123 "$original"
truncate -s 128M "$image"
"$mkntfs" -F -Q -q -L SPARSE_TEST "$image"

# Let NTFS-3G create the sparse attribute and mapping pairs independently.
if ! "$ntfs3g" "$image" "$mountpoint" -o permissions; then
    exit 77
fi
mounted=1
cp --sparse=always "$original" "$mountpoint/sparse.bin"
sync "$mountpoint/sparse.bin"
unmount_fixture
mounted=0

dd if=/dev/zero bs=8195 count=1 status=none |
    tr '\000' A >"$patch_a"
dd if=/dev/zero bs=5000 count=1 status=none |
    tr '\000' B >"$patch_b"
dd if=/dev/zero bs=3000 count=1 status=none |
    tr '\000' C >"$patch_c"
cp "$original" "$expected"
dd if="$patch_a" of="$expected" bs=1 seek=4093 \
    conv=notrunc status=none
dd if="$patch_b" of="$expected" bs=1 seek=110000 \
    conv=notrunc status=none

free_before=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
clusters=$("$driver" --probe "$image" |
    awk '/^clusters:/ { print $2 }')

# The shared volume-bitmap API rounds to a byte boundary and returns the
# literal $Bitmap bits. Compare a bounded prefix with NTFS-3G's independent
# metadata-stream reader.
bitmap_hex=$("$ntfscat" -i 6 "$image" 2>/dev/null |
    dd bs=1 skip=1 count=4 status=none |
    od -An -tx1 |
    tr -d ' \n')
bitmap=$("$driver" --volume-bitmap "$image" 13 4)
test "$bitmap" = "8 $((clusters - 8)) $bitmap_hex"

# The first write allocates clusters 0..2; the extending write allocates
# clusters 26..28 while preserving the intervening hole.
"$driver" --write "$image" /sparse.bin 4093 "$patch_a"
"$driver" --write "$image" /sparse.bin 110000 "$patch_b"

"$driver" --cat "$image" /sparse.bin | cmp - "$expected"
"$ntfscat" "$image" /sparse.bin | cmp - "$expected"

free_after=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test $((free_before - free_after)) -eq 6

list_info=$("$driver" --list-info "$image" /)
sparse_info=$(printf '%s\n' "$list_info" |
    awk '$4 == "sparse.bin" { print $2 " " $3 }')
test "$sparse_info" = "115000 24576"

data_info=$("$ntfsinfo" -F /sparse.bin "$image" |
    sed -n '/Dumping attribute \$DATA/,/End of inode reached/p')
printf '%s\n' "$data_info" |
    grep -Eq 'Attribute flags:[[:space:]]+0x8000'
printf '%s\n' "$data_info" |
    grep -Eq 'Compression unit:[[:space:]]+4 '
printf '%s\n' "$data_info" |
    grep -Eq 'Data size:[[:space:]]+115000 '
printf '%s\n' "$data_info" |
    grep -Eq 'Allocated size:[[:space:]]+118784 '
printf '%s\n' "$data_info" |
    grep -Eq 'Initialized size:[[:space:]]+115000 '
printf '%s\n' "$data_info" |
    grep -Eq 'Compressed size:[[:space:]]+24576 '

# Retrieval pointers expose the same VCN/LCN runlist as NTFS-3G. A request
# inside the first run is rounded back to that extent's starting VCN.
expected_pointers=$(
    printf 'start 0\n'
    "$ntfscluster" -f -F /sparse.bin "$image" 2>/dev/null |
        awk '$1 ~ /^-?[0-9]+$/ &&
             $2 ~ /^-?[0-9]+$/ &&
             $3 ~ /^[0-9]+$/ {
                 print $1 + $3, $2
             }'
)
pointers=$("$driver" --retrieval-pointers \
    "$image" /sparse.bin 1)
test "$pointers" = "$expected_pointers"

# Shrinking past four physical tail clusters releases them. With no hole left
# in the retained two-cluster mapping, NTFS uses the compact ordinary
# nonresident header while the file itself remains marked sparse.
"$driver" --truncate "$image" /sparse.bin 5000
truncate -s 5000 "$expected"
"$driver" --cat "$image" /sparse.bin | cmp - "$expected"
"$ntfscat" "$image" /sparse.bin | cmp - "$expected"

free_shrunk=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test $((free_before - free_shrunk)) -eq 2

data_info=$("$ntfsinfo" -F /sparse.bin "$image" |
    sed -n '/Dumping attribute \$DATA/,/End of inode reached/p')
printf '%s\n' "$data_info" |
    grep -Eq 'Resident:[[:space:]]+No'
printf '%s\n' "$data_info" |
    grep -Eq 'Attribute flags:[[:space:]]+0x0000'
printf '%s\n' "$data_info" |
    grep -Eq 'Data size:[[:space:]]+5000 '
printf '%s\n' "$data_info" |
    grep -Eq 'Allocated size:[[:space:]]+8192 '
printf '%s\n' "$data_info" |
    grep -Eq 'Initialized size:[[:space:]]+5000 '
if printf '%s\n' "$data_info" | grep -q 'Compressed size:'; then
    exit 1
fi
"$ntfsinfo" -F /sparse.bin "$image" |
    grep -Eq 'File attributes:.*SPARSE_FILE'

# EOF growth appends a logical hole and does not consume a cluster.
"$driver" --truncate "$image" /sparse.bin 70000
truncate -s 70000 "$expected"
"$driver" --cat "$image" /sparse.bin | cmp - "$expected"
"$ntfscat" "$image" /sparse.bin | cmp - "$expected"

free_regrown=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_regrown" -eq "$free_shrunk"

data_info=$("$ntfsinfo" -F /sparse.bin "$image" |
    sed -n '/Dumping attribute \$DATA/,/End of inode reached/p')
printf '%s\n' "$data_info" |
    grep -Eq 'Attribute flags:[[:space:]]+0x8000'
printf '%s\n' "$data_info" |
    grep -Eq 'Compression unit:[[:space:]]+4 '
printf '%s\n' "$data_info" |
    grep -Eq 'Data size:[[:space:]]+70000 '
printf '%s\n' "$data_info" |
    grep -Eq 'Allocated size:[[:space:]]+73728 '
printf '%s\n' "$data_info" |
    grep -Eq 'Initialized size:[[:space:]]+5000 '
printf '%s\n' "$data_info" |
    grep -Eq 'Compressed size:[[:space:]]+8192 '

# Exercise a write after another shrink has removed the sparse attribute
# header. The standard sparse-file flag must still select sparse extension:
# only the two clusters touched by this write may be allocated.
"$driver" --truncate "$image" /sparse.bin 5000
truncate -s 5000 "$expected"
"$driver" --write "$image" /sparse.bin 68000 "$patch_c"
dd if="$patch_c" of="$expected" bs=1 seek=68000 \
    conv=notrunc status=none
"$driver" --cat "$image" /sparse.bin | cmp - "$expected"
"$ntfscat" "$image" /sparse.bin | cmp - "$expected"

free_rewritten=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test $((free_before - free_rewritten)) -eq 4

# Allocated-range queries coalesce logically adjacent physical fragments and
# bias their first/last entries to the exact byte range requested.
ranges=$("$driver" --allocated-ranges \
    "$image" /sparse.bin 0 71000)
test "$ranges" = "$(printf '0 8192\n65536 5464')"

# A full 64-KiB sparse unit can be punched without changing EOF. The leading
# two physical clusters become a hole while the short trailing allocation is
# retained and both readers must observe zeros in the released range.
"$driver" --zero-data "$image" /sparse.bin 0 65536
dd if=/dev/zero of="$expected" bs=1 count=65536 \
    conv=notrunc status=none
"$driver" --cat "$image" /sparse.bin | cmp - "$expected"
"$ntfscat" "$image" /sparse.bin | cmp - "$expected"

free_punched=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test $((free_before - free_punched)) -eq 2
ranges=$("$driver" --allocated-ranges \
    "$image" /sparse.bin 0 71000)
test "$ranges" = "65536 5464"

# Clearing sparse fully allocates every hole before removing the stream and
# file flags. Query semantics then echo the complete requested range, even
# when it extends beyond EOF.
"$driver" --set-sparse "$image" /sparse.bin 0
free_unsparse=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test $((free_before - free_unsparse)) -eq 18
ranges=$("$driver" --allocated-ranges \
    "$image" /sparse.bin 123 90000)
test "$ranges" = "123 90000"

data_info=$("$ntfsinfo" -F /sparse.bin "$image" |
    sed -n '/Dumping attribute \$DATA/,/End of inode reached/p')
printf '%s\n' "$data_info" |
    grep -Eq 'Attribute flags:[[:space:]]+0x0000'
printf '%s\n' "$data_info" |
    grep -Eq 'Allocated size:[[:space:]]+73728 '
if printf '%s\n' "$data_info" | grep -q 'Compressed size:'; then
    exit 1
fi
if "$ntfsinfo" -F /sparse.bin "$image" |
    grep -Eq 'File attributes:.*SPARSE_FILE'; then
    exit 1
fi

# Marking an existing file sparse is metadata-only. It consumes no clusters,
# and sparse query semantics expose allocation only through the virtual
# allocation boundary.
"$driver" --set-sparse "$image" /sparse.bin 1
free_remarked=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_remarked" -eq "$free_unsparse"
ranges=$("$driver" --allocated-ranges \
    "$image" /sparse.bin 0 90000)
test "$ranges" = "0 73728"
"$ntfsinfo" -F /sparse.bin "$image" |
    grep -Eq 'File attributes:.*SPARSE_FILE'

# A range reaching EOF may release its short final sparse unit. Punching the
# complete file leaves a valid all-hole nonresident stream and recovers all
# physical clusters without changing the 71000-byte EOF.
"$driver" --zero-data "$image" /sparse.bin 0 71000
truncate -s 0 "$expected"
truncate -s 71000 "$expected"
"$driver" --cat "$image" /sparse.bin | cmp - "$expected"
"$ntfscat" "$image" /sparse.bin | cmp - "$expected"

free_all_holes=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_all_holes" -eq "$free_before"
ranges=$("$driver" --allocated-ranges \
    "$image" /sparse.bin 0 71000)
test -z "$ranges"

data_info=$("$ntfsinfo" -F /sparse.bin "$image" |
    sed -n '/Dumping attribute \$DATA/,/End of inode reached/p')
printf '%s\n' "$data_info" |
    grep -Eq 'Attribute flags:[[:space:]]+0x8000'
printf '%s\n' "$data_info" |
    grep -Eq 'Compression unit:[[:space:]]+4 '
printf '%s\n' "$data_info" |
    grep -Eq 'Data size:[[:space:]]+71000 '
printf '%s\n' "$data_info" |
    grep -Eq 'Allocated size:[[:space:]]+73728 '
printf '%s\n' "$data_info" |
    grep -Eq 'Initialized size:[[:space:]]+71000 '
printf '%s\n' "$data_info" |
    grep -Eq 'Compressed size:[[:space:]]+0 '
pointers=$("$driver" --retrieval-pointers \
    "$image" /sparse.bin 5)
test "$pointers" = "$(printf 'start 0\n18 -1')"

# Truncation to zero returns the stream to a resident empty value, releases
# every physical cluster, and clears the file's sparse flag.
"$driver" --truncate "$image" /sparse.bin 0
truncate -s 0 "$expected"
"$driver" --cat "$image" /sparse.bin | cmp - "$expected"
"$ntfscat" "$image" /sparse.bin | cmp - "$expected"

free_zero=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_zero" -eq "$free_before"

data_info=$("$ntfsinfo" -F /sparse.bin "$image" |
    sed -n '/Dumping attribute \$DATA/,/End of inode reached/p')
printf '%s\n' "$data_info" |
    grep -Eq 'Resident:[[:space:]]+Yes'
printf '%s\n' "$data_info" |
    grep -Eq 'Attribute flags:[[:space:]]+0x0000'
printf '%s\n' "$data_info" |
    grep -Eq 'Data size:[[:space:]]+0 '
if "$ntfsinfo" -F /sparse.bin "$image" |
    grep -Eq 'File attributes:.*SPARSE_FILE'; then
    exit 1
fi
if "$driver" --retrieval-pointers \
    "$image" /sparse.bin 0 >/dev/null 2>&1; then
    exit 1
fi

"$ntfsfix" -n "$image" >/dev/null
