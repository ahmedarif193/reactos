#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfscp=$3
ntfscat=$4
ntfsfix=$5
ntfsinfo=$6
ntfscluster=$7

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-ads.XXXXXX")
if test "${NTFSLIB_KEEP_TEST_DIR:-0}" = 1
then
    trap 'printf "preserved ADS fixture: %s\n" "$workdir" >&2' \
        EXIT HUP INT TERM
else
    trap 'rm -rf "$workdir"' EXIT HUP INT TERM
fi
image="$workdir/ads.ntfs"
base="$workdir/base.bin"
tiny="$workdir/tiny.bin"
tiny_patch="$workdir/tiny-patch.bin"
tiny_expected="$workdir/tiny-expected.bin"
tiny_truncated="$workdir/tiny-truncated.bin"
alpha_promoted="$workdir/alpha-promoted.bin"
bulk="$workdir/bulk.bin"
bulk_patch="$workdir/bulk-patch.bin"
bulk_expected="$workdir/bulk-expected.bin"
created_resident="$workdir/created-resident.bin"
created_bulk="$workdir/created-bulk.bin"
created_bulk_expected="$workdir/created-bulk-expected.bin"
empty="$workdir/empty.bin"
list_source="$workdir/list-source.bin"
list_modified="$workdir/list-modified.bin"
list_patch="$workdir/list-patch.bin"
extension_bulk="$workdir/extension-bulk.bin"
extension_bulk_expected="$workdir/extension-bulk-expected.bin"
extension_bulk_patch="$workdir/extension-bulk-patch.bin"
core_extent_expected="$workdir/core-extent-expected.bin"
mapping_expected="$workdir/mapping-expected.bin"
mapping_patch="$workdir/mapping-patch.bin"
mft_bitmap_before="$workdir/mft-before.bitmap"
mft_bitmap_after="$workdir/mft-after.bitmap"
mapping_split_info="$workdir/mapping-split.info"
mapping_collapsed_info="$workdir/mapping-collapsed.info"
list_layout_before="$workdir/list-layout-before.txt"
list_layout_after="$workdir/list-layout-after.txt"
list_runs_before="$workdir/list-runs-before.txt"
core_extent_runs_after="$workdir/core-extent-runs-after.txt"
volume_bitmap_after="$workdir/volume-after.bitmap"
stream_info="$workdir/stream-info.txt"
streams_actual="$workdir/streams-actual.txt"
streams_expected="$workdir/streams-expected.txt"
streams_expected_updated="$workdir/streams-expected-updated.txt"
streams_expected_sorted="$workdir/streams-expected-sorted.txt"

printf 'unnamed-data\n' >"$base"
printf 'resident-alternate-stream\n' >"$tiny"
dd if=/dev/zero bs=8192 count=1 status=none |
    tr '\000' P >"$tiny_patch"
cp "$tiny" "$tiny_expected"
dd if="$tiny_patch" of="$tiny_expected" bs=1 seek=26 conv=notrunc status=none
dd if="$tiny_expected" of="$tiny_truncated" \
    bs=1 count=13 status=none

dd if=/dev/zero bs=65536 count=1 status=none |
    tr '\000' A >"$bulk"
dd if=/dev/zero bs=8192 count=1 status=none |
    tr '\000' B >"$bulk_patch"
cp "$bulk" "$bulk_expected"
truncate -s 70000 "$bulk_expected"
dd if="$bulk_patch" of="$bulk_expected" bs=1 seek=70000 conv=notrunc status=none
printf 'created-by-ntfslib\n' >"$created_resident"
printf 'attribute-list-stream\n' >"$list_source"
dd if=/dev/zero bs=16384 count=1 status=none |
    tr '\000' X >"$extension_bulk"
dd if=/dev/zero bs=4100 count=1 status=none |
    tr '\000' Y >"$extension_bulk_patch"
cp "$extension_bulk" "$extension_bulk_expected"
dd if="$extension_bulk_patch" \
    of="$extension_bulk_expected" \
    bs=1 seek=4090 conv=notrunc status=none
dd if=/dev/zero bs=8192 count=1 status=none |
    tr '\000' C >"$created_bulk"
dd if=/dev/zero bs=4096 count=1 status=none >"$created_bulk_expected"
dd if="$created_bulk" of="$created_bulk_expected" \
    bs=1 seek=4096 conv=notrunc status=none
printf Q >"$mapping_patch"
truncate -s 0 "$empty"

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L ADS_TEST "$image"
"$ntfscp" -f -q "$image" "$base" /streams.bin
"$ntfscp" -f -q -N tiny "$image" "$tiny" /streams.bin
"$ntfscp" -f -q -N bulk "$image" "$bulk" /streams.bin

