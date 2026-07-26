#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfsls=$3
ntfscat=$4
ntfsinfo=$5
ntfsfix=$6
ntfs3g=$7
fusermount3=$8

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-create.XXXXXX")
image="$workdir/create.ntfs"
large="$workdir/large.ntfs"
mountpoint="$workdir/mnt"
payload="$workdir/payload.bin"
report="$workdir/report.txt"
mounted=0
old_time=126256467060000000

# Some hosts confine fusermount3 with an AppArmor profile that rejects
# TMPDIR mountpoints; util-linux umount stays available for the fixture.
unmount_fixture()
{
    "$fusermount3" -u "$mountpoint" 2>/dev/null ||
        umount "$mountpoint"
}

cleanup()
{
    if test "$mounted" -eq 1; then
        unmount_fixture >/dev/null 2>&1 || true
    fi
    rm -r -- "$workdir"
}
trap cleanup EXIT HUP INT TERM

# The literal $MFT:$BITMAP bit for one file record, read with the
# independent NTFS-3G metadata-stream reader.
mft_bitmap_bit()
{
    _record=$1
    _byte=$("$ntfscat" -i 0 -a 0xb0 "$2" 2>/dev/null |
        dd bs=1 skip=$((_record / 8)) count=1 status=none |
        od -An -tu1 |
        tr -d ' ')
    echo $(((_byte >> (_record % 8)) & 1))
}

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L CREATE_TEST "$image"

free_before=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')

# Empty file creation publishes an ordinary base record.
file_record=$("$driver" --create-file "$image" /alpha.txt)
test "$(mft_bitmap_bit "$file_record" "$image")" = 1

# Empty directory creation, nested parents, and a trailing separator.
dir_record=$("$driver" --create-dir "$image" /dir1)
"$driver" --create-dir "$image" /dir1/dir2 >/dev/null
"$driver" --create-file "$image" /dir1/dir2/deep.txt >/dev/null
"$driver" --create-dir "$image" /dir1/dir2/trail/ >/dev/null

# Duplicate names must collide through NTFS $UpCase collation.
if "$driver" --create-file "$image" /alpha.txt 2>/dev/null; then
    echo "duplicate name was accepted" >&2
    exit 1
fi
if "$driver" --create-dir "$image" /ALPHA.TXT 2>/dev/null; then
    echo "case-insensitive duplicate was accepted" >&2
    exit 1
fi

# Illegal names, reserved dot names, and missing/non-directory parents.
for bad in '/bad|name' '/bad<name' '/dir1/..' '/dir1/.'; do
    if "$driver" --create-file "$image" "$bad" 2>/dev/null; then
        echo "illegal name was accepted: $bad" >&2
        exit 1
    fi
done
if "$driver" --create-file "$image" /nodir/file.txt 2>/dev/null; then
    echo "missing parent was accepted" >&2
    exit 1
fi
if "$driver" --create-file "$image" /alpha.txt/child 2>/dev/null; then
    echo "file parent was accepted" >&2
    exit 1
fi

# Rejected requests may never consume MFT records: allocation stays
# contiguous across the failures above.
next_record=$("$driver" --create-file "$image" /continuity.txt)
last_record=$("$driver" --create-file "$image" /continuity2.txt)
test "$last_record" -eq $((next_record + 1))
test "$(mft_bitmap_bit $((last_record + 1)) "$image")" = 0

# Parent $I30 publication is visible to the independent reader at every
# level, and both enumerations agree once the dot entries synthesized by
# ntfsls are set aside.
"$ntfsls" "$image" | grep -qx 'alpha.txt'
"$ntfsls" "$image" | grep -qx 'dir1'
"$ntfsls" -p /dir1 "$image" | grep -qx 'dir2'
"$ntfsls" -p /dir1/dir2 "$image" | grep -qx 'deep.txt'
"$ntfsls" -p /dir1/dir2 "$image" | grep -qx 'trail'
ours=$("$driver" --list "$image" /dir1/dir2 | wc -l)
theirs=$("$ntfsls" -p /dir1/dir2 "$image" |
    grep -cvE '^\.\.?$')
test "$theirs" -eq "$ours"

# New-file metadata: indexed POSIX $FILE_NAME, default ARCHIVE content
# attribute, and an empty resident unnamed $DATA value.
"$ntfsinfo" -F /alpha.txt "$image" >"$report"
grep -Eq 'Namespace:[[:space:]]+POSIX' "$report"
grep -Eq 'File attributes:[[:space:]]+ARCHIVE \(0x00000020\)' "$report"
sed -n '/Dumping attribute \$DATA/,/End of inode reached/p' "$report" |
    grep -Eq 'Resident:[[:space:]]+Yes'
sed -n '/Dumping attribute \$DATA/,/End of inode reached/p' "$report" |
    grep -Eq 'Data size:[[:space:]]+0 '

# New-directory metadata: the DIRECTORY record flag, an empty $I30
# $INDEX_ROOT with filename collation, and no default ARCHIVE bit.
"$ntfsinfo" -F /dir1 "$image" >"$report"
grep -Eq 'MFT Record Flags:[[:space:]]+IN_USE DIRECTORY' "$report"
grep -q 'Dumping attribute \$INDEX_ROOT' "$report"
grep -Eq 'Collation Rule:[[:space:]]+1 ' "$report"
sed -n '/Dumping attribute \$STANDARD_INFORMATION/,/Dumping attribute/p' \
    "$report" | grep -Eq 'File attributes:[[:space:]]+\(0x00000000\)'

