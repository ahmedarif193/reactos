#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfsinfo=$5
ntfsfix=$6
xxd=$7

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-reparse.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

image="$workdir/reparse.ntfs"
empty="$workdir/empty"
ea_value="$workdir/ea-value"
symlink_one="$workdir/symlink-one.bin"
symlink_two="$workdir/symlink-two.bin"
symlink_delete="$workdir/symlink-delete.bin"
wrong_tag="$workdir/wrong-tag.bin"
third_party="$workdir/third-party.bin"
third_delete="$workdir/third-delete.bin"
wrong_guid_delete="$workdir/wrong-guid-delete.bin"
report="$workdir/ntfsinfo.txt"

truncate -s 0 "$empty"
printf 'Ada' >"$ea_value"

# Relative IO_REPARSE_TAG_SYMLINK buffers for "target.txt" and "next.bin".
printf '%s\n' \
    '0c0000a0200000000000140000001400010000007400610072006700650074002e00740078007400' |
    "$xxd" -r -p >"$symlink_one"
printf '%s\n' \
    '0c0000a01c0000000000100000001000010000006e006500780074002e00620069006e00' |
    "$xxd" -r -p >"$symlink_two"
printf '%s\n' '0c0000a000000000' |
    "$xxd" -r -p >"$symlink_delete"

# A different, valid Microsoft tag used to exercise replacement mismatch.
printf '%s\n' '4200008000000000' |
    "$xxd" -r -p >"$wrong_tag"

# A non-Microsoft tag, nonzero GUID, and 8 KiB opaque payload. The complete
# value is 8,216 bytes and therefore must become nonresident in a 1 KiB MFT
# record.
printf '%s\n' \
    '420000000020000000112233445566778899aabbccddeeff' |
    "$xxd" -r -p >"$third_party"
dd if=/dev/zero bs=8192 count=1 status=none |
    tr '\000' R >>"$third_party"
printf '%s\n' \
    '420000000000000000112233445566778899aabbccddeeff' |
    "$xxd" -r -p >"$third_delete"
printf '%s\n' \
    '420000000000000000112233445566778899aabbccddee00' |
    "$xxd" -r -p >"$wrong_guid_delete"

truncate -s 128M "$image"
"$mkntfs" -F -Q -q -L REPARSE_TEST "$image"
"$ntfscp" -f -q "$image" "$empty" /link.bin
"$ntfscp" -f -q "$image" "$empty" /third.bin
"$ntfscp" -f -q "$image" "$empty" /ea.bin
"$ntfscp" -f -q "$image" "$ea_value" /data.bin

if "$driver" --set-reparse \
    "$image" /data.bin "$symlink_one" >/dev/null 2>&1
then
    echo "accepted a symlink over a nonempty data stream" >&2
    exit 1
fi
if "$driver" --set-reparse \
    "$image" / "$wrong_tag" >/dev/null 2>&1
then
    echo "accepted a reparse point on a nonempty directory" >&2
    exit 1
fi

# Make change-time and archive maintenance independently observable.
"$driver" --set-basic "$image" /link.bin - - - 100 0
"$driver" --set-basic "$image" /third.bin - - - 100 0

"$driver" --set-reparse "$image" /link.bin "$symlink_one"
"$driver" --reparse "$image" /link.bin | cmp - "$symlink_one"
"$ntfscat" -a 0xc0 "$image" /link.bin | cmp - "$symlink_one"
test "$("$driver" --readlink "$image" /link.bin)" = target.txt
set -- $("$driver" --lookup-reparse "$image" /link.bin 0)
test "$1" = 0x00000104
test "$2" -eq 0
link_record=$3
set -- $("$driver" --lookup-reparse "$image" /link.bin 1)
test "$1" = 0x00000000
test "$2" -eq 0
test "$3" = "$link_record"
set -- $("$driver" --lookup-reparse "$image" /link.bin/child 1)
test "$1" = 0x00000104
test "$2" -eq 12
test "$3" = "$link_record"
set -- $("$driver" --lookup-reparse "$image" /link.bin/ 0)
test "$1" = 0x00000104
test "$2" -eq 2
test "$3" = "$link_record"
test "$("$driver" --basic "$image" /link.bin |
    awk '{ print $5 }')" = 0x00000420