"$driver" --cat "$image" '/streams.bin::$DATA' | cmp - "$base"
"$driver" --cat "$image" '/streams.bin:tiny' | cmp - "$tiny"
"$driver" --cat "$image" '/streams.bin:bulk:$DATA' | cmp - "$bulk"
"$ntfscat" -n tiny "$image" /streams.bin | cmp - "$tiny"
"$ntfscat" -n bulk "$image" /streams.bin | cmp - "$bulk"

if "$ntfscat" -n alpha "$image" /streams.bin >/dev/null 2>&1
then
    echo "fixture unexpectedly contains the alpha stream" >&2
    exit 1
fi
"$driver" --write "$image" /streams.bin:alpha 0 "$created_resident"
"$driver" --write "$image" /streams.bin:omega 4096 "$created_bulk"
"$driver" --cat "$image" /streams.bin:alpha | cmp - "$created_resident"
"$driver" --cat "$image" /streams.bin:omega | cmp - "$created_bulk_expected"
"$ntfscat" -n alpha "$image" /streams.bin | cmp - "$created_resident"
"$ntfscat" -n omega "$image" /streams.bin | cmp - "$created_bulk_expected"

"$driver" --write "$image" /streams.bin:tiny 26 "$tiny_patch"
"$driver" --write "$image" /streams.bin:bulk 70000 "$bulk_patch"
"$driver" --cat "$image" /streams.bin:tiny | cmp - "$tiny_expected"
"$driver" --cat "$image" /streams.bin:bulk | cmp - "$bulk_expected"
"$ntfscat" -n tiny "$image" /streams.bin | cmp - "$tiny_expected"
"$ntfscat" -n bulk "$image" /streams.bin | cmp - "$bulk_expected"

"$driver" --truncate "$image" /streams.bin:tiny 13
"$driver" --truncate "$image" /streams.bin:bulk 0
"$driver" --cat "$image" /streams.bin:tiny | cmp - "$tiny_truncated"
"$driver" --cat "$image" /streams.bin:bulk | cmp - "$empty"
"$ntfscat" -n tiny "$image" /streams.bin | cmp - "$tiny_truncated"
"$ntfscat" -n bulk "$image" /streams.bin | cmp - "$empty"

if "$driver" --cat "$image" '/streams.bin:tiny/child' >/dev/null 2>&1
then
    echo "accepted an ADS on a non-final path component" >&2
    exit 1
fi
if "$driver" --cat "$image" '/streams.bin:tiny:$BOGUS' >/dev/null 2>&1
then
    echo "accepted an unknown attribute type" >&2
    exit 1
fi

# Have the core create the initial $ATTRIBUTE_LIST when the base record fills,
# then let NTFS-3G extend that layout. This preserves an independent writer in
# the same single compact stream/interoperability test.
base_size=$(wc -c <"$base")
tiny_size=$(wc -c <"$tiny_truncated")
bulk_size=$(wc -c <"$empty")
alpha_size=$(wc -c <"$created_resident")
omega_size=$(wc -c <"$created_bulk_expected")
list_size=$(wc -c <"$list_source")
{
    printf '::$DATA %s\n' "$base_size"
    printf ':tiny:$DATA %s\n' "$tiny_size"
    printf ':bulk:$DATA %s\n' "$bulk_size"
    printf ':alpha:$DATA %s\n' "$alpha_size"
    printf ':omega:$DATA %s\n' "$omega_size"
} >"$streams_expected"

"$ntfsinfo" -F /streams.bin "$image" >"$stream_info"
if grep -q 'Dumping attribute \$ATTRIBUTE_LIST' "$stream_info"
then
    echo "fixture unexpectedly has an attribute list before core growth" >&2
    exit 1
fi

core_index=1
core_count=0
while test "$core_index" -le 12
do
    stream_name=$(printf 'core-list-%02d' "$core_index")
    "$driver" --write "$image" \
        "/streams.bin:$stream_name" 0 \
        "$list_source"
    printf ':%s:$DATA %s\n' \
        "$stream_name" "$list_size" \
        >>"$streams_expected"
    core_count=$core_index
    "$ntfsinfo" -F /streams.bin \
        "$image" >"$stream_info"
    if grep -q \
        'Dumping attribute \$ATTRIBUTE_LIST' \
        "$stream_info"
    then
        break
    fi
    core_index=$((core_index + 1))
done
if ! grep -q \
    'Dumping attribute \$ATTRIBUTE_LIST' \
    "$stream_info"