# Explicit creation attributes are honored.
"$driver" --create-file "$image" /hidden.txt 0x2 >/dev/null
"$ntfsinfo" -F /hidden.txt "$image" |
    grep -Eq 'File attributes:[[:space:]]+HIDDEN \(0x00000002\)'

# A created file accepts ordinary data and both readers agree exactly.
printf 'created-file-data-fixture' >"$payload"
"$driver" --write "$image" /alpha.txt 0 "$payload"
"$driver" --cat "$image" /alpha.txt | cmp - "$payload"
"$ntfscat" "$image" /alpha.txt | cmp - "$payload"

# Creation advances only the parent's last-write and change stamps.
"$driver" --set-basic "$image" /dir1 \
    "$old_time" "$old_time" "$old_time" "$old_time" -
"$driver" --create-file "$image" /dir1/stamp.txt >/dev/null
set -- $("$driver" --basic "$image" /dir1)
test "$#" -eq 5
test "$1" -eq "$old_time"
test "$2" -eq "$old_time"
test "$3" -gt "$old_time"
test "$4" -gt "$old_time"

# Every record so far is resident-only: clusters may go only to the
# chunked $MFT data preallocation, at most one 16-cluster reservation
# beyond the handful of freshly initialized 1 KiB records.
free_after=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_after" -le "$free_before"
test $((free_before - free_after)) -le 16

# Grow one directory far past its resident root using only the shared
# core: the first insertion overflow promotes the root into an index
# buffer and later overflows split leaves. The independent reader must
# agree on the complete name set afterwards.
"$driver" --create-dir "$image" /many >/dev/null
i=1
while [ "$i" -le 300 ]; do
    "$driver" --create-file "$image" \
        "$(printf '/many/entry-%04u.dat' "$i")" >/dev/null
    i=$((i + 1))
done
"$driver" --list "$image" /many |
    awk '{ print $NF }' | sort >"$workdir/names.ours"
"$ntfsls" -p /many "$image" |
    grep -vE '^\.\.?$' | sort >"$workdir/names.ntfs3g"
cmp "$workdir/names.ours" "$workdir/names.ntfs3g"
test "$(wc -l <"$workdir/names.ours")" -eq 300
"$driver" --basic "$image" /many/entry-0001.dat >/dev/null
"$driver" --basic "$image" /many/entry-0150.dat >/dev/null
"$driver" --basic "$image" /many/entry-0300.dat >/dev/null

"$ntfsfix" -n "$image" >/dev/null

# Multi-node directories: let NTFS-3G build an $I30 tree with INDX
# leaves, then insert at collation extremes and interior positions.
truncate -s 128M "$large"
"$mkntfs" -F -Q -q -L CREATE_LARGE "$large"
mkdir "$mountpoint"
if ! "$ntfs3g" "$large" "$mountpoint" -o permissions 2>/dev/null; then
    echo "ntfs-3g mount unavailable; leaf-insertion section skipped" >&2
    exit 0
fi
mounted=1
mkdir "$mountpoint/bigdir"
i=1
while [ "$i" -le 350 ]; do
    printf 'x' >"$mountpoint/bigdir/$(printf 'file-%04u.dat' "$i")"
    i=$((i + 1))
done
sync
unmount_fixture
mounted=0

"$driver" --create-file "$large" /bigdir/aaa-first.dat >/dev/null
"$driver" --create-file "$large" /bigdir/file-0180-mid.dat >/dev/null
last_leaf_record=$("$driver" --create-file "$large" /bigdir/zzz-last.dat)
"$ntfsls" -p /bigdir "$large" | grep -qx 'aaa-first.dat'
"$ntfsls" -p /bigdir "$large" | grep -qx 'file-0180-mid.dat'
"$ntfsls" -p /bigdir "$large" | grep -qx 'zzz-last.dat'
ours=$("$driver" --list "$large" /bigdir | wc -l)
theirs=$("$ntfsls" -p /bigdir "$large" |
    grep -cvE '^\.\.?$')
test "$ours" -eq 353
test "$theirs" -eq "$ours"

# Saturating one leaf of the NTFS-3G-built tree now splits it. Every
# insertion targeted at the same key range must succeed, stay visible to
# the independent reader, and keep MFT allocation contiguous.
i=1
last_success=$last_leaf_record
while [ "$i" -le 60 ]; do
    last_success=$("$driver" --create-file "$large" \
        "$(printf '/bigdir/file-0180-fill-%02u.dat' "$i")")
    i=$((i + 1))
done
"$driver" --list "$large" /bigdir |
    awk '{ print $NF }' | sort >"$workdir/names.ours"
"$ntfsls" -p /bigdir "$large" |
    grep -vE '^\.\.?$' | sort >"$workdir/names.ntfs3g"
cmp "$workdir/names.ours" "$workdir/names.ntfs3g"
test "$(wc -l <"$workdir/names.ours")" -eq 413
recovered_record=$("$driver" --create-file "$large" /leaf-rollback.dat)
test "$recovered_record" -eq $((last_success + 1))
test "$(mft_bitmap_bit $((recovered_record + 1)) "$large")" = 0

"$ntfsfix" -n "$large" >/dev/null
