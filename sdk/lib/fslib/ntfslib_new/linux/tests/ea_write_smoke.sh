#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfsinfo=$5
ntfsfix=$6

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-ea.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/ea.ntfs"
source="$workdir/source.bin"
ada="$workdir/ada.bin"
grace="$workdir/grace.bin"
blob="$workdir/blob.bin"
fill="$workdir/fill.bin"
expected_small="$workdir/expected-small.ea"
expected_all="$workdir/expected-all.ea"
expected_blob="$workdir/expected-blob.ea"
report="$workdir/ntfsinfo.txt"
timestamp=126256467060000000
stream_index=1

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L EA_WRITE "$image"
printf 'native-ea-fixture\n' >"$source"
printf 'Ada' >"$ada"
printf 'Grace' >"$grace"
dd if=/dev/zero bs=32768 count=1 status=none |
    tr '\000' B >"$blob"
dd if=/dev/zero bs=520 count=1 status=none |
    tr '\000' F >"$fill"
"$ntfscp" -f -q "$image" "$source" /file.bin

# Start without ARCHIVE and at a deterministic change time. EA mutation must
# add ARCHIVE while changing only the automatic change timestamp.
"$driver" --set-basic "$image" /file.bin \
    - - - "$timestamp" 0x1
cluster_size=$("$driver" --probe "$image" |
    awk '/^cluster size:/ { print $3 }')

"$driver" --set-ea "$image" /file.bin 0 Author "$ada"
test "$("$driver" --ea "$image" /file.bin)" = \
    "- Author 3 416461"
printf '\024\000\000\000\000\006\003\000Author\000Ada\000\000' \
    >"$expected_small"
"$ntfscat" -a 0xe0 "$image" /file.bin |
    cmp - "$expected_small"

basic=$("$driver" --basic "$image" /file.bin)
set -- $basic
test "$#" -eq 5
test "$4" -gt "$timestamp"
test "$5" = 0x00000021

# Fill the base record with independently written NTFS-3G streams. Stop as
# soon as the native attribute list appears so the EA case stays compact.
while test "$stream_index" -le 32
do
    stream_name=$(printf 'ea-list-%02d' "$stream_index")
    "$ntfscp" -f -q -N "$stream_name" \
        "$image" "$source" /file.bin
    "$ntfsinfo" -F /file.bin "$image" >"$report"
    if grep -q 'Dumping attribute \$ATTRIBUTE_LIST' "$report"
    then
        break
    fi
    stream_index=$((stream_index + 1))
done
test "$stream_index" -le 32
base_record=$(awk '
    /^Dumping Inode / {
        print $3
        exit
    }
' "$report")
test "$(awk '
    /^Dumping attribute \$EA \(0xe0\)/ {
        print $8
        exit
    }
' "$report")" = "$base_record"

# Replacement is case-insensitive and preserves the existing name spelling.
"$driver" --set-ea "$image" /file.bin 0 author "$grace"
"$ntfsinfo" -F /file.bin "$image" >"$report"
information_owner=$(awk '
    /^Dumping attribute \$EA_INFORMATION/ {
        print $8
        exit
    }
' "$report")
ea_owner=$(awk '
    /^Dumping attribute \$EA \(0xe0\)/ {
        print $8
        exit
    }
' "$report")
test "$information_owner" = "$ea_owner"
test "$ea_owner" != "$base_record"
free_before=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')

"$driver" --set-ea "$image" /file.bin 0x80 Blob "$blob"
summary=$("$driver" --ea "$image" /file.bin |
    awk '{ print $1 " " $2 " " $3 }')
test "$summary" = "$(printf '%s\n%s' \
    '- Author 5' '! Blob 32768')"

# Build the exact native $EA value independently and let NTFS-3G read it.
{
    printf '\024\000\000\000\000\006\005\000Author\000Grace'
    printf '\020\200\000\000\200\004\000\200Blob\000'
    cat "$blob"
    printf '\000\000\000'
} >"$expected_all"
"$ntfscat" -a 0xe0 "$image" /file.bin |
    cmp - "$expected_all"

packed_size=$("$driver" --list-info "$image" / |
    awk '$4 == "file.bin" { print $5 }')
test "$packed_size" = 32793

"$ntfsinfo" -F /file.bin "$image" >"$report"
grep -q 'Packed EA length:[[:space:]]*32793 ' "$report"
grep -q 'NEED_EA count:[[:space:]]*1 ' "$report"
grep -q 'Unpacked EA length:[[:space:]]*32804 ' "$report"
sed -n '/Dumping attribute \$EA (0xe0)/,/End of inode/p' "$report" |
    grep -q 'Resident:[[:space:]]*No'

free_large=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
expected_clusters=$(((32804 + cluster_size - 1) / cluster_size))
test $((free_before - free_large)) -eq "$expected_clusters"

# Removing one entry rewrites a valid remaining nonresident list.
"$driver" --remove-ea "$image" /file.bin AUTHOR
{
    printf '\020\200\000\000\200\004\000\200Blob\000'
    cat "$blob"
    printf '\000\000\000'
} >"$expected_blob"
"$ntfscat" -a 0xe0 "$image" /file.bin |
    cmp - "$expected_blob"
test "$("$driver" --ea "$image" /file.bin |
    awk '{ print $1 " " $2 " " $3 }')" = "! Blob 32768"

# Removing the last entry removes both native attributes and releases runs.
"$driver" --remove-ea "$image" /file.bin Blob
test -z "$("$driver" --ea "$image" /file.bin)"
if "$ntfscat" -a 0xe0 "$image" /file.bin >/dev/null 2>&1
then
    echo '$EA remained after removing the last entry' >&2
    exit 1
fi
free_after=$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')
test "$free_after" -eq "$free_before"
test "$("$driver" --list-info "$image" / |
    awk '$4 == "file.bin" { print $5 }')" = 0

# Creating the pair after the list already exists must publish both entries
# together in one extension and remove them without leaking that record.
"$driver" --set-ea "$image" /file.bin 0 Recreated "$ada"
test "$("$driver" --ea "$image" /file.bin |
    awk '{ print $2 " " $3 }')" = "Recreated 3"