then
    echo "ntfslib did not create an initial attribute list" >&2
    exit 1
fi
core_base_record=$(awk '
    /^Dumping Inode / {
        print $3
        exit
    }
' "$stream_info")
if ! awk -v base_record="$core_base_record" '
    /^Dumping attribute \$DATA / &&
        $8 != base_record {
        found = 1
    }
    END {
        exit found ? 0 : 1
    }
' "$stream_info"
then
    echo "ntfslib did not relocate a stream into an extension record" >&2
    exit 1
fi

ntfs_stream_count=$((24 - core_count))
stream_index=1
while test "$stream_index" -le "$ntfs_stream_count"
do
    stream_name=$(printf 'list-%02d' "$stream_index")
    "$ntfscp" -f -q -N "$stream_name" \
        "$image" "$list_source" /streams.bin
    printf ':%s:$DATA %s\n' \
        "$stream_name" "$list_size" \
        >>"$streams_expected"
    stream_index=$((stream_index + 1))
done
"$ntfscp" -f -q -N extent-bulk \
    "$image" "$extension_bulk" /streams.bin
printf ':extent-bulk:$DATA %s\n' \
    "$(wc -c <"$extension_bulk")" \
    >>"$streams_expected"

"$ntfsinfo" -F /streams.bin "$image" >"$stream_info"
grep -q 'Dumping attribute \$ATTRIBUTE_LIST' "$stream_info"
base_record=$(awk '/^Dumping Inode / { print $3; exit }' "$stream_info")
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / &&
        $8 != base_record {
        found = 1
    }
    END {
        exit found ? 0 : 1
    }
' "$stream_info"
then
    echo "fixture did not place a data stream in an extension record" >&2
    exit 1
fi
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        resident = ""
        next
    }
    /^[[:space:]]*Resident:/ {
        resident = $2
        next
    }
    /Attribute name:[[:space:]]*'\''extent-bulk'\''/ &&
        record != base_record &&
        resident == "No" {
        found = 1
    }
    END {
        exit found ? 0 : 1
    }
' "$stream_info"
then
    echo "fixture did not place extent-bulk nonresident in an extension record" >&2
    exit 1
fi

# Modify one resident stream that NTFS-3G placed in an extension record.
# The stream owner must be committed there while last-write/change remain
# file-wide metadata in the base record.
extension_name=$(awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        next
    }
    /Attribute name:/ {
        name = substr($3, 2, length($3) - 2)
        if (record != base_record &&
            name ~ /^list-/) {
            print name
            exit
        }
    }
' "$stream_info")
test -n "$extension_name"
printf 'Z' >"$list_patch"
cp "$list_source" "$list_modified"
dd if="$list_patch" of="$list_modified" \
    bs=1 seek=0 conv=notrunc status=none

set -- $("$driver" --basic "$image" /streams.bin)
write_time_before=$3
change_time_before=$4
"$driver" --write "$image" \
    "/streams.bin:$extension_name" 0 "$list_patch"
"$driver" --cat "$image" \
    "/streams.bin:$extension_name" |
    cmp - "$list_modified"
"$ntfscat" -n "$extension_name" \
    "$image" /streams.bin |
    cmp - "$list_modified"
set -- $("$driver" --basic "$image" /streams.bin)
test "$3" -gt "$write_time_before"
test "$4" -gt "$change_time_before"

"$driver" --truncate "$image" \
    "/streams.bin:$extension_name" 7
truncate -s 7 "$list_modified"
"$ntfscat" -n "$extension_name" \
    "$image" /streams.bin |
    cmp - "$list_modified"
"$driver" --truncate "$image" \
    "/streams.bin:$extension_name" 12
truncate -s 12 "$list_modified"
"$driver" --cat "$image" \
    "/streams.bin:$extension_name" |
    cmp - "$list_modified"
"$ntfscat" -n "$extension_name" \
    "$image" /streams.bin |
    cmp - "$list_modified"

"$driver" --allocate "$image" \
    "/streams.bin:$extension_name" 4096
"$ntfscat" -n "$extension_name" \
    "$image" /streams.bin |
    cmp - "$list_modified"

"$driver" --write "$image" \
    "/streams.bin:$extension_name" 26 "$tiny_patch"
dd if="$tiny_patch" of="$list_modified" \
    bs=1 seek=26 conv=notrunc status=none
"$driver" --cat "$image" \
    "/streams.bin:$extension_name" |
    cmp - "$list_modified"
"$ntfscat" -n "$extension_name" \
    "$image" /streams.bin |
    cmp - "$list_modified"
