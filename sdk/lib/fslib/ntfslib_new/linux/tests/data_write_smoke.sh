#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfsfix=$5

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-write.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/write.ntfs"
original="$workdir/original.bin"
patch="$workdir/patch.bin"
expected="$workdir/expected.bin"

dd if=/dev/zero bs=65536 count=1 status=none |
    tr '\000' A >"$original"
dd if=/dev/zero bs=8192 count=1 status=none |
    tr '\000' B >"$patch"
cp "$original" "$expected"
dd if="$patch" of="$expected" bs=1 seek=3000 conv=notrunc status=none

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L WRITE_TEST "$image"
"$ntfscp" -f -q "$image" "$original" /data.bin

"$driver" --write "$image" /data.bin 3000 "$patch"
"$driver" --cat "$image" /data.bin | cmp - "$expected"
"$ntfscat" "$image" /data.bin | cmp - "$expected"
"$ntfsfix" -n "$image" >/dev/null
