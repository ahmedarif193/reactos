#!/bin/sh
set -eu

frontend=$1
mkntfs=$2
ntfscp=$3
source_file=$4
resident_source=$5
empty_source=$6
ntfstruncate=$7
ntfsls=$8
ntfscat=$9
ntfsinfo=${10}
sparse_size=8388608

test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-image-test.XXXXXX")
image="$test_dir/test.ntfs"
listing="$test_dir/listing"
mft_bitmap="$test_dir/mft.bitmap"
mft_raw="$test_dir/mft.raw"
mft_expected="$test_dir/mft.expected"
mft_actual="$test_dir/mft.actual"
mft_meta="$test_dir/mft.meta"

cleanup()
{
    rm -f "$listing"
    rm -f "$mft_bitmap"
    rm -f "$mft_raw"
    rm -f "$mft_expected"
    rm -f "$mft_actual"
    rm -f "$mft_meta"
    rm -f "$image"
    rmdir "$test_dir"
}
trap cleanup EXIT HUP INT TERM

truncate -s 64M "$image"
"$mkntfs" -F -Q -q -L NTFSLIB_TEST "$image"
"$ntfscp" -f -q "$image" "$source_file" /hello.txt
"$ntfscp" -f -q "$image" "$resident_source" /resident-é.txt
"$ntfscp" -f -q "$image" "$empty_source" /empty.txt
"$ntfscp" -f -q "$image" "$empty_source" /sparse.bin

sparse_inode=$("$ntfsls" -i -p / "$image" |
    awk '$2 == "sparse.bin" { print $1 }')
test -n "$sparse_inode"
"$ntfstruncate" -f -q "$image" "$sparse_inode" 0x80 '' "$sparse_size"

"$frontend" --probe "$image" >/dev/null
"$frontend" --list "$image" / >"$listing"
grep -q 'hello.txt$' "$listing"
grep -q 'resident-é.txt$' "$listing"
grep -q 'empty.txt$' "$listing"
"$frontend" --cat "$image" /hello.txt | cmp - "$source_file"

# Volume geometry comes from the exact 64-bit boot-sector fields, while free
# space and $MFT valid length are independently checked through NTFS-3G.
volume_data=$("$frontend" --volume-data "$image")
volume_field()
{
    printf '%s\n' "$volume_data" |
        awk -v name="$1" '$1 == name { print $2; exit }'
}
boot_sector_size=$(od -An -tu2 -j 11 -N 2 "$image" |
    tr -d '[:space:]')
boot_sectors_per_cluster=$(od -An -tu1 -j 13 -N 1 "$image" |
    tr -d '[:space:]')
boot_sectors=$(od -An -tu8 -j 40 -N 8 "$image" |
    tr -d '[:space:]')
boot_mft_lcn=$(od -An -tu8 -j 48 -N 8 "$image" |
    tr -d '[:space:]')
boot_mftmirr_lcn=$(od -An -tu8 -j 56 -N 8 "$image" |
    tr -d '[:space:]')
boot_serial=$(od -An -tu8 -j 72 -N 8 "$image" |
    tr -d '[:space:]')
ntfs_volume_info=$("$ntfsinfo" -m "$image")
ntfs_free_clusters=$(printf '%s\n' "$ntfs_volume_info" |
    awk '$1 == "Free" && $2 == "Clusters:" { print $3; exit }')
ntfs_record_size=$(printf '%s\n' "$ntfs_volume_info" |
    awk '$1 == "MFT" && $2 == "Record" && $3 == "Size:" {
        print $4
        exit
    }')
ntfs_version=$(printf '%s\n' "$ntfs_volume_info" |
    awk '$1 == "Volume" && $2 == "Version:" { print $3; exit }')
ntfs_mft_info=$("$ntfsinfo" -i 0 "$image")
ntfs_mft_valid=$(printf '%s\n' "$ntfs_mft_info" |
    sed -n '/Dumping attribute \$DATA/,/Dumping attribute \$BITMAP/p' |
    awk '$1 == "Data" && $2 == "size:" { print $3; exit }')

test "$(volume_field serial)" = "$boot_serial"
test "$(volume_field sectors)" = "$boot_sectors"
test "$(volume_field clusters)" -eq \
    $((boot_sectors / boot_sectors_per_cluster))
test "$(volume_field free-clusters)" = "$ntfs_free_clusters"
test "$(volume_field reserved-clusters)" -eq 0
test "$(volume_field bytes-per-sector)" = "$boot_sector_size"
test "$(volume_field bytes-per-cluster)" -eq \
    $((boot_sector_size * boot_sectors_per_cluster))
test "$(volume_field bytes-per-file-record)" = "$ntfs_record_size"
test "$(volume_field clusters-per-file-record)" -eq \
    $((ntfs_record_size /
       (boot_sector_size * boot_sectors_per_cluster)))