extension_size=$(wc -c <"$list_modified")
awk -v target=":$extension_name:\$DATA" \
    -v extension_size="$extension_size" '
    $1 == target {
        $2 = extension_size
    }
    {
        print
    }
' "$streams_expected" >"$streams_expected_updated"
mv "$streams_expected_updated" "$streams_expected"

"$driver" --write "$image" \
    /streams.bin:extent-bulk 4090 \
    "$extension_bulk_patch"
"$driver" --cat "$image" \
    /streams.bin:extent-bulk |
    cmp - "$extension_bulk_expected"
"$ntfscat" -n extent-bulk \
    "$image" /streams.bin |
    cmp - "$extension_bulk_expected"

# Promote a resident stream that remains in the base record after
# $ATTRIBUTE_LIST creation. Its existing list entry continues to identify the
# same record and attribute ID while the resident variant becomes a
# nonresident mapping.
"$ntfsinfo" -F /streams.bin "$image" >"$stream_info"
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        resident = ""
        next
    }
    /^[[:space:]]*Resident:/ {
        resident = $2
        next
    }
    /Attribute name:[[:space:]]*'\''alpha'\''/ {
        found = record == base_record &&
            resident == "Yes"
    }
    END {
        exit found ? 0 : 1
    }
' "$stream_info"
then
    echo "alpha is not base-resident after attribute-list creation" >&2
    exit 1
fi
cp "$created_resident" "$alpha_promoted"
dd if="$tiny_patch" of="$alpha_promoted" \
    bs=1 seek=4090 conv=notrunc status=none
"$driver" --write "$image" \
    /streams.bin:alpha 4090 "$tiny_patch"
"$driver" --cat "$image" /streams.bin:alpha |
    cmp - "$alpha_promoted"
"$ntfscat" -n alpha "$image" /streams.bin |
    cmp - "$alpha_promoted"
"$ntfsinfo" -F /streams.bin "$image" >"$stream_info"
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        resident = ""
        next
    }
    /^[[:space:]]*Resident:/ {
        resident = $2
        next
    }
    /Attribute name:[[:space:]]*'\''alpha'\''/ {
        count++
        valid = record == base_record &&
            resident == "No"
    }
    END {
        exit count == 1 && valid ? 0 : 1
    }
' "$stream_info"
then
    echo "base-resident stream did not promote in the attribute-list file" >&2
    exit 1
fi
alpha_size=$(wc -c <"$alpha_promoted")
awk -v alpha_size="$alpha_size" '
    $1 == ":alpha:$DATA" {
        $2 = alpha_size
    }
    {
        print
    }
' "$streams_expected" >"$streams_expected_updated"
mv "$streams_expected_updated" "$streams_expected"

# Create a new nonresident stream after NTFS-3G has split the file across
# extension records. This covers the core allocator, extension-record
# initialization, $MFT bitmap update, and ordered $ATTRIBUTE_LIST insertion
# in this same compact interoperability test.
"$driver" --write "$image" \
    /streams.bin:core-extent 0 "$created_bulk"
"$driver" --cat "$image" \
    /streams.bin:core-extent |
    cmp - "$created_bulk"
"$ntfscat" -n core-extent \
    "$image" /streams.bin |
    cmp - "$created_bulk"

# Grow, shrink, preallocate, and logically regrow the new extension-owned
# stream. The host-side truncate supplies the same zero tail NTFS must expose
# beyond initialized data.
cp "$created_bulk" "$core_extent_expected"
dd if="$extension_bulk_patch" \
    of="$core_extent_expected" \
    bs=1 seek=8190 conv=notrunc status=none
"$driver" --write "$image" \
    /streams.bin:core-extent 8190 \
    "$extension_bulk_patch"
"$ntfscat" -n core-extent \
    "$image" /streams.bin |
    cmp - "$core_extent_expected"

"$driver" --truncate "$image" \
    /streams.bin:core-extent 5000
truncate -s 5000 "$core_extent_expected"
"$driver" --allocate "$image" \
    /streams.bin:core-extent 16384
"$driver" --truncate "$image" \
    /streams.bin:core-extent 10000
truncate -s 10000 "$core_extent_expected"
"$driver" --allocate "$image" \
    /streams.bin:core-extent 12288
"$driver" --cat "$image" \
    /streams.bin:core-extent |
    cmp - "$core_extent_expected"
"$ntfscat" -n core-extent \
    "$image" /streams.bin |
    cmp - "$core_extent_expected"

printf ':core-extent:$DATA %s\n' \
    "$(wc -c <"$core_extent_expected")" \
    >>"$streams_expected"

