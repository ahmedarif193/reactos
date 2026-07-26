#!/bin/sh
# Crash-consistency smoke for the writable FUSE mount: every operation the
# mount has acknowledged must survive a SIGKILL of the filesystem daemon.
# The shared core publishes each mutation synchronously in a readable
# order, so unlike a buffered writer, killing the daemon at any point
# between operations loses nothing and never leaves the volume
# inconsistent. One additional round kills the daemon while a background
# writer is mid-stream; the in-flight file may be short, but the volume
# must stay consistent and every previously acknowledged byte intact.
set -eu

frontend=$1
mkntfs=$2
ntfs3g=$3
fusermount3=$4
ntfscat=$5
ntfsfix=$6
rounds=${NTFSLIB_CRASH_ROUNDS:-4}

test -e /dev/fuse || exit 77

runtime_root=${NTFSLIB_BENCH_ROOT:-${TMPDIR:-/tmp}}
workdir=$(mktemp -d "$runtime_root/ntfslib-crash.XXXXXX")
image="$workdir/test.ntfs"
mountpoint="$workdir/mnt"
checkpoint="$workdir/ntfs3g"
twin="$workdir/twin"
daemon_pid=0
checkmounted=0

release_mountpoint()
{
    "$fusermount3" -u "$mountpoint" 2>/dev/null ||
        umount "$mountpoint" 2>/dev/null || true
}

cleanup()
{
    if test "$checkmounted" -eq 1; then
        "$fusermount3" -u "$checkpoint" 2>/dev/null ||
            umount "$checkpoint" 2>/dev/null || true
    fi
    if test "$daemon_pid" -ne 0; then
        kill -KILL "$daemon_pid" 2>/dev/null || true
    fi
    release_mountpoint
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM

mkdir "$mountpoint" "$checkpoint" "$twin"
truncate -s 48M "$image"
"$mkntfs" -F -Q -q -L CRASH "$image"
dd if=/dev/urandom of="$workdir/chunk.bin" bs=32K count=2 status=none

mount_writable()
{
    "$frontend" --writable "$image" "$mountpoint" -f &
    daemon_pid=$!
    attempt=0
    while ! mountpoint -q "$mountpoint" && test "$attempt" -lt 50; do
        sleep 0.1
        attempt=$((attempt + 1))
    done
    mountpoint -q "$mountpoint"
}

kill_daemon()
{
    kill -KILL "$daemon_pid"
    wait "$daemon_pid" 2>/dev/null || true
    daemon_pid=0
    release_mountpoint
}

verify_twin()
{
    "$ntfsfix" -n "$image" >/dev/null 2>&1
    for expected in "$twin"/*; do
        test -f "$expected" || continue
        "$ntfscat" "$image" "/$(basename "$expected")" |
            cmp - "$expected"
    done
}

mount_writable || exit 77

round=1
while test "$round" -le "$rounds"; do
    for suffix in a b c; do
        name="round$round-$suffix.bin"
        cp "$workdir/chunk.bin" "$mountpoint/$name"
        cat "$workdir/chunk.bin" >> "$mountpoint/$name"
        cp "$mountpoint/$name" "$twin/$name"
    done
    kill_daemon
    verify_twin
    mount_writable
    round=$((round + 1))
done

# Racy round: kill mid-write. The in-flight file is allowed to be short
# or absent; consistency and all acknowledged bytes are not negotiable.
( index=1
  while test "$index" -le 200; do
      cat "$workdir/chunk.bin" >> "$mountpoint/inflight.bin" 2>/dev/null ||
          exit 0
      index=$((index + 1))
  done ) &
writer_pid=$!
sleep 0.3
kill_daemon
wait "$writer_pid" 2>/dev/null || true
verify_twin

# The surviving volume must be fully readable by NTFS-3G as well.
"$ntfs3g" "$image" "$checkpoint" -o ro
checkmounted=1
attempt=0
while ! mountpoint -q "$checkpoint" && test "$attempt" -lt 50; do
    sleep 0.1
    attempt=$((attempt + 1))
done
mountpoint -q "$checkpoint"
for expected in "$twin"/*; do
    cmp "$checkpoint/$(basename "$expected")" "$expected"
done
"$fusermount3" -u "$checkpoint" 2>/dev/null || umount "$checkpoint"
checkmounted=0

echo "crash consistency: $rounds kill rounds plus one mid-write kill," \
    "every acknowledged byte intact, volume consistent throughout"
