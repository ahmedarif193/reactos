#!/usr/bin/env python3
"""
VM Monitor Script
Builds livecd, starts VM (VirtualBox or QEMU) and monitors for stalls.
If log stops updating for more than 6 seconds, or total runtime exceeds 30 seconds,
forcefully stops the VM.

Usage:
  # Run from within your build directory (e.g., output-arm64)
  python3 ../vm_monitor.py
  
  # Or if script is in the build dir:
  python3 vm_monitor.py
"""

import subprocess
import time
import os
import sys
import signal
import atexit
import argparse
import shutil
import platform
import json
import socket
import tempfile
import re

# Configuration (never change those values)
LOG_FILE = "/tmp/v.log"
DEFAULT_STALL_TIMEOUT = 16
DEFAULT_HARD_TIMEOUT = 160
STALL_TIMEOUT = DEFAULT_STALL_TIMEOUT
HARD_TIMEOUT = DEFAULT_HARD_TIMEOUT
COMPLETION_MARKERS = []
VM_NAME = "testWin11"
BOOT_SUCCESS_MARKER = "Attempting to call RegisterClassNameW in comctl32.dll"
BOOT_SUCCESS_STATUS = "BOOT_SUCCESS"
BOOT_FAILURE_STATUS = "BOOT_FAILURE"
NTFSLX_AUTO_SUCCESS_MARKER = "NTFSLX-AUTO: COMPLETE failures=0"
NTFSLX_BLOCKER_MARKER = "NTFSLX-AUTO: BLOCKER PROBES COMPLETE"
KMTEST_AUTO_SUCCESS_MARKER = "KMTEST-AUTO: COMPLETE"
DEFAULT_SHUTDOWN_GRACE = 25
SHUTDOWN_GRACE = DEFAULT_SHUTDOWN_GRACE
STRICT_PROBE_GATING = False
SHORT_CYCLE = False

def get_build_dir():
    """
    Returns the current working directory as the build directory.
    Strictly assumes the script is executed FROM the output directory.
    """
    if "REACTOS_BUILD_DIR" in os.environ:
        return os.environ["REACTOS_BUILD_DIR"]
    
    cwd = os.getcwd()
    
    # We validate strictly: must look like an output dir or contain build.ninja
    # but we do NOT search parent directories.
    if "output-" in os.path.basename(cwd) or os.path.exists(os.path.join(cwd, "build.ninja")):
        return cwd
        
    print(f"Warning: Current directory '{cwd}' does not look like a standard 'output-' directory.")
    print("Proceeding using current directory as BUILD_DIR...")
    return cwd

BUILD_DIR = get_build_dir()
FAT32_IMG = os.path.join(BUILD_DIR, "fat32.img")
NTFS_IMG = os.path.join(BUILD_DIR, "ntfs.img")
NTFS_IMG_SIZE_MB = 512
QMP_SOCKET = os.path.join(BUILD_DIR, "vm_monitor.qmp")
FAT32_BENCH_LABEL = "NTFXBENCH"

# UEFI firmware paths - architecture dependent
OVMF_ENV_CODE_VARS = ["REACTOS_OVMF_CODE", "OVMF_CODE"]
OVMF_ENV_VARS_VARS = ["REACTOS_OVMF_VARS", "OVMF_VARS"]

OVMF_X64_CODE_CANDIDATES = [
    "/tmp/OVMF_CODE_latest.fd",
    "/opt/homebrew/share/qemu/edk2-x86_64-code.fd",
    "/usr/local/share/qemu/edk2-x86_64-code.fd",
    "/usr/share/OVMF/OVMF_CODE.fd",
    "/usr/share/OVMF/OVMF_CODE_4M.fd",
    "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd",
    "/usr/share/edk2/ovmf/OVMF_CODE.fd",
]

OVMF_X64_VARS_CANDIDATES = [
    "/tmp/OVMF_VARS_latest.fd",
    "/opt/homebrew/share/qemu/edk2-x86_64-vars.fd",
    "/usr/local/share/qemu/edk2-x86_64-vars.fd",
    "/usr/share/OVMF/OVMF_VARS.fd",
    "/usr/share/OVMF/OVMF_VARS_4M.fd",
    "/usr/share/edk2-ovmf/x64/OVMF_VARS.fd",
    "/usr/share/edk2/ovmf/OVMF_VARS.fd",
]

OVMF_IA32_CODE_CANDIDATES = [
    "/usr/share/OVMF/OVMF32_CODE_4M.fd",
    "/usr/share/OVMF/OVMF32_CODE.fd",
    "/usr/share/edk2-ovmf/ia32/OVMF_CODE.fd",
    "/usr/share/edk2/ovmf/OVMF32_CODE.fd",
]

OVMF_IA32_VARS_CANDIDATES = [
    "/usr/share/OVMF/OVMF32_VARS_4M.fd",
    "/usr/share/OVMF/OVMF32_VARS.fd",
    "/usr/share/edk2-ovmf/ia32/OVMF_VARS.fd",
    "/usr/share/edk2/ovmf/OVMF32_VARS.fd",
]

# Global state
qemu_process = None
use_qemu = False
target_arch = "amd64" 


def detect_target_arch():
    """Detect target architecture from build directory name."""
    global target_arch
    build_dir_lower = BUILD_DIR.lower()
    
    if "arm64" in build_dir_lower or "aarch64" in build_dir_lower:
        target_arch = "arm64"
    elif "i386" in build_dir_lower or "x86" in build_dir_lower or "i686" in build_dir_lower:
        target_arch = "i386"
    elif "amd64" in build_dir_lower or "x64" in build_dir_lower:
        target_arch = "amd64"
    else:
        target_arch = "amd64"
    return target_arch


def env_path(var_names):
    """Return the first set env var path from var_names, or None."""
    for name in var_names:
        value = os.environ.get(name)
        if value:
            return value
    return None


def find_first_existing(paths):
    """Return the first existing path in paths, or None."""
    for path in paths:
        if path and os.path.exists(path):
            return path
    return None


