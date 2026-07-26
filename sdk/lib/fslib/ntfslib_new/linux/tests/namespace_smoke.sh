#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfsls=$3
ntfscat=$4
ntfsinfo=$5
ntfsfix=$6

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-namespace.XXXXXX")
image="$workdir/ns.ntfs"
payload="$workdir/payload.bin"
trap 'rm -r -- "$workdir"' EXIT HUP INT TERM

free_clusters()
{
    "$driver" --probe "$image" |
        awk '/^free clusters:/ { print $3 }'
}

link_count()
{
    "$ntfsinfo" -F "$1" "$image" 2>/dev/null |
        awk '/Number of Hard Links/ { print $5 }'
}

truncate -s 256M "$image"
"$mkntfs" -F -Q -q -L NAMESPACE "$image"
baseline=$(free_clusters)

# Deletion reclaims a file's name, record, and clusters.
"$driver" --create-dir "$image" /dir >/dev/null
"$driver" --create-file "$image" /dir/data.bin >/dev/null
dd if=/dev/urandom of="$payload" bs=4096 count=8 status=none
"$driver" --write "$image" /dir/data.bin 0 "$payload"
before_delete=$(free_clusters)
"$driver" --remove "$image" /dir/data.bin
test $(($(free_clusters) - before_delete)) -eq 8
if "$ntfsls" -p /dir "$image" | grep -qx 'data.bin'; then
    echo "deleted name still visible" >&2
    exit 1
fi

# Directory semantics: only the matching remover works, and only empty
# directories go away.
"$driver" --create-file "$image" /dir/keep.bin >/dev/null
if "$driver" --remove "$image" /dir 2>/dev/null; then
    echo "--remove accepted a directory" >&2
    exit 1
fi
if "$driver" --remove-dir "$image" /dir 2>/dev/null; then
    echo "nonempty directory was removed" >&2
    exit 1
fi
if "$driver" --remove-dir "$image" /dir/keep.bin 2>/dev/null; then
    echo "--remove-dir accepted a file" >&2
    exit 1
fi
"$driver" --remove "$image" /dir/keep.bin
"$driver" --remove-dir "$image" /dir
if "$ntfsls" "$image" | grep -qx 'dir'; then
    echo "removed directory still visible" >&2
    exit 1
fi

# Rename within one directory, across directories, case-only, and the
# collision refusal. Data must survive every move byte-for-byte.
"$driver" --create-dir "$image" /src >/dev/null
"$driver" --create-dir "$image" /dst >/dev/null
"$driver" --create-file "$image" /src/one.bin >/dev/null
"$driver" --write "$image" /src/one.bin 0 "$payload"
"$driver" --rename "$image" /src/one.bin /src/two.bin
"$driver" --rename "$image" /src/two.bin /dst/three.bin
"$driver" --rename "$image" /dst/three.bin /dst/THREE.BIN
"$ntfsls" -p /dst "$image" | grep -qx 'THREE.BIN'
if "$ntfsls" -p /src "$image" | grep -qE 'one.bin|two.bin'; then
    echo "renamed name still visible at source" >&2
    exit 1
fi
"$ntfscat" "$image" /dst/THREE.BIN | cmp - "$payload"
"$driver" --create-file "$image" /src/other.bin >/dev/null
if "$driver" --rename "$image" /src/other.bin /dst/three.bin \
    2>/dev/null; then
    echo "rename onto an existing name succeeded" >&2
    exit 1
fi
"$driver" --rename "$image" /src /moved-src
"$ntfsls" -p /moved-src "$image" | grep -qx 'other.bin'

# Hard links share bytes and the record; the count tracks every name.
"$driver" --link "$image" /dst/THREE.BIN /moved-src/alias.bin
test "$(link_count /dst/THREE.BIN)" = 2
"$ntfscat" "$image" /moved-src/alias.bin | cmp - "$payload"
if "$driver" --link "$image" /moved-src /dirlink 2>/dev/null; then
    echo "hard link to a directory succeeded" >&2
    exit 1
fi
"$driver" --remove "$image" /dst/THREE.BIN
test "$(link_count /moved-src/alias.bin)" = 1
"$ntfscat" "$image" /moved-src/alias.bin | cmp - "$payload"
"$driver" --remove "$image" /moved-src/alias.bin
"$driver" --remove "$image" /src/other.bin 2>/dev/null ||
    "$driver" --remove "$image" /moved-src/other.bin
"$driver" --remove-dir "$image" /moved-src
"$driver" --remove-dir "$image" /dst
"$ntfsfix" -n "$image" >/dev/null

# Push one directory through root promotion, node splits, and the
# initial $ATTRIBUTE_LIST transition, then drain it back through every
# removal shape and reclaim the allocation.
"$driver" --create-dir "$image" /grow >/dev/null
i=1
while [ "$i" -le 1500 ]; do
    "$driver" --create-file "$image" \
        "$(printf '/grow/entry-%04u.dat' "$i")" >/dev/null
    i=$((i + 1))
done
"$driver" --list "$image" /grow |
    awk '{ print $NF }' | sort >"$workdir/grow.ours"
"$ntfsls" -p /grow "$image" |
    grep -vE '^\.\.?$' | sort >"$workdir/grow.ntfs3g"
cmp "$workdir/grow.ours" "$workdir/grow.ntfs3g"
test "$(wc -l <"$workdir/grow.ours")" -eq 1500

i=1
while [ "$i" -le 1500 ]; do
    "$driver" --remove "$image" \
        "$(printf '/grow/entry-%04u.dat' "$i")" >/dev/null
    i=$((i + 3))
done
"$driver" --list "$image" /grow |
    awk '{ print $NF }' | sort >"$workdir/grow.ours"
"$ntfsls" -p /grow "$image" |
    grep -vE '^\.\.?$' | sort >"$workdir/grow.ntfs3g"
cmp "$workdir/grow.ours" "$workdir/grow.ntfs3g"
test "$(wc -l <"$workdir/grow.ours")" -eq 1000

while read -r name; do
    "$driver" --remove "$image" "/grow/$name" >/dev/null
done <"$workdir/grow.ours"
test "$("$driver" --list "$image" /grow | wc -l)" -eq 0
"$driver" --remove-dir "$image" /grow

# Every cluster except permanent chunked $MFT growth must be free again.
final=$(free_clusters)
test "$final" -le "$baseline"
test $((baseline - final)) -le 400

"$ntfsfix" -n "$image" >/dev/null