test "$("$driver" --basic "$image" /link.bin |
    awk '{ print $4 }')" -gt 100
test "$("$driver" --list-info "$image" / |
    awk '$4 == "link.bin" { print $5 " " $6 }')" = \
    '0 0xa000000c'

"$driver" --set-reparse "$image" /link.bin "$symlink_two"
"$driver" --reparse "$image" /link.bin | cmp - "$symlink_two"
test "$("$driver" --readlink "$image" /link.bin)" = next.bin

if "$driver" --set-reparse \
    "$image" /link.bin "$wrong_tag" >/dev/null 2>&1
then
    echo "accepted a replacement with a different reparse tag" >&2
    exit 1
fi
"$driver" --reparse "$image" /link.bin | cmp - "$symlink_two"
if "$driver" --delete-reparse \
    "$image" /link.bin "$wrong_tag" >/dev/null 2>&1
then
    echo "deleted a reparse point with a different tag" >&2
    exit 1
fi
"$driver" --reparse "$image" /link.bin | cmp - "$symlink_two"

"$driver" --delete-reparse "$image" /link.bin "$symlink_delete"
if "$driver" --reparse "$image" /link.bin >/dev/null 2>&1
then
    echo "deleted reparse attribute is still readable" >&2
    exit 1
fi
test "$("$driver" --basic "$image" /link.bin |
    awk '{ print $5 }')" = 0x00000020
test "$("$driver" --list-info "$image" / |
    awk '$4 == "link.bin" { print $5 " " $6 }')" = \
    '0 0x00000000'

# Native NTFS rejects adding the first reparse point to a file that already
# owns EAs. Existing reparse points may retain and mutate EAs separately.
"$driver" --set-ea "$image" /ea.bin 0 Author "$ea_value"
if "$driver" --set-reparse \
    "$image" /ea.bin "$symlink_one" >/dev/null 2>&1
then
    echo "accepted a first reparse point on an EA-bearing file" >&2
    exit 1
fi
test "$("$driver" --ea "$image" /ea.bin |
    awk '{ print $2 " " $3 " " $4 }')" = 'Author 3 416461'

free_before=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
cluster_size=$("$driver" --probe "$image" |
    awk '/^cluster size:/ { print $3 }')
"$driver" --set-reparse "$image" /third.bin "$third_party"
free_after=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
expected_clusters=$(((8216 + cluster_size - 1) / cluster_size))
test $((free_before - free_after)) -eq "$expected_clusters"

"$driver" --reparse "$image" /third.bin | cmp - "$third_party"
"$ntfscat" -a 0xc0 "$image" /third.bin | cmp - "$third_party"
test "$("$driver" --list-info "$image" / |
    awk '$4 == "third.bin" { print $5 " " $6 }')" = \
    '0 0x00000042'
"$ntfsinfo" -F /third.bin "$image" >"$report"
sed -n '/Dumping attribute \$REPARSE_POINT (0xc0)/,/End of inode/p' \
    "$report" |
    grep -q 'Resident:[[:space:]]*No'

if "$driver" --delete-reparse \
    "$image" /third.bin "$wrong_guid_delete" >/dev/null 2>&1
then
    echo "deleted a third-party reparse point with a different GUID" >&2
    exit 1
fi
"$driver" --reparse "$image" /third.bin | cmp - "$third_party"

"$driver" --delete-reparse "$image" /third.bin "$third_delete"
free_final=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_final" -eq "$free_before"
test "$("$driver" --basic "$image" /third.bin |
    awk '{ print $5 }')" = 0x00000020
"$ntfsfix" -n "$image" >/dev/null
