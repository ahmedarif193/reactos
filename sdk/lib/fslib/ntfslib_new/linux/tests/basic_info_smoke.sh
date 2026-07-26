#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfsinfo=$4
ntfsfix=$5

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-basic.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/basic.ntfs"
source="$workdir/source.bin"
patch="$workdir/patch.bin"
report="$workdir/ntfsinfo.txt"
timestamp=126256467060000000

truncate -s 32M "$image"
"$mkntfs" -F -Q -q -L BASIC_INFO "$image"
printf 'basic-information-fixture\n' >"$source"
printf 'X' >"$patch"
"$ntfscp" -f -q "$image" "$source" /basic.bin

"$driver" --set-basic "$image" /basic.bin \
    "$timestamp" "$timestamp" "$timestamp" "$timestamp" 0x23

actual=$("$driver" --basic "$image" /basic.bin)
expected="$timestamp $timestamp $timestamp $timestamp 0x00000023"
if [ "$actual" != "$expected" ]
then
    echo "unexpected basic information: $actual" >&2
    exit 1
fi

"$ntfsinfo" -F /basic.bin "$image" >"$report"
if [ "$(grep -cF 'Sat Feb  3 04:05:06 2001 UTC' "$report")" -ne 4 ]
then
    echo "ntfsinfo did not find the four updated standard timestamps" >&2
    exit 1
fi
if [ "$(grep -cF '(0x00000023)' "$report")" -ne 1 ]
then
    echo "ntfsinfo did not find the updated basic attributes" >&2
    exit 1
fi

"$driver" --write "$image" /basic.bin 0 "$patch"
actual=$("$driver" --basic "$image" /basic.bin)
set -- $actual
if [ "$#" -ne 5 ] ||
   [ "$1" != "$timestamp" ] ||
   [ "$2" != "$timestamp" ] ||
   [ "$3" -le "$timestamp" ] ||
   [ "$4" -le "$timestamp" ] ||
   [ "$5" != 0x00000023 ]
then
    echo "write did not maintain automatic timestamps: $actual" >&2
    exit 1
fi

"$driver" --set-basic "$image" /basic.bin \
    "$timestamp" "$timestamp" "$timestamp" "$timestamp" 0x23
"$driver" --allocate "$image" /basic.bin 8192
actual=$("$driver" --basic "$image" /basic.bin)
set -- $actual
if [ "$#" -ne 5 ] ||
   [ "$1" != "$timestamp" ] ||
   [ "$2" != "$timestamp" ] ||
   [ "$3" != "$timestamp" ] ||
   [ "$4" -le "$timestamp" ] ||
   [ "$5" != 0x00000023 ]
then
    echo "allocation did not maintain automatic timestamps: $actual" >&2
    exit 1
fi

"$ntfsfix" -n "$image" >/dev/null
