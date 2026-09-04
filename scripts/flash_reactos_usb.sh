#!/bin/sh

# Safely flash a ReactOS raw disk image to the only attached removable USB disk.

set -eu

usage()
{
    echo "Usage: $0 [--dry-run] [ReactOS.img]" >&2
    exit 2
}

fail()
{
    echo "error: $*" >&2
    exit 1
}

plist_value()
{
    /usr/libexec/PlistBuddy -c "Print :$1" "$2"
}

find_only_external_disk()
{
    /usr/sbin/diskutil list external physical | /usr/bin/awk '/^\/dev\/disk[0-9]+ \(external, physical\):$/ { print $1 }'
}

if [ "$(/usr/bin/uname -s)" != "Darwin" ]; then
    fail "this script is supported only on macOS"
fi

dry_run=false
if [ "${1-}" = "--dry-run" ]; then
    dry_run=true
    shift
fi

if [ "$#" -gt 1 ]; then
    usage
fi

image_path=${1:-ReactOS.img}
[ -f "$image_path" ] || fail "image not found: $image_path"
[ -r "$image_path" ] || fail "image is not readable: $image_path"

case "$image_path" in
    /*) ;;
    *) image_path="$(cd "$(dirname "$image_path")" && pwd -P)/$(basename "$image_path")" ;;
esac

image_size=$(/usr/bin/stat -f '%z' "$image_path")
[ "$image_size" -gt 0 ] || fail "image is empty: $image_path"
image_hash=$(/usr/bin/shasum -a 256 "$image_path" | /usr/bin/awk '{ print $1 }')

candidate_list=$(find_only_external_disk)
candidate_count=$(printf '%s\n' "$candidate_list" | /usr/bin/awk 'NF { count++ } END { print count + 0 }')

if [ "$candidate_count" -eq 0 ]; then
    fail "no external physical disk is attached"
fi

if [ "$candidate_count" -ne 1 ]; then
    echo "External physical disks:" >&2
    printf '%s\n' "$candidate_list" >&2
    fail "refusing to guess because more than one external disk is attached"
fi

target_node=$candidate_list
target_id=${target_node#/dev/}
case "$target_id" in
    disk[0-9]*)
        target_number=${target_id#disk}
        case "$target_number" in
            *[!0-9]*|'') fail "unexpected disk identifier: $target_id" ;;
        esac
        ;;
    *) fail "unexpected disk identifier: $target_id" ;;
esac

[ "$target_number" -ge 4 ] || fail "refusing to overwrite disk0 through disk3"

work_dir=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/reactos-usb.XXXXXX")
trap '/bin/rm -rf "$work_dir"' EXIT HUP INT TERM
identity_before="$work_dir/identity-before.plist"
identity_after="$work_dir/identity-after.plist"

/usr/sbin/diskutil info -plist "$target_node" > "$identity_before"

device_node=$(plist_value DeviceNode "$identity_before")
device_id=$(plist_value DeviceIdentifier "$identity_before")
media_name=$(plist_value MediaName "$identity_before")
bus_protocol=$(plist_value BusProtocol "$identity_before")
device_size=$(plist_value Size "$identity_before")
device_tree_path=$(plist_value DeviceTreePath "$identity_before")
is_internal=$(plist_value Internal "$identity_before")
is_removable=$(plist_value Removable "$identity_before")
is_whole=$(plist_value WholeDisk "$identity_before")
is_writable=$(plist_value WritableMedia "$identity_before")

[ "$device_node" = "$target_node" ] || fail "device identity changed during inspection"
[ "$device_id" = "$target_id" ] || fail "device identifier mismatch"
[ "$bus_protocol" = "USB" ] || fail "$target_node is not a USB disk"
[ "$is_internal" = "false" ] || fail "$target_node is internal"
[ "$is_removable" = "true" ] || fail "$target_node is not removable"
[ "$is_whole" = "true" ] || fail "$target_node is not a whole disk"
[ "$is_writable" = "true" ] || fail "$target_node is not writable"
[ "$image_size" -le "$device_size" ] || fail "the image is larger than the target disk"

echo "Image : $image_path"
echo "Bytes : $image_size"
echo "SHA256: $image_hash"
echo "Target: $target_node"
echo "Model : $media_name"
echo "Bus   : $bus_protocol"
echo "Size  : $device_size bytes"
echo
echo "WARNING: every existing partition and file on $target_node will be overwritten."

if [ "$dry_run" = "true" ]; then
    echo "Dry run complete; no disk was changed."
    exit 0
fi

printf "Type 'ERASE %s' to continue: " "$target_id"
IFS= read -r confirmation
[ "$confirmation" = "ERASE $target_id" ] || fail "confirmation did not match"

# Re-enumerate immediately before the destructive operation. The target must
# still be the sole external physical disk with the same live identity.
candidate_list=$(find_only_external_disk)
[ "$candidate_list" = "$target_node" ] || fail "external disk selection changed before writing"
/usr/sbin/diskutil info -plist "$target_node" > "$identity_after"

[ "$(plist_value DeviceNode "$identity_after")" = "$device_node" ] || fail "device node changed before writing"
[ "$(plist_value MediaName "$identity_after")" = "$media_name" ] || fail "device model changed before writing"
[ "$(plist_value BusProtocol "$identity_after")" = "USB" ] || fail "target is no longer a USB disk"
[ "$(plist_value Internal "$identity_after")" = "false" ] || fail "target became internal"
[ "$(plist_value Removable "$identity_after")" = "true" ] || fail "target is no longer removable"
[ "$(plist_value WholeDisk "$identity_after")" = "true" ] || fail "target is no longer a whole disk"
[ "$(plist_value WritableMedia "$identity_after")" = "true" ] || fail "target is no longer writable"
[ "$(plist_value Size "$identity_after")" = "$device_size" ] || fail "device size changed before writing"
[ "$(plist_value DeviceTreePath "$identity_after")" = "$device_tree_path" ] || fail "USB device path changed before writing"

raw_node="/dev/r${target_id}"
verify_blocks=$(( (image_size + 1048575) / 1048576 ))

/usr/sbin/diskutil unmountDisk "$target_node"

if [ "$(/usr/bin/id -u)" -eq 0 ]; then
    /bin/dd if="$image_path" of="$raw_node" bs=8388608
    /bin/sync
    written_hash=$(/bin/dd if="$raw_node" bs=1048576 count="$verify_blocks" 2>/dev/null | /usr/bin/head -c "$image_size" | /usr/bin/shasum -a 256 | /usr/bin/awk '{ print $1 }')
else
    written_hash=$(/usr/bin/osascript \
        -e 'on run argv' \
        -e 'set imagePath to item 1 of argv' \
        -e 'set rawPath to item 2 of argv' \
        -e 'set imageBytes to item 3 of argv' \
        -e 'set verifyBlocks to item 4 of argv' \
        -e 'set writeCommand to "/bin/dd if=" & quoted form of imagePath & " of=" & quoted form of rawPath & " bs=8388608 && /bin/sync && /bin/dd if=" & quoted form of rawPath & " bs=1048576 count=" & verifyBlocks & " 2>/dev/null | /usr/bin/head -c " & imageBytes & " | /usr/bin/shasum -a 256 | /usr/bin/awk '\''{ print $1 }'\''"' \
        -e 'do shell script writeCommand with administrator privileges' \
        -e 'end run' \
        "$image_path" "$raw_node" "$image_size" "$verify_blocks")
fi

[ "$written_hash" = "$image_hash" ] || fail "verification failed: expected $image_hash, read $written_hash"

echo "Verified written SHA-256: $written_hash"
/usr/sbin/diskutil eject "$target_node"
echo "ReactOS image written and $target_node ejected successfully."
