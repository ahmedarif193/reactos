#!/bin/sh
# End-to-end writable-mount smoke test: drive a --writable FUSE mount with
# ordinary POSIX tooling while applying identical operations to a host-side
# twin directory, then verify the volume independently through ntfsfix,
# ntfscat, and a full recursive comparison via a real NTFS-3G mount.
set -eu

frontend=$1
mkntfs=$2
ntfs3g=$3
fusermount3=$4
ntfsls=$5
ntfscat=$6
ntfsfix=$7

test -e /dev/fuse || exit 77

# AppArmor may deny FUSE mounts under the shared TMPDIR entirely; setting
# NTFSLIB_BENCH_ROOT to an owned directory (as benchmark_fuse.sh does)
# keeps the fixture mountable on confined hosts.
runtime_root=${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}
workdir=$(mktemp -d "$runtime_root/ntfslib-mount-write.XXXXXX")
image="$workdir/test.ntfs"
mountpoint="$workdir/mnt"
checkpoint="$workdir/ntfs3g"
twin="$workdir/twin"
mounted=0
checkmounted=0

# AppArmor can deny fusermount3 the unmount operation for TMPDIR
# mountpoints; util-linux umount stays available for the fixture.
unmount_fixture()
{
    "$fusermount3" -u "$mountpoint" 2>/dev/null ||
        umount "$mountpoint"
}

unmount_check()
{
    "$fusermount3" -u "$checkpoint" 2>/dev/null ||
        umount "$checkpoint"
}

cleanup()
{
    if test "$checkmounted" -eq 1; then
        unmount_check >/dev/null 2>&1 || true
    fi
    if test "$mounted" -eq 1; then
        unmount_fixture >/dev/null 2>&1 || true
    fi
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM

mkdir "$mountpoint" "$checkpoint" "$twin"
truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L MOUNTWRITE "$image"
dd if=/dev/urandom of="$workdir/chunk.bin" bs=64K count=3 status=none

"$frontend" --writable "$image" "$mountpoint" || exit 77
mounted=1
attempt=0
while ! mountpoint -q "$mountpoint" && test "$attempt" -lt 50; do
    sleep 0.1
    attempt=$((attempt + 1))
done
mountpoint -q "$mountpoint" || exit 77

# Every mutation runs against the mount and the twin; the trees must stay
# identical through each phase.
both()
{
    (cd "$mountpoint" && "$@")
    (cd "$twin" && "$@")
}

both mkdir -p dir1/sub dir2
both cp "$workdir/chunk.bin" dir1/data.bin
both sh -c 'cat "$1" >> dir1/data.bin' apply "$workdir/chunk.bin"
both sh -c 'dd if="$1" of=dir1/data.bin bs=64K seek=1 \
    conv=notrunc status=none' apply "$workdir/chunk.bin"
both sh -c 'index=1
while [ "$index" -le 20 ]; do
    printf "payload %d\n" "$index" > "dir2/f$index.txt"
    index=$((index + 1))
done'
both truncate -s 100000 dir1/data.bin
both truncate -s 250000 dir1/data.bin
both mv dir2/f1.txt dir2/renamed.txt
both mv dir2/f2.txt dir1/sub/moved.txt
both mv -f dir2/f3.txt dir2/renamed.txt
both mv dir1/sub dir1/subrenamed
both ln dir1/data.bin dir1/hard.bin
both rm dir2/f4.txt
both mkdir dir3
both rmdir dir3
both touch -d '2020-01-02 03:04:05 UTC' dir2/f5.txt

# Deleting a populated directory must fail without changing either tree.
if rmdir "$mountpoint/dir2" 2>/dev/null; then
    echo "rmdir of a populated directory unexpectedly succeeded" >&2
    exit 1
fi

# Native NTFS EAs surface as user.* xattrs; diff -r ignores xattrs so the
# twin tree stays comparable.
have_python=0
if command -v python3 >/dev/null 2>&1; then
    have_python=1
    python3 - "$mountpoint" <<'PYEOF'
import os
import sys

target = os.path.join(sys.argv[1], "dir1", "data.bin")
os.setxattr(target, "user.Comment", b"hello ea")
os.setxattr(target, "user.other", b"\x00\x01\x02")
assert sorted(os.listxattr(target)) == ["user.Comment", "user.other"]
assert os.getxattr(target, "user.comment") == b"hello ea"
os.setxattr(target, "user.Comment", b"replaced", os.XATTR_REPLACE)
assert os.getxattr(target, "user.Comment") == b"replaced"
try:
    os.setxattr(target, "user.other", b"x", os.XATTR_CREATE)
    raise SystemExit("XATTR_CREATE on an existing EA succeeded")
except FileExistsError:
    pass
os.removexattr(target, "user.other")
assert os.listxattr(target) == ["user.Comment"]
try:
    os.getxattr(target, "user.other")
    raise SystemExit("a removed EA is still readable")
except OSError as error:
    if error.errno != 61:
        raise
PYEOF
fi

diff -r "$mountpoint" "$twin"
mount_mtime=$(stat -c %Y "$mountpoint/dir2/f5.txt")
twin_mtime=$(stat -c %Y "$twin/dir2/f5.txt")
test "$mount_mtime" = "$twin_mtime"

unmount_fixture
mounted=0

"$ntfsfix" -n "$image" >/dev/null 2>&1

# Independent readers must agree with the twin byte for byte.
"$ntfscat" "$image" /dir1/data.bin | cmp - "$twin/dir1/data.bin"
"$ntfscat" "$image" /dir1/hard.bin | cmp - "$twin/dir1/data.bin"
"$ntfsls" "$image" | sort > "$workdir/names.actual"
ls -1 "$twin" | sort > "$workdir/names.expected"
cmp "$workdir/names.actual" "$workdir/names.expected"

if test "$have_python" -eq 1; then
    "$frontend" --ea "$image" /dir1/data.bin > "$workdir/ea.actual"
    grep -q -- "- Comment 8 7265706c61636564" "$workdir/ea.actual"
    test "$(wc -l < "$workdir/ea.actual")" = 1
fi

"$ntfs3g" "$image" "$checkpoint" -o ro
checkmounted=1
attempt=0
while ! mountpoint -q "$checkpoint" && test "$attempt" -lt 50; do
    sleep 0.1
    attempt=$((attempt + 1))
done
mountpoint -q "$checkpoint"
diff -r "$checkpoint" "$twin"
check_mtime=$(stat -c %Y "$checkpoint/dir2/f5.txt")
test "$check_mtime" = "$twin_mtime"
if test "$have_python" -eq 1; then
    python3 - "$checkpoint" <<'PYEOF'
import os
import sys

target = os.path.join(sys.argv[1], "dir1", "data.bin")
try:
    packed = os.getxattr(target, "system.ntfs_ea")
except OSError:
    raise SystemExit(0)
assert b"Comment" in packed and b"replaced" in packed
PYEOF
fi
unmount_check
checkmounted=0

echo "writable mount end-to-end comparison passed"