"$ntfsinfo" -F /streams.bin "$image" >"$stream_info"
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        resident = ""
        next
    }
    /^[[:space:]]*Resident:/ {
        resident = $2
        next
    }
    /Attribute name:[[:space:]]*'\''core-extent'\''/ &&
        record != base_record &&
        resident == "No" {
        found = 1
    }
    END {
        exit found ? 0 : 1
    }
' "$stream_info"
then
    echo "ntfslib did not create core-extent nonresident in an extension record" >&2
    exit 1
fi

# Force one base-owned stream's mapping pairs across an MFT-record boundary
# without a large fixture: grow a sparse tail, then allocate every other
# cluster with one-byte writes. The shared writer must publish ordered
# continuation entries, remain byte-identical to NTFS-3G, and collapse the
# stream back into the base record while freeing its mapping-only MFT record.
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        next
    }
    /Attribute name:[[:space:]]*'\''omega'\''/ {
        found = record == base_record
    }
    END {
        exit found ? 0 : 1
    }
' "$stream_info"
then
    echo "omega is not base-owned before mapping continuation" >&2
    exit 1
fi

mapping_cluster_size=$("$driver" --probe "$image" |
    awk '/^cluster size:/ { print $3 }')
mapping_initial_size=$(wc -c <"$created_bulk_expected")
mapping_cluster=$(((mapping_initial_size +
    mapping_cluster_size - 1) /
    mapping_cluster_size + 1))
mapping_last_cluster=$((mapping_cluster + 2 * 63))
mapping_size=$(((mapping_last_cluster + 1) *
    mapping_cluster_size))
cp "$created_bulk_expected" "$mapping_expected"
truncate -s "$mapping_size" "$mapping_expected"
"$driver" --set-sparse "$image" /streams.bin:omega 1
"$driver" --truncate "$image" \
    /streams.bin:omega "$mapping_size"
while test "$mapping_cluster" -le "$mapping_last_cluster"
do
    mapping_offset=$((mapping_cluster *
        mapping_cluster_size))
    "$driver" --write "$image" \
        /streams.bin:omega \
        "$mapping_offset" \
        "$mapping_patch"
    dd if="$mapping_patch" \
        of="$mapping_expected" \
        bs=1 seek="$mapping_offset" \
        conv=notrunc status=none
    mapping_cluster=$((mapping_cluster + 2))
done
"$driver" --cat "$image" /streams.bin:omega |
    cmp - "$mapping_expected"
"$ntfscat" -n omega "$image" /streams.bin |
    cmp - "$mapping_expected"

"$ntfsinfo" -F /streams.bin "$image" >"$mapping_split_info"
continuation_record=$(awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        next
    }
    /Attribute name:[[:space:]]*'\''omega'\''/ &&
        record != base_record {
        print record
        exit
    }
' "$mapping_split_info")
if test -z "$continuation_record"
then
    echo "omega mapping pairs did not enter a continuation record" >&2
    exit 1
fi

"$ntfscat" -i 0 -a 0xb0 "$image" >"$mft_bitmap_before"
bitmap_byte=$(od -An -tu1 \
    -j $((continuation_record / 8)) -N 1 \
    "$mft_bitmap_before" | tr -d '[:space:]')
test $((bitmap_byte &
        (1 << (continuation_record % 8)))) -ne 0

mapping_size=0
"$driver" --truncate "$image" \
    /streams.bin:omega "$mapping_size"
truncate -s "$mapping_size" "$mapping_expected"
"$driver" --cat "$image" /streams.bin:omega |
    cmp - "$mapping_expected"
"$ntfscat" -n omega "$image" /streams.bin |
    cmp - "$mapping_expected"

"$ntfsinfo" -F /streams.bin "$image" >"$mapping_collapsed_info"
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        resident = ""
        next
    }
    /^[[:space:]]*Resident:/ {
        resident = $2
        next
    }
    /Attribute name:[[:space:]]*'\''omega'\''/ {
        count++
        valid = record == base_record &&
            resident == "Yes"
    }
    END {
        exit count == 1 && valid ? 0 : 1
    }
' "$mapping_collapsed_info"
then
    echo "omega mapping continuation did not collapse into the base record" >&2
    exit 1
fi

"$ntfscat" -i 0 -a 0xb0 "$image" >"$mft_bitmap_after"
bitmap_byte=$(od -An -tu1 \
    -j $((continuation_record / 8)) -N 1 \
    "$mft_bitmap_after" | tr -d '[:space:]')
test $((bitmap_byte &
        (1 << (continuation_record % 8)))) -eq 0

