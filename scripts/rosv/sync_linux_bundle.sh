#!/bin/bash
#
# sync_linux_bundle.sh
# Copy prebuilt ROSV Linux bundle assets into a specific ReactOS output tree.
#
# Usage:
#   scripts/rosv/sync_linux_bundle.sh --build-dir /path/to/output-*
#
# Source assets:
#   <repo>/roslinux/vmlinuz
#   <repo>/roslinux/ubuntu24.vhdx
#
# Destination:
#   <build-dir>/rosv/

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: sync_linux_bundle.sh --build-dir <path>

Copy ROSV Linux bundle assets (vmlinuz + ubuntu24.vhdx) from <repo>/roslinux
into <build-dir>/rosv/ (where rosl.exe and vm_monitor.py look for them).
EOF
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(realpath "$SCRIPT_DIR/../..")"
ROSLINUX_DIR="$REPO_ROOT/roslinux"

BUILD_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { echo "Missing value for --build-dir" >&2; exit 1; }
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

[[ -n "$BUILD_DIR" ]] || { usage >&2; exit 1; }
BUILD_DIR="$(realpath "$BUILD_DIR")"
DEST_DIR="$BUILD_DIR/rosv"

VMLINUX_SRC="$ROSLINUX_DIR/vmlinuz"
VHDX_SRC="$ROSLINUX_DIR/ubuntu24.vhdx"

[[ -f "$VMLINUX_SRC" ]] || { echo "Missing source kernel: $VMLINUX_SRC" >&2; exit 1; }
[[ -f "$VHDX_SRC"    ]] || { echo "Missing source VHDX: $VHDX_SRC" >&2; exit 1; }

mkdir -p "$DEST_DIR"
cp -f "$VMLINUX_SRC" "$DEST_DIR/vmlinuz"
cp -f "$VHDX_SRC" "$DEST_DIR/ubuntu24.vhdx"

echo "[sync-linux-bundle] Synced assets to: $DEST_DIR"
echo "[sync-linux-bundle]   vmlinuz       <- $VMLINUX_SRC"
echo "[sync-linux-bundle]   ubuntu24.vhdx <- $VHDX_SRC"
