#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfssecaudit=$5
fakeroot=$6
xxd=$7

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-security.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/security.ntfs"
payload="$workdir/payload.bin"
core_direct="$workdir/core-direct.bin"
reference_direct="$workdir/reference-direct.bin"
core_central="$workdir/core-central.bin"
reference_central="$workdir/reference-central.bin"

printf 'security-descriptor-fixture\n' >"$payload"
truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L SECURITY_TEST "$image"
"$ntfscp" -f -q "$image" "$payload" /legacy.bin

# libntfs-3g creates a per-file descriptor for copied files.
"$driver" --security "$image" /legacy.bin >"$core_direct"
"$ntfscat" -a 0x50 "$image" /legacy.bin >"$reference_direct"
cmp "$core_direct" "$reference_direct"

# $Secure itself uses its NTFS 3.x SecurityId and exercises $SII -> $SDS.
"$driver" --show-metadata --security \
    "$image" '/$Secure' >"$core_central"
"$fakeroot" -- "$ntfssecaudit" -vv \
    "$image" '/$Secure' 2>&1 |
    awk '
        length($1) == 6 && $1 !~ /[^0-9A-Fa-f]/ {
            for (field = 2; field <= NF; field++) {
                if (length($field) != 8 ||
                    $field ~ /[^0-9A-Fa-f]/)
                    break
                printf "%s", $field
            }
        }
    ' |
    "$xxd" -r -p >"$reference_central"
test -s "$reference_central"
cmp "$core_central" "$reference_central"