awk -v mapping_size="$mapping_size" '
    $1 == ":omega:$DATA" {
        $2 = mapping_size
    }
    {
        print
    }
' "$streams_expected" >"$streams_expected_updated"
mv "$streams_expected_updated" "$streams_expected"

read_attribute_list_sizes()
{
    "$ntfsinfo" -F /streams.bin \
        "$image" >"$stream_info"
    awk '
        /^Dumping attribute \$ATTRIBUTE_LIST / {
            in_list = 1
            next
        }
        /^Dumping attribute / {
            in_list = 0
        }
        in_list && /Data size:/ {
            data_size = $3
        }
        in_list && /Allocated size:/ {
            allocated_size = $3
        }
        END {
            if (data_size != "" &&
                allocated_size != "") {
                print data_size, allocated_size
            }
        }
    ' "$stream_info"
}

# First let the independent NTFS-3G writer cross one list-allocation boundary.
# Its allocator leaves the two list clusters noncontiguous in this fixture,
# providing an existing fragmented bootstrap mapping for the core to compact.
padding_count=0
mft_valid_before=$("$driver" --volume-data "$image" |
    awk '/^mft-valid-bytes / { print $2 }')
test -n "$mft_valid_before"
set -- $(read_attribute_list_sizes)
if test "$#" -ne 2 ||
    test "$1" -gt "$2"
then
    echo "could not read the initial attribute-list allocation" >&2
    exit 1
fi
initial_list_allocation=$2
while test "$2" -eq "$initial_list_allocation"
do
    if test "$padding_count" -ge 160
    then
        echo "NTFS-3G did not grow the attribute list" >&2
        exit 1
    fi
    padding_count=$((padding_count + 1))
    stream_name=$(printf 'pad-%03d' "$padding_count")
    "$ntfscp" -f -q -N "$stream_name" \
        "$image" "$mapping_patch" /streams.bin
    printf ':%s:$DATA 1\n' \
        "$stream_name" >>"$streams_expected"
    set -- $(read_attribute_list_sizes)
    if test "$#" -ne 2 ||
        test "$1" -gt "$2"
    then
        echo "could not read the attribute-list allocation" >&2
        exit 1
    fi
done

"$ntfscluster" -F /streams.bin \
    "$image" >"$list_layout_before"
awk '
    /^    0x20 - non-resident/ {
        in_list = 1
        found = 1
        next
    }
    in_list && /^    0x[0-9a-f]+ -/ {
        in_list = 0
    }
    in_list && NF == 3 &&
        $1 ~ /^[0-9]+$/ &&
        $2 ~ /^[0-9]+$/ &&
        $3 ~ /^[0-9]+$/ {
        print $2, $3
    }
    END {
        if (!found)
            exit 1
    }
' "$list_layout_before" >"$list_runs_before"
if test "$(wc -l <"$list_runs_before")" -lt 2
then
    echo "NTFS-3G did not fragment the attribute-list mapping" >&2
    exit 1
fi

# Fill the fragmented list to less than one core-extent entry from its next
# allocation boundary. Publishing that continuation must relocate the list to
# a bounded one-run mapping and keep the old clusters until commit.
while :
do
    set -- $(read_attribute_list_sizes)
    if test "$#" -ne 2 ||
        test "$1" -gt "$2"
    then
        echo "could not read the attribute-list allocation" >&2
        exit 1
    fi
    list_data_before=$1
    list_allocation_before=$2
    if test $((list_allocation_before -
            list_data_before)) -lt 48
    then
        break
    fi
    if test "$padding_count" -ge 260
    then
        echo "attribute list did not approach its allocation boundary" >&2
        exit 1
    fi

    padding_count=$((padding_count + 1))
    stream_name=$(printf 'pad-%03d' "$padding_count")
    "$driver" --write "$image" \
        "/streams.bin:$stream_name" 0 \
        "$mapping_patch"
    printf ':%s:$DATA 1\n' \
        "$stream_name" >>"$streams_expected"
done

# Repeat the same bounded mapping-pair stress with a stream whose VCN-zero
# attribute is owned by an extension record. A freshly allocated extent has
# more mapping-pair room than the crowded base record, so use 192 alternating
# physical clusters to cross its MFT-record boundary. This exact packed layout
# also leaves no room for core-extent's larger sparse header, forcing its
# VCN-zero attribute to relocate before the mapping is split.
"$ntfsinfo" -F /streams.bin "$image" >"$stream_info"
core_extent_owner_before=$(awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        next
    }
    /Attribute name:[[:space:]]*'\''core-extent'\''/ &&
        record != base_record {
        print record
        exit
    }
