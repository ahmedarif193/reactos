#!/bin/sh
# Security-mutation smoke: set a validated self-relative descriptor
# through the shared core (legacy per-file $SECURITY_DESCRIPTOR with the
# $Secure SecurityId cleared), replace it, reject a malformed one, and
# verify byte-for-byte through our reader, ntfsfix, and the
# system.ntfs_acl xattr of a real NTFS-3G mount.
set -eu

frontend=$1
mkntfs=$2
ntfscp=$3
ntfs3g=$4
fusermount3=$5
ntfsfix=$6

test -e /dev/fuse || exit 77
command -v python3 >/dev/null 2>&1 || exit 77

runtime_root=${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}
workdir=$(mktemp -d "$runtime_root/ntfslib-secwrite.XXXXXX")
image="$workdir/test.ntfs"
mountpoint="$workdir/mnt"
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
truncate -s 32M "$image"
"$mkntfs" -F -Q -q -L SECWRITE "$image"
printf 'security smoke payload\n' > "$workdir/f.txt"
"$ntfscp" -f -q "$image" "$workdir/f.txt" /f.txt

build_descriptor()
{
    python3 - "$1" "$2" <<'PYEOF'
import struct
import sys

def sid(*subs, ida=5):
    return (struct.pack("<BB6B", 1, len(subs), 0, 0, 0, 0, 0, ida) +
            b"".join(struct.pack("<I", value) for value in subs))

mask = int(sys.argv[2], 0)
owner = sid(32, 544)
group = sid(32, 545)
world = struct.pack("<BB6BI", 1, 1, 0, 0, 0, 0, 0, 1, 0)
ace = struct.pack("<BBHI", 0, 0, 8 + len(world), mask) + world
acl = struct.pack("<BBHHH", 2, 0, 8 + len(ace), 1, 0) + ace
dacl_off = 20
owner_off = dacl_off + len(acl)
group_off = owner_off + len(owner)
sd = (struct.pack("<BBHIIII", 1, 0, 0x8004,
                  owner_off, group_off, 0, dacl_off) +
      acl + owner + group)
sd += bytes((-len(sd)) % 4)
open(sys.argv[1], "wb").write(sd)
PYEOF
}

verify_descriptor()
{
    "$frontend" --security "$image" /f.txt | cmp - "$1"
    "$ntfsfix" -n "$image" >/dev/null 2>&1
    "$ntfs3g" "$image" "$mountpoint" -o ro
    mounted=1
    attempt=0
    while ! mountpoint -q "$mountpoint" && test "$attempt" -lt 50; do
        sleep 0.1
        attempt=$((attempt + 1))
    done
    mountpoint -q "$mountpoint"
    python3 - "$mountpoint/f.txt" "$1" <<'PYEOF'
import os
import sys

acl = os.getxattr(sys.argv[1], "system.ntfs_acl")
want = open(sys.argv[2], "rb").read()
if acl != want:
    raise SystemExit("NTFS-3G returned a different descriptor")
PYEOF
    "$fusermount3" -u "$mountpoint" 2>/dev/null || umount "$mountpoint"
    mounted=0
}

# Initial set, then replacement with a different access mask.
build_descriptor "$workdir/sd1.bin" 0x10000000
"$frontend" --set-security "$image" /f.txt "$workdir/sd1.bin"
verify_descriptor "$workdir/sd1.bin"

build_descriptor "$workdir/sd2.bin" 0x001200A9
"$frontend" --set-security "$image" /f.txt "$workdir/sd2.bin"
verify_descriptor "$workdir/sd2.bin"

# A truncated descriptor must be rejected without changing the file.
dd if="$workdir/sd2.bin" of="$workdir/bad.bin" bs=1 count=17 status=none
if "$frontend" --set-security "$image" /f.txt "$workdir/bad.bin" \
    2>/dev/null; then
    echo "malformed descriptor was accepted" >&2
    exit 1
fi
verify_descriptor "$workdir/sd2.bin"

echo "security write smoke passed"