"$ntfsinfo" -F /file.bin "$image" >"$report"
information_owner=$(awk '
    /^Dumping attribute \$EA_INFORMATION/ {
        print $8
        exit
    }
' "$report")
ea_owner=$(awk '
    /^Dumping attribute \$EA \(0xe0\)/ {
        print $8
        exit
    }
' "$report")
test "$information_owner" = "$ea_owner"
test "$ea_owner" != "$base_record"
"$driver" --remove-ea "$image" /file.bin Recreated
test -z "$("$driver" --ea "$image" /file.bin)"
test "$("$driver" --probe "$image" |
    awk '/^free clusters:/ { print $3 }')" -eq "$free_before"

# A resident EA pair can itself be the space reclaimed while the core creates
# the initial list for a new named stream.
"$ntfscp" -f -q "$image" "$source" /initial.bin
"$driver" --set-ea "$image" /initial.bin 0 Fill "$fill"
"$ntfsinfo" -F /initial.bin "$image" >"$report"
if grep -q 'Dumping attribute \$ATTRIBUTE_LIST' "$report"
then
    echo 'initial-EA fixture unexpectedly has an attribute list' >&2
    exit 1
fi
sed -n '/Dumping attribute \$EA (0xe0)/,/End of inode/p' "$report" |
    grep -q 'Resident:[[:space:]]*Yes'
"$driver" --write "$image" /initial.bin:new 0 "$source"
"$driver" --cat "$image" /initial.bin:new | cmp - "$source"
"$ntfscat" -n new "$image" /initial.bin | cmp - "$source"
"$ntfsinfo" -F /initial.bin "$image" >"$report"
grep -q 'Dumping attribute \$ATTRIBUTE_LIST' "$report"
base_record=$(awk '/^Dumping Inode / { print $3; exit }' "$report")
information_owner=$(awk '
    /^Dumping attribute \$EA_INFORMATION/ {
        print $8
        exit
    }
' "$report")
ea_owner=$(awk '
    /^Dumping attribute \$EA \(0xe0\)/ {
        print $8
        exit
    }
' "$report")
test "$information_owner" = "$ea_owner"
test "$ea_owner" != "$base_record"
"$driver" --set-ea "$image" /initial.bin 0 fill "$grace"
test "$("$driver" --ea "$image" /initial.bin |
    awk '{ print $2 " " $3 }')" = "Fill 5"
"$driver" --remove-ea "$image" /initial.bin Fill
test -z "$("$driver" --ea "$image" /initial.bin)"

"$ntfsfix" -n "$image" >/dev/null
