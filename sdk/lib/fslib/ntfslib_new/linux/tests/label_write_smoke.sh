#!/bin/sh
set -eu

driver=$1
mkntfs=$2
ntfslabel=$3
ntfsfix=$4

workdir=$(mktemp -d "${TMPDIR:-/tmp}/ntfslib-label.XXXXXX")
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
image="$workdir/label.ntfs"
long_label=ABCDEFGHIJKLMNOPQRSTUVWXYZ123456

truncate -s 64M "$image"
"$mkntfs" -F -Q -L BEFORE "$image" >/dev/null

"$driver" --set-label "$image" "$long_label"
core_label=$("$driver" --probe "$image" |
    sed -n 's/^label: //p')
reference_label=$("$ntfslabel" "$image")
test "$core_label" = "$long_label"
test "$reference_label" = "$long_label"
"$ntfsfix" -n "$image" >/dev/null

"$driver" --set-label "$image" ""
core_label=$("$driver" --probe "$image" |
    sed -n 's/^label: //p')
reference_label=$("$ntfslabel" "$image")
test -z "$core_label"
test -z "$reference_label"
"$ntfsfix" -n "$image" >/dev/null