test "$(volume_field mft-valid-bytes)" = "$ntfs_mft_valid"
test "$(volume_field mft-lcn)" = "$boot_mft_lcn"
test "$(volume_field mftmirr-lcn)" = "$boot_mftmirr_lcn"
test "$(volume_field mft-zone-start)" -eq 0
test "$(volume_field mft-zone-end)" -eq 0
test "$(volume_field version)" = "$ntfs_version"

# Find the first in-use MFT ordinal at or below 23 from the independent
# $MFT::$BITMAP stream. A nonzero high sequence component must be ignored.
"$ntfscat" -i 0 -a 0xb0 "$image" >"$mft_bitmap"
expected_record=23
while test "$expected_record" -ge 0; do
    bitmap_byte=$(od -An -tu1 \
        -j $((expected_record / 8)) -N 1 "$mft_bitmap" |
        tr -d '[:space:]')
    if test $((bitmap_byte &
               (1 << (expected_record % 8)))) -ne 0; then
        break
    fi
    expected_record=$((expected_record - 1))
done
test "$expected_record" -ge 0

prepare_fixed_record()
{
    record_number=$1
    "$ntfscat" -i 0 "$image" 2>/dev/null |
        dd bs="$ntfs_record_size" skip="$record_number" count=1 \
            status=none >"$mft_expected"
    test "$(wc -c <"$mft_expected")" -eq \
        "$ntfs_record_size"
}

prepare_fixed_record "$expected_record"
"$frontend" --mft-record \
    "$image" 0xffff000000000017 \
    >"$mft_actual" 2>"$mft_meta"
test "$(cat "$mft_meta")" = \
    "file-record $expected_record $ntfs_record_size"
cmp "$mft_actual" "$mft_expected"

# Record 5 contains live root-index data, so its sector trailers prove that
# the shared API returns a validated, fixup-applied record rather than raw
# update-sequence sentinels. ntfscat independently returns the fixed record;
# read the physical base $MFT run as well and verify both trailer forms.
prepare_fixed_record 5
dd if="$image" bs=1 \
    skip=$((boot_mft_lcn *
            boot_sector_size *
            boot_sectors_per_cluster +
            5 * ntfs_record_size)) \
    count="$ntfs_record_size" status=none >"$mft_raw"
test "$(wc -c <"$mft_raw")" -eq "$ntfs_record_size"
usa_offset=$(od -An -tu2 -j 4 -N 2 "$mft_raw" |
    tr -d '[:space:]')
usa_count=$(od -An -tu2 -j 6 -N 2 "$mft_raw" |
    tr -d '[:space:]')
usa_number=$(od -An -tu2 -j "$usa_offset" -N 2 "$mft_raw" |
    tr -d '[:space:]')
test "$usa_count" -eq \
    $((ntfs_record_size / boot_sector_size + 1))
usa_index=1
while test "$usa_index" -lt "$usa_count"; do
    trailer_offset=$((boot_sector_size * usa_index - 2))
    replacement_offset=$((usa_offset + 2 * usa_index))
    raw_trailer=$(od -An -tu2 -j "$trailer_offset" -N 2 \
        "$mft_raw" | tr -d '[:space:]')
    replacement=$(od -An -tu2 -j "$replacement_offset" -N 2 \
        "$mft_raw" | tr -d '[:space:]')
    fixed_trailer=$(od -An -tu2 -j "$trailer_offset" -N 2 \
        "$mft_expected" | tr -d '[:space:]')
    test "$raw_trailer" = "$usa_number"
    test "$fixed_trailer" = "$replacement"
    usa_index=$((usa_index + 1))
done
if cmp -s "$mft_raw" "$mft_expected"; then
    exit 1
fi
"$frontend" --mft-record \
    "$image" 0xabcd000000000005 \
    >"$mft_actual" 2>"$mft_meta"
test "$(cat "$mft_meta")" = \
    "file-record 5 $ntfs_record_size"
cmp "$mft_actual" "$mft_expected"
"$frontend" --cat "$image" /RESIDENT-É.TXT | cmp - "$resident_source"
"$frontend" --cat "$image" /empty.txt | cmp - "$empty_source"
"$frontend" --cat "$image" /sparse.bin |
    cmp -n "$sparse_size" - /dev/zero
test "$("$frontend" --cat "$image" /sparse.bin | wc -c)" -eq "$sparse_size"

# Force the root index into multiple levels and move $INDEX_ROOT into an
# extension record. This exercises $ATTRIBUTE_LIST lookup plus in-order B-tree
# traversal, including separator keys at every depth.
large_index=1
while test "$large_index" -le 1000; do
    large_name=$(printf '/large-%06d.txt' "$large_index")
    "$ntfscp" -f -q "$image" "$resident_source" "$large_name"
    large_index=$((large_index + 1))
done

"$frontend" --list "$image" / >"$listing"
test "$(wc -l <"$listing")" -eq 1004
grep -q 'large-000001.txt$' "$listing"
grep -q 'large-001000.txt$' "$listing"
"$frontend" --cat "$image" /hello.txt | cmp - "$source_file"