' "$stream_info")
if test -z "$core_extent_owner_before"
then
    echo "could not identify core-extent owner before relocation" >&2
    exit 1
fi
extension_mapping_initial_size=$(wc -c <"$core_extent_expected")
extension_mapping_cluster=$(((extension_mapping_initial_size +
    mapping_cluster_size - 1) /
    mapping_cluster_size + 1))
extension_mapping_last_cluster=$((extension_mapping_cluster + 2 * 191))
extension_mapping_size=$(((extension_mapping_last_cluster + 1) *
    mapping_cluster_size))
truncate -s "$extension_mapping_size" "$core_extent_expected"
"$driver" --set-sparse "$image" /streams.bin:core-extent 1
"$driver" --truncate "$image" \
    /streams.bin:core-extent "$extension_mapping_size"
while test "$extension_mapping_cluster" \
    -le "$extension_mapping_last_cluster"
do
    extension_mapping_offset=$((extension_mapping_cluster *
        mapping_cluster_size))
    "$driver" --write "$image" \
        /streams.bin:core-extent \
        "$extension_mapping_offset" \
        "$mapping_patch"
    dd if="$mapping_patch" \
        of="$core_extent_expected" \
        bs=1 seek="$extension_mapping_offset" \
        conv=notrunc status=none
    extension_mapping_cluster=$((extension_mapping_cluster + 2))
done
"$driver" --cat "$image" /streams.bin:core-extent |
    cmp - "$core_extent_expected"
"$ntfscat" -n core-extent "$image" /streams.bin |
    cmp - "$core_extent_expected"