def resolve_ovmf_paths(arch):
    """Resolve OVMF CODE and VARS (writable) paths for the given arch."""
    if arch == "i386":
        code_candidates = OVMF_IA32_CODE_CANDIDATES
        vars_candidates = OVMF_IA32_VARS_CANDIDATES
        vars_local = os.path.join(BUILD_DIR, "OVMF32_VARS.fd")
    else:
        code_candidates = OVMF_X64_CODE_CANDIDATES
        vars_candidates = OVMF_X64_VARS_CANDIDATES
        vars_local = os.path.join(BUILD_DIR, "OVMF_VARS.fd")

    code_env = env_path(OVMF_ENV_CODE_VARS)
    vars_env = env_path(OVMF_ENV_VARS_VARS)

    if code_env and os.path.exists(code_env):
        ovmf_code = code_env
    else:
        ovmf_code = find_first_existing(code_candidates)

    if vars_env:
        if os.path.exists(vars_env) and os.access(vars_env, os.W_OK):
            ovmf_vars = vars_env
            ovmf_vars_template = None
        else:
            ovmf_vars = vars_env
            ovmf_vars_template = find_first_existing(vars_candidates)
    else:
        ovmf_vars = vars_local
        ovmf_vars_template = find_first_existing(vars_candidates)

    return ovmf_code, ovmf_vars, ovmf_vars_template, code_candidates, vars_candidates


def prepare_ovmf_vars(template_path, vars_path):
    """Ensure a writable VARS file exists; copy template if needed."""
    if template_path is None:
        if os.path.exists(vars_path):
            return True
        print(f"Error: OVMF VARS not found: {vars_path}")
        return False

    if not os.path.exists(template_path):
        print(f"Error: OVMF VARS template not found: {template_path}")
        return False

    try:
        if (not os.path.exists(vars_path) or
                os.path.getmtime(template_path) > os.path.getmtime(vars_path)):
            shutil.copy(template_path, vars_path)
            print(f"Copied OVMF VARS to {vars_path}")
        return True
    except Exception as e:
        print(f"Error preparing OVMF VARS: {e}")
        return False


