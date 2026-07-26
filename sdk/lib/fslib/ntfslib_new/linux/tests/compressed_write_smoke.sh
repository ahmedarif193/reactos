#!/bin/sh
# Compressed-write smoke: NTFS-3G authors an LZNT1-compressed file, the
# shared core patches it (unaligned mid-unit, multi-unit span, EOF
# growth, and a recompressing all-zeros rewrite), and every state is
# verified byte-for-byte through NTFS-3G's own decoder plus ntfsfix -n.
set -eu

frontend=$1
mkntfs=$2
ntfs3g=$3
fusermount3=$4
ntfscat=$5
ntfsinfo=$6
ntfsfix=$7

test -e /dev/fuse || exit 77
command -v python3 >/dev/null 2>&1 || exit 77

runtime_root=${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}
workdir=$(mktemp -d "$runtime_root/ntfslib-cwrite.XXXXXX")
image="$workdir/test.ntfs"
mountpoint="$workdir/mnt"
twin="$workdir/twin.bin"
mounted=0

cleanup()
{
    if test "$mounted" -eq 1; then
        "$fusermount3" -u "$mountpoint" 2>/dev/null ||
            umount "$mountpoint" 2>/dev/null || true
    fi
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM

mkdir "$mountpoint"
truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L CWRITE "$image"

"$ntfs3g" "$image" "$mountpoint" || exit 77
mounted=1
attempt=0
while ! mountpoint -q "$mountpoint" && test "$attempt" -lt 50; do
    sleep 0.1
    attempt=$((attempt + 1))
done
mountpoint -q "$mountpoint" || exit 77

mkdir "$mountpoint/c"
python3 - "$mountpoint/c" <<'PYEOF'
import os
import struct
import sys

path = sys.argv[1]
value = struct.unpack("<I", os.getxattr(path, "system.ntfs_attrib"))[0]
os.setxattr(path, "system.ntfs_attrib", struct.pack("<I", value | 0x800))
PYEOF
python3 -c '
import sys
data = (b"compressible pattern " * 3250)[:65536]
data += bytes(65536)
data += (b"XYZ" * 20000)[:60000]
sys.stdout.buffer.write(data)
' > "$twin"
cp "$twin" "$mountpoint/c/f.bin"
"$fusermount3" -u "$mountpoint" 2>/dev/null || umount "$mountpoint"
mounted=0

"$ntfsinfo" -F /c/f.bin "$image" 2>/dev/null |
    grep -q 'Attribute flags:.*0x0001' ||
    { echo "fixture file is not LZNT1 compressed" >&2; exit 77; }
"$ntfscat" "$image" /c/f.bin | cmp - "$twin"

verify()
{
    "$ntfscat" "$image" /c/f.bin | cmp - "$twin"
    "$frontend" --cat "$image" /c/f.bin | cmp - "$twin"
    "$ntfsfix" -n "$image" >/dev/null 2>&1
}

# Unaligned write inside one unit, incompressible payload.
dd if=/dev/urandom of="$workdir/patch.bin" bs=10000 count=1 status=none
"$frontend" --write "$image" /c/f.bin 30000 "$workdir/patch.bin"
dd if="$workdir/patch.bin" of="$twin" bs=1 seek=30000 \
    conv=notrunc status=none
verify

# Write spanning two compression units.
dd if=/dev/urandom of="$workdir/span.bin" bs=90000 count=1 status=none
"$frontend" --write "$image" /c/f.bin 60000 "$workdir/span.bin"
dd if="$workdir/span.bin" of="$twin" bs=1 seek=60000 \
    conv=notrunc status=none
verify

# Growth past EOF.
python3 -c 'import sys; sys.stdout.buffer.write(b"tail growth payload " * 800)' \
    > "$workdir/grow.bin"
"$frontend" --write "$image" /c/f.bin 191072 "$workdir/grow.bin"
oldsize=$(wc -c < "$twin")
if test "$oldsize" -lt 191072; then
    truncate -s 191072 "$twin"
fi
dd if="$workdir/grow.bin" of="$twin" bs=1 seek=191072 \
    conv=notrunc status=none
verify

# All-zeros rewrite must recompress back down to a small physical size.
python3 -c 'import sys; sys.stdout.buffer.write(bytes(207072))' \
    > "$workdir/zero.bin"
"$frontend" --write "$image" /c/f.bin 0 "$workdir/zero.bin"
cp "$workdir/zero.bin" "$twin"
verify
compressed=$("$ntfsinfo" -F /c/f.bin "$image" 2>/dev/null |
    awk '/Compressed size:/ { print $3; exit }')
test "$compressed" -le 16384 ||
    { echo "zero rewrite did not recompress ($compressed)" >&2; exit 1; }

echo "compressed write smoke passed (final compressed size $compressed)"