"$ntfsinfo" -F /streams.bin "$image" >"$mapping_split_info"
set -- $(awk '
    /^Dumping attribute \$ATTRIBUTE_LIST / {
        in_list = 1
        next
    }
    /^Dumping attribute / {
        in_list = 0
    }
    in_list && /Data size:/ {
        data_size = $3
    }
    in_list && /Allocated size:/ {
        allocated_size = $3
    }
    END {
        if (data_size != "" &&
            allocated_size != "") {
            print data_size, allocated_size
        }
    }
' "$mapping_split_info")
if test "$#" -ne 2
then
    echo "could not read the grown attribute list" >&2
    exit 1
fi
list_data_after=$1
list_allocation_after=$2
if test "$list_data_after" \
        -le "$list_allocation_before" ||
    test "$list_allocation_after" \
        -le "$list_allocation_before"
then
    echo "mapping continuation did not grow the attribute list" >&2
    exit 1
fi

"$ntfscluster" -F /streams.bin \
    "$image" >"$list_layout_after"
set -- $(awk '
    /^    0x20 - non-resident/ {
        in_list = 1
        found = 1
        next
    }
    in_list && /^    0x[0-9a-f]+ -/ {
        in_list = 0
    }
    in_list && NF == 3 &&
        $1 ~ /^[0-9]+$/ &&
        $2 ~ /^[0-9]+$/ &&
        $3 ~ /^[0-9]+$/ {
        count++
        clusters += $3
    }
    END {
        if (found)
            print count, clusters
    }
' "$list_layout_after")
if test "$#" -ne 2 ||
    test "$1" -ne 1 ||
    test $((list_allocation_after %
            mapping_cluster_size)) -ne 0 ||
    test "$2" -ne $((list_allocation_after /
            mapping_cluster_size))
then
    echo "attribute-list mapping was not compacted to one run" >&2
    exit 1
fi

# Remaining sparse writes can immediately reuse a released low cluster. Every
# superseded list cluster must therefore either be clear in $Bitmap or appear
# in the final target-stream mapping; require at least one literal clear bit
# so the reclamation path itself is independently observed.
"$ntfscat" -i 6 "$image" >"$volume_bitmap_after"
"$driver" --retrieval-pointers "$image" \
    /streams.bin:core-extent 0 \
    >"$core_extent_runs_after"
released_list_clusters=0
while read -r old_lcn old_length
do
    old_cluster=$old_lcn
    old_cluster_end=$((old_lcn + old_length))
    while test "$old_cluster" -lt "$old_cluster_end"
    do
        bitmap_byte=$(od -An -tu1 \
            -j $((old_cluster / 8)) -N 1 \
            "$volume_bitmap_after" |
            tr -d '[:space:]')
        if test -z "$bitmap_byte"
        then
            echo "could not read the volume bitmap" >&2
            exit 1
        fi
        if test $((bitmap_byte &
                (1 << (old_cluster % 8)))) -eq 0
        then
            released_list_clusters=$((released_list_clusters + 1))
        elif ! awk -v lcn="$old_cluster" '
            $2 == lcn {
                found = 1
            }
            END {
                exit found ? 0 : 1
            }
        ' "$core_extent_runs_after"
        then
            echo "superseded attribute-list cluster was leaked" >&2
            exit 1
        fi
        old_cluster=$((old_cluster + 1))
    done
done <"$list_runs_before"
if test "$released_list_clusters" -eq 0
then
    echo "attribute-list reclamation was not observed" >&2
    exit 1
fi
mft_valid_after=$("$driver" --volume-data "$image" |
    awk '/^mft-valid-bytes / { print $2 }')
if test -z "$mft_valid_after" ||
    test "$mft_valid_after" -le "$mft_valid_before"
then
    echo "extension-stream stress did not grow the MFT" >&2
    exit 1
fi

set -- $(awk '
    /^Dumping attribute \$DATA / {
        record = $8
        next
    }
    /Attribute name:[[:space:]]*'\''core-extent'\''/ {
        if (!owner) {
            owner = record
        } else if (record != owner) {
            print owner, record
            exit
        }
    }
' "$mapping_split_info")
test "$#" -eq 2
extension_owner_record=$1
continuation_record=$2
test "$extension_owner_record" != "$base_record"
if test "$extension_owner_record" = "$core_extent_owner_before"
then
    echo "full VCN-zero owner was not relocated" >&2
    exit 1
fi

"$ntfscat" -i 0 -a 0xb0 "$image" >"$mft_bitmap_before"
bitmap_byte=$(od -An -tu1 \
    -j $((continuation_record / 8)) -N 1 \
    "$mft_bitmap_before" | tr -d '[:space:]')
test $((bitmap_byte &
        (1 << (continuation_record % 8)))) -ne 0

extension_mapping_size=0
"$driver" --truncate "$image" \
    /streams.bin:core-extent "$extension_mapping_size"
truncate -s "$extension_mapping_size" "$core_extent_expected"
"$driver" --cat "$image" /streams.bin:core-extent |
    cmp - "$core_extent_expected"
"$ntfscat" -n core-extent "$image" /streams.bin |
    cmp - "$core_extent_expected"

"$ntfsinfo" -F /streams.bin "$image" >"$mapping_collapsed_info"
if ! awk -v base_record="$base_record" '
    /^Dumping attribute \$DATA / {
        record = $8
        resident = ""
        next
    }
    /^[[:space:]]*Resident:/ {
        resident = $2
        next
    }
    /Attribute name:[[:space:]]*'\''core-extent'\''/ {
        count++
        valid = record != base_record &&
            resident == "Yes"
    }
    END {
        exit count == 1 && valid ? 0 : 1
    }
' "$mapping_collapsed_info"
then
    echo "extension-owned mapping continuation did not collapse" >&2
    exit 1
fi

"$ntfscat" -i 0 -a 0xb0 "$image" >"$mft_bitmap_after"
bitmap_byte=$(od -An -tu1 \
    -j $((continuation_record / 8)) -N 1 \
    "$mft_bitmap_after" | tr -d '[:space:]')
test $((bitmap_byte &
        (1 << (continuation_record % 8)))) -eq 0

awk -v mapping_size="$extension_mapping_size" '
    $1 == ":core-extent:$DATA" {
        $2 = mapping_size
    }
    {
        print
    }
' "$streams_expected" >"$streams_expected_updated"
mv "$streams_expected_updated" "$streams_expected"

"$driver" --streams "$image" /streams.bin |
    awk '{ print $1 " " $2 }' |
    sort >"$streams_actual"
sort "$streams_expected" >"$streams_expected_sorted"
cmp "$streams_actual" "$streams_expected_sorted"

if ! "$driver" --streams "$image" /streams.bin |
    awk '
        $1 == ":core-extent:$DATA" {
            found = ($2 == 0 && $3 == 0)
        }
        END {
            exit found ? 0 : 1
        }
    '
then
    echo "core-extent logical/allocation sizes are incorrect" >&2
    exit 1
fi

cluster_size=$("$driver" --probe "$image" |
    awk '/^cluster size:/ { print $3 }')
"$driver" --streams "$image" /streams.bin |
    awk -v cluster_size="$cluster_size" \
        -v padding_count="$padding_count" '
        NF != 3 ||
        $2 !~ /^[0-9]+$/ ||
        $3 !~ /^[0-9]+$/ ||
        ($3 != 0 &&
         $3 % cluster_size != 0) {
            exit 1
        }
        END {
            if (NR != 31 + padding_count)
                exit 1
        }
    '

"$ntfsfix" -n "$image" >/dev/null
