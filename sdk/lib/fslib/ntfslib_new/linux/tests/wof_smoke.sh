#!/bin/sh
set -eu

driver=$1
fixture=$2
mkntfs=$3
ntfscp=$4
ntfsfix=$5
xxd=$6
ntfscat=$7
ntfsinfo=$8

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-wof.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

image="$workdir/wof.ntfs"
expected="$workdir/expected.bin"
logical="$workdir/logical-zero.bin"
slice="$workdir/expected-slice.bin"
materialized_expected="$workdir/materialized-expected.bin"
patch="$workdir/patch.bin"
report="$workdir/ntfsinfo.txt"

"$fixture" --generate "$expected" 100123
dd if="$expected" of="$slice" bs=1 skip=123 count=70001 status=none
truncate -s 100123 "$logical"
truncate -s 128M "$image"
"$mkntfs" -F -Q -q -L WOF_TEST "$image"

for entry in \
    '0 xpress4k.bin 00000000' \
    '1 lzx.bin 01000000' \
    '2 xpress8k.bin 02000000' \
    '3 xpress16k.bin 03000000'
do
    set -- $entry
    algorithm=$1
    name=$2
    algorithm_le=$3
    backing="$workdir/$name.wof"
    reparse="$workdir/$name.reparse"

    "$fixture" --compress \
        "$algorithm" "$expected" "$backing"
    printf '%s\n' \
        "1700008010000000010000000200000001000000$algorithm_le" |
        "$xxd" -r -p >"$reparse"

    "$ntfscp" -f -q "$image" "$logical" "/$name"
    "$driver" --write \
        "$image" "/$name:WofCompressedData" 0 "$backing"
    "$driver" --set-reparse \
        "$image" "/$name" "$reparse"
    "$driver" --cat "$image" "/$name" | cmp - "$expected"
    "$driver" --cat-range "$image" "/$name" 123 70001 |
        cmp - "$slice"
done

# Modifying externally backed content must first restore the complete logical
# stream as ordinary $DATA, then discard the WOF point and backing stream.
cp "$expected" "$materialized_expected"
printf 'ordinary-after-wof' >"$patch"
dd if="$patch" of="$materialized_expected" bs=1 seek=123 \
    conv=notrunc status=none
cluster_size=$("$driver" --probe "$image" |
    awk '/^cluster size:/ { print $3 }')
free_before=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
old_logical_allocation=$("$driver" --list-info "$image" / |
    awk '$4 == "xpress4k.bin" { print $3 }')
backing_size=$(wc -c <"$workdir/xpress4k.bin.wof")
new_logical_clusters=$(((100123 + cluster_size - 1) / cluster_size))
backing_clusters=$(((backing_size + cluster_size - 1) / cluster_size))
expected_free=$((free_before +
    old_logical_allocation / cluster_size +
    backing_clusters -
    new_logical_clusters))

"$driver" --write "$image" /xpress4k.bin 123 "$patch"
"$driver" --cat "$image" /xpress4k.bin |
    cmp - "$materialized_expected"
"$ntfscat" -f "$image" /xpress4k.bin |
    cmp - "$materialized_expected"
if "$driver" --reparse "$image" /xpress4k.bin >/dev/null 2>&1
then
    echo "WOF reparse point survived data materialization" >&2
    exit 1
fi
if "$driver" --cat \
    "$image" /xpress4k.bin:WofCompressedData >/dev/null 2>&1
then
    echo "WOF backing stream survived data materialization" >&2
    exit 1
fi
if "$ntfscat" -f -n WofCompressedData \
    "$image" /xpress4k.bin >/dev/null 2>&1
then
    echo "ntfscat still sees the removed WOF backing stream" >&2
    exit 1
fi
test "$("$driver" --basic "$image" /xpress4k.bin |
    awk '{ print $5 }')" = 0x00000020
test "$("$driver" --list-info "$image" / |
    awk '$4 == "xpress4k.bin" { print $2 " " $5 " " $6 }')" = \
    '100123 0 0x00000000'
free_after=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_after" -eq "$expected_free"
"$ntfsinfo" -F /xpress4k.bin "$image" >"$report"
if grep -q 'Dumping attribute.*WofCompressedData' "$report" ||
   grep -q 'Dumping attribute \$REPARSE_POINT' "$report"
then
    echo "independent metadata inspection found stale WOF state" >&2
    exit 1
fi

# The explicit core operation used by FSCTL_DELETE_EXTERNAL_BACKING restores
# the same ordinary form even when no content patch follows.
"$driver" --delete-external-backing "$image" /lzx.bin
"$driver" --cat "$image" /lzx.bin | cmp - "$expected"
"$ntfscat" -f "$image" /lzx.bin | cmp - "$expected"
if "$driver" --reparse "$image" /lzx.bin >/dev/null 2>&1 ||
   "$driver" --cat \
       "$image" /lzx.bin:WofCompressedData >/dev/null 2>&1
then
    echo "explicit external-backing deletion left WOF metadata" >&2
    exit 1
fi
if "$driver" --delete-external-backing \
    "$image" /lzx.bin >/dev/null 2>&1
then
    echo "ordinary file accepted external-backing deletion twice" >&2
    exit 1
fi

# A cumulative chunk end beyond the backing stream must be rejected without
# reading outside the WofCompressedData attribute.
cp "$workdir/xpress4k.bin.wof" "$workdir/bad.wof"
printf '%s\n' 'ffffff7f' |
    "$xxd" -r -p |
    dd of="$workdir/bad.wof" bs=1 seek=0 conv=notrunc status=none
"$ntfscp" -f -q "$image" "$logical" /bad.bin
"$driver" --write \
    "$image" /bad.bin:WofCompressedData 0 "$workdir/bad.wof"
"$driver" --set-reparse \
    "$image" /bad.bin "$workdir/xpress4k.bin.reparse"
if "$driver" --cat "$image" /bad.bin >/dev/null 2>&1
then
    echo "accepted an out-of-range WOF chunk offset" >&2
    exit 1
fi
if "$driver" --write \
    "$image" /bad.bin 0 "$patch" >/dev/null 2>&1
then
    echo "modified a file with corrupt WOF backing" >&2
    exit 1
fi
"$driver" --reparse "$image" /bad.bin |
    cmp - "$workdir/xpress4k.bin.reparse"
"$driver" --cat \
    "$image" /bad.bin:WofCompressedData |
    cmp - "$workdir/bad.wof"

"$ntfsfix" -n "$image" >/dev/null