def create_fat32_img():
    """Recreate a fresh FAT32 disk image with an MBR partition."""
    if os.path.exists(FAT32_IMG):
        print(f"Removing old FAT32 image: {FAT32_IMG}")
        os.remove(FAT32_IMG)

    print(f"Creating FAT32 image: {FAT32_IMG}")
    part_start_sector = 2048
    total_sectors = 64 * 1024 * 1024 // 512
    part_sectors = total_sectors - part_start_sector
    part_tmp = FAT32_IMG + ".part.tmp"
    try:
        subprocess.run(
            ["qemu-img", "create", "-f", "raw", FAT32_IMG, "64M"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )

        sfdisk_script = (
            f"label: dos\nunit: sectors\n"
            f"1 : start={part_start_sector}, size={part_sectors}, type=c\n"
        )
        subprocess.run(
            ["sfdisk", FAT32_IMG], input=sfdisk_script, text=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )

        subprocess.run(
            ["qemu-img", "create", "-f", "raw", part_tmp, f"{part_sectors * 512}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )
        subprocess.run(
            ["mkfs.vfat", "-F", "32", "-n", FAT32_BENCH_LABEL, part_tmp],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )
        subprocess.run(
            ["dd", f"if={part_tmp}", f"of={FAT32_IMG}",
             "bs=512", f"seek={part_start_sector}", "conv=notrunc"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )
        return True
    except Exception as e:
        print(f"Error creating FAT32 image: {e}")
        return False
    finally:
        if os.path.exists(part_tmp):
            os.remove(part_tmp)


def create_ntfs_img():
    """Recreate a fresh NTFS disk image for ntfslx driver testing.

    Lays down an MBR with a single NTFS partition aligned to 1 MiB, then
    formats that partition with mkfs.ntfs by building the filesystem as a
    temp file of the exact partition size and splicing it back into the
    image. This avoids losetup/sudo entirely since mkfs.ntfs -F happily
    targets a regular file.
    """
    if os.path.exists(NTFS_IMG):
        print(f"Removing old NTFS image: {NTFS_IMG}")
        os.remove(NTFS_IMG)

    print(f"Creating NTFS image: {NTFS_IMG} ({NTFS_IMG_SIZE_MB} MB)")
    part_start_sector = 2048
    total_sectors = NTFS_IMG_SIZE_MB * 1024 * 1024 // 512
    part_sectors = total_sectors - part_start_sector
    part_tmp = NTFS_IMG + ".part.tmp"
    try:
        subprocess.run(
            ["qemu-img", "create", "-f", "raw", NTFS_IMG, f"{NTFS_IMG_SIZE_MB}M"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )

        sfdisk_script = (
            f"label: dos\nunit: sectors\n"
            f"1 : start={part_start_sector}, size={part_sectors}, type=7\n"
        )
        subprocess.run(
            ["sfdisk", NTFS_IMG], input=sfdisk_script, text=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )

        subprocess.run(
            ["qemu-img", "create", "-f", "raw", part_tmp, f"{part_sectors * 512}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )
        subprocess.run(
            ["mkfs.ntfs", "-f", "-F", "-s", "512", "-L", "NTFSLX", part_tmp],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )
        subprocess.run(
            ["dd", f"if={part_tmp}", f"of={NTFS_IMG}",
             "bs=512", f"seek={part_start_sector}", "conv=notrunc"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True,
        )
        return True
    except subprocess.CalledProcessError as e:
        print(f"Error creating NTFS image: {e}")
        return False
    except FileNotFoundError as e:
        print(f"Missing tool for NTFS image creation: {e}")
        return False
    finally:
        if os.path.exists(part_tmp):
            os.remove(part_tmp)



def fill_pattern(seed, step, size):
    """Return the deterministic byte pattern used by ntfslx_auto.exe."""
    return bytes(((seed + (i * step)) & 0xFF) for i in range(size))


def read_log_contents(filepath):
    """Read the whole log file as text, returning an empty string on error."""
    try:
        with open(filepath, 'r', errors='ignore') as f:
            return f.read()
    except Exception:
        return ""


def get_ntfs_partition_layout(image_path):
    """Return (start_sector, size_in_sectors) for the first partition in image_path."""
    result = subprocess.run(
        ["sfdisk", "--json", image_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"sfdisk failed for {image_path}: {result.stderr.strip()}")

    data = json.loads(result.stdout)
    partitions = data.get("partitiontable", {}).get("partitions", [])
    if not partitions:
        raise RuntimeError(f"No partitions found in {image_path}")

    start_sector = int(partitions[0]["start"])
    size_in_sectors = int(partitions[0]["size"])
    return start_sector, size_in_sectors


def carve_ntfs_partition(image_path):
    """Extract the first partition from the NTFS disk image into a temp file."""
    start_sector, size_in_sectors = get_ntfs_partition_layout(image_path)
    fd, part_path = tempfile.mkstemp(prefix="ntfslx-verify-", suffix=".img")
    os.close(fd)

    remaining = size_in_sectors * 512
    with open(image_path, 'rb') as src, open(part_path, 'wb') as dst:
        src.seek(start_sector * 512)
        while remaining > 0:
            chunk = src.read(min(4 * 1024 * 1024, remaining))
            if not chunk:
                raise RuntimeError("Unexpected end of NTFS image while carving partition")
            dst.write(chunk)
            remaining -= len(chunk)

    return part_path


def ntfsls_entries(partition_path, dir_path):
    """Return a set of directory entry names from ntfsls -p."""
    result = subprocess.run(
        ["ntfsls", "-p", dir_path, partition_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"ntfsls failed for {dir_path}: {details}")

    entries = set()
    for line in result.stdout.splitlines():
        name = line.strip()
        if name:
            entries.add(name)
    return entries


def ntfscat_bytes(partition_path, file_path):
    """Return the raw file content from ntfscat."""
    result = subprocess.run(
        ["ntfscat", partition_path, file_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        details = result.stderr.decode("utf-8", errors="ignore").strip()
        raise RuntimeError(f"ntfscat failed for {file_path}: {details}")
    return result.stdout


def verify_ntfslx_persistent_image():
    """Verify the persistent artifact tree written by ntfslx_auto.exe."""
    for tool in ("sfdisk", "ntfsls", "ntfscat"):
        if shutil.which(tool) is None:
            raise RuntimeError(f"Required host tool is missing: {tool}")

    if not os.path.exists(NTFS_IMG):
        raise RuntimeError(f"NTFS image not found: {NTFS_IMG}")

    partition_path = carve_ntfs_partition(NTFS_IMG)
    print(f"Verifying NTFS image contents via {partition_path}")

    try:
        def require_entries(entries, expected, label):
            missing = sorted(name for name in expected if name not in entries)
            if missing:
                raise RuntimeError(f"{label} is missing entries: {', '.join(missing)}")

        def forbid_entries(entries, forbidden, label):
            present = sorted(name for name in forbidden if name in entries)
            if present:
                raise RuntimeError(f"{label} unexpectedly contains: {', '.join(present)}")

        def require_file(path_name, size, prefix):
            data = ntfscat_bytes(partition_path, path_name)
            if len(data) != size:
                raise RuntimeError(f"{path_name} size is {len(data)}, expected {size}")
            if data[:len(prefix)] != prefix:
                raise RuntimeError(
                    f"{path_name} prefix mismatch: got {data[:len(prefix)].hex()} expected {prefix.hex()}"
                )
            return data

        root_entries = ntfsls_entries(partition_path, "/")
        require_entries(root_entries, {"ntfslx_auto", "ntfslx_root_move.bin", "ntfslx_recycle.bin"}, "root")

        auto_entries = ntfsls_entries(partition_path, "/ntfslx_auto")
        require_entries(auto_entries, {"alpha", "mix", "multi", "cases", "root.bin"}, "/ntfslx_auto")

        alpha_entries = ntfsls_entries(partition_path, "/ntfslx_auto/alpha")
        require_entries(alpha_entries, {"a.bin", "copy.bin"}, "/ntfslx_auto/alpha")

        mix_entries = ntfsls_entries(partition_path, "/ntfslx_auto/mix")
        require_entries(mix_entries, {"big.bin", "small.bin"}, "/ntfslx_auto/mix")

        multi_entries = ntfsls_entries(partition_path, "/ntfslx_auto/multi")
        require_entries(multi_entries, {"fa.bin", "fb.bin", "fc.bin"}, "/ntfslx_auto/multi")

        cases_entries = ntfsls_entries(partition_path, "/ntfslx_auto/cases")
        require_entries(cases_entries, {"rename", "move_done", "share.bin", "replace_move_src", "replace_move_dst", "dir_recycle", "shared_rewrite.bin"}, "/ntfslx_auto/cases")
        forbid_entries(cases_entries, {"move_src", "delete_gone.bin", "root_move_src"}, "/ntfslx_auto/cases")

        rename_entries = ntfsls_entries(partition_path, "/ntfslx_auto/cases/rename")
        require_entries(rename_entries, {"victim.bin"}, "/ntfslx_auto/cases/rename")
        forbid_entries(rename_entries, {"source.bin"}, "/ntfslx_auto/cases/rename")

        move_entries = ntfsls_entries(partition_path, "/ntfslx_auto/cases/move_done")
        require_entries(move_entries, {"child.bin"}, "/ntfslx_auto/cases/move_done")

        replace_move_src_entries = ntfsls_entries(partition_path, "/ntfslx_auto/cases/replace_move_src")
        forbid_entries(replace_move_src_entries, {"source.bin"}, "/ntfslx_auto/cases/replace_move_src")

        replace_move_dst_entries = ntfsls_entries(partition_path, "/ntfslx_auto/cases/replace_move_dst")
        require_entries(replace_move_dst_entries, {"victim.bin"}, "/ntfslx_auto/cases/replace_move_dst")

        dir_recycle_entries = ntfsls_entries(partition_path, "/ntfslx_auto/cases/dir_recycle")
        require_entries(dir_recycle_entries, {"child.bin"}, "/ntfslx_auto/cases/dir_recycle")

        require_file("/ntfslx_auto/root.bin", 300, fill_pattern(0x10, 1, 16))
        require_file("/ntfslx_auto/alpha/a.bin", 266, fill_pattern(0xA0, 1, 16))
        require_file("/ntfslx_auto/alpha/copy.bin", 300, fill_pattern(0x10, 1, 16))
        require_file("/ntfslx_auto/mix/small.bin", 128, fill_pattern(0x20, 1, 16))
        require_file("/ntfslx_auto/mix/big.bin", 4096, fill_pattern(0x40, 3, 16))
        require_file("/ntfslx_auto/multi/fa.bin", 64, b"\xAA" * 16)
        require_file("/ntfslx_auto/cases/rename/victim.bin", 384, fill_pattern(0x71, 2, 16))
        require_file("/ntfslx_auto/cases/move_done/child.bin", 300, fill_pattern(0xC0, 1, 16))
        require_file("/ntfslx_auto/cases/replace_move_dst/victim.bin", 704, fill_pattern(0x62, 5, 16))
        require_file("/ntfslx_auto/cases/dir_recycle/child.bin", 960, fill_pattern(0xA7, 3, 16))
        require_file("/ntfslx_auto/cases/shared_rewrite.bin", 1536, fill_pattern(0xD4, 2, 16))
        require_file("/ntfslx_root_move.bin", 512, fill_pattern(0xE1, 2, 16))
        require_file("/ntfslx_recycle.bin", 640, fill_pattern(0xB2, 1, 16))

        share_data = require_file("/ntfslx_auto/cases/share.bin", 256, fill_pattern(0x15, 1, 16))
        rewritten = fill_pattern(0xD0, 1, 16)
        if share_data[128:144] != rewritten:
            raise RuntimeError(
                f"/ntfslx_auto/cases/share.bin rewrite mismatch: got {share_data[128:144].hex()} expected {rewritten.hex()}"
            )

        print("Host NTFS verification passed.")
    finally:
        try:
            os.remove(partition_path)
        except OSError:
            pass

def cleanup_qmp_socket():
    """Remove the stale QMP socket left behind by a previous run."""
    try:
        if os.path.exists(QMP_SOCKET):
            os.remove(QMP_SOCKET)
    except OSError:
        pass


def qemu_is_running():
    """Return True while the tracked QEMU child is still alive."""
    return qemu_process is not None and qemu_process.poll() is None


def wait_for_qemu_exit(timeout):
    """Wait up to timeout seconds for QEMU to exit."""
    if qemu_process is None:
        return True

    deadline = time.time() + timeout
    while time.time() < deadline:
        if qemu_process.poll() is not None:
            print(f"QEMU exited with code {qemu_process.returncode}")
            return True
        time.sleep(0.5)

    return qemu_process.poll() is not None


def wait_for_qmp_socket(timeout):
    """Wait for the QMP Unix socket to become connectable."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not qemu_is_running():
            return False
        if os.path.exists(QMP_SOCKET):
            try:
                probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                probe.settimeout(0.5)
                probe.connect(QMP_SOCKET)
                probe.close()
                return True
            except OSError:
                pass
        time.sleep(0.2)
    return False


def qmp_execute(command, arguments=None, timeout=5.0):
    """Execute a QMP command and return its result payload."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    fileobj = None
    try:
        sock.connect(QMP_SOCKET)
        fileobj = sock.makefile("rwb")

        def read_message():
            while True:
                line = fileobj.readline()
                if not line:
                    raise RuntimeError("QMP connection closed")
                message = json.loads(line.decode("utf-8"))
                if "event" in message:
                    continue
                return message

        greeting = read_message()
        if "QMP" not in greeting:
            raise RuntimeError(f"Unexpected QMP greeting: {greeting!r}")

        fileobj.write(json.dumps({"execute": "qmp_capabilities"}).encode("utf-8") + b"\n")
        fileobj.flush()
        response = read_message()
        if "error" in response:
            raise RuntimeError(response["error"])

        payload = {"execute": command}
        if arguments is not None:
            payload["arguments"] = arguments
        fileobj.write(json.dumps(payload).encode("utf-8") + b"\n")
        fileobj.flush()
        response = read_message()
        if "error" in response:
            raise RuntimeError(response["error"])
        return response.get("return")
    finally:
        try:
            if fileobj is not None:
                fileobj.close()
        finally:
            sock.close()


def request_qemu_powerdown():
    """Ask QEMU to inject an ACPI power button event into the guest."""
    if not qemu_is_running():
        return True

    if not wait_for_qmp_socket(10):
        print(f"QMP socket did not become ready: {QMP_SOCKET}")
        return False

    try:
        qmp_execute("system_powerdown")
        print("Sent QEMU system_powerdown request.")
        return True
    except Exception as e:
        print(f"Failed to send QEMU system_powerdown: {e}")
        return False


def force_kill_vm():
    """Forcefully kill the current VM instance - called on exit."""
    global qemu_process, use_qemu

    if use_qemu:
        try:
            if qemu_is_running():
                try:
                    qemu_process.terminate()
                    qemu_process.wait(timeout=5)
                except Exception:
                    try:
                        qemu_process.kill()
                    except Exception:
                        pass
        finally:
            cleanup_qmp_socket()
    else:
        try:
            subprocess.run(
                ["VBoxManage", "controlvm", VM_NAME, "poweroff"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=10
            )
        except Exception:
            pass


def build_livecd():
    """Build livecd using ninja before starting VM."""
    print(f"Building livecd in {BUILD_DIR}...")
    try:
        result = subprocess.run(
            ["ninja", "livecd"],
            cwd=BUILD_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=600
        )
        if result.returncode != 0:
            print(f"Build failed with return code {result.returncode}")
            return False
        print("Build completed successfully.")
        return True
    except Exception as e:
        print(f"Error building livecd: {e}")
        return False


def start_qemu(rpi_mode=False, smp_cores=1):
    """Start QEMU based on architecture."""
    global qemu_process, target_arch

    livecd_path = os.path.join(BUILD_DIR, "livecd.iso")
    
    # Reset log file and remove any stale monitor socket from a previous run.
    cleanup_qmp_socket()
    try:
        open(LOG_FILE, 'w').close()
    except Exception as e:
        print(f"Error resetting log file: {e}")
        return False

    # ---------------- ARM64 CONFIGURATION ----------------
    if target_arch == "arm64":
        is_darwin = platform.system() == "Darwin"
        if rpi_mode:
            mode_str = "RPI emulation (cortex-a76)"
        else:
            mode_str = "HVF accelerated (max)" if is_darwin else "CPU max"
        print(f"Starting QEMU (ARM64 - {mode_str})...")
        print(f"  SMP cores: {smp_cores}")

        # Darwin-specific configuration (macOS)
        if is_darwin:
            if rpi_mode:
                # Raspberry Pi emulation mode (cortex-a76, no HVF)
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", str(smp_cores),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "cortex-a76",
                    "-m", "4G",
                    "-drive", "if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
                    "-drive", f"if=virtio,media=cdrom,readonly=on,file={livecd_path}",
                    "-boot", "order=d,menu=on",
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}",
                    "-device", "qemu-xhci,id=xhci",
                    "-device", "usb-kbd,bus=xhci.0",
                    "-device", "usb-mouse,bus=xhci.0"
                ]
            else:
                # HVF accelerated mode (max CPU features)
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-accel", "hvf",
                    "-smp", str(smp_cores),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "max",
                    "-m", "4G",
                    "-drive", "if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
                    "-drive", f"if=virtio,media=cdrom,readonly=on,file={livecd_path}",
                    "-boot", "order=d,menu=on",
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}",
                    "-device", "qemu-xhci,id=xhci",
                    "-device", "usb-kbd,bus=xhci.0",
                    "-device", "usb-mouse,bus=xhci.0"
                ]
        else:
            # Linux/other systems
            if rpi_mode:
                # Raspberry Pi emulation mode
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", str(smp_cores),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "cortex-a76",
                    "-m", "4G",
                    "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
                    "-drive", f"if=virtio,media=cdrom,readonly=on,file={livecd_path}",
                    "-boot", "order=d,menu=on",
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}",
                    "-device", "qemu-xhci,id=xhci",
                    "-device", "usb-kbd,bus=xhci.0",
                    "-device", "usb-mouse,bus=xhci.0"
                ]
            else:
                # Default accelerated mode
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", str(smp_cores),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "max",
                    "-m", "4G",
                    "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
                    "-drive", f"if=virtio,media=cdrom,readonly=on,file={livecd_path}",
                    "-boot", "order=d,menu=on",
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}",
                    "-device", "qemu-xhci,id=xhci",
                    "-device", "usb-kbd,bus=xhci.0",
                    "-device", "usb-mouse,bus=xhci.0"
                ]

        qemu_cmd += ["-qmp", f"unix:{QMP_SOCKET},server=on,wait=off"]
        print(f"  QMP socket: {QMP_SOCKET}")
        print(f"  Command: {' '.join(qemu_cmd)}")

        try:
            # Output stdout/stderr to console, but serial is redirected via the command argument
            qemu_process = subprocess.Popen(
                qemu_cmd,
                cwd=BUILD_DIR,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL
            )
            print(f"QEMU ARM64 started with PID {qemu_process.pid}")
            return True
        except Exception as e:
            print(f"Error starting QEMU ARM64: {e}")
            return False

    # ---------------- X86 / X64 CONFIGURATION ----------------
    # Linux amd64 uses a fixed legacy-style command profile; i386 uses BIOS; macOS amd64 keeps UEFI path.
    qemu_binary = "qemu-system-i386" if target_arch == "i386" else "qemu-system-x86_64"
    is_darwin = platform.system() == "Darwin"
    amd64_linux_custom = (target_arch == "amd64" and not is_darwin)
    use_uefi = (target_arch != "i386") and not amd64_linux_custom
    ovmf_code = None
    ovmf_vars = None
    darwin_amd64_simple = use_uefi and is_darwin and target_arch == "amd64"

    if use_uefi:
        ovmf_code, ovmf_vars, ovmf_vars_template, code_candidates, vars_candidates = resolve_ovmf_paths(target_arch)

    print(f"Starting QEMU ({target_arch}) with xHCI USB...")
    print(f"  QEMU binary: {qemu_binary}")
    if amd64_linux_custom:
        print("  Boot mode: Legacy/Custom (Linux amd64)")
    else:
        print(f"  Boot mode: {'UEFI' if use_uefi else 'BIOS'}")
    print(f"  SMP cores: {smp_cores}")
    if use_uefi:
        print(f"  OVMF CODE: {ovmf_code}")
        if darwin_amd64_simple:
            print("  OVMF VARS: (not used on macOS amd64)")
        else:
            print(f"  OVMF VARS: {ovmf_vars}")
    print(f"  LiveCD: {livecd_path}")
    if amd64_linux_custom:
        print(f"  NTFS disk: {NTFS_IMG}")
        print(f"  FAT32 disk: {FAT32_IMG}")
    else:
        print(f"  FAT32 USB disk: {FAT32_IMG}")
    print(f"  Serial output: {LOG_FILE}")

    # Verify OVMF firmware exists (only for UEFI boot)
    if use_uefi:
        if not ovmf_code or not os.path.exists(ovmf_code):
            print(f"Error: OVMF CODE not found: {ovmf_code}")
            print("Set REACTOS_OVMF_CODE or OVMF_CODE to override.")
            print(f"Tried: {', '.join(code_candidates)}")
            print("Install ovmf package: sudo apt install ovmf")
            return False
        if not darwin_amd64_simple:
            if not prepare_ovmf_vars(ovmf_vars_template, ovmf_vars):
                return False

    try:
        log_fd = None

        if amd64_linux_custom:
            # Linux amd64 ntfslx test profile:
            # qemu-system-x86_64 -m 4G -cpu host,+vmx -enable-kvm \
            #   -drive file=./livecd.iso -serial file:/tmp/v.log \
            #   -M q35 -drive file=./ntfs.img,format=raw -drive file=./fat32.img,format=raw
            qemu_cmd = [
                qemu_binary,
                "-m", "4G",
                "-cpu", "host,+vmx",
                "-enable-kvm",
                "-drive", "file=./livecd.iso",
                "-serial", f"file:{LOG_FILE}",
                "-display", "none",
                "-M", "q35",
                "-drive", "file=./ntfs.img,format=raw",
                "-drive", "file=./fat32.img,format=raw",
                "-smp", str(smp_cores),
            ]
        elif use_uefi:
            # Open log file for stdout redirection (UEFI uses serial stdio)
            log_fd = open(LOG_FILE, 'w')
            if darwin_amd64_simple:
                # macOS amd64: Homebrew EDK2 code-only + IDE CD-ROM
                qemu_cmd = [
                    qemu_binary,
                    "-M", "q35",
                    "-smp", str(smp_cores),
                    "-m", "3G",
                    "-drive", f"file={livecd_path},format=raw,if=ide,index=0,media=cdrom",
                    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
                    "-serial", "stdio",
                    "-device", "qemu-xhci,id=usbxhci",
                    "-device", "usb-kbd,bus=usbxhci.0",
                    "-device", "usb-mouse,bus=usbxhci.0",
                    "-device", "virtio-scsi-pci,id=scsi0",
                ]
            else:
                # UEFI boot for amd64 (non-macOS defaults)
                qemu_cmd = [
                    qemu_binary,
                    "-smp", str(smp_cores),
                    "-m", "3G",
                    "-M", "q35",
                    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
                    "-cdrom", livecd_path,
                    "-boot", "d",  # Boot from CD-ROM
                    "-serial", "stdio",
                    "-display", "none",
                    "-no-reboot",
                    "-no-shutdown",
                    "-device", "qemu-xhci,id=xhci",
                    "-device", "usb-kbd,bus=xhci.0",
                    "-device", "usb-mouse,bus=xhci.0",
                    "-drive", f"if=none,id=usbdisk,format=raw,file={FAT32_IMG}",
                    "-device", "usb-storage,bus=xhci.0,drive=usbdisk"
                ]
        else:
            # BIOS boot for i386
            qemu_cmd = [
                qemu_binary,
                "-M", "q35",
                "-smp", str(smp_cores),
                "-m", "3G",
                "-drive", f"file={livecd_path},format=raw,if=ide,index=0,media=cdrom",
                "-boot", "order=d",
                "-serial", f"file:{LOG_FILE}",
                "-device", "qemu-xhci,id=xhci",
                "-device", "usb-kbd,bus=xhci.0",
                "-device", "usb-mouse,bus=xhci.0",
                "-drive", f"if=none,id=usbdisk,format=raw,file={FAT32_IMG}",
                "-device", "usb-storage,bus=xhci.0,drive=usbdisk"
            ]

        # Add acceleration: TCG on macOS, KVM on Linux.
        # amd64_linux_custom has -enable-kvm baked into the command list already.
        if is_darwin:
            qemu_cmd.insert(1, "-accel")
            qemu_cmd.insert(2, "tcg")
        elif not amd64_linux_custom:
            qemu_cmd.insert(1, "-enable-kvm")

        qemu_cmd += ["-qmp", f"unix:{QMP_SOCKET},server=on,wait=off"]
        print(f"  QMP socket: {QMP_SOCKET}")
        print(f"  Command: {' '.join(qemu_cmd)}")

        if use_uefi and log_fd is not None:
            # stdio serial mode: redirect host stdio to log file
            qemu_process = subprocess.Popen(
                qemu_cmd,
                cwd=BUILD_DIR,
                stdout=log_fd,
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL
            )
        else:
            # amd64_linux_custom / BIOS mode: serial goes directly to file via QEMU
            qemu_process = subprocess.Popen(
                qemu_cmd,
                cwd=BUILD_DIR,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                stdin=subprocess.DEVNULL
            )

        print(f"QEMU started with PID {qemu_process.pid}")
        return True

    except Exception as e:
        print(f"Error starting QEMU: {e}")
        return False


def start_vbox():
    """Start VirtualBox VM in headless mode."""
    print(f"Starting VirtualBox VM '{VM_NAME}' (headless)...")
    try:
        subprocess.Popen(
            ["VBoxManage", "startvm", VM_NAME, "--type", "headless"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        print(f"VM '{VM_NAME}' start command issued.")
        return True
    except Exception as e:
        print(f"Error starting VM: {e}")
        return False


def start_vm(rpi_mode=False, smp_cores=1):
    """Start the VM (QEMU or VirtualBox)."""
    if use_qemu:
        return start_qemu(rpi_mode, smp_cores)
    else:
        return start_vbox()


def get_file_size(filepath):
    try:
        return os.path.getsize(filepath)
    except OSError:
        return -1


def check_log_contents(filepath):
    try:
        with open(filepath, 'r', errors='ignore') as f:
            content = f.read()
        has_xhci = "usbxhci" in content.lower() or "xhci" in content.lower()
        has_usb = "usb" in content.lower() and "device" in content.lower()
        return has_xhci, has_usb
    except Exception:
        return False, False


def append_line_to_log(filepath, line):
    try:
        with open(filepath, 'a') as f:
            f.write(f"{line}\n")
        return True
    except Exception:
        return False


def sanitize_log_file(filepath):
    """Remove noisy runtime lines that should not be part of result logs."""
    try:
        with open(filepath, 'r', errors='ignore') as f:
            lines = f.readlines()
    except Exception:
        return False

    filtered = []
    for line in lines:
        lower = line.lower()
        if "qemu-system-" in lower and "terminating on signal" in lower:
            continue
        if "stall detected after" in lower:
            continue
        if "ected after" in lower and "xhci=" in lower and "usb=" in lower:
            continue
        filtered.append(line)

    try:
        with open(filepath, 'w') as f:
            f.writelines(filtered)
        return True
    except Exception:
        return False


def emit_boot_status(success):
    status = BOOT_SUCCESS_STATUS if success else BOOT_FAILURE_STATUS
    line = f"[{status}]"
    print(line)
    append_line_to_log(LOG_FILE, line)


def has_boot_success_marker(filepath):
    try:
        with open(filepath, 'r', errors='ignore') as f:
            return BOOT_SUCCESS_MARKER in f.read()
    except Exception:
        return False


def get_missing_markers(filepath, markers):
    if not markers:
        return []
    try:
        with open(filepath, 'r', errors='ignore') as f:
            content = f.read()
    except Exception:
        return list(markers)
    return [marker for marker in markers if marker not in content]


def validate_probe_results(log_contents, strict=False):
    issues = []

    def require(token, label):
        if token not in log_contents:
            issues.append(f"missing {label} probe result: {token}")

    require("probe-result security=pass", "security")
    require("probe-result logfile=pass", "logfile")

    if "probe-result ea=pass" in log_contents:
        pass
    elif "probe-result ea=unsupported" in log_contents:
        if strict:
            issues.append("EA remains unsupported")
    else:
        issues.append("missing EA probe result")

    if "probe-result reparse=pass" in log_contents:
        pass
    elif "probe-result reparse=unsupported" in log_contents:
        if strict:
            issues.append("reparse remains unsupported")
    else:
        issues.append("missing reparse probe result")

    if "probe-result dirty-volume=dirty" in log_contents:
        pass
    elif "probe-result dirty-volume=unsupported" in log_contents:
        if strict:
            issues.append("dirty-volume query remains unsupported")
    elif "probe-result dirty-volume=clean" in log_contents:
        issues.append("dirty-volume probe unexpectedly reported a clean mounted volume")
    else:
        issues.append("missing dirty-volume probe result")

    require("probe-result volume-lock=pass", "volume-lock")
    require("probe-result volume-dismount=pass", "volume-dismount")

    return issues


def is_ntfslx_profile():
    return (SHORT_CYCLE or
            any(marker.startswith("NTFSLX-AUTO:") for marker in COMPLETION_MARKERS))


def summarize_guest_benchmarks(log_contents):
    """Print the guest FAT32 vs NTFS benchmark summary if present in the log."""
    result_re = re.compile(
        r"NTFSLX-AUTO: bench result fs='([^']+)' drive='([^']+)' bytes=(\d+) "
        r"write_mib_s=(\d+)\.(\d{2}) read_mib_s=(\d+)\.(\d{2}) cluster=(\d+) "
        r"write_ms=(\d+)\.(\d{2}) read_ms=(\d+)\.(\d{2})"
    )
    ratio_re = re.compile(
        r"NTFSLX-AUTO: bench compare ntfs_vs_fat32 write_ratio=(\d+)\.(\d{2}) "
        r"read_ratio=(\d+)\.(\d{2})"
    )

    results = []
    for match in result_re.finditer(log_contents):
        results.append({
            "fs": match.group(1),
            "drive": match.group(2),
            "bytes": int(match.group(3)),
            "write": f"{match.group(4)}.{match.group(5)}",
            "read": f"{match.group(6)}.{match.group(7)}",
            "cluster": int(match.group(8)),
            "write_ms": f"{match.group(9)}.{match.group(10)}",
            "read_ms": f"{match.group(11)}.{match.group(12)}",
        })

    if not results:
        return

    results.sort(key=lambda item: (item["fs"].upper() != "NTFS", item["drive"]))

    print("Guest benchmark summary:")
    for result in results:
        mib = result["bytes"] // (1024 * 1024)
        print(
            f"  {result['fs']} {result['drive']} {mib} MiB: "
            f"write={result['write']} MiB/s ({result['write_ms']} ms), "
            f"read={result['read']} MiB/s ({result['read_ms']} ms), "
            f"cluster={result['cluster']}"
        )

    ratio = ratio_re.search(log_contents)
    if ratio:
        print(
            "  NTFS/FAT32 ratio: "
            f"write={ratio.group(1)}.{ratio.group(2)}x "
            f"read={ratio.group(3)}.{ratio.group(4)}x"
        )


def monitor_log():
    """Monitor log file for stalls and enforce hard timeout."""
    global qemu_process, use_qemu, SHUTDOWN_GRACE

    stall_count = 0
    last_size = -1
    last_change_time = time.time()
    
    overall_start_time = time.time()

    print(f"Monitoring log file: {LOG_FILE}")
    print(f"Stall timeout: {STALL_TIMEOUT} seconds")
    print(f"Hard timeout: {HARD_TIMEOUT} seconds")

    wait_count = 0
    while get_file_size(LOG_FILE) <= 0:
        if time.time() - overall_start_time > HARD_TIMEOUT:
            print(f"HARD TIMEOUT ({HARD_TIMEOUT}s) reached waiting for log.")
            force_kill_vm()
            return

        if wait_count % 10 == 0:
            print(f"Waiting for log output... ({int(time.time() - overall_start_time)}s)")
        wait_count += 1
        time.sleep(0.5)
        
        if use_qemu and qemu_process and qemu_process.poll() is not None:
            print(f"QEMU exited with code {qemu_process.returncode}")
            return

    print(f"Log file has content. Starting monitor...\n")
    last_size = get_file_size(LOG_FILE)
    last_change_time = time.time()

    try:
        while True:
            time.sleep(0.5)

            # 1. Check Hard Timeout
            total_runtime = time.time() - overall_start_time
            if total_runtime > HARD_TIMEOUT:
                print(f"HARD TIMEOUT REACHED! Running for {total_runtime:.1f} seconds.")
                force_kill_vm()
                return

            # 2. Check Process Status (QEMU only)
            if use_qemu and qemu_process and qemu_process.poll() is not None:
                print(f"QEMU exited with code {qemu_process.returncode}")
                return

            # 3. Check Log Stall
            current_size = get_file_size(LOG_FILE)
            current_time = time.time()

            if current_size != last_size:
                last_size = current_size
                last_change_time = current_time

            if COMPLETION_MARKERS:
                missing = get_missing_markers(LOG_FILE, COMPLETION_MARKERS)
                if not missing:
                    print("Completion markers reached; requesting clean guest shutdown.")
                    if use_qemu:
                        requested = request_qemu_powerdown()
                        if not requested:
                            print("QEMU powerdown request failed; waiting for natural exit anyway.")
                        if wait_for_qemu_exit(SHUTDOWN_GRACE):
                            return
                        print(f"Guest did not exit within {SHUTDOWN_GRACE} seconds after completion; forcing power off.")
                        force_kill_vm()
                        return
                    print("Completion markers reached; stopping monitor.")
                    return

            stall_duration = current_time - last_change_time
            if stall_duration >= STALL_TIMEOUT:
                print(f"STALL DETECTED! Log unchanged for {stall_duration:.1f} seconds")

                has_xhci, has_usb = check_log_contents(LOG_FILE)
                print(f"Stall details: XHCI={has_xhci}, USB={has_usb}")

                force_kill_vm()
                return

    except KeyboardInterrupt:
        print("\nMonitoring interrupted.")


def signal_handler(sig, frame):
    force_kill_vm()
    sys.exit(0)


def main():
    global use_qemu, target_arch, STALL_TIMEOUT, HARD_TIMEOUT, SHUTDOWN_GRACE, COMPLETION_MARKERS, STRICT_PROBE_GATING, SHORT_CYCLE

    parser = argparse.ArgumentParser(description='VM Monitor Script')
    parser.add_argument('--qemu', action='store_true', help='Use QEMU instead of VirtualBox')
    parser.add_argument('--vbox', action='store_true', help='Use VirtualBox (default behavior)')
    parser.add_argument('--rpi', action='store_true', help='Use Raspberry Pi emulation mode (cortex-a76, no HVF)')
    parser.add_argument('--smp', type=int, default=1, help='Number of virtual CPU cores for QEMU (default: 1)')
    parser.add_argument('--stall-timeout', type=int, default=DEFAULT_STALL_TIMEOUT, help='Log inactivity timeout in seconds')
    parser.add_argument('--hard-timeout', type=int, default=DEFAULT_HARD_TIMEOUT, help='Overall runtime timeout in seconds')
    parser.add_argument('--shutdown-grace', type=int, default=DEFAULT_SHUTDOWN_GRACE, help='Seconds to wait for guest shutdown after completion markers are reached')
    parser.add_argument('--completion-marker', action='append', default=[], help='Stop once this log marker is present; may be specified multiple times')
    parser.add_argument('--skip-build', action='store_true', help='Reuse the existing build artifacts instead of rebuilding livecd')
    parser.add_argument('--skip-host-verify', action='store_true', help='Skip host-side NTFS image verification after QEMU completes')
    parser.add_argument('--strict-probe-gating', action='store_true', help='Treat unsupported blocker-probe results as failures')
    parser.add_argument('--short-cycle', action='store_true', help='Stop after blocker probes complete, before the long persistence pass')
    parser.add_argument('--kmtest', action='store_true', help='Use the livecd kmtest autorun completion marker and skip NTFSLX probe validation')
    args = parser.parse_args()

    if args.smp < 1:
        parser.error("--smp must be >= 1")
    if args.stall_timeout < 1:
        parser.error("--stall-timeout must be >= 1")
    if args.hard_timeout < 1:
        parser.error("--hard-timeout must be >= 1")

    STALL_TIMEOUT = args.stall_timeout
    HARD_TIMEOUT = args.hard_timeout
    SHUTDOWN_GRACE = args.shutdown_grace
    COMPLETION_MARKERS = list(args.completion_marker)
    STRICT_PROBE_GATING = args.strict_probe_gating
    SHORT_CYCLE = args.short_cycle

    # --vbox is explicit but same as default (no --qemu)
    use_qemu = args.qemu and not args.vbox

    # Detect target architecture from CWD
    target_arch = detect_target_arch()

    if args.kmtest and not COMPLETION_MARKERS:
        COMPLETION_MARKERS = [KMTEST_AUTO_SUCCESS_MARKER]
    elif SHORT_CYCLE:
        COMPLETION_MARKERS = [NTFSLX_BLOCKER_MARKER]
    elif not COMPLETION_MARKERS and use_qemu and target_arch == "amd64" and platform.system() != "Darwin":
        COMPLETION_MARKERS = ["NTFSLX-AUTO: COMPLETE"]
    
    # Force QEMU for ARM64 (VirtualBox does not exist for arm64)
    if target_arch == "arm64":
        use_qemu = True

    atexit.register(force_kill_vm)
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    vm_type = f"QEMU ({target_arch})" if use_qemu else "VirtualBox"
    if args.rpi:
        vm_type += " - RPI Mode"
    print("="*60)
    print(f"VM Monitor Script ({vm_type})")
    print(f"Build directory: {BUILD_DIR}")
    print("="*60 + "\n")

    if args.skip_build:
        print("Skipping livecd build (--skip-build).")
    elif not build_livecd():
        sys.exit(1)

    # Cleanup: Stop previous QEMU instances (skip on darwin/macOS)
    is_darwin = platform.system() == "Darwin"

    if not is_darwin:
        if target_arch != "arm64":
            print("Ensuring previous QEMU instances are stopped...")
            try:
                subprocess.run("pkill -9 -u \"$USER\" -f qemu-system-x86_64", shell=True,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                subprocess.run("pkill -9 -u \"$USER\" -f qemu-system-i386", shell=True,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass
        else:
            # Simple cleanup for aarch64
            try:
                subprocess.run("pkill -9 -f qemu-system-aarch64", shell=True,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass

    if use_qemu and target_arch != "arm64":
        if not create_fat32_img():
            print("Warning: Could not create FAT32 image...")
        if target_arch == "amd64" and platform.system() != "Darwin":
            if not create_ntfs_img():
                print("Warning: Could not create NTFS image...")

    if not start_vm(rpi_mode=args.rpi, smp_cores=args.smp):
        sys.exit(1)

    time.sleep(2)

    monitor_log()

    boot_succeeded = True
    if use_qemu:
        boot_succeeded = has_boot_success_marker(LOG_FILE)

    if not use_qemu or qemu_is_running():
        force_kill_vm()
    else:
        cleanup_qmp_socket()
    sanitize_log_file(LOG_FILE)

    if use_qemu:
        emit_boot_status(boot_succeeded)

    if use_qemu and COMPLETION_MARKERS:
        missing = get_missing_markers(LOG_FILE, COMPLETION_MARKERS)
        if missing:
            print("Missing completion markers:")
            for marker in missing:
                print(f"  - {marker}")
            sys.exit(1)

    if use_qemu and not boot_succeeded:
        sys.exit(1)

    if use_qemu and is_ntfslx_profile():
        log_contents = read_log_contents(LOG_FILE)
        summarize_guest_benchmarks(log_contents)
        probe_issues = validate_probe_results(log_contents, strict=STRICT_PROBE_GATING)
        if probe_issues:
            print("Probe validation failed:")
            for issue in probe_issues:
                print(f"  - {issue}")
            sys.exit(1)

    if (use_qemu and not SHORT_CYCLE and not args.skip_host_verify and target_arch == "amd64" and
            "NTFSLX-AUTO: COMPLETE" in COMPLETION_MARKERS):
        log_contents = read_log_contents(LOG_FILE)
        if NTFSLX_AUTO_SUCCESS_MARKER not in log_contents:
            print(f"Missing successful autorun marker: {NTFSLX_AUTO_SUCCESS_MARKER}")
            sys.exit(1)
        try:
            verify_ntfslx_persistent_image()
        except Exception as e:
            print(f"Host NTFS verification failed: {e}")
            sys.exit(1)


if __name__ == "__main__":
    main()
